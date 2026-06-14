/*! @file EncoderPool.cpp

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


#ifndef _WIN32
#include <uuid/uuid.h>
#endif

// Include files from the codec library
#include "encoder.h"
#include "metadata.h"

// Include files for the encoder DLL
#include "Allocator.h"
#include "CFHDError.h"
#include "CFHDTypes.h"
#include "SampleMetadata.h"
#include "VideoBuffers.h"

// Include the declarations for the thread pool
#include "Lock.h"
#include "Condition.h"
#include "ThreadMessage.h"
#include "MessageQueue.h"
#include "ThreadPool.h"

// Include the declarations for the sample encoder and encoder metadata
#include "MetadataWriter.h"
#include "SampleEncoder.h"

// Include the declarations for the asynchronous encoder
#include "EncoderQueue.h"
#include "AsyncEncoder.h"

#include "EncoderPool.h"


CEncoderPool::CEncoderPool(size_t encoderThreadCount,
						   size_t encoderJobQueueSize,
						   CFHD_ALLOCATOR *allocator) :
	error(CFHD_ERROR_OKAY),
	m_encoderList(encoderThreadCount, this, allocator),
	m_encoderJobQueue(encoderJobQueueSize),
	m_encodingStarted(false),
	m_encoderIndex(0),
	m_encoderMetadata(NULL),
	m_timecodeBase(0),
	m_timecodeFrame(-1),
	m_uniqueFrameID(-1),
	m_nextFrameQuality(CFHD_ENCODING_QUALITY_FIXED),
	m_allocator(allocator)
{
}

CEncoderPool::~CEncoderPool()
{
	StopEncoders();

	// The pool of asynchronous encoders will be deallocated automatically

	// The encoder job queue will be deallocated automatically
}

/*!
	@brief Return a list of input formats in decreasing order of preference

	Since all of the asynchronous encoders are identical, the first encoder
	is used to process this request.
*/
CFHD_Error CEncoderPool::GetInputFormats(CFHD_PixelFormat *inputFormatArray,
										 int inputFormatArrayLength,
										 int *actualInputFormatCountOut)
{
	if (m_encodingStarted) {
		return CFHD_ERROR_UNEXPECTED;
	}

	if (m_encoderList.size() == 0) {
		return CFHD_ERROR_UNEXPECTED;
	}

	return m_encoderList[0]->GetInputFormats(inputFormatArray,
											 inputFormatArrayLength,
											 actualInputFormatCountOut);
}

/*!
	@brief Prepare each of the encoders in the pool for encoding

	Once the encoder threads have started to encode samples, it is not
	possible to change the encoding parameters without stopping the
	worker threads or at least waiting until all of the threads are idle.

	The m_encodingStarted flag is used to prevent this method from being
	called more than once.  Since it is not possible to encode samples before
	initializing the encoders, this means that the encoders will be initialized
	once before any samples are encoded and then never initialized again.
*/
CFHD_Error CEncoderPool::PrepareToEncode(uint_least16_t frameWidth,
										 uint_least16_t frameHeight,
										 CFHD_PixelFormat pixelFormat,
										 CFHD_EncodedFormat encodedFormat,
										 CFHD_EncodingFlags encodingFlags,
										 CFHD_EncodingQuality encodingQuality)
{
	//CFHD_Error error = CFHD_ERROR_OKAY;

	if (m_encodingStarted) {
		SetNextFrameQuality(encodingQuality);
		return CFHD_ERROR_OKAY;
	}

	// Initialize each of the encoders
	for (AsyncEncoderList::iterator p = m_encoderList.begin();
		p != m_encoderList.end();
		p++)
	{
		error = (*p)->PrepareToEncode(frameWidth,
									  frameHeight,
									  pixelFormat,
									  encodedFormat,
									  encodingFlags,
									  &encodingQuality);
		if (error != CFHD_ERROR_OKAY) {
			break;
		}
	}
	SetNextFrameQuality(encodingQuality);

	return error;
}

//! Set the license for all of the encoders in the pool
uint32_t CEncoderPool::SetLicense(unsigned char *license)
{
	uint32_t ret = 0;
	//CFHD_Error error = CFHD_ERROR_OKAY;

	if (m_encodingStarted) {
		return 0; // no license, as we should be testing for a license during an encoding
	}

	// Set the license in each of the encoders
	for (AsyncEncoderList::iterator p = m_encoderList.begin();
		p != m_encoderList.end();
		p++)
	{
		ret = (*p)->SetLicense(license);
	}

	return ret;
}

//! Bind a collection of metadata to the encoder pool
CFHD_Error CEncoderPool::AttachMetadata(CSampleEncodeMetadata *encoderMetadata)
{
	// Attach the metadata to all of the encoders in this pool
	m_encoderMetadata = encoderMetadata;

	return CFHD_ERROR_OKAY;
}

