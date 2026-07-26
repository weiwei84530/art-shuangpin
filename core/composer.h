// Modal composition engine: owns the reading grid, the pending syllable and
// the Composing/Selecting state machine. Deliberately TSF-free: the shell
// (or the CLI REPL) feeds abstract keys and renders the outputs.
//
// Behavior contract: docs/spec.md §6.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "gramambular2/reading_grid.h"
#include "relaxed_tone_lm.h"
#include "syllable_input.h"

namespace mspy {

class Composer {
 public:
  enum class State { kEmpty, kComposing, kSelecting };

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

  // Inline composition text: walked sentence followed by the pending
  // syllable's display (e.g. 我喜歡ㄋ while typing ㄋㄧˇ).
  std::string composedText() const;

  // Display decomposition of composedText():
  //   before      tone-settled text left of the active area
  //   unconfirmed the pending syllable display plus, if the syllable just
  //               inserted is still tone-retrofittable, its character
  //   after       tone-settled text right of the caret (cursor movement)
  // The caret sits between `unconfirmed` and `after`. The shell renders
  // `unconfirmed` with the "input" attribute (blue) and the rest as
  // "converted" (black); the whole string stays underlined until commit.
  struct DisplaySegments {
    std::string before;
    std::string unconfirmed;
    std::string after;
  };
  DisplaySegments displaySegments() const;

  // Convenience for tests: displaySegments().unconfirmed.
  std::string unconfirmedTail() const;

  // Cursor movement inside the composition (MS-Bopomofo style).
  Result feedLeft();
  Result feedRight();

  // Candidates of the span being selected (valid in kSelecting).
  const std::vector<Formosa::Gramambular2::ReadingGrid::Candidate>&
  candidates() const {
    return candidates_;
  }

  // Side-effect-free preview of feedChar's consumption decision. TSF calls
  // OnTestKeyDown before OnKeyDown, so the eat/pass decision must not
  // mutate state.
  bool wouldConsume(char c) const;

  // Feeds a printable character (letters, ';', digits, space, ',', '.').
  Result feedChar(char c);
  Result feedBackspace();
  Result feedEnter();
  Result feedEsc();
  Result feedDown();
  Result feedUp();

  // Selects a candidate by index into candidates() (kSelecting only).
  Result selectCandidate(size_t index);

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
  // Commits the current buffer (dropping a half-typed syllable) and resets.
  std::string takeCommitText();
  void reset();
  void updateStateAfterMutation();

  std::shared_ptr<RelaxedToneLM> lm_;
  Formosa::Gramambular2::ReadingGrid grid_;
  Formosa::Gramambular2::ReadingGrid::WalkResult walk_;
  SyllableInput pending_;
  State state_ = State::kEmpty;

  // True when the most recent grid mutation was an eager bare insert, i.e.
  // a tone digit may still retrofit it.
  bool lastWasBare_ = false;
  std::vector<std::string> lastBareSyllables_;

  // Paired-quote alternation (Rime-style): next " types “ or ”.
  bool doubleQuoteOpen_ = false;
  bool singleQuoteOpen_ = false;

  // Selection state.
  size_t selectionLocation_ = 0;
  std::vector<Formosa::Gramambular2::ReadingGrid::Candidate> candidates_;
};

}  // namespace mspy
