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

#ifdef _WIN32

// Exclude rarely-used stuff from Windows headers
//#define WIN32_LEAN_AND_MEAN

// Windows header files
#include <windows.h>

// Includes required for Visual Studio 2005 (not required for Visual Studio 2003)
//#include <atlbase.h>
#include <tchar.h>

#include <stdlib.h>
#include <memory.h>
#include <assert.h>

#ifdef _WIN32
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#endif

#else

// Hack to eliminate conflict with the codec CODESET data type
#define _LANGINFO_H 1

#include <stddef.h>
#include <stdlib.h>
#include <memory.h>
#include <assert.h>

#ifndef _WIN32
#include <uuid/uuid.h>
#endif

#ifndef __APPLE__
#include <mm_malloc.h>
#endif

#include <libgen.h>
#include <pthread.h>
#include <semaphore.h>

#define __STDC_LIMIT_MACROS
#include <stdint.h>

//#if __APPLE__
//#include "macdefs.h"
//#endif

#include <stdio.h>
#include <stdarg.h>

#endif

// The encoder and thread pools use the vector data type from the standard template library
#include <vector>

// The encoder job queue uses a deque from the standard template library
#include <deque>

// The message queue for the worker threads uses a queue from the standard template library
#include <queue>

// TODO: reference additional headers your program requires here
