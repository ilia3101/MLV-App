/*! @file CFHDEncoderPool.cpp

*  @brief This module implements the C functions for the asynchronous encoder API.
*  
*  The asynchronous encoder uses a pool of asynchronous encoders for encoding samples
*  concurrently.  The encoder pool contains a queue of encoding jobs in the order in
*  which the encoded samples should be decoded and displayed.  All of the encoding jobs
*  in a GOP are sent in order to the same asynchronous encoder.  When encoding is done,
*  the encoding job is marked as done.  Encoded samples are removed from the queue of
*  encoding jobs in the order in which the input frames were placed in the queue.
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
#include "thumbnail.h"

//TODO: Eliminate references to the codec library

// Include files from the encoder DLL
#include "Allocator.h"
#include "CFHDEncoder.h"

#include "SampleMetadata.h"
#include "VideoBuffers.h"
#include "CFHDError.h"
#include "CFHDTypes.h"
#include "Lock.h"
#include "Condition.h"
#include "MetadataWriter.h"
#include "SampleEncoder.h"
#include "ThreadMessage.h"
#include "MessageQueue.h"
#include "ThreadPool.h"
#include "EncoderQueue.h"
#include "AsyncEncoder.h"
#include "EncoderPool.h"

static CEncoderPool *GetEncoderPool(CFHD_EncoderPoolRef encoderPoolRef)
{
	CEncoderPool *encoderPool = reinterpret_cast<CEncoderPool *>(encoderPoolRef);
	if (encoderPool == NULL) {
		throw CFHD_ERROR_UNEXPECTED;
	}
	assert(encoderPool != NULL);
	return encoderPool;
}

static CSampleBuffer *GetSampleBuffer(CFHD_SampleBufferRef sampleBufferRef)
{
	CSampleBuffer *sampleBuffer = reinterpret_cast<CSampleBuffer *>(sampleBufferRef);
	if (sampleBuffer == NULL) {
		throw CFHD_ERROR_UNEXPECTED;
	}
	assert(sampleBuffer != NULL);
	return sampleBuffer;
}

static CSampleEncodeMetadata *GetEncoderMetadata(CFHD_MetadataRef metadataRef)
{
	CSampleEncodeMetadata *metadata = NULL;
	if (metadataRef != NULL)
	{
		metadata = reinterpret_cast<CSampleEncodeMetadata *>(metadataRef);
		if (metadata == NULL) {
			throw CFHD_ERROR_UNEXPECTED;
		}
		assert(metadata != NULL);
	}
	return metadata;
}


/*!
	@brief Create an encoder pool for asynchronous encoding

	The encoder pool manages a set of encoders and a job queue of frames waiting
	to be encoded and samples that have been encoded.  The number of encoders is
	controlled by the encoder thread count and the maximum number of encoding jobs
	in the queue is determined by the job queue length parameter.  If a frame is
	submitted for encoding and the job queue is full, then the call will block
	until an encoded sample is removed from the queue.
*/
CFHDENCODER_API CFHD_Error
CFHD_CreateEncoderPool(CFHD_EncoderPoolRef *encoderPoolRefOut,
					   int encoderThreadCount,
					   int jobQueueLength,
					   CFHD_ALLOCATOR *allocator)
{
	CEncoderPool *encoderPool = NULL;

	try
	{
		encoderPool = new CEncoderPool(encoderThreadCount, jobQueueLength, allocator);
		if (encoderPool == NULL) {
			return CFHD_ERROR_OUTOFMEMORY;
		}

		*encoderPoolRefOut = reinterpret_cast<CFHD_EncoderPoolRef>(encoderPool);
		
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_CreateEncoderPool ref:%04x thread:%d", (0xffff)&(int)*encoderPoolRefOut, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
		return CFHD_ERROR_OKAY;
	}
	catch (...)
	{
		if (encoderPool != NULL) {
			delete encoderPool;
			encoderPool = NULL;
		}

		return CFHD_ERROR_UNEXPECTED;
	}
}

