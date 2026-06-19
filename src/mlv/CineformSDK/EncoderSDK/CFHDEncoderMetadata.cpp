/*! @file CFHDEncoder.cpp

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
#include "Interface.h"

// Include files from the codec library
#include "encoder.h"
//#include "thread.h"
#include "metadata.h"
#include "AVIExtendedHeader.h"

//TODO: Eliminate references to the codec library

// Include files from the encoder DLL
#include "Allocator.h"
#include "CFHDEncoder.h"

#include "Lock.h"

#include "SampleMetadata.h"
#include "VideoBuffers.h"
#include "SampleEncoder.h"
#include "MetadataWriter.h"
//#include "Watermark.h"

//#define SYSLOG	0
//#define QLONOPEN 0


/*!
	@function CFHD_MetadataOpen

	@brief Opens a handle for attaching metadata.

	@description Opens a handle for attaching metadata is one of two class: global and local.
	Global is for metadata that should appear in every frame, and is likely not changing.
	Local is for metadata that only attached sometimes or is change often.  Something changing
	every frame can use either class. If an item frames every ten frame, global will store the
	last value for the non changing frame, whereas local on store data on the frames impacted.

	@param metadataRefOut
	Pointer to the variable that will receive the metadata reference.

	@return Returns a CFHD error code.
*/
CFHDENCODER_API CFHD_Error
CFHD_MetadataOpen(CFHD_MetadataRef *metadataRefOut)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (metadataRefOut == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	// Allocate a new encoder metadata structure
	CSampleEncodeMetadata *metadataRef = new CSampleEncodeMetadata;
	if (metadataRef == NULL) {
		return CFHD_ERROR_OUTOFMEMORY;
	}

	// Return the encoder data structure
	*metadataRefOut = (CFHD_MetadataRef)metadataRef;
	
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_MetadataOpen mref:%04x thread:%d", (0xffff)&(int)metadataRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	return errorCode;
}

/*!
	@function CFHD_MetadataClose

	@brief Release any resources allocated to the CFHD_MetadataOpen.

	@param metadataRef
	Reference to an metadata engine created by a call to @ref CFHD_MetadataOpen
	and initialized by a calls to @ref CFHD_MetadataAdd.

	@discussion Do not attempt to use an metadata reference after being
	closed by a call to this function.

	@return Returns a CFHD error code.
*/
CFHDENCODER_API CFHD_Error
CFHD_MetadataClose(CFHD_MetadataRef metadataRef)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_MetadataClose mref:%04x thread:%d", (0xffff)&(int)metadataRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	//CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (metadataRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleEncodeMetadata *metadata = (CSampleEncodeMetadata *)metadataRef;

	delete metadata;

	return CFHD_ERROR_OKAY;
}

