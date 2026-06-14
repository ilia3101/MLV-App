/*! @file CFHDMetadata.cpp

*  @brief This module implements the C functions for the metadata API
*  
*  Interface to the CineForm HD decoder.  The decoder API uses an opaque
*  data type to represent an instance of a decoder.  The decoder reference
*  is returned by the call to CFHD_OpenDecoder.
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

#ifdef _WIN32
#else
#define MAX_PATH	260
#if __APPLE__
#include <string.h>
#include "CoreFoundation/CoreFoundation.h"
#else
#include  <mm_malloc.h>
#endif
#endif

// Export the metadata interface
#define METADATADLL_EXPORTS	1

#include "CFHDMetadata.h"
#include "../Common/AVIExtendedHeader.h"
#include "SampleMetadata.h"
#include "../Codec/metadata.h"
#include "../Codec/lutpath.h"


#define BUFSIZE	1024
/* Table of CRCs of all 8-bit messages. */
static uint32_t crc_table[256];

/* Flag: has the table been computed? Initially false. */
static int crc_table_computed = 0;

/* Make the table for a fast CRC. */
static void make_crc_table(void)
{
 uint32_t c;
 int n, k;

 for (n = 0; n < 256; n++) {
   c = (uint32_t) n;
   for (k = 0; k < 8; k++) {
     if (c & 1)
       c = 0xedb88320L ^ (c >> 1);
     else
       c = c >> 1;
   }
   crc_table[n] = c;
 }
 crc_table_computed = 1;
}

/* Update a running CRC with the bytes buf[0..len-1]--the CRC
  should be initialized to all 1's, and the transmitted value
  is the 1's complement of the final running CRC (see the
  crc() routine below)). */

static uint32_t update_crc(uint32_t crc, unsigned char *buf,
                        int len)
{
 uint32_t c = crc;
 int n;

 if (!crc_table_computed)
   make_crc_table();
 for (n = 0; n < len; n++) {
   c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
 }
 return c;
}

/* Return the CRC of the bytes buf[0..len-1]. */
static uint32_t calccrc(unsigned char *buf, int len)
{
 return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}



/*!
	@function CFHD_OpenMetadata

	@brief Creates an interface to CineForm HD metadata.

	@description This function creates a interface that can be used to
	read CineForm HD metadata.  A reference to the metadata interface
	is returned if the call was successful.

	@return Returns a CFHD error code.
*/
CFHDMETADATA_API CFHD_Error
CFHD_OpenMetadata(CFHD_MetadataRef *metadataRefOut)
{
	// Check the input arguments
	if (metadataRefOut == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	// Allocate a new data structure for metadata
	CSampleMetadata *metadataRef = new CSampleMetadata;
	if (metadataRef == NULL) {
		return CFHD_ERROR_OUTOFMEMORY;
	}

	// Return an opaque reference to the metadata data structure
	*metadataRefOut = (CFHD_MetadataRef)metadataRef;

	return CFHD_ERROR_OKAY;
}


/*!
	@function CFHD_InitSampleMetadata

	@brief Opens an interface to CineForm HD metadata in the specified sample.

	@description This function intializes metadata from a sample of CineForm HD
	encoded video. This is call on each new sample before retrieve any metadata
	from the sample

	@param metadataRef Reference to a metadata interface returned by a call
	to @ref CFHD_OpenMetadata.

	@param track set the type of metadata to be extracted, camera original, user
	changes, and/or filtered against active decoding elements.

	@param sampleData Pointer to a sample of CineForm HD encoded video.

	@param sampleSize Size of the encoded sample in bytes.

	@return Returns a CFHD error code.
*/
CFHDMETADATA_API CFHD_Error
CFHD_InitSampleMetadata(CFHD_MetadataRef metadataRef,
						CFHD_MetadataTrack track,
						void *sampleData,
						size_t sampleSize)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (metadataRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}
//	DAN20140515 -- it is now possible to initialize the metadata for color correction of an image buffer so the sampleData and size will be NULL.
//	if (sampleData == NULL || sampleSize == 0) {
//		return CFHD_ERROR_INVALID_ARGUMENT;
//	}

	CSampleMetadata *metadata = reinterpret_cast<CSampleMetadata *>(metadataRef);

	metadata->m_sampleData = reinterpret_cast<unsigned char *>(sampleData);
	metadata->m_sampleSize = sampleSize;
	metadata->m_currentData = metadata->m_sampleData;
	metadata->m_currentSize = metadata->m_sampleSize;
	metadata->m_metadataStart = NULL;
	metadata->m_metadataTrack = track;
	memset(&metadata->m_currentClipGUID, 0, sizeof(metadata->m_currentClipGUID));

	metadata->FreeDatabase();

	metadata->m_currentUFRM = -1;
	metadata->m_CPLastOffset = 0;

	return errorCode;
}


static void FilterData(CFHD_MetadataTag tag, void *data, CFHD_MetadataSize *size)
{
	switch(tag)
	{
	case TAG_GAMMA_TWEAKS:
	case TAG_WHITE_BALANCE:
		{
			size_t f,num=(int)*size/sizeof(float);
			float *fptr = (float *)data;
			for(f=0;f<num;f++)
			{
				fptr[f] = 1.0;
			}
		}
		break;

	case TAG_COLOR_MATRIX:
		{
			size_t f,num=(int)*size/sizeof(float);
			float *fptr = (float *)data;
			for(f=0;f<num;f++)
			{
				fptr[f] = 0.0;
			}
			fptr[0] = 1.0;
			fptr[5] = 1.0;
			fptr[10] = 1.0;
		}
		break;
	case TAG_LOOK_FILE:
		{
			char *ptr = (char *)data;
			ptr[0] = 0;
			*size = 0;
		}
		break;
	case TAG_LOOK_CRC:
		{
			int *ptr = (int *)data;
			ptr[0] = 0;
		}
		break;
	}
}


static void NewReturnType(CFHD_MetadataType *type, unsigned char ctype)
{
	switch(ctype)
	{
	case METADATA_TYPE_STRING:
		*type = METADATATYPE_STRING;
		break;
	case METADATA_TYPE_SIGNED_BYTE:
		*type = METADATATYPE_UINT8;
		break;
	case METADATA_TYPE_UNSIGNED_BYTE:
		*type = METADATATYPE_UINT8;
		break;
	case METADATA_TYPE_DOUBLE:
		*type = METADATATYPE_DOUBLE;
		break;
	case METADATA_TYPE_FLOAT:
		*type = METADATATYPE_FLOAT;
		break;
	case METADATA_TYPE_GUID:
		*type = METADATATYPE_GUID;
		break;
	case METADATA_TYPE_UNSIGNED_LONG_HEX:
		*type = METADATATYPE_UINT32;
		break;
	case METADATA_TYPE_SIGNED_LONG:
		*type = METADATATYPE_UINT32;
		break;
	case METADATA_TYPE_UNSIGNED_LONG:
		*type = METADATATYPE_UINT32;
		break;
	case METADATA_TYPE_SIGNED_SHORT:
		*type = METADATATYPE_UINT16;
		break;
	case METADATA_TYPE_UNSIGNED_SHORT:
		*type = METADATATYPE_UINT16;
		break;
	case METADATA_TYPE_XML:
		*type = METADATATYPE_XML;
		break;
	case METADATA_TYPE_TAG:
		*type = METADATATYPE_TAG;
		break;
	case METADATA_TYPE_CUSTOM_DATA:
	default:
		*type = METADATATYPE_UNKNOWN;
		break;
	}
}

