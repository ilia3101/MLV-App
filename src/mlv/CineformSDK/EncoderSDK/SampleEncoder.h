/*! @file SampleEncoder.h

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

// Forward reference to the encoder in the code library
typedef struct encoder ENCODER;


typedef enum watermark_state
{
	WATERMARK_UNCHECKED = -1,				// Encoder license not yet checked
	WATERMARK_DISABLED = 0,					// No watermark on the video
	WATERMARK_ENABLED = 1,					// Apply the default watermark - stays on since license invalid
	WATERMARK_ENABLED_RESOLUTION = 2,		// Apply the watermark - resolution limit exceeded, but license valid
} watermark_state;


class CSampleEncoder
{
public:

	CSampleEncoder() :
		m_allocator(NULL),
		//m_privateAllocatorFlag(true),
		m_logfile(NULL),
		m_encoder(NULL),
		m_inputWidth(0),
		m_inputHeight(0),
		m_inputFormat(CFHD_PIXEL_FORMAT_UNKNOWN),
		m_encodedWidth(0),
		m_encodedHeight(0),
		m_channelCount(0),
		m_interlacedSource(false),
		m_chromaFullRes(false),
		m_gopLength(0),
		m_encodingQuality(CFHD_ENCODING_QUALITY_HIGH),
		m_encodingBitrate(0),
		m_scratchBuffer(NULL),
		m_scratchBufferSize(0),
		m_sampleBuffer(NULL),
#if 0
		m_sampleBufferSize(0),
		m_sampleBufferSizeReturned(0),
#endif
		m_frameRate(0.0),
		//m_metadataGlobal(NULL),
		//m_metadataGlobalSize(0),
		//m_metadataLocal(NULL),
		//m_metadataLocalSize(0),
		m_last_unique_frame(-1),
		m_last_timecode_base(0),
		m_last_timecode_frame(-1),
		m_watermark(WATERMARK_UNCHECKED)
	{
		// Clear the array of wavelet transforms
		memset(m_transformArray, 0, sizeof(m_transformArray));
		memset(m_licenseFeatures, 0, sizeof(m_licenseFeatures));
#if 0
		// Create a default allocator
		m_allocator = new CMemAlloc;
		assert(m_allocator);
#endif
		// Initialize the global metadata
		memset(&local, 0, sizeof(METADATA));
		for(int i=0; i<5; i++)
			memset(&global[i], 0, sizeof(METADATA));
	}

	CSampleEncoder(CFHD_ALLOCATOR *allocator) :
		m_allocator(allocator),
		//m_privateAllocatorFlag(false),
		m_logfile(NULL),
		m_encoder(NULL),
		m_inputWidth(0),
		m_inputHeight(0),
		m_inputFormat(CFHD_PIXEL_FORMAT_UNKNOWN),
		m_encodedWidth(0),
		m_encodedHeight(0),
		m_channelCount(0),
		m_interlacedSource(false),
		m_chromaFullRes(false),
		m_gopLength(0),
		m_encodingQuality(CFHD_ENCODING_QUALITY_HIGH),
		m_encodingBitrate(0),
		m_scratchBuffer(NULL),
		m_scratchBufferSize(0),
		m_sampleBuffer(NULL),
#if 0
		m_sampleBufferSize(0),
		m_sampleBufferSizeReturned(0),
#endif
		m_frameRate(0.0),
		//m_metadataGlobal(NULL),
		//m_metadataGlobalSize(0),
		//m_metadataLocal(NULL),
		//m_metadataLocalSize(0),
		m_last_unique_frame(-1),
		m_last_timecode_base(0),
		m_last_timecode_frame(-1),
		m_watermark(WATERMARK_UNCHECKED)
	{
		// Clear the array of wavelet transforms
		memset(m_transformArray, 0, sizeof(m_transformArray));

		// Clear the array of licensed features
		memset(m_licenseFeatures, 0, sizeof(m_licenseFeatures));

		// Initialize the global metadata
		memset(&local, 0, sizeof(METADATA));
		for(int i=0; i<5; i++)
			memset(&global[i], 0, sizeof(METADATA));
	}

	~CSampleEncoder()
	{
		// Release resources allocated by the encoder
		if (m_encoder)
		{
			EncodeRelease(m_encoder, m_transformArray, m_channelCount, NULL);

			// Release the encoder
			Free(m_encoder);
			m_encoder = NULL;
		}
		// Free the global metadata
		for(int i=0; i<5; i++)
			FreeMetadata(&global[i]);
		// Free the local metadata
		FreeMetadata(&local);
		

		// Free the transform data structures
		for (int channel = 0; channel < FRAME_MAX_CHANNELS; channel++)
		{
			if (m_transformArray[channel] != NULL)
			{
				Free(m_transformArray[channel]);
				m_transformArray[channel] = NULL;
			}
		}

		// Release the buffer allocated for encoded samples
		ReleaseSampleBuffer();

		// Release the scratch buffer used by the encoder
		ReleaseScratchBuffer();

		// Release the allocator if it is only referenced by this sample encoder
	//	if (m_allocator && m_privateAllocatorFlag) {
	//		//delete m_allocator; //DAN fixed memory trash
	//		m_allocator = NULL;
	//	}

		// Close the logfile
		if (m_logfile) {
			fclose(m_logfile);
			m_logfile = NULL;
		}
	}

	CFHD_Error GetInputFormats(CFHD_PixelFormat *inputFormatArray,
							   int inputFormatArrayLength,
							   int *actualInputFormatCountOut);

	CFHD_Error PrepareToEncode(int inputWidth,
							   int inputHeight,
							   CFHD_PixelFormat inputFormat,
							   CFHD_EncodedFormat encodedFormat,
							   CFHD_EncodingFlags encodingFlags,
							   CFHD_EncodingQuality *encodingQuality);

	CFHD_Error EncodeSample(void *frameBuffer,
							int framePitch,
							CFHD_EncodingQuality frameQuality = CFHD_ENCODING_QUALITY_FIXED);

	uint32_t SetLicense(unsigned char *license);
	
	CFHD_Error GetThumbnail(void *samplePtr,
							  size_t sampleSize,
							  void *outputBuffer,
							  size_t outputSize, 
							  uint32_t flags,
							  size_t *retWidth,
							  size_t *retHeight,
							  size_t *retSize);

	CFHD_Error SetAllocator(CFHD_ALLOCATOR * allocator)
	{
#if _ALLOCATOR
		m_allocator = allocator;
#endif
		return CFHD_ERROR_OKAY;
	}

	CFHD_Error GetAllocator(CFHD_ALLOCATOR ** allocator)
	{
#if _ALLOCATOR
		*allocator = m_allocator;
#endif
		return CFHD_ERROR_OKAY;
	}

	CFHD_Error HandleMetadata();

	CFHD_Error MergeMetadata(METADATA *global, METADATA *local);
	CFHD_Error EyeDeltaMetadata(METADATA *both, METADATA *left, METADATA *right);

	//! Use the metadata directly for encoding without further processing
	CFHD_Error ApplyMetadata(METADATA *global, METADATA *local);

	CFHD_Error AddLocalMetadata(METADATA_TAG tag, METADATA_TYPE type,
								 METADATA_SIZE size, uint32_t *data);

	CFHD_Error AddGlobalMetadata(METADATA_TAG tag, METADATA_TYPE type,
								 METADATA_SIZE size, uint32_t *data);

	CFHD_Error AddEyeMetadata(METADATA_TAG tag, METADATA_TYPE type, 
								METADATA_SIZE size, uint32_t *data, uint32_t eye);


	CFHD_Error FreeLocalMetadata()
	{
		CFHD_Error errorCode = CFHD_ERROR_OKAY;

		if (m_encoder == NULL) {
			return CFHD_ERROR_CODEC_ERROR;
		}

		if (local.block && local.size)
		{
			FreeMetadata(&local);
			FreeMetadata(&m_encoder->metadata.local); 
		}

		return errorCode;
	}

	CFHD_Error GetSampleData(void **sampleDataOut, size_t *sampleSizeOut)
	{
		if (sampleDataOut != NULL && sampleSizeOut != NULL)
		{
#if 0
			*sampleDataOut = m_sampleBuffer;
			*sampleSizeOut = m_sampleBufferSizeReturned;
#else
			*sampleDataOut = m_sampleBuffer->Buffer();
			*sampleSizeOut = m_sampleBuffer->Size();
#endif
			return CFHD_ERROR_OKAY;
		}
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	// Pass the sample buffer to the caller
	CFHD_Error GetSampleBuffer(CSampleBuffer **sampleBufferOut)
	{
		if (sampleBufferOut != NULL)
		{
			*sampleBufferOut = m_sampleBuffer;
			m_sampleBuffer = NULL;
			return CFHD_ERROR_OKAY;
		}
#ifndef __APPLE__
		//DebugOutput("Returning invalid argument error\n");
#endif
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	size_t PixelSize(CFHD_PixelFormat pixelFormat);
	
protected:

	// Allocate a buffer for the encoded sample
	CFHD_Error AllocateSampleBuffer(int inputWidth,
								    int inputHeight,
									CFHD_PixelFormat inputFormat);

	// Release the buffer allocated for encoded samples
	CFHD_Error ReleaseSampleBuffer();

	// Allocate a scratch buffer for encoding
	CFHD_Error AllocateScratchBuffer(int inputWidth,
									 int inputHeight,
									 int inputPitch,
									 CFHD_PixelFormat inputFormat);

	// Release the scratch buffer used for encoding
	CFHD_Error ReleaseScratchBuffer();

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

	void *AllocAligned(size_t size, size_t alignment)
	{
#if _ALLOCATOR
		// Use the aligned allocator if it is available
		if (m_allocator != NULL) {
			return m_allocator->vtable->aligned_malloc(m_allocator, size, alignment);
		}
#endif
		// Otherwise use the default aligned memory allocator
#ifdef __APPLE__
		return malloc(size);
#else
		return _mm_malloc(size, alignment);
#endif
	}

	void FreeAligned(void *block)
	{
#if _ALLOCATOR
		// Use the aligned allocator if it is available
		if (m_allocator) {
			m_allocator->vtable->aligned_free(m_allocator, block);
			return;
		}
#endif
		// Otherwise use the default aligned memory allocator
#ifdef __APPLE__
		free(block);
#else
		_mm_free(block);
#endif		
	}

	// Convert the four character code to the color format used by the encoder
	COLOR_FORMAT EncoderColorFormat(CFHD_PixelFormat pixelFormat);

	// Draw a watermark on the image before encoding
	static void ApplyWatermark(void *frameBuffer,
							   int frameWidth,
							   int frameHeight,
							   int framePitch,
							   CFHD_PixelFormat pixelFormat);

private:

	//IMemAlloc *m_allocator;
	CFHD_ALLOCATOR *m_allocator;

	// The destructor should release the allocator if it is private
	//bool m_privateAllocatorFlag;

	FILE *m_logfile;
	ENCODER *m_encoder;

	TRANSFORM *m_transformArray[TRANSFORM_MAX_CHANNELS];

	int m_inputWidth;					//!< Width of the input frames
	int m_inputHeight;					//!< Height of the input frames
	CFHD_PixelFormat m_inputFormat;		//!< Input pixel format
	CFHD_EncodingFlags m_encodingFlags;	//!< buffer allocation flags

	int m_encodedWidth;					//!< Encoded frame width
	int m_encodedHeight;				//!< Encoded frame height

	int m_channelCount;					//!< Number of channels

	bool m_interlacedSource;			//!< Interlaced video source?

	bool m_chromaFullRes;				//!< Chroma sampled at full resolution?

	int m_gopLength;					//!< Length of the group of pictures

	//! Quality of the encoding
	CFHD_EncodingQuality m_encodingQuality;

	//! Encoded bitrate
	CFHD_EncodingBitrate m_encodingBitrate;

	void *m_scratchBuffer;				//!< Scratch buffer used during encoding
	size_t m_scratchBufferSize;			//!< Size of the scratch buffer (in bytes)

	float m_frameRate;					//!< Frame rate

	//TODO: Change the frame rate to be a ration of cononical integers

	// The decoded format code as used internally by the codec
	//DECODED_FORMAT m_decodedFormat;

	// The decoded resolution must match the ratio between the encoded and decoded dimensions
	//DECODED_RESOLUTION m_decodedResolution;

#if 0
	void *m_sampleBuffer;
	size_t m_sampleBufferSize;
	size_t m_sampleBufferSizeReturned;
#else
	CSampleBuffer *m_sampleBuffer;
#endif

	//uint32_t *m_metadataGlobal;
	//size_t m_metadataGlobalSize;
	//uint32_t *m_metadataLocal;
	//size_t m_metadataLocalSize;
	METADATA global[5]; // 0-both, 1-left, 2-right, 3-diffLeft, 4-diffRight
	METADATA local;


	int32_t m_last_unique_frame;
	int32_t m_last_timecode_base;
	int32_t m_last_timecode_frame;

	int	m_watermark;

	uint8_t	m_licenseFeatures[8];
};
