#include "relaxed_tone_lm.h"

#include <memory>

#include "gtest/gtest.h"
#include "testing/fake_lm.h"

namespace mspy {
namespace {

std::shared_ptr<testing::FakeLM> MakeInner() {
  auto lm = std::make_shared<testing::FakeLM>();
  lm->add("ㄉㄜ", "得", -3);     // tone 1 (unmarked in data)
  lm->add("ㄉㄜ˙", "的", -1);    // neutral tone
  lm->add("ㄉㄜˊ", "德", -4);    // tone 2
  lm->add("ㄇㄚ-ㄇㄚ˙", "媽媽", -2);
  return lm;
}

std::vector<std::string> Values(
    std::vector<Formosa::Gramambular2::LanguageModel::Unigram> unigrams) {
  std::vector<std::string> values;
  for (const auto& u : unigrams) values.push_back(u.value());
  return values;
}

TEST(RelaxedToneLMTest, BareSyllableMergesTone1AndNeutral) {
  RelaxedToneLM lm(MakeInner());
  auto values = Values(lm.getUnigrams("ㄉㄜ"));
  // 的 (-1) outranks 得 (-3); 德 (tone 2) must not appear.
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], "的");
  EXPECT_EQ(values[1], "得");
}

TEST(RelaxedToneLMTest, ExplicitTone1SentinelIsStrict) {
  RelaxedToneLM lm(MakeInner());
  auto values = Values(lm.getUnigrams(std::string("ㄉㄜ") + kToneSentinel1));
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0], "得");
}

TEST(RelaxedToneLMTest, MarkedTonesAreExact) {
  RelaxedToneLM lm(MakeInner());
  EXPECT_EQ(Values(lm.getUnigrams("ㄉㄜˊ")), std::vector<std::string>{"德"});
  EXPECT_EQ(Values(lm.getUnigrams("ㄉㄜ˙")), std::vector<std::string>{"的"});
}

TEST(RelaxedToneLMTest, PhraseKeyExpandsBareSyllables) {
  RelaxedToneLM lm(MakeInner());
  // 媽媽 is stored under ㄇㄚ-ㄇㄚ˙; both bare syllables must reach it.
  EXPECT_EQ(Values(lm.getUnigrams("ㄇㄚ-ㄇㄚ")), std::vector<std::string>{"媽媽"});
  EXPECT_TRUE(lm.hasUnigrams("ㄇㄚ-ㄇㄚ"));
}

TEST(RelaxedToneLMTest, HasUnigramsAndSyllableExists) {
  RelaxedToneLM lm(MakeInner());
  EXPECT_TRUE(lm.hasUnigrams("ㄉㄜ"));
  EXPECT_TRUE(lm.hasUnigrams("ㄉㄜˊ"));
  EXPECT_FALSE(lm.hasUnigrams("ㄅㄥ"));
  EXPECT_TRUE(lm.syllableExists("ㄉㄜ"));
  EXPECT_FALSE(lm.syllableExists("ㄅㄥ"));
}

}  // namespace
}  // namespace mspy
