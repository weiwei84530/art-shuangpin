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
    // A live reconversion session dissolves before any composer mirroring:
    // its composition must never be treated as the composer's (in
    // particular _RemoveDummyCompositionForComposing would wipe the
    // document text the reconversion range covers).
    if (_isReconverting)
    {
        _EndReconversion(ec, pContext, TRUE);
    }

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
            _RefreshCandidateWindowTexts(
                pBridge->CandidateTexts(),
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
                    LONG total, start, len;
                    if (_isReconverting)
                    {
                        // Reconversion: the anchor is the trailing code
                        // point of the covered document text.
                        total = (LONG)_reconvText.length();
                        len = _reconvAnchorUnits;
                        start = total - len;
                    }
                    else
                    {
                        const CMspyBridge::Segments& segments = pBridge->GetSegments();
                        total = (LONG)segments.FullText().length();
                        start = (LONG)(segments.before.length() + segments.unconfirmed.length());
                        len = (LONG)segments.highlighted.length();
                    }
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

namespace
{

// True if the code point reads as Chinese for separator purposes: Han
// ideographs (incl. extensions/compat) and bopomofo symbols.
bool IsCjkCodePoint(UINT32 cp)
{
    return (cp >= 0x4E00 && cp <= 0x9FFF) ||    // CJK Unified
           (cp >= 0x3400 && cp <= 0x4DBF) ||    // Ext A
           (cp >= 0xF900 && cp <= 0xFAFF) ||    // compat ideographs
           (cp >= 0x3105 && cp <= 0x312F) ||    // bopomofo
           (cp >= 0x20000 && cp <= 0x3FFFF);    // Ext B..F
}

// Decodes the LAST code point of a UTF-16 tail (cch valid units in buf).
UINT32 LastCodePoint(const WCHAR* buf, ULONG cch)
{
    if (cch == 0)
    {
        return 0;
    }
    WCHAR last = buf[cch - 1];
    if (last >= 0xDC00 && last <= 0xDFFF && cch >= 2 &&
        buf[cch - 2] >= 0xD800 && buf[cch - 2] <= 0xDBFF)
    {
        return 0x10000 + (((UINT32)(buf[cch - 2] - 0xD800)) << 10) +
               (last - 0xDC00);
    }
    return last;
}

// Reads up to two UTF-16 units immediately left of the caret. Returns the
// fetched count (0 when the document is empty or refuses read access).
ULONG GetTextBeforeCaret(TfEditCookie ec, _In_ ITfContext *pContext,
                         _Out_writes_(2) WCHAR *buf)
{
    ULONG fetched = 0;
    TF_SELECTION tfSelection;
    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched) != S_OK || fetched == 0)
    {
        return 0;
    }

    ULONG cch = 0;
    ITfRange* pRange = nullptr;
    if (SUCCEEDED(tfSelection.range->Clone(&pRange)))
    {
        LONG shifted = 0;
        pRange->Collapse(ec, TF_ANCHOR_START);
        pRange->ShiftStart(ec, -2, &shifted, nullptr);
        if (FAILED(pRange->GetText(ec, 0, buf, 2, &cch)))
        {
            cch = 0;
        }
        pRange->Release();
    }
    tfSelection.range->Release();
    return cch;
}

// [MspyIME] Reads up to maxBefore UTF-16 units left of the caret and up to
// maxAfter units right of it (reconversion context). Returns FALSE when
// the selection is not an empty caret or the document refuses reads.
BOOL GetTextAroundCaret(TfEditCookie ec, _In_ ITfContext *pContext,
                        _Out_writes_(maxBefore) WCHAR *before, ULONG maxBefore, _Out_ ULONG *cchBefore,
                        _Out_writes_(maxAfter) WCHAR *after, ULONG maxAfter, _Out_ ULONG *cchAfter)
{
    *cchBefore = 0;
    *cchAfter = 0;

    ULONG fetched = 0;
    TF_SELECTION tfSelection;
    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched) != S_OK || fetched == 0)
    {
        return FALSE;
    }

    BOOL isEmpty = TRUE;
    if (SUCCEEDED(tfSelection.range->IsEmpty(ec, &isEmpty)) && !isEmpty)
    {
        tfSelection.range->Release();
        return FALSE;
    }

    ITfRange* pRange = nullptr;
    if (SUCCEEDED(tfSelection.range->Clone(&pRange)))
    {
        LONG shifted = 0;
        pRange->Collapse(ec, TF_ANCHOR_START);
        pRange->ShiftStart(ec, -(LONG)maxBefore, &shifted, nullptr);
        if (FAILED(pRange->GetText(ec, 0, before, maxBefore, cchBefore)))
        {
            *cchBefore = 0;
        }
        pRange->Release();
    }
    if (SUCCEEDED(tfSelection.range->Clone(&pRange)))
    {
        LONG shifted = 0;
        pRange->Collapse(ec, TF_ANCHOR_END);
        pRange->ShiftEnd(ec, (LONG)maxAfter, &shifted, nullptr);
        if (FAILED(pRange->GetText(ec, 0, after, maxAfter, cchAfter)))
        {
            *cchAfter = 0;
        }
        pRange->Release();
    }
    tfSelection.range->Release();
    return TRUE;
}

