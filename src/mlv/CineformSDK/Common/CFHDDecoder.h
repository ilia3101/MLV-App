/*! @file CFHDDecoder.h
*
*  @brief Interface to the CineForm HD decoder.  The decoder API uses an opaque
*  data type to represent an instance of an decoder.  The decoder reference
*  is returned by the call to @ref CFHD_OpenDecoder.
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
#ifndef CFHD_DECODER_H
#define CFHD_DECODER_H

#include "CFHDError.h"
#include "CFHDTypes.h"
#include "CFHDMetadata.h"
#ifdef __cplusplus
#include "CFHDSampleHeader.h"
#endif

#ifdef _WIN32
	#ifndef DYNAMICLIB
		#define CFHDDECODER_API
	#else
		#ifdef DECODERDLL_EXPORTS
			// Export the entry points for the decoder
			#define CFHDDECODER_API __declspec(dllexport)
		#else
			// Declare the entry points to the decoder
			#define CFHDDECODER_API __declspec(dllimport)
		#endif
	#endif
#else
	#ifdef DECODERDLL_EXPORTS
		#define CFHDDECODER_API __attribute__((visibility("default")))
	#else
		#define CFHDDECODER_API
	#endif
#endif

// Opaque datatype for the CineForm HD decoder
typedef void *CFHD_DecoderRef;

// Interface to the codec library for use with either C or C++
#ifdef __cplusplus
extern "C" {
#endif


#if DYNAMICALLY_LINK


// Open an instance of the CineForm HD decoder
CFHD_Error
CFHD_OpenDecoderStub(CFHD_DecoderRef *decoderRefOut,
				#if define(_WIN32)
				 CFHD_ALLOCATOR *allocator = NULL
				#else
				 CFHD_ALLOCATOR *allocator
				#endif
				 );

// Return a list of the output formats in decreasing order of preference
CFHD_Error
CFHD_GetOutputFormatsStub(CFHD_DecoderRef decoderRef,
					  void *samplePtr,
					  size_t sampleSize,
					  CFHD_PixelFormat *outputFormatArray,
					  int outputFormatArrayLength,
					  int *actualOutputFormatCountOut);

CFHD_Error
CFHD_GetSampleInfoStub(	CFHD_DecoderRef decoderRef,
					void *samplePtr,
					size_t sampleSize,
					CFHD_SampleInfoTag tag,
					void *value,
					size_t buffer_size);

// Initialize for decoding frames to the specified dimensions and format
CFHD_Error
CFHD_PrepareToDecodeStub(CFHD_DecoderRef decoderRef,
					 int outputWidth,
					 int outputHeight,
					 CFHD_PixelFormat outputFormat,
					 CFHD_DecodedResolution decodedResolution,
					 CFHD_DecodingFlags decodingFlags,
					 void *samplePtr,
					 size_t sampleSize,
					 int *actualWidthOut,
					 int *actualHeightOut,
					 CFHD_PixelFormat *actualFormatOut);

#ifdef __cplusplus
// The sample header is parsed to obtain information about the
//		video sample without decoding the video sample.
CFHD_Error
CFHD_ParseSampleHeaderStub(void *samplePtr,
					   size_t sampleSize,
					   CFHD_SampleHeader *sampleHeaderOut);
#endif

// Return the size of the specified pixel format in bytes
CFHD_Error
CFHD_GetPixelSizeStub(CFHD_PixelFormat pixelFormat, uint32_t *pixelSizeOut);

// Return the allocated length of each image row in bytes.
CFHD_Error
CFHD_GetImagePitchStub(uint32_t imageWidth, CFHD_PixelFormat pixelFormat, int32_t *imagePitchOut);

// Return the size of an image in bytes.
CFHD_Error
CFHD_GetImageSizeStub(uint32_t imageWidth, uint32_t imageHeight, CFHD_PixelFormat pixelFormat,
					  CFHD_VideoSelect videoselect,	CFHD_Stereo3DType stereotype, uint32_t *imageSizeOut);

// Decode one frame of CineForm HD encoded video
CFHD_Error
CFHD_DecodeSampleStub(CFHD_DecoderRef decoderRef,
				  void *samplePtr,
				  size_t sampleSize,
				  void *outputBuffer,
				  int outputPitch);

// Set the license for the decoder, controlling time trials and decode resolutions, else watermarked
CFHD_Error
CFHD_SetLicenseStub(CFHD_DecoderRef decoderRef,
				const unsigned char *licenseKey);

// Set the metadata rules for the decoder
CFHD_Error
CFHD_SetActiveMetadataStub(CFHD_DecoderRef decoderRef,
					   CFHD_MetadataRef metadataRef,
					   unsigned int tag,
					   CFHD_MetadataType type,
					   void *data,
					   CFHD_MetadataSize size);

CFHD_Error
CFHD_GetThumbnailStub(CFHD_DecoderRef decoderRef,
				  void *samplePtr,
				  size_t sampleSize,
				  void *outputBuffer,
				  size_t outputBufferSize,
				  size_t flags,
				  size_t *retWidth,
				  size_t *retHeight,
				  size_t *retSize);

// Clear the metadata rules for the decoder
CFHD_Error
CFHD_ClearActiveMetadataStub(CFHD_DecoderRef decoderRef,
					CFHD_MetadataRef metadataRef);

// Close an instance of the CineForm HD decoder
CFHD_Error
CFHD_CloseDecoderStub(CFHD_DecoderRef decoderRef);

// Close an instance of the CineForm HD developer
CFHD_Error
CFHD_CreateImageDeveloperStub(CFHD_DecoderRef decoderRef,
						  uint32_t imageWidth,
						  uint32_t imageHeight,
						  uint32_t sourceVideoChannels,
						  CFHD_PixelFormat pixelFormatSrc,
						  CFHD_PixelFormat pixelFormatDst);


#define CFHD_OpenDecoder			CFHD_OpenDecoderStub
#define CFHD_GetOutputFormats		CFHD_GetOutputFormatsStub
#define CFHD_GetSampleInfo			CFHD_GetSampleInfoStub
#define CFHD_PrepareToDecode		CFHD_PrepareToDecodeStub
#define CFHD_ParseSampleHeader		CFHD_ParseSampleHeaderStub
#define CFHD_GetPixelSize			CFHD_GetPixelSizeStub
#define CFHD_GetImagePitch			CFHD_GetImagePitchStub
#define CFHD_GetImageSize			CFHD_GetImageSizeStub
#define CFHD_DecodeSample			CFHD_DecodeSampleStub
#define CFHD_SetLicense				CFHD_SetLicenseStub
#define CFHD_SetActiveMetadata		CFHD_SetActiveMetadataStub
#define CFHD_GetThumbnail			CFHD_GetThumbnailStub
#define CFHD_ClearActiveMetadata	CFHD_ClearActiveMetadataStub
#define CFHD_CloseDecoder			CFHD_CloseDecoderStub
#define CFHD_CreateImageDeveloper	CFHD_CreateImageDeveloperStub


#else // DYNAMICALLY_LINK


// Open an instance of the CineForm HD decoder
CFHDDECODER_API CFHD_Error
CFHD_OpenDecoder(CFHD_DecoderRef *decoderRefOut,
				 CFHD_ALLOCATOR *allocator
				 );

// Return a list of the output formats in decreasing order of preference
CFHDDECODER_API CFHD_Error
CFHD_GetOutputFormats(CFHD_DecoderRef decoderRef,
					  void *samplePtr,
					  size_t sampleSize,
					  CFHD_PixelFormat *outputFormatArray,
					  int outputFormatArrayLength,
					  int *actualOutputFormatCountOut);

CFHDDECODER_API CFHD_Error
CFHD_GetSampleInfo(	CFHD_DecoderRef decoderRef,
					void *samplePtr,
					size_t sampleSize,
					CFHD_SampleInfoTag tag,
					void *value,
					size_t buffer_size);

// Initialize for decoding frames to the specified dimensions and format
CFHDDECODER_API CFHD_Error
CFHD_PrepareToDecode(CFHD_DecoderRef decoderRef,
					 int outputWidth,
					 int outputHeight,
					 CFHD_PixelFormat outputFormat,
					 CFHD_DecodedResolution decodedResolution,
					 CFHD_DecodingFlags decodingFlags,
					 void *samplePtr,
					 size_t sampleSize,
					 int *actualWidthOut,
					 int *actualHeightOut,
					 CFHD_PixelFormat *actualFormatOut);

#ifdef __cplusplus
// The sample header is parsed to obtain information about the
//		video sample without decoding the video sample.
CFHDDECODER_API CFHD_Error
CFHD_ParseSampleHeader(void *samplePtr,
					   size_t sampleSize,
					   CFHD_SampleHeader *sampleHeaderOut);
#endif

// Return the size of the specified pixel format in bytes
CFHDDECODER_API CFHD_Error
CFHD_GetPixelSize(CFHD_PixelFormat pixelFormat, uint32_t *pixelSizeOut);

// Return the allocated length of each image row (in bytes)
CFHDDECODER_API CFHD_Error
CFHD_GetImagePitch(uint32_t imageWidth, CFHD_PixelFormat pixelFormat, int32_t *imagePitchOut);

// Return the size of an image (in bytes)
CFHDDECODER_API CFHD_Error
CFHD_GetImageSize(uint32_t imageWidth, uint32_t imageHeight, CFHD_PixelFormat pixelFormat,
				  CFHD_VideoSelect videoselect,	CFHD_Stereo3DType stereotype, uint32_t *imageSizeOut);

// Decode one frame of CineForm HD encoded video
CFHDDECODER_API CFHD_Error
CFHD_DecodeSample(CFHD_DecoderRef decoderRef,
				  void *samplePtr,
				  size_t sampleSize,
				  void *outputBuffer,
				  int32_t outputPitch);

// Set the license for the decoder, controlling time trials and decode resolutions, else watermarked
CFHDDECODER_API CFHD_Error
CFHD_SetLicense(CFHD_DecoderRef decoderRef,
				const unsigned char *licenseKey);

// Set the metadata rules for the decoder
CFHDDECODER_API CFHD_Error
CFHD_SetActiveMetadata(CFHD_DecoderRef decoderRef,
					   CFHD_MetadataRef metadataRef,
					   unsigned int tag,
					   CFHD_MetadataType type,
					   void *data,
					   unsigned int size);

CFHDDECODER_API CFHD_Error
CFHD_GetThumbnail(CFHD_DecoderRef decoderRef,
				  void *samplePtr,
				  size_t sampleSize,
				  void *outputBuffer,
				  size_t outputBufferSize,
				  uint32_t flags,
				  size_t *retWidth,
				  size_t *retHeight,
				  size_t *retSize);

// Clear the metadata rules for the decoder
CFHDDECODER_API CFHD_Error
CFHD_ClearActiveMetadata(CFHD_DecoderRef decoderRef,
						 CFHD_MetadataRef metadataRef);

// Close an instance of the CineForm HD decoder
CFHDDECODER_API CFHD_Error
CFHD_CloseDecoder(CFHD_DecoderRef decoderRef);

// Close an instance of the CineForm HD developer
CFHDDECODER_API CFHD_Error
CFHD_CreateImageDeveloper(CFHD_DecoderRef decoderRef,
						  uint32_t imageWidth,
						  uint32_t imageHeight,
						  uint32_t sourceVideoChannels,
						  CFHD_PixelFormat pixelFormatSrc,
						  CFHD_PixelFormat pixelFormatDst);


#endif // DYNAMICALLY_LINK


#ifdef __cplusplus
}
#endif

#endif // CFHD_DECODER_H
