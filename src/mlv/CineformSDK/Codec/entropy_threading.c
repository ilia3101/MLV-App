/*! @file 

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

#include "config.h"
#include "timing.h"
#include "decoder.h"
#include <math.h>

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif
#ifndef TIMING
#define TIMING (1 && _TIMING)
#endif
#ifndef XMMOPT
#define XMMOPT (1 && _XMMOPT)
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <assert.h>
#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif

#include "config.h"
#include "dump.h"
#include "decoder.h"
#include "codec.h"
#include "vlc.h"
#include "codebooks.h"			// References to the codebooks
#include "debug.h"
#include "color.h"				// Color formats supported by image processing routines
#include "image.h"
#include "filter.h"
#include "spatial.h"
#include "temporal.h"
//#include "logo40x5.h"
#include "convert.h"
#include "wavelet.h"
#include "bitstream.h"
#include "frame.h"
#include "cpuid.h"
#include "exception.h"

#ifndef DUMP
#define DUMP (0 && _DUMP)
#endif

#define ERROR_TOLERANT			1

#if (DEBUG && _WIN32)
#include <tchar.h>				// For printing debug string in the console window
#endif

#define _DECODE_TRANSFORM		1	// Enable concurrent decoding and inverse transform
#define _TRANSFORM_FIELDPLUS	1	// Use the field plus transform

#if _SIF							// In SIF resolution, enable the _DECODE_TRANSFORM switch
#if _DECODE_TRANSFORM == 0
#define _DECODE_TRANSFORM		1
#endif
#endif

#ifndef _FSMBUFFER
#define _FSMBUFFER	0
#endif

// Turn off saturation in this file
#ifdef SATURATE
#undef SATURATE
#endif
#define SATURATE(x)			(assert(PIXEL_MIN <= (x) && (x) <= PIXEL_MAX), (x))
#define SATURATE8S(x)		(assert(PIXEL8S_MIN <= (x) && (x) <= PIXEL8S_MAX), (x))
//#define SATURATE8S(x)		SATURATE_8S(x)
//#define SATURATE(x) (x)

// Enable or disable function inlining
#if 1	//DEBUG
#define inline
#else
#define inline __forceinline
#endif

// Pixel size used for computing the compression ratio
#define BITS_PER_PIXEL 8

// Default processor capabilities
#define DEFAULT_FEATURES (_CPU_FEATURE_MMX )



// Macros for the finite state machine decoder
#if _INDIVIDUAL_LUT

#define GetFSMTableEntry(fsm, index)	(FSMENTRY *)fsm->next_state+index
#define ResetFSM(fsm)					fsm->next_state = fsm->table.entries[0]
#define UpdateFSM(fsm, next)			fsm->next_state = fsm->table.entries[next]

#define GetFSMTableEntryIndividual(fsm, index)	(FSMENTRY *)fsm->table.entries_ind[(fsm->next_state_index << FSM_INDEX_SIZE) | index]
#define ResetFSMIndividual(fsm)					fsm->next_state_index = 0
#define UpdateFSMIndividual(fsm, next)			fsm->next_state_index = next

#else

#define GetFSMTableEntry(fsm, index)	(FSMENTRY *)fsm->next_state+index
#define ResetFSM(fsm)					fsm->next_state = fsm->table.entries
#define UpdateFSM(fsm, next)			fsm->next_state = fsm->table.entries+((int)next << FSM_INDEX_SIZE)

#endif

bool DecodeBandFSM16sNoGapWithPeaks(FSM *fsm, BITSTREAM *stream, PIXEL16S *image, int width, int height, int pitch, PIXEL *peaks, int level, int quant);
bool DecodeBandFSM16sNoGap(FSM *fsm, BITSTREAM *stream, PIXEL16S *image, int width, int height, int pitch);




#if _THREADED

void DecodeEntropy(DECODER *decoder, int work_index, int thread_index, FSM *fsm, int *initFsm)
{
	struct entropy_data_new *data;
	BITSTREAM *stream;
	PIXEL *rowptr;
	int width;
	int height;
	int pitch;
	PIXEL *peaks;
	int level;
	int quant;
	IMAGE *wavelet;
	int band_index;
	int active_codebook;
	int difference_coding;
	int result = true;
	int skip = 0;

	data = &decoder->entropy_worker_new.entropy_data[work_index];

	// Get the processing parameters
	active_codebook = data->active_codebook;
	difference_coding = data->difference_coding;

	stream =  &data->stream;
	rowptr = data->rowptr;
	width = data->width;
	height = data->height;
	pitch = data->pitch;
	peaks = data->peaks;
	level = data->level;
	quant = data->quant;
	wavelet = data->wavelet;
	band_index = data->band_index;

	if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
	{
		if(wavelet->level == 1 && (band_index == 1 || band_index == 3))
			skip = 1;
	}

	if(!skip)
	{
		if(*initFsm != active_codebook)
		{
			*initFsm = active_codebook;
			memcpy(fsm, &decoder->fsm[active_codebook], sizeof(FSM));
		}

		// Unlock access to the transform data
		//Unlock(&decoder->entropy_worker_new.lock);

		DeQuantFSM(fsm, quant);

		//Do stuff
		if(level)
		{
			result = DecodeBandFSM16sNoGapWithPeaks(fsm, stream, (PIXEL16S *)rowptr,
				width, height, pitch, peaks, level, 1);
		}
		else
		{
			result = DecodeBandFSM16sNoGap(fsm, stream, (PIXEL16S *)rowptr, width, height, pitch);
		}


		if(difference_coding)
		{
			int x,y;
			PIXEL *line = rowptr;

			for(y=0;y<height;y++)
			{
				for(x=1;x<width;x++)
				{
					line[x] += line[x-1];
				}
				line += pitch/2;
			}
		}
	}

	if (result)
	{
		// Call thread safe routine to update the band valid flags
		UpdateWaveletBandValidFlags(decoder, wavelet, band_index);

		{
			int num_entries;
			//int work_index = -1;
			struct transform_queue *data;
			data = &decoder->transform_queue;

			{
				int curr_entry;
				TRANSFORM *transform;
				IMAGE *wavelet;
				int channel;
				int index;
				int precision;
				//int32_t lPreviousCount;

				Lock(&decoder->entropy_worker_new.lock);
				num_entries = data->num_entries;
				Unlock(&decoder->entropy_worker_new.lock);

				if(num_entries>0)
				{
					do
					{
						Lock(&decoder->entropy_worker_new.lock);
						// Get the transform parameters
						for(curr_entry=0; curr_entry<data->free_entry; curr_entry++)
						{
							assert(0 <= curr_entry && curr_entry < DECODING_QUEUE_LENGTH);

							transform = data->queue[curr_entry].transform;
							assert(transform != NULL);

							channel = data->queue[curr_entry].channel;
							assert(0 <= channel && channel < TRANSFORM_MAX_CHANNELS);

							index = data->queue[curr_entry].index;
							assert(0 <= index && index < TRANSFORM_MAX_WAVELETS);

							wavelet = transform->wavelet[index];
							assert(wavelet != NULL);

							precision = data->queue[curr_entry].precision;

							if(data->queue[curr_entry].done == 0 && BANDS_ALL_VALID(wavelet))
							{
								data->queue[curr_entry].done = 1;
								data->next_entry++;
								data->num_entries--;
								break;
							}
							else
							{
								wavelet = NULL;
							}
						}

						// Unlock access to the transform queue
						Unlock(&decoder->entropy_worker_new.lock);

						if(wavelet && BANDS_ALL_VALID(wavelet))
						{
							SCRATCH local;
						//	int localsize;

							InitScratchBuffer(&local, decoder->threads_buffer[thread_index],
								decoder->threads_buffer_size);

						/*	PushScratchBuffer(&local, &decoder->scratch);

							localsize = (local.free_size/decoder->entropy_worker_new.pool.thread_count) & ~15;

							local.base_ptr += localsize*thread_index;
							local.free_ptr += localsize*thread_index;
							local.free_size = localsize; */

							// Apply the inverse wavelet transform to reconstruct the lower level wavelet
							ReconstructWaveletBand(decoder, transform, channel, wavelet, index, precision,
											&local, 0);
						}
					} while(wavelet && BANDS_ALL_VALID(wavelet));
				}
			}
		}
	}
}


