#include "composer.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

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
    // Syllables a LONE first key spells by itself (z -> ㄗ, u -> ㄕ...).
    inner->add("ㄗ", "資", -4);
    inner->add("ㄗˋ", "字", -3);
    inner->add("ㄕ", "師", -4);
    inner->add("ㄕˋ", "是", -2);
    // 知情: a lone ㄓ in front of a two-key syllable (v q ;).
    inner->add("ㄓ", "之", -3);
    inner->add("ㄑㄧㄥˊ", "情", -3);
    inner->add("ㄓ-ㄑㄧㄥˊ", "知情", -1);
    // ㄏ + the 'w'/'y' keys decode to two candidates each; only one of them
    // is a real syllable, and the dictionary is what says which.
    inner->add("ㄏㄨㄞˊ", "懷", -3);
    inner->add("ㄏㄨㄚ", "花", -3);
    // 不鏽鋼杯: a three-character phrase whose tail a two-character
    // candidate overlaps. Scores are set so the walk prefers 不鏽鋼 + 悲.
    inner->add("ㄅㄨˊ", "不", -2);
    inner->add("ㄒㄧㄡˋ", "秀", -3);
    inner->add("ㄒㄧㄡˋ", "鏽", -5);
    inner->add("ㄍㄤ", "鋼", -4);
    inner->add("ㄅㄟ", "悲", -3);
    inner->add("ㄅㄟ", "杯", -4);
    inner->add("ㄅㄨˊ-ㄒㄧㄡˋ-ㄍㄤ", "不鏽鋼", -5);
    inner->add("ㄍㄤ-ㄅㄟ", "鋼杯", -6.8);
    lm_ = std::make_shared<RelaxedToneLM>(inner);
    composer_ = std::make_unique<Composer>(lm_);
  }

  // Feeds a string of printable keys.
  void Type(const std::string& keys) {
    for (char c : keys) composer_->feedChar(c);
  }

  // Space with a TONELESS syllable in progress: settles it into its
  // character without committing. A toned syllable settles itself, so this
  // is only for the tone-1/neutral default — with nothing left to settle
  // Space types a half-width space into the buffer instead.
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

TEST_F(ComposerTest, ToneDigitSettlesTheSyllable) {
  // 2026-08-08: the tone digit turns the bopomofo into its character right
  // away, exactly like Space does for the toneless default.
  Type("vs3");
  EXPECT_EQ(composer_->composedText(), "種");
  EXPECT_EQ(composer_->unconfirmedTail(), "");

  // Settled means the digits are control keys again: 9 moves the cursor
  // instead of typing tone 2.
  auto r = composer_->feedChar('9');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "種");
  EXPECT_EQ(composer_->displaySegments().highlighted, "種");
}

TEST_F(ComposerTest, MirroredToneDigits) {
  // Tones are reachable with either hand: 1-5 on the left, and 0-7
  // mirrored around the 5/6 gap on the right (0=1, 9=2, 8=3, 7=4). 6 is
  // NOT one of them since 2026-08-17 -- it is Backspace -- so the
  // neutral tone is 5 alone.
  const struct {
    const char* keys;
    const char* settled;
  } kCases[] = {
      {"de0", "得"},  // explicit tone 1
      {"de9", "德"},
      {"vs8", "種"},
      {"ul7", "曬"},
  };
  for (const auto& c : kCases) {
    composer_->feedEsc();
    Type(c.keys);
    EXPECT_EQ(composer_->composedText(), c.settled) << c.keys;
  }
}

TEST_F(ComposerTest, DigitsAreTonesUntilTheSyllableSettles) {
  // While the syllable shows bopomofo every digit is a tone digit, so the
  // control meanings (menu, cursor, delete) are all out of reach.
  Type("ni3hk");
  EXPECT_EQ(composer_->composedText(), "你ㄏㄠ");

  // 8 is tone 3 here, not the menu key — and it settles the syllable.
  Type("8");
  EXPECT_EQ(composer_->composedText(), "你好");

  // Settled: the same 8 opens the menu.
  Type("8");
  EXPECT_EQ(composer_->state(), Composer::State::kSelecting);
}

