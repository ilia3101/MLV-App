/*! @file 

*  @brief 
*
*  @version 1.0.0
*
*  (C) Copyright 2017 GoPro Inc (http://gopro.com/).
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
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
#if WARPSTUFF
#include "WarpLib.h"
#endif
//#include <stdlib.h>
#include <stddef.h>
#include <math.h>
#include <memory.h>
#include <time.h>
//#include <stdint.h>

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif
#ifndef TIMING
#define TIMING (1 && _TIMING)
#endif
#ifndef XMMOPT
#define XMMOPT (1 && _XMMOPT)
#endif

#define GEN_LICENSE 0

#ifndef PI
#define PI 3.14159265359f
#endif

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include "../Common/macdefs.h"
#else
#ifndef ZeroMemory
#define ZeroMemory(p,s)		memset(p,0,s)
#endif
#endif

#include <stdio.h>
#include <assert.h>
#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif

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
#include "bayer.h"
#include "metadata.h"
#include "DemoasicFrames.h"		//TODO: Change filename to lower case
#include "swap.h"
#include "draw.h"
#include "RGB2YUV.h"
#include "lutpath.h"
#include "exception.h"

extern void FastVignetteInplaceWP13(DECODER *decoder, int displayWidth, int width, int height, int y, float r1, float r2, float gain,
							  int16_t *sptr, int resolution, int pixelsize);
extern void FastSharpeningBlurHinplaceWP13(int width, int16_t *sptr, float sharpness, int resolution, int pixelsize);
extern void FastSharpeningBlurVWP13(short *Aptr,
						 short *Bptr,
						 short *Cptr,
						 short *Dptr,
						 short *Eptr,
						 int pitch,
						 int edgenear,
						 short *output, 
						 int pixels, 
						 float sharpness,
						 int resolution,
						 int channel_blend_type);
extern void FastSharpeningBlurVW13A(short *Aptr,
						 short *Bptr,
						 short *Cptr,
						 short *Dptr,
						 short *Eptr,
						 int pitch,
						 int edgenear,
						 short *output, 
						 int pixels, 
						 float sharpness,
						 int resolution,
						 int channel_blend_type);

#ifdef SPI_LOADER
#include "spi.h"
#include "keyframes.h"
#endif

#ifndef DUMP
#define DUMP (0 && _DUMP)
#endif

#define ERROR_TOLERANT			1

#if defined(_WIN32) && DEBUG
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

#define DEMOSAIC_DELAYLINES	4

// Forward references
void AllocDecoderGroup(DECODER *decoder);
bool AllocDecoderBuffer(DECODER *decoder, int width, int height, int format);
void EraseDecoderFrames(DECODER *decoder);
TRANSFORM *AllocGroupTransform(GROUP *group, int channel);
void EraseOutputBuffer(uint8_t *buffer, int width, int height, int32_t pitch, int format);
#if _DEBUG
bool DecodeBandFSM16sNoGap(FSM *fsm, BITSTREAM *stream, PIXEL16S *image, int width, int height, int pitch, FILE *logfile);
#else
bool DecodeBandFSM16sNoGap(FSM *fsm, BITSTREAM *stream, PIXEL16S *image, int width, int height, int pitch);
#endif
bool DecodeBandFSM16sNoGapHighByte(FSM *fsm, BITSTREAM *stream, PIXEL16S *image, int width, int height, int pitch, int quant);
bool DecodeBandFSM16sNoGap2Pass(FSM *fsm, BITSTREAM *stream, PIXEL16S *image, int width, int height, int pitch, int quant);
void CopyLowpassRGB444ToBuffer(DECODER *decoder, IMAGE *image_array[], int num_channels,
							   uint8_t *output_buffer, int32_t output_pitch,
							   FRAME_INFO *info, int chroma_offset,
							   int precision);

extern void Row16uQuarter2OutputFormat(DECODER *decoder, FRAME_INFO *info, int thread_index,
	uint8_t *output, int pitch, int frame, void *scratch, size_t scratch_size, int threading,
	uint8_t *channeldata[TRANSFORM_MAX_CHANNELS], // used in quarter res decodes
	int channelpitch[TRANSFORM_MAX_CHANNELS]); // used in quarter res decodes);
//extern void ComputeCube(DECODER *decoder);
extern bool NeedCube(DECODER *decoder);
extern void LoadTweak();

//extern int g_topdown;
//extern int g_bottomup;

// Performance measurements
#if _TIMING

extern TIMER tk_decompress;				// Timers
extern TIMER tk_decoding;
extern TIMER tk_convert;
extern TIMER tk_inverse;

extern COUNTER decode_byte_count;		// Counters
extern COUNTER sample_byte_count;
extern COUNTER alloc_group_count;
extern COUNTER alloc_transform_count;
extern COUNTER alloc_buffer_count;
extern COUNTER spatial_decoding_count;
extern COUNTER temporal_decoding_count;
extern COUNTER progressive_decode_count;

#endif

#if 0

// Table that maps from decoded format to pixel size
static const int PixelSize[] =
{
	0,		// DECODED_FORMAT_UNSUPPORTED
	2,		// DECODED_FORMAT_YUYV
	2,		// DECODED_FORMAT_UYVY
	2,		// DECODED_FORMAT_420
	4,		// DECODED_FORMAT_RGB32
	3,		// DECODED_FORMAT_RGB24
	2,		// DECODED_FORMAT_RGB555
	2,		// DECODED_FORMAT_RGB565

#if 0
	2,		// DECODED_FORMAT_YUYV_INVERTED
	2,		// DECODED_FORMAT_UYVY_INVERTED
	2,		// DECODED_FORMAT_420_INVERTED
#endif

	4,		// DECODED_FORMAT_RGB32_INVERTED
	3,		// DECODED_FORMAT_RGB24_INVERTED
	2,		// DECODED_FORMAT_RGB555_INVERTED
	2,		// DECODED_FORMAT_RGB565_INVERTED

	3,		// DECODED_FORMAT_V210,
	4,		// DECODED_FORMAT_YU64,			// Custom 16 bits per channel (all data scaled up) YUYV format.
	4,		// DECODED_FORMAT_YR16		// Rows of YUV with 16 bits per channel
};

#if _DEBUG
char *decoded_format_string[] =
{
	"Unsupported",
	"YUYV",
	"UYUV",
	"420",
	"RGB32",
	"RGB24",
	"RGB555",
	"RGB565",
#if 0
	"YUYV Inverted",
	"UYVY Inverted",
	"420 Inverted",
#endif
//#if BUILD_PROSPECT
	"RGB32 Inverted",
	"RGB24 Inverted",
	"RGB555 Inverted",
	"RGB565 Inverted",
	"V210"
//#endif
};
#endif

#else

static const int pixel_size_table[] =
{
	0,		// COLOR_FORMAT_UNKNOWN
	2,		// COLOR_FORMAT_UYVY
	2,		// COLOR_FORMAT_YUYV
	2,		// COLOR_FORMAT_YVYU
	0,		// COLOR_FORMAT_YV12
	0,		// COLOR_FORMAT_I420
	2,		// COLOR_FORMAT_RGB16
	3,		// COLOR_FORMAT_RGB24
	4,		// COLOR_FORMAT_RGB32
	0,
	3,		// COLOR_FORMAT_V210
	0,		// COLOR_FORMAT_RGB10
	4,		// COLOR_FORMAT_YU64
	4,		// COLOR_FORMAT_YR16
	4,		// COLOR_FORMAT_YUVA
};

static const int pixel_size_table_length = sizeof(pixel_size_table)/sizeof(pixel_size_table[0]);


static int PixelSize(int format)
{
	int pixel_size = 0;

	// Mask off the other fields in the format descriptor
	// Use the lookup table to determine the pixel size (if possible)
	if (0 <= format && format < pixel_size_table_length)
	{
		pixel_size = pixel_size_table[format];
		//return pixel_size;
	}

	//TODO: Change the rest of this routine into one big switch statement

	// Is this an Avid format?
	else if (COLOR_FORMAT_AVID <= format && format <= COLOR_FORMAT_AVID_END)
	{
		switch (format)
		{
		case COLOR_FORMAT_CbYCrY_8bit:
		case COLOR_FORMAT_CbYCrY_10bit_2_8:		// Only valid for the lower plane
			pixel_size = 1;
			break;

		case COLOR_FORMAT_CbYCrY_16bit:
		case COLOR_FORMAT_CbYCrY_16bit_2_14:
		case COLOR_FORMAT_CbYCrY_16bit_10_6:
			pixel_size = 2;
			break;

		default:
			assert(0);
			pixel_size = 2;		// Assume 16 bits per pixel if the format is unknown
			break;
		}
	}

	// Is this a Bayer format?
	else if (COLOR_FORMAT_BAYER <= format && format <= COLOR_FORMAT_BAYER_END)
	{
		pixel_size = (format - 100);
		if(pixel_size > 2)
			pixel_size = 2;
	}
	else if (format == COLOR_FORMAT_RG48)
		pixel_size = 6;
	else if (format == COLOR_FORMAT_RG64)
		pixel_size = 8;
	else if (format == COLOR_FORMAT_B64A) {
		pixel_size = 8;
	}

	return pixel_size;
}

#endif

int DecodedPixelSize(DECODED_FORMAT format)
{
	int pixel_size = 0;

	// Compute the pixel size
	switch (format)
	{
	case DECODED_FORMAT_YUYV:
		pixel_size = 2;
		break;

	case DECODED_FORMAT_RGB32:
		pixel_size = 4;
		break;

	case DECODED_FORMAT_RG48:
		pixel_size = 6;
		break;

	case DECODED_FORMAT_CT_UCHAR:
		pixel_size = 2;
		break;

	case DECODED_FORMAT_CT_SHORT:
	case DECODED_FORMAT_CT_SHORT_2_14:
	case DECODED_FORMAT_CT_USHORT_10_6:
		pixel_size = 4;
		break;

	case DECODED_FORMAT_CT_10Bit_2_8:
	case DECODED_FORMAT_V210:
		// This routine should not be called to compute the pixel sizes for these formats
		assert(0);
		return 0;
		break;

	case DECODED_FORMAT_ROW16U:
		pixel_size = 4;
		break;

	default:
		assert(0);
		return 0;
		break;
	}

	return pixel_size;
}

#if 0
// Convert FOURCC code to a string
static void str4cc(char *string, uint32_t  marker)
{
	char *p = (char *)&marker + 3;
	char *s = string;
	int i;
	for (i = 0; i < 4; i++)
		*(s++) = *(p--);
	*s = '\0';
}
#endif


void GetDisplayAspectRatio(DECODER *decoder, int *w, int *h)
{
	int origw,origh, guess = 0;

	origw = decoder->frame.width;
	origh = decoder->frame.height;

	switch(decoder->frame.resolution)
	{
	case DECODED_RESOLUTION_FULL:
		break;
	case DECODED_RESOLUTION_HALF:
		origw *= 2;
		origh *= 2;
		break;
	case DECODED_RESOLUTION_QUARTER:
		origw *= 4;
		origh *= 4;
		break;
	case DECODED_RESOLUTION_LOWPASS_ONLY:
		origw *= 8;
		origh *= 8;
		break;
	case DECODED_RESOLUTION_FULL_DEBAYER:
		break;
	case DECODED_RESOLUTION_HALF_NODEBAYER:
		origw *= 2;
		origh *= 2;
		break;
	case DECODED_RESOLUTION_QUARTER_NODEBAYER_SCALED:
		origw *= 4;
		origh *= 4;
		break;
	case DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER:
		//origw *= 2; //DAN20110129 -- seems the width has been corrected elsewhere or was never halved.
		break;
	case DECODED_RESOLUTION_HALF_HORIZONTAL:
		origw *= 2;
		break;
	case DECODED_RESOLUTION_HALF_VERTICAL:
		origh *= 2;
		break;
	}

	if(decoder->codec.picture_aspect_x <= 0 ||  decoder->codec.picture_aspect_y <= 0)
		guess = 1;

	// if guess default values, we can't trust them
	if(decoder->codec.picture_aspect_x == 16 && decoder->codec.picture_aspect_y == 9)
		guess = 1;

	if(decoder->pixel_aspect_x && decoder->pixel_aspect_y)
	{
		int j,den,num;
		decoder->codec.picture_aspect_x = num = (origw * decoder->pixel_aspect_x) / decoder->pixel_aspect_y;
		decoder->codec.picture_aspect_y = den = origh;

		for(j=2; j<num+den; j++)
		{
			while(num == (num/j)*j && den == (den/j)*j)
			{
				num /= j;
				den /= j;
			}
		}
		decoder->codec.picture_aspect_x = num; 
		decoder->codec.picture_aspect_y = den;
		guess = 0;
	}

	if(guess)
	{
		if(origw > 720) //HD.
		{
			if(origh == 1080)
			{
				if(origw == 2048)
					*w=origw,*h=origh;
				else
					*w=16,*h=9; // assume 16x9
			}
			else if(origh == 720)
			{
				*w=16,*h=9; // assume 16x9
			}
			else
			{
				*w=origw,*h=origh; // assume square pixel.
			}
		}
		else
		{
			if(origh == 720)
			{
				*w=16,*h=9; // assume 16x9
			}
			else
			{
				*w=origw,*h=origh; // assume square pixel.
			}
		}
	}
	else
	{
		*w=decoder->codec.picture_aspect_x;
		*h=decoder->codec.picture_aspect_y;
	}
}


bool IsValidFrameResolution(int resolution)
{
	switch (resolution)
	{
	case DECODED_RESOLUTION_FULL:
	case DECODED_RESOLUTION_HALF:
	case DECODED_RESOLUTION_QUARTER:
	case DECODED_RESOLUTION_LOWPASS_ONLY:
	case DECODED_RESOLUTION_HALF_HORIZONTAL:
	case DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER:
		return true;

	default:
		return false;
	}
}

// Return true if this decoder can decode to quarter resolution
bool IsQuarterResolutionEnabled(DECODER *decoder)
{
	return true;
}
size_t DecoderSize()
{
	return sizeof(DECODER);
}
void InitDecoder(DECODER *decoder, FILE *logfile, CODESET *cs)
{
#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "InitDecoder, decoder: 0x%p\n", decoder);
	}
#endif

	{
		//TODO: Clear the decoder before setting the CPU limit and affinity

		int i;
		//int thread_limit=0, thread_affinity=0, set_thread_params=0, capabilities=0;

		//save key params
		Thread_cntrl saved_params = decoder->thread_cntrl;

		// Clear everything
		memset(decoder, 0, sizeof(DECODER));

		//restore key params
		if(saved_params.set_thread_params == 1) // used by the DShow Interface
		{
			decoder->thread_cntrl = saved_params;
		}

#if _TIMING
		InitTiming();
#endif
		// Set the file for status information during decoding
		decoder->logfile = logfile;

		// Initialize the decoding error to no error
		decoder->error = CODEC_ERROR_OKAY;

		// Most recent marker found during decoding
		decoder->marker = 0;

		// Count of frames decoded
		decoder->frame_count = 0;

		// Set the codebooks that will be used for decoding
		if (cs != NULL)
		{
			// Use the codeset provided in the call
			for(i=0; i<CODEC_NUM_CODESETS; i++)
			{

				// Codebook for decoding highpass coefficients
				decoder->magsbook[i] = cs[i].magsbook;

				// Codebook for decoding runs of coefficients
				decoder->runsbook[i] = cs[i].runsbook;

				// Lookup table for fast codebook search
				decoder->fastbook[i] = cs[i].fastbook;
			}
		}
		else
		{
			// Use the default codeset
			decoder->magsbook[0] = cs9.magsbook;
			decoder->runsbook[0] = cs9.runsbook;
			decoder->fastbook[0] = cs9.fastbook;
		}

		// Initialize the codec state
		InitCodecState(&decoder->codec);
		
		InitScratchBuffer(&decoder->scratch, NULL, 0);

#if _DUMP

		// Initialize the descriptor for controlling debug output

		decoder->dump.enabled = false;

		decoder->dump.channel_mask = 0;
		decoder->dump.wavelet_mask = 0;

		memset(decoder->dump.directory, 0, sizeof(decoder->dump.directory));
		memset(decoder->dump.filename, 0, sizeof(decoder->dump.filename));

#endif

	}
//REDTEST
	decoder->frm = 0;
	decoder->run = 1;

#if _ALLOCATOR
	decoder->allocator = NULL;
#endif

	decoder->initialized = 1; //DAN20060912
}

void InitDecoderLicense(DECODER *decoder, const unsigned char *licensekey)
{
	if (decoder && licensekey)
	{
		const unsigned char unlicensed[16] = {0};
		//memset(unlicensed, 0, sizeof(unlicensed));

		// Has the license been set?
		if (memcmp(decoder->licensekey, unlicensed, sizeof(decoder->licensekey)) == 0)
		{
			// Copy the license into the decoder
			memcpy(decoder->licensekey, licensekey, sizeof(decoder->licensekey));
		}
	}
}

// Free data allocated within the decoder
void ClearDecoder(DECODER *decoder)
{
#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif

#if _ALLOCATOR
	ALLOCATOR *allocator = decoder->allocator;
#endif

	// Free the transforms allocated in the decoder
	int i;

	if(decoder->initialized == 0)
		return;  // nothing to free //DAN20060912

#if _GRAPHICS
	DrawClose(decoder);
#endif

	for(i=0; i<=METADATA_PRIORITY_MAX; i++)
	{
		if(decoder->DataBases[i])
		{
			#if _ALLOCATOR
			Free(decoder->allocator, decoder->DataBases[i]);
			#else
			MEMORY_FREE(decoder->DataBases[i]);
			#endif

			decoder->DataBases[i] = NULL;
			decoder->DataBasesSize[i] = 0;
			decoder->DataBasesAllocSize[i] = 0;
		}
	}

	if(decoder->sqrttable)
	{
#if _ALLOCATOR
		Free(decoder->allocator, decoder->sqrttable);
#else
		MEMORY_FREE(decoder->sqrttable);
#endif
		decoder->sqrttable = NULL;
	}

	for (i = 0; i < TRANSFORM_MAX_CHANNELS; i++)
	{
#if _ALLOCATOR
		FreeTransform(allocator, decoder->transform[i]);
#else
		FreeTransform(decoder->transform[i]);
#endif
		decoder->transform[i] = NULL;
	}

	if(decoder->aligned_sample_buffer)
	{
#if _ALLOCATOR
		FreeAligned(decoder->allocator, decoder->aligned_sample_buffer);
#else
		MEMORY_ALIGNED_FREE(decoder->aligned_sample_buffer);
#endif
		decoder->aligned_sample_buffer = NULL;
		decoder->aligned_sample_buffer_size = 0;
	}

	if(decoder->tools)
	{
#if _ALLOCATOR
		Free(decoder->allocator, decoder->tools);
#else
		MEMORY_FREE(decoder->tools);
#endif
		decoder->tools = NULL;
	}

	// Free the buffer allocated for decoding
	if (decoder->buffer != NULL)
	{

#if DEBUG_BUFFER_USAGE
		int i;
		char *ptr = (char *)decoder->buffer;
		FILE *fp = fopen("C:/free.txt", "a");
		fprintf(fp, "decoder->buffer = %08x buffer_size = %d\n", decoder->buffer ,decoder->buffer_size);

		i = decoder->buffer_size-1;
		while(ptr[i] == 1) i--;

		fprintf(fp, "used %2.3f percent\n", 100.0*(float)i/(float)decoder->buffer_size);
		fclose(fp);
#endif
#if _ALLOCATOR
		FreeAligned(allocator, decoder->buffer);
#else
		MEMORY_ALIGNED_FREE(decoder->buffer);
#endif
		decoder->buffer = NULL;
		decoder->buffer_size = 0;

		// Clear the fields in the scratch buffer descriptor
		memset(&decoder->scratch, 0, sizeof(SCRATCH));

		// Eventually the buffer and buffer size fields will be obsolete
	}

	for(i=0;i<_MAX_CPUS;i++)
	{
		if(decoder->threads_buffer[i])
		{
#if _ALLOCATOR
			FreeAligned(decoder->allocator, decoder->threads_buffer[i]);
#else
			MEMORY_ALIGNED_FREE(decoder->threads_buffer[i]);
#endif

			decoder->threads_buffer[i] = NULL;
		}
	}
	decoder->threads_buffer_size = 0;

	// Do not attempt to free the codebooks since the
	// codebook pointers are references to static tables

	// Can free some of the data structures allocated by the decoder
	FreeCodebooks(decoder);

#if _INTERLACED_WORKER_THREADS
	if(decoder->interlaced_worker.lock_init) // threads started
	{
		int i;

		// Signal this thread to stop
		SetEvent(decoder->interlaced_worker.stop_event);

	// Free all handles used by the worker threads
		for(i=0; i<THREADS_IN_LAST_WAVELET; i++)
		{
 			WaitForSingleObject(decoder->interlaced_worker.handle[i], INFINITE); //JY20080307

			CloseHandle(decoder->interlaced_worker.handle[i]);
			CloseHandle(decoder->interlaced_worker.start_event[i]);
			CloseHandle(decoder->interlaced_worker.done_event[i]);
		}
		CloseHandle(decoder->interlaced_worker.row_semaphore);
		CloseHandle(decoder->interlaced_worker.stop_event);

		for(i=0; i<THREADS_IN_LAST_WAVELET; i++)
		{
			decoder->interlaced_worker.handle[i] = 0;
			decoder->interlaced_worker.start_event[i] = 0;
			decoder->interlaced_worker.done_event[i] = 0;
		}
		decoder->interlaced_worker.row_semaphore = 0;
		decoder->interlaced_worker.stop_event = 0;
	}

	// Free the critical section used by the worker threads
	DeleteCriticalSection(&decoder->interlaced_worker.lock);
	decoder->interlaced_worker.lock_init = 0;
#endif

#if _THREADED
	if(decoder->entropy_worker_new.pool.thread_count)
	{
		ThreadPoolDelete(&decoder->entropy_worker_new.pool);
		DeleteLock(&decoder->entropy_worker_new.lock);
	}

	if(decoder->worker_thread.pool.thread_count)
	{
		ThreadPoolDelete(&decoder->worker_thread.pool);
		DeleteLock(&decoder->worker_thread.lock);
	}

	if(decoder->draw_thread.pool.thread_count)
	{
		ThreadPoolDelete(&decoder->draw_thread.pool);
		DeleteLock(&decoder->draw_thread.lock);
	}
	/*
	if(decoder->qt_convert_worker.pool.thread_count)
	{
		ThreadPoolDelete(&decoder->qt_convert_worker.pool);
		DeleteLock(&decoder->qt_convert_worker.lock);
	}

	if(decoder->qt_scale_worker.pool.thread_count)
	{
		ThreadPoolDelete(&decoder->qt_scale_worker.pool);
		DeleteLock(&decoder->qt_scale_worker.lock);
	}
	 */

	if(decoder->parallelDecoder)
	{
		if(decoder->parallelDecoder->decoder_thread.pool.thread_count)
		{
			ThreadPoolDelete(&decoder->parallelDecoder->decoder_thread.pool);
			DeleteLock(&decoder->parallelDecoder->decoder_thread.lock);
			decoder->parallelDecoder->decoder_thread.pool.thread_count = 0;
		}

		ClearDecoder(decoder->parallelDecoder);

		#if _ALLOCATOR
		Free(decoder->allocator, decoder->parallelDecoder);
		#else
		MEMORY_FREE(decoder->parallelDecoder);
		#endif
		decoder->parallelDecoder = NULL;
	}

#endif


	//MEMORY_ALIGNED_FREE(RawBayer16);
#if _ALLOCATOR
	if(decoder->RGBFilterBuffer16)
	{
		FreeAligned(decoder->allocator, decoder->RGBFilterBuffer16);
		decoder->RGBFilterBuffer16 = 0;
		decoder->RGBFilterBufferSize = 0;
	}
	if(decoder->RawBayer16)
	{
		FreeAligned(decoder->allocator, decoder->RawBayer16);
		decoder->RawBayer16 = 0;
		decoder->RawBayerSize = 0;
	}
	if(decoder->StereoBuffer)
	{
		FreeAligned(decoder->allocator, decoder->StereoBuffer);
		decoder->StereoBuffer = 0;
		decoder->StereoBufferSize = 0;
	}
	if(decoder->RawCube)
	{
		FreeAligned(decoder->allocator, decoder->RawCube);
		decoder->RawCube = 0;
	}
	if(decoder->Curve2Linear)
	{
		FreeAligned(decoder->allocator, decoder->Curve2Linear);
		decoder->Curve2Linear = 0;
	}
	if(decoder->Linear2CurveRed)
	{
		FreeAligned(decoder->allocator, decoder->Linear2CurveRed);
		decoder->Linear2CurveRed = NULL;
	}
	if(decoder->Linear2CurveGrn)
	{
		FreeAligned(decoder->allocator, decoder->Linear2CurveGrn);
		decoder->Linear2CurveGrn = NULL;
	}
	if(decoder->Linear2CurveBlu)
	{
		FreeAligned(decoder->allocator, decoder->Linear2CurveBlu);
		decoder->Linear2CurveBlu = NULL;
	}
	if(decoder->BYR4LinearRestore)
	{
		FreeAligned(decoder->allocator, decoder->BYR4LinearRestore);
		decoder->BYR4LinearRestore = NULL;
	}
	if(decoder->GammaContrastRed)
	{
		FreeAligned(decoder->allocator, decoder->GammaContrastRed);
		decoder->GammaContrastRed = NULL;
	}
	if(decoder->GammaContrastGrn)
	{
		FreeAligned(decoder->allocator, decoder->GammaContrastGrn);
		decoder->GammaContrastGrn = NULL;
	}
	if(decoder->GammaContrastBlu)
	{
		FreeAligned(decoder->allocator, decoder->GammaContrastBlu);
		decoder->GammaContrastBlu = NULL;
	}

	//3d LUT
	{
		if(decoder->LUTcache)
			Free(decoder->allocator, decoder->LUTcache);
		decoder->LUTcache = NULL;
		decoder->LUTcacheCRC = 0;
	}

#if WARPSTUFF
	{
		if (decoder->lens_correct_buffer)	
#if _ALLOCATOR
			Free(decoder->allocator, decoder->lens_correct_buffer);
#else
			MEMORY_ALIGNED_FREE(decoder->lens_correct_buffer);
#endif

		if (decoder->mesh)
			geomesh_destroy(decoder->mesh);


		decoder->lastLensOffsetX = 0;
		decoder->lastLensOffsetY = 0;
		decoder->lastLensOffsetZ = 0;
		decoder->lastLensOffsetR = 0;
		decoder->lastLensZoom = 0;
		decoder->lastLensFishFOV = 0;
		decoder->lastLensGoPro = 0;
		decoder->lastLensSphere = 0;
		decoder->lastLensFill = 0;
		decoder->lastLensStyleSel = 0;
		memset(decoder->lastLensCustomSRC, 0, sizeof(decoder->lastLensCustomSRC));
		memset(decoder->lastLensCustomDST, 0, sizeof(decoder->lastLensCustomDST));
		decoder->mesh = NULL;
		decoder->lens_correct_buffer = NULL;
	}
#endif

	if(decoder->overrideData)
	{
		Free(decoder->allocator, decoder->overrideData);
		decoder->overrideData = NULL;
		decoder->overrideSize = 0;
	}

	for(i=0; i<64; i++)
	{
		if(decoder->mdc[i])
			Free(decoder->allocator, decoder->mdc[i]);
		decoder->mdc[i] = NULL;
		decoder->mdc_size[i] = 0;
	}
	
#else
	if(decoder->RGBFilterBuffer16)
	{
		MEMORY_ALIGNED_FREE(decoder->RGBFilterBuffer16);
		decoder->RGBFilterBuffer16 = NULL;
	}
	if(decoder->RawBayer16)
	{
		MEMORY_ALIGNED_FREE(decoder->RawBayer16);
		decoder->RawBayer16 = NULL;
	}
	if(decoder->StereoBuffer)
	{
		MEMORY_ALIGNED_FREE(decoder->StereoBuffer);
		decoder->StereoBuffer = NULL;
		decoder->StereoBufferSize = 0;
	}
	if(decoder->RawCube)
	{
		MEMORY_ALIGNED_FREE(decoder->RawCube);
		decoder->RawCube = NULL;
	}
	if(decoder->Curve2Linear)
	{
		MEMORY_ALIGNED_FREE(decoder->Curve2Linear);
		decoder->Curve2Linear = NULL;
	}
	if(decoder->BYR4LinearRestore)
	{
		MEMORY_ALIGNED_FREE(decoder->BYR4LinearRestore);
		decoder->BYR4LinearRestore = NULL;
	}
	if(decoder->Linear2CurveRed)
	{
		MEMORY_ALIGNED_FREE(decoder->Linear2CurveRed);
		decoder->Linear2CurveRed = NULL;
	}
	if(decoder->Linear2CurveGrn)
	{
		MEMORY_ALIGNED_FREE(decoder->Linear2CurveGrn);
		decoder->Linear2CurveGrn = NULL;
	}
	if(decoder->Linear2CurveBlu)
	{
		MEMORY_ALIGNED_FREE(decoder->Linear2CurveBlu);
		decoder->Linear2CurveBlu = NULL;
	}
	if(decoder->GammaContrastRed)
	{
		MEMORY_ALIGNED_FREE(decoder->GammaContrastRed);
		decoder->GammaContrastRed = NULL;
	}
	if(decoder->GammaContrastGrn)
	{
		MEMORY_ALIGNED_FREE(decoder->GammaContrastGrn);
		decoder->GammaContrastGrn = NULL;
	}
	if(decoder->GammaContrastBlu)
	{
		MEMORY_ALIGNED_FREE(decoder->GammaContrastBlu);
		decoder->GammaContrastBlu = NULL;
	}

	//3d LUT
	{
		if(decoder->LUTcache)
			MEMORY_FREE(decoder->LUTcache);
		decoder->LUTcache = NULL;
		decoder->LUTcacheCRC = 0;
	}

#if WARPSTUFF
	{
		if (decoder->lens_correct_buffer)	
#if _ALLOCATOR
			Free(decoder->allocator, decoder->lens_correct_buffer);
#else
			MEMORY_ALIGNED_FREE(decoder->lens_correct_buffer);
#endif

		if (decoder->mesh)
			geomesh_destroy(mesh);


		decoder->mesh = NULL;
		decoder->lens_correct_buffer = NULL;
		decoder->lastLensOffsetX = 0;
		decoder->lastLensOffsetY = 0;
		decoder->lastLensOffsetZ = 0;
		decoder->lastLensOffsetR = 0;
		decoder->lastLensZoom = 0;
		decoder->lastLensFishFOV = 0;
		decoder->lastLlensGoPro = 0;
		decoder->lastLlensSphere = 0;
		decoder->lastLlensFill = 0;
		decoder->lastLlensStyleSel = 0;
		memset(decoder->lastLensCustomSRC, 0, sizeof(decoder->lastLensCustomSRC));
		memset(decoder->lastLensCustomDST, 0, sizeof(decoder->lastLensCustomDST));
	}
#endif

	if(decoder->overrideData)
	{
		MEMORY_FREE(decoder->overrideData);
		decoder->overrideData = NULL;
		decoder->overrideSize = 0;
	}

	for(i=0; i<64; i++)
	{
		if(decoder->mdc[i])
			MEMORY_FREE(decoder->mdc[i]);
		decoder->mdc[i] = NULL;
		decoder->mdc_size[i] = 0;
	}
#endif

#ifdef SPI_LOADER
	SPIReleaseAll(decoder);
	//KeyframesReleaseAll(decoder);
#endif

	decoder->initialized = 0;// cleared
}

void ExitDecoder(DECODER *decoder)
{
	// Let the caller keep the logfile open or choose to close it
	//if (logfile) fclose(logfile);

	// Free data allocated within the decoder
	ClearDecoder(decoder);
}

// Allocate the data structures for decoding a group
void AllocDecoderGroup(DECODER *decoder)
{
#if _ALLOCATOR
	ALLOCATOR *allocator = decoder->allocator;
#endif

	//CODEC_STATE *codec = &decoder->codec;
	//int num_channels = codec->num_channels;//DAN07022004
	int channel;

	assert(decoder->codec.num_channels <= TRANSFORM_MAX_CHANNELS); //DAN07022004

	for (channel = 0; channel < TRANSFORM_MAX_CHANNELS; channel++)//DAN07022004
	{
		TRANSFORM *transform = decoder->transform[channel];

		// Need to allocate a transform data structure?
		if (transform == NULL) {
#if _ALLOCATOR
			transform = (TRANSFORM *)Alloc(allocator, sizeof(TRANSFORM));
#else
			transform = (TRANSFORM *)MEMORY_ALLOC(sizeof(TRANSFORM));
#endif
			assert(transform != NULL);
			if (transform == NULL) {
				decoder->error = CODEC_ERROR_TRANSFORM_MEMORY;
				return;
			}
			memset(transform, 0, sizeof(TRANSFORM));
			decoder->transform[channel] = transform;

#if _TIMING
			alloc_transform_count++;
#endif
		}
	}
}

// Allocate the buffer used for intermediate results during decoding
bool AllocDecoderBuffer(DECODER *decoder, int width, int height, int format)
{
	int cpus;
	size_t size;
	size_t row_size;
	char *buffer;

#if 0
	// Allocate a buffer large enough for six rows of cache lines
	size = width * sizeof(PIXEL);
	size = ALIGN(size, _CACHE_LINE_SIZE);
	size = 2 * TRANSFORM_MAX_CHANNELS * size;
#else
	// Allocate a buffer large enough for nine rows of cache lines
	size = width * sizeof(PIXEL) * 4;
	size = ALIGN(size, _CACHE_LINE_SIZE);
	size = 3 * TRANSFORM_MAX_CHANNELS * size;
#endif

	switch (format)
	{
	case DECODED_FORMAT_V210:
	case DECODED_FORMAT_YU64:
		// Increase the buffer size for decoding to the V210 format
		row_size = 4 * width * sizeof(PIXEL);
		row_size = ALIGN(row_size, _CACHE_LINE_SIZE);
		size += 4 * 2 * row_size;
		break;

	case DECODED_FORMAT_YR16:
	case DECODED_FORMAT_CbYCrY_10bit_2_8:
	case DECODED_FORMAT_CbYCrY_16bit_2_14:
	case DECODED_FORMAT_CbYCrY_16bit_10_6:
		// Increase the buffer size for decoding to the YUV16 format
		row_size = 4 * width * sizeof(PIXEL);
		row_size = ALIGN(row_size, _CACHE_LINE_SIZE);
		size += 8 * 2 * row_size;
		break;

	case DECODED_FORMAT_RG48:
	case DECODED_FORMAT_WP13:
		// Increase the buffer size for decoding to the YUV16 format
		row_size = 6 * width * sizeof(PIXEL);
		row_size = ALIGN(row_size, _CACHE_LINE_SIZE);
		size += 12 * 2 * row_size;
		break;

	case DECODED_FORMAT_RG64:
		// Increase the buffer size for decoding to the YUV16 format
		row_size = 8 * width * sizeof(PIXEL);
		row_size = ALIGN(row_size, _CACHE_LINE_SIZE);
		size += 16 * 2 * row_size;
		break;

	case DECODED_FORMAT_BYR3:
		// Increase the buffer size for decoding to the YUV16 format
		row_size = 2 * width * sizeof(PIXEL);
		row_size = ALIGN(row_size, _CACHE_LINE_SIZE);
		size += 4 * 2 * row_size;
		break;

	case DECODED_FORMAT_BYR4:
		// Increase the buffer size for decoding to the YUV16 format
		row_size = 2 * width * sizeof(PIXEL);
		row_size = ALIGN(row_size, _CACHE_LINE_SIZE);
		size += 4 * 2 * row_size;
		break;

	case DECODED_FORMAT_B64A:
	case DECODED_FORMAT_W13A:
		// Increase the buffer size for decoding to the B64A format
		row_size = 8 * width * sizeof(PIXEL);
		row_size = ALIGN(row_size, _CACHE_LINE_SIZE);
		size += 16 * 2 * row_size;
		break;

	default:
		// Increase the buffer size for YUV to RGB conversion
		row_size = 3 * width * sizeof(PIXEL);
		row_size = ALIGN(row_size, _CACHE_LINE_SIZE);
		size += 2 * 2 * row_size;
		break;
	}

	cpus = decoder->thread_cntrl.capabilities >> 16;
	if(cpus > 4)
		size *= 4;
	if(cpus > 16) //DAN20120803 -- 4444 clips 
		size *= 2;

	// Has a buffer already been allocated?
	if (decoder->buffer != NULL)
	{
		// Is the buffer large enough?
		if (decoder->buffer_size < size)
		{
			// Free the previous buffer
#if _ALLOCATOR
			FreeAligned(decoder->allocator, decoder->buffer);
#else
			MEMORY_ALIGNED_FREE(decoder->buffer);
#endif
			decoder->buffer = NULL;
			decoder->buffer_size = 0;
		}
		else
		{
			return true;
		}
	}

    buffer = decoder->buffer;
	if(buffer == NULL)
	{
		// Allocate the decoding buffer
	#if _ALLOCATOR
		buffer = (char *)AllocAligned(decoder->allocator, size, _CACHE_LINE_SIZE);
	#else
		buffer = (char *)MEMORY_ALIGNED_ALLOC(size, _CACHE_LINE_SIZE);
	#endif
		if(buffer == NULL)
		{
			return false;
		}
	}

#if DEBUG_BUFFER_USAGE
	memset(buffer, 1, size);
#endif

	// Save the buffer and its size in the decoder
	decoder->buffer = buffer;
	decoder->buffer_size = size;

	// Initialize the scratch space descriptor
	InitScratchBuffer(&decoder->scratch, buffer, size);

	// allocate buffer for each debayer/color formating thread
	{
		int i;

		size = (width+16)*3*2*4*2*4;// sixteen lines

		if(height*4 > width*3)  //square or tall images where running out of scratch space for zooms.
			size *= 1 + ((height+(width/2))/width);

		if (decoder->threads_buffer_size < size)
		{
			for(i=0;i<_MAX_CPUS;i++)
			{
				if(decoder->threads_buffer[i])
				{
#if _ALLOCATOR
					FreeAligned(decoder->allocator, decoder->threads_buffer[i]);
#else
					MEMORY_ALIGNED_FREE(decoder->threads_buffer[i]);
#endif

					decoder->threads_buffer[i] = NULL;
				}
			}
			decoder->threads_buffer_size = 0;
		}

		for(i=0;i<cpus;i++)
		{
			if(decoder->threads_buffer[i] == NULL)
			{
				#if _ALLOCATOR
				decoder->threads_buffer[i] = (char *)AllocAligned(decoder->allocator, size, _CACHE_LINE_SIZE);
				#else
				decoder->threads_buffer[i] = (char *)MEMORY_ALIGNED_ALLOC(size, _CACHE_LINE_SIZE);
				#endif

				if(decoder->threads_buffer[i] == NULL)
				{
					return false;
				}
			}
		}

		decoder->threads_buffer_size = size;
	}

	// Eventually the scratch space descriptor will replace the buffer and buffer_size fields
	return true;
}

bool ResizeDecoderBuffer(DECODER *decoder, int width, int height, int format)
{
	// Check that the dimensions are valid
	assert(width > 0);
	assert(height > 0);

	// Just call the allocation routine
	return AllocDecoderBuffer(decoder, width, height, format);
}

void ClearTransformFlags(DECODER *decoder)
{
	TRANSFORM **transform_array = decoder->transform;
	int channel;

	for (channel = 0; channel < TRANSFORM_MAX_CHANNELS; channel++)
	{
		TRANSFORM *transform = transform_array[channel];
		int index;

		if (transform == NULL) break;

		for (index = 0; index < TRANSFORM_MAX_WAVELETS; index++)
		{
			IMAGE *wavelet = transform->wavelet[index];
			if (wavelet != NULL) {
				wavelet->band_valid_flags = 0;
				wavelet->band_started_flags = 0;
			}
		}
	}
}

// Initialize the tables for decoding the wavelet transforms
void InitWaveletDecoding(DECODER *decoder, int subband_wavelet_index[], int subband_band_index[], int num_subbands)
{
	size_t subband_table_size = num_subbands * sizeof(int);

	memset(decoder->subband_wavelet_index, 0, sizeof(decoder->subband_wavelet_index));
	memcpy(decoder->subband_wavelet_index, subband_wavelet_index, subband_table_size);

	memset(decoder->subband_band_index, 0, sizeof(decoder->subband_band_index));
	memcpy(decoder->subband_band_index, subband_band_index, subband_table_size);
}

#if 0
static bool IsValidFormat(int format)
{
	bool valid_format = true;

	//TODO: Change this routine into a switch statement

	if(format == COLOR_FORMAT_BYR5)
		return true; // can decode to BYR5
	if(format == COLOR_FORMAT_BYR4)
		return true; // can decode to BYR4
	if(format == COLOR_FORMAT_BYR3)
		return true; // can decode to BYR3
	if(format == COLOR_FORMAT_BYR2)
		return true; // can decode to BYR2
	if(format == COLOR_FORMAT_RG48)
		return true; // can decode to RGB48
	if(format == COLOR_FORMAT_RG64)
		return true; // can decode to RGBA64

	if (format == COLOR_FORMAT_B64A)
	{
		return true;	// Can decode to B64A
	}

	if (!(COLOR_FORMAT_UNKNOWN < format && format <= MAX_DECODED_COLOR_FORMAT)) {
		valid_format = false;
	}

	return valid_format;
}
#endif

#if _INTERLACED_WORKER_THREADS
void StartInterlaceWorkerThreads(DECODER *decoder)
{
	int i;
	if(decoder->interlaced_worker.lock_init == 0)
	{
		// Create events for starting the worker threads
		for(i=0; i<THREADS_IN_LAST_WAVELET; i++)
		{
			decoder->interlaced_worker.start_event[i] = CreateEvent(NULL, false, false, NULL);
		}

		// Create a semaphore to signal the worker threads to process rows
		decoder->interlaced_worker.row_semaphore = CreateSemaphore(NULL, 0, LONG_MAX, NULL);

		// Create an event for each worker thread to signal that it has finished
		for(i=0; i<THREADS_IN_LAST_WAVELET; i++)
		{
			decoder->interlaced_worker.done_event[i] = CreateEvent(NULL, false, false, NULL);
		}

		// Create an event for forcing the worker threads to terminate
		decoder->interlaced_worker.stop_event = CreateEvent(NULL, true, false, NULL);

		// Zero the count of worker threads that are active
		decoder->interlaced_worker.thread_count = 0;

		// Initialize the lock for controlling access to the worker thread data
		InitializeCriticalSection(&decoder->interlaced_worker.lock);
		decoder->interlaced_worker.lock_init = 1;

		for (i = 0; i < THREADS_IN_LAST_WAVELET; i++)
		{
			decoder->interlaced_worker.id[i] = 0;
			decoder->interlaced_worker.handle[i] = CreateThread(NULL, 0, InterlacedWorkerThreadProc, decoder, 0, &decoder->interlaced_worker.id[i]);
			assert(decoder->interlaced_worker.handle[i] != NULL);
		}
	}
}
#endif

#if 0
int TestException(int x)
{
	static volatile int y1 = 100;
	volatile int x1 = x;
	return y1 / x1;
}
#endif

// Process device driver request to initialize the decoder
#if _ALLOCATOR
bool DecodeInit(ALLOCATOR *allocator, DECODER *decoder, int width, int height, int format, int resolution, FILE *logfile)
#else
bool DecodeInit(DECODER *decoder, int width, int height, int format, int resolution, FILE *logfile)
#endif
{
	CODESET codesets[CODEC_NUM_CODESETS];
	int i;
	int cpus;

	//int x = 0;

#if CODEC_NUM_CODESETS == 3
	memcpy(&codesets[0], &CURRENT_CODESET, sizeof(CODESET));
	memcpy(&codesets[1], &SECOND_CODESET, sizeof(CODESET));
	memcpy(&codesets[2], &THIRD_CODESET, sizeof(CODESET));
#elif CODEC_NUM_CODESETS == 2
	memcpy(&codesets[0], &CURRENT_CODESET, sizeof(CODESET));
	memcpy(&codesets[1], &SECOND_CODESET, sizeof(CODESET));
#else
	memcpy(&codesets[0], &CURRENT_CODESET, sizeof(CODESET));
#endif
	
#ifdef _WIN32
	// Set the handler for system exceptions
	SetDefaultExceptionHandler();
#endif

	//TestException(x);

	// Clear all decoder fields except the logfile and set the codebooks for decoding
	InitDecoder(decoder, logfile, &codesets[0]);

#if _ALLOCATOR
	decoder->allocator = allocator;
#endif

	if(decoder->thread_cntrl.capabilities == 0)
	{
		// Determine the processor capabilities
		SetDecoderCapabilities(decoder);
	}
	cpus = decoder->thread_cntrl.capabilities >> 16;
	assert(cpus > 0 && cpus <= _MAX_CPUS);

	// Decode to half resolution?
	if (resolution == DECODED_RESOLUTION_HALF)
	{
		// Reduce the frame size by half in each dimension
		width = width/2;
		height = height/2;
	}
	else if (resolution == DECODED_RESOLUTION_QUARTER)
	{
		// Reduce the frame size by one fourth in each dimension
		width = width/4;
		height = height/4;
	}

	// Initialize the codebooks
#if _ALLOCATOR
	if (!InitCodebooks(decoder->allocator, codesets)) {
		//decoder->error = CODEC_ERROR_INIT_CODEBOOKS;
		// The subroutine has already set the error code
		return false;
	}
#else
	if (!InitCodebooks(codesets)) {
		//decoder->error = CODEC_ERROR_INIT_CODEBOOKS;
		// The subroutine has already set the error code
		return false;
	}
#endif
	// Initize the FSM
	InitDecoderFSM(decoder, &codesets[0]);

	// Check the frame dimensions and format
	//assert(width > 0);
	//assert(height > 0);
//	assert(IsValidFormat(format));


#if _THREADED_DECODER
	// Create a semaphore to signal the transform thread to begin processing
	// Initialize the transform queue

	decoder->transform_queue.started = 0;
	decoder->transform_queue.num_entries = 0;
	decoder->transform_queue.next_entry = 0;
	decoder->transform_queue.free_entry = 0;

	memset(decoder->transform_queue.queue, 0, sizeof(decoder->transform_queue.queue));
#endif

#if _INTERLACED_WORKER_THREADS && _DELAY_THREAD_START==0
	StartInterlaceWorkerThreads(decoder);
#endif

#if _THREADED
  #if !_DELAY_THREAD_START  //start threads now if not _DELAY_THREAD_START
	if(cpus > 1)
	{
		int threads = cpus;
		if(threads > 4) 
			threads = 4;

		CreateLock(&decoder->entropy_worker_new.lock);

		// Initialize the pool of transform worker threads
		ThreadPoolCreate(&decoder->entropy_worker_new.pool,
							threads,
							EntropyWorkerThreadProc,
							decoder);
	}

	// Initialize the lock that controls access to the generic worker thread data
	CreateLock(&decoder->worker_thread.lock);
	// Initialize the pool of transform worker threads
	ThreadPoolCreate(&decoder->worker_thread.pool,
						cpus,
						WorkerThreadProc,
						decoder);
  #endif
#endif

	// Set the frame dimensions and format
	SetDecoderFormat(decoder, width, height, format, resolution);

	// Allocate the data structure for decoding the samples
	AllocDecoderGroup(decoder);

	// Note that this code assumes that the samples to decode are groups
	// as opposed to isolated frames which are not supported in this code

	// Allocate a buffer for storing intermediate results during decoding
	if (!AllocDecoderBuffer(decoder, width, height, format)) {
		return false;
	}

	// Should check that the finite state machine tables were initialized
	assert(decoder->fsm[0].table.flags < 0);

	// Initialize the finite state machine for this decoder

	for(i=0; i<CODEC_NUM_CODESETS; i++)
	{
		InitFSM(&decoder->fsm[i], codesets[i].fsm_table);

#if _COMPANDING
		// Scale the values in the finite state machine entries for companding
		ScaleFSM(&decoder->fsm[i].table);
#endif
	}

	// Indicate that the decoder has been initialized
	decoder->state = DECODER_STATE_INITIALIZED;

#if (1 && DUMP)
	// Write the wavelet bands as images
	SetDumpDirectory(CODEC_TYPE(decoder), DUMP_DECODER_DIRECTORY);
	SetDumpFilename(CODEC_TYPE(decoder), DUMP_DEFAULT_FILENAME);
	SetDumpChannelMask(CODEC_TYPE(decoder), 1/*ULONG_MAX*/);
//	SetDumpWaveletMask(CODEC_TYPE(decoder), 7<<4 | 1/*ULONG_MAX*/);
	SetDumpWaveletMask(CODEC_TYPE(decoder), ULONG_MAX);

	// Set this flag to enable output
	decoder->dump.enabled = true;
#endif

#if _TIMING
	// Initialize the global timers and counters
	InitTiming();
#endif

	//DAN20160203 Fix for a memory leak in InitCookbooks
	for (i = 0; i < CODEC_NUM_CODESETS; i++)
	{
#if _ALLOCATOR
		Free(allocator, codesets[i].codebook_runbook);	codesets[i].codebook_runbook = NULL;
		Free(allocator, codesets[i].fastbook);			codesets[i].fastbook = NULL;
		Free(allocator, codesets[i].valuebook);			codesets[i].valuebook = NULL;
#else
		MEMORY_FREE(codesets[i].codebook_runbook);		codesets[i].codebook_runbook = NULL;
		MEMORY_FREE(codesets[i].fastbook);				codesets[i].fastbook = NULL;
		MEMORY_FREE(codesets[i].valuebook);				codesets[i].valuebook = NULL;
#endif
	}

	// The decoder has been initialized successfully
	return true;
}


void DecodeEntropyInit(DECODER *decoder)
{
	int cpus = 1;
	if(decoder->thread_cntrl.capabilities == 0)
	{
		// Determine the processor capabilities
		SetDecoderCapabilities(decoder);
	}
	cpus = decoder->thread_cntrl.capabilities >> 16;
	if (cpus > (int)decoder->cfhddata.cpu_limit && decoder->cfhddata.cpu_limit)
	{
		cpus = decoder->cfhddata.cpu_limit;
		decoder->thread_cntrl.limit = cpus;
		decoder->thread_cntrl.set_thread_params = 1;
		decoder->thread_cntrl.capabilities &= 0xffff;
		decoder->thread_cntrl.capabilities |= cpus<<16;
	}
	assert(cpus > 0 && cpus <= _MAX_CPUS);

#if _THREADED
  #if _DELAY_THREAD_START  //start threads now if not _DELAY_THREAD_START
	if(cpus > 1 && decoder->entropy_worker_new.pool.thread_count == 0)
	{
		int threads = cpus;
		if(threads > 4) 
			threads = 4;

		CreateLock(&decoder->entropy_worker_new.lock);

		// Initialize the pool of transform worker threads
		ThreadPoolCreate(&decoder->entropy_worker_new.pool,
							threads,
							EntropyWorkerThreadProc,
							decoder);
	}
  #endif
#endif
}


bool DecodeOverrides(DECODER *decoder, unsigned char *overrideData, int overrideSize)
{
	if(decoder->overrideData)
	{

#if _ALLOCATOR
		Free(decoder->allocator, decoder->overrideData);
#else
		MEMORY_FREE(decoder->overrideData);
#endif
		decoder->overrideData = NULL;
		decoder->overrideSize = 0;
	}

	if(overrideSize)
	{

#if _ALLOCATOR
		decoder->overrideData = Alloc(decoder->allocator, overrideSize);
#else
		decoder->overrideData = MEMORY_ALLOC(overrideSize);
#endif

		if(decoder->overrideData)
		{
			memcpy(decoder->overrideData, overrideData, overrideSize);
			decoder->overrideSize = overrideSize;
		}
	}
	else
	{
		int i;
		for(i=METADATA_PRIORITY_OVERRIDE; i<=METADATA_PRIORITY_MAX; i++) //This was 0 to max but that cause right eye primary corrections(side-by-side) mode to flicker.
																		 // This database cleariing was added but I don't know why.
		{
			if(decoder->DataBases[i])
			{
				#if _ALLOCATOR
				Free(decoder->allocator, decoder->DataBases[i]);
				#else
				MEMORY_FREE(decoder->DataBases[i]);
				#endif

				decoder->DataBases[i] = NULL;
				decoder->DataBasesSize[i] = 0;
				decoder->DataBasesAllocSize[i] = 0;
			}
		}
	}
	return true;
}

TRANSFORM *AllocGroupTransform(GROUP *group, int channel)
{
#if _ALLOCATOR
	//TODO:ALLOC Change this routine to take an allocator as the first argument
	ALLOCATOR *allocator = NULL;
#endif

	TRANSFORM *transform;

	// Channel zero is a special case because it may mean
	// that the group header has not been decoded yet
	if (channel != 0)
	{
		// Make sure that the channel number is in range
		assert(0 <= channel && channel < group->header.num_channels);
		if (!(0 <= channel && channel < group->header.num_channels))
			return NULL;
	}

	transform = group->transform[channel];

	// Need to allocate a transform data structure?
	if (transform == NULL) {
#if _ALLOCATOR
		transform = (TRANSFORM *)Alloc(allocator, sizeof(TRANSFORM));
#else
		transform = (TRANSFORM *)MEMORY_ALLOC(sizeof(TRANSFORM));
#endif
		assert(transform != NULL);
		if (transform == NULL) return NULL;
		memset(transform, 0, sizeof(TRANSFORM));
		group->transform[channel] = transform;

#if _TIMING
		alloc_transform_count++;
#endif
	}

	return transform;
}

//extern FILE *logfile;

void EraseOutputBuffer(uint8_t *buffer, int width, int height, int32_t pitch, int format)
{
	size_t size = height * pitch;

	union {
		uint8_t byte[4];
		uint32_t word;
	} output;

	switch (format)
	{
	case DECODED_FORMAT_YUYV:
		output.byte[0] = COLOR_LUMA_BLACK;
		output.byte[1] = COLOR_CHROMA_ZERO;
		output.byte[2] = COLOR_LUMA_BLACK;
		output.byte[3] = COLOR_CHROMA_ZERO;
		break;

	default:
		//if (logfile) fprintf(logfile,"**Unknown format: %d\n", format);
		//assert(0);
		output.word = 0;
		break;
	}

	memset(buffer, output.word, size);
}


// Decode the coefficients in a subband
bool DecodeSampleSubband(DECODER *decoder, BITSTREAM *input, int subband);

// Decode the coefficients in a lowpass band
bool DecodeSampleLowPassBand(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet);

// Decode the coefficients in a highpass band
bool DecodeSampleHighPassBand(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet, int band, int threading);

// Decode an empty band
bool DecodeSampleEmptyBand(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet, int band);

bool DecodeBand16s(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
				   int band_index, int width, int height);

bool DecodeBand16sLossless(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
				   int band_index, int width, int height);

// Decode a sample channel header
bool DecodeSampleChannelHeader(DECODER *decoder, BITSTREAM *input);

// Apply the inverse horizontal-temporal transform to reconstruct the output frame
void ReconstructSampleFrameToBuffer(DECODER *decoder, int frame, uint8_t *output, int pitch);

#if 0
// Reconstruct the frame to quarter resolution at full frame rate
void ReconstructQuarterFrame(DECODER *decoder, int num_channels,
							 uint8_t *frame1, uint8_t *frame2, int output_pitch,
							 FRAME_INFO *info, char *buffer, size_t buffer_size);
#else
// Reconstruct the frame to quarter resolution at full frame rate
void ReconstructQuarterFrame(DECODER *decoder, int num_channels,
							 int frame_index, uint8_t *output, int output_pitch,
							 FRAME_INFO *info, const SCRATCH *scratch, int precision);
#endif

// Copy the quarter resolution lowpass channels from the spatial transform
void CopyQuarterFrameToBuffer(TRANSFORM **transform_array, int num_channels,
							  uint8_t *output, int output_pitch,
							  FRAME_INFO *info, int precision);

// Convert the quarter resolution lowpass channels to the specified output format
void ConvertQuarterFrameToBuffer(DECODER *decoder, TRANSFORM **transform_array, int num_channels,
								 uint8_t *output, int output_pitch,
								 FRAME_INFO *info, int precision);

// Routines for converting the new encoded formats to the requested output format
CODEC_ERROR ReconstructSampleFrameRGB444ToBuffer(DECODER *decoder, int frame, uint8_t *output, int pitch);

CODEC_ERROR ReconstructSampleFrameRGBA4444ToBuffer(DECODER *decoder, int frame, uint8_t *output, int pitch);

CODEC_ERROR ReconstructSampleFrameYUVA4444ToBuffer(DECODER *decoder, int frame, uint8_t *output, int pitch);

// The first Bayer routine calls the other Bayer routines for the decoded resolution
CODEC_ERROR ReconstructSampleFrameBayerToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output, int pitch);
CODEC_ERROR ReconstructSampleFrameDeBayerFullToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output, int pitch);
CODEC_ERROR ReconstructSampleFrameBayerFullToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output, int pitch);
CODEC_ERROR ReconstructSampleFrameBayerHalfToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output, int pitch);
CODEC_ERROR ReconstructSampleFrameBayerQuarterToBuffer(DECODER *decoder, int frame, uint8_t *output, int pitch);

CODEC_ERROR UncompressedSampleFrameBayerToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output, int pitch);
CODEC_ERROR UncompressedSampleFrameYUVToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output, int pitch);
CODEC_ERROR UncompressedSampleFrameRGBToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output, int pitch);

// New code for handling the original YUV 4:2:2 encoded format
CODEC_ERROR ReconstructSampleFrameYUV422ToBuffer(DECODER *decoder, int frame, uint8_t *output, int pitch);


// Return true if the rest of the channel does not have to be decoded
static bool CanSkipChannel(DECODER *decoder, int resolution)
{
	CODEC_STATE *codec = &decoder->codec;
	int channel = codec->channel;
	TRANSFORM *transform = decoder->transform[channel];
	int transform_type = transform->type;

	// Can the rest of the channel be skipped?
	if (transform_type == TRANSFORM_TYPE_FIELDPLUS)
	{
		switch (resolution)
		{
		case DECODED_RESOLUTION_HALF:
			if(decoder->codec.encoded_format != ENCODED_FORMAT_BAYER)
				return ((codec->decoded_subband_flags & DECODED_SUBBAND_MASK_HALF) == DECODED_SUBBAND_MASK_HALF);
			break;

		case DECODED_RESOLUTION_QUARTER:
			if(decoder->codec.encoded_format != ENCODED_FORMAT_BAYER)
				return ((codec->decoded_subband_flags & DECODED_SUBBAND_MASK_QUARTER) == DECODED_SUBBAND_MASK_QUARTER);
			break;

		case DECODED_RESOLUTION_LOWPASS_ONLY:
			return (codec->decoded_subband_flags & 1);
			break;

		default:
			if(decoder->codec.encoded_format != ENCODED_FORMAT_BAYER)
			{
				if(decoder->frame.format == DECODED_FORMAT_YUYV || decoder->frame.format == DECODED_FORMAT_UYVY)
				{
					// If we are requesting a YUV decode we don't need the 4th channel
					if(codec->channel == 3)
					{
						return true;
					}
				}
			}
			break;
		}
	}
	else
	{
		const uint32_t decoded_subband_mask_half = 0x7F;
		const uint32_t decoded_subband_mask_quarter = 0x0F;

		assert(transform_type == TRANSFORM_TYPE_SPATIAL);

		switch (resolution)
		{
		case DECODED_RESOLUTION_HALF:
			if(decoder->codec.encoded_format != ENCODED_FORMAT_BAYER)
				return ((codec->decoded_subband_flags & decoded_subband_mask_half) == decoded_subband_mask_half);
			break;

		case DECODED_RESOLUTION_QUARTER:
			if(decoder->codec.encoded_format != ENCODED_FORMAT_BAYER)
				return ((codec->decoded_subband_flags & decoded_subband_mask_quarter) == decoded_subband_mask_quarter);
			break;

		case DECODED_RESOLUTION_LOWPASS_ONLY:
			return (codec->decoded_subband_flags & 1);
			break;

		default:
			if(decoder->codec.encoded_format != ENCODED_FORMAT_BAYER)
			{
				if(decoder->frame.format == DECODED_FORMAT_YUYV || decoder->frame.format == DECODED_FORMAT_UYVY)
				{
					// If we are requesting a YUV decode we don't need the 4th channel
					if(codec->channel == 3)
					{
						return true;
					}
				}
			}
			break;
		}
	}

	// Cannot skip the rest of the channel
	return false;
}

#if 0
static bool CanSkipSubband(DECODER *decoder, int subband)
{
	// Bitmask indicates which subbands must be decoded for quarter resolution
	static uint32_t quarter_resolution_mask = 0x008F;

	// Convert the subband number into a bitmask (could use a lookup table)
	uint32_t subband_mask = SUBBAND_MASK(subband);

	// Select the resolution of the fully decoded frames
	int resolution = decoder->frame.resolution;

	switch (resolution)
	{
	case DECODED_RESOLUTION_QUARTER:
		//if (4 <= subband && subband <= 6)
		if(decoder->codec.encoded_format != ENCODED_FORMAT_BAYER)
		{
			if ((subband_mask & quarter_resolution_mask) == 0) {
				return true;
			}
		}
		break;

	default:
		// Assume that the subband must be decoded
		break;
	}

	return false;
}
#endif

// Return true if the wavelet exists and all bands are valid
static bool AllBandsValid(IMAGE *wavelet)
{
	return (wavelet != NULL && BANDS_ALL_VALID(wavelet));
}

#if DEBUG
static bool AllTransformBandsValid(TRANSFORM *transform_array[], int num_channels, int frame_index)
{
	int channel;

	if (!(1 <= num_channels && num_channels <= TRANSFORM_MAX_CHANNELS)) {
		assert(0);
		return false;
	}

	if (!(0 <= frame_index && frame_index < TRANSFORM_MAX_FRAMES)) {
		assert(0);
		return false;
	}

	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform_array[channel]->wavelet[frame_index];
		if (!AllBandsValid(wavelet))
		{
			return false;
		}
	}

	// All wavelet bands in all channels are valid
	return true;
}

static bool AllLowpassBandsValid(TRANSFORM *transform_array[], int num_channels, int frame_index)
{
	int channel;

	if (!(0 < num_channels && num_channels <= TRANSFORM_MAX_CHANNELS)) {
		return false;
	}

	if (!(0 <= frame_index && frame_index < TRANSFORM_MAX_FRAMES)) {
		return false;
	}

	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform_array[channel]->wavelet[frame_index];
		if (!(wavelet != NULL && wavelet->band_valid_flags & BAND_VALID_MASK(0))) {
			return false;
		}
	}

	// All lowpass bands in all channels are valid
	return true;
}
#endif

static bool
ComputeFrameDimensionsFromFirstWavelet(int transform_type,
									   int first_wavelet_width,
									   int first_wavelet_height,
									   int *frame_width_out,
									   int *frame_height_out)
{
	int frame_width;
	int frame_height;

	int expansion = 8;

	switch (transform_type)
	{
		case TRANSFORM_TYPE_SPATIAL:
			frame_width = first_wavelet_width * expansion;
			frame_height = first_wavelet_height * expansion;
			break;

		case TRANSFORM_TYPE_FIELDPLUS:
			frame_width = first_wavelet_width * expansion;
			frame_height = first_wavelet_height * expansion;
			break;

		default:
			assert(0);
			return false;
	}

	// Return the frame dimensions
	*frame_width_out = frame_width;
	*frame_height_out = frame_height;

	return true;
}

// Decode the sample header to determine the type of sample and other parameters
bool ParseSampleHeader(BITSTREAM *input, SAMPLE_HEADER *header)
{
	TAGVALUE segment;
	int sample_type;
	int sample_size = 0;

	// Group index
	uint32_t channel_size[TRANSFORM_MAX_CHANNELS];

	// Number of channels in the group index
	int channel_count;

	// Values used for computing the frame width and height (if necessary)
	int transform_type = -1;
	int first_wavelet_width = 0;
	int first_wavelet_height = 0;
	int display_height = 0;
	int current_channel = 0;
	int currentVideoChannel = header->videoChannels;
	int find_lowpass_bands = header->find_lowpass_bands & 1;
	int find_uncompressed = header->find_lowpass_bands & 2 ? 1 : 0;
	int find_header_info_only = header->find_lowpass_bands & 4 ? 1 : 0;

	if (header == NULL) {
		return false;
	}
	if(currentVideoChannel == 0)
		currentVideoChannel = 1;

	// Clear the entire sample header to prevent early return from this routine
	memset(header, 0, sizeof(SAMPLE_HEADER));

	// Clear the error code
	header->error = CODEC_ERROR_OKAY;

	// Initialize the frame dimensions to unknown
	header->width = 0;
	header->height = 0;
	header->videoChannels = 1;

	// Initialize the original pixel format to unknown
	header->input_format = COLOR_FORMAT_UNKNOWN;

	// Initialize the encoded format to unknown
	header->encoded_format = ENCODED_FORMAT_UNKNOWN;

	// Clear the frame number in case it is not present in the sample
	header->frame_number = 0;

	// The video is not progressive if the sample flags are not present
	header->hdr_progressive = false;

#if _BITSTREAM_UNALIGNED
	// Record the alignment of the bitstream within the sample
	SetBitstreamAlignment(input, 0);
#endif
	
	sample_size = input->nWordsUsed;

	// Get the type of sample (should be the first tag value pair)
	segment = GetTagValue(input);
	assert(segment.tuple.tag == CODEC_TAG_SAMPLE);
	if (!IsValidSegment(input, segment, CODEC_TAG_SAMPLE)) {
		header->error = CodecErrorBitstream(input);
		return false;
	}
	sample_type = segment.tuple.value;

	switch (sample_type)
	{
	case SAMPLE_TYPE_GROUP:		// Group of frames
		header->key_frame = true;
		header->difference_frame = false;
		header->droppable_frame = false;
		break;

	case SAMPLE_TYPE_FRAME:		// The second or later frame in a group
		header->key_frame = false;
		header->difference_frame = true;
		header->droppable_frame = true;
		break;

	case SAMPLE_TYPE_IFRAME:	// One frame in the group
		header->key_frame = true;
		header->difference_frame = false;
		header->droppable_frame = true;
		break;

	case SAMPLE_TYPE_SEQUENCE_HEADER:
		// Treat the video sequence header like a keyframe that can be dropped
		header->key_frame = true;
		header->difference_frame = false;
		header->droppable_frame = true;
		break;

	default:
		// Unknown type of sample
		header->error = CODEC_ERROR_SAMPLE_TYPE;
		return false;
		break;
	}

	// Continue parsing the sample header until all of the information has been found
	while (	(find_lowpass_bands == 1 && current_channel < 3) || //parse all
			(find_uncompressed == 1 && current_channel < 1) || 
		   display_height == 0 ||
		   header->width == 0 ||
		   header->height == 0 ||
		   header->input_format == COLOR_FORMAT_UNKNOWN ||
		   header->frame_number == 0 ||
		   (header->interlaced_flags == 0 && header->hdr_progressive == 0))
	{
		int chunksize = 0;
		// Get the next tag value pair from the bitstream
		segment = GetSegment(input);

		// Did the bitstream end before the last tag was found?
		if (input->error == BITSTREAM_ERROR_UNDERFLOW) {
			break;
		}

		// Did an error occur while reading the bitstream?
		if (input->error != BITSTREAM_ERROR_OKAY) {
			header->error = CodecErrorBitstream(input);
			return false;
		}

		// Is this an optional tag?
		if (segment.tuple.tag < 0) {
			segment.tuple.tag = NEG(segment.tuple.tag);
		}

		if(segment.tuple.tag & 0x2000)
		{
			chunksize = segment.tuple.value;
			chunksize &= 0xffff;
			chunksize += ((segment.tuple.tag&0xff)<<16);
		}
		else if(segment.tuple.tag & 0x4000)
		{
			chunksize = segment.tuple.value;
			chunksize &= 0xffff;
		}
	//	else if(tag == CODEC_TAG_INDEX) // handled below
	//	{
	//		chunksize = value;
	//		chunksize &= 0xffff;
	//	}
		else
		{
			chunksize = 0;
		}

		if((int)(segment.tuple.tag) <= ((int)CODEC_TAG_LAST_NON_SIZED) || segment.tuple.tag & 0x6000)
		{
			int skip = 1;

			if((segment.tuple.tag & 0xff00) == 0x2200) //sample size
			{
				if(sample_size < chunksize*4)
					find_header_info_only = 1;

				skip = find_header_info_only;

				if(currentVideoChannel <= 1 && header->videoChannels == 2 && !find_header_info_only)
				{
					BITSTREAM input2;
					SAMPLE_HEADER header2;
					BITWORD *eye2 = (BITWORD *)(input->lpCurrentWord + chunksize*4);
					int eye_offset = sample_size - input->nWordsUsed + chunksize*4; //approx
					int eye_sample_size =  input->nWordsUsed - eye_offset;

					// Search for first sample of the next frame
					while((eye2[1] != (uint8_t)CODEC_TAG_SAMPLE || eye2[0] != 0 || eye2[2] != 0) && eye_sample_size > 0)
					{
						eye2 += 4;
						chunksize ++;
						eye_offset += 4;
						eye_sample_size -= 4;
					}

					// Save the offset to the right stereo sample
					header->left_sample_size = eye_offset;
					
					{
						InitBitstreamBuffer(&input2, eye2, eye_sample_size, BITSTREAM_ACCESS_READ);


						memset(&header2, 0, sizeof(SAMPLE_HEADER));
						header2.find_lowpass_bands = 1;

						currentVideoChannel++;
						header2.videoChannels = currentVideoChannel;

						if(ParseSampleHeader(&input2, &header2))
						{
							int i;
							for(i=0;i<4;i++)
							{
								if(header2.thumbnail_channel_offsets[i])
									header->thumbnail_channel_offsets_2nd_Eye[i] = eye_offset + header2.thumbnail_channel_offsets[i];
							}
						}
					}
				}
			}
			if((segment.tuple.tag & 0xff00) == 0x2300) //uncompressed sample size
			{
				header->hdr_uncompressed = 1;
				skip = 1;
				if(find_lowpass_bands != 1)
					break;
			}
			if((segment.tuple.tag & 0xff00) == 0x2100) //level
			{
				if(find_lowpass_bands == 1)
				{
					skip = 0;
				}
				else
				{
					skip = 1; // no header data after the fix level
					break;
				}
			}


			if(chunksize)
			{
				if(skip)
				{
					input->lpCurrentWord += chunksize*4;
					input->nWordsUsed -= chunksize*4;
				}
			}
			else
			{
				switch (segment.tuple.tag)
				{
				 case CODEC_TAG_VERSION:			// Version number of the encoder used in each GOP.
					header->encoder_version =	(((segment.tuple.value>>12) & 0xf)<<16) |
												(((segment.tuple.value>>8) & 0xf)<<8) |
												((segment.tuple.value) & 0xff);
					break;
				case CODEC_TAG_INDEX:
					// Get the number of channels in the index to skip
					channel_count = segment.tuple.value;
					DecodeGroupIndex(input, (uint32_t *)&channel_size[0], channel_count);
					break;

				case CODEC_TAG_FRAME_WIDTH:
					// Record the frame width in the sample header
					header->width = segment.tuple.value;
					break;

				case CODEC_TAG_FRAME_HEIGHT:
					// Record the frame height in the sample header
					header->height = segment.tuple.value;
					break;

				case CODEC_TAG_FRAME_DISPLAY_HEIGHT:
					display_height = segment.tuple.value;
					break;

				case CODEC_TAG_LOWPASS_WIDTH:
					// Save the width of the smallest wavelet for computing the frame dimensions
					first_wavelet_width = segment.tuple.value;
					break;

				case CODEC_TAG_LOWPASS_HEIGHT:
					// Save the height of the smallest wavelet for computing the frame dimensions
					first_wavelet_height = segment.tuple.value;
					break;

				case CODEC_TAG_TRANSFORM_TYPE:
					// Save the type of transform for computing the frame dimensions (if necessary)
					transform_type = segment.tuple.value;
					break;

				case CODEC_TAG_INPUT_FORMAT:
					// Record the original format of the encoded frames
					header->input_format = (COLOR_FORMAT)segment.tuple.value;
					break;

				case CODEC_TAG_ENCODED_FORMAT:
				case CODEC_TAG_OLD_ENCODED_FORMAT:
					// Record the encoded format (internal representation)
					header->encoded_format = (ENCODED_FORMAT)segment.tuple.value;
					if(header->encoded_format == ENCODED_FORMAT_RGBA_4444 && channel_count == 3)
						header->encoded_format = ENCODED_FORMAT_RGB_444;
					break;

				case CODEC_TAG_FRAME_NUMBER:
					// Record the frame number for debugging
					header->frame_number = segment.tuple.value;
					break;

				case CODEC_TAG_INTERLACED_FLAGS:
					// Record the flags that indicate the field type
					header->interlaced_flags = segment.tuple.value;
					break;

				case CODEC_TAG_SAMPLE_FLAGS:
					// The sample flags specify progressive versus interlaced decoding
					header->hdr_progressive = !!(segment.tuple.value & SAMPLE_FLAGS_PROGRESSIVE);
					if (header->hdr_progressive) {
						// Clear the interlaced flags
						header->interlaced_flags = 0;
					}
					break;

				case CODEC_TAG_LOWPASS_SUBBAND:
					if(segment.tuple.value == 0) // low pass band
					{
						int count = 8;
						uint32_t *lptr = (uint32_t *)input->lpCurrentWord;
						do
						{
							uint32_t longword = SwapInt32(lptr[count]);
							unsigned short t,v;
							t = (longword>>16) & 0xffff;
							v = (longword) & 0xffff;
							if (t == CODEC_TAG_MARKER && IsLowPassBandMarker(v) && current_channel < 4)
							{
								header->thumbnail_channel_offsets[current_channel] = (sample_size - input->nWordsUsed) + count*4 + 4;
								break;
							}

							count++;
						} while(count < 32);

						current_channel++;
					}
					break;

				case CODEC_TAG_ENCODED_CHANNELS:
					if(header->videoChannels == 1)
					{
						header->videoChannels = segment.tuple.value;
						if(header->videoChannels < 1)
							header->videoChannels = 1;
					}
					break;
					

				case CODEC_TAG_QUALITY_L:		//
					header->encode_quality &= 0xffff0000;
					header->encode_quality |= segment.tuple.value;
					break;

				case CODEC_TAG_QUALITY_H:		//
					header->encode_quality &= 0xffff;
					header->encode_quality |= segment.tuple.value<<16;
					break;

				}

				// Have the encoded frame dimensions been computed?
				if (header->width == 0 || header->height == 0)
				{
					// Found the first wavelet in the bitstream?
					if (transform_type >= 0 && first_wavelet_width > 0 && first_wavelet_height > 0)
					{
						// The group header did not contain tags for the frame dimensions
						// prior to the release of support for RGB 4:4:4, so must attempt to
						// compute the frame dimensions from the dimensions of the lowpass band.
						int frame_width = 0;
						int frame_height = 0;

						// Use the dimensions of the first wavelet to compute the frame width and height
						if (!ComputeFrameDimensionsFromFirstWavelet(transform_type,
																	first_wavelet_width,
																	first_wavelet_height,
																	&frame_width,
																	&frame_height)) {

							// Could not compute the frame dimensions
							header->error = CODEC_ERROR_FRAME_DIMENSIONS;
							return false;
						}

						// Save the frame dimensions in the sample header
						header->width = frame_width;
						header->height = frame_height;

						// No more header information after finding the lowpass band
						break;
					}
				}

				if(find_lowpass_bands != 1 && find_uncompressed != 1)
				{
					// No more header information after the first encoded band
					if (segment.tuple.tag == CODEC_TAG_BAND_NUMBER)
					{
						// Stop looking for header information
						break;
					}

					// No more header information after the frame index
					if (segment.tuple.tag == CODEC_TAG_FRAME_INDEX)
					{
						// Stop looking for header information
						break;
					}

					// No more header information after the lowpass band header
					if (segment.tuple.tag == CODEC_TAG_PIXEL_DEPTH)
					{
						// Stop looking for header information
						break;
					}
				}
			}
		}
	}

	if (header->width == 0 || header->height == 0) {
		assert(0);
	}

	// Fill in the encoded format if it was not present in the header
	if (header->encoded_format == ENCODED_FORMAT_UNKNOWN) {
		header->encoded_format = GetEncodedFormat(header->input_format, header->encode_quality, channel_count);
	}

	if (display_height > 0) {
		header->height = display_height;
	}

	if (header->encoded_format == ENCODED_FORMAT_BAYER)
	{
		header->width *= 2;
		header->height *= 2;

		if(display_height == 0)
		{
			if(header->height == 1088)
				header->height = 1080;
		}
	}

	// Return true if the header was parsed completely and correctly
	return (header->width > 0 &&
			header->height > 0 &&
			((sample_type == SAMPLE_TYPE_FRAME) ||
			(header->input_format != COLOR_FORMAT_UNKNOWN &&
			header->encoded_format != ENCODED_FORMAT_UNKNOWN)));

	// It is not an error if the frame number was not found in the sample header
}

bool DumpSampleHeader(BITSTREAM *input, FILE *logfile)
{
	TAGVALUE segment;

	int lowpass_width = 0;
	int lowpass_height = 0;

	// Parse the sample header until the lowpass band is found
	while (lowpass_width == 0 && lowpass_height == 0)
	{
		// Get the next tag value pair from the bitstream
		segment = GetSegment(input);

		// Did an error occur while reading the bitstream?
		if (input->error != BITSTREAM_ERROR_OKAY) {
			return false;
		}

		// Is this an optional tag?
		if (segment.tuple.tag < 0) {
			segment.tuple.tag = NEG(segment.tuple.tag);
		}

		// Check that the tag is valid
		assert(CODEC_TAG_ZERO < segment.tuple.tag && segment.tuple.tag <= CODEC_TAG_LAST_NON_SIZED);

		switch (segment.tuple.tag)
		{
		case CODEC_TAG_SAMPLE:
			fprintf(logfile, "Sample type: %d\n", segment.tuple.value);
			break;

		case CODEC_TAG_FRAME_WIDTH:
			fprintf(logfile, "Frame width: %d\n", segment.tuple.value);
			break;

		case CODEC_TAG_FRAME_HEIGHT:
			fprintf(logfile, "Frame height: %d\n", segment.tuple.value);
			break;

		case CODEC_TAG_LOWPASS_WIDTH:
			lowpass_width = segment.tuple.value;
			fprintf(logfile, "Lowpass width: %d\n", lowpass_width);
			break;

		case CODEC_TAG_LOWPASS_HEIGHT:
			lowpass_height = segment.tuple.value;
			fprintf(logfile, "Lowpass height: %d\n", lowpass_height);
			break;

		case CODEC_TAG_TRANSFORM_TYPE:
			fprintf(logfile, "Transform type: %d\n", segment.tuple.value);
			break;

		case CODEC_TAG_INPUT_FORMAT:
			fprintf(logfile, "Input format: %d\n", segment.tuple.value);
			break;

		case CODEC_TAG_ENCODED_FORMAT:
		case CODEC_TAG_OLD_ENCODED_FORMAT:
			fprintf(logfile, "Encoded format: %d\n", segment.tuple.value);
			break;

		case CODEC_TAG_FRAME_NUMBER:
			fprintf(logfile, "Frame number: %d\n", segment.tuple.value);
			break;
		}
	}

	return true;
}

int SkipVideoChannel(DECODER *decoder, BITSTREAM *input, int skip_to_channel) // 3D work
{
	TAGWORD tag,value=1; 
	unsigned char *pos = NULL;
	int readsize =  input->nWordsUsed;
	if(readsize > 4096) // only need to scan the first few tuplets
	{
		readsize = 4096;
	}
	else
	{
		//Tiny therefore P-frame, nothing to be read so:
		value=decoder->real_channels; // return the last value.
		return value;
	}


	pos = GetTupletAddr(input->lpCurrentBuffer, readsize, CODEC_TAG_ENCODED_CHANNELS, &value);

	if(pos && value>1 && skip_to_channel>1)
	{
		int chunksize = 0;
		intptr_t offset;
		int count = 0;

		do
		{
			tag = *pos++<<8;
			tag |= *pos++;
			value = *pos++<<8;
			value |= *pos++;

			if (tag < 0)
			{
				tag = NEG(tag);
			}
		} while((tag & 0xff00) != CODEC_TAG_SAMPLE_SIZE && count++ < 10);

		if((tag & 0xff00) == CODEC_TAG_SAMPLE_SIZE)
		{
			chunksize = value;
			chunksize &= 0xffff;
			chunksize += ((tag&0xff)<<16);

			offset = ((intptr_t)pos - (intptr_t)input->lpCurrentWord) + chunksize*4;

			input->lpCurrentWord += offset;
			input->nWordsUsed -= (int)offset;

			{
				uint8_t *tag = (uint8_t *)input->lpCurrentWord;
			
				// Search for first sample of the next frame
				while((tag[1] != (uint8_t)CODEC_TAG_SAMPLE || tag[0] != 0 || tag[2] != 0) && input->nWordsUsed > 0)
				{
					input->lpCurrentWord += 4;
					input->nWordsUsed -= 4;
					tag += 4;
				}

			}
		}
	}

	//if(value == 0) value = 1; // old non-stereo file
	return value;
}


#define SUBPIXEL	64


static short gains[SUBPIXEL+1][4] = {
    {0*128,0*128,0x7fff,0*128},
    {0*128,2*128,0x7fff,-2*128},
    {0*128,5*128,255*128,-4*128},
    {0*128,8*128,254*128,-6*128},
    {0*128,11*128,253*128,-8*128},
    {0*128,14*128,252*128,-10*128},
    {0*128,18*128,250*128,-12*128},
    {0*128,21*128,248*128,-13*128},
    {-1*128,25*128,247*128,-15*128},
    {-1*128,29*128,244*128,-16*128},
    {-1*128,33*128,241*128,-17*128},
    {-2*128,37*128,239*128,-18*128},
    {-2*128,41*128,236*128,-19*128},
    {-3*128,46*128,233*128,-20*128},
    {-3*128,50*128,229*128,-20*128},
    {-4*128,55*128,226*128,-21*128},
    {-4*128,60*128,221*128,-21*128},
    {-5*128,65*128,217*128,-21*128},
    {-5*128,70*128,213*128,-22*128},
    {-6*128,75*128,209*128,-22*128},
    {-7*128,80*128,205*128,-22*128},
    {-7*128,85*128,199*128,-21*128},
    {-8*128,91*128,194*128,-21*128},
    {-9*128,96*128,190*128,-21*128},
    {-10*128,102*128,185*128,-21*128},
    {-10*128,107*128,179*128,-20*128},
    {-11*128,113*128,174*128,-20*128},
    {-12*128,118*128,169*128,-19*128},
    {-13*128,124*128,164*128,-19*128},
    {-14*128,129*128,159*128,-18*128},
    {-14*128,135*128,152*128,-17*128},
    {-15*128,141*128,147*128,-17*128},
    {-16*128,144*128,144*128,-16*128},
    {-17*128,147*128,141*128,-15*128},
    {-17*128,152*128,135*128,-14*128},
    {-18*128,159*128,129*128,-14*128},
    {-19*128,164*128,124*128,-13*128},
    {-19*128,169*128,118*128,-12*128},
    {-20*128,174*128,113*128,-11*128},
    {-20*128,179*128,107*128,-10*128},
    {-21*128,185*128,102*128,-10*128},
    {-21*128,190*128,96*128,-9*128},
    {-21*128,194*128,91*128,-8*128},
    {-21*128,199*128,85*128,-7*128},
    {-22*128,205*128,80*128,-7*128},
    {-22*128,209*128,75*128,-6*128},
    {-22*128,213*128,70*128,-5*128},
    {-21*128,217*128,65*128,-5*128},
    {-21*128,221*128,60*128,-4*128},
    {-21*128,226*128,55*128,-4*128},
    {-20*128,229*128,50*128,-3*128},
    {-20*128,233*128,46*128,-3*128},
    {-19*128,236*128,41*128,-2*128},
    {-18*128,239*128,37*128,-2*128},
    {-17*128,241*128,33*128,-1*128},
    {-16*128,244*128,29*128,-1*128},
    {-15*128,247*128,25*128,-1*128},
    {-13*128,248*128,21*128,0*128},
    {-12*128,250*128,18*128,0*128},
    {-10*128,252*128,14*128,0*128},
    {-8*128,253*128,11*128,0*128},
    {-6*128,254*128,8*128,0*128},
    {-4*128,255*128,5*128,0*128},
    {-2*128,0x7fff,2*128,0*128},
    {0*128,0*128,0x7fff,0*128}
};


static int lanczos[256] =
{
	0,
   -2,
   -8,
  -18,
  -33,
  -53,
  -77,
 -106,
 -141,
 -179,
 -223,
 -272,
 -325,
 -384,
 -447,
 -514,
 -586,
 -662,
 -742,
 -826,
 -913,
 -1004,
 -1097,
 -1193,
 -1290,
 -1389,
 -1490,
 -1591,
 -1692,
 -1792,
 -1892,
 -1990,
 -2086,
 -2179,
 -2269,
 -2355,
 -2436,
 -2511,
 -2580,
 -2643,
 -2697,
 -2744,
 -2781,
 -2809,
 -2826,
 -2832,
 -2826,
 -2808,
 -2776,
 -2730,
 -2670,
 -2594,
 -2503,
 -2395,
 -2271,
 -2129,
 -1969,
 -1790,
 -1593,
 -1377,
 -1141,
 -886,
 -611,
 -315,
    0,
  336,
  692,
 1069,
 1466,
 1884,
 2321,
 2778,
 3255,
 3750,
 4265,
 4797,
 5347,
 5914,
 6498,
 7097,
 7711,
 8340,
 8982,
 9636,
 10301,
 10977,
 11663,
 12357,
 13058,
 13765,
 14477,
 15192,
 15910,
 16630,
 17349,
 18066,
 18781,
 18871,
 19580,
 20285,
 20986,
 21678,
 22361,
 23035,
 23697,
 24348,
 24983,
 25604,
 26206,
 26790,
 27354,
 27898,
 28419,
 28915,
 29387,
 29832,
 30249,
 30638,
 30997,
 31326,
 31623,
 31886,
 32117,
 32314,
 32476,
 32603,
 32695,
 32749,
 32767,	//was 32768, issue for SSE2
 32749,
 32695,
 32603,
 32476,
 32314,
 32117,
 31886,
 31623,
 31326,
 30997,
 30638,
 30249,
 29832,
 29387,
 28915,
 28419,
 27898,
 27354,
 26790,
 26206,
 25604,
 24983,
 24348,
 23697,
 23035,
 22361,
 21678,
 20986,
 20285,
 19580,
 18871,
 18159,
 18066,
 17349,
 16630,
 15910,
 15192,
 14477,
 13765,
 13058,
 12357,
 11663,
 10977,
 10301,
 9636,
 8982,
 8340,
 7711,
 7097,
 6498,
 5914,
 5347,
 4797,
 4265,
 3750,
 3255,
 2778,
 2321,
 1884,
 1466,
 1069,
  692,
  336,
    0,
 -315,
 -611,
 -886,
 -1141,
 -1377,
 -1593,
 -1790,
 -1969,
 -2129,
 -2271,
 -2395,
 -2503,
 -2594,
 -2670,
 -2730,
 -2776,
 -2808,
 -2826,
 -2832,
 -2826,
 -2809,
 -2781,
 -2744,
 -2697,
 -2643,
 -2580,
 -2511,
 -2436,
 -2355,
 -2269,
 -2179,
 -2086,
 -1990,
 -1892,
 -1792,
 -1692,
 -1591,
 -1490,
 -1389,
 -1290,
 -1193,
 -1097,
 -1004,
 -913,
 -826,
 -742,
 -662,
 -586,
 -514,
 -447,
 -384,
 -325,
 -272,
 -223,
 -179,
 -141,
 -106,
  -77,
  -53,
  -33,
  -18,
   -8,
   -2,
};


void RGB48VerticalShiftZoom(DECODER *decoder, unsigned short *RGB48, unsigned short *buffer,
						int widthbytes, int height, int pitch, float offset,
						float zoom)
{
	float yposf,ystepf;
	int x;
	//int endofSSEline = 0;
	unsigned short *scanline[4];
	//int spitch = pitch/2;
	int neg = 0,step;

	__m128i lA,lB,lC,lD,gA,gB,gC,gD,o128,t1;
	__m128i *lineA, *lineB, *lineC, *lineD, *outline128;

	offset = -offset;

	yposf = height * offset;
	yposf = (float)height*(0.5f - 1.0f/(2.0f*zoom) - offset);
	ystepf = 1.0f/zoom;

	if(yposf < 0.0)
		neg = 1;

	if(pitch < 0)
		yposf -= ystepf;

/*	yposi = floor(yposf);

	remainf = yposf - (float)yposi;
	tablepos = (remainf*(float)SUBPIXEL);

	yposi = abs(yposi);

	if(yposi==0 && tablepos == 0)
		return; // no move required
*/
	// -3 , 0 best small notch at zero?
	//

	switch(decoder->StereoBufferFormat)
	{
	case DECODED_FORMAT_RGB32:
	case DECODED_FORMAT_RGB24:
	case DECODED_FORMAT_YUYV:
		step = 16;
		break;
	case DECODED_FORMAT_W13A:
	case DECODED_FORMAT_RG64:
	case DECODED_FORMAT_WP13:
	case DECODED_FORMAT_RG48:
	default:
		step = 32;
		break;
	}

	{
		static char zeroline[1024] = {0};
		int y,yoffset = ((int)(yposf-2.0)),yend = ((int)(yposf+2.0+ystepf*height));
		unsigned char *src = (unsigned char *)RGB48;
		unsigned char *dst = (unsigned char *)RGB48;
		unsigned char *ptr = (unsigned char *)buffer;

		if(yoffset < 0) yoffset = 0;
		if(yend > height) yend = height;

		src += pitch * yoffset;

		for(y=yoffset; y<yend; y++)
		{
			memcpy(ptr, src, widthbytes);

			ptr += widthbytes;
			src += pitch;
		}

		ptr = (unsigned char *)buffer;
		for(y=0;y<height; y++)
		{
			int i,t,yp = ((int)yposf);
			int rmdr = 63-((int)(yposf*64.0) & 63);
			int gains[4];

			yp -= 1; // use -2 cause a image down shift //DAN20100225
			t = 0;
			for(i=0; i<4; i++)
			{
				if(yp<0 || yp>= height) // skip 0 line as the top line was zagged
				{
					t += gains[i] = lanczos[rmdr];
					scanline[i] = (unsigned short *)zeroline;
				}
				else
				{
					t += gains[i] = lanczos[rmdr];
					scanline[i] = (unsigned short *)&ptr[widthbytes*(yp-yoffset)];
				}

				yp++;
				rmdr+=64;
			}

			if(t)
			{
				__m128i half;

				gA = _mm_set1_epi16(gains[0]);
				gB = _mm_set1_epi16(gains[1]);
				gC = _mm_set1_epi16(gains[2]);
				gD = _mm_set1_epi16(gains[3]);

				outline128 = (__m128i *)dst;

				lineA = (__m128i *)scanline[0];
				lineB = (__m128i *)scanline[1];
				lineC = (__m128i *)scanline[2];
				lineD = (__m128i *)scanline[3];

				switch(decoder->StereoBufferFormat)
				{
				case DECODED_FORMAT_W13A:
				case DECODED_FORMAT_WP13:
					for(x=0;x<widthbytes; x+=step)
					{
						lA =  _mm_loadu_si128(lineA++);
						lB =  _mm_loadu_si128(lineB++);
						lC =  _mm_loadu_si128(lineC++);
						lD =  _mm_loadu_si128(lineD++);

						o128 = _mm_mulhi_epi16(lA, gA);

						t1 = _mm_mulhi_epi16(lB, gB);
						o128 = _mm_adds_epi16(o128,t1);

						t1 = _mm_mulhi_epi16(lC, gC);
						o128 = _mm_adds_epi16(o128,t1);

						t1 = _mm_mulhi_epi16(lD, gD);
						o128 = _mm_adds_epi16(o128,t1);


						// upper limit to 32767
						o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
						o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
						o128 = _mm_slli_epi16(o128,1);
						_mm_storeu_si128(outline128++, o128);

						lA =  _mm_loadu_si128(lineA++);
						lB =  _mm_loadu_si128(lineB++);
						lC =  _mm_loadu_si128(lineC++);
						lD =  _mm_loadu_si128(lineD++);


						o128 = _mm_mulhi_epi16(lA, gA);

						t1 = _mm_mulhi_epi16(lB, gB);
						o128 = _mm_adds_epi16(o128,t1);

						t1 = _mm_mulhi_epi16(lC, gC);
						o128 = _mm_adds_epi16(o128,t1);

						t1 = _mm_mulhi_epi16(lD, gD);
						o128 = _mm_adds_epi16(o128,t1);

						
						// upper limit to 32767
						o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
						o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
						o128 = _mm_slli_epi16(o128,1);

						_mm_storeu_si128(outline128++, o128);
					}
					break;


				case DECODED_FORMAT_RG64:
				case DECODED_FORMAT_RG48:
					for(x=0;x<widthbytes; x+=step)
					{
						lA =  _mm_loadu_si128(lineA++);
						lA = _mm_srli_epi16(lA,3); //13-bit unsigned
						lB =  _mm_loadu_si128(lineB++);
						lB = _mm_srli_epi16(lB,3); //13-bit unsigned
						lC =  _mm_loadu_si128(lineC++);
						lC = _mm_srli_epi16(lC,3); //13-bit unsigned
						lD =  _mm_loadu_si128(lineD++);
						lD = _mm_srli_epi16(lD,3); //13-bit unsigned

						o128 = _mm_mulhi_epi16(lA, gA);

						t1 = _mm_mulhi_epi16(lB, gB);
						o128 = _mm_adds_epi16(o128,t1);

						t1 = _mm_mulhi_epi16(lC, gC);
						o128 = _mm_adds_epi16(o128,t1);

						t1 = _mm_mulhi_epi16(lD, gD);
						o128 = _mm_adds_epi16(o128,t1);


						o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
						o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
						o128 = _mm_slli_epi16(o128,4);
						_mm_storeu_si128(outline128++, o128);

						lA =  _mm_loadu_si128(lineA++);
						lA = _mm_srli_epi16(lA,3); //13-bit unsigned
						lB =  _mm_loadu_si128(lineB++);
						lB = _mm_srli_epi16(lB,3); //13-bit unsigned
						lC =  _mm_loadu_si128(lineC++);
						lC = _mm_srli_epi16(lC,3); //13-bit unsigned
						lD =  _mm_loadu_si128(lineD++);
						lD = _mm_srli_epi16(lD,3); //13-bit unsigned


						o128 = _mm_mulhi_epi16(lA, gA);

						t1 = _mm_mulhi_epi16(lB, gB);
						o128 = _mm_adds_epi16(o128,t1);

						t1 = _mm_mulhi_epi16(lC, gC);
						o128 = _mm_adds_epi16(o128,t1);

						t1 = _mm_mulhi_epi16(lD, gD);
						o128 = _mm_adds_epi16(o128,t1);

						o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
						o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
						o128 = _mm_slli_epi16(o128,4);
						_mm_storeu_si128(outline128++, o128);
					}
					break;
				case DECODED_FORMAT_RGB32:
				case DECODED_FORMAT_RGB24:
				case DECODED_FORMAT_YUYV:
					for(x=0;x<widthbytes; x+=step)
					{

						lA =  _mm_loadu_si128(lineA);
						lA = _mm_unpackhi_epi8 (_mm_setzero_si128(), lA);
						lB =  _mm_loadu_si128(lineB);
						lB = _mm_unpackhi_epi8 (_mm_setzero_si128(), lB);
						lC =  _mm_loadu_si128(lineC);
						lC = _mm_unpackhi_epi8 (_mm_setzero_si128(), lC);
						lD =  _mm_loadu_si128(lineD);
						lD = _mm_unpackhi_epi8 (_mm_setzero_si128(), lD);

						lA = _mm_srli_epi16(lA,3); //13-bit unsigned
						lB = _mm_srli_epi16(lB,3); //13-bit unsigned
						lC = _mm_srli_epi16(lC,3); //13-bit unsigned
						lD = _mm_srli_epi16(lD,3); //13-bit unsigned

						o128 = _mm_mulhi_epi16(lA, gA);

						t1 = _mm_mulhi_epi16(lB, gB);
						o128 = _mm_adds_epi16(o128,t1);

						t1 = _mm_mulhi_epi16(lC, gC);
						o128 = _mm_adds_epi16(o128,t1);

						t1 = _mm_mulhi_epi16(lD, gD);
						o128 = _mm_adds_epi16(o128,t1);


						o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
						o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
						o128 = _mm_slli_epi16(o128,4);
						half = o128;

						lA =  _mm_loadu_si128(lineA++);
						lA = _mm_unpacklo_epi8 (_mm_setzero_si128(), lA);
						lB =  _mm_loadu_si128(lineB++);
						lB = _mm_unpacklo_epi8 (_mm_setzero_si128(), lB);
						lC =  _mm_loadu_si128(lineC++);
						lC = _mm_unpacklo_epi8 (_mm_setzero_si128(), lC);
						lD =  _mm_loadu_si128(lineD++);
						lD = _mm_unpacklo_epi8 (_mm_setzero_si128(), lD);

						lA = _mm_srli_epi16(lA,3); //13-bit unsigned
						lB = _mm_srli_epi16(lB,3); //13-bit unsigned
						lC = _mm_srli_epi16(lC,3); //13-bit unsigned
						lD = _mm_srli_epi16(lD,3); //13-bit unsigned


						o128 = _mm_mulhi_epi16(lA, gA);

						t1 = _mm_mulhi_epi16(lB, gB);
						o128 = _mm_adds_epi16(o128,t1);

						t1 = _mm_mulhi_epi16(lC, gC);
						o128 = _mm_adds_epi16(o128,t1);

						t1 = _mm_mulhi_epi16(lD, gD);
						o128 = _mm_adds_epi16(o128,t1);

						o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
						o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
						o128 = _mm_slli_epi16(o128,4);

						half = _mm_srli_epi16(half,8);
						o128 = _mm_srli_epi16(o128,8);
						o128 = _mm_packus_epi16(o128, half);
						_mm_storeu_si128(outline128++, o128);
					}
					break;
				}
			}
			else
			{
				if(decoder->StereoBufferFormat == DECODED_FORMAT_YUYV)
				{
					memset(dst, 0x10801080, widthbytes);
				}
				else
				{
					memset(dst, 0, widthbytes);
				}
			}

			yposf += ystepf;
			dst += pitch;
		}
		/*ptr = (unsigned char *)buffer;
		for(y=0;y<height; y++)
		{
			int r,g,b,yp = ((int)yposf);

			yposf += ystepf;

			if(yp<0 || yp>= height)
			{
				memset(dst, 0, widthbytes);
			}
			else
			{
				memcpy(dst, &ptr[widthbytes*yp], widthbytes);
			}
			dst += pitch;
		}*/
	}
}



void RGB48VerticalShiftZoomFine(DECODER *decoder, unsigned short *RGB48, unsigned short *buffer,
						int widthbytes, int height, int pitch, float offset,
						float zoom, int xx)
{
	float yposf,ystepf;
	//int endofSSEline = 0;
	unsigned short *scanline[4];
	//int spitch = pitch/2;
	int neg = 0,step;

	__m128i lA,lB,lC,lD,gA,gB,gC,gD,o128,t1;
	uint8_t *lineAPos, *lineBPos, *lineCPos, *lineDPos;
	uint8_t *outlinePos8;
	uint16_t *outlinePos16;

	offset = -offset;

	//yposf = height * offset;
	yposf = (float)height*(0.5f - 1.0f/(2.0f*zoom) - offset);
	ystepf = 1.0f/zoom;

	if(yposf < 0.0)
		neg = 1;

	if(pitch < 0)
		yposf -= ystepf;

/*	yposi = floor(yposf);

	remainf = yposf - (float)yposi;
	tablepos = (remainf*(float)SUBPIXEL);

	yposi = abs(yposi);

	if(yposi==0 && tablepos == 0)
		return; // no move required
*/
	// -3 , 0 best small notch at zero?
	//

	switch(decoder->StereoBufferFormat)
	{
	case DECODED_FORMAT_RGB32:
		step = 4;
		break;
	case DECODED_FORMAT_RGB24:
		step = 3;
		break;
	case DECODED_FORMAT_YUYV:
		step = 4;
		break;
	case DECODED_FORMAT_W13A:
	case DECODED_FORMAT_RG64:
		step = 8;
		break;
	case DECODED_FORMAT_WP13:
	case DECODED_FORMAT_RG48:
		step = 6;
		break;
	default:
		assert(0);
		break;
	}

	{
		static char zeroline[1024] = {0};
		int y,yoffset = ((int)(yposf-2.0)),yend = ((int)(yposf+2.0+ystepf*height));
		unsigned char *src = (unsigned char *)RGB48;
		unsigned char *dst = (unsigned char *)RGB48;
		unsigned char *ptr = (unsigned char *)buffer;

		if(yoffset < 0) yoffset = 0;
		if(yend > height) yend = height;

		src += pitch * yoffset;

		for(y=yoffset; y<yend; y++)
		{
			memcpy(ptr, src, widthbytes);

			ptr += widthbytes;
			src += pitch;
		}

		ptr = (unsigned char *)buffer;
		for(y=0;y<height; y++)
		{
			int i,t,yp = ((int)yposf);
			int rmdr = 63-((int)(yposf*64.0) & 63);
			int gains[4];

			yp -= 1; // use -2 cause a image down shift //DAN20100225
			t = 0;
			for(i=0; i<4; i++)
			{
				if(yp<0 || yp>= height) // skip 0 line as the top line was zagged
				{
					t += gains[i] = lanczos[rmdr];
					scanline[i] = (unsigned short *)zeroline;
				}
				else
				{
					t += gains[i] = lanczos[rmdr];
					scanline[i] = (unsigned short *)&ptr[widthbytes*(yp-yoffset)];
				}

				yp++;
				rmdr+=64;
			}

			if(t)
			{
				gA = _mm_set1_epi16(gains[0]);
				gB = _mm_set1_epi16(gains[1]);
				gC = _mm_set1_epi16(gains[2]);
				gD = _mm_set1_epi16(gains[3]);

				outlinePos8 = (uint8_t *)dst;
				outlinePos16 = (uint16_t *)dst;

				lineAPos = (uint8_t *)scanline[0];
				lineBPos = (uint8_t *)scanline[1];
				lineCPos = (uint8_t *)scanline[2];
				lineDPos = (uint8_t *)scanline[3];

				switch(decoder->StereoBufferFormat)
				{
				case DECODED_FORMAT_W13A:
					lA =  _mm_loadu_si128((__m128i *)lineAPos);	lineAPos+=8;
					lB =  _mm_loadu_si128((__m128i *)lineBPos);	lineBPos+=8;
					lC =  _mm_loadu_si128((__m128i *)lineCPos);	lineCPos+=8;
					lD =  _mm_loadu_si128((__m128i *)lineDPos);	lineDPos+=8;

					o128 = _mm_mulhi_epi16(lA, gA);

					t1 = _mm_mulhi_epi16(lB, gB);
					o128 = _mm_adds_epi16(o128,t1);

					t1 = _mm_mulhi_epi16(lC, gC);
					o128 = _mm_adds_epi16(o128,t1);

					t1 = _mm_mulhi_epi16(lD, gD);
					o128 = _mm_adds_epi16(o128,t1);

					// upper limit to 32767
					o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
					o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
					o128 = _mm_slli_epi16(o128,1);

					//_mm_storeu_si128((__m128i *)outlinePos, o128); 
					outlinePos16[0] = _mm_extract_epi16(o128, 0);
					outlinePos16[1] = _mm_extract_epi16(o128, 1);
					outlinePos16[2] = _mm_extract_epi16(o128, 2);
					outlinePos16[3] = _mm_extract_epi16(o128, 3);
					outlinePos16+=4;
					break;

				case DECODED_FORMAT_WP13:
					lA =  _mm_loadu_si128((__m128i *)lineAPos);	lineAPos+=6;
					lB =  _mm_loadu_si128((__m128i *)lineBPos);	lineBPos+=6;
					lC =  _mm_loadu_si128((__m128i *)lineCPos);	lineCPos+=6;
					lD =  _mm_loadu_si128((__m128i *)lineDPos);	lineDPos+=6;

					o128 = _mm_mulhi_epi16(lA, gA);

					t1 = _mm_mulhi_epi16(lB, gB);
					o128 = _mm_adds_epi16(o128,t1);

					t1 = _mm_mulhi_epi16(lC, gC);
					o128 = _mm_adds_epi16(o128,t1);

					t1 = _mm_mulhi_epi16(lD, gD);
					o128 = _mm_adds_epi16(o128,t1);

					// upper limit to 32767
					o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
					o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
					o128 = _mm_slli_epi16(o128,1);

					//_mm_storeu_si128((__m128i *)outlinePos, o128); 
					outlinePos16[0] = _mm_extract_epi16(o128, 0);
					outlinePos16[1] = _mm_extract_epi16(o128, 1);
					outlinePos16[2] = _mm_extract_epi16(o128, 2);
					outlinePos16+=3;
					break;

				case DECODED_FORMAT_RG64:
					lA =  _mm_loadu_si128((__m128i *)lineAPos);	lineAPos+=8;
					lB =  _mm_loadu_si128((__m128i *)lineBPos);	lineBPos+=8;
					lC =  _mm_loadu_si128((__m128i *)lineCPos);	lineCPos+=8;
					lD =  _mm_loadu_si128((__m128i *)lineDPos);	lineDPos+=8;

					lA = _mm_srli_epi16(lA,3); //13-bit unsigned
					lB = _mm_srli_epi16(lB,3); //13-bit unsigned
					lC = _mm_srli_epi16(lC,3); //13-bit unsigned
					lD = _mm_srli_epi16(lD,3); //13-bit unsigned

					o128 = _mm_mulhi_epi16(lA, gA);

					t1 = _mm_mulhi_epi16(lB, gB);
					o128 = _mm_adds_epi16(o128,t1);

					t1 = _mm_mulhi_epi16(lC, gC);
					o128 = _mm_adds_epi16(o128,t1);

					t1 = _mm_mulhi_epi16(lD, gD);
					o128 = _mm_adds_epi16(o128,t1);


					o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
					o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
					o128 = _mm_slli_epi16(o128,4);


					//_mm_storeu_si128((__m128i *)outlinePos, o128); 
					outlinePos16[0] = _mm_extract_epi16(o128, 0);
					outlinePos16[1] = _mm_extract_epi16(o128, 1);
					outlinePos16[2] = _mm_extract_epi16(o128, 2);
					outlinePos16[3] = _mm_extract_epi16(o128, 3);
					outlinePos16+=4;
					break;

				case DECODED_FORMAT_RG48:
					lA =  _mm_loadu_si128((__m128i *)lineAPos);	lineAPos+=6;
					lB =  _mm_loadu_si128((__m128i *)lineBPos);	lineBPos+=6;
					lC =  _mm_loadu_si128((__m128i *)lineCPos);	lineCPos+=6;
					lD =  _mm_loadu_si128((__m128i *)lineDPos);	lineDPos+=6;

					lA = _mm_srli_epi16(lA,3); //13-bit unsigned
					lB = _mm_srli_epi16(lB,3); //13-bit unsigned
					lC = _mm_srli_epi16(lC,3); //13-bit unsigned
					lD = _mm_srli_epi16(lD,3); //13-bit unsigned

					o128 = _mm_mulhi_epi16(lA, gA);

					t1 = _mm_mulhi_epi16(lB, gB);
					o128 = _mm_adds_epi16(o128,t1);

					t1 = _mm_mulhi_epi16(lC, gC);
					o128 = _mm_adds_epi16(o128,t1);

					t1 = _mm_mulhi_epi16(lD, gD);
					o128 = _mm_adds_epi16(o128,t1);


					o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
					o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
					o128 = _mm_slli_epi16(o128,4);


					//_mm_storeu_si128((__m128i *)outlinePos, o128); 
					outlinePos16[0] = _mm_extract_epi16(o128, 0);
					outlinePos16[1] = _mm_extract_epi16(o128, 1);
					outlinePos16[2] = _mm_extract_epi16(o128, 2);
					outlinePos16+=3;
					break;

				case DECODED_FORMAT_RGB32:
				case DECODED_FORMAT_YUYV:
					lA =  _mm_loadu_si128((__m128i *)lineAPos);	lineAPos+=4;
					lA = _mm_unpackhi_epi8 (_mm_setzero_si128(), lA);
					lB =  _mm_loadu_si128((__m128i *)lineBPos);	lineBPos+=4;
					lB = _mm_unpackhi_epi8 (_mm_setzero_si128(), lB);
					lC =  _mm_loadu_si128((__m128i *)lineCPos);	lineCPos+=4;
					lC = _mm_unpackhi_epi8 (_mm_setzero_si128(), lC);
					lD =  _mm_loadu_si128((__m128i *)lineDPos);	lineDPos+=4;
					lD = _mm_unpackhi_epi8 (_mm_setzero_si128(), lD);

					lA = _mm_srli_epi16(lA,3); //13-bit unsigned
					lB = _mm_srli_epi16(lB,3); //13-bit unsigned
					lC = _mm_srli_epi16(lC,3); //13-bit unsigned
					lD = _mm_srli_epi16(lD,3); //13-bit unsigned

					o128 = _mm_mulhi_epi16(lA, gA);

					t1 = _mm_mulhi_epi16(lB, gB);
					o128 = _mm_adds_epi16(o128,t1);

					t1 = _mm_mulhi_epi16(lC, gC);
					o128 = _mm_adds_epi16(o128,t1);

					t1 = _mm_mulhi_epi16(lD, gD);
					o128 = _mm_adds_epi16(o128,t1);


					o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
					o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
					o128 = _mm_srli_epi16(o128,4);
					outlinePos8[0] = _mm_extract_epi16(o128, 0);
					outlinePos8[1] = _mm_extract_epi16(o128, 1);
					outlinePos8[2] = _mm_extract_epi16(o128, 2);
					outlinePos8[3] = _mm_extract_epi16(o128, 3);
					outlinePos8+=4;
					break;

				case DECODED_FORMAT_RGB24:
					{
					int r,g,b;

					b = ((lineAPos[0] * gains[0])>>7) + 
						((lineBPos[0] * gains[1])>>7) + 
						((lineCPos[0] * gains[2])>>7) + 
						((lineDPos[0] * gains[3])>>7); //16-bit

					g = ((lineAPos[1] * gains[0])>>7) + 
						((lineBPos[1] * gains[1])>>7) + 
						((lineCPos[1] * gains[2])>>7) + 
						((lineDPos[1] * gains[3])>>7); //16-bit

					r = ((lineAPos[2] * gains[0])>>7) + 
						((lineBPos[2] * gains[1])>>7) + 
						((lineCPos[2] * gains[2])>>7) + 
						((lineDPos[2] * gains[3])>>7); //16-bit

					if(r<0) r = 0; if(r>65535) r = 65535;
					if(g<0) g = 0; if(g>65535) g = 65535;
					if(b<0) b = 0; if(b>65535) b = 65535;

					lineAPos+=3;
					lineBPos+=3;
					lineCPos+=3;
					lineDPos+=3;

					outlinePos8[0] = b >> 8; //b
					outlinePos8[1] = g >> 8; //g
					outlinePos8[2] = r >> 8; //r
					outlinePos8+=3;

					/* SSE2 can't load byte alligned 
					lA =  _mm_loadu_si128((__m128i *)lineAPos);	lineAPos+=3;
					lA = _mm_unpackhi_epi8 (_mm_setzero_si128(), lA);
					lB =  _mm_loadu_si128((__m128i *)lineBPos);	lineBPos+=3;
					lB = _mm_unpackhi_epi8 (_mm_setzero_si128(), lB);
					lC =  _mm_loadu_si128((__m128i *)lineCPos);	lineCPos+=3;
					lC = _mm_unpackhi_epi8 (_mm_setzero_si128(), lC);
					lD =  _mm_loadu_si128((__m128i *)lineDPos);	lineDPos+=3;
					lD = _mm_unpackhi_epi8 (_mm_setzero_si128(), lD);

					lA = _mm_srli_epi16(lA,3); //13-bit unsigned
					lB = _mm_srli_epi16(lB,3); //13-bit unsigned
					lC = _mm_srli_epi16(lC,3); //13-bit unsigned
					lD = _mm_srli_epi16(lD,3); //13-bit unsigned

					o128 = _mm_mulhi_epi16(lA, gA);

					t1 = _mm_mulhi_epi16(lB, gB);
					o128 = _mm_adds_epi16(o128,t1);

					t1 = _mm_mulhi_epi16(lC, gC);
					o128 = _mm_adds_epi16(o128,t1);

					t1 = _mm_mulhi_epi16(lD, gD);
					o128 = _mm_adds_epi16(o128,t1);


					o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
					o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
					o128 = _mm_srli_epi16(o128,4);
					outlinePos8[0] = _mm_extract_epi16(o128, 0); //b
					outlinePos8[1] = _mm_extract_epi16(o128, 1); //g
					outlinePos8[2] = _mm_extract_epi16(o128, 2); //r
					outlinePos8+=3;
					*/
					}
					break;
				}
			}
			else
			{
				if(decoder->StereoBufferFormat == DECODED_FORMAT_YUYV)
				{
					memset(dst, 0x10801080, widthbytes);
				}
				else
				{
					memset(dst, 0, widthbytes);
				}
			}

			yposf += ystepf;
			dst += pitch;
		}
	}
}



void RGB48VerticalShift(DECODER *decoder, unsigned short *RGB48, unsigned short *buffer,
						int widthbytes, int height, int pitch, float offset)
{
	float yposf,remainf;
	int yposi,tablepos,x,y;
	int gainA,gainB,gainC,gainD;
	//int endofSSEline = 0;
	unsigned short *scanline[4], *tline;
	int spitch = pitch/2;
	int neg = 0,shift = 0,skip,step;
	int origwidthbytes = widthbytes;
	int origwidthextra;

	__m128i lA, lB, lC, lD, gA, gB, gC, gD, o128, t1;
	__m128i *lineA, *lineB, *lineC, *lineD, *outline128;

//	offset = -offset;

	if(offset < 0.0)
		neg = 1;

	yposf = height * offset;
	yposi = (int)floor(yposf);

	remainf = yposf - (float)yposi;
	tablepos = (int)(remainf*(float)SUBPIXEL);

	yposi = abs(yposi);

	if(yposi==0 && tablepos == 0)
		return; // no move required

	// -3 , 0 best small notch at zero?
	//

	if(neg)
	{
		yposi -= 2;
		gainA = gains[tablepos][0];
		gainB = gains[tablepos][1];
		gainC = gains[tablepos][2];
		gainD = gains[tablepos][3];
	}
	else
	{
		yposi -= 1; //offset inherent in the table
		gainD = gains[tablepos][0];
		gainC = gains[tablepos][1];
		gainB = gains[tablepos][2];
		gainA = gains[tablepos][3];
	}

	gA = _mm_set1_epi16(gainA);
	gB = _mm_set1_epi16(gainB);
	gC = _mm_set1_epi16(gainC);
	gD = _mm_set1_epi16(gainD);


	switch(decoder->StereoBufferFormat)
	{
	case DECODED_FORMAT_RGB32:
		skip = 4;
		step = 16;
		break;
	case DECODED_FORMAT_RGB24:
		skip = 3;
		step = 16;
		break;
	case DECODED_FORMAT_YUYV:
		skip = 2;
		step = 16;
		break;
	case DECODED_FORMAT_WP13:
	case DECODED_FORMAT_RG48:
	case DECODED_FORMAT_W13A:
	case DECODED_FORMAT_RG64:
	default:
		skip = 6;
		step = 32;
		break;
	}


//	scanline[0] = buffer;
//	scanline[1] = buffer + width*skip/2;
//	scanline[2] = buffer + width*skip/2*2;
//	scanline[3] = buffer + width*skip/2*3;

	widthbytes += (step - 1);
	widthbytes -= (widthbytes % step);
	origwidthextra = (origwidthbytes % step);

	scanline[0] = buffer;
	scanline[1] = buffer + widthbytes/2;
	scanline[2] = buffer + widthbytes/2*2;
	scanline[3] = buffer + widthbytes/2*3;


	for(y=0; y<4; y++)
	{
		if(yposi+y >=0 && yposi+y<height)
		{
			unsigned short *ptr = RGB48;
			if(neg)
				ptr += (height-1-yposi-y)*spitch;
			else
				ptr += (yposi+y)*spitch;
			memcpy(scanline[y], ptr, origwidthbytes);
		}
		else
		{
			memset(scanline[y], 0, origwidthbytes);
		}
	}

	{


		for(y=0;y<height; y++)
		{
			unsigned short *ptr = RGB48;

			if(neg)
				ptr += (height-y-1)*spitch;
			else
				ptr += y*spitch;
			outline128 = (__m128i *)ptr;

			lineA = (__m128i *)scanline[0];
			lineB = (__m128i *)scanline[1];
			lineC = (__m128i *)scanline[2];
			lineD = (__m128i *)scanline[3];

			//for(x=0;x<width*skip/2; x+=step)
			for(x=0;x<widthbytes; x+=step)
			{
				__m128i half;

				switch(decoder->StereoBufferFormat)
				{
				case DECODED_FORMAT_W13A:
				case DECODED_FORMAT_WP13:
					{
						lA =  _mm_loadu_si128(lineA++);
						lB =  _mm_loadu_si128(lineB++);
						lC =  _mm_loadu_si128(lineC++);
						lD =  _mm_loadu_si128(lineD++);

						shift = 0;
					}
					break;
				case DECODED_FORMAT_RG64:
				case DECODED_FORMAT_RG48:
					{
						lA =  _mm_loadu_si128(lineA++);
						lA = _mm_srli_epi16(lA,3); //13-bit unsigned
						lB =  _mm_loadu_si128(lineB++);
						lB = _mm_srli_epi16(lB,3); //13-bit unsigned
						lC =  _mm_loadu_si128(lineC++);
						lC = _mm_srli_epi16(lC,3); //13-bit unsigned
						lD =  _mm_loadu_si128(lineD++);
						lD = _mm_srli_epi16(lD,3); //13-bit unsigned

						shift = 3;
					}
					break;
				case DECODED_FORMAT_RGB32:
				case DECODED_FORMAT_RGB24:
				case DECODED_FORMAT_YUYV:

					lA =  _mm_loadu_si128(lineA);
					lA = _mm_unpackhi_epi8 (_mm_setzero_si128(), lA);
					lB =  _mm_loadu_si128(lineB);
					lB = _mm_unpackhi_epi8 (_mm_setzero_si128(), lB);
					lC =  _mm_loadu_si128(lineC);
					lC = _mm_unpackhi_epi8 (_mm_setzero_si128(), lC);
					lD =  _mm_loadu_si128(lineD);
					lD = _mm_unpackhi_epi8 (_mm_setzero_si128(), lD);

					lA = _mm_srli_epi16(lA,3); //13-bit unsigned
					lB = _mm_srli_epi16(lB,3); //13-bit unsigned
					lC = _mm_srli_epi16(lC,3); //13-bit unsigned
					lD = _mm_srli_epi16(lD,3); //13-bit unsigned

					shift = 3;

					break;
				}

				o128 = _mm_mulhi_epi16(lA, gA);

				t1 = _mm_mulhi_epi16(lB, gB);
				o128 = _mm_adds_epi16(o128,t1);

				t1 = _mm_mulhi_epi16(lC, gC);
				o128 = _mm_adds_epi16(o128,t1);

				t1 = _mm_mulhi_epi16(lD, gD);
				o128 = _mm_adds_epi16(o128,t1);

				if(shift)
				{
					o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
					o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
					o128 = _mm_slli_epi16(o128,4);
				}
				else
				{			
					// upper limit to 32767
					o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
					o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
					o128 = _mm_slli_epi16(o128,1);
				}

				if(skip == 6) //RGB48 || WP13
				{						
					if(widthbytes == origwidthbytes || x+16 < origwidthbytes)
						_mm_storeu_si128(outline128++, o128);
					else
					{
						//if(x < origwidthbytes+16/*bytes in an SSE2 reg*/)
						_mm_storeu_si128((__m128i *)scanline[0], o128);
						memcpy((char *)outline128, (char *)scanline[0], origwidthextra);
						outline128++;
					}
				}
				else
				{
					half = o128;
				}





				switch(decoder->StereoBufferFormat)
				{
				case DECODED_FORMAT_W13A:
				case DECODED_FORMAT_WP13:
					{
						lA =  _mm_loadu_si128(lineA++);
						lB =  _mm_loadu_si128(lineB++);
						lC =  _mm_loadu_si128(lineC++);
						lD =  _mm_loadu_si128(lineD++);

						shift = 0;
					}
					break;
				case DECODED_FORMAT_RG64:
				case DECODED_FORMAT_RG48:
					{
						lA =  _mm_loadu_si128(lineA++);
						lA = _mm_srli_epi16(lA,3); //13-bit unsigned
						lB =  _mm_loadu_si128(lineB++);
						lB = _mm_srli_epi16(lB,3); //13-bit unsigned
						lC =  _mm_loadu_si128(lineC++);
						lC = _mm_srli_epi16(lC,3); //13-bit unsigned
						lD =  _mm_loadu_si128(lineD++);
						lD = _mm_srli_epi16(lD,3); //13-bit unsigned

						shift = 3;
					}
					break;
				case DECODED_FORMAT_RGB32:
				case DECODED_FORMAT_RGB24:
				case DECODED_FORMAT_YUYV:

					lA =  _mm_loadu_si128(lineA++);
					lA = _mm_unpacklo_epi8 (_mm_setzero_si128(), lA);
					lB =  _mm_loadu_si128(lineB++);
					lB = _mm_unpacklo_epi8 (_mm_setzero_si128(), lB);
					lC =  _mm_loadu_si128(lineC++);
					lC = _mm_unpacklo_epi8 (_mm_setzero_si128(), lC);
					lD =  _mm_loadu_si128(lineD++);
					lD = _mm_unpacklo_epi8 (_mm_setzero_si128(), lD);

					lA = _mm_srli_epi16(lA,3); //13-bit unsigned
					lB = _mm_srli_epi16(lB,3); //13-bit unsigned
					lC = _mm_srli_epi16(lC,3); //13-bit unsigned
					lD = _mm_srli_epi16(lD,3); //13-bit unsigned

					shift = 3;

					break;
				}

				o128 = _mm_mulhi_epi16(lA, gA);

				t1 = _mm_mulhi_epi16(lB, gB);
				o128 = _mm_adds_epi16(o128,t1);

				t1 = _mm_mulhi_epi16(lC, gC);
				o128 = _mm_adds_epi16(o128,t1);

				t1 = _mm_mulhi_epi16(lD, gD);
				o128 = _mm_adds_epi16(o128,t1);

				if(shift)
				{
					o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
					o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
					o128 = _mm_slli_epi16(o128,4);
				}
				else
				{
					// upper limit to 32767
					o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
					o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
					o128 = _mm_slli_epi16(o128,1);
				}

				if(skip != 6) //!RGB48 || !WP13
				{
					half = _mm_srli_epi16(half,8);
					o128 = _mm_srli_epi16(o128,8);
					o128 = _mm_packus_epi16(o128, half);
				}

				if(widthbytes == origwidthbytes || x+32 < origwidthbytes)
				{	
					_mm_storeu_si128(outline128++, o128);
				}
				else
				{
					//if(x+16 < origwidthbytes+16)
				
					if(origwidthextra > 16)
					{
						_mm_storeu_si128((__m128i *)scanline[0], o128);
						memcpy((char *)outline128, (char *)scanline[0], origwidthextra - 16);
					}
					outline128++;
				}
			}

			tline = scanline[0];
			scanline[0] = scanline[1];
			scanline[1] = scanline[2];
			scanline[2] = scanline[3];
			scanline[3] = tline;

			if(yposi+y+4 >=0 && yposi+y+4<height)
			{
				unsigned short *ptr = RGB48;
				if(neg)
					ptr += (height-1-(yposi+y+4))*spitch;
				else
					ptr += (yposi+y+4)*spitch;
				memcpy(scanline[3], ptr, origwidthbytes);
			}
			else
			{
				memset(scanline[3], 0, origwidthbytes);
			}
		}
	}
}




void RGB48HoriShiftZoom(DECODER *decoder, unsigned short *RGB48, unsigned short *buffer, int width, int height, int line, float hoffset, float roffset, float zoom, int flip, float frameTilt, int eye)
{
	float xposf,xstepf;
	int x;
	//int endofSSEline = 0;
	unsigned short *scanline = (unsigned short *)buffer;
	short *sscanline = (short *)buffer;
	int neg = 0;
	float offset = hoffset;

	if(flip)
	{
		unsigned short *ptrL = RGB48;
		unsigned short *ptrR = RGB48;
		ptrR += (width*3) - 3;
		for(x=0;x<width/2;x++)
		{
			int t;

			t = *ptrL;
			*ptrL++ = *ptrR;
			*ptrR++ = t;
			t = *ptrL;
			*ptrL++ = *ptrR;
			*ptrR++ = t;
			t = *ptrL;
			*ptrL++ = *ptrR;
			*ptrR++ = t;
			ptrR -= 6;
		}
	}


	if(eye > 0)
	{
		zoom *= 1.0f + frameTilt;
	}
	else
	{
		zoom /= 1.0f + frameTilt;
	}


	xposf = (float)width*(0.5f - 1.0f/(2.0f*zoom) - offset);
	xposf -= width * roffset * 0.5f / zoom;
	xposf += (float)line * ((float)width* roffset / ((float)height*zoom));

	if(xposf < 0.0)
		neg = 1;

	xstepf = 1.0f/zoom;


	memcpy(scanline, RGB48, width*3*2);
	{
		//unsigned short zeroline[3] = {0};
		int xx = 0;
		int ixpos = (int)(xposf * 65536.0f);
		int ixstep = (int)(xstepf * 65536.0f);
		float xbase = xposf / (float)width;
		float xstep = xstepf / (float)width;
		float z = (decoder->cfhddata.FrameHDynamic - 1.0f)*2.0f;
	//	int holdstart = width*5/10; // Use to specify a area of uniform stretch
	//	int holdend = width*5/10;
		int holdstart = (int)((decoder->cfhddata.FrameHDynCenter - decoder->cfhddata.FrameHDynWidth*0.125)*(float)width);
		int holdend = (int)((decoder->cfhddata.FrameHDynCenter + decoder->cfhddata.FrameHDynWidth*0.125)*(float)width);
		float flatxstep;
		float modified_xstep_avg;
	    float bottomxstep;
	    float basexstepstart;
	    float basexstepend;
		float range;
#if MMXSUPPORTED //TODO DANREMOVE
		__m64 overflowprotect = _mm_set1_pi16(0x7fff-0x3fff);
#endif

		if(holdstart < 0) holdstart = 0, holdend = (int)((decoder->cfhddata.FrameHDynWidth*0.5)*(float)width);
		if(holdend > width) holdend = width, holdstart = (int)((1.0 - decoder->cfhddata.FrameHDynWidth*0.5)*(float)width);

		range = (float)(holdend - holdstart);

		flatxstep = xstep-z*0.5f*xstep;
		modified_xstep_avg = (xstep * (float)width - range * flatxstep) / ((float)width - range);
	    bottomxstep = modified_xstep_avg - (flatxstep - modified_xstep_avg);

		if(holdstart == (width-holdend))
		{
			basexstepstart = bottomxstep;
			basexstepend = bottomxstep;
		}
		else if(holdstart < (width-holdend))
		{
			float a = (float)holdstart / (float)(width-holdend);
			float startavg = a * modified_xstep_avg + (1.0f - a) * flatxstep;
			float endavg = (modified_xstep_avg * ((float)width-range) - startavg * (float)holdstart) / (float)(width-holdend);

			basexstepstart = startavg - (flatxstep - startavg);
			basexstepend = endavg - (flatxstep - endavg);

		}
		else
		{
			float a = (float)(width-holdend) / (float)holdstart;
			float endavg = a * modified_xstep_avg + (1.0f - a) * flatxstep;
			float startavg = (modified_xstep_avg * ((float)width-range) - endavg * (float)(width-holdend)) / (float)holdstart;

			basexstepstart = startavg - (flatxstep - startavg);
			basexstepend = endavg - (flatxstep - endavg);

		}


		if(decoder->StereoBufferFormat == DECODED_FORMAT_WP13)
		{
			float fxpos = xbase;

			for(x=0;x<width; x++) //RGB
			{
				int gains = 0;
				int xp, rmdr;

				if(z != 0.0)
				{
					if(x<holdstart)
					{
						fxpos += basexstepstart*((float)(holdstart-x)/(float)holdstart) + flatxstep*((float)x/(float)holdstart);
					}
					else if(x>holdend)
 					{
						int diff = width - x;
						int range = width - holdend;
						fxpos += basexstepend*((float)(range-diff)/(float)range) + flatxstep*((float)(diff)/(float)range);
					}
					else
					{
						fxpos += flatxstep;
					}

					xp = (int)(fxpos * 65536.0f*(float)width);
					rmdr = 63-((xp>>10) & 63);
					xp >>= 16;
				}
				else
				{
					xp = ixpos>>16;
					rmdr = 63-((ixpos>>10) & 63);
					ixpos += ixstep;
				}

				xp -= 1;// was -2 causing a right shift //DAN20100225
#if MMXSUPPORTED //TODO DANREMOVE
				if(xp>4 && xp<width-4 && xx < (width-1)*3) //We need 3 values for RGB< yet we write 4, so the last pixel can't be done with MMX
				{
					__m64 *src64;
					__m64 *dst64;
					__m64 sumx16;
					__m64 rgbx16;
					__m64 gain16;
					int linepos = (xp-1)*3;

					src64 = (__m64 *)&sscanline[linepos];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr]); //15-bit
					sumx16 = _mm_mulhi_pi16(rgbx16, gain16); //13*15-bit

					src64 = (__m64 *)&sscanline[linepos+3];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+64]); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //13*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					src64 = (__m64 *)&sscanline[linepos+6];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+128]); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //13*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					src64 = (__m64 *)&sscanline[linepos+9];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+192]); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //13*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					sumx16 = _mm_adds_pi16(sumx16, overflowprotect);
					sumx16 = _mm_subs_pu16(sumx16, overflowprotect);

					sumx16 = _mm_slli_pi16(sumx16, 1);

					dst64 = (__m64 *)&RGB48[xx];
					*dst64 = sumx16;
				}
				else
#endif
				{
					int i,r=0,g=0,b=0;

					for(i=0; i<4; i++)
					{
						if(xp<=0 || xp>= width)
						{
							gains += lanczos[rmdr]>>1;
						}
						else
						{
							gains += lanczos[rmdr]>>1;
							r += (gains * sscanline[xp*3]);
							g += (gains * sscanline[xp*3+1]);
							b += (gains * sscanline[xp*3+2]);
							gains = 0;
						}

						xp++;
						rmdr+=64;
					}
					r >>= 14;
					g >>= 14;
					b >>= 14;
					if(r<0) r=0; else if(r>65535) r=65535;
					if(g<0) g=0; else if(g>65535) g=65535;
					if(b<0) b=0; else if(b>65535) b=65535;
					RGB48[xx] = r;
					RGB48[xx+1] = g;
					RGB48[xx+2] = b;
				}
				xx+=3;
			}
		}
		else
		{	
			float fxpos = xbase;

			for(x=0;x<width; x++) //RGB
			{
				int gains = 0;
				int xp, rmdr;

				if(z != 0.0)
				{
					if(x<holdstart)
					{
						fxpos += basexstepstart*((float)(holdstart-x)/(float)holdstart) + flatxstep*((float)x/(float)holdstart);
					}
					else if(x>holdend)
 					{
						int diff = width - x;
						int range = width - holdend;
						fxpos += basexstepend*((float)(range-diff)/(float)range) + flatxstep*((float)(diff)/(float)range);
					}
					else
					{
						fxpos += flatxstep;
					}

					xp = (int)(fxpos * 65536.0f*(float)width);
					rmdr = 63-((xp>>10) & 63);
					xp >>= 16;
				}
				else
				{
					xp = ixpos>>16;
					rmdr = 63-((ixpos>>10) & 63);
					ixpos += ixstep;
				}

				xp -= 1; // was -2 causing a right shift //DAN20100225
#if MMXSUPPORTED //TODO DANREMOVE
				if(xp>4 && xp<width-4)
				{
					__m64 *src64;
					__m64 *dst64;
					__m64 sumx16;
					__m64 rgbx16;
					__m64 gain16;
					int linepos = (xp-0)*3; //DAN20102602 -- fix left edge error.

					src64 = (__m64 *)&scanline[linepos];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr]); //15-bit
					rgbx16 = _mm_srli_pi16(rgbx16, 1); //15-bit
					sumx16 = _mm_mulhi_pi16(rgbx16, gain16); //15*15-bit

					src64 = (__m64 *)&scanline[linepos+3];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+64]); //15-bit
					rgbx16 = _mm_srli_pi16(rgbx16, 1); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //15*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					src64 = (__m64 *)&scanline[linepos+6];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+128]); //15-bit
					rgbx16 = _mm_srli_pi16(rgbx16, 1); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //15*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					src64 = (__m64 *)&scanline[linepos+9];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+192]); //15-bit
					rgbx16 = _mm_srli_pi16(rgbx16, 1); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //15*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					sumx16 = _mm_adds_pi16(sumx16, overflowprotect);
					sumx16 = _mm_subs_pu16(sumx16, overflowprotect);

					sumx16 = _mm_slli_pi16(sumx16, 2);

					dst64 = (__m64 *)&RGB48[xx];
					*dst64 = sumx16;
				}
				else
#endif
				{
					int i,r=0,g=0,b=0;

					for(i=0; i<4; i++)
					{
						if(xp<=0 || xp>= width)
						{
							gains += lanczos[rmdr]>>1;
						}
						else
						{
							gains += lanczos[rmdr]>>1;
							r += (gains * scanline[xp*3]);
							g += (gains * scanline[xp*3+1]);
							b += (gains * scanline[xp*3+2]);
							gains = 0;
						}

						xp++;
						rmdr+=64;
					}
					r >>= 14;
					g >>= 14;
					b >>= 14;
					if(r<0) r=0; else if(r>65535) r=65535;
					if(g<0) g=0; else if(g>65535) g=65535;
					if(b<0) b=0; else if(b>65535) b=65535;
					RGB48[xx] = r;
					RGB48[xx+1] = g;
					RGB48[xx+2] = b;
				}
				xx+=3;
			}
		}
	}

#if MMXSUPPORTED //TODO DANREMOVE
	//_mm_empty();
#endif
}


#if 0 //Why is this not used? 
void RGB48HoriShiftZoomFine(DECODER *decoder, unsigned short *RGB48, unsigned short *buffer, int width, int height, int line, float hoffset, float roffset, float zoom, int flip, float frameTilt, int eye)
{
	float xposf,remainf,xstepf;
	int xposi,tablepos,x;
	int Ra,Rb,Rc,Rd;
	int Ga,Gb,Gc,Gd;
	int Ba,Bb,Bc,Bd;
	int gainA,gainB,gainC,gainD;
	int endofSSEline = 0;
	unsigned short *scanline = (unsigned short *)buffer;
	short *sscanline = (short *)buffer;
	int neg = 0,shift = 0;
	float offset = hoffset;

	__m128i l1,l2,l3,gA,gB,gC,gD,o128,t1,t2;
	__m128i *line128, *outline128;

	if(flip)
	{
		unsigned short *ptrL = RGB48;
		unsigned short *ptrR = RGB48;
		ptrR += (width*3) - 3;
		for(x=0;x<width/2;x++)
		{
			int t;

			t = *ptrL;
			*ptrL++ = *ptrR;
			*ptrR++ = t;
			t = *ptrL;
			*ptrL++ = *ptrR;
			*ptrR++ = t;
			t = *ptrL;
			*ptrL++ = *ptrR;
			*ptrR++ = t;
			ptrR -= 6;
		}
	}


	if(eye > 0)
	{
		zoom *= 1.0 + frameTilt;
	}
	else
	{
		zoom /= 1.0 + frameTilt;
	}


	xposf = (float)width*(0.5 - 1.0/(2.0*zoom) - offset);
	xposf -= width * roffset * 0.5 / zoom;
	xposf += (float)line * ((float)width* roffset / ((float)height*zoom));

	if(xposf < 0.0)
		neg = 1;

	xstepf = 1.0/zoom;

	memcpy(scanline, RGB48, width*3*2);
	{
		unsigned short zeroline[3] = {0};
		int xx = 0;
		int ixpos = xposf * 65536.0;
		int ixstep = xstepf * 65536.0;
		float xbase = xposf / (float)width;
		float xstep = xstepf / (float)width;
		float z = (decoder->cfhddata.FrameHDynamic - 1.0)*2.0;
		int holdstart = width*5/10; // Use to specify a area of uniform stretch
		int holdend = width*5/10;
		float flatxstep = xstep-z*0.5*xstep;
		float modified_xstep_avg = (xstep * (float)width - (float)(holdend - holdstart) * flatxstep) / (float)(width - (holdend - holdstart));
	    float bottomxstep = modified_xstep_avg - (flatxstep- modified_xstep_avg);
		__m64 overflowprotect = _mm_set1_pi16(0x7fff-0x3fff);

		if(bottomxstep < 0.0)
		{
			bottomxstep = 0.0;
			flatxstep = modified_xstep_avg + modified_xstep_avg;
		}
		if(flatxstep < 0.0)
		{
			flatxstep = 0.0;
			bottomxstep = modified_xstep_avg - (flatxstep- modified_xstep_avg);
		}

		if(decoder->StereoBufferFormat == DECODED_FORMAT_WP13)
		{
			float fxpos = xbase;

			for(x=0;x<width; x++) //RGB
			{
				int gains = 0;
				int xp, rmdr;

				if(z != 0.0)
				{					
					if(x<holdstart)
					{
						fxpos += bottomxstep*((float)(holdstart-x)/(float)holdstart) + flatxstep*((float)x/(float)holdstart);
					}
					else if(x>holdend)
 					{
						int diff = width - x;
						int range = width - holdend;
						fxpos += bottomxstep*((float)(range-diff)/(float)range) + flatxstep*((float)(diff)/(float)range);
					}
					else
					{
						fxpos += flatxstep;
					}
				/*	fxpos = xbase + xstep * x;//(float)ixpos/(65536.0*(float)width);
					
					if(fxpos >= 0.0 && fxpos <= 1.0)
					{
						if(z > 0.0)
						{
							fxpos = 1.8*fxpos - 2.4*fxpos*fxpos + (1.6*fxpos*fxpos*fxpos);
							fxpos = fxpos * (z) + (xbase + xstep * x) * (1.0-z);
						}
						else
						{
							fxpos = 3.0*fxpos*fxpos - 2.0*fxpos*fxpos*fxpos;
							fxpos = fxpos * (-z) + (xbase + xstep * x) * (1.0+z);
						}
					}
				*/

					xp = (fxpos * 65536.0*(float)width);
					rmdr = 63-((xp>>10) & 63);
					xp >>= 16;
				}
				else
				{
					xp = ixpos>>16;
					rmdr = 63-((ixpos>>10) & 63);
					ixpos += ixstep;
				}

				xp -= 1;// was -2 causing a right shift //DAN20100225
				if(xp>4 && xp<width-4)
				{
					__m64 *src64;
					__m64 *dst64;
					__m64 sumx16;
					__m64 rgbx16;
					__m64 gain16;
					int linepos = (xp-1)*3;

					src64 = (__m64 *)&sscanline[linepos];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr]); //15-bit
					sumx16 = _mm_mulhi_pi16(rgbx16, gain16); //13*15-bit

					src64 = (__m64 *)&sscanline[linepos+3];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+64]); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //13*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					src64 = (__m64 *)&sscanline[linepos+6];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+128]); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //13*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					src64 = (__m64 *)&sscanline[linepos+9];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+192]); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //13*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					sumx16 = _mm_adds_pi16(sumx16, overflowprotect);
					sumx16 = _mm_subs_pu16(sumx16, overflowprotect);

					sumx16 = _mm_slli_pi16(sumx16, 1);

					dst64 = (__m64 *)&RGB48[xx];
					*dst64 = sumx16;
				}
				else
				{
					int i,t,r=0,g=0,b=0;

					for(i=0; i<4; i++)
					{
						if(xp<=0 || xp>= width)
						{
						/*	if(i == 3) //DAN20101112 this code was crashing disparity zoom
							{
								gains = lanczos[rmdr]>>1;
								r += (gains * sscanline[(xp-1)*3]);
								g += (gains * sscanline[(xp-1)*3+1]);
								b += (gains * sscanline[(xp-1)*3+2]);
							}
							else */
							{
								gains += lanczos[rmdr]>>1;
							}
						}
						else
						{
							gains += lanczos[rmdr]>>1;
							r += (gains * sscanline[xp*3]);
							g += (gains * sscanline[xp*3+1]);
							b += (gains * sscanline[xp*3+2]);
							gains = 0;
						}

						xp++;
						rmdr+=64;
					}
					r >>= 14;
					g >>= 14;
					b >>= 14;
					if(r<0) r=0; else if(r>65535) r=65535;
					if(g<0) g=0; else if(g>65535) g=65535;
					if(b<0) b=0; else if(b>65535) b=65535;
					RGB48[xx] = r;
					RGB48[xx+1] = g;
					RGB48[xx+2] = b;
				}
				xx+=3;
			}
		}
		else
		{	
			float fxpos = xbase;

			for(x=0;x<width; x++) //RGB
			{
				int gains = 0;
				int xp, rmdr;

				if(z != 0.0)
				{
					if(x<holdstart)
					{
						fxpos += bottomxstep*((float)(holdstart-x)/(float)holdstart) + flatxstep*((float)x/(float)holdstart);
					}
					else if(x>holdend)
 					{
						int diff = width - x;
						int range = width - holdend;
						fxpos += bottomxstep*((float)(range-diff)/(float)range) + flatxstep*((float)(diff)/(float)range);
					}
					else
					{
						fxpos += flatxstep;
					}
				/*	fxpos = xbase + xstep * x;//(float)ixpos/(65536.0*(float)width);
					
					if(fxpos >= 0.0 && fxpos <= 1.0)
					{
						if(z > 0.0)
						{
							fxpos = 1.8*fxpos - 2.4*fxpos*fxpos + (1.6*fxpos*fxpos*fxpos);
							fxpos = fxpos * (z) + (xbase + xstep * x) * (1.0-z);
						}
						else
						{
							fxpos = 3.0*fxpos*fxpos - 2.0*fxpos*fxpos*fxpos;
							fxpos = fxpos * (-z) + (xbase + xstep * x) * (1.0+z);
						}
					}
				*/

					xp = (fxpos * 65536.0*(float)width);
					rmdr = 63-((xp>>10) & 63);
					xp >>= 16;
				}
				else
				{
					xp = ixpos>>16;
					rmdr = 63-((ixpos>>10) & 63);
					ixpos += ixstep;
				}

				xp -= 1; // was -2 causing a right shift //DAN20100225
				if(xp>4 && xp<width-4)
				{
					__m64 *src64;
					__m64 *dst64;
					__m64 sumx16;
					__m64 rgbx16;
					__m64 gain16;
					int linepos = (xp-0)*3; //DAN20102602 -- fix left edge error.

					src64 = (__m64 *)&scanline[linepos];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr]); //15-bit
					rgbx16 = _mm_srli_pi16(rgbx16, 1); //15-bit
					sumx16 = _mm_mulhi_pi16(rgbx16, gain16); //15*15-bit

					src64 = (__m64 *)&scanline[linepos+3];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+64]); //15-bit
					rgbx16 = _mm_srli_pi16(rgbx16, 1); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //15*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					src64 = (__m64 *)&scanline[linepos+6];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+128]); //15-bit
					rgbx16 = _mm_srli_pi16(rgbx16, 1); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //15*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					src64 = (__m64 *)&scanline[linepos+9];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+192]); //15-bit
					rgbx16 = _mm_srli_pi16(rgbx16, 1); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //15*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					sumx16 = _mm_adds_pi16(sumx16, overflowprotect);
					sumx16 = _mm_subs_pu16(sumx16, overflowprotect);

					sumx16 = _mm_slli_pi16(sumx16, 2);

					dst64 = (__m64 *)&RGB48[xx];
					*dst64 = sumx16;
				}
				else
				{
					int i,t,r=0,g=0,b=0;

					for(i=0; i<4; i++)
					{
						if(xp<=0 || xp>= width)
						{
						/*	if(i == 3) //DAN20101112 this code was crashing disparity zoom
							{
								gains = lanczos[rmdr]>>1;
								r += (gains * scanline[(xp-1)*3]);
								g += (gains * scanline[(xp-1)*3+1]);
								b += (gains * scanline[(xp-1)*3+2]);
							}
							else */
							{
								gains += lanczos[rmdr]>>1;
							}
						}
						else
						{
							gains += lanczos[rmdr]>>1;
							r += (gains * scanline[xp*3]);
							g += (gains * scanline[xp*3+1]);
							b += (gains * scanline[xp*3+2]);
							gains = 0;
						}

						xp++;
						rmdr+=64;
					}
					r >>= 14;
					g >>= 14;
					b >>= 14;
					if(r<0) r=0; else if(r>65535) r=65535;
					if(g<0) g=0; else if(g>65535) g=65535;
					if(b<0) b=0; else if(b>65535) b=65535;
					RGB48[xx] = r;
					RGB48[xx+1] = g;
					RGB48[xx+2] = b;
				}
				xx+=3;
			}
		}
	}

/*
	memcpy(scanline, RGB48, width*3*2);
	{
		for(x=0;x<width*3; x+=3) //RGB
		{
			int r,g,b,xp = ((int)xposf)*3;

			xposf += xstepf;

			if(xp<0 || xp>= width*3)
			{
				RGB48[x] = 0;
				RGB48[x+1] = 0;
				RGB48[x+2] = 0;
			}
			else
			{
				r = scanline[xp];
				g = scanline[xp+1];
				b = scanline[xp+2];

				RGB48[x] = r;
				RGB48[x+1] = g;
				RGB48[x+2] = b;
			}
		}
	}
*/

	//_mm_empty();
}
#endif

void RGBA64HoriShiftZoom(DECODER *decoder, unsigned short *RGB48, unsigned short *buffer, int width, int height, int line, float hoffset, float roffset, float zoom, int flip, float frameTilt, int eye)
{
	float xposf,xstepf;
	int x;
	//int endofSSEline = 0;
	unsigned short *scanline = (unsigned short *)buffer;
	short *sscanline = (short *)buffer;
	int neg = 0;
	float offset = hoffset;

	if(flip)
	{
		unsigned short *ptrL = RGB48;
		unsigned short *ptrR = RGB48;
		ptrR += (width*4) - 4;
		for(x=0;x<width/2;x++)
		{
			int t;

			t = *ptrL;
			*ptrL++ = *ptrR;
			*ptrR++ = t;
			t = *ptrL;
			*ptrL++ = *ptrR;
			*ptrR++ = t;
			t = *ptrL;
			*ptrL++ = *ptrR;
			*ptrR++ = t;
			t = *ptrL;
			*ptrL++ = *ptrR;
			*ptrR++ = t;
			ptrR -= 4;
		}
	}


	if(eye > 0)
	{
		zoom *= 1.0f + frameTilt;
	}
	else
	{
		zoom /= 1.0f + frameTilt;
	}


	xposf = (float)width*(0.5f - 1.0f/(2.0f*zoom) - offset);
	xposf -= width * roffset * 0.5f;
	xposf += line * (width* roffset / ((float)height*zoom));

	if(xposf < 0.0)
		neg = 1;

	xstepf = 1.0f/zoom;


	memcpy(scanline, RGB48, width*4*2);
	{
		//unsigned short zeroline[3] = {0};
		int xx = 0;
		int ixpos = (int)(xposf * 65536.0f);
		int ixstep = (int)(xstepf * 65536.0f);
		float xbase = xposf / (float)width;
		float xstep = xstepf / (float)width;
		float z = (decoder->cfhddata.FrameHDynamic - 1.0f)*2.0f;
		int holdstart = width*5/10; // Use to specify a area of uniform stretch
		int holdend = width*5/10;
		float flatxstep = xstep-z*0.5f*xstep;
		float modified_xstep_avg = (xstep * (float)width - (float)(holdend - holdstart) * flatxstep) / (float)(width - (holdend - holdstart));
	    float bottomxstep = modified_xstep_avg - (flatxstep- modified_xstep_avg);
#if MMXSUPPORTED //TODO DANREMOVE
		__m64 overflowprotect = _mm_set1_pi16(0x7fff-0x3fff);
#endif

		if(bottomxstep < 0.0)
		{
			bottomxstep = 0.0;
			flatxstep = modified_xstep_avg + modified_xstep_avg;
		}
		if(flatxstep < 0.0)
		{
			flatxstep = 0.0;
			bottomxstep = modified_xstep_avg - (flatxstep- modified_xstep_avg);
		}

		if(decoder->StereoBufferFormat == DECODED_FORMAT_W13A)
		{
			float fxpos = xbase;

			for(x=0;x<width; x++) //RGB
			{
				int gains = 0;
				int xp, rmdr;

				if(z != 0.0)
				{
					if(x<holdstart)
					{
						fxpos += bottomxstep*((float)(holdstart-x)/(float)holdstart) + flatxstep*((float)x/(float)holdstart);
					}
					else if(x>holdend)
 					{
						int diff = width - x;
						int range = width - holdend;
						fxpos += bottomxstep*((float)(range-diff)/(float)range) + flatxstep*((float)(diff)/(float)range);
					}
					else
					{
						fxpos += flatxstep;
					}

					xp = (int)(fxpos * 65536.0f*(float)width);
					rmdr = 63-((xp>>10) & 63);
					xp >>= 16;
				}
				else
				{
					xp = ixpos>>16;
					rmdr = 63-((ixpos>>10) & 63);
					ixpos += ixstep;
				}

				xp -= 1;// was -2 causing a right shift //DAN20100225
#if MMXSUPPORTED //TODO DANREMOVE
				if(xp>4 && xp<width-4)
				{
					__m64 *src64;
					__m64 *dst64;
					__m64 sumx16;
					__m64 rgbx16;
					__m64 gain16;
					int linepos = (xp-1)*4;

					src64 = (__m64 *)&sscanline[linepos];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr]); //15-bit
					sumx16 = _mm_mulhi_pi16(rgbx16, gain16); //13*15-bit

					src64 = (__m64 *)&sscanline[linepos+4];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+64]); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //13*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					src64 = (__m64 *)&sscanline[linepos+8];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+128]); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //13*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					src64 = (__m64 *)&sscanline[linepos+12];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+192]); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //13*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					sumx16 = _mm_adds_pi16(sumx16, overflowprotect);
					sumx16 = _mm_subs_pu16(sumx16, overflowprotect);

					sumx16 = _mm_slli_pi16(sumx16, 1);

					dst64 = (__m64 *)&RGB48[xx];
					*dst64 = sumx16;
				}
				else
#endif
				{
					int i,r=0,g=0,b=0,a=0;

					for(i=0; i<4; i++)
					{
						if(xp<=0 || xp>= width)
						{
							gains += lanczos[rmdr]>>1;
						}
						else
						{
							gains += lanczos[rmdr]>>1;
							r += (gains * sscanline[xp*4]);
							g += (gains * sscanline[xp*4+1]);
							b += (gains * sscanline[xp*4+2]);
							a += (gains * sscanline[xp*4+3]);
							gains = 0;
						}

						xp++;
						rmdr+=64;
					}
					r >>= 14;
					g >>= 14;
					b >>= 14;
					a >>= 14;
					if(r<0) r=0; else if(r>65535) r=65535;
					if(g<0) g=0; else if(g>65535) g=65535;
					if(b<0) b=0; else if(b>65535) b=65535;
					if(a<0) a=0; else if(a>65535) a=65535;
					RGB48[xx] = r;
					RGB48[xx+1] = g;
					RGB48[xx+2] = b;
					RGB48[xx+3] = a;
				}
				xx+=4;
			}
		}
		else
		{	
			float fxpos = xbase;

			for(x=0;x<width; x++) //RGB
			{
				int gains = 0;
				int xp, rmdr;

				if(z != 0.0)
				{
					if(x<holdstart)
					{
						fxpos += bottomxstep*((float)(holdstart-x)/(float)holdstart) + flatxstep*((float)x/(float)holdstart);
					}
					else if(x>holdend)
 					{
						int diff = width - x;
						int range = width - holdend;
						fxpos += bottomxstep*((float)(range-diff)/(float)range) + flatxstep*((float)(diff)/(float)range);
					}
					else
					{
						fxpos += flatxstep;
					}

					xp = (int)(fxpos * 65536.0f*(float)width);
					rmdr = 63-((xp>>10) & 63);
					xp >>= 16;
				}
				else
				{
					xp = ixpos>>16;
					rmdr = 63-((ixpos>>10) & 63);
					ixpos += ixstep;
				}

				xp -= 1; // was -2 causing a right shift //DAN20100225
#if MMXSUPPORTED //TODO DANREMOVE
				if(xp>4 && xp<width-4)
				{
					__m64 *src64;
					__m64 *dst64;
					__m64 sumx16;
					__m64 rgbx16;
					__m64 gain16;
					int linepos = (xp-0)*4; //DAN20102602 -- fix left edge error.

					src64 = (__m64 *)&scanline[linepos];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr]); //15-bit
					rgbx16 = _mm_srli_pi16(rgbx16, 1); //15-bit
					sumx16 = _mm_mulhi_pi16(rgbx16, gain16); //15*15-bit

					src64 = (__m64 *)&scanline[linepos+4];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+64]); //15-bit
					rgbx16 = _mm_srli_pi16(rgbx16, 1); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //15*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					src64 = (__m64 *)&scanline[linepos+8];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+128]); //15-bit
					rgbx16 = _mm_srli_pi16(rgbx16, 1); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //15*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					src64 = (__m64 *)&scanline[linepos+12];
					rgbx16 = *src64;
					gain16 = _mm_set1_pi16(lanczos[rmdr+192]); //15-bit
					rgbx16 = _mm_srli_pi16(rgbx16, 1); //15-bit
					rgbx16 = _mm_mulhi_pi16(rgbx16, gain16); //15*15-bit
					sumx16 = _mm_adds_pi16(sumx16, rgbx16);

					sumx16 = _mm_adds_pi16(sumx16, overflowprotect);
					sumx16 = _mm_subs_pu16(sumx16, overflowprotect);

					sumx16 = _mm_slli_pi16(sumx16, 2);

					dst64 = (__m64 *)&RGB48[xx];
					*dst64 = sumx16;
				}
				else
#endif
				{
					int i,r=0,g=0,b=0,a=0;

					for(i=0; i<4; i++)
					{
						if(xp<=0 || xp>= width)
						{
							gains += lanczos[rmdr]>>1;
						}
						else
						{
							gains += lanczos[rmdr]>>1;
							r += (gains * scanline[xp*4]);
							g += (gains * scanline[xp*4+1]);
							b += (gains * scanline[xp*4+2]);
							a += (gains * scanline[xp*4+3]);
							gains = 0;
						}

						xp++;
						rmdr+=64;
					}
					r >>= 14;
					g >>= 14;
					b >>= 14;
					a >>= 14;
					if(r<0) r=0; else if(r>65535) r=65535;
					if(g<0) g=0; else if(g>65535) g=65535;
					if(b<0) b=0; else if(b>65535) b=65535;
					if(a<0) a=0; else if(a>65535) a=65535;
					RGB48[xx] = r;
					RGB48[xx+1] = g;
					RGB48[xx+2] = b;
					RGB48[xx+3] = a;
				}
				xx+=4;
			}
		}
	}

#if MMXSUPPORTED //TODO DANREMOVE
	//_mm_empty();
#endif
}

void RGB48WindowMask(DECODER *decoder, unsigned short *RGB48, int width, int channel, float windowMask)
{
	float line = (float)width * fabsf(windowMask);
	int  pixelbytes = 6;
	float frac = (float)(line-(float)((int)line));

	switch(decoder->StereoBufferFormat)
	{
		case DECODED_FORMAT_RGB32:
		case DECODED_FORMAT_W13A:
		case DECODED_FORMAT_RG64:
			pixelbytes = 8;
			break;
	}
	if(decoder->StereoBufferFormat == DECODED_FORMAT_W13A ||
       decoder->StereoBufferFormat == DECODED_FORMAT_WP13) // signed math needed
	{
		short *ptrL = (short *)RGB48;
		short *ptrR = (short *)RGB48;

		if(windowMask < 0)
			channel = channel == 0 ? 1 : 0;
		
		if(pixelbytes == 6)
		{
			if(channel == 0)
			{
				memset(ptrL, 0, 6*(int)line);
				ptrL += ((int)line*3);
				ptrL[0] = (int)((float)ptrL[0] * (1.0-frac));
				ptrL[1] = (int)((float)ptrL[1] * (1.0-frac));
				ptrL[2] = (int)((float)ptrL[2] * (1.0-frac));
			}
			else
			{		
				ptrR += ((width-(int)line)*3);
				memset(ptrR, 0, 6*(int)line);
				ptrR[-1] = (int)((float)ptrR[-1] * (1.0-frac));
				ptrR[-2] = (int)((float)ptrR[-2] * (1.0-frac));
				ptrR[-3] = (int)((float)ptrR[-3] * (1.0-frac));
			}
		}
		else
		{
			if(channel == 0)
			{
				memset(ptrL, 0, 8*(int)line);
				ptrL += ((int)line*4);
				ptrL[0] = (int)((float)ptrL[0] * (1.0-frac));
				ptrL[1] = (int)((float)ptrL[1] * (1.0-frac));
				ptrL[2] = (int)((float)ptrL[2] * (1.0-frac));
				ptrL[3] = (int)((float)ptrL[3] * (1.0-frac));
			}
			else
			{		
				ptrR += ((width-(int)line)*4);
				memset(ptrR, 0, 8*(int)line);
				ptrR[-1] = (int)((float)ptrR[-1] * (1.0-frac));
				ptrR[-2] = (int)((float)ptrR[-2] * (1.0-frac));
				ptrR[-3] = (int)((float)ptrR[-3] * (1.0-frac));
				ptrR[-4] = (int)((float)ptrR[-4] * (1.0-frac));
			}
		}
	}
	else
	{
		unsigned short *ptrL = RGB48;
		unsigned short *ptrR = RGB48;

		if(windowMask < 0)
			channel = channel == 0 ? 1 : 0;
		
		if(pixelbytes == 6)
		{
			if(channel == 0)
			{
				memset(ptrL, 0, 6*(int)line);
				ptrL += ((int)line*3);
				ptrL[0] = (int)((float)ptrL[0] * (1.0-frac));
				ptrL[1] = (int)((float)ptrL[1] * (1.0-frac));
				ptrL[2] = (int)((float)ptrL[2] * (1.0-frac));
			}
			else
			{		
				ptrR += ((width-(int)line)*3);
				memset(ptrR, 0, 6*(int)line);
				ptrR[-1] = (int)((float)ptrR[-1] * (1.0-frac));
				ptrR[-2] = (int)((float)ptrR[-2] * (1.0-frac));
				ptrR[-3] = (int)((float)ptrR[-3] * (1.0-frac));
			}
		}
		else
		{
			if(channel == 0)
			{
				memset(ptrL, 0, 8*(int)line);
				ptrL += ((int)line*4);
				ptrL[0] = (int)((float)ptrL[0] * (1.0-frac));
				ptrL[1] = (int)((float)ptrL[1] * (1.0-frac));
				ptrL[2] = (int)((float)ptrL[2] * (1.0-frac));
				ptrL[3] = (int)((float)ptrL[3] * (1.0-frac));
			}
			else
			{		
				ptrR += ((width-(int)line)*4);
				memset(ptrR, 0, 8*(int)line);
				ptrR[-1] = (int)((float)ptrR[-1] * (1.0-frac));
				ptrR[-2] = (int)((float)ptrR[-2] * (1.0-frac));
				ptrR[-3] = (int)((float)ptrR[-3] * (1.0-frac));
				ptrR[-4] = (int)((float)ptrR[-4] * (1.0-frac));
			}
		}
	}
}


void RGB48HoriShift(DECODER *decoder, unsigned short *RGB48, unsigned short *buffer, int width, float offset, int flip)
{
	float xposf,remainf;
	int xposi,tablepos,x;
	int gainA,gainB,gainC,gainD;
	//int endofSSEline = 0;
	unsigned short *scanline = (unsigned short *)buffer;
	int neg = 0,shift = 0;

	__m128i l1,l2,l3,gA,gB,gC,gD,o128,t1,t2;
	__m128i *line128, *outline128;

	if(flip)
	{
		unsigned short *ptrL = RGB48;
		unsigned short *ptrR = RGB48;
		ptrR += (width*3) - 3;
		for(x=0;x<width/2;x++)
		{
			int t1,t2,t3;

			t1 = ptrL[0];
			ptrL[0] = ptrR[0];
			ptrR[0] = t1;
			t2 = ptrL[1];
			ptrL[1] = ptrR[1];
			ptrR[1] = t2;
			t3 = ptrL[2];
			ptrL[2] = ptrR[2];
			ptrR[2] = t3;
			ptrL += 3;
			ptrR -= 3;
		}
	}

	if(offset < 0.0)
		neg = 1;

	xposf = width * offset;
	xposi = (int)floorf(xposf);

	remainf = xposf - (float)xposi;
	tablepos = (int)(remainf*(float)SUBPIXEL);

	xposi = abs(xposi);

	if(xposi==0 && tablepos == 0)
		return; // no move required

	gainA = gains[tablepos][0];
	gainB = gains[tablepos][1];
	gainC = gains[tablepos][2];
	gainD = gains[tablepos][3];


	if(neg == 0)
	{
		unsigned short *ptr = scanline;
		int nwidth = width-xposi+16;
		if(nwidth > width)
			nwidth = width;

		for(x=0;x<xposi+2;x++)
		{
			*ptr++ = 0;//r
			*ptr++ = 0;//g
			*ptr++ = 0;//b
		}

		memcpy(ptr, RGB48, (nwidth)*3*2);
		ptr += (nwidth)*3;
		for(x=0;x<16;x++)
		{
			*ptr++ = 0;//r
			*ptr++ = 0;//g
			*ptr++ = 0;//b
		}
	}
	else
	{
		unsigned short *ptr = scanline;

		for(x=0;x<2;x++)
		{
			if(x+xposi-2>=0)
			{
				*ptr++ = RGB48[(x+xposi-2)*3];//r
				*ptr++ = RGB48[(x+xposi-2)*3+1];//g
				*ptr++ = RGB48[(x+xposi-2)*3+2];//b
			}
			else
			{
				*ptr++ = 0;//r
				*ptr++ = 0;//g
				*ptr++ = 0;//b
			}
		}

		memcpy(ptr, &RGB48[xposi*3], (width-xposi)*3*2);
		ptr += (width-xposi)*3;
		for(x=0;x<xposi+16;x++)
		{
			*ptr++ = 0;//r
			*ptr++ = 0;//g
			*ptr++ = 0;//b
		}
	}

	gA = _mm_set1_epi16(gainA);
	gB = _mm_set1_epi16(gainB);
	gC = _mm_set1_epi16(gainC);
	gD = _mm_set1_epi16(gainD);

	line128 = (__m128i *)&scanline[0];
	//outline128 = line128;
	outline128 = (__m128i *)&RGB48[0];

	//l1 = load128;//r1,g1,b1,r2,g2,b2,r3,g3,
	//l2 = load128;//b3,r4,g4,b4,r5,g5,b5,r6
	//l3 = load128;//g6,b6,r7,g7,b7,r8,g8,b8


	if(decoder->StereoBufferFormat == DECODED_FORMAT_WP13)
	{
		l1 =  _mm_loadu_si128(line128++);
		l2 =  _mm_loadu_si128(line128++);
		l3 =  _mm_loadu_si128(line128++);

		shift = 0;
	}
	else
	{
		l1 =  _mm_loadu_si128(line128++);
		l1 = _mm_srli_epi16(l1,3); //13-bit unsigned
		l2 =  _mm_loadu_si128(line128++);
		l2 = _mm_srli_epi16(l2,3); //13-bit unsigned
		l3 =  _mm_loadu_si128(line128++);
		l3 = _mm_srli_epi16(l3,3); //13-bit unsigned

		shift = 3;
	}



	for(x=0;x<width*3; x+=8)
	{
		//o=l1* gainA
		o128 = _mm_mulhi_epi16(l1, gA);

		//t1 = l1<<3*16	//t1 = r2,g2,b2,r3,g3, 0 0 0
		//t2 = l2>>16*5	//t2 = 0  0  0  0  0  b3,r4,g4
		//t1 += t2;		//t1 = r2,g2,b2,r3,g3,b3,r4,g4
		//l1 = t1			//l1 = r2,g2,b2,r3,g3,b3,r4,g4
		//t1 *= gainB
		//o  += t1
		t1 = _mm_srli_si128(l1,3*2);
		t2 = _mm_slli_si128(l2,5*2);
		t1 = _mm_adds_epi16(t1,t2);
		l1 = t1;
		t1 = _mm_mulhi_epi16(t1, gB);
		o128 = _mm_adds_epi16(o128,t1);


		//t1 = l1<<3*16	//t1 = r3,g3,b3,r4,g4 0  0  0
		//t2 = l2<<3*16;	//t2 = b4,r5,g5,b5,r6 0  0  0
		//t2 >>= 5*16;	//t2 = 0  0  0  0  0  b4,r5,g5
		//t1 += t2		//t1 = r3,g3,b3,r4,g4,b4,r5,g5
		//l1 = t1			//l1 = r3,g3,b3,r4,g4,b4,r5,g5
		//t1 *= gainC
		//o  += t1
		t1 = _mm_srli_si128(l1,3*2);
		t2 = _mm_srli_si128(l2,3*2);
		t2 = _mm_slli_si128(t2,5*2);
		t1 = _mm_adds_epi16(t1,t2);
		l1 = t1;
		t1 = _mm_mulhi_epi16(t1, gC);
		o128 = _mm_adds_epi16(o128,t1);


		//t1 = l1<<3*16	//t1 = r4,g4,b4,r5,g5 0  0  0
		//t2 = l2<<6*16	//t2 = b5,r6 0  0  0  0  0  0
		//t2 >>= 5 * 16;	//t2 = 0  0  0  0  0  b5,r6 0
		//t1 += t2		//t1 = r4,g4,b4,r5,g5,b5,r6, 0
		//t2 = l3>>7*16	//t2 = 0  0  0  0  0  0  0  g6
		//t1 += t2		//t1 = r4,g4,b4,r5,g5,b5,r6,g6
		//t1 *= gainD
		//o  += t1
		t1 = _mm_srli_si128(l1,3*2);
		t2 = _mm_srli_si128(l2,6*2);
		t2 = _mm_slli_si128(t2,5*2);
		t1 = _mm_adds_epi16(t1,t2);
		t2 = _mm_slli_si128(l3,7*2);
		t1 = _mm_adds_epi16(t1,t2);
		t1 = _mm_mulhi_epi16(t1, gD);
		o128 = _mm_adds_epi16(o128,t1);

		l1 = l2;
		l2 = l3;
		l3 = _mm_loadu_si128(line128++);

		if(shift)
		{
			l3 = _mm_srli_epi16(l3,3); //13-bit unsigned

			o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
			o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
			o128 = _mm_slli_epi16(o128,4);
		}
		else
		{
			// upper limit to 32767
			o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
			o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
			o128 = _mm_slli_epi16(o128,1);
		}
		_mm_storeu_si128(outline128++, o128);

	}
}


void RGBA64HoriShift(DECODER *decoder, unsigned short *RGB48, unsigned short *buffer, int width, float offset, int flip)
{
	float xposf,remainf;
	int xposi,tablepos,x;
	int gainA,gainB,gainC,gainD;
	//int endofSSEline = 0;
	unsigned short *scanline = (unsigned short *)buffer;
	int neg = 0,shift = 0;

	__m128i l1,l2,l3,gA,gB,gC,gD,o128,t1,t2;
	__m128i *line128, *outline128;

	if(flip)
	{
		unsigned short *ptrL = RGB48;
		unsigned short *ptrR = RGB48;
		ptrR += (width*4) - 4;
		for(x=0;x<width/2;x++)
		{
			int t1,t2,t3,t4;

			t1 = ptrL[0];
			ptrL[0] = ptrR[0];
			ptrR[0] = t1;
			t2 = ptrL[1];
			ptrL[1] = ptrR[1];
			ptrR[1] = t2;
			t3 = ptrL[2];
			ptrL[2] = ptrR[2];
			ptrR[2] = t3;
			t4 = ptrL[2];
			ptrL[3] = ptrR[3];
			ptrR[3] = t4;
			ptrL += 4;
			ptrR -= 4;
		}
	}

	if(offset < 0.0)
		neg = 1;

	xposf = width * offset;
	xposi = (int)floorf(xposf);

	remainf = xposf - (float)xposi;
	tablepos = (int)(remainf*(float)SUBPIXEL);

	xposi = abs(xposi);

	if(xposi==0 && tablepos == 0)
		return; // no move required

	gainA = gains[tablepos][0];
	gainB = gains[tablepos][1];
	gainC = gains[tablepos][2];
	gainD = gains[tablepos][3];


	if(neg == 0)
	{
		unsigned short *ptr = scanline;
		int nwidth = width-xposi+16;
		if(nwidth > width)
			nwidth = width;

		for(x=0;x<xposi+2;x++)
		{
			*ptr++ = 0;//r
			*ptr++ = 0;//g
			*ptr++ = 0;//b
			*ptr++ = 0;//a
		}

		memcpy(ptr, RGB48, (nwidth)*4*2);
		ptr += (nwidth)*4;
		for(x=0;x<16;x++)
		{
			*ptr++ = 0;//r
			*ptr++ = 0;//g
			*ptr++ = 0;//b
			*ptr++ = 0;//a
		}
	}
	else
	{
		unsigned short *ptr = scanline;

		for(x=0;x<2;x++)
		{
			if(x+xposi-2>=0)
			{
				*ptr++ = RGB48[(x+xposi-2)*4];//r
				*ptr++ = RGB48[(x+xposi-2)*4+1];//g
				*ptr++ = RGB48[(x+xposi-2)*4+2];//b
				*ptr++ = RGB48[(x+xposi-2)*4+3];//a
			}
			else
			{
				*ptr++ = 0;//r
				*ptr++ = 0;//g
				*ptr++ = 0;//b
				*ptr++ = 0;//a
			}
		}

		memcpy(ptr, &RGB48[xposi*4], (width-xposi)*4*2);
		ptr += (width-xposi)*4;
		for(x=0;x<xposi+16;x++)
		{
			*ptr++ = 0;//r
			*ptr++ = 0;//g
			*ptr++ = 0;//b
			*ptr++ = 0;//a
		}
	}

	gA = _mm_set1_epi16(gainA);
	gB = _mm_set1_epi16(gainB);
	gC = _mm_set1_epi16(gainC);
	gD = _mm_set1_epi16(gainD);

	line128 = (__m128i *)&scanline[0];
	//outline128 = line128;
	outline128 = (__m128i *)&RGB48[0];

	//l1 = load128;//r1,g1,b1,a1,r2,g2,b2,a2,
	//l2 = load128;//r3,g3,b3,a3,r4,g4,b4,a4,
	//l3 = load128;//r5,g5,b5,a5,r6,g6,b6,a6,
	//l4 = load128;//r7,g7,b7,a7,r8,g8,b8,a8,


	if(decoder->StereoBufferFormat == DECODED_FORMAT_WP13 || decoder->StereoBufferFormat == DECODED_FORMAT_W13A)
	{
		l1 =  _mm_loadu_si128(line128++);
		l2 =  _mm_loadu_si128(line128++);
		l3 =  _mm_loadu_si128(line128++);

		shift = 0;
	}
	else
	{
		l1 =  _mm_loadu_si128(line128++);
		l1 = _mm_srli_epi16(l1,3); //13-bit unsigned
		l2 =  _mm_loadu_si128(line128++);
		l2 = _mm_srli_epi16(l2,3); //13-bit unsigned
		l3 =  _mm_loadu_si128(line128++);
		l3 = _mm_srli_epi16(l3,3); //13-bit unsigned

		shift = 3;
	}



	for(x=0;x<width*4; x+=8)
	{
		//o=l1* gainA
		o128 = _mm_mulhi_epi16(l1, gA);

		//t1 = l1<<4*16	//t1 = r2,g2,b2,a2,0, 0 0 0
		//t2 = l2>>4*16	//t2 = 0  0  0  0  r3,g3,b3,a4
		//t1 += t2;		//t1 = r2,g2,b2,a2,r3,g3,b3,a4
		//l1 = t1		//l1 = r2,g2,b2,a2,r3,g3,b3,a4
		//t1 *= gainB
		//o  += t1
		t1 = _mm_srli_si128(l1,4*2);
		t2 = _mm_slli_si128(l2,4*2);
		t1 = _mm_adds_epi16(t1,t2);
		l1 = t1;
		t1 = _mm_mulhi_epi16(t1, gB);
		o128 = _mm_adds_epi16(o128,t1);


		//t1 = l1<<4*16	//t1 = r3,g3,b3,a3, 0  0  0  0
		//t2 = l2<<4*16;//t2 = r4,g4,b4,a4, 0  0  0  0
		//t2 >>= 4*16;	//t2 = 0  0  0  0  r4,g4,b4,a4
		//t1 += t2		//t1 = r3,g3,b3,a4,r4,g4,b4,a4
		//l1 = t1		//l1 = r3,g3,b3,a4,r4,g4,b4,a4
		//t1 *= gainC
		//o  += t1
		t1 = _mm_srli_si128(l1,4*2);
		t2 = _mm_srli_si128(l2,4*2);
		t2 = _mm_slli_si128(t2,4*2);
		t1 = _mm_adds_epi16(t1,t2);
		l1 = t1;
		t1 = _mm_mulhi_epi16(t1, gC);
		o128 = _mm_adds_epi16(o128,t1);


		//t1 = l1<<4*16	//t1 = r4,g4,b4,a4,0  0  0  0
		//t2 = l3>>4*16	//t2 = 0 0  0  0   r5,g5,b5,a5
		//t1 += t2		//t1 = r4,g4,b4,a4,r5,g5,b5,a5
		//t1 *= gainD
		//o  += t1
		t1 = _mm_srli_si128(l1,4*2);
		t2 = _mm_slli_si128(l3,4*2);
		t1 = _mm_adds_epi16(t1,t2);
		t1 = _mm_mulhi_epi16(t1, gD);
		o128 = _mm_adds_epi16(o128,t1);

		l1 = l2;
		l2 = l3;
		l3 = _mm_loadu_si128(line128++);

		if(shift)
		{
			l3 = _mm_srli_epi16(l3,3); //13-bit unsigned

			o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
			o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
			o128 = _mm_slli_epi16(o128,4);
		}
		else
		{
			// upper limit to 32767
			o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
			o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
			o128 = _mm_slli_epi16(o128,1);
		}
		_mm_storeu_si128(outline128++, o128);

	}
}



void RGB48HoriShiftAnaglyph(DECODER *decoder, unsigned short *RGB48, unsigned short *buffer, int width,
							float offsetR, float offsetG, float offsetB ,
							int flipR, int flipG, int flipB)
{
	float Rxposf,Rremainf;
	int Rxposi,Rtablepos;
	float Gxposf,Gremainf;
	int Gxposi,Gtablepos;
	float Bxposf,Bremainf;
	int Bxposi,Btablepos;
	int x;
	int RgainA,RgainB,RgainC,RgainD;
	int GgainA,GgainB,GgainC,GgainD;
	int BgainA,BgainB,BgainC,BgainD;
	//int endofSSEline = 0;
	unsigned short *scanline = (unsigned short *)buffer;
	int negR = 0;
	int negG = 0;
	int negB = 0;
	int shift = 0;

	__m128i l1,l2,l3,o128,t1,t2;
	__m128i *line128, *outline128;
	__m128i gA1,gB1,gC1,gD1,gA2,gB2,gC2,gD2,gA3,gB3,gC3,gD3;

	if(flipR)
	{
		unsigned short *ptrL = RGB48;
		unsigned short *ptrR = RGB48;
		ptrR += (width*3) - 3;
		for(x=0;x<width/2;x++)
		{
			int t;

			t = *ptrL;
			*ptrL = *ptrR;
			*ptrR = t;
			ptrL += 3;
			ptrR -= 3;
		}
	}

	if(flipG)
	{
		unsigned short *ptrL = &RGB48[1];
		unsigned short *ptrR = &RGB48[1];
		ptrR += (width*3) - 3;
		for(x=0;x<width/2;x++)
		{
			int t;

			t = *ptrL;
			*ptrL = *ptrR;
			*ptrR = t;
			ptrL += 3;
			ptrR -= 3;
		}
	}

	if(flipB)
	{
		unsigned short *ptrL = &RGB48[2];
		unsigned short *ptrR = &RGB48[2];
		ptrR += (width*3) - 3;
		for(x=0;x<width/2;x++)
		{
			int t;

			t = *ptrL;
			*ptrL = *ptrR;
			*ptrR = t;
			ptrL += 3;
			ptrR -= 3;
		}
	}

	if(offsetR < 0.0)
		negR = 1;
	if(offsetG < 0.0)
		negG = 1;
	if(offsetB < 0.0)
		negB = 1;

	Rxposf = width * offsetR;
	Rxposi = (int)floorf(Rxposf);
	Rremainf = Rxposf - (float)Rxposi;
	Rtablepos = (int)(Rremainf*(float)SUBPIXEL);

	Gxposf = width * offsetG;
	Gxposi = (int)floorf(Gxposf);
	Gremainf = Gxposf - (float)Gxposi;
	Gtablepos = (int)(Gremainf*(float)SUBPIXEL);

	Bxposf = width * offsetB;
	Bxposi = (int)floorf(Bxposf);
	Bremainf = Bxposf - (float)Bxposi;
	Btablepos = (int)(Bremainf*(float)SUBPIXEL);

	Rxposi = abs(Rxposi);
	Gxposi = abs(Gxposi);
	Bxposi = abs(Bxposi);

	if(Rxposi==0 && Rtablepos == 0)
		return; // no move required

	RgainA = gains[Rtablepos][0];
	RgainB = gains[Rtablepos][1];
	RgainC = gains[Rtablepos][2];
	RgainD = gains[Rtablepos][3];

	GgainA = gains[Gtablepos][0];
	GgainB = gains[Gtablepos][1];
	GgainC = gains[Gtablepos][2];
	GgainD = gains[Gtablepos][3];

	BgainA = gains[Btablepos][0];
	BgainB = gains[Btablepos][1];
	BgainC = gains[Btablepos][2];
	BgainD = gains[Btablepos][3];

	if(negR == 0)
	{
		unsigned short *ptr = scanline;
		int nwidth = width-Rxposi+16;
		if(nwidth > width)
			nwidth = width;

		for(x=0;x<Rxposi+2;x++)
		{
			*ptr++ = 0;//r
			ptr++;//g
			ptr++;//b
		}
		for(x=0;x<nwidth;x++)
		{
			*ptr++ = RGB48[x*3];//r
			ptr++;//g
			ptr++;//b
		}
		for(x=0;x<16;x++)
		{
			*ptr++ = 0;//r
			ptr++;//g
			ptr++;//b
		}
	}
	else
	{
		unsigned short *ptr = scanline;

		for(x=0;x<2;x++)
		{
			if(x+Rxposi-2>=0)
			{
				*ptr++ = RGB48[(x+Rxposi-2)*3];//r
				ptr++;//g
				ptr++;//b
			}
			else
			{
				*ptr++ = 0;//r
				ptr++;//g
				ptr++;//b
			}
		}

		//memcpy(ptr, &RGB48[xposi*3], (width-xposi)*3*2);
		//ptr += (width-xposi)*3;

		for(x=Rxposi;x<width;x++)
		{
			*ptr++ = RGB48[x*3];//r
			ptr++;//g
			ptr++;//b
		}
		for(x=0;x<Rxposi+16;x++)
		{
			*ptr++ = 0;//r
			ptr++;//g
			ptr++;//b
		}
	}


	if(negG == 0)
	{
		unsigned short *ptr = scanline;
		int nwidth = width-Gxposi+16;
		if(nwidth > width)
			nwidth = width;

		for(x=0;x<Gxposi+2;x++)
		{
			ptr++;//r
			*ptr++ = 0;//g
			ptr++;//b
		}
		for(x=0;x<nwidth;x++)
		{
			ptr++;//r
			*ptr++ = RGB48[x*3+1];//g
			ptr++;//b
		}
		for(x=0;x<16;x++)
		{
			ptr++;//r
			*ptr++ = 0;//g
			ptr++;//b
		}
	}
	else
	{
		unsigned short *ptr = scanline;

		for(x=0;x<2;x++)
		{
			if(x+Gxposi-2>=0)
			{
				ptr++;//r
				*ptr++ = RGB48[(x+Gxposi-2)*3+1];//g
				ptr++;//b
			}
			else
			{
				ptr++;//r
				*ptr++ = 0;//g
				ptr++;//b
			}
		}

		//memcpy(ptr, &RGB48[xposi*3], (width-xposi)*3*2);
		//ptr += (width-xposi)*3;

		for(x=Gxposi;x<width;x++)
		{
			ptr++;//r
			*ptr++ = RGB48[x*3+1];//g
			ptr++;//b
		}
		for(x=0;x<Gxposi+16;x++)
		{
			ptr++;//r
			*ptr++ = 0;//g
			ptr++;//b
		}
	}


	if(negB == 0)
	{
		unsigned short *ptr = scanline;
		int nwidth = width-Bxposi+16;
		if(nwidth > width)
			nwidth = width;

		for(x=0;x<Bxposi+2;x++)
		{
			ptr++;//r
			ptr++;//g
			*ptr++ = 0;//b
		}
		for(x=0;x<nwidth;x++)
		{
			ptr++;//r
			ptr++;//g
			*ptr++ = RGB48[x*3+2];//b
		}
		for(x=0;x<16;x++)
		{
			ptr++;//r
			ptr++;//g
			*ptr++ = 0;//b
		}
	}
	else
	{
		unsigned short *ptr = scanline;

		for(x=0;x<2;x++)
		{
			if(x+Bxposi-2>=0)
			{
				ptr++;//r
				ptr++;//g
				*ptr++ = RGB48[(x+Bxposi-2)*3+2];//b
			}
			else
			{
				ptr++;//r
				ptr++;//g
				*ptr++ = 0;//b
			}
		}

		//memcpy(ptr, &RGB48[xposi*3], (width-xposi)*3*2);
		//ptr += (width-xposi)*3;

		for(x=Bxposi;x<width;x++)
		{
			ptr++;//r
			ptr++;//g
			*ptr++ = RGB48[x*3+2];//b
		}
		for(x=0;x<Bxposi+16;x++)
		{
			ptr++;//r
			ptr++;//g
			*ptr++ = 0;//b
		}
	}


	gA1 = _mm_set_epi16(RgainA,GgainA,BgainA,RgainA,GgainA,BgainA,RgainA,GgainA);
	gA2 = _mm_set_epi16(BgainA,RgainA,GgainA,BgainA,RgainA,GgainA,BgainA,RgainA);
	gA3 = _mm_set_epi16(GgainA,BgainA,RgainA,GgainA,BgainA,RgainA,GgainA,BgainA);

	gB1 = _mm_set_epi16(RgainB,GgainB,BgainB,RgainB,GgainB,BgainB,RgainB,GgainB);
	gB2 = _mm_set_epi16(BgainB,RgainB,GgainB,BgainB,RgainB,GgainB,BgainB,RgainB);
	gB3 = _mm_set_epi16(GgainB,BgainB,RgainB,GgainB,BgainB,RgainB,GgainB,BgainB);

	gC1 = _mm_set_epi16(RgainC,GgainC,BgainC,RgainC,GgainC,BgainC,RgainC,GgainC);
	gC2 = _mm_set_epi16(BgainC,RgainC,GgainC,BgainC,RgainC,GgainC,BgainC,RgainC);
	gC3 = _mm_set_epi16(GgainC,BgainC,RgainC,GgainC,BgainC,RgainC,GgainC,BgainC);

	gD1 = _mm_set_epi16(RgainD,GgainD,BgainD,RgainD,GgainD,BgainD,RgainD,GgainD);
	gD2 = _mm_set_epi16(BgainD,RgainD,GgainD,BgainD,RgainD,GgainD,BgainD,RgainD);
	gD3 = _mm_set_epi16(GgainD,BgainD,RgainD,GgainD,BgainD,RgainD,GgainD,BgainD);

	line128 = (__m128i *)&scanline[0];
	//outline128 = line128;
	outline128 = (__m128i *)&RGB48[0];

	//l1 = load128;//r1,g1,b1,r2,g2,b2,r3,g3,
	//l2 = load128;//b3,r4,g4,b4,r5,g5,b5,r6
	//l3 = load128;//g6,b6,r7,g7,b7,r8,g8,b8
	if(decoder->StereoBufferFormat == DECODED_FORMAT_WP13)
	{
		l1 =  _mm_loadu_si128(line128++);
		l2 =  _mm_loadu_si128(line128++);
		l3 =  _mm_loadu_si128(line128++);

		shift = 0;
	}
	else
	{
		l1 =  _mm_loadu_si128(line128++);
		l1 = _mm_srli_epi16(l1,3); //13-bit unsigned
		l2 =  _mm_loadu_si128(line128++);
		l2 = _mm_srli_epi16(l2,3); //13-bit unsigned
		l3 =  _mm_loadu_si128(line128++);
		l3 = _mm_srli_epi16(l3,3); //13-bit unsigned

		shift = 3;
	}

	for(x=0;x<width*3; x+=8)
	{
		//o=l1* gainA
		o128 = _mm_mulhi_epi16(l1, gA1);

		//t1 = l1<<3*16	//t1 = r2,g2,b2,r3,g3, 0 0 0
		//t2 = l2>>16*5	//t2 = 0  0  0  0  0  b3,r4,g4
		//t1 += t2;		//t1 = r2,g2,b2,r3,g3,b3,r4,g4
		//l1 = t1			//l1 = r2,g2,b2,r3,g3,b3,r4,g4
		//t1 *= gainB
		//o  += t1
		t1 = _mm_srli_si128(l1,3*2);
		t2 = _mm_slli_si128(l2,5*2);
		t1 = _mm_adds_epi16(t1,t2);
		l1 = t1;
		t1 = _mm_mulhi_epi16(t1, gB1);
		o128 = _mm_adds_epi16(o128,t1);


		//t1 = l1<<3*16	//t1 = r3,g3,b3,r4,g4 0  0  0
		//t2 = l2<<3*16;	//t2 = b4,r5,g5,b5,r6 0  0  0
		//t2 >>= 5*16;	//t2 = 0  0  0  0  0  b4,r5,g5
		//t1 += t2		//t1 = r3,g3,b3,r4,g4,b4,r5,g5
		//l1 = t1			//l1 = r3,g3,b3,r4,g4,b4,r5,g5
		//t1 *= gainC
		//o  += t1
		t1 = _mm_srli_si128(l1,3*2);
		t2 = _mm_srli_si128(l2,3*2);
		t2 = _mm_slli_si128(t2,5*2);
		t1 = _mm_adds_epi16(t1,t2);
		l1 = t1;
		t1 = _mm_mulhi_epi16(t1, gC1);
		o128 = _mm_adds_epi16(o128,t1);


		//t1 = l1<<3*16	//t1 = r4,g4,b4,r5,g5 0  0  0
		//t2 = l2<<6*16	//t2 = b5,r6 0  0  0  0  0  0
		//t2 >>= 5 * 16;	//t2 = 0  0  0  0  0  b5,r6 0
		//t1 += t2		//t1 = r4,g4,b4,r5,g5,b5,r6, 0
		//t2 = l3>>7*16	//t2 = 0  0  0  0  0  0  0  g6
		//t1 += t2		//t1 = r4,g4,b4,r5,g5,b5,r6,g6
		//t1 *= gainD
		//o  += t1
		t1 = _mm_srli_si128(l1,3*2);
		t2 = _mm_srli_si128(l2,6*2);
		t2 = _mm_slli_si128(t2,5*2);
		t1 = _mm_adds_epi16(t1,t2);
		t2 = _mm_slli_si128(l3,7*2);
		t1 = _mm_adds_epi16(t1,t2);
		t1 = _mm_mulhi_epi16(t1, gD1);
		o128 = _mm_adds_epi16(o128,t1);


		t1 = gA1;
		gA1 = gA2;
		gA2 = gA3;
		gA3 = t1;

		t1 = gB1;
		gB1 = gB2;
		gB2 = gB3;
		gB3 = t1;

		t1 = gC1;
		gC1 = gC2;
		gC2 = gC3;
		gC3 = t1;

		t1 = gD1;
		gD1 = gD2;
		gD2 = gD3;
		gD3 = t1;


		l1 = l2;
		l2 = l3;
		l3 = _mm_loadu_si128(line128++);

		if(shift)
		{
			l3 = _mm_srli_epi16(l3,3); //13-bit unsigned

			o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
			o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x0fff));
			o128 = _mm_slli_epi16(o128,4);
		}
		else
		{
			// upper limit to 32767
			o128 = _mm_adds_epi16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
			o128 = _mm_subs_epu16(o128, _mm_set1_epi16(0x7fff - 0x3fff));
			o128 = _mm_slli_epi16(o128,1);
		}
		_mm_storeu_si128(outline128++, o128);

	}
}


void HistogramLine(DECODER *decoder, unsigned short *sbase, int width, int format, int whitepoint)
{
	int x,val,ypos=0,upos=1,vpos=3;
	int step = 1,pos=0;
	short *ssbase = (short *)sbase;
	uint32_t *lbase = (uint32_t *)sbase;
	ToolsHandle *tools = decoder->tools;
	int scaledvectorscope = 0;

	if(tools == NULL)
		return;

	if(whitepoint == 13)
	{
		if(format == DECODED_FORMAT_RG64)
			format = DECODED_FORMAT_W13A;
		else
			format = DECODED_FORMAT_WP13;
	}
	while(width/step > 360)
	{
		step*=2;
	}

	tools->waveformWidth = width/step;
	decoder->tools->blurUVdone = 0;

	switch(format & 0xffffff)
	{
	case DECODED_FORMAT_WP13:
		decoder->tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int32_t R,G,B,U,V;
			R = ssbase[0]>>5;
			G = ssbase[1]>>5;
			B = ssbase[2]>>5;

			if(R > 255) R = 255;
			if(R < 0) R = 0;
			if(G > 255) G = 255;
			if(G < 0) G = 0;
			if(B > 255) B = 255;
			if(B < 0) B = 0;
			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;

			//Y = (((1499 * R) + (5030 * G) + (508 * B))>>13) + 16;
			if(scaledvectorscope)
			{
				U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
				V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
			}
			else
			{
				U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
				V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
			}

			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;
			tools->scopeUV[U][V]++;

			ssbase += step*3;
		}
		break;

	case DECODED_FORMAT_W13A:
		tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int32_t R,G,B,U,V;
			R = ssbase[0]>>5;
			G = ssbase[1]>>5;
			B = ssbase[2]>>5;

			if(R > 255) R = 255;
			if(R < 0) R = 0;
			if(G > 255) G = 255;
			if(G < 0) G = 0;
			if(B > 255) B = 255;
			if(B < 0) B = 0;
			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;

			//Y = (((1499 * R) + (5030 * G) + (508 * B))>>13) + 16;
			if(scaledvectorscope)
			{
				U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
				V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
			}
			else
			{
				U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
				V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
			}

			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;
			tools->scopeUV[U][V]++;

			ssbase += step*4;
		}
		break;

	case DECODED_FORMAT_RG48:
		tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int32_t R,G,B,U,V;
			R = sbase[0]>>8;
			G = sbase[1]>>8;
			B = sbase[2]>>8;
			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;
			
			//Y = (((1499 * R) + (5030 * G) + (508 * B))>>13) + 16;
			if(scaledvectorscope)
			{
				U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
				V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
			}
			else
			{
				U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
				V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
			}

			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;
			tools->scopeUV[U][V]++;

			sbase += step*3;
		}
		break;

		
	case DECODED_FORMAT_AB10:
	case DECODED_FORMAT_RG30:
		tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int32_t R,G,B,U,V;
			val = lbase[x];

			R = (val>>22)&0xff;
			G = (val>>12)&0xff;
			B = (val>>02)&0xff;
			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;

			//Y = (((1499 * R) + (5030 * G) + (508 * B))>>13) + 16;
			if(scaledvectorscope)
			{
				U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
				V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
			}
			else
			{
				U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
				V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
			}

			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;
			tools->scopeUV[U][V]++;
		}
		break;

	case DECODED_FORMAT_AR10:
		tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int32_t R,G,B,U,V;
			val = lbase[x];

			B = (val>>22)&0xff;
			G = (val>>12)&0xff;
			R = (val>>02)&0xff;
			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;
			
			//Y = (((1499 * R) + (5030 * G) + (508 * B))>>13) + 16;
			if(scaledvectorscope)
			{
				U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
				V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
			}
			else
			{
				U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
				V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
			}

			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;		
			tools->scopeUV[U][V]++;
		}
		break;
		
	case DECODED_FORMAT_R210:
		tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int32_t R,G,B,U,V;
			val = SwapInt32BtoN(lbase[x]);
			
			R = (val>>22)&0xff;
			G = (val>>12)&0xff;
			B = (val>>02)&0xff;
			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;
			
			//Y = (((1499 * R) + (5030 * G) + (508 * B))>>13) + 16;
			if(scaledvectorscope)
			{
				U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
				V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
			}
			else
			{
				U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
				V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
			}

			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;
			tools->scopeUV[U][V]++;
		}
		break;

	case DECODED_FORMAT_DPX0:
		tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int32_t R,G,B,U,V;
			val = SwapInt32BtoN(lbase[x]);

			R = (val>>24)&0xff;
			G = (val>>14)&0xff;
			B = (val>>04)&0xff;
			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;
			
			//Y = (((1499 * R) + (5030 * G) + (508 * B))>>13) + 16;
			if(scaledvectorscope)
			{
				U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
				V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
			}
			else
			{
				U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
				V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
			}

			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;
			tools->scopeUV[U][V]++;
		}
		break;

	case DECODED_FORMAT_RG64:
	case DECODED_FORMAT_B64A:
		tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int32_t R,G,B,U,V;
			R = sbase[1]>>8;
			G =	sbase[2]>>8;
			B = sbase[3]>>8;
			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;
			
			//Y = (((1499 * R) + (5030 * G) + (508 * B))>>13) + 16;
			if(scaledvectorscope)
			{
				U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
				V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
			}
			else
			{
				U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
				V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
			}

			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;
			tools->scopeUV[U][V]++;

			sbase += step*4;
		}
		break;

	case COLOR_FORMAT_UYVY:
		ypos=1,upos=0,vpos=2;
	case DECODED_FORMAT_CbYCrY_8bit:		// CMD: 20100109
	case COLOR_FORMAT_YUYV:
		tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int Y,U,V,R,G,B;
			uint8_t *bptr = (uint8_t *)sbase;
			bptr +=  x * 2;

			Y = bptr[ypos]-16;
			U = bptr[upos]-128;
			Y+= bptr[ypos+2]-16; Y>>=1;
			V = bptr[vpos]-128;

			R = (9535*Y + 14688*V)>>13; //13-bit white
			G = (9535*Y - 4375*V - 1745*U)>>13;
			B = (9535*Y + 17326*U)>>13;

//TODO much -20 to 120 RGB range.
			if(R > 255) R = 255;
			if(R < 0) R = 0;
			if(G > 255) G = 255;
			if(G < 0) G = 0;
			if(B > 255) B = 255;
			if(B < 0) B = 0;
			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;

			if(scaledvectorscope)
			{
				U *= 255;	U /= 314;
				V *= 255;	V /= 244;
			}
			//* 255.0/314.0
			//* 255.0/244.0
			U += 128;
			V += 128;
			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;
			tools->scopeUV[U][V]++;
		}
		break;
		
	case COLOR_FORMAT_YU64:
		tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int Y,U,V,R,G,B;
			uint8_t *bptr = (uint8_t *)sbase;
			bptr +=  x * 4;
			bptr++; //read only the high byte out of the 16-bit

			Y = bptr[0]-16;
			V = bptr[2]-128;
			Y+= bptr[4]-16; Y>>=1;
			U = bptr[6]-128;

			R = (9535*Y + 14688*V)>>13; //13-bit white
			G = (9535*Y - 4375*V - 1745*U)>>13;
			B = (9535*Y + 17326*U)>>13;

			if(R > 255) R = 255;
			if(R < 0) R = 0;
			if(G > 255) G = 255;
			if(G < 0) G = 0;
			if(B > 255) B = 255;
			if(B < 0) B = 0;
			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;

			if(scaledvectorscope)
			{
				U *= 255;	U /= 314;
				V *= 255;	V /= 244;
			}
			U += 128;
			V += 128;
			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;
			tools->scopeUV[U][V]++;
		}
		break;


		
	case COLOR_FORMAT_V210:
		tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int Y,U,V,R,G,B;
			uint32_t *lptr = (uint32_t *)sbase;
			lptr += (x/6)*4;

			switch(x % 6)
			{
			case 0:
				V = ((*lptr>>02) & 0xff) - 128; 
				Y = ((*lptr>>12) & 0xff) - 16; 
				U = ((*lptr>>22) & 0xff) - 128; 
				lptr++;
				Y+= ((*lptr>>02) & 0xff) - 16; Y>>=1;
				break;
			case 1:
				lptr++;
				Y = ((*lptr>>02) & 0xff) - 16; 
				V = ((*lptr>>12) & 0xff) - 128; 
				Y+= ((*lptr>>22) & 0xff) - 16; Y>>=1;
				lptr--;
				U = ((*lptr>>22) & 0xff) - 128; 
				break;
			case 2:
				lptr++;
				Y = ((*lptr>>22) & 0xff) - 16; 
				lptr++;
				U = ((*lptr>>02) & 0xff) - 128; 
				Y+= ((*lptr>>12) & 0xff) - 16;  Y>>=1;
				V = ((*lptr>>22) & 0xff) - 128;
				break;
			case 3:
				lptr++;
				V = ((*lptr>>12) & 0xff) - 128; 
				lptr++;
				U = ((*lptr>>02) & 0xff) - 128; 
				Y = ((*lptr>>12) & 0xff) - 16; 
				lptr++;
				Y+= ((*lptr>>02) & 0xff) - 16; Y>>=1;
				break;
			case 4:
				lptr+=2;
				V = ((*lptr>>22) & 0xff) - 128; 
				lptr++;
				Y = ((*lptr>>02) & 0xff) - 16; 
				U = ((*lptr>>12) & 0xff) - 128; 
				Y+= ((*lptr>>22) & 0xff) - 16; Y>>=1;
				break;
			case 5:
				lptr+=2;
				V = ((*lptr>>22) & 0xff) - 128; 
				lptr++;
				U = ((*lptr>>12) & 0xff) - 128; 
				Y = ((*lptr>>22) & 0xff) - 16; 
				lptr++;
				Y+= ((*lptr>>02) & 0xff) - 16; Y>>=1;
				break;
			}

			R = (9535*Y + 14688*V)>>13; //13-bit white
			G = (9535*Y - 4375*V - 1745*U)>>13;
			B = (9535*Y + 17326*U)>>13;

			if(R > 255) R = 255;
			if(R < 0) R = 0;
			if(G > 255) G = 255;
			if(G < 0) G = 0;
			if(B > 255) B = 255;
			if(B < 0) B = 0;
			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;		

			if(scaledvectorscope)
			{
				U *= 255;	U /= 314;
				V *= 255;	V /= 244;
			}
			U += 128;
			V += 128;
			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;
			tools->scopeUV[U][V]++;
		}
		break;

	case COLOR_FORMAT_RGB24:
		tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int R,G,B,U,V;
			uint8_t *bptr = (uint8_t *)sbase;
			bptr +=  x * 3;

			R = bptr[2];
			G = bptr[1];
			B = bptr[0];

			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;
			
			//Y = (((1499 * R) + (5030 * G) + (508 * B))>>13) + 16;
			if(scaledvectorscope)
			{
				U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
				V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
			}
			else
			{
				U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
				V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
			}

			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;
			tools->scopeUV[U][V]++;
		}
		break;

	case COLOR_FORMAT_RGB32:
		tools->histogram = 1;
		for(x=0,pos=0; x<width; x+=step,pos++)
		{
			int R,G,B,U,V;
			uint8_t *bptr = (uint8_t *)sbase;
			bptr +=  x * 4;
			
			R = bptr[2];
			G = bptr[1];
			B = bptr[0];

			tools->histR[R]++;
			tools->histG[G]++;
			tools->histB[B]++;
			tools->waveR[pos][R]++;
			tools->waveG[pos][G]++;
			tools->waveB[pos][B]++;
			
			//Y = (((1499 * R) + (5030 * G) + (508 * B))>>13) + 16;
			if(scaledvectorscope)
			{
				U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
				V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
			}
			else
			{
				U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
				V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
			}

			if(U<0) U=0; if(U>255) U=255;
			if(V<0) V=0; if(V>255) V=255;
			tools->scopeUV[U][V]++;
		}
		break;

	case COLOR_FORMAT_BYR2:
	case COLOR_FORMAT_BYR4:
		//do nothing
		break;

	default:
		assert(0);	
#if (0 && DEBUG)
		fprintf(stderr,"decoder.HistogramLine: Unsupported pixel format\n");
#endif
		break;

	}
}


void GhostBust(DECODER *decoder, unsigned short *sbaseL, unsigned short *sbaseR, int width, int ileakL, int ileakR)
{
	#if 1
	int x,RL,GL,BL,RR,GR,BR;
	int nRL,nGL,nBL;
	int nRR,nGR,nBR;
	int max = 1024*1024-1;
	unsigned short *sqrttable = decoder->sqrttable;
	ileakL>>=6;
	ileakR>>=6;

	if(sqrttable == NULL)
		return;

	for(x=0;x<width;x++)
	{
		RL = sbaseL[0]>>6;
		GL = sbaseL[1]>>6; //10-bit
		BL = sbaseL[2]>>6;
		RL*=RL;
		GL*=GL;	//20-bit
		BL*=BL;

		RR = sbaseR[0]>>6;
		GR = sbaseR[1]>>6; //10-bit
		BR = sbaseR[2]>>6;
		RR*=RR;
		GR*=GR;	//20-bit
		BR*=BR;

		nRL = RL*(1023-ileakL) + ileakL*max - RR*ileakL; //30-bit
		nGL = GL*(1023-ileakL) + ileakL*max - GR*ileakL;
		nBL = BL*(1023-ileakL) + ileakL*max - BR*ileakL;

		nRL >>= 10; //20-bit
		nGL >>= 10;
		nBL >>= 10;

		if(nRL>max) nRL=max; if(nRL<0) nRL=0;
		if(nGL>max) nGL=max; if(nGL<0) nGL=0;
		if(nBL>max) nBL=max; if(nBL<0) nBL=0;
		
		if(sqrttable[nRL] == 65535)
			sqrttable[nRL] = (int)sqrt(nRL);
		if(sqrttable[nGL] == 65535)
			sqrttable[nGL] = (int)sqrt(nGL);
		if(sqrttable[nBL] == 65535)
			sqrttable[nBL] = (int)sqrt(nBL);
		sbaseL[0] = sqrttable[nRL]<<6;
		sbaseL[1] = sqrttable[nGL]<<6;
		sbaseL[2] = sqrttable[nBL]<<6;
		sbaseL += 3;


		nRR = RR*(1023-ileakR) + ileakR*max - RL*ileakR; //30-bit
		nGR = GR*(1023-ileakR) + ileakR*max - GL*ileakR;
		nBR = BR*(1023-ileakR) + ileakR*max - BL*ileakR;

		nRR >>= 10; //20-bit
		nGR >>= 10;
		nBR >>= 10;

		if(nRR>max) nRR=max; if(nRR<0) nRR=0;
		if(nGR>max) nGR=max; if(nGR<0) nGR=0;
		if(nBR>max) nBR=max; if(nBR<0) nBR=0;

		if(sqrttable[nRR] == 65535)
			sqrttable[nRR] = (int)sqrt(nRR);
		if(sqrttable[nGR] == 65535)
			sqrttable[nGR] = (int)sqrt(nGR);
		if(sqrttable[nBR] == 65535)
			sqrttable[nBR] = (int)sqrt(nBR);
		sbaseR[0] = sqrttable[nRR]<<6;
		sbaseR[1] = sqrttable[nGR]<<6;
		sbaseR[2] = sqrttable[nBR]<<6;
		sbaseR += 3;

	}
#else // works and fast but has not image linearization, not as good
	__m128i *ptrL = (__m128i *)sbaseL;
	__m128i *ptrR = (__m128i *)sbaseR;

	__m128i t,L,R,nL,nR;

	int x,width8 = (width*3) & ~7;
	__m128i white_epi16 = _mm_set1_epi16(32767);
	__m128i leak_epi16 = _mm_set1_epi16(ileak>>1);
	__m128i oneNegLeak_epi16 = _mm_set1_epi16(32767-(ileak>>1));

	for(x=0;x<width8;x+=8)
	{
		L = _mm_load_si128(ptrL); 
		R = _mm_load_si128(ptrR); 

		L = _mm_srli_epi16(L,1); //15-bit
		R = _mm_srli_epi16(R,1); //15-bit

		nL = _mm_mulhi_epi16(L, oneNegLeak_epi16);
		t  = _mm_mulhi_epi16(white_epi16, leak_epi16);
		nL = _mm_adds_epi16(nL, t);
		t  = _mm_mulhi_epi16(R, leak_epi16);
		nL = _mm_subs_epu16(nL, t);

		nR = _mm_mulhi_epi16(R, oneNegLeak_epi16);
		t  = _mm_mulhi_epi16(white_epi16, leak_epi16);
		nR = _mm_adds_epi16(nR, t);
		t  = _mm_mulhi_epi16(L, leak_epi16);
		nR = _mm_subs_epu16(nR, t);

		L = _mm_slli_epi16(nL,2);
		R = _mm_slli_epi16(nR,2);
		
		_mm_store_si128(ptrL++, L);
		_mm_store_si128(ptrR++, R);
	}
#endif
}


void GhostBustRC(DECODER *decoder, unsigned short *sbase, int width, int ileakL, int ileakR)
{
#if 1
	int x,R,G,B;
	int nR,nG,nB;
	int max = 1024*1024-1;
	unsigned short *sqrttable = decoder->sqrttable;
	ileakL>>=6;
	ileakR>>=6;

	if(sqrttable == NULL)
		return;

	for(x=0;x<width;x++)
	{
		R = sbase[0]>>6;
		G = sbase[1]>>6; //10-bit
		B = sbase[2]>>6;
		R*=R;
		G*=G;	//20-bit
		B*=B;

		nR = R*(1023-ileakL) + ileakL*max - ((G+B)>>1)*ileakL; //30-bit
		nG = G*(1023-ileakR) + ileakR*max - R*ileakR;
		nB = B*(1023-ileakR) + ileakR*max - R*ileakR;

		nR >>= 10; //20-bit
		nG >>= 10;
		nB >>= 10;

		if(nR>max) nR=max; if(nR<0) nR=0;
		if(nG>max) nG=max; if(nG<0) nG=0;
		if(nB>max) nB=max; if(nB<0) nB=0;

		if(sqrttable[nR] == 65535)
			sqrttable[nR] = (int)sqrt(nR);
		if(sqrttable[nG] == 65535)
			sqrttable[nG] = (int)sqrt(nG);
		if(sqrttable[nB] == 65535)
			sqrttable[nB] = (int)sqrt(nB);
		sbase[0] = sqrttable[nR]<<6;
		sbase[1] = sqrttable[nG]<<6;
		sbase[2] = sqrttable[nB]<<6;
		sbase += 3;
	}

#elif 0
	int x;
	float R,G,B;
	float nR,nG,nB;
	float fleakL = (float)ileakL / 65535.0; 
	float fleakR = (float)ileakR / 65535.0; 
	
	for(x=0;x<width;x++)
	{
		R = sbase[0];
		G = sbase[1];
		B = sbase[2];
		R /= 65535.0;
		G /= 65535.0;
		B /= 65535.0;
		R *= R;
		G *= G;
		B *= B;

		nR = R*(1.0-fleakL) + fleakL - (G+B)*0.5*fleakL;
		nG = G*(1.0-fleakR) + fleakR - R*fleakR;
		nB = B*(1.0-fleakR) + fleakR - R*fleakR;

		if(nR<0) nR=0;
		if(nG<0) nG=0;
		if(nB<0) nB=0;

		nR = sqrt(nR);
		nG = sqrt(nG);
		nB = sqrt(nB);

		sbase[0] = nR * 65535.0;
		sbase[1] = nG * 65535.0;
		sbase[2] = nB * 65535.0;
		sbase += 3;
	}
#elif 0
	__m128i RGBRGB,rgb_epi32,RGB1,RGB2;
	__m128i zero_epi128 = _mm_setzero_si128();

	int x,width6 = (width*3) / 6 * 6;
	__m128 white_ps = _mm_set1_ps(1.0);
	__m128 mul_neg_leak_ps = _mm_set_ps(1.0 - ((float)ileakL/65536.0), 1.0 - ((float)ileakR/65536.0), 1.0 - ((float)ileakR/65536.0), 1.0 - ((float)ileakL/65536.0));
	__m128 leak_ps = _mm_set_ps((float)ileakL/65536.0, (float)ileakR/65536.0, (float)ileakR/65536.0, (float)ileakL/65536.0);
	__m128 scale_ps = _mm_set1_ps(65535.0);
	__m128 scalehalf_ps = _mm_set1_ps(32767.0);
	__m128 zero_ps = _mm_set1_ps(0.0);
	__m128 rgb_ps, alt_rgb_ps;
	__m128i sub_epi32;
	__m128 sub_ps;

	for(x=0;x<width6;x+=6) // two RGB pairs
	{
		int R,G,B;
		RGBRGB = _mm_loadu_si128((__m128i *)sbase); 

		R = _mm_extract_epi16(RGBRGB, 0);
		G = _mm_extract_epi16(RGBRGB, 1);
		B = _mm_extract_epi16(RGBRGB, 2);
		G+=B;
		G>>=1;

		sub_epi32 = _mm_set_epi32(G,R,R,G);
		sub_ps = _mm_cvtepi32_ps(sub_epi32); // range 0 to 65535.0
		sub_ps = _mm_div_ps(sub_ps, scale_ps); // range 0 to 1.0
		sub_ps = _mm_mul_ps(sub_ps, sub_ps); // square

		rgb_epi32 = _mm_unpacklo_epi16(RGBRGB, zero_epi128);
		rgb_ps = _mm_cvtepi32_ps(rgb_epi32); // range 0 to 65535.0
		rgb_ps = _mm_div_ps(rgb_ps, scale_ps); // range 0 to 1.0
		rgb_ps = _mm_mul_ps(rgb_ps, rgb_ps); // square
		rgb_ps = _mm_mul_ps(rgb_ps, mul_neg_leak_ps); // [R*(1.0-fleakL)] + fleakL - (G+B)*0.5*fleakL;
		rgb_ps = _mm_add_ps(rgb_ps, leak_ps); // R*(1.0-fleakL) [+ fleakL] - (G+B)*0.5*fleakL;
		sub_ps = _mm_mul_ps(sub_ps, leak_ps); // R*(1.0-fleakL) + fleakL - [(G+B)*0.5*fleakL;]
		rgb_ps = _mm_sub_ps(rgb_ps, sub_ps); // R*(1.0-fleakL) + fleakL] [- (G+B)*0.5*fleakL;]

		rgb_ps = _mm_max_ps(rgb_ps, zero_ps);	// if(x < 0) x= 0;
		rgb_ps = _mm_sqrt_ps(rgb_ps);			// sqrt()
		rgb_ps = _mm_mul_ps(rgb_ps, scalehalf_ps); // range 0 to 32767
		RGB1 = _mm_cvtps_epi32(rgb_ps);
		RGB1 = _mm_packs_epi32 (RGB1, zero_epi128);
		RGB1 = _mm_slli_si128(RGB1, 10);
		RGB1 = _mm_srli_si128(RGB1, 10);

		
		RGBRGB = _mm_srli_si128(RGBRGB, 6);

		R = _mm_extract_epi16(RGBRGB, 0);
		G = _mm_extract_epi16(RGBRGB, 1);
		B = _mm_extract_epi16(RGBRGB, 2);
		G+=B;
		G>>=1;

		sub_epi32 = _mm_set_epi32(G,R,R,G);
		sub_ps = _mm_cvtepi32_ps(sub_epi32); // range 0 to 65535.0
		sub_ps = _mm_div_ps(sub_ps, scale_ps); // range 0 to 1.0
		sub_ps = _mm_mul_ps(sub_ps, sub_ps); // square

		rgb_epi32 = _mm_unpacklo_epi16(RGBRGB, zero_epi128);
		rgb_ps = _mm_cvtepi32_ps(rgb_epi32); // range 0 to 65535.0
		rgb_ps = _mm_div_ps(rgb_ps, scale_ps); // range 0 to 1.0
		rgb_ps = _mm_mul_ps(rgb_ps, rgb_ps); // square
		rgb_ps = _mm_mul_ps(rgb_ps, mul_neg_leak_ps); // [R*(1.0-fleakL)] + fleakL - (G+B)*0.5*fleakL;
		rgb_ps = _mm_add_ps(rgb_ps, leak_ps); // R*(1.0-fleakL) [+ fleakL] - (G+B)*0.5*fleakL;
		sub_ps = _mm_mul_ps(sub_ps, leak_ps); // R*(1.0-fleakL) + fleakL - [(G+B)*0.5*fleakL;]
		rgb_ps = _mm_sub_ps(rgb_ps, sub_ps); // R*(1.0-fleakL) + fleakL] [- (G+B)*0.5*fleakL;]

		rgb_ps = _mm_max_ps(rgb_ps, zero_ps);	// if(x < 0) x= 0;
		rgb_ps = _mm_sqrt_ps(rgb_ps);			// sqrt()
		rgb_ps = _mm_mul_ps(rgb_ps, scalehalf_ps); // range 0 to 32767
		RGB2 = _mm_cvtps_epi32(rgb_ps);
		RGB2 = _mm_packs_epi32 (RGB2, zero_epi128);
		RGB2 = _mm_slli_si128(RGB2, 6);

		RGB1 = _mm_adds_epi16(RGB1, RGB2);
		RGB1 = _mm_slli_epi16(RGB1, 1);
		RGB1 = _mm_slli_si128(RGB1, 4);
		RGB1 = _mm_srli_si128(RGB1, 4);
		
		RGBRGB = _mm_srli_si128(RGBRGB, 6);
		RGBRGB = _mm_slli_si128(RGBRGB, 12);
		RGBRGB = _mm_adds_epi16(RGB1, RGBRGB);

		_mm_storeu_si128((__m128i *)sbase, RGBRGB);
		sbase += 6;
	}
#endif
}

void GhostBustAB(DECODER *decoder, unsigned short *sbase, int width, int ileakL, int ileakR)
{
	int x,R,G,B;
	int nR,nG,nB;
	int max = 1024*1024-1;
	unsigned short *sqrttable = decoder->sqrttable;
	ileakL>>=6;
	ileakR>>=6;

	if(sqrttable == NULL)
		return;

	for(x=0;x<width;x++)
	{
		R = sbase[0]>>6;
		G = sbase[1]>>6; //10-bit
		B = sbase[2]>>6;
		R*=R;
		G*=G;	//20-bit
		B*=B;

		nR = R*(1023-ileakL) + ileakL*max - B*ileakL;
		nG = G*(1023-ileakL) + ileakL*max - B*ileakL;
		nB = B*(1023-ileakR) + ileakR*max - ((R+G)>>1)*ileakR;

		nR >>= 10; //20-bit
		nG >>= 10;
		nB >>= 10;

		if(nR>max) nR=max; if(nR<0) nR=0;
		if(nG>max) nG=max; if(nG<0) nG=0;
		if(nB>max) nB=max; if(nB<0) nB=0;

		if(sqrttable[nR] == 65535)
			sqrttable[nR] = (int)sqrt(nR);
		if(sqrttable[nG] == 65535)
			sqrttable[nG] = (int)sqrt(nG);
		if(sqrttable[nB] == 65535)
			sqrttable[nB] = (int)sqrt(nB);
		sbase[0] = sqrttable[nR]<<6;
		sbase[1] = sqrttable[nG]<<6;
		sbase[2] = sqrttable[nB]<<6;
		sbase += 3;
	}
}

void GhostBustGM(DECODER *decoder, unsigned short *sbase, int width, int ileakL, int ileakR)
{
	int x,R,G,B;
	int nR,nG,nB;
	int max = 1024*1024-1;
	unsigned short *sqrttable = decoder->sqrttable;
	ileakL>>=6;
	ileakR>>=6;

	if(sqrttable == NULL)
		return;

	for(x=0;x<width;x++)
	{
		R = sbase[0]>>6;
		G = sbase[1]>>6; //10-bit
		B = sbase[2]>>6;
		R*=R;
		G*=G;	//20-bit
		B*=B;

		nR = R*(1023-ileakL) + ileakL*max - G*ileakL;
		nG = G*(1023-ileakR) + ileakR*max - ((R+B)>>1)*ileakR;
		nB = B*(1023-ileakL) + ileakL*max - G*ileakL;

		nR >>= 10; //20-bit
		nG >>= 10;
		nB >>= 10;

		if(nR>max) nR=max; if(nR<0) nR=0;
		if(nG>max) nG=max; if(nG<0) nG=0;
		if(nB>max) nB=max; if(nB<0) nB=0;

		if(sqrttable[nR] == 65535)
			sqrttable[nR] = (int)sqrt(nR);
		if(sqrttable[nG] == 65535)
			sqrttable[nG] = (int)sqrt(nG);
		if(sqrttable[nB] == 65535)
			sqrttable[nB] = (int)sqrt(nB);
		sbase[0] = sqrttable[nR]<<6;
		sbase[1] = sqrttable[nG]<<6;
		sbase[2] = sqrttable[nB]<<6;
		sbase += 3;
	}
}


void ProcessLine3D(DECODER *decoder, uint8_t *buffer, int bufferremain, uint8_t *output, int pitch, uint8_t *source_buffer, int source_pitch, int channel_offset, int y, int blank)
{
	uint16_t *scratchline,*scratchline2,*scratchline3;
	uint16_t *sptr;
	uint16_t *srclineA,*srclineB;
	uint16_t *dstlineA,*dstlineB;
	int x,y2;
	int width = decoder->frame.width;
	int height = decoder->frame.height;
	int skip = 3;
	int sskip = 3;
	uint8_t *bptr1;
	uint8_t *bptr2;
	uint8_t *baseptr1;
	uint8_t *baseptr2;
	float windowMaskL = decoder->cfhddata.channel[0].FloatingWindowMaskL;
	float windowMaskR = decoder->cfhddata.channel[0].FloatingWindowMaskR;
	float frameTilt = decoder->cfhddata.channel[0].FrameTilt;
	float horizOffset = decoder->cfhddata.channel[1].HorizontalOffset;
	float horizOffsetR = decoder->cfhddata.channel[2].HorizontalOffset;
	float rotOffset = decoder->cfhddata.channel[1].RotationOffset;
	float rotOffsetR = decoder->cfhddata.channel[2].RotationOffset;
	float horizOffsetStep = 0;
	float horizOffsetStepR = 0;
	int flip1=0,flip2=0;
	int channel_flip = decoder->cfhddata.channel_flip;
	int source_pitch1 = source_pitch;
	int source_pitch2 = source_pitch;
	uint8_t *outputline = output+y*pitch;
	uint8_t *outputline2 = NULL;
	float horizOffsetBase;
	float rotOffsetBase;
	float horizOffsetBaseR;
	float rotOffsetBaseR;
	int formatdone = 0;
	float xmin = decoder->cfhddata.channel[0].FrameMask.topLftX;
	float xmax = decoder->cfhddata.channel[0].FrameMask.topRgtX;	
	//float ymin = decoder->cfhddata.channel[0].FrameMask.topLftY;
	float ymax = decoder->cfhddata.channel[0].FrameMask.botLftY;
	float zoom;
	float zoomR;
	float frameZoom1 = decoder->cfhddata.channel[1].FrameZoom;
	float frameZoom2 = decoder->cfhddata.channel[2].FrameZoom;
	float frameAutoZoom = decoder->cfhddata.channel[0].FrameAutoZoom;
	float frameDiffZoom1 = decoder->cfhddata.channel[1].FrameDiffZoom;
	float frameDiffZoom2 = decoder->cfhddata.channel[2].FrameDiffZoom;
	float frameHDynamic = decoder->cfhddata.FrameHDynamic;
	float frameHDynCenter = decoder->cfhddata.FrameHDynCenter;
	float frameHDynWidth = decoder->cfhddata.FrameHDynWidth;
	float frameHScale = decoder->cfhddata.FrameHScale;
	int alphachannel = 0;
	int whitepoint = 16;
	float blursharpenL = decoder->cfhddata.channel[1].user_blur_sharpen;
	float blursharpenR = decoder->cfhddata.channel[2].user_blur_sharpen;
	float vignette = decoder->cfhddata.channel[0].user_vignette_start;
	int flip_LR = 0;
	float vig_r1;
	float vig_r2;
	float vig_gain;

	if(blank) // blankline, no shifts required 
	{		
		windowMaskL = 0;
		windowMaskR = 0;
		frameTilt = 0;
		horizOffset = 0;
		horizOffsetR = 0;
		rotOffset = 0;
		rotOffsetR = 0;
		frameZoom1 = 1.0;
		frameZoom2 = 1.0;
		frameAutoZoom = 1.0;
		frameDiffZoom1 = 1.0;
		frameDiffZoom2 = 1.0;
		frameHScale = 1.0;
		frameHDynamic = 1.0;
		frameHDynCenter = 0.5;
		frameHDynWidth = 0.0;
	}

	if( decoder->StereoBufferFormat == DECODED_FORMAT_RG64 || 
		decoder->StereoBufferFormat == DECODED_FORMAT_W13A ||
		decoder->StereoBufferFormat == DECODED_FORMAT_RGB32)
	   alphachannel = 1;

	if(xmax == 0.0) xmax = 1.0;
	if(ymax == 0.0) ymax = 1.0;

	if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
	{
		width *= 2;
	}

	if(decoder->source_channels < 2) // 2D
	{
		channel_flip &= 0x3;
		channel_flip |= channel_flip<<2;
		decoder->cfhddata.channel_flip = channel_flip;
	}

	if(!(decoder->cfhddata.process_path_flags & PROCESSING_COLORMATRIX) || 
		decoder->frame.resolution == DECODED_RESOLUTION_QUARTER || 
		decoder->frame.resolution == DECODED_RESOLUTION_LOWPASS_ONLY ||
		decoder->frame.resolution == DECODED_RESOLUTION_QUARTER_NODEBAYER_SCALED)
	{
		blursharpenL = 0.0;
		blursharpenR = 0.0;
	}

	if(!(decoder->cfhddata.process_path_flags & PROCESSING_ORIENTATION))
	{
		horizOffset = rotOffset = 0;
		horizOffsetR = rotOffsetR = 0;
		frameTilt = 0;
		frameAutoZoom = 1.0;
		frameDiffZoom1 = 1.0;
		frameDiffZoom2 = 1.0;
	}
	
	if(!(decoder->cfhddata.process_path_flags & PROCESSING_IMAGEFLIPS))
	{
		channel_flip = 0;
	}

	if(decoder->cfhddata.process_path_flags & PROCESSING_FRAMING)
	{
		horizOffset += decoder->cfhddata.FrameOffsetX;
		horizOffsetR -= decoder->cfhddata.FrameOffsetX;
		frameZoom1 += frameHScale - 1.0f;
		frameZoom2 += frameHScale - 1.0f;

		if(frameHDynamic != 1.0)
		{
			frameZoom1 += 0.00001f;
			frameZoom2 += 0.00001f;
		}

		if(vignette != 0.0)
		{
			float vig_diag = sqrtf(1.0f + ((float)decoder->frame.height / (float) decoder->frame.width) * ((float)decoder->frame.height / (float) decoder->frame.width));

			vig_r1 = (vignette+1.0f); 
			vig_r2 = (decoder->cfhddata.channel[0].user_vignette_end+1.0f);
			vig_gain = decoder->cfhddata.channel[0].user_vignette_gain;

			vig_r1 *= vig_diag;
			vig_r2 *= vig_diag;
		}
	}
	else
	{
		frameZoom1 = 1.0f;
		frameZoom2 = 1.0f;
		vignette = 0;
	}

	zoom = frameZoom1 * frameAutoZoom * frameDiffZoom1;
	if(frameDiffZoom2 != 0.0)
		zoomR = frameZoom2 * frameAutoZoom / frameDiffZoom2;
	else
		zoomR = 0.0;

	if(decoder->cfhddata.process_path_flags & PROCESSING_FRAMING)
	{
		if(decoder->cfhddata.InvertOffset)
		{
			rotOffset = -rotOffset;
			rotOffsetR = -rotOffsetR;

			rotOffset -= decoder->cfhddata.FrameOffsetR;
			rotOffsetR -= -decoder->cfhddata.FrameOffsetR;
		}
		else
		{
			rotOffset += decoder->cfhddata.FrameOffsetR;
			rotOffsetR += -decoder->cfhddata.FrameOffsetR;
		}
	}
	


	rotOffsetBase = rotOffset;
	horizOffsetBase = horizOffset;
	rotOffsetBaseR = rotOffsetR;
	horizOffsetBaseR = horizOffsetR;

	horizOffset -= rotOffset * 0.5f;
	horizOffsetStep = rotOffset / (float)height;
	horizOffsetR -= rotOffsetR * 0.5f;
	horizOffsetStepR = rotOffsetR / (float)height;

	horizOffset += horizOffsetStep * y;
	horizOffsetR += horizOffsetStepR * y;

	assert(bufferremain >= width * 8 * 2 * 2);

	baseptr1 = source_buffer;
	baseptr2 = source_buffer + channel_offset;

	if(channel_flip & 0xf)
	{
		if(channel_flip & 1)
		{
			flip1 = 1;
		}
		if(channel_flip & 4)
		{
			flip2 = 1;
		}
	}

	if(source_pitch1 < 0)
		flip_LR = 1;

	decoder->sharpen_flip = 0;
	if(channel_flip & 2) //ProcessLine3D
	{
		if(decoder->channel_blend_type == BLEND_NONE && decoder->channel_current == 1) // right channel only (stored in baseptr1)
		{
		}
		else
		{
			baseptr1 += source_pitch1*(height-1);
			source_pitch1 = -source_pitch1;
			decoder->sharpen_flip = 1;
		}
	}
	if(channel_flip & 8)
	{
		if(decoder->channel_blend_type == BLEND_NONE && decoder->channel_current == 1) // right channel only (stored in baseptr1)
		{
			baseptr1 += source_pitch1*(height-1);
			source_pitch1 = -source_pitch1;
			decoder->sharpen_flip = 1;
		}
		else
		{
			baseptr2 += source_pitch2*(height-1);
			source_pitch2 = -source_pitch2;
		}
	}

	bptr1 = baseptr1 + y*source_pitch1;
	bptr2 = baseptr2 + y*source_pitch2;
	
	y2 = y;
	if(decoder->channel_blend_type == BLEND_FREEVIEW) //FreeView
	{
		if(y2 < height/4)
		{
			blank = 1;
			y2 = 0;
		} 
		else
		{
			y2 -= height/4;
			y2 *= 2;

			if(y2 >= height-1)
			{
				blank = 1;
				y2 = height - 2;
			}
		}
		bptr1 = baseptr1 + y2*source_pitch1;
		bptr2 = baseptr2 + y2*source_pitch2;
	}

	srclineA = (uint16_t *)bptr1;
	srclineB = (uint16_t *)bptr2;

	scratchline = (uint16_t *)buffer;
	scratchline2 = (uint16_t *)(buffer + width * 6 + width) /* as we pad the line */ ;;
	scratchline3 = (uint16_t *)(buffer + width * 6*2 + width*2) /* as we pad the line */ ;

	if(alphachannel)
	{
		scratchline = (uint16_t *)buffer;
		scratchline2 = (uint16_t *)(buffer + width * 8 + width) /* as we pad the line */ ;;
		scratchline3 = (uint16_t *)(buffer + width * 8*2 + width*2) /* as we pad the line */ ;
	}

	dstlineA = sptr = scratchline;
	dstlineB = scratchline3;


	switch(decoder->StereoBufferFormat)
	{
	case DECODED_FORMAT_RG64:
		whitepoint = 16;
		skip = 8;
		sskip = 4;
		break;
	case DECODED_FORMAT_W13A:
		whitepoint = 13;
		skip = 8;
		sskip = 4;
		break;
	case DECODED_FORMAT_WP13:
		whitepoint = 13;
		skip = 6;
		sskip = 3;
		break;
	case DECODED_FORMAT_RG48:
		skip = 6;
		sskip = 3;
		break;
	case DECODED_FORMAT_RGB32:
		skip = 4;
		break;
	case DECODED_FORMAT_RGB24:
		skip = 3;
		break;
	case DECODED_FORMAT_YUYV:
		skip = 2;
		break;
	}

	if(blank)
	{
		if(srclineA)
			memset(srclineA, 0, width*skip);
		if(srclineB && decoder->channel_decodes > 1)
			memset(srclineB, 0, width*skip);
	}

	

	if(blursharpenL != 0.0 || blursharpenR != 0.0)
	{
		if(decoder->channel_blend_type == BLEND_FREEVIEW || 
			decoder->channel_blend_type == BLEND_STACKED_ANAMORPHIC ||
			decoder->channel_blend_type == BLEND_LINE_INTERLEAVED
			)
		{
			decoder->doVerticalFilter = 0;
		}
		else
		{
			decoder->doVerticalFilter = 1;
		}
	}

	{
		switch(decoder->channel_blend_type)
		{
		case BLEND_FREEVIEW:
		case BLEND_SIDEBYSIDE_ANAMORPHIC: //side by side
			if(!blank)
			{
				if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL || decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER)
				{
					dstlineA = srclineA;
					sptr = dstlineA;

					if(zoom != 1.0 || zoomR != 1.0 || horizOffsetR || horizOffset || channel_flip || frameTilt)
					{
						if(!alphachannel)
						{
							if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
							{
								RGB48HoriShift(decoder, srclineA, scratchline2, width/2, -horizOffset, flip1);
								RGB48HoriShift(decoder, srclineB, scratchline2, width/2, horizOffsetR, flip2);
							}
							else
							{
								RGB48HoriShiftZoom(decoder, srclineA, scratchline2, width/2, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);
								RGB48HoriShiftZoom(decoder, srclineB, scratchline2, width/2, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR, flip2, frameTilt, 1);
							}
						}
						else
						{
							if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
							{
								RGBA64HoriShift(decoder, srclineA, scratchline2, width/2, -horizOffset, flip1);
								RGBA64HoriShift(decoder, srclineB, scratchline2, width/2, horizOffsetR, flip2);
							}
							else
							{
								RGBA64HoriShiftZoom(decoder, srclineA, scratchline2, width/2, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);
								RGBA64HoriShiftZoom(decoder, srclineB, scratchline2, width/2, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR, flip2, frameTilt, 1);
							}
						}
					}

					if(vignette != 0.0)
					{
						int cwidth= width/2;
						if(decoder->channel_blend_type == BLEND_SIDEBYSIDE_ANAMORPHIC)
							cwidth= width;

						FastVignetteInplaceWP13(decoder, width/2, cwidth, height, y, vig_r1, vig_r2, vig_gain,
							  (int16_t *)srclineA, decoder->frame.resolution, skip);
						FastVignetteInplaceWP13(decoder, width/2, cwidth, height, y, vig_r1, vig_r2, vig_gain,
							  (int16_t *)srclineB, decoder->frame.resolution, skip);
					}

					if(blursharpenL != 0.0) FastSharpeningBlurHinplaceWP13(width/2, (int16_t *)srclineA, blursharpenL, decoder->frame.resolution, skip);
					if(blursharpenR != 0.0) FastSharpeningBlurHinplaceWP13(width/2, (int16_t *)srclineB, blursharpenR, decoder->frame.resolution, skip);

					memcpy(dstlineA+sskip*(width/2), srclineB, width/2*sskip*2);
				}
				else
				{

					int16_t *ptr;
					int16_t *ptr1 = (int16_t *)srclineA;
					int16_t *ptr2 = (int16_t *)srclineB;

					if(!alphachannel)
					{
						if(zoom != 1.0 || zoomR != 1.0 || horizOffsetR || horizOffset || channel_flip || frameTilt)
						{					
							if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
							{
								RGB48HoriShift(decoder, srclineA, scratchline2, width, -horizOffset, flip1);
								RGB48HoriShift(decoder, srclineB, scratchline2, width, horizOffset, flip2);
							}
							else
							{
								RGB48HoriShiftZoom(decoder, srclineA, scratchline2, width, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);
								RGB48HoriShiftZoom(decoder, srclineB, scratchline2, width, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR, flip2, frameTilt, 1);
							}
						}
					}
					else
					{
						if(zoom != 1.0 || zoomR != 1.0 || horizOffsetR || horizOffset || channel_flip || frameTilt)
						{					
							if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
							{
								RGBA64HoriShift(decoder, srclineA, scratchline2, width, -horizOffset, flip1);
								RGBA64HoriShift(decoder, srclineB, scratchline2, width, horizOffset, flip2);
							}
							else
							{
								RGBA64HoriShiftZoom(decoder, srclineA, scratchline2, width, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);
								RGBA64HoriShiftZoom(decoder, srclineB, scratchline2, width, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR, flip2, frameTilt, 1);
							}
						}
					}
					
					if(vignette != 0.0)
					{
						int cwidth= width/2;
						if(decoder->channel_blend_type == BLEND_SIDEBYSIDE_ANAMORPHIC)
							cwidth= width;

						FastVignetteInplaceWP13(decoder, width, cwidth, height, y, vig_r1, vig_r2, vig_gain,
							  (int16_t *)srclineA, decoder->frame.resolution, skip);
						FastVignetteInplaceWP13(decoder, width, cwidth, height, y, vig_r1, vig_r2, vig_gain,
							  (int16_t *)srclineB, decoder->frame.resolution, skip);
					}
						
					if(blursharpenL != 0.0) FastSharpeningBlurHinplaceWP13(width, (int16_t *)srclineA, blursharpenL, decoder->frame.resolution, skip);
					if(blursharpenR != 0.0) FastSharpeningBlurHinplaceWP13(width, (int16_t *)srclineB, blursharpenR, decoder->frame.resolution, skip);

                    dstlineA = srclineA;
					ptr = (int16_t *)srclineA;
					for(x=0; x<width/2; x++)
					{
						*ptr++ = (ptr1[0]+ptr1[3])>>1;
						*ptr++ = (ptr1[1]+ptr1[4])>>1;
						*ptr++ = (ptr1[2]+ptr1[5])>>1 ;

						ptr1+=sskip*2;
					}
					for(; x<width; x++)
					{
						*ptr++ = (ptr2[0]+ptr2[3])>>1;
						*ptr++ = (ptr2[1]+ptr2[4])>>1;
						*ptr++ = (ptr2[2]+ptr2[5])>>1;

						ptr2+=sskip*2;
					}
				}


				if(windowMaskL || xmin)
				{
					float mask =  windowMaskL > xmin ? windowMaskL : xmin;
					RGB48WindowMask(decoder, dstlineA, width/2, 0, mask);

					if(windowMaskL < 0)
						RGB48WindowMask(decoder, dstlineA, width/2, 0, windowMaskL);

					if(xmin)
					{
						RGB48WindowMask(decoder, dstlineA, width/2, 1, xmin);
					}
				}		
				if(windowMaskR || (1.0-xmax))
				{
					float mask =  windowMaskR > (1.0f-xmax) ? windowMaskR : (1.0f-xmax);
					RGB48WindowMask(decoder, dstlineA+width*sskip/2, width/2, 1, mask);
					
					if(windowMaskR < 0)
						RGB48WindowMask(decoder, dstlineA+width*sskip/2, width/2, 1, windowMaskR);

					if(xmin)
					{
						RGB48WindowMask(decoder, dstlineA+width*sskip/2, width/2, 0, xmin);
					}
				}

				if(decoder->channel_swapped_flags & FLAG3D_GHOSTBUST)
				{
					if(decoder->ghost_bust_left || decoder->ghost_bust_right)
					{
						GhostBust(decoder, dstlineA, dstlineA+width*sskip/2, width/2, decoder->ghost_bust_left, decoder->ghost_bust_right);
					}
				}

				if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
				{
					memcpy(scratchline2+width*sskip/2, dstlineA, width*sskip*2/2);
					memcpy(dstlineA, dstlineA+width*sskip/2, width*sskip*2/2);
					memcpy(dstlineA+width*sskip/2, scratchline2+width*sskip/2, width*sskip*2/2);
				}
			}
			break;



		case BLEND_STACKED_ANAMORPHIC: //stacked
		case BLEND_LINE_INTERLEAVED: //fields
			if((y & 1) == 1) return;  

			if(!blank)
			{
				uint16_t *ptrA1 = (uint16_t *)srclineA;
				uint16_t *ptrA2 = (uint16_t *)srclineA + (source_pitch1>>1);
				uint16_t *ptrB1 = (uint16_t *)srclineB;
				uint16_t *ptrB2 = (uint16_t *)srclineB + (source_pitch2>>1);

				
				FastBlendWP13((short *)ptrA1, (short *)ptrA2, (short *)ptrA1/*output*/, width*skip);
				FastBlendWP13((short *)ptrB1, (short *)ptrB2, (short *)ptrB1/*output*/, width*skip);

				if(zoom != 1.0 || zoomR != 1.0 || horizOffset || horizOffsetR || channel_flip || frameTilt)
				{						
					if(!alphachannel)
					{
						if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
						{
							RGB48HoriShift(decoder, srclineA, scratchline2, width, -horizOffset, flip1);
							RGB48HoriShift(decoder, srclineB, scratchline2, width, horizOffsetR, flip2);
						}
						else
						{
							RGB48HoriShiftZoom(decoder, srclineA, scratchline2, width, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);
							RGB48HoriShiftZoom(decoder, srclineB, scratchline2, width, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR,  flip2, frameTilt, 1);
						}
					}
					else
					{
						if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
						{
							RGBA64HoriShift(decoder, srclineA, scratchline2, width, -horizOffset, flip1);
							RGBA64HoriShift(decoder, srclineB, scratchline2, width, horizOffsetR, flip2);
						}
						else
						{
							RGBA64HoriShiftZoom(decoder, srclineA, scratchline2, width, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);
							RGBA64HoriShiftZoom(decoder, srclineB, scratchline2, width, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR,  flip2, frameTilt, 1);
						}
					}
				}

				if(vignette != 0.0)
				{
					FastVignetteInplaceWP13(decoder, width, width, height, y, vig_r1, vig_r2, vig_gain,
						  (short *)srclineA, decoder->frame.resolution, skip);
					FastVignetteInplaceWP13(decoder, width, width, height, y, vig_r1, vig_r2, vig_gain,
						  (short *)srclineB, decoder->frame.resolution, skip);
				}

				if(blursharpenL != 0.0) FastSharpeningBlurHinplaceWP13(width, (short *)srclineA, blursharpenL, decoder->frame.resolution, skip);
				if(blursharpenR != 0.0) FastSharpeningBlurHinplaceWP13(width, (short *)srclineB, blursharpenR, decoder->frame.resolution, skip);

				
				if(windowMaskL || xmin)
				{
					float mask =  windowMaskL > xmin ? windowMaskL : xmin;
					RGB48WindowMask(decoder, srclineA, width, 0, mask);
						
					if(windowMaskL < 0)
						RGB48WindowMask(decoder, srclineA, width, 0, windowMaskL);
					
					if(xmin)
					{
						RGB48WindowMask(decoder, srclineA, width, 1, xmin);
					}
				}		
				if(windowMaskR || (1.0-xmax))
				{
					float mask =  windowMaskR > (1.0f-xmax) ? windowMaskR : (1.0f-xmax);
					RGB48WindowMask(decoder, srclineB, width, 1, mask);

					if(windowMaskR < 0)
						RGB48WindowMask(decoder, srclineB, width, 1, windowMaskR);

					if(xmin)
					{
						RGB48WindowMask(decoder, srclineB, width, 0, xmin);
					}
				}

				if(decoder->channel_swapped_flags & FLAG3D_GHOSTBUST)
				{
					if(decoder->ghost_bust_left || decoder->ghost_bust_right)
					{
						GhostBust(decoder, srclineA, srclineB, width, decoder->ghost_bust_left, decoder->ghost_bust_right);
					}
				}

				if(decoder->doVerticalFilter == 0)
				{
					if(decoder->channel_blend_type==BLEND_STACKED_ANAMORPHIC) //stacked
					{
						if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
						{
							outputline2 = output+(y>>1)*pitch;
							outputline = output+((y>>1)+(height/2))*pitch;
						}
						else
						{
							outputline = output+(y>>1)*pitch;
							outputline2 = output+((y>>1)+(height/2))*pitch;
						}
					}
					else //fields
					{
						if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
						{
							outputline = output+(y)*pitch;
							outputline2 = output+(y+1)*pitch;
						}
						else
						{
							outputline2 = output+(y)*pitch;
							outputline = output+(y+1)*pitch;
						}
					}
					
					if(flip_LR/*source_pitch1 < 0*/) // flip Left and Right
					{
						uint8_t *tmp = outputline2;
						outputline2 = outputline;
						outputline = tmp;
					}
				}
				else
				{
					if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
					{
						memcpy(scratchline2, srclineA, width*skip);
						memcpy(srclineA, srclineB, width*skip);
						memcpy(srclineB, scratchline2, width*skip);
					}
				}
			}
			break;


		case BLEND_ONION: //onion
		case BLEND_DIFFERENCE: //difference
		case BLEND_SPLITVIEW: //splitView
			if(!blank)
			{
				//dstlineA = source_buffer;
				//dstlineA += (source_pitch>>1) * y;
				sptr = dstlineA = srclineA;
				srclineA = (uint16_t *)bptr1;
				srclineB = (uint16_t *)bptr2;

				if(zoom != 1.0 || zoomR != 1.0 || horizOffset || horizOffsetR || channel_flip || frameTilt)
				{				
					if(!alphachannel)
					{
						if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
						{
							RGB48HoriShift(decoder, srclineA, scratchline2, width, -horizOffset, flip1);
							RGB48HoriShift(decoder, srclineB, scratchline2, width, horizOffsetR, flip2);
						}
						else
						{
							RGB48HoriShiftZoom(decoder, srclineA, scratchline2, width, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);
							RGB48HoriShiftZoom(decoder, srclineB, scratchline2, width, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR, flip2, frameTilt, 1);
						}
					}
					else
					{
						if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
						{
							RGBA64HoriShift(decoder, srclineA, scratchline2, width, -horizOffset, flip1);
							RGBA64HoriShift(decoder, srclineB, scratchline2, width, horizOffsetR, flip2);
						}
						else
						{
							RGBA64HoriShiftZoom(decoder, srclineA, scratchline2, width, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);
							RGBA64HoriShiftZoom(decoder, srclineB, scratchline2, width, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR, flip2, frameTilt, 1);
						}
					}
				}
				
				if(vignette != 0.0)
				{
					FastVignetteInplaceWP13(decoder, width, width, height, y, vig_r1, vig_r2, vig_gain,
						  (short *)srclineA, decoder->frame.resolution, skip);
					FastVignetteInplaceWP13(decoder, width, width, height, y, vig_r1, vig_r2, vig_gain,
						  (short *)srclineB, decoder->frame.resolution, skip);
				}

				if(blursharpenL != 0.0) FastSharpeningBlurHinplaceWP13(width, (short *)srclineA, blursharpenL, decoder->frame.resolution, skip);
				if(blursharpenR != 0.0) FastSharpeningBlurHinplaceWP13(width, (short *)srclineB, blursharpenR, decoder->frame.resolution, skip);

				if(windowMaskL || xmin)
				{
					float mask =  windowMaskL > xmin ? windowMaskL : xmin;
					RGB48WindowMask(decoder, srclineA, width, 0, mask);
					
					if(windowMaskL < 0)
						RGB48WindowMask(decoder, srclineA, width, 0, windowMaskL);

					if(xmin)
					{
						RGB48WindowMask(decoder, srclineA, width, 1, xmin);
					}
				}
				
				if(windowMaskR || (1.0-xmax))
				{
					float mask =  windowMaskR > (1.0f-xmax) ? windowMaskR : (1.0f-xmax);
					RGB48WindowMask(decoder, srclineB, width, 1, mask);

					if(windowMaskR < 0)
						RGB48WindowMask(decoder, srclineB, width, 1, windowMaskR);

					if(xmin)
					{
						RGB48WindowMask(decoder, srclineB, width, 0, xmin);
					}
				}

				x = 0;
				if(decoder->channel_blend_type == BLEND_SPLITVIEW) //split view
				{	
					int xsplit = width * (decoder->cfhddata.split_pos_xy & 0xff) / 255;
					for(x = xsplit*sskip; x<width*sskip; x++)
					{
						srclineA[x] = srclineB[x];
					}
				}
				else if(decoder->channel_blend_type == BLEND_ONION) //onion
				{
					FastBlendWP13((short *)srclineA, (short *)srclineB, (short *)dstlineA/*output*/, width*skip);
				}
				else if(decoder->channel_blend_type == BLEND_DIFFERENCE) //difference
				{
#if XMMOPT
					int width8 = (width*sskip) & 0xfff8;
					__m128i mid_epi16;
					//int unaligned = ((int)sbase) & 15;
					//unaligned += ((int)in_rgb8) & 15;
					if(whitepoint == 13)
						mid_epi16 = _mm_set1_epi16(0x0fff);
					else
						mid_epi16 = _mm_set1_epi16(0x1fff);

					for(x=0; x<width8; x+=8)
					{
						__m128i rgb16A = _mm_load_si128((__m128i *)&srclineA[x]);
						__m128i rgb16B = _mm_load_si128((__m128i *)&srclineB[x]);

						// 0 to 0xffff
						if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
						{
							rgb16A = _mm_subs_epi16(rgb16B, rgb16A); // -3fff to 3fff
						}
						else
						{
							rgb16A = _mm_subs_epi16(rgb16A, rgb16B);
						}
						rgb16A = _mm_adds_epi16(rgb16A, mid_epi16); // -0x1fff to 0x5fff , avg 0x1fff

						
						_mm_store_si128((__m128i *)&dstlineA[x], rgb16A);
					}
#endif
					for(; x<width*sskip; x++)
					{
						int val;

						if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
						{
							val = (srclineB[x] - srclineA[x]) + 32768;
						}
						else
						{
							val = (srclineA[x] - srclineB[x]) + 32768;
						}
						if(val > 0x7fff) val = 0x7fff;
						if(val < 0) val = 0;
						dstlineA[x] = val;
					}
				}
			}
			break;

		case BLEND_ANAGLYPH_RC:
		case BLEND_ANAGLYPH_RC_BW:
		case BLEND_ANAGLYPH_AB:
		case BLEND_ANAGLYPH_AB_BW:
		case BLEND_ANAGLYPH_GM:
		case BLEND_ANAGLYPH_GM_BW:
		case BLEND_ANAGLYPH_DUBOIS: //Optimized
			{
				uint16_t *sptr1 = scratchline2;
				uint16_t *sptr2 = scratchline3;

				dstlineA = (uint16_t *)bptr1;
			//	dstlineA += (source_pitch>>1) * y;
				sptr = dstlineA;
				sptr1 = srclineA = (uint16_t *)bptr1;
				sptr2 = srclineB = (uint16_t *)bptr2;
			
				if(zoom != 1.0 || zoomR != 1.0 || horizOffset || horizOffsetR || channel_flip || frameTilt)
				{
					if(!alphachannel)
					{
						if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)			
						{
							RGB48HoriShift(decoder, srclineA, scratchline, width, -horizOffset, flip1);
							RGB48HoriShift(decoder, srclineB, scratchline, width, horizOffsetR, flip2);
						}
						else
						{
							RGB48HoriShiftZoom(decoder, srclineA, scratchline, width, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);
							RGB48HoriShiftZoom(decoder, srclineB, scratchline, width, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR, flip2, frameTilt, 1);
						}
					}
					else
					{
						if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)			
						{
							RGBA64HoriShift(decoder, scratchline2, scratchline, width, -horizOffset, flip1);
							RGBA64HoriShift(decoder, scratchline3, scratchline, width, horizOffsetR, flip2);
						}
						else
						{
							RGBA64HoriShiftZoom(decoder, scratchline2, scratchline, width, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);
							RGBA64HoriShiftZoom(decoder, scratchline3, scratchline, width, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR, flip2, frameTilt, 1);
						}
					}
				} 

				
				if(vignette != 0.0)
				{
					FastVignetteInplaceWP13(decoder, width, width, height, y, vig_r1, vig_r2, vig_gain,
						  (short *)srclineA, decoder->frame.resolution, skip);
					FastVignetteInplaceWP13(decoder, width, width, height, y, vig_r1, vig_r2, vig_gain,
						  (short *)srclineB, decoder->frame.resolution, skip);
				}

				if(blursharpenL != 0.0) FastSharpeningBlurHinplaceWP13(width, (short *)srclineA, blursharpenL, decoder->frame.resolution, skip);
				if(blursharpenR != 0.0) FastSharpeningBlurHinplaceWP13(width, (short *)srclineB, blursharpenR, decoder->frame.resolution, skip);

				if(decoder->channel_swapped_flags & FLAG3D_GHOSTBUST)
				{
					if(decoder->ghost_bust_left || decoder->ghost_bust_right)
					{
						GhostBust(decoder, srclineA, srclineB, width, decoder->ghost_bust_left, decoder->ghost_bust_right);
					}
				}

				if(windowMaskL || xmin)
				{
					float mask =  windowMaskL > xmin ? windowMaskL : xmin;
					RGB48WindowMask(decoder, srclineA, width, 0, mask);

					if(windowMaskL < 0)
						RGB48WindowMask(decoder, srclineA, width, 0, windowMaskL);

					if(xmin)
					{
						RGB48WindowMask(decoder, srclineA, width, 1, xmin);
					}
				}		
				if(windowMaskR || (1.0-xmax))
				{
					float mask =  windowMaskR > (1.0f-xmax) ? windowMaskR : (1.0f-xmax);
					RGB48WindowMask(decoder, srclineB, width, 1, mask);
					
					if(windowMaskR < 0)
						RGB48WindowMask(decoder, srclineB, width, 1, windowMaskR);

					if(xmin)
					{
						RGB48WindowMask(decoder, srclineB, width, 0, xmin);
					}
				}
				

				if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
				{
					uint16_t *tmp = srclineA;
					srclineA = srclineB;
					srclineB = tmp;
				}


				switch(decoder->channel_blend_type)
				{
				case BLEND_ANAGLYPH_RC:
					{
						int16_t *ptr1 = (int16_t *)srclineA;
						int16_t *ptr2 = (int16_t *)srclineB;

						if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
						{
							for(x=0; x<width; x++)
							{						
								sptr[0] = ptr2[0];
								sptr[1] = ptr1[1];
								sptr[2] = ptr1[2];

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
							}
						}
						else
						{
							for(x=0; x<width; x++)
							{						
								sptr[0] = ptr1[0];
								sptr[1] = ptr2[1];
								sptr[2] = ptr2[2];

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
							}
						}
					}
					break;
				case BLEND_ANAGLYPH_RC_BW:
					{
						int16_t *ptr1 = (int16_t *)srclineA;
						int16_t *ptr2 = (int16_t *)srclineB;

						if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
						{
							for(x=0; x<width; x++)
							{						
								int y1 = (ptr1[0]*5+ptr1[1]*10+ptr1[2])>>4;
								int y2 = (ptr2[0]*5+ptr2[1]*10+ptr2[2])>>4;

								sptr[0] = y2;
								sptr[1] = y1;
								sptr[2] = y1;

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
							}
						}
						else
						{
							for(x=0; x<width; x++)
							{						
								int y1 = (ptr1[0]*5+ptr1[1]*10+ptr1[2])>>4;
								int y2 = (ptr2[0]*5+ptr2[1]*10+ptr2[2])>>4;

								sptr[0] = y1;
								sptr[1] = y2;
								sptr[2] = y2;

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
							}
						}
					}
					break;

				case BLEND_ANAGLYPH_AB:
					{
						int16_t *ptr1 = (int16_t *)srclineA;
						int16_t *ptr2 = (int16_t *)srclineB;

						if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
						{
							for(x=0; x<width; x++)
							{						
								sptr[0] = ptr2[0];
								sptr[1] = ptr2[1];
								sptr[2] = ptr1[2];

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
							}
						}
						else
						{	
							for(x=0; x<width; x++)
							{						
								sptr[0] = ptr1[0];
								sptr[1] = ptr1[1];
								sptr[2] = ptr2[2];

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
							}
						}
					}
					break;
				case BLEND_ANAGLYPH_AB_BW:
					{
						int16_t *ptr1 = (int16_t *)srclineA;
						int16_t *ptr2 = (int16_t *)srclineB;

						if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
						{
							for(x=0; x<width; x++)
							{						
								int y1 = (ptr1[0]*5+ptr1[1]*10+ptr1[2])>>4;
								int y2 = (ptr2[0]*5+ptr2[1]*10+ptr2[2])>>4;

								sptr[0] = y2;
								sptr[1] = y2;
								sptr[2] = y1;

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
							}
						}
						else
						{
							for(x=0; x<width; x++)
							{						
								int y1 = (ptr1[0]*5+ptr1[1]*10+ptr1[2])>>4;
								int y2 = (ptr2[0]*5+ptr2[1]*10+ptr2[2])>>4;

								sptr[0] = y1;
								sptr[1] = y1;
								sptr[2] = y2;

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
							}
						}
					}
					break;
				case BLEND_ANAGLYPH_GM:
					{
						int16_t *ptr1 = (int16_t *)srclineA;
						int16_t *ptr2 = (int16_t *)srclineB;

						if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
						{
							for(x=0; x<width; x++)
							{						
								sptr[0] = ptr1[0];
								sptr[1] = ptr2[1];
								sptr[2] = ptr1[2];

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
							}
						}
						else
						{
							for(x=0; x<width; x++)
							{						
								sptr[0] = ptr2[0];
								sptr[1] = ptr1[1];
								sptr[2] = ptr2[2];

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
							}
						}
					}
					break;
				case BLEND_ANAGLYPH_GM_BW:
					{
						int16_t *ptr1 = (int16_t *)srclineA;
						int16_t *ptr2 = (int16_t *)srclineB;

						if(decoder->channel_swapped_flags & FLAG3D_SWAPPED)
						{
							for(x=0; x<width; x++)
							{						
								int y1 = (ptr1[0]*5+ptr1[1]*10+ptr1[2])>>4;
								int y2 = (ptr2[0]*5+ptr2[1]*10+ptr2[2])>>4;

								sptr[0] = y1;
								sptr[1] = y2;
								sptr[2] = y1;

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
							}
						}
						else
						{
							for(x=0; x<width; x++)
							{						
								int y1 = (ptr1[0]*5+ptr1[1]*10+ptr1[2])>>4;
								int y2 = (ptr2[0]*5+ptr2[1]*10+ptr2[2])>>4;

								sptr[0] = y2;
								sptr[1] = y1;
								sptr[2] = y2;

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
							}
						}
					}
					break;
				case BLEND_ANAGLYPH_DUBOIS: //Optimized
					{
						int16_t *ptr1 = (int16_t *)srclineA;
						int16_t *ptr2 = (int16_t *)srclineB;
						int r,g,b;

						for(x=0; x<width; x++)
						{								
							r =(ptr1[0]*456 + ptr1[1]*500 + ptr1[2]*176 + ptr2[0]*-43 + ptr2[1]*-88 + ptr2[2]*-2  ) / 1000;
							g =(ptr1[0]*-40 + ptr1[1]*-38 + ptr1[2]*-16 + ptr2[0]*378 + ptr2[1]*734 + ptr2[2]*-18 ) / 1000;
							b =(ptr1[0]*-15 + ptr1[1]*-21 + ptr1[2]*-5  + ptr2[0]*-72 + ptr2[1]*-113+ ptr2[2]*1226) / 1000;

							if(r<0) r=0; if(r>0x3fff) r=0x3fff;
							if(g<0) g=0; if(g>0x3fff) g=0x3fff;
							if(b<0) b=0; if(b>0x3fff) b=0x3fff;

							sptr[0] = r;
							sptr[1] = g;
							sptr[2] = b;

								ptr1 += sskip;
								ptr2 += sskip;	
								sptr += sskip;
						}
					}
					break;
				}
			}
			break;

		case BLEND_NONE: 
		default:		
			if(decoder->channel_decodes == 1) // only one channel
			{
				if(skip == 8)
				{
					//the data is already in the correct format
					sptr = (unsigned short *)bptr1;
					// shift if needed.
					if(zoom != 1.0 || zoomR != 1.0 || horizOffsetR || horizOffset || channel_flip || frameTilt)
					{
						if(decoder->channel_current == 0)
						{
							if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
								RGBA64HoriShift(decoder, sptr, scratchline2, width, -horizOffset, flip1);
							else
								RGBA64HoriShiftZoom(decoder, sptr, scratchline2, width, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);
						}
						else
						{
							if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
								RGBA64HoriShift(decoder, sptr, scratchline2, width, horizOffsetR, flip2);
							else
								RGBA64HoriShiftZoom(decoder, sptr, scratchline2, width, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR, flip2, frameTilt, 1);
						}
					}				
				}
				else if(skip == 6)
				{
					//the data is already in the correct format
					dstlineA = sptr = (unsigned short *)srclineA;
					// shift if needed.
					if(zoom != 1.0 || zoomR != 1.0 || horizOffsetR || horizOffset || channel_flip || frameTilt)
					{
						if(decoder->channel_current == 0)
						{
							if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
								RGB48HoriShift(decoder, srclineA, scratchline2, width, -horizOffset, flip1);
							else
								RGB48HoriShiftZoom(decoder, srclineA, scratchline2, width, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);
						}
						else
						{
							if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
								RGB48HoriShift(decoder, srclineA, scratchline2, width, horizOffsetR, flip2);
							else
								RGB48HoriShiftZoom(decoder, srclineA, scratchline2, width, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR, flip2, frameTilt, 1);
						}
					}
					
					if(vignette != 0.0)
					{
						FastVignetteInplaceWP13(decoder, width, width, height, y, vig_r1, vig_r2, vig_gain,
							  (int16_t *)srclineA, decoder->frame.resolution, skip);
					}


					if(decoder->channel_current == 0)	
					{
						if(blursharpenL != 0.0) 
						{
							FastSharpeningBlurHinplaceWP13(width, (int16_t *)srclineA, blursharpenL, decoder->frame.resolution, skip);
						}
					}
					else
					{
						if(blursharpenR != 0.0) 
						{
							FastSharpeningBlurHinplaceWP13(width, (int16_t *)srclineA, blursharpenR, decoder->frame.resolution, skip);
						}
					}
				
				}

				if ((windowMaskL && decoder->channel_current == 0) || xmin)
				{
					float mask =  windowMaskL > xmin ? windowMaskL : xmin;
					if(decoder->channel_current != 0) mask = xmin;
					
					if(windowMaskL < 0)
						RGB48WindowMask(decoder, srclineA, width, 0, windowMaskL);

					RGB48WindowMask(decoder, srclineA, width, 0, mask);
				}
				
				if ((windowMaskR && decoder->channel_current == 1) || (1.0f-xmax))
				{
					float mask =  windowMaskR > (1.0f-xmax) ? windowMaskR : (1.0f-xmax);
					if(decoder->channel_current != 1) mask = (1.0f-xmax);
					
					if(windowMaskR < 0)
						RGB48WindowMask(decoder, srclineA, width, 1, windowMaskR);

					RGB48WindowMask(decoder, srclineA, width, 1, mask);
				}
			}
			else
			{
				outputline2 = output+(y+height)*pitch;
			
				if(zoom != 1.0 || zoomR != 1.0 || horizOffsetR || horizOffset || channel_flip || frameTilt)
				{					
					if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
						RGB48HoriShift(decoder, srclineA, scratchline2, width, -horizOffset, flip1);
					else
						RGB48HoriShiftZoom(decoder, srclineA, scratchline2, width, height, y, -horizOffsetBase, rotOffsetBase, zoom, flip1, frameTilt, 0);

					if(zoom == 1.0 && zoomR == 1.0 && frameTilt == 0.0)
						RGB48HoriShift(decoder, srclineB, scratchline2, width, horizOffset, flip2);
					else
						RGB48HoriShiftZoom(decoder, srclineB, scratchline2, width, height, y, horizOffsetBaseR, -rotOffsetBaseR, zoomR, flip2, frameTilt, 1);
				}

				if(windowMaskL || xmin)
				{
					float mask =  windowMaskL > xmin ? windowMaskL : xmin;
					RGB48WindowMask(decoder, srclineA, width, 0, mask);
					
					if(windowMaskL < 0)
						RGB48WindowMask(decoder, srclineA, width, 0, windowMaskL);
				}
				
				if(windowMaskR || (1.0-xmax))
				{
					float mask =  windowMaskR > (1.0f-xmax) ? windowMaskR : (1.0f-xmax);
					RGB48WindowMask(decoder, srclineB, width, 1, mask);

					if(windowMaskR < 0)
						RGB48WindowMask(decoder, srclineB, width, 1, windowMaskR);
				}

				if(decoder->channel_swapped_flags & FLAG3D_GHOSTBUST)
				{
					if(decoder->ghost_bust_left || decoder->ghost_bust_right)
					{
						GhostBust(decoder, srclineA, srclineB, width, decoder->ghost_bust_left, decoder->ghost_bust_right);
					}
				}
			}
			break;
		}
	}


	if(!formatdone)
	{
		int flags = ACTIVEMETADATA_PRESATURATED;
		int whitebitdepth = 16;
		if(decoder->StereoBufferFormat == DECODED_FORMAT_WP13 || decoder->StereoBufferFormat == DECODED_FORMAT_W13A)
		{
			flags = 0;
			whitebitdepth = 13;
		}

		if(outputline2)
		{
		//	if(decoder->cfhddata.ComputeFlags&2 && (0 == (y&3)) && decoder->tools)
		//		HistogramLine(decoder, srclineA, width, DECODED_FORMAT_RG48, whitebitdepth);

			if(decoder->doVerticalFilter == 0) // No sharp stage so output now
			{
				if(alphachannel)				
					Convert4444LinesToOutput(decoder, width, 1, y, srclineA,
						outputline, pitch, decoder->frame.format, whitebitdepth, flags);
				else
					ConvertLinesToOutput(decoder, width, 1, y, srclineA,
						outputline, pitch, decoder->frame.format, whitebitdepth, flags);

				//if(decoder->cfhddata.ComputeFlags&2 && (0 == (y&3)) && decoder->tools)
				//	HistogramLine(decoder, dstlineA, width, DECODED_FORMAT_RG48, whitebitdepth);

				if(alphachannel)	
					Convert4444LinesToOutput(decoder, width, 1, y, srclineB,
						outputline2, pitch, decoder->frame.format, whitebitdepth, flags);
				else
					ConvertLinesToOutput(decoder, width, 1, y, srclineB,
						outputline2, pitch, decoder->frame.format, whitebitdepth, flags);
			}
		}
		else
		{
			//if(decoder->cfhddata.ComputeFlags&2 && (0 == (y&3)) && decoder->tools)
			//{
			//	if(alphachannel)
			//		HistogramLine(decoder, srclineA, width, DECODED_FORMAT_RG64, whitebitdepth);
			//	else
			//		HistogramLine(decoder, srclineA, width, DECODED_FORMAT_RG48, whitebitdepth);
			//} 

			if(decoder->doVerticalFilter == 0) // No sharp stage so output now
			{
				if(alphachannel)
					Convert4444LinesToOutput(decoder, width, 1, y, srclineA,
						outputline, pitch, decoder->frame.format, whitebitdepth, flags);
				else
					ConvertLinesToOutput(decoder, width, 1, y, srclineA,
						outputline, pitch, decoder->frame.format, whitebitdepth, flags);
			}
		}
	}
}


void SharpenLine(DECODER *decoder, uint8_t *buffer, int bufferremain, uint8_t *output, int pitch, uint8_t *local_output, int local_pitch, int channel_offset, int y, int thread_index)
{
    uint16_t *sbase;//*sbase2 = NULL;
	int width = decoder->frame.width;
	int height = decoder->frame.height;
	int skip = 3;
    //int flip1=0;//flip2=0;
	int channel_flip = decoder->cfhddata.channel_flip;
	//int local_pitch1 = local_pitch;
	//int local_pitch2 = local_pitch;
	uint8_t *outputline = output+y*pitch;
	//uint8_t *outputline2 = NULL;
	short *scratch;
	//int formatdone = 0;
	//float xmin = decoder->cfhddata.channel[0].FrameMask.topLftX;
	//float xmax = decoder->cfhddata.channel[0].FrameMask.topRgtX;
	//float ymin = decoder->cfhddata.channel[0].FrameMask.topLftY;
	//float ymax = decoder->cfhddata.channel[0].FrameMask.botLftY;
	int alphachannel = 0;
	float blursharpen = 0;
	int line_max = decoder->frame.height;
	int yy = y;

	if(decoder->channel_current == 0)
		blursharpen = decoder->cfhddata.channel[1].user_blur_sharpen;  // TODO LEFT and RIGHT separate vertical sharpen
	else
		blursharpen = decoder->cfhddata.channel[2].user_blur_sharpen;  // TODO LEFT and RIGHT separate vertical sharpen



	if(!(decoder->cfhddata.process_path_flags & PROCESSING_COLORMATRIX)|| 
		decoder->frame.resolution == DECODED_RESOLUTION_QUARTER || 
		decoder->frame.resolution == DECODED_RESOLUTION_LOWPASS_ONLY ||
		decoder->frame.resolution == DECODED_RESOLUTION_QUARTER_NODEBAYER_SCALED)
	{
		blursharpen = 0.0;
	}

	if(decoder->channel_mix_half_res == 1)
		line_max *= 2;

	if(!(decoder->cfhddata.process_path_flags & PROCESSING_IMAGEFLIPS))
	{
		channel_flip = 0;
	}

	if(decoder->sharpen_flip) //SharpenLine
	{
		//if(!(decoder->channel_blend_type == BLEND_NONE && decoder->channel_current == 1)) // right channel only (stored in baseptr1)
		{
			yy = (line_max - 1 - y);
			outputline = output+yy*pitch;
		}
	}



	if( decoder->StereoBufferFormat == DECODED_FORMAT_RG64 || 
		decoder->StereoBufferFormat == DECODED_FORMAT_W13A ||
		decoder->StereoBufferFormat == DECODED_FORMAT_RGB32)
	   alphachannel = 1;

	if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
	{
		width *= 2;
	}

	sbase = (uint16_t *)local_output;
	sbase += (local_pitch>>1) * y;


	switch(decoder->StereoBufferFormat)
	{
	case DECODED_FORMAT_RG64:
	case DECODED_FORMAT_W13A:
		skip = 8;
		break;
	case DECODED_FORMAT_WP13:
		skip = 6;
		break;
	case DECODED_FORMAT_RG48:
		skip = 6;
		break;
	case DECODED_FORMAT_RGB32:
		skip = 4;
		break;
	case DECODED_FORMAT_RGB24:
		skip = 3;
		break;
	case DECODED_FORMAT_YUYV:
		skip = 2;
		break;
	}


	scratch = (short*)(buffer + width * skip * thread_index);

	{
		int flags = ACTIVEMETADATA_PRESATURATED;
		int whitebitdepth = 16;
		if((decoder->StereoBufferFormat == DECODED_FORMAT_WP13 || decoder->StereoBufferFormat == DECODED_FORMAT_W13A))
		{
			int use_pitch = local_pitch;
			int edgeclose = 0;
			flags = 0;
			whitebitdepth = 13;

			if(blursharpen != 0.0 && local_pitch != 0)
			{
				short *Aptr,*Bptr,*Cptr,*Dptr,*Eptr;

				switch(decoder->channel_blend_type)
				{
				case BLEND_STACKED_ANAMORPHIC:
					sbase = (uint16_t *)local_output;
					sbase += (local_pitch>>1) * y * 2;
					if(y<=4) edgeclose = 1;  
					if(y>=2) Aptr = (short *)sbase - (local_pitch>>1) * 4; else Aptr = (short *)sbase;
					if(y>=1) Bptr = (short *)sbase - (local_pitch>>1) * 2; else Bptr = (short *)sbase;
					Cptr = (short *)sbase;
					if(y<height-1) Dptr = (short *)sbase + (local_pitch>>1) * 2; else Dptr = (short *)sbase;
					if(y<height-2) Eptr = (short *)sbase + (local_pitch>>1) * 4; else Eptr = (short *)sbase;
					if(y>=height-4) edgeclose = 1;  
					use_pitch = local_pitch * 2;
					break;

				case BLEND_LINE_INTERLEAVED:					
					sbase = (uint16_t *)local_output;
					if(y & 1)
					{
						y--;
						sbase += (local_pitch>>1) * y;
					}
					else
					{
						sbase += (local_pitch>>1) * y;
						sbase += channel_offset>>1;
					}
				
					if(y<=8) edgeclose = 1;  
					if(y>=4) Aptr = (short *)sbase - (local_pitch>>1) * 4; else Aptr = (short *)sbase;
					if(y>=2) Bptr = (short *)sbase - (local_pitch>>1) * 2; else Bptr = (short *)sbase;
					Cptr = (short *)sbase;
					if(y<height-2) Dptr = (short *)sbase + (local_pitch>>1) * 2; else Dptr = (short *)sbase;
					if(y<height-4) Eptr = (short *)sbase + (local_pitch>>1) * 4; else Eptr = (short *)sbase;
					if(y>=height-8) edgeclose = 1;  
					use_pitch = local_pitch * 2;
					break;

				default:
					if(y<=4) edgeclose = 1;  
					if(y>=2) Aptr = (short *)sbase - (local_pitch>>1) * 2; else Aptr = (short *)sbase;
					if(y>=1) Bptr = (short *)sbase - (local_pitch>>1) * 1; else Bptr = (short *)sbase;
					Cptr = (short *)sbase;
					if(y<height-1) Dptr = (short *)sbase + (local_pitch>>1) * 1; else Dptr = (short *)sbase;
					if(y<height-2) Eptr = (short *)sbase + (local_pitch>>1) * 2; else Eptr = (short *)sbase;
					if(y>=height-4) edgeclose = 1;  
					use_pitch = local_pitch;
					break;
				}

				if(skip == 8)
				{
					FastSharpeningBlurVW13A(Aptr, Bptr, Cptr, Dptr, Eptr, use_pitch, edgeclose, 
						scratch, width, blursharpen, 
						decoder->frame.resolution,
						decoder->channel_blend_type);
				}
				else
				{
					FastSharpeningBlurVWP13(Aptr, Bptr, Cptr, Dptr, Eptr, use_pitch, edgeclose,
						scratch, width, blursharpen, 
						decoder->frame.resolution,
						decoder->channel_blend_type);
				}

				sbase = (uint16_t *)scratch;
			}
		}

		if(alphachannel)
			Convert4444LinesToOutput(decoder, width, 1, y, sbase,
				outputline, pitch, decoder->frame.format, whitebitdepth, flags);
		else
			ConvertLinesToOutput(decoder, width, 1, y, sbase,
				outputline, pitch, decoder->frame.format, whitebitdepth, flags);
	}
}


#if _GRAPHICS

void PaintFrame(DECODER *decoder, uint8_t *output, int pitch, int output_format)
{
	int x,y,v,width, height;
	int maxR=0,maxG=0,maxB=0;

	width = decoder->frame.width;
	height = decoder->frame.height;

	if(decoder->cfhddata.BurninFlags == 0)
		return;

	if(decoder->cfhddata.BurninFlags & 2 && decoder->cfhddata.ComputeFlags & ~1) // tools
	{
		if(decoder->tools == NULL)
		{
		#if _ALLOCATOR
			decoder->tools = (ToolsHandle *)Alloc(decoder->allocator, sizeof(ToolsHandle));
		#else
			decoder->tools = (ToolsHandle *)MEMORY_ALLOC(sizeof(ToolsHandle));
		#endif

			if(decoder->tools)
			{
				memset(decoder->tools, 0, sizeof(ToolsHandle));
			}
			else
			{
				return;
			}
		}
	}


	decoder->frame.output_format = output_format;
	
#if _THREADED && 1
	if(decoder->cfhddata.BurninFlags & 2 && decoder->cfhddata.ComputeFlags & ~1 && decoder->tools) // histogram/scopes/waveform
	{
		WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;
		int workunits;

	#if _DELAY_THREAD_START
		if(decoder->tools->histogram == 0 && decoder->worker_thread.pool.thread_count == 0)
		{
			CreateLock(&decoder->worker_thread.lock);
			// Initialize the pool of transform worker threads
			ThreadPoolCreate(&decoder->worker_thread.pool,
							decoder->thread_cntrl.capabilities >> 16/*cpus*/,
							WorkerThreadProc,
							decoder);
		}
	#endif
		{
			int avgR=0,avgG=0,avgB=0;
			// Post a message to the mailbox
			mailbox->output = output;

			if(height >= 1080)
			{
				mailbox->pitch = pitch*4; // only read every 4th scan line
				workunits = height/4; // only read every 4th scan line
			}
			else if(height >= 540)
			{
				mailbox->pitch = pitch*2; // only read every 2th scan line
				workunits = height/2; // only read every 2th scan line
			}
			else 
			{
				mailbox->pitch = pitch; // read every scan line
				workunits = height; // read every scan line
			}

			if(decoder->tools->histogram == 0)
			{
				mailbox->jobType = JOB_TYPE_HISTOGRAM; // histogram

				// Set the work count to the number of rows to process
				ThreadPoolSetWorkCount(&decoder->worker_thread.pool, workunits);

				// Start the transform worker threads
				ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

				// Wait for all of the worker threads to finish
				ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
			}


			for(x=0;x<256;x++)
			{
				avgR += decoder->tools->histR[x];
				avgG += decoder->tools->histG[x];
				avgB += decoder->tools->histB[x];
				//if(maxR < decoder->histR[x]) maxR = decoder->histR[x];
				//if(maxG < decoder->histG[x]) maxG = decoder->histG[x];
				//if(maxB < decoder->histB[x]) maxB = decoder->histB[x];
			}
			avgR /= 256;
			avgG /= 256;
			avgB /= 256;
			//maxR++;
			//maxG++;
			//maxB++;

			decoder->tools->maxR = avgR*3;//maxR;
			decoder->tools->maxG = avgG*3;//maxG;
			decoder->tools->maxB = avgB*3;//maxB;
		}
	}
#endif

	if(decoder->cfhddata.BurninFlags && DrawOpen(decoder))
	{

		if(decoder->cfhddata.BurninFlags & 3) // overlays / tools
		{
#if _THREADED
			//DrawInit(decoder);
			//DrawStartThreaded(decoder);

			if(decoder->draw_thread.pool.thread_count > 0)
			{			
				DrawWaitThreaded(decoder);
			}
			else
#endif
			{
				DrawInit(decoder);
				DrawMetadataObjects(decoder);	
			}
		}
		else
		{
			DrawInit(decoder);
		}
		if(decoder->drawSafeMarkers)
			DrawSafeMarkers(decoder);
		
		if(decoder->cfhddata.BurninFlags & 2) // tools
		{
			if(decoder->tools)
			{
				if(decoder->tools->histogram && decoder->cfhddata.ComputeFlags & 16)
					DrawGrid(decoder, 0/*decoder->MDPcurrent.parallax*/);
                
				if(decoder->tools->histogram && decoder->cfhddata.ComputeFlags & 2)
					DrawHistogram(decoder, 0/*decoder->MDPcurrent.parallax*/);

				if(decoder->tools->histogram && decoder->cfhddata.ComputeFlags & 4)
					DrawWaveform(decoder, 0/*decoder->MDPcurrent.parallax*/);
				
				if(decoder->tools->histogram && decoder->cfhddata.ComputeFlags & 8)
					DrawVectorscope(decoder, 0/*decoder->MDPcurrent.parallax*/);
				
			}
		}
		DrawScreen(decoder, output, pitch, output_format);
	}

#if 0	
	#if _THREADED && 1
	if(decoder->cfhddata.BurninFlags & 2 && decoder->cfhddata.ComputeFlags & 2 && decoder->tools) // histogram
	{		
		WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;
		int workunits;
		int targetW, targetH;

		if(width < 256 || height < 256)
			return;

		targetW = width / 4;
		targetH = height / 8;


		mailbox->output = output;
		mailbox->pitch = pitch;
		workunits = targetW;

		mailbox->jobType = JOB_TYPE_BURNINS; // burnin

		// Set the work count to the number of rows to process
		ThreadPoolSetWorkCount(&decoder->worker_thread.pool, workunits);

		// Start the transform worker threads
		ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

		// Wait for all of the worker threads to finish
		ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
	}
	#else

		if(decoder->histogram == 0)
		{
			for(y=0; y<height; y+=4)
			{
				uint8_t *bptr = output;
				bptr +=  pitch * y;

				HistogramLine(decoder, (unsigned short *)bptr, width, output_format);

				if(decoder->histogram == 0)
					return; // don't know how to create Histogram for that format
			}
		}


		for(x=1;x<255;x++)
		{
			if(maxR < decoder->histR[x]) maxR = decoder->histR[x];
			if(maxG < decoder->histG[x]) maxG = decoder->histG[x];
			if(maxB < decoder->histB[x]) maxB = decoder->histB[x];

		}
		maxR++;
		maxG++;
		maxB++;

		decoder->maxR = maxR;
		decoder->maxG = maxG;
		decoder->maxB = maxB;


		for(x=0; x<targetW; x++)
		{
			HistogramRender(decoder, output, pitch, output_format, x, targetW, targetH);
		}
	#endif
#endif

	if(decoder->tools)
		memset(decoder->tools, 0, sizeof(ToolsHandle));
}

#endif


extern int geomesh_alloc_cache(void *gm);

#define DEG2RAD(d)    (PI*(d)/180.0f)
#define RAD2DEG(r)    (180.0f*(r)/PI)

bool approx_equal(int x, int y)
{  
	if(y > 1080)
	{
		x >>= 6;
		y >>= 6;
	}
	else if(y > 540)
	{
		x >>= 5;
		y >>= 5;
	} else	
	{
		x >>= 4;
		y >>= 4;
	}

	if(x == y || x+1 == y || x == y+1)
		return true;
	
	return false;
}


bool approx_equal_float(float x, float y)
{
	if (x*0.99 < y && y < x*1.01)
		return true;

	return false;
}

#if WARPSTUFF
void WarpFrame(DECODER *decoder, uint8_t *output, int pitch, int output_format)
{
	int width, height;
	//int maxR = 0, maxG = 0, maxB = 0;
	int status = WARPLIB_SUCCESS;
	CFHDDATA *cfhddata = &decoder->cfhddata;
	int backgroundfill = cfhddata->lensFill;
	float sensorcrop = 1.0;
	float phi, theta, rho;
	int srcLens = HERO4;

	if (!cfhddata->doMesh) return;

	if (decoder->lastLensOffsetX != cfhddata->LensOffsetX ||
		decoder->lastLensOffsetY != cfhddata->LensOffsetY ||
		decoder->lastLensOffsetZ != cfhddata->LensOffsetZ ||
		decoder->lastLensOffsetR != cfhddata->LensOffsetR ||
		decoder->lastLensZoom != cfhddata->LensZoom ||
		decoder->lastLensFishFOV != cfhddata->LensFishFOV ||
		decoder->lastLensGoPro != cfhddata->lensGoPro ||
		decoder->lastLensSphere != cfhddata->lensSphere ||
		decoder->lastLensFill != cfhddata->lensFill ||
		decoder->lastLensStyleSel != cfhddata->lensStyleSel ||
		memcmp(decoder->lastLensCustomSRC, cfhddata->lensCustomSRC, sizeof(cfhddata->lensCustomSRC)) ||
		memcmp(decoder->lastLensCustomDST, cfhddata->lensCustomDST, sizeof(cfhddata->lensCustomDST)) )
	{
		if (decoder->mesh)
			geomesh_destroy(decoder->mesh);


		width = decoder->frame.width;
		height = decoder->frame.height;

		if (approx_equal(width, height * 2)) // approx. 2:1   
		{
			float outputaspect = 16.0f/9.0f;
			srcLens = EQUIRECT;
			sensorcrop = 1.00623f; // Fixes the slight calculation error difference between 16x9 with a 4x3, and 16x9 within a 2x1 image.

			if (cfhddata->lensCustomSRC[1])
			{
				outputaspect = cfhddata->lensCustomSRC[0] / cfhddata->lensCustomSRC[1];
				if (outputaspect >= 1.0f && outputaspect <= 3.0f)
				{
					//float sourceratio = (float)width / (float)height;

					if (approx_equal_float(outputaspect, 4.0f / 3.0f))
						sensorcrop = sqrtf((float)(width*width + height*height)) / sqrtf((float)((width * 2 / 3)*(width * 2 / 3) + (height*height)));  

					if (approx_equal_float(outputaspect, 16.0f / 9.0f)) // 0.88;
						sensorcrop = 1.00623f; // Fixes the slight calculation error difference between 16x9 with a 4x3, and 16x9 within a 2x1 image.
				}
			}

			if (width >= 2496)
				decoder->mesh = geomesh_create(199, 99);
			else if (width >= 1272)
				decoder->mesh = geomesh_create(99, 49);
			else
				decoder->mesh = geomesh_create(49, 25);

			phi = cfhddata->LensOffsetX * DEG2RAD(720.0f); // +-180deg HFOV for 2:1
			theta = cfhddata->LensOffsetY * DEG2RAD(720.0f); // +-180deg VFOV for 2:1
			rho = (cfhddata->LensOffsetZ - 1.0f)*4.0f* DEG2RAD(360.0f); // +-360deg 
		}
		else if (approx_equal(width * 3, height * 4)) // approx. 4:3  
		{
			srcLens = HERO4;
			sensorcrop = 1.0;

			if (width > 2880) // UHD
				decoder->mesh = geomesh_create(159, 119);
			else if (width >= 1920) //HD/2.7K
				decoder->mesh = geomesh_create(79, 59);
			else
				decoder->mesh = geomesh_create(39, 29);
			phi = cfhddata->LensOffsetX * DEG2RAD(120.0f); // +-60deg HFOV for 16:9
			theta = cfhddata->LensOffsetY * DEG2RAD(98.0f); // +-49deg VFOV for 16:9
			rho = (cfhddata->LensOffsetZ - 1.0f)*4.0f* DEG2RAD(360.0f); // +-360deg 
		}
		else //if(approx_equal(width*9,height*16)) // approx. 16:9  
		{
			srcLens = HERO4;
			sensorcrop = sqrtf(1920 * 1920 + 1080 * 1080) / sqrtf(2000 * 2000 + 1500 * 1500); // 3840x2160 from 4000x3000
			if (width > 2880) // UHD
				decoder->mesh = geomesh_create(159, 119);
			else if (width >= 1920) //HD/2.7K
				decoder->mesh = geomesh_create(79, 59);
			else
				decoder->mesh = geomesh_create(39, 29);
			phi = cfhddata->LensOffsetX * DEG2RAD(120.0f); // +-60.1deg HFOV for 16:9
			theta = cfhddata->LensOffsetY * DEG2RAD(70.0f); // +-34.75deg VFOV for 16:9
			rho = (cfhddata->LensOffsetZ - 1.0f)*4.0f* DEG2RAD(360.0f); // +-360deg 
		}

		if ((output_format & 0x7fffffff) == COLOR_FORMAT_YUYV)
			status |= geomesh_init(decoder->mesh, width, height, pitch, WARPLIB_FORMAT_YUY2, width, height, pitch, WARPLIB_FORMAT_YUY2, backgroundfill);
		else if ((output_format & 0x7fffffff) == COLOR_FORMAT_RGB32)
			status |= geomesh_init(decoder->mesh, width, height, pitch, WARPLIB_FORMAT_32BGRA, width, height, pitch, WARPLIB_FORMAT_32BGRA, backgroundfill);
		else if ((output_format & 0x7fffffff) == COLOR_FORMAT_W13A)
			status |= geomesh_init(decoder->mesh, width, height, pitch, WARPLIB_FORMAT_W13A, width, height, pitch, WARPLIB_FORMAT_W13A, backgroundfill);
		else if ((output_format & 0x7fffffff) == COLOR_FORMAT_WP13)
			status |= geomesh_init(decoder->mesh, width, height, pitch, WARPLIB_FORMAT_WP13, width, height, pitch, WARPLIB_FORMAT_WP13, backgroundfill);
		else if ((output_format & 0x7fffffff) == COLOR_FORMAT_RG48)
			status |= geomesh_init(decoder->mesh, width, height, pitch, WARPLIB_FORMAT_RG48, width, height, pitch, WARPLIB_FORMAT_RG48, backgroundfill);
		else if ((output_format & 0x7fffffff) == COLOR_FORMAT_BGRA64)
			status |= geomesh_init(decoder->mesh, width, height, pitch, WARPLIB_FORMAT_64ARGB, width, height, pitch, WARPLIB_FORMAT_64ARGB, backgroundfill);
		else
			assert(0);

		if (cfhddata->lensSphere == 1)
		{
			if (cfhddata->lensGoPro != 2) // not outputting EQUIRECT
			{
				if (cfhddata->LensOffsetR != 0.0)
				{
					//float angle = 360.0 * asinf(cfhddata->LensOffsetR * 1.7777777777) / (2.0 * 3.14159);
					float angle = 360.0f * cfhddata->LensOffsetR * cfhddata->LensOffsetR * 2.1f;//asinf(cfhddata->LensOffsetR * 1.7777777777) / (2.0 * 3.14159);
					if (cfhddata->LensOffsetR < 0.0) angle = -angle;
					geomesh_transform_rotate(decoder->mesh, angle);
				}
				if (cfhddata->LensZoom != 1.0)
					geomesh_transform_scale(decoder->mesh, cfhddata->LensZoom, cfhddata->LensZoom);

				if (cfhddata->LensFishFOV != 0.0) // DeFish
				{
					float fov = cfhddata->LensFishFOV;// *180.0;
					if (fov > 89.9f) fov = 89.9f;
					if (fov < -89.9f) fov = -89.9f;

					if (fov)
						status |= geomesh_transform_defish(decoder->mesh, fov);
				}
			}

			switch (cfhddata->lensGoPro)
			{
			case 0: geomesh_transform_repoint_src_to_dst(decoder->mesh, sensorcrop, phi, theta, rho, srcLens, RECTILINEAR); break;
			case 1: geomesh_transform_repoint_src_to_dst(decoder->mesh, sensorcrop, phi, theta, rho, srcLens, HERO4); break;
			case 2: geomesh_transform_repoint_src_to_dst(decoder->mesh, sensorcrop, phi, theta, rho, srcLens, EQUIRECT); break;
			case 4:
				geomesh_set_custom_lens(decoder->mesh, cfhddata->lensCustomSRC, cfhddata->lensCustomDST, sizeof(cfhddata->lensCustomDST));
				if (srcLens == EQUIRECT) geomesh_transform_repoint_src_to_dst(decoder->mesh, sensorcrop, phi, theta, rho, EQUIRECT, CUSTOM_LENS);
				else geomesh_transform_repoint_src_to_dst(decoder->mesh, sensorcrop, phi, theta, rho, CUSTOM_LENS, CUSTOM_LENS);
				break;
			}

		}
		else // old boring geometry
		{
			if (cfhddata->LensZoom != 1.0)
				geomesh_transform_scale(decoder->mesh, cfhddata->LensZoom, cfhddata->LensZoom);

			// basic orthographic moves			
			if (cfhddata->LensOffsetX != 0.0 || cfhddata->LensOffsetY != 0.0)
				geomesh_transform_pan(decoder->mesh, cfhddata->LensOffsetX*(float)width, -cfhddata->LensOffsetY*(float)height);

			if (cfhddata->LensOffsetR != 0.0)
			{
				float angle = 360.0f * asinf(cfhddata->LensOffsetR * 1.7777777777f) / (2.0f * 3.14159f);
				geomesh_transform_rotate(decoder->mesh, angle);
			}

			if (cfhddata->lensGoPro == 0) //Rectilear
				status |= geomesh_transform_gopro_to_rectilinear(decoder->mesh, sensorcrop);
			//status |= geomesh_fisheye_gopro_adjustmesh(mesh, &correction_mode, WARPLIB_ALGORITHM_PRESERVE_EVERYTHING,//WARPLIB_ALGORITHM_BEST_FIT, 
			//	width, height, product, model, lens_type, fov, (int)decoder->frame.resolution);
		}

		geomesh_alloc_cache(decoder->mesh); // required for JOB_TYPE_WARP_CACHE

		if (status == WARPLIB_SUCCESS)
		{
			if (decoder->lens_correct_buffer == NULL)
			{
#if _ALLOCATOR
				decoder->lens_correct_buffer = (int *)Alloc(decoder->allocator, pitch * height);
#else
				decoder->lens_correct_buffer = (int *)MEMORY_ALLOC(pitch * height);
#endif

			}
		}
		else
		{
			return;
		}
		/* need resources?
			{
			if(decoder->tools == NULL)
			{
			#if _ALLOCATOR
			decoder->tools = (ToolsHandle *)Alloc(decoder->allocator, sizeof(ToolsHandle));
			#else
			decoder->tools = (ToolsHandle *)MEMORY_ALLOC(sizeof(ToolsHandle));
			#endif

			if(decoder->tools)
			{
			memset(decoder->tools, 0, sizeof(ToolsHandle));
			}
			else
			{
			return;
			}
			}
			}
			*/


#if _THREADED && 1
		{
			WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;
			int workunits = decoder->frame.height;

#if _DELAY_THREAD_START
			if (decoder->worker_thread.pool.thread_count == 0)
			{
				CreateLock(&decoder->worker_thread.lock);
				// Initialize the pool of transform worker threads
				ThreadPoolCreate(&decoder->worker_thread.pool,
					decoder->thread_cntrl.capabilities >> 16,
					WorkerThreadProc,
					decoder);
			}
#endif
			{
				// Post a message to the mailbox
				mailbox->data = decoder->mesh;
				mailbox->output = output;
				mailbox->local_output = (uint8_t *)decoder->lens_correct_buffer;
				mailbox->line_max = decoder->frame.height;
				mailbox->chunk_size = 16;
				workunits = (mailbox->line_max + mailbox->chunk_size - 1) / mailbox->chunk_size;
				mailbox->jobType = JOB_TYPE_WARP_CACHE;

				// Set the work count to the number of rows to process
				ThreadPoolSetWorkCount(&decoder->worker_thread.pool, workunits);
				// Start the transform worker threads
				ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);
				// Wait for all of the worker threads to finish
				ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
			}
		}
#endif
		//decoder->frame.output_format = output_format;

		decoder->lastLensOffsetX = cfhddata->LensOffsetX;
		decoder->lastLensOffsetY = cfhddata->LensOffsetY;
		decoder->lastLensOffsetZ = cfhddata->LensOffsetZ;
		decoder->lastLensOffsetR = cfhddata->LensOffsetR;
		decoder->lastLensZoom = cfhddata->LensZoom;
		decoder->lastLensFishFOV = cfhddata->LensFishFOV;
		decoder->lastLensGoPro = cfhddata->lensGoPro;
		decoder->lastLensSphere = cfhddata->lensSphere;
		decoder->lastLensFill = cfhddata->lensFill;
		decoder->lastLensStyleSel = cfhddata->lensStyleSel;
		memcpy(decoder->lastLensCustomSRC, cfhddata->lensCustomSRC, sizeof(cfhddata->lensCustomSRC));
		memcpy(decoder->lastLensCustomDST, cfhddata->lensCustomDST, sizeof(cfhddata->lensCustomDST));

	}

#if _THREADED && 1
	{
		WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;
		int workunits = decoder->frame.height;

		mailbox->data = decoder->mesh;
		mailbox->output = output;
		mailbox->local_output = (uint8_t *)decoder->lens_correct_buffer;
		mailbox->line_max = decoder->frame.height;
		mailbox->chunk_size = 16;
		workunits = (mailbox->line_max + mailbox->chunk_size-1)/mailbox->chunk_size;
		mailbox->jobType = JOB_TYPE_WARP;

		// Set the work count to the number of rows to process
		ThreadPoolSetWorkCount(&decoder->worker_thread.pool, workunits);
		// Start the transform worker threads
		ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);
		// Wait for all of the worker threads to finish
		ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

		if(backgroundfill) // may need to blur the filled in areas
		{
			mailbox->data = decoder->mesh;
			mailbox->output = (uint8_t *)decoder->lens_correct_buffer;
			mailbox->local_output = (uint8_t *)decoder->lens_correct_buffer;
			mailbox->line_max = decoder->frame.width;
			mailbox->chunk_size = 16;
			mailbox->pitch = pitch;
			workunits = (mailbox->line_max + mailbox->chunk_size-1)/mailbox->chunk_size;
			mailbox->jobType = JOB_TYPE_WARP_BLURV;

			// Set the work count to the number of rows to process
			ThreadPoolSetWorkCount(&decoder->worker_thread.pool, workunits);
			// Start the transform worker threads
			ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);
			// Wait for all of the worker threads to finish
			ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
		}
	}
#else // not threading
	{
		//geomesh_cache_init_bilinear(decoder->mesh);  //bad
		geomesh_cache_init_bilinear_range(decoder->mesh, 0, decoder->frame.height); //good
		geomesh_apply_bilinear(decoder->mesh, (unsigned char *)output, (unsigned char *)decoder->lens_correct_buffer, 0, decoder->frame.height);
	}
#endif

	memcpy(output, decoder->lens_correct_buffer, pitch * decoder->frame.height);

	/*
	if(lens_correct_buffer)
#if _ALLOCATOR
		Free(decoder->allocator, lens_correct_buffer);
#else
		MEMORY_ALIGNED_FREE(lens_correct_buffer);
#endif

	geomesh_destroy(mesh);
	*/
	
}

void MaskFrame(DECODER *decoder, uint8_t *output, int pitch, int output_format)
{
	int x, y, width, height;
	int minY, maxY;
	int minX, maxX;
	CFHDDATA *cfhddata = &decoder->cfhddata;
	uint8_t *line = output;
	uint32_t fillA = 0;
	uint32_t fillB = 0;
	int bitsize = 8;

	if (!cfhddata->doMesh) return;

	width = decoder->frame.width;
	height = decoder->frame.height;

	if (decoder->cfhddata.LensYmin == 0.0 && decoder->cfhddata.LensXmin == 0.0 && decoder->cfhddata.LensYmax == 0.0 && decoder->cfhddata.LensXmax == 0.0) return;
	if (decoder->cfhddata.LensYmin == 0.0 && decoder->cfhddata.LensXmin == 0.0 && decoder->cfhddata.LensYmax == 1.0 && decoder->cfhddata.LensXmax == 1.0) return;

	minY = (int)(decoder->cfhddata.LensYmin*(float)height);
	maxY = (int)(decoder->cfhddata.LensYmax*(float)height);
	minX = 0xfffc & (int)(decoder->cfhddata.LensXmin*(float)pitch);
	maxX = 0xfffc & (int)(decoder->cfhddata.LensXmax*(float)pitch);


	if (FORMATRGB(output_format))
	{

		line = output;
		// Top rows
		for (y = 0; y < minY; y++)
		{
			memset(line, 0, abs(pitch));
			line += pitch;
		}

		// Left and Right edges of middle rows
		if (maxX - minX != pitch)
		{
			for (; y < maxY; y++)
			{
				memset(line, 0, minX);
				memset(line + maxX, 0, pitch - maxX);
				line += pitch;
			}
		}

		//Bottom wows
		y = maxY;
		line = output + y*pitch;
		for (; y < height; y++)
		{
			memset(line, 0, abs(pitch));
			line += pitch;
		}
	}
	else
	{
		switch (output_format & 0x7fffffff)
		{
		case COLOR_FORMAT_YVYU:
		case COLOR_FORMAT_YUYV:
			fillA = 0x10;
			fillB = 0x80;
			break;
		case COLOR_FORMAT_UYVY:
		case COLOR_FORMAT_2VUY:
			fillA = 0x80;
			fillB = 0x10;
			break;
		case COLOR_FORMAT_YU64:
			fillA = 0x8000;
			fillB = 0x1000;
			bitsize = 16;
			break;
		}
	}

	if (bitsize == 8)
	{
		line = output;
		// Top rows
		for (y = 0; y < minY; y++)
		{
			for (x = 0; x < pitch; x += 2)
			{
				line[x] = fillA;
				line[x + 1] = fillB;
			}
			line += pitch;
		}

		// Left and Right edges of middle rows
		if (maxX - minX != pitch)
		{
			for (; y < maxY; y++)
			{
				for (x = 0; x < minX; x += 2)
				{
					line[x] = fillA;
					line[x + 1] = fillB;
				}
				for (x = maxX; x < pitch; x += 2)
				{
					line[x] = fillA;
					line[x + 1] = fillB;
				}
				line += pitch;
			}
		}

		//Bottom wows
		y = maxY;
		line = output + y*pitch;
		for (; y < height; y++)
		{
			for (x = 0; x < pitch; x += 2)
			{
				line[x] = fillA;
				line[x + 1] = fillB;
			}
			line += pitch;
		}
	}
}
#endif //#if WARPSTUFF

void ConvertLocalToOutput(DECODER *decoder, uint8_t *output, int pitch, int output_format, uint8_t *local_output, int local_pitch, int channel_offset)
{
	uint8_t *local_output_double = local_output;

	//Frame_Region emptyFrameMask = {0};
	if(decoder->StereoBuffer)
		local_output_double = local_output = (uint8_t *)decoder->StereoBuffer;


	if(channel_offset < 0) // channel swapped
	{
		channel_offset = -channel_offset;
	}

	if(INVERTEDFORMAT(decoder->frame.format) != INVERTEDFORMAT(output_format))
	{
		local_output += local_pitch*(decoder->frame.height-1);
		if(decoder->channel_blend_type == BLEND_STACKED_ANAMORPHIC)
			local_output_double += local_pitch*(decoder->frame.height*decoder->channel_decodes-1);
		else
			local_output_double = local_output;
		local_pitch = -local_pitch;
	}
	if(FLIPCOLORS(output_format) || output_format & 0x80000000)
	{
		decoder->cfhddata.InvertOffset = 1;
	}
	else
	{
		decoder->cfhddata.InvertOffset = 0;
	}
	decoder->frame.format = output_format;

	//decoder->frame.colorspace = COLOR_SPACE_CG_601;
#if _THREADED
	{
		WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;
		int workunits;

	#if _DELAY_THREAD_START
		if(decoder->worker_thread.pool.thread_count == 0)
		{
			CreateLock(&decoder->worker_thread.lock);
			// Initialize the pool of transform worker threads
			ThreadPoolCreate(&decoder->worker_thread.pool,
							decoder->thread_cntrl.capabilities >> 16/*cpus*/,
							WorkerThreadProc,
							decoder);
		}
	#endif


		if(	((decoder->cfhddata.process_path_flags & PROCESSING_ORIENTATION) &&
				(decoder->cfhddata.channel[0].FrameAutoZoom * decoder->cfhddata.channel[1].FrameDiffZoom != 1.0 ||
				decoder->cfhddata.channel[1].FrameKeyStone ||
				decoder->cfhddata.channel[1].VerticalOffset ||
				decoder->cfhddata.channel[1].RotationOffset ||
				decoder->cfhddata.channel[1].FrameTilt ||
				decoder->cfhddata.channel[0].FrameAutoZoom / decoder->cfhddata.channel[2].FrameDiffZoom != 1.0 ||
				decoder->cfhddata.channel[2].FrameKeyStone ||
				decoder->cfhddata.channel[2].VerticalOffset ||
				decoder->cfhddata.channel[2].RotationOffset ||
				decoder->cfhddata.channel[2].FrameTilt))
				||
			((decoder->cfhddata.process_path_flags & PROCESSING_FRAMING) &&
				(decoder->cfhddata.FrameOffsetY ||
				decoder->cfhddata.FrameOffsetR ||
			//	decoder->cfhddata.FrameOffsetX || ||
				decoder->cfhddata.FrameHScale != 1.0 ||
				decoder->cfhddata.FrameHDynamic != 1.0 ||
				decoder->cfhddata.channel[1].FrameZoom != 1.0 ||
				decoder->cfhddata.channel[2].FrameZoom != 1.0) ))
		{
			//int x;
			int xbytes, xstep;
			//uint8_t *base = local_output;
			int width, height, chunk_size;
			int fine_vertical = 0;
			width = decoder->frame.width;
			height = decoder->frame.height;

			
			switch(decoder->StereoBufferFormat)
			{
			case DECODED_FORMAT_RGB32:
				xbytes = width*4;
				xstep = 16;
				break;
			case DECODED_FORMAT_RGB24:
				xbytes = width*3;
				xstep = 16;
				break;
			case DECODED_FORMAT_YUYV:
				xbytes = width*2;
				xstep = 16;
				break;
			case DECODED_FORMAT_W13A:
			case DECODED_FORMAT_RG64:
				xbytes = width*8;
				xstep = 32;
				break;
			case DECODED_FORMAT_WP13:
			case DECODED_FORMAT_RG48:
				xbytes = width*6;
				xstep = 32;
				break;
			default:
				assert(0);
				break;
			}

			if(!(decoder->cfhddata.process_path_flags & (PROCESSING_ORIENTATION|PROCESSING_FRAMING)) ||
				(decoder->cfhddata.channel[1].RotationOffset == 0.0 && decoder->cfhddata.channel[1].FrameKeyStone == 0.0 &&
				decoder->cfhddata.channel[2].RotationOffset == 0.0 && decoder->cfhddata.channel[2].FrameKeyStone == 0.0 &&
				decoder->cfhddata.FrameOffsetR == 0.0))
			{
				chunk_size = 8;
			}
			else
			{
				chunk_size = 1;
								
				if((fabs(decoder->cfhddata.channel[1].RotationOffset) + 
					fabs(decoder->cfhddata.channel[1].FrameKeyStone*0.2) + 
					fabs(decoder->cfhddata.FrameOffsetR)) > 0.015  ||
						
				   (fabs(decoder->cfhddata.channel[2].RotationOffset) + 
					fabs(decoder->cfhddata.channel[2].FrameKeyStone*0.2) +
					fabs(decoder->cfhddata.FrameOffsetR)) > 0.015)
				{
					switch(decoder->StereoBufferFormat)
					{
					case DECODED_FORMAT_RGB32:
						xstep = 4;
						break;
					case DECODED_FORMAT_RGB24:
						xstep = 3;
						break;
					case DECODED_FORMAT_YUYV:
						xstep = 4;
						break;
					case DECODED_FORMAT_W13A:
					case DECODED_FORMAT_RG64:
						xstep = 8;
						break;
					case DECODED_FORMAT_WP13:
					case DECODED_FORMAT_RG48:
					default:
						xstep = 6;
						break;
					}

					fine_vertical = 1;
				}
			}

			if( decoder->codec.encoded_format == ENCODED_FORMAT_YUV_422 && 
				(decoder->frame.resolution == DECODED_RESOLUTION_FULL ||
				decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL) &&
				decoder->codec.progressive == false)
			{
				int interlaced_pitch = local_pitch * 2;
				uint8_t *field2_output = local_output + local_pitch;

				// Post a message to the mailbox
				mailbox->local_output = local_output;
				mailbox->local_pitch = interlaced_pitch;
				mailbox->channel_offset = channel_offset;
				memcpy(&mailbox->info, &decoder->frame, sizeof(FRAME_INFO));
				mailbox->info.height >>= 1;
				mailbox->line_max = (xbytes + xstep-1)/xstep;
				mailbox->chunk_size = chunk_size;
				mailbox->fine_vertical = fine_vertical;
				mailbox->jobType = JOB_TYPE_VERTICAL_3D; // 3d work -- vertical

				workunits = (mailbox->line_max + mailbox->chunk_size - 1) / mailbox->chunk_size;

				// Set the work count to the number of rows to process
				ThreadPoolSetWorkCount(&decoder->worker_thread.pool, workunits);

				// Start the transform worker threads
				ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

				// Wait for all of the worker threads to finish
				ThreadPoolWaitAllDone(&decoder->worker_thread.pool);


				// Post a message to the mailbox
				mailbox->local_output = field2_output;
				mailbox->local_pitch = interlaced_pitch;
				mailbox->channel_offset = channel_offset;
				memcpy(&mailbox->info, &decoder->frame, sizeof(FRAME_INFO));
				mailbox->info.height >>= 1;
				mailbox->chunk_size = chunk_size;
				mailbox->line_max = (xbytes + xstep-1)/xstep;
				mailbox->fine_vertical = fine_vertical;
				mailbox->jobType = JOB_TYPE_VERTICAL_3D; // 3d work -- vertical

				workunits = (mailbox->line_max + mailbox->chunk_size - 1) / mailbox->chunk_size;

				// Set the work count to the number of rows to process
				ThreadPoolSetWorkCount(&decoder->worker_thread.pool, workunits);

				// Start the transform worker threads
				ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

				// Wait for all of the worker threads to finish
				ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
			}
			else
			{
				//TODO Lens corect here.
				//call JOB_TYPE_VERTICAL_3D then  (or lens correction equivalent.)
				//     JOB_TYPE_HORIZONTAL_3D
				//before doing any offset and rotation corrections.




				if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER) //HACK //DAN20110129
					width /= 2;

				// Post a message to the mailbox
				mailbox->local_output = local_output;
				mailbox->local_pitch = local_pitch;
				mailbox->channel_offset = channel_offset;
				memcpy(&mailbox->info, &decoder->frame, sizeof(FRAME_INFO));
				mailbox->chunk_size = chunk_size;
				mailbox->line_max = (xbytes + xstep-1)/xstep;
				mailbox->fine_vertical = fine_vertical;
				mailbox->jobType = JOB_TYPE_VERTICAL_3D; // 3d work -- vertical

				workunits = (mailbox->line_max + mailbox->chunk_size - 1) / mailbox->chunk_size;

				// Set the work count to the number of rows to process
				ThreadPoolSetWorkCount(&decoder->worker_thread.pool, workunits);

				// Start the transform worker threads
				ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

				// Wait for all of the worker threads to finish
				ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

			}
		}


		// Post a message to the mailbox
		mailbox->output = output;
		mailbox->pitch = pitch;

		mailbox->local_output = local_output;
		mailbox->local_pitch = local_pitch;
		mailbox->channel_offset = channel_offset;
		memcpy(&mailbox->info, &decoder->frame, sizeof(FRAME_INFO));

		mailbox->chunk_size = 16;
		mailbox->line_max = decoder->frame.height;
		if(decoder->channel_mix_half_res == 1)
			mailbox->line_max *= 2;
		workunits = (mailbox->line_max + mailbox->chunk_size - 1) / mailbox->chunk_size;

		decoder->doVerticalFilter = 0;
		mailbox->jobType = JOB_TYPE_HORIZONAL_3D; // 3d work && horizontal and vertical flips

		// Set the work count to the number of rows to process
		ThreadPoolSetWorkCount(&decoder->worker_thread.pool, workunits);

		// Start the transform worker threads
		ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

		// Wait for all of the worker threads to finish
		ThreadPoolWaitAllDone(&decoder->worker_thread.pool);



		if(decoder->doVerticalFilter)
		{
			// Post a message to the mailbox
			mailbox->output = output;
			mailbox->pitch = pitch;

			mailbox->local_output = local_output_double;
			mailbox->local_pitch = local_pitch;
			mailbox->channel_offset = channel_offset;
			memcpy(&mailbox->info, &decoder->frame, sizeof(FRAME_INFO));

			mailbox->chunk_size = 16;
			mailbox->line_max = decoder->frame.height;

			
			if(decoder->channel_decodes == 2 && decoder->channel_blend_type == 0)
				mailbox->line_max *= 2;

			if(decoder->channel_mix_half_res == 1)
				mailbox->line_max *= 2;
			workunits = (mailbox->line_max + mailbox->chunk_size - 1) / mailbox->chunk_size;

			mailbox->jobType = JOB_TYPE_SHARPEN; // 3d work && horizontal and vertical flips

			// Set the work count to the number of rows to process
			ThreadPoolSetWorkCount(&decoder->worker_thread.pool, workunits);

			// Start the transform worker threads
			ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

			// Wait for all of the worker threads to finish
			ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
		}
	}
#else
	{
		int y,width, height;
		uint8_t scratch[4096*16];
		int scratchremain = 4096*16;
		int ymin = 0, ymax;



		width = decoder->frame.width;
		height = decoder->frame.height;

		ymax = height;

		if((decoder->cfhddata.process_path_flags & PROCESSING_FRAMING) &&
			memcmp(&decoder->cfhddata.channel[0].FrameMask, &emptyFrameMask, 32))
		{
			ymin = (float)height * decoder->cfhddata.channel[0].FrameMask.topLftY;
			ymax = (float)height * decoder->cfhddata.channel[0].FrameMask.botLftY;
		}

		if(	((decoder->cfhddata.process_path_flags & PROCESSING_ORIENTATION) &&
				(decoder->cfhddata.channel[0].FrameAutoZoom * decoder->cfhddata.channel[1].FrameDiffZoom != 1.0 ||
				decoder->cfhddata.channel[1].FrameKeyStone ||
				decoder->cfhddata.channel[1].VerticalOffset ||
				decoder->cfhddata.channel[1].RotationOffset ||
				decoder->cfhddata.channel[0].FrameAutoZoom / decoder->cfhddata.channel[2].FrameDiffZoom != 1.0 ||
				decoder->cfhddata.channel[2].FrameKeyStone ||
				decoder->cfhddata.channel[2].VerticalOffset ||
				decoder->cfhddata.channel[2].RotationOffset))
				||
			((decoder->cfhddata.process_path_flags & PROCESSING_FRAMING) &&
				(decoder->cfhddata.FrameOffsetY ||
				decoder->cfhddata.FrameOffsetR ||
				decoder->cfhddata.FrameOffsetX ||
				 decoder->cfhddata.FrameHScale != 1.0 ||
				 decoder->cfhddata.FrameHDynamic != 1.0 ||
				decoder->cfhddata.channel[1].FrameZoom != 1.0 ||
				decoder->cfhddata.channel[2].FrameZoom != 1.0))
		{
			int x,xbytes, xstep;
			uint8_t *base = local_output;
			float voffsetstep;
			float voffset = decoder->cfhddata.channel[1].VerticalOffset;
			float roffset = decoder->cfhddata.channel[1].RotationOffset;
			float voffset1, voffset2;
			float voffsetstep1, voffsetstep2;
			int channel_flip = decoder->cfhddata.channel_flip;
			int aspectx,aspecty;
			float aspectfix;

			GetDisplayAspectRatio(decoder, &aspectx, &aspecty); 
			aspectfix = (float)(aspectx*aspectx) / (float)(aspecty*aspecty);

			if(!(decoder->cfhddata.process_path_flags & PROCESSING_ORIENTATION))
			{
				voffset = roffset = 0;
			}

			if(!(decoder->cfhddata.process_path_flags & PROCESSING_IMAGEFLIPS))
			{
				channel_flip = 0;
			}

			if(decoder->cfhddata.process_path_flags & PROCESSING_FRAMING)
				voffset += decoder->cfhddata.FrameOffsetY;

			if(decoder->cfhddata.InvertOffset)
			{
				voffset = -voffset;
				roffset = -roffset;
			}

			switch(decoder->StereoBufferFormat)
			{
			case DECODED_FORMAT_RGB32:
				xbytes = width*4;
				xstep = 16;
				break;
			case DECODED_FORMAT_RGB24:
				xbytes = width*3;
				xstep = 16;
				break;
			case DECODED_FORMAT_YUYV:
				xbytes = width*2;
				xstep = 16;
				break;
			case DECODED_FORMAT_WP13:
			case DECODED_FORMAT_RG48:
			default:
				xbytes = width*6;
				xstep = 32;
				break;
			}

			//DAN20100923 -- simplied
			//voffset += roffset * (float)(width*width) / (float)(height*height) * 0.5;
			//voffsetstep = -roffset * (float)(width*width) / (float)(height*height)  / (float)(xbytes/xstep);
			voffset += roffset * aspectfix * 0.5;
			voffsetstep = -roffset * aspectfix / (float)(xbytes/xstep);

			if(roffset == 0.0)
				xstep = xbytes;

			voffset1 = voffset2 = voffset;
			voffsetstep1 = voffsetstep2 = voffsetstep;

			if(channel_flip & 0xf)
			{
				if(channel_flip & 2)
				{
					voffset1 = -voffset1;
					voffsetstep1 = -voffsetstep1;
				}
				if(channel_flip & 8)
				{
					voffset2 = -voffset2;
					voffsetstep2 = -voffsetstep2;
				}
				if(channel_flip & 1)
				{
					voffset1 += voffsetstep1*(xbytes/xstep);
					voffsetstep1 = -voffsetstep1;
				}
				if(channel_flip & 4)
				{
					voffset2 += voffsetstep2*(xbytes/xstep);
					voffsetstep2 = -voffsetstep2;
				}
			}


			for(x=0; x<xbytes; x+=xstep)
			{
				if(decoder->channel_decodes == 1 && decoder->channel_current == 1) // Right only
				{
					RGB48VerticalShift(decoder, base, (unsigned short *)scratch,
								xstep, height, local_pitch, -voffset2);
				}
				else
				{
					RGB48VerticalShift(decoder, base, (unsigned short *)scratch,
								xstep, height, local_pitch, voffset1);
				}
				if(decoder->channel_decodes == 2)
				{
					uint8_t *bptr = base + channel_offset;
					RGB48VerticalShift(decoder, bptr, (unsigned short *)scratch,
								xstep, height, local_pitch, -voffset2);
				}

				base += xstep;
				voffset1 += voffsetstep1;
				voffset2 += voffsetstep2;
			}
		}

		if(decoder->channel_mix_half_res == 1)
			height *= 2;

		if(ymin)
		{
			memset(local_output, 0, abs(local_pitch)); // zero one line;
		}
		for(y=0; y<ymin; y++)
		{
			ProcessLine3D(decoder, scratch, scratchremain, output, pitch, local_output, 0, channel_offset, y, 0);
		}
		for(; y<ymax; y++)
		{
			ProcessLine3D(decoder, scratch, scratchremain, output, pitch, local_output, local_pitch, channel_offset, y, 0);
		}
		for(; y<height; y++)
		{
			ProcessLine3D(decoder, scratch, scratchremain, output, pitch, local_output, 0, channel_offset, y, 0);
		}
	}
#endif
}



// Decode a sample from the input bitstream into the output frame buffer
bool DecodeSample(DECODER *decoder, BITSTREAM *input, uint8_t *output, int pitch, ColorParam *colorparams, CFHDDATA *cfhddata)
{
	//CODEC_ERROR error = CODEC_ERROR_OKAY;
#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	//CODEC_STATE *codec = &decoder->codec;
	//int subband_wavelet_index[] = {5, 5, 5, 5, 4, 4, 4, 3, 3, 3, 1, 1, 1, 0, 0, 0};
	int channel_decodes = 1; // 3D Work
	int channel_offset = 0;
	int channel_mask = 0;
	int channel_current = 0;
	//int wavelet_index;
	bool result = true;
	uint8_t *local_output = output;
	uint8_t *local_buffer = NULL;
	int local_pitch = pitch;
	int internal_format = decoder->frame.format;
	int output_format = decoder->frame.output_format;
	bool use_local_buffer = false;
	DECODER *local_decoder = decoder;
	//Frame_Region emptyFrameMask = {0};
	Frame_Region emptyFrameMask = FRAME_REGION_INITIALIZER;
	int orig_width = decoder->frame.width;
	int orig_height = decoder->frame.height;

	decoder->local_output = local_output; // used for NV12 decodes.

	decoder->sample_uncompressed = 0; // set if a uncompressed sample is found.
	decoder->image_dev_only = 0;

	if(decoder->flags & (1<<3)) // This is an image development only decode.
	{
		decoder->sample_uncompressed = 1;
		decoder->image_dev_only = 1;
		decoder->codec.encoded_format = ENCODED_FORMAT_RGB_444;
		decoder->codec.unique_framenumber = 0; //What should this be?  
		decoder->frame.white_point = 16; // how to we pass this in?
		
		decoder->uncompressed_chunk = (uint32_t *)input->lpCurrentBuffer;

		switch(output_format & 0x7fffffff)
		{
			case COLOR_FORMAT_RGB24:
				decoder->uncompressed_size = orig_width * orig_height * 3;
				break;
			case COLOR_FORMAT_RGB32:
				decoder->uncompressed_size = orig_width * orig_height * 4;
				break;
			case COLOR_FORMAT_RG48:
			case COLOR_FORMAT_WP13:
				decoder->uncompressed_size = orig_width * orig_height * 6;
				break;
			default:
				decoder->uncompressed_size = orig_width * orig_height * 6;
				assert(0);
				break;
		}
	}

	decoder->frame.alpha_Companded = 0; // reset this state.
	if(decoder->parallelDecoder)
		decoder->parallelDecoder->sample_uncompressed = 0;

	decoder->error = CODEC_ERROR_OKAY;
	input->error = BITSTREAM_ERROR_OKAY;

	// first time through encoded_format is not initized.
	if(input->nWordsUsed > 4096 && decoder->image_dev_only == 0) // an I-frame is needed
	{
		SAMPLE_HEADER header;		
		BITSTREAM input2;
		InitBitstreamBuffer(&input2, input->lpCurrentWord, input->nWordsUsed, BITSTREAM_ACCESS_READ);
		memset(&header, 0, sizeof(SAMPLE_HEADER));
		header.find_lowpass_bands = 2; // help finding the uncompressed flag
		if(ParseSampleHeader(&input2, &header))
		{
			decoder->codec.encoded_format = header.encoded_format;
			decoder->sample_uncompressed = header.hdr_uncompressed;
			if(decoder->parallelDecoder)
				decoder->parallelDecoder->sample_uncompressed = header.hdr_uncompressed;
		}
	}
	
	if((uintptr_t)input->lpCurrentBuffer & 0x3)
	{

		if(decoder->aligned_sample_buffer == NULL)
		{

#if _ALLOCATOR
			ALLOCATOR *allocator = decoder->allocator;
			decoder->aligned_sample_buffer =
				(uint8_t *)AllocAligned(allocator, (size_t)input->dwBlockLength, 16);
#else
			decoder->aligned_sample_buffer =
				(uint8_t *)MEMORY_ALIGNED_ALLOC(input->dwBlockLength, 16);
#endif
            memcpy(decoder->aligned_sample_buffer, input->lpCurrentBuffer, input->dwBlockLength);
			decoder->aligned_sample_buffer_size = input->dwBlockLength;
		}
		else
		{
			if ((size_t)input->dwBlockLength <= decoder->aligned_sample_buffer_size)
			{
				memcpy(decoder->aligned_sample_buffer, input->lpCurrentBuffer, input->dwBlockLength);
			}
			else
			{
#if _ALLOCATOR
				ALLOCATOR *allocator = decoder->allocator;
				FreeAligned(decoder->allocator, decoder->aligned_sample_buffer);
				decoder->aligned_sample_buffer =
					(uint8_t *)AllocAligned(allocator, input->dwBlockLength, 16);
#else
				MEMORY_ALIGNED_FREE(decoder->aligned_sample_buffer);
				decoder->aligned_sample_buffer =
					(uint8_t *)MEMORY_ALIGNED_ALLOC(input->dwBlockLength, 16);
#endif
				memcpy(decoder->aligned_sample_buffer, input->lpCurrentBuffer, input->dwBlockLength);
				decoder->aligned_sample_buffer_size = input->dwBlockLength;
			}
		}

		input->lpCurrentBuffer = decoder->aligned_sample_buffer;
		input->lpCurrentWord = decoder->aligned_sample_buffer;
	}

#if 0 // Test for missaligning the image data
	if(((int)input->lpCurrentBuffer&3) == 0)
	{
		int i;
		uint8_t *ptr = (uint8_t *)input->lpCurrentBuffer;
		int missaligned = 1; //2 or 3

		for(i=input->dwBlockLength-1; i>=0; i--)
			ptr[i+missaligned] = ptr[missaligned];

		input->lpCurrentBuffer = (uint8_t  *)&ptr[missaligned];
		input->lpCurrentWord = (uint8_t  *)&ptr[missaligned];
	}
#endif

//HACK
	// Unfortunately I need color matrix data deep within the codec for RT playback.
	if(cfhddata && cfhddata->MagicNumber == CFHDDATA_MAGIC_NUMBER) // valid input
	{
		if(decoder->cfhddata.MagicNumber != CFHDDATA_MAGIC_NUMBER)
		{
			//int size = cfhddata->size;
			size_t size = cfhddata->size;
			memset(&decoder->cfhddata, 0, sizeof(CFHDDATA));
			if (size > sizeof(CFHDDATA)) {
				// Limit the size to the known structure
				size = sizeof(CFHDDATA);
			}
			memcpy(&decoder->cfhddata, cfhddata, size);
		}
	}
	else
	{
		unsigned short value;

		if(decoder->cfhddata.MagicNumber != CFHDDATA_MAGIC_NUMBER || decoder->cfhddata.size != sizeof(CFHDDATA))
		{
			memset(&decoder->cfhddata, 0, sizeof(CFHDDATA));
			decoder->cfhddata.MagicNumber = CFHDDATA_MAGIC_NUMBER;
			decoder->cfhddata.size = sizeof(CFHDDATA);

			if(decoder->image_dev_only) // For baseband image only corrections, initize the decoder with defaults
			{
				decoder->cfhddata.cfhd_subtype = 2;		//RGB
				decoder->cfhddata.num_channels = 3;
			}
			else if(GetTuplet(input->lpCurrentBuffer, input->nWordsUsed, CODEC_TAG_INPUT_FORMAT, &value))
			{
				if(value == COLOR_FORMAT_RG48)
				{
					decoder->cfhddata.cfhd_subtype = 2;		//RGB
					decoder->cfhddata.num_channels = 3;
				}
				else if(value == COLOR_FORMAT_RG64)
				{
					decoder->cfhddata.cfhd_subtype = 3;		//RGBA
					decoder->cfhddata.num_channels = 4;
				}
				else if(value > COLOR_FORMAT_BAYER && value < COLOR_FORMAT_BAYER_END)
				{
					unsigned int format = BAYER_FORMAT_RED_GRN;

					decoder->cfhddata.cfhd_subtype = 1;		//BAYER
					decoder->cfhddata.bayer_format = format; // default to Red-Grn
					decoder->cfhddata.version = CFHDDATA_VERSION;
				}
			}
		}
	}


	OverrideCFHDDATA(decoder, input->lpCurrentBuffer, input->nWordsUsed);
	if(decoder->image_dev_only) // HACK we need to support 3D also. 
		decoder->source_channels = 1;
	else
		decoder->source_channels = decoder->real_channels = SkipVideoChannel(decoder, input, 0);

	if(!decoder->basic_only && (decoder->cfhddata.MSChannel_type_value || decoder->cfhddata.MSCTV_Override))
	{
		//int channels = 0;
		int channel_blend_type = BLEND_NONE;
		int channel_swapped_flags = 0;

		if(decoder->cfhddata.MSCTV_Override)
		{
			channel_mask = decoder->cfhddata.MSCTV_Override&0xff;
			channel_blend_type = ((decoder->cfhddata.MSCTV_Override>>8) & 0xff);
			channel_swapped_flags = ((decoder->cfhddata.MSCTV_Override>>16) & 0xffff);
		}
		else
		{
			channel_mask = decoder->cfhddata.MSChannel_type_value&0xff;
			channel_blend_type = ((decoder->cfhddata.MSChannel_type_value>>8) & 0xff);
			channel_swapped_flags = ((decoder->cfhddata.MSChannel_type_value>>16) & 0xffff);
		}

		if(channel_mask != 3)
		{
			channel_blend_type = BLEND_NONE;
			channel_swapped_flags = 0;
		}

		//if(channels >= 2) // even "mono" files need to be displayed as Stereo if a 3D mode is selected //DAN20090302
		{

			if(channel_mask == 1 && decoder->source_channels >= 2) // Decode Left only
			{
				if(decoder->cfhddata.FramingFlags & 2) // channel swap
				{
					SkipVideoChannel(decoder, input, 2); // 3D work
				}
			}
			else if(channel_mask == 2 && decoder->source_channels >= 2) // Decode Right only
			{
				if(decoder->cfhddata.FramingFlags & 2) // channel swap
				{
					SkipVideoChannel(decoder, input, 1); // 3D work
				}
				else
				{
					//assume second channel decode
					SkipVideoChannel(decoder, input, 2); // 3D work
				}
				channel_current = 1;
				channel_decodes = 1;
				channel_blend_type = BLEND_NONE;
				channel_swapped_flags = 0;
			}
			else if(channel_mask == 2 && decoder->source_channels <= 1) // Decode 2D as Right channel
			{
				channel_current = 1;
				channel_decodes = 1;
				channel_blend_type = BLEND_NONE;
				channel_swapped_flags = 0;
			}
			else if((channel_mask&3) == 3) // A+B 3d work
			{
				channel_decodes = 2;
				decoder->channel_mix_half_res = 0;

				if(channel_blend_type != BLEND_NONE)
				{

					if(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444 && ALPHAOUTPUT(decoder->frame.format))
					{
						//if(decoder->frame.format == DECODED_FORMAT_W13A)
					//	{
					//		decoder->frame.format = internal_format = DECODED_FORMAT_W13A;
					//	}
						//else
						//{
					//		decoder->frame.format = internal_format = DECODED_FORMAT_RG64;
					//	}

						decoder->frame.format = internal_format = DECODED_FORMAT_RGB32;
						local_pitch = decoder->frame.width * 4;
					}
					else
					{
						decoder->frame.format = internal_format = DECODED_FORMAT_RGB24;
						local_pitch = decoder->frame.width * 3; //RGB24
					}

				/*	if(decoder->frame.resolution == DECODED_RESOLUTION_FULL &&
						(output_format == DECODED_FORMAT_YUYV ||
						output_format == DECODED_FORMAT_UYVY))
					{
						if(  channel_blend_type == BLEND_FREEVIEW || 
							((channel_blend_type == BLEND_STACKED_ANAMORPHIC ||
							 channel_blend_type == BLEND_SIDEBYSIDE_ANAMORPHIC ||
							 channel_blend_type == BLEND_LINE_INTERLEAVED) && decoder->frame.width > 1280))
						{
							decoder->frame.resolution = DECODED_RESOLUTION_HALF;
							decoder->channel_mix_half_res = 1;
							decoder->frame.width /= 2;
							decoder->frame.height /= 2;

							local_pitch = (decoder->frame.width) * 3; //RGB24
						}
					} */
				}

			/*	if(channel_blend_type == BLEND_STEREO_YUY2inRGBA) //YUY2 in RGBA
				{
					decoder->frame.format = internal_format = DECODED_FORMAT_YUYV;
					local_pitch = decoder->frame.width * 2; //YUY2

					channel_offset = local_pitch * (decoder->frame.height);
					use_local_buffer = true;
				}*/

/* DAN20120316 	FLAG3D_HALFRES broken			if(decoder->frame.resolution == DECODED_RESOLUTION_FULL && channel_swapped_flags & FLAG3D_HALFRES && output_format != DECODED_FORMAT_W13A)
				{
					decoder->frame.resolution = DECODED_RESOLUTION_HALF;
					decoder->channel_mix_half_res = 1;
					decoder->frame.width /= 2;
					decoder->frame.height /= 2;
					local_pitch /= 2;
				} */

				if( decoder->frame.resolution == DECODED_RESOLUTION_FULL && 
					(channel_blend_type == BLEND_SIDEBYSIDE_ANAMORPHIC || channel_blend_type == BLEND_FREEVIEW))
				{
					if(decoder->codec.encoded_format != ENCODED_FORMAT_BAYER)
					{	
						if(decoder->sample_uncompressed)
						{
							decoder->frame.resolution = DECODED_RESOLUTION_HALF;
							decoder->channel_mix_half_res = 1;
							decoder->frame.width /= 2;
							decoder->frame.height /= 2;
							local_pitch /= 2;
						}
						else
						{
							if(decoder->preformatted_3D_type > BLEND_NONE)
							{
								// leave as is.
							}
							else if(FORMAT8BIT(output_format))
							{
								decoder->frame.resolution = DECODED_RESOLUTION_HALF_HORIZONTAL;
								decoder->frame.width /= 2;
								local_pitch /= 2;
							}
						}
					}
					else
					{
						if(FORMAT8BIT(output_format))
							decoder->frame.resolution = DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER;
					}

					//TODO int uncompressed = decoder->uncompressed_chunk && decoder->uncompressed_size && decoder->sample_uncompressed;
				
				}


				if(channel_blend_type >= BLEND_STACKED_ANAMORPHIC && channel_blend_type < BLEND_ANAGLYPH_RC)// stacked, side-by-side, fields, Onion, YUY2
				{
					channel_offset = local_pitch * (decoder->frame.height);

				}
				else if(channel_blend_type >= BLEND_ANAGLYPH_RC)
				{
			/*		if(channel_blend_type & 1 && channel_blend_type <= 21) // B&W Anaglyph
					{
						//B&W using YUYV
						decoder->frame.format = internal_format = DECODED_FORMAT_YUYV;
						local_pitch = decoder->frame.width * 2; //YUY2
					}*/

					channel_offset = local_pitch * (decoder->frame.height);
					use_local_buffer = true;
				}
				else if(channel_blend_type == BLEND_NONE) // double high
				{
					channel_offset = pitch * decoder->frame.height;
				}
				else
				{
					channel_blend_type = BLEND_STACKED_ANAMORPHIC;
					channel_offset = pitch * (decoder->frame.height/2);
				}

				// fields, stacked, etc, only works on full or half res.
				if (channel_blend_type > BLEND_NONE && channel_blend_type <= BLEND_LINE_INTERLEAVED &&
					decoder->frame.resolution == DECODED_RESOLUTION_LOWPASS_ONLY) //thumnbail.
				{
					channel_decodes = 1;
					channel_blend_type = BLEND_NONE;
					channel_swapped_flags = 0;
				}
				if (channel_blend_type != BLEND_NONE &&
				   (output_format == DECODED_FORMAT_BYR1 ||
					output_format == DECODED_FORMAT_BYR2 ||
					output_format == DECODED_FORMAT_BYR3 ||
					output_format == DECODED_FORMAT_BYR4 ))
				{
					channel_decodes = 1;
					channel_blend_type = BLEND_NONE;
					channel_swapped_flags = 0;
				}
			}
		}
		
		decoder->channel_decodes = channel_decodes;
		decoder->channel_blend_type = channel_blend_type;
		decoder->channel_swapped_flags = channel_swapped_flags;
	}
	else
	{		
		decoder->channel_decodes = channel_decodes = 1;
		decoder->channel_blend_type = BLEND_NONE;
		decoder->channel_swapped_flags = 0;
	}

	if(cfhddata)  // So the P-frames can know the bayerformat
	{
		//int size = cfhddata->size;
		size_t size = cfhddata->size;

		if (size > sizeof(CFHDDATA)) {
			size = sizeof(CFHDDATA);
		}
		memcpy(cfhddata, &decoder->cfhddata, size);
	}

	{
		bool doOrientation = true;
		bool doFraming = true;
		bool doBurins = true;
		bool doImageflips = true;
		bool doGhostBust = false;
		bool doPrimaries = true;
		int process_path_flags = decoder->cfhddata.process_path_flags;
		int process_path_flags_mask = decoder->cfhddata.process_path_flags_mask;


		
		if(decoder->basic_only)
		{
			doOrientation = false;
			doFraming = false;
			doBurins = false;
			doImageflips = false;
			doPrimaries = false;
		}
		else 
		{
			if(decoder->cfhddata.process_path_flags_mask)
			{
				//DAN20101007 -- 
				if(process_path_flags == 0)
					decoder->cfhddata.process_path_flags = process_path_flags = decoder->cfhddata.process_path_flags_mask;

				process_path_flags &= decoder->cfhddata.process_path_flags_mask;
				if(process_path_flags_mask & PROCESSING_ACTIVE2)
				{
					if(!(process_path_flags_mask & PROCESSING_ORIENTATION))
						doOrientation = false;
					if(!(process_path_flags_mask & PROCESSING_FRAMING))
						doFraming = false;
					if(!(process_path_flags_mask & PROCESSING_BURNINS))
						doBurins = false;
					if(!(process_path_flags_mask & PROCESSING_IMAGEFLIPS))
						doImageflips = false;
				}

				if(!(process_path_flags_mask & PROCESSING_COLORMATRIX))
					doPrimaries = false;
			}

			if(process_path_flags & PROCESSING_ACTIVE2)
			{
				if(!(process_path_flags & PROCESSING_ORIENTATION))
					doOrientation = false;
				if(!(process_path_flags & PROCESSING_FRAMING))
					doFraming = false;
				if(!(process_path_flags & PROCESSING_BURNINS))
					doBurins = false;
				if(!(process_path_flags & PROCESSING_IMAGEFLIPS))
					doImageflips = false;
				if(!(process_path_flags	& PROCESSING_COLORMATRIX))
					doPrimaries = false;
			}
		}

		if(doOrientation)
			process_path_flags |= PROCESSING_ORIENTATION;
		if(doFraming)
			process_path_flags |= PROCESSING_FRAMING;
		if(doBurins)
			process_path_flags |= PROCESSING_BURNINS;
		if(doImageflips)
			process_path_flags |= PROCESSING_IMAGEFLIPS;
		if(doPrimaries)
			process_path_flags |= PROCESSING_COLORMATRIX;

		if(decoder->channel_swapped_flags & FLAG3D_GHOSTBUST)
		{
			if(decoder->ghost_bust_left || decoder->ghost_bust_right)
			{
				doGhostBust = true;
			}
		}

		decoder->cfhddata.process_path_flags = process_path_flags;

		if((!decoder->basic_only &&
				(doOrientation && (	decoder->cfhddata.channel[0].FloatingWindowMaskL ||
									decoder->cfhddata.channel[0].FloatingWindowMaskR ||
									decoder->cfhddata.channel[0].FrameKeyStone ||
									decoder->cfhddata.channel[0].FrameTilt ||
									decoder->cfhddata.channel[0].HorizontalOffset ||
									decoder->cfhddata.channel[0].VerticalOffset ||
									decoder->cfhddata.channel[0].RotationOffset ||

									decoder->cfhddata.channel[1].FloatingWindowMaskL ||
									decoder->cfhddata.channel[1].FloatingWindowMaskR ||
									decoder->cfhddata.channel[1].FrameKeyStone ||
									decoder->cfhddata.channel[1].FrameTilt ||
									decoder->cfhddata.channel[1].HorizontalOffset ||
									decoder->cfhddata.channel[1].VerticalOffset ||
									decoder->cfhddata.channel[1].RotationOffset ||
									decoder->cfhddata.channel[0].FrameAutoZoom * decoder->cfhddata.channel[1].FrameDiffZoom != 1.0 ||

									decoder->cfhddata.channel[2].FloatingWindowMaskL ||
									decoder->cfhddata.channel[2].FloatingWindowMaskR ||
									decoder->cfhddata.channel[2].FrameKeyStone ||
									decoder->cfhddata.channel[2].FrameTilt ||
									decoder->cfhddata.channel[2].HorizontalOffset ||
									decoder->cfhddata.channel[2].VerticalOffset ||
									decoder->cfhddata.channel[2].RotationOffset ||
                                   decoder->cfhddata.channel[0].FrameAutoZoom / decoder->cfhddata.channel[2].FrameDiffZoom != 1.0)))
									||
				(doPrimaries && (	decoder->cfhddata.channel[0].user_blur_sharpen != 0.0 || 
									decoder->cfhddata.channel[1].user_blur_sharpen != 0.0 || 
									decoder->cfhddata.channel[2].user_blur_sharpen != 0.0))
									||
				(doFraming && (		decoder->cfhddata.channel[0].user_vignette_start != 0.0 || 
									decoder->cfhddata.channel[1].user_vignette_start != 0.0 || 
									decoder->cfhddata.channel[2].user_vignette_start != 0.0))
									||
				(doFraming &&	(	memcmp(&decoder->cfhddata.channel[0].FrameMask, &emptyFrameMask, 32) ||
									decoder->cfhddata.FrameOffsetX ||
									decoder->cfhddata.FrameOffsetY ||
									decoder->cfhddata.FrameOffsetR ||
									decoder->cfhddata.FrameHScale != 1.0 ||
									decoder->cfhddata.FrameHDynamic != 1.0 ||
									decoder->cfhddata.channel[1].FrameZoom != 1.0 ||
									decoder->cfhddata.channel[2].FrameZoom != 1.0))
									||
				(doGhostBust && (decoder->channel_blend_type == BLEND_NONE) && (channel_decodes == 2)) 
									||
				(doImageflips && decoder->cfhddata.channel_flip) 
									||
				(decoder->preformatted_3D_type == BLEND_STACKED_ANAMORPHIC) || 
				(decoder->preformatted_3D_type == BLEND_SIDEBYSIDE_ANAMORPHIC) ||
				(decoder->channel_blend_type && decoder->frame.resolution == DECODED_RESOLUTION_QUARTER) ||  // 3D mode generally don't work in quarter res -- this prevents crashes.
				( ((decoder->frame.width+7)/8)*8 != decoder->frame.width  || (channel_decodes > 1 && decoder->channel_blend_type != BLEND_NONE) ||
				decoder->sample_uncompressed) ||
				(decoder->cfhddata.doMesh)

			)
		{
			if(	output_format == DECODED_FORMAT_BYR1 ||
				output_format == DECODED_FORMAT_BYR2 ||
				output_format == DECODED_FORMAT_BYR3 ||
				output_format == DECODED_FORMAT_BYR4 )
			{
				// no manipulation should be applied
			}
			else
			{
				use_local_buffer = true;
				local_pitch = ((decoder->frame.width+7)/8)*8 * 6; //RGB48

				if(decoder->image_dev_only)
				{
					decoder->frame.white_point = 13;
					decoder->frame.format = internal_format = DECODED_FORMAT_WP13;
				}
				else if(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444 && ALPHAOUTPUT(decoder->frame.format))
				{
					decoder->frame.white_point = 13;
					decoder->frame.format = internal_format = DECODED_FORMAT_W13A;
					local_pitch = ((decoder->frame.width+7)/8)*8 * 8; 
				}
				else
				{
					decoder->frame.white_point = 13;
					decoder->frame.format = internal_format = DECODED_FORMAT_WP13;
				}

				if(	decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL || 
					decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER)
				{
					local_pitch *= 2;  // need horizontal room to make 3D side by side frame
				}

/*
				if(output_format == DECODED_FORMAT_WP13 || output_format == DECODED_FORMAT_W13A) 
				{
					// preserve HDR
					decoder->frame.format = internal_format = output_format;//DECODED_FORMAT_WP13; // HDR output

					if(output_format == DECODED_FORMAT_W13A) 
						local_pitch = decoder->frame.width * 8; 
				}
				else
				{
					if(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444 && ALPHAOUTPUT(decoder->frame.format))
					{
						decoder->frame.format = internal_format = DECODED_FORMAT_RG64;
						local_pitch = decoder->frame.width * 8; 
					}
					else
					{
						decoder->frame.format = internal_format = DECODED_FORMAT_RG48;
					}
				}*/
				
				channel_offset = local_pitch * (decoder->frame.height);
			}
		}
	}

	if(output_format == DECODED_FORMAT_BYR4 && decoder->cfhddata.encode_curve_preset == 0)
	{
		if(decoder->BYR4LinearRestore == NULL)
		{
			int j,val;
			int encode_curve_type = decoder->cfhddata.encode_curve >> 16;
			//int encode_curve_neg = encode_curve_type & CURVE_TYPE_NEGATIVE;
			float encode_curvebase;

			if(encode_curve_type) //1 or 2
			{
				if(encode_curve_type & CURVE_TYPE_EXTENDED)
					encode_curvebase = (float)(decoder->cfhddata.encode_curve & 0xffff); // use all 16-bits for larger log bases
				else
					encode_curvebase = (float)((decoder->cfhddata.encode_curve >> 8) & 0xff) / (float)(decoder->cfhddata.encode_curve & 0xff);
			}
			else
			{
				encode_curve_type = CURVE_TYPE_LOG;
				encode_curvebase = 90.0;
			}

		#if _ALLOCATOR
			decoder->BYR4LinearRestore = (unsigned short *)AllocAligned(decoder->allocator,16384*2, 16);
		#else
			decoder->BYR4LinearRestore = (unsigned short *)MEMORY_ALIGNED_ALLOC(16384*2, 16);
		#endif

			for(j=0; j<16384; j++) //0 to 1
			{				
				switch(encode_curve_type & CURVE_TYPE_MASK)
				{
				case CURVE_TYPE_LOG:
					val = (int)(CURVE_LOG2LIN((float)j/16384.0f,
						(float)encode_curvebase) * 65535.0f);
					break;
				case CURVE_TYPE_GAMMA:
					val = (int)(CURVE_GAM2LIN((float)j/16384.0f,
						(float)encode_curvebase) * 65535.0f);
					break;
				case CURVE_TYPE_CINEON:
					val = (int)(CURVE_CINEON2LIN((float)j/16384.0f,
						(float)encode_curvebase) * 65535.0f);
					break;
				case CURVE_TYPE_CINE985:
					val = (int)(CURVE_CINE9852LIN((float)j/16384.0f,
						(float)encode_curvebase) * 65535.0f);
					break;
				case CURVE_TYPE_PARA:
					val = (int)(CURVE_PARA2LIN((float)j/16384.0f,
						(int)((decoder->cfhddata.encode_curve >> 8) & 0xff), (int)(decoder->cfhddata.encode_curve & 0xff)) * 65535.0f);
					break;
				case CURVE_TYPE_CSTYLE:
					val = (int)(CURVE_CSTYLE2LIN((float)j/16384.0f,
						(int)((decoder->cfhddata.encode_curve >> 8) & 0xff)) * 65535.0f);
					break;
				case CURVE_TYPE_SLOG:
					val = (int)(CURVE_SLOG2LIN((float)j/16384.0f) * 65535.0f);
					break;
				case CURVE_TYPE_LOGC:
					val = (int)(CURVE_LOGC2LIN((float)j/16384.0f) * 65535.0f);
					break;
				case CURVE_TYPE_LINEAR:
				default:
					val = j;
					break;
				}
				if(val < 0) val = 0;
				if(val > 65535) val = 65535;
				decoder->BYR4LinearRestore[j] = val;
			}
		}
	}

	//DAN20120319 - removed 
	/*if(decoder->channel_mix_half_res)	//decoding half but scaling to double the output size
	{
		local_pitch *= 2;
		channel_offset = local_pitch * (decoder->frame.height*2);
	}*/


	if(use_local_buffer == true) // need buffer for anaglyph and other 3D presentation formats
	{
		int stereoframesize = channel_offset * channel_decodes/*stacked frames*/;
		if(decoder->source_channels == 1 && decoder->preformatted_3D_type == BLEND_NONE)
			stereoframesize = channel_offset;

		if(channel_decodes == 1 && decoder->preformatted_3D_type != BLEND_NONE)
			stereoframesize = channel_offset * 2;

		if(channel_decodes == 2 && decoder->source_channels == 1 && decoder->channel_blend_type != BLEND_NONE)
			stereoframesize = channel_offset * 2;
			

		if(decoder->StereoBuffer==NULL || decoder->StereoBufferSize < stereoframesize)
		{

#if _ALLOCATOR
			if(decoder->StereoBuffer)
			{
				FreeAligned(decoder->allocator, decoder->StereoBuffer);
				decoder->StereoBuffer = NULL;
			}
			decoder->StereoBuffer = (PIXEL16U *)AllocAligned(decoder->allocator, stereoframesize+256, 16); //DAN20130517 add 256, as 2.7K half we are write off the buffers end for zoom, don't know why yet. 
#else
			if(decoder->StereoBuffer)
			{
				MEMORY_ALIGNED_FREE(decoder->StereoBuffer);
				decoder->StereoBuffer = NULL;
			}
			decoder->StereoBuffer = (PIXEL16U *)MEMORY_ALIGNED_ALLOC(stereoframesize+256, 16); //DAN20130517 add 256, as 2.7K half we are write off the buffers end for zoom, don't know why yet. 
#endif
			assert(decoder->StereoBuffer != NULL);
			if (! (decoder->StereoBuffer != NULL)) {
				return CODEC_ERROR_MEMORY_ALLOC;
			}
			decoder->StereoBufferSize = stereoframesize;
		}

		decoder->StereoBufferFormat = internal_format;
 		local_buffer = (uint8_t *)decoder->StereoBuffer;
		local_output = local_buffer;
	}

	DecodeEntropyInit(decoder);
	//swapped  -- Maybe useful for double height decodes.
/*	if(channel_decodes == 2 && channel_swapped_flags & FLAG3D_SWAPPED)
	{
		local_output += channel_offset;
		channel_offset = -channel_offset;
	}*/

	decoder->use_local_buffer = use_local_buffer ? 1 : 0;

	if(channel_decodes == 2 && decoder->parallelDecoder == NULL && decoder->source_channels > 1)
	{
		int encoded_width = decoder->frame.width;
		int encoded_height = decoder->frame.height;
		if (decoder->frame.resolution == DECODED_RESOLUTION_HALF)
		{
			// Compute the encoded dimensions from the frame dimensions
			encoded_width *= 2;
			encoded_height *= 2;
		}
		else if (decoder->frame.resolution == DECODED_RESOLUTION_QUARTER)
		{
			// Compute the encoded dimensions from the frame dimensions
			encoded_width *= 4;
			encoded_height *= 4;
		}
		else if (decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
		{
			// Compute the encoded dimensions from the frame dimensions
			encoded_width *= 2;
		}
		else if (decoder->frame.resolution == DECODED_RESOLUTION_HALF_VERTICAL)
		{
			// Compute the encoded dimensions from the frame dimensions
			encoded_height *= 2;
		}


#if _ALLOCATOR
		decoder->parallelDecoder = (DECODER *)Alloc(decoder->allocator, sizeof(DECODER));
		if(decoder->parallelDecoder)
		{
			memset(decoder->parallelDecoder, 0, sizeof(DECODER));
			DecodeInit(decoder->allocator, decoder->parallelDecoder, encoded_width, encoded_height,
							internal_format, DECODED_RESOLUTION_FULL, NULL);
		}
#else
		decoder->parallelDecoder = (DECODER *)MEMORY_ALLOC(sizeof(DECODER));
		if(decoder->parallelDecoder)
		{
			memset(decoder->parallelDecoder, 0, sizeof(DECODER));
			decoder->parallelDecoder->thread_cntrl = decoder->thread_cntrl;
			DecodeInit(decoder->parallelDecoder, encoded_width, encoded_height,
							internal_format, DECODED_RESOLUTION_FULL, NULL);
		}
#endif
	}

	// Using the parallel decoder?
	if (decoder->parallelDecoder)
	{
		// Initialize the parallel decoder with parameters from the regular decoder
		memcpy(&decoder->parallelDecoder->cfhddata, &decoder->cfhddata, sizeof(CFHDDATA));
		memcpy(decoder->parallelDecoder->licensekey,decoder->licensekey, 16);
		
		DecodeEntropyInit(decoder->parallelDecoder);

		DecodeOverrides(decoder->parallelDecoder, decoder->overrideData, decoder->overrideSize);
		decoder->parallelDecoder->channel_decodes = decoder->channel_decodes;
		decoder->parallelDecoder->channel_blend_type = decoder->channel_blend_type;
		decoder->parallelDecoder->flags = decoder->flags;
		decoder->parallelDecoder->frame = decoder->frame;

		decoder->parallelDecoder->use_local_buffer = use_local_buffer ? 1 : 0;
		decoder->parallelDecoder->codec.encoded_format = decoder->codec.encoded_format;

		if(decoder->parallelDecoder->decoder_thread.pool.thread_count == 0)
		{
			CreateLock(&decoder->parallelDecoder->decoder_thread.lock);

			// Initialize the pool of transform worker threads
			ThreadPoolCreate(&decoder->parallelDecoder->decoder_thread.pool,
								1, //
								ParallelThreadProc,
								decoder->parallelDecoder);
		}
	}

	if(channel_decodes == 2 && decoder->real_channels > 1 && decoder->parallelDecoder && decoder->parallelDecoder->decoder_thread.pool.thread_count)
	{
		// Second stream as a thread.
		BITSTREAM second_input = *input;

		if(decoder->cfhddata.FramingFlags & 2 && decoder->source_channels >= 2) // channel swap
		{
			BITSTREAM leftEye_input = *input;
			SkipVideoChannel(decoder, &leftEye_input, 2); // 3D work
			*input = leftEye_input;
			SkipVideoChannel(decoder, &second_input, 1); // 3D work
		}
		else
			SkipVideoChannel(decoder, &second_input, 2); // 3D work

		decoder->channel_current = 0;
		decoder->parallelDecoder->channel_current = 1;

		// Instead of reading the metadata databases again, use the ones in the main decoder
		OverrideCFHDDATAUsingParent(decoder->parallelDecoder, decoder, input->lpCurrentBuffer, input->nWordsUsed);

		// DAN20110404 Use left (first) eye metadata for both eyes (just in case right GUID is bad.)
		// OverrideCFHDDATA(decoder->parallelDecoder, input->lpCurrentBuffer, input->nWordsUsed);
		//OverrideCFHDDATA(decoder->parallelDecoder, second_input.lpCurrentWord, second_input.nWordsUsed);

		// Hack, this gets lost
		decoder->parallelDecoder->cfhddata.split_CC_position = decoder->cfhddata.split_CC_position;
		
#if (_THREADED && _GRAPHICS)
		if(decoder->cfhddata.process_path_flags & PROCESSING_BURNINS && output)
		{
			if(decoder->cfhddata.BurninFlags & 3) // overlays / tools
			{
				DrawStartThreaded(decoder);
			}
		}
#endif
		
		// Post a message to the mailbox
		decoder->parallelDecoder->decoder_thread.input = &second_input;

		if(use_local_buffer == false &&
			(decoder->frame.format == DECODED_FORMAT_RGB32 || decoder->frame.format == DECODED_FORMAT_RGB24))
		{
			decoder->parallelDecoder->decoder_thread.output = local_output;
			local_output += channel_offset;
		}
		else
		{
			decoder->parallelDecoder->decoder_thread.output = local_output + channel_offset;
		}

		decoder->parallelDecoder->decoder_thread.pitch = local_pitch;
		decoder->parallelDecoder->decoder_thread.colorparams = colorparams;

		// Set the work count to the number of rows to process
		ThreadPoolSetWorkCount(&decoder->parallelDecoder->decoder_thread.pool, 1);

		// Start the transform worker threads
		ThreadPoolSendMessage(&decoder->parallelDecoder->decoder_thread.pool, THREAD_MESSAGE_START);

		// do the first channel
		{
			TAGVALUE segment;
			int sample_type;

	#if _THREADED
			decoder->entropy_worker_new.next_queue_num = 0;
			decoder->entropy_worker_new.threads_used = 0;
	#endif

			// Get the type of sample
			segment = GetTagValue(input);
			assert(segment.tuple.tag == CODEC_TAG_SAMPLE);
			if (!IsValidSegment(input, segment, CODEC_TAG_SAMPLE)) {
				decoder->error = CODEC_ERROR_BITSTREAM;
				STOP(tk_decompress);
				return false;
			}
			sample_type = segment.tuple.value;

			switch (sample_type)
			{
			case SAMPLE_TYPE_GROUP:		// Group of frames (decode the first frame)
				result = DecodeSampleGroup(decoder, input, local_output, local_pitch, colorparams);
				break;

			case SAMPLE_TYPE_FRAME:		// Decode the second or later frame in a group
				result = DecodeSampleFrame(decoder, input, local_output, local_pitch, colorparams);
				break;

			case SAMPLE_TYPE_IFRAME:	// Decode a sample that represents an isolated frame
				result = DecodeSampleIntraFrame(decoder, input, local_output, local_pitch, colorparams);
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
		
		// Wait for all of the worker threads to finish
		ThreadPoolWaitAllDone(&decoder->parallelDecoder->decoder_thread.pool);
	}
	else
	{
		while(channel_decodes > 0)
		{
			TAGVALUE segment;
			int sample_type;

			local_decoder->channel_current = channel_current++;

			//OverrideCFHDDATA(local_decoder, input->lpCurrentBuffer, input->nWordsUsed);
			
#if (_THREADED && _GRAPHICS)
			if(decoder->cfhddata.process_path_flags & PROCESSING_BURNINS && output)
			{
				if(decoder->cfhddata.BurninFlags & 3) //overlays / tools
				{
					DrawStartThreaded(decoder);
				}
			}
#endif


	#if _THREADED
			local_decoder->entropy_worker_new.next_queue_num = 0;
			local_decoder->entropy_worker_new.threads_used = 0;
	#endif

			if(decoder->image_dev_only)
			{
				result = DecodeSampleIntraFrame(local_decoder, input, local_output, local_pitch, colorparams);
			}
			else
			{
				// Get the type of sample
				segment = GetTagValue(input);
				assert(segment.tuple.tag == CODEC_TAG_SAMPLE);
				if (!IsValidSegment(input, segment, CODEC_TAG_SAMPLE)) {
					local_decoder->error = CODEC_ERROR_BITSTREAM;
					STOP(tk_decompress);
					return false;
				}
				sample_type = segment.tuple.value;

				switch (sample_type)
				{
				case SAMPLE_TYPE_GROUP:		// Group of frames (decode the first frame)
					result = DecodeSampleGroup(local_decoder, input, local_output, local_pitch, colorparams);
					break;

				case SAMPLE_TYPE_FRAME:		// Decode the second or later frame in a group
					result = DecodeSampleFrame(local_decoder, input, local_output, local_pitch, colorparams);
					break;

				case SAMPLE_TYPE_IFRAME:	// Decode a sample that represents an isolated frame
					result = DecodeSampleIntraFrame(local_decoder, input, local_output, local_pitch, colorparams);
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

			if(ConvertPreformatted3D(decoder, use_local_buffer, internal_format, channel_mask, local_output, local_pitch, &channel_offset))
			{
				channel_decodes = 0;
			}
			else
			{
				channel_decodes--;

				local_output += channel_offset;

				if(decoder->parallelDecoder)
				{
					local_decoder = decoder->parallelDecoder;
				}
			}
		}
	}

	if(use_local_buffer && output)
	{
		decoder->use_local_buffer = 0;

#if WARPSTUFF
		WarpFrame(decoder, local_buffer, local_pitch, decoder->StereoBufferFormat);

		MaskFrame(decoder, local_buffer, local_pitch, decoder->StereoBufferFormat);
#endif

		ConvertLocalToOutput(decoder, output, pitch, output_format, local_buffer, local_pitch, abs(channel_offset));

	}
	else
	{
#if WARPSTUFF
		WarpFrame(decoder, output, pitch, output_format);

		MaskFrame(decoder, output, pitch, output_format);
#endif
	}

	if(decoder->channel_mix_half_res)	//HACK
	{
		decoder->frame.resolution = DECODED_RESOLUTION_FULL;
		decoder->frame.width *= 2;
		decoder->frame.height *= 2;
		decoder->channel_mix_half_res = 0;
	}

	if(	decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL) //HACK
	{
		decoder->frame.resolution = DECODED_RESOLUTION_FULL;
		decoder->frame.width *= 2;
	}
	if(	decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER) //HACK
	{
		decoder->frame.resolution = DECODED_RESOLUTION_FULL;
	}

#if _GRAPHICS
	if(decoder->cfhddata.process_path_flags & PROCESSING_BURNINS && output)
	{
		PaintFrame(decoder, output, pitch, output_format);
	}
#endif


	STOP(tk_decompress);

	// Return indication of whether decoding succeeded or failed
	return result;
}


// Decode a sample that encoded a group of frames (return the first frame)
bool DecodeSampleGroup(DECODER *decoder, BITSTREAM *input, uint8_t *output, int pitch, ColorParam *colorparams)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	CODEC_STATE *codec = &decoder->codec;
	int32_t frame_size = decoder->frame.height * pitch;
	int resolution = decoder->frame.resolution;
	bool result = true;

	static int subband_wavelet_index[] = {5, 5, 5, 5, 4, 4, 4, 3, 3, 3, 3, 1, 1, 1, 0, 0, 0};
	static int subband_band_index[] = {0, 1, 2, 3, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3};

	int num_subbands = sizeof(subband_wavelet_index)/sizeof(subband_wavelet_index[0]);

#if (0 && DEBUG)
	// Force quarter resolution decoding for debug that feature
	resolution = DECODED_RESOLUTION_QUARTER;
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Decoding sample group\n");
	}
#endif

	START(tk_decoding);

	// Initialize the codec state
	InitCodecState(&decoder->codec);

	// Allocate the transform data structure for the group of frames
	AllocDecoderGroup(decoder);

	// Initialize the tables for decoding the wavelet transforms
	InitWaveletDecoding(decoder, subband_wavelet_index, subband_band_index, num_subbands);

	// Clear the flags in the wavelet transforms
	ClearTransformFlags(decoder);

	// Process the tag value pairs until an encoded subband is found
	for (;;)
	{
		TAGVALUE segment;
		
		// Read the next tag value pair from the bitstream
		//segment = GetTagValue(input);
		segment = GetSegment(input);
		assert(input->error == BITSTREAM_ERROR_OKAY);
		if (input->error != BITSTREAM_ERROR_OKAY) {
				decoder->error = CODEC_ERROR_BITSTREAM;
				result = false;
				break;
		}

		// Update the codec state with the information in the tag value pair
		{
			TAGWORD tag = segment.tuple.tag;
			TAGWORD value = segment.tuple.value;

			// Use the tag value pair to update the codec state
			error = UpdateCodecState(decoder, input, codec, tag, value);

			assert(error == CODEC_ERROR_OKAY);

			if (error != CODEC_ERROR_OKAY)
			{
				decoder->error = error;
				result = false;
				break;
				//NOTE: Consider moving the error code into the codec state
			}
		}

		// Check whether the group has been decoded
		if (codec->sample_done) break;

		// Skip the rest of the current channel?
		if (CanSkipChannel(decoder, resolution))
		{
			if(codec->channel == 3 && (decoder->frame.format == DECODED_FORMAT_YUYV || decoder->frame.format == DECODED_FORMAT_UYVY))
			{
				int channel = codec->channel;
				uint32_t channel_size = codec->channel_size[channel];
				uint8_t  *position = codec->channel_position + channel_size;

				// Advance the bitstream to the next channel
				SetBitstreamPosition(input, position);

				// Reset the decoded subband flags (otherwise this code will be executed again)
				codec->decoded_subband_flags = 0;

				codec->num_channels = 3;
				goto decoding_complete;
			}
			else
			if (resolution == DECODED_RESOLUTION_LOWPASS_ONLY)
			{
				int channel = codec->channel;
				uint32_t channel_size = codec->channel_size[channel];
				uint8_t  *position = codec->channel_position + channel_size;


				// Advance the bitstream to the next channel
				SetBitstreamPosition(input, position);

				// Reset the decoded subband flags (otherwise this code will be executed again)
				codec->decoded_subband_flags = 0;
			}
			else
			{
				// Compute the bitstream position after the current channel
				int channel = codec->channel;
				uint32_t channel_size = codec->channel_size[channel];
				uint8_t  *position = codec->channel_position + channel_size;

				// Get the temporal wavelet
				int temporal_index = 2;
				TRANSFORM *transform = decoder->transform[channel];
				IMAGE *wavelet = transform->wavelet[temporal_index];

#if (0 && DEBUG)
				if (IsBandValid(wavelet, HIGHPASS_BAND))
				{
					int static count = 0;
					if (count < 20) {
						char label[_MAX_PATH];
						sprintf(label, "Temporal-decode-%d-", count);
						DumpBandPGM(label, wavelet, HIGHPASS_BAND, NULL);
					}
					count++;
				}
#endif
#if _THREADED_DECODER
				// Ready to invert this wavelet to get the lowpass band in the lower wavelet?
				//if (DecodedBandsValid(wavelet, temporal_index))
				if (resolution != DECODED_RESOLUTION_QUARTER || (decoder->codec.encoded_format == ENCODED_FORMAT_BAYER))
#else
				// Have all bands in the temporal wavelet been decoded?
				//if (wavelet && BANDS_ALL_VALID(wavelet))
				if (AllBandsValid(wavelet))
#endif
				{
					//PIXEL *buffer = (PIXEL *)decoder->buffer;
					//size_t buffer_size = decoder->buffer_size;
					int precision = codec->precision;
#if (0 && DEBUG)
					if (logfile) {
						fprintf(logfile, "Reconstructing the lowpass bands in the first level wavelets\n");
					}
#endif
#if _THREADED_DECODER
					// Add the temporal inverse transform to the processing queue
					if(decoder->entropy_worker_new.pool.thread_count)
					{
						ReconstructWaveletBand(decoder, transform, channel, wavelet, temporal_index,
							precision, &decoder->scratch, 1);
						QueueThreadedTransform(decoder, channel, temporal_index);
					}
					else
#endif
					{
						// Reconstruct the lowpass bands in the first level wavelets
						//ReconstructWaveletBand(transform, channel, wavelet, temporal_index, precision, buffer, buffer_size);
						ReconstructWaveletBand(decoder, transform, channel, wavelet, temporal_index,
							precision, &decoder->scratch, 0 );
					}

					// Advance the bitstream to the next channel
					SetBitstreamPosition(input, position);

					// Reset the decoded subband flags (otherwise this code will be executed again)
					codec->decoded_subband_flags = 0;

					// Note that the subband flags are also reset when the channel header is decoded
				}
				// Was the wavelet created?
				else if (wavelet == NULL)
				{
					// The temporal wavelet is not created during quarter resolution decoding

					// Advance the bitstream to the next channel
					SetBitstreamPosition(input, position);

					// Reset the decoded subband flags (otherwise this code will be executed again)
					codec->decoded_subband_flags = 0;
				}

				//TODO: Improve quarter resolution decoding so that the wavelet is created?
			}
		}
	}

decoding_complete:
	STOP(tk_decoding);

#if (0 && DEBUG)
	if (logfile)
	{
		char label[_MAX_PATH];
		int channel;

		for (channel = 0; channel < codec->num_channels; channel++)
		{
			TRANSFORM *transform = decoder->transform[channel];
			IMAGE *wavelet = transform->wavelet[2];
			uint8_t *data = (uint8_t *)wavelet->band[HIGHPASS_BAND];
			int height = wavelet->height;
			int pitch = wavelet->pitch;
			int size = height * pitch;
			int band;

			for (band = 0; band < wavelet->num_bands; band++)
			{
				sprintf(label, "Temporal channel: %d, band: %d", channel, band);
				DumpBandStatistics(label, wavelet, band, logfile);
#if 0
				sprintf(label, "Temporal-channel%d-band%d-", channel, band);
				DumpBandPGM(label, wavelet, band, NULL);
#endif
			}

			assert(size > 0);
			ZeroMemory(data, size);
		}
	}
#endif

	if (result)
	{
		// Two frames have been decoded
		decoder->gop_length = 2;
		decoder->frame_count += 2;

#if (1 && DEBUG)
		if (logfile) {
			fprintf(logfile,
					"DecodeSampleGroup, decoder: 0x%p, GOP length: %d\n",
					decoder, decoder->gop_length);
		}
#endif

		// Return the first frame in the group
		if (!decoder->no_output)
		{
#if 0
			// Decoding to quarter frame resolution at full frame rate?
			if (resolution == DECODED_RESOLUTION_QUARTER)
			{
				int num_channels = codec->num_channels;
				FRAME_INFO *info = &decoder->frame;
				char *buffer = decoder->buffer;
				size_t buffer_size = decoder->buffer_size;

				uint8_t *frame1 = output;
				uint8_t *frame2 = decoder->output2;
				assert(frame2 != NULL);

				// Reconstruct two frames at quarter resolution
				ReconstructQuarterFrame(decoder, num_channels,
										frame1, frame2, pitch,
										info, buffer, buffer_size);
			}
			else
#endif

			// Finish computing the output frame
			ReconstructSampleFrameToBuffer(decoder, 0, output, pitch);
		}

		if (decoder->error != CODEC_ERROR_OKAY) {
			result = false;
		}

#if TIMING
		// Increment the count of bytes that have been decoded
		decode_byte_count += (COUNTER)BitstreamByteCount(input);
#endif
	}

	if (!result)
	{
		// Check that the frame can be cleared
		assert(frame_size > 0);
		if (frame_size > 0)
		{
			// Zero the frame
			memset(output, 0, frame_size);
		}
	}

	return result;
}

// Decode a sample that represents the second frame in a group
bool DecodeSampleFrame(DECODER *decoder, BITSTREAM *input, uint8_t *output, int pitch, ColorParam *colorparams)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif

	CODEC_STATE *codec = &decoder->codec;
	int32_t frame_size = decoder->frame.height * pitch;
	bool result = true;

	START(tk_decoding);

	// Decode the tag value pairs in the frame sample
	for (;;)
	{
		TAGWORD tag;
		TAGWORD value;

		// Read the next tag value pair from the bitstream
		//TAGVALUE segment = GetTagValue(input);
		TAGVALUE segment = GetSegment(input);
		assert(input->error == BITSTREAM_ERROR_OKAY);
		if (input->error != BITSTREAM_ERROR_OKAY) {
			decoder->error = CODEC_ERROR_BITSTREAM;
			result = false;
			break;
		}

		// Update the codec state with the information in the tag value pair
		tag = segment.tuple.tag;
		value = segment.tuple.value;

		// Use the tag value pair to update the codec state
		error = UpdateCodecState(decoder, input, codec, tag, value);
		assert(error == CODEC_ERROR_OKAY);
		if (error != CODEC_ERROR_OKAY) {
			decoder->error = error;
			result = false;
			break;
		}

		// End of the frame header?
		if (tag == CODEC_TAG_FRAME_INDEX) break;
	}

	STOP(tk_decoding);

#if (1 && DEBUG)
	if (logfile) {
		fprintf(logfile,
				"DecodeSampleFrame, decoder: 0x%p, GOP length: %d\n",
				decoder, decoder->gop_length);
	}
#endif

	if (result)
	{
		// Return the second frame in the group
//		assert(decoder->gop_length >= 2);
		if (decoder->gop_length >= 2)
		{
			int frame_index = 1;	// Display the second frame in the group

			ReconstructSampleFrameToBuffer(decoder, frame_index, output, pitch);
			if (decoder->error != CODEC_ERROR_OKAY) {
				result = false;
			}
		}
		else if (decoder->gop_length > 0)
		{
			int frame_index = 0;	// Display the first frame in the group

			ReconstructSampleFrameToBuffer(decoder, frame_index, output, pitch);
			if (decoder->error != CODEC_ERROR_OKAY) {
				result = false;
			}
		}

#if TIMING
		// Increment the count of bytes that have been decoded
		decode_byte_count += (COUNTER)BitstreamByteCount(input);
#endif
	}

	if (!result)
	{
		// Frame type that is not handled

		// Check that the frame can be cleared
		assert(frame_size > 0);
		if (frame_size > 0)
		{
			// Zero the frame
			memset(output, 0, frame_size);
		}
	}

	return result;
}

// Decode a sample that encodes an intra frame
bool DecodeSampleIntraFrame(DECODER *decoder, BITSTREAM *input, uint8_t *output, int pitch, ColorParam *colorparams)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	CODEC_STATE *codec = &decoder->codec;
	int32_t frame_size = decoder->frame.height * pitch;
	int resolution = decoder->frame.resolution;
	bool result = true;

	static int subband_wavelet_index[] = {2, 2, 2, 2, 1, 1, 1, 0, 0, 0};
	static int subband_band_index[] = {0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3};
	int num_subbands = sizeof(subband_wavelet_index)/sizeof(subband_wavelet_index[0]);

	START(tk_decoding);

	if(decoder->image_dev_only) goto decoding_completeI;

	// Initialize the codec state
	InitCodecState(&decoder->codec);

	// Allocate the transform data structure for the group of frames
	AllocDecoderGroup(decoder);

	// Initialize the tables for decoding the wavelet transforms
	InitWaveletDecoding(decoder, subband_wavelet_index, subband_band_index, num_subbands);

	// Clear the flags in the wavelet transforms
	ClearTransformFlags(decoder);

	//Force V210 output for debugging ***DEBUG***
	//decoder->frame.format = DECODED_FORMAT_V210;

	// Process the tag value pairs until an encoded subband is found
	for (;;)
	{
		TAGVALUE segment;
		
		// Read the next tag value pair from the bitstream
		segment = GetSegment(input);
		assert(input->error == BITSTREAM_ERROR_OKAY);
		if (input->error != BITSTREAM_ERROR_OKAY) {
			decoder->error = CODEC_ERROR_BITSTREAM;
			result = false;
			break;
		}

		{
			TAGWORD tag = segment.tuple.tag;
			TAGWORD value = segment.tuple.value;

			// Use the tag value pair to update the codec state
			error = UpdateCodecState(decoder, input, codec, tag, value);

			assert(error == CODEC_ERROR_OKAY);

			if (error != CODEC_ERROR_OKAY) {
				decoder->error = error;
				result = false;
				break;

				//NOTE: Consider moving the error code into the codec state
			}
		}

		// Check whether the group has been decoded
		if (codec->sample_done) {
			break;
		}

		// Skip the rest of the current channel?
		if (CanSkipChannel(decoder, resolution))
		{
			if(codec->channel == 3 && (decoder->frame.format == DECODED_FORMAT_YUYV || decoder->frame.format == DECODED_FORMAT_UYVY))
			{
				int channel = codec->channel;
				uint32_t channel_size = codec->channel_size[channel];
				uint8_t  *position = codec->channel_position + channel_size;

				// Advance the bitstream to the next channel
				SetBitstreamPosition(input, position);

				// Reset the decoded subband flags (otherwise this code will be executed again)
				codec->decoded_subband_flags = 0;

				codec->num_channels = 3;
				goto decoding_completeI;
			}
			else if (resolution == DECODED_RESOLUTION_LOWPASS_ONLY)
			{
				int channel = codec->channel;
				uint32_t channel_size = codec->channel_size[channel];
				uint8_t  *position = codec->channel_position + channel_size;

				// Advance the bitstream to the next channel
				SetBitstreamPosition(input, position);

				// Reset the decoded subband flags (otherwise this code will be executed again)
				codec->decoded_subband_flags = 0;
			}
			else
			{
				// Compute the bitstream position after the current channel
				int channel = codec->channel;
				uint32_t channel_size = codec->channel_size[channel];
				uint8_t  *position = codec->channel_position + channel_size;

				// Get the highest wavelet in the pyramid
				int wavelet_index = 2;
				TRANSFORM *transform = decoder->transform[channel];
				IMAGE *wavelet = transform->wavelet[wavelet_index];

#if _THREADED_DECODER
				// Ready to invert this wavelet to get the lowpass band in the lower wavelet?
				//if (DecodedBandsValid(wavelet, temporal_index))
				if (resolution != DECODED_RESOLUTION_QUARTER || (decoder->codec.encoded_format == ENCODED_FORMAT_BAYER))
#else
				// Have all bands in the wavelet been decoded?
				if (AllBandsValid(wavelet))
#endif
				{
					//PIXEL *buffer = (PIXEL *)decoder->buffer;
					//size_t buffer_size = decoder->buffer_size;
					int precision = codec->precision;
#if (0 && DEBUG)
					if (logfile) {
						char label[_MAX_PATH];
						int band;

						sprintf(label, "Channel: %d, index: %d", channel, wavelet_index);
						DumpImageStatistics(label, wavelet, logfile);
#if 1
						for (band = 1; band < wavelet->num_bands; band++)
						{
							sprintf(label, "Channel: %d, index: %d, band: %d", channel, wavelet_index, band);
							DumpBandStatistics(label, wavelet, band, logfile);
						}
#endif
					}
#endif
#if (0 & DEBUG)
					if (logfile) {
						fprintf(logfile, "Reconstructing the lowpass bands in the first level wavelets\n");
					}
#endif
#if _THREADED_DECODER
					// Add the inverse spatial transform to the processing queue
					if(decoder->entropy_worker_new.pool.thread_count)
					{
						ReconstructWaveletBand(decoder, transform, channel, wavelet, wavelet_index,
													precision, &decoder->scratch, 1);
						QueueThreadedTransform(decoder, channel, wavelet_index);
					}
					else
#endif
					{
						// Reconstruct the lowpass bands in the first level wavelets
						//ReconstructWaveletBand(transform, channel, wavelet, temporal_index, precision, buffer, buffer_size);
						ReconstructWaveletBand(decoder, transform, channel, wavelet, wavelet_index,
												precision, &decoder->scratch, 0);
					}
					// Advance the bitstream to the next channel
					SetBitstreamPosition(input, position);

					// Reset the decoded subband flags (otherwise this code will be executed again)
					codec->decoded_subband_flags = 0;

					// Note that the subband flags are also reset when the channel header is decoded
				}
				// Was the wavelet created?
				//else if (wavelet == NULL)
				else
				{
					// The wavelet may not have been created during quarter resolution decoding

					// The wavelet should have been created if all bands are valid
					 assert(wavelet != NULL);

					// Advance the bitstream to the next channel
					SetBitstreamPosition(input, position);

					// Reset the decoded subband flags (otherwise this code will be executed again)
					codec->decoded_subband_flags = 0;
				}
				//TODO: Improve quarter resolution decoding so that the wavelet is created?
			}
		}
	}


decoding_completeI:
	STOP(tk_decoding);

	if (result)
	{
		// One frame has been decoded
		decoder->gop_length = 1;
		decoder->frame_count += 1;

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile,
					"DecodeSampleIntraFrame, decoder: 0x%p, GOP length: %d\n",
					decoder, decoder->gop_length);
		}
#endif

		// Return the first frame (the only frame that was decoded)
		if (!decoder->no_output)
		{
			int uncompressed = decoder->uncompressed_chunk && decoder->uncompressed_size && decoder->sample_uncompressed;
			if ( !uncompressed && resolution == DECODED_RESOLUTION_QUARTER && (decoder->codec.encoded_format != ENCODED_FORMAT_BAYER))
			{
				//CODEC_STATE *codec = &decoder->codec;
				TRANSFORM **transform_array = decoder->transform;
				int num_channels = codec->num_channels;
				//int progressive = codec->progressive;
				FRAME_INFO *info = &decoder->frame;
				int precision = codec->precision;

#if _THREADED_DECODER
				// Wait until the transform thread has finished all pending transforms
				WaitForTransformThread(decoder);
#endif
				ConvertQuarterFrameToBuffer(decoder, transform_array, num_channels, output, pitch, info, precision);
			}
			else
			{
				// Finish computing the output frame
				ReconstructSampleFrameToBuffer(decoder, 0, output, pitch);
			}
		}

		if (decoder->error != CODEC_ERROR_OKAY) {
			result = false;
		}

#if TIMING
		// Increment the count of bytes that have been decoded
		decode_byte_count += (COUNTER)BitstreamByteCount(input);
#endif
	}

	if (!result)
	{
		// Check that the frame can be cleared
		assert(frame_size > 0);
		if (frame_size > 0)
		{
			// Zero the frame
			memset(output, 0, frame_size);
		}
	}

	return result;
}

// Decode a sample channel header
bool DecodeSampleChannelHeader(DECODER *decoder, BITSTREAM *input)
{
#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	CODEC_ERROR error = CODEC_ERROR_OKAY;

	CODEC_STATE *codec = &decoder->codec;
	int channel = codec->channel;

	CHANNEL_HEADER header;

	TRANSFORM *transform = decoder->transform[channel];
	TRANSFORM *next_transform;

	// Advance to the next channel
	channel++;

	// Get the next transform for decoded information
	//TRANSFORM *next_transform = AllocGroupTransform(group, channel);

	// Decode the rest of the channel header
	error = DecodeChannelHeader(input, &header, SAMPLE_TYPE_CHANNEL);
	assert(error == CODEC_ERROR_OKAY);
	decoder->error = error;
	if (error != CODEC_ERROR_OKAY) return false;
	
	// The decoder is not able to skip channels
	assert(header.channel == channel);

	// Initialize the next transform using the previous one
	next_transform = decoder->transform[channel];
	InitChannelTransform(next_transform, transform);

	// Update the channel
	codec->channel = channel;

	// Reset the subband counter
	codec->band.subband = 0;

	// Reset the decoded subband flags
	codec->decoded_subband_flags = 0;

	// Loop back to decode the next channel
	//transform = next_transform;

	return true;
}

// Decode the coefficients in a subband
bool DecodeSampleSubband(DECODER *decoder, BITSTREAM *input, int subband)
{
#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	CODEC_STATE *codec = &decoder->codec;
	int channel = codec->channel;
	TRANSFORM *transform = decoder->transform[channel];
	int *subband_wavelet_index = decoder->subband_wavelet_index;

	// Used for quarter resolution and threaded decoding
	int transform_type = transform->type;

	// Wavelet parameters
	int width;
	int height;
	int level;
	int type;
	int band;
	int threading = 1;

	// Wavelet containing the band to decode
	int index;
	IMAGE *wavelet = NULL;

	bool result;
	
	if(subband >= 7 && subband <= 10 && transform_type == TRANSFORM_TYPE_FIELDPLUS)
		threading = 0;

	// Update the transform data structure from the codec state
	UpdateCodecTransform(transform, codec);

	// Is this an empty band?
	if (subband == 255)
	{
		// Decode an empty band

		// This wavelet is the temporal wavelet
		index = 2;
		wavelet = transform->wavelet[index];

		// Get the wavelet parameters decoded from the bitstream
		width = codec->band.width;
		height = codec->band.height;
		level = codec->highpass.wavelet_level;
		type = codec->highpass.wavelet_type;
		band = codec->band.number;

		// The empty band should be the highpass band in a temporal wavelet
		assert(type == WAVELET_TYPE_TEMPORAL && band == 1);

#if _THREADED_DECODER
		// Allocate (or reallocate) the wavelet with thread safety
		wavelet = GetWaveletThreadSafe(decoder, transform, index, width, height, level, type);
#else
		// Allocate (or reallocate) the wavelet
#if _ALLOCATOR
		wavelet = ReallocWaveletEx(decoder->allocator, wavelet, width, height, level, type);
#else
		wavelet = ReallocWaveletEx(wavelet, width, height, level, type);
#endif
		// Save this wavelet in the transform data structure
		transform->wavelet[index] = wavelet;
#endif
		// Set the wavelet parameters
		wavelet->pixel_type[band] = PIXEL_TYPE_16S;
		wavelet->num_bands = 2;

		result = DecodeSampleEmptyBand(decoder, input, wavelet, band);

		// Set the subband number for the next band expected in the bitstream
		codec->band.subband = 11;
	}

	// Is this a highpass band?
	else if (subband > 0)
	{
		// Decode a highpass band

		// Get the wavelet that contains this subband
		index = subband_wavelet_index[subband];
		wavelet = transform->wavelet[index];

		// Get the wavelet parameters decoded from the bitstream
		width = codec->band.width;
		height = codec->band.height;
		level = codec->highpass.wavelet_level;
		type = codec->highpass.wavelet_type;
		band = codec->band.number;

#if _THREADED_DECODER
		// Allocate (or reallocate) the wavelet with thread safety
		wavelet = GetWaveletThreadSafe(decoder, transform, index, width, height, level, type);
#else
		// Allocate (or reallocate) the wavelet
#if _ALLOCATOR
		wavelet = ReallocWaveletEx(decoder->allocator, wavelet, width, height, level, type);
#else
		wavelet = ReallocWaveletEx(wavelet, width, height, level, type);
#endif
		// Save this wavelet in the transform data structure
		transform->wavelet[index] = wavelet;
#endif
		result = DecodeSampleHighPassBand(decoder, input, wavelet, band, threading);
		if (result)
		{
			// Call thread safe routine to update the band valid flags
			UpdateWaveletBandStartedFlags(decoder, wavelet, band);
		}

		// Reset the default encoding method
		codec->band.encoding = BAND_ENCODING_RUNLENGTHS;

		// Set the subband number for the next band expected in the bitstream
		codec->band.subband = subband + 1;
	}
	else
	{
		// Decode a lowpass band

		// Get the wavelet that contains this subband
		index = subband_wavelet_index[0];
		wavelet = transform->wavelet[index];

		// Get the wavelet parameters decoded from the bitstream
		width = codec->lowpass.width;
		height = codec->lowpass.height;
		level = codec->lowpass.level;
		type = codec->first_wavelet;
		//band = codec->band.number;
		band = 0;

#if _THREADED_DECODER
		// Allocate (or reallocate) the wavelet with thread safety
		wavelet = GetWaveletThreadSafe(decoder, transform, index, width, height, level, type);
#else
		// Allocate (or reallocate) the wavelet
#if _ALLOCATOR
		wavelet = ReallocWaveletEx(decoder->allocator, wavelet, width, height, level, type);
#else
		wavelet = ReallocWaveletEx(wavelet, width, height, level, type);
#endif
		// Save this wavelet in the transform data structure
		transform->wavelet[index] = wavelet;
#endif
		// The lowpass data is always stored in wavelet band zero
		assert(band == 0);

		// The lowpass band must be subband zero
		assert(subband == 0);

		result = DecodeSampleLowPassBand(decoder, input, wavelet);
		if (result)
		{
			// Call thread safe routine to update the band valid flags
			UpdateWaveletBandValidFlags(decoder, wavelet, band);
		}

		// Set the subband number for the next band expected in the bitstream
		codec->band.subband = subband + 1;
	}

	// Was the subband successfully decoded?
	if (result)
	{
		// The transform will set the band valid flag if this is the temporal wavelet
		//if (index != 2)

		// Record that this subband has been decoded successfully
		if (0 <= subband && subband <= CODEC_MAX_SUBBAND)
			codec->decoded_subband_flags |= DECODED_SUBBAND_MASK(subband);

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Decoded subband: %d, wavelet: %d, channel: %d\n",
					subband, index, channel);
		}
#endif
	}

#if _THREADED_DECODER
	// Ready to queue a threaded transform to invert this wavelet?

	if (BANDS_ALL_STARTED(wavelet))
	{
		// Are frames being decoded to quarter resolution?

		if (decoder->frame.resolution == DECODED_RESOLUTION_QUARTER && (decoder->codec.encoded_format != ENCODED_FORMAT_BAYER))
		{
			// Smallest spatial wavelet above the lowpass temporal band (fieldplus transform)
			int highest_index = 5;

			if (transform_type == TRANSFORM_TYPE_SPATIAL)
			{
				// Smallest wavelet in the spatial transform
				highest_index = 2;
			}

			// Only the smallest spatial wavelet must be reconstructed
			if (index != highest_index) {
				return result;
			}

			//TODO: Can we improve on the current scheme for quarter resolution decoding?
		}

		if ((transform->type == TRANSFORM_TYPE_SPATIAL && index > 0) || index >= 2)
		{
			if(decoder->entropy_worker_new.pool.thread_count && threading)
			{
				ReconstructWaveletBand(decoder, transform, codec->channel, wavelet, index,
											codec->precision, &decoder->scratch, 1);
				// Add the inverse wavelet transform to the processing queue
				QueueThreadedTransform(decoder, codec->channel, index);
			}
			else
			{
				// Apply the inverse wavelet transform to reconstruct the lower level wavelet
				ReconstructWaveletBand(decoder, transform, codec->channel, wavelet, index,
											codec->precision, &decoder->scratch, 0);
			}
		}
	}

#else
	// Ready to invert this wavelet to get the lowpass band in the lower wavelet?
	if (BANDS_ALL_VALID(wavelet))
	{
		int channel = codec->channel;
		//PIXEL *buffer = (PIXEL *)decoder->buffer;
		//size_t buffer_size = decoder->buffer_size;
		int precision = codec->precision;

#if (0 && DEBUG)
		if (logfile) {
			char label[_MAX_PATH];
			int band;

			sprintf(label, "Channel: %d, index: %d", channel, index);
			DumpImageStatistics(label, wavelet, logfile);
#if 1
			for (band = 1; band < wavelet->num_bands; band++)
			{
				sprintf(label, "Channel: %d, index: %d, band: %d", channel, index, band);
				DumpBandStatistics(label, wavelet, band, logfile);
			}
#endif
		}
#endif
		// Are frames being decoded to quarter resolution?
		if (decoder->frame.resolution == DECODED_RESOLUTION_QUARTER && (decoder->codec.encoded_format != ENCODED_FORMAT_BAYER))
		{
			// Smallest spatial wavelet above the lowpass temporal band (fieldplus transform)
			int highest_index = 5;

			if (transform_type == TRANSFORM_TYPE_SPATIAL)
			{
				// Smallest wavelet in the spatial transform
				highest_index = 2;
			}

			// Only the smallest spatial wavelet must be reconstructed
			if (index != highest_index) {
				return result;
			}

			//TODO: Can we improve on the current scheme for quarter resolution decoding?
		}

		// Apply the inverse wavelet transform to reconstruct the lower level wavelet
		ReconstructWaveletBand(decoder, transform, channel, wavelet, index, precision, &decoder->scratch, 0);
	}
#endif

	return result;
}

// Decode the coefficients in a lowpass band
bool DecodeSampleLowPassBand(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet)
{

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	CODEC_STATE *codec = &decoder->codec;
	int channel = codec->channel;
	bool result = true;

	int lowpass_width;		// Lowpass band dimensions
	int lowpass_height;
	int lowpass_pitch;
	PIXEL *pLowPassRow;		// Pointer into the lowpass band

	//int wavelet_width;	// Dimensions of the wavelet image
	//int wavelet_height;

	int bits_per_pixel;
	int quantization;
	int offset;

	//int pixel_divisor = (1 << (2 * codec->lowpass.level));
	int row, column;
	int32_t solid_color = -1;

	const int gain = 128;
	const int colorshift = 0;

//	int channelgain[4];
	//int waterrow=19, watercol=214;

	//int cspace = decoder->frame.colorspace;

	// Lowpass image dimensions may be smaller than the wavelet dimensions
	// because the encoder may have transmitted an image without the border
	lowpass_width = codec->lowpass.width;
	lowpass_height = codec->lowpass.height;
	lowpass_pitch = wavelet->pitch/sizeof(PIXEL);
	pLowPassRow = wavelet->band[0];


	// Get the parameters for quantization performed by the encoder
	quantization = codec->lowpass.quantization;
	offset = codec->lowpass.pixel_offset;
	bits_per_pixel = codec->lowpass.bits_per_pixel;

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Decode lowpass subband\n");
	}
#endif

	if (bits_per_pixel == 16 && stream->nBitsFree == BITSTREAM_BUFFER_SIZE && !(lowpass_width&1))
	{
		int32_t *lpCurrentLong = (int32_t *)stream->lpCurrentWord;
		//int signval = 0;
		//int channel3stats = 0;
		int channeloffset = 0;

		if(decoder->codec.precision == 8)
		{
			channeloffset = (codec->num_frames==2 ? 64 : 32);
		}
		else if(decoder->codec.precision == 10)
		{
			switch(decoder->frame.format)
			{
			case DECODED_FORMAT_YU64:
			case DECODED_FORMAT_YR16:
			case DECODED_FORMAT_V210:
				channeloffset = codec->num_frames==2 ? 14 : 4;//DAN20090601, recal I-frame DAN20110301
				break;
			default:
				channeloffset = codec->num_frames==2 ? 48 : 24;//DAN20090601
			}

			if(decoder->sample_uncompressed) //DAN20110301 was testing the GOP length for this (why?)
				channeloffset = 0; //DAN20100822 -- Prevent offset between uncompressed V210 and compressed frames
		}
		else if(decoder->codec.precision == 12)
		{
			switch(decoder->frame.format)
			{
			case DECODED_FORMAT_RGB24:
			case DECODED_FORMAT_RGB24_INVERTED:
			case DECODED_FORMAT_RGB32:
			case DECODED_FORMAT_RGB32_INVERTED:
				channeloffset = 8;  //DAN200906010
				break;

				// 16-bit precision:
			case DECODED_FORMAT_RG48:
			case DECODED_FORMAT_RG64:
			case DECODED_FORMAT_B64A:
			case DECODED_FORMAT_WP13:
			case DECODED_FORMAT_W13A:
				channeloffset = 0;
				break;

			case DECODED_FORMAT_RG30:
			case DECODED_FORMAT_R210:
			case DECODED_FORMAT_DPX0:
			case DECODED_FORMAT_AR10:
			case DECODED_FORMAT_AB10:
				channeloffset = 6;   //DAN200906010  //DAN20100822 -- prefect for uncompressed to compressed.
				break;	

			default:
				channeloffset = 0; 
				break;
			}
		}

		if(decoder->codec.encoded_format == ENCODED_FORMAT_BAYER)	//DAN20090728 -- Prevent offset between uncompressed and compressed RAW frames
			channeloffset = 0;


#define DUMPLL 0
#if (_DEBUG && DUMPLL)
		FILE *fp;

		if(channel == 0)
		{
			static int inc = 1;
			char name[256];

			sprintf(name,"C:\\Cedoc\\LLdec%03d.pgm", inc++);


			fp = fopen(name,"w");
			fprintf(fp, "P2\n# CREATOR: DAN\n%d %d\n255\n", lowpass_width, lowpass_height);
		}
#endif

#if LOSSLESS
		channeloffset = 0; //LOSSLESS
#endif

		//if(lpCurrentLong[0] == 0xffffffff)
		if(lpCurrentLong[0] == (int32_t)UINT32_MAX)
		{
			if(SwapInt32BtoN(lpCurrentLong[2]) == (uint32_t)lowpass_width)
			{
				if(SwapInt32BtoN(lpCurrentLong[3]) == (uint32_t)lowpass_height)
				{
					solid_color = SwapInt32BtoN(lpCurrentLong[1]);
					solid_color |= (solid_color<<16);
					lpCurrentLong += 4;
				}
			}
		}

		// Decode each row in the lowpass image
		for (row = 0; row < lowpass_height; row++)
		{
			int pixels;

			// Start at the first column
			column = 0;

			// Process the rest of the row
			{
				for (; column < lowpass_width; column++)
				{
					int pixel_value;
					//int i;

					// Perform inverse quantization
					if(column & 1)
					{
						pixel_value = pixels;

					}
					else
					{
						//pixels = _bswap(*(lpCurrentLong++));
						if(solid_color == -1)
							pixels = SwapInt32BtoN(*(lpCurrentLong++));
						else
							pixels = solid_color;
						pixel_value = (pixels>>16);
						pixels <<= 16;
						pixels >>= 16;
					}


				//  Store the pixel in the lowpass band of the wavelet
					pixel_value += channeloffset;
				//	pixel_value -= 64;
				//	pixel_value += ((rand() & 0x7fff) - 0x4000);
				//	if(pixel_value < 0) pixel_value = 0;

					if(pixel_value > 0x7fff) pixel_value = 0x7fff;
					pLowPassRow[column] = pixel_value;

#if (_DEBUG && DUMPLL)
					if(channel==0 && fp)
						fprintf(fp, "%d\n", pixel_value>>7);
#endif
				}
			}
			// Advance to the next row in the lowpass image
			pLowPassRow += lowpass_pitch;
		}


#if (_DEBUG && DUMPLL)
		if(channel == 0 && fp)
			fclose(fp);
#endif


#if ERROR_TOLERANT
		// Update the count of bytes used
		stream->nWordsUsed -= (int)(((intptr_t)lpCurrentLong - (intptr_t)stream->lpCurrentWord));
#endif
		// Update the bitstream
		stream->lpCurrentWord = (uint8_t  *)lpCurrentLong;

	}
	else if (bits_per_pixel == 8 && stream->nBitsFree == BITSTREAM_BUFFER_SIZE)
	{
		uint8_t *lpCurrentByte = (uint8_t *)stream->lpCurrentWord;

		//int signval = 0;

		// Decode each row in the lowpass image
		for (row = 0; row < lowpass_height; row++)
		{
			// Start at the first column
			column = 0;

			// Process the rest of the row
			for (; column < lowpass_width; column++)
			{
				int pixel_value = *(lpCurrentByte++);

				// Perform inverse quantization
#if _ENCODE_CHROMA_ZERO
				if (channel == 0)
					pixel_value = (quantization * pixel_value) + offset;
				else
					pixel_value = (pixel_value - offset) * quantization;
#else
				pixel_value = (quantization * pixel_value) + offset;// + colorshift;
#endif
				pixel_value -= 128 * quantization;
				pixel_value *= gain;
				pixel_value >>= 7;

				pixel_value += 128 * quantization;

				pixel_value += colorshift;


				// Store the pixel in the lowpass band of the wavelet
				// Multiply by 16 to turn 8-bit into the new 16-bit format
				pLowPassRow[column] = pixel_value * 16;
			}

			// Advance to the next row in the lowpass image
			pLowPassRow += lowpass_pitch;
		}

#if ERROR_TOLERANT
		// Update the count of bytes used
		stream->nWordsUsed -= (int)(((intptr_t)lpCurrentByte - (intptr_t)stream->lpCurrentWord));
#endif

		// Update the bitstream
		stream->lpCurrentWord = (uint8_t  *)lpCurrentByte;
	}
	else
	{
		int channeloffset = 0;

		if(decoder->codec.precision == 8)
		{
			channeloffset = (codec->num_frames==2 ? 64 : 32);
		}
		else if(decoder->codec.precision == 10)
		{
			channeloffset = (codec->num_frames==2 ? 10 : 5);
		}
		else if(decoder->codec.precision == 12)
		{
		//	channeloffset = (codec->num_frames==2 ? 4 : 2);  // Seems to result in less shift using the viper images
		}

		//DAN20050923 no longer trying to compensate for YUV to RGB issues.
		if(decoder->frame.format == DECODED_FORMAT_RGB24 || decoder->frame.format == DECODED_FORMAT_RGB32)
		{
			if(decoder->codec.precision == 8)
			{
				switch(channel)
				{
				case 0: channeloffset += 8; break; // fixed rounding error introduced by YUV->RGB
				case 1: channeloffset += 16; break;
				case 2: channeloffset += 10; break;
				}
			}
			else if(decoder->codec.precision == 10)
			{
				switch(channel)
				{
				case 0: channeloffset += -8; break; // fixed rounding error introduced by YUV->RGB
				case 1: channeloffset += -4; break;
				case 2: channeloffset += -4; break;
				}
			}
			else if(decoder->codec.precision == 12)
			{
				switch(channel)
				{
				case 0: channeloffset += 0; break; // fixed rounding error introduced by YUV->RGB
				case 1: channeloffset += 0; break;
				case 2: channeloffset += 0; break;
				}
			}
		}

		if(bits_per_pixel != 16)
			channeloffset = 0;

		for (row = 0; row < lowpass_height; row++)
		{
			for (column = 0; column < lowpass_width; column++) {
				int pixel_value = GetBits(stream, bits_per_pixel);

				// Perform inverse quantization
#if _ENCODE_CHROMA_ZERO
				if (channel == 0)
					pixel_value = (quantization * pixel_value) + offset;
				else
					pixel_value = (pixel_value - offset) * quantization;
#else
				pixel_value = (quantization * pixel_value) + offset;// + colorshift;
#endif

				// Store the pixel in the lowpass band of the wavelet
				pLowPassRow[column] = SATURATE(pixel_value + channeloffset); // DAN20050926 added chromaoffet to match the normal path -- this code will be used for SD (720) encodes
			}
			stream->nWordsUsed -= lowpass_width*(bits_per_pixel>>3);

			// Advance to the next row in the lowpass image
			pLowPassRow += lowpass_pitch;
		}
	}

	// Set the wavelet scale factor
	wavelet->scale[0] = quantization;

	// Align the bitstream to the next tag value pair
	AlignBitsTag(stream);

	// Return indication of lowpass decoding success
	return result;
}

// Decode the coefficients in a highpass band
bool DecodeSampleHighPassBand(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet, int band, int threading)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif

	CODEC_STATE *codec = &decoder->codec;
	//int channel = codec->channel;
	//int subband = codec->band.subband;
	//int index = codec->highpass.wavelet_number;

	int width;
	int height;
	int quantization;

	// The encoder may not have used variable-length coding
	int method = codec->band.encoding;

	bool result = true;

	// Check that the band index is in range
	assert(0 <= band && band <= codec->max_subband);

	// Encoded coefficients start on a tag boundary
	AlignBitsTag(stream);

#if (0 && DEBUG)
	// Dump the band header to the logfile
	if (logfile) {
		fprintf(logfile,
				"Band header marker: 0x%04X, subband: %d, width: %d, height: %d, encoding: %d\n",
				header->marker, header->subband, header->width, header->height, header->encoding);
	}
#endif

	// Copy the scale factors used by the encoder into the wavelet band
	// (Zero means that the encoder did not supply this parameter)
	if (codec->band.scale > 0) {
		wavelet->scale[band] = codec->band.scale;
	}

	// Get the quantization factor that was used to encode the band coefficients
	quantization = codec->band.quantization;

	// Copy the quantization into the wavelet
	wavelet->quantization[band] = quantization;

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Decode highpass subband: %d, quantization: %d\n", subband, quantization);
	}
#endif

	// Get the highpass band dimensions
	width = codec->band.width;
	height = codec->band.height;

	// Is this a special band for the temporal high pass thumbnail?
	if (method == BAND_ENCODING_LOSSLESS)
	{
		//lossless temporal subband //DAN20060701
		result = DecodeBand16sLossless(decoder, stream, wavelet, band, width, height);
		assert(result);
		if (result)
		{
			// Call thread safe routine to update the band valid flags
			UpdateWaveletBandValidFlags(decoder, wavelet, band);
		}
	}
	else if (method == BAND_ENCODING_16BIT)
	{
		//lossless temporal subband //DAN20060701
		result = DecodeBand16s(decoder, stream, wavelet, band, width, height);
		assert(result);
		if (result)
		{
			// Call thread safe routine to update the band valid flags
			UpdateWaveletBandValidFlags(decoder, wavelet, band);
		}
	}
	else
	{
		// Must use the runlength encoding method
		assert(codec->band.encoding == BAND_ENCODING_RUNLENGTHS);
#if 0
		// This code attempts to not decode various subbands for 1/4 res decodes.
		// Unforuntately playback would stop after 5 seonds with this code (but not in debug mode.)
		if (subband >= 4 && subband <= 6)
		{
			TAGVALUE segment;

			AlignBitsTag(stream);
			do
			{
				segment = GetTagValue(stream);
			}
			while(segment.tuple.tag != CODEC_TAG_BAND_TRAILER);

			stream->lpCurrentWord -= 4;
			stream->nWordsUsed += 4;
		}
		else
#elif 0
		// Is this subband required for decoding the frame?
		if (CanSkipSubband(decoder, subband))
		{
			// Skip past the end of this subband
			SkipSubband(stream);
		}
#endif
		// Decode this subband
		result = DecodeFastRunsFSM16s(decoder, stream, wavelet, band, width, height, threading);
	}

	// Return failure if a problem was encountered while reading the band coefficients
	if (!result) return result;

	// The encoded band coefficients end on a bitstream word boundary
	// to avoid interference with the marker for the coefficient band trailer
	AlignBits(stream);

	// Decode the band trailer
	error = DecodeBandTrailer(stream, NULL);
	decoder->error = error;
	assert(error == CODEC_ERROR_OKAY);
	if (error != CODEC_ERROR_OKAY) {
#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Error in band %d trailer: %d\n", band, error);
		}
#endif
		return false;
	}

	return result;
}

// Decode an empty band
bool DecodeSampleEmptyBand(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet, int band)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif

	CODEC_STATE *codec = &decoder->codec;
	int quantization;

	// Check that the band is in range
	assert(0 <= band && band <= CODEC_MAX_HIGHBANDS);

	// Check that the highpass band is 16 bits
	assert(wavelet->pixel_type[1] == PIXEL_TYPE_16S);

#if (0 && DEBUG)
	//TODO: Change format string to handle 64-bit pointers
	if (logfile) {
		fprintf(logfile, "Start decoding an empty band, stream: 0x%p\n", stream->lpCurrentWord);
	}
#endif

	// Encoded coefficients must start on a word boundary
	AlignBits(stream);

	// Copy the scale factors used by the encoder into the wavelet band
	// (Zero means that the encoder did not supply the parameter)
	if (codec->band.scale > 0)
		wavelet->scale[band] = codec->band.scale;

	// Set the quantization used to encode the band coefficients
	quantization = codec->band.quantization;
	wavelet->quantization[band] = quantization;

#if (0 && DEBUG)
	if (logfile) {
		DumpBits(stream, logfile);
	}
#endif

	// Decode the band trailer
	error = DecodeBandTrailer(stream, NULL);
	decoder->error = error;
	assert(error == CODEC_ERROR_OKAY);
	if (error != CODEC_ERROR_OKAY) {
#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Error in band: %d, error: %d\n", band, error);
		}
#endif
		return false;
	}

	// The encoded band coefficients end on a bitstream word boundary
	// to avoid interference with the marker for the coefficient band trailer
	AlignBits(stream);

#if (0 && DEBUG)
	// Dump the band trailer to the logfile
	if (logfile) {
		fprintf(logfile, "Band trailer marker: 0x%04X\n", trailer->marker);
	}
#endif

#if (0 && DEBUG)
	if (logfile) {
		//TODO: Change format string to handle 64-bit pointers
		fprintf(logfile, "End decode empty band, stream: 0x%X\n", stream->lpCurrentWord);
	}
#endif

	return true;
}

bool DecodeBand16s(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
				   int band_index, int width, int height)
{
	PIXEL *rowptr = wavelet->band[band_index];
	int pitch = wavelet->pitch;
	int row,dequant = wavelet->quantization[band_index];

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	//BAND_ENCODING_16BIT
	if(dequant == 1)
	{
		for (row = 0; row < height; row++)
		{
			int column;

#if 0
			for (column = 0; column < width; column++)
			{
				int value = GetWord16s(stream);
				rowptr[column] = value;
			}
#else // Mild speedup (2.5% overall half-res decode improvement.)
			char *sptr = (char *)stream->lpCurrentWord;
			char *dptr = (char *)rowptr;
			for (column = 0; column < width; column++)
			{
				*(dptr+1) = *sptr++;
				*dptr = *sptr++;
				dptr+=2;
			}

			stream->lpCurrentWord += width*2;
			stream->nWordsUsed += width*2;
#endif
			rowptr += pitch;
		}
	}
	else
	{
		for (row = 0; row < height; row++)
		{
			int column;

			for (column = 0; column < width; column++)
			{
				int value = GetWord16s(stream);
				rowptr[column] = value*dequant;
			}

			rowptr += pitch;
		}
	}


#if (0 && DEBUG)
	{
		int static count = 0;
		if (count < 20) {
			char label[_MAX_PATH];
			sprintf(label, "Hightemp-decode-%d-", count);
			DumpBandPGM(label, wavelet, band_index, NULL);
		}
		count++;
	}
#endif

	return true;
}


bool DecodeBand16sLossless(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
				   int band_index, int width, int height)
{
	//CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	int result = true;
	int quant = wavelet->quantization[band_index];

	// Get the pointer to the finite state machine
	FSM *fsm = &decoder->fsm[decoder->codec.active_codebook];
	int size;
	PIXEL *rowptr;
	//int row = 0;
	int pitch;
	//CODEC_STATE *codec = &decoder->codec;
	//int channel = codec->channel;
	//int subband = codec->band.subband;
	//int num_subbands = codec->num_subbands;
	//int pixel_type = wavelet->pixel_type[band_index];
	//int difference_coding = decoder->codec.difference_coding;
	//int localquant = 1;
	//int threading = 0;

	decoder->codec.active_codebook = 0; // reset CODEC state
	decoder->codec.difference_coding = 0; //reset state for next subband

	// Must have a valid wavelet
	assert(wavelet != NULL);
	if (! (wavelet != NULL)) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}

	//Must have a valid FSM
	assert(fsm != NULL);
	if (! (fsm != NULL)) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}

	// All rows are treated as one int32_t row that covers the entire band
	size = fsm->table.num_states;

	assert(size > 0);
	if (size == 0) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}

	// Check if the band is intended for 8-bit pixels
	assert(wavelet->pixel_type[band_index] == PIXEL_TYPE_16S);

	rowptr = (PIXEL *)wavelet->band[band_index];
	pitch = wavelet->pitch;

	assert(rowptr != NULL && pitch != 0);
	if (! (rowptr != NULL && pitch != 0)) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}

	DeQuantFSM(fsm, 1); // can;t use this to dequant as we split the cooefficients into high and low bytes.
	if (!DecodeBandFSM16sNoGap2Pass(fsm, stream, (PIXEL16S *)rowptr, width, height, pitch, quant)) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}

	if(quant)
	{
		int x,y;
		PIXEL *line = rowptr;

		if(quant == 32)
		{
			for(y=0;y<height;y++)
			{
				for(x=0;x<width;x++)
				{
					line[x] <<= 5;
				}
				line += pitch/2;
			}
		}
		else
		{
			for(y=0;y<height;y++)
			{
				for(x=0;x<width;x++)
				{
					line[x] *= quant;
				}
				line += pitch/2;
			}
		}
	}
/*	if(once <= 60)
	{
		char name[200];
		FILE *fp;

		sprintf(name,"C:/Cedoc/DUMP/Decoder/dump%02d.raw", once);

		fp = fopen(name,"wb");
		fwrite(rowptr,width*height,1,fp);
		fclose(fp);
		once++;
	}*/

	assert(result == true);
	if (! (result == true)) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}

	return true;
}


// Invert the wavelet to reconstruct the lower wavelet in the transform
void ReconstructWaveletBand(DECODER *decoder, TRANSFORM *transform, int channel,
							IMAGE *wavelet, int index, int precision,
							const SCRATCH *scratch, int allocations_only)
{
	int transform_type = transform->type;
	int width = wavelet->width;
	int height = wavelet->height;
	int level = wavelet->level;

	PIXEL *buffer = (PIXEL *)scratch->free_ptr;
	size_t buffer_size = scratch->free_size;
	
	// Is the current wavelet a spatial wavelet?
	if (transform_type == TRANSFORM_TYPE_SPATIAL && index > 0)
	{
		// Reconstruct the lowpass band in the lower wavelet
		int lowpass_index = index - 1;
		IMAGE *lowpass = transform->wavelet[lowpass_index];
		int lowpass_width = 2 * width;
		int lowpass_height = 2 * height;
		int lowpass_level = level - 1;
		int lowpass_type = (lowpass_index == 0) ? WAVELET_TYPE_FRAME : WAVELET_TYPE_SPATIAL;

		//const int prescale = 1;
		const bool inverse_prescale = (precision >= CODEC_PRECISION_10BIT);
		int prescale = transform->prescale[index];

#if _THREADED_DECODER
		// Allocate (or reallocate) the wavelet with thread safety
		lowpass = GetWaveletThreadSafe(decoder, transform, lowpass_index,
									   lowpass_width, lowpass_height,
									   lowpass_level, lowpass_type);
#else
		// Allocate the wavelet if not already allocated
#if _ALLOCATOR
		lowpass = ReallocWaveletEx(decoder->allocator, lowpass, lowpass_width, lowpass_height, lowpass_level, lowpass_type);
#else
		lowpass = ReallocWaveletEx(lowpass, lowpass_width, lowpass_height, lowpass_level, lowpass_type);
#endif
		transform->wavelet[lowpass_index] = lowpass;
#endif
		// Check that the lowpass band has not already been reconstructed
		//assert((lowpass->band_valid_flags & BAND_VALID_MASK(0)) == 0);

		if(!allocations_only)
		{
			// Check that all of the wavelet bands have been decoded
			assert(BANDS_ALL_VALID(wavelet));

			// Has this wavelet already been reconstructed?
			if ((lowpass->band_valid_flags & BAND_VALID_MASK(0)) == 0)
			{
				// Perform the inverse spatial transform before decoding the next wavelet
				STOP(tk_decoding);
				START(tk_inverse);
				//TransformInverseSpatialQuantLowpass(wavelet, lowpass, buffer, buffer_size, prescale, inverse_prescale);
				TransformInverseSpatialQuantLowpass(wavelet, lowpass, scratch, prescale, inverse_prescale);
				STOP(tk_inverse);
				START(tk_decoding);

				// Call thread safe routine to update the band valid flags
				UpdateWaveletBandValidFlags(decoder, lowpass, 0);
	#if TIMING
				// Increment the count of spatial transforms performed during decoding
				spatial_decoding_count++;
	#endif
			}
		}
	}

	// Is the current wavelet a spatial wavelet above the temporal lowpass band?
	else if (index > 3)
	{
		// Reconstruct the lowpass band in the lower wavelet
		const int temporal_wavelet_index = 2;
		int lowpass_index = (index > 4) ? index - 1 : index - 2;
		IMAGE *lowpass = transform->wavelet[lowpass_index];
		int lowpass_width = 2 * width;
		int lowpass_height = 2 * height;
		int lowpass_level = level - 1;
		int lowpass_type = ((lowpass_index == temporal_wavelet_index) ? WAVELET_TYPE_TEMPORAL : WAVELET_TYPE_SPATIAL);

		//const int prescale = 2;
		const bool inverse_prescale = (precision >= CODEC_PRECISION_10BIT);
		int prescale = transform->prescale[index];

#if _THREADED_DECODER
		// Allocate (or reallocate) the wavelet with thread safety
		lowpass = GetWaveletThreadSafe(decoder, transform, lowpass_index,
									   lowpass_width, lowpass_height,
									   lowpass_level, lowpass_type);
#else
		// Allocate the wavelet if not already allocated
#if _ALLOCATOR
		lowpass = ReallocWaveletEx(decoder->allocator, lowpass, lowpass_width, lowpass_height, lowpass_level, lowpass_type);
#else
		lowpass = ReallocWaveletEx(lowpass, lowpass_width, lowpass_height, lowpass_level, lowpass_type);
#endif
		transform->wavelet[lowpass_index] = lowpass;
#endif

		if(!allocations_only)
		{
			// Check that the lowpass band has not already been reconstructed
			assert((lowpass->band_valid_flags & BAND_VALID_MASK(0)) == 0);

			// Check that all of the wavelet bands have been decoded
			assert(BANDS_ALL_VALID(wavelet));

			// Perform the inverse spatial transform before decoding the next wavelet
			STOP(tk_decoding);
			START(tk_inverse);
			//TransformInverseSpatialQuantLowpass(wavelet, lowpass, buffer, buffer_size, prescale, inverse_prescale);
			TransformInverseSpatialQuantLowpass(wavelet, lowpass, scratch, prescale, inverse_prescale);
			STOP(tk_inverse);
			START(tk_decoding);

			// Call thread safe routine to update the band valid flags
			UpdateWaveletBandValidFlags(decoder, lowpass, 0);
	#if TIMING
			// Increment the count of spatial transforms performed during decoding
			spatial_decoding_count++;
	#endif
		}
	}

	// Is the current wavelet the spatial wavelet above the temporal highpass band?
	else if (index == 3)
	{
		// Reconstruct the highpass band in the temporal wavelet
		const int temporal_wavelet_index = 2;
		int highpass_index = index - 1;
		IMAGE *highpass = transform->wavelet[highpass_index];
		int highpass_width = 2 * width;
		int highpass_height = 2 * height;
		int highpass_level = level - 1;
		int highpass_type = ((highpass_index == temporal_wavelet_index) ? WAVELET_TYPE_TEMPORAL : WAVELET_TYPE_SPATIAL);

		const bool inverse_prescale = (precision >= CODEC_PRECISION_10BIT);
		int prescale = inverse_prescale ? transform->prescale[index] : 0;

#if _THREADED_DECODER
		// Allocate (or reallocate) the wavelet with thread safety
		highpass = GetWaveletThreadSafe(decoder, transform, highpass_index,
										highpass_width, highpass_height,
										highpass_level, highpass_type);
#else
		// Allocate the wavelet if not already allocated
#if _ALLOCATOR
		highpass  = ReallocWaveletEx(decoder->allocator, highpass , highpass_width, highpass_height, highpass_level, highpass_type);
#else
		highpass  = ReallocWaveletEx(highpass , highpass_width, highpass_height, highpass_level, highpass_type);
#endif
		transform->wavelet[highpass_index] = highpass;
#endif

		if(!allocations_only)
		{
			// Check that the highpass band has not already been reconstructed
			assert((highpass->band_valid_flags & BAND_VALID_MASK(1)) == 0);

			// Check that all of the wavelet bands have been decoded
			assert(BANDS_ALL_VALID(wavelet));

			// Perform the inverse spatial transform before decoding the next wavelet
			STOP(tk_decoding);
			START(tk_inverse);
			TransformInverseSpatialQuantHighpass(wavelet, highpass, buffer, buffer_size, prescale);
			STOP(tk_inverse);
			START(tk_decoding);

			// Call thread safe routine to update the band valid flags
			UpdateWaveletBandValidFlags(decoder, highpass, 1);
	#if TIMING
			// Increment the count of spatial transforms performed during decoding
			spatial_decoding_count++;
	#endif
		}
	}

	// Is the current wavelet the temporal wavelet?
	else if (index == 2)
	{
		// Get the temporal wavelet
		IMAGE *temporal = wavelet;

		// Set the frame wavelet parameters
		int frame_level = 1;
		int frame_type = WAVELET_TYPE_FRAME;

		// Get the two frame wavelets
		IMAGE *frame[2];
		frame[0] = transform->wavelet[0];
		frame[1] = transform->wavelet[1];

		// Check that the temporal wavelet is valid
		assert(temporal->num_bands == 2 && temporal->wavelet_type == WAVELET_TYPE_TEMPORAL);

#if _THREADED_DECODER
		// Allocate (or reallocate) the frame wavelets with thread safety
		frame[0] = GetWaveletThreadSafe(decoder, transform, 0, width, height, frame_level, frame_type);
		frame[1] = GetWaveletThreadSafe(decoder, transform, 1, width, height, frame_level, frame_type);
#else
		// Allocate the frame wavelets if not already allocated
#if _ALLOCATOR
		frame[0] = ReallocWaveletEx(decoder->allocator, frame[0], width, height, frame_level, frame_type);
		frame[1] = ReallocWaveletEx(decoder->allocator, frame[1], width, height, frame_level, frame_type);
#else
		frame[0] = ReallocWaveletEx(frame[0], width, height, frame_level, frame_type);
		frame[1] = ReallocWaveletEx(frame[1], width, height, frame_level, frame_type);
#endif
		transform->wavelet[0] = frame[0];
		transform->wavelet[1] = frame[1];
#endif
#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Before inverse temporal transform");
			DumpArray16s("Temporal Lowpass", temporal->band[0], temporal->width, temporal->height, temporal->pitch, logfile);
			DumpArray16s("Temporal Highpass", temporal->band[1], temporal->width, temporal->height, temporal->pitch, logfile);
		}
#endif

		if(!allocations_only)
		{
			// Check that the lowpass bands have not already been reconstructed
			assert((frame[0]->band_valid_flags & BAND_VALID_MASK(0)) == 0);
			assert((frame[1]->band_valid_flags & BAND_VALID_MASK(0)) == 0);

			// Check that all of the wavelet bands have been decoded
			assert(BANDS_ALL_VALID(temporal));

			// Invert the temporal transform between the frame wavelets
			STOP(tk_decoding);
			START(tk_inverse);
			TransformInverseTemporalQuant(temporal, frame[0], frame[1], buffer, buffer_size, precision);
			STOP(tk_inverse);
			START(tk_decoding);

	#if (0 && DEBUG)
			if (logfile) {
				IMAGE *wavelet = quad[0];
				fprintf(logfile, "After inverse temporal transform\n");
				DumpArray16s("Temporal Lowpass", temporal->band[0], temporal->width, temporal->height, temporal->pitch, logfile);
				DumpArray16s("Temporal Highpass", temporal->band[1], temporal->width, temporal->height, temporal->pitch, logfile);
				DumpArray16s("First frame wavelet, band 0", wavelet->band[0], wavelet->width, wavelet->height, wavelet->pitch, logfile);
			}
	#endif

			// Call thread safe routine to update the band valid flags
			UpdateWaveletBandValidFlags(decoder, frame[0], 0);
			UpdateWaveletBandValidFlags(decoder, frame[1], 0);
	#if TIMING
			// Increment the number of temporal transforms performed outside of decoding
			temporal_decoding_count++;
	#endif
		}
	}
}

// Compute the dimensions of the output buffer
void ComputeOutputDimensions(DECODER *decoder, int frame,
							 int *decoded_width_out, int *decoded_height_out)
{

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	CODEC_STATE *codec = &decoder->codec;
	int num_channels = codec->num_channels;

	FRAME_INFO *info = &decoder->frame;

	//int progressive = codec->progressive;

	TRANSFORM **transform_array = decoder->transform;
	//IMAGE *lowpass_images[TRANSFORM_MAX_CHANNELS];
	IMAGE *wavelet = NULL;
	int wavelet_width;
	int wavelet_height;
	int decoded_width;
	int decoded_height;
	int resolution = info->resolution;
	//int chroma_offset = decoder->codec.chroma_offset;
	int decoded_scale = 0;

	if (decoded_width_out == NULL || decoded_height_out == NULL) {
		return;
	}

	// Clear the return values in case this routine terminates early
	*decoded_width_out = 0;
	*decoded_height_out = 0;

	// Get the decoding scale
	switch(resolution)
	{
		case DECODED_RESOLUTION_FULL:
		case DECODED_RESOLUTION_HALF_HORIZONTAL:
#if DEBUG
			assert(AllTransformBandsValid(transform_array, num_channels, frame));
#endif
			decoded_scale = 2;
			wavelet = transform_array[0]->wavelet[0];
			break;

		case DECODED_RESOLUTION_HALF:
#if DEBUG
			assert(AllLowpassBandsValid(transform_array, num_channels, frame));
#endif
			decoded_scale = 1;
			wavelet = transform_array[0]->wavelet[0];
			break;

		case DECODED_RESOLUTION_QUARTER:
			if(decoder->codec.encoded_format == ENCODED_FORMAT_BAYER)
			{
#if DEBUG
				assert(AllLowpassBandsValid(transform_array, num_channels, frame));
#endif
				decoded_scale = 1;
				wavelet = transform_array[0]->wavelet[0];
			}
			else
			{
				decoded_scale = 1;
				wavelet = transform_array[0]->wavelet[3];
			}
			break;

		case DECODED_RESOLUTION_LOWPASS_ONLY:
			decoded_scale = 1;
			wavelet = transform_array[0]->wavelet[5];
			if(wavelet == NULL) // there Intra Frame compressed
				wavelet = transform_array[0]->wavelet[2];
			break;

		default:
			assert(0);
			break;
	}

	// Get the decoded frame dimensions
	assert(wavelet != NULL);
	wavelet_width = wavelet->width;
	wavelet_height = wavelet->height;
	if(resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
		decoded_width = wavelet_width;
	else
		decoded_width = decoded_scale * wavelet_width;
	decoded_height = decoded_scale * wavelet_height;

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Decoded scale: %d, decoded width: %d, wavelet width: %d\n", decoded_scale, decoded_width, wavelet_width);
	}
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Decoded width: %d, height: %d, frame width: %d, height: %d, output pitch: %d\n",
				decoded_width, decoded_height, info->width, info->height, pitch);
	}
#endif

	// Return the decoded width and height
	*decoded_width_out = decoded_width;
	*decoded_height_out = decoded_height;
}

#define DEBUG_ROW16U	0

void ReconstructSampleFrameToBuffer(DECODER *decoder, int frame, uint8_t *output, int pitch)
{
	FRAME_INFO local_info;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	FRAME_INFO *info = &local_info;

	CODEC_STATE *codec = &decoder->codec;
	int num_channels = codec->num_channels;
	int progressive = codec->progressive;

	TRANSFORM **transform_array = decoder->transform;
	IMAGE *lowpass_images[TRANSFORM_MAX_CHANNELS];
	IMAGE *wavelet;
	int wavelet_width;
	int wavelet_height;
	int decoded_width;
	int decoded_height;
	int resolution = decoder->frame.resolution;
	int chroma_offset = decoder->codec.chroma_offset;
	int uncompressed = decoder->uncompressed_chunk && decoder->uncompressed_size && decoder->sample_uncompressed;
	//TODO: Change this routine to return the codec error code
	CODEC_ERROR error = CODEC_ERROR_OKAY;

	//if(decoder->cfhddata.calibration)
	//	LoadTweak();

	//TODO: Change this routine to return an error code
	if (decoder == NULL) {
		return;
	}

	decoder->gop_frame_num = frame;

#if _THREADED_DECODER
	// Wait until the transform thread has finished all pending transforms
	WaitForTransformThread(decoder);
#endif

	//return;

	// copy frame info in a changable local structure
	memcpy(info, &decoder->frame, sizeof(FRAME_INFO));

	// Use the old code for reconstructing the frame

#if (0 && DEBUG)
	// Force quarter resolution decoding for debugging that feature
	resolution = DECODED_RESOLUTION_QUARTER;
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Inverting last wavelet, frame: %d\n", frame);
	}
#endif

	// The decoder can decode a video sample without returning a frame
	if (output == NULL || pitch == 0) return;

#if (1 && DEBUG_ROW16U)
	// Force decoding to 16-bit pixels for debugging
	info->format = DECODED_FORMAT_YR16;
#endif

#if 0
	if (info->format == DECODED_FORMAT_YR16)
	{
		// Force interlaced or progressive decoding for debugging
		//progressive = false;
		progressive = true;
	}
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Decoder flags: 0x%p\n", decoder->flags);
	}
#endif

	// Does this frame have to be reconstructed?
	if ((decoder->flags & DECODER_FLAGS_RENDER) == 0) {
#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Decoder discarding frame: %d\n", frame);
		}
#endif
		return;
	}

	// Check that the requested frame is within the limits of the group of frames
	assert(0 <= frame && frame < decoder->gop_length);

	// Check that the frame resolution is valid
	assert(IsValidFrameResolution(resolution));
	if (!IsValidFrameResolution(resolution)) {
		decoder->error = CODEC_ERROR_RESOLUTION;
		return;
	}

#if (0 && TIMING)	//(0 && DEBUG)
	// Override progressive flag read from the bitstream for debugging
	//progressive = 0;		// Use the inverse frame transform
	progressive = 1;		// Use the inverse spatial transform
#endif

	// Build the 3D LUTs if needed
	ComputeCube(decoder);

	//HACK DAN20110131 -- some formats will not directly decode so need to use the AM route
	{
		if(	decoder->codec.encoded_format == ENCODED_FORMAT_YUV_422 && 
			resolution == DECODED_RESOLUTION_HALF)
		{
			if(	decoder->frame.format == COLOR_FORMAT_R408 || 
				decoder->frame.format == COLOR_FORMAT_V408)
			{
			
				decoder->use_active_metadata_decoder = true;
				decoder->apply_color_active_metadata = true;
			}
		}

		if(	decoder->frame.format == COLOR_FORMAT_NV12)
		{		
			decoder->use_active_metadata_decoder = true;
			
			decoder->apply_color_active_metadata = true; // TODO, make it work with this.
		}

		if (decoder->codec.progressive == false && decoder->frame.format == COLOR_FORMAT_RGB24)
		{
			decoder->use_active_metadata_decoder = true;
			decoder->apply_color_active_metadata = true;
		}
	}

	// Get the decoding scale
	if(!uncompressed)
	{
		switch(resolution)
		{
			case DECODED_RESOLUTION_FULL:
			case DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER:
#if DEBUG
				assert(AllTransformBandsValid(transform_array, num_channels, frame));
#endif
				wavelet = transform_array[0]->wavelet[0];
				// Get the decoded frame dimensions
				assert(wavelet != NULL);
				wavelet_width = wavelet->width;
				wavelet_height = wavelet->height;
				decoded_width = 2 * wavelet_width;
				decoded_height = 2 * wavelet_height;
				break;

			case DECODED_RESOLUTION_HALF:
#if DEBUG
				assert(AllLowpassBandsValid(transform_array, num_channels, frame));
#endif
				wavelet = transform_array[0]->wavelet[0];
				// Get the decoded frame dimensions
				assert(wavelet != NULL);
				wavelet_width = wavelet->width;
				wavelet_height = wavelet->height;
				decoded_width = wavelet_width;
				decoded_height = wavelet_height;
				break;

			case DECODED_RESOLUTION_HALF_HORIZONTAL:
#if DEBUG
				assert(AllLowpassBandsValid(transform_array, num_channels, frame));
#endif
				wavelet = transform_array[0]->wavelet[0];
				// Get the decoded frame dimensions
				assert(wavelet != NULL);
				wavelet_width = wavelet->width;
				wavelet_height = wavelet->height;
				decoded_width = wavelet_width;
				decoded_height = 2 * wavelet_height;
				break;

			case DECODED_RESOLUTION_QUARTER:
				if(decoder->codec.encoded_format == ENCODED_FORMAT_BAYER)
				{
#if DEBUG
					assert(AllLowpassBandsValid(transform_array, num_channels, frame));
#endif
					wavelet = transform_array[0]->wavelet[0];
				}
				else
				{
					wavelet = transform_array[0]->wavelet[3];
				}
				
				// Get the decoded frame dimensions
				assert(wavelet != NULL);
				wavelet_width = wavelet->width;
				wavelet_height = wavelet->height;
				decoded_width = wavelet_width;
				decoded_height = wavelet_height;
				break;

			case DECODED_RESOLUTION_LOWPASS_ONLY:
				wavelet = transform_array[0]->wavelet[5];
				if(wavelet == NULL) // there Intra Frame compressed
					wavelet = transform_array[0]->wavelet[2];
				
				// Get the decoded frame dimensions
				assert(wavelet != NULL);
				wavelet_width = wavelet->width;
				wavelet_height = wavelet->height;
				decoded_width = wavelet_width;
				decoded_height = wavelet_height;
				break;

			default:
				assert(0);
				break;
		}
	}
	else
	{
		if(decoder->codec.encoded_format == ENCODED_FORMAT_BAYER)
		{
			decoded_width = info->width/2;
			decoded_height = info->height/2;
		}
		else
		{
			decoded_width = info->width;
			decoded_height = info->height;
		}
	}

	if(decoder->codec.encoded_format == ENCODED_FORMAT_BAYER)
	{
		if(resolution == DECODED_RESOLUTION_FULL)
		{
			if(decoded_width*2 == info->width)
			{
				info->width /= 2;
				info->height /= 2;

				info->resolution = resolution = DECODED_RESOLUTION_FULL_DEBAYER;
			}
		}
		else if(resolution == DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER)
		{
			if(decoded_width*2 == info->width)
			{
				info->width /= 2;
				info->height /= 2;
			}
		}
		else if(resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
		{
			if(decoded_width*2 == info->width)
			{
				info->height /= 2;
				info->resolution = resolution = DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER;
			}
		}
		else if(decoder->frame.format == DECODED_FORMAT_BYR2 || decoder->frame.format == DECODED_FORMAT_BYR4)
		{
			if(decoded_width*2 == info->width)
			{
				info->width /= 2;
				info->height /= 2;

				info->resolution = resolution = DECODED_RESOLUTION_HALF_NODEBAYER;
			}
		}
		else
		{
			if(resolution == DECODED_RESOLUTION_HALF)
			{
				if(decoded_width*2 == info->width)
				{
					decoded_width *= 2;
					decoded_height *= 2;
					info->resolution = resolution = DECODED_RESOLUTION_FULL;
				}
			}
			else if(resolution == DECODED_RESOLUTION_QUARTER)
			{
				if(uncompressed)
				{
					decoded_width *= 2;
					decoded_height *= 2;
					info->resolution = resolution = DECODED_RESOLUTION_QUARTER_NODEBAYER_SCALED;
				}
				else
				{
					if(decoded_width == info->width)
					{
						info->resolution = resolution = DECODED_RESOLUTION_HALF;
					}
				}
			}
		}
	}

	if(uncompressed)
	{
		// Call the appropriate routine for the encoded format
		switch (decoder->codec.encoded_format)
		{
		case ENCODED_FORMAT_YUVA_4444:		// Four planes of YUVA 4:4:4:4
			// Not implemented
			assert(0);
			error = CODEC_ERROR_UNSUPPORTED_FORMAT;
			break;

		case ENCODED_FORMAT_BAYER:			// Bayer encoded data
			// Add new code here for the final steps in decoding the Bayer format
			error = UncompressedSampleFrameBayerToBuffer(decoder, info, frame, output, pitch);
			break;

		case ENCODED_FORMAT_YUV_422:		// Original encoding scheme for YUV 4:2:2 (always v210)
			error = UncompressedSampleFrameYUVToBuffer(decoder, info, frame, output, pitch);//CODEC_ERROR_UNSUPPORTED_FORMAT;
			break;

		case ENCODED_FORMAT_RGB_444:		// Original encoding scheme for RGB 444 (always DPX0)
			error = UncompressedSampleFrameRGBToBuffer(decoder, info, frame, output, pitch);//CODEC_ERROR_UNSUPPORTED_FORMAT;
			break;

		default:
			// Fall through into the old code for reconstructing frames
			error = CODEC_ERROR_UNSUPPORTED_FORMAT;
			break;
		}
	}
	else
	{
		// Call the appropriate routine for the encoded format
		switch (decoder->codec.encoded_format)
		{
		case ENCODED_FORMAT_RGB_444:		// channels = decoder->codec.num_channels; planes of RGB 4:4:4
		case ENCODED_FORMAT_RGBA_4444:		// Four planes of ARGB 4:4:4:4
			error = ReconstructSampleFrameRGB444ToBuffer(decoder, frame, output, pitch);
			break;

		case ENCODED_FORMAT_YUVA_4444:		// Four planes of YUVA 4:4:4:4
			// Not implemented
			assert(0);
			//error = ReconstructSampleFrameYUVA4444ToBuffer(decoder, frame, output, pitch);
			break;

		case ENCODED_FORMAT_BAYER:			// Bayer encoded data
			// Add new code here for the final steps in decoding the Bayer format
			error = ReconstructSampleFrameBayerToBuffer(decoder, info, frame, output, pitch);
			break;

		case ENCODED_FORMAT_YUV_422:		// Original encoding scheme for YUV 4:2:2
			// Add new code here for the final steps in decoding the original YUV 4:2:2 format
			error = ReconstructSampleFrameYUV422ToBuffer(decoder, frame, output, pitch);
			break;

		default:
			// Fall through into the old code for reconstructing frames
			error = CODEC_ERROR_UNSUPPORTED_FORMAT;
			break;
		}
	}

	// Was the newer code able to successfully reconstruct the frame?
	if (error != CODEC_ERROR_UNSUPPORTED_FORMAT)
	{
		// Save the codec error code in the decoder state and return
		decoder->error = error;
		return;
	}

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Decoded scale: %d, decoded width: %d, wavelet width: %d\n", decoded_scale, decoded_width, wavelet_width);
	}
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Decoded width: %d, height: %d, frame width: %d, height: %d, output pitch: %d\n",
				decoded_width, decoded_height, info->width, info->height, pitch);
	}
#endif

#if (0 && DEBUG)
	if (logfile) {
		IMAGE *wavelet = transform[0]->wavelet[frame];
		int band = 0;
		fprintf(logfile, "Luminance wavelet, frame: %d, band: %d\n", frame, band);
		DumpArray16s("Lowpass Band", wavelet->band[band], wavelet->width, wavelet->height, wavelet->pitch, logfile);
	}
#endif

	// Check that the requested frame is large enough to hold the decoded frame
#if (0 && DEBUG)
	//if (! (info->width >= decoded_width))
	{
		if (logfile) {
			//fprintf(logfile, "Requested frame not large enough to hold decoded frame: %d < %d\n", info->width, decoded_width);
			fprintf(logfile, "Output frame width: %d, decoded frame width: %d\n", info->width, decoded_width);
		}
	}
#endif
	assert(info->width >= decoded_width);
	assert((info->height+7)/8 >= (decoded_height+7)/8);

	if (!(info->width >= decoded_width && (info->height+7)/8 >= (decoded_height+7)/8)) {
		decoder->error = CODEC_ERROR_FRAMESIZE;
		return;
	}

#if (0 && DEBUG)
	if (logfile) {
		//SUBIMAGE subimage = SUBIMAGE_UPPER_LEFT(16, 16);
		SUBIMAGE subimage = SUBIMAGE_UPPER_RIGHT(16, 16);

		// Adjust the subimage to be at the middle of the right border
		//subimage.row += wavelet_height/2 - 8;

		DumpBand("SIF Image", wavelet, 0, &subimage, logfile);
	}
#endif

	START(tk_inverse);

	if (resolution == DECODED_RESOLUTION_QUARTER)
	{
		int precision = codec->precision;

		// Reconstruct the frame to quarter resolution
		ReconstructQuarterFrame(decoder, num_channels, frame, output, pitch,
								info, &decoder->scratch, precision);
	}
	else

	// Was the first transform a frame transform (used for interlaced frames)?
	if (!progressive)
	{
		// Can the inverse frame transform and output byte packing be done in one pass?
		if ((resolution == DECODED_RESOLUTION_FULL) &&
			(info->format == DECODED_FORMAT_YUYV || info->format == DECODED_FORMAT_UYVY))
		{
			// Apply the inverse frame transform and pack the results into the output buffer
			int precision = codec->precision;

#if (0 && DEBUG)
			DumpWaveletBandsPGM(wavelet, frame, num_channels);
#endif
#if _INTERLACED_WORKER_THREADS
			StartInterlaceWorkerThreads(decoder);

			//TODO: support new threading
			// Send the upper and lower rows of the transforms to the worker threads
			TransformInverseFrameThreadedToYUV(decoder, frame, num_channels, output, pitch,
											   info, chroma_offset, precision);
#else
			// Transform the wavelets for each channel to the output image (not threaded)
			TransformInverseFrameToYUV(transform_array, frame, num_channels, output, pitch,
									   info, &decoder->scratch, chroma_offset, precision);
#endif
		}

//#if BUILD_PROSPECT
		else if (resolution == DECODED_RESOLUTION_FULL && info->format == DECODED_FORMAT_YR16)
		{
			// Apply the inverse frame transform and output rows of luma and chroma

			//DWORD dwThreadID1;
			//DWORD dwThreadID2;
			//HANDLE thread1;
			//HANDLE thread2;
			int precision = codec->precision;

#if _INTERLACED_WORKER_THREADS
			StartInterlaceWorkerThreads(decoder);

			//TODO: support new threading
			// Send the upper and lower rows of the transforms to the worker threads
			TransformInverseFrameThreadedToRow16u(decoder, frame, num_channels,
												  (PIXEL16U *)output, pitch,
												  info, chroma_offset, precision);
#else
			// Transform the wavelets for each channel to the output image (not threaded)
			TransformInverseFrameToRow16u(decoder, transform_array, frame, num_channels,
										  (PIXEL16U *)output, pitch, info,
										  &decoder->scratch, chroma_offset, precision);
#endif
		}
//#endif
		else
		{
			// Reconstruct the frame as separate planes and combine the planes into a packed output image
			int channel;

			if (resolution == DECODED_RESOLUTION_LOWPASS_ONLY)
			{
				int scale = 13;

				for (channel = 0; channel < num_channels; channel++)
				{
					lowpass_images[channel] = transform_array[channel]->wavelet[5];
					if(lowpass_images[channel] == NULL) // therefore IntreFrame compressed.
					{
						scale = 12;
						lowpass_images[channel] = transform_array[channel]->wavelet[2];
					}
				}

				STOP(tk_inverse);

				CopyLowpass16sToBuffer(decoder, lowpass_images, num_channels, output, pitch, info, chroma_offset,
					scale, decoder->codec.encoded_format, decoder->frame.white_point);

				START(tk_inverse);
			}
			else
			// In SIF resolution, no need to reconstruct the bottom-level wavelet transforms
			// Just copy the lowpass images directly into output frame
			if (resolution == DECODED_RESOLUTION_HALF)
			{
				int precision = codec->precision;

				for (channel = 0; channel < num_channels; channel++)
				{
					lowpass_images[channel] = transform_array[channel]->wavelet[frame];
				}

				STOP(tk_inverse);

				CopyLowpass16sToBuffer(decoder, lowpass_images, num_channels, output, pitch, info, chroma_offset,
					precision, decoder->codec.encoded_format, decoder->frame.white_point);

				START(tk_inverse);
			}

			// In full resolution, reconstruct the frame wavelet and
			// convert the YUYV output to the specified color format
			else
			{
				int precision = codec->precision;
				TransformInverseFrameToBuffer(transform_array, frame, num_channels, output, pitch,
											  info, &decoder->scratch, chroma_offset, precision);
			}
		}
	}
	else	// The first transform was a spatial transform (used for progressive frames)
	{
		// Can the inverse frame transform and output byte packing be done in one pass?

		if ((resolution == DECODED_RESOLUTION_FULL) &&
			(info->format == DECODED_FORMAT_YUYV || info->format == DECODED_FORMAT_UYVY) && // Output YUV
			decoder->thread_cntrl.capabilities & _CPU_FEATURE_SSE2)
		{
			int precision = codec->precision;

			//DWORD dwThreadID1;
			//DWORD dwThreadID2;
			//HANDLE thread1;
			//HANDLE thread2;


			// Apply the inverse frame transform and pack the results into the output buffer
#if _THREADED
			if(decoder->codec.encoded_format == ENCODED_FORMAT_BAYER)
			{
				uint8_t *pixoutput = output;

				if(decoder->use_active_metadata_decoder) //WIP
				{
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
											 pixoutput, pitch,
											 info, chroma_offset, precision,
											 InvertHorizontalStrip16sBayerThruLUT);
				}
				else
				{
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
											 pixoutput, pitch,
											 info, chroma_offset, precision,
											 InvertHorizontalStrip16sToBayerYUV);
				}
			}
			else if((decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
					(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
			{
				TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
											 output, pitch,
											 info, chroma_offset, precision,
											 InvertHorizontalStrip16sRGB2YUV);
			}
			else
			{
				TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
											 output, pitch,
											 info, chroma_offset, precision,
											 InvertHorizontalStrip16sToYUV);
			}
#else
			//TODO : Accelerated BAYER for single thread decoding.

			assert(0);
			// Transform the wavelets for each channel to the output image (not threaded)
			//TransformInverseSpatialToYUV(decoder, transform_array, frame, num_channels, output, pitch, info,
			//							 &decoder->scratch, chroma_offset, precision);
#endif
		}

		else if ((resolution == DECODED_RESOLUTION_FULL) && decoder->codec.encoded_format == ENCODED_FORMAT_BAYER &&
			(info->format == DECODED_FORMAT_RGB24 || info->format == DECODED_FORMAT_RGB32) && // Output RGB
			decoder->thread_cntrl.capabilities & _CPU_FEATURE_SSE2 && decoder->use_active_metadata_decoder)
		{
			int precision = codec->precision;

			//DWORD dwThreadID1;
			//DWORD dwThreadID2;
			//HANDLE thread1;
			//HANDLE thread2;


			// Apply the inverse frame transform and pack the results into the output buffer
#if _THREADED
			{
				uint8_t *pixoutput = output;

				if(info->format == DECODED_FORMAT_RGB24 || info->format == DECODED_FORMAT_RGB32)
				{
					pixoutput += (info->height-1)*pitch;
					pitch = -pitch;
				}

				TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
										 pixoutput, pitch,
										 info, chroma_offset, precision,
										 InvertHorizontalStrip16sBayerThruLUT);
			}
#endif
		}


//#if BUILD_PROSPECT
		else if (resolution == DECODED_RESOLUTION_FULL && info->format == DECODED_FORMAT_YR16)
		{
			// Apply the inverse frame transform and output rows of luma and chroma
			int precision = codec->precision;

#if _THREADED
			TransformInverseSpatialUniversalThreadedToRow16u(decoder, frame, num_channels,
													(uint8_t *)output, pitch,
													info, chroma_offset, precision);
#else
			// Transform the wavelets for each channel to the output image (not threaded)
			TransformInverseSpatialToRow16u(transform_array, frame, num_channels,
											(PIXEL16U *)output, pitch, info,
											&decoder->scratch, chroma_offset, precision);
#endif
		}
//#endif
		else
		{
			// Reconstruct the frame as separate planes and combine the planes into a packed output image
			int channel;

			if (resolution == DECODED_RESOLUTION_LOWPASS_ONLY)
			{
				//int precision = codec->precision;
				int scale = 13;

				//DAN20081203 -- fix for 444 decodes in AE32-bit float
				decoder->frame.white_point = 16;
				//decoder->frame.signed_pixels = 0;

				for (channel = 0; channel < num_channels; channel++)
				{
					lowpass_images[channel] = transform_array[channel]->wavelet[5];
					if(lowpass_images[channel] == NULL) // therefore IntreFrame compressed.
					{
						scale = 12;
						lowpass_images[channel] = transform_array[channel]->wavelet[2];
					}
				}

				STOP(tk_inverse);

				CopyLowpass16sToBuffer(decoder, lowpass_images, num_channels, output, pitch, info, chroma_offset,
					scale, decoder->codec.encoded_format, decoder->frame.white_point);

				START(tk_inverse);
			}
			else
			// In SIF resolution, no need to reconstruct the bottom-level wavelet transforms
			// Just copy the lowpass images directly into output frame
			if (resolution == DECODED_RESOLUTION_HALF || resolution == DECODED_RESOLUTION_HALF_NODEBAYER)// || resolution == DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER)
			{
				int precision = codec->precision;

				for (channel = 0; channel < num_channels; channel++)
				{
					lowpass_images[channel] = transform_array[channel]->wavelet[frame];
#if (0 && DEBUG)
					if (logfile) {
						char label[_MAX_PATH];
						char *format = decoded_format_string[info->format];
						sprintf(label, "Output, channel: %d, format: %s", channel, format);
						DumpImageStatistics(label, lowpass_images[channel], logfile);
					}
#endif
				}

				STOP(tk_inverse);


#if 1 //|| BAYER_SUPPORT
				if(decoder->codec.encoded_format == ENCODED_FORMAT_BAYER)
				{
#if _THREADED
					WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	#if _DELAY_THREAD_START
					if(decoder->worker_thread.pool.thread_count == 0)
					{
						CreateLock(&decoder->worker_thread.lock);
						// Initialize the pool of transform worker threads
						ThreadPoolCreate(&decoder->worker_thread.pool,
										decoder->thread_cntrl.capabilities >> 16/*cpus*/,
										WorkerThreadProc,
										decoder);
					}
	#endif

					// Post a message to the mailbox
					mailbox->output = output;
					mailbox->pitch = pitch;
					memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
					mailbox->jobType = JOB_TYPE_OUTPUT;

					// Set the work count to the number of rows to process
					ThreadPoolSetWorkCount(&decoder->worker_thread.pool, info->height);

					// Start the transform worker threads
					ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

					// Wait for all of the worker threads to finish
					ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

#else
					//unsigned short scanline[4096*3],*sptr;
					//unsigned short scanline2[4096*3],*sptr2;
					unsigned short *scanline,*sptr;
					unsigned short *scanline2,*sptr2;
                    char *buffer = decoder->scratch.free_ptr;
					size_t buffer_size = decoder->scratch.free_size;

					IMAGE *g_image = lowpass_images[0];
					IMAGE *rg_image = lowpass_images[1];
					IMAGE *bg_image = lowpass_images[2];
					IMAGE *gd_image = lowpass_images[3];

					uint8_t *outyuv,*line = output;
					PIXEL *bayer_line, *bayerptr;
					PIXEL *G,*RG,*BG,*GD;
					int x,y;
					int bayer_pitch = info->width*4;
					int format = info->format;
					bool inverted = false;
					int maxbound = 4095; //10-bit source
					int midpoint = 32768>>3;
					int shift = 4;

					if(precision == 12)
					{
						maxbound = 16383;
						midpoint = 32768>>1;
						shift = 2;
					}


					if(buffer_size < info->width * 2 * 3 * 2)
						assert(0); // not enough memory

					if (format == DECODED_FORMAT_RGB24 || format == DECODED_FORMAT_RGB32)
					{
						inverted = true;
						line += (info->height-1)*pitch;
						pitch = -pitch;
					}

					scanline = (unsigned short *)buffer;
					buffer += info->width * 2 * 3;
					scanline2 = (unsigned short *)buffer;

					G = g_image->band[0];
					RG = rg_image->band[0];
					BG = bg_image->band[0];


					for(y=0; y<info->height; y++)
					{
						uint8_t *newline = line;
						PIXEL *newG=G,*newRG=RG,*newBG=BG;
						PIXEL *gptr,*rgptr,*bgptr,*gdptr;
						int r,g,b,rg,bg,y1,y2,u,v;
						int r1,g1,b1;
						int i;

						newline += pitch*y;

						newG += y * (g_image->pitch / sizeof(PIXEL));
						newRG += y * (rg_image->pitch / sizeof(PIXEL));
						newBG += y * (bg_image->pitch / sizeof(PIXEL));

						gptr = newG;
						rgptr = newRG;
						bgptr = newBG;

						sptr = scanline;

						for(x=0; x<info->width; x++)
						{
							g = (*gptr++);
							if(g > maxbound) g = maxbound;
							rg = (*rgptr++);
							bg = (*bgptr++);

							r = (rg<<1) - midpoint + g;
							b = (bg<<1) - midpoint + g;

							if(r > maxbound) r = maxbound;
							if(b > maxbound) b = maxbound;

							if(r < 0) r = 0;
							if(g < 0) g = 0;
							if(b < 0) b = 0;

							*sptr++ = r<<shift;
							*sptr++ = g<<shift;
							*sptr++ = b<<shift;
						}

						{
							int flags = 0;
							int whitebitdepth = 16;

							sptr = scanline;
							if(decoder->apply_color_active_metadata)
								sptr = ApplyActiveMetaData(decoder, info->width, 1, y, scanline, scanline2,
									info->format, &whitebitdepth, &flags);

							ConvertLinesToOutput(decoder, info->width, 1, sptr,
								newline, y, pitch,
								info->format, whitebitdepth, flags);
						}
					}
#endif
				}
				else if((decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
					(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
				{

					IMAGE *g_image = lowpass_images[0];
					IMAGE *rg_image = lowpass_images[1];
					IMAGE *bg_image = lowpass_images[2];

					uint8_t *line = output;
					unsigned char *rgb8;
					PIXEL *G,*RG,*BG;
					int x,y;

					G = g_image->band[0];
					RG = rg_image->band[0];
					BG = bg_image->band[0];

					if(info->format == DECODED_FORMAT_RGB32)
					{
						line = output;
						line += (info->height-1) * pitch;
						for(y=0; y<info->height; y++)
						{
							PIXEL *gptr,*rgptr,*bgptr;
							int r,g,b;
							int i,noisearray[32];
							for(i=0; i<32; i++)
							{
								noisearray[i] = (rand() & 63);
							}

							gptr = G;
							rgptr = RG;
							bgptr = BG;

							rgb8 = (unsigned char *)line;
							for(x=0; x<info->width; x++)
							{
								int rnd = noisearray[x&31];

								g = ((*gptr++) + rnd) >> 6;
								r = ((*rgptr++) + rnd) >> 6;
								b = ((*bgptr++) + rnd) >> 6;

								if(r < 0) r=0; if(r > 255) r=255;
								if(g < 0) g=0; if(g > 255) g=255;
								if(b < 0) b=0; if(b > 255) b=255;

								*rgb8++ = b;
								*rgb8++ = g;
								*rgb8++ = r;
								*rgb8++ = 255;
							}

							line -= pitch;
							G += g_image->pitch / sizeof(PIXEL);
							RG += rg_image->pitch / sizeof(PIXEL);
							BG += bg_image->pitch / sizeof(PIXEL);
						}
					}
					else if(info->format == DECODED_FORMAT_RGB24)
					{
						line = output;
						line += (info->height-1) * pitch;
						for(y=0; y<info->height; y++)
						{
							PIXEL *gptr,*rgptr,*bgptr;
							int r,g,b;
							int i,noisearray[32];
							for(i=0; i<32; i++)
							{
								noisearray[i] = (rand() & 63);
							}

							gptr = G;
							rgptr = RG;
							bgptr = BG;

							rgb8 = (unsigned char *)line;
							for(x=0; x<info->width; x++)
							{
								int rnd = noisearray[x&31];

								g = ((*gptr++) + rnd) >> 6;
								r = ((*rgptr++) + rnd) >> 6;
								b = ((*bgptr++) + rnd) >> 6;

								if(r < 0) r=0; if(r > 255) r=255;
								if(g < 0) g=0; if(g > 255) g=255;
								if(b < 0) b=0; if(b > 255) b=255;

								*rgb8++ = b;
								*rgb8++ = g;
								*rgb8++ = r;
							}

							line -= pitch;
							G += g_image->pitch / sizeof(PIXEL);
							RG += rg_image->pitch / sizeof(PIXEL);
							BG += bg_image->pitch / sizeof(PIXEL);
						}
					}
				}
				else
#endif
				{
					CopyLowpass16sToBuffer(decoder, lowpass_images, num_channels, output, pitch, info, chroma_offset,
						precision, decoder->codec.encoded_format, decoder->frame.white_point);
				}

				START(tk_inverse);

#if (0 && DEBUG)
				if (logfile) {
					char label[_MAX_PATH];
					int width = info->width;
					int height = info->height;

					sprintf(label, "Output");
					DumpBufferStatistics(label, output, width, height, pitch, logfile);
				}
#endif
			}

			// In full resolution, reconstruct the frame wavelet and
			// convert the YUYV output to the specified color format
			else
			{
				// Handle inversion of the output image in this routine
				FRAME_INFO info2;
				int format;
				bool inverted = false;
				int precision = codec->precision;

				memcpy(&info2, info, sizeof(FRAME_INFO));

				format = info2.format;

				if (format == DECODED_FORMAT_RGB24) {
					format = DECODED_FORMAT_RGB24_INVERTED;
					info2.format = format;
					inverted = true;
				}
				else if (format == DECODED_FORMAT_RGB32) {
					format = DECODED_FORMAT_RGB32_INVERTED;
					info2.format = format;
					inverted = true;
				}

				// Have the output location and pitch been inverted?
				if (inverted && pitch > 0) {
					int height = info->height;
					if(resolution == DECODED_RESOLUTION_FULL_DEBAYER)
						height *= 2;
					output += (height - 1) * pitch;		// Start at the bottom row
					pitch = NEG(pitch);					// Negate the pitch to go up
				}

//#if BUILD_PROSPECT
				// Output the frame in V210 foramt?
				if(	(format == DECODED_FORMAT_V210 ||
					format == DECODED_FORMAT_YU64) &&
					decoder->codec.encoded_format != ENCODED_FORMAT_BAYER )
				{
					//char *buffer = decoder->buffer;
					//size_t buffer_size = decoder->buffer_size;
					int precision = codec->precision;

					// The output buffer is an array of 10-bit pixels packed into double words
	#if 0
					TransformInverseSpatialToV210(transform_array, frame, num_channels, output, pitch, &info2,
												  buffer, buffer_size, chroma_offset, decoder->codec.precision);
	#else
					TransformInverseSpatialToV210(transform_array, frame, num_channels, output, pitch,
												  &info2, &decoder->scratch, chroma_offset, precision);
	#endif
				}
				else
//#endif
				// Decoding a full resolution progressive frame to a Bayer output format?
				if(decoder->codec.encoded_format == ENCODED_FORMAT_BAYER)
				{
					//char *buffer = decoder->buffer;
					//size_t buffer_size = decoder->buffer_size;
					int precision = codec->precision;
				//	PIXEL16U *RawBayer16 = (PIXEL16U *)MEMORY_ALIGNED_ALLOC(info->width*decoded_height*4*sizeof(PIXEL), 16);
					if(decoder->RawBayer16 == NULL)
					{
#if _ALLOCATOR
						ALLOCATOR *allocator = decoder->allocator;
						size_t size = info->width*decoded_height*4*sizeof(PIXEL);

						decoder->RawBayer16 =
							(PIXEL16U *)AllocAligned(allocator, size, 16);
#else
						decoder->RawBayer16 =
							(PIXEL16U *)MEMORY_ALIGNED_ALLOC(info->width*decoded_height*4*sizeof(PIXEL), 16);
#endif
						decoder->RawBayerSize = info->width*decoded_height*4*sizeof(PIXEL);
					}
					//TODO: Replace this memory allocation with a scratch buffer allocation
//#ifdef SHARPENING
					if(decoder->RGBFilterBuffer16 == NULL)
					{
#if _ALLOCATOR
						ALLOCATOR *allocator = decoder->allocator;
						size_t size = info->width*decoded_height*4*3*sizeof(PIXEL);

						decoder->RGBFilterBuffer16 =
							(PIXEL16U *)AllocAligned(allocator, size, 16);
#else
						decoder->RGBFilterBuffer16 =
							(PIXEL16U *)MEMORY_ALIGNED_ALLOC(info->width*decoded_height*4*3*sizeof(PIXEL), 16);
#endif
						decoder->RGBFilterBufferSize = info->width*decoded_height*4*3*sizeof(PIXEL);
					}
//#endif

					if(decoder->RawBayer16 == NULL || decoder->RGBFilterBuffer16 == NULL)
					{
						decoder->error = CODEC_ERROR_MEMORY_ALLOC;
						return;
					}


					if(decoder->RawBayer16)
					{
						uint8_t *line;
						PIXEL16U *bayer_line, *bayerptr, *outA16, *outB16;
						PIXEL16U *G,*RG,*BG,*GD;
						int x,y;
						int bayer_pitch = info->width*4;

						//float scale = 256.0;

						//int matrix_non_unity = 0;
						//int wb_non_unity = 0;
						//float curve2lin[2048];
						//float lin2curve[2048+512+2];
#if 0
						static float rgb2yuv[3][4] =
						{
                            {0.183f, 0.614f, 0.062f, 16.0f/256.0f},
                            {-0.101f,-0.338f, 0.439f, 0.5f},
                            {0.439f,-0.399f,-0.040f, 0.5f}
						};

						float mtrx[3][4] =
						{
                            {1.0f,  0,   0,   0},
                            {0,  1.0f,   0,   0},
                            {0,    0, 1.0f,   0}
						};

						float whitebalance[3] = { 1.0f, 1.0f, 1.0f };
#endif
                        
#if 0 // Matrix disabled as it can only be correct handled by the 3D LUT due to the required linear conversions
/*						if(decoder->cfhddata.MagicNumber == CFHDDATA_MAGIC_NUMBER && decoder->cfhddata.version >= 2)
						{
							float fval = 0.0;
							int i;
							for(i=0; i<12; i++)
							{
								mtrx[i>>2][i&3] = fval = decoder->cfhddata.colormatrix[i>>2][i&3];

								if((i>>2) == (i&3))
								{
									if(fval != 1.0)
									{
										matrix_non_unity = 1;
									}
								}
								else
								{
									if(fval != 0.0)
									{
										matrix_non_unity = 1;
									}
								}
							}

							// not active as VFW isn't yet support the 3D LUTs
							if(decoder->cfhddata.version >= 5)
							{
								int j;
								float encode_curvebase = 90.0;
								float decode_curvebase = 90.0;
								int encode_curve_type = decoder->cfhddata.encode_curve >> 16;
								int decode_curve_type = decoder->cfhddata.decode_curve >> 16;


								if(decoder->cfhddata.user_white_balance[0] > 0.0)
								{
									wb_non_unity = 1;

									whitebalance[0] = decoder->cfhddata.user_white_balance[0];
									whitebalance[1] = (decoder->cfhddata.user_white_balance[1]+decoder->cfhddata.user_white_balance[2])/2.0;
									whitebalance[2] = decoder->cfhddata.user_white_balance[3];
								}


								if(encode_curve_type) //1 or 2
									encode_curvebase = (float)((decoder->cfhddata.encode_curve >> 8) & 0xff) / (float)(decoder->cfhddata.encode_curve & 0xff);
								else
								{
									encode_curve_type = 1;
									encode_curvebase = 90.0;
								}

								if(decode_curve_type) //1 or 2
									decode_curvebase = (float)((decoder->cfhddata.decode_curve >> 8) & 0xff) / (float)(decoder->cfhddata.decode_curve & 0xff);
								else
								{
									decode_curve_type = 1;
									decode_curvebase = 90.0;
								}


								for(j=0; j<2048; j++)
								{
									if(encode_curve_type == 1)
										curve2lin[j] = CURVE_LOG2LIN((float)j/2047.0,encode_curvebase);
									else
										curve2lin[j] = CURVE_GAM2LIN((float)j/2047.0,encode_curvebase);

								}

								for(j=-512; j<=2048; j++) // -1 to +4
								{
									if(encode_curve_type == CURVE_TYPE_LOG)
										lin2curve[j+512] = CURVE_LIN2LOG((float)j/512.0,encode_curvebase);
									else
										lin2curve[j+512] = CURVE_LIN2GAM((float)j/512.0,encode_curvebase);

								}
							}
						}*/
#endif


#if _THREADED
						TransformInverseSpatialUniversalThreadedToRow16u(decoder, frame, num_channels,
											(uint8_t *)decoder->RawBayer16, bayer_pitch*sizeof(PIXEL),
											info, chroma_offset, precision);
#else
						// Decode that last transform to rows of Bayer data (one row per channel)
						TransformInverseSpatialToRow16u(transform_array, frame, num_channels,
											decoder->RawBayer16, bayer_pitch*sizeof(PIXEL), info,
											&decoder->scratch, chroma_offset, precision);
#endif

						if(resolution == DECODED_RESOLUTION_FULL_DEBAYER &&
							(info->format < DECODED_FORMAT_BYR1 || info->format > DECODED_FORMAT_BYR4))
						{

#if _THREADED				//DemosaicRAW
							WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;
	#if _DELAY_THREAD_START
							if(decoder->worker_thread.pool.thread_count == 0)
							{
								CreateLock(&decoder->worker_thread.lock);
								// Initialize the pool of transform worker threads
								ThreadPoolCreate(&decoder->worker_thread.pool,
												decoder->thread_cntrl.capabilities >> 16/*cpus*/,
												WorkerThreadProc,
												decoder);
							}
	#endif
							// Post a message to the mailbox
							mailbox->output = output;
							mailbox->pitch = pitch;
							memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
							mailbox->jobType = JOB_TYPE_OUTPUT;

							// Set the work count to the number of rows to process
							ThreadPoolSetWorkCount(&decoder->worker_thread.pool, info->height);

							// Start the transform worker threads
							ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

							// Wait for all of the worker threads to finish
							ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

#else
							assert(0) // old code disabled
				/*			int bayer_format = decoder->cfhddata.bayer_format;
							unsigned char *outA8, *outB8;
							unsigned short *lineStartA16, *lineStartB16;
							unsigned short *lineA16, *lineB16;

						//	int stats1=0, stats2=0, statsd=0;
						//	double dstats1=0, dstats2=0, dstatsd=0;

							line = output;
							bayer_line = decoder->RawBayer16;
							for(y=0; y<info->height+DEMOSAIC_DELAYLINES; y++)
							{
								bayer_line = decoder->RawBayer16;
								bayer_line += bayer_pitch * y;

								if(y<info->height)
								{
									ColorDifference2Bayer(info->width,
										bayer_line, bayer_pitch, bayer_format);
								}

								if(y>=3+DEMOSAIC_DELAYLINES && y<info->height-3+DEMOSAIC_DELAYLINES) //middle scanline
								{
									unsigned short *delayptr = decoder->RawBayer16;
									delayptr += bayer_pitch * (y-DEMOSAIC_DELAYLINES);

									BayerRippleFilter(info->width,
										delayptr, bayer_pitch, bayer_format, decoder->RawBayer16);
								}

								if(y>=DEMOSAIC_DELAYLINES)
								{
									int delay_y = y - DEMOSAIC_DELAYLINES;
									unsigned short *sptr, scanline[8192*3];
									outA8 = line;
									line += pitch;
									outB8 = line;
									line += pitch;

									sptr = scanline;

									DebayerLine(info->width*2, info->height*2, delay_y*2,
										decoder->RawBayer16,  bayer_format, sptr, sharpening);

									for(x=0; x<info->width*2; x++)
									{
										outA8[2] = *sptr++>>8;
										outA8[1] = *sptr++>>8;
										outA8[0] = *sptr++>>8;
										outA8+=3;
									}
									for(x=0; x<info->width*2; x++)
									{
										outB8[2] = *sptr++>>8;
										outB8[1] = *sptr++>>8;
										outB8[0] = *sptr++>>8;
										outB8+=3;
									}
								}
							}*/
#endif // _THREADED
						}
						else


						if(format == DECODED_FORMAT_BYR2 || format == DECODED_FORMAT_BYR4)
						{
#if _THREADED
							WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	#if _DELAY_THREAD_START
							if(decoder->worker_thread.pool.thread_count == 0)
							{
								CreateLock(&decoder->worker_thread.lock);
								// Initialize the pool of transform worker threads
								ThreadPoolCreate(&decoder->worker_thread.pool,
												decoder->thread_cntrl.capabilities >> 16/*cpus*/,
												WorkerThreadProc,
												decoder);
							}
	#endif
							// Post a message to the mailbox
							mailbox->output = output;
							mailbox->pitch = pitch;
							memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
							mailbox->jobType = JOB_TYPE_OUTPUT;

							// Set the work count to the number of rows to process
							ThreadPoolSetWorkCount(&decoder->worker_thread.pool, info->height);

							// Start the transform worker threads
							ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

							// Wait for all of the worker threads to finish
							ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
#else
							assert(0) // old code disabled
						/*	{
								int bayer_format = decoder->cfhddata.bayer_format;
							//	int stats1=0, stats2=0, statsd=0;
							//	double dstats1=0, dstats2=0, dstatsd=0;
								line = output;
								bayer_line = decoder->RawBayer16;

								for(y=0; y<info->height; y++)
								{
									outA16 = (PIXEL16U *)line;
									line += pitch;
									outB16 = (PIXEL16U *)line;
									line += pitch;

									bayerptr = bayer_line;
									G = bayerptr;
									RG = G + bayer_pitch/4;
									BG = RG + bayer_pitch/4;
									GD = BG + bayer_pitch/4;
									for(x=0; x<info->width; x++)
									{
										int r,g,b,rg,bg,gd,g1,g2,y1,y2,u,v,dither;


										g = (*G++);
										rg = (*RG++);
										bg = (*BG++);
										gd = (*GD++) - 32768;

										r = ((rg - 32768)<<1) + g;
										b = ((bg - 32768)<<1) + g;
										g1 = g + gd;
										g2 = g - gd; //TODO:  Is there a DC offset to gd (causes a check in output )

									//	stats1+=g1;
									//	stats2+=g2;
									//	statsd+=gd;

										if(r < 0) r = 0;
										if(g1 < 0) g1 = 0;
										if(g2 < 0) g2 = 0;
										if(b < 0) b = 0;

										if(r > 0xffff) r = 0xffff;
										if(g1 > 0xffff) g1 = 0xffff;
										if(g2 > 0xffff) g2 = 0xffff;
										if(b > 0xffff) b = 0xffff;

										switch(bayer_format)
										{
										case BAYER_FORMAT_RED_GRN: //Red-grn phase
											*outA16++ = r;
											*outA16++ = g1;
											*outB16++ = g2;
											*outB16++ = b;
											break;
										case BAYER_FORMAT_GRN_RED:// grn-red
											*outA16++ = g1;
											*outA16++ = r;
											*outB16++ = b;
											*outB16++ = g2;
											break;
										case BAYER_FORMAT_GRN_BLU:
											*outA16++ = g1;
											*outA16++ = b;
											*outB16++ = r;
											*outB16++ = g2;
											break;
										case BAYER_FORMAT_BLU_GRN:
											*outA16++ = b;
											*outA16++ = g1;
											*outB16++ = g2;
											*outB16++ = r;
											break;
										}
									}

									bayer_line += bayer_pitch;
								}


								if(decoder->flags & DECODER_FLAGS_HIGH_QUALITY)
								{
									int bayer_format = decoder->cfhddata.bayer_format;

									for(y=2; y<info->height-3; y++)
									{
										int offset = pitch>>1;

										line = output; //0
										line += pitch * y * 2;

										// If on a red line, move to a blue line
										if(bayer_format == BAYER_FORMAT_GRN_RED || bayer_format == BAYER_FORMAT_RED_GRN)
											line -= pitch;


										{
											int offset = pitch>>1;
											outA16 = (PIXEL16U *)line;

											outA16++; //g //for BAYER_FORMAT_RED_GRN input
											outA16++; //b

											outA16++; //g
											outA16++; //b

											//point to green pixel with *outA16
											if(bayer_format == BAYER_FORMAT_GRN_RED || bayer_format == BAYER_FORMAT_GRN_BLU)
												outA16++;

											for(x=2; x<info->width-2; x++)
											{
												int mn,mx,g;
												int range = 8*256; //1<<11
												int shift = 11;
												int delta;
												int alpha;

												g =  *outA16;

												// lines below do not need to be tested for a corrected value
												mn = mx = outA16[offset+1];
												if(mn > outA16[offset-1]) mn = outA16[offset-1];
												if(mx < outA16[offset-1]) mx = outA16[offset-1];
												if((outA16[-offset-1] & 1)==0)

												{
													if(mn > outA16[-offset-1]) mn = outA16[-offset-1];
													if(mx < outA16[-offset-1]) mx = outA16[-offset-1];
												}
												if((outA16[-offset+1] & 1)==0)
												{
													if(mn > outA16[-offset+1]) mn = outA16[-offset+1];
													if(mx < outA16[-offset+1]) mx = outA16[-offset+1];
												}

												delta = mx - mn;

												if(delta < range && ((mn-range < g && g < mn) || (mx+range > g && g > mx)))
												{
													int gmn,gmx;

													gmn = gmx = g;
													if((outA16[-2*offset-2] & 1)==0)
													{
														if(gmn > outA16[-2*offset-2]) gmn = outA16[-2*offset-2];
														if(gmx < outA16[-2*offset-2]) gmx = outA16[-2*offset-2];
													}
													if((outA16[-2*offset] & 1)==0)
													{
														if(gmn > outA16[-2*offset]) gmn = outA16[-2*offset];
														if(gmx < outA16[-2*offset]) gmx = outA16[-2*offset];
													}
													if((outA16[-2*offset+2] & 1)==0)
													{
														if(gmn > outA16[-2*offset+2]) gmn = outA16[-2*offset+2];
														if(gmx < outA16[-2*offset+2]) gmx = outA16[-2*offset+2];
													}
													if((outA16[-2] & 1)==0)
													{
														if(gmn > outA16[-2]) gmn = outA16[-2];
														if(gmx < outA16[-2]) gmx = outA16[-2];
													}
													// lines below do not need to be tested for a corrected value
													if(gmn > outA16[2*offset-2]) gmn = outA16[2*offset-2];
													if(gmx < outA16[2*offset-2]) gmx = outA16[2*offset-2];
													if(gmn > outA16[2*offset]) gmn = outA16[2*offset];
													if(gmx < outA16[2*offset]) gmx = outA16[2*offset];
													if(gmn > outA16[2*offset+2]) gmn = outA16[2*offset+2];
													if(gmx < outA16[2*offset+2]) gmx = outA16[2*offset+2];
													if(gmn > outA16[2]) gmn = outA16[2];
													if(gmx < outA16[2]) gmx = outA16[2];


													if((gmx - gmn) < range)
													{
														alpha = range;//delta;

														if(g > mx)
														{
															alpha *= (g-mx); //max range
															alpha >>= shift;
														}
														else // g < mn
														{
															alpha *= (mn-g); //max range
															alpha >>= shift;
														}

														alpha *= alpha;
														alpha >>= shift;


													//	avg = (outA16[-offset-1] + outA16[offset-1] + outA16[-offset+1] + outA16[offset+1] + 2) >> 2;
													//	*outA16 = avg; //good
													//	*outA16 = mn; //spotty

														if( (abs(outA16[offset] - outA16[-offset]) < range)
															&& ((abs(outA16[1] - outA16[-1]) < range)))
														{
															int val = (alpha*g + (range - alpha)*((mn+mx)>>1))>>shift;
															if(val > 0xffff) val = 0xffff;
															if(val < 0) val = 0;
															val |= 1;
															*outA16 = val;

														//	*outA16 = ((mn+mx)>>1) | 1; // like avg but less compute
														}
													}
												}

												outA16++; //g
												outA16++; //b
											}
										}
									}
								}
							}*/
#endif
						}

						// Pack the rows of Bayer data (full resolution progressive) into BYR3 format?
						else if (format == DECODED_FORMAT_BYR3)
						{
							PIXEL16U *outR, *outG1, *outG2, *outB;
						//	int stats1=0, stats2=0, statsd=0;
						//	double dstats1=0, dstats2=0, dstatsd=0;

						//	#pragma omp parallel for
							for(y=0; y<info->height; y++)
							{

								uint8_t *line = output;
								PIXEL *bayerptr = (PIXEL *)decoder->RawBayer16;

								line += pitch*2*y;
								bayerptr += bayer_pitch * y;

								outR = (PIXEL16U *)line;
								outG1 = outR + (pitch/4);
								outG2 = outR + (pitch/4)*2;
								outB = outR + (pitch/4)*3;

								G = (PIXEL16U *)bayerptr;
								RG = G + bayer_pitch/4;
								BG = RG + bayer_pitch/4;
								GD = BG + bayer_pitch/4;

								// Pack the rows of Bayer components into the BYR3 pattern
#if (1 && XMMOPT)
								{

									__m128i *G_128 = (__m128i *)G;
									__m128i *RG_128 = (__m128i *)RG;
									__m128i *BG_128 = (__m128i *)BG;
									__m128i *GD_128 = (__m128i *)GD;

									__m128i *outR_128 = (__m128i *)outR;
									__m128i *outG1_128 = (__m128i *)outG1;
									__m128i *outG2_128 = (__m128i *)outG2;
									__m128i *outB_128 = (__m128i *)outB;

									__m128i limiter = _mm_set1_epi16(0x7fff - 0x3ff);
									__m128i midpoint1 = _mm_set1_epi16(32768>>6);
									__m128i midpoint2 = _mm_set1_epi16(32768>>5);

									int column_step = 8;
									int post_column = (info->width) - ((info->width) % column_step);

									for (x=0; x < post_column; x += column_step)
									{
										__m128i r_128;
										__m128i g1_128;
										__m128i g2_128;
										__m128i b_128;

										__m128i g_128;
										__m128i rg_128;
										__m128i bg_128;
										__m128i gd_128;

										g_128 = _mm_load_si128(G_128++);
										rg_128 = _mm_load_si128(RG_128++);
										bg_128 = _mm_load_si128(BG_128++);
										gd_128 = _mm_load_si128(GD_128++);


										g_128 = _mm_srli_epi16(g_128, 6);
										rg_128 = _mm_srli_epi16(rg_128, 5);
										bg_128 = _mm_srli_epi16(bg_128, 5);
										gd_128 = _mm_srli_epi16(gd_128, 6);
										gd_128 = _mm_subs_epi16(gd_128, midpoint1);

										rg_128 = _mm_subs_epi16(rg_128, midpoint2);
										bg_128 = _mm_subs_epi16(bg_128, midpoint2);
										r_128 = _mm_adds_epi16(rg_128, g_128);
										b_128 = _mm_adds_epi16(bg_128, g_128);
										g1_128 = _mm_adds_epi16(g_128, gd_128);
										g2_128 = _mm_subs_epi16(g_128, gd_128);

										r_128 = _mm_adds_epi16(r_128, limiter);
										r_128 = _mm_subs_epu16(r_128, limiter);
										g1_128 = _mm_adds_epi16(g1_128, limiter);
										g1_128 = _mm_subs_epu16(g1_128, limiter);
										g2_128 = _mm_adds_epi16(g2_128, limiter);
										g2_128 = _mm_subs_epu16(g2_128, limiter);
										b_128 = _mm_adds_epi16(b_128, limiter);
										b_128 = _mm_subs_epu16(b_128, limiter);


										_mm_store_si128(outR_128++, r_128);
										_mm_store_si128(outG1_128++, g1_128);
										_mm_store_si128(outG2_128++, g2_128);
										_mm_store_si128(outB_128++, b_128);
									}

									G  = (PIXEL16U *)G_128;
									RG = (PIXEL16U *)RG_128;
									BG = (PIXEL16U *)BG_128;
									GD = (PIXEL16U *)GD_128;

									outR = (PIXEL16U *)outR_128;
									outG1 = (PIXEL16U *)outG1_128;
									outG2 = (PIXEL16U *)outG2_128;
									outB = (PIXEL16U *)outB_128;

								}
#endif

								for(; x<info->width; x++)
								{
									int r,g,b,rg,bg,gd,g1,g2;


									g = (*G++);
									rg = (*RG++);
									bg = (*BG++);
									gd = (*GD++) - 32768;

									r = ((rg - 32768)<<1) + g;
									b = ((bg - 32768)<<1) + g;
									g1 = g + gd;
									g2 = g - gd; //TODO:  Is there a DC offset to gd (causes a check in output )

									if(r < 0) r = 0;
									if(g1 < 0) g1 = 0;
									if(g2 < 0) g2 = 0;
									if(b < 0) b = 0;

									if(r > 0xffff) r = 0xffff;
									if(g1 > 0xffff) g1 = 0xffff;
									if(g2 > 0xffff) g2 = 0xffff;
									if(b > 0xffff) b = 0xffff;

									//Red-grn phase
									*outR++ = r>>6;
									*outG1++ = g1>>6;
									*outG2++ = g2>>6;
									*outB++ = b>>6;
								}
							}
						}

						// Pack the rows of Bayer data (full resolution progressive) into BYR4 format?
						else if (format == DECODED_FORMAT_BYR4)
						{
							int bayer_format = decoder->cfhddata.bayer_format;
							line = output;
							bayer_line = decoder->RawBayer16;

							for(y=0; y<info->height; y++)
							{
								outA16 = (PIXEL16U *)line;
								line += pitch;
								outB16 = (PIXEL16U *)line;
								line += pitch;

								bayerptr = bayer_line;
								G = bayerptr;
								RG = G + bayer_pitch/4;
								BG = RG + bayer_pitch/4;
								GD = BG + bayer_pitch/4;
								for(x=0; x<info->width; x++)
								{
									//int r,g,b,rg,bg,gd,g1,g2,y1,y2,u,v,dither;
									int32_t r, g, b, rg, bg, gd, g1, g2;

									// The output of the inverse transform is unsigned 16-bit integers
									const int midpoint = 32768;

									g = (*G++);
									rg = (*RG++);
									bg = (*BG++);
									gd = (*GD++) - midpoint;

									r = ((rg - midpoint)<<1) + g;
									b = ((bg - midpoint)<<1) + g;
									g1 = g + gd;
									g2 = g - gd;

									r = SATURATE_16U(r);
									g1 = SATURATE_16U(g1);
									g2 = SATURATE_16U(g2);
									b = SATURATE_16U(b);

								//	stats1+=g1;
								//	stats2+=g2;
								//	statsd+=gd;

									switch(bayer_format)
									{
									case BAYER_FORMAT_RED_GRN: //Red-grn phase
										*outA16++ = r;
										*outA16++ = g1;
										*outB16++ = g2;
										*outB16++ = b;
										break;
									case BAYER_FORMAT_GRN_RED:// grn-red
										*outA16++ = g1;
										*outA16++ = r;
										*outB16++ = b;
										*outB16++ = g2;
										break;
									case BAYER_FORMAT_GRN_BLU:
										*outA16++ = g1;
										*outA16++ = b;
										*outB16++ = r;
										*outB16++ = g2;
										break;
									case BAYER_FORMAT_BLU_GRN:
										*outA16++ = b;
										*outA16++ = g1;
										*outB16++ = g2;
										*outB16++ = r;
										break;

									default:
										// Unsupported Bayer format
										assert(0);
										*outA16++ = 0;
										*outA16++ = 0;
										*outB16++ = 0;
										*outB16++ = 0;
										break;
									}
								}

								bayer_line += bayer_pitch;
							}


							if(decoder->flags & DECODER_FLAGS_HIGH_QUALITY)
							{
								for(y=2; y<info->height-3; y++)
								{
									//int offset = pitch>>1;

									line = output; //0
									line += pitch * y * 2;

									// If on a red line, move to a blue line
									if(bayer_format == BAYER_FORMAT_GRN_RED || bayer_format == BAYER_FORMAT_RED_GRN)
										line -= pitch;


									{
										int offset = pitch>>1;
										outA16 = (PIXEL16U *)line;

										outA16++; //g //for BAYER_FORMAT_RED_GRN input
										outA16++; //b

										outA16++; //g
										outA16++; //b

										//point to green pixel with *outA16
										if(bayer_format == BAYER_FORMAT_GRN_RED || bayer_format == BAYER_FORMAT_GRN_BLU)
											outA16++;



										for(x=2; x<info->width-2; x++)
										{
											int mn,mx,g;
											int range = 8*256; //1<<11
											int shift = 11;
											int delta;
											int alpha;

											g =  *outA16;

											// lines below do not need to be tested for a corrected value
											mn = mx = outA16[offset+1];
											if(mn > outA16[offset-1]) mn = outA16[offset-1];
											if(mx < outA16[offset-1]) mx = outA16[offset-1];
											if((outA16[-offset-1] & 1)==0)
											{
												if(mn > outA16[-offset-1]) mn = outA16[-offset-1];
												if(mx < outA16[-offset-1]) mx = outA16[-offset-1];
											}
											if((outA16[-offset+1] & 1)==0)
											{
												if(mn > outA16[-offset+1]) mn = outA16[-offset+1];
												if(mx < outA16[-offset+1]) mx = outA16[-offset+1];
											}

											delta = mx - mn;

											if(delta < range && ((mn-range < g && g < mn) || (mx+range > g && g > mx)))
											{
												int gmn,gmx;

												gmn = gmx = g;
												if((outA16[-2*offset-2] & 1)==0)
												{
													if(gmn > outA16[-2*offset-2]) gmn = outA16[-2*offset-2];
													if(gmx < outA16[-2*offset-2]) gmx = outA16[-2*offset-2];
												}
												if((outA16[-2*offset] & 1)==0)
												{
													if(gmn > outA16[-2*offset]) gmn = outA16[-2*offset];
													if(gmx < outA16[-2*offset]) gmx = outA16[-2*offset];
												}
												if((outA16[-2*offset+2] & 1)==0)
												{
													if(gmn > outA16[-2*offset+2]) gmn = outA16[-2*offset+2];
													if(gmx < outA16[-2*offset+2]) gmx = outA16[-2*offset+2];
												}
												if((outA16[-2] & 1)==0)
												{
													if(gmn > outA16[-2]) gmn = outA16[-2];
													if(gmx < outA16[-2]) gmx = outA16[-2];
												}
												// lines below do not need to be tested for a corrected value
												if(gmn > outA16[2*offset-2]) gmn = outA16[2*offset-2];
												if(gmx < outA16[2*offset-2]) gmx = outA16[2*offset-2];
												if(gmn > outA16[2*offset]) gmn = outA16[2*offset];
												if(gmx < outA16[2*offset]) gmx = outA16[2*offset];
												if(gmn > outA16[2*offset+2]) gmn = outA16[2*offset+2];
												if(gmx < outA16[2*offset+2]) gmx = outA16[2*offset+2];
												if(gmn > outA16[2]) gmn = outA16[2];
												if(gmx < outA16[2]) gmx = outA16[2];


												if((gmx - gmn) < range)
												{
													alpha = range;//delta;

													if(g > mx)
													{
														alpha *= (g-mx); //max range
														alpha >>= shift;
													}
													else // g < mn
													{
														alpha *= (mn-g); //max range
														alpha >>= shift;
													}

													alpha *= alpha;
													alpha >>= shift;


												//	avg = (outA16[-offset-1] + outA16[offset-1] + outA16[-offset+1] + outA16[offset+1] + 2) >> 2;
												//	*outA16 = avg; //good
												//	*outA16 = mn; //spotty

													if( (abs(outA16[offset] - outA16[-offset]) < range)
														&& ((abs(outA16[1] - outA16[-1]) < range)))
													{
														int val = (alpha*g + (range - alpha)*((mn+mx)>>1))>>shift;
														if(val > 0xffff) val = 0xffff;
														if(val < 0) val = 0;
														val |= 1;
														*outA16 = val;

													//	*outA16 = ((mn+mx)>>1) | 1; // like avg but less compute
													}
												}
											}

											outA16++; //g
											outA16++; //b
										}
									}
								}


							}
							// Linear restore
							{
								unsigned short *buff = (unsigned short *)output;
								//static int pos = 0;
								for(y=0; y<info->height*2; y++)
								{
									for(x=0; x<info->width*2; x++)
									{
										float val = (float)buff[y*info->width*2 + x]/65535.0f;
										float encode_curvebase = 90.0;
										int encode_curve_type = CURVE_TYPE_LOG;
										int encode_curve_neg;

										if((decoder->cfhddata.encode_curve)>>16) //1 or 2
										{
											encode_curve_type = (decoder->cfhddata.encode_curve)>>16;
											if(encode_curve_type & CURVE_TYPE_EXTENDED)
												encode_curvebase = (float)(decoder->cfhddata.encode_curve & 0xffff); // use all 16-bits for larger log bases
											else
												encode_curvebase = (float)((decoder->cfhddata.encode_curve >> 8) & 0xff) / (float)(decoder->cfhddata.encode_curve & 0xff);			
										}

										if(encode_curvebase == 1.0 && encode_curve_type <= CURVE_TYPE_LINEAR)
											encode_curve_type = CURVE_TYPE_LINEAR;
										
										encode_curve_neg = encode_curve_type & CURVE_TYPE_NEGATIVE;

										switch(encode_curve_type & CURVE_TYPE_MASK)
										{
										case CURVE_TYPE_LOG:
											val = CURVE_LOG2LIN(val,encode_curvebase);
											break;

										case CURVE_TYPE_GAMMA:
											val = CURVE_GAM2LIN(val,encode_curvebase);
											break;

										case CURVE_TYPE_CINEON:
											val = CURVE_CINEON2LIN(val,encode_curvebase);
											break;

										case CURVE_TYPE_CINE985:
											val = CURVE_CINE9852LIN(val,encode_curvebase);
											break;

										case CURVE_TYPE_PARA:
											val = CURVE_PARA2LIN(val,(int)((decoder->cfhddata.encode_curve >> 8) & 0xff), (int)(decoder->cfhddata.encode_curve & 0xff));
											break;

										case CURVE_TYPE_CSTYLE:
											val = CURVE_CSTYLE2LIN((float)val,(int)((decoder->cfhddata.encode_curve >> 8) & 0xff));
											break;
											
										case CURVE_TYPE_SLOG:
											val = CURVE_SLOG2LIN((float)val);
											break;

										case CURVE_TYPE_LOGC:
											val = CURVE_LOGC2LIN((float)val);
											break;

										case CURVE_TYPE_LINEAR:
										default:
											break;
										}

										buff[y*info->width*2 + x] = (int)(val*4095.0);
									}
								}
							}
						}
						else
						{
#if _THREADED
							WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	#if _DELAY_THREAD_START
							if(decoder->worker_thread.pool.thread_count == 0)
							{
								CreateLock(&decoder->worker_thread.lock);
								// Initialize the pool of transform worker threads
								ThreadPoolCreate(&decoder->worker_thread.pool,
												decoder->thread_cntrl.capabilities >> 16/*cpus*/,
												WorkerThreadProc,
												decoder);
							}
	#endif
							// Post a message to the mailbox
							mailbox->output = output;
							mailbox->pitch = pitch;
							memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
							mailbox->jobType = JOB_TYPE_OUTPUT;

							// Set the work count to the number of rows to process
							ThreadPoolSetWorkCount(&decoder->worker_thread.pool, info->height);

							// Start the transform worker threads
							ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

							// Wait for all of the worker threads to finish
							ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
#else
							//unsigned short scanline[8192*3],*sptr;
							//unsigned short scanline2[8192*3],*sptr2;
							unsigned short *scanline,*sptr;
							unsigned short *scanline2,*sptr2;
						    char *buffer = decoder->scratch.free_ptr;
							size_t buffer_size = decoder->scratch.free_size;

							uint8_t *outyuv,*line = output;
							PIXEL *bayerptr;
							int x,y;

							if(buffer_size < info->width * 2 * 3 * 2)
								assert(0); // not enough memory

							scanline = (unsigned short *)buffer;
							buffer += info->width * 2 * 3;
							scanline2 = (unsigned short *)buffer;

							line = output;
							bayer_line = decoder->RawBayer16;

							for(y=0; y<info->height; y++)
							{
								int r,g,b,rg,bg,y1,y2,u,v;
								int r1,g1,b1;
								int i;

								__m128i gggggggg,ggggggg2,rgrgrgrg,bgbgbgbg;
								__m128i rrrrrrrr,bbbbbbbb;
								__m128i mid8192 = _mm_set1_epi16(8192);
								__m128i mid16384 = _mm_set1_epi16(16384);
								__m128i mid32768 = _mm_set1_epi16(32768);

								__m128i overflowprotectRGB_epi16 = _mm_set1_epi16(0x7fff-0x3fff);
								int sse2width = info->width & 0xfff8;

								bayerptr = bayer_line;
								G = bayerptr;
								RG = G + bayer_pitch/4;
								BG = RG + bayer_pitch/4;
								GD = BG + bayer_pitch/4;

								sptr = scanline;



								x = 0;
								for(; x<sse2width; x+=8)
								{
									gggggggg = _mm_loadu_si128((__m128i *)G); G+=8;
									rgrgrgrg = _mm_loadu_si128((__m128i *)RG); RG+=8;
									bgbgbgbg = _mm_loadu_si128((__m128i *)BG); BG+=8;


									ggggggg2 = _mm_srli_epi16(gggggggg, 2);// 0-16383 14bit unsigned
									rgrgrgrg = _mm_srli_epi16(rgrgrgrg, 2);// 14bit unsigned
									bgbgbgbg = _mm_srli_epi16(bgbgbgbg, 2);// 14bit unsigned

									rrrrrrrr = _mm_subs_epi16(rgrgrgrg, mid8192);// -8191 to 8191 14bit signed
									rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 1);		// -16382 to 16382 15bit signed
									rrrrrrrr = _mm_adds_epi16(rrrrrrrr, ggggggg2); // -16382 to 32767

									bbbbbbbb = _mm_subs_epi16(bgbgbgbg, mid8192);// -8191 to 8191 14bit signed
									bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 1);		// -16382 to 16382 15bit signed
									bbbbbbbb = _mm_adds_epi16(bbbbbbbb, ggggggg2); // -16382 to 32767

									//limit to 0 to 16383
									rrrrrrrr = _mm_adds_epi16(rrrrrrrr, overflowprotectRGB_epi16);
									rrrrrrrr = _mm_subs_epu16(rrrrrrrr, overflowprotectRGB_epi16);

									//limit to 0 to 16383
									bbbbbbbb = _mm_adds_epi16(bbbbbbbb, overflowprotectRGB_epi16);
									bbbbbbbb = _mm_subs_epu16(bbbbbbbb, overflowprotectRGB_epi16);

									rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 2); // restore to 0 to 65535
									bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 2); // restore to 0 to 65535


									*sptr++ = _mm_extract_epi16(rrrrrrrr, 0);
									*sptr++ = _mm_extract_epi16(gggggggg, 0);
									*sptr++ = _mm_extract_epi16(bbbbbbbb, 0);

									*sptr++ = _mm_extract_epi16(rrrrrrrr, 1);
									*sptr++ = _mm_extract_epi16(gggggggg, 1);
									*sptr++ = _mm_extract_epi16(bbbbbbbb, 1);

									*sptr++ = _mm_extract_epi16(rrrrrrrr, 2);
									*sptr++ = _mm_extract_epi16(gggggggg, 2);
									*sptr++ = _mm_extract_epi16(bbbbbbbb, 2);

									*sptr++ = _mm_extract_epi16(rrrrrrrr, 3);
									*sptr++ = _mm_extract_epi16(gggggggg, 3);
									*sptr++ = _mm_extract_epi16(bbbbbbbb, 3);

									*sptr++ = _mm_extract_epi16(rrrrrrrr, 4);
									*sptr++ = _mm_extract_epi16(gggggggg, 4);
									*sptr++ = _mm_extract_epi16(bbbbbbbb, 4);

									*sptr++ = _mm_extract_epi16(rrrrrrrr, 5);
									*sptr++ = _mm_extract_epi16(gggggggg, 5);
									*sptr++ = _mm_extract_epi16(bbbbbbbb, 5);

									*sptr++ = _mm_extract_epi16(rrrrrrrr, 6);
									*sptr++ = _mm_extract_epi16(gggggggg, 6);
									*sptr++ = _mm_extract_epi16(bbbbbbbb, 6);

									*sptr++ = _mm_extract_epi16(rrrrrrrr, 7);
									*sptr++ = _mm_extract_epi16(gggggggg, 7);
									*sptr++ = _mm_extract_epi16(bbbbbbbb, 7);
								}

								for(; x<info->width; x++)
								{
									g = (*G++);
									rg = (*RG++);
									bg = (*BG++);

									r = ((rg - 32768)<<1) + g;
									b = ((bg - 32768)<<1) + g;

									if(r < 0) r = 0;  if(r > 0xffff) r = 0xffff;
									if(g < 0) g = 0;  if(g > 0xffff) g = 0xffff;
									if(b < 0) b = 0;  if(b > 0xffff) b = 0xffff;

									*sptr++ = r;
									*sptr++ = g;
									*sptr++ = b;
								}

								{
									int flags = 0;
									int whitebitdepth = 16;

									sptr = scanline;
									if(decoder->apply_color_active_metadata)
										sptr = ApplyActiveMetaData(decoder, info->width, 1, y, scanline, scanline2,
											info->format, &whitebitdepth, &flags);

									ConvertLinesToOutput(decoder, info->width, 1, sptr, line, pitch,
										info->format, whitebitdepth, flags);
								}

								line += pitch;
								bayer_line += bayer_pitch;
							}
#endif
						}
						/* // switch to using the ApplyActiveMetaData() and ConvertLinesToOutput() calls - DAN20071201

						// Pack the rows of Bayer data (full resolution progressive) into BYR2 format?
						else if (format == DECODED_FORMAT_YUYV)
						{
							line = output;
							bayer_line = decoder->RawBayer16;


							scale = 256.0;

							y_rmult = ((rgb2yuv[0][0]) * scale);
							y_gmult = ((rgb2yuv[0][1]) * scale);
							y_bmult = ((rgb2yuv[0][2]) * scale);
							y_offset= ((rgb2yuv[0][3]) * scale);

							u_rmult = ((rgb2yuv[1][0]) * scale);
							u_gmult = ((rgb2yuv[1][1]) * scale);
							u_bmult = ((rgb2yuv[1][2]) * scale);
							u_offset= ((rgb2yuv[1][3]) * scale);

							v_rmult = ((rgb2yuv[2][0]) * scale);
							v_gmult = ((rgb2yuv[2][1]) * scale);
							v_bmult = ((rgb2yuv[2][2]) * scale);
							v_offset= ((rgb2yuv[2][3]) * scale);


							r_rmult= (mtrx[0][0] * scale * whitebalance[0]);
							r_gmult= (mtrx[0][1] * scale * whitebalance[1]);
							r_bmult= (mtrx[0][2] * scale * whitebalance[2]);
							r_offset= (mtrx[0][3] * scale);

							g_rmult= (mtrx[1][0] * scale * whitebalance[0]);
							g_gmult= (mtrx[1][1] * scale * whitebalance[1]);
							g_bmult= (mtrx[1][2] * scale * whitebalance[2]);
							g_offset= (mtrx[1][3] * scale);

							b_rmult= (mtrx[2][0] * scale * whitebalance[0]);
							b_gmult= (mtrx[2][1] * scale * whitebalance[1]);
							b_bmult= (mtrx[2][2] * scale * whitebalance[2]);
							b_offset= (mtrx[2][3] * scale);


							for(y=0; y<info->height; y++)
							{
								outyuv = line;
								bayerptr = bayer_line;
								G = bayerptr;
								RG = G + bayer_pitch/4;
								BG = RG + bayer_pitch/4;
								for(x=0; x<info->width; x+=2)
								{
									int r,g,b,r1,g1,b1,rg,bg,y1,y2,u,v,dither;


									g = (*G++);
									rg = (*RG++);
									bg = (*BG++);

									r = ((rg - 32768)<<1) + g;
									b = ((bg - 32768)<<1) + g;

								//	dither = (rand() & 65535)<<1;


									if(matrix_non_unity)
									{
										//TODO : need on convert to linear first.

										r1= (( r_rmult * r + r_gmult * g + r_bmult * b + r_offset)>>8);
										g1= (( g_rmult * r + g_gmult * g + g_bmult * b + g_offset)>>8);
										b1= (( b_rmult * r + b_gmult * g + b_bmult * b + b_offset)>>8);

										//TODO : need on convert back to log/display curve.

										if(r1 < 0) r1 = 0;
										if(r1 > 65535) r1 = 65535;
										if(g1 < 0) g1 = 0;
										if(g1 > 65535) g1 = 65535;
										if(b1 < 0) b1 = 0;
										if(b1 > 65535) b1 = 65535;
									}
									else
									{
										r1 = r;
										g1 = g;
										b1 = b;
									}

									y1= ( y_rmult * r1 + y_gmult * g1 + y_bmult * b1 + 32768)>>16;
									u = (-u_rmult * r1 - u_gmult * g1 + u_bmult * b1 + 32768)>>16;
									v = ( v_rmult * r1 - v_gmult * g1 - v_bmult * b1 + 32768)>>16;


									g = (*G++);
									rg = (*RG++);
									bg = (*BG++);

									r = ((rg - 32768)<<1) + g;
									b = ((bg - 32768)<<1) + g;

								//	dither = (rand() & 65535)<<1;

									if(matrix_non_unity)
									{
										//TODO : need on convert to linear first.

										r1= (( r_rmult * r + r_gmult * g + r_bmult * b + r_offset)>>8);
										g1= (( g_rmult * r + g_gmult * g + g_bmult * b + g_offset)>>8);
										b1= (( b_rmult * r + b_gmult * g + b_bmult * b + b_offset)>>8);

										//TODO : need on convert back to log/display curve.

										if(r1 < 0) r1 = 0;
										if(r1 > 65535) r1 = 65535;
										if(g1 < 0) g1 = 0;
										if(g1 > 65535) g1 = 65535;
										if(b1 < 0) b1 = 0;
										if(b1 > 65535) b1 = 65535;
									}
									else
									{
										r1 = r;
										g1 = g;
										b1 = b;
									}

									y2 = ( y_rmult * r1 + y_gmult * g1 + y_bmult * b1 + 32768)>>16;
									u += (-u_rmult * r1 - u_gmult * g1 + u_bmult * b1 + 32768)>>16;
									v += ( v_rmult * r1 - v_gmult * g1 - v_bmult * b1 + 32768)>>16;

									u >>= 1;
									v >>= 1;

									y1 += y_offset;
									y2 += y_offset;
									u += u_offset;
									v += v_offset;

									if(y1 < 0) y1 = 0;
									if(y1 > 255) y1 = 255;
									if(y2 < 0) y2 = 0;
									if(y2 > 255) y2 = 255;
									if(u < 0) u = 0;
									if(u > 255) u = 255;
									if(v < 0) v = 0;
									if(v > 255) v = 255;


									*outyuv++ = y1;
									*outyuv++ = u;
									*outyuv++ = y2;
									*outyuv++ = v;
								}
								line += pitch;
								bayer_line += bayer_pitch;
							}
						}
						else if (format == DECODED_FORMAT_YU64)
						{
							int shift = 14;
							PIXEL16U *outyuv64;
							line = output;
							bayer_line = decoder->RawBayer16;


							scale = 16384.0;
							//_mm_empty();	// Clear the mmx register state


							y_rmult = ((rgb2yuv[0][0]) * scale);
							y_gmult = ((rgb2yuv[0][1]) * scale);
							y_bmult = ((rgb2yuv[0][2]) * scale);
							y_offset= ((rgb2yuv[0][3]) * scale * 4.0);

							u_rmult = ((rgb2yuv[1][0]) * scale);
							u_gmult = ((rgb2yuv[1][1]) * scale);
							u_bmult = ((rgb2yuv[1][2]) * scale);
							u_offset= ((rgb2yuv[1][3]) * scale * 4.0);

							v_rmult = ((rgb2yuv[2][0]) * scale);
							v_gmult = ((rgb2yuv[2][1]) * scale);
							v_bmult = ((rgb2yuv[2][2]) * scale);
							v_offset= ((rgb2yuv[2][3]) * scale * 4.0);


							scale = 4096.0;
							r_rmult= (mtrx[0][0] * scale * whitebalance[0]);
							r_gmult= (mtrx[0][1] * scale * whitebalance[1]);
							r_bmult= (mtrx[0][2] * scale * whitebalance[2]);
							r_offset= (mtrx[0][3] * scale);

							g_rmult= (mtrx[1][0] * scale * whitebalance[0]);
							g_gmult= (mtrx[1][1] * scale * whitebalance[1]);
							g_bmult= (mtrx[1][2] * scale * whitebalance[2]);
							g_offset= (mtrx[1][3] * scale);

							b_rmult= (mtrx[2][0] * scale * whitebalance[0]);
							b_gmult= (mtrx[2][1] * scale * whitebalance[1]);
							b_bmult= (mtrx[2][2] * scale * whitebalance[2]);
							b_offset= (mtrx[2][3] * scale);



							y_offset += 26;
							u_offset += 26;
							v_offset += 26;


							for(y=0; y<info->height; y++)
							{
								outyuv64 = (PIXEL16U *)line;
								bayerptr = bayer_line;
								G = bayerptr;
								RG = G + bayer_pitch/4;
								BG = RG + bayer_pitch/4;
								for(x=0; x<info->width; x+=2)
								{
									int r,g,b,r1,g1,b1,rg,bg,y1,y2,u,v,dither;


									g = (*G++);
									rg = (*RG++);
									bg = (*BG++);

									r = ((rg - 32768)<<1) + g;
									b = ((bg - 32768)<<1) + g;

								//	dither = (rand() & 65535)<<1;

									if(matrix_non_unity)
									{
										//TODO : need on convert to linear first.

										r1= (( r_rmult * r + r_gmult * g + r_bmult * b + r_offset)>>12);
										g1= (( g_rmult * r + g_gmult * g + g_bmult * b + g_offset)>>12);
										b1= (( b_rmult * r + b_gmult * g + b_bmult * b + b_offset)>>12);

										//TODO : need on convert back to log/display curve.

										if(r1 < 0) r1 = 0;
										if(r1 > 65535) r1 = 65535;
										if(g1 < 0) g1 = 0;
										if(g1 > 65535) g1 = 65535;
										if(b1 < 0) b1 = 0;
										if(b1 > 65535) b1 = 65535;
									}
									else
									{
										r1 = r;
										g1 = g;
										b1 = b;
									}

									y1= (( y_rmult * r1 + y_gmult * g1 + y_bmult * b1)>>shift) + y_offset;
									u = (( u_rmult * r1 + u_gmult * g1 + u_bmult * b1)>>shift);
									v = (( v_rmult * r1 + v_gmult * g1 + v_bmult * b1)>>shift);

									g = (*G++);
									rg = (*RG++);
									bg = (*BG++);

									r = ((rg - 32768)<<1) + g;
									b = ((bg - 32768)<<1) + g;

								//	dither = (rand() & 65535)<<1;

									if(matrix_non_unity)
									{
										//TODO : need on convert to linear first.

										r1= (( r_rmult * r + r_gmult * g + r_bmult * b + r_offset)>>12);
										g1= (( g_rmult * r + g_gmult * g + g_bmult * b + g_offset)>>12);
										b1= (( b_rmult * r + b_gmult * g + b_bmult * b + b_offset)>>12);

										//TODO : need on convert back to log/display curve.

										if(r1 < 0) r1 = 0;
										if(r1 > 65535) r1 = 65535;
										if(g1 < 0) g1 = 0;
										if(g1 > 65535) g1 = 65535;
										if(b1 < 0) b1 = 0;
										if(b1 > 65535) b1 = 65535;
									}
									else
									{
										r1 = r;
										g1 = g;
										b1 = b;
									}

									y2= (( y_rmult * r1 + y_gmult * g1 + y_bmult * b1)>>shift) + y_offset;
									u+= (( u_rmult * r1 + u_gmult * g1 + u_bmult * b1)>>shift);
									v+= (( v_rmult * r1 + v_gmult * g1 + v_bmult * b1)>>shift);

									u >>= 1;
									v >>= 1;

									u += u_offset;
									v += v_offset;

									if(y1 < 0) y1 = 0;
									if(y1 > 65535) y1 = 65535;
									if(y2 < 0) y2 = 0;
									if(y2 > 65535) y2 = 65535;
									if(u < 0) u = 0;
									if(u > 65535) u = 65535;
									if(v < 0) v = 0;
									if(v > 65535) v = 65535;


									*outyuv64++ = y1;
									*outyuv64++ = v;
									*outyuv64++ = y2;
									*outyuv64++ = u;
								}
								line += pitch;
								bayer_line += bayer_pitch;
							}
						}
						else //RGBs
						{
							line = output;
							bayer_line = decoder->RawBayer16;

							scale = 256.0;

							r_rmult = (mtrx[0][0]) * scale * whitebalance[0];
							r_gmult = (mtrx[0][1]) * scale * whitebalance[1];
							r_bmult = (mtrx[0][2]) * scale * whitebalance[2];
							r_offset= (mtrx[0][3]) * scale;

							g_rmult = (mtrx[1][0]) * scale * whitebalance[0];
							g_gmult = (mtrx[1][1]) * scale * whitebalance[1];
							g_bmult = (mtrx[1][2]) * scale * whitebalance[2];
							g_offset= (mtrx[1][3]) * scale;

							b_rmult = (mtrx[2][0]) * scale * whitebalance[0];
							b_gmult = (mtrx[2][1]) * scale * whitebalance[1];
							b_bmult = (mtrx[2][2]) * scale * whitebalance[2];
							b_offset= (mtrx[2][3]) * scale;


							for(y=0; y<info->height; y++)
							{
								int i,noisearray[32];
								outyuv = line;
								bayerptr = bayer_line;
								G = bayerptr;
								RG = G + bayer_pitch/4;
								BG = RG + bayer_pitch/4;
								GD = RG + bayer_pitch/4;

								for(i=0; i<32; i++)
								{
									noisearray[i] = (rand() & 127);
								}

								if(info->format == DECODED_FORMAT_RGB32)
								{
									for(x=0; x<info->width; x++)
									{
										int R1,G1,B1;
										int rnd = noisearray[x&31];
									//	*ptr++ = *bayerptr++ >> 8;
									//	*ptr++ = 0x80;
									//	*ptr++ = *bayerptr++ >> 8;
									//	*ptr++ = 0x80;

										int r,g,b,g1,g2,gdiff,y1,y2,u,v;

									//	g = (g1+g2)>>1;
									//	*g_row_ptr++ = g;
									//	*rg_row_ptr++ = (r-g+256)>>1;
									//	*bg_row_ptr++ = (b-g+256)>>1;
									//	*gdiff_row_ptr++ = (g1-g2+256)>>1;

										g = ((*G++)>>1);
										r = ((*RG++ + 64)>>0)-(256<<7)+g;
										b = ((*BG++ + 64)>>0)-(256<<7)+g;
									//	gdiff = ((*GD++ + 64)>>7)-256+g;

										if(matrix_non_unity)
										{
											//TODO : need on convert to linear first.

											R1 = ((r*r_rmult + g*r_gmult + b*r_bmult + r_offset)>>8) + rnd;
											G1 = ((r*g_rmult + g*g_gmult + b*g_bmult + g_offset)>>8) + rnd;
											B1 = ((r*b_rmult + g*b_gmult + b*b_bmult + b_offset)>>8) + rnd;

											//TODO : need on convert back to log/display curve.
										}
										else
										{
											R1 = r + rnd;
											G1 = g + rnd;
											B1 = b + rnd;
										}

										R1 >>= 7;
										G1 >>= 7;
										B1 >>= 7;

										if(R1 < 0) R1 = 0;
										if(R1 > 255) R1 = 255;
										if(G1 < 0) G1 = 0;
										if(G1 > 255) G1 = 255;
										if(B1 < 0) B1 = 0;
										if(B1 > 255) B1 = 255;


										*outyuv++ = B1;
										*outyuv++ = G1;
										*outyuv++ = R1;
										*outyuv++ = 255;
									}
								}
								else
								{
									for(x=0; x<info->width; x++)
									{
										int R1,G1,B1;
										int rnd = noisearray[x&31];
									//	*ptr++ = *bayerptr++ >> 8;
									//	*ptr++ = 0x80;
									//	*ptr++ = *bayerptr++ >> 8;
									//	*ptr++ = 0x80;

										int r,g,b,g1,g2,gdiff,y1,y2,u,v;
										//g = (g1+g2)>>1;
									//	*g_row_ptr++ = g;
									//	*rg_row_ptr++ = (r-g+256)>>1;
									//	*bg_row_ptr++ = (b-g+256)>>1;
									//	*gdiff_row_ptr++ = (g1-g2+256)>>1;

										g = ((*G++)>>1);
										r = ((*RG++ + 64)>>0)-(256<<7)+g;
										b = ((*BG++ + 64)>>0)-(256<<7)+g;
									//	gdiff = ((*GD++ + 64)>>7)-256+g;

										if(matrix_non_unity)
										{
											//TODO: Need to convert to linear first.

											R1 = ((r*r_rmult + g*r_gmult + b*r_bmult + r_offset)>>8) + rnd;
											G1 = ((r*g_rmult + g*g_gmult + b*g_bmult + g_offset)>>8) + rnd;
											B1 = ((r*b_rmult + g*b_gmult + b*b_bmult + b_offset)>>8) + rnd;

											//TODO: Need to convert back to log/display curve.
										}
										else
										{
											R1 = r + rnd;
											G1 = g + rnd;
											B1 = b + rnd;
										}

										R1 >>= 7;
										G1 >>= 7;
										B1 >>= 7;

										if(R1 < 0) R1 = 0;
										if(R1 > 255) R1 = 255;
										if(G1 < 0) G1 = 0;
										if(G1 > 255) G1 = 255;
										if(B1 < 0) B1 = 0;
										if(B1 > 255) B1 = 255;


										*outyuv++ = B1;
										*outyuv++ = G1;
										*outyuv++ = R1;
									}
								}

								line += pitch;
								bayer_line += bayer_pitch;
							}
						}
						*/
						//MEMORY_ALIGNED_FREE(RawBayer16);
					}
				}
				else
				if((decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
					(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
				{
					int precision = codec->precision;
					if(decoder->RawBayer16 == NULL)
					{
#if _ALLOCATOR
						ALLOCATOR *allocator = decoder->allocator;
						size_t size = info->width*info->height*num_channels*sizeof(PIXEL);

						decoder->RawBayer16 =
							(PIXEL16U *)AllocAligned(allocator, size, 16);
#else
						decoder->RawBayer16 =
							(PIXEL16U *)MEMORY_ALIGNED_ALLOC(info->width*info->height*num_channels*sizeof(PIXEL), 16);
#endif
						decoder->RawBayerSize = info->width*info->height*num_channels*sizeof(PIXEL);
					}

//#ifdef SHARPENING
					if(decoder->RGBFilterBuffer16 == NULL)
					{
						int frame_size = info->width*decoded_height*4*3*sizeof(PIXEL);
						if(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444 && ALPHAOUTPUT(decoder->frame.format))
							frame_size = info->width*decoded_height*4*4*sizeof(PIXEL);
#if _ALLOCATOR
						{
							ALLOCATOR *allocator = decoder->allocator;
							decoder->RGBFilterBuffer16 =
								(PIXEL16U *)AllocAligned(allocator, frame_size, 16);
						}
#else
						decoder->RGBFilterBuffer16 =
							(PIXEL16U *)MEMORY_ALIGNED_ALLOC(frame_size, 16);
#endif
						decoder->RGBFilterBufferSize = frame_size;
					}
//#endif

					if(decoder->RawBayer16 == NULL || decoder->RGBFilterBuffer16 == NULL)
					{
						decoder->error = CODEC_ERROR_MEMORY_ALLOC;
						return;
					}


					//TODO: Replace this memory allocation with a scratch buffer allocation

					if(decoder->RawBayer16)
					{
						uint8_t *outyuv,*line, *source_line;
						PIXEL16U *bayerptr;
						PIXEL16U *G,*RG,*BG;
						int x,y;
						int src_pitch = info->width*num_channels*sizeof(PIXEL);

                        int y_rmult,y_gmult,y_bmult,y_offset;//shift=8;
						int u_rmult,u_gmult,u_bmult,u_offset;
						int v_rmult,v_gmult,v_bmult,v_offset;
						
						float scale = 256.0;

						//int matrix_non_unity = 0;
						//int wb_non_unity = 0;
						//float curve2lin[2048];
						//float lin2curve[2048+512+2];

						static float rgb2yuv[3][4] =
						{
                            {0.183f, 0.614f, 0.062f, 16.0f/256.0f},
                            {-0.101f,-0.338f, 0.439f, 0.5f},
                            {0.439f,-0.399f,-0.040f, 0.5}
						};

#if _THREADED
						TransformInverseSpatialUniversalThreadedToRow16u(decoder, frame, num_channels,
											(uint8_t *)decoder->RawBayer16, src_pitch,
											info, chroma_offset, precision);
#else
						TransformInverseSpatialToRow16u(transform_array, frame, num_channels,
											decoder->RawBayer16, src_pitch, info,
											&decoder->scratch, chroma_offset, precision);
#endif


						if (format == DECODED_FORMAT_YUYV)
						{
							line = output;
							source_line = (unsigned char *)decoder->RawBayer16;


							scale = 256.0;

							y_rmult = (int)((rgb2yuv[0][0]));
							y_gmult = (int)((rgb2yuv[0][1]));
							y_bmult = (int)((rgb2yuv[0][2]));
							y_offset= (int)((rgb2yuv[0][3]));

							u_rmult = (int)((rgb2yuv[1][0]));
							u_gmult = (int)((rgb2yuv[1][1]));
							u_bmult = (int)((rgb2yuv[1][2]));
							u_offset= (int)((rgb2yuv[1][3]));

							v_rmult = (int)((rgb2yuv[2][0]));
							v_gmult = (int)((rgb2yuv[2][1]));
							v_bmult = (int)((rgb2yuv[2][2]));
							v_offset= (int)((rgb2yuv[2][3]));


							for(y=0; y<info->height; y++)
							{
								outyuv = line;
								bayerptr = (PIXEL16U *)source_line;
								G = bayerptr;
								RG = G + src_pitch/(2*num_channels);
								BG = RG + src_pitch/(2*num_channels);
								for(x=0; x<info->width; x+=2)
								{
									int r,g,b,r1,g1,b1,rg,bg,y1,y2,u,v;

									g = (*G++);
									rg = (*RG++);
									bg = (*BG++);

									r = ((rg - 32768)<<1) + g;
									b = ((bg - 32768)<<1) + g;

									r1 = r;
									g1 = g;
									b1 = b;

									y1= ( y_rmult * r1 + y_gmult * g1 + y_bmult * b1 + 32768)>>16;
									u = (-u_rmult * r1 - u_gmult * g1 + u_bmult * b1 + 32768)>>16;
									v = ( v_rmult * r1 - v_gmult * g1 - v_bmult * b1 + 32768)>>16;

									g = (*G++);
									rg = (*RG++);
									bg = (*BG++);

									r = ((rg - 32768)<<1) + g;
									b = ((bg - 32768)<<1) + g;

									r1 = r;
									g1 = g;
									b1 = b;

									y2 = ( y_rmult * r1 + y_gmult * g1 + y_bmult * b1 + 32768)>>16;
									u += (-u_rmult * r1 - u_gmult * g1 + u_bmult * b1 + 32768)>>16;
									v += ( v_rmult * r1 - v_gmult * g1 - v_bmult * b1 + 32768)>>16;

									u >>= 1;
									v >>= 1;

									y1 += y_offset;
									y2 += y_offset;
									u += u_offset;
									v += v_offset;

									if(y1 < 0) y1 = 0;
									if(y1 > 255) y1 = 255;
									if(y2 < 0) y2 = 0;
									if(y2 > 255) y2 = 255;
									if(u < 0) u = 0;
									if(u > 255) u = 255;
									if(v < 0) v = 0;
									if(v > 255) v = 255;

									*outyuv++ = y1;
									*outyuv++ = u;
									*outyuv++ = y2;
									*outyuv++ = v;
								}
								line += pitch;
								source_line += src_pitch;
							}
						}
						else if (format == DECODED_FORMAT_YU64)
						{
							int shift = 14;
							PIXEL16U *outyuv64;
							line = output;
							source_line = (unsigned char *)decoder->RawBayer16;

							scale = 16384.0;

							y_rmult = (int)((rgb2yuv[0][0]) * scale);
							y_gmult = (int)((rgb2yuv[0][1]) * scale);
							y_bmult = (int)((rgb2yuv[0][2]) * scale);
							y_offset= (int)((rgb2yuv[0][3]) * scale * 4.0f);

							u_rmult = (int)((rgb2yuv[1][0]) * scale);
							u_gmult = (int)((rgb2yuv[1][1]) * scale);
							u_bmult = (int)((rgb2yuv[1][2]) * scale);
							u_offset= (int)((rgb2yuv[1][3]) * scale * 4.0f);

							v_rmult = (int)((rgb2yuv[2][0]) * scale);
							v_gmult = (int)((rgb2yuv[2][1]) * scale);
							v_bmult = (int)((rgb2yuv[2][2]) * scale);
							v_offset= (int)((rgb2yuv[2][3]) * scale * 4.0f);


							scale = 4096.0;

							y_offset += 26;
							u_offset += 26;
							v_offset += 26;


							for(y=0; y<info->height; y++)
							{
								outyuv64 = (PIXEL16U *)line;
								bayerptr = (PIXEL16U *)source_line;
								G = bayerptr;
								RG = G + src_pitch/(2*num_channels);
								BG = RG + src_pitch/(2*num_channels);
								for(x=0; x<info->width; x+=2)
								{
									int r,g,b,r1,g1,b1,rg,bg,y1,y2,u,v;


									g = (*G++);
									rg = (*RG++);
									bg = (*BG++);

									r = ((rg - 32768)<<1) + g;
									b = ((bg - 32768)<<1) + g;

									r1 = r;
									g1 = g;
									b1 = b;

									y1= (( y_rmult * r1 + y_gmult * g1 + y_bmult * b1)>>shift) + y_offset;
									u = (( u_rmult * r1 + u_gmult * g1 + u_bmult * b1)>>shift);
									v = (( v_rmult * r1 + v_gmult * g1 + v_bmult * b1)>>shift);

									g = (*G++);
									rg = (*RG++);
									bg = (*BG++);

									r = ((rg - 32768)<<1) + g;
									b = ((bg - 32768)<<1) + g;

									r1 = r;
									g1 = g;
									b1 = b;

									y2= (( y_rmult * r1 + y_gmult * g1 + y_bmult * b1)>>shift) + y_offset;
									u+= (( u_rmult * r1 + u_gmult * g1 + u_bmult * b1)>>shift);
									v+= (( v_rmult * r1 + v_gmult * g1 + v_bmult * b1)>>shift);

									u >>= 1;
									v >>= 1;

									u += u_offset;
									v += v_offset;

									if(y1 < 0) y1 = 0;
									if(y1 > 65535) y1 = 65535;
									if(y2 < 0) y2 = 0;
									if(y2 > 65535) y2 = 65535;
									if(u < 0) u = 0;
									if(u > 65535) u = 65535;
									if(v < 0) v = 0;
									if(v > 65535) v = 65535;


									*outyuv64++ = y1;
									*outyuv64++ = v;
									*outyuv64++ = y2;
									*outyuv64++ = u;
								}
								line += pitch;
								source_line += src_pitch;
							}
						}
						else //RGBs
						{
							line = output;
							source_line = (unsigned char *)decoder->RawBayer16;

							for(y=0; y<info->height; y++)
							{
								int i,noisearray[32];
								unsigned short *rgb16 = (unsigned short *)line;
								outyuv = line;
								bayerptr = (PIXEL16U *)source_line;
								G = bayerptr;
								RG = G + src_pitch/(2*num_channels);
								BG = RG + src_pitch/(2*num_channels);

								for(i=0; i<32; i++)
								{
									noisearray[i] = (rand() & 255);
								}

								if(info->format == DECODED_FORMAT_RGB32)
								{
									for(x=0; x<info->width; x++)
									{
										int R1,G1,B1;
										int rnd = noisearray[x&31];

										#if 0
										G1 = (*G++) + rnd;
										R1 = ((*RG++<<1) - (128<<9)) + G1;
										B1 = ((*BG++<<1) - (128<<9)) + G1;
										#else
										G1 = (*G++) + rnd;
										R1 = (*RG++) + rnd;
										B1 = (*BG++) + rnd;
										#endif

										R1 >>= 8;
										G1 >>= 8;
										B1 >>= 8;

										if(R1 < 0) R1 = 0;
										if(R1 > 255) R1 = 255;
										if(G1 < 0) G1 = 0;
										if(G1 > 255) G1 = 255;
										if(B1 < 0) B1 = 0;
										if(B1 > 255) B1 = 255;

										*outyuv++ = B1;
										*outyuv++ = G1;
										*outyuv++ = R1;
										*outyuv++ = 255;
									}
								}
								else if(info->format == DECODED_FORMAT_RGB24)
								{
									for(x=0; x<info->width; x++)
									{
										int R1,G1,B1;
										int rnd = noisearray[x&31];

										#if 0
										G1 = (*G++) + rnd;
										R1 = ((*RG++<<1) - (128<<9)) + G1;
										B1 = ((*BG++<<1) - (128<<9)) + G1;
										#else
										G1 = (*G++) + rnd;
										R1 = (*RG++) + rnd;
										B1 = (*BG++) + rnd;
										#endif

										R1 >>= 8;
										G1 >>= 8;
										B1 >>= 8;

										if(R1 < 0) R1 = 0;
										if(R1 > 255) R1 = 255;
										if(G1 < 0) G1 = 0;
										if(G1 > 255) G1 = 255;
										if(B1 < 0) B1 = 0;
										if(B1 > 255) B1 = 255;

										*outyuv++ = B1;
										*outyuv++ = G1;
										*outyuv++ = R1;
									}
								}
								else if(info->format == DECODED_FORMAT_RG48)
								{
									for(x=0; x<info->width; x++)
									{
										int R1,G1,B1;

										G1 = (*G++);
										R1 = (*RG++);
										B1 = (*BG++);

										*rgb16++ = R1;
										*rgb16++ = G1;
										*rgb16++ = B1;
									}
								}

								line += pitch;
								source_line += src_pitch;
							}
						}

						//MEMORY_ALIGNED_FREE(RawBayer16);
					}
				}
				else // Output the frame in one of the RGB 8-bit formats
				{
					//char *buffer = decoder->buffer;
					//size_t buffer_size = decoder->buffer_size;

					// Invert the bottom wavelet and convert the output to the requested color format
#if _THREADED
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
											 output, pitch,
											 info, chroma_offset, precision,
											 InvertHorizontalStrip16sYUVtoRGB);
#else
					TransformInverseSpatialToBuffer(decoder, transform_array, frame, num_channels, output, pitch,
													&info2, &decoder->scratch, chroma_offset, precision);
#endif
				}
			}
		}

#if TIMING
		// Count the number of progressive frames that were decoded
		progressive_decode_count++;
#endif
	}

	STOP(tk_inverse);

#ifdef ADOBE_MEMORY_FUNCTIONS
	if((decoder->RawBayer16 && decoder->RawBayerSize > 2048*1152*2) ||
	   (decoder->RGBFilterBuffer16 && decoder->RGBFilterBufferSize > 2048*1152*2))
	{
#if _ALLOCATOR
		if(decoder->RawBayer16)
		{
			FreeAligned(decoder->allocator, decoder->RawBayer16);
			decoder->RawBayer16 = NULL;
			decoder->RawBayerSize = NULL;
		}
		if(decoder->RGBFilterBuffer16)
		{
			FreeAligned(decoder->allocator, decoder->RGBFilterBuffer16);
			decoder->RGBFilterBuffer16 = NULL;
			decoder->RGBFilterBufferSize = NULL;
		}
#else
		if(decoder->RawBayer16)
		{
			MEMORY_ALIGNED_FREE(decoder->RawBayer16);
			decoder->RawBayer16 = NULL;
			decoder->RawBayerSize = NULL;
		}
		if(decoder->RGBFilterBuffer16)
		{
			MEMORY_ALIGNED_FREE(decoder->RGBFilterBuffer16);
			decoder->RGBFilterBuffer16 = NULL;
			decoder->RGBFilterBufferSize = NULL;
		}
#endif
	}
#endif

#if (0 && DEBUG)
	if (logfile) {
		//uint8_t *subimage = output;
		uint8_t *subimage = output + (2 * info->width) - 16;
		DumpArray8u("YUV Image", subimage, 16, 16, pitch, logfile);
	}
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Exit ReconstructFrameToBuffer\n");
	}
#endif

#if (0 && DEBUG && _WIN32)
	_CrtCheckMemory();
#endif

}



#if 0

// Reconstruct the frame to quarter resolution at full frame rate
void ReconstructQuarterFrame(DECODER *decoder, int num_channels,
							 uint8_t *frame1, uint8_t *frame2, int output_pitch,
							 FRAME_INFO *info, char *buffer, size_t buffer_size)
{

	TRANSFORM **transform_array = decoder->transform;
	int output_width = info->width;
	int output_height = info->height;
	PIXEL *low_row_ptr[CODEC_MAX_CHANNELS];
	PIXEL *high_row_ptr[CODEC_MAX_CHANNELS];
	PIXEL *out1_row_ptr[CODEC_MAX_CHANNELS];
	PIXEL *out2_row_ptr[CODEC_MAX_CHANNELS];
	PIXEL *bufptr = (PIXEL *)buffer;
	uint8_t *output_row_ptr = output;
	int low_pitch[CODEC_MAX_CHANNELS];
	int high_pitch[CODEC_MAX_CHANNELS];
	int channel;
	int row;

	// Check that there is enough space for the intermediate results from each channel
	assert(output_width * sizeof(PIXEL) < buffer_size);

	// Get pointers into the wavelets for each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the lowpass bands from the two wavelets for the two halves of the temporal wavelet
		IMAGE *low_wavelet = transform_array[channel]->wavelet[3];
		IMAGE *high_wavelet = transform_array[channel]->wavelet[2];

		// Get the pointers to the first row in each lowpass band
		low_row_ptr[channel] = low_wavelet->band[0];
		high_row_ptr[channel] = high_wavelet->band[0];

		low_pitch[channel] = low_wavelet->pitch / sizeof(PIXEL);
		high_pitch[channel] = high_wavelet->pitch / sizeof(PIXEL);

		// Allocate space for one row of results for this channel
		channel_row_ptr[channel] = bufptr;
		bufptr += low_wavelet->width;
	}

	for (row = 0; row < output_height; row++)
	{
		char *bufptr = buffer;

		for (channel = 0; channel < num_channels; channel++)
		{
			// Invert the temporal transform at quarter resolution
			InvertTemporalQuarterRow16s(low_row_ptr[channel], high_row_ptr[channel], channel_row_ptr[channel]);

			// Advance to the next row in each band for the temporal transform
			low_row_ptr[channel] += low_pitch[channel];
			high_row_ptr[channel] += high_pitch[channel];
		}

		// Pack the intermediate results into the output row
		ConvertUnpacked16sRowToPacked8u(channel_row_ptr, num_channels, output_row_ptr, output_width);

		// Advance the output row pointer
		output_row_ptr += output_pitch;
	}
}

#else

// Reconstruct the frame to quarter resolution at full frame rate
void ReconstructQuarterFrame(DECODER *decoder, int num_channels,
							 int frame_index, uint8_t *output, int output_pitch,
							 FRAME_INFO *info, const SCRATCH *scratch, int precision)
{

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	TRANSFORM **transform_array = decoder->transform;
	int output_width = info->width;
	int output_height = info->height;
	PIXEL *low_row_ptr[CODEC_MAX_CHANNELS];
	PIXEL *high_row_ptr[CODEC_MAX_CHANNELS];
	uint8_t *output_row_ptr = output;
	int low_pitch[CODEC_MAX_CHANNELS];
	int high_pitch[CODEC_MAX_CHANNELS];
	int channel;
	int row;

	// Value used for filling the fourth channel in ARGB output
	int alpha = 255;

	int format = COLORFORMAT(info);
	int color_space = COLORSPACE(info);
	int decoded_format = DECODEDFORMAT(info);
	//bool inverted = false;

	// The pixels are descaled in the inverse temporal transform
	//const int descale = 0;

	// Shift the intermediate results to 16-bit pixels
	const int shift_yu64 = 8;

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
#if DEBUG
	size_t buffer_size = scratch->free_size;
#endif

	// Initialize a pointer for allocating space in the buffer
	PIXEL *bufptr = (PIXEL *)buffer;

	// Array of pointers to the start of each channel in the intermediate results
	PIXEL *channel_row_ptr[CODEC_MAX_CHANNELS];

	// Check that there is enough space for the intermediate results from each channel
#if DEBUG
	assert(output_width * sizeof(PIXEL) < buffer_size);
#endif
	ComputeCube(decoder);

	// Get pointers into the wavelets for each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the lowpass bands from the two wavelets for the two halves of the temporal wavelet
		IMAGE *low_wavelet = transform_array[channel]->wavelet[4];
		IMAGE *high_wavelet = transform_array[channel]->wavelet[3];

		// Get the pointers to the first row in each lowpass band
		low_row_ptr[channel] = low_wavelet->band[0];
		high_row_ptr[channel] = high_wavelet->band[0];

		low_pitch[channel] = low_wavelet->pitch / sizeof(PIXEL);
		high_pitch[channel] = high_wavelet->pitch / sizeof(PIXEL);

		// Force the row of intermediate results to be properly aligned
		bufptr = (PIXEL *)ALIGN16(bufptr);

		// Allocate space for one row of results for this channel
		channel_row_ptr[channel] = bufptr;
		bufptr += low_wavelet->width;

		// Check that the row of intermediate results is properly aligned
		assert(ISALIGNED16(channel_row_ptr[channel]));
	}

	// Invert the image if required
	switch (decoded_format)
	{
	case DECODED_FORMAT_RGB24:
	case DECODED_FORMAT_RGB32:
		output_row_ptr += (output_height - 1) * output_pitch;
		output_pitch = NEG(output_pitch);
	}

	//HACK: Seems to work, I don't know why.	//DAN20070304
	if (precision == 12) precision = 8;

	// Apply the inverse temporal transform to the lowpass and highpass rows
	for (row = 0; row < output_height; row++)
	{
		// Most of the color conversion routines use zero descaling
		int descale = 0;
		//char *bufptr = buffer;

		for (channel = 0; channel < num_channels; channel++)
		{
			if (frame_index == 0)
			{
				// Invert the temporal transform at quarter resolution to get the even row
				InvertTemporalQuarterEvenRow16s(low_row_ptr[channel], high_row_ptr[channel],
												channel_row_ptr[channel], output_width, precision);
			}
			else
			{
				assert(frame_index == 1);

				// Invert the temporal transform at quarter resolution to get the odd row
				InvertTemporalQuarterOddRow16s(low_row_ptr[channel], high_row_ptr[channel],
											   channel_row_ptr[channel], output_width, precision);
			}

			// Advance to the next row in each band for the temporal transform
			low_row_ptr[channel] += low_pitch[channel];
			high_row_ptr[channel] += high_pitch[channel];
		}

		if(decoder->use_active_metadata_decoder)
		{
			uint8_t *channeldata[TRANSFORM_MAX_CHANNELS]; // used in quarter res decodes
			int channelpitch[TRANSFORM_MAX_CHANNELS]; // used in quarter res decodes
			int i;

			FRAME_INFO info2;
			memcpy(&info2, info, sizeof(FRAME_INFO));
			info2.height = 1;

			for(i=0;i<num_channels;i++)
			{
				channeldata[i] = (uint8_t *)channel_row_ptr[i];
				channelpitch[i] = 0;
			}

#if 1
			{
				__m128i *Y = (__m128i *)channeldata[0];
				__m128i *U = (__m128i *)channeldata[1];
				__m128i *V = (__m128i *)channeldata[2];
				__m128i v;
				int x;

				__m128i rgb_limit_epi16 = _mm_set1_epi16(0x7fff - 0x0fff);

				for(x=0;x<info->width;x+=8)
				{
					v = _mm_load_si128(Y);
					v = _mm_adds_epi16(v, rgb_limit_epi16);
					v = _mm_subs_epu16(v, rgb_limit_epi16);
					v = _mm_slli_epi16(v, 4);
					_mm_store_si128(Y++, v);
				}
				for(x=0;x<info->width/2;x+=8)
				{
					v = _mm_load_si128(U);
					v = _mm_adds_epi16(v, rgb_limit_epi16);
					v = _mm_subs_epu16(v, rgb_limit_epi16);
					v = _mm_slli_epi16(v, 4);
					_mm_store_si128(U++, v);
				}
				for(x=0;x<info->width/2;x+=8)
				{
					v = _mm_load_si128(V);
					v = _mm_adds_epi16(v, rgb_limit_epi16);
					v = _mm_subs_epu16(v, rgb_limit_epi16);
					v = _mm_slli_epi16(v, 4);
					_mm_store_si128(V++, v);
				}
			}
#else
			//non SSE2
			for(x=0;x<info->width*2;x++)
			{
				int val = *gptr++;
				if(val < 0) val = 0;
				if(val > 4095) val = 4095;
				val <<= 4;
				*src++ = val;
			}
			src = scanline2;
#endif

			Row16uQuarter2OutputFormat(decoder, &info2, 0, output_row_ptr, output_pitch,
				decoder->gop_frame_num/*0 frame*/, scratch->free_ptr, scratch->free_size, false, channeldata, channelpitch);

		}
		else
		{
			//DAN20081203 -- fix for 444 decodes in AE32-bit float
			decoder->frame.white_point = 16;
			//decoder->frame.signed_pixels = 0;

			// Convert the rows of luma and chroma into the output format
			switch(format)
			{
			case COLOR_FORMAT_YUYV:
			case COLOR_FORMAT_UYVY:
				// Pack the intermediate results into the output row

				if(decoder->codec.encoded_format == ENCODED_FORMAT_BAYER)
				{
					assert(0);//need quarter res BAYER To YUV decoder
				}
				else if((decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
						(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
				{
				//	assert(0);//need quarter res RGB To YUV decoder
					ConvertRGB2YUV(	channel_row_ptr[1], channel_row_ptr[0], channel_row_ptr[2],
									output_width, output_width, output_width,
									output_row_ptr, output_pitch,
									info->width, 1, 10, info->colorspace, format);

				}
				else
				{
					ConvertUnpacked16sRowToPacked8u(channel_row_ptr, num_channels, output_row_ptr, output_width, format);
				}
				break;

			case COLOR_FORMAT_RGB24:
				if((decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
					(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
				{
					ConvertRGB48toRGB24(	channel_row_ptr[1], channel_row_ptr[0], channel_row_ptr[2],
									output_width, output_width, output_width,
									output_row_ptr, output_pitch,
									info->width, 1, 10, 0);
				}
				else
				{
					// Convert the intermediate results into a row of RGB24
					ConvertUnpacked16sRowToRGB24(channel_row_ptr, num_channels, output_row_ptr, output_width,
											descale, format, color_space);
				}
				break;

			case COLOR_FORMAT_RGB32:
				if((decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
					(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
				{
					ConvertRGBA48toRGB32(channel_row_ptr[1], channel_row_ptr[0], channel_row_ptr[2], NULL,
										output_width,
										output_row_ptr, output_pitch,
										info->width, 1, 10, 0, 3/*only 3 chhanel not 4 for alpha*/);
				}
				else
				{
					// Convert the intermediate results into a row of RGBA32
					ConvertUnpacked16sRowToRGB32(channel_row_ptr, num_channels, output_row_ptr, output_width,
												descale, format, color_space, alpha);
				}
				break;

			case COLOR_FORMAT_YU64:
			case COLOR_FORMAT_V210:
				// Convert the intermediate results into a row of YU64
				ConvertUnpacked16sRowToYU64(channel_row_ptr, num_channels, output_row_ptr, output_width,
											shift_yu64, precision, format);
				break;

			case COLOR_FORMAT_B64A:
				if((decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
					(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
				{
					// Convert the intermediate results into a row of RGBA with 16 bits per component
					descale = 2;
					ConvertUnpacked16sRowToB64A(channel_row_ptr, num_channels, output_row_ptr, output_width,
												descale, precision);
				}
				else
				{
					ConvertUnpackedYUV16sRowToRGB48(channel_row_ptr, num_channels, output_row_ptr, output_width,
												descale, precision, COLOR_FORMAT_B64A, color_space);
				}
				break;
			case COLOR_FORMAT_R210:
			case COLOR_FORMAT_DPX0:
			case COLOR_FORMAT_RG30:
			case COLOR_FORMAT_AR10:
			case COLOR_FORMAT_AB10:
				if((decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
					(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
				{
					// Convert the intermediate results into a row of RGBA with 16 bits per component
					descale = 2;
					ConvertUnpacked16sRowToRGB30(channel_row_ptr, num_channels, output_row_ptr, output_width,
												descale, precision, format, color_space);
				}
				else
				{
					ConvertUnpackedYUV16sRowToRGB48(channel_row_ptr, num_channels, output_row_ptr, output_width,
												descale, precision, format, color_space);
				}
				break;

			case COLOR_FORMAT_RG48:
				// Convert the intermediate results into a row of RGBA with 16 bits per component
				descale = 2;
				ConvertUnpacked16sRowToRGB48(channel_row_ptr, num_channels, output_row_ptr, output_width,
											descale, precision);
				break;

			case COLOR_FORMAT_RG64:
				// Convert the intermediate results into a row of RGBA with 16 bits per component
				descale = 2;
				ConvertUnpacked16sRowToRGBA64(channel_row_ptr, num_channels, output_row_ptr, output_width,
											descale, precision);
				break;

			default:
	#if (1 && DEBUG)
				if (logfile) {
					fprintf(logfile, "ReconstructQuarterFrame bad color format: %d\n", format);
				}
	#endif
				assert(0);
				break;
			}
		}

		// Advance the output row pointer
		output_row_ptr += output_pitch;
	}
}

#endif

#if 0
// Copy the quarter resolution lowpass channels from the spatial transform
void CopyQuarterFrameToBuffer(TRANSFORM **transform_array, int num_channels,
							  uint8_t *output, int output_pitch,
							  FRAME_INFO *info, int precision)
{
	int output_width = info->width;
	int output_height = info->height;
	PIXEL *input_row_ptr[CODEC_MAX_CHANNELS];
	uint8_t *output_row_ptr = output;
	int input_pitch[CODEC_MAX_CHANNELS];
	int channel;
	int row;

	// Get pointers into the wavelets for each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the lowpass bands from the two wavelets for the two halves of the temporal wavelet
		IMAGE *wavelet = transform_array[channel]->wavelet[1];

		// Get the pointers to the first row in each lowpass band
		input_row_ptr[channel] = wavelet->band[0];
		input_pitch[channel] = wavelet->pitch / sizeof(PIXEL);
	}

	for (row = 0; row < output_height; row++)
	{
		// Descale and pack the pixels in each output row
		CopyQuarterRowToBuffer(input_row_ptr, num_channels, output_row_ptr, output_width, precision);

		// Advance the input row pointers
		for (channel = 0; channel < num_channels; channel++) {
			input_row_ptr[channel] += input_pitch[channel];
		}

		// Advance the output row pointer
		output_row_ptr += output_pitch;
	}
}
#endif


// Convert the quarter resolution lowpass channels to the specified output format
void ConvertQuarterFrameToBuffer(DECODER *decoder, TRANSFORM **transform_array, int num_channels,
								 uint8_t *output, int output_pitch,
								 FRAME_INFO *info, int precision)
{
	int output_width = info->width;
	int output_height = info->height;
	PIXEL *input_row_ptr[CODEC_MAX_CHANNELS];
	uint8_t *output_row_ptr = output;
	int input_pitch[CODEC_MAX_CHANNELS];
	int channel;
	int row;

	// Value used for filling the fourth channel in ARGB output
	int alpha = 255;

	int format = COLORFORMAT(info);
	int color_space = COLORSPACE(info);
	int decoded_format = DECODEDFORMAT(info);
	//bool inverted = false;

	// Get pointers into the wavelets for each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the lowpass bands from the wavelets with quarter resolution
		const int wavelet_index = 1;
		IMAGE *wavelet = transform_array[channel]->wavelet[wavelet_index];

		// The wavelet should have been reconstructed
		assert(wavelet != NULL);

		// The lowpass band should be valid
		assert((wavelet->band_valid_flags & BAND_VALID_MASK(0)) != 0);

		// Get the pointers to the first row in each lowpass band
		input_row_ptr[channel] = wavelet->band[0];
		input_pitch[channel] = wavelet->pitch / sizeof(PIXEL);
	}

	// Invert the image if required
	switch (decoded_format)
	{
	case DECODED_FORMAT_RGB24:
	case DECODED_FORMAT_RGB32:
		output_row_ptr += (output_height - 1) * output_pitch;
		output_pitch = NEG(output_pitch);
	}

	ComputeCube(decoder);

	//HACK DAN20110122 -- some formats will not directly decode so need to use the AM route
	{
		if(	format == COLOR_FORMAT_YU64 || 
			format == COLOR_FORMAT_V210 ||
			format == COLOR_FORMAT_R408 || 
			format == COLOR_FORMAT_V408)
		{
			if(	(decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
				(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
			{
				decoder->use_active_metadata_decoder = true;
				decoder->apply_color_active_metadata = true;
			}
		}
	}



	if(decoder->use_active_metadata_decoder)
	{
#if _THREADED
		{
			WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	#if _DELAY_THREAD_START
			if(decoder->worker_thread.pool.thread_count == 0)
			{
				CreateLock(&decoder->worker_thread.lock);
				// Initialize the pool of transform worker threads
				ThreadPoolCreate(&decoder->worker_thread.pool,
								decoder->thread_cntrl.capabilities >> 16/*cpus*/,
								WorkerThreadProc,
								decoder);
			}
	#endif
			// Post a message to the mailbox
			mailbox->output = output_row_ptr;
			mailbox->pitch = output_pitch;
			mailbox->framenum = 0;
			for(channel = 0; channel < num_channels; channel++)
			{
				mailbox->channeldata[channel] = (uint8_t *)input_row_ptr[channel];
				mailbox->channelpitch[channel] = input_pitch[channel]*sizeof(PIXEL);
			}
			memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
			mailbox->jobType = JOB_TYPE_OUTPUT;
			decoder->RGBFilterBufferPhase = 1;


			// Set the work count to the number of rows to process
			ThreadPoolSetWorkCount(&decoder->worker_thread.pool, info->height);

			// Start the transform worker threads
			ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

			// Wait for all of the worker threads to finish
			ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

			decoder->RGBFilterBufferPhase = 0;
		}
#endif
	}
	else
	{
		//DAN20081203 -- fix for 444 decodes in AE32-bit float
		decoder->frame.white_point = 16;
		//decoder->frame.signed_pixels = 0;

		// Convert each row to the specified output format
		for (row = 0; row < output_height; row++)
		{
			// Right shift for converting lowpass coefficients to pixels
			int descale = 4;

			switch(format & 0x7fffffff)
			{
			case COLOR_FORMAT_YUYV:
			case COLOR_FORMAT_UYVY:
				if(	(decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
					(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
				{
				//	assert(0);//need quarter res RGB To YUV decoder
					ConvertRGB2YUV(	input_row_ptr[1], input_row_ptr[0], input_row_ptr[2],
									output_width, output_width, output_width,
									output_row_ptr, output_pitch,
									info->width, 1, 14, info->colorspace, format);

				}
				else
				{
					// Descale and pack the pixels in each output row
					CopyQuarterRowToBuffer(input_row_ptr, num_channels, output_row_ptr, output_width,
										precision, format);
				}
				break;

			case COLOR_FORMAT_RGB24:
				if(	(decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
					(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
				{
					ConvertRGB48toRGB24(input_row_ptr[1], input_row_ptr[0], input_row_ptr[2],
										output_width, output_width, output_width,
										output_row_ptr, output_pitch,
										info->width, 1, 14, 0);
				}
				else
				{
					// Convert the intermediate results into a row of RGB24
					ConvertUnpacked16sRowToRGB24(input_row_ptr, num_channels, output_row_ptr, output_width, descale, format, color_space);
				}
				break;

			case COLOR_FORMAT_RGB32:
			case COLOR_FORMAT_RGB32_INVERTED:
				if(	(decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
					(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
				{
					ConvertRGBA48toRGB32(	input_row_ptr[1], input_row_ptr[0], input_row_ptr[2],  input_row_ptr[3],
									output_width,
									output_row_ptr, output_pitch,
									info->width, 1, 14, 0, num_channels);
				}
				else
				{
					// Convert the intermediate results into a row of RGBA32
					ConvertUnpacked16sRowToRGB32(input_row_ptr, num_channels, output_row_ptr, output_width,
											descale, format, color_space, alpha);
				}
				break;

			case COLOR_FORMAT_YU64:
			case COLOR_FORMAT_V210:
				if(	(decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444) ||
					(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444))
				{
	//TODO  RGB to YUV Quarter RES DAN20110120 - handle above with HACK DAN20110122
	//
				}
				else
				{
					// Convert the intermediate results into a row of YU64
					ConvertUnpacked16sRowToYU64(input_row_ptr, num_channels, output_row_ptr, output_width,
												descale, precision, format);
				}
				break;

			case COLOR_FORMAT_B64A:
				// Convert the intermediate results to a row of ARGB with 16 bits per pixel
				descale = 2;
				ConvertUnpacked16sRowToB64A(input_row_ptr, num_channels, output_row_ptr, output_width,
											descale, precision);
				break;
			case COLOR_FORMAT_R210:
			case COLOR_FORMAT_DPX0:
			case COLOR_FORMAT_RG30:
			case COLOR_FORMAT_AR10:
			case COLOR_FORMAT_AB10:
				// Convert the intermediate results to a row of ARGB with 16 bits per pixel
				descale = 2;
				ConvertUnpacked16sRowToRGB30(input_row_ptr, num_channels, output_row_ptr, output_width,
											descale, precision, format, color_space);
				break;

			case COLOR_FORMAT_RG48:
				// Convert the intermediate results into a row of RGBA with 16 bits per component
				descale = 2;
				ConvertUnpacked16sRowToRGB48(input_row_ptr, num_channels, output_row_ptr, output_width,
											descale, precision);
				break;

			case COLOR_FORMAT_RG64:
				// Convert the intermediate results into a row of RGBA with 16 bits per component
				descale = 2;
				ConvertUnpacked16sRowToRGBA64(input_row_ptr, num_channels, output_row_ptr, output_width,
											descale, precision);
				break;

			default:
				assert(0);
				break;
			}

			// Advance the input row pointers
			for (channel = 0; channel < num_channels; channel++) {
				input_row_ptr[channel] += input_pitch[channel];
			}

			// Advance the output row pointer
			output_row_ptr += output_pitch;
		}
	}
}

// Release all resources allocated by the decoder
void DecodeRelease(DECODER *decoder, TRANSFORM *transform[], int num_transforms)
{
#if _TIMING && 0
	FILE *logfile = decoder->logfile;
	uint32_t frame_count = decoder->frame_count;

	if (logfile != NULL && frame_count > 0)\
	{
#ifdef _WIN32
		PrintStatistics(logfile, frame_count, NULL, TIMING_CSV_FILENAME);
#else
		PrintStatistics(logfile, frame_count, NULL, NULL);
#endif
	}
#endif

	// Free the data structures allocated for decoding
	ClearDecoder(decoder);
}

void DecodeForceMetadataRefresh(DECODER *decoder)
{
    CFHDDATA *cfhddata = &decoder->cfhddata;
   
    cfhddata->force_metadata_refresh = true;
    
    if (decoder->parallelDecoder) {
        cfhddata = &decoder->parallelDecoder->cfhddata;
        cfhddata->force_metadata_refresh = true;
    }
    
}
void SetDecoderFlags(DECODER *decoder, uint32_t flags)
{

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif

	// Set the decoder flags
	decoder->flags = flags;

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Decoder flags: 0x%p\n", decoder->flags);
	}
#endif
}

void SetDecoderFormat(DECODER *decoder, int width, int height, int format, int resolution)
{
	// Need to modify the codec to use the decoding format

	decoder->frame.width = width;
	decoder->frame.height = height;

	if(format == DECODED_FORMAT_WP13)
	{
		decoder->frame.output_format = format;
		//decoder->frame.format = DECODED_FORMAT_RG48; //TODO Why is this needed with W13A work natively.
		decoder->frame.format = format;
		//decoder->frame.signed_pixels = 1;
		decoder->frame.white_point = 13;
	}
	else if(format == DECODED_FORMAT_W13A)
	{
		decoder->frame.output_format = format;
//		decoder->frame.format = DECODED_FORMAT_W13A; // TODO eventually this might be DECODED_FORMAT_RG64
		decoder->frame.format = format;
		//decoder->frame.signed_pixels = 1;
		decoder->frame.white_point = 13;
	}
	else
	{
		decoder->frame.output_format = format;
		decoder->frame.format = format;
		//decoder->frame.signed_pixels = 0;
		decoder->frame.white_point = 16;
	}
	decoder->frame.resolution = resolution;
	decoder->frame.pixel_size = PixelSize(decoder->frame.format);
}



void SetDecoderCapabilities(DECODER *decoder)
{
	int processor_count;
#ifdef _WIN32
	int limit_cpus = 32;
#else
	int limit_cpus = 32;		// AJA spins off too many
#endif

	// Set the capabilities that are most likely supported by the Intel Mac
	decoder->thread_cntrl.capabilities = (_CPU_FEATURE_MMX | _CPU_FEATURE_SSE | _CPU_FEATURE_SSE2);

	if (decoder->thread_cntrl.limit)
	{
		limit_cpus = decoder->thread_cntrl.limit;
	}
	else if (decoder->thread_cntrl.affinity)
	{
		int i;
		const int max_cpu_count = 32;

		limit_cpus = 0;

		for (i = 0; i < max_cpu_count; i++)
		{
			if (decoder->thread_cntrl.affinity & (1<<i)) {
				limit_cpus++;
			}
		}
	}

	// Set the number of processors
	processor_count = GetProcessorCount();

	if(processor_count > limit_cpus)
		processor_count = limit_cpus;

#if (0 && DEBUG)
	// Set the number of processors (for debugging)
	//processor_count = 8;
	processor_count = 1;
	fprintf(stderr, "Limit processors to %d\n", processor_count);
#endif

	decoder->thread_cntrl.capabilities |= (processor_count << 16);
}
int GetDecoderCapabilities(DECODER *decoder)
{
    return decoder->thread_cntrl.capabilities;
}
bool SetDecoderColorFlags(DECODER *decoder, uint32_t color_flags)
{
	if (/*MIN_DECODED_COLOR_SPACE <= color_flags && */color_flags <= MAX_DECODED_COLOR_SPACE)
	{
		decoder->frame.colorspace = color_flags;
		// Indicate that the color flags were set as specified
		return true;
	}

	// The specified color flags were not valid
	return false;
}

// Compute the resolution corresponding to the specified combination of input and output dimensions
int DecodedResolution(int input_width, int input_height, int output_width, int output_height)
{
	int decoded_width;
	int decoded_height;

	// Output height can be negative for inverted RGB
	output_height = abs(output_height);

	if (output_width == input_width && output_height == input_height) {
		return DECODED_RESOLUTION_FULL;
	}

	// Compute the dimensions for half resolution decoding
	decoded_width = input_width / 2;
	decoded_height = input_height / 2;

	// Do the output dimensions correspond to half resolution decoding?
	if (output_width == decoded_width && output_height == decoded_height) {
		return DECODED_RESOLUTION_HALF;
	}

	// Compute the dimensions for quarter resolution decoding
	decoded_width /= 2;
	decoded_height /= 2;

	// Do the output dimensions correspond to half resolution decoding?
	if (output_width == decoded_width && output_height == decoded_height) {
		return DECODED_RESOLUTION_QUARTER;
	}

	return DECODED_RESOLUTION_UNSUPPORTED;
}

// Compute the decoded resolution that is closest to the output dimensions
int DecodedScale(int input_width, int input_height, int output_width, int output_height)
{
	int decoded_width = input_width;
	int decoded_height = input_height;

	static int decodedResolution[] =
	{
		DECODED_RESOLUTION_FULL,
		DECODED_RESOLUTION_HALF,
		DECODED_RESOLUTION_QUARTER
	};

	int reduction = 0;
	int max_reduction = 2;

	// Output height can be negative for inverted RGB
	output_height = abs(output_height);
#if 1
	// Always decode to the next larger size
	while (decoded_width > output_width &&
		   decoded_height > output_height &&
		   reduction < max_reduction)
	{
		// Decode to a frame size that is larger than the output image
		int reduced_width = decoded_width / 2;
		int reduced_height = decoded_height / 2;

		if (reduced_width >= output_width && reduced_height >= output_height)
		{
			decoded_width = reduced_width;
			decoded_height = reduced_height;

			reduction++;
		}
		else
		{
			break;
		}
	}
#else
	while (decoded_width*4 > output_width*5 &&
		   decoded_height*4 > output_height*5 &&
		   reduction < max_reduction)
	{
#if 0
		// Decode to a frame size that is larger than the output image
		int reduced_width = decoded_width / 2;
		int reduced_height = decoded_height / 2;

		if (reduced_width >= output_width && reduced_height >= output_height)
		{
			decoded_width = reduced_width;
			decoded_height = reduced_height;

			reduction++;
		}
		else
		{
			break;
		}
#else
		// Better to scale up a smaller image than scale down a larger image
		decoded_width /= 2;
		decoded_height /= 2;
		reduction++;
#endif
	}
#endif
	// Check that the decoded resolution is valid
	assert(0 <= reduction && reduction <= max_reduction);

	return decodedResolution[reduction];
}

void ComputeDecodedDimensions(int encoded_width, int encoded_height, int decoded_resolution,
							  int *decoded_width_out, int *decoded_height_out)
{
	switch (decoded_resolution)
	{
	default:
		assert(0);

	case DECODED_RESOLUTION_FULL:
		*decoded_width_out = encoded_width;
		*decoded_height_out = encoded_height;
		break;

	case DECODED_RESOLUTION_HALF:
		*decoded_width_out = encoded_width / 2;
		*decoded_height_out = encoded_height / 2;
		break;

	case DECODED_RESOLUTION_QUARTER:
		*decoded_width_out = encoded_width / 4;
		*decoded_height_out = encoded_height / 4;
		break;

	case DECODED_RESOLUTION_LOWPASS_ONLY:
		//TODO: Check that the lowpass dimensions are correct
		*decoded_width_out = encoded_width / 8;
		*decoded_height_out = encoded_height / 8;
		break;
	}
}

// Return true if the specified resolution is supported
bool IsDecodedResolution(int resolution)
{
	if (resolution == DECODED_RESOLUTION_QUARTER) {
		return true;
	}

	return (resolution == DECODED_RESOLUTION_FULL ||
			resolution == DECODED_RESOLUTION_HALF);
}

// Return true if the encoded sample is a key frame
bool IsSampleKeyFrame(uint8_t *sample, size_t size)
{
	bool key_frame_flag = false;

	// Search the first twenty tags for the sample type
	const int num_tags = 20;
	int i;

	BITSTREAM bitstream;
	InitBitstreamBuffer(&bitstream, sample, size, BITSTREAM_ACCESS_READ);

	for (i = 0; i < num_tags && size > 0; i++, size -= sizeof(TAGVALUE))
	{
		TAGVALUE segment = GetSegment(&bitstream);
		if (segment.tuple.tag == CODEC_TAG_SAMPLE)
		{
			switch (segment.tuple.value)
			{
			case SAMPLE_TYPE_GROUP:
			case SAMPLE_TYPE_FIRST:
			case SAMPLE_TYPE_IFRAME:
				key_frame_flag = true;
				break;

			case SAMPLE_TYPE_SEQUENCE_HEADER:
			case SAMPLE_TYPE_FRAME:
			case SAMPLE_TYPE_SECOND:
			case SAMPLE_TYPE_PFRAME:
			default:
				key_frame_flag = false;
				break;

			case SAMPLE_TYPE_GROUP_TRAILER:
			case SAMPLE_TYPE_NONE:
			case SAMPLE_TYPE_ERROR:
			case SAMPLE_TYPE_CHANNEL:
				assert(0);					// Unexpected situation
				key_frame_flag = false;		// Report the sample as a non-key frame
				break;
			}

			break;		// Found the sample type
		}
	}

	return key_frame_flag;
}

// Return the number of the more recent decoded frame
uint32_t DecodedFrameNumber(DECODER *decoder)
{
	CODEC_STATE *codec = &decoder->codec;
	if (decoder == NULL) return 0;
	return codec->frame_number;
}


/***** Start of the new code for the finite state machine (FSM) decoder *****/

#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

static inline void ZeroHighPassRow(PIXEL *rowptr, int length)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// This version assumes that the row is a multiple of 8 bytes
static inline void ZeroHighPassRow(PIXEL *rowptr, int length)
{
	int count;

	// Check that the row starts on a 16-byte boundary
	//assert(ISALIGNED(rowptr, 16));

	// Check that the row length (in bytes) is a multiple of 8 byte blocks
	assert(ISALIGNED(length, 8));

	// Convert the length from pixels to 8-byte blocks
	count = (length >> 3);

	// This code assumes that at least one 8-byte block will be zeroed
	assert(count > 0);

	__asm
	{
	        pxor    mm0, mm0			// Zero a 16 byte register
	        mov     eax, rowptr			// Load the pointer to the memory block
	        mov     ebx, count			// Load the count of 8-byte blocks

	loop:	movq	[eax], mm0			// Write 8 bytes of zeros
			add		eax, 8				// Advance to the next 8 byte block
			sub		ebx, 1				// Decrement the number of blocks
			jg		loop
	}

	//_mm_empty();
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

 #ifndef  _WIN64
// This version assumes that the row is a multiple of 16 bytes
static inline void ZeroHighPassRow(PIXEL *rowptr, int length)
{
	int count;

	// Check that the row starts on a 16-byte boundary
	assert(ISALIGNED(rowptr, 16));

	// Check that the row length (in bytes) is a multiple of 16 byte blocks
	assert(ISALIGNED(length, 16));

	// Convert the length from pixels to 16-byte blocks
	count = (length >> 4);

	// This code assumes that at least one 16-byte block will be zeroed
	assert(count > 0);

#if 1 //DANREMOVE

	memset(rowptr, 0, length);

#else

	__asm
	{
	        pxor    xmm0, xmm0			// Zero a 16 byte register
	        mov     eax, rowptr			// Load the pointer to the memory block
	        mov     ebx, count			// Load the count of 16-byte blocks

	loop:	movdqa		[eax], xmm0			// Write 16 bytes of zeros
			add		eax, 16				// Advance to the next 16 byte block
			sub		ebx, 1				// Decrement the number of blocks
			jg		loop
	}

#endif
}
 #else
// This version assumes that the row is a multiple of 16 bytes
static inline void ZeroHighPassRow(PIXEL *rowptr, int length)
{
	// Check that the row starts on a 16-byte boundary
	assert(ISALIGNED(rowptr, 16));

	// Check that the row length (in bytes) is a multiple of 16 byte blocks
	assert(ISALIGNED(length, 16));


	memset(rowptr, 0, length);
}
 #endif
#endif

#if (0 && _DEBUG)

// Functions for the finite state machine decoder (debug version)

static FSMENTRY *GetFSMTableEntry(FSM *fsm, int index)
{
	// Return the address of the next table entry in the finite state machine
	return &fsm->next_state[index];
}

static void ResetFSM(FSM *fsm)
{
	// Reset the state to the beginning of the finite state machine entries
	fsm->next_state = fsm->entries;
}

static void UpdateFSM(FSM *fsm, int next)
{
	// Change the state pointer to the next block of table entries
	fsm->next_state = fsm->entries + (next << FSM_INDEX_SIZE);
}

#else

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

#endif


#if _DEBUG

static void DebugOutputFSMEntry(FSM *fsm, int index, FSMENTRY *entry)
{
	int pre_skip = (entry->pre_post_skip & 0xFFF);
	int post_skip = (entry->pre_post_skip >> 12);

	// Remove companding
	int value0 = entry->value0 / 32;
	int value1 = entry->value1 / 32;

	// Convert the index to start at the beginning of the table
	index += (int)(fsm->next_state - fsm->table.entries[0]);
}

static void DebugOutputFSMEntryFast(FSM *fsm, int index, FSMENTRYFAST *entry)
{
	int pre_skip = (entry->pre_post_skip & 0xFFF);
	int post_skip = (entry->pre_post_skip >> 12);

	// Remove companding
	int value0 = (entry->values >> 16) / 32;
	int value1 = (entry->values & 0xFFFF) / 32;

	// Convert the index to start at the beginning of the table
	index += (int)(fsm->next_state - fsm->table.entries[0]);
}

static void DebugOutputFSM(FSM *fsm)
{
	int num_entries = FSM_INDEX_ENTRIES;
	int i;

	for (i = 0; i < num_entries; i++)
	{
		FSMENTRY *entry = &fsm->table.entries[0][i];
		int pre_skip = (entry->pre_post_skip & 0xFFF);
		int post_skip = (entry->pre_post_skip >> 12);
	}
}

static void PrintFSMEntry(FSM *fsm, int index, FSMENTRY *entry, FILE *logfile)
{
	int pre_skip = (entry->pre_post_skip & 0xFFF);
	int post_skip = (entry->pre_post_skip >> 12);

	// Remove companding
	int value0 = entry->value0 / 32;
	int value1 = entry->value1 / 32;

	// Convert the index to start at the beginning of the table
	index += (int)(fsm->next_state - fsm->table.entries[0]);

	if (logfile) {
		fprintf(logfile, "%d, %d, %d, %d, %d\n", index, value0, value1, pre_skip, post_skip);
	}
}

static void PrintFSMEntryFast(FSM *fsm, int index, FSMENTRYFAST *entry, FILE *logfile)
{
	int pre_skip = (entry->pre_post_skip & 0xFFF);
	int post_skip = (entry->pre_post_skip >> 12);

	// Remove companding
	int value0 = (entry->values >> 16) / 32;
	int value1 = (entry->values & 0xFFFF) / 32;

	// Convert the index to start at the beginning of the table
	index += (int)(fsm->next_state - fsm->table.entries[0]);

	if (logfile) {
		fprintf(logfile, "%d, %d, %d, %d, %d\n", index, value0, value1, pre_skip, post_skip);
	}
}

#endif


static inline int GetFastByte(BITSTREAM *stream)
{
	// Inline of the third case of GetByte
	uint8_t  *lpCurrentWord = stream->lpCurrentWord;

	// Get the next byte from the bitstream
	int byte = (uint32_t )(*(lpCurrentWord++));

	// Update the state of the bitstream
	stream->lpCurrentWord = lpCurrentWord;

#if ERROR_TOLERANT
	// Update the count of bytes used
	stream->nWordsUsed--;
#endif

	// Check that the high bits are zero
	assert((byte & ~BITMASK(8)) == 0);

	return byte;
}

#if 0
static inline int GetFastShort(BITSTREAM *stream)
{
	// Adaptation of the code in GetByte
	uint8_t  *lpCurrentWord = stream->lpCurrentWord;

	// Get the next byte from the bitstream
	int byte = (uint32_t )(lpCurrentWord[0]);

	int word = (byte << 8) | (uint32_t )(lpCurrentWord[1]);

	// Update the state of the bitstream
	stream->lpCurrentWord = lpCurrentWord+2;

	// Check that the high bits are zero
	assert((word & ~BITMASK(16)) == 0);

	return word;
}
#endif

// Must declare the byte swap function even though it is an intrinsic
//int _bswap(int);

#if 0
static inline int GetFastLong(BITSTREAM *stream)
{
	uint32_t  *lpCurrentWord = (uint32_t  *)stream->lpCurrentWord;
	int word = *(lpCurrentWord)++;
	//word = _bswap(word);
	word = SwapInt32BtoN(word);
	stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;

	return word;
}
#endif

#if 0 //DAN20041030 not used
// Decode a subband using FSM. One byte is read from the bitstream each time and decoded in two steps
// Original version that does not use a separate buffer for decoding
bool DecodeBandFSM(FSM *fsm, BITSTREAM *stream, PIXEL *image, int width, int height, int pitch, int quantization)
{
	int index, byte;
	FSMENTRY *entry;
	PIXEL *rowptr = image;
	int column = 0;
	int32_t value;
	size_t bytes_row_size = width * sizeof(PIXEL);
	PIXEL *maxptr;
	int length = width * sizeof(PIXEL);
	//ROI roi = {width, 1};


	// This version of Huffman decoder assumes that one byte
	// is processed as two 4-bit chunks
	assert(BITSTREAM_WORD_SIZE == FSM_INDEX_SIZE*2);

	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	// Convert the pitch to units of pixels
	pitch /= sizeof(PIXEL);

	// Compute the address of the row after the last row in the band
	maxptr = rowptr + height * pitch;

	// Round up the row length (in bytes) to a multiple of 16 bytes
	length = ALIGN16(length);

#if (0 && DEBUG)
	zerorow_count = 0;
#endif

	ZeroHighPassRow(rowptr, length);

	// Decode runs and magnitude values until the band end trailer is decoded
	for (;;)
	{
		// Read a byte from the bitstream
		byte = GetFastByte(stream);

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

		// Return when the entire band is decoded
		if (entry->value0 == BAND_END_TRAILER) {
			// Zero out the whole subband from here on
			rowptr += pitch;
			while(rowptr < maxptr) {
				ZeroHighPassRow(rowptr, length);
				rowptr += pitch;
			}
			ResetFSM(fsm);
			return true;
		}

		// set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// If no magnitude value is decoded
		if (entry->value0 == 0) {
			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow(rowptr, length);
			}
		}
		// If there is only one decoded magnitude value
		else if(entry->value1 == 0) {
			// Undo quantization and scaling
			value = quantization * entry->value0;

			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow(rowptr, length);
			}

			// Fill in the decoded magnitude

			// Check the column before storing the value
			//assert(index < width);
			assert(0 <= column && column < width);

			// Store the saturated value at the position found in the scan
			rowptr[column] = SATURATE(value);

			column += entry->post_skip;

			// Did the scan go beyond the end of the row?
			if (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow(rowptr, length);
			}
		}
		// If there are two decoded magnitude values
		else
		{
			// Check the column before storing values
			assert(0 <= column && column < width);

			if(column < width-1) {

				value = quantization * entry->value0;
				rowptr[column++] = SATURATE(value);
				value = quantization * entry->value1;
				rowptr[column++] = SATURATE(value);
			}
			else {
				value = quantization * entry->value0;
				rowptr[column] = SATURATE(value);
				value = quantization * entry->value1;
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow(rowptr, length);

				column = 0;
				rowptr[column++] = SATURATE(value);
			}
		}

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER) {
			// Zero out the whole subband from here on
			rowptr += pitch;
			while(rowptr < maxptr) {
				ZeroHighPassRow(rowptr, length);
				rowptr += pitch;
			}
			ResetFSM(fsm);
			return true;
		}

		// set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// If no magnitude value is decoded
		if (entry->value0 == 0) {
			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow(rowptr, length);
			}
		}
		// If there is only one decoded magnitude value
		else if (entry->value1 == 0) {
			// Undo quantization and scaling
			int32_t value = quantization * entry->value0;

			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow(rowptr, length);
			}

			// Fill in the decoded magnitude

			// Check the column before storing the value
			//assert(index < width);
			assert(0 <= column && column < width);

			// Store the saturated value at the position found in the scan
			rowptr[column] = SATURATE(value);

			column += entry->post_skip;

			// Did the scan go beyond the end of the row?
			if (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow(rowptr, length);
			}
		}
		// If there are two decoded magnitude values
		else
		{
			// Check the column before storing values
			assert(0 <= column && column < width);

			if(column < width-1) {
				value = quantization * entry->value0;
				rowptr[column++] = SATURATE(value);
				value = quantization * entry->value1;
				rowptr[column++] = SATURATE(value);
			}
			else {
				value = quantization * entry->value0;
				rowptr[column] = SATURATE(value);
				value = quantization * entry->value1;
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow(rowptr, length);

				column = 0;
				rowptr[column++] = SATURATE(value);
			}
		}
	}
}
#endif

// Decode a subband of highpass coefficients using a finite state machine.
// One byte is read from the bitstream each time and decoded in two steps.
// New version that uses a buffer aligned to the cache for decoding.

#if 0
static inline void ZeroHighPassBuffer(PIXEL *ptrCacheLines, int numCacheLines)
{
	// This routine assume that the cache line size is 64 bytes
	assert(_CACHE_LINE_SIZE == 64);

	// This routine assumes that the input pointer is aligned to a cache line
	assert(ISALIGNED(ptrCacheLines, _CACHE_LINE_SIZE));

	// This routine assumes that at least one cache line will be written
	assert(numCacheLines > 0);

#if __GNUC__

	memset(ptrCacheLines, 0, numCacheLines * _CACHE_LINE_SIZE);

#else

	__asm
	{
	        pxor    xmm0, xmm0				// Zero a 16 byte register
	        mov     eax, ptrCacheLines		// Load the pointer to the memory block
	        mov     ebx, numCacheLines		// Load the count of the number of cache lines

	loop:	movdqa	[eax], xmm0				// Write 64 bytes of zeros using aligned stores
			movdqa	[eax+16], xmm0
			movdqa	[eax+32], xmm0
			movdqa	[eax+48], xmm0

			add		eax, 64					// Advance to the next cache line
			sub		ebx, 1					// Decrement the number of cache lines
			jg		loop
	}
#endif

	// The routine returns the pointer to the cache line after zeroing the block
}
#endif
#if 0
static inline void CopyRowBuffer(char *rowptr, PIXEL *buffer, int length)
{
	// Note that the length is in units of bytes (not pixels)

	int count;		// Number of 16-byte blocks to copy

	// Check that the row length is an integer multiple of 16-byte blocks
	assert(ISALIGNED(length, 16));

	// Convert the row length to the number of 16-byte blocks to copy
	count = length >> 4;

	// This routine assumes that at least one 16 byte block will be copied
	assert(count > 0);

#if __GNUC__

	// Use standard memory copy
	memcpy(rowptr, buffer, length);

#else

	// Copy a multiple of 16 byte blocks
	__asm
	{
			mov		eax, rowptr			// Load the pointer to the destination
			mov		ebx, buffer			// Load the pointer to the source
			mov		ecx, count			// Load the number of 16-byte blocks to copy

	loop:	movdqa	xmm0, [ebx]			// Load 16 bytes from the source
			movntdq	[eax], xmm0			// Copy 16 bytes to the destination
			add		eax, 16				// Advance to the group of 16 bytes
			add		ebx, 16
			sub		ecx, 1				// Decrement the number of blocks to copy
			jg		loop
	}

#endif
}
#endif

// DecodeBandFSMBuffered is no longer used
#if 0 //dan20041030 not used
bool DecodeBandFSMBuffered(FSM *fsm, BITSTREAM *stream, PIXEL *image,
						   int width, int height, int pitch,
						   int quantization, char *decoding_buffer, size_t decoding_buffer_size)
{
	char *rowptr = (char *)image;				// Pointer to current row
	char *maxptr = rowptr + height * pitch;		// Address of row after the last row
	FSMENTRY *entry;
	int index;
	int byte;
	int column = 0;
	int32_t value;
	size_t row_size;
	size_t cache_row_size;						// Size of a row in bytes
	int cache_line_count;						// Size of the buffer in cache lines
	PIXEL *buffer;								// Pixel pointer to the buffer
	int length;									// Length of row in bytes


	// Check that the processing size allows two chunks per byte
	assert(BITSTREAM_WORD_SIZE == FSM_INDEX_SIZE*2);

	// The bitstream buffer should be empty
	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	// Compute the number of cache lines used in the buffer
	row_size = width * sizeof(PIXEL);
	cache_row_size = ALIGN(row_size, _CACHE_LINE_SIZE);
	cache_line_count = (cache_row_size >> _CACHE_LINE_SHIFT);

	// Check that the buffer is large enough
	assert(decoding_buffer != NULL && decoding_buffer_size >= cache_row_size);

	// Check that the buffer starts on a cache line boundary
	assert(ISALIGNED(decoding_buffer, _CACHE_LINE_SIZE));

	// This routine assumes that the rows are contiguous and the pitch is a multiple of 16 bytes
	length = pitch;
	assert(length == ALIGN(row_size, 16));

	// Cast the buffer pointer for pixel access
	buffer = (PIXEL *)decoding_buffer;

	// Zero the decoding buffer
	ZeroHighPassBuffer(buffer, cache_line_count);

	// Decode runs and magnitude values until the band end trailer is decoded
	for (;;)
	{
		// Read a byte from the bitstream
		byte = GetFastByte(stream);

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

		// Return when the entire band is decoded
		if (entry->value0 == BAND_END_TRAILER)
		{
			// Copy the buffer to the row if not already beyond the band
			if (rowptr < maxptr) CopyRowBuffer(rowptr, buffer, length);

			// Advance to the next row
			rowptr += pitch;

			// Zero the remaining rows in the subband
			while (rowptr < maxptr) {
				ZeroHighPassRow((PIXEL *)rowptr, length);
				rowptr += pitch;
			}

			// Reset the finite state machine to the root node in the Huffman tree
			ResetFSM(fsm);

			// Return indication that the band was fully decoded
			return true;
		}

		// Set the finite state machine to the next state in the Huffman tree
		UpdateFSM(fsm, entry->next_state);

		// No magnitude values decoded?
		if (entry->value0 == 0)
		{
			// No magnitudes decoded so just advance the column pointer
			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				assert(rowptr < maxptr);
				CopyRowBuffer(rowptr, buffer, length);
				rowptr += pitch;

				// Zero the decoding buffer if there are more rows to process
				if (rowptr < maxptr) ZeroHighPassBuffer(buffer, cache_line_count);
			}
		}
		// Only one magnitude value decoded?
		else if (entry->value1 == 0)
		{
			// Process the magnitude value that was decoded

			// Undo quantization and scaling
			value = quantization * entry->value0;

			// Advance to the column where the value should be placed
			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				assert(rowptr < maxptr);
				CopyRowBuffer(rowptr, buffer, length);
				rowptr += pitch;

				// Zero the decoding buffer if there are more rows to process
				if (rowptr < maxptr) ZeroHighPassBuffer(buffer, cache_line_count);
			}

			// Fill in the decoded magnitude

			// Check the column before storing the value
			assert(0 <= column && column < width);

			// Store the saturated value at the position found in the scan
			buffer[column] = SATURATE(value);

			column += entry->post_skip;

			// Did the scan go beyond the end of the row?
			if (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				assert(rowptr < maxptr);
				CopyRowBuffer(rowptr, buffer, length);
				rowptr += pitch;

				// Zero the decoding buffer if there are more rows to process
				if (rowptr < maxptr) ZeroHighPassBuffer(buffer, cache_line_count);
			}
		}
		else	// Two magnitude values were decoded
		{
			// Check the column before storing values
			assert(0 <= column && column < width);

			if (column < width - 1) {
				// Dequantize and store the first value
				value = quantization * entry->value0;
				buffer[column++] = SATURATE(value);

				// Dequantize and store the second value
				value = quantization * entry->value1;
				buffer[column++] = SATURATE(value);
			}
			else {
				// Dequantize and store the first value in the current row
				value = quantization * entry->value0;
				buffer[column] = SATURATE(value);

				// Dequantize the second value
				value = quantization * entry->value1;

				// Advance to the next row
				assert(rowptr < maxptr);
				CopyRowBuffer(rowptr, buffer, length);
				rowptr += pitch;

				// Zero the decoding buffer if there are more rows to process
				if (rowptr < maxptr) ZeroHighPassBuffer(buffer, cache_line_count);

				// Reset the column to the beginning of the row
				column = 0;

				// Store the second value in the new row
				buffer[column++] = SATURATE(value);
			}
		}

		// Decode the second 4-bit chunk
		index = byte & FSM_INDEX_MASK;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			// Copy the buffer to the row if not already beyond the band
			if (rowptr < maxptr) CopyRowBuffer(rowptr, buffer, length);

			// Advance to the next row
			rowptr += pitch;

			// Zero the remaining rows in the subband
			while (rowptr < maxptr) {
				ZeroHighPassRow((PIXEL *)rowptr, length);
				rowptr += pitch;
			}

			// Reset the finite state machine to the root node in the Huffman tree
			ResetFSM(fsm);

			// Return indication that the band was fully decoded
			return true;
		}

		// Set the finite state machine to the next state in the Huffman tree
		UpdateFSM(fsm, (int)entry->next_state);

		// If no magnitude value is decoded
		if (entry->value0 == 0) {
			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				assert(rowptr < maxptr);
				CopyRowBuffer(rowptr, buffer, length);
				rowptr += pitch;

				// Zero the decoding buffer if there are more rows to process
				if (rowptr < maxptr) ZeroHighPassBuffer(buffer, cache_line_count);
			}
		}
		// If there is only one decoded magnitude value
		else if (entry->value1 == 0) {
			// Undo quantization and scaling
			int32_t value = quantization * entry->value0;

			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				assert(rowptr < maxptr);
				CopyRowBuffer(rowptr, buffer, length);
				rowptr += pitch;

				// Zero the decoding buffer if there are more rows to process
				if (rowptr < maxptr) ZeroHighPassBuffer(buffer, cache_line_count);
			}

			// Fill in the decoded magnitude

			// Check the column before storing the value
			//assert(index < width);
			assert(0 <= column && column < width);

			// Store the saturated value at the position found in the scan
			buffer[column] = SATURATE(value);

			column += entry->post_skip;

			// Did the scan go beyond the end of the row?
			if (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				assert(rowptr < maxptr);
				CopyRowBuffer(rowptr, buffer, length);
				rowptr += pitch;

				// Zero the decoding buffer if there are more rows to process
				if (rowptr < maxptr) ZeroHighPassBuffer(buffer, cache_line_count);
			}
		}
		// If there are two decoded magnitude values
		else
		{
			// Check the column before storing values
			assert(0 <= column && column < width);

			if (column < width-1) {
				value = quantization * entry->value0;
				buffer[column++] = SATURATE(value);

				value = quantization * entry->value1;
				buffer[column++] = SATURATE(value);
			}
			else {
				value = quantization * entry->value0;
				buffer[column] = SATURATE(value);

				value = quantization * entry->value1;

				// Advance to the next row
				assert(rowptr < maxptr);
				CopyRowBuffer(rowptr, buffer, length);
				rowptr += pitch;

				// Zero the decoding buffer if there are more rows to process
				if (rowptr < maxptr) ZeroHighPassBuffer(buffer, cache_line_count);

				// Reset the column to the beginning of the row
				column = 0;

				buffer[column++] = SATURATE(value);
			}
		}
	}
}
#endif

#if 0 //dan20041030 not used
// Decode a subband using FSM, combine the two results decoded from one byte
bool DecodeBandFSMCombined(FSM *fsm, BITSTREAM *stream, PIXEL *image, int width, int height, int pitch, int quantization)
{
	int index, skip;
	uint8_t byte;
	FSMENTRY *entry1, *entry2;
	PIXEL *rowptr = image;
	int row = 0, column = 0;
	int32_t value,bytes_row_size = width*sizeof(PIXEL);
	PIXEL *maxptr = rowptr + height*pitch;

	// This Huffman decoder assumes each byte is processed as two 4-bit chunks
	assert(BITSTREAM_WORD_SIZE == FSM_INDEX_SIZE*2);

	ZeroHighPassRow(rowptr, width);

	// Double check that the bitstream buffer is empty
	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	// Decode runs and magnitude values until the band end trailer is decoded
	for (;;)
	{
		// Read a byte from the bitstream
		//byte = GetBits(stream, BITSTREAM_WORD_SIZE);
#if 0
		byte = GetByte(stream);

		if (stream->error != BITSTREAM_ERROR_OKAY) {
			stream->error = VLC_ERROR_NOTFOUND;
			return false;
		}
#else
		// Inline of the third case of GetByte
		uint8_t  *lpCurrentWord = stream->lpCurrentWord;

		// Get the next byte from the bitstream
		byte = (uint32_t )(*(lpCurrentWord++));

		// Update the state of the bitstream
		stream->lpCurrentWord = lpCurrentWord;

		// Check that the high bits are zero
		assert((byte & ~BITMASK(8)) == 0);
#endif

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;
		entry1 = GetFSMTableEntry(fsm, index);
		UpdateFSM(fsm, entry1->next_state);

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);
		entry2 = GetFSMTableEntry(fsm, index);
		UpdateFSM(fsm, entry2->next_state);

		// Return when the subband is completely decoded
		if(entry1->value0 == BAND_END_TRAILER || entry2->value0 == BAND_END_TRAILER) {
			ResetFSM(fsm);
			return true;
		}

		// If no magnitude value is decoded at the first step
		if (entry1->value0 == 0) {
			// If no magnitude is decoded at the second step
			if(entry2->value0 == 0) {
				column += entry1->pre_skip+entry2->pre_skip;

				// Did the scan go beyond the end of the row?
				while (column >= width)
				{
					// Compute the starting column for the next row
					column -= width;

					// Advance to the next row
					rowptr += pitch;

					if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);
				}
			}

			// If one magnitude is decoded at the second step
			else if(entry2->value1 == 0) {
				// Skip to the non-zero position
				column += entry1->pre_skip+entry2->pre_skip;

				// Did the scan go beyond the end of the row?
				while (column >= width)
				{
					// Compute the starting column for the next row
					column -= width;

					// Advance to the next row
					rowptr += pitch;

					if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);
				}

				// Fill in the decoded magnitude

				// Undo quantization and scaling
				value = quantization * entry2->value0;

				// Check the column before storing the value
				//assert(index < width);
				assert(0 <= column && column < width);

				// Store the saturated value
				rowptr[column] = SATURATE(value);

				column += entry2->post_skip;

				// Did the scan go beyond the end of the row?
				if (column >= width)
				{
					// Compute the starting column for the next row
					column -= width;

					// Advance to the next row
					rowptr += pitch;

					if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);
				}
			}
			// If two magnitudes are decoded at the second step
			else {
				column += entry1->pre_skip;

				// Did the scan go beyond the end of the row?
				while (column >= width)
				{
					// Compute the starting column for the next row
					column -= width;

					// Advance to the next row
					rowptr += pitch;

					if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);
				}

				// Check the column before storing values
				assert(0 <= column && column < width);

				if(column < width-1) {
					value = quantization * entry2->value0;
					rowptr[column++] = SATURATE(value);
					value = quantization * entry2->value1;
					rowptr[column++] = SATURATE(value);
				}
				else {
					value = quantization * entry2->value0;
					rowptr[column] = SATURATE(value);
					value = quantization * entry2->value1;
					rowptr += pitch;

					if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);

					column = 0;
					rowptr[column++] = SATURATE(value);
				}
			}
		}

		// If only one magnitude is decoded at the first step
		else if(entry1->value1 == 0) {
			// Undo quantization and scaling
			value = quantization * entry1->value0;

			column += entry1->pre_skip;

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);
			}

			// Fill in the decoded magnitude

			// Check the column before storing the value
			//assert(index < width);
			assert(0 <= column && column < width);

			// Store the saturated value at the position found in the scan
			rowptr[column] = SATURATE(value);

			// If no magnitude is decoded at the second step
			if(entry2->value0 == 0) {
				column += entry1->post_skip+entry2->pre_skip;

				// Did the scan go beyond the end of the row?
				while (column >= width)
				{
					// Compute the starting column for the next row
					column -= width;

					// Advance to the next row
					rowptr += pitch;

					if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);
				}
			}

			// If one magnitude is decoded at the second step
			else if (entry2->value1 == 0)
			{
				// Undo quantization and scaling
				value = quantization * entry2->value0;

				column += entry1->post_skip+entry2->pre_skip;

				// Did the scan go beyond the end of the row?
				while (column >= width)
				{
					// Compute the starting column for the next row
					column -= width;

					// Advance to the next row
					rowptr += pitch;

					if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);
				}

				// Fill in the decoded magnitude

				// Check the column before storing the value
				assert(0 <= column && column < width);

				// Store the saturated value at the position found in the scan
				rowptr[column] = SATURATE(value);

				column += entry2->post_skip;

				// Did the scan go beyond the end of the row?
				if (column >= width)
				{
					// Compute the starting column for the next row
					column -= width;

					// Advance to the next row
					rowptr += pitch;

					if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);
				}
			}

			// If two magnitudes are decoded at the second step
			else
			{
				column += entry1->post_skip;

				// Did the scan go beyond the end of the row?
				if (column >= width)
				{
					// Compute the starting column for the next row
					column -= width;

					// Advance to the next row
					rowptr += pitch;

					if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);
				}

				// Check the column before storing values
				assert(0 <= column && column < width);

				if(column < width-1) {
					value = quantization * entry2->value0;
					rowptr[column++] = SATURATE(value);
					value = quantization * entry2->value1;
					rowptr[column++] = SATURATE(value);
				}
				else {
					value = quantization * entry2->value0;
					rowptr[column] = SATURATE(value);
					value = quantization * entry2->value1;
					rowptr += pitch;

					if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);

					column = 0;
					rowptr[column++] = SATURATE(value);
				}
			}
		}

		// If two magnitudes are decoded at the first step
		else {
			// Check the column before storing values
			assert(0 <= column && column < width);

			if(column < width-1) {
				value = quantization * entry1->value0;
				rowptr[column++] = SATURATE(value);
				value = quantization * entry1->value1;
				rowptr[column++] = SATURATE(value);
			}
			else {
				value = quantization * entry1->value0;
				rowptr[column] = SATURATE(value);
				value = quantization * entry1->value1;
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);

				column = 0;
				rowptr[column++] = SATURATE(value);
			}

			// If two magnitudes are decoded at the first step
			// then at most one more magnitude can be decoded at the second step
			assert(entry2->value1 == 0);

			// If no magnitude is decoded at the second step
			if(entry2->value0 == 0) {
				column += entry2->pre_skip; // entry2->pre_skip <=4 must be true

				// Did the scan go beyond the end of the row?
				if (column >= width)
				{
					// Compute the starting column for the next row
					column -= width;

					// Advance to the next row
					rowptr += pitch;

					if(rowptr < maxptr) ZeroHighPassRow(rowptr, width);
				}
			}

			// If one magnitude is decoded at the second step
			else {
				column += entry2->pre_skip;		// must be a small zero run

				// Did the scan go beyond the end of the row?
				if (column >= width)
				{
					// Compute the starting column for the next row
					column -= width;

					// Advance to the next row
					rowptr += pitch;

					if (rowptr < maxptr) ZeroHighPassRow(rowptr, width);
				}

				// Fill in the decoded magnitude

				// Undo quantization and scaling
				value = quantization * entry2->value0;

				// Check the column before storing the value
				assert(0 <= column && column < width);

				// Store the saturated value at the position found in the scan
				rowptr[column] = SATURATE(value);

				column += entry2->post_skip;

				// Did the scan go beyond the end of the row?
				if (column >= width)
				{
					// Compute the starting column for the next row
					column -= width;

					// Advance to the next row
					rowptr += pitch;

					if (rowptr < maxptr) ZeroHighPassRow(rowptr, width);
				}
			}
		}
	}
}
#endif


#if 0 //dan20041030 not used
// Decode a subband using FSM. One byte is read from the bitstream each time and decoded in two steps
// Original version that does not use a separate buffer for decoding
bool DecodeBandFSM8s(FSM *fsm, BITSTREAM *stream, PIXEL8S *image, int width, int height, int pitch)
{
	int index, byte;
	FSMENTRY *entry;
	PIXEL8S *rowptr = image;
	int column = 0;
	int32_t value;
	PIXEL8S *maxptr;
	int length = width * sizeof(PIXEL8S);
	//ROI roi = {width, 1};

	// This version of Huffman decoder assumes that one byte
	// is processed as two 4-bit chunks
	assert(BITSTREAM_WORD_SIZE == FSM_INDEX_SIZE*2);

	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	// Convert the pitch to units of pixels
	pitch /= sizeof(PIXEL8S);

	// Compute the address of the row after the last row in the band
	maxptr = rowptr + height * pitch;

	// Round up the row length (in bytes) to a multiple of 16 bytes
	length = ALIGN16(length);

	ZeroHighPassRow((PIXEL *)rowptr, length);

	// Decode runs and magnitude values until the band end trailer is decoded
	for (;;)
	{
		// Read a byte from the bitstream
		byte = GetFastByte(stream);

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

		// Return when the entire band is decoded
		if (entry->value0 == BAND_END_TRAILER) {
			// Zero out the whole subband from here on
			rowptr += pitch;
			while(rowptr < maxptr) {
				ZeroHighPassRow((PIXEL *)rowptr, length);
				rowptr += pitch;
			}
			ResetFSM(fsm);
			return true;
		}

		// set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// If no magnitude value is decoded
		if (entry->value0 == 0)
		{
			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow((PIXEL *)rowptr, length);
			}
		}
		// If there is only one decoded magnitude value
		else if(entry->value1 == 0)
		{
			value = entry->value0;

			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow((PIXEL *)rowptr, length);
			}

			// Fill in the decoded magnitude

			// Check the column before storing the value
			assert(0 <= column && column < width);

			// Store the saturated value at the position found in the scan
			rowptr[column] = SATURATE8S(value);

			column += entry->post_skip;

			// Did the scan go beyond the end of the row?
			if (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow((PIXEL *)rowptr, length);
			}
		}
		// If there are two decoded magnitude values
		else
		{
			// Check the column before storing values
			assert(0 <= column && column < width);

			if(column < width-1) {
				value = entry->value0;
				rowptr[column++] = SATURATE8S(value);
				value = entry->value1;
				rowptr[column++] = SATURATE8S(value);
			}
			else {
				value = entry->value0;
				rowptr[column] = SATURATE8S(value);
				value = entry->value1;
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow((PIXEL *)rowptr, length);

				column = 0;
				rowptr[column++] = SATURATE8S(value);
			}
		}

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			// Zero out the whole subband from here on
			rowptr += pitch;
			while(rowptr < maxptr) {
				ZeroHighPassRow((PIXEL *)rowptr, length);
				rowptr += pitch;
			}
			ResetFSM(fsm);
			return true;
		}

		// Set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// If no magnitude value is decoded
		if (entry->value0 == 0)
		{
			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow((PIXEL *)rowptr, length);
			}
		}
		// If there is only one decoded magnitude value
		else if (entry->value1 == 0)
		{
			value = entry->value0;

			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow((PIXEL *)rowptr, length);
			}

			// Fill in the decoded magnitude

			// Check the column before storing the value
			assert(0 <= column && column < width);

			// Store the saturated value at the position found in the scan
			rowptr[column] = SATURATE8S(value);

			column += entry->post_skip;

			// Did the scan go beyond the end of the row?
			if (column >= width)
			{
				// Compute the starting column for the next row
				column -= width;

				// Advance to the next row
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow((PIXEL *)rowptr, length);
			}
		}
		// If there are two decoded magnitude values
		else
		{
			// Check the column before storing values
			assert(0 <= column && column < width);

			if(column < width-1) {
				value = entry->value0;
				rowptr[column++] = SATURATE8S(value);
				value = entry->value1;
				rowptr[column++] = SATURATE8S(value);
			}
			else {
				value = entry->value0;
				rowptr[column] = SATURATE8S(value);
				value = entry->value1;
				rowptr += pitch;

				if(rowptr < maxptr) ZeroHighPassRow((PIXEL *)rowptr, length);

				column = 0;
				rowptr[column++] = SATURATE8S(value);
			}
		}
	}
}
#endif


// same as DecodeBandFSM8sNoGap but output to 16bit data
bool DecodeBandFSM16sNoGap2Pass(FSM *fsm, BITSTREAM *stream, PIXEL16S *image, int width, int height, int pitch, int quant)
{
	int index, byte;
	FSMENTRY *entry;
	PIXEL *rowptr = (PIXEL *)image;
	PIXEL16S *bandendptr;
	int value;

#if ERROR_TOLERANT
	uint8_t  *startCurrentWord = stream->lpCurrentWord;
	int32_t startWordsUsed = stream->nWordsUsed;
#endif


#if _FSMBUFFER
	__declspec(align(32)) FSMENTRY buffer;
#endif

	if (image == NULL) {
		return false;
	}

	// Reset the decoder
	ResetFSM(fsm);

	pitch /= sizeof(PIXEL16S);

	// Zero out the entire subband
	ZeroHighPassRow((PIXEL *)rowptr, pitch*height*sizeof(PIXEL16S));

	// This Huffman decoder assumes each byte is processed as two 4-bit chunks
	assert(BITSTREAM_WORD_SIZE == 2 * FSM_INDEX_SIZE);

	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	bandendptr = rowptr + height * pitch;


#if 0  // test for errors.
	{
		if((rand() % 10) == 1)
			stream->lpCurrentWord[rand()%50] ^= 1;
	}
#endif



	// Decode runs and magnitude values until the entire band is decoded
#if ERROR_TOLERANT
	while((intptr_t)bandendptr - (intptr_t)rowptr >= 0)
#else
	for (;;)
#endif
	{
		// Read a byte from the bitstream
#if ERROR_TOLERANT
		if(stream->nWordsUsed)
		{
			byte = GetFastByte(stream);
		}
		else
		{
			break;
		}
#else
		byte = GetFastByte(stream);
#endif

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

#if _FSMBUFFER
		memcpy(&buffer, entry, sizeof(FSMENTRY));
		entry = &buffer;
#endif

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			assert(rowptr <= bandendptr);
			ResetFSM(fsm);
			goto SecondPass;
		}

		// Set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip & 0xfff];

		// Write down the first decoded magnitude
		value = entry->value0;
		rowptr[0] = value;//SATURATE(value);

		// Write down the second decoded magnitude
		value = entry->value1;
		rowptr[1] = value;//SATURATE(value);

		// Skip the appropriate distance
		rowptr = &rowptr[entry->pre_post_skip >> 12];

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

#if _FSMBUFFER
		memcpy(&buffer, entry, sizeof(FSMENTRY));
		entry = &buffer;
#endif

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			assert(rowptr <= bandendptr);
			ResetFSM(fsm);
			goto SecondPass;
		}

		// set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip & 0xfff];

		// Write down the first decoded magnitude
		value = entry->value0;
		rowptr[0] = value;//SATURATE(value);

		// Write down the second decoded magnitude
		value = entry->value1;
		rowptr[1] = value;//SATURATE(value);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip >> 12];
	}




SecondPass:

	rowptr = (PIXEL16S *)image;

	AlignBits(stream);
	AlignBitsTag(stream);

	stream->lpCurrentWord += 4;
	stream->nWordsUsed -= 4;

	// Decode runs and magnitude values until the entire band is decoded
#if ERROR_TOLERANT
	while((intptr_t)bandendptr - (intptr_t)rowptr >= 0)
#else
	for (;;)
#endif
	{
		// Read a byte from the bitstream
#if ERROR_TOLERANT
		if(stream->nWordsUsed)
		{
			byte = GetFastByte(stream);
		}
		else
		{
			break;
		}
#else
		byte = GetFastByte(stream);
#endif

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

#if _FSMBUFFER
		memcpy(&buffer, entry, sizeof(FSMENTRY));
		entry = &buffer;
#endif

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			assert(rowptr <= bandendptr);
			ResetFSM(fsm);
			return true;
		}

		// Set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip & 0xfff];

		// Write down the first decoded magnitude
		value = entry->value0;
		rowptr[0] |= value << 8;

		// Write down the second decoded magnitude
		value = entry->value1;
		rowptr[1] |= value << 8;


		// Skip the appropriate distance
		rowptr = &rowptr[entry->pre_post_skip >> 12];

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

#if _FSMBUFFER
		memcpy(&buffer, entry, sizeof(FSMENTRY));
		entry = &buffer;
#endif

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			assert(rowptr <= bandendptr);
			ResetFSM(fsm);
			return true;
		}

		// set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip & 0xfff];

		// Write down the first decoded magnitude
		value = entry->value0;
		rowptr[0] |= value << 8;

		// Write down the second decoded magnitude
		value = entry->value1;
		rowptr[1] |= value << 8;

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip >> 12];
	}





#if ERROR_TOLERANT

	// Reset the decoder
	ResetFSM(fsm);

	// Backup the bitstream to the beginning of the band
	stream->lpCurrentWord = startCurrentWord;
	stream->nWordsUsed = startWordsUsed;

#if 0
	AlignBitsTag(stream);

	// Read the debugging marker
	{
		TAGVALUE segment;

		do
		{
			segment = GetTagValue(stream);
		}
		while(segment.tuple.tag != CODEC_TAG_BAND_TRAILER);

		stream->lpCurrentWord -= 4;
		stream->nWordsUsed += 4;
	}
#else
	SkipSubband(stream);
#endif
#endif
	return true;
}

// Same as DecodeBandFSM8sNoGap but output to 16bit data
#if _DEBUG
bool DecodeBandFSM16sNoGap(FSM *fsm, BITSTREAM *stream, PIXEL16S *image, int width, int height, int pitch, FILE *logfile)
#else
bool DecodeBandFSM16sNoGap(FSM *fsm, BITSTREAM *stream, PIXEL16S *image, int width, int height, int pitch)
#endif
{
	int index, byte;
	FSMENTRY *entry;
	FSMENTRYFAST *entryfast;
	PIXEL16S *rowptr = image;
	PIXEL16S *bandendptr;
	PIXEL16S *fastendptr;
	int32_t value;

	uint8_t  *startCurrentWord = stream->lpCurrentWord;
	uint8_t  *CurrentWord = stream->lpCurrentWord;
	int32_t startWordsUsed = stream->nWordsUsed;

	ptrdiff_t offset;

#if _FSMBUFFER
	__declspec(align(32)) FSMENTRY buffer;
#endif

#if (0 && DEBUG)
	DebugOutputBitstreamPosition(stream);
	DebugOutputBitstreamBytes(stream, 16);
#endif

	// Reset the decoder
	ResetFSM(fsm);

#if (0 && DEBUG)
	DebugOutputFSM(fsm);
#endif

	pitch /= sizeof(PIXEL16S);

	// Zero out the entire subband
	ZeroHighPassRow((PIXEL *)rowptr, pitch*height*sizeof(PIXEL16S));
	//memset(rowptr, 0, pitch*height*sizeof(PIXEL16S));

	// This Huffman decoder assumes each byte is processed as two 4-bit chunks
	assert(BITSTREAM_WORD_SIZE == 2 * FSM_INDEX_SIZE);

	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	bandendptr = rowptr + height * pitch;


#if 0  // test for errors.
	{
		if((rand() % 10) == 1)
			stream->lpCurrentWord[rand()%50] ^= 1;
	}
#endif

	fastendptr = bandendptr;
	fastendptr -= 500;

	// Decode runs and magnitude values until the entire band is decoded
	while(rowptr < fastendptr)
	{
		// Read a byte from the bitstream
		byte = *CurrentWord++;

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entryfast = (FSMENTRYFAST *)GetFSMTableEntry(fsm, index);

#if (0 && DEBUG)
		//DebugOutputFSMEntryFast(fsm, index, entryfast);
		PrintFSMEntryFast(fsm, index, entryfast, logfile);
#endif
		// Set the pointer to the next state
		UpdateFSM(fsm, (int)entryfast->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entryfast->pre_post_skip & 0xfff];

		// Write down the first decoded magnitude
		*((uint32_t *)rowptr) = entryfast->values;

		// Skip the appropriate distance
		rowptr = &rowptr[entryfast->pre_post_skip >> 12];

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);

		// Index into the lookup table at that state
		entryfast = (FSMENTRYFAST *)GetFSMTableEntry(fsm, index);

#if (0 && DEBUG)
		//DebugOutputFSMEntryFast(fsm, index, entryfast);
		PrintFSMEntryFast(fsm, index, entryfast, logfile);
#endif
		// set the pointer to the next state
		UpdateFSM(fsm, (int)entryfast->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entryfast->pre_post_skip & 0xfff];

		// Write down the first decoded magnitude
		*((uint32_t *)rowptr) = entryfast->values;

		// Skip the decoded zero runs
		rowptr = &rowptr[entryfast->pre_post_skip >> 12];
	}

	offset = CurrentWord - startCurrentWord;
	stream->lpCurrentWord += offset;
	stream->nWordsUsed -= (int)offset;

	// Decode runs and magnitude values until the entire band is decoded
#if ERROR_TOLERANT
	while(bandendptr >= rowptr)
#else
	for (;;)
#endif
	{
#if (0 && DEBUG)
		if (!(rowptr < bandendptr)) {
			return true;
		}
#endif

#if (0 && DEBUG)
		PrintBitstreamPosition(stream, logfile);
#endif

		// Read a byte from the bitstream
#if ERROR_TOLERANT
		if(stream->nWordsUsed)
		{
			byte = GetFastByte(stream);
		}
		else
		{
			break;
		}
#else
		byte = GetFastByte(stream);
#endif

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);
#if (0 && DEBUG)
		//DebugOutputFSMEntry(fsm, index, entry);
		PrintFSMEntry(fsm, index, entry, logfile);
#endif

#if _FSMBUFFER
		memcpy(&buffer, entry, sizeof(FSMENTRY));
		entry = &buffer;
#endif

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			assert(rowptr <= bandendptr);
			ResetFSM(fsm);
			return true;
		}

		// Set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip & 0xfff];

		// Write down the first decoded magnitude
		if ((value = entry->value0)) {
			rowptr[0] = value;//SATURATE(value);
		}

		// Write down the second decoded magnitude
		if ((value = entry->value1)) {
			rowptr[1] = value;//SATURATE(value);
		}

		// Skip the appropriate distance
		rowptr = &rowptr[entry->pre_post_skip >> 12];

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);
#if (0 && DEBUG)
		//DebugOutputFSMEntry(fsm, index, entry);
		PrintFSMEntry(fsm, index, entry, logfile);
#endif

#if _FSMBUFFER
		memcpy(&buffer, entry, sizeof(FSMENTRY));
		entry = &buffer;
#endif

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			assert(rowptr <= bandendptr);
			ResetFSM(fsm);
			return true;
		}

		// set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip & 0xfff];

		// Write down the first decoded magnitude
		if ((value = entry->value0)) {
			rowptr[0] = value;//SATURATE(value);
		}

		// Write down the second decoded magnitude
		if ((value = entry->value1)) {
			rowptr[1] = value;//SATURATE(value);
		}

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip >> 12];
	}

#if ERROR_TOLERANT

	// Reset the decoder
	ResetFSM(fsm);

	// Backup the bitstream to the beginning of the band
	stream->lpCurrentWord = startCurrentWord;
	stream->nWordsUsed = startWordsUsed;

#if 0
	AlignBitsTag(stream);

	// Read the debugging marker
	{
		TAGVALUE segment;

		do
		{
			segment = GetTagValue(stream);
		}
		while(segment.tuple.tag != CODEC_TAG_BAND_TRAILER);

		stream->lpCurrentWord -= 4;
		stream->nWordsUsed += 4;
	}
#else
	SkipSubband(stream);
#endif
#endif

	return true;
}

bool DecodeBandFSM16sNoGapWithPeaks(FSM *fsm, BITSTREAM *stream, PIXEL16S *image, int width, int height, int pitch, PIXEL *peaks, int level, int quant)
{
	int index, byte;
	FSMENTRY *entry;
	PIXEL16S *rowptr = image;
	PIXEL16S *bandendptr;
	PIXEL16S *fastendptr;
	int32_t value;

	uint8_t  *startCurrentWord = stream->lpCurrentWord;
	uint8_t  *CurrentWord = stream->lpCurrentWord;
	int32_t startWordsUsed = stream->nWordsUsed;

#if _FSMBUFFER
	__declspec(align(32)) FSMENTRY buffer;
#endif

	// Reset the decoder
	ResetFSM(fsm);

	//This is been called with non-prequantized FSM
	if(quant>1) level /= quant;

	pitch /= sizeof(PIXEL16S);

	// Zero out the entire subband
	ZeroHighPassRow((PIXEL *)rowptr, pitch*height*sizeof(PIXEL16S));

	// This Huffman decoder assumes each byte is processed as two 4-bit chunks
	assert(BITSTREAM_WORD_SIZE == 2 * FSM_INDEX_SIZE);

	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	bandendptr = rowptr + height * pitch;


#if 0  // test for errors.
	{
		if((rand() % 10) == 1)
			stream->lpCurrentWord[rand()%50] ^= 1;
	}
#endif

	fastendptr = bandendptr;
	fastendptr -= 1000;

	// Decode runs and magnitude values until the entire band is decoded
	while(rowptr < fastendptr)
	{
		// Read a byte from the bitstream
		byte = *CurrentWord++;

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

		// Set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip & 0xfff];

		// Write down the first decoded magnitude
		value = entry->value0;
		if(abs(value) > level)
			rowptr[0] = *peaks++ / quant;
		else
			rowptr[0] = value;//SATURATE(value);

		value = entry->value1;
		rowptr[1] = value;//SATURATE(value);

		// Skip the appropriate distance
		rowptr = &rowptr[entry->pre_post_skip >> 12];

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

		// set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip & 0xfff];

		// Write down the first decoded magnitude
		value = entry->value0;
		if(abs(value) > level)
			rowptr[0] = *peaks++ / quant;
		else
			rowptr[0] = value;//SATURATE(value);
		
		value = entry->value1;
		rowptr[1] = value;//SATURATE(value);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip >> 12];
	}

	stream->lpCurrentWord += ((intptr_t)CurrentWord - (intptr_t)startCurrentWord);
	stream->nWordsUsed -= (int)(((intptr_t)CurrentWord - (intptr_t)startCurrentWord));

	// Decode runs and magnitude values until the entire band is decoded
#if ERROR_TOLERANT
	while(((intptr_t)bandendptr - (intptr_t)rowptr) >= 0)
#else
	for (;;)
#endif
	{
#if (0 && DEBUG)
		if (!(rowptr < bandendptr)) {
			return true;
		}
#endif


		// Read a byte from the bitstream
#if ERROR_TOLERANT
		if(stream->nWordsUsed)
		{
			byte = GetFastByte(stream);
		}
		else
		{
			break;
		}
#else
		byte = GetFastByte(stream);
#endif

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

#if _FSMBUFFER
		memcpy(&buffer, entry, sizeof(FSMENTRY));
		entry = &buffer;
#endif

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			assert(rowptr <= bandendptr);
			ResetFSM(fsm);
			return true;
		}

		// Set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip & 0xfff];

		// Write down the first decoded magnitude
		value = entry->value0;
		if(abs(value) > level)
			rowptr[0] = *peaks++ / quant;
		else
			rowptr[0] = value;//SATURATE(value);

		// Write down the second decoded magnitude
		value = entry->value1;
		rowptr[1] = value;//SATURATE(value);

		// Skip the appropriate distance
		rowptr = &rowptr[entry->pre_post_skip >> 12];

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

#if _FSMBUFFER
		memcpy(&buffer, entry, sizeof(FSMENTRY));
		entry = &buffer;
#endif

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			assert(rowptr <= bandendptr);
			ResetFSM(fsm);
			return true;
		}

		// set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip & 0xfff];

		// Write down the first decoded magnitude
		value = entry->value0;
		if(abs(value) > level)
			rowptr[0] = *peaks++ / quant;
		else
			rowptr[0] = value;//SATURATE(value);

		// Write down the second decoded magnitude
		value = entry->value1;
		rowptr[1] = value;//SATURATE(value);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_post_skip >> 12];
	}

#if ERROR_TOLERANT

	// Reset the decoder
	ResetFSM(fsm);

	// Backup the bitstream to the beginning of the band
	stream->lpCurrentWord = startCurrentWord;
	stream->nWordsUsed = startWordsUsed;

#if 0
	AlignBitsTag(stream);

	// Read the debugging marker
	{
		TAGVALUE segment;

		do
		{
			segment = GetTagValue(stream);
		}
		while(segment.tuple.tag != CODEC_TAG_BAND_TRAILER);

		stream->lpCurrentWord -= 4;
		stream->nWordsUsed += 4;
	}
#else
	SkipSubband(stream);
#endif
#endif
	return true;
}

// This version of DecodeBandFSM() assumes that the gap between width and pitch has been coded as
// zero runs. Therefore decoded magnitude values can be written down without the need to check
// if the end of a row has been reached. Hence the total number of conditionals in DecodeBandFSM
// can be significantly reduced.

// Decode a subband using FSM. One byte is read from the bitstream each time and decoded in two steps
// Original version that does not use a separate buffer for decoding
#if !_INDIVIDUAL_ENTRY

#if 0 //dan20041030 not used
bool DecodeBandFSM8sNoGap(FSM *fsm, BITSTREAM *stream, PIXEL8S *image, int width, int height, int pitch)
{
	int index, byte;
	FSMENTRY *entry;
	PIXEL8S *rowptr = image;
	PIXEL8S *bandendptr;
	int32_t value;

#if _FSMBUFFER
	__declspec(align(32)) FSMENTRY buffer;
#endif

	pitch /= sizeof(PIXEL8S);

	// Zero out the entire subband
	ZeroHighPassRow((PIXEL *)rowptr, pitch*height);

	// This version of Huffman decoder assumes that one byte
	// is processed as two 4-bit chunks
	assert(BITSTREAM_WORD_SIZE == FSM_INDEX_SIZE*2);

	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	bandendptr = rowptr + height * pitch;

	// Decode runs and magnitude values until the entire band is decoded
	//while (rowptr < bandendptr)
	for (;;)
	{
#if (0 && DEBUG)
		if (!(rowptr < bandendptr)) {
			return true;
		}
#endif
		// Check that the decoder has not overrun the output array
		//assert(rowptr < bandendptr);

		// Read a byte from the bitstream
		byte = GetFastByte(stream);

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

#if _FSMBUFFER
		memcpy(&buffer, entry, sizeof(FSMENTRY));
		entry = &buffer;
#endif

#if 1
		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			assert(rowptr <= bandendptr);
			ResetFSM(fsm);
			return true;
		}
#endif

		// Set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_skip];

		// Write down the first decoded magnitude
		value = entry->value0;
		rowptr[0] = SATURATE(value);

		// Write down the second decoded magnitude
		value = entry->value1;
		rowptr[1] = SATURATE(value);

		// Skip the appropriate distance
		rowptr = &rowptr[entry->post_skip];

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

#if _FSMBUFFER
		memcpy(&buffer, entry, sizeof(FSMENTRY));
		entry = &buffer;
#endif

#if 1
		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			assert(rowptr <= bandendptr);
			ResetFSM(fsm);
			return true;
		}
#endif

		// set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_skip];

		// Write down the first decoded magnitude
		value = entry->value0;
		rowptr[0] = SATURATE(value);

		// Write down the second decoded magnitude
		value = entry->value1;
		rowptr[1] = SATURATE(value);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->post_skip];
	}
}
#endif

#elif _SINGLE_FSM_TABLE

bool DecodeBandFSM8sNoGap(FSM *fsm, BITSTREAM *stream, PIXEL8S *image, int width, int height, int pitch)
{
	int index, byte, i;
	FSMENTRY *entry,*firstentry = fsm->table->firstentry;
	PIXEL8S *rowptr = image;
	PIXEL8S *bandendptr;
	int32_t value;

	pitch /= sizeof(PIXEL8S);

	// Zero out the entire subband
	ZeroHighPassRow((PIXEL *)rowptr, pitch*height);

	// The Huffman decoder assumes each byte is processed as two 4-bit chunks
	assert(BITSTREAM_WORD_SIZE == 2 * FSM_INDEX_SIZE);

	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	// Decode runs and magnitude values until the entire band is decoded
	for (;;)
	{
		// Check that the decoder has not overrun the output array
		//assert(rowptr < bandendptr);

		// Read a byte from the bitstream
		byte = GetFastByte(stream);

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		i = (fsm->next_state_index << FSM_INDEX_SIZE) | index;//DAN
		entry = firstentry+i; //DAN

		// Return if the subband is decoded completely
		if(entry->value0 == BAND_END_TRAILER)
		{
			assert(rowptr <= bandendptr);
			ResetFSMIndividual(fsm);

			return true;
		}

		// set the pointer to the next state
		UpdateFSMIndividual(fsm, (entry->next_state));

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_skip];

		// Write down the first decoded magnitude
		value = entry->value0;
		rowptr[0] = SATURATE(value);

		// Write down the second decoded magnitude
		value = entry->value1;
		rowptr[1] = SATURATE(value);

		// Skip the appropriate distance
		rowptr = &rowptr[entry->post_skip];

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);

		// Index into the lookup table at that state
		i = (fsm->next_state_index << FSM_INDEX_SIZE) | index;//DAN
		entry = firstentry+i; //DAN

		// Return if the subband is decoded completely
		if(entry->value0 == BAND_END_TRAILER)
		{
			assert(rowptr <= bandendptr);
			ResetFSMIndividual(fsm);
			return true;
		}

		// set the pointer to the next state
		UpdateFSMIndividual(fsm, (entry->next_state));

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_skip];

		// Write down the first decoded magnitude
		value = entry->value0;
		rowptr[0] = SATURATE(value);

		// Write down the second decoded magnitude
		value = entry->value1;
		rowptr[1] = SATURATE(value);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->post_skip];
	}
}

#else

bool DecodeBandFSM8sNoGap(FSM *fsm, BITSTREAM *stream, PIXEL8S *image, int width, int height, int pitch)
{
	int index, byte;
	FSMENTRY *entry;
	PIXEL8S *rowptr = image;
	PIXEL8S *bandendptr;
	int32_t value;

#if 1
	__declspec(align(4)) FSMENTRY buffer;
#endif

	pitch /= sizeof(PIXEL8S);

	// zero out the entire subband
	ZeroHighPassRow((PIXEL *)rowptr, pitch*height);

	// The Huffman decoder assumes each byte is processed as two 4-bit chunks
	assert(BITSTREAM_WORD_SIZE == 2 * FSM_INDEX_SIZE);

	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	bandendptr = rowptr + height * pitch;

	// Decode runs and magnitude values until the entire band is decoded
	for (;;)
	{
#if (0 && DEBUG)
		if (!(rowptr < bandendptr)) {
			return true;
		}
#endif
		// Read a byte from the bitstream
		byte = GetFastByte(stream);

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entry = GetFSMTableEntryIndividual(fsm, index);

		// Return if the subband is decoded completely
		if(entry == NULL)
		{
			assert(rowptr <= bandendptr);
			ResetFSMIndividual(fsm);

			return true;
		}

		// Set the pointer to the next state
		UpdateFSMIndividual(fsm, (entry->next_state));

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_skip];

		// Write down the first decoded magnitude
		value = entry->value0;
		rowptr[0] = SATURATE(value);

		// Write down the second decoded magnitude
		value = entry->value1;
		rowptr[1] = SATURATE(value);

		// Skip the appropriate distance
		rowptr = &rowptr[entry->post_skip];

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);

		// Index into the lookup table at that state
		entry = GetFSMTableEntryIndividual(fsm, index);

		// Return if the subband is decoded completely
		if (entry == NULL)
		{
			assert(rowptr <= bandendptr);
			ResetFSMIndividual(fsm);
			return true;
		}

		// Set the pointer to the next state
		UpdateFSMIndividual(fsm, (entry->next_state));

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->pre_skip];

		// Write down the first decoded magnitude
		value = entry->value0;
		rowptr[0] = SATURATE(value);

		// Write down the second decoded magnitude
		value = entry->value1;
		rowptr[1] = SATURATE(value);

		// Skip the decoded zero runs
		rowptr = &rowptr[entry->post_skip];
	}
}
#endif

// Decode the highpass band coefficients but do not write them out - used in SIF mode
bool SkipBandFSM(FSM *fsm, BITSTREAM *stream, PIXEL8S *image, int width, int height, int pitch)
{
	int index, byte;
	FSMENTRY *entry;

	pitch /= sizeof(PIXEL8S);

	// The Huffman decoder assumes each byte is processed as two 4-bit chunks
	assert(BITSTREAM_WORD_SIZE == FSM_INDEX_SIZE*2);

	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	// Decode runs and magnitude values until the entire band is decoded
	for (;;)
	{
		// Read a byte from the bitstream
		byte = GetFastByte(stream);

		// Decode the first 4-bit chunk
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER) {
			ResetFSM(fsm);
			return true;
		}

		// set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// decode the second 4-bit chunk
		index = byte & ((1<<FSM_INDEX_SIZE)-1);

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER) {
			ResetFSM(fsm);
			return true;
		}

		// set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);
	}
}

#if _TIMING
extern TIMER tk_fastruns;
#endif

#if 0 //dan20041030 not used
// New version of coefficient runs decoder that uses a finite state machine with a scaling factor
bool DecodeFastRunsFSM8s(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
						 int band_index, int width, int height)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
	FILE *logfile = decoder->logfile;
	int result;

	// Get the pointer to the finite state machine
	FSM *fsm = &decoder->fsm[decoder->codec.active_codebook]; //DAN20041026

	// All rows are treated as one long row that covers the entire band
	int size = fsm->table.num_states;

	PIXEL *rowptr;
	int row = 0;
	int pitch;

	int pixel_type = wavelet->pixel_type[band_index];

	decoder->codec.active_codebook = 0; // reset CODEC state

	// Must have a valid wavelet
	assert(wavelet != NULL);
	if (wavelet == NULL) return false;

	//Must have a valid FSM
	assert(fsm != NULL);
	if(fsm == NULL) return false;

	assert(size > 0);
	if (size == 0) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}

	// Check if the band is intended for 8-bit pixels
	assert(pixel_type == PIXEL_TYPE_8S);

	START(tk_fastruns);

	rowptr = (PIXEL *)wavelet->band[band_index];
	pitch = wavelet->pitch8s;		// Use the 8-bit pitch
	//pitch = wavelet->pitch;

	// The finite state machine does not support a marker at the end of rows
#if RUNS_ROWEND_MARKER
	assert(0);
#endif

	// Get one byte from the bitstream and decode 4 bits at a time
	result = DecodeBandFSM8sNoGap(fsm, stream, (PIXEL8S *)rowptr, width, height, pitch);

	assert(result == true);
	if (result != true) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}

#if (0 && DEBUG && _WIN32)
	_CrtCheckMemory();
#endif

#if (0 && DEBUG)
	if (logfile)
		DumpBand("Band", wavelet, band_index, NULL, logfile);
#endif

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "DecodeFastRunsFSM8s, band index: %d\n", band_index);
		DumpWaveletRow(wavelet, band_index, 0, logfile);
	}
#endif

end:
	STOP(tk_fastruns);

	return true;
}
#endif

#if _DEQUANTIZE_IN_FSM
void ReQuantFSM(FSM *fsm, int quant)
{
	int count = 0;
	int i, j;
	short *restore = &fsm->restoreFSM[0];

	#if !_INDIVIDUAL_ENTRY
		for (i = 0; i < fsm->table.num_states; i++)
		{
			FSMENTRY *entry = fsm->table.entries[i];
			for (j = 0; j < (1 << FSM_INDEX_SIZE); j++)
			{
				entry[j].value0 = restore[count++];
				entry[j].value1 = restore[count++];
			}
		}
	#else
		for (i = 0; i < (fsm->table.num_states << FSM_INDEX_SIZE); i++)
		{
			FSMENTRY *entry = fsm_table.entries_ind[i];

			if(entry)
			{
				 entry->value0 = restore[count++];
				 entry->value1 = restore[count++];
			}
		}
	#endif
}

void DeQuantFSM(FSM *fsm, int quant)
{
	int i, j;


	if(fsm->LastQuant > 1 && fsm->LastQuant != quant)
	{
		ReQuantFSM(fsm, fsm->LastQuant);
	}
	else if(fsm->LastQuant == quant)
	{
		return;
	}


	if(fsm->InitizedRestore == 0)
	{
		short *restore = &fsm->restoreFSM[0];
		int count = 0;

		#if !_INDIVIDUAL_ENTRY
			for (i = 0; i < fsm->table.num_states; i++)
			{
				FSMENTRY *entry = fsm->table.entries[i];
				for (j = 0; j < (1 << FSM_INDEX_SIZE); j++)
				{
					restore[count++] = entry[j].value0;
					restore[count++] = entry[j].value1;
				}
			}
		#else
			for (i = 0; i < (fsm->table.num_states << FSM_INDEX_SIZE); i++)
			{
				FSMENTRY *entry = fsm->table.entries_ind[i];

				if(entry)
				{
					restore[count++] = entry->value0;
					restore[count++] = entry->value1;
				}
			}
		#endif

		fsm->InitizedRestore = 1;
	}

#if !_INDIVIDUAL_ENTRY
	for (i = 0; i < fsm->table.num_states; i++)
	{
		FSMENTRY *entry = fsm->table.entries[i];
		for (j = 0; j < (1 << FSM_INDEX_SIZE); j++)
		{
			if(entry[j].value0 < 0x7ff0) // band end trailer
				entry[j].value0 *= quant;

			entry[j].value1 *= quant;
		}
	}
#else
	for (i = 0; i < (fsm->table.num_states << FSM_INDEX_SIZE); i++)
	{
		FSMENTRY *entry = fsm->table.entries_ind[i];

		if(entry)
		{
			if(entry->value0 < 0x7ff0) // band end trailer etc
				entry->value0 *= quant;

			entry->value1 *= quant;
		}
	}
#endif

	fsm->LastQuant = quant;

}
#endif // _DEQUANTIZE_IN_FSM

// New version of coefficient runs decoder that uses a finite state machine with a scaling factor
//dan 7-11-03
bool DecodeFastRunsFSM16s(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
						  int band_index, int width, int height, int threading)
{
	//CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	int result = true;
	int quant = wavelet->quantization[band_index];

	int active_codebook = decoder->codec.active_codebook;
	// Get the pointer to the finite state machine
	FSM *fsm = &decoder->fsm[active_codebook];
	int size;
	PIXEL *rowptr;
	//int row = 0;
	int pitch;
	CODEC_STATE *codec = &decoder->codec;
	//int channel = codec->channel;
	//int subband = codec->band.subband;
	//int num_subbands = codec->num_subbands;
	//int pixel_type = wavelet->pixel_type[band_index];
	int difference_coding = decoder->codec.difference_coding;
	//int localquant = 1;
	int peaklevel = 0;
	//int peaksize = 0;
	PIXEL *peakbase = NULL;

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Subband: %d, active_codebook: %d, difference_coding: %d\n",
				subband, decoder->codec.active_codebook, difference_coding);
	}
#endif

	decoder->codec.active_codebook = 0; // reset CODEC state
	decoder->codec.difference_coding = 0; //reset state for next subband

	// Must have a valid wavelet
	assert(wavelet != NULL);
	if (wavelet == NULL) return false;

	//Must have a valid FSM
	assert(fsm != NULL);
	if(fsm == NULL) return false;

	// All rows are treated as one long row that covers the entire band
	size = fsm->table.num_states;

	assert(size > 0);
	if (size == 0) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}

	// Check if the band is intended for 8-bit pixels
	assert(wavelet->pixel_type[band_index] == PIXEL_TYPE_16S);

	START(tk_fastruns);

	rowptr = (PIXEL *)wavelet->band[band_index];
	//pitch = wavelet->pitch8s;		// Use the 8-bit pitch
	pitch = wavelet->pitch;

	peaklevel = codec->peak_table.level;
	peakbase = codec->peak_table.base;

#if _THREADED
	threading = decoder->entropy_worker_new.pool.thread_count > 1 ? threading : 0;

	if(threading)
	{
		decoder->entropy_worker_new.threads_used = 1;

		{
			//int start = stream->nWordsUsed;
			int end;
			struct entropy_data_new *data;

			int next_queue_num = decoder->entropy_worker_new.next_queue_num++;
			data = &decoder->entropy_worker_new.entropy_data[next_queue_num];

			memcpy(&data->stream,stream, sizeof(BITSTREAM));
			data->rowptr = rowptr;
			data->width = width;
			data->height = height;
			data->pitch = pitch;
			data->peaks = peakbase;
			data->level = peaklevel;
			data->quant = quant;
			data->wavelet = wavelet;
			data->band_index = band_index;
			data->active_codebook = active_codebook;
			data->difference_coding = difference_coding;

			// Start only a particular threadid
			if(next_queue_num == 0)
			{
				ThreadPoolSetWorkCount(&decoder->entropy_worker_new.pool, 1);

#if _DELAYED_THREAD_START==0
				ThreadPoolSendMessage(&decoder->entropy_worker_new.pool, THREAD_MESSAGE_START);
#endif
			}
			else
			{	// Set the work count to the number of rows to process
				ThreadPoolAddWorkCount(&decoder->entropy_worker_new.pool, 1);
			}

			{
				unsigned short tag = *(stream->lpCurrentWord-8) << 8;
				if(tag == (unsigned short)OPTIONALTAG(CODEC_TAG_SUBBAND_SIZE))
				{
					int chunksize;
					int value = *(stream->lpCurrentWord-6) << 8;
					value |= *(stream->lpCurrentWord-5);

					tag |= *(stream->lpCurrentWord-7);
					tag = NEG(tag);

					chunksize = value;
					chunksize &= 0xffff;
					chunksize += ((tag&0xff)<<16);

					chunksize *= 4;

					chunksize -= 8;

					{
						uint32_t *ptr = (uint32_t *)stream->lpCurrentWord;
						ptr += (chunksize>>2);

						if(*ptr != 0x00003800) // bandend
						{
							goto continuesearch;
						}
					}

					stream->lpCurrentWord += chunksize;
					stream->nWordsUsed -= chunksize;
					end = stream->nWordsUsed;
				}
				else
				{
continuesearch:
					while(*((uint32_t *)stream->lpCurrentWord) != 0x00003800) // bandend
					{
						stream->lpCurrentWord += 4;
						stream->nWordsUsed -= 4;
					}
					end = stream->nWordsUsed;
				}
			}
		}
	}
	else
#endif // _THREADED
	{
		DeQuantFSM(fsm, quant);

		if (peaklevel)
		{
			result = DecodeBandFSM16sNoGapWithPeaks(fsm, stream, (PIXEL16S *)rowptr, width, height, pitch, peakbase, peaklevel, 1);
		}
		else
		{
#if _DEBUG
			result = DecodeBandFSM16sNoGap(fsm, stream, (PIXEL16S *)rowptr, width, height, pitch, logfile);
#else
			result = DecodeBandFSM16sNoGap(fsm, stream, (PIXEL16S *)rowptr, width, height, pitch);
#endif
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


		if (result)
		{
			// Call thread safe routine to update the band valid flags
			UpdateWaveletBandValidFlags(decoder, wavelet, band_index);
		}
	}

	assert(result == true);
	if (result != true) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}


//end:
	STOP(tk_fastruns);

	return true;
}





bool SkipFastRunsFSM(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
					 int band_index, int width, int height)
{
	//CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	int result;

	// Get the pointer to the finite state machine
	FSM *fsm = &decoder->fsm[decoder->codec.active_codebook]; //DAN20041026

	// All rows are treated as one long row that covers the entire band
	int size = fsm->table.num_states;

	PIXEL *rowptr;
	//int row = 0;
	int pitch;

	//int pixel_type = wavelet->pixel_type[band_index];

	decoder->codec.active_codebook = 0; // reset CODEC state

	// Must have a valid wavelet
	assert(wavelet != NULL);
	if (wavelet == NULL) return false;

	//Must have a valid FSM
	assert(fsm != NULL);
	if(fsm == NULL) return false;

	assert(size > 0);
	if (size == 0) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}

	// Check if the band is 8bit/pixel
	assert(wavelet->pixel_type[band_index] == PIXEL_TYPE_8S);

	START(tk_fastruns);

	rowptr = (PIXEL *)wavelet->band[band_index];
	pitch = wavelet->pitch8s;		// Use the 8-bit pitch

	// The finite state machine does not support a marker at the end of rows
#if RUNS_ROWEND_MARKER
	assert(0);
#endif


#if 1		// Get one byte from the bitstream and decode 4 bits at a time
	result = SkipBandFSM(fsm, stream, (PIXEL8S *)rowptr, width, height, pitch);

	assert(result == true);
	if (result != true) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}

#endif

#if (0 && DEBUG && _WIN32)
	_CrtCheckMemory();
#endif

#if (0 && DEBUG)
	if (logfile)
		DumpBand("Band", wavelet, band_index, NULL, logfile);
#endif

//end:
	STOP(tk_fastruns);

	return true;
}


// The third version is also based on the finite state machine decoder with
// gaps between rows encoded as zero runs, but dequantization is performed as
// the highpass values are read from the bitstream and placed into a row buffer.

// The highpass values are not written into the wavelet highpass band.

// Eventually this routine will be merged into the routine DecodeTemporalBand8s
// since this routine contains code specific to the inverse temporal transform
// and DecodeTemporalBand8s has become a shell.

#if 0
bool DecodeBandRunsFSM8s(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
						 int band_index, int width, int height,
						 IMAGE *frame0, IMAGE *frame1)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
	FILE *logfile = decoder->logfile;
	int result;

	// Get the pointer to the finite state machine
	FSM *fsm = &decoder->fsm;

	// All rows are treated as one long row that covers the entire band
	int size = fsm->table.num_states;

	PIXEL *lowpass = wavelet->band[0];
	int lowpass_pitch = wavelet->pitch;
	//PIXEL8S *rowptr;
	int row = 0;
	int pitch;
	int row_width;		// Width of the encoded row of highpass coefficients

	PIXEL *even = frame0->band[0];
	PIXEL *odd = frame1->band[0];

	int even_pitch = frame0->pitch;
	int odd_pitch = frame1->pitch;

	int pixel_type = wavelet->pixel_type[band_index];
	int quantization = wavelet->quantization[band_index];
	PIXEL *buffer;
	size_t buffer_size;

	int index, byte;
	FSMENTRY *entry;
	int column = 0;
	int32_t value;
	int buffer_row_size;
	PIXEL *highpass;

	// Check that the wavelet into which the band will be decoded is valid
	assert(wavelet != NULL);
	if (wavelet == NULL) return false;

	// Check that the finite state machine is valid
	assert(fsm != NULL);
	if (fsm == NULL) return false;

	assert(size > 0);
	if (size == 0) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}

	// Check that the band was encoded using 8-bit signed coefficients
	assert(pixel_type == PIXEL_TYPE_8S);

	pitch = wavelet->pitch8s;		// Use the pitch for 8-bit packed rows

	// Get the buffer for storing one row of dequantized highpass coefficients
	buffer = (PIXEL *)decoder->buffer;
	buffer_size = decoder->buffer_size;

	// The finite state machine does not support a marker at the end of each row
	assert(RUNS_ROWEND_MARKER == 0);


	/***** Start of code included from DecodeBandFSM8s() *****/


	// Check that one byte can be processes as two 4-bit nibbles
	assert(BITSTREAM_WORD_SIZE == (2 * FSM_INDEX_SIZE));

	// Check that the bitstream buffer is empty
	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	// Convert the pitch to units of pixels
	pitch /= sizeof(PIXEL8S);

	buffer_row_size = pitch * sizeof(PIXEL);

	lowpass_pitch /= sizeof(PIXEL);
	even_pitch /= sizeof(PIXEL);
	odd_pitch /= sizeof(PIXEL);

	// Compute the address of the row after the last row in the band
	//maxptr = rowptr + height * pitch;

	// Round up the row length (in bytes) to a multiple of 16 bytes
	//row_size = ALIGN16(row_size);

	// Check that the buffer is large enough to hold one row
	//assert(buffer_size >= row_size);
	assert(buffer_size >= buffer_row_size);

	// Use the buffer for the row or highpass coefficients
	highpass = buffer;

#if 1
	// The row spans the allocated width (pitch) of the band in no gap mode
	row_width = pitch;
#else
	// For debugging
	row_width = wavelet->encoded_pitch/sizeof(PIXEL8S);
#endif

	// Clear the highpass buffer before decoding the non-zero coefficients
	ZeroHighPassRow(highpass, buffer_row_size);

	// Decode zero runs and magnitude values (with appended sign bit)
	// until the marker for the band end trailer has been decoded
	for (;;)
	{
		// Read a byte from the bitstream
		byte = GetFastByte(stream);


		/***** Decode the first 4-bit nibble *****/

		// Decode the first 4-bit nibble
		index = byte >> FSM_INDEX_SIZE;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

		// Return when the entire band is decoded
		if (entry->value0 == BAND_END_TRAILER)
		{
			// Dequantize the highpass coefficients
			//DequantizeBandRow(rowptr, width, quantization, highpass);

			// Apply the inverse temporal transform to the current row
			InvertTemporalRow16s(lowpass, highpass, even, odd, width);

			// Advance to the next lowpass input row
			lowpass += lowpass_pitch;

			 // Advance to the next even and odd output rows
			even += even_pitch;
			odd += odd_pitch;

			// Process the rest of the subband
			ZeroHighPassRow(highpass, buffer_row_size);
			while (++row < height)
			{
				// Apply the inverse temporal transform to the current row
				InvertTemporalRow16s(lowpass, highpass, even, odd, width);

				// Advance to the next lowpass input row
				lowpass += lowpass_pitch;

				 // Advance to the next even and odd output rows
				even += even_pitch;
				odd += odd_pitch;
			}

			ResetFSM(fsm);
			return true;
		}

		// set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// If no magnitude value is decoded
		if (entry->value0 == 0)
		{
			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= row_width)
			{
				// Dequantize the highpass coefficients
				//DequantizeBandRow(rowptr, width, quantization, highpass);

				// Apply the inverse temporal transform to the current row
				InvertTemporalRow16s(lowpass, highpass, even, odd, width);

				// Advance to the next lowpass input row
				lowpass += lowpass_pitch;

				 // Advance to the next even and odd output rows
				even += even_pitch;
				odd += odd_pitch;

				// Compute the starting column for the next row
				column -= row_width;

				// Advance to the next row
				row++;

				// Clear the highpass buffer before decoding the non-zero coefficients
				ZeroHighPassRow(highpass, buffer_row_size);
			}
		}
		// If there is only one decoded magnitude value
		else if (entry->value1 == 0)
		{
			value = entry->value0;

			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= row_width)
			{
				// Dequantize the highpass coefficients
				//DequantizeBandRow(rowptr, width, quantization, highpass);

				// Apply the inverse temporal transform to the current row
				InvertTemporalRow16s(lowpass, highpass, even, odd, width);

				// Advance to the next lowpass input row
				lowpass += lowpass_pitch;

				 // Advance to the next even and odd output rows
				even += even_pitch;
				odd += odd_pitch;

				// Compute the starting column for the next row
				column -= row_width;

				// Advance to the next row
				row++;

				// Clear the highpass buffer before decoding the non-zero coefficients
				ZeroHighPassRow(highpass, buffer_row_size);
			}

			// Fill in the decoded magnitude

			// Check the column before storing the value
			assert(0 <= column && column < row_width);

			// Dequantize the value and store it in the highpass row buffer
			highpass[column] = quantization * value;

			column += entry->post_skip;

			// Did the scan go beyond the end of the row?
			if (column >= row_width)
			{
				// Dequantize the highpass coefficients
				//DequantizeBandRow(rowptr, width, quantization, highpass);

				// Apply the inverse temporal transform to the current row
				InvertTemporalRow16s(lowpass, highpass, even, odd, width);

				// Advance to the next lowpass input row
				lowpass += lowpass_pitch;

				 // Advance to the next even and odd output rows
				even += even_pitch;
				odd += odd_pitch;

				// Compute the starting column for the next row
				column -= row_width;

				// Advance to the next row
				row++;

				// Clear the highpass buffer before decoding the non-zero coefficients
				ZeroHighPassRow(highpass, buffer_row_size);
			}
		}
		// If there are two decoded magnitude values
		else
		{
			// Check the column before storing values
			assert(0 <= column && column < row_width);

			if (column < (row_width - 1)) {
				// Store both values in the current row
				highpass[column++] = quantization * entry->value0;
				highpass[column++] = quantization * entry->value1;
			}
			else {
				value = entry->value0;
				highpass[column] = quantization * value;

				value = entry->value1;

				// Dequantize the highpass coefficients
				//DequantizeBandRow(rowptr, width, quantization, highpass);

				// Apply the inverse temporal transform to the current row
				InvertTemporalRow16s(lowpass, highpass, even, odd, width);

				// Advance to the next lowpass input row
				lowpass += lowpass_pitch;

				 // Advance to the next even and odd output rows
				even += even_pitch;
				odd += odd_pitch;

				// Advance to the next row
				row++;

				// Clear the highpass buffer before decoding the non-zero coefficients
				ZeroHighPassRow(highpass, buffer_row_size);

				column = 0;
				highpass[column++] = quantization * value;
			}
		}


		/***** Decode the second 4-bit nibble *****/

		// Decode the second 4-bit nibble
		index = byte & FSM_INDEX_MASK;

		// Index into the lookup table at that state
		entry = GetFSMTableEntry(fsm, index);

		// Return if the subband is decoded completely
		if (entry->value0 == BAND_END_TRAILER)
		{
			// Dequantize the highpass coefficients
			//DequantizeBandRow(rowptr, width, quantization, highpass);

			// Apply the inverse temporal transform to the current row
			InvertTemporalRow16s(lowpass, highpass, even, odd, width);

			// Advance to the next lowpass input row
			lowpass += lowpass_pitch;

			 // Advance to the next even and odd output rows
			even += even_pitch;
			odd += odd_pitch;

			// Process the rest of the subband
			ZeroHighPassRow(highpass, buffer_row_size);
			while (++row < height)
			{
				// Apply the inverse temporal transform to the current row
				InvertTemporalRow16s(lowpass, highpass, even, odd, width);

				// Advance to the next lowpass input row
				lowpass += lowpass_pitch;

				 // Advance to the next even and odd output rows
				even += even_pitch;
				odd += odd_pitch;
			}

			ResetFSM(fsm);
			return true;
		}

		// Set the pointer to the next state
		UpdateFSM(fsm, (int)entry->next_state);

		// If no magnitude value is decoded
		if (entry->value0 == 0)
		{
			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= row_width)
			{
				// Dequantize the highpass coefficients
				//DequantizeBandRow(rowptr, width, quantization, highpass);

				// Apply the inverse temporal transform to the current row
				InvertTemporalRow16s(lowpass, highpass, even, odd, width);

				// Advance to the next lowpass input row
				lowpass += lowpass_pitch;

				 // Advance to the next even and odd output rows
				even += even_pitch;
				odd += odd_pitch;

				// Compute the starting column for the next row
				column -= row_width;

				// Advance to the next row
				row++;

				// Clear the highpass buffer before decoding the non-zero coefficients
				ZeroHighPassRow(highpass, buffer_row_size);
			}
		}
		// If there is only one decoded magnitude value
		else if (entry->value1 == 0)
		{
			value = entry->value0;

			column += entry->pre_skip;

			// The run length scan can go past the end of the row if the row ends
			// with a run of zeros and the next row begins with a run of zeros

			// Did the scan go beyond the end of the row?
			while (column >= row_width)
			{
				// Dequantize the highpass coefficients
				//DequantizeBandRow(rowptr, width, quantization, highpass);

				// Apply the inverse temporal transform to the current row
				InvertTemporalRow16s(lowpass, highpass, even, odd, width);

				// Advance to the next lowpass input row
				lowpass += lowpass_pitch;

				 // Advance to the next even and odd output rows
				even += even_pitch;
				odd += odd_pitch;

				// Compute the starting column for the next row
				column -= row_width;

				// Advance to the next row
				row++;

				// Clear the highpass buffer before decoding the non-zero coefficients
				ZeroHighPassRow(highpass, buffer_row_size);
			}

			// Fill in the decoded magnitude

			// Check the column before storing the value
			//assert(index < width);
			assert(0 <= column && column < row_width);

			highpass[column] = quantization * value;

			column += entry->post_skip;

			// Did the scan go beyond the end of the row?
			if (column >= row_width)
			{
				// Dequantize the highpass coefficients
				//DequantizeBandRow(rowptr, width, quantization, highpass);

				// Apply the inverse temporal transform to the current row
				InvertTemporalRow16s(lowpass, highpass, even, odd, width);

				// Advance to the next lowpass input row
				lowpass += lowpass_pitch;

				 // Advance to the next even and odd output rows
				even += even_pitch;
				odd += odd_pitch;

				// Compute the starting column for the next row
				column -= row_width;

				// Advance to the next row
				row++;

				// Clear the highpass buffer before decoding the non-zero coefficients
				ZeroHighPassRow(highpass, buffer_row_size);
			}
		}
		// If there are two decoded magnitude values
		else
		{
			// Check the column before storing values
			assert(0 <= column && column < row_width);

			if (column < (row_width - 1)) {
				// Store both highpass values in the current row
				highpass[column++] = quantization * entry->value0;
				highpass[column++] = quantization * entry->value1;
			}
			else {
				highpass[column] = quantization * entry->value0;
				value = entry->value1;

				// Dequantize the highpass coefficients
				//DequantizeBandRow(rowptr, width, quantization, highpass);

				// Apply the inverse temporal transform to the current row
				InvertTemporalRow16s(lowpass, highpass, even, odd, width);

				// Advance to the next lowpass input row
				lowpass += lowpass_pitch;

				 // Advance to the next even and odd output rows
				even += even_pitch;
				odd += odd_pitch;

				// Advance to the next row
				row++;

				// Clear the highpass buffer before decoding the non-zero coefficients
				ZeroHighPassRow(highpass, buffer_row_size);

				column = 0;
				highpass[column++] = quantization * value;
			}
		}
	}
	/***** End of the code included from DecodeBandFSM8s() *****/

#if 0
	assert(result == true);
	if (result != true) {
		decoder->error = CODEC_ERROR_RUN_DECODE;
		return false;
	}
#endif

#if (0 && DEBUG && _WIN32)
	_CrtCheckMemory();
#endif

#if (0 && DEBUG)
	if (logfile)
		DumpBand("Band", wavelet, band_index, NULL, logfile);
#endif

#if 0
end:

	return true;
#endif

}
#endif


/***** End of the code for the finite state machine decoder *****/


#if 1

// The second version applies the horizontal inverse filters row by row, so the
// memory access pattern is more efficient.  The lowpass and highpass temporal
// coefficients for each row are inverted and packed into the output in one pass.

// Apply the inverse horizontal-temporal transform and pack the output into a buffer

void TransformInverseFrameToYUV(TRANSFORM *transform[], int frame_index, int num_channels,
								uint8_t *output, int output_pitch, FRAME_INFO *frame,
								const SCRATCH *scratch, int chroma_offset, int precision)
{
	// Pointers to the rows in the horizontal wavelet for each channel
	PIXEL *horizontal_lowlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_lowhigh[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highhigh[TRANSFORM_MAX_CHANNELS];

	// Horizontal wavelet band width and pitch
	int horizontal_width[TRANSFORM_MAX_CHANNELS];
	int horizontal_pitch[TRANSFORM_MAX_CHANNELS];
	int horizontal_pitch8s[TRANSFORM_MAX_CHANNELS];

	// Quantization factors
	int lowlow_quantization[TRANSFORM_MAX_CHANNELS];
	int lowhigh_quantization[TRANSFORM_MAX_CHANNELS];
	int highlow_quantization[TRANSFORM_MAX_CHANNELS];
	int highhigh_quantization[TRANSFORM_MAX_CHANNELS];

	// Pointers to the rows in the temporal wavelet for each channel
	PIXEL *temporal_lowpass[TRANSFORM_MAX_CHANNELS];
	PIXEL *temporal_highpass[TRANSFORM_MAX_CHANNELS];

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
#if DEBUG
	size_t buffer_size = scratch->free_size;
#endif

	// Dimensions of the reconstructed frame
	int frame_width = frame->width;
	int frame_height = frame->height;
	int half_height = frame_height / 2;
	size_t temporal_row_size = frame_width * sizeof(PIXEL);
	int field_pitch = 2 * output_pitch;
	int output_width;
	int channel;
	int row;

	// Round up the temporal row size to an integral number of cache lines
	temporal_row_size = ALIGN(temporal_row_size, _CACHE_LINE_SIZE);

	// Check that the buffer starts on a cache line boundary
	assert(ISALIGNED(buffer, _CACHE_LINE_SIZE));

	// Check that the number of channels is reasonable
	assert(0 < num_channels && num_channels <= TRANSFORM_MAX_CHANNELS);

	// Check that the buffer is large enough
#if DEBUG
	assert((2 * num_channels * temporal_row_size) <= buffer_size);
#endif

	// Allocate buffers for a single row of lowpass and highpass temporal coefficients
	// and initialize the arrays of row pointers into the horizontal transform bands
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

#if (0 && DEBUG)
		int static count = 0;
		if (count < 20) {
			char label[_MAX_PATH];
			int i;

			sprintf(label, "Frame%d-%d-", frame_index, count);
			DumpPGM(label, wavelet, NULL);

			for (i = 1; i < wavelet->num_bands; i++)
			{
				sprintf(label, "Frame-%d-band%d-%d-", frame_index, i, count);
				DumpBandPGM(label, wavelet, i, NULL);
			}
		}
		count++;
#endif
		// Initialize the row pointers into the horizontal bands
		horizontal_lowlow[channel] = wavelet->band[LL_BAND];
		horizontal_lowhigh[channel] = wavelet->band[LH_BAND];
		horizontal_highlow[channel] = wavelet->band[HL_BAND];
		horizontal_highhigh[channel] = wavelet->band[HH_BAND];

		lowlow_quantization[channel] = wavelet->quantization[LL_BAND];
		lowhigh_quantization[channel] = wavelet->quantization[LH_BAND];
		highlow_quantization[channel] = wavelet->quantization[HL_BAND];
		highhigh_quantization[channel] = wavelet->quantization[HH_BAND];

		// Compute the pitch in units of pixels
		horizontal_pitch[channel] = wavelet->pitch/sizeof(PIXEL);

		// Compute the 8-bit pitch in units of pixels
		horizontal_pitch8s[channel] = wavelet->pitch8s/sizeof(PIXEL);
		//horizontal_pitch8s[channel] = wavelet->pitch8s/sizeof(PIXEL8S);

		// Remember the width of the horizontal wavelet rows for this channel
		horizontal_width[channel] = wavelet->width;

		//TODO: Need to recode the buffer allocations using the scratch space API

		// Divide the buffer into temporal lowpass and highpass rows
		temporal_lowpass[channel] = (PIXEL *)(buffer + (2 * channel) * temporal_row_size);
		temporal_highpass[channel] = (PIXEL *)(buffer + (2 * channel + 1) * temporal_row_size);
	}

	// Process one row at a time from each channel
	for (row = 0; row < half_height; row++)
	{
		PIXEL *line_buffer = (PIXEL *)(buffer + (2 * num_channels + 2) * temporal_row_size);

		// Invert the horizontal transform applied to the temporal bands in each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			int pitch = horizontal_pitch[channel];
			//int pitch8s = horizontal_pitch8s[channel];

			// Invert the horizontal transform applied to the temporal lowpass row
			InvertHorizontalRow16s8sTo16sBuffered(horizontal_lowlow[channel], lowlow_quantization[channel],
										  (PIXEL8S *)horizontal_lowhigh[channel], lowhigh_quantization[channel],
										  temporal_lowpass[channel],
										  horizontal_width[channel],
										  (PIXEL *)line_buffer);

			// Invert the horizontal transform applied to the temporal highpass row
			//DAN20051004 -- possible reversiblity issue
			//InvertHorizontalRow8sBuffered //----------------------- Maybe bad
			InvertHorizontalRow16s8sTo16sBuffered(horizontal_highlow[channel], highlow_quantization[channel],
								  (PIXEL8S *)horizontal_highhigh[channel], highhigh_quantization[channel],
								  temporal_highpass[channel],
								  horizontal_width[channel],
								  (PIXEL *)line_buffer);

			// Advance to the next row in each horizontal band in this channel
			horizontal_lowlow[channel] += pitch;
			horizontal_lowhigh[channel] += pitch;
			horizontal_highlow[channel] += pitch;
			horizontal_highhigh[channel] += pitch;
		}

		// The output width is twice the width of the wavelet bands
		output_width = 2 * horizontal_width[0];

		// Adjust the frame width to fill to the end of each row
		//frame_width = output_pitch / 2;

		if (precision == CODEC_PRECISION_10BIT)
		{
			// Invert the temporal bands from all channels and pack output pixels
			switch (frame->format)
			{
				// Need to reduce the resolution from 10 bits to 8 bits during the inverse

				case DECODED_FORMAT_YUYV:
					InvertInterlacedRow16s10bitToYUV(temporal_lowpass, temporal_highpass, num_channels,
													 output, output_pitch, output_width, frame_width,
													 chroma_offset);
					break;

				case DECODED_FORMAT_UYVY:
					InvertInterlacedRow16s10bitToUYVY(temporal_lowpass, temporal_highpass, num_channels,
													  output, output_pitch, output_width, frame_width,
													  chroma_offset);
					break;

				default:
					assert(0);
					break;
			}
		}

		else	// Older code for 8-bit precision
		{
			int format;

			assert(precision == CODEC_PRECISION_8BIT);

			switch (frame->format)
			{
				case DECODED_FORMAT_YUYV:
					format = COLOR_FORMAT_YUYV;
					break;

				case DECODED_FORMAT_UYVY:
					format = COLOR_FORMAT_UYVY;
					break;
			}

			// Invert the temporal bands from all channels and pack output pixels
			InvertInterlacedRow16sToYUV(temporal_lowpass, temporal_highpass, num_channels,
										output, output_pitch, output_width, frame_width,
										chroma_offset, format);
		}

		// Advance to the next row in the packed output image
		output += field_pitch;
	}
}

#endif


#if _INTERLACED_WORKER_THREADS
void TransformInverseFrameSectionToYUV(DECODER *decoder, int thread_index, int frame_index, int num_channels,
									   uint8_t *output, int output_pitch, FRAME_INFO *frame,
									   int chroma_offset, int precision)
{
	FILE *logfile = decoder->logfile;
	TRANSFORM **transform = decoder->transform;
	const SCRATCH *scratch = &decoder->scratch;

	// Pointers to the rows in the horizontal wavelet for each channel
	PIXEL *horizontal_lowlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_lowhigh[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highhigh[TRANSFORM_MAX_CHANNELS];

	// Horizontal wavelet band width and pitch
	int horizontal_width[TRANSFORM_MAX_CHANNELS];
	int horizontal_pitch[TRANSFORM_MAX_CHANNELS];
	int horizontal_pitch8s[TRANSFORM_MAX_CHANNELS];

	// Quantization factors
	int lowlow_quantization[TRANSFORM_MAX_CHANNELS];
	int lowhigh_quantization[TRANSFORM_MAX_CHANNELS];
	int highlow_quantization[TRANSFORM_MAX_CHANNELS];
	int highhigh_quantization[TRANSFORM_MAX_CHANNELS];

	// Pointers to the rows in the temporal wavelet for each channel
	PIXEL *temporal_lowpass[TRANSFORM_MAX_CHANNELS];
	PIXEL *temporal_highpass[TRANSFORM_MAX_CHANNELS];

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
	size_t buffer_size = scratch->free_size;
	uint8_t *output_row_ptr = output;

	// Dimensions of the reconstructed frame
	int frame_width = frame->width;
	int frame_height = frame->height;
	int half_height = frame_height / 2;
	size_t temporal_row_size = frame_width * sizeof(PIXEL);
	int field_pitch = 2 * output_pitch;
	int output_width;
	int channel;
	int row;

	HANDLE row_semaphore = decoder->interlaced_worker.row_semaphore;

	int return_value;

	// Round up the temporal row size to an integral number of cache lines
	temporal_row_size = ALIGN(temporal_row_size, _CACHE_LINE_SIZE);

	// Divide the buffer space between the four threads
	buffer_size /= 4;
	buffer += buffer_size * thread_index;

	// Check that the buffer starts on a cache line boundary
	assert(ISALIGNED(buffer, _CACHE_LINE_SIZE));

	// Check that the number of channels is reasonable
	assert(0 < num_channels && num_channels <= TRANSFORM_MAX_CHANNELS);

	// Check that the buffer is large enough
	assert((2 * num_channels * temporal_row_size) <= buffer_size);

	// Allocate buffers for a single row of lowpass and highpass temporal coefficients
	// and initialize the arrays of row pointers into the horizontal transform bands
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

#if (0 && DEBUG)
		int static count = 0;
		if (count < 20) {
			char label[_MAX_PATH];
			int i;

			sprintf(label, "Frame%d-%d-", frame_index, count);
			DumpPGM(label, wavelet, NULL);

			for (i = 1; i < wavelet->num_bands; i++)
			{
				sprintf(label, "Frame-%d-band%d-%d-", frame_index, i, count);
				DumpBandPGM(label, wavelet, i, NULL);
			}
		}
		count++;
#endif
		// Initialize the row pointers into the horizontal bands
		horizontal_lowlow[channel] = wavelet->band[LL_BAND];
		horizontal_lowhigh[channel] = wavelet->band[LH_BAND];
		horizontal_highlow[channel] = wavelet->band[HL_BAND];
		horizontal_highhigh[channel] = wavelet->band[HH_BAND];

		lowlow_quantization[channel] = wavelet->quantization[LL_BAND];
		lowhigh_quantization[channel] = wavelet->quantization[LH_BAND];
		highlow_quantization[channel] = wavelet->quantization[HL_BAND];
		highhigh_quantization[channel] = wavelet->quantization[HH_BAND];

		// Compute the pitch in units of pixels
		horizontal_pitch[channel] = wavelet->pitch/sizeof(PIXEL);

		// Compute the 8-bit pitch in units of pixels
		horizontal_pitch8s[channel] = wavelet->pitch8s/sizeof(PIXEL);
		//horizontal_pitch8s[channel] = wavelet->pitch8s/sizeof(PIXEL8S);

		// Remember the width of the horizontal wavelet rows for this channel
		horizontal_width[channel] = wavelet->width;

		//TODO: Need to recode the buffer allocations using the scratch space API

		// Divide the buffer into temporal lowpass and highpass rows
		temporal_lowpass[channel] = (PIXEL *)(buffer + (2 * channel) * temporal_row_size);
		temporal_highpass[channel] = (PIXEL *)(buffer + (2 * channel + 1) * temporal_row_size);
	}

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Output buffer: %d (0x%p)\n", output, output);
	}
#endif

/*	if (thread_index == 0)
	{
		row = 0;
		row_step = 1;
	}
	else if (thread_index == 1)
	{
		row = half_height - 1;
		row_step = -1;

		// Move to the bottom of the transform and process moving up
		for (channel = 0; channel < num_channels; channel++)
		{
			int offset = horizontal_pitch[channel] * (half_height - 1);

			horizontal_lowlow[channel] += offset;
			horizontal_lowhigh[channel] += offset;
			horizontal_highlow[channel] += offset;
			horizontal_highhigh[channel] += offset;

			horizontal_pitch[channel] = NEG(horizontal_pitch[channel]);
			horizontal_pitch8s[channel] = NEG(horizontal_pitch8s[channel]);
		}

		output += field_pitch * (half_height - 1);

		field_pitch = NEG(field_pitch);
	}
	else
	{
		assert(0); // what about middle threads?
	}

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Thread index: %d, start row: %d, row step: %d, field_pitch: %d\n",
				thread_index, row, row_step, field_pitch);
	}
#endif
*/
	// Loop until all of the rows have been processed
	for (;;)
	{
		// Wait for one row from each channel to invert the transform
		return_value = WaitForSingleObject(row_semaphore, 0);

		// Determine the index of this worker thread
		if (return_value == WAIT_OBJECT_0)
		{
			if(decoder->interlaced_worker.lock_init)
			{
				EnterCriticalSection(&decoder->interlaced_worker.lock);
			}
			row = decoder->interlaced_worker.current_row++;

			if(decoder->interlaced_worker.lock_init)
				LeaveCriticalSection(&decoder->interlaced_worker.lock);

			output_row_ptr = output;
			output_row_ptr += row * 2 * output_pitch;

			for (channel = 0; channel < num_channels; channel++)
			{
				int pitch = horizontal_pitch[channel];
				IMAGE *wavelet = transform[channel]->wavelet[frame_index];

				horizontal_lowlow[channel] = wavelet->band[LL_BAND];
				horizontal_lowhigh[channel] = wavelet->band[LH_BAND];
				horizontal_highlow[channel] = wavelet->band[HL_BAND];
				horizontal_highhigh[channel] = wavelet->band[HH_BAND];

				horizontal_lowlow[channel] += pitch*row;
				horizontal_lowhigh[channel] += pitch*row;
				horizontal_highlow[channel] += pitch*row;
				horizontal_highhigh[channel] += pitch*row;
			}
		}

		if (return_value == WAIT_OBJECT_0 && 0 <= row && row < half_height)
		{
			//PIXEL *line_buffer = (PIXEL *)(buffer + (2 * num_channels + 2) * temporal_row_size);
			PIXEL *line_buffer = (PIXEL *)(buffer + 2 * num_channels * temporal_row_size);

		//	assert(0 <= row && row < half_height);

#if (0 && DEBUG)
			if (logfile) {
				fprintf(logfile, "Processing row: %d, thread index: %d, output: %d (0x%p)\n",
						row, thread_index, output_row_ptr);
			}
#endif
			// Invert the horizontal transform applied to the temporal bands in each channel
			for (channel = 0; channel < num_channels; channel++)
			{
				int pitch = horizontal_pitch[channel];
				//int pitch8s = horizontal_pitch8s[channel];

#if (0 && DEBUG)
				// Invert the horizontal transform by duplicating the lowpass pixels
				InvertHorizontalRowDuplicated16s(horizontal_lowlow[channel], lowlow_quantization[channel],
												 (PIXEL8S *)horizontal_lowhigh[channel], lowhigh_quantization[channel],
												 temporal_lowpass[channel], horizontal_width[channel],
												 (PIXEL *)line_buffer);
#else
				// Invert the horizontal transform applied to the temporal lowpass row
				InvertHorizontalRow16s8sTo16sBuffered(horizontal_lowlow[channel], lowlow_quantization[channel],
											  (PIXEL8S *)horizontal_lowhigh[channel], lowhigh_quantization[channel],
											  temporal_lowpass[channel],
											  horizontal_width[channel],
											  (PIXEL *)line_buffer);
#endif
				// Invert the horizontal transform applied to the temporal highpass row
				InvertHorizontalRow8sBuffered((PIXEL8S *)horizontal_highlow[channel], highlow_quantization[channel],
									  (PIXEL8S *)horizontal_highhigh[channel], highhigh_quantization[channel],
									  temporal_highpass[channel],
									  horizontal_width[channel],
									  (PIXEL *)line_buffer);

				// Advance to the next row in each horizontal band in this channel
				//horizontal_lowlow[channel] += pitch;
				//horizontal_lowhigh[channel] += pitch;
				//horizontal_highlow[channel] += pitch;
				//horizontal_highhigh[channel] += pitch;
			}

			// The output width is twice the width of the wavelet bands
			output_width = 2 * horizontal_width[0];

			// Adjust the frame width to fill to the end of each row
			//frame_width = output_pitch / 2;

			if (precision == CODEC_PRECISION_10BIT)
			{
				// Invert the temporal bands from all channels and pack output pixels
				switch (frame->format)
				{
					// Need to reduce the resolution from 10 bits to 8 bits during the inverse

					case DECODED_FORMAT_YUYV:
						InvertInterlacedRow16s10bitToYUV(temporal_lowpass, temporal_highpass, num_channels,
														 output_row_ptr, output_pitch, output_width, frame_width,
														 chroma_offset);
						break;

					case DECODED_FORMAT_UYVY:
						InvertInterlacedRow16s10bitToUYVY(temporal_lowpass, temporal_highpass, num_channels,
														  output_row_ptr, output_pitch, output_width, frame_width,
														  chroma_offset);
						break;

					default:
						assert(0);
						break;
				}
			}

			else	// Older code for 8-bit precision
			{
				int format;

				assert(precision == CODEC_PRECISION_8BIT);

				switch (frame->format)
				{
					case DECODED_FORMAT_YUYV:
						format = COLOR_FORMAT_YUYV;
						break;

					case DECODED_FORMAT_UYVY:
						format = COLOR_FORMAT_UYVY;
						break;
				}

				// Invert the temporal bands from all channels and pack output pixels
				InvertInterlacedRow16sToYUV(temporal_lowpass, temporal_highpass, num_channels,
											output_row_ptr, output_pitch, output_width, frame_width,
											chroma_offset, format);
			}

			// Advance to the next row in the input transforms
			//row += row_step;

			// Advance to the next row in the packed output image
			//output += field_pitch;
		}
		else
		{
			// No more rows to process
			break;
		}
	}

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Finished transform, thread index: %d\n", thread_index);
	}
#endif
}

#endif


//#if BUILD_PROSPECT
// Apply the inverse horizontal-temporal transform and output rows of luma and chroma
#if 0
void TransformInverseFrameToRow16u(TRANSFORM *transform[], int frame_index, int num_channels,
								   PIXEL16U *output, int output_pitch, FRAME_INFO *frame,
								   char *buffer, size_t buffer_size, int chroma_offset,
								   int precision)
#else
void TransformInverseFrameToRow16u(DECODER *decoder, TRANSFORM *transform[], int frame_index, int num_channels,
								   PIXEL16U *output, int output_pitch, FRAME_INFO *frame,
								   const SCRATCH *scratch, int chroma_offset,
								   int precision)
#endif
{
	// Pointers to the rows in the horizontal wavelet for each channel
	PIXEL *horizontal_lowlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_lowhigh[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highhigh[TRANSFORM_MAX_CHANNELS];

	// Horizontal wavelet band width and pitch
	int horizontal_width[TRANSFORM_MAX_CHANNELS];
	int horizontal_pitch[TRANSFORM_MAX_CHANNELS];

	// Quantization factors
	int lowlow_quantization[TRANSFORM_MAX_CHANNELS];
	int lowhigh_quantization[TRANSFORM_MAX_CHANNELS];
	int highlow_quantization[TRANSFORM_MAX_CHANNELS];
	int highhigh_quantization[TRANSFORM_MAX_CHANNELS];

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
#if DEBUG
	size_t buffer_size = scratch->free_size;
#endif
	// Buffers for the rows in the temporal wavelet (reused for each channel)
	PIXEL *temporal_lowpass;
	PIXEL *temporal_highpass;

	int output_row_width[TRANSFORM_MAX_CHANNELS];

	// Dimensions of the reconstructed frame
	int frame_width = frame->width;
	int frame_height = frame->height;
	int half_height = frame_height / 2;
	size_t temporal_row_size = frame_width * sizeof(PIXEL);
	int field_pitch = 2 * output_pitch;

	int luma_width = frame_width;
	int chroma_width = luma_width/2;

	int channel;
	int row;

#if (1 && DEBUG_ROW16U)
	PIXEL16U *output_buffer;
#endif

	// This routine should only be called to decode rows of 16-bit luma and chroma
	//assert(frame->format == DECODED_FORMAT_YR16);

	// Round up the temporal row size to an integral number of cache lines
	temporal_row_size = ALIGN(temporal_row_size, _CACHE_LINE_SIZE);

	// Check that the buffer starts on a cache line boundary
	assert(ISALIGNED(buffer, _CACHE_LINE_SIZE));

	// Check that the number of channels is reasonable
	assert(0 < num_channels && num_channels <= TRANSFORM_MAX_CHANNELS);

	// Buffer must be large enough for two rows of temporal coefficients (lowpass and highpass)
	// plus the buffer used by the inverse horizontal transform for its intermediate results
#if DEBUG
	assert((2 * temporal_row_size) <= buffer_size);
#endif

	// Allocate buffers for one row of lowpass and highpass temporal coefficients
	temporal_lowpass = (PIXEL *)&buffer[0];
	temporal_highpass = (PIXEL *)&buffer[temporal_row_size];

#if (1 && DEBUG_ROW16U)
	output_buffer = (PIXEL16U *)&buffer[2 * temporal_row_size];
#endif

	// Initialize the arrays of row pointers into the horizontal transform bands
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

#if (0 && DEBUG)
		int static count = 0;
		if (count < 20) {
			char label[_MAX_PATH];
			int i;

			sprintf(label, "Frame%d-%d-", frame_index, count);
			DumpPGM(label, wavelet, NULL);

			for (i = 1; i < wavelet->num_bands; i++)
			{
				sprintf(label, "Frame-%d-band%d-%d-", frame_index, i, count);
				DumpBandPGM(label, wavelet, i, NULL);
			}
		}
		count++;
#endif
		// Initialize the row pointers into the horizontal bands
		horizontal_lowlow[channel] = wavelet->band[LL_BAND];
		horizontal_lowhigh[channel] = wavelet->band[LH_BAND];
		horizontal_highlow[channel] = wavelet->band[HL_BAND];
		horizontal_highhigh[channel] = wavelet->band[HH_BAND];

		lowlow_quantization[channel] = wavelet->quantization[LL_BAND];
		lowhigh_quantization[channel] = wavelet->quantization[LH_BAND];
		highlow_quantization[channel] = wavelet->quantization[HL_BAND];
		highhigh_quantization[channel] = wavelet->quantization[HH_BAND];

		// Compute the pitch in units of pixels
		horizontal_pitch[channel] = wavelet->pitch/sizeof(PIXEL);

		// Remember the width of the horizontal wavelet rows for this channel
		horizontal_width[channel] = wavelet->width;

		// Compute the width of each row of output pixels
		output_row_width[channel] = (channel == 0) ? luma_width : chroma_width;
	}

	// Process one row at a time from each channel
	for (row = 0; row < half_height; row++)
	{
#if (1 && DEBUG_ROW16U)
		PIXEL16U *output_row_ptr = output_buffer;
		PIXEL16U *planar_output[TRANSFORM_MAX_CHANNELS];
		int planar_pitch[TRANSFORM_MAX_CHANNELS];
		ROI strip = {luma_width, 2};
		uint8_t *yuv_output = (uint8_t *)output;
		uint8_t *output1 = yuv_output;
		uint8_t *output2 = yuv_output + output_pitch;
#else
		PIXEL16U *output_row_ptr = output;
#endif
		// Invert the horizontal transform applied to the temporal bands in each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			int pitch = horizontal_pitch[channel];

			if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
			{
				// Invert the horizontal transform applied to the temporal lowpass row
				BypassHorizontalRow16s(horizontal_lowlow[channel], horizontal_lowhigh[channel],
									   temporal_lowpass, horizontal_width[channel]);

				// Invert the horizontal transform applied to the temporal highpass row
				BypassHorizontalRow16s(horizontal_highlow[channel], horizontal_highhigh[channel],
									   temporal_highpass, horizontal_width[channel]);
			}
			else
			{
				// Invert the horizontal transform applied to the temporal lowpass row
				InvertHorizontalRow16s(horizontal_lowlow[channel], horizontal_lowhigh[channel],
									   temporal_lowpass, horizontal_width[channel]);

				// Invert the horizontal transform applied to the temporal highpass row
				InvertHorizontalRow16s(horizontal_highlow[channel], horizontal_highhigh[channel],
									   temporal_highpass, horizontal_width[channel]);
			}

			//***DEBUG***
			//ZeroMemory(temporal_highpass, temporal_row_size);
			//FillPixelMemory(temporal_highpass, temporal_row_size/sizeof(PIXEL), 50);

			// Advance to the next row in each horizontal band in this channel
			horizontal_lowlow[channel] += pitch;
			horizontal_lowhigh[channel] += pitch;
			horizontal_highlow[channel] += pitch;
			horizontal_highhigh[channel] += pitch;

#if (1 && DEBUG_ROW16U)
			// Write the rows of 16-bit pixels to a temporary buffer
			planar_output[channel] = output_row_ptr;
			planar_pitch[channel] = output_pitch * sizeof(PIXEL);

			// Invert the temporal transform and output two rows of luma or chroma
			InvertInterlacedRow16sToRow16u(temporal_lowpass, temporal_highpass,
										   planar_output[channel], planar_pitch[channel],
										   output_row_width[channel],
										   frame_width, chroma_offset, precision);
			//if (channel > 0)
			if (0)
			{
				uint8_t *output3 = (uint8_t *)planar_output[channel];
				uint8_t *output4 = (uint8_t *)output3 + planar_pitch[channel];
				int output_size = output_row_width[channel] * sizeof(PIXEL);
				int fill_value = (128 << 8);
				//ZeroMemory(output3, output_size);
				//ZeroMemory(output4, output_size);
				FillPixelMemory((PIXEL *)output3, output_row_width[channel], fill_value);
				FillPixelMemory((PIXEL *)output4, output_row_width[channel], fill_value);
			}
#else
			// Invert the temporal transform and output two rows of luma or chroma
			InvertInterlacedRow16sToRow16u(temporal_lowpass, temporal_highpass,
										   output_row_ptr, output_pitch, output_row_width[channel],
										   frame_width, chroma_offset, precision);
#endif
			// Advance the output row pointer to the next channel
			output_row_ptr += output_row_width[channel];

			// Check the output row alignment
			assert(ISALIGNED16(output_row_ptr));
		}

		// Advance to the next group of rows in the output image
		output += field_pitch/sizeof(PIXEL16U);
	}
}
//#endif


#if _INTERLACED_WORKER_THREADS

void TransformInverseFrameSectionToRow16u(DECODER *decoder, int thread_index, int frame_index, int num_channels,
										  PIXEL16U *output, int output_pitch, FRAME_INFO *frame,
										  int chroma_offset, int precision)
{
	FILE *logfile = decoder->logfile;
	TRANSFORM **transform = decoder->transform;
	const SCRATCH *scratch = &decoder->scratch;

	// Pointers to the rows in the horizontal wavelet for each channel
	PIXEL *horizontal_lowlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_lowhigh[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highhigh[TRANSFORM_MAX_CHANNELS];

	// Horizontal wavelet band width and pitch
	int horizontal_width[TRANSFORM_MAX_CHANNELS];
	int horizontal_pitch[TRANSFORM_MAX_CHANNELS];

	// Quantization factors
	int lowlow_quantization[TRANSFORM_MAX_CHANNELS];
	int lowhigh_quantization[TRANSFORM_MAX_CHANNELS];
	int highlow_quantization[TRANSFORM_MAX_CHANNELS];
	int highhigh_quantization[TRANSFORM_MAX_CHANNELS];

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	// Buffers for the rows in the temporal wavelet (reused for each channel)
	PIXEL *temporal_lowpass;
	PIXEL *temporal_highpass;

	int output_row_width[TRANSFORM_MAX_CHANNELS];

	// Dimensions of the reconstructed frame
	int frame_width = frame->width;
	int frame_height = frame->height;
	int half_height = frame_height / 2;
	size_t temporal_row_size = frame_width * sizeof(PIXEL);
	int field_pitch = 2 * output_pitch;

	int luma_width = frame_width;
	int chroma_width = luma_width/2;

	int channel;
	int row;

	HANDLE row_semaphore = decoder->interlaced_worker.row_semaphore;

	int return_value;

#if (1 && DEBUG_ROW16U)
	PIXEL16U *output_buffer;
#endif

	// This routine should only be called to decode rows of 16-bit luma and chroma
	//assert(frame->format == DECODED_FORMAT_YR16);

	// Round up the temporal row size to an integral number of cache lines
	temporal_row_size = ALIGN(temporal_row_size, _CACHE_LINE_SIZE);

#if 0
	if (thread_index == 1)
	{
		// Skip over the buffer space used by the other thread
		size_t buffer_usage = 2 * temporal_row_size;
		buffer += buffer_usage;
		buffer_size -= buffer_usage;
	}
#else
	// Divide the buffer space between the two threads
	buffer_size /= 4;
	buffer += buffer_size * thread_index;
#endif

	// Check that the buffer starts on a cache line boundary
	assert(ISALIGNED(buffer, _CACHE_LINE_SIZE));

	// Check that the number of channels is reasonable
	assert(0 < num_channels && num_channels <= TRANSFORM_MAX_CHANNELS);

	// Buffer must be large enough for two rows of temporal coefficients (lowpass and highpass)
	// plus the buffer used by the inverse horizontal transform for its intermediate results
	assert((2 * temporal_row_size) <= buffer_size);

	// Allocate buffers for one row of lowpass and highpass temporal coefficients
	temporal_lowpass = (PIXEL *)&buffer[0];
	temporal_highpass = (PIXEL *)&buffer[temporal_row_size];

#if (1 && DEBUG_ROW16U)
	output_buffer = (PIXEL16U *)&buffer[2 * temporal_row_size];
#endif

	// Initialize the arrays of row pointers into the horizontal transform bands
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

#if (0 && DEBUG)
		int static count = 0;
		if (count < 20) {
			char label[_MAX_PATH];
			int i;

			sprintf(label, "Frame%d-%d-", frame_index, count);
			DumpPGM(label, wavelet, NULL);

			for (i = 1; i < wavelet->num_bands; i++)
			{
				sprintf(label, "Frame-%d-band%d-%d-", frame_index, i, count);
				DumpBandPGM(label, wavelet, i, NULL);
			}
		}
		count++;
#endif
		// Initialize the row pointers into the horizontal bands
		horizontal_lowlow[channel] = wavelet->band[LL_BAND];
		horizontal_lowhigh[channel] = wavelet->band[LH_BAND];
		horizontal_highlow[channel] = wavelet->band[HL_BAND];
		horizontal_highhigh[channel] = wavelet->band[HH_BAND];

		lowlow_quantization[channel] = wavelet->quantization[LL_BAND];
		lowhigh_quantization[channel] = wavelet->quantization[LH_BAND];
		highlow_quantization[channel] = wavelet->quantization[HL_BAND];
		highhigh_quantization[channel] = wavelet->quantization[HH_BAND];

		// Compute the pitch in units of pixels
		horizontal_pitch[channel] = wavelet->pitch/sizeof(PIXEL);

		// Remember the width of the horizontal wavelet rows for this channel
		horizontal_width[channel] = wavelet->width;

		// Compute the width of each row of output pixels
		output_row_width[channel] = (channel == 0) ? luma_width : chroma_width;
	}

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Output buffer: %d (0x%p)\n", output, output);
	}
#endif

/*	if (thread_index == 0)
	{
		row = 0;
		row_step = 1;
	}
	else if (thread_index == 1)
	{
		row = half_height - 1;
		row_step = -1;

		// Move to the bottom of the transform and process moving up
		for (channel = 0; channel < num_channels; channel++)
		{
			int offset = horizontal_pitch[channel] * (half_height - 1);

			horizontal_lowlow[channel] += offset;
			horizontal_lowhigh[channel] += offset;
			horizontal_highlow[channel] += offset;
			horizontal_highhigh[channel] += offset;

			horizontal_pitch[channel] = NEG(horizontal_pitch[channel]);
			//horizontal_pitch8s[channel] = NEG(horizontal_pitch8s[channel]);
		}

		//output += field_pitch * (half_height - 1);
		output += (frame_height - 1) * output_pitch/sizeof(PIXEL16U);
		output_pitch = NEG(output_pitch);
		field_pitch = NEG(field_pitch);
	}
	else
	{
		assert(0); // middle threads
	}
	*/
#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Thread index: %d, start row: %d, row step: %d, field_pitch: %d\n",
				thread_index, row, row_step, field_pitch);
	}
#endif

	// Loop until all of the rows have been processed
	for (;;)
	{
		PIXEL16U *output_row_ptr;
		// Wait for one row from each channel to invert the transform
		return_value = WaitForSingleObject(row_semaphore, 0);

		// Determine the index of this worker thread
		if (return_value == WAIT_OBJECT_0)
		{
			if(decoder->interlaced_worker.lock_init)
			{
				EnterCriticalSection(&decoder->interlaced_worker.lock);
			}
			row = decoder->interlaced_worker.current_row++;

			if(decoder->interlaced_worker.lock_init)
				LeaveCriticalSection(&decoder->interlaced_worker.lock);

			output_row_ptr = output;
			output_row_ptr += row * output_pitch;

			for (channel = 0; channel < num_channels; channel++)
			{
				int pitch = horizontal_pitch[channel];
				IMAGE *wavelet = transform[channel]->wavelet[frame_index];

				horizontal_lowlow[channel] = wavelet->band[LL_BAND];
				horizontal_lowhigh[channel] = wavelet->band[LH_BAND];
				horizontal_highlow[channel] = wavelet->band[HL_BAND];
				horizontal_highhigh[channel] = wavelet->band[HH_BAND];

				horizontal_lowlow[channel] += pitch*row;
				horizontal_lowhigh[channel] += pitch*row;
				horizontal_highlow[channel] += pitch*row;
				horizontal_highhigh[channel] += pitch*row;
			}
		}

		if (return_value == WAIT_OBJECT_0 && 0 <= row && row < half_height)
		{
			assert(0 <= row && row < half_height);

			if(decoder->frame.resolution == DECODED_RESOLUTION_FULL) 
			{
				// Invert the horizontal transform applied to the temporal bands in each channel
				for (channel = 0; channel < num_channels; channel++)
				{
					int pitch = horizontal_pitch[channel];

					// Invert the horizontal transform applied to the temporal lowpass row
					InvertHorizontalRow16s(horizontal_lowlow[channel], horizontal_lowhigh[channel],
										   temporal_lowpass, horizontal_width[channel]);

					// Invert the horizontal transform applied to the temporal highpass row
					InvertHorizontalRow16s(horizontal_highlow[channel], horizontal_highhigh[channel],
										   temporal_highpass, horizontal_width[channel]);

					// Invert the temporal transform and output two rows of luma or chroma
					InvertInterlacedRow16sToRow16u(temporal_lowpass, temporal_highpass,
												   output_row_ptr, output_pitch, output_row_width[channel],
												   frame_width, chroma_offset, precision);

					// Advance the output row pointer to the next channel
					output_row_ptr += output_row_width[channel];
				}
			}
			else if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
			{
				// Invert the horizontal transform applied to the temporal bands in each channel
				for (channel = 0; channel < num_channels; channel++)
				{
					int pitch = horizontal_pitch[channel];

					// Invert the horizontal transform applied to the temporal lowpass row
					BypassHorizontalRow16s(horizontal_lowlow[channel], horizontal_lowhigh[channel],
										   temporal_lowpass, horizontal_width[channel]);

					// Invert the horizontal transform applied to the temporal highpass row
					BypassHorizontalRow16s(horizontal_highlow[channel], horizontal_highhigh[channel],
										   temporal_highpass, horizontal_width[channel]);

					// Invert the temporal transform and output two rows of luma or chroma
					InvertInterlacedRow16sToRow16u(temporal_lowpass, temporal_highpass,
												   output_row_ptr, output_pitch, output_row_width[channel],
												   frame_width, chroma_offset, precision);

					// Advance the output row pointer to the next channel
					output_row_ptr += output_row_width[channel];
				}
			}
		}
		else
		{
			// No more rows to process
			break;
		}
	}

#if (1 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Finished transform, thread index: %d\n", thread_index);
	}
#endif
}

#endif


#if 0

DWORD WINAPI TransformInverseFrameToRow16utopThread(LPVOID param)
{
	struct data
	{
		TRANSFORM *transform[3];
		int frame_index;
		int num_channels;
		uint8_t *output;
		int output_pitch;
		FRAME_INFO *info;
		SCRATCH *scratch;
		int chroma_offset;
		int precision;
	} *dptr;

	dptr = (struct data *)param;


	TransformInverseFrameToRow16utop(dptr->transform, dptr->frame_index, dptr->num_channels,
								  (PIXEL16U *)dptr->output, dptr->output_pitch, dptr->info,
								  dptr->scratch, dptr->chroma_offset, dptr->precision);

	return 0;
}

DWORD WINAPI TransformInverseFrameToRow16ubottomThread(LPVOID param)
{
	struct data
	{
		TRANSFORM *transform[3];
		int frame_index;
		int num_channels;
		uint8_t *output;
		int output_pitch;
		FRAME_INFO *info;
		SCRATCH *scratch;
		int chroma_offset;
		int precision;
	} *dptr;

	dptr = (struct data *)param;


	TransformInverseFrameToRow16ubottom(dptr->transform, dptr->frame_index, dptr->num_channels,
								  (PIXEL16U *)dptr->output, dptr->output_pitch, dptr->info,
								  dptr->scratch, dptr->chroma_offset, dptr->precision);

	return 0;
}

#endif


extern void fast_srand( int seed );

// Apply the inverse horizontal-temporal transform and pack the output into a buffer
#if 0
void TransformInverseFrameToBuffer(TRANSFORM *transform[], int frame_index, int num_channels,
								   uint8_t *output, int output_pitch, FRAME_INFO *frame,
								   char *buffer, size_t buffer_size, int chroma_offset,
								   int precision)
#else
void TransformInverseFrameToBuffer(TRANSFORM *transform[], int frame_index, int num_channels,
								   uint8_t *output, int output_pitch, FRAME_INFO *frame,
								   const SCRATCH *scratch, int chroma_offset, int precision)
#endif
{
	// Pointers to the rows in the horizontal wavelet for each channel
	PIXEL *horizontal_lowlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_lowhigh[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highhigh[TRANSFORM_MAX_CHANNELS];

	// Horizontal wavelet band width and pitch
	int horizontal_width[TRANSFORM_MAX_CHANNELS];
	int horizontal_pitch[TRANSFORM_MAX_CHANNELS];
	//int horizontal_pitch8s[TRANSFORM_MAX_CHANNELS];

	// Quantization factors
	int lowlow_quantization[TRANSFORM_MAX_CHANNELS];
	int lowhigh_quantization[TRANSFORM_MAX_CHANNELS];
	int highlow_quantization[TRANSFORM_MAX_CHANNELS];
	int highhigh_quantization[TRANSFORM_MAX_CHANNELS];

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	// Pointers to the rows in the temporal wavelet for each channel
	PIXEL *temporal_lowpass[TRANSFORM_MAX_CHANNELS];
	PIXEL *temporal_highpass[TRANSFORM_MAX_CHANNELS];

	// Dimensions of the reconstructed frame
	int frame_width = frame->width;
	int frame_height = frame->height;
	int half_height = frame_height / 2;
	size_t temporal_row_size = frame_width * sizeof(PIXEL);
	size_t temporal_buffer_size = 2 * num_channels * temporal_row_size;
#if DEBUG
	size_t yuv_row_size = frame_width * 2;
#endif
	char *yuv_buffer;
	size_t yuv_buffer_size;
	int field_pitch = 2 * output_pitch;
	int format = frame->format;
	bool inverted = (format == DECODED_FORMAT_RGB24 || format == DECODED_FORMAT_RGB32);
	int output_width;
	int channel;
	int row;

	// Round up the temporal row size to an integral number of cache lines
	temporal_row_size = ALIGN(temporal_row_size, _CACHE_LINE_SIZE);

	// Check that the buffer starts on a cache line boundary
	assert(ISALIGNED(buffer, _CACHE_LINE_SIZE));

	// Check that the number of channels is reasonable
	assert(0 < num_channels && num_channels <= TRANSFORM_MAX_CHANNELS);

	// Check that the buffer is large enough
	assert((2 * num_channels * temporal_row_size) <= buffer_size);

	// Allocate buffers for a single row of lowpass and highpass temporal coefficients
	// and initialize the arrays of row pointers into the horizontal transform bands
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Initialize the row pointers into the horizontal bands
		horizontal_lowlow[channel] = wavelet->band[LL_BAND];
		horizontal_lowhigh[channel] = wavelet->band[LH_BAND];
		horizontal_highlow[channel] = wavelet->band[HL_BAND];
		horizontal_highhigh[channel] = wavelet->band[HH_BAND];

		lowlow_quantization[channel] = wavelet->quantization[LL_BAND];
		lowhigh_quantization[channel] = wavelet->quantization[LH_BAND];
		highlow_quantization[channel] = wavelet->quantization[HL_BAND];
		highhigh_quantization[channel] = wavelet->quantization[HH_BAND];

		// Compute the pitch in units of pixels
		horizontal_pitch[channel] = wavelet->pitch/sizeof(PIXEL);

		// Compute the 8-bit pitch in units of pixels
		//horizontal_pitch8s[channel] = wavelet->pitch8s/sizeof(PIXEL);
		//horizontal_pitch8s[channel] = wavelet->pitch8s/sizeof(PIXEL8S);

		// Remember the width of the horizontal wavelet rows for this channel
		horizontal_width[channel] = wavelet->width;

		// Divide the buffer into temporal lowpass and highpass rows
		temporal_lowpass[channel] = (PIXEL *)(buffer + (2 * channel) * temporal_row_size);
		temporal_highpass[channel] = (PIXEL *)(buffer + (2 * channel + 1) * temporal_row_size);
	}

	// Allocate buffer space for the intermediate YUV data
	yuv_buffer = buffer + temporal_buffer_size;
	yuv_buffer_size = buffer_size - temporal_buffer_size;
#if DEBUG
	assert(yuv_buffer_size >= 2 * yuv_row_size);
#endif

	if (inverted)
	{
		output += (frame_height - 1) * output_pitch;
		output_pitch = (- output_pitch);
		field_pitch = (- field_pitch);
	}

	// Process one row at a time from each channel
	for (row = 0; row < half_height; row++)
	{
		PIXEL *line_buffer = (PIXEL *)(buffer + (2 * num_channels + 2) * temporal_row_size);

		// Invert the horizontal transform applied to the temporal bands in each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			int pitch = horizontal_pitch[channel];
			//int pitch8s = horizontal_pitch8s[channel];

			// Invert the horizontal transform applied to the temporal lowpass row
			InvertHorizontalRow16s8sTo16sBuffered(horizontal_lowlow[channel], lowlow_quantization[channel],
										  (PIXEL8S *)horizontal_lowhigh[channel], lowhigh_quantization[channel],
										  temporal_lowpass[channel],
										  horizontal_width[channel],
										  (PIXEL *)line_buffer);

			// Invert the horizontal transform applied to the temporal highpass row
			InvertHorizontalRow8sBuffered((PIXEL8S *)horizontal_highlow[channel], highlow_quantization[channel],
								  (PIXEL8S *)horizontal_highhigh[channel], highhigh_quantization[channel],
								  temporal_highpass[channel],
								  horizontal_width[channel],
								  (PIXEL *)line_buffer);

			// Advance to the next row in each horizontal band in this channel
			horizontal_lowlow[channel] += pitch;
			horizontal_lowhigh[channel] += pitch;
			horizontal_highlow[channel] += pitch;
			horizontal_highhigh[channel] += pitch;
		}

		// The output width is twice the width of the wavelet bands
		output_width = 2 * horizontal_width[0];

		// Adjust the frame width to fill to the end of each row
		//frame_width = output_pitch / 2;

//#if BUILD_PROSPECT
		if (format == DECODED_FORMAT_V210 || format == DECODED_FORMAT_YU64)
		{
			// Invert the temporal bands from all channels and pack as V210 output
			InvertInterlacedRow16sToV210(temporal_lowpass, temporal_highpass, num_channels,
										 output, output_pitch, output_width, frame_width,
										 yuv_buffer, yuv_buffer_size, format, chroma_offset, precision);
		}
		else
//#endif
		{
			// Invert the temporal bands from all channels and pack as 8-bit output
			InvertInterlacedRow16s(temporal_lowpass, temporal_highpass, num_channels,
								   output, output_pitch, output_width, frame_width,
								   yuv_buffer, yuv_buffer_size, format, frame->colorspace,
								   chroma_offset, precision, row);
		}

		// Advance to the next row in the packed output image
		output += field_pitch;
	}
}

void CopyImageToBuffer(IMAGE *image, uint8_t *output_buffer, int32_t output_pitch, int format)
{
	bool inverted = false;
	size_t output_size;

	START(tk_convert);

	// Determine the type of conversion
	switch (format)
	{
	case DECODED_FORMAT_RGB24:
		inverted = true;
		// Fall through and convert to RGB (first image row displayed at the bottom)

	case DECODED_FORMAT_RGB24_INVERTED:
		ConvertImageToRGB(image, output_buffer, output_pitch, COLOR_FORMAT_RGB24, inverted);
		break;

	case DECODED_FORMAT_RGB32:
		inverted = true;
		// Fall through and convert to RGB (first image row displayed at the bottom)

	case DECODED_FORMAT_RGB32_INVERTED:
		ConvertImageToRGB(image, output_buffer, output_pitch, COLOR_FORMAT_RGB32, inverted);
		break;

#if 0
	case DECODED_FORMAT_YUYV_INVERTED:
		inverted = true;
		// Fall through and convert to YUV (first image row displayed at the bottom)
#endif

	case DECODED_FORMAT_YUYV:
		ConvertImageToYUV(image, output_buffer, output_pitch, COLOR_FORMAT_YUYV, inverted);
		break;

#if 0
	case DECODED_FORMAT_UYVY_INVERTED:
		inverted = true;
		// Fall through and convert to YUV (first image row displayed at the bottom)
#endif

	case DECODED_FORMAT_UYVY:
		ConvertImageToYUV(image, output_buffer, output_pitch, COLOR_FORMAT_UYVY, inverted);
		break;

	default:				// Unsupported format (return a blank frame)
		assert(0);
		output_size = image->height * output_pitch;
		memset(output_buffer, COLOR_CHROMA_ZERO, output_size);
		break;
	}

	STOP(tk_convert);
}



void SideLowpass16s10bitToYUYV(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height,
								 int output_pitch, bool inverted)
{
	IMAGE *y_image = images[0];
	IMAGE *u_image = images[1];
	IMAGE *v_image = images[2];
	int width = y_image->width;
	int height = output_height;

	PIXEL *y_row_ptr = y_image->band[0];
	PIXEL *u_row_ptr = u_image->band[0];
	PIXEL *v_row_ptr = v_image->band[0];
	int y_pitch = y_image->pitch/sizeof(PIXEL);
	int u_pitch = u_image->pitch/sizeof(PIXEL);
	int v_pitch = v_image->pitch/sizeof(PIXEL);

	uint8_t *outrow = output_buffer;
	uint8_t *outptr;
	int row, column;

	// Definitions for optimization
	//const int column_step = 2 * sizeof(__m64);

	// Column at which post processing must begin
	//int post_column = width - (width % column_step);

	// The output pitch should be a positive number before inversion
	assert(output_pitch > 0);

	// Should the image be inverted?
	if (inverted) {
		outrow += (height - 1) * output_pitch;		// Start at the bottom row
		output_pitch = NEG(output_pitch);			// Negate the pitch to go up
	}

	for (row = 0; row < height; row++)
	{
		outptr = outrow;

		// Fill the rest of the output row
		for (column = 0; column < width; column+=4)
		{
			int chroma_column = column>>1;
			*(outptr++) = SATURATE_8U((y_row_ptr[column]+y_row_ptr[column+1])>>5);
			*(outptr++) = SATURATE_8U((v_row_ptr[chroma_column]+v_row_ptr[chroma_column+1])>>5);
			*(outptr++) = SATURATE_8U((y_row_ptr[column+2]+y_row_ptr[column+3])>>5);
			*(outptr++) = SATURATE_8U((u_row_ptr[chroma_column]+u_row_ptr[chroma_column+1])>>5);
		}

		// Advance to the next rows in the input and output images
		y_row_ptr += y_pitch;// 3D Work
		u_row_ptr += u_pitch;
		v_row_ptr += v_pitch;

		outrow += output_pitch;
	}
}




// Convert 16-bit signed lowpass data into packed RGB/YUV and store it in the output buffer
void CopyLowpass16sToBuffer(DECODER *decoder, IMAGE *images[], int num_channels, uint8_t *output_buffer, int32_t output_pitch,
							FRAME_INFO *info, int chroma_offset, int precision, int encode_format, int whitebitdepth)
{
	//IMAGE *image = frame->channel[0];
	bool inverted = false;
	int output_width = info->width;
	int output_height = info->height;
	int descale = precision - 8;

	// Get the color format from the decoded format
	int color_format = info->format & COLOR_FORMAT_MASK;

	// Must compile this routine with switches set for decoding to 8-bit unsigned pixels
#if !defined(_DECODE_FRAME_8U) || (_DECODE_FRAME_8U == 0)
	assert(0);
	return;
#endif

	START(tk_convert);

#if 0
	// Fill the output buffer with blank values
	EraseOutputBuffer(output_buffer, info->width, info->height, output_pitch, info->format);
#endif


	// Determine the type of conversion
	switch (info->format)
	{
	case DECODED_FORMAT_RGB24:
		inverted = true;
		// Fall through and convert to RGB (first image row displayed at the bottom)

	case DECODED_FORMAT_RGB24_INVERTED:
		if(encode_format == ENCODED_FORMAT_RGB_444 || encode_format == ENCODED_FORMAT_RGBA_4444)
		{
			ConvertLowpass16sRGB48ToRGB(images, output_buffer, output_width, output_height, output_pitch,
										COLOR_FORMAT_RGB24, info->colorspace, inverted, descale, num_channels);
		}
		else
		{
			ConvertLowpass16sToRGBNoIPPFast(images, output_buffer, output_width, output_height, output_pitch,
										COLOR_FORMAT_RGB24, info->colorspace, inverted, descale);
		}
		break;

	case DECODED_FORMAT_RGB32:
		inverted = true;
		// Fall through and convert to RGB (first image row displayed at the bottom)

	case DECODED_FORMAT_RGB32_INVERTED:
		if(encode_format == ENCODED_FORMAT_RGB_444 || encode_format == ENCODED_FORMAT_RGBA_4444)
		{
			ConvertLowpass16sRGB48ToRGB(images, output_buffer, output_width, output_height, output_pitch,
										COLOR_FORMAT_RGB32, info->colorspace, inverted, descale, num_channels);
		}
		else
		{
			ConvertLowpass16sToRGBNoIPPFast(images, output_buffer, output_width, output_height, output_pitch,
										COLOR_FORMAT_RGB32, info->colorspace, inverted, descale);
		}
		break;

	case DECODED_FORMAT_RG48:
		if(encode_format == ENCODED_FORMAT_BAYER)
		{
			ConvertLowpass16sBayerToRGB48(images, output_buffer, output_width, output_height,
				output_pitch, 2, num_channels);
		}
		else if(encode_format == ENCODED_FORMAT_RGB_444 || encode_format == ENCODED_FORMAT_RGBA_4444)
		{
			int scale = 1;
			if(encode_format == ENCODED_FORMAT_RGB_444 || encode_format == ENCODED_FORMAT_RGBA_4444)
				scale = 2;
			ConvertLowpass16sRGB48ToRGB48(images, output_buffer, output_width, output_height,
				output_pitch, scale, num_channels);
		}
		else
		{
			ConvertLowpass16sYUVtoRGB48(images, (uint8_t *)output_buffer, output_width,
				output_height, output_pitch, info->colorspace, inverted, descale,
				info->format, whitebitdepth);
		}
		break;


	case DECODED_FORMAT_RG64:
		if(encode_format == ENCODED_FORMAT_RGB_444 || encode_format == ENCODED_FORMAT_RGBA_4444)
		{
			ConvertLowpass16sRGBA64ToRGBA64(images, output_buffer, output_width, output_height, output_pitch,
									descale, num_channels, info->format & 0xffff);
		}
		else
		{
			assert(0);
		}
		break;
	case DECODED_FORMAT_B64A:
	case DECODED_FORMAT_R210:
	case DECODED_FORMAT_DPX0:
	case DECODED_FORMAT_RG30:
	case DECODED_FORMAT_AR10:
	case DECODED_FORMAT_AB10:
		if(encode_format == ENCODED_FORMAT_RGB_444 || encode_format == ENCODED_FORMAT_RGBA_4444)
		{
			ConvertLowpass16sRGBA64ToRGBA64(images, output_buffer, output_width, output_height, output_pitch,
									descale, num_channels, info->format & 0xffff);
		}
		else
		{
			ConvertLowpass16sYUVtoRGB48(images, (uint8_t *)output_buffer, output_width,
				output_height, output_pitch, info->colorspace, inverted, descale,
				info->format, whitebitdepth);
		}
		break;
#if 0
	case DECODED_FORMAT_YUYV_INVERTED:
		inverted = true;
		// Fall through and convert to YUV (first image row displayed at the bottom)
#endif

	case DECODED_FORMAT_YUYV:
	case DECODED_FORMAT_UYVY:
		if (precision == CODEC_PRECISION_10BIT)
		{
			int lineskip = 1; // 3D Work
			int pitch = output_pitch;

			if(decoder->channel_decodes > 1 && decoder->frame.format == DECODED_FORMAT_YUYV)
			{
				if(decoder->channel_blend_type == BLEND_STACKED_ANAMORPHIC || decoder->channel_blend_type == BLEND_LINE_INTERLEAVED) // 3d Work
				{
					lineskip = 2;
					if(decoder->channel_blend_type == 3)
						pitch *= 2;

				}
			}

			if((decoder->channel_blend_type == BLEND_SIDEBYSIDE_ANAMORPHIC || decoder->channel_blend_type == BLEND_FREEVIEW) && decoder->frame.format == DECODED_FORMAT_YUYV) //side by side
			{
				SideLowpass16s10bitToYUYV(images, output_buffer, output_width, output_height, pitch, inverted);
			}
			else
			{
				//ConvertLowpass16s10bitToYUV(images, output_buffer, output_width, output_height, pitch, COLOR_FORMAT_YUYV, inverted, lineskip);
				ConvertLowpass16s10bitToYUV(images, output_buffer, output_width, output_height, pitch, color_format, inverted, lineskip);
			}
		}
		else
		{
			//ConvertLowpass16sToYUV(images, output_buffer, output_width, output_height, output_pitch, COLOR_FORMAT_YUYV, inverted);
			ConvertLowpass16sToYUV(images, output_buffer, output_width, output_height, output_pitch, color_format, inverted);
		}
		break;

#if 0
	case DECODED_FORMAT_UYVY_INVERTED:
		inverted = true;
		// Fall through and convert to YUV (first image row displayed at the bottom)
#endif
#if 0
	case DECODED_FORMAT_UYVY:
		ConvertLowpass16sToYUV(images, output_buffer, output_width, output_height, output_pitch, COLOR_FORMAT_UYVY, inverted);
		break;
#endif

//#if BUILD_PROSPECT
	case DECODED_FORMAT_V210:
		if (precision == CODEC_PRECISION_10BIT)
		{
			ConvertLowpass16s10bitToV210(images, output_buffer, output_width, output_height, output_pitch, COLOR_FORMAT_V210, inverted);
		}
		else
		{
			//ConvertLowpass16sToV210(images, output_buffer, output_width, output_pitch, COLOR_FORMAT_V210, inverted);
			assert(0);
		}
		break;
//#endif

	case DECODED_FORMAT_YU64:
		// DAN04262004
		ConvertLowpass16sToYUV64(images, output_buffer, output_width, output_height, output_pitch, COLOR_FORMAT_YU64, inverted, precision);
		break;

//#if BUILD_PROSPECT
	case DECODED_FORMAT_YR16:
		ConvertLowpass16sToYR16(images, output_buffer, output_width, output_height,  output_pitch, COLOR_FORMAT_YR16, inverted, precision);
		break;
//#endif

	default:				// Unsupported format (output a blank frame)
		assert(0);
		break;
	}

	STOP(tk_convert);
}

void ConvertYUVStripPlanarToBuffer(uint8_t *planar_output[], int planar_pitch[], ROI roi,
								   uint8_t *output_buffer, int output_pitch, int frame_width,
								   int format, int colorspace)
{
	bool inverted = false;
	int output_width = roi.width;

#if !defined(_DECODE_FRAME_8U) || (_DECODE_FRAME_8U == 0)
#error Must set compile-time switches to decode to 8-bit pixels
#endif

	START(tk_convert);

#if _ENCODE_CHROMA_OFFSET
#error Cannot handle images encoded with a non-zero chroma offset
#endif

	// Determine the type of conversion
	switch(format)
	{
	case DECODED_FORMAT_RGB24:
		inverted = true;
		// Fall through and convert to RGB (first image row displayed at the bottom)

	case DECODED_FORMAT_RGB24_INVERTED:
		ConvertPlanarYUVToRGB(planar_output, planar_pitch, roi, output_buffer, output_width, output_pitch,
							  COLOR_FORMAT_RGB24, colorspace, inverted);
		break;

	case DECODED_FORMAT_RGB32:
		inverted = true;
		// Fall through and convert to RGB (first image row displayed at the bottom)

	case DECODED_FORMAT_RGB32_INVERTED:
		ConvertPlanarYUVToRGB(planar_output, planar_pitch, roi, output_buffer, output_width, output_pitch,
							  COLOR_FORMAT_RGB32, colorspace, inverted);
		break;

#if 0
	case DECODED_FORMAT_YUYV_INVERTED:
		inverted = true;
		// Fall through and convert to YUV (first image row displayed at the bottom)
#endif

	case DECODED_FORMAT_YUYV:
		ConvertYUVStripPlanarToPacked(planar_output, planar_pitch, roi,
									  output_buffer, output_pitch, frame_width, format);
		break;

#if 0
	case DECODED_FORMAT_UYVY_INVERTED:
		inverted = true;
		// Fall through and convert to YUV (first image row displayed at the bottom)
#endif

	case DECODED_FORMAT_UYVY:
		ConvertPlanarYUVToUYVY(planar_output, planar_pitch, roi, output_buffer, output_width, output_pitch,
							   COLOR_FORMAT_UYVY, colorspace, inverted);
		break;

	default:				// Unsupported format (output a blank frame)
		assert(0);
		break;
	}

	STOP(tk_convert);
}

void ConvertRow16uToDitheredBuffer(DECODER *decoder, uint8_t *planar_output[], int planar_pitch[], ROI roi,
								   uint8_t *output_buffer, int output_pitch, int frame_width,
								   int format, int colorspace)
{
	bool inverted = false;
	int output_width = roi.width;

	START(tk_convert);

	// Determine the type of conversion
	switch(format)
	{
	case DECODED_FORMAT_RGB24:
		inverted = true;
		// Fall through and convert to RGB (first image row displayed at the bottom)

	case DECODED_FORMAT_RGB24_INVERTED:
		//ConvertPlanarYUVToRGB
		ConvertRow16uToDitheredRGB(decoder, planar_output, planar_pitch, roi, output_buffer, output_width, output_pitch,
							  COLOR_FORMAT_RGB24, colorspace, inverted);
		break;

	case DECODED_FORMAT_RGB32:
		inverted = true;
		// Fall through and convert to RGB (first image row displayed at the bottom)

	case DECODED_FORMAT_RGB32_INVERTED:
		ConvertRow16uToDitheredRGB(decoder, planar_output, planar_pitch, roi, output_buffer, output_width, output_pitch,
							  COLOR_FORMAT_RGB32, colorspace, inverted);
		break;


	case COLOR_FORMAT_WP13:
	case COLOR_FORMAT_B64A:
	case COLOR_FORMAT_RG48:
	case COLOR_FORMAT_R210:
	case COLOR_FORMAT_DPX0:
	case COLOR_FORMAT_RG30:
	case COLOR_FORMAT_AR10:
	case COLOR_FORMAT_AB10:
		ConvertYUVRow16uToBGRA64(planar_output, planar_pitch, roi, output_buffer, output_width, output_pitch, format, colorspace, NULL, NULL);
		break;


	case DECODED_FORMAT_YUYV:
		assert(0);// These routines are not yet updated for ROW16u inputs
		ConvertYUVStripPlanarToPacked(planar_output, planar_pitch, roi,
							  output_buffer, output_pitch, frame_width, format);
		break;

	case DECODED_FORMAT_UYVY:
		assert(0);// These routines are not yet updated for ROW16u inputs
		ConvertPlanarYUVToUYVY(planar_output, planar_pitch, roi, output_buffer, output_width, output_pitch,
							   COLOR_FORMAT_UYVY, colorspace, inverted);
		break;

	default:				// Unsupported format (output a blank frame)
		assert(0);
		break;
	}

	STOP(tk_convert);
}



// Convert one row of packed YUYV to the specified color
void ConvertRowYUYV(uint8_t *input, uint8_t *output, int length, int format, int colorspace, int precision)
{
	size_t row_size = 2 * length;
	bool inverted = false;

	START(tk_convert);

	// Determine the type of color conversion
	switch (format)
	{
	case DECODED_FORMAT_RGB24:
		inverted = true;
		// Fall through and convert to RGB (first image row displayed at the bottom)

	case DECODED_FORMAT_RGB24_INVERTED:
		ConvertYUYVRowToRGB(input, output, length, COLOR_FORMAT_RGB24, colorspace, precision);
		break;

	case DECODED_FORMAT_RGB32:
		inverted = true;
		// Fall through and convert to RGB (first image row displayed at the bottom)

	case DECODED_FORMAT_RGB32_INVERTED:
 		ConvertYUYVRowToRGB(input, output, length, COLOR_FORMAT_RGB32, colorspace, precision);
		break;

	case DECODED_FORMAT_YUYV:
		if(precision == 8)
			memcpy(output, input, row_size);
		else
		{
			//need to dither to 8-bit
			assert(0);
		}
		break;

	case DECODED_FORMAT_UYVY:
		if(precision == 8)
			ConvertYUYVRowToUYVY(input, output, length, COLOR_FORMAT_UYVY);
		else
		{
			//need to dither to 8-bit
			assert(0);
		}
		break;

//#if BUILD_PROSPECT
	case DECODED_FORMAT_V210:
		assert(0); // should get here with 8bit data.
		//ConvertYUYVRowToV210(input, output, length, COLOR_FORMAT_V210);
		break;

	case DECODED_FORMAT_YU64:
		assert(0); // should get here with 8bit data.
		//ConvertYUYVRowToYU64(input, output, length, COLOR_FORMAT_YU64);
		break;

	case DECODED_FORMAT_BYR3:
	case DECODED_FORMAT_BYR4:
		assert(0); // should get here with 8bit data.
		//ConvertYUYVRowToYU64(input, output, length, COLOR_FORMAT_YU64);
		break;
//#endif

	default:				// Unsupported format (output a blank frame)
		assert(0);
		memset(output, 0, row_size);
		break;
	}

	STOP(tk_convert);
}


#if _THREADED_DECODER

IMAGE *GetWaveletThreadSafe(DECODER *decoder, TRANSFORM *transform, int index,
							int width, int height, int level, int type)
{
	IMAGE *wavelet = transform->wavelet[index];

	assert(decoder != NULL && transform != NULL);
	if (decoder != NULL && transform != NULL)
	{
		
#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif

		// Lock access to the wavelet data
#if _DELAYED_THREAD_START==0
		Lock(&decoder->entropy_worker_new.lock);
#endif

		// Get the wavelet from the transform data structure (thread safe)
		wavelet = transform->wavelet[index];

		// Allocate (or reallocate) the wavelet
#if _ALLOCATOR
		wavelet = ReallocWaveletEx(decoder->allocator, wavelet, width, height, level, type);
#else
		wavelet = ReallocWaveletEx(wavelet, width, height, level, type);
#endif
		// Save this wavelet in the transform data structure
		transform->wavelet[index] = wavelet;

		// Unlock access to the wavelet data
#if _DELAYED_THREAD_START==0
		Unlock(&decoder->entropy_worker_new.lock);
#endif
	}

	return wavelet;
}

// Update the codec state with the information in a tag value pair
CODEC_ERROR UpdateCodecState(DECODER *decoder, BITSTREAM *input, CODEC_STATE *codec, TAGWORD tag, TAGWORD value)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif

	bool optional = false;
	int chunksize = 0;

	bool result;

	// Is this an optional tag?
	if (tag < 0) {
		tag = NEG(tag);
		optional = true;
	}

#if (0 && DEBUG)
	if (logfile) {
		fprintf(logfile, "UpdateCodecState tag: %d, value: %d, optional: %d\n",
				tag, value, optional);
	}
#endif

	switch (tag)
	{
	case CODEC_TAG_ZERO:				// Used internally
		assert(0);						// Should not occur in the bitstream
		error = CODEC_ERROR_INVALID_BITSTREAM;
		break;

	case CODEC_TAG_SAMPLE:				// Type of sample
		//assert(0);
		if (value == SAMPLE_TYPE_CHANNEL)
		{
			result = DecodeSampleChannelHeader(decoder, input);
			if (!result)
				error = CODEC_ERROR_DECODE_SAMPLE_CHANNEL_HEADER;
			else
				error = CODEC_ERROR_OKAY;
		}
		break;

	case CODEC_TAG_INDEX:				// Sample index table
		//assert(0);					// Need to figure out how to return the group index
		{
			int count = value;
			uint32_t *index = (uint32_t *)(&codec->channel_size[0]);
			DecodeGroupIndex(input, index, count);
			codec->num_channels = count;
		}
		break;

	case CODEC_TAG_SUBBAND:			// Has the decoder encountered a subband?
		{							// This tag is obsolete and not used in modern streams
			int subband = value;

			// Check that the subband number makes sense
			assert(0 <= subband && subband <= codec->max_subband);
			if (! (0 <= subband && subband <= codec->max_subband))
			{
				error = CODEC_ERROR_DECODING_SUBBAND;
				break;
			}

			// Decompress the subband
			result = DecodeSampleSubband(decoder, input, subband);
			if (!result)
				error = CODEC_ERROR_DECODING_SUBBAND;
			else
				error = CODEC_ERROR_OKAY;
		}
		break;

	case CODEC_TAG_BAND_HEADER: //CODEC_TAG_BAND_DIVISOR:		// Band divisor. this is last TAG before subband data so act.
		codec->band.divisor = value; // This tag value pair encodes the band divisor which is obsolete
		{
			// This tag value pair marks the beginning of the encoded coefficients

			// The subband number has already been decoded
			int subband = codec->band.subband;

			result = DecodeSampleSubband(decoder, input, subband);
			if (!result)
				error = CODEC_ERROR_DECODING_SUBBAND;
			else
				error = CODEC_ERROR_OKAY;
		}
		break;

	case CODEC_TAG_ENTRY:				// Entry in sample index
		assert(0);						// Need to figure out how to return the group index
		break;

	case CODEC_TAG_MARKER:				// Bitstream marker
		{
			int marker = value;
			uint8_t  *current_position;

			// Save the current bitstream position
			current_position = GetBitstreamPosition(input);
			current_position -= 4; // Step back to before the GetSegment i.e. the TAG

			if (IsLowPassHeaderMarker(marker))
			{
				// Save the bitstream position for the start of the channel
				codec->channel_position = current_position;
			}
			else if (IsLowPassBandMarker(marker))
			{
				int subband = 0;

				result = DecodeSampleSubband(decoder, input, subband);
				if (!result)
					error = CODEC_ERROR_DECODING_SUBBAND;
				else
					error = CODEC_ERROR_OKAY;
			}
		}

		break;

	case CODEC_TAG_VERSION_MAJOR:		// Version
		assert(0);
		break;

	case CODEC_TAG_VERSION_MINOR:		// Minor version number
		assert(0);
		break;

	case CODEC_TAG_VERSION_REVISION:	// Revision number
		assert(0);
		break;

	case CODEC_TAG_VERSION_EDIT:		// Edit number
		assert(0);
		break;

	case CODEC_TAG_SEQUENCE_FLAGS:		// Video sequence flags
		assert(0);
		break;

	case CODEC_TAG_TRANSFORM_TYPE:		// Type of transform
		assert(TRANSFORM_TYPE_FIRST <= value && value <= TRANSFORM_TYPE_LAST);
		if (TRANSFORM_TYPE_FIRST <= value && value <= TRANSFORM_TYPE_LAST)
		{
			int i;

			codec->transform_type = value;

			for(i=0;i<TRANSFORM_MAX_CHANNELS;i++)
			{
				TRANSFORM *transform = decoder->transform[i];
				if(transform)
				{
					GetTransformPrescale(transform, codec->transform_type, codec->precision);
				}
			}
		}
		else
			error = CODEC_ERROR_TRANSFORM_TYPE;
		break;

	case CODEC_TAG_NUM_FRAMES:			// Number of frames in the group
		assert(0 <= value && value <= TRANSFORM_NUM_FRAMES);
		if (0 <= value && value <= TRANSFORM_NUM_FRAMES)
			codec->num_frames = value;
		else
			error = CODEC_ERROR_NUM_FRAMES;
		break;

	case CODEC_TAG_NUM_CHANNELS:		// Number of channels in the transform
		assert(value <= CODEC_MAX_CHANNELS);
		if (value <= CODEC_MAX_CHANNELS)
			codec->num_channels = value;
		else
			error = CODEC_ERROR_NUM_CHANNELS;
		break;

	case CODEC_TAG_NUM_WAVELETS:		// Number of wavelets in the transform
		assert(0 < value && value <= TRANSFORM_NUM_WAVELETS);
		if (0 < value && value <= TRANSFORM_NUM_WAVELETS)
			codec->num_wavelets = value;
		else
			error = CODEC_ERROR_NUM_WAVELETS;
		break;

	case CODEC_TAG_NUM_SUBBANDS:		// Number of encoded subbands
		assert(0 < value && value <= TRANSFORM_NUM_SUBBANDS);
		if (0 < value && value <= TRANSFORM_NUM_SUBBANDS)
			codec->num_subbands = value;
		else
			error = CODEC_ERROR_NUM_SUBBANDS;
		break;

	case CODEC_TAG_NUM_SPATIAL:			// Number of spatial levels
		assert(0 < value && value <= TRANSFORM_NUM_SPATIAL);
		if (0 < value && value <= TRANSFORM_NUM_SPATIAL)
			codec->num_spatial = value;
		else
			error = CODEC_ERROR_NUM_SPATIAL;
		break;

	case CODEC_TAG_FIRST_WAVELET:		// Type of the first wavelet
		assert(value == TRANSFORM_FIRST_WAVELET);
		if (value == TRANSFORM_FIRST_WAVELET)
			codec->first_wavelet = value;
		else
			error = CODEC_ERROR_FIRST_WAVELET;
		break;

	case CODEC_TAG_CHANNEL_SIZE:		// Number of bytes in each channel
		assert(0);
		break;

	case CODEC_TAG_GROUP_TRAILER:		// Group trailer and checksum
		codec->sample_done = true;
		break;

	case CODEC_TAG_FRAME_TYPE:			// Type of frame marks the frame start
		codec->frame.type = value;
		break;

	case CODEC_TAG_FRAME_WIDTH:			// Width of the frame
		codec->frame.width = value;
		break;

	case CODEC_TAG_FRAME_HEIGHT:		// Height of the frame
		codec->frame.height = value;

		//DAN20080729 -- Initialize the default colorspace based on clip resolution
		if ((decoder->frame.colorspace & COLORSPACE_MASK) == COLOR_SPACE_UNDEFINED)
		{
			int internalheight = value;
			int internalwidth = codec->frame.width;
			if(decoder->codec.encoded_format == ENCODED_FORMAT_BAYER)
			{
				internalwidth *= 2;
				internalheight *= 2;
			}

			if(internalheight > 576 || internalwidth > 720)
				decoder->frame.colorspace |= COLOR_SPACE_CG_709;
			else
				decoder->frame.colorspace |= COLOR_SPACE_CG_601;
		}
		//if(decoder->frame.colorspace_filedefault)
		//	decoder->frame.colorspace = decoder->frame.colorspace_filedefault;
		if(decoder->frame.colorspace_override)
			decoder->frame.colorspace = decoder->frame.colorspace_override;
		break;

	case CODEC_TAG_ENCODED_COLORSPACE: //DAN20080729
		if(decoder->codec.encoded_format == ENCODED_FORMAT_BAYER)
			value &= ~(COLOR_SPACE_BT_601|COLOR_SPACE_BT_709); // Bayer has no 601 vs 709,
						//there was a bug in 3.9.4 that had bayer flagged as 601.

		if(decoder->frame.colorspace_override)
			decoder->frame.colorspace = decoder->frame.colorspace_override;
		else
		{
			if(decoder->codec.encoded_format == ENCODED_FORMAT_YUV_422)
			{
				decoder->frame.colorspace &= ~(COLOR_SPACE_BT_601 | COLOR_SPACE_BT_709);
				decoder->frame.colorspace |= (value & (COLOR_SPACE_BT_601 | COLOR_SPACE_BT_709));
				//Let the VSRGB status be controllable by the calling application (e.g. Vegas)
			}
			else
			{
				decoder->frame.colorspace &= ~(COLOR_SPACE_VS_RGB);
				decoder->frame.colorspace |= (value & (COLOR_SPACE_VS_RGB));
			}
		}
		decoder->frame.colorspace_filedefault = value;
		break;

	case CODEC_TAG_FRAME_FORMAT:		// Format of the encoded pixels (GRAY, YUV, RGB, RGBA)
		assert(0);
		break;

	case CODEC_TAG_INPUT_FORMAT:		// Format of the original pixels
		codec->input_format = value;

		// Set the encoded format if it has not already been set
	//	error = UpdateEncodedFormat(codec, (COLOR_FORMAT)value);
		break;

	case CODEC_TAG_ENCODED_FORMAT:		// Internal format of the encoded data
	case CODEC_TAG_OLD_ENCODED_FORMAT:
		codec->encoded_format = value;	
		if(codec->encoded_format == ENCODED_FORMAT_RGBA_4444 && codec->num_channels == 3)
			codec->encoded_format = ENCODED_FORMAT_RGB_444;
		break;

	case CODEC_TAG_FRAME_INDEX:			// Position of frame within the group
		codec->frame.group_index = value;
		break;

	case CODEC_TAG_FRAME_TRAILER:		// Frame trailer and checksum
		codec->sample_done = true;
		break;

	case CODEC_TAG_LOWPASS_SUBBAND:		// Subband number of the lowpass band
		codec->lowpass.subband = value;
		error = SetDefaultEncodedFormat(codec);
		break;

	case CODEC_TAG_NUM_LEVELS:			// Number of wavelet levels
		codec->lowpass.level = value;
		break;

	case CODEC_TAG_LOWPASS_WIDTH:		// Width of the lowpass band
		codec->lowpass.width = value;
		break;

	case CODEC_TAG_LOWPASS_HEIGHT:		// Height of the lowpass band
		codec->lowpass.height = value;
		break;

	case CODEC_TAG_MARGIN_TOP:			// Margins that define the encoded subset
		codec->lowpass.margin.top = value;
		break;

	case CODEC_TAG_MARGIN_BOTTOM:
		codec->lowpass.margin.bottom = value;
		break;

	case CODEC_TAG_MARGIN_LEFT:
		codec->lowpass.margin.left = value;
		 break;

	case CODEC_TAG_MARGIN_RIGHT:
		codec->lowpass.margin.right = value;
		break;

	case CODEC_TAG_PIXEL_OFFSET:		// Quantization parameters
		codec->lowpass.pixel_offset = value;
		break;

	case CODEC_TAG_QUANTIZATION:		// Quantization divisor used during encoding
		codec->lowpass.quantization = value;
		break;

	case CODEC_TAG_PIXEL_DEPTH:			// Number of bits per pixel
		codec->lowpass.bits_per_pixel = value;
		break;

	case CODEC_TAG_LOWPASS_TRAILER:		// Lowpass trailer
		assert(0);
		break;

	case CODEC_TAG_WAVELET_TYPE:		// Type of wavelet
		codec->highpass.wavelet_type = value;
		break;

	case CODEC_TAG_WAVELET_NUMBER:		// Number of the wavelet in the transform
		codec->highpass.wavelet_number = value;
		break;

	case CODEC_TAG_WAVELET_LEVEL:		// Level of the wavelet in the transform
		codec->highpass.wavelet_level = value;
		break;

	case CODEC_TAG_NUM_BANDS:			// Number of wavelet bands
		codec->highpass.num_bands = value;
		break;

	case CODEC_TAG_HIGHPASS_WIDTH:		// Width of each highpass band
		codec->highpass.width = value;
		break;

	case CODEC_TAG_HIGHPASS_HEIGHT:		// Height of each highpass band
		codec->highpass.height = value;
		break;

	case CODEC_TAG_LOWPASS_BORDER:		// Dimensions of lowpass border (obsolete)
		codec->highpass.lowpass_border = value;
		break;

	case CODEC_TAG_HIGHPASS_BORDER:		// Dimensions of highpass border (obsolete)
		codec->highpass.highpass_border = value;
		break;

	case CODEC_TAG_LOWPASS_SCALE:		// Scale factor for lowpass band
		codec->highpass.lowpass_scale = value;
		break;

	case CODEC_TAG_LOWPASS_DIVISOR:		// Divisor for the lowpass band
		codec->highpass.lowpass_divisor = value;
		break;

	case CODEC_TAG_HIGHPASS_TRAILER:	// Highpass trailer
		assert(0);
		break;

	case CODEC_TAG_BAND_NUMBER:			// Identifying number of a wavelet band
		codec->band.number = value;
		break;

	case CODEC_TAG_BAND_WIDTH:			// Band data width
		codec->band.width = value;
		break;

	case CODEC_TAG_BAND_HEIGHT:			// Band data height
		codec->band.height = value;
		break;

	case CODEC_TAG_BAND_SUBBAND:		// Subband number of this wavelet band
		codec->band.subband = value;
		//assert(value != 255);
		break;

	case CODEC_TAG_BAND_ENCODING:		// Encoding method for this band
		codec->band.encoding = value;
		break;

	case CODEC_TAG_BAND_QUANTIZATION:	// Quantization applied to band
		codec->band.quantization = value;
		break;

	case CODEC_TAG_BAND_SCALE:			// Band scale factor
		codec->band.scale = value;
		break;

	case CODEC_TAG_BAND_TRAILER:		// Band trailer
		assert(0);
		break;

	case CODEC_TAG_NUM_ZEROVALUES:		// Number of zero values
		assert(0);
		break;

	case CODEC_TAG_NUM_ZEROTREES:		// Number of zerotrees
		assert(0);
		break;

	case CODEC_TAG_NUM_POSITIVES:		// Number of positive values
		assert(0);
		break;

	case CODEC_TAG_NUM_NEGATIVES:		// Number of negative values
		assert(0);
		break;

	case CODEC_TAG_NUM_ZERONODES:		// Number of zerotree nodes
		assert(0);
		break;

	case CODEC_TAG_CHANNEL:				// Channel number
		assert(0);
		break;

	case CODEC_TAG_INTERLACED_FLAGS:	// Interlaced structure of the video stream
		//assert(0);
		break;
		//assert(0);

	case CODEC_TAG_PROTECTION_FLAGS:	// Copy protection bits
		//assert(0);
		break;

	case CODEC_TAG_PICTURE_ASPECT_X:	// Numerator of the picture aspect ratio
		codec->picture_aspect_x = value;
		//assert(0);
		break;

	case CODEC_TAG_PICTURE_ASPECT_Y:	// Denominator of the picture aspect ratio
		codec->picture_aspect_y = value;
		//assert(0);
		break;

	case CODEC_TAG_SAMPLE_FLAGS:		// Flag bits that control sample decoding
		// Progressive versus interlaced decoding is specified by the sample flags
		error = UpdateCodecFlags(codec, value);
		break;

	case CODEC_TAG_FRAME_NUMBER:		// Sequence number of the frame in the bitstream
		codec->frame_number = value;
		break;

	// This TAG is now support as part of the universal decoder.
	// Only Prospect HD builds can decode 10bit.
	case CODEC_TAG_PRECISION:			// Number of bits in the video source
		codec->precision = value;
		{
			int i;

			for(i=0;i<TRANSFORM_MAX_CHANNELS;i++)
			{
				TRANSFORM *transform = decoder->transform[i];
				if(transform)
				{
					GetTransformPrescale(transform, codec->transform_type, codec->precision);
				}
			}
		}
		break;

	case CODEC_TAG_PRESCALE_TABLE:
		{
			int i;
			int prescale[TRANSFORM_MAX_WAVELETS] = {0};

			for(i=0;i<TRANSFORM_MAX_WAVELETS;i++)
				prescale[i] = value >> (14-i*2) & 0x3;

			for(i=0;i<TRANSFORM_MAX_CHANNELS;i++)
			{
				TRANSFORM *transform = decoder->transform[i];
				if(transform)
				{
					memcpy(transform->prescale, prescale, sizeof(prescale));
				}
			}
		}
		break;

	case CODEC_TAG_VERSION:			// Version number of the encoder used in each GOP.
		codec->version[0] = (value>>12) & 0xf;
		codec->version[1] = (value>>8) & 0xf;
		codec->version[2] = value & 0xff;
		break;

	case CODEC_TAG_QUALITY_L:		//
		codec->encode_quality &= 0xffff0000;
		codec->encode_quality |= value;
		break;

	case CODEC_TAG_QUALITY_H:		//
		codec->encode_quality &= 0xffff;
		codec->encode_quality |= value<<16;
		break;

	case CODEC_TAG_BAND_CODING_FLAGS:
		codec->active_codebook = value & 0xf; // 0-15 valid code books
		codec->difference_coding = (value>>4) & 1;
		break;

	// Peak table processing
	case CODEC_TAG_PEAK_TABLE_OFFSET_L:
		codec->peak_table.offset &= ~0xffff;
		codec->peak_table.offset |= (value & 0xffff);
		codec->peak_table.base = (PIXEL *)(input->lpCurrentWord);
		codec->peak_table.level = 0; // reset for the next subband
		break;

	case CODEC_TAG_PEAK_TABLE_OFFSET_H:
		codec->peak_table.offset &= 0xffff;
		codec->peak_table.offset |= (value & 0xffff)<<16;
		codec->peak_table.level = 0; // reset for the next subband
		break;

	case CODEC_TAG_PEAK_LEVEL:
		codec->peak_table.level = value;
		codec->peak_table.base += codec->peak_table.offset / sizeof(PIXEL);
		break;

	case CODEC_TAG_PEAK_TABLE:
		//this is the chunk header, so we have peak data
		codec->peak_table.level = 0; // reset for the next subband

		//Just skip as the data was read ahead

		chunksize = value;
		chunksize &= 0xffff;
		input->lpCurrentWord += chunksize*4;
		input->nWordsUsed -= chunksize*4;
		break;


#if (1 && DEBUG)

	case CODEC_TAG_SAMPLE_END:			// Marks the end of the sample (for debugging only)
		assert(0);
		break;

#endif

	default:		// Unknown tag
		if(tag & 0x4000)
		{
			if(tag & 0x2000) // i.e. 0x6xxx = 24bit size.
			{
				chunksize = value;
				chunksize &= 0xffff;
				chunksize += ((tag&0xff)<<16);
			}
			else // 16bit size
			{
				chunksize = value;
				chunksize &= 0xffff;
			}
		}
		else if(tag & 0x2000)	//24bit LONGs chunk size
		{
			optional = true; // Fixes a weird seneraio where the size fields in SizeTagPop() has not
						  // updated the size and turned the tag to optional. TODO : WHY
			chunksize = 0; // not not skip
		//	chunksize = value + ((tag & 0xff)<<16);
		//	do not skip an unknown but optional chunk
		//  These are only use to size subbands, but the data within should not be skipped

			// unless
			if((tag & 0xff00) == CODEC_TAG_UNCOMPRESS)
			{
				optional = true;
				chunksize = value;
				chunksize &= 0xffff;
				chunksize += ((tag&0xff)<<16);


				decoder->uncompressed_chunk = (uint32_t *)input->lpCurrentWord;
				decoder->uncompressed_size = chunksize*4;
				decoder->sample_uncompressed = 1;
			}
		}

		assert(optional);
		if(!optional)
		{
			error = CODEC_ERROR_UNKNOWN_REQUIRED_TAG;
		}
		else if(chunksize > 0) // skip this option chunk
		{
			input->lpCurrentWord += chunksize*4;
			input->nWordsUsed -= chunksize*4;
		}
		break;
	}

	return error;
}

void UpdateWaveletBandValidFlags(DECODER *decoder, IMAGE *wavelet, int band)
{
	assert(decoder != NULL);
	assert(wavelet != NULL);

	if (decoder != NULL && wavelet != NULL)
	{

#if (1 && DEBUG)
		FILE *logfile = decoder->logfile;
#endif

#if _THREADED_DECODER
		// Lock access to the wavelet data
		if(decoder->entropy_worker_new.pool.thread_count)
			Lock(&decoder->entropy_worker_new.lock);
#endif

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Changing band valid flags: 0x%04X, mask: 0x%04X\n",
					wavelet->band_valid_flags, BAND_VALID_MASK(band));
		}
#endif
		// Update the wavelet band flags
		wavelet->band_valid_flags |= BAND_VALID_MASK(band);
		wavelet->band_started_flags |= BAND_VALID_MASK(band);

#if _THREADED_DECODER

		// Unlock access to the wavelet data
		if(decoder->entropy_worker_new.pool.thread_count)
			Unlock(&decoder->entropy_worker_new.lock);

#endif


	}
}


void UpdateWaveletBandStartedFlags(DECODER *decoder, IMAGE *wavelet, int band)
{
	assert(decoder != NULL);
	assert(wavelet != NULL);

	if (decoder != NULL && wavelet != NULL)
	{
		// Update the wavelet band flags
#if _DELAYED_THREAD_START==0
		if(decoder->entropy_worker_new.pool.thread_count)
			Lock(&decoder->entropy_worker_new.lock);
#endif

		wavelet->band_started_flags |= BAND_VALID_MASK(band);

#if _DELAYED_THREAD_START==0
		if(decoder->entropy_worker_new.pool.thread_count)
			Unlock(&decoder->entropy_worker_new.lock);
#endif
	}
}

bool DecodedBandsValid(IMAGE *wavelet, int index, int transform_type)
{
	uint32_t threaded_band_mask;
	uint32_t wavelet_band_mask;
	uint32_t decoded_band_mask;

	bool decoded_bands_valid;

	// Has this wavelet been created?
	if (wavelet == NULL)
	{
		// Too soon to wait for the wavelet bands to be decoded
		return false;
	}

	// Is this a fieldplus transform?
	if (transform_type == TRANSFORM_TYPE_FIELDPLUS)
	{
		// Is this the temporal wavelet?
		if (index == 2)
		{
			assert(wavelet->wavelet_type == WAVELET_TYPE_TEMPORAL);
			assert(wavelet->num_bands == 2);

			// Earlier transforms in the queue will compute both wavelet bands
			return true;
		}

		// Is this wavelet at the end of a chain of transforms?
		if (index == 3 || index == 5)
		{
			// Must wait for all bands to be decoded
			threaded_band_mask = 0;
		}
		else
		{
			// The lowpass band will be computed by transforms earlier in the queue
			threaded_band_mask = BAND_VALID_MASK(0);
		}
	}

	// Is this a spatial transform?
	else if (transform_type == TRANSFORM_TYPE_SPATIAL)
	{
		// Is this wavelet at the top of the pyramid?
		if (index == 2)
		{
			// Must wait for all bands to be decoded
			threaded_band_mask = 0;
		}
#if 0
		// Is this wavelet at the bottom of the pyramid?
		else if (index == 0)
		{
			// Must wait for all bands to be decoded
			threaded_band_mask = 0;
		}
#endif
		else
		{
			// The lowpass band will be computed by transforms earlier in the queue
			threaded_band_mask = BAND_VALID_MASK(0);
		}
	}

	else
	{
		// Unknown type of transform
		assert(0);

		// Assume that the bands are not valid
		return false;
	}

	// Compute the mask for the bands in this wavelet
	decoded_band_mask = ((1 << wavelet->num_bands) - 1);

	// Clear the bit for the band computed by the threaded transform
	decoded_band_mask &= ~threaded_band_mask;

	// Compute the wavelet bands that have been decoded
	wavelet_band_mask = (wavelet->band_valid_flags & decoded_band_mask);

	// Have all of the bands not computed by the transform thread been decoded?
	decoded_bands_valid = (wavelet_band_mask == decoded_band_mask);

	return decoded_bands_valid;
}

void QueueThreadedTransform(DECODER *decoder, int channel, int index)
{

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	CODEC_STATE *codec = &decoder->codec;
	TRANSFORM *transform = decoder->transform[channel];
	//IMAGE *wavelet = transform->wavelet[index];
	int precision = codec->precision;

	// The transform data structure must exist
	assert(transform != NULL);

	// The transform thread variables should have been created
	{
		int free_entry;

#if _DELAYED_THREAD_START==0
		// Lock access to the transform queue
		Lock(&decoder->entropy_worker_new.lock);
#endif

		// Copy the transform parameters into the next queue entry
		free_entry = decoder->transform_queue.free_entry;
		assert(0 <= free_entry && free_entry < DECODING_QUEUE_LENGTH);
		if (0 <= free_entry && free_entry < DECODING_QUEUE_LENGTH)
		{
			assert(transform != NULL);
			assert(0 <= channel && channel < TRANSFORM_MAX_CHANNELS);
			assert(0 <= index && index < TRANSFORM_MAX_WAVELETS);

			// Note: The wavelet may not exist when the transform is queued

			decoder->transform_queue.queue[free_entry].transform = transform;
			decoder->transform_queue.queue[free_entry].channel = channel;
			decoder->transform_queue.queue[free_entry].index = index;
			decoder->transform_queue.queue[free_entry].precision = precision;
			decoder->transform_queue.queue[free_entry].done = 0;

			// Update the transform request queue
			decoder->transform_queue.free_entry++;
			decoder->transform_queue.num_entries++;

#if (1 && DEBUG)
			if (logfile) {
				fprintf(logfile, "Queued transform, channel: %d, index: %d\n", channel, index);
			}
#endif
		}
#if _DELAYED_THREAD_START==0
		Unlock(&decoder->entropy_worker_new.lock);
#endif
	}
}


#if _THREADED_DECODER
void WaitForTransformThread(DECODER *decoder)
{
	if(decoder->entropy_worker_new.pool.thread_count)
	{
#if _DELAYED_THREAD_START
		ThreadPoolSendMessage(&decoder->entropy_worker_new.pool, THREAD_MESSAGE_START);
#endif
	
		ThreadPoolWaitAllDone(&decoder->entropy_worker_new.pool);
	
		decoder->transform_queue.started = 0;
		decoder->transform_queue.num_entries = 0;
		decoder->transform_queue.next_entry = 0;
		decoder->transform_queue.free_entry = 0;
	}
}
#endif

#endif

#if _INTERLACED_WORKER_THREADS
void TransformInverseFrameThreadedToYUV(DECODER *decoder, int frame_index, int num_channels,
										uint8_t *output, int pitch, FRAME_INFO *info,
										int chroma_offset, int precision)
{
    LONG lPreviousCount,i;

	// There are half as many input rows as output rows
	int transform_height = (((info->height+7)/8)*8) / 2;
	int middle_row_count = transform_height;

	// Post a message to the mailbox
	struct interlace_data *mailbox = &decoder->interlaced_worker.interlace_data;
	mailbox->type = THREAD_TRANSFORM_FRAME_YUV;
	mailbox->frame = frame_index;
	mailbox->num_channels = num_channels;
	mailbox->output = output;
	mailbox->pitch = pitch;
	memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
	mailbox->chroma_offset = chroma_offset;
	mailbox->precision = precision;

	// Set the semaphore to the number of rows
	decoder->interlaced_worker.current_row = 0;
	ReleaseSemaphore(decoder->interlaced_worker.row_semaphore, middle_row_count, &lPreviousCount);
	assert(lPreviousCount == 0);

	// Wake up both worker threads
	for(i=0; i<THREADS_IN_LAST_WAVELET; i++)
	{
		SetEvent(decoder->interlaced_worker.start_event[i]);
	}

	// Wait for both worker threads to finish
	WaitForMultipleObjects(THREADS_IN_LAST_WAVELET, decoder->interlaced_worker.done_event, true, INFINITE);
}

void TransformInverseFrameThreadedToRow16u(DECODER *decoder, int frame_index, int num_channels,
										   PIXEL16U *output, int pitch, FRAME_INFO *info,
										   int chroma_offset, int precision)
{
    LONG lPreviousCount,i;

	// There are half as many input rows as output rows
	int transform_height = (((info->height+7)/8)*8) / 2;
	int middle_row_count = transform_height;

	// Post a message to the mailbox
	struct interlace_data *mailbox = &decoder->interlaced_worker.interlace_data;
	mailbox->type = THREAD_TRANSFORM_FRAME_ROW16U;
	mailbox->frame = frame_index;
	mailbox->num_channels = num_channels;
	mailbox->output = (uint8_t *)output;
	mailbox->pitch = pitch;
	memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
	mailbox->chroma_offset = chroma_offset;
	mailbox->precision = precision;

	// Set the semaphore to the number of rows
	decoder->interlaced_worker.current_row = 0;
	ReleaseSemaphore(decoder->interlaced_worker.row_semaphore, middle_row_count, &lPreviousCount);
	assert(lPreviousCount == 0);

	// Wake up both worker threads
	for(i=0; i<THREADS_IN_LAST_WAVELET; i++)
	{
		SetEvent(decoder->interlaced_worker.start_event[i]);
	}

	// Wait for both worker threads to finish
	WaitForMultipleObjects(THREADS_IN_LAST_WAVELET, decoder->interlaced_worker.done_event, true, INFINITE);
}


DWORD WINAPI InterlacedWorkerThreadProc(LPVOID lpParam)
{
	DECODER *decoder = (DECODER *)lpParam;
	FILE *logfile = decoder->logfile;
	struct interlace_data *data = &decoder->interlaced_worker.interlace_data;
	int thread_index;

	HANDLE hObjects[2];
	DWORD dwReturnValue;

	if(decoder->thread_cntrl.affinity)
	{
		HANDLE hCurrentThread = GetCurrentThread();
		SetThreadAffinityMask(hCurrentThread,decoder->thread_cntrl.affinity);
	}

	// Set the handler for system exceptions
#ifdef _WIN32
	SetDefaultExceptionHandler();
#endif
	
	// Determine the index of this worker thread
	if(decoder->interlaced_worker.lock_init)
	{
		EnterCriticalSection(&decoder->interlaced_worker.lock);
	}

	thread_index = decoder->interlaced_worker.thread_count++;

	if(decoder->interlaced_worker.lock_init)
		LeaveCriticalSection(&decoder->interlaced_worker.lock);

	// The transform worker variables should have been created
	assert(decoder->interlaced_worker.start_event[thread_index] != NULL);
	assert(decoder->interlaced_worker.row_semaphore != NULL);
	assert(decoder->interlaced_worker.done_event[thread_index] != NULL);
	assert(decoder->interlaced_worker.stop_event != NULL);

	if (!(decoder->interlaced_worker.start_event[thread_index] != NULL &&
		  decoder->interlaced_worker.row_semaphore != NULL &&
		  decoder->interlaced_worker.done_event[thread_index] != NULL &&
		  decoder->interlaced_worker.stop_event != NULL)) {
		return 1;
	}

	hObjects[0] = decoder->interlaced_worker.start_event[thread_index];
	hObjects[1] = decoder->interlaced_worker.stop_event;

	for (;;)
	{
		// Wait for the signal to begin processing a transform
		dwReturnValue = WaitForMultipleObjects(2, hObjects, false, INFINITE);

		// Received a signal to begin inverse transform processing?
		if (dwReturnValue == WAIT_OBJECT_0)
		{
			int type;				// Type of inverse transform to perform
			int frame_index;		// Index of output frame to produce
			int num_channels;		// Number of channels in the transform array
			uint8_t *output;			// Output frame buffer
			int pitch;				// Output frame pitch
			FRAME_INFO info;		// Format of the output frame
			int chroma_offset;		// Offset for the output chroma
			int precision;			// Source pixel bit depth

			// Lock access to the transform data
			if(decoder->interlaced_worker.lock_init) {
				EnterCriticalSection(&decoder->interlaced_worker.lock);
			}
			// Get the processing parameters
			type = data->type;
			frame_index = data->frame;
			num_channels = data->num_channels;
			output = data->output;
			pitch = data->pitch;
			memcpy(&info, &data->info, sizeof(FRAME_INFO));
			chroma_offset = data->chroma_offset;
			precision = data->precision;

			// Unlock access to the transform data
			if(decoder->interlaced_worker.lock_init)
				LeaveCriticalSection(&decoder->interlaced_worker.lock);

			// Select the type of inverse transform to perform
			switch (type)
			{
			case THREAD_TRANSFORM_FRAME_YUV:
				//TODO: more to new _THREADED model
				TransformInverseFrameSectionToYUV(decoder, thread_index, frame_index, num_channels,
												  output, pitch, &info, chroma_offset, precision);
				break;

			case THREAD_TRANSFORM_FRAME_ROW16U:
				//TODO: more to new _THREADED model
				TransformInverseFrameSectionToRow16u(decoder, thread_index, frame_index, num_channels,
													 (PIXEL16U *)output, pitch, &info, chroma_offset, precision);
				break;

			default:
				assert(0);
				break;
			}

			// Signal that this thread is done
			SetEvent(decoder->interlaced_worker.done_event[thread_index]);
		}
		else
		{
			// Should have a condition that causes the thread to terminate
			assert(dwReturnValue == WAIT_OBJECT_0+1 || dwReturnValue == WAIT_ABANDONED);
			break;
		}
	}

	return 0;
}


#endif

void GetDecodedFrameDimensions(TRANSFORM **transform_array,
							   int num_channels,
							   int frame_index,
							   int resolution,
							   int *decoded_width_out,
							   int *decoded_height_out)
{
	IMAGE *wavelet = NULL;
	int decoded_scale = 0;
	int wavelet_width;
	int wavelet_height;
	int decoded_width;
	int decoded_height;

	// Get the decoding scale
	switch(resolution)
	{
		case DECODED_RESOLUTION_FULL_DEBAYER:
		case DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER:
#if DEBUG
			assert(AllTransformBandsValid(transform_array, num_channels, frame_index));
#endif
			decoded_scale = 2;
			wavelet = transform_array[0]->wavelet[0];
			break;

		case DECODED_RESOLUTION_FULL:
#if DEBUG
			assert(AllTransformBandsValid(transform_array, num_channels, frame_index));
#endif
			decoded_scale = 2;
			wavelet = transform_array[0]->wavelet[0];
			break;
		case DECODED_RESOLUTION_HALF_NODEBAYER:
		case DECODED_RESOLUTION_HALF:
#if DEBUG
			assert(AllLowpassBandsValid(transform_array, num_channels, frame_index));
#endif
			decoded_scale = 1;
			wavelet = transform_array[0]->wavelet[0];
			break;

		case DECODED_RESOLUTION_QUARTER:
			decoded_scale = 1;
			wavelet = transform_array[0]->wavelet[3];
			break;

		case DECODED_RESOLUTION_LOWPASS_ONLY:
			decoded_scale = 1;
			wavelet = transform_array[0]->wavelet[5];

			// Is this an intra frame?
			if (wavelet == NULL) {
				wavelet = transform_array[0]->wavelet[2];
			}

			break;

		default:
			assert(0);
			break;
	}

	// Compute the decoded frame dimensions
	assert(wavelet != NULL);
	wavelet_width = wavelet->width;
	wavelet_height = wavelet->height;
	decoded_width = decoded_scale * wavelet_width;
	decoded_height = decoded_scale * wavelet_height;

	if (decoded_width_out) {
		*decoded_width_out = decoded_width;
	}

	if (decoded_height_out) {
		*decoded_height_out = decoded_height;
	}
}

// Reconstruct Bayer format to the requested output format
CODEC_ERROR UncompressedSampleFrameBayerToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output_buffer, int output_pitch)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	//CODEC_STATE *codec = &decoder->codec;
	//int num_channels = codec->num_channels;
	//int precision = codec->precision;
	int format = info->format;
	int width = info->width;
	int height = info->height;
	//int resolution = info->resolution;

	// Compute the number of bytes between each row of Bayer data
	//int bayer_pitch = 2 * width * sizeof(PIXEL16U);

	// Compute the pitch between pairs of rows of bayer data (one pair per image row)
	//int raw_bayer_pitch = 2 * bayer_pitch;

	//int chroma_offset = decoder->codec.chroma_offset;

	error = CODEC_ERROR_UNSUPPORTED_FORMAT;
	switch (format)
	{
	case DECODED_FORMAT_RGB24:
	case DECODED_FORMAT_RGB32:
	case DECODED_FORMAT_RG48: //DAN20090120 added not sure why they weren't here.
	case DECODED_FORMAT_RG64: //DAN20101207 added not sure why they weren't here.
	case DECODED_FORMAT_WP13: //DAN20090120  ""
	case DECODED_FORMAT_W13A: //DAN20101207  ""
	case DECODED_FORMAT_B64A:
	case DECODED_FORMAT_R210:
	case DECODED_FORMAT_DPX0:
	case DECODED_FORMAT_RG30:
	case DECODED_FORMAT_AR10:
	case DECODED_FORMAT_AB10:
	case DECODED_FORMAT_YR16:
	case DECODED_FORMAT_V210:
	case DECODED_FORMAT_YU64:
	case DECODED_FORMAT_YUYV: //?
	case DECODED_FORMAT_UYVY: //?
	case DECODED_FORMAT_R408:
	case DECODED_FORMAT_V408:
		error = CODEC_ERROR_OKAY;
		break;

	case DECODED_FORMAT_BYR2:
	case DECODED_FORMAT_BYR4:
		{
			//bool linearRestore = false;
			unsigned short *curve = NULL;

			if(decoder->BYR4LinearRestore && decoder->frame.format == DECODED_FORMAT_BYR4 && decoder->cfhddata.encode_curve_preset == 0)
			{
				curve = decoder->BYR4LinearRestore;
			}
			ConvertPackedToBYR2(width, height, decoder->uncompressed_chunk, decoder->uncompressed_size, output_buffer, output_pitch, curve);
		}
		decoder->uncompressed_chunk = 0;
		decoder->uncompressed_size = 0;
		return CODEC_ERROR_OKAY;
		break;
	case DECODED_FORMAT_BYR3:
		ConvertPackedToBYR3(width, height, decoder->uncompressed_chunk, decoder->uncompressed_size, output_buffer, output_pitch);
		decoder->uncompressed_chunk = 0;
		decoder->uncompressed_size = 0;
		return CODEC_ERROR_OKAY;
		break;

	}

	if(error)
		return error;


	//int row;
	//int column;

	// Need to allocate a scratch buffer for decoding the Bayer frame?
	if (decoder->RawBayer16 == NULL)
	{
		// Four Bayer data samples at each 2x2 quad in the grid
		int pixel_size = 4 * sizeof(PIXEL16U);
		int frame_size;
		const size_t alignment = 16;

#if _ALLOCATOR
		ALLOCATOR *allocator = decoder->allocator;
#endif

		frame_size = width * height * pixel_size;

#if _ALLOCATOR
		decoder->RawBayer16 = (PIXEL16U *)AllocAligned(allocator, (size_t)frame_size, alignment);
#else
		decoder->RawBayer16 = (PIXEL16U *)MEMORY_ALIGNED_ALLOC(frame_size, alignment);
#endif
		assert(decoder->RawBayer16 != NULL);
		if (! (decoder->RawBayer16 != NULL)) {
			return CODEC_ERROR_MEMORY_ALLOC;
		}
		decoder->RawBayerSize = frame_size;

		if(decoder->RGBFilterBuffer16 == NULL)
		{
			int size = frame_size*3;
			if(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444 && ALPHAOUTPUT(decoder->frame.format))
				size = frame_size*4;
#if _ALLOCATOR
			decoder->RGBFilterBuffer16 = (PIXEL16U *)AllocAligned(allocator, (size_t)size, 16);
#else
			decoder->RGBFilterBuffer16 = (PIXEL16U *)MEMORY_ALIGNED_ALLOC(size, 16);
#endif
			assert(decoder->RGBFilterBuffer16 != NULL);
			if (! (decoder->RGBFilterBuffer16 != NULL)) {
				return CODEC_ERROR_MEMORY_ALLOC;
			}
			decoder->RGBFilterBufferSize = frame_size*3;
		}
	}
		// Using the RGBFilterBuffer16 as scratch space
	ConvertPackedToRawBayer16(width, height, decoder->uncompressed_chunk, decoder->uncompressed_size, decoder->RawBayer16, decoder->RGBFilterBuffer16, info->resolution);
	decoder->uncompressed_chunk = 0;
	decoder->uncompressed_size = 0;


#if _THREADED

	//DemosaicRAW
	{
		WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;
		int inverted = false;
		uint8_t *output = output_buffer;
		int pitch = output_pitch;

	#if _DELAY_THREAD_START
		if(decoder->worker_thread.pool.thread_count == 0)
		{
			CreateLock(&decoder->worker_thread.lock);
			// Initialize the pool of transform worker threads
			ThreadPoolCreate(&decoder->worker_thread.pool,
							decoder->thread_cntrl.capabilities >> 16/*cpus*/,
							WorkerThreadProc,
							decoder);
		}
	#endif
		if (format == DECODED_FORMAT_RGB24)
		{
			format = DECODED_FORMAT_RGB24_INVERTED;
			inverted = true;
		}
		else if (format == DECODED_FORMAT_RGB32)
		{
			format = DECODED_FORMAT_RGB32_INVERTED;
			inverted = true;
		}

		// Have the output location and pitch been inverted?
		if (inverted && pitch > 0) {
			int height = info->height;
			if(info->resolution == DECODED_RESOLUTION_FULL_DEBAYER || info->resolution == DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER)
				height *= 2;
			output += (height - 1) * pitch;		// Start at the bottom row
			pitch = NEG(pitch);					// Negate the pitch to go up
		}

		// Post a message to the mailbox
		mailbox->output = output;
		mailbox->pitch = pitch;
		memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
		mailbox->jobType = JOB_TYPE_OUTPUT;

		// Set the work count to the number of rows to process
		ThreadPoolSetWorkCount(&decoder->worker_thread.pool, info->height);

		// Start the transform worker threads
		ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

		// Wait for all of the worker threads to finish
		ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
	}

#else
	error = CODEC_ERROR_UNSUPPORTED_FORMAT;
#endif

	return error;
}


// Reconstruct uncompressed v210 YUV format to the requested output format
CODEC_ERROR UncompressedSampleFrameYUVToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output_buffer, int output_pitch)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	//CODEC_STATE *codec = &decoder->codec;
	//int num_channels = codec->num_channels;
	//int precision = codec->precision;
	int format = info->format;
	int width = info->width;
	int height = info->height;
	int resolution = info->resolution;

	// Compute the number of bytes between each row of Bayer data
	//int bayer_pitch = 2 * width * sizeof(PIXEL16U);

	// Compute the pitch between pairs of rows of bayer data (one pair per image row)
	//int raw_bayer_pitch = 2 * bayer_pitch;

	//int chroma_offset = decoder->codec.chroma_offset;

	error = CODEC_ERROR_UNSUPPORTED_FORMAT;

	if(format == DECODED_FORMAT_V210 && resolution == DECODED_RESOLUTION_FULL && decoder->use_active_metadata_decoder == false)
	{
		int smallest_Stride = output_pitch;
		int unc_Stride = decoder->uncompressed_size / height;

		if(unc_Stride < smallest_Stride)
			smallest_Stride = unc_Stride;


		if(unc_Stride == output_pitch)
			memcpy(output_buffer, decoder->uncompressed_chunk, decoder->uncompressed_size);
		else
		{
			int y;
			uint8_t *src = (uint8_t *)decoder->uncompressed_chunk;
			uint8_t *dst = (uint8_t *)output_buffer;

			for(y=0; y<height; y++)
			{
				memcpy(dst, src, smallest_Stride);
				src += unc_Stride;
				dst += output_pitch;
			}
		}
		decoder->uncompressed_chunk = 0;
		decoder->uncompressed_size = 0;
		return CODEC_ERROR_OKAY;
	}


	if((format == DECODED_FORMAT_YUYV || format == DECODED_FORMAT_UYVY) && resolution == DECODED_RESOLUTION_FULL && decoder->use_active_metadata_decoder == false)
	{
		int smallest_Stride = output_pitch;
		int unc_Stride = decoder->uncompressed_size / height;

		if(unc_Stride < smallest_Stride)
			smallest_Stride = unc_Stride;

		{
			int y;
			uint8_t *src = (uint8_t *)decoder->uncompressed_chunk;
			uint8_t *dst = (uint8_t *)output_buffer;

			for(y=0; y<height; y++)
			{
				uint32_t *input_ptr = (uint32_t *)src;
				int pos = 0;
				int column=0,length = width;
				length -= length % 6; //DAN03252004 -- fix a memory overflow.

				for (column=0; column < length; column += 6)
				{
					uint32_t yuv;

					int y;
					int u;
					int v;

					// Read the first word
					yuv = *(input_ptr++);

					u = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;
					y = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
					v = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

					// Expand the pixels to sixteen bits
					u <<= 6;
					y <<= 6;
					v <<= 6;

					dst[pos++] = SATURATE_16U(y)>>8;
					dst[pos++] = SATURATE_16U(u)>>8;

					// Read the second word
					yuv = *(input_ptr++);

					y = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;

					y <<= 6;

					dst[pos++] = SATURATE_16U(y)>>8;
					dst[pos++] = SATURATE_16U(v)>>8;

					u = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
					y = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

					u <<= 6;
					y <<= 6;

					dst[pos++] = SATURATE_16U(y)>>8;
					dst[pos++] = SATURATE_16U(u)>>8;

					// Read the third word
					yuv = *(input_ptr++);

					v = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;
					y = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;

					v <<= 6;
					y <<= 6;

					dst[pos++] = SATURATE_16U(y)>>8;
					dst[pos++] = SATURATE_16U(v)>>8;

					u = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;
					u <<= 6;

					// Read the fourth word
					yuv = *(input_ptr++);

					y = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;
					y <<= 6;

					dst[pos++] = SATURATE_16U(y)>>8;
					dst[pos++] = SATURATE_16U(u)>>8;

					v = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
					y = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

					v <<= 6;
					y <<= 6;

					dst[pos++] = SATURATE_16U(y)>>8;
					dst[pos++] = SATURATE_16U(v)>>8;
				}

				if(format == DECODED_FORMAT_UYVY)
				{
					for (column=0; column < pos; column += 2)
					{
						int t = dst[column];

						dst[column] = dst[column+1];
						dst[column+1] = t;
					}
				}

				
				src += unc_Stride;
				dst += output_pitch;
			}
		}
		decoder->uncompressed_chunk = 0;
		decoder->uncompressed_size = 0;
		return CODEC_ERROR_OKAY;
	}
		
	{
		// Expand YUV at the target resolution, and use the ActiveMetadata engine.
		
		// Need to allocate a scratch buffer for decoding the frame?
		if (decoder->RawBayer16 == NULL || decoder->RawBayerSize < width * 64) //RawBayer used as a scratch buffer
		{
			//int pixel_size = 2 * sizeof(PIXEL16U);
			const size_t alignment = 16;
			#if _ALLOCATOR
			ALLOCATOR *allocator = decoder->allocator;
			#endif
			
			int orig_width = width;
			if(resolution == DECODED_RESOLUTION_HALF)
				orig_width *= 2;
			if(resolution == DECODED_RESOLUTION_QUARTER)
				orig_width *= 4;

			if(decoder->RawBayer16)
			{
				#if _ALLOCATOR
					FreeAligned(allocator, decoder->RawBayer16);
					decoder->RawBayer16 = NULL;
					decoder->RawBayerSize = 0;
				#else
					MEMORY_ALIGNED_FREE(decoder->RawBayer16);
					decoder->RawBayer16 = NULL;
					decoder->RawBayerSize = 0;
				#endif
			}


			#if _ALLOCATOR
			decoder->RawBayer16 = (PIXEL16U *)AllocAligned(allocator, orig_width * 64, alignment);
			#else
			decoder->RawBayer16 = (PIXEL16U *)MEMORY_ALIGNED_ALLOC(orig_width * 64, alignment);
			#endif
			assert(decoder->RawBayer16 != NULL);
			if (! (decoder->RawBayer16 != NULL)) {
				return CODEC_ERROR_MEMORY_ALLOC;
			}
			decoder->RawBayerSize = orig_width * 64;
		}
	}

	// unpack source original YUV into YU64?

	if(decoder->RawBayer16)
	{
		//uint8_t *src = (uint8_t *)decoder->uncompressed_chunk;
		//uint8_t *dst = (uint8_t *)output_buffer;

#if _THREADED
		{
			WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

		#if _DELAY_THREAD_START
			if(decoder->worker_thread.pool.thread_count == 0)
			{
				CreateLock(&decoder->worker_thread.lock);
				// Initialize the pool of transform worker threads
				ThreadPoolCreate(&decoder->worker_thread.pool,
								decoder->thread_cntrl.capabilities >> 16/*cpus*/,
								WorkerThreadProc,
								decoder);
			}
		#endif

			// Post a message to the mailbox
			mailbox->output = output_buffer;
			mailbox->pitch = output_pitch;
			memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
			mailbox->jobType = JOB_TYPE_OUTPUT_UNCOMPRESSED;

			// Set the work count to the number of rows to process
			ThreadPoolSetWorkCount(&decoder->worker_thread.pool, height);

			// Start the transform worker threads
			ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

			// Wait for all of the worker threads to finish
			ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
		}
#else

		{
			int orig_width = width;
			int orig_height = height;
			int row,lines = 1;
			int start,end;
			if(resolution == DECODED_RESOLUTION_HALF)
			{
				orig_width *= 2;
				orig_height *= 2;
				lines = 2;
			}
			if(resolution == DECODED_RESOLUTION_QUARTER)
			{
				orig_width *= 4;
				orig_height *= 4;
				lines = 4;
			}

			start = 0;
			end = height;
			if(format == DECODED_FORMAT_RGB32 || format == DECODED_FORMAT_RGB24)
			{
				start = height-1;
				end = -1;
			}
			for (row = start; row != end; end > start ? row++ : row--)
			{		
				int whitebitdepth = 16;
				int flags = 0;
				uint8_t *planar_output[3];
				int planar_pitch[3];
				ROI roi;
				PIXEL16U *y_row_ptr;
				PIXEL16U *u_row_ptr;
				PIXEL16U *v_row_ptr;
				PIXEL16U *scanline = (PIXEL16U *)decoder->RawBayer16;
				PIXEL16U *scanline2 = scanline + orig_width * 8;
				unsigned short *sptr;
				int i,unc_Stride = decoder->uncompressed_size / orig_height;

				y_row_ptr = (PIXEL16U *)scanline;
				u_row_ptr = y_row_ptr + orig_width;
				v_row_ptr = u_row_ptr + orig_width/2;
				for(i=0; i<lines; i++)
				{
					src = (uint8_t *)decoder->uncompressed_chunk;
					src += row * unc_Stride;

					// Repack the row of 10-bit pixels into 16-bit pixels
					ConvertV210RowToYUV16((uint8_t *)src, y_row_ptr, u_row_ptr, v_row_ptr, orig_width, scanline2);

					// Advance to the next rows in the input and output images
					y_row_ptr += orig_width*2;
					u_row_ptr = y_row_ptr + orig_width;
					v_row_ptr = u_row_ptr + orig_width/2;

				}


				y_row_ptr = (PIXEL16U *)scanline;
				u_row_ptr = y_row_ptr + width;
				v_row_ptr = u_row_ptr + width/2;
				if(lines == 2)
				{
					for(i=0; i<width*2;i++)
						y_row_ptr[i] = (y_row_ptr[i*2] + y_row_ptr[i*2+1] + y_row_ptr[orig_width*2+i*2] + y_row_ptr[orig_width*2+i*2+1]) >> 2;	
					
				}
				else if(lines == 4)
				{
					for(i=0; i<width*2;i++)
						y_row_ptr[i] = (y_row_ptr[i*4] + y_row_ptr[i*4+2] + y_row_ptr[orig_width*2*2+i*4] + y_row_ptr[orig_width*2*2+i*4+2]) >> 2;
				}

				
				roi.width = width;
				roi.height = 1;			

				planar_output[0] = (uint8_t *)y_row_ptr;
				planar_output[1] = (uint8_t *)v_row_ptr;
				planar_output[2] = (uint8_t *)u_row_ptr;
				planar_pitch[0] = 0;
				planar_pitch[1] = 0;
				planar_pitch[2] = 0;
				
				if(decoder->apply_color_active_metadata)
				{
					ConvertYUVRow16uToBGRA64(planar_output, planar_pitch, roi,
						(unsigned char *)scanline2, width, output_pitch,
						COLOR_FORMAT_RGB_8PIXEL_PLANAR, decoder->frame.colorspace, &whitebitdepth, &flags);
					sptr = scanline2;

					sptr = ApplyActiveMetaData(decoder, width, 1, row, scanline2, scanline,
						info->format, &whitebitdepth, &flags);
				}
				else
				{
					ConvertYUVRow16uToBGRA64(planar_output, planar_pitch, roi,
						(unsigned char *)scanline2, width, output_pitch,
						COLOR_FORMAT_WP13, decoder->frame.colorspace, &whitebitdepth, &flags);
					sptr = scanline2;					
				}

				ConvertLinesToOutput(decoder, width, 1, row, sptr,
					dst, output_pitch, format, whitebitdepth, flags);

				dst += output_pitch;
			}
		}
#endif
	}

	error = CODEC_ERROR_OKAY;

	return error;
}





// Reconstruct uncompressed DPX0 RGB format to the requested output format
CODEC_ERROR UncompressedSampleFrameRGBToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output_buffer, int output_pitch)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	//CODEC_STATE *codec = &decoder->codec;
	//int num_channels = codec->num_channels;
	//int precision = codec->precision;
	int format = info->format;
	//int output_format = info->output_format; // used by image_dev_only decodes
	int width = info->width;
	int height = info->height;
	int resolution = info->resolution;
	//int chroma_offset = decoder->codec.chroma_offset;

	error = CODEC_ERROR_UNSUPPORTED_FORMAT;

	if(	(format == DECODED_FORMAT_DPX0 || format == DECODED_FORMAT_AR10 || format == DECODED_FORMAT_AB10 || format == DECODED_FORMAT_RG30 || format == DECODED_FORMAT_R210) &&
		 resolution == DECODED_RESOLUTION_FULL && decoder->use_active_metadata_decoder == false)
	{
		int smallest_Stride = output_pitch;
		int unc_Stride = decoder->uncompressed_size / height;

		if(unc_Stride < smallest_Stride)
			smallest_Stride = unc_Stride;

		if(format != DECODED_FORMAT_DPX0)
		{
			int unc_Stride = decoder->uncompressed_size / height;
			ConvertDPX0ToRGB10((uint8_t *)decoder->uncompressed_chunk, unc_Stride, width, height, format);
		}

		if(unc_Stride == output_pitch)
			memcpy(output_buffer, decoder->uncompressed_chunk, decoder->uncompressed_size);
		else
		{
			int y;
			uint8_t *src = (uint8_t *)decoder->uncompressed_chunk;
			uint8_t *dst = (uint8_t *)output_buffer;

			for(y=0; y<height; y++)
			{
				memcpy(dst, src, smallest_Stride);
				src += unc_Stride;
				dst += output_pitch;
			}
		}
		decoder->uncompressed_chunk = 0;
		decoder->uncompressed_size = 0;
		return CODEC_ERROR_OKAY;
	}

		
	{
		// Expand YUV at the target resolution, and use the ActiveMetadata engine.
		
		// Need to allocate a scratch buffer for decoding the frame?
		if (decoder->RawBayer16 == NULL || decoder->RawBayerSize < width * 64) //RawBayer used as a scratch buffer
		{
			//int pixel_size = 2 * sizeof(PIXEL16U);
			const size_t alignment = 16;
			#if _ALLOCATOR
			ALLOCATOR *allocator = decoder->allocator;
			#endif
			
			int orig_width = width;
			if(resolution == DECODED_RESOLUTION_HALF)
				orig_width *= 2;
			if(resolution == DECODED_RESOLUTION_QUARTER)
				orig_width *= 4;

			if(decoder->RawBayer16)
			{
				#if _ALLOCATOR
					FreeAligned(allocator, decoder->RawBayer16);
					decoder->RawBayer16 = NULL;
					decoder->RawBayerSize = 0;
				#else
					MEMORY_ALIGNED_FREE(decoder->RawBayer16);
					decoder->RawBayer16 = NULL;
					decoder->RawBayerSize = 0;
				#endif
			}


			#if _ALLOCATOR
			decoder->RawBayer16 = (PIXEL16U *)AllocAligned(allocator, orig_width * 64, alignment);
			#else
			decoder->RawBayer16 = (PIXEL16U *)MEMORY_ALIGNED_ALLOC(orig_width * 64, alignment);
			#endif
			assert(decoder->RawBayer16 != NULL);
			if (! (decoder->RawBayer16 != NULL)) {
				return CODEC_ERROR_MEMORY_ALLOC;
			}
			decoder->RawBayerSize = orig_width * 64;
		}
	}

	// unpack source original YUV into YU64?

	if(decoder->RawBayer16)
	{
		//uint8_t *src = (uint8_t *)decoder->uncompressed_chunk;
		//uint8_t *dst = (uint8_t *)output_buffer;

#if _THREADED
		{
			WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

		#if _DELAY_THREAD_START
			if(decoder->worker_thread.pool.thread_count == 0)
			{
				CreateLock(&decoder->worker_thread.lock);
				// Initialize the pool of transform worker threads
				ThreadPoolCreate(&decoder->worker_thread.pool,
								decoder->thread_cntrl.capabilities >> 16/*cpus*/,
								WorkerThreadProc,
								decoder);
			}
		#endif

			// Post a message to the mailbox
			mailbox->output = output_buffer;
			mailbox->pitch = output_pitch;
			memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
			mailbox->jobType = JOB_TYPE_OUTPUT_UNCOMPRESSED;

			// Set the work count to the number of rows to process
			ThreadPoolSetWorkCount(&decoder->worker_thread.pool, height);

			// Start the transform worker threads
			ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

			// Wait for all of the worker threads to finish
			ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
		}
#else

		{
			int orig_width = width;
			int orig_height = height;
			int row,lines = 1;
			int start,end;
			if(resolution == DECODED_RESOLUTION_HALF)
			{
				orig_width *= 2;
				orig_height *= 2;
				lines = 2;
			}
			if(resolution == DECODED_RESOLUTION_QUARTER)
			{
				orig_width *= 4;
				orig_height *= 4;
				lines = 4;
			}

			start = 0;
			end = height;
			if(format == DECODED_FORMAT_RGB32 || format == DECODED_FORMAT_RGB24) // Can this work, all the code below expects 10-bit
			{
				start = height-1;
				end = -1;
			}
			for (row = start; row != end; end > start ? row++ : row--)
			{		
				int whitebitdepth = 16;
				int flags = 0;
				uint8_t *planar_output[3];
				int planar_pitch[3];
				ROI roi;
				PIXEL16U *y_row_ptr;
				PIXEL16U *u_row_ptr;
				PIXEL16U *v_row_ptr;
				PIXEL16U *scanline = (PIXEL16U *)decoder->RawBayer16;
				PIXEL16U *scanline2 = scanline + orig_width * 8;
				unsigned short *sptr;
				int i,unc_Stride = decoder->uncompressed_size / orig_height;

				whitebitdepth = 13;
				if(decoder->apply_color_active_metadata)
					flags = ACTIVEMETADATA_SRC_8PIXEL_PLANAR;
				else
					flags = 0;
			
				roi.width = width;
				roi.height = 1;			

				if(lines == 1)
				{
					uint16_t *sptr;
					uint32_t j,*lptr = (uint32_t *)decoder->uncompressed_chunk;
					PIXEL16U *ptr = (PIXEL16U *)scanline;
		
					lptr += row * (unc_Stride>>2);
					sptr = (uint16_t *)lptr;
					for(i=0; i<width;i+=8)
					{
						int val,r,g,b;
						if(flags == ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
						{
							if(decoder->image_dev_only) // HACK, currently assuming RG48 input data.
							{
								for(j=0; j<8; j++)
								{
									ptr[j] = sptr[0] >> 3;
									ptr[j+8] = sptr[1] >> 3;
									ptr[j+16] = sptr[2] >> 3;

									sptr += 3;
								}
							}
							else
							{
								for(j=0; j<8; j++)
								{
									val = SwapInt32(*lptr++);
									val >>= 2;
									b = (val & 0x3ff) << 3;
									val >>= 10;
									g = (val & 0x3ff) << 3;
									val >>= 10;
									r = (val & 0x3ff) << 3;

									ptr[j] = r;
									ptr[j+8] = g;
									ptr[j+16] = b;
								}
							}
						}
						else
						{
							if(decoder->image_dev_only) // HACK, currently assuming RG48 input data.
							{
								for(j=0; j<8*3; j+=3)
								{
									ptr[j] = sptr[0] >> 3;
									ptr[j+1] = sptr[1] >> 3;
									ptr[j+2] = sptr[2] >> 3;

									sptr += 3;
								}
							}
							else
							{
								for(j=0; j<8*3; j+=3)
								{
									val = SwapInt32(*lptr++);
									val >>= 2;
									b = (val & 0x3ff) << 3;
									val >>= 10;
									g = (val & 0x3ff) << 3;
									val >>= 10;
									r = (val & 0x3ff) << 3;

									ptr[j] = r;
									ptr[j+1] = g;
									ptr[j+2] = b;
								}
							}
						}

						ptr += 24;
					}
				}
				else if(lines == 2)
				{
					uint32_t j,*lptr = (uint32_t)decoder->uncompressed_chunk;
					PIXEL16U *ptr = (PIXEL16U *)scanline;
		
					lptr += row * (unc_Stride>>2) * lines;
					for(i=0; i<width;i+=8)
					{
						int val,r,g,b,r2,g2,b2,r3,g3,b3,r4,g4,b4;
						for(j=0; j<8; j++)
						{
							val = SwapInt32(lptr[0]);
							val >>= 2;
							b = (val & 0x3ff) << 3;
							val >>= 10;
							g = (val & 0x3ff) << 3;
							val >>= 10;
							r = (val & 0x3ff) << 3;

							val = SwapInt32(lptr[1]);
							val >>= 2;
							b += (val & 0x3ff) << 3;
							val >>= 10;
							g += (val & 0x3ff) << 3;
							val >>= 10;
							r += (val & 0x3ff) << 3;

							val = SwapInt32(lptr[unc_Stride>>2]);
							val >>= 2;
							b += (val & 0x3ff) << 3;
							val >>= 10;
							g += (val & 0x3ff) << 3;
							val >>= 10;
							r += (val & 0x3ff) << 3;

							val = SwapInt32(lptr[(unc_Stride>>2)+1]);
							val >>= 2;
							b += (val & 0x3ff) << 3;
							val >>= 10;
							g += (val & 0x3ff) << 3;
							val >>= 10;
							r += (val & 0x3ff) << 3;

							if(flags == ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
							{
								ptr[j] = r>>2;
								ptr[j+8] = g>>2;
								ptr[j+16] = b>>2;
							}
							else
							{
								ptr[j*3] = r>>2;
								ptr[j*3+1] = g>>2;
								ptr[j*3+2] = b>>2;
							}


							lptr += lines;
						}
						ptr += 24;
					}
				}
				else if(lines == 4)
				{
					uint32_t j,*lptr = (uint32_t)decoder->uncompressed_chunk;
					PIXEL16U *ptr = (PIXEL16U *)scanline;
		
					lptr += row * (unc_Stride>>2) * lines;
					for(i=0; i<width;i+=8)
					{
						int val,r,g,b,r2,g2,b2,r3,g3,b3,r4,g4,b4;
						for(j=0; j<8; j++)
						{
							val = SwapInt32(lptr[0]);
							val >>= 2;
							b = (val & 0x3ff) << 3;
							val >>= 10;
							g = (val & 0x3ff) << 3;
							val >>= 10;
							r = (val & 0x3ff) << 3;

							val = SwapInt32(lptr[2]);
							val >>= 2;
							b += (val & 0x3ff) << 3;
							val >>= 10;
							g += (val & 0x3ff) << 3;
							val >>= 10;
							r += (val & 0x3ff) << 3;

							val = SwapInt32(lptr[unc_Stride>>1]);
							val >>= 2;
							b += (val & 0x3ff) << 3;
							val >>= 10;
							g += (val & 0x3ff) << 3;
							val >>= 10;
							r += (val & 0x3ff) << 3;

							val = SwapInt32(lptr[(unc_Stride>>1)+2]);
							val >>= 2;
							b += (val & 0x3ff) << 3;
							val >>= 10;
							g += (val & 0x3ff) << 3;
							val >>= 10;
							r += (val & 0x3ff) << 3;

							if(flags == ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
							{
								ptr[j] = r>>2;
								ptr[j+8] = g>>2;
								ptr[j+16] = b>>2;
							}
							else
							{
								ptr[j*3] = r>>2;
								ptr[j*3+1] = g>>2;
								ptr[j*3+2] = b>>2;
							}

							lptr += lines;
						}
						ptr += 24;
					}
				}

				sptr = scanline;
				if(decoder->apply_color_active_metadata)
					sptr = ApplyActiveMetaData(decoder, width, 1, row, scanline, scanline2,
						info->format, &whitebitdepth, &flags);

				ConvertLinesToOutput(decoder, width, 1, row, sptr,
					dst, output_pitch, format, whitebitdepth, flags);

				dst += output_pitch;
			}
		}
#endif
	}

	error = CODEC_ERROR_OKAY;

	return error;
}


// Reconstruct Bayer format to the requested output format
CODEC_ERROR ReconstructSampleFrameBayerToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output, int pitch)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;


#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif

	//CODEC_STATE *codec = &decoder->codec;
	//int num_channels = codec->num_channels;
	//int progressive = codec->progressive;
	//int precision = codec->precision;

	//TRANSFORM **transform_array = decoder->transform;
	int resolution = info->resolution;
	//int format = info->format;

	// Switch to the subroutine for the requested resolution
	switch (resolution)
	{
	case DECODED_RESOLUTION_FULL_DEBAYER:
	case DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER:
		//error = CODEC_ERROR_UNSUPPORTED_FORMAT;
		return ReconstructSampleFrameDeBayerFullToBuffer(decoder, info, frame, output, pitch);
		break;

	case DECODED_RESOLUTION_FULL:
		//return ReconstructSampleFrameBayerFullToBuffer(decoder, info, frame, output, pitch);
		error = CODEC_ERROR_UNSUPPORTED_FORMAT;
		break;

	//case DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER:
	case DECODED_RESOLUTION_HALF_NODEBAYER:
	case DECODED_RESOLUTION_HALF:
		//return ReconstructSampleFrameBayerHalfToBuffer(decoder, info, frame, output, pitch);
		error = CODEC_ERROR_UNSUPPORTED_FORMAT;
		break;

	case DECODED_RESOLUTION_QUARTER:
		//return ReconstructSampleFrameBayerQuarterToBuffer(decoder, frame, output, pitch);
		error = CODEC_ERROR_UNSUPPORTED_FORMAT;
		break;

	case DECODED_RESOLUTION_LOWPASS_ONLY:
		error = CODEC_ERROR_UNSUPPORTED_FORMAT;
		break;

	default:
		// The decoded resolution is not supported by this routine
		assert(0);
		error = CODEC_ERROR_UNSUPPORTED_FORMAT;
		break;
	}

	return error;
}

// Reconstruct Bayer encoded data to full resolution
CODEC_ERROR ReconstructSampleFrameBayerFullToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output_buffer, int output_pitch)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	CODEC_STATE *codec = &decoder->codec;
	int num_channels = codec->num_channels;
	//int progressive = codec->progressive;
	//int precision = codec->precision;

	//TRANSFORM **transform_array = decoder->transform;
	//int decoded_width = 0;
	//int decoded_height = 0;
	//int resolution = info->resolution;
	int format = info->format;
	//int width = info->width;
	//int height = info->height;

	// Compute the number of bytes between each row of Bayer data
	//int bayer_pitch = 2 * width * sizeof(PIXEL16U);

	// Compute the pitch between pairs of rows of bayer data (one pair per image row)
	//int raw_bayer_pitch = 2 * bayer_pitch;

	//int chroma_offset = decoder->codec.chroma_offset;

	//int row;
	//int column;

	// Need to allocate a scratch buffer for decoding the Bayer frame?
	if (decoder->RawBayer16 == NULL)
	{
		TRANSFORM **transform_array = decoder->transform;
		int decoded_width = 0;
		int decoded_height = 0;
		int resolution = info->resolution;
		//int format = info->format;
		// Four Bayer data samples at each 2x2 quad in the grid
		int pixel_size = 4 * sizeof(PIXEL16U);
		int frame_size;
		const size_t alignment = 16;

#if _ALLOCATOR
		ALLOCATOR *allocator = decoder->allocator;
#endif
		// Compute the decoded width and height for the specified resolution
		GetDecodedFrameDimensions(transform_array, num_channels, frame, resolution, &decoded_width, &decoded_height);
		assert(decoded_width > 0 && decoded_height > 0);
		if (! (decoded_width > 0 && decoded_height > 0)) {
			return CODEC_ERROR_UNSUPPORTED_FORMAT;
		}

		frame_size = decoded_width * decoded_height * pixel_size;

#if _ALLOCATOR
		decoder->RawBayer16 = (PIXEL16U *)AllocAligned(allocator, (size_t)frame_size, alignment);
#else
		decoder->RawBayer16 = (PIXEL16U *)MEMORY_ALIGNED_ALLOC(frame_size, alignment);
#endif
		assert(decoder->RawBayer16 != NULL);
		if (! (decoder->RawBayer16 != NULL)) {
			return CODEC_ERROR_MEMORY_ALLOC;
		}
		decoder->RawBayerSize = frame_size;

//#ifdef SHARPENING
		if(decoder->RGBFilterBuffer16 == NULL)
		{
			int size = frame_size*3;
			if(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444 && ALPHAOUTPUT(decoder->frame.format))
				size = frame_size*4;
#if _ALLOCATOR
			decoder->RGBFilterBuffer16 = (PIXEL16U *)AllocAligned(allocator, (size_t)size, 16);
#else
			decoder->RGBFilterBuffer16 = (PIXEL16U *)MEMORY_ALIGNED_ALLOC(size, 16);
#endif
			assert(decoder->RGBFilterBuffer16 != NULL);
			if (! (decoder->RGBFilterBuffer16 != NULL)) {
				return CODEC_ERROR_MEMORY_ALLOC;
			}
			decoder->RGBFilterBufferSize = frame_size*3;
		}
//#endif
	}

	//TODO: Need to add more output formats to this routine
	switch (format)
	{
	case DECODED_FORMAT_RGB32:
		error = CODEC_ERROR_UNSUPPORTED_FORMAT;

		// Decode the last transform to rows of Bayer data (one row per channel)
	//	TransformInverseSpatialToRow16u(transform_array, frame, num_channels,
	//								decoder->RawBayer16, raw_bayer_pitch, info,
	//								&decoder->scratch, chroma_offset, precision);

	//	ConvertPackedBayerToRGB32(decoder->RawBayer16, info, bayer_pitch,
	//							  output_buffer, output_pitch,
	//							  width, height);

		break;

	case DECODED_FORMAT_RGB24:
		error = CODEC_ERROR_UNSUPPORTED_FORMAT;
		// Decode the last transform to rows of Bayer data (one row per channel)
		//TransformInverseSpatialToRow16u(transform_array, frame, num_channels,
		//							decoder->RawBayer16, raw_bayer_pitch, info,
		//							&decoder->scratch, chroma_offset, precision);

		//ConvertPackedBayerToRGB24(decoder->RawBayer16, info, bayer_pitch,
		//						  output_buffer, output_pitch,
		//						  width, height);
		break;

	default:
		error = CODEC_ERROR_UNSUPPORTED_FORMAT;
		break;
	}

	return error;
}

// Reconstruct Bayer encoded data and demosaic to full resolution
CODEC_ERROR ReconstructSampleFrameDeBayerFullToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output_buffer, int output_pitch)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	CODEC_STATE *codec = &decoder->codec;
	int num_channels = codec->num_channels;
	//int progressive = codec->progressive;
	int precision = codec->precision;

	//TRANSFORM **transform_array = decoder->transform;
	//int decoded_width = 0;
	//int decoded_height = 0;
	//int resolution = info->resolution;
	int format = info->format;
	int width = info->width;
	//int height = info->height;

	// Compute the number of bytes between each row of Bayer data
	int bayer_pitch = 2 * width * sizeof(PIXEL16U);

	// Compute the pitch between pairs of rows of bayer data (one pair per image row)
	//int raw_bayer_pitch = 2 * bayer_pitch;

	int chroma_offset = decoder->codec.chroma_offset;


	error = CODEC_ERROR_UNSUPPORTED_FORMAT;
	switch (format)
	{
	case DECODED_FORMAT_RGB24:
	case DECODED_FORMAT_RGB32:
	case DECODED_FORMAT_RG48: //DAN20090120 added not sure why they weren't here.
	case DECODED_FORMAT_WP13: //DAN20090120  ""
	case DECODED_FORMAT_B64A:
	case DECODED_FORMAT_R210:
	case DECODED_FORMAT_DPX0:
	case DECODED_FORMAT_RG30:
	case DECODED_FORMAT_AR10:
	case DECODED_FORMAT_AB10:
	case DECODED_FORMAT_YR16:
	case DECODED_FORMAT_V210:
	case DECODED_FORMAT_YU64:
		error = CODEC_ERROR_OKAY;
		break;
	}

	if(error)
		return error;


	//int row;
	//int column;

	// Need to allocate a scratch buffer for decoding the Bayer frame?
	if (decoder->RawBayer16 == NULL)
	{
		TRANSFORM **transform_array = decoder->transform;
		int decoded_width = 0;
		int decoded_height = 0;
		int resolution = info->resolution;
		//int format = info->format;
		// Four Bayer data samples at each 2x2 quad in the grid
		int pixel_size = 4 * sizeof(PIXEL16U);
		int frame_size;
		const size_t alignment = 16;

#if _ALLOCATOR
		ALLOCATOR *allocator = decoder->allocator;
#endif

		// Compute the decoded width and height for the specified resolution
		GetDecodedFrameDimensions(transform_array, num_channels, frame, resolution, &decoded_width, &decoded_height);
		assert(decoded_width > 0 && decoded_height > 0);
		if (! (decoded_width > 0 && decoded_height > 0)) {
			return CODEC_ERROR_UNSUPPORTED_FORMAT;
		}

		frame_size = decoded_width * decoded_height * pixel_size;

#if _ALLOCATOR
		decoder->RawBayer16 = (PIXEL16U *)AllocAligned(allocator, (size_t)frame_size, alignment);
#else
		decoder->RawBayer16 = (PIXEL16U *)MEMORY_ALIGNED_ALLOC(frame_size, alignment);
#endif
		assert(decoder->RawBayer16 != NULL);
		if (! (decoder->RawBayer16 != NULL)) {
			return CODEC_ERROR_MEMORY_ALLOC;
		}
		decoder->RawBayerSize = frame_size;

//#ifdef SHARPENING
		if(decoder->RGBFilterBuffer16 == NULL)
		{
			int size = frame_size*3;
			if(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444 && ALPHAOUTPUT(decoder->frame.format))
				size = frame_size*4;
#if _ALLOCATOR
			decoder->RGBFilterBuffer16 = (PIXEL16U *)AllocAligned(allocator, (size_t)size, 16);
#else
			decoder->RGBFilterBuffer16 = (PIXEL16U *)MEMORY_ALIGNED_ALLOC(size, 16);
#endif
			assert(decoder->RGBFilterBuffer16 != NULL);
			if (! (decoder->RGBFilterBuffer16 != NULL)) {
				return CODEC_ERROR_MEMORY_ALLOC;
			}
			decoder->RGBFilterBufferSize = frame_size*3;
		}
//#endif
	}

#if _THREADED
	TransformInverseSpatialUniversalThreadedToRow16u(decoder, frame, num_channels,
						(uint8_t *)decoder->RawBayer16, bayer_pitch*sizeof(PIXEL),
						info, chroma_offset, precision);


	//DemosaicRAW
	{
		WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;
		int inverted = false;
		uint8_t *output = output_buffer;
		int pitch = output_pitch;

	#if _DELAY_THREAD_START
		if(decoder->worker_thread.pool.thread_count == 0)
		{
			CreateLock(&decoder->worker_thread.lock);
			// Initialize the pool of transform worker threads
			ThreadPoolCreate(&decoder->worker_thread.pool,
							decoder->thread_cntrl.capabilities >> 16/*cpus*/,
							WorkerThreadProc,
							decoder);
		}
	#endif
		if (format == DECODED_FORMAT_RGB24)
		{
			format = DECODED_FORMAT_RGB24_INVERTED;
			inverted = true;
		}
		else if (format == DECODED_FORMAT_RGB32)
		{
			format = DECODED_FORMAT_RGB32_INVERTED;
			inverted = true;
		}

		// Have the output location and pitch been inverted?
		if (inverted && pitch > 0) {
			int height = info->height;
			if(info->resolution == DECODED_RESOLUTION_FULL_DEBAYER || info->resolution == DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER)
				height *= 2;
			output += (height - 1) * pitch;		// Start at the bottom row
			pitch = NEG(pitch);					// Negate the pitch to go up
		}

		// Post a message to the mailbox
		mailbox->output = output;
		mailbox->pitch = pitch;
		memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
		mailbox->jobType = JOB_TYPE_OUTPUT;

		// Set the work count to the number of rows to process
		ThreadPoolSetWorkCount(&decoder->worker_thread.pool, info->height);

		// Start the transform worker threads
		ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

		// Wait for all of the worker threads to finish
		ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
	}

#else
	error = CODEC_ERROR_UNSUPPORTED_FORMAT;
#endif

	return error;
}

// Reconstruct Bayer encoded data to half resolution
CODEC_ERROR ReconstructSampleFrameBayerHalfToBuffer(DECODER *decoder, FRAME_INFO *info, int frame, uint8_t *output_buffer, int output_pitch)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	//CODEC_STATE *codec = &decoder->codec;
	//int num_channels = codec->num_channels;
	//int progressive = codec->progressive;
	//int precision = codec->precision;

	TRANSFORM **transform_array = decoder->transform;
	int frame_width = info->width;
	int frame_height = info->height;
	//int resolution = info->resolution;
	int format = info->format;

	//IMAGE *lowpass_images[TRANSFORM_MAX_CHANNELS];

	PIXEL16U *g1_plane;
	PIXEL16U *rg_plane;
	PIXEL16U *bg_plane;
	PIXEL16U *g2_plane;

	int g1_pitch;
	int rg_pitch;
	int bg_pitch;
	int g2_pitch;

#if 0
	int channel;
	for (channel = 0; channel < num_channels; channel++)
	{
		lowpass_images[channel] = transform_array[channel]->wavelet[frame];

#if (0 && DEBUG)
		if (logfile) {
			char label[_MAX_PATH];
			char *format = decoded_format_string[info->format];
			sprintf(label, "Output, channel: %d, format: %s", channel, format);
			DumpImageStatistics(label, lowpass_images[channel], logfile);
		}
#endif
	}
#endif

	// Get the lowpass bands in the wavelet coresponding to the output frame
	g1_plane = (PIXEL16U *)transform_array[0]->wavelet[frame]->band[0];
	rg_plane = (PIXEL16U *)transform_array[1]->wavelet[frame]->band[0];
	bg_plane = (PIXEL16U *)transform_array[2]->wavelet[frame]->band[0];

	if(transform_array[3]->wavelet[frame]) //half res don't decode g1-g2 //HACK
	{
		g2_plane = (PIXEL16U *)transform_array[3]->wavelet[frame]->band[0];
		g2_pitch = transform_array[3]->wavelet[frame]->pitch;
	}
	else
	{
		g2_plane = NULL;
		g2_pitch = 0;
	}

	// Get the pitch of each plane
	g1_pitch = transform_array[0]->wavelet[frame]->pitch;
	rg_pitch = transform_array[1]->wavelet[frame]->pitch;
	bg_pitch = transform_array[2]->wavelet[frame]->pitch;

	switch (format)
	{
	case DECODED_FORMAT_RGB32:
		ConvertPlanarBayerToRGB32(g1_plane, g1_pitch, rg_plane, rg_pitch,
								  bg_plane, bg_pitch, g2_plane, g2_pitch,
								  output_buffer, output_pitch,
								  frame_width, frame_height);
		break;

	default:
		error = CODEC_ERROR_UNSUPPORTED_FORMAT;
		break;
	}

	return error;
}

// Reconstruct Bayer encoded data to quarter resolution
CODEC_ERROR ReconstructSampleFrameBayerQuarterToBuffer(DECODER *decoder, int frame, uint8_t *output, int pitch)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	//FRAME_INFO *info = &decoder->frame;

	//CODEC_STATE *codec = &decoder->codec;
	//int num_channels = codec->num_channels;
	//int progressive = codec->progressive;
	//int precision = codec->precision;

	//TRANSFORM **transform_array = decoder->transform;
	//int decoded_width = 0;
	//int decoded_height = 0;
	//int resolution = info->resolution;
	//int format = info->format;

	//TODO: Need to finish this routine
	assert(0);

	return error;
}

// Reconstruct the original YUV 4:2:2 encoded format to the requested output format
CODEC_ERROR ReconstructSampleFrameYUV422ToBuffer(DECODER *decoder, int frame, uint8_t *output, int pitch)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	FRAME_INFO *info = &decoder->frame;

	CODEC_STATE *codec = &decoder->codec;
	int num_channels = codec->num_channels;
	int progressive = codec->progressive;
	int precision = codec->precision;

	TRANSFORM **transform_array = decoder->transform;
	//int decoded_width = 0;
	//int decoded_height = 0;
	int resolution = info->resolution;
	int format = info->format;

	//int color_space = decoder->frame.colorspace;

	//TODO: Eliminate use of the chroma offset
	int chroma_offset = decoder->codec.chroma_offset;

#if _THREADED
	// Type of threaded inverse transform
	//int type;
#endif

#if _ALLOCATOR
	ALLOCATOR *allocator = decoder->allocator;
#endif

	if (decoder == NULL) {
		return CODEC_ERROR_INVALID_ARGUMENT;
	}

	//TODO: Split this routine into subroutines for progressive versus interlaced video
	//TODO: Split progressive and interlaced routines into subroutines for each resolution

	if(resolution == DECODED_RESOLUTION_HALF)
	{
		bool inverted = false;
		FRAME_INFO info2;

		memcpy(&info2, info, sizeof(FRAME_INFO));

		format = info2.format;

		if (format == DECODED_FORMAT_RGB24) {
			format = DECODED_FORMAT_RGB24_INVERTED;
			info2.format = format;
			inverted = true;
		}
		else if (format == DECODED_FORMAT_RGB32) {
			format = DECODED_FORMAT_RGB32_INVERTED;
			info2.format = format;
			inverted = true;
		}
	#if 1
		// Have the output location and pitch been inverted?
		if (inverted && pitch > 0) {
			int height = info->height;
			output += (height - 1) * pitch;		// Start at the bottom row
			pitch = NEG(pitch);					// Negate the pitch to go up
		}
	#endif

		if(decoder->use_active_metadata_decoder)
		{
#if _THREADED
			WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	#if _DELAY_THREAD_START
			if(decoder->worker_thread.pool.thread_count == 0)
			{
				CreateLock(&decoder->worker_thread.lock);
				// Initialize the pool of transform worker threads
				ThreadPoolCreate(&decoder->worker_thread.pool,
								decoder->thread_cntrl.capabilities >> 16/*cpus*/,
								WorkerThreadProc,
								decoder);
			}
	#endif
			// Post a message to the mailbox
			mailbox->output = output;
			mailbox->pitch = pitch;
			mailbox->framenum = frame;
			memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
			mailbox->jobType = JOB_TYPE_OUTPUT;
			decoder->RGBFilterBufferPhase = 1;

			// Set the work count to the number of rows to process
			ThreadPoolSetWorkCount(&decoder->worker_thread.pool, info->height);

			// Start the transform worker threads
			ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

			// Wait for all of the worker threads to finish
			ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

			decoder->RGBFilterBufferPhase = 0;
			return CODEC_ERROR_OKAY;
#endif
		}
		else
		{
			int precision = codec->precision;
			TRANSFORM **transform_array = decoder->transform;
			int channel;
			IMAGE *lowpass_images[TRANSFORM_MAX_CHANNELS];
			CODEC_STATE *codec = &decoder->codec;
			int num_channels = codec->num_channels;

			for (channel = 0; channel < num_channels; channel++)
			{
				lowpass_images[channel] = transform_array[channel]->wavelet[frame];
			}

			CopyLowpass16sToBuffer(decoder, lowpass_images, num_channels, output, pitch, &info2, chroma_offset,
				precision, decoder->codec.encoded_format, decoder->frame.white_point);
		}
		return CODEC_ERROR_OKAY;
	}


	// Was the video source interlaced or progressive?
	if (progressive)
	{
		// The video source was progressive (the first transform was a spatial transform)

		if (resolution == DECODED_RESOLUTION_FULL || resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
		{
			FRAME_INFO info2;
			int format;
			bool inverted = false;
			int precision = codec->precision;

			memcpy(&info2, info, sizeof(FRAME_INFO));

			format = info2.format;

			if (format == DECODED_FORMAT_RGB24) {
				format = DECODED_FORMAT_RGB24_INVERTED;
				info2.format = format;
				inverted = true;
			}
			else if (format == DECODED_FORMAT_RGB32) {
				format = DECODED_FORMAT_RGB32_INVERTED;
				info2.format = format;
				inverted = true;
			}
#if 1
			// Have the output location and pitch been inverted?
			if (inverted && pitch > 0) {
				int height = info->height;
				output += (height - 1) * pitch;		// Start at the bottom row
				pitch = NEG(pitch);					// Negate the pitch to go up
			}
#endif


			/*if(decoder->use_active_metadata_decoder)
			{
				switch (format & 0x7ffffff)
				{
				case DECODED_FORMAT_RGB24: // Output buffer is too small to decode into for
				case DECODED_FORMAT_YUYV:  // computing the active metadata.
				case DECODED_FORMAT_UYVY:
					return CODEC_ERROR_OKAY;
					break;
				}
			}*/

			switch (format & 0x7ffffff)
			{
			case DECODED_FORMAT_RGB24: // Output buffer is too small to decode into for
				if(decoder->use_active_metadata_decoder)
				{
	#if _THREADED
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sThruActiveMetadata);
					return CODEC_ERROR_OKAY;
	#endif
				}
				else
				{
	#if _THREADED
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sYUVtoRGB);
					return CODEC_ERROR_OKAY;
	#endif
				}
				break;
			case DECODED_FORMAT_YUYV:
			case DECODED_FORMAT_UYVY:
				if(decoder->use_active_metadata_decoder)
				{
	#if _THREADED
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sThruActiveMetadata);
					return CODEC_ERROR_OKAY;
	#endif
				}
				else
				{
	#if _THREADED
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sToYUV);
					return CODEC_ERROR_OKAY;
	#endif
				}
				break;

			//Handle sizes that are smaller than the interim decode buffer //DAN20081222
			case DECODED_FORMAT_CbYCrY_10bit_2_8:
				decoder->upper_plane = output;
				decoder->lower_plane = output + decoder->frame.width * decoder->frame.height / 2;

				// Use the address and pitch of the lower plane
				output = decoder->lower_plane;
				pitch = decoder->frame.width * 2;

				// Fall through and compute the inverse spatial transform
			case DECODED_FORMAT_CbYCrY_16bit_2_14:
			case DECODED_FORMAT_CbYCrY_16bit_10_6:
			case DECODED_FORMAT_CbYCrY_8bit:
			case DECODED_FORMAT_CbYCrY_16bit:
				if(decoder->use_active_metadata_decoder)
				{
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sThruActiveMetadata);
					return CODEC_ERROR_OKAY;
				}
				else
				{
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sToOutput);
					return CODEC_ERROR_OKAY;
				}
				break;

			case DECODED_FORMAT_V210:
				if(decoder->use_active_metadata_decoder)
				{
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sThruActiveMetadata);
					return CODEC_ERROR_OKAY;
				}
				else
				{
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalYUVStrip16sToYUVOutput);
					return CODEC_ERROR_OKAY;
				}
				break;

			case DECODED_FORMAT_RGB32:
			case DECODED_FORMAT_RGB32_INVERTED:
			// As long as the outpitch is greater or equal to 4:2:2 16-bit YR16 this works.
			case DECODED_FORMAT_RG48:
			case DECODED_FORMAT_RG64:
			case DECODED_FORMAT_R210:
			case DECODED_FORMAT_DPX0:
			case DECODED_FORMAT_RG30:
			case DECODED_FORMAT_AR10:
			case DECODED_FORMAT_AB10:
			case DECODED_FORMAT_B64A:
			case DECODED_FORMAT_R408:
			case DECODED_FORMAT_V408:
			case DECODED_FORMAT_YU64:
			case DECODED_FORMAT_YR16:
			case DECODED_FORMAT_WP13:
			case DECODED_FORMAT_W13A:
				if((format & 0x7FFFFFFF) == DECODED_FORMAT_RGB32 && decoder->use_active_metadata_decoder == false)
				{

	#if _THREADED
					TransformInverseSpatialThreadedYUV422ToBuffer(decoder,
						frame, num_channels, output, pitch,
						&info2, chroma_offset, precision);
	#elif 0
					TransformInverseSpatialToBuffer(decoder, transform_array, frame,
						num_channels, output, pitch,
						&info2, &decoder->scratch, chroma_offset, precision);
	#else
					TransformInverseSpatialYUV422ToOutput(decoder, transform_array,
						frame, num_channels, output, pitch,
						&info2, &decoder->scratch, chroma_offset, precision,
						InvertHorizontalStripYUV16sToPackedRGB32);
	#endif
					return CODEC_ERROR_OKAY;
				}

#if _THREADED
				if(decoder->use_active_metadata_decoder)
				{
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sThruActiveMetadata);
					return CODEC_ERROR_OKAY;
				}
				else
				{
					TransformInverseSpatialUniversalThreadedToRow16u(decoder, frame,
						num_channels, output, pitch,
						&info2, chroma_offset, precision);

					ConvertRow16uToOutput(decoder, frame, num_channels, output, pitch,
						&info2, chroma_offset, precision);
					return CODEC_ERROR_OKAY;
				}
#endif
				break;


			default:
				if(decoder->use_active_metadata_decoder)
				{
					#if _THREADED
					TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sThruActiveMetadata);
					return CODEC_ERROR_OKAY;
					#endif
				} 
				// else Return the error code for unsupported output format
				break;
			}
		}
	}
	else
	{
		// The video source was interlaced (the first transform was a frame transform)

		if (resolution == DECODED_RESOLUTION_FULL || resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
		{
			bool inverted = false;
			if (format == DECODED_FORMAT_RGB32 || format == DECODED_FORMAT_RGB24) {
		//		info->format = DECODED_FORMAT_RGB32_INVERTED; //DAN20080702 vertically flips QT decodes if active.
				inverted = true;
			}
#if 1
			// Have the output location and pitch been inverted?
			if (inverted && pitch > 0) {
				int height = info->height;
				output += (height - 1) * pitch;		// Start at the bottom row
				pitch = NEG(pitch);					// Negate the pitch to go up
			}
#endif

			switch (format & 0x7ffffff)
			{
			case DECODED_FORMAT_NV12:
			case DECODED_FORMAT_RGB24: // Output buffer is too small to decode into for
			case DECODED_FORMAT_YUYV:
			case DECODED_FORMAT_UYVY:
			case DECODED_FORMAT_V210:  // only supported with use_active_metadata_decoder
				if(decoder->use_active_metadata_decoder)
				{
					int frame_size = info->width * info->height * 4;
					if(decoder->RGBFilterBuffer16==NULL || decoder->RGBFilterBufferSize < frame_size)
					{

#if _ALLOCATOR
						if(decoder->RGBFilterBuffer16)
						{
							FreeAligned(decoder->allocator, decoder->RGBFilterBuffer16);
							decoder->RGBFilterBuffer16 = NULL;
						}
						decoder->RGBFilterBuffer16 = (PIXEL16U *)AllocAligned(allocator, frame_size, 16);
#else
						if(decoder->RGBFilterBuffer16)
						{
							MEMORY_ALIGNED_FREE(decoder->RGBFilterBuffer16);
							decoder->RGBFilterBuffer16 = NULL;
						}
						decoder->RGBFilterBuffer16 = (PIXEL16U *)MEMORY_ALIGNED_ALLOC(frame_size, 16);
#endif
						assert(decoder->RGBFilterBuffer16 != NULL);
						if (! (decoder->RGBFilterBuffer16 != NULL)) {
							return CODEC_ERROR_MEMORY_ALLOC;
						}
						decoder->RGBFilterBufferSize = frame_size;
					}

					//TransformInverseSpatialUniversalThreadedToRow16u(
					//	decoder, frame, num_channels,
					//	(uint8_t *)decoder->RGBFilterBuffer16, info->width * 3 * 2,
					//	info, chroma_offset, precision);

#if _INTERLACED_WORKER_THREADS
					StartInterlaceWorkerThreads(decoder);

					//TODO: support new threading
					// Send the upper and lower rows of the transforms to the worker threads
					TransformInverseFrameThreadedToRow16u(decoder, frame, num_channels,
															(PIXEL16U *)decoder->RGBFilterBuffer16,
															info->width * 4,
															info, chroma_offset, precision);
#else
					// Transform the wavelets for each channel to the output image (not threaded)
					TransformInverseFrameToRow16u(decoder, transform_array, frame, num_channels,
													(PIXEL16U *)decoder->RGBFilterBuffer16,
													info->width * 4, info,
													&decoder->scratch, chroma_offset, precision);
#endif

#if _THREADED
					{
						WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	#if _DELAY_THREAD_START
						if(decoder->worker_thread.pool.thread_count == 0)
						{
							CreateLock(&decoder->worker_thread.lock);
							// Initialize the pool of transform worker threads
							ThreadPoolCreate(&decoder->worker_thread.pool,
											decoder->thread_cntrl.capabilities >> 16/*cpus*/,
											WorkerThreadProc,
											decoder);
						}
	#endif
						// Post a message to the mailbox
						mailbox->output = output;
						mailbox->pitch = pitch;
						memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
						mailbox->jobType = JOB_TYPE_OUTPUT;
						decoder->RGBFilterBufferPhase = 2; // yuv

						// Set the work count to the number of rows to process
						ThreadPoolSetWorkCount(&decoder->worker_thread.pool, info->height);

						// Start the transform worker threads
						ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

						// Wait for all of the worker threads to finish
						ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

						decoder->RGBFilterBufferPhase = 0;
					}
#endif
					return CODEC_ERROR_OKAY;
				}
			}



			switch (format)
			{
			// As long as the outpitch is greater or equal to 4:2:2 16-bit YR16 this works.
			case DECODED_FORMAT_WP13: //DAN20110203 - missing 
			case DECODED_FORMAT_W13A: //DAN20110203 - missing 
			case DECODED_FORMAT_RG48:
			case DECODED_FORMAT_RG64:
			case DECODED_FORMAT_R210:
			case DECODED_FORMAT_DPX0:
			case DECODED_FORMAT_RG30:
			case DECODED_FORMAT_AR10:
			case DECODED_FORMAT_AB10:
			case DECODED_FORMAT_B64A:
			case DECODED_FORMAT_RGB32:  //32-bit format can fit the interim YR16 decode into
			case DECODED_FORMAT_R408:   //the output buffer
			case DECODED_FORMAT_V408:
			case DECODED_FORMAT_YU64:
			case DECODED_FORMAT_YR16:

#if _INTERLACED_WORKER_THREADS
				StartInterlaceWorkerThreads(decoder);

				//TODO: support new threading
				// Send the upper and lower rows of the transforms to the worker threads
				TransformInverseFrameThreadedToRow16u(decoder, frame, num_channels,
												  (PIXEL16U *)output, pitch,
												  info, chroma_offset, precision);

				ConvertRow16uToOutput(decoder, frame, num_channels, output, pitch,
														info, chroma_offset, precision);
#else
				// Transform the wavelets for each channel to the output image (not threaded)
				TransformInverseFrameToRow16u(decoder, transform_array, frame, num_channels,
										  (PIXEL16U *)output, pitch, info,
										  &decoder->scratch, chroma_offset, precision);

				ConvertRow16uToOutput(decoder, frame, num_channels, output, pitch,
														info, chroma_offset, precision);

				//Old code converts 4:2:2 directly to RGBA (single threaded.)
				//TransformInverseFrameToBuffer(transform_array, frame, num_channels, output, pitch,
				//							  info, &decoder->scratch, chroma_offset, precision);
#endif
				return CODEC_ERROR_OKAY;

			default:
				// else Return the error code for unsupported output format
				break;
			}
		}
	}

	// The output format is not supported by this routine
	error = CODEC_ERROR_UNSUPPORTED_FORMAT;

	return error;
}



// Routines for converting the new encoded formats to the requested output format
CODEC_ERROR ReconstructSampleFrameRGB444ToBuffer(DECODER *decoder, int frame, uint8_t *output, int pitch)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	FRAME_INFO *info = &decoder->frame;

	CODEC_STATE *codec = &decoder->codec;
	int num_channels = codec->num_channels;
	//int progressive = codec->progressive;

	TRANSFORM **transform_array = decoder->transform;
	//IMAGE *lowpass_images[TRANSFORM_MAX_CHANNELS];
	//IMAGE *wavelet;
	//int wavelet_width;
	//int wavelet_height;
	int decoded_width = 0;
	int decoded_height = 0;
	int resolution = info->resolution;
	//int chroma_offset = decoder->codec.chroma_offset;
	//int decoded_scale;

#if _ALLOCATOR
	ALLOCATOR *allocator = decoder->allocator;
#endif

	//TODO: Eliminate use of the chroma offset

	if (decoder == NULL) {
		return CODEC_ERROR_INVALID_ARGUMENT;
	}

	// This routine should only be called for progressive frames
	assert(codec->progressive);

	// The decoder can decode a video sample without returning a frame
	if (output == NULL || pitch == 0) {
		return CODEC_ERROR_OKAY;
	}

	// Does this frame have to be reconstructed?
	if ((decoder->flags & DECODER_FLAGS_RENDER) == 0) {
		return CODEC_ERROR_OKAY;
	}

	// Check that the requested frame is within the limits of the group of frames
	assert(0 <= frame && frame < decoder->gop_length);

	// Check that the frame resolution is valid
	assert(IsValidFrameResolution(resolution));
	if (!IsValidFrameResolution(resolution)) {
		return CODEC_ERROR_RESOLUTION;
	}

	// Compute the decoded width and height
	ComputeOutputDimensions(decoder, frame, &decoded_width, &decoded_height);
	assert(decoded_width > 0 && decoded_height > 0);

	if (info->format == DECODED_FORMAT_RGB24 || info->format == DECODED_FORMAT_RGB32)
	{
		output += (info->height-1)*pitch;
		pitch = -pitch;
	}

#if (0 && DEBUG)
	if (logfile) {
		IMAGE *wavelet = transform[0]->wavelet[frame];
		int band = 0;
		fprintf(logfile, "Luminance wavelet, frame: %d, band: %d\n", frame, band);
		DumpArray16s("Lowpass Band", wavelet->band[band], wavelet->width, wavelet->height, wavelet->pitch, logfile);
	}
#endif

	// Check that the requested frame is large enough to hold the decoded frame
#if (0 && DEBUG)
	//if (! (info->width >= decoded_width))
	{
		if (logfile) {
			//fprintf(logfile, "Requested frame not large enough to hold decoded frame: %d < %d\n", info->width, decoded_width);
			fprintf(logfile, "Output frame width: %d, decoded frame width: %d\n", info->width, decoded_width);
		}
	}
#endif
	assert(info->width >= decoded_width);
	if (!(info->width >= decoded_width)) {
		return CODEC_ERROR_FRAMESIZE;
	}
//	assert((info->height+7)/8 >= (decoded_height+7)/8);
//	if (!(info->height+7)/8 >= (decoded_height+7)/8) {
//		return CODEC_ERROR_FRAMESIZE;
//	}

	START(tk_convert);

	if (resolution == DECODED_RESOLUTION_LOWPASS_ONLY)
	{
		//int precision = codec->precision;
		int scale = 13;
		int channel;
		IMAGE *lowpass_images[TRANSFORM_MAX_CHANNELS];
		int chroma_offset = decoder->codec.chroma_offset;

		//DAN20081203 -- fix for 444 decodes in AE32-bit float
		decoder->frame.white_point = 16;
		//decoder->frame.signed_pixels = 0;

		for (channel = 0; channel < num_channels; channel++)
		{
			lowpass_images[channel] = transform_array[channel]->wavelet[5];
			if(lowpass_images[channel] == NULL) // therefore IntreFrame compressed.
			{
				scale = 12;
				lowpass_images[channel] = transform_array[channel]->wavelet[2];
			}
		}

		CopyLowpass16sToBuffer(decoder, lowpass_images, num_channels, output, pitch, info, chroma_offset,
			scale, decoder->codec.encoded_format, decoder->frame.white_point);
	}
	else
	// Quarter resolution
	if (resolution == DECODED_RESOLUTION_QUARTER)
	{
		// Output quarter resolution for the two frame GOP
		int precision = codec->precision;

		// Reconstruct the frame to quarter resolution
		ReconstructQuarterFrame(decoder, num_channels, frame, output, pitch,
								info, &decoder->scratch, precision);

		// Quarter resolution one frame GOP is handled in DecodeSampleIntraFrame
	}
	else
	// Half resolution
	if (resolution == DECODED_RESOLUTION_HALF)
	{
		IMAGE *wavelet_array[TRANSFORM_MAX_CHANNELS];
		int precision = codec->precision;
		int chroma_offset = 0;
		int channel;

		if(decoder->use_active_metadata_decoder)
		{
#if _THREADED
			{
				WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	#if _DELAY_THREAD_START
				if(decoder->worker_thread.pool.thread_count == 0)
				{
					CreateLock(&decoder->worker_thread.lock);
					// Initialize the pool of transform worker threads
					ThreadPoolCreate(&decoder->worker_thread.pool,
									decoder->thread_cntrl.capabilities >> 16/*cpus*/,
									WorkerThreadProc,
									decoder);
				}
	#endif
				// Post a message to the mailbox
				mailbox->output = output;
				mailbox->pitch = pitch;
				mailbox->framenum = frame;
				memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
				mailbox->jobType = JOB_TYPE_OUTPUT;
				decoder->RGBFilterBufferPhase = 1;

				// Set the work count to the number of rows to process
				ThreadPoolSetWorkCount(&decoder->worker_thread.pool, info->height);

				// Start the transform worker threads
				ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

				// Wait for all of the worker threads to finish
				ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

				decoder->RGBFilterBufferPhase = 0;
			}
#endif
		}
		else
		{
			//DAN20081203 -- fix for 444 decodes in AE32-bit float
			decoder->frame.white_point = 16;
			//decoder->frame.signed_pixels = 0;

			// Get the first level wavelet in each channel
			for (channel = 0; channel < num_channels; channel++)
			{
				wavelet_array[channel] = transform_array[channel]->wavelet[frame];
			}

			// Pack the pixels from the lowpass band in each channel into the output buffer
			CopyLowpassRGB444ToBuffer(decoder, wavelet_array, num_channels, output, pitch,
									info, chroma_offset, precision);
		}
	}

	// Full resolution or half horizontal
	else
	{
		int chroma_offset = 0;
		int precision = codec->precision;

		// Reconstruct the output frame from a full resolution decode
		//assert(resolution == DECODED_RESOLUTION_FULL);

		if(decoder->use_active_metadata_decoder)
		{
			int frame_size, channels = 3;
			if(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444 && ALPHAOUTPUT(decoder->frame.format))
				channels = 4;
			
			frame_size = info->width * info->height * channels * 2;


			if(decoder->RGBFilterBuffer16==NULL || decoder->RGBFilterBufferSize < frame_size)
			{
#if _ALLOCATOR
				if(decoder->RGBFilterBuffer16)
				{
					FreeAligned(decoder->allocator, decoder->RGBFilterBuffer16);
					decoder->RGBFilterBuffer16 = NULL;
				}
				decoder->RGBFilterBuffer16 = (PIXEL16U *)AllocAligned(allocator, frame_size, 16);
#else
				if(decoder->RGBFilterBuffer16)
				{
					MEMORY_ALIGNED_FREE(decoder->RGBFilterBuffer16);
					decoder->RGBFilterBuffer16 = NULL;
				}
				decoder->RGBFilterBuffer16 = (PIXEL16U *)MEMORY_ALIGNED_ALLOC(frame_size, 16);
#endif
				assert(decoder->RGBFilterBuffer16 != NULL);
				if (! (decoder->RGBFilterBuffer16 != NULL)) {
					return CODEC_ERROR_MEMORY_ALLOC;
				}
				decoder->RGBFilterBufferSize = frame_size;
			}

#if _THREADED
			TransformInverseSpatialUniversalThreadedToRow16u(decoder, frame, num_channels,
								(uint8_t *)decoder->RGBFilterBuffer16, info->width * channels * 2,
								info, chroma_offset, precision);
#else
			// Decode that last transform to rows of Bayer data (one row per channel)
			TransformInverseSpatialToRow16u(transform_array, frame, num_channels,
								(uint8_t *)decoder->RGBFilterBuffer16, info->width * channels * 2,
								info, &decoder->scratch, chroma_offset, precision);
#endif


#if _THREADED
			{
				WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	#if _DELAY_THREAD_START
				if(decoder->worker_thread.pool.thread_count == 0)
				{
					CreateLock(&decoder->worker_thread.lock);
					// Initialize the pool of transform worker threads
					ThreadPoolCreate(&decoder->worker_thread.pool,
									decoder->thread_cntrl.capabilities >> 16/*cpus*/,
									WorkerThreadProc,
									decoder);
				}
	#endif
				// Post a message to the mailbox
				mailbox->output = output;
				mailbox->pitch = pitch;
				memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
				mailbox->jobType = JOB_TYPE_OUTPUT;
				decoder->RGBFilterBufferPhase = 1;

				// Set the work count to the number of rows to process
				ThreadPoolSetWorkCount(&decoder->worker_thread.pool, info->height);

				// Start the transform worker threads
				ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

				// Wait for all of the worker threads to finish
				ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

				decoder->RGBFilterBufferPhase = 0;
			}
#endif
		}
		else
		{

			//DAN20081203 -- fix for 444 decodes in AE32-bit float
			decoder->frame.white_point = 16;
			//decoder->frame.signed_pixels = 0;


			switch (info->format)
			{
			case DECODED_FORMAT_B64A:
	#if _THREADED
				TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sRGB2B64A);
	#else
				TransformInverseRGB444ToB64A(transform_array, frame, num_channels, output, pitch,
											info, &decoder->scratch, chroma_offset, precision);
	#endif
				break;

			case DECODED_FORMAT_YU64: //TODO : Threading
				TransformInverseRGB444ToYU64(transform_array, frame, num_channels, output, pitch,
											info, &decoder->scratch, chroma_offset, precision);
				break;

			case DECODED_FORMAT_RGB24:
			case DECODED_FORMAT_RGB24_INVERTED:
			case DECODED_FORMAT_RGB32:
			case DECODED_FORMAT_RGB32_INVERTED://TODO, needs to be threaded. WIP
				TransformInverseRGB444ToRGB32(transform_array, frame, num_channels, output, pitch,
											info, &decoder->scratch, chroma_offset, precision);
				break;

			case DECODED_FORMAT_RG48:
			case DECODED_FORMAT_RG64: //TODO, needs to be threaded. WIP
				TransformInverseRGB444ToRGB48(transform_array, frame, num_channels, output, pitch,
											info, &decoder->scratch, chroma_offset, precision);
				break;

			case DECODED_FORMAT_R210:
			case DECODED_FORMAT_DPX0:
			case DECODED_FORMAT_RG30:
			case DECODED_FORMAT_AR10:
			case DECODED_FORMAT_AB10:
	#if _THREADED
				TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sRGB2RG30);
	#else
				TransformInverseRGB444ToRGB48(transform_array, frame, num_channels, output, pitch,
											info, &decoder->scratch, chroma_offset, precision);
	#endif
				break;


			case DECODED_FORMAT_YUYV:
			case DECODED_FORMAT_UYVY:
	#if _THREADED
				TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sRGB2YUV);
	#else
				TransformInverseSpatialYUV422ToOutput(decoder, transform_array, frame, num_channels, output, pitch,
												info, &decoder->scratch, chroma_offset, precision,
												InvertHorizontalStripRGB16sToPackedYUV8u);
	#endif
				break;


			case DECODED_FORMAT_R408:
			case DECODED_FORMAT_V408:

	#if _THREADED
				TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sRGBA2YUVA);
	#else
				assert(0);
	#endif
				break;

			case DECODED_FORMAT_YR16:
	#if _THREADED
				TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sRGB2YR16);
	#else
				assert(0);// missing non-threaded version
	#endif
				break;

			case DECODED_FORMAT_V210:
	#if _THREADED
				TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sRGB2v210);
	#else
				assert(0);// missing non-threaded version
	#endif
				break;

			case DECODED_FORMAT_CbYCrY_8bit:		// DECODED_FORMAT_CT_UCHAR
	#if _THREADED
				TransformInverseSpatialUniversalThreadedToOutput(decoder, frame, num_channels,
												output, pitch,
												info, chroma_offset, precision,
												InvertHorizontalStrip16sRGB2YUV);
	#else
				assert(0);// missing non-threaded version
	#endif
				break;

			//TODO: Add code to handle other Avid pixel formats
			case DECODED_FORMAT_CbYCrY_16bit:		// DECODED_FORMAT_CT_SHORT
			case DECODED_FORMAT_CbYCrY_10bit_2_8:	// DECODED_FORMAT_CT_10Bit_2_8
			case DECODED_FORMAT_CbYCrY_16bit_2_14:	// DECODED_FORMAT_CT_SHORT_2_14
			case DECODED_FORMAT_CbYCrY_16bit_10_6:	// DECODED_FORMAT_CT_USHORT_10_6
				assert(0);
				break;

			default:
	#if (1 && DEBUG)
				if (logfile) {
					fprintf(logfile, "Invalid decoded format: %d\n", info->format);
				}
	#endif
				assert(0);
				error = CODEC_ERROR_INVALID_FORMAT;
				break;
			}
		}
	}

	STOP(tk_convert);

	return error;
}




// Convert 16-bit signed lowpass data into the requested output format
void CopyLowpassRGB444ToBuffer(DECODER *decoder, IMAGE *image_array[], int num_channels,
							   uint8_t *output_buffer, int32_t output_pitch,
							   FRAME_INFO *info, int chroma_offset,
							   int precision)
{
	bool inverted = false;
	int output_width = info->width;
	int output_height = info->height;
	int format = info->format;

	// Left shift to scale the pixels to 16 bits minus the shift already in the lowpass values
	const int shift = 16 - precision - PRESCALE_LUMA;

	START(tk_convert);

#if 0
	// Fill the output buffer with blank values
	EraseOutputBuffer(output_buffer, info->width, info->height, output_pitch, info->format);
#endif

	// Determine the type of conversion
	switch (info->format)
	{
	case DECODED_FORMAT_RGB24:
	case DECODED_FORMAT_RGB32:
		inverted = true;
	case DECODED_FORMAT_RGB24_INVERTED:
	case DECODED_FORMAT_RGB32_INVERTED:
	case DECODED_FORMAT_B64A:
	case DECODED_FORMAT_R210:
	case DECODED_FORMAT_DPX0:
	case DECODED_FORMAT_RG30:
	case DECODED_FORMAT_AR10:
	case DECODED_FORMAT_AB10:
	case DECODED_FORMAT_RG48:
	case DECODED_FORMAT_RG64: //WIP
		ConvertLowpassRGB444ToRGB(image_array, output_buffer, output_width, output_height,
								  output_pitch, format, inverted, shift, num_channels);
		break;
	case DECODED_FORMAT_YUYV:
	case DECODED_FORMAT_UYVY:
		{
			IMAGE *g_image = image_array[0];
			IMAGE *r_image = image_array[1];
			IMAGE *b_image = image_array[2];

			if (info->format == COLOR_FORMAT_YUYV)
			{
				ConvertRGB2YUV(r_image->band[0], g_image->band[0], b_image->band[0],
							   r_image->pitch, g_image->pitch, b_image->pitch,
							   output_buffer, output_pitch,
							   output_width, output_height, 14,
							   info->colorspace, info->format);
			}
			else if (info->format == COLOR_FORMAT_UYVY)
			{
				ConvertRGB2UYVY(r_image->band[0], g_image->band[0], b_image->band[0],
								r_image->pitch, g_image->pitch, b_image->pitch,
								output_buffer, output_pitch,
								output_width, output_height, 14,
								info->colorspace, info->format);
			}
		}
		break;

	default:				
		{
			int y;
			IMAGE *g_image = image_array[0];
			IMAGE *r_image = image_array[1];
			IMAGE *b_image = image_array[2];
			IMAGE *a_image = image_array[3];
			unsigned short *scanline = (unsigned short *)decoder->scratch.free_ptr;
			//unsigned short *scanline2 = scanline + output_width*3;
			uint8_t *newline = (uint8_t *)output_buffer;
			unsigned short *Rptr,*Gptr,*Bptr,*Aptr = NULL;

			Rptr = (unsigned short *)r_image->band[0];
			Gptr = (unsigned short *)g_image->band[0];
			Bptr = (unsigned short *)b_image->band[0];

			if(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444 && ALPHAOUTPUT(decoder->frame.format))
			{
				Aptr = (unsigned short *)a_image->band[0];
				for(y=0; y<output_height; y++)
				{
					int flags = (ACTIVEMETADATA_PLANAR);
					int whitebitdepth = 14;

					memcpy(scanline, Rptr, info->width*2);
					memcpy(scanline+info->width, Gptr, info->width*2);
					memcpy(scanline+info->width*2, Bptr, info->width*2);
					memcpy(scanline+info->width*3, Aptr, info->width*2);
					Rptr += r_image->pitch/2;
					Gptr += g_image->pitch/2;
					Bptr += b_image->pitch/2;
					Aptr += a_image->pitch/2;				

					Convert4444LinesToOutput(decoder, info->width, 1, y, scanline,
						newline, output_pitch, info->format, whitebitdepth, flags);

					newline += output_pitch;
				}
			}
			else
			{
				for(y=0; y<output_height; y++)
				{
					int flags = (ACTIVEMETADATA_PLANAR);
					int whitebitdepth = 14;

					memcpy(scanline, Rptr, info->width*2);
					memcpy(scanline+info->width, Gptr, info->width*2);
					memcpy(scanline+info->width*2, Bptr, info->width*2);
					Rptr += r_image->pitch/2;
					Gptr += g_image->pitch/2;
					Bptr += b_image->pitch/2;
				

					ConvertLinesToOutput(decoder, info->width, 1, y, scanline,
						newline, output_pitch, info->format, whitebitdepth, flags);

					newline += output_pitch;
				}
			}
		}
		//assert(0);
		break;
	}

	STOP(tk_convert);
}


#if _THREADED

// Threaded inverse transform using the new threads API
void TransformInverseSpatialThreadedYUV422ToBuffer(DECODER *decoder, int frame_index, int num_channels,
											 uint8_t *output, int pitch, FRAME_INFO *info,
											 int chroma_offset, int precision)
{

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif

	//TODO: Add support for more output formats
	int format = DECODED_FORMAT_RGB32;

	// The upper and lower spatial transforms only share the middle rows
	int transform_height = (((info->height + 7) / 8) * 8) / 2;
	int middle_row_count = transform_height;

	// Data structure for passing information to the worker threads
	WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	// Inverse horizontal filter that outputs the desired format
	HorizontalInverseFilterOutputProc horizontal_filter_proc;

#if _DELAY_THREAD_START
	if(decoder->worker_thread.pool.thread_count == 0)
	{
		CreateLock(&decoder->worker_thread.lock);
		// Initialize the pool of transform worker threads
		ThreadPoolCreate(&decoder->worker_thread.pool,
						decoder->thread_cntrl.capabilities >> 16/*cpus*/,
						WorkerThreadProc,
						decoder);
	}
#endif
	// Choose the correct inverse horizontal filter for the output format
	switch (format)
	{
	case DECODED_FORMAT_RGB32:
		horizontal_filter_proc = InvertHorizontalStripYUV16sToPackedRGB32;
		break;

	default:
		assert(0);
		return;
	}

	// Post a message to the mailbox
	mailbox->horizontal_filter_proc = horizontal_filter_proc;
	mailbox->frame = frame_index;
	mailbox->num_channels = num_channels;
	mailbox->output = output;
	mailbox->pitch = pitch;
	memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
	mailbox->chroma_offset = chroma_offset;
	mailbox->precision = precision;
	mailbox->jobType = JOB_TYPE_WAVELET;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&decoder->worker_thread.pool, middle_row_count);

	// Start the transform worker threads
	ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

#if (1 && DEBUG)
	if (logfile) {
		fprintf(logfile, "All worker threads signalled done\n");
	}
#endif
}


// Threaded inverse transform using the new threads API
// Convert RGB RGBA or BAYER (4 channel) data to a 16-bit planar format
void TransformInverseSpatialUniversalThreadedToRow16u(DECODER *decoder, int frame_index, int num_channels,
											 uint8_t *output, int pitch, FRAME_INFO *info,
											 int chroma_offset, int precision)
{

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif

	// The upper and lower spatial transforms only share the middle rows
	int transform_height = (((info->height + 7) / 8) * 8) / 2;
	int middle_row_count = transform_height;

	// Data structure for passing information to the worker threads
	WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	// Inverse horizontal filter that outputs the desired format
	HorizontalInverseFilterOutputProc horizontal_filter_proc;
	horizontal_filter_proc = InvertHorizontalStrip16sToRow16uPlanar;
#if _DELAY_THREAD_START
	if(decoder->worker_thread.pool.thread_count == 0)
	{
		CreateLock(&decoder->worker_thread.lock);
		// Initialize the pool of transform worker threads
		ThreadPoolCreate(&decoder->worker_thread.pool,
						decoder->thread_cntrl.capabilities >> 16/*cpus*/,
						WorkerThreadProc,
						decoder);
	}
#endif

	// Post a message to the mailbox
	mailbox->horizontal_filter_proc = horizontal_filter_proc;
	mailbox->frame = frame_index;
	mailbox->num_channels = num_channels;
	mailbox->output = output;
	mailbox->pitch = pitch;
	memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
	mailbox->chroma_offset = chroma_offset;
	mailbox->precision = precision;
	mailbox->jobType = JOB_TYPE_WAVELET;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&decoder->worker_thread.pool, middle_row_count);

	// Start the transform worker threads
	ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
}



// Threaded inverse transform using the new threads API
// Convert RGB RGBA or BAYER (4 channel) data to a 16-bit planar format
void TransformInverseSpatialUniversalThreadedToOutput(
								DECODER *decoder, int frame_index, int num_channels,
								uint8_t *output, int pitch, FRAME_INFO *info,
								int chroma_offset, int precision,
								HorizontalInverseFilterOutputProc horizontal_filter_proc)
{

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif

	// The upper and lower spatial transforms only share the middle rows
	int transform_height = (((info->height + 7) / 8) * 8) / 2;
	int middle_row_count = transform_height;

	// Data structure for passing information to the worker threads
	WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	// Inverse horizontal filter that outputs the desired format
#if _DELAY_THREAD_START
	if(decoder->worker_thread.pool.thread_count == 0)
	{
		CreateLock(&decoder->worker_thread.lock);
		// Initialize the pool of transform worker threads
		ThreadPoolCreate(&decoder->worker_thread.pool,
						decoder->thread_cntrl.capabilities >> 16/*cpus*/,
						WorkerThreadProc,
						decoder);
	}
#endif

	// Post a message to the mailbox
	mailbox->horizontal_filter_proc = horizontal_filter_proc;
	mailbox->frame = frame_index;
	mailbox->num_channels = num_channels;
	mailbox->output = output;
	mailbox->pitch = pitch;
	memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
	mailbox->chroma_offset = chroma_offset;
	mailbox->precision = precision;
	mailbox->jobType = JOB_TYPE_WAVELET;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&decoder->worker_thread.pool, middle_row_count);

	// Start the transform worker threads
	ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
}

// Routines for the worker threads that use the new threads API

void TransformInverseSpatialSectionToOutput(DECODER *decoder, int thread_index,
											int frame_index, int num_channels,
											uint8_t *output_buffer, int output_pitch, FRAME_INFO *info,
											int chroma_offset, int precision,
											HorizontalInverseFilterOutputProc horizontal_filter_proc)
{

#if (1 && DEBUG)
	FILE *logfile = decoder->logfile;
#endif
	TRANSFORM **transform = decoder->transform;
	const SCRATCH *scratch = &decoder->scratch;

	PIXEL *lowlow_band[CODEC_MAX_CHANNELS];
	PIXEL *lowhigh_band[CODEC_MAX_CHANNELS];
	PIXEL *highlow_band[CODEC_MAX_CHANNELS];
	PIXEL *highhigh_band[CODEC_MAX_CHANNELS];

	int lowlow_pitch[CODEC_MAX_CHANNELS];
	int lowhigh_pitch[CODEC_MAX_CHANNELS];
	int highlow_pitch[CODEC_MAX_CHANNELS];
	int highhigh_pitch[CODEC_MAX_CHANNELS];

	int channel_width[CODEC_MAX_CHANNELS];

	uint8_t *output_row_ptr;
	uint8_t *plane_array[TRANSFORM_MAX_CHANNELS];
	int plane_pitch[TRANSFORM_MAX_CHANNELS];
	int output_width = info->width;
	int output_height = info->height;
	int half_height = output_height/2;
	int luma_band_width;
	ROI strip;
	char *bufptr;
	int last_row;
	int last_display_row;
	int last_line;
	int channel;
	int row;
	int odd_display_lines = 0;

	THREAD_ERROR error;

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	//TODO: Replace uses of buffer variables with calls to the scratch space API

	// This version is for 16-bit pixels
	assert(sizeof(PIXEL) == 2);

	// Must have a valid inverse horizontal filter
	assert(horizontal_filter_proc != NULL);

	// Check for enough space in the local array allocations
//	assert(num_channels <= CODEC_NUM_CHANNELS);
	assert(num_channels <= TRANSFORM_MAX_CHANNELS);

	// Divide the buffer space between the four threads
	buffer_size /= decoder->worker_thread.pool.thread_count;  // used to assume max of 4
	buffer += buffer_size * thread_index;

	// Round the buffer pointer up to the next cache line
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)buffer & _CACHE_LINE_MASK));
	bufptr = (char *)ALIGN(buffer, _CACHE_LINE_SIZE);

	// Allocate buffer space for the output rows from each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the row width for this channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		//int pitch = wavelet->pitch;
		size_t channel_buffer_size;

		// Compute the width and pitch for the output rows stored in this buffer
		int buffer_width = 2 * width;
		int buffer_height = 2;
		int buffer_pitch = ALIGN16(buffer_width);

		// Compute the total allocation for this channel
		channel_buffer_size = buffer_height * buffer_pitch;

		// Check that there is enough space available
		assert(channel_buffer_size <= buffer_size);

		// Allocate the buffer for this channel
		plane_array[channel] = (uint8_t *)bufptr;

		// Remember the pitch for rows in this channel
		plane_pitch[channel] = buffer_pitch;

		// Advance the buffer pointer past the allocated space for this channel
		bufptr += channel_buffer_size;

		// Reduce the amount of space remaining in the buffer
		buffer_size -= channel_buffer_size;

		// The dimensions of the output image are the same as the luma channel
		if (channel == 0)
		{
			strip.width = buffer_width;
			strip.height = buffer_height;
			last_row = height;
			//DAN20050606 Added to fix issue with non-div by 8 heihts.
			last_display_row = (info->height+1)/2; // DAN20090215 -- fix for odd display lines.
			odd_display_lines = info->height & 1;

			// Remember the width of the wavelet bands for luma
			luma_band_width = width;
		}

		// Save the bands per channel for routines that process all channels at once
		lowlow_band[channel] = wavelet->band[0];
		lowhigh_band[channel] = wavelet->band[1];
		highlow_band[channel] = wavelet->band[2];
		highhigh_band[channel] = wavelet->band[3];

		lowlow_pitch[channel] = wavelet->pitch;
		lowhigh_pitch[channel] = wavelet->pitch;
		highlow_pitch[channel] = wavelet->pitch;
		highhigh_pitch[channel] = wavelet->pitch;

		// Remember the width of the wavelet for this channel
		channel_width[channel] = width;
	}

	// Use the remaining buffer space for intermediate results
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)bufptr & _CACHE_LINE_MASK));
	buffer = (char *)ALIGN(bufptr, _CACHE_LINE_SIZE);

	if (last_row == last_display_row)
	{
		last_line = half_height - 1;
	}
	else
	{
		last_line = half_height;
	}

	if(odd_display_lines)
		last_line++;


	if (thread_index == TRANSFORM_WORKER_TOP_THREAD)
	{
		// Process the first row
		row = 0;

		output_row_ptr = output_buffer;

#if (0 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Thread: %d, processing row: %d\n", thread_index, row);
		}
#endif
		// Process the first row using special border filters for the top row
		InvertSpatialTopRow16sToOutput(decoder, thread_index, lowlow_band, lowlow_pitch,
									   lowhigh_band, lowhigh_pitch,
									   highlow_band, highlow_pitch,
									   highhigh_band, highhigh_pitch,
									   output_row_ptr, output_pitch,
									   output_width, info->format, info->colorspace,
									   row, channel_width,
									   (PIXEL *)buffer, buffer_size,
									   precision,
									   horizontal_filter_proc);
	}

	if (thread_index == TRANSFORM_WORKER_BOTTOM_THREAD || decoder->worker_thread.pool.thread_count == 1)
	{
		if(last_row == last_display_row) //DAN20071218 -- Added as old 1080 RAW files would crash
		{
			int pitch = output_pitch;
			// Process the last row
			row = last_row - 1;

			if(decoder->channel_decodes > 1 && decoder->frame.format == DECODED_FORMAT_YUYV)  // 3d work
				if(decoder->channel_blend_type == BLEND_STACKED_ANAMORPHIC)
					pitch >>= 1;
			// Begin filling the last output row with results
			output_row_ptr = output_buffer + row * 2 * pitch;

#if (0 && DEBUG)
			if (logfile) {
				fprintf(logfile, "Thread: %d, processing row: %d\n", thread_index, row);
			}
#endif
			// Process the last row using special border filters for the bottom row

			if(decoder->channel_decodes > 1 && decoder->frame.format == DECODED_FORMAT_YUYV)
				if(decoder->channel_blend_type == BLEND_STACKED_ANAMORPHIC || decoder->channel_blend_type == BLEND_LINE_INTERLEAVED) // 3d Work TODO Fix
					output_row_ptr -= output_pitch;

			InvertSpatialBottomRow16sToOutput(decoder, thread_index, lowlow_band, lowlow_pitch,
											lowhigh_band, lowhigh_pitch,
											highlow_band, highlow_pitch,
											highhigh_band, highhigh_pitch,
											output_row_ptr, output_pitch,
											output_width, info->format, info->colorspace,
											row, channel_width,
											(PIXEL *)buffer, buffer_size,
											precision, odd_display_lines,
											horizontal_filter_proc);
		}
	}

	// Loop until all of the middle rows have been processed
	for (;;)
	{
		int work_index;
		int row;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			int pitch = output_pitch;
			// Compute the next row to process from the work index
			row = work_index + 1;

			if(decoder->channel_decodes > 1 && decoder->frame.format == DECODED_FORMAT_YUYV) // 3d work
				if(decoder->channel_blend_type == BLEND_STACKED_ANAMORPHIC) // stacked
					pitch >>= 1;

			// Compute the output row corresponding to this row index
			output_row_ptr = output_buffer + row * 2 * pitch;
		}
		else
		{
			// No more work to do
			return;
		}

		// Is the row inside the top and bottom border?
		if (0 < row && row < last_line)
		{
			int outputlines = 2;

#if (0 && DEBUG)
			if (logfile) {
				fprintf(logfile, "Thread: %d, processing row: %d\n", thread_index, row);
			}
#endif
			if(odd_display_lines && row==last_line-1)
			{
				outputlines = 1;
			}

			// Process the middle row using the normal wavelet filters
			InvertSpatialMiddleRow16sToOutput(decoder, thread_index,
											  lowlow_band, lowlow_pitch,
											  lowhigh_band, lowhigh_pitch,
											  highlow_band, highlow_pitch,
											  highhigh_band, highhigh_pitch,
											  output_row_ptr, output_pitch,
											  output_width, info->format, info->colorspace,
											  row, channel_width,
											  (PIXEL *)buffer, buffer_size,
											  precision,
											  horizontal_filter_proc,
											  outputlines);
		}
	}
}


#endif //_THREADED


bool GetTuplet(unsigned char *data, int datasize,
			   unsigned short findtag, unsigned short *retvalue)
{
	bool ret = false;
	BITSTREAM myinput, *pinput;
	TAGVALUE segment;
	TAGWORD tag,value;
	int error = 0;
	//char t[100];

	InitBitstream(&myinput);
	myinput.lpCurrentWord = data;
	myinput.nWordsUsed = datasize;
	pinput = &myinput;

	do
	{
		bool optional = false;
		int chunksize = 0;

		// Read the next tag value pair from the bitstream
		segment = GetSegment(pinput);

		tag = segment.tuple.tag;
		value = segment.tuple.value;


		// Is this an optional tag?
		if (tag < 0)
		{
			tag = NEG(tag);
			optional = true;
		}



		if(tag & 0x2000)
		{
			chunksize = value;
			chunksize &= 0xffff;
			chunksize += ((tag&0xff)<<16);
		}
		else if(tag & 0x4000)
		{
			chunksize = value;
			chunksize &= 0xffff;
		}
		else if(tag == CODEC_TAG_INDEX)
		{
			chunksize = value;
			chunksize &= 0xffff;
		}
		else
		{
			chunksize = 0;
		}

		if((int)(tag) <= ((int)CODEC_TAG_LAST_NON_SIZED) || tag & 0x6000)
		{
			int skip = 1;
			error = 0;

			if(tag == (int)findtag)
			{
				*retvalue = value;
				ret = true;
				break;
			}

			if((tag & 0xff00) == 0x2200) //sample size
			{
				chunksize = 0; // don't test against pinput->nWordsUsed, as we might be only reader enough for metadata only.
				skip = 0;
			}
			if((tag & 0xff00) == 0x2300) //uncompressed sample size
			{
				skip = 1;
			}
			if((tag & 0xff00) == 0x2100) //level
				skip = 0;


			if(chunksize)
			{
				if(chunksize*4 > pinput->nWordsUsed || chunksize < 0)
				{
					break;
				}

				if(skip)
				{
					//unsigned int *iptr =  (unsigned int *)pinput->lpCurrentWord;

					pinput->lpCurrentWord += chunksize*4;
					pinput->nWordsUsed -= chunksize*4;
				}

			}
		}
		else
		{
			error = 1;
		}
	} while(tag != CODEC_TAG_GROUP_TRAILER &&
			tag != CODEC_TAG_FRAME_TRAILER &&
			pinput->nWordsUsed>0 && !error);

	return ret;
}

/*!
	Copied from metadata.cpp in the cedoc common directory
*/
uint8_t *GetTupletAddr(uint8_t *data,
					   int datasize,
					   uint16_t findtag,
					   int16_t *retvalue)
{
	unsigned char *ret = NULL;
	BITSTREAM myinput, *pinput;
	TAGVALUE segment;
	TAGWORD tag,value;
	int error = 0;

	if (data == NULL || datasize == 0) {
		return NULL;
	}

	//InitBitstream(&myinput);
	memset(&myinput, 0, sizeof(BITSTREAM));
	myinput.lpCurrentWord = data;
	myinput.nWordsUsed = datasize;
	myinput.nBitsFree = BITSTREAM_LONG_SIZE;
	pinput = &myinput;

	do
	{
		//BOOL optional = FALSE;
		bool optional = false;
		int chunksize = 0;

		// Read the next tag value pair from the bitstream
		segment = GetSegment(pinput);

		tag = segment.tuple.tag;
		value = segment.tuple.value;


		// Is this an optional tag?
		if (tag < 0)
		{
			tag = NEG(tag);
			//optional = TRUE;
			optional = true;
		}

		if(tag & 0x2000)
		{
			chunksize = value;
			chunksize &= 0xffff;
			chunksize += ((tag&0xff)<<16);
		}
		else if(tag & 0x4000)
		{
			chunksize = value;
			chunksize &= 0xffff;
		}
		else if(tag == CODEC_TAG_INDEX)
		{
			chunksize = value;
			chunksize &= 0xffff;
		}
		else
		{
			chunksize = 0;
		}

		if((int)(tag) <= ((int)CODEC_TAG_LAST_NON_SIZED) || tag & 0x6000)
		{
			int skip = 1;
			error = 0;

			if(tag == (int)findtag)
			{
				*retvalue = value;
				ret = pinput->lpCurrentWord;
				break;
			}

			if((tag & 0xff00) == 0x2200) //sample size
			{
				chunksize = 0; // don't test against pinput->nWordsUsed, as we might be only reader enough for metadata only.
				skip = 0;
			}
			if((tag & 0xff00) == 0x2300) //uncompressed sample size
			{
				skip = 1;
			}
			if((tag & 0xff00) == 0x2100) //level
				skip = 0;


			if(chunksize)
			{
				if(chunksize*4 > pinput->nWordsUsed || chunksize < 0)
				{
					break;
				}

				if(skip)
				{
					//unsigned int *iptr =  (unsigned int *)pinput->lpCurrentWord;

					pinput->lpCurrentWord += chunksize*4;
					pinput->nWordsUsed -= chunksize*4;
				}

			}
		}
		else
		{
			error = 1;
		}
	} while(tag != CODEC_TAG_GROUP_TRAILER &&
			tag != CODEC_TAG_FRAME_TRAILER &&
			pinput->nWordsUsed>0 && !error);

	return ret;
}
