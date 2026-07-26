// Tone-relaxed LanguageModel adapter implementing the project's strict tone
// semantics on top of any inner LM (normally McBopomofoLM):
//
//   reading key           dictionary lookup
//   ---------------       ------------------------------------------
//   ㄉㄜ    (bare)        ㄉㄜ (tone 1, unmarked) UNION ㄉㄜ˙ (neutral)
//   ㄉㄜˉ   (sentinel)    ㄉㄜ only  -- "explicitly typed tone 1"
//   ㄉㄜˊ/ˇ/ˋ/˙           as-is
//
// The sentinel U+02C9 (ˉ) never appears in the data files (tone 1 is
// unmarked there), so it is safe as an internal marker. Multi-syllable keys
// are joined with '-' (gramambular2's default separator); bare syllables in
// a phrase key expand combinatorially (worst case 2^8 lookups per span).

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "gramambular2/language_model.h"

namespace mspy {

inline constexpr char kToneSentinel1[] = "ˉ";  // U+02C9, internal only
inline constexpr char kTone2[] = "ˊ";          // U+02CA
inline constexpr char kTone3[] = "ˇ";          // U+02C7
inline constexpr char kTone4[] = "ˋ";          // U+02CB
inline constexpr char kTone5[] = "˙";          // U+02D9

// Prefix for literal readings ("\x01" + one ASCII char). A literal reading
// resolves to exactly one fixed candidate whose value is that char; it is
// how English-mode text and the mode-switch spaces live inside the grid.
// 0x01 never appears in dictionary keys, so it is safe as a marker.
inline constexpr char kLiteralPrefix = '\x01';

class RelaxedToneLM : public Formosa::Gramambular2::LanguageModel {
 public:
  explicit RelaxedToneLM(
      std::shared_ptr<Formosa::Gramambular2::LanguageModel> inner);

  // Literal keys (kLiteralPrefix + char, no separator) yield a single
  // score-0 unigram; multi-reading keys containing a literal never match.
  std::vector<Unigram> getUnigrams(const std::string& key) override;
  bool hasUnigrams(const std::string& key) override;

  // True if any tone variant (1-5) of the tone-less bopomofo syllable exists
  // in the inner LM. Used by the composer to validate a decoded syllable.
  bool syllableExists(const std::string& tonelessSyllable);

 private:
  std::shared_ptr<Formosa::Gramambular2::LanguageModel> inner_;
};

}  // namespace mspy
