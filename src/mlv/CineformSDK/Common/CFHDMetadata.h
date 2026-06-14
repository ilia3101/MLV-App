/*! @file CFHDMetadata.h
*
*  @brief Metadata parsing functions.
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
#ifndef CFHD_METADATA_H
#define CFHD_METADATA_H

#include "CFHDError.h"
#include "CFHDTypes.h"
#include "CFHDMetadataTags.h"

#ifdef _WIN32
	#ifndef DYNAMICLIB
		#define CFHDMETADATA_API
	#else
		#ifdef METADATADLL_EXPORTS
			// Export the entry points for the metadata interface
			#define CFHDMETADATA_API __declspec(dllexport)
		#else
			// Declare the entry points to the metadata interface
			#define CFHDMETADATA_API __declspec(dllimport)
		#endif
	#endif
#else
	#ifdef METADATADLL_EXPORTS
		#define CFHDMETADATA_API __attribute__((visibility("default")))
	#else
		#define CFHDMETADATA_API
	#endif
#endif


// Opaque datatype for the CineForm HD metadata
typedef void *CFHD_MetadataRef;

// Interface to the codec library for use with either C or C++
#ifdef __cplusplus
extern "C" {
#endif


#if DYNAMICALLY_LINK

// Open an interface to the CineForm HD metadata
CFHD_Error
CFHD_OpenMetadataStub(CFHD_MetadataRef *metadataRefOut);

// Initial the metadata interface for a particular sample and metadata track
CFHD_Error
CFHD_InitSampleMetadataStub(CFHD_MetadataRef metadataRef,
						CFHD_MetadataTrack track,
						void *sampleData,
						size_t sampleSize);

// Read the block of metadata
CFHD_Error
CFHD_ReadMetadataFromSampleStub(CFHD_MetadataRef metadataRef,
				  void **data,
				  CFHD_MetadataSize *size);


// Read the next tag, size & value from the metadata
CFHD_Error
CFHD_ReadMetadataStub(CFHD_MetadataRef metadataRef,
				  unsigned int *tag,
				  CFHD_MetadataType *type,
				  void **data,
				  CFHD_MetadataSize *size);

// Find a particular tag, size & value from the metadata
CFHD_Error
CFHD_FindMetadataStub(CFHD_MetadataRef metadataRef,
					unsigned int tag,
					CFHD_MetadataType *type,
					void **data,
					CFHD_MetadataSize *size);

// Close an interface to the CineForm HD metadata
CFHD_Error
CFHD_CloseMetadataStub(CFHD_MetadataRef metadataRef);

#define CFHD_OpenMetadata			CFHD_OpenMetadataStub
#define CFHD_InitSampleMetadata		CFHD_InitSampleMetadataStub
#define CFHD_ReadMetadataFromSample	CFHD_ReadMetadataFromSampleStub
#define CFHD_ReadMetadata			CFHD_ReadMetadataStub
#define CFHD_FindMetadata			CFHD_FindMetadataStub
#define CFHD_CloseMetadata			CFHD_CloseMetadataStub

#else // DYNAMICALLY_LINK

// Open an interface to the CineForm HD metadata
CFHDMETADATA_API CFHD_Error
CFHD_OpenMetadata(CFHD_MetadataRef *metadataRefOut);

// Initial the metadata interface for a particular sample and metadata track
CFHDMETADATA_API CFHD_Error
CFHD_InitSampleMetadata(CFHD_MetadataRef metadataRef,
						CFHD_MetadataTrack track,
						void *sampleData,
						size_t sampleSize);

// Read the block of metadata
CFHDMETADATA_API CFHD_Error
CFHD_ReadMetadataFromSample(CFHD_MetadataRef metadataRef,
				  void **dataOut,
				  size_t *sizeOut);

// Read the next tag, size & value from the metadata
CFHDMETADATA_API CFHD_Error
CFHD_ReadMetadata(CFHD_MetadataRef metadataRef,
				  CFHD_MetadataTag *tag,
				  CFHD_MetadataType *type,
				  void **data,
				  CFHD_MetadataSize *size);

// Find a particular metadata tag and return the size and value
CFHDMETADATA_API CFHD_Error
CFHD_FindMetadata(CFHD_MetadataRef metadataRef,
				  CFHD_MetadataTag tag,
				  CFHD_MetadataType *type,
				  void **data,
				  CFHD_MetadataSize *size);

// Close an interface to the CineForm HD metadata
CFHDMETADATA_API CFHD_Error
CFHD_CloseMetadata(CFHD_MetadataRef metadataRef);

#endif // DYNAMICALLY_LINK

#ifdef __cplusplus
}
#endif

#endif // CFHD_METADATA_H
