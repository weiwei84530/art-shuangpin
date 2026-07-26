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
    // [MspyIME] Reset the composer too (Selecting -> Composing -> Empty).
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge && pBridge->IsReady())
    {
        pBridge->Composer()->feedEsc();
        pBridge->Composer()->feedEsc();
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
        CStringRange commitRange;
        commitRange.Set(commitText.c_str(), commitText.length());
        _AddComposingAndChar(ec, pContext, &commitRange);
        _DestroyCandidatePresenter();
        _TerminateComposition(ec, pContext);
        // The composer is Empty after producing commit text.
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
    const std::wstring& composedText = pBridge->ComposedText();
    CStringRange composedRange;
    composedRange.Set(composedText.c_str(), composedText.length());
    HRESULT hr = _AddComposingAndChar(ec, pContext, &composedRange);
    if (FAILED(hr))
    {
        return hr;
    }

    // Two-tone rendering: tone-settled head in plain black ("converted"),
    // still-retrofittable tail in blue underline ("input").
    const std::wstring& tail = pBridge->UnconfirmedTail();
    _SetCompositionDisplayAttributesSplit(ec, pContext, (LONG)tail.length());

    if (state == mspy::Composer::State::kSelecting)
    {
        hr = _CreateAndStartCandidate(_pCompositionProcessorEngine, ec, pContext);
        if (SUCCEEDED(hr) && _pCandidateListUIPresenter)
        {
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
                hr = _pCandidateListUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange, pCompositionProcessorEngine->GetCandidateWindowWidth());
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
    // [MspyIME] Down arrow: open the candidate window at the cursor span.
    isWildcardSearch;
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge == nullptr || !pBridge->IsReady())
    {
        return S_OK;
    }
    mspy::Composer::Result result = pBridge->Composer()->feedDown();
    return _SyncComposer(ec, pContext, result.commitText.c_str());
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
    // [MspyIME] v0.1: Left/Right are eaten and ignored while composing so
    // the caret cannot escape the composition. (Cursor movement inside the
    // buffer is a later feature.)
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
