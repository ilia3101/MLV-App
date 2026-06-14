/*! @file SampleEncoder.cpp

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
#include <time.h>

#ifdef _WIN32
#else
#include <uuid/uuid.h>
#ifdef __APPLE__
//#include "GPOutputDebugString.h"
#endif
#endif

// Include files from the codec library
#include "encoder.h"
#include "thumbnail.h"

// Include files for the encoder DLL
#include "Allocator.h"
//#include "Includes/CFHDEncoder.h"
#include "CFHDError.h"
#include "CFHDTypes.h"
//#include "Lock.h"
#include "SampleMetadata.h"
#include "VideoBuffers.h"
//#include "SampleMetadata.h"
#include "SampleEncoder.h"

#include "metadata.h"

//TODO: Need to add logfile capability
#define LOGFILE 0

//TODO: Support arbitrary scaling
//#define _SCALING 0


// Embed an error from the codec library in an SDK error code
static inline CFHD_Error CFHD_CODEC_ERROR(CODEC_ERROR error)
{
	return (CFHD_Error)((uint32_t)CFHD_ERROR_CODEC_ERROR | (uint32_t)error);
}


CFHD_Error
CSampleEncoder::GetInputFormats(CFHD_PixelFormat *inputFormatArray,
								int inputFormatArrayLength,
								int *actualInputFormatCountOut)
{
	// Return the acceptable input formats in decreasing order of preference

	// List of input formats in decreasing order of preference
	static CFHD_PixelFormat inputFormats[] = {
		CFHD_PIXEL_FORMAT_RG64,
		CFHD_PIXEL_FORMAT_B64A,
		CFHD_PIXEL_FORMAT_BYR4,
		CFHD_PIXEL_FORMAT_BYR5,
		CFHD_PIXEL_FORMAT_RG48,
		CFHD_PIXEL_FORMAT_RG30,
		CFHD_PIXEL_FORMAT_AB10,
		CFHD_PIXEL_FORMAT_AR10,
		CFHD_PIXEL_FORMAT_R210,
		CFHD_PIXEL_FORMAT_DPX0,
		CFHD_PIXEL_FORMAT_BGRA,
		CFHD_PIXEL_FORMAT_BGRa,
		CFHD_PIXEL_FORMAT_RG24,
		CFHD_PIXEL_FORMAT_V210,
		CFHD_PIXEL_FORMAT_YUY2,
		CFHD_PIXEL_FORMAT_2VUY,
	};

	if (inputFormatArray == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	int inputFormatsCount = sizeof(inputFormats)/sizeof(inputFormats[0]);

	if (inputFormatsCount > inputFormatArrayLength) {
		inputFormatsCount = inputFormatArrayLength;
	}

	// Copy the acceptable input formats to the array argument
	for (int index = 0; index < inputFormatsCount; index++)
	{
		inputFormatArray[index] = inputFormats[index];
	}

	// Return the number of input formats copied into the output array
	if (actualInputFormatCountOut != NULL) {
		*actualInputFormatCountOut = inputFormatsCount;
	}

	return CFHD_ERROR_OKAY;
}

CFHD_Error
CSampleEncoder::PrepareToEncode(int inputWidth,
								int inputHeight,
								CFHD_PixelFormat inputFormat,
								CFHD_EncodedFormat encodedFormat,
								CFHD_EncodingFlags encodingFlags,
								CFHD_EncodingQuality *encodingQuality)
{
	CFHD_Error error = CFHD_ERROR_OKAY;

	int encodedWidth = inputWidth;		// Encoded dimensions
	int encodedHeight = inputHeight;

	FILE *logfile = NULL;
	bool progressive = true;
	//bool chromaFullRes = FALSE;

	CFHD_EncodingBitrate fixedBitrate = 0;
	int gopLength = 1;
	COLOR_FORMAT format = COLOR_FORMAT_UNKNOWN;
	int vsRGB=0,yuv601=0;
	unsigned int quality = (unsigned int)*encodingQuality;

	bool result;

	// Has the encoder been allocated and initialized?
	if (m_encoder != NULL)
	{
        // Have the encoding parameters changed?
		if (inputWidth != m_encodedWidth   || inputHeight != m_encodedHeight)
		{
			// Safest method is to destroy the encoder and let it be rebuilt
			EncodeRelease(m_encoder, m_transformArray, m_channelCount, NULL);
			free(m_encoder);
			m_encoder = NULL;
		}
	}

	// Has the encoder been allocated?
	if (m_encoder == NULL)
	{
		// Allocate the encoder data structure using the allocator
		m_encoder = (ENCODER *)Alloc(sizeof(ENCODER));
		if (m_encoder == NULL) {
			assert(0);
			error = CFHD_ERROR_OUTOFMEMORY;
			goto bail;
		}

#ifndef _WIN32
		// Check the features against the image size
		if( (m_watermark==WATERMARK_DISABLED) || (m_watermark==WATERMARK_ENABLED_RESOLUTION))
		{
			if(m_licenseFeatures[0]==0xFF)
			{
				if((inputWidth>1920) || (inputHeight>1080))
				{
					m_watermark = WATERMARK_ENABLED_RESOLUTION;
				}
				else
				{
					m_watermark = WATERMARK_DISABLED;
				}
			}
			else
			{
				m_watermark = WATERMARK_DISABLED;
			}
		}
#endif

		// Allocate the transform data structure
		for (int channel = 0; channel < FRAME_MAX_CHANNELS; channel++)
		{
			if (m_transformArray[channel] == NULL)
			{
				// Allocate the transform using the allocator
				m_transformArray[channel] = (TRANSFORM *)Alloc(sizeof(TRANSFORM));
				assert(m_transformArray[channel] != NULL);
				if (! (m_transformArray[channel] != NULL)) {
					error = CFHD_ERROR_OUTOFMEMORY;
					goto bail;
				}
				InitTransform(m_transformArray[channel]);
			}
		}


		format = EncoderColorFormat(inputFormat);
		int defaultformat = DefaultEncodedFormat(format, m_channelCount);

		switch (encodedFormat)
		{
		case CFHD_ENCODED_FORMAT_YUV_422:
			m_channelCount = 3;
			m_chromaFullRes = false;
			progressive = !(encodingFlags & CFHD_ENCODING_FLAGS_YUV_INTERLACED);
			gopLength = (encodingFlags & CFHD_ENCODING_FLAGS_YUV_2FRAME_GOP) ? 2 : 1;
			yuv601 = (encodingFlags & CFHD_ENCODING_FLAGS_YUV_601) ? 1 : 2;
			vsRGB = (encodingFlags & CFHD_ENCODING_FLAGS_RGB_STUDIO) ? 2 : 1;

			if(defaultformat != ENCODED_FORMAT_YUV_422)
			{
				if(Toggle444vs422EncodedFormat(format, m_channelCount) == ENCODED_FORMAT_YUV_422)
					quality |= 0x08000000;
				else if(Toggle4444vs444EncodedFormat(format, m_channelCount) == ENCODED_FORMAT_YUV_422)
					quality |= 0x20000000;
				else if(Toggle4444vs422EncodedFormat(format, m_channelCount) == ENCODED_FORMAT_YUV_422)
					quality |= 0x28000000;
				else
					assert(0);
			}
			break;

		case CFHD_ENCODED_FORMAT_RGB_444:
			m_channelCount = 3;
			m_chromaFullRes = true;
			vsRGB = (encodingFlags & CFHD_ENCODING_FLAGS_RGB_STUDIO) ? 2 : 1;
			
			if(defaultformat != ENCODED_FORMAT_RGB_444)
			{
				if(Toggle444vs422EncodedFormat(format, m_channelCount) == ENCODED_FORMAT_RGB_444)
					quality |= 0x08000000;
				else if(Toggle4444vs444EncodedFormat(format, m_channelCount) == ENCODED_FORMAT_RGB_444)
					quality |= 0x20000000;
				else if(Toggle4444vs422EncodedFormat(format, m_channelCount) == ENCODED_FORMAT_RGB_444)
					quality |= 0x28000000;
				else
					assert(0);
			}
			break;

		case CFHD_ENCODED_FORMAT_RGBA_4444:
			m_channelCount = 4;
			m_chromaFullRes = true;
			vsRGB = (encodingFlags & CFHD_ENCODING_FLAGS_RGB_STUDIO) ? 2 : 1;
			
			if(defaultformat != ENCODED_FORMAT_RGBA_4444)
			{
				if(Toggle444vs422EncodedFormat(format, m_channelCount) == ENCODED_FORMAT_RGBA_4444)
					quality |= 0x08000000;
				else if(Toggle4444vs444EncodedFormat(format, m_channelCount) == ENCODED_FORMAT_RGBA_4444)
					quality |= 0x20000000;
				else if(Toggle4444vs422EncodedFormat(format, m_channelCount) == ENCODED_FORMAT_RGBA_4444)
					quality |= 0x28000000;
				else
					assert(0);
			}
			break;

		case CFHD_ENCODED_FORMAT_BAYER:
			m_channelCount = 4;
			m_chromaFullRes = true;

			inputWidth >>= 1;
			inputHeight >>= 1;
			break;

		default:
			assert(0);
		}


		m_encodingQuality = (CFHD_EncodingQuality)quality;
		*encodingQuality = (CFHD_EncodingQuality)quality; // Return quality updates to calling function

		// Create and initialize an encoding parameters structure
		ENCODING_PARAMETERS parameters = {0};

		//TODO: Change the version number if more parameters are added

		parameters.version = 1;
		parameters.gop_length = gopLength;
		parameters.encoded_width = inputWidth;
		parameters.encoded_height = inputHeight;
		parameters.logfile = logfile;
		parameters.logfile = stderr;
		parameters.fixed_quality = (CFHD_EncodingQuality)quality;
		parameters.fixed_bitrate = fixedBitrate;
		parameters.progressive = progressive;
		parameters.format = format;
		parameters.frame_sampling = (m_chromaFullRes ? FRAME_SAMPLING_444 : FRAME_SAMPLING_422);

		parameters.colorspace_yuv = yuv601;//0 = unset, 1 = 601, 2 = 709
		parameters.colorspace_rgb = vsRGB;//0 = unset, 1 = cgRGB, 2 = vsRGB

#if _ALLOCATOR
		// Cast the allocator instance into an allocator for use by the encoder
		ALLOCATOR *allocator = (ALLOCATOR *)m_allocator;
		result = InitializeEncoderWithParameters(allocator, m_encoder, m_transformArray, m_channelCount, &parameters);
#else
		// Initialize the encoder
		result = InitializeEncoderWithParameters(m_encoder, m_transformArray, m_channelCount, &parameters);
#endif
		assert(result);
		if (!result) {
			error = CFHD_ERROR_CODEC_ERROR;
			goto bail;
		}

		// Assume video systems 601 color space
		//SetDecoderColorFlags(m_decoder, COLOR_SPACE_VS_RGB);

		// Remember the dimensions and format used for initializing the decoder
		m_encodedWidth = encodedWidth;
		m_encodedHeight = encodedHeight;
	}
	else
	{
		int newquality = (0xffff0000 & (int)m_encodingQuality) | (0xffff & quality);
		m_encodingQuality = (CFHD_EncodingQuality)newquality;
		SetEncoderQuality(m_encoder, newquality); // just changing quality
		return CFHD_ERROR_OKAY;
	}

	// Has a buffer for the encoded sample been allocated?
	if (m_sampleBuffer == NULL)
	{
		CFHD_Error err;
		// Allocate a buffer for the encoded sample
		if(encodingFlags & CFHD_ENCODING_FLAGS_LARGER_OUTPUT)
			err = AllocateSampleBuffer(inputWidth, inputHeight*2, inputFormat);
		else
			err = AllocateSampleBuffer(inputWidth, inputHeight, inputFormat);

		
		if(err != CFHD_ERROR_OKAY)
		{
			// Assume compression produces 3:1 or more.
			if(m_encodingFlags & CFHD_ENCODING_FLAGS_LARGER_OUTPUT)
				err = AllocateSampleBuffer(m_inputWidth, m_inputHeight*2/3, m_inputFormat);	
			else
				err = AllocateSampleBuffer(m_inputWidth, m_inputHeight/3, m_inputFormat);	

			if(err != CFHD_ERROR_OKAY)
			{
#if _WIN32
				OutputDebugString("First AllocateSampleBuffer Failed Again\n");
#endif
				return err;
			}
			
			m_encodingQuality = (CFHD_EncodingQuality)(0xffff000f & (uint32_t)m_encodingQuality);  // remove uncompressed flags.
			SetEncoderQuality(m_encoder, m_encodingQuality); // just changing quality
		}
	}

	// Remember the input dimensions and format
	m_inputWidth = inputWidth;
	m_inputHeight = inputHeight;
	m_inputFormat = inputFormat;
	m_encodingFlags = encodingFlags;

	// Save the encoding parameters
	m_gopLength = gopLength;

bail:
	return error;
}

CFHD_Error
CSampleEncoder::AllocateSampleBuffer(int inputWidth,
									 int inputHeight,
									 CFHD_PixelFormat inputFormat)
{
	//const int sampleAlignment = 16;

	if (m_sampleBuffer == NULL)
	{
		// Compute the pixel size for the specified input format
		size_t pixelSize = PixelSize(inputFormat);

		// Compute the maximum size of the encoded sample
		size_t sampleSize = inputWidth * inputHeight * pixelSize + 65536 /* metadata padding */;

		// Allocate the sample buffer using the allocator
		//m_sampleBuffer = AllocAligned(sampleSize, sampleAlignment);
		m_sampleBuffer = new CSampleBuffer(sampleSize, 16, m_allocator);
		if (m_sampleBuffer == NULL) {
			return CFHD_ERROR_OUTOFMEMORY;
		}

		if(m_sampleBuffer->Buffer() == NULL) {
			delete m_sampleBuffer;
			m_sampleBuffer = NULL;
			return CFHD_ERROR_OUTOFMEMORY;
		}


		//m_sampleBufferSize = sampleSize;
	}

	//assert(m_sampleBuffer != NULL && m_sampleBufferSize > 0);
	assert(m_sampleBuffer != NULL);

	return CFHD_ERROR_OKAY;
}

