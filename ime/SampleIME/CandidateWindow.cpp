// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "Private.h"
#include "Globals.h"
#include "BaseWindow.h"
#include "CandidateWindow.h"

// [MspyIME] Rounded corners on Windows 11.
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CCandidateWindow::CCandidateWindow(_In_ CANDWNDCALLBACK pfnCallback, _In_ void *pv, _In_ CCandidateRange *pIndexRange, _In_ BOOL isStoreAppMode)
{
    _currentSelection = 0;

    // [MspyIME] Modern light theme defaults.
    _SetTextColor(CANDWND_ITEM_COLOR, CANDWND_BK_COLOR);
    _SetFillColor((HBRUSH)GetStockObject(WHITE_BRUSH));

    _pIndexRange = pIndexRange;

    _pfnCallback = pfnCallback;
    _pObj = pv;

    _pShadowWnd = nullptr;

    // [MspyIME] 96 dpi defaults; _UpdateMetricsForDpi rescales them (and
    // builds the font) once the window knows which monitor it is on.
    _dpi = 0;
    _hFont = nullptr;
    _cyRow = CANDWND_ROW_WIDTH;
    _cyPageBar = CANDWND_PAGEBAR_HEIGHT;
    _cxBorder = CANDWND_BORDER_WIDTH;
    _cxTitle = 0;

    _pVScrollBarWnd = nullptr;

    _wndWidth = 0;

    _dontAdjustOnEmptyItemPage = FALSE;

    _isStoreAppMode = isStoreAppMode;

    _pageStatusCur = 0;
    _pageStatusTotal = 0;
}

// [MspyIME]
void CCandidateWindow::_SetPageStatus(_In_ UINT current, _In_ UINT total)
{
    _pageStatusCur = current;
    _pageStatusTotal = total;
}