/*!
	@brief Return a list of input formats in decreasing order of preference

	This routine is identical to @ref CFHD_GetInputFormats, except that it is
	called with an encoder pool as the first argument instead of a sample encoder.
*/
CFHDENCODER_API CFHD_Error
CFHD_GetAsyncInputFormats(CFHD_EncoderPoolRef encoderPoolRef,
						  CFHD_PixelFormat *inputFormatArray,
						  int inputFormatArrayLength,
						  int *actualInputFormatCountOut)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_GetAsyncInputFormats ref:%04x thread:%d", (0xffff)&(int)encoderPoolRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CEncoderPool *encoderPool = GetEncoderPool(encoderPoolRef);
		return encoderPool->GetInputFormats(inputFormatArray,
											inputFormatArrayLength,
											actualInputFormatCountOut);
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}
}

/*!
	@brief Prepare the encoders in a pool for encoding

	This routine initializes each of the encoders in the pool.
	It is equivalent to using @ref CFHD_PrepareToEncode to
	initialize every encoder in the pool.  This routine cannot
	be called after the encoders have been started.
*/
CFHDENCODER_API CFHD_Error
CFHD_PrepareEncoderPool(CFHD_EncoderPoolRef encoderPoolRef,
						uint_least16_t frameWidth,
						uint_least16_t frameHeight,
						CFHD_PixelFormat pixelFormat,
						CFHD_EncodedFormat encodedFormat,
						CFHD_EncodingFlags encodingFlags,
						CFHD_EncodingQuality encodingQuality)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_PrepareEncoderPool ref:%04x thread:%d", (0xffff)&(int)encoderPoolRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CEncoderPool *encoderPool = GetEncoderPool(encoderPoolRef);
		return encoderPool->PrepareToEncode(frameWidth, frameHeight, pixelFormat,
											encodedFormat, encodingFlags, encodingQuality);
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}
}

/*!
	@brief Set the license for all of the encoders in the pool
	
	This routine sets applies the license to each of the encoders in the pool.
	Without a license, the encoded frames will be watermarked.  This routine
	cannot be called after the encoders have been started.
	
	@description The license key is used to control trial periods, format and resolution
	limits.

	@param encoderPoolRef
	An opaque reference to a decoder created by a call to @ref CFHD_OpenEncoder.

	@param licenseKey
	Pointer to an array of 16 bytes contain the license key.

	@return Returns a CFHD error code.
*/
CFHDENCODER_API CFHD_Error
CFHD_SetEncoderPoolLicense(CFHD_EncoderPoolRef encoderPoolRef,
						   unsigned char *licenseKey)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_SetEncoderPoolLicense ref:%04x thread:%d", (0xffff)&(int)encoderPoolRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CEncoderPool *encoderPool = GetEncoderPool(encoderPoolRef);
		uint32_t level = encoderPool->SetLicense(licenseKey);
		if(level == 0)
			return CFHD_ERROR_LICENSING;
		else
			return CFHD_ERROR_OKAY;
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}
}


/*!
	@brief Set the license for all of the encoders in the pool
	
	This routine sets applies the license to each of the encoders in the pool.
	Without a license, the encoded frames will be watermarked.  This routine
	cannot be called after the encoders have been started.
	
	@description The license key is used to control trial periods, format and resolution
	limits.

	@param encoderPoolRef
	An opaque reference to a decoder created by a call to @ref CFHD_OpenEncoder.

	@param licenseKey
	Pointer to an array of 16 bytes contain the license key.
	
	@param level
	Pointer to an 32-bit long to return the license level mask.
	level 0 for no license, 
	1 for 422, 
	2 for 444,
	4 for 4444,
	8 for RAW,
	16 for 3D

	@return Returns a CFHD error code.
*/
CFHDENCODER_API CFHD_Error
CFHD_SetEncoderPoolLicense2(CFHD_EncoderPoolRef encoderPoolRef,
						   unsigned char *licenseKey,
						   uint32_t *level)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_SetEncoderPoolLicense2 ref:%04x thread:%d", (0xffff)&(int)encoderPoolRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CEncoderPool *encoderPool = GetEncoderPool(encoderPoolRef);
		*level = encoderPool->SetLicense(licenseKey);
		if(*level == 0)
			return CFHD_ERROR_LICENSING;
		else
			return CFHD_ERROR_OKAY;
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}
}