TEST_F(ComposerTest, BackspaceDeletesTheTonedSyllable) {
  // A tone digit settles its syllable, so there is no tone left to take
  // back: Backspace removes the character and the syllable is retyped.
  Type("vs3");
  EXPECT_EQ(composer_->composedText(), "種");
  composer_->feedBackspace();
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);

  // Same for a syllable that only exists with a tone (ㄕㄞ).
  Type("ul4");
  EXPECT_EQ(composer_->composedText(), "曬");
  composer_->feedBackspace();
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);

  // An untoned syllable is still shown as bopomofo rather than as its
  // character, and it goes the same way: one press clears the whole thing.
  Type("vs");
  EXPECT_EQ(composer_->composedText(), "ㄓㄨㄥ");
  composer_->feedBackspace();
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

TEST_F(ComposerTest, LoneFirstKeyConvertsWithATone) {
  // A first key whose bopomofo is already a syllable needs no final: 字 is
  // two keystrokes (z, tone 4).
  Type("z4");
  EXPECT_EQ(composer_->composedText(), "字");
  EXPECT_EQ(composer_->feedEnter().commitText, "字");

  // Space takes the tone-1/neutral default, as everywhere else.
  Type("z");
  EXPECT_EQ(composer_->composedText(), "ㄗ");
  Settle();
  EXPECT_EQ(composer_->composedText(), "資");
  composer_->feedEsc();

  // Enter converts it too, rather than dropping it as unfinished input.
  Type("u4");
  EXPECT_EQ(composer_->feedEnter().commitText, "是");

  // The key still takes a final instead when one follows (ㄕ -> ㄕㄞ).
  Type("ul4");
  EXPECT_EQ(composer_->composedText(), "曬");
  composer_->feedEsc();

  // A key that spells no syllable on its own (ㄋ) ignores the tone digit
  // and keeps waiting for its final.
  Type("n");
  auto r = composer_->feedChar('4');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(composer_->composedText(), "ㄋ");
  Type("i3");
  EXPECT_EQ(composer_->composedText(), "你");
}

TEST_F(ComposerTest, PendingSyllableShowsTheReadingThatExists) {
  // The 'y' key is both ü and uai: ㄏ + y decodes to ㄏㄩ (impossible) and
  // ㄏㄨㄞ (懷). The display must never show the impossible one.
  Type("hy");
  EXPECT_EQ(composer_->composedText(), "ㄏㄨㄞ");
  Type("2");
  EXPECT_EQ(composer_->composedText(), "懷");
  composer_->feedEsc();

  // Same for the 'w' key (ia vs ua): ㄏㄧㄚ is impossible, 花 is not.
  Type("hw");
  EXPECT_EQ(composer_->composedText(), "ㄏㄨㄚ");
  Settle();
  EXPECT_EQ(composer_->composedText(), "花");
  composer_->feedEsc();

  // A pair that spells no syllable at all is not our key: it is eaten and
  // the first key keeps waiting for a final it can use.
  Type("h");
  auto r = composer_->feedChar('x');  // ㄏㄧㄝ: no such syllable
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(composer_->composedText(), "ㄏ");
  Type("w");
  EXPECT_EQ(composer_->composedText(), "ㄏㄨㄚ");
}

TEST_F(ComposerTest, ALoneSyllableNeverSplitsImplicitly) {
  // Decision record (2026-08-08, implemented and reverted the same day):
  // ㄓ cannot take the 'q' final, so 'q' COULD be read as the next
  // syllable's first key and make 知情 three keys. It is not: 知識 (v+u =
  // ㄓㄨ, a real syllable) can never work that way, so the two words would
  // train opposite habits and the rule would be "sometimes". The key is
  // eaten instead, and the separator is always Space or a tone digit.
  Type("v");
  auto r = composer_->feedChar('q');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(composer_->composedText(), "ㄓ");

  // With the separator the same three-key sequence works as everywhere.
  Settle();
  Type("q;2");
  EXPECT_EQ(composer_->composedText(), "知情");  // the walk corrects 之
  EXPECT_EQ(composer_->feedEnter().commitText, "知情");
}

