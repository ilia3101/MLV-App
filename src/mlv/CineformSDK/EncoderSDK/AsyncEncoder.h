/*! @file AsyncEncoder.h

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


// Forward reference to the encoder pool
class CEncoderPool;


/*!
	@brief Asynchronous encoder with a message queue of encoder jobs

	Each asynchronous encoder is associated with a worker thread that allows
	frames to be encoded asynchronously.  The asynchronous encoder extends the
	sample encoder with a message queue that contains encoding jobs assigned to
	the encoder and control messages that start and stop the work thread.

	The message queue allows one or more encoding jobs to be assigned to the
	asynchronous encoder without waiting for the encoder to finish the current
	encoding job.
*/
class CAsyncEncoder : public CSampleEncoder
{
public:

	CAsyncEncoder(CEncoderPool *encoderPool, CFHD_ALLOCATOR *allocator) :
		pool(encoderPool),
		CSampleEncoder(allocator)
	{
	}

	~CAsyncEncoder()
	{
		// The message queue should be empty
		//assert(m_encoderMessageQueue.size() == 0);
	}

	//! Start the worker thread for this asynchronous encoder
	CFHD_Error Start(void *param)
	{
		return thread.Start(WorkerThreadProc, param);
	}

	//! Stop the worker thread associated with the asynchronous encoder
	CFHD_Error Stop()
	{
		EncoderMessage message(ThreadMessage::THREAD_COMMAND_STOP);
		return queue.AddMessage(message);
	}

	//! Wait for the worker thread to terminate
	CFHD_Error Wait()
	{
		return thread.Wait();
	}

	//! Post an encoder message to the queue for this asynchronous encoder
	CFHD_Error SendMessage(EncoderMessage &message)
	{
		return queue.AddMessage(message);
	}

protected:

	//! Procedure executed by the worker thread for this asynchronous encoder
	static CThread::ThreadReturnType STDCALL WorkerThreadProc(void *param);

	//! Process messages sent to this asynchronous encoder
	CFHD_Error MessageLoop();

	//! Attach the metadata for encoding the next frame
	//CFHD_Error HandleMetadata(CSampleEncodeMetadata *encoderMetadata);

	//! Use the metadata for encoding the next frame
	CFHD_Error ApplyMetadata(CSampleEncodeMetadata *metadata);

	CFHD_Error EncodeSample(EncoderJob *job)
	{
		if (job == NULL) {
			return CFHD_ERROR_UNEXPECTED;
		}
		assert(job->framePitch <= INT_MAX);
		return EncodeSample(job->frameBuffer, (int)job->framePitch, job->keyFrame, job->encoderMetadata, job->frameQuality);
	}

	//! Encode the frame after attaching the metadata to the encoder
	CFHD_Error EncodeSample(void *frameBuffer,
							int framePitch,
							bool keyFrame,
							CSampleEncodeMetadata *encoderMetadata,
							CFHD_EncodingQuality frameQuality = CFHD_ENCODING_QUALITY_FIXED);

private:

	//! Encoder pool that manages this asynchronous encoder
	CEncoderPool *pool;

	//! Queue of control messages and encoding requests
	CEncoderMessageQueue queue;

	//! Worker thread for this asynchronous encoder
	CThread thread;
};
