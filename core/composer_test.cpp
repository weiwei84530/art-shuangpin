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

  // Space with a syllable in progress: settles it into its character
  // without committing. Most tests need this before they can see converted
  // text or use the control digits (2026-08-04: a tone digit alone leaves
  // the syllable unsettled, and digits are tone keys until it settles).
  void Settle() { composer_->feedChar(' '); }

  std::shared_ptr<RelaxedToneLM> lm_;
  std::unique_ptr<Composer> composer_;
};

TEST_F(ComposerTest, SecondKeyKeepsBopomofoUntilSettled) {
  Type("vs");  // zhong, no tone yet: stays bopomofo, no character flash
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->composedText(), "ㄓㄨㄥ");

  // Space settles it with the default tone-1/neutral reading, in place:
  // the buffer is NOT committed.
  auto r = composer_->feedChar(' ');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "中");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);

  // A tone digit no longer applies once settled.
  Type("3");
  EXPECT_EQ(composer_->composedText(), "中");
}

TEST_F(ComposerTest, ToneDigitKeepsBopomofoUntilSettled) {
  // 2026-08-04: a tone digit no longer settles the syllable either — the
  // bopomofo stays, now carrying its tone mark.
  Type("vs3");
  EXPECT_EQ(composer_->composedText(), "ㄓㄨㄥˇ");
  EXPECT_EQ(composer_->unconfirmedTail(), "ㄓㄨㄥˇ");

  // A second tone digit is eaten: tones are corrected with Backspace.
  auto r = composer_->feedChar('2');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "ㄓㄨㄥˇ");

  Settle();
  EXPECT_EQ(composer_->composedText(), "種");
}

TEST_F(ComposerTest, MirroredToneDigits) {
  // Tones are reachable with either hand: 1-5 on the left, and 0-6
  // mirrored around the 5/6 gap on the right (0=1, 9=2, 8=3, 7=4, 6=5).
  const struct {
    const char* keys;
    const char* display;
    const char* settled;
  } kCases[] = {
      {"de0", "ㄉㄜˉ", "得"},  // explicit tone 1
      {"de9", "ㄉㄜˊ", "德"},
      {"vs8", "ㄓㄨㄥˇ", "種"},
      {"ul7", "ㄕㄞˋ", "曬"},
      {"de6", "ㄉㄜ˙", "的"},
  };
  for (const auto& c : kCases) {
    composer_->feedEsc();
    Type(c.keys);
    EXPECT_EQ(composer_->composedText(), c.display) << c.keys;
    Settle();
    EXPECT_EQ(composer_->composedText(), c.settled) << c.keys;
  }
}

TEST_F(ComposerTest, ControlDigitsAreEatenWhileUnsettled) {
  // With a tone already given, every digit and '-'/'=' is eaten: the
  // control keys only come back once the syllable is settled.
  Type("ni3hk3");
  EXPECT_EQ(composer_->composedText(), "你ㄏㄠˇ");
  for (const char* key : {"8", "9", "0", "-", "="}) {
    auto r = composer_->feedChar(key[0]);
    EXPECT_TRUE(r.consumed) << key;
    EXPECT_EQ(r.commitText, "") << key;
    EXPECT_EQ(composer_->state(), Composer::State::kComposing) << key;
    EXPECT_EQ(composer_->composedText(), "你ㄏㄠˇ") << key;
  }

  // Settled: 8 opens the menu again.
  Settle();
  EXPECT_EQ(composer_->composedText(), "你好");
  Type("8");
  EXPECT_EQ(composer_->state(), Composer::State::kSelecting);
}

