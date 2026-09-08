// See ModeIndicator.h.

#include "Private.h"
#include "Globals.h"
#include "Define.h"
#include "ModeIndicator.h"

namespace
{
// Authored at 96 dpi; every metric below is scaled to the caret's monitor.
//
// Small on purpose: this is a glance, not a dialog. One glyph with just
// enough air around it to read as a card rather than as stray text.
const int   kCardWidth     = 28;
const int   kCardHeight    = 24;
const int   kCornerRadius  = 6;
const int   kCaretGap      = 5;    // between the caret and the top of the card
const int   kFontPointSize = 11;

// Held fully opaque, then faded. The whole thing is over in well under a
// second: long enough to read one character out of the corner of the eye,
// short enough that it never reads as something the user has to dismiss.
const UINT  kHoldMs        = 550;
const UINT  kFadeStepMs    = 25;
const BYTE  kFadeStep      = 24;

const UINT_PTR kTimerHold  = 1;
const UINT_PTR kTimerFade  = 2;

const WCHAR kClassName[] = L"MspyModeIndicator";

ATOM g_classAtom = 0;
}

//+---------------------------------------------------------------------------
//
// _EnsureWindowClass
//
// Registered once per module. RegisterClassEx is process-wide, so a second
// TIP instance on another thread finds it already there.
//----------------------------------------------------------------------------

/* static */
BOOL CModeIndicator::_EnsureWindowClass()
{
    if (g_classAtom != 0)
    {
        return TRUE;
    }

    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(wc);
    // CS_DROPSHADOW is what lifts the card off the text behind it; the
    // shadow follows the rounded region set in Flash(), so it does not
    // outline a square.
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_IME | CS_DROPSHADOW;
    wc.lpfnWndProc   = CModeIndicator::_WindowProc;
    wc.hInstance     = Global::dllInstanceHandle;
    wc.hCursor       = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;

    g_classAtom = RegisterClassEx(&wc);
    if (g_classAtom == 0 && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
    {
        // Another instance in this process won the race; the class is there,
        // which is all the caller needs. Remember a non-zero atom so we stop
        // asking.
        g_classAtom = static_cast<ATOM>(-1);
    }
    return g_classAtom != 0;
}

CModeIndicator::~CModeIndicator()
{
    Destroy();
}

void CModeIndicator::Destroy()
{
    if (_wndHandle != nullptr)
    {
        KillTimer(_wndHandle, kTimerHold);
        KillTimer(_wndHandle, kTimerFade);
        DestroyWindow(_wndHandle);
        _wndHandle = nullptr;
    }
    if (_font != nullptr)
    {
        DeleteObject(_font);
        _font = nullptr;
    }
    _dpi = 0;
}

//+---------------------------------------------------------------------------
//
// _EnsureWindow
//
// WS_EX_NOACTIVATE keeps the focus where it is -- this window appears while
// the user is clicking into a text field, and stealing the click would be a
// far worse bug than the one it is here to fix. WS_EX_TRANSPARENT keeps it
// out of the way of the mouse for the same reason.
//----------------------------------------------------------------------------

BOOL CModeIndicator::_EnsureWindow()
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
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        kClassName, nullptr, WS_POPUP,
        0, 0, kCardWidth, kCardHeight,
        nullptr, nullptr, Global::dllInstanceHandle, this);

    return _wndHandle != nullptr;
}

//+---------------------------------------------------------------------------
//
// _UpdateMetricsForDpi
//
// Same problem the candidate window has: the font handle and every pixel
// metric belong to one monitor's dpi, and a per-monitor-aware host reports
// 96 for the process. Recomputed from the window's own monitor each time it
// is shown, which is also what makes a card moved between monitors correct.
//----------------------------------------------------------------------------

