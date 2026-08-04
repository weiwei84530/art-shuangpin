#include "user_preference_lm.h"

#include <algorithm>
#include <memory>

#include "gtest/gtest.h"
#include "relaxed_tone_lm.h"
#include "testing/fake_lm.h"

namespace mspy {
namespace {

constexpr int64_t kNow = 1'800'000'000;
constexpr int64_t kDay = 24 * 60 * 60;

class UserPreferenceLMTest : public ::testing::Test {
 protected:
  void SetUp() override {
    inner_ = std::make_shared<testing::FakeLM>();
    // 知道 is the better phrase; 之道 is the one a contextual pick used to
    // pin forever under the old score-0 store.
    inner_->add("ㄓ-ㄉㄠˋ", "知道", -4.0);
    inner_->add("ㄓ-ㄉㄠˋ", "之道", -7.0);
    inner_->add("ㄓ", "之", -5.0);
    inner_->add("ㄗㄞˋ", "在", -4.0);
    inner_->add("ㄗㄞˋ", "再", -4.5);
    inner_->add("ㄨㄛˇ", "我", -3.0);
    inner_->add("ㄍㄜ", "個", -3.0);
    inner_->add("ㄍㄜ", "歌", -6.0);

    prefs_ = std::make_shared<UserPreferences>();
    lm_ = std::make_shared<UserPreferenceLM>(inner_, prefs_);
    lm_->clock = [this] { return now_; };
  }

  // Highest-scoring value the model returns for a key.
  std::string Best(const std::string& key) {
    auto unigrams = lm_->getUnigrams(key);
    if (unigrams.empty()) return "";
    auto best = std::max_element(unigrams.begin(), unigrams.end(),
                                 [](const auto& a, const auto& b) {
                                   return a.score() < b.score();
                                 });
    return best->value();
  }

  int64_t now_ = kNow;
  std::shared_ptr<testing::FakeLM> inner_;
  std::shared_ptr<UserPreferences> prefs_;
  std::shared_ptr<UserPreferenceLM> lm_;
};

TEST_F(UserPreferenceLMTest, NoPreferencesLeavesTheDictionaryAlone) {
  EXPECT_EQ(Best("ㄓ-ㄉㄠˋ"), "知道");
  EXPECT_EQ(lm_->getUnigrams("ㄓ-ㄉㄠˋ").size(), 2u);
}

TEST_F(UserPreferenceLMTest, ALivePreferenceWinsItsKey) {
  prefs_->record("ㄓ-ㄉㄠˋ", "之道", now_);
  EXPECT_EQ(Best("ㄓ-ㄉㄠˋ"), "之道");
}

TEST_F(UserPreferenceLMTest, AOneOffPickFadesBackToTheDictionary) {
  // The 經營之道 problem: one contextual pick must not own the reading.
  prefs_->record("ㄓ-ㄉㄠˋ", "之道", now_);
  ASSERT_EQ(Best("ㄓ-ㄉㄠˋ"), "之道");

  now_ = kNow + 20 * kDay;  // past one half-life for a single pick
  EXPECT_EQ(Best("ㄓ-ㄉㄠˋ"), "知道");
}

TEST_F(UserPreferenceLMTest, ACorrectionTakesEffect) {
  // 之道 first, 知道 later -- the case the old store could never fix.
  prefs_->record("ㄓ-ㄉㄠˋ", "之道", now_);
  prefs_->record("ㄓ-ㄉㄠˋ", "知道", now_ + kDay);
  now_ += kDay;
  EXPECT_EQ(Best("ㄓ-ㄉㄠˋ"), "知道");
}

TEST_F(UserPreferenceLMTest, PreferenceBeatsTheDictionaryOnlyByAHair) {
  // The margin must stay tiny so span-vs-span competition is undistorted.
  prefs_->record("ㄓ-ㄉㄠˋ", "之道", now_);
  auto unigrams = lm_->getUnigrams("ㄓ-ㄉㄠˋ");
  double preferred = 0.0;
  double best = -1e9;
  for (const auto& u : unigrams) {
    if (u.value() == "之道") preferred = u.score();
    if (u.value() == "知道") best = u.score();
  }
  EXPECT_GT(preferred, best);
  EXPECT_LT(preferred - best, 1e-3);
}

TEST_F(UserPreferenceLMTest, LearnsAPhraseTheDictionaryDoesNotHave) {
  // Context phrases from single-character picks are usually not in the
  // dictionary; they must still be reachable and must beat spelling the
  // same syllables out one character at a time.
  const std::string key = "ㄨㄛˇ-ㄗㄞˋ";
  ASSERT_FALSE(inner_->hasUnigrams(key));

  prefs_->record(key, "我在", now_);
  EXPECT_TRUE(lm_->hasUnigrams(key));
  ASSERT_EQ(Best(key), "我在");

  // -3 (我) + -4 (在) is what the character-by-character path costs.
  auto unigrams = lm_->getUnigrams(key);
  ASSERT_EQ(unigrams.size(), 1u);
  EXPECT_GT(unigrams[0].score(), -7.0);
  EXPECT_LT(unigrams[0].score(), -7.0 + 1e-3);
}

TEST_F(UserPreferenceLMTest, AnUntypeableCoinedPhraseIsIgnored) {
  prefs_->record("ㄓ-ㄅㄨˋㄅㄨˋ", "亂碼", now_);
  EXPECT_FALSE(lm_->hasUnigrams("ㄓ-ㄅㄨˋㄅㄨˋ"));
  EXPECT_TRUE(lm_->getUnigrams("ㄓ-ㄅㄨˋㄅㄨˋ").empty());
}

TEST_F(UserPreferenceLMTest, ExplicitToneOneMatchesThePlainStoredKey) {
  // ㄍㄜˉ carries the internal sentinel; the store holds plain ㄍㄜ.
  // Resolving the sentinel is RelaxedToneLM's job, so this needs the real
  // chain rather than the bare dictionary.
  auto relaxed = std::make_shared<RelaxedToneLM>(inner_);
  auto stacked = std::make_shared<UserPreferenceLM>(relaxed, prefs_);
  stacked->clock = [this] { return now_; };

  prefs_->record("ㄍㄜ", "歌", now_);
  auto unigrams = stacked->getUnigrams(std::string("ㄍㄜ") + kToneSentinel1);
  ASSERT_FALSE(unigrams.empty());
  EXPECT_EQ(unigrams.front().value(), "歌");
  EXPECT_EQ(UserPreferenceLM::NormalizeKey(std::string("ㄍㄜ") + kToneSentinel1),
            "ㄍㄜ");
}

TEST_F(UserPreferenceLMTest, StacksOnTopOfTheToneRelaxedModel) {
  // The real chain: preferences over tone relaxation over the dictionary.
  auto relaxed = std::make_shared<RelaxedToneLM>(inner_);
  auto stacked = std::make_shared<UserPreferenceLM>(relaxed, prefs_);
  stacked->clock = [this] { return now_; };

  prefs_->record("ㄗㄞˋ", "再", now_);
  auto unigrams = stacked->getUnigrams("ㄗㄞˋ");
  ASSERT_FALSE(unigrams.empty());
  EXPECT_EQ(unigrams.front().value(), "再");
}

}  // namespace
}  // namespace mspy