TEST_F(ComposerTest, BackspaceTakesBackOnlyTheTone) {
  // The syllable is in the grid untoned (ㄓㄨㄥ exists), so Backspace
  // returns it to the untoned unsettled state and another tone can be typed.
  Type("vs3");
  EXPECT_EQ(composer_->composedText(), "ㄓㄨㄥˇ");
  composer_->feedBackspace();
  EXPECT_EQ(composer_->composedText(), "ㄓㄨㄥ");
  Type("3");  // the tone window reopened
  EXPECT_EQ(composer_->composedText(), "ㄓㄨㄥˇ");
  Settle();
  EXPECT_EQ(composer_->composedText(), "種");
  composer_->feedEsc();

  // ㄕㄞ has no toneless entry, so it cannot sit in the grid untoned:
  // Backspace puts it back in the pending slot, still awaiting a tone.
  Type("ul4");
  EXPECT_EQ(composer_->composedText(), "ㄕㄞˋ");
  composer_->feedBackspace();
  EXPECT_EQ(composer_->composedText(), "ㄕㄞ");
  Type("4");
  Settle();
  EXPECT_EQ(composer_->composedText(), "曬");

  // One more Backspace past the untoned syllable deletes it outright.
  composer_->feedEsc();
  Type("vs3");
  composer_->feedBackspace();
  composer_->feedBackspace();
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

TEST_F(ComposerTest, SpecSyllables) {
  Type("ul4");
  Settle();
  EXPECT_EQ(composer_->composedText(), "曬");
  composer_->feedEsc();
  Type("oj3");
  Settle();
  EXPECT_EQ(composer_->composedText(), "俺");
  composer_->feedEsc();
  Type("x;");
  EXPECT_EQ(composer_->composedText(), "ㄒㄧㄥ");  // tone 1 = Space or 1
  Settle();
  EXPECT_EQ(composer_->composedText(), "星");
}

TEST_F(ComposerTest, StrictToneSemantics) {
  Type("de");
  Settle();  // settle with the toneless default
  EXPECT_EQ(composer_->composedText(), "的");  // neutral outranks tone 1
  composer_->feedEsc();

  Type("de1");
  Settle();
  EXPECT_EQ(composer_->composedText(), "得");  // explicit 1: strictly tone 1
  composer_->feedEsc();

  Type("de2");
  Settle();
  EXPECT_EQ(composer_->composedText(), "德");
  composer_->feedEsc();

  Type("de5");
  Settle();
  EXPECT_EQ(composer_->composedText(), "的");
  composer_->feedEsc();

  Type("wo");
  Settle();
  EXPECT_EQ(composer_->composedText(), "窝");  // 我 requires wo3
  composer_->feedEsc();
  Type("wo3");
  Settle();
  EXPECT_EQ(composer_->composedText(), "我");
}

TEST_F(ComposerTest, PhraseConversion) {
  Type("ni3hk3");
  Settle();
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
  EXPECT_EQ(composer_->composedText(), "ㄕㄞˋ");
  Settle();
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

TEST_F(ComposerTest, SpacePassesThroughWhenIdleAndCommitsOnceSettled) {
  auto idle = composer_->feedChar(' ');
  EXPECT_FALSE(idle.consumed);

  // First Space settles the syllable, second Space (nothing left to
  // settle) commits the buffer like Enter.
  Type("vs");
  auto r = composer_->feedChar(' ');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  r = composer_->feedChar(' ');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "中");
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);

  // A toned syllable is unsettled too, so it takes the same two Spaces.
  Type("vs3");
  r = composer_->feedChar(' ');
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "種");
  r = composer_->feedChar(' ');
  EXPECT_EQ(r.commitText, "種");
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

TEST_F(ComposerTest, CursorMovementAndMidBufferEditing) {
  Type("ni3hk3");  // 你好
  Settle();
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
  Settle();
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
  Settle();
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
  Settle();
  Type("-");
  auto segments = composer_->displaySegments();
  EXPECT_EQ(segments.before, "");
  EXPECT_EQ(segments.highlighted, "你");
  EXPECT_EQ(segments.after, "好");
}

TEST_F(ComposerTest, EqualsJumpsCursorToEnd) {
  Type("ni3hk3");
  Settle();
  Type("-");  // cursor jumped to the front
  Type("=");
  auto segments = composer_->displaySegments();
  EXPECT_EQ(segments.before, "你好");
  EXPECT_EQ(segments.highlighted, "");
  EXPECT_EQ(segments.after, "");
}