TEST_F(ComposerTest, SpecSyllables) {
  Type("ul4");
  EXPECT_EQ(composer_->composedText(), "曬");
  composer_->feedEsc();
  Type("oj3");
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
  EXPECT_EQ(composer_->composedText(), "得");  // explicit 1: strictly tone 1
  composer_->feedEsc();

  Type("de2");
  EXPECT_EQ(composer_->composedText(), "德");
  composer_->feedEsc();

  Type("de5");
  EXPECT_EQ(composer_->composedText(), "的");
  composer_->feedEsc();

  Type("wo");
  Settle();
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

TEST_F(ComposerTest, SpacePassesThroughWhenIdleAndTypesASpaceOnceSettled) {
  auto idle = composer_->feedChar(' ');
  EXPECT_FALSE(idle.consumed);

  // First Space settles the syllable. The second has nothing left to
  // settle, so it types a half-width space into the buffer -- it does not
  // commit (2026-08-16). Only Enter commits.
  Type("vs");
  auto r = composer_->feedChar(' ');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "中");
  r = composer_->feedChar(' ');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "中 ");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);

  // The space is ordinary buffer content: Backspace takes it back and
  // Enter is what finally sends the lot.
  composer_->feedBackspace();
  EXPECT_EQ(composer_->composedText(), "中");
  EXPECT_EQ(composer_->feedEnter().commitText, "中");

  // A toned syllable settled itself, so Space after it is a space.
  Type("vs3");
  EXPECT_EQ(composer_->composedText(), "種");
  r = composer_->feedChar(' ');
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "種 ");
  composer_->feedEsc();

  // Space still cannot open a composition of its own: idle, it is the
  // application's.
  composer_->feedEsc();
  r = composer_->feedChar(' ');
  EXPECT_FALSE(r.consumed);
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

TEST_F(ComposerTest, DigitsFiveAndSixDeleteOnceSettled) {
  // 6 is Backspace and 5 is Delete, so deleting inside the composition never
  // takes a hand off the main block.
  Type("ni3hk3");
  EXPECT_EQ(composer_->composedText(), "你好");
  Type("6");
  EXPECT_EQ(composer_->composedText(), "你");

  Type("hk3");
  Type("9");  // cursor back between 你 and 好
  Type("5");  // forward delete removes 好
  EXPECT_EQ(composer_->composedText(), "你");
  Type("5");  // nothing to the right any more
  EXPECT_EQ(composer_->composedText(), "你");
}

TEST_F(ComposerTest, SixDeletesWhileBopomofoShowsAndFiveKeepsTheNeutralTone) {
  // 2026-08-17 reverses the 2026-08-14 ruling that 5 and its mirror 6 could
  // not be borrowed: 6 is Backspace in every state now, which leaves the
  // neutral tone one key instead of two.
  Type("d5");
  EXPECT_EQ(composer_->composedText(), "的");
  composer_->feedEsc();

  Type("de");  // still bopomofo on screen, so every digit is a tone key...
  Type("6");   // ... except 6, which deletes the syllable outright
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

TEST_F(ComposerTest, MinusEqualsPlusTypeThemselves) {
  // '-' '=' '+' are the three deliberate half-width exceptions (2026-08-16):
  // Rime's half_shape table types them literally under the same layout, so we
  // do too. They behave like every other punctuation key -- settle whatever is
  // pending, join the composition, never commit on their own.
  Type("ni3hk3");
  Type("-");
  EXPECT_EQ(composer_->composedText(), "你好-");
  Type("=");
  Type("+");
  EXPECT_EQ(composer_->composedText(), "你好-=+");
  EXPECT_EQ(composer_->feedEnter().commitText, "你好-=+");

  // A pending syllable settles first, exactly as a comma settles it.
  Type("hk");
  EXPECT_EQ(composer_->composedText(), "ㄏㄠ");
  Type("=");
  EXPECT_EQ(composer_->unconfirmedTail(), "");
  composer_->feedEsc();

  // Idle they open a fresh composition rather than committing.
  Type("+");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->composedText(), "+");
}

TEST_F(ComposerTest, MinusWhileSelectingDismissesTheMenuThenTypes) {
  Type("ni3hk3");
  Type("9");  // cursor between 你 and 好
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  // Any key that is not a selection key closes the menu and then performs
  // its own function -- for '-' that is typing itself at the cursor.
  Type("-");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  EXPECT_EQ(composer_->composedText(), "你-好");
}

TEST_F(ComposerTest, HalfWidthOnlySymbolsAreEatenEvenWhenIdle) {
  // A symbol key with no meaning of its own must not leak a half-width
  // character into the document. Typing these requires English mode (Shift).
  // ('-' '=' '+' left this list on 2026-08-16; they now type themselves.)
  for (const char c : {'@', '#', '$', '%', '&', '*', '|'}) {
    auto r = composer_->feedChar(c);
    EXPECT_TRUE(r.consumed) << c;
    EXPECT_EQ(r.commitText, "") << c;
    EXPECT_EQ(composer_->state(), Composer::State::kEmpty) << c;
  }
}