/*!
	@brief Attach metadata to the encoders in the pool.

	Every encoding job in the queue has a copy of the metadata that was
	attached to the encoder pool when the frame was submitted for encoding.
	The metadata provided in this call will apply to subsequent frames that
	are submitted for encoding.

	The encoder pool automatically applies the same GUID to all encoded
	samples and updates the timecode and unique frame ID for each frame.
	Specifying the timecode or unique frame number in metadata provided
	as an argument to this routine will reset the timecode or frame number.

	This routine can be called once before encoding begins to provide metadata
	that is common to all encoded frames and to provide the starting timecode
	and unique frame number.  It is not necessary to call this routine for
	every frame that is submitted for encoding as the timecode and frame number
	will be incremented automatically, but this routine can be called to change
	the metadata that will be used for subsequent frames.

	There is no way to change the metadata that will be used for encoding frames
	that have already been submitted.
*/
CFHDENCODER_API CFHD_Error
CFHD_AttachEncoderPoolMetadata(CFHD_EncoderPoolRef encoderPoolRef,
							   CFHD_MetadataRef metadataRef)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_AttachEncoderPoolMetadata ref:%04x thread:%d", (0xffff)&(int)encoderPoolRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CEncoderPool *encoderPool = GetEncoderPool(encoderPoolRef);
		CSampleEncodeMetadata *encoderMetadata = GetEncoderMetadata(metadataRef);
		
		CFHD_ALLOCATOR *encAllocator = NULL;
		CFHD_ALLOCATOR *metAllocator = NULL;
		encoderPool->GetAllocator(&encAllocator);
		encoderMetadata->GetAllocator(&metAllocator);
		if(encAllocator && metAllocator == NULL)
		{
			encoderMetadata->SetAllocator(encAllocator);
		}

		return encoderPool->AttachMetadata(encoderMetadata);
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}
}

/*!
	@brief Start all of the encoders in the pool

	Each encoder runs in its own thread so that all of the encoders can work concurrently.
	This routine starts the worker thread for each of the encoders in the pool.  Once the
	encoders have been started, the encoders can not be reinitialized and the license cannot
	be changed.  It is necessary to stop all of the encoders before calling any initialization
	routine such as @ref CFHD_PrepareEncoderPool or @ref CFHD_SetEncoderPoolLicense.
*/
CFHDENCODER_API CFHD_Error
CFHD_StartEncoderPool(CFHD_EncoderPoolRef encoderPoolRef)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_StartEncoderPool ref:%04x thread:%d", (0xffff)&(int)encoderPoolRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CEncoderPool *encoderPool = GetEncoderPool(encoderPoolRef);
		return encoderPool->StartEncoders();
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}

	return CFHD_ERROR_OKAY;
}

/*!
	@brief Stop all of the encoders in the pool.
	
	Each encoder will be allowed to finish encoding the frames that have been assigned
	to that encoder, but no more frames can be submitted for encoding.  The worker
	thread associated with each encoder is terminated after the encoder has finished
	encoding all of the frames that have been assigned to it.

	After the encoder pool has been stopped, the encoders can be reinitialized by calling
	@ref CFHD_PrepareEncoderPool and the encoder pool can be re
*/
CFHDENCODER_API CFHD_Error
CFHD_StopEncoderPool(CFHD_EncoderPoolRef encoderPoolRef)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_StopEncoderPool ref:%04x thread:%d", (0xffff)&(int)encoderPoolRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CEncoderPool *encoderPool = GetEncoderPool(encoderPoolRef);
		return encoderPool->StopEncoders();
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}

	return CFHD_ERROR_OKAY;
}

