// Generator for the typing-drill data the tutorial site plays back.
//
//   drill_gen.exe --data out/data.txt --lessons drills/lessons.txt
//                 --out web/drills.js [--coverage]
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
//   - each sentence ends with its punctuation, then any character the walk
//     got wrong is corrected through the candidate menu, then Enter.

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
};

struct Lesson {
  std::string id;
  std::string title;
  std::string intro;
  std::vector<Sentence> sentences;
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

  // The whole lesson, so each step can report how much of it is right.
  void SetTarget(const std::string& text) { target_ = SplitCodePoints(text); }

  // Feeds one printable character and records the resulting screen.
  void Press(char c) {
    mspy::Composer::Result r = c == '\n' ? composer_.feedEnter()
                                         : composer_.feedChar(c);
    committed_ += r.commitText;
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

  // Corrects the composition until it reads `target`, through the candidate
  // menu, exactly as a user would. Returns false (and sets error()) when a
  // character cannot be reached that way.
  bool CorrectTo(const std::string& target) {
    for (int round = 0; round < 40; ++round) {
      const auto current = SplitCodePoints(ComposedText());
      const auto wanted = SplitCodePoints(target);
      if (current == wanted) {
        // Picking a candidate parks the cursor just past the span it
        // fixed, so typing would carry on in the MIDDLE of the sentence.
        // '=' puts it back at the end, which is what a user does too.
        if (CursorPosition() != GridLength()) Press('=');
        return true;
      }
      if (current.size() != wanted.size()) {
        error_ = "composed " + ComposedText() + " has a different length from "
                 + target;
        return false;
      }
      size_t at = 0;
      while (at < wanted.size() && current[at] == wanted[at]) ++at;

      MoveCursorTo(at);
      if (!error_.empty()) return false;
      Press('8');
      if (composer_.state() != mspy::Composer::State::kSelecting) {
        error_ = "no candidate menu at " + std::to_string(at) + " of " + target;
        return false;
      }
      if (!PickCandidateFor(wanted, at)) return false;
    }
    error_ = "gave up correcting towards " + target;
    return false;
  }

 private:
  // Finds the candidate that makes the text from `at` match, pages to it and
  // presses its digit.
  bool PickCandidateFor(const std::vector<std::string>& wanted, size_t at) {
    const auto& candidates = composer_.candidates();
    size_t chosen = candidates.size();
    for (size_t i = 0; i < candidates.size(); ++i) {
      const auto& candidate = candidates[i];
      if (candidate.location > at) continue;
      const size_t end = candidate.location + candidate.spanningLength;
      if (end > wanted.size()) continue;
      std::string expected;
      for (size_t k = candidate.location; k < end; ++k) expected += wanted[k];
      if (candidate.value != expected) continue;
      if (end <= at) continue;  // fixes nothing at the mismatch
      chosen = i;
      break;
    }
    if (chosen == candidates.size()) {
      std::string offered;
      for (size_t i = 0; i < candidates.size() && i < 12; ++i) {
        offered += " " + candidates[i].value;
      }
      error_ = "no candidate for " + wanted[at] + " at " + std::to_string(at) +
               "; offered" + offered;
      composer_.feedEsc();
      return false;
    }

    const size_t page = chosen / mspy::Composer::kCandidatePageSize;
    while (composer_.candidatePageIndex() < page) Press('8');
    Press(static_cast<char>(
        '1' + (chosen % mspy::Composer::kCandidatePageSize)));
    return true;
  }

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
    // How much of the lesson is right SO FAR -- the common prefix, not the
    // character count, so that during a correction the site points at the
    // character being fixed rather than at the end of the sentence.
    const auto produced = SplitCodePoints(committed_ + ComposedText());
    step.done = 0;
    while (step.done < produced.size() && step.done < target_.size() &&
           produced[step.done] == target_[step.done]) {
      ++step.done;
    }
    steps_.push_back(std::move(step));
  }

