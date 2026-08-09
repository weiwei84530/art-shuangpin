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

    // Learned corrections are applied by the composer itself, as node
    // overrides on the reading grid rather than as scores, so no language
    // model layer sits above tone relaxation any more. McBopomofoLM's own
    // user-phrase slot stays empty: it scores every entry at 0, which is
    // what used to let one stale pick own a reading forever.
    _relaxed = std::make_shared<mspy::RelaxedToneLM>(_lm);
    _preferences = std::make_shared<mspy::UserPreferences>();
    LoadPreferences();
    _composer = std::make_unique<mspy::Composer>(_relaxed);
    _composer->setPreferences(_preferences);

    // Corrections are rare (a keypress or two per sentence at worst) and
    // must survive a crash, so each one is written out immediately.
    _composer->onLearned = [this](const std::string&, const std::string&,
                                  const std::string&) { SavePreferences(); };

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
    _userChoicesPath = dir + L"\\user-choices.txt";

    // The pre-2026-08-09 store recorded no context, so nothing in it can be
    // turned into a contextual record. Move it aside rather than delete it:
    // it is the only copy of what the user taught the old build.
    const std::wstring legacyPath = dir + L"\\user-phrases.txt";
    if (GetFileAttributesW(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        const std::wstring parked = legacyPath + L".bak";
        if (MoveFileExW(legacyPath.c_str(), parked.c_str(),
                        MOVEFILE_REPLACE_EXISTING))
        {
            Global::DebugLog(L"MspyBridge: parked the old preference file at %s",
                             parked.c_str());
        }
    }

    std::ifstream in(_userChoicesPath.c_str(), std::ios::binary);
    if (!in.is_open()) return;
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    in.close();
    _preferences->loadFromText(text);
}

void CMspyBridge::SavePreferences()
{
    if (_userChoicesPath.empty() || !_preferences->dirty()) return;

    // Every application hosts its own TIP instance with its own copy, so
    // fold in whatever is on disk now before rewriting the whole file.
    std::ifstream in(_userChoicesPath.c_str(), std::ios::binary);
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
    const std::wstring temp = _userChoicesPath + L".tmp";
    {
        std::ofstream out(temp.c_str(), std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return;
        out << _preferences->serialize();
        if (!out.good()) return;
    }
    if (MoveFileExW(temp.c_str(), _userChoicesPath.c_str(),
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
