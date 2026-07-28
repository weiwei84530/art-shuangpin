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
    // Extra homophones so the ㄨㄛ menu spans two pages (page size 6).
    inner->add("ㄨㄛ", "倭", -5.5);
    inner->add("ㄨㄛ", "渦", -5.6);
    inner->add("ㄨㄛ", "窩", -5.7);
    inner->add("ㄨㄛ", "蝸", -5.8);
    inner->add("ㄨㄛ", "萵", -5.9);
    inner->add("ㄨㄛ", "撾", -6.0);
    inner->add("ㄨㄛ", "喔", -6.1);
    inner->add("ㄨㄛ", "偓", -6.2);
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
    inner->add("ㄏㄠˇ", "郝", -7);
    inner->add("ㄋㄧˇ-ㄏㄠˇ", "你好", -1);
    inner->add("ㄋㄧˇ-ㄏㄠˇ", "妳好", -4);
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
  // A second tone digit is no longer a tone: eaten, nothing leaks.
  auto r = composer_->feedChar('3');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "種");
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

TEST_F(ComposerTest, DigitsDisabledWhenIdle) {
  // Literal digits require English mode; in Chinese mode idle digits are
  // eaten silently so nothing leaks into the document.
  auto r = composer_->feedChar('2');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
  r = composer_->feedChar('0');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

TEST_F(ComposerTest, SpacePassesThroughWhenIdleCommitsWhileComposing) {
  auto idle = composer_->feedChar(' ');
  EXPECT_FALSE(idle.consumed);

  // Space acts like Enter: commits the buffer (no extra space character).
  Type("vs");
  auto r = composer_->feedChar(' ');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "中");
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

TEST_F(ComposerTest, CursorMovementAndMidBufferEditing) {
  Type("ni3hk3");  // 你好
  EXPECT_EQ(composer_->composedText(), "你好");

  // 9 moves left: cursor between 你 and 好; 8 opens the menu there. The
  // currently displayed picks (你好/好) are hidden as no-ops; the other
  // ㄏㄠˇ readings remain.
  Type("98");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  bool foundAlternative = false;
  for (const auto& c : composer_->candidates()) {
    EXPECT_NE(c.value, "好");
    EXPECT_NE(c.value, "你好");
    if (c.value == "郝") foundAlternative = true;
  }
  EXPECT_TRUE(foundAlternative);
  composer_->closeCandidateMenu();
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);

  // Move to the very front and insert 我 there.
  Type("9");
  Type("wo3");
  EXPECT_EQ(composer_->composedText(), "我你好");

  // Segments: caret after 我, tone settled everywhere; 你 (right of the
  // cursor) is the highlighted selection anchor.
  auto segments = composer_->displaySegments();
  EXPECT_EQ(segments.before, "我");
  EXPECT_EQ(segments.unconfirmed, "");
  EXPECT_EQ(segments.highlighted, "你");
  EXPECT_EQ(segments.after, "好");

  // Enter commits the whole buffer regardless of cursor position.
  auto r = composer_->feedEnter();
  EXPECT_EQ(r.commitText, "我你好");
}

TEST_F(ComposerTest, CursorWrapsAtBothEnds) {
  Type("ni3hk3");  // 你好, cursor at the right end (2)
  auto segments = composer_->displaySegments();
  EXPECT_EQ(segments.highlighted, "");  // nothing right of the cursor

  Type("0");  // right at the right end: wrap to the far left
  segments = composer_->displaySegments();
  EXPECT_EQ(segments.before, "");
  EXPECT_EQ(segments.highlighted, "你");

  Type("9");  // left at the far left: wrap to the right end
  segments = composer_->displaySegments();
  EXPECT_EQ(segments.before, "你好");
  EXPECT_EQ(segments.highlighted, "");

  Type("9");  // normal step left
  segments = composer_->displaySegments();
  EXPECT_EQ(segments.before, "你");
  EXPECT_EQ(segments.highlighted, "好");
}

TEST_F(ComposerTest, MinusJumpsCursorToStart) {
  Type("ni3hk3");  // 你好, cursor at the right end
  Type("-");
  auto segments = composer_->displaySegments();
  EXPECT_EQ(segments.before, "");
  EXPECT_EQ(segments.highlighted, "你");
  EXPECT_EQ(segments.after, "好");
}

TEST_F(ComposerTest, EqualsJumpsCursorToEnd) {
  Type("ni3hk3-");  // cursor jumped to the front
  Type("=");
  auto segments = composer_->displaySegments();
  EXPECT_EQ(segments.before, "你好");
  EXPECT_EQ(segments.highlighted, "");
  EXPECT_EQ(segments.after, "");
}

TEST_F(ComposerTest, MinusWhileSelectingDismissesMenuAndJumps) {
  Type("ni3hk39");  // cursor between 你 and 好
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  // Like 9/0, '-' closes the menu and then performs its normal function.
  Type("-");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  auto segments = composer_->displaySegments();
  EXPECT_EQ(segments.before, "");
  EXPECT_EQ(segments.highlighted, "你");
}