CFHD_Error
CSampleEncoder::ReleaseSampleBuffer()
{
	if (m_sampleBuffer != NULL)
	{
		// Was an allocator provided?
		//FreeAligned(m_sampleBuffer);
		delete m_sampleBuffer;
		m_sampleBuffer = NULL;
	}

	return CFHD_ERROR_OKAY;
}

CFHD_Error
CSampleEncoder::AllocateScratchBuffer(int inputWidth,
									  int inputHeight,
									  int inputPitch,
									  CFHD_PixelFormat inputFormat)
{
	if (m_scratchBuffer == NULL)
	{
		bool progressiveFlag = !m_interlacedSource;

		if(inputFormat == CFHD_PIXEL_FORMAT_BYR5)
		{
			inputHeight *= 4;
			inputHeight /= 3;
		}

#if _ALLOCATOR
		ALLOCATOR *allocator = (ALLOCATOR *)m_allocator;
		m_scratchBuffer = CreateEncodingBuffer(allocator, inputWidth, inputHeight, inputPitch,
											   inputFormat, m_gopLength, progressiveFlag,
											   &m_scratchBufferSize);
#else
		m_scratchBuffer = CreateEncodingBuffer(inputWidth, inputHeight, inputPitch,
											   inputFormat, m_gopLength, progressiveFlag,
											   &m_scratchBufferSize);
#endif
		assert(m_scratchBuffer != NULL);
		if (! (m_scratchBuffer != NULL)) {
			return CFHD_ERROR_OUTOFMEMORY;
		}
	}

	return CFHD_ERROR_OKAY;
}

