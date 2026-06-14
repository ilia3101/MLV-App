/*! @file filter.c

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

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif
#define TIMING (1 && _TIMING)
#define XMMOPT (1 && _XMMOPT)

#include <assert.h>
#include <limits.h>
#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif


#include "filter.h"			// Declarations of filter routines
#include "image.h"			// Image processing data types
#include "debug.h"
#include "codec.h"
#include "buffer.h"
#include "quantize.h"
#include "spatial.h"
#include "temporal.h"

// Apply the frame (temporal and horizontal) transform and quantize to unsigned bytes
// New version that processes data by rows to improve the memory access pattern
void FilterFrameRuns8u(PIXEL8U *frame, int frame_pitch,
					   PIXEL *lowlow_band, int lowlow_pitch,
					   PIXEL *lowhigh_band, int lowhigh_pitch,
					   PIXEL *highlow_band, int highlow_pitch,
					   PIXEL *highhigh_band, int highhigh_pitch,
					   ROI roi, int input_scale,
					   PIXEL *buffer, size_t buffer_size,
					   int offset,
					   int quantization[IMAGE_NUM_BANDS],
					   int num_runs[IMAGE_NUM_BANDS])
{
	PIXEL8U *even_row_ptr = frame;
	PIXEL8U *odd_row_ptr = even_row_ptr + frame_pitch/sizeof(PIXEL8U);
	PIXEL *lowlow_row_ptr = lowlow_band;
#if _HIGHPASS_8S
	PIXEL8S *lowhigh_row_ptr = (PIXEL8S *)lowhigh_band;
	PIXEL8S *highlow_row_ptr = (PIXEL8S *)highlow_band;
	PIXEL8S *highhigh_row_ptr = (PIXEL8S *)highhigh_band;
#else
	PIXEL *lowhigh_row_ptr = (PIXEL *)lowhigh_band;
	PIXEL *highlow_row_ptr = (PIXEL *)highlow_band;
	PIXEL *highhigh_row_ptr = (PIXEL *)highhigh_band;
#endif
	PIXEL *temporal_lowpass;
	PIXEL *temporal_highpass;
	PIXEL *lowhigh_row_buffer;
	PIXEL *highlow_row_buffer;
	PIXEL *highhigh_row_buffer;
	size_t temporal_buffer_size;
	size_t horizontal_buffer_size;
	char *bufptr;
	int field_pitch = 2 * frame_pitch;		// Offset in pixels between field rows
	int frame_width = roi.width;
	int half_width = frame_width/2;
	//int row_length = frame_width/2;
	int temporal_row_length;
	int horizontal_row_length;
	//int allocated_row_length;
	//int temporal_lowpass_scale = 2 * input_scale;
	//int temporal_highpass_scale = input_scale;
	//int lowlow_scale = 0;
	//int lowhigh_scale = 0;
	int highlow_scale = 0;
	int highhigh_scale = 0;

	//const int prescale = 0;

	int row;

	// Quantization factor for each band
	int lowlow_divisor;
	int lowhigh_divisor;
	int highlow_divisor;
	int highhigh_divisor;

	// Compute the quantization multipliers
	if (quantization != NULL) {
		lowlow_divisor = quantization[0];
		lowhigh_divisor = quantization[1];
		highlow_divisor = quantization[2];
		highhigh_divisor = quantization[3];
	}
	else {
		lowlow_divisor = 1;
		lowhigh_divisor = 1;
		highlow_divisor = 1;
		highhigh_divisor = 1;
	}

	// Convert pitch from bytes to pixels
	field_pitch /= sizeof(PIXEL8U);
	lowlow_pitch /= sizeof(PIXEL);
#if _HIGHPASS_8S
	lowhigh_pitch /= sizeof(PIXEL8S);
	highlow_pitch /= sizeof(PIXEL8S);
	highhigh_pitch /= sizeof(PIXEL8S);
#else
	lowhigh_pitch /= sizeof(PIXEL);
	highlow_pitch /= sizeof(PIXEL);
	highhigh_pitch /= sizeof(PIXEL);
#endif

	// Round up the frame width to a multiple of 16 bytes
	temporal_row_length = ALIGN16(frame_width);

	// Compute the length of each temporal buffer (in bytes)
	temporal_buffer_size = temporal_row_length * sizeof(PIXEL);

	// Round up the temporal buffer size to an integer number of cache lines
	temporal_buffer_size = ALIGN(temporal_buffer_size, _CACHE_LINE_SIZE);

	// Round up the highpass output row length to 16 bytes
	horizontal_row_length = ALIGN16(half_width);

	// Compute the length of each output buffer (in bytes)
	horizontal_buffer_size = horizontal_row_length * sizeof(PIXEL);

	// Round up the output buffer size to an integer number of cache lines
	horizontal_buffer_size = ALIGN(horizontal_buffer_size, _CACHE_LINE_SIZE);

	// Check that the buffer is large enough for three rows
	assert(buffer_size >= (2 * temporal_buffer_size + 3 * horizontal_buffer_size));

	// Track the buffer allocations
	bufptr = (char *)buffer;

	// Allocate buffers for two rows of temporal coefficients
	temporal_lowpass = (PIXEL *)bufptr;		bufptr += temporal_buffer_size;
	temporal_highpass = (PIXEL *)bufptr;	bufptr += temporal_buffer_size;

	// Allocate buffers for three rows of wavelet coefficients
	lowhigh_row_buffer = (PIXEL *)bufptr;	bufptr += horizontal_buffer_size;
	highlow_row_buffer = (PIXEL *)bufptr;	bufptr += horizontal_buffer_size;
	highhigh_row_buffer = (PIXEL *)bufptr;	bufptr += horizontal_buffer_size;

	// Apply the temporal transform to the even and odd rows each iteration of the loop
	for (row = 0; row < roi.height; row += 2)
	{
		PIXEL *lowlow_ptr;			// Pointer into the lowlow band
		PIXEL *lowhigh_ptr;			// Pointer into the lowhigh band
		PIXEL *highlow_ptr;			// Pointer into the highlow band
		PIXEL *highhigh_ptr;		// Pointer into the highhigh band

		// Initialize pointers into the four output rows
		lowlow_ptr = lowlow_row_ptr;
		lowhigh_ptr = lowhigh_row_buffer;
		highlow_ptr = highlow_row_buffer;
		highhigh_ptr = highhigh_row_buffer;


		// Each pass through the loop applies the horizontal lowpass filter to the
		// temporal lowpass and highpass results producing results for the current
		// column and applies the horizontal highpass filter to the temporal results
		// producing results for the second and later columns due to border effects.

		// Apply the temporal transform to the even and odd rows
		FilterTemporalRow8uTo16s(even_row_ptr, odd_row_ptr, temporal_row_length,
								 temporal_lowpass, temporal_highpass, offset);

		
		//DAN20051004 -- possible reversibility issue
		//FilterHorizontalRowScaled16s(temporal_lowpass, lowlow_row_ptr, lowhigh_row_buffer,
		//							 frame_width, lowlow_scale, lowhigh_scale);
		// Apply the horizontal transform to the temporal lowpass
		//DAN20051004 -- fixed
		FilterHorizontalRow16s(temporal_lowpass, lowlow_row_ptr, lowhigh_row_buffer, frame_width);


		// Quantize and pack the rows of highpass coefficients
		#if _HIGHPASS_8S
		QuantizeRow16sTo8s(lowhigh_row_buffer, lowhigh_row_ptr, half_width, lowhigh_divisor);
		#else
		QuantizeRow16sTo16s(lowhigh_row_buffer, lowhigh_row_ptr, half_width, lowhigh_divisor);
		#endif


		// Apply the horizontal transform to the temporal highpass
		#if DIFFERENCE_CODING // for interlaced data use the new differencing transfrom
		{// test DifferenceFiltering opf the interlace LH band.
			//FilterHorizontalRowScaled16s(temporal_highpass, highlow_row_buffer, highhigh_row_buffer,
			//						 frame_width, highlow_scale, highhigh_scale);
			//high low is quantized as part of the differencing (quantization neded to occur befopre differencing.)
			FilterHorizontalRowScaled16sDifferenceFiltered(temporal_highpass, highlow_row_ptr, highhigh_row_buffer,
									 frame_width, highlow_scale, highhigh_scale,  highlow_divisor);

			// Quantize and pack the rows of highpass coefficients
			#if _HIGHPASS_8S
			QuantizeRow16sTo8s(highhigh_row_buffer, highhigh_row_ptr, half_width, highhigh_divisor);
			#else
			QuantizeRow16sTo16s(highhigh_row_buffer, highhigh_row_ptr, half_width, highhigh_divisor);
			#endif
		}
		#else
		{
			//DAN20051004 -- possible reversibility issue
			// Apply the horizontal transform to the temporal highpass
			//FilterHorizontalRowScaled16s(temporal_highpass, highlow_row_buffer, highhigh_row_buffer,
			//						 frame_width, highlow_scale, highhigh_scale);
			//DAN20051004 -- fixed
			FilterHorizontalRow16s(temporal_highpass, highlow_row_buffer, highhigh_row_buffer, frame_width);


			// Quantize and pack the rows of highpass coefficients
			#if _HIGHPASS_8S
			QuantizeRow16sTo8s(highlow_row_buffer, highlow_row_ptr, half_width, highlow_divisor);
			QuantizeRow16sTo8s(highhigh_row_buffer, highhigh_row_ptr, half_width, highhigh_divisor);
			#else
			QuantizeRow16sTo16s(highlow_row_buffer, highlow_row_ptr, half_width, highlow_divisor);
			QuantizeRow16sTo16s(highhigh_row_buffer, highhigh_row_ptr, half_width, highhigh_divisor);
			#endif
		}
		#endif
/*		
		// Apply the horizontal transform to the temporal lowpass
		FilterHorizontalRowScaled16s(temporal_lowpass, lowlow_row_ptr, lowhigh_row_buffer,
									 frame_width, lowlow_scale, lowhigh_scale);

		// Apply the horizontal transform to the temporal highpass
		FilterHorizontalRowScaled16s(temporal_highpass, highlow_row_buffer, highhigh_row_buffer,
									 frame_width, highlow_scale, highhigh_scale);

		// Quantize and pack the rows of highpass coefficients
#if _HIGHPASS_8S
		QuantizeRow16sTo8s(lowhigh_row_buffer, lowhigh_row_ptr, horizontal_row_length, lowhigh_divisor);
		QuantizeRow16sTo8s(highlow_row_buffer, highlow_row_ptr, horizontal_row_length, highlow_divisor);
		QuantizeRow16sTo8s(highhigh_row_buffer, highhigh_row_ptr, horizontal_row_length, highhigh_divisor);
#else
		QuantizeRow16sTo16s(lowhigh_row_buffer, lowhigh_row_ptr, horizontal_row_length, lowhigh_divisor);
		QuantizeRow16sTo16s(highlow_row_buffer, highlow_row_ptr, horizontal_row_length, highlow_divisor);
		QuantizeRow16sTo16s(highhigh_row_buffer, highhigh_row_ptr, horizontal_row_length, highhigh_divisor);
#endif
*/
		// Advance to the next row in each highpass band
		lowhigh_row_ptr += lowhigh_pitch;
		highlow_row_ptr += highlow_pitch;
		highhigh_row_ptr += highhigh_pitch;

		// Advance to the next row in each input field
		even_row_ptr += field_pitch;
		odd_row_ptr += field_pitch;

		// Advance to the next row in the lowpass band
		lowlow_row_ptr += lowlow_pitch;
	}
}

