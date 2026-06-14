/*! @file metadata.c

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

#include <stddef.h>
#include "metadata.h"

/*
// Example of how to retrieve an item of metadata
CODEC_ERROR GetMetadataItemPixelAspectRatio(METADATA *metadata,
											MetadataPixelAspectRatio *pixel_aspect_ratio_out,
											size_t size)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

	if (size != sizeof(MetadataPixelAspectRatio)) {
		return CODEC_ERROR_INVALID_ARGUMENT;
	}

	return GetMetadataItem(metadata,
						   METADATA_ITEM_PIXEL_ASPECT_RATIO,
						   pixel_aspect_ratio_out,
						   size);
}

// Sample code for retrieving all of the keys in a metadata set
CODEC_ERROR DumpMetadata(METADATA *metadata, FILE *logfile)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

	METADATA_KEY key;
	METADATA_HEADER header;

	// Start at the beginning of the metadata
	GetMetadataFirstKey(metadata, &key);

	// Loop through the metadata items
	while (key != METADATA_KEY_NULL)
	{
		// Get the header for the item corresponding to the key
		GetMetadataHeader(metadata, key, &header);

		// Print the metadata item key and size (in bytes)
		fprintf(logfile, "Metadata key: 5x%08X, size: %d\n", header.key, header.size);

		// Get the key for the next item in the set of metadata
		GetMetadataNextKey(metadata, &key);
	}

	return error;
}
*/
void *MetadataFind(void *data, 				// Points to the first tag
				   size_t datasize,			// Size of the metadata chunk (in bytes)
				   METADATA_TAG find_tag,	// Metadata tag to find
				   METADATA_SIZE *retsize,	// Return the size of the metadata (in bytes)
				   METADATA_TYPE *rettype	// Return the metadata type code
				   )
{
	uint32_t *idata = data;
	int pos = 0;
	unsigned int tag;
	unsigned int typesize;
	int size,offset;
	unsigned char type;

	if(idata==NULL || datasize==0)
		return NULL;


	do
	{
		tag = *idata++; pos += 4;
		typesize = *idata++; pos += 4;

		type = (typesize >> 24) & 0xff;
		size = typesize & 0xffffff;

		if(find_tag == tag)
		{
			*rettype = type;
			*retsize = size;
			return (void *)idata;
		}

		offset = (size + 3) & 0xfffffc;

		pos += offset;
		idata += (offset >> 2);

	}
	while((size_t)pos < datasize);

	return NULL;
}

void *MetadataFindAtUniqueFrame(void *data, 				// Points to the first tag
				   size_t datasize,			// Size of the metadata chunk (in bytes)
				   uint32_t UFRM, 
				   METADATA_TAG find_tag,	// Metadata tag to find
				   METADATA_SIZE *retsize,	// Return the size of the metadata (in bytes)
				   METADATA_TYPE *rettype	// Return the metadata type code
				   )
{
	uint32_t *idata = data;
	int pos = 0;
	unsigned int tag;
	unsigned int typesize;
	int size,offset;
	unsigned char type;
	void *ret = NULL;
	int foundCP = 0;
	uint32_t frstCP_UFRM = 0;
	uint32_t scndCP_UFRM = 0;

	if(idata==NULL || datasize==0)
		return NULL;


	do
	{
		tag = *idata++; pos += 4;
		typesize = *idata++; pos += 4;

		type = (typesize >> 24) & 0xff;
		size = typesize & 0xffffff;

		if(find_tag == tag)
		{
			*rettype = type;
			*retsize = size;
			ret = (void *)idata; // store the interpolated point here
		}
		if(ret && (tag & 0xffffff) == 0x4C5443)
		{
			if(((tag >> 24) & 0xff) >= 'a' && ((tag >> 24) & 0xff) <= 'z')
			{
				//control point
				//uint32_t cpTYPE  = idata[0];
				uint32_t cpUFRM  = idata[4];

				void *ldata;
				METADATA_SIZE lsize;
				METADATA_TYPE lctype;

				ldata = MetadataFind(&idata[5], size-24,
									find_tag, &lsize, &lctype);

				if(ldata)
				{
					int	i,flts = lsize/sizeof(float); 
					if(UFRM >= cpUFRM)
					{
						float *src,*dst;

						foundCP = 1;
						frstCP_UFRM = cpUFRM;

						dst = ret;
						src = ldata;
						for(i=0; i<flts; i++)
							*(float *)dst++ = *(float *)src++; 

						if(frstCP_UFRM == UFRM) // found
							return ret;
					}

					
					if(cpUFRM > UFRM)
					{
						float *cp1, *cp2;
						if(foundCP == 0) // first control is later in time
						{						
							float *src,*dst;	

							dst = ret;
							src = ldata;
							for(i=0; i<flts; i++)
								*(float *)dst++ = *(float *)src++; 

							return ret;
						}
						
						scndCP_UFRM = cpUFRM;

						cp1 = (float *)ret;
						cp2 = (float *)ldata; 
						flts = lsize/sizeof(float); 

						for(i=0; i<flts; i++)
						{
							*cp1 += (*cp2++ - *cp1)*(float)(UFRM-frstCP_UFRM)/(float)(scndCP_UFRM-frstCP_UFRM);
                            cp1++;
						}
					
						return ret;
					}
				}
			}
		}

		offset = (size + 3) & 0xfffffc;

		pos += offset;
		idata += (offset >> 2);

	}
	while((size_t)pos < datasize);

	return ret;
}



