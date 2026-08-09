#include "user_preferences.h"

#include <algorithm>
#include <sstream>

namespace mspy {

namespace {

// Splits on whitespace; every field (a Chinese phrase, a bopomofo key, a
// context of one or two characters and two integers) is space-free.
std::vector<std::string> SplitFields(const std::string& line) {
  std::vector<std::string> fields;
  std::istringstream in(line);
  std::string field;
  while (in >> field) fields.push_back(field);
  return fields;
}

}  // namespace

bool UserPreferences::StrongerThan(const Entry& a, const Entry& b) {
  if (a.count != b.count) return a.count > b.count;
  return a.serial > b.serial;
}

void UserPreferences::loadFromText(const std::string& text) {
  byContext_.clear();
  nextSerial_ = 1;
  dirty_ = false;

  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;

    const auto fields = SplitFields(line);
    if (fields.size() != 5) continue;  // including every pre-2026-08-09 line

    Entry entry;
    entry.value = fields[0];
    const std::string& reading = fields[1];
    const std::string& context = fields[2];
    if (entry.value.empty() || reading.empty() || context.empty()) continue;
    try {
      entry.count = std::stod(fields[3]);
      entry.serial = std::stoll(fields[4]);
    } catch (...) {
      continue;  // corrupt numbers: lose the line, not the file
    }
    if (!(entry.count > 0.0)) continue;
    entry.count = std::min(entry.count, kMaxCount);
    nextSerial_ = std::max(nextSerial_, entry.serial + 1);

    auto& entries = byContext_[context][reading];
    auto it = std::find_if(entries.begin(), entries.end(),
                           [&](const Entry& e) { return e.value == entry.value; });
    if (it == entries.end()) {
      entries.push_back(std::move(entry));
    } else if (entry.serial > it->serial) {
      *it = std::move(entry);
    }
  }

  for (auto& [context, byReading] : byContext_) {
    for (auto& [reading, entries] : byReading) {
      std::sort(entries.begin(), entries.end(), StrongerThan);
    }
  }
}

std::string UserPreferences::serialize() const {
  std::string out;
  for (const auto& [context, byReading] : byContext_) {
    for (const auto& [reading, entries] : byReading) {
      for (const auto& entry : entries) {
        out += entry.value;
        out += ' ';
        out += reading;
        out += ' ';
        out += context;
        out += ' ';
        // Counts are whole numbers in practice; keep the file hand-readable.
        out += std::to_string(static_cast<long long>(entry.count));
        out += ' ';
        out += std::to_string(entry.serial);
        out += '\n';
      }
    }
  }
  return out;
}

bool UserPreferences::hasContext(const std::string& context) const {
  return byContext_.find(context) != byContext_.end();
}

std::vector<UserPreferences::Entry>* UserPreferences::find(
    const std::string& context, const std::string& reading) {
  auto contextIt = byContext_.find(context);
  if (contextIt == byContext_.end()) return nullptr;
  auto readingIt = contextIt->second.find(reading);
  if (readingIt == contextIt->second.end()) return nullptr;
  return &readingIt->second;
}

std::string UserPreferences::lookup(const std::string& context,
                                    const std::string& reading) const {
  auto contextIt = byContext_.find(context);
  if (contextIt == byContext_.end()) return {};
  auto readingIt = contextIt->second.find(reading);
  if (readingIt == contextIt->second.end()) return {};
  const auto& entries = readingIt->second;
  return entries.empty() ? std::string() : entries.front().value;
}

void UserPreferences::record(const std::string& context,
                             const std::string& reading,
                             const std::string& value) {
  if (context.empty() || reading.empty() || value.empty()) return;

  auto& entries = byContext_[context][reading];
  double count = 1.0;
  for (auto it = entries.begin(); it != entries.end();) {
    if (it->value == value) {
      count = std::max(count, it->count + 1.0);
      it = entries.erase(it);
      continue;
    }
    // The user just said this reading means something else here, so the
    // habit that was winning has to be able to lose -- and lose NOW, not
    // after being out-counted, which is what "one correction is enough"
    // means. Rivals fade a point at a time so flipping back is just as
    // cheap.
    count = std::max(count, it->count + 1.0);
    it->count -= 1.0;
    if (it->count <= 0.0) {
      it = entries.erase(it);
    } else {
      ++it;
    }
  }
  entries.push_back(Entry{value, std::min(count, kMaxCount), nextSerial_++});
  std::sort(entries.begin(), entries.end(), StrongerThan);
  dirty_ = true;
}

void UserPreferences::mergeFrom(const UserPreferences& other) {
  for (const auto& [context, byReading] : other.byContext_) {
    for (const auto& [reading, entries] : byReading) {
      auto& mine = byContext_[context][reading];
      for (const auto& entry : entries) {
        auto it = std::find_if(mine.begin(), mine.end(), [&](const Entry& e) {
          return e.value == entry.value;
        });
        if (it == mine.end()) {
          mine.push_back(entry);
        } else {
          // Two processes may both have picked this; take the stronger and
          // newer record of each rather than letting the later writer win.
          it->count = std::min(kMaxCount, std::max(it->count, entry.count));
          it->serial = std::max(it->serial, entry.serial);
        }
        nextSerial_ = std::max(nextSerial_, entry.serial + 1);
      }
      std::sort(mine.begin(), mine.end(), StrongerThan);
    }
  }
}

size_t UserPreferences::size() const {
  size_t n = 0;
  for (const auto& [context, byReading] : byContext_) {
    for (const auto& [reading, entries] : byReading) n += entries.size();
  }
  return n;
}

std::vector<UserPreferences::Record> UserPreferences::all() const {
  std::vector<Record> records;
  for (const auto& [context, byReading] : byContext_) {
    for (const auto& [reading, entries] : byReading) {
      for (const auto& entry : entries) {
        records.push_back(
            Record{context, reading, entry.value, entry.count, entry.serial});
      }
    }
  }
  return records;
}

}  // namespace mspy
