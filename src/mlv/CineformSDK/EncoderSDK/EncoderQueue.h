/*! @file EncoderQueue.h

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

/*! @file EncoderQueue.h

	@brief Declaration of encoder jobs and the job queue for asynchronous encoders

	Each asynchronous encoder has a message queue.  The encoder pool creates an encoder
	job for each encoding request and adds the encoder job to the message queue for the
	asynchronous encoder that is assigned to encode the frame.
*/

// Forward reference
class EncoderJobQueue;

/*!
	@brief Status of an encoder job

	Every encoder job must be in one of three states: the input frame is
	waiting to be assigned to an encoder, the frame has been assigned to
	an encoder, or the frame has been encoded and the encoded sample is
	ready to be delivered.
*/
enum EncoderJobStatus
{
	ENCODER_JOB_STATUS_UNKNOWN = 0,		//!< Encoder job status is not known
	ENCODER_JOB_STATUS_UNASSIGNED,		//!< Job has not been assigned to an encoder
	ENCODER_JOB_STATUS_ENCODING,		//!< Encoding is in progress
	ENCODER_JOB_STATUS_FINISHED,		//!< The encoded sample is ready
};

/*!
	@brief Data structure for an encoder job

	Every encoder job has a status that is one of the valid states
	in the @ref EncoderJobStatus enumeration, the frame buffer and
	pitch of the input frame to encode, and a pointer to the sample
	buffer for the encoded sample.

	@todo Add support for non-key frames.
*/
struct EncoderJob
{
	friend class EncoderJobQueue;
	friend class CEncoderPool;
	friend class CAsyncEncoder;

	EncoderJob() :
		status(ENCODER_JOB_STATUS_UNKNOWN),
		error(CFHD_ERROR_OKAY),
		frameNumber(0),
		frameBuffer(NULL),
		framePitch(0),
		keyFrame(true),
		sampleBuffer(NULL),
		encoderMetadata(NULL)
	{
	}

	EncoderJob(uint32_t frameNumber,
			   void *frameBuffer,
			   ptrdiff_t framePitch,
			   bool keyFrame = true,
			   CSampleEncodeMetadata *encoderMetadata = NULL,
			   CFHD_EncodingQuality frameQuality = CFHD_ENCODING_QUALITY_FIXED) :
		status(ENCODER_JOB_STATUS_UNASSIGNED),
		error(CFHD_ERROR_OKAY),
		frameNumber(frameNumber),
		frameBuffer(frameBuffer),
		framePitch(framePitch),
		frameQuality(frameQuality),
		keyFrame(keyFrame),
		sampleBuffer(NULL),
		encoderMetadata(encoderMetadata)
	{
	}

	EncoderJob(const EncoderJob &job)
	{
		status = job.status;
		error = job.error;
		frameNumber = job.frameNumber;
		frameBuffer = job.frameBuffer;
		framePitch = job.framePitch;;
		frameQuality = job.frameQuality;
		keyFrame = job.keyFrame;
		sampleBuffer = job.sampleBuffer;
		encoderMetadata = job.encoderMetadata;
	}

	EncoderJob& operator= (const EncoderJob &job)
	{
		status = job.status;
		error = job.error;
		frameNumber = job.frameNumber;
		frameBuffer = job.frameBuffer;
		framePitch = job.framePitch;
		frameQuality = job.frameQuality;
		keyFrame = job.keyFrame;
		sampleBuffer = job.sampleBuffer;
		encoderMetadata = job.encoderMetadata;
		return *this;
	}

	~EncoderJob()
	{
		//DebugOutput("Deleting encoder job\n");
		if (sampleBuffer) {
			delete sampleBuffer;
			sampleBuffer = NULL;
		}

		if (encoderMetadata) {
			delete encoderMetadata;
			encoderMetadata = NULL;
		}
	}

	/*!
		@brief Get the sample buffer from the job

		The sample buffer in the encoder job is set to null so that it
		is not deleted when the encoder job is deleted.  After calling
		this method, the sample buffer belongs to the caller and the
		caller is responsible for releasing it.
	*/
	CSampleBuffer *GetSampleBuffer()
	{
		CSampleBuffer *sample = sampleBuffer;
		sampleBuffer = NULL;
		//DebugOutput("Encoder job returning sample: %p\n", sample);
		return sample;
	}

