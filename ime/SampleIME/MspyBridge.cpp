// [MspyIME]

#include "Private.h"
#include "Globals.h"
#include "MspyBridge.h"

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
    _ready = TRUE;
    Global::DebugLog(L"MspyBridge: loaded %s", path.c_str());
    return TRUE;
}

const std::wstring& CMspyBridge::ComposedText()
{
    _composedText = _composer ? ToWide(_composer->composedText()) : L"";
    return _composedText;
}

const std::wstring& CMspyBridge::UnconfirmedTail()
{
    _unconfirmedTail = _composer ? ToWide(_composer->unconfirmedTail()) : L"";
    return _unconfirmedTail;
}

const std::vector<std::wstring>& CMspyBridge::CandidateTexts()
{
    _candidateTexts.clear();
    if (_composer)
    {
        for (const auto& c : _composer->candidates())
        {
            _candidateTexts.push_back(ToWide(c.value));
        }
    }
    return _candidateTexts;
}
