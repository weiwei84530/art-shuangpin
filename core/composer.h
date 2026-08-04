// Modal composition engine: owns the reading grid, the pending syllable and
// the Composing/Selecting state machine. Deliberately TSF-free: the shell
// (or the CLI REPL) feeds abstract keys and renders the outputs.
//
// Behavior contract: docs/spec.md §6.
//
// Key map while composing (digits are never typed literally mid-buffer).
// Digits have two meanings selected by whether a syllable is UNSETTLED --
// i.e. still shown as bopomofo rather than as the character it converts to:
//
//   syllable unsettled, no tone yet -> every digit is a tone digit, the
//     right hand reaching them mirrored around the 5/6 gap:
//       1-5 = tones 1-5, and 0=1, 9=2, 8=3, 7=4, 6=5
//   syllable unsettled, tone already given -> all digits (and '-'/'=')
//     are eaten: a second tone digit never re-tones, and the control keys
//     stay out of reach until the syllable is settled (Space settles it)
//   nothing unsettled -> digits are control keys:
//     6,7  eaten, no-op
//     8    open the candidate menu at the cursor
//     9/0  move the cursor left/right, wrapping at both ends
//     -/=  jump the cursor to the start/end of the composition
//          (when idle, '-'/'=' pass through: the shell owns them as
//          Home/End navigation keys)
// While the candidate menu is open:
//   1-6  pick the numbered candidate on the current page
//   7/8  previous/next page (no wrap; out-of-range is a no-op)
//   any other key closes the menu AND performs its normal function.
// Chinese/English switching lives entirely in the shell (a bare Shift tap
// commits the buffer and toggles the system keyboard-open state); the
// composer itself is Chinese-only.
//
// Raw bopomofo symbols (勿轉換): '`' SETTLES bopomofo into the composition
// as fixed black text (still underlined/uncommitted). With a pending
// syllable visible, '`' settles its display (n` -> settled ㄋ); with no
// pending, '`' hollows the initial slot and the next key is read as a
// FINAL whose bopomofo settles directly (`k -> settled ㄠ). Settled
// symbols live in the grid as single-candidate literal readings, so they
// mix freely with converted Chinese and commit together (ㄋㄧㄠ = n`y``k).
//
// Space SETTLES rather than commits (2026-08-02): the syllable in progress
// takes its default tone-1/neutral reading (ㄏㄠ -> 蒿) and bopomofo with
// no reading of its own settles as symbols (ㄋ, ㄋㄧㄠ), all still inside
// the composition. Only when there is nothing left to settle does Space
// commit the whole buffer, like Enter (which still drops the residue).
//
// Full-width punctuation SETTLES and JOINS the composition (2026-08-04)
// instead of committing it: 最, -> 最，still underlined and still
// selectable. Punctuation lives in the grid as a literal reading, exactly
// like '`'-settled bopomofo. Nothing auto-commits any more -- only Enter,
// Space with nothing left to settle, a Shift language switch or losing
// focus send text to the application.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "UserOverrideModel.h"
#include "gramambular2/reading_grid.h"
#include "relaxed_tone_lm.h"
#include "syllable_input.h"

namespace mspy {

class Composer {
 public:
  enum class State { kEmpty, kComposing, kSelecting };

  // Candidates shown per menu page; selection digits are 1..6.
  static constexpr size_t kCandidatePageSize = 6;

  struct Result {
    // Whether the key was eaten by the composer. When false, the shell must
    // let the application handle the key (e.g. literal space/digit with no
    // composition in progress).
    bool consumed = false;
    // Text to commit to the application before/instead of state changes.
    std::string commitText;
  };

  // Takes the OUTERMOST language model of the chain (normally
  // UserPreferenceLM -> RelaxedToneLM -> McBopomofoLM); the composer only
  // needs hasUnigrams from it.
  explicit Composer(
      std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm);

  State state() const { return state_; }

  // Inline composition text: walked sentence followed by the pending
  // syllable's display (e.g. 我喜歡ㄋ while typing ㄋㄧˇ).
  std::string composedText() const;