THREAD_PROC(EntropyWorkerThreadProc, lpParam)
{
	DECODER *decoder = (DECODER *)lpParam;
#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;
	int initFsm = -1;
	FSM fsm;

	if (decoder->thread_cntrl.affinity)
	{
#ifdef _WIN32
		HANDLE hCurrentThread = GetCurrentThread();
		SetThreadAffinityMask(hCurrentThread, decoder->thread_cntrl.affinity);
#else
		pthread_t thread = pthread_self();
		uint32_t thread_affinity = decoder->thread_cntrl.affinity;
		SetThreadAffinityMask(thread, &thread_affinity);
#endif
	}

	// Set the handler for system exceptions
	SetDefaultExceptionHandler();

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&decoder->entropy_worker_new.pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	for (;;)
	{
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
        error = PoolThreadWaitForMessage(&decoder->entropy_worker_new.pool, thread_index, &message);

		// Received a signal to begin?
		if(error == THREAD_ERROR_OKAY &&
			(message == THREAD_MESSAGE_START || message == THREAD_MESSAGE_MORE_WORK))
		{

			for(;;)
			{
				int work_index = -1;

				error = PoolThreadWaitForWork(&decoder->entropy_worker_new.pool, &work_index, thread_index);

				// Is there another band to process?
				if (error == THREAD_ERROR_OKAY)
				{
					DecodeEntropy(decoder, work_index, thread_index, &fsm, &initFsm);
				}
				else if(error == THREAD_ERROR_NOWORK)
				{
					PoolThreadSignalDone(&decoder->entropy_worker_new.pool, thread_index);
					break;
				}
			}
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else if (error != THREAD_ERROR_OKAY)
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}


	//OutputDebugString("thread end");

	return (THREAD_RETURN_TYPE)error;
}



THREAD_PROC(ParallelThreadProc, lpParam)
{
	DECODER *decoder = (DECODER *)lpParam;
#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	if(decoder->thread_cntrl.affinity)
	{
#ifdef _WIN32
		HANDLE hCurrentThread = GetCurrentThread();
		SetThreadAffinityMask(hCurrentThread,decoder->thread_cntrl.affinity);
#else
		pthread_t thread = pthread_self();
		uint32_t thread_affinity = decoder->thread_cntrl.affinity;
		SetThreadAffinityMask(thread, &thread_affinity);
#endif
	}

	// Set the handler for system exceptions
	SetDefaultExceptionHandler();

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&decoder->decoder_thread.pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	for (;;)
	{
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
        error = PoolThreadWaitForMessage(&decoder->decoder_thread.pool, thread_index, &message);

		// Received a signal to begin?
		if(error == THREAD_ERROR_OKAY &&
			(message == THREAD_MESSAGE_START || message == THREAD_MESSAGE_MORE_WORK))
		{

			for(;;)
			{
				int work_index = -1;

				error = PoolThreadWaitForWork(&decoder->decoder_thread.pool, &work_index, thread_index);

				// Is there another band to process?
				if (error == THREAD_ERROR_OKAY)
				{
					bool result = true;
					BITSTREAM *input;
					uint8_t *output;
					int pitch;
					ColorParam *colorparams;
					// do the second channel
					TAGVALUE segment;
					int sample_type;


					input = decoder->decoder_thread.input;
					output = decoder->decoder_thread.output;
					pitch = decoder->decoder_thread.pitch;
					colorparams = decoder->decoder_thread.colorparams;

					decoder->entropy_worker_new.next_queue_num = 0;
					decoder->entropy_worker_new.threads_used = 0;

					// Get the type of sample
					segment = GetTagValue(input);
					assert(segment.tuple.tag == CODEC_TAG_SAMPLE);
					if (!IsValidSegment(input, segment, CODEC_TAG_SAMPLE)) {
						decoder->error = CODEC_ERROR_BITSTREAM;
					}

					if(decoder->error == CODEC_ERROR_OKAY)
					{
						sample_type = segment.tuple.value;

						switch (sample_type)
						{
						case SAMPLE_TYPE_GROUP:		// Group of frames (decode the first frame)
							result = DecodeSampleGroup(decoder, input, output, pitch, colorparams);
							break;

						case SAMPLE_TYPE_FRAME:		// Decode the second or later frame in a group
							result = DecodeSampleFrame(decoder, input, output, pitch, colorparams);
							break;

						case SAMPLE_TYPE_IFRAME:	// Decode a sample that represents an isolated frame
							result = DecodeSampleIntraFrame(decoder, input, output, pitch, colorparams);
							break;

						case SAMPLE_TYPE_SEQUENCE_HEADER:
							// The video sequence header is ignored
							result = true;
							break;

						default:
							// Need to fill the output frame
							//error = CODEC_ERROR_SAMPLE_TYPE;
							result = false;
						}
					}
				}
				else if(error == THREAD_ERROR_NOWORK)
				{
					PoolThreadSignalDone(&decoder->decoder_thread.pool, thread_index);
					break;
				}
			}
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else if (error != THREAD_ERROR_OKAY)
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}

	return (THREAD_RETURN_TYPE)error;
}


#endif
