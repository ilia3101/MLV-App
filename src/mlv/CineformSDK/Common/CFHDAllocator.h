/*! @file CFHDAllocator.h
*
*  @brief Setting up and controlling the Allocator used within the CineForm SDKs
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
*/

#pragma once
#ifndef CFHD_ALLOCATOR_H
#define CFHD_ALLOCATOR_H

typedef void * (* UnalignedAllocProc)(void *allocator, size_t size);
typedef void * (* AlignedAllocProc)(void *allocator, size_t size, size_t alignment);
typedef void (* UnalignedFreeProc)(void *allocator, void *block);
typedef void (* AlignedFreeProc)(void *allocator, void *block);


// Table of function pointers in an instance of a C++ allocator interface
struct cfhd_allocator_vtable
{
	// Do not change the order of the procedure pointers
	UnalignedAllocProc unaligned_malloc;
	UnalignedFreeProc unaligned_free;
	AlignedAllocProc aligned_malloc;
	AlignedFreeProc aligned_free;
};

typedef struct cfhd_allocator
{
	// Pointer to the vtable in the allocator interface
	struct cfhd_allocator_vtable *vtable;

	// Add member variables here if they are accessed in the C code

} ALLOCATOR;

#define CFHD_ALLOCATOR ALLOCATOR

#endif // CFHD_ALLOCATOR_H
