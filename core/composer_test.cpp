#include "composer.h"

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "testing/fake_lm.h"

namespace mspy {
namespace {

class ComposerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto inner = std::make_shared<testing::FakeLM>();
    inner->add("ㄨㄛ", "窝", -5);
    inner->add("ㄨㄛˇ", "我", -3);
    inner->add("ㄓㄨㄥ", "中", -3);
    inner->add("ㄓㄨㄥˇ", "種", -4);
    inner->add("ㄕㄞˋ", "曬", -4);
    inner->add("ㄢˇ", "俺", -6);
    inner->add("ㄒㄧㄥ", "星", -4);
    inner->add("ㄉㄜ", "得", -3);
    inner->add("ㄉㄜ˙", "的", -1);
    inner->add("ㄉㄜˊ", "德", -4);
    inner->add("ㄋㄧ", "妮", -6);
    inner->add("ㄋㄧˇ", "你", -2);
    inner->add("ㄏㄠˇ", "好", -2);
    inner->add("ㄋㄧˇ-ㄏㄠˇ", "你好", -1);
    lm_ = std::make_shared<RelaxedToneLM>(inner);
    composer_ = std::make_unique<Composer>(lm_);
  }

  // Feeds a string of printable keys.
  void Type(const std::string& keys) {
    for (char c : keys) composer_->feedChar(c);
  }

  std::shared_ptr<RelaxedToneLM> lm_;
  std::unique_ptr<Composer> composer_;
};

TEST_F(ComposerTest, EagerConversionOnSyllableCompletion) {
  Type("vs");  // zhong, no tone yet
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->composedText(), "中");
}

TEST_F(ComposerTest, ToneRetrofitsJustCompletedSyllable) {
  Type("vs3");
  EXPECT_EQ(composer_->composedText(), "種");
  // A second tone digit is no longer a tone: it commits buffer + literal.
  auto r = composer_->feedChar('3');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "種3");
}

TEST_F(ComposerTest, SpecSyllables) {
  Type("ul4");
  EXPECT_EQ(composer_->composedText(), "曬");
  composer_->feedEsc();
  Type("oj3");
  EXPECT_EQ(composer_->composedText(), "俺");
  composer_->feedEsc();
  Type("x;");
  EXPECT_EQ(composer_->composedText(), "星");
}

TEST_F(ComposerTest, StrictToneSemantics) {
  Type("de");
  EXPECT_EQ(composer_->composedText(), "的");  // neutral outranks tone 1
  composer_->feedEsc();

  Type("de1");
  EXPECT_EQ(composer_->composedText(), "得");  // explicit 1: strictly tone 1
  composer_->feedEsc();

  Type("de2");
  EXPECT_EQ(composer_->composedText(), "德");
  composer_->feedEsc();

  Type("de5");
  EXPECT_EQ(composer_->composedText(), "的");
  composer_->feedEsc();

  Type("wo");
  EXPECT_EQ(composer_->composedText(), "窝");  // 我 requires wo3
  composer_->feedEsc();
  Type("wo3");
  EXPECT_EQ(composer_->composedText(), "我");
}

TEST_F(ComposerTest, PhraseConversion) {
  Type("ni3hk3");
  EXPECT_EQ(composer_->composedText(), "你好");
}

TEST_F(ComposerTest, HalfSyllableDisplaysInitial) {
  Type("u");
  EXPECT_EQ(composer_->composedText(), "ㄕ");
  // shai has no tone-1/neutral entry in this fake dictionary, so the
  // completed syllable stays pending as bopomofo awaiting its tone digit.
  Type("l");
  EXPECT_EQ(composer_->composedText(), "ㄕㄞ");
  Type("4");
  EXPECT_EQ(composer_->composedText(), "曬");
}

TEST_F(ComposerTest, DigitsPassThroughWhenIdle) {
  auto r = composer_->feedChar('2');
  EXPECT_FALSE(r.consumed);
  r = composer_->feedChar('0');
  EXPECT_FALSE(r.consumed);
}

TEST_F(ComposerTest, SpacePassesThroughWhenIdleCommitsWhileComposing) {
  auto idle = composer_->feedChar(' ');
  EXPECT_FALSE(idle.consumed);

  Type("vs");
  auto r = composer_->feedChar(' ');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "中 ");
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

TEST_F(ComposerTest, DirectPunctuation) {
  auto r = composer_->feedChar(',');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "，");

  Type("vs");
  r = composer_->feedChar('.');
  EXPECT_EQ(r.commitText, "中。");
}

TEST_F(ComposerTest, EnterCommits) {
  Type("vs3");
  auto r = composer_->feedEnter();
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "種");
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

TEST_F(ComposerTest, BackspaceDeletesSyllableThenEmpties) {
  Type("vs");
  composer_->feedBackspace();
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
  EXPECT_EQ(composer_->composedText(), "");

  Type("v");
  composer_->feedBackspace();
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);

  auto r = composer_->feedBackspace();
  EXPECT_FALSE(r.consumed);  // nothing to delete: let the app handle it
}

TEST_F(ComposerTest, SelectionFlow) {
  Type("de");
  auto r = composer_->feedDown();
  EXPECT_TRUE(r.consumed);
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  ASSERT_GE(composer_->candidates().size(), 2u);
  EXPECT_EQ(composer_->candidates()[0].value, "的");
  EXPECT_EQ(composer_->candidates()[1].value, "得");

  // Digit selects during selection instead of acting as a tone.
  composer_->feedChar('2');
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->composedText(), "得");
}

TEST_F(ComposerTest, EscClosesSelectionThenClearsBuffer) {
  Type("de");
  composer_->feedDown();
  composer_->feedEsc();
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  composer_->feedEsc();
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

}  // namespace
}  // namespace mspy
