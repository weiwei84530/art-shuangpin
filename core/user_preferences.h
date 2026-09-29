// Persisted record of the corrections the user has made by hand.
//
// Behavior contract: docs/spec.md §7.
//
// Every record is one EXAMPLE of a manual pick: "the reading <reading>,
// between <left> and <right>, was picked as <value>". The composer applies
// the example that best matches the sentence being typed as a HIGH-SCORE
// node override on the reading grid, which is what makes ONE correction
// enough (2026-08-09).
//
// The window around the span (2026-09-29):
//
//   left   up to two CHARACTERS in front of the span, nearest last. `^`
//          stands for "nothing usable in front": the start of the
//          composition, or punctuation, settled bopomofo or English.
//   right  up to two READINGS after the span, nearest first. Readings, not
//          characters, because what follows is often still wrong itself
//          when the span is decided (再見 has to fire while it still reads
//          再建). Nothing past the end of the composition or a literal.
//          Compared without tone marks: tone 1 is usually left off, and 一
//          is typed ㄧˊ or ㄧˋ by sandhi, without it being another word.
//
// Matching counts how many window tokens agree, walking outward from the
// span on each side and stopping at the first disagreement. The example
// with the most agreeing characters and readings wins; a tie goes to the
// one with more agreement on the RIGHT (what follows decides 在/再 far more
// often than what precedes), then to one that also agrees on `^`. The
// boundary never outweighs a character: it is a position, not a word. A single character still tied after that is left to the
// dictionary rather than guessed -- that is what stops "我在吃飯" and
// "我再吃一碗" from flipping each other back and forth. A phrase breaks the
// tie by recency instead, and needs no agreeing token at all: a phrase
// picked once applies after anything.
//
// There are no counts: the newest pick wins. `record` deletes every older
// example that disagrees in value but not in context (every token the two
// windows both have agrees), because such an example would otherwise tie
// with -- or outrank -- the pick just made, in the very sentence it was
// made in. Examples that disagree in context stay; they are how the same
// reading learns two answers.
//
// File format (one record per line, fields never contain a space):
//
//     值 讀音鍵 左 右 序號
//     再 ㄗㄞˋ ^我 ㄔ-ㄧ 17
//     在 ㄗㄞˋ ^我 ㄔ-ㄈㄢˋ 18
//     鋼杯 ㄍㄤ-ㄅㄟ 鏽 * 4
//
// `*` is an empty right window. The pre-2026-09-29 format
// (值 讀音鍵 上下文 次數 序號, the fourth field a number) is still read, so
// the file an older build left behind carries over: each (context, reading)
// keeps its strongest value as an example with an empty right window.
// Every record carries a serial so that two application processes, each
// with its own copy of the store, merge to the same answer.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mspy {

class UserPreferences {
 public:
  // Window size on each side of the span.
  static constexpr size_t kMaxLeft = 2;
  static constexpr size_t kMaxRight = 2;
  // Left token for "nothing usable in front of here".
  static constexpr const char* kBoundary = "^";

  // What surrounds a span in the sentence being typed or picked in.
  struct Situation {
    std::vector<std::string> left;   // text order; the nearest is back()
    std::vector<std::string> right;  // text order; the nearest is front()
  };

  // The outcome of a lookup.
  struct Match {
    // Some example matched. With `value` empty the best matches disagree
    // and the dictionary should decide.
    bool found = false;
    std::string value;
    // Agreeing characters (left, the boundary not counted) and readings
  // (right) of the winning example.
    size_t leftMatched = 0;
    size_t rightMatched = 0;
    // The winning example also agrees on the boundary in front.
    bool boundary = false;
    // Serial of the winning example: larger is newer.
    int64_t serial = 0;
  };

  // One stored example, as the file spells it.
  struct Record {
    std::string value;
    std::string reading;
    std::string left;
    std::string right;
    int64_t serial = 0;
  };

  // Parses the whole file, in either format. Unparseable lines are skipped
  // rather than fatal: the file is rewritten in place and a partial write
  // must not cost the rest of it.
  void loadFromText(const std::string& text);
  std::string serialize() const;

  // Cheap gate for the per-keystroke scan: false means nothing was ever
  // picked for this reading.
  bool hasReading(const std::string& reading) const;

  // The best example for `reading` in `situation`. A reading of several
  // syllables (a phrase) matches with no agreeing token; a single
  // character needs at least one (the boundary counts for this).
  Match lookup(const Situation& situation, const std::string& reading) const;

  // Records a deliberate pick made in `situation`. The next lookup in the
  // same situation returns `value`, whatever was on file before.
  void record(const Situation& situation, const std::string& reading,
              const std::string& value);

  // Folds another copy (normally the file as a sibling process left it)
  // into this one. Every application hosts its own TIP instance, so saving
  // must merge; newer picks win exactly as they would in one process.
  void mergeFrom(const UserPreferences& other);

  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }
  size_t size() const;
  // Every record, for the CLI and for tests.
  std::vector<Record> all() const;

  // The file's spelling of a window side.
  static std::string FormatLeft(const std::vector<std::string>& left);
  static std::string FormatRight(const std::vector<std::string>& right);

 private:
  struct Example {
    std::string value;
    std::vector<std::string> left;
    std::vector<std::string> right;
    int64_t serial = 0;
  };

  // Newest first, then drops every example that a newer one contradicts
  // (see the header comment) or duplicates.
  static void Normalize(std::vector<Example>* examples);

  // reading -> examples, newest first
  std::map<std::string, std::vector<Example>> byReading_;
  int64_t nextSerial_ = 1;
  bool dirty_ = false;
};

}  // namespace mspy
