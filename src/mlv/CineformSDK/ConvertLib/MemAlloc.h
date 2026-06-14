/*! @file MemAlloc.h

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

// Any memory allocator must provide methods to allocate and free memory
class IMemAlloc
{
public:
	virtual void *Alloc(size_t size) = 0;
	virtual void Free(void *block) = 0;

	// Must declare virtual destructor for gcc
	virtual ~IMemAlloc() {};
};

// Default memory allocator provided by the conversion library
class CMemAlloc : public IMemAlloc
{
public:

	//CMemAlloc(int alignment)
	//{
	//	m_alignment = alignment;
	//}

#if _DEBUG
	CMemAlloc() :
		m_allocationCount(0)
	{
		memset(m_allocationTable, 0, sizeof(m_allocationTable));
	}

	virtual ~CMemAlloc()
	{
		// assert(m_allocationCount == 0);
	}
#endif

	// Implementation of the memory allocator interface
	void *Alloc(size_t size);
	void Free(void *block);

private:

	// Byte alignment of allocated memory blocks
	static const int m_alignment = 16;

private:

#if _DEBUG
	typedef struct
	{
		void *block;
		size_t size;

	} AllocatedBlock;

	// Table for recording allocated memory blocks for debugging
	AllocatedBlock m_allocationTable[100];

	size_t m_allocationCount;

	//TODO: Replace this data structure with an STL array
#endif
};
