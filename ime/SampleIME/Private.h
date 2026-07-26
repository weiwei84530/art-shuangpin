// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#pragma once

// [MspyIME] Dev builds write diagnostics to %TEMP%\MspyIME.debug.log; remove
// for release packaging.
#define MSPY_DEBUG_LOG

#include "stdafx.h"
#include "sal.h"

#include <combaseapi.h>
#include <olectl.h>
#include <assert.h>

#include <strsafe.h>
#include <intsafe.h>

#include "initguid.h"
#include "msctf.h"
#include "ctffunc.h"