CFHD_Error
CSampleEncoder::ReleaseScratchBuffer()
{
	if (m_scratchBuffer != NULL)
	{
#if _ALLOCATOR
		ALLOCATOR *allocator = (ALLOCATOR *)m_allocator;
		DeleteEncodingBuffer(allocator, (PIXEL *)m_scratchBuffer);
#else
		DeleteEncodingBuffer((PIXEL *)m_scratchBuffer);
#endif
		m_scratchBuffer = NULL;
	}

	return CFHD_ERROR_OKAY;
}


CFHD_Error
CSampleEncoder::EncodeSample(void *frameBuffer,
							 int framePitch,
							 CFHD_EncodingQuality frameQuality)
{
	//CFHD_Error errorCode = CFHD_ERROR_OKAY;
	//unsigned long decoding_flags;
	bool result;

	CFHD_EncodingQuality fixedQuality = m_encodingQuality;
	CFHD_EncodingBitrate fixedBitrate = m_encodingBitrate;

	if(frameQuality != CFHD_ENCODING_QUALITY_FIXED)
		fixedQuality = frameQuality;

	if(m_inputFormat == CFHD_PIXEL_FORMAT_BYR4)
		framePitch <<= 1;

	if(m_inputFormat == CFHD_PIXEL_FORMAT_BYR5)
		framePitch <<= 1;

	// Has the sample buffer been allocated?
	if (m_sampleBuffer == NULL)
	{
		CFHD_Error err;

		// Allocate a buffer for the encoded sample
		if(m_encodingFlags & CFHD_ENCODING_FLAGS_LARGER_OUTPUT)
			err = AllocateSampleBuffer(m_inputWidth, m_inputHeight*2, m_inputFormat);	
		else
			err = AllocateSampleBuffer(m_inputWidth, m_inputHeight, m_inputFormat);	
		
		if(err != CFHD_ERROR_OKAY)
		{
#if _WIN32
			OutputDebugString("AllocateSampleBuffer Failed\n");
#endif
			// Assume compression produces 3:1 or more.
			if(m_encodingFlags & CFHD_ENCODING_FLAGS_LARGER_OUTPUT)
				err = AllocateSampleBuffer(m_inputWidth, m_inputHeight*2/3, m_inputFormat);	
			else
				err = AllocateSampleBuffer(m_inputWidth, m_inputHeight/3, m_inputFormat);	

			fixedQuality = (CFHD_EncodingQuality)(0xffff000f & (uint32_t)fixedQuality);  // remove uncompressed flags.

			if(err != CFHD_ERROR_OKAY)
			{
#if _WIN32
				OutputDebugString("AllocateSampleBuffer Failed Again\n");
#endif
				return err;
			}
		}
	}

	// Has the scratch buffer been allocated?
	if (m_scratchBuffer == NULL)
	{
		CFHD_Error err;
		// Call a routine in the codec library to determine the scratch buffer size
		if (m_encodingFlags & CFHD_ENCODING_FLAGS_LARGER_OUTPUT)
			err = AllocateScratchBuffer(m_inputWidth, m_inputHeight*2, abs(framePitch), m_inputFormat);
		else
			err = AllocateScratchBuffer(m_inputWidth, m_inputHeight, abs(framePitch), m_inputFormat);

		if(err != CFHD_ERROR_OKAY)
			return err;
	}

	// Verify that the sample and scratch buffers have been allocated
	assert(m_sampleBuffer != NULL && m_scratchBuffer != NULL);
	
	// Initialize a bitstream to the sample data
	BITSTREAM bitstream;
	InitBitstreamBuffer(&bitstream,
						reinterpret_cast<uint8_t *>(m_sampleBuffer->Buffer()),
						m_sampleBuffer->BufferSize(),
						BITSTREAM_ACCESS_WRITE);

	// Convert the four character code to the pixel format used by the encoder
	COLOR_FORMAT colorFormat = EncoderColorFormat(m_inputFormat);
	assert(colorFormat != COLOR_FORMAT_UNKNOWN);
	if (! (colorFormat != COLOR_FORMAT_UNKNOWN)) {
		return CFHD_ERROR_BADFORMAT;
	}

	try
	{

		//fprintf(stderr, "Call EncodeSample inw: %d inh: %d framePitch: %d colorFmt: %d channels: %d inFormat %08X:\n",
		//		m_inputWidth, m_inputHeight, framePitch, colorFormat, m_channelCount, m_inputFormat);
		// Call the routine in the codec library to encode the sample
		result = ::EncodeSample(m_encoder, (uint8_t *)frameBuffer, m_inputWidth, m_inputHeight, framePitch,
								colorFormat, m_transformArray, m_channelCount, &bitstream,
								(PIXEL *)m_scratchBuffer, m_scratchBufferSize, fixedQuality, fixedBitrate,
								NULL, m_frameRate, NULL);
	}
	catch (...)
	{
#if _WIN32
		OutputDebugString("::EncodeSample: Unexpected error");
#endif
		return CFHD_ERROR_UNEXPECTED;
	}

	//fprintf(stderr, "Back from encode, result: %d error %d size %d\n",result,m_encoder->error,bitstream.nWordsUsed);
	if (!result) {
		//assert(0);
		//return (CFHD_Error)m_encoder->error;
		//return CFHD_ERROR_CODEC_ERROR;
		return CFHD_CODEC_ERROR(m_encoder->error);
	}

	m_sampleBuffer->SetActualSize(bitstream.nWordsUsed);

	// Indicate that the frame has been encoded
	return CFHD_ERROR_OKAY;
}

