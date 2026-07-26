// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "Private.h"
#include "globals.h"
#include "SampleIME.h"

//+---------------------------------------------------------------------------
//
// _ClearCompositionDisplayAttributes
//
//----------------------------------------------------------------------------

void CSampleIME::_ClearCompositionDisplayAttributes(TfEditCookie ec, _In_ ITfContext *pContext)
{
    ITfRange* pRangeComposition = nullptr;
    ITfProperty* pDisplayAttributeProperty = nullptr;

    // get the compositon range.
    if (FAILED(_pComposition->GetRange(&pRangeComposition)))
    {
        return;
    }

    // get our the display attribute property
    if (SUCCEEDED(pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pDisplayAttributeProperty)))
    {
        // clear the value over the range
        pDisplayAttributeProperty->Clear(ec, pRangeComposition);

        pDisplayAttributeProperty->Release();
    }

    pRangeComposition->Release();
}

//+---------------------------------------------------------------------------
//
// _SetCompositionDisplayAttributes
//
//----------------------------------------------------------------------------

BOOL CSampleIME::_SetCompositionDisplayAttributes(TfEditCookie ec, _In_ ITfContext *pContext, TfGuidAtom gaDisplayAttribute)
{
    ITfRange* pRangeComposition = nullptr;
    ITfProperty* pDisplayAttributeProperty = nullptr;
    HRESULT hr = S_OK;

    // we need a range and the context it lives in
    hr = _pComposition->GetRange(&pRangeComposition);
    if (FAILED(hr))
    {
        return FALSE;
    }

    hr = E_FAIL;

    // get our the display attribute property
    if (SUCCEEDED(pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pDisplayAttributeProperty)))
    {
        VARIANT var;
        // set the value over the range
        // the application will use this guid atom to lookup the acutal rendering information
        var.vt = VT_I4; // we're going to set a TfGuidAtom
        var.lVal = gaDisplayAttribute; 

        hr = pDisplayAttributeProperty->SetValue(ec, pRangeComposition, &var);

        pDisplayAttributeProperty->Release();
    }

    pRangeComposition->Release();
    return (hr == S_OK);
}

//+---------------------------------------------------------------------------
//
// _SetCompositionDisplayAttributesSplit    [MspyIME]
//
//----------------------------------------------------------------------------

BOOL CSampleIME::_SetCompositionDisplayAttributesSplit(TfEditCookie ec, _In_ ITfContext *pContext, LONG inputStart, LONG inputLen, LONG anchorLen)
{
    ITfRange* pRangeComposition = nullptr;
    if (FAILED(_pComposition->GetRange(&pRangeComposition)))
    {
        return FALSE;
    }

    ITfProperty* pProperty = nullptr;
    HRESULT hr = E_FAIL;
    if (SUCCEEDED(pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pProperty)))
    {
        VARIANT var;
        var.vt = VT_I4;

        // Whole range: Converted (black, underlined) as the base coat.
        var.lVal = _gaDisplayAttributeConverted;
        hr = pProperty->SetValue(ec, pRangeComposition, &var);

        // Helper: overlay [start, start+len) with the given attribute atom.
        auto overlay = [&](LONG start, LONG len, TfGuidAtom atom)
        {
            if (len <= 0)
            {
                return;
            }
            ITfRange* pSegment = nullptr;
            if (SUCCEEDED(pRangeComposition->Clone(&pSegment)))
            {
                LONG shifted = 0;
                pSegment->Collapse(ec, TF_ANCHOR_START);
                pSegment->ShiftEnd(ec, start + len, &shifted, nullptr);
                pSegment->ShiftStart(ec, start, &shifted, nullptr);

                var.lVal = atom;
                hr = pProperty->SetValue(ec, pSegment, &var);
                pSegment->Release();
            }
        };

        // Unconfirmed segment in blue; the selection anchor (right after
        // the caret) with the highlight background.
        overlay(inputStart, inputLen, _gaDisplayAttributeInput);
        overlay(inputStart + inputLen, anchorLen, _gaDisplayAttributeAnchor);

        pProperty->Release();
    }

    pRangeComposition->Release();
    return (hr == S_OK);
}

//+---------------------------------------------------------------------------
//
// _SetCaretInComposition    [MspyIME]
//
//----------------------------------------------------------------------------

void CSampleIME::_SetCaretInComposition(TfEditCookie ec, _In_ ITfContext *pContext, LONG caretOffset)
{
    ITfRange* pRangeComposition = nullptr;
    if (FAILED(_pComposition->GetRange(&pRangeComposition)))
    {
        return;
    }

    ITfRange* pCaret = nullptr;
    if (SUCCEEDED(pRangeComposition->Clone(&pCaret)))
    {
        LONG shifted = 0;
        pCaret->Collapse(ec, TF_ANCHOR_START);
        pCaret->ShiftEnd(ec, caretOffset, &shifted, nullptr);
        pCaret->Collapse(ec, TF_ANCHOR_END);

        TF_SELECTION sel;
        sel.range = pCaret;
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        pContext->SetSelection(ec, 1, &sel);
        pCaret->Release();
    }
    pRangeComposition->Release();
}

//+---------------------------------------------------------------------------
//
// _InitDisplayAttributeGuidAtom
//
// Because it's expensive to map our display attribute GUID to a TSF
// TfGuidAtom, we do it once when Activate is called.
//----------------------------------------------------------------------------

BOOL CSampleIME::_InitDisplayAttributeGuidAtom()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&pCategoryMgr);

    if (FAILED(hr))
    {
        return FALSE;
    }

    // register the display attribute for input text.
    hr = pCategoryMgr->RegisterGUID(Global::SampleIMEGuidDisplayAttributeInput, &_gaDisplayAttributeInput);
	if (FAILED(hr))
    {
        goto Exit;
    }
    // register the display attribute for the converted text.
    hr = pCategoryMgr->RegisterGUID(Global::SampleIMEGuidDisplayAttributeConverted, &_gaDisplayAttributeConverted);
	if (FAILED(hr))
    {
        goto Exit;
    }
    // [MspyIME] register the selection-anchor highlight attribute.
    hr = pCategoryMgr->RegisterGUID(Global::SampleIMEGuidDisplayAttributeAnchor, &_gaDisplayAttributeAnchor);
	if (FAILED(hr))
    {
        goto Exit;
    }

Exit:
    pCategoryMgr->Release();

    return (hr == S_OK);
}
