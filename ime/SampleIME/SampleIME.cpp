// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "Private.h"
#include "globals.h"
#include "SampleIME.h"
#include "CandidateListUIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "Compartment.h"

//+---------------------------------------------------------------------------
//
// CreateInstance
//
//----------------------------------------------------------------------------

/* static */
HRESULT CSampleIME::CreateInstance(_In_ IUnknown *pUnkOuter, REFIID riid, _Outptr_ void **ppvObj)
{
    CSampleIME* pSampleIME = nullptr;
    HRESULT hr = S_OK;

    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (nullptr != pUnkOuter)
    {
        return CLASS_E_NOAGGREGATION;
    }

    pSampleIME = new (std::nothrow) CSampleIME();
    if (pSampleIME == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    hr = pSampleIME->QueryInterface(riid, ppvObj);

    pSampleIME->Release();

    return hr;
}

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CSampleIME::CSampleIME()
{
    DllAddRef();

    _pThreadMgr = nullptr;

    _threadMgrEventSinkCookie = TF_INVALID_COOKIE;

    _pTextEditSinkContext = nullptr;
    _textEditSinkCookie = TF_INVALID_COOKIE;

    _activeLanguageProfileNotifySinkCookie = TF_INVALID_COOKIE;

    _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;

    _pComposition = nullptr;

    _pCompositionProcessorEngine = nullptr;

    _candidateMode = CANDIDATE_NONE;
    _pCandidateListUIPresenter = nullptr;
    _isCandidateWithWildcard = FALSE;

    _pDocMgrLastFocused = nullptr;

    _pSIPIMEOnOffCompartment = nullptr;
    _dwSIPIMEOnOffCompartmentSinkCookie = 0;
    _msgWndHandle = nullptr;

    _pContext = nullptr;

    _lastCaretRc = RECT{};
    _haveLastCaretRc = FALSE;

    _refCount = 1;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CSampleIME::~CSampleIME()
{
    if (_pCandidateListUIPresenter)
    {
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;
    }
    DllRelease();
}

//+---------------------------------------------------------------------------
//
// QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CSampleIME::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor))
    {
        *ppvObj = (ITfTextInputProcessor *)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    {
        *ppvObj = (ITfTextInputProcessorEx *)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
    {
        *ppvObj = (ITfThreadMgrEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextEditSink))
    {
        *ppvObj = (ITfTextEditSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    {
        *ppvObj = (ITfKeyEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfActiveLanguageProfileNotifySink))
    {
        *ppvObj = (ITfActiveLanguageProfileNotifySink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
    {
        *ppvObj = (ITfKeyEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
    {
        *ppvObj = (ITfDisplayAttributeProvider *)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
    {
        *ppvObj = (ITfThreadFocusSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFunctionProvider))
    {
        *ppvObj = (ITfFunctionProvider *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFunction))
    {
        *ppvObj = (ITfFunction *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFnGetPreferredTouchKeyboardLayout))
    {
        *ppvObj = (ITfFnGetPreferredTouchKeyboardLayout *)this;
    }

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}


//+---------------------------------------------------------------------------
//
// AddRef
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CSampleIME::AddRef()
{
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CSampleIME::Release()
{
    LONG cr = --_refCount;

    assert(_refCount >= 0);

    if (_refCount == 0)
    {
        delete this;
    }

    return cr;
}

//+---------------------------------------------------------------------------
//
// ITfTextInputProcessorEx::ActivateEx
//
//----------------------------------------------------------------------------

STDAPI CSampleIME::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId, DWORD dwFlags)
{
    _pThreadMgr = pThreadMgr;
    _pThreadMgr->AddRef();

    _tfClientId = tfClientId;
    _dwActivateFlags = dwFlags;

    if (!_InitThreadMgrEventSink())
    {
        goto ExitError;
    }

    ITfDocumentMgr* pDocMgrFocus = nullptr;
    if (SUCCEEDED(_pThreadMgr->GetFocus(&pDocMgrFocus)) && (pDocMgrFocus != nullptr))
    {
        _InitTextEditSink(pDocMgrFocus);
        pDocMgrFocus->Release();
    }

    if (!_InitKeyEventSink())
    {
        goto ExitError;
    }

    if (!_InitActiveLanguageProfileNotifySink())
    {
        goto ExitError;
    }

    if (!_InitThreadFocusSink())
    {
        goto ExitError;
    }

    if (!_InitDisplayAttributeGuidAtom())
    {
        goto ExitError;
    }

    if (!_InitFunctionProviderSink())
    {
        goto ExitError;
    }

    if (!_AddTextProcessorEngine())
    {
        goto ExitError;
    }

    return S_OK;

ExitError:
    Deactivate();
    return E_FAIL;
}

//+---------------------------------------------------------------------------
//
// _RestoreKeyboardOpenForApp    [MspyIME]
//
// Chinese/English is remembered per application. The keyboard open/close
// state is shared across applications by the system, so re-assert this
// application's own mode whenever it takes the focus back; a freshly
// started application begins in English (see
// InitializeSampleIMECompartment).
//----------------------------------------------------------------------------

void CSampleIME::_RestoreKeyboardOpenForApp()
{
    if (_pThreadMgr == nullptr || _tfClientId == TF_CLIENTID_NULL)
    {
        return;
    }

    CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    BOOL isOpen = FALSE;
    if (SUCCEEDED(CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen)) &&
        (isOpen ? TRUE : FALSE) == (_rememberedKeyboardOpen ? TRUE : FALSE))
    {
        return;
    }
    Global::DebugLog(L"RestoreKeyboardOpenForApp: system=%d -> app=%d",
                     isOpen ? 1 : 0, _rememberedKeyboardOpen ? 1 : 0);
    CompartmentKeyboardOpen._SetCompartmentBOOL(_rememberedKeyboardOpen);
}

//+---------------------------------------------------------------------------
//
// MeasureSelectionExtent    [MspyIME]
//
// Measures the caret so the 中/英 bubble knows where to appear. The caret's
// rectangle is only readable under a document lock, and there is no
// composition to hang the request on at focus time, so this asks for the
// default selection's extent instead.
//----------------------------------------------------------------------------

// Shared by the edit session below and by _FlashModeIndicatorUnderLock,
// which already holds a lock of its own (the Shift tap runs inside one).
static BOOL MeasureSelectionExtent(TfEditCookie ec, _In_opt_ ITfContext *pContext, _Out_ RECT *prc)
{
    *prc = RECT{};
    if (pContext == nullptr)
    {
        return FALSE;
    }

    TF_SELECTION selection = {};
    ULONG fetched = 0;
    if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) ||
        fetched == 0 || selection.range == nullptr)
    {
        return FALSE;
    }

    BOOL measured = FALSE;
    ITfContextView* pContextView = nullptr;
    if (SUCCEEDED(pContext->GetActiveView(&pContextView)) && pContextView != nullptr)
    {
        BOOL isClipped = FALSE;
        if (SUCCEEDED(pContextView->GetTextExt(ec, selection.range, prc, &isClipped)))
        {
            measured = TRUE;
        }
        pContextView->Release();
    }
    selection.range->Release();
    return measured;
}

//+---------------------------------------------------------------------------
//
// _FlashModeIndicatorUnderLock    [MspyIME]
//
// The Shift tap already runs inside an edit session, so it can measure the
// caret directly instead of queueing another one.
//----------------------------------------------------------------------------

void CSampleIME::_FlashModeIndicatorUnderLock(TfEditCookie ec, _In_opt_ ITfContext *pContext)
{
    RECT rc = {};
    const BOOL measured = MeasureSelectionExtent(ec, pContext, &rc);
    _FlashModeIndicatorAt(measured ? &rc : nullptr);
}

//+---------------------------------------------------------------------------
//
// _FlashModeIndicatorForFocus    [MspyIME]
//
// The focus landed in a text field. Windows announces a mode CHANGE by
// itself, but this is not a change -- the per-application memory has just
// put the mode back to whatever this application was left in, and nothing
// on screen says which one that is. See ModeIndicator.h.
//----------------------------------------------------------------------------

void CSampleIME::_FlashModeIndicatorForFocus(_In_opt_ ITfDocumentMgr *pDocMgrFocus)
{
    if (pDocMgrFocus == nullptr || _pThreadMgr == nullptr || _tfClientId == TF_CLIENTID_NULL)
    {
        return;
    }

    ITfContext* pContext = nullptr;
    if (FAILED(pDocMgrFocus->GetTop(&pContext)) || pContext == nullptr)
    {
        return;
    }

    // A context the keyboard is disabled in is not somewhere the user can
    // type, so there is no mode worth announcing.
    //
    // Both flags are read off the CONTEXT, not off the thread manager.
    // Chromium keeps one document manager per input type and focuses a
    // dedicated DISABLED one for everything that is not an editable field,
    // marking that CONTEXT with both flags; the thread manager knows
    // nothing about it. Asking the thread manager therefore answered
    // "enabled" for every click on a web page, bubble and all.
    BOOL isDisabled = FALSE;
    CCompartment CompartmentKeyboardDisabled(pContext, _tfClientId, GUID_COMPARTMENT_KEYBOARD_DISABLED);
    CompartmentKeyboardDisabled._GetCompartmentBOOL(isDisabled);
    if (!isDisabled)
    {
        CCompartment CompartmentEmptyContext(pContext, _tfClientId, GUID_COMPARTMENT_EMPTYCONTEXT);
        CompartmentEmptyContext._GetCompartmentBOOL(isDisabled);
    }

    if (!isDisabled)
    {
        _RequestFocusFlashMeasurement(pContext);
    }

    pContext->Release();
}

//+---------------------------------------------------------------------------
//
// Focus-bubble placement    [MspyIME]
//
// Nobody knows where the caret is except the application that draws it, so
// the position always comes from asking it -- ITfContextView::GetTextExt,
// under a document lock, hence an edit session.
//
// The Shift tap can trust that answer: the user has been typing in the
// field, so the application has long since told its text store where the
// caret is. The focus event cannot. A Chromium text store answers with the
// bounds it was last TOLD about, and the renderer reports the newly focused
// field only a few frames later, so at focus time it hands back the
// PREVIOUS caret and reports success -- measured 2026-09-09, one document
// manager returned a byte-identical rect for two clicks into different
// fields five seconds apart.
//
// That echo is what gives it away: the stale answer is the rect we
// ourselves last measured in this process (this TIP instance IS the
// application, one per process). So measure once and place the bubble
// unless the answer repeats what we already had -- in which case we know
// nothing about the new field and say nothing. No waiting, no retries, and
// the bubble never labels a caret that is not there (2026-09-09, at the
// user's direction; the earlier version polled for up to 570 ms, which
// hosts that answered correctly the first time paid in full).
//----------------------------------------------------------------------------

// Measures under a lock and hands the result back without deciding
// anything: _OnFocusFlashMeasured owns the decision.
class CFocusFlashEditSession : public CEditSessionBase
{
public:
    CFocusFlashEditSession(_In_ CSampleIME *pTextService, _In_ ITfContext *pContext)
        : CEditSessionBase(pTextService, pContext)
    {
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec)
    {
        RECT rc = {};
        const BOOL measured = MeasureSelectionExtent(ec, _pContext, &rc);
        _pTextService->_OnFocusFlashMeasured(measured, rc);
        return S_OK;
    }
};

void CSampleIME::_RequestFocusFlashMeasurement(_In_ ITfContext *pContext)
{
    if (pContext == nullptr || _tfClientId == TF_CLIENTID_NULL)
    {
        return;
    }
    CFocusFlashEditSession* pEditSession =
        new (std::nothrow) CFocusFlashEditSession(this, pContext);
    if (pEditSession == nullptr)
    {
        return;
    }
    HRESULT hrSession = S_OK;
    // The session holds its own references, so nothing here has to outlive
    // this call.
    pContext->RequestEditSession(_tfClientId, pEditSession,
                                 TF_ES_ASYNCDONTCARE | TF_ES_READ, &hrSession);
    pEditSession->Release();
}

void CSampleIME::_OnFocusFlashMeasured(BOOL measured, const RECT &rc)
{
    if (!measured)
    {
        return;  // no caret to label
    }

    if (_haveLastCaretRc &&
        rc.left == _lastCaretRc.left && rc.top == _lastCaretRc.top &&
        rc.right == _lastCaretRc.right && rc.bottom == _lastCaretRc.bottom)
    {
        // The host is echoing the caret we already measured: it has not
        // been told about the field the focus just landed in, so we do not
        // know where that field is. Saying nothing beats pointing at the
        // previous one.
        return;
    }

    _FlashModeIndicatorAt(&rc);
}

//+---------------------------------------------------------------------------
//
// _FlashModeIndicatorAt    [MspyIME]
//
// The bubble is a LABEL ON THE CARET, so it appears next to the caret or it
// does not appear at all (2026-09-09, at the user's direction). `prcCaret`
// is the caret's screen rectangle as the host reported it, or nullptr when
// it would not report one; the system caret is the other place a caret can
// come from, for hosts that keep one instead of answering GetTextExt.
//
// There is deliberately no fallback beyond those two. Guessing from the
// mouse pointer puts a bubble next to something that is not a text field --
// on a tab strip, on a heading, on empty page background -- which is worse
// than saying nothing: it invites the user to read a mode into a place
// where nothing can be typed.
//----------------------------------------------------------------------------

// The caret the GUI thread reports belongs to whichever window last created
// one, which need not be the window now holding the focus.
static BOOL SystemCaretPoint(_Out_ POINT *ppt)
{
    *ppt = POINT{};
    GUITHREADINFO threadInfo = {};
    threadInfo.cbSize = sizeof(threadInfo);
    if (!GetGUIThreadInfo(GetCurrentThreadId(), &threadInfo) ||
        threadInfo.hwndCaret == nullptr ||
        threadInfo.rcCaret.bottom <= threadInfo.rcCaret.top)  // no height, no caret
    {
        return FALSE;
    }
    if (threadInfo.hwndFocus != nullptr &&
        threadInfo.hwndCaret != threadInfo.hwndFocus &&
        !IsChild(threadInfo.hwndFocus, threadInfo.hwndCaret))
    {
        return FALSE;  // someone else's caret
    }
    POINT pt = {threadInfo.rcCaret.left, threadInfo.rcCaret.bottom};
    if (!ClientToScreen(threadInfo.hwndCaret, &pt))
    {
        return FALSE;
    }
    *ppt = pt;
    return TRUE;
}

void CSampleIME::_FlashModeIndicatorAt(const RECT *prcCaret)
{
    POINT pt = {};
    BOOL havePoint = FALSE;

    // A caret has HEIGHT. Zero width is normal -- an empty selection is a
    // bar -- but a rectangle with no height is not a caret, it is a host
    // answering the question without having one.
    //
    // Measured 2026-09-09: clicking the desktop or the taskbar focuses a
    // context that is not marked keyboard-disabled (the desktop's
    // SysListView32 and the Windows 11 taskbar's InputSite both take
    // type-ahead), and GetTextExt then succeeds with the same degenerate
    // rect every time -- (1919,1031)-(1920,1031), one pixel wide, no
    // height, parked in the bottom-right corner of the work area. That is
    // what put a bubble in the corner of the screen for clicks on the
    // wallpaper and on the tray.
    if (prcCaret != nullptr && prcCaret->bottom > prcCaret->top)
    {
        pt.x = prcCaret->left;
        pt.y = prcCaret->bottom;
        havePoint = TRUE;
        // What a stale host answer will echo next time (see the focus
        // section above). The Shift tap measures the caret the user has
        // been typing at, so this is kept up to date by the trustworthy
        // path as well as by the focus one.
        _lastCaretRc = *prcCaret;
        _haveLastCaretRc = TRUE;
    }

    if (!havePoint)
    {
        havePoint = SystemCaretPoint(&pt);
    }

    if (!havePoint)
    {
        return;  // no caret to label
    }

    BOOL isOpen = _rememberedKeyboardOpen;
    if (_pThreadMgr != nullptr && _tfClientId != TF_CLIENTID_NULL)
    {
        CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        BOOL compartmentValue = FALSE;
        if (SUCCEEDED(CompartmentKeyboardOpen._GetCompartmentBOOL(compartmentValue)))
        {
            isOpen = compartmentValue;
        }
    }

    _modeIndicator.Flash(isOpen, pt);
}

//+---------------------------------------------------------------------------
//
// ITfTextInputProcessorEx::Deactivate
//
//----------------------------------------------------------------------------

STDAPI CSampleIME::Deactivate()
{
    if (_pCompositionProcessorEngine)
    {
        delete _pCompositionProcessorEngine;
        _pCompositionProcessorEngine = nullptr;
    }

    ITfContext* pContext = _pContext;
    if (_pContext)
    {   
        pContext->AddRef();
        _EndComposition(_pContext);
    }

    if (_pCandidateListUIPresenter)
    {
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;

        if (pContext)
        {
            pContext->Release();
        }

        _candidateMode = CANDIDATE_NONE;
        _isCandidateWithWildcard = FALSE;
    }

    // [MspyIME] The bubble's window and timers belong to this thread; tear
    // them down here rather than leaving them to the destructor, which COM
    // may run later and elsewhere.
    _modeIndicator.Destroy();

    _UninitFunctionProviderSink();

    _UninitThreadFocusSink();

    _UninitActiveLanguageProfileNotifySink();

    _UninitKeyEventSink();

    _UninitThreadMgrEventSink();

    CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    CompartmentKeyboardOpen._ClearCompartment();

    CCompartment CompartmentDoubleSingleByte(_pThreadMgr, _tfClientId, Global::SampleIMEGuidCompartmentDoubleSingleByte);
    CompartmentDoubleSingleByte._ClearCompartment();

    CCompartment CompartmentPunctuation(_pThreadMgr, _tfClientId, Global::SampleIMEGuidCompartmentPunctuation);
    CompartmentDoubleSingleByte._ClearCompartment();

    if (_pThreadMgr != nullptr)
    {
        _pThreadMgr->Release();
    }

    _tfClientId = TF_CLIENTID_NULL;

    if (_pDocMgrLastFocused)
    {
        _pDocMgrLastFocused->Release();
		_pDocMgrLastFocused = nullptr;
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::GetType
//
//----------------------------------------------------------------------------
HRESULT CSampleIME::GetType(__RPC__out GUID *pguid)
{
    HRESULT hr = E_INVALIDARG;
    if (pguid)
    {
        *pguid = Global::SampleIMECLSID;
        hr = S_OK;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::::GetDescription
//
//----------------------------------------------------------------------------
HRESULT CSampleIME::GetDescription(__RPC__deref_out_opt BSTR *pbstrDesc)
{
    HRESULT hr = E_INVALIDARG;
    if (pbstrDesc != nullptr)
    {
        *pbstrDesc = nullptr;
        hr = E_NOTIMPL;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::::GetFunction
//
//----------------------------------------------------------------------------
HRESULT CSampleIME::GetFunction(__RPC__in REFGUID rguid, __RPC__in REFIID riid, __RPC__deref_out_opt IUnknown **ppunk)
{
    HRESULT hr = E_NOINTERFACE;

    if ((IsEqualGUID(rguid, GUID_NULL)) 
        && (IsEqualGUID(riid, __uuidof(ITfFnSearchCandidateProvider))))
    {
        hr = _pITfFnSearchCandidateProvider->QueryInterface(riid, (void**)ppunk);
    }
    else if (IsEqualGUID(rguid, GUID_NULL))
    {
        hr = QueryInterface(riid, (void **)ppunk);
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunction::GetDisplayName
//
//----------------------------------------------------------------------------
HRESULT CSampleIME::GetDisplayName(_Out_ BSTR *pbstrDisplayName)
{
    HRESULT hr = E_INVALIDARG;
    if (pbstrDisplayName != nullptr)
    {
        *pbstrDisplayName = nullptr;
        hr = E_NOTIMPL;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFnGetPreferredTouchKeyboardLayout::GetLayout
// The tkblayout will be Optimized layout.
//----------------------------------------------------------------------------
HRESULT CSampleIME::GetLayout(_Out_ TKBLayoutType *ptkblayoutType, _Out_ WORD *pwPreferredLayoutId)
{
    HRESULT hr = E_INVALIDARG;
    if ((ptkblayoutType != nullptr) && (pwPreferredLayoutId != nullptr))
    {
        *ptkblayoutType = TKBLT_OPTIMIZED;
        *pwPreferredLayoutId = TKBL_OPT_SIMPLIFIED_CHINESE_PINYIN;
        hr = S_OK;
    }
    return hr;
}