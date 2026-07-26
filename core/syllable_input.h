// Tracks the in-progress ("pending") syllable: first key -> second key
// (complete). The composer applies the optional tone digit afterwards and
// decides when to finalize the syllable into the reading grid.

#pragma once

#include <string>
#include <vector>

namespace mspy {

class SyllableInput {
 public:
  bool empty() const { return len_ == 0; }
  bool complete() const { return len_ == 2; }

  // Feeds a raw character. Returns false if it cannot extend the pending
  // syllable (invalid key, structurally impossible pair, or the syllable is
  // already complete); the caller decides what to do with the key then.
  bool feed(char c);

  // Removes the last key. Returns false if nothing is pending.
  bool backspace();

  void clear();

  // Raw keys typed so far, e.g. "u" or "ul".
  std::string rawKeys() const { return std::string(keys_, keys_ + len_); }

  // Bopomofo candidates once complete; empty otherwise.
  const std::vector<std::string>& candidates() const { return candidates_; }

  // Inline display for the pending syllable: the initial's bopomofo for a
  // lone consonant key (e.g. 'u' -> ㄕ), the raw character otherwise, and
  // the first bopomofo candidate once complete.
  std::string displayText() const;

 private:
  char keys_[2] = {0, 0};
  int len_ = 0;
  std::vector<std::string> candidates_;
};

}  // namespace mspy
