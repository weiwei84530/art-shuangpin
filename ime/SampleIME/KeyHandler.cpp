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
        _RememberCommittedTail(commitText);
        _SaveLastCommitCaret(ec, pContext);
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

}  // namespace

HRESULT CSampleIME::_HandleShiftTap(TfEditCookie ec, _In_ ITfContext *pContext)
{
    // Separator space, v4 (2026-07-29, passive memory): most hosts never
    // let an IME read the document (only full TSF apps do), so the decision
    // uses only what the IME itself knows — _lastCharClass, maintained from
    // our own commits and from keys watched passing through to the app:
    //   switching to English: last known char is Chinese     -> one space
    //   switching to Chinese: last known char is A-Z/a-z/0-9 -> one space
    //   anything else (space, punctuation, Unknown): none.
    // A live composition commits first; its tail then plays that role.
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
            // The memory is only trustworthy while the caret still sits
            // where our last commit left it; in hosts that never report
            // caret moves (no selection-changed OnEndEdit), this compare is
            // the only way to notice a mouse repositioning.
            addSpace = (_lastCharClass == LASTCHAR_CHINESE) &&
                       _IsCaretAtLastCommit(ec, pContext);
        }
    }
    else
    {
        // English -> Chinese. Digits belong to the English class.
        addSpace = (_lastCharClass == LASTCHAR_ENGLISH);
    }

    if (addSpace)
    {
        commit += " ";
    }
    // The commit path also refreshes _lastCharClass from the commit tail
    // (a lone separator space classifies as Other, which is correct).
    _SyncComposer(ec, pContext, commit.c_str());

    const BOOL nowOpen = isOpen ? FALSE : TRUE;
    CompartmentKeyboardOpen._SetCompartmentBOOL(nowOpen);
    // This application now remembers the mode it was switched to.
    _RememberKeyboardOpen(nowOpen);
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _RememberCommittedTail / _ObserveBypassedKey    [MspyIME]
//
// The two feeders of _lastCharClass (see SampleIME.h).
//
//----------------------------------------------------------------------------

void CSampleIME::_SaveLastCommitCaret(TfEditCookie ec, _In_ ITfContext *pContext)
{
    _ClearLastCommitCaret();

    ULONG fetched = 0;
    TF_SELECTION tfSelection;
    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched) != S_OK || fetched == 0)
    {
        return;
    }
    ITfRange* pClone = nullptr;
    if (SUCCEEDED(tfSelection.range->Clone(&pClone)))
    {
        pClone->Collapse(ec, TF_ANCHOR_END);
        _pLastCommitCaret = pClone;
        pContext->AddRef();
        _pLastCommitContext = pContext;
    }
    tfSelection.range->Release();
}

BOOL CSampleIME::_IsCaretAtLastCommit(TfEditCookie ec, _In_ ITfContext *pContext)
{
    // Conservative: any doubt (no snapshot, other context, non-empty
    // selection, failed compare) counts as "moved".
    if (_pLastCommitCaret == nullptr || _pLastCommitContext != pContext)
    {
        return FALSE;
    }

    ULONG fetched = 0;
    TF_SELECTION tfSelection;
    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched) != S_OK || fetched == 0)
    {
        return FALSE;
    }

    BOOL isEmpty = FALSE;
    LONG cmp = 1;
    HRESULT hr = tfSelection.range->IsEmpty(ec, &isEmpty);
    if (SUCCEEDED(hr) && isEmpty)
    {
        hr = tfSelection.range->CompareStart(ec, _pLastCommitCaret, TF_ANCHOR_START, &cmp);
    }
    tfSelection.range->Release();
    return SUCCEEDED(hr) && isEmpty && cmp == 0;
}

void CSampleIME::_RememberCommittedTail(const std::wstring& text)
{
    if (text.empty())
    {
        return;
    }
    UINT32 cp = LastCodePoint(text.c_str(), (ULONG)text.length());
    if (IsCjkCodePoint(cp))
    {
        _lastCharClass = LASTCHAR_CHINESE;
    }
    else if ((cp >= L'A' && cp <= L'Z') || (cp >= L'a' && cp <= L'z') ||
             (cp >= L'0' && cp <= L'9'))
    {
        _lastCharClass = LASTCHAR_ENGLISH;
    }
    else
    {
        _lastCharClass = LASTCHAR_OTHER;
    }
}

void CSampleIME::_ObserveBypassedKey(UINT code)
{
    // Bare modifiers say nothing (a bare Shift IS the toggle trigger).
    switch (code)
    {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN: case VK_CAPITAL:
        return;
    default:
        break;
    }

    // Ctrl/Alt chords (paste, undo, app shortcuts) can put anything at the
    // caret: forget.
    if (Global::ModifiersValue & (TF_MOD_CONTROL | TF_MOD_LCONTROL | TF_MOD_RCONTROL |
                                  TF_MOD_ALT | TF_MOD_LALT | TF_MOD_RALT))
    {
        _lastCharClass = LASTCHAR_UNKNOWN;
        return;
    }

    const bool shiftHeld =
        (Global::ModifiersValue & (TF_MOD_SHIFT | TF_MOD_LSHIFT | TF_MOD_RSHIFT)) != 0;

    if ((code >= 'A' && code <= 'Z') || (code >= VK_NUMPAD0 && code <= VK_NUMPAD9))
    {
        _lastCharClass = LASTCHAR_ENGLISH;
        return;
    }
    if (code >= '0' && code <= '9')
    {
        // Shift+digit types punctuation ("!", "@", ...), not a digit.
        _lastCharClass = shiftHeld ? LASTCHAR_OTHER : LASTCHAR_ENGLISH;
        return;
    }

    switch (code)
    {
    case VK_SPACE:
        _lastCharClass = LASTCHAR_OTHER;
        return;

    // Caret moves, or edits whose result we cannot see: forget.
    case VK_RETURN: case VK_TAB: case VK_BACK: case VK_DELETE:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_ESCAPE:
        _lastCharClass = LASTCHAR_UNKNOWN;
        return;

    default:
        break;
    }

    // Non-typing keys (F1-F24, browser/media/IME keys) change nothing.
    if ((code >= VK_F1 && code <= VK_F24) ||
        (code >= VK_BROWSER_BACK && code <= VK_LAUNCH_APP2) ||
        code == VK_INSERT || code == VK_SNAPSHOT || code == VK_APPS ||
        code == VK_NUMLOCK || code == VK_SCROLL)
    {
        return;
    }

    // Everything else that reaches the app is punctuation-like output
    // (OEM keys and their Shift variants).
    _lastCharClass = LASTCHAR_OTHER;
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