  mspy::Composer composer_;
  std::vector<std::string> target_;
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
                 std::vector<Lesson>* lessons, std::string* error) {
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

      if (!forced.empty()) {
        const auto readings = Split(forced, '-');
        if (readings.size() != characters.size()) {
          *error = path + ":" + std::to_string(lineNo) + ": " + word +
                   " has " + std::to_string(characters.size()) +
                   " characters but " + std::to_string(readings.size()) +
                   " readings";
          return false;
        }
        sentence.text += word;
        for (const auto& reading : readings) sentence.readings.push_back(reading);
        continue;
      }

      auto it = index.find(word);
      if (it == index.end()) {
        // Punctuation and any other literal: typed as itself.
        if (characters.size() == 1) {
          sentence.text += word;
          sentence.readings.push_back("");
          continue;
        }
        *error = path + ":" + std::to_string(lineNo) + ": no reading for " +
                 word + " (add one after a slash)";
        return false;
      }
      if (it->second.size() > 1) {
        // Several readings: take the one the dictionary scores highest and
        // say so, since only the author can tell whether it is the one the
        // sentence means.
        std::string options;
        for (size_t k = 1; k < it->second.size() && k < 4; ++k) {
          options += " " + it->second[k].reading;
        }
        std::cout << "  note: " << word << " read as "
                  << it->second.front().reading << " (also" << options
                  << ")\n";
      }
      sentence.text += word;
      for (const auto& reading : Split(it->second.front().reading, '-')) {
        sentence.readings.push_back(reading);
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

}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);

  std::string dataPath = "out/data.txt";
  std::vector<std::string> lessonPaths;
  std::string outPath = "web/drills.js";
  std::string syllablesPath;
  bool coverage = false;
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
    if (!LoadLessons(path, index, &lessons, &error)) {
      std::cerr << error << "\n";
      return 1;
    }
  }

  std::set<std::string> coveredKeys;    // key sequences exercised
  std::set<std::string> coveredSyllables;
  std::string json = "// Generated by cli/drill_gen.cpp -- do not edit.\n";
  json += "const DRILLS = [\n";

  for (const auto& lesson : lessons) {
    Runner runner(relaxed);
    runner.SetTarget(lesson.text());
    // Everything typed since the last Enter. Punctuation settles the
    // syllable in front of it, so that is where the run can compare what
    // the walk produced against the lesson -- and a comma does that just as
    // well as a full stop, which keeps corrections local instead of piling
    // them up at the end of a long line.
    std::string pending;
    for (const auto& sentence : lesson.sentences) {
      const auto characters = SplitCodePoints(sentence.text);
      if (characters.size() != sentence.readings.size()) {
        std::cerr << lesson.id << ": " << sentence.text
                  << " has a reading count mismatch\n";
        return 1;
      }
      for (size_t i = 0; i < characters.size(); ++i) {
        const std::string& reading = sentence.readings[i];
        if (reading.empty()) {
          const char key = PunctuationKey(characters[i]);
          if (key == 0) {
            std::cerr << lesson.id << ": no key types " << characters[i]
                      << "\n";
            return 1;
          }
          runner.Press(key);
          pending += characters[i];
          if (!runner.CorrectTo(pending)) {
            std::cerr << lesson.id << ": " << runner.error() << "\n";
            return 1;
          }
          // Only a sentence ending sends the buffer to the application; a
          // comma just carries on in the same composition.
          if (IsSentenceEnd(characters[i])) {
            runner.Press('\n');
            pending.clear();
          }
          continue;
        }
        std::string bare;
        char tone = '1';
        SplitTone(reading, &bare, &tone);
        std::string keys;
        if (!KeysForSyllable(bare, *relaxed, &keys)) {
          std::cerr << lesson.id << ": no key pair spells " << bare << "\n";
          return 1;
        }
        coveredKeys.insert(keys);
        coveredSyllables.insert(bare);
        for (char key : keys) runner.Press(key);
        // Tones 1 and 5 are never typed: no digit at all already means
        // "tone 1 or neutral" (docs/spec.md §5), so 的 is d + Space, not
        // d + 6. The explicit digits exist only to EXCLUDE the other one,
        // which a drill never needs.
        if (tone != '1' && tone != '5') {
          runner.Press(ToneKeyFor(tone, keys.back()));
        } else if (keys.size() == 1) {
          // A lone key is never settled implicitly (docs/spec.md §1).
          runner.Press(' ');
        }
        pending += characters[i];
      }
    }

    json += "  {\n";
    json += "    id: " + JsonString(lesson.id) + ",\n";
    json += "    title: " + JsonString(lesson.title) + ",\n";
    json += "    intro: " + JsonString(lesson.intro) + ",\n";
    json += "    text: " + JsonString(lesson.text()) + ",\n";
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
    std::cout << lesson.id << ": " << SplitCodePoints(lesson.text()).size()
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

  if (!syllablesPath.empty()) {
    std::ofstream dump(syllablesPath, std::ios::binary | std::ios::trunc);
    for (const auto& syllable : coveredSyllables) dump << syllable << "\n";
    std::cout << "wrote " << syllablesPath << "\n";
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
