/*! @file MemAlloc.cpp 

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

#include "StdAfx.h"
#include "MemAlloc.h"

void *CMemAlloc::Alloc(size_t size)
{
#ifdef _WIN32
	void *block = _aligned_malloc(size, m_alignment);
#else
  #ifdef __APPLE__
	void *block = malloc(size);
  #else
	void *block = _mm_malloc(size, m_alignment);
  #endif
#endif

#if _DEBUG
	if (m_allocationCount < sizeof(m_allocationTable) / sizeof(m_allocationTable[0]))
	{
		m_allocationTable[m_allocationCount].block = block;
		m_allocationTable[m_allocationCount].size = size;
		m_allocationCount++;
	}
#endif

	return block;
}

void CMemAlloc::Free(void *block)
{
#ifdef _WIN32
	_aligned_free(block);
#else
  #ifdef __APPLE__
	free(block);
  #else
	_mm_free(block);
  #endif	 
#endif

#if _DEBUG
	bool found = false;

	for (size_t i = 0; i < m_allocationCount; i++)
	{
		// The index should not exceed the allocated size of the table
		assert(i < sizeof(m_allocationTable)/sizeof(m_allocationTable[0]));

		// Found the block in the table?
		if (m_allocationTable[i].block == block)
		{
			//m_allocationTable[i].block = NULL;
			//m_allocationTable[i].size = 0;

			// Shift the remaining entries in the table
			for (size_t j = i + 1; j < m_allocationCount; j++)
			{
				// The index should not exceed the allocated size of the table
				assert(j < sizeof(m_allocationTable)/sizeof(m_allocationTable[0]));

				m_allocationTable[i].block = m_allocationTable[j].block;
				m_allocationTable[i].size = m_allocationTable[j].size;

				// Advance the index to the next table entry to fill
				i = j;
			}

			m_allocationTable[i].block = NULL;
			m_allocationTable[i].size = 0;

			// Reduce the number of entries in the table
			m_allocationCount--;

			// Done searching for the allocation table entry
			found = true;
			break;
		}

		// Should have found the allocated block in the table
		assert(found);
	}
#endif
}