void *MetadataFindFreeform(void *data,	 			// Points to the first tag
						   size_t datasize, 		// Size of the metadata chunk (in bytes)
						   char *freeform,			// Freeform metadata to find
						   METADATA_SIZE *retsize,	// Return the size of the metadata (in bytes)
						   METADATA_TYPE *rettype	// Return the metadata type code
						   )
{
	uint32_t *idata = data;
	int pos = 0;
	unsigned int tag;
	unsigned int typesize;
	int size,offset;
	unsigned char type;
	int len = (int)strlen(freeform);

	if(len == 0)
		return NULL;

	do
	{
		tag = *idata++; pos += 4;
		typesize = *idata++; pos += 4;

		type = (typesize >> 24) & 0xff;
		size = typesize & 0xffffff;


		if(TAG_REGISTRY_NAME == tag || TAG_NAME == tag)
		{
			int testsize = size;
			if(testsize > 1 && ((char *)idata)[size-1] == 0)
				testsize = (int)strlen((char *)idata);

			if(testsize == len)
			{
				if(0 == strncmp((char *)idata, freeform, testsize))
				{
					// get next REGV or TAGV
					offset = (size + 3) & 0xfffffc;

					pos += offset;
					idata += (offset >> 2);
					tag = *idata++; pos += 4;
					
					if(TAG_REGISTRY_VALUE == tag || TAG_VALUE == tag) // Fixed a situation where TAGN doesn't have a TAGV pair.
					{
						typesize = *idata++; pos += 4;

						type = (typesize >> 24) & 0xff;
						size = typesize & 0xffffff;

						*rettype = type;
						*retsize = size;
						return (void *)idata;
					}
					else
					{
						return NULL;
					}
				}
			}
		}

		offset = (size + 3) & 0xfffffc;

		pos += offset;
		idata += (offset >> 2);

	} while((size_t)pos < datasize);

	return NULL;
}


void *MetaDataFindNext(void *sampledata,
					   size_t sampledatasize,
					   void **startmetadata,
					   void *lastdata,
					   METADATA_TAG *rettag,
					   METADATA_SIZE *retsize,
					   METADATA_TYPE *rettype,
					   METADATA_FLAGS flags)
{
	unsigned int *idata = (unsigned int *)lastdata;
	int size,typesize,type,offset,tag;
	intptr_t pos;
	int datasize;
	unsigned short *datasizeptr = (unsigned short *)*startmetadata;

	if(flags == 0) // not in file sample metadata
	{
		datasize = (int)sampledatasize;
		pos = (uintptr_t)lastdata - (uintptr_t)sampledata;
	}
	else
	{
		datasizeptr -= 5; //10 bytes	

		datasize = ((int)*datasizeptr);
		datasize = (((datasize & 0xff) << 8) | ((datasize & 0xff00) >> 8)) << 2;
		pos = (uintptr_t)lastdata - (uintptr_t)*startmetadata;
		pos += 8; //previous tag and size
	}

	idata--; //getsize;
	typesize = *idata++;
	size = typesize & 0xffffff;
	offset = (size + 3) & 0xfffffc;

	pos += offset;
	idata += (offset >> 2);
	if(pos < datasize)
	{
		tag = *idata++; pos += 4;

		typesize = *idata++; pos += 4;
		type = (typesize >> 24) & 0xff;
		size = typesize & 0xffffff;

		*rettype = type;
		*retsize = size;
		*rettag = tag;
		return (void *)idata;
	}
	else if(flags) //if in sample look for the next metadata chumk 
	{
		//void *data;
		size_t total_size = 0;
		//void *metadatastart;
		uint8_t *nexttuplet = (uint8_t *)*startmetadata + datasize - 8;
		int remainder = (int)sampledatasize - (int)((uintptr_t)nexttuplet - (uintptr_t)sampledata);
		if(remainder > 256)
		{
			*startmetadata = MetaDataFindFirst(nexttuplet, remainder,
					&total_size, rettag, retsize, rettype);

			return *startmetadata;
		}
		return NULL;
	}
	else
	{
		return NULL;
	}
}

