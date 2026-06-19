/*! @file SampleDecoder.h

*  @brief Internal routines used for processing sample metadata.
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

#pragma once
#ifndef _SAMPLE_DEC_H
#define _SAMPLE_DEC_H

// This sample decoder implements the sample decoder interface
#include "ISampleDecoder.h"
#include "CFHDSampleHeader.h"

#include "decoder.h"

// Avoid exposing the data structures defined in the codec library
typedef struct decoder DECODER;
typedef enum decoded_format DECODED_FORMAT;
typedef enum decoded_resolution DECODED_RESOLUTION;
typedef enum encoded_format ENCODED_FORMAT;


class CSampleDecoder : public ISampleDecoder
{
public:

	// Class factory method for allocating sample decoders
	static ISampleDecoder *CreateSampleDecoder(IAllocator *allocator,
															   CFHD_LicenseKey license,
															   FILE *logfile = NULL);

	// Initialize the block of data as a sample decoder instance
	CFHDDECODER_API static CFHD_Error InitializeSampleDecoder(void *data, size_t size);

	// Default constructor
	CSampleDecoder(CFHD_ALLOCATOR *allocator = NULL,
				   CFHD_LicenseKey license = NULL,
				   FILE *logfile = NULL);

	// Virtual destructor for the sample decoder interface
	virtual ~CSampleDecoder();

	CFHD_Error GetOutputFormats(void *samplePtr,
								size_t sampleSize,
								CFHD_PixelFormat *outputFormatArray,
								int outputFormatArrayLength,
								int *actualOutputFormatCountOut);

	
	CFHD_Error GetSampleInfo(void *samplePtr,
							 size_t sampleSize,
							 CFHD_SampleInfoTag tag,
							 void *value,
							 size_t buffer_size);


	CFHD_Error PrepareDecoder(int outputWidth,
							  int outputHeight,
							  CFHD_PixelFormat outputFormat,
							  int decodedResolution,
							  CFHD_DecodingFlags decodingFlags,
							  void *samplePtr,
							  size_t sampleSize,
							  int *actualWidthOut,
							  int *actualHeightOut,
							  CFHD_PixelFormat *actualFormatOut);

	CFHD_Error SetLicense(const unsigned char *license);

	CFHD_Error GetThumbnail(void *samplePtr,
							  size_t sampleSize,
							  void *outputBuffer,
							  size_t outputSize, 
							  size_t flags,
							  size_t *retWidth,
							  size_t *retHeight,
							  size_t *retSize);

	CFHD_Error SetAllocator(CFHD_ALLOCATOR * allocator)
	{
		m_allocator = allocator;
		return CFHD_ERROR_OKAY;
	}

	CFHD_ALLOCATOR *GetAllocator()
	{
		return m_allocator;
	}

	CFHD_Error ParseSampleHeader(void *samplePtr,
								 size_t sampleSize,
								 CFHD_SampleHeader *sampleHeaderOut);

	CFHD_Error DecodeSample(void *samplePtr,
							size_t sampleSize,
							void *outputBuffer,
							int outputPitch);

	CFHD_Error SetDecoderOverrides(unsigned char *databaseData, int databaseSize);

	CFHD_Error GetFrameFormat(int &width, int &height, CFHD_PixelFormat &format);

	CFHD_Error GetRequiredBufferSize(uint32_t &bytes);

	
	CFHD_Error SetChannelsActive(uint32_t data)
	{
		m_channelsActive = data;
		return CFHD_ERROR_OKAY;
	}
	CFHD_Error SetChannelMix(uint32_t data)
	{
		m_channelMix = data;
		return CFHD_ERROR_OKAY;
	}

	CFHD_Error GetChannelsActive(uint32_t &data)
	{
		data = m_channelsActive;
		return CFHD_ERROR_OKAY;
	}
	CFHD_Error GetChannelMix(uint32_t &data)
	{
		data = m_channelMix;
		return CFHD_ERROR_OKAY;
	}

	CFHD_Error ReleaseDecoder();

	bool IsDecoderObsolete(int outputWidth,
						   int outputHeight,
						   CFHD_PixelFormat outputFormat,
						   int decodedResolution);

protected:
#if 0
	CFHD_Error PrepareToDecodeBayer(int outputWidth,
									int outputHeight,
									CFHD_PixelFormat outputFormat,
									int decodedResolution,
									CFHD_DecodingFlags decodingFlags,
									void *samplePtr,
									size_t sampleSize,
									int *actualWidthOut,
									int *actualHeightOut,
									CFHD_PixelFormat *actualFormatOut);
#endif

	static ENCODED_FORMAT GetEncodedFormat(void *samplePtr,
										   size_t sampleSize);

	CFHD_Error CopyToOutputBuffer(void *decodedBuffer, int decodedPitch,
								  void *outputBuffer, int outputPitch);
	CFHD_Error ConvertWhitePoint(void *decodedBuffer, int decodedPitch);

	void ReleaseFrameBuffer()
	{
		if (m_decodedFrameBuffer)
		{
			try
			{
				//_mm_free(m_decodedFrameBuffer);
				AlignFree(m_decodedFrameBuffer);
			}
			catch(...)
			{
			//	OutputDebugString("ReleaseFrameBuffer Exception");
			}
			m_decodedFrameBuffer = NULL;
			m_decodedFrameSize = 0;
		}
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
#ifdef __APPLE__
		return malloc(size);
#else
		return _mm_malloc(size, alignment);
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
#ifdef __APPLE__
		free(block);
#else
		_mm_free(block);
#endif
	}
public:

	// Convert internal codec data types into sample decoder data types
	static CFHD_EncodedFormat EncodedFormat(ENCODED_FORMAT encoded_format);
	static CFHD_FieldType FieldType(struct sample_header *header);

private:

	FILE *m_logfile;
	DECODER *m_decoder;
	CFHD_ALLOCATOR *m_allocator;

	int m_encodedWidth;
	int m_encodedHeight;
	ENCODED_FORMAT m_encodedFormat;

	int m_decodedWidth;
	int m_decodedHeight;

	int m_outputWidth;
	int m_outputHeight;

	// Output format represented using the decoder values
	CFHD_PixelFormat m_outputFormat;

	// The decoded format code as used internally by the codec
	DECODED_FORMAT m_decodedFormat;

	// The decoded resolution must match the ratio between the encoded and decoded dimensions
	DECODED_RESOLUTION m_decodedResolution;

	// Decode the sample into a temporary buffer for color conversion and scaling
	void *m_decodedFrameBuffer;
	uint32_t m_decodedFrameSize;

	// Pitch of the decoded frame in bytes
	int m_decodedFramePitch;

	// Decoding flags used when the decoder was initialized
	CFHD_DecodingFlags m_decodingFlags;

	// License key
	unsigned char m_license[16];

	// Decoder has been prepared for Thumbnail decodes
	bool m_preparedForThumbnails;

	uint32_t m_channelsActive;
	uint32_t m_channelMix;
};

#endif //_SAMPLE_DEC_H
