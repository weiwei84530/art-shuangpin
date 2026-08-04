// Language-model layer that applies the user's learned picks
// (UserPreferences) on top of the tone-relaxed dictionary.
//
// It sits ABOVE RelaxedToneLM -- Composer -> UserPreferenceLM ->
// RelaxedToneLM -> McBopomofoLM -- so it sees the final candidate list with
// the tone semantics already resolved, and only has to strip the internal
// explicit-tone-1 sentinel to match the keys the store holds.
//
// A live preference is scored just above the best dictionary candidate for
// the SAME key, ordered among the preferences themselves by weight. The
// margin is deliberately tiny: it is enough to win that key, and small
// enough that a longer phrase spanning the same syllables still wins the
// walk on its own merits (the 丼 / 動作 problem McBopomofoLM guards against
// with the same trick). A preference that has decayed away contributes
// nothing at all, which is what lets a one-off contextual pick expire.
//
// Preferences may name a phrase the dictionary does not have -- the
// two-syllable context phrases learned from single-character picks usually
// are -- so when the inner model returns nothing for a key, the base score
// is synthesized from the per-syllable bests. The coined phrase then beats
// spelling it out one character at a time by exactly the same margin.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gramambular2/language_model.h"
#include "user_preferences.h"

namespace mspy {

class UserPreferenceLM : public Formosa::Gramambular2::LanguageModel {
 public:
  UserPreferenceLM(std::shared_ptr<Formosa::Gramambular2::LanguageModel> inner,
                   std::shared_ptr<UserPreferences> preferences);

  std::vector<Unigram> getUnigrams(const std::string& key) override;
  bool hasUnigrams(const std::string& key) override;

  // Injectable clock (Unix seconds); tests drive decay through it.
  std::function<int64_t()> clock;

  // Strips the internal explicit-tone-1 sentinel so a reading typed as
  // ㄍㄜˉ matches the ㄍㄜ the store holds. Also used by the shell when
  // recording, so both sides normalize identically.
  static std::string NormalizeKey(const std::string& key);

  // Wall clock in Unix seconds -- the default `clock`, and what the shell
  // uses to date entries before the model exists.
  static int64_t SystemNowSeconds();

 private:
  // Best dictionary score for a key, or the sum of the per-syllable bests
  // when the dictionary has no entry for the phrase as a whole.
  bool baseScoreFor(const std::string& key,
                    const std::vector<Unigram>& innerUnigrams, double* out);

  std::shared_ptr<Formosa::Gramambular2::LanguageModel> inner_;
  std::shared_ptr<UserPreferences> preferences_;
};

}  // namespace mspy
