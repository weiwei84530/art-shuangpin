#include "user_preferences.h"

#include <string>

#include "gtest/gtest.h"

namespace mspy {
namespace {

using Situation = UserPreferences::Situation;

// The value a lookup settles on; "" for no match or a tie.
std::string Value(const UserPreferences& prefs, const Situation& situation,
                  const std::string& reading) {
  return prefs.lookup(situation, reading).value;
}

TEST(UserPreferencesTest, TheNewestPickWinsAtOnce) {
  UserPreferences prefs;
  // However many times the old habit was picked...
  for (int i = 0; i < 20; ++i) prefs.record({{"鋼"}, {}}, "ㄅㄟ", "悲");
  ASSERT_EQ(Value(prefs, {{"鋼"}, {}}, "ㄅㄟ"), "悲");

  // ...one correction flips it, and one more flips it back.
  prefs.record({{"鋼"}, {}}, "ㄅㄟ", "杯");
  EXPECT_EQ(Value(prefs, {{"鋼"}, {}}, "ㄅㄟ"), "杯");
  prefs.record({{"鋼"}, {}}, "ㄅㄟ", "悲");
  EXPECT_EQ(Value(prefs, {{"鋼"}, {}}, "ㄅㄟ"), "悲");
  // No counts: the store holds the one answer, not a tally.
  EXPECT_EQ(prefs.size(), 1u);
}

TEST(UserPreferencesTest, ASingleCharacterNeedsSomeAgreement) {
  UserPreferences prefs;
  prefs.record({{"鋼"}, {}}, "ㄅㄟ", "杯");
  EXPECT_FALSE(prefs.lookup({{"茶"}, {}}, "ㄅㄟ").found);
  EXPECT_FALSE(prefs.lookup({{"^"}, {}}, "ㄅㄟ").found);
  EXPECT_TRUE(prefs.hasReading("ㄅㄟ"));
  EXPECT_FALSE(prefs.hasReading("ㄅㄟˇ"));
}

TEST(UserPreferencesTest, AgreementStopsAtTheFirstMismatch) {
  UserPreferences prefs;
  prefs.record({{"不", "鏽"}, {}}, "ㄍㄤ", "剛");
  // 鏽 matches, 不 would too, but only counting outward from the span.
  const auto match = prefs.lookup({{"不", "鏽"}, {}}, "ㄍㄤ");
  EXPECT_EQ(match.leftMatched, 2u);
  EXPECT_EQ(prefs.lookup({{"生", "鏽"}, {}}, "ㄍㄤ").leftMatched, 1u);
  EXPECT_FALSE(prefs.lookup({{"不", "繡"}, {}}, "ㄍㄤ").found);
}

// The 我在吃飯 / 我再吃一碗 pair: the same left window, told apart only by
// the second reading on the right.
class ZaiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    prefs_.record({{"^", "我"}, {"ㄔ", "ㄧ"}}, "ㄗㄞˋ", "再");
    prefs_.record({{"^", "我"}, {"ㄔ", "ㄈㄢˋ"}}, "ㄗㄞˋ", "在");
  }
  UserPreferences prefs_;
};

TEST_F(ZaiTest, TheLaterPickDoesNotEraseAnExampleItDisagreesWith) {
  EXPECT_EQ(prefs_.size(), 2u);
}

TEST_F(ZaiTest, TheLongestAgreementWins) {
  EXPECT_EQ(Value(prefs_, {{"^", "我"}, {"ㄔ", "ㄧ"}}, "ㄗㄞˋ"), "再");
  EXPECT_EQ(Value(prefs_, {{"^", "我"}, {"ㄔ", "ㄈㄢˋ"}}, "ㄗㄞˋ"), "在");
}