/*!
	@function CFHD_MetadataAdd

	@brief Adds metadata for later attachment to the encoded bitstream.

	@description The CineForm metadata can be in two classes, global and local.
	Global is the most common, adding the same fields to every frame, whether
	the fields are changing of not.  Local only places the metadata in the current
	frame that is about to be encoded.  If you want only local metadata, set the
	local flag.  Examples, director, DP and timecode is global, closed captioning
	is local. CFHD_MetadataAdd requires a call to @ref CFHD_MetadataAdd to bind the
	metadata to the encoded frame -- separating these function helps with threading.

	@param metadataRef
	Reference to an metadata engine created by a call to @ref CFHD_MetadataOpen..

	@param tag
	FOURCC code for the tag to add.

	@param type
	CFHD_MetadataType of the data with this tag.

	@param size
	number of byte of data within the tag.

	@param data
	data for the tag

	@param local
	If the local flag is set, the metadata is will be local and only placed in the
	next frame to be encoded.  Otherwise, the metadata will be used for all frames.

	@discussion While CFHD_MetadataAdd is thread safe, it should not be threaded with
	multiple encoders like CFHD_MetadataAttach can with one metadataRef pointwe.
	If you wish to control metadata on a per-frame basis, you should have a separate
	metadataRefs for each thread. Non-frame accurate global data could have it own
	metadataRef, calling CFHD_MetadataAttach one with each thread, then use the threaded
	metadataRefs for frame accurate local metadata.

	@return Returns a CFHD error code.

	@todo Change the metadata size to size_t and the data pointer to void * to eliminate
	unnecessary compiler warnings.
*/
CFHDENCODER_API CFHD_Error
CFHD_MetadataAdd(CFHD_MetadataRef metadataRef,
				 uint32_t tag,
				 CFHD_MetadataType type,
				 size_t size,
				 uint32_t *data,
				 bool local)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_MetadataAdd mref:%04x thread:%d", (0xffff)&(int)metadataRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	CFHD_Error error = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (metadataRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}
	if (tag == 0 || size == 0 || data == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleEncodeMetadata *metadata = (CSampleEncodeMetadata *)metadataRef;
	assert(metadata != NULL);

	// Compute the character code for the metadata type
	unsigned char ctype = 0;
	switch (type)
	{
		case METADATATYPE_STRING:
			ctype = 'c';
			break;
		case METADATATYPE_UINT32:
			ctype = 'L';
			break;
		case METADATATYPE_UINT16:
			ctype = 'S';
			break;
		case METADATATYPE_UINT8:
			ctype = 'B';
			break;
		case METADATATYPE_FLOAT:
			ctype = 'f';
			break;
		case METADATATYPE_DOUBLE:
			ctype = 'd';
			break;
		case METADATATYPE_GUID:
			ctype = 'G';
			break;
		case METADATATYPE_XML:
			ctype = 'x';
			break;
		case METADATATYPE_LONG_HEX:
			ctype = 'H';
			break;
		case METADATATYPE_HIDDEN:
			ctype = 'h';
			break;
		case METADATATYPE_UNKNOWN:
		default:
			break;
	}
	assert(ctype);

	//Lock(&metadata->m_lock);
	CAutoLock lock(metadata->m_lock);

	metadata->m_metadataChanged = true;
	
	// Allocator not used on Mac.  local.allocator does not exist in the METADATA structure
#if _ALLOCATOR
	if(local)
	{
		if (metadata->local.block == NULL)
			metadata->GetAllocator(&metadata->local.allocator);
	}
	else
	{
		int i;
		if (metadata->global[0].block == NULL)
		{
			for(i=0; i<5; i++)
				metadata->GetAllocator(&metadata->global[i].allocator);
		}
	}
#endif
	
	// Need to initialize the metadata attached to all encoded frames?
	//if (metadata->m_metadataGlobal == NULL &&
	if (metadata->global[0].block == NULL &&
		(tag != TAG_CLIP_GUID) &&
		(!local))
	{
		// Add basic metadata tags and values to the encoder metadata
		error = metadata->AddGUID();
		if (error != CFHD_ERROR_OKAY) {
			return error;
		}
	}

	// Adding a look file?
	//if (metadata->m_metadataGlobal && tag == TAG_LOOK_FILE)
	if (metadata->global[0].block && tag == TAG_LOOK_FILE)
	{
		error = metadata->AddLookFile((METADATA_TYPE)ctype, (METADATA_SIZE)size, data);
		if (error != CFHD_ERROR_OKAY) {
			return error;
		}
		return CFHD_ERROR_OKAY; //DAN20110131 --  AddLookFile correct adds the LOOK and LCRC
	}

	//Eye selection
	if(tag == TAG_SET_EYE)
	{
		metadata->m_selectedEye = *data;
		return CFHD_ERROR_OKAY;
	}

	if (local)
	{	
		bool result;
		assert(size <= UINT32_MAX);
		result = AddMetadata(&metadata->local, tag, ctype, (uint32_t)size, data);
		if (result == false) {
			return CFHD_ERROR_UNEXPECTED;
		}
	}
	else
	{
		bool result;
		// Add metadata that will be applied to all frames
		assert(size <= UINT32_MAX);
		result = AddMetadata(&metadata->global[metadata->m_selectedEye],  tag, ctype, (uint32_t)size, data);
		if (result == false) {
			return error;
		}
	}


	//Unlock(&metadata->m_lock);

	return CFHD_ERROR_OKAY;
}

/*!
	@function CFHD_MetadataAttach

	@brief Attaches metadata to the encoded bitstream.

	@description Attaches all data allocated with @ref CFHD_MetadataAdd to the
	next encoded frame.  CFHD_MetadataAttach can be used concurrently by threaded
	instances of the encoder.  Note that @ref CFHD_MetadataAdd is not thread safe.

	@param encoderRef
	Reference to an encoder engine created by a call to @ref CFHD_MetadataOpen that
	the current metadata should be attached.

	@param metadataRef
	Reference to an metadata engine created by a call to @ref CFHD_MetadataOpen..

	@return Returns a CFHD error code.
*/
CFHDENCODER_API CFHD_Error
CFHD_MetadataAttach(CFHD_EncoderRef encoderRef, CFHD_MetadataRef metadataRef)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_MetadataAttach ref:%04x mref:%04x thread:%d", (0xffff)&(int)encoderRef, (0xffff)&(int)metadataRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	// Check the input arguments
	if (metadataRef == NULL || encoderRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleEncoder *encoder = (CSampleEncoder *)encoderRef;
	CSampleEncodeMetadata *metadata = (CSampleEncodeMetadata *)metadataRef;

	CFHD_ALLOCATOR *encAllocator = NULL;
	CFHD_ALLOCATOR *metAllocator = NULL;
	encoder->GetAllocator(&encAllocator);
	metadata->GetAllocator(&metAllocator);
	if(encAllocator && metAllocator == NULL)
	{
		metadata->SetAllocator(encAllocator);
	}

	// Need exclusive access to the metadata for the rest of this routine
	CAutoLock lock(metadata->m_lock);

	if (metadata->m_metadataChanged)
	{
		encoder->EyeDeltaMetadata(&metadata->global[0], &metadata->global[1], &metadata->global[2]);
		encoder->MergeMetadata(&metadata->global[0], &metadata->local);

		if (metadata->local.block)
		{				
			FreeMetadata(&metadata->local);
		}

		metadata->m_metadataChanged = false;
	}

	return CFHD_ERROR_OKAY;
}
