#include "relaxed_tone_lm.h"

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace mspy {
namespace {

using Formosa::Gramambular2::LanguageModel;

bool EndsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> SplitKey(const std::string& key) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (true) {
    size_t dash = key.find('-', start);
    if (dash == std::string::npos) {
      parts.push_back(key.substr(start));
      return parts;
    }
    parts.push_back(key.substr(start, dash - start));
    start = dash + 1;
  }
}

// Expands one syllable per the tone semantics table in the header.
std::vector<std::string> ExpandSyllable(const std::string& syl) {
  if (EndsWith(syl, kToneSentinel1)) {
    return {syl.substr(0, syl.size() - sizeof(kToneSentinel1) + 1)};
  }
  if (EndsWith(syl, kTone2) || EndsWith(syl, kTone3) || EndsWith(syl, kTone4) ||
      EndsWith(syl, kTone5)) {
    return {syl};
  }
  return {syl, syl + kTone5};
}

}  // namespace

RelaxedToneLM::RelaxedToneLM(std::shared_ptr<LanguageModel> inner)
    : inner_(std::move(inner)) {}

std::vector<LanguageModel::Unigram> RelaxedToneLM::getUnigrams(
    const std::string& key) {
  // Literal readings: one fixed candidate; any joined key containing the
  // literal marker (a would-be phrase spanning a literal) never matches.
  if (key.find(kLiteralPrefix) != std::string::npos) {
    if (key.size() > 1 && key[0] == kLiteralPrefix &&
        key.find('-') == std::string::npos) {
      return {Unigram(key.substr(1), 0.0)};
    }
    return {};
  }

  const auto syllables = SplitKey(key);

  // Cartesian product of the per-syllable expansions.
  std::vector<std::string> keys = {""};
  for (size_t i = 0; i < syllables.size(); ++i) {
    const auto expansions = ExpandSyllable(syllables[i]);
    std::vector<std::string> next;
    next.reserve(keys.size() * expansions.size());
    for (const auto& prefix : keys) {
      for (const auto& exp : expansions) {
        next.push_back(i == 0 ? exp : prefix + "-" + exp);
      }
    }
    keys = std::move(next);
  }

  std::vector<Unigram> merged;
  std::unordered_map<std::string, size_t> indexByValue;
  for (const auto& k : keys) {
    for (const auto& u : inner_->getUnigrams(k)) {
      auto it = indexByValue.find(u.value());
      if (it == indexByValue.end()) {
        indexByValue.emplace(u.value(), merged.size());
        merged.push_back(u);
      } else if (u.score() > merged[it->second].score()) {
        merged[it->second] = u;
      }
    }
  }

  std::stable_sort(merged.begin(), merged.end(),
                   [](const Unigram& a, const Unigram& b) {
                     return a.score() > b.score();
                   });
  return merged;
}

bool RelaxedToneLM::hasUnigrams(const std::string& key) {
  if (key.find(kLiteralPrefix) != std::string::npos) {
    return key.size() > 1 && key[0] == kLiteralPrefix &&
           key.find('-') == std::string::npos;
  }
  const auto syllables = SplitKey(key);
  // Fast path for single syllables (the common case during typing).
  if (syllables.size() == 1) {
    for (const auto& k : ExpandSyllable(syllables[0])) {
      if (inner_->hasUnigrams(k)) return true;
    }
    return false;
  }
  return !getUnigrams(key).empty();
}

bool RelaxedToneLM::syllableExists(const std::string& tonelessSyllable) {
  return inner_->hasUnigrams(tonelessSyllable) ||
         inner_->hasUnigrams(tonelessSyllable + kTone2) ||
         inner_->hasUnigrams(tonelessSyllable + kTone3) ||
         inner_->hasUnigrams(tonelessSyllable + kTone4) ||
         inner_->hasUnigrams(tonelessSyllable + kTone5);
}

}  // namespace mspy
