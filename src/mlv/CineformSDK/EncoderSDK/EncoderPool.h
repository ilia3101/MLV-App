/*! @file EncoderPool.h

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

/*!
	@brief List of asynchronous encoders managed by the encoder pool
*/
class AsyncEncoderList : public std::vector<CAsyncEncoder *>
{
public:

	AsyncEncoderList(size_t length, CEncoderPool *pool, CFHD_ALLOCATOR *allocator = NULL)
	{
		for (size_t index = 0; index < length; index++)
		{
			CAsyncEncoder *encoder = new CAsyncEncoder(pool, allocator);
			assert(encoder);
			if (encoder) {
				push_back(encoder);
			}
		}
	}

	~AsyncEncoderList()
	{
		// Delete all of the asynchronous encoders in the list
		for (iterator p = begin(); p != end(); p++)
		{
			delete *p;
		}		
	}
};

/*! @class CEncoderPool

	@brief Manager of a pool of asynchronous encoders

	This class manages a pool of asynchronous encoders and the queue
	of encoding jobs.  Each asynchronous encoder has its own worker
	thread and a message queue for sending commands and encoding jobs
	to the asynchronous encoder.  The asynchronous encoder receives
	encoding jobs from its message queue and uses a sample encoder to
	encode the frame specified in the encoding job.  The encoded sample
	and an error code is written into the encoder job.

	The queue of encoding jobs is used to track every request to encode
	a frame and the resulting sample.  Encoding jobs are kept in the order
	in which frames are received.  All of the encoding jobs in a GOP are
	sent to the same asynchronous encoder.  The encoder pool sends each
	GOP to an asynchronous encoder in round robin order.

	The encoder pool handles requests for the next encoded sample.  If the
	oldest encoding job in the queue has been encoded, the encoded sample
	is returned.  Otherwise, the caller is blocked until the next sample
	is ready.  Encoded samples are always returned to the caller in the same
	order as the frames are submitted to the encoder pool.
*/
class CEncoderPool
{
protected:

	//typedef std::vector<CAsyncEncoder> EncoderPool;
	//typedef EncoderPool::iterator EncoderPoolIterator;

public:

	CEncoderPool(size_t encoderThreadCount,
				 size_t encoderJobQueueSize,
				 CFHD_ALLOCATOR *allocator = NULL);

	~CEncoderPool();

	//! Return a list of input formats in decreasing order of preference
	CFHD_Error GetInputFormats(CFHD_PixelFormat *inputFormatArray,
							   int inputFormatArrayLength,
							   int *actualInputFormatCountOut);

	//! Prepare each of the encoders in the pool for encoding
	CFHD_Error PrepareToEncode(uint_least16_t frameWidth,
							   uint_least16_t frameHeight,
							   CFHD_PixelFormat pixelFormat,
							   CFHD_EncodedFormat encodedFormat,
							   CFHD_EncodingFlags encodingFlags,
							   CFHD_EncodingQuality encodingQuality);

	//! Set the license for all of the encoders in the pool
	uint32_t SetLicense(unsigned char *license);

	//! Bind a collection of metadata to the encoder pool
	CFHD_Error AttachMetadata(CSampleEncodeMetadata *encoderMetadata);

	//! Start the asynchronous encoder worker threads
	CFHD_Error StartEncoders();

	//! Stop the asynchronous encoder worker threads
	CFHD_Error StopEncoders();

	//! Submit a frame for encoding
	CFHD_Error EncodeSample(uint32_t frameNumber,
							uint8_t *frameBuffer,
							ptrdiff_t framePitch,
							bool keyFrame = true,
							CSampleEncodeMetadata *encoderMetadata = NULL);

	//! Wait until the next encoded sample is ready
	CFHD_Error WaitForSample(uint32_t *frameNumberOut,
							 CSampleBuffer **sampleBufferOut);

	//! Test whether the next encoded sample is ready
	CFHD_Error TestForSample(uint32_t *frameNumberOut,
							 CSampleBuffer **sampleBufferOut);

	//! Signal that an encoder job has finished
	CFHD_Error SignalJobFinished()
	{
		m_encoderJobQueue.SignalJobFinished();
		return CFHD_ERROR_OKAY;
	}

	//! Release the sample buffer
	CFHD_Error ReleaseSampleBuffer(CSampleBuffer *sampleBuffer);

	CFHD_Error SetNextFrameQuality(CFHD_EncodingQuality nextFrameQuality)
	{
		m_nextFrameQuality = nextFrameQuality;
		return CFHD_ERROR_OKAY;
	}
	
	CFHD_Error GetAllocator(CFHD_ALLOCATOR ** allocator)
	{
		*allocator = m_allocator;
		return CFHD_ERROR_OKAY;
	}

protected:

	//! Prepare the metadata for encoding the next frame
	CSampleEncodeMetadata *PrepareMetadata(CSampleEncodeMetadata *encoderMetadata = NULL);

	//! Add the frame metadata requried by every encoded sample
	CFHD_Error UpdateMetadata();

private:

	//! Most recent error encountered by the encoder pool
	CFHD_Error error;

	//! Queue of input frames and encoded samples in decode order
	EncoderJobQueue m_encoderJobQueue;

	//! Pool of asynchronous encoders that can encode samples concurrently
	//std::vector<CAsyncEncoder> m_encoderPool;
	//EncoderList m_encoderPool;
	AsyncEncoderList m_encoderList;

	//! True if the worker threads in the asynchronous encoders are running
	bool m_encodingStarted;

	//! Index of the next asynchronous encoder in the pool for assigning jobs
	size_t m_encoderIndex;

	//! Metadata attached to this encoder pool
	CSampleEncodeMetadata *m_encoderMetadata;

	//! Timebase for converting timecode to frame number
	int m_timecodeBase;

	//! Frame number corresponding to the timecode of the previous frame
	int32_t m_timecodeFrame;

	//! Unique frame number for each encoded sample
	int32_t m_uniqueFrameID;

	// To change the quality of encoding on the fly.
	CFHD_EncodingQuality m_nextFrameQuality;

	CFHD_ALLOCATOR *m_allocator;
};
