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

// Returns the number of bytes of the last UTF-8 code point in `s`
// (0 if empty).
size_t LastUtf8CharBytes(const std::string& s) {
  if (s.empty()) return 0;
  size_t i = s.size();
  while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
  return s.size() - (i > 0 ? i - 1 : 0);
}

// Returns the number of bytes of the first UTF-8 code point in `s`.
size_t FirstUtf8CharBytes(const std::string& s) {
  if (s.empty()) return 0;
  size_t i = 1;
  while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
  return i;
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
  // for Chinese (and literals), so the reading cursor doubles as a
  // code-point offset.
  DisplaySegments segments;
  std::string left;
  std::string right;
  SplitAtCodePoints(walked, grid_.cursor(), &left, &right);

  if (lastWasBare_ && !left.empty()) {
    // The just-inserted bare syllable (left of the cursor) is still
    // tone-retrofittable.
    size_t n = LastUtf8CharBytes(left);
    segments.unconfirmed = left.substr(left.size() - n);
    left.resize(left.size() - n);
  }
  segments.before = std::move(left);
  segments.unconfirmed += pending_.displayText();

  // The character right of the cursor is the selection anchor; emphasize
  // it so the user can see where digit-8 selection would start.
  if (!right.empty()) {
    size_t n = FirstUtf8CharBytes(right);
    segments.highlighted = right.substr(0, n);
    segments.after = right.substr(n);
  }
  return segments;
}

std::string Composer::composedText() const {
  DisplaySegments segments = displaySegments();
  return segments.before + segments.unconfirmed + segments.highlighted +
         segments.after;
}

std::string Composer::unconfirmedTail() const {
  return displaySegments().unconfirmed;
}

size_t Composer::candidatePageCount() const {
  if (candidates_.empty()) return 0;
  return (candidates_.size() + kCandidatePageSize - 1) / kCandidatePageSize;
}

std::vector<Formosa::Gramambular2::ReadingGrid::Candidate>
Composer::currentPageCandidates() const {
  std::vector<Formosa::Gramambular2::ReadingGrid::Candidate> page;
  const size_t start = pageIndex_ * kCandidatePageSize;
  for (size_t i = start;
       i < candidates_.size() && i < start + kCandidatePageSize; ++i) {
    page.push_back(candidates_[i]);
  }
  return page;
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

void Composer::moveCursor(int delta) {
  lastWasBare_ = false;  // moving away ends the tone-retrofit window
  const size_t cursor = grid_.cursor();
  const size_t length = grid_.length();
  if (length == 0) return;
  if (delta < 0) {
    grid_.setCursor(cursor == 0 ? length : cursor - 1);
  } else {
    grid_.setCursor(cursor >= length ? 0 : cursor + 1);
  }
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
  pageIndex_ = 0;
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
  // Space, control digits (6-0), everything else printable.
  return composing;
}

Composer::Result Composer::feedChar(char c) {
  if (state_ == State::kSelecting) {
    if (c >= '1' && c <= '6') {
      return selectOnCurrentPage(static_cast<size_t>(c - '1'));
    }
    if (c == '7') {  // previous page (no wrap)
      if (pageIndex_ > 0) --pageIndex_;
      return {true, ""};
    }
    if (c == '8') {  // next page (no wrap)
      if (pageIndex_ + 1 < candidatePageCount()) ++pageIndex_;
      return {true, ""};
    }
    // Any other key closes the menu and then performs its normal function.
    dismissMenu();
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
    // Not a tone position: fall through to the control-digit handling below.
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

  // Digits while composing are controls, never literal text:
  //   8 opens the menu, 9/0 move the cursor (wrapping), the rest no-op.
  // When idle they pass through to the application.
  if (c >= '0' && c <= '9') {
    if (!composing) return {false, ""};
    switch (c) {
      case '8':
        return openCandidateMenu();
      case '9':
        moveCursor(-1);
        return {true, ""};
      case '0':
        moveCursor(+1);
        return {true, ""};
      default:  // 6, 7 and non-tone-position 1-5
        return {true, ""};
    }
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

  // Everything else printable: pass through when idle; eaten while
  // composing (digits are controls now, so nothing may leak mid-buffer).
  if (!composing) return {false, ""};
  return {true, ""};
}

Composer::Result Composer::feedBackspace() {
  if (state_ == State::kSelecting) {
    // Close the menu, then delete as usual.
    dismissMenu();
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
    // Close the menu, then commit as usual.
    dismissMenu();
  }
  if (state_ == State::kEmpty) return {false, ""};
  return {true, takeCommitText()};
}

Composer::Result Composer::feedEsc() {
  if (state_ == State::kEmpty) return {false, ""};
  // Selecting or Composing: cancel the whole composition (the menu, if
  // open, closes as part of the reset).
  reset();
  return {true, ""};
}

Composer::Result Composer::closeCandidateMenu() {
  if (state_ != State::kSelecting) return {false, ""};
  dismissMenu();
  return {true, ""};
}

Composer::Result Composer::openCandidateMenu() {
  if (pending_.complete() && !finalizePendingBare()) return {true, ""};
  if (grid_.length() == 0) return {true, ""};

  // Select at the cursor: candidatesAt targets the character right of the
  // cursor and falls back to the last character at the end of the buffer.
  selectionLocation_ = grid_.cursor();
  candidates_ = grid_.candidatesAt(selectionLocation_);
  if (candidates_.empty()) return {true, ""};
  lastWasBare_ = false;
  pageIndex_ = 0;
  state_ = State::kSelecting;
  return {true, ""};
}

void Composer::dismissMenu() {
  candidates_.clear();
  pageIndex_ = 0;
  state_ = State::kComposing;
}

Composer::Result Composer::selectOnCurrentPage(size_t indexInPage) {
  const size_t index = pageIndex_ * kCandidatePageSize + indexInPage;
  if (indexInPage >= kCandidatePageSize || index >= candidates_.size()) {
    return {true, ""};  // number without a candidate on this page: no-op
  }
  return selectCandidate(index);
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

  dismissMenu();
  return {true, ""};
}

}  // namespace mspy