void *MetaDataFindNextOld(void *startmetadata,
						  size_t datasize,
						  void *lastdata,
						  METADATA_TAG *rettag,
						  METADATA_SIZE *retsize,
						  METADATA_TYPE *rettype)
{
	unsigned int *idata = (unsigned int *)lastdata;
	int size,typesize,type,offset,pos,tag;

	ptrdiff_t diff = (intptr_t)lastdata - (intptr_t)startmetadata;
	pos = (int)diff;
	pos += 8; //previous tag and size

	idata--; //getsize;
	typesize = *idata++;
	size = typesize & 0xffffff;
	offset = (size + 3) & 0xfffffc;

	pos += offset;
	idata += (offset >> 2);
	if ((size_t)pos < datasize)
	{
		tag = *idata++; pos += 4;

//		if(tag == TAG_FREESPACE)
//		{
//			return NULL; // no more metadata
//		}

		typesize = *idata++; pos += 4;
		type = (typesize >> 24) & 0xff;
		size = typesize & 0xffffff;

		*rettype = type;
		*retsize = size;
		*rettag = tag;
		return (void *)idata;
	}
	else
		return NULL;
}

void *MetaDataFindFirst(void *data,
						size_t datasize,
						size_t *retchunksize,
						METADATA_TAG *rettag,
						METADATA_SIZE *retsize,
						METADATA_TYPE *rettype)
{
	BITSTREAM myinput, *pinput = &myinput;
	TAGVALUE segment;
	TAGWORD tag,value;
	int error = 0;
	//char t[100];

#if 1
	InitBitstreamBuffer(pinput, data, datasize, BITSTREAM_ACCESS_READ);
#else
	myinput.lpCurrentWord = data;
	myinput.nWordsUsed = datasize;
	myinput.nBitsFree = BITSTREAM_LONG_SIZE;
#endif

	do
	{
		bool optional = false;
		int chunksize = 0;

		// Read the next tag value pair from the bitstream
		segment = GetSegment(pinput);

		tag = segment.tuple.tag;
		value = segment.tuple.value;


		// Is this an optional tag?
		if (tag < 0)
		{
			tag = NEG(tag);
			optional = true;
		}

		if(tag & 0x2000)
		{
			chunksize = value;
			chunksize &= 0xffff;
			chunksize += ((tag&0xff)<<16);
		}
		else if(tag & 0x4000)
		{
			chunksize = value;
			chunksize &= 0xffff;
		}
		else if(tag == CODEC_TAG_INDEX)
		{
			chunksize = value;
			chunksize &= 0xffff;
		}
		else
		{
			chunksize = 0;
		}

		if((int)(tag) <= ((int)CODEC_TAG_LAST_NON_SIZED) || tag & 0x6000)
		{
			int skip = 1;
			error = 0;

			if(tag == (int)CODEC_TAG_METADATA || tag == (int)CODEC_TAG_METADATA_LARGE)
			{
				unsigned int *idata =  (unsigned int *)pinput->lpCurrentWord;
				unsigned int tag;
				unsigned int typesize;
				//int size;
				//int offset;
				//unsigned char type;

				tag = *idata++;
				typesize = *idata++;

				*rettag = tag;
				*rettype = (typesize >> 24) & 0xff;
				*retsize = typesize & 0xffffff;
				*retchunksize = chunksize*4;
				return (void *)idata;
			}

			if((tag & 0xff00) == 0x2200) //sample size
				skip = 0;
			if((tag & 0xff00) == 0x2300) //uncompressed sample size
				skip = 1;
			if((tag & 0xff00) == 0x2100) //level
				skip = 0;


			if(chunksize)
			{
				if(chunksize*4 > pinput->nWordsUsed || chunksize < 0)
				{
					break;
				}

				if(skip)
				{
					//unsigned int *iptr =  (unsigned int *)pinput->lpCurrentWord;

					pinput->lpCurrentWord += chunksize*4;
					pinput->nWordsUsed -= chunksize*4;
				}

			}
		}
		else
		{
			error = 1;
		}
	} while(tag != CODEC_TAG_GROUP_TRAILER &&
			tag != CODEC_TAG_FRAME_TRAILER &&
			pinput->nWordsUsed>0 && !error);

	return NULL;
}

int ValidMetadataLength(void *data, size_t len)
{
	uint32_t *idata = (uint32_t *)data;
	int size,typesize,pos;
	METADATA_TAG tag;

	if(len < 12)
		return 0;

	tag = *idata;
	if(tag == 0)
		return 0;

	pos = 0;
	do
	{
		tag = *idata++;
		typesize = *idata++;
		//type = (typesize >> 24) & 0xff;
		size = 8/* tag + typesize*/ + (((typesize & 0xffffff) + 3) & ~0x3);

		if ((size_t)(pos + size) <= len)
		{
			pos += size; idata+=((size-8)>>2);
			tag = *idata;
		}
		else
		{
			break;
		}
	} while(tag && (size_t)pos < len);

	return pos;
}

