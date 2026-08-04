#include "user_preferences.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mspy {

namespace {

// Splits on single spaces; the fields (a Chinese phrase, a bopomofo key and
// two integers) never contain one themselves.
std::vector<std::string> SplitFields(const std::string& line) {
  std::vector<std::string> fields;
  std::istringstream in(line);
  std::string field;
  while (in >> field) fields.push_back(field);
  return fields;
}

}  // namespace

double UserPreferences::WeightAt(const Entry& entry, int64_t now) {
  // A timestamp in the future (clock change, or a file copied from another
  // machine) must not inflate the weight, so clamp the age at zero.
  const double age = static_cast<double>(std::max<int64_t>(0, now - entry.lastUsed));
  const double weight = entry.count * std::pow(0.5, age / kHalfLifeSeconds);
  return weight < kMinWeight ? 0.0 : weight;
}

void UserPreferences::loadFromText(const std::string& text,
                                   int64_t legacyTimestamp) {
  byKey_.clear();
  dirty_ = false;

  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;

    const auto fields = SplitFields(line);
    // "值 讀音鍵" (old format) or "值 讀音鍵 次數 最後使用秒數".
    if (fields.size() != 2 && fields.size() != 4) continue;

    Entry entry;
    entry.value = fields[0];
    const std::string& key = fields[1];
    if (entry.value.empty() || key.empty()) continue;

    if (fields.size() == 4) {
      try {
        entry.count = std::stod(fields[2]);
        entry.lastUsed = std::stoll(fields[3]);
      } catch (...) {
        continue;  // corrupt numbers: skip the line rather than the file
      }
      if (!(entry.count > 0.0)) continue;
      entry.count = std::min(entry.count, kMaxCount);
    } else {
      // Old format: one pick, at an unknown time. Dating it from the
      // migration keeps the vocabulary the user already built while still
      // letting it age out if it turns out to be stale.
      entry.count = 1.0;
      entry.lastUsed = legacyTimestamp;
      entry.legacy = true;
      dirty_ = true;  // rewrite in the new format on the next save
    }

    auto& entries = byKey_[key];
    auto it = std::find_if(entries.begin(), entries.end(),
                           [&](const Entry& e) { return e.value == entry.value; });
    if (it == entries.end()) {
      entries.push_back(std::move(entry));
    } else if (entry.lastUsed > it->lastUsed) {
      *it = std::move(entry);
    }
  }
}

std::string UserPreferences::serialize() const {
  std::string out;
  for (const auto& [key, entries] : byKey_) {
    for (const auto& entry : entries) {
      out += entry.value;
      out += ' ';
      out += key;
      out += ' ';
      // Counts are whole numbers in practice; print them without a decimal
      // tail so the file stays readable and diffable by hand.
      out += std::to_string(static_cast<long long>(std::lround(entry.count)));
      out += ' ';
      out += std::to_string(entry.lastUsed);
      out += '\n';
    }
  }
  return out;
}

std::vector<UserPreferences::Entry>* UserPreferences::find(
    const std::string& key) {
  auto it = byKey_.find(key);
  return it == byKey_.end() ? nullptr : &it->second;
}

std::vector<UserPreferences::Entry> UserPreferences::lookup(
    const std::string& key, int64_t now) const {
  auto it = byKey_.find(key);
  if (it == byKey_.end()) return {};

  std::vector<Entry> live;
  for (const auto& entry : it->second) {
    if (WeightAt(entry, now) > 0.0) live.push_back(entry);
  }
  std::stable_sort(live.begin(), live.end(),
                   [now](const Entry& a, const Entry& b) {
                     return WeightAt(a, now) > WeightAt(b, now);
                   });
  return live;
}

void UserPreferences::record(const std::string& key, const std::string& value,
                             int64_t now) {
  if (key.empty() || value.empty()) return;
  auto& entries = byKey_[key];
  auto it = std::find_if(entries.begin(), entries.end(),
                         [&](const Entry& e) { return e.value == value; });
  if (it == entries.end()) {
    entries.push_back(Entry{value, 1.0, now});
  } else {
    // Bump from the DECAYED weight, not the stored count: re-picking a
    // phrase last used a year ago should not resume from its old streak.
    const double decayed = WeightAt(*it, now);
    it->count = std::min(kMaxCount, std::max(1.0, decayed) + 1.0);
    it->lastUsed = now;
  }
  dirty_ = true;
}

void UserPreferences::touch(const std::string& key, const std::string& value,
                            int64_t now) {
  auto* entries = find(key);
  if (entries == nullptr) return;
  auto it = std::find_if(entries->begin(), entries->end(),
                         [&](const Entry& e) { return e.value == value; });
  if (it == entries->end()) return;
  if (it->lastUsed >= now) return;
  it->lastUsed = now;
  dirty_ = true;
}

void UserPreferences::mergeFrom(const UserPreferences& other) {
  for (const auto& [key, entries] : other.byKey_) {
    auto& mine = byKey_[key];
    for (const auto& entry : entries) {
      auto it = std::find_if(mine.begin(), mine.end(), [&](const Entry& e) {
        return e.value == entry.value;
      });
      if (it == mine.end()) {
        mine.push_back(entry);
      } else {
        // Two processes may both have picked this; keep the stronger record
        // of each field rather than letting the later writer win outright.
        it->count = std::min(kMaxCount, std::max(it->count, entry.count));
        it->lastUsed = std::max(it->lastUsed, entry.lastUsed);
      }
    }
  }
}

std::vector<std::string> UserPreferences::dropAmbiguousLegacyKeys() {
  std::vector<std::string> dropped;
  for (auto it = byKey_.begin(); it != byKey_.end();) {
    const auto& entries = it->second;
    const bool allLegacy =
        std::all_of(entries.begin(), entries.end(),
                    [](const Entry& e) { return e.legacy; });
    if (entries.size() > 1 && allLegacy) {
      dropped.push_back(it->first);
      it = byKey_.erase(it);
      dirty_ = true;
    } else {
      ++it;
    }
  }
  return dropped;
}

size_t UserPreferences::size() const {
  size_t n = 0;
  for (const auto& [key, entries] : byKey_) n += entries.size();
  return n;
}

}  // namespace mspy
