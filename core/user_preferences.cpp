#include "user_preferences.h"

#include <algorithm>
#include <sstream>
#include <tuple>

namespace mspy {

namespace {

// Spelling of an empty right window in the file.
constexpr const char* kEmptyWindow = "*";

// Splits on whitespace; every field (a Chinese phrase, a bopomofo key, a
// window of one or two tokens and an integer) is space-free.
std::vector<std::string> SplitFields(const std::string& line) {
  std::vector<std::string> fields;
  std::istringstream in(line);
  std::string field;
  while (in >> field) fields.push_back(field);
  return fields;
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

std::vector<std::string> ParseLeft(const std::string& field) {
  if (field == kEmptyWindow) return {};
  std::vector<std::string> tokens;
  std::string rest = field;
  const std::string boundary = UserPreferences::kBoundary;
  if (rest.compare(0, boundary.size(), boundary) == 0) {
    tokens.push_back(boundary);
    rest.erase(0, boundary.size());
  }
  for (auto& c : SplitCodePoints(rest)) tokens.push_back(std::move(c));
  return tokens;
}

std::vector<std::string> ParseRight(const std::string& field) {
  std::vector<std::string> tokens;
  if (field == kEmptyWindow) return tokens;
  size_t start = 0;
  while (true) {
    const size_t dash = field.find('-', start);
    tokens.push_back(field.substr(start, dash - start));
    if (dash == std::string::npos) return tokens;
    start = dash + 1;
  }
}

// Agreeing tokens walking outward from the span: from the back of both
// left windows, from the front of both right windows.
size_t LeftAgreement(const std::vector<std::string>& a,
                     const std::vector<std::string>& b) {
  size_t n = 0;
  while (n < a.size() && n < b.size() &&
         a[a.size() - 1 - n] == b[b.size() - 1 - n]) {
    ++n;
  }
  return n;
}

// A reading without its tone mark. The right window compares these: what
// follows is evidence of which WORD comes next, and the tone there is mostly
// typing habit -- tone 1 left off, 一 typed as ㄧˊ before 碗 -- rather than
// a different word.
std::string Toneless(const std::string& reading) {
  std::string out = reading;
  for (const char* mark : {"ˊ", "ˇ", "ˋ", "˙"}) {
    const std::string m = mark;
    size_t pos;
    while ((pos = out.find(m)) != std::string::npos) out.erase(pos, m.size());
  }
  return out;
}

size_t RightAgreement(const std::vector<std::string>& a,
                      const std::vector<std::string>& b) {
  size_t n = 0;
  while (n < a.size() && n < b.size() && Toneless(a[n]) == Toneless(b[n])) {
    ++n;
  }
  return n;
}

// True if the two windows never disagree where both have a token: one
// could be the other seen from closer up.
bool Compatible(const std::vector<std::string>& leftA,
                const std::vector<std::string>& rightA,
                const std::vector<std::string>& leftB,
                const std::vector<std::string>& rightB) {
  return LeftAgreement(leftA, leftB) == std::min(leftA.size(), leftB.size()) &&
         RightAgreement(rightA, rightB) ==
             std::min(rightA.size(), rightB.size());
}

// Window tokens are characters and bopomofo readings, never ASCII (the
// boundary marker aside); anything else is a damaged line.
bool PlausibleWindow(const std::vector<std::string>& tokens) {
  for (const auto& token : tokens) {
    if (token == UserPreferences::kBoundary) continue;
    if (token.empty() || static_cast<unsigned char>(token[0]) < 0x80) {
      return false;
    }
  }
  return true;
}

bool IsNumber(const std::string& s) {
  if (s.empty()) return false;
  try {
    size_t used = 0;
    (void)std::stod(s, &used);
    return used == s.size();
  } catch (...) {
    return false;
  }
}

}  // namespace

std::string UserPreferences::FormatLeft(const std::vector<std::string>& left) {
  if (left.empty()) return kEmptyWindow;
  std::string out;
  for (const auto& token : left) out += token;
  return out;
}

std::string UserPreferences::FormatRight(
    const std::vector<std::string>& right) {
  if (right.empty()) return kEmptyWindow;
  std::string out;
  for (size_t i = 0; i < right.size(); ++i) {
    if (i > 0) out += '-';
    out += right[i];
  }
  return out;
}

void UserPreferences::Normalize(std::vector<Example>* examples) {
  std::stable_sort(examples->begin(), examples->end(),
                   [](const Example& a, const Example& b) {
                     return a.serial > b.serial;
                   });
  std::vector<Example> kept;
  for (auto& example : *examples) {
    bool drop = false;
    for (const auto& newer : kept) {
      if (!Compatible(newer.left, newer.right, example.left, example.right)) {
        continue;
      }
      // A newer pick with another value in a compatible window overrules
      // this one; the same value in the very same window is a duplicate.
      if (newer.value != example.value ||
          (newer.left == example.left && newer.right == example.right)) {
        drop = true;
        break;
      }
    }
    if (!drop) kept.push_back(std::move(example));
  }
  *examples = std::move(kept);
}

void UserPreferences::loadFromText(const std::string& text) {
  byReading_.clear();
  nextSerial_ = 1;
  dirty_ = false;

  // Pre-2026-09-29 lines, keyed by (context, reading): only the strongest
  // value of each ever showed, so only it carries over.
  struct Legacy {
    std::string value;
    double count = 0.0;
    int64_t serial = 0;
  };
  std::map<std::pair<std::string, std::string>, Legacy> legacy;

  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;

    const auto fields = SplitFields(line);
    if (fields.size() != 5) continue;  // including every pre-2026-08-09 line
    const std::string& value = fields[0];
    const std::string& reading = fields[1];
    if (value.empty() || reading.empty() || fields[2].empty()) continue;
    int64_t serial = 0;
    try {
      serial = std::stoll(fields[4]);
    } catch (...) {
      continue;  // corrupt numbers: lose the line, not the file
    }
    nextSerial_ = std::max(nextSerial_, serial + 1);

    if (IsNumber(fields[3])) {
      const double count = std::stod(fields[3]);
      if (!(count > 0.0)) continue;
      auto& slot = legacy[{fields[2], reading}];
      if (std::tie(count, serial) > std::tie(slot.count, slot.serial)) {
        slot = Legacy{value, count, serial};
      }
      continue;
    }

    Example example;
    example.value = value;
    example.left = ParseLeft(fields[2]);
    example.right = ParseRight(fields[3]);
    example.serial = serial;
    if (!PlausibleWindow(example.left) || !PlausibleWindow(example.right)) {
      continue;
    }
    byReading_[reading].push_back(std::move(example));
  }

