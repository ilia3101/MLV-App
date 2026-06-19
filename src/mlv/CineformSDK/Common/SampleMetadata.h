/*! @file SampleMetadata.h

*  @brief Metadata Tools
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
#define MAX_OVERRIDE_SIZE	16384
class CSampleMetadata
{
public:

	//TODO: Initialize other parameters to default values in the constructor
	CSampleMetadata() :
		m_sampleData(NULL),
		m_sampleSize(0),
		m_currentData(NULL),
		m_currentSize(0),
		m_databaseData(NULL),
		m_databaseSize(0),
		m_databaseDataL(NULL),
		m_databaseSizeL(0),
		m_databaseDataR(NULL),
		m_databaseSizeR(0),
		m_overrideSize(0),
		m_workspaceSize(0),
		m_metadataStart(NULL),
		m_lastMetadata(NULL),
		m_metadataTrack(METADATATYPE_MODIFIED),
		m_active_mask(0),
		m_currentUFRM(-1),
		m_hash(0),
		last_process_time(0),
		last_write_time(0),
		m_allocator(NULL)
	{
		memset(&m_currentClipGUID, 0, sizeof(myGUID));		
		memset(PathStr, 0, sizeof(PathStr));
		memset(DBStr, 0, sizeof(DBStr));

		memset(m_overrideData, 0, MAX_OVERRIDE_SIZE);
		memset(m_workspaceData, 0, MAX_OVERRIDE_SIZE);
	}

public:

	//TODO: Should replace unsigned char pointers with void pointers

	unsigned char *m_sampleData;
	size_t m_sampleSize;

	unsigned char *m_currentData;	// Points to the sample or the database
	size_t m_currentSize;

	unsigned char *m_databaseData;	// Color database read from disk
	uint32_t m_databaseSize;

	unsigned char *m_databaseDataL;	// Color database read from disk
	uint32_t m_databaseSizeL;

	unsigned char *m_databaseDataR;	// Color database read from disk
	uint32_t m_databaseSizeR;

	unsigned char m_overrideData[MAX_OVERRIDE_SIZE];	// Local color database override decodes
	uint32_t m_overrideSize;

	unsigned char m_workspaceData[MAX_OVERRIDE_SIZE];	// scratchspace for metadata manipulation
	uint32_t m_workspaceSize;

	uint32_t scratch_buffer[16];	//used from computing Left/Right metadata deltas and result the results.

	void *m_metadataStart;
	void *m_lastMetadata;

	CFHD_MetadataTrack m_metadataTrack;

	//TODO: Replace non-portable unsigned integer with a standard integer type
	unsigned int m_active_mask;

	myGUID m_currentClipGUID;
	int32_t m_currentUFRM;
	uint32_t m_CPLastOffset;
	uint32_t m_hash;
	uint32_t m_smart_render_ok;

	char PathStr[260];
	char DBStr[64];
	
	clock_t last_process_time;
	time_t last_now_time;

	uint32_t last_write_time;

	CFHD_Error SetAllocator(CFHD_ALLOCATOR * allocator)
	{
		m_allocator = allocator;
		return CFHD_ERROR_OKAY;
	}

	CFHD_ALLOCATOR *GetAllocator()
	{
		return m_allocator;
	}

	
	void *Alloc(size_t size)
	{
#if _ALLOCATOR
		// Use the allocator if it is available
		if (m_allocator) {
			return m_allocator->vtable->unaligned_malloc(m_allocator, size);
		}
#endif
		// Otherwise use the default memory allocator
		return malloc(size);
	}

	void Free(void *block)
	{
#if _ALLOCATOR
		// Use the allocator if it is available
		if (m_allocator) {
			m_allocator->vtable->unaligned_free(m_allocator, block);
			return;
		}
#endif
		// Otherwise use the default memory allocator
		free(block);
	}


	void *AlignAlloc(size_t size, size_t alignment)
	{
#if _ALLOCATOR
		// Use the allocator if it is available
		if (m_allocator) {
			return m_allocator->vtable->aligned_malloc(m_allocator, size, alignment);
		}
#endif
		// Otherwise use the default memory allocator
#if 1
  #ifdef __APPLE__
		return malloc(size);
  #else
		return _mm_malloc(size, alignment);
  #endif
#else
        int     memerror;
        void *  allocatedMemory;
        
        memerror = posix_memalign(&allocatedMemory, alignment, size);
        if (0==memerror) {
            return allocatedMemory;
        } else {
            return NULL;
        }
#endif
	}

	void AlignFree(void *block)
	{
#if _ALLOCATOR
		// Use the allocator if it is available
		if (m_allocator) {
			m_allocator->vtable->aligned_free(m_allocator, block);
			return;
		}
#endif
		// Otherwise use the default memory allocator
#if __APPLE__
        free(block);
#else
		_mm_free(block);
#endif
	}

	bool GetClipDatabase();

	void FreeDatabase()
	{
		if(m_databaseSize && m_databaseData)
		{
			Free(m_databaseData);
			m_databaseData = NULL;
			m_databaseSize = 0;
		}	
		if(m_databaseSizeL && m_databaseDataL)
		{
			Free(m_databaseDataL);
			m_databaseDataL = NULL;
			m_databaseSizeL = 0;
		}	
		if(m_databaseSizeR && m_databaseDataR)
		{
			Free(m_databaseDataR);
			m_databaseDataR = NULL;
			m_databaseSizeR = 0;
		}

		
		if(m_overrideSize)
		{
			memset(m_overrideData, 0, MAX_OVERRIDE_SIZE);
			memset(m_workspaceData, 0, MAX_OVERRIDE_SIZE);
			m_overrideSize = m_workspaceSize = 0;
		}
	}
	
	bool AddMetaData(uint32_t Tag, unsigned int typesizebytes, void *pData);
	bool AddMetaDataWorkspace(uint32_t Tag, unsigned int typesizebytes, void *pData);
	void MakeLeftRightDelta(uint32_t Tag, unsigned int typesizebytes, void *pData);
	bool AddMetaDataChannel(uint32_t Tag, unsigned int typesizebytes, void *pData, uint32_t channel);

protected:
	CFHD_ALLOCATOR *m_allocator;

private:

};

// Return the pathname of the LUT directory and the filename of the database directory
void InitGetLUTPaths(char *pPathStr,	//!< Pathname to the LUT directory
					 size_t pathSize,	//!< Size of the LUT pathname (in bytes)
					 char *pDBStr,		//!< Filename of the database directory
					 size_t DBSize		//!< Size of the database filename (in bytes)
					 );
