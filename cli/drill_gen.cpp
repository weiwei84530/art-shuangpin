// Generator for the typing-drill data the tutorial site plays back.
//
//   drill_gen.exe --data out/data.txt --lessons drills/lessons.txt
//                 --out web/drills.js [--coverage]
//   drill_gen.exe ... --audit [--allow drills/skip-syllables.txt]
//       walks every key combination the IME accepts and reports the ones
//       the lessons never ask for; exit code 2 when any are left.
//
// The drill has to tell the learner the FASTEST correct keystroke for the
// next character, so the keystrokes cannot be guessed in JavaScript: they
// are derived here and then replayed through the real mspy::Composer, and
// the run fails if the composer does not end up with the lesson's text.
// Every step carries the screen the IME would be showing at that moment,
// so the site is a pure player.
//
// Keystroke rules (docs/spec.md §1, §5, §6):
//   - a syllable is its double-pinyin key pair, or the single key when the
//     pair is what that key already means on its own (的 = d, not de);
//   - tones 2, 3 and 4 are typed with the hand OPPOSITE the one that typed
//     the syllable's last letter, using the mirrored digits (tone 3 = 3 or
//     8);
//   - tones 1 and 5 are never typed: no digit already means "tone 1 or
//     neutral", so the next syllable settles a two-key syllable and a
//     single-key one takes Space (的 = d + Space, not d + 6);
//   - a comma settles what precedes it and carries on; Enter comes only at
//     。！？.
//
// The drill NEVER corrects anything (2026-08-10). Sending the learner
// through the candidate menu meant long runs of cursor keys whose behaviour
// surprises more than it teaches, so a line the sentence walk gets wrong is
// a hard error for a hand-written lesson and a dropped line for generated
// filler -- see scripts/build-drills.ps1, which bans the offending word and
// re-picks until every line converts cleanly.

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "McBopomofoLM.h"
#include "composer.h"
#include "double_pinyin.h"
#include "relaxed_tone_lm.h"