  // Display decomposition of composedText():
  //   before      tone-settled text left of the active area
  //   unconfirmed the pending syllable display plus, if the syllable just
  //               inserted is still unsettled, ITS BOPOMOFO WITH THE TONE
  //               MARK (2026-08-02: the second key of a syllable no longer
  //               flashes the tone-1 character — hk shows ㄏㄠ; 2026-08-04:
  //               a tone digit no longer settles it either — hk4 shows
  //               ㄏㄠˋ, and Space/punctuation/the next syllable turns it
  //               into 號. Explicit tone 1 shows the ˉ mark so "toned" and
  //               "not toned yet" never look alike.)
  //   highlighted the single character right of the cursor (the selection
  //               anchor emphasized with a background color); empty when
  //               the cursor is at the right end
  //   after       tone-settled text right of `highlighted`
  // The caret sits between `unconfirmed` and `highlighted`. The shell
  // renders `unconfirmed` with the "input" attribute (blue), `highlighted`
  // with the background-highlight attribute and the rest as "converted"
  // (black); the whole string stays underlined until commit.
  struct DisplaySegments {
    std::string before;
    std::string unconfirmed;
    std::string highlighted;
    std::string after;
  };
  DisplaySegments displaySegments() const;

  // Convenience for tests: displaySegments().unconfirmed.
  std::string unconfirmedTail() const;

  // Candidates of the span being selected (valid in kSelecting).
  const std::vector<Formosa::Gramambular2::ReadingGrid::Candidate>&
  candidates() const {
    return candidates_;
  }

  // Menu paging (valid in kSelecting).
  size_t candidatePageIndex() const { return pageIndex_; }
  size_t candidatePageCount() const;
  // The slice of candidates() visible on the current page (at most
  // kCandidatePageSize entries).
  std::vector<Formosa::Gramambular2::ReadingGrid::Candidate>
  currentPageCandidates() const;

  // Side-effect-free preview of feedChar's consumption decision. TSF calls
  // OnTestKeyDown before OnKeyDown, so the eat/pass decision must not
  // mutate state.
  bool wouldConsume(char c) const;

  // Feeds a printable character (letters, ';', digits, space, ',', '.').
  Result feedChar(char c);
  Result feedBackspace();
  Result feedEnter();
  // Esc cancels the whole composition (closing the menu first if open).
  Result feedEsc();

  // Closes the candidate menu without touching the composition (used by the
  // shell for window-only teardown, e.g. mouse dismissal).
  Result closeCandidateMenu();
  // Unconditionally resets the composer (app-terminated composition).
  void cancel() { reset(); }

  // Selects a candidate by index into candidates() (kSelecting only).
  Result selectCandidate(size_t index);

  // Called on every manual candidate selection with (reading key, value);
  // the shell persists these as user preferences. The reading key is free
  // of internal sentinels.
  //
  // A SINGLE-CHARACTER pick reports the two-syllable phrase it sits in
  // rather than the character alone (我在家 -> 我在, ㄨㄛˇ-ㄗㄞˋ). A global
  // preference for a lone 在 would just flip 我在家 and 我再說一次 against
  // each other; the surrounding context is what actually disambiguates, so
  // that is what gets learned. Nothing is reported when the character has
  // no single-character neighbour to pair with.
  std::function<void(const std::string&, const std::string&)>
      onManualSelection;

  // Called once per multi-syllable phrase in the buffer when it commits,
  // with the same normalized (reading key, value) pair. The shell refreshes
  // matching preferences so vocabulary that keeps being used as-is does not
  // decay out of the store; it never creates entries.
  std::function<void(const std::string&, const std::string&)> onPhraseUsed;