TEST_F(ComposerTest, ControlCharactersAreNeverConsumed) {
  // Ctrl chords reach the shell as control codes; the composer must decline
  // them or Ctrl+A would be swallowed instead of selecting all.
  EXPECT_FALSE(composer_->wouldConsume(''));
  EXPECT_FALSE(composer_->wouldConsume(''));
  EXPECT_FALSE(composer_->wouldConsume(''));
  Type("ni3");
  EXPECT_FALSE(composer_->wouldConsume(''));
}

TEST_F(ComposerTest, SlashTypesTheEnumerationComma) {
  // Rime maps both '\' and '/' to 、; '/' is the shorter reach.
  Type("/");
  EXPECT_EQ(composer_->composedText(), "、");
  Type("\\");
  EXPECT_EQ(composer_->composedText(), "、、");
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

  // With a syllable in progress it settles it and appends itself.
  Type("ul");
  EXPECT_EQ(composer_->composedText(), "ㄕㄞ");
  Type("4");
  EXPECT_EQ(composer_->composedText(), "曬");
  r = composer_->feedChar('.');
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "曬。");

  // The sentence keeps composing across the punctuation, and it all
  // commits together on Enter.
  Type("vs3");
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
  Type("00");  // walk the cursor back to the end
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

  Type("3");  // the tone settles it: character out, nothing unconfirmed
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