//! Start the encoder worker threads
CFHD_Error CEncoderPool::StartEncoders()
{
	if (m_encodingStarted) {
		return CFHD_ERROR_UNEXPECTED;
	}

	// Start the worker thread for each encoder in the pool
	for (AsyncEncoderList::iterator p = m_encoderList.begin();
		p != m_encoderList.end();
		p++)
	{
		CFHD_Error error = CFHD_ERROR_OKAY;
		CAsyncEncoder *encoder = (*p);
		void *param = reinterpret_cast<void *>(encoder);
		error = encoder->Start(param);
		if (error != CFHD_ERROR_OKAY) {
			return error;
		}
	}

	m_encodingStarted = true;

	return CFHD_ERROR_OKAY;
}

//! Start the encoder worker threads
CFHD_Error CEncoderPool::StopEncoders()
{
	if (!m_encodingStarted) {
		return CFHD_ERROR_ENCODING_NOT_STARTED;
	}

	// Send stop messages to all of the asynchronous encoders
	for (AsyncEncoderList::iterator p = m_encoderList.begin();
		p != m_encoderList.end();
		p++)
	{
		(*p)->Stop();
	}

	// Wait for the asynchronous encoders to terminate
	for (AsyncEncoderList::iterator p = m_encoderList.begin();
		p != m_encoderList.end();
		p++)
	{
		(*p)->Wait();
	}

	m_encodingStarted = false;

	return CFHD_ERROR_OKAY;
}

//! Submit a frame for encoding
CFHD_Error CEncoderPool::EncodeSample(uint32_t frameNumber,
									  uint8_t *frameBuffer,
									  ptrdiff_t framePitch,
									  bool keyFrame,
									  CSampleEncodeMetadata *encoderMetadata)
{
	//CFHD_Error error = CFHD_ERROR_OKAY;

	if (!m_encodingStarted) {
		return CFHD_ERROR_ENCODING_NOT_STARTED;
	}

	if (m_encoderList.size() == 0) {
		return CFHD_ERROR_UNEXPECTED;
	}

	// Prepare the metadata to attach to this encoded frame
	CSampleEncodeMetadata *currentMetadata = PrepareMetadata(encoderMetadata);
	if (currentMetadata == NULL) {
		error = CFHD_ERROR_BAD_METADATA;
		return error;
	}

	// Create a new encoder job
	EncoderJob *job = new EncoderJob(frameNumber, frameBuffer, framePitch, keyFrame, currentMetadata, m_nextFrameQuality);
	if (job == NULL) {
		error = CFHD_ERROR_OUTOFMEMORY;
		return error;
	}

	// Add the new job to the end of the encoder job queue
	error = m_encoderJobQueue.AddEncoderJob(job);
	if (error != CFHD_ERROR_OKAY) {
		return error;
	}

	// Assign the new job to an asynchronous encoder
	assert(job->status == ENCODER_JOB_STATUS_UNASSIGNED);

	// Indicate that this job has been assigned to an encoder
	job->status = ENCODER_JOB_STATUS_ENCODING;

	// Select the next encoder
	if (job->keyFrame) {
		// Advance to the next encoder
		m_encoderIndex = (m_encoderIndex + 1) % m_encoderList.size();
	}
	assert(m_encoderIndex < m_encoderList.size());
	//CSampleEncoder &encoder = m_encoderPool[m_encoderIndex];

	// Add the job to the message queue for the asynchronous encoder
	EncoderMessage message(job);
	error = m_encoderList[m_encoderIndex]->SendMessage(message);

	return error;
}

