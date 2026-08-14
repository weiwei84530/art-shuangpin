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
//   syllable unsettled -> every digit is a tone digit, the right hand
//     reaching them mirrored around the 5/6 gap:
//       1-5 = tones 1-5, and 0=1, 9=2, 8=3, 7=4, 6=5
//     The tone digit SETTLES the syllable (2026-08-08), so the character
//     appears at once and the control keys are usable again immediately; a
//     mistyped tone is corrected by deleting the syllable (Backspace) and
//     retyping it.
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
//
// A syllable normally takes two keys, but a first key whose bopomofo is
// already a syllable (ㄓㄔㄕㄖㄗㄘㄙ, ㄧㄨ, ㄚㄜㄛ) converts on its own as
// soon as a tone digit or Space arrives (2026-08-08): 字 = z4, 是 = u4,
// 知 = v + Space. Those keys still accept a final instead (zh -> ㄗㄤ), and
// a key that spells nothing with them is simply eaten: separating a lone
// syllable from what follows is ALWAYS Space (or a tone digit), never
// implicit -- see the reverted split in feedChar.
//
// Chinese/English switching is driven by the shell (a bare Shift tap toggles
// the system keyboard-open state), but the composition SURVIVES it
// (2026-08-08): switchLanguage() only settles what is in progress and drops
// in the half-width separator space, and while English mode is on the shell
// feeds letters through feedEnglishChar so they join the same underlined,
// uncommitted buffer as literal text.
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

#include "gramambular2/reading_grid.h"
#include "relaxed_tone_lm.h"
#include "syllable_input.h"
#include "user_preferences.h"

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
  // RelaxedToneLM -> McBopomofoLM). What the user has taught the IME is
  // NOT a model layer: it is applied as node overrides on the grid, see
  // setPreferences.
  explicit Composer(
      std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm);

  State state() const { return state_; }

  // Inline composition text: walked sentence followed by the pending
  // syllable's display (e.g. 我喜歡ㄋ while typing ㄋㄧˇ).
  std::string composedText() const;

  // Display decomposition of composedText():
  //   before      tone-settled text left of the active area
  //   unconfirmed the pending syllable display plus, if the syllable just
  //               inserted is still unsettled, ITS BOPOMOFO (2026-08-02: the
  //               second key of a syllable no longer flashes the tone-1
  //               character — hk shows ㄏㄠ, and Space, a tone digit,
  //               punctuation or the next syllable turns it into 蒿/好)
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
  // Forward delete (digit 5 while composing).
  Result feedDelete();
  Result feedEnter();
  // Esc cancels the whole composition (closing the menu first if open).
  Result feedEsc();

  // Feeds a character typed in ENGLISH mode. It settles whatever Chinese is
  // in progress and joins the composition as literal text, keeping one
  // uncommitted buffer for both scripts. Returns consumed=false when nothing
  // is composing (the shell then lets the key through to the application).
  Result feedEnglishChar(char c);

  // Bare-Shift language switch with a live composition: settles what is in
  // progress and inserts the half-width separator space when the character
  // before the cursor calls for one (Chinese before English, an English word
  // character before Chinese). Never commits. Returns consumed=false when
  // there is no composition to keep, which is the shell's cue that the
  // switch is nothing but a mode flip.
  Result switchLanguage(bool toEnglish);

  // Closes the candidate menu without touching the composition (used by the
  // shell for window-only teardown, e.g. mouse dismissal).
  Result closeCandidateMenu();
  // Unconditionally resets the composer (app-terminated composition).
  void cancel() { reset(); }

  // Selects a candidate by index into candidates() (kSelecting only).
  Result selectCandidate(size_t index);

  // The store of learned corrections. The composer both READS it (every
  // walk is checked against it, see applyLearnedOverrides) and WRITES to it
  // (a manual pick is recorded with the context it was made in). Optional:
  // with none set the composer simply does not learn.
  void setPreferences(std::shared_ptr<UserPreferences> preferences) {
    preferences_ = std::move(preferences);
  }
  const std::shared_ptr<UserPreferences>& preferences() const {
    return preferences_;
  }

  // Called after a manual pick has been written into the store, with
  // (context, reading, value) for the most specific context recorded. The
  // shell uses it to persist the file; the CLI prints it.
  std::function<void(const std::string&, const std::string&,
                     const std::string&)>
      onLearned;

 private:
  // Finalizes the pending syllable as tone-less (tone 1/neutral). This
  // happens EAGERLY the moment the second key completes a syllable so the
  // sentence walk sees it (the character itself stays hidden behind its
  // bopomofo until settled); a following tone digit then *retrofits* the
  // tone onto that syllable.
  // Returns false if no dictionary entry accepts the tone-less reading (the
  // syllable then stays pending, shown as bopomofo, awaiting a tone digit).
  bool finalizePendingBare();
  // Applies a tone digit ('1'..'5') to the still-pending syllable: the path
  // taken when no tone-less entry exists (ㄗㄨㄟˋ) and the path a lone first
  // key takes (ㄗ -> 字), since those never enter the grid on their own.
  bool applyToneToPending(char digit);
  // Replaces the unsettled bare syllable in the grid with its toned
  // reading, settling it. A tone with no dictionary entry leaves the
  // syllable bare and still unsettled, so another tone digit can be tried.
  bool applyToneToUnsettled(char digit);
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
  // Opens the candidate menu at the cursor span (digit 8).
  Result openCandidateMenu();
  // Clears menu state and returns to kComposing.
  void dismissMenu();
  // Selects by page-relative index ('1'..'6'); out of range is a no-op.
  Result selectOnCurrentPage(size_t indexInPage);
  // Re-pins every position OUTSIDE [from, to) whose character changed since
  // `before`, so that fixing one word leaves the rest of the sentence
  // exactly as it was (2026-08-09). Re-walks after each pin.
  void restoreCharactersOutside(const std::vector<std::string>& before,
                                size_t from, size_t to);
  // Writes a manual pick into the store under both context lengths.
  void learnFromSelection(
      const Formosa::Gramambular2::ReadingGrid::Candidate& chosen);
  // Applies every learned correction that matches the current walk,
  // repeating until nothing more changes. Each override protects the rest
  // of the sentence exactly like a manual pick does.
  void applyLearnedOverrides();
  // One pass of the above. Returns true if it changed anything.
  bool applyOneLearnedOverride();
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

  // Corrections learned from manual selections, applied as high-score node
  // overrides after every walk. Shared with the shell, which persists it.
  std::shared_ptr<UserPreferences> preferences_;

  // The most recent syllable, already in the grid (so the sentence walk
  // sees it and the preceding text keeps auto-correcting) but still shown
  // as bopomofo rather than as the character it converts to. A tone digit,
  // Space, punctuation, the next syllable's first key, a cursor move or the
  // candidate menu SETTLES it, which is when the character finally appears.
  struct Unsettled {
    bool active = false;
    // Tone-less readings of the syllable, best first -- what a tone mark
    // gets appended to.
    std::vector<std::string> syllables;
    // Displayed bopomofo; identical to the tone-less reading now in the
    // grid (ㄏㄠ).
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