// Apply the frame (temporal and horizontal) transform and quantize the highpass bands
void FilterFrameQuant16s(PIXEL *frame, int frame_pitch,
						 PIXEL *lowlow_band, int lowlow_pitch,
						 PIXEL *lowhigh_band, int lowhigh_pitch,
						 PIXEL *highlow_band, int highlow_pitch,
						 PIXEL *highhigh_band, int highhigh_pitch,
						 ROI roi, int input_scale,
						 PIXEL *buffer, size_t buffer_size,
						 int offset,
						 int quantization[IMAGE_NUM_BANDS])
{
	PIXEL *even_row_ptr = frame;
	PIXEL *odd_row_ptr = even_row_ptr + frame_pitch/sizeof(PIXEL);
	PIXEL *lowlow_row_ptr = lowlow_band;
	PIXEL *lowhigh_row_ptr = lowhigh_band;
	PIXEL *highlow_row_ptr = highlow_band;
	PIXEL *highhigh_row_ptr = highhigh_band;
	PIXEL *temporal_lowpass;
	PIXEL *temporal_highpass;
	PIXEL *lowhigh_row_buffer;
	PIXEL *highlow_row_buffer;
	PIXEL *highhigh_row_buffer;
	size_t temporal_buffer_size;
	size_t horizontal_buffer_size;
	char *bufptr;
	int field_pitch = 2 * frame_pitch;		// Offset in pixels between field rows
	int frame_width = roi.width;
	int half_width = frame_width/2;
	//int row_length = frame_width/2;
	int temporal_row_length;
	int horizontal_row_length;
	//int allocated_row_length;
	//int temporal_lowpass_scale = 2 * input_scale;
	//int temporal_highpass_scale = input_scale;
	//int lowlow_scale = 0;
	//int lowhigh_scale = 0;
	int highlow_scale = 0;
	int highhigh_scale = 0;

	//const int prescale = 0;

	int row;

	// Quantization factor for each band
	int lowlow_divisor;
	int lowhigh_divisor;
	int highlow_divisor;
	int highhigh_divisor;

	// Compute the quantization multipliers
	if (quantization != NULL) {
		lowlow_divisor = quantization[0];
		lowhigh_divisor = quantization[1];
		highlow_divisor = quantization[2];
		highhigh_divisor = quantization[3];
	}
	else {
		lowlow_divisor = 1;
		lowhigh_divisor = 1;
		highlow_divisor = 1;
		highhigh_divisor = 1;
	}

	// Convert pitch from bytes to pixels
	field_pitch /= sizeof(PIXEL);
	lowlow_pitch /= sizeof(PIXEL);
	lowhigh_pitch /= sizeof(PIXEL);
	highlow_pitch /= sizeof(PIXEL);
	highhigh_pitch /= sizeof(PIXEL);

	// Round up the frame width to a multiple of 16 bytes
	temporal_row_length = ALIGN16(frame_width);

	// Compute the length of each temporal buffer (in bytes)
	temporal_buffer_size = temporal_row_length * sizeof(PIXEL);

	// Round up the temporal buffer size to an integer number of cache lines
	temporal_buffer_size = ALIGN(temporal_buffer_size, _CACHE_LINE_SIZE);

	// Round up the highpass output row length to 16 bytes
	horizontal_row_length = ALIGN16(half_width);

	// Compute the length of each output buffer (in bytes)
	horizontal_buffer_size = horizontal_row_length * sizeof(PIXEL);

	// Round up the output buffer size to an integer number of cache lines
	horizontal_buffer_size = ALIGN(horizontal_buffer_size, _CACHE_LINE_SIZE);

	// Check that the buffer is large enough for three rows
	assert(buffer_size >= (2 * temporal_buffer_size + 3 * horizontal_buffer_size));

	// Track the buffer allocations
	bufptr = (char *)buffer;

	// Allocate buffers for two rows of temporal coefficients
	temporal_lowpass = (PIXEL *)bufptr;		bufptr += temporal_buffer_size;
	temporal_highpass = (PIXEL *)bufptr;	bufptr += temporal_buffer_size;

	// Allocate buffers for three rows of wavelet coefficients
	lowhigh_row_buffer = (PIXEL *)bufptr;	bufptr += horizontal_buffer_size;
	highlow_row_buffer = (PIXEL *)bufptr;	bufptr += horizontal_buffer_size;
	highhigh_row_buffer = (PIXEL *)bufptr;	bufptr += horizontal_buffer_size;

	// Apply the temporal transform to the even and odd rows each iteration of the loop
	for (row = 0; row < roi.height; row += 2)
	{
		PIXEL *lowlow_ptr;			// Pointer into the lowlow band
		PIXEL *lowhigh_ptr;			// Pointer into the lowhigh band
		PIXEL *highlow_ptr;			// Pointer into the highlow band
		PIXEL *highhigh_ptr;		// Pointer into the highhigh band

		// Initialize pointers into the four output rows
		lowlow_ptr = lowlow_row_ptr;
		lowhigh_ptr = lowhigh_row_buffer;
		highlow_ptr = highlow_row_buffer;
		highhigh_ptr = highhigh_row_buffer;

		// Each pass through the loop applies the horizontal lowpass filter to the
		// temporal lowpass and highpass results producing results for the current
		// column and applies the horizontal highpass filter to the temporal results
		// producing results for the second and later columns due to border effects.

		// Apply the temporal transform to the even and odd rows
		FilterTemporalRow16s(even_row_ptr, odd_row_ptr, temporal_row_length,
							 temporal_lowpass, temporal_highpass, offset);

//#if !BUILD_PROSPECT
/*		// Apply the horizontal transform to the temporal lowpass
		FilterHorizontalRowScaled16s(temporal_lowpass, lowlow_row_ptr, lowhigh_row_buffer,
									 frame_width, lowlow_scale, lowhigh_scale);

		// Quantize the rows of highpass coefficients
		QuantizeRow16sTo16s(lowhigh_row_buffer, lowhigh_row_ptr, horizontal_row_length, lowhigh_divisor);
		*/
//#else //10-bit for everyone
		//SSE2 only 
		// Apply the horizontal transform to the temporal highpass and quantize
		FilterHorizontalRowQuant16s(temporal_lowpass, lowlow_row_ptr, lowhigh_row_ptr,
									 frame_width, lowlow_divisor, lowhigh_divisor);

//#endif

		// Apply the horizontal transform to the temporal highpass
		#if DIFFERENCE_CODING // for interlaced data use the new differencing transfrom
		{// test DifferenceFiltering opf the interlace LH band.
			//FilterHorizontalRowScaled16s(temporal_highpass, highlow_row_buffer, highhigh_row_buffer,
			//						 frame_width, highlow_scale, highhigh_scale);
			//high low is quantized as part of the differencing (quantization neded to occur befopre differencing.)
			FilterHorizontalRowScaled16sDifferenceFiltered(temporal_highpass, highlow_row_ptr, highhigh_row_buffer,
									 frame_width, highlow_scale, highhigh_scale,  highlow_divisor);

			// Quantize and pack the rows of highpass coefficients
			QuantizeRow16sTo16s(highhigh_row_buffer, highhigh_row_ptr, horizontal_row_length, highhigh_divisor);
		}
		#else
		{
			// Apply the horizontal transform to the temporal highpass
			FilterHorizontalRowScaled16s(temporal_highpass, highlow_row_buffer, highhigh_row_buffer,
									 frame_width, highlow_scale, highhigh_scale);

			QuantizeRow16sTo16s(highlow_row_buffer, highlow_row_ptr, horizontal_row_length, highlow_divisor);
			QuantizeRow16sTo16s(highhigh_row_buffer, highhigh_row_ptr, horizontal_row_length, highhigh_divisor);

		}
		#endif


		// Advance to the next row in each highpass band
		lowhigh_row_ptr += lowhigh_pitch;
		highlow_row_ptr += highlow_pitch;
		highhigh_row_ptr += highhigh_pitch;

		// Advance to the next row in each input field
		even_row_ptr += field_pitch;
		odd_row_ptr += field_pitch;

		// Advance to the next row in the lowpass band
		lowlow_row_ptr += lowlow_pitch;
	}
}