void *MetaDataFindInSample(void *data, size_t datasize,
						   METADATA_TAG findmetadatatag,
						   METADATA_SIZE *retsize,
						   METADATA_TYPE *rettype)
{
	BITSTREAM myinput, *pinput = &myinput;
	TAGVALUE segment;
	TAGWORD tag,value;
	int error = 0;
	//char t[100];

#if 1
	InitBitstreamBuffer(pinput, data, datasize, BITSTREAM_ACCESS_READ);
#else
	myinput.lpCurrentWord = data;
	myinput.nWordsUsed = datasize;
	myinput.nBitsFree = BITSTREAM_LONG_SIZE;
#endif

	do
	{
		bool optional = false;
		int chunksize = 0;

		// Read the next tag value pair from the bitstream
		segment = GetSegment(pinput);

		tag = segment.tuple.tag;
		value = segment.tuple.value;


		// Is this an optional tag?
		if (tag < 0)
		{
			tag = NEG(tag);
			optional = true;
		}

		if(tag & 0x2000)
		{
			chunksize = value;
			chunksize &= 0xffff;
			chunksize += ((tag&0xff)<<16);
		}
		else if(tag & 0x4000)
		{
			chunksize = value;
			chunksize &= 0xffff;
		}
		else if(tag == CODEC_TAG_INDEX)
		{
			chunksize = value;
			chunksize &= 0xffff;
		}
		else
		{
			chunksize = 0;
		}

		if((int)(tag) <= ((int)CODEC_TAG_LAST_NON_SIZED) || tag & 0x6000)
		{
			int skip = 1;
			error = 0;

			if(tag == (int)CODEC_TAG_METADATA || tag == (int)CODEC_TAG_METADATA_LARGE)
			{
				unsigned int *idata =  (unsigned int *)pinput->lpCurrentWord;
				void *retptr = MetadataFind(idata, chunksize*4, findmetadatatag, retsize, rettype);

				if(retptr)
					return retptr;
			}

			if((tag & 0xff00) == 0x2200) //sample size
				skip = 0;
			if((tag & 0xff00) == 0x2300) //uncompressed sample size
				skip = 1;
			if((tag & 0xff00) == 0x2100) //level
				skip = 0;


			if(chunksize)
			{
				if(chunksize*4 > pinput->nWordsUsed || chunksize < 0)
				{
					break;
				}

				if(skip)
				{
					//unsigned int *iptr =  (unsigned int *)pinput->lpCurrentWord;

					pinput->lpCurrentWord += chunksize*4;
					pinput->nWordsUsed -= chunksize*4;
				}

			}
		}
		else
		{
			error = 1;
		}
	} while(tag != CODEC_TAG_GROUP_TRAILER &&
			tag != CODEC_TAG_FRAME_TRAILER &&
			pinput->nWordsUsed>0 && !error);

	return NULL;
}

void *MetaDataFindTag(void *data, size_t datasize,
					  METADATA_TAG findmetadatatag,
					  METADATA_SIZE *retsize,
					  METADATA_TYPE *rettype)
{
	int pos = 0;
	unsigned int tag;
	unsigned int typesize;
	int size,offset;
	unsigned char type;

	unsigned int *idata = (unsigned int *)data;

	if(idata && datasize)
	{
		do
		{
			tag = *idata++; pos += 4;
			typesize = *idata++; pos += 4;
	
			type = (typesize >> 24) & 0xff;
			size = typesize & 0xffffff;
	
			if(findmetadatatag == tag)
			{
				*rettype = type;
				*retsize = size;
				return (void *)idata;
			}
	
			offset = (size + 3) & 0xfffffc;
	
			pos += offset;
			idata += (offset >> 2);
	
		} while(pos < (int)datasize);
	}

	return NULL;
}

bool FindMetadata(METADATA *metadata,
				  METADATA_TAG tag,
				  void **data_out,
				  METADATA_SIZE *size_out,
				  METADATA_TYPE *type_out)
{
	METADATA_SIZE size;
	METADATA_TYPE type;

	// Search for the metadata item with the specified tag
	void *data = MetadataFind(metadata->block, metadata->size, tag, &size, &type);

	// Found the metadata item?
	if (data != NULL)
	{
		if (data_out != NULL) {
			*data_out = data;
		}

		if (size_out != NULL) {
			*size_out = size;
		}

		if (type_out != NULL) {
			*type_out = type;
		}

		return true;
	}

	return false;
}
