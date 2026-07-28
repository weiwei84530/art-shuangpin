// [MspyIME]

#include "Private.h"
#include "Globals.h"
#include "MspyBridge.h"

#include <fstream>

#include "McBopomofoLM.h"

CMspyBridge::CMspyBridge() = default;
CMspyBridge::~CMspyBridge() = default;

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

    _relaxed = std::make_shared<mspy::RelaxedToneLM>(_lm);
    _composer = std::make_unique<mspy::Composer>(_relaxed);

    // User phrases live under %APPDATA%\MspyIME\user-phrases.txt.
    WCHAR appData[MAX_PATH] = {L'\0'};
    if (ExpandEnvironmentStringsW(L"%APPDATA%", appData, ARRAYSIZE(appData)) > 1)
    {
        std::wstring dir = std::wstring(appData) + L"\\MspyIME";
        CreateDirectoryW(dir.c_str(), nullptr);
        _userPhrasesPath = dir + L"\\user-phrases.txt";

        std::ifstream in(_userPhrasesPath.c_str(), std::ios::binary);
        if (in.is_open())
        {
            std::string line;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) _userPhraseLines.insert(line);
            }
            in.close();
            _lm->loadUserPhrases(ToUtf8(_userPhrasesPath).c_str(), nullptr);
        }
    }

    _composer->onManualSelection =
        [this](const std::string& reading, const std::string& value)
    {
        PersistUserPhrase(reading, value);
    };

    _ready = TRUE;
    Global::DebugLog(L"MspyBridge: loaded %s", path.c_str());
    return TRUE;
}

void CMspyBridge::PersistUserPhrase(const std::string& reading, const std::string& value)
{
    if (_userPhrasesPath.empty()) return;
    // Only multi-syllable phrases; UserPhrasesLM lines are "value key".
    if (reading.find('-') == std::string::npos) return;
    std::string line = value + " " + reading;
    if (!_userPhraseLines.insert(line).second) return;  // already stored

    std::ofstream out(_userPhrasesPath.c_str(), std::ios::binary | std::ios::app);
    if (!out.is_open()) return;
    out << line << "\n";
    out.close();
    _lm->loadUserPhrases(ToUtf8(_userPhrasesPath).c_str(), nullptr);
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
