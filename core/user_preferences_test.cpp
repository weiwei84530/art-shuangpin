#include "user_preferences.h"

#include <string>

#include "gtest/gtest.h"

namespace mspy {
namespace {

TEST(UserPreferencesTest, OneCorrectionIsEnough) {
  UserPreferences prefs;
  // A habit the store already holds, however strong.
  for (int i = 0; i < 20; ++i) prefs.record("鋼", "ㄅㄟ", "悲");
  ASSERT_EQ(prefs.lookup("鋼", "ㄅㄟ"), "悲");

  // One correction flips it, with nothing to wait for.
  prefs.record("鋼", "ㄅㄟ", "杯");
  EXPECT_EQ(prefs.lookup("鋼", "ㄅㄟ"), "杯");

  // And flips back just as cheaply: the store follows the user.
  prefs.record("鋼", "ㄅㄟ", "悲");
  EXPECT_EQ(prefs.lookup("鋼", "ㄅㄟ"), "悲");
}

TEST(UserPreferencesTest, ContextsAreIndependent) {
  UserPreferences prefs;
  prefs.record("鋼", "ㄅㄟ", "杯");
  prefs.record("可", "ㄅㄟ", "悲");
  EXPECT_EQ(prefs.lookup("鋼", "ㄅㄟ"), "杯");
  EXPECT_EQ(prefs.lookup("可", "ㄅㄟ"), "悲");
  // A context nobody has taught anything about stays untouched.
  EXPECT_TRUE(prefs.lookup("茶", "ㄅㄟ").empty());
  EXPECT_FALSE(prefs.hasContext("茶"));
  EXPECT_TRUE(prefs.hasContext("鋼"));
}

TEST(UserPreferencesTest, RivalsFadeOneCorrectionAtATime) {
  UserPreferences prefs;
  prefs.record("鋼", "ㄅㄟ", "悲");
  prefs.record("鋼", "ㄅㄟ", "悲");  // a habit worth two corrections

  prefs.record("鋼", "ㄅㄟ", "杯");
  // 杯 wins at once, but 悲 is still on file: the loser has to be able to
  // come back without starting from nothing.
  EXPECT_EQ(prefs.lookup("鋼", "ㄅㄟ"), "杯");
  EXPECT_EQ(prefs.size(), 2u);

  // Sticking with 杯 wears the rival down to nothing.
  prefs.record("鋼", "ㄅㄟ", "杯");
  EXPECT_EQ(prefs.size(), 1u);
  EXPECT_EQ(prefs.lookup("鋼", "ㄅㄟ"), "杯");
}

TEST(UserPreferencesTest, CountIsCapped) {
  UserPreferences prefs;
  for (int i = 0; i < 50; ++i) prefs.record("鋼", "ㄅㄟ", "杯");
  const auto records = prefs.all();
  ASSERT_EQ(records.size(), 1u);
  EXPECT_LE(records[0].count, UserPreferences::kMaxCount);
}

TEST(UserPreferencesTest, RoundTripsThroughTheFile) {
  UserPreferences prefs;
  prefs.record("鋼", "ㄅㄟ", "杯");
  prefs.record("鏽鋼", "ㄅㄟ", "杯");
  prefs.record(UserPreferences::kStartContext, "ㄧ", "一");
  prefs.record("鋼", "ㄍㄤ-ㄅㄟ", "鋼杯");
  const std::string text = prefs.serialize();

  UserPreferences reloaded;
  reloaded.loadFromText(text);
  EXPECT_EQ(reloaded.size(), prefs.size());
  EXPECT_EQ(reloaded.lookup("鋼", "ㄅㄟ"), "杯");
  EXPECT_EQ(reloaded.lookup("鏽鋼", "ㄅㄟ"), "杯");
  EXPECT_EQ(reloaded.lookup(UserPreferences::kStartContext, "ㄧ"), "一");
  EXPECT_EQ(reloaded.lookup("鋼", "ㄍㄤ-ㄅㄟ"), "鋼杯");
  EXPECT_FALSE(reloaded.dirty());
  // Reloading must not reuse serials, or a later merge cannot order them.
  reloaded.record("鋼", "ㄅㄟ", "盃");
  EXPECT_EQ(reloaded.lookup("鋼", "ㄅㄟ"), "盃");
}

TEST(UserPreferencesTest, SkipsJunkAndTheOldFormat) {
  UserPreferences prefs;
  prefs.loadFromText(
      "# comment\n"
      "\n"
      "杯 ㄅㄟ 鋼 2 7\n"
      "知道 ㄓ-ㄉㄠˋ 5 1754332800\n"  // a pre-2026-08-09 four-field line
      "壞 ㄅㄟ 鋼 x 8\n"
      "杯 ㄅㄟ 鋼 1 3\n");  // same triple, older serial: the newer wins
  EXPECT_EQ(prefs.size(), 1u);
  EXPECT_EQ(prefs.lookup("鋼", "ㄅㄟ"), "杯");
  const auto records = prefs.all();
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records[0].count, 2.0);
  EXPECT_EQ(records[0].serial, 7);
}

TEST(UserPreferencesTest, MergeKeepsTheStrongerRecord) {
  // Every application hosts its own instance, so saving folds the file in.
  UserPreferences mine;
  mine.record("鋼", "ㄅㄟ", "杯");

  UserPreferences theirs;
  theirs.record("鋼", "ㄅㄟ", "杯");
  theirs.record("鋼", "ㄅㄟ", "杯");
  theirs.record("茶", "ㄅㄟ", "杯");

  mine.mergeFrom(theirs);
  EXPECT_EQ(mine.lookup("鋼", "ㄅㄟ"), "杯");
  EXPECT_EQ(mine.lookup("茶", "ㄅㄟ"), "杯");
  const auto records = mine.all();
  for (const auto& record : records) {
    if (record.context == "鋼") EXPECT_EQ(record.count, 2.0);
  }
}

TEST(UserPreferencesTest, IgnoresEmptyFields) {
  UserPreferences prefs;
  prefs.record("", "ㄅㄟ", "杯");
  prefs.record("鋼", "", "杯");
  prefs.record("鋼", "ㄅㄟ", "");
  EXPECT_EQ(prefs.size(), 0u);
  EXPECT_FALSE(prefs.dirty());
}

}  // namespace
}  // namespace mspy
