/*! @file config.h

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

#ifndef _CONFIG_H
#define _CONFIG_H

#include <stdint.h>
#include <stdlib.h>

// Enable overlay text and graphics using Cairo
#ifndef _GRAPHICS
#define _GRAPHICS	0
#endif

#ifdef _WIN32
 #ifndef DEBUG_ALLOCS
 #define DEBUG_ALLOCS	1
 #endif

 #ifndef FORCEINLINE
 #define FORCEINLINE __forceinline
 #endif
#else
#define FORCEINLINE __inline__
#endif

// Enable or disable use of the new state machine decoder
#ifndef _NEW_DECODER
#define _NEW_DECODER 1
#endif

// Enable use of multimedia instructions for code optimization
#ifndef _XMMOPT
#define _XMMOPT 1
#endif

#ifndef _MAX_CPUS
#define _MAX_CPUS 32
#endif

// Enable use of assembly language for code optimization
#ifndef _ASMOPT
 #ifdef __LP64__
  #define _ASMOPT 0
 #else
  #ifndef _WIN64
   #define _ASMOPT 1
  #else
   #define _ASMOPT 0
  #endif
 #endif
#endif

// Disable use of Intel Performance Primitives
#ifndef _IPPLIB
#define _IPPLIB 0
#endif

// Run length encode zero runs within the frame transform.
#define _PACK_RUNS_IN_BAND_16S	0

// Do not use Posix routines by default
//#ifndef _POSIX
//#define _POSIX 0
//#endif

//TODO: Replace Windows datatypes with Standard C datatypes
#include <stdint.h>
#include <stdbool.h>

// Use the standard definition for the maximum length of a pathname
#ifdef _WIN32
#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif
#endif

// Parameters of the default cache line size
#define _CACHE_LINE_SIZE		64
#define _CACHE_LINE_MASK		0x3F
#define _CACHE_LINE_SHIFT		6

// Default memory alignment
#define _MEMORY_ALIGNMENT		16

#if 1 

	// Control compilation of code for different processors
	#ifndef _PROCESSOR_DISPATCH
	#define _PROCESSOR_DISPATCH		0
	#endif

	// Select what processor specific code is generated
	#ifndef _PROCESSOR_GENERIC
	#define _PROCESSOR_GENERIC		0
	#endif

	#ifndef _PROCESSOR_PENTIUM_4
	#define _PROCESSOR_PENTIUM_4	1
	#endif

#else // Enable support for P3/Athlon - likely too old to work

	// Control compilation of code for different processors
	#ifndef _PROCESSOR_DISPATCH
	#define _PROCESSOR_DISPATCH		1
	#endif

	// Select what processor specific code is generated
	#ifndef _PROCESSOR_GENERIC
	#define _PROCESSOR_GENERIC		1
	#endif

	#ifndef _PROCESSOR_PENTIUM_4
	#define _PROCESSOR_PENTIUM_4	1
	#endif

#endif

//TODO: Enable use of the new memory allocator functions be default

// Enable or disable use of the new memory allocator functions
#ifndef _ALLOCATOR
#define _ALLOCATOR	0
#endif

//TODO: Eliminate functions that do have a memory allocator argument

#ifdef __cplusplus
extern "C" {
#endif

//TODO: Replace uses of MEMORY_ALLOC and similar macros

#if (0 && _ALLOCATOR)

// The static memory functions should not be used
#define MEMORY_ALLOC(size)							(assert(0), NULL)
#define MEMORY_FREE(block)							assert(0)
#define MEMORY_ALIGNED_ALLOC(size, alignment)		(assert(0), NULL)
#define MEMORY_ALIGNED_FREE(block)					assert(0)

#elif defined(ADOBE_MEMORY_FUNCTIONS)

extern void *MEMORY_ALLOC(int size);
extern void MEMORY_FREE(void *);
extern void *MEMORY_ALIGNED_ALLOC(int size, int alignment);
extern void MEMORY_ALIGNED_FREE(void *);
extern void MEMORY_ALIGNED_CACHE_RELEAE();

#elif defined(AE_MEMORY_FUNCTIONS)

extern void *_ae_malloc(int size);
extern void _ae_free(void *);
extern void *_ae_aligned_malloc(int size, int alignment);
extern void _ae_aligned_free(void *);

#define MEMORY_ALLOC			_ae_malloc
#define MEMORY_FREE				_ae_free
#define MEMORY_ALIGNED_ALLOC	_ae_aligned_malloc
#define MEMORY_ALIGNED_FREE		_ae_aligned_free
#define MEMORY_ALIGNED_CACHE_RELEAE

#else

/* for debugging */
#if DEBUG_ALLOCS  //TODO For some reason _mm_malloc is not freeing correctly under certain conditions (4K transcodes.)
static void *_mm_malloc22(size_t size, size_t align)
{
	size_t newsize = size + align + 16;
	char *ptr = 0, *bptr = (char *)malloc(newsize);
	if(bptr)
	{
		size_t lptr,*lptr2;

		ptr = bptr;
		ptr += align+16;
		lptr = (size_t)ptr;
		lptr &= ~(align-1);
		ptr = (char *)lptr;
		lptr2 = (size_t *)lptr;
		lptr2--;
		*lptr2 = (size_t)bptr;
		lptr2--;
		*lptr2 = (size_t)bptr;
	}
	return ptr;
}
static void _mm_free22(void *addr)
{
	size_t *lptr1,*lptr2,*lptr = (size_t*)addr;
	lptr--;
	lptr1 = lptr;
	lptr--;
	lptr2 = lptr;
	
	if(*lptr1 == *lptr2 && (size_t)*lptr1 < (size_t)addr && ((size_t)*lptr1 + 1024)> (size_t)addr)
		free((void*)*lptr1);
	else
	{
		return;
	}
}
#endif

