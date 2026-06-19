/*! @file MetadataWriter.h

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

// Use the metadata routines in the codec library
#include "metadata.h"


// Forward reference to the decoder metadata
class CSampleMetadata;


class CSampleEncodeMetadata
{
public:

	CSampleEncodeMetadata() :
		m_allocator(NULL),
		//m_privateAllocatorFlag(false),//(true),
		m_selectedEye(0)
	{
#if 0
		// Create a default allocator
		m_allocator = new CMemAlloc;
		assert(m_allocator);
#endif
		//CreateLock(&m_lock);

		// Initialize the global metadata
		memset(&local, 0, sizeof(METADATA));
		for(int i=0; i<5; i++)
			memset(&global[i], 0, sizeof(METADATA));
	}

	/* CSampleEncodeMetadata(IMemAlloc *allocator) :
		m_allocator(allocator),
		m_privateAllocatorFlag(false),
		m_metadataChanged(true),
		m_selectedEye(0)
	{
		//CreateLock(&m_lock);
		memset(&local, 0, sizeof(METADATA));
		for(int i=0; i<5; i++)
			memset(&global[i], 0, sizeof(METADATA));
	} */

	CSampleEncodeMetadata(const CSampleEncodeMetadata *metadata) :
		m_allocator(NULL),
		//m_privateAllocatorFlag(false),
		m_metadataChanged(true),
		m_selectedEye(0)
	{
		memset(&local, 0, sizeof(METADATA));
		for(int i=0; i<5; i++)
			memset(&global[i], 0, sizeof(METADATA));

		m_allocator = metadata->m_allocator;
		//m_privateAllocatorFlag = metadata->m_privateAllocatorFlag;
		m_metadataChanged = metadata->m_metadataChanged;
		m_selectedEye = metadata->m_selectedEye;


		// Make a deep copy of the global metadata
		//if (metadata->m_metadataGlobal && metadata->m_metadataGlobalSize > 0)
		if (metadata->global[0].block != NULL && metadata->global[0].size > 0)
		{
#if _ALLOCATOR
			AllocMetadata(metadata->global[0].allocator, &global[0], metadata->global[0].size);
#else
			AllocMetadata(&global[0], metadata->global[0].size);
#endif
			//if (m_metadataGlobal)
			if (global[0].block)
			{
				//memcpy(m_metadataGlobal, metadata->m_metadataGlobal, metadata->m_metadataGlobalSize);
				//m_metadataGlobalSize = metadata->m_metadataGlobalSize;
				memcpy(global[0].block, metadata->global[0].block, metadata->global[0].size);
				global[0].size = metadata->global[0].size;
			}
		}

		// Make a deep copy of the local metadata
		//if (metadata->m_metadataLocal && metadata->m_metadataLocalSize > 0)
		if (metadata->local.block != NULL && metadata->local.size > 0)
		{
#if _ALLOCATOR
			AllocMetadata(metadata->local.allocator, &local, metadata->local.size);
#else
			AllocMetadata(&local, metadata->local.size);
#endif
			//if (m_metadataLocal)
			if (local.block)
			{
				//memcpy(m_metadataLocal, metadata->m_metadataLocal, metadata->m_metadataLocalSize);
				//m_metadataLocalSize = metadata->m_metadataLocalSize;
				memcpy(local.block, metadata->local.block, metadata->local.size);
				local.size = metadata->local.size;
			}
		}
	}

	~CSampleEncodeMetadata()
	{
		// Release resources allocated by the encoder
		//if (m_metadataGlobal)
		for(int i=0; i<5; i++)
		{
			if (global[i].block)
			{
				ReleaseMetadata(&global[i]);
			}
		}
		if (local.block)
		{
			ReleaseMetadata(&local);
		}

		// Release the allocator if it is only referenced by this sample encoder
	//	if (m_allocator && m_privateAllocatorFlag) {
	//		//delete m_allocator;
	//		m_allocator = NULL;
	//	}
	}

	CFHD_Error SetAllocator(CFHD_ALLOCATOR * allocator)
	{
		m_allocator = allocator;
		return CFHD_ERROR_OKAY;
	}

	CFHD_Error GetAllocator(CFHD_ALLOCATOR ** allocator)
	{
		*allocator = m_allocator;
		return CFHD_ERROR_OKAY;
	}


	CFHD_Error AddGUID();
	CFHD_Error AddLookFile(METADATA_TYPE type, METADATA_SIZE size, uint32_t *data);
	CFHD_Error AddTimeStamp(const char *date, const char *time);
	CFHD_Error AddTimeCode(const char *timecode, bool local_metadata = false);
	CFHD_Error AddFrameNumber(uint32_t framenum, bool local_metadata = false);

	//TODO: Make the following member variables protected or private

	CSimpleLock m_lock;

	bool m_metadataChanged;

	uint32_t m_selectedEye;

	//uint32_t *m_metadataGlobal;
	//size_t m_metadataGlobalSize;
	//uint32_t *m_metadataLocal;
	//size_t m_metadataLocalSize;
	METADATA global[5]; // 0-both, 1-left, 2-right, 3-diffLeft, 4-dffRight
	METADATA local;

	// Attach metadata to this sample encoder
	CFHD_Error AttachMetadata(CSampleMetadata *metadata);

protected:

	static void ReleaseMetadata(METADATA *metadata);

private:

	//IMemAlloc *m_allocator;
	CFHD_ALLOCATOR *m_allocator;

	// The destructor should release the allocator if it is private
	//bool m_privateAllocatorFlag; 

	//TODO: Move the private allocator flag into the allocator class
};