void CModeIndicator::_UpdateMetricsForDpi()
{
    UINT dpi = 0;
    if (_wndHandle != nullptr)
    {
        dpi = GetDpiForWindow(_wndHandle);
    }
    if (dpi == 0)
    {
        HDC dcHandle = GetDC(nullptr);
        if (dcHandle != nullptr)
        {
            dpi = (UINT)GetDeviceCaps(dcHandle, LOGPIXELSY);
            ReleaseDC(nullptr, dcHandle);
        }
    }
    if (dpi == 0)
    {
        dpi = 96;
    }
    if (dpi == _dpi && _font != nullptr)
    {
        return;
    }

    _dpi = dpi;
    if (_font != nullptr)
    {
        DeleteObject(_font);
        _font = nullptr;
    }

    // CLEARTYPE_QUALITY rather than the candidate window's DEFAULT_QUALITY:
    // this card is one glyph at a small size, so the difference is the whole
    // difference between crisp and mushy. Subpixel rendering survives the
    // layered window because LWA_ALPHA composites an already-drawn opaque
    // bitmap.
    const int fontHeight = -MulDiv(kFontPointSize, (int)dpi, 72);
    _font = CreateFont(fontHeight, 0, 0, 0, FW_MEDIUM, 0, 0, 0, 0, 0, 0,
                       CLEARTYPE_QUALITY, 0, SAMPLEIME_FONT_DEFAULT);
    if (_font == nullptr)
    {
        LOGFONT lf = {};
        SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0);
        _font = CreateFont(fontHeight, 0, 0, 0, FW_MEDIUM, 0, 0, 0, 0, 0, 0,
                           CLEARTYPE_QUALITY, 0, lf.lfFaceName);
    }

    _cardWidth   = MulDiv(kCardWidth, (int)dpi, 96);
    _cardHeight  = MulDiv(kCardHeight, (int)dpi, 96);
    _borderWidth = max(1, MulDiv(1, (int)dpi, 96));
}

//+---------------------------------------------------------------------------
//
// Flash
//
//----------------------------------------------------------------------------