TEST_F(ComposerTest, MinusWhileSelectingDismissesMenuAndJumps) {
  Type("ni3hk3");
  Settle();
  Type("9");  // cursor between 你 and 好
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

TEST_F(ComposerTest, PunctuationSettlesAndJoinsTheComposition) {
  // 2026-08-04: punctuation no longer commits. Idle, it starts a fresh
  // composition holding just the symbol.
  auto r = composer_->feedChar(',');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->composedText(), "，");
  composer_->feedEsc();

  // With a syllable in progress it settles it — even one that has been
  // given a tone but not yet settled — and appends itself.
  Type("ul4");
  EXPECT_EQ(composer_->composedText(), "ㄕㄞˋ");
  r = composer_->feedChar('.');
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "曬。");

  // The sentence keeps composing across the punctuation, and it all
  // commits together on Enter.
  Type("vs3");
  Settle();
  EXPECT_EQ(composer_->composedText(), "曬。種");
  EXPECT_EQ(composer_->feedEnter().commitText, "曬。種");
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

TEST_F(ComposerTest, PunctuationIsSelectableAndDeletable) {
  Type("ni3hk3");
  composer_->feedChar(',');
  EXPECT_EQ(composer_->composedText(), "你好，");

  // The cursor walks over punctuation like any other character, and the
  // menu still opens on the characters around it.
  Type("9");
  EXPECT_EQ(composer_->displaySegments().highlighted, "，");
  Type("9");
  EXPECT_EQ(composer_->displaySegments().highlighted, "好");
  Type("8");
  EXPECT_EQ(composer_->state(), Composer::State::kSelecting);
  composer_->closeCandidateMenu();

  // Backspace deletes it one symbol at a time.
  Type("=");
  composer_->feedBackspace();
  EXPECT_EQ(composer_->composedText(), "你好");
}

TEST_F(ComposerTest, FullPunctuationTable) {
  const struct {
    char key;
    const char* symbol;
  } kTable[] = {
      {'[', "「"}, {']', "」"}, {'{', "『"}, {'}', "』"}, {'\\', "、"},
      {'?', "？"}, {'!', "！"}, {'<', "《"}, {'^', "……"},
      // Quotes alternate between opening and closing forms.
      {'"', "“"}, {'"', "”"}, {'\'', "‘"}, {'\'', "’"},
      // ';' alone is punctuation (it only forms -ing inside a syllable).
      {';', "；"},
  };
  std::string expected;
  for (const auto& entry : kTable) {
    EXPECT_EQ(composer_->feedChar(entry.key).commitText, "") << entry.symbol;
    expected += entry.symbol;
    EXPECT_EQ(composer_->composedText(), expected);
  }
  EXPECT_EQ(composer_->feedEnter().commitText, expected);

  // ';' still forms -ing inside a syllable.
  Type("x;");
  EXPECT_EQ(composer_->composedText(), "ㄒㄧㄥ");
}

TEST_F(ComposerTest, UnconfirmedTailTracksToneSettlement) {
  Type("wo");  // eager bare insert: shown as bopomofo, still retrofittable
  EXPECT_EQ(composer_->composedText(), "ㄨㄛ");
  EXPECT_EQ(composer_->unconfirmedTail(), "ㄨㄛ");

  Type("3");  // toned, still unsettled: the tone mark joins the bopomofo
  EXPECT_EQ(composer_->composedText(), "ㄨㄛˇ");
  EXPECT_EQ(composer_->unconfirmedTail(), "ㄨㄛˇ");

  Settle();  // settled -> everything confirmed
  EXPECT_EQ(composer_->composedText(), "我");
  EXPECT_EQ(composer_->unconfirmedTail(), "");
  composer_->feedEsc();

  // Starting the next syllable settles the previous bare one (the 窩愛你
  // scenario): typing the next first key turns ㄨㄛ into 窝.
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
  Settle();
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

TEST_F(ComposerTest, SelectionMovesCursorPastTheFixedSpan) {
  // 你好你好 with the cursor jumped back to the front.
  Type("ni3hk3ni3hk3");
  Settle();
  Type("-");
  ASSERT_EQ(composer_->displaySegments().highlighted, "你");

  // Picking the two-character 妳好 parks the cursor after the whole span,
  // so 8 immediately targets the following character.
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  size_t index = composer_->candidates().size();
  for (size_t i = 0; i < composer_->candidates().size(); ++i) {
    if (composer_->candidates()[i].value == "妳好") index = i;
  }
  ASSERT_LT(index, composer_->candidates().size());
  composer_->selectCandidate(index);
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  auto segments = composer_->displaySegments();
  EXPECT_EQ(segments.before, "妳好");
  EXPECT_EQ(segments.highlighted, "你");
  EXPECT_EQ(segments.after, "好");

  // A single-character pick steps exactly one character right.
  Type("9");  // back onto the second character
  ASSERT_EQ(composer_->displaySegments().highlighted, "好");
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  index = composer_->candidates().size();
  for (size_t i = 0; i < composer_->candidates().size(); ++i) {
    if (composer_->candidates()[i].value == "郝") index = i;
  }
  ASSERT_LT(index, composer_->candidates().size());
  composer_->selectCandidate(index);
  segments = composer_->displaySegments();
  EXPECT_EQ(segments.highlighted, "你");
  EXPECT_EQ(segments.after, "好");
}

TEST_F(ComposerTest, MenuHidesNoOpCandidatesAndSkipsEmptyMenus) {
  // A span whose only candidate is what is already displayed opens no
  // menu at all.
  Type("x;");  // 星 is the only ㄒㄧㄥ entry
  Settle();
  composer_->feedChar('8');
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->composedText(), "星");

  composer_->feedEsc();

  // Same for a settled bopomofo literal (single fixed candidate).
  Type("n`");
  composer_->feedChar('8');
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
}