//! Wait until the next encoded sample is ready
CFHD_Error CEncoderPool::WaitForSample(uint32_t *frameNumberOut,
									   CSampleBuffer **sampleBufferOut)
{
	//CFHD_Error error = CFHD_ERROR_OKAY;

	if (!m_encodingStarted) {
		error = CFHD_ERROR_ENCODING_NOT_STARTED;
		return error;
	}

	if (m_encoderList.size() == 0) {
		error = CFHD_ERROR_UNEXPECTED;
		return error;
	}
	// Wait for the next encoding job in the queue to finish
	EncoderJob *job = m_encoderJobQueue.WaitForFinishedJob();
	if (job == NULL) {
		error = CFHD_ERROR_UNEXPECTED;
		return error;
	}
	assert(job->status == ENCODER_JOB_STATUS_FINISHED);

	if (job->error != CFHD_ERROR_OKAY)
	{
		return job->error;
	}

	if (frameNumberOut != NULL && sampleBufferOut != NULL)
	{
		*frameNumberOut = job->frameNumber;
		*sampleBufferOut = job->GetSampleBuffer();
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

//! Test whether the next encoded sample is ready
CFHD_Error CEncoderPool::TestForSample(uint32_t *frameNumberOut,
									   CSampleBuffer **sampleBufferOut)
{
	//CFHD_Error error = CFHD_ERROR_OKAY;

	if (!m_encodingStarted) {
		error = CFHD_ERROR_ENCODING_NOT_STARTED;
		return error;
	}

	if (m_encoderList.size() == 0) {
		error = CFHD_ERROR_UNEXPECTED;
		return error;
	}

	// Get the next encoding job from the queue
	EncoderJob *job = m_encoderJobQueue.TestForFinishedJob();
	if (job == NULL) {
		error = CFHD_ERROR_NOT_FINISHED;
		return error;
	}
	assert(job->status == ENCODER_JOB_STATUS_FINISHED);

	if (frameNumberOut != NULL && sampleBufferOut != NULL)
	{
		*frameNumberOut = job->frameNumber;
		*sampleBufferOut = job->GetSampleBuffer();
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

//! Release a sample buffer that contained an encoded sample
CFHD_Error CEncoderPool::ReleaseSampleBuffer(CSampleBuffer *sampleBuffer)
{
	delete sampleBuffer;
	return CFHD_ERROR_OKAY;
}

//! Prepare the metadata required by each encoded frame
CSampleEncodeMetadata *CEncoderPool::PrepareMetadata(CSampleEncodeMetadata *encoderMetadata)
{
	// New metadata for this encoder pool?
	if (encoderMetadata) {
		// Attach the new metadata for the current and future encoded frames
		AttachMetadata(encoderMetadata);
	}

	// Has metadata been attached to this encoder pool?
	if (m_encoderMetadata == NULL)
	{
		// Create a new metadata container for this encoder pool
		m_encoderMetadata = new CSampleEncodeMetadata;
		if (m_encoderMetadata == NULL) {
			error = CFHD_ERROR_OUTOFMEMORY;
			return NULL;
		}
	}

	assert(m_encoderMetadata);
	if (! (m_encoderMetadata)) {
		error = CFHD_ERROR_UNEXPECTED;
		return NULL;
	}

	// Add the GUID and frame ID to the encoder metadata
	error = UpdateMetadata();
	if (error != CFHD_ERROR_OKAY) {
		return NULL;
	}

	// Copy the current metadata for encoding the next frame
	encoderMetadata = new CSampleEncodeMetadata(m_encoderMetadata);
	if (encoderMetadata == NULL) {
		error = CFHD_ERROR_OUTOFMEMORY;
	}

	// Return the new encoder sample metadata
	return encoderMetadata;
}

/*!
	@brief Update the metadata for the next encoded frame

	The metadata that is unique to each frame such as the timecode
	and unique frame ID must be added to the metadata in the same order
	as the frames are input to the encoder pool.  Frames may be encoded
	concurrently and it is not possible to known the order in which each
	frame is encoded.Each frame may be encoded
*/
CFHD_Error CEncoderPool::UpdateMetadata()
{
	// Update the metadata attached to the encoder pool
	CSampleEncodeMetadata *metadata = m_encoderMetadata;
	if (metadata == NULL) {
		error = CFHD_ERROR_UNEXPECTED;
		return error;
	}

	// Check the GUID and update the timecode and unique frame ID
	void *data;
	METADATA_SIZE retsize;
	METADATA_TYPE rettype;
	bool in_global = false;
	bool in_local = false;

	// Is the GUID set in the global metadata?
	if (!(data = MetadataFind(metadata->global[0].block, metadata->global[0].size,
							  TAG_CLIP_GUID, &retsize, &rettype)))
	{
		// Add the GUID to the global metadata
		metadata->AddGUID();
	}

	// Get the current time
	time_t clock = time(NULL);
	struct tm *time = localtime(&clock);
	char date_string[16];
	char time_string[16];
	char timecode[16];

	// Format the date and time strings
#ifdef _WIN32
	sprintf_s(date_string, sizeof(date_string), "%04d-%02d-%02d", time->tm_year + 1900, time->tm_mon + 1, time->tm_mday);
	sprintf_s(time_string, sizeof(time_string), "%02d:%02d:%02d", time->tm_hour, time->tm_min, time->tm_sec);
#else
	sprintf(date_string, "%04d-%02d-%02d", time->tm_year + 1900, time->tm_mon + 1, time->tm_mday);
	sprintf(time_string, "%02d:%02d:%02d", time->tm_hour, time->tm_min, time->tm_sec);
#endif

	// Use the date and time strings as the encoder time stamp
	metadata->AddTimeStamp(date_string, time_string);

	// Is the timecode set in the global metadata?
	if (!(data = MetadataFind(metadata->global[0].block, metadata->global[0].size,
							  TAG_TIMECODE, &retsize, &rettype)))
	{
		// Is the timecode set in the local metadata?
		if (!(data = MetadataFind(metadata->local.block, metadata->local.size,
								  TAG_TIMECODE, &retsize, &rettype)))
		{
			// Generate the timecode metadata from the local time
			m_timecodeBase = 24;
			m_timecodeFrame = (((time->tm_hour * 60 + time->tm_min) * 60) + time->tm_sec) * m_timecodeBase;
#ifdef _WIN32
			sprintf_s(timecode, sizeof(timecode), "%02d:%02d:%02d:00", time->tm_hour, time->tm_min, time->tm_sec);
#else
			sprintf(timecode, "%02d:%02d:%02d:00", time->tm_hour, time->tm_min, time->tm_sec);
#endif

			metadata->AddTimeCode(timecode);
		}
		else
		{
			in_local = true;
		}
	}
	else
	{
		in_global = true;
	}

	// Was timecode found in the metadata?
	if (data)
	{
		// Parse the timecode string
		char *tc = (char *)data;
		int hours   = (tc[0]-'0') * 10 + (tc[1]-'0');
		int minutes = (tc[3]-'0') * 10 + (tc[4]-'0');
		int seconds = (tc[6]-'0') * 10 + (tc[7]-'0');
		int frames  = (tc[9]-'0') * 10 + (tc[10]-'0');

		if (m_timecodeBase == 0)
		{
			// Look for the timebase in the local metadata
			if (!(data = MetadataFind(metadata->local.block, (int)metadata->local.size,
									  TAG_TIMECODE_BASE, &retsize, &rettype)))
			{
				// Look for the timebase in the global metadata
				if (!(data = MetadataFind(metadata->global[0].block, metadata->global[0].size,
										  TAG_TIMECODE_BASE, &retsize, &rettype)))
				{
					// Assume that the timebase is 24 frames per second
					m_timecodeBase = 24;
				}
			}

			// Found the timebase in the metadata?
			if (data)
			{
				m_timecodeBase = *(uint8_t *)data;

				if (m_timecodeBase == 0) {
					m_timecodeBase = 24;
				}
			}
		}

		int32_t frame_number = ((hours * 60 + minutes) * 60 + seconds) * m_timecodeBase + frames;

		if (m_timecodeFrame == -1)
		{
			m_timecodeFrame = frame_number;
		}
		else if (frame_number == m_timecodeFrame && m_timecodeBase <= 30)
		{
			// Compute the timecode from the previous frame number
			m_timecodeFrame++;
			frame_number = m_timecodeFrame;

			frames = frame_number % m_timecodeBase;		frame_number /= m_timecodeBase;
			seconds = frame_number % 60;				frame_number /= 60;
			minutes = frame_number % 60;				frame_number /= 60;
			hours = frame_number % 60;					frame_number /= 60;

#ifdef _WIN32
			sprintf_s(timecode, sizeof(timecode), "%02d:%02d:%02d:%02d", hours, minutes, seconds, frames);
#else
			sprintf(timecode, "%02d:%02d:%02d:%02d", hours, minutes, seconds, frames);
#endif

			metadata->AddTimeCode(timecode, in_local);
		}
	}

	// Add metadata for the unique frame number
	in_local = in_global = false;

	// Look for the unique frame number in the global metadata
	if (!(data = MetadataFind(metadata->global[0].block, metadata->global[0].size,
							  TAG_UNIQUE_FRAMENUM, &retsize, &rettype)))
	{
		// Look for the unique frame number in the local metadata
		if (!(data = MetadataFind(metadata->local.block, (int)metadata->local.size,
								 TAG_UNIQUE_FRAMENUM, &retsize, &rettype)))
		{
			// Generate a unique frame number
			m_uniqueFrameID = 0;

			// Add the frame number to the global metadata
			metadata->AddFrameNumber(m_uniqueFrameID);
		}
		else
		{
			in_local = true;
		}
	}
	else
	{
		in_global = true;
	}

	// Was the unique frame number found in the metadata?
	if (data)
	{
		// Get the unique frame ID from the metadata
		int32_t uniqueFrameID = *(unsigned long *)data;

		// Has the unique frame ID for the encoder pool been initialized?
		if (m_uniqueFrameID == -1)
		{
			// Initialize the encoder pool unique frame ID using the metadata
			m_uniqueFrameID = uniqueFrameID;
		}
		else if (uniqueFrameID <= m_uniqueFrameID)
		{
			// Use the next encoder pool unique frame ID for this frame
			m_uniqueFrameID++;
			metadata->AddFrameNumber(m_uniqueFrameID, in_local);
		}
	}

	return CFHD_ERROR_OKAY;
}