namespace {

std::string Narrow(const wchar_t* wide) {
  int len =
      WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 0) return {};
  std::string out(static_cast<size_t>(len) - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), len, nullptr, nullptr);
  return out;
}

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

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : s) {
    if (c == sep) {
      parts.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  parts.push_back(current);
  return parts;
}

std::vector<std::string> SplitWhitespace(const std::string& s) {
  std::vector<std::string> parts;
  std::istringstream in(s);
  std::string part;
  while (in >> part) parts.push_back(part);
  return parts;
}

std::string Trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// ---------------------------------------------------------------- readings

// Tone marks as they appear at the END of a reading; the empty string is
// tone 1 and needs no digit.
const char* const kToneMarks[] = {"", "ˊ", "ˇ", "ˋ", "˙"};

// Splits a reading into its toneless syllable and its tone digit ('1'..'5').
bool SplitTone(const std::string& reading, std::string* bare, char* tone) {
  for (int i = 1; i <= 4; ++i) {
    const std::string mark = kToneMarks[i];
    if (reading.size() > mark.size() &&
        reading.compare(reading.size() - mark.size(), mark.size(), mark) == 0) {
      *bare = reading.substr(0, reading.size() - mark.size());
      *tone = static_cast<char>('1' + i);
      return true;
    }
  }
  *bare = reading;
  *tone = '1';
  return true;
}

// value -> its readings, best-scoring first. Only entries whose value has
// one code point per syllable are usable, which is every real word.
struct WordReading {
  std::string reading;
  double score = 0.0;
};
using ReadingIndex = std::map<std::string, std::vector<WordReading>>;

ReadingIndex LoadReadingIndex(const std::string& path,
                              std::map<std::string, double>* syllableMass) {
  ReadingIndex index;
  std::ifstream in(path, std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const auto fields = SplitWhitespace(line);
    if (fields.size() < 2) continue;
    const std::string& reading = fields[0];
    const std::string& value = fields[1];
    if (reading.find('_') != std::string::npos) continue;  // punctuation rows
    const size_t syllables = Split(reading, '-').size();
    if (SplitCodePoints(value).size() != syllables) continue;
    double score = -100.0;
    if (fields.size() >= 3) {
      try {
        score = std::stod(fields[2]);
      } catch (...) {
      }
    }
    index[value].push_back(WordReading{reading, score});
    if (syllables == 1 && syllableMass != nullptr) {
      std::string bare;
      char tone = '1';
      SplitTone(reading, &bare, &tone);
      (*syllableMass)[bare] += std::exp(score);
    }
  }
  // Best reading first: a word with several is typed the common way unless
  // the lesson says otherwise.
  for (auto& [value, readings] : index) {
    std::stable_sort(readings.begin(), readings.end(),
                     [](const WordReading& a, const WordReading& b) {
                       return a.score > b.score;
                     });
  }
  return index;
}

// ---------------------------------------------------------------- keystrokes

// Left-hand letters on a standard keyboard; everything else is right-hand.
bool IsLeftHand(char key) {
  static const std::string left = "qwertasdfgzxcvb12345";
  return left.find(key) != std::string::npos;
}

// The tone digit to prescribe: the mirrored right-hand digit after a
// left-hand syllable, the plain left-hand digit otherwise, so the hands
// alternate (2026-08-09).
char ToneKeyFor(char tone, char lastLetterKey) {
  if (!IsLeftHand(lastLetterKey)) return tone;  // left hand types 1-5
  switch (tone) {                               // right hand mirrors them
    case '1': return '0';
    case '2': return '9';
    case '3': return '8';
    case '4': return '7';
    case '5': return '6';
    default: return tone;
  }
}

// True if any tone of this toneless syllable exists -- the same test
// SyllableInput applies, so the keys derived here are the keys the IME
// actually accepts.
bool SyllableExists(Formosa::Gramambular2::LanguageModel& lm,
                    const std::string& syllable) {
  if (lm.hasUnigrams(syllable)) return true;
  for (const char* mark : {"ˊ", "ˇ", "ˋ"}) {
    if (lm.hasUnigrams(syllable + mark)) return true;
  }
  return false;
}

// Drops the candidates the dictionary rejects, exactly as SyllableInput
// does; the decoder is a structural superset (the y key is both ü and uai,
// so k + y decodes to ㄎㄩ and ㄎㄨㄞ and only the second one is real).
std::vector<std::string> Accepted(std::vector<std::string> decoded,
                                  Formosa::Gramambular2::LanguageModel& lm) {
  decoded.erase(std::remove_if(decoded.begin(), decoded.end(),
                               [&lm](const std::string& syllable) {
                                 return !SyllableExists(lm, syllable);
                               }),
                decoded.end());
  return decoded;
}

// The double-pinyin keys for a toneless bopomofo syllable, the single-key
// form first. Returns false when no key pair spells it.
bool KeysForSyllable(const std::string& bare,
                     Formosa::Gramambular2::LanguageModel& lm,
                     std::string* keys) {
  for (char first = 'a'; first <= 'z'; ++first) {
    const auto single = Accepted(mspy::DecodeSingleKey(first), lm);
    if (!single.empty() && single.front() == bare) {
      *keys = std::string(1, first);
      return true;
    }
  }
  for (char first = 'a'; first <= 'z'; ++first) {
    for (char second : std::string("abcdefghijklmnopqrstuvwxyz;")) {
      const auto pair = Accepted(mspy::DecodeKeyPair(first, second), lm);
      if (!pair.empty() && pair.front() == bare) {
        *keys = std::string{first, second};
        return true;
      }
    }
  }
  return false;
}

// ---------------------------------------------------------------- lessons

// Punctuation that ends a sentence, and so the only place the drill presses
// Enter. A comma settles what precedes it just as well, but the composition
// carries on (docs/spec.md §6: nothing auto-commits).
bool IsSentenceEnd(const std::string& symbol) {
  return symbol == "。" || symbol == "！" || symbol == "？";
}

struct Sentence {
  std::string text;                     // including the trailing punctuation
  std::vector<std::string> readings;    // one per character; empty = literal
  std::vector<std::string> words;       // as written in the lesson file
  std::vector<size_t> wordOfChar;       // which word each character came from
};

struct Lesson {
  std::string id;
  std::string title;
  std::string intro;
  std::vector<Sentence> sentences;
  // Generated filler: a line that does not convert cleanly is dropped and
  // reported so the next round can pick different words. Hand-written
  // lessons are a hard error instead -- they are meant to be reworded.
  bool droppable = false;
  std::string text() const {
    std::string all;
    for (const auto& s : sentences) all += s.text;
    return all;
  }
};

// ---------------------------------------------------------------- JSON

std::string JsonString(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
  return out;
}

// ---------------------------------------------------------------- simulation

// One keystroke plus the screen the IME shows right after it. The screen
// fields mirror what web/app.js renders, so the player needs no logic of
// its own.
struct Step {
  std::string key;                                   // keyboard id to press
  std::string committed;                             // text already on screen
  std::vector<std::pair<std::string, char>> comp;    // [char, 's'|'p']
  int anchor = -1;                                   // highlighted index
  int caret = -1;                                    // caret index
  bool menuOpen = false;
  std::vector<std::string> menuItems;
  std::string menuPage;
  size_t done = 0;  // target characters produced so far
};

// The keyboard id for a typed character, matching web/tutorials.js KEY_ROWS.
std::string KeyId(char c) {
  if (c == ' ') return "Space";
  if (c == '\n') return "Enter";
  return std::string(1, c);
}

class Runner {
 public:
  Runner(std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm)
      : composer_(std::move(lm)) {}

  const std::vector<Step>& steps() const { return steps_; }
  const std::string& error() const { return error_; }

  // Marks one more character of the lesson as typed. The article panel
  // follows the KEYSTROKES, not the screen: once the last key of 家 is
  // down the learner is finished with that character, even though the IME
  // still shows ㄐㄧㄚ until the next syllable settles it.
  void FinishCharacter() {
    ++charactersDone_;
    if (!steps_.empty()) steps_.back().done = charactersDone_;
  }

  // Feeds one printable character and records the resulting screen.
  void Press(char c) {
    mspy::Composer::Result r = c == '\n' ? composer_.feedEnter()
                                         : composer_.feedChar(c);
    committed_ += r.commitText;
    // A key the composer does not eat reaches the application itself. The
    // only one the drill ever presses is Enter with nothing composing,
    // which is how the line actually breaks.
    if (!r.consumed && c == '\n') committed_ += '\n';
    Record(KeyId(c));
  }

  // Everything the composer currently shows as real characters.
  std::string ComposedText() const {
    const auto s = composer_.displaySegments();
    return s.before + s.highlighted + s.after;
  }

  size_t CursorPosition() const {
    const auto s = composer_.displaySegments();
    return SplitCodePoints(s.before).size() +
           SplitCodePoints(s.unconfirmed).size();
  }

  size_t GridLength() const {
    const auto s = composer_.displaySegments();
    return SplitCodePoints(s.before).size() +
           SplitCodePoints(s.unconfirmed).size() +
           SplitCodePoints(s.highlighted).size() +
           SplitCodePoints(s.after).size();
  }

  // Walks the cursor to `target` by the cheapest route that does NOT wrap.
  // 9/0 do wrap around both ends, but a drill that sends the cursor off one
  // end to come back at the other teaches nothing and reads as a glitch;
  // '-' and '=' jump to the ends and are usually no more keystrokes anyway.
  void MoveCursorTo(size_t target) {
    const size_t at = CursorPosition();
    if (at == target) return;
    const size_t length = GridLength();

    const size_t direct = at > target ? at - target : target - at;
    const size_t viaStart = 1 + target;
    const size_t viaEnd = 1 + (length - target);

    if (viaStart < direct && viaStart <= viaEnd) {
      Press('-');
    } else if (viaEnd < direct) {
      Press('=');
    }
    for (int guard = 0; guard <= static_cast<int>(length) + 1; ++guard) {
      const size_t now = CursorPosition();
      if (now == target) return;
      Press(now > target ? '9' : '0');
    }
    error_ = "cursor would not reach position " + std::to_string(target);
  }

  // Index of the first character the walk got wrong, or npos when the
  // composition already reads `target`.
  //
  // The drill never corrects anything (2026-08-10). Sending the learner
  // through the candidate menu meant long runs of cursor keys whose
  // behaviour -- wrapping, parking past the chosen span -- surprises more
  // than it teaches, so a line that does not convert cleanly is a line to
  // reword or drop, not to patch at run time.
  size_t FirstMismatch(const std::string& target) const {
    const auto current = SplitCodePoints(ComposedText());
    const auto wanted = SplitCodePoints(target);
    for (size_t i = 0; i < wanted.size(); ++i) {
      if (i >= current.size() || current[i] != wanted[i]) return i;
    }
    return current.size() > wanted.size() ? wanted.size() : std::string::npos;
  }

  // Everything typed so far, for the mismatch report.
  std::string ComposedSoFar() const { return ComposedText(); }

  // Winds back to a mark taken before a line, so a line that does not
  // convert can be dropped without disturbing the rest of the lesson.
  // Lines are independent: Enter resets the composer.
  size_t Mark() const { return steps_.size(); }
  void Rollback(size_t mark, const std::string& committed) {
    steps_.resize(mark);
    committed_ = committed;
    composer_.cancel();
  }
  const std::string& committed() const { return committed_; }

 private:
  void Record(const std::string& key) {
    Step step;
    step.key = key;
    step.committed = committed_;
    const auto segments = composer_.displaySegments();
    for (const auto& c : SplitCodePoints(segments.before)) {
      step.comp.push_back({c, 's'});
    }
    for (const auto& c : SplitCodePoints(segments.unconfirmed)) {
      step.comp.push_back({c, 'p'});
    }
    step.caret = static_cast<int>(step.comp.size());
    if (!segments.highlighted.empty()) {
      step.anchor = static_cast<int>(step.comp.size());
      step.comp.push_back({segments.highlighted, 's'});
    }
    for (const auto& c : SplitCodePoints(segments.after)) {
      step.comp.push_back({c, 's'});
    }
    if (composer_.state() == mspy::Composer::State::kSelecting) {
      step.menuOpen = true;
      for (const auto& c : composer_.currentPageCandidates()) {
        step.menuItems.push_back(c.value);
      }
      step.menuPage = std::to_string(composer_.candidatePageIndex() + 1) + "/" +
                      std::to_string(composer_.candidatePageCount());
    }
    step.done = charactersDone_;
    steps_.push_back(std::move(step));
  }

  mspy::Composer composer_;
  size_t charactersDone_ = 0;
  std::string committed_;
  std::vector<Step> steps_;
  std::string error_;
};

// ---------------------------------------------------------------- lesson file

// Lesson source format:
//   #id=basics
//   #title=基礎：兩鍵一音節
//   #intro=一句話說明
//   你好 世界 。            <- one sentence per line, words separated by spaces
//   今天 天氣 很好 ！
// A word may carry an explicit reading after a slash (麼/ㄇㄜ˙) when the
// dictionary knows more than one.
bool LoadLessons(const std::string& path, const ReadingIndex& index,
                 bool droppable, std::vector<Lesson>* lessons,
                 std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    *error = "cannot open " + path;
    return false;
  }
  std::string line;
  int lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed.compare(0, 2, "//") == 0) continue;

    if (trimmed[0] == '#') {
      const size_t eq = trimmed.find('=');
      if (eq == std::string::npos) continue;  // a plain comment
      const std::string field = trimmed.substr(1, eq - 1);
      const std::string value = trimmed.substr(eq + 1);
      if (field == "id") {
        lessons->push_back(Lesson{});
        lessons->back().id = value;
        lessons->back().droppable = droppable;
      } else if (lessons->empty()) {
        *error = path + ":" + std::to_string(lineNo) + ": " + field +
                 " before any #id";
        return false;
      } else if (field == "title") {
        lessons->back().title = value;
      } else if (field == "intro") {
        lessons->back().intro = value;
      }
      continue;
    }

    if (lessons->empty()) {
      *error = path + ":" + std::to_string(lineNo) + ": text before any #id";
      return false;
    }
    Sentence sentence;
    for (const auto& token : SplitWhitespace(trimmed)) {
      std::string word = token;
      std::string forced;
      const size_t slash = token.find('/');
      if (slash != std::string::npos) {
        word = token.substr(0, slash);
        forced = token.substr(slash + 1);
      }
      const auto characters = SplitCodePoints(word);
      std::vector<std::string> readings;

      if (!forced.empty()) {
        readings = Split(forced, '-');
        if (readings.size() != characters.size()) {
          *error = path + ":" + std::to_string(lineNo) + ": " + word +
                   " has " + std::to_string(characters.size()) +
                   " characters but " + std::to_string(readings.size()) +
                   " readings";
          return false;
        }
      } else {
        auto it = index.find(word);
        if (it == index.end()) {
          // Punctuation and any other literal: typed as itself.
          if (characters.size() != 1) {
            *error = path + ":" + std::to_string(lineNo) + ": no reading for " +
                     word + " (add one after a slash)";
            return false;
          }
          readings.push_back("");
        } else {
          if (it->second.size() > 1) {
            // Several readings: take the one the dictionary scores highest
            // and say so, since only the author can tell whether it is the
            // one the sentence means.
            std::string options;
            for (size_t k = 1; k < it->second.size() && k < 4; ++k) {
              options += " " + it->second[k].reading;
            }
            std::cout << "  note: " << word << " read as "
                      << it->second.front().reading << " (also" << options
                      << ")\n";
          }
          readings = Split(it->second.front().reading, '-');
        }
      }

      const size_t wordIndex = sentence.words.size();
      sentence.words.push_back(word);
      sentence.text += word;
      for (const auto& reading : readings) {
        sentence.readings.push_back(reading);
        sentence.wordOfChar.push_back(wordIndex);
      }
    }
    // One line is one sentence: it has to end with 。！？ so the run can
    // check what the walk produced and then commit it. Commas inside the
    // line are welcome -- they are what joins clauses into a single Enter.
    if (sentence.readings.empty() || !sentence.readings.back().empty() ||
        !IsSentenceEnd(SplitCodePoints(sentence.text).back())) {
      *error = path + ":" + std::to_string(lineNo) +
               ": a line must end with 。, ！ or ？";
      return false;
    }
    lessons->back().sentences.push_back(std::move(sentence));
  }
  return true;
}

