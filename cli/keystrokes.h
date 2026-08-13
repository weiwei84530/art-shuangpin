// The fastest keystrokes for a syllable -- shared by the drill generator,
// which prescribes them, and the tutorial audit, which checks a hand-written
// lesson still demonstrates them.
//
// Keystroke rules (docs/spec.md §1, §5):
//   - a syllable is its double-pinyin key pair, or the single key when that
//     key already means the whole syllable on its own (的 = d, not de);
//   - tones 2, 3 and 4 are typed with the hand OPPOSITE the one that typed
//     the syllable's last letter, using the mirrored digits (tone 3 = 3 or 8);
//   - tones 1 and 5 are never typed: no digit already means "tone 1 or
//     neutral".

#ifndef MSPY_CLI_KEYSTROKES_H_
#define MSPY_CLI_KEYSTROKES_H_

#include <algorithm>
#include <string>
#include <vector>

#include "double_pinyin.h"
#include "gramambular2/language_model.h"

namespace mspy_cli {

// Left-hand letters on a standard keyboard; everything else is right-hand.
inline bool IsLeftHand(char key) {
  static const std::string left = "qwertasdfgzxcvb12345";
  return left.find(key) != std::string::npos;
}

// The tone digit to prescribe: the mirrored right-hand digit after a
// left-hand syllable, the plain left-hand digit otherwise, so the hands
// alternate (2026-08-09).
inline char ToneKeyFor(char tone, char lastLetterKey) {
  if (!IsLeftHand(lastLetterKey)) return tone;  // left hand types 1-5
  switch (tone) {                               // right hand mirrors them
    case '1': return '0';
    case '2': return '9';
    case '3': return '8';
    case '4': return '7';
    case '5': return '6';
    default: return tone;
  }
}

// True if any tone of this toneless syllable exists -- the same test
// SyllableInput applies, so the keys derived here are the keys the IME
// actually accepts.
inline bool SyllableExists(Formosa::Gramambular2::LanguageModel& lm,
                           const std::string& syllable) {
  if (lm.hasUnigrams(syllable)) return true;
  for (const char* mark : {"ˊ", "ˇ", "ˋ"}) {
    if (lm.hasUnigrams(syllable + mark)) return true;
  }
  return false;
}

// Drops the candidates the dictionary rejects, exactly as SyllableInput
// does; the decoder is a structural superset (the y key is both ü and uai,
// so k + y decodes to ㄎㄩ and ㄎㄨㄞ and only the second one is real).
inline std::vector<std::string> Accepted(
    std::vector<std::string> decoded,
    Formosa::Gramambular2::LanguageModel& lm) {
  decoded.erase(std::remove_if(decoded.begin(), decoded.end(),
                               [&lm](const std::string& syllable) {
                                 return !SyllableExists(lm, syllable);
                               }),
                decoded.end());
  return decoded;
}

// The double-pinyin keys for a toneless bopomofo syllable, the single-key
// form first. Returns false when no key pair spells it.
inline bool KeysForSyllable(const std::string& bare,
                            Formosa::Gramambular2::LanguageModel& lm,
                            std::string* keys) {
  for (char first = 'a'; first <= 'z'; ++first) {
    const auto single = Accepted(mspy::DecodeSingleKey(first), lm);
    if (!single.empty() && single.front() == bare) {
      *keys = std::string(1, first);
      return true;
    }
  }
  for (char first = 'a'; first <= 'z'; ++first) {
    for (char second : std::string("abcdefghijklmnopqrstuvwxyz;")) {
      // Alternate spellings reach the same syllable in the same two keys,
      // so they never win on length -- but they would win on iteration
      // order (ㄨㄟ is 'v' before 'z'), and what gets prescribed has to be
      // the Microsoft spelling.
      if (mspy::IsAlternateKeyPair(first, second)) continue;
      const auto pair = Accepted(mspy::DecodeKeyPair(first, second), lm);
      if (!pair.empty() && pair.front() == bare) {
        *keys = std::string{first, second};
        return true;
      }
    }
  }
  return false;
}

}  // namespace mspy_cli

#endif  // MSPY_CLI_KEYSTROKES_H_