void CModeIndicator::Flash(BOOL isChinese, POINT ptScreen)
{
    if (!_EnsureWindow())
    {
        return;
    }

    _isChinese = isChinese;

    // Nothing stale may reach the screen, not even for one frame.
    //
    // Moving or resizing a layered window that is still VISIBLE blits
    // whatever it last painted to the new position, and WM_PAINT only
    // arrives afterwards -- which is how the PREVIOUS glyph was visible for
    // a frame or two at the start of a new flash: press Shift, see 英 jump
    // to the new spot, then turn into 中.
    //
    // So the window is unmapped and taken to alpha 0 before anything is
    // moved, and only comes back to opaque after WM_PAINT has run (below).
    // Two belts: SW_HIDE means there is no composited surface to present,
    // and alpha 0 means it would not matter if there were.
    KillTimer(_wndHandle, kTimerHold);
    KillTimer(_wndHandle, kTimerFade);
    ShowWindow(_wndHandle, SW_HIDE);
    SetLayeredWindowAttributes(_wndHandle, 0, 0, LWA_ALPHA);

    // Position first, then measure: GetDpiForWindow answers for the monitor
    // the window is currently on, so it has to be moved before the metrics
    // are recomputed or a second monitor gets the first one's sizes.
    SetWindowPos(_wndHandle, HWND_TOPMOST, ptScreen.x, ptScreen.y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
    _UpdateMetricsForDpi();

    const int gap = MulDiv(kCaretGap, (int)_dpi, 96);
    RECT work = {};
    HMONITOR monitor = MonitorFromPoint(ptScreen, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (monitor != nullptr && GetMonitorInfo(monitor, &mi))
    {
        work = mi.rcWork;
    }

    int x = ptScreen.x;
    int y = ptScreen.y + gap;
    if (work.right > work.left)
    {
        if (x + _cardWidth > work.right)  x = work.right - _cardWidth;
        if (x < work.left)                x = work.left;
        // No room below the caret: sit above it instead, the way the
        // candidate window flips (CalcFitPointAroundTextExtent).
        if (y + _cardHeight > work.bottom) y = ptScreen.y - gap - _cardHeight;
        if (y < work.top)                  y = work.top;
    }

    SetWindowPos(_wndHandle, HWND_TOPMOST, x, y, _cardWidth, _cardHeight,
                 SWP_NOACTIVATE);

    // Rounded corners come from the window region: the card is drawn opaque
    // and the region is what makes the corners actually absent, so they stay
    // clean over any background.
    HRGN region = CreateRoundRectRgn(0, 0, _cardWidth + 1, _cardHeight + 1,
                                     MulDiv(kCornerRadius, (int)_dpi, 96) * 2,
                                     MulDiv(kCornerRadius, (int)_dpi, 96) * 2);
    if (region != nullptr)
    {
        // The window takes ownership of the region.
        SetWindowRgn(_wndHandle, region, FALSE);
    }

    // Map it while still fully transparent and force the repaint through
    // synchronously: UpdateWindow dispatches WM_PAINT before returning, so
    // by the time the alpha goes back up the surface holds THIS flash's
    // glyph and nothing else.
    InvalidateRect(_wndHandle, nullptr, TRUE);
    ShowWindow(_wndHandle, SW_SHOWNOACTIVATE);
    UpdateWindow(_wndHandle);

    _alpha = 255;
    SetLayeredWindowAttributes(_wndHandle, 0, _alpha, LWA_ALPHA);
    SetTimer(_wndHandle, kTimerHold, kHoldMs, nullptr);
}

void CModeIndicator::_Hide()
{
    if (_wndHandle == nullptr)
    {
        return;
    }
    KillTimer(_wndHandle, kTimerHold);
    KillTimer(_wndHandle, kTimerFade);
    ShowWindow(_wndHandle, SW_HIDE);
}

void CModeIndicator::_OnTimer(UINT_PTR timerId)
{
    if (_wndHandle == nullptr)
    {
        return;
    }
    if (timerId == kTimerHold)
    {
        KillTimer(_wndHandle, kTimerHold);
        SetTimer(_wndHandle, kTimerFade, kFadeStepMs, nullptr);
        return;
    }
    if (timerId != kTimerFade)
    {
        return;
    }
    if (_alpha <= kFadeStep)
    {
        _Hide();
        return;
    }
    _alpha = (BYTE)(_alpha - kFadeStep);
    SetLayeredWindowAttributes(_wndHandle, 0, _alpha, LWA_ALPHA);
}

//+---------------------------------------------------------------------------
//
// _OnPaint
//
// Same light card as the candidate window (Define.h), so the two pieces of
// chrome this IME puts on screen look like they belong together.
//----------------------------------------------------------------------------

void CModeIndicator::_OnPaint(_In_ HDC dcHandle)
{
    RECT rc = {};
    GetClientRect(_wndHandle, &rc);

    HBRUSH background = CreateSolidBrush(CANDWND_BK_COLOR);
    if (background != nullptr)
    {
        FillRect(dcHandle, &rc, background);
        DeleteObject(background);
    }

    HPEN pen = CreatePen(PS_SOLID, _borderWidth, CANDWND_BORDER_COLOR);
    if (pen != nullptr)
    {
        HGDIOBJ oldPen = SelectObject(dcHandle, pen);
        HGDIOBJ oldBrush = SelectObject(dcHandle, GetStockObject(NULL_BRUSH));
        const int radius = MulDiv(kCornerRadius, (int)_dpi, 96) * 2;
        RoundRect(dcHandle, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
        SelectObject(dcHandle, oldBrush);
        SelectObject(dcHandle, oldPen);
        DeleteObject(pen);
    }

    HGDIOBJ oldFont = SelectObject(dcHandle, _font);
    SetBkMode(dcHandle, TRANSPARENT);
    SetTextColor(dcHandle, CANDWND_ITEM_COLOR);
    const WCHAR* label = _isChinese ? L"中" : L"英";
    DrawText(dcHandle, label, 1, &rc,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dcHandle, oldFont);
}

/* static */
LRESULT CALLBACK CModeIndicator::_WindowProc(HWND wndHandle, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CModeIndicator* self =
        reinterpret_cast<CModeIndicator*>(GetWindowLongPtr(wndHandle, GWLP_USERDATA));

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

    case WM_TIMER:
        if (self != nullptr)
        {
            self->_OnTimer((UINT_PTR)wParam);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;  // _OnPaint fills the whole client area

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_DPICHANGED:
        if (self != nullptr)
        {
            self->_dpi = 0;  // force a recompute on the next Flash
        }
        break;

    default:
        break;
    }

    return DefWindowProc(wndHandle, uMsg, wParam, lParam);
}
