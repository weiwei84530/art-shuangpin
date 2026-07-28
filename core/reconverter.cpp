#include "reconverter.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace mspy {

Reconverter::Reconverter(
    std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm,
    ReverseLookup reverseLookup)
    : lm_(std::move(lm)), reverseLookup_(std::move(reverseLookup)) {}

bool Reconverter::start(const std::vector<std::string>& contextCps) {
  dismiss();
  if (contextCps.empty() || lm_ == nullptr || !reverseLookup_) return false;

  // Memoize reverse lookups within this call: each may be a linear scan of
  // the whole phrase DB, and the per-char top-reading fallback would repeat
  // lookups across spans.
  std::unordered_map<std::string, std::vector<FoundReading>> memo;
  auto lookup = [&](const std::string& value)
      -> const std::vector<FoundReading>& {
    auto it = memo.find(value);
    if (it == memo.end()) it = memo.emplace(value, reverseLookup_(value)).first;
    return it->second;
  };

  const size_t n = contextCps.size();
  // Longest span first, mirroring the composition menu's ordering (word
  // candidates before single characters).
  for (size_t k = n; k >= 1; --k) {
    std::string spanText;
    for (size_t i = n - k; i < n; ++i) spanText += contextCps[i];

    // Every distinct reading of the span. For k == 1 the reverse lookup
    // already yields the union over ALL readings of the anchor (破音字).
    // For k >= 2 additionally try joining each code point's top reading,
    // in case the span text is not itself a dictionary word.
    std::vector<std::string> readings;
    for (const auto& found : lookup(spanText)) readings.push_back(found.reading);
    if (k >= 2) {
      std::string joined;
      bool allFound = true;
      for (size_t i = n - k; i < n; ++i) {
        const std::vector<FoundReading>& perChar = lookup(contextCps[i]);
        const FoundReading* top = nullptr;
        for (const auto& found : perChar) {
          if (top == nullptr || found.score > top->score) top = &found;
        }
        if (top == nullptr) {
          allFound = false;
          break;
        }
        if (!joined.empty()) joined += "-";
        joined += top->reading;
      }
      if (allFound) readings.push_back(joined);
    }
    std::sort(readings.begin(), readings.end());
    readings.erase(std::unique(readings.begin(), readings.end()),
                   readings.end());

    // Union of unigrams over the readings, deduplicated by value keeping
    // the best score; the span's current text is filtered out (selecting
    // it would change nothing).
    std::unordered_map<std::string, double> best;
    for (const std::string& reading : readings) {
      for (const auto& unigram : lm_->getUnigrams(reading)) {
        if (unigram.value() == spanText) continue;
        auto [it, inserted] = best.emplace(unigram.value(), unigram.score());
        if (!inserted && unigram.score() > it->second) {
          it->second = unigram.score();
        }
      }
    }
    std::vector<Candidate> spanCandidates;
    spanCandidates.reserve(best.size());
    for (const auto& [value, score] : best) {
      spanCandidates.push_back(Candidate{value, k, score});
    }
    // Score descending; tie-break on the value for determinism.
    std::sort(spanCandidates.begin(), spanCandidates.end(),
              [](const Candidate& a, const Candidate& b) {
                if (a.score != b.score) return a.score > b.score;
                return a.value < b.value;
              });
    candidates_.insert(candidates_.end(), spanCandidates.begin(),
                       spanCandidates.end());
  }

  if (candidates_.empty()) return false;
  active_ = true;
  return true;
}

void Reconverter::dismiss() {
  active_ = false;
  candidates_.clear();
  pageIndex_ = 0;
}

size_t Reconverter::pageCount() const {
  return (candidates_.size() + kPageSize - 1) / kPageSize;
}

std::vector<Reconverter::Candidate> Reconverter::currentPageCandidates()
    const {
  std::vector<Candidate> page;
  const size_t begin = pageIndex_ * kPageSize;
  const size_t end = std::min(candidates_.size(), begin + kPageSize);
  for (size_t i = begin; i < end; ++i) page.push_back(candidates_[i]);
  return page;
}

Reconverter::KeyResult Reconverter::feedKey(char c) {
  if (!active_) return {Action::kNone, {}};

  if (c >= '1' && c <= '6') {
    const size_t index =
        pageIndex_ * kPageSize + static_cast<size_t>(c - '1');
    if (index >= candidates_.size()) return {Action::kNone, {}};
    Candidate chosen = candidates_[index];
    dismiss();
    return {Action::kSelected, std::move(chosen)};
  }
  if (c == '7') {  // previous page (no wrap)
    if (pageIndex_ == 0) return {Action::kNone, {}};
    --pageIndex_;
    return {Action::kPageChanged, {}};
  }
  if (c == '8') {  // next page (no wrap)
    if (pageIndex_ + 1 >= pageCount()) return {Action::kNone, {}};
    ++pageIndex_;
    return {Action::kPageChanged, {}};
  }
  dismiss();
  return {Action::kDismissed, {}};
}

}  // namespace mspy