 private:
  // Finalizes the pending complete syllable as tone-less (tone 1/neutral).
  // This happens EAGERLY the moment the second key completes a syllable so
  // the sentence walk sees it (the character itself stays hidden behind
  // its bopomofo until settled); a following tone digit then *retrofits*
  // the tone onto that syllable.
  // Returns false if no dictionary entry accepts the tone-less reading (the
  // syllable then stays pending, shown as bopomofo, awaiting a tone digit).
  bool finalizePendingBare();
  // Applies a tone digit ('1'..'5') to the still-pending complete syllable
  // (the path taken when no tone-less entry exists, e.g. ㄗㄨㄟˋ).
  bool applyToneToPending(char digit);
  // Replaces the unsettled bare syllable in the grid with its toned
  // reading. A tone with no dictionary entry leaves the syllable bare and
  // still untoned, so another tone digit can be tried.
  bool applyToneToUnsettled(char digit);
  // Backspace on a toned-but-unsettled syllable: drops just the tone,
  // returning to the untoned unsettled state (which is how a mistyped tone
  // gets corrected, since a second tone digit is ignored).
  void undoUnsettledTone();
  // Settles whatever is in progress: the pending syllable converts (or its
  // bopomofo settles as symbols) and the unsettled syllable stops hiding
  // behind its bopomofo. Idempotent; a no-op when nothing is unsettled.
  void settlePending();
  // True while a syllable is unsettled anywhere -- pending (half or whole)
  // or in the grid. Digits are tone keys or eaten in this window, never
  // control keys.
  bool anythingUnsettled() const { return unsettled_.active || !pending_.empty(); }
  // Inserts a reading into the grid and re-walks.
  bool insertReading(const std::string& reading);
  // Settles UTF-8 text into the grid as literal readings, one per code
  // point ('`'-fixed bopomofo symbols).
  void insertLiteralText(const std::string& utf8);
  // Moves the cursor by delta with wrap-around at both ends.
  void moveCursor(int delta);
  // Jumps the cursor to the start or the end of the grid ('-'/'=').
  void jumpCursor(bool toStart);
  // Opens the candidate menu at the cursor span (digit 8).
  Result openCandidateMenu();
  // Clears menu state and returns to kComposing.
  void dismissMenu();
  // Selects by page-relative index ('1'..'6'); out of range is a no-op.
  Result selectOnCurrentPage(size_t indexInPage);
  // Reports a manual pick to the shell, widening a single-character pick to
  // the two-syllable phrase around it (see onManualSelection).
  void reportManualSelection(
      const Formosa::Gramambular2::ReadingGrid::Candidate& chosen);
  // Reports every multi-syllable phrase in the walk to onPhraseUsed.
  void reportPhrasesUsed() const;
  // Key dispatch while the hollow-final sub-state is active ('`' pressed,
  // awaiting the final key).
  Result feedHollowFinal(char c);
  // Space: settles the syllable in progress (default tone, or its bopomofo
  // when no reading fits) and commits the buffer only when there is
  // nothing left to settle.
  Result settleOrCommit();
  // Commits the current buffer (dropping a half-typed syllable) and resets.
  std::string takeCommitText();
  void reset();
  void updateStateAfterMutation();

  std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm_;
  Formosa::Gramambular2::ReadingGrid grid_;
  Formosa::Gramambular2::ReadingGrid::WalkResult walk_;
  SyllableInput pending_;
  State state_ = State::kEmpty;

  // Session re-ranking learned from manual selections (LRU with time
  // decay, McBopomofo's UserOverrideModel).
  McBopomofo::UserOverrideModel uom_;

  // The most recent syllable, already in the grid (so the sentence walk
  // sees it and the preceding text keeps auto-correcting) but still shown
  // as bopomofo rather than as the character it converts to. A tone digit
  // refines it in place; Space, punctuation, the next syllable's first key,
  // a cursor move or the candidate menu SETTLES it, which is when the
  // character finally appears.
  struct Unsettled {
    bool active = false;
    // Set once a tone digit has been applied: further tone digits are
    // ignored (correcting a tone goes through Backspace).
    bool toneGiven = false;
    // Tone-less readings of the syllable, best first -- what a tone mark
    // gets appended to, and what Backspace restores.
    std::vector<std::string> syllables;
    // The two double-pinyin keys, kept so Backspace can put the syllable
    // back into pending_ when no tone-less reading exists (ㄗㄨㄟ).
    std::string keys;
    // Displayed bopomofo; identical to the reading now in the grid, tone
    // mark included (ㄏㄠ, ㄏㄠˋ, ㄏㄠˉ, ㄏㄠ˙).
    std::string display;
  };
  Unsettled unsettled_;

  // Paired-quote alternation (Rime-style): next " types “ or ”.
  bool doubleQuoteOpen_ = false;
  bool singleQuoteOpen_ = false;

  // Hollow-final sub-state: set between a bare '`' and the final key.
  bool hollowFinal_ = false;

  // Selection state.
  size_t selectionLocation_ = 0;
  size_t pageIndex_ = 0;
  std::vector<Formosa::Gramambular2::ReadingGrid::Candidate> candidates_;
};

}  // namespace mspy
