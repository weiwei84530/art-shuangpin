// Modal composition engine: owns the reading grid, the pending syllable and
// the Composing/Selecting state machine. Deliberately TSF-free: the shell
// (or the CLI REPL) feeds abstract keys and renders the outputs.
//
// Behavior contract: docs/spec.md §6.
//
// Key map while composing (digits are never typed literally mid-buffer):
//   1-5  tone digits (pending syllable or retrofit)
//   6,7  eaten, no-op
//   8    open the candidate menu at the cursor
//   9/0  move the cursor left/right, wrapping at both ends
// While the candidate menu is open:
//   1-6  pick the numbered candidate on the current page
//   7/8  previous/next page (no wrap; out-of-range is a no-op)
//   any other key closes the menu AND performs its normal function.
// A bare Shift tap toggles English mode; mid-composition it also inserts a
// half-width space at the cursor (both on entering and leaving English).

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

  explicit Composer(std::shared_ptr<RelaxedToneLM> lm);

  State state() const { return state_; }

  // English mode (toggled by a bare Shift tap). While kEmpty, every key
  // passes through to the application; while composing, printable keys are
  // inserted literally at the cursor.
  bool englishMode() const { return englishMode_; }

  // Inline composition text: walked sentence followed by the pending
  // syllable's display (e.g. 我喜歡ㄋ while typing ㄋㄧˇ).
  std::string composedText() const;

  // Display decomposition of composedText():
  //   before      tone-settled text left of the active area
  //   unconfirmed the pending syllable display plus, if the syllable just
  //               inserted is still tone-retrofittable, its character
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
  // A bare Shift tap (no other key between down and up).
  Result feedShiftTap();

  // Closes the candidate menu without touching the composition (used by the
  // shell for window-only teardown, e.g. mouse dismissal).
  Result closeCandidateMenu();
  // Unconditionally resets the composer (app-terminated composition).
  void cancel() { reset(); }

  // Selects a candidate by index into candidates() (kSelecting only).
  Result selectCandidate(size_t index);

  // Called on every manual candidate selection with (reading key, value);
  // the shell persists these as user phrases. The reading key is free of
  // internal sentinels.
  std::function<void(const std::string&, const std::string&)>
      onManualSelection;

 private:
  // Finalizes the pending complete syllable as tone-less (tone 1/neutral).
  // This happens EAGERLY the moment the second key completes a syllable, so
  // the converted character appears inline immediately ("母音一按就見字");
  // a following tone digit then *retrofits* the tone onto that syllable.
  // Returns false if no dictionary entry accepts the tone-less reading (the
  // syllable then stays pending, shown as bopomofo, awaiting a tone digit).
  bool finalizePendingBare();
  // Applies a tone digit ('1'..'5') to the still-pending complete syllable.
  bool applyToneToPending(char digit);
  // Replaces the just-inserted bare syllable with its toned reading.
  bool retrofitToneToLastSyllable(char digit);
  // Inserts a reading into the grid and re-walks.
  bool insertReading(const std::string& reading);
  // Inserts a literal character (English-mode text, auto space) at the
  // cursor as a single-candidate grid node.
  bool insertLiteral(char c);
  // Moves the cursor by delta with wrap-around at both ends.
  void moveCursor(int delta);
  // Opens the candidate menu at the cursor span (digit 8).
  Result openCandidateMenu();
  // Clears menu state and returns to kComposing.
  void dismissMenu();
  // Selects by page-relative index ('1'..'6'); out of range is a no-op.
  Result selectOnCurrentPage(size_t indexInPage);
  // Commits the current buffer (dropping a half-typed syllable) and resets.
  std::string takeCommitText();
  void reset();
  void updateStateAfterMutation();

  std::shared_ptr<RelaxedToneLM> lm_;
  Formosa::Gramambular2::ReadingGrid grid_;
  Formosa::Gramambular2::ReadingGrid::WalkResult walk_;
  SyllableInput pending_;
  State state_ = State::kEmpty;

  // Session re-ranking learned from manual selections (LRU with time
  // decay, McBopomofo's UserOverrideModel).
  McBopomofo::UserOverrideModel uom_;

  // True when the most recent grid mutation was an eager bare insert, i.e.
  // a tone digit may still retrofit it.
  bool lastWasBare_ = false;
  std::vector<std::string> lastBareSyllables_;

  // Paired-quote alternation (Rime-style): next " types “ or ”.
  bool doubleQuoteOpen_ = false;
  bool singleQuoteOpen_ = false;

  // English mode survives commits/resets; it is a device-level toggle.
  bool englishMode_ = false;

  // Selection state.
  size_t selectionLocation_ = 0;
  size_t pageIndex_ = 0;
  std::vector<Formosa::Gramambular2::ReadingGrid::Candidate> candidates_;
};

}  // namespace mspy