/*
void GetLUTPath(char PathStr[260])
{
#ifdef _WIN32
	USES_CONVERSION;

	CSettings cfg;
	cfg.Open(HKEY_CURRENT_USER, ("SOFTWARE\\CineForm\\ColorProcessing"));
	CComBSTR path(cfg.GetString(_T("LUTPath"), _T("C:/Program Files/Common Files/CineForm/LUTs")));
	strcpy(PathStr, CW2T(path));
#else
	strcpy(PathStr, "/Library/Application Support/CineForm/LUTs");

#endif
}
*/

#ifdef _WIN32
static int WINAPI lstrlenWInternal(LPCWSTR lpString)
{
    int i = -1;
    while (*(lpString+(++i)))
        ;
    return i;
}
#endif


void *LeftRightDelta(CSampleMetadata *metadata, 
						CFHD_MetadataTag tag,
						METADATA_SIZE size,
						METADATA_TYPE type,
						void *ldata)
{
	void *ddata = NULL;

	if(metadata == NULL)
		return ldata;

	if (type != METADATA_TYPE_FLOAT) // only deltas on float values
		return ldata;

	assert(0 < size && size <= METADATA_SIZE_MAX);
	if ((size_t)size > sizeof(metadata->scratch_buffer)) {
		return ldata;
	}
	
	memcpy(metadata->scratch_buffer, ldata, size);
	ldata = metadata->scratch_buffer;

	if(metadata->m_metadataTrack & METADATAFLAG_RIGHT_EYE)
	{
		METADATA_SIZE lsize;
		METADATA_TYPE lctype;

		ddata = MetadataFind(metadata->m_databaseDataR, metadata->m_databaseSizeR,
															  tag, &lsize, &lctype);
	}
	else if(metadata->m_metadataTrack & METADATAFLAG_LEFT_EYE)
	{
		METADATA_SIZE lsize;
		METADATA_TYPE lctype;

		ddata = MetadataFind(metadata->m_databaseDataL, metadata->m_databaseSizeL,
															  tag, &lsize, &lctype);
	}

	if(ddata)
	{
		int i;
		float *fldata = (float *)ldata;
		float *fddata = (float *)ddata;
		const int item_count = size/sizeof(float);

		switch(tag)
		{
		case TAG_WHITE_BALANCE:
		case TAG_EXPOSURE:
		case TAG_RGB_GAIN:
		case TAG_FRAME_ZOOM:
		case TAG_FRAME_DIFF_ZOOM:
			for (i = 0; i < item_count; i++)
			{
				*fldata++ *= (*fddata++);
			}
			break;
		default:
		//case TAG_HORIZONTAL_OFFSET:
		//case TAG_VERTICAL_OFFSET:
		//case TAG_ROTATION_OFFSET:
		//case TAG_GAMMA_TWEAKS:
		//case TAG_RGB_OFFSET:
		//case TAG_SATURATION:
		//case TAG_CONTRAST:
			for (i = 0; i < item_count; i++)
			{
				*fldata++ += (*fddata++);
			}
			break;
		}

	}
	return ldata;
}

#if _WIN32
uint32_t GetLastWriteTime(char *name)
{
    HANDLE hFile;
    FILETIME ftCreate, ftAccess, ftWrite;
	uint32_t ret = 0;

    hFile = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if(hFile == INVALID_HANDLE_VALUE)
    {
        return 0;
    }        

    // Retrieve the file times for the file.
    if (!GetFileTime(hFile, &ftCreate, &ftAccess, &ftWrite))
	{
	    CloseHandle(hFile);    
        return 0;
	}

	ret = ftWrite.dwLowDateTime;

    CloseHandle(hFile);    

	return ret;
}
#endif


