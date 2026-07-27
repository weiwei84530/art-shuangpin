#include "double_pinyin.h"

#include <array>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace mspy {
namespace {

// Pinyin finals reachable from each second key. Order matters only when two
// candidates are both dictionary-valid, which does not happen for standard
// Mandarin syllables; keep i/ü-variants after the plain variant for
// determinism.
const std::unordered_map<char, std::vector<std::string_view>>& FinalKeyMap() {
  static const std::unordered_map<char, std::vector<std::string_view>> map = {
      {'a', {"a"}},
      {'o', {"o", "uo"}},
      {'e', {"e"}},
      {'i', {"i"}},
      {'u', {"u"}},
      {'q', {"iu"}},
      {'w', {"ia", "ua"}},
      {'r', {"er", "uan"}},
      {'t', {"ue"}},
      {'y', {"v", "uai"}},
      {'p', {"un"}},
      {'s', {"ong", "iong"}},
      {'d', {"iang", "uang"}},
      {'f', {"en"}},
      {'g', {"eng"}},
      {'h', {"ang"}},
      {'j', {"an"}},
      {'k', {"ao"}},
      {'l', {"ai"}},
      {'z', {"ei"}},
      {'x', {"ie"}},
      {'c', {"iao"}},
      {'v', {"ui", "ue"}},
      {'b', {"ou"}},
      {'n', {"in"}},
      {'m', {"ian"}},
      {';', {"ing"}},
  };
  return map;
}

enum class InitialClass {
  kPlain,     // b p m f d t n l g k h
  kSibilant,  // zh ch sh r z c s: bare "i" is the buzzing vowel, no ü finals
  kPalatal,   // j q x: u-finals are really ü-finals
  kYod,       // y
  kWau,       // w
};

struct Initial {
  std::string_view zhuyin;
  InitialClass cls;
};

// First key -> initial. 'v'/'i'/'u' are zh/ch/sh; 'a'/'e'/'o' start
// zero-initial syllables and are handled separately.
std::optional<Initial> LookupInitial(char c) {
  switch (c) {
    case 'b': return Initial{"ㄅ", InitialClass::kPlain};
    case 'p': return Initial{"ㄆ", InitialClass::kPlain};
    case 'm': return Initial{"ㄇ", InitialClass::kPlain};
    case 'f': return Initial{"ㄈ", InitialClass::kPlain};
    case 'd': return Initial{"ㄉ", InitialClass::kPlain};
    case 't': return Initial{"ㄊ", InitialClass::kPlain};
    case 'n': return Initial{"ㄋ", InitialClass::kPlain};
    case 'l': return Initial{"ㄌ", InitialClass::kPlain};
    case 'g': return Initial{"ㄍ", InitialClass::kPlain};
    case 'k': return Initial{"ㄎ", InitialClass::kPlain};
    case 'h': return Initial{"ㄏ", InitialClass::kPlain};
    case 'j': return Initial{"ㄐ", InitialClass::kPalatal};
    case 'q': return Initial{"ㄑ", InitialClass::kPalatal};
    case 'x': return Initial{"ㄒ", InitialClass::kPalatal};
    case 'r': return Initial{"ㄖ", InitialClass::kSibilant};
    case 'z': return Initial{"ㄗ", InitialClass::kSibilant};
    case 'c': return Initial{"ㄘ", InitialClass::kSibilant};
    case 's': return Initial{"ㄙ", InitialClass::kSibilant};
    case 'v': return Initial{"ㄓ", InitialClass::kSibilant};
    case 'i': return Initial{"ㄔ", InitialClass::kSibilant};
    case 'u': return Initial{"ㄕ", InitialClass::kSibilant};
    case 'y': return Initial{"", InitialClass::kYod};
    case 'w': return Initial{"", InitialClass::kWau};
    default: return std::nullopt;
  }
}

// Zero-initial syllables: bopomofo for the standalone final.
std::optional<std::string_view> ZeroInitialZhuyin(std::string_view final) {
  static const std::unordered_map<std::string_view, std::string_view> map = {
      {"a", "ㄚ"},   {"o", "ㄛ"},   {"e", "ㄜ"},   {"ai", "ㄞ"},
      {"ei", "ㄟ"},  {"ao", "ㄠ"},  {"ou", "ㄡ"},  {"an", "ㄢ"},
      {"en", "ㄣ"},  {"ang", "ㄤ"}, {"eng", "ㄥ"}, {"er", "ㄦ"},
  };
  auto it = map.find(final);
  if (it == map.end()) return std::nullopt;
  return it->second;
}

// y-initial syllables (pinyin y + final as spelled with our final keys).
std::optional<std::string_view> YodZhuyin(std::string_view final) {
  static const std::unordered_map<std::string_view, std::string_view> map = {
      {"i", "ㄧ"},     {"a", "ㄧㄚ"},   {"e", "ㄧㄝ"},   {"ao", "ㄧㄠ"},
      {"ou", "ㄧㄡ"},  {"an", "ㄧㄢ"},  {"in", "ㄧㄣ"},  {"ing", "ㄧㄥ"},
      {"ang", "ㄧㄤ"}, {"ong", "ㄩㄥ"}, {"u", "ㄩ"},     {"v", "ㄩ"},
      {"ue", "ㄩㄝ"},  {"uan", "ㄩㄢ"}, {"un", "ㄩㄣ"},  {"o", "ㄧㄛ"},
  };
  auto it = map.find(final);
  if (it == map.end()) return std::nullopt;
  return it->second;
}

// w-initial syllables.
std::optional<std::string_view> WauZhuyin(std::string_view final) {
  static const std::unordered_map<std::string_view, std::string_view> map = {
      {"u", "ㄨ"},    {"a", "ㄨㄚ"},   {"o", "ㄨㄛ"},  {"ai", "ㄨㄞ"},
      {"ei", "ㄨㄟ"}, {"an", "ㄨㄢ"},  {"en", "ㄨㄣ"}, {"ang", "ㄨㄤ"},
      {"eng", "ㄨㄥ"},
  };
  auto it = map.find(final);
  if (it == map.end()) return std::nullopt;
  return it->second;
}

// Finals after a real consonant initial. `palatal` turns u-family finals
// into ü-family (ju/jun/juan/ju e); `sibilant` makes bare "i" the buzzing
// vowel (empty final) and forbids i/ü-medial finals.
std::optional<std::string> ConsonantFinalZhuyin(std::string_view final,
                                                InitialClass cls) {
  const bool palatal = cls == InitialClass::kPalatal;
  const bool sibilant = cls == InitialClass::kSibilant;

  if (final == "i") {
    if (sibilant) return std::string("");  // zhi/chi/shi/ri/zi/ci/si
    return std::string("ㄧ");
  }
  if (final == "u") return std::string(palatal ? "ㄩ" : "ㄨ");
  if (final == "v") {
    if (sibilant) return std::nullopt;  // no ü after zh/ch/sh/r/z/c/s
    return std::string("ㄩ");
  }
  if (final == "ue") {
    if (sibilant) return std::nullopt;
    return std::string("ㄩㄝ");
  }
  if (final == "uan") return std::string(palatal ? "ㄩㄢ" : "ㄨㄢ");
  if (final == "un") return std::string(palatal ? "ㄩㄣ" : "ㄨㄣ");

  static const std::unordered_map<std::string_view, std::string_view> plain = {
      {"a", "ㄚ"},     {"o", "ㄛ"},     {"e", "ㄜ"},     {"ai", "ㄞ"},
      {"ei", "ㄟ"},    {"ao", "ㄠ"},    {"ou", "ㄡ"},    {"an", "ㄢ"},
      {"en", "ㄣ"},    {"ang", "ㄤ"},   {"eng", "ㄥ"},   {"ong", "ㄨㄥ"},
      {"ia", "ㄧㄚ"},  {"ie", "ㄧㄝ"},  {"iao", "ㄧㄠ"}, {"iu", "ㄧㄡ"},
      {"ian", "ㄧㄢ"}, {"in", "ㄧㄣ"},  {"iang", "ㄧㄤ"},{"ing", "ㄧㄥ"},
      {"iong", "ㄩㄥ"},{"ua", "ㄨㄚ"},  {"uo", "ㄨㄛ"},  {"uai", "ㄨㄞ"},
      {"ui", "ㄨㄟ"},  {"uang", "ㄨㄤ"},
  };
  auto it = plain.find(final);
  if (it == plain.end()) return std::nullopt;

  const char lead = final.front();
  // j/q/x only take i/ü-family finals (the ü rewrites returned above).
  if (palatal && lead != 'i') return std::nullopt;
  // zh/ch/sh/r/z/c/s take no i-medial finals.
  if (sibilant && lead == 'i') return std::nullopt;
  // iong occurs only after j/q/x (and y, handled in YodZhuyin).
  if (!palatal && final == "iong") return std::nullopt;
  return std::string(it->second);
}

}  // namespace

