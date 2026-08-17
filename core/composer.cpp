#include "composer.h"

#include <chrono>
#include <cstdint>
#include <utility>

#include "double_pinyin.h"

namespace mspy {

namespace {

// Longest learned phrase the walk is checked against. Records longer than
// this are still stored; they simply never fire, which no real correction
// runs into.
constexpr size_t kMaxLearnedSpan = 4;

// Punctuation settled directly as symbols, following the user's Rime
// (rime-ice default) habits. Quotes are handled separately because they
// alternate between opening and closing forms.
const char* DirectPunctuation(char c) {
  switch (c) {
    case ',': return "，";
    case '.': return "。";
    case '?': return "？";
    case '!': return "！";
    case ':': return "：";
    case ';': return "；";  // only reachable when not part of a syllable
    case '\\': return "、";
    case '/': return "、";  // Rime maps both '\' and '/' to 、; '/' is the shorter reach
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
    // The three half-width exceptions to "Chinese mode emits full-width text
    // or nothing" (2026-08-16). Rime's half_shape table -- what 小狼毫 types
    // under the same 微軟雙拼 layout -- leaves these alone ('-' -> '-',
    // '=' -> '=', '+' -> '+'), and they are wanted inside Chinese text often
    // enough (ranges, arithmetic, identifiers) that a detour through English
    // mode was the wrong trade. '_' -> —— above is Rime's mapping too. Every
    // other half-width-only key (@ # $ % & * |) still types nothing.
    case '-': return "-";
    case '=': return "=";
    case '+': return "+";
    // '`' was the bopomofo function key until 2026-08-17 (it settled the
    // pending syllable as symbols, and on its own hollowed the initial slot
    // so the next key read as a final). That whole sub-state is gone; the
    // key does what Rime does with it, which is nothing but type itself.
    // Enter now covers the job it was mostly used for -- see takeCommitText.
    // Shift+'`' stays the full-width ～, unchanged.
    case '`': return "`";
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

// Decodes the LAST UTF-8 code point of `s` (0 when empty or malformed).
uint32_t LastCodePoint(const std::string& s) {
  if (s.empty()) return 0;
  size_t i = s.size();
  while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
  if (i == 0) return 0;
  --i;
  const unsigned char lead = static_cast<unsigned char>(s[i]);
  uint32_t cp = 0;
  if (lead < 0x80) {
    return lead;
  } else if ((lead & 0xE0) == 0xC0) {
    cp = lead & 0x1Fu;
  } else if ((lead & 0xF0) == 0xE0) {
    cp = lead & 0x0Fu;
  } else if ((lead & 0xF8) == 0xF0) {
    cp = lead & 0x07u;
  } else {
    return 0;
  }
  for (size_t k = i + 1; k < s.size(); ++k) {
    cp = (cp << 6) | (static_cast<unsigned char>(s[k]) & 0x3Fu);
  }
  return cp;
}

// Code points the separator space applies to: Han ideographs and bopomofo
// on one side, ASCII word characters on the other.
bool IsChineseCodePoint(uint32_t cp) {
  return (cp >= 0x4E00 && cp <= 0x9FFF) ||    // CJK Unified
         (cp >= 0x3400 && cp <= 0x4DBF) ||    // Ext A
         (cp >= 0xF900 && cp <= 0xFAFF) ||    // compat ideographs
         (cp >= 0x3105 && cp <= 0x312F) ||    // bopomofo
         (cp >= 0x20000 && cp <= 0x3FFFF);    // Ext B..F
}

bool IsAsciiWordCodePoint(uint32_t cp) {
  return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
         (cp >= '0' && cp <= '9');
}

// Returns the number of bytes of the first UTF-8 code point in `s`.
size_t FirstUtf8CharBytes(const std::string& s) {
  if (s.empty()) return 0;
  size_t i = 1;
  while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
  return i;
}

// Strips the internal explicit-tone-1 sentinel: the store and the shell
// both key on the plain bopomofo, so ㄍㄜˉ and ㄍㄜ are the same preference.
std::string NormalizeReading(const std::string& reading) {
  std::string out = reading;
  size_t pos;
  while ((pos = out.find(kToneSentinel1)) != std::string::npos) {
    out.erase(pos, sizeof(kToneSentinel1) - 1);
  }
  return out;
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

// Splits UTF-8 text into code points.
std::vector<std::string> SplitCodePoints(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = i + 1;
    while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) {
      ++j;
    }
    out.push_back(s.substr(i, j - i));
    i = j;
  }
  return out;
}

// Splits a node reading on the '-' the grid joins syllables with.
std::vector<std::string> SplitReading(const std::string& reading) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (true) {
    const size_t dash = reading.find('-', start);
    if (dash == std::string::npos) {
      parts.push_back(reading.substr(start));
      return parts;
    }
    parts.push_back(reading.substr(start, dash - start));
    start = dash + 1;
  }
}

// One entry per reading position, as the current walk shows it. A node's
// value has one code point per reading it spans; a node where that does not
// hold contributes empty entries, which every caller skips rather than act
// on the wrong character.
struct WalkChar {
  std::string value;    // the character displayed here
  std::string reading;  // its reading, sentinel-free
  bool literal = false;  // punctuation, settled bopomofo or English
};

std::vector<WalkChar> WalkChars(
    const Formosa::Gramambular2::ReadingGrid::WalkResult& walk) {
  std::vector<WalkChar> chars;
  for (const auto& node : walk.nodes) {
    const size_t span = node->spanningLength();
    auto values = SplitCodePoints(node->value());
    auto readings = SplitReading(node->reading());
    if (values.size() != span || readings.size() != span) {
      chars.insert(chars.end(), span, WalkChar{});
      continue;
    }
    for (size_t i = 0; i < span; ++i) {
      WalkChar c;
      c.value = std::move(values[i]);
      c.reading = NormalizeReading(readings[i]);
      c.literal = c.reading.find(kLiteralPrefix) != std::string::npos;
      chars.push_back(std::move(c));
    }
  }
  return chars;
}

// Just the displayed characters, for the pin-and-repair pass.
std::vector<std::string> ValuesOf(const std::vector<WalkChar>& chars) {
  std::vector<std::string> values;
  values.reserve(chars.size());
  for (const auto& c : chars) values.push_back(c.value);
  return values;
}

// The contexts a span at `loc` is learned and looked up under, most
// specific first: the two characters in front of it, then the one in front
// of it, then the start marker. Punctuation, settled bopomofo and English
// end a context the way the start of the composition does -- what precedes
// them says nothing about what follows.
std::vector<std::string> ContextsAt(const std::vector<WalkChar>& chars,
                                    size_t loc) {
  const auto usable = [&](size_t i) {
    return i < chars.size() && !chars[i].literal && !chars[i].value.empty();
  };
  if (loc == 0 || !usable(loc - 1)) return {UserPreferences::kStartContext};
  std::vector<std::string> contexts;
  if (loc >= 2 && usable(loc - 2)) {
    contexts.push_back(chars[loc - 2].value + chars[loc - 1].value);
  }
  contexts.push_back(chars[loc - 1].value);
  return contexts;
}

// True if any tone of this toneless bopomofo syllable exists. A bare key
// already covers tone 1 and the neutral tone (RelaxedToneLM expands it), so
// only 2/3/4 need asking for separately.
bool SyllableExists(Formosa::Gramambular2::LanguageModel& lm,
                    const std::string& syllable) {
  if (lm.hasUnigrams(syllable)) return true;
  for (const char* mark : {kTone2, kTone3, kTone4}) {
    if (lm.hasUnigrams(syllable + mark)) return true;
  }
  return false;
}

}  // namespace

Composer::Composer(
    std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm)
    : lm_(lm), grid_(std::move(lm)) {
  grid_.setReadingSeparator("-");
  // The double-pinyin decoder is a structural superset (the 'w' key is both
  // ia and ua, the 'y' key both ü and uai); the dictionary is what says
  // which reading a key pair actually has, so the pending syllable asks it
  // before showing or accepting anything. Capturing the model rather than
  // `this` keeps the callback valid however the composer is stored.
  auto model = lm_;
  pending_.setValidator([model](const std::string& syllable) {
    return SyllableExists(*model, syllable);
  });
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

  applyLearnedOverrides();
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
  applyLearnedOverrides();
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

bool Composer::finalizePendingBare() {
  if (!pending_.convertible()) return false;
  for (const auto& syllable : pending_.candidates()) {
    if (insertReading(syllable)) {  // bare reading = tone 1 or neutral
      unsettled_.active = true;
      unsettled_.syllables = pending_.candidates();
      unsettled_.display = syllable;
      pending_.clear();
      return true;
    }
  }
  return false;
}

bool Composer::applyToneToPending(char digit) {
  const char* mark = ToneMark(digit);
  if (mark == nullptr || !pending_.convertible()) return false;
  for (const auto& syllable : pending_.candidates()) {
    if (insertReading(syllable + mark)) {
      // A toned syllable is settled at once: the character appears now.
      unsettled_ = {};
      pending_.clear();
      return true;
    }
  }
  return false;
}

bool Composer::applyToneToUnsettled(char digit) {
  const char* mark = ToneMark(digit);
  if (mark == nullptr || grid_.length() == 0) return false;
  const std::vector<std::string> syllables = unsettled_.syllables;
  grid_.deleteReadingBeforeCursor();
  for (const auto& syllable : syllables) {
    if (insertReading(syllable + mark)) {
      unsettled_ = {};  // toned means settled: show the character
      return true;
    }
  }
  // No entry for that tone: restore the bare syllable, still untoned, so
  // the next tone digit gets its chance.
  for (const auto& syllable : syllables) {
    if (insertReading(syllable)) {
      unsettled_.display = syllable;
      return false;
    }
  }
  walk_ = grid_.walk();  // should be unreachable; keep the walk consistent
  return false;
}

void Composer::settlePending() {
  if (!pending_.empty()) {
    // A syllable with a tone-1/neutral entry converts; anything else (a
    // lone ㄅ, or a syllable no toneless entry accepts) settles as its
    // bopomofo symbols, exactly like '`'.
    if (!finalizePendingBare()) {
      std::string symbols = pending_.displayText();
      pending_.clear();
      insertLiteralText(symbols);
    }
  }
  // Whatever is in the grid now shows as its character, not its bopomofo.
  unsettled_ = {};
}

std::string Composer::takeCommitText() {
  // What you see is what is sent (2026-08-17). A syllable still showing as
  // bopomofo goes out AS bopomofo: nc gives ㄋㄧㄠ (which used to be
  // dropped outright) and k gives ㄎ (which used to convert to 科 with the
  // tone-1 default). Space and the tone digits are how a character is asked
  // for; Enter only sends the screen, so nothing on it is ever silently
  // discarded or silently changed on the way out.
  //
  // composedText() IS the screen, unsettled syllable and pending keys
  // included, so there is nothing to reassemble here.
  std::string text = composedText();
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
  // Printable ASCII only. Everything else -- control codes from Ctrl chords,
  // anything above 0x7E -- belongs to the application. This matters now that
  // the fall-through below claims every remaining printable: without it a
  // Ctrl+A arriving as 0x01 would be swallowed.
  if (c < 0x20 || c >= 0x7F) return false;
  if (state_ == State::kSelecting) return true;
  const bool composing = state_ == State::kComposing;
  if (c >= 'a' && c <= 'z') return true;
  if (DirectPunctuation(c) != nullptr || c == '"' || c == '\'') return true;
  if (c >= '0' && c <= '9') {
    // Digits are tone/control keys while composing. When idle the shell
    // claims them first as the navigation layer, so this only answers for
    // callers that have no shell (the REPL): eaten, never typed.
    return true;
  }
  // Space is the one printable that still passes through when idle -- it is
  // a word separator, not a symbol. Everything else is eaten either way, so
  // that Chinese mode emits no half-width text beyond the three keys named
  // in DirectPunctuation (see feedChar's tail).
  if (c == ' ') return composing;
  return true;
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

  // Digits split cleanly on whether something is unsettled. While it is,
  // they only ever type tones (mirrored, so either hand can reach them) and
  // never act as control keys; the control meanings come back the moment the
  // syllable settles -- which the tone digit itself does.
  if (c >= '0' && c <= '9') {
    if (anythingUnsettled()) {
      if (c == '6') {
        // 6 is Backspace in every state now (2026-08-17): here it takes
        // back the last KEY of the syllable being typed, the same edit
        // feedBackspace makes. That costs 輕聲 its right-hand mirror --
        // it is 5 only from now on -- which reverses the 2026-08-14 ruling
        // that 5/6 could not be borrowed. The user chose it knowing that:
        // deleting without leaving the main block is worth more than the
        // second way to reach one tone.
        return feedBackspace();
      }
      const char tone = ToneDigit(c);
      if (pending_.convertible()) {
        // Either no tone-less entry accepted this syllable (ㄗㄨㄟ) or only
        // the first key is typed (ㄗ): the tone digit is what puts it in
        // the grid.
        applyToneToPending(tone);
      } else if (pending_.empty() && unsettled_.active) {
        applyToneToUnsettled(tone);
      }
      // A key that spells no syllable yet (ㄅ) has no tone to take: eaten.
      updateStateAfterMutation();
      return {true, ""};
    }
    if (!composing) return {true, ""};  // idle: the shell owns the digit row
    switch (c) {
      case '5':
        // Delete and Backspace, reachable without leaving the main block
        // (2026-08-14). They are only here once the syllable has settled --
        // while bopomofo is still on screen every digit is a tone, which is
        // what keeps 輕聲 typable on 5 and its mirror 6.
        return feedDelete();
      case '6':
        return feedBackspace();
      case '8':
        return openCandidateMenu();
      case '9':
        moveCursor(-1);
        return {true, ""};
      case '0':
        moveCursor(+1);
        return {true, ""};
      default:  // 1-4 and 7 have no control meaning
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
      // The pair spells no syllable: reject the key while composing.
      // (2026-08-08, reverted the same day: a lone first key does NOT split
      // off here to let the key open the next syllable. It works -- 知情
      // would be v q ; -- but only when the pair happens to spell nothing,
      // so 知識 (v+u = ㄓㄨ) still needs its separator and the two words
      // train opposite habits. A lone syllable always takes Space or a tone
      // digit, with no exceptions to remember.)
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
  // types a half-width space into the buffer -- it does NOT commit
  // (2026-08-16). A plain space when idle still passes through to the
  // application.
  if (c == ' ') {
    if (!composing) return {false, ""};
    return settleOrSpace();
  }

  // Everything else printable is EATEN, idle or not. A key with no symbol of
  // its own types nothing at all rather than leaking a half-width character
  // into the document -- the same bargain the digit row already makes.
  // `@ # $ % & * |` all land here; Shift switches to English mode when they
  // are wanted. (`- = +` used to as well, until 2026-08-16 gave them their
  // Rime half-width meanings.)
  return {true, ""};
}

// Forward delete: the counterpart of feedBackspace, for digit 5. A pending
// syllable sits AT the cursor, so there is nothing to its right to remove --
// that case is a no-op rather than an error. (The digit branch only calls
// this once everything is settled, but callers should not have to know.)
Composer::Result Composer::feedDelete() {
  if (state_ == State::kSelecting) {
    dismissMenu();
  }
  if (state_ == State::kEmpty) return {false, ""};

  if (pending_.empty() && grid_.length() > 0) {
    grid_.deleteReadingAfterCursor();
    walk_ = grid_.walk();
    applyLearnedOverrides();
  }
  unsettled_ = {};
  updateStateAfterMutation();
  return {true, ""};
}

Composer::Result Composer::feedBackspace() {
  if (state_ == State::kSelecting) {
    // Close the menu, then delete as usual.
    dismissMenu();
  }
  if (state_ == State::kEmpty) return {false, ""};

  if (!pending_.empty()) {
    // The WHOLE syllable in progress goes, not one key of it: ㄅ and ㄅㄧㄠ
    // both leave nothing behind (2026-08-17, reverting the key-at-a-time
    // unwind tried earlier the same day). A syllable is one unit to type and
    // one unit to take back, so there is nothing to remember about how far
    // into it a press lands.
    pending_.clear();
  } else if (grid_.length() > 0) {
    grid_.deleteReadingBeforeCursor();
    walk_ = grid_.walk();
    applyLearnedOverrides();
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

Composer::Result Composer::feedEnglishChar(char c) {
  // English mode only reaches the composer while a composition is live: the
  // character joins it as literal text so one uncommitted buffer can hold
  // both scripts. With nothing composing the shell never calls us and the
  // key goes straight to the application.
  if (state_ == State::kEmpty) return {false, ""};
  if (state_ == State::kSelecting) dismissMenu();
  settlePending();
  insertLiteralText(std::string(1, c));
  updateStateAfterMutation();
  return {true, ""};
}

Composer::Result Composer::switchLanguage(bool toEnglish) {
  if (state_ == State::kEmpty) return {false, ""};
  if (state_ == State::kSelecting) dismissMenu();
  settlePending();
  // Both sides of the junction are inside our own buffer, so the separator
  // decision is exact: Chinese before an English run, an English word
  // character before Chinese. Punctuation and an existing space add nothing.
  const uint32_t cp = LastCodePoint(displaySegments().before);
  if (toEnglish ? IsChineseCodePoint(cp) : IsAsciiWordCodePoint(cp)) {
    insertLiteralText(" ");
  }
  updateStateAfterMutation();
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

Composer::Result Composer::settleOrSpace() {
  if (anythingUnsettled()) {
    settlePending();
    updateStateAfterMutation();
    return {true, ""};
  }
  // Everything is settled already, so Space has no syllable to finish. It
  // used to commit the buffer here; since 2026-08-16 it types a plain
  // half-width space instead, joining the composition the way punctuation
  // and English letters do. Enter is the only key that sends text to the
  // application now (Esc still throws it away, losing focus still flushes
  // it). The buffer is meant to run as long as the user wants, and a word
  // separator was the one printable left that broke that promise.
  if (grid_.length() == 0) {
    // Nothing in the buffer at all: back to idle rather than opening a
    // composition that holds only a space.
    updateStateAfterMutation();
    return {true, ""};
  }
  insertLiteralText(" ");
  return {true, ""};
}

Composer::Result Composer::selectCandidate(size_t index) {
  if (state_ != State::kSelecting || index >= candidates_.size()) {
    return {true, ""};
  }
  const auto& chosen = candidates_[index];
  const auto walkBefore = walk_;
  const auto charactersBefore = ValuesOf(WalkChars(walkBefore));
  grid_.overrideCandidate(selectionLocation_, chosen);
  walk_ = grid_.walk();
  // Correcting one word must never rewrite another. overrideCandidate
  // resets every node OVERLAPPING the span it writes, so picking 鋼杯 at
  // 不鏽鋼[悲] tears up the 不鏽鋼 node and the leftover 不鏽 re-segments
  // into 不秀. Put back whatever the re-walk moved outside the chosen span.
  restoreCharactersOutside(charactersBefore, chosen.location,
                           chosen.location + chosen.spanningLength);

  // Park the cursor just past the span that was fixed, so its anchor is
  // the next character: repeated 8s walk through the sentence without
  // touching the cursor keys. At the right end the cursor simply stays
  // there (8 then re-targets the last character).
  const size_t next = chosen.location + chosen.spanningLength;
  grid_.setCursor(next > grid_.length() ? grid_.length() : next);

  learnFromSelection(chosen);

  dismissMenu();
  return {true, ""};
}

void Composer::restoreCharactersOutside(
    const std::vector<std::string>& before, size_t from, size_t to) {
  // Each repair is a LENGTH-1 override, and overrideCandidate only resets
  // nodes overlapping the span it writes, so pinning one position can never
  // un-pin another one: the loop converges. The guard is belt-and-braces.
  for (size_t guard = 0; guard <= before.size(); ++guard) {
    const auto now = ValuesOf(WalkChars(walk_));
    bool repaired = false;
    for (size_t i = 0; i < before.size() && i < now.size(); ++i) {
      if (i >= from && i < to) continue;  // the span the user just chose
      if (before[i].empty() || now[i].empty() || before[i] == now[i]) continue;
      // A character that only exists inside a longer word has no
      // single-reading node to pin it to; leave those alone.
      if (!grid_.overrideCandidate(i, before[i])) continue;
      walk_ = grid_.walk();
      repaired = true;
      break;  // one pin can move several positions: re-measure first
    }
    if (!repaired) return;
  }
}

void Composer::learnFromSelection(
    const Formosa::Gramambular2::ReadingGrid::Candidate& chosen) {
  if (!preferences_) return;
  const std::string reading = NormalizeReading(chosen.reading);
  // Punctuation, settled bopomofo and English are literal readings with one
  // candidate each; there is nothing to learn about them.
  if (reading.find(kLiteralPrefix) != std::string::npos) return;

  // Positions left of the chosen span are exactly what they were before the
  // pick (restoreCharactersOutside just made sure of it), so the current
  // walk is the right place to read the context off.
  const auto chars = WalkChars(walk_);
  const auto contexts = ContextsAt(chars, chosen.location);
  for (const auto& context : contexts) {
    preferences_->record(context, reading, chosen.value);
  }
  if (onLearned && !contexts.empty()) {
    onLearned(contexts.front(), reading, chosen.value);
  }
}

void Composer::applyLearnedOverrides() {
  if (!preferences_ || grid_.length() == 0) return;
  // Each pass fixes at most one span and then re-walks, because one
  // override can move several positions. Every span can be settled at most
  // once (an overridden node is skipped from then on), so the grid length
  // bounds the number of passes.
  for (size_t guard = 0; guard <= grid_.length(); ++guard) {
    if (!applyOneLearnedOverride()) return;
  }
}

bool Composer::applyOneLearnedOverride() {
  const auto chars = WalkChars(walk_);
  const auto& readings = grid_.readings();
  if (chars.size() != readings.size()) return false;

  // Positions already carrying an override are the user's own picks, the
  // pins that protect them, and the corrections applied on an earlier pass.
  // None of them may be second-guessed here.
  std::vector<bool> overridden(chars.size(), false);
  size_t loc = 0;
  for (const auto& node : walk_.nodes) {
    const size_t span = node->spanningLength();
    if (node->isOverridden()) {
      for (size_t i = loc; i < loc + span && i < overridden.size(); ++i) {
        overridden[i] = true;
      }
    }
    loc += span;
  }

  for (size_t start = 0; start < chars.size(); ++start) {
    if (overridden[start]) continue;
    const auto contexts = ContextsAt(chars, start);
    // Longest match first: a learned two-character phrase outranks a
    // learned single character sitting at the same place.
    const size_t maxSpan = std::min(kMaxLearnedSpan, chars.size() - start);
    for (size_t span = maxSpan; span >= 1; --span) {
      bool blocked = false;
      std::string reading;
      std::string current;
      for (size_t i = start; i < start + span; ++i) {
        if (overridden[i] || chars[i].literal || chars[i].reading.empty()) {
          blocked = true;
          break;
        }
        if (i > start) reading += '-';
        reading += NormalizeReading(readings[i]);
        current += chars[i].value;
      }
      if (blocked) break;  // a shorter span here would end at the same wall

      for (const auto& context : contexts) {
        if (!preferences_->hasContext(context)) continue;
        const std::string value = preferences_->lookup(context, reading);
        if (value.empty() || value == current) continue;

        const auto before = ValuesOf(chars);
        if (!grid_.overrideCandidate(start, value)) continue;
        walk_ = grid_.walk();
        // A learned correction is as narrow as a manual one: whatever the
        // re-walk moved outside this span goes straight back.
        restoreCharactersOutside(before, start, start + span);
        return true;
      }
    }
  }
  return false;
}

}  // namespace mspy
