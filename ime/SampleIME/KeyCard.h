// [MspyIME] The keyboard reminder card (2026-10-07).
//
// `|` (Shift+\) in Chinese mode puts up a picture of the keyboard with every
// key's initial and finals on it -- the same annotations as the tutorial
// site's keyboard (web/tutorials.js) -- for the moment you have forgotten
// where ㄩㄥ lives. Any other key takes it down again and then does its own
// job, so looking something up never costs a keystroke; `|` itself toggles.
//
// `|` was chosen because Chinese mode already eats it without output (spec
// §6: symbols with no full-width mapping are swallowed), so claiming it
// takes nothing away. English mode still types it.
//
// macOS has the same card in mac/src/ArtKeyCard.mm.
//
// Lifetime and threading: one instance per CSampleIME, like CModeIndicator.

#pragma once

class CKeyCard
{
public:
    CKeyCard() = default;
    ~CKeyCard();

    // Centred on the work area of the monitor holding `anchor`'s window.
    void Show(HWND anchor);
    void Hide();
    BOOL IsVisible() const;

    void Destroy();

private:
    static LRESULT CALLBACK _WindowProc(HWND wndHandle, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static BOOL _EnsureWindowClass();

    BOOL _EnsureWindow();
    void _UpdateMetricsForDpi();
    void _OnPaint(_In_ HDC dcHandle);
    void _DeleteFonts();
    int  _Scale(int px) const { return MulDiv(px, (int)_dpi, 96); }

    HWND  _wndHandle = nullptr;
    HFONT _letterFont = nullptr;
    HFONT _zhuyinFont = nullptr;
    HFONT _vowelFont = nullptr;
    HFONT _titleFont = nullptr;
    HFONT _functionFont = nullptr;
    HFONT _toneFont = nullptr;
    UINT  _dpi = 0;
    int   _cardWidth = 0;
    int   _cardHeight = 0;
};
