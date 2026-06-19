/*! @file stdafx.h

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

#define __STDC_LIMIT_MACROS

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <memory.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif


#ifdef _WIN32
#include <windows.h>
#include <direct.h>
 #ifdef __cplusplus
 // #include <atlbase.h> // Required for VS2005 but not 2003
  #include <tchar.h>
 #endif
#elif __APPLE__
#include <string.h>
#include <sys/stat.h>		// for _mkdir()
#include <unistd.h>			// for usleep()
#include <CoreFoundation/CoreFoundation.h>		// for propertylist/preferences
#else
#include <sys/stat.h>		// for _mkdir()
#include <unistd.h>			// for usleep()
#include <ctype.h>			// for isalpha()
#endif

#ifdef __cplusplus

//#include <string>

#endif