COLOR_FORMAT CSampleEncoder::EncoderColorFormat(CFHD_PixelFormat pixelFormat)
{
	COLOR_FORMAT colorFormat = COLOR_FORMAT_UNKNOWN;

	//fprintf(stderr, "Convert pixel format from: %08x\n",pixelFormat);

	switch (pixelFormat)
	{
	case CFHD_PIXEL_FORMAT_BGRA:
		colorFormat = COLOR_FORMAT_BGRA;		// BGRA
		break;
			
	case CFHD_PIXEL_FORMAT_2VUY:
		colorFormat = COLOR_FORMAT_UYVY;	// 2vuy
		break;

	case CFHD_PIXEL_FORMAT_BGRa:
		colorFormat = COLOR_FORMAT_RGB32_INVERTED;		// BGRA
		break;

	case CFHD_PIXEL_FORMAT_RG24:
		colorFormat = COLOR_FORMAT_RGB24;		// rgb
		break;

	case CFHD_PIXEL_FORMAT_YUY2:
		colorFormat = COLOR_FORMAT_YUYV;		// YUY2
		break;

	case CFHD_PIXEL_FORMAT_V210:
		colorFormat = COLOR_FORMAT_V210;		// v210
		break;

	case CFHD_PIXEL_FORMAT_R210:
		colorFormat = COLOR_FORMAT_R210;		// r210
		break;

	case CFHD_PIXEL_FORMAT_DPX0:
		colorFormat = COLOR_FORMAT_DPX0;		// DPX0
		break;

	case CFHD_PIXEL_FORMAT_AR10:
		colorFormat = COLOR_FORMAT_AR10;		// AR10
		break;

	case CFHD_PIXEL_FORMAT_AB10:
		colorFormat = COLOR_FORMAT_AB10;		// AB10
		break;

	case CFHD_PIXEL_FORMAT_RG30:
		colorFormat = COLOR_FORMAT_RG30;		// RG30
		break;

	case CFHD_PIXEL_FORMAT_B64A:
		colorFormat = COLOR_FORMAT_B64A;		// b64a
		break;

	case CFHD_PIXEL_FORMAT_BYR4:
		colorFormat = COLOR_FORMAT_BYR4;		// BYR4
		break;

	case CFHD_PIXEL_FORMAT_BYR5:
		colorFormat = COLOR_FORMAT_BYR5;		// BYR5
		break;

	case CFHD_PIXEL_FORMAT_YU64:
		colorFormat = COLOR_FORMAT_YU64;
		break;

	case CFHD_PIXEL_FORMAT_RG48:				// l48r
		colorFormat = COLOR_FORMAT_RGB48;
		break;

	case CFHD_PIXEL_FORMAT_RG64:
		colorFormat = COLOR_FORMAT_RGBA64;		//
		break;
		
	// Avid pixel formats
	case CFHD_PIXEL_FORMAT_CT_UCHAR:			//  avu8
		colorFormat = COLOR_FORMAT_CbYCrY_8bit;
		break;

	case CFHD_PIXEL_FORMAT_CT_10BIT_2_8:		// av28
		colorFormat = COLOR_FORMAT_CbYCrY_10bit_2_8;
		break;

	case CFHD_PIXEL_FORMAT_CT_SHORT_2_14:		// a214
		colorFormat = COLOR_FORMAT_CbYCrY_16bit_2_14;
		break;

	case CFHD_PIXEL_FORMAT_CT_USHORT_10_6:		// a106
		colorFormat = COLOR_FORMAT_CbYCrY_16bit_10_6;
		break;

	case CFHD_PIXEL_FORMAT_CT_SHORT:			// av16
		colorFormat = COLOR_FORMAT_CbYCrY_16bit;
		break;

	default:
		assert(0);
		colorFormat = COLOR_FORMAT_UNKNOWN;
		break;
	}

	return colorFormat;
}