// [MspyIME] The LAST up-to-maxCps code points of a UTF-16 buffer, in
// document order, surrogate-aware. A buffer whose head was cut mid-pair by
// a fixed-unit backward shift is safe: collection walks from the tail.
std::vector<std::wstring> TrailingCodePoints(const WCHAR* buf, ULONG cch, size_t maxCps)
{
    std::vector<std::wstring> cps;
    ULONG i = cch;
    while (i > 0 && cps.size() < maxCps)
    {
        ULONG start = i - 1;
        if (start > 0 && buf[start] >= 0xDC00 && buf[start] <= 0xDFFF &&
            buf[start - 1] >= 0xD800 && buf[start - 1] <= 0xDBFF)
        {
            --start;
        }
        cps.insert(cps.begin(), std::wstring(buf + start, i - start));
        i = start;
    }
    return cps;
}

// [MspyIME] The first code point of a UTF-16 buffer ("" when empty).
std::wstring LeadingCodePoint(const WCHAR* buf, ULONG cch)
{
    if (cch == 0)
    {
        return std::wstring();
    }
    if (cch >= 2 && buf[0] >= 0xD800 && buf[0] <= 0xDBFF &&
        buf[1] >= 0xDC00 && buf[1] <= 0xDFFF)
    {
        return std::wstring(buf, 2);
    }
    return std::wstring(buf, 1);
}

// [MspyIME] UTF-16 units spanned by the trailing `cps` code points.
LONG TrailingUnits(const std::wstring& text, size_t cps)
{
    size_t i = text.length();
    while (cps > 0 && i > 0)
    {
        --i;
        if (i > 0 && text[i] >= 0xDC00 && text[i] <= 0xDFFF &&
            text[i - 1] >= 0xD800 && text[i - 1] <= 0xDBFF)
        {
            --i;
        }
        --cps;
    }
    return (LONG)(text.length() - i);
}

// [MspyIME] True when the single-code-point string reads as Chinese.
bool IsCjkCp(const std::wstring& cp)
{
    return !cp.empty() && IsCjkCodePoint(LastCodePoint(cp.c_str(), (ULONG)cp.length()));
}

}  // namespace

HRESULT CSampleIME::_HandleShiftTap(TfEditCookie ec, _In_ ITfContext *pContext)
{
    // The separator space is decided purely from the character left of the
    // caret at the moment of the switch (no typing-history state):
    //   switching to English: left char is Chinese      -> insert one space
    //   switching to Chinese: left char is an A-Z letter -> insert one space
    //   anything else (space, digits, punctuation, empty, unreadable): none.
    // A live composition commits first; its last character then plays the
    // "left of caret" role.
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge == nullptr || !pBridge->IsReady())
    {
        return S_OK;
    }

    BOOL isOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);

    std::string commit;
    bool addSpace = false;
    if (isOpen)
    {
        // Chinese -> English. feedEnter also closes the candidate menu.
        mspy::Composer::Result result = pBridge->Composer()->feedEnter();
        commit = result.commitText;
        if (!commit.empty())
        {
            std::wstring wide = CMspyBridge::ToWide(commit);
            addSpace = IsCjkCodePoint(LastCodePoint(wide.c_str(), (ULONG)wide.length()));
        }
        else
        {
            WCHAR before[2] = {L'\0', L'\0'};
            ULONG cch = GetTextBeforeCaret(ec, pContext, before);
            addSpace = IsCjkCodePoint(LastCodePoint(before, cch));
        }
    }
    else
    {
        // English -> Chinese. Digits belong to the English class.
        WCHAR before[2] = {L'\0', L'\0'};
        ULONG cch = GetTextBeforeCaret(ec, pContext, before);
        UINT32 cp = LastCodePoint(before, cch);
        addSpace = (cp >= L'A' && cp <= L'Z') || (cp >= L'a' && cp <= L'z') ||
                   (cp >= L'0' && cp <= L'9');
    }

    if (addSpace)
    {
        commit += " ";
    }
    _SyncComposer(ec, pContext, commit.c_str());

    CompartmentKeyboardOpen._SetCompartmentBOOL(isOpen ? FALSE : TRUE);
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleNumpadCommit    [MspyIME]
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleNumpadCommit(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
{
    // Numpad keys are exempt from the top-row digit ban. While composing,
    // commit the whole buffer first (a menu is dismissed by feedEnter),
    // then emit the numpad character literally after it.
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge == nullptr || !pBridge->IsReady())
    {
        return S_OK;
    }

    mspy::Composer::Result result = pBridge->Composer()->feedEnter();
    std::string commit = result.commitText;
    if (wch >= 0x20 && wch < 0x7F)
    {
        commit += static_cast<char>(wch);
    }
    return _SyncComposer(ec, pContext, commit.c_str());
}

