// Reconversion session: homophone candidates for text that is ALREADY in
// the document (committed by any IME). Driven by a reverse lookup
// (value -> readings) injected by the shell, plus the LanguageModel's
// forward lookup (reading -> unigrams); this keeps the engine behind the
// core's interfaces (no McBopomofo headers here).
//
// Contract (spec §6 重選字): the caller hands over 1..3 UTF-8 code points,
// rightmost = the anchor character; candidate spans are right-aligned at
// the anchor, longest span first, so picking a length-k candidate replaces
// the last k code points. Menu keys mirror the composer's Selecting state:
// 1-6 select on the current page, 7/8 page (no wrap), anything else
// dismisses.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gramambular2/language_model.h"

namespace mspy {

class Reconverter {
 public:
  // Keep in sync with Composer::kCandidatePageSize.
  static constexpr size_t kPageSize = 6;

  struct FoundReading {
    std::string reading;
    double score = 0;
  };
  // All readings of a value ("行" -> ㄒㄧㄥˊ and ㄏㄤˊ); empty when the
  // value is unknown. Multi-code-point values return joined reading keys.
  using ReverseLookup =
      std::function<std::vector<FoundReading>(const std::string& value)>;

  struct Candidate {
    std::string value;
    // Code points replaced, right-aligned at the anchor.
    size_t spanLength = 0;
    double score = 0;
  };

  Reconverter(std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm,
              ReverseLookup reverseLookup);

  // contextCps: 1..3 UTF-8 code points, rightmost element is the anchor.
  // The caller has already validated Han-ness and truncated at non-Han.
  // Returns false (and stays inactive) when no candidate survives the
  // self-text filter.
  bool start(const std::vector<std::string>& contextCps);
  bool active() const { return active_; }
  void dismiss();

  const std::vector<Candidate>& candidates() const { return candidates_; }
  size_t pageIndex() const { return pageIndex_; }
  size_t pageCount() const;
  // The slice of candidates() visible on the current page (at most
  // kPageSize entries).
  std::vector<Candidate> currentPageCandidates() const;

  enum class Action { kNone, kPageChanged, kSelected, kDismissed };
  struct KeyResult {
    Action action = Action::kNone;
    Candidate selected;  // valid when action == kSelected
  };
  // '1'-'6' select on the current page (out-of-page numbers are no-ops),
  // '7'/'8' page with no wrap, any other key dismisses the session.
  KeyResult feedKey(char c);

 private:
  std::shared_ptr<Formosa::Gramambular2::LanguageModel> lm_;
  ReverseLookup reverseLookup_;

  bool active_ = false;
  std::vector<Candidate> candidates_;
  size_t pageIndex_ = 0;
};

}  // namespace mspy
