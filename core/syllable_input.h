// Tracks the in-progress ("pending") syllable: first key -> second key
// (complete). The composer applies the optional tone digit afterwards and
// decides when to finalize the syllable into the reading grid.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace mspy {

class SyllableInput {
 public:
  // Dictionary check every decoded syllable is filtered through: given a
  // TONELESS bopomofo syllable, true if any tone of it exists.
  //
  // DecodeKeyPair is deliberately a structural superset -- the 'w' key is
  // both ia and ua, so ㄏ + w yields ㄏㄧㄚ and ㄏㄨㄚ, and the 'y' key is
  // both ü and uai, so ㄏ + y yields ㄏㄩ and ㄏㄨㄞ. Only the dictionary
  // knows which of those is a real syllable (and that ㄉㄣ or ㄎㄟ are not
  // syllables at all), so it is what decides both what the display shows
  // and whether the key pair is accepted.
  //
  // Unset = accept everything, i.e. structural decoding only.
  void setValidator(std::function<bool(const std::string&)> validator) {
    validator_ = std::move(validator);
  }

  bool empty() const { return len_ == 0; }
  // Both keys typed: the syllable can take no further letter.
  bool complete() const { return len_ == 2; }
  // Has a syllable to convert -- either complete, or a lone first key whose
  // bopomofo is already a syllable (ㄗ, ㄕ, ㄧ...). This is what a tone
  // digit, Space or Enter act on; `complete` still governs whether another
  // letter may extend the syllable.
  bool convertible() const { return !candidates_.empty(); }

  // Feeds a raw character. Returns false if it cannot extend the pending
  // syllable (invalid key, structurally impossible pair, or the syllable is
  // already complete); the caller decides what to do with the key then.
  bool feed(char c);

  // Removes the last key. Returns false if nothing is pending.
  bool backspace();

  void clear();

  // Raw keys typed so far, e.g. "u" or "ul".
  std::string rawKeys() const { return std::string(keys_, keys_ + len_); }

  // Bopomofo candidates of the syllable typed so far; empty when the keys
  // do not spell a syllable yet (a lone ㄅ).
  const std::vector<std::string>& candidates() const { return candidates_; }

  // Inline display for the pending syllable: the initial's bopomofo for a
  // lone consonant key (e.g. 'u' -> ㄕ), the raw character otherwise, and
  // the first bopomofo candidate once complete.
  std::string displayText() const;

 private:
  // Drops the syllables the validator rejects (all of them when it is unset).
  std::vector<std::string> accepted(std::vector<std::string> decoded) const;

  char keys_[2] = {0, 0};
  int len_ = 0;
  std::vector<std::string> candidates_;
  std::function<bool(const std::string&)> validator_;
};

}  // namespace mspy
