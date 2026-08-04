// [MspyIME]

#include "Private.h"
#include "Globals.h"
#include "MspyBridge.h"

#include <fstream>

#include "McBopomofoLM.h"

CMspyBridge::CMspyBridge() = default;

CMspyBridge::~CMspyBridge()
{
    // Flush any usage refreshes the throttle has been holding back.
    if (_preferences != nullptr) SavePreferences();
}

std::wstring CMspyBridge::ToWide(const std::string& utf8)
{
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                  nullptr, 0);
    if (len <= 0) return {};
    std::wstring out((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                        out.data(), len);
    return out;
}

std::string CMspyBridge::ToUtf8(const std::wstring& wide)
{
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                        out.data(), len, nullptr, nullptr);
    return out;
}

BOOL CMspyBridge::Initialize()
{
    if (_ready) return TRUE;

    WCHAR wszFileName[MAX_PATH] = {L'\0'};
    DWORD cch = GetModuleFileNameW(Global::dllInstanceHandle, wszFileName,
                                   ARRAYSIZE(wszFileName));
    if (cch == 0) return FALSE;

    std::wstring path(wszFileName, cch);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return FALSE;
    path.resize(slash + 1);
    path += L"mspy-data.txt";

    _lm = std::make_shared<McBopomofo::McBopomofoLM>();
    _lm->loadLanguageModel(ToUtf8(path).c_str());
    if (!_lm->isDataModelLoaded())
    {
        Global::DebugLog(L"MspyBridge: failed to load %s", path.c_str());
        _lm.reset();
        return FALSE;
    }

    // The learned picks are applied ABOVE tone relaxation, so the layer
    // sees the final candidate list. McBopomofoLM's own user-phrase slot is
    // deliberately left empty: it scores every entry at 0, which is what
    // used to let one stale pick own a reading forever.
    _relaxed = std::make_shared<mspy::RelaxedToneLM>(_lm);
    _preferences = std::make_shared<mspy::UserPreferences>();
    LoadPreferences();
    _preferenceLm =
        std::make_shared<mspy::UserPreferenceLM>(_relaxed, _preferences);
    _composer = std::make_unique<mspy::Composer>(_preferenceLm);

    _composer->onManualSelection =
        [this](const std::string& reading, const std::string& value)
    {
        // The composer already widened single-character picks into the
        // phrase around them, so everything worth learning arrives here.
        if (reading.find('-') == std::string::npos) return;
        _preferences->record(reading, value, _preferenceLm->clock());
        SavePreferences();
    };

    _composer->onPhraseUsed =
        [this](const std::string& reading, const std::string& value)
    {
        const int64_t now = _preferenceLm->clock();
        _preferences->touch(reading, value, now);
        // Refreshes happen on every commit, so writing each one would mean
        // a file write per sentence. Throttle: a lost refresh only costs a
        // phrase a little of its remaining half-life.
        if (_preferences->dirty() && now - _lastSaveTime >= kSaveThrottleSeconds)
        {
            SavePreferences();
            _lastSaveTime = now;
        }
    };

    _ready = TRUE;
    Global::DebugLog(L"MspyBridge: loaded %s", path.c_str());
    return TRUE;
}

void CMspyBridge::LoadPreferences()
{
    WCHAR appData[MAX_PATH] = {L'\0'};
    if (ExpandEnvironmentStringsW(L"%APPDATA%", appData, ARRAYSIZE(appData)) <= 1)
    {
        return;
    }
    std::wstring dir = std::wstring(appData) + L"\\MspyIME";
    CreateDirectoryW(dir.c_str(), nullptr);
    _userPhrasesPath = dir + L"\\user-phrases.txt";

    std::ifstream in(_userPhrasesPath.c_str(), std::ios::binary);
    if (!in.is_open()) return;
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    in.close();

    // Legacy two-field lines are dated from this migration so the phrases
    // already learned keep working; they age normally from here.
    _preferences->loadFromText(
        text, mspy::UserPreferenceLM::SystemNowSeconds());

    // Files written by the old append-only store can hold two competing
    // values under one reading (the stuck first pick plus the correction
    // that could never replace it). Neither is trustworthy, so drop both
    // and let normal use relearn.
    const auto dropped = _preferences->dropAmbiguousLegacyKeys();
    for (const auto& key : dropped)
    {
        Global::DebugLog(L"MspyBridge: dropped ambiguous learned key %s",
                         ToWide(key).c_str());
    }
    if (_preferences->dirty()) SavePreferences();
}

void CMspyBridge::SavePreferences()
{
    if (_userPhrasesPath.empty() || !_preferences->dirty()) return;

    // Every application hosts its own TIP instance with its own copy, so
    // fold in whatever is on disk now before rewriting the whole file.
    std::ifstream in(_userPhrasesPath.c_str(), std::ios::binary);
    if (in.is_open())
    {
        std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        in.close();
        mspy::UserPreferences onDisk;
        onDisk.loadFromText(text);
        _preferences->mergeFrom(onDisk);
    }

    // Write to a sibling file and rename over the target, so a crash or a
    // concurrent reader never sees a half-written store.
    const std::wstring temp = _userPhrasesPath + L".tmp";
    {
        std::ofstream out(temp.c_str(), std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return;
        out << _preferences->serialize();
        if (!out.good()) return;
    }
    if (MoveFileExW(temp.c_str(), _userPhrasesPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING))
    {
        _preferences->clearDirty();
    }
    else
    {
        DeleteFileW(temp.c_str());
    }
}

const CMspyBridge::Segments& CMspyBridge::GetSegments()
{
    if (_composer)
    {
        mspy::Composer::DisplaySegments segments = _composer->displaySegments();
        _segments.before = ToWide(segments.before);
        _segments.unconfirmed = ToWide(segments.unconfirmed);
        _segments.highlighted = ToWide(segments.highlighted);
        _segments.after = ToWide(segments.after);
    }
    else
    {
        _segments = {};
    }
    return _segments;
}

const std::vector<std::wstring>& CMspyBridge::CandidateTexts()
{
    _candidateTexts.clear();
    if (_composer)
    {
        for (const auto& c : _composer->currentPageCandidates())
        {
            _candidateTexts.push_back(ToWide(c.value));
        }
    }
    return _candidateTexts;
}
