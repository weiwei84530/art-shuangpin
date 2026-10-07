// See KeyCard.h.

#include "Private.h"
#include "Globals.h"
#include "Define.h"
#include "KeyCard.h"

namespace
{
// One keycap's annotations. Mirrors web/tutorials.js (INITIALS,
// SINGLE_VOWELS, FINALS, CONTROLS) and mac/src/ArtKeyCard.mm; change all
// three together.
struct KeyCap
{
    WCHAR        label;
    const WCHAR* initial;      // orange, top right
    const WCHAR* vowel;        // small, under the initial: the key alone as a syllable
    const WCHAR* finals[3];    // teal, bottom right, top to bottom
    const WCHAR* function;     // slate, bottom left: what the key does
    const WCHAR* tone;         // purple, top right where an initial would be:
                               // the tone mark, or the full-width punctuation
};

// The digit row has two jobs (spec §6): a tone key while a syllable is
// still bopomofo, and an editing key otherwise -- the idle editing layer, or
// the cursor/menu keys inside a composition. The function label is the
// shortest wording that covers both.
const KeyCap kRow0[] = {
    { L'1', nullptr, nullptr, {}, L"行首",     L"ˉ" },
    { L'2', nullptr, nullptr, {}, L"選到行首", L"ˊ" },
    { L'3', nullptr, nullptr, {}, L"選到行尾", L"ˇ" },
    { L'4', nullptr, nullptr, {}, L"行尾",     L"ˋ" },
    { L'5', nullptr, nullptr, {}, L"⌦ 刪除",   L"˙" },
    { L'6', nullptr, nullptr, {}, L"⌫ 退格",   nullptr },
    { L'7', nullptr, nullptr, {}, L"↑ 上頁",   L"ˋ" },
    { L'8', nullptr, nullptr, {}, L"↓ 選字",   L"ˇ" },
    { L'9', nullptr, nullptr, {}, L"← 左移",   L"ˊ" },
    { L'0', nullptr, nullptr, {}, L"→ 右移",   L"ˉ" },
};

const KeyCap kRow1[] = {
    { L'Q', L"ㄑ", L"ㄧ", { L"ㄧㄡ" }, nullptr },
    { L'W', L"ㄨ", nullptr, { L"ㄧㄚ", L"ㄨㄚ" }, nullptr },
    { L'E', nullptr, nullptr, { L"ㄜ" }, nullptr },
    { L'R', L"ㄖ", nullptr, { L"ㄦ", L"ㄨㄢ", L"ㄩㄢ" }, nullptr },
    { L'T', L"ㄊ", L"ㄜ", { L"ㄩㄝ" }, nullptr },
    { L'Y', L"ㄧ", nullptr, { L"ㄨㄞ", L"ㄩ" }, nullptr },
    { L'U', L"ㄕ", nullptr, { L"ㄨ", L"ㄩ" }, nullptr },
    { L'I', L"ㄔ", nullptr, { L"ㄧ" }, nullptr },
    { L'O', nullptr, nullptr, { L"ㄛ", L"ㄨㄛ" }, nullptr },
    { L'P', L"ㄆ", L"ㄛ", { L"ㄨㄣ", L"ㄩㄣ" }, nullptr },
};

const KeyCap kRow2[] = {
    { L'A', nullptr, nullptr, { L"ㄚ" }, nullptr },
    { L'S', L"ㄙ", nullptr, { L"ㄨㄥ", L"ㄩㄥ" }, nullptr },
    { L'D', L"ㄉ", L"ㄜ", { L"ㄧㄤ", L"ㄨㄤ" }, nullptr },
    { L'F', L"ㄈ", L"ㄛ", { L"ㄣ" }, nullptr },
    { L'G', L"ㄍ", L"ㄜ", { L"ㄥ" }, nullptr },
    { L'H', L"ㄏ", L"ㄜ", { L"ㄤ" }, nullptr },
    { L'J', L"ㄐ", L"ㄧ", { L"ㄢ" }, nullptr },
    { L'K', L"ㄎ", L"ㄜ", { L"ㄠ" }, nullptr },
    { L'L', L"ㄌ", L"ㄜ", { L"ㄞ" }, nullptr },
    { L';', nullptr, nullptr, { L"ㄧㄥ" }, nullptr },
};

const KeyCap kRow3[] = {
    { L'Z', L"ㄗ", nullptr, { L"ㄟ" }, nullptr },
    { L'X', L"ㄒ", L"ㄧ", { L"ㄧㄝ" }, nullptr },
    { L'C', L"ㄘ", nullptr, { L"ㄧㄠ" }, nullptr },
    { L'V', L"ㄓ", nullptr, { L"ㄨㄟ", L"ㄩㄝ" }, nullptr },
    { L'B', L"ㄅ", L"ㄛ", { L"ㄡ" }, nullptr },
    { L'N', L"ㄋ", L"ㄜ", { L"ㄧㄣ" }, nullptr },
    { L'M', L"ㄇ", L"ㄛ", { L"ㄧㄢ" }, nullptr },
    { L',', nullptr, nullptr, {}, nullptr, L"，" },
    { L'.', nullptr, nullptr, {}, nullptr, L"。" },
    { L'/', nullptr, nullptr, {}, nullptr, L"、" },
};

struct Row
{
    const KeyCap* keys;
    int           count;
    int           offsetQuarters;  // stagger, in quarters of a key pitch
};

const Row kRows[] = {
    { kRow0, ARRAYSIZE(kRow0), 0 },
    { kRow1, ARRAYSIZE(kRow1), 2 },
    { kRow2, ARRAYSIZE(kRow2), 3 },
    { kRow3, ARRAYSIZE(kRow3), 5 },
};

// Authored at 96 dpi.
const int kKeyWidth     = 66;
const int kKeyHeight    = 76;
const int kKeyGap       = 5;
const int kPadding      = 14;
const int kTitleHeight  = 26;
const int kCornerRadius = 10;
const int kKeyRadius    = 6;
const int kKeyInset     = 5;   // text inside a keycap
const int kZhuyinLine   = 16;

const int kLetterPt = 13;
const int kZhuyinPt = 11;
const int kVowelPt  = 9;
const int kTitlePt  = 10;
const int kFunctionPt = 8;

// Darker than the tutorial site's colours: those sit on a grey keycap, these
// on a white one, and the site's orange is unreadable on white.
const COLORREF kInitialColor  = RGB(0xC8, 0x67, 0x1A);
const COLORREF kFinalColor    = RGB(0x1E, 0x85, 0x77);
const COLORREF kToneColor     = RGB(0x7B, 0x4F, 0xC9);
const COLORREF kFunctionColor = RGB(0x5C, 0x67, 0x80);
// The recited vowel under an initial is a memory aid, not something to read
// first: a pale tint of the initial's orange, close to the keycap.
const COLORREF kVowelColor    = RGB(0xDE, 0xBF, 0xA6);
const COLORREF kKeyFillColor  = RGB(0xF6, 0xF7, 0xF9);
const COLORREF kKeyEdgeColor  = RGB(0xDD, 0xDF, 0xE4);
const COLORREF kLetterColor   = RGB(0x30, 0x30, 0x30);

const WCHAR kClassName[] = L"MspyKeyCard";

ATOM g_classAtom = 0;

int TotalColumnsQuarters()
{
    int widest = 0;
    for (const Row& row : kRows)
    {
        widest = max(widest, row.offsetQuarters + row.count * 4);
    }
    return widest;
}
}