TEST_F(ComposerTest, BackspaceDeletesTheWholeSyllableInProgress) {
  Type("vs");
  EXPECT_EQ(composer_->composedText(), "ㄓㄨㄥ");
  composer_->feedBackspace();
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
  EXPECT_EQ(composer_->composedText(), "");  // the lot, not one key of it

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
  // 你好你好 with the cursor walked back to the front.
  Type("ni3hk3ni3hk3");
  Type("9999");
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

TEST_F(ComposerTest, PickingACandidateLeavesTheRestOfTheSentenceAlone) {
  Type("bu2xq4gh");
  Type("bz");
  Settle();
  ASSERT_EQ(composer_->composedText(), "不鏽鋼悲");

  // 鋼杯 spans the tail of the 不鏽鋼 node, so applying it tears that node
  // up and the leftover 不鏽 would re-segment into 不秀. Only the two
  // characters the user actually chose may move.
  Type("8");
  ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
  size_t index = composer_->candidates().size();
  for (size_t i = 0; i < composer_->candidates().size(); ++i) {
    if (composer_->candidates()[i].value == "鋼杯") index = i;
  }
  ASSERT_LT(index, composer_->candidates().size());
  composer_->selectCandidate(index);
  EXPECT_EQ(composer_->composedText(), "不鏽鋼杯");
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

TEST_F(ComposerTest, NonControlDigitsEatenOnceSettled) {
  // Settled, only 5/6 (delete) and 8/9/0 mean anything; the rest are eaten
  // with no output.
  Type("vs");
  Settle();
  for (const char digit : {'1', '2', '3', '4', '7'}) {
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

  // Enter no longer drops unsettled residue either (2026-08-17): it sends
  // the screen, bopomofo and all.
  Type("vs3n");
  EXPECT_EQ(composer_->feedEnter().commitText, "種ㄋ");
}

TEST_F(ComposerTest, BacktickTypesItself) {
  // The bopomofo function key is gone (2026-08-17). '`' used to settle the
  // pending syllable as symbols, and on its own hollowed the initial slot
  // so the next key read as a final (`k -> ㄠ). Both are retired: Space
  // settles, Enter sends the screen as it stands, and '`' is just Rime's
  // half-width punctuation now.
  auto r = composer_->feedChar('`');
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "`");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);
  composer_->feedEsc();

  // It settles what is pending, exactly like every other punctuation key,
  // and the next key is an ordinary first key rather than a final.
  Type("vs3");
  composer_->feedChar('`');
  Type("k");
  EXPECT_EQ(composer_->composedText(), "種`ㄎ");
  composer_->feedEsc();
}

TEST_F(ComposerTest, EnterSendsWhatIsOnScreen) {
  // 2026-08-17: Enter commits the screen verbatim. A syllable still shown
  // as bopomofo goes out AS bopomofo -- it is neither dropped (which is
  // what used to happen to a syllable no reading fits) nor silently
  // converted with the tone-1/neutral default.
  Type("n");  // no ㄋㄜ in the dictionary: used to commit nothing at all
  EXPECT_EQ(composer_->feedEnter().commitText, "ㄋ");

  Type("ul");  // ㄕㄞ exists only with a tone
  EXPECT_EQ(composer_->feedEnter().commitText, "ㄕㄞ");

  Type("vs");  // ㄓㄨㄥ DOES convert (中), but the screen says bopomofo
  EXPECT_EQ(composer_->feedEnter().commitText, "ㄓㄨㄥ");

  // Settled text is unaffected: a tone digit or Space is how a character
  // is asked for, and both still work.
  Type("vs3");
  EXPECT_EQ(composer_->feedEnter().commitText, "種");
  Type("vs");
  composer_->feedChar(' ');
  EXPECT_EQ(composer_->feedEnter().commitText, "中");

  // Settled characters and an unsettled tail travel together.
  Type("vs3ul");
  EXPECT_EQ(composer_->feedEnter().commitText, "種ㄕㄞ");
}

// What the shell is told to learn from a manual pick. A single-character
// pick reports the phrase around it, never the character alone.
class ComposerLearningTest : public ComposerTest {
 protected:
  void SetUp() override {
    ComposerTest::SetUp();
    prefs_ = std::make_shared<UserPreferences>();
    composer_->setPreferences(prefs_);
  }

  // Opens the menu at the cursor and picks the named candidate.
  void PickByValue(const std::string& value) {
    Type("8");
    ASSERT_EQ(composer_->state(), Composer::State::kSelecting);
    for (size_t i = 0; i < composer_->candidates().size(); ++i) {
      if (composer_->candidates()[i].value == value) {
        composer_->selectCandidate(i);
        return;
      }
    }
    FAIL() << "no candidate " << value;
  }

  std::shared_ptr<UserPreferences> prefs_;
};

TEST_F(ComposerLearningTest, OneCorrectionIsEnough) {
  Type("ni3hk3");
  Type("9");  // anchor the second character
  ASSERT_EQ(composer_->displaySegments().highlighted, "好");
  PickByValue("郝");
  ASSERT_EQ(composer_->composedText(), "你郝");
  composer_->feedEnter();

  // Typing the very same thing again must not need the correction twice.
  Type("ni3hk3");
  EXPECT_EQ(composer_->composedText(), "你郝");
}

TEST_F(ComposerLearningTest, TheCorrectionIsTiedToItsContext) {
  Type("ni3hk3");
  Type("9");
  PickByValue("郝");
  composer_->feedEnter();

  // ㄏㄠˇ after something else is untouched: what was learned is "after 你",
  // not "ㄏㄠˇ means 郝".
  Type("vs3hk3");
  EXPECT_EQ(composer_->composedText(), "種好");
}

TEST_F(ComposerLearningTest, PicksAreRecordedUnderBothContextLengths) {
  Type("wo3ni3hk3");
  Type("9");
  ASSERT_EQ(composer_->displaySegments().highlighted, "好");
  PickByValue("郝");
  EXPECT_EQ(prefs_->lookup("你", "ㄏㄠˇ"), "郝");
  EXPECT_EQ(prefs_->lookup("我你", "ㄏㄠˇ"), "郝");
}

TEST_F(ComposerLearningTest, ContextIsLearnedFromInsideALongerWord) {
  // The old store gave up here: the character on the left belonged to a
  // three-character node, so there was no single-character neighbour to
  // pair with and nothing at all was learned.
  Type("bu2xq4gh");
  Type("bz");
  Settle();
  ASSERT_EQ(composer_->composedText(), "不鏽鋼悲");
  PickByValue("杯");
  ASSERT_EQ(composer_->composedText(), "不鏽鋼杯");
  EXPECT_EQ(prefs_->lookup("鋼", "ㄅㄟ"), "杯");
  EXPECT_EQ(prefs_->lookup("鏽鋼", "ㄅㄟ"), "杯");

  // ...and it applies on its own the next time, without disturbing the
  // three-character word in front of it.
  composer_->feedEnter();
  Type("bu2xq4gh");
  Type("bz");
  Settle();
  EXPECT_EQ(composer_->composedText(), "不鏽鋼杯");
}

TEST_F(ComposerLearningTest, ContextStopsAtPunctuation) {
  // Nothing before the comma says anything about what follows it.
  Type("ni3hk3");
  composer_->feedChar(',');
  Type("hk3");
  Type("9");
  ASSERT_EQ(composer_->displaySegments().highlighted, "好");
  PickByValue("郝");
  EXPECT_EQ(prefs_->lookup(UserPreferences::kStartContext, "ㄏㄠˇ"), "郝");
  EXPECT_TRUE(prefs_->lookup("好", "ㄏㄠˇ").empty());
}

TEST_F(ComposerLearningTest, AManualPickOverridesWhatWasLearned) {
  Type("ni3hk3");
  Type("9");
  PickByValue("郝");
  composer_->feedEnter();

  // The correction fires, the user disagrees, and the disagreement sticks:
  // a learned override never argues with a node the user just set.
  Type("ni3hk3");
  ASSERT_EQ(composer_->composedText(), "你郝");
  Type("9");
  ASSERT_EQ(composer_->displaySegments().highlighted, "郝");
  PickByValue("好");
  EXPECT_EQ(composer_->composedText(), "你好");
  composer_->feedEnter();
  Type("ni3hk3");
  EXPECT_EQ(composer_->composedText(), "你好");
}

TEST_F(ComposerLearningTest, WithoutAStoreNothingIsLearned) {
  composer_->setPreferences(nullptr);
  Type("ni3hk3");
  Type("9");
  PickByValue("郝");
  EXPECT_EQ(composer_->composedText(), "你郝");
  composer_->feedEnter();
  Type("ni3hk3");
  EXPECT_EQ(composer_->composedText(), "你好");
}

// English mode inside a live composition: the buffer survives the switch
// and holds both scripts until it commits (2026-08-08).
TEST_F(ComposerTest, EnglishJoinsTheUncommittedComposition) {
  Type("ni3hk3");
  EXPECT_EQ(composer_->composedText(), "你好");

  // Switching to English settles what is in progress and drops in the
  // separator space; nothing is committed.
  auto r = composer_->switchLanguage(/*toEnglish=*/true);
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.commitText, "");
  EXPECT_EQ(composer_->composedText(), "你好 ");

  for (char c : std::string("a Test")) composer_->feedEnglishChar(c);
  EXPECT_EQ(composer_->composedText(), "你好 a Test");
  EXPECT_EQ(composer_->state(), Composer::State::kComposing);

  // Switching back adds the closing separator, and Chinese carries on in
  // the same buffer.
  composer_->switchLanguage(/*toEnglish=*/false);
  EXPECT_EQ(composer_->composedText(), "你好 a Test ");
  Type("vs3");
  EXPECT_EQ(composer_->composedText(), "你好 a Test 種");

  // One commit, one string.
  EXPECT_EQ(composer_->feedEnter().commitText, "你好 a Test 種");
  EXPECT_EQ(composer_->state(), Composer::State::kEmpty);
}

