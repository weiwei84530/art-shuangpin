// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "Private.h"
#include "resource.h"
#include "BaseWindow.h"
#include "define.h"
#include "SampleIMEBaseStructure.h"

// [MspyIME] Dev diagnostics: append a line to %TEMP%\MspyIME.debug.log.
#ifdef MSPY_DEBUG_LOG
#include <stdio.h>
#include <stdarg.h>
namespace Global {
void DebugLog(_In_ PCWSTR pwszFormat, ...)
{
    WCHAR path[MAX_PATH] = {L'\0'};
    if (!GetTempPathW(ARRAYSIZE(path), path)) return;
    wcscat_s(path, L"MspyIME.debug.log");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a, ccs=UTF-8") != 0 || f == nullptr) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(f, L"%02u:%02u:%02u.%03u [%lu] ", st.wHour, st.wMinute,
             st.wSecond, st.wMilliseconds, GetCurrentProcessId());
    va_list args;
    va_start(args, pwszFormat);
    vfwprintf(f, pwszFormat, args);
    va_end(args);
    fwprintf(f, L"\n");
    fclose(f);
}
}  // namespace Global
#else
namespace Global {
void DebugLog(_In_ PCWSTR, ...) {}
}  // namespace Global
#endif

namespace Global {
HINSTANCE dllInstanceHandle;

LONG dllRefCount = -1;

CRITICAL_SECTION CS;
HFONT defaultlFontHandle;				// Global font object we use everywhere

//---------------------------------------------------------------------
// SampleIME CLSID
//---------------------------------------------------------------------
// {22DFB512-5772-4938-9FF1-EE24B3904B74}
extern const CLSID SampleIMECLSID = {
    0x22dfb512,
    0x5772,
    0x4938,
    { 0x9f, 0xf1, 0xee, 0x24, 0xb3, 0x90, 0x4b, 0x74 }
};

//---------------------------------------------------------------------
// Profile GUID
//---------------------------------------------------------------------
// {96D6DDBE-6CF2-45C4-B4AE-A3F23251BAE9}
extern const GUID SampleIMEGuidProfile = {
    0x96d6ddbe,
    0x6cf2,
    0x45c4,
    { 0xb4, 0xae, 0xa3, 0xf2, 0x32, 0x51, 0xba, 0xe9 }
};

//---------------------------------------------------------------------
// PreserveKey GUID
//---------------------------------------------------------------------
// {18199D32-F54A-46C7-AACD-0094C569185A}
extern const GUID SampleIMEGuidImeModePreserveKey = {
    0x18199d32,
    0xf54a,
    0x46c7,
    { 0xaa, 0xcd, 0x00, 0x94, 0xc5, 0x69, 0x18, 0x5a }
};

// {5CCDBB6C-DA33-444A-AE83-4896ECEC782B}
extern const GUID SampleIMEGuidDoubleSingleBytePreserveKey = {
    0x5ccdbb6c,
    0xda33,
    0x444a,
    { 0xae, 0x83, 0x48, 0x96, 0xec, 0xec, 0x78, 0x2b }
};

// {2CAFEFE7-8DCB-4C75-B6E2-B0EAF9B61D5A}
extern const GUID SampleIMEGuidPunctuationPreserveKey = {
    0x2cafefe7,
    0x8dcb,
    0x4c75,
    { 0xb6, 0xe2, 0xb0, 0xea, 0xf9, 0xb6, 0x1d, 0x5a }
};

//---------------------------------------------------------------------
// Compartments
//---------------------------------------------------------------------
// {737D5763-BD70-4031-8727-32DBB625F042}
extern const GUID SampleIMEGuidCompartmentDoubleSingleByte = {
    0x737d5763,
    0xbd70,
    0x4031,
    { 0x87, 0x27, 0x32, 0xdb, 0xb6, 0x25, 0xf0, 0x42 }
};

// {B1370281-352C-4E97-B380-108B941C1749}
extern const GUID SampleIMEGuidCompartmentPunctuation = {
    0xb1370281,
    0x352c,
    0x4e97,
    { 0xb3, 0x80, 0x10, 0x8b, 0x94, 0x1c, 0x17, 0x49 }
};


//---------------------------------------------------------------------
// LanguageBars
//---------------------------------------------------------------------

// {160865BB-5724-44C6-B02F-10D8DA51D0F5}
extern const GUID SampleIMEGuidLangBarIMEMode = {
    0x160865bb,
    0x5724,
    0x44c6,
    { 0xb0, 0x2f, 0x10, 0xd8, 0xda, 0x51, 0xd0, 0xf5 }
};

// {94FE7F5E-1545-4E1F-806E-B7B7D64B43C7}
extern const GUID SampleIMEGuidLangBarDoubleSingleByte = {
    0x94fe7f5e,
    0x1545,
    0x4e1f,
    { 0x80, 0x6e, 0xb7, 0xb7, 0xd6, 0x4b, 0x43, 0xc7 }
};

// {C43123FB-8F93-4B3A-B844-A26B69B3AF8C}
extern const GUID SampleIMEGuidLangBarPunctuation = {
    0xc43123fb,
    0x8f93,
    0x4b3a,
    { 0xb8, 0x44, 0xa2, 0x6b, 0x69, 0xb3, 0xaf, 0x8c }
};

// {E0238C62-D64B-4382-BE07-9F89F115F4B8}
extern const GUID SampleIMEGuidDisplayAttributeInput = {
    0xe0238c62,
    0xd64b,
    0x4382,
    { 0xbe, 0x07, 0x9f, 0x89, 0xf1, 0x15, 0xf4, 0xb8 }
};

// {0AC5930E-1A59-4B1F-BBBB-8B9D255F10F8}
extern const GUID SampleIMEGuidDisplayAttributeConverted = {
    0x0ac5930e,
    0x1a59,
    0x4b1f,
    { 0xbb, 0xbb, 0x8b, 0x9d, 0x25, 0x5f, 0x10, 0xf8 }
};

// [MspyIME] selection anchor (char right of the cursor), highlighted bk
// {3A89ADB3-9FDF-40FB-AB77-601DB679562F}
extern const GUID SampleIMEGuidDisplayAttributeAnchor = {
    0x3a89adb3,
    0x9fdf,
    0x40fb,
    { 0xab, 0x77, 0x60, 0x1d, 0xb6, 0x79, 0x56, 0x2f }
};


//---------------------------------------------------------------------
// UI element
//---------------------------------------------------------------------

// {899AF1B5-7107-4A56-8071-1FE9AFF4B141}
extern const GUID SampleIMEGuidCandUIElement = {
    0x899af1b5,
    0x7107,
    0x4a56,
    { 0x80, 0x71, 0x1f, 0xe9, 0xaf, 0xf4, 0xb1, 0x41 }
};

//---------------------------------------------------------------------
// Unicode byte order mark
//---------------------------------------------------------------------
extern const WCHAR UnicodeByteOrderMark = 0xFEFF;

//---------------------------------------------------------------------
// dictionary table delimiter
//---------------------------------------------------------------------
extern const WCHAR KeywordDelimiter = L'=';
extern const WCHAR StringDelimiter  = L'\"';

//---------------------------------------------------------------------
// defined item in setting file table [PreservedKey] section
//---------------------------------------------------------------------
extern const WCHAR ImeModeDescription[] = L"Chinese/English input (Shift)";
extern const int ImeModeOnIcoIndex = IME_MODE_ON_ICON_INDEX;
extern const int ImeModeOffIcoIndex = IME_MODE_OFF_ICON_INDEX;

extern const WCHAR DoubleSingleByteDescription[] = L"Double/Single byte (Shift+Space)";
extern const int DoubleSingleByteOnIcoIndex = IME_DOUBLE_ON_INDEX;
extern const int DoubleSingleByteOffIcoIndex = IME_DOUBLE_OFF_INDEX;

extern const WCHAR PunctuationDescription[] = L"Chinese/English punctuation (Ctrl+.)";
extern const int PunctuationOnIcoIndex = IME_PUNCTUATION_ON_INDEX;
extern const int PunctuationOffIcoIndex = IME_PUNCTUATION_OFF_INDEX;

//---------------------------------------------------------------------
// defined item in setting file table [LanguageBar] section
//---------------------------------------------------------------------
extern const WCHAR LangbarImeModeDescription[] = L"Conversion mode";
extern const WCHAR LangbarDoubleSingleByteDescription[] = L"Character width";
extern const WCHAR LangbarPunctuationDescription[] = L"Punctuation";

//---------------------------------------------------------------------
// windows class / titile / atom
//---------------------------------------------------------------------
extern const WCHAR CandidateClassName[] = L"SampleIME.CandidateWindow";
ATOM AtomCandidateWindow;

extern const WCHAR ShadowClassName[] = L"SampleIME.ShadowWindow";
ATOM AtomShadowWindow;

extern const WCHAR ScrollBarClassName[] = L"SampleIME.ScrollBarWindow";
ATOM AtomScrollBarWindow;

BOOL RegisterWindowClass()
{
    if (!CBaseWindow::_InitWindowClass(CandidateClassName, &AtomCandidateWindow))
    {
        return FALSE;
    }
    if (!CBaseWindow::_InitWindowClass(ShadowClassName, &AtomShadowWindow))
    {
        return FALSE;
    }
    if (!CBaseWindow::_InitWindowClass(ScrollBarClassName, &AtomScrollBarWindow))
    {
        return FALSE;
    }
    return TRUE;
}

//---------------------------------------------------------------------
// defined full width characters for Double/Single byte conversion
//---------------------------------------------------------------------
extern const WCHAR FullWidthCharTable[] = {
    //         !       "       #       $       %       &       '       (    )       *       +       ,       -       .       /
    0x3000, 0xFF01, 0xFF02, 0xFF03, 0xFF04, 0xFF05, 0xFF06, 0xFF07, 0xFF08, 0xFF09, 0xFF0A, 0xFF0B, 0xFF0C, 0xFF0D, 0xFF0E, 0xFF0F,
    // 0       1       2       3       4       5       6       7       8       9       :       ;       <       =       >       ?
    0xFF10, 0xFF11, 0xFF12, 0xFF13, 0xFF14, 0xFF15, 0xFF16, 0xFF17, 0xFF18, 0xFF19, 0xFF1A, 0xFF1B, 0xFF1C, 0xFF1D, 0xFF1E, 0xFF1F,
    // @       A       B       C       D       E       F       G       H       I       J       K       L       M       N       0
    0xFF20, 0xFF21, 0xFF22, 0xFF23, 0xFF24, 0xFF25, 0xFF26, 0xFF27, 0xFF28, 0xFF29, 0xFF2A, 0xFF2B, 0xFF2C, 0xFF2D, 0xFF2E, 0xFF2F,
    // P       Q       R       S       T       U       V       W       X       Y       Z       [       \       ]       ^       _
    0xFF30, 0xFF31, 0xFF32, 0xFF33, 0xFF34, 0xFF35, 0xFF36, 0xFF37, 0xFF38, 0xFF39, 0xFF3A, 0xFF3B, 0xFF3C, 0xFF3D, 0xFF3E, 0xFF3F,
    // '       a       b       c       d       e       f       g       h       i       j       k       l       m       n       o       
    0xFF40, 0xFF41, 0xFF42, 0xFF43, 0xFF44, 0xFF45, 0xFF46, 0xFF47, 0xFF48, 0xFF49, 0xFF4A, 0xFF4B, 0xFF4C, 0xFF4D, 0xFF4E, 0xFF4F,
    // p       q       r       s       t       u       v       w       x       y       z       {       |       }       ~
    0xFF50, 0xFF51, 0xFF52, 0xFF53, 0xFF54, 0xFF55, 0xFF56, 0xFF57, 0xFF58, 0xFF59, 0xFF5A, 0xFF5B, 0xFF5C, 0xFF5D, 0xFF5E
};

//---------------------------------------------------------------------
// defined punctuation characters
//---------------------------------------------------------------------
extern const struct _PUNCTUATION PunctuationTable[14] = {
    {L'!',  0xFF01},
    {L'$',  0xFFE5},
    {L'&',  0x2014},
    {L'(',  0xFF08},
    {L')',  0xFF09},
    {L',',  0xFF0C},
    {L'.',  0x3002},
    {L':',  0xFF1A},
    {L';',  0xFF1B},
    {L'?',  0xFF1F},
    {L'@',  0x00B7},
    {L'\\', 0x3001},
    {L'^',  0x2026},
    {L'_',  0x2014}
};

//+---------------------------------------------------------------------------
//
// CheckModifiers
//
//----------------------------------------------------------------------------

#define TF_MOD_ALLALT     (TF_MOD_RALT | TF_MOD_LALT | TF_MOD_ALT)
#define TF_MOD_ALLCONTROL (TF_MOD_RCONTROL | TF_MOD_LCONTROL | TF_MOD_CONTROL)
#define TF_MOD_ALLSHIFT   (TF_MOD_RSHIFT | TF_MOD_LSHIFT | TF_MOD_SHIFT)
#define TF_MOD_RLALT      (TF_MOD_RALT | TF_MOD_LALT)
#define TF_MOD_RLCONTROL  (TF_MOD_RCONTROL | TF_MOD_LCONTROL)
#define TF_MOD_RLSHIFT    (TF_MOD_RSHIFT | TF_MOD_LSHIFT)

#define CheckMod(m0, m1, mod)        \
    if (m1 & TF_MOD_ ## mod ##)      \
{ \
    if (!(m0 & TF_MOD_ ## mod ##)) \
{      \
    return FALSE;   \
}      \
} \
    else       \
{ \
    if ((m1 ^ m0) & TF_MOD_RL ## mod ##)    \
{      \
    return FALSE;   \
}      \
} \



BOOL CheckModifiers(UINT modCurrent, UINT mod)
{
    mod &= ~TF_MOD_ON_KEYUP;

    if (mod & TF_MOD_IGNORE_ALL_MODIFIER)
    {
        return TRUE;
    }

    if (modCurrent == mod)
    {
        return TRUE;
    }

    if (modCurrent && !mod)
    {
        return FALSE;
    }

    CheckMod(modCurrent, mod, ALT);
    CheckMod(modCurrent, mod, SHIFT);
    CheckMod(modCurrent, mod, CONTROL);

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// UpdateModifiers
//
//    wParam - virtual-key code
//    lParam - [0-15]  Repeat count
//  [16-23] Scan code
//  [24]    Extended key
//  [25-28] Reserved
//  [29]    Context code
//  [30]    Previous key state
//  [31]    Transition state
//----------------------------------------------------------------------------

USHORT ModifiersValue = 0;
BOOL   IsShiftKeyDownOnly = FALSE;
BOOL   IsControlKeyDownOnly = FALSE;
BOOL   IsAltKeyDownOnly = FALSE;

BOOL UpdateModifiers(WPARAM wParam, LPARAM lParam)
{
    // high-order bit : key down
    // low-order bit  : toggled
    SHORT sksMenu = GetKeyState(VK_MENU);
    SHORT sksCtrl = GetKeyState(VK_CONTROL);
    SHORT sksShft = GetKeyState(VK_SHIFT);

    switch (wParam & 0xff)
    {
    case VK_MENU:
        // is VK_MENU down?
        if (sksMenu & 0x8000)
        {
            // is extended key?
            if (lParam & 0x01000000)
            {
                ModifiersValue |= (TF_MOD_RALT | TF_MOD_ALT);
            }
            else
            {
                ModifiersValue |= (TF_MOD_LALT | TF_MOD_ALT);
            }

            // is previous key state up?
            if (!(lParam & 0x40000000))
            {
                // is VK_CONTROL and VK_SHIFT up?
                if (!(sksCtrl & 0x8000) && !(sksShft & 0x8000))
                {
                    IsAltKeyDownOnly = TRUE;
                }
                else
                {
                    IsShiftKeyDownOnly = FALSE;
                    IsControlKeyDownOnly = FALSE;
                    IsAltKeyDownOnly = FALSE;
                }
            }
        }
        break;

    case VK_CONTROL:
        // is VK_CONTROL down?
        if (sksCtrl & 0x8000)    
        {
            // is extended key?
            if (lParam & 0x01000000)
            {
                ModifiersValue |= (TF_MOD_RCONTROL | TF_MOD_CONTROL);
            }
            else
            {
                ModifiersValue |= (TF_MOD_LCONTROL | TF_MOD_CONTROL);
            }

            // is previous key state up?
            if (!(lParam & 0x40000000))
            {
                // is VK_SHIFT and VK_MENU up?
                if (!(sksShft & 0x8000) && !(sksMenu & 0x8000))
                {
                    IsControlKeyDownOnly = TRUE;
                }
                else
                {
                    IsShiftKeyDownOnly = FALSE;
                    IsControlKeyDownOnly = FALSE;
                    IsAltKeyDownOnly = FALSE;
                }
            }
        }
        break;

    case VK_SHIFT:
        // is VK_SHIFT down?
        if (sksShft & 0x8000)    
        {
            // is scan code 0x36(right shift)?
            if (((lParam >> 16) & 0x00ff) == 0x36)
            {
                ModifiersValue |= (TF_MOD_RSHIFT | TF_MOD_SHIFT);
            }
            else
            {
                ModifiersValue |= (TF_MOD_LSHIFT | TF_MOD_SHIFT);
            }

            // is previous key state up?
            if (!(lParam & 0x40000000))
            {
                // is VK_MENU and VK_CONTROL up?
                if (!(sksMenu & 0x8000) && !(sksCtrl & 0x8000))
                {
                    IsShiftKeyDownOnly = TRUE;
                }
                else
                {
                    IsShiftKeyDownOnly = FALSE;
                    IsControlKeyDownOnly = FALSE;
                    IsAltKeyDownOnly = FALSE;
                }
            }
        }
        break;

    default:
        IsShiftKeyDownOnly = FALSE;
        IsControlKeyDownOnly = FALSE;
        IsAltKeyDownOnly = FALSE;
        break;
    }

    if (!(sksMenu & 0x8000))
    {
        ModifiersValue &= ~TF_MOD_ALLALT;
    }
    if (!(sksCtrl & 0x8000))
    {
        ModifiersValue &= ~TF_MOD_ALLCONTROL;
    }
    if (!(sksShft & 0x8000))
    {
        ModifiersValue &= ~TF_MOD_ALLSHIFT;
    }

    return TRUE;
}

//---------------------------------------------------------------------
// override CompareElements
//---------------------------------------------------------------------
BOOL CompareElements(LCID locale, const CStringRange* pElement1, const CStringRange* pElement2)
{
    return (CStringRange::Compare(locale, (CStringRange*)pElement1, (CStringRange*)pElement2) == CSTR_EQUAL) ? TRUE : FALSE;
}
}