/* static */
BOOL CKeyCard::_EnsureWindowClass()
{
    if (g_classAtom != 0)
    {
        return TRUE;
    }

    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_IME | CS_DROPSHADOW;
    wc.lpfnWndProc   = CKeyCard::_WindowProc;
    wc.hInstance     = Global::dllInstanceHandle;
    wc.hCursor       = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;

    g_classAtom = RegisterClassEx(&wc);
    if (g_classAtom == 0 && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
    {
        g_classAtom = static_cast<ATOM>(-1);
    }
    return g_classAtom != 0;
}

CKeyCard::~CKeyCard()
{
    Destroy();
}

void CKeyCard::Destroy()
{
    if (_wndHandle != nullptr)
    {
        DestroyWindow(_wndHandle);
        _wndHandle = nullptr;
    }
    _DeleteFonts();
    _dpi = 0;
}

void CKeyCard::_DeleteFonts()
{
    HFONT* const fonts[] = { &_letterFont, &_zhuyinFont, &_vowelFont, &_titleFont,
                             &_functionFont, &_toneFont };
    for (HFONT* font : fonts)
    {
        if (*font != nullptr)
        {
            DeleteObject(*font);
            *font = nullptr;
        }
    }
}

// Same window flags as the mode bubble, for the same reason: it must never
// take the focus or a click away from the text field being typed into.
BOOL CKeyCard::_EnsureWindow()
{
    if (_wndHandle != nullptr)
    {
        return TRUE;
    }
    if (!_EnsureWindowClass())
    {
        return FALSE;
    }

    _wndHandle = CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        kClassName, nullptr, WS_POPUP,
        0, 0, 1, 1,
        nullptr, nullptr, Global::dllInstanceHandle, this);

    return _wndHandle != nullptr;
}