uint32_t
CSampleEncoder::SetLicense(unsigned char *licensekey)
{
	uint32_t level = 31;

	return level;
}


CFHD_Error
CSampleEncoder::GetThumbnail(void *samplePtr,
		size_t sampleSize,
		void *outputBuffer,
		size_t outputSize, 
		uint32_t flags,
		size_t *retWidth,
		size_t *retHeight,
		size_t *retSize)
{
	if (GenerateThumbnail(samplePtr,
						  sampleSize,
						  outputBuffer,
						  outputSize, 
						  flags,
						  retWidth,
						  retHeight,
						  retSize))
	{
		return CFHD_ERROR_OKAY;
	}
	else
	{
		return CFHD_ERROR_CODEC_ERROR;
	}
}


CFHD_Error
CSampleEncoder::HandleMetadata()
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	if (m_encoder == NULL) {
		return CFHD_ERROR_CODEC_ERROR;
	}

	// Has the global metadata been initialized?
	//if (m_metadataGlobal == NULL)
	if (global[0].block == NULL)
	{
		// Add the basic metadata required for all encoded samples
#if _WIN32
		//UUID guid;
		//UuidCreate(&guid);
		GUID guid;
		CoCreateGuid(&guid);
#else
		uuid_t guid;
		uuid_generate(guid);
#endif
		AddMetadata(&global[0], TAG_CLIP_GUID, (int)'G', 16, (uint32_t *)&guid);
	}

	// update the clock
	{
		time_t		clock;
		struct tm	*	SystemTime;
		char datestr[16],timestr[16], tmpstr[16];

		clock = time(NULL);
		SystemTime = localtime( &clock );

#ifdef _WIN32
		sprintf_s(datestr, sizeof(datestr), "%04d-%02d-%02d", SystemTime->tm_year + 1900, SystemTime->tm_mon + 1, SystemTime->tm_mday);
		sprintf_s(timestr, sizeof(timestr), "%02d:%02d:%02d", SystemTime->tm_hour, SystemTime->tm_min, SystemTime->tm_sec);
#else
		sprintf(datestr, "%04d-%02d-%02d", SystemTime->tm_year + 1900, SystemTime->tm_mon + 1, SystemTime->tm_mday);
		sprintf(timestr, "%02d:%02d:%02d", SystemTime->tm_hour, SystemTime->tm_min, SystemTime->tm_sec);
#endif

		AddMetadata(&global[0], TAG_ENCODE_DATE, 'c', 10, (uint32_t *)datestr);
		AddMetadata(&global[0], TAG_ENCODE_TIME, 'c', 8, (uint32_t *)timestr);
#if 1
		// update Timecode and Unique Frame
		void *data;
		METADATA_SIZE retsize;
		METADATA_TYPE rettype;
		//uint32_t uniq;
		int in_global = 0;
		int in_local = 0;

		// look in Global
		if(!(data = MetadataFind(global[0].block, global[0].size, TAG_TIMECODE, &retsize, &rettype)))
		{
			// look in Local
			if(!(data = MetadataFind(local.block, local.size, TAG_TIMECODE, &retsize, &rettype)))
			{
				// not found, therefore generate

				m_last_timecode_base = 24;
				m_last_timecode_frame = SystemTime->tm_hour * 3600 * 24 + SystemTime->tm_min * 60 * 24 + SystemTime->tm_sec * 24;

#ifdef _WIN32
				sprintf_s(tmpstr, sizeof(tmpstr), "%02d:%02d:%02d:00", SystemTime->tm_hour, SystemTime->tm_min, SystemTime->tm_sec);
#else
				sprintf(tmpstr, "%02d:%02d:%02d:00", SystemTime->tm_hour, SystemTime->tm_min, SystemTime->tm_sec);
#endif

				AddMetadata(&global[0], TAG_TIMECODE, 'c', 11, (uint32_t *)tmpstr);
			}
			else
			{
				in_local = 1;
			}
		}
		else
		{
			in_global = 1;
		}

		if(data) // Timecode found;
		{
			char *tc = (char *)data;
			int hours = (tc[0]-'0')*10 + (tc[1]-'0');
			int mins = (tc[3]-'0')*10 + (tc[4]-'0');
			int secs = (tc[6]-'0')*10 + (tc[7]-'0');
			int frms = (tc[9]-'0')*10 + (tc[10]-'0');

			if(m_last_timecode_base == 0)
			{
				// look in Local
				if(!(data = MetadataFind(local.block, local.size,
						   TAG_TIMECODE_BASE, &retsize, &rettype)))
				{
					// look in Global
					if(!(data = MetadataFind(global[0].block, global[0].size,
					   TAG_TIMECODE_BASE, &retsize, &rettype)))
					{
						//guess
						m_last_timecode_base = 24;
					}
				}

				if(data)
				{
					m_last_timecode_base = *(uint8_t *)data;

					if (m_last_timecode_base == 0)
						m_last_timecode_base = 24;
				}
			}


			int framenum = hours * 3600 * m_last_timecode_base + mins * 60 * m_last_timecode_base + secs * m_last_timecode_base + frms;

			if (m_last_timecode_frame == -1)
			{
				m_last_timecode_frame = framenum;
			}
			else if(framenum == m_last_timecode_frame && m_last_timecode_base <= 30)
			{
				m_last_timecode_frame++;
				framenum = m_last_timecode_frame;

				frms = framenum % m_last_timecode_base; framenum /= m_last_timecode_base;
				secs = framenum % 60; framenum /= 60;
				mins = framenum % 60; framenum /= 60;
				hours = framenum % 60; framenum /= 60;

#ifdef _WIN32
				sprintf_s(tmpstr, sizeof(tmpstr), "%02d:%02d:%02d:%02d", hours, mins, secs, frms);
#else
				sprintf(tmpstr, "%02d:%02d:%02d:%02d", hours, mins, secs, frms);
#endif

				if (in_local)
				{
					AddMetadata(&local, TAG_TIMECODE, 'c', 11, (uint32_t *)tmpstr);
				}
				else
				{
					AddMetadata(&global[0], TAG_TIMECODE, 'c', 11, (uint32_t *)tmpstr);
				}
			}
		}


		in_local = in_global = 0;
		// look in Global
		if(!(data = MetadataFind(global[0].block, global[0].size,
			TAG_UNIQUE_FRAMENUM, &retsize, &rettype)))
		{
			// look in Local
			if(!(data = MetadataFind(local.block, local.size,
				TAG_UNIQUE_FRAMENUM, &retsize, &rettype)))
			{
				// not found, therefore generate

				m_last_unique_frame = 0;

				AddMetadata(&global[0], TAG_UNIQUE_FRAMENUM, (int)'L', 4, (uint32_t *)&m_last_unique_frame);
			}
			else
			{
				in_local = 1;
			}
		}
		else
		{
			in_global = 1;
		}

		if(data) // Unique_frame_num found;
		{
			int32_t unique_frame_number = *(uint32_t *)data;

			if (m_last_unique_frame == -1)
			{
				m_last_unique_frame = unique_frame_number;
			}
			else if (unique_frame_number <= m_last_unique_frame)
			{
				m_last_unique_frame++;

				if (in_local)
				{
					AddMetadata(&local, TAG_UNIQUE_FRAMENUM, 'L', 4, (uint32_t *)&m_last_unique_frame);
				}
				else
				{
					AddMetadata(&global[0], TAG_UNIQUE_FRAMENUM, 'L', 4, (uint32_t *)&m_last_unique_frame);
				}
			}
		}
#endif
	}

	METADATA *dstG = &m_encoder->metadata.global;
	METADATA *dstL = &m_encoder->metadata.local;
	AttachMetadata(m_encoder, dstG, &global[0]);
	AttachMetadata(m_encoder, dstL, &local);

	return errorCode;
}