bool IsFirstKey(char c) { return c >= 'a' && c <= 'z'; }

std::string FirstKeyDisplay(char c) {
  // Keys without a dedicated bopomofo initial still show a representative
  // symbol so every first key reads as bopomofo, never as a raw letter
  // (the second key corrects the actual syllable): y/w show the medial
  // they usually start, a/e/o show their standalone vowel.
  switch (c) {
    case 'y': return "ㄧ";
    case 'w': return "ㄨ";
    case 'a': return "ㄚ";
    case 'e': return "ㄜ";
    case 'o': return "ㄛ";
    default: break;
  }
  auto initial = LookupInitial(c);
  if (initial && !initial->zhuyin.empty()) return std::string(initial->zhuyin);
  return std::string(1, c);
}

bool IsSecondKey(char c) { return (c >= 'a' && c <= 'z') || c == ';'; }

std::vector<std::string> DecodeKeyPair(char first, char second) {
  std::vector<std::string> results;
  if (!IsFirstKey(first) || !IsSecondKey(second)) return results;

  auto finalsIt = FinalKeyMap().find(second);
  if (finalsIt == FinalKeyMap().end()) return results;
  const auto& finals = finalsIt->second;

  // Zero-initial forms: 'o' + any standalone final; 'a'/'e' use the
  // doubled-vowel spelling, so the final must begin with the same vowel.
  if (first == 'o' || first == 'a' || first == 'e') {
    for (const auto& f : finals) {
      if (first != 'o' && f.front() != first) continue;
      if (auto z = ZeroInitialZhuyin(f)) results.emplace_back(*z);
    }
    if (first != 'o' && !results.empty()) return results;
    if (first == 'o') return results;
    // 'a'/'e' are not consonant initials; nothing else to try.
    return results;
  }

  auto initial = LookupInitial(first);
  if (!initial) return results;

  for (const auto& f : finals) {
    std::optional<std::string> zhuyinFinal;
    switch (initial->cls) {
      case InitialClass::kYod:
        if (auto z = YodZhuyin(f)) zhuyinFinal = std::string(*z);
        break;
      case InitialClass::kWau:
        if (auto z = WauZhuyin(f)) zhuyinFinal = std::string(*z);
        break;
      default:
        zhuyinFinal = ConsonantFinalZhuyin(f, initial->cls);
        break;
    }
    if (!zhuyinFinal) continue;
    results.emplace_back(std::string(initial->zhuyin) + *zhuyinFinal);
  }

  // The 'o' key is both ㄛ and ㄨㄛ. Only b/p/m/f prefer bare ㄛ (bo/po/mo/fo);
  // everything else means ㄨㄛ (duo/luo/zhuo...), and both can be
  // dictionary-valid (lo 咯 vs luo 落), so ordering matters here.
  if (second == 'o' && results.size() > 1 && first != 'b' && first != 'p' &&
      first != 'm' && first != 'f') {
    std::swap(results[0], results[1]);
  }
  return results;
}

}  // namespace mspy
