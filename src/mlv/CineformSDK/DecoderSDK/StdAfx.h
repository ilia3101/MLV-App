/*! @file StdAfx.h

*  @brief 
*  
*  Interface to the CineForm HD decoder.  The decoder API uses an opaque
*  data type to represent an instance of a decoder.  The decoder reference
*  is returned by the call to CFHD_OpenDecoder.
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

// Windows header files (required for registry access)
#include <windows.h>

// Includes required for Visual Studio 2005 (not required for Visual Studio 2003)
//#include <atlbase.h>
#include <tchar.h>

#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <limits.h>
#include <memory.h>
#include <stdint.h>
#include <stdbool.h>
#include "../Common/ver.h"

//TODO: reference additional headers your program requires here

