#include "composer.h"

#include <chrono>
#include <utility>

#include "double_pinyin.h"

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

// Maps a digit key to the tone it types. Tones are reachable with either
// hand (2026-08-04): the left hand uses 1-5 as always, and the right hand
// uses 0-6 mirrored around the gap between 5 and 6, so tone 4 is '7' and
// tone 1 is '0'. Returns 0 for a key that is not a digit.
char ToneDigit(char c) {
  if (c >= '1' && c <= '5') return c;
  switch (c) {
    case '0': return '1';
    case '9': return '2';
    case '8': return '3';
    case '7': return '4';
    case '6': return '5';
    default: return 0;
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

  if (unsettled_.active && !left.empty()) {
    // The just-inserted syllable (left of the cursor) is unsettled: show
    // the bopomofo the user typed -- tone mark and all -- not the character
    // it would convert to. Space, punctuation, the next syllable or any
    // cursor move settles it into that character.
    size_t n = LastUtf8CharBytes(left);
    segments.unconfirmed = unsettled_.display.empty()
                               ? left.substr(left.size() - n)
                               : unsettled_.display;
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

void Composer::insertLiteralText(const std::string& utf8) {
  // One literal reading per code point keeps the reading/character 1:1
  // mapping the cursor and display math rely on. Literals have a single
  // fixed candidate, so no UOM/suggestion pass is needed.
  size_t i = 0;
  while (i < utf8.size()) {
    size_t j = i + 1;
    while (j < utf8.size() &&
           (static_cast<unsigned char>(utf8[j]) & 0xC0) == 0x80) {
      ++j;
    }
    std::string reading;
    reading.push_back(kLiteralPrefix);
    reading.append(utf8, i, j - i);
    grid_.insertReading(reading);
    i = j;
  }
  walk_ = grid_.walk();
  unsettled_ = {};
  state_ = State::kComposing;
}

void Composer::moveCursor(int delta) {
  unsettled_ = {};  // moving away settles the syllable
  const size_t cursor = grid_.cursor();
  const size_t length = grid_.length();
  if (length == 0) return;
  if (delta < 0) {
    grid_.setCursor(cursor == 0 ? length : cursor - 1);
  } else {
    grid_.setCursor(cursor >= length ? 0 : cursor + 1);
  }
}

void Composer::jumpCursor(bool toStart) {
  unsettled_ = {};  // moving away settles the syllable
  grid_.setCursor(toStart ? 0 : grid_.length());
}

bool Composer::finalizePendingBare() {
  if (!pending_.complete()) return false;
  for (const auto& syllable : pending_.candidates()) {
    if (insertReading(syllable)) {  // bare reading = tone 1 or neutral
      unsettled_.active = true;
      unsettled_.toneGiven = false;
      unsettled_.syllables = pending_.candidates();
      unsettled_.keys = pending_.rawKeys();
      unsettled_.display = syllable;
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
      unsettled_.active = true;
      unsettled_.toneGiven = true;
      unsettled_.syllables = pending_.candidates();
      unsettled_.keys = pending_.rawKeys();
      unsettled_.display = syllable + mark;
      pending_.clear();
      return true;
    }
  }
  return false;
}

bool Composer::applyToneToUnsettled(char digit) {
  const char* mark = ToneMark(digit);
  if (mark == nullptr || grid_.length() == 0) return false;
  grid_.deleteReadingBeforeCursor();
  for (const auto& syllable : unsettled_.syllables) {
    if (insertReading(syllable + mark)) {
      unsettled_.toneGiven = true;
      unsettled_.display = syllable + mark;
      return true;
    }
  }
  // No entry for that tone: restore the bare syllable, still untoned, so
  // the next tone digit gets its chance.
  for (const auto& syllable : unsettled_.syllables) {
    if (insertReading(syllable)) {
      unsettled_.display = syllable;
      return false;
    }
  }
  walk_ = grid_.walk();  // should be unreachable; keep the walk consistent
  return false;
}

void Composer::undoUnsettledTone() {
  if (!unsettled_.active || !unsettled_.toneGiven) return;
  grid_.deleteReadingBeforeCursor();
  for (const auto& syllable : unsettled_.syllables) {
    if (insertReading(syllable)) {
      unsettled_.toneGiven = false;
      unsettled_.display = syllable;
      return;
    }
  }
  // The syllable only exists with a tone (ㄗㄨㄟ has no tone-1 entry), so
  // it cannot live in the grid untoned: put it back where it came from, in
  // the pending slot, awaiting another tone digit.
  walk_ = grid_.walk();
  const std::string keys = unsettled_.keys;
  unsettled_ = {};
  for (char key : keys) pending_.feed(key);
}

void Composer::settlePending() {
  if (!pending_.empty()) {
    // A complete syllable with a tone-1/neutral entry converts; anything
    // else (half syllable, or a syllable no toneless entry accepts)
    // settles as its bopomofo symbols, exactly like '`'.
    if (!(pending_.complete() && finalizePendingBare())) {
      std::string symbols = pending_.displayText();
      pending_.clear();
      insertLiteralText(symbols);
    }
  }
  // Whatever is in the grid now shows as its character, not its bopomofo.
  unsettled_ = {};
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
  unsettled_ = {};
  selectionLocation_ = 0;
  pageIndex_ = 0;
  hollowFinal_ = false;
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
  if (hollowFinal_) return true;
  if (c == '`') return true;  // settles/hollows bopomofo
  const bool composing = state_ == State::kComposing;
  if (c >= 'a' && c <= 'z') return true;
  if (DirectPunctuation(c) != nullptr || c == '"' || c == '\'') return true;
  if (c >= '0' && c <= '9') {
    // Digits are tone/control keys while composing and DISABLED when
    // idle: typing literal digits requires English mode (Shift).
    return true;
  }
  // Space and everything else printable.
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

  if (hollowFinal_) {
    return feedHollowFinal(c);
  }

  const bool composing = state_ == State::kComposing;

  // Backtick settles bopomofo as fixed text: with a pending syllable it
  // settles the visible display (n` -> settled ㄋ); with none it hollows
  // the initial slot so the next key is read as a final.
  if (c == '`') {
    if (!pending_.empty()) {
      std::string symbols = pending_.displayText();
      pending_.clear();
      insertLiteralText(symbols);
    } else {
      hollowFinal_ = true;
      state_ = State::kComposing;
    }
    return {true, ""};
  }

  // Digits split cleanly on whether something is unsettled. While it is,
  // they only ever type tones (mirrored, so either hand can reach them)
  // and never act as control keys; the control meanings come back once the
  // syllable is settled, which is what Space is for.
  if (c >= '0' && c <= '9') {
    if (anythingUnsettled()) {
      if (!unsettled_.toneGiven) {
        const char tone = ToneDigit(c);
        if (pending_.complete()) {
          // No tone-less entry accepted this syllable (ㄗㄨㄟ): it is still
          // pending and the tone digit is what puts it in the grid.
          applyToneToPending(tone);
        } else if (pending_.empty() && unsettled_.active) {
          applyToneToUnsettled(tone);
        }
        // A half syllable (ㄋ) has no tone to take: eaten.
        updateStateAfterMutation();
      }
      // Tone already given: eaten, so a mistyped tone is corrected with
      // Backspace rather than by a second digit.
      return {true, ""};
    }
    if (!composing) return {true, ""};  // idle: the digit row is disabled
    switch (c) {
      case '8':
        return openCandidateMenu();
      case '9':
        moveCursor(-1);
        return {true, ""};
      case '0':
        moveCursor(+1);
        return {true, ""};
      default:  // 1-7 have no control meaning
        return {true, ""};
    }
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
      unsettled_ = {};  // the previous syllable settles into its character
      if (pending_.complete()) {
        // Eager insert: the sentence walk sees the syllable at once (so the
        // preceding text auto-corrects) while the display keeps showing its
        // bopomofo. finalizePendingBare marks it unsettled; a failure means
        // no tone-less entry exists, so the syllable stays pending on
        // screen awaiting its tone digit.
        finalizePendingBare();
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

  // '-'/'=' jump the cursor to the start/end of the composition. They are
  // eaten while a syllable is unsettled, for the same reason 9/0 are: no
  // cursor key moves until the syllable in progress is settled. When idle
  // they pass through: the shell owns them as Home/End navigation keys.
  if (c == '-' || c == '=') {
    if (!composing) return {false, ""};
    if (anythingUnsettled()) return {true, ""};
    jumpCursor(c == '-');
    return {true, ""};
  }

  // Quotes alternate between opening and closing forms.
  if (c == '"' || c == '\'') {
    bool& open = (c == '"') ? doubleQuoteOpen_ : singleQuoteOpen_;
    const char* symbol =
        (c == '"') ? (open ? "”" : "“") : (open ? "’" : "‘");
    open = !open;
    settlePending();
    insertLiteralText(symbol);
    return {true, ""};
  }

  // Full-width punctuation SETTLES the syllable in progress and joins the
  // composition as a literal (2026-08-04) rather than committing it: 最,
  // gives 最，still underlined, still selectable. With nothing composing
  // it starts a fresh composition holding just the symbol.
  if (const char* punct = DirectPunctuation(c)) {
    settlePending();
    insertLiteralText(punct);
    return {true, ""};
  }

  // Space settles the syllable in progress WITHOUT committing: the default
  // tone-1/neutral reading wins (ㄏㄠ -> 蒿) and bopomofo no reading
  // accepts settles as symbols (ㄋ, ㄋㄧㄠ). With nothing left to settle it
  // commits the whole buffer like Enter. A plain space when idle passes
  // through to the application.
  if (c == ' ') {
    if (!composing) return {false, ""};
    return settleOrCommit();
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
  if (hollowFinal_) {
    // Undo the bare backtick.
    hollowFinal_ = false;
    updateStateAfterMutation();
    return {true, ""};
  }
  if (state_ == State::kEmpty) return {false, ""};

  if (!pending_.empty()) {
    pending_.backspace();
  } else if (unsettled_.active && unsettled_.toneGiven) {
    // Take back just the tone digit, leaving the syllable unsettled and
    // untoned: this is how a mistyped tone is fixed, since a second tone
    // digit is ignored.
    undoUnsettledTone();
    updateStateAfterMutation();
    return {true, ""};
  } else if (grid_.length() > 0) {
    grid_.deleteReadingBeforeCursor();
    walk_ = grid_.walk();
  }
  unsettled_ = {};
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
  // Defensive: '8' is a tone digit while anything is unsettled, so the menu
  // is only reachable with a settled buffer. Settle anyway rather than
  // opening a menu over a syllable that is still showing bopomofo.
  settlePending();
  if (grid_.length() == 0) return {true, ""};

  // Select at the cursor: candidatesAt targets the character right of the
  // cursor and falls back to the last character at the end of the buffer.
  selectionLocation_ = grid_.cursor();
  candidates_ = grid_.candidatesAt(selectionLocation_);

  // Hide no-op candidates: a value identical to the walked text over its
  // own span changes nothing when picked (the 聽不懂 menu would otherwise
  // lead with 聽不懂/不懂/懂 themselves).
  std::string walked;
  for (const auto& v : walk_.valuesAsStrings()) walked += v;
  std::vector<Formosa::Gramambular2::ReadingGrid::Candidate> filtered;
  for (const auto& candidate : candidates_) {
    std::string left, rest, covered, right;
    SplitAtCodePoints(walked, candidate.location, &left, &rest);
    SplitAtCodePoints(rest, candidate.spanningLength, &covered, &right);
    if (covered != candidate.value) filtered.push_back(candidate);
  }
  candidates_ = std::move(filtered);

  if (candidates_.empty()) return {true, ""};  // nothing worth a menu
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

Composer::Result Composer::feedHollowFinal(char c) {
  if ((c >= 'a' && c <= 'z') || c == ';') {
    // The hollowed key is read as a final; its bopomofo settles directly.
    std::string symbol = HollowFinalDisplay(c);
    if (!symbol.empty()) {
      hollowFinal_ = false;
      insertLiteralText(symbol);
    }
    return {true, ""};
  }
  if (c == ' ') {
    // The hollow slot has nothing to settle: drop it and let Space act on
    // the rest of the buffer.
    hollowFinal_ = false;
    return settleOrCommit();
  }
  // Anything else while the sub-state is active: eaten (Backspace, Enter
  // and Esc are handled by their dedicated feeds).
  return {true, ""};
}

Composer::Result Composer::settleOrCommit() {
  if (anythingUnsettled()) {
    settlePending();
    updateStateAfterMutation();
    return {true, ""};
  }
  std::string commit = takeCommitText();
  if (commit.empty()) {
    // Nothing at all (e.g. a bare '`'): consumed, back to idle.
    updateStateAfterMutation();
    return {true, ""};
  }
  return {true, commit};
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

  // Park the cursor just past the span that was fixed, so its anchor is
  // the next character: repeated 8s walk through the sentence without
  // touching the cursor keys. At the right end the cursor simply stays
  // there (8 then re-targets the last character).
  const size_t next = chosen.location + chosen.spanningLength;
  grid_.setCursor(next > grid_.length() ? grid_.length() : next);

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