/*!
	@brief Submit a frame for asynchronous encoding

	Add a new frame to the end of the queue of encoding jobs.  The metadata
	that was attached to the encoder pool at the time that this frame is
	submitted for encoding will be copied so that subsequent changes to the
	metadata will not affect the metadata used for encoding frames that have
	already been submitted.

	The frame number is not the same as the unique frame ID.  The frame
	number provided to this routine is used to identify the encoded sample.
	Encoded samples are returned in the order in which the frames were
	submitted to the encoder pool, so it is not necessary to use the frame
	number to sort the encoded samples into the correct order.

	The intent of the frame number parameter is to provide an easy method
	for identifying the frame that was used to create the encoded sample.
	For example, the frame number returned with the encoded sample can be used
	as an index into the pool of frame buffers managed by the application.
	When an encoded sample is returned to the application, the frame number
	can be used to identify the frame buffer associated with the encoded sample
	and that frame buffer can be released.
*/
CFHDENCODER_API CFHD_Error
CFHD_EncodeAsyncSample(CFHD_EncoderPoolRef encoderPoolRef,
					   uint32_t frameNumber,
					   void *frameBuffer,
					   ptrdiff_t framePitch,
					   CFHD_MetadataRef metadataRef)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_EncodeAsyncSample ref:%04x mref:%04x thread:%d", (0xffff)&(int)encoderPoolRef, (0xffff)&(int)metadataRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CEncoderPool *encoderPool = GetEncoderPool(encoderPoolRef);
		CSampleEncodeMetadata *encoderMetadata = GetEncoderMetadata(metadataRef);
		bool keyFrame = true;
		return encoderPool->EncodeSample(frameNumber, (uint8_t *)frameBuffer, framePitch, keyFrame, encoderMetadata);
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}
}

/*!
	@brief Wait until the next encoded sample is ready

	Encoded samples are returned to the caller in the order in which the
	frames were submitted for encoding.  The frame number is the same number
	that was provided as an argument to @ref CFHD_EncodeAsyncSample.

	This routine blocks until the next encoded sample is ready.

	The routine returns a sample buffer that must be released by the
	application when the sample is no longer needed.

	See also @ref CFHD_GetEncodedSample and @ref CFHD_ReleaseSampleBuffer.
*/
CFHDENCODER_API CFHD_Error
CFHD_WaitForSample(CFHD_EncoderPoolRef encoderPoolRef,
				   uint32_t *frameNumberOut,
				   CFHD_SampleBufferRef *sampleBufferRefOut)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_WaitForSample ref:%04x thread:%d", (0xffff)&(int)encoderPoolRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CFHD_Error error = CFHD_ERROR_OKAY;
		CEncoderPool *encoderPool = GetEncoderPool(encoderPoolRef);
		uint32_t frameNumber = 0;
		CSampleBuffer *sampleBuffer = NULL;
		error = encoderPool->WaitForSample(&frameNumber, &sampleBuffer);
		if (error != CFHD_ERROR_OKAY) {
			return error;
		}
		*frameNumberOut = frameNumber;
		*sampleBufferRefOut = reinterpret_cast<CFHD_SampleBufferRef>(sampleBuffer);
		return CFHD_ERROR_OKAY;
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}
}