#if 0

// Need to finish this routine -- have not even changed the argument list

// May not need this routine -- use TransformForwardYUVToFrame

// Apply the frame (temporal and horizontal) transform and quantize to unsigned bytes
// New version that can process a frame of packed YUV pixels
void NewFilterFrameQuantYUV(BYTE *frame, int frame_pitch,
							PIXEL *lowlow_band, int lowlow_pitch,
							PIXEL *lowhigh_band, int lowhigh_pitch,
							PIXEL *highlow_band, int highlow_pitch,
							PIXEL *highhigh_band, int highhigh_pitch,
							ROI roi, int input_scale,
							PIXEL *buffer, size_t buffer_size,
							int quantization[IMAGE_NUM_BANDS],
							int num_runs[IMAGE_NUM_BANDS])
{
	PIXEL8U *even_row_ptr = frame;
	PIXEL8U *odd_row_ptr = even_row_ptr + frame_pitch/sizeof(PIXEL8U);
	PIXEL *lowlow_row_ptr = lowlow_band;
	PIXEL8S *lowhigh_row_ptr = (PIXEL8S *)lowhigh_band;
	PIXEL8S *highlow_row_ptr = (PIXEL8S *)highlow_band;
	PIXEL8S *highhigh_row_ptr = (PIXEL8S *)highhigh_band;
	PIXEL *temporal_lowpass;
	PIXEL *temporal_highpass;
	PIXEL *lowhigh_row_buffer;
	PIXEL *highlow_row_buffer;
	PIXEL *highhigh_row_buffer;
	size_t temporal_buffer_size;
	size_t output_buffer_size;
	char *bufptr;
	int field_pitch = 2 * frame_pitch;		// Offset in pixels between field rows
	int frame_width = roi.width;
	int row_length = frame_width/2;
	//int allocated_row_length;
	int temporal_lowpass_scale = 2 * input_scale;
	int temporal_highpass_scale = input_scale;
	int lowlow_scale = 0;
	int lowhigh_scale = 0;
	int highlow_scale = 0;
	int highhigh_scale = 0;
	//const int prescale = 0;

	int row;

	// Quantization factor for each band
	int lowlow_divisor;
	int lowhigh_divisor;
	int highlow_divisor;
	int highhigh_divisor;

	// Compute the quantization multipliers
	if (quantization != NULL) {
		lowlow_divisor = quantization[0];
		lowhigh_divisor = quantization[1];
		highlow_divisor = quantization[2];
		highhigh_divisor = quantization[3];
	}
	else {
		lowlow_divisor = 1;
		lowhigh_divisor = 1;
		highlow_divisor = 1;
		highhigh_divisor = 1;
	}

	// Convert pitch from bytes to pixels
	field_pitch /= sizeof(PIXEL8U);
	lowlow_pitch /= sizeof(PIXEL);
	lowhigh_pitch /= sizeof(PIXEL8S);
	highlow_pitch /= sizeof(PIXEL8S);
	highhigh_pitch /= sizeof(PIXEL8S);

	// Round up the frame width to a multiple of 16 bytes
	frame_width = ALIGN16(frame_width);

	// Compute the length of each temporal buffer (in bytes)
	temporal_buffer_size = frame_width * sizeof(PIXEL);

	// Round up the temporal buffer size to an integer number of cache lines
	temporal_buffer_size = ALIGN(temporal_buffer_size, _CACHE_LINE_SIZE);

	// Round up the highpass output row length to 16 bytes
	row_length = ALIGN16(row_length);

	// Compute the length of each output buffer (in bytes)
	output_buffer_size = row_length * sizeof(PIXEL);

	// Round up the output buffer size to an integer number of cache lines
	output_buffer_size = ALIGN(output_buffer_size, _CACHE_LINE_SIZE);

	// Check that the buffer is large enough for three rows
	assert(buffer_size >= (2 * temporal_buffer_size + 3 * output_buffer_size));

	// Track the buffer allocations
	bufptr = (char *)buffer;

	// Allocate buffers for two rows of temporal coefficients
	temporal_lowpass = (PIXEL *)bufptr;		bufptr += temporal_buffer_size;
	temporal_highpass = (PIXEL *)bufptr;	bufptr += temporal_buffer_size;

	// Allocate buffers for three rows of wavelet coefficients
	lowhigh_row_buffer = (PIXEL *)bufptr;	bufptr += output_buffer_size;
	highlow_row_buffer = (PIXEL *)bufptr;	bufptr += output_buffer_size;
	highhigh_row_buffer = (PIXEL *)bufptr;	bufptr += output_buffer_size;

	// Apply the temporal transform to the even and odd rows each iteration of the loop
	for (row = 0; row < roi.height; row += 2)
	{
		PIXEL *lowlow_ptr;			// Pointer into the lowlow band
		PIXEL *lowhigh_ptr;			// Pointer into the lowhigh band
		PIXEL *highlow_ptr;			// Pointer into the highlow band
		PIXEL *highhigh_ptr;		// Pointer into the highhigh band

		// Initialize pointers into the four output rows
		lowlow_ptr = lowlow_row_ptr;
		lowhigh_ptr = lowhigh_row_buffer;
		highlow_ptr = highlow_row_buffer;
		highhigh_ptr = highhigh_row_buffer;

		// Each pass through the loop applies the horizontal lowpass filter to the
		// temporal lowpass and highpass results producing results for the current
		// column and applies the horizontal highpass filter to the temporal results
		// producing results for the second and later columns due to border effects.

		// Apply the temporal transform to the even and odd rows
		FilterTemporalRow8uTo16s(even_row_ptr, odd_row_ptr, frame_width, temporal_lowpass, temporal_highpass);

		// Apply the horizontal transform to the temporal lowpass
		FilterHorizontalRowScaled16s(temporal_lowpass, lowlow_row_ptr, lowhigh_row_buffer,
									 frame_width, lowlow_scale, lowhigh_scale);

		// Apply the horizontal transform to the temporal highpass
		FilterHorizontalRowScaled16s(temporal_highpass, highlow_row_buffer, highhigh_row_buffer,
									 frame_width, highlow_scale, highhigh_scale);

		// Quantize and pack the rows of highpass coefficients
		QuantizeRow16sTo8s(lowhigh_row_buffer, lowhigh_row_ptr, row_length, lowhigh_divisor);
		QuantizeRow16sTo8s(highlow_row_buffer, highlow_row_ptr, row_length, highlow_divisor);
		QuantizeRow16sTo8s(highhigh_row_buffer, highhigh_row_ptr, row_length, highhigh_divisor);

		// Advance to the next row in each highpass band
		lowhigh_row_ptr += lowhigh_pitch;
		highlow_row_ptr += highlow_pitch;
		highhigh_row_ptr += highhigh_pitch;

		// Advance to the next row in each input field
		even_row_ptr += field_pitch;
		odd_row_ptr += field_pitch;

		// Advance to the next row in the lowpass band
		lowlow_row_ptr += lowlow_pitch;
	}
}