CFHD_Error
CSampleEncoder::ApplyMetadata(METADATA *global, METADATA *local)
{
	METADATA *dstG = &m_encoder->metadata.global;
	METADATA *dstL = &m_encoder->metadata.local;
	AttachMetadata(m_encoder, dstG, global);
	AttachMetadata(m_encoder, dstL, local);
	return CFHD_ERROR_OKAY;
}

CFHD_Error
CSampleEncoder::MergeMetadata(METADATA *newglobal, METADATA *newlocal)
{
	CFHD_Error error = CFHD_ERROR_OKAY;

	if (m_encoder == NULL) {
		return CFHD_ERROR_CODEC_ERROR;
	}

	if (newglobal)
	{
		bool different = true;

		if (global[0].size == newglobal->size)
		{
			if (!memcmp(global[0].block, newglobal->block, newglobal->size))
			{
				different = false;
			}
		}

		if (different)
		{
			if (global[0].size != newglobal->size)
			{
#if _ALLOCATOR
				ALLOCATOR *allocator = newglobal->allocator;
#endif
				// Reallocate the buffer for the global metadata
				FreeMetadata(&global[0]);
				global[0].size = 0;

				// Allocate a new block large enough for the global metadata
#if _ALLOCATOR
				AllocMetadata(allocator, &global[0], newglobal->size);
#else
				AllocMetadata(&global[0], newglobal->size);
#endif
				if (global[0].block) {
					global[0].size = newglobal->size;
				}
			}

			if (global[0].block)
			{
				// Copy the new global metadata into the global metadata for this encoder
				memcpy(global[0].block, newglobal->block, newglobal->size);
			}
		}
	}

	if (newlocal)
	{
		bool different = true;

		if (local.size == newlocal->size)
		{
			if (!memcmp(local.block, newlocal->block, newlocal->size))
			{
				different = false;
			}
		}

		if (different)
		{
			if (local.size != newlocal->size)
			{
#if _ALLOCATOR
				ALLOCATOR *allocator = newlocal->allocator;
#endif
				// Reallocate the buffer for the local metadata
				FreeMetadata(&local);
				local.size = 0;

				// Allocate a new block large enough for the local metadata
				
#if _ALLOCATOR
				AllocMetadata(allocator, &local, newlocal->size);
#else
				AllocMetadata(&local, newlocal->size);
#endif
				if (local.block) {
					local.size = newlocal->size;
				}
			}

			if (local.block)
			{
				// Copy the new local metadata into the local metadata for this encoder
				memcpy(local.block, newlocal->block, newlocal->size);
			}
		}
	}

	return error;
}