/*!
	@brief Test whether the next encoded sample is ready

	Encoded samples are returned to the caller in the order in which the
	frames were submitted for encoding.  The frame number is the same number
	that was provided as an argument to @ref CFHD_EncodeAsyncSample.

	This routine returns the error code CFHD_ERROR_NOT_FINISHED if the next
	sample is not ready.

	The routine returns a sample buffer that must be released by the
	application when the sample is no longer needed.

	See also @ref CFHD_GetEncodedSample and @ref CFHD_ReleaseSampleBuffer.
*/
CFHDENCODER_API CFHD_Error
CFHD_TestForSample(CFHD_EncoderPoolRef encoderPoolRef,
				   uint32_t *frameNumberOut,
				   CFHD_SampleBufferRef *sampleBufferRefOut)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_TestForSample ref:%04x thread:%d", (0xffff)&(int)encoderPoolRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CFHD_Error error = CFHD_ERROR_OKAY;
		CEncoderPool *encoderPool = GetEncoderPool(encoderPoolRef);
		uint32_t frameNumber = 0;
		CSampleBuffer *sampleBuffer = NULL;
		error = encoderPool->TestForSample(&frameNumber, &sampleBuffer);
		if (error != CFHD_ERROR_OKAY) {
			return error;
		}
		*frameNumberOut = frameNumber;
		*sampleBufferRefOut = reinterpret_cast<CFHD_SampleBufferRef>(sampleBuffer);
		return CFHD_ERROR_OKAY;
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}
}

/*!
	@brief Get the size and address of an encoded sample

	The routine returns the address of the sample in the sample
	buffer without copying the sample, so the sample buffer must
	not be released until the application is done with the sample.
*/
CFHDENCODER_API CFHD_Error
CFHD_GetEncodedSample(CFHD_SampleBufferRef sampleBufferRef,
					  void **sampleDataOut,
					  size_t *sampleSizeOut)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_GetEncodedSample thread:%d", GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	if (sampleDataOut == NULL || sampleSizeOut == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	try
	{
		CSampleBuffer *sample = GetSampleBuffer(sampleBufferRef);
		*sampleDataOut = sample->Buffer();
		*sampleSizeOut = sample->Size();
		return CFHD_ERROR_OKAY;
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}
}

/*!
	@brief Get the thumbnail image from an encoded sample

	The routine returns the thumbnail image from an encoded sample
	without decoding the sample.  The actual width and height of the
	thumbnail image is returned.

	@param sampleBufferRef
	The encoded sample buffer returned by the encoder pool.

	@param thumbnailBuffer
	The buffer for the thumbnail image.

	@param bufferSize
	Size of the thumbnail image buffer in bytes.

	@param flags
	Reserved for future use.

	@param actualWidthOut
	The actual width of the thumbnail image.
	This argument may be NULL if the actual width is not needed.

	@param actualHeightOut
	The actual height of the thumbnail image.
	This argument may be NULL if the actual height is not needed.

	@param pixelFormatOut
	The actual pixel format of the thumbnail image.
	This argument may be NULL if the actual format is not needed.

	@param actualSizeOut
	The actual size of the thumbnail image (in bytes).  The pitch of
	the thumbnail image is the actual size divided by teh actual height.
	This argument may be NULL if the actual size is not needed.
*/
CFHDENCODER_API CFHD_Error
CFHD_GetSampleThumbnail(CFHD_SampleBufferRef sampleBufferRef,
						void *thumbnailBuffer,
						size_t bufferSize,
						uint32_t flags,
						uint_least16_t *actualWidthOut,
						uint_least16_t *actualHeightOut,
						CFHD_PixelFormat *pixelFormatOut,
						size_t *actualSizeOut)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_GetSampleThumbnail thread:%d", GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CSampleBuffer *sampleBuffer = GetSampleBuffer(sampleBufferRef);
		if (sampleBuffer == NULL) {
			return CFHD_ERROR_INVALID_ARGUMENT;
		}

		// Did the caller set the thumbnail flags?
		if (flags == 0) {
			// Use the default value for the thumbnail flags
			flags = 1;
		}

		size_t actualWidth = 0;
		size_t actualHeight = 0;
		size_t actualSize = 0;

		if (thumbnailBuffer == NULL || bufferSize == 0)
		{
			// Compute the thumbnail dimensions and format
			if (GetThumbnailInfo(sampleBuffer->Buffer(),
								 sampleBuffer->Size(),
								 flags,
								 &actualWidth,
								 &actualHeight,
								 &actualSize))
			{
				if (actualWidthOut) {
					assert(actualWidth <= UINT_LEAST16_MAX);
					*actualWidthOut = (uint_least16_t)actualWidth;
				}
				if (actualHeightOut) {
					assert(actualWidth <= UINT_LEAST16_MAX);
					*actualHeightOut = (uint_least16_t)actualHeight;
				}
				if (pixelFormatOut) {
					*pixelFormatOut = CFHD_PIXEL_FORMAT_DPX0;
				}
				if (actualSizeOut) {
					*actualSizeOut = actualSize;
				}
				return CFHD_ERROR_OKAY;
			}
		}
		else
		{
			// Generate the thumbnail image from the encoded sample
			if (GenerateThumbnail(sampleBuffer->Buffer(),
								  sampleBuffer->Size(),
								  thumbnailBuffer,
								  bufferSize, 
								  flags,
								  &actualWidth,
								  &actualHeight,
								  &actualSize))
			{
				if (actualWidthOut) {
					assert(actualWidth <= UINT_LEAST16_MAX);
					*actualWidthOut = (uint_least16_t)actualWidth;
				}
				if (actualHeightOut) {
					assert(actualWidth <= UINT_LEAST16_MAX);
					*actualHeightOut = (uint_least16_t)actualHeight;
				}
				if (pixelFormatOut) {
					*pixelFormatOut = CFHD_PIXEL_FORMAT_DPX0;
				}
				if (actualSizeOut) {
					*actualSizeOut = actualSize;
				}
				return CFHD_ERROR_OKAY;
			}
		}
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}

	// Could not parse the sample or generate the thumbnail image
	return CFHD_ERROR_CODEC_ERROR;
}