//TODO: Needs upgrade for stereo, col2, colb and global overrides.
bool CSampleMetadata::GetClipDatabase()
{
	if(	m_currentClipGUID.Data1 == 0 &&
		m_currentClipGUID.Data2 == 0 &&
		m_currentClipGUID.Data3 == 0)
	{
		void *data;
		METADATA_SIZE size;
		METADATA_TYPE type;

		data = MetaDataFindInSample(m_sampleData, m_sampleSize,
									TAG_CLIP_GUID, &size, &type);
		if (data)
		{
			if(size == sizeof(m_currentClipGUID))
			{
				memcpy(&m_currentClipGUID, data, sizeof(m_currentClipGUID));
			}
		}
	}

	//Get any changes from the database
	if(	m_currentClipGUID.Data1 ||
		m_currentClipGUID.Data2 ||
		m_currentClipGUID.Data3)
	{
		char filenameGUID[260];
		//char namelen = 0;

		if(PathStr[0] == 0 || DBStr[0] == 0) 
		{
			//OutputDebugString("InitGetLUTPaths");
			InitGetLUTPaths(PathStr, (size_t)sizeof(PathStr), DBStr, (size_t)sizeof(DBStr));
		}
		//GetLUTPath(PathStr);
		bool checkdiskinfo = false;

#ifdef _WIN32
		sprintf_s(filenameGUID, sizeof(filenameGUID), 
#else	
		sprintf(filenameGUID,
#endif
					"%s/%s/%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X.colr",
					PathStr,DBStr,
					m_currentClipGUID.Data1,
					m_currentClipGUID.Data2,
					m_currentClipGUID.Data3,
					m_currentClipGUID.Data4[0],
					m_currentClipGUID.Data4[1],
					m_currentClipGUID.Data4[2],
					m_currentClipGUID.Data4[3],
					m_currentClipGUID.Data4[4],
					m_currentClipGUID.Data4[5],
					m_currentClipGUID.Data4[6],
					m_currentClipGUID.Data4[7]);


#if _WIN32
		uint32_t write_time = GetLastWriteTime(filenameGUID);
		//char t[100];
		//sprintf(t,"last_write_time %d %s", last_write_time,filenameGUID);
		//OutputDebugString(t);
		if(write_time != last_write_time || m_databaseSize == 0)
		{
			last_write_time = write_time;
			checkdiskinfo = true;
		}
#else
		//TODO -- something like the above for Mac/Linux
		clock_t process_time = clock();
		time_t now = time(NULL);
		uint32_t diff = (uint32_t)process_time - (uint32_t)last_process_time;

		#define MS_DIFF	(CLOCKS_PER_SEC / 15)
		if(diff > MS_DIFF || last_process_time==0 || last_now_time != now) // only test every 1000ms
		{	
			last_process_time = (unsigned int)process_time;
			last_now_time = now;
			checkdiskinfo = true;
		}
#endif

		if(checkdiskinfo)
		{
			FILE *fp;
			int err = 0;

#ifdef _WIN32
			err = fopen_s(&fp, filenameGUID, "rb");
#else
			fp = fopen(filenameGUID, "rb");
#endif
			if (err == 0 && fp != NULL)
			{
				uint32_t len = 0;
				fseek (fp, 0, SEEK_END);
				len = ftell(fp);

				if(m_databaseSize > 0 && m_databaseSize < len && m_databaseData)
				{
					Free(m_databaseData);
					m_databaseSize = 0;
					m_databaseData = NULL;
				}
				if(m_databaseSize < len || m_databaseData == NULL)
					m_databaseData = (unsigned char *)Alloc(len);

				if(m_databaseData)
				{
					fseek (fp, 0, SEEK_SET);
#ifdef _MSC_VER
					len = (uint32_t)fread_s(m_databaseData, len, 1, len, fp);
#else
					len = (uint32_t)fread(m_databaseData,1,len,fp);
#endif

					m_databaseSize = ValidMetadataLength(m_databaseData, len);
				}
				else
				{
					m_databaseSize = 0;
				}

				fclose(fp);
			}
		}

	/*	DAN20120104 -- stop loading very old .COL1 and .COL2 files.

		if(m_databaseSize && m_databaseData)
		{
			namelen = (int)strlen(filenameGUID);
			
			// left eye delta
			filenameGUID[namelen-1] = '1'; 
			fp = fopen(filenameGUID,"rb");
			if (fp != NULL)
			{
				int len = 0;
				fseek (fp, 0, SEEK_END);
				len = ftell(fp);

				m_databaseDataL = (unsigned char *)Alloc(len);

				if(m_databaseDataL)
				{
					fseek (fp, 0, SEEK_SET);
					fread(m_databaseDataL,1,len,fp);

					m_databaseSizeL = ValidMetadataLength(m_databaseDataL, len);
				}
				else
				{
					m_databaseSizeL = 0;
				}

				fclose(fp);
			}

			
			// left eye delta
			filenameGUID[namelen-1] = '2'; 
			fp = fopen(filenameGUID,"rb");
			if (fp != NULL)
			{
				int len = 0;
				fseek (fp, 0, SEEK_END);
				len = ftell(fp);

				m_databaseDataR = (unsigned char *)Alloc(len);

				if(m_databaseDataR)
				{
					fseek (fp, 0, SEEK_SET);
					fread(m_databaseDataR,1,len,fp);

					m_databaseSizeR = ValidMetadataLength(m_databaseDataR, len);
				}
				else
				{
					m_databaseSizeR = 0;
				}

				fclose(fp);
			}
		}
	*/
	}

	if(m_databaseSize && m_databaseData)
		return true;
	else
		return false;
}


/* -- not include in Doxygen
 @function CFHD_ReadMetadataFromSample

 @brief OBSOLETE use CFHD_ReadMetadata(). Returns the buffer of metadata from the current sample..

 @description After a call to @ref CFHD_InitSampleMetadata the next call to
 this function returns all the metadata from all metadata blocks in the sample.

 @param metadataRef Reference to a metadata interface returned by a call
 to @ref CFHD_OpenMetadata.

 @param data Pointer to the variable to receive the address of the metadata.

 @param size Pointer to the variable to receive the size of the metadata
 array in bytes.

 @return Returns the CFHD error code CFHD_ERROR_METADATA_END if no more
 metadata was not found in the sample; otherwise, the CFHD error code
 CFHD_ERROR_OKAY is returned if the operation succeeded.
 */
CFHDMETADATA_API CFHD_Error
CFHD_ReadMetadataFromSample(CFHD_MetadataRef metadataRef,
							void **data_out,
							size_t *size_out)
{
	CFHD_Error error = CFHD_ERROR_OKAY;
    unsigned char *ptr;

	// Check the input arguments
	if (metadataRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	if (data_out == NULL || size_out == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleMetadata *metadata = (CSampleMetadata *)metadataRef;

	// Total size of the collection of metadata (in bytes)
	size_t total_size = 0;

	// Attributes of the first metadata item in the collection
	METADATA_TAG tag;
	METADATA_SIZE size;
	METADATA_TYPE type;

    // note: total_size only contains the size of the first metadata block
	metadata->m_metadataStart = MetaDataFindFirst(metadata->m_sampleData, metadata->m_sampleSize,
												  &total_size, &tag, &size, &type);
	if (metadata->m_metadataStart)
	{
		*data_out = (void *)((intptr_t)(metadata->m_metadataStart) - 8);
		*size_out = total_size;
		metadata->m_lastMetadata = metadata->m_metadataStart;
	}
	else
	{
		return CFHD_ERROR_METADATA_END;
	}
    
    // Keep looking for additional metadata blocks. The assumption is that all metadata blocks
    // are adjacent. Additional metadata blocks are defined by
    // 0xbf 0xfe sz1 sz0
    // where "0xbf 0xfe" is a tag, sz1 is the most-significant byte of the size, and sz0 is
    // the least significant byte of the size. The size is the number of 4-byte words in the
    // tag.
    
    ptr = (unsigned char *)(*data_out) + *size_out;
    
    while (ptr[0] == 0xbf && ptr[1] == 0xfe && ptr < metadata->m_sampleData + metadata->m_sampleSize)
    {
        unsigned short words = (ptr[2] << 8) + ptr[3];
        *size_out += words * 4;
        ptr = (unsigned char *)(*data_out) + *size_out;
    }
    
	return error;
}

/*!
	@function CFHD_ReadMetadata

	@brief Returns the next available metadata entry. Calling recursively will
	retrieve all the samples metadata until CFHD_ERROR_METADATA_END is returned.

	@description After a call to @ref CFHD_InitSampleMetadata the next call to
	this function returns the first metadata tag/size/value group.  The next call
	returns the next metadata group and so on until all the data is extracted.

	@param metadataRef Reference to a metadata interface returned by a call
	to @ref CFHD_OpenMetadata.

	@param tag Pointer to the variable to receive the FOURCC metadata tag.

	@param type Pointer to the variable to receive the CFHD_MetadataType.  This
	specify the type of data returned, such as METADATATYPE_STRING,
	METADATATYPE_UINT32 or METADATATYPE_FLOAT.

	@param data Pointer to the variable to receive the address of the metadata.

	@param size Pointer to the variable to receive the size of the metadata
	array in bytes.

	@return Returns the CFHD error code CFHD_ERROR_METADATA_END if no more
	metadata was not found in the sample; otherwise, the CFHD error code
	CFHD_ERROR_OKAY is returned if the operation succeeded.
*/
CFHDMETADATA_API CFHD_Error
CFHD_ReadMetadata(CFHD_MetadataRef metadataRef,
				  CFHD_MetadataTag *tag,
				  CFHD_MetadataType *type,
				  void **data,
				  CFHD_MetadataSize *size)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (metadataRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}
	if (tag == NULL || type == NULL || data == NULL || size == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleMetadata *metadata = (CSampleMetadata *)metadataRef;
	METADATA_TYPE ctype;

	if(metadata->m_metadataStart == NULL && metadata->m_currentData == metadata->m_sampleData)
	{
		size_t metadatasize = 0;

		if(metadata->m_metadataTrack & METADATAFLAG_FILTERED)
		{
			void *data = NULL;
			METADATA_TYPE type;
			METADATA_SIZE size;

			data = MetaDataFindInSample(metadata->m_sampleData, metadata->m_sampleSize,
										TAG_PROCESS_PATH, &size, &type);
			if (data)
			{
				metadata->m_active_mask = *((unsigned int *)data);
			}
			else
			{
				metadata->m_active_mask = 0;
			}

			if(metadata->GetClipDatabase())
			{
//dan20100916 MetaDataFindInSample
				data = MetadataFind(metadata->m_databaseData, metadata->m_databaseSize,
											TAG_PROCESS_PATH, &size, &type);
				if (data)
				{
					metadata->m_active_mask = *((unsigned int *)data);
				}
			}
		}

		metadata->m_metadataStart = MetaDataFindFirst(metadata->m_sampleData, metadata->m_sampleSize,
													  &metadatasize, tag, size, &ctype);
		if (metadata->m_metadataStart)
		{
			*data = metadata->m_metadataStart;
			metadata->m_lastMetadata = *data;
		}
		else
		{
			return CFHD_ERROR_METADATA_END;
		}
	}
	else
	{
		METADATA_FLAGS flags = (metadata->m_currentData == metadata->m_sampleData);
		*data = MetaDataFindNext(
					(unsigned char *)metadata->m_currentData,
					(int)metadata->m_currentSize,
					(void **)&metadata->m_metadataStart,
					(void *)metadata->m_lastMetadata,
					(METADATA_TAG *)tag,
					size,
					&ctype,
					flags);
		if (*data)
		{
			metadata->m_lastMetadata = *data;
		}
		else
		{
			if(metadata->m_currentData == metadata->m_sampleData && metadata->m_metadataTrack & METADATAFLAG_MODIFIED)
			{
				if(metadata->GetClipDatabase())
				{
					uint8_t *firstdata = 8 + (uint8_t *)metadata->m_databaseData;
					*data = MetaDataFindNext(metadata->m_databaseData,
											(int)metadata->m_databaseSize,
											(void **)&metadata->m_metadataStart,
											(void *)firstdata,
											tag,
											size,
											&ctype,
											0);
					if (*data)
					{
						metadata->m_lastMetadata = *data;
			
						metadata->m_currentData = metadata->m_databaseData;
						metadata->m_currentSize = metadata->m_databaseSize;
					}
					else
					{
						return CFHD_ERROR_METADATA_END;
					}
				}
				else
				{
					return CFHD_ERROR_METADATA_END;
				}
			}
			else
			{
				return CFHD_ERROR_METADATA_END;
			}
		}
	}

	if(metadata->m_metadataTrack & METADATAFLAG_MODIFIED)
	{
		if(metadata->GetClipDatabase())
		{
			void *ldata;
			METADATA_SIZE lsize;
			METADATA_TYPE lctype;
//dan20100916 was MetaDataFindInSample
			ldata = MetadataFind(metadata->m_databaseData, metadata->m_databaseSize,
										 *tag, &lsize, &lctype);

			if (ldata)
			{

				//TODO LRDIFF -- issue can't modify *(*data) as this is the base we are differencing against
				ldata = LeftRightDelta(metadata, *tag, lsize, lctype, ldata);

				*data = ldata;
				*size = lsize;
				ctype = lctype;

				if(lctype == METADATA_TYPE_FLOAT) // floats may be keyframed
				{
					if (static_cast<int32_t>(metadata->m_currentUFRM) == -1)
					{
						void *data = NULL;
						METADATA_TYPE type;
						METADATA_SIZE size;

						if(metadata->m_overrideSize)
						{
							//void *ldata;
							//METADATA_SIZE lsize;
							//METADATA_TYPE lctype;

							data = MetadataFind(metadata->m_overrideData, metadata->m_overrideSize,
															  TAG_UNIQUE_FRAMENUM, &size, &type);

							if (data)
							{
								metadata->m_currentUFRM = *(int *)data;
							}
						}
					}
		
					if (static_cast<int32_t>(metadata->m_currentUFRM) == -1)
					{
						void *data = NULL;
						METADATA_TYPE type;
						METADATA_SIZE size;
						data = MetaDataFindInSample(metadata->m_sampleData, metadata->m_sampleSize,
											TAG_UNIQUE_FRAMENUM, &size, &type);
						if (data)
						{
							metadata->m_currentUFRM = *(int *)data;
						}
					}

					if(metadata->m_currentUFRM >= 0)
					{			
						ldata = MetadataFindAtUniqueFrame(metadata->m_databaseData, metadata->m_databaseSize,
										  metadata->m_currentUFRM, *tag, &lsize, &lctype);
						if (ldata)
						{							
							//TODO LRDIFF
							ldata = LeftRightDelta(metadata, *tag, lsize, lctype, ldata);

							*data = ldata;
							*size = lsize;
							ctype = lctype;
						}
					}
				}
			}
		}
	}

	if(metadata->m_active_mask)
	{
		FilterData(*tag, *data, size);
	}

	NewReturnType(type, ctype);

	return errorCode;
}



typedef struct metadataCheck
{
	uint32_t tag;
	uint32_t size;
	uint32_t data[4];
} metadataCheck;

metadataCheck unityMD[] =
{
    {0x56524345,0x00000001,{0x00020b05,0x00000000,0x00000000,0x00000000}}, //ECRV
    {0x56524344,0x00000001,{0x00020b05,0x00000000,0x00000000,0x00000000}}, //DCRV
    {0x44334650,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //PF3D
    {0x4352434c,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //LCRC
    {0x55544153,0x00000001,{0x3f800000,0x00000000,0x00000000,0x00000000}}, //SATU
    {0x48534c42,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //BLSH
    {0x53525443,0x00000001,{0x3f800000,0x00000000,0x00000000,0x00000000}}, //CTRS
    {0x53505845,0x00000001,{0x3f800000,0x00000000,0x00000000,0x00000000}}, //EXPS
    {0x4c414257,0x00000004,{0x3f800000,0x3f800000,0x3f800000,0x3f800000}}, //WBAL
    {0x47424752,0x00000003,{0x3f800000,0x3f800000,0x3f800000,0x00000000}}, //RGBG
    {0x544d4147,0x00000003,{0x3f800000,0x3f800000,0x3f800000,0x00000000}}, //GAMT
    {0x4f424752,0x00000003,{0x00000000,0x00000000,0x00000000,0x00000000}}, //RGBO
    {0x4d5a5441,0x00000001,{0x00000001,0x00000000,0x00000000,0x00000000}}, //ATZM
    {0x46464f56,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //VOFF
    {0x46464f48,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //HOFF
    {0x46464f52,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //ROFF
    {0x4d4f5a44,0x00000001,{0x3f800000,0x00000000,0x00000000,0x00000000}}, //DZOM
    {0x5453594b,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //KYST
    {0x544c4954,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //TILT
    {0x4c4b534d,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //MSKL
    {0x524b534d,0x00000001,{0x80000000,0x00000000,0x00000000,0x00000000}}, //MSKR
    {0x4d4f4f5a,0x00000001,{0x3f800000,0x00000000,0x00000000,0x00000000}}, //ZOOM
    {0x5846464f,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //OFFX
    {0x5946464f,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //OFFY
    {0x5246464f,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //OFFR
    {0x4846464f,0x00000001,{0x3f800000,0x00000000,0x00000000,0x00000000}}, //OFFH
    {0x4446464f,0x00000001,{0x3f800000,0x00000000,0x00000000,0x00000000}}, //OFFD
    {0x534e4756,0x00000001,{0x3f800000,0x00000000,0x00000000,0x00000000}}, //VGNS
    {0x50464843,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //CHFP
    {0x50575343,0x00000001,{0x00000000,0x00000000,0x00000000,0x00000000}}, //CSWP
    {0x4b53414d,0x00000008,{0x00000000,0x00000000,0x00000000,0x00000000}}, //MASK
    {0x00000000,0x00000000,{0x00000000,0x00000000,0x00000000,0x00000000}}, // terminator
};


static uint32_t ScanForAMChanges(void *mdData, size_t sampleSize)
{
	void *startmetadata = mdData;
	void *lastdata = ((char *)mdData + 8);
	METADATA_TAG tag;
	METADATA_TYPE type;
	METADATA_SIZE size;
	void *data;
	uint32_t ret = 1, i;

	if(sampleSize < 12) return 1;

	while((data = MetaDataFindNext(
				(unsigned char *)mdData,
				(int)sampleSize,
				(void **)&startmetadata,
				lastdata,
				&tag,
				&size,
				&type,
				false)))
	{
		if(tag == 0) break;

		lastdata = data;
		size >>= 2;

		uint32_t *lptr = (uint32_t *)data;

		i = 0;
		while(unityMD[i].tag != 0 && unityMD[i].tag != tag) i++;

		if(unityMD[i].tag == tag) // compare contents
		{
			int j;
			if(unityMD[i].size != size)
			{
				return 0;
			}
			for(j=0; j<size; j++)
				if(unityMD[i].data[j] != lptr[j])
				{
					return 0;
				}
		}
	}

	return ret;
}

/*!
	@function CFHD_FindMetadata

	@brief Returns the data for a particular metadata entry.

	@description After a call to @ref CFHD_InitSampleMetadata the next call to
	this function returns the data for an particular metadata entry.

	@param metadataRef Reference to a metadata interface returned by a call
	to @ref CFHD_OpenMetadata.

	@param tag is the FOURCC for the requested data.

	@param type Pointer to the variable to receive the CFHD_MetadataType.  This
	specify the type of data returned, such as METADATATYPE_STRING,
	METADATATYPE_UINT32 or METADATATYPE_FLOAT.

	@param data Pointer to the variable to receive the address of the metadata.

	@param size Pointer to the variable to receive the size of the metadata
	array in bytes.

	@return Returns the CFHD error code CFHD_ERROR_METADATA_END if no more
	metadata was not found in the sample; otherwise, the CFHD error code
	CFHD_ERROR_OKAY is returned if the operation succeeded.
*/
CFHDMETADATA_API CFHD_Error
CFHD_FindMetadata(CFHD_MetadataRef metadataRef,
				  CFHD_MetadataTag tag,
				  CFHD_MetadataType *type,
				  void **data,
				  CFHD_MetadataSize *size)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;
	uint32_t smart_render_ok = 1;
	//fprintf(stdout,"Findmetadata\n");
	// Check the input arguments
	if (metadataRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}
	if (tag == 0 || type == NULL || data == NULL || size == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	//clock_t process_time = clock();


	CSampleMetadata *metadata = (CSampleMetadata *)metadataRef;

	{
		unsigned char ctype;

		if(metadata->m_active_mask == 0)
		{
			if(	metadata->m_metadataTrack & METADATAFLAG_FILTERED)
			{
				void *data = NULL;
				METADATA_TYPE type;
				METADATA_SIZE size;
				//unsigned int tag;
				data = MetaDataFindInSample(metadata->m_sampleData, metadata->m_sampleSize,
											TAG_PROCESS_PATH, &size, &type);
				if (data)
				{
					metadata->m_active_mask = *((unsigned int *)data);
				}
				else
				{
					metadata->m_active_mask = 0;
				}

				if(metadata->GetClipDatabase())
				{
//dan20100916 was MetaDataFindInSample
					data = MetadataFind(metadata->m_databaseData, metadata->m_databaseSize,
												TAG_PROCESS_PATH, &size, &type);
					if (data)
					{
						metadata->m_active_mask = *((unsigned int *)data);
					}
				}
			}
		}

		if(tag == TAG_CLIP_HASH || tag == TAG_SMART_RENDER_OK)
		{
		//	char t[100];
		//	sprintf(t,"HASH %08x", (uint32_t)metadataRef);
		//	OutputDebugString(t);
			if(	metadata->m_currentClipGUID.Data1 == 0 &&
				metadata->m_currentClipGUID.Data2 == 0 &&
				metadata->m_currentClipGUID.Data3 == 0)
			{
				void *data;
				METADATA_SIZE size;
				METADATA_TYPE type;

				if(metadata->m_sampleData && metadata->m_sampleSize) // which is absolutely should have
				{
					data = MetaDataFindInSample(metadata->m_sampleData, metadata->m_sampleSize,
												TAG_CLIP_GUID, &size, &type);
					if (data)
					{
						if(size == sizeof(metadata->m_currentClipGUID))
						{
							memcpy(&metadata->m_currentClipGUID, data, sizeof(metadata->m_currentClipGUID));
						}
					}
				}
			}

			if(tag == TAG_CLIP_HASH)
			{
				metadata->m_hash =  calccrc((unsigned char *)&metadata->m_currentClipGUID, (int)16);
			}

			if(tag == TAG_SMART_RENDER_OK)
			{
				void *data;
				METADATA_SIZE size;
				METADATA_TYPE type;

				if(metadata->m_sampleData && metadata->m_sampleSize) // which is absolutely should have
				{
					data = MetaDataFindInSample(metadata->m_sampleData, metadata->m_sampleSize,
												TAG_CLIP_GUID, &size, &type);
					if (data)
					{
						uint16_t *sptr = (uint16_t *)data;

						sptr -= 6; // should point to //BFFE

						if(*sptr == 0xFEBF)
						{
							size = ((sptr[1]>>8)&0xff) | ((sptr[1]&0xff)<<8);
							size <<= 2; // in bytes

							smart_render_ok = ScanForAMChanges(&sptr[2], size);
						}
					}
				}
			}
		}
		else
		{
		//	char t[100];
		//	sprintf(t,"Find %c%c%c%c", tag&0xff, (tag>>8)&0xff, (tag>>16)&0xff, (tag>>24)&0xff);
		//	OutputDebugString(t);

			//fprintf(stdout,"Call MetaDataFindInSample\n");
			*data = MetaDataFindInSample(metadata->m_sampleData, metadata->m_sampleSize,
        			tag, size, &ctype);
			//fprintf(stdout, "Metadata is at %08x\n",*data);
		}
		
		

		if(metadata->m_metadataTrack & METADATAFLAG_MODIFIED)
		{
			//Get any changes from the database

			if(metadata->GetClipDatabase())
			{
				void *ldata = NULL;
				METADATA_SIZE lsize;
				METADATA_TYPE lctype;

				
				if( tag == TAG_SMART_RENDER_OK && metadata->m_databaseSize && smart_render_ok == 1)
				{
					smart_render_ok = ScanForAMChanges(metadata->m_databaseData, metadata->m_databaseSize);
				}

				if(tag == TAG_CLIP_HASH && metadata->m_databaseSize)
				{	
					metadata->m_hash ^= calccrc((unsigned char *)metadata->m_databaseData, (int)metadata->m_databaseSize);
				}
				else if(tag == TAG_CONTROL_POINT)
				{
					int reduced_size = metadata->m_databaseSize-metadata->m_CPLastOffset;
					if(reduced_size > 0)
						ldata = MetadataFind(metadata->m_databaseData+metadata->m_CPLastOffset, reduced_size,
												  tag, &lsize, &lctype);
					if(ldata == NULL)
						return CFHD_ERROR_END_OF_DATABASE;

					metadata->m_CPLastOffset = (uint32_t)((unsigned char *)ldata - metadata->m_databaseData + lsize); 
				}
				else
				{
					ldata = MetadataFind(metadata->m_databaseData, metadata->m_databaseSize,
												  tag, &lsize, &lctype);
				}
				if (ldata)
				{
					//TODO LRDIFF
					ldata = LeftRightDelta(metadata, tag, lsize, lctype, ldata);

					*data = ldata;
					*size = lsize;
					ctype = lctype;


					if(lctype == METADATA_TYPE_FLOAT) // floats may be keyframed
					{
						//TODO: Replace use of -1 with something that does not produce compiler warnings
						if (static_cast<int32_t>(metadata->m_currentUFRM) == -1)
						{
							void *data = NULL;
							METADATA_TYPE type;
							METADATA_SIZE size;

							if(metadata->m_overrideSize)
							{
								//void *ldata;
								//METADATA_SIZE lsize;
								//METADATA_TYPE lctype;

								data = MetadataFind(metadata->m_overrideData, metadata->m_overrideSize,
																  TAG_UNIQUE_FRAMENUM, &size, &type);

								if (data)
								{
									metadata->m_currentUFRM = *(int *)data;
								}
							}
						}

						if (static_cast<int32_t>(metadata->m_currentUFRM) == -1)
						{
							void *data = NULL;
							METADATA_TYPE type;
							METADATA_SIZE size;
							data = MetaDataFindInSample(metadata->m_sampleData, metadata->m_sampleSize,
												TAG_UNIQUE_FRAMENUM, &size, &type);
							if (data)
							{
								metadata->m_currentUFRM = *(int *)data;
							}
						}

						if(metadata->m_currentUFRM >= 0)
						{			
							ldata = MetadataFindAtUniqueFrame(metadata->m_databaseData, metadata->m_databaseSize,
											  metadata->m_currentUFRM, tag, &lsize, &lctype);
							if (ldata)
							{								
								//TODO LRDIFF
								ldata = LeftRightDelta(metadata, tag, lsize, lctype, ldata);

								*data = ldata;
								*size = lsize;
								ctype = lctype;
							}
						}
					}
				}
			}
		}

		if(metadata->m_overrideSize)
		{
			void *ldata;
			METADATA_SIZE lsize;
			METADATA_TYPE lctype;

				
			if( tag == TAG_SMART_RENDER_OK && metadata->m_overrideSize && smart_render_ok == 1)
			{
				smart_render_ok = ScanForAMChanges(metadata->m_overrideData, metadata->m_overrideSize);
			}

			if(tag == TAG_CLIP_HASH && metadata->m_overrideSize)
			{	
				metadata->m_hash ^= calccrc((unsigned char *)metadata->m_overrideData, (int)metadata->m_overrideSize);
			}
			else
			{
				ldata = MetadataFind(metadata->m_overrideData, metadata->m_overrideSize,
												  tag, &lsize, &lctype);

				if (ldata)
				{
					*data = ldata;
					*size = lsize;
					ctype = lctype;
				}
			}
		}

		if(tag == TAG_SMART_RENDER_OK)
		{		
			metadata->m_smart_render_ok = smart_render_ok;
			*data = &metadata->m_smart_render_ok;
			*type = METADATATYPE_UINT32;
			*size = 4;
		}
		else
		if(tag == TAG_CLIP_HASH)
		{
		//	char t[100];
		//	sprintf(t,"HASH %08x", (uint32_t)metadata->m_hash);
		//	OutputDebugString(t);
			// To add in the global override, not just m_overrideData which is local to the SDK
			*data = &metadata->m_hash;
			*type = METADATATYPE_UINT32;
			*size = 4;
		}
		else
		{
			if(metadata->m_active_mask)
			{
				FilterData(tag, *data, size);
			}

			NewReturnType(type, ctype);
		}
	}

	return errorCode;
}



/*!
	@function CFHD_CloseMetadata

	@brief Releases an interface to CineForm HD metadata.

	@description This function releases an interface to CineForm HD metadata
	created by calls to routines that obtain the metadata from various sources,
	such as @ref CFHD_ReadMetadata.  All resources allocated by the
	metadata interface are released.  It is a serious error to call any functions
	in the metadata API after the interface has been released.

	@param metadataRef Reference to a metadata interface returned by a call
	to @ref CFHD_OpenMetadata.

	@return Returns a CFHD error code.
*/
CFHDMETADATA_API CFHD_Error
CFHD_CloseMetadata(CFHD_MetadataRef metadataRef)
{
	//CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (metadataRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleMetadata *metadata = (CSampleMetadata *)metadataRef;

	metadata->FreeDatabase();
	delete metadata;

	return CFHD_ERROR_OKAY;
}








/*-- not include in Doxygen
 *	@brief Add a new metadata tuple to the block of metadata
 *	@param maxsize added as an int parameter by CMD 20090605 and changed to size_t by BGS.
 */
bool CSampleMetadata::AddMetaData(uint32_t Tag, unsigned int typesizebytes, void *pData)
{
	//int type = typesizebytes >> 24;
	int size = typesizebytes & 0xffffff;
	int allocsize = (8 + (size + 3)) & 0xfffffc;

	if (pData && size && (m_overrideSize + allocsize < MAX_OVERRIDE_SIZE))
	{
		int found = 0;

		{
			// If TAG pairs or Freespace or last char or FOURCC is lower, don't check of existing tag duplicates.
			if((Tag>>24) < 'a' && Tag != TAG_FREESPACE && Tag != TAG_REGISTRY_NAME && Tag != TAG_REGISTRY_VALUE && Tag != TAG_NAME && Tag != TAG_VALUE)
			{
				int i,offset = m_overrideSize;
				uint8_t *newdata = (uint8_t *)m_overrideData;
				uint8_t *srcdata = (uint8_t *)pData;
				uint32_t *Lnewdata;
				uint32_t *Lstartdata = (uint32_t *)newdata;
				int pos=0,longs = offset >> 2;
				newdata += offset;
				Lnewdata = (uint32_t *)newdata;


				while(pos < longs)
				{
					int datalen;
					if(Lstartdata[pos] == Tag)
					{
						int currsize = (Lstartdata[pos+1] & 0xffffff);

						if(currsize == size) // replace the data
						{
							Lnewdata = &Lstartdata[pos];

							*Lnewdata++ = Tag;
							*Lnewdata++ = typesizebytes;
							newdata = (uint8_t *)Lnewdata;
							for(i=0;i<size;i++)
							{
								*newdata++ = *srcdata++;
							}
							for(;i<((size+3) & 0xfffffc);i++)
							{
								*newdata++ = 0;
							}
							found = 1;
						}
						else // remove the data
						{
							int longsize = ((currsize+8+3)&~3)>>2;
							int p;
							for(p=pos; p<pos+longsize && p<longs-longsize; p++)
							{
								Lstartdata[p] = Lstartdata[p+longsize];
							}

							m_overrideSize -= longsize<<2;
							
							found = 0;
						}
						break;
					}
					else
					{
						datalen = ((Lstartdata[pos+1] & 0xffffff)+3)>>2;

						pos += 2;
						pos += datalen;
					}
				}
			}
		}

		if(!found)
		{
			int i,offset = m_overrideSize;
			uint8_t *newdata = (uint8_t *)m_overrideData;
			uint8_t *srcdata = (uint8_t *)pData;
			uint32_t *Lnewdata;
			uint32_t *Lstartdata = (uint32_t *)newdata;
			int pos=0,longs = offset >> 2;
			newdata += offset;
			Lnewdata = (uint32_t *)newdata;			


			while(pos < longs)
			{
				int datalen;

				if (Lstartdata[pos] == TAG_FREESPACE &&
					(((int)(Lstartdata[pos+1] & 0xffffff)) >= size))
				{
					int freebytes = (Lstartdata[pos+1] & 0xffffff);

					Lnewdata = &Lstartdata[pos];

					*Lnewdata++ = Tag;
					*Lnewdata++ = typesizebytes;

					newdata = (uint8_t *)Lnewdata;
					for(i=0;i<size;i++)
					{
						*newdata++ = *srcdata++;
					}
					for(;i<((size+3) & 0xfffffc);i++)
					{
						*newdata++ = 0;
					}
					found = 1;

					uintptr_t temp = (uintptr_t)newdata;
					Lnewdata = (uint32_t *)((temp + 3) & 0xfffffffc);
					freebytes -= (size+3) & 0xfffffc;
					freebytes -= 8; // TAG + typesize
					if(freebytes > 16)
					{
						*Lnewdata++ = TAG_FREESPACE;
						*Lnewdata++ = ('c'<<24)|freebytes;
					}
					else
					{
						allocsize -= freebytes;
					}
					break;
				}
				else
				{
					datalen = ((Lstartdata[pos+1] & 0xffffff)+3)>>2;

					pos += 2;
					pos += datalen;
				}
			}


			if(!found)
			{

				offset = m_overrideSize;
				*Lnewdata++ = Tag;
				*Lnewdata++ = typesizebytes;
				newdata = (uint8_t *)Lnewdata;
				for(i=0;i<size;i++)
				{
					*newdata++ = *srcdata++;
				}
				for(;i<((size+3) & 0xfffffc);i++)
				{
					*newdata++ = 0;
				}

				m_overrideSize += allocsize;
			}
			return true;
		}
		else
		{
			return false;
		}
	}
	return false;
}



/*-- not include in Doxygen
 *	@brief Add a new metadata tuple to the block of metadata
 *	@param maxsize added as an int parameter by CMD 20090605 and changed to size_t by BGS.
 */
bool CSampleMetadata::AddMetaDataWorkspace(uint32_t Tag, unsigned int typesizebytes, void *pData)
{
	//int type = typesizebytes >> 24;
	int size = typesizebytes & 0xffffff;
	int allocsize = (8 + (size + 3)) & 0xfffffc;

	if (pData && size && (m_workspaceSize + allocsize < MAX_OVERRIDE_SIZE))
	{
		int found = 0;

		{
			// If TAG pairs or Freespace or last char or FOURCC is lower, don't check of existing tag duplicates.
			if((Tag>>24) < 'a' && Tag != TAG_FREESPACE && Tag != TAG_REGISTRY_NAME && Tag != TAG_REGISTRY_VALUE && Tag != TAG_NAME && Tag != TAG_VALUE)
			{
				int i,offset = m_workspaceSize;
				uint8_t *newdata = (uint8_t *)m_workspaceData;
				uint8_t *srcdata = (uint8_t *)pData;
				uint32_t *Lnewdata;
				uint32_t *Lstartdata = (uint32_t *)newdata;
				int pos=0,longs = offset >> 2;
				newdata += offset;
				Lnewdata = (uint32_t *)newdata;


				while(pos < longs)
				{
					int datalen;
					if(Lstartdata[pos] == Tag)
					{
						int currsize = (Lstartdata[pos+1] & 0xffffff);

						if(currsize == size) // replace the data
						{
							Lnewdata = &Lstartdata[pos];

							*Lnewdata++ = Tag;
							*Lnewdata++ = typesizebytes;
							newdata = (uint8_t *)Lnewdata;
							for(i=0;i<size;i++)
							{
								*newdata++ = *srcdata++;
							}
							for(;i<((size+3) & 0xfffffc);i++)
							{
								*newdata++ = 0;
							}
							found = 1;
						}
						else // remove the data
						{
							int longsize = ((currsize+8+3)&~3)>>2;
							int p;
							for(p=pos; p<pos+longsize && p<longs-longsize; p++)
							{
								Lstartdata[p] = Lstartdata[p+longsize];
							}

							m_workspaceSize -= longsize<<2;
							
							found = 0;
						}
						break;
					}
					else
					{
						datalen = ((Lstartdata[pos+1] & 0xffffff)+3)>>2;

						pos += 2;
						pos += datalen;
					}
				}
			}
		}

		if(!found)
		{
			int i,offset = m_workspaceSize;
			uint8_t *newdata = (uint8_t *)m_workspaceData;
			uint8_t *srcdata = (uint8_t *)pData;
			uint32_t *Lnewdata;
			uint32_t *Lstartdata = (uint32_t *)newdata;
			int pos=0,longs = offset >> 2;
			newdata += offset;
			Lnewdata = (uint32_t *)newdata;			


			while(pos < longs)
			{
				int datalen;

				if (Lstartdata[pos] == TAG_FREESPACE &&
					(((int)(Lstartdata[pos+1] & 0xffffff)) >= size))
				{
					int freebytes = (Lstartdata[pos+1] & 0xffffff);

					Lnewdata = &Lstartdata[pos];

					*Lnewdata++ = Tag;
					*Lnewdata++ = typesizebytes;

					newdata = (uint8_t *)Lnewdata;
					for(i=0;i<size;i++)
					{
						*newdata++ = *srcdata++;
					}
					for(;i<((size+3) & 0xfffffc);i++)
					{
						*newdata++ = 0;
					}
					found = 1;

					uintptr_t temp = (uintptr_t)newdata;
					Lnewdata = (uint32_t *)((temp + 3) & 0xfffffffc);
					freebytes -= (size+3) & 0xfffffc;
					freebytes -= 8; // TAG + typesize
					if(freebytes > 16)
					{
						*Lnewdata++ = TAG_FREESPACE;
						*Lnewdata++ = ('c'<<24)|freebytes;
					}
					else
					{
						allocsize -= freebytes;
					}
					break;
				}
				else
				{
					datalen = ((Lstartdata[pos+1] & 0xffffff)+3)>>2;

					pos += 2;
					pos += datalen;
				}
			}


			if(!found)
			{

				offset = m_workspaceSize;
				*Lnewdata++ = Tag;
				*Lnewdata++ = typesizebytes;
				newdata = (uint8_t *)Lnewdata;
				for(i=0;i<size;i++)
				{
					*newdata++ = *srcdata++;
				}
				for(;i<((size+3) & 0xfffffc);i++)
				{
					*newdata++ = 0;
				}

				m_workspaceSize += allocsize;
			}
			return true;
		}
		else
		{
			return false;
		}
	}
	return false;
}




/*-- not include in Doxygen
 *	@brief Add a new metadata tuple to the block of metadata
 *	@param maxsize added as an int parameter by CMD 20090605 and changed to size_t by BGS.
 */
void CSampleMetadata::MakeLeftRightDelta(uint32_t Tag, unsigned int typesizebytes, void *pData)
{
	METADATA_SIZE lsize;
	METADATA_TYPE lctype;
	float *fdata;

	fdata = (float *)MetadataFind(m_overrideData, m_overrideSize, Tag, &lsize, &lctype);

	if(fdata)
	{	
		int i;
		float *fldata = (float *)fdata;
		float *fddata = (float *)pData;
		const int item_count = (typesizebytes & 0xffffff)/sizeof(float);

		switch(Tag)
		{
		case TAG_WHITE_BALANCE:
		case TAG_EXPOSURE:
		case TAG_RGB_GAIN:
		case TAG_FRAME_ZOOM:
		case TAG_FRAME_DIFF_ZOOM:
			for (i = 0; i < item_count; i++)
			{
				if(*fldata)
					*fddata++ /= *fldata++;
			}
			break;
		default:
		//case TAG_HORIZONTAL_OFFSET:
		//case TAG_VERTICAL_OFFSET:
		//case TAG_ROTATION_OFFSET:
		//case TAG_GAMMA_TWEAKS:
		//case TAG_RGB_OFFSET:
		//case TAG_SATURATION:
		//case TAG_CONTRAST:
			for (i = 0; i < item_count; i++)
			{
				*fddata++ -= *fldata++;
			}
			break;
		}

	}
	else
	{
		int i;
		float *fldata = (float *)fdata;
		float *fddata = (float *)pData;
		const int item_count = (typesizebytes & 0xffffff)/sizeof(float);

		switch(Tag)
		{
		case TAG_WHITE_BALANCE:
		case TAG_EXPOSURE:
		case TAG_RGB_GAIN:
		case TAG_FRAME_ZOOM:
		case TAG_FRAME_DIFF_ZOOM:
			for (i = 0; i < item_count; i++)
			{
				if(fldata && *fldata)
					*fddata++ /= 1.0;
			}
			break;
		case TAG_GAMMA_TWEAKS:
		case TAG_SATURATION:
		case TAG_CONTRAST:
			for (i = 0; i < item_count; i++)
			{
				*fddata++ -= 1.0;
			}
			break;
		default:
		//case TAG_HORIZONTAL_OFFSET:
		//case TAG_VERTICAL_OFFSET:
		//case TAG_ROTATION_OFFSET:
		//case TAG_RGB_OFFSET:
			for (i = 0; i < item_count; i++)
			{
				*fddata++ -= 0.0;
			}
			break;
		}
	}
}




/*-- not include in Doxygen
 *	@brief Add a new metadata tuple to the block of metadata
 *	@param maxsize added as an int parameter by CMD 20090605 and changed to size_t by BGS.
 */
bool CSampleMetadata::AddMetaDataChannel(uint32_t Tag, unsigned int typesizebytes, void *pData, uint32_t channel)
{
	if(channel < 1 || channel > 2) // currently only support Left/Right 2 channels.
		return false;

	if((typesizebytes >> 24) != 'f')// only float type are diffed.
		return false;

	size_t datasize = typesizebytes & 0xffffff;
	if (datasize > 256*sizeof(float)) // on diff practical data sizes
		return false;

	// Copy data locally as MakeLeftRightDelta my change it before storage
	float local[256];
	memcpy(local, pData, datasize);
	pData = (void *)local;

	MakeLeftRightDelta(Tag, typesizebytes, pData);

	METADATA_SIZE lsize;
	METADATA_TYPE lctype;
	void *ddata;
	int size = (((typesizebytes & 0xffffff) + 3) & ~3);

	uint32_t COLtag = TAG_EYE_DELTA_1;
	COLtag += (channel-1)<<24;

	ddata = MetadataFind(m_overrideData, m_overrideSize, COLtag, &lsize, &lctype);

	if(ddata)
	{
		memcpy(m_workspaceData, ddata, lsize);
		m_workspaceSize = lsize;

		AddMetaDataWorkspace(Tag, typesizebytes, pData);
		AddMetaData(COLtag, m_workspaceSize+8, m_workspaceData);
	}
	else
	{
		uint32_t *newchanneldata = (uint32_t *)Alloc(size + 8);

		if(newchanneldata)
		{
			newchanneldata[0] = Tag;
			newchanneldata[1] = typesizebytes;
			memcpy(&newchanneldata[2], pData, size);

			AddMetaData(COLtag, size+8, newchanneldata);

			Free(newchanneldata);
		}
	}

	return true;
}
