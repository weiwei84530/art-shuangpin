// [MspyIME] Bridge between the TSF shell and the mspy input core.
//
// Owns the conversion engine stack (McBopomofoLM -> RelaxedToneLM ->
// mspy::Composer) and provides stable UTF-16 snapshots of the composer's
// output for TSF (CStringRange does not own memory, so the shell points
// into the strings cached here).

#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "composer.h"
#include "reconverter.h"

namespace McBopomofo {
class McBopomofoLM;
}

class CMspyBridge
{
public:
    CMspyBridge();
    ~CMspyBridge();

    // Loads mspy-data.txt from the directory containing this DLL.
    BOOL Initialize();
    BOOL IsReady() const { return _ready; }

    mspy::Composer* Composer() { return _composer.get(); }
    // Reconversion session for committed document text (idle digit 8).
    mspy::Reconverter* Reconverter() { return _reconverter.get(); }

    static std::wstring ToWide(const std::string& utf8);
    static std::string ToUtf8(const std::wstring& wide);

    // UTF-16 mirror of mspy::Composer::DisplaySegments; the caret sits
    // after `unconfirmed`, `highlighted` is the selection-anchor char.
    struct Segments
    {
        std::wstring before;
        std::wstring unconfirmed;
        std::wstring highlighted;
        std::wstring after;
        std::wstring FullText() const
        {
            return before + unconfirmed + highlighted + after;
        }
    };
    // Refreshes and returns the current display segments.
    const Segments& GetSegments();
    // Refreshes and returns the candidate strings of the CURRENT MENU PAGE
    // (at most Composer::kCandidatePageSize entries, Selecting state).
    const std::vector<std::wstring>& CandidateTexts();
    // Same, for the reconversion session's current page.
    const std::vector<std::wstring>& ReconversionPageTexts();

private:
    // Persists a manually selected multi-syllable phrase and reloads the
    // user-phrase LM. Single characters rely on the in-session
    // UserOverrideModel instead (a permanent score-0 entry would
    // steamroll the dictionary ranking).
    void PersistUserPhrase(const std::string& reading, const std::string& value);

    std::shared_ptr<McBopomofo::McBopomofoLM> _lm;
    std::shared_ptr<mspy::RelaxedToneLM> _relaxed;
    std::unique_ptr<mspy::Composer> _composer;
    std::unique_ptr<mspy::Reconverter> _reconverter;
    std::wstring _userPhrasesPath;
    std::set<std::string> _userPhraseLines;
    Segments _segments;
    std::vector<std::wstring> _candidateTexts;
    std::vector<std::wstring> _reconvTexts;
    BOOL _ready = FALSE;
};
