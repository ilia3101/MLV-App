/*! @file IAllocator.h

*  @brief Memory tools
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

#ifdef	_WIN32
// Force use of the normal C language calling conventions
#define	CALLTYPE_(type)  type __cdecl
#else
// Use the default calling conventions the Macintosh
#define	CALLTYPE_(type)  type
#endif


/*!
	@brief Interface to a memory allocator

	The definitions in this class must match the definitions in the
	struct for defining allocators in the C interface.
*/
class IAllocator
{
public:
	// Do not change the order of these virtual methods
	virtual CALLTYPE_(void *) Alloc(size_t size) = 0;
	virtual CALLTYPE_(void)   Free(void *block) = 0;
	virtual CALLTYPE_(void *) AlignedAlloc(size_t size, size_t alignment) = 0;
	virtual CALLTYPE_(void)   AlignedFree(void *block) = 0;

	// Define virtual destructor so the destructor in derived classes is called
	virtual ~IAllocator() {}
};