// [MspyIME]
void CCandidateWindow::_OnListUpdated()
{
    _ResizeWindow();
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CCandidateWindow::~CCandidateWindow()
{
    _ClearList();
    _DeleteShadowWnd();
    _DeleteVScrollBarWnd();

    if (_hFont != nullptr)  // [MspyIME] per-DPI font
    {
        DeleteObject(_hFont);
        _hFont = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// _GetDpiForWnd / _CandidateFont / _UpdateMetricsForDpi    [MspyIME]
//
// Everything this window draws is authored at 96 dpi. Hosts differ in what
// they report: a per-monitor-aware host (LINE, Chrome) leaves the process
// system DPI at 96, so the shared Global::defaultlFontHandle — built once
// from GetDeviceCaps(LOGPIXELSY) — comes out tiny on a 150%/175% display.
// Ask the window itself instead and rebuild everything per monitor.
//----------------------------------------------------------------------------

UINT CCandidateWindow::_GetDpiForWnd(_In_opt_ HWND wndHandle)
{
    UINT dpi = 0;
    if (wndHandle != nullptr)
    {
        dpi = GetDpiForWindow(wndHandle);
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
    return (dpi != 0) ? dpi : 96;
}

HFONT CCandidateWindow::_CandidateFont() const
{
    return (_hFont != nullptr) ? _hFont : Global::defaultlFontHandle;
}

void CCandidateWindow::_UpdateMetricsForDpi(_In_opt_ HWND wndHandle)
{
    const UINT dpi = _GetDpiForWnd(wndHandle);
    if (dpi == _dpi && _hFont != nullptr)
    {
        return;
    }
    _dpi = dpi;

    WCHAR fontName[50] = {L'\0'};
    LoadString(Global::dllInstanceHandle, IDS_DEFAULT_FONT, fontName, ARRAYSIZE(fontName));
    const int fontHeight = -MulDiv(CANDWND_FONT_POINT_SIZE, (int)dpi, 72);
    HFONT hFont = CreateFont(fontHeight, 0, 0, 0, FW_MEDIUM, 0, 0, 0, 0, 0, 0, 0, 0, fontName);
    if (hFont == nullptr)
    {
        LOGFONT lf = {0};
        SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(LOGFONT), &lf, 0);
        hFont = CreateFont(fontHeight, 0, 0, 0, FW_MEDIUM, 0, 0, 0, 0, 0, 0, 0, 0, lf.lfFaceName);
    }
    if (hFont != nullptr)
    {
        if (_hFont != nullptr)
        {
            DeleteObject(_hFont);
        }
        _hFont = hFont;
    }

    _cyRow = MulDiv(CANDWND_ROW_WIDTH, (int)dpi, 96);
    _cyPageBar = MulDiv(CANDWND_PAGEBAR_HEIGHT, (int)dpi, 96);
    _cxBorder = max(1, MulDiv(CANDWND_BORDER_WIDTH, (int)dpi, 96));

    // The window width is a multiple of the character width, so it follows
    // the font once the metrics are re-measured with it.
    HDC dcHandle = GetDC(wndHandle);
    if (dcHandle != nullptr)
    {
        HFONT hFontOld = (HFONT)SelectObject(dcHandle, _CandidateFont());
        GetTextMetrics(dcHandle, &_TextMetric);
        _cxTitle = _TextMetric.tmMaxCharWidth * _wndWidth;
        SelectObject(dcHandle, hFontOld);
        ReleaseDC(wndHandle, dcHandle);
    }
}

//+---------------------------------------------------------------------------
//
// _Create
//
// CandidateWinow is the top window
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_Create(ATOM atom, _In_ UINT wndWidth, _In_opt_ HWND parentWndHandle)
{
    BOOL ret = FALSE;
    _wndWidth = wndWidth;

    ret = _CreateMainWindow(atom, parentWndHandle);
    if (FALSE == ret)
    {
        goto Exit;
    }

    ret = _CreateBackGroundShadowWindow();
    if (FALSE == ret)
    {
        goto Exit;
    }

    // [MspyIME] No scrollbar: the composer pages with digits 7/8 and the
    // window only ever shows the current page (at most 6 rows).

    _ResizeWindow();

Exit:
    return TRUE;
}

BOOL CCandidateWindow::_CreateMainWindow(ATOM atom, _In_opt_ HWND parentWndHandle)
{
    _SetUIWnd(this);

    if (!CBaseWindow::_Create(atom,
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        WS_POPUP,
        NULL, 0, 0, parentWndHandle))
    {
        return FALSE;
    }

    // [MspyIME] Ask DWM for small rounded corners (no-op before Win11).
    HWND wndHandle = _GetWnd();
    if (wndHandle != nullptr)
    {
        const DWORD DWMWA_WINDOW_CORNER_PREFERENCE_LOCAL = 33;  // DWMWA_WINDOW_CORNER_PREFERENCE
        const DWORD DWMWCP_ROUNDSMALL_LOCAL = 3;                // DWMWCP_ROUNDSMALL
        DWORD pref = DWMWCP_ROUNDSMALL_LOCAL;
        DwmSetWindowAttribute(wndHandle, DWMWA_WINDOW_CORNER_PREFERENCE_LOCAL, &pref, sizeof(pref));
    }

    return TRUE;
}

BOOL CCandidateWindow::_CreateBackGroundShadowWindow()
{
    _pShadowWnd = new (std::nothrow) CShadowWindow(this);
    if (_pShadowWnd == nullptr)
    {
        return FALSE;
    }

    if (!_pShadowWnd->_Create(Global::AtomShadowWindow,
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WS_DISABLED | WS_POPUP, this))
    {
        _DeleteShadowWnd();
        return FALSE;
    }

    return TRUE;
}

BOOL CCandidateWindow::_CreateVScrollWindow()
{
    BOOL ret = FALSE;

    SHELL_MODE shellMode = _isStoreAppMode ? STOREAPP : DESKTOP;
    CScrollBarWindowFactory* pFactory = CScrollBarWindowFactory::Instance();
    _pVScrollBarWnd = pFactory->MakeScrollBarWindow(shellMode);

    if (_pVScrollBarWnd == nullptr)
    {
        _DeleteShadowWnd();
        goto Exit;
    }

    _pVScrollBarWnd->_SetUIWnd(this);

    if (!_pVScrollBarWnd->_Create(Global::AtomScrollBarWindow, WS_EX_TOPMOST | WS_EX_TOOLWINDOW, WS_CHILD, this))
    {
        _DeleteVScrollBarWnd();
        _DeleteShadowWnd();
        goto Exit;
    }
    
    ret = TRUE;

Exit:
    pFactory->Release();
    return ret;
}

void CCandidateWindow::_ResizeWindow()
{
    SIZE size = {0, 0};

    // [MspyIME] The presenter moves the window next to the caret before the
    // list is filled in, so this is where the monitor (and its DPI) is
    // finally known.
    _UpdateMetricsForDpi(_GetWnd());

    _cxTitle = max(_cxTitle, size.cx + 2 * GetSystemMetrics(SM_CXFRAME));

    // [MspyIME] Size to the rows actually shown (the list holds only the
    // current page) plus the page-indicator strip at the bottom.
    int rows = (int)_candidateList.Count();
    int candidateListPageCnt = _pIndexRange->Count();
    if (rows < 1)
    {
        rows = 1;
    }
    if (rows > candidateListPageCnt)
    {
        rows = candidateListPageCnt;
    }

    // Keep the window where the presenter placed it (next to the text
    // extent); resizing must never snap it back to the screen origin.
    POINT pt = {0, 0};
    HWND wndHandle = _GetWnd();
    if (wndHandle != nullptr)
    {
        RECT rcWnd = {0, 0, 0, 0};
        GetWindowRect(wndHandle, &rcWnd);
        pt.x = rcWnd.left;
        pt.y = rcWnd.top;
    }
    CBaseWindow::_Resize(pt.x, pt.y, _cxTitle, _cyRow * rows + _cyPageBar);
}

//+---------------------------------------------------------------------------
//
// _Move
//
//----------------------------------------------------------------------------

void CCandidateWindow::_Move(int x, int y)
{
    CBaseWindow::_Move(x, y);
}

//+---------------------------------------------------------------------------
//
// _Show
//
//----------------------------------------------------------------------------

void CCandidateWindow::_Show(BOOL isShowWnd)
{
    if (_pShadowWnd)
    {
        _pShadowWnd->_Show(isShowWnd);
    }
    CBaseWindow::_Show(isShowWnd);
}

//+---------------------------------------------------------------------------
//
// _SetTextColor
// _SetFillColor
//
//----------------------------------------------------------------------------

VOID CCandidateWindow::_SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor)
{
    _crTextColor = _AdjustTextColor(crColor, crBkColor);
    _crBkColor = crBkColor;
}

VOID CCandidateWindow::_SetFillColor(_In_ HBRUSH hBrush)
{
    _brshBkColor = hBrush;
}

//+---------------------------------------------------------------------------
//
// _WindowProcCallback
//
// Cand window proc.
//----------------------------------------------------------------------------

const int PageCountPosition = 1;
const int StringPosition = 4;

LRESULT CALLBACK CCandidateWindow::_WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        // [MspyIME] Font + pixel metrics for this window's DPI (redone in
        // _ResizeWindow once the window has been placed).
        _UpdateMetricsForDpi(wndHandle);
        return 0;

    // [MspyIME] Dragged onto a monitor with a different scale factor.
    case WM_DPICHANGED:
        _UpdateMetricsForDpi(wndHandle);
        _ResizeWindow();
        _InvalidateRect();
        return 0;

    case WM_DESTROY:
        _DeleteShadowWnd();
        return 0;

    case WM_WINDOWPOSCHANGED:
        {
            WINDOWPOS* pWndPos = (WINDOWPOS*)lParam;

            // move shadow
            if (_pShadowWnd)
            {
                _pShadowWnd->_OnOwnerWndMoved((pWndPos->flags & SWP_NOSIZE) == 0);
            }

            // move v-scroll
            if (_pVScrollBarWnd)
            {
                _pVScrollBarWnd->_OnOwnerWndMoved((pWndPos->flags & SWP_NOSIZE) == 0);
            }

            _FireMessageToLightDismiss(wndHandle, pWndPos);
        }
        break;

    case WM_WINDOWPOSCHANGING:
        {
            WINDOWPOS* pWndPos = (WINDOWPOS*)lParam;

            // show/hide shadow
            if (_pShadowWnd)
            {
                if ((pWndPos->flags & SWP_HIDEWINDOW) != 0)
                {
                    _pShadowWnd->_Show(FALSE);
                }

                // don't go behaind of shadow
                if (((pWndPos->flags & SWP_NOZORDER) == 0) && (pWndPos->hwndInsertAfter == _pShadowWnd->_GetWnd()))
                {
                    pWndPos->flags |= SWP_NOZORDER;
                }

                _pShadowWnd->_OnOwnerWndMoved((pWndPos->flags & SWP_NOSIZE) == 0);
            }

            // show/hide v-scroll
            if (_pVScrollBarWnd)
            {
                if ((pWndPos->flags & SWP_HIDEWINDOW) != 0)
                {
                    _pVScrollBarWnd->_Show(FALSE);
                }

                _pVScrollBarWnd->_OnOwnerWndMoved((pWndPos->flags & SWP_NOSIZE) == 0);
            }
        }
        break;

    case WM_SHOWWINDOW:
        // show/hide shadow
        if (_pShadowWnd)
        {
            _pShadowWnd->_Show((BOOL)wParam);
        }

        // show/hide v-scroll
        if (_pVScrollBarWnd)
        {
            _pVScrollBarWnd->_Show((BOOL)wParam);
        }
        break;

    case WM_PAINT:
        {
            HDC dcHandle = nullptr;
            PAINTSTRUCT ps;

            dcHandle = BeginPaint(wndHandle, &ps);
            _OnPaint(dcHandle, &ps);
            _DrawBorder(wndHandle, _cxBorder);
            EndPaint(wndHandle, &ps);
        }
        return 0;

    case WM_SETCURSOR:
        {
            POINT cursorPoint;

            GetCursorPos(&cursorPoint);
            MapWindowPoints(NULL, wndHandle, &cursorPoint, 1);

            // handle mouse message
            _HandleMouseMsg(HIWORD(lParam), cursorPoint);
        }
        return 1;

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        {
            POINT point;

            POINTSTOPOINT(point, MAKEPOINTS(lParam));

            // handle mouse message
            _HandleMouseMsg(uMsg, point);
        }
		// we processes this message, it should return zero. 
        return 0;

    case WM_MOUSEACTIVATE:
        {
            WORD mouseEvent = HIWORD(lParam);
            if (mouseEvent == WM_LBUTTONDOWN || 
                mouseEvent == WM_RBUTTONDOWN || 
                mouseEvent == WM_MBUTTONDOWN) 
            {
                return MA_NOACTIVATE;
            }
        }
        break;

    case WM_POINTERACTIVATE:
        return PA_NOACTIVATE;

    case WM_VSCROLL:
        _OnVScroll(LOWORD(wParam), HIWORD(wParam));
        return 0;
    }

    return DefWindowProc(wndHandle, uMsg, wParam, lParam);
}