//+---------------------------------------------------------------------------
//
// _RefreshCandidateWindowTexts    [MspyIME]
//
//----------------------------------------------------------------------------

void CSampleIME::_RefreshCandidateWindowTexts(const std::vector<std::wstring>& texts, UINT page, UINT pageCount)
{
    if (_pCandidateListUIPresenter == nullptr)
    {
        return;
    }
    CSampleImeArray<CCandidateListItem> items;
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
    _pCandidateListUIPresenter->_SetPageStatus(page, pageCount);
}

//+---------------------------------------------------------------------------
//
// _HandleReconversionStart    [MspyIME]
//
// Idle digit 8: open a homophone menu over the committed Chinese text
// around the caret (spec §6 重選字). A composition is started OVER the
// existing characters; nothing is mutated until a candidate is picked, so
// every failure path is a silent no-op with the key eaten.
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleReconversionStart(TfEditCookie ec, _In_ ITfContext *pContext)
{
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge == nullptr || !pBridge->IsReady())
    {
        return S_OK;
    }
    mspy::Reconverter* pReconverter = pBridge->Reconverter();
    if (pReconverter == nullptr || _IsComposing() || _isReconverting)
    {
        return S_OK;
    }

    // Up to 3 code points left of the caret and 1 right of it.
    WCHAR before[6] = {L'\0'};
    WCHAR after[2] = {L'\0'};
    ULONG cchBefore = 0, cchAfter = 0;
    if (!GetTextAroundCaret(ec, pContext, before, ARRAYSIZE(before), &cchBefore,
                            after, ARRAYSIZE(after), &cchAfter))
    {
        return S_OK;
    }

    std::vector<std::wstring> beforeCps = TrailingCodePoints(before, cchBefore, 3);
    const std::wstring rightCp = LeadingCodePoint(after, cchAfter);

    // Anchor = the code point right of the caret when it is Han, else the
    // one left of it; the span then extends left over up to 2 more
    // contiguous Han code points.
    std::wstring anchorCp;
    LONG afterUnits = 0;
    size_t leftAvailable = beforeCps.size();
    if (IsCjkCp(rightCp))
    {
        anchorCp = rightCp;
        afterUnits = (LONG)rightCp.length();
    }
    else if (!beforeCps.empty() && IsCjkCp(beforeCps.back()))
    {
        anchorCp = beforeCps.back();
        leftAvailable = beforeCps.size() - 1;
    }
    else
    {
        return S_OK;  // no Chinese next to the caret
    }

    size_t take = 0;
    while (take < 2 && take < leftAvailable &&
           IsCjkCp(beforeCps[leftAvailable - 1 - take]))
    {
        ++take;
    }

    std::vector<std::wstring> contextCps;
    for (size_t i = leftAvailable - take; i < leftAvailable; ++i)
    {
        contextCps.push_back(beforeCps[i]);
    }
    contextCps.push_back(anchorCp);

    std::vector<std::string> contextUtf8;
    for (const std::wstring& cp : contextCps)
    {
        contextUtf8.push_back(CMspyBridge::ToUtf8(cp));
    }
    if (!pReconverter->start(contextUtf8))
    {
        return S_OK;  // nothing to offer
    }

    std::wstring spanText;
    for (const std::wstring& cp : contextCps)
    {
        spanText += cp;
    }
    const LONG leftUnits = (LONG)spanText.length() - afterUnits;

    // Build the span range from the caret and start a composition over it.
    ULONG fetched = 0;
    TF_SELECTION tfSelection;
    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched) != S_OK || fetched == 0)
    {
        pReconverter->dismiss();
        return S_OK;
    }
    ITfRange* pSpan = nullptr;
    HRESULT hr = tfSelection.range->Clone(&pSpan);
    tfSelection.range->Release();
    if (FAILED(hr))
    {
        pReconverter->dismiss();
        return S_OK;
    }

    LONG shifted = 0;
    pSpan->Collapse(ec, TF_ANCHOR_START);
    BOOL rangeOk = TRUE;
    if (leftUnits > 0)
    {
        pSpan->ShiftStart(ec, -leftUnits, &shifted, nullptr);
        rangeOk = (shifted == -leftUnits);
    }
    if (rangeOk && afterUnits > 0)
    {
        pSpan->ShiftEnd(ec, afterUnits, &shifted, nullptr);
        rangeOk = (shifted == afterUnits);
    }
    if (rangeOk)
    {
        ITfContextComposition* pContextComposition = nullptr;
        if (SUCCEEDED(pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition)))
        {
            ITfComposition* pComposition = nullptr;
            if (SUCCEEDED(pContextComposition->StartComposition(ec, pSpan, this, &pComposition)) && pComposition != nullptr)
            {
                _SetComposition(pComposition);
                _SaveCompositionContext(pContext);
            }
            pContextComposition->Release();
        }
    }
    pSpan->Release();
    if (!_IsComposing())
    {
        pReconverter->dismiss();
        return S_OK;
    }

    _isReconverting = TRUE;
    _reconvText = spanText;
    _reconvCaretOffsetUnits = leftUnits;
    _reconvAnchorUnits = (LONG)anchorCp.length();

    // Dashed underline over the span, anchor highlighted; the text itself
    // is untouched. The caret stays where it was (inside the range), so
    // the text-edit sink does not terminate the composition.
    _SetCompositionDisplayAttributesSplit(ec, pContext,
                                          (LONG)_reconvText.length() - _reconvAnchorUnits,
                                          0, _reconvAnchorUnits);

    hr = _CreateAndStartCandidate(_pCompositionProcessorEngine, ec, pContext);
    if (SUCCEEDED(hr) && _pCandidateListUIPresenter)
    {
        _RefreshCandidateWindowTexts(pBridge->ReconversionPageTexts(),
                                     (UINT)(pReconverter->pageIndex() + 1),
                                     (UINT)pReconverter->pageCount());
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleReconversionKey    [MspyIME]
//
//----------------------------------------------------------------------------

HRESULT CSampleIME::_HandleReconversionKey(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
{
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge == nullptr || !pBridge->IsReady())
    {
        return S_OK;
    }
    mspy::Reconverter* pReconverter = pBridge->Reconverter();
    if (pReconverter == nullptr || !pReconverter->active() || !_isReconverting)
    {
        return _EndReconversion(ec, pContext, TRUE);
    }

    // Non-printable keys (arrows, Esc, Backspace, ...) dismiss via '\0'.
    const char key = (wch >= 0x20 && wch < 0x7F) ? static_cast<char>(wch) : '\0';
    mspy::Reconverter::KeyResult result = pReconverter->feedKey(key);
    switch (result.action)
    {
    case mspy::Reconverter::Action::kNone:
        return S_OK;

    case mspy::Reconverter::Action::kPageChanged:
        _RefreshCandidateWindowTexts(pBridge->ReconversionPageTexts(),
                                     (UINT)(pReconverter->pageIndex() + 1),
                                     (UINT)pReconverter->pageCount());
        return S_OK;

    case mspy::Reconverter::Action::kSelected:
    {
        // Replace the trailing spanLength code points, commit in place.
        const std::wstring chosen = CMspyBridge::ToWide(result.selected.value);
        const LONG cutUnits = TrailingUnits(_reconvText, result.selected.spanLength);
        std::wstring newText = _reconvText.substr(0, _reconvText.length() - cutUnits);
        newText += chosen;
        _SetCompositionText(ec, pContext, newText.c_str(), (LONG)newText.length());
        _SetCaretInComposition(ec, pContext, (LONG)newText.length());
        _DestroyCandidatePresenter();
        _TerminateComposition(ec, pContext);
        _isReconverting = FALSE;
        _reconvText.clear();
        return S_OK;
    }

    case mspy::Reconverter::Action::kDismissed:
    default:
        return _EndReconversion(ec, pContext, TRUE);
    }
}

//+---------------------------------------------------------------------------
//
// _EndReconversion    [MspyIME]
//
// Tears the session down leaving the document text untouched.
//----------------------------------------------------------------------------

HRESULT CSampleIME::_EndReconversion(TfEditCookie ec, _In_ ITfContext *pContext, BOOL restoreCaret)
{
    if (!_isReconverting)
    {
        return S_OK;
    }
    _isReconverting = FALSE;  // clear first: _SyncComposer guards on this

    if (restoreCaret && _IsComposing())
    {
        _SetCaretInComposition(ec, pContext, _reconvCaretOffsetUnits);
    }
    _DestroyCandidatePresenter();
    if (_IsComposing())
    {
        _TerminateComposition(ec, pContext);
    }
    CMspyBridge* pBridge = _pCompositionProcessorEngine->GetBridge();
    if (pBridge != nullptr && pBridge->IsReady() && pBridge->Reconverter() != nullptr)
    {
        pBridge->Reconverter()->dismiss();
    }
    _reconvText.clear();
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