/*!
	@brief Release the sample buffer

	The application owns the sample buffer returned by a call to
	@ref CFHD_WaitForSample or @ref CFHD_TestForSample and must
	release the sample buffer when the application is done with
	the sample.
*/
CFHDENCODER_API CFHD_Error
CFHD_ReleaseSampleBuffer(CFHD_EncoderPoolRef encoderPoolRef,
						 CFHD_SampleBufferRef sampleBufferRef)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_ReleaseSampleBuffer ref:%04x thread:%d", (0xffff)&(int)encoderPoolRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CEncoderPool *encoderPool = GetEncoderPool(encoderPoolRef);
		CSampleBuffer *sampleBuffer = GetSampleBuffer(sampleBufferRef);
		return encoderPool->ReleaseSampleBuffer(sampleBuffer);
	}
	catch (...)
	{
		return CFHD_ERROR_UNEXPECTED;
	}
}

/*!
	@brief Release the encoder pool

	This routine stops all of the encoders and releases any resources
	that were acquired by the encoder pool.  Note that any encoding
	requests in the queue of submitted requests are allowed to finish
	before the worker threads terminate and the encoder pool is released.

	After the encoder pool is released it is not possible to submit new
	encoding requests or to obtain encoded samples from requests that were
	submitted before the encoder pool was released.
*/
CFHDENCODER_API CFHD_Error
CFHD_ReleaseEncoderPool(CFHD_EncoderPoolRef encoderPoolRef)
{
#ifdef CALL_LOG
	char tt[100];
	sprintf(tt,"CFHD_ReleaseEncoderPool ref:%04x thread:%d", (0xffff)&(int)encoderPoolRef, GetCurrentThreadId()); 
	OutputDebugString(tt);
#endif
	try
	{
		CEncoderPool *encoderPool = GetEncoderPool(encoderPoolRef);
#ifdef _WIN32
		delete encoderPool;  //TODO need find out why this isn't working on Linux. 
#endif
		return CFHD_ERROR_OKAY;
	}
	catch (...)
	{
		printf("CFHD_ReleaseEncoderPool error\n");
		return CFHD_ERROR_UNEXPECTED;
	}
}