#endif

#if 0

// This routine is not longer used since the inverse frame transform
// has been optimized to perform color conversion to YUV as well

void InvertFrameTo8u(PIXEL *lowlow_band, int lowlow_pitch,
					 PIXEL *lowhigh_band, int lowhigh_pitch,
					 PIXEL *highlow_band, int highlow_pitch,
					 PIXEL *highhigh_band, int highhigh_pitch,
					 PIXEL8U *frame, int frame_pitch, PIXEL *buffer, ROI roi)
{
	PIXEL8U *even_field = frame;
	PIXEL8U *odd_field = even_field + frame_pitch/sizeof(PIXEL8U);
	PIXEL *lowpass = buffer;
	PIXEL *highpass = buffer + (roi.height * frame_pitch);
	int field_pitch = 2 * frame_pitch;
	//BOOL fastmode = TRUE;

#if _HIGHPASS_8S

	// Apply the inverse horizontal filter to reconstruct the temporal lowpass band
	InvertHorizontalMixedTo16s(lowlow_band, lowlow_pitch, lowhigh_band, lowhigh_pitch,
							   lowpass, field_pitch, roi);
							   //even_band, field_pitch, roi, fastmode);

	// Apply the inverse horizontal filter to reconstruct the temporal highpass band
	InvertHorizontal8s(highlow_band, highlow_pitch, highhigh_band, highhigh_pitch,
					   highpass, field_pitch, roi);
					   //odd_band, field_pitch, roi, fastmode);

#else
#error Frame transform inverse has not been implemented for 16-bit coefficients
#endif

	// Apply the inverse temporal transform to reconstruct two fields
	InvertTemporalTo8u(lowpass, frame_pitch, highpass, frame_pitch,
					   even_field, field_pitch, odd_field, field_pitch, roi);
}

