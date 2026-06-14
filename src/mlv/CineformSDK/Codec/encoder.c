/*! @file encoder.c

*  @brief Main Encoder entry point
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


#include <stdint.h>
#include "config.h"
#include "timing.h"

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include "../Common/macdefs.h"
#endif

#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <memory.h>

#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif

#include "dump.h"
#include "encoder.h"
#include "quantize.h"
#include "codec.h"
#include "wavelet.h"
#include "vlc.h"
#include "debug.h"
#include "codebooks.h"
#include "frame.h"
#include "stats.h"
#include "color.h"
#include "frame.h"
#include "bitstream.h"
#include "filter.h"
#include "convert.h"
#include "image.h"
#include "spatial.h"
#include "metadata.h"
#include "thumbnail.h"
#include "lutpath.h"
#include "bandfile.h"

#if _RECURSIVE
#include "recursive.h"
#endif

#if (0 && _THREADED_ENCODER)
#include "threaded.h"
#endif

#if __APPLE__
#include "../Common/macdefs.h"
#endif

#if !defined(_WIN32)
#define min(x,y)	(((x) < (y)) ? (x) : (y))
#define max(x,y)	(((x) > (y)) ? (x) : (y))
#endif

// Control debugging and timing in this module
#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif
#ifndef TIMING
#define TIMING (1 && _TIMING)
#endif
#ifndef XMMOPT
#define XMMOPT (1 && _XMMOPT)
#endif
#ifndef DUMP
#define DUMP   (0 && _DUMP)
#endif

#ifdef _WIN32
#define SYSLOG	0
#else
#define SYSLOG	(0 && DEBUG)
#endif

#define PREFETCH (1 && _PREFETCH)

#ifndef ALIGNED_N_PTR
#define ALIGNED_N_PTR(p, n)	((((uintptr_t)(p)) + (n)) & (UINTPTR_MAX & ~(n)))
#endif

// This macro transforms the run value before run length counting
#define VALUE(value) (value)

// Must declare the byte swap function even though it is an intrinsic
//int _bswap(int);
#include "swap.h"

#ifndef _FRAME_TRANSFORM
#if _NEW_DECODER
#define _FRAME_TRANSFORM	1		// Use the frame transform for the level one wavelets
#else
#define _FRAME_TRANSFORM	1		// Use the frame transform for the level one wavelets
#endif
#endif

#define FAST_BYR3		1
#define FAST_RG30		1

// Performance measurements
#if _TIMING

extern TIMER tk_encoding;
extern TIMER tk_compress;
extern TIMER tk_convert;
//extern TIMER tk_lowpass;
extern TIMER tk_finish;

extern COUNTER progressive_encode_count;

#endif


#ifdef _WIN32
// Forward reference

#elif defined(__APPLE__)
#include <Carbon/Carbon.h>



#ifndef MAX_PATH
#define MAX_PATH	260
#endif
#endif

// Use the standard definition for the maximum number of characters in a pathname
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

#if _STATS
static int stats_lastbits = 0;
#endif


// Local functions
void EncodeQuantizedGroup(ENCODER *encoder, TRANSFORM *transform[], int num_transforms, BITSTREAM *output);
void EncodeQuantizedFrameTransform(ENCODER *encoder, TRANSFORM *transform, BITSTREAM *output, int channel);
void EncodeQuantizedFieldPlusTransform(ENCODER *encoder, TRANSFORM *transform, BITSTREAM *output, int channel);
void EncodeQuantizedFieldTransform(ENCODER *encoder, TRANSFORM *transform, BITSTREAM *output, int channel);


#if _THREADED_ENCODER
// Forward references for thread routines
DWORD WINAPI EncodeChannelThreadLoop(LPVOID param);
DWORD WINAPI EncodeFrameThreadLoop(LPVOID param);
DWORD GetEncoderAffinityMask(ENCODER *encoder, int channel);
void SetEncoderAffinityMask(ENCODER *encoder);
#endif


#define ROUNDUP(a, b)	((((a) + ((b) - 1)) / (b)) * b)


/* Table of CRCs of all 8-bit messages. */
uint32_t crc_tableA[256];

/* Flag: has the table been computed? Initially false. */
int crc_table_computedA = 0;

/* Make the table for a fast CRC. */
void make_crc_tableA(void)
{
	uint32_t c;
	int n, k;

	for (n = 0; n < 256; n++) {
		c = (uint32_t)n;
		for (k = 0; k < 8; k++) {
			if (c & 1)
				c = 0xedb88320L ^ (c >> 1);
			else
				c = c >> 1;
		}
		crc_tableA[n] = c;
	}
	crc_table_computedA = 1;
}

/* Update a running CRC with the bytes buf[0..len-1]--the CRC
should be initialized to all 1's, and the transmitted value
is the 1's complement of the final running CRC (see the
crc() routine below)). */

uint32_t update_crcA(uint32_t crc, unsigned char *buf,
	int len)
{
	uint32_t c = crc;
	int n;

	if (!crc_table_computedA)
		make_crc_tableA();
	for (n = 0; n < len; n++) {
		c = crc_tableA[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}

/* Return the CRC of the bytes buf[0..len-1]. */
uint32_t calccrcA(unsigned char *buf, int len)
{
	return update_crcA(0xffffffffL, buf, len) ^ 0xffffffffL;
}

/* Return the CRC of the bytes buf[0..len-1]. */
uint32_t gencrc(unsigned char *buf, int len)
{
	return update_crcA(0xffffffffL, buf, len) ^ 0xffffffffL;
}



void InitEncoder(ENCODER *encoder, FILE *logfile, CODESET *cs)
{
	int i; 

	// Clear everything then set the logfile
	memset(encoder, 0, sizeof(ENCODER));
	encoder->logfile = logfile;

	// Set the codebooks that will be used for encoding
	if (cs != NULL)
	{
		// Use the codeset provided in the call
		for(i=0; i<CODEC_NUM_CODESETS; i++)
		{
			encoder->valuebook[i] = cs[i].valuebook;
			//encoder->magsbook[i] = cs[i]->magsbook;
			encoder->codebook_runbook[i] = cs[i].codebook_runbook;
			encoder->codebook_magbook[i] = cs[i].codebook_magbook;
			{
				int pos = cs[i].tagsbook[0]-1; // The last code in the tagsbook in the band_end_code
				encoder->band_end_code[i] = (unsigned int)cs[i].tagsbook[pos*2+2];
				encoder->band_end_size[i] = (int)cs[i].tagsbook[pos*2+1];
			}
		}
	}
	else
	{
		// Use the default codeset
		//encoder->magsbook[0] = cs9.magsbook;
		encoder->codebook_runbook[0] = cs9.codebook_runbook;
		encoder->codebook_magbook[0] = cs9.codebook_magbook;
		encoder->valuebook[0] = cs9.valuebook;
		{
			int pos = cs9.tagsbook[0]-1; // The last code in the tagsbook in the band_end_code
			encoder->band_end_code[0] = (unsigned int)cs9.tagsbook[pos*2+2];
			encoder->band_end_size[0] = (int)cs9.tagsbook[pos*2+1];
		}
	}

	// Set the variable bitrate scale factor
	encoder->vbrscale = 256;
	
	// Initialize the codec state
	InitCodecState(&encoder->codec);

#if 0	//_THREADED_ENCODER
	// Initialize the encoder for single thread processing
	encoder->thread_type = THREAD_TYPE_SINGLE;
	encoder->num_threads = 0;
#endif

#if _THREADED_ENCODER

#if 0
	// Initialize the frame processing thread handles
	for (i = 0; i < CODEC_GOP_LENGTH; i++)
	{
		encoder->frame_thread[i] = INVALID_HANDLE_VALUE;
	}

	// Clear the handle to the object that owns this encoder
	//encoder->handle = NULL;
#endif

	// Initialize the channel thread handles for each frame
	for (i = 0; i < CODEC_GOP_LENGTH; i++)
	{
		int j;

		for (j = 0; j < CODEC_MAX_CHANNELS; j++)
		{
			encoder->frame_channel_thread[i][j] = INVALID_HANDLE_VALUE;
		}
	}

	// Initialize the channel processing thread handles
	for (i = 0; i < CODEC_MAX_CHANNELS; i++)
	{
		encoder->finish_channel_thread[i] = INVALID_HANDLE_VALUE;
	}

	encoder->affinity_mask = 0;

#endif

	// Set the input color space to the default value
	encoder->input.color_space = 0;//COLOR_SPACE_DEFAULT;

	// Set the encoded format to the default internal representation
	encoder->encoded_format = ENCODED_FORMAT_YUV_422;

#if _DUMP

	// Initialize the descriptor for controlling debug output

	encoder->dump.enabled = false;

	encoder->dump.channel_mask = 0;
	encoder->dump.wavelet_mask = 0;

	memset(encoder->dump.directory, 0, sizeof(encoder->dump.directory));
	memset(encoder->dump.filename, 0, sizeof(encoder->dump.filename));

#endif

#if _ALLOCATOR
	//TODO: Need to create a memory allocator for the encoder
	encoder->allocator = NULL;
#endif

	// Clear all of the metadata entries (local and global)
	memset(&encoder->metadata, 0, sizeof(encoder->metadata));

#if _DEBUG
	// Buffer used to debug entropy coding of highpass bands
	encoder->encoded_band_bitstream = NULL;
#endif
}


#if _ALLOCATOR
void MetadataFree(ALLOCATOR *allocator, void **extended, size_t *extended_size)
#else
void MetadataFree(void **extended, size_t *extended_size)
#endif
{
	if (extended && *extended && extended_size && *extended_size)
	{
#if _ALLOCATOR
		Free(allocator, *extended);
#else
		MEMORY_FREE(*extended);
#endif
		*extended = NULL;
		*extended_size = 0;
	}
}

#if _ALLOCATOR
uint32_t *AllocMetadataBlock(ALLOCATOR *allocator, size_t size)
#else
uint32_t *AllocMetadataBlock(size_t size)
#endif
{
#if _ALLOCATOR
	return Alloc(allocator, size);
#else
	return MEMORY_ALLOC(size);
#endif
}

#if _ALLOCATOR
void AllocMetadata(ALLOCATOR *allocator, METADATA *metadata, size_t size)
{
	if (metadata != NULL)
	{
		metadata->block = AllocMetadataBlock(allocator, size);
		if (metadata->block != NULL)
		{
			metadata->size = 0;
			metadata->limit = size;
			metadata->allocator = allocator;
		}
	}
}
#else
void AllocMetadata(METADATA *metadata, size_t size)
{
	if (metadata != NULL)
	{
		metadata->block = AllocMetadataBlock(size);
		if (metadata->block != NULL)
		{
			metadata->size = 0;
			metadata->limit = size;
		}
	}
}
#endif

void FreeMetadata(METADATA *metadata)
{
	if(metadata)
	{
		if(metadata->block)
		{
		#if _ALLOCATOR
			Free(metadata->allocator, metadata->block);
		#else
			MEMORY_FREE(metadata->block);
		#endif

			memset(metadata, 0, sizeof(METADATA));
		}
	}
}

#define TAGSIZE(x) ((((((x)&0xffffff))+3)>>2)<<2)

/*!
	@brief Add the specified item of metadata to the metadata structure.
	
	The memory block used for metadata in the metadata structure is enlarged
	if necessary.

	@todo Start using the metadata limit member variable for the size of the
	allocated block and use the size member variable for the actual size of
	the metadata.  This change would allow the metadata block to be allocated
	larger than necessary to hold the metadata.  Memory fragmentation would be
	reduced by allocating blocks in sizes that are a power of two.
*/
bool AddMetadata(METADATA *metadata,
				 uint32_t tag,
				 unsigned char type,
				 uint32_t size,
				 uint32_t *data)
{
	uint32_t typesizebytes = METADATA_TYPESIZE(type, size);
	int allocsize = 8 + ((size + 3) & 0xfffffc);
	//void **extended;
	//size_t *extended_size;
	size_t new_block_size;

	//! Maximum allocated size of the metadata block
	const size_t maximum_size = 65500*4;
	
#if _ALLOCATOR
	ALLOCATOR *allocator = NULL;
#endif

	if(metadata == NULL) 
		return false;
	
#if _ALLOCATOR
	allocator = metadata->allocator;
#endif

	//TODO: Replace use of these variables with references to the metadata structure
	//extended = &metadata->block;
	//extended_size = &metadata->size;

	new_block_size = metadata->size + allocsize;

	if (data && size && (new_block_size < maximum_size))
	{
		int found = 0;

		// Has a metadata block been allocated?
		if (metadata->block)
		{
			// If TAG pairs or Freespace or last char or FOURCC is lower, don't check of existing tag duplicates.
			if ((tag>>24) < 'a' && tag != TAG_FREESPACE && tag != TAG_REGISTRY_NAME &&
				tag != TAG_REGISTRY_VALUE && tag != TAG_NAME && tag != TAG_VALUE)
			{
				int i;
				//size_t offset = *extended_size;
				uint32_t offset = (uint32_t)metadata->size;
				//uint8_t *newdata = (uint8_t *)*extended;
				uint8_t *newdata = (uint8_t *)metadata->block;
				uint8_t *srcdata = (uint8_t *)data;
				uint32_t  *Lnewdata;
				uint32_t  *Lstartdata = (uint32_t  *)newdata;
				int pos = 0;
				int longs = offset >> 2;
				newdata += offset;
				Lnewdata = (uint32_t  *)newdata;

				while (pos < longs)
				{
					int datalen;
					if (Lstartdata[pos] == tag)
					{
						if (TAGSIZE(Lstartdata[pos+1]) == TAGSIZE(typesizebytes)) // same size replace
						{
							Lnewdata = &Lstartdata[pos];

							*Lnewdata++ = tag;
							*Lnewdata++ = typesizebytes;
							newdata = (uint8_t *)Lnewdata;
							for(i=0;i<(int)size;i++)
							{
								*newdata++ = *srcdata++;
							}
							for(;i<(int)((size+3) & 0xfffffc);i++)
							{
								*newdata++ = 0;
							}
							found = 1;
							break;
						}
						else // size change so remove the old
						{	
							//uint32_t len = *extended_size>>2;
							uint32_t len = (uint32_t)(metadata->size >> 2);
							uint32_t remlen = ((TAGSIZE(Lstartdata[pos+1])+8+3)>>2);
							for(i=pos+remlen; i<(int)len; i++)
								Lstartdata[i-remlen] = Lstartdata[i];

							found = 0;
							//*extended_size -= (remlen<<2);
							metadata->size -= (remlen<<2);

							break;
						}
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

		if (!found)
		{
			// Need to allocate the metadata block?
			//if(*extended == NULL)
			if (metadata->block == NULL)
			{
#if _ALLOCATOR
				//*extended = (uint32_t  *)Alloc(allocator, allocsize);
				metadata->block = (uint32_t  *)Alloc(allocator, allocsize);
#else
				//*extended = (uint32_t  *)MEMORY_ALLOC(allocsize);
				metadata->block = (uint32_t  *)MEMORY_ALLOC(allocsize);
#endif
			}
			else
			{
#if 0
				uint32_t *last = *extended;
#if _ALLOCATOR
				*extended = (uint32_t  *)Alloc(allocator, *extended_size + allocsize);
				memcpy(*extended, last, *extended_size);
				Free(allocator, last);
#else
				*extended = (uint32_t  *)MEMORY_ALLOC(*extended_size + allocsize);
				memcpy(*extended, last, *extended_size);
				MEMORY_FREE(last);
#endif
#else
				uint32_t *old_block = metadata->block;
#if _ALLOCATOR
				metadata->block = (uint32_t *)Alloc(allocator, new_block_size);
				memcpy(metadata->block, old_block, metadata->size);
				Free(allocator, old_block);
#else
				metadata->block = (uint32_t *)MEMORY_ALLOC(new_block_size);
				memcpy(metadata->block, old_block, metadata->size);
				MEMORY_FREE(old_block);
#endif
#endif
			}

			//if(*extended)
			if (metadata->block)
			{
				int i;
				//size_t offset = *extended_size;
				uint32_t offset = (uint32_t)metadata->size;
				//uint8_t *newdata = (uint8_t *)*extended;
				uint8_t *newdata = (uint8_t *)metadata->block;
				uint8_t *srcdata = (uint8_t *)data;
				uint32_t  *Lnewdata;
				uint32_t  *Lstartdata = (uint32_t  *)newdata;
				int pos=0,longs = offset >> 2;
				newdata += offset;
				Lnewdata = (uint32_t  *)newdata;

				while(pos < longs)
				{
					int datalen;

					if (Lstartdata[pos] == TAG_FREESPACE &&
						((METADATA_SIZE)(Lstartdata[pos+1] & 0xffffff) >= size))
					{
						int freebytes = (Lstartdata[pos+1] & 0xffffff);

						Lnewdata = &Lstartdata[pos];

						*Lnewdata++ = tag;
						*Lnewdata++ = typesizebytes;

						newdata = (uint8_t *)Lnewdata;
						for(i=0;i<(int)size;i++)
						{
							*newdata++ = *srcdata++;
						}
						for(;i<(int)((size+3) & 0xfffffc);i++)
						{
							*newdata++ = 0;
						}
						found = 1;

						//Lnewdata = (unsigned long  *)(((uintptr_t)newdata + 3) & (UINTPTR_MAX 0xfffffffc);
						Lnewdata = (uint32_t *)ALIGNED_N_PTR(newdata, 3);
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

				if (!found)
				{
					//offset = *extended_size;
					offset = (uint32_t)metadata->size;
					*Lnewdata++ = tag;
					*Lnewdata++ = typesizebytes;
					newdata = (uint8_t *)Lnewdata;
					for(i=0;i<(int)size;i++)
					{
						*newdata++ = *srcdata++;
					}
					for(;i<(int)((size+3) & 0xfffffc);i++)
					{
						*newdata++ = 0;
					}

					//*extended_size += allocsize;
					metadata->size += allocsize;
				}
			}
			return true;
		}
		else
		{
			// Data type was found in the metadata, and updated.
			return true; // was false, no idea why.  
		}
	}
	return false;
}

void AttachMetadata(ENCODER *encoder, METADATA *dst, METADATA *src)
{
	if(dst == NULL || src == NULL) return;

	if(dst->block)
	{
		if(src->size == 0)
		{
			FreeMetadata(dst);
		}
		else if(dst->size >= src->size)
		{
			memcpy(dst->block, src->block, src->size);
			dst->size = src->size;
		}
		else
		{
#if _ALLOCATOR
			FreeMetadata(dst);
			AllocMetadata(encoder->allocator, dst, src->size);
#else
			FreeMetadata(dst);
			AllocMetadata(dst, src->size);
#endif
			if(dst->block)
			{
				memcpy(dst->block, src->block, src->size);
				dst->size = src->size;
			}
		}
	}
	else
	{
#if _ALLOCATOR
		AllocMetadata(encoder->allocator, dst, src->size);
#else
		AllocMetadata(dst, src->size);
#endif
		if(dst->block)
		{
			memcpy(dst->block, src->block, src->size);
			dst->size = src->size;
		}
	}
}


// Free data structures allocated within the encoder
void ClearEncoder(ENCODER *encoder)
{
#if _ALLOCATOR
	ALLOCATOR *allocator = encoder->allocator;
#endif

	if(encoder->metadata.global.block)
	{
		FreeMetadata(&encoder->metadata.global);
		encoder->metadata.global.block = NULL;
		encoder->metadata.global.size = 0;
	}
	if(encoder->metadata.local.block)
	{
		FreeMetadata(&encoder->metadata.local);
		encoder->metadata.local.block = NULL;
		encoder->metadata.local.size = 0;
	}

	if (encoder->frame)
	{
		// Free the frame allocated as scratch memory
#if _ALLOCATOR
		DeleteFrame(allocator, encoder->frame);
#else
		DeleteFrame(encoder->frame);
#endif
		encoder->frame = NULL;
	}

	if(encoder->linebuffer)
	{
#if _ALLOCATOR
		FreeAligned(allocator, encoder->linebuffer);
#else
		MEMORY_ALIGNED_FREE(encoder->linebuffer);
#endif
		encoder->linebuffer = NULL;
	}
}

// Cleanup the encoder before the program exits
void ExitEncoder(ENCODER *encoder)
{
	// Let the caller keep the logfile open or choose to close it
	//if (encoder->logfile != NULL) fclose(encoder->logfile);

	// Free any data structures allocated within the encoder
	ClearEncoder(encoder);
}

// Compute the size of the encoding buffer required for the specified combination
// of frame dimensions and format, GOP length, and progressive versus interlaced.
// This routine must be kept in sync with the actual scratch buffer allocations
// used by the encoder.
size_t EncodingBufferSize(int width, int height, int pitch, int format,
						  int gop_length, bool progressive)
{
	size_t size = 0;

	// Compute the encoding buffer size for the specified encoding parameters
	if (gop_length == 1)
	{
		if (progressive)
		{
			{
				// Need enough scratch space for the largest spatial transform
				size = ForwardSpatialBufferSize(width);
			}
		}
	}

	// Was the exact encoding buffer size computed?
	if (size == 0)
	{
		//TODO: Need to implement more accurate buffer size calculations for the other cases
		//assert(0);

		size = height * pitch;

#if 1
		// Some paths through the code may need extra buffer space
		size += 32 * width * sizeof(PIXEL);
		size *= 2;
#endif
	}
#ifdef  __APPLE__
#undef ALIGN
#define ALIGN(n,m)			((((size_t)(n)) + ((m)-1)) & ~((size_t)((m)-1)))
#endif
	// Round up the buffer allocation to an integer number of cache lines
	size = ALIGN(size, _CACHE_LINE_SIZE);

	return size;
}

// Compute the encoding buffer size forcing the size to be at least as large as a frame
size_t TotalEncodingBufferSize(int width, int height, int pitch, int format,
						  int gop_length, bool progressive)
{
	size_t size;
	size_t rounded_height = (height + 7) & ~0x07;
	size_t frame_size = rounded_height * pitch;

	// Compute the size of the encoding buffer
	size = EncodingBufferSize(width, height, pitch, format, gop_length, progressive);

	// The buffer must be large enough to hold the extended input frame
	if (size < frame_size) {
		size = frame_size;
	}

	return size;
}

// Create a scratch buffer for use by the encoder
#if _ALLOCATOR
PIXEL *CreateEncodingBuffer(ALLOCATOR *allocator,
							int width, int height, int pitch, int format,
							int gop_length, bool progressive,
							size_t *allocated_size)
#else
PIXEL *CreateEncodingBuffer(int width, int height, int pitch, int format,
							int gop_length, bool progressive,
							size_t *allocated_size)
#endif
{
	size_t size;
	PIXEL *buffer;
	size_t rounded_height = (height + 7) & ~0x07;
	size_t frame_size = rounded_height * pitch;

	frame_size += 65536; // metadata overhead

	frame_size += pitch * 18; //DAN20140922 some code paths need up to 18 addition scanlines of working space.


	// Compute the size of the encoding buffer
	size = EncodingBufferSize(width, height, pitch, format, gop_length, progressive);

	// The buffer must be large enough to hold the extended input frame
	if (size < frame_size) {
		size = frame_size;
	}

    // Allocate a buffer aligned to the cache line size
#if _ALLOCATOR
	buffer = (PIXEL *)AllocAligned(allocator, size, _CACHE_LINE_SIZE);
#else
	buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, _CACHE_LINE_SIZE);
#endif

	// Return the allocated size
	if (allocated_size != NULL) {
		if (buffer != NULL) *allocated_size = size;
		else *allocated_size = 0;
	}

	// Return the pointer to the allocated buffer
	return buffer;
}

#if _ALLOCATOR
void DeleteEncodingBuffer(ALLOCATOR *allocator, PIXEL *buffer)
#else
void DeleteEncodingBuffer(PIXEL *buffer)
#endif
{
	if (buffer != NULL)
	{
#if _ALLOCATOR
		FreeAligned(allocator, buffer);
#else
		MEMORY_ALIGNED_FREE(buffer);
#endif
	}
}

#if 0
// Compute statistics required for encoding the lowpass band
void ComputeLowPassStatistics(ENCODER *encoder, IMAGE *image)
{
	int32_t pixel_sum = 0;
	int pixel_count = 0;
	int pixel_minimum = PIXEL_MAXIMUM;
	int pixel_maximum = PIXEL_MINIMUM;
	PIXEL *pImageRow;
	int row, column;
	int pixel_average;
	int pitch = image->pitch/sizeof(PIXEL);

	assert(image != NULL);

	pImageRow = image->band[0];

	for (row = 0; row < image->height; row++) {
		for (column = 0; column < image->width; column++) {
			PIXEL pixel_value = pImageRow[column];
			pixel_sum += pixel_value;
			pixel_count++;
			if (pixel_minimum > pixel_value)
				pixel_minimum = pixel_value;
			if (pixel_maximum < pixel_value)
				pixel_maximum = pixel_value;
		}
		pImageRow += pitch;
	}

	pixel_average = pixel_sum / pixel_count;

	encoder->lowpass.average = pixel_average;
	encoder->lowpass.minimum = pixel_minimum;
	encoder->lowpass.maximum = pixel_maximum;
}
#endif

#if DEBUG
// Verify that the codebooks are valid
static bool ValidCodebooks(void)
{
	//if (!IsValidCodebook(cs5.magsbook)) return false;
	//if (!IsValidCodebook(cs6.magsbook)) return false;
	//if (!IsValidCodebook(cs7.magsbook)) return false;
	//if (!IsValidCodebook(cs8.magsbook)) return false;

	// Insert verifications for additional codebooks here

	if (!IsValidCodebook(cs9.magsbook)) return false;


	// All codebooks are valid if all of the codebooks passed the tests above
	return true;
}
#endif

void SetLogfile(ENCODER *state, FILE *file)
{
	state->logfile = file;
}

void SetEncoderParams(ENCODER *encoder, int gop_length, int num_spatial)
{
	encoder->gop_length = gop_length;		// Number of frames in group
	encoder->num_spatial = num_spatial;		// Levels of spatial wavelets

	// Must initialize num_levels and num_subbands in the wavelet transform
}

void SetEncoderFormat(ENCODER *encoder, int width, int height, int display_height, int format, int encoded_format)
{
#if _ALLOCATOR
	ALLOCATOR *allocator = encoder->allocator;
#endif

	// Remember the parameters of the input data
	encoder->input.width = width;
	encoder->input.height = height;
	//encoder->input.display_height = display_height;
	encoder->input.format = format;

	// Set the display parameters
	encoder->display.width = width;
	encoder->display.height = display_height;

	// Set the encoded format to the default internal representation
	encoder->encoded_format = encoded_format;

	//NOTE: Currently the format field contains only the pixel format and the
	// color space is stored in a different field.  The design is evolving and
	// the color space may be stored in the high word of the format double word.
	
	// The encoder frames use the input dimensions but with YUV format
	if(encoded_format == ENCODED_FORMAT_BAYER)
	{
		// Allocate a frame with four channels of equal dimensions
		#if _ALLOCATOR
		encoder->frame = ReallocFrame(allocator, encoder->frame, width, height,
									  display_height, FRAME_FORMAT_RGBA);
		#else
		encoder->frame = ReallocFrame(encoder->frame, width, height, display_height, FRAME_FORMAT_RGBA);
		#endif
		// Remember the parameters of the Bayer image
		encoder->bayer.width = width / 2;
		encoder->bayer.height = height / 2;
	}
	else if(encoded_format == ENCODED_FORMAT_RGBA_4444)
	{
		// Allocate a frame with four channels of equal dimensions
		#if _ALLOCATOR
		encoder->frame = ReallocFrame(allocator, encoder->frame, width, height,
									  display_height, FRAME_FORMAT_RGBA);
		#else
		encoder->frame = ReallocFrame(encoder->frame, width, height, display_height, FRAME_FORMAT_RGBA);
		#endif
	}
	else if(encoded_format == ENCODED_FORMAT_RGB_444)
	{
		// Allocate a frame with three channels of equal dimensions
		#if _ALLOCATOR
		encoder->frame = ReallocFrame(allocator, encoder->frame, width, height,
									  display_height, FRAME_FORMAT_RGB);
		#else
		encoder->frame = ReallocFrame(encoder->frame, width, height, display_height, FRAME_FORMAT_RGB);
		#endif
	}
	else
	{
		// Allocate a frame with one luma and two half-width chroma channels (4:2:2 format)
		#if _ALLOCATOR
		encoder->frame = ReallocFrame(allocator, encoder->frame, width, height,
									  display_height, FRAME_FORMAT_YUV);
		#else
		encoder->frame = ReallocFrame(encoder->frame, width, height, display_height, FRAME_FORMAT_YUV);
		#endif
		encoder->encoded_format = ENCODED_FORMAT_YUV_422; //DAN20120228
	}

#if (1 && SYSLOG)
	fprintf(stderr, "SetEncoderFormat allocated frame: 0x%p\n", (int)encoder->frame);
#endif
}

// The recursive encoder does not allocate a frame for unpacking the input image
void SetEncoderFormatRecursive(ENCODER *encoder, int width, int height, int display_height, int format)
{
	// Remember the parameters of the input data
	encoder->input.width = width;
	encoder->input.height = height;
	//encoder->input.display_height = display_height;
	encoder->input.format = format;

	// Set the display parameters
	encoder->display.width = width;
	encoder->display.height = display_height;

	//NOTE: Currently the format field contains only the pixel format and the
	// color space is stored in a different field.  The design is evolving and
	// the color space may be stored in the high word of the format double word.

	// Older code uses the parameters in the frame data structure even though the
	// input frame is always unpacked row by row and the frame memory is not allocated

	// Set the frame dimensions without allocating memory for the planes
	if (encoder->frame == NULL)
	{
#if _ALLOCATOR
		encoder->frame = (FRAME *)Alloc(encoder->allocator, sizeof(FRAME));
#else
		encoder->frame = (FRAME *)MEMORY_ALLOC(sizeof(FRAME));
#endif
	}

	if (encoder->frame == NULL)
	{
#if (DEBUG && _WIN32)
		OutputDebugString("sizeof(FRAME)");
#endif
		return;
	}

	if(ISBAYER(format))
	{
		// Set the frame dimensions to four channels of equal dimensions
		SetFrameDimensions(encoder->frame, width, height, display_height, FRAME_FORMAT_RGBA);
	}
	else if (format == COLOR_FORMAT_RG64)
	{
		// Set the frame dimensions to four channels of equal dimensions
		SetFrameDimensions(encoder->frame, width, height, display_height, FRAME_FORMAT_RGBA);
	}
	else if (format == COLOR_FORMAT_RG48)
	{
		// Set the frame dimensions to four channels of equal dimensions
		SetFrameDimensions(encoder->frame, width, height, display_height, FRAME_FORMAT_RGB);
	}
	else
	{
		// Set the frame dimensions to one luma and two half-width chroma channels (4:2:2 format)
		SetFrameDimensions(encoder->frame, width, height, display_height, FRAME_FORMAT_YUV);
	}
}

bool SetEncoderColorSpace(ENCODER *encoder, int color_space)
{
	if (MIN_DECODED_COLOR_SPACE <= color_space && color_space <= MAX_DECODED_COLOR_SPACE)
	{
		// Set the color space for the input pixels
		encoder->input.color_space = color_space;

		// Indicate that the color flags were set as specified
		return true;
	}

	// The specified color flags were not valid
	return false;
}

void SetEncoderQuantization(ENCODER *encoder, int format, int i_fixedquality, int fixedbitrate, custom_quant *custom)
{
	//FILE *logfile = encoder->logfile;
	QUANTIZER *q = &encoder->q;
	int fixedquality = i_fixedquality;
	bool ChromaFullRes = false;
	int rgb_quality = (i_fixedquality & 0x06000000/*CFEncode_RGB_Quality_Mask*/) >> 25;

	ChromaFullRes = (format >= COLOR_FORMAT_BAYER);

	if(custom && custom->magicnumber == 0x12345678)
	{
		int i;
		q->newQuality = 7; // custom
		q->quantLimit = DEFAULT_QUANT_LIMIT;

		for(i=0;i<MAX_QUANT_SUBBANDS;i++)
		{
			q->quantLuma[i] = custom->quantY[i];
			q->quantLumaMAX[i] = custom->quantY[i];
			if(ChromaFullRes)
			{
				q->quantChroma[i] = custom->quantY[i];
				q->quantChromaMAX[i] = custom->quantY[i];
			}
			else
			{
				q->quantChroma[i] = custom->quantC[i];
				q->quantChromaMAX[i] = custom->quantC[i];
			}

			q->codebookflags[i] = custom->codebookflags[i];

			if(encoder->codec.precision >= 10)
			{
				if(i==7) //
				{
					q->quantLuma[i] = 4; //TLL - lossless
					q->quantLumaMAX[i] = 4;
					q->quantChroma[i] = 4;
					q->quantChromaMAX[i] = 4;
				}
				else if(i>8)
				{
					q->quantLuma[i] *= 4;
					q->quantLumaMAX[i] *= 4;
					q->quantChroma[i] *= 4;
					q->quantChromaMAX[i] *= 4;
				}
			}

			if(encoder->codec.precision == CODEC_PRECISION_12BIT)
			{
				int chromagain = 4;

				if(i>=4 && i<7)
				{
					q->quantLuma[i] *= 4;
					q->quantChroma[i] *= 4; // If coding as G, R-G and B-G else *4
					q->quantLumaMAX[i] *= 4;
					q->quantChromaMAX[i] *= 4; // If coding as G, R-G and B-G else *4
				}
				switch(rgb_quality)
				{
				case 0: chromagain = 8; break;
				case 1: chromagain = 6; break;
				case 2: chromagain = 4; break;
				case 3: chromagain = 4; break;
				}

				if(i>=11 && i<17)
				{
					q->quantLuma[i] *= 4;
					q->quantChroma[i] *= chromagain; // If coding as G, R-G and B-G else *4
					q->quantLumaMAX[i] *= 4;
					q->quantChromaMAX[i] *= chromagain; // If coding as G, R-G and B-G else *4
				}
			}
		}

		if(encoder->gop_length == 1)
		{
			for(i=7; i<10; i++)
			{
				q->quantLuma[i] = q->quantLuma[i+4];
				q->quantLumaMAX[i] = q->quantLumaMAX[i+4];
				q->quantChroma[i] = q->quantChroma[i+4];
				q->quantChromaMAX[i] = q->quantChromaMAX[i+4];

				q->codebookflags[i] = q->codebookflags[i+4];
			}
		}
	}
	else
	{
		if ((fixedquality & 0xFF) == 0)
			QuantizationSetRate(q, fixedbitrate, encoder->progressive, encoder->codec.precision, encoder->gop_length, ChromaFullRes);
		else
			QuantizationSetQuality(q, fixedquality,
				encoder->progressive,
				encoder->codec.precision,
				encoder->gop_length,
				ChromaFullRes,
				encoder->frame,
				encoder->lastgopbitcount>>3,
				encoder->video_channels);
	}

#if (0 && DEBUG)
	if (logfile) {
		PrintQuantizer(q, logfile);
	}
#endif

}

// Compute the index of a subband in a spatial wavelet pyramid
int SubBandIndex(ENCODER *encoder, int level, int band)
{
	int num_levels = encoder->num_levels;
	int index;

	// Check that the level and band arguments are valid
	assert(0 < level && level <= num_levels);
	assert(0 <= band && band < CODEC_MAX_BANDS);

	// Invert the level into an index from the top of the pyramid
	level = num_levels - level;

	// Check that lowpass band is accessed only at the top level
	assert(band > 0 || level == 0);

	if (level == 0) index = band;
	else index = 4 + 3 * (level - 1) + (band - 1);

	// Check that the index is valid
	assert(0 <= index && index < CODEC_MAX_SUBBANDS);

	return index;
}

#if _ALLOCATOR
void SetEncoderAllocator(ENCODER *encoder, ALLOCATOR *allocator)
{
	// Do not change the allocator after it has been used by the encoder
	assert(encoder->allocator == NULL);
	encoder->allocator = allocator;
}
#endif

// New routine for allocating and initializing an encoder
ENCODER *CreateEncoderWithParameters(ALLOCATOR *allocator,
									 TRANSFORM *transform[],
									 int num_channels,
									 ENCODING_PARAMETERS *parameters)
{
	bool result = false;

	ENCODER *encoder = Alloc(allocator, sizeof(ENCODER));
	if (encoder == NULL) {
		return encoder;
	}

	// Set default values for the encoder parameters that were not provided
	SetDefaultEncodingParameters(parameters);

	// Initialize the encoder using the information in the encoder parameters
#if _ALLOCATOR
	result = InitializeEncoderWithParameters(allocator, encoder, transform, num_channels, parameters);
#else
	result = InitializeEncoderWithParameters(encoder, transform, num_channels, parameters);
#endif

	if (!result)
	{
		// The initializer should have released any resources allocated for the encoder

		// Free the encoder and return a null pointer
		Free(allocator, encoder);
		return NULL;
	}

	// Return the encoder
	return encoder;
}

void SetDefaultEncodingParameters(ENCODING_PARAMETERS *parameters)
{
	if (parameters->version < ENCODING_PARAMETERS_CURRENT_VERSION)
	{
		// Initialize any parameters that were added since the older version

	}
}

#if _DEBUG

void PrintEncodingParameters(ENCODING_PARAMETERS *parameters)
{
	int err = 0;
	FILE *file;

#ifdef _WIN32
	err = fopen_s(&file, "parameters.log", "w+");
#else
	file = fopen("parameters.log", "w+");
#endif

	if (err == 0 && file != NULL)
	{
		fprintf(file, "version: %d\n", parameters->version);
		fprintf(file, "gop_length: %d\n", parameters->gop_length);
		fprintf(file, "encoded_width: %d\n", parameters->encoded_width);
		fprintf(file, "encoded_height: %d\n", parameters->encoded_height);
		fprintf(file, "fixed_quality: %d\n", parameters->fixed_quality);
		fprintf(file, "fixed_bitrate: %d\n", parameters->fixed_bitrate);
		fprintf(file, "format: %d\n", parameters->format);
		fprintf(file, "progressive: %d\n", parameters->progressive);
		fprintf(file, "frame_sampling: %d\n", parameters->frame_sampling);
		fprintf(file, "colorspace_yuv: %d\n", parameters->colorspace_yuv);
		fprintf(file, "colorspace_rgb: %d\n", parameters->colorspace_rgb);

		fclose(file);
	}
}

#endif

#if _ALLOCATOR
bool InitializeEncoderWithParameters(ALLOCATOR *allocator,
									 ENCODER *encoder, TRANSFORM *transform[], int num_channels,
									 ENCODING_PARAMETERS *parameters)
#else
bool InitializeEncoderWithParameters(ENCODER *encoder, TRANSFORM *transform[], int num_channels,
									 ENCODING_PARAMETERS *parameters)
#endif
{
	int encoded_format = ENCODED_FORMAT_YUV_422;
	CODESET codesets[CODEC_NUM_CODESETS];

	//TODO: Allocate the transform in the encoder if the transform array is null

	int chroma_width;				// Dimensions of the chroma images
	int chroma_height;
	int transform_type;				// Type of wavelet transform
	int num_spatial = TRANSFORM_NUM_SPATIAL;
	int channel;

	QUANTIZER *q = &encoder->q;

	// Encoding parameters that used to be passed as arguments to EncodeInit
	int gop_length;
	int width;
	int height;
	int display_height;
	FILE *logfile;
	int fixedquality;
	int fixedbitrate;
	int format;
	bool progressive;
	bool chromaFullRes;

#if 0	//_THREADED_ENCODER
	DWORD WINAPI (* thread_proc)(LPVOID param);
	int i;
#endif

	if (parameters == NULL) {
		return false;
	}

#if CODEC_NUM_CODESETS == 3
	memcpy(&codesets[0], &CURRENT_CODESET, sizeof(CODESET));
	memcpy(&codesets[1], &SECOND_CODESET, sizeof(CODESET));
	memcpy(&codesets[2], &THIRD_CODESET, sizeof(CODESET));
#elif CODEC_NUM_CODESETS == 2
	memcpy(&codesets[0], &CURRENT_CODESET, sizeof(CODESET));
	memcpy(&codesets[1], &SECOND_CODESET, sizeof(CODESET));
#else
	memcpy(&codesets[0], &CURRENT_CODESET, sizeof(CODESET));
#endif

	// Initialize the codebooks
#if _ALLOCATOR
	if (!InitCodebooks(allocator, &codesets[0])) {
		encoder->error = CODEC_ERROR_INIT_CODEBOOKS;
		return false;
	}
#else
	if (!InitCodebooks(&codesets[0])) {
		encoder->error = CODEC_ERROR_INIT_CODEBOOKS;
		return false;
	}
#endif

	//PrintEncodingParameters(parameters);

	// Extract the encoding parameters (for compatibility with code from EncodeInit)
	gop_length = parameters->gop_length;
	width = parameters->encoded_width;
	display_height = height = parameters->encoded_height;
	logfile = parameters->logfile;
	fixedquality = parameters->fixed_quality;
	fixedbitrate = parameters->fixed_bitrate;
	progressive = parameters->progressive;
	format = parameters->format;

	// Is this a frame transform?
	if (gop_length == 1)
	{
		// Eliminate the spatial transform on the temporal highpass band
		num_spatial--;
	}

	// Clear all encoder fields except the logfile and set the codebooks for encoding
	InitEncoder(encoder, logfile, &codesets[0]);

	
	chromaFullRes = false;

	encoded_format = GetEncodedFormat(format, fixedquality, num_channels);
	encoder->encoded_format = encoded_format;

	switch(encoded_format)
	{
		default:
		case ENCODED_FORMAT_YUV_422:
			chromaFullRes = false;
			num_channels = 3;
			break;
		case ENCODED_FORMAT_RGB_444:
			chromaFullRes = true;
			num_channels = 3;
			break;
		case ENCODED_FORMAT_RGBA_4444:
			chromaFullRes = true;
			num_channels = 4;
			break;
		case ENCODED_FORMAT_BAYER:
			chromaFullRes = true;
			num_channels = 4;
			break;
	}

	encoder->chromaFullRes = chromaFullRes;


#if _ALLOCATOR
	// Set the allocator before allocating any encoder resources
	SetEncoderAllocator(encoder, allocator);
#endif

	// Set the default encoding parameters that define the wavelet transform
	//SetEncoderParams(encoder, CODEC_GOP_LENGTH, TRANSFORM_NUM_SPATIAL);
	SetEncoderParams(encoder, gop_length, num_spatial);

	// Initialize the default quantization tables
	//InitDefaultQuantizer();

	// Initialize the quantizer in the encoder
	InitQuantizer(q);

	if(parameters->colorspace_yuv || parameters->colorspace_rgb)
	{
		if(parameters->colorspace_yuv == 1) //601
		{
			if(parameters->colorspace_rgb == 2) //vsrgb
			{
				SetEncoderColorSpace(encoder, COLOR_SPACE_VS_601);
			}
			else
			{
				SetEncoderColorSpace(encoder, COLOR_SPACE_CG_601);
			}
		}
		else //0 or 2 = 709 default
		{
			if(parameters->colorspace_rgb == 2) //vsrgb
			{
				SetEncoderColorSpace(encoder, COLOR_SPACE_VS_709);
			}
			else
			{
				SetEncoderColorSpace(encoder, COLOR_SPACE_CG_709);
			}
		}
	}
	else if(height && width)
	{
		if(height > 576 || width > 720)
			SetEncoderColorSpace(encoder, COLOR_SPACE_CG_709);
		else
			SetEncoderColorSpace(encoder, COLOR_SPACE_CG_601);
	}
	else
	{
		SetEncoderColorSpace(encoder, COLOR_SPACE_CG_709); // common default.
	}

	if ((fixedquality & 0xFFFF) == 0)
	{
		QuantizationSetRate(q, fixedbitrate, progressive, encoder->codec.precision, encoder->gop_length, chromaFullRes);
	}
	else {
		QuantizationSetQuality(q,
			fixedquality,
			progressive,
			encoder->codec.precision,
			encoder->gop_length,
			chromaFullRes,
			encoder->frame,
			encoder->lastgopbitcount>>3,
			encoder->video_channels);
	}

	// Load new quality table if any.
	//QuantizationLoadTables();

	// Set progressive or interlaced frame encoding
	//assert(progressive == 0 || progressive == 1);
	encoder->progressive = progressive ? 1:0;

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "EncodeInit progressive: %d\n", encoder->progressive);
	}
#endif

	// Reset the frame number and count of encoded frames
	encoder->frame_number = 0;
	encoder->frame_count = 0;

	encoder->lastgopbitcount = 0;

	//Round the height up to the near multiple of 8.
	//Need for 1080 bayer encodes (as 540 per band is not divisible by 8)
	height += 7;
	height /= 8;
	height *= 8;

	// Allocate space for the wavelet transforms
	if(chromaFullRes)
	{
		chroma_width = width;
	}
	else
	{
		chroma_width = width/2;
	}
	chroma_height = height;



#if _FIELDPLUS_TRANSFORM
	transform_type = (gop_length > 1) ? TRANSFORM_TYPE_FIELDPLUS : TRANSFORM_TYPE_SPATIAL;
#else
	transform_type = (gop_length > 1) ? TRANSFORM_TYPE_FIELD : TRANSFORM_TYPE_SPATIAL;
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile,
				"EncodeInit gop: %d, width: %d, height: %d, transform type: %d, spatial levels: %d\n",
				gop_length, width, height, transform_type, num_spatial);
	}
#endif

	// Check that the frame dimensions are appropriate for the transform
	assert(IsFrameTransformable(chroma_width, height, transform_type, num_spatial));

	// Should return an error if the frame size is not apppropriate
	// but have never encountered this situation in testing so far

	// Allocate a transform data structure for each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		int transform_width = (channel == 0) ? width : chroma_width;
		int transform_height = (channel == 0) ? height : chroma_height;

		//TODO: Check whether the transform for this channel must be allocated

#if _ALLOCATOR
		AllocTransform(allocator, transform[channel], transform_type,
					   transform_width, transform_height, gop_length, num_spatial);
#else
		AllocTransform(transform[channel], transform_type, transform_width, transform_height,
					   gop_length, num_spatial);
#endif
	}

#if _TIMING
	InitTiming();
#endif

#if (0 && DEBUG)
	if (logfile)
		printf("Initialized the encoder, width: %d, height: %d\n", width, height);
#endif

#if (0 && DEBUG)
	if (logfile) {
		PrintValueCodebook(logfile, codeset->valuebook);
	}
#endif

#if 0	//_THREADED_ENCODER
	// Create threads for parallel processing
	encoder->thread_type = THREAD_TYPE_DEFAULT;

	switch (encoder->thread_type)
	{
	default:
		assert(0);
		encoder->thread_type = THREAD_TYPE_SINGLE;

		// Fall through and zero the number of threads

	case THREAD_TYPE_SINGLE:	// No threaded processing
		encoder->num_threads = 0;
		break;

	case THREAD_TYPE_COLORS:	// Process color channels in parallel
		thread_proc = EncodeChannelThreadLoop;
		encoder->num_threads = CODEC_MAX_CHANNELS;
		break;

	case: THREAD_TYPE_FRAMES:	// Process frames in parallel
		thread_proc = EncodeFrameThreadLoop;
		encoder->num_threads = CODEC_GOP_LENGTH;
		break;
	}

	for (i = 0; i < encoder->num_threads; i++)
	{
		encoder->thread[i] = CreateThread(NULL, 0, thread_proc, NULL, 0, &dwThreadID);
	}
#endif

#if _THREADED_ENCODER
	// Determine the processors on which this encoder can execute
	SetEncoderAffinityMask(encoder);
#endif

#if (1 && DUMP)
	// Write the wavelet bands as images
	SetDumpDirectory(CODEC_TYPE(encoder), DUMP_ENCODER_DIRECTORY);
	SetDumpFilename(CODEC_TYPE(encoder), DUMP_DEFAULT_FILENAME);
	SetDumpChannelMask(CODEC_TYPE(encoder), 1/*ULONG_MAX*/);
//	SetDumpWaveletMask(CODEC_TYPE(encoder), 7<<4 | 1/*ULONG_MAX*/);
	SetDumpWaveletMask(CODEC_TYPE(encoder), ULONG_MAX);

	// Set this flag to enable output
	encoder->dump.enabled = true;
#endif

	// The internal representation is YUV 4:2:2 by default
	//encoder->encoded_format = ENCODED_FORMAT_YUV_422;

	SetEncoderFormat(encoder, width, height, display_height, format, encoded_format);//DAN20080530

#if 0 //DAN20160205  This was not appropriate for the Encoder.
	//DAN20160203 Fix for a memory leak in InitCookbooks
	for (i = 0; i < CODEC_NUM_CODESETS; i++)
	{
#if _ALLOCATOR
		Free(allocator, codesets[i].codebook_runbook);	codesets[i].codebook_runbook = NULL;
		Free(allocator, codesets[i].fastbook);			codesets[i].fastbook = NULL;
		Free(allocator, codesets[i].valuebook);			codesets[i].valuebook = NULL;
#else
		MEMORY_FREE(codesets[i].codebook_runbook);		codesets[i].codebook_runbook = NULL;
		MEMORY_FREE(codesets[i].fastbook);				codesets[i].fastbook = NULL;
		MEMORY_FREE(codesets[i].valuebook);				codesets[i].valuebook = NULL;
#endif
	}
#endif

	// The encoder has been initialized successfully
	return true;
}


void SetEncoderQuality(ENCODER *encoder, int fixedquality)
{
	QUANTIZER *q = &encoder->q;
	int quality = (q->inputFixedQuality & 0xffff0000) | (0xffff & (int)fixedquality);

	// Initialize the quantizer in the encoder
	InitQuantizer(q);

	QuantizationSetQuality(q,
		quality,
		encoder->progressive,
		encoder->codec.precision,
		encoder->gop_length,
		encoder->chromaFullRes,
		encoder->frame,
		encoder->lastgopbitcount>>3,
		encoder->video_channels);
}


// Deprecated routine for initializing an encoder
bool EncodeInit(ENCODER *encoder, TRANSFORM *transform[], int num_channels,
				int gop_length, int width, int height, FILE *logfile,
				int i_fixedquality, int fixedbitrate, int format, int progressive, int flags)
{
	ENCODING_PARAMETERS parameters;

	memset(&parameters, 0, sizeof(parameters));

	parameters.version = 1;
	parameters.gop_length = gop_length;
	parameters.encoded_width = width;
	parameters.encoded_height = height;
	parameters.logfile = logfile;
	parameters.fixed_quality = i_fixedquality;
	parameters.fixed_bitrate = fixedbitrate;
	parameters.progressive = progressive;
	parameters.format = format;
	parameters.frame_sampling = (flags & ENCODEINITFLAGS_CHROMA_FULL_RES ? FRAME_SAMPLING_444 : FRAME_SAMPLING_422);
	if(flags & ENCODEINITFLAGS_SET601)
		parameters.colorspace_yuv = 1;
	if(flags & ENCODEINITFLAGS_SET709)
		parameters.colorspace_yuv = 2;
	if(flags & ENCODEINITFLAGS_SETcgRGB)
		parameters.colorspace_rgb = 1;
	if(flags & ENCODEINITFLAGS_SETvsRGB)
		parameters.colorspace_rgb = 2;

#if _ALLOCATOR
	return InitializeEncoderWithParameters(encoder->allocator, encoder, transform, num_channels, &parameters);
#else
	return InitializeEncoderWithParameters(encoder, transform, num_channels, &parameters);
#endif
}

void EncodeRelease(ENCODER *encoder, TRANSFORM *transform[], int num_transforms, BITSTREAM *output)
{
#if _ALLOCATOR
	ALLOCATOR *allocator = encoder->allocator;
#endif

	int i;

#if _TIMING && 0 
	FILE *logfile = encoder->logfile;
	uint32_t frame_count = encoder->frame_count;

	if (logfile != NULL && frame_count > 0)
	{
#ifdef _WIN32
		PrintStatistics(logfile, frame_count, NULL, TIMING_CSV_FILENAME);
#else
		PrintStatistics(logfile, frame_count, NULL, NULL);
#endif
	}
#endif

#if (1 && TRACE_PUTBITS)
	// Close the trace file
	CloseTraceFile();
#endif


#if _THREADED_ENCODER
	// Terminate any frame threads that are active
	for (i = 0; i < CODEC_GOP_LENGTH; i++)
	{
		//assert(encoder->frame_thread[i] == INVALID_HANDLE_VALUE);
		if (encoder->frame_thread[i] != INVALID_HANDLE_VALUE)
		{
			// Wait for the thread to finish
			WaitForSingleObject(encoder->frame_thread[i], INFINITE);

			// Indicate that the thread is no longer active
			encoder->frame_thread[i] = INVALID_HANDLE_VALUE;
		}
	}

	// Terminate any channel thread handles associated with frame processing
	for (i = 0; i < CODEC_GOP_LENGTH; i++)
	{
		int j;

		for (j = 0; j < CODEC_MAX_CHANNELS; j++)
		{
			if (encoder->frame_channel_thread[i][j] != INVALID_HANDLE_VALUE)
			{
				// Wait for the thread to finish
				WaitForSingleObject(encoder->frame_channel_thread[i][j], INFINITE);

				// Indicate that the thread is no longer active
				encoder->frame_channel_thread[i][j] = INVALID_HANDLE_VALUE;
			}
		}
	}

	// Terminate any channel thread handles associated with finishing the group
	for (i = 0; i < CODEC_MAX_CHANNELS; i++)
	{
		//assert(encoder->finish_channel_thread[i] == INVALID_HANDLE_VALUE);
		if (encoder->finish_channel_thread[i] != INVALID_HANDLE_VALUE)
		{
			// Wait for the thread to finish
			WaitForSingleObject(encoder->finish_channel_thread[i], INFINITE);

			// Indicate that the thread is no longer active
			encoder->finish_channel_thread[i] = INVALID_HANDLE_VALUE;
		}
	}
#endif

	// Free the data structures used for the wavelet transforms
	for (i = 0; i < num_transforms; i++)
	{
#if _ALLOCATOR
		ClearTransform(allocator, transform[i]);
#else
		ClearTransform(transform[i]);
#endif
	}

	// Free the data structures used by the bitstream
	ClearBitstream(output);

	// Free the data structures allocated within the encoder
	ClearEncoder(encoder);
}



void MoveInterleavedLine(uint8_t *data, uint8_t *t1, uint8_t *t2, uint8_t *done, int pitch, int height, int h)
{	
	int toline;

	if(done[h] == 0)
	{
		if(h & 1)
			toline = h/2;
		else
			toline = h/2 + height/2;

		if(toline != h)
		{
			memcpy(t2, data+toline*pitch, pitch);
			memcpy(data+toline*pitch, t1, pitch);

			done[h] = 1;

			MoveInterleavedLine(data, t2, t1, done, pitch, height, toline);
		}
		done[h] = 1;
	}
}


// Encode one frame of video

#if _RECURSIVE
bool EncodeSampleOld(ENCODER *encoder, LPBYTE data, int width, int height, int pitch, int format,
					 TRANSFORM *transform[], int num_transforms, BITSTREAM *output,
					 PIXEL *buffer, size_t buffer_size, int i_fixedquality, int fixedbitrate,
					 uint8_t* pPreviewBuffer, float framerate, custom_quant *custom)
#else
bool EncodeSample(ENCODER *encoder, uint8_t *data, int width, int height, int pitch, int format,
				  TRANSFORM *transform[], int num_transforms, BITSTREAM *output,
				  PIXEL *buffer, size_t buffer_size, int i_fixedquality, int fixedbitrate,
				  uint8_t* pPreviewBuffer, float framerate, custom_quant *custom)
#endif
{
	//FILE *logfile = encoder->logfile;
	bool result = true;
	bool first_frame = false;
	FRAME *frame;

#if DEBUG
	FILE *logfile = stdout;
#endif

	uint8_t *orig_data = data;
	//QUANTIZER *q = &encoder->q;
	int chroma_width = width/2;
	int chroma_offset = encoder->codec.chroma_offset;
	int transform_type = (encoder->gop_length > 1) ? TRANSFORM_TYPE_FIELDPLUS : TRANSFORM_TYPE_SPATIAL;
	int i, j;
	int display_height = height;
	int fixedquality = i_fixedquality;
	//int pixeldepth = pitch * 8 / width;
	int origformat = format;
	int rgbaswap = 0;
	int w_res_limit = 0;
	int h_res_limit = 0;
	int bayer_support = 0;
	int stereo3D_support = 0;
	int rgb444_support = 0;
	int bitdepth_limit = 8;
	int video_channels = 1; // 3D work
	int stereo_encode = 0;
	int limit_yuv = 0; // Canon 5d
	int conv_601_709 = 0; // Canon 5d
	int current_channel = 0;
	unsigned char *frame_base = (unsigned char *)data;
	ENCODER encoder_copy; // 3D work
	int encoded_format = encoder->encoded_format;
	
	//int unc_size = 0;
	int watermark = 0;
	int end_user_license = 0;

	CODEC_STATE *codec = &encoder->codec;

#if _ALLOCATOR
	ALLOCATOR *allocator = encoder->allocator;
#endif

#if (1 && SYSLOG)
	fprintf(stderr, "EncodeSample buffer address: 0x%p, size: %d\n", buffer, buffer_size);
#endif

	// Get the frame for storing the unpacked data
	frame = encoder->frame;
	assert(frame != NULL);


	if(pitch < 0) 
	{		
		data += (display_height - 1) * pitch;
		pitch = -pitch;
	}

#if DEBUG && 0
	{
		char t[100];
		sprintf(t,"encode format %d", format);
		OutputDebugString(t);
	}
#endif

	encoder->uncompressed = 0;
	if(	origformat == COLOR_FORMAT_V210 || 
		origformat == COLOR_FORMAT_RG30 || origformat == COLOR_FORMAT_R210 || origformat == COLOR_FORMAT_DPX0 ||
		origformat == COLOR_FORMAT_AR10 || origformat == COLOR_FORMAT_AB10 ||
		origformat == COLOR_FORMAT_BYR3 || origformat == COLOR_FORMAT_BYR4 || origformat == COLOR_FORMAT_BYR5)
	{
		int i,count = 0,target = 0, seed = 0;

		//i_fixedquality = fixedquality |= 16<<8; //CFEncode_Uncompressed16
		target = (fixedquality >> 8) & 0x1f;

		if(target > 0) // some frames are uncompressed
		{
			for(i=0; i<16; i++)
			{
				count += (encoder->unc_lastsixteen[i] ? 1 : 0);
				if(i)
				{
					encoder->unc_lastsixteen[i-1] = encoder->unc_lastsixteen[i];
				}
			}

			target += (target - count);
			if(target < 0)
				target=0;

			seed = *((unsigned int *)frame_base);
			if (encoder->metadata.global.block && encoder->metadata.global.size)
			{
				seed += calccrcA((uint8_t *)encoder->metadata.global.block, (int)encoder->metadata.global.size);
			}
			if (encoder->metadata.local.block && encoder->metadata.local.size)
			{
				seed += calccrcA((uint8_t *)encoder->metadata.local.block, (int)encoder->metadata.local.size);
			}
			srand(seed);

			if((rand()&15) < target)
			{
				encoder->uncompressed = 1;
				if((fixedquality >> 8) & 0x20) // skip
				{
					encoder->uncompressed = 3;
				}
				encoder->unc_origformat = origformat;
			}

			encoder->unc_lastsixteen[15] = encoder->uncompressed;
		}
	}

	if(encoder->uncompressed == 0 && (fixedquality & 0x1f00)) // trying for uncompressed with an illegal pixel format
	{
		fixedquality &= ~0x1fff;
		fixedquality |= 6;
	}

		
	encoder->encoder_quality = fixedquality;


	// For testing other colorspaces in VirtualDub
	//encoder->input.color_space |= COLOR_SPACE_VS_709;
#if _TIMING
	DoThreadTiming(2);
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Encode sample, width: %d, height: %d, format: %d\n", width, height, format);
	}
#endif

#if (1 && TRACE_PUTBITS)
	TraceEncodeFrame(encoder->frame_number, encoder->gop_length, width, height);
#endif

	// The transforms should have already been allocated
	assert(transform != NULL && transform[0] != NULL);


  	//allow all resolutions
	w_res_limit = 32768;
	h_res_limit = 32768;
	bitdepth_limit = 16;
	rgb444_support = 1;
	stereo3D_support = 1;
	bayer_support = 1;
	#if BAYER_SUPPORT
		w_res_limit /= 2;
		h_res_limit /= 2;
	#endif


	// Set up curve overrides, or 3D overrides via metadata
	if (encoder->metadata.global.block && encoder->metadata.global.size)
	{
		UpdateEncoderOverrides(encoder, (unsigned char *)encoder->metadata.global.block, (int)encoder->metadata.global.size);
	}

	if (encoder->metadata.local.block && encoder->metadata.local.size)
	{
		UpdateEncoderOverrides(encoder, (unsigned char *)encoder->metadata.local.block, (int)encoder->metadata.local.size);
	}

	if(encoder->video_channels <= 1 && encoder->ignore_database == 0 && encoder->ignore_overrides == 0) // not doing a double high encode
	{
		OverrideEncoderSettings(encoder); // psuedo 3D encoding, and encoder override.
										  //
										  //	Patch this to only do single channel encode when FCP starts up
										  //
		encoder->ignore_overrides = 0; // need to reset, as in may be accidientally sety within UpdateEncoderOverrides. 
									// The ignore_overrides flag in only to be used is set from global or local, to override 
									// read of the disk based override.colr file. 

		if(encoder->video_channels>1 && (format==COLOR_FORMAT_UYVY || format==COLOR_FORMAT_R408 || format==COLOR_FORMAT_V408))
		{
			// AJA patch - remove this to allow 3D capture of 2vuy
#if 0
			CFBundleRef			callerBundle;
			UInt32			pkgType;
			UInt32			pkgCreator;
			
			callerBundle = CFBundleGetMainBundle();
			CFBundleGetPackageInfo( callerBundle, &pkgType, &pkgCreator);
			if (pkgType== 0x4150504c && pkgCreator == 'KeyG') {		// 'APPL' 'KeyG' (FCP)
				encoder->video_channels = 1;
			}
#endif
		}
	}
	video_channels = encoder->video_channels; // 3D work
	limit_yuv = encoder->limit_yuv; // Canon 5d
	conv_601_709 = encoder->conv_601_709; //canon 5d

	if(video_channels == 2)
		stereo_encode = 1;

	if(encoded_format == 0)
		encoded_format = GetEncodedFormat(format, fixedquality, num_transforms);

	switch(encoded_format)
	{
		default:
		case ENCODED_FORMAT_YUV_422:
			frame->format = FRAME_FORMAT_YUV;
			frame->num_channels = 3;
			chroma_width = width / 2;
			break;
		case ENCODED_FORMAT_RGB_444:
			frame->format = FRAME_FORMAT_RGB;
			frame->num_channels = 3;
			chroma_width = width;
			break;
		case ENCODED_FORMAT_RGBA_4444:
			frame->format = FRAME_FORMAT_RGBA;
			frame->num_channels = 4;
			chroma_width = width;
			break;
		case ENCODED_FORMAT_BAYER:
			frame->format = FRAME_FORMAT_RGBA;
			frame->num_channels = 4;
			chroma_width = width;
			break;
	}



	if(encoded_format != ENCODED_FORMAT_YUV_422 && !rgb444_support)
	{
		if(end_user_license)
		{
			watermark |= 2;// no 444 support
		}
		else
		{	
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			return false;
		}
	}


	if(encoded_format == ENCODED_FORMAT_BAYER && !bayer_support)
	{
		if(end_user_license)
		{
			watermark |= 4;// no bayer support
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			return false;
		}
	}

	// Was the number of video channels set?
	if (video_channels == 0) {
		// Set the number of video channels to the default
		video_channels = 1;
	}

	// Check that the number of video channels was set
	assert(video_channels > 0);

	if(width > w_res_limit || height > h_res_limit*video_channels)
	{
		if(end_user_license)
		{
			watermark |= 8;// no >1080p support
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_SIZE;
			return false;
		}
	}

	if(video_channels>1 && !stereo3D_support)
	{
		if(end_user_license)
		{
			watermark |= 16;// no >3D support
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			return false;
		}
	}


	//Round the height up to the near multiple of 8.
	//Need for 1080 bayer encodes (as 540 per band is not divisible by 8)
	height += 7;
	height /= 8;
	height *= 8;

	// Check that the frame dimensions correspond to the dimensions used for the transform
	assert(width == transform[0]->width);
	assert(height == transform[0]->height);

	// Start compressing the frame (including conversion time)
	START(tk_compress);

	// Check that the frame dimensions are transformable
	assert(IsFrameTransformable(chroma_width, height, transform_type, encoder->num_spatial));

	/*
		Note: The transform is allocated and the fields are initialized when the encoder is
		initialized.  The dimensions and type of the transform is not chanaged during encoding.
		Not sure what will happen if the frame dimensions do not correspond to the dimensions
		used to initialize the transform data structure.  Probably should crop or fill the frame
		to the dimensions defined in the transform data structure when the packed frame provided
		as input to the encoder is converted to planes of luma and chroma channels.
	*/

	// Allocate a data structure for the unpacked frame
	if(!encoder->uncompressed)
		SetEncoderFormat(encoder, width, height, display_height, format, encoded_format);//DAN20080530


#if (0 && DEBUG)
	if (logfile)
	{
		int progressive = encoder->progressive;
		int gop_length = encoder->gop_length;
		int chromaFullRes = encoder->chromaFullRes;

		fprintf(logfile,
			"EncodeSample width: %d, height: %d, format: %d, progressive: %d, gop length: %d, full chroma: %d\n",
			width, height, format, progressive, gop_length, chromaFullRes);
	}
#endif

	if(video_channels > 1) // 3D work
	{
		encoder->current_channel = 0;

		if(encoder->preformatted3D)
		{
			uint32_t preFormatType = 0;
			encoder->video_channels = video_channels = 1; // 2D encode, but flag the 3D type
					
			switch(encoder->mix_type_value & 0xffff)
			{
				case 1: //stacked
					preFormatType = 1;
					break;
				case 2: //side-by-side
					preFormatType = 2;
					break;
				case 3: //fields					
					preFormatType = 1; // use stacked preformatting, reformat to match			
					//reformat here


					{
						int h;
						uint8_t *done = (uint8_t *)buffer;
						uint8_t *scratch = done + height;

						memset(done, 0, height);
						for(h=0; h<height; h++)
						{
							if(done[h] == 0)
							{
								memcpy(scratch, data+pitch*h, pitch);
								MoveInterleavedLine(data, scratch, scratch+pitch, done, pitch, height, h);
							}
						}
					}
					break;
				default:
					// unknown 3D type, encode as 2D
					preFormatType = 0; 
					break;
			}
			
			AddMetadata(&encoder->metadata.global, TAG_PREFORMATTED_3D, 'H', 4, (uint32_t *)&preFormatType);
			AddMetadata(&encoder->metadata.global, TAG_VIDEO_CHANNELS, 'H', 4, (uint32_t *)&video_channels);
		}

		memcpy(&encoder_copy, encoder, sizeof(ENCODER));
	}
	else
	{
		video_channels = 1;
	}

   do // 3D Work
   {
	// Convert the packed color to planes of YUV 4:2:2 (one byte per pixel)
	START(tk_convert);
	switch (origformat)
	{

	case COLOR_FORMAT_RGB24:
		if(encoded_format == ENCODED_FORMAT_RGB_444)
		{
			format = COLOR_FORMAT_RG48;
			encoder->codec.precision = CODEC_PRECISION_12BIT;
			fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
			encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;

			ConvertRGBtoRGB48(data, pitch, frame, (uint8_t *)buffer, encoder->codec.precision);
		}
		else if(encoded_format == ENCODED_FORMAT_YUV_422)
		{
			format = COLOR_FORMAT_YU64;
			encoder->codec.precision = CODEC_PRECISION_10BIT;
			fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
			encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;

			// Convert the packed RGB data to planes of YUV 4:2:2 (one byte per pixel)
			ConvertRGB32to10bitYUVFrame(data, pitch, frame, (uint8_t *)buffer, (int)buffer_size,
				encoder->input.color_space, encoder->codec.precision, false, 0);
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;

	case COLOR_FORMAT_NV12:
		{
			format = COLOR_FORMAT_YU64;
			encoder->codec.precision = CODEC_PRECISION_10BIT;
			fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
			encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;

			// Convert the packed RGB data to planes of YUV 4:2:2 (one byte per pixel)
			ConvertNV12to10bitYUVFrame(data, pitch, frame, (uint8_t *)buffer, (int)buffer_size,
				encoder->input.color_space, encoder->codec.precision, encoder->progressive);
		}
		break;

	case COLOR_FORMAT_YV12:
		{
			format = COLOR_FORMAT_YU64;
			encoder->codec.precision = CODEC_PRECISION_10BIT;
			fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
			encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;

			// Convert the packed RGB data to planes of YUV 4:2:2 (one byte per pixel)
			ConvertYV12to10bitYUVFrame(data, pitch, frame, (uint8_t *)buffer, (int)buffer_size,
				encoder->input.color_space, encoder->codec.precision, encoder->progressive);
		}
		break;

	case COLOR_FORMAT_QT32:				// QuickTime 'ARGB' with 8 bits per component
		rgbaswap = 1;
	case COLOR_FORMAT_BGRA:				// QuickTime 'BGRA' with 8 bits per component
	case COLOR_FORMAT_RGB32:
	case COLOR_FORMAT_RGB32_INVERTED:
		if(origformat == COLOR_FORMAT_RGB32_INVERTED || origformat == COLOR_FORMAT_QT32)
		{
			data += (display_height - 1) * pitch;
			pitch = -pitch;
		}

		if(encoded_format == ENCODED_FORMAT_RGB_444)
		{
			format = COLOR_FORMAT_RG48;
			encoder->codec.precision = CODEC_PRECISION_12BIT;
			fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
			encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;

			ConvertRGBAtoRGB48(data, pitch, frame, (uint8_t *)buffer, encoder->codec.precision, rgbaswap);
		} 
		else if(encoded_format == ENCODED_FORMAT_RGBA_4444)
		{
			format = COLOR_FORMAT_RG64; // if alpha is requested.

			encoder->codec.precision = CODEC_PRECISION_12BIT;
			fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
			encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;

			//Also does alpha range tweak
			ConvertRGBAtoRGBA64(data, pitch, frame, (uint8_t *)buffer, encoder->codec.precision, rgbaswap);
		}
		else if(encoded_format == ENCODED_FORMAT_YUV_422)
		{
			format = COLOR_FORMAT_YU64;
			encoder->codec.precision = CODEC_PRECISION_10BIT;
			fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
			encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
			// Convert the packed RGBA data to planes of YUV 4:2:2 (one byte per pixel)
			// The data in the alpha channel is not used
			ConvertRGB32to10bitYUVFrame(data, pitch, frame, (uint8_t *)buffer, (int)buffer_size,
				encoder->input.color_space, encoder->codec.precision, true, rgbaswap);
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;

	case COLOR_FORMAT_YUYV:
	case COLOR_FORMAT_UYVY:
		if (display_height != height)
		{
			size_t display_size = width * display_height * 2;
			size_t extended_size = width * (height - display_height) * 2;
			size_t frame_size = display_size + extended_size;
			char *tmp;

#if (1 && SYSLOG)
			fprintf(stderr, "Frame size: %d, buffer size: %d\n", frame_size, buffer_size);
#endif
			// Check that the temporary frame will fit in the scratch buffer
			assert(frame_size <= buffer_size);

			// Copy the displayed portion of the frame into the scratch buffer
			memcpy(buffer, data, display_size);
			tmp = (char *)buffer;
			tmp += display_size;

			// Fill the extra lines in the extended frame
			memset(tmp, 128, extended_size);
			tmp += extended_size;

			// Swap the input data with the scratch buffer
			//tmp = (char *)data;
			data = (uint8_t *)buffer;

			// Update the free space in the scratch buffer
			buffer = (PIXEL *)tmp;
			buffer_size -= frame_size;

			assert(buffer_size > 0);
#if (1 && SYSLOG)
			fprintf(stderr, "EncodeSample new buffer address: 0x%p, size: %d\n", buffer, buffer_size);
#endif
		}

		//10-bit for everyone
		codec->precision = CODEC_PRECISION_10BIT;
		break;

	case COLOR_FORMAT_B64A: // now allowed in AHD foir AE support
		
		if(encoded_format == ENCODED_FORMAT_RGB_444)
		{
			// Convert to three planes of RGB with 4:4:4 sampling and 12 bit precision
			codec->precision = CODEC_PRECISION_12BIT;
			//Does alpha range tweak
			ConvertBGRA64ToFrame_4444_16s(data, pitch, frame, (uint8_t *)buffer, codec->precision);
		}
		else if(encoded_format == ENCODED_FORMAT_RGBA_4444)
		{
			// Convert to three planes of RGB with 4:4:4 sampling and 12 bit precision
			codec->precision = CODEC_PRECISION_12BIT;
			//Does alpha range tweak
			ConvertBGRA64ToFrame_4444_16s(data, pitch, frame, (uint8_t *)buffer, codec->precision);
		}
		else if(encoded_format == ENCODED_FORMAT_YUV_422)
		{
			//TODO: Need to change the conversion routine to handle 12 bit precision

			// Convert to three planes of YUV with 4:2:2 sampling and 10 bit precision
			codec->precision = CODEC_PRECISION_10BIT;
			ConvertAnyDeep444to422(data, pitch, frame, (uint8_t *)buffer, encoder->input.color_space, origformat);

			format = COLOR_FORMAT_YU64;
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		// The encoder should not have been initialized for full resolution chroma
		break;

	case COLOR_FORMAT_V210:
		if(bitdepth_limit >= 10)
		{
			if(encoder->uncompressed)
			{
				// uncompressed
				encoder->unc_buffer = (uint8_t *)buffer;
				encoder->unc_data = data;
				encoder->unc_pitch = pitch;
				memcpy(&encoder->unc_frame, frame, sizeof(FRAME));
			}
			else
			{
				// Convert the packed 10-bit YUV 4:2:2 to planes of 8-bit YUV
				ConvertV210ToFrame16s(data, pitch, frame, (uint8_t *)buffer);
				codec->precision = CODEC_PRECISION_10BIT;
			}
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;

	case COLOR_FORMAT_YU64:
		if(bitdepth_limit >= 10)
		{
			// Convert the unpacked 64/16-bit YUV 4:2:2 to planes of 16-bit YUV
			ConvertYU64ToFrame16s(data, pitch, frame, (uint8_t *)buffer);
			encoder->codec.precision = CODEC_PRECISION_10BIT;
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;

	case COLOR_FORMAT_BYR1:
		if(bayer_support)
		{
			// Convert the unpacked 64/16-bit YUV 4:2:2 to planes of 16-bit YUV
			encoder->codec.precision = CODEC_PRECISION_10BIT;
			ConvertBYR1ToFrame16s(encoder->bayer.format, data, pitch, frame, (uint8_t *)buffer);
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;

	case COLOR_FORMAT_BYR2:
		if(bayer_support)
		{
			// Convert the unpacked 64/16-bit YUV 4:2:2 to planes of 16-bit YUV
			ConvertBYR2ToFrame16s(data, pitch, frame, (uint8_t *)buffer);
			encoder->codec.precision = CODEC_PRECISION_10BIT;
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;

	case COLOR_FORMAT_BYR3: // now handled directly for speed.
		if(bayer_support)
		{
			if(encoder->uncompressed)
			{
				// uncompressed
				//unc_size = ConvertBYR3ToPacked(data, pitch, frame, (uint8_t *)buffer);
				encoder->unc_buffer = (uint8_t *)buffer;
				encoder->unc_data = data;
				encoder->unc_pitch = pitch;
				memcpy(&encoder->unc_frame, frame, sizeof(FRAME));

				//unc_size = 3 * width * 4 * display_height / 2;
			}
			else
			{
				// Convert the planer 16-bit Bayer to planes of 16-bit YUV
				#if !FAST_BYR3
				ConvertBYR3ToFrame16s(data, pitch, frame, (uint8_t *)buffer);
				// DAN 20060125 use odd preprocess BYR3 and the TransformForwardSpatialBYR3 in broken (arfitacts.)
				#endif
			}
			encoder->codec.precision = CODEC_PRECISION_10BIT;
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;


	case COLOR_FORMAT_BYR4:
		if(bayer_support)
		{
			if(encoder->uncompressed)
			{
				// uncompressed
				encoder->unc_buffer = (uint8_t *)buffer;
				encoder->unc_data = data;
				encoder->unc_pitch = pitch;

				encoder->codec.precision = CODEC_PRECISION_12BIT;
				if(encoder->encode_curve_preset == 0)
					AddCurveToUncompressedBYR4(encoder->encode_curve, encoder->encode_curve_preset,
						data, pitch, frame);
				memcpy(&encoder->unc_frame, frame, sizeof(FRAME));
			}
			else
			{
				// Convert the unpacked 64/16-bit YUV 4:2:2 to planes of 16-bit YUV
				encoder->codec.precision = CODEC_PRECISION_12BIT;
				i_fixedquality |= (3 << 25); // CFEncode_RGB_Quality_Mask , DAN20080110 prevent increased quant on channels 1-3
				ConvertBYR4ToFrame16s(encoder->bayer.format, encoder->encode_curve, encoder->encode_curve_preset, data, pitch, frame, encoder->codec.precision);
			}
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;

	case COLOR_FORMAT_BYR5:
		if(bayer_support)
		{
			if(encoder->uncompressed)
			{
				// uncompressed
				encoder->unc_buffer = (uint8_t *)buffer;
				encoder->unc_data = data;
				encoder->unc_pitch = pitch;
				memcpy(&encoder->unc_frame, frame, sizeof(FRAME));
			}
			else
			{

				uint8_t *scratch = (uint8_t *)encoder->linebuffer;
				if(scratch == NULL)
				{
				#if _ALLOCATOR
					encoder->linebuffer = scratch = (uint8_t *)AllocAligned(allocator, pitch*2, _CACHE_LINE_SIZE);
				#else
					encoder->linebuffer = scratch = (uint8_t *)MEMORY_ALIGNED_ALLOC(pitch*2, _CACHE_LINE_SIZE);
				#endif
				}

				// Convert the unpacked 64/16-bit YUV 4:2:2 to planes of 16-bit YUV
				encoder->codec.precision = CODEC_PRECISION_12BIT;
				i_fixedquality |= (3 << 25); // CFEncode_RGB_Quality_Mask , DAN20080110 prevent increased quant on channels 1-3
				ConvertBYR5ToFrame16s(encoder->bayer.format, data, pitch, frame, (uint8_t *)scratch);
			}
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;

	case COLOR_FORMAT_R4FL:		// QuickTime 'r4fl' floating-point 4:4:4:4
		if(encoded_format == ENCODED_FORMAT_RGB_444)
		{
			// Convert to three planes of RGB with 4:4:4 sampling and 12 bit precision
			codec->precision = CODEC_PRECISION_12BIT;
			ConvertYUVAFloatToFrame_RGB444_16s(data, pitch, frame, (uint8_t *)buffer);
		}
		else if(encoded_format == ENCODED_FORMAT_RGBA_4444)
		{
			codec->precision = CODEC_PRECISION_12BIT;
			ConvertYUVAFloatToFrame_RGBA4444_16s(data, pitch, frame, (uint8_t *)buffer);
		}
		else if(encoded_format == ENCODED_FORMAT_YUV_422)
		{
			//TODO: Need to change the conversion routine to handle 12 bit precision

			// Convert to three planes of YUV with 4:2:2 sampling and 10 bit precision
			codec->precision = CODEC_PRECISION_10BIT;
			ConvertYUVAFloatToFrame16s(data, pitch, frame, (uint8_t *)buffer);

			format = COLOR_FORMAT_YU64;
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;

	case COLOR_FORMAT_R408:		// QuickTime 'r408' 8-bit AYUV to 16bit 4:2:2
	case COLOR_FORMAT_V408:		// QuickTime 'r408' 8-bit UYVA to 16bit 4:2:2
		{
		/*	if(encoder->chromaFullRes)
			{
				// Convert to three planes of RGB with 4:4:4 sampling and 12 bit precision
				codec->precision = CODEC_PRECISION_12BIT;
				//ConvertYUVAFloatToFrame_RGB444_16s(data, pitch, frame, (uint8_t *)buffer);
			}
			else*/
			{
				//TODO: Need to change the conversion routine to handle 12 bit precision

				// Convert to three planes of YUV with 4:2:2 sampling and 10 bit precision
				codec->precision = CODEC_PRECISION_10BIT;
				ConvertYUVAToFrame16s(data, pitch, frame, (uint8_t *)buffer, origformat);
			}
		}
		break;

	case COLOR_FORMAT_RG64:
		if(encoded_format == ENCODED_FORMAT_RGB_444)
		{
			int alpha = 0;
			codec->precision = CODEC_PRECISION_12BIT;
			ConvertRGBA64ToFrame16s(data, pitch, frame, (uint8_t *)buffer, codec->precision, origformat, alpha);
		}
		else if(encoded_format == ENCODED_FORMAT_RGBA_4444)
		{
			int alpha = 1;
			codec->precision = CODEC_PRECISION_12BIT;
			ConvertRGBA64ToFrame16s(data, pitch, frame, (uint8_t *)buffer, codec->precision, origformat, alpha);
		}
		else if(encoded_format == ENCODED_FORMAT_YUV_422)
		{
			//TODO: Need to change the conversion routine to handle 12 bit precision

			// Convert to three planes of YUV with 4:2:2 sampling and 10 bit precision
			codec->precision = CODEC_PRECISION_10BIT;
			ConvertAnyDeep444to422(data, pitch, frame, (uint8_t *)buffer, encoder->input.color_space, origformat);

			format = COLOR_FORMAT_YU64;
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;

	case COLOR_FORMAT_RG48:
		if(encoded_format == ENCODED_FORMAT_RGB_444)
		{
			codec->precision = CODEC_PRECISION_12BIT;
			ConvertRGB48ToFrame16s(data, pitch, frame, (uint8_t *)buffer, codec->precision, origformat);
		}
		else if(encoded_format == ENCODED_FORMAT_YUV_422)
		{
			//TODO: Need to change the conversion routine to handle 12 bit precision

			// Convert to three planes of YUV with 4:2:2 sampling and 10 bit precision
			codec->precision = CODEC_PRECISION_10BIT;
			ConvertAnyDeep444to422(data, pitch, frame, (uint8_t *)buffer, encoder->input.color_space, origformat);

			format = COLOR_FORMAT_YU64;
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;


	case COLOR_FORMAT_RG30:
	case COLOR_FORMAT_R210:
	case COLOR_FORMAT_DPX0:
	case COLOR_FORMAT_AB10:
	case COLOR_FORMAT_AR10:
		if(encoded_format == ENCODED_FORMAT_RGB_444)
		{
			if(encoder->uncompressed)
			{
				// uncompressed
				encoder->unc_buffer = (uint8_t *)buffer;
				encoder->unc_data = data;
				encoder->unc_pitch = pitch;
				memcpy(&encoder->unc_frame, frame, sizeof(FRAME));
			}
			else
			{
				codec->precision = CODEC_PRECISION_12BIT;
			#if !FAST_RG30
				ConvertRGBA64ToFrame16s(data, pitch, frame, (uint8_t *)buffer, codec->precision, origformat, 0 /*alpha*/);
			#endif
			}
		}
		else if(encoded_format == ENCODED_FORMAT_YUV_422)
		{
			//TODO: Need to change the conversion routine to handle 12 bit precision

			// Convert to three planes of YUV with 4:2:2 sampling and 10 bit precision
			codec->precision = CODEC_PRECISION_10BIT;
			ConvertAnyDeep444to422(data, pitch, frame, (uint8_t *)buffer, encoder->input.color_space, origformat);

			format = COLOR_FORMAT_YU64;
		}
		else
		{
			encoder->error = CODEC_ERROR_INVALID_FORMAT;
			assert(0);
		}
		break;


	case COLOR_FORMAT_CbYCrY_10bit_2_8:
		ConvertCbYCrY_10bit_2_8ToFrame16s(data, pitch, frame, (uint8_t *)buffer, codec->precision, 0 /*alpha*/);
		codec->precision = CODEC_PRECISION_10BIT;
		break;

	case COLOR_FORMAT_CbYCrY_16bit_2_14:
		ConvertCbYCrY_16bit_2_14ToFrame16s(data, pitch, frame, (uint8_t *)buffer, codec->precision, 0 /*alpha*/);
		codec->precision = CODEC_PRECISION_10BIT;
		break;

	case COLOR_FORMAT_CbYCrY_16bit_10_6:
		ConvertCbYCrY_16bit_10_6ToFrame16s(data, pitch, frame, (uint8_t *)buffer, codec->precision, 0 /*alpha*/);
		codec->precision = CODEC_PRECISION_10BIT;
		break;

	case COLOR_FORMAT_CbYCrY_8bit:
		ConvertCbYCrY_8bitToFrame16s(data, pitch, frame, (uint8_t *)buffer, codec->precision, 0 /*alpha*/);
		codec->precision = CODEC_PRECISION_10BIT;
		break;

	case COLOR_FORMAT_CbYCrY_16bit:
		ConvertCbYCrY_16bitToFrame16s(data, pitch, frame, (uint8_t *)buffer, codec->precision, 0 /*alpha*/);
		codec->precision = CODEC_PRECISION_10BIT;
		break;

	default:
		// Cannot handle this color format
		encoder->error = CODEC_ERROR_INVALID_FORMAT;
		return false;
	}
	STOP(tk_convert);


	if(encoder->error)
		return false;

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Quantization fixed quality: %d, fixed bitrate: %d\n", fixedquality, fixedbitrate);
	}
#endif

	// Set the quantization parameters unless the sample is uncompressed
	if (!encoder->uncompressed) {
		SetEncoderQuantization(encoder, format, i_fixedquality, fixedbitrate, custom);
	}

#if (0 && DEBUG)
	if (logfile) {
		PrintQuantizer(&encoder->q, logfile);
	}
#endif

	// Is this the first frame in the GOP?
	if (encoder->group.count == 0)
	{
		int channel;

		// Set the quantization and prescaling for the transforms in this group
		if(!encoder->uncompressed)
		{
			for (channel = 0; channel < num_transforms; channel++)
			{
				// Set the prescaling (may be used in setting the quantization)
				SetTransformPrescale(transform[channel], transform_type, codec->precision);

				// Set the quantization
				SetTransformQuantization(encoder, transform[channel], channel, framerate);
			}
		}
	}

#if (0 && DEBUG)
	if (logfile)
	{
		int k;

		for (k = 0; k < num_transforms; k++)
		{
			fprintf(logfile, "Prescale for channel: %d\n", k);
			PrintTransformPrescale(transform[k], logfile);
			fprintf(logfile, "\n");

			fprintf(logfile, "Quantization for channel: %d\n", k);
			PrintTransformQuantization(transform[k], logfile);
			fprintf(logfile, "\n");
		}
	}
#endif

	// Is this the first frame in the video sequence?
	if (encoder->no_video_seq_hdr == 0 && encoder->frame_count == 0 && encoder->group.count == 0 && encoder->gop_length > 1)
	{
		// Note: Do not write out the video sequence header when encoding one frame groups

		// Fill the first sample with the video sequence header
	//	result = EncodeFirstSample(encoder, transform, num_transforms, frame, output, format);
	//	if (!result) goto finish;

		encoder->output.iskey = true;

		first_frame = true;
	}

	// Determine the index of this frame in the group
	j = encoder->group.count;

	// Should be the first or second frame in a two frame group
	assert(0 <= j && j <= 1);

	// Set the number of channels in the encoder quantization table
	encoder->num_quant_channels = num_transforms;

	// Which wavelet transform should be used at the lowest level:
	// frame transform (interlaced) or spatial transform (progressive)
	if (!encoder->progressive)
	{
		int frame_index = j;

#if _NEW_DECODER
		// Interlaced frame encoding (implemented using the frame transform)
		codec->progressive = 0;
#endif

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Transform forward frame: %d, progressive: %d\n", frame_index, encoder->progressive);
		}
#endif

		if (format == COLOR_FORMAT_YUYV || format == COLOR_FORMAT_UYVY)
		{
			FRAME_INFO info;
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			InitFrameInfo(&info, width, height, format);

			// Apply the frame transform directly to the frame
			TransformForwardFrameYUV(data, pitch, &info, transform, frame_index, num_transforms,
									(char *)buffer, buffer_size, chroma_offset, codec->precision, limit_yuv, conv_601_709);
		}
		else
		{
			// Apply the frame wavelet transform to each plane
			for (i = 0; i < num_transforms; i++)
			{
				//int k;

				IMAGE *image = frame->channel[i];
				IMAGE *wavelet = transform[i]->wavelet[j];

				// The lowpass band must be one byte pixels
				//assert(image->pixel_type[0] == PIXEL_TYPE_8U);

				// Apply the frame transform to the image plane for this channel
				TransformForwardFrame(image, wavelet, buffer, buffer_size, chroma_offset, wavelet->quant);

#if (0 && DEBUG)
				if (logfile) {
					char label[_MAX_PATH];
					int band;

					sprintf(label, "Frame transform, channel: %d", i);
					DumpImageStatistics(label, wavelet, logfile);
#if 1
					for (band = 1; band < wavelet->num_bands; band++)
					{
						sprintf(label, "Frame transform, band: %d", band);
						DumpBandStatistics(label, wavelet, band, logfile);
					}
#endif
				}
#endif

#if (0 && DEBUG)
				for (k = 0; k < CODEC_MAX_BANDS; k++)
				{
					int static count = 0;
					if (count < 20) {
						char label[_MAX_PATH];
						sprintf(label, "Frame%dc%db%d-encode-%d-", j, i, k, count);
						if (k == 0) DumpPGM(label, wavelet, NULL);
						else DumpBandPGM(label, wavelet, k, NULL);
					}
					count++;
				}
#endif
			}
		}
	}
	else
	{
		int frame_index = j;

#if _NEW_DECODER
		// Progressive frame transform (implemented using the spatial transform)
		codec->progressive = 1;
#endif

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Transform forward spatial: %d, progressive: %d\n", frame_index, encoder->progressive);
		}
#endif

#if (0 && _THREADED_ENCODER)

		if (format == COLOR_FORMAT_YUYV)
		{
			FRAME_INFO info = {width, height, format};
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			// Is this the first frame in the group?
			if (encoder->group.count == 0)
			{
				//DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;
#if 1
				// Apply the first level transform to the first frame in the group
				TransformForwardSpatialYUVThreaded(encoder, data, pitch, &info, transform, frame_index,
												num_transforms, buffer, buffer_size, chroma_offset);
#else
				// Apply the first level transform to the first frame in the group
				TransformForwardSpatialYUVPlanarThreaded(encoder, data, pitch, &info, transform, frame_index,
														num_transforms, buffer, buffer_size, chroma_offset);
#endif
				// Wait for the first frame transform to finish
				//WaitForSingleObject(encoder->frame_thread[0], dwTimeout);

				// Signal that the thread as ended
				//encoder->frame_thread[0] = INVALID_HANDLE_VALUE;
			}
			else
			{
				DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;
				const int thread_frame_index = 0;

				// Use a portion of the buffer for each frame transform
				buffer_size /= CODEC_GOP_LENGTH;
				buffer += frame_index * buffer_size / sizeof(PIXEL);

				buffer = (PIXEL *)ALIGN(buffer, _CACHE_LINE_SIZE);

				// Apply the first level transform to the second frame in the group
				TransformForwardSpatialYUV(data, pitch, &info, transform, frame_index, num_transforms,
										buffer, buffer_size, chroma_offset, true, codec->precision, limit_yuv, conv_601_709);

				// Is a thread running on the other frame?
				if (encoder->frame_thread[thread_frame_index] != INVALID_HANDLE_VALUE)
				{
					// Wait for the first frame transform to finish
					WaitForSingleObject(encoder->frame_thread[thread_frame_index], dwTimeout);

					// Signal that the thread has ended
					encoder->frame_thread[thread_frame_index] = INVALID_HANDLE_VALUE;
				}
			}
		}

#else
		if (format == COLOR_FORMAT_YUYV || format == COLOR_FORMAT_UYVY)
		{
			FRAME_INFO info;
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			InitFrameInfo(&info, width, height, format);

#if (0 && DEBUG)
			if (logfile) {
				char label[_MAX_PATH];
				sprintf(label, "Input");
				DumpBufferStatistics(label, data, width, height, pitch, logfile);
			}
#endif
#if (0 && DEBUG)
			if (logfile) {
				fprintf(logfile, "TransformForwardSpatialYUV, format: %d, precision: %d\n", info.format, codec->precision);
			}
#endif
			// Apply the frame transform directly to the frame
			TransformForwardSpatialYUV(data, pitch, &info, transform, frame_index, num_transforms,
									buffer, buffer_size, chroma_offset, false, codec->precision, limit_yuv, conv_601_709);
		}
#endif

#if FAST_BYR3
		else if (format == COLOR_FORMAT_BYR3)
		{
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			if(encoder->uncompressed)
			{
				//uncompressed needs no wavelet operations
			}
			else
			{
				FRAME_INFO info;

				InitFrameInfo(&info, width, height, format);

				// Apply the frame transform directly to the frame
				TransformForwardSpatialBYR3(data, pitch, &info, transform, frame_index, num_transforms,
									buffer, buffer_size, chroma_offset, false, display_height);
			}
		}
#endif
		else if ((format == COLOR_FORMAT_V210 || format == COLOR_FORMAT_BYR4 || format == COLOR_FORMAT_BYR5) && encoder->uncompressed)
		{
			//uncompressed needs no wavelet operations
		}
#if FAST_RG30
		else if(format == COLOR_FORMAT_RG30 ||
				format == COLOR_FORMAT_R210 ||
				format == COLOR_FORMAT_DPX0 ||
				format == COLOR_FORMAT_AR10 ||
				format == COLOR_FORMAT_AB10)
		{
			if(encoder->uncompressed)
			{
				//uncompressed needs no wavelet operations
			}
			else
			{
				FRAME_INFO info;

				InitFrameInfo(&info, width, height, format);

				// Apply the frame transform directly to the frame
				TransformForwardSpatialRGB30(data, pitch, &info, transform, frame_index, num_transforms,
									buffer, buffer_size, chroma_offset, false, display_height, codec->precision, origformat);
			}
		}
#endif
		else
		{
			// Apply the spatial wavelet transform to each plane
			for (i = 0; i < num_transforms; i++)
			{
				//int k;

				IMAGE *image = frame->channel[i];
				IMAGE *wavelet = transform[i]->wavelet[j];
				const int band = 0;
				const int level = 1;

				// The lowpass band must be one byte pixels
				//assert(image->pixel_type[0] == PIXEL_TYPE_8U);

				// Apply the spatial transform to the image plane for this channel
#if _ALLOCATOR
				TransformForwardSpatial(allocator, image, band, wavelet, level,
										buffer, buffer_size, 0, wavelet->quant, 0);
#else
				TransformForwardSpatial(image, band, wavelet, level,
										buffer, buffer_size, 0, wavelet->quant, 0);
#endif

#if (0 && DEBUG)
				if (logfile) {
					char label[_MAX_PATH];
					int channel = i;
					int band;

					sprintf(label, "Spatial transform, channel: %d", channel);
					DumpImageStatistics(label, wavelet, logfile);
#if 1
					for (band = 1; band < wavelet->num_bands; band++)
					{
						sprintf(label, "Spatial transform, band: %d", band);
						DumpBandStatistics(label, wavelet, band, logfile);
					}
#endif
				}
#endif
			}
		}

#if TIMING
		// Count the number of progressive frames that were encoded
		progressive_encode_count++;
#endif
	}

	if(first_frame)
	{
		EncodeFirstSample(encoder, transform, num_transforms, frame, output, format);
	}

	// Increment the count of the number of frames in the group
	encoder->group.count++;

	// Is this encoded sample an intra frame?
	if (encoder->gop_length == 1)
	{
		if(encoder->uncompressed)
		{
			//uncompressed
			// Encode the transform for the current frame
			EncodeQuantizedGroup(encoder, transform, num_transforms, output);
		}
		else
		{
#if (0 && DEBUG)
			// Select the wavelet bands to dump to a file (for debugging)
			uint32_t channel_mask = 0x0F;
			uint32_t wavelet_mask = 0x07;
			uint32_t wavelet_band_mask = 0x0F;
			const char *pathname = "C:/Users/bschunck/Temp/encoder1.dat";
			//const char *pathname = "C:/Users/bschunck/Temp/highpass1.dat";
#endif
			// Compute the spatial transform wavelet tree for each channel
			ComputeGroupTransformQuant(encoder, transform, num_transforms);

#if (0 && DEBUG)
			// Dump selected wavelet bands to a file (for debugging)
			if (encoder->frame_count == 0) {
				WriteTransformBandFile(transform, num_transforms, channel_mask, wavelet_mask, wavelet_band_mask, pathname);
			}
#endif
			// Encode the transform for the current frame
			EncodeQuantizedGroup(encoder, transform, num_transforms, output);
		}

		//DAN Variable Bit Rate control feedback.
		//encoder->lastgopbitcount = output->nWordsUsed * 8;//output->cntBits;
		//output->cntBits = 0;

		// Reset the group of frames
		encoder->group.count = 0;

		// Update the frame count
		encoder->frame_count += encoder->gop_length;

		// Mark this frame as a key frame since it is an intra frame
		frame->iskey = true;
		encoder->output.iskey = true;
	}
	else
	// Enough frames to compute the rest of the wavelet transform?
	if (encoder->group.count == encoder->gop_length)
	{
		//int channel;

		// Copy encoder parameters into the transform data structure
		//int gop_length = encoder->gop_length;
		//int num_spatial = encoder->num_spatial;

		extern void OutputRGB(unsigned char *buffer, IMAGE *waveletY, IMAGE *waveletU, IMAGE *waveletV, int scale);

		if(pPreviewBuffer)
		{
			int scale = 4;
			int level;
			int valuescale;

			switch(scale)
			{
			case 2:
				level = 2;
				valuescale = 3;
				break;
			case 4:
				level = 4;
				valuescale = 5;
				break;
			case 8:
			default:
				level = 5;
				valuescale = 7;
				break;
			}

			// TODO: lock preview buffer
			OutputRGB(pPreviewBuffer, transform[0]->wavelet[level], transform[1]->wavelet[level], transform[2]->wavelet[level], valuescale);
			// TODO: unlock preview buffer
			// TODO: post preview msg
		}

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Before compute group transform\n");
		}
#endif

		ComputeGroupTransformQuant(encoder, transform, num_transforms);

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Before encode quantized group\n");
		}
#endif

		// Encode the transform for the current group of frames
		EncodeQuantizedGroup(encoder, transform, num_transforms, output);

		//DAN Variable Bit Rate control feedback.
		//encoder->lastgopbitcount = output->nWordsUsed * 8;//output->cntBits;
		//output->cntBits = 0;

		// Reset the group of frames
		encoder->group.count = 0;

		// Update the frame count
		encoder->frame_count += encoder->gop_length;

		// Mark this frame as a key frame since it is the start of a group
		frame->iskey = true;
		encoder->output.iskey = true;

#if (0 && DEBUG)
		if (logfile) {
#if 0
			fprintf(logfile, "Encoded transforms (all channels):\n\n");
			for (channel = 0; channel < num_transforms; channel++)
#else
			channel = 0;
			fprintf(logfile, "Encoded transforms, channel %d:\n\n", channel);
#endif
			{
				char label[256];
				int row = 1;

				sprintf(label, "Channel %d wavelets", channel);
				DumpTransform(label, transform[channel], row, logfile);
				fprintf(logfile, "\n");
			}
		}
#endif
	}
	else	// Waiting for enough frames to complete a group
	{
		// Is this the first frame in the video sequence?
		if (first_frame)
		{
			// Mark this frame as a key frame since it is the start of the sequence
			frame->iskey = true;
			encoder->output.iskey = true;
		}
		else
		{
			int width = frame->width;
			int height = frame->height;
			int group_index = encoder->group.count;
			int frame_number = encoder->frame_number;
			int encoded_format = encoder->encoded_format;

			// Increment the frame sequence number
			encoder->frame_number++;

			PutVideoFrameHeader(output, FRAME_TYPE_PFRAME, width, height, display_height, group_index,
                                frame_number, encoded_format, encoder->presentationWidth, encoder->presentationHeight);

			// Update the frame count
			//encoder->frame_count++;

			// This frame is not a key frame
			frame->iskey = false;
			encoder->output.iskey = false;
		}
	}

//finish:

	// Force output of any bits pending in the bitstream buffer
	if(stereo_encode)
		FlushBitstreamAlign(output, 16);
	else
		FlushBitstream(output);



	if (frame->iskey && !encoder->uncompressed) {
		encoder->lastgopbitcount = output->nWordsUsed * 8;	//output->cntBits;
	}

	video_channels--;
	if(video_channels > 0)
	{
		memcpy(encoder, &encoder_copy, sizeof(ENCODER));
		if(!encoder->preformatted3D) //double height full frame 3D encode
		{
			if(pitch < 0)
			{
				//RGBa or something
				data = orig_data;
				pitch = -pitch;
				data += pitch * (display_height + encoder->video_channel_gap);
			}
			else
			{
				data = orig_data;
				data += pitch * (display_height + encoder->video_channel_gap);
			}
		}
		current_channel++;
		encoder->current_channel = current_channel;
	}
   } while(video_channels > 0);
   
	if ((encoder->thumbnail_generate >= 1 && encoder->thumbnail_generate <= 3) || watermark)
	{
		GenerateThumbnail((void*)output->lpCurrentBuffer, (size_t)output->nWordsUsed, 
			(void*)output->lpCurrentWord, (size_t)(output->dwBlockLength - output->nWordsUsed),
			encoder->thumbnail_generate | (watermark<<8), NULL, NULL, NULL);
	}

	STOP(tk_compress);

#if (0 && DEBUG)
	if (logfile) {
		CODEC_ERROR error = encoder->error;
		fprintf(logfile, "Returning from encode sample, result: %d, error: %d\n", result, error);
	}
#endif

#if _TIMING
	DoThreadTiming(3);
#endif

	return result;
}

bool EncodeFirstSample(ENCODER *encoder, TRANSFORM *transform[], int num_transforms,
					   FRAME *frame, BITSTREAM *output, int input_format)
{
	int major = CODEC_VERSION_MAJOR;
	int minor = CODEC_VERSION_MINOR;
	int revision = CODEC_VERSION_REVISION;
	uint32_t flags = 0;
	int width = frame->width;
	int height = frame->height;
	int display_height = frame->display_height;
	int format = frame->format;

	// Internal representation of the encoded data
	int encoded_format = encoder->encoded_format;

	// Indicate unused variables
	(void) transform;
	(void) num_transforms;

	// Code only works for gop length of two frames
	//assert(encoder->gop_length == CODEC_GOP_LENGTH);

	// This should be the first video sample in the group
	assert(encoder->group.count == 0);
	encoder->group.count = 0;

	// Output the video sequence header
	if (RUNS_ROWEND_MARKER) flags |= SEQUENCE_FLAGS_RUNROWEND;
	PutVideoSequenceHeader(output, major, minor, revision, flags, width, height, display_height,
                           format, input_format, encoded_format, encoder->presentationWidth, encoder->presentationHeight);

	// Set the error code in the encoder if a bitstream error occurred
	if (output->error != BITSTREAM_ERROR_OKAY) {
		encoder->error = CODEC_ERROR_BITSTREAM;
		return false;
	}

	//rdoherty fix -- important to DirectShow that the first frame appear to be a keyframe
	encoder->output.iskey = true;

	return true;
}



#if _RECURSIVE

// Initialize the codec state used during encoding
void InitEncoderCodecState(ENCODER *encoder, CODEC_STATE *codec)
{
	if (!encoder->progressive)
	{
		// The first level uses an interlaced transform
		codec->progressive = 0;
	}
	else
	{
		// Progressive frame transform (implemented using the spatial transform)
		codec->progressive = 1;
	}
}

char *AllocateAndCopyExtendedFrame(char *frame, int width, int height, int pitch,
								   int display_height, SCRATCH *scratch)
{
	char *buffer;
	char *extension;
	size_t buffer_size;
	size_t display_size;
	size_t extension_size;

	// Do not need to extend the frame if the processed height fits the display height
	if (height <= display_height) {
		return frame;
	}

	// Allocate space for the extended frame
	buffer_size = height * pitch;
	buffer = AllocScratchBuffer(scratch, buffer_size);

	// Copy the original frame into the extended frame
	display_size = display_height * pitch;
	memcpy(buffer, frame, display_size);

	// Clear the extension
	extension = buffer + display_size;
	extension_size = (height - display_height) * pitch;
	memset(buffer, 128, extension_size);

	// Return the new frame
	return buffer;
}

bool EncodeFirstSampleRecursive(ENCODER *encoder, BITSTREAM *output,
								TRANSFORM *transform[], int num_transforms,
								int width, int height, int display_height, int format,
								int input_format, int encoded_format)
{
	int major = CODEC_VERSION_MAJOR;
	int minor = CODEC_VERSION_MINOR;
	int revision = CODEC_VERSION_REVISION;
	uint32_t flags = 0;
	//int width = frame->width;
	//int height = frame->height;
	//int format = frame->format;

	// Code only works for gop length of two frames
	//assert(encoder->gop_length == CODEC_GOP_LENGTH);

	// This should be the first video sample in the group
	assert(encoder->group.count == 0);
	encoder->group.count = 0;

	// Output the video sequence header
	if (RUNS_ROWEND_MARKER) flags |= SEQUENCE_FLAGS_RUNROWEND;
	PutVideoSequenceHeader(output, major, minor, revision, flags, width, height, display_height,
							format, input_format, encoded_format);

	// Set the error code in the encoder if a bitstream error occurred
	if (output->error != BITSTREAM_ERROR_OKAY) {
		encoder->error = CODEC_ERROR_BITSTREAM;
		return false;
	}

	//rdoherty fix -- important to DirectShow that the first frame appear to be a keyframe
	encoder->output.iskey = true;

	return true;
}

// Encode one frame of video using recursive wavelet transforms if possible
bool EncodeSampleRecursive(ENCODER *encoder, uint8_t * data, int width, int height, int pitch, int format,
						   TRANSFORM *transform[], int num_transforms, BITSTREAM *output,
						   SCRATCH *scratch, int i_fixedquality, int fixedbitrate,
						   uint8_t* pPreviewBuffer, float framerate, custom_quant *custom)
{
	FILE *logfile = encoder->logfile;
	CODEC_STATE *codec = &encoder->codec;
	QUANTIZER *q = &encoder->q;

	bool result = true;
	bool first_frame = false;

	int chroma_offset = encoder->codec.chroma_offset;
	int transform_type = (encoder->gop_length > 1) ? TRANSFORM_TYPE_FIELDPLUS : TRANSFORM_TYPE_SPATIAL;
	int chroma_width;
	int frame_index;

	// The display height is the height of the original input image
	int display_height = height;

	int fixedquality = i_fixedquality;

	// Need to modify routines called from this routine to accept the scratch buffer data structure
	PIXEL *buffer = (PIXEL *)scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	encoder->encoder_quality = fixedquality;

#if _TIMING
	DoThreadTiming(2);
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Encode sample, width: %d, height: %d, format: %d\n", width, height, format);
	}
#endif

	// The transforms should have already been allocated
	assert(transform != NULL && transform[0] != NULL);

	// Initialize the codec state used during encoding
	InitEncoderCodecState(encoder, codec);

	// Round the height up to the nearest multiple of 8
	// Needed for 1080 bayer encodes as 540 per band is not divisible by 8
	height = ROUNDUP(height, 8);

	// Check that the frame dimensions correspond to the dimensions used for the transform
	assert(width == transform[0]->width);
	assert(height == transform[0]->height);

	// Compute the width of the chroma channels
	switch (format)
	{
	case COLOR_FORMAT_BYR1:
	case COLOR_FORMAT_BYR2:
	case COLOR_FORMAT_BYR3:
	case COLOR_FORMAT_BYR4:
	case COLOR_FORMAT_BYR5:
		chroma_width = width;
		break;

	case COLOR_FORMAT_YUYV:
	case COLOR_FORMAT_UYVY:
		if (display_height != height)
		{
			// Extend the input frame
			data = AllocateAndCopyExtendedFrame(data, width, height, pitch, display_height, scratch);

			// Update the free space in the scratch buffer
			buffer = (PIXEL *)scratch->free_ptr;
			buffer_size = scratch->free_size;
		}

//#if BUILD_PROSPECT //10-bit for everyone
//		if (i_fixedquality & 0x10000/*CFQ_FORCE_10BIT*/)
//		{
			codec->precision = CODEC_PRECISION_10BIT;
//		}
//#endif

		// Fall through to set the chroma width

	default:
		chroma_width = width / 2;
		break;
	}

	// Check that the frame dimensions are transformable
	assert(IsFrameTransformable(chroma_width, height, transform_type, encoder->num_spatial));

	/*
		Note: The transform is allocated and the fields are initialized when the encoder is
		initialized.  The dimensions and type of the transform is not chanaged during encoding.
		Not sure what will happen if the frame dimensions do not correspond to the dimensions
		used to initialize the transform data structure.  Probably should crop or fill the frame
		to the dimensions defined in the transform data structure when the packed frame provided
		as input to the encoder is converted to planes of luma and chroma channels.
	*/

	// Start compressing the frame (including conversion time)
	START(tk_compress);

	// Allocate a data structure for the unpacked frame
	SetEncoderFormatRecursive(encoder, width, height, display_height, format);

	START(tk_convert);
	switch (format)
	{
#if 0
	case COLOR_FORMAT_RGB24:
 // #if BUILD_PROSPECT//10-bit for everyone
		format = COLOR_FORMAT_YU64;
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
		encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
//  #endif
		// Convert the packed RGB data to planes of YUV 4:2:2 (one byte per pixel)
		ConvertRGB32to10bitYUVFrame(data, pitch, frame, (uint8_t *)buffer, buffer_size, encoder->input.color_space, encoder->codec.precision, false, 0);
		break;

	case COLOR_FORMAT_RGB32:
//#if BUILD_PROSPECT//10-bit for everyone
		format = COLOR_FORMAT_YU64;
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
		encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
//#endif
		// Convert the packed RGBA data to planes of YUV 4:2:2 (one byte per pixel)
		// The data in the alpha channel is not used
		ConvertRGB32to10bitYUVFrame(data, pitch, frame, (uint8_t *)buffer, buffer_size,
			encoder->input.color_space, encoder->codec.precision, true, 0);
		break;

#if BUILD_PROSPECT
	case COLOR_FORMAT_V210:
		// Convert the packed 10-bit YUV 4:2:2 to planes of 16-bit YUV
		ConvertV210ToFrame16s(data, pitch, frame, (uint8_t *)buffer);
		codec->precision = CODEC_PRECISION_10BIT;
		break;

	case COLOR_FORMAT_YU64:
		// Convert the unpacked 64/16-bit YUV 4:2:2 to planes of 16-bit YUV
		ConvertYU64ToFrame16s(data, pitch, frame, (uint8_t *)buffer);
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		break;

	case COLOR_FORMAT_BYR1:
		// Convert the unpacked 64/16-bit YUV 4:2:2 to planes of 16-bit YUV
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		ConvertBYR1ToFrame16s(encoder->bayer.format, data, pitch, frame, (uint8_t *)buffer);
		chroma_width = width;
		break;

	case COLOR_FORMAT_BYR2:
		// Convert the unpacked 64/16-bit YUV 4:2:2 to planes of 16-bit YUV
		ConvertBYR2ToFrame16s(data, pitch, frame, (uint8_t *)buffer);
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		chroma_width = width;
		break;

	case COLOR_FORMAT_BYR3: // now handled directly for speed.
		// Convert the planer 16-bit Bayer to planes of 16-bit YUV
		//ConvertBYR3ToFrame16s(data, pitch, frame, (uint8_t *)buffer); //path not used
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		chroma_width = width;
		break;

	case COLOR_FORMAT_BYR4:
		// Convert the unpacked 64/16-bit YUV 4:2:2 to planes of 16-bit YUV
		encoder->codec.precision = CODEC_PRECISION_12BIT;
		i_fixedquality |= (3 << 25); // CFEncode_RGB_Quality_Mask , DAN20080110 prevent increased quant on channels 1-3
		ConvertBYR4ToFrame16s(&encoder->bayer.format, encoder->encode_curve, encoder->encode_curve_preset, data, pitch, frame, encoder->codec.precision);
		chroma_width = width;
		break;
		
	case COLOR_FORMAT_BYR5:
		assert(0);
		break;
#endif
#endif
	}
	STOP(tk_convert);

	SetEncoderQuantization(encoder, format, i_fixedquality, fixedbitrate, custom);

	// Is this the first frame in the GOP?
	if (encoder->group.count == 0)
	{
		int channel;

		// Set the quantization and prescaling for the transforms in this group
		for (channel = 0; channel < num_transforms; channel++)
		{
			// Set the prescaling (may be used in setting the quantization)
			SetTransformPrescale(transform[channel], transform_type, codec->precision);

			// Set the quantization
			SetTransformQuantization(encoder, transform[channel], channel, framerate);

			// Copy the quantization into the recursive transform state
			//SetTransformStateQuantization(transform[channel]);

			// Intialize the descriptors that specify the transforms
			SetTransformDescriptors(encoder, transform[channel]);
		}
	}

#if (0 && DEBUG)
	if (logfile)
	{
		int k;

		for (k = 0; k < num_transforms; k++) {
			fprintf(logfile, "Quantization for channel: %d\n", k);
			PrintTransformQuantization(transform[k], logfile);
			fprintf(logfile, "\n");
		}
	}
#endif

	// Is this the first frame in the video sequence?
	if (encoder->frame_count == 0 && encoder->group.count == 0 && encoder->gop_length > 1)
	{
		// The first frame must alwasy be a key frame
		encoder->output.iskey = true;
		first_frame = true;
	}

	// Determine the index of this frame in the group
	frame_index = encoder->group.count;

	// Should be the first or second frame in a two frame group
	assert(0 <= frame_index && frame_index <= encoder->gop_length);

	// Set the number of channels in the encoder quantization table
	encoder->num_quant_channels = num_transforms;

	// The first version of the recursive encoder only handles intra frame encoding
	assert(encoder->gop_length == 1);

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Transform recursive: %d, progressive: %d\n", frame_index, encoder->progressive);
	}
#endif

	switch (format)
	{
#if 1
	case COLOR_FORMAT_YUYV:
		TransformForwardRecursiveYUYV(encoder, data, frame_index,
									  width, height, pitch,
									  transform, num_transforms,
									  (uint8_t *)buffer, buffer_size);
		break;
#endif
#if 0
	case COLOR_FORMAT_UYVY:
		TransformForwardRecursiveUYVY(encoder, data, width, height, pitch,
									  transform, num_transforms,
									  (uint8_t *)buffer, buffer_size);
		break;
#endif
#if 0
	case COLOR_FORMAT_RGB24:
		TransformForwardRecursiveRGB24(encoder, data, width, height, pitch,
									  transform, num_transforms,
									  (uint8_t *)buffer, buffer_size);
		break;
#endif
#if 0
		TransformForwardRecursiveRGB32(encoder, data, width, height, pitch,
									  transform, num_transforms,
									  (uint8_t *)buffer, buffer_size);
		break;
#endif

#if BUILD_PROSPECT
#if 0
	case COLOR_FORMAT_V210:
		// Convert the packed 10-bit YUV 4:2:2 to planes of 16-bit YUV
		ConvertV210ToFrame16s(data, pitch, frame, (uint8_t *)buffer);
		codec->precision = CODEC_PRECISION_10BIT;
		break;
#endif
#if 0
	case COLOR_FORMAT_YU64:
		// Convert the unpacked 64/16-bit YUV 4:2:2 to planes of 16-bit YUV
		ConvertYU64ToFrame16s(data, pitch, frame, (uint8_t *)buffer);
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		break;
#endif
#if 0
	case COLOR_FORMAT_BYR1:
		// Convert the unpacked 64/16-bit YUV 4:2:2 to planes of 16-bit YUV
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		ConvertBYR1ToFrame16s(encoder->bayer.format, data, pitch, frame, (uint8_t *)buffer);
		chroma_width = width;
		break;
#endif
#if 0
	case COLOR_FORMAT_BYR2:
		// Convert the unpacked 64/16-bit YUV 4:2:2 to planes of 16-bit YUV
		ConvertBYR2ToFrame16s(data, pitch, frame, (uint8_t *)buffer);
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		chroma_width = width;
		break;
#endif
#if 0
	case COLOR_FORMAT_BYR3: // now handled directly for speed.
		{
			FRAME_INFO info = {width, height, format};
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			// Apply the frame transform directly to the frame
			TransformForwardSpatialBYR3(data, pitch, &info, transform, frame_index, num_transforms,
									   buffer, buffer_size, chroma_offset, false, display_height);
		}
		break;
#endif
#endif

	default:
		// Cannot encode this color format
		assert(0);
		encoder->error = CODEC_ERROR_BADFORMAT;
		return false;
	}

#if TIMING
	if (encoder->progressive)
	{
		// Count the number of progressive frames that were encoded
		progressive_encode_count++;
	}
#endif

	if (first_frame)
	{
		EncodeFirstSampleRecursive(encoder, output, transform, num_transforms, width, height, display_height, format, format);
	}

	// Increment the count of the number of frames in the group
	encoder->group.count++;

	// Is this encoded sample an intra frame?
	if (encoder->gop_length == 1)
	{
		// Encode the transform for the current frame
		EncodeQuantizedGroup(encoder, transform, num_transforms, output, 0, 0);

		// Reset the group of frames
		encoder->group.count = 0;

		// Update the frame count
		encoder->frame_count += encoder->gop_length;

		// Mark this frame as a key frame since it is an intra frame
		//frame->iskey = true;
		encoder->output.iskey = true;
	}

#if 0
	// Enough frames to compute the rest of the wavelet transform?
	else if (encoder->group.count == encoder->gop_length)
	{
		int channel;

		// Copy encoder parameters into the transform data structure
		//int gop_length = encoder->gop_length;
		//int num_spatial = encoder->num_spatial;

		if (pPreviewBuffer)
		{
			PreviewDuringEncoding(encoder, transform, num_transforms, pPreviewBuffer);
		}

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Before encode quantized group\n");
		}
#endif

		// Encode the transform for the current group of frames
		EncodeQuantizedGroup(encoder, transform, num_transforms, output, 0, 0);

		//DAN Variable Bit Rate control feedback.
		//encoder->lastgopbitcount = output->nWordsUsed * 8;//output->cntBits;
		//output->cntBits = 0;

		// Reset the group of frames
		encoder->group.count = 0;

		// Update the frame count
		encoder->frame_count += encoder->gop_length;

		// Mark this frame as a key frame since it is the start of a group
		//frame->iskey = true;
		encoder->output.iskey = true;
	}

	// Waiting for enough frames to complete a group
	else
	{
		// Is this the first frame in the video sequence?
		if (first_frame)
		{
			// Mark this frame as a key frame since it is the start of the sequence
			//frame->iskey = true;
			encoder->output.iskey = true;
		}
		else
		{
			//int width = frame->width;
			//int height = frame->height;
			int group_index = encoder->group.count;
			int frame_number = encoder->frame_number;
			int encoded_format = encoder->encoded_format;

			// Increment the frame sequence number
			encoder->frame_number++;

			// Output the header that indicates a prediction frame
			PutVideoFrameHeader(output, FRAME_TYPE_PFRAME, width, height, display_height, group_index,
								frame_number, encoded_format);

			// Update the frame count
			//encoder->frame_count++;

			// This frame is not a key frame
			//frame->iskey = false;
			encoder->output.iskey = false;
		}
	}
#endif

finish:

	// Force output of any bits pending in the bitstream buffer
	FlushBitstream(output);

	if (encoder->output.iskey) {
		encoder->lastgopbitcount = BITSTREAM_WORD_SIZE * output->nWordsUsed;
	}

	// Clear the mmx register state in case was not cleared by the filter routines
	//_mm_empty();

	STOP(tk_compress);

#if (0 && DEBUG)
	if (logfile)
	{
		CODEC_ERROR error = encoder->error;
		fprintf(logfile, "Returning from encode sample recursive, result: %d, error: %d\n", result, error);
	}
#endif

#if _TIMING
	DoThreadTiming(3);
#endif

	return result;
}


// Encode one frame of video using recursive wavelet transforms if possible
bool EncodeSample(ENCODER *encoder, uint8_t * data, int width, int height, int pitch, int format,
				  TRANSFORM *transform[], int num_transforms, BITSTREAM *output,
				  PIXEL *buffer, size_t buffer_size, int fixedquality, int fixedbitrate,
				  uint8_t* pPreviewBuffer, float framerate, custom_quant *custom)
{
	assert(encoder != NULL);
	if (encoder != NULL)
	{
		bool progressive = encoder->progressive;
		int gop_length = encoder->gop_length;

		// Can this case be handled using recursive wavelet transforms?
		if (gop_length == 1 && format == COLOR_FORMAT_YUYV)
		{
			SCRATCH scratch = SCRATCH_INITIALIZER(buffer, buffer_size);

			return EncodeSampleRecursive(encoder, data, width, height, pitch, format,
										 transform, num_transforms, output,
										 &scratch, fixedquality, fixedbitrate,
										 pPreviewBuffer, framerate, custom);
		}
	}

	// Use the non-recursive wavelet transforms if this case is not implemented recursively
	return EncodeSampleOld(encoder, data, width, height, pitch, format,
						   transform, num_transforms, output,
						   buffer, buffer_size, fixedquality, fixedbitrate,
						   pPreviewBuffer, framerate, custom);
}

#endif


void PreviewDuringEncoding(ENCODER * encoder,
						   TRANSFORM *transform[],
						   int num_transforms,
						   uint8_t *pPreviewBuffer)
{
	int scale = 4;
	int level;
	int valuescale;

	switch(scale)
	{
	case 2:
		level = 2;
		valuescale = 3;
		break;
	case 4:
		level = 4;
		valuescale = 5;
		break;
	case 8:
	default:
		level = 5;
		valuescale = 7;
		break;
	}

	// TODO: lock preview buffer
	OutputRGB(pPreviewBuffer, transform[0]->wavelet[level], transform[1]->wavelet[level], transform[2]->wavelet[level], valuescale);
	// TODO: unlock preview buffer
	// TODO: post preview msg
}


#if 0 //dan20041031 not used
// Encode highpass coefficients using run length coding only for zeros.
// Runs of zeros can extend across multiple rows.
void EncodeLongRuns(ENCODER *encoder, BITSTREAM *stream, PIXEL *image,
					int width, int height, int pitch)
{
	RLCBOOK *runsbook = encoder->codebook[encoder->codec.active_codebook]->runbook; //DAN20041026
	VLCBOOK *magsbook = encoder->codebook[encoder->codec.active_codebook]->magbook; //DAN20041026
	VALBOOK *valuebook = encoder->valuebook[encoder->codec.active_codebook]; //DAN20041026
	PIXEL *rowptr = image;
	RUN run = {0, 0};
	int row, column;

#if _STATS
	int current = (int)stream->cntBits;
#endif

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	for (row = 0; row < height; row++)
	{
		int index = 0;				// Start at the beginning of the row
		int length = width;			// Search to the end of the row

		// Search the row for runs of zeros and nonzero values
		while (index < width)
		{
			int count;

			// Loop invariant
			assert(0 <= index && index < width);

			// Look for nonzero values until the end of the row
			count = FindNonZero(&rowptr[index], length);
			assert(0 <= count && (index + count) <= width);

			// Accumulate runs of zeros across rows into a single run
			if (count > 0) {
				run.count += count;
				index += count;
			}

			// Need to output a value?
			if (index < width) {
				PIXEL value = rowptr[index];
				assert(value != 0);

				// Need to output a run of zeros before this value?
				if (run.count > 0) {
					assert(run.value == 0);
					PutZeroRun(stream, run.count, runsbook);

#if _STATS
					CountRuns(STATS_DEFAULT, run.count);
					//CountValues(STATS_DEFAULT, 0, run.count);
					{
						int i;
						for(i=0; i<run.count; i++)
						{
							CountValues(STATS_DEFAULT, 0, 1);
						}
					}
#endif
					run.count = 0;
				}

				// Output the signed value
				PutVlcByte(stream, VALUE(value), valuebook);

				CountValues(STATS_DEFAULT, value, 1);

				index++;
			}

			// Reduce the remaining length of the row
			length = width - index;
		}

		// Should have processed the entire row
		assert(length == 0);

		// Advance to the next row
		rowptr += pitch;
	}

	// Need to output a pending run of zeros?
	if (run.count > 0) {
		assert(run.value == 0);
		PutZeroRun(stream, run.count, runsbook);

#if _STATS
		CountRuns(STATS_DEFAULT, run.count);
//		CountValues(STATS_DEFAULT, 0, run.count);
		{
			int i;
			for(i=0; i<run.count; i++)
			{
				CountValues(STATS_DEFAULT, 0, 1);
			}
		}
#endif
	}


#if _STATS
	// Update the file of run length and value statistics
	UpdateStats(STATS_DEFAULT);

	NewSubBand(width,height,0,((int)stream->cntBits) - current, current - stats_lastbits);

	stats_lastbits = (int)stream->cntBits;
#endif
}
#endif

//NOTE: Need to create a codebook for the lowpass image pixels?

void EncodeLowPassBand(ENCODER *encoder, BITSTREAM *output, IMAGE *wavelet, int channel, int subband)
{
	//FILE *logfile = encoder->logfile;
	int level = wavelet->level;
	int width = wavelet->width;
	int height = wavelet->height;
	int lowpass_border = 0;		//wavelet->lowpass_border;
	int left_margin = 0;
	int top_margin = 0;
	int right_margin = lowpass_border;
	int bottom_margin = lowpass_border;
	int image_pitch;
	PIXEL *image_row_ptr = wavelet->band[0];
	int row, column;

	// Quantization parameters
	//int pixel_average;
	//int pixel_minimum;
	//int pixel_maximum;
	//int normalized_range;
	int pixel_offset = 0;
	int quantization = 1;
	int bits_per_pixel = 16;
	int solid_color, solid = 1;

#if _STATS
	int current;
	stats_lastbits = (int)output->cntBits;
#endif

	//START(tk_lowpass);

#if (0 && DEBUG)
	if (debugfile && debug['L']) {
		SUBIMAGE border = SUBIMAGE_LOWER_LEFT(16, 15);
		DumpBand("Low Pass Image", wavelet, 0, &border, debugfile);
	}
#endif

#if (0 && DEBUG)
	if (logfile) {
		int scale = wavelet->scale[0];
		fprintf(logfile, "Encode low pass channel: %d, scale: %d, quantization: %d\n",
				channel, scale, quantization);
	}
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile,
				"Before encoding lowpass channel: %d, width: %d, height: %d, quantization: %d\n",
				channel, width, height, quantization);
		DumpWaveletRow(wavelet, 0, 0, logfile);
	}
#endif

#if _STATS
	SetQuantStats(quantization);
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "\nLowpass Statistics:\n");
		fprintf(logfile, "Minimum: %d, maximum: %d, average: %d\n",
				pixel_minimum, pixel_maximum, pixel_average);
		fprintf(logfile, "Normalized range: %d, offset: %d, bpp: %d\n",
				normalized_range, pixel_offset, bits_per_pixel);
		fprintf(logfile, "Quantization: %d\n", quantization);
	}
#endif

#if (0 && DEBUG)
	//if (channel == 0)
	{
		static bool first = true;

		if (first)
		{
			char label[_MAX_PATH];
			sprintf(label, "Low%d-", channel);
			DumpPGM(label, wavelet, NULL);

			if (channel == 2) first = false;
		}
	}
#endif


	if(encoder->encoder_quality & 0x40000000) // CFEncode_OptimizeEmptyChannels
	{
		image_pitch = wavelet->pitch/sizeof(PIXEL);
		solid_color = image_row_ptr[0];
		solid = 1;
		for (row = 0; row < height; row++)
		{
			for (column = 0; column < width; column++)
			{
				if(solid_color != image_row_ptr[column])
				{
					solid = 0;
					break;
				}
			}
			image_row_ptr += image_pitch;
		}
		image_row_ptr = wavelet->band[0];
	}
	else
	{
		solid = 0;
	}


	// Need to transmit the pixel offset as a non-negative number since
	// we know (assume) that it is positive for luma and negative for chroma
	PutVideoLowPassHeader(output, subband, level, width, height,
						  left_margin, top_margin, right_margin, bottom_margin,
						  abs(pixel_offset), quantization, bits_per_pixel);

#if _STATS
	current = (int)output->cntBits;
#endif

#if (_CODEC_TAGS && _CODEC_MARKERS)
	PutVideoLowPassMarker(output);
#endif

	// Check that the bitstream is tag aligned before writing the pixels
	assert(IsAlignedBits(output));

	image_pitch = wavelet->pitch/sizeof(PIXEL);

	// Adjustment for preserving precision
	if (width & 1)  // not divisible by two
	{
		for (row = 0; row < height; row++)
		{
			for (column = 0; column < width; column++)
			{
				int pixel_value = SATURATE(image_row_ptr[column]);
				PutBits(output, pixel_value, bits_per_pixel);
			}
			image_row_ptr += image_pitch;
		}
	}
	else
	{
#define DUMPLL 0
#if (_DEBUG && DUMPLL)
		FILE *fp;

		if(channel == 0)
		{
			static int inc = 1;
			char name[256];

			sprintf(name,"C:\\Cedoc\\LLenc%03d.pgm", inc++);

			fp = fopen(name,"w");
			fprintf(fp, "P2\n# CREATOR: DAN\n%d %d\n255\n", width, height);
		}
#endif

		if(solid)
		{
			PutLong(output, 0xffffffff);
			PutLong(output, solid_color);
			PutLong(output, width);
			PutLong(output, height);
		}
		else
		{
			for (row = 0; row < height; row++)
			{
				unsigned short *pixelptr;
				pixelptr = (unsigned short *)image_row_ptr;
				for (column = 0; column < width; column += 2)
				{
					uint32_t  val;

	#if (_DEBUG && DUMPLL)
					if(channel==0 && fp)
						fprintf(fp, "%d\n%d\n", (*pixelptr)>>7, (*(pixelptr+1))>>7 );
	#endif
					val = (*pixelptr++);
					val <<= 16;
					val |= (*pixelptr++) & 0xffff;
					PutLong(output, val);
				}
				image_row_ptr += image_pitch;
			}
		}
#if (_DEBUG && DUMPLL)
		if(channel == 0 && fp)
			fclose(fp);
#endif
	}

#if (0 && DEBUG)
	if (logfile) DumpBits(output, logfile);
#endif

#if _CODEC_TAGS
	// Align the bitstream to a tag boundary
	PadBitsTag(output);
#endif

#if (0 && DEBUG)
	if (logfile) DumpBits(output, logfile);
#endif

	PutVideoLowPassTrailer(output);

#if (0 && DEBUG)
	if (logfile) DumpBits(output, logfile);
#endif

#if _STATS
	NewSubBand(width,height,1,((int)output->cntBits) - current, current - stats_lastbits);
	stats_lastbits = (int)output->cntBits;
#endif

	//STOP(tk_lowpass);
}


#if _PACK_RUNS_IN_BAND_16S

void EncodeQuantPackedLongRuns(ENCODER *encoder, BITSTREAM *stream, PIXEL *image,
							   int width, int height, int pitch, int divisor)
{
	FILE *logfile = encoder->logfile;
	RLCBOOK *runsbook = encoder->codebook->runbook;
//	VLCBOOK *magsbook = encoder->codebook->magbook;
	VALBOOK *valuebook = encoder->valuebook;
	PIXEL *rowptr = image;
	//RUN run = {0, 0};
	int row, column;
	int gap;
	int count = 0;
	PIXEL *sptr;

#if _STATS
	int current = (int)stream->cntBits;
#endif

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	// Compute the number of pixels in the gap at the end of each row
	gap = (pitch - width);

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Encoding width: %d, height: %d, gap: %d\n",
				width, height, gap);
	}
#endif

	sptr = rowptr;

	for (row = 0; row < height; row++)
	{
		int index = 0,zeros, outcount = 0;				// Start at the beginning of the row

		{
			int indx,tmp;
			int runsbooklength = runsbook->length;
			int valuebooklength = valuebook->length;
			RLC *rlc = (RLC *)((char *)runsbook + sizeof(RLCBOOK));
			VLE *table = (VLE *)((char *)valuebook + sizeof(VALBOOK));
			uint32_t  wBuffer;
			int nBitsFree;
//			int cntBits;
			const int nWordsPerLong = sizeof(uint32_t )/sizeof(uint8_t );
			uint32_t  *lpCurrentWord = (uint32_t  *)(stream->lpCurrentWord);
			int nWordsUsed = stream->nWordsUsed;
			int col = 0;


			// Move the current word into a int32_t buffer
			wBuffer = stream->wBuffer;
			// Number of bits remaining in the current word
			nBitsFree = stream->nBitsFree;
//			cntBits = stream->cntBits;


	//		sptr = rowptr;
			while((tmp = *sptr++) && col < width)
			{
				col++;

				if(tmp & 1)  //value
				{
					//PutVlcByte(stream, (PIXEL)*sptr++, valuebook);
					DWORD codeword;
					DWORD codesize;
					int value = tmp>>1;

					//DAN20050914 -- This fixes large positive numbers (peaks) overflowing as negative non-peak value
					if(value < 0)
					{
						if(value <= -(VALUE_TABLE_LENGTH>>1))
							value = -((VALUE_TABLE_LENGTH>>1)-1);

						indx = VALUE_TABLE_LENGTH + value;
					}
					else
					{
						if(value >= (VALUE_TABLE_LENGTH>>1))
							value = ((VALUE_TABLE_LENGTH>>1)-1);

						indx = value;
					}
					/*
					if(value < 0)
						indx = VALUE_TABLE_LENGTH + value;
					else
						indx = value;

					if (indx < 0) indx = 0;
					else if (indx >= valuebooklength) indx = valuebooklength - 1;*/

					// Use the packed version of the codebook entry
					codeword = table[indx].entry & VLE_CODEWORD_MASK;
					codesize = table[indx].entry >> VLE_CODESIZE_SHIFT;
					//PutBits(stream, codeword, codesize);
					//void PutBits(BITSTREAM *stream, uint32_t  wBits, int nBits)
					{
						int nBits = codesize;
						int wBits = codeword;

						// Will the bits fit in the int32_t buffer?
						if (nBitsFree == BITSTREAM_LONG_SIZE) {
							wBuffer = wBits & BITMASK(nBits);
							nBitsFree -= nBits;
						}
						else if (nBits <= nBitsFree) {
							wBuffer <<= nBits;
							wBuffer |= (wBits & BITMASK(nBits));
							nBitsFree -= nBits;
						}
						else {
							// Fill the buffer with as many bits as will fit
							wBuffer <<= nBitsFree;
							nBits -= nBitsFree;

							// Insert as many bits as will fit into the buffer
							wBuffer |= (wBits >> nBits) & BITMASK(nBitsFree);

							// Insert all of the bytes in the buffer into the bitstream
							//PutLong(stream, wBuffer);
							//void PutLong(BITSTREAM *stream, uint32_t  word)
							{
								nWordsUsed += nWordsPerLong;

							//	if (nWordsUsed <= stream->dwBlockLength) {
									//*(lpCurrentWord++) = _bswap(wBuffer);
									*(lpCurrentWord++) = SwapInt32NtoB(wBuffer);
								//	stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
								//	stream->nWordsUsed = nWordsUsed;
							//	}
							//	else {
							//		stream->error = BITSTREAM_ERROR_OVERFLOW;
							//	}
							}

							wBuffer = wBits & BITMASK(nBits);
							nBitsFree = BITSTREAM_LONG_SIZE - nBits;
						}

						// Count the number of bits written to the bitstream
//						cntBits += nBits;
					}
				}
				else // zero run
				{
				//	PutZeroRun(stream, zeros, runsbook);
					zeros = tmp >> 1;

					// Output one or more run lengths until the run is finished
					while (zeros > 0)
					{
						// Index into the codebook to get a run length code that covers most of the run
						indx = (zeros < runsbooklength) ? zeros : runsbooklength - 1;

						// Output the run length code
						//PutBits(stream, rlc[indx].bits, rlc[indx].size);
						//void PutBits(BITSTREAM *stream, uint32_t  wBits, int nBits)
						{
							int nBits = rlc[indx].size;
							int wBits = rlc[indx].bits;

							// Will the bits fit in the int32_t buffer?
							if (nBitsFree == BITSTREAM_LONG_SIZE) {
								wBuffer = wBits & BITMASK(nBits);
								nBitsFree -= nBits;
							}
							else if (nBits <= nBitsFree) {
								wBuffer <<= nBits;
								wBuffer |= (wBits & BITMASK(nBits));
								nBitsFree -= nBits;
							}
							else {
								// Fill the buffer with as many bits as will fit
								wBuffer <<= nBitsFree;
								nBits -= nBitsFree;

								// Insert as many bits as will fit into the buffer
								wBuffer |= (wBits >> nBits) & BITMASK(nBitsFree);

								// Insert all of the bytes in the buffer into the bitstream
								//PutLong(stream, wBuffer);
								//void PutLong(BITSTREAM *stream, uint32_t  word)
								{
									nWordsUsed += nWordsPerLong;

								//	if (nWordsUsed <= stream->dwBlockLength) {
										//*(lpCurrentWord++) = _bswap(wBuffer);
										*(lpCurrentWord++) = SwapInt32NtoB(wBuffer);
									//	stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
									//	stream->nWordsUsed = nWordsUsed;
								//	}
								//	else {
								//		stream->error = BITSTREAM_ERROR_OVERFLOW;
								//	}
								}

								wBuffer = wBits & BITMASK(nBits);
								nBitsFree = BITSTREAM_LONG_SIZE - nBits;
							}

							// Count the number of bits written to the bitstream
//							cntBits += nBits;
						}

						// Reduce the length of the run by the amount output
						zeros -= rlc[indx].count;
					}
				}
			}

			// Move the current word into a int32_t buffer
			stream->wBuffer = wBuffer;
			// Number of bits remaining in the current word
			stream->nBitsFree = nBitsFree;
//			stream->cntBits = cntBits;
			stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
			stream->nWordsUsed = nWordsUsed;
		}

		// Advance to the next row
		rowptr += pitch;
	}

	// Need to output a pending run of zeros?
	if (count > 0)
	{
		PutZeroRun(stream, count, runsbook);

 #if _STATS
		CountRuns(STATS_DEFAULT, count);
//		CountValues(STATS_DEFAULT, 0, count);
		{
			int i;
			for(i=0; i<count; i++)
			{
				CountValues(STATS_DEFAULT, 0, 1);
			}
		}
 #endif
	}

 #if _STATS
	// Update the file of run length and value statistics
	UpdateStats(STATS_DEFAULT);

	NewSubBand(width,height,0,((int)stream->cntBits) - current, current - stats_lastbits);

	stats_lastbits = (int)stream->cntBits;
 #endif
}

#endif



int EncodeZeroLongRuns(ENCODER *encoder, BITSTREAM *stream, PIXEL *image,
						 int width, int height, int pitch, int divisor, int active_codebook)
{
	//FILE *logfile = encoder->logfile;
	RLCBOOK *runsbook;
	//VALBOOK *valuebook;
	//PIXEL *rowptr = image;
	//PIXEL *peaksptr = image;
	int peakscounter = 0;
	//RUN run = {0, 0};
	//int row, column;
	int gap;
	int count = 0;

#if _STATS
	int current = (int)stream->cntBits;
#endif

	//CODEC_STATE *codec = &encoder->codec;
	//int subband = codec->band.subband;

	runsbook = encoder->codebook_runbook[active_codebook]; //DAN20150817

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	// Compute the number of pixels in the gap at the end of each row
	gap = (pitch - width);

	count = (height - 1) * (width + gap) + width;

	// Need to output a pending run of zeros?
	if (count > 0)
	{
		PutZeroRun(stream, count, runsbook);

#if _STATS
		CountRuns(STATS_DEFAULT, count);
//		CountValues(STATS_DEFAULT, 0, count);
		{
			int i;
			for(i=0; i<count; i++)
			{
				CountValues(STATS_DEFAULT, 0, 1);
			}
		}
#endif
	}

#if _STATS
	// Update the file of run length and value statistics
	UpdateStats(STATS_DEFAULT);

	NewSubBand(width,height,0,((int)stream->cntBits) - current, current - stats_lastbits);

	stats_lastbits = (int)stream->cntBits;
#endif

	return peakscounter;
}




int EncodeQuantLongRunsPlusPeaks(ENCODER *encoder, BITSTREAM *stream, PIXEL *image,
						 int width, int height, int pitch, int divisor, int active_codebook, int quantization)
{
	//FILE *logfile = encoder->logfile;
	RLCBOOK *runsbook;
//	VLCBOOK *magsbook = encoder->codebook[encoder->active_codebook]->magbook;
	VALBOOK *valuebook;
	PIXEL *rowptr = image;
	PIXEL *peaksptr = image;
	int peakscounter = 0;
	//RUN run = {0, 0};
	int row;
	//int column;
	int gap;
	int count = 0;

#if _STATS
	int current = (int)stream->cntBits;
#endif

	//CODEC_STATE *codec = &encoder->codec;
	//int subband = codec->band.subband;

	runsbook = encoder->codebook_runbook[active_codebook]; //DAN20150817
	valuebook = encoder->valuebook[active_codebook]; //DAN20041026


	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	// Compute the number of pixels in the gap at the end of each row
	gap = (pitch - width);

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Encoding width: %d, height: %d, gap: %d\n",
				width, height, gap);
	}
#endif

	for (row = 0; row < height; row++)
	{
		int index = 0;				// Start at the beginning of the row
		//int zeros;
		int indx;
		int runsbooklength = runsbook->length;
		//int valuebooklength = valuebook->length;
		RLC *rlc = (RLC *)((char *)runsbook + sizeof(RLCBOOK));
		VLE *table = (VLE *)((char *)valuebook + sizeof(VALBOOK));
#if 1
		uint32_t  wBuffer;
		int nBitsFree;
		//int cntBits;
		const int nWordsPerLong = sizeof(uint32_t )/sizeof(uint8_t );
		uint32_t  *lpCurrentWord = (uint32_t  *)(stream->lpCurrentWord);
		int nWordsUsed = stream->nWordsUsed;
		//uint32_t  *lpStartWord = lpCurrentWord;

		// Move the current word into a int32_t buffer
		wBuffer = stream->wBuffer;
		// Number of bits remaining in the current word
		nBitsFree = stream->nBitsFree;
//		cntBits = stream->cntBits;
#endif
		// Search the row for runs of zeros and nonzero values
		while (index < width)
		{
			// Loop invariant
			assert(0 <= index && index < width);

			// Search the rest of the row for a nonzero value
			for (; index < width; index++) {
				if (rowptr[index] == 0) count++;
				else break;
			}

			// Need to output a value?
			if (index < width)
			{
				PIXEL value = rowptr[index];

				// Need to output a run of zeros before this value?
				if (count > 0)
				{
				//	PutZeroRun(stream, count, runsbook);
					while (count > 0)
					{
						// Index into the codebook to get a run length code that covers most of the run
						indx = (count < runsbooklength) ? count : runsbooklength - 1;

						// Output the run length code
#if 0
						PutBits(stream, rlc[indx].bits, rlc[indx].size);
#else
						//void PutBits(BITSTREAM *stream, uint32_t  wBits, int nBits)
						{
							int nBits = rlc[indx].size;
							int wBits = rlc[indx].bits;

							// Will the bits fit in the int32_t buffer?
#if 0
							if (nBitsFree == BITSTREAM_LONG_SIZE) {
								wBuffer = wBits & BITMASK(nBits);
								nBitsFree -= nBits;
							}
							else
#endif
							if (nBits <= nBitsFree) {
								wBuffer <<= nBits;
								wBuffer |= (wBits & BITMASK(nBits));
								nBitsFree -= nBits;
							}
							else {
								// Fill the buffer with as many bits as will fit
								wBuffer <<= nBitsFree;
								nBits -= nBitsFree;
#if (0 && DEBUG)
								// Count the number of bits written to the bitstream
								stream->cntBits += nBitsFree;
#endif
								// Insert as many bits as will fit into the buffer
								wBuffer |= (wBits >> nBits) & BITMASK(nBitsFree);

								// Insert all of the bytes in the buffer into the bitstream
								//PutLong(stream, wBuffer);
								//void PutLong(BITSTREAM *stream, uint32_t  word)
								{
									nWordsUsed += nWordsPerLong;

								//	if (nWordsUsed <= stream->dwBlockLength) {
										//*(lpCurrentWord++) = _bswap(wBuffer);
										*(lpCurrentWord++) = SwapInt32NtoB(wBuffer);
									//	stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
									//	stream->nWordsUsed = nWordsUsed;
								//	}
								//	else {
								//		stream->error = BITSTREAM_ERROR_OVERFLOW;
								//	}
								}

								wBuffer = wBits & BITMASK(nBits);
								nBitsFree = BITSTREAM_LONG_SIZE - nBits;
							}
#if (0 && DEBUG)
							// Count the number of bits written to the bitstream
							stream->cntBits += nBits;
#endif
						}
#endif
						// Reduce the length of the run by the amount output
						count -= rlc[indx].count;
					}

					count = 0;
				}

				//PutVlcByte(stream, value, valuebook);
				{
					uint32_t codeword;
					uint32_t codesize;

					if(abs(value) > PEAK_THRESHOLD)
					{
						*peaksptr++ = value * quantization;
						peakscounter++;

						//DAN20050914 -- This fixes large positive numbers (peaks) overflowing as negative non-peak value
						if(value>0)
							value = PEAK_THRESHOLD+1;
						else
							value = -PEAK_THRESHOLD-1;
					}

					//DAN20050914 -- This fixes large positive numbers (peaks) overflowing as negative non-peak value
				/*	if(abs(value) >= VALUE_TABLE_LENGTH>>1)
					{
						if(value>0)
							value = (VALUE_TABLE_LENGTH>>1)-1;
						else
							value = -((VALUE_TABLE_LENGTH>>1)-1);
					}*/

					if(value < 0)
						indx = VALUE_TABLE_LENGTH + value;
					else
						indx = value;

				/* // DAN20050914 because of the PEAK_THRESHOLD+1 lines this is not needed
					if (indx < 0)
						indx = 0;
					else if (indx >= valuebooklength)
						indx = valuebooklength - 1;*/


					// Use the packed version of the codebook entry
					codeword = table[indx].entry & VLE_CODEWORD_MASK;
					codesize = table[indx].entry >> VLE_CODESIZE_SHIFT;
#if 0
					PutBits(stream, codeword, codesize);
#else
					//void PutBits(BITSTREAM *stream, uint32_t  wBits, int nBits)
					{
						int nBits = codesize;
						int wBits = codeword;

						// Will the bits fit in the long buffer?
#if 0
						if (nBitsFree == BITSTREAM_LONG_SIZE) {
							wBuffer = wBits & BITMASK(nBits);
							nBitsFree -= nBits;
						}
						else
#endif
						if (nBits <= nBitsFree) {
							wBuffer <<= nBits;
							wBuffer |= (wBits & BITMASK(nBits));
							nBitsFree -= nBits;
						}
						else {
							// Fill the buffer with as many bits as will fit
							wBuffer <<= nBitsFree;
							nBits -= nBitsFree;
#if (0 && DEBUG)
							// Count the number of bits written to the bitstream
							stream->cntBits += nBitsFree;
#endif
							// Insert as many bits as will fit into the buffer
							wBuffer |= (wBits >> nBits) & BITMASK(nBitsFree);

							// Insert all of the bytes in the buffer into the bitstream
							//PutLong(stream, wBuffer);
							//void PutLong(BITSTREAM *stream, uint32_t  word)
							{
								nWordsUsed += nWordsPerLong;

							//	if (nWordsUsed <= stream->dwBlockLength) {
									//*(lpCurrentWord++) = _bswap(wBuffer);
									*(lpCurrentWord++) = SwapInt32NtoB(wBuffer);
								//	stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
								//	stream->nWordsUsed = nWordsUsed;
							//	}
							//	else {
							//		stream->error = BITSTREAM_ERROR_OVERFLOW;
							//	}
							}

							wBuffer = wBits & BITMASK(nBits);
							nBitsFree = BITSTREAM_LONG_SIZE - nBits;
						}

#if (0 && DEBUG)
						// Count the number of bits written to the bitstream
						stream->cntBits += nBits;
#endif
					}
#endif
				}
				index++;
			}

			// Add the row gap onto the encoding length
			if (index == width) count += gap;

		}
#if 1
		// Move the current word into a int32_t buffer
		stream->wBuffer = wBuffer;
		// Number of bits remaining in the current word
		stream->nBitsFree = nBitsFree;
//		stream->cntBits = cntBits;
		stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
		stream->nWordsUsed = nWordsUsed;
#endif

		// Advance to the next row
		rowptr += pitch;
	}

	// Need to output a pending run of zeros?
	if (count > 0)
	{
		PutZeroRun(stream, count, runsbook);

#if _STATS
		CountRuns(STATS_DEFAULT, count);
//		CountValues(STATS_DEFAULT, 0, count);
		{
			int i;
			for(i=0; i<count; i++)
			{
				CountValues(STATS_DEFAULT, 0, 1);
			}
		}
#endif
	}

#if _STATS
	// Update the file of run length and value statistics
	UpdateStats(STATS_DEFAULT);

	NewSubBand(width,height,0,((int)stream->cntBits) - current, current - stats_lastbits);

	stats_lastbits = (int)stream->cntBits;
#endif

	return peakscounter;
}



void EncodeQuantLongRuns2Pass(ENCODER *encoder, BITSTREAM *stream, PIXEL *image,
						 int width, int height, int pitch, int divisor, int active_codebook)
{
	//FILE *logfile = encoder->logfile;
	RLCBOOK *runsbook;
	VALBOOK *valuebook;
	PIXEL *rowptr = image;
	int row;
	//int column;
	int gap;
	int count = 0;
	int pass;

//static int once = 0;
	//CODEC_STATE *codec = &encoder->codec;
	//int subband = codec->band.subband;

	runsbook = encoder->codebook_runbook[active_codebook]; //DAN20150817
	valuebook = encoder->valuebook[active_codebook]; //DAN20041026

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	// Compute the number of pixels in the gap at the end of each row
	gap = (pitch - width);

/*	if(once <= 60)
	{
		char name[200];
		FILE *fp;

		sprintf(name,"C:/Cedoc/DUMP/Encoder/dump%02d.raw", once);

		fp = fopen(name,"wb");
		fwrite(rowptr,width*height,1,fp);
		fclose(fp);
		once++;
	//	image[0] = 0xa4fe;
	//	image[1] = 0x00ff;
	}*/

	for(pass=1;pass <= 2; pass++)
	{
		for (row = 0; row < height; row++)
		{
			int index = 0;				// Start at the beginning of the row
			//int zeros;
			int indx;
			int runsbooklength = runsbook->length;
			//int valuebooklength = valuebook->length;
			RLC *rlc = (RLC *)((char *)runsbook + sizeof(RLCBOOK));
			VLE *table = (VLE *)((char *)valuebook + sizeof(VALBOOK));

			uint32_t wBuffer;
			int nBitsFree;
			//int cntBits;
			const int nWordsPerLong = sizeof(uint32_t )/sizeof(uint8_t );
			uint32_t  *lpCurrentWord = (uint32_t  *)(stream->lpCurrentWord);
			int nWordsUsed = stream->nWordsUsed;
			//uint32_t *lpStartWord = lpCurrentWord;

			// Move the current word into a int32_t buffer
			wBuffer = stream->wBuffer;
			// Number of bits remaining in the current word
			nBitsFree = stream->nBitsFree;

			// Search the row for runs of zeros and nonzero values
			while (index < width)
			{
				// Loop invariant
				assert(0 <= index && index < width);

				// Search the rest of the row for a nonzero value
				if(pass == 1)
				{
					for (; index < width; index++)
					{
						if ((rowptr[index]&0xff) == 0)
						{
							rowptr[index] >>= 8;
							rowptr[index] &= 0xff; // for pass2
							count++;
						}
						else
						{
							break;
						}
					}
				}
				else
				{
					for (; index < width; index++)
					{
						if (rowptr[index] == 0)
							count++;
						else
						{
							break;
						}
					}
				}

				// Need to output a value?
				if (index < width)
				{
					PIXEL value = rowptr[index];
					if(pass == 1)
					{
					//	rowptr[index] = abs(value)>>8; // for pass2

					//	rowptr[index] = value>>8; // for pass2
					//	value <<= 8;
					//	value >>= 8; // sign extended 8-bit


						//if(value & 0x8000/*neg*/ && (value & 0x7f00) == 0)
						if(value < 0 && value >= -255)
						{
							rowptr[index] = 0;
							//value = value;
						}
						else
						{
							rowptr[index] = (value>>8) & 0xff; // for pass2
							value &= 0xff;

						}
					}

					// Need to output a run of zeros before this value?
					if (count > 0)
					{
					//	PutZeroRun(stream, count, runsbook);
						while (count > 0)
						{
							// Index into the codebook to get a run length code that covers most of the run
							indx = (count < runsbooklength) ? count : runsbooklength - 1;

							// Output the run length code
							//void PutBits(BITSTREAM *stream, uint32_t  wBits, int nBits)
							{
								int nBits = rlc[indx].size;
								int wBits = rlc[indx].bits;

								// Will the bits fit in the int32_t buffer?
								if (nBits <= nBitsFree) {
									wBuffer <<= nBits;
									wBuffer |= (wBits & BITMASK(nBits));
									nBitsFree -= nBits;
								}
								else
								{
									// Fill the buffer with as many bits as will fit
									wBuffer <<= nBitsFree;
									nBits -= nBitsFree;

									// Insert as many bits as will fit into the buffer
									wBuffer |= (wBits >> nBits) & BITMASK(nBitsFree);

									// Insert all of the bytes in the buffer into the bitstream
									//PutLong(stream, wBuffer);
									//void PutLong(BITSTREAM *stream, uint32_t  word)
									nWordsUsed += nWordsPerLong;
									*(lpCurrentWord++) = SwapInt32NtoB(wBuffer);

									wBuffer = wBits & BITMASK(nBits);
									nBitsFree = BITSTREAM_LONG_SIZE - nBits;
								}
							}
							// Reduce the length of the run by the amount output
							count -= rlc[indx].count;
						}

						count = 0;
					}

					//PutVlcByte(stream, value, valuebook);
					{
						uint32_t codeword;
						uint32_t codesize;

						//DAN20050914 -- This fixes large positive numbers (peaks) overflowing as negative non-peak value
						if(value < 0)
						{
							if(value <= -(VALUE_TABLE_LENGTH>>1))
								value = -((VALUE_TABLE_LENGTH>>1)-1);

							indx = VALUE_TABLE_LENGTH + value;
						}
						else
						{
							if(value >= (VALUE_TABLE_LENGTH>>1))
								value = ((VALUE_TABLE_LENGTH>>1)-1);

							indx = value;
						}

						// Use the packed version of the codebook entry
						codeword = table[indx].entry & VLE_CODEWORD_MASK;
						codesize = table[indx].entry >> VLE_CODESIZE_SHIFT;

						//void PutBits(BITSTREAM *stream, uint32_t  wBits, int nBits)
						{
							int nBits = codesize;
							int wBits = codeword;

							// Will the bits fit in the int32_t buffer?
							if (nBits <= nBitsFree) {
								wBuffer <<= nBits;
								wBuffer |= (wBits & BITMASK(nBits));
								nBitsFree -= nBits;
							}
							else
							{
								// Fill the buffer with as many bits as will fit
								wBuffer <<= nBitsFree;
								nBits -= nBitsFree;

								// Insert as many bits as will fit into the buffer
								wBuffer |= (wBits >> nBits) & BITMASK(nBitsFree);

								nWordsUsed += nWordsPerLong;

								*(lpCurrentWord++) = SwapInt32NtoB(wBuffer);

								wBuffer = wBits & BITMASK(nBits);
								nBitsFree = BITSTREAM_LONG_SIZE - nBits;
							}

						}

					}
					index++;
				}

				// Add the row gap onto the encoding length
				if (index == width)
					count += gap;

			}

			// Move the current word into a int32_t buffer
			stream->wBuffer = wBuffer;
			// Number of bits remaining in the current word
			stream->nBitsFree = nBitsFree;
			stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
			stream->nWordsUsed = nWordsUsed;

			// Advance to the next row
			rowptr += pitch;
		}

		// Need to output a pending run of zeros?
		if (count > 0)
		{
			PutZeroRun(stream, count, runsbook);
			count = 0; //Fix required for two pass, so the second pass has a count of zero.
		}

		if(pass == 1)
		{
			// Append the band end codeword to the encoded coefficients
			FinishEncodeBand(stream,encoder->band_end_code[active_codebook],encoder->band_end_size[active_codebook]);
			PutVideoBandMidPoint2Pass(stream);
			rowptr = image;
		}
	}
}




#if 1	// New version that inlines subroutines into this routine

void EncodeQuantLongRuns(ENCODER *encoder, BITSTREAM *stream, PIXEL *image,
						 int width, int height, int pitch, int divisor, int active_codebook)
{
	//FILE *logfile = encoder->logfile;
	RLCBOOK *runsbook;
//	VLCBOOK *magsbook = encoder->codebook[encoder->active_codebook]->magbook;
	VALBOOK *valuebook;
	PIXEL *rowptr = image;
	//RUN run = {0, 0};
	int row;
	//int column;
	int gap;
	int count = 0;

#if (0 && DEBUG)
	static int file_count = 0;
	char pathname[PATH_MAX];
	FILE *file = NULL;
	sprintf(pathname, "C:/Users/bschunck/Temp/Encode1/encode-%02d.dat", ++file_count);
	file = fopen(pathname, "w");
#endif

#if _STATS
	int current = (int)stream->cntBits;
#endif

	//CODEC_STATE *codec = &encoder->codec;
	//int subband = codec->band.subband;

	runsbook = encoder->codebook_runbook[active_codebook]; //DAN20150817
	valuebook = encoder->valuebook[active_codebook]; //DAN20041026

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	// Compute the number of pixels in the gap at the end of each row
	gap = (pitch - width);

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Encoding width: %d, height: %d, gap: %d\n",
				width, height, gap);
	}
#endif

#if (1 && TRACE_PUTBITS)
	TraceEncodeBand(width, height);
#endif

	for (row = 0; row < height; row++)
	{
		int index = 0;			// Start at the beginning of the row
		//int zeros;
		int indx;
		int runsbook_length = runsbook->length;
		//int valuebooklength = valuebook->length;
		RLC *rlc = (RLC *)((char *)runsbook + sizeof(RLCBOOK));
		VLE *table = (VLE *)((char *)valuebook + sizeof(VALBOOK));
#if 1
		uint32_t  wBuffer;
		int nBitsFree;
		//int cntBits;
		const int nWordsPerLong = sizeof(uint32_t )/sizeof(uint8_t );
		uint32_t  *lpCurrentWord = (uint32_t  *)(stream->lpCurrentWord);
		int nWordsUsed = stream->nWordsUsed;
		//uint32_t  *lpStartWord = lpCurrentWord;

		// Move the current word into a int32_t buffer
		wBuffer = stream->wBuffer;
		// Number of bits remaining in the current word
		nBitsFree = stream->nBitsFree;
//		cntBits = stream->cntBits;
#endif
#if (0 && DEBUG)
		stream->logfile = file;
		stream->putbits_flag = true;
		fprintf(file, "EncodeQuantLongRuns row: %d\n", row);
#endif
		// Search the row for runs of zeros and nonzero values
		while (index < width)
		{
			// Loop invariant
			assert(0 <= index && index < width);

			// Search the rest of the row for a nonzero value
			for (; index < width; index++) {
				if (rowptr[index] == 0) count++;
				else break;
			}

			// Need to output a value?
			if (index < width)
			{
				PIXEL value = rowptr[index];

				// Need to output a run of zeros before this value?
				if (count > 0)
				{
					//PutZeroRun(stream, count, runsbook);
#if (0 && DEBUG)
					fprintf(file, "PutZeroRun: %d\n", count);
#endif
					while (count > 0)
					{
						// Index into the codebook to get a run length code that covers most of the run
						indx = (count < runsbook_length) ? count : runsbook_length - 1;

						// Output the run length code
						//void PutBits(BITSTREAM *stream, uint32_t  wBits, int nBits)
						{
							int nBits = rlc[indx].size;
							int wBits = rlc[indx].bits;

							if (nBits <= nBitsFree)
							{
								wBuffer <<= nBits;
								wBuffer |= (wBits & BITMASK(nBits));
								nBitsFree -= nBits;
#if (1 && TRACE_PUTBITS)
								TracePutBits(nBits);
#endif
							}
							else
							{
								// Fill the buffer with as many bits as will fit
								wBuffer <<= nBitsFree;
								nBits -= nBitsFree;
#if (0 && DEBUG)
								// Count the number of bits written to the bitstream
								stream->cntBits += nBitsFree;
#endif
								// Insert as many bits as will fit into the buffer
								wBuffer |= (wBits >> nBits) & BITMASK(nBitsFree);
#if (1 && TRACE_PUTBITS)
								TracePutBits(nBitsFree);
#endif
								// Insert all of the bytes in the buffer into the bitstream
								//PutLong(stream, wBuffer);
								//void PutLong(BITSTREAM *stream, uint32_t  word)
								{
									nWordsUsed += nWordsPerLong;

								//	if (nWordsUsed <= stream->dwBlockLength) {
										//*(lpCurrentWord++) = _bswap(wBuffer);
										*(lpCurrentWord++) = SwapInt32NtoB(wBuffer);
									//	stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
									//	stream->nWordsUsed = nWordsUsed;
								//	}
								//	else {
								//		stream->error = BITSTREAM_ERROR_OVERFLOW;
								//	}
								}

								wBuffer = wBits & BITMASK(nBits);
								nBitsFree = BITSTREAM_LONG_SIZE - nBits;
							}
						}
						// Reduce the length of the run by the amount output
						count -= rlc[indx].count;
					}

					count = 0;
				}

				//PutVlcByte(stream, value, valuebook);
				{
					uint32_t codeword;
					uint32_t codesize;

					//DAN20050914 -- This fixes large positive numbers (peaks) overflowing as negative non-peak value
					if(value < 0)
					{
						if(value <= -(VALUE_TABLE_LENGTH>>1))
							value = -((VALUE_TABLE_LENGTH>>1)-1);

						indx = VALUE_TABLE_LENGTH + value;
					}
					else
					{
						if(value >= (VALUE_TABLE_LENGTH>>1))
							value = ((VALUE_TABLE_LENGTH>>1)-1);

						indx = value;
					}

					/*
					if(value < 0)
						indx = VALUE_TABLE_LENGTH + value;
					else
						indx = value;

					if (indx < 0) indx = 0;
					else if (indx >= valuebooklength) indx = valuebooklength - 1;*/

					// Use the packed version of the codebook entry
					codeword = table[indx].entry & VLE_CODEWORD_MASK;
					codesize = table[indx].entry >> VLE_CODESIZE_SHIFT;
					//void PutBits(BITSTREAM *stream, uint32_t  wBits, int nBits)
					{
						int nBits = codesize;
						int wBits = codeword;

						if (nBits <= nBitsFree) {
							wBuffer <<= nBits;
							wBuffer |= (wBits & BITMASK(nBits));
							nBitsFree -= nBits;
#if (1 && TRACE_PUTBITS)
							TracePutBits(nBits);
#endif
						}
						else {
							// Fill the buffer with as many bits as will fit
							wBuffer <<= nBitsFree;
							nBits -= nBitsFree;
#if (0 && DEBUG)
							// Count the number of bits written to the bitstream
							stream->cntBits += nBitsFree;
#endif
							// Insert as many bits as will fit into the buffer
							wBuffer |= (wBits >> nBits) & BITMASK(nBitsFree);
#if (1 && TRACE_PUTBITS)
							TracePutBits(nBitsFree);
#endif
							// Insert all of the bytes in the buffer into the bitstream
							//PutLong(stream, wBuffer);
							//void PutLong(BITSTREAM *stream, uint32_t  word)
							{
								nWordsUsed += nWordsPerLong;

							//	if (nWordsUsed <= stream->dwBlockLength) {
									//*(lpCurrentWord++) = _bswap(wBuffer);
									*(lpCurrentWord++) = SwapInt32NtoB(wBuffer);
								//	stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
								//	stream->nWordsUsed = nWordsUsed;
							//	}
							//	else {
							//		stream->error = BITSTREAM_ERROR_OVERFLOW;
							//	}
							}

							wBuffer = wBits & BITMASK(nBits);
							nBitsFree = BITSTREAM_LONG_SIZE - nBits;
						}

#if (0 && DEBUG)
						// Count the number of bits written to the bitstream
						stream->cntBits += nBits;
#endif
					}
				}
				index++;
			}

			// Add the row gap onto the encoding length
			if (index == width) count += gap;

		}
#if 1
		// Move the current word into a int32_t buffer
		stream->wBuffer = wBuffer;
		// Number of bits remaining in the current word
		stream->nBitsFree = nBitsFree;
//		stream->cntBits = cntBits;
		stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
		stream->nWordsUsed = nWordsUsed;
#endif

		// Advance to the next row
		rowptr += pitch;
	}

	// Need to output a pending run of zeros?
	if (count > 0)
	{
#if (0 && DEBUG)
		fprintf(file, "PutZeroRun: %d\n", count);
#endif
		PutZeroRun(stream, count, runsbook);

#if _STATS
		CountRuns(STATS_DEFAULT, count);
//		CountValues(STATS_DEFAULT, 0, count);
		{
			int i;
			for(i=0; i<count; i++)
			{
				CountValues(STATS_DEFAULT, 0, 1);
			}
		}
#endif
	}

#if (0 && DEBUG)
	stream->logfile = NULL;
	stream->putbits_flag = false;
	fclose(file);
#endif

#if _STATS
	// Update the file of run length and value statistics
	UpdateStats(STATS_DEFAULT);

	NewSubBand(width,height,0,((int)stream->cntBits) - current, current - stats_lastbits);

	stats_lastbits = (int)stream->cntBits;
#endif
}

#else		// Original code before inlining the calls to PutBits

#if PREFETCH
DWORD dummy1 = 0;
#endif

// Quantize and encode highpass coefficients and use run length
// coding for runs of zeros that may extend across multiple rows
void EncodeQuantLongRuns(ENCODER *encoder, BITSTREAM *stream, PIXEL *image,
						 int width, int height, int pitch, int divisor, int active_codebook)
{
	FILE *logfile = encoder->logfile;
	RLCBOOK *runsbook = encoder->codebook->runbook;
	VLCBOOK *magsbook = encoder->codebook->magbook;
	VALBOOK *valuebook = encoder->valuebook;
	PIXEL *rowptr = image;
	//RUN run = {0, 0};
	int row, column;
	int gap;
	int count = 0;

#if _STATS
	int current = (int)stream->cntBits;
#endif

#if (0 && PREFETCH)

	// Prefetch the array of highpass coefficients
	size_t size = height * pitch;
	const int cache_line_size = 64;
	int num_cache_lines = size / cache_line_size;
	uint8_t *prefetch = (uint8_t *)image;
	DWORD dummy2 = 0;
	num_cache_lines--;
	for (; num_cache_lines > 0; num_cache_lines -= 4)
	{
		dummy2 += *((DWORD *)&prefetch[(num_cache_lines - 0) * cache_line_size]);
		dummy2 += *((DWORD *)&prefetch[(num_cache_lines - 1) * cache_line_size]);
		dummy2 += *((DWORD *)&prefetch[(num_cache_lines - 2) * cache_line_size]);
		dummy2 += *((DWORD *)&prefetch[(num_cache_lines - 3) * cache_line_size]);
	}
	dummy1 += dummy2;

#endif

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	// Compute the number of pixels in the gap at the end of each row
	gap = (pitch - width);

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Encoding width: %d, height: %d, gap: %d\n",
				width, height, gap);
	}
#endif

	for (row = 0; row < height; row++)
	{
		int index = 0;			// Start at the beginning of the row

		// Search the row for runs of zeros and nonzero values
		while (index < width)
		{
			// Loop invariant
			assert(0 <= index && index < width);

			// Search the rest of the row for a nonzero value
			for (; index < width; index++) {
				if (rowptr[index] == 0) count++;
				else break;
			}

			// Need to output a value?
			if (index < width) {
				PIXEL value = rowptr[index];
				assert(value != 0);

				// Need to output a run of zeros before this value?
				if (count > 0)
				{
					PutZeroRun(stream, count, runsbook);

#if _STATS
					CountRuns(STATS_DEFAULT, count);
//					CountValues(STATS_DEFAULT, 0, count);
					{
						int i;
						for(i=0; i<count; i++)
						{
							CountValues(STATS_DEFAULT, 0, 1);
						}
					}
#endif
					count = 0;
				}

#if (0 && DEBUG)
				// Check for highpass coefficients that will be saturated
				if (!(SCHAR_MIN <= value && value <= SCHAR_MAX)) stream->nSaturated++;
#endif

#if _COMPANDING_MORE
				PutVlcByte(stream, value, valuebook);
#else
				if (value > 125)
					value = 125; // Hack to fix weird overflow

				PutVlcByte(stream, SATURATE_8S(value), valuebook);
#endif
				CountValues(STATS_DEFAULT, value, 1);

				index++;
			}

			// Add the row gap onto the encoding length
			if (index == width) count += gap;
		}

		// Should have processed the entire row
		assert(index == width);

		// Advance to the next row
		rowptr += pitch;
	}

	// Need to output a pending run of zeros?
	if (count > 0)
	{
		PutZeroRun(stream, count, runsbook);

#if _STATS
		CountRuns(STATS_DEFAULT, count);
//		CountValues(STATS_DEFAULT, 0, count);
		{
			int i;
			for(i=0; i<count; i++)
			{
				CountValues(STATS_DEFAULT, 0, 1);
			}
		}
#endif
	}

#if _STATS
	// Update the file of run length and value statistics
	UpdateStats(STATS_DEFAULT);

	NewSubBand(width,height,0,((int)stream->cntBits) - current, current - stats_lastbits);

	stats_lastbits = (int)stream->cntBits;
#endif
}
#endif

void EncodeQuant16s(ENCODER *encoder, BITSTREAM *stream, PIXEL *image,
					int width, int height, int pitch, int divisor)
{
	//FILE *logfile = encoder->logfile;
	PIXEL *rowptr = image;
	int row;
#if (0 && DEBUG)
	static int low32=0,low256=0,total=0;
#endif

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Encoding width: %d, height: %d\n", width, height);
	}
#endif

	//BAND_ENCODING_16BIT
	for (row = 0; row < height; row++)
	{
		int column;

#if 0
		for (column = 0; column < width; column++)
		{
			int value = rowptr[column];
			PutWord16s(stream, value);

          #if (0 && DEBUG)
			if(abs(value) < 32) low32++;
			if(abs(value) < 256) low256++;
			total++;
          #endif
		}

#else // Mild speedup (about 2%)
		char *sptr = (char *)stream->lpCurrentWord;
		char *dptr = (char *)rowptr;
		for (column = 0; column < width; column++)
		{
			*sptr++ = *(dptr+1) ;
			*sptr++ = *dptr;
			dptr+=2;
		}

		stream->lpCurrentWord += width*2;
		stream->nWordsUsed += width*2;
#endif
		// Advance to the next row
		rowptr += pitch;
	}

#if (0 && DEBUG)
	{
		FILE *fp = fopen("c:/tempstats.txt","w");
		fprintf(fp,"low32 = %d, low256 %d\n", low32 * 100/total, low256 * 100/total);
		fclose(fp);
	}
#endif
}



#if _HIGHPASS_CODED

// Encode a band of quantized coefficients
void EncodeQuantizedCoefficients(ENCODER *encoder, BITSTREAM *stream, PIXEL *input, int length,
								 int gap, int *zero_count_ptr, bool output_runs_flag)
{
	RLCBOOK *runsbook = encoder->codebook->runbook;
	//VLCBOOK *magsbook = encoder->codebook->magbook;
	VALBOOK *valuebook = encoder->valuebook;
	PIXEL *rowptr = input;
	//RUN run = {0, 0};
	//int row, column;
	//int gap;
	int width = length;
	//int count = 0;
	int count = *zero_count_ptr;

	// Check that the count is not negative
	assert(count >= 0);

#if (1 && _STATS)
	int current = (int)stream->cntBits;
#endif

	// Compute the number of pixels in the gap at the end of each row
	//gap = (pitch - width);

	//for (row = 0; row < height; row++)
	{
		int index = 0;			// Start at the beginning of the row

		// Search the row for runs of zeros and nonzero values
		while (index < width)
		{
			// Loop invariant
			assert(0 <= index && index < width);

			// Search the rest of the row for a nonzero value
			for (; index < width; index++) {
				if (rowptr[index] == 0) count++;
				else break;
			}

			// Need to output a value?
			if (index < width) {
				PIXEL value = rowptr[index];
				assert(value != 0);

				// Need to output a run of zeros before this value?
				if (count > 0)
				{
					PutZeroRun(stream, count, runsbook);

#if _STATS
					CountRuns(STATS_DEFAULT, count);
//					CountValues(STATS_DEFAULT, 0, count);
					{
						int i;
						for(i=0; i<count; i++)
						{
							CountValues(STATS_DEFAULT, 0, 1);
						}
					}
#endif

					count = 0;
				}

#if (0 && DEBUG)
				// Check for highpass coefficients that will be saturated
				if (!(SCHAR_MIN <= value && value <= SCHAR_MAX)) stream->nSaturated++;
#endif

#if _COMPANDING_MORE
				PutVlcByte(stream, value, valuebook);
#else
				if (value > 125)
					value = 125; // Hack to fix weird overflow

				PutVlcByte(stream, SATURATE_8S(value), valuebook);
#endif
				CountValues(STATS_DEFAULT, value, 1);

				index++;
			}

			// Add the row gap onto the encoding length
			if (index == width) count += gap;
		}

		// Should have processed the entire row
		assert(index == width);

		// Advance to the next row
		//rowptr += pitch;
	}

	// Should the last run of zeros be output?
	if (output_runs_flag)
	{
		// Need to output a pending run of zeros?
		if (count > 0)
		{
			PutZeroRun(stream, count, runsbook);

#if _STATS
			CountRuns(STATS_DEFAULT, count);
//			CountValues(STATS_DEFAULT, 0, count);
			{
				int i;
				for(i=0; i<count; i++)
				{
					CountValues(STATS_DEFAULT, 0, 1);
				}
			}
#endif
			count = 0;
		}

		// Check that the zero run has been output
		assert(count == 0);
	}

#if (1 && _STATS)
	// Update the file of run length and value statistics
	UpdateStats(STATS_DEFAULT);

	NewSubBand(width,height,0,((int)stream->cntBits) - current, current - stats_lastbits);

	stats_lastbits = (int)stream->cntBits;
#endif

	// Save the count of zeros at the end of the row
	*zero_count_ptr = count;
}

// Copy highpass coefficients that have already been encoded to the bitstream
void EncodeQuantCodedRuns(ENCODER *encoder, BITSTREAM *stream, uint8_t *buffer, size_t size, int divisor)
{
	FILE *logfile = encoder->logfile;
	int nWordsOutput = size/sizeof(uint8_t );
	uint8_t  *lpCurrentWord = stream->lpCurrentWord;
	int nWordsUsed = stream->nWordsUsed + nWordsOutput;

	// Check that the size is a multiple of the bitstream word size
	assert((size % sizeof(uint32_t )) == 0);

	// Check that the bitstream is aligned on a tag boundary
	assert(IsAlignedTag(stream));

	// Check that there is nothing in the bit buffer
	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	// Check that there is room in the block for the int32_t word
	assert(nWordsUsed <= stream->dwBlockLength);

	// Copy the encoded coefficients into the stream
	if (nWordsUsed <= stream->dwBlockLength)
	{
		memcpy(lpCurrentWord, buffer, size);
		stream->lpCurrentWord += nWordsOutput;
		stream->nWordsUsed = nWordsUsed;
	}
	else
	{
#if DEBUG
		fprintf(logfile, "Blocklength error; words used: %d, block length: %d\n",
			stream->nWordsUsed, stream->dwBlockLength);
#endif
		stream->error = BITSTREAM_ERROR_OVERFLOW;
	}

	// Check that the stream is still aligned on a tag boundary
	assert(IsAlignedTag(stream));
}

#endif

#if 0
void EncodeZeroRun(ENCODER *encoder, BITSTREAM *stream, int count)
{
	RLCBOOK *runsbook = encoder->codebook->runbook;

	if (count > 0)
		PutZeroRun(stream, count, runsbook);
}
#endif

int ComputeHighpassHash(PIXEL *image, int width, int height, int pitch)
{
	int hash = 0;
	int row, column;
	PIXEL *rowptr = image;

	for(row = 0; row < height; row++, rowptr += pitch)
		for(column = 0; column < width; column++)
			hash += rowptr[column];

	hash = hash % 1000;
	if (hash < 0) hash += 1000;

	return hash;
}


int SetCodingFlags(ENCODER *encoder, int subband, int *active_codebook_ret, int *peaks_coding_ret)
{
//#if BUILD_PROSPECT //10-bit for everyone
	int active_codebook = 1; // use the deeper table for everything for highest quality yet the bitrate will climb (like in P2K.)
//#else
//	int active_codebook = 0;
//#endif
	int difference_coding = 0;
	int peaks_coding = 0;

#if (1 && CODEC_NUM_CODESETS >= 2)
	if(encoder->progressive)
	{
		if(encoder->gop_length == 2 && (subband >= 7 && subband <= 10)) // use the deeper table for temporal difference.
		{
			active_codebook = 1;

			#if DIFFERENCE_TEMPORAL_LL
			if(subband == 7)
				difference_coding = 1;
			#endif
		}
	}
	else // interlace encoder use a special codebook for LowHori-HighVert subbands
	{
		if(encoder->gop_length == 2 && ((subband >= 7 && subband <= 10) || subband == 12 || subband == 15))
		{
			active_codebook = 1;

			#if DIFFERENCE_CODING
			if(subband == 12 || subband == 15)// && encoder->codec.precision == 10)
			{
				difference_coding = 1;
				active_codebook = 2;
				peaks_coding = 1;
			}
			#endif

			#if DIFFERENCE_TEMPORAL_LL
			if(subband == 7)
				difference_coding = 1;
			#endif

		}
		else if(encoder->gop_length == 1 && subband == 8)
		{
			active_codebook = 1;
			#if DIFFERENCE_CODING
		//	if(encoder->codec.precision == 10)
				difference_coding = 1;
				active_codebook = 2;
				peaks_coding = 1;
			#endif
		}
	}


	if(subband < MAX_QUANT_SUBBANDS && encoder->q.codebookflags[subband]) //non zero means there is a overide for this subband
	{
		int flags = encoder->q.codebookflags[subband];

#if DIFFERENCE_CODING
		if(flags & CBFLAG_DIFFCODE)
		{
			difference_coding = 1;
			active_codebook = 2;
			peaks_coding = 1;
		} else
#endif
		if(flags & CBFLAG_PEAKCODE)
		{
			active_codebook = 2;
			peaks_coding = 1;
		} else
		if(flags & CBFLAG_TABLMASK)
		{
			active_codebook = flags & CBFLAG_TABLMASK;
		}
	}

#endif

#if !DIFFERENCE_CODING
	difference_coding = 0;
#endif

#if LOSSLESS
	//LOSSLESS
	active_codebook = 2;
	peaks_coding = 1;
#endif

	*active_codebook_ret = active_codebook;
	*peaks_coding_ret = peaks_coding;

	return active_codebook + (difference_coding<<4);
}



void EncodeZeroBand(ENCODER *encoder, BITSTREAM *stream, IMAGE *wavelet,
						 int band, int subband, int encoding, int quantization)
{
	//FILE *logfile = encoder->logfile;
	int width;
	int height;
	int scale;
	int divisor;
	//int level = wavelet->level;
	CODEC_STATE *codec = &encoder->codec;
	int active_codebook = 0;
	int peaks_coding = 0;
	int codingflags = 0;
	//int peakscounter = 0;
	//int peak_offset_tag = 0;

#if DEBUG
	BITCOUNT bitcount = 0;
#endif

	// This routine only implements runlength encoding
	assert(encoding == BAND_ENCODING_RUNLENGTHS);

	// Check that the band index is valid
	assert(0 <= band && band < wavelet->num_bands);	// band can be the "lowlow" band in field+ transform

	// Check that the quantization divisor is valid
	assert(quantization > 0);

	// Encode the portion of the band that is valid wavelet transform output
	width = wavelet->width;
	height = wavelet->height;

	codec->band.subband = subband; //DAN20041031

	codingflags = SetCodingFlags(encoder, subband, &active_codebook, &peaks_coding);
	peaks_coding = 0;


	// Get the scaling parameters set by the wavelet transform
	scale = wavelet->scale[band];
	//divisor = wavelet->divisor[band];
	divisor = 0;


#if _CODEC_TAGS
	// Check that the band header starts on a tag boundary
	assert(IsAlignedTag(stream));
#endif


	// Output the band header
	PutVideoBandHeader(stream, band, width, height, subband, encoding,
					   quantization, scale, divisor, NULL, codingflags, peaks_coding);

#if _CODEC_TAGS
	// Check that the band data starts on a tag boundary
	assert(IsAlignedTag(stream));
#else
	// The FSM decoder requires that subband data start on a byte boundary
	PadBits(stream);
#endif



#if _ENCODE_LONG_RUNS
	// This routine only handles 16-bit pixels
	assert(wavelet->pixel_type[band] == PIXEL_TYPE_16S);


#if (0 && DEBUG)
	START_BITCOUNT(stream, bitcount);
#endif

	EncodeZeroLongRuns(encoder, stream, wavelet->band[band], width, height, wavelet->pitch, 1, active_codebook);

#if (0 && DEBUG)
	STOP_BITCOUNT(stream, bitcount);

	//if (logfile && debug['b']) {
	if (logfile) {
		fprintf(logfile, "Subband: %d, bitcount: %I64d, ss1: %.0f, ss2: %.0f\n", subband, bitcount, ss1, ss2);
	}
#endif


#else
#error Encoding method not implemented
#endif

	// Append the band end codeword to the encoded coefficients
	FinishEncodeBand(stream,encoder->band_end_code[active_codebook],encoder->band_end_size[active_codebook]);

	// Output the band trailer
	PutVideoBandTrailer(stream);
}

// Encode a band of highpass coefficients that have been quantized to signed words
void EncodeQuantizedBand(ENCODER *encoder, BITSTREAM *stream, IMAGE *wavelet,
						 int band, int subband, int encoding, int quantization)
{
	//FILE *logfile = encoder->logfile;
	int width;
	int height;
	int scale;
	int divisor;
	//int level = wavelet->level;
	CODEC_STATE *codec = &encoder->codec;
	int active_codebook = 0;
	int peaks_coding = 0;
	int codingflags = 0;
	int peakscounter = 0;
	int peak_offset_tag = 0;

#if DEBUG
	BITCOUNT bitcount = 0;
#endif

	// This routine only implements runlength encoding
	assert(encoding == BAND_ENCODING_RUNLENGTHS);

	// Check that the band index is valid
	assert(0 <= band && band < wavelet->num_bands);	// band can be the "lowlow" band in field+ transform

	// Check that the quantization divisor is valid
	assert(quantization > 0);

	// Encode the portion of the band that is valid wavelet transform output
	width = wavelet->width;
	height = wavelet->height;

	codec->band.subband = subband; //DAN20041031

	codingflags = SetCodingFlags(encoder, subband, &active_codebook, &peaks_coding);

	// Get the scaling parameters set by the wavelet transform
	scale = wavelet->scale[band];
	//divisor = wavelet->divisor[band];
	divisor = 0;


#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "\nEncode quant band: %d, subband: %d, quantization: %d\n", band, subband, quantization);
		DumpBandRow(wavelet->band[band], min(32, wavelet->width), wavelet->pixel_type[band], logfile);
	}
#endif

#if (0 && DEBUG)
	if (logfile)
		fprintf(logfile, "Start encoder subband: %d, stream: 0x%X\n", subband, stream->lpCurrentWord);
#endif

#if _CODEC_TAGS
	// Check that the band header starts on a tag boundary
	assert(IsAlignedTag(stream));
#endif

	// Output the band header
	PutVideoBandHeader(stream, band, width, height, subband, encoding,
					   quantization, scale, divisor, NULL, codingflags, peaks_coding);

	if(peaks_coding)
	{
		peak_offset_tag = stream->nWordsUsed - 20;// step back over CODEC_TAG_PEAK_TABLE_OFFSET_(L|H), CODEC_TAG_PEAK_LEVEL, CODEC_TAG_SUBBAND_SIZE & CODEC_TAG_BAND_HEADER
	}

#if _CODEC_TAGS
	// Check that the band data starts on a tag boundary
	assert(IsAlignedTag(stream));
#else
	// The FSM decoder requires that subband data start on a byte boundary
	PadBits(stream);
#endif

#if (0 && DEBUG)
	if (logfile)
		fprintf(logfile, "Encoder subband: %d, stream: 0x%X\n", subband, stream->lpCurrentWord);
#endif

#if _ENCODE_LONG_RUNS
	// This routine only handles 16-bit pixels
	assert(wavelet->pixel_type[band] == PIXEL_TYPE_16S);

#if (0 && DEBUG)
	if (logfile) {
		int width = wavelet->width;
		int height = wavelet->height;
		fprintf(logfile,
				"Before long encoding wavelet subband: %d, width: %d, height: %d, band: %d, quantization: %d\n",
				subband, width, height, band, quantization);
		DumpWaveletRow(wavelet, band, 0, logfile);
	}
#endif

#if (0 && DEBUG)
	// Clear the count of output values that were saturated
	stream->nSaturated = 0;
#endif

#if (0 && DEBUG)
	START_BITCOUNT(stream, bitcount);
#endif


#if _PACK_RUNS_IN_BAND_16S
	if (level == 1)
	{
		EncodeQuantPackedLongRuns(encoder, stream, wavelet->band[band], width, height, wavelet->pitch, 1);
	}
	else
	{
		EncodeQuantLongRuns(encoder, stream, wavelet->band[band], width, height, wavelet->pitch, 1, active_codebook);
	}
#else
#if 0
	ss1 = BandEnergy(wavelet->band[band], width, height, wavelet->pitch, band, subband);
//	if (subband == 12 || subband == 15) {
//		FilterHorizontalDelta(wavelet->band[band], width, height, wavelet->pitch);
//	}
//	ss2 = BandEnergy(wavelet->band[band], width, height, wavelet->pitch);
#endif


	if(peaks_coding)
	{
#if (0 && DEBUG)
		if (logfile) {
			PIXEL *rowptr = wavelet->band[band];
			int i;
			fprintf(logfile, "EncodeQuantizedBand subband: %d\n", subband);

			for (i = 0; i < 16; i++) {
				fprintf(logfile, "%5d", rowptr[i]);
			}
			fprintf(logfile, "\n");
		}
#endif
		peakscounter = EncodeQuantLongRunsPlusPeaks(encoder, stream, wavelet->band[band], width, height, wavelet->pitch, 1, active_codebook, quantization);
	}
	else
	{
#if (0 && DEBUG)
		if (encoder->encoded_band_bitstream)
		{
			BITSTREAM *bitstream = encoder->encoded_band_bitstream;
			const int frame = (int)encoder->frame_number;
			const int channel = encoder->encoded_band_channel;
			const int wavelet_index = encoder->encoded_band_wavelet;
			const int band_index = encoder->encoded_band_number;
			//const int width = wavelet->width;
			//const int height = wavelet->height;
			void *data;
			size_t size;

			uint32_t channel_mask = 0x01;
			uint32_t wavelet_mask = 0x04;
			uint32_t band_mask = 0x08;

			EncodeQuantLongRuns(encoder, bitstream, wavelet->band[band], width, height, wavelet->pitch, 1, active_codebook);

			if ((((1 << channel) & channel_mask) != 0) &&
				(((1 << wavelet_index) & wavelet_mask) != 0) &&
				(((1 << band_index) & band_mask) != 0))
			{
				data = bitstream->lpCurrentBuffer;
				size = bitstream->nWordsUsed;

				WriteWaveletBand(&encoder->encoded_band_file, frame, channel, wavelet_index,
					band_index, BAND_TYPE_ENCODED_RUNLENGTHS, width, height, data, size);
			}

			RewindBitstream(bitstream);
		}
		else
#endif
		{
			EncodeQuantLongRuns(encoder, stream, wavelet->band[band], width, height, wavelet->pitch, 1, active_codebook);
		}
	}
#endif

#if (0 && DEBUG)
	STOP_BITCOUNT(stream, bitcount);

	//if (logfile && debug['b']) {
	if (logfile) {
		fprintf(logfile, "Subband: %d, bitcount: %I64d, ss1: %.0f, ss2: %.0f\n", subband, bitcount, ss1, ss2);
	}
#endif

#if (0 && DEBUG)
	// Output the saturation count
	if (logfile) {
		int saturation_count = stream->nSaturated;
		if (saturation_count > 0)
			fprintf(logfile, "Subband: %d, saturation count: %d\n", subband, saturation_count);
		else
			fprintf(logfile, "Subband: %d, no saturation\n", subband);
	}
#endif

#if (0 && DEBUG)
	if (logfile) {
		int width = wavelet->width;
		int height = wavelet->height;
		fprintf(logfile,
				"After long encoding wavelet subband: %d, width: %d, height: %d, band: %d, quantization: %d\n",
				subband, width, height, band, quantization);
		DumpWaveletRow(wavelet, band, 1, logfile);
	}
#endif

#else
#error Encoding method not implemented
#endif

	// Append the band end codeword to the encoded coefficients
	FinishEncodeBand(stream,encoder->band_end_code[active_codebook],encoder->band_end_size[active_codebook]);

	// Output the band trailer
	PutVideoBandTrailer(stream);

	if(peakscounter)
	{
	//	FILE *fp = fopen("c:/peaks.txt","a");
	//	fprintf(fp,"peakscounter = %d\n", peakscounter);
	//	fclose(fp);
		uint32_t *peakptr = (uint32_t *)wavelet->band[band];
		int peakscounterroundedup = peakscounter;
		if(peakscounter & 1) // odd value zero out the last entry (4 byte alignment)
		{
			peakptr[peakscounter>>1] &= 0xffff;
			peakscounterroundedup = peakscounter + 1;
		}
//		PadBitsTag(stream);

		if((peakscounterroundedup/2) <= MAX_CHUNK_SIZE)
		{

			// Right back in the stream the offset the peak data.
			{
				BITSTREAM streamcopy = *stream;
				streamcopy.lpCurrentWord -= stream->nWordsUsed - peak_offset_tag;
				streamcopy.nWordsUsed = peak_offset_tag;
				PutTagPair(&streamcopy, OPTIONALTAG(CODEC_TAG_PEAK_TABLE_OFFSET_L), (stream->nWordsUsed - peak_offset_tag) & 0xffff);
				PutTagPair(&streamcopy, OPTIONALTAG(CODEC_TAG_PEAK_TABLE_OFFSET_H), (stream->nWordsUsed - peak_offset_tag)>>16);
				PutTagPair(&streamcopy, OPTIONALTAG(CODEC_TAG_PEAK_LEVEL), PEAK_THRESHOLD * quantization);
			}

			//Peak tables structure
			//  CODEC_TAG_PEAK_TABLE|(chunk size) // chunk size = ((num + 1)&0xfffffe)
			//  table of 16bit values
			//  two byte pad if necessary
			PutTagPair(stream, OPTIONALTAG(CODEC_TAG_PEAK_TABLE), peakscounterroundedup/2);
		//	while(peakscounterroundedup > 0)
		//	{
		//		PutLong(stream, *peakptr++);
		//		peakscounterroundedup -= 2;
		//	}

			// Ouput table directly -- faster than PutLong or PutBits etc.
			memcpy(stream->lpCurrentWord, peakptr, peakscounterroundedup*2);
			stream->nWordsUsed += peakscounterroundedup*2;
			stream->lpCurrentWord += peakscounterroundedup*2;
		}
		else
		{
			assert(0);
		}
	}


#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Encoder: hash value for this highpass band is %d\n",
			ComputeHighpassHash(wavelet->band[band], width, height, wavelet->pitch/sizeof(PIXEL)));
	}
#endif

#if (0 && DEBUG)
	if (logfile)
		fprintf(logfile, "Finish encoder subband: %d, stream: 0x%X\n", subband, stream->lpCurrentWord);
#endif
}

// Encode an empty band. This is called to code the temporal highpass band in the field+ transform.
void EncodeEmptyQuantBand(ENCODER *encoder, BITSTREAM *stream, IMAGE *wavelet,
						  int band, int subband, int encoding, int quantization)
{
	//FILE *logfile = encoder->logfile;

	int width;
	int height;
	int scale;
	int divisor;
	int active_codebook = 0;
	int peaks_coding = 0;
	int codingflags = 0;

	// Check that the band index is valid
	assert(0 <= band && band < wavelet->num_bands);

	// Check that the quantization divisor is valid
	assert(quantization > 0);

	// Encode the portion of the band that is valid wavelet transform output
	width = wavelet->width;
	height = wavelet->height;

	codingflags = SetCodingFlags(encoder, subband, &active_codebook, &peaks_coding);


	// Get the scaling parameters set by the wavelet transform
	scale = wavelet->scale[band];
	//divisor = wavelet->divisor[band];
	divisor = 0;

#if (0 && DEBUG)
	{
		FILE *logfile = encoder->logfile;
		fprintf(logfile, "\nEncode quant band: %d, subband: %d, quantization: %d\n", band, subband, quantization);
		DumpBandRow(wavelet->band[band], min(32, wavelet->width), wavelet->pixel_type[band], logfile);
	}
#endif

#if (0 && DEBUG)
	if (logfile)
		fprintf(logfile, "Start encode empty band, stream: 0x%X\n", stream->lpCurrentWord);
#endif

	// This routine only supports runlength encoding
	assert(encoding == BAND_ENCODING_RUNLENGTHS);

	// Output the band header
	PutVideoBandHeader(stream, band, width, height, subband, encoding,
					   quantization, scale, divisor, NULL, codingflags, false);

	// The finite state machine decoder requires that subband data start on a byte boundary
	PadBits(stream);

	// Append the band end codeword to the encoded coefficients
	//FinishEncodeBand(stream,,encoder->band_end_code[active_codebook],encoder->band_end_size[active_codebook]);

	// Output the band trailer
	PutVideoBandTrailer(stream);

	// The finite state machine decoder ends the subband on a byte boundary
	PadBits(stream);

#if 0
	fprintf(encoder->logfile, "Encoder: hash value for this highpass band is %d\n",
		ComputeHighpassHash(wavelet->band[band], width, height, wavelet->pitch/sizeof(PIXEL)));
#endif

#if (0 && DEBUG)
	if (logfile)
		fprintf(logfile, "Finish encoding empty band, stream: 0x%X\n", stream->lpCurrentWord);
#endif
}

// Encode a band of highpass coefficients that have been quantized to signed words
void EncodeQuantizedBand16s(ENCODER *encoder, BITSTREAM *stream, IMAGE *wavelet,
							int band, int subband, int encoding, int quantization)
{
	//FILE *logfile = encoder->logfile;
	int width;
	int height;
	int scale;
	int divisor;
	//int level = wavelet->level;
	int active_codebook = 0;
	int peaks_coding = 0;
	int codingflags = 0;

	// This routine only implements runlength encoding
	assert(encoding == BAND_ENCODING_16BIT);

	// Check that the band index is valid
	//assert(0 <= band && band < wavelet->num_bands);
	assert(band == 0);

	// Check that the quantization divisor is valid
	//assert(quantization > 0);
	//assert(quantization == 0);

	// Do not quantize the band
//	quantization = 0;

	// Encode the portion of the band that is valid wavelet transform output
	width = wavelet->width;
	height = wavelet->height;

	codingflags = SetCodingFlags(encoder, subband, &active_codebook, &peaks_coding);


	// Get the scaling parameters set by the wavelet transform
	scale = wavelet->scale[band];
	//divisor = wavelet->divisor[band];
	divisor = 0;

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "\nEncode quant band: %d, subband: %d, quantization: %d\n", band, subband, quantization);
		DumpBandRow(wavelet->band[band], min(32, wavelet->width), wavelet->pixel_type[band], logfile);
	}
#endif

#if (0 && DEBUG)
	if (logfile)
		fprintf(logfile, "Start encoder subband: %d, stream: 0x%X\n", subband, stream->lpCurrentWord);
#endif

#if _CODEC_TAGS
	// Check that the band header starts on a tag boundary
	assert(IsAlignedTag(stream));
#endif

	// Output the band header
	PutVideoBandHeader(stream, band, width, height, subband, encoding,
					   quantization, scale, divisor, NULL, codingflags, false);

#if _CODEC_TAGS
	// Check that the band data starts on a tag boundary
	assert(IsAlignedTag(stream));
#else
	// The FSM decoder requires that subband data start on a byte boundary
	PadBits(stream);
#endif

#if (0 && DEBUG)
	if (logfile)
		fprintf(logfile, "Encoder subband: %d, stream: 0x%X\n", subband, stream->lpCurrentWord);
#endif

	// This routine only handles 16-bit pixels
	assert(wavelet->pixel_type[band] == PIXEL_TYPE_16S);

#if (0 && DEBUG)
	if (logfile) {
		int width = wavelet->width;
		int height = wavelet->height;
		fprintf(logfile,
				"Before direct encoding wavelet subband: %d, width: %d, height: %d, band: %d, quantization: %d\n",
				subband, width, height, band, quantization);
		DumpWaveletRow(wavelet, band, 0, logfile);
	}
#endif

#if (0 && DEBUG)
	// Clear the count of output values that were saturated
	stream->nSaturated = 0;
#endif

#if (0 && DEBUG)
	{
		int static count = 0;
		if (count < 20) {
			char label[_MAX_PATH];
			sprintf(label, "Hightemp-encode-%d-", count);
			DumpBandPGM(label, wavelet, band, NULL);
		}
		count++;
	}
#endif

	EncodeQuant16s(encoder, stream, wavelet->band[band], width, height, wavelet->pitch, 1);

#if (0 && DEBUG)
	// Output the saturation count
	if (logfile) {
		int saturation_count = stream->nSaturated;
		if (saturation_count > 0)
			fprintf(logfile, "Subband: %d, saturation count: %d\n", subband, saturation_count);
		else
			fprintf(logfile, "Subband: %d, no saturation\n", subband);
	}
#endif

#if (0 && DEBUG)
	if (logfile) {
		int width = wavelet->width;
		int height = wavelet->height;
		fprintf(logfile,
				"After long encoding wavelet subband: %d, width: %d, height: %d, band: %d, quantization: %d\n",
				subband, width, height, band, quantization);
		DumpWaveletRow(wavelet, band, 1, logfile);
	}
#endif

	// Append the band end codeword to the encoded coefficients
	FinishEncodeBand(stream,encoder->band_end_code[active_codebook],encoder->band_end_size[active_codebook]);

	// Output the band trailer
	PutVideoBandTrailer(stream);

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Encoder: hash value for this highpass band is %d\n",
			ComputeHighpassHash(wavelet->band[band], width, height, wavelet->pitch/sizeof(PIXEL)));
	}
#endif

#if (0 && DEBUG)
	if (logfile)
		fprintf(logfile, "Finish encoder subband: %d, stream: 0x%X\n", subband, stream->lpCurrentWord);
#endif
}



// Encode a band of highpass coefficients that have been quantized to signed words
void EncodeBand16sLossless(ENCODER *encoder, BITSTREAM *stream, IMAGE *wavelet,
							int band, int subband, int encoding, int quantization)
{
	//FILE *logfile = encoder->logfile;
	int width;
	int height;
	int scale;
	int divisor;
	//int level = wavelet->level;
	int active_codebook = 0;
	//int peaks_coding = 0;
	int codingflags = 0;

	// This routine only implements runlength encoding
	assert(encoding == BAND_ENCODING_LOSSLESS);

	// Check that the band index is valid
	//assert(0 <= band && band < wavelet->num_bands);
	assert(band == 0);

	// Encode the portion of the band that is valid wavelet transform output
	width = wavelet->width;
	height = wavelet->height;

	codingflags = active_codebook = 2;//;SetCodingFlags(encoder, subband, &active_codebook, &peaks_coding);

	// Get the scaling parameters set by the wavelet transform
	scale = wavelet->scale[band];
	//divisor = wavelet->divisor[band];
	divisor = 0;


	// This is a need hack that tests if the import data was 8-bit yet 10-bit compressed is applied.
	//The reason to do this is often the bottom two bits are not used -- therefor way encode them
	{
		//PIXEL mask = 0;
		//int x;
		int pitch = wavelet->pitch / sizeof(PIXEL);

		if(quantization > 1)
		{
			PIXEL* pix = wavelet->band[band];
			int y;
			for(y=0;y<height;y++)
			{
				QuantizeRow16s((PIXEL16S *)pix, width, quantization);
				pix += pitch;
			}
		}
	}



	// Output the band header
	PutVideoBandHeader(stream, band, width, height, subband, encoding,
					   quantization, scale, divisor, NULL, codingflags, false);

#if _CODEC_TAGS
	// Check that the band data starts on a tag boundary
	assert(IsAlignedTag(stream));
#else
	// The FSM decoder requires that subband data start on a byte boundary
	PadBits(stream);
#endif

	// This routine only handles 16-bit pixels
	assert(wavelet->pixel_type[band] == PIXEL_TYPE_16S);

	EncodeQuantLongRuns2Pass(encoder, stream, wavelet->band[band], width, height, wavelet->pitch, 1, active_codebook);

	// Append the band end codeword to the encoded coefficients
	FinishEncodeBand(stream,encoder->band_end_code[active_codebook],encoder->band_end_size[active_codebook]);

	// Output the band trailer
	PutVideoBandTrailer(stream);
}


#if _HIGHPASS_CODED

// Write a band of highpass coefficients that has already been encoded to the bitstream
void EncodeCodedBand(ENCODER *encoder, BITSTREAM *stream, IMAGE *wavelet,
					 int band, int subband, int encoding, int quantization)
{
	FILE *logfile = encoder->logfile;
	int width;
	int height;
	int scale;
	int divisor;

	// This routine only implements runlength encoding
	assert(encoding == BAND_ENCODING_RUNLENGTHS);

	// Check that the band index is valid
	assert(0 <= band && band < wavelet->num_bands);	// band can be the "lowlow" band in field+ transform

	// Check that the quantization divisor is valid
	assert(quantization > 0);

	// Encode the portion of the band that is valid wavelet transform output
	width = wavelet->width;
	height = wavelet->height;

	codingflags = SetCodingFlags(encoder, subband, &active_codebook, &peaks_coding);

	// Get the scaling parameters set by the wavelet transform
	scale = wavelet->scale[band];
	//divisor = wavelet->divisor[band];
	divisor = 0;

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "\nEncode quant band: %d, subband: %d, quantization: %d\n", band, subband, quantization);
		DumpBandRow(wavelet->band[band], min(32, wavelet->width), wavelet->pixel_type[band], logfile);
	}
#endif

#if (0 && DEBUG)
	if (logfile)
		fprintf(logfile, "Start encoder subband: %d, stream: 0x%X\n", subband, stream->lpCurrentWord);
#endif

#if _CODEC_TAGS
	// Check that the band header starts on a tag boundary
	assert(IsAlignedTag(stream));
#endif

	// Output the band header
	PutVideoBandHeader(stream, band, width, height, subband, encoding,
					   quantization, scale, divisor, NULL, codingflags, false);

#if _CODEC_TAGS
	// Check that the band data starts on a tag boundary
	assert(IsAlignedTag(stream));
#else
	// The FSM decoder requires that subband data start on a byte boundary
	PadBits(stream);
#endif

#if (0 && DEBUG)
	if (logfile)
		fprintf(logfile, "Encoder subband: %d, stream: 0x%X\n", subband, stream->lpCurrentWord);
#endif

#if _ENCODE_LONG_RUNS
	// This routine only handles bands that have already been encoded
	assert(wavelet->pixel_type[band] == PIXEL_TYPE_CODED);

#if (0 && DEBUG)
	if (logfile) {
		int width = wavelet->width;
		int height = wavelet->height;
		fprintf(logfile,
				"Before long encoding wavelet subband: %d, width: %d, height: %d, band: %d, quantization: %d\n",
				subband, width, height, band, quantization);
		DumpWaveletRow(wavelet, band, 0, logfile);
	}
#endif

#if (0 && DEBUG)
	// Clear the count of output values that were saturated
	stream->nSaturated = 0;
#endif

	// Should not be using a band that has not already been encoded
	assert(wavelet->coded_size[band] > 0);

	// Copy the encoded band to the output bitstream
	EncodeQuantCodedRuns(encoder, stream, (uint8_t *)wavelet->band[band], wavelet->coded_size[band], 1);

#if (0 && DEBUG)
	// Output the saturation count
	if (logfile) {
		int saturation_count = stream->nSaturated;
		if (saturation_count > 0)
			fprintf(logfile, "Subband: %d, saturation count: %d\n", subband, saturation_count);
		else
			fprintf(logfile, "Subband: %d, no saturation\n", subband);
	}
#endif

#if (0 && DEBUG)
	if (logfile) {
		int width = wavelet->width;
		int height = wavelet->height;
		fprintf(logfile,
				"After long encoding wavelet subband: %d, width: %d, height: %d, band: %d, quantization: %d\n",
				subband, width, height, band, quantization);
		DumpWaveletRow(wavelet, band, 1, logfile);
	}
#endif

#else
#error Encoding method not implemented
#endif

	// Append the band end codeword to the encoded coefficients
	//FinishEncodeBand(stream,,encoder->band_end_code[active_codebook],encoder->band_end_size[active_codebook]);

	// Output the band trailer
	PutVideoBandTrailer(stream);

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Encoder: hash value for this highpass band is %d\n",
			ComputeHighpassHash(wavelet->band[band], width, height, wavelet->pitch/sizeof(PIXEL)));
	}
#endif

#if (0 && DEBUG)
	if (logfile)
		fprintf(logfile, "Finish encoder subband: %d, stream: 0x%X\n", subband, stream->lpCurrentWord);
#endif
}

#endif

// Reverse the order of all 4 bytes in a 32-bit integer
int ReverseByteOrder(int input)
{
	int output;

#if 0
	output  = (input & 0x000000FF) << 24;
	output |= (input & 0x0000FF00) << 8;
	output |= (input & 0x00FF0000) >> 8;
	output |= (input & 0xFF000000) >> 24;
#else
	//output = _bswap(input);
	output = SwapInt32(input);
#endif

	return output;
}

#if 0

// New routine for encoding group transform (derived from EncodeQuant by removing quantization)
// assumes that the band has already been quantized and the coefficients are signed bytes or runs
void EncodeQuantizedGroup(ENCODER *encoder, TRANSFORM *transform[], int num_transforms, BITSTREAM *output)
{
	FILE *logfile = encoder->logfile;
	int num_channels;
	int subband_count;
	int channel;
	int k;
	uint8_t  *temp;

#if _CODEC_TAGS
	DWORD *channel_size_vector;		// Pointer to vector of channel sizes
#else
	int *channel_size[3];
#endif

	// Count the subbands as they are encoded
	int subband = 0;

	// Verify that the codebooks are valid
#if DEBUG
	assert(ValidCodebooks());
#endif

	// Verify that there are three channels
	assert(num_transforms == 3);

	START(tk_encoding);

	// Output the header for the group of frames
	num_channels = num_transforms;
	subband_count = SubbandCount(transform[0]);
	temp = output->lpCurrentWord;

#if _CODEC_TAGS
	PutVideoGroupHeader(output, transform[0], num_channels, subband_count, &channel_size_vector,
						encoder->encoder_quality, encoder->encoded_format);
#else
	PutVideoGroupHeader(output, transform[0], num_channels, subband_count, NULL);
#endif

#if _CODEC_TAGS
	// The location of the channel size vector was returned
	// by the routine that encoded the group header
#else
	// Remember the positions of the channel size fields
	channel_size[2] = (int *)(output->lpCurrentWord - 4);
	channel_size[1] = channel_size[2] - 1;
	channel_size[0] = channel_size[1] - 1;
#endif

	// Write the optional tags in the group header extension
	PutVideoGroupExtension(output, &encoder->codec);

#if _CODEC_SAMPLE_FLAGS
	// Write the flag bits that encode the codec state
	PutVideoSampleFlags(output, &encoder->codec);
#endif

	for (channel = 0; channel < num_channels; channel++)
	{
		int num_wavelets = transform[channel]->num_wavelets;
		IMAGE *lowpass;
		bool temporal_runs_encoded = true;
		int k;
		int channel_size_in_byte;

#if (0 && DEBUG)
		if (logfile) {
			char label[256];
			sprintf(label, "Channel %d transform", channel);
			DumpTransform(label, transform[channel], logfile);
			fprintf(logfile, "\n");
		}
#endif
		// Align start of channel on a bitword boundary
		PadBits(output);

		// Output a channel header between channels
		if (channel > 0) {
			PutVideoChannelHeader(output, channel);
		}

		// Remember the beginning of the channel data
		channel_size_in_byte = BitstreamSize(output);

		// Get the wavelet that contains the lowpass band that will be encoded
		lowpass = transform[channel]->wavelet[num_wavelets - 1];
#if 0
		// Compute the lowpass band statistics used for encoding
		ComputeLowPassStatistics(encoder, lowpass);
#endif
		// Encode the lowest resolution image from the top of the wavelet pyramid
		EncodeLowPassBand(encoder, output, lowpass, channel, subband++);

		switch(transform[channel]->type)
		{
		case TRANSFORM_TYPE_FIELDPLUS:	// Field+ transform
			// Start at the top of the wavelet pyramid
			// Encode the two spatial transforms from the temporal lowpass band
			for (k = num_wavelets - 1; k >= num_wavelets - 2; k--)
			{
				IMAGE *wavelet = transform[channel]->wavelet[k];
				int wavelet_type = wavelet->wavelet_type;
				int wavelet_level = wavelet->level;
				int wavelet_number = k + 1;
				int num_highpass_bands = wavelet->num_bands - 1;
				//int encoding_method = BAND_ENCODING_CODEBOOK;
				int encoding_method = BAND_ENCODING_RUNLENGTHS;
				int encoding_order[] = {LH_BAND, HL_BAND, HH_BAND};
				int quantization;
				//int divisor = wavelet->divisor[0];
				int divisor = 0;
				int i;

				// Output the header for the high pass bands at this level
				PutVideoHighPassHeader(output, wavelet_type, wavelet_number, wavelet_level,
									   wavelet->width, wavelet->height, wavelet->num_bands,
									   wavelet->scale[0], divisor);

				assert(wavelet_type == WAVELET_TYPE_SPATIAL);

				// Use run length coding for runs of zeros and individual codes
				// for the magnitude and sign if the coefficient is not zero
				for (i = 0; i < num_highpass_bands; i++) {
					int band = encoding_order[i];
					//quantization = encoder->quant[channel][subband];
					//quantization = 1;
					quantization = wavelet->quantization[band];
#if (0 && DEBUG)
					if (logfile) {
						fprintf(logfile, "Encoding channel: %d, subband: %d, wavelet: %d\n", channel, subband, k);
					}
#endif
#if (0 && TIMING)
					if (logfile) {
						int scale = wavelet->scale[band];
						fprintf(logfile,
								"Encoding channel: %d, subband: %d, scale: %d, quantization: %d\n",
								channel, subband, scale, quantization);
					}
#endif
					// Encode the band without performing quantization
					EncodeQuantizedBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
					++subband;
				}

				// Output the trailer for the highpass bands
				PutVideoHighPassTrailer(output, 0, 0, 0, 0, 0);

				//NOTE: Need to change the statistics output to something appropriate
			}

			// Encode the spatial transform from the temporal highpass band
			{
				IMAGE *wavelet = transform[channel]->wavelet[k];
				int wavelet_type = wavelet->wavelet_type;
				int wavelet_level = wavelet->level;
				int wavelet_number = k + 1;
				int num_highpass_bands = wavelet->num_bands;
				//int encoding_method = BAND_ENCODING_CODEBOOK;
				int encoding_method = BAND_ENCODING_RUNLENGTHS;
				int encoding_order[] = {LL_BAND, LH_BAND, HL_BAND, HH_BAND};
				int quantization;
				//int divisor = wavelet->divisor[0];
				int divisor = 0;
				int i;

				// Output the header for the high pass bands at this level
				PutVideoHighPassHeader(output, wavelet_type, wavelet_number, wavelet_level,
									   wavelet->width, wavelet->height, wavelet->num_bands,
									   wavelet->scale[0], divisor);

				assert(wavelet_type == WAVELET_TYPE_SPATIAL);

				// Use run length coding for runs of zeros and individual codes
				// for the magnitude and sign if the coefficient is not zero
				for (i = 0; i < num_highpass_bands; i++)
				{
					int band = i;
					quantization = wavelet->quantization[band];
#if (0 && DEBUG)
					if (logfile) {
						fprintf(logfile, "Encoding channel: %d, subband: %d, wavelet: %d\n", channel, subband, k);
					}
#endif
#if (0 && TIMING)
					if (logfile) {
						int scale = wavelet->scale[band];
						fprintf(logfile,
								"Encoding channel: %d, subband: %d, scale: %d, quantization: %d\n",
								channel, subband, scale, quantization);
					}
#endif
					// Encode band without performing quantization
					EncodeQuantizedBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
					++subband;
				}

				// Output the trailer for the highpass bands
				PutVideoHighPassTrailer(output, 0, 0, 0, 0, 0);

				k--;
				//NOTE: Need to change the statistics output to something appropriate
			}

			// Encode the temporal transform as an empty band
			{
				IMAGE *wavelet = transform[channel]->wavelet[k];
				int wavelet_type = wavelet->wavelet_type;
				int wavelet_level = wavelet->level;
				int wavelet_number = k + 1;
				int num_highpass_bands = wavelet->num_bands - 1;
				//int encoding_method = BAND_ENCODING_CODEBOOK;
				int encoding_method = BAND_ENCODING_RUNLENGTHS;
				int encoding_order[] = {LH_BAND, HL_BAND, HH_BAND};
				int quantization = 1;
				//int divisor = wavelet->divisor[0];
				int divisor = 0;
				int i;

				// Output the header for the high pass bands at this level
				PutVideoHighPassHeader(output, wavelet_type, wavelet_number, wavelet_level,
									   wavelet->width, wavelet->height, wavelet->num_bands,
									   wavelet->scale[0], divisor);

				// Check that the wavelet type is valid
				assert(wavelet_type == WAVELET_TYPE_TEMPORAL);

				// Check that number of highpass bands is 1
				assert(num_highpass_bands == 1);


				// Encode the temporal highpass as an empty band

				// Set the subband index to (unsigned char) -1, i.e., 255
				EncodeEmptyQuantBand(encoder, output, wavelet, 1, 255, encoding_method, quantization);

				// Output the trailer for the highpass bands
				PutVideoHighPassTrailer(output, 0, 0, 0, 0, 0);

				k--;
				//NOTE: Need to change the statistics output to something appropriate
			}

			// Encode the two field transforms
			for (; k >= 0; k--)
			{
				IMAGE *wavelet = transform[channel]->wavelet[k];
				int wavelet_type = wavelet->wavelet_type;
				int wavelet_level = wavelet->level;
				int wavelet_number = k + 1;
				int num_highpass_bands = wavelet->num_bands - 1;
				//int encoding_method = BAND_ENCODING_CODEBOOK;
				int encoding_method = BAND_ENCODING_RUNLENGTHS;
				int encoding_order[] = {LH_BAND, HL_BAND, HH_BAND};
				int quantization;
				//int divisor = wavelet->divisor[0];
				int divisor = 0;
				int i;

				// Output the header for the high pass bands at this level
				PutVideoHighPassHeader(output, wavelet_type, wavelet_number, wavelet_level,
									   wavelet->width, wavelet->height, wavelet->num_bands,
									   wavelet->scale[0], divisor);

				assert(wavelet_type == WAVELET_TYPE_HORZTEMP);

				// Use run length coding for runs of zeros and individual codes
				// for the magnitude and sign if the coefficient is not zero
				for (i = 0; i < num_highpass_bands; i++)
				{
					int band = encoding_order[i];
					quantization = wavelet->quantization[band];
#if (0 && DEBUG)
					if (logfile) {
						fprintf(logfile, "Encoding channel: %d, subband: %d, wavelet: %d\n", channel, subband, k);
					}
#endif
#if (0 && TIMING)
					if (logfile) {
						int scale = wavelet->scale[band];
						fprintf(logfile,
								"Encoding channel: %d, subband: %d, scale: %d, quantization: %d\n",
								channel, subband, scale, quantization);
					}
#endif
					// Encode band without performing quantization
					EncodeQuantizedBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
					++subband;
				}

				// Output the trailer for the highpass bands
				PutVideoHighPassTrailer(output, 0, 0, 0, 0, 0);

				//NOTE: Need to change the statistics output to something appropriate
			}

			break;

		case TRANSFORM_TYPE_FIELD:	// Field transform

			// Start at the top of the wavelet pyramid
			for (k = num_wavelets - 1; k >= 0; k--)
			{
				IMAGE *wavelet = transform[channel]->wavelet[k];
				int wavelet_type = wavelet->wavelet_type;
				int wavelet_level = wavelet->level;
				int wavelet_number = k + 1;
				int num_highpass_bands = wavelet->num_bands - 1;
				//int encoding_method = BAND_ENCODING_CODEBOOK;
				int encoding_method = BAND_ENCODING_RUNLENGTHS;
				int encoding_order[] = {LH_BAND, HL_BAND, HH_BAND};
				int quantization;
				//int divisor = wavelet->divisor[0];
				int divisor = 0;
				int i;

				// Output the header for the high pass bands at this level
				PutVideoHighPassHeader(output, wavelet_type, wavelet_number, wavelet_level,
									   wavelet->width, wavelet->height, wavelet->num_bands,
									   wavelet->scale[0], divisor);

				// Use run length coding for runs of zeros and individual codes
				// for the magnitude and sign if the coefficient is not zero
				for (i = 0; i < num_highpass_bands; i++)
				{
					int band = encoding_order[i];
					quantization = wavelet->quantization[band];

					// Encode band without performing quantization
					EncodeQuantizedBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
					++subband;
				}

				// Output the trailer for the highpass bands
				PutVideoHighPassTrailer(output, 0, 0, 0, 0, 0);

				//NOTE: Need to change the statistics output to something appropriate
			}

			break;

			default:
				assert(0);	// Can only handle field or field+ transforms now
				break;
			}

			// Should have processed all subbands
			// Fix this assertion after deciding whether the number of subbands
			// is defined
			//assert(subband == encoder->num_subbands);

			// Output a chanel trailer?

			// Align end of channel on a bitword boundary
			PadBits(output);

#if _CODEC_TAGS
			// Compute the number of bytes used for encoding this channel
			channel_size_in_byte = BitstreamSize(output) - channel_size_in_byte;

#if (0 && DEBUG)
			if (logfile) {
				fprintf(logfile, "Channel: %d, size: %d (bytes)\n", channel, channel_size_in_byte);
			}
#endif
			// Write the number of bytes used to code this channel in the channel size table
			channel_size_vector[channel] = ReverseByteOrder(channel_size_in_byte);
#else
			// Remember the number of bytes used to code this channel
			channel_size_in_byte = BitstreamSize(output) - channel_size_in_byte;
			*channel_size[channel] = ReverseByteOrder(channel_size_in_byte);
#endif

			// Output a channel trailer?

			// Start numbering the subbands in the next channel beginning with zero
			subband = 0;
		}

	// Output the trailer for the group of frames
	PutVideoGroupTrailer(output);

	STOP(tk_encoding);
}

#elif _CODEC_TAGS


// Simplified routine for encoding the group transform
void EncodeQuantizedGroup(ENCODER *encoder, TRANSFORM *transform[], int num_transforms, BITSTREAM *output)
{
	//FILE *logfile = encoder->logfile;
	bool encode_iframe;
	int num_channels;
	int subband_count;
	int channel;
	//int k;
	//uint8_t  *finished_output = output->lpCurrentWord;
	//uint8_t  *temp;

	uint32_t *channel_size_vector;		// Pointer to vector of channel sizes

	// Count the subbands as they are encoded
	int subband = 0;
	int unc_size = 3 * encoder->unc_frame.width * 4 * encoder->unc_frame.display_height / 2;

	if(encoder->unc_origformat == COLOR_FORMAT_V210)
		unc_size = ((((encoder->unc_frame.width + 47)/48)*48) * 8 / 3) * encoder->unc_frame.display_height;

	if(	encoder->unc_origformat == COLOR_FORMAT_DPX0 ||
		encoder->unc_origformat == COLOR_FORMAT_RG30 ||
		encoder->unc_origformat == COLOR_FORMAT_R210 ||
		encoder->unc_origformat == COLOR_FORMAT_AR10 ||
		encoder->unc_origformat == COLOR_FORMAT_AB10)
		unc_size = encoder->unc_frame.width * 4 * encoder->unc_frame.display_height;


	// Verify that the codebooks are valid
#if DEBUG
	assert(ValidCodebooks());;
#endif

	// Verify that there are three channels
	//assert(num_transforms == 3);	//DAN06302004

	START(tk_encoding);

#if (DEBUG && _WIN32)
	//OutputDebugString("EncodeQuantizedGroup");
#endif

//	if(encoder->uncompressed)
//	{
//		int align = (int)finished_output & 0xf;
//		output->lpCurrentWord = encoder->unc_buffer+align;
//		output->lpCurrentBuffer = encoder->unc_buffer+align;
//	}

	num_channels = num_transforms;
	subband_count = SubbandCount(transform[0]);
	//temp = output->lpCurrentWord;

	// Increment the sequence number of the encoded frame
	encoder->frame_number++;

	if (encoder->gop_length > 1)
	{
		uint32_t frame_number = encoder->frame_number;
		int precision = encoder->codec.precision;

		// Output the header for the group of frames
		PutVideoGroupHeader(output, transform[0], num_channels, subband_count,
							&channel_size_vector, precision, frame_number,
							encoder->input.format, encoder->input.color_space,
							encoder->encoder_quality, encoder->encoded_format,
							encoder->input.width, encoder->input.height, encoder->display.height,
							encoder->presentationWidth, encoder->presentationHeight);
		encode_iframe = false;
	}
	else
	{
		uint32_t frame_number = encoder->frame_number;
		int precision = encoder->codec.precision;

		assert(encoder->gop_length == 1);

		// Output the header for an intra frame
		PutVideoIntraFrameHeader(output, transform[0], num_channels, subband_count,
								 &channel_size_vector, precision, frame_number,
								 encoder->input.format, encoder->input.color_space,
								 encoder->encoder_quality, encoder->encoded_format,
                                 encoder->input.width, encoder->input.height, encoder->display.height,
								 encoder->presentationWidth, encoder->presentationHeight);
		encode_iframe = true;
	}

	if(encoder->video_channels > 1)
	{
		PutTagPairOptional(output, CODEC_TAG_ENCODED_CHANNELS, encoder->video_channels);
		PutTagPairOptional(output, CODEC_TAG_ENCODED_CHANNEL_NUMBER, encoder->current_channel);
	}
	else if(encoder->current_channel || encoder->ignore_overrides)  //always set the channel number in ignore_overrides is 1
	{
		PutTagPairOptional(output, CODEC_TAG_ENCODED_CHANNEL_NUMBER, encoder->current_channel);
	}

	//	Put Sample size field here.
	SizeTagPush(output, CODEC_TAG_SAMPLE_SIZE);  // moved, used to be after Metadata, //DAN20081216


	if (encoder->metadata.global.block && encoder->metadata.global.size)
	{
		int len = (int)encoder->metadata.global.size;
		len = RemoveHiddenMetadata((unsigned char *)encoder->metadata.global.block, (int)encoder->metadata.global.size);

		if(len)
		{
			PutTagPairOptional(output, CODEC_TAG_METADATA, len>>2);

			// Output table directly -- faster than PutLong or PutBits etc.
			memcpy(output->lpCurrentWord, encoder->metadata.global.block, len);
			output->nWordsUsed += len;
			output->lpCurrentWord += len;
		}
	}

	if (encoder->metadata.local.block && encoder->metadata.local.size)
	{
		int len = (int)encoder->metadata.local.size;
		len = RemoveHiddenMetadata((unsigned char *)encoder->metadata.local.block, (int)encoder->metadata.local.size);

		if(len)
		{
			PutTagPairOptional(output, CODEC_TAG_METADATA, len>>2);

			// Ouput table directly -- faster than PutLong or PutBits etc.
			memcpy(output->lpCurrentWord, encoder->metadata.local.block, len);
			output->nWordsUsed += len;
			output->lpCurrentWord += len;
		}
	}

#define FREE_META_SIZE 512
	if (FREE_META_SIZE) // Create freespace, TODO make that controllable
	{
		uint32_t *ptr;
		PutTagPairOptional(output, CODEC_TAG_METADATA, (FREE_META_SIZE)>>2);

		ptr = (uint32_t *)output->lpCurrentWord;

		*ptr++ = TAG_FREESPACE;
		*ptr++ = FREE_META_SIZE-8;
		memset(ptr,0,FREE_META_SIZE-8);

		//output->nWordsUsed += FREE_META_SIZE-8;
		//output->lpCurrentWord += FREE_META_SIZE-8;
		output->nWordsUsed += FREE_META_SIZE;
		output->lpCurrentWord += FREE_META_SIZE;
	}


	// Write the optional tags in the group header extension
	PutVideoGroupExtension(output, &encoder->codec);

	//	Put Sample size field here.
	//SizeTagPush(output, CODEC_TAG_SAMPLE_SIZE); //DAN20081216 moved up to before metadata.

#if _CODEC_SAMPLE_FLAGS
	// Write the flag bits that encode the codec state
	PutVideoSampleFlags(output, &encoder->codec);
#endif


	if(encoder->uncompressed)
	{
		//uncompressed
		uint32_t *ptr;
		unsigned short tag = CODEC_TAG_UNCOMPRESS;
		int size = (unc_size)>>2;
		int alignment = ((uintptr_t)output->lpCurrentWord) & 0xf;
		alignment += 4;

		if(tag & 0x2000) // 24bit chunks
		{
			tag |= (size >> 16);
			size &= 0xffff;
		}
		else // 16bit chunks
		{
			size &= 0xffff;
		}


		while(alignment & 0xc)
		{
			PutLong(output, ((uint32_t )(-CODEC_TAG_SKIP) << 16) | 0);
			alignment += 4;
		}


		PutLong(output, ((uint32_t )tag << 16) | (size & CODEC_TAG_MASK));

/*		{
			int overlappinglines = (output->nWordsUsed * 4 / encoder->unc_pitch) + 4;
			uint8_t * newbase;

			if(overlappinglines)
			{
				if(encoder->unc_origformat == COLOR_FORMAT_BYR3)
					unc_size = ConvertBYR3ToPacked(encoder->unc_data, encoder->unc_pitch,
						encoder->unc_frame.width, overlappinglines, (uint8_t *)output->lpCurrentWord);
				else if(encoder->unc_origformat == COLOR_FORMAT_BYR4)
					unc_size = ConvertBYR4ToPacked(encoder->unc_data, encoder->unc_pitch,
						encoder->unc_frame.width, overlappinglines, (uint8_t *)output->lpCurrentWord, encoder->bayer.format);
				else if(encoder->unc_origformat == COLOR_FORMAT_BYR5)
				{
					unc_size = 0;
				}

				output->nWordsUsed += unc_size;
				output->lpCurrentWord += unc_size;
			}

			newbase = encoder->unc_data;
			newbase += overlappinglines * encoder->unc_pitch;
			ptr = (uint32_t *)(finished_output + output->nWordsUsed);

			if(encoder->unc_origformat == COLOR_FORMAT_BYR3)
				unc_size = ConvertBYR3ToPacked(newbase, encoder->unc_pitch,
					encoder->unc_frame.width, encoder->unc_frame.display_height-overlappinglines, (uint8_t *)ptr);
			else if(encoder->unc_origformat == COLOR_FORMAT_BYR4)
				unc_size = ConvertBYR4ToPacked(newbase, encoder->unc_pitch,
					encoder->unc_frame.width, encoder->unc_frame.display_height-overlappinglines, (uint8_t *)ptr, encoder->bayer.format);
			else if(encoder->unc_origformat == COLOR_FORMAT_BYR5)
			{
			}

			memcpy(finished_output, output->lpCurrentBuffer, output->nWordsUsed);

			output->lpCurrentWord = (uint8_t  *)ptr;
			output->lpCurrentBuffer = finished_output;

			output->nWordsUsed += unc_size;
			output->lpCurrentWord += unc_size;
		}
		*/

		ptr = (uint32_t *)output->lpCurrentWord;
		if(encoder->unc_origformat == COLOR_FORMAT_BYR3)
			unc_size = ConvertBYR3ToPacked(encoder->unc_data, encoder->unc_pitch,
				encoder->unc_frame.width, encoder->unc_frame.display_height, (uint8_t *)ptr);
		else if(encoder->unc_origformat == COLOR_FORMAT_BYR4)
			unc_size = ConvertBYR4ToPacked(encoder->unc_data, encoder->unc_pitch,
				encoder->unc_frame.width, encoder->unc_frame.display_height, (uint8_t *)ptr, encoder->bayer.format);
		else if(encoder->unc_origformat == COLOR_FORMAT_RG30 ||
				encoder->unc_origformat == COLOR_FORMAT_R210 ||
				encoder->unc_origformat == COLOR_FORMAT_AR10 ||
				encoder->unc_origformat == COLOR_FORMAT_AB10)
		{
			ConvertRGB10ToDPX0(encoder->unc_data, encoder->unc_pitch,
							   encoder->unc_frame.width, encoder->unc_frame.display_height,
							   encoder->unc_origformat);
			if(encoder->uncompressed & 2)
			{
				// No Store so
				// do nothing
			}
			else
			{
				memcpy(ptr, encoder->unc_data, unc_size);
			}
		}
		else if(encoder->unc_origformat == COLOR_FORMAT_BYR5 || 
				encoder->unc_origformat == COLOR_FORMAT_V210 ||
				encoder->unc_origformat == COLOR_FORMAT_DPX0)
		{
			if(encoder->uncompressed & 2)
			{
				// No Store so
				// do nothing
			}
			else
			{
				memcpy(ptr, encoder->unc_data, unc_size);
			}
		}

		output->nWordsUsed += unc_size;
		output->lpCurrentWord += unc_size;
	}
	else
	{

#if (0 && _DEBUG)
		const char *pathname = "C:/Users/bschunck/Temp/entropy1.dat";
		CreateEncodedBandFile(encoder, pathname);
#endif
		for (channel = 0; channel < num_channels; channel++)
		{
			int num_wavelets = transform[channel]->num_wavelets;
			IMAGE *lowpass;
			//bool temporal_runs_encoded = true;
			//int k;
			int channel_size_in_byte;

	#if (0 && DEBUG)
			if (logfile) {
				fprintf(logfile, "Encoding channel: %d\n", channel);
			}
	#endif
	#if (0 && DEBUG)
			if (logfile) {
				char label[256];
				sprintf(label, "Channel %d transform", channel);
				DumpTransform(label, transform[channel], logfile);
				fprintf(logfile, "\n");
			}
	#endif
#if (1 && TRACE_PUTBITS)
			TraceEncodeChannel(channel);
#endif
			// Align start of channel on a bitword boundary
			PadBits(output);

			// Output a channel header between channels
			if (channel > 0) {
				PutVideoChannelHeader(output, channel);
			}

			// Remember the beginning of the channel data
			channel_size_in_byte = BitstreamSize(output);

			// Get the wavelet that contains the lowpass band that will be encoded
			lowpass = transform[channel]->wavelet[num_wavelets - 1];
	#if 0
			// Compute the lowpass band statistics used for encoding
			ComputeLowPassStatistics(encoder, lowpass);
	#endif
			// Encode the lowest resolution image from the top of the wavelet pyramid
			EncodeLowPassBand(encoder, output, lowpass, channel, subband++);

			switch(transform[channel]->type)
			{
			case TRANSFORM_TYPE_SPATIAL:
#if _DEBUG
				encoder->encoded_band_channel = channel;
#endif
				EncodeQuantizedFrameTransform(encoder, transform[channel], output, channel);
				break;

			case TRANSFORM_TYPE_FIELD:
				EncodeQuantizedFieldTransform(encoder, transform[channel], output, channel);
				break;

			case TRANSFORM_TYPE_FIELDPLUS:
				EncodeQuantizedFieldPlusTransform(encoder, transform[channel], output, channel);
				break;

			default:
				assert(0);	// Can only handle field or field+ transforms now
				break;
			}

			// Should have processed all subbands.  Fix this assertion after deciding
			// whether the number of subbands is defined in the encoder structure
			//assert(subband == encoder->num_subbands);

			// Output a channel trailer?

			// Align end of channel on a bitword boundary
			PadBits(output);

			// Compute the number of bytes used for encoding this channel
			channel_size_in_byte = BitstreamSize(output) - channel_size_in_byte;

	#if (0 && DEBUG)
			if (logfile) {
				fprintf(logfile, "Channel: %d, size: %d (bytes)\n", channel, channel_size_in_byte);
			}
	#endif
			// Write the number of bytes used to code this channel in the channel size table
			channel_size_vector[channel] = ReverseByteOrder(channel_size_in_byte);

			// Start numbering the subbands in the next channel beginning with zero
			subband = 0;
		}

#if (0 && DEBUG)
		CloseEncodedBandFile(encoder);
#endif
	}

	if(!(encoder->uncompressed & 2)) // only hdr is written
	{
#if (DEBUG && _WIN32)
		//OutputDebugString("pop 1");
#endif
		if (encode_iframe)
		{
			// Output the trailer for an intra frame
			PutVideoIntraFrameTrailer(output);
		}
		else
		{
			// Output the trailer for the group of frames
			PutVideoGroupTrailer(output);
		}

		//	Set Sample size field here.
		SizeTagPop(output);
	}
	else if(encoder->uncompressed == 3)
	{
#if (DEBUG && _WIN32)
		OutputDebugString("pop 2");
#endif
		//	Set Sample size field here.
		SizeTagPop(output);

		// only the header is output
		output->nWordsUsed -= unc_size;
		output->lpCurrentWord -= unc_size;
	}
	else
	{
#if (DEBUG && _WIN32)
		OutputDebugString("pop 3");
#endif
		//	Set Sample size field here.
		SizeTagPop(output);
	}



	STOP(tk_encoding);
}

void EncodeQuantizedFrameTransform(ENCODER *encoder, TRANSFORM *transform, BITSTREAM *output, int channel)
{
	int num_wavelets = transform->num_wavelets;
	int subband = 1;
	int k;

	// Start at the top of the wavelet pyramid
	for (k = num_wavelets - 1; k >= 0; k--)
	{
		IMAGE *wavelet = transform->wavelet[k];
		int wavelet_type = wavelet->wavelet_type;
		int wavelet_level = wavelet->level;
		int wavelet_number = k + 1;
		int num_highpass_bands = wavelet->num_bands - 1;
		//int encoding_method = BAND_ENCODING_CODEBOOK;
		int encoding_method = BAND_ENCODING_RUNLENGTHS;
		int encoding_order[] = {LH_BAND, HL_BAND, HH_BAND};
		int quantization;
		//int divisor = wavelet->divisor[0];
		int divisor = 0;
		int i;

#if _DEBUG
		encoder->encoded_band_wavelet = k;
#endif
		// Output the header for the high pass bands at this level
		PutVideoHighPassHeader(output, wavelet_type, wavelet_number, wavelet_level,
							   wavelet->width, wavelet->height, wavelet->num_bands,
							   wavelet->scale[0], divisor);

		// Use run length coding for runs of zeros and individual codes
		// for the magnitude and sign if the coefficient is not zero
		for (i = 0; i < num_highpass_bands; i++)
		{
			int band = encoding_order[i];
			int zeroband = 0;
			//int precent = output->nWordsUsed*100 / output->dwBlockLength;
			
			quantization = wavelet->quantization[band];
#if _DEBUG
			encoder->encoded_band_number = band;
#endif
			/* TODO -- Does this work correctly for 3D, as the dwBlockLength might be twice the size?
			if(	encoder->encoded_format == ENCODED_FORMAT_RGB_444)
			{
				if(channel == 0 && precent > 28)
					zeroband = 1;
				if(channel == 1 && precent > 59)
					zeroband = 1;
				if(precent > 85)
					zeroband = 1;
			}
			else if(encoder->encoded_format == ENCODED_FORMAT_RGBA_4444 || 
					encoder->encoded_format == ENCODED_FORMAT_BAYER)
			{
				if(channel == 0 && precent > 22)
					zeroband = 1;
				if(channel == 1 && precent > 46)
					zeroband = 1;
				if(channel == 2 && precent > 70)
					zeroband = 1;
				if(precent > 85)
					zeroband = 1;
			}
			else
			{
				if(channel == 0 && precent > 50)
					zeroband = 1;
				if(channel == 1 && precent > 70)
					zeroband = 1;
				if(precent > 85)
					zeroband = 1;
			}
			*/

	
			if(zeroband)
			{
				//Encode the band as zeros -- fast and compatible with older decoders.
				EncodeZeroBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
			}
			else 
			{
				// Encode band without performing quantization
				EncodeQuantizedBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
			}

			++subband;
		}

		// Output the trailer for the highpass bands
		PutVideoHighPassTrailer(output, 0, 0, 0, 0, 0);

		//NOTE: Need to change the statistics output to something appropriate
	}
}

void EncodeQuantizedFieldTransform(ENCODER *encoder, TRANSFORM *transform, BITSTREAM *output, int channel)
{
	int num_wavelets = transform->num_wavelets;
	int subband = 1;
	int k;

	// Start at the top of the wavelet pyramid
	for (k = num_wavelets - 1; k >= 0; k--)
	{
		IMAGE *wavelet = transform->wavelet[k];
		int wavelet_type = wavelet->wavelet_type;
		int wavelet_level = wavelet->level;
		int wavelet_number = k + 1;
		int num_highpass_bands = wavelet->num_bands - 1;
		//int encoding_method = BAND_ENCODING_CODEBOOK;
		int encoding_method = BAND_ENCODING_RUNLENGTHS;
		int encoding_order[] = {LH_BAND, HL_BAND, HH_BAND};
		int quantization;
		//int divisor = wavelet->divisor[0];
		int divisor = 0;
		int i;

		// Output the header for the high pass bands at this level
		PutVideoHighPassHeader(output, wavelet_type, wavelet_number, wavelet_level,
							   wavelet->width, wavelet->height, wavelet->num_bands,
							   wavelet->scale[0], divisor);

		// Use run length coding for runs of zeros and individual codes
		// for the magnitude and sign if the coefficient is not zero
		for (i = 0; i < num_highpass_bands; i++)
		{
			int band = encoding_order[i];
			int zeroband = 0;
			//int precent = output->nWordsUsed*100 / output->dwBlockLength;
			
			quantization = wavelet->quantization[band];

			/* TODO -- Does this work correctly for 3D, as the dwBlockLength might be twice the size?
			if(	encoder->encoded_format == ENCODED_FORMAT_RGB_444)
			{
				if(channel == 0 && precent > 28)
					zeroband = 1;
				if(channel == 1 && precent > 59)
					zeroband = 1;
				if(precent > 85)
					zeroband = 1;
			}
			else if(encoder->encoded_format == ENCODED_FORMAT_RGBA_4444 || 
					encoder->encoded_format == ENCODED_FORMAT_BAYER)
			{
				if(channel == 0 && precent > 22)
					zeroband = 1;
				if(channel == 1 && precent > 46)
					zeroband = 1;
				if(channel == 2 && precent > 70)
					zeroband = 1;
				if(precent > 85)
					zeroband = 1;
			}
			else
			{
				if(channel == 0 && precent > 50)
					zeroband = 1;
				if(channel == 1 && precent > 70)
					zeroband = 1;
				if(precent > 85)
					zeroband = 1;
			}
			*/

	
			if(zeroband)
			{
				//Encode the band as zeros -- fast and compatible with older decoders.
				EncodeZeroBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
			}
			else
			{
				// Encode band without performing quantization
				EncodeQuantizedBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
			}

			++subband;
		}

		// Output the trailer for the highpass bands
		PutVideoHighPassTrailer(output, 0, 0, 0, 0, 0);

		//NOTE: Need to change the statistics output to something appropriate
	}
}

void EncodeQuantizedFieldPlusTransform(ENCODER *encoder, TRANSFORM *transform, BITSTREAM *output, int channel)
{
	//FILE *logfile = encoder->logfile;
	int num_wavelets = transform->num_wavelets;
	int subband = 1;
	int k;

	// Start at the top of the wavelet pyramid
	// Encode the two spatial transforms from the temporal lowpass band
	for (k = num_wavelets - 1; k >= num_wavelets - 2; k--)
	{
		IMAGE *wavelet = transform->wavelet[k];
		int wavelet_type = wavelet->wavelet_type;
		int wavelet_level = wavelet->level;
		int wavelet_number = k + 1;
		int num_highpass_bands = wavelet->num_bands - 1;
		//int encoding_method = BAND_ENCODING_CODEBOOK;
		int encoding_method = BAND_ENCODING_RUNLENGTHS;
		int encoding_order[] = {LH_BAND, HL_BAND, HH_BAND};
		int quantization;
		//int divisor = wavelet->divisor[0];
		int divisor = 0;
		int i;

		// Output the header for the high pass bands at this level
		PutVideoHighPassHeader(output, wavelet_type, wavelet_number, wavelet_level,
							   wavelet->width, wavelet->height, wavelet->num_bands,
							   wavelet->scale[0], divisor);

		assert(wavelet_type == WAVELET_TYPE_SPATIAL);

		// Use run length coding for runs of zeros and individual codes
		// for the magnitude and sign if the coefficient is not zero
		for (i = 0; i < num_highpass_bands; i++) {
			int band = encoding_order[i];
			//quantization = encoder->quant[channel][subband];
			//quantization = 1;
			quantization = wavelet->quantization[band];
#if (0 && DEBUG)
			if (logfile) {
				fprintf(logfile, "Encoding channel: %d, subband: %d, wavelet: %d\n", channel, subband, k);
			}
#endif
#if (0 && TIMING)
			if (logfile) {
				int scale = wavelet->scale[band];
				fprintf(logfile,
						"Encoding channel: %d, subband: %d, scale: %d, quantization: %d\n",
						channel, subband, scale, quantization);
			}
#endif
			if (wavelet->pixel_type[band] == PIXEL_TYPE_16S)
			{
				// Encode the band without performing quantization
				EncodeQuantizedBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
				++subband;
			}
#if _HIGHPASS_CODED
			else if (wavelet->pixel_type[band] == PIXEL_TYPE_CODED)
			{
				// Copy an encoded band to the bitstream
				EncodeCodedBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
				++subband;
			}
#endif
			else {
				assert(0);
			}
		}

		// Output the trailer for the highpass bands
		PutVideoHighPassTrailer(output, 0, 0, 0, 0, 0);

		//NOTE: Need to change the statistics output to something appropriate
	}

	// Encode the spatial transform from the temporal highpass band
	{
		IMAGE *wavelet = transform->wavelet[k];
		int wavelet_type = wavelet->wavelet_type;
		int wavelet_level = wavelet->level;
		int wavelet_number = k + 1;
		int num_highpass_bands = wavelet->num_bands;
		//int encoding_method = BAND_ENCODING_CODEBOOK;
		int encoding_method = BAND_ENCODING_RUNLENGTHS;
		//int encoding_order[] = {LL_BAND, LH_BAND, HL_BAND, HH_BAND};
		int quantization;
		//int divisor = wavelet->divisor[0];
		int divisor = 0;
		int i;

		// Output the header for the high pass bands at this level
		PutVideoHighPassHeader(output, wavelet_type, wavelet_number, wavelet_level,
							   wavelet->width, wavelet->height, wavelet->num_bands,
							   wavelet->scale[0], divisor);

		assert(wavelet_type == WAVELET_TYPE_SPATIAL);

		// Use run length coding for runs of zeros and individual codes
		// for the magnitude and sign if the coefficient is not zero
		for (i = 0; i < num_highpass_bands; i++)
		{
			int band = i;
			quantization = wavelet->quantization[band];

#if (0 && DEBUG)
			if (logfile) {
				fprintf(logfile, "Encoding channel: %d, subband: %d, wavelet: %d\n", channel, subband, k);
			}
#endif
#if (0 && TIMING)
			if (logfile) {
				int scale = wavelet->scale[band];
				fprintf(logfile,
						"Encoding channel: %d, subband: %d, scale: %d, quantization: %d\n",
						channel, subband, scale, quantization);
			}
#endif

#if 1
			// Should the band be encoded without using variable length codes?
			if (encoder->codec.precision >= CODEC_PRECISION_10BIT && band == 0)
			{
				// Directly encode the 16-bit coefficients in the lowpass band
				//lossless temporal subband //DAN20060701
				if(encoder->encoder_quality & 0x1000000/*CFEncode_Temporal_Quality_On*/)
				{
					int factor = (encoder->encoder_quality & 0x0e00000/*CFEncode_Temporal_Quality_Mask*/) >> 21/*CFEncode_Temporal_Quality_Shift*/;
					int tempquant = 1 << factor;

					EncodeBand16sLossless(encoder, output, wavelet, band, subband, BAND_ENCODING_LOSSLESS, tempquant); //quant of 32 is for a efficient file size will only minor PSNR impacts
				}
				else
				{
					EncodeQuantizedBand16s(encoder, output, wavelet, band, subband, BAND_ENCODING_16BIT, quantization);
				}
			}
			else
#endif
			{
				// Encode band without performing quantization
				EncodeQuantizedBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
			}

			// Advance to the next subband
			++subband;
		}

		// Output the trailer for the highpass bands
		PutVideoHighPassTrailer(output, 0, 0, 0, 0, 0);

		k--;
		//NOTE: Need to change the statistics output to something appropriate
	}

	// Encode the temporal transform as an empty band
	{
		IMAGE *wavelet = transform->wavelet[k];
		int wavelet_type = wavelet->wavelet_type;
		int wavelet_level = wavelet->level;
		int wavelet_number = k + 1;
		//int num_highpass_bands = wavelet->num_bands - 1;
		//int encoding_method = BAND_ENCODING_CODEBOOK;
		int encoding_method = BAND_ENCODING_RUNLENGTHS;
		//int encoding_order[] = {LH_BAND, HL_BAND, HH_BAND};
		int quantization = 1;
		//int divisor = wavelet->divisor[0];
		int divisor = 0;
		//int i;

		// Output the header for the high pass bands at this level
		PutVideoHighPassHeader(output, wavelet_type, wavelet_number, wavelet_level,
							   wavelet->width, wavelet->height, wavelet->num_bands,
							   wavelet->scale[0], divisor);

		// Check that the wavelet type is valid
		assert(wavelet_type == WAVELET_TYPE_TEMPORAL);

		// Check that number of highpass bands is 1
		assert(wavelet->num_bands == 2);


		// Encode the temporal highpass as an empty band

		// Set the subband index to (unsigned char) -1, i.e., 255
		EncodeEmptyQuantBand(encoder, output, wavelet, 1, 255, encoding_method, quantization);

		// Output the trailer for the highpass bands
		PutVideoHighPassTrailer(output, 0, 0, 0, 0, 0);

		k--;
		//NOTE: Need to change the statistics output to something appropriate
	}

	// Encode the two field transforms
	for (; k >= 0; k--)
	{
		IMAGE *wavelet = transform->wavelet[k];
		int wavelet_type = wavelet->wavelet_type;
		int wavelet_level = wavelet->level;
		int wavelet_number = k + 1;
		int num_highpass_bands = wavelet->num_bands - 1;
		//int encoding_method = BAND_ENCODING_CODEBOOK;
		int encoding_method = BAND_ENCODING_RUNLENGTHS;
		int encoding_order[] = {LH_BAND, HL_BAND, HH_BAND};
		int quantization;
		//int divisor = wavelet->divisor[0];
		int divisor = 0;
		int i;

		// Output the header for the high pass bands at this level
		PutVideoHighPassHeader(output, wavelet_type, wavelet_number, wavelet_level,
							   wavelet->width, wavelet->height, wavelet->num_bands,
							   wavelet->scale[0], divisor);

		assert(wavelet_type == WAVELET_TYPE_HORZTEMP);

		// Use run length coding for runs of zeros and individual codes
		// for the magnitude and sign if the coefficient is not zero
		for (i = 0; i < num_highpass_bands; i++)
		{
			int limitPrecent = 80;
			int band = encoding_order[i];
			quantization = wavelet->quantization[band];
#if (0 && DEBUG)
			if (logfile) {
				fprintf(logfile, "Encoding channel: %d, subband: %d, wavelet: %d\n", channel, subband, k);
			}
#endif
#if (0 && TIMING)
			if (logfile) {
				int scale = wavelet->scale[band];
				fprintf(logfile,
						"Encoding channel: %d, subband: %d, scale: %d, quantization: %d\n",
						channel, subband, scale, quantization);
			}
#endif
			// Encode band without performing quantization

			//This had issues with the StEM footage		-- the confetti scene overwhelmed the bit-rate
			// this /3 is actually a 6:1 conpression as we are only using one frame buffer for the
			// two compressed fields
			//if(output->nWordsUsed < output->dwBlockLength/3) // only compress up to half the frame size
			//	EncodeQuantizedBand(encoder, output, wavelet, band, subband, encoding_method, quantization);

			//DAN Overflow , bypass , limit the compressed output.
			// As the band is already quantized, if the bit-rate is too high we need to quantize it further.
#if !LOSSLESS
			//{
			//	FILE *fp = fopen("c:/overflow.txt","a");
			//	fprintf(fp,"%d%% full chn %d subband %d\n", output->nWordsUsed*100/output->dwBlockLength, channel, subband);
			//	fclose(fp);
			//	}

			if(output->nWordsUsed*100 > output->dwBlockLength*limitPrecent) // only compress up to 80% of the frame size
			{
				//Encode the band as zeros -- fast and compatible with older decoders.
				EncodeZeroBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
			}
			else
#endif
			{
				EncodeQuantizedBand(encoder, output, wavelet, band, subband, encoding_method, quantization);
#if DUMP && DEBUG
				if(output->nWordsUsed*100 > output->dwBlockLength*80)
				{
					FILE *fp = fopen("c:/overflow.txt","a");
					fprintf(fp,"%d%% full subband %d\n", output->nWordsUsed*100/output->dwBlockLength, subband);
					fclose(fp);
				}
#endif
			}
			++subband;
		}

		// Output the trailer for the highpass bands
		PutVideoHighPassTrailer(output, 0, 0, 0, 0, 0);

		//NOTE: Need to change the statistics output to something appropriate
	}
}

#else
#error Must have codec tags enabled
#endif


// Compute the upper levels of the wavelet transform for a group of frames
void ComputeGroupTransformQuant(ENCODER *encoder, TRANSFORM *transform[], int num_transforms)
{
#if _ALLOCATOR
	ALLOCATOR *allocator = encoder->allocator;
#endif

	int channel;

	// Copy parameters from the encoder into the transform data structures
	int num_frames = encoder->gop_length;
	int num_spatial = encoder->num_spatial;

	for (channel = 0; channel < num_transforms; channel++)
	{
		//int precision = encoder->codec.precision;
		//int prescale = 0;

		assert(transform[channel]->type == TRANSFORM_TYPE_SPATIAL ||
			   transform[channel]->type == TRANSFORM_TYPE_FIELD   ||
			   transform[channel]->type == TRANSFORM_TYPE_FIELDPLUS);

		transform[channel]->num_frames = num_frames;
		transform[channel]->num_spatial = num_spatial;

		// Compute the temporal and spatial wavelets to finish the transform
		switch (transform[channel]->type)
		{
		case TRANSFORM_TYPE_SPATIAL:
			//prescale = (precision == CODEC_PRECISION_DEFAULT) ? 0 : 1;
			//FinishFrameTransformQuant(encoder, transform[channel], channel, prescale);
			FinishFrameTransformQuant(encoder, transform[channel], channel);
			break;

		case TRANSFORM_TYPE_FIELD:
#if _ALLOCATOR
			FinishFieldTransform(allocator, transform[channel], num_frames, num_spatial);
#else
			//FinishFieldTransform(transform[channel], num_frames, num_spatial, prescale);
			FinishFieldTransform(transform[channel], num_frames, num_spatial);
#endif
			break;

		case TRANSFORM_TYPE_FIELDPLUS:
			//prescale = (precision == CODEC_PRECISION_DEFAULT) ? 0 : 2;
			//FinishFieldPlusTransformQuant(encoder, transform[channel], channel, prescale);
			FinishFieldPlusTransformQuant(encoder, transform[channel], channel);
			break;

		default:	// Transform type is not supported
			assert(0);
			break;
		}

#if (1 && DUMP)
		if (encoder->dump.enabled)
		{
			// Dump the wavelet bands in the transform for this channel
			DumpTransformBands(CODEC_TYPE(encoder), transform[channel], channel, false);
		}
#endif
	}
}

// Finish the wavelet transform for the group of frames
//void FinishFieldPlusTransformQuant(ENCODER *encoder, TRANSFORM *transform, int channel, int prescale)
void FinishFieldPlusTransformQuant(ENCODER *encoder, TRANSFORM *transform, int channel)
{
	//FILE *logfile = encoder->logfile;

#if _ALLOCATOR
	ALLOCATOR *allocator = encoder->allocator;
#endif

	IMAGE *temporal;	// Temporal wavelet (two bands)
	IMAGE *wavelet;		// Spatio-temporal wavelet

	int num_frames = encoder->gop_length;
	//int num_spatial = encoder->num_spatial;

	size_t size = transform->size;

	//int quantization[IMAGE_NUM_BANDS];

	//int width, height;
	int level;
	//int last_level;
	int wavelet_index = 0;
	int index;
	//int i;						// Index to the frame within the group

	int prescale = 0;

	// Can only handle a group length of two
	assert(num_frames == 2);

	// Cannot exceed the maximum number of frames
	assert(num_frames <= WAVELET_MAX_FRAMES);

	// Allocate a buffer for image processing (if necessary)
	if (transform->buffer == NULL) {
		IMAGE *wavelet = transform->wavelet[0];
		assert(wavelet != NULL);
		size = wavelet->height * wavelet->pitch;

#if _ALLOCATOR
		transform->buffer = (PIXEL *)AllocAligned(allocator, size, 16);
#else
		transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
#endif
		assert(transform->buffer != NULL);
		transform->size = size;
	}

	// Have already computed the frame transforms at the base of the wavelet pyramid
	wavelet_index += num_frames;


	/***** Perform the temporal transform between frames *****/

	// Compute a temporal wavelet between the two frame (temporal-horizontal) wavelets
	level = 2;
	temporal = transform->wavelet[wavelet_index];

	// Use the prescale shift in the transform data structure
	prescale = transform->prescale[wavelet_index];

	TransformForwardTemporal(transform->wavelet[0], 0, transform->wavelet[1], 0, temporal, 0, temporal, 1);

#if (0 && DEBUG)
	if (logfile) {
		char label[_MAX_PATH];
		int band;

		for (band = 0; band < temporal->num_bands; band++)
		{
			sprintf(label, "Temporal transform, channel: %d, band: %d", channel, band);
			DumpBandStatistics(label, temporal, band, logfile);
		}
	}
#endif


	/***** Apply spatial transforms to the temporal highpass band *****/

	assert(encoder->num_spatial == 3);

	assert((size_t)(level + 1) < sizeof(transform->wavelet)/sizeof(transform->wavelet[0]));

	// Compute the spatial wavelet transform for the temporal highpass band
	wavelet = transform->wavelet[wavelet_index + 1];

	// Use the prescale shift in the transform data structure
	prescale = transform->prescale[wavelet_index + 1];
	//assert(prescale == 0);

#if 1
	if (encoder->codec.precision >= CODEC_PRECISION_10BIT)
	{
		wavelet->quant[0] = 1; //DAN20050511 -- add this back again. The TLL band had quant of 16.
#if _ALLOCATOR
		wavelet = TransformForwardSpatial(allocator, transform->wavelet[wavelet_index], 1, wavelet, level + 1,
										  transform->buffer, transform->size, prescale/*DAN20070220*/, wavelet->quant/*NULL*/, 0); //DAN031304 why was temporal quant set to NULL?
#else
		wavelet = TransformForwardSpatial(transform->wavelet[wavelet_index], 1, wavelet, level + 1,
										  transform->buffer, transform->size, prescale/*DAN20070220*/, wavelet->quant/*NULL*/, 0); //DAN031304 why was temporal quant set to NULL?
#endif
#if (0 && DEBUG)
		if (logfile) {
			char label[_MAX_PATH];
			int band;

			sprintf(label, "Highpass spatial, channel: %d", channel);
			DumpImageStatistics(label, wavelet, logfile);
#if 1
			for (band = 1; band < wavelet->num_bands; band++)
			{
				sprintf(label, "Highpass spatial, band: %d", band);
				DumpBandStatistics(label, wavelet, band, logfile);
			}
#endif
		}
#endif
	}
	else
#endif
	{
#if _ALLOCATOR
		wavelet = TransformForwardSpatial(allocator, transform->wavelet[wavelet_index], 1, wavelet, level + 1,
										  transform->buffer, transform->size, 0, wavelet->quant, DIFFERENCE_TEMPORAL_LL);
#else
		wavelet = TransformForwardSpatial(transform->wavelet[wavelet_index], 1, wavelet, level + 1,
										  transform->buffer, transform->size, 0, wavelet->quant, DIFFERENCE_TEMPORAL_LL);
#endif
	}

	if (wavelet == NULL) {
		// Record the number levels in the wavelet pyramid
		transform->num_levels = level;

		// Record the number of wavelets
		transform->num_wavelets = wavelet_index;
		return;
	}


	/***** Apply spatial transforms to the temporal lowpass band *****/

	index = wavelet_index + 2;

	// First spatial transform for the temporal lowpass band
	wavelet = transform->wavelet[index];

	// Use the prescale shift in the transform data structure
	prescale = transform->prescale[index];

#if _HIGHPASS_CODED
	if (!TransformForwardSpatialCoded(encoder, transform->wavelet[wavelet_index], 0, wavelet, level + 1,
		transform->buffer, transform->size, prescale, wavelet->quant))
	{
		// Record the number levels in the wavelet pyramid
		transform->num_levels = level;

		// Record the number of wavelets
		transform->num_wavelets = wavelet_index+1;
		return;
	}
#else
#if _ALLOCATOR
	wavelet = TransformForwardSpatial(allocator, transform->wavelet[wavelet_index], 0, wavelet, level+1,
									  transform->buffer, transform->size, prescale, wavelet->quant, 0);
#else
	wavelet = TransformForwardSpatial(transform->wavelet[wavelet_index], 0, wavelet, level+1,
									  transform->buffer, transform->size, prescale, wavelet->quant, 0);
#endif
#if (0 && DEBUG)
	if (logfile) {
		char label[_MAX_PATH];
		int band;

		sprintf(label, "Lowpass spatial, channel: %d", channel);
		DumpImageStatistics(label, wavelet, logfile);
#if 1
		for (band = 1; band < wavelet->num_bands; band++)
		{
			sprintf(label, "Lowpass spatial, band: %d", band);
			DumpBandStatistics(label, wavelet, band, logfile);
		}
#endif
	}
#endif

	if (wavelet == NULL) {
		// Record the number levels in the wavelet pyramid
		transform->num_levels = level;

		// Record the number of wavelets
		transform->num_wavelets = wavelet_index+1;
		return;
	}
#endif

	wavelet_index += 2;

	level++;
	assert((size_t)(level + 1) < sizeof(transform->wavelet)/sizeof(transform->wavelet[0]));

	index = wavelet_index + 1;

	// Second spatial transform for the temporal lowpass band
	wavelet = transform->wavelet[index];

	// Use the prescale shift in the transform data structure
	prescale = transform->prescale[index];

#if _ALLOCATOR
	wavelet = TransformForwardSpatial(allocator, transform->wavelet[wavelet_index], 0, wavelet, level+1,
									  transform->buffer, transform->size, prescale, wavelet->quant, 0);
#else
	wavelet = TransformForwardSpatial(transform->wavelet[wavelet_index], 0, wavelet, level+1,
									  transform->buffer, transform->size, prescale, wavelet->quant, 0);
#endif

#if (0 && DEBUG)
	if (logfile) {
		char label[_MAX_PATH];
		int band;

		sprintf(label, "Lowpass spatial, channel: %d", channel);
		DumpImageStatistics(label, wavelet, logfile);
#if 1
		for (band = 1; band < wavelet->num_bands; band++)
		{
			sprintf(label, "Lowpass spatial, band: %d", band);
			DumpBandStatistics(label, wavelet, band, logfile);
		}
#endif
	}
#endif

	if (wavelet == NULL) {
		// Record the number levels in the wavelet pyramid
		transform->num_levels = level;

		// Record the number of wavelets
		transform->num_wavelets = wavelet_index;
		return;
	}

	level++;
	wavelet_index += 1;

//finish:
	// Record the number levels in the wavelet pyramid
	transform->num_levels = level;

	// Record the number of wavelets
	transform->num_wavelets = wavelet_index+1;
}


// Finish the wavelet transform for an intra frame group
//void FinishFrameTransformQuant(ENCODER *encoder, TRANSFORM *transform, int channel, int prescale)
void FinishFrameTransformQuant(ENCODER *encoder, TRANSFORM *transform, int channel)
{
	//FILE *logfile = encoder->logfile;

#if _ALLOCATOR
	ALLOCATOR *allocator = NULL;
#endif

	//IMAGE *temporal;	// Temporal wavelet (two bands)
	//IMAGE *wavelet;		// Spatio-temporal wavelet

	int num_frames = encoder->gop_length;
	int num_spatial = encoder->num_spatial;

	size_t size = transform->size;

	//int quantization[IMAGE_NUM_BANDS];

	int wavelet_index = 0;
	int last_level = num_spatial + 1;
	int level = 0;
	//int width;
	//int height;
	int index;

	int prescale = 0;

	// Can only handle a group length of one
	assert(num_frames == 1);

	// Cannot exceed the maximum number of frames
	assert(num_frames <= WAVELET_MAX_FRAMES);

	// Allocate a buffer for image processing (if necessary)
	if (transform->buffer == NULL) {
		IMAGE *wavelet = transform->wavelet[0];
		assert(wavelet != NULL);
		size = wavelet->height * wavelet->pitch;
		transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
		assert(transform->buffer != NULL);
		transform->size = size;
	}

	// Have already computed the first wavelet at the base of the pyramid
	wavelet_index += num_frames;
	level = 1;

	// Apply spatial transforms to the lowpass band of the first wavelet
	for (index = wavelet_index; index < last_level; index++)
	{
		IMAGE *wavelet;
		//int quant[CODEC_MAX_BANDS];
		//int *pquant;

		assert(0 < index && index < transform->num_wavelets);

		// The prescale shift is determined by the prescale table in the transform
		prescale = transform->prescale[index];

		wavelet = transform->wavelet[index];
#if _ALLOCATOR
		wavelet = TransformForwardSpatial(allocator, transform->wavelet[index - 1], 0, wavelet, level + 1,
										  transform->buffer, transform->size, prescale, wavelet->quant, 0);
#else
		wavelet = TransformForwardSpatial(transform->wavelet[index - 1], 0, wavelet, level + 1,
										  transform->buffer, transform->size, prescale, wavelet->quant, 0);
#endif
#if (0 && DEBUG)
		if (logfile) {
			char label[_MAX_PATH];
			int band;

			sprintf(label, "Spatial transform, channel: %d", channel);
			DumpImageStatistics(label, wavelet, logfile);
#if 1
			for (band = 1; band < wavelet->num_bands; band++)
			{
				sprintf(label, "Spatial transform, band: %d", band);
				DumpBandStatistics(label, wavelet, band, logfile);
			}
#endif
		}
#endif

		if (wavelet == NULL) {
			// Record the number of levels in the wavelet pyramid
			transform->num_levels = level;

			// Record the number of wavelets
			transform->num_wavelets = wavelet_index + 1;
			return;
		}

		level++;
		wavelet_index++;
	}

	// Record the number levels in the wavelet pyramid
	transform->num_levels = level;

	// Record the number of wavelets
	transform->num_wavelets = wavelet_index;
}

void OverrideEncoderSettings(ENCODER *encoder)
{
	unsigned int *last_set_time = &encoder->last_set_time;
	int checkdiskinfo = 0;
	int type;

	clock_t time = clock();
	int32_t diff = (long)time - (long)*last_set_time;
#define MS_ENC_DIFF	(CLOCKS_PER_SEC / 5)

	if(abs(diff) > MS_ENC_DIFF || *last_set_time==0) // only test every 1000ms
	{
		*last_set_time = (unsigned int)time;
		checkdiskinfo = 1;
	}


	if(checkdiskinfo) // only test every 1000ms)
	{
		unsigned char *ptr;
		int len;

		InitLUTPathsEnc(encoder);

		for (type = 0; type < 2; type++)
		{
			unsigned int *size;
			unsigned char *buffer = encoder->forceData;
			char filenameGUID[PATH_MAX + 1] = "";
			len = 0;

			if(type == 0) // preset_default an colr file for all clips.
			{
#ifdef _WIN32
				sprintf_s(filenameGUID, sizeof(filenameGUID), "%s/%s/defaults.colr", encoder->LUTsPathStr, encoder->UserDBPathStr);
#else
				sprintf(filenameGUID, "%s/%s/defaults.colr", encoder->LUTsPathStr, encoder->UserDBPathStr);
#endif

				buffer = encoder->baseData; // default user data
				size = &encoder->baseDataSize;
			}
			else if(type == 1) // preset_override an colr file for all clips.
			{
#ifdef _WIN32
				sprintf_s(filenameGUID, sizeof(filenameGUID), "%s/override.colr", encoder->OverridePathStr);
#else
				sprintf(filenameGUID, "%s/override.colr", encoder->OverridePathStr);
#endif

				buffer = encoder->forceData;// override user data
				size = &encoder->forceDataSize;
			}

			len = 0;
			if(strlen(filenameGUID))
			{
				int err = 0;
				FILE *fp;

#ifdef _WIN32
				err = fopen_s(&fp, filenameGUID, "rb");
#else
				fp = fopen(filenameGUID, "rb");
#endif
				if (err == 0 && fp != NULL)
				{
					ptr = buffer;
					len = 0;
					fseek (fp, 0, SEEK_END);
					len = ftell(fp);

					if(len <= MAX_ENCODE_DATADASE_LENGTH)
					{
						fseek (fp, 0, SEEK_SET);
#ifdef _MSC_VER
						len = (int)fread_s(buffer, MAX_ENCODE_DATADASE_LENGTH, 1, len, fp);
#else
						len = (int)fread(buffer,1,len,fp);
#endif
						*size = len;
					}
					else
						*size = 0;


					fclose(fp);
				}
				else
					*size = 0;
			}
		}
	}

	for (type = 0; type < 2; type++)
	{
		int len = 0;
		unsigned char *buffer = NULL;

		if(type == 0) // preset_default an colr file for all clips.
		{
			buffer = encoder->baseData; // default user data
			len = encoder->baseDataSize;
		}
		else if(type == 1) // preset_override an colr file for all clips.
		{
			buffer = encoder->forceData;// override user data
			len = encoder->forceDataSize;
		}

		UpdateEncoderOverrides(encoder, buffer, len);
	}
}

int RemoveHiddenMetadata(unsigned char *ptr, int len)
{
	int retlen = len;
	if(ptr && len) // overrides form database or external control
	{
		int pos = 0;
		unsigned char type = ptr[pos+7];
		unsigned int size = ptr[pos+4] + (ptr[pos+5]<<8) + (ptr[pos+6]<<16);

		while(pos < retlen)
		{
			int entrysize = (8 + size + 3) & 0xfffffc;
			if(type == METADATA_TYPE_HIDDEN)
			{
				int i;

				for(i=pos+entrysize; i<retlen; i++)
				{
					ptr[i-entrysize] = ptr[i];
				}
				retlen -= entrysize;
			}

			pos += entrysize;
			if(pos + 12 <= retlen)
			{
				type = ptr[pos+7];
				size = ptr[pos+4] + (ptr[pos+5]<<8) + (ptr[pos+6]<<16);
			}
			else
			{
				break;
			}
		}
	}

	return retlen;
}

void UpdateEncoderOverrides(ENCODER *encoder, unsigned char *ptr, int len)
{
	if(encoder && ptr && len) // overrides form database or external control
	{
		unsigned char *base = ptr;
		void *data = (void *)&ptr[8];
		unsigned char type;
		unsigned int size;
		unsigned int tag;
		//void *metadatastart = data;
		bool terminate = false;

		while ((int)((uintptr_t)data - (uintptr_t)base) < len && !terminate)
		{
			type = ptr[7];
			size = ptr[4] + (ptr[5]<<8) + (ptr[6]<<16);
			tag = MAKETAG(ptr[0],ptr[1],ptr[2],ptr[3]);

			switch(tag)
			{
			case 0:
				terminate = true;
				break;

			case TAG_BAYER_FORMAT:
				//encoder->bayer_format = *((uint32_t *)data);
				encoder->bayer.format = *((uint32_t *)data);
				break;

			case TAG_LIMIT_YUV:	// Canon 5D 0-255 convert to 16-235 (10-bit)
				encoder->limit_yuv = *((uint32_t *)data);
				break;
			case TAG_CONV_601_709:	// Canon 5D 601 to 709
				encoder->conv_601_709 = *((uint32_t *)data);
				break;
			case TAG_PROXY_COPY:
				// no not apply twice
				encoder->limit_yuv = 0;
				encoder->conv_601_709 = 0;
				break;

			case TAG_COLORSPACE_YUV: // 601/709
				if(*((uint32_t *)data) & 1)
				{
					encoder->input.color_space &= ~COLOR_SPACE_BT_709;
					encoder->input.color_space |= COLOR_SPACE_BT_601;
				}
				if(*((uint32_t *)data) & 2)
				{
					encoder->input.color_space &= ~COLOR_SPACE_BT_601;
					encoder->input.color_space |= COLOR_SPACE_BT_709;
				}
				break;
				
			case TAG_COLORSPACE_RGB: // cgRGB/vsRGB
				if(*((uint32_t *)data) & 1)
				{
					encoder->input.color_space &= ~COLOR_SPACE_VS_RGB;
				}
				if(*((uint32_t *)data) & 2)
				{
					encoder->input.color_space |= COLOR_SPACE_VS_RGB;
				}
				if((encoder->input.color_space & (COLOR_SPACE_BT_601|COLOR_SPACE_BT_709)) == 0) // YUV mode not set
					encoder->input.color_space |= COLOR_SPACE_BT_709;
				break;
				
			case TAG_COLORSPACE_FTR: // 422 dup'd/422to444 filtered
				if(*((uint32_t *)data) & 1)
				{
					encoder->input.color_space |= COLOR_SPACE_422_TO_444;
				}
				else
				{
					encoder->input.color_space &= ~COLOR_SPACE_422_TO_444;
				}
				break;				

			case TAG_ENCODE_PRESET   :// Used for BYR4 which is normally expecting a linear input
									  //to be curved by the value in m_encode_curve
				encoder->encode_curve_preset = *((uint32_t *)data);
				break;

			case TAG_ENCODE_CURVE  :  //can be used for BYR4
				encoder->encode_curve = *((uint32_t *)data);
				break;

			case TAG_PRESENTATION_WIDTH   :// To support resolution independent decoding
				encoder->presentationWidth = *((uint32_t *)data);
				break;
			case TAG_PRESENTATION_HEIGHT   :// To support resolution independent decoding
				encoder->presentationHeight = *((uint32_t *)data);
				break;

			case TAG_IGNORE_DATABASE:
				encoder->ignore_database = *((uint32_t *)data);
				break;

			case TAG_VIDEO_CHANNELS: // the way DS Encoder does 3D.
				encoder->video_channels = *((uint32_t *)data);
				encoder->ignore_overrides = 1;
				break;
				
			case TAG_VIDEO_CHANNEL_GAP: 
				encoder->video_channel_gap = *((uint32_t *)data);
				break;

			case TAG_CHANNELS_ACTIVE:
				switch(*((unsigned int *)data))
				{
				case 1: // decoding channel 1
					encoder->current_channel = 0; // first and default channel
					encoder->video_channels = 1;
					encoder->preformatted3D = 0;
					encoder->ignore_overrides = 1;
					break;
				case 2: // decoding channel 2
					encoder->current_channel = 1; // choose second channel
					encoder->video_channels = 1;
					encoder->preformatted3D = 0;
					encoder->ignore_overrides = 1;
					break;
				case 3: // decoding channel 1+2
					encoder->current_channel = 0; // start with the first channel
					encoder->video_channels = 2;
					encoder->preformatted3D = 1;
					encoder->ignore_overrides = 1;
					break;
				default:
					encoder->video_channels = 1;
					encoder->preformatted3D = 0;
					encoder->ignore_overrides = 0;
					break;
				}
				break;

			case TAG_CHANNELS_MIX:
				encoder->mix_type_value &= 0xffff0000;
				encoder->mix_type_value |= *((unsigned int *)data);
				break;

			case TAG_CHANNELS_MIX_VAL:
				encoder->mix_type_value &= 0xffff;
				encoder->mix_type_value |= *((unsigned int *)data)<<16;
				break;

			}

			if(!terminate)
			{
				ptr += (8 + size + 3) & 0xfffffc;
				data = (void *)&ptr[8];
			}
		}
		//
		//	Move it to here so the order of the metadata does not matter.
		//	CMD 20090923
		switch(encoder->mix_type_value & 0xffff)
		{
			case 0: //chould be double high source for 3D mastering.
			case 1: //stacked
			case 2: //side-by-side
			case 3: //fields
					// All fine.
				break;
			default: // we don;t 3D enocde other formats
				encoder->mix_type_value = 0;
				encoder->video_channels = 1;
				encoder->preformatted3D = 0;
				break;
		}

	}
}

#if _DEBUG && 0

CODEC_ERROR WriteTransformBandFile(TRANSFORM *transform[],
								   int num_transforms,
								   uint32_t channel_mask,
								   uint32_t channel_wavelet_mask,
								   uint32_t wavelet_band_mask,
								   const char *pathname)
{
	BANDFILE file;
	int max_band_width;
	int max_band_height;
	int channel_count;
	int channel_index;

	//TODO: Modify this routine to take the frame index as an argument
	const int frame_index = 0;

	// Get the number of channels in the encoder wavelet transform
	channel_count = num_transforms;

	// Compute the maximum dimensions of each subband
	max_band_width = transform[0]->width;
	max_band_height = transform[0]->height;

	// Create the band file
	CreateBandFile(&file, pathname);

	// Write the band file header
	WriteFileHeader(&file, max_band_width, max_band_height);

	for (channel_index = 0;
		 channel_index < channel_count && channel_mask != 0;
		 channel_mask >>= 1, channel_index++)
	{
		uint32_t wavelet_mask = channel_wavelet_mask;
		int wavelet_count = transform[channel_index]->num_wavelets;
		int wavelet_index;

		for (wavelet_index = 0;
			 wavelet_index < wavelet_count && wavelet_mask != 0;
			 wavelet_mask >>= 1, wavelet_index++)
		{
			// Write bands in this wavelet?
			if ((wavelet_mask & 0x01) != 0)
			{
				IMAGE *wavelet = transform[channel_index]->wavelet[wavelet_index];
				uint32_t band_mask = wavelet_band_mask;
				int band_count = wavelet->num_bands;
				int band_index;

				// Get the actual dimensions of the bands in this wavelet
				int width = wavelet->width;
				int height = wavelet->height;

				for (band_index = 0;
					 band_index < band_count && band_mask != 0;
					 band_mask >>= 1, band_index++)
				{
					// Write this band in the wavelet?
					if ((band_mask & 0x01) != 0)
					{
						void *data = wavelet->band[band_index];
						size_t size = wavelet->width * wavelet->height * sizeof(PIXEL);

						WriteWaveletBand(&file, frame_index, channel_index, wavelet_index,
							band_index, BAND_TYPE_SINT16, width, height, data, size);
					}
				}
			}
		}
	}

	CloseBandFile(&file);

	return CODEC_ERROR_OKAY;
}

// Create a band data file for the encoded highpass bands
CODEC_ERROR CreateEncodedBandFile(ENCODER *encoder, const char *pathname)
{
	ALLOCATOR *allocator = encoder->allocator;
	BITSTREAM *encoded_bitstream;
	int max_band_width;
	int max_band_height;
	void *encoded_band_buffer;
	size_t encoded_band_size;

	// Use the encoded dimensions to maximum dimensions of each subband
	max_band_width = encoder->input.width;
	max_band_height = encoder->input.height;
	encoded_band_size = max_band_width * max_band_height;

	// Create the band file
	CreateBandFile(&encoder->encoded_band_file, pathname);

	// Write the band file header
	WriteFileHeader(&encoder->encoded_band_file, max_band_width, max_band_height);

	// Allocate a buffer for the encoded band data
	encoded_band_buffer = Alloc(allocator, encoded_band_size);
	encoded_band_size = encoded_band_size;

	// Create a new bitstream for the encoded band data
	encoded_bitstream = Alloc(allocator, sizeof(BITSTREAM));

	// Redirect the encoded bitstream to a buffer
	InitBitstreamBuffer(encoded_bitstream, encoded_band_buffer, encoded_band_size, BITSTREAM_ACCESS_WRITE);
	encoder->encoded_band_bitstream = encoded_bitstream;

	return CODEC_ERROR_OKAY;
}

//TODO: Close the bitstream used for encoded band data?
CODEC_ERROR CloseEncodedBandFile(ENCODER *encoder)
{
	CloseBandFile(&encoder->encoded_band_file);
	return CODEC_ERROR_OKAY;
}

#endif


/***** Threaded implementations of the encoding routines *****/

#if _THREADED_ENCODER

#include "temporal.h"

// Forward references to subroutines used during threaded encoding
void ComputeGroupTransformQuantThreaded(ENCODER *encoder, TRANSFORM *transform[], int num_transforms);
void EncodeQuantizedGroupThreaded(ENCODER *encoder, TRANSFORM *transform[], int num_transforms,
								  BITSTREAM *output, uint8_t *buffer, size_t buffer_size);

// Set the handle to the instance of CFEncode
void SetEncoderHandle(ENCODER *encoder, void *handle)
{
	//encoder->handle = handle;
}

// Set the affinity mask that determines on which processors the encoder can execute
void SetEncoderAffinityMask(ENCODER *encoder)
{
	HANDLE hProcess = GetCurrentProcess();
	DWORD dwProcessAffinityMask;
	DWORD dwSystemAffinityMask;
	bool result;

	result = GetProcessAffinityMask(hProcess, &dwProcessAffinityMask, &dwSystemAffinityMask);

	if (result) {
#if 0
		dwProcessAffinityMask &= 0x01;
		SetProcessAffinityMask(hProcess, dwProcessAffinityMask);
#endif
		encoder->affinity_mask = dwProcessAffinityMask;
	}

#if 0
	if (result) {
		HANDLE hCurrentThread = GetCurrentThread();
		DWORD dwThreadAffinityMask = dwProcessAffinityMask & 0x01;

		SetThreadAffinityMask(hCurrentThread, dwThreadAffinityMask);
	}
#endif
}

// Determine the processor used for encoding
DWORD GetEncoderAffinityMask(ENCODER *encoder, int channel)
{
	// Array of affinity masks indexed by color channel
	//DWORD channel_affinity[] = {0x55555555, 0xAAAAAAAA, 0xAAAAAAAA};
	//DWORD channel_affinity[] = {0x01, 0x01, 0x01};
	//DWORD channel_affinity[] = {0x33333333, 0xCCCCCCCC, 0xCCCCCCCC};
	//DWORD channel_affinity[] = {0xCCCCCCCC, 0xCCCCCCCC, 0xCCCCCCCC};
	//DWORD channel_affinity[] = {0x44444444, 0x44444444, 0x44444444};
	DWORD channel_affinity[] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
	//DWORD channel_affinity[] = {0x22222222, 0x22222222, 0x22222222};

	DWORD affinity = channel_affinity[channel] & encoder->affinity_mask;
	if (affinity == 0) affinity = 0x01;

	return affinity;
}

#if 1

// This version incorporates threading for the first level wavelet transform and
// performs the remaining wavelet transforms for each color channel in parallel.
// Bitstream encoding is done in parallel using temporary buffers for the chroma.

// Adapted from the version of EncodeSample modified by David.

// Encode one frame of video
bool EncodeSampleThreaded(ENCODER *encoder, uint8_t * data, int width, int height, int pitch, int format,
						  TRANSFORM *transform[], int num_transforms, BITSTREAM *output,
						  PIXEL *buffer, size_t buffer_size, int i_fixedquality, int fixedbitrate)
{
	FILE *logfile = encoder->logfile;
	bool result = true;
	bool first_frame = false;
	FRAME *frame;
	QUANTIZER *q = &encoder->q;
	int chroma_width = width/2;
	int chroma_offset = encoder->codec.chroma_offset;
	int transform_type = (encoder->gop_length > 1) ? TRANSFORM_TYPE_FIELDPLUS : TRANSFORM_TYPE_SPATIAL;
	int i, j;
	int fixedquality = i_fixedquality;

	CODEC_STATE *codec = &encoder->codec;

	encoder->encoder_quality = fixedquality;

	if(ISBAYER(format))
		chroma_width = width;

#if _TIMING
	DoThreadTiming(2);
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Encode sample, width: %d, height: %d, format: %d\n", width, height, format);
	}
#endif

	// The transforms should have already been allocated
	assert(transform != NULL && transform[0] != NULL);

	// Check that the frame dimensions correspond to the dimensions used for the transform
	assert(width == transform[0]->width);
	assert(height == transform[0]->height);

	// Check that the frame dimensions are transformable
	assert(IsFrameTransformable(chroma_width, height, transform_type, encoder->num_spatial));

	/*
		Note: The transform is allocated and the fields are initialized when the encoder is
		initialized.  The dimensions and type of the transform is not chanaged during encoding.
		Not sure what will happen if the frame dimensions do not correspond to the dimensions
		used to initialize the transform data structure.  Probably should crop or fill the frame
		to the dimensions defined in the transform data structure when the packed frame provided
		as input to the encoder is converted to planes of luma and chroma channels.
	*/

	// Start compressing the frame (including conversion time)
	START(tk_compress);

	// Allocate a data structure for the unpacked frame
	SetEncoderFormat(encoder, width, height, display_height, format);

	// Get the frame for storing the unpacked data
	frame = encoder->frame;
	assert(frame != NULL);

	// Convert the packed color to planes of YUV 4:2:2 (one byte per pixel)
	START(tk_convert);
	switch (format)
	{
	case COLOR_FORMAT_UYVY:
		break;

	case COLOR_FORMAT_RGB24:
#if 1
//  #if BUILD_PROSPECT //10-bit for everyone
		format = COLOR_FORMAT_YU64;
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
		encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
 // #endif
		// Convert the packed RGB data to planes of YUV 4:2:2 (one byte per pixel)
		ConvertRGB32to10bitYUVFrame(data, pitch, frame, (uint8_t *)buffer, buffer_size, encoder->input.color_space, encoder->codec.precision, false, 0);
#else
		// Convert the RGB24 to V210 for testing
		ConvertRGB24ToV210(data, width, height, pitch, (PBYTE)buffer);
		ConvertV210ToFrame8u(data, pitch, frame, (uint8_t *)buffer);
		format = COLOR_FORMAT_V210;
#endif
		break;

	case COLOR_FORMAT_YUYV:
		break;

	case COLOR_FORMAT_RGB32:
//  #if BUILD_PROSPECT //10-bit for everyone
		format = COLOR_FORMAT_YU64;
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
		encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
//  #endif
		// Convert the packed RGBA data to planes of YUV 4:2:2 (one byte per pixel)
		// The data in the alpha channel is not used
		ConvertRGB32to10bitYUVFrame(data, pitch, frame, (uint8_t *)buffer, buffer_size,
			encoder->input.color_space, encoder->codec.precision, true, 0);
		break;

	case COLOR_FORMAT_V210:
		// Convert the packed 10-bit YUV 4:2:2 to planes of 8-bit YUV
#if 0
		ConvertV210ToFrame8u(data, pitch, frame, (uint8_t *)buffer);
#else
		ConvertV210ToFrame16s(data, pitch, frame, (uint8_t *)buffer);
		encoder->codec.precision = CODEC_PRECISION_10BIT;
#endif
		break;

	default:
		// Cannot handle this color format
		return false;
	}
	STOP(tk_convert);

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Quantization fixed quality: %d, fixed bitrate: %d\n", fixedquality, fixedbitrate);
	}
#endif

	if (fixedquality&0xffff == 0)
		QuantizationSetRate(q, fixedbitrate, encoder->progressive, encoder->codec.precision, encoder->gop_length);
	else
		QuantizationSetQuality(q,
			fixedquality,
			encoder->progressive,
			encoder->codec.precision,
			encoder->gop_length,
			ChromaFullRes,
			encoder->frame,
			encoder->lastgopbitcount>>3,
			encoder->video_channels);
#if (0 && DEBUG)
	if (logfile) {
		PrintQuantizer(q, logfile);
	}
#endif

	// Is this the first frame in the GOP?
	if (encoder->group.count == 0)
	{
		int channel;

		// Set the quantization for the transforms in this GOP
		for (channel = 0; channel < num_transforms; channel++)
		{
			SetTransformQuantization(encoder, transform[channel], channel);
		}
	}

#if (0 && DEBUG)
	if (logfile) {
		int k;

		for (k = 0; k < num_transforms; k++) {
			fprintf(logfile, "Quantization for channel: %d\n", k);
			PrintTransformQuantization(transform[k], logfile);
			fprintf(logfile, "\n");
		}
	}
#endif

	// Is this the first frame in the video sequence?
	if (encoder->frame_count == 0 && encoder->group.count == 0 && encoder->gop_length > 1)
	{
		// Note: Do not write out the video sequence header when encoding one frame groups

		// Fill the first sample with the video sequence header
	//	result = EncodeFirstSample(encoder, transform, num_transforms, frame, output, format);
	//	if (!result) goto finish;

		encoder->output.iskey = true;

		first_frame = true;
	}

	// Determine the index of this frame in the group
	j = encoder->group.count;

	// Should be the first or second frame in a two frame group
	assert(0 <= j && j <= 1);

	// Set the number of channels in the encoder quantization table
	encoder->num_quant_channels = num_transforms;

	// Which wavelet transform should be used at the lowest level:
	// frame transform (interlaced) or spatial transform (progressive)
	if (!encoder->progressive)
	{
		int frame_index = j;

#if _NEW_DECODER
		// Interlaced frame encoding (implemented using the frame transform)
		codec->progressive = 0;
#endif

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Transform forward frame: %d, progressive: %d\n", frame_index, encoder->progressive);
		}
#endif

#if (1 && _THREADED_ENCODER)

		if (format == COLOR_FORMAT_YUYV)
		{
			FRAME_INFO info = {width, height, format};
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			// Apply the frame transform directly to the frame
			TransformForwardFrameYUVThreaded(encoder, data, pitch, &info, transform, frame_index,
											 num_transforms, (char *)buffer, buffer_size, chroma_offset, limit_yuv, conv_601_709);
		}

		if (format == COLOR_FORMAT_YUYV)
		{
			FRAME_INFO info = {width, height, format};
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			// Apply the frame transform directly to the frame
			TransformForwardFrameYUVThreaded(encoder, data, pitch, &info, transform, frame_index,
											 num_transforms, (char *)buffer, buffer_size, chroma_offset, limit_yuv, conv_601_709);
		}
#else
		if (format == COLOR_FORMAT_YUYV)
		{
			FRAME_INFO info = {width, height, format};
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			// Apply the frame transform directly to the frame
			TransformForwardFrameYUV(data, pitch, &info, transform, frame_index, num_transforms,
									 (char *)buffer, buffer_size, chroma_offset, codec->precision, limit_yuv, conv_601_709);
		}

		else if (format == COLOR_FORMAT_UYVY)
		{
			FRAME_INFO info = {width, height, format};
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			// Apply the frame transform directly to the frame
			TransformForwardFrameYUV(data, pitch, &info, transform, frame_index, num_transforms,
									 (char *)buffer, buffer_size, chroma_offset, codec->precision, limit_yuv, conv_601_709);
		}
#endif
		else
		{
			// Apply the frame wavelet transform to each plane
			for (i = 0; i < num_transforms; i++)
			{
				//int k;

				IMAGE *image = frame->channel[i];
				IMAGE *wavelet = transform[i]->wavelet[j];

				// The lowpass band must be one byte pixels
				//assert(image->pixel_type[0] == PIXEL_TYPE_8U);

				// Apply the frame transform to the image plane for this channel
				TransformForwardFrame(image, wavelet, buffer, buffer_size, chroma_offset, wavelet->quant);
			}
		}
	}
	else
	{
		int frame_index = j;

#if _NEW_DECODER
		// Progressive frame transform (implemented using the spatial transform)
		codec->progressive = 1;
#endif

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Transform forward spatial: %d, progressive: %d\n", frame_index, encoder->progressive);
		}
#endif

#if (1 && _THREADED_ENCODER)

		if (format == COLOR_FORMAT_YUYV)
		{
			FRAME_INFO info = {width, height, format};
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			// Is this the first frame in the group?
			if (encoder->group.count == 0)
			{
				//DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;

				size_t local_buffer_size = buffer_size / CODEC_GOP_LENGTH;
#if 0
				// This version has tearing in the image because the input sample is not locked.
				// Need to either lock the sample or unpack the data into one plane per buffer.

				// Apply the first level transform to the first frame in the group
				TransformForwardSpatialYUVThreaded(encoder, data, pitch, &info, transform, frame_index,
												   num_transforms, buffer, local_buffer_size, chroma_offset);
#else
				// This version has artifact on right size fo the frame.

#if (0 && DEBUG)
				if (logfile) {
					fprintf(logfile, "Buffer: 0x%p (%d), size: %d, local: 0X%08X (%d), size: %d\n",
							buffer, buffer, buffer_size, buffer, buffer, local_buffer_size);
				}
#endif
				// Apply the first level transform to the first frame in the group
				TransformForwardSpatialYUVPlanarThreaded(encoder, data, pitch, &info, transform, frame_index,
														 num_transforms, buffer, local_buffer_size, chroma_offset);
#endif
				// Wait for the first frame transform to finish
				//WaitForSingleObject(encoder->frame_thread[0], dwTimeout);

				// Signal that the thread as ended
				//encoder->frame_thread[0] = INVALID_HANDLE_VALUE;
			}
			else
			{
				DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;
				const int thread_frame_index = 0;
#if 0
				// Use a portion of the buffer for each frame transform
				buffer_size /= CODEC_GOP_LENGTH;
				buffer += frame_index * buffer_size / sizeof(PIXEL);

				buffer = (PIXEL *)ALIGN(buffer, _CACHE_LINE_SIZE);

				// Apply the first level transform to the second frame in the group
				TransformForwardSpatialYUV(data, pitch, &info, transform, frame_index, num_transforms,
										   buffer, buffer_size, chroma_offset, true, codec->precision, limit_yuv, conv_601_709);

				// Is a thread running on the other frame?
				if (encoder->frame_thread[thread_frame_index] != INVALID_HANDLE_VALUE)
				{
					// Wait for the first frame transform to finish
					WaitForSingleObject(encoder->frame_thread[thread_frame_index], dwTimeout);

					// Signal that the thread as ended
					encoder->frame_thread[thread_frame_index] = INVALID_HANDLE_VALUE;
				}
#else
				// Use a portion of the buffer for each frame transform
				size_t local_buffer_size = buffer_size / CODEC_GOP_LENGTH;
				PIXEL *local_buffer = buffer + frame_index * local_buffer_size / sizeof(PIXEL);

				local_buffer = (PIXEL *)ALIGN(local_buffer, _CACHE_LINE_SIZE);
#if (0 && DEBUG)
				if (logfile) {
					uint8_t *end = (uint8_t *)&local_buffer[local_buffer_size/sizeof(PIXEL)];
					fprintf(logfile, "Buffer: 0x%p (%d), size: %d, local: 0X%08X (%d), size: %d\n",
							buffer, buffer, buffer_size, local_buffer, local_buffer, local_buffer_size);
					fprintf(logfile, "End: 0x%p (%d)\n", end, end);
				}
#endif
				// Apply the first level transform to the first frame in the group
				TransformForwardSpatialYUVPlanarThreaded(encoder, data, pitch, &info, transform, frame_index,
														 num_transforms, local_buffer, local_buffer_size,
														 chroma_offset);
#if (0 && DEBUG)
				// Is another thread processing the first frame?
				if (encoder->frame_thread[thread_frame_index] != INVALID_HANDLE_VALUE)
				{
					// Wait for the first frame transform to finish
					WaitForSingleObject(encoder->frame_thread[thread_frame_index], dwTimeout);

					// Signal that the thread has ended
					encoder->frame_thread[thread_frame_index] = INVALID_HANDLE_VALUE;
				}
#endif
#endif
			}
		}

#else
		if (format == COLOR_FORMAT_YUYV)
		{
			FRAME_INFO info = {width, height, format};
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			// Apply the frame transform directly to the frame
			TransformForwardSpatialYUV(data, pitch, &info, transform, frame_index, num_transforms,
									   buffer, buffer_size, chroma_offset, false, codec->precision, limit_yuv, conv_601_709);
		}
#endif
#if 0
		else if (format == COLOR_FORMAT_UYVY)
		{
			FRAME_INFO info = {width, height, format};
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			// Need to get the spatial YUV transform working for the UYVY format
			assert(0);

			// Apply the frame transform directly to the frame
			TransformForwardSpatialYUV(data, pitch, &info, transform, frame_index, num_transforms,
									   buffer, buffer_size, chroma_offset, codec->precision, limit_yuv, conv_601_709);
		}
#endif
		else
		{
			// Apply the spatial wavelet transform to each plane
			for (i = 0; i < num_transforms; i++)
			{
				//int k;

				IMAGE *image = frame->channel[i];
				IMAGE *wavelet = transform[i]->wavelet[j];
				int band = 0;
				int level = 1;

				// The lowpass band must be one byte pixels
				assert(image->pixel_type[0] == PIXEL_TYPE_8U);

				// Apply the spatial transform to the image plane for this channel
				TransformForwardSpatial(image, band, wavelet, level, buffer, buffer_size, 0, wavelet->quant, 0);
			}
		}

#if TIMING
		// Count the number of progressive frames that were encoded
		progressive_encode_count++;
#endif
	}

	// Increment the count of the number of frames in the group
	encoder->group.count++;

	if(first_frame)
	{
		EncodeFirstSample(encoder, transform, num_transforms, frame, output, format);
	}

#if 1
	if (encoder->gop_length == 1)
	{
		// The progressive encoder is threaded
		//if (encoder->progressive)
		{
			// Compute the spatial transform wavelet tree for each channel
			ComputeGroupTransformQuantThreaded(encoder, transform, num_transforms);

			// Encode the transform for the current frame
			EncodeQuantizedGroupThreaded(encoder, transform, num_transforms, output, (uint8_t *)buffer, buffer_size);
		}
#if 0
		else
		{
			// Compute the spatial transform wavelet tree for each channel
			ComputeGroupTransformQuant(encoder, transform, num_transforms);

			// Encode the transform for the current frame
			EncodeQuantizedGroup(encoder, transform, num_transforms, output, 0, 0);
		}
#endif
		//DAN Variable Bit Rate control feedback.
		//encoder->lastgopbitcount = output->nWordsUsed * 8;//output->cntBits;
		//output->cntBits = 0;

		// Reset the group of frames
		encoder->group.count = 0;

		// Update the frame count
		encoder->frame_count += encoder->gop_length;

		// Mark this frame as a key frame since it is an intra frame
		frame->iskey = true;
		encoder->output.iskey = true;
	}
	else
#endif

	// Enough frames to compute the rest of the wavelet transform?
	if (encoder->group.count == encoder->gop_length)
	{
		int channel;

		// Copy encoder parameters into the transform data structure
		int gop_length = encoder->gop_length;
		int num_spatial = encoder->num_spatial;

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Before compute group transform\n");
		}
#endif

#if 0
		WaitForMultipleObjects(6, encoder->frame_channel_thread, true, INFINITE);
		ComputeGroupTransformQuant(encoder, transform, num_transforms);
#else
		ComputeGroupTransformQuantThreaded(encoder, transform, num_transforms);
#endif

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Before encode quantized group\n");
		}
#endif

		// Encode the transform for the current group of frames
#if 0
		EncodeQuantizedGroup(encoder, transform, num_transforms, output, 0, 0);
#else
		EncodeQuantizedGroupThreaded(encoder, transform, num_transforms, output, (uint8_t *)buffer, buffer_size);
#endif

		//DAN Variable Bit Rate control feedback.
		//encoder->lastgopbitcount = output->nWordsUsed * 8;//output->cntBits;
		//output->cntBits = 0;

		// Reset the group of frames
		encoder->group.count = 0;

		// Update the frame count
		encoder->frame_count += encoder->gop_length;

		// Mark this frame as a key frame since it is the start of a group
		frame->iskey = true;
		encoder->output.iskey = true;

#if (0 && DEBUG)
		if (logfile) {
#if 0
			fprintf(logfile, "Encoded transforms (all channels):\n\n");
			for (channel = 0; channel < num_transforms; channel++)
#else
			channel = 0;
			fprintf(logfile, "Encoded transforms, channel %d:\n\n", channel);
#endif
			{
				char label[256];
				int row = 1;

				sprintf(label, "Channel %d wavelets", channel);
				DumpTransform(label, transform[channel], row, logfile);
				fprintf(logfile, "\n");
			}
		}
#endif
	}
	else	// Waiting for enough frames to complete a group
	{
		// Is this the first frame in the video sequence?
		if (first_frame)
		{
			// Mark this frame as a key frame since it is the start of the sequence
			frame->iskey = true;
			encoder->output.iskey = true;
		}
		else
		{
			int width = frame->width;
			int height = frame->height;
			int group_index = encoder->group.count;
			int frame_number = encoder->frame_number;
			int encoded_format = encoder->encoded_format;

			// Increment the frame sequence number
			encoder->frame_number++;

			PutVideoFrameHeader(output, FRAME_TYPE_PFRAME, width, height, display_height, group_index,
								frame_number, encoded_format);

			// Update the frame count
			//encoder->frame_count++;

			// This frame is not a key frame
			frame->iskey = false;
			encoder->output.iskey = false;
		}
	}

finish:

	// Force output of any bits pending in the bitstream buffer
	FlushBitstream(output);

	if(frame->iskey)
		encoder->lastgopbitcount = output->nWordsUsed * 8;//output->cntBits;

	// Clear the mmx register state in case not cleared by the filter routines
	//_mm_empty();

	STOP(tk_compress);

#if (0 && DEBUG)
	if (logfile) {
		CODEC_ERROR error = encoder->error;
		fprintf(logfile, "Returning from encode sample, result: %d, error: %d\n", result, error);
	}
#endif

#if _TIMING
	DoThreadTiming(3);
#endif

	return result;
}

#endif


#if 1

// Finish the intra frame group transform for one channel
DWORD WINAPI FinishFrameTransformThread(LPVOID param)
{
	THREAD_FINISH_DATA *data = (THREAD_FINISH_DATA *)param;
	ENCODER *encoder = data->encoder;
	TRANSFORM *transform = data->transform;
	int channel = data->channel;
	int prescale = data->prescale;
	const DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;

	IMAGE *temporal;	// Temporal wavelet (two bands)
	IMAGE *wavelet;		// Spatio-temporal wavelet

	int num_frames = encoder->gop_length;
	int num_spatial = encoder->num_spatial;

	size_t size = transform->size;

	int quantization[IMAGE_NUM_BANDS];

	int wavelet_index = 0;
	int last_level = num_spatial + 1;
	int level = 0;
	int width;
	int height;
	int index;

	// Can only handle a group length of one
	assert(num_frames == 1);

	// Cannot exceed the maximum number of frames
	assert(num_frames <= WAVELET_MAX_FRAMES);

	// Allocate a buffer for image processing (if necessary)
	if (transform->buffer == NULL) {
		IMAGE *wavelet = transform->wavelet[0];
		assert(wavelet != NULL);
		size = wavelet->height * wavelet->pitch;
		transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
		assert(transform->buffer != NULL);
		transform->size = size;
	}

	// Have already computed the first wavelet at the base of the pyramid
	wavelet_index += num_frames;
	level = 1;

#if 0
	// Build a list of the channel threads processing each frame
	for (frame_index = 0; frame_index < CODEC_GOP_LENGTH; frame_index++)
	{
		HANDLE thread = encoder->frame_channel_thread[frame_index][channel];
		if (thread != INVALID_HANDLE_VALUE)
		{
			frame_channel_thread[frame_channel_count++] = thread;
		}
	}

	// Are any threads actively processing this channel in a first level transform?
	if (frame_channel_count > 0)
	{
		// Synchronize with the channel processing in the first level transforms
		WaitForMultipleObjects(frame_channel_count, frame_channel_thread, true, dwTimeout);

		// Indicate that all threads have completed
		for (frame_index = 0; frame_index < CODEC_GOP_LENGTH; frame_index++)
		{
			encoder->frame_channel_thread[frame_index][channel] = INVALID_HANDLE_VALUE;
		}
	}
#else
	if (encoder->frame_channel_thread[0][channel] != INVALID_HANDLE_VALUE)
	{
		WaitForSingleObject(encoder->frame_channel_thread[0][channel], dwTimeout);
		encoder->frame_channel_thread[0][channel] = INVALID_HANDLE_VALUE;
	}
#endif

	// Apply spatial transforms to the lowpass band of the first wavelet
	for (index = wavelet_index; index < last_level; index++)
	{
		IMAGE *wavelet;

		assert(0 < index && index < transform->num_wavelets);

		wavelet = transform->wavelet[index];
		wavelet = TransformForwardSpatial(transform->wavelet[index - 1], 0, wavelet, level + 1,
										  transform->buffer, transform->size, prescale, wavelet->quant, 0);

		if (wavelet == NULL) {
			// Record the number of levels in the wavelet pyramid
			transform->num_levels = level;

			// Record the number of wavelets
			transform->num_wavelets = wavelet_index + 1;
			return;
		}

		level++;
		wavelet_index++;
	}

	// Record the number levels in the wavelet pyramid
	transform->num_levels = level;

	// Record the number of wavelets
	transform->num_wavelets = wavelet_index;

	return 0;
}


// Finish the field group transform for one channel
DWORD WINAPI FinishFieldTransformThread(LPVOID param)
{
	THREAD_FINISH_DATA *data = (THREAD_FINISH_DATA *)param;
	TRANSFORM *transform = data->transform;
	int group_length = data->num_frames;
	int num_spatial = data->num_spatial;
	int prescale = data->prescale;

	IMAGE *temporal;	// Temporal wavelet (two bands)
	IMAGE *wavelet;		// Spatio-temporal wavelet
	size_t size = transform->size;
	//int background = ((channel == 0) ? COLOR_LUMA_BLACK : COLOR_CHROMA_ZERO);
	int level;
	int last_level;
	int wavelet_index = 0;
	int i;						// Index to the frame within the group

	// Can only handle a group length of two
	assert(group_length == 2);

	// Cannot exceed the maximum number of frames
	assert(group_length <= WAVELET_MAX_FRAMES);

#if 0
	// Allocate a buffer for image processing (if necessary)
	if (transform->buffer == NULL) {
		IMAGE *wavelet = transform->wavelet[0];
		assert(wavelet != NULL);
		size = wavelet->height * wavelet->pitch;
		transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
		assert(transform->buffer != NULL);
		transform->size = size;
	}
#elif 1
	// Allocate a buffer as large as the original frame (if necessary)
	if (transform->buffer == NULL) {
		int width = transform->height;
		int height = transform->width;
		int pitch = width * sizeof(PIXEL);
		size = height * ALIGN16(pitch);
		transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
		assert(transform->buffer != NULL);
		transform->size = size;
	}
#endif

	// Have already computed the frame transforms at the base of the wavelet pyramid
	wavelet_index += group_length;


	/***** Temporal Transform *****/

	// Compute a temporal wavelet between the two frame (temporal-horizontal) wavelets
	level = 2;
	temporal = transform->wavelet[wavelet_index++];
	TransformForwardTemporal(transform->wavelet[0], 0, transform->wavelet[1], 0, temporal, 0, temporal, 1);

	/***** Spatial Transforms *****/

	last_level = level + num_spatial;
	while (level < last_level)
	{
		// Compute the spatial wavelet transform
		int next_level = level + 1;
		assert(next_level < sizeof(transform->wavelet)/sizeof(transform->wavelet[0]));

		wavelet = TransformForwardSpatial(transform->wavelet[level], 0, transform->wavelet[next_level],
										  next_level, transform->buffer, transform->size, prescale, NULL, 0);
		if (wavelet == NULL) break;

		// Advance to the next level in the pyramid
		level = next_level;
	}

	// Record the number levels in the wavelet pyramid
	transform->num_levels = level;

	// Record the number of wavelets
	transform->num_wavelets = level + 1;

#if (0 && DEBUG && _WIN32)
	_CrtCheckMemory();
#endif
}

#endif


#if 1

// Finish the field plus group transform for one channel
DWORD WINAPI FinishFieldPlusTransformThread(LPVOID param)
{
	THREAD_FINISH_DATA *data = (THREAD_FINISH_DATA *)param;
	ENCODER *encoder = data->encoder;
	TRANSFORM *transform = data->transform;
	int channel = data->channel;
	int prescale = data->prescale;

	int frame_channel_count = 0;
	HANDLE frame_channel_thread[CODEC_GOP_LENGTH];
	const DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;

	IMAGE *temporal;	// Temporal wavelet (two bands)
	IMAGE *wavelet;		// Spatio-temporal wavelet

	int num_frames = encoder->gop_length;
	int num_spatial = encoder->num_spatial;

	int precision = encoder->codec.precision;

	size_t size = transform->size;

	int quantization[IMAGE_NUM_BANDS];

	int width, height, level;
	int last_level;
	int wavelet_index = 0;
	int index;

	int frame_index;		// Index to the frame within the group

	// Can only handle a group length of two
	assert(num_frames == 2);

	// Cannot exceed the maximum number of frames
	assert(num_frames <= WAVELET_MAX_FRAMES);

	// Allocate a buffer for image processing (if necessary)
	if (transform->buffer == NULL) {
		IMAGE *wavelet = transform->wavelet[0];
		assert(wavelet != NULL);
		size = wavelet->height * wavelet->pitch;
		transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
		assert(transform->buffer != NULL);
		transform->size = size;
	}

	// Have already computed the frame transforms at the base of the wavelet pyramid
	wavelet_index += num_frames;

#if 1
	// Build a list of the channel threads processing each frame
	for (frame_index = 0; frame_index < CODEC_GOP_LENGTH; frame_index++)
	{
		HANDLE thread = encoder->frame_channel_thread[frame_index][channel];
		if (thread != INVALID_HANDLE_VALUE)
		{
			frame_channel_thread[frame_channel_count++] = thread;
		}
	}

	// Are any threads actively processing this channel in a first level transform?
	if (frame_channel_count > 0)
	{
		// Synchronize with the channel processing in the first level transforms
		WaitForMultipleObjects(frame_channel_count, frame_channel_thread, true, dwTimeout);

		// Indicate that all threads have completed
		for (frame_index = 0; frame_index < CODEC_GOP_LENGTH; frame_index++)
		{
			encoder->frame_channel_thread[frame_index][channel] = INVALID_HANDLE_VALUE;
		}
	}
#endif


	/***** Perform the temporal transform between frames *****/

	// Compute a temporal wavelet between the two frame (temporal-horizontal) wavelets
	level = 2;
	temporal = transform->wavelet[wavelet_index];
	TransformForwardTemporal(transform->wavelet[0], 0, transform->wavelet[1], 0, temporal, 0, temporal, 1);


	/***** Apply spatial transforms to the temporal highpass band *****/

	assert(num_spatial == 3);

	assert((level + 1) < sizeof(transform->wavelet)/sizeof(transform->wavelet[0]));

	// Compute the spatial wavelet transform for the temporal highpass band
	wavelet = transform->wavelet[wavelet_index+1];
	wavelet = TransformForwardSpatial(transform->wavelet[wavelet_index], 1, wavelet, level+1,
									  transform->buffer, transform->size, 0, wavelet->quant, 0);
	if (wavelet == NULL) {
		// Record the number levels in the wavelet pyramid
		transform->num_levels = level;

		// Record the number of wavelets
		transform->num_wavelets = wavelet_index;
		return;
	}


	/***** Apply spatial transforms to the temporal lowpass band *****/

	index = wavelet_index + 2;

	// First spatial transform

#if _HIGHPASS_CODED
	wavelet = transform->wavelet[index];
	if (!TransformForwardSpatialCoded(encoder, transform->wavelet[wavelet_index], 0, wavelet, level + 1,
		transform->buffer, transform->size, prescale, wavelet->quant))
	{
		// Record the number levels in the wavelet pyramid
		transform->num_levels = level;

		// Record the number of wavelets
		transform->num_wavelets = wavelet_index+1;
		return;
	}
#else
	wavelet = transform->wavelet[index];
	wavelet = TransformForwardSpatial(transform->wavelet[wavelet_index], 0, wavelet, level+1,
									  transform->buffer, transform->size, prescale, wavelet->quant, 0);
	if (wavelet == NULL) {
		// Record the number levels in the wavelet pyramid
		transform->num_levels = level;

		// Record the number of wavelets
		transform->num_wavelets = wavelet_index+1;
		return;
	}
#endif

	wavelet_index += 2;

	level++;
	assert((level + 1) < sizeof(transform->wavelet)/sizeof(transform->wavelet[0]));

	index = wavelet_index + 1;

	// Second spatial transform
	wavelet = transform->wavelet[index];

	// Did the video source contain high resolution pixels?
	if (precision >= CODEC_PRECISION_10BIT)
	{
		prescale = 2;

		// Apply the spatial transform with prescaling
		wavelet = TransformForwardSpatial(transform->wavelet[wavelet_index], 0, wavelet, level+1,
										  transform->buffer, transform->size, prescale, wavelet->quant, 0);
	}
	else
	{
		// Apply the spatial transform without prescaling
		wavelet = TransformForwardSpatial(transform->wavelet[wavelet_index], 0, wavelet, level+1,
										  transform->buffer, transform->size, prescale, wavelet->quant, 0);
	}

	if (wavelet == NULL) {
		// Record the number levels in the wavelet pyramid
		transform->num_levels = level;

		// Record the number of wavelets
		transform->num_wavelets = wavelet_index;
		return;
	}

	level++;
	wavelet_index += 1;


finish:
	// Record the number levels in the wavelet pyramid
	transform->num_levels = level;

	// Record the number of wavelets
	transform->num_wavelets = wavelet_index+1;
}


// Compute the upper levels of the wavelet transform for a group of frames
void ComputeGroupTransformQuantThreaded(ENCODER *encoder, TRANSFORM *transform[], int num_transforms)
{
	//HANDLE thread[CODEC_MAX_CHANNELS];
	//THREAD_FINISH_DATA data[CODEC_MAX_CHANNELS];
	THREAD_FINISH_DATA *data = encoder->thread_finish_data;
	DWORD dwThreadID;
	DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;
	int32_t result;
	int channel;

	// Copy parameters from the encoder into the transform data structures
	int num_frames = encoder->gop_length;
	int num_spatial = encoder->num_spatial;

	START(tk_finish);

	for (channel = 0; channel < num_transforms; channel++)
	{
		HANDLE thread;

		int prescale = 0;
		int affinity;
		int result;

		assert(transform[channel]->type == TRANSFORM_TYPE_SPATIAL ||
			   transform[channel]->type == TRANSFORM_TYPE_FIELD   ||
			   transform[channel]->type == TRANSFORM_TYPE_FIELDPLUS);

		transform[channel]->num_frames = num_frames;
		transform[channel]->num_spatial = num_spatial;

		// Fill the thread data structure for this channel
		data[channel].encoder = encoder;
		data[channel].transform = transform[channel];
		data[channel].channel = channel;
		data[channel].prescale = prescale;
		data[channel].num_frames = num_frames;
		data[channel].num_spatial = num_spatial;

		// Determine the processor in which this thread should run
		affinity = GetEncoderAffinityMask(encoder, channel);

		// Compute the temporal and spatial wavelets to finish the transform
		switch (transform[channel]->type)
		{
		case TRANSFORM_TYPE_SPATIAL:
			//FinishFrameTransformQuantThreaded(encoder, transform[channel], channel, prescale);
			//thread[channel] = CreateThread(NULL, 0, FinishFrameTransformThread, &data[channel], 0, &dwThreadID);
			thread = CreateThread(NULL, 0, FinishFrameTransformThread, &data[channel], 0, &dwThreadID);
			break;

		case TRANSFORM_TYPE_FIELD:
			//FinishFieldTransformThreaded(encoder, transform[channel], num_frames, num_spatial, prescale);
			//thread[channel] = CreateThread(NULL, 0, FinishFieldTransformThread, &data[channel], 0, &dwThreadID);
			thread = CreateThread(NULL, 0, FinishFieldTransformThread, &data[channel], 0, &dwThreadID);
			break;

		case TRANSFORM_TYPE_FIELDPLUS:
			//FinishFieldPlusTransformQuantThreaded(encoder, transform[channel], channel, prescale);
			//thread[channel] = CreateThread(NULL, 0, FinishFieldPlusTransformThread, &data[channel], 0, &dwThreadID);
			thread = CreateThread(NULL, 0, FinishFieldPlusTransformThread, &data[channel], 0, &dwThreadID);
			break;

		default:	// Transform type is not supported
			assert(0);
			break;
		}

		// Set the processor on which this thread should run
		result = SetThreadAffinityMask(thread, affinity);
		assert(result != 0);

		// Record the thread that is processing this chanel
		encoder->finish_channel_thread[channel] = thread;
	}

#if 0
	// Wait for the threads to finish
	result = WaitForMultipleObjects(num_transforms, thread, true, dwTimeout);
	assert(result == 0);
#endif

	STOP(tk_finish);
}

#endif


#if 1

DWORD WINAPI EncodeQuantizedChannelThread(LPVOID param)
{
	THREAD_ENCODE_DATA *data = (THREAD_ENCODE_DATA *)param;
	ENCODER *encoder = data->encoder;
	BITSTREAM *output = data->bitstream;
	TRANSFORM *transform = data->transform;
	int channel = data->channel;
	size_t encoded_channel_size;
	int num_wavelets;
	IMAGE *lowpass;
	int subband = 0;

	// Check for valid data passed to the thread
	assert(encoder != NULL);
	assert(output != NULL);
	assert(transform != NULL);
	assert(0 <= channel && channel <= CODEC_MAX_CHANNELS);

	// Wait for the transforms corresponding to this channel to finish
	if (encoder->finish_channel_thread[channel] != INVALID_HANDLE_VALUE)
	{
		DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;

		WaitForSingleObject(encoder->finish_channel_thread[channel], dwTimeout);
		encoder->finish_channel_thread[channel] = INVALID_HANDLE_VALUE;
	}

	// Zero the number of bytes written to the channel
	data->channel_size = 0;

	// Align the start of the encoded channel on a bitword boundary
	PadBits(output);

	// Output a channel header between channels
	if (channel > 0) {
		PutVideoChannelHeader(output, channel);
	}

	// Remember the beginning of the channel data
	encoded_channel_size = BitstreamSize(output);

	// Get the wavelet that contains the lowpass band that will be encoded
	num_wavelets = transform->num_wavelets;
	lowpass = transform->wavelet[num_wavelets - 1];

#if 0
	// Compute the lowpass band statistics used for encoding
	ComputeLowPassStatistics(encoder, lowpass);
#endif

	// Encode the lowest resolution image from the top of the wavelet pyramid
	EncodeLowPassBand(encoder, output, lowpass, channel, subband++);

	switch(transform->type)
	{
	case TRANSFORM_TYPE_SPATIAL:
		EncodeQuantizedFrameTransform(encoder, transform, output);
		break;

	case TRANSFORM_TYPE_FIELD:
		EncodeQuantizedFieldTransform(encoder, transform, output);
		break;

	case TRANSFORM_TYPE_FIELDPLUS:
		EncodeQuantizedFieldPlusTransform(encoder, transform, output);
		break;

	default:
		assert(0);	// Invalid type of wavelet transform data structure
		break;
	}

	// Align the end of the channel on a bitword boundary
	PadBits(output);

	// Compute the number of bytes used for encoding this channel
	encoded_channel_size = BitstreamSize(output) - encoded_channel_size;

	// Return the number of bytes encoded in the bitstream
	data->channel_size = encoded_channel_size;

	return 0;
}
#endif


#if 0
void EncodeQuantizedChannelThreaded(ENCODER *encoder, TRANSFORM *transform, BITSTREAM *output)
{
	int num_wavelets = transform[channel]->num_wavelets;
	IMAGE *lowpass;
	bool temporal_runs_encoded = true;
	int k;
	int channel_size_in_byte;

#if (0 && DEBUG)
	if (logfile) {
		char label[256];
		sprintf(label, "Channel %d transform", channel);
		DumpTransform(label, transform[channel], logfile);
		fprintf(logfile, "\n");
	}
#endif
	// Align start of channel on a bitword boundary
	PadBits(output);

	// Output a channel header between channels
	if (channel > 0) {
		PutVideoChannelHeader(output, channel);
	}

	// Remember the beginning of the channel data
	channel_size_in_byte = BitstreamSize(output);

#if 0

	// Get the wavelet that contains the lowpass band that will be encoded
	lowpass = transform[channel]->wavelet[num_wavelets - 1];
#if 0
	// Compute the lowpass band statistics used for encoding
	ComputeLowPassStatistics(encoder, lowpass);
#endif
	// Encode the lowest resolution image from the top of the wavelet pyramid
	EncodeLowPassBand(encoder, output, lowpass, channel, subband++);

	switch(transform[channel]->type)
	{
	case TRANSFORM_TYPE_SPATIAL:
		EncodeQuantizedFrameTransform(encoder, transform[channel], output);
		break;

	case TRANSFORM_TYPE_FIELD:
		EncodeQuantizedFieldTransform(encoder, transform[channel], output);
		break;

	case TRANSFORM_TYPE_FIELDPLUS:
		EncodeQuantizedFieldPlusTransform(encoder, transform[channel], output);
		break;

	default:
		assert(0);	// Can only handle field or field+ transforms now
		break;
	}

	// Should have processed all subbands.  Fix this assertion after deciding
	// whether the number of subbands is defined in the encoder structure
	//assert(subband == encoder->num_subbands);

	// Output a channel trailer?

	// Align end of channel on a bitword boundary
	PadBits(output);

	// Compute the number of bytes used for encoding this channel
	channel_size_in_byte = BitstreamSize(output) - channel_size_in_byte;

#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Channel: %d, size: %d (bytes)\n", channel, channel_size_in_byte);
	}
#endif
	// Write the number of bytes used to code this channel in the channel size table
	channel_size_vector[channel] = ReverseByteOrder(channel_size_in_byte);

	// Start numbering the subbands in the next channel beginning with zero
	subband = 0;
}
#endif


#if 1

// Simplified routine for encoding the group transform
void EncodeQuantizedGroupThreaded(ENCODER *encoder, TRANSFORM *transform[], int num_transforms,
								  BITSTREAM *output, uint8_t *buffer, size_t buffer_size)
{
	FILE *logfile = encoder->logfile;
	bool encode_iframe;
	int num_channels;
	int subband_count;
	int channel;
	int k;

	HANDLE thread[CODEC_MAX_CHANNELS];
	BITSTREAM chroma_bitstream[2];
	size_t chroma_buffer_size;
	uint8_t *chroma_buffer[2];
	//THREAD_ENCODE_DATA data[3];
	THREAD_ENCODE_DATA *data = encoder->thread_encode_data;

	DWORD *channel_size_vector;		// Pointer to vector of channel sizes
	DWORD dwThreadID;
	DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;
	int32_t result;

	// Count the subbands as they are encoded
	int subband = 0;

	// Verify that the codebooks are valid
#if DEBUG
	assert(ValidCodebooks());;
#endif

	// Verify that there are three channels
	assert(num_transforms == 3);

	START(tk_encoding);

	// Allocate space for encoding the chroma channels
	chroma_buffer_size = buffer_size/2;
	chroma_buffer[0] = buffer;
	chroma_buffer[1] = buffer + chroma_buffer_size;

	InitBitstream(&chroma_bitstream[0]);
	InitBitstream(&chroma_bitstream[1]);

	SetBitstreamBuffer(&chroma_bitstream[0], chroma_buffer[0], chroma_buffer_size, BITSTREAM_ACCESS_WRITE);
	SetBitstreamBuffer(&chroma_bitstream[1], chroma_buffer[1], chroma_buffer_size, BITSTREAM_ACCESS_WRITE);

	num_channels = num_transforms;
	subband_count = SubbandCount(transform[0]);

	// Increment the sequence number of the encoded frame
	encoder->frame_number++;

	if (encoder->gop_length > 1)
	{
		uint32_t frame_number = encoder->frame_number;
		int precision = encoder->codec.precision;

		// Output the header for the group of frames
		PutVideoGroupHeader(output, transform[0], num_channels, subband_count,
							&channel_size_vector, precision, frame_number,
							encoder->encoder_quality, encoder->encoded_format,
							encoder->input.width, encoder->input.height, encoder->input.display_height);
		encode_iframe = false;
	}
	else
	{
		uint32_t frame_number = encoder->frame_number;
		int precision = encoder->codec.precision;

		assert(encoder->gop_length == 1);

		// Output the header for an intra frame
		PutVideoIntraFrameHeader(output, transform[0], num_channels, subband_count,
								 &channel_size_vector, precision, frame_number,
								 encoder->encoder_quality, encoder->encoded_format,
								 encoder->input.width, encoder->input.height, encoder->input.display_height);
		encode_iframe = true;
	}

	if(encoder->metadata && encoder->metasize)
	{
		PutTagPairOptional(output, CODEC_TAG_METADATA, encoder->metasize>>2);

		// Ouput table directly -- faster than PutLong or PutBits etc.
		memcpy(output->lpCurrentWord, encoder->metadata, encoder->metasize);
		output->nWordsUsed += encoder->metasize;
		output->lpCurrentWord += encoder->metasize;
	}

	// Write the optional tags in the group header extension
	PutVideoGroupExtension(output, &encoder->codec);

#if _CODEC_SAMPLE_FLAGS
	// Write the flag bits that encode the codec state
	PutVideoSampleFlags(output, &encoder->codec);
#endif

	for (channel = 0; channel < num_channels; channel++)
	{
		// Determine the processor in which this thread should run
		int affinity = GetEncoderAffinityMask(encoder, channel);
		int result;

		// Initialize the data structure for this thread
		data[channel].encoder = encoder;
		data[channel].transform = transform[channel];
		data[channel].channel = channel;
		data[channel].channel_size = 0;

		// Is this the luma or one of the chroma channels?
		if (channel == 0)
		{
			// Write encoded luma coefficients directly to the output
			data[0].bitstream = output;
		}
		else
		{
			const int chroma_channel = channel - 1;

			// Write encoded chroma coefficients to an intermediate buffer
			data[channel].bitstream = &chroma_bitstream[chroma_channel];
		}

		// Start a thread to encode the coefficients for this channel
		thread[channel] = CreateThread(NULL, 0, EncodeQuantizedChannelThread, &data[channel], 0, &dwThreadID);

		// Set the processor on which this thread should run
		result = SetThreadAffinityMask(thread[channel], affinity);
		assert(result != 0);
	}

	// Wait for the first channel thread to finish
	result = WaitForSingleObject(thread[0], dwTimeout);

	// Copy the encoded chroma into the output bitstream
	for (channel = 1; channel < num_channels; channel++)
	{
		const int chroma_channel = channel - 1;
		BITSTREAM *bitstream = &chroma_bitstream[chroma_channel];

		// Wait for this channel thread to finish
		result = WaitForSingleObject(thread[channel], dwTimeout);

		// Copy the chroma bitstream to the output bitstream
		CopyBitstream(bitstream, output);
	}

	// Record the number of bytes written to each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Write the number of bytes used to code this channel in the channel size table
		//channel_size_vector[channel] = ReverseByteOrder(channel_size_in_byte);
		channel_size_vector[channel] = ReverseByteOrder(data[channel].channel_size);

		// Start numbering the subbands in the next channel beginning with zero
		//subband = 0;
	}

	// Write the group trailer
	if (encode_iframe)
	{
		// Output the trailer for an intra frame
		PutVideoIntraFrameTrailer(output);
	}
	else
	{
		// Output the trailer for the group of frames
		PutVideoGroupTrailer(output);
	}

	STOP(tk_encoding);
}

#endif


#if 0

// Compute the upper levels of the wavelet transform for one channel in a group of frames
void ComputeChannelTransformQuant(ENCODER *encoder, TRANSFORM *transform, int channel)
{
	const int prescale = 0;

	// Copy parameters from the encoder into the transform data structures
	int num_frames = encoder->gop_length;
	int num_spatial = encoder->num_spatial;

	transform->num_frames = num_frames;
	transform->num_spatial = num_spatial;

	// Check that the transform type is valid
	assert(transform->type == TRANSFORM_TYPE_SPATIAL ||
		   transform->type == TRANSFORM_TYPE_FIELD   ||
		   transform->type == TRANSFORM_TYPE_FIELDPLUS);

	// Compute the temporal and spatial wavelets to finish the transform
	switch (transform->type)
	{
	case TRANSFORM_TYPE_SPATIAL:
		FinishFrameTransformQuant(encoder, transform, channel, prescale);
		break;

	case TRANSFORM_TYPE_FIELD:
		FinishFieldTransform(transform, num_frames, num_spatial, prescale);
		break;

	case TRANSFORM_TYPE_FIELDPLUS:
		FinishFieldPlusTransformQuant(encoder, transform, channel, prescale);
		break;

	default:	// Transform type is not supported
		assert(0);
		break;
	}
}

#endif


#if 0

void EncodeQuantizedGroupHeader(ENCODER *encoder, TRANSFORM *transform[], int num_channels, BITSTREAM *output)
{
	int subband_count = SubbandCount(transform[0]);

	// Increment the sequence number of the encoded frame
	encoder->frame_number++;

	if (encoder->gop_length > 1)
	{
		uint32_t frame_number = encoder->frame_number;
		int precision = encoder->codec.precision;

		// Output the header for the group of frames
		PutVideoGroupHeader(output, transform[0], num_channels, subband_count,
							&encoder->channel_size_vector, precision, frame_number,
							encoder->encoder_quality, encoder->encoded_format);
		//encode_iframe = false;
	}
	else
	{
		uint32_t frame_number = encoder->frame_number;
		int precision = encoder->codec.precision;

		assert(encoder->gop_length == 1);

		// Output the header for an intra frame
		PutVideoIntraFrameHeader(output, transform[0], num_channels, subband_count,
								 &encoder->channel_size_vector, precision, frame_number,
								 encoder->encoder_quality, encoder->encoded_format,
								 encoder->input.width, encoder->input.height, encoder->input.display_height);
		//encode_iframe = true;
	}


	if(encoder->metadata && encoder->metasize)
	{
		PutTagPairOptional(output, CODEC_TAG_METADATA, encoder->metasize>>2);

		// Ouput table directly -- faster than PutLong or PutBits etc.
		memcpy(output->lpCurrentWord, encoder->metadata, encoder->metasize);
		output->nWordsUsed += encoder->metasize;
		output->lpCurrentWord += encoder->metasize;
	}

	// Write the optional tags in the group header extension
	PutVideoGroupExtension(output, &encoder->codec);

#if _CODEC_SAMPLE_FLAGS
	// Write the flag bits that encode the codec state
	PutVideoSampleFlags(output, &encoder->codec);
#endif
}

#endif


#if 0

// Encode the transform for one channel
void EncodeQuantizedChannel(ENCODER *encoder, TRANSFORM *transform, int channel, BITSTREAM *output)
{
	FILE *logfile = encoder->logfile;
	bool encode_iframe;
	//int num_channels;
	int subband_count;
	//int channel;
	int k;
	//uint8_t  *temp;

	//DWORD *channel_size_vector;		// Pointer to vector of channel sizes

	// Count the subbands as they are encoded
	int subband = 0;

	// Verify that the codebooks are valid
#if DEBUG
	assert(ValidCodebooks());
#endif

	// Verify that there are three channels
	//assert(num_transforms == 3);

	//num_channels = num_transforms;
	//subband_count = SubbandCount(transform[0]);
	//temp = output->lpCurrentWord;


	//for (channel = 0; channel < num_channels; channel++)
	{
		int num_wavelets = transform->num_wavelets;
		IMAGE *lowpass;
		bool temporal_runs_encoded = true;
		int k;
		int channel_size;

#if (0 && DEBUG)
		if (logfile) {
			char label[256];
			sprintf(label, "Channel %d transform", channel);
			DumpTransform(label, transform, logfile);
			fprintf(logfile, "\n");
		}
#endif
		// Align start of channel on a bitword boundary
		PadBits(output);

		// Output a channel header between channels
		if (channel > 0) {
			PutVideoChannelHeader(output, channel);
		}

		// Remember the beginning of the channel data
		channel_size = BitstreamSize(output);

		// Get the wavelet that contains the lowpass band that will be encoded
		lowpass = transform->wavelet[num_wavelets - 1];
#if 0
		// Compute the lowpass band statistics used for encoding
		ComputeLowPassStatistics(encoder, lowpass);
#endif
		// Encode the lowest resolution image from the top of the wavelet pyramid
		EncodeLowPassBand(encoder, output, lowpass, channel, subband++);

		switch(transform->type)
		{
		case TRANSFORM_TYPE_SPATIAL:
			EncodeQuantizedFrameTransform(encoder, transform, output);
			break;

		case TRANSFORM_TYPE_FIELD:
			EncodeQuantizedFieldTransform(encoder, transform, output);
			break;

		case TRANSFORM_TYPE_FIELDPLUS:
			EncodeQuantizedFieldPlusTransform(encoder, transform, output);
			break;

		default:
			assert(0);	// Can only handle field or field+ transforms now
			break;
		}

		// Should have processed all subbands.  Fix this assertion after deciding
		// whether the number of subbands is defined in the encoder structure
		//assert(subband == encoder->num_subbands);

		// Output a channel trailer?

		// Align end of channel on a bitword boundary
		PadBits(output);

		// Compute the number of bytes used for encoding this channel
		channel_size = BitstreamSize(output) - channel_size;

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Channel: %d, size: %d (bytes)\n", channel, channel_size);
		}
#endif
		// Write the number of bytes used to code this channel in the channel size table
		encoder->channel_size_vector[channel] = ReverseByteOrder(channel_size);

		// Start numbering the subbands in the next channel beginning with zero
		//subband = 0;
	}
}

#endif


#if 0

void EncodeQuantizedGroupTrailer(ENCODER *encoder, bool iframe, BITSTREAM *output)
{
	if (iframe)
	{
		// Output the trailer for an intra frame
		PutVideoIntraFrameTrailer(output);
	}
	else
	{
		// Output the trailer for the group of frames
		PutVideoGroupTrailer(output);
	}
}

#endif


#if 0

typedef struct _encode_channel_data
{
	ENCODER *encoder;
	TRANSFORM *transform;
	BITSTREAM *bitstream;
	int channel;

} ENCODE_CHANNEL_DATA;


// Encode one channel of video in its own thread
DWORD WINAPI EncodeChannelThread(LPVOID param)
{
	ENCODE_CHANNEL_DATA *data = (ENCODE_CHANNEL_DATA *)param;
	ENCODER *encoder = data->encoder;
	TRANSFORM *transform = data->transform;
	BITSTREAM *bitstream = data->bitstream;
	int channel = data->channel;

	ComputeChannelTransformQuant(encoder, transform, channel);

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Before encode quantized group\n");
	}
#endif

	// Encode the transform for the current group of frames
	EncodeQuantizedChannel(encoder, transform, channel, bitstream);

	return 0;
}


// Encode a video group using different threads for each channel
bool EncodeChannelsThreaded(ENCODER *encoder, uint8_t * data, int width, int height, int pitch, int format,
							TRANSFORM *transform[], int num_transforms, BITSTREAM *output,
							PIXEL *buffer, size_t buffer_size, int i_fixedquality, int fixedbitrate)
{
	FILE *logfile = encoder->logfile;
	bool result = true;
	bool first_frame = false;
	FRAME *frame;
	QUANTIZER *q = &encoder->q;
	int chroma_width = width/2;
	int chroma_offset = encoder->codec.chroma_offset;
	int transform_type = (encoder->gop_length > 1) ? TRANSFORM_TYPE_FIELDPLUS : TRANSFORM_TYPE_SPATIAL;
	int i, j;
	int fixedquality = i_fixedquality;

	CODEC_STATE *codec = &encoder->codec;

	encoder->encoder_quality = fixedquality;

	if (ISBAYER(format)) {
		chroma_width = width;
	}

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Encode sample, width: %d, height: %d, format: %d\n", width, height, format);
	}
#endif

	// The transforms should have already been allocated
	assert(transform != NULL && transform[0] != NULL);

	// Check that the frame dimensions correspond to the dimensions used for the transform
	assert(width == transform[0]->width);
	assert(height == transform[0]->height);

	// Check that the frame dimensions are transformable
	assert(IsFrameTransformable(chroma_width, height, transform_type, encoder->num_spatial));

	/*
		Note: The transform is allocated and the fields are initialized when the encoder is
		initialized.  The dimensions and type of the transform is not chanaged during encoding.
		Not sure what will happen if the frame dimensions do not correspond to the dimensions
		used to initialize the transform data structure.  Probably should crop or fill the frame
		to the dimensions defined in the transform data structure when the packed frame provided
		as input to the encoder is converted to planes of luma and chroma channels.
	*/

	// Start compressing the frame (including conversion time)
	START(tk_compress);

	// Allocate a data structure for the unpacked frame
	SetEncoderFormat(encoder, width, height, display_height, format);

	// Get the frame for storing the unpacked data
	frame = encoder->frame;
	assert(frame != NULL);

	// Convert the packed color to planes of YUV 4:2:2 (one byte per pixel)
	START(tk_convert);
	switch (format)
	{
	case COLOR_FORMAT_UYVY:
		break;

	case COLOR_FORMAT_RGB24:
//  #if BUILD_PROSPECT //10-bit for everyone
		format = COLOR_FORMAT_YU64;
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
		encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
//  #endif
		// Convert the packed RGB data to planes of YUV 4:2:2 (one byte per pixel)
		ConvertRGB32to10bitYUVFrame(data, pitch, frame, (uint8_t *)buffer, buffer_size, encoder->input.color_space, encoder->codec.precision, false, 0);
		break;

	case COLOR_FORMAT_YUYV:
		break;

	case COLOR_FORMAT_RGB32:
//  #if BUILD_PROSPECT //10-bit for everyone
		format = COLOR_FORMAT_YU64;
		encoder->codec.precision = CODEC_PRECISION_10BIT;
		fixedquality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
		encoder->encoder_quality |= 0x1a00000;//CFEncode_Temporal_Quality_32;
//  #endif
		// Convert the packed RGBA data to planes of YUV 4:2:2 (one byte per pixel)
		// The data in the alpha channel is not used
		ConvertRGB32to10bitYUVFrame(data, pitch, frame, (uint8_t *)buffer, buffer_size,
			encoder->input.color_space, encoder->codec.precision, true, 0);
		break;
#if BUILD_PROSPECT
	case COLOR_FORMAT_V210:
		// Convert the packed 10-bit YUV 4:2:2 to planes of 8-bit YUV
		ConvertV210ToFrame8u(data, pitch, frame, (uint8_t *)buffer);
		break;
#endif

	default:
		// Cannot handle this color format
		return false;
	}
	STOP(tk_convert);

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Quantization fixed quality: %d, fixed bitrate: %d\n", fixedquality, fixedbitrate);
	}
#endif

	if (fixedquality&0xffff == 0)
		QuantizationSetRate(q, fixedbitrate, encoder->progressive, encoder->codec.precision, encoder->gop_length);
	else
		QuantizationSetQuality(q,
			fixedquality,
			encoder->progressive,
			encoder->codec.precision,
			encoder->gop_length,
			encoder->frame,
			encoder->lastgopbitcount>>3,
			encoder->video_channels);

#if (0 && DEBUG)
	if (logfile) {
		PrintQuantizer(q, logfile);
	}
#endif

	// Is this the first frame in the GOP?
	if (encoder->group.count == 0)
	{
		int channel;

		// Set the quantization for the transforms in this GOP
		for (channel = 0; channel < num_transforms; channel++)
		{
			SetTransformQuantization(encoder, transform[channel], channel);
		}
	}

#if (0 && DEBUG)
	if (logfile) {
		int k;

		for (k = 0; k < num_transforms; k++) {
			fprintf(logfile, "Quantization for channel: %d\n", k);
			PrintTransformQuantization(transform[k], logfile);
			fprintf(logfile, "\n");
		}
	}
#endif

	// Is this the first frame in the video sequence?
	if (encoder->frame_count == 0 && encoder->group.count == 0 && encoder->gop_length > 1)
	{
		// Note: Do not write out the video sequence header when encoding one frame groups

		// Fill the first sample with the video sequence header
	//	result = EncodeFirstSample(encoder, transform, num_transforms, frame, output, format);
	//	if (!result) goto finish;

		encoder->output.iskey = true;

		first_frame = true;
	}

	// Determine the index of this frame in the group
	j = encoder->group.count;

	// Should be the first or second frame in a two frame group
	assert(0 <= j && j <= 1);

	// Set the number of channels in the encoder quantization table
	encoder->num_quant_channels = num_transforms;

	// Which wavelet transform should be used at the lowest level:
	// frame transform (interlaced) or spatial transform (progressive)
	//if (!encoder->progressive)
	if (0)
	{
		int frame_index = j;

#if _NEW_DECODER
		// Interlaced frame encoding (implemented using the frame transform)
		codec->progressive = 0;
#endif

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Transform forward frame: %d, progressive: %d\n", frame_index, encoder->progressive);
		}
#endif

		if (format == COLOR_FORMAT_YUYV)
		{
			FRAME_INFO info = {width, height, format};
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			// Apply the frame transform directly to the frame
			TransformForwardFrameYUV(data, pitch, &info, transform, frame_index, num_transforms,
									 (char *)buffer, buffer_size, chroma_offset, codec->precision, limit_yuv, conv_601_709);
		}
		else if (format == COLOR_FORMAT_UYVY)
		{
			FRAME_INFO info = {width, height, format};
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			// Apply the frame transform directly to the frame
			TransformForwardFrameYUV(data, pitch, &info, transform, frame_index, num_transforms,
									 (char *)buffer, buffer_size, chroma_offset, codec->precision, limit_yuv, conv_601_709);
		}
		else
		{
			// Apply the frame wavelet transform to each plane
			for (i = 0; i < num_transforms; i++)
			{
				//int k;

				IMAGE *image = frame->channel[i];
				IMAGE *wavelet = transform[i]->wavelet[j];

				// The lowpass band must be one byte pixels
				assert(image->pixel_type[0] == PIXEL_TYPE_8U);

				// Apply the frame transform to the image plane for this channel
				TransformForwardFrame(image, wavelet, buffer, buffer_size, chroma_offset, wavelet->quant);
			}
		}
	}
	else
	{
		int frame_index = j;

#if _NEW_DECODER
		// Progressive frame transform (implemented using the spatial transform)
		codec->progressive = 1;
#endif

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Transform forward spatial: %d, progressive: %d\n", frame_index, encoder->progressive);
		}
#endif

		if (format == COLOR_FORMAT_YUYV)
		{
			FRAME_INFO info = {width, height, format};
			//int frame_index = (j == 0) ? 1 : 0;
			//int frame_index = j;
			//int chroma_offset = encoder->codec.chroma_offset;

			// Apply the frame transform directly to the frame
			TransformForwardSpatialThreadedYUV(data, pitch, &info, transform, frame_index, num_transforms,
											   buffer, buffer_size, chroma_offset);
		}
		else
		{
			const int level = 1;

			// Apply the transform to the individual channels
			TransformForwardSpatialThreadedChannels(frame, frame_index, transform, level, buffer, buffer_size);
		}

#if TIMING
		// Count the number of progressive frames that were encoded
		progressive_encode_count++;
#endif
	}

	if(first_frame)
	{
		EncodeFirstSample(encoder, transform, num_transforms, frame, output, format);
	}

	// Increment the count of the number of frames in the group
	encoder->group.count++;

#if 1
	if (encoder->gop_length == 1)
	{
		// Compute the spatial transform wavelet tree for each channel
		ComputeGroupTransformQuant(encoder, transform, num_transforms);

		// Encode the transform for the current frame
		EncodeQuantizedGroup(encoder, transform, num_transforms, output, 0, 0);

		//DAN Variable Bit Rate control feedback.
		//encoder->lastgopbitcount = output->nWordsUsed * 8;//output->cntBits;
		//output->cntBits = 0;

		// Reset the group of frames
		encoder->group.count = 0;

		// Update the frame count
		encoder->frame_count += encoder->gop_length;

		// Mark this frame as a key frame since it is an intra frame
		frame->iskey = true;
		encoder->output.iskey = true;
	}
	else
#endif

	// Enough frames to compute the rest of the wavelet transform?
	if (encoder->group.count == encoder->gop_length)
	{
		ENCODE_CHANNEL_DATA data[CODEC_MAX_CHANNELS];
		HANDLE thread[CODEC_MAX_CHANNELS];
		BITSTREAM chroma_bitstream[2];
		size_t chroma_buffer_size;
		uint8_t *chroma_buffer[2];
		DWORD dwThreadID;
		DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;
		int num_channels = num_transforms;
		int channel;

		// Allocate space for encoding the chroma channels
		chroma_buffer_size = buffer_size/2;
		chroma_buffer[0] = (uint8_t *)buffer;
		chroma_buffer[1] = (uint8_t *)buffer + chroma_buffer_size;

		InitBitstream(&chroma_bitstream[0]);
		InitBitstream(&chroma_bitstream[1]);

		SetBitstreamBuffer(&chroma_bitstream[0], chroma_buffer[0], chroma_buffer_size, BITSTREAM_ACCESS_WRITE);
		SetBitstreamBuffer(&chroma_bitstream[1], chroma_buffer[1], chroma_buffer_size, BITSTREAM_ACCESS_WRITE);

		// Encode the group header
		EncodeQuantizedGroupHeader(encoder, transform, num_channels, output);

		// Start a thread for processing each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			// Fill the data structure for this channel
			data[channel].encoder = encoder;
			data[channel].transform = transform[channel];
			data[channel].channel = channel;

			// Is this the luma or one of the chroma channels?
			if (channel == 0)
			{
				// Write encoded luma coefficients directly to the output
				data[0].bitstream = output;
			}
			else
			{
				const int chroma_channel = channel - 1;

				// Write encoded chroma coefficients to an intermediate buffer
				data[channel].bitstream = &chroma_bitstream[chroma_channel];
			}

			// Start a thread for processing this channel
			thread[channel] = CreateThread(NULL, 0, EncodeChannelThread, &data[channel], 0, &dwThreadID);
		}

		// Wait for all of the channels to finish
		result = WaitForMultipleObjects(num_channels, thread, true, dwTimeout);

		// Copy the encoded chroma coefficients to the output bitstream
		for (channel = 1; channel < num_channels; channel++)
		{
			const int chroma_channel = channel - 1;
			BITSTREAM *bitstream = &chroma_bitstream[chroma_channel];

			// Copy the chroma bitstream to the output bitstream
			CopyBitstream(bitstream, output);
		}

		// Encode the group trailer
		EncodeQuantizedGroupTrailer(encoder, (encoder->gop_length == 1), output);

		//DAN Variable Bit Rate control feedback.
		//encoder->lastgopbitcount = output->nWordsUsed * 8;//output->cntBits;
		//output->cntBits = 0;

		// Reset the group of frames
		encoder->group.count = 0;

		// Update the frame count
		encoder->frame_count += encoder->gop_length;

		// Mark this frame as a key frame since it is the start of a group
		frame->iskey = true;
		encoder->output.iskey = true;

#if (0 && DEBUG)
		if (logfile) {
#if 0
			fprintf(logfile, "Encoded transforms (all channels):\n\n");
			for (channel = 0; channel < num_transforms; channel++)
#else
			channel = 0;
			fprintf(logfile, "Encoded transforms, channel %d:\n\n", channel);
#endif
			{
				char label[256];
				int row = 1;

				sprintf(label, "Channel %d wavelets", channel);
				DumpTransform(label, transform[channel], row, logfile);
				fprintf(logfile, "\n");
			}
		}
#endif

	}
	else	// Waiting for enough frames to complete a group
	{
		// Is this the first frame in the video sequence?
		if (first_frame)
		{
			// Mark this frame as a key frame since it is the start of the sequence
			frame->iskey = true;
			encoder->output.iskey = true;
		}
		else
		{
			int width = frame->width;
			int height = frame->height;
			int group_index = encoder->group.count;
			int frame_number = encoder->frame_number;
			int encoded_format = encoder->encoded_format;

			// Increment the frame sequence number
			encoder->frame_number++;

			PutVideoFrameHeader(output, FRAME_TYPE_PFRAME, width, height, display_height, group_index,
								frame_number, encoded_format);

			// Update the frame count
			//encoder->frame_count++;

			// This frame is not a key frame
			frame->iskey = false;
			encoder->output.iskey = false;
		}
	}

finish:

	// Force output of any bits pending in the bitstream buffer
	FlushBitstream(output);

	if (frame->iskey) {
		encoder->lastgopbitcount = output->nWordsUsed * 8;	//output->cntBits;
	}

	// Clear the mmx register state in case not cleared by the filter routines
	//_mm_empty();

	STOP(tk_compress);

#if (0 && DEBUG)
	if (logfile) {
		CODEC_ERROR error = encoder->error;
		fprintf(logfile, "Returning from encode sample, result: %d, error: %d\n", result, error);
	}
#endif

	return result;
}

#endif


DWORD WINAPI TransformForwardSpatialYUVThread(LPVOID param)
{

	uint8_t *input;
	int input_pitch;
	FRAME_INFO *frame;
	TRANSFORM **transform;
	int frame_index;
	int num_channels;
	PIXEL *buffer;
	size_t buffer_size;
	int chroma_offset;

	THREAD_FRAME_DATA *data = (THREAD_FRAME_DATA *)param;

	input = data->input;
	input_pitch = data->input_pitch;
	frame = &data->frame;
	transform = data->transform;
	frame_index = data->frame_index;
	num_channels = data->num_channels;
	buffer = data->buffer;
	buffer_size = data->buffer_size;
	chroma_offset = data->chroma_offset;

#if 1
	// Apply the forward spatial transform to the current frame of packed YUV data
	TransformForwardSpatialYUV(input, input_pitch, frame, transform, frame_index, num_channels,
							   buffer, buffer_size, chroma_offset, false, 8, limit_yuv, conv_601_709);
#else
	// Unpack the current frame of YUV data and apply the forward spatial transform
	TransformForwardSpatialYUVPlanar(input, input_pitch, frame, transform, frame_index,
									 num_channels, buffer, buffer_size, chroma_offset);
#endif

	return 0;
}

void TransformForwardSpatialYUVThreaded(ENCODER *encoder, uint8_t *input, int input_pitch, FRAME_INFO *frame,
										TRANSFORM *transform[], int frame_index, int num_channels,
										PIXEL *buffer, size_t buffer_size, int chroma_offset)
{
	//static THREAD_FRAME_DATA data[CODEC_GOP_LENGTH];
	THREAD_FRAME_DATA *data = encoder->thread_frame_data;
	DWORD dwThreadID;
	HANDLE thread;

	DWORD dwCreationFlags = 0;	//CREATE_SUSPENDED

	// Use a portion of the buffer for each frame transform
	buffer_size /= CODEC_GOP_LENGTH;
	buffer += frame_index * buffer_size;

	data[frame_index].input = input;
	data[frame_index].input_pitch = input_pitch;
	data[frame_index].frame = *frame;
	data[frame_index].transform = transform;
	data[frame_index].frame_index = frame_index;
	data[frame_index].num_channels = num_channels;
	data[frame_index].buffer = buffer;
	data[frame_index].buffer_size = buffer_size;
	data[frame_index].chroma_offset = chroma_offset;

	thread = CreateThread(NULL, 0, TransformForwardSpatialYUVThread, &data[frame_index], dwCreationFlags, &dwThreadID);

#if DEBUG
	Sleep(1);
#endif

	//SetThreadAffinityMask(thread, 0xf);
	//SetThreadIdealProcessor(thread,2/*MAXIMUM_PROCESSORS*/);
	//ResumeThread(thread);

	// Remember the thread that was created to process this frame
	encoder->frame_thread[frame_index] = thread;

	//return thread;
}


typedef struct thread_planar_data
{
	uint8_t *plane[CODEC_MAX_CHANNELS];
	int pitch[CODEC_MAX_CHANNELS];
	int width[CODEC_MAX_CHANNELS];
	int height;

	ENCODER *encoder;
	FRAME_INFO frame;
	TRANSFORM **transform;
	int frame_index;
	int num_channels;
	PIXEL *buffer;
	size_t buffer_size;
	int chroma_offset;

} THREAD_PLANAR_DATA;


#if 0

// Apply the spatial transform to all planes in a single thread
DWORD WINAPI TransformForwardSpatialYUVPlanarThread(LPVOID param)
{
	THREAD_PLANAR_DATA *data = (THREAD_PLANAR_DATA *)param;

	ENCODER *encoder = data->encoder;
	TRANSFORM **transform = data->transform;
	int num_channels = data->num_channels;
	int frame_index = data->frame_index;
	int frame_width = data->frame.width;
	int frame_height = data->frame.height;
	PIXEL *buffer = data->buffer;
	size_t buffer_size = data->buffer_size;
	int channel;

	// Apply the forward spatial transform to each image plane
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		PIXEL *lowlow_band = wavelet->band[0];
		PIXEL *lowhigh_band = wavelet->band[1];
		PIXEL *highlow_band = wavelet->band[2];
		PIXEL *highhigh_band = wavelet->band[3];

		uint8_t *plane = data->plane[channel];
		int pitch = data->pitch[channel];

		// Compute the input dimensions from the output dimensions
		ROI roi = {2 * width, 2 * height};

		int quantization[IMAGE_NUM_BANDS];
		int k;

		for (k = 0; k < IMAGE_NUM_BANDS; k++) {
			quantization[k] = wavelet->quant[k];
		}

		// Check the input dimensions
		assert((channel == 0 && roi.width == frame_width) ||
				(channel > 0 && roi.width == frame_width/2));
		assert(roi.height == frame_height);

		// Apply the spatial transform to the pixels for this channel
		FilterSpatialQuant8u(plane, pitch,
							 lowlow_band, wavelet->pitch,
							 lowhigh_band, wavelet->pitch,
							 highlow_band, wavelet->pitch,
							 highhigh_band, wavelet->pitch,
							 buffer, buffer_size, roi,
							 quantization);

		// Set the output pixel type
		wavelet->pixel_type[0] = PIXEL_TYPE_16S;
		wavelet->pixel_type[1] = PIXEL_TYPE_16S;
		wavelet->pixel_type[2] = PIXEL_TYPE_16S;
		wavelet->pixel_type[3] = PIXEL_TYPE_16S;

		// Record any quantization that was applied after filtering
		//if (quantization != NULL)
		{
			int k;
			for (k = 0; k < IMAGE_NUM_BANDS; k++)
				wavelet->quantization[k] = quantization[k];
		}
#if 0
		else {
			int k;
			for (k = 0; k < IMAGE_NUM_BANDS; k++)
				wavelet->quantization[k] = 1;
		}
#endif
	}

	//STOP(tk_spatial);

}

#else

DWORD WINAPI TransformForwardSpatialPlanarThread(LPVOID param)
{
	THREAD_SPATIAL_DATA *data = (THREAD_SPATIAL_DATA *)param;

	int channel = data->channel;

	int width = data->width;
	int height = data->height;
	PIXEL *lowlow_band = data->band[0];
	PIXEL *lowhigh_band = data->band[1];
	PIXEL *highlow_band = data->band[2];
	PIXEL *highhigh_band = data->band[3];

	int lowlow_pitch = data->pitch[0];
	int lowhigh_pitch = data->pitch[1];
	int highlow_pitch = data->pitch[2];
	int highhigh_pitch = data->pitch[3];

	size_t buffer_size;
	uint8_t *buffer;

	int *quantization = data->quantization;

	uint8_t *plane = data->input;
	int pitch = data->input_pitch;

	// Compute the input dimensions from the output dimensions
	ROI roi = {width, height};

	// Compute the width of each row of horizontal filter output
	int output_width = width/2;

	// Compute the size of each row of horizontal filter output in bytes
	size_t output_buffer_size = output_width * sizeof(PIXEL);

	// Compute the size of the buffer for prescaling the input rows to avoid overflow
	size_t prescaling_buffer_size = width * sizeof(PIXEL);

	// Round up the buffer size to an integer number of cache lines
	output_buffer_size = ALIGN(output_buffer_size, _CACHE_LINE_SIZE);

	// Round tp the buffer size to an integer number of cache lines
	prescaling_buffer_size = ALIGN(prescaling_buffer_size, _CACHE_LINE_SIZE);

#if _QUANTIZE_SPATIAL_LOWPASS
	// The buffer must be large enough for sixteen rows plus the prescaling buffer
	buffer_size = (16 * output_buffer_size) + prescaling_buffer_size;
#else
	// The buffer must be large enough for fifteen rows plus the prescaling buffer
	buffer_size = (15 * output_buffer_size) + prescaling_buffer_size;
#endif

	buffer = MEMORY_ALIGNED_ALLOC(buffer_size, _CACHE_LINE_SIZE);

	// Apply the spatial transform to the pixels for this channel
	FilterSpatialQuant8u(plane, pitch,
						 lowlow_band, lowlow_pitch,
						 lowhigh_band, lowhigh_pitch,
						 highlow_band, highlow_pitch,
						 highhigh_band, highhigh_pitch,
						 (PIXEL *)buffer, buffer_size,
						 roi, quantization);

	MEMORY_ALIGNED_FREE(buffer);

	return 0;
}

#if 0
// Apply the spatial transform to all planes in multiple threads
DWORD WINAPI TransformForwardSpatialYUVPlanarThread(LPVOID param)
{
	THREAD_PLANAR_DATA *data = (THREAD_PLANAR_DATA *)param;

	ENCODER *encoder = data->encoder;
	TRANSFORM **transform = data->transform;
	int num_channels = data->num_channels;
	int frame_index = data->frame_index;
	int frame_width = data->frame.width;
	int frame_height = data->frame.height;
	PIXEL *buffer = data->buffer;
	size_t buffer_size = data->buffer_size;
	int channel;

	static THREAD_SPATIAL_DATA thread_data[CODEC_GOP_LENGTH][CODEC_MAX_CHANNELS];
	//HANDLE thread[CODEC_MAX_CHANNELS];
	HANDLE thread;
	const DWORD dwCreationFlags = 0;
	const DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;
	DWORD dwThreadID;

	// Apply the forward spatial transform to each image plane
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		PIXEL *lowlow_band = wavelet->band[0];
		PIXEL *lowhigh_band = wavelet->band[1];
		PIXEL *highlow_band = wavelet->band[2];
		PIXEL *highhigh_band = wavelet->band[3];

		uint8_t *plane = data->plane[channel];
		int pitch = data->pitch[channel];

		// Compute the input dimensions from the output dimensions
		ROI roi = {2 * width, 2 * height};

		//int quantization[IMAGE_NUM_BANDS];
		int k;

		// Check the input dimensions
		assert((channel == 0 && roi.width == frame_width) ||
				(channel > 0 && roi.width == frame_width/2));
		assert(roi.height == frame_height);

		thread_data[frame_index][channel].channel = channel;

		thread_data[frame_index][channel].width = roi.width;
		thread_data[frame_index][channel].height = roi.height;

		thread_data[frame_index][channel].input = plane;
		thread_data[frame_index][channel].input_pitch = pitch;

		for (k = 0; k < IMAGE_NUM_BANDS; k++)
		{
			thread_data[frame_index][channel].pitch[k] = wavelet->pitch;
			thread_data[frame_index][channel].band[k] = wavelet->band[k];
			thread_data[frame_index][channel].quantization[k] = wavelet->quant[k];
		}
#if 0
		thread[channel] = CreateThread(NULL, 0, TransformForwardSpatialPlanarThread,
									   &thread_data[frame_index][channel], dwCreationFlags, &dwThreadID);
#else
		thread = CreateThread(NULL, 0, TransformForwardSpatialPlanarThread,
							  &thread_data[frame_index][channel], dwCreationFlags, &dwThreadID);

		encoder->frame_channel_thread[frame_index][channel] = thread;
#endif
	}

	// Update parameters in the wavelet data structures
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Set the output pixel type
		wavelet->pixel_type[0] = PIXEL_TYPE_16S;
		wavelet->pixel_type[1] = PIXEL_TYPE_16S;
		wavelet->pixel_type[2] = PIXEL_TYPE_16S;
		wavelet->pixel_type[3] = PIXEL_TYPE_16S;

		// Record any quantization that was applied after filtering
		//if (quantization != NULL)
		{
			int k;
			for (k = 0; k < IMAGE_NUM_BANDS; k++)
				wavelet->quantization[k] = wavelet->quant[k];
		}
#if 0
		else {
			int k;
			for (k = 0; k < IMAGE_NUM_BANDS; k++)
				wavelet->quantization[k] = 1;
		}
#endif
	}

#if 0
	// Wait for all of the channel threads to finish
	WaitForMultipleObjects(num_channels, thread, true, dwTimeout);
#endif

	//STOP(tk_spatial);
}
#endif

#endif


#if 0

// Convert YUV packed to planar and perform the forward spatial transform
void TransformForwardSpatialYUVPlanarThreaded(ENCODER *encoder, uint8_t *input, int input_pitch, FRAME_INFO *frame,
											  TRANSFORM *transform[], int frame_index, int num_channels,
											  PIXEL *buffer, size_t buffer_size, int chroma_offset)
{
	FILE *logfile = encoder->logfile;
	int frame_width = frame->width;
	int frame_height = frame->height;
	uint8_t *unpacking_buffer;
	uint8_t *plane[CODEC_MAX_CHANNELS];
	int pitch[CODEC_MAX_CHANNELS];
	int width[CODEC_MAX_CHANNELS];
	size_t size;
	int channel;

	static THREAD_PLANAR_DATA data[CODEC_GOP_LENGTH];
	DWORD dwThreadID;
	HANDLE thread;

	DWORD dwCreationFlags = 0;	//CREATE_SUSPENDED

	// Use a portion of the buffer for each frame transform
	//buffer_size /= CODEC_GOP_LENGTH;
	//buffer += frame_index * buffer_size;

	// Compute the size of buffer required for the forward wavelet transform
	size = (frame_width / 2) * sizeof(PIXEL);		// Output image is half as wide
	size = ALIGN(size, _CACHE_LINE_SIZE);			// Align each output row
	size *= 15;										// Need fifteen rows

	// Allocate space for unpacking the image
	unpacking_buffer = (uint8_t *)buffer + size;
	unpacking_buffer = (uint8_t *)ALIGN(unpacking_buffer, _CACHE_LINE_SIZE);

	// Scratch space for luma
	width[0] = frame_width;
	plane[0] = unpacking_buffer;
	pitch[0] = ALIGN16(width[0]);
	size += frame_height * pitch[0];

	// Scratch space for u chroma
	width[1] = frame_width/2;
	plane[1] = plane[0] + frame_height * pitch[0];
	pitch[1] = ALIGN16(width[1]);
	size += frame_height * pitch[1];

	// Scratch space for v chroma
	width[2] = frame_width/2;
	plane[2] = plane[1] + frame_height * pitch[1];
	pitch[2] = ALIGN16(width[2]);
	size += frame_height * pitch[2];

	// The image processing buffer should have already been allocated
	assert(buffer != NULL);
	assert(buffer_size >= size);

#if (0 && DEBUG)
	if (band == 0) DumpPGM("LLenc",image,NULL);
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Input: 0x%p (%d), planes: 0x%p (%d) 0x%p (%d) 0x%p (%d), width: %d, height: %d\n",
			input, input, plane[0], plane[0], plane[1], plane[1], plane[2], plane[2], frame_width, frame_height);
	}
#endif

	// Unpack the YUV image into separate planes
	ConvertYUVPackedToPlanar8u(input, input_pitch, plane, pitch, frame_width, frame_height);

	// Initialize the data structure for the processing thread
	data[frame_index].plane[0] = plane[0];
	data[frame_index].plane[1] = plane[1];
	data[frame_index].plane[2] = plane[2];

	data[frame_index].pitch[0] = pitch[0];
	data[frame_index].pitch[1] = pitch[1];
	data[frame_index].pitch[2] = pitch[2];

	data[frame_index].width[0] = width[0];
	data[frame_index].width[1] = width[1];
	data[frame_index].width[2] = width[2];

	data[frame_index].height = frame_height;

	data[frame_index].encoder = encoder;
	data[frame_index].frame = *frame;
	data[frame_index].transform = transform;
	data[frame_index].frame_index = frame_index;
	data[frame_index].num_channels = num_channels;
	data[frame_index].buffer = buffer;
	data[frame_index].buffer_size = buffer_size;
	data[frame_index].chroma_offset = chroma_offset;

	thread = CreateThread(NULL, 0, TransformForwardSpatialYUVPlanarThread,
						  &data[frame_index], dwCreationFlags, &dwThreadID);

#if DEBUG
	Sleep(1);
#endif

#if 1
	// Wait for the transform thread to launch the channel threads
	WaitForSingleObject(thread, INFINITE);
	encoder->frame_thread[frame_index] = INVALID_HANDLE_VALUE;
#else
	// Remember the thread that was created to process this frame
	encoder->frame_thread[frame_index] = thread;
#endif
}

#else

// Convert YUV packed to planar and perform the forward spatial transform
void TransformForwardSpatialYUVPlanarThreaded(ENCODER *encoder, uint8_t *input, int input_pitch, FRAME_INFO *frame,
											  TRANSFORM *transform[], int frame_index, int num_channels,
											  PIXEL *buffer, size_t buffer_size, int chroma_offset)
{
	FILE *logfile = encoder->logfile;
	int frame_width = frame->width;
	int frame_height = frame->height;
	uint8_t *unpacking_buffer;
	uint8_t *plane[CODEC_MAX_CHANNELS];
	int pitch[CODEC_MAX_CHANNELS];
	int width[CODEC_MAX_CHANNELS];
	size_t size;
	int channel;

	//static THREAD_SPATIAL_DATA data[CODEC_GOP_LENGTH][CODEC_MAX_CHANNELS];
	THREAD_SPATIAL_DATA (* data)[CODEC_MAX_CHANNELS] = encoder->thread_spatial_data;
	const DWORD dwCreationFlags = 0;
	const DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;
	DWORD dwThreadID;
	HANDLE thread;

	// Use a portion of the buffer for each frame transform
	//buffer_size /= CODEC_GOP_LENGTH;
	//buffer += frame_index * buffer_size;

	// Compute the size of buffer required for the forward wavelet transform
	size = (frame_width / 2) * sizeof(PIXEL);		// Output image is half as wide
	size = ALIGN(size, _CACHE_LINE_SIZE);			// Align each output row
	size *= 15;										// Need fifteen rows

	// Allocate space for unpacking the image
	unpacking_buffer = (uint8_t *)buffer + size;
	unpacking_buffer = (uint8_t *)ALIGN(unpacking_buffer, _CACHE_LINE_SIZE);

	// Scratch space for luma
	width[0] = frame_width;
	plane[0] = unpacking_buffer;
	pitch[0] = ALIGN16(width[0]);
	size += frame_height * pitch[0];

	// Scratch space for u chroma
	width[1] = frame_width/2;
	plane[1] = plane[0] + frame_height * pitch[0];
	pitch[1] = ALIGN16(width[1]);
	size += frame_height * pitch[1];

	// Scratch space for v chroma
	width[2] = frame_width/2;
	plane[2] = plane[1] + frame_height * pitch[1];
	pitch[2] = ALIGN16(width[2]);
	size += frame_height * pitch[2];

	// The image processing buffer should have already been allocated
	assert(buffer != NULL);
	assert(buffer_size >= size);

#if (0 && DEBUG)
	if (band == 0) DumpPGM("LLenc", image, NULL);
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Input: 0x%p (%d), planes: 0x%p (%d) 0x%p (%d) 0x%p (%d), width: %d, height: %d\n",
			input, input, plane[0], plane[0], plane[1], plane[1], plane[2], plane[2], frame_width, frame_height);
	}
#endif

	// Unpack the YUV image into separate planes
	ConvertYUVPackedToPlanar8u(input, input_pitch, plane, pitch, frame_width, frame_height);

	// Apply the forward spatial transform to each image plane
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		PIXEL *lowlow_band = wavelet->band[0];
		PIXEL *lowhigh_band = wavelet->band[1];
		PIXEL *highlow_band = wavelet->band[2];
		PIXEL *highhigh_band = wavelet->band[3];

		// Compute the input dimensions from the output dimensions
		ROI roi = {2 * width, 2 * height};

		int affinity;
		int result;

		//int quantization[IMAGE_NUM_BANDS];
		int k;

		// Check the input dimensions
		assert((channel == 0 && roi.width == frame_width) ||
				(channel > 0 && roi.width == frame_width/2));
		assert(roi.height == frame_height);

		data[frame_index][channel].channel = channel;

		data[frame_index][channel].width = roi.width;
		data[frame_index][channel].height = roi.height;

		data[frame_index][channel].input = plane[channel];
		data[frame_index][channel].input_pitch = pitch[channel];

		for (k = 0; k < IMAGE_NUM_BANDS; k++)
		{
			data[frame_index][channel].pitch[k] = wavelet->pitch;
			data[frame_index][channel].band[k] = wavelet->band[k];
			data[frame_index][channel].quantization[k] = wavelet->quant[k];
		}
#if 0
		thread[channel] = CreateThread(NULL, 0, TransformForwardSpatialPlanarThread,
									   &data[frame_index][channel], dwCreationFlags, &dwThreadID);
#else
		thread = CreateThread(NULL, 0, TransformForwardSpatialPlanarThread,
							  &data[frame_index][channel], dwCreationFlags, &dwThreadID);

		// Set the processor on which this thread should run
		affinity = GetEncoderAffinityMask(encoder, channel);
		result = SetThreadAffinityMask(thread, affinity);
		assert(result != 0);

		encoder->frame_channel_thread[frame_index][channel] = thread;
#endif
	}

	// Update parameters in the wavelet data structures
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Set the output pixel type
		wavelet->pixel_type[0] = PIXEL_TYPE_16S;
		wavelet->pixel_type[1] = PIXEL_TYPE_16S;
		wavelet->pixel_type[2] = PIXEL_TYPE_16S;
		wavelet->pixel_type[3] = PIXEL_TYPE_16S;

		// Record any quantization that was applied after filtering
		//if (quantization != NULL)
		{
			int k;
			for (k = 0; k < IMAGE_NUM_BANDS; k++)
				wavelet->quantization[k] = wavelet->quant[k];
		}
#if 0
		else {
			int k;
			for (k = 0; k < IMAGE_NUM_BANDS; k++)
				wavelet->quantization[k] = 1;
		}
#endif
	}

#if 0
	// Wait for all of the channel threads to finish
	WaitForMultipleObjects(num_channels, thread, true, dwTimeout);
#endif

}

#endif


#if 0

typedef struct thread_field_data
{
	int channel;
	int frame_format;
	uint8_t *even_row_ptr;
	uint8_t *odd_row_ptr;
	int frame_row_length;
	PIXEL *temporal_lowpass;
	PIXEL *temporal_highpass;
	int offset;
	PIXEL *horizontal_lowlow;
	PIXEL *horizontal_lowhigh;
	PIXEL *horizontal_highlow;
	PIXEL *horizontal_highhigh;
	int horizontal_width;
	PIXEL *lowhigh_row_buffer;
	PIXEL *highlow_row_buffer;
	PIXEL *highhigh_row_buffer;
	int temporal_width;
	int lowlow_scale;
	int lowhigh_scale;
	int highlow_scale;
	int highhigh_scale;
	int lowhigh_quantization;
	int highlow_quantization;
	int highhigh_quantization;

} THREAD_FIELD_DATA;


DWORD WINAPI FilterFrameYUVChannelThread(LPVOID param)
{
	THREAD_FIELD_DATA *data = (THREAD_FIELD_DATA *)param;

	int channel = data->channel;
	int frame_format = data->frame_format;
	uint8_t *even_row_ptr = data->even_row_ptr;
	uint8_t *odd_row_ptr = data->odd_row_ptr;
	int frame_row_length = data->frame_row_length;
	PIXEL *temporal_lowpass = data->temporal_lowpass;
	PIXEL *temporal_highpass = data->temporal_highpass;
	int offset = data->offset;
	PIXEL *horizontal_lowlow = data->horizontal_lowlow;
	PIXEL *horizontal_lowhigh = data->horizontal_lowhigh;
	PIXEL *horizontal_highlow = data->horizontal_highlow;
	PIXEL *horizontal_highhigh = data->horizontal_highhigh;
	int horizontal_width = data->horizontal_width;
	PIXEL *lowhigh_row_buffer = data->lowhigh_row_buffer;
	PIXEL *highlow_row_buffer = data->highlow_row_buffer;
	PIXEL *highhigh_row_buffer = data->highhigh_row_buffer;
	int temporal_width = data->temporal_width;
	int lowlow_scale = data->lowlow_scale;
	int lowhigh_scale = data->lowhigh_scale;
	int highlow_scale = data->highlow_scale;
	int highhigh_scale = data->highhigh_scale;
	int lowhigh_quantization = data->lowhigh_quantization;
	int highlow_quantization = data->highlow_quantization;
	int highhigh_quantization = data->highhigh_quantization;

	assert(ISALIGNED16(even_row_ptr));
	assert(ISALIGNED16(odd_row_ptr));

	// What is the color format?
	if (frame_format == COLOR_FORMAT_YUYV)
	{
		// Apply the temporal transform to one channel in the even and odd rows
		FilterTemporalRowYUYVChannelTo16s(even_row_ptr, odd_row_ptr, frame_row_length, channel,
										  temporal_lowpass, temporal_highpass, offset, 8, 0);
	}
	else
	{
		// Frame color format must be UYUV
		assert(frame_format == COLOR_FORMAT_UYVY);

		// Apply the temporal transform to one channel in the even and odd rows
		FilterTemporalRowUYVYChannelTo16s(even_row_ptr, odd_row_ptr, frame_row_length, channel,
										  temporal_lowpass, temporal_highpass, offset, 8, 0);
	}

	// Apply the horizontal transform to the temporal lowpass
	FilterHorizontalRowScaled16s(temporal_lowpass, horizontal_lowlow, lowhigh_row_buffer,
								 temporal_width, lowlow_scale, lowhigh_scale);

	// Apply the horizontal transform to the temporal highpass
	FilterHorizontalRowScaled16s(temporal_highpass, highlow_row_buffer, highhigh_row_buffer,
								 temporal_width, highlow_scale, highhigh_scale);

	// Quantize and pack the rows of highpass coefficients
	QuantizeRow16sTo16s(lowhigh_row_buffer, horizontal_lowhigh, horizontal_width, lowhigh_quantization);
	QuantizeRow16sTo16s(highlow_row_buffer, horizontal_highlow, horizontal_width, highlow_quantization);
	QuantizeRow16sTo16s(highhigh_row_buffer, horizontal_highhigh, horizontal_width, highhigh_quantization);

	return 0;
}


// Apply the forward horizontal-temporal transform to a packed frame of YUV data
void TransformForwardFrameYUVThreaded(ENCODER *encoder, uint8_t *input, int input_pitch, FRAME_INFO *frame,
									  TRANSFORM *transform[], int frame_index, int num_channels,
									  char *buffer, size_t buffer_size, int chroma_offset)
{
	// Pointers to the even and odd rows of packed pixels
	uint8_t *even_row_ptr = input;
	uint8_t *odd_row_ptr = input + input_pitch;

	// For allocating buffer space
	char *bufptr = buffer;

	// Pointers to the rows in the temporal wavelet for each channel
	PIXEL *temporal_lowpass[TRANSFORM_MAX_CHANNELS];
	PIXEL *temporal_highpass[TRANSFORM_MAX_CHANNELS];

	// Pointers to the rows in the horizontal wavelet for each channel
	PIXEL *horizontal_lowlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_lowhigh[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highhigh[TRANSFORM_MAX_CHANNELS];

	// Buffer for the horizontal highpass coefficients
	PIXEL *lowhigh_row_buffer[TRANSFORM_MAX_CHANNELS];
	PIXEL *highlow_row_buffer[TRANSFORM_MAX_CHANNELS];
	PIXEL *highhigh_row_buffer[TRANSFORM_MAX_CHANNELS];

	// Length of each temporal row
	int temporal_width[TRANSFORM_MAX_CHANNELS];

	// Horizontal wavelet band width and pitch
	int horizontal_width[TRANSFORM_MAX_CHANNELS];
	int horizontal_pitch[TRANSFORM_MAX_CHANNELS];
	//int horizontal_pitch8s[TRANSFORM_MAX_CHANNELS];

	// Quantization factors
	int lowlow_quantization[TRANSFORM_MAX_CHANNELS];
	int lowhigh_quantization[TRANSFORM_MAX_CHANNELS];
	int highlow_quantization[TRANSFORM_MAX_CHANNELS];
	int highhigh_quantization[TRANSFORM_MAX_CHANNELS];

	// Scale factors for the frame transform
	int lowlow_scale = 0;
	int lowhigh_scale = 0;
	int highlow_scale = 0;
	int highhigh_scale = 0;

	// Dimensions of the frame
	int frame_width = frame->width;
	int frame_height = frame->height;
	int frame_format = frame->format;
	int half_height = frame_height / 2;
	int half_width = frame_width/2;
	int field_pitch = 2 * input_pitch;
	size_t temporal_row_size;
	size_t horizontal_row_size;
	size_t total_buffer_size;
	//int horizontal_row_length;
	int frame_row_length;
	int channel;
	int row;

	// Check that the frame format is supported
	assert(frame_format == COLOR_FORMAT_YUYV || frame_format == COLOR_FORMAT_UYVY);

	// Check that the number of channels is reasonable
	assert(0 < num_channels && num_channels <= TRANSFORM_MAX_CHANNELS);

	// Check that the buffer starts on a cache line boundary
	assert(ISALIGNED(buffer, _CACHE_LINE_SIZE));

	// Since the frame transform performs both temporal and horizontal filtering
	// the time spent in both transforms will be counted with a separate timer
	//START(tk_frame);

	// Round up the frame width to an integer number of cache lines
	frame_row_length = frame_width * 2;
	frame_row_length = ALIGN(frame_row_length, _CACHE_LINE_SIZE);
	frame_row_length /= 2;

	// Compute the size of the largest temporal output row
	temporal_row_size = frame_row_length * sizeof(PIXEL);

	// Round up the temporal row size to an integer number of cache lines
	temporal_row_size = ALIGN(temporal_row_size, _CACHE_LINE_SIZE);

	// Round up the horizontal row length to a multiple of 16 coefficients
	//horizontal_row_length = ALIGN16(half_width);

	// Compute the size of the largest horizontal output row
	horizontal_row_size = half_width * sizeof(PIXEL);

	// Round up the horizontal row size to an integer number of cache lines
	horizontal_row_size = ALIGN(horizontal_row_size, _CACHE_LINE_SIZE);

	// Check that the buffer is large enough
	total_buffer_size = 2 * temporal_row_size + 3 * horizontal_row_size;
	assert(buffer_size >= total_buffer_size);

	// Allocate buffers for a single row of lowpass and highpass temporal coefficients
	// and initialize the arrays of row pointers into the horizontal transform bands
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Initialize the row pointers into the horizontal bands
		horizontal_lowlow[channel] = wavelet->band[LL_BAND];
		horizontal_lowhigh[channel] = (PIXEL *)wavelet->band[LH_BAND];
		horizontal_highlow[channel] = (PIXEL *)wavelet->band[HL_BAND];
		horizontal_highhigh[channel] = (PIXEL *)wavelet->band[HH_BAND];

		lowlow_quantization[channel] = wavelet->quant[LL_BAND];
		lowhigh_quantization[channel] = wavelet->quant[LH_BAND];
		highlow_quantization[channel] = wavelet->quant[HL_BAND];
		highhigh_quantization[channel] = wavelet->quant[HH_BAND];

		// Compute the width of the temporal rows for this channel
		temporal_width[channel] = (channel == 0) ? frame_width : half_width;

		// Round the width up to an integer number of cache lines
		//temporal_width[channel] = ALIGN(temporal_width[channel], _CACHE_LINE_SIZE);

		// Compute the pitch in units of pixels
		//horizontal_pitch[channel] = wavelet->pitch/sizeof(PIXEL);

		// Keep the pitch in units of bytes
		horizontal_pitch[channel] = wavelet->pitch;

		// Remember the width of the horizontal wavelet rows for this channel
		horizontal_width[channel] = wavelet->width;

		// Divide the buffer into temporal lowpass and highpass rows
		temporal_lowpass[channel] = (PIXEL *)bufptr;		bufptr += temporal_row_size;
		temporal_highpass[channel] = (PIXEL *)bufptr;		bufptr += temporal_row_size;

		// Allocate buffer space for the horizontal highpass coefficients
		lowhigh_row_buffer[channel] = (PIXEL *)bufptr;		bufptr += horizontal_row_size;
		highlow_row_buffer[channel] = (PIXEL *)bufptr;		bufptr += horizontal_row_size;
		highhigh_row_buffer[channel] = (PIXEL *)bufptr;		bufptr += horizontal_row_size;

		// Note: Should the temporal and horizontal rows be the same size for all channels?
	}

	// Apply the temporal transform to the even and odd rows each iteration of the loop
	for (row = 0; row < frame_height; row += 2)
	{
		const DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;
		HANDLE thread[CODEC_MAX_CHANNELS];
		DWORD dwThreadID;

		// Apply the temporal and horizontal transforms to each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			int offset = (channel == 0) ? 0 : chroma_offset;
			int runs_count;
#if 1
			static THREAD_FIELD_DATA data[CODEC_MAX_CHANNELS];

			assert(ISALIGNED16(even_row_ptr));
			assert(ISALIGNED16(odd_row_ptr));

			data[channel].channel = channel;
			data[channel].frame_format = frame_format;
			data[channel].even_row_ptr = even_row_ptr;
			data[channel].odd_row_ptr = odd_row_ptr;
			data[channel].frame_row_length = frame_row_length;
			data[channel].temporal_lowpass = temporal_lowpass[channel];
			data[channel].temporal_highpass = temporal_highpass[channel];
			data[channel].offset = offset;
			data[channel].horizontal_lowlow = horizontal_lowlow[channel];
			data[channel].horizontal_lowhigh = horizontal_lowhigh[channel];
			data[channel].horizontal_highlow = horizontal_highlow[channel];
			data[channel].horizontal_highhigh = horizontal_highhigh[channel];
			data[channel].horizontal_width = horizontal_width[channel];
			data[channel].lowhigh_row_buffer = lowhigh_row_buffer[channel];
			data[channel].highlow_row_buffer = highlow_row_buffer[channel];
			data[channel].highhigh_row_buffer = highhigh_row_buffer[channel];
			data[channel].temporal_width = temporal_width[channel];
			data[channel].lowlow_scale = lowlow_scale;
			data[channel].lowhigh_scale = lowhigh_scale;
			data[channel].highlow_scale = highlow_scale;
			data[channel].highhigh_scale = highhigh_scale;
			data[channel].lowhigh_quantization = lowhigh_quantization[channel];
			data[channel].highlow_quantization = highlow_quantization[channel];
			data[channel].highhigh_quantization = highhigh_quantization[channel];

			thread[channel] = CreateThread(NULL, 0, FilterFrameYUVChannelThread, &data[channel], 0, &dwThreadID);
			//WaitForSingleObject(thread[channel], dwTimeout);
#else
			if (frame_format == COLOR_FORMAT_YUYV) {
				// Apply the temporal transform to one channel in the even and odd rows
				FilterTemporalRowYUYVChannelTo16s(even_row_ptr, odd_row_ptr, frame_row_length, channel,
												  temporal_lowpass[channel], temporal_highpass[channel],
												  offset,8,0);
			}
			else {
				// Frame color format must be UYUV
				assert(frame_format == COLOR_FORMAT_UYVY);

				// Apply the temporal transform to one channel in the even and odd rows
				FilterTemporalRowUYVYChannelTo16s(even_row_ptr, odd_row_ptr, frame_row_length, channel,
												  temporal_lowpass[channel], temporal_highpass[channel],
												  offset,8,0);
			}

			// Apply the horizontal transform to the temporal lowpass
			FilterHorizontalRowScaled16s(temporal_lowpass[channel], horizontal_lowlow[channel],
										 lowhigh_row_buffer[channel], temporal_width[channel],
										 lowlow_scale, lowhigh_scale);

			// Apply the horizontal transform to the temporal highpass
			FilterHorizontalRowScaled16s(temporal_highpass[channel], highlow_row_buffer[channel],
										 highhigh_row_buffer[channel], temporal_width[channel],
										 highlow_scale, highhigh_scale);

			// Quantize and pack the rows of highpass coefficients
			QuantizeRow16sTo16s(lowhigh_row_buffer[channel], horizontal_lowhigh[channel], horizontal_width[channel], lowhigh_quantization[channel]);
			QuantizeRow16sTo16s(highlow_row_buffer[channel], horizontal_highlow[channel], horizontal_width[channel], highlow_quantization[channel]);
			QuantizeRow16sTo16s(highhigh_row_buffer[channel], horizontal_highhigh[channel], horizontal_width[channel], highhigh_quantization[channel]);
#endif
			// Advance to the next row in the lowpass band
			horizontal_lowlow[channel] += horizontal_pitch[channel] / sizeof(PIXEL);

			// Advance to the next row in each highpass band
			horizontal_lowhigh[channel] += horizontal_pitch[channel] / sizeof(PIXEL);
			horizontal_highlow[channel] += horizontal_pitch[channel] / sizeof(PIXEL);
			horizontal_highhigh[channel] += horizontal_pitch[channel] / sizeof(PIXEL);
		}

		WaitForMultipleObjects(num_channels, thread, true, dwTimeout);

		// Advance to the next row in each input field
		even_row_ptr += field_pitch;
		odd_row_ptr += field_pitch;
	}

	// Record the pixel type in each band
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		wavelet->pixel_type[LL_BAND] = PIXEL_TYPE_16S;

#if _HIGHPASS_8S
		wavelet->pixel_type[LH_BAND] = PIXEL_TYPE_8S;
		wavelet->pixel_type[HL_BAND] = PIXEL_TYPE_8S;
		wavelet->pixel_type[HH_BAND] = PIXEL_TYPE_8S;
#else
		wavelet->pixel_type[LH_BAND] = PIXEL_TYPE_16S;
		wavelet->pixel_type[HL_BAND] = PIXEL_TYPE_16S;
		wavelet->pixel_type[HH_BAND] = PIXEL_TYPE_16S;
#endif

		wavelet->num_runs[LL_BAND] = 0;
		wavelet->num_runs[LH_BAND] = 0;
		wavelet->num_runs[HL_BAND] = 0;
		wavelet->num_runs[HH_BAND] = 0;
	}

	// Record the quantization that was applied to each wavelet band
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int band;

		for (band = 0; band < wavelet->num_bands; band++)
		{
			wavelet->quantization[band] = wavelet->quant[band];
		}
	}

	//STOP(tk_frame);
}

#else

DWORD WINAPI FilterFrameYUVChannelThread(LPVOID param)
{
	THREAD_FIELD_DATA *data = (THREAD_FIELD_DATA *)param;

	int channel = data->channel;
	int frame_height = data->frame_height;
	int frame_format = data->frame_format;
	uint8_t *even_row_ptr = data->even_row_ptr;
	uint8_t *odd_row_ptr = data->odd_row_ptr;
	int field_pitch = data->field_pitch;
	int frame_row_length = data->frame_row_length;
	PIXEL *temporal_lowpass = data->temporal_lowpass;
	PIXEL *temporal_highpass = data->temporal_highpass;
	int offset = data->offset;
	PIXEL *horizontal_lowlow = data->horizontal_lowlow;
	PIXEL *horizontal_lowhigh = data->horizontal_lowhigh;
	PIXEL *horizontal_highlow = data->horizontal_highlow;
	PIXEL *horizontal_highhigh = data->horizontal_highhigh;
	int horizontal_width = data->horizontal_width;
	int horizontal_pitch = data->horizontal_pitch;
	PIXEL *lowhigh_row_buffer = data->lowhigh_row_buffer;
	PIXEL *highlow_row_buffer = data->highlow_row_buffer;
	PIXEL *highhigh_row_buffer = data->highhigh_row_buffer;
	int temporal_width = data->temporal_width;
	int lowlow_scale = data->lowlow_scale;
	int lowhigh_scale = data->lowhigh_scale;
	int highlow_scale = data->highlow_scale;
	int highhigh_scale = data->highhigh_scale;
	int lowhigh_quantization = data->quantization[1];
	int highlow_quantization = data->quantization[2];
	int highhigh_quantization = data->quantization[3];
	int row;

	assert(ISALIGNED16(even_row_ptr));
	assert(ISALIGNED16(odd_row_ptr));

	// Apply the temporal transform to the even and odd rows each iteration of the loop
	for (row = 0; row < frame_height; row += 2)
	{
		if (frame_format == COLOR_FORMAT_YUYV)
		{
			// Apply the temporal transform to one channel in the even and odd rows
			FilterTemporalRowYUYVChannelTo16s(even_row_ptr, odd_row_ptr, frame_row_length, channel,
											  temporal_lowpass, temporal_highpass, offset,8,0);
		}
		else
		{
			// Frame color format must be UYUV
			assert(frame_format == COLOR_FORMAT_UYVY);

			// Apply the temporal transform to one channel in the even and odd rows
			FilterTemporalRowUYVYChannelTo16s(even_row_ptr, odd_row_ptr, frame_row_length, channel,
											  temporal_lowpass, temporal_highpass, offset, 8,0);
		}

		// Apply the horizontal transform to the temporal lowpass
		FilterHorizontalRowScaled16s(temporal_lowpass, horizontal_lowlow,
									 lowhigh_row_buffer, temporal_width,
									 lowlow_scale, lowhigh_scale);

		// Apply the horizontal transform to the temporal highpass
		FilterHorizontalRowScaled16s(temporal_highpass, highlow_row_buffer,
									 highhigh_row_buffer, temporal_width,
									 highlow_scale, highhigh_scale);

		// Quantize and pack the rows of highpass coefficients
		QuantizeRow16sTo16s(lowhigh_row_buffer, horizontal_lowhigh, horizontal_width, lowhigh_quantization);
		QuantizeRow16sTo16s(highlow_row_buffer, horizontal_highlow, horizontal_width, highlow_quantization);
		QuantizeRow16sTo16s(highhigh_row_buffer, horizontal_highhigh, horizontal_width, highhigh_quantization);

		// Advance to the next row in the lowpass band
		horizontal_lowlow += horizontal_pitch / sizeof(PIXEL);

		// Advance to the next row in each highpass band
		horizontal_lowhigh += horizontal_pitch / sizeof(PIXEL);
		horizontal_highlow += horizontal_pitch / sizeof(PIXEL);
		horizontal_highhigh += horizontal_pitch / sizeof(PIXEL);

		// Advance to the next row in each input field
		even_row_ptr += field_pitch;
		odd_row_ptr += field_pitch;
	}

	return 0;
}


// Apply the forward horizontal-temporal transform to a packed frame of YUV data
void TransformForwardFrameYUVThreaded(ENCODER *encoder, uint8_t *input, int input_pitch, FRAME_INFO *frame,
									  TRANSFORM *transform[], int frame_index, int num_channels,
									  char *buffer, size_t buffer_size, int chroma_offset)
{
	// Pointers to the even and odd rows of packed pixels
	uint8_t *even_row_ptr = input;
	uint8_t *odd_row_ptr = input + input_pitch;

	// For allocating buffer space
	char *bufptr = buffer;

	// Pointers to the rows in the temporal wavelet for each channel
	PIXEL *temporal_lowpass[TRANSFORM_MAX_CHANNELS];
	PIXEL *temporal_highpass[TRANSFORM_MAX_CHANNELS];

	// Pointers to the rows in the horizontal wavelet for each channel
	PIXEL *horizontal_lowlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_lowhigh[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highhigh[TRANSFORM_MAX_CHANNELS];

	// Buffer for the horizontal highpass coefficients
	PIXEL *lowhigh_row_buffer[TRANSFORM_MAX_CHANNELS];
	PIXEL *highlow_row_buffer[TRANSFORM_MAX_CHANNELS];
	PIXEL *highhigh_row_buffer[TRANSFORM_MAX_CHANNELS];

	// Length of each temporal row
	int temporal_width[TRANSFORM_MAX_CHANNELS];

	// Horizontal wavelet band width and pitch
	int horizontal_width[TRANSFORM_MAX_CHANNELS];
	int horizontal_pitch[TRANSFORM_MAX_CHANNELS];

	// Quantization factors
	int lowlow_quantization[TRANSFORM_MAX_CHANNELS];
	int lowhigh_quantization[TRANSFORM_MAX_CHANNELS];
	int highlow_quantization[TRANSFORM_MAX_CHANNELS];
	int highhigh_quantization[TRANSFORM_MAX_CHANNELS];

	// Scale factors for the frame transform
	int lowlow_scale = 0;
	int lowhigh_scale = 0;
	int highlow_scale = 0;
	int highhigh_scale = 0;

	// Dimensions of the frame
	int frame_width = frame->width;
	int frame_height = frame->height;
	int frame_format = frame->format;
	int half_height = frame_height / 2;
	int half_width = frame_width/2;
	int field_pitch = 2 * input_pitch;
	size_t temporal_row_size;
	size_t horizontal_row_size;
	size_t total_buffer_size;
	//int horizontal_row_length;
	int frame_row_length;
	int channel;
	int row;

	const DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;
	HANDLE thread[CODEC_MAX_CHANNELS];
	DWORD dwThreadID;

	// Check that the frame format is supported
	assert(frame_format == COLOR_FORMAT_YUYV || frame_format == COLOR_FORMAT_UYVY);

	// Check that the number of channels is reasonable
	assert(0 < num_channels && num_channels <= TRANSFORM_MAX_CHANNELS);

	// Check that the buffer starts on a cache line boundary
	assert(ISALIGNED(buffer, _CACHE_LINE_SIZE));

	// Since the frame transform performs both temporal and horizontal filtering
	// the time spent in both transforms will be counted with a separate timer
	//START(tk_frame);

	// Round up the frame width to an integer number of cache lines
	frame_row_length = frame_width * 2;
	frame_row_length = ALIGN(frame_row_length, _CACHE_LINE_SIZE);
	frame_row_length /= 2;

	// Compute the size of the largest temporal output row
	temporal_row_size = frame_row_length * sizeof(PIXEL);

	// Round up the temporal row size to an integer number of cache lines
	temporal_row_size = ALIGN(temporal_row_size, _CACHE_LINE_SIZE);

	// Round up the horizontal row length to a multiple of 16 coefficients
	//horizontal_row_length = ALIGN16(half_width);

	// Compute the size of the largest horizontal output row
	horizontal_row_size = half_width * sizeof(PIXEL);

	// Round up the horizontal row size to an integer number of cache lines
	horizontal_row_size = ALIGN(horizontal_row_size, _CACHE_LINE_SIZE);

	// Check that the buffer is large enough
	total_buffer_size = 2 * temporal_row_size + 3 * horizontal_row_size;
	assert(buffer_size >= total_buffer_size);

	// Allocate buffers for a single row of lowpass and highpass temporal coefficients
	// and initialize the arrays of row pointers into the horizontal transform bands
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Initialize the row pointers into the horizontal bands
		horizontal_lowlow[channel] = wavelet->band[LL_BAND];
		horizontal_lowhigh[channel] = (PIXEL *)wavelet->band[LH_BAND];
		horizontal_highlow[channel] = (PIXEL *)wavelet->band[HL_BAND];
		horizontal_highhigh[channel] = (PIXEL *)wavelet->band[HH_BAND];

		lowlow_quantization[channel] = wavelet->quant[LL_BAND];
		lowhigh_quantization[channel] = wavelet->quant[LH_BAND];
		highlow_quantization[channel] = wavelet->quant[HL_BAND];
		highhigh_quantization[channel] = wavelet->quant[HH_BAND];

		// Compute the width of the temporal rows for this channel
		temporal_width[channel] = (channel == 0) ? frame_width : half_width;

		// Round the width up to an integer number of cache lines
		//temporal_width[channel] = ALIGN(temporal_width[channel], _CACHE_LINE_SIZE);

		// Compute the pitch in units of pixels
		//horizontal_pitch[channel] = wavelet->pitch/sizeof(PIXEL);

		// Keep the pitch in units of bytes
		horizontal_pitch[channel] = wavelet->pitch;

		// Remember the width of the horizontal wavelet rows for this channel
		horizontal_width[channel] = wavelet->width;

		// Divide the buffer into temporal lowpass and highpass rows
		temporal_lowpass[channel] = (PIXEL *)bufptr;		bufptr += temporal_row_size;
		temporal_highpass[channel] = (PIXEL *)bufptr;		bufptr += temporal_row_size;

		// Allocate buffer space for the horizontal highpass coefficients
		lowhigh_row_buffer[channel] = (PIXEL *)bufptr;		bufptr += horizontal_row_size;
		highlow_row_buffer[channel] = (PIXEL *)bufptr;		bufptr += horizontal_row_size;
		highhigh_row_buffer[channel] = (PIXEL *)bufptr;		bufptr += horizontal_row_size;

		// Note: Should the temporal and horizontal rows be the same size for all channels?
	}

	// Process the frame transform in each channel using multiple threads
	for (channel = 0; channel < num_channels; channel++)
	{
		int offset = (channel == 0) ? 0 : chroma_offset;
		int runs_count;

		THREAD_FIELD_DATA *data = encoder->thread_field_data;

		assert(ISALIGNED16(even_row_ptr));
		assert(ISALIGNED16(odd_row_ptr));

		data[channel].channel = channel;
		data[channel].frame_height = frame_height;
		data[channel].frame_format = frame_format;
		data[channel].even_row_ptr = even_row_ptr;
		data[channel].odd_row_ptr = odd_row_ptr;
		data[channel].field_pitch = field_pitch;
		data[channel].frame_row_length = frame_row_length;
		data[channel].temporal_lowpass = temporal_lowpass[channel];
		data[channel].temporal_highpass = temporal_highpass[channel];
		data[channel].offset = offset;
		data[channel].horizontal_lowlow = horizontal_lowlow[channel];
		data[channel].horizontal_lowhigh = horizontal_lowhigh[channel];
		data[channel].horizontal_highlow = horizontal_highlow[channel];
		data[channel].horizontal_highhigh = horizontal_highhigh[channel];
		data[channel].horizontal_width = horizontal_width[channel];
		data[channel].horizontal_pitch = horizontal_pitch[channel];
		data[channel].lowhigh_row_buffer = lowhigh_row_buffer[channel];
		data[channel].highlow_row_buffer = highlow_row_buffer[channel];
		data[channel].highhigh_row_buffer = highhigh_row_buffer[channel];
		data[channel].temporal_width = temporal_width[channel];
		data[channel].lowlow_scale = lowlow_scale;
		data[channel].lowhigh_scale = lowhigh_scale;
		data[channel].highlow_scale = highlow_scale;
		data[channel].highhigh_scale = highhigh_scale;
		data[channel].quantization[1] = lowhigh_quantization[channel];
		data[channel].quantization[2] = highlow_quantization[channel];
		data[channel].quantization[3] = highhigh_quantization[channel];

		thread[channel] = CreateThread(NULL, 0, FilterFrameYUVChannelThread, &data[channel], 0, &dwThreadID);
		//WaitForSingleObject(thread[channel], dwTimeout);
	}

	// Wait for all of the channels to finish
	WaitForMultipleObjects(num_channels, thread, true, dwTimeout);

	// Record the pixel type in each band
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		wavelet->pixel_type[LL_BAND] = PIXEL_TYPE_16S;

#if _HIGHPASS_8S
		wavelet->pixel_type[LH_BAND] = PIXEL_TYPE_8S;
		wavelet->pixel_type[HL_BAND] = PIXEL_TYPE_8S;
		wavelet->pixel_type[HH_BAND] = PIXEL_TYPE_8S;
#else
		wavelet->pixel_type[LH_BAND] = PIXEL_TYPE_16S;
		wavelet->pixel_type[HL_BAND] = PIXEL_TYPE_16S;
		wavelet->pixel_type[HH_BAND] = PIXEL_TYPE_16S;
#endif

		wavelet->num_runs[LL_BAND] = 0;
		wavelet->num_runs[LH_BAND] = 0;
		wavelet->num_runs[HL_BAND] = 0;
		wavelet->num_runs[HH_BAND] = 0;
	}

	// Record the quantization that was applied to each wavelet band
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int band;

		for (band = 0; band < wavelet->num_bands; band++)
		{
			wavelet->quantization[band] = wavelet->quant[band];
		}
	}

	//STOP(tk_frame);
}

#endif

#endif
