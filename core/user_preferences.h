// Persisted record of the phrases the user has picked by hand.
//
// Behavior contract: docs/spec.md §7.
//
// The pre-2026-08-04 store was a plain set of "值 讀音鍵" lines fed to
// McBopomofo's UserPhrasesLM, which scores every user phrase at exactly 0.
// Zero beats every dictionary entry (those are negative log probabilities),
// the walk breaks ties with strict '>', and candidates keep file order --
// so the FIRST pick ever made for a reading won forever. One contextual
// pick (經營之道) permanently owned ㄓ-ㄉㄠˋ, and a later correction could
// never displace it. The learned file made output worse than no learning.
//
// This store fixes that by giving each entry a WEIGHT instead of a flag:
//
//     值 讀音鍵 次數 最後使用的 Unix 秒數
//     知道 ㄓ-ㄉㄠˋ 7 1754332800
//
// weight = count halved every kHalfLifeSeconds since the entry was last
// used. Below kMinWeight the entry stops overriding the dictionary at all,
// so a one-off pick fades on its own while a habit stays. `record` bumps
// the count (a deliberate pick), `touch` only refreshes the timestamp (the
// phrase was used without needing a correction), which is what keeps daily
// vocabulary from decaying.
//
// Two-field lines from the old format load as count 1 with an unknown
// (epoch-zero) timestamp: they still rank, but they are the first to fade.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mspy {

class UserPreferences {
 public:
  // A pick decays to half its weight this long after it was last used.
  static constexpr double kHalfLifeSeconds = 14 * 24 * 60 * 60;  // 14 days
  // Below this weight an entry no longer overrides the dictionary.
  static constexpr double kMinWeight = 0.5;
  // Ceiling on count, so a heavily used phrase cannot become immortal.
  static constexpr double kMaxCount = 32.0;

  struct Entry {
    std::string value;
    double count = 1.0;
    int64_t lastUsed = 0;
    // Came from a two-field line, i.e. the old store wrote it and we do not
    // know when or how often. Not serialized: rewriting the file promotes
    // the entry to a normal one.
    bool legacy = false;
  };

  // count halved per half-life since lastUsed; 0 once past kMinWeight.
  static double WeightAt(const Entry& entry, int64_t now);

  // Parses the whole file. Unparseable lines are skipped, not fatal: this
  // file is rewritten in place and a partial write must not lose the rest.
  //
  // Two-field lines from the old format carry no timestamp, so they are
  // dated `legacyTimestamp` -- pass the current time to give the phrases
  // already learned a normal lease, after which they age like any other.
  void loadFromText(const std::string& text, int64_t legacyTimestamp = 0);
  std::string serialize() const;

  // Live entries for a reading key, strongest first. Decayed-out entries
  // are omitted, so the caller sees only preferences worth applying.
  std::vector<Entry> lookup(const std::string& key, int64_t now) const;

  // A deliberate pick: creates or bumps the entry and refreshes it.
  void record(const std::string& key, const std::string& value, int64_t now);
  // The phrase was used as-is (it already won the walk). Refreshes an
  // existing entry's timestamp without bumping its count; never creates one.
  void touch(const std::string& key, const std::string& value, int64_t now);

  // Folds a second copy (normally the file as another process left it) into
  // this one, keeping the stronger record of each (key, value). Every
  // application hosts its own TIP instance, so saving must merge rather
  // than clobber a sibling's writes.
  void mergeFrom(const UserPreferences& other);

  // Drops keys that carry several values ALL of which came from the old
  // format, and returns them. That shape is the signature of the previous
  // append-only store: a stuck first pick plus the correction that could
  // never replace it, with no way to tell which the user wanted. Keys with
  // any dated entry are left alone -- there, weight already decides.
  //
  // Run once, right after a legacy load; rewriting the file clears the
  // legacy flags so it cannot fire again on real picks.
  std::vector<std::string> dropAmbiguousLegacyKeys();

  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }
  size_t size() const;

 private:
  std::vector<Entry>* find(const std::string& key);

  std::map<std::string, std::vector<Entry>> byKey_;
  bool dirty_ = false;
};

}  // namespace mspy