TEST_F(ComposerTest, MinusEqualsPassThroughWhenIdle) {
  // Idle '-'/'=' belong to the shell (Home/End navigation keys); the
  // composer must not consume them.
  auto r = composer_->feedChar('-');
  EXPECT_FALSE(r.consumed);
  r = composer_->feedChar('=');
  EXPECT_FALSE(r.consumed);
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

TEST_F(ComposerTest, FullPunctuationTable) {
  EXPECT_EQ(composer_->feedChar('[').commitText, "「");
  EXPECT_EQ(composer_->feedChar(']').commitText, "」");
  EXPECT_EQ(composer_->feedChar('{').commitText, "『");
  EXPECT_EQ(composer_->feedChar('}').commitText, "』");
  EXPECT_EQ(composer_->feedChar('\\').commitText, "、");
  EXPECT_EQ(composer_->feedChar('?').commitText, "？");
  EXPECT_EQ(composer_->feedChar('!').commitText, "！");
  EXPECT_EQ(composer_->feedChar('<').commitText, "《");
  EXPECT_EQ(composer_->feedChar('^').commitText, "……");

  // Quotes alternate between opening and closing forms.
  EXPECT_EQ(composer_->feedChar('"').commitText, "“");
  EXPECT_EQ(composer_->feedChar('"').commitText, "”");
  EXPECT_EQ(composer_->feedChar('\'').commitText, "‘");
  EXPECT_EQ(composer_->feedChar('\'').commitText, "’");

  // ';' alone is punctuation, but still forms -ing inside a syllable.
  EXPECT_EQ(composer_->feedChar(';').commitText, "；");
  Type("x;");
  EXPECT_EQ(composer_->composedText(), "星");
}

TEST_F(ComposerTest, UnconfirmedTailTracksToneSettlement) {
  Type("wo");  // eager bare insert: still retrofittable -> unconfirmed
  EXPECT_EQ(composer_->composedText(), "窝");
  EXPECT_EQ(composer_->unconfirmedTail(), "窝");

  Type("3");  // tone settled -> everything confirmed
  EXPECT_EQ(composer_->composedText(), "我");
  EXPECT_EQ(composer_->unconfirmedTail(), "");
  composer_->feedEsc();

  // Starting the next syllable confirms the previous bare one (the 窩愛你
  // scenario): typing the next first key turns 窩 black.
  Type("wo");
  Type("o");
  EXPECT_EQ(composer_->composedText(), "窝ㄛ");
  EXPECT_EQ(composer_->unconfirmedTail(), "ㄛ");
  composer_->feedEsc();

  // A half syllable alone is entirely unconfirmed.
  Type("u");
  EXPECT_EQ(composer_->unconfirmedTail(), "ㄕ");
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
  auto r = composer_->feedChar('8');
  EXPECT_TRUE(r.consumed);
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  // The displayed 的 is hidden as a no-op; 得 remains selectable.
  ASSERT_GE(composer_->candidates().size(), 1u);
  EXPECT_EQ(composer_->candidates()[0].value, "得");

  // Digit selects during selection instead of acting as a tone.
  composer_->feedChar('1');
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->composedText(), "得");
}

TEST_F(ComposerTest, MenuHidesNoOpCandidatesAndSkipsEmptyMenus) {
  // A span whose only candidate is what is already displayed opens no
  // menu at all.
  Type("x;");  // 星 is the only ㄒㄧㄥ entry
  EXPECT_EQ(composer_->composedText(), "星");
  composer_->feedChar('8');
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);

  composer_->feedEsc();

  // Same for a settled bopomofo literal (single fixed candidate).
  Type("n`");
  composer_->feedChar('8');
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
}

TEST_F(ComposerTest, SelectDigitWithoutCandidateIsNoOp) {
  Type("de8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  const size_t count = composer_->candidates().size();
  ASSERT_LT(count, 5u);
  composer_->feedChar('5');  // no 5th candidate on this page
  EXPECT_EQ(composer_->state(), Composer::State::kSelecting);
}

TEST_F(ComposerTest, MenuPagingNoWrap) {
  Type("wo8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  ASSERT_GE(composer_->candidates().size(), 8u);
  EXPECT_EQ(composer_->candidatePageCount(), 2u);
  EXPECT_EQ(composer_->candidatePageIndex(), 0u);
  EXPECT_EQ(composer_->currentPageCandidates().size(), 6u);

  composer_->feedChar('7');  // previous page on the first page: no-op
  EXPECT_EQ(composer_->candidatePageIndex(), 0u);

  composer_->feedChar('8');  // next page
  EXPECT_EQ(composer_->candidatePageIndex(), 1u);
  EXPECT_LE(composer_->currentPageCandidates().size(), 6u);

  composer_->feedChar('8');  // next page on the last page: no-op
  EXPECT_EQ(composer_->candidatePageIndex(), 1u);

  composer_->feedChar('7');  // back to the first page
  EXPECT_EQ(composer_->candidatePageIndex(), 0u);

  // Selection digit is page-relative: 1 on page 2 = 7th candidate.
  composer_->feedChar('8');
  const std::string seventh = composer_->candidates()[6].value;
  composer_->feedChar('1');
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->composedText(), seventh);
}

TEST_F(ComposerTest, AnyOtherKeyClosesMenuAndActs) {
  // 9/0 close the menu and move the cursor.
  Type("ni3hk3");
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  Type("9");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->displaySegments().highlighted, "好");

  // A letter closes the menu and starts a new syllable at the cursor.
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  Type("wo3");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->composedText(), "你我好");

  // Backspace closes the menu and deletes as usual.
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  composer_->feedBackspace();
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->composedText(), "你好");

  // Enter closes the menu and commits.
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  auto r = composer_->feedEnter();
  EXPECT_EQ(r.commitText, "你好");
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

