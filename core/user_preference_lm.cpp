#include "user_preference_lm.h"

#include <algorithm>
#include <chrono>

#include "relaxed_tone_lm.h"

namespace mspy {

namespace {

using Formosa::Gramambular2::LanguageModel;

// Margin by which a live preference outranks the best dictionary candidate
// for its own key. Big enough to win that key deterministically, small
// enough not to distort competition between spans of different lengths.
constexpr double kPreferenceMargin = 1e-6;

std::vector<std::string> SplitOnDash(const std::string& key) {
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

}  // namespace

int64_t UserPreferenceLM::SystemNowSeconds() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

UserPreferenceLM::UserPreferenceLM(std::shared_ptr<LanguageModel> inner,
                                   std::shared_ptr<UserPreferences> preferences)
    : clock(&UserPreferenceLM::SystemNowSeconds),
      inner_(std::move(inner)),
      preferences_(std::move(preferences)) {}

std::string UserPreferenceLM::NormalizeKey(const std::string& key) {
  std::string out = key;
  size_t pos;
  while ((pos = out.find(kToneSentinel1)) != std::string::npos) {
    out.erase(pos, sizeof(kToneSentinel1) - 1);
  }
  return out;
}

bool UserPreferenceLM::baseScoreFor(const std::string& key,
                                    const std::vector<Unigram>& innerUnigrams,
                                    double* out) {
  if (!innerUnigrams.empty()) {
    double best = innerUnigrams.front().score();
    for (const auto& u : innerUnigrams) best = std::max(best, u.score());
    *out = best;
    return true;
  }

  // The dictionary does not know this phrase (a learned context phrase).
  // Its natural cost is spelling it out syllable by syllable, so match that
  // and let the margin below decide.
  double sum = 0.0;
  for (const auto& syllable : SplitOnDash(key)) {
    const auto unigrams = inner_->getUnigrams(syllable);
    if (unigrams.empty()) return false;  // untypeable; leave it alone
    double best = unigrams.front().score();
    for (const auto& u : unigrams) best = std::max(best, u.score());
    sum += best;
  }
  *out = sum;
  return true;
}

std::vector<LanguageModel::Unigram> UserPreferenceLM::getUnigrams(
    const std::string& key) {
  auto unigrams = inner_->getUnigrams(key);

  const auto preferred =
      preferences_->lookup(NormalizeKey(key), clock ? clock() : 0);
  if (preferred.empty()) return unigrams;

  double base = 0.0;
  if (!baseScoreFor(key, unigrams, &base)) return unigrams;

  // Strongest preference first, so rank 0 gets the largest margin.
  for (size_t i = 0; i < preferred.size(); ++i) {
    const double score =
        base + kPreferenceMargin * static_cast<double>(preferred.size() - i);
    auto it = std::find_if(unigrams.begin(), unigrams.end(),
                           [&](const Unigram& u) {
                             return u.value() == preferred[i].value;
                           });
    if (it == unigrams.end()) {
      unigrams.emplace_back(preferred[i].value, score);
    } else if (score > it->score()) {
      *it = Unigram(preferred[i].value, score);
    }
  }

  std::stable_sort(unigrams.begin(), unigrams.end(),
                   [](const Unigram& a, const Unigram& b) {
                     return a.score() > b.score();
                   });
  return unigrams;
}

bool UserPreferenceLM::hasUnigrams(const std::string& key) {
  if (inner_->hasUnigrams(key)) return true;
  // A learned context phrase the dictionary lacks still has to be reachable
  // -- but only when getUnigrams will actually produce it. Claiming a key
  // the model then returns nothing for would leave the grid holding an
  // empty node.
  if (preferences_->lookup(NormalizeKey(key), clock ? clock() : 0).empty()) {
    return false;
  }
  double base = 0.0;
  return baseScoreFor(key, {}, &base);
}

}  // namespace mspy
