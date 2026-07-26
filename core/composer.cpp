#include "composer.h"

#include <utility>

namespace mspy {

namespace {

// Punctuation committed directly (docs/spec.md §6).
const char* DirectPunctuation(char c) {
  switch (c) {
    case ',': return "，";
    case '.': return "。";
    default: return nullptr;
  }
}

const char* ToneMark(char digit) {
  switch (digit) {
    case '1': return kToneSentinel1;
    case '2': return kTone2;
    case '3': return kTone3;
    case '4': return kTone4;
    case '5': return kTone5;
    default: return nullptr;
  }
}

}  // namespace

Composer::Composer(std::shared_ptr<RelaxedToneLM> lm)
    : lm_(lm), grid_(std::move(lm)) {
  grid_.setReadingSeparator("-");
}

std::string Composer::composedText() const {
  std::string text;
  for (const auto& v : walk_.valuesAsStrings()) text += v;
  text += pending_.displayText();
  return text;
}

bool Composer::insertReading(const std::string& reading) {
  if (!lm_->hasUnigrams(reading)) return false;
  if (!grid_.insertReading(reading)) return false;
  walk_ = grid_.walk();
  return true;
}

bool Composer::finalizePendingBare() {
  if (!pending_.complete()) return false;
  for (const auto& syllable : pending_.candidates()) {
    if (insertReading(syllable)) {  // bare reading = tone 1 or neutral
      lastBareSyllables_ = pending_.candidates();
      pending_.clear();
      return true;
    }
  }
  return false;
}

bool Composer::applyToneToPending(char digit) {
  const char* mark = ToneMark(digit);
  if (mark == nullptr || !pending_.complete()) return false;
  for (const auto& syllable : pending_.candidates()) {
    if (insertReading(syllable + mark)) {
      pending_.clear();
      return true;
    }
  }
  return false;
}

bool Composer::retrofitToneToLastSyllable(char digit) {
  const char* mark = ToneMark(digit);
  if (mark == nullptr || grid_.length() == 0) return false;
  grid_.deleteReadingBeforeCursor();
  for (const auto& syllable : lastBareSyllables_) {
    if (insertReading(syllable + mark)) return true;
  }
  // No entry for that tone: restore the bare syllable and stay retrofittable.
  for (const auto& syllable : lastBareSyllables_) {
    if (insertReading(syllable)) return false;
  }
  walk_ = grid_.walk();  // should be unreachable; keep the walk consistent
  return false;
}

std::string Composer::takeCommitText() {
  // A complete-but-toneless syllable joins the commit; a half-typed or
  // tone-awaiting-only syllable is dropped (as Windows Bopomofo drops
  // trailing unconverted bopomofo).
  if (pending_.complete()) finalizePendingBare();
  std::string text;
  for (const auto& v : walk_.valuesAsStrings()) text += v;
  reset();
  return text;
}

void Composer::reset() {
  grid_.clear();
  walk_ = {};
  pending_.clear();
  candidates_.clear();
  lastBareSyllables_.clear();
  lastWasBare_ = false;
  selectionLocation_ = 0;
  state_ = State::kEmpty;
}

void Composer::updateStateAfterMutation() {
  if (grid_.length() == 0 && pending_.empty()) {
    state_ = State::kEmpty;
  } else if (state_ != State::kSelecting) {
    state_ = State::kComposing;
  }
}

Composer::Result Composer::feedChar(char c) {
  if (state_ == State::kSelecting) {
    if (c >= '1' && c <= '9') {
      return selectCandidate(static_cast<size_t>(c - '1'));
    }
    return {true, ""};  // eat everything else while the window is open
  }

  const bool composing = state_ == State::kComposing;

  // Tone digits: first choice is a pending syllable awaiting its tone;
  // otherwise retrofit the tone onto the just-inserted bare syllable.
  if (c >= '1' && c <= '5') {
    if (pending_.complete()) {
      if (applyToneToPending(c)) lastWasBare_ = false;
      updateStateAfterMutation();
      return {true, ""};
    }
    if (pending_.empty() && lastWasBare_) {
      if (retrofitToneToLastSyllable(c)) lastWasBare_ = false;
      updateStateAfterMutation();
      return {true, ""};
    }
    // Not a tone position: fall through to the literal-key handling below.
  }

  // Direct punctuation commits the buffer plus the full-width symbol.
  if (const char* punct = DirectPunctuation(c)) {
    std::string commit = composing ? takeCommitText() : "";
    commit += punct;
    reset();
    return {true, commit};
  }

  // Letters and ';' build syllables.
  if ((c >= 'a' && c <= 'z') || c == ';') {
    if (pending_.complete()) {
      // The syllable has no tone-1/neutral entry (eager finalize failed
      // earlier); a tone digit or backspace is required first.
      return {true, ""};
    }
    const bool wasEmpty = pending_.empty();
    if (!pending_.feed(c)) {
      // Invalid first key (';') or structurally impossible pair.
      if (composing) return {true, ""};
      return {false, ""};
    }
    lastWasBare_ = false;
    if (pending_.complete()) {
      // Eager finalize: show the converted character the moment the
      // syllable completes; a following tone digit retrofits the tone.
      if (finalizePendingBare()) {
        lastWasBare_ = true;
      }
      // else: keep the pending syllable on screen awaiting its tone.
    }
    (void)wasEmpty;
    state_ = State::kComposing;
    return {true, ""};
  }

  // Everything else (space, non-tone digits, unhandled punctuation):
  // pass through when idle; commit-then-emit while composing.
  if (!composing) return {false, ""};
  std::string commit = takeCommitText();
  commit.push_back(c);
  return {true, commit};
}

Composer::Result Composer::feedBackspace() {
  if (state_ == State::kSelecting) {
    state_ = State::kComposing;
    candidates_.clear();
    return {true, ""};
  }
  if (state_ == State::kEmpty) return {false, ""};

  if (!pending_.empty()) {
    pending_.backspace();
  } else if (grid_.length() > 0) {
    grid_.deleteReadingBeforeCursor();
    walk_ = grid_.walk();
  }
  lastWasBare_ = false;
  updateStateAfterMutation();
  return {true, ""};
}

Composer::Result Composer::feedEnter() {
  if (state_ == State::kSelecting) {
    state_ = State::kComposing;
    candidates_.clear();
    return {true, ""};
  }
  if (state_ == State::kEmpty) return {false, ""};
  return {true, takeCommitText()};
}

Composer::Result Composer::feedEsc() {
  switch (state_) {
    case State::kSelecting:
      state_ = State::kComposing;
      candidates_.clear();
      return {true, ""};
    case State::kComposing:
      reset();
      return {true, ""};
    case State::kEmpty:
    default:
      return {false, ""};
  }
}

Composer::Result Composer::feedDown() {
  if (state_ != State::kComposing) {
    return {state_ == State::kSelecting, ""};
  }
  if (pending_.complete() && !finalizePendingBare()) return {true, ""};
  if (grid_.length() == 0) return {true, ""};

  // Select at the span before the (end-of-buffer) cursor; candidatesAt
  // handles the end boundary internally.
  selectionLocation_ = grid_.cursor();
  candidates_ = grid_.candidatesAt(selectionLocation_);
  if (candidates_.empty()) return {true, ""};
  lastWasBare_ = false;
  state_ = State::kSelecting;
  return {true, ""};
}

Composer::Result Composer::feedUp() {
  if (state_ == State::kSelecting) {
    state_ = State::kComposing;
    candidates_.clear();
    return {true, ""};
  }
  return {false, ""};
}

Composer::Result Composer::selectCandidate(size_t index) {
  if (state_ != State::kSelecting || index >= candidates_.size()) {
    return {true, ""};
  }
  grid_.overrideCandidate(selectionLocation_, candidates_[index]);
  walk_ = grid_.walk();
  candidates_.clear();
  state_ = State::kComposing;
  return {true, ""};
}

}  // namespace mspy
