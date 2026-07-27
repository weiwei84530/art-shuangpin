#include "double_pinyin.h"

#include "gtest/gtest.h"

namespace mspy {
namespace {

TEST(DoublePinyinTest, SpecExamples) {
  // docs/spec.md §8.
  EXPECT_EQ(DecodeKeyPair('u', 'l'), std::vector<std::string>{"ㄕㄞ"});   // shai
  EXPECT_EQ(DecodeKeyPair('v', 's'), std::vector<std::string>{"ㄓㄨㄥ"});  // zhong
  EXPECT_EQ(DecodeKeyPair('o', 'j'), std::vector<std::string>{"ㄢ"});     // an
  EXPECT_EQ(DecodeKeyPair('x', ';'), std::vector<std::string>{"ㄒㄧㄥ"});  // xing
  EXPECT_EQ(DecodeKeyPair('d', 'e'), std::vector<std::string>{"ㄉㄜ"});    // de
  EXPECT_EQ(DecodeKeyPair('w', 'o'), std::vector<std::string>{"ㄨㄛ"});    // wo
}

TEST(DoublePinyinTest, Initials) {
  EXPECT_EQ(DecodeKeyPair('v', 'u'), std::vector<std::string>{"ㄓㄨ"});  // zhu
  EXPECT_EQ(DecodeKeyPair('i', 'u'), std::vector<std::string>{"ㄔㄨ"});  // chu
  EXPECT_EQ(DecodeKeyPair('u', 'u'), std::vector<std::string>{"ㄕㄨ"});  // shu
  // Buzzing-vowel syllables: zhi/chi/shi/ri/zi/ci/si.
  EXPECT_EQ(DecodeKeyPair('v', 'i'), std::vector<std::string>{"ㄓ"});
  EXPECT_EQ(DecodeKeyPair('r', 'i'), std::vector<std::string>{"ㄖ"});
  EXPECT_EQ(DecodeKeyPair('s', 'i'), std::vector<std::string>{"ㄙ"});
}

TEST(DoublePinyinTest, UmlautFinals) {
  // ü on the y key; üe on t (and its v alias); ü-family after j/q/x/y.
  EXPECT_EQ(DecodeKeyPair('n', 'y').front(), "ㄋㄩ");   // nü
  EXPECT_EQ(DecodeKeyPair('j', 't'), std::vector<std::string>{"ㄐㄩㄝ"});  // jue
  EXPECT_EQ(DecodeKeyPair('j', 'v'), std::vector<std::string>{"ㄐㄩㄝ"});  // jue (v alias)
  EXPECT_EQ(DecodeKeyPair('d', 'v').front(), "ㄉㄨㄟ");  // dui
  EXPECT_EQ(DecodeKeyPair('j', 'u'), std::vector<std::string>{"ㄐㄩ"});    // ju
  EXPECT_EQ(DecodeKeyPair('j', 'r'), std::vector<std::string>{"ㄐㄩㄢ"});  // juan
  EXPECT_EQ(DecodeKeyPair('d', 'r').front(), "ㄉㄨㄢ");                    // duan
  EXPECT_EQ(DecodeKeyPair('j', 'p'), std::vector<std::string>{"ㄐㄩㄣ"});  // jun
  EXPECT_EQ(DecodeKeyPair('d', 'p'), std::vector<std::string>{"ㄉㄨㄣ"});  // dun
  EXPECT_EQ(DecodeKeyPair('y', 'p'), std::vector<std::string>{"ㄩㄣ"});    // yun
}

TEST(DoublePinyinTest, ZeroInitialForms) {
  // 'o' + final, plus doubled-vowel spellings for a/e-leading syllables.
  EXPECT_EQ(DecodeKeyPair('o', 'a'), std::vector<std::string>{"ㄚ"});
  EXPECT_EQ(DecodeKeyPair('a', 'a'), std::vector<std::string>{"ㄚ"});
  EXPECT_EQ(DecodeKeyPair('a', 'l'), std::vector<std::string>{"ㄞ"});   // ai
  EXPECT_EQ(DecodeKeyPair('o', 'l'), std::vector<std::string>{"ㄞ"});
  EXPECT_EQ(DecodeKeyPair('e', 'e'), std::vector<std::string>{"ㄜ"});
  EXPECT_EQ(DecodeKeyPair('e', 'f'), std::vector<std::string>{"ㄣ"});   // en
  EXPECT_EQ(DecodeKeyPair('o', 'b'), std::vector<std::string>{"ㄡ"});   // ou
  EXPECT_EQ(DecodeKeyPair('e', 'r'), std::vector<std::string>{"ㄦ"});   // er
  EXPECT_EQ(DecodeKeyPair('o', 'r'), std::vector<std::string>{"ㄦ"});
  EXPECT_EQ(DecodeKeyPair('o', 'h'), std::vector<std::string>{"ㄤ"});   // ang
}

TEST(DoublePinyinTest, YodAndWau) {
  EXPECT_EQ(DecodeKeyPair('y', 'b'), std::vector<std::string>{"ㄧㄡ"});  // you
  EXPECT_EQ(DecodeKeyPair('w', 'z'), std::vector<std::string>{"ㄨㄟ"});  // wei
  EXPECT_EQ(DecodeKeyPair('y', 'i'), std::vector<std::string>{"ㄧ"});    // yi
  EXPECT_EQ(DecodeKeyPair('w', 'u'), std::vector<std::string>{"ㄨ"});    // wu
  EXPECT_EQ(DecodeKeyPair('y', 'u'), std::vector<std::string>{"ㄩ"});    // yu
  EXPECT_EQ(DecodeKeyPair('y', 's'), std::vector<std::string>{"ㄩㄥ"});  // yong
  EXPECT_EQ(DecodeKeyPair('w', 'f'), std::vector<std::string>{"ㄨㄣ"});  // wen
}

TEST(DoublePinyinTest, MedialDependentFinals) {
  EXPECT_EQ(DecodeKeyPair('j', 'w').front(), "ㄐㄧㄚ");  // jia
  // 'w' is both ia and ua for plain initials; the dictionary rejects the
  // invalid one (ㄏㄧㄚ has no entries), so both candidates must be present.
  auto hw = DecodeKeyPair('h', 'w');
  ASSERT_EQ(hw.size(), 2u);
  EXPECT_EQ(hw[1], "ㄏㄨㄚ");  // hua
  EXPECT_EQ(DecodeKeyPair('x', 's'), std::vector<std::string>{"ㄒㄩㄥ"});  // xiong
  EXPECT_EQ(DecodeKeyPair('g', 's'), std::vector<std::string>{"ㄍㄨㄥ"});  // gong
  EXPECT_EQ(DecodeKeyPair('j', 'd').front(), "ㄐㄧㄤ");  // jiang
  EXPECT_EQ(DecodeKeyPair('v', 'd').front(), "ㄓㄨㄤ");  // zhuang
  EXPECT_EQ(DecodeKeyPair('d', 'o').front(), "ㄉㄨㄛ");  // duo (o -> uo)
  EXPECT_EQ(DecodeKeyPair('b', 'o').front(), "ㄅㄛ");    // bo
}

TEST(DoublePinyinTest, StructurallyInvalidPairs) {
  EXPECT_TRUE(DecodeKeyPair('z', 'x').empty());  // z + ie
  EXPECT_TRUE(DecodeKeyPair('v', 'n').empty());  // zh + in
  EXPECT_TRUE(DecodeKeyPair('a', 'f').empty());  // 'a' + en
  EXPECT_TRUE(DecodeKeyPair('e', 'l').empty());  // 'e' + ai
  EXPECT_TRUE(DecodeKeyPair('j', 'o').empty());  // j + o/uo
  EXPECT_TRUE(DecodeKeyPair(';', 'a').empty());  // ';' is never a first key
}

TEST(DoublePinyinTest, FirstKeyDisplay) {
  EXPECT_EQ(FirstKeyDisplay('u'), "ㄕ");
  EXPECT_EQ(FirstKeyDisplay('v'), "ㄓ");
  EXPECT_EQ(FirstKeyDisplay('b'), "ㄅ");
  // Every first key displays as bopomofo, never as a raw letter.
  EXPECT_EQ(FirstKeyDisplay('y'), "ㄧ");
  EXPECT_EQ(FirstKeyDisplay('w'), "ㄨ");
  EXPECT_EQ(FirstKeyDisplay('a'), "ㄚ");
  EXPECT_EQ(FirstKeyDisplay('e'), "ㄜ");
  EXPECT_EQ(FirstKeyDisplay('o'), "ㄛ");
}

}  // namespace
}  // namespace mspy
