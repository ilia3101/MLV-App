/*! @file macdefs.h

*  @brief Apple stuff
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

#ifndef _MACDEFS_H
#define _MACDEFS_H


// Override symbols used in Windows programming

#ifdef WINAPI
#undef WINAPI
#endif

#define WINAPI

#ifndef _MAX_PATH
#define _MAX_PATH	256
#endif

#define	BAYER_SUPPORT 1

// Define data types used in the codec that are not defined on the Macintosh

typedef unsigned char BYTE;
typedef unsigned char *PBYTE;
typedef unsigned char *LPBYTE;

typedef unsigned short WORD;

typedef long LONG;
typedef unsigned long DWORD;
typedef unsigned long ULONG;

typedef void VOID;
typedef void *HANDLE;
typedef void *LPVOID;

#ifdef  __INTEL_COMPILER
#else
typedef long long __int64;
#endif

typedef char INT8;
typedef unsigned char UINT8;

typedef short INT16;
typedef unsigned short UINT16;

typedef long INT32;
typedef unsigned long UINT32;

typedef unsigned long FOURCC;

//#ifndef _OBJC_OBJC_H_
//typedef int BOOL;
//#endif

//enum {FALSE = 0, TRUE = 1};
#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define INFINITE	0xFFFFFFFF		// Infinite timeout

#define CopyMemory(dst, src, length)		memcpy(dst, src, length)
#define ZeroMemory(dst, length)				memset(dst, 0, length)

#if defined(HRESULT)
#undef HRESULT
typedef int32_t HRESULT;
#endif

#if !defined(FAILED)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#endif

#if !defined(SUCCEEDED)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif

#if !defined(_HRESULT_TYPEDEF)
#define _HRESULT_TYPEDEF_(_sc) ((HRESULT)_sc)
#endif

#if !defined(E_UNEXPECTED)
#define E_UNEXPECTED		_HRESULT_TYPEDEF_(0x8000FFFFL)
#endif

#if !defined(E_INVALIDARG)
#define E_INVALIDARG		_HRESULT_TYPEDEF_(0x80070057L)
#endif

#if !defined(E_POINTER)
#define E_POINTER			_HRESULT_TYPEDEF_(0x80004003L)
#endif

#if !defined(E_FAIL)
#define E_FAIL				_HRESULT_TYPEDEF_(0x80004005L)
#endif

#if !defined(E_OUTOFMEMORY)
#define E_OUTOFMEMORY		_HRESULT_TYPEDEF_(0x8007000EL)
#endif

#if !defined(S_OK)
#define S_OK				((HRESULT) 0L)
#endif

#if !defined(S_FALSE)	
#define S_FALSE				((HRESULT 1L)
#endif

#endif