void CKeyCard::_UpdateMetricsForDpi()
{
    UINT dpi = _wndHandle != nullptr ? GetDpiForWindow(_wndHandle) : 0;
    if (dpi == 0)
    {
        dpi = 96;
    }
    if (dpi == _dpi && _letterFont != nullptr)
    {
        return;
    }

    _dpi = dpi;
    _DeleteFonts();

    auto makeFont = [dpi](int points, int weight) -> HFONT
    {
        return CreateFont(-MulDiv(points, (int)dpi, 72), 0, 0, 0, weight, 0, 0, 0, 0, 0, 0,
                          CLEARTYPE_QUALITY, 0, SAMPLEIME_FONT_DEFAULT);
    };
    _letterFont = makeFont(kLetterPt, FW_SEMIBOLD);
    _zhuyinFont = makeFont(kZhuyinPt, FW_BOLD);
    _vowelFont  = makeFont(kVowelPt, FW_BOLD);
    _titleFont  = makeFont(kTitlePt, FW_NORMAL);
    _functionFont = makeFont(kFunctionPt, FW_NORMAL);
    _toneFont   = makeFont(kLetterPt, FW_BOLD);

    const int pitchQuarters = TotalColumnsQuarters();
    const int pitch = _Scale(kKeyWidth + kKeyGap);
    _cardWidth  = _Scale(kPadding) * 2 + MulDiv(pitch, pitchQuarters, 4) - _Scale(kKeyGap);
    _cardHeight = _Scale(kPadding) * 2 + _Scale(kTitleHeight) +
                  ARRAYSIZE(kRows) * _Scale(kKeyHeight + kKeyGap) - _Scale(kKeyGap);
}