// The punctuation keys the IME turns into full-width symbols, reversed so a
// lesson can spell 「，」 and the drill knows to press ','.
char PunctuationKey(const std::string& symbol) {
  static const std::map<std::string, char> map = {
      {"，", ','}, {"。", '.'}, {"？", '?'}, {"！", '!'}, {"：", ':'},
      {"；", ';'}, {"、", '\\'}, {"「", '['}, {"」", ']'}, {"『", '{'},
      {"』", '}'}, {"（", '('}, {"）", ')'}, {"《", '<'}, {"》", '>'},
      {"～", '~'},
  };
  auto it = map.find(symbol);
  return it == map.end() ? 0 : it->second;
}

// Types one line into `runner`, checking the walk at every punctuation mark
// (a comma settles what precedes it just as well as a full stop does) and
// pressing Enter at the end. `breakLine` adds the second Enter that starts
// the next line -- false on the last line of a lesson, where committing is
// the end of it. Returns npos when the line came out exactly as written,
// otherwise the index of the first character that did not.
size_t TypeSentence(Runner& runner, const Sentence& sentence,
                    Formosa::Gramambular2::LanguageModel& lm,
                    bool breakLine,
                    std::set<std::string>* keysUsed,
                    std::set<std::string>* syllablesUsed,
                    std::string* fatal) {
  const auto characters = SplitCodePoints(sentence.text);
  std::string pending;  // the line so far, which is what the walk must match
  for (size_t i = 0; i < characters.size(); ++i) {
    const std::string& reading = sentence.readings[i];

    if (reading.empty()) {
      const char key = PunctuationKey(characters[i]);
      if (key == 0) {
        *fatal = "no key types " + characters[i];
        return i;
      }
      runner.Press(key);
      runner.FinishCharacter();
      pending += characters[i];
      const size_t bad = runner.FirstMismatch(pending);
      if (bad != std::string::npos) return bad;
      // Only a sentence ending sends the buffer to the application; a comma
      // just carries on in the same composition. The second Enter is the
      // line break (2026-08-10): nobody presses Enter mid-paragraph just to
      // commit, so the drill only asks for it where a new line is wanted,
      // and then it is worth two presses -- commit, then break.
      if (IsSentenceEnd(characters[i])) {
        runner.Press('\n');
        if (breakLine) {
          runner.Press('\n');
          runner.FinishCharacter();  // the newline is a character of the text
        }
      }
      continue;
    }

    std::string bare;
    char tone = '1';
    SplitTone(reading, &bare, &tone);
    std::string keys;
    if (!KeysForSyllable(bare, lm, &keys)) {
      *fatal = "no key pair spells " + bare;
      return i;
    }
    if (keysUsed != nullptr) keysUsed->insert(keys);
    if (syllablesUsed != nullptr) syllablesUsed->insert(bare);
    for (char key : keys) runner.Press(key);
    // Tones 1 and 5 are never typed: no digit at all already means "tone 1
    // or neutral" (docs/spec.md §5), so 的 is d + Space, not d + 6. The
    // explicit digits exist only to EXCLUDE the other one, which a drill
    // never needs.
    if (tone != '1' && tone != '5') {
      runner.Press(ToneKeyFor(tone, keys.back()));
    } else if (keys.size() == 1) {
      // A lone key is never settled implicitly (docs/spec.md §1).
      runner.Press(' ');
    }
    runner.FinishCharacter();
    pending += characters[i];
  }
  return std::string::npos;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);

  std::string dataPath = "out/data.txt";
  std::vector<std::string> lessonPaths;
  std::string outPath = "web/drills.js";
  std::vector<std::string> fillerPaths;
  std::string syllablesPath;
  std::string droppedPath;
  std::string auditAllowPath;
  std::string reachablePath;
  bool coverage = false;
  bool audit = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = Narrow(argv[i]);
    if (arg == "--data" && i + 1 < argc) {
      dataPath = Narrow(argv[++i]);
    } else if (arg == "--lessons" && i + 1 < argc) {
      lessonPaths.push_back(Narrow(argv[++i]));
    } else if (arg == "--out" && i + 1 < argc) {
      outPath = Narrow(argv[++i]);
    } else if (arg == "--syllables" && i + 1 < argc) {
      syllablesPath = Narrow(argv[++i]);
    } else if (arg == "--filler" && i + 1 < argc) {
      fillerPaths.push_back(Narrow(argv[++i]));
    } else if (arg == "--dropped" && i + 1 < argc) {
      droppedPath = Narrow(argv[++i]);
    } else if (arg == "--audit") {
      audit = true;
    } else if (arg == "--reachable" && i + 1 < argc) {
      reachablePath = Narrow(argv[++i]);
    } else if (arg == "--allow" && i + 1 < argc) {
      audit = true;
      auditAllowPath = Narrow(argv[++i]);
    } else if (arg == "--coverage") {
      coverage = true;
    }
  }

  auto lm = std::make_shared<McBopomofo::McBopomofoLM>();
  lm->loadLanguageModel(dataPath.c_str());
  if (!lm->isDataModelLoaded()) {
    std::cerr << "failed to load language model: " << dataPath << "\n";
    return 1;
  }
  auto relaxed = std::make_shared<mspy::RelaxedToneLM>(lm);

  std::map<std::string, double> syllableMass;
  const auto index = LoadReadingIndex(dataPath, &syllableMass);

  if (lessonPaths.empty()) lessonPaths.push_back("drills/lessons.txt");
  std::vector<Lesson> lessons;
  std::string error;
  for (const auto& path : lessonPaths) {
    if (!LoadLessons(path, index, /*droppable=*/false, &lessons, &error)) {
      std::cerr << error << "\n";
      return 1;
    }
  }
  for (const auto& path : fillerPaths) {
    if (!LoadLessons(path, index, /*droppable=*/true, &lessons, &error)) {
      std::cerr << error << "\n";
      return 1;
    }
  }

  std::set<std::string> coveredKeys;    // key sequences exercised
  std::set<std::string> coveredSyllables;
  std::set<std::string> droppedWords;   // filler words the walk gets wrong
  std::string json = "// Generated by cli/drill_gen.cpp -- do not edit.\n";
  json += "const DRILLS = [\n";

  for (const auto& lesson : lessons) {
    // Pass 1: which lines does the walk get right? A line is independent
    // of the rest -- Enter resets the composer -- so a throwaway run
    // answers this exactly. Lines that come out wrong are not patched up
    // through the candidate menu any more; a hand-written one is an error
    // to reword, a generated one is dropped and its word reported so the
    // next round picks something else.
    std::vector<const Sentence*> accepted;
    for (const auto& sentence : lesson.sentences) {
      if (SplitCodePoints(sentence.text).size() != sentence.readings.size()) {
        std::cerr << lesson.id << ": " << sentence.text
                  << " has a reading count mismatch\n";
        return 1;
      }
      Runner probe(relaxed);
      std::string fatal;
      const size_t bad =
          TypeSentence(probe, sentence, *relaxed, /*breakLine=*/false, nullptr,
                       nullptr, &fatal);
      if (!fatal.empty()) {
        std::cerr << lesson.id << ": " << fatal << "\n";
        return 1;
      }
      if (bad == std::string::npos) {
        accepted.push_back(&sentence);
        continue;
      }
      const std::string culprit = sentence.words[sentence.wordOfChar[bad]];
      // stdout, not stderr: a dropped filler line is routine, and PowerShell
      // turns any stderr from a native command into a terminating error.
      std::cout << lesson.id << ": " << sentence.text << " came out as "
                << probe.ComposedSoFar() << " (" << culprit << ")\n";
      if (!lesson.droppable) return 1;
      droppedWords.insert(culprit);
    }
    if (accepted.empty()) continue;

    // Every sentence but the last gets its own line, which is the newline
    // the second Enter types; the article panel and the notepad break in
    // the same places, so the two read as one. The last line has nothing
    // after it, so committing it ends the lesson.
    std::string lessonText;
    for (size_t i = 0; i < accepted.size(); ++i) {
      lessonText += accepted[i]->text;
      if (i + 1 < accepted.size()) lessonText += "\n";
    }

    // Pass 2: the real run, over the lines that survived.
    Runner runner(relaxed);
    for (size_t i = 0; i < accepted.size(); ++i) {
      std::string fatal;
      TypeSentence(runner, *accepted[i], *relaxed,
                   /*breakLine=*/i + 1 < accepted.size(), &coveredKeys,
                   &coveredSyllables, &fatal);
    }

    json += "  {\n";
    json += "    id: " + JsonString(lesson.id) + ",\n";
    json += "    title: " + JsonString(lesson.title) + ",\n";
    json += "    intro: " + JsonString(lesson.intro) + ",\n";
    json += "    text: " + JsonString(lessonText) + ",\n";
    json += "    steps: [\n";
    for (const auto& step : runner.steps()) {
      json += "      {k:" + JsonString(step.key);
      json += ",t:" + JsonString(step.committed);
      json += ",c:[";
      for (size_t i = 0; i < step.comp.size(); ++i) {
        if (i > 0) json += ",";
        json += "[" + JsonString(step.comp[i].first) + "," +
                JsonString(std::string(1, step.comp[i].second)) + "]";
      }
      json += "]";
      json += ",a:" + std::to_string(step.anchor);
      json += ",r:" + std::to_string(step.caret);
      json += ",d:" + std::to_string(step.done);
      if (step.menuOpen) {
        json += ",m:{items:[";
        for (size_t i = 0; i < step.menuItems.size(); ++i) {
          if (i > 0) json += ",";
          json += JsonString(step.menuItems[i]);
        }
        json += "],page:" + JsonString(step.menuPage) + "}";
      }
      json += "},\n";
    }
    json += "    ]\n";
    json += "  },\n";
    std::cout << lesson.id << ": " << SplitCodePoints(lessonText).size()
              << " chars, " << runner.steps().size() << " keystrokes\n";
  }
  json += "];\n";

  std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    std::cerr << "cannot write " << outPath << "\n";
    return 1;
  }
  out << json;
  out.close();
  std::cout << "wrote " << outPath << "\n";

  if (!droppedPath.empty()) {
    std::ofstream dump(droppedPath, std::ios::binary | std::ios::trunc);
    for (const auto& word : droppedWords) dump << word << "\n";
  }
  if (!droppedWords.empty()) {
    std::cout << "dropped " << droppedWords.size()
              << " filler word(s) the walk gets wrong\n";
  }

  if (!syllablesPath.empty()) {
    std::ofstream dump(syllablesPath, std::ios::binary | std::ios::trunc);
    for (const auto& syllable : coveredSyllables) dump << syllable << "\n";
    std::cout << "wrote " << syllablesPath << "\n";
  }

  if (audit || !reachablePath.empty()) {
    // Walks the whole keyboard rather than the dictionary: every first key,
    // and every first+second pair, asking the decoder (filtered by the
    // dictionary, exactly as SyllableInput does) what it spells. That is
    // the definitive list of combinations a learner can type, so anything
    // in it that the lessons never ask for is a hole in the drills.
    std::set<std::string> allowed;
    if (!auditAllowPath.empty()) {
      std::ifstream in(auditAllowPath, std::ios::binary);
      std::string line;
      while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        const std::string entry = Trim(line);
        if (!entry.empty()) allowed.insert(entry);
      }
    }

    // syllable -> the keys that reach it (there can be several spellings)
    std::map<std::string, std::set<std::string>> spellings;
    size_t livePairs = 0, liveSingles = 0;
    const std::string secondKeys = "abcdefghijklmnopqrstuvwxyz;";
    for (char first = 'a'; first <= 'z'; ++first) {
      for (const auto& syllable : Accepted(mspy::DecodeSingleKey(first), *relaxed)) {
        spellings[syllable].insert(std::string(1, first));
        ++liveSingles;
      }
      for (char second : secondKeys) {
        const auto pair = Accepted(mspy::DecodeKeyPair(first, second), *relaxed);
        if (!pair.empty()) ++livePairs;
        for (const auto& syllable : pair) {
          spellings[syllable].insert(std::string{first, second});
        }
      }
    }

    if (!reachablePath.empty()) {
      // The target list for scripts/make-filler-lessons.py: everything the
      // keyboard can say, which is what the drills have to cover.
      std::ofstream dump(reachablePath, std::ios::binary | std::ios::trunc);
      for (const auto& [syllable, keys] : spellings) dump << syllable << "\n";
      std::cout << "wrote " << reachablePath << " (" << spellings.size()
                << " syllables)\n";
    }
    if (!audit) return 0;

    struct Hole {
      std::string syllable, keys, examples;
      double mass = 0.0;
    };
    std::vector<Hole> holes;
    size_t excused = 0;
    for (const auto& [syllable, keys] : spellings) {
      if (coveredSyllables.count(syllable) != 0) continue;
      if (allowed.count(syllable) != 0) {
        ++excused;
        continue;
      }
      Hole hole;
      hole.syllable = syllable;
      // The spelling the drill would prescribe, which is the one a learner
      // would practise; the others reach the same sound.
      if (!KeysForSyllable(syllable, *relaxed, &hole.keys)) {
        hole.keys = *keys.begin();
      }
      auto mass = syllableMass.find(syllable);
      hole.mass = mass == syllableMass.end() ? 0.0 : mass->second;
      // A few common characters, so the gap is judged by what it can type.
      std::vector<std::pair<double, std::string>> words;
      for (const char* tone : {"", "ˊ", "ˇ", "ˋ", "˙"}) {
        for (const auto& unigram : relaxed->getUnigrams(syllable + tone)) {
          if (SplitCodePoints(unigram.value()).size() == 1) {
            words.push_back({unigram.score(), unigram.value()});
          }
        }
      }
      std::sort(words.rbegin(), words.rend());
      for (size_t i = 0; i < words.size() && i < 5; ++i) {
        hole.examples += (i == 0 ? "" : " ") + words[i].second;
      }
      holes.push_back(std::move(hole));
    }
    std::sort(holes.begin(), holes.end(), [](const Hole& a, const Hole& b) {
      return a.mass > b.mass;
    });

    std::cout << "\n=== 看打練習鍵位稽核 ===\n";
    std::cout << "有效鍵位：" << liveSingles << " 個單鍵 ＋ " << livePairs
              << " 組雙鍵，共可拼出 " << spellings.size() << " 個音節\n";
    std::cout << "課文用到：" << coveredSyllables.size() << " 個音節 ／ "
              << coveredKeys.size() << " 種鍵序\n";
    std::cout << "明列略過：" << excused << " 個\n";
    if (holes.empty()) {
      std::cout << "沒有練到的：0 個 —— 全部覆蓋。\n";
    } else {
      std::cout << "沒有練到的：" << holes.size() << " 個（依詞庫使用量排序）\n";
      for (const auto& hole : holes) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", hole.mass);
        std::cout << "  " << hole.syllable << "\t" << hole.keys << "\t" << buf
                  << "\t" << hole.examples << "\n";
      }
    }
    if (!holes.empty()) return 2;
  }

  if (coverage) {
    // Which key pairs the lessons never exercise, worst offenders first, so
    // the filler lessons can be aimed at what actually matters.
    std::vector<std::pair<double, std::string>> missing;
    for (const auto& [syllable, mass] : syllableMass) {
      if (coveredSyllables.count(syllable)) continue;
      missing.push_back({mass, syllable});
    }
    std::sort(missing.rbegin(), missing.rend());
    double covered = 0.0, total = 0.0;
    for (const auto& [syllable, mass] : syllableMass) {
      total += mass;
      if (coveredSyllables.count(syllable)) covered += mass;
    }
    std::cout << "\nsyllables covered: " << coveredSyllables.size() << "/"
              << syllableMass.size() << " (" << (100.0 * covered / total)
              << "% of single-character usage)\n";
    std::cout << "key sequences exercised: " << coveredKeys.size() << "\n";
    std::cout << "top uncovered syllables:\n";
    for (size_t i = 0; i < missing.size() && i < 60; ++i) {
      std::cout << "  " << missing[i].second << "  " << missing[i].first
                << "\n";
    }
  }
  return 0;
}
