/*! @file buffer.h

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

#ifndef _BUFFER_H
#define _BUFFER_H

// Use the same structure packing as the Intel C/C++ compiler
//#pragma pack(push)
//#pragma pack(16)
#pragma pack(push, 16)

typedef struct section		// Definition of the buffer data structure
{
	char *base_ptr;			// Base address of the buffer (before subdivision)
	char *free_ptr;			// Pointer to the free area in the buffer
	size_t free_size;		// Size of the free space in the buffer
	char *next_ptr;			// Pointer to the overflow block

} SCRATCH;

// Macro for initializing a buffer data structure
#define SCRATCH_INITIALIZER(buffer, size)	{(char *)(buffer), (char *)(buffer), (size), NULL}

#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif

// Initialize a scratch buffer
void InitScratchBuffer(SCRATCH *scratch, char *base, size_t size);

// Initialize a local section within the scratch buffer
void PushScratchBuffer(SCRATCH *section, const SCRATCH *scratch);

// Routine for allocating a scratch buffer
char *AllocScratchBuffer(SCRATCH *scratch, size_t request);

// Aligned allocation of a scratch buffer
char *AllocAlignedBuffer(SCRATCH *scratch, size_t request, int alignment);

// Allocate scratch space for intermediate results
void AllocScratchSpace(SCRATCH *scratch, size_t size, void *allocator);

// Free all scratch space used for intermediate results
void ReleaseScratchSpace(SCRATCH *scratch);

// Force alignment of the remaining free space
void AlignScratchSpace(SCRATCH *scratch, int alignment);

#ifdef __cplusplus
}
#endif

#endif