void CKeyCard::Show(HWND anchor)
{
    if (!_EnsureWindow())
    {
        return;
    }

    HMONITOR monitor = MonitorFromWindow(anchor != nullptr ? anchor : GetForegroundWindow(),
                                         MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (monitor == nullptr || !GetMonitorInfo(monitor, &mi))
    {
        return;
    }
    const RECT& work = mi.rcWork;

    // Move onto the target monitor before measuring: GetDpiForWindow answers
    // for the monitor the window is on now (same rule as CModeIndicator).
    ShowWindow(_wndHandle, SW_HIDE);
    SetWindowPos(_wndHandle, HWND_TOPMOST, work.left, work.top, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
    _UpdateMetricsForDpi();

    const int x = work.left + ((work.right - work.left) - _cardWidth) / 2;
    const int y = work.top + ((work.bottom - work.top) - _cardHeight) / 2;
    SetWindowPos(_wndHandle, HWND_TOPMOST, x, y, _cardWidth, _cardHeight, SWP_NOACTIVATE);

    const int radius = _Scale(kCornerRadius) * 2;
    HRGN region = CreateRoundRectRgn(0, 0, _cardWidth + 1, _cardHeight + 1, radius, radius);
    if (region != nullptr)
    {
        SetWindowRgn(_wndHandle, region, FALSE);
    }

    InvalidateRect(_wndHandle, nullptr, TRUE);
    ShowWindow(_wndHandle, SW_SHOWNOACTIVATE);
    UpdateWindow(_wndHandle);
}

void CKeyCard::Hide()
{
    if (_wndHandle != nullptr)
    {
        ShowWindow(_wndHandle, SW_HIDE);
    }
}

BOOL CKeyCard::IsVisible() const
{
    return _wndHandle != nullptr && IsWindowVisible(_wndHandle);
}

void CKeyCard::_OnPaint(_In_ HDC dcHandle)
{
    RECT client = {};
    GetClientRect(_wndHandle, &client);

    // Double-buffered: a few hundred text runs drawn straight to the screen
    // visibly ripple in.
    HDC dc = CreateCompatibleDC(dcHandle);
    HBITMAP bitmap = CreateCompatibleBitmap(dcHandle, client.right, client.bottom);
    if (dc == nullptr || bitmap == nullptr)
    {
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (dc != nullptr) DeleteDC(dc);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    HBRUSH background = CreateSolidBrush(CANDWND_BK_COLOR);
    FillRect(dc, &client, background);
    DeleteObject(background);

    HPEN border = CreatePen(PS_SOLID, max(1, _Scale(1)), CANDWND_BORDER_COLOR);
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    const int cardRadius = _Scale(kCornerRadius) * 2;
    RoundRect(dc, 0, 0, client.right, client.bottom, cardRadius, cardRadius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(border);

    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, _titleFont);

    // Title line doubles as the legend.
    const int pad = _Scale(kPadding);
    RECT title = { pad, pad, client.right - pad, pad + _Scale(kTitleHeight) };
    struct Run { const WCHAR* text; COLORREF color; };
    const Run legend[] = {
        { L"鍵位提示　", CANDWND_ITEM_COLOR },
        { L"■ 聲母　", kInitialColor },
        { L"■ 韻母　", kFinalColor },
        { L"■ 聲調／符號　", kToneColor },
        { L"■ 功能", kFunctionColor },
    };
    int cursorX = title.left;
    for (const Run& run : legend)
    {
        SetTextColor(dc, run.color);
        RECT rc = { cursorX, title.top, title.right, title.bottom };
        DrawText(dc, run.text, -1, &rc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SIZE size = {};
        GetTextExtentPoint32(dc, run.text, (int)wcslen(run.text), &size);
        cursorX += size.cx;
    }
    SetTextColor(dc, CANDWND_PAGE_COLOR);
    DrawText(dc, L"按任意鍵關閉", -1, &title, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    HBRUSH keyFill = CreateSolidBrush(kKeyFillColor);
    HPEN keyEdge = CreatePen(PS_SOLID, max(1, _Scale(1)), kKeyEdgeColor);
    const int keyW = _Scale(kKeyWidth);
    const int keyH = _Scale(kKeyHeight);
    const int pitchX = _Scale(kKeyWidth + kKeyGap);
    const int pitchY = _Scale(kKeyHeight + kKeyGap);
    const int inset = _Scale(kKeyInset);
    const int line = _Scale(kZhuyinLine);
    const int keyRadius = _Scale(kKeyRadius) * 2;
    const UINT textFlags = DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP;

    for (int r = 0; r < ARRAYSIZE(kRows); ++r)
    {
        const Row& row = kRows[r];
        const int top = title.bottom + r * pitchY;
        for (int i = 0; i < row.count; ++i)
        {
            const KeyCap& cap = row.keys[i];
            const int left = pad + MulDiv(pitchX, row.offsetQuarters, 4) + i * pitchX;
            RECT key = { left, top, left + keyW, top + keyH };

            SelectObject(dc, keyFill);
            SelectObject(dc, keyEdge);
            RoundRect(dc, key.left, key.top, key.right, key.bottom, keyRadius, keyRadius);

            RECT inner = { key.left + inset, key.top + inset - _Scale(2),
                           key.right - inset, key.bottom - inset + _Scale(1) };

            SelectObject(dc, _letterFont);
            SetTextColor(dc, kLetterColor);
            WCHAR letter[2] = { cap.label, 0 };
            DrawText(dc, letter, 1, &inner, DT_LEFT | DT_TOP | textFlags);

            if (cap.initial != nullptr)
            {
                SelectObject(dc, _zhuyinFont);
                SetTextColor(dc, kInitialColor);
                RECT rc = inner;
                rc.top += _Scale(1);
                DrawText(dc, cap.initial, -1, &rc, DT_RIGHT | DT_TOP | textFlags);
                if (cap.vowel != nullptr)
                {
                    SelectObject(dc, _vowelFont);
                    SetTextColor(dc, kVowelColor);
                    rc.top += line;
                    DrawText(dc, cap.vowel, -1, &rc, DT_RIGHT | DT_TOP | textFlags);
                }
            }

            int finalsCount = 0;
            while (finalsCount < 3 && cap.finals[finalsCount] != nullptr)
            {
                ++finalsCount;
            }
            if (finalsCount > 0)
            {
                SelectObject(dc, _zhuyinFont);
                SetTextColor(dc, kFinalColor);
                for (int f = 0; f < finalsCount; ++f)
                {
                    RECT rc = inner;
                    rc.bottom -= (finalsCount - 1 - f) * line;
                    DrawText(dc, cap.finals[f], -1, &rc, DT_RIGHT | DT_BOTTOM | textFlags);
                }
            }

            if (cap.tone != nullptr)
            {
                SelectObject(dc, _toneFont);
                SetTextColor(dc, kToneColor);
                RECT rc = inner;
                rc.top += _Scale(1);
                DrawText(dc, cap.tone, -1, &rc, DT_RIGHT | DT_TOP | textFlags);
            }

            if (cap.function != nullptr)
            {
                SelectObject(dc, _functionFont);
                SetTextColor(dc, kFunctionColor);
                DrawText(dc, cap.function, -1, &inner, DT_LEFT | DT_BOTTOM | textFlags);
            }
        }
    }

    SelectObject(dc, oldFont);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(keyFill);
    DeleteObject(keyEdge);

    BitBlt(dcHandle, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

/* static */
LRESULT CALLBACK CKeyCard::_WindowProc(HWND wndHandle, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CKeyCard* self = reinterpret_cast<CKeyCard*>(GetWindowLongPtr(wndHandle, GWLP_USERDATA));

    switch (uMsg)
    {
    case WM_NCCREATE:
        {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            SetWindowLongPtr(wndHandle, GWLP_USERDATA,
                             reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        break;

    case WM_PAINT:
        if (self != nullptr)
        {
            PAINTSTRUCT ps = {};
            HDC dcHandle = BeginPaint(wndHandle, &ps);
            if (dcHandle != nullptr)
            {
                self->_OnPaint(dcHandle);
                EndPaint(wndHandle, &ps);
            }
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_DPICHANGED:
        if (self != nullptr)
        {
            self->_dpi = 0;
        }
        break;

    default:
        break;
    }

    return DefWindowProc(wndHandle, uMsg, wParam, lParam);
}