TEST_F(ZaiTest, AnUndecidedTieIsLeftToTheDictionary) {
  for (const Situation& s : {Situation{{"^", "我"}, {}},
                             Situation{{"^", "我"}, {"ㄔ"}},
                             Situation{{"^", "我"}, {"ㄔ", "ㄆㄧㄥˊ"}}}) {
    const auto match = prefs_.lookup(s, "ㄗㄞˋ");
    EXPECT_TRUE(match.found);
    EXPECT_EQ(match.value, "");
  }
}

TEST(UserPreferencesTest, ATieGoesToTheRightSide) {
  UserPreferences prefs;
  prefs.record({{"^", "他"}, {"ㄕㄨㄛ", "ㄕㄣˊ"}}, "ㄗㄞˋ", "在");
  prefs.record({{"^", "你"}, {"ㄕㄨㄛ", "ㄧ"}}, "ㄗㄞˋ", "再");
  ASSERT_EQ(prefs.size(), 2u);
  // 你在說什麼: two agreeing tokens each, but 在's are both on the right.
  EXPECT_EQ(Value(prefs, {{"^", "你"}, {"ㄕㄨㄛ", "ㄕㄣˊ"}}, "ㄗㄞˋ"), "在");
}

TEST(UserPreferencesTest, TheRightSideIgnoresTones) {
  UserPreferences prefs;
  // Picked with 一 typed plain...
  prefs.record({{"^", "我"}, {"ㄔ", "ㄧ"}}, "ㄗㄞˋ", "再");
  // ...and typed with its sandhi tone later, or 什 without its tone.
  const auto match = prefs.lookup({{"^", "我"}, {"ㄔ", "ㄧˊ"}}, "ㄗㄞˋ");
  EXPECT_EQ(match.rightMatched, 2u);
  EXPECT_EQ(match.value, "再");
}

TEST(UserPreferencesTest, ANewPickReplacesACompatibleOlderOne) {
  UserPreferences prefs;
  // Learned with more on the right than the new pick can see...
  prefs.record({{"^", "我"}, {"ㄔ", "ㄧ"}}, "ㄗㄞˋ", "再");
  // ...but the new pick contradicts it wherever both windows look.
  prefs.record({{"^", "我"}, {"ㄔ"}}, "ㄗㄞˋ", "在");
  EXPECT_EQ(prefs.size(), 1u);
  // So picking again in the very same sentence is never needed.
  EXPECT_EQ(Value(prefs, {{"^", "我"}, {"ㄔ"}}, "ㄗㄞˋ"), "在");
}

TEST(UserPreferencesTest, APhraseAppliesAfterAnything) {
  UserPreferences prefs;
  prefs.record({{"^", "我"}, {}}, "ㄍㄤ-ㄅㄟ", "鋼杯");
  const auto match = prefs.lookup({{"買"}, {"ㄌㄜ˙"}}, "ㄍㄤ-ㄅㄟ");
  EXPECT_TRUE(match.found);
  EXPECT_EQ(match.value, "鋼杯");
}

TEST(UserPreferencesTest, APhraseTieGoesToTheNewest) {
  UserPreferences prefs;
  prefs.record({{"甲"}, {}}, "ㄋㄧˇ-ㄗㄞˋ", "妳在");
  prefs.record({{"乙"}, {}}, "ㄋㄧˇ-ㄗㄞˋ", "你在");
  ASSERT_EQ(prefs.size(), 2u);
  EXPECT_EQ(Value(prefs, {{"甲"}, {}}, "ㄋㄧˇ-ㄗㄞˋ"), "妳在");
  EXPECT_EQ(Value(prefs, {{"丙"}, {}}, "ㄋㄧˇ-ㄗㄞˋ"), "你在");
}