	EncoderJobStatus status;			//!< Status of the encoding job
	CFHD_Error error;					//!< Error code from the sample encoder
	uint32_t frameNumber;				//!< Frame number the identifies the encoding job
	void *frameBuffer;					//!< Address of the frame to encode
	ptrdiff_t framePitch;				//!< Pitch of the frame buffer (in bytes)
	bool keyFrame;						//!< True if this is the first frame in a GOP
	CFHD_EncodingQuality frameQuality;	//!< Compression quality override if non-zero

	//! Metadata that will be attached to the encoded sample for this frame
	CSampleEncodeMetadata *encoderMetadata;

private:

	CSampleBuffer *sampleBuffer;		//!< Buffer that contains the encoded sample

};

/*!
	@brief Use a counting semaphore to track usage of some resource
*/
class CResourceCounter
{
#ifdef _WIN32

public:

	CResourceCounter(long limit) :
		handle(NULL)
	{
		handle = CreateSemaphore(NULL, 0, limit, NULL);
		assert(handle != NULL);
	}

	~CResourceCounter()
	{
		CloseHandle(handle);
		handle = NULL;
	}

	int Wait()
	{
		// Wait for the semaphore indefinitely
		return WaitForSingleObject(handle, INFINITE);
	}

	int Release(LONG amount = 1)
	{
		ReleaseSemaphore(handle, amount, NULL);
		return 0;
	}

private:

	HANDLE handle;

#else

public:

	CResourceCounter(long limit)
	{
		assert(limit > 0);
		//sem_init(&sema, 0, limit);
		pthread_mutex_init(&mutex, NULL);
		pthread_cond_init(&cond, NULL);
		count = limit;
	}

	~CResourceCounter()
	{
		//sem_destroy(&sema);
		pthread_cond_destroy(&cond);
		pthread_mutex_destroy(&mutex);
	}

	int Wait()
	{
		// Wait for the semaphore indefinitely
		//return sem_wait(&sema);
		pthread_mutex_lock(&mutex);
		while (! (count > 0)) {
			pthread_cond_wait(&cond, &mutex);
		}
		count--;
		assert(count >= 0);
		pthread_mutex_unlock(&mutex);
		return 0;
	}

	int Release(size_t amount = 1)
	{
#if 0
		for (; amount > 0; amount--) {
			sem_post(&sema);
		}
#else
		// Increment the resource count
		pthread_mutex_lock(&mutex);
		count += amount;
		pthread_mutex_unlock(&mutex);

		// Signal waiting threads that the count has changed
		pthread_cond_signal(&cond);
#endif
		return 0;
	}

private:

	//sem_t sema;

	pthread_mutex_t mutex;
	pthread_cond_t cond;
	size_t count;

#endif

};

/*!
	@class EncoderJobQueue
*/
class EncoderJobQueue
{
protected:

	typedef std::deque<EncoderJob *> JobQueue;

	static const size_t DEFAULT_QUEUE_LENGTH = 1024;

public:

	EncoderJobQueue(size_t length) :
		available(length)
	{
		// Allocate a job queue of the specified size
		//queue.resize(encoderJobQueueSize);
		assert(length > 0);
		if (! (length > 0)) {
			available = DEFAULT_QUEUE_LENGTH;
		}
	}

	~EncoderJobQueue()
	{
		// Delete all of the jobs in the queue
		while (!queue.empty())
		{
			EncoderJob *job = queue.front();
			queue.pop_front();
			delete job;
		}
	}

	//! Add an encoding job to the end of the queue
	CFHD_Error AddEncoderJob(EncoderJob *job)
	{
		// Available space in the encoder job queue?
		CAutoLock lock(mutex);
		while (available == 0)
		{
			// Wait until there is space in the queue
			space.Wait(mutex);
		}
		assert(available > 0);

		// Add the encoder job to the end of the queue
		queue.push_back(job);

		// Decrease the amount of space in the encoder job queue
		available--;

		return CFHD_ERROR_OKAY;
	}

