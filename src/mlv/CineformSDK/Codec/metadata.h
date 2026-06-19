/*! @file metadata.h

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

#ifndef _METADATA_H
#define _METADATA_H

#include "decoder.h"
#include "bitstream.h"
#include "codec.h"

/*!
	@brief Structure that references a collection of metadata in memory

	Most of the legacy code does not use the limit member variable.  The
	size member variable is both the actual size of the metadata in the
	block and the allocated size of the block.  New code should allocate
	metadata blocks with enough space to allow for additional metadata to
	be added without having to call the memory allocator and copy the old
	block for every addition of metadata.

	If the size member variable is used to represent the actual amount of
	metadata in the block, then the size of the actual metadata can used
	to determine the end of the actual metadata and hence the location for
	adding new metadata.

	Suggest using power of two memory allocation to reduce fragmentation.
*/
typedef struct metadata
{
	void *block;		//!< Pointer to the block of metadata
	size_t size;		//!< Actual size of the metadata (in bytes)
	size_t limit;		//!< Allocated size of the block (in bytes)

#if _ALLOCATOR
	//! Allocated used for metadata blocks
	ALLOCATOR *allocator;
#endif

} METADATA;

#if _ALLOCATOR
#define METADATA_INITIALIZER	{NULL, 0, 0, NULL}
#else
#define METADATA_INITIALIZER	{NULL, 0, 0}
#endif

#define METADATA_EYE_BOTH		0
#define METADATA_EYE_LEFT		1
#define METADATA_EYE_RGHT		2
#define METADATA_EYE_DIFFLEFT	3
#define METADATA_EYE_DIFFRGHT	4

#ifdef __cplusplus
extern "C" {
#endif

// Returns the metadata chuck size and the first tag and it data.
void *MetadataFind(void *data,	 					// Points to the first tag
				   size_t size,						// Size of the metadata chunk (in bytes)
				   METADATA_TAG tag,				// Metadata tag to find
				   METADATA_SIZE *retsize,			// Return the size of the metadata (in bytes)
				   METADATA_TYPE *rettype);			// Return the metadata type code

void *MetadataFindAtUniqueFrame(void *data,	 					// Points to the first tag
				   size_t size,						// Size of the metadata chunk (in bytes)
				   uint32_t UFRM, 
				   METADATA_TAG tag,				// Metadata tag to find
				   METADATA_SIZE *retsize,			// Return the size of the metadata (in bytes)
				   METADATA_TYPE *rettype);			// Return the metadata type code

void *MetadataFindFreeform(void *data,	 			// Points to the first tag
						   size_t size,				// Size of the metadata chunk (in bytes)
						   char *freeform,			// Freeform metadata to find
						   METADATA_SIZE *retsize,	// Return the size of the metadata (in bytes)
						   METADATA_TYPE *rettype);	// Return the metadata type code

// pass the pointer to the sample begining, and the size of the sample
// Returns the metadata chuck size and the first tag and it data.
void *MetaDataFindFirst(void *data,
						size_t datasize,
						size_t *retchunksize,
						METADATA_TAG *rettag,
						METADATA_SIZE *retsize,
						METADATA_TYPE *rettype);

// pass the point to the sample begining, and the size of the sample
// Returns the metadata chuck size and the first tag and it data.
void *MetaDataFindNext(void *sampledata,
					   size_t sampledatasize,
					   void **startmetadata,
					   void *lastdata,
					   METADATA_TAG *rettag,
					   METADATA_SIZE *retsize,
					   METADATA_TYPE *rettype,
					   METADATA_FLAGS flags);

void *MetaDataFindNextOld(void *startmetadata,
						  size_t datasize,
						  void *lastdata,
						  METADATA_TAG *rettag,
						  METADATA_SIZE *retsize,
						  METADATA_TYPE *rettype);

int ValidMetadataLength(void *data, size_t len);

/*!
	Pass the pointer to the sample beginning and the size of the sample.
	The routine returns the metadata for the requested tag or NULL.
*/
void *MetaDataFindInSample(void *sample_data,
						   size_t sample_size,
						   METADATA_TAG findmetadatatag,
						   METADATA_SIZE *retsize,
						   METADATA_TYPE *rettype);

/*!
	Pass the pointer to the first tag of a metadata chunk and the size of the chunk.
	This routine returns the metadata for the requested tag and the size and type of
	the metadata item.
*/
void *MetaDataFindTag(void *chunk_data,
					  size_t chunk_size,
					  METADATA_TAG findmetadatatag,
					  METADATA_SIZE *retsize,
					  METADATA_TYPE *rettype);

// This routine is only defined for C++
void *MetaDataFindNextExtended(void *sampledata,
							   size_t sampledatasize,
							   void **startmetadata,
							   void *lastdata,
							   METADATA_TAG *rettag,
							   METADATA_SIZE *retsize,
							   METADATA_TYPE *rettype);


// New metadata API that uses a struct to represent the block of metadata

#if _ALLOCATOR
void AllocMetadata(ALLOCATOR *allocator, METADATA *metadata, size_t size);
#else
void AllocMetadata(METADATA *metadata, size_t size);
#endif

bool AddMetadata(METADATA *metadata,
				 uint32_t tag,
				 unsigned char type,
				 uint32_t size,
				 uint32_t *data);

bool FindMetadata(METADATA *metadata,
				  METADATA_TAG tag,
				  void **data_out,
				  METADATA_SIZE *size_out,
				  METADATA_TYPE *type_out);

// Find the item of metadata for the specified tag and return the metadata tuple
bool FindMetadataTuple(METADATA *metadata,
					   METADATA_TAG tag,
					   METADATA_TUPLE *tuple);

void GetMetadataValue(METADATA *metadata, METADATA_SIZE size, METADATA_TYPE type, void *output);
void FreeMetadata(METADATA *metadata);

void UpdateCFHDDATA(struct decoder *decoder, unsigned char *ptr, int len, int delta, int priority);

#ifdef __cplusplus
}
#endif

#endif
