// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#pragma once
#include "resource.h"

#define TEXTSERVICE_MODEL        L"Apartment"
// [MspyIME] zh-TW (0x0404): this IME lives under the user's Traditional
// Chinese (Taiwan) language so no extra system language is needed.
#define TEXTSERVICE_LANGID       MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)
#define TEXTSERVICE_ICON_INDEX   -IDIS_SAMPLEIME
#define TEXTSERVICE_DIC L"SampleIMESimplifiedQuanPin.txt"

#define IME_MODE_ON_ICON_INDEX      IDI_IME_MODE_ON
#define IME_MODE_OFF_ICON_INDEX     IDI_IME_MODE_OFF
#define IME_DOUBLE_ON_INDEX         IDI_DOUBLE_SINGLE_BYTE_ON
#define IME_DOUBLE_OFF_INDEX        IDI_DOUBLE_SINGLE_BYTE_OFF
#define IME_PUNCTUATION_ON_INDEX    IDI_PUNCTUATION_ON
#define IME_PUNCTUATION_OFF_INDEX   IDI_PUNCTUATION_OFF

// [MspyIME] Traditional-Chinese UI font.
#define SAMPLEIME_FONT_DEFAULT L"Microsoft JhengHei UI"

//---------------------------------------------------------------------
// defined Candidated Window
// [MspyIME] Modern light theme: white card, thin border, soft accent
// highlight for the numbered selection, gray digits, page indicator.
//---------------------------------------------------------------------
#define CANDWND_ROW_WIDTH				(34)
#define CANDWND_BORDER_COLOR			(RGB(0xD8, 0xD8, 0xD8))
#define CANDWND_BORDER_WIDTH			(1)
#define CANDWND_NUM_COLOR				(RGB(0x9A, 0x9A, 0x9A))
#define CANDWND_SELECTED_ITEM_COLOR		(RGB(0x1A, 0x1A, 0x1A))
#define CANDWND_SELECTED_BK_COLOR		(RGB(0xE3, 0xEE, 0xFC))
#define CANDWND_ITEM_COLOR				(RGB(0x20, 0x20, 0x20))
#define CANDWND_BK_COLOR				(RGB(0xFF, 0xFF, 0xFF))
#define CANDWND_PAGE_COLOR				(RGB(0xA0, 0xA0, 0xA0))
#define CANDWND_PAGEBAR_HEIGHT			(20)

//---------------------------------------------------------------------
// defined modifier
//---------------------------------------------------------------------
#define _TF_MOD_ON_KEYUP_SHIFT_ONLY    (0x00010000 | TF_MOD_ON_KEYUP)
#define _TF_MOD_ON_KEYUP_CONTROL_ONLY  (0x00020000 | TF_MOD_ON_KEYUP)
#define _TF_MOD_ON_KEYUP_ALT_ONLY      (0x00040000 | TF_MOD_ON_KEYUP)

#define CAND_WIDTH     (13)      // * tmMaxCharWidth

//---------------------------------------------------------------------
// string length of CLSID
//---------------------------------------------------------------------
#define CLSID_STRLEN    (38)  // strlen("{xxxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxx}")