	EncoderJob *WaitForFinishedJob()
	{
		// Has the next encoding job in the queue finished?
		CAutoLock lock(mutex);
		EncoderJob *job = queue.size() > 0 ? queue.front() : NULL;
		while (job == NULL || job->status != ENCODER_JOB_STATUS_FINISHED)
		{
			// Wait until the next encoding job has finished
			ready.Wait(mutex);
			job = queue.size() > 0 ? queue.front() : NULL;
		}

		// Remove the encoding job from the front of the queue
		queue.pop_front();

		// Increase the amount of space in the encoder job queue
		available++;
		space.Wake();

		// Return the encoder job with the next encoded sample
		return job;
	}

	EncoderJob *TestForFinishedJob()
	{
		// Has the next encoding job in the queue finished?
		CAutoLock lock(mutex);
		EncoderJob *job = queue.size() > 0 ? queue.front() : NULL;
		if (job == NULL || job->status != ENCODER_JOB_STATUS_FINISHED) {
			return NULL;
		}

		// Remove the encoding job from the front of the queue
		queue.pop_front();

		// Increase the amount of space in the encoder job queue
		available++;
		space.Wake();

		// Return the encoder job with the next encoded sample
		return job;
	}

	void SignalJobFinished()
	{
		// Wake a thread that is waiting for an encoded sample
		ready.Wake();
	}

	//! Get the next encoded sample from the job queue
	CFHD_Error GetEncodedSample(uint32_t *frameNumberOut,
								CSampleBuffer **sampleBufferOut)
	{
		CFHD_Error error = CFHD_ERROR_OKAY;

		// Wait for the encoder to finish encoding the next job in the queue
		EncoderJob *job = WaitForFinishedJob();
		if (job == NULL) {
			return CFHD_ERROR_UNEXPECTED;
		}

		if (frameNumberOut != NULL && sampleBufferOut != NULL)
		{
			*frameNumberOut = job->frameNumber;
			*sampleBufferOut = job->sampleBuffer;
			error = CFHD_ERROR_OKAY;
		}
		else
		{
			error = CFHD_ERROR_INVALID_ARGUMENT;
		}

		// Free the encoder job and return
		delete job;
		return error;
	}

private:

	//! Array of encoder jobs in the queue
	JobQueue queue;

	//! Amount of available space in the encoder job queue
	size_t available;

	//! Wait until space is available in the encoder job queue
	ConditionVariable space;

	//! Wait until the next encoder job in the queue has finished
	ConditionVariable ready;

	//! Exclusive access to the encoder job queue
	CSimpleLock mutex;

};

/*!
	@brief Definition of the payload of messages sent to an encoder

	The encoder message contains a pointer to the encoder job that
	specifies the frame to be encoded.
*/

class EncoderMessage : public ThreadMessage
{
public:

	EncoderMessage() :
		ThreadMessage(ThreadMessage::THREAD_COMMAND_NULL),
		encoderJob(NULL)
	{
	}

	EncoderMessage(ThreadMessage::ThreadCommand command) :
		ThreadMessage(command),
		encoderJob(NULL)
	{
	}

	EncoderMessage(EncoderJob *job) :
		ThreadMessage(THREAD_COMMAND_ENCODE),
		encoderJob(job)
	{
	}

	// Must define a copy constructor for this class and the base class
	EncoderMessage(const EncoderMessage& message) :
		ThreadMessage(message),
		encoderJob(message.encoderJob)
	{
	}

	// Must define an assignment operator for this class and the base class
	const EncoderMessage& operator= (const EncoderMessage& message)
	{
		if (&message != this)
		{
			encoderJob = message.encoderJob;
			ThreadMessage::operator=(message);
		}
		return *this;
	}

	EncoderJob *Job()
	{
		return encoderJob;
	}

	void Job(EncoderJob *job)
	{
		encoderJob = job;
	}

private:

	EncoderJob *encoderJob;
};

/*! @class CEncoderMessageQueue

	@brief Each worker thread has its own message queue

	Each worker thread has a unique sample encoder and its own
	message queue.  All encoder jobs in the same GOP are added
	to the message queue for the same encoder so that the
	encoder state is maintained between frames in the GOP.
*/
typedef class MessageQueue<EncoderMessage> CEncoderMessageQueue;