  for (const auto& [key, entry] : legacy) {
    Example example;
    example.value = entry.value;
    example.left = ParseLeft(key.first);
    example.serial = entry.serial;
    byReading_[key.second].push_back(std::move(example));
  }
  for (auto& [reading, examples] : byReading_) Normalize(&examples);
}

std::string UserPreferences::serialize() const {
  std::string out;
  for (const auto& [reading, examples] : byReading_) {
    for (const auto& example : examples) {
      out += example.value;
      out += ' ';
      out += reading;
      out += ' ';
      out += FormatLeft(example.left);
      out += ' ';
      out += FormatRight(example.right);
      out += ' ';
      out += std::to_string(example.serial);
      out += '\n';
    }
  }
  return out;
}

bool UserPreferences::hasReading(const std::string& reading) const {
  return byReading_.find(reading) != byReading_.end();
}

UserPreferences::Match UserPreferences::lookup(
    const Situation& situation, const std::string& reading) const {
  Match match;
  auto it = byReading_.find(reading);
  if (it == byReading_.end()) return match;
  const bool phrase = reading.find('-') != std::string::npos;

  // (agreeing characters and readings, of which on the right, boundary)
  // of the best examples so far. The boundary is a position, not a
  // character: agreeing on "start of the sentence" is worth less than any
  // character and only breaks ties. Counted as a character, it made
  // 你再說一次 outscore 他在說什麼 in 你在說什麼.
  std::tuple<size_t, size_t, bool> best{0, 0, false};
  const Example* winner = nullptr;
  bool disagree = false;
  // Examples are newest first, so the first of equals is the newest.
  for (const auto& example : it->second) {
    size_t left = LeftAgreement(example.left, situation.left);
    const bool boundary = left > 0 && example.left[example.left.size() - left] ==
                                          UserPreferences::kBoundary;
    if (boundary) --left;
    const size_t right = RightAgreement(example.right, situation.right);
    if (left + right == 0 && !boundary && !phrase) continue;
    const std::tuple<size_t, size_t, bool> score{left + right, right, boundary};
    if (winner == nullptr || score > best) {
      best = score;
      winner = &example;
      disagree = false;
      match.leftMatched = left;
      match.rightMatched = right;
      match.boundary = boundary;
      match.serial = example.serial;
    } else if (score == best && example.value != winner->value) {
      disagree = true;
    }
  }
  if (winner == nullptr) return match;
  match.found = true;
  // A phrase keeps the newest of equals; a single character tied between
  // two answers is the dictionary's call.
  if (!disagree || phrase) match.value = winner->value;
  return match;
}

void UserPreferences::record(const Situation& situation,
                             const std::string& reading,
                             const std::string& value) {
  if (reading.empty() || value.empty()) return;
  auto& examples = byReading_[reading];
  Example example;
  example.value = value;
  example.left = situation.left;
  example.right = situation.right;
  example.serial = nextSerial_++;
  examples.push_back(std::move(example));
  Normalize(&examples);
  dirty_ = true;
}

void UserPreferences::mergeFrom(const UserPreferences& other) {
  for (const auto& [reading, examples] : other.byReading_) {
    auto& mine = byReading_[reading];
    for (const auto& example : examples) {
      mine.push_back(example);
      nextSerial_ = std::max(nextSerial_, example.serial + 1);
    }
    Normalize(&mine);
  }
}

size_t UserPreferences::size() const {
  size_t n = 0;
  for (const auto& [reading, examples] : byReading_) n += examples.size();
  return n;
}

std::vector<UserPreferences::Record> UserPreferences::all() const {
  std::vector<Record> records;
  for (const auto& [reading, examples] : byReading_) {
    for (const auto& example : examples) {
      records.push_back(Record{example.value, reading, FormatLeft(example.left),
                               FormatRight(example.right), example.serial});
    }
  }
  return records;
}

}  // namespace mspy
