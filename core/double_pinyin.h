// Microsoft double-pinyin key-pair decoding.
//
// Every syllable is exactly two keys: an initial key and a final key
// (see docs/spec.md for the authoritative tables). Decoding returns
// tone-less bopomofo syllable candidates; a final key can map to several
// pinyin finals (e.g. 's' = ong/iong), and structurally impossible
// combinations are filtered here while dictionary-level validity (does any
// tone of this syllable exist?) is the caller's job.

#pragma once

#include <string>
#include <vector>

namespace mspy {

// Decodes a key pair into candidate bopomofo syllables (no tone marks).
// Returns an empty vector if the pair is structurally invalid.
std::vector<std::string> DecodeKeyPair(char first, char second);

// True if the character can start a syllable (a-z).
bool IsFirstKey(char c);

// True if the character can be the second key of a syllable (a-z or ';').
bool IsSecondKey(char c);

// Bopomofo display for a lone first key (e.g. 'u' -> "ㄕ"); returns the raw
// character for keys without an unambiguous bopomofo (a/e/o/y/w).
std::string FirstKeyDisplay(char c);

}  // namespace mspy