TEST_F(ComposerTest, MenuOpensAtLastCharWhenCursorAtEnd) {
  Type("ni3hk3");  // cursor at the right end
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  bool foundAlternative = false;
  for (const auto& c : composer_->candidates()) {
    EXPECT_NE(c.value, "好");    // displayed: hidden as a no-op
    EXPECT_NE(c.value, "你好");  // ditto
    if (c.value == "郝" || c.value == "妳好") foundAlternative = true;
  }
  EXPECT_TRUE(foundAlternative);
}

TEST_F(ComposerTest, Digits67EatenWhileComposing) {
  Type("vs");
  auto r = composer_->feedChar('6');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  r = composer_->feedChar('7');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "中");
}

TEST_F(ComposerTest, SpaceCommitsVisibleBopomofoResidue) {
  // A lone first key + Space outputs its bopomofo symbol.
  Type("n");
  EXPECT_EQ(composer_->composedText(), "ㄋ");
  auto r = composer_->feedChar(' ');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "ㄋ");
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);

  // A tone-awaiting syllable (no bare entry) commits its bopomofo too.
  Type("ul");
  EXPECT_EQ(composer_->feedChar(' ').commitText, "ㄕㄞ");

  // Converted text plus residue commit together.
  Type("vs3n");
  EXPECT_EQ(composer_->feedChar(' ').commitText, "種ㄋ");

  // Enter still drops the residue (converted output only).
  Type("vs3n");
  EXPECT_EQ(composer_->feedEnter().commitText, "種");
}

TEST_F(ComposerTest, BacktickSettlesPendingBopomofo) {
  // n` settles ㄋ as fixed (black) text, still uncommitted.
  Type("n`");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  auto segments = composer_->displaySegments();
  EXPECT_EQ(segments.before, "ㄋ");
  EXPECT_EQ(segments.unconfirmed, "");

  // Chinese continues around the settled symbol; Enter commits both.
  Type("vs3");
  EXPECT_EQ(composer_->composedText(), "ㄋ種");
  EXPECT_EQ(composer_->feedEnter().commitText, "ㄋ種");

  // A tone-awaiting syllable settles its whole display.
  Type("ul`");
  EXPECT_EQ(composer_->displaySegments().before, "ㄕㄞ");
  composer_->feedEsc();
}

TEST_F(ComposerTest, HollowFinalSettlesDirectly) {
  // A bare ` hollows the initial slot: the next key is read as a final
  // and its bopomofo settles immediately: `k -> ㄠ.
  Type("`k");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->displaySegments().before, "ㄠ");
  EXPECT_EQ(composer_->feedEnter().commitText, "ㄠ");

  // ㄋㄧㄠ in one composition: n` y` `k, committed together.
  Type("n`y``k");
  EXPECT_EQ(composer_->composedText(), "ㄋㄧㄠ");
  EXPECT_EQ(composer_->feedEnter().commitText, "ㄋㄧㄠ");

  // Compound and single finals map per key: `x -> ㄧㄝ, `f -> ㄣ.
  Type("`x`f");
  EXPECT_EQ(composer_->composedText(), "ㄧㄝㄣ");
  composer_->feedEsc();

  // Mid-composition settle joins the buffer.
  Type("vs3`k");
  EXPECT_EQ(composer_->composedText(), "種ㄠ");
  EXPECT_EQ(composer_->feedEnter().commitText, "種ㄠ");
}

TEST_F(ComposerTest, HollowFinalBackspaceAndEsc) {
  // A settled symbol deletes like any other character.
  Type("`k");
  composer_->feedBackspace();
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);

  // Backspace between ` and the final key just cancels the hollow state.
  Type("`");
  composer_->feedBackspace();
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);

  // Esc cancels outright; the hollow sub-state is gone afterwards (an
  // idle digit is eaten with no output, not routed as a final).
  Type("`k");
  composer_->feedEsc();
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
  auto r = composer_->feedChar('7');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

TEST_F(ComposerTest, EscCancelsSelectionAndComposition) {
  Type("de8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  auto r = composer_->feedEsc();
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}


}  // namespace
}  // namespace mspy
