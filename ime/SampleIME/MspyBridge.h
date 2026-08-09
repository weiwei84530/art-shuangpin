// [MspyIME] Bridge between the TSF shell and the mspy input core.
//
// Owns the conversion engine stack (McBopomofoLM -> RelaxedToneLM ->
// mspy::Composer) and provides stable UTF-16 snapshots
// of the composer's output for TSF (CStringRange does not own memory, so
// the shell points into the strings cached here).

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "composer.h"
#include "user_preferences.h"

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

private:
    // Loads %APPDATA%\MspyIME\user-choices.txt into _preferences. A
    // user-phrases.txt left by a pre-2026-08-09 build is renamed aside: it
    // records no context, which the contextual store cannot invent.
    void LoadPreferences();
    // Merges _preferences with whatever is on disk now (another
    // application's TIP instance may have written since we loaded) and
    // rewrites the file atomically.
    void SavePreferences();

    std::shared_ptr<McBopomofo::McBopomofoLM> _lm;
    std::shared_ptr<mspy::RelaxedToneLM> _relaxed;
    std::shared_ptr<mspy::UserPreferences> _preferences;
    std::unique_ptr<mspy::Composer> _composer;

    std::wstring _userChoicesPath;
    Segments _segments;
    std::vector<std::wstring> _candidateTexts;
    BOOL _ready = FALSE;
};
