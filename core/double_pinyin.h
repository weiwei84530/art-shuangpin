// Microsoft double-pinyin key-pair decoding.
//
// Every syllable is exactly two keys: an initial key and a final key
// (see docs/spec.md for the authoritative tables). Decoding returns
// tone-less bopomofo syllable candidates; a final key can map to several
// pinyin finals (e.g. 's' = ong/iong), and structurally impossible
// combinations are filtered here while dictionary-level validity (does any
// tone of this syllable exist?) is the caller's job -- SyllableInput's
// validator, which is also what picks WHICH candidate the display shows
// (ㄏ + the 'y' key decodes to ㄏㄩ and ㄏㄨㄞ; only the dictionary knows
// that 懷 is the one that exists).

#pragma once

#include <string>
#include <vector>

namespace mspy {

// Decodes a key pair into candidate bopomofo syllables (no tone marks).
// Returns an empty vector if the pair is structurally invalid.
std::vector<std::string> DecodeKeyPair(char first, char second);

// Decodes a LONE first key into the syllable it stands for by itself, so
// that key plus a tone digit is a whole character: 'z' -> ㄗ (字 = z4),
// 'u' -> ㄕ (是 = u4), 'y' -> ㄧ (一 = y + Space), 'd' -> ㄉㄜ (的 =
// d + Space).
//
// Two groups qualify, and together they cover every letter (2026-08-09):
//   - keys whose bopomofo IS a syllable: ㄓㄔㄕㄖㄗㄘㄙ, ㄧㄨ, ㄚㄜㄛ
//     (2026-08-08);
//   - the remaining initials, which stand for the syllable their bopomofo
//     is RECITED with: ㄅㄛ ㄆㄛ ㄇㄛ ㄈㄛ / ㄉㄜ ㄊㄜ ㄋㄜ ㄌㄜ ㄍㄜ ㄎㄜ ㄏㄜ /
//     ㄐㄧ ㄑㄧ ㄒㄧ. That is literally "the final key you would have typed
//     anyway", so `d` and `de` are the same reading and the shortcut adds
//     no ambiguity.
std::vector<std::string> DecodeSingleKey(char first);

// True if the character can start a syllable (a-z).
bool IsFirstKey(char c);

// True if the character can be the second key of a syllable (a-z or ';').
bool IsSecondKey(char c);

// Bopomofo display for a lone first key (e.g. 'u' -> "ㄕ"); every key shows
// a representative symbol (y=ㄧ w=ㄨ a=ㄚ e=ㄜ o=ㄛ), never a raw letter.
std::string FirstKeyDisplay(char c);

// Bopomofo for a key read AS A FINAL (the '`'-hollowed-initial path, e.g.
// 'k' -> "ㄠ", 'x' -> "ㄧㄝ"). Ambiguous keys show their primary final.
// Returns empty for keys that are not second keys.
std::string HollowFinalDisplay(char c);

}  // namespace mspy