CFHD_Error
CSampleEncoder::EyeDeltaMetadata(METADATA *both, METADATA *left, METADATA *right)
{
	CFHD_Error error = CFHD_ERROR_OKAY;
	bool different = true;

	if (m_encoder == NULL || both == NULL || left == NULL || right == NULL || both->block == NULL) {
		return CFHD_ERROR_CODEC_ERROR;
	}

	for(int eye = METADATA_EYE_DIFFLEFT; eye <= METADATA_EYE_DIFFRGHT; eye++)
	{
		uint32_t *ptr = (uint32_t *)left->block;
		int i,size = (int)left->size;

		if(eye == METADATA_EYE_DIFFRGHT)
		{
			ptr = (uint32_t *)right->block;
			size = (int)right->size;
		}
		while(ptr && size > 0)
		{
			METADATA_TAG tag = *ptr++; 
			uint32_t typesize = *ptr++; 
			METADATA_SIZE tagsize = typesize & 0xffffff;
			METADATA_TYPE tagtype = (typesize & 0xff000000) >> 24;
			METADATA_SIZE retsize;
			METADATA_TYPE rettype;
			void *retdata;

diff2both:
			if(FindMetadata(both, tag, &retdata, &retsize, &rettype))
			{
				if(retsize == tagsize && rettype == METADATA_TYPE_FLOAT) // only floats are in the difference data
				{
					if(0 != memcmp(retdata, ptr, tagsize)) // different
					{
						float fval[64],*fboth,*feye;

						fboth = (float*)ptr;
						feye = (float*)retdata;

						switch(tag)
						{
						case TAG_WHITE_BALANCE:
						case TAG_EXPOSURE:
						case TAG_RGB_GAIN:
						case TAG_FRAME_ZOOM:
						case TAG_FRAME_DIFF_ZOOM: // multiple diffs
							for(i=0; i<(int)tagsize/(int)sizeof(float); i++)
							{							
								fval[i] = *fboth++ / *feye++;
							}
							break;
						default:				 // addition diffs
						//case TAG_HORIZONTAL_OFFSET:
						//case TAG_VERTICAL_OFFSET:
						//case TAG_ROTATION_OFFSET:
						//case TAG_GAMMA_TWEAKS:
						//case TAG_RGB_OFFSET:
						//case TAG_SATURATION:
						//case TAG_CONTRAST:
							for(i=0; i<size/(int)sizeof(float); i++)
							{
								fval[i] = *fboth++ - *feye++;
							}
							break;
						}

						AddMetadata(&global[eye], tag, tagtype, tagsize, (uint32_t *)&fval[0]);
					}
				}
			}
			else 
			{
				if(( eye == METADATA_EYE_DIFFRGHT && FindMetadata(left, tag, &retdata, &retsize, &rettype)) ||
					(eye == METADATA_EYE_DIFFLEFT && FindMetadata(right, tag, &retdata, &retsize, &rettype)))
				{ 
					//so not in both, both it is in left and right.  Average.
					if(retsize == tagsize && rettype == METADATA_TYPE_FLOAT) // only floats are in the difference data
					{	
						float fval[64],*feye1,*feye2;

						feye1 = (float*)ptr;
						feye2 = (float*)retdata;

						for(i=0; i<(int)tagsize/(int)sizeof(float); i++)
						{							
							fval[i] = (*feye1++ + *feye2++)/2.0f;
						}

						// Store average in diff.
						AddMetadata(both, tag, tagtype, tagsize, (uint32_t *)&fval[0]);
						goto diff2both;
					}
					else
					{
						AddMetadata(both, tag, tagtype, tagsize, (uint32_t *)ptr);
					}
				}
				else // not found elsewhere, so store in both
				{
					AddMetadata(both, tag, tagtype, tagsize, (uint32_t *)ptr);
				}
			}	

			ptr += (tagsize + 3)>>2;
			size -= 8 + ((tagsize + 3) & 0xfffffc);
		}
	}

	if(global[METADATA_EYE_DIFFLEFT].block && global[METADATA_EYE_DIFFLEFT].size)
	{
		AddMetadata(both, TAG_EYE_DELTA_1, METADATA_TYPE_CUSTOM_DATA, (uint32_t)global[METADATA_EYE_DIFFLEFT].size, (uint32_t *)global[METADATA_EYE_DIFFLEFT].block);
	}
	if(global[METADATA_EYE_DIFFRGHT].block && global[METADATA_EYE_DIFFRGHT].size)
	{
		AddMetadata(both, TAG_EYE_DELTA_2, METADATA_TYPE_CUSTOM_DATA, (uint32_t)global[METADATA_EYE_DIFFRGHT].size, (uint32_t *)global[METADATA_EYE_DIFFRGHT].block);
	}

	if (global[0].size == both->size)
	{
		if (!memcmp(global[0].block, both->block, both->size))
		{
			different = false;
		}
	}

	if (different)
	{
		if (global[0].size != both->size)
		{
#if _ALLOCATOR
			ALLOCATOR *allocator = both->allocator;
#endif
			// Reallocate the buffer for the global metadata
			FreeMetadata(&global[0]);
			global[0].size = 0;

			// Allocate a new block large enough for the global metadata
#if _ALLOCATOR
			AllocMetadata(allocator, &global[0], both->size);
#else
			AllocMetadata(&global[0], both->size);
#endif
			if (global[0].block) {
				global[0].size = both->size;
			}
		}

		if (global[0].block)
		{
			// Copy the new global metadata into the global metadata for this encoder
			memcpy(global[0].block, both->block, both->size);
		}
	}

	return error;
}