#ifdef __APPLE__
static void *malloc22(size_t size, size_t align)
{
	void *p = NULL;
	posix_memalign(&p, align, size);
	return p;
}
#endif

#define MEMORY_ALLOC			malloc
#define MEMORY_FREE				free

#if DEBUG_ALLOCS
  #define MEMORY_ALIGNED_ALLOC	_mm_malloc22
  #define MEMORY_ALIGNED_FREE	_mm_free22
#else
	#ifdef __APPLE__
	#define MEMORY_ALIGNED_ALLOC	malloc22
	#define MEMORY_ALIGNED_FREE		free
	#else
	#define MEMORY_ALIGNED_ALLOC	_mm_malloc
	#define MEMORY_ALIGNED_FREE		_mm_free
	#endif 
#endif

#define MEMORY_ALIGNED_CACHE_RELEAE

#endif

#ifdef __cplusplus
}
#endif


#ifndef _THREADED
#define _THREADED	1		// Switch for threading that is implemented on both Windows and Macintosh
#endif

#ifndef _THREADED_ENCODER
#define _THREADED_ENCODER	0		// Perform encoding using multiple threads?
#endif

#ifndef _THREADED_DECODER
#define _THREADED_DECODER	1
#endif

#define _DELAYED_THREAD_START	1

#ifndef _INTERLACED_WORKER_THREADS
#ifdef _WIN32
#define _INTERLACED_WORKER_THREADS (_THREADED_DECODER)		// Use worker threads for the last transform?
#else
#define _INTERLACED_WORKER_THREADS 0
#endif
#endif

#ifndef _ENTROPY_THREADS
#define _ENTROPY_THREADS (_THREADED_DECODER)		// Use worker threads for the last transform?
#endif

#ifndef _DELAY_THREAD_START
#define _DELAY_THREAD_START		1
#endif



#ifndef _PREFETCH
#define _PREFETCH	1		// Use memory prefetch optimizations?
#endif

#ifndef LOSSLESS
#define LOSSLESS	0		// Sent the quantization to 1 and use peaks table (no companding.)
#endif

#ifndef _RECURSIVE
#define _RECURSIVE	0		// Disable the recursive wavelet transform by default
#endif

#ifndef _NODITHER
#define	_NODITHER	0
#endif


#endif
