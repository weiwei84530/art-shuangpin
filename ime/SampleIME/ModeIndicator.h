// [MspyIME] The 中/英 bubble (2026-09-08).
//
// macOS has ArtModeHUD for the same job and for the same reason: the mode is
// a property of the application you are typing into, and nothing on screen
// says which one you are in until you have already typed a character in the
// wrong script.
//
// Two moments, both of them a mode the user did not read anywhere:
//
//   * the Shift tap (_HandleShiftTap) -- the switch they just made;
//   * taking the focus (_FlashModeIndicatorForFocus) -- the switch the
//     per-application memory made for them, which nothing announces because
//     from the system's point of view nothing changed.
//
// The first version showed only the second, on the assumption that Windows'
// own mode indicator would cover the switch and two bubbles would be worse
// than none. Measured on Win 11 26200: no system indicator appears for this
// TIP, so there was nothing to defer to.
//
// Lifetime: one instance per CSampleIME, i.e. one per thread that hosts the
// TIP, so the window is always created and destroyed on its own UI thread and
// the timers need no marshalling.

#pragma once

class CModeIndicator
{
public:
    CModeIndicator() = default;
    ~CModeIndicator();

    // Shows 中 or 英 just below `ptScreen` (screen coordinates: the bottom
    // left of the caret) and fades it out. Called again before the fade
    // finishes, it restarts -- the newest state is always the visible one.
    void Flash(BOOL isChinese, POINT ptScreen);

    void Destroy();

private:
    static LRESULT CALLBACK _WindowProc(HWND wndHandle, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static BOOL _EnsureWindowClass();

    BOOL _EnsureWindow();
    void _UpdateMetricsForDpi();
    void _OnPaint(_In_ HDC dcHandle);
    void _OnTimer(UINT_PTR timerId);
    void _Hide();

    HWND  _wndHandle = nullptr;
    HFONT _font = nullptr;
    UINT  _dpi = 0;
    int   _cardWidth = 0;
    int   _cardHeight = 0;
    int   _borderWidth = 1;
    BOOL  _isChinese = TRUE;
    BYTE  _alpha = 255;
};