size_t CSampleEncoder::PixelSize(CFHD_PixelFormat pixelFormat)
{
	size_t pixelSize = 0;

	switch (pixelFormat)
	{
	case CFHD_PIXEL_FORMAT_BGRA:
	case CFHD_PIXEL_FORMAT_BGRa:
		pixelSize = 4;
		break;

	case CFHD_PIXEL_FORMAT_YUY2:
	case CFHD_PIXEL_FORMAT_2VUY:
		pixelSize = 4;
		break;
		
	case CFHD_PIXEL_FORMAT_RG24:
	case CFHD_PIXEL_FORMAT_V210:
		pixelSize = 3;
		break;

	case CFHD_PIXEL_FORMAT_AB10:
	case CFHD_PIXEL_FORMAT_AR10:
	case CFHD_PIXEL_FORMAT_R210:
	case CFHD_PIXEL_FORMAT_DPX0:
	case CFHD_PIXEL_FORMAT_RG30:
		pixelSize = 4;
		break;

	case CFHD_PIXEL_FORMAT_RG48:
		pixelSize = 6;
		break;

	case CFHD_PIXEL_FORMAT_BYR4:
		pixelSize = 4*2;
		break;

	case CFHD_PIXEL_FORMAT_BYR5:
		pixelSize = 4*3/2;
		break;

	case CFHD_PIXEL_FORMAT_RG64:
	case CFHD_PIXEL_FORMAT_B64A:
	default:
		pixelSize = 8;
		break;
	}

	// Should have set the pixel size to a non-zero value
	assert(pixelSize > 0);

	return pixelSize;
}
