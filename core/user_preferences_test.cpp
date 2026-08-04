#include "user_preferences.h"

#include <string>

#include "gtest/gtest.h"

namespace mspy {
namespace {

constexpr int64_t kNow = 1'800'000'000;
constexpr int64_t kDay = 24 * 60 * 60;

TEST(UserPreferencesTest, RoundTripsTheFourFieldFormat) {
  UserPreferences prefs;
  prefs.loadFromText("知道 ㄓ-ㄉㄠˋ 7 1754332800\n");
  ASSERT_EQ(prefs.size(), 1u);
  EXPECT_EQ(prefs.serialize(), "知道 ㄓ-ㄉㄠˋ 7 1754332800\n");
  // Nothing changed on load, so there is nothing to write back.
  EXPECT_FALSE(prefs.dirty());
}

TEST(UserPreferencesTest, ReadsTheOldTwoFieldFormatAsOneDatedPick) {
  UserPreferences prefs;
  prefs.loadFromText("知道 ㄓ-ㄉㄠˋ\n", kNow);
  // Dated from the migration, so vocabulary already learned survives...
  const auto live = prefs.lookup("ㄓ-ㄉㄠˋ", kNow);
  ASSERT_EQ(live.size(), 1u);
  EXPECT_EQ(live[0].count, 1.0);
  // ...and then ages out like any other single pick if it goes unused.
  EXPECT_TRUE(prefs.lookup("ㄓ-ㄉㄠˋ", kNow + 20 * kDay).empty());
  // The file wants rewriting into the new format.
  EXPECT_TRUE(prefs.dirty());
}

TEST(UserPreferencesTest, SkipsBlankCommentAndCorruptLines) {
  UserPreferences prefs;
  prefs.loadFromText(
      "\n# a comment\n知道 ㄓ-ㄉㄠˋ 2 1800000000\nrubbish\n"
      "壞 ㄏㄨㄞˋ x y\n三 ㄙㄢ 0 1800000000\n");
  ASSERT_EQ(prefs.size(), 1u);
  EXPECT_EQ(prefs.lookup("ㄓ-ㄉㄠˋ", kNow).size(), 1u);
}

TEST(UserPreferencesTest, WeightHalvesEveryHalfLifeThenDropsOut) {
  UserPreferences::Entry entry{"知道", 1.0, kNow};
  EXPECT_DOUBLE_EQ(UserPreferences::WeightAt(entry, kNow), 1.0);

  // One half-life later a single pick is exactly at the cutoff...
  const int64_t oneHalfLife =
      kNow + static_cast<int64_t>(UserPreferences::kHalfLifeSeconds);
  EXPECT_DOUBLE_EQ(UserPreferences::WeightAt(entry, oneHalfLife), 0.5);
  // ...and just past it the entry stops overriding the dictionary.
  EXPECT_DOUBLE_EQ(UserPreferences::WeightAt(entry, oneHalfLife + kDay), 0.0);

  // A phrase picked repeatedly survives far longer.
  UserPreferences::Entry habit{"知道", 8.0, kNow};
  EXPECT_GT(UserPreferences::WeightAt(habit, kNow + 40 * kDay), 0.0);
}

TEST(UserPreferencesTest, ACorrectionOutranksTheOlderPickImmediately) {
  // The exact failure the old store could not express: 之道 was picked
  // first (and won forever); 知道 is picked later and must take over.
  UserPreferences prefs;
  prefs.record("ㄓ-ㄉㄠˋ", "之道", kNow);
  prefs.record("ㄓ-ㄉㄠˋ", "知道", kNow + kDay);

  const auto live = prefs.lookup("ㄓ-ㄉㄠˋ", kNow + kDay);
  ASSERT_EQ(live.size(), 2u);
  EXPECT_EQ(live[0].value, "知道");
}

TEST(UserPreferencesTest, RecordBumpsFromTheDecayedWeightNotTheOldStreak) {
  UserPreferences prefs;
  // First record creates the entry at 1; the other nine bump it.
  for (int i = 0; i < 10; ++i) prefs.record("ㄍㄜ", "歌", kNow);
  ASSERT_EQ(prefs.lookup("ㄍㄜ", kNow)[0].count, 10.0);

  // Re-picking it a year later resumes from ~1, not from 10.
  const int64_t muchLater = kNow + 365 * kDay;
  prefs.record("ㄍㄜ", "歌", muchLater);
  EXPECT_LE(prefs.lookup("ㄍㄜ", muchLater)[0].count, 2.0);
}

TEST(UserPreferencesTest, CountIsCapped) {
  UserPreferences prefs;
  for (int i = 0; i < 200; ++i) prefs.record("ㄍㄜ", "歌", kNow);
  EXPECT_LE(prefs.lookup("ㄍㄜ", kNow)[0].count, UserPreferences::kMaxCount);
}

TEST(UserPreferencesTest, TouchRefreshesButNeverCreates) {
  UserPreferences prefs;
  prefs.record("ㄓ-ㄉㄠˋ", "知道", kNow);
  prefs.clearDirty();

  // A phrase that keeps being used as-is stays alive past its half-life.
  const int64_t later = kNow + 13 * kDay;
  prefs.touch("ㄓ-ㄉㄠˋ", "知道", later);
  EXPECT_TRUE(prefs.dirty());
  EXPECT_FALSE(prefs.lookup("ㄓ-ㄉㄠˋ", later + 13 * kDay).empty());

  // Touching something unknown does nothing at all.
  prefs.clearDirty();
  prefs.touch("ㄅㄨˋ-ㄓ-ㄉㄠˋ", "不知道", later);
  EXPECT_FALSE(prefs.dirty());
  EXPECT_EQ(prefs.size(), 1u);
}

TEST(UserPreferencesTest, MergeKeepsTheStrongerRecordOfEachField) {
  // Every application hosts its own TIP instance, so a save has to fold in
  // whatever a sibling process wrote in the meantime.
  UserPreferences mine;
  mine.record("ㄓ-ㄉㄠˋ", "知道", kNow);

  UserPreferences theirs;
  theirs.loadFromText("知道 ㄓ-ㄉㄠˋ 9 " + std::to_string(kNow - kDay) +
                      "\n很好 ㄏㄣˇ-ㄏㄠˇ 2 " + std::to_string(kNow) + "\n");

  mine.mergeFrom(theirs);
  ASSERT_EQ(mine.size(), 2u);
  const auto merged = mine.lookup("ㄓ-ㄉㄠˋ", kNow);
  ASSERT_EQ(merged.size(), 1u);
  EXPECT_EQ(merged[0].count, 9.0);       // the higher count
  EXPECT_EQ(merged[0].lastUsed, kNow);   // and the newer timestamp
}

TEST(UserPreferencesTest, DropAmbiguousLegacyKeysClearsStuckOldEntries) {
  // Exactly the shape the old append-only file left behind: a stuck first
  // pick plus the correction that could never replace it, neither dated.
  UserPreferences prefs;
  prefs.loadFromText("妳的 ㄋㄧˇ-ㄉㄜ\n你的 ㄋㄧˇ-ㄉㄜ\n很好 ㄏㄣˇ-ㄏㄠˇ\n",
                     kNow);
  const auto dropped = prefs.dropAmbiguousLegacyKeys();
  ASSERT_EQ(dropped.size(), 1u);
  EXPECT_EQ(dropped[0], "ㄋㄧˇ-ㄉㄜ");
  EXPECT_TRUE(prefs.lookup("ㄋㄧˇ-ㄉㄜ", kNow).empty());
  // A single legacy entry is fine -- only the ambiguous ones go.
  EXPECT_EQ(prefs.lookup("ㄏㄣˇ-ㄏㄠˇ", kNow).size(), 1u);
}

TEST(UserPreferencesTest, DropAmbiguousLegacyKeysSparesRealPicks) {
  // Two dated values under one key is the NORMAL shape after a correction:
  // weight decides between them, so the cleanup must not touch it.
  UserPreferences prefs;
  prefs.record("ㄓ-ㄉㄠˋ", "之道", kNow);
  prefs.record("ㄓ-ㄉㄠˋ", "知道", kNow + kDay);
  EXPECT_TRUE(prefs.dropAmbiguousLegacyKeys().empty());
  EXPECT_EQ(prefs.lookup("ㄓ-ㄉㄠˋ", kNow + kDay)[0].value, "知道");

  // Nor a key that mixes a legacy line with a real pick.
  UserPreferences mixed;
  mixed.loadFromText("妳的 ㄋㄧˇ-ㄉㄜ\n", kNow);
  mixed.record("ㄋㄧˇ-ㄉㄜ", "你的", kNow + kDay);
  EXPECT_TRUE(mixed.dropAmbiguousLegacyKeys().empty());
  EXPECT_EQ(mixed.lookup("ㄋㄧˇ-ㄉㄜ", kNow + kDay)[0].value, "你的");
}

}  // namespace
}  // namespace mspy