TEST_F(ComposerTest, SelectDigitWithoutCandidateIsNoOp) {
  Type("de");
  Settle();
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  const size_t count = composer_->candidates().size();
  ASSERT_LT(count, 5u);
  composer_->feedChar('5');  // no 5th candidate on this page
  EXPECT_EQ(composer_->state(), Composer::State::kSelecting);
}

TEST_F(ComposerTest, MenuPagingNoWrap) {
  Type("wo");
  Settle();
  Type("8");
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
  Settle();
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  Type("9");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->displaySegments().highlighted, "好");

  // A letter closes the menu and starts a new syllable at the cursor.
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  Type("wo3");
  Settle();
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
  Settle();
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

TEST_F(ComposerTest, NonControlDigitsEatenOnceSettled) {
  // Settled, only 8/9/0 mean anything; 1-7 are eaten with no output.
  Type("vs");
  Settle();
  for (char digit = '1'; digit <= '7'; ++digit) {
    auto r = composer_->feedChar(digit);
    EXPECT_TRUE(r.consumed) << digit;
    EXPECT_EQ(r.commitText, "") << digit;
  }
  EXPECT_EQ(composer_->composedText(), "中");
}

TEST_F(ComposerTest, ToneWithNoDictionaryEntryLeavesTheWindowOpen) {
  // ㄓㄨㄥ has no tone-2 entry here: the key is eaten, the syllable stays
  // untoned, and the next tone digit still works.
  Type("vs2");
  EXPECT_EQ(composer_->composedText(), "ㄓㄨㄥ");
  Type("3");
  EXPECT_EQ(composer_->composedText(), "ㄓㄨㄥˇ");
  Settle();
  EXPECT_EQ(composer_->composedText(), "種");
}

TEST_F(ComposerTest, SpaceSettlesVisibleBopomofoResidue) {
  // A lone first key + Space settles its bopomofo symbol into the
  // composition (like '`'), without committing.
  Type("n");
  EXPECT_EQ(composer_->composedText(), "ㄋ");
  auto r = composer_->feedChar(' ');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  auto segments = composer_->displaySegments();
  EXPECT_EQ(segments.before, "ㄋ");   // settled, not retrofittable
  EXPECT_EQ(segments.unconfirmed, "");
  EXPECT_EQ(composer_->feedEnter().commitText, "ㄋ");

  // A syllable no toneless entry accepts settles as bopomofo too.
  Type("ul");
  EXPECT_EQ(composer_->composedText(), "ㄕㄞ");
  EXPECT_EQ(composer_->feedChar(' ').commitText, "");
  EXPECT_EQ(composer_->displaySegments().before, "ㄕㄞ");
  composer_->feedEsc();

  // Converted text and settled symbols mix, then commit together.
  Type("vs3n");
  composer_->feedChar(' ');
  EXPECT_EQ(composer_->composedText(), "種ㄋ");
  EXPECT_EQ(composer_->feedEnter().commitText, "種ㄋ");

  // Enter still drops UNSETTLED residue (converted output only).
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
  Settle();
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
  Type("de");
  Settle();
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  auto r = composer_->feedEsc();
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}


}  // namespace
}  // namespace mspy
