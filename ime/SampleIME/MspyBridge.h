// [MspyIME] Bridge between the TSF shell and the mspy input core.
//
// Owns the conversion engine stack (McBopomofoLM -> RelaxedToneLM ->
// UserPreferenceLM -> mspy::Composer) and provides stable UTF-16 snapshots
// of the composer's output for TSF (CStringRange does not own memory, so
// the shell points into the strings cached here).

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "composer.h"
#include "user_preference_lm.h"
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
    // Loads %APPDATA%\MspyIME\user-phrases.txt into _preferences, migrating
    // the old two-field format and dropping keys the old append-only store
    // left with two competing values (see UserPreferences).
    void LoadPreferences();
    // Merges _preferences with whatever is on disk now (another
    // application's TIP instance may have written since we loaded) and
    // rewrites the file atomically.
    void SavePreferences();

    std::shared_ptr<McBopomofo::McBopomofoLM> _lm;
    std::shared_ptr<mspy::RelaxedToneLM> _relaxed;
    std::shared_ptr<mspy::UserPreferences> _preferences;
    std::shared_ptr<mspy::UserPreferenceLM> _preferenceLm;
    std::unique_ptr<mspy::Composer> _composer;
    // Usage refreshes fire on every commit; only write the file this often.
    static constexpr int64_t kSaveThrottleSeconds = 120;

    std::wstring _userPhrasesPath;
    int64_t _lastSaveTime = 0;
    Segments _segments;
    std::vector<std::wstring> _candidateTexts;
    BOOL _ready = FALSE;
};
