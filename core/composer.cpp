#include "composer.h"

#include <chrono>
#include <utility>

namespace mspy {

namespace {

// UserOverrideModel parameters, same as McBopomofo's.
constexpr size_t kUserOverrideModelCapacity = 500;
constexpr double kObservedOverrideHalfLife = 5400.0;  // 90 minutes

double NowSeconds() {
  using namespace std::chrono;
  return duration<double>(system_clock::now().time_since_epoch()).count();
}

// Punctuation committed directly as full-width symbols, following the
// user's Rime (rime-ice default) habits. Quotes are handled separately
// because they alternate between opening and closing forms.
const char* DirectPunctuation(char c) {
  switch (c) {
    case ',': return "，";
    case '.': return "。";
    case '?': return "？";
    case '!': return "！";
    case ':': return "：";
    case ';': return "；";  // only reachable when not part of a syllable
    case '\\': return "、";
    case '[': return "「";
    case ']': return "」";
    case '{': return "『";
    case '}': return "』";
    case '(': return "（";
    case ')': return "）";
    case '<': return "《";
    case '>': return "》";
    case '^': return "……";
    case '_': return "——";
    case '~': return "～";
    default: return nullptr;
  }
}

// Returns the number of bytes of the last UTF-8 code point in `s`
// (0 if empty).
size_t LastUtf8CharBytes(const std::string& s) {
  if (s.empty()) return 0;
  size_t i = s.size();
  while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
  return s.size() - (i > 0 ? i - 1 : 0);
}

// Splits `s` after `n` code points.
void SplitAtCodePoints(const std::string& s, size_t n, std::string* left,
                       std::string* right) {
  size_t i = 0, count = 0;
  while (i < s.size() && count < n) {
    ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    ++count;
  }
  *left = s.substr(0, i);
  *right = s.substr(i);
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
    : lm_(lm),
      grid_(std::move(lm)),
      uom_(kUserOverrideModelCapacity, kObservedOverrideHalfLife) {
  grid_.setReadingSeparator("-");
}

Composer::DisplaySegments Composer::displaySegments() const {
  std::string walked;
  for (const auto& v : walk_.valuesAsStrings()) walked += v;

  // Split the walked text at the cursor. Readings map 1:1 to characters
  // for Chinese, so the reading cursor doubles as a code-point offset.
  DisplaySegments segments;
  std::string left;
  SplitAtCodePoints(walked, grid_.cursor(), &left, &segments.after);

  if (lastWasBare_ && !left.empty()) {
    // The just-inserted bare syllable (left of the cursor) is still
    // tone-retrofittable.
    size_t n = LastUtf8CharBytes(left);
    segments.unconfirmed = left.substr(left.size() - n);
    left.resize(left.size() - n);
  }
  segments.before = std::move(left);
  segments.unconfirmed += pending_.displayText();
  return segments;
}

std::string Composer::composedText() const {
  DisplaySegments segments = displaySegments();
  return segments.before + segments.unconfirmed + segments.after;
}

std::string Composer::unconfirmedTail() const {
  return displaySegments().unconfirmed;
}

Composer::Result Composer::feedLeft() {
  if (state_ == State::kSelecting) return {true, ""};
  if (state_ != State::kComposing) return {false, ""};
  if (!pending_.empty()) return {true, ""};  // settle the syllable first
  lastWasBare_ = false;  // moving away ends the tone-retrofit window
  size_t cursor = grid_.cursor();
  if (cursor > 0) grid_.setCursor(cursor - 1);
  return {true, ""};
}

Composer::Result Composer::feedRight() {
  if (state_ == State::kSelecting) return {true, ""};
  if (state_ != State::kComposing) return {false, ""};
  if (!pending_.empty()) return {true, ""};
  lastWasBare_ = false;
  size_t cursor = grid_.cursor();
  if (cursor < grid_.length()) grid_.setCursor(cursor + 1);
  return {true, ""};
}

bool Composer::insertReading(const std::string& reading) {
  if (!lm_->hasUnigrams(reading)) return false;
  if (!grid_.insertReading(reading)) return false;
  walk_ = grid_.walk();

  // Re-apply learned preferences around the just-inserted reading
  // (mirrors McBopomofo's KeyHandler pattern).
  const double now = NowSeconds();
  auto suggestion = uom_.suggest(walk_, grid_.cursor(), now);
  if (!suggestion.empty()) {
    for (const auto& candidate : grid_.candidatesAt(grid_.cursor())) {
      if (candidate.value == suggestion.candidate) {
        grid_.overrideCandidate(
            grid_.cursor(), candidate,
            suggestion.forceHighScoreOverride
                ? Formosa::Gramambular2::ReadingGrid::Node::OverrideType::
                      kOverrideValueWithHighScore
                : Formosa::Gramambular2::ReadingGrid::Node::OverrideType::
                      kOverrideValueWithScoreFromTopUnigram);
        walk_ = grid_.walk();
        break;
      }
    }
  }
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

bool Composer::wouldConsume(char c) const {
  if (state_ == State::kSelecting) return true;
  const bool composing = state_ == State::kComposing;
  if (c >= 'a' && c <= 'z') return true;
  if (DirectPunctuation(c) != nullptr || c == '"' || c == '\'') return true;
  if (c >= '1' && c <= '5') {
    if (pending_.complete()) return true;
    if (pending_.empty() && lastWasBare_) return true;
    return composing;
  }
  // Space, other digits, everything else printable.
  return composing;
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

  // Letters and ';' build syllables. (';' doubles as the ing final key;
  // when it cannot extend a syllable it falls through to punctuation.)
  if ((c >= 'a' && c <= 'z') || c == ';') {
    if (pending_.complete()) {
      // The syllable has no tone-1/neutral entry (eager finalize failed
      // earlier); a tone digit or backspace is required first.
      return {true, ""};
    }
    if (pending_.feed(c)) {
      lastWasBare_ = false;
      if (pending_.complete()) {
        // Eager finalize: show the converted character the moment the
        // syllable completes; a following tone digit retrofits the tone.
        if (finalizePendingBare()) {
          lastWasBare_ = true;
        }
        // else: keep the pending syllable on screen awaiting its tone.
      }
      state_ = State::kComposing;
      return {true, ""};
    }
    if (c != ';') {
      // Structurally impossible pair: reject the key while composing.
      if (composing) return {true, ""};
      return {false, ""};
    }
    // fall through: lone ';' becomes full-width punctuation
  }

  // Quotes alternate between opening and closing forms.
  if (c == '"' || c == '\'') {
    bool& open = (c == '"') ? doubleQuoteOpen_ : singleQuoteOpen_;
    const char* symbol =
        (c == '"') ? (open ? "”" : "“") : (open ? "’" : "‘");
    open = !open;
    std::string commit = composing ? takeCommitText() : "";
    commit += symbol;
    reset();
    return {true, commit};
  }

  // Direct punctuation commits the buffer plus the full-width symbol.
  if (const char* punct = DirectPunctuation(c)) {
    std::string commit = composing ? takeCommitText() : "";
    commit += punct;
    reset();
    return {true, commit};
  }

  // Space commits the composition as-is (like Enter); a plain space when
  // idle passes through to the application.
  if (c == ' ') {
    if (!composing) return {false, ""};
    return {true, takeCommitText()};
  }

  // Everything else (non-tone digits, unhandled printable): pass through
  // when idle; commit-then-emit while composing.
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
  const auto& chosen = candidates_[index];
  const auto walkBefore = walk_;
  grid_.overrideCandidate(selectionLocation_, chosen);
  walk_ = grid_.walk();
  uom_.observe(walkBefore, walk_, selectionLocation_, NowSeconds());

  if (onManualSelection) {
    // Strip the internal explicit-tone-1 sentinel before handing the
    // reading key to the shell for persistence.
    std::string reading = chosen.reading;
    size_t pos;
    while ((pos = reading.find(kToneSentinel1)) != std::string::npos) {
      reading.erase(pos, sizeof(kToneSentinel1) - 1);
    }
    onManualSelection(reading, chosen.value);
  }

  candidates_.clear();
  state_ = State::kComposing;
  return {true, ""};
}

}  // namespace mspy