#endif

void FilterHorizontalDelta(PIXEL *data, int width, int height, int pitch)
{
	PIXEL *rowptr = data;
	int row;

	return;

	pitch /= sizeof(PIXEL);

	for (row = 0; row < height; row++)
	//for (row = 0; row < 8; row++)
	{
		PIXEL previous = rowptr[0];
		int column = 1;

		for (; column < width; column++)
		{
			PIXEL delta = rowptr[column] - previous;
			previous = rowptr[column];
			rowptr[column] = delta;
		}

		rowptr += pitch;
	}
}

double BandEnergy(PIXEL *data, int width, int height, int pitch, int band, int subband)
{
	PIXEL *rowptr = data;
	double sumsqr = 0.0;
	int row;
	int valuemax = 0;

	pitch /= sizeof(PIXEL);

	for (row = 0; row < height; row++)
	{
		//PIXEL previous = rowptr[0];
		int column = 1;

		for (; column < width; column++)
		{
			double value = rowptr[column];
			if(abs(rowptr[column]) > valuemax) 
				valuemax = abs(rowptr[column]);
			sumsqr += value * value;
		}

		rowptr += pitch;
	}

#if (0 && _WIN32)
	{
		char t[100];
		sprintf(t,"band = %d, subband = %d, valuemax = %d, rms = %f", 
			band, subband, valuemax, sqrt(sumsqr/(double)(width*height)));
		OutputDebugString(t);
	}
#endif

	return sumsqr;
}
