/*! @file StdAfx.h

*  @brief 
*
*  @version 1.0.0
*
*  (C) Copyright 2017 GoPro Inc (http://gopro.com/).
*
*  Licensed under either:
*  - Apache License, Version 2.0, http://www.apache.org/licenses/LICENSE-2.0  
*  - MIT license, http://opensource.org/licenses/MIT
*  at your option.
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
*/

#pragma once

#if _WIN32
#include <windows.h>
//#include <atlbase.h>
#include <tchar.h>
//#include <malloc.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <memory.h>
#include <limits.h>

#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "../Codec/sse2neon/sse2neon.h"
#endif


//#include <string.h>
//#include <assert.h>
//#include <vfw.h>
//#include <time.h>

#ifdef __APPLE__
#include "CoreFoundation/CoreFoundation.h"
#endif

#ifdef _WIN32

#if 0
// Windows does not have the standard integer types
typedef signed char int8_t;
typedef unsigned char   uint8_t;
typedef short  int16_t;
typedef unsigned short  uint16_t;
typedef int  int32_t;
typedef unsigned   uint32_t;
typedef __int64  int64_t;
typedef unsigned __int64   uint64_t;
#else
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#endif

#else

// The standard integer types should be available on most platforms
#define __STDC_LIMIT_MACROS
#include <stdint.h>

#if !defined(_WIN32) && !defined(__APPLE__)
// Use byte swapping functions on Linux
#include <byteswap.h>
#endif

#endif
