// Persisted record of the corrections the user has made by hand.
//
// Behavior contract: docs/spec.md §7.
//
// Every record answers one question: "right after <context>, the reading
// <reading> means <value>". The composer applies a matching record as a
// HIGH-SCORE node override on the reading grid, which is what makes ONE
// correction enough (2026-08-09). The two stores this replaces could not
// manage that:
//
//   - the 2026-08-04 store scored a learned phrase 1e-6 above the best
//     dictionary entry for the SAME key. That wins the key but not the
//     walk: 不鏽鋼(-5) + 悲(-3) beats 不(-2) + 鏽(-3) + 鋼杯(-6.8) no
//     matter how the last term is nudged, so correcting 悲 to 杯 changed
//     nothing the next time round. Measured, not deduced.
//   - a single-character pick was widened into the two-syllable phrase
//     around it and was simply DROPPED when the neighbour was part of a
//     longer word -- picking 杯 after 不鏽鋼 learned nothing at all.
//
// File format (one record per line, fields never contain a space):
//
//     值 讀音鍵 上下文 次數 序號
//     杯 ㄅㄟ 鋼 2 17
//     杯 ㄅㄟ 鏽鋼 1 18
//     一 ㄧ ^ 1 4
//
// The context is the one OR two characters immediately before the span, and
// `^` means nothing precedes it (start of the composition, or the first
// character after punctuation, settled bopomofo or English). Both lengths
// are written on every pick, so the habit generalizes -- "after 鋼" fires in
// a sentence that never mentions 鏽 -- while the longer one wins where both
// match.
//
// There is NO time decay: strength is what the user did LAST, not how
// recently. `record` lifts the picked value above every rival for its key
// and knocks one off each rival, so a habit flips on the first correction
// and flips back just as easily; `kMaxCount` keeps the numbers bounded.
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
  // Ceiling on a record's count, so no habit becomes immovable.
  static constexpr double kMaxCount = 8.0;
  // Context for a span with nothing usable in front of it.
  static constexpr const char* kStartContext = "^";

  struct Record {
    std::string context;
    std::string reading;
    std::string value;
    double count = 1.0;
    int64_t serial = 0;
  };

  // Parses the whole file. Unparseable lines are skipped rather than
  // fatal: the file is rewritten in place and a partial write must not
  // cost the rest of it.
  void loadFromText(const std::string& text);
  std::string serialize() const;

  // Cheap gate for the per-keystroke scan: false means no record in the
  // store mentions this context at all.
  bool hasContext(const std::string& context) const;

  // The value learned for (context, reading), or empty. Ranked by count,
  // then by serial, so the strongest and then the newest record wins.
  std::string lookup(const std::string& context,
                     const std::string& reading) const;

  // Records a deliberate pick. The value ends up ranked above every rival
  // for the same (context, reading) -- one correction is enough -- and each
  // rival loses a point so the store follows the user rather than the other
  // way round.
  void record(const std::string& context, const std::string& reading,
              const std::string& value);

  // Folds another copy (normally the file as a sibling process left it)
  // into this one, keeping the stronger record of each triple. Every
  // application hosts its own TIP instance, so saving must merge.
  void mergeFrom(const UserPreferences& other);

  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }
  size_t size() const;
  // Every record, for the CLI and for tests.
  std::vector<Record> all() const;

 private:
  struct Entry {
    std::string value;
    double count = 1.0;
    int64_t serial = 0;
  };

  // Strongest first (count, then serial).
  static bool StrongerThan(const Entry& a, const Entry& b);
  std::vector<Entry>* find(const std::string& context,
                           const std::string& reading);

  // context -> reading -> values
  std::map<std::string, std::map<std::string, std::vector<Entry>>> byContext_;
  int64_t nextSerial_ = 1;
  bool dirty_ = false;
};

}  // namespace mspy