TEST_F(ComposerTest, EnglishIsDeletableAndSelectableLikeAnyLiteral) {
  Type("ni3hk3");
  composer_->switchLanguage(true);
  for (char c : std::string("ab")) composer_->feedEnglishChar(c);
  EXPECT_EQ(composer_->composedText(), "你好 ab");

  composer_->feedBackspace();
  EXPECT_EQ(composer_->composedText(), "你好 a");
  composer_->feedBackspace();
  composer_->feedBackspace();  // the separator space
  EXPECT_EQ(composer_->composedText(), "你好");
}

TEST_F(ComposerTest, SeparatorSpaceOnlyWhereTheScriptsMeet) {
  // Punctuation on the boundary needs no space.
  composer_->feedChar(',');
  composer_->switchLanguage(true);
  EXPECT_EQ(composer_->composedText(), "，");
  composer_->feedEnglishChar('a');
  EXPECT_EQ(composer_->composedText(), "，a");

  // ...and neither does a space that is already there.
  composer_->feedEnglishChar(' ');
  composer_->switchLanguage(false);
  EXPECT_EQ(composer_->composedText(), "，a ");
  composer_->feedEsc();

  // A bopomofo symbol counts as Chinese.
  Type("n");
  composer_->feedChar(' ');  // settles ㄋ as a symbol, no reading fits
  composer_->switchLanguage(true);
  EXPECT_EQ(composer_->composedText(), "ㄋ ");
}

TEST_F(ComposerTest, LanguageSwitchWithNothingComposingIsNotConsumed) {
  // With an empty buffer the switch is a pure mode flip for the shell, and
  // English keys go straight to the application.
  auto r = composer_->switchLanguage(true);
  EXPECT_FALSE(r.consumed);
  r = composer_->feedEnglishChar('a');
  EXPECT_FALSE(r.consumed);
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