//+---------------------------------------------------------------------------
//
// _HandleMouseMsg
//
//----------------------------------------------------------------------------

void CCandidateWindow::_HandleMouseMsg(_In_ UINT mouseMsg, _In_ POINT point)
{
    switch (mouseMsg)
    {
    case WM_MOUSEMOVE:
        _OnMouseMove(point);
        break;
    case WM_LBUTTONDOWN:
        _OnLButtonDown(point);
        break;
    case WM_LBUTTONUP:
        _OnLButtonUp(point);
        break;
    }
}

//+---------------------------------------------------------------------------
//
// _OnPaint
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pPaintStruct)
{
    SetBkMode(dcHandle, TRANSPARENT);

    HFONT hFontOld = (HFONT)SelectObject(dcHandle, _CandidateFont());

    FillRect(dcHandle, &pPaintStruct->rcPaint, _brshBkColor);

    UINT currentPageIndex = 0;
    UINT currentPage = 0;

    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        goto cleanup;
    }
    
    _AdjustPageIndex(currentPage, currentPageIndex);

    _DrawList(dcHandle, currentPageIndex, &pPaintStruct->rcPaint);

cleanup:
    SelectObject(dcHandle, hFontOld);
}

//+---------------------------------------------------------------------------
//
// _OnLButtonDown
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnLButtonDown(POINT pt)
{
    RECT rcWindow = {0, 0, 0, 0};;
    _GetClientRect(&rcWindow);

    int cyLine = _cyRow;
    
    UINT candidateListPageCnt = _pIndexRange->Count();
    UINT index = 0;
    int currentPage = 0;

    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        return;
    }

    // Hit test on list items
    index = *_PageIndex.GetAt(currentPage);

    for (UINT pageCount = 0; (index < _candidateList.Count()) && (pageCount < candidateListPageCnt); index++, pageCount++)
    {
        RECT rc = {0, 0, 0, 0};

        rc.left = rcWindow.left;
        rc.right = rcWindow.right;  // [MspyIME] full-width rows, no scrollbar
        rc.top = rcWindow.top + (pageCount * cyLine);
        rc.bottom = rcWindow.top + ((pageCount + 1) * cyLine);

        if (PtInRect(&rc, pt) && _pfnCallback)
        {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            _currentSelection = index;
            _pfnCallback(_pObj, CAND_ITEM_SELECT);
            return;
        }
    }

    SetCursor(LoadCursor(NULL, IDC_ARROW));

    if (_pVScrollBarWnd)
    {
        RECT rc = {0, 0, 0, 0};

        _pVScrollBarWnd->_GetClientRect(&rc);
        MapWindowPoints(_GetWnd(), _pVScrollBarWnd->_GetWnd(), &pt, 1);

        if (PtInRect(&rc, pt))
        {
            _pVScrollBarWnd->_OnLButtonDown(pt);
        }
    }
}