TEST(UserPreferencesTest, RoundTripsThroughTheFile) {
  UserPreferences prefs;
  prefs.record({{"^", "我"}, {"ㄔ", "ㄧ"}}, "ㄗㄞˋ", "再");
  prefs.record({{"鏽", "鋼"}, {}}, "ㄅㄟ", "杯");
  prefs.record({{"^"}, {"ㄏㄠˇ"}}, "ㄋㄧˇ", "妳");
  const std::string text = prefs.serialize();
  EXPECT_NE(text.find("再 ㄗㄞˋ ^我 ㄔ-ㄧ "), std::string::npos);
  EXPECT_NE(text.find("杯 ㄅㄟ 鏽鋼 * "), std::string::npos);

  UserPreferences reloaded;
  reloaded.loadFromText(text);
  EXPECT_EQ(reloaded.size(), prefs.size());
  EXPECT_EQ(Value(reloaded, {{"^", "我"}, {"ㄔ", "ㄧ"}}, "ㄗㄞˋ"), "再");
  EXPECT_EQ(Value(reloaded, {{"鏽", "鋼"}, {}}, "ㄅㄟ"), "杯");
  EXPECT_EQ(Value(reloaded, {{"^"}, {"ㄏㄠˇ"}}, "ㄋㄧˇ"), "妳");
  EXPECT_FALSE(reloaded.dirty());
  // Reloading must not reuse serials, or the next pick would not be the
  // newest.
  reloaded.record({{"鏽", "鋼"}, {}}, "ㄅㄟ", "盃");
  EXPECT_EQ(Value(reloaded, {{"鏽", "鋼"}, {}}, "ㄅㄟ"), "盃");
}

TEST(UserPreferencesTest, ReadsThePreviousFormat) {
  UserPreferences prefs;
  prefs.loadFromText(
      "# comment\n"
      "\n"
      "再 ㄗㄞˋ ^ 8 770\n"
      "在 ㄗㄞˋ ^ 8 720\n"  // the same key, weaker: it never showed
      "杯 ㄅㄟ 鋼 2 7\n"
      "知道 ㄓ-ㄉㄠˋ 5 1754332800\n"  // a pre-2026-08-09 four-field line
      "壞 ㄅㄟ 鋼 x 8\n");
  EXPECT_EQ(Value(prefs, {{"^"}, {}}, "ㄗㄞˋ"), "再");
  EXPECT_EQ(Value(prefs, {{"鋼"}, {}}, "ㄅㄟ"), "杯");
  EXPECT_EQ(prefs.size(), 2u);
  // Written back in the new format.
  EXPECT_NE(prefs.serialize().find("再 ㄗㄞˋ ^ * 770"), std::string::npos);
  // Serials carry on past the old ones.
  prefs.record({{"^"}, {}}, "ㄗㄞˋ", "在");
  EXPECT_EQ(Value(prefs, {{"^"}, {}}, "ㄗㄞˋ"), "在");
}

TEST(UserPreferencesTest, MergeLetsTheNewerPickWin) {
  // Every application hosts its own instance, so saving folds the file in.
  UserPreferences file;
  file.record({{"鋼"}, {}}, "ㄅㄟ", "悲");
  file.record({{"茶"}, {}}, "ㄅㄟ", "杯");

  UserPreferences mine;
  mine.loadFromText(file.serialize());
  mine.record({{"鋼"}, {}}, "ㄅㄟ", "杯");

  // The copy on disk still has the pick this process just overruled; the
  // merge must not bring it back.
  mine.mergeFrom(file);
  EXPECT_EQ(Value(mine, {{"鋼"}, {}}, "ㄅㄟ"), "杯");
  EXPECT_EQ(Value(mine, {{"茶"}, {}}, "ㄅㄟ"), "杯");
  EXPECT_EQ(mine.size(), 2u);
}

TEST(UserPreferencesTest, IgnoresEmptyFields) {
  UserPreferences prefs;
  prefs.record({{"鋼"}, {}}, "", "杯");
  prefs.record({{"鋼"}, {}}, "ㄅㄟ", "");
  EXPECT_EQ(prefs.size(), 0u);
  EXPECT_FALSE(prefs.dirty());
}

}  // namespace
}  // namespace mspy
