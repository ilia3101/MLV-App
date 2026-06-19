/*! @file AsyncEncoder.cpp

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

// Include files from the codec library
#include "encoder.h"

// Include files for the encoder DLL
#include "Allocator.h"
#include "CFHDError.h"
#include "CFHDTypes.h"
#include "SampleMetadata.h"
#include "VideoBuffers.h"
#include "SampleEncoder.h"

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

// Include the declarations for the encoder pool
#include "EncoderPool.h"


CThread::ThreadReturnType STDCALL CAsyncEncoder::WorkerThreadProc(void *param)
{
	//CFHD_Error error = CFHD_ERROR_OKAY;

	// The thread parameter is the asynchronous encoder for this worker thread
	CAsyncEncoder *encoder = reinterpret_cast<CAsyncEncoder*>(param);
	assert(encoder != NULL);
	if (! (encoder != NULL)) {
		return (CThread::ThreadReturnType)CFHD_ERROR_UNEXPECTED;
	}

	// Process messages that are sent to this asynchronous encoder
	return (CThread::ThreadReturnType)encoder->MessageLoop();
}

CFHD_Error CAsyncEncoder::MessageLoop()
{
	CFHD_Error error = CFHD_ERROR_OKAY;

	for (;;)
	{
		EncoderMessage message;
		EncoderJob *job = NULL;

		error = queue.WaitForMessage(message);
		if (error != CFHD_ERROR_OKAY) {
			return error;
		}

		switch (message.Command())
		{
		case ThreadMessage::THREAD_COMMAND_NULL:
		case ThreadMessage::THREAD_COMMAND_START:
			// Ignore this message
			break;

		case ThreadMessage::THREAD_COMMAND_STOP:
			// Terminate this thread
			return CFHD_ERROR_OKAY;
			break;

		case ThreadMessage::THREAD_COMMAND_ENCODE:
			job = message.Job();
			assert(job != NULL);
			error = EncodeSample(job);
			job->error = error;
			if (error == CFHD_ERROR_OKAY)
			{
				// Record the encoded sample in the job
				CSampleBuffer *sampleBuffer = NULL;
				error = GetSampleBuffer(&sampleBuffer);
				if (error != CFHD_ERROR_OKAY) {
					return error;
				}
				job->sampleBuffer = sampleBuffer;
			}

			// Done encoding the frame
			job->status = ENCODER_JOB_STATUS_FINISHED;

			// Signal that the encoder job has finished
			pool->SignalJobFinished();
		}
	}
}

#if 0
CFHD_Error CAsyncEncoder::HandleMetadata(CSampleEncodeMetadata *metadata)
{
	CFHD_Error error = CFHD_ERROR_OKAY;

	// Merge the global and local metadata into the metadata stored in this encoder
	error = MergeMetadata(metadata->m_metadataGlobal, metadata->m_metadataGlobalSize,
						  metadata->m_metadataLocal, metadata->m_metadataLocalSize);
	if (error != CFHD_ERROR_OKAY) {
		return error;
	}

	// Tell the sample encoder to use the metadata
	return CSampleEncoder::HandleMetadata();
}
#endif

CFHD_Error CAsyncEncoder::ApplyMetadata(CSampleEncodeMetadata *metadata)
{
	return CSampleEncoder::ApplyMetadata(&metadata->global[0], &metadata->local);
}

//! Encode the frame after attaching the metadata to the encoder
CFHD_Error CAsyncEncoder::EncodeSample(void *frameBuffer,
									   int framePitch,
									   bool keyFrame,
									   CSampleEncodeMetadata *encoderMetadata,
									   CFHD_EncodingQuality frameQuality)
{
	CFHD_Error error = CFHD_ERROR_OKAY;
	
	// Use the metadata from the encoder job for encoding this frame
	//error = HandleMetadata(encoderMetadata);
	error = ApplyMetadata(encoderMetadata);
	if (error != CFHD_ERROR_OKAY) {
		return error;
	}

	// Encode the frame
	error = CSampleEncoder::EncodeSample(frameBuffer, framePitch, frameQuality);

	// Free the local metadata even if the encoder returned an error
	CFHD_Error error_free = FreeLocalMetadata();

	// Return the error code from the encoder or freeing the metadata
	return (error != CFHD_ERROR_OKAY) ? error : error_free;
}
