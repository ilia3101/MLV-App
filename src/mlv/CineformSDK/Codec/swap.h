/*! @file swap.h

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

#ifndef SWAP_H
#define SWAP_H

// Need to define the byte swapping intrinsic
#if __cplusplus
extern "C"
#endif

// Define a platform independent byte swapping routine

#ifdef _WIN32
#if _MSC_VER
#define _bswap(x)		_byteswap_ulong(x)
#endif
#define SwapInt32(x)	_byteswap_ulong(x)
#define SwapInt16(x)	_byteswap_ushort(x)

// Define byte swapping routines for the native format (Windows)
#define SwapInt32BtoN(x)	SwapInt32(x)
#define SwapInt32LtoN(x)	(x)
#define SwapInt32NtoB(x)	SwapInt32(x)
#define SwapInt32NtoL(x)	(x)



#elif __APPLE__
#include "CoreFoundation/CoreFoundation.h"
#define _bswap(x)		_OSSwapInt32(x)
#define SwapInt32(x)	_OSSwapInt32(x)
#define SwapInt16(x)	_OSSwapInt16(x)

#define SwapInt32BtoN(x)	SwapInt32(x)
#define SwapInt32LtoN(x)	(x)
#define SwapInt32NtoB(x)	SwapInt32(x)
#define SwapInt32NtoL(x)	(x)

#else

// Use the GCC byte swapping macros

#include <byteswap.h>

#define SwapInt16(x)	bswap_16(x)
#define SwapInt32(x)	bswap_32(x)

// Define byte swapping routines for the native format (Windows)
#define SwapInt32BtoN(x)	SwapInt32(x)
#define SwapInt32LtoN(x)	(x)
#define SwapInt32NtoB(x)	SwapInt32(x)
#define SwapInt32NtoL(x)	(x)

//TODO: Replace uses of _bswap with SwapInt32
#define _bswap(x)		SwapInt32(x)

#endif

#endif
