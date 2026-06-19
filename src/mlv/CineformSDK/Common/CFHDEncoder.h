/*! @file CFHDEncoder.h
*
*  @brief Interface to the CineForm HD encoder.  The encoder API uses an opaque
*  data type to represent an instance of an encoder.  The encoder reference
*  is returned by the call to @ref CFHD_OpenEncoder.
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
#ifndef CFHD_ENCODER_H
#define CFHD_ENCODER_H

#include "CFHDError.h"
#include "CFHDTypes.h"

#ifdef _WIN32
	#ifndef DYNAMICLIB
		#define CFHDENCODER_API
	#else
		#ifdef ENCODERDLL_EXPORTS
		// Export the entry points for the encoder
		#define CFHDENCODER_API __declspec(dllexport)
		#else
		// Declare the entry points to the encoder
		#define CFHDENCODER_API __declspec(dllimport)
		#endif
     #endif
#else
  #ifdef ENCODERDLL_EXPORTS
    #define CFHDENCODER_API __attribute__((visibility("default")))
  #else
    #define CFHDENCODER_API
  #endif
#endif

// Convenience macro that defines the entry point and return type
#define CFHDENCODER_API_(type) CFHDENCODER_API type

// Opaque datatypes for the CineForm HD encoder
typedef void *CFHD_EncoderRef;
typedef void *CFHD_MetadataRef;
typedef void *CFHD_EncoderPoolRef;
typedef void *CFHD_SampleBufferRef;

// Interface to the codec library for use with either C or C++
#ifdef __cplusplus
extern "C" {
#endif


#if DYNAMICALLY_LINK


// Open an instance of the CineForm HD ENCODER
CFHD_Error CFHD_OpenEncoderStub(CFHD_EncoderRef *encoderRefOut,
				 CFHD_ALLOCATOR *allocator);

// Return a list of input formats in decreasing order of preference
CFHD_Error CFHD_GetInputFormatsStub(CFHD_EncoderRef encoderRef,
					 CFHD_PixelFormat *inputFormatArray,
					 int inputFormatArrayLength,
					 int *actualInputFormatCountOut);

// Initialize for encoding frames with the specified dimensions and format
CFHD_Error CFHD_PrepareToEncodeStub(CFHD_EncoderRef encoderRef,
					 int frameWidth,
					 int frameHeight,
					 CFHD_PixelFormat pixelFormat,
					 CFHD_EncodedFormat encodedFormat,
					 CFHD_EncodingFlags encodingFlags,
					 CFHD_EncodingQuality encodingQuality);

// Set the license for the encoder, controlling time trials and encode resolutions, else watermarked
CFHD_Error CFHD_SetEncodeLicenseStub(CFHD_EncoderRef encoderRef,
					  unsigned char *licenseKey);
// Set the license for the encoder, controlling time trials and encode resolutions, else watermarked
CFHD_Error CFHD_SetEncodeLicense2Stub(CFHD_EncoderRef encoderRef,
					  unsigned char *licenseKey, uint32_t *level);
// Set the license for the encoder, controlling time trials and encode resolutions, else watermarked
CFHD_Error CFHD_SetEncodeLicenseCompat(CFHD_EncoderRef encoderRef,
					  unsigned char *licenseKey, uint32_t *level);

// Encode one sample of CineForm HD
CFHD_Error CFHD_EncodeSampleStub(CFHD_EncoderRef encoderRef,
				  void *frameBuffer,
				  int framePitch);

// Get the sample data and size of the encoded sample
CFHD_Error CFHD_GetSampleDataStub(CFHD_EncoderRef encoderRef,
				   void **sampleDataOut,
				   size_t *sampleSizeOut);

// Close an instance of the CineForm HD decoder
CFHD_Error CFHD_CloseEncoderStub(CFHD_EncoderRef encoderRef);

CFHD_Error CFHD_GetEncodeThumbnailStub(CFHD_EncoderRef encoderRef,
						void *samplePtr,
						size_t sampleSize,
						void *outputBuffer,
						size_t outputBufferSize,
						uint32_t flags,
						size_t *retWidth,
						size_t *retHeight,
						size_t *retSize);

CFHD_Error CFHD_MetadataOpenStub(CFHD_MetadataRef *metadataRefOut);

CFHD_Error CFHD_MetadataAddStub(CFHD_MetadataRef metadataRef,
				 uint32_t tag,
				 CFHD_MetadataType type,
				 size_t size,
				 uint32_t *data,
				 bool temporary);

CFHD_Error CFHD_MetadataAttachStub(CFHD_EncoderRef encoderRef, CFHD_MetadataRef metadataRef);

CFHD_Error CFHD_MetadataCloseStub(CFHD_MetadataRef metadataRef);

void CFHD_ApplyWatermarkStub(void *frameBuffer,
					int frameWidth,
					int frameHeight,
					int framePitch,
					CFHD_PixelFormat pixelFormat);

// Create an encoder pool for asynchronous encoding
CFHD_Error CFHD_CreateEncoderPoolStub(CFHD_EncoderPoolRef *encoderPoolRefOut,
					   int encoderThreadCount,
					   int jobQueueLength,
					   CFHD_ALLOCATOR *allocator);

// Return a list of input formats in decreasing order of preference
CFHD_Error CFHD_GetAsyncInputFormatsStub(CFHD_EncoderPoolRef encoderPoolRef,
						  CFHD_PixelFormat *inputFormatArray,
						  int inputFormatArrayLength,
						  int *actualInputFormatCountOut);

// Prepare the asynchronous encoders in a pool for encoding
CFHD_Error CFHD_PrepareEncoderPoolStub(CFHD_EncoderPoolRef encoderPoolRef,
						uint_least16_t frameWidth,
						uint_least16_t frameHeight,
						CFHD_PixelFormat pixelFormat,
						CFHD_EncodedFormat encodedFormat,
						CFHD_EncodingFlags encodingFlags,
						CFHD_EncodingQuality encodingQuality);

// Set the license for all of the encoders in the pool (otherwise use watermark)
CFHD_Error CFHD_SetEncoderPoolLicenseStub(CFHD_EncoderPoolRef encoderPoolRef,
						   unsigned char *licenseKey);
// Set the license for all of the encoders in the pool (otherwise use watermark)
CFHD_Error CFHD_SetEncoderPoolLicense2Stub(CFHD_EncoderPoolRef encoderPoolRef,
						   unsigned char *licenseKey, uint32_t *level);
// Set the license for all of the encoders in the pool (otherwise use watermark)
CFHD_Error CFHD_SetEncoderPoolLicenseCompat(CFHD_EncoderPoolRef encoderPoolRef,
						   unsigned char *licenseKey, uint32_t *level);

// Attach metadata to all of the encoders in the pool
CFHD_Error CFHD_AttachEncoderPoolMetadataStub(CFHD_EncoderPoolRef encoderPoolRef,
							   CFHD_MetadataRef metadataRef);

// Start the asynchronous encoders
CFHD_Error CFHD_StartEncoderPoolStub(CFHD_EncoderPoolRef encoderPoolRef);

// Stop the asynchronous encoders
CFHD_Error CFHD_StopEncoderPoolStub(CFHD_EncoderPoolRef encoderPoolRef);

// Submit a frame for asynchronous encoding
CFHD_Error CFHD_EncodeAsyncSampleStub(CFHD_EncoderPoolRef encoderPoolRef,
					   uint32_t frameNumber,
					   void *frameBuffer,
					   intptr_t framePitch,
					   CFHD_MetadataRef metadataRef);

// Wait until the next encoded sample is ready
CFHD_Error CFHD_WaitForSampleStub(CFHD_EncoderPoolRef encoderPoolRef,
				   uint32_t *frameNumberOut,
				   CFHD_SampleBufferRef *sampleBufferRefOut);

// Test whether the next encoded sample is ready
CFHD_Error CFHD_TestForSampleStub(CFHD_EncoderPoolRef encoderPoolRef,
				   uint32_t *frameNumberOut,
				   CFHD_SampleBufferRef *sampleBufferRefOut);

// Get the size and address of an encoded sample
CFHD_Error CFHD_GetEncodedSampleStub(CFHD_SampleBufferRef sampleBufferRef,
					  void **sampleDataOut,
					  size_t *sampleSizeOut);

// Get the thumbnail image from an encoded sample
CFHD_Error CFHD_GetSampleThumbnailStub(CFHD_SampleBufferRef sampleBufferRef,
						void *thumbnailBuffer,
						size_t bufferSize,
						uint32_t flags,
						uint_least16_t *actualWidthOut,
						uint_least16_t *actualHeightOut,
						CFHD_PixelFormat *pixelFormatOut,
						size_t *actualSizeOut);

// Release the sample buffer
CFHD_Error CFHD_ReleaseSampleBufferStub(CFHD_EncoderPoolRef encoderPoolRef,
						 CFHD_SampleBufferRef sampleBufferRef);

// Release the encoder pool
CFHD_Error CFHD_ReleaseEncoderPoolStub(CFHD_EncoderPoolRef encoderPoolRef);


#define CFHD_OpenEncoder                  CFHD_OpenEncoderStub
#define CFHD_GetInputFormats			  CFHD_GetInputFormatsStub
#define CFHD_PrepareToEncode			  CFHD_PrepareToEncodeStub
#define CFHD_SetEncodeLicense			  CFHD_SetEncodeLicenseStub
#define CFHD_EncodeSample				  CFHD_EncodeSampleStub
#define CFHD_GetSampleData				  CFHD_GetSampleDataStub
#define CFHD_CloseEncoder				  CFHD_CloseEncoderStub
#define CFHD_GetEncodeThumbnail			  CFHD_GetEncodeThumbnailStub
#define CFHD_MetadataOpen				  CFHD_MetadataOpenStub
#define CFHD_MetadataAdd				  CFHD_MetadataAddStub
#define CFHD_MetadataAttach				  CFHD_MetadataAttachStub
#define CFHD_MetadataClose				  CFHD_MetadataCloseStub
#define CFHD_ApplyWatermark				  CFHD_ApplyWatermarkStub
#define CFHD_CreateEncoderPool			  CFHD_CreateEncoderPoolStub
#define CFHD_GetAsyncInputFormats		  CFHD_GetAsyncInputFormatsStub
#define CFHD_PrepareEncoderPool			  CFHD_PrepareEncoderPoolStub
#define CFHD_SetEncoderPoolLicense		  CFHD_SetEncoderPoolLicenseStub
#define CFHD_AttachEncoderPoolMetadata	  CFHD_AttachEncoderPoolMetadataStub
#define CFHD_StartEncoderPool			  CFHD_StartEncoderPoolStub
#define CFHD_StopEncoderPool			  CFHD_StopEncoderPoolStub
#define CFHD_EncodeAsyncSample			  CFHD_EncodeAsyncSampleStub
#define CFHD_WaitForSample				  CFHD_WaitForSampleStub
#define CFHD_TestForSample				  CFHD_TestForSampleStub
#define CFHD_GetEncodedSample			  CFHD_GetEncodedSampleStub
#define CFHD_GetSampleThumbnail			  CFHD_GetSampleThumbnailStub
#define CFHD_ReleaseSampleBuffer		  CFHD_ReleaseSampleBufferStub
#define CFHD_ReleaseEncoderPool			  CFHD_ReleaseEncoderPoolStub
#define CFHD_SetEncodeLicense2			  CFHD_SetEncodeLicense2Stub
#define CFHD_SetEncoderPoolLicense2		  CFHD_SetEncoderPoolLicense2Stub


#else // DYNAMICALLY_LINK


// Open an instance of the CineForm HD ENCODER
CFHDENCODER_API CFHD_Error
CFHD_OpenEncoder(CFHD_EncoderRef *encoderRefOut,
				 CFHD_ALLOCATOR *allocator);

// Return a list of input formats in decreasing order of preference
CFHDENCODER_API CFHD_Error
CFHD_GetInputFormats(CFHD_EncoderRef encoderRef,
					 CFHD_PixelFormat *inputFormatArray,
					 int inputFormatArrayLength,
					 int *actualInputFormatCountOut);

// Initialize for encoding frames with the specified dimensions and format
CFHDENCODER_API CFHD_Error
CFHD_PrepareToEncode(CFHD_EncoderRef encoderRef,
					 int frameWidth,
					 int frameHeight,
					 CFHD_PixelFormat pixelFormat,
					 CFHD_EncodedFormat encodedFormat,
					 CFHD_EncodingFlags encodingFlags,
					 CFHD_EncodingQuality encodingQuality);

// Set the license for the encoder, controlling time trials and encode resolutions, else watermarked
CFHDENCODER_API CFHD_Error
CFHD_SetEncodeLicense(CFHD_EncoderRef encoderRef,
					  unsigned char *licenseKey);
// Set the license for the encoder, controlling time trials and encode resolutions, else watermarked
CFHDENCODER_API CFHD_Error
CFHD_SetEncodeLicense2(CFHD_EncoderRef encoderRef,
					  unsigned char *licenseKey, uint32_t *level);

// Encode one sample of CineForm HD
CFHDENCODER_API CFHD_Error
CFHD_EncodeSample(CFHD_EncoderRef encoderRef,
				  void *frameBuffer,
				  int framePitch);

// Get the sample data and size of the encoded sample
CFHDENCODER_API CFHD_Error
CFHD_GetSampleData(CFHD_EncoderRef encoderRef,
				   void **sampleDataOut,
				   size_t *sampleSizeOut);

// Close an instance of the CineForm HD decoder
CFHDENCODER_API CFHD_Error
CFHD_CloseEncoder(CFHD_EncoderRef encoderRef);


CFHDENCODER_API CFHD_Error
CFHD_GetEncodeThumbnail(CFHD_EncoderRef encoderRef,
						void *samplePtr,
						size_t sampleSize,
						void *outputBuffer,
						size_t outputBufferSize,
						uint32_t flags,
						size_t *retWidth,
						size_t *retHeight,
						size_t *retSize);

CFHDENCODER_API CFHD_Error
CFHD_MetadataOpen(CFHD_MetadataRef *metadataRefOut);

CFHDENCODER_API CFHD_Error
CFHD_MetadataAdd(CFHD_MetadataRef metadataRef,
				 uint32_t tag,
				 CFHD_MetadataType type,
				 size_t size,
				 uint32_t *data,
				 bool temporary);

CFHDENCODER_API CFHD_Error
CFHD_MetadataAttach(CFHD_EncoderRef encoderRef, CFHD_MetadataRef metadataRef);

CFHDENCODER_API CFHD_Error
CFHD_MetadataClose(CFHD_MetadataRef metadataRef);

CFHDENCODER_API void
CFHD_ApplyWatermark(void *frameBuffer,
					int frameWidth,
					int frameHeight,
					int framePitch,
					CFHD_PixelFormat pixelFormat);

// Create an encoder pool for asynchronous encoding
CFHDENCODER_API CFHD_Error
CFHD_CreateEncoderPool(CFHD_EncoderPoolRef *encoderPoolRefOut,
					   int encoderThreadCount,
					   int jobQueueLength,
					   CFHD_ALLOCATOR *allocator);

// Return a list of input formats in decreasing order of preference
CFHDENCODER_API CFHD_Error
CFHD_GetAsyncInputFormats(CFHD_EncoderPoolRef encoderPoolRef,
						  CFHD_PixelFormat *inputFormatArray,
						  int inputFormatArrayLength,
						  int *actualInputFormatCountOut);

// Prepare the asynchronous encoders in a pool for encoding
CFHDENCODER_API CFHD_Error
CFHD_PrepareEncoderPool(CFHD_EncoderPoolRef encoderPoolRef,
						uint_least16_t frameWidth,
						uint_least16_t frameHeight,
						CFHD_PixelFormat pixelFormat,
						CFHD_EncodedFormat encodedFormat,
						CFHD_EncodingFlags encodingFlags,
						CFHD_EncodingQuality encodingQuality);

// Set the license for all of the encoders in the pool (otherwise use watermark)
CFHDENCODER_API CFHD_Error
CFHD_SetEncoderPoolLicense(CFHD_EncoderPoolRef encoderPoolRef,
						   unsigned char *licenseKey);
// Set the license for all of the encoders in the pool (otherwise use watermark)
CFHDENCODER_API CFHD_Error
CFHD_SetEncoderPoolLicense2(CFHD_EncoderPoolRef encoderPoolRef,
						   unsigned char *licenseKey, uint32_t *level);

// Attach metadata to all of the encoders in the pool
CFHDENCODER_API CFHD_Error
CFHD_AttachEncoderPoolMetadata(CFHD_EncoderPoolRef encoderPoolRef,
							   CFHD_MetadataRef metadataRef);

// Start the asynchronous encoders
CFHDENCODER_API CFHD_Error
CFHD_StartEncoderPool(CFHD_EncoderPoolRef encoderPoolRef);

// Stop the asynchronous encoders
CFHDENCODER_API CFHD_Error
CFHD_StopEncoderPool(CFHD_EncoderPoolRef encoderPoolRef);

// Submit a frame for asynchronous encoding
CFHDENCODER_API CFHD_Error
CFHD_EncodeAsyncSample(CFHD_EncoderPoolRef encoderPoolRef,
					   uint32_t frameNumber,
					   void *frameBuffer,
					   intptr_t framePitch,
					   CFHD_MetadataRef metadataRef);

// Wait until the next encoded sample is ready
CFHDENCODER_API CFHD_Error
CFHD_WaitForSample(CFHD_EncoderPoolRef encoderPoolRef,
				   uint32_t *frameNumberOut,
				   CFHD_SampleBufferRef *sampleBufferRefOut);

// Test whether the next encoded sample is ready
CFHDENCODER_API CFHD_Error
CFHD_TestForSample(CFHD_EncoderPoolRef encoderPoolRef,
				   uint32_t *frameNumberOut,
				   CFHD_SampleBufferRef *sampleBufferRefOut);

// Get the size and address of an encoded sample
CFHDENCODER_API CFHD_Error
CFHD_GetEncodedSample(CFHD_SampleBufferRef sampleBufferRef,
					  void **sampleDataOut,
					  size_t *sampleSizeOut);

// Get the thumbnail image from an encoded sample
CFHDENCODER_API CFHD_Error
CFHD_GetSampleThumbnail(CFHD_SampleBufferRef sampleBufferRef,
						void *thumbnailBuffer,
						size_t bufferSize,
						uint32_t flags,
						uint_least16_t *actualWidthOut,
						uint_least16_t *actualHeightOut,
						CFHD_PixelFormat *pixelFormatOut,
						size_t *actualSizeOut);

// Release the sample buffer
CFHDENCODER_API CFHD_Error
CFHD_ReleaseSampleBuffer(CFHD_EncoderPoolRef encoderPoolRef,
						 CFHD_SampleBufferRef sampleBufferRef);

// Release the encoder pool
CFHDENCODER_API CFHD_Error
CFHD_ReleaseEncoderPool(CFHD_EncoderPoolRef encoderPoolRef);


#endif // DYNAMICALLY_LINK


#ifdef __cplusplus
}
#endif

#endif // CFHD_ENCODER_H
