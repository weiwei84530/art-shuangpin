// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "Private.h"
#include "Globals.h"
#include "EditSession.h"
#include "SampleIME.h"
#include "CandidateListUIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "Compartment.h" // [MspyIME]
#include "MspyBridge.h"  // [MspyIME]
#include <string>        // [MspyIME]

//////////////////////////////////////////////////////////////////////
//
// CSampleIME class
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// _IsRangeCovered
//
// Returns TRUE if pRangeTest is entirely contained within pRangeCover.
//
//----------------------------------------------------------------------------

BOOL CSampleIME::_IsRangeCovered(TfEditCookie ec, _In_ ITfRange *pRangeTest, _In_ ITfRange *pRangeCover)
{
    LONG lResult = 0;;

    if (FAILED(pRangeCover->CompareStart(ec, pRangeTest, TF_ANCHOR_START, &lResult)) 
        || (lResult > 0))
    {
        return FALSE;
    }

    if (FAILED(pRangeCover->CompareEnd(ec, pRangeTest, TF_ANCHOR_END, &lResult)) 
        || (lResult < 0))
    {
        return FALSE;
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _DeleteCandidateList
//
//----------------------------------------------------------------------------

VOID CSampleIME::_DeleteCandidateList(BOOL isForce, _In_opt_ ITfContext *pContext)
{
    isForce;pContext;

    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;
    pCompositionProcessorEngine->PurgeVirtualKey();

    if (_pCandidateListUIPresenter)
    {
        _pCandidateListUIPresenter->_EndCandidateList();

        _candidateMode = CANDIDATE_NONE;
        _isCandidateWithWildcard = FALSE;
    }
}

//+---------------------------------------------------------------------------
//
// _HandleComplete
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleComplete(TfEditCookie ec, _In_ ITfContext *pContext)
{
    _DeleteCandidateList(FALSE, pContext);

    // just terminate the composition
    _TerminateComposition(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCancel
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleCancel(TfEditCookie ec, _In_ ITfContext *pContext)
{
    // [MspyIME] Reset the composer too (any state -> Empty).
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge && pBridge->IsReady())
    {
        pBridge->Composer()->cancel();
    }

    _RemoveDummyCompositionForComposing(ec, _pComposition);

    _DeleteCandidateList(FALSE, pContext);

    _TerminateComposition(ec, pContext);

    return S_OK;
}

// [MspyIME]
void CSampleIME::_DestroyCandidatePresenter()
{
    if (_pCandidateListUIPresenter)
    {
        _pCandidateListUIPresenter->_EndCandidateList();
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;

        _candidateMode = CANDIDATE_NONE;
        _isCandidateWithWildcard = FALSE;
    }
}

// [MspyIME] Sets the composition range's text as a whole. The sample's
// _AddComposingAndChar resolves the target range from the current
// selection, which clips the range once the caret moves inside the
// composition (it duplicated the text right of the caret).
HRESULT CSampleIME::_SetCompositionText(TfEditCookie ec, _In_ ITfContext *pContext, const WCHAR* pText, LONG cchText)
{
    if (_pComposition == nullptr)
    {
        return E_FAIL;
    }
    ITfRange* pRange = nullptr;
    HRESULT hr = _pComposition->GetRange(&pRange);
    if (FAILED(hr))
    {
        return hr;
    }
    hr = pRange->SetText(ec, 0, pText, cchText);
    pRange->Release();
    _SetCompositionLanguage(ec, pContext);
    return hr;
}

// [MspyIME] Single place where the composer's state is reflected into TSF.
HRESULT CSampleIME::_SyncComposer(TfEditCookie ec, _In_ ITfContext *pContext, const char* commitUtf8)
{
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge == nullptr || !pBridge->IsReady())
    {
        return S_OK;
    }
    mspy::Composer* pComposer = pBridge->Composer();

    if (commitUtf8 != nullptr && commitUtf8[0] != '\0')
    {
        std::wstring commitText = CMspyBridge::ToWide(commitUtf8);
        if (!_IsComposing())
        {
            _StartComposition(pContext);
        }
        _SetCompositionText(ec, pContext, commitText.c_str(), (LONG)commitText.length());
        _SetCaretInComposition(ec, pContext, (LONG)commitText.length());
        _DestroyCandidatePresenter();
        _TerminateComposition(ec, pContext);
        // The composer is Empty after producing commit text.

        // Track document text for the Shift-tap separator logic.
        _typedSinceBoundary = TRUE;
        const char last = commitUtf8[strlen(commitUtf8) - 1];
        _lastCharWasSeparator = (last == ' ' || last == '\n');
    }

    const mspy::Composer::State state = pComposer->state();

    if (state == mspy::Composer::State::kEmpty)
    {
        _DestroyCandidatePresenter();
        if (_IsComposing())
        {
            _RemoveDummyCompositionForComposing(ec, _pComposition);
            _TerminateComposition(ec, pContext);
        }
        return S_OK;
    }

    // Composing or Selecting: refresh the inline composition text.
    if (!_IsComposing())
    {
        _StartComposition(pContext);
    }
    const CMspyBridge::Segments& segments = pBridge->GetSegments();
    const std::wstring composedText = segments.FullText();
    HRESULT hr = _SetCompositionText(ec, pContext, composedText.c_str(),
                                     (LONG)composedText.length());
    if (FAILED(hr))
    {
        return hr;
    }

    // Three-part rendering: tone-settled text in black (still underlined
    // until commit), the unconfirmed segment in blue, and the selection
    // anchor (char right of the cursor) with a highlight background.
    _SetCompositionDisplayAttributesSplit(ec, pContext,
                                          (LONG)segments.before.length(),
                                          (LONG)segments.unconfirmed.length(),
                                          (LONG)segments.highlighted.length());

    // The app caret follows the composer cursor (after the unconfirmed
    // segment) so mid-buffer editing is visible.
    _SetCaretInComposition(ec, pContext,
                           (LONG)(segments.before.length() + segments.unconfirmed.length()));

    if (state == mspy::Composer::State::kSelecting)
    {
        hr = _CreateAndStartCandidate(_pCompositionProcessorEngine, ec, pContext);
        if (SUCCEEDED(hr) && _pCandidateListUIPresenter)
        {
            // The composer owns paging: the window shows exactly the
            // current page (at most 6 entries) plus a page indicator.
            CSampleImeArray<CCandidateListItem> items;
            const std::vector<std::wstring>& texts = pBridge->CandidateTexts();
            for (const std::wstring& text : texts)
            {
                CCandidateListItem* pItem = items.Append();
                if (pItem)
                {
                    pItem->_ItemString.Set(text.c_str(), text.length());
                }
            }
            _pCandidateListUIPresenter->_ClearList();
            _pCandidateListUIPresenter->_SetText(&items, FALSE);
            _pCandidateListUIPresenter->_SetPageStatus(
                (UINT)(pBridge->Composer()->candidatePageIndex() + 1),
                (UINT)pBridge->Composer()->candidatePageCount());
        }
    }
    else
    {
        _DestroyCandidatePresenter();
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionInput
//
// If the keystroke happens within a composition, eat the key and return S_OK.
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleCompositionInput(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
{
    // [MspyIME] Feed the printable key into the modal composer and mirror
    // the outcome into TSF.
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge == nullptr || !pBridge->IsReady() || wch >= 0x80)
    {
        return S_OK;
    }

    if (!_IsComposing())
    {
        _StartComposition(pContext);
    }

    mspy::Composer::Result result = pBridge->Composer()->feedChar(static_cast<char>(wch));
    return _SyncComposer(ec, pContext, result.commitText.c_str());
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionInputWorker
//
// If the keystroke happens within a composition, eat the key and return S_OK.
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleCompositionInputWorker(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;
    CSampleImeArray<CStringRange> readingStrings;
    BOOL isWildcardIncluded = TRUE;

    //
    // Get reading string from composition processor engine
    //
    pCompositionProcessorEngine->GetReadingStrings(&readingStrings, &isWildcardIncluded);

    for (UINT index = 0; index < readingStrings.Count(); index++)
    {
        hr = _AddComposingAndChar(ec, pContext, readingStrings.GetAt(index));
        if (FAILED(hr))
        {
            return hr;
        }
    }

    //
    // Get candidate string from composition processor engine
    //
    CSampleImeArray<CCandidateListItem> candidateList;

    pCompositionProcessorEngine->GetCandidateList(&candidateList, TRUE, FALSE);

    Global::DebugLog(L"InputWorker: readings=%u candidates=%u",
                     readingStrings.Count(), candidateList.Count());

    if ((candidateList.Count()))
    {
        hr = _CreateAndStartCandidate(pCompositionProcessorEngine, ec, pContext);
        Global::DebugLog(L"CreateAndStartCandidate hr=0x%08X", hr);
        if (SUCCEEDED(hr))
        {
            _pCandidateListUIPresenter->_ClearList();
            _pCandidateListUIPresenter->_SetText(&candidateList, TRUE);
        }
    }
    else if (_pCandidateListUIPresenter)
    {
        _pCandidateListUIPresenter->_ClearList();
    }
    else if (readingStrings.Count() && isWildcardIncluded)
    {
        hr = _CreateAndStartCandidate(pCompositionProcessorEngine, ec, pContext);
        if (SUCCEEDED(hr))
        {
            _pCandidateListUIPresenter->_ClearList();
        }
    }
    return hr;
}
//+---------------------------------------------------------------------------
//
// _CreateAndStartCandidate
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_CreateAndStartCandidate(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;

    if (((_candidateMode == CANDIDATE_PHRASE) && (_pCandidateListUIPresenter))
        || ((_candidateMode == CANDIDATE_NONE) && (_pCandidateListUIPresenter)))
    {
        // Recreate candidate list
        _pCandidateListUIPresenter->_EndCandidateList();
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;

        _candidateMode = CANDIDATE_NONE;
        _isCandidateWithWildcard = FALSE;
    }

    if (_pCandidateListUIPresenter == nullptr)
    {
        _pCandidateListUIPresenter = new (std::nothrow) CCandidateListUIPresenter(this, Global::AtomCandidateWindow,
            CATEGORY_CANDIDATE,
            pCompositionProcessorEngine->GetCandidateListIndexRange(),
            FALSE);
        if (!_pCandidateListUIPresenter)
        {
            return E_OUTOFMEMORY;
        }

        _candidateMode = CANDIDATE_INCREMENTAL;
        _isCandidateWithWildcard = FALSE;

        // we don't cache the document manager object. So get it from pContext.
        ITfDocumentMgr* pDocumentMgr = nullptr;
        if (SUCCEEDED(pContext->GetDocumentMgr(&pDocumentMgr)))
        {
            // get the composition range.
            ITfRange* pRange = nullptr;
            if (SUCCEEDED(_pComposition->GetRange(&pRange)))
            {
                // [MspyIME] Track the selection-anchor character instead of
                // the whole composition, so the window opens under the char
                // being converted (MS-Bopomofo style), not under the start
                // of the sentence.
                ITfRange* pTrackRange = pRange;
                ITfRange* pAnchorRange = nullptr;
                CMspyBridge* pBridge = pCompositionProcessorEngine->GetBridge();
                if (pBridge != nullptr && pBridge->IsReady())
                {
                    const CMspyBridge::Segments& segments = pBridge->GetSegments();
                    LONG total = (LONG)segments.FullText().length();
                    LONG start = (LONG)(segments.before.length() + segments.unconfirmed.length());
                    LONG len = (LONG)segments.highlighted.length();
                    if (len == 0 && total > 0)
                    {
                        // Cursor at the right end: the menu targets the
                        // last character.
                        start = total - 1;
                        len = 1;
                    }
                    if (len > 0 && SUCCEEDED(pRange->Clone(&pAnchorRange)))
                    {
                        LONG shifted = 0;
                        pAnchorRange->Collapse(ec, TF_ANCHOR_START);
                        pAnchorRange->ShiftEnd(ec, start + len, &shifted, nullptr);
                        pAnchorRange->ShiftStart(ec, start, &shifted, nullptr);
                        pTrackRange = pAnchorRange;
                    }
                }

                hr = _pCandidateListUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pTrackRange, pCompositionProcessorEngine->GetCandidateWindowWidth());
                if (pAnchorRange)
                {
                    pAnchorRange->Release();
                }
                pRange->Release();
            }
            pDocumentMgr->Release();
        }
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionFinalize
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleCompositionFinalize(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isCandidateList)
{
    // [MspyIME] Enter commits the whole buffer through the composer.
    isCandidateList;
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge == nullptr || !pBridge->IsReady())
    {
        return S_OK;
    }
    mspy::Composer::Result result = pBridge->Composer()->feedEnter();
    return _SyncComposer(ec, pContext, result.commitText.c_str());
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionConvert
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleCompositionConvert(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isWildcardSearch)
{
    // [MspyIME] Unused since the menu moved to digit 8 (kept because the
    // sample's candidate plumbing still references this entry point).
    ec; pContext; isWildcardSearch;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionBackspace
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleCompositionBackspace(TfEditCookie ec, _In_ ITfContext *pContext)
{
    // [MspyIME]
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge == nullptr || !pBridge->IsReady())
    {
        return S_OK;
    }
    mspy::Composer::Result result = pBridge->Composer()->feedBackspace();
    return _SyncComposer(ec, pContext, result.commitText.c_str());
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionArrowKey
//
// Update the selection within a composition.
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleCompositionArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, KEYSTROKE_FUNCTION keyFunction)
{
    // [MspyIME] Cursor movement moved to digits 9/0; arrows are eaten as
    // no-ops upstream and never routed here anymore.
    ec; pContext; keyFunction;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionPunctuation
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleCompositionPunctuation(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
{
    // [MspyIME] Same path as normal input; the composer maps ,/. itself.
    return _HandleCompositionInput(ec, pContext, wch);
}

//+---------------------------------------------------------------------------
//
// _HandleShiftTap    [MspyIME]
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleShiftTap(TfEditCookie ec, _In_ ITfContext *pContext)
{
    // Chinese -> English: commit the live composition and append one
    // half-width separator space, then close the keyboard (taskbar shows
    // 英). English -> Chinese: emit a separator space if text was typed
    // since the last boundary, then reopen the keyboard. No text since the
    // boundary (or already ending in a space/newline) means no space.
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge == nullptr || !pBridge->IsReady())
    {
        return S_OK;
    }

    BOOL isOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);

    std::string commit;
    if (isOpen)
    {
        // feedEnter also closes the candidate menu if it is open.
        mspy::Composer::Result result = pBridge->Composer()->feedEnter();
        commit = result.commitText;
    }
    if (!commit.empty() || (_typedSinceBoundary && !_lastCharWasSeparator))
    {
        commit += " ";
    }
    _SyncComposer(ec, pContext, commit.c_str());

    CompartmentKeyboardOpen._SetCompartmentBOOL(isOpen ? FALSE : TRUE);

    // The mode switch itself is a boundary.
    _typedSinceBoundary = FALSE;
    _lastCharWasSeparator = TRUE;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionDoubleSingleByte
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleCompositionDoubleSingleByte(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
{
    HRESULT hr = S_OK;

    WCHAR fullWidth = Global::FullWidthCharTable[wch - 0x20];

    CStringRange fullWidthString;
    fullWidthString.Set(&fullWidth, 1);

    // Finalize character
    hr = _AddCharAndFinalize(ec, pContext, &fullWidthString);
    if (FAILED(hr))
    {
        return hr;
    }

    _HandleCancel(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _InvokeKeyHandler
//
// This text service is interested in handling keystrokes to demonstrate the
// use the compositions. Some apps will cancel compositions if they receive
// keystrokes while a compositions is ongoing.
//
// param
//    [in] uCode - virtual key code of WM_KEYDOWN wParam
//    [in] dwFlags - WM_KEYDOWN lParam
//    [in] dwKeyFunction - Function regarding virtual key
//----------------------------------------------------------------------------

HRESULT CSampleIME::_InvokeKeyHandler(_In_ ITfContext *pContext, UINT code, WCHAR wch, DWORD flags, _KEYSTROKE_STATE keyState)
{
    flags;

    CKeyHandlerEditSession* pEditSession = nullptr;
    HRESULT hr = E_FAIL;

    // we'll insert a char ourselves in place of this keystroke
    pEditSession = new (std::nothrow) CKeyHandlerEditSession(this, pContext, code, wch, keyState);
    if (pEditSession == nullptr)
    {
        goto Exit;
    }

    //
    // Call CKeyHandlerEditSession::DoEditSession().
    //
    // Do not specify TF_ES_SYNC so edit session is not invoked on WinWord
    //
    hr = pContext->RequestEditSession(_tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);

    pEditSession->Release();

Exit:
    return hr;
}