//+---------------------------------------------------------------------------
//
// _OnLButtonUp
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnLButtonUp(POINT pt)
{
    if (nullptr == _pVScrollBarWnd)
    {
        return;
    }

    RECT rc = {0, 0, 0, 0};
    _pVScrollBarWnd->_GetClientRect(&rc);
    MapWindowPoints(_GetWnd(), _pVScrollBarWnd->_GetWnd(), &pt, 1);

    if (_IsCapture())
    {
        _pVScrollBarWnd->_OnLButtonUp(pt);
    }
    else if (PtInRect(&rc, pt))
    {
        _pVScrollBarWnd->_OnLButtonUp(pt);
    }
}

//+---------------------------------------------------------------------------
//
// _OnMouseMove
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnMouseMove(POINT pt)
{
    RECT rcWindow = {0, 0, 0, 0};

    _GetClientRect(&rcWindow);

    RECT rc = {0, 0, 0, 0};

    rc.left   = rcWindow.left;
    rc.right  = rcWindow.right - GetSystemMetrics(SM_CXVSCROLL) * 2;

    rc.top    = rcWindow.top;
    rc.bottom = rcWindow.bottom;

    if (PtInRect(&rc, pt))
    {
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return;
    }

    SetCursor(LoadCursor(NULL, IDC_ARROW));

    if (_pVScrollBarWnd)
    {
        _pVScrollBarWnd->_GetClientRect(&rc);
        MapWindowPoints(_GetWnd(), _pVScrollBarWnd->_GetWnd(), &pt, 1);

        if (PtInRect(&rc, pt))
        {
            _pVScrollBarWnd->_OnMouseMove(pt);
        }
    }
}

//+---------------------------------------------------------------------------
//
// _OnVScroll
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnVScroll(DWORD dwSB, _In_ DWORD nPos)
{
    switch (dwSB)
    {
    case SB_LINEDOWN:
        _SetSelectionOffset(+1);
        _InvalidateRect();
        break;
    case SB_LINEUP:
        _SetSelectionOffset(-1);
        _InvalidateRect();
        break;
    case SB_PAGEDOWN:
        _MovePage(+1, FALSE);
        _InvalidateRect();
        break;
    case SB_PAGEUP:
        _MovePage(-1, FALSE);
        _InvalidateRect();
        break;
    case SB_THUMBPOSITION:
        _SetSelection(nPos, FALSE);
        _InvalidateRect();
        break;
    }
}

//+---------------------------------------------------------------------------
//
// _DrawList
//
//----------------------------------------------------------------------------

