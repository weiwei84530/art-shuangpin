#include "syllable_input.h"

#include "double_pinyin.h"

namespace mspy {

bool SyllableInput::feed(char c) {
  if (len_ == 0) {
    if (!IsFirstKey(c)) return false;
    keys_[0] = c;
    len_ = 1;
    // Keys that already spell a syllable can be converted right away; the
    // rest keep an empty candidate list until their final arrives.
    candidates_ = DecodeSingleKey(c);
    return true;
  }
  if (len_ == 1) {
    if (!IsSecondKey(c)) return false;
    auto decoded = DecodeKeyPair(keys_[0], c);
    if (decoded.empty()) return false;
    keys_[1] = c;
    len_ = 2;
    candidates_ = std::move(decoded);
    return true;
  }
  return false;  // already complete; the composer finalizes first
}

bool SyllableInput::backspace() {
  if (len_ == 0) return false;
  --len_;
  candidates_ = len_ == 1 ? DecodeSingleKey(keys_[0]) : std::vector<std::string>{};
  return true;
}

void SyllableInput::clear() {
  len_ = 0;
  candidates_.clear();
}

std::string SyllableInput::displayText() const {
  if (len_ == 0) return "";
  if (len_ == 1) return FirstKeyDisplay(keys_[0]);
  return candidates_.empty() ? rawKeys() : candidates_.front();
}

}  // namespace mspy