void CCandidateWindow::_DrawList(_In_ HDC dcHandle, _In_ UINT iIndex, _In_ RECT *prc)
{
    // [MspyIME] Modern light card: full-width rows, soft accent fill on
    // the selected row, gray digits, page indicator strip at the bottom.
    int pageCount = 0;
    int candidateListPageCnt = _pIndexRange->Count();

    int cxLine = _TextMetric.tmAveCharWidth;
    int cyLine = max(_cyRow, _TextMetric.tmHeight);
    int cyOffset = (cyLine == _cyRow ? (cyLine - _TextMetric.tmHeight) / 2 : 0);

    RECT rcClient = {0, 0, 0, 0};
    _GetClientRect(&rcClient);

    HBRUSH selectedBrush = CreateSolidBrush(CANDWND_SELECTED_BK_COLOR);

    SetBkMode(dcHandle, TRANSPARENT);

    const size_t lenOfPageCount = 16;
    for (;
        (iIndex < _candidateList.Count()) && (pageCount < candidateListPageCnt);
        iIndex++, pageCount++)
    {
        WCHAR pageCountString[lenOfPageCount] = {'\0'};
        CCandidateListItem* pItemList = nullptr;

        RECT rcRow;
        rcRow.top = prc->top + pageCount * cyLine;
        rcRow.bottom = rcRow.top + cyLine;
        rcRow.left = rcClient.left;
        rcRow.right = rcClient.right;

        if (_currentSelection == iIndex && selectedBrush != nullptr)
        {
            FillRect(dcHandle, &rcRow, selectedBrush);
        }

        // Digit label in gray.
        SetTextColor(dcHandle, CANDWND_NUM_COLOR);
        StringCchPrintf(pageCountString, ARRAYSIZE(pageCountString), L"%d", (LONG)*_pIndexRange->GetAt(pageCount));
        ExtTextOut(dcHandle, PageCountPosition * cxLine, pageCount * cyLine + cyOffset, 0, NULL, pageCountString, (UINT)wcslen(pageCountString), NULL);

        // Candidate text.
        SetTextColor(dcHandle, (_currentSelection == iIndex)
                                   ? CANDWND_SELECTED_ITEM_COLOR
                                   : _crTextColor);
        pItemList = _candidateList.GetAt(iIndex);
        ExtTextOut(dcHandle, StringPosition * cxLine, pageCount * cyLine + cyOffset, 0, NULL, pItemList->_ItemString.Get(), (DWORD)pItemList->_ItemString.GetLength(), NULL);
    }

    if (selectedBrush != nullptr)
    {
        DeleteObject(selectedBrush);
    }

    // Page indicator ("current/total"), bottom-right, only when paging.
    if (_pageStatusTotal > 1)
    {
        WCHAR pageStatus[32] = {'\0'};
        StringCchPrintf(pageStatus, ARRAYSIZE(pageStatus), L"%u/%u",
                        _pageStatusCur, _pageStatusTotal);

        RECT rcBar;
        rcBar.left = rcClient.left;
        rcBar.right = rcClient.right - cxLine;
        rcBar.top = rcClient.bottom - _cyPageBar;
        rcBar.bottom = rcClient.bottom;

        SetTextColor(dcHandle, CANDWND_PAGE_COLOR);
        DrawText(dcHandle, pageStatus, -1, &rcBar,
                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

//+---------------------------------------------------------------------------
//
// _DrawBorder
//
//----------------------------------------------------------------------------
void CCandidateWindow::_DrawBorder(_In_ HWND wndHandle, _In_ int cx)
{
    RECT rcWnd;

    HDC dcHandle = GetWindowDC(wndHandle);

    GetWindowRect(wndHandle, &rcWnd);
    // zero based
    OffsetRect(&rcWnd, -rcWnd.left, -rcWnd.top); 

    // [MspyIME] Thin solid hairline instead of the sample's dotted frame.
    HPEN hPen = CreatePen(PS_SOLID, cx, CANDWND_BORDER_COLOR);
    HPEN hPenOld = (HPEN)SelectObject(dcHandle, hPen);
    HBRUSH hBorderBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH hBorderBrushOld = (HBRUSH)SelectObject(dcHandle, hBorderBrush);

    Rectangle(dcHandle, rcWnd.left, rcWnd.top, rcWnd.right, rcWnd.bottom);

    SelectObject(dcHandle, hPenOld);
    SelectObject(dcHandle, hBorderBrushOld);
    DeleteObject(hPen);
    DeleteObject(hBorderBrush);
    ReleaseDC(wndHandle, dcHandle);

}

//+---------------------------------------------------------------------------
//
// _AddString
//
//----------------------------------------------------------------------------

void CCandidateWindow::_AddString(_Inout_ CCandidateListItem *pCandidateItem, _In_ BOOL isAddFindKeyCode)
{
    DWORD_PTR dwItemString = pCandidateItem->_ItemString.GetLength();
    const WCHAR* pwchString = nullptr;
    if (dwItemString)
    {
        pwchString = new (std::nothrow) WCHAR[ dwItemString ];
        if (!pwchString)
        {
            return;
        }
        memcpy((void*)pwchString, pCandidateItem->_ItemString.Get(), dwItemString * sizeof(WCHAR));
    }

    DWORD_PTR itemWildcard = pCandidateItem->_FindKeyCode.GetLength();
    const WCHAR* pwchWildcard = nullptr;
    if (itemWildcard && isAddFindKeyCode)
    {
        pwchWildcard = new (std::nothrow) WCHAR[ itemWildcard ];
        if (!pwchWildcard)
        {
            if (pwchString)
            {
                delete [] pwchString;
            }
            return;
        }
        memcpy((void*)pwchWildcard, pCandidateItem->_FindKeyCode.Get(), itemWildcard * sizeof(WCHAR));
    }

    CCandidateListItem* pLI = nullptr;
    pLI = _candidateList.Append();
    if (!pLI)
    {
        if (pwchString)
        {
            delete [] pwchString;
            pwchString = nullptr;
        }
        if (pwchWildcard)
        {
            delete [] pwchWildcard;
            pwchWildcard = nullptr;
        }
        return;
    }

    if (pwchString)
    {
        pLI->_ItemString.Set(pwchString, dwItemString);
    }
    if (pwchWildcard)
    {
        pLI->_FindKeyCode.Set(pwchWildcard, itemWildcard);
    }

    return;
}

//+---------------------------------------------------------------------------
//
// _ClearList
//
//----------------------------------------------------------------------------

void CCandidateWindow::_ClearList()
{
    for (UINT index = 0; index < _candidateList.Count(); index++)
    {
        CCandidateListItem* pItemList = nullptr;
        pItemList = _candidateList.GetAt(index);
        delete [] pItemList->_ItemString.Get();
        delete [] pItemList->_FindKeyCode.Get();
    }
    _currentSelection = 0;
    _candidateList.Clear();
    _PageIndex.Clear();
}

//+---------------------------------------------------------------------------
//
// _SetScrollInfo
//
//----------------------------------------------------------------------------

void CCandidateWindow::_SetScrollInfo(_In_ int nMax, _In_ int nPage)
{
    CScrollInfo si;
    si.nMax = nMax;
    si.nPage = nPage;
    si.nPos = 0;

    if (_pVScrollBarWnd)
    {
        _pVScrollBarWnd->_SetScrollInfo(&si);
    }
}

//+---------------------------------------------------------------------------
//
// _GetCandidateString
//
//----------------------------------------------------------------------------

DWORD CCandidateWindow::_GetCandidateString(_In_ int iIndex, _Outptr_result_maybenull_z_ const WCHAR **ppwchCandidateString)
{
    CCandidateListItem* pItemList = nullptr;

    if (iIndex < 0 )
    {
        *ppwchCandidateString = nullptr;
        return 0;
    }

    UINT index = static_cast<UINT>(iIndex);
	
	if (index >= _candidateList.Count())
    {
        *ppwchCandidateString = nullptr;
        return 0;
    }

    pItemList = _candidateList.GetAt(iIndex);
    if (ppwchCandidateString)
    {
        *ppwchCandidateString = pItemList->_ItemString.Get();
    }
    return (DWORD)pItemList->_ItemString.GetLength();
}

//+---------------------------------------------------------------------------
//
// _GetSelectedCandidateString
//
//----------------------------------------------------------------------------

DWORD CCandidateWindow::_GetSelectedCandidateString(_Outptr_result_maybenull_ const WCHAR **ppwchCandidateString)
{
    CCandidateListItem* pItemList = nullptr;

    if (_currentSelection >= _candidateList.Count())
    {
        *ppwchCandidateString = nullptr;
        return 0;
    }

    pItemList = _candidateList.GetAt(_currentSelection);
    if (ppwchCandidateString)
    {
        *ppwchCandidateString = pItemList->_ItemString.Get();
    }
    return (DWORD)pItemList->_ItemString.GetLength();
}

//+---------------------------------------------------------------------------
//
// _SetSelectionInPage
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_SetSelectionInPage(int nPos)
{	
    if (nPos < 0)
    {
        return FALSE;
    }

    UINT pos = static_cast<UINT>(nPos);

    if (pos >= _candidateList.Count())
    {
        return FALSE;
    }

    int currentPage = 0;
    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        return FALSE;
    }

    _currentSelection = *_PageIndex.GetAt(currentPage) + nPos;

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _MoveSelection
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_MoveSelection(_In_ int offSet, _In_ BOOL isNotify)
{
    if (_currentSelection + offSet >= _candidateList.Count())
    {
        return FALSE;
    }

    _currentSelection += offSet;

    _dontAdjustOnEmptyItemPage = TRUE;

    if (_pVScrollBarWnd && isNotify)
    {
        _pVScrollBarWnd->_ShiftLine(offSet, isNotify);
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _SetSelection
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_SetSelection(_In_ int selectedIndex, _In_ BOOL isNotify)
{
    if (selectedIndex == -1)
    {
        selectedIndex = _candidateList.Count() - 1;
    }

    if (selectedIndex < 0)
    {
        return FALSE;
    }

    int candCnt = static_cast<int>(_candidateList.Count());
    if (selectedIndex >= candCnt)
    {
        return FALSE;
    }

    _currentSelection = static_cast<UINT>(selectedIndex);

    BOOL ret = _AdjustPageIndexForSelection();

    if (_pVScrollBarWnd && isNotify)
    {
        _pVScrollBarWnd->_ShiftPosition(selectedIndex, isNotify);
    }

    return ret;
}

//+---------------------------------------------------------------------------
//
// _SetSelection
//
//----------------------------------------------------------------------------
void CCandidateWindow::_SetSelection(_In_ int nIndex)
{
    _currentSelection = nIndex;
}

//+---------------------------------------------------------------------------
//
// _MovePage
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_MovePage(_In_ int offSet, _In_ BOOL isNotify)
{
    if (offSet == 0)
    {
        return TRUE;
    }

    int currentPage = 0;
    int selectionOffset = 0;
    int newPage = 0;

    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        return FALSE;
    }

    newPage = currentPage + offSet;
    if ((newPage < 0) || (newPage >= static_cast<int>(_PageIndex.Count())))
    {
        return FALSE;
    }

    // If current selection is at the top of the page AND 
    // we are on the "default" page border, then we don't
    // want adjustment to eliminate empty entries.
    //
    // We do this for keeping behavior inline with downlevel.
    if (_currentSelection % _pIndexRange->Count() == 0 && 
        _currentSelection == *_PageIndex.GetAt(currentPage)) 
    {
        _dontAdjustOnEmptyItemPage = TRUE;
    }

    selectionOffset = _currentSelection - *_PageIndex.GetAt(currentPage);
    _currentSelection = *_PageIndex.GetAt(newPage) + selectionOffset;
    _currentSelection = _candidateList.Count() > _currentSelection ? _currentSelection : _candidateList.Count() - 1;

    // adjust scrollbar position
    if (_pVScrollBarWnd && isNotify)
    {
        _pVScrollBarWnd->_ShiftPage(offSet, isNotify);
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _SetSelectionOffset
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_SetSelectionOffset(_In_ int offSet)
{
	if (_currentSelection + offSet >= _candidateList.Count())
    {
        return FALSE;
    }

    BOOL fCurrentPageHasEmptyItems = FALSE;
    BOOL fAdjustPageIndex = TRUE;

    _CurrentPageHasEmptyItems(&fCurrentPageHasEmptyItems);

    int newOffset = _currentSelection + offSet;

    // For SB_LINEUP and SB_LINEDOWN, we need to special case if CurrentPageHasEmptyItems.
    // CurrentPageHasEmptyItems if we are on the last page.
    if ((offSet == 1 || offSet == -1) &&
        fCurrentPageHasEmptyItems && _PageIndex.Count() > 1)
    {
        int iPageIndex = *_PageIndex.GetAt(_PageIndex.Count() - 1);
        // Moving on the last page and last page has empty items.
        if (newOffset >= iPageIndex)
        {
            fAdjustPageIndex = FALSE;
        }
        // Moving across page border.
        else if (newOffset < iPageIndex)
        {
            fAdjustPageIndex = TRUE;
        }

        _dontAdjustOnEmptyItemPage = TRUE;
    }

    _currentSelection = newOffset;

    if (fAdjustPageIndex)
    {
        return _AdjustPageIndexForSelection();
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _GetPageIndex
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_GetPageIndex(UINT *pIndex, _In_ UINT uSize, _Inout_ UINT *puPageCnt)
{
    HRESULT hr = S_OK;

    if (uSize > _PageIndex.Count())
    {
        uSize = _PageIndex.Count();
    }
    else
    {
        hr = S_FALSE;
    }

    if (pIndex)
    {
        for (UINT i = 0; i < uSize; i++)
        {
            *pIndex = *_PageIndex.GetAt(i);
            pIndex++;
        }
    }

    *puPageCnt = _PageIndex.Count();

    return hr;
}

//+---------------------------------------------------------------------------
//
// _SetPageIndex
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_SetPageIndex(UINT *pIndex, _In_ UINT uPageCnt)
{
    uPageCnt;

    _PageIndex.Clear();

    for (UINT i = 0; i < uPageCnt; i++)
    {
        UINT *pLastNewPageIndex = _PageIndex.Append();
        if (pLastNewPageIndex != nullptr)
        {
            *pLastNewPageIndex = *pIndex;
            pIndex++;
        }
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _GetCurrentPage
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_GetCurrentPage(_Inout_ UINT *pCurrentPage)
{
    HRESULT hr = S_OK;

    if (pCurrentPage == nullptr)
    {
        hr = E_INVALIDARG;
        goto Exit;
    }

    *pCurrentPage = 0;

    if (_PageIndex.Count() == 0)
    {
        hr = E_UNEXPECTED;
        goto Exit;
    }

    if (_PageIndex.Count() == 1)
    {
        *pCurrentPage = 0;
         goto Exit;
    }

    UINT i = 0;
    for (i = 1; i < _PageIndex.Count(); i++)
    {
        UINT uPageIndex = *_PageIndex.GetAt(i);

        if (uPageIndex > _currentSelection)
        {
            break;
        }
    }

    *pCurrentPage = i - 1;

Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _GetCurrentPage
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_GetCurrentPage(_Inout_ int *pCurrentPage)
{
    HRESULT hr = E_FAIL;
    UINT needCastCurrentPage = 0;
    
    if (nullptr == pCurrentPage)
    {
        goto Exit;
    }

    *pCurrentPage = 0;

    hr = _GetCurrentPage(&needCastCurrentPage);
    if (FAILED(hr))
    {
       goto Exit;
    }

    hr = UIntToInt(needCastCurrentPage, pCurrentPage);
    if (FAILED(hr))
    {
        goto Exit;
    }

Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _AdjustPageIndexForSelection
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_AdjustPageIndexForSelection()
{
    UINT candidateListPageCnt = _pIndexRange->Count();
    UINT* pNewPageIndex = nullptr;
    UINT newPageCnt = 0;

    if (_candidateList.Count() < candidateListPageCnt)
    {
        // no needed to restruct page index
        return TRUE;
    }

    // B is number of pages before the current page
    // A is number of pages after the current page
    // uNewPageCount is A + B + 1;
    // A is (uItemsAfter - 1) / candidateListPageCnt + 1 -> 
    //      (_CandidateListCount - _currentSelection - CandidateListPageCount - 1) / candidateListPageCnt + 1->
    //      (_CandidateListCount - _currentSelection - 1) / candidateListPageCnt
    // B is (uItemsBefore - 1) / candidateListPageCnt + 1 ->
    //      (_currentSelection - 1) / candidateListPageCnt + 1
    // A + B is (_CandidateListCount - 2) / candidateListPageCnt + 1

    BOOL isBefore = _currentSelection;
    BOOL isAfter = _candidateList.Count() > _currentSelection + candidateListPageCnt;

    // only have current page
    if (!isBefore && !isAfter) 
    {
        newPageCnt = 1;
    }
    // only have after pages; just count the total number of pages
    else if (!isBefore && isAfter)
    {
        newPageCnt = (_candidateList.Count() - 1) / candidateListPageCnt + 1;
    }
    // we are at the last page
    else if (isBefore && !isAfter)
    {
        newPageCnt = 2 + (_currentSelection - 1) / candidateListPageCnt;
    }
    else if (isBefore && isAfter)
    {
        newPageCnt = (_candidateList.Count() - 2) / candidateListPageCnt + 2;
    }

    pNewPageIndex = new (std::nothrow) UINT[ newPageCnt ];
    if (pNewPageIndex == nullptr)
    {
        return FALSE;
    }
    pNewPageIndex[0] = 0;
    UINT firstPage = _currentSelection % candidateListPageCnt;
    if (firstPage && newPageCnt > 1) 
    {
        pNewPageIndex[1] = firstPage;
    }

    for (UINT i = firstPage ? 2 : 1; i < newPageCnt; ++i)
    {
        pNewPageIndex[i] = pNewPageIndex[i - 1] + candidateListPageCnt;
    }

    _SetPageIndex(pNewPageIndex, newPageCnt);

    delete [] pNewPageIndex;

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _AdjustTextColor
//
//----------------------------------------------------------------------------

COLORREF _AdjustTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor)
{
    if (!Global::IsTooSimilar(crColor, crBkColor))
    {
        return crColor;
    }
    else
    {
        return crColor ^ RGB(255, 255, 255);
    }
}

//+---------------------------------------------------------------------------
//
// _CurrentPageHasEmptyItems
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_CurrentPageHasEmptyItems(_Inout_ BOOL *hasEmptyItems)
{
    int candidateListPageCnt = _pIndexRange->Count();
    UINT currentPage = 0;

    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        return S_FALSE;
    }

    if ((currentPage == 0 || currentPage == _PageIndex.Count()-1) &&
        (_PageIndex.Count() > 0) &&
        (*_PageIndex.GetAt(currentPage) > (UINT)(_candidateList.Count() - candidateListPageCnt)))
    {
        *hasEmptyItems = TRUE;
    }
    else 
    {
        *hasEmptyItems = FALSE;
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _FireMessageToLightDismiss
//      fire EVENT_OBJECT_IME_xxx to let LightDismiss know about IME window.
//----------------------------------------------------------------------------

void CCandidateWindow::_FireMessageToLightDismiss(_In_ HWND wndHandle, _In_ WINDOWPOS *pWndPos)
{
    if (nullptr == pWndPos)
    {
        return;
    }

    BOOL isShowWnd = ((pWndPos->flags & SWP_SHOWWINDOW) != 0);
    BOOL isHide = ((pWndPos->flags & SWP_HIDEWINDOW) != 0);
    BOOL needResize = ((pWndPos->flags & SWP_NOSIZE) == 0);
    BOOL needMove = ((pWndPos->flags & SWP_NOMOVE) == 0);
    BOOL needRedraw = ((pWndPos->flags & SWP_NOREDRAW) == 0);

    if (isShowWnd)
    {
        NotifyWinEvent(EVENT_OBJECT_IME_SHOW, wndHandle, OBJID_CLIENT, CHILDID_SELF);
    }
    else if (isHide)
    {
        NotifyWinEvent(EVENT_OBJECT_IME_HIDE, wndHandle, OBJID_CLIENT, CHILDID_SELF);
    }
    else if (needResize || needMove || needRedraw)
    {
        if (IsWindowVisible(wndHandle))
        {
            NotifyWinEvent(EVENT_OBJECT_IME_CHANGE, wndHandle, OBJID_CLIENT, CHILDID_SELF);
        }
    }

}

HRESULT CCandidateWindow::_AdjustPageIndex(_Inout_ UINT & currentPage, _Inout_ UINT & currentPageIndex)
{
    HRESULT hr = E_FAIL;
    UINT candidateListPageCnt = _pIndexRange->Count();

    currentPageIndex = *_PageIndex.GetAt(currentPage);

    BOOL hasEmptyItems = FALSE;
    if (FAILED(_CurrentPageHasEmptyItems(&hasEmptyItems)))
    {
        goto Exit; 
    }

    if (FALSE == hasEmptyItems)
    {
        goto Exit;
    }

    if (TRUE == _dontAdjustOnEmptyItemPage)
    {
        goto Exit;
    }

    UINT tempSelection = _currentSelection;

    // Last page
    UINT candNum = _candidateList.Count();
    UINT pageNum = _PageIndex.Count();

    if ((currentPageIndex > candNum - candidateListPageCnt) && (pageNum > 0) && (currentPage == (pageNum - 1)))
    {
        _currentSelection = candNum - candidateListPageCnt;

        _AdjustPageIndexForSelection();

        _currentSelection = tempSelection;

        if (FAILED(_GetCurrentPage(&currentPage)))
        {
            goto Exit;
        }

        currentPageIndex = *_PageIndex.GetAt(currentPage);
    }
    // First page
    else if ((currentPageIndex < candidateListPageCnt) && (currentPage == 0))
    {
        _currentSelection = 0;

        _AdjustPageIndexForSelection();

        _currentSelection = tempSelection;
    }

    _dontAdjustOnEmptyItemPage = FALSE;
    hr = S_OK;

Exit:
    return hr;
}
void CCandidateWindow::_DeleteShadowWnd()
{
    if (nullptr != _pShadowWnd)
    {
        delete _pShadowWnd;
        _pShadowWnd = nullptr;
    }
}

void CCandidateWindow::_DeleteVScrollBarWnd()
{
    if (nullptr != _pVScrollBarWnd)
    {
        delete _pVScrollBarWnd;
        _pVScrollBarWnd = nullptr;
    }
}