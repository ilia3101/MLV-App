/*! @file temporal.c

*  @brief 3D Wavelet tools
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
#define MMXOPT (1 && _XMMOPT)

#include <assert.h>
#include <limits.h>
#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif

#include "temporal.h"
#include "filter.h"			// Declarations of filter routines
//#include "image.h"		// Image processing data types
//#include "ipp.h"			// Use Intel Performance Primitives
//#include "debug.h"
#include "codec.h"
//#include "buffer.h"
#include "quantize.h"
#include "convert.h"
#include "decoder.h"

// Control whether the code in this file uses prefetching
#define PREFETCH (1 && _PREFETCH)

#ifndef STRICT_SATURATE
#define STRICT_SATURATE			0
#endif

#ifndef _TEMPORAL_TWO_ROWS
#define _TEMPORAL_TWO_ROWS		1
#endif

#ifndef _SWAP_CHROMA
#define _SWAP_CHROMA	1		// Chroma channels must be swapped
#endif

#if TIMING
extern TIMER tk_inverse;
#endif


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void FilterTemporal(PIXEL *field1, int pitch1, PIXEL *field2, int pitch2,
					PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					ROI roi)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Apply the temporal transform between two images producing 16-bit coefficients
void FilterTemporal(PIXEL *field1, int pitch1, PIXEL *field2, int pitch2,
					PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					ROI roi)
{
	int column_step = 8;
	int post_column = roi.width - (roi.width % column_step);
	int row, column;

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 16) == 0);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
		__m64 *input1_ptr = (__m64 *)field1;
		__m64 *input2_ptr = (__m64 *)field2;
		__m64 *low_ptr = (__m64 *)lowpass;
		__m64 *high_ptr = (__m64 *)highpass;

		__m64 temp1_pi16;
		__m64 temp2_pi16;
		__m64 diff1_pi16;
		__m64 diff2_pi16;

		__m64 input1_pi16;
		__m64 input2_pi16;

		column = 0;

#if (1 && XMMOPT)

		// Preload the input values (which may be overwritten)
		input1_pi16 = *(input1_ptr++);
		input2_pi16 = *(input2_ptr++);

		for (; column < post_column; column += column_step)
		{
			__m64 input3_pi16;
			__m64 input4_pi16;

			// Preload the next set of input values
			input3_pi16 = *(input1_ptr++);
			input4_pi16 = *(input2_ptr++);

			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(low_ptr++) = temp1_pi16;

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			*(high_ptr++) = diff1_pi16;

			// Preload the next set of input values
			input1_pi16 = *(input1_ptr++);
			input2_pi16 = *(input2_ptr++);

			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input3_pi16, input4_pi16);
			*(low_ptr++) = temp1_pi16;

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input4_pi16, input3_pi16);

			// Store the differences between the pixels in each row
			*(high_ptr++) = diff1_pi16;
		}

		// Should have exited the loop at the post processing column
		assert(column == post_column);

		//_mm_empty();		// Clear the mmx register state
#endif

		// Process the rest of the row
		for (; column < roi.width; column++)
		{
			int value1 = field1[column];
			int value2 = field2[column];

			int temp = value1 + value2;
			int diff = value2 - value1;

			lowpass[column] = SATURATE(temp);
			highpass[column] = SATURATE(diff);
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Apply the temporal transform between two images producing 16-bit coefficients
void FilterTemporal(PIXEL *field1, int pitch1, PIXEL *field2, int pitch2,
					PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					ROI roi)
{
	int column_step = 16;
	int post_column = roi.width - (roi.width % column_step);
	int row, column;

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 16) == 0);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
		__m128i *input1_ptr = (__m128i *)field1;
		__m128i *input2_ptr = (__m128i *)field2;
		__m128i *low_ptr = (__m128i *)lowpass;
		__m128i *high_ptr = (__m128i *)highpass;

		__m128i temp1_epi16;
		__m128i diff1_epi16;

		__m128i input1_epi16;
		__m128i input2_epi16;

		column = 0;

#if (1 && XMMOPT)

		// Check that the pointers to the groups of pixels are properly aligned
		assert(ISALIGNED16(input1_ptr));
		assert(ISALIGNED16(input2_ptr));

		// Check that the output pointers are properly aligned
		assert(ISALIGNED16(low_ptr));
		assert(ISALIGNED16(high_ptr));

		// Preload the input values (which may be overwritten)
		input1_epi16 = _mm_load_si128(input1_ptr++);
		input2_epi16 = _mm_load_si128(input2_ptr++);

		for (; column < post_column; column += column_step)
		{
			__m128i input3_epi16;
			__m128i input4_epi16;

			// Preload the next set of input values
			input3_epi16 = _mm_load_si128(input1_ptr++);
			input4_epi16 = _mm_load_si128(input2_ptr++);

			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(low_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(high_ptr++, diff1_epi16);

			// Preload the next set of input values
			input1_epi16 = _mm_load_si128(input1_ptr++);
			input2_epi16 = _mm_load_si128(input2_ptr++);

			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input3_epi16, input4_epi16);
			_mm_store_si128(low_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input4_epi16, input3_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(high_ptr++, diff1_epi16);
		}

		// Should have exited the loop at the post processing column
		assert(column == post_column);
#endif

		// Process the rest of the row
		for (; column < roi.width; column++)
		{
			int value1 = field1[column];
			int value2 = field2[column];

			int temp = value1 + value2;
			int diff = value2 - value1;

			lowpass[column] = SATURATE(temp);
			highpass[column] = SATURATE(diff);
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}
}

#endif


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void FilterTemporal16s(PIXEL *field1, int pitch1, PIXEL *field2, int pitch2,
					   PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					   ROI roi)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Apply the temporal transform between two images of 16-bit signed pixels
void FilterTemporal16s(PIXEL *field1, int pitch1, PIXEL *field2, int pitch2,
					   PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					   ROI roi)
{
#if _HIGHPASS_8S

	int column_step = 8;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins
	int row, column;
	PIXEL8S *highrow;

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 8) == 0);

	// Compute the column where end of row processing must begin
	post_column = roi.width - (roi.width % column_step);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
		__m64 *input1_ptr = (__m64 *)field1;
		__m64 *input2_ptr = (__m64 *)field2;
		__m64 *low_ptr = (__m64 *)lowpass;
		__m64 *high_ptr = (__m64 *)highpass;

		column = 0;

#if (1 && XMMOPT)

		// Process column elements in parallel until end of row processing is required
		for (; column < post_column; column += column_step)
		{
			__m64 input1_pi16;		// Four input pixels from the first row
			__m64 input2_pi16;		// Four input pixels from the second row
			__m64 temp1_pi16;		// Four temporal lowpass coefficients
			__m64 diff1_pi16;		// Four temporal highpass coefficients
			__m64 temp2_pi16;		// Four temporal lowpass coefficients
			__m64 diff2_pi16;		// Four temporal highpass coefficients
			__m64 high_pi8;			// Eight signed bytes of highpass results

			// Check that the pointers to the next groups of pixels are properly aligned
			//assert(ISALIGNED16(input1_ptr));
			//assert(ISALIGNED16(input2_ptr));

			// Load the input values before they are overwritten
			input1_pi16 = *(input1_ptr++);
			input2_pi16 = *(input2_ptr++);

			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(low_ptr++) = temp1_pi16;

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Check that the pointers to the next groups of pixels are properly aligned
			//assert(ISALIGNED16(input1_ptr));
			//assert(ISALIGNED16(input2_ptr));

			// Load the input values before they are overwritten
			input1_pi16 = *(input1_ptr++);
			input2_pi16 = *(input2_ptr++);

			// Compute and store the sum of the pixels in each row
			temp2_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(low_ptr++) = temp2_pi16;

			// Compute the differences between the pixels in each row
			diff2_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Pack the highpass values from each stage of the loop
			high_pi8 = _mm_packs_pi16(diff1_pi16, diff2_pi16);

			// Store eight signed bytes of highpass results
			*(high_ptr++) = high_pi8;
		}

		// Should have left the loop at the post processing column
		assert(column == post_column);
#endif

		// Handle end of row processing for the remaining columns
		highrow = (PIXEL8S *)highpass;
		for (; column < roi.width; column++) {
			int value1 = field1[column];
			int value2 = field2[column];

			int temp = value1 + value2;
			int diff = value2 - value1;

			lowpass[column] = SATURATE_16S(temp);
			highrow[column] = SATURATE_8S(diff);
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}

#else

	int row, column;

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of four word blocks
	assert((roi.width % 4) == 0);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
		__m64 *input1_ptr = (__m64 *)field1;
		__m64 *input2_ptr = (__m64 *)field2;
		__m64 *low_ptr = (__m64 *)lowpass;
		__m64 *high_ptr = (__m64 *)highpass;

		int column_step = 4;

		for (column = 0; column < roi.width; column += column_step)
		{
			__m64 input1_pi16;
			__m64 input2_pi16;

			// Check that the pointers to the next groups of pixels are properly aligned
			//assert(ISALIGNED16(input1_ptr));
			//assert(ISALIGNED16(input2_ptr));

			// Load the input values which may be overwritten
			input1_pi16 = *(input1_ptr++);
			input2_pi16 = *(input2_ptr++);

			// Compute the sum and differences of the pixels in each row
			*(low_ptr++) = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(high_ptr++) = _mm_subs_pi16(input2_pi16, input1_pi16);
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}

#endif

	//_mm_empty();	// Clear the mmx register state
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Apply the temporal transform between two images of 16-bit signed pixels
void FilterTemporal16s(PIXEL *field1, int pitch1, PIXEL *field2, int pitch2,
					   PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					   ROI roi)
{
#if _HIGHPASS_8S

	int column_step = 16;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins
	int row, column;
	PIXEL8S *highrow;

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 16) == 0);

	// Compute the column where end of row processing must begin
	post_column = roi.width - (roi.width % column_step);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
		__m128i *input1_ptr = (__m128i *)field1;
		__m128i *input2_ptr = (__m128i *)field2;
		__m128i *low_ptr = (__m128i *)lowpass;
		__m128i *high_ptr = (__m128i *)highpass;

		column = 0;

#if (1 && XMMOPT)

		// Process column elements in parallel until end of row processing is required
		for (; column < post_column; column += column_step)
		{
			__m128i input1_epi16;		// Eight input pixels from the first row
			__m128i input2_epi16;		// Eight input pixels from the second row
			__m128i temp1_epi16;		// Eight temporal lowpass coefficients
			__m128i diff1_epi16;		// Eight temporal highpass coefficients
			__m128i temp2_epi16;		// Eight temporal lowpass coefficients
			__m128i diff2_epi16;		// Eight temporal highpass coefficients
			__m128i high_epi8;			// Sixteen signed bytes of highpass results


			// Check that the pointers to the next groups of pixels are properly aligned
			assert(ISALIGNED16(input1_ptr));
			assert(ISALIGNED16(input2_ptr));

			// Load the input values before they are overwritten
			input1_epi16 = _mm_load_si128(input1_ptr++);
			input2_epi16 = _mm_load_si128(input2_ptr++);

			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(low_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Check that the pointers to the next groups of pixels are properly aligned
			assert(ISALIGNED16(input1_ptr));
			assert(ISALIGNED16(input2_ptr));

			// Load the input values before they are overwritten
			input1_epi16 = _mm_load_si128(input1_ptr++);
			input2_epi16 = _mm_load_si128(input2_ptr++);

			// Compute and store the sum of the pixels in each row
			temp2_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(low_ptr++, temp2_epi16);

			// Compute the differences between the pixels in each row
			diff2_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Pack the highpass values from each stage of the loop
			high_epi8 = _mm_packs_epi16(diff1_epi16, diff2_epi16);

			// Save sixteen signed bytes of highpass results
			_mm_store_si128(high_ptr++, high_epi8);
		}

		// Should have left the loop at the post processing column
		assert(column == post_column);
#endif

		// Handle end of row processing for the remaining columns
		highrow = (PIXEL8S *)highpass;
		for (; column < roi.width; column++) {
			int value1 = field1[column];
			int value2 = field2[column];

			int temp = value1 + value2;
			int diff = value2 - value1;

			lowpass[column] = SATURATE_16S(temp);
			highrow[column] = SATURATE_8S(diff);
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}

#else

	int row, column;

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of eight word blocks
	assert((roi.width % 16) == 0);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
		__m128i *input1_ptr = (__m128i *)field1;
		__m128i *input2_ptr = (__m128i *)field2;
		__m128i *low_ptr = (__m128i *)lowpass;
		__m128i *high_ptr = (__m128i *)highpass;
		int column_step = 8;

		for (column = 0; column < roi.width; column += column_step)
		{
			__m128i input1_epi16;
			__m128i input2_epi16;

			// Check that the pointers to the next groups of pixels are properly aligned
			assert(ISALIGNED16(input1_ptr));
			assert(ISALIGNED16(input2_ptr));

			// Load the input values which may be overwritten
			input1_epi16 = _mm_load_si128(input1_ptr++);
			input2_epi16 = _mm_load_si128(input2_ptr++);

			// Compute the sum and differences of the pixels in each row
			_mm_store_si128(low_ptr++, _mm_adds_epi16(input1_epi16, input2_epi16));
			_mm_store_si128(high_ptr++, _mm_subs_epi16(input2_epi16, input1_epi16));
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}

#endif
}

#endif


#if 0
void FilterTemporalQuant16s(PIXEL *field1, int pitch1, PIXEL *field2, int pitch2,
							PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
							ROI roi, PIXEL *buffer, size_t buffer_size, int quantization)
{
	int column_step = 8;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins
	int row, column;
	PIXEL *highrow;

	// Check that the buffer is large enough
	assert(buffer_size >= roi.width * sizeof(PIXEL));

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 8) == 0);

	// Compute the column where end of row processing must begin
	post_column = roi.width - (roi.width % column_step);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
		__m64 *input1_ptr = (__m64 *)field1;
		__m64 *input2_ptr = (__m64 *)field2;
		__m64 *low_ptr = (__m64 *)lowpass;
		__m64 *high_ptr = (__m64 *)buffer;

		column = 0;

#if (1 && XMMOPT)

		// Process column elements in parallel until end of row processing is required
		for (; column < post_column; column += column_step)
		{
			__m64 input1_pi16;		// Four input pixels from the first row
			__m64 input2_pi16;		// Four input pixels from the second row
			__m64 temp1_pi16;		// Four temporal lowpass coefficients
			__m64 diff1_pi16;		// Four temporal highpass coefficients
			__m64 temp2_pi16;		// Four temporal lowpass coefficients
			__m64 diff2_pi16;		// Four temporal highpass coefficients
			__m64 high_pi8;			// Eight signed bytes of highpass results

			// Check that the pointers to the next groups of pixels are properly aligned
			//assert(ISALIGNED16(input1_ptr));
			//assert(ISALIGNED16(input2_ptr));

			// Load the input values before they are overwritten
			input1_pi16 = *(input1_ptr++);
			input2_pi16 = *(input2_ptr++);

			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(low_ptr++) = temp1_pi16;

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Check that the pointers to the next groups of pixels are properly aligned
			//assert(ISALIGNED16(input1_ptr));
			//assert(ISALIGNED16(input2_ptr));

			// Load the input values before they are overwritten
			input1_pi16 = *(input1_ptr++);
			input2_pi16 = *(input2_ptr++);

			// Compute and store the sum of the pixels in each row
			temp2_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(low_ptr++) = temp2_pi16;

			// Compute the differences between the pixels in each row
			diff2_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the highpass values
			*(high_ptr++) = diff1_pi16;
			*(high_ptr++) = diff2_pi16;
		}

		// Should have left the loop at the post processing column
		assert(column == post_column);
#endif

		// Handle end of row processing for the remaining columns
		highrow = buffer;
		for (; column < roi.width; column++) {
			int value1 = field1[column];
			int value2 = field2[column];

			int temp = value1 + value2;
			int diff = value2 - value1;

			lowpass[column] = SATURATE_16S(temp);
			highrow[column] = SATURATE_16S(diff);
		}

		// Quantize the row of highpass results
		QuantizeRow16sTo8s(buffer, (PIXEL8S *)highpass, roi.width, quantization);

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}
#endif


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void FilterTemporalRow8uTo16s(PIXEL8U *row1, PIXEL8U *row2, int length,
							  PIXEL16S *lowpass, PIXEL16S *highpass,
							  int offset)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

void FilterTemporalRow8uTo16s(PIXEL8U *row1, PIXEL8U *row2, int length,
							  PIXEL16S *lowpass, PIXEL16S *highpass,
							  int offset)
{
	int column_step = 8;
	int post_column = length - (length % column_step);
	int column;

#if (1 && XMMOPT)

	__m64 *input1_ptr = (__m64 *)row1;
	__m64 *input2_ptr = (__m64 *)row2;
	__m64 *lowpass_ptr = (__m64 *)lowpass;
	__m64 *highpass_ptr = (__m64 *)highpass;

	__m64 temp1_pi16;
	__m64 diff1_pi16;
	//__m64 temp2_pi16;
	//__m64 diff2_pi16;

	__m64 input1_pu8;
	__m64 input2_pu8;

#if _ENCODE_CHROMA_OFFSET
	__m64 offset_pi16 = _mm_set1_pi16(offset);
#endif
#endif

	column = 0;

#if (1 && XMMOPT)

	// Check that the pointers to the groups of pixels are properly aligned
	//assert(ISALIGNED16(input1_ptr));
	//assert(ISALIGNED16(input2_ptr));

	// Check that the output pointers are properly aligned
	//assert(ISALIGNED16(lowpass_ptr));
	//assert(ISALIGNED16(highpass_ptr));

	// Preload the input values (which may be overwritten)
	input1_pu8 = *(input1_ptr++);
	input2_pu8 = *(input2_ptr++);

	for (; column < post_column; column += column_step)
	{
		__m64 input3_pu8;
		__m64 input4_pu8;
		__m64 input1_pi16;
		__m64 input2_pi16;
		__m64 zero_si64 = _mm_setzero_si64();

		// Preload the next set of input values
		input3_pu8 = *(input1_ptr++);
		input4_pu8 = *(input2_ptr++);

		// Unpack the first eight pixels
		input1_pi16 = _mm_unpacklo_pi8(input1_pu8, zero_si64);
		input2_pi16 = _mm_unpacklo_pi8(input2_pu8, zero_si64);

#if _ENCODE_CHROMA_OFFSET
		// Subtract the offset
		input1_pi16 = _mm_subs_pi16(input1_pi16, offset_pi16);
		input2_pi16 = _mm_subs_pi16(input2_pi16, offset_pi16);
#endif
		// Compute and store the sum of the pixels in each row
		temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
		*(lowpass_ptr++) = temp1_pi16;

		// Compute the differences between the pixels in each row
		diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

		// Store the differences between the pixels in each row
		*(highpass_ptr++) = diff1_pi16;

		// Unpack the second eight pixels
		input1_pi16 = _mm_unpackhi_pi8(input1_pu8, zero_si64);
		input2_pi16 = _mm_unpackhi_pi8(input2_pu8, zero_si64);

#if _ENCODE_CHROMA_OFFSET
		// Subtract the offset
		input1_pi16 = _mm_subs_pi16(input1_pi16, offset_pi16);
		input2_pi16 = _mm_subs_pi16(input2_pi16, offset_pi16);
#endif
		// Compute and store the sum of the pixels in each row
		temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
		*(lowpass_ptr++) = temp1_pi16;

		// Compute the differences between the pixels in each row
		diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

		// Store the differences between the pixels in each row
		*(highpass_ptr++) = diff1_pi16;

		// Use the preloaded pixels for the next loop iteration
		input1_pu8 = input3_pu8;
		input2_pu8 = input4_pu8;
	}

	//_mm_empty();	// Clear the mmx register state

	// Should have exited the loop at the post processing column
	assert(column == post_column);
#endif

	// Process the rest of the pixels in the pair of rows
	for (; column < length; column++)
	{
#if _ENCODE_CHROMA_OFFSET
		int value1 = row1[column] - offset;
		int value2 = row2[column] - offset;
#else
		int value1 = row1[column];
		int value2 = row2[column];
#endif
		int temp = value1 + value2;
		int diff = value2 - value1;

		lowpass[column] = SATURATE(temp);
		highpass[column] = SATURATE(diff);
	}
}

#endif

#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

#if _TEMPORAL_TWO_ROWS

// This version reads the two rows at the same time

// Perform the temporal transform on a pair of rows producing 16-bit coefficients
void FilterTemporalRow8uTo16s(PIXEL8U *row1, PIXEL8U *row2, int length,
							  PIXEL16S *lowpass, PIXEL16S *highpass,
							  int offset)
{
	int column_step = 16;
	int post_column = length - (length % column_step);
	int column;

#if (1 && XMMOPT)

	__m128i *input1_ptr = (__m128i *)row1;
	__m128i *input2_ptr = (__m128i *)row2;
	__m128i *lowpass_ptr = (__m128i *)lowpass;
	__m128i *highpass_ptr = (__m128i *)highpass;

	__m128i temp1_epi16;
	__m128i diff1_epi16;
	//__m128i temp2_epi16;
	//__m128i diff2_epi16;

	__m128i input1_epu8;
	__m128i input2_epu8;

#if _ENCODE_CHROMA_OFFSET
	__m128i offset_epi16 = _mm_set1_epi16(offset);
#endif
#endif

	column = 0;

#if (1 && XMMOPT)

	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(input1_ptr));
	assert(ISALIGNED16(input2_ptr));

	// Check that the output pointers are properly aligned
	assert(ISALIGNED16(lowpass_ptr));
	assert(ISALIGNED16(highpass_ptr));

	// Preload the input values (which may be overwritten)
	input1_epu8 = _mm_load_si128(input1_ptr++);
	input2_epu8 = _mm_load_si128(input2_ptr++);

	for (; column < post_column; column += column_step)
	{
		__m128i input3_epu8;
		__m128i input4_epu8;
		__m128i input1_epi16;
		__m128i input2_epi16;

		// Preload the next set of input values
		input3_epu8 = _mm_load_si128(input1_ptr++);
		input4_epu8 = _mm_load_si128(input2_ptr++);

		// Unpack the first eight pixels
		input1_epi16 = _mm_unpacklo_epi8(input1_epu8, _mm_setzero_si128());
		input2_epi16 = _mm_unpacklo_epi8(input2_epu8, _mm_setzero_si128());

#if _ENCODE_CHROMA_OFFSET
		// Subtract the offset
		input1_epi16 = _mm_subs_epi16(input1_epi16, offset_epi16);
		input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif
		// Compute and store the sum of the pixels in each row
		temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
		_mm_store_si128(lowpass_ptr++, temp1_epi16);

		// Compute the differences between the pixels in each row
		diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

		// Store the differences between the pixels in each row
		_mm_store_si128(highpass_ptr++, diff1_epi16);

		// Unpack the second eight pixels
		input1_epi16 = _mm_unpackhi_epi8(input1_epu8, _mm_setzero_si128());
		input2_epi16 = _mm_unpackhi_epi8(input2_epu8, _mm_setzero_si128());

#if _ENCODE_CHROMA_OFFSET
		// Subtract the offset
		input1_epi16 = _mm_subs_epi16(input1_epi16, offset_epi16);
		input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif
		// Compute and store the sum of the pixels in each row
		temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
		_mm_store_si128(lowpass_ptr++, temp1_epi16);

		// Compute the differences between the pixels in each row
		diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

		// Store the differences between the pixels in each row
		_mm_store_si128(highpass_ptr++, diff1_epi16);

		// Use the preloaded pixels for the next loop iteration
		input1_epu8 = input3_epu8;
		input2_epu8 = input4_epu8;
	}

	// Should have exited the loop at the post processing column
	assert(column == post_column);
#endif

	// Process the rest of the pixels in the pair of rows
	for (; column < length; column++)
	{
#if _ENCODE_CHROMA_OFFSET
		int value1 = row1[column] - offset;
		int value2 = row2[column] - offset;
#else
		int value1 = row1[column];
		int value2 = row2[column];
#endif
		int temp = value1 + value2;
		int diff = value2 - value1;

		lowpass[column] = SATURATE(temp);
		highpass[column] = SATURATE(diff);
	}
}

#else

// This version reads one row at a time to enable hardware prefetching

// Perform the temporal transform on a pair of rows producing 16-bit coefficients
void FilterTemporalRow8uTo16s(PIXEL8U *row1, PIXEL8U *row2, int length,
							  PIXEL16S *lowpass, PIXEL16S *highpass, int offset)
{
	int column_step = 16;
	int post_column = length - (length % column_step);
	int column;

#if (1 && XMMOPT)

	__m128i *input1_ptr = (__m128i *)row1;
	__m128i *input2_ptr = (__m128i *)row2;
	__m128i *lowpass_ptr = (__m128i *)lowpass;
	__m128i *highpass_ptr = (__m128i *)highpass;

	__m128i temp1_epi16;
	__m128i diff1_epi16;
	//__m128i temp2_epi16;
	//__m128i diff2_epi16;

	__m128i input1_epu8;
	__m128i input2_epu8;
	__m128i input1_epi16;

#if _ENCODE_CHROMA_OFFSET
	__m128i offset_epi16 = _mm_set1_epi16(offset);
#endif
#endif

	column = 0;

#if (1 && XMMOPT)

	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(input1_ptr));
	assert(ISALIGNED16(input2_ptr));

	// Check that the output pointers are properly aligned
	assert(ISALIGNED16(lowpass_ptr));
	assert(ISALIGNED16(highpass_ptr));

	// Preload the first group of input values (which may be overwritten)
	input1_epu8 = _mm_load_si128(input1_ptr++);

	// Load the first row of pixels
	for (; column < post_column; column += column_step)
	{
		// Preload the next set of pixels
		input2_epu8 = _mm_load_si128(input1_ptr++);

		// Unpack the first half of the pixels
		input1_epi16 = _mm_unpacklo_epi8(input1_epu8, _mm_setzero_si128());

#if _ENCODE_CHROMA_OFFSET
		// Subtract the offset
		input1_epi16 = _mm_subs_epi16(input1_epi16, offset_epi16);
#endif
		// Store the 16-bit pixels in the buffer for the lowpass results
		_mm_store_si128(lowpass_ptr++, input1_epi16);

		// Unpack the second half of the pixels
		input1_epi16 = _mm_unpackhi_epi8(input1_epu8, _mm_setzero_si128());

#if _ENCODE_CHROMA_OFFSET
		// Subtract the offset
		input1_epi16 = _mm_subs_pi16(input1_epi16, offset_epi16);
#endif
		// Store the 16-bit pixels in the buffer for the lowpass results
		_mm_store_si128(lowpass_ptr++, input1_epi16);

		// The preloaded pixels are used on the next iteration of the loop
		input1_epu8 = input2_epu8;
	}

	// Should have exited the loop at the post processing column
	assert(column == post_column);

#endif

	for (; column < length; column++)
	{
#if _ENCODE_CHROMA_OFFSET
		lowpass[column] = row1[column] - offset;
#else
		lowpass[column] = row1[column];
#endif
	}

	// Start at the first column of the second row of pixels
	column = 0;

#if (1 && XMMOPT)

	// Reset the lowpass pointer to the begininning of the buffer
	lowpass_ptr = (__m128i *)lowpass;

	// Preload the first group of input values (which may be overwritten)
	input2_epu8 = _mm_load_si128(input2_ptr++);

	for (; column < post_column; column += column_step)
	{
		//__m128i input3_epu8;
		//__m128i input4_epu8;
		__m128i input2_epi16;

		// Load eight pixels from the first row
		input1_epi16 = _mm_load_si128(lowpass_ptr);

		// Unpack the first eight pixels from the second row
		input2_epi16 = _mm_unpacklo_epi8(input2_epu8, _mm_setzero_si128());

#if _ENCODE_CHROMA_OFFSET
		// Subtract the offset
		input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif
		// Compute and store the sum of the pixels in each row
		temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
		_mm_store_si128(lowpass_ptr++, temp1_epi16);

		// Compute the differences between the pixels in each row
		diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

		// Store the differences between the pixels in each row
		_mm_store_si128(highpass_ptr++, diff1_epi16);

		// Load the next eight pixels from the first row
		input1_epi16 = _mm_load_si128(lowpass_ptr);

		// Unpack the second eight pixels from the second row
		input2_epi16 = _mm_unpackhi_epi8(input2_epu8, _mm_setzero_si128());

#if _ENCODE_CHROMA_OFFSET
		// Subtract the offset
		input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif
		// Compute and store the sum of the pixels in each row
		temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
		_mm_store_si128(lowpass_ptr++, temp1_epi16);

		// Compute the differences between the pixels in each row
		diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

		// Store the differences between the pixels in each row
		_mm_store_si128(highpass_ptr++, diff1_epi16);

		// Load the next sixteen pixels from the second row
		input2_epu8 = _mm_load_si128(input2_ptr++);
	}

	// Should have exited the loop at the post processing column
	assert(column == post_column);
#endif

	// Process the rest of the pixels in the second row
	for (; column < length; column++)
	{
		int value1 = lowpass[column];
#if _ENCODE_CHROMA_OFFSET
		int value2 = row2[column] - offset;
#else
		int value2 = row2[column];
#endif
		int temp = value1 + value2;
		int diff = value2 - value1;

		lowpass[column] = SATURATE(temp);
		highpass[column] = SATURATE(diff);
	}
}

#endif

#endif


// Perform the temporal transform on a pair of rows of 16-bit coefficients
void FilterTemporalRow16s(PIXEL *row1, PIXEL *row2, int length,
						  PIXEL *lowpass, PIXEL *highpass, int offset)
{
	int column_step = 8;
	int post_column = length - (length % column_step);
	int column;

#if (1 && XMMOPT)

	__m128i *input1_ptr = (__m128i *)row1;
	__m128i *input2_ptr = (__m128i *)row2;
	__m128i *lowpass_ptr = (__m128i *)lowpass;
	__m128i *highpass_ptr = (__m128i *)highpass;

	__m128i temp1_epi16;
	__m128i diff1_epi16;
	//__m128i temp2_epi16;
	//__m128i diff2_epi16;

	__m128i input1_epi16;
	__m128i input2_epi16;

 #if _ENCODE_CHROMA_OFFSET
	__m128i offset_epi16 = _mm_set1_epi16(offset);
 #endif
#else
	assert(0); // running non-optimized code
#endif

	column = 0;

#if (1 && XMMOPT)

	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(input1_ptr));
	assert(ISALIGNED16(input2_ptr));

	// Check that the output pointers are properly aligned
	assert(ISALIGNED16(lowpass_ptr));
	assert(ISALIGNED16(highpass_ptr));

	// Preload the input values (which may be overwritten)
	input1_epi16 = _mm_load_si128(input1_ptr++);
	input2_epi16 = _mm_load_si128(input2_ptr++);

	for (; column < post_column; column += column_step)
	{
		__m128i input3_epi16;
		__m128i input4_epi16;

		// Preload the next set of input values
		input3_epi16 = _mm_load_si128(input1_ptr++);
		input4_epi16 = _mm_load_si128(input2_ptr++);

#if _ENCODE_CHROMA_OFFSET
		// Subtract the offset
		input1_epi16 = _mm_subs_epi16(input1_epi16, offset_epi16);
		input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif
		// Compute and store the sum of the pixels in each row
		temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
		_mm_store_si128(lowpass_ptr++, temp1_epi16);

		// Compute the differences between the pixels in each row
		diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

		// Store the differences between the pixels in each row
		_mm_store_si128(highpass_ptr++, diff1_epi16);

		// Use the preloaded pixels for the next loop iteration
		input1_epi16 = input3_epi16;
		input2_epi16 = input4_epi16;
	}

	// Should have exited the loop at the post processing column
	assert(column == post_column);
#endif

	// Process the rest of the pixels in the pair of rows
	for (; column < length; column++)
	{
#if _ENCODE_CHROMA_OFFSET
		int value1 = row1[column] - offset;
		int value2 = row2[column] - offset;
#else
		int value1 = row1[column];
		int value2 = row2[column];
#endif
		int temp = value1 + value2;
		int diff = value2 - value1;

		lowpass[column] = SATURATE(temp);
		highpass[column] = SATURATE(diff);
	}
}


#if 0  // not used
// Apply the temporal transform to a row of packed YUV pixels
void FilterTemporalRowYUVTo16s(BYTE *row1, BYTE *row2, int frame_width,
							   PIXEL *lowpass[], PIXEL *highpass[], int num_channels)
{
	int column_step = 32;
	int length = frame_width * 2;
	int post_column = length - (length % column_step);
	int column;

#if (1 && XMMOPT)

	__m128i *input1_ptr = (__m128i *)row1;
	__m128i *input2_ptr = (__m128i *)row2;
	__m128i *y_temp_ptr = (__m128i *)lowpass[0];
	__m128i *u_temp_ptr = (__m128i *)lowpass[1];
	__m128i *v_temp_ptr = (__m128i *)lowpass[2];
	__m128i *y_diff_ptr = (__m128i *)highpass[0];
	__m128i *u_diff_ptr = (__m128i *)highpass[1];
	__m128i *v_diff_ptr = (__m128i *)highpass[2];

	__m128i temp1_epi16;
	__m128i diff1_epi16;
	//__m128i temp2_epi16;
	//__m128i diff2_epi16;

	__m128i input1_epu8;
	__m128i input2_epu8;

#endif

	column = 0;

#if (1 && XMMOPT)

	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(input1_ptr));
	assert(ISALIGNED16(input2_ptr));

	// Check that the output pointers are properly aligned
	assert(ISALIGNED16(y_temp_ptr));
	assert(ISALIGNED16(u_temp_ptr));
	assert(ISALIGNED16(v_temp_ptr));
	assert(ISALIGNED16(y_diff_ptr));
	assert(ISALIGNED16(u_diff_ptr));
	assert(ISALIGNED16(v_diff_ptr));

	// Preload the packed pixels
	input1_epu8 = _mm_load_si128(input1_ptr++);
	input2_epu8 = _mm_load_si128(input2_ptr++);

	for (; column < post_column; column += column_step)
	{
		__m128i input3_epu8;
		__m128i input4_epu8;
		__m128i input1_epi16;
		__m128i input2_epi16;
		__m128i input3_epi16;
		__m128i input4_epi16;


		/***** Process the first eight luma pixels from each row *****/

		// Preload the next set of packed pixels
		input3_epu8 = _mm_load_si128(input1_ptr++);
		input4_epu8 = _mm_load_si128(input2_ptr++);

		// Unpack the first eight luma pixels from each row
		input1_epi16 = _mm_and_si128(input1_epu8, _mm_set1_epi16(0x00FF));
		input2_epi16 = _mm_and_si128(input2_epu8, _mm_set1_epi16(0x00FF));

		// Compute and store the sum of the pixels in each row
		temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
		_mm_store_si128(y_temp_ptr++, temp1_epi16);

		// Compute the differences between the pixels in each row
		diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

		// Store the differences between the pixels in each row
		_mm_store_si128(y_diff_ptr++, diff1_epi16);


		/***** Process eight u chroma pixels from each row *****/

		// Unpack the first four u chroma pixels from each row
		input1_epi16 = _mm_slli_epi32(input1_epu8, 16);
		input2_epi16 = _mm_slli_epi32(input2_epu8, 16);
		input1_epi16 = _mm_srli_epi32(input1_epi16, 24);
		input2_epi16 = _mm_srli_epi32(input2_epi16, 24);

		// Unpack the second four u chroma pixels from each row
		input3_epi16 = _mm_slli_epi32(input3_epu8, 16);
		input4_epi16 = _mm_slli_epi32(input4_epu8, 16);
		input3_epi16 = _mm_srli_epi32(input3_epi16, 24);
		input4_epi16 = _mm_srli_epi32(input4_epi16, 24);

		// Combine the two sets of u chroma pixels from each row
		input1_epi16 = _mm_packs_epi32(input1_epi16, input3_epi16);
		input2_epi16 = _mm_packs_epi32(input2_epi16, input4_epi16);

		// Compute and store the sum of the pixels in each row
		temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
		_mm_store_si128(u_temp_ptr++, temp1_epi16);

		// Compute the differences between the pixels in each row
		diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

		// Store the differences between the pixels in each row
		_mm_store_si128(u_diff_ptr++, diff1_epi16);


		/***** Process eight v chroma pixels from each row *****/

		// Unpack the first four v chroma pixels from each row
		input1_epi16 = _mm_srli_epi32(input1_epu8, 24);
		input2_epi16 = _mm_srli_epi32(input2_epu8, 24);

		// Unpack the second four v chroma pixels from each row
		input3_epi16 = _mm_srli_epi32(input3_epu8, 24);
		input4_epi16 = _mm_srli_epi32(input4_epu8, 24);

		// Combine the two sets of v chroma pixels from each row
		input1_epi16 = _mm_packs_epi32(input1_epi16, input3_epi16);
		input2_epi16 = _mm_packs_epi32(input2_epi16, input4_epi16);

		// Compute and store the sum of the pixels in each row
		temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
		_mm_store_si128(v_temp_ptr++, temp1_epi16);

		// Compute the differences between the pixels in each row
		diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

		// Store the differences between the pixels in each row
		_mm_store_si128(v_diff_ptr++, diff1_epi16);


		/***** Process the second eight luma pixels from each row *****/

		// Preload the next set of packed pixels
		input1_epu8 = _mm_load_si128(input1_ptr++);
		input2_epu8 = _mm_load_si128(input2_ptr++);

		// Unpack the first eight luma pixels from each row
		input1_epi16 = _mm_and_si128(input3_epu8, _mm_set1_epi16(0x00FF));
		input2_epi16 = _mm_and_si128(input4_epu8, _mm_set1_epi16(0x00FF));

		// Compute and store the sum of the pixels in each row
		temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
		_mm_store_si128(y_temp_ptr++, temp1_epi16);

		// Compute the differences between the pixels in each row
		diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

		// Store the differences between the pixels in each row
		_mm_store_si128(y_diff_ptr++, diff1_epi16);
	}

	// Should have exited the loop at the post processing column
	assert(column == post_column);
#endif

	// Process the rest of the pixels in the pair of rows
	for (; column < length; column += 4)
	{
		int value1;
		int value2;
		int temp;
		int diff;


		/***** Process first pair of luma pixels *****/

		value1 = row1[column + 0];
		value2 = row2[column + 0];

		temp = value1 + value2;
		diff = value2 - value1;

		lowpass[0][column] = SATURATE(temp);
		highpass[0][column] = SATURATE(diff);


		/***** Process the pair of u chroma pixels *****/

		value1 = row1[column + 1];
		value2 = row2[column + 1];

		temp = value1 + value2;
		diff = value2 - value1;

		lowpass[1][column] = SATURATE(temp);
		highpass[1][column] = SATURATE(diff);


		/***** Process second pair of luma pixels *****/

		value1 = row1[column + 2];
		value2 = row2[column + 2];

		temp = value1 + value2;
		diff = value2 - value1;

		lowpass[0][column] = SATURATE(temp);
		highpass[0][column] = SATURATE(diff);


		/***** Process the pair of v chroma pixels *****/

		value1 = row1[column + 3];
		value2 = row2[column + 3];

		temp = value1 + value2;
		diff = value2 - value1;

		lowpass[2][column] = SATURATE(temp);
		highpass[2][column] = SATURATE(diff);
	}

}
#endif

#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

// Apply the temporal transform to one channel in the even and odd rows
void FilterTemporalRowYUYVChannelTo16s(BYTE *row1, BYTE *row2, int frame_width,
									   int channel, PIXEL *lowpass, PIXEL *highpass,
									   int offset, int precision, int limit_yuv)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Apply the temporal transform to one channel in the even and odd rows
void FilterTemporalRowYUYVChannelTo16s(BYTE *row1, BYTE *row2, int frame_width,
									   int channel, PIXEL *lowpass, PIXEL *highpass,
									   int offset, int precision, int limit_yuv)
{
	int length = frame_width * 2;
	int column;

#if (1 && XMMOPT)
	int column_step = 16;
	int post_column = length - (length % column_step);

	__m64 *input1_ptr = (__m64 *)row1;
	__m64 *input2_ptr = (__m64 *)row2;
	__m64 *temp_ptr = (__m64 *)lowpass;
	__m64 *diff_ptr = (__m64 *)highpass;

	__m64 temp1_pi16;
	__m64 diff1_pi16;
	//__m64 temp2_pi16;
	//__m64 diff2_pi16;

	__m64 input1_pu8;
	__m64 input2_pu8;

#if _ENCODE_CHROMA_OFFSET
	__m64 offset_pi16 = _mm_set1_pi16(offset);
#endif
#endif

	column = 0;

#if (1 && XMMOPT)

	// Check that the pointers to the groups of pixels are properly aligned
	//assert(ISALIGNED16(input1_ptr));
	//assert(ISALIGNED16(input2_ptr));

	// Check that the output pointers are properly aligned
	//assert(ISALIGNED16(temp_ptr));
	//assert(ISALIGNED16(diff_ptr));

	// Adjust the post processing column to avoid bad memory access during preloading
	if (post_column + sizeof(__m64) > length)
		post_column -= column_step;

	// Check that the post processing column is an integer number of loop iterations
	assert((post_column % column_step) == 0);

	// Preload the packed pixels
	input1_pu8 = *(input1_ptr++);
	input2_pu8 = *(input2_ptr++);

	for (; column < post_column; column += column_step)
	{
		__m64 input3_pu8;
		__m64 input4_pu8;
		__m64 input1_pi16;
		__m64 input2_pi16;
		__m64 input3_pi16;
		__m64 input4_pi16;

		// Preload the next set of packed pixels
		input3_pu8 = *(input1_ptr++);
		input4_pu8 = *(input2_ptr++);

		if (channel == 0)
		{
#if (0 && STRICT_SATURATE)
			// Perform strict saturation on luma if required
			input1_pu8 = _mm_subs_pu8(input1_pu8, _mm_set1_pi8(16));
			input1_pu8 = _mm_adds_pu8(input1_pu8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input1_pu8 = _mm_subs_pu8(input1_pu8, _mm_set1_pi8(20));

			input2_pu8 = _mm_subs_pu8(input2_pu8, _mm_set1_pi8(16));
			input2_pu8 = _mm_adds_pu8(input2_pu8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input2_pu8 = _mm_subs_pu8(input2_pu8, _mm_set1_pi8(20));
#endif
			// Unpack the first four luma pixels from each row
			input1_pi16 = _mm_and_si64(input1_pu8, _mm_set1_pi16(0x00FF));
			input2_pi16 = _mm_and_si64(input2_pu8, _mm_set1_pi16(0x00FF));

			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(temp_ptr++) = temp1_pi16;

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			*(diff_ptr++) = diff1_pi16;

			// Preload the next set of packed pixels
			input1_pu8 = *(input1_ptr++);
			input2_pu8 = *(input2_ptr++);

#if (0 && STRICT_SATURATE)
			// Perform strict saturation on luma if required
			input3_pu8 = _mm_subs_pu8(input3_pu8, _mm_set1_pi8(16));
			input3_pu8 = _mm_adds_pu8(input3_pu8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input3_pu8 = _mm_subs_pu8(input3_pu8, _mm_set1_pi8(20));

			input4_pu8 = _mm_subs_pu8(input4_pu8, _mm_set1_pi8(16));
			input4_pu8 = _mm_adds_pu8(input4_pu8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input4_pu8 = _mm_subs_pu8(input4_pu8, _mm_set1_pi8(20));
#endif
			// Unpack the second four luma pixels from each row
			input1_pi16 = _mm_and_si64(input3_pu8, _mm_set1_pi16(0x00FF));
			input2_pi16 = _mm_and_si64(input4_pu8, _mm_set1_pi16(0x00FF));

			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(temp_ptr++) = temp1_pi16;

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			*(diff_ptr++) = diff1_pi16;
		}
		else if (channel == 1)
		{
#if _SWAP_CHROMA
			// Unpack the first two u chroma pixels from each row
			input1_pi16 = _mm_srli_pi32(input1_pu8, 24);
			input2_pi16 = _mm_srli_pi32(input2_pu8, 24);

			// Preload the next set of packed pixels
			input1_pu8 = *(input1_ptr++);
			input2_pu8 = *(input2_ptr++);

			// Unpack the second two u chroma pixels from each row
			input3_pi16 = _mm_srli_pi32(input3_pu8, 24);
			input4_pi16 = _mm_srli_pi32(input4_pu8, 24);

			// Combine the two sets of u chroma pixels from each row
			input1_pi16 = _mm_packs_pi32(input1_pi16, input3_pi16);
			input2_pi16 = _mm_packs_pi32(input2_pi16, input4_pi16);

#if _ENCODE_CHROMA_OFFSET
			// Subtract the offset
			input1_pi16 = _mm_subs_pi16(input1_pi16, offset_pi16);
			input2_pi16 = _mm_subs_pi16(input2_pi16, offset_pi16);
#endif
			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(temp_ptr++) = temp1_pi16;

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			*(diff_ptr++) = diff1_pi16;
#else

#error This section of code not finished

			// Unpack the first four u chroma pixels from each row
			input1_pi16 = _mm_slli_pi32(input1_pu8, 16);
			input2_pi16 = _mm_slli_pi32(input2_pu8, 16);
			input1_pi16 = _mm_srli_pi32(input1_pi16, 24);
			input2_pi16 = _mm_srli_pi32(input2_pi16, 24);

			// Preload the next set of packed pixels
			input1_pu8 = _mm_load_si64(input1_ptr++);
			input2_pu8 = _mm_load_si64(input2_ptr++);

			// Unpack the second four u chroma pixels from each row
			input3_pi16 = _mm_slli_pi32(input3_pu8, 16);
			input4_pi16 = _mm_slli_pi32(input4_pu8, 16);
			input3_pi16 = _mm_srli_pi32(input3_pi16, 24);
			input4_pi16 = _mm_srli_pi32(input4_pi16, 24);

			// Combine the two sets of u chroma pixels from each row
			input1_pi16 = _mm_packs_pi32(input1_pi16, input3_pi16);
			input2_pi16 = _mm_packs_pi32(input2_pi16, input4_pi16);

			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			_mm_store_si64(temp_ptr++, temp1_pi16);

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			_mm_store_si64(diff_ptr++, diff1_pi16);
#endif
		}
		else
		{
#if _SWAP_CHROMA
			// Unpack the first two v chroma pixels from each row
			input1_pi16 = _mm_slli_pi32(input1_pu8, 16);
			input2_pi16 = _mm_slli_pi32(input2_pu8, 16);
			input1_pi16 = _mm_srli_pi32(input1_pi16, 24);
			input2_pi16 = _mm_srli_pi32(input2_pi16, 24);

			// Preload the next set of packed pixels
			input1_pu8 = *(input1_ptr++);
			input2_pu8 = *(input2_ptr++);

			// Unpack the second two v chroma pixels from each row
			input3_pi16 = _mm_slli_pi32(input3_pu8, 16);
			input4_pi16 = _mm_slli_pi32(input4_pu8, 16);
			input3_pi16 = _mm_srli_pi32(input3_pi16, 24);
			input4_pi16 = _mm_srli_pi32(input4_pi16, 24);

			// Combine the two sets of v chroma pixels from each row
			input1_pi16 = _mm_packs_pi32(input1_pi16, input3_pi16);
			input2_pi16 = _mm_packs_pi32(input2_pi16, input4_pi16);

#if _ENCODE_CHROMA_OFFSET
			// Subtract the offset
			input1_pi16 = _mm_subs_pi16(input1_pi16, offset_pi16);
			input2_pi16 = _mm_subs_pi16(input2_pi16, offset_pi16);
#endif
			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(temp_ptr++) = temp1_pi16;

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			*(diff_ptr++) = diff1_pi16;
#else

#error This section of code not finished

			// Unpack the first four v chroma pixels from each row
			input1_pi16 = _mm_srli_pi32(input1_pu8, 24);
			input2_pi16 = _mm_srli_pi32(input2_pu8, 24);

			// Preload the next set of packed pixels
			input1_pu8 = _mm_load_si64(input1_ptr++);
			input2_pu8 = _mm_load_si64(input2_ptr++);

			// Unpack the second four v chroma pixels from each row
			input3_pi16 = _mm_srli_pi32(input3_pu8, 24);
			input4_pi16 = _mm_srli_pi32(input4_pu8, 24);

			// Combine the two sets of v chroma pixels from each row
			input1_pi16 = _mm_packs_pi32(input1_pi16, input3_pi16);
			input2_pi16 = _mm_packs_pi32(input2_pi16, input4_pi16);

			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			_mm_store_si64(temp_ptr++, temp1_pi16);

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			_mm_store_si64(diff_ptr++, diff1_pi16);
#endif
		}
	}

	//_mm_empty();		// Clear the mmx register state

	// Should have exited the loop at the post processing column
	assert(column == post_column);

#endif

	// Process the rest of the pixels in the pair of rows
	for (; column < length; column += 4)
	{
		int value1;
		int value2;
		int temp;
		int diff;

		if (channel == 0)
		{
			value1 = row1[column + 0];
#if (0 && STRICT_SATURATE)
			if(value1 < 16) value1 = 16;
			if(value1 > 235) value1 = 235;
#endif
			value2 = row2[column + 0];
#if (0 && STRICT_SATURATE)
			if(value2 < 16) value2 = 16;
			if(value2 > 235) value2 = 235;
#endif

			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/2] = SATURATE(temp);
			highpass[column/2] = SATURATE(diff);

			value1 = row1[column + 2];
			value2 = row2[column + 2];

			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/2 + 1] = SATURATE(temp);
			highpass[column/2 + 1] = SATURATE(diff);
		}
#if _SWAP_CHROMA
		else if (channel == 2)
#else
		else if (channel == 1)
#endif
		{
#if _ENCODE_CHROMA_OFFSET
			value1 = row1[column + 1] - offset;
			value2 = row2[column + 1] - offset;
#else
			value1 = row1[column + 1];
			value2 = row2[column + 1];
#endif
			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/4] = SATURATE(temp);
			highpass[column/4] = SATURATE(diff);
		}
		else
		{
#if _ENCODE_CHROMA_OFFSET
			value1 = row1[column + 3] - offset;
			value2 = row2[column + 3] - offset;
#else
			value1 = row1[column + 3];
			value2 = row2[column + 3];
#endif
			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/4] = SATURATE(temp);
			highpass[column/4] = SATURATE(diff);
		}
	}
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Apply the temporal transform to one channel in the even and odd rows
void FilterTemporalRowYUYVChannelTo16s(uint8_t *row1, uint8_t *row2, int frame_width,
									   int channel, PIXEL *lowpass, PIXEL *highpass,
									   int offset, int precision, int limit_yuv)
{
	int column_step = 32;
	int length = frame_width * 2;
	int post_column = length - (length % column_step);
	int column;
	int shift = precision - 8;

#if (1 && XMMOPT)

	__m128i *input1_ptr = (__m128i *)row1;
	__m128i *input2_ptr = (__m128i *)row2;
	__m128i *temp_ptr = (__m128i *)lowpass;
	__m128i *diff_ptr = (__m128i *)highpass;

	__m128i temp1_epi16;
	__m128i diff1_epi16;
	//__m128i temp2_epi16;
	//__m128i diff2_epi16;

	__m128i input1_epu8;
	__m128i input2_epu8;

#if _ENCODE_CHROMA_OFFSET
	__m128i offset_epi16 = _mm_set1_epi16(offset);
#endif
#endif

	// Adjust the post column so that pixels are not preloaded beyond the end of the row
	if (post_column == length) post_column -= column_step;

	// Check that the post column is an integral number of column steps
	assert((post_column % column_step) == 0);

	// Start processing at the first (leftmost) column
	column = 0;

#if (1 && XMMOPT)

	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(input1_ptr));
	assert(ISALIGNED16(input2_ptr));

	// Check that the output pointers are properly aligned
	assert(ISALIGNED16(temp_ptr));
	assert(ISALIGNED16(diff_ptr));

	// Preload the packed pixels
	input1_epu8 = _mm_load_si128(input1_ptr++);
	input2_epu8 = _mm_load_si128(input2_ptr++);

	for (; column < post_column; column += column_step)
	{
		__m128i input3_epu8;
		__m128i input4_epu8;
		__m128i input1_epi16;
		__m128i input2_epi16;
		__m128i input3_epi16;
		__m128i input4_epi16;

		// Preload the next set of packed pixels
		input3_epu8 = _mm_load_si128(input1_ptr++);
		input4_epu8 = _mm_load_si128(input2_ptr++);

		if (channel == 0)
		{
#if (0 && STRICT_SATURATE)
			// Perform strict saturation on luma if required
			input1_epu8 = _mm_subs_epu8(input1_epu8, _mm_set1_epi8(16));
			input1_epu8 = _mm_adds_epu8(input1_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input1_epu8 = _mm_subs_epu8(input1_epu8, _mm_set1_epi8(20));

			input2_epu8 = _mm_subs_epu8(input2_epu8, _mm_set1_epi8(16));
			input2_epu8 = _mm_adds_epu8(input2_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input2_epu8 = _mm_subs_epu8(input2_epu8, _mm_set1_epi8(20));
#endif

			// Unpack the first eight luma pixels from each row
			input1_epi16 = _mm_and_si128(input1_epu8, _mm_set1_epi16(0x00FF));
			input2_epi16 = _mm_and_si128(input2_epu8, _mm_set1_epi16(0x00FF));

			if(limit_yuv && shift == 2)//TODO DAN 20090226 Much this active for Canon 5D
			{
				input1_epi16 = _mm_mullo_epi16(input1_epi16, _mm_set1_epi16(55));
				input2_epi16 = _mm_mullo_epi16(input2_epi16, _mm_set1_epi16(55));
				input1_epi16 = _mm_srai_epi16(input1_epi16, 4);
				input2_epi16 = _mm_srai_epi16(input2_epi16, 4);
				input1_epi16 = _mm_adds_epi16(input1_epi16, _mm_set1_epi16(64));
				input2_epi16 = _mm_adds_epi16(input2_epi16, _mm_set1_epi16(64));
			}
			else
			{
				input1_epi16 = _mm_slli_epi16(input1_epi16, shift);
				input2_epi16 = _mm_slli_epi16(input2_epi16, shift);
			}

			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(temp_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_epi16);

			// Preload the next set of packed pixels
			input1_epu8 = _mm_load_si128(input1_ptr++);
			input2_epu8 = _mm_load_si128(input2_ptr++);


#if (0 && STRICT_SATURATE)
			// Perform strict saturation on luma if required
			input3_epu8 = _mm_subs_epu8(input3_epu8, _mm_set1_epi8(16));
			input3_epu8 = _mm_adds_epu8(input3_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input3_epu8 = _mm_subs_epu8(input3_epu8, _mm_set1_epi8(20));

			input4_epu8 = _mm_subs_epu8(input4_epu8, _mm_set1_epi8(16));
			input4_epu8 = _mm_adds_epu8(input4_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input4_epu8 = _mm_subs_epu8(input4_epu8, _mm_set1_epi8(20));
#endif
			// Unpack the first eight luma pixels from each row
			input1_epi16 = _mm_and_si128(input3_epu8, _mm_set1_epi16(0x00FF));
			input2_epi16 = _mm_and_si128(input4_epu8, _mm_set1_epi16(0x00FF));


			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				input1_epi16 = _mm_mullo_epi16(input1_epi16, _mm_set1_epi16(55));
				input2_epi16 = _mm_mullo_epi16(input2_epi16, _mm_set1_epi16(55));
				input1_epi16 = _mm_srai_epi16(input1_epi16, 4);
				input2_epi16 = _mm_srai_epi16(input2_epi16, 4);
				input1_epi16 = _mm_adds_epi16(input1_epi16, _mm_set1_epi16(64));
				input2_epi16 = _mm_adds_epi16(input2_epi16, _mm_set1_epi16(64));
			}
			else
			{
				input1_epi16 = _mm_slli_epi16(input1_epi16, shift);
				input2_epi16 = _mm_slli_epi16(input2_epi16, shift);
			}


			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(temp_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_epi16);
		}
		else if (channel == 1)
		{
#if _SWAP_CHROMA
			// Unpack the first four u chroma pixels from each row
			input1_epi16 = _mm_srli_epi32(input1_epu8, 24);
			input2_epi16 = _mm_srli_epi32(input2_epu8, 24);

			// Preload the next set of packed pixels
			input1_epu8 = _mm_load_si128(input1_ptr++);
			input2_epu8 = _mm_load_si128(input2_ptr++);

			// Unpack the second four u chroma pixels from each row
			input3_epi16 = _mm_srli_epi32(input3_epu8, 24);
			input4_epi16 = _mm_srli_epi32(input4_epu8, 24);

			// Combine the two sets of u chroma pixels from each row
			input1_epi16 = _mm_packs_epi32(input1_epi16, input3_epi16);
			input2_epi16 = _mm_packs_epi32(input2_epi16, input4_epi16);

#if _ENCODE_CHROMA_OFFSET
			// Subtract the offset
			input1_epi16 = _mm_subs_epi16(input1_epi16, offset_epi16);
			input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif
			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				input1_epi16 = _mm_mullo_epi16(input1_epi16, _mm_set1_epi16(56));
				input2_epi16 = _mm_mullo_epi16(input2_epi16, _mm_set1_epi16(56));
				input1_epi16 = _mm_srai_epi16(input1_epi16, 4);
				input2_epi16 = _mm_srai_epi16(input2_epi16, 4);
				input1_epi16 = _mm_adds_epi16(input1_epi16, _mm_set1_epi16(64));
				input2_epi16 = _mm_adds_epi16(input2_epi16, _mm_set1_epi16(64));
			}
			else
			{
				input1_epi16 = _mm_slli_epi16(input1_epi16, shift);
				input2_epi16 = _mm_slli_epi16(input2_epi16, shift);
			}

			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(temp_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_epi16);
#else
			// Unpack the first four u chroma pixels from each row
			input1_epi16 = _mm_slli_epi32(input1_epu8, 16);
			input2_epi16 = _mm_slli_epi32(input2_epu8, 16);
			input1_epi16 = _mm_srli_epi32(input1_epi16, 24);
			input2_epi16 = _mm_srli_epi32(input2_epi16, 24);

			// Preload the next set of packed pixels
			input1_epu8 = _mm_load_si128(input1_ptr++);
			input2_epu8 = _mm_load_si128(input2_ptr++);

			// Unpack the second four u chroma pixels from each row
			input3_epi16 = _mm_slli_epi32(input3_epu8, 16);
			input4_epi16 = _mm_slli_epi32(input4_epu8, 16);
			input3_epi16 = _mm_srli_epi32(input3_epi16, 24);
			input4_epi16 = _mm_srli_epi32(input4_epi16, 24);

			// Combine the two sets of u chroma pixels from each row
			input1_epi16 = _mm_packs_epi32(input1_epi16, input3_epi16);
			input2_epi16 = _mm_packs_epi32(input2_epi16, input4_epi16);

#if _ENCODE_CHROMA_OFFSET
			// Subtract the offset
			input1_epi16 = _mm_subs_epi16(input1_epi16, offset_epi16);
			input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif
			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				input1_epi16 = _mm_mullo_epi16(input1_epi16, _mm_set1_epi16(56));
				input2_epi16 = _mm_mullo_epi16(input2_epi16, _mm_set1_epi16(56));
				input1_epi16 = _mm_srai_epi16(input1_epi16, 4);
				input2_epi16 = _mm_srai_epi16(input2_epi16, 4);
				input1_epi16 = _mm_adds_epi16(input1_epi16, _mm_set1_epi16(64));
				input2_epi16 = _mm_adds_epi16(input2_epi16, _mm_set1_epi16(64));
			}
			else
			{
				input1_epi16 = _mm_slli_epi16(input1_epi16, shift);
				input2_epi16 = _mm_slli_epi16(input2_epi16, shift);
			}


			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(temp_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_epi16);
#endif
		}
		else
		{
#if _SWAP_CHROMA
			// Unpack the first four v chroma pixels from each row
			input1_epi16 = _mm_slli_epi32(input1_epu8, 16);
			input2_epi16 = _mm_slli_epi32(input2_epu8, 16);
			input1_epi16 = _mm_srli_epi32(input1_epi16, 24);
			input2_epi16 = _mm_srli_epi32(input2_epi16, 24);

			// Preload the next set of packed pixels
			input1_epu8 = _mm_load_si128(input1_ptr++);
			input2_epu8 = _mm_load_si128(input2_ptr++);

			// Unpack the second four v chroma pixels from each row
			input3_epi16 = _mm_slli_epi32(input3_epu8, 16);
			input4_epi16 = _mm_slli_epi32(input4_epu8, 16);
			input3_epi16 = _mm_srli_epi32(input3_epi16, 24);
			input4_epi16 = _mm_srli_epi32(input4_epi16, 24);

			// Combine the two sets of v chroma pixels from each row
			input1_epi16 = _mm_packs_epi32(input1_epi16, input3_epi16);
			input2_epi16 = _mm_packs_epi32(input2_epi16, input4_epi16);

#if _ENCODE_CHROMA_OFFSET
			// Subtract the offset
			input1_epi16 = _mm_subs_epi16(input1_epi16, offset_epi16);
			input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif

			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				input1_epi16 = _mm_mullo_epi16(input1_epi16, _mm_set1_epi16(56));
				input2_epi16 = _mm_mullo_epi16(input2_epi16, _mm_set1_epi16(56));
				input1_epi16 = _mm_srai_epi16(input1_epi16, 4);
				input2_epi16 = _mm_srai_epi16(input2_epi16, 4);
				input1_epi16 = _mm_adds_epi16(input1_epi16, _mm_set1_epi16(64));
				input2_epi16 = _mm_adds_epi16(input2_epi16, _mm_set1_epi16(64));
			}
			else
			{
				input1_epi16 = _mm_slli_epi16(input1_epi16, shift);
				input2_epi16 = _mm_slli_epi16(input2_epi16, shift);
			}


			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(temp_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_epi16);
#else
			// Unpack the first four v chroma pixels from each row
			input1_epi16 = _mm_srli_epi32(input1_epu8, 24);
			input2_epi16 = _mm_srli_epi32(input2_epu8, 24);

			// Preload the next set of packed pixels
			input1_epu8 = _mm_load_si128(input1_ptr++);
			input2_epu8 = _mm_load_si128(input2_ptr++);

			// Unpack the second four v chroma pixels from each row
			input3_epi16 = _mm_srli_epi32(input3_epu8, 24);
			input4_epi16 = _mm_srli_epi32(input4_epu8, 24);

			// Combine the two sets of v chroma pixels from each row
			input1_epi16 = _mm_packs_epi32(input1_epi16, input3_epi16);
			input2_epi16 = _mm_packs_epi32(input2_epi16, input4_epi16);

#if _ENCODE_CHROMA_OFFSET
			// Subtract the offset
			input1_epi16 = _mm_subs_epi16(input1_epi16, offset_epi16);
			input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif

			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				input1_epi16 = _mm_mullo_epi16(input1_epi16, _mm_set1_epi16(56));
				input2_epi16 = _mm_mullo_epi16(input2_epi16, _mm_set1_epi16(56));
				input1_epi16 = _mm_srai_epi16(input1_epi16, 4);
				input2_epi16 = _mm_srai_epi16(input2_epi16, 4);
				input1_epi16 = _mm_adds_epi16(input1_epi16, _mm_set1_epi16(64));
				input2_epi16 = _mm_adds_epi16(input2_epi16, _mm_set1_epi16(64));
			}
			else
			{
				input1_epi16 = _mm_slli_epi16(input1_epi16, shift);
				input2_epi16 = _mm_slli_epi16(input2_epi16, shift);
			}


			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(temp_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_epi16);
#endif
		}
	}

	// Should have exited the loop at the post processing column
	assert(column == post_column);
#endif

	// Process the rest of the pixels in the pair of rows
	for (; column < length; column += 4)
	{
		int value1;
		int value2;
		int temp;
		int diff;

		if (channel == 0)
		{
			value1 = row1[column + 0];
#if (0 && STRICT_SATURATE)
			if(value1 < 16) value1 = 16;
			if(value1 > 235) value1 = 235;
#endif
			value2 = row2[column + 0];
#if (0 && STRICT_SATURATE)
			if(value2 < 16) value2 = 16;
			if(value2 > 235) value2 = 235;
#endif
		//	value1 *= 234; // fix for the "Highlight compensation"
		//	value1 >>= 8;
		//	value2 *= 234;
		//	value2 >>= 8;

			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				value1 *= 55;
				value2 *= 55;
				value1 >>= 4;
				value2 >>= 4;
				value1 += 64;
				value2 += 64;
			}
			else
			{
				value1 <<= shift;
				value2 <<= shift;
			}


			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/2] = SATURATE(temp);
			highpass[column/2] = SATURATE(diff);

			value1 = row1[column + 2];
			value2 = row2[column + 2];

			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				value1 *= 55;
				value2 *= 55;
				value1 >>= 4;
				value2 >>= 4;
				value1 += 64;
				value2 += 64;
			}
			else
			{
				value1 <<= shift;
				value2 <<= shift;
			}

			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/2 + 1] = SATURATE(temp);
			highpass[column/2 + 1] = SATURATE(diff);

		}
		else if (channel == 1)
		{
#if _SWAP_CHROMA
			int index = column + 3;
#else
			int index = column + 1;
#endif
			value1 = row1[index];
			value2 = row2[index];

#if _ENCODE_CHROMA_OFFSET
			value1 -= offset;
			value2 -= offset;
#endif

			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				value1 *= 56;
				value2 *= 56;
				value1 >>= 4;
				value2 >>= 4;
				value1 += 64;
				value2 += 64;
			}
			else
			{
				value1 <<= shift;
				value2 <<= shift;
			}

			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/4] = SATURATE(temp);
			highpass[column/4] = SATURATE(diff);
		}
		else
		{
#if _SWAP_CHROMA
			int index = column + 1;
#else
			int index = column + 3;
#endif
			value1 = row1[index];
			value2 = row2[index];

#if _ENCODE_CHROMA_OFFSET
			value1 -= offset;
			value2 -= offset;
#endif

			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				value1 *= 56;
				value2 *= 56;
				value1 >>= 4;
				value2 >>= 4;
				value1 += 64;
				value2 += 64;
			}
			else
			{
				value1 <<= shift;
				value2 <<= shift;
			}

			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/4] = SATURATE(temp);
			highpass[column/4] = SATURATE(diff);
		}
	}
}

#endif


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

// Apply the temporal transform to one channel in the even and odd rows
void FilterTemporalRowUYVYChannelTo16s(uint8_t *row1, uint8_t *row2, int frame_width,
									   int channel, PIXEL *lowpass, PIXEL *highpass,
									   int offset, int precision, int limit_yuv)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Apply the temporal transform to one channel in the even and odd rows
void FilterTemporalRowUYVYChannelTo16s(uint8_t *row1, uint8_t *row2, int frame_width,
									   int channel, PIXEL *lowpass, PIXEL *highpass,
									   int offset, int precision, int limit_yuv)
{
	int column_step = 16;
	int length = frame_width * 2;
	int post_column = length - (length % column_step);
	int column;

#if (1 && XMMOPT)

	__m64 *input1_ptr = (__m64 *)row1;
	__m64 *input2_ptr = (__m64 *)row2;
	__m64 *temp_ptr = (__m64 *)lowpass;
	__m64 *diff_ptr = (__m64 *)highpass;

	__m64 temp1_pi16;
	__m64 diff1_pi16;
	//__m64 temp2_pi16;
	//__m64 diff2_pi16;

	__m64 input1_pu8;
	__m64 input2_pu8;

#if _ENCODE_CHROMA_OFFSET
	__m64 offset_pi16 = _mm_set1_pi16(offset);
#endif
#endif

	column = 0;

#if (1 && XMMOPT)

	// Check that the pointers to the groups of pixels are properly aligned
	//assert(ISALIGNED16(input1_ptr));
	//assert(ISALIGNED16(input2_ptr));

	// Check that the output pointers are properly aligned
	//assert(ISALIGNED16(temp_ptr));
	//assert(ISALIGNED16(diff_ptr));

	// Preload the packed pixels
	input1_pu8 = *(input1_ptr++);
	input2_pu8 = *(input2_ptr++);

	for (; column < post_column; column += column_step)
	{
		__m64 input3_pu8;
		__m64 input4_pu8;
		__m64 input1_pi16;
		__m64 input2_pi16;
		__m64 input3_pi16;
		__m64 input4_pi16;

		// Preload the next set of packed pixels
		input3_pu8 = *(input1_ptr++);
		input4_pu8 = *(input2_ptr++);

		if (channel == 0)
		{
#if (0 && STRICT_SATURATE)
			// Perform strict saturation on luma if required
			input1_pu8 = _mm_subs_pu8(input1_pu8, _mm_set1_pi8(16));
			input1_pu8 = _mm_adds_pu8(input1_pu8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input1_pu8 = _mm_subs_pu8(input1_pu8, _mm_set1_pi8(20));

			input2_pu8 = _mm_subs_pu8(input2_pu8, _mm_set1_pi8(16));
			input2_pu8 = _mm_adds_pu8(input2_pu8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input2_pu8 = _mm_subs_pu8(input2_pu8, _mm_set1_pi8(20));
#endif

			// Unpack the first four luma pixels from each row
			input1_pi16 = _mm_srli_pi16(input1_pu8, 8);
			input2_pi16 = _mm_srli_pi16(input2_pu8, 8);

			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(temp_ptr++) = temp1_pi16;

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			*(diff_ptr++) = diff1_pi16;

			// Preload the next set of packed pixels
			input1_pu8 = *(input1_ptr++);
			input2_pu8 = *(input2_ptr++);

#if (0 && STRICT_SATURATE)
			// Perform strict saturation on luma if required
			input3_pu8 = _mm_subs_pu8(input3_pu8, _mm_set1_pi8(16));
			input3_pu8 = _mm_adds_pu8(input3_pu8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input3_pu8 = _mm_subs_pu8(input3_pu8, _mm_set1_pi8(20));

			input4_pu8 = _mm_subs_pu8(input4_pu8, _mm_set1_pi8(16));
			input4_pu8 = _mm_adds_pu8(input4_pu8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input4_pu8 = _mm_subs_pu8(input4_pu8, _mm_set1_pi8(20));
#endif
			// Unpack the second four luma pixels from each row
			input1_pi16 = _mm_srli_pi16(input3_pu8, 8);
			input2_pi16 = _mm_srli_pi16(input4_pu8, 8);

			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(temp_ptr++) = temp1_pi16;

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			*(diff_ptr++) = diff1_pi16;
		}
		else if (channel == 1)
		{
#if _SWAP_CHROMA
			// Unpack the first two u chroma pixels from each row
			input1_pi16 = _mm_slli_pi32(input1_pu8, 8);
			input2_pi16 = _mm_slli_pi32(input2_pu8, 8);
			input1_pi16 = _mm_srli_pi32(input1_pi16, 24);
			input2_pi16 = _mm_srli_pi32(input2_pi16, 24);

			// Preload the next set of packed pixels
			input1_pu8 = *(input1_ptr++);
			input2_pu8 = *(input2_ptr++);

			// Unpack the second two u chroma pixels from each row
			input3_pi16 = _mm_slli_pi32(input3_pu8, 8);
			input4_pi16 = _mm_slli_pi32(input4_pu8, 8);
			input3_pi16 = _mm_srli_pi32(input3_pi16, 24);
			input4_pi16 = _mm_srli_pi32(input4_pi16, 24);

			// Combine the two sets of u chroma pixels from each row
			input1_pi16 = _mm_packs_pi32(input1_pi16, input3_pi16);
			input2_pi16 = _mm_packs_pi32(input2_pi16, input4_pi16);

#if _ENCODE_CHROMA_OFFSET
			// Subtract the offset
			input1_pi16 = _mm_subs_pi16(input1_pi16, offset_pi16);
			input2_pi16 = _mm_subs_pi16(input2_pi16, offset_pi16);
#endif
			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(temp_ptr++) = temp1_pi16;

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			*(diff_ptr++) = diff1_pi16;
#else

#error This section of code not finished

			// Unpack the first two u chroma pixels from each row
			input1_pi16 = _mm_and_si128(input1_pu8, _mm_set1_pi32(0x000000FF));
			input2_pi16 = _mm_and_si128(input2_pu8, _mm_set1_pi32(0x000000FF));

			// Preload the next set of packed pixels
			input1_pu8 = _mm_load_si128(input1_ptr++);
			input2_pu8 = _mm_load_si128(input2_ptr++);

			// Unpack the second two u chroma pixels from each row
			input3_pi16 = _mm_and_si128(input3_pu8, _mm_set1_pi32(0x000000FF));
			input4_pi16 = _mm_and_si128(input4_pu8, _mm_set1_pi32(0x000000FF));

			// Combine the two sets of u chroma pixels from each row
			input1_pi16 = _mm_packs_pi32(input1_pi16, input3_pi16);
			input2_pi16 = _mm_packs_pi32(input2_pi16, input4_pi16);

			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			_mm_store_si128(temp_ptr++, temp1_pi16);

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_pi16);
#endif
		}
		else
		{
#if _SWAP_CHROMA
			// Unpack the first two v chroma pixels from each row
			input1_pi16 = _mm_and_si64(input1_pu8, _mm_set1_pi32(0x000000FF));
			input2_pi16 = _mm_and_si64(input2_pu8, _mm_set1_pi32(0x000000FF));

			// Preload the next set of packed pixels
			input1_pu8 = *(input1_ptr++);
			input2_pu8 = *(input2_ptr++);

			// Unpack the second two v chroma pixels from each row
			input3_pi16 = _mm_and_si64(input3_pu8, _mm_set1_pi32(0x000000FF));
			input4_pi16 = _mm_and_si64(input4_pu8, _mm_set1_pi32(0x000000FF));

			// Combine the two sets of v chroma pixels from each row
			input1_pi16 = _mm_packs_pi32(input1_pi16, input3_pi16);
			input2_pi16 = _mm_packs_pi32(input2_pi16, input4_pi16);

#if _ENCODE_CHROMA_OFFSET
			// Subtract the offset
			input1_pi16 = _mm_subs_pi16(input1_pi16, offset_pi16);
			input2_pi16 = _mm_subs_pi16(input2_pi16, offset_pi16);
#endif
			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			*(temp_ptr++) = temp1_pi16;

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			*(diff_ptr++) = diff1_pi16;
#else

#error This section of code not finished

			// Unpack the first two v chroma pixels from each row
			input1_pi16 = _mm_slli_pi32(input1_pu8, 8);
			input2_pi16 = _mm_slli_pi32(input2_pu8, 8);
			input1_pi16 = _mm_srli_pi32(input1_pi16, 24);
			input2_pi16 = _mm_srli_pi32(input2_pi16, 24);

			// Preload the next set of packed pixels
			input1_pu8 = _mm_load_si128(input1_ptr++);
			input2_pu8 = _mm_load_si128(input2_ptr++);

			// Unpack the second two v chroma pixels from each row
			input3_pi16 = _mm_slli_pi32(input3_pu8, 8);
			input4_pi16 = _mm_slli_pi32(input4_pu8, 8);
			input3_pi16 = _mm_srli_pi32(input3_pi16, 24);
			input4_pi16 = _mm_srli_pi32(input4_pi16, 24);

			// Combine the two sets of v chroma pixels from each row
			input1_pi16 = _mm_packs_pi32(input1_pi16, input3_pi16);
			input2_pi16 = _mm_packs_pi32(input2_pi16, input4_pi16);

			// Compute and store the sum of the pixels in each row
			temp1_pi16 = _mm_adds_pi16(input1_pi16, input2_pi16);
			_mm_store_si128(temp_ptr++, temp1_pi16);

			// Compute the differences between the pixels in each row
			diff1_pi16 = _mm_subs_pi16(input2_pi16, input1_pi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_pi16);
#endif
		}
	}

	//_mm_empty();		// Clear the mmx register state

	// Should have exited the loop at the post processing column
	assert(column == post_column);
#endif

	// Process the rest of the pixels in the pair of rows
	for (; column < length; column += 4)
	{
		int value1;
		int value2;
		int temp;
		int diff;

		if (channel == 0)
		{
			value1 = row1[column + 1];
#if (0 && STRICT_SATURATE)
			if(value1 < 16) value1 = 16;
			if(value1 > 235) value1 = 235;
#endif
			value2 = row2[column + 1];
#if (0 && STRICT_SATURATE)
			if(value2 < 16) value2 = 16;
			if(value2 > 235) value2 = 235;
#endif

			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/2] = SATURATE(temp);
			highpass[column/2] = SATURATE(diff);

			value1 = row1[column + 3];
			value2 = row2[column + 3];

			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/2 + 1] = SATURATE(temp);
			highpass[column/2 + 1] = SATURATE(diff);

		}
		else if (channel == 1)
		{
#if _SWAP_CHROMA
			int index = column + 2;
#else
			int index = column + 0;
#endif
			value1 = row1[index];
			value2 = row2[index];

#if _ENCODE_CHROMA_OFFSET
			value1 -= offset;
			value2 -= offset;
#endif
			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/4] = SATURATE(temp);
			highpass[column/4] = SATURATE(diff);
		}
		else
		{
#if _SWAP_CHROMA
			int index = column + 0;
#else
			int index = column + 2;
#endif

			value1 = row1[index];
			value2 = row2[index];

#if _ENCODE_CHROMA_OFFSET
			value1 -= offset;
			value2 -= offset;
#endif
			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/4] = SATURATE(temp);
			highpass[column/4] = SATURATE(diff);
		}
	}
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Apply the temporal transform to one channel in the even and odd rows
void FilterTemporalRowUYVYChannelTo16s(uint8_t *row1, uint8_t *row2, int frame_width,
									   int channel, PIXEL *lowpass, PIXEL *highpass,
									   int offset, int precision, int limit_yuv)
{
	int column_step = 32;
	int length = frame_width * 2;
	int post_column = length - (length % column_step);
	int column;
	int shift = precision - 8;

#if (1 && XMMOPT)

	__m128i *input1_ptr = (__m128i *)row1;
	__m128i *input2_ptr = (__m128i *)row2;
	__m128i *temp_ptr = (__m128i *)lowpass;
	__m128i *diff_ptr = (__m128i *)highpass;

	__m128i temp1_epi16;
	__m128i diff1_epi16;
	//__m128i temp2_epi16;
	//__m128i diff2_epi16;

	__m128i input1_epu8;
	__m128i input2_epu8;

#if _ENCODE_CHROMA_OFFSET
	__m128i offset_epi16 = _mm_set1_epi16(offset);
#endif
#endif

	// Adjust the post column so that pixels are not preloaded beyond the end of the row
	if (post_column == length)
		post_column -= column_step;

	// Check that the post column is an integral number of column steps
	assert((post_column % column_step) == 0);

	// Start processing at the first (leftmost) column
	column = 0;

#if (1 && XMMOPT)

	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(input1_ptr));
	assert(ISALIGNED16(input2_ptr));

	// Check that the output pointers are properly aligned
	assert(ISALIGNED16(temp_ptr));
	assert(ISALIGNED16(diff_ptr));

	// Preload the packed pixels
	input1_epu8 = _mm_load_si128(input1_ptr++);
	input2_epu8 = _mm_load_si128(input2_ptr++);

	for (; column < post_column; column += column_step)
	{
		__m128i input3_epu8;
		__m128i input4_epu8;
		__m128i input1_epi16;
		__m128i input2_epi16;
		__m128i input3_epi16;
		__m128i input4_epi16;

		// Preload the next set of packed pixels
		input3_epu8 = _mm_load_si128(input1_ptr++);
		input4_epu8 = _mm_load_si128(input2_ptr++);

		if (channel == 0)
		{
#if (0 && STRICT_SATURATE)
			// Perform strict saturation on luma if required
			input1_epu8 = _mm_subs_epu8(input1_epu8, _mm_set1_epi8(16));
			input1_epu8 = _mm_adds_epu8(input1_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input1_epu8 = _mm_subs_epu8(input1_epu8, _mm_set1_epi8(20));

			input2_epu8 = _mm_subs_epu8(input2_epu8, _mm_set1_epi8(16));
			input2_epu8 = _mm_adds_epu8(input2_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input2_epu8 = _mm_subs_epu8(input2_epu8, _mm_set1_epi8(20));
#endif

			// Unpack the first eight luma pixels from each row
			input1_epi16 = _mm_srli_epi16(input1_epu8, 8);
			input2_epi16 = _mm_srli_epi16(input2_epu8, 8);

			// Increase the pixel depth
			if(limit_yuv && shift == 2)//TODO DAN 20090226 Much this active for Canon 5D
			{
				input1_epi16 = _mm_mullo_epi16(input1_epi16, _mm_set1_epi16(55));
				input2_epi16 = _mm_mullo_epi16(input2_epi16, _mm_set1_epi16(55));
				input1_epi16 = _mm_srai_epi16(input1_epi16, 4);
				input2_epi16 = _mm_srai_epi16(input2_epi16, 4);
				input1_epi16 = _mm_adds_epi16(input1_epi16, _mm_set1_epi16(64));
				input2_epi16 = _mm_adds_epi16(input2_epi16, _mm_set1_epi16(64));
			}
			else
			{
				input1_epi16 = _mm_slli_epi16(input1_epi16, shift);
				input2_epi16 = _mm_slli_epi16(input2_epi16, shift);
			}

			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(temp_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_epi16);

			// Preload the next set of packed pixels
			input1_epu8 = _mm_load_si128(input1_ptr++);
			input2_epu8 = _mm_load_si128(input2_ptr++);

#if (0 && STRICT_SATURATE)
			// Perform strict saturation on luma if required
			input3_epu8 = _mm_subs_epu8(input3_epu8, _mm_set1_epi8(16));
			input3_epu8 = _mm_adds_epu8(input3_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input3_epu8 = _mm_subs_epu8(input3_epu8, _mm_set1_epi8(20));

			input4_epu8 = _mm_subs_epu8(input4_epu8, _mm_set1_epi8(16));
			input4_epu8 = _mm_adds_epu8(input4_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			input4_epu8 = _mm_subs_epu8(input4_epu8, _mm_set1_epi8(20));
#endif
			// Unpack the second eight luma pixels from each row
			input1_epi16 = _mm_srli_epi16(input3_epu8, 8);
			input2_epi16 = _mm_srli_epi16(input4_epu8, 8);

			// Increase the pixel depth
			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				input1_epi16 = _mm_mullo_epi16(input1_epi16, _mm_set1_epi16(55));
				input2_epi16 = _mm_mullo_epi16(input2_epi16, _mm_set1_epi16(55));
				input1_epi16 = _mm_srai_epi16(input1_epi16, 4);
				input2_epi16 = _mm_srai_epi16(input2_epi16, 4);
				input1_epi16 = _mm_adds_epi16(input1_epi16, _mm_set1_epi16(64));
				input2_epi16 = _mm_adds_epi16(input2_epi16, _mm_set1_epi16(64));
			}
			else
			{
				input1_epi16 = _mm_slli_epi16(input1_epi16, shift);
				input2_epi16 = _mm_slli_epi16(input2_epi16, shift);
			}

			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(temp_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_epi16);
		}
		else if (channel == 1)
		{
#if _SWAP_CHROMA
			// Unpack the first four u chroma pixels from each row
			input1_epi16 = _mm_slli_epi32(input1_epu8, 8);
			input2_epi16 = _mm_slli_epi32(input2_epu8, 8);
			input1_epi16 = _mm_srli_epi32(input1_epi16, 24);
			input2_epi16 = _mm_srli_epi32(input2_epi16, 24);

			// Preload the next set of packed pixels
			input1_epu8 = _mm_load_si128(input1_ptr++);
			input2_epu8 = _mm_load_si128(input2_ptr++);

			// Unpack the second four u chroma pixels from each row
			input3_epi16 = _mm_slli_epi32(input3_epu8, 8);
			input4_epi16 = _mm_slli_epi32(input4_epu8, 8);
			input3_epi16 = _mm_srli_epi32(input3_epi16, 24);
			input4_epi16 = _mm_srli_epi32(input4_epi16, 24);

			// Combine the two sets of u chroma pixels from each row
			input1_epi16 = _mm_packs_epi32(input1_epi16, input3_epi16);
			input2_epi16 = _mm_packs_epi32(input2_epi16, input4_epi16);

#if _ENCODE_CHROMA_OFFSET
			// Subtract the offset
			input1_epi16 = _mm_subs_epi16(input1_epi16, offset_epi16);
			input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif
			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				input1_epi16 = _mm_mullo_epi16(input1_epi16, _mm_set1_epi16(56));
				input2_epi16 = _mm_mullo_epi16(input2_epi16, _mm_set1_epi16(56));
				input1_epi16 = _mm_srai_epi16(input1_epi16, 4);
				input2_epi16 = _mm_srai_epi16(input2_epi16, 4);
				input1_epi16 = _mm_adds_epi16(input1_epi16, _mm_set1_epi16(64));
				input2_epi16 = _mm_adds_epi16(input2_epi16, _mm_set1_epi16(64));
			}
			else
			{
				input1_epi16 = _mm_slli_epi16(input1_epi16, shift);
				input2_epi16 = _mm_slli_epi16(input2_epi16, shift);
			}

			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(temp_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_epi16);
#else
			// Unpack the first four u chroma pixels from each row
			input1_epi16 = _mm_and_si128(input1_epu8, _mm_set1_epi32(0x000000FF));
			input2_epi16 = _mm_and_si128(input2_epu8, _mm_set1_epi32(0x000000FF));

			// Preload the next set of packed pixels
			input1_epu8 = _mm_load_si128(input1_ptr++);
			input2_epu8 = _mm_load_si128(input2_ptr++);

			// Unpack the second four u chroma pixels from each row
			input3_epi16 = _mm_and_si128(input3_epu8, _mm_set1_epi32(0x000000FF));
			input4_epi16 = _mm_and_si128(input4_epu8, _mm_set1_epi32(0x000000FF));

			// Combine the two sets of u chroma pixels from each row
			input1_epi16 = _mm_packs_epi32(input1_epi16, input3_epi16);
			input2_epi16 = _mm_packs_epi32(input2_epi16, input4_epi16);

#if _ENCODE_CHROMA_OFFSET
			// Subtract the offset
			input1_epi16 = _mm_subs_epi16(input1_epi16, offset_epi16);
			input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif
			// Increase the pixel depth
			input1_epi16 = _mm_slli_epi16(input1_epi16, shift);
			input2_epi16 = _mm_slli_epi16(input2_epi16, shift);

			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(temp_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_epi16);
#endif
		}
		else
		{
#if _SWAP_CHROMA
			// Unpack the first four v chroma pixels from each row
			input1_epi16 = _mm_and_si128(input1_epu8, _mm_set1_epi32(0x000000FF));
			input2_epi16 = _mm_and_si128(input2_epu8, _mm_set1_epi32(0x000000FF));

			// Preload the next set of packed pixels
			input1_epu8 = _mm_load_si128(input1_ptr++);
			input2_epu8 = _mm_load_si128(input2_ptr++);

			// Unpack the second four v chroma pixels from each row
			input3_epi16 = _mm_and_si128(input3_epu8, _mm_set1_epi32(0x000000FF));
			input4_epi16 = _mm_and_si128(input4_epu8, _mm_set1_epi32(0x000000FF));

			// Combine the two sets of v chroma pixels from each row
			input1_epi16 = _mm_packs_epi32(input1_epi16, input3_epi16);
			input2_epi16 = _mm_packs_epi32(input2_epi16, input4_epi16);

#if _ENCODE_CHROMA_OFFSET
			// Subtract the offset
			input1_epi16 = _mm_subs_epi16(input1_epi16, offset_epi16);
			input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif
			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				input1_epi16 = _mm_mullo_epi16(input1_epi16, _mm_set1_epi16(56));
				input2_epi16 = _mm_mullo_epi16(input2_epi16, _mm_set1_epi16(56));
				input1_epi16 = _mm_srai_epi16(input1_epi16, 4);
				input2_epi16 = _mm_srai_epi16(input2_epi16, 4);
				input1_epi16 = _mm_adds_epi16(input1_epi16, _mm_set1_epi16(64));
				input2_epi16 = _mm_adds_epi16(input2_epi16, _mm_set1_epi16(64));
			}
			else
			{
				input1_epi16 = _mm_slli_epi16(input1_epi16, shift);
				input2_epi16 = _mm_slli_epi16(input2_epi16, shift);
			}

			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(temp_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_epi16);
#else
			// Unpack the first four v chroma pixels from each row
			input1_epi16 = _mm_slli_epi32(input1_epu8, 8);
			input2_epi16 = _mm_slli_epi32(input2_epu8, 8);
			input1_epi16 = _mm_srli_epi32(input1_epi16, 24);
			input2_epi16 = _mm_srli_epi32(input2_epi16, 24);

			// Preload the next set of packed pixels
			input1_epu8 = _mm_load_si128(input1_ptr++);
			input2_epu8 = _mm_load_si128(input2_ptr++);

			// Unpack the second four v chroma pixels from each row
			input3_epi16 = _mm_slli_epi32(input3_epu8, 8);
			input4_epi16 = _mm_slli_epi32(input4_epu8, 8);
			input3_epi16 = _mm_srli_epi32(input3_epi16, 24);
			input4_epi16 = _mm_srli_epi32(input4_epi16, 24);

			// Combine the two sets of v chroma pixels from each row
			input1_epi16 = _mm_packs_epi32(input1_epi16, input3_epi16);
			input2_epi16 = _mm_packs_epi32(input2_epi16, input4_epi16);

#if _ENCODE_CHROMA_OFFSET
			// Subtract the offset
			input1_epi16 = _mm_subs_epi16(input1_epi16, offset_epi16);
			input2_epi16 = _mm_subs_epi16(input2_epi16, offset_epi16);
#endif
			// Increase the pixel depth
			input1_epi16 = _mm_slli_epi16(input1_epi16, shift);
			input2_epi16 = _mm_slli_epi16(input2_epi16, shift);

			// Compute and store the sum of the pixels in each row
			temp1_epi16 = _mm_adds_epi16(input1_epi16, input2_epi16);
			_mm_store_si128(temp_ptr++, temp1_epi16);

			// Compute the differences between the pixels in each row
			diff1_epi16 = _mm_subs_epi16(input2_epi16, input1_epi16);

			// Store the differences between the pixels in each row
			_mm_store_si128(diff_ptr++, diff1_epi16);
#endif
		}
	}

	// Should have exited the loop at the post processing column
	assert(column == post_column);
#endif

	// Process the rest of the pixels in the pair of rows
	for (; column < length; column += 4)
	{
		int value1;
		int value2;
		int temp;
		int diff;

		if (channel == 0)
		{
			value1 = row1[column + 1];
			value2 = row2[column + 1];

#if (0 && STRICT_SATURATE)
			if(value1 < 16) value1 = 16;
			else if(value1 > 235) value1 = 235;

			if(value2 < 16) value2 = 16;
			else if(value2 > 235) value2 = 235;
#endif
			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				value1 *= 55;
				value2 *= 55;
				value1 >>= 4;
				value2 >>= 4;
				value1 += 64;
				value2 += 64;
			}
			else
			{
				value1 <<= shift;
				value2 <<= shift;
			}

			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/2] = SATURATE(temp);
			highpass[column/2] = SATURATE(diff);

			value1 = row1[column + 3];
			value2 = row2[column + 3];

#if (0 && STRICT_SATURATE)
			if(value1 < 16) value1 = 16;
			else if(value1 > 235) value1 = 235;

			if(value2 < 16) value2 = 16;
			else if(value2 > 235) value2 = 235;
#endif
			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				value1 *= 55;
				value2 *= 55;
				value1 >>= 4;
				value2 >>= 4;
				value1 += 64;
				value2 += 64;
			}
			else
			{
				value1 <<= shift;
				value2 <<= shift;
			}

			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/2 + 1] = SATURATE(temp);
			highpass[column/2 + 1] = SATURATE(diff);
		}
		else if (channel == 1)
		{
#if _SWAP_CHROMA
			int index = column + 2;
#else
			int index = column + 0;
#endif
			value1 = row1[index];
			value2 = row2[index];

#if _ENCODE_CHROMA_OFFSET
			value1 -= offset;
			value2 -= offset;
#endif
			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				value1 *= 56;
				value2 *= 56;
				value1 >>= 4;
				value2 >>= 4;
				value1 += 64;
				value2 += 64;
			}
			else
			{
				value1 <<= shift;
				value2 <<= shift;
			}
			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/4] = SATURATE(temp);
			highpass[column/4] = SATURATE(diff);
		}
		else
		{
#if _SWAP_CHROMA
			int index = column + 0;
#else
			int index = column + 2;
#endif
			value1 = row1[index];
			value2 = row2[index];

#if _ENCODE_CHROMA_OFFSET
			value1 -= offset;
			value2 -= offset;
#endif
			if(limit_yuv && shift == 2) //TODO DAN 20090226 Much this active for Canon 5D
			{
				value1 *= 56;
				value2 *= 56;
				value1 >>= 4;
				value2 >>= 4;
				value1 += 64;
				value2 += 64;
			}
			else
			{
				value1 <<= shift;
				value2 <<= shift;
			}

			temp = value1 + value2;
			diff = value2 - value1;

			lowpass[column/4] = SATURATE(temp);
			highpass[column/4] = SATURATE(diff);
		}
	}
}

#endif


// Invert the temporal transform between two images of 16-bit signed pixels
void InvertTemporal16s(PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					   PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi)
{
#if _HIGHPASS_8S

	int column_step = 16;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins
	PIXEL8S *highrow;
	int row, column;

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 16) == 0);

	// Compute the column where end of row processing must begin
	post_column = roi.width - (roi.width % column_step);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
		__m128i *lowpass_ptr = (__m128i *)lowpass;
		__m128i *highpass_ptr = (__m128i *)highpass;
		__m128i *even_ptr = (__m128i *)field1;
		__m128i *odd_ptr = (__m128i *)field2;

		// Process column elements in parallel until end of row processing is required
		for (column = 0; column < post_column; column += column_step)
		{
			__m128i lowpass_epi16;		// Eight lowpass coefficient
			__m128i highpass_epi8;		// Sixteen highpass coefficients
			__m128i high1_epi16;		// Eight highpass coefficients
			__m128i high2_epi16;		// Eight highpass coefficients
			__m128i sign_epi8;
			__m128i even_epi16;
			__m128i odd_epi16;

			// Check that the pointers to the next groups of pixels are properly aligned
			assert(ISALIGNED16(lowpass_ptr));
			assert(ISALIGNED16(highpass_ptr));

			// Load sixteen highpass coefficients
			highpass_epi8 = _mm_load_si128(highpass_ptr++);
			sign_epi8 = _mm_cmplt_epi8(highpass_epi8, _mm_setzero_si128());

			// Load the first eight lowpass coefficients
			lowpass_epi16 = _mm_load_si128(lowpass_ptr++);

			// Unpack the first (lower) eight highpass coefficients
			high1_epi16 = _mm_unpacklo_epi8(highpass_epi8, sign_epi8);

			// Reconstruct eight pixels in the first field
			even_epi16 = _mm_subs_epi16(lowpass_epi16, high1_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);
			_mm_store_si128(even_ptr++, even_epi16);

			// Reconstruct eight pixels in the second field
			odd_epi16 = _mm_adds_epi16(lowpass_epi16, high1_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
			_mm_store_si128(odd_ptr++, odd_epi16);

			// Load the next eight lowpass coefficients
			lowpass_epi16 = _mm_load_si128(lowpass_ptr++);

			// Unpack the second (upper) eight highpass coefficients
			high2_epi16 = _mm_unpackhi_epi8(highpass_epi8, sign_epi8);

			// Reconstruct eight pixels in the first field
			even_epi16 = _mm_subs_epi16(lowpass_epi16, high2_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);
			_mm_store_si128(even_ptr++, even_epi16);

			// Reconstruct eight pixels in the second field
			odd_epi16 = _mm_adds_epi16(lowpass_epi16, high2_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
			_mm_store_si128(odd_ptr++, odd_epi16);
		}

		// Handle end of row processing for the remaining columns
		highrow = (PIXEL8S *)highpass;
		for (; column < roi.width; column++) {
			// Get the lowpass and highpass coefficients
			PIXEL low = lowpass[column];
			PIXEL high = highrow[column];

			// Reconstruct the pixels in the even and odd rows
			field1[column] = (low - high)/2;
			field2[column] = (low + high)/2;
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}

#else

	// Have not written the version that uses 16-bit pixels for the highpass band
	assert(0);

#endif
}

// Apply the temporal transform between two images of 8-bit unsigned pixels
void FilterTemporal8u(PIXEL8U *field1, int pitch1, PIXEL8U *field2, int pitch2,
					  PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					  ROI roi)
{
	assert(0);
}

// Invert the temporal transform between two images of 8-bit unsigned pixels
void InvertTemporalTo8u(PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
						PIXEL8U *field1, int pitch1, PIXEL8U *field2, int pitch2, ROI roi)
{
	PIXEL *lowpass_row_ptr = lowpass;
	PIXEL *highpass_row_ptr = highpass;
	PIXEL8U *field1_row_ptr = field1;
	PIXEL8U *field2_row_ptr = field2;
	int row, column;

	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	pitch1 /= sizeof(PIXEL8S);
	pitch2 /= sizeof(PIXEL8S);

	for (row = 0; row < roi.height; row++) {
		for (column = 0; column < roi.width; column++) {
			int low = lowpass_row_ptr[column];
			int high = highpass_row_ptr[column];

			field1[column] = low + high;
			field2[column] = low - high;
		}

		lowpass_row_ptr += lowpass_pitch;
		highpass_row_ptr += highpass_pitch;
		field1_row_ptr += pitch1;
		field2_row_ptr += pitch2;
	}
}


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void FilterInterlaced(PIXEL *frame, int frame_pitch,
					  PIXEL *lowpass, int lowpass_pitch,
					  PIXEL *highpass, int highpass_pitch,
					  ROI roi)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Apply the forward temporal transform between the even and odd rows
// of an interlaced frame.  This version supports in place computation.
void FilterInterlaced(PIXEL *frame, int frame_pitch,
					  PIXEL *lowpass, int lowpass_pitch,
					  PIXEL *highpass, int highpass_pitch,
					  ROI roi)
{
	int row, column;

	// Convert pitch to units of pixels
	frame_pitch /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 16) == 0);

	// Process each pair of even and odd rows
	for (row = 0; row < roi.height; row += 2)
	{

#if (1 && MMXOPT)

		__m64 *evenptr = (__m64 *)frame;
		__m64 *oddptr = (__m64 *)(frame + frame_pitch);
		__m64 *lowptr = (__m64 *)lowpass;
		__m64 *highptr = (__m64 *)highpass;
		int column_step = 4;

		// The processing loops requires an integral number of four pixel blocks
		assert((roi.width % 4) == 0);

		for (column = 0; column < roi.width; column += column_step)
		{
			// Load the input values which may be overwritten
			__m64 even_pi16 = *(evenptr++);
			__m64 odd_pi16 = *(oddptr++);

			// Compute the sum and differences of the pixels in each row
			*(lowptr++) = _mm_adds_pi16(even_pi16, odd_pi16);
			*(highptr++) = _mm_subs_pi16(odd_pi16, even_pi16);
		}
#else
		PIXEL *evenptr = frame;
		PIXEL *oddptr = frame + frame_pitch;
		PIXEL *lowptr = lowpass;
		PIXEL *highptr = highpass;

		for (column = 0; column < roi.width; column++)
		{
			// Get the input value since it may be overwritten
			PIXEL even = *(evenptr++);
			PIXEL odd = *(oddptr++);

			// Compute the sum and differences of the pixels in each row
			*(lowptr++) = even + odd;
			*(highptr++) = odd - even;
		}
#endif
		// Advance to the next input and output rows
		frame += 2 * frame_pitch;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Apply the forward temporal transform between the even and odd rows
// of an interlaced frame.  This version supports in place computation.
void FilterInterlaced(PIXEL *frame, int frame_pitch,
					  PIXEL *lowpass, int lowpass_pitch,
					  PIXEL *highpass, int highpass_pitch,
					  ROI roi)
{
	int row, column;

	// Convert pitch to units of pixels
	frame_pitch /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 16) == 0);
	assert((roi.width % 8) == 0);

	// Process each pair of even and odd rows
	for (row = 0; row < roi.height; row += 2)
	{
#if 1
		__m128i *evenptr = (__m128i *)frame;
		__m128i *oddptr = (__m128i *)(frame + frame_pitch);
		__m128i *lowptr = (__m128i *)lowpass;
		__m128i *highptr = (__m128i *)highpass;
		int column_step = 8;

		for (column = 0; column < roi.width; column += column_step)
		{
			__m128i even_epi16;
			__m128i odd_epi16;

			// Check that the pointers to the next groups of pixels are properly aligned
			assert(ISALIGNED16(evenptr));
			assert(ISALIGNED16(oddptr));

			// Load the input values which may be overwritten
			even_epi16 = _mm_load_si128(evenptr++);
			odd_epi16 = _mm_load_si128(oddptr++);

			// Compute the sum and differences of the pixels in each row
			_mm_store_si128(lowptr++, _mm_adds_epi16(even_epi16, odd_epi16));
			_mm_store_si128(highptr++, _mm_subs_epi16(odd_epi16, even_epi16));
		}
#elif 0
		__m64 *evenptr = (__m64 *)frame;
		__m64 *oddptr = (__m64 *)(frame + frame_pitch);
		__m64 *lowptr = (__m64 *)lowpass;
		__m64 *highptr = (__m64 *)highpass;
		int column_step = 4;

		for (column = 0; column < roi.width; column += column_step)
		{
			// Load the input values which may be overwritten
			__m64 even_pi16 = *(evenptr++);
			__m64 odd_pi16 = *(oddptr++);

			// Compute the sum and differences of the pixels in each row
			*(lowptr++) = _mm_adds_pi16(even_pi16, odd_pi16);
			*(highptr++) = _mm_subs_pi16(odd_pi16, even_pi16);
		}
#else
		PIXEL *evenptr = frame;
		PIXEL *oddptr = frame + frame_pitch;
		PIXEL *lowptr = lowpass;
		PIXEL *highptr = highpass;

		for (column = 0; column < roi.width; column++)
		{
			// Get the input value since it may be overwritten
			PIXEL even = *(evenptr++);
			PIXEL odd = *(oddptr++);

			// Compute the sum and differences of the pixels in each row
			*(lowptr++) = even + odd;
			*(highptr++) = odd - even;
		}
#endif
		// Advance to the next input and output rows
		frame += 2 * frame_pitch;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}

#endif





#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertInterlaced16s(PIXEL *lowpass, int lowpass_pitch,
						 PIXEL *highpass, int highpass_pitch,
						 PIXEL *even, int even_pitch,
						 PIXEL *odd, int odd_pitch,
						 ROI roi)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif


// Apply inverse temporal transform to reconstruct two fields
void InvertInterlaced16s(PIXEL *lowpass, int lowpass_pitch,
						 PIXEL *highpass, int highpass_pitch,
						 PIXEL *even, int even_pitch,
						 PIXEL *odd, int odd_pitch,
						 ROI roi)
{
	PIXEL *lowptr;
	PIXEL *highptr;
	PIXEL *evenptr;
	PIXEL *oddptr;
	int row, column;

	// Convert pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	even_pitch /= sizeof(PIXEL);
	odd_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of 8 word blocks
	//assert((roi.width % 8) == 0);

	// Process each pair of lowpass and highpass rows
	for (row = 0; row < roi.height; row++)
	{
#if (1 && XMMOPT)
		__m64 *lowptr = (__m64 *)lowpass;
		__m64 *highptr = (__m64 *)highpass;
		__m64 *evenptr = (__m64 *)even;
		__m64 *oddptr = (__m64 *)odd;
		int column_step = 4;
		int post_column = roi.width - (roi.width % column_step);
#endif
		// Start at the beginning of the row
		column = 0;

#if (1 && XMMOPT)
		// MMX optimization
		for (; column < post_column; column += column_step)
		{
			__m64 low_pi16;
			__m64 high_pi16;
			__m64 even_pi16;
			__m64 odd_pi16;

			// Get four lowpass and four highpass coefficients
			low_pi16 = *(lowptr++);
			high_pi16 = *(highptr++);

			// Reconstruct the pixels in the even row
			even_pi16 = _mm_subs_pi16(low_pi16, high_pi16);
			even_pi16 = _mm_srai_pi16(even_pi16, 1);
			*(evenptr++) = even_pi16;

			// Reconstruct the pixels in the odd row
			odd_pi16 = _mm_adds_pi16(low_pi16, high_pi16);
			odd_pi16 = _mm_srai_pi16(odd_pi16, 1);
			*(oddptr++) = odd_pi16;
		}

		// The loop should have terminated at the post processing column
		assert(column == post_column);
#endif
		for (; column < roi.width; column++)
		{
			// Get the lowpass and highpass coefficients
			PIXEL low = lowpass[column];
			PIXEL high = highpass[column];

			// Reconstruct the pixels in the even and odd rows
			odd[column] = (low + high)/2;
			even[column] = (low - high)/2;
		}

		// Advance to the next input and output rows
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		even += even_pitch;
		odd += odd_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}
#endif //generic


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif


// Apply inverse temporal transform to reconstruct two fields
void InvertInterlaced16s(PIXEL *lowpass, int lowpass_pitch,
						 PIXEL *highpass, int highpass_pitch,
						 PIXEL *even, int even_pitch,
						 PIXEL *odd, int odd_pitch,
						 ROI roi)
{
	int row, column;

	// Convert pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	even_pitch /= sizeof(PIXEL);
	odd_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of 8 word blocks
	//assert((roi.width % 8) == 0);

	// Process each pair of lowpass and highpass rows
	for (row = 0; row < roi.height; row++)
	{
#if (1 && XMMOPT)
		// SSE2 optimization
		__m128i *lowptr = (__m128i *)lowpass;
		__m128i *highptr = (__m128i *)highpass;
		__m128i *evenptr = (__m128i *)even;
		__m128i *oddptr = (__m128i *)odd;
		int column_step = 8;
		int post_column = roi.width - (roi.width % column_step);
#endif
		// Start at the beginning of the row
		column = 0;

#if (1 && XMMOPT)
		// SSE2 optimization
		for (; column < post_column; column += column_step)
		{
			__m128i low_epi16;
			__m128i high_epi16;
			__m128i even_epi16;
			__m128i odd_epi16;

			// Check that the pointers to the next groups of pixels are properly aligned
			assert(ISALIGNED16(lowptr));
			assert(ISALIGNED16(highptr));

			// Get four lowpass and four highpass coefficients
			low_epi16 = _mm_load_si128(lowptr++);
			high_epi16 = _mm_load_si128(highptr++);

			// Reconstruct the pixels in the even row
			even_epi16 = _mm_subs_epi16(low_epi16, high_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);
			_mm_store_si128(evenptr++, even_epi16);

			// Reconstruct the pixels in the odd row
			odd_epi16 = _mm_adds_epi16(low_epi16, high_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
			_mm_store_si128(oddptr++, odd_epi16);
		}

		// The loop should have terminated at the post processing column
		assert(column == post_column);
#endif
		for (; column < roi.width; column++)
		{
			// Get the lowpass and highpass coefficients
			PIXEL low = lowpass[column];
			PIXEL high = highpass[column];

			// Reconstruct the pixels in the even and odd rows
			odd[column] = (low + high)/2;
			even[column] = (low - high)/2;
		}

		// Advance to the next input and output rows
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		even += even_pitch;
		odd += odd_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}

#endif //p4 dispatch


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertInterlaced16sTo8u(PIXEL16S *lowpass, int lowpass_pitch,
							 PIXEL16S *highpass, int highpass_pitch,
							 PIXEL8U *even_field, int even_pitch,
							 PIXEL8U *odd_field, int odd_pitch, ROI roi)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Apply the inverse temporal transform to reconstruct two fields with
// 8-bit pixels from 16-bit signed lowpass and highpass coefficients.
// The version is optimized using MMX instructions.
void InvertInterlaced16sTo8u(PIXEL16S *lowpass, int lowpass_pitch,
							 PIXEL16S *highpass, int highpass_pitch,
							 PIXEL8U *even_field, int even_pitch,
							 PIXEL8U *odd_field, int odd_pitch, ROI roi)
{
	int row, column;

	// Convert pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL16S);
	highpass_pitch /= sizeof(PIXEL16S);
	even_pitch /= sizeof(PIXEL8U);
	odd_pitch /= sizeof(PIXEL8U);

	// Process each pair of lowpass and highpass rows
	for (row = 0; row < roi.height; row++)
	{
#if (1 && XMMOPT)
		int column_step = 8;
		int post_column = roi.width - (roi.width % column_step);
		int preload_column = post_column - column_step;

		__m64 *low_ptr = (__m64 *)lowpass;
		__m64 *high_ptr = (__m64 *)highpass;
		__m64 *even_ptr = (__m64 *)even_field;
		__m64 *odd_ptr = (__m64 *)odd_field;

		__m64 low1_pi16;
		__m64 high1_pi16;
#endif

#if 0
		// Check that the pointers to the next row of pixels are properly aligned
		assert(ISALIGNED16(low_ptr));
		assert(ISALIGNED16(high_ptr));
		assert(ISALIGNED16(even_ptr));
		assert(ISALIGNED16(odd_ptr));
#endif

		// Start at the beginning of the row
		column = 0;

#if (1 && XMMOPT)

		// Preload four lowpass and four highpass coefficients
		low1_pi16 = *(low_ptr++);
		high1_pi16 = *(high_ptr++);

		for (; column < post_column; column += column_step)
		{
			__m64 low2_pi16;
			__m64 high2_pi16;
			__m64 odd1_pi16;
			__m64 odd2_pi16;
			__m64 even1_pi16;
			__m64 even2_pi16;
			__m64 odd_pu8;
			__m64 even_pu8;

			// Preload the next four lowpass and highpass coefficients
			low2_pi16 = *(low_ptr++);
			high2_pi16 = *(high_ptr++);


			/***** Reconstruct the first four pixels in the even and odd rows *****/

			// Reconstruct the pixels in the even row
			even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
			even1_pi16 = _mm_srai_pi16(even1_pi16, 1);

			// Reconstruct the pixels in the odd row
			odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
			odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1);


			if (column < preload_column)
			{
				// Preload the lowpass and highpass coefficients for the next iteration
				low1_pi16 = *(low_ptr++);
				high1_pi16 = *(high_ptr++);
			}


			/***** Reconstruct the second four pixels in the even and odd rows *****/

			// Reconstruct the pixels in the even row
			even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
			even2_pi16 = _mm_srai_pi16(even2_pi16, 1);

			// Reconstruct the pixels in the odd row
			odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
			odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1);


			/***** Combine the even and odd pixels from the two stages *****/

			// Pack and store sixteen bytes of pixels in the even row
			even_pu8 = _mm_packs_pu16(even1_pi16, even2_pi16);
			//_mm_store_si128(even_ptr++, even_pu8);
			*(even_ptr++) = even_pu8;

			// Pack and store sixteen bytes of pixels in the odd row
			odd_pu8 = _mm_packs_pu16(odd1_pi16, odd2_pi16);
			//_mm_store_si128(odd_ptr++, odd_pu8);
			*(odd_ptr++) = odd_pu8;
		}

		// The loop should have terminated at the post processing column
		assert(column == post_column);
#endif

		for (; column < roi.width; column++)
		{
			// Get the lowpass and highpass coefficients
			PIXEL low = lowpass[column];
			PIXEL high = highpass[column];

			// Reconstruct the pixels in the even and odd rows
			odd_field[column] = (low + high)/2;
			even_field[column] = (low - high)/2;
		}

		// Advance to the next input and output rows
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		even_field += even_pitch;
		odd_field += odd_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Apply the inverse temporal transform to reconstruct two fields with
// 8-bit pixels from 16-bit signed lowpass and highpass coefficients.
// This version is optimized using SSE2 instructions for the Pentium 4.
void InvertInterlaced16sTo8u(PIXEL16S *lowpass, int lowpass_pitch,
							 PIXEL16S *highpass, int highpass_pitch,
							 PIXEL8U *even_field, int even_pitch,
							 PIXEL8U *odd_field, int odd_pitch, ROI roi)
{
	int row, column;

	// Convert pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL16S);
	highpass_pitch /= sizeof(PIXEL16S);
	even_pitch /= sizeof(PIXEL8U);
	odd_pitch /= sizeof(PIXEL8U);

	// Process each pair of lowpass and highpass rows
	for (row = 0; row < roi.height; row++)
	{
#if (1 && XMMOPT)
		int column_step = 16;
		int post_column = roi.width - (roi.width % column_step);
		int preload_column = post_column - column_step;

		__m128i *low_ptr = (__m128i *)lowpass;
		__m128i *high_ptr = (__m128i *)highpass;
		__m128i *even_ptr = (__m128i *)even_field;
		__m128i *odd_ptr = (__m128i *)odd_field;

		__m128i low1_epi16;
		__m128i high1_epi16;
#endif

		// Check that the pointers to the next row of pixels are properly aligned
		assert(ISALIGNED16(low_ptr));
		assert(ISALIGNED16(high_ptr));
		assert(ISALIGNED16(even_ptr));
		assert(ISALIGNED16(odd_ptr));

		// Start at the beginning of the row
		column = 0;

#if (1 && XMMOPT)

		// Preload four lowpass and four highpass coefficients
		low1_epi16 = _mm_load_si128(low_ptr++);
		high1_epi16 = _mm_load_si128(high_ptr++);

		for (; column < post_column; column += column_step)
		{
			__m128i low2_epi16;
			__m128i high2_epi16;
			__m128i odd1_epi16;
			__m128i odd2_epi16;
			__m128i even1_epi16;
			__m128i even2_epi16;
			__m128i odd_epu8;
			__m128i even_epu8;

			// Preload the next four lowpass and highpass coefficients
			low2_epi16 = _mm_load_si128(low_ptr++);
			high2_epi16 = _mm_load_si128(high_ptr++);


			/***** Reconstruct the first eight pixels in the even and odd rows *****/

			// Reconstruct the pixels in the even row
			even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

			// Reconstruct the pixels in the odd row
			odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);


			if (column < preload_column)
			{
				// Preload the lowpass and highpass coefficients for the next iteration
				low1_epi16 = _mm_load_si128(low_ptr++);
				high1_epi16 = _mm_load_si128(high_ptr++);
			}


			/***** Reconstruct the second eight pixels in the even and odd rows *****/

			// Reconstruct the pixels in the even row
			even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
			even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

			// Reconstruct the pixels in the odd row
			odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
			odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);


			/***** Combine the even and odd pixels from the two stages *****/

			// Pack and store sixteen bytes of pixels in the even row
			even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);
			_mm_store_si128(even_ptr++, even_epu8);

			// Pack and store sixteen bytes of pixels in the odd row
			odd_epu8 = _mm_packus_epi16(odd1_epi16, odd2_epi16);
			_mm_store_si128(odd_ptr++, odd_epu8);
		}

		// The loop should have terminated at the post processing column
		assert(column == post_column);
#endif

		for (; column < roi.width; column++)
		{
			// Get the lowpass and highpass coefficients
			PIXEL low = lowpass[column];
			PIXEL high = highpass[column];

			// Reconstruct the pixels in the even and odd rows
			odd_field[column] = (low + high)/2;
			even_field[column] = (low - high)/2;
		}

		// Advance to the next input and output rows
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		even_field += even_pitch;
		odd_field += odd_pitch;
	}
}

#endif


#if 0
// Apply the inverse temporal transform to reconstruct an interlaced
// color frame of packed YUV planes from 16-bit signed coefficients
void InvertInterlaced16sToYUV(PIXEL16S *y_lowpass, int y_lowpass_pitch,
							  PIXEL16S *y_highpass, int y_highpass_pitch,
							  PIXEL16S *u_lowpass, int u_lowpass_pitch,
							  PIXEL16S *u_highpass, int u_highpass_pitch,
							  PIXEL16S *v_lowpass, int v_lowpass_pitch,
							  PIXEL16S *v_highpass, int v_highpass_pitch,
							  PIXEL8U *output, int output_pitch, ROI roi)
{
	PIXEL8U *even_field;
	PIXEL8U *odd_field;
	int field_pitch;
	int row, column;

	// Set pointers into the interlaced rows
	output_pitch /= sizeof(PIXEL8U);
	even_field = output;
	odd_field = even_field + output_pitch;
	field_pitch = 2 * output_pitch;

	// Convert pitch to units of pixels
	y_lowpass_pitch /= sizeof(PIXEL16S);
	u_lowpass_pitch /= sizeof(PIXEL16S);
	v_lowpass_pitch /= sizeof(PIXEL16S);
	y_highpass_pitch /= sizeof(PIXEL16S);
	u_highpass_pitch /= sizeof(PIXEL16S);
	v_highpass_pitch /= sizeof(PIXEL16S);

	// Process each pair of lowpass and highpass rows
	for (row = 0; row < roi.height; row++)
	{
#if (1 && XMMOPT)
		int chroma_width = roi.width/2;
		int column_step = 16;
		int post_column = chroma_width - (chroma_width % column_step);

		__m128i *y_low_ptr = (__m128i *)y_lowpass;
		__m128i *u_low_ptr = (__m128i *)u_lowpass;
		__m128i *v_low_ptr = (__m128i *)v_lowpass;
		__m128i *y_high_ptr = (__m128i *)y_highpass;
		__m128i *u_high_ptr = (__m128i *)u_highpass;
		__m128i *v_high_ptr = (__m128i *)v_highpass;
		__m128i *even_ptr = (__m128i *)even_field;
		__m128i *odd_ptr = (__m128i *)odd_field;

		// Check that the pointers to the groups of pixels are properly aligned
		assert(ISALIGNED16(y_low_ptr));
		assert(ISALIGNED16(y_high_ptr));
		assert(ISALIGNED16(u_low_ptr));
		assert(ISALIGNED16(u_high_ptr));
		assert(ISALIGNED16(v_low_ptr));
		assert(ISALIGNED16(v_high_ptr));
#endif

		// Start at the beginning of the row
		column = 0;

#if (1 && XMMOPT)
		for (; column < post_column; column += column_step)
		{
			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;
			__m128i odd1_epi16;
			__m128i odd2_epi16;
			__m128i even1_epi16;
			__m128i even2_epi16;

			__m128i y_even_epu8;
			__m128i u_even_epu8;
			__m128i v_even_epu8;
			__m128i y_odd_epu8;
			__m128i u_odd_epu8;
			__m128i v_odd_epu8;

			__m128i uvuv_even_epu8;
			__m128i yuyv_even_epu8;
			__m128i uvuv_odd_epu8;
			__m128i yuyv_odd_epu8;


			/***** Reconstruct the first sixteen luma pixels for the even and odd rows *****/

			// Get the first eight lowpass and four highpass coefficients
			low1_epi16 = _mm_load_si128(y_low_ptr++);
			high1_epi16 = _mm_load_si128(y_high_ptr++);

			// Reconstruct the luma pixels in the even row
			even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

			// Reconstruct the luma pixels in the odd row
			odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

			// Get the second eight lowpass and four highpass coefficients
			low2_epi16 = _mm_load_si128(y_low_ptr++);
			high2_epi16 = _mm_load_si128(y_high_ptr++);

			// Reconstruct the luma pixels in the even row
			even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
			even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

			// Reconstruct the luma pixels in the odd row
			odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
			odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

			// Pack sixteen bytes of luma in the even row
			y_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

			// Pack sixteen bytes of luma in the odd row
			y_odd_epu8 = _mm_packus_epi16(odd1_epi16, odd2_epi16);


			/***** Reconstruct sixteen u chroma pixels for the even and odd rows *****/

			// Get the first eight lowpass and four highpass coefficients
			low1_epi16 = _mm_load_si128(u_low_ptr++);
			high1_epi16 = _mm_load_si128(u_high_ptr++);

			// Reconstruct the u chroma pixels in the even row
			even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

			// Reconstruct the u chroma pixels in the odd row
			odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

			// Get the second eight lowpass and four highpass coefficients
			low2_epi16 = _mm_load_si128(u_low_ptr++);
			high2_epi16 = _mm_load_si128(u_high_ptr++);

			// Reconstruct the u chroma pixels in the even row
			even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
			even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

			// Reconstruct the u chroma pixels in the odd row
			odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
			odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

			// Pack sixteen bytes of u chroma in the even row
			u_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

			// Pack sixteen bytes of u chroma in the odd row
			u_odd_epu8 =  _mm_packus_epi16(odd1_epi16, odd2_epi16);


			/***** Reconstruct sixteen v chroma pixels for the even and odd rows *****/

			// Get the first eight lowpass and four highpass coefficients
			low1_epi16 = _mm_load_si128(v_low_ptr++);
			high1_epi16 = _mm_load_si128(v_high_ptr++);

			// Reconstruct the v chroma pixels in the even row
			even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

			// Reconstruct the v chroma pixels in the odd row
			odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

			// Get the second eight lowpass and four highpass coefficients
			low2_epi16 = _mm_load_si128(v_low_ptr++);
			high2_epi16 = _mm_load_si128(v_high_ptr++);

			// Reconstruct the v chroma pixels in the even row
			even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
			even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

			// Reconstruct the v chroma pixels in the odd row
			odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
			odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

			// Pack sixteen bytes of v chroma in the even row
			v_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

			// Pack sixteen bytes of v chroma in the odd row
			v_odd_epu8 =  _mm_packus_epi16(odd1_epi16, odd2_epi16);


			/***** Interleave the first sixteen bytes of luma with alternating chroma *****/

			// Interleave the first eight chroma for the even and odd rows
			uvuv_even_epu8 = _mm_unpacklo_epi8(v_even_epu8, u_even_epu8);
			uvuv_odd_epu8 = _mm_unpacklo_epi8(v_odd_epu8, u_odd_epu8);

			// Pack and store the first eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpacklo_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpacklo_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

			// Pack and store the second eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpackhi_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the second eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpackhi_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);


			/***** Reconstruct the second sixteen luma pixels for the even and odd rows *****/

			// Get the first eight lowpass and four highpass coefficients
			low1_epi16 = _mm_load_si128(y_low_ptr++);
			high1_epi16 = _mm_load_si128(y_high_ptr++);

			// Reconstruct the luma pixels in the even row
			even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

			// Reconstruct the luma pixels in the odd row
			odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

			// Get the second eight lowpass and four highpass coefficients
			low2_epi16 = _mm_load_si128(y_low_ptr++);
			high2_epi16 = _mm_load_si128(y_high_ptr++);

			// Reconstruct the luma pixels in the even row
			even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
			even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

			// Reconstruct the luma pixels in the odd row
			odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
			odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

			// Pack sixteen bytes of luma in the even row
			y_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

			// Pack sixteen bytes of luma in the odd row
			y_odd_epu8 = _mm_packus_epi16(odd1_epi16, odd2_epi16);


			/***** Interleave the second sixteen bytes of luma with alternating chroma *****/

			// Interleave the second eight chroma for the even and odd rows
			uvuv_even_epu8 = _mm_unpackhi_epi8(v_even_epu8, u_even_epu8);
			uvuv_odd_epu8 = _mm_unpackhi_epi8(v_odd_epu8, u_odd_epu8);

			// Pack and store the first eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpacklo_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpacklo_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

			// Pack and store the second eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpackhi_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the second eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpackhi_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);


			// Done interleaving 32 bytes of luma with 16 bytes or each chroma in the even and odd rows
		}

		// The loop should have terminated at the post processing column
		assert(column == post_column);
#endif

		// Process the remaining portion of the row
		for (; column < roi.width; column += 2)
		{
			int chroma_column = column / 2;
			int output_column = column * 2;
			int low;
			int high;
			int k0 = output_column;
			int k1 = output_column + 1;
			int k2 = output_column + 2;
			int k3 = output_column + 3;

			// Get the lowpass and highpass coefficients for the first luma value
			low = y_lowpass[column];
			high = y_highpass[column];

			// Reconstruct the luma in the even and odd rows
			odd_field[k0] = (low + high)/2;
			even_field[k0] = (low - high)/2;

			// Get the lowpass and highpass coefficients for the first chroma value
			low = v_lowpass[chroma_column];
			high = v_highpass[chroma_column];

			// Reconstruct the chroma in the even and odd rows
			odd_field[k1] = (low + high)/2;
			even_field[k1] = (low - high)/2;

			// Get the lowpass and highpass coefficients for the second luma value
			low = y_lowpass[column + 1];
			high = y_highpass[column + 1];

			// Reconstruct the luma in the even and odd rows
			odd_field[k2] = (low + high)/2;
			even_field[k2] = (low - high)/2;

			// Get the lowpass and highpass coefficients for the second chroma value
			low = u_lowpass[chroma_column];
			high = u_highpass[chroma_column];

			// Reconstruct the chroma in the even and odd rows
			odd_field[k3] = (low + high)/2;
			even_field[k3] = (low - high)/2;
		}

		// Advance to the next input and output rows
		y_lowpass += y_lowpass_pitch;
		u_lowpass += u_lowpass_pitch;
		v_lowpass += v_lowpass_pitch;
		y_highpass += y_highpass_pitch;
		u_highpass += u_highpass_pitch;
		v_highpass += v_highpass_pitch;
		even_field += field_pitch;
		odd_field += field_pitch;
	}
}
#endif

#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertInterlacedRow16sToYUV(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
								 uint8_t *output, int pitch, int output_width, int frame_width,
								 int chroma_offset, int format)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

#define _PACKS_PU16 1		// Not sure if should use unsigned pack

// Invert the temporal bands from all channels and pack the output pixels
void InvertInterlacedRow16sToYUV(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
								 uint8_t *output, int pitch, int output_width, int frame_width,
								 int chroma_offset, int format)
{
	uint8_t *even_field = output;
	uint8_t *odd_field = even_field + pitch;
	int row, column;

#if (1 && XMMOPT)
	int chroma_width = output_width/2;
	int chroma_step = 8;
	int post_column = 2 * (chroma_width - (chroma_width % chroma_step));
	int column_step = 2 * chroma_step;

	// Initialize pointers to the rows or lowpass and highpass coefficients
	__m64 *y_low_ptr = (__m64 *)lowpass[0];
	__m64 *u_low_ptr = (__m64 *)lowpass[1];
	__m64 *v_low_ptr = (__m64 *)lowpass[2];
	__m64 *y_high_ptr = (__m64 *)highpass[0];
	__m64 *u_high_ptr = (__m64 *)highpass[1];
	__m64 *v_high_ptr = (__m64 *)highpass[2];

	// Initialize the pointers to the rows in the even and odd fields
	__m64 *even_ptr = (__m64 *)even_field;
	__m64 *odd_ptr = (__m64 *)odd_field;

#if _ENCODE_CHROMA_OFFSET
	__m64 offset_pi16 = _mm_set1_pi16(chroma_offset);
#endif
#endif

	// This code assumes that there are three channels
	assert(num_channels == 3);

#if 0
	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(y_low_ptr));
	assert(ISALIGNED16(u_low_ptr));
	assert(ISALIGNED16(v_low_ptr));
	assert(ISALIGNED16(y_high_ptr));
	assert(ISALIGNED16(u_high_ptr));
	assert(ISALIGNED16(v_high_ptr));
#endif

	// This code assumes that there are three channels
	assert(num_channels == 3);

	// Start at the beginning of the row
	column = 0;

#if (1 && XMMOPT)

	for (; column < post_column; column += column_step)
	{
		__m64 low1_pi16;
		__m64 low2_pi16;
		__m64 high1_pi16;
		__m64 high2_pi16;
		__m64 odd1_pi16;
		__m64 odd2_pi16;
		__m64 even1_pi16;
		__m64 even2_pi16;

		__m64 y_even_pu8;
		__m64 u_even_pu8;
		__m64 v_even_pu8;
		__m64 y_odd_pu8;
		__m64 u_odd_pu8;
		__m64 v_odd_pu8;

		__m64 uvuv_even_pu8;
		__m64 yuyv_even_pu8;
		__m64 uvuv_odd_pu8;
		__m64 yuyv_odd_pu8;


		/***** Reconstruct the first eight luma pixels for the even and odd rows *****/

		// Get the first four lowpass and four highpass coefficients
		low1_pi16 = *(y_low_ptr++);
		high1_pi16 = *(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
		even1_pi16 = _mm_srai_pi16(even1_pi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
		odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1);

		// Get the second four lowpass and four highpass coefficients
		low2_pi16 = *(y_low_ptr++);
		high2_pi16 = *(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
		even2_pi16 = _mm_srai_pi16(even2_pi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
		odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1);

		// Pack eight bytes of luma in the even row
#if _PACKS_PU16
		y_even_pu8 = _mm_packs_pu16(even1_pi16, even2_pi16);
#else
		y_even_pu8 = _mm_packs_pi16(even1_pi16, even2_pi16);
#endif

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on luma if required
		y_even_pu8 = _mm_subs_pu8(y_even_pu8, _mm_set1_pi8(16));
		y_even_pu8 = _mm_adds_pu8(y_even_pu8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
		y_even_pu8 = _mm_subs_pu8(y_even_pu8, _mm_set1_pi8(20));
#endif

		// Pack eight bytes of luma in the odd row
#if _PACKS_PU16
		y_odd_pu8 = _mm_packs_pu16(odd1_pi16, odd2_pi16);
#else
		y_odd_pu8 = _mm_packs_pi16(odd1_pi16, odd2_pi16);
#endif

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on luma if required
		y_odd_pu8 = _mm_subs_pu8(y_odd_pu8, _mm_set1_pi8(16));
		y_odd_pu8 = _mm_adds_pu8(y_odd_pu8, _mm_set1_pi8(36));		// 36 = 16 + 20 = 16 + (255-235)
		y_odd_pu8 = _mm_subs_pu8(y_odd_pu8, _mm_set1_pi8(20));
#endif

		/***** Reconstruct eight u chroma pixels for the even and odd rows *****/

		// Get the first eight lowpass and four highpass coefficients
		low1_pi16 = *(u_low_ptr++);
		high1_pi16 = *(u_high_ptr++);

		// Reconstruct the u chroma pixels in the even row
		even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
		even1_pi16 = _mm_srai_pi16(even1_pi16, 1);

		// Reconstruct the u chroma pixels in the odd row
		odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
		odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1);

		// Get the second four lowpass and four highpass coefficients
		low2_pi16 = *(u_low_ptr++);
		high2_pi16 = *(u_high_ptr++);

		// Reconstruct the u chroma pixels in the even row
		even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
		even2_pi16 = _mm_srai_pi16(even2_pi16, 1);

		// Reconstruct the u chroma pixels in the odd row
		odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
		odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1);

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even row
		even1_pi16 = _mm_adds_pi16(even1_pi16, offset_pi16);
		even2_pi16 = _mm_adds_pi16(even2_pi16, offset_pi16);

		// Add the chroma offset to the odd row
		odd1_pi16 = _mm_adds_pi16(odd1_pi16, offset_pi16);
		odd2_pi16 = _mm_adds_pi16(odd2_pi16, offset_pi16);
#endif

		// Pack eight bytes of u chroma in the even row
#if _PACKS_PU16
		u_even_pu8 = _mm_packs_pu16(even1_pi16, even2_pi16);
#else
		u_even_pu8 = _mm_packs_pi16(even1_pi16, even2_pi16);
#endif

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		u_even_pu8 = _mm_subs_pu8(u_even_pu8, _mm_set1_pi8(16));
		u_even_pu8 = _mm_adds_pu8(u_even_pu8, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		u_even_pu8 = _mm_subs_pu8(u_even_pu8, _mm_set1_pi8(15));
#endif

		// Pack eight bytes of u chroma in the odd row
#if _PACKS_PU16
		u_odd_pu8 =  _mm_packs_pu16(odd1_pi16, odd2_pi16);
#else
		u_odd_pu8 =  _mm_packs_pi16(odd1_pi16, odd2_pi16);
#endif

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		y_odd_pu8 = _mm_subs_pu8(y_odd_pu8, _mm_set1_pi8(16));
		y_odd_pu8 = _mm_adds_pu8(y_odd_pu8, _mm_set1_pi8(31));		// 31 = 16 + 15 = 16 + (255-240)
		y_odd_pu8 = _mm_subs_pu8(y_odd_pu8, _mm_set1_pi8(15));
#endif

		/***** Reconstruct eight v chroma pixels for the even and odd rows *****/

		// Get the first four lowpass and four highpass coefficients
		low1_pi16 = *(v_low_ptr++);
		high1_pi16 = *(v_high_ptr++);

		// Reconstruct the v chroma pixels in the even row
		even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
		even1_pi16 = _mm_srai_pi16(even1_pi16, 1);

		// Reconstruct the v chroma pixels in the odd row
		odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
		odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1);

		// Get the second eight lowpass and four highpass coefficients
		low2_pi16 = *(v_low_ptr++);
		high2_pi16 = *(v_high_ptr++);

		// Reconstruct the v chroma pixels in the even row
		even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
		even2_pi16 = _mm_srai_pi16(even2_pi16, 1);

		// Reconstruct the v chroma pixels in the odd row
		odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
		odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1);

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even row
		even1_pi16 = _mm_adds_pi16(even1_pi16, offset_pi16);
		even2_pi16 = _mm_adds_pi16(even2_pi16, offset_pi16);

		// Add the chroma offset to the odd row
		odd1_pi16 = _mm_adds_pi16(odd1_pi16, offset_pi16);
		odd2_pi16 = _mm_adds_pi16(odd2_pi16, offset_pi16);
#endif

		// Pack eight bytes of v chroma in the even row
#if _PACKS_PU16
		v_even_pu8 = _mm_packs_pu16(even1_pi16, even2_pi16);
#else
		v_even_pu8 = _mm_packs_pi16(even1_pi16, even2_pi16);
#endif

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		u_even_pu8 = _mm_subs_pu8(u_even_pu8, _mm_set1_pi8(16));
		u_even_pu8 = _mm_adds_pu8(u_even_pu8, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		u_even_pu8 = _mm_subs_pu8(u_even_pu8, _mm_set1_pi8(15));
#endif

		// Pack eight bytes of v chroma in the odd row
#if _PACKS_PU16
		v_odd_pu8 =  _mm_packs_pu16(odd1_pi16, odd2_pi16);
#else
		v_odd_pu8 =  _mm_packs_pi16(odd1_pi16, odd2_pi16);
#endif

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		y_odd_pu8 = _mm_subs_pu8(y_odd_pu8, _mm_set1_pi8(16));
		y_odd_pu8 = _mm_adds_pu8(y_odd_pu8, _mm_set1_pi8(31));		// 31 = 16 + 15 = 16 + (255-240)
		y_odd_pu8 = _mm_subs_pu8(y_odd_pu8, _mm_set1_pi8(15));
#endif

		/***** Interleave the first eight bytes of luma with alternating chroma *****/

		// Interleave the first four chroma for the even and odd rows
		uvuv_even_pu8 = _mm_unpacklo_pi8(v_even_pu8, u_even_pu8);
		uvuv_odd_pu8 = _mm_unpacklo_pi8(v_odd_pu8, u_odd_pu8);

		// Pack and store the first four luma and chroma pairs for the even row
		yuyv_even_pu8 = _mm_unpacklo_pi8(y_even_pu8, uvuv_even_pu8);
		//_mm_store_si128(even_ptr++, yuyv_even_pu8);
		*(even_ptr++) = yuyv_even_pu8;

		// Pack and store the first four luma and chroma pairs for the odd row
		yuyv_odd_pu8 = _mm_unpacklo_pi8(y_odd_pu8, uvuv_odd_pu8);
		//_mm_store_si128(odd_ptr++, yuyv_odd_pu8);
		*(odd_ptr++) = yuyv_odd_pu8;

		// Pack and store the second four luma and chroma pairs for the even row
		yuyv_even_pu8 = _mm_unpackhi_pi8(y_even_pu8, uvuv_even_pu8);
		//_mm_store_si128(even_ptr++, yuyv_even_pu8);
		*(even_ptr++) = yuyv_even_pu8;

		// Pack and store the second four luma and chroma pairs for the odd row
		yuyv_odd_pu8 = _mm_unpackhi_pi8(y_odd_pu8, uvuv_odd_pu8);
		//_mm_store_si128(odd_ptr++, yuyv_odd_pu8);
		*(odd_ptr++) = yuyv_odd_pu8;


		/***** Reconstruct the second eight luma pixels for the even and odd rows *****/

		// Get the first four lowpass and four highpass coefficients
		low1_pi16 = *(y_low_ptr++);
		high1_pi16 = *(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
		even1_pi16 = _mm_srai_pi16(even1_pi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
		odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1);

		// Get the second four lowpass and four highpass coefficients
		low2_pi16 = *(y_low_ptr++);
		high2_pi16 = *(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
		even2_pi16 = _mm_srai_pi16(even2_pi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
		odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1);

		// Pack eight bytes of luma in the even row
#if _PACKS_PU16
		y_even_pu8 = _mm_packs_pu16(even1_pi16, even2_pi16);
#else
		y_even_pu8 = _mm_packs_pi16(even1_pi16, even2_pi16);
#endif

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on luma if required
		y_even_pu8 = _mm_subs_pu8(y_even_pu8, _mm_set1_pi8(16));
		y_even_pu8 = _mm_adds_pu8(y_even_pu8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
		y_even_pu8 = _mm_subs_pu8(y_even_pu8, _mm_set1_pi8(20));
#endif

		// Pack eight bytes of luma in the odd row
#if _PACKS_PU16
		y_odd_pu8 = _mm_packs_pu16(odd1_pi16, odd2_pi16);
#else
		y_odd_pu8 = _mm_packs_pi16(odd1_pi16, odd2_pi16);
#endif

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on luma if required
		y_odd_pu8 = _mm_subs_pu8(y_odd_pu8, _mm_set1_pi8(16));
		y_odd_pu8 = _mm_adds_pu8(y_odd_pu8, _mm_set1_pi8(36));		// 36 = 16 + 20 = 16 + (255-235)
		y_odd_pu8 = _mm_subs_pu8(y_odd_pu8, _mm_set1_pi8(20));
#endif

		/***** Interleave the second eight bytes of luma with alternating chroma *****/

		// Interleave the second eight chroma for the even and odd rows
		uvuv_even_pu8 = _mm_unpackhi_pi8(v_even_pu8, u_even_pu8);
		uvuv_odd_pu8 = _mm_unpackhi_pi8(v_odd_pu8, u_odd_pu8);

		// Pack and store the first four luma and chroma pairs for the even row
		yuyv_even_pu8 = _mm_unpacklo_pi8(y_even_pu8, uvuv_even_pu8);
		//_mm_store_si128(even_ptr++, yuyv_even_pu8);
		*(even_ptr++) = yuyv_even_pu8;

		// Pack and store the first four luma and chroma pairs for the odd row
		yuyv_odd_pu8 = _mm_unpacklo_pi8(y_odd_pu8, uvuv_odd_pu8);
		//_mm_store_si128(odd_ptr++, yuyv_odd_pu8);
		*(odd_ptr++) = yuyv_odd_pu8;

		// Pack and store the second four luma and chroma pairs for the even row
		yuyv_even_pu8 = _mm_unpackhi_pi8(y_even_pu8, uvuv_even_pu8);
		*(even_ptr++) = yuyv_even_pu8;

		// Pack and store the second four luma and chroma pairs for the odd row
		yuyv_odd_pu8 = _mm_unpackhi_pi8(y_odd_pu8, uvuv_odd_pu8);
		*(odd_ptr++) = yuyv_odd_pu8;


		// Done interleaving 32 bytes of luma with 16 bytes of each chroma channel
		// to reconstruct the 16 quad tuples of YUYV for the even and odd output rows
	}

	// The loop should have terminated at the post processing column
	assert(column == post_column);

#endif

	// Process the remaining portion of the row
	for (; column < output_width; column += 2)
	{
		int chroma_column = column / 2;
		int output_column = column * 2;
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;
		int low, high;
		int even, odd;

		// Get the lowpass and highpass coefficients for the first luma value
		low = lowpass[0][column];
		high = highpass[0][column];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k0] = SATURATE_Y(odd);
		even_field[k0] = SATURATE_Y(even);

		// Get the lowpass and highpass coefficients for the first chroma value
		// Note that the v chroma value is the second byte in the four tuple
		low = lowpass[2][chroma_column];
		high = highpass[2][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset
		odd += chroma_offset;
		even += chroma_offset;
#endif
		// Store the first chroma value in the even and odd rows
		odd_field[k1] = SATURATE_Cr(odd);
		even_field[k1] = SATURATE_Cr(even);

		// Get the lowpass and highpass coefficients for the second luma value
		low = lowpass[0][column + 1];
		high = highpass[0][column + 1];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k2] = SATURATE_Y(odd);
		even_field[k2] = SATURATE_Y(even);

		// Get the lowpass and highpass coefficients for the second chroma value
		// Note that the u chroma value is the fourth byte in the four tuple
		low = lowpass[1][chroma_column];
		high = highpass[1][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset
		odd += chroma_offset;
		even += chroma_offset;
#endif
		// Store the first chroma value in the even and odd rows
		odd_field[k3] = SATURATE_Cb(odd);
		even_field[k3] = SATURATE_Cb(even);
	}

#if 0
	// Fill the rest of the row with background values
	for (; column < frame_width; column += 2)
	{
		int output_column = column * 2;
#if 1
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;

		even_field[k0] = COLOR_LUMA_BLACK;
		odd_field[k0] = COLOR_LUMA_BLACK;

		even_field[k1] = COLOR_CHROMA_ZERO;
		odd_field[k1] = COLOR_CHROMA_ZERO;

		even_field[k2] = COLOR_LUMA_BLACK;
		odd_field[k2] = COLOR_LUMA_BLACK;

		even_field[k3] = COLOR_CHROMA_ZERO;
		odd_field[k3] = COLOR_CHROMA_ZERO;
#else
		int i;

		for (i = 0; i < 4; i++) {
			even_field[output_column + i] = 0;
			odd_field[output_column + i] = 0;
		}
#endif
	}
#endif

	//_mm_empty();	// Clear the mmx register state
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Invert the temporal bands from all channels and pack the output pixels
void InvertInterlacedRow16sToYUV(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
								 uint8_t *output, int pitch, int output_width, int frame_width,
								 int chroma_offset, int format)
{
	uint8_t *even_field = output;
	uint8_t *odd_field = output + pitch;
	int column;

#if (1 && XMMOPT)
	int chroma_width = output_width/2;
	int chroma_step = 8;
	int post_column = 2 * (chroma_width - (chroma_width % chroma_step));
	int column_step = 2 * chroma_step;

	// Initialize pointers to the rows of lowpass and highpass coefficients
	__m128i *y_low_ptr = (__m128i *)lowpass[0];
	__m128i *u_low_ptr = (__m128i *)lowpass[1];
	__m128i *v_low_ptr = (__m128i *)lowpass[2];
	__m128i *y_high_ptr = (__m128i *)highpass[0];
	__m128i *u_high_ptr = (__m128i *)highpass[1];
	__m128i *v_high_ptr = (__m128i *)highpass[2];

	// Initialize the pointers to the rows in the even and odd fields
	__m128i *even_ptr = (__m128i *)even_field;
	__m128i *odd_ptr = (__m128i *)odd_field;
	
	// This code assumes that there are three channels
	assert(num_channels == 3);

	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(y_low_ptr));
	assert(ISALIGNED16(u_low_ptr));
	assert(ISALIGNED16(v_low_ptr));
	assert(ISALIGNED16(y_high_ptr));
	assert(ISALIGNED16(u_high_ptr));
	assert(ISALIGNED16(v_high_ptr));
#endif

	// Check that the output pointers are properly aligned
	//assert(ISALIGNED16(even_ptr));
	//assert(ISALIGNED16(odd_ptr));

	// Start at the beginning of the row
	column = 0;

#if (0 && XMMOPT)

	// Longer loop that processes 32 output pixels per iteration
	assert(column_step == 32);

	// Process groups of pixels in the fast loop
	for (; column < post_column; column += column_step)
	{
		__m128i low1_epi16;
		__m128i low2_epi16;
		__m128i high1_epi16;
		__m128i high2_epi16;
		__m128i odd1_epi16;
		__m128i odd2_epi16;
		__m128i even1_epi16;
		__m128i even2_epi16;

		__m128i y_even_epu8;
		__m128i u_even_epu8;
		__m128i v_even_epu8;
		__m128i y_odd_epu8;
		__m128i u_odd_epu8;
		__m128i v_odd_epu8;

		__m128i uvuv_even_epu8;
		__m128i yuyv_even_epu8;
		__m128i uvuv_odd_epu8;
		__m128i yuyv_odd_epu8;


		/***** Reconstruct the first sixteen luma pixels for the even and odd rows *****/

		// Get the first eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(y_low_ptr++);
		high1_epi16 = _mm_load_si128(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

		// Get the second eight lowpass and eight highpass coefficients
		low2_epi16 = _mm_load_si128(y_low_ptr++);
		high2_epi16 = _mm_load_si128(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
		even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
		odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

		// Pack sixteen bytes of luma in the even row
		y_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

#if STRICT_SATURATE
		// Perform strict saturation on luma if required
		y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(16));
		y_even_epu8 = _mm_adds_epu8(y_even_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
		y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(20));
#endif
		// Pack sixteen bytes of luma in the odd row
		y_odd_epu8 = _mm_packus_epi16(odd1_epi16, odd2_epi16);

#if STRICT_SATURATE
		// Perform strict saturation on luma if required
		y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(16));
		y_odd_epu8 = _mm_adds_epu8(y_odd_epu8, _mm_set1_epi8(36));		// 36 = 16 + 20 = 16 + (255-235)
		y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(20));
#endif

		/***** Reconstruct sixteen u chroma pixels for the even and odd rows *****/

		// Get the first eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(u_low_ptr++);
		high1_epi16 = _mm_load_si128(u_high_ptr++);

		// Reconstruct the u chroma pixels in the even row
		even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

		// Reconstruct the u chroma pixels in the odd row
		odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

		// Get the second eight lowpass and eight highpass coefficients
		low2_epi16 = _mm_load_si128(u_low_ptr++);
		high2_epi16 = _mm_load_si128(u_high_ptr++);

		// Reconstruct the u chroma pixels in the even row
		even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
		even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

		// Reconstruct the u chroma pixels in the odd row
		odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
		odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even row
		even1_epi16 = _mm_adds_epi16(even1_epi16, offset_epi16);
		even2_epi16 = _mm_adds_epi16(even2_epi16, offset_epi16);

		// Add the chroma offset to the odd row
		odd1_epi16 = _mm_adds_epi16(odd1_epi16, offset_epi16);
		odd2_epi16 = _mm_adds_epi16(odd2_epi16, offset_epi16);
#endif
		// Pack sixteen bytes of u chroma in the even row
		u_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		u_even_epu8 = _mm_subs_epu8(u_even_epu8, _mm_set1_epi8(16));
		u_even_epu8 = _mm_adds_epu8(u_even_epu8, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		u_even_epu8 = _mm_subs_epu8(u_even_epu8, _mm_set1_epi8(15));
#endif
		// Pack sixteen bytes of u chroma in the odd row
		u_odd_epu8 =  _mm_packus_epi16(odd1_epi16, odd2_epi16);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		u_odd_epu8 = _mm_subs_epu8(u_odd_epu8, _mm_set1_epi8(16));
		u_odd_epu8 = _mm_adds_epu8(u_odd_epu8, _mm_set1_epi8(31));		// 31 = 16 + 15 = 16 + (255-240)
		u_odd_epu8 = _mm_subs_epu8(u_odd_epu8, _mm_set1_epi8(15));
#endif

		/***** Reconstruct sixteen v chroma pixels for the even and odd rows *****/

		// Get the first eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(v_low_ptr++);
		high1_epi16 = _mm_load_si128(v_high_ptr++);

		// Reconstruct the v chroma pixels in the even row
		even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

		// Reconstruct the v chroma pixels in the odd row
		odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

		// Get the second eight lowpass and eight highpass coefficients
		low2_epi16 = _mm_load_si128(v_low_ptr++);
		high2_epi16 = _mm_load_si128(v_high_ptr++);

		// Reconstruct the v chroma pixels in the even row
		even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
		even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

		// Reconstruct the v chroma pixels in the odd row
		odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
		odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even row
		even1_epi16 = _mm_adds_epi16(even1_epi16, offset_epi16);
		even2_epi16 = _mm_adds_epi16(even2_epi16, offset_epi16);

		// Add the chroma offset to the odd row
		odd1_epi16 = _mm_adds_epi16(odd1_epi16, offset_epi16);
		odd2_epi16 = _mm_adds_epi16(odd2_epi16, offset_epi16);
#endif
		// Pack sixteen bytes of v chroma in the even row
		v_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		v_even_epu8 = _mm_subs_epu8(v_even_epu8, _mm_set1_epi8(16));
		v_even_epu8 = _mm_adds_epu8(v_even_epu8, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		v_even_epu8 = _mm_subs_epu8(v_even_epu8, _mm_set1_epi8(15));
#endif
		// Pack sixteen bytes of v chroma in the odd row
		v_odd_epu8 =  _mm_packus_epi16(odd1_epi16, odd2_epi16);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		v_odd_epu8 = _mm_subs_epu8(v_odd_epu8, _mm_set1_epi8(16));
		v_odd_epu8 = _mm_adds_epu8(v_odd_epu8, _mm_set1_epi8(31));		// 31 = 16 + 15 = 16 + (255-240)
		v_odd_epu8 = _mm_subs_epu8(v_odd_epu8, _mm_set1_epi8(15));
#endif

		/***** Interleave the first sixteen bytes of luma with alternating chroma *****/

		if(format == COLOR_FORMAT_YUYV)
		{
			// Interleave the first eight chroma for the even and odd rows
			uvuv_even_epu8 = _mm_unpacklo_epi8(v_even_epu8, u_even_epu8);
			uvuv_odd_epu8 = _mm_unpacklo_epi8(v_odd_epu8, u_odd_epu8);

			// Pack and store the first eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpacklo_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpacklo_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

			// Pack and store the second eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpackhi_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the second eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpackhi_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);
		}
		else //UYVY
		{
			// Interleave the first eight chroma for the even and odd rows
			uvuv_even_epu8 = _mm_unpacklo_epi8(v_even_epu8, u_even_epu8);
			uvuv_odd_epu8 = _mm_unpacklo_epi8(v_odd_epu8, u_odd_epu8);

			// Pack and store the first eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpacklo_epi8(uvuv_even_epu8, y_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpacklo_epi8(uvuv_odd_epu8, y_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

			// Pack and store the second eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpackhi_epi8(uvuv_even_epu8, y_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the second eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpackhi_epi8(uvuv_odd_epu8, y_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);
		}


		/***** Reconstruct the second sixteen luma pixels for the even and odd rows *****/

		// Get the first eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(y_low_ptr++);
		high1_epi16 = _mm_load_si128(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

		// Get the second eight lowpass and eight highpass coefficients
		low2_epi16 = _mm_load_si128(y_low_ptr++);
		high2_epi16 = _mm_load_si128(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
		even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
		odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

		// Pack sixteen bytes of luma in the even row
		y_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

#if STRICT_SATURATE
		// Perform strict saturation on luma if required
		y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(16));
		y_even_epu8 = _mm_adds_epu8(y_even_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
		y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(20));
#endif
		// Pack sixteen bytes of luma in the odd row
		y_odd_epu8 = _mm_packus_epi16(odd1_epi16, odd2_epi16);

#if STRICT_SATURATE
		// Perform strict saturation on YUV if required
		y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(16));
		y_odd_epu8 = _mm_adds_epu8(y_odd_epu8, _mm_set1_epi8(36));		// 36 = 16 + 20 = 16 + (255-235)
		y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(20));
#endif

		/***** Interleave the second sixteen bytes of luma with alternating chroma *****/

		if(format == COLOR_FORMAT_YUYV)
		{
			// Interleave the second eight chroma for the even and odd rows
			uvuv_even_epu8 = _mm_unpackhi_epi8(v_even_epu8, u_even_epu8);
			uvuv_odd_epu8 = _mm_unpackhi_epi8(v_odd_epu8, u_odd_epu8);

			// Pack and store the first eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpacklo_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpacklo_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

			// Pack and store the second eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpackhi_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the second eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpackhi_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);
		}
		else
		{
			// Interleave the second eight chroma for the even and odd rows
			uvuv_even_epu8 = _mm_unpackhi_epi8(v_even_epu8, u_even_epu8);
			uvuv_odd_epu8 = _mm_unpackhi_epi8(v_odd_epu8, u_odd_epu8);

			// Pack and store the first eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpacklo_epi8(uvuv_even_epu8, y_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpacklo_epi8(uvuv_odd_epu8, y_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

			// Pack and store the second eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpackhi_epi8(uvuv_even_epu8, y_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the second eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpackhi_epi8(uvuv_odd_epu8, y_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);
		}

		// Done interleaving 32 bytes of luma with 16 bytes of each chroma channel
		// to reconstruct the 16 quad tuples of YUYV for the even and odd output rows
	}

	// The loop should have terminated at the post processing column
	assert(column == post_column);

	// Can we use a fast loop for the next sixteen output pixels?
	if ((post_column + 16) <= output_width)
	{
		__m128i low1_epi16;
		__m128i low2_epi16;
		__m128i high1_epi16;
		__m128i high2_epi16;
		__m128i odd1_epi16;
		__m128i odd2_epi16;
		__m128i even1_epi16;
		__m128i even2_epi16;

		__m128i y_even_epu8;
		__m128i y_odd_epu8;
		__m128i u_even_epi16;
		__m128i v_even_epi16;
		__m128i u_odd_epi16;
		__m128i v_odd_epi16;

		__m128i uvuv_even_epu8;
		__m128i yuyv_even_epu8;
		__m128i uvuv_odd_epu8;
		__m128i yuyv_odd_epu8;


		/***** Reconstruct sixteen luma pixels for the even and odd rows *****/

		// Get the first eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(y_low_ptr++);
		high1_epi16 = _mm_load_si128(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

		// Get the second eight lowpass and eight highpass coefficients
		low2_epi16 = _mm_load_si128(y_low_ptr++);
		high2_epi16 = _mm_load_si128(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
		even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
		odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

		// Pack sixteen bytes of luma in the even row
		y_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

#if STRICT_SATURATE
		// Perform strict saturation on YUV if required
		y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(16));
		y_even_epu8 = _mm_adds_epu8(y_even_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
		y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(20));
#endif
		// Pack sixteen bytes of luma in the odd row
		y_odd_epu8 = _mm_packus_epi16(odd1_epi16, odd2_epi16);

#if STRICT_SATURATE
		// Perform strict saturation on YUV if required
		y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(16));
		y_odd_epu8 = _mm_adds_epu8(y_odd_epu8, _mm_set1_epi8(36));		// 36 = 16 + 20 = 16 + (255-235)
		y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(20));
#endif

		/***** Reconstruct eight u chroma pixels for the even and odd rows *****/

		// Load eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(u_low_ptr++);
		high1_epi16 = _mm_load_si128(u_high_ptr++);

		// Reconstruct the u chroma pixels in the even row
		u_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		u_even_epi16 = _mm_srai_epi16(u_even_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		u_even_epi16 = _mm_subs_epu8(u_even_epi16, _mm_set1_epi8(16));
		u_even_epi16 = _mm_adds_epu8(u_even_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		u_even_epi16 = _mm_subs_epu8(u_even_epi16, _mm_set1_epi8(15));
#endif
		// Reconstruct the u chroma pixels in the odd row
		u_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		u_odd_epi16 = _mm_srai_epi16(u_odd_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		u_odd_epi16 = _mm_subs_epu8(u_odd_epi16, _mm_set1_epi8(16));
		u_odd_epi16 = _mm_adds_epu8(u_odd_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		u_odd_epi16 = _mm_subs_epu8(u_odd_epi16, _mm_set1_epi8(15));
#endif

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even and odd rows
		u_even_epi16 = _mm_adds_epi16(u_even_epi16, offset_epi16);
		u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, offset_epi16);
#endif

		/***** Reconstruct eight v chroma pixels for the even and odd rows *****/

		// Load eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(v_low_ptr++);
		high1_epi16 = _mm_load_si128(v_high_ptr++);

		// Reconstruct the v chroma pixels in the even row
		v_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		v_even_epi16 = _mm_srai_epi16(v_even_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		v_even_epi16 = _mm_subs_epu8(v_even_epi16, _mm_set1_epi8(16));
		v_even_epi16 = _mm_adds_epu8(v_even_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		v_even_epi16 = _mm_subs_epu8(v_even_epi16, _mm_set1_epi8(15));
#endif
		// Reconstruct the v chroma pixels in the odd row
		v_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		v_odd_epi16 = _mm_srai_epi16(v_odd_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		v_odd_epi16 = _mm_subs_epu8(v_odd_epi16, _mm_set1_epi8(16));
		v_odd_epi16 = _mm_adds_epu8(v_odd_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		v_odd_epi16 = _mm_subs_epu8(v_odd_epi16, _mm_set1_epi8(15));
#endif

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even and odd rows
		v_even_epi16 = _mm_adds_epi16(v_even_epi16, offset_epi16);
		v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, offset_epi16);
#endif

		/***** Interleave the sixteen bytes of luma with alternating chroma *****/

		if(format == COLOR_FORMAT_YUYV)
		{
			// Interleave the eight chroma for the even and odd rows
			uvuv_even_epu8 = _mm_packus_epi16(v_even_epi16, u_even_epi16);
			uvuv_odd_epu8 = _mm_packus_epi16(v_odd_epi16, u_odd_epi16);

			// Pack and store the first eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpacklo_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpacklo_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

			// Pack and store the second eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpackhi_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the second eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpackhi_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);
		}
		else //UYUV
		{
			// Interleave the eight chroma for the even and odd rows
			uvuv_even_epu8 = _mm_packus_epi16(v_even_epi16, u_even_epi16);
			uvuv_odd_epu8 = _mm_packus_epi16(v_odd_epi16, u_odd_epi16);

			// Pack and store the first eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpacklo_epi8(uvuv_even_epu8, y_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpacklo_epi8(uvuv_odd_epu8, y_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

			// Pack and store the second eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpackhi_epi8(uvuv_even_epu8, y_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Pack and store the second eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpackhi_epi8(uvuv_odd_epu8, y_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);
		}

		// Advance the column
		column += 16;
	}

	/*
		NOTE: Consider replacing the first fast loop that produces 32 pixels per iteration
		with the second fast loop that produces 16 output pixels per iteration, since 720
		is a multiple of 16 and so the expensive post processing loop below would not be
		required.  The shorter loop might require fewer registers and hence may be faster.
		Try an even shorter fast loop that processes only 8 output pixels per iteration.
	*/

#elif (1 && XMMOPT)

	// Shorter loop that processes 16 output pixels per iteration
	assert(column_step == 16);

	// Process groups of pixels in the fast loop
	for (; column < post_column; column += column_step)
	{
		__m128i low1_epi16;
		__m128i low2_epi16;
		__m128i high1_epi16;
		__m128i high2_epi16;
		__m128i odd1_epi16;
		__m128i odd2_epi16;
		__m128i even1_epi16;
		__m128i even2_epi16;

		__m128i y_even_epu8;
		__m128i y_odd_epu8;
		__m128i u_even_epi16;
		__m128i v_even_epi16;
		__m128i u_odd_epi16;
		__m128i v_odd_epi16;

		__m128i uvuv1_epi16;
		__m128i uvuv2_epi16;

		__m128i uvuv_even_epu8;
		__m128i yuyv_even_epu8;
		__m128i uvuv_odd_epu8;
		__m128i yuyv_odd_epu8;


		/***** Reconstruct sixteen luma pixels for the even and odd rows *****/

		// Get the first eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(y_low_ptr++);
		high1_epi16 = _mm_load_si128(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

		// Get the second eight lowpass and eight highpass coefficients
		low2_epi16 = _mm_load_si128(y_low_ptr++);
		high2_epi16 = _mm_load_si128(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
		even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
		odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

		// Pack sixteen bytes of luma in the even row
		y_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

#if STRICT_SATURATE
		// Perform strict saturation on YUV if required
		y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(16));
		y_even_epu8 = _mm_adds_epu8(y_even_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
		y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(20));
#endif
		// Pack sixteen bytes of luma in the odd row
		y_odd_epu8 = _mm_packus_epi16(odd1_epi16, odd2_epi16);

#if STRICT_SATURATE
		// Perform strict saturation on YUV if required
		y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(16));
		y_odd_epu8 = _mm_adds_epu8(y_odd_epu8, _mm_set1_epi8(36));		// 36 = 16 + 20 = 16 + (255-235)
		y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(20));
#endif

		/***** Reconstruct eight u chroma pixels for the even and odd rows *****/

		// Load eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(u_low_ptr++);
		high1_epi16 = _mm_load_si128(u_high_ptr++);

		// Reconstruct the u chroma pixels in the even row
		u_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		u_even_epi16 = _mm_srai_epi16(u_even_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		u_even_epi16 = _mm_subs_epu8(u_even_epi16, _mm_set1_epi8(16));
		u_even_epi16 = _mm_adds_epu8(u_even_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		u_even_epi16 = _mm_subs_epu8(u_even_epi16, _mm_set1_epi8(15));
#endif
		// Reconstruct the u chroma pixels in the odd row
		u_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		u_odd_epi16 = _mm_srai_epi16(u_odd_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		u_odd_epi16 = _mm_subs_epu8(u_odd_epi16, _mm_set1_epi8(16));
		u_odd_epi16 = _mm_adds_epu8(u_odd_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		u_odd_epi16 = _mm_subs_epu8(u_odd_epi16, _mm_set1_epi8(15));
#endif

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even and odd rows
		u_even_epi16 = _mm_adds_epi16(u_even_epi16, offset_epi16);
		u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, offset_epi16);
#endif

		/***** Reconstruct eight v chroma pixels for the even and odd rows *****/

		// Load eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(v_low_ptr++);
		high1_epi16 = _mm_load_si128(v_high_ptr++);

		// Reconstruct the v chroma pixels in the even row
		v_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		v_even_epi16 = _mm_srai_epi16(v_even_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		v_even_epi16 = _mm_subs_epu8(v_even_epi16, _mm_set1_epi8(16));
		v_even_epi16 = _mm_adds_epu8(v_even_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		v_even_epi16 = _mm_subs_epu8(v_even_epi16, _mm_set1_epi8(15));
#endif
		// Reconstruct the v chroma pixels in the odd row
		v_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		v_odd_epi16 = _mm_srai_epi16(v_odd_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		v_odd_epi16 = _mm_subs_epu8(v_odd_epi16, _mm_set1_epi8(16));
		v_odd_epi16 = _mm_adds_epu8(v_odd_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		v_odd_epi16 = _mm_subs_epu8(v_odd_epi16, _mm_set1_epi8(15));
#endif

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even and odd rows
		v_even_epi16 = _mm_adds_epi16(v_even_epi16, offset_epi16);
		v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, offset_epi16);
#endif

		/***** Interleave the sixteen bytes of luma with alternating chroma *****/

		// Interleave the first four chroma for the even row
		uvuv1_epi16 = _mm_unpacklo_epi16(v_even_epi16, u_even_epi16);

		// Interleave the second four chroma for the even row
		uvuv2_epi16 = _mm_unpackhi_epi16(v_even_epi16, u_even_epi16);

		// Pack sixteen alternating chroma for the even row
		uvuv_even_epu8 = _mm_packus_epi16(uvuv1_epi16, uvuv2_epi16);

		// Interleave the first four chroma for the odd row
		uvuv1_epi16 = _mm_unpacklo_epi16(v_odd_epi16, u_odd_epi16);

		// Interleave the second four chroma for the odd row
		uvuv2_epi16 = _mm_unpackhi_epi16(v_odd_epi16, u_odd_epi16);

		// Pack sixteen alternating chroma for the odd row
		uvuv_odd_epu8 = _mm_packus_epi16(uvuv1_epi16, uvuv2_epi16);

		if(format == COLOR_FORMAT_YUYV)
		{
			// Interleave and store the first eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpacklo_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Interleave and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpacklo_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

			// Interleave and store the second eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpackhi_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Interleave and store the second eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpackhi_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);
		}
		else //UYVY
		{
			// Interleave and store the first eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpacklo_epi8(uvuv_even_epu8, y_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Interleave and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpacklo_epi8(uvuv_odd_epu8, y_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

			// Interleave and store the second eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpackhi_epi8(uvuv_even_epu8, y_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Interleave and store the second eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpackhi_epi8(uvuv_odd_epu8, y_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

		}
	}

	// The loop should have terminated at the post processing column
	assert(column == post_column);

#endif

#if 1
	// Process the remaining portion of the row
	for (; column < output_width; column += 2)
	{
		int chroma_column = column / 2;
		int output_column = column * 2;
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;
		int low, high;
		int even, odd;

		if((format&0xffff) == COLOR_FORMAT_UYVY)
		{
			k0 = output_column + 1;
			k1 = output_column;
			k2 = output_column + 3;
			k3 = output_column + 2;
		}



		// Get the lowpass and highpass coefficients for the first luma value
		low = lowpass[0][column];
		high = highpass[0][column];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)>>1;
		even = (low - high)>>1;

#if (0 && DEBUG)
		// Suppress the luminance for debugging
		odd = 0;
		even = 0;
#endif
		// Store the luma values for the even and odd rows
#if STRICT_SATURATE
		odd_field[k0] = SATURATE_Y(odd);
		even_field[k0] = SATURATE_Y(even);
#else
		odd_field[k0] = (odd);
		even_field[k0] = (even);
#endif
		// Get the lowpass and highpass coefficients for the first chroma value
		// Note that the v chroma value is the second byte in the four tuple
		low = lowpass[2][chroma_column];
		high = highpass[2][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)>>1;
		even = (low - high)>>1;

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset the values for the even and odd rows
		odd += chroma_offset;
		even += chroma_offset;
#endif

#if (0 && DEBUG)
		// Suppress the chrominance for debugging
		odd = 128;
		even = 128;
#endif
		// Store the chroma values for the even and odd rows
#if STRICT_SATURATE
		odd_field[k1] = SATURATE_Cr(odd);
		even_field[k1] = SATURATE_Cr(even);
#else
		odd_field[k1] = (odd);
		even_field[k1] = (even);
#endif

		// Get the lowpass and highpass coefficients for the second luma value
		low = lowpass[0][column + 1];
		high = highpass[0][column + 1];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)>>1;
		even = (low - high)>>1;

#if (0 && DEBUG)
		// Suppress the luminance for debugging
		odd = 0;
		even = 0;
#endif
		// Store the luma values for the even and odd rows

#if STRICT_SATURATE
		odd_field[k2] = SATURATE_Y(odd);
		even_field[k2] = SATURATE_Y(even);
#else
		odd_field[k2] = (odd);
		even_field[k2] = (even);
#endif

		// Get the lowpass and highpass coefficients for the second chroma value
		// Note that the u chroma value is the fourth byte in the four tuple
		low = lowpass[1][chroma_column];
		high = highpass[1][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)>>1;
		even = (low - high)>>1;

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset the values for the even and odd rows
		odd += chroma_offset;
		even += chroma_offset;
#endif

#if (0 && DEBUG)
		// Suppress the chrominance for debugging
		odd = 128;
		even = 128;
#endif
		// Store the chroma values for the even and odd rows
#if STRICT_SATURATE
		odd_field[k3] = SATURATE_Cb(odd);
		even_field[k3] = SATURATE_Cb(even);
#else
		odd_field[k3] = (odd);
		even_field[k3] = (even);
#endif
	}

#else

	// Fill the remaining portion of the row with a debugging value
	for (; column < output_width; column += 2)
	{
		int chroma_column = column / 2;
		int output_column = column * 2;
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;
		int low, high;
		int even, odd;
		const int DEBUG_COLOR_LUMA  = 0;
		const int DEBUG_COLOR_CHROMA = 128;

		// Get the lowpass and highpass coefficients for the first luma value
		low = lowpass[0][column];
		high = highpass[0][column];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k0] = DEBUG_COLOR_LUMA;		//SATURATE_8U(odd);
		even_field[k0] = DEBUG_COLOR_LUMA;		//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the first chroma value
		// Note that the v chroma value is the second byte in the four tuple
		low = lowpass[2][chroma_column];
		high = highpass[2][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k1] = DEBUG_COLOR_CHROMA;		//SATURATE_8U(odd);
		even_field[k1] = DEBUG_COLOR_CHROMA;	//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the second luma value
		low = lowpass[0][column + 1];
		high = highpass[0][column + 1];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k2] = DEBUG_COLOR_LUMA;		//SATURATE_8U(odd);
		even_field[k2] = DEBUG_COLOR_LUMA;		//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the second chroma value
		// Note that the u chroma value is the fourth byte in the four tuple
		low = lowpass[1][chroma_column];
		high = highpass[1][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k3] = DEBUG_COLOR_CHROMA;		//SATURATE_8U(odd);
		even_field[k3] = DEBUG_COLOR_CHROMA;	//SATURATE_8U(even);
	}
#endif

#if 0
	// Fill the rest of the row with background values
	for (; column < frame_width; column += 2)
	{
		int output_column = column * 2;
#if 1
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;

		even_field[k0] = COLOR_LUMA_BLACK;
		odd_field[k0] = COLOR_LUMA_BLACK;

		even_field[k1] = COLOR_CHROMA_ZERO;
		odd_field[k1] = COLOR_CHROMA_ZERO;

		even_field[k2] = COLOR_LUMA_BLACK;
		odd_field[k2] = COLOR_LUMA_BLACK;

		even_field[k3] = COLOR_CHROMA_ZERO;
		odd_field[k3] = COLOR_CHROMA_ZERO;
#else
		int i;

		for (i = 0; i < 4; i++) {
			even_field[output_column + i] = 0;
			odd_field[output_column + i] = 0;
		}
#endif
	}
#endif
}

#endif


// Invert the temporal bands from all channels and pack the output pixels into YUV format (this routine is SSE2 only)
void InvertInterlacedRow16s10bitToYUV(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
									  uint8_t *output, int pitch, int output_width, int frame_width,
									  int chroma_offset)
{
	uint8_t *even_field = output;
	uint8_t *odd_field = even_field + pitch;
	int column;

#if (1 && XMMOPT)
	int chroma_width = output_width/2;
	int chroma_step = 8;
	int post_column = 2 * (chroma_width - (chroma_width % chroma_step));
	int column_step = 2 * chroma_step;

	// Initialize pointers to the rows or lowpass and highpass coefficients
	__m128i *y_low_ptr = (__m128i *)lowpass[0];
	__m128i *u_low_ptr = (__m128i *)lowpass[1];
	__m128i *v_low_ptr = (__m128i *)lowpass[2];
	__m128i *y_high_ptr = (__m128i *)highpass[0];
	__m128i *u_high_ptr = (__m128i *)highpass[1];
	__m128i *v_high_ptr = (__m128i *)highpass[2];

	// Initialize the pointers to the rows in the even and odd fields
	__m128i *even_ptr = (__m128i *)even_field;
	__m128i *odd_ptr = (__m128i *)odd_field;

	__m128i overflowprotect_epi16 = _mm_set1_epi16(0x7fff-2047);

	__m128i rounding1_epi16 = _mm_set1_epi16(0);
	__m128i rounding2_epi16 = _mm_set1_epi16(0);
	int mask = 1; //was 3 DAN20090601


	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 0);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 1);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 2);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 3);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 4);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 5);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 6);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 7);

	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 0);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 1);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 2);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 3);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 4);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 5);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 6);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 7);
#endif

	// This code assumes that there are three channels
	assert(num_channels == 3);

	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(y_low_ptr));
	assert(ISALIGNED16(u_low_ptr));
	assert(ISALIGNED16(v_low_ptr));
	assert(ISALIGNED16(y_high_ptr));
	assert(ISALIGNED16(u_high_ptr));
	assert(ISALIGNED16(v_high_ptr));

	// Check that the output pointers are properly aligned
	//assert(ISALIGNED16(even_ptr));
	//assert(ISALIGNED16(odd_ptr));

	// Start at the beginning of the row
	column = 0;

#if (1 && XMMOPT)

	// Shorter loop that processes 16 output pixels per iteration
	assert(column_step == 16);

	// Process groups of pixels in the fast loop
	for (; column < post_column; column += column_step)
	{
		__m128i low1_epi16;
		__m128i low2_epi16;
		__m128i high1_epi16;
		__m128i high2_epi16;
		__m128i odd1_epi16;
		__m128i odd2_epi16;
		__m128i even1_epi16;
		__m128i even2_epi16;

		__m128i y_even_epu8;
		__m128i y_odd_epu8;
		__m128i u_even_epi16;
		__m128i v_even_epi16;
		__m128i u_odd_epi16;
		__m128i v_odd_epi16;

		__m128i uvuv1_epi16;
		__m128i uvuv2_epi16;

		__m128i uvuv_even_epu8;
		__m128i yuyv_even_epu8;
		__m128i uvuv_odd_epu8;
		__m128i yuyv_odd_epu8;


		/***** Reconstruct sixteen luma pixels for the even and odd rows *****/

		// Get the first eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(y_low_ptr++);
		high1_epi16 = _mm_load_si128(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
//		low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
		even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		even1_epi16 = _mm_adds_epi16(even1_epi16, overflowprotect_epi16);
		even1_epi16 = _mm_subs_epu16(even1_epi16, overflowprotect_epi16);
		even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		odd1_epi16 = _mm_adds_epi16(odd1_epi16, overflowprotect_epi16);
		odd1_epi16 = _mm_subs_epu16(odd1_epi16, overflowprotect_epi16);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

		// Get the second eight lowpass and eight highpass coefficients
		low2_epi16 = _mm_load_si128(y_low_ptr++);
		high2_epi16 = _mm_load_si128(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
//		low2_epi16 = _mm_adds_epi16(low2_epi16, overflowprotect_epi16);
		even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
		even2_epi16 = _mm_adds_epi16(even2_epi16, overflowprotect_epi16);
		even2_epi16 = _mm_subs_epu16(even2_epi16, overflowprotect_epi16);
		even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
		odd2_epi16 = _mm_adds_epi16(odd2_epi16, overflowprotect_epi16);
		odd2_epi16 = _mm_subs_epu16(odd2_epi16, overflowprotect_epi16);
		odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

		// Reduce the output values to eight bits
		even1_epi16 = _mm_adds_epi16(even1_epi16, rounding1_epi16);
		even1_epi16 = _mm_srai_epi16(even1_epi16, PRESCALE_V210_OUTPUT);
		even2_epi16 = _mm_adds_epi16(even2_epi16, rounding2_epi16);
		even2_epi16 = _mm_srai_epi16(even2_epi16, PRESCALE_V210_OUTPUT);

		// Pack sixteen bytes of luma in the even row
		y_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

#if STRICT_SATURATE
		// Perform strict saturation on YUV if required
		y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(16));
		y_even_epu8 = _mm_adds_epu8(y_even_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
		y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(20));
#endif
		// Reduce the output values to eight bits
		odd1_epi16 = _mm_adds_epi16(odd1_epi16, rounding2_epi16);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, PRESCALE_V210_OUTPUT);
		odd2_epi16 = _mm_adds_epi16(odd2_epi16, rounding1_epi16);
		odd2_epi16 = _mm_srai_epi16(odd2_epi16, PRESCALE_V210_OUTPUT);

		// Pack sixteen bytes of luma in the odd row
		y_odd_epu8 = _mm_packus_epi16(odd1_epi16, odd2_epi16);

#if STRICT_SATURATE
		// Perform strict saturation on YUV if required
		y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(16));
		y_odd_epu8 = _mm_adds_epu8(y_odd_epu8, _mm_set1_epi8(36));		// 36 = 16 + 20 = 16 + (255-235)
		y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(20));
#endif

		/***** Reconstruct eight u chroma pixels for the even and odd rows *****/

		// Load eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(u_low_ptr++);
		high1_epi16 = _mm_load_si128(u_high_ptr++);

		// Reconstruct the u chroma pixels in the even row
//		low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
		u_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		u_even_epi16 = _mm_adds_epi16(u_even_epi16, overflowprotect_epi16);
		u_even_epi16 = _mm_subs_epu16(u_even_epi16, overflowprotect_epi16);
		u_even_epi16 = _mm_srai_epi16(u_even_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		u_even_epi16 = _mm_subs_epu8(u_even_epi16, _mm_set1_epi8(16));
		u_even_epi16 = _mm_adds_epu8(u_even_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		u_even_epi16 = _mm_subs_epu8(u_even_epi16, _mm_set1_epi8(15));
#endif
		// Reconstruct the u chroma pixels in the odd row
		u_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, overflowprotect_epi16);
		u_odd_epi16 = _mm_subs_epu16(u_odd_epi16, overflowprotect_epi16);
		u_odd_epi16 = _mm_srai_epi16(u_odd_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		u_odd_epi16 = _mm_subs_epu8(u_odd_epi16, _mm_set1_epi8(16));
		u_odd_epi16 = _mm_adds_epu8(u_odd_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		u_odd_epi16 = _mm_subs_epu8(u_odd_epi16, _mm_set1_epi8(15));
#endif

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even and odd rows
		u_even_epi16 = _mm_adds_epi16(u_even_epi16, offset_epi16);
		u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, offset_epi16);
#endif
		// Reduce the output values to eight bits
		u_even_epi16 = _mm_adds_epi16(u_even_epi16, rounding1_epi16);
		u_even_epi16 = _mm_srai_epi16(u_even_epi16, PRESCALE_V210_OUTPUT);
		u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, rounding2_epi16);
		u_odd_epi16 = _mm_srai_epi16(u_odd_epi16, PRESCALE_V210_OUTPUT);

#if (0 && DEBUG)
		u_even_epi16 = _mm_set1_epi16(128);
		u_odd_epi16 = _mm_set1_epi16(128);
#endif


		/***** Reconstruct eight v chroma pixels for the even and odd rows *****/

		// Load eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(v_low_ptr++);
		high1_epi16 = _mm_load_si128(v_high_ptr++);

		// Reconstruct the v chroma pixels in the even row
//		low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
		v_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		v_even_epi16 = _mm_adds_epi16(v_even_epi16, overflowprotect_epi16);
		v_even_epi16 = _mm_subs_epu16(v_even_epi16, overflowprotect_epi16);
		v_even_epi16 = _mm_srai_epi16(v_even_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		v_even_epi16 = _mm_subs_epu8(v_even_epi16, _mm_set1_epi8(16));
		v_even_epi16 = _mm_adds_epu8(v_even_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		v_even_epi16 = _mm_subs_epu8(v_even_epi16, _mm_set1_epi8(15));
#endif
		// Reconstruct the v chroma pixels in the odd row
		v_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, overflowprotect_epi16);
		v_odd_epi16 = _mm_subs_epu16(v_odd_epi16, overflowprotect_epi16);
		v_odd_epi16 = _mm_srai_epi16(v_odd_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		v_odd_epi16 = _mm_subs_epu8(v_odd_epi16, _mm_set1_epi8(16));
		v_odd_epi16 = _mm_adds_epu8(v_odd_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		v_odd_epi16 = _mm_subs_epu8(v_odd_epi16, _mm_set1_epi8(15));
#endif

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even and odd rows
		v_even_epi16 = _mm_adds_epi16(v_even_epi16, offset_epi16);
		v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, offset_epi16);
#endif
		// Reduce the output values to eight bits
		v_even_epi16 = _mm_adds_epi16(v_even_epi16, rounding2_epi16);
		v_even_epi16 = _mm_srai_epi16(v_even_epi16, PRESCALE_V210_OUTPUT);
		v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, rounding1_epi16);
		v_odd_epi16 = _mm_srai_epi16(v_odd_epi16, PRESCALE_V210_OUTPUT);

#if (0 && DEBUG)
		v_even_epi16 = _mm_set1_epi16(128);
		v_odd_epi16 = _mm_set1_epi16(128);
#endif


		/***** Interleave the sixteen bytes of luma with alternating chroma *****/

		// Interleave the first four chroma for the even row
		uvuv1_epi16 = _mm_unpacklo_epi16(v_even_epi16, u_even_epi16);

		// Interleave the second four chroma for the even row
		uvuv2_epi16 = _mm_unpackhi_epi16(v_even_epi16, u_even_epi16);

		// Pack sixteen alternating chroma for the even row
		uvuv_even_epu8 = _mm_packus_epi16(uvuv1_epi16, uvuv2_epi16);

		// Interleave the first four chroma for the odd row
		uvuv1_epi16 = _mm_unpacklo_epi16(v_odd_epi16, u_odd_epi16);

		// Interleave the second four chroma for the odd row
		uvuv2_epi16 = _mm_unpackhi_epi16(v_odd_epi16, u_odd_epi16);

		// Pack sixteen alternating chroma for the odd row
		uvuv_odd_epu8 = _mm_packus_epi16(uvuv1_epi16, uvuv2_epi16);

		// Interleave and store the first eight luma and chroma pairs for the even row
		yuyv_even_epu8 = _mm_unpacklo_epi8(y_even_epu8, uvuv_even_epu8);
		_mm_store_si128(even_ptr++, yuyv_even_epu8);

		// Interleave and store the first eight luma and chroma pairs for the odd row
		yuyv_odd_epu8 = _mm_unpacklo_epi8(y_odd_epu8, uvuv_odd_epu8);
		_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

		// Interleave and store the second eight luma and chroma pairs for the even row
		yuyv_even_epu8 = _mm_unpackhi_epi8(y_even_epu8, uvuv_even_epu8);
		_mm_store_si128(even_ptr++, yuyv_even_epu8);

		// Interleave and store the second eight luma and chroma pairs for the odd row
		yuyv_odd_epu8 = _mm_unpackhi_epi8(y_odd_epu8, uvuv_odd_epu8);
		_mm_store_si128(odd_ptr++, yuyv_odd_epu8);
	}

	// The loop should have terminated at the post processing column
	assert(column == post_column);

#endif

#if 1
	// Process the remaining portion of the row
	for (; column < output_width; column += 2)
	{
		int chroma_column = column / 2;
		int output_column = column * 2;
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;
		int low, high;
		int even, odd;

		// Get the lowpass and highpass coefficients for the first luma value
		low = lowpass[0][column];
		high = highpass[0][column];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

#if (0 && DEBUG)
		// Suppress the luminance for debugging
		odd = 0;
		even = 0;
#endif
		// Reduce the output values to eight bits
		even >>= PRESCALE_V210_OUTPUT;
		odd >>= PRESCALE_V210_OUTPUT;

		if(odd > 255) odd = 255;
		if(even > 255) even = 255;
		if(odd < 0) odd = 0;
		if(even < 0) even = 0;

		// Store the luma values for the even and odd rows
		odd_field[k0] = SATURATE_Y(odd);
		even_field[k0] = SATURATE_Y(even);

		// Get the lowpass and highpass coefficients for the first chroma value
		// Note that the v chroma value is the second byte in the four tuple
		low = lowpass[2][chroma_column];
		high = highpass[2][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset the values for the even and odd rows
		odd += chroma_offset;
		even += chroma_offset;
#endif

#if (0 && DEBUG)
		// Suppress the chrominance for debugging
		odd = 128;
		even = 128;
#endif
		// Reduce the output values to eight bits
		even >>= PRESCALE_V210_OUTPUT;
		odd >>= PRESCALE_V210_OUTPUT;

		if(odd > 255) odd = 255;
		if(even > 255) even = 255;
		if(odd < 0) odd = 0;
		if(even < 0) even = 0;

		// Store the chroma values for the even and odd rows
		odd_field[k1] = SATURATE_Cr(odd);
		even_field[k1] = SATURATE_Cr(even);

		// Get the lowpass and highpass coefficients for the second luma value
		low = lowpass[0][column + 1];
		high = highpass[0][column + 1];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

#if (0 && DEBUG)
		// Suppress the luminance for debugging
		odd = 0;
		even = 0;
#endif
		// Reduce the output values to eight bits
		even >>= PRESCALE_V210_OUTPUT;
		odd >>= PRESCALE_V210_OUTPUT;

		if(odd > 255) odd = 255;
		if(even > 255) even = 255;
		if(odd < 0) odd = 0;
		if(even < 0) even = 0;

		// Store the luma values for the even and odd rows
		odd_field[k2] = SATURATE_Y(odd);
		even_field[k2] = SATURATE_Y(even);

		// Get the lowpass and highpass coefficients for the second chroma value
		// Note that the u chroma value is the fourth byte in the four tuple
		low = lowpass[1][chroma_column];
		high = highpass[1][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset the values for the even and odd rows
		odd += chroma_offset;
		even += chroma_offset;
#endif

#if (0 && DEBUG)
		// Suppress the chrominance for debugging
		odd = 128;
		even = 128;
#endif
		// Reduce the output values to eight bits
		even >>= PRESCALE_V210_OUTPUT;
		odd >>= PRESCALE_V210_OUTPUT;

		if(odd > 255) odd = 255;
		if(even > 255) even = 255;
		if(odd < 0) odd = 0;
		if(even < 0) even = 0;

		// Store the chroma values for the even and odd rows
		odd_field[k3] = SATURATE_Cb(odd);
		even_field[k3] = SATURATE_Cb(even);
	}

#else

	// Fill the remaining portion of the row with a debugging value
	for (; column < output_width; column += 2)
	{
		int chroma_column = column / 2;
		int output_column = column * 2;
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;
		int low, high;
		int even, odd;
		const int DEBUG_COLOR_LUMA  = 0;
		const int DEBUG_COLOR_CHROMA = 128;

		// Get the lowpass and highpass coefficients for the first luma value
		low = lowpass[0][column];
		high = highpass[0][column];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k0] = DEBUG_COLOR_LUMA;		//SATURATE_8U(odd);
		even_field[k0] = DEBUG_COLOR_LUMA;		//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the first chroma value
		// Note that the v chroma value is the second byte in the four tuple
		low = lowpass[2][chroma_column];
		high = highpass[2][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k1] = DEBUG_COLOR_CHROMA;		//SATURATE_8U(odd);
		even_field[k1] = DEBUG_COLOR_CHROMA;	//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the second luma value
		low = lowpass[0][column + 1];
		high = highpass[0][column + 1];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k2] = DEBUG_COLOR_LUMA;		//SATURATE_8U(odd);
		even_field[k2] = DEBUG_COLOR_LUMA;		//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the second chroma value
		// Note that the u chroma value is the fourth byte in the four tuple
		low = lowpass[1][chroma_column];
		high = highpass[1][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k3] = DEBUG_COLOR_CHROMA;		//SATURATE_8U(odd);
		even_field[k3] = DEBUG_COLOR_CHROMA;	//SATURATE_8U(even);
	}
#endif

#if 0
	// Fill the rest of the row with background values
	for (; column < frame_width; column += 2)
	{
		int output_column = column * 2;
#if 1
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;

		even_field[k0] = COLOR_LUMA_BLACK;
		odd_field[k0] = COLOR_LUMA_BLACK;

		even_field[k1] = COLOR_CHROMA_ZERO;
		odd_field[k1] = COLOR_CHROMA_ZERO;

		even_field[k2] = COLOR_LUMA_BLACK;
		odd_field[k2] = COLOR_LUMA_BLACK;

		even_field[k3] = COLOR_CHROMA_ZERO;
		odd_field[k3] = COLOR_CHROMA_ZERO;
#else
		int i;

		for (i = 0; i < 4; i++) {
			even_field[output_column + i] = 0;
			odd_field[output_column + i] = 0;
		}
#endif
	}
#endif
}

// Invert the temporal bands from all channels and pack the output pixels into UYVY format (this routine is SSE2 only)
void InvertInterlacedRow16s10bitToUYVY(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
									   uint8_t *output, int pitch, int output_width, int frame_width,
									   int chroma_offset)
{
	uint8_t *even_field = output;
	uint8_t *odd_field = even_field + pitch;
	int column;

#if (1 && XMMOPT)
	int chroma_width = output_width/2;
	int chroma_step = 8;
	int post_column = 2 * (chroma_width - (chroma_width % chroma_step));
	int column_step = 2 * chroma_step;

	// Initialize pointers to the rows or lowpass and highpass coefficients
	__m128i *y_low_ptr = (__m128i *)lowpass[0];
	__m128i *u_low_ptr = (__m128i *)lowpass[2];
	__m128i *v_low_ptr = (__m128i *)lowpass[1];
	__m128i *y_high_ptr = (__m128i *)highpass[0];
	__m128i *u_high_ptr = (__m128i *)highpass[2];
	__m128i *v_high_ptr = (__m128i *)highpass[1];

	// Initialize the pointers to the rows in the even and odd fields
	__m128i *even_ptr = (__m128i *)even_field;
	__m128i *odd_ptr = (__m128i *)odd_field;

	__m128i overflowprotect_epi16 = _mm_set1_epi16(0x7fff-2047);

	__m128i rounding1_epi16 = _mm_set1_epi16(2);
	__m128i rounding2_epi16 = _mm_set1_epi16(2);
	int mask = 1; //was 3 DAN20090601


	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 0);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 1);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 2);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 3);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 4);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 5);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 6);
	rounding1_epi16 = _mm_insert_epi16(rounding1_epi16, rand()&mask, 7);

	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 0);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 1);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 2);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 3);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 4);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 5);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 6);
	rounding2_epi16 = _mm_insert_epi16(rounding2_epi16, rand()&mask, 7);
#endif

	// This code assumes that there are three channels
	assert(num_channels == 3);

	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(y_low_ptr));
	assert(ISALIGNED16(u_low_ptr));
	assert(ISALIGNED16(v_low_ptr));
	assert(ISALIGNED16(y_high_ptr));
	assert(ISALIGNED16(u_high_ptr));
	assert(ISALIGNED16(v_high_ptr));

	// Check that the output pointers are properly aligned
	//assert(ISALIGNED16(even_ptr));
	//assert(ISALIGNED16(odd_ptr));

	// Start at the beginning of the row
	column = 0;

#if (1 && XMMOPT)

	// Shorter loop that processes 16 output pixels per iteration
	assert(column_step == 16);

	// Process groups of pixels in the fast loop
	for (; column < post_column; column += column_step)
	{
		__m128i low1_epi16;
		__m128i low2_epi16;
		__m128i high1_epi16;
		__m128i high2_epi16;
		__m128i odd1_epi16;
		__m128i odd2_epi16;
		__m128i even1_epi16;
		__m128i even2_epi16;

		__m128i y_even_epu8;
		__m128i y_odd_epu8;
		__m128i u_even_epi16;
		__m128i v_even_epi16;
		__m128i u_odd_epi16;
		__m128i v_odd_epi16;

		__m128i uvuv1_epi16;
		__m128i uvuv2_epi16;

		__m128i uvuv_even_epu8;
		__m128i uyvy_even_epu8;
		__m128i uvuv_odd_epu8;
		__m128i uyvy_odd_epu8;


		/***** Reconstruct sixteen luma pixels for the even and odd rows *****/

		// Get the first eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(y_low_ptr++);
		high1_epi16 = _mm_load_si128(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		//		low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
		even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		even1_epi16 = _mm_adds_epi16(even1_epi16, overflowprotect_epi16);
		even1_epi16 = _mm_subs_epu16(even1_epi16, overflowprotect_epi16);
		even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		odd1_epi16 = _mm_adds_epi16(odd1_epi16, overflowprotect_epi16);
		odd1_epi16 = _mm_subs_epu16(odd1_epi16, overflowprotect_epi16);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

		// Get the second eight lowpass and eight highpass coefficients
		low2_epi16 = _mm_load_si128(y_low_ptr++);
		high2_epi16 = _mm_load_si128(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		//		low2_epi16 = _mm_adds_epi16(low2_epi16, overflowprotect_epi16);
		even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
		even2_epi16 = _mm_adds_epi16(even2_epi16, overflowprotect_epi16);
		even2_epi16 = _mm_subs_epu16(even2_epi16, overflowprotect_epi16);
		even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

		// Reconstruct the luma pixels in the odd row
		odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
		odd2_epi16 = _mm_adds_epi16(odd2_epi16, overflowprotect_epi16);
		odd2_epi16 = _mm_subs_epu16(odd2_epi16, overflowprotect_epi16);
		odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

		// Reduce the output values to eight bits
		even1_epi16 = _mm_adds_epi16(even1_epi16, rounding1_epi16);
		even1_epi16 = _mm_srai_epi16(even1_epi16, PRESCALE_V210_OUTPUT);
		even2_epi16 = _mm_adds_epi16(even2_epi16, rounding2_epi16);
		even2_epi16 = _mm_srai_epi16(even2_epi16, PRESCALE_V210_OUTPUT);

		// Pack sixteen bytes of luma in the even row
		y_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

#if STRICT_SATURATE
		// Perform strict saturation on YUV if required
		y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(16));
		y_even_epu8 = _mm_adds_epu8(y_even_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
		y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(20));
#endif
		// Reduce the output values to eight bits
		odd1_epi16 = _mm_adds_epi16(odd1_epi16, rounding2_epi16);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, PRESCALE_V210_OUTPUT);
		odd2_epi16 = _mm_adds_epi16(odd2_epi16, rounding1_epi16);
		odd2_epi16 = _mm_srai_epi16(odd2_epi16, PRESCALE_V210_OUTPUT);

		// Pack sixteen bytes of luma in the odd row
		y_odd_epu8 = _mm_packus_epi16(odd1_epi16, odd2_epi16);

#if STRICT_SATURATE
		// Perform strict saturation on YUV if required
		y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(16));
		y_odd_epu8 = _mm_adds_epu8(y_odd_epu8, _mm_set1_epi8(36));		// 36 = 16 + 20 = 16 + (255-235)
		y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(20));
#endif

		/***** Reconstruct eight u chroma pixels for the even and odd rows *****/

		// Load eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(u_low_ptr++);
		high1_epi16 = _mm_load_si128(u_high_ptr++);

		// Reconstruct the u chroma pixels in the even row
		//		low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
		u_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		u_even_epi16 = _mm_adds_epi16(u_even_epi16, overflowprotect_epi16);
		u_even_epi16 = _mm_subs_epu16(u_even_epi16, overflowprotect_epi16);
		u_even_epi16 = _mm_srai_epi16(u_even_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		u_even_epi16 = _mm_subs_epu8(u_even_epi16, _mm_set1_epi8(16));
		u_even_epi16 = _mm_adds_epu8(u_even_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		u_even_epi16 = _mm_subs_epu8(u_even_epi16, _mm_set1_epi8(15));
#endif
		// Reconstruct the u chroma pixels in the odd row
		u_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, overflowprotect_epi16);
		u_odd_epi16 = _mm_subs_epu16(u_odd_epi16, overflowprotect_epi16);
		u_odd_epi16 = _mm_srai_epi16(u_odd_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		u_odd_epi16 = _mm_subs_epu8(u_odd_epi16, _mm_set1_epi8(16));
		u_odd_epi16 = _mm_adds_epu8(u_odd_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		u_odd_epi16 = _mm_subs_epu8(u_odd_epi16, _mm_set1_epi8(15));
#endif

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even and odd rows
		u_even_epi16 = _mm_adds_epi16(u_even_epi16, offset_epi16);
		u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, offset_epi16);
#endif
		// Reduce the output values to eight bits
		u_even_epi16 = _mm_adds_epi16(u_even_epi16, rounding1_epi16);
		u_even_epi16 = _mm_srai_epi16(u_even_epi16, PRESCALE_V210_OUTPUT);
		u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, rounding2_epi16);
		u_odd_epi16 = _mm_srai_epi16(u_odd_epi16, PRESCALE_V210_OUTPUT);

#if (0 && DEBUG)
		u_even_epi16 = _mm_set1_epi16(128);
		u_odd_epi16 = _mm_set1_epi16(128);
#endif


		/***** Reconstruct eight v chroma pixels for the even and odd rows *****/

		// Load eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(v_low_ptr++);
		high1_epi16 = _mm_load_si128(v_high_ptr++);

		// Reconstruct the v chroma pixels in the even row
		//		low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
		v_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		v_even_epi16 = _mm_adds_epi16(v_even_epi16, overflowprotect_epi16);
		v_even_epi16 = _mm_subs_epu16(v_even_epi16, overflowprotect_epi16);
		v_even_epi16 = _mm_srai_epi16(v_even_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		v_even_epi16 = _mm_subs_epu8(v_even_epi16, _mm_set1_epi8(16));
		v_even_epi16 = _mm_adds_epu8(v_even_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		v_even_epi16 = _mm_subs_epu8(v_even_epi16, _mm_set1_epi8(15));
#endif
		// Reconstruct the v chroma pixels in the odd row
		v_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, overflowprotect_epi16);
		v_odd_epi16 = _mm_subs_epu16(v_odd_epi16, overflowprotect_epi16);
		v_odd_epi16 = _mm_srai_epi16(v_odd_epi16, 1);

#if (0 && STRICT_SATURATE)
		// Perform strict saturation on chroma if required
		v_odd_epi16 = _mm_subs_epu8(v_odd_epi16, _mm_set1_epi8(16));
		v_odd_epi16 = _mm_adds_epu8(v_odd_epi16, _mm_set1_epi8(31));	// 31 = 16 + 15 = 16 + (255-240)
		v_odd_epi16 = _mm_subs_epu8(v_odd_epi16, _mm_set1_epi8(15));
#endif

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even and odd rows
		v_even_epi16 = _mm_adds_epi16(v_even_epi16, offset_epi16);
		v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, offset_epi16);
#endif
		// Reduce the output values to eight bits
		v_even_epi16 = _mm_adds_epi16(v_even_epi16, rounding2_epi16);
		v_even_epi16 = _mm_srai_epi16(v_even_epi16, PRESCALE_V210_OUTPUT);
		v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, rounding1_epi16);
		v_odd_epi16 = _mm_srai_epi16(v_odd_epi16, PRESCALE_V210_OUTPUT);

#if (0 && DEBUG)
		v_even_epi16 = _mm_set1_epi16(128);
		v_odd_epi16 = _mm_set1_epi16(128);
#endif


		/***** Interleave the sixteen bytes of luma with alternating chroma *****/

		// Interleave the first four chroma for the even row
		uvuv1_epi16 = _mm_unpacklo_epi16(u_even_epi16, v_even_epi16);

		// Interleave the second four chroma for the even row
		uvuv2_epi16 = _mm_unpackhi_epi16(u_even_epi16, v_even_epi16);

		// Pack sixteen alternating chroma for the even row
		uvuv_even_epu8 = _mm_packus_epi16(uvuv1_epi16, uvuv2_epi16);

		// Interleave the first four chroma for the odd row
		uvuv1_epi16 = _mm_unpacklo_epi16(u_odd_epi16, v_odd_epi16);

		// Interleave the second four chroma for the odd row
		uvuv2_epi16 = _mm_unpackhi_epi16(u_odd_epi16, v_odd_epi16);

		// Pack sixteen alternating chroma for the odd row
		uvuv_odd_epu8 = _mm_packus_epi16(uvuv1_epi16, uvuv2_epi16);

		// Interleave and store the first eight luma and chroma pairs for the even row
		uyvy_even_epu8 = _mm_unpacklo_epi8(uvuv_even_epu8, y_even_epu8);
		_mm_store_si128(even_ptr++, uyvy_even_epu8);

		// Interleave and store the first eight luma and chroma pairs for the odd row
		uyvy_odd_epu8 = _mm_unpacklo_epi8(uvuv_odd_epu8, y_odd_epu8);
		_mm_store_si128(odd_ptr++, uyvy_odd_epu8);

		// Interleave and store the second eight luma and chroma pairs for the even row
		uyvy_even_epu8 = _mm_unpackhi_epi8(uvuv_even_epu8, y_even_epu8);
		_mm_store_si128(even_ptr++, uyvy_even_epu8);

		// Interleave and store the second eight luma and chroma pairs for the odd row
		uyvy_odd_epu8 = _mm_unpackhi_epi8(uvuv_odd_epu8, y_odd_epu8);
		_mm_store_si128(odd_ptr++, uyvy_odd_epu8);
	}

	// The loop should have terminated at the post processing column
	assert(column == post_column);

#endif

#if 1
	// Process the remaining portion of the row
	for (; column < output_width; column += 2)
	{
		int chroma_column = column / 2;
		int output_column = column * 2;
#if 0
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;
#else
		int k0 = output_column + 1;
		int k1 = output_column + 0;
		int k2 = output_column + 3;
		int k3 = output_column + 2;
#endif
		int low, high;
		int even, odd;

		// Get the lowpass and highpass coefficients for the first luma value
		low = lowpass[0][column];
		high = highpass[0][column];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

#if (0 && DEBUG)
		// Suppress the luminance for debugging
		odd = 0;
		even = 0;
#endif
		// Reduce the output values to eight bits
		even >>= PRESCALE_V210_OUTPUT;
		odd >>= PRESCALE_V210_OUTPUT;

		if(odd > 255) odd = 255;
		if(even > 255) even = 255;
		if(odd < 0) odd = 0;
		if(even < 0) even = 0;

		// Store the luma values for the even and odd rows
		odd_field[k0] = SATURATE_Y(odd);
		even_field[k0] = SATURATE_Y(even);

		// Get the lowpass and highpass coefficients for the first chroma value
		// Note that the v chroma value is the second byte in the four tuple
		low = lowpass[2][chroma_column];
		high = highpass[2][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset the values for the even and odd rows
		odd += chroma_offset;
		even += chroma_offset;
#endif

#if (0 && DEBUG)
		// Suppress the chrominance for debugging
		odd = 128;
		even = 128;
#endif
		// Reduce the output values to eight bits
		even >>= PRESCALE_V210_OUTPUT;
		odd >>= PRESCALE_V210_OUTPUT;

		if(odd > 255) odd = 255;
		if(even > 255) even = 255;
		if(odd < 0) odd = 0;
		if(even < 0) even = 0;

		// Store the chroma values for the even and odd rows
		odd_field[k1] = SATURATE_Cr(odd);
		even_field[k1] = SATURATE_Cr(even);

		// Get the lowpass and highpass coefficients for the second luma value
		low = lowpass[0][column + 1];
		high = highpass[0][column + 1];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

#if (0 && DEBUG)
		// Suppress the luminance for debugging
		odd = 0;
		even = 0;
#endif
		// Reduce the output values to eight bits
		even >>= PRESCALE_V210_OUTPUT;
		odd >>= PRESCALE_V210_OUTPUT;

		if(odd > 255) odd = 255;
		if(even > 255) even = 255;
		if(odd < 0) odd = 0;
		if(even < 0) even = 0;

		// Store the luma values for the even and odd rows
		odd_field[k2] = SATURATE_Y(odd);
		even_field[k2] = SATURATE_Y(even);

		// Get the lowpass and highpass coefficients for the second chroma value
		// Note that the u chroma value is the fourth byte in the four tuple
		low = lowpass[1][chroma_column];
		high = highpass[1][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset the values for the even and odd rows
		odd += chroma_offset;
		even += chroma_offset;
#endif

#if (0 && DEBUG)
		// Suppress the chrominance for debugging
		odd = 128;
		even = 128;
#endif
		// Reduce the output values to eight bits
		even >>= PRESCALE_V210_OUTPUT;
		odd >>= PRESCALE_V210_OUTPUT;

		if(odd > 255) odd = 255;
		if(even > 255) even = 255;
		if(odd < 0) odd = 0;
		if(even < 0) even = 0;

		// Store the chroma values for the even and odd rows
		odd_field[k3] = SATURATE_Cb(odd);
		even_field[k3] = SATURATE_Cb(even);
	}

#else

	// Fill the remaining portion of the row with a debugging value
	for (; column < output_width; column += 2)
	{
		int chroma_column = column / 2;
		int output_column = column * 2;
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;
		int low, high;
		int even, odd;
		const int DEBUG_COLOR_LUMA  = 0;
		const int DEBUG_COLOR_CHROMA = 128;

		// Get the lowpass and highpass coefficients for the first luma value
		low = lowpass[0][column];
		high = highpass[0][column];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k0] = DEBUG_COLOR_LUMA;		//SATURATE_8U(odd);
		even_field[k0] = DEBUG_COLOR_LUMA;		//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the first chroma value
		// Note that the v chroma value is the second byte in the four tuple
		low = lowpass[2][chroma_column];
		high = highpass[2][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k1] = DEBUG_COLOR_CHROMA;		//SATURATE_8U(odd);
		even_field[k1] = DEBUG_COLOR_CHROMA;	//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the second luma value
		low = lowpass[0][column + 1];
		high = highpass[0][column + 1];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k2] = DEBUG_COLOR_LUMA;		//SATURATE_8U(odd);
		even_field[k2] = DEBUG_COLOR_LUMA;		//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the second chroma value
		// Note that the u chroma value is the fourth byte in the four tuple
		low = lowpass[1][chroma_column];
		high = highpass[1][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;
		odd_field[k3] = DEBUG_COLOR_CHROMA;		//SATURATE_8U(odd);
		even_field[k3] = DEBUG_COLOR_CHROMA;	//SATURATE_8U(even);
	}
#endif

#if 0
	// Fill the rest of the row with background values
	for (; column < frame_width; column += 2)
	{
		int output_column = column * 2;
#if 1
#if 0
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;
#else
		int k0 = output_column + 1;
		int k1 = output_column + 0;
		int k2 = output_column + 3;
		int k3 = output_column + 2;
#endif

		even_field[k0] = COLOR_LUMA_BLACK;
		odd_field[k0] = COLOR_LUMA_BLACK;

		even_field[k1] = COLOR_CHROMA_ZERO;
		odd_field[k1] = COLOR_CHROMA_ZERO;

		even_field[k2] = COLOR_LUMA_BLACK;
		odd_field[k2] = COLOR_LUMA_BLACK;

		even_field[k3] = COLOR_CHROMA_ZERO;
		odd_field[k3] = COLOR_CHROMA_ZERO;
#else
		int i;

		for (i = 0; i < 4; i++) {
			even_field[output_column + i] = 0;
			odd_field[output_column + i] = 0;
		}
#endif
	}
#endif
}


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertInterlacedRow16sToRow16u(PIXEL *lowpass, PIXEL *highpass,
									PIXEL16U *output, int pitch, int output_width,
									int frame_width, int chroma_offset, int precision)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

void InvertInterlacedRow16sToRow16u(PIXEL *lowpass, PIXEL *highpass,
									PIXEL16U *output, int pitch, int output_width,
									int frame_width, int chroma_offset, int precision)
{
	int InvertInterlacedRow16sToRow16uNotMMX_yet = 0;
	assert(InvertInterlacedRow16sToRow16uNotMMX_yet);
}
#endif //_PROCESSOR_GENERIC


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

/***SSE2 Only***/

// Invert the temporal bands from one channel and output sixteen bit pixels
void InvertInterlacedRow16sToRow16u(PIXEL *lowpass, PIXEL *highpass,
									PIXEL16U *output, int pitch, int output_width,
									int frame_width, int chroma_offset, int precision)
{
	PIXEL *even_field = (PIXEL *)output;
	PIXEL *odd_field = even_field + pitch/sizeof(PIXEL);

	// Compute the shift required to scale the results to sixteen bits
	int scale = (precision == CODEC_PRECISION_8BIT) ? 8 : 6;

#if (1 && XMMOPT)
	int column_step = 8;
	int post_column = output_width - (output_width % column_step);

	// Initialize pointers to the rows or lowpass and highpass coefficients
	__m128i *lowpass_ptr = (__m128i *)lowpass;
	__m128i *highpass_ptr = (__m128i *)highpass;

	// Initialize the pointers to the rows in the even and odd fields
	__m128i *even_ptr = (__m128i *)even_field;
	__m128i *odd_ptr = (__m128i *)odd_field;

	//__m128i offset_epi16 = _mm_set1_epi16(chroma_offset);

	int protect = (precision == CODEC_PRECISION_8BIT) ? 511 : 2047;
	__m128i overflowprotect_epi16 = _mm_set1_epi16(0x7fff - protect);
#endif

	// Start at the beginning of the row
	int column = 0;

#if (1 && XMMOPT)

	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(lowpass_ptr));
	assert(ISALIGNED16(highpass_ptr));

	// Check that the output pointers are properly aligned
	//assert(ISALIGNED16(even_ptr));
	//assert(ISALIGNED16(odd_ptr));

	// Process eight output pixels per iteration
	assert(column_step == 8);

	// Process groups of pixels in the fast loop
	for (; column < post_column; column += column_step)
	{
		__m128i low1_epi16;
		//__m128i low2_epi16;
		__m128i high1_epi16;
		//__m128i high2_epi16;
		__m128i odd1_epi16;
		//__m128i odd2_epi16;
		__m128i even1_epi16;
		//__m128i even2_epi16;


		/***** Reconstruct eight pixels for the even and odd rows *****/

		// Get eight lowpass and eight highpass coefficients
		low1_epi16 = _mm_load_si128(lowpass_ptr++);
		high1_epi16 = _mm_load_si128(highpass_ptr++);

		// Reconstruct the pixels in the even row
	//	low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
		even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		even1_epi16 = _mm_adds_epi16(even1_epi16, overflowprotect_epi16);
		even1_epi16 = _mm_subs_epu16(even1_epi16, overflowprotect_epi16);
		even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

		// Scale the even results to the full 16-bit range
		even1_epi16 = _mm_slli_epi16(even1_epi16, scale);

		// Store eight even pixels
		_mm_storeu_si128(even_ptr++, even1_epi16);

		// Reconstruct the pixels in the odd row
		odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		odd1_epi16 = _mm_adds_epi16(odd1_epi16, overflowprotect_epi16);
		odd1_epi16 = _mm_subs_epu16(odd1_epi16, overflowprotect_epi16);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

		// Scale the odd results to the full 16-bit range
		odd1_epi16 = _mm_slli_epi16(odd1_epi16, scale);

		// Store eight odd pixels
		_mm_storeu_si128(odd_ptr++, odd1_epi16);
	}

	// The loop should have terminated at the post processing column
	assert(column == post_column);
#endif

	if(precision == CODEC_PRECISION_8BIT)
	{
		// Process the remaining portion of the row
		for (; column < output_width; column++)
		{
			int low, high;
			int even, odd;

			// Get the lowpass and highpass coefficients
			low = lowpass[column];
			high = highpass[column];

			// Reconstruct the luma in the even and odd rows
			even = (low - high)/2;
			odd = (low + high)/2;

			if(even < 0) even = 0;
			if(even > 255) even = 255;
			if(odd < 0) odd = 0;
			if(odd > 255) odd = 255;

			// Scale the output values to the full 16-bit range
			even <<= scale;
			odd <<= scale;

			// Store the output values for the even and odd rows
			even_field[column] = even;
			odd_field[column] = odd;
		}
	}
	else
	{
		// Process the remaining portion of the row
		for (; column < output_width; column++)
		{
			int low, high;
			int even, odd;

			// Get the lowpass and highpass coefficients
			low = lowpass[column];
			high = highpass[column];

			// Reconstruct the luma in the even and odd rows
			even = (low - high)/2;
			odd = (low + high)/2;

			if(even < 0) even = 0;
			if(even > 1023) even = 1023;
			if(odd < 0) odd = 0;
			if(odd > 1023) odd = 1023;

			// Scale the output values to the full 16-bit range
			even <<= scale;
			odd <<= scale;

			// Store the output values for the even and odd rows
			even_field[column] = even;
			odd_field[column] = odd;
		}	}
}

#endif

static unsigned int g_seed = 1;

//Used to seed the generator.
/*inline*/ void fast_srand( int seed )
{
   g_seed = seed;
}


//fastrand routine returns one integer, similar output value range as C lib.
/*inline*/ int fast_rand()
{
   g_seed = (214013*g_seed+2531011);
   return (g_seed>>16)&0x7FFF;
}


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertInterlacedRow16s(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
							uint8_t *output, int pitch, int output_width, int frame_width,
							char *buffer, size_t buffer_size,
							int format, int colorspace, int chroma_offset, int precision, int row)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

#define _PACKS_PU16 1		// Not sure if should use unsigned pack

// Invert the temporal bands from all channels and pack the output pixels
void InvertInterlacedRow16s(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
							uint8_t *output, int pitch, int output_width, int frame_width,
							char *buffer, size_t buffer_size,
							int format, int colorspace, int chroma_offset, int precision, int row)
{
	int row_size = abs(pitch);
	int shift = precision - 8;

	uint8_t *even_field = (uint8_t *)buffer;
	uint8_t *odd_field = even_field + row_size;

	uint8_t *even_output = output;
	uint8_t *odd_output = even_output + pitch;

	int column;

#if (1 && XMMOPT)
	int chroma_width = output_width/2;
	int chroma_step = 8;
	int post_column = 2 * (chroma_width - (chroma_width % chroma_step));
	int column_step = 2 * chroma_step;

	// Initialize pointers to the rows or lowpass and highpass coefficients
	__m64 *y_low_ptr = (__m64 *)lowpass[0];
	__m64 *u_low_ptr = (__m64 *)lowpass[1];
	__m64 *v_low_ptr = (__m64 *)lowpass[2];
	__m64 *y_high_ptr = (__m64 *)highpass[0];
	__m64 *u_high_ptr = (__m64 *)highpass[1];
	__m64 *v_high_ptr = (__m64 *)highpass[2];

	// Initialize the pointers to the rows in the even and odd fields
	__m64 *even_ptr = (__m64 *)even_field;
	__m64 *odd_ptr = (__m64 *)odd_field;

#if _ENCODE_CHROMA_OFFSET
	__m64 offset_pi16 = _mm_set1_pi16(chroma_offset);
#endif
#endif

	// This code assumes that there are three channels
	assert(num_channels == 3);

#if 0
	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(y_low_ptr));
	assert(ISALIGNED16(u_low_ptr));
	assert(ISALIGNED16(v_low_ptr));
	assert(ISALIGNED16(y_high_ptr));
	assert(ISALIGNED16(u_high_ptr));
	assert(ISALIGNED16(v_high_ptr));
#endif

	// This code assumes that there are three channels
	assert(num_channels == 3);

	// Start at the beginning of the row
	column = 0;

#if (1 && XMMOPT)

	for (; column < post_column; column += column_step)
	{
		__m64 low1_pi16;
		__m64 low2_pi16;
		__m64 high1_pi16;
		__m64 high2_pi16;
		__m64 odd1_pi16;
		__m64 odd2_pi16;
		__m64 even1_pi16;
		__m64 even2_pi16;

		__m64 y_even_pu8;
		__m64 u_even_pu8;
		__m64 v_even_pu8;
		__m64 y_odd_pu8;
		__m64 u_odd_pu8;
		__m64 v_odd_pu8;

		__m64 uvuv_even_pu8;
		__m64 yuyv_even_pu8;
		__m64 uvuv_odd_pu8;
		__m64 yuyv_odd_pu8;


		/***** Reconstruct the first eight luma pixels for the even and odd rows *****/

		// Get the first four lowpass and four highpass coefficients
		low1_pi16 = *(y_low_ptr++);
		high1_pi16 = *(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
		even1_pi16 = _mm_srai_pi16(even1_pi16, 1+shift);

		// Reconstruct the luma pixels in the odd row
		odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
		odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1+shift);

		// Get the second four lowpass and four highpass coefficients
		low2_pi16 = *(y_low_ptr++);
		high2_pi16 = *(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
		even2_pi16 = _mm_srai_pi16(even2_pi16, 1+shift);

		// Reconstruct the luma pixels in the odd row
		odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
		odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1+shift);

		// Pack eight bytes of luma in the even row
#if _PACKS_PU16
		y_even_pu8 = _mm_packs_pu16(even1_pi16, even2_pi16);
#else
		y_even_pu8 = _mm_packs_pi16(even1_pi16, even2_pi16);
#endif

		// Pack eight bytes of luma in the odd row
#if _PACKS_PU16
		y_odd_pu8 = _mm_packs_pu16(odd1_pi16, odd2_pi16);
#else
		y_odd_pu8 = _mm_packs_pi16(odd1_pi16, odd2_pi16);
#endif

		/***** Reconstruct eight u chroma pixels for the even and odd rows *****/

		// Get the first eight lowpass and four highpass coefficients
		low1_pi16 = *(u_low_ptr++);
		high1_pi16 = *(u_high_ptr++);

		// Reconstruct the u chroma pixels in the even row
		even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
		even1_pi16 = _mm_srai_pi16(even1_pi16, 1+shift);

		// Reconstruct the u chroma pixels in the odd row
		odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
		odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1+shift);

		// Get the second four lowpass and four highpass coefficients
		low2_pi16 = *(u_low_ptr++);
		high2_pi16 = *(u_high_ptr++);

		// Reconstruct the u chroma pixels in the even row
		even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
		even2_pi16 = _mm_srai_pi16(even2_pi16, 1+shift);

		// Reconstruct the u chroma pixels in the odd row
		odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
		odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1+shift);

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even row
		even1_pi16 = _mm_adds_pi16(even1_pi16, offset_pi16);
		even2_pi16 = _mm_adds_pi16(even2_pi16, offset_pi16);

		// Add the chroma offset to the odd row
		odd1_pi16 = _mm_adds_pi16(odd1_pi16, offset_pi16);
		odd2_pi16 = _mm_adds_pi16(odd2_pi16, offset_pi16);
#endif

		// Pack eight bytes of u chroma in the even row
#if _PACKS_PU16
		u_even_pu8 = _mm_packs_pu16(even1_pi16, even2_pi16);
#else
		u_even_pu8 = _mm_packs_pi16(even1_pi16, even2_pi16);
#endif
		// Pack eight bytes of u chroma in the odd row
#if _PACKS_PU16
		u_odd_pu8 =  _mm_packs_pu16(odd1_pi16, odd2_pi16);
#else
		u_odd_pu8 =  _mm_packs_pi16(odd1_pi16, odd2_pi16);
#endif

		/***** Reconstruct eight v chroma pixels for the even and odd rows *****/

		// Get the first four lowpass and four highpass coefficients
		low1_pi16 = *(v_low_ptr++);
		high1_pi16 = *(v_high_ptr++);

		// Reconstruct the v chroma pixels in the even row
		even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
		even1_pi16 = _mm_srai_pi16(even1_pi16, 1+shift);

		// Reconstruct the v chroma pixels in the odd row
		odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
		odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1+shift);

		// Get the second eight lowpass and four highpass coefficients
		low2_pi16 = *(v_low_ptr++);
		high2_pi16 = *(v_high_ptr++);

		// Reconstruct the v chroma pixels in the even row
		even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
		even2_pi16 = _mm_srai_pi16(even2_pi16, 1+shift);

		// Reconstruct the v chroma pixels in the odd row
		odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
		odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1+shift);

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset to the even row
		even1_pi16 = _mm_adds_pi16(even1_pi16, offset_pi16);
		even2_pi16 = _mm_adds_pi16(even2_pi16, offset_pi16);

		// Add the chroma offset to the odd row
		odd1_pi16 = _mm_adds_pi16(odd1_pi16, offset_pi16);
		odd2_pi16 = _mm_adds_pi16(odd2_pi16, offset_pi16);
#endif

		// Pack eight bytes of v chroma in the even row
#if _PACKS_PU16
		v_even_pu8 = _mm_packs_pu16(even1_pi16, even2_pi16);
#else
		v_even_pu8 = _mm_packs_pi16(even1_pi16, even2_pi16);
#endif
		// Pack eight bytes of v chroma in the odd row
#if _PACKS_PU16
		v_odd_pu8 =  _mm_packs_pu16(odd1_pi16, odd2_pi16);
#else
		v_odd_pu8 =  _mm_packs_pi16(odd1_pi16, odd2_pi16);
#endif

		/***** Interleave the first eight bytes of luma with alternating chroma *****/

		// Interleave the first four chroma for the even and odd rows
		uvuv_even_pu8 = _mm_unpacklo_pi8(v_even_pu8, u_even_pu8);
		uvuv_odd_pu8 = _mm_unpacklo_pi8(v_odd_pu8, u_odd_pu8);

		// Pack and store the first four luma and chroma pairs for the even row
		yuyv_even_pu8 = _mm_unpacklo_pi8(y_even_pu8, uvuv_even_pu8);
		//_mm_store_si128(even_ptr++, yuyv_even_pu8);
		*(even_ptr++) = yuyv_even_pu8;

		// Pack and store the first four luma and chroma pairs for the odd row
		yuyv_odd_pu8 = _mm_unpacklo_pi8(y_odd_pu8, uvuv_odd_pu8);
		//_mm_store_si128(odd_ptr++, yuyv_odd_pu8);
		*(odd_ptr++) = yuyv_odd_pu8;

		// Pack and store the second four luma and chroma pairs for the even row
		yuyv_even_pu8 = _mm_unpackhi_pi8(y_even_pu8, uvuv_even_pu8);
		//_mm_store_si128(even_ptr++, yuyv_even_pu8);
		*(even_ptr++) = yuyv_even_pu8;

		// Pack and store the second four luma and chroma pairs for the odd row
		yuyv_odd_pu8 = _mm_unpackhi_pi8(y_odd_pu8, uvuv_odd_pu8);
		//_mm_store_si128(odd_ptr++, yuyv_odd_pu8);
		*(odd_ptr++) = yuyv_odd_pu8;


		/***** Reconstruct the second eight luma pixels for the even and odd rows *****/

		// Get the first four lowpass and four highpass coefficients
		low1_pi16 = *(y_low_ptr++);
		high1_pi16 = *(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
		even1_pi16 = _mm_srai_pi16(even1_pi16, 1+shift);

		// Reconstruct the luma pixels in the odd row
		odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
		odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1+shift);

		// Get the second four lowpass and four highpass coefficients
		low2_pi16 = *(y_low_ptr++);
		high2_pi16 = *(y_high_ptr++);

		// Reconstruct the luma pixels in the even row
		even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
		even2_pi16 = _mm_srai_pi16(even2_pi16, 1+shift);

		// Reconstruct the luma pixels in the odd row
		odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
		odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1+shift);

		// Pack eight bytes of luma in the even row
#if _PACKS_PU16
		y_even_pu8 = _mm_packs_pu16(even1_pi16, even2_pi16);
#else
		y_even_pu8 = _mm_packs_pi16(even1_pi16, even2_pi16);
#endif
		// Pack eight bytes of luma in the odd row
#if _PACKS_PU16
		y_odd_pu8 = _mm_packs_pu16(odd1_pi16, odd2_pi16);
#else
		y_odd_pu8 = _mm_packs_pi16(odd1_pi16, odd2_pi16);
#endif

		/***** Interleave the second eight bytes of luma with alternating chroma *****/

		// Interleave the second eight chroma for the even and odd rows
		uvuv_even_pu8 = _mm_unpackhi_pi8(v_even_pu8, u_even_pu8);
		uvuv_odd_pu8 = _mm_unpackhi_pi8(v_odd_pu8, u_odd_pu8);

		// Pack and store the first four luma and chroma pairs for the even row
		yuyv_even_pu8 = _mm_unpacklo_pi8(y_even_pu8, uvuv_even_pu8);
		//_mm_store_si128(even_ptr++, yuyv_even_pu8);
		*(even_ptr++) = yuyv_even_pu8;

		// Pack and store the first four luma and chroma pairs for the odd row
		yuyv_odd_pu8 = _mm_unpacklo_pi8(y_odd_pu8, uvuv_odd_pu8);
		//_mm_store_si128(odd_ptr++, yuyv_odd_pu8);
		*(odd_ptr++) = yuyv_odd_pu8;

		// Pack and store the second four luma and chroma pairs for the even row
		yuyv_even_pu8 = _mm_unpackhi_pi8(y_even_pu8, uvuv_even_pu8);
		*(even_ptr++) = yuyv_even_pu8;

		// Pack and store the second four luma and chroma pairs for the odd row
		yuyv_odd_pu8 = _mm_unpackhi_pi8(y_odd_pu8, uvuv_odd_pu8);
		*(odd_ptr++) = yuyv_odd_pu8;

		// Done interleaving 32 bytes of luma with 16 bytes of each chroma channel
		// to reconstruct the 16 quad tuples of YUYV for the even and odd output rows
	}

	// The loop should have terminated at the post processing column
	assert(column == post_column);

#endif

	// Process the remaining portion of the row
	for (; column < output_width; column += 2)
	{
		int chroma_column = column / 2;
		int output_column = column * 2;
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;
		int low, high;
		int even, odd;

		// Get the lowpass and highpass coefficients for the first luma value
		low = lowpass[0][column];
		high = highpass[0][column];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)>>(1+shift);
		even = (low - high)>>(1+shift);
		odd_field[k0] = SATURATE_8U(odd);
		even_field[k0] = SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the first chroma value
		// Note that the v chroma value is the second byte in the four tuple
		low = lowpass[2][chroma_column];
		high = highpass[2][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)>>(1+shift);
		even = (low - high)>>(1+shift);

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset
		odd += chroma_offset;
		even += chroma_offset;
#endif
		// Store the first chroma value in the even and odd rows
		odd_field[k1] = SATURATE_8U(odd);
		even_field[k1] = SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the second luma value
		low = lowpass[0][column + 1];
		high = highpass[0][column + 1];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)>>(1+shift);
		even = (low - high)>>(1+shift);
		odd_field[k2] = SATURATE_8U(odd);
		even_field[k2] = SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the second chroma value
		// Note that the u chroma value is the fourth byte in the four tuple
		low = lowpass[1][chroma_column];
		high = highpass[1][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)>>(1+shift);
		even = (low - high)>>(1+shift);

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset
		odd += chroma_offset;
		even += chroma_offset;
#endif
		// Store the first chroma value in the even and odd rows
		odd_field[k3] = SATURATE_8U(odd);
		even_field[k3] = SATURATE_8U(even);
	}

#if 0
	// Fill the rest of the row with background values
	for (; column < frame_width; column += 2)
	{
		int output_column = column * 2;
#if 1
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;

		even_field[k0] = COLOR_LUMA_BLACK;
		odd_field[k0] = COLOR_LUMA_BLACK;

		even_field[k1] = COLOR_CHROMA_ZERO;
		odd_field[k1] = COLOR_CHROMA_ZERO;

		even_field[k2] = COLOR_LUMA_BLACK;
		odd_field[k2] = COLOR_LUMA_BLACK;

		even_field[k3] = COLOR_CHROMA_ZERO;
		odd_field[k3] = COLOR_CHROMA_ZERO;
#else
		int i;

		for (i = 0; i < 4; i++) {
			even_field[output_column + i] = 0;
			odd_field[output_column + i] = 0;
		}
#endif
	}
#endif

	// Convert the even and odd rows of YUV to the specified color
	STOP(tk_inverse);
	ConvertRowYUYV(even_field, even_output, frame_width, format, colorspace, 8);
	ConvertRowYUYV(odd_field, odd_output, frame_width, format, colorspace, 8);
	START(tk_inverse);

	//_mm_empty();	// Clear the mmx register state
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Invert the temporal bands from all channels and pack the output pixels
void InvertInterlacedRow16s(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
							uint8_t *output, int pitch, int output_width, int frame_width,
							char *buffer, size_t buffer_size,
							int format, int colorspace, int chroma_offset, int precision, int row)
{
	size_t row_size = abs(pitch);
	int shift = precision - 8;

	uint8_t *even_field = (uint8_t *)buffer;
	uint8_t *odd_field = even_field + row_size * (precision == 8 ? 1 : 2);

	uint8_t *even_output = output;
	uint8_t *odd_output = even_output + pitch;

	int column;

#if (1 && XMMOPT)
	int chroma_width = output_width/2;
	int chroma_step = 8;
	int post_column = 2 * (chroma_width - (chroma_width % chroma_step));
	int column_step = 2 * chroma_step;

	// Initialize pointers to the rows or lowpass and highpass coefficients
	__m128i *y_low_ptr = (__m128i *)lowpass[0];
	__m128i *u_low_ptr = (__m128i *)lowpass[1];
	__m128i *v_low_ptr = (__m128i *)lowpass[2];
	__m128i *y_high_ptr = (__m128i *)highpass[0];
	__m128i *u_high_ptr = (__m128i *)highpass[1];
	__m128i *v_high_ptr = (__m128i *)highpass[2];

	// Initialize the pointers to the rows in the even and odd fields
	__m128i *even_ptr = (__m128i *)even_field;
	__m128i *odd_ptr = (__m128i *)odd_field;
#endif

	// This code assumes that there are three channels
	assert(num_channels == 3);

	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(y_low_ptr));
	assert(ISALIGNED16(u_low_ptr));
	assert(ISALIGNED16(v_low_ptr));
	assert(ISALIGNED16(y_high_ptr));
	assert(ISALIGNED16(u_high_ptr));
	assert(ISALIGNED16(v_high_ptr));

	// Check that the output pointers are properly aligned
	//assert(ISALIGNED16(even_ptr));
	//assert(ISALIGNED16(odd_ptr));

	// Must have enough space for two rows of pixels (four bytes per RGBA pixel)
	//assert(buffer_size >= 4 * frame_width);
	assert(buffer_size >= 4 * row_size);

	// Start at the beginning of the row
	column = 0;

#if (1 && XMMOPT)

	// Shorter loop that processes 16 output pixels per iteration
	assert(column_step == 16);

	if(precision == 8)
	{
		// Process groups of pixels in the fast loop
		for (; column < post_column; column += column_step)
		{
			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;
			__m128i odd1_epi16;
			__m128i odd2_epi16;
			__m128i even1_epi16;
			__m128i even2_epi16;

			__m128i y_even_epu8;
			__m128i y_odd_epu8;
			__m128i u_even_epi16;
			__m128i v_even_epi16;
			__m128i u_odd_epi16;
			__m128i v_odd_epi16;

			__m128i uvuv1_epi16;
			__m128i uvuv2_epi16;

			__m128i uvuv_even_epu8;
			__m128i yuyv_even_epu8;
			__m128i uvuv_odd_epu8;
			__m128i yuyv_odd_epu8;


			/***** Reconstruct sixteen luma pixels for the even and odd rows *****/

			// Get the first eight lowpass and eight highpass coefficients
			low1_epi16 = _mm_load_si128(y_low_ptr++);
			high1_epi16 = _mm_load_si128(y_high_ptr++);

			// Reconstruct the luma pixels in the even row
			even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			even1_epi16 = _mm_srai_epi16(even1_epi16, 1+shift);

			// Reconstruct the luma pixels in the odd row
			odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1+shift);

			// Get the second eight lowpass and eight highpass coefficients
			low2_epi16 = _mm_load_si128(y_low_ptr++);
			high2_epi16 = _mm_load_si128(y_high_ptr++);

			// Reconstruct the luma pixels in the even row
			even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
			even2_epi16 = _mm_srai_epi16(even2_epi16, 1+shift);

			// Reconstruct the luma pixels in the odd row
			odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
			odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1+shift);

			// Pack sixteen bytes of luma in the even row
			y_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

	#if STRICT_SATURATE
			// Perform strict saturation on YUV if required
			y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(16));
			y_even_epu8 = _mm_adds_epu8(y_even_epu8, _mm_set1_epi8(36));	// 36 = 16 + 20 = 16 + (255-235)
			y_even_epu8 = _mm_subs_epu8(y_even_epu8, _mm_set1_epi8(20));
	#endif

			// Pack sixteen bytes of luma in the odd row
			y_odd_epu8 = _mm_packus_epi16(odd1_epi16, odd2_epi16);

	#if STRICT_SATURATE
			// Perform strict saturation on YUV if required
			y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(16));
			y_odd_epu8 = _mm_adds_epu8(y_odd_epu8, _mm_set1_epi8(36));		// 36 = 16 + 20 = 16 + (255-235)
			y_odd_epu8 = _mm_subs_epu8(y_odd_epu8, _mm_set1_epi8(20));
	#endif

			/***** Reconstruct eight u chroma pixels for the even and odd rows *****/

			// Load eight lowpass and eight highpass coefficients
			low1_epi16 = _mm_load_si128(u_low_ptr++);
			high1_epi16 = _mm_load_si128(u_high_ptr++);

			// Reconstruct the u chroma pixels in the even row
			u_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			u_even_epi16 = _mm_srai_epi16(u_even_epi16, 1+shift);

			// Reconstruct the u chroma pixels in the odd row
			u_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			u_odd_epi16 = _mm_srai_epi16(u_odd_epi16, 1+shift);

	#if _ENCODE_CHROMA_OFFSET
			// Add the chroma offset to the even and odd rows
			u_even_epi16 = _mm_adds_epi16(u_even_epi16, offset_epi16);
			u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, offset_epi16);
	#endif

			/***** Reconstruct eight v chroma pixels for the even and odd rows *****/

			// Load eight lowpass and eight highpass coefficients
			low1_epi16 = _mm_load_si128(v_low_ptr++);
			high1_epi16 = _mm_load_si128(v_high_ptr++);

			// Reconstruct the v chroma pixels in the even row
			v_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			v_even_epi16 = _mm_srai_epi16(v_even_epi16, 1+shift);

			// Reconstruct the v chroma pixels in the odd row
			v_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			v_odd_epi16 = _mm_srai_epi16(v_odd_epi16, 1+shift);

	#if _ENCODE_CHROMA_OFFSET
			// Add the chroma offset to the even and odd rows
			v_even_epi16 = _mm_adds_epi16(v_even_epi16, offset_epi16);
			v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, offset_epi16);
	#endif

			/***** Interleave the sixteen bytes of luma with alternating chroma *****/

			// Interleave the first four chroma for the even row
			uvuv1_epi16 = _mm_unpacklo_epi16(v_even_epi16, u_even_epi16);

			// Interleave the second four chroma for the even row
			uvuv2_epi16 = _mm_unpackhi_epi16(v_even_epi16, u_even_epi16);

			// Pack sixteen alternating chroma for the even row
			uvuv_even_epu8 = _mm_packus_epi16(uvuv1_epi16, uvuv2_epi16);

			// Interleave the first four chroma for the odd row
			uvuv1_epi16 = _mm_unpacklo_epi16(v_odd_epi16, u_odd_epi16);

			// Interleave the second four chroma for the odd row
			uvuv2_epi16 = _mm_unpackhi_epi16(v_odd_epi16, u_odd_epi16);

			// Pack sixteen alternating chroma for the odd row
			uvuv_odd_epu8 = _mm_packus_epi16(uvuv1_epi16, uvuv2_epi16);

			// Interleave and store the first eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpacklo_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Interleave and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpacklo_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);

			// Interleave and store the second eight luma and chroma pairs for the even row
			yuyv_even_epu8 = _mm_unpackhi_epi8(y_even_epu8, uvuv_even_epu8);
			_mm_store_si128(even_ptr++, yuyv_even_epu8);

			// Interleave and store the second eight luma and chroma pairs for the odd row
			yuyv_odd_epu8 = _mm_unpackhi_epi8(y_odd_epu8, uvuv_odd_epu8);
			_mm_store_si128(odd_ptr++, yuyv_odd_epu8);
		}
	}
	else
	{
		// Process groups of pixels in the fast loop
		for (; column < post_column; column += column_step)
		{
			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;
			__m128i odd1_epi16;
			__m128i odd2_epi16;
			__m128i even1_epi16;
			__m128i even2_epi16;

			//__m128i y_even_epu8;
			//__m128i y_odd_epu8;
			__m128i u_even_epi16;
			__m128i v_even_epi16;
			__m128i u_odd_epi16;
			__m128i v_odd_epi16;

			//__m128i uvuv1_epi16;
			//__m128i uvuv2_epi16;
			//__m128i uvuv1_even_epi16;
			//__m128i uvuv2_even_epi16;
			//__m128i uvuv1_odd_epi16;
			//__m128i uvuv2_odd_epi16;

			//__m128i uvuv_even_epu8;
			//__m128i yuyv_even_epu8;
			//__m128i uvuv_odd_epu8;
			//__m128i yuyv_odd_epu8;

			//__m128i yuyv_even_epi16;
			//__m128i yuyv_odd_epi16;


			/***** Reconstruct sixteen luma pixels for the even and odd rows *****/

			// Get the first eight lowpass and eight highpass coefficients
			low1_epi16 = _mm_load_si128(y_low_ptr++);
			high1_epi16 = _mm_load_si128(y_high_ptr++);

			// Reconstruct the luma pixels in the even row
			even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			even1_epi16 = _mm_srai_epi16(even1_epi16, 1);
			_mm_store_si128(even_ptr++, even1_epi16);

			// Reconstruct the luma pixels in the odd row
			odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);
			_mm_store_si128(odd_ptr++, odd1_epi16);

			// Get the second eight lowpass and eight highpass coefficients
			low2_epi16 = _mm_load_si128(y_low_ptr++);
			high2_epi16 = _mm_load_si128(y_high_ptr++);

			// Reconstruct the luma pixels in the even row
			even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
			even2_epi16 = _mm_srai_epi16(even2_epi16, 1);
			_mm_store_si128(even_ptr++, even2_epi16);

			// Reconstruct the luma pixels in the odd row
			odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
			odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);
			_mm_store_si128(odd_ptr++, odd2_epi16);




			/***** Reconstruct eight u chroma pixels for the even and odd rows *****/

			// Load eight lowpass and eight highpass coefficients
			low1_epi16 = _mm_load_si128(u_low_ptr++);
			high1_epi16 = _mm_load_si128(u_high_ptr++);

			// Reconstruct the u chroma pixels in the even row
			u_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			u_even_epi16 = _mm_srai_epi16(u_even_epi16, 1);
			_mm_store_si128(even_ptr++, u_even_epi16);

			// Reconstruct the u chroma pixels in the odd row
			u_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			u_odd_epi16 = _mm_srai_epi16(u_odd_epi16, 1);
			_mm_store_si128(odd_ptr++, u_odd_epi16);

			/***** Reconstruct eight v chroma pixels for the even and odd rows *****/

			// Load eight lowpass and eight highpass coefficients
			low1_epi16 = _mm_load_si128(v_low_ptr++);
			high1_epi16 = _mm_load_si128(v_high_ptr++);

			// Reconstruct the v chroma pixels in the even row
			v_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			v_even_epi16 = _mm_srai_epi16(v_even_epi16, 1);
			_mm_store_si128(even_ptr++, v_even_epi16);

			// Reconstruct the v chroma pixels in the odd row
			v_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			v_odd_epi16 = _mm_srai_epi16(v_odd_epi16, 1);
			_mm_store_si128(odd_ptr++, v_odd_epi16);


			/***** Interleave the sixteen bytes of luma with alternating chroma *****/

	/*		// Interleave the first four chroma for the even row
			uvuv1_even_epi16 = _mm_unpacklo_epi16(v_even_epi16, u_even_epi16);

			// Interleave the second four chroma for the even row
			uvuv2_even_epi16 = _mm_unpackhi_epi16(v_even_epi16, u_even_epi16);

			// Pack sixteen alternating chroma for the even row
		//	uvuv_even_epu8 = _mm_packus_epi16(uvuv1_epi16, uvuv2_epi16);

			// Interleave the first four chroma for the odd row
			uvuv1_odd_epi16 = _mm_unpacklo_epi16(v_odd_epi16, u_odd_epi16);

			// Interleave the second four chroma for the odd row
			uvuv2_odd_epi16 = _mm_unpackhi_epi16(v_odd_epi16, u_odd_epi16);

			// Pack sixteen alternating chroma for the odd row
			yuyv_even_epi16 = _mm_unpacklo_epi16(even1_epi16, uvuv1_even_epi16);
			_mm_store_si128(even_ptr++, yuyv_even_epi16);
			yuyv_even_epi16 = _mm_unpackhi_epi16(even1_epi16, uvuv1_even_epi16);
			_mm_store_si128(even_ptr++, yuyv_even_epi16);

			// Interleave and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epi16 = _mm_unpacklo_epi16(odd1_epi16, uvuv1_odd_epi16);
			_mm_store_si128(odd_ptr++, yuyv_odd_epi16);
			yuyv_odd_epi16 = _mm_unpackhi_epi16(odd1_epi16, uvuv1_odd_epi16);
			_mm_store_si128(odd_ptr++, yuyv_odd_epi16);

			// Interleave and store the second eight luma and chroma pairs for the even row
			yuyv_even_epi16 = _mm_unpacklo_epi16(even2_epi16, uvuv2_even_epi16);
			_mm_store_si128(even_ptr++, yuyv_even_epi16);
			yuyv_even_epi16 = _mm_unpackhi_epi16(even2_epi16, uvuv2_even_epi16);
			_mm_store_si128(even_ptr++, yuyv_even_epi16);

			// Interleave and store the first eight luma and chroma pairs for the odd row
			yuyv_odd_epi16 = _mm_unpacklo_epi16(odd2_epi16, uvuv2_odd_epi16);
			_mm_store_si128(odd_ptr++, yuyv_odd_epi16);
			yuyv_odd_epi16 = _mm_unpackhi_epi16(odd2_epi16, uvuv2_odd_epi16);
			_mm_store_si128(odd_ptr++, yuyv_odd_epi16);
*/


		}
	}


	// The loop should have terminated at the post processing column
	assert(column == post_column);

#endif

#if 1
	{
		int offset = 0;

		// Process the remaining portion of the row
		for (; column < output_width; column += 2)
		{
			int chroma_column = column / 2;
			int output_column = column * 2;
			int k0 = output_column;
			int k1 = output_column + 1;
			int k2 = output_column + 2;
			int k3 = output_column + 3;
			int low, high;
			int even, odd;

			// Get the lowpass and highpass coefficients for the first luma value
			low = lowpass[0][column];
			high = highpass[0][column];

			// Reconstruct the luma in the even and odd rows
			odd = (low + high + offset)>>(1+shift);
			even = (low - high + offset)>>(1+shift);

	#if (0 && DEBUG)
			// Suppress the luminance for debugging
			odd = 0;
			even = 0;
	#endif
			// Store the luma values for the even and odd rows
			if(odd < 16) odd = 16; else if(odd>235) odd = 235;
			if(even < 16) even = 16; else if(even>235) even = 235;
			odd_field[k0] = odd;//SATURATE_8U(odd);
			even_field[k0] = even;//SATURATE_8U(even);

			// Get the lowpass and highpass coefficients for the first chroma value
			// Note that the v chroma value is the second byte in the four tuple
			low = lowpass[2][chroma_column];
			high = highpass[2][chroma_column];

			// Reconstruct the chroma in the even and odd rows
			odd = (low + high + offset)>>(1+shift);
			even = (low - high + offset)>>(1+shift);

	#if _ENCODE_CHROMA_OFFSET
			// Add the chroma offset the values for the even and odd rows
			odd += chroma_offset;
			even += chroma_offset;
	#endif

	#if (0 && DEBUG)
			// Suppress the chrominance for debugging
			odd = 128;
			even = 128;
	#endif
			// Store the chroma values for the even and odd rows
			if(odd < 16) odd = 16; else if(odd>240) odd = 240;
			if(even < 16) even = 16; else if(even>240) even = 240;
			odd_field[k1] = odd;//SATURATE_8U(odd);
			even_field[k1] = even;//SATURATE_8U(even);

			// Get the lowpass and highpass coefficients for the second luma value
			low = lowpass[0][column + 1];
			high = highpass[0][column + 1];

			// Reconstruct the luma in the even and odd rows
			odd = (low + high + offset)>>(1+shift);
			even = (low - high + offset)>>(1+shift);

	#if (0 && DEBUG)
			// Suppress the luminance for debugging
			odd = 0;
			even = 0;
	#endif
			// Store the luma values for the even and odd rows
			if(odd < 16) odd = 16; else if(odd>235) odd = 235;
			if(even < 16) even = 16; else if(even>235) even = 235;
			odd_field[k2] = odd;//SATURATE_8U(odd);
			even_field[k2] = even;//SATURATE_8U(even);

			// Get the lowpass and highpass coefficients for the second chroma value
			// Note that the u chroma value is the fourth byte in the four tuple
			low = lowpass[1][chroma_column];
			high = highpass[1][chroma_column];

			// Reconstruct the chroma in the even and odd rows
			odd = (low + high + offset)>>(1+shift);
			even = (low - high + offset)>>(1+shift);

	#if _ENCODE_CHROMA_OFFSET
			// Add the chroma offset the values for the even and odd rows
			odd += chroma_offset;
			even += chroma_offset;
	#endif

	#if (0 && DEBUG)
			// Suppress the chrominance for debugging
			odd = 128;
			even = 128;
	#endif
			// Store the chroma values for the even and odd rows
			if(odd < 16) odd = 16; else if(odd>240) odd = 240;
			if(even < 16) even = 16; else if(even>240) even = 240;
			odd_field[k3] = odd;//SATURATE_8U(odd);
			even_field[k3] = even;//SATURATE_8U(even);
		}
	}

#else

	// Fill the remaining portion of the row with a debugging value
	for (; column < output_width; column += 2)
	{
		int chroma_column = column / 2;
		int output_column = column * 2;
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;
		int low, high;
		int even, odd;
		const int DEBUG_COLOR_LUMA  = 0;
		const int DEBUG_COLOR_CHROMA = 128;

		// Get the lowpass and highpass coefficients for the first luma value
		low = lowpass[0][column];
		high = highpass[0][column];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)>>(1+shift);
		even = (low - high)>>(1+shift);
		odd_field[k0] = DEBUG_COLOR_LUMA;		//SATURATE_8U(odd);
		even_field[k0] = DEBUG_COLOR_LUMA;		//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the first chroma value
		// Note that the v chroma value is the second byte in the four tuple
		low = lowpass[2][chroma_column];
		high = highpass[2][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)>>(1+shift);
		even = (low - high)>>(1+shift);
		odd_field[k1] = DEBUG_COLOR_CHROMA;		//SATURATE_8U(odd);
		even_field[k1] = DEBUG_COLOR_CHROMA;	//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the second luma value
		low = lowpass[0][column + 1];
		high = highpass[0][column + 1];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)>>(1+shift);
		even = (low - high)>>(1+shift);
		odd_field[k2] = DEBUG_COLOR_LUMA;		//SATURATE_8U(odd);
		even_field[k2] = DEBUG_COLOR_LUMA;		//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the second chroma value
		// Note that the u chroma value is the fourth byte in the four tuple
		low = lowpass[1][chroma_column];
		high = highpass[1][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)>>(1+shift);
		even = (low - high)>>(1+shift);
		odd_field[k3] = DEBUG_COLOR_CHROMA;		//SATURATE_8U(odd);
		even_field[k3] = DEBUG_COLOR_CHROMA;	//SATURATE_8U(even);
	}
#endif

#if 0
	// Fill the rest of the row with background values
	for (; column < frame_width; column += 2)
	{
		int output_column = column * 2;
#if 1
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;

		even_field[k0] = COLOR_LUMA_BLACK;
		odd_field[k0] = COLOR_LUMA_BLACK;

		even_field[k1] = COLOR_CHROMA_ZERO;
		odd_field[k1] = COLOR_CHROMA_ZERO;

		even_field[k2] = COLOR_LUMA_BLACK;
		odd_field[k2] = COLOR_LUMA_BLACK;

		even_field[k3] = COLOR_CHROMA_ZERO;
		odd_field[k3] = COLOR_CHROMA_ZERO;
#else
		int i;

		for (i = 0; i < 4; i++) {
			even_field[output_column + i] = 0;
			odd_field[output_column + i] = 0;
		}
#endif
	}
#endif

	// Convert the even and odd rows of YUV to the specified color
	STOP(tk_inverse);
	ConvertRowYUYV(even_field, even_output, frame_width, format, colorspace, precision);
	ConvertRowYUYV(odd_field, odd_output, frame_width, format, colorspace, precision);
	START(tk_inverse);
}

#endif


// This routine is SSE2 only
// Invert the temporal bands from all channels and pack the output pixels
void InvertInterlacedRow16sToV210(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
								  uint8_t *output, int pitch, int output_width, int frame_width,
								  char *buffer, size_t buffer_size, int format, int chroma_offset, int precision)
{
	int row_size = abs(pitch) * sizeof(PIXEL);
	int shift = 10 - precision;

	// Allocate field buffers large enough for the end of row processing required for v210
	PIXEL *even_field = (PIXEL *)buffer;
	PIXEL *odd_field = (PIXEL *)(even_field + row_size);

	uint8_t *even_output = output;
	uint8_t *odd_output = output + pitch;

	int column;

#if (1 && XMMOPT)
	int chroma_width = output_width/2;
	int chroma_step = 8;
	int post_column = 2 * (chroma_width - (chroma_width % chroma_step));
	int column_step = 2 * chroma_step;

	// Initialize pointers to the rows or lowpass and highpass coefficients
	__m128i *y_low_ptr = (__m128i *)lowpass[0];
	__m128i *u_low_ptr = (__m128i *)lowpass[1];
	__m128i *v_low_ptr = (__m128i *)lowpass[2];
	__m128i *y_high_ptr = (__m128i *)highpass[0];
	__m128i *u_high_ptr = (__m128i *)highpass[1];
	__m128i *v_high_ptr = (__m128i *)highpass[2];

	// Initialize the pointers to the rows in the even and odd fields
	__m128i *even_ptr = (__m128i *)even_field;
	__m128i *odd_ptr = (__m128i *)odd_field;

	__m128i overflowprotect_epi16 = _mm_set1_epi16(0x7fff - 2047);
#endif

	// This code assumes that there are three channels
	assert(num_channels == 3);

	// Check that the pointers to the groups of pixels are properly aligned
	assert(ISALIGNED16(y_low_ptr));
	assert(ISALIGNED16(u_low_ptr));
	assert(ISALIGNED16(v_low_ptr));
	assert(ISALIGNED16(y_high_ptr));
	assert(ISALIGNED16(u_high_ptr));
	assert(ISALIGNED16(v_high_ptr));

	// Check that the output pointers are properly aligned
	//assert(ISALIGNED16(even_ptr));
	//assert(ISALIGNED16(odd_ptr));

	// Must have enough space for two rows of pixels (four bytes per RGBA pixel)
	//assert(buffer_size >= 4 * frame_width);
//	assert(buffer_size >= 2 * row_size);

	// Start at the beginning of the row
	column = 0;

#if (1 && XMMOPT)

	// Shorter loop that processes 16 output pixels per iteration
	assert(column_step == 16);

	if(shift == 0)
	{
		// Process groups of pixels in the fast loop
		for (; column < post_column; column += column_step)
		{
			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;

			__m128i y1_even_epi16;
			__m128i y2_even_epi16;
			__m128i y1_odd_epi16;
			__m128i y2_odd_epi16;

			__m128i u_even_epi16;
			__m128i v_even_epi16;
			__m128i u_odd_epi16;
			__m128i v_odd_epi16;

			__m128i uv1_even_epi16;
			__m128i uv2_even_epi16;
			__m128i uv1_odd_epi16;
			__m128i uv2_odd_epi16;

			__m128i yuv1_even_epi16;
			__m128i yuv2_even_epi16;
			__m128i yuv3_even_epi16;
			__m128i yuv4_even_epi16;
			__m128i yuv1_odd_epi16;
			__m128i yuv2_odd_epi16;
			__m128i yuv3_odd_epi16;
			__m128i yuv4_odd_epi16;

			__m128i mask_epi16;
			__m128i limit_epi16 = _mm_set1_epi16(V210_VALUE_MASK);


			/***** Reconstruct sixteen luma pixels for the even and odd rows *****/

			// Get the first eight lowpass and eight highpass coefficients
			low1_epi16 = _mm_load_si128(y_low_ptr++);
			high1_epi16 = _mm_load_si128(y_high_ptr++);

			// Reconstruct the luma pixels in the even row
		//	low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
			y1_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			y1_even_epi16 = _mm_adds_epi16(y1_even_epi16, overflowprotect_epi16);
			y1_even_epi16 = _mm_subs_epu16(y1_even_epi16, overflowprotect_epi16);
			y1_even_epi16 = _mm_srai_epi16(y1_even_epi16, 1);

			// Reconstruct the luma pixels in the odd row
			y1_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			y1_odd_epi16 = _mm_adds_epi16(y1_odd_epi16, overflowprotect_epi16);
			y1_odd_epi16 = _mm_subs_epu16(y1_odd_epi16, overflowprotect_epi16);
			y1_odd_epi16 = _mm_srai_epi16(y1_odd_epi16, 1);

			// Get the second eight lowpass and eight highpass coefficients
			low2_epi16 = _mm_load_si128(y_low_ptr++);
			high2_epi16 = _mm_load_si128(y_high_ptr++);

			// Reconstruct the luma pixels in the even row
		//	low2_epi16 = _mm_adds_epi16(low2_epi16, overflowprotect_epi16);
			y2_even_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
			y2_even_epi16 = _mm_adds_epi16(y2_even_epi16, overflowprotect_epi16);
			y2_even_epi16 = _mm_subs_epu16(y2_even_epi16, overflowprotect_epi16);
			y2_even_epi16 = _mm_srai_epi16(y2_even_epi16, 1);

			// Reconstruct the luma pixels in the odd row
			y2_odd_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
			y2_odd_epi16 = _mm_adds_epi16(y2_odd_epi16, overflowprotect_epi16);
			y2_odd_epi16 = _mm_subs_epu16(y2_odd_epi16, overflowprotect_epi16);
			y2_odd_epi16 = _mm_srai_epi16(y2_odd_epi16, 1);

			// Pack sixteen bytes of luma in the even row
			//y_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

			// Pack sixteen bytes of luma in the odd row
			//y_odd_epu8 = _mm_packus_epi16(odd1_epi16, odd2_epi16);


			/***** Reconstruct eight u chroma pixels for the even and odd rows *****/

			// Load eight lowpass and eight highpass coefficients
			low1_epi16 = _mm_load_si128(u_low_ptr++);
			high1_epi16 = _mm_load_si128(u_high_ptr++);

			// Reconstruct the u chroma pixels in the even row
	//		low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
			u_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			u_even_epi16 = _mm_adds_epi16(u_even_epi16, overflowprotect_epi16);
			u_even_epi16 = _mm_subs_epu16(u_even_epi16, overflowprotect_epi16);
			u_even_epi16 = _mm_srai_epi16(u_even_epi16, 1);

			// Reconstruct the u chroma pixels in the odd row
			u_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, overflowprotect_epi16);
			u_odd_epi16 = _mm_subs_epu16(u_odd_epi16, overflowprotect_epi16);
			u_odd_epi16 = _mm_srai_epi16(u_odd_epi16, 1);

	#if _ENCODE_CHROMA_OFFSET
			// Add the chroma offset to the even and odd rows
			u_even_epi16 = _mm_adds_epi16(u_even_epi16, offset_epi16);
			u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, offset_epi16);
	#endif

			/***** Reconstruct eight v chroma pixels for the even and odd rows *****/

			// Load eight lowpass and eight highpass coefficients
			low1_epi16 = _mm_load_si128(v_low_ptr++);
			high1_epi16 = _mm_load_si128(v_high_ptr++);

			// Reconstruct the v chroma pixels in the even row
		//	low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
			v_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			v_even_epi16 = _mm_adds_epi16(v_even_epi16, overflowprotect_epi16);
			v_even_epi16 = _mm_subs_epu16(v_even_epi16, overflowprotect_epi16);
			v_even_epi16 = _mm_srai_epi16(v_even_epi16, 1);

			// Reconstruct the v chroma pixels in the odd row
			v_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, overflowprotect_epi16);
			v_odd_epi16 = _mm_subs_epu16(v_odd_epi16, overflowprotect_epi16);
			v_odd_epi16 = _mm_srai_epi16(v_odd_epi16, 1);

	#if _ENCODE_CHROMA_OFFSET
			// Add the chroma offset to the even and odd rows
			v_even_epi16 = _mm_adds_epi16(v_even_epi16, offset_epi16);
			v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, offset_epi16);
	#endif

			/***** Interleave the sixteen luma values with alternating chroma *****/

			// Interleave the first four chroma for the even row
			uv1_even_epi16 = _mm_unpacklo_epi16(v_even_epi16, u_even_epi16);

			// Interleave the second four chroma for the even row
			uv2_even_epi16 = _mm_unpackhi_epi16(v_even_epi16, u_even_epi16);

			// Interleave the first four chroma for the odd row
			uv1_odd_epi16 = _mm_unpacklo_epi16(v_odd_epi16, u_odd_epi16);

			// Interleave the second four chroma for the odd row
			uv2_odd_epi16 = _mm_unpackhi_epi16(v_odd_epi16, u_odd_epi16);

			// Interleave the first four luma and chroma pairs for the even row
			yuv1_even_epi16 = _mm_unpacklo_epi16(y1_even_epi16, uv1_even_epi16);

			// Interleave the first four luma and chroma pairs for the odd row
			yuv1_odd_epi16 = _mm_unpacklo_epi16(y1_odd_epi16, uv1_odd_epi16);

			// Saturate the pixels in the even row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv1_even_epi16, limit_epi16);
			yuv1_even_epi16 = _mm_andnot_si128(mask_epi16, yuv1_even_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv1_even_epi16 = _mm_or_si128(yuv1_even_epi16, mask_epi16);

			// Saturate the pixels in the odd row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv1_odd_epi16, limit_epi16);
			yuv1_odd_epi16 = _mm_andnot_si128(mask_epi16, yuv1_odd_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv1_odd_epi16 = _mm_or_si128(yuv1_odd_epi16, mask_epi16);

			// Store the first four pairs of luma and chroma values for the even row
			_mm_store_si128(even_ptr++, yuv1_even_epi16);

			// Store the first four pairs of luma and chroma values for the odd row
			_mm_store_si128(odd_ptr++, yuv1_odd_epi16);

			// Interleave the second four luma and chroma pairs for the even row
			yuv2_even_epi16 = _mm_unpackhi_epi16(y1_even_epi16, uv1_even_epi16);

			// Interleave the second four luma and chroma pairs for the odd row
			yuv2_odd_epi16 = _mm_unpackhi_epi16(y1_odd_epi16, uv1_odd_epi16);

			// Saturate the pixels in the even row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv2_even_epi16, limit_epi16);
			yuv2_even_epi16 = _mm_andnot_si128(mask_epi16, yuv2_even_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv2_even_epi16 = _mm_or_si128(yuv2_even_epi16, mask_epi16);

			// Saturate the pixels in the odd row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv2_odd_epi16, limit_epi16);
			yuv2_odd_epi16 = _mm_andnot_si128(mask_epi16, yuv2_odd_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv2_odd_epi16 = _mm_or_si128(yuv2_odd_epi16, mask_epi16);

			// Store the second four pairs of luma and chroma values for the even row
			_mm_store_si128(even_ptr++, yuv2_even_epi16);

			// Store the second four pairs of luma and chroma values for the odd row
			_mm_store_si128(odd_ptr++, yuv2_odd_epi16);

			// Interleave the third four luma and chroma pairs for the even row
			yuv3_even_epi16 = _mm_unpacklo_epi16(y2_even_epi16, uv2_even_epi16);

			// Interleave the third four luma and chroma pairs for the odd row
			yuv3_odd_epi16 = _mm_unpacklo_epi16(y2_odd_epi16, uv2_odd_epi16);

			// Saturate the pixels in the even row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv3_even_epi16, limit_epi16);
			yuv3_even_epi16 = _mm_andnot_si128(mask_epi16, yuv3_even_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv3_even_epi16 = _mm_or_si128(yuv3_even_epi16, mask_epi16);

			// Saturate the pixels in the odd row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv3_odd_epi16, limit_epi16);
			yuv3_odd_epi16 = _mm_andnot_si128(mask_epi16, yuv3_odd_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv3_odd_epi16 = _mm_or_si128(yuv3_odd_epi16, mask_epi16);

			// Store the first four pairs of luma and chroma values for the even row
			_mm_store_si128(even_ptr++, yuv3_even_epi16);

			// Store the first four pairs of luma and chroma values for the odd row
			_mm_store_si128(odd_ptr++, yuv3_odd_epi16);

			// Interleave the fourth four luma and chroma pairs for the even row
			yuv4_even_epi16 = _mm_unpackhi_epi16(y2_even_epi16, uv2_even_epi16);

			// Interleave the fourth four luma and chroma pairs for the odd row
			yuv4_odd_epi16 = _mm_unpackhi_epi16(y2_odd_epi16, uv2_odd_epi16);

			// Saturate the pixels in the even row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv4_even_epi16, limit_epi16);
			yuv4_even_epi16 = _mm_andnot_si128(mask_epi16, yuv4_even_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv4_even_epi16 = _mm_or_si128(yuv4_even_epi16, mask_epi16);

			// Saturate the pixels in the odd row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv4_odd_epi16, limit_epi16);
			yuv4_odd_epi16 = _mm_andnot_si128(mask_epi16, yuv4_odd_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv4_odd_epi16 = _mm_or_si128(yuv4_odd_epi16, mask_epi16);

			// Store the second four pairs of luma and chroma values for the even row
			_mm_store_si128(even_ptr++, yuv4_even_epi16);

			// Store the second four pairs of luma and chroma values for the odd row
			_mm_store_si128(odd_ptr++, yuv4_odd_epi16);
		}
	}
	else
	{
		// Process groups of pixels in the fast loop
		for (; column < post_column; column += column_step)
		{
			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;

			__m128i y1_even_epi16;
			__m128i y2_even_epi16;
			__m128i y1_odd_epi16;
			__m128i y2_odd_epi16;

			__m128i u_even_epi16;
			__m128i v_even_epi16;
			__m128i u_odd_epi16;
			__m128i v_odd_epi16;

			__m128i uv1_even_epi16;
			__m128i uv2_even_epi16;
			__m128i uv1_odd_epi16;
			__m128i uv2_odd_epi16;

			__m128i yuv1_even_epi16;
			__m128i yuv2_even_epi16;
			__m128i yuv3_even_epi16;
			__m128i yuv4_even_epi16;
			__m128i yuv1_odd_epi16;
			__m128i yuv2_odd_epi16;
			__m128i yuv3_odd_epi16;
			__m128i yuv4_odd_epi16;

			__m128i mask_epi16;
			__m128i limit_epi16 = _mm_set1_epi16(V210_VALUE_MASK);


			/***** Reconstruct sixteen luma pixels for the even and odd rows *****/

			// Get the first eight lowpass and eight highpass coefficients
			low1_epi16 = _mm_load_si128(y_low_ptr++);
			low1_epi16 = _mm_slli_epi16(low1_epi16, shift);
			high1_epi16 = _mm_load_si128(y_high_ptr++);
			high1_epi16 = _mm_slli_epi16(high1_epi16, shift);

			// Reconstruct the luma pixels in the even row
		//	low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
			y1_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			y1_even_epi16 = _mm_adds_epi16(y1_even_epi16, overflowprotect_epi16);
			y1_even_epi16 = _mm_subs_epu16(y1_even_epi16, overflowprotect_epi16);
			y1_even_epi16 = _mm_srai_epi16(y1_even_epi16, 1);

			// Reconstruct the luma pixels in the odd row
			y1_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			y1_odd_epi16 = _mm_adds_epi16(y1_odd_epi16, overflowprotect_epi16);
			y1_odd_epi16 = _mm_subs_epu16(y1_odd_epi16, overflowprotect_epi16);
			y1_odd_epi16 = _mm_srai_epi16(y1_odd_epi16, 1);

			// Get the second eight lowpass and eight highpass coefficients
			low2_epi16 = _mm_load_si128(y_low_ptr++);
			low2_epi16 = _mm_slli_epi16(low2_epi16, shift);
			high2_epi16 = _mm_load_si128(y_high_ptr++);
			high2_epi16 = _mm_slli_epi16(high2_epi16, shift);

			// Reconstruct the luma pixels in the even row
			y2_even_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
			y2_even_epi16 = _mm_srai_epi16(y2_even_epi16, 1);

			// Reconstruct the luma pixels in the odd row
			y2_odd_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
			y2_odd_epi16 = _mm_srai_epi16(y2_odd_epi16, 1);

			// Pack sixteen bytes of luma in the even row
			//y_even_epu8 = _mm_packus_epi16(even1_epi16, even2_epi16);

			// Pack sixteen bytes of luma in the odd row
			//y_odd_epu8 = _mm_packus_epi16(odd1_epi16, odd2_epi16);


			/***** Reconstruct eight u chroma pixels for the even and odd rows *****/

			// Load eight lowpass and eight highpass coefficients
			low1_epi16 = _mm_load_si128(u_low_ptr++);
			low1_epi16 = _mm_slli_epi16(low1_epi16, shift);
			high1_epi16 = _mm_load_si128(u_high_ptr++);
			high1_epi16 = _mm_slli_epi16(high1_epi16, shift);

			// Reconstruct the u chroma pixels in the even row
		//	low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
			u_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			u_even_epi16 = _mm_adds_epi16(u_even_epi16, overflowprotect_epi16);
			u_even_epi16 = _mm_subs_epu16(u_even_epi16, overflowprotect_epi16);
			u_even_epi16 = _mm_srai_epi16(u_even_epi16, 1);

			// Reconstruct the u chroma pixels in the odd row
			u_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, overflowprotect_epi16);
			u_odd_epi16 = _mm_subs_epu16(u_odd_epi16, overflowprotect_epi16);
			u_odd_epi16 = _mm_srai_epi16(u_odd_epi16, 1);

	#if _ENCODE_CHROMA_OFFSET
			// Add the chroma offset to the even and odd rows
			u_even_epi16 = _mm_adds_epi16(u_even_epi16, offset_epi16);
			u_odd_epi16 = _mm_adds_epi16(u_odd_epi16, offset_epi16);
	#endif

			/***** Reconstruct eight v chroma pixels for the even and odd rows *****/

			// Load eight lowpass and eight highpass coefficients
			low1_epi16 = _mm_load_si128(v_low_ptr++);
			low1_epi16 = _mm_slli_epi16(low1_epi16, shift);
			high1_epi16 = _mm_load_si128(v_high_ptr++);
			high1_epi16 = _mm_slli_epi16(high1_epi16, shift);

			// Reconstruct the v chroma pixels in the even row
		//	low1_epi16 = _mm_adds_epi16(low1_epi16, overflowprotect_epi16);
			v_even_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			v_even_epi16 = _mm_adds_epi16(v_even_epi16, overflowprotect_epi16);
			v_even_epi16 = _mm_subs_epu16(v_even_epi16, overflowprotect_epi16);
			v_even_epi16 = _mm_srai_epi16(v_even_epi16, 1);

			// Reconstruct the v chroma pixels in the odd row
			v_odd_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
			v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, overflowprotect_epi16);
			v_odd_epi16 = _mm_subs_epu16(v_odd_epi16, overflowprotect_epi16);
			v_odd_epi16 = _mm_srai_epi16(v_odd_epi16, 1);

	#if _ENCODE_CHROMA_OFFSET
			// Add the chroma offset to the even and odd rows
			v_even_epi16 = _mm_adds_epi16(v_even_epi16, offset_epi16);
			v_odd_epi16 = _mm_adds_epi16(v_odd_epi16, offset_epi16);
	#endif

			/***** Interleave the sixteen luma values with alternating chroma *****/

			// Interleave the first four chroma for the even row
			uv1_even_epi16 = _mm_unpacklo_epi16(v_even_epi16, u_even_epi16);

			// Interleave the second four chroma for the even row
			uv2_even_epi16 = _mm_unpackhi_epi16(v_even_epi16, u_even_epi16);

			// Interleave the first four chroma for the odd row
			uv1_odd_epi16 = _mm_unpacklo_epi16(v_odd_epi16, u_odd_epi16);

			// Interleave the second four chroma for the odd row
			uv2_odd_epi16 = _mm_unpackhi_epi16(v_odd_epi16, u_odd_epi16);

			// Interleave the first four luma and chroma pairs for the even row
			yuv1_even_epi16 = _mm_unpacklo_epi16(y1_even_epi16, uv1_even_epi16);

			// Interleave the first four luma and chroma pairs for the odd row
			yuv1_odd_epi16 = _mm_unpacklo_epi16(y1_odd_epi16, uv1_odd_epi16);

			// Saturate the pixels in the even row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv1_even_epi16, limit_epi16);
			yuv1_even_epi16 = _mm_andnot_si128(mask_epi16, yuv1_even_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv1_even_epi16 = _mm_or_si128(yuv1_even_epi16, mask_epi16);

			// Saturate the pixels in the odd row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv1_odd_epi16, limit_epi16);
			yuv1_odd_epi16 = _mm_andnot_si128(mask_epi16, yuv1_odd_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv1_odd_epi16 = _mm_or_si128(yuv1_odd_epi16, mask_epi16);

			// Store the first four pairs of luma and chroma values for the even row
			_mm_store_si128(even_ptr++, yuv1_even_epi16);

			// Store the first four pairs of luma and chroma values for the odd row
			_mm_store_si128(odd_ptr++, yuv1_odd_epi16);

			// Interleave the second four luma and chroma pairs for the even row
			yuv2_even_epi16 = _mm_unpackhi_epi16(y1_even_epi16, uv1_even_epi16);

			// Interleave the second four luma and chroma pairs for the odd row
			yuv2_odd_epi16 = _mm_unpackhi_epi16(y1_odd_epi16, uv1_odd_epi16);

			// Saturate the pixels in the even row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv2_even_epi16, limit_epi16);
			yuv2_even_epi16 = _mm_andnot_si128(mask_epi16, yuv2_even_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv2_even_epi16 = _mm_or_si128(yuv2_even_epi16, mask_epi16);

			// Saturate the pixels in the odd row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv2_odd_epi16, limit_epi16);
			yuv2_odd_epi16 = _mm_andnot_si128(mask_epi16, yuv2_odd_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv2_odd_epi16 = _mm_or_si128(yuv2_odd_epi16, mask_epi16);

			// Store the second four pairs of luma and chroma values for the even row
			_mm_store_si128(even_ptr++, yuv2_even_epi16);

			// Store the second four pairs of luma and chroma values for the odd row
			_mm_store_si128(odd_ptr++, yuv2_odd_epi16);


			// Interleave the third four luma and chroma pairs for the even row
			yuv3_even_epi16 = _mm_unpacklo_epi16(y2_even_epi16, uv2_even_epi16);

			// Interleave the third four luma and chroma pairs for the odd row
			yuv3_odd_epi16 = _mm_unpacklo_epi16(y2_odd_epi16, uv2_odd_epi16);

			// Saturate the pixels in the even row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv3_even_epi16, limit_epi16);
			yuv3_even_epi16 = _mm_andnot_si128(mask_epi16, yuv3_even_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv3_even_epi16 = _mm_or_si128(yuv3_even_epi16, mask_epi16);

			// Saturate the pixels in the odd row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv3_odd_epi16, limit_epi16);
			yuv3_odd_epi16 = _mm_andnot_si128(mask_epi16, yuv3_odd_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv3_odd_epi16 = _mm_or_si128(yuv3_odd_epi16, mask_epi16);

			// Store the first four pairs of luma and chroma values for the even row
			_mm_store_si128(even_ptr++, yuv3_even_epi16);

			// Store the first four pairs of luma and chroma values for the odd row
			_mm_store_si128(odd_ptr++, yuv3_odd_epi16);

			// Interleave the fourth four luma and chroma pairs for the even row
			yuv4_even_epi16 = _mm_unpackhi_epi16(y2_even_epi16, uv2_even_epi16);

			// Interleave the fourth four luma and chroma pairs for the odd row
			yuv4_odd_epi16 = _mm_unpackhi_epi16(y2_odd_epi16, uv2_odd_epi16);

			// Saturate the pixels in the even row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv4_even_epi16, limit_epi16);
			yuv4_even_epi16 = _mm_andnot_si128(mask_epi16, yuv4_even_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv4_even_epi16 = _mm_or_si128(yuv4_even_epi16, mask_epi16);

			// Saturate the pixels in the odd row to the limits of V210
			mask_epi16 = _mm_cmpgt_epi16(yuv4_odd_epi16, limit_epi16);
			yuv4_odd_epi16 = _mm_andnot_si128(mask_epi16, yuv4_odd_epi16);
			mask_epi16 = _mm_and_si128(mask_epi16, limit_epi16);
			yuv4_odd_epi16 = _mm_or_si128(yuv4_odd_epi16, mask_epi16);

			// Store the second four pairs of luma and chroma values for the even row
			_mm_store_si128(even_ptr++, yuv4_even_epi16);

			// Store the second four pairs of luma and chroma values for the odd row
			_mm_store_si128(odd_ptr++, yuv4_odd_epi16);
		}
	}

	// The loop should have terminated at the post processing column
	assert(column == post_column);

#endif

#if 1
	// Process the remaining portion of the row
	for (; column < output_width; column += 2)
	{
		int chroma_column = column / 2;
		int output_column = column * 2;
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;
		int low, high;
		int even, odd;

		// Get the lowpass and highpass coefficients for the first luma value
		low = lowpass[0][column];
		high = highpass[0][column];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

		odd <<= shift;
		even <<= shift;

#if (0 && DEBUG)
		// Suppress the luminance for debugging
		odd = 0;
		even = 0;
#endif
		// Store the luma values for the even and odd rows
		odd_field[k0] = SATURATE_LUMA(odd);
		even_field[k0] = SATURATE_LUMA(even);

		// Get the lowpass and highpass coefficients for the first chroma value
		// Note that the v chroma value is the second byte in the four tuple
		low = lowpass[2][chroma_column];
		high = highpass[2][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

		odd <<= shift;
		even <<= shift;

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset the values for the even and odd rows
		odd += chroma_offset;
		even += chroma_offset;
#endif

#if (0 && DEBUG)
		// Suppress the chrominance for debugging
		odd = 128;
		even = 128;
#endif
		// Store the chroma values for the even and odd rows
		odd_field[k1] = SATURATE_CHROMA(odd);
		even_field[k1] = SATURATE_CHROMA(even);

		// Get the lowpass and highpass coefficients for the second luma value
		low = lowpass[0][column + 1];
		high = highpass[0][column + 1];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

		odd <<= shift;
		even <<= shift;

#if (0 && DEBUG)
		// Suppress the luminance for debugging
		odd = 0;
		even = 0;
#endif
		// Store the luma values for the even and odd rows
		odd_field[k2] = SATURATE_LUMA(odd);
		even_field[k2] = SATURATE_LUMA(even);

		// Get the lowpass and highpass coefficients for the second chroma value
		// Note that the u chroma value is the fourth byte in the four tuple
		low = lowpass[1][chroma_column];
		high = highpass[1][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

		odd <<= shift;
		even <<= shift;

#if _ENCODE_CHROMA_OFFSET
		// Add the chroma offset the values for the even and odd rows
		odd += chroma_offset;
		even += chroma_offset;
#endif

#if (0 && DEBUG)
		// Suppress the chrominance for debugging
		odd = 128;
		even = 128;
#endif
		// Store the chroma values for the even and odd rows
		odd_field[k3] = SATURATE_CHROMA(odd);
		even_field[k3] = SATURATE_CHROMA(even);
	}

#else

	// Fill the remaining portion of the row with a debugging value
	for (; column < output_width; column += 2)
	{
		int chroma_column = column / 2;
		int output_column = column * 2;
		int k0 = output_column;
		int k1 = output_column + 1;
		int k2 = output_column + 2;
		int k3 = output_column + 3;
		int low, high;
		int even, odd;
		const int DEBUG_COLOR_LUMA  = 0;
		const int DEBUG_COLOR_CHROMA = 128;

		// Get the lowpass and highpass coefficients for the first luma value
		low = lowpass[0][column];
		high = highpass[0][column];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

		odd <<= shift;
		even <<= shift;

		odd_field[k0] = DEBUG_COLOR_LUMA;		//SATURATE_8U(odd);
		even_field[k0] = DEBUG_COLOR_LUMA;		//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the first chroma value
		// Note that the v chroma value is the second byte in the four tuple
		low = lowpass[2][chroma_column];
		high = highpass[2][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

		odd <<= shift;
		even <<= shift;

		odd_field[k1] = DEBUG_COLOR_CHROMA;		//SATURATE_8U(odd);
		even_field[k1] = DEBUG_COLOR_CHROMA;	//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the second luma value
		low = lowpass[0][column + 1];
		high = highpass[0][column + 1];

		// Reconstruct the luma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

		odd <<= shift;
		even <<= shift;

		odd_field[k2] = DEBUG_COLOR_LUMA;		//SATURATE_8U(odd);
		even_field[k2] = DEBUG_COLOR_LUMA;		//SATURATE_8U(even);

		// Get the lowpass and highpass coefficients for the second chroma value
		// Note that the u chroma value is the fourth byte in the four tuple
		low = lowpass[1][chroma_column];
		high = highpass[1][chroma_column];

		// Reconstruct the chroma in the even and odd rows
		odd = (low + high)/2;
		even = (low - high)/2;

		odd <<= shift;
		even <<= shift;

		odd_field[k3] = DEBUG_COLOR_CHROMA;		//SATURATE_8U(odd);
		even_field[k3] = DEBUG_COLOR_CHROMA;	//SATURATE_8U(even);
	}
#endif

	// Convert the even and odd rows into V210 pixels
	STOP(tk_inverse);


//#if BUILD_PROSPECT
	if(format == DECODED_FORMAT_V210)
	{
		// Adjust the frame width to fill the row (the v210 loop requires six pixels per iteration)
		frame_width = (3 * pitch )/8;
		assert(frame_width >= output_width);

		ConvertYUV16sRowToV210(even_field, even_output, frame_width);
		ConvertYUV16sRowToV210(odd_field, odd_output, frame_width);
	}
	else if(format == DECODED_FORMAT_YU64)
	{
		ConvertYUV16sRowToYU64(even_field, even_output, frame_width);
		ConvertYUV16sRowToYU64(odd_field, odd_output, frame_width);
	}
//#endif

	START(tk_inverse);
}


// Invert the temporal bands from all channels and pack the output pixels
void InvertInterlacedRow8sToYUV(PIXEL8S *lowpass[], PIXEL8S *highpass[], int num_channels,
								uint8_t *output, int pitch, int output_width, int frame_width)
{
	// Need to implement this routine for 8-bit decoding

#if _DECODE_LOWPASS_16S

#else
#error Have not implemented 8-bit lowpass coefficients
#endif
}



// Invert the temporal transform between two images of 16-bit signed pixels
void InvertTemporal16s8sTo16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch,
							  PIXEL8S *highpass, int highpass_quantization, int highpass_pitch,
							  PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi)
{
	int column_step = 16;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins
	int row, column;

	//PIXEL *field1ptr = field1;
	//PIXEL *field2ptr = field2;

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL8S);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 16) == 0);

	// Compute the column where end of row processing must begin
	post_column = roi.width - (roi.width % column_step);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
		__m128i *lowpass_ptr = (__m128i *)lowpass;
		__m128i *highpass_ptr = (__m128i *)highpass;
		__m128i *even_ptr = (__m128i *)field1;
		__m128i *odd_ptr = (__m128i *)field2;

		// Process column elements in parallel until end of row processing is required
		for (column = 0; column < post_column; column += column_step)
		{
			__m128i lowpass_epi16;		// Eight lowpass coefficient
			__m128i highpass_epi8;		// Sixteen highpass coefficients
			__m128i high_epi16;			// Eight highpass coefficients
			__m128i sign_epi8;
			__m128i even_epi16;
			__m128i odd_epi16;
			__m128i quantization = _mm_set_epi16(highpass_quantization, highpass_quantization,
												 highpass_quantization, highpass_quantization,
												 highpass_quantization, highpass_quantization,
												 highpass_quantization, highpass_quantization);

			// Check that the pointers to the next groups of pixels are properly aligned
			assert(ISALIGNED16(lowpass_ptr));
			assert(ISALIGNED16(highpass_ptr));

			// Load sixteen highpass coefficients
			highpass_epi8 = _mm_load_si128(highpass_ptr++);
			sign_epi8 = _mm_cmplt_epi8(highpass_epi8, _mm_setzero_si128());

			// Load the first eight lowpass coefficients
			lowpass_epi16 = _mm_load_si128(lowpass_ptr++);

			// Unpack the first (lower) eight highpass coefficients
			high_epi16 = _mm_unpacklo_epi8(highpass_epi8, sign_epi8);

			// Undo quantization and scaling
			high_epi16 = _mm_mullo_epi16(high_epi16, quantization);

			// Reconstruct eight pixels in the first field
			even_epi16 = _mm_subs_epi16(lowpass_epi16, high_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);
			_mm_store_si128(even_ptr++, even_epi16);

			// Reconstruct eight pixels in the second field
			odd_epi16 = _mm_adds_epi16(lowpass_epi16, high_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
			_mm_store_si128(odd_ptr++, odd_epi16);

			// Load the next eight lowpass coefficients
			lowpass_epi16 = _mm_load_si128(lowpass_ptr++);

			// Unpack the second (upper) eight highpass coefficients
			high_epi16 = _mm_unpackhi_epi8(highpass_epi8, sign_epi8);

			// Undo quantization and scaling
			high_epi16 = _mm_mullo_epi16(high_epi16, quantization);

			// Reconstruct eight pixels in the first field
			even_epi16 = _mm_subs_epi16(lowpass_epi16, high_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);
			_mm_store_si128(even_ptr++, even_epi16);

			// Reconstruct eight pixels in the second field
			odd_epi16 = _mm_adds_epi16(lowpass_epi16, high_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
			_mm_store_si128(odd_ptr++, odd_epi16);
		}

		// Handle end of row processing for the remaining columns
		for (; column < roi.width; column++) {
			// Get the lowpass and highpass coefficients
			PIXEL low = lowpass[column];
			PIXEL high = highpass[column] * highpass_quantization;

			// Reconstruct the pixels in the even and odd rows
			field1[column] = (low - high)/2;
			field2[column] = (low + high)/2;
			//field1[column] = (low - high);
			//field2[column] = (low + high);
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}
}


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertTemporalQuant16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch,
							PIXEL *highpass, int highpass_quantization, int highpass_pitch,
							PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi,
							PIXEL *buffer, size_t buffer_size, int precision)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Invert the temporal transform between two images of 16-bit signed pixels
// and dequantize the results
void InvertTemporalQuant16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch,
							PIXEL *highpass, int highpass_quantization, int highpass_pitch,
							PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi,
							PIXEL *buffer, size_t buffer_size, int precision)
{
	int column_step = 8;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins
	PIXEL *field1ptr = field1;
	PIXEL *field2ptr = field2;
	int width = roi.width;
	int row, column;

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 16) == 0);

	// Compute the column where end of row processing must begin
	post_column = width - (width % column_step);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
#if (1 && XMMOPT)
		__m64 *lowpass_ptr = (__m64 *)lowpass;
		__m64 *highpass_ptr;
		__m64 *even_ptr = (__m64 *)field1;
		__m64 *odd_ptr = (__m64 *)field2;

		__m64 low1_pi16;		// First four lowpass coefficients
		__m64 high1_pi16;		// First four highpass coefficients

		__m64 halftone_pi16;

//DAN20050921 --- WARNING this temporal dither, fixes a tiny luma shift between frames in a GOP.
		// Potentially dangerous -- although multi-generation is working great.
		if(precision == 8)
		{
			if(row&1)
				halftone_pi16 = _mm_set_pi16(1,0,1,0);
			else
				halftone_pi16 = _mm_set_pi16(0,1,0,1);
		}
		else
		{
			halftone_pi16 = _mm_set1_pi16(0);
		}

		highpass_ptr = (__m64 *)highpass;
#endif
		// Start at the left column
		column = 0;

#if (1 && XMMOPT)

		// Preload the first four lowpass and highpass coefficients
		low1_pi16 = *(lowpass_ptr++);
		high1_pi16 = *(highpass_ptr++);

		// Process column elements in parallel until end of row processing is required
		for (; column < post_column; column += column_step)
		{
			__m64 even1_pi16;		// First four results for the even row
			__m64 even2_pi16;		// Second four results for the even row
			__m64 odd1_pi16;		// First four results for the odd row
			__m64 odd2_pi16;		// Second four results for the odd row
			__m64 low2_pi16;		// Second four lowpass coefficients
			__m64 high2_pi16;		// Second four highpass coefficients

			// Check that the pointers to the next groups of pixels are properly aligned
			//assert(ISALIGNED16(lowpass_ptr));
			//assert(ISALIGNED16(highpass_ptr));

			// Load the next four lowpass coefficients
			low2_pi16 = *(lowpass_ptr++);

			// Load the next four highpass coefficients
			high2_pi16 = *(highpass_ptr++);

			// Reconstruct the first four pixels in the even field
			even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
			even1_pi16 = _mm_srai_pi16(even1_pi16, 1);
			*(even_ptr++) = even1_pi16;

			// Reconstruct the first four pixels in the odd field
			odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
#if !LOSSLESS //This fixes luma/chroma offsets between A & B frames in a GOP.
		odd1_pi16 = _mm_adds_pi16(odd1_pi16, halftone_pi16);
#endif
			odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1);
			*(odd_ptr++) = odd1_pi16;

			// Load the lowpass and highpass coefficients for the next iteration
			low1_pi16 = *(lowpass_ptr++);
			high1_pi16 = *(highpass_ptr++);

			// Reconstruct the second four pixels in the even field
			even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
			even2_pi16 = _mm_srai_pi16(even2_pi16, 1);
			*(even_ptr++) = even2_pi16;

			// Reconstruct the second four pixels in the odd field
			odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
#if !LOSSLESS //This fixes luma/chroma offsets between A & B frames in a GOP.
		odd2_pi16 = _mm_adds_pi16(odd2_pi16, halftone_pi16);
#endif
			odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1);
			*(odd_ptr++) = odd2_pi16;
		}

		//_mm_empty();	// Clear the mmx register state

		// Should have terminated the loop at the post processing column
		assert(column == post_column);

#endif


		// Handle end of row processing for the remaining columns
		if(precision == 8)
		{
			for (; column < width; column++) {
				// Get the lowpass and highpass coefficients
				PIXEL low = lowpass[column];
				PIXEL high = highpass[column];

				// Reconstruct the pixels in the even and odd rows
				field1[column] = (low - high)>>1;
				field2[column] = (low + high
#if !LOSSLESS //This fixes luma/chroma offsets between A & B frames in a GOP.
				+ ((column+row) & 1)
#endif
				)>>1;
				//field1[column] = (low - high);
				//field2[column] = (low + high);
			}
		}
		else
		{
			for (; column < width; column++) {
				// Get the lowpass and highpass coefficients
				PIXEL low = lowpass[column];
				PIXEL high = highpass[column];

				// Reconstruct the pixels in the even and odd rows
				field1[column] = (low - high)>>1;
				field2[column] = (low + high)>>1;
				//field1[column] = (low - high);
				//field2[column] = (low + high);
			}
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Invert the temporal transform between two images of 16-bit signed pixels
// and dequantize the results
void InvertTemporalQuant16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch,
							PIXEL *highpass, int highpass_quantization, int highpass_pitch,
							PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi,
							PIXEL *buffer, size_t buffer_size, int precision)
{
	int column_step = 40;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins
	//PIXEL *field1ptr = field1;
	//PIXEL *field2ptr = field2;
	int width = roi.width;
	int row, column;

	// Length of the row in bytes
	const int row_size = width * sizeof(PIXEL);

	// Compute the cache line size for the prefetch cache
	const size_t prefetch_size = 2 * _CACHE_LINE_SIZE;

	// The distance (in bytes) for prefetching the next block of input data
	const size_t prefetch_offset = 1 * ALIGN(row_size, prefetch_size);

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 16) == 0);

	// Compute the column where end of row processing must begin
	post_column = width - (width % column_step);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
#if (1 && XMMOPT)
		__m128i *lowpass_ptr = (__m128i *)lowpass;
		__m128i *highpass_ptr;
		__m128i *even_ptr = (__m128i *)field1;
		__m128i *odd_ptr = (__m128i *)field2;

		__m128i low1_epi16;		// First eight lowpass coefficients
		__m128i high1_epi16;	// First eight highpass coefficients

		__m128i low2_epi16;		// Second eight lowpass coefficients
		__m128i high2_epi16;	// Second eight highpass coefficients

		__m128i low3_epi16;		// Third eight lowpass coefficients
		__m128i high3_epi16;	// Third eight highpass coefficients

		__m128i halftone_epi16;

//DAN20050921 --- WARNING this temporal dither, fixes a tiny luma shift between frames in a GOP.
		// Potentially dangerous -- although multi-generation is working great.
		if(precision == 8)
		{
			if(row&1)
				halftone_epi16 = _mm_set_epi16(1,0,1,0,1,0,1,0);
			else
				halftone_epi16 = _mm_set_epi16(0,1,0,1,0,1,0,1);
		}
		else
		{
			halftone_epi16 = _mm_set1_epi16(0);
		}


		highpass_ptr = (__m128i *)highpass;
#endif
		// Start at the left column
		column = 0;

#if (1 && XMMOPT)

		// Check that the input and output addresses are properly aligned
		assert(ISALIGNED16(lowpass_ptr));
		assert(ISALIGNED16(highpass_ptr));
		assert(ISALIGNED16(even_ptr));
		assert(ISALIGNED16(odd_ptr));

		// Preload the first eight lowpass and highpass coefficients
		low1_epi16 = _mm_load_si128(lowpass_ptr++);
		high1_epi16 = _mm_load_si128(highpass_ptr++);

		// Preload the second eight lowpass and highpass coefficients
		low2_epi16 = _mm_load_si128(lowpass_ptr++);
		high2_epi16 = _mm_load_si128(highpass_ptr++);

		// Preload the third eight lowpass and highpass coefficients
		low3_epi16 = _mm_load_si128(lowpass_ptr++);
		high3_epi16 = _mm_load_si128(highpass_ptr++);

		// Process column elements in parallel until end of row processing is required
		for (; column < post_column; column += column_step)
		{
			__m128i even1_epi16;	// First eight results for the even row
			__m128i even2_epi16;	// Second eight results for the even row
			__m128i even3_epi16;	// Third eight results for the even row
			__m128i even4_epi16;	// Fourth eight results for the even row
			__m128i even5_epi16;	// Fifth eight results for the even row

			__m128i odd1_epi16;		// First eight results for the odd row
			__m128i odd2_epi16;		// Second eight results for the odd row
			__m128i odd3_epi16;		// Third eight results for the odd row
			__m128i odd4_epi16;		// Fourth eight results for the odd row
			__m128i odd5_epi16;		// Fifth eight results for the odd row

			__m128i low4_epi16;		// Fourth eight lowpass coefficients
			__m128i low5_epi16;		// Fifth eight lowpass coefficients

			__m128i high4_epi16;	// Fourth eight highpass coefficients
			__m128i high5_epi16;	// Fifth eight highpass coefficients

			// Check that the pointers to the next groups of pixels are properly aligned
			//assert(ISALIGNED16(lowpass_ptr));
			//assert(ISALIGNED16(highpass_ptr));

#if (1 && PREFETCH)
			// Prefetch input data that may be used in the near future
			_mm_prefetch((const char *)lowpass_ptr + prefetch_offset, _MM_HINT_T2);
			_mm_prefetch((const char *)highpass_ptr + prefetch_offset, _MM_HINT_T2);
#endif

			/***** First phase of the loop *****/

			// Preload the next eight lowpass coefficients
			low4_epi16 = _mm_load_si128(lowpass_ptr++);

			// Preload the next eight highpass coefficients
			high4_epi16 = _mm_load_si128(highpass_ptr++);

			// Reconstruct the first eight pixels in the even field
			even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
			even1_epi16 = _mm_srai_epi16(even1_epi16, 1);

			// Store the first eight even results
			_mm_store_si128(even_ptr++, even1_epi16);

			// Reconstruct the first eight pixels in the odd field
			odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
#if !LOSSLESS //This fixes luma/chroma offsets between A & B frames in a GOP.
		odd1_epi16 = _mm_adds_epi16(odd1_epi16, halftone_epi16);
#endif
			odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);

			// Store the first eight odd results
			_mm_store_si128(odd_ptr++, odd1_epi16);


			/***** Second phase of the loop *****/

			// Preload the next eight lowpass coefficients
			low5_epi16 = _mm_load_si128(lowpass_ptr++);

			// Preload the next eight highpass coefficients
			high5_epi16 = _mm_load_si128(highpass_ptr++);

			// Reconstruct the second eight pixels in the even field
			even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
			even2_epi16 = _mm_srai_epi16(even2_epi16, 1);

			// Store the second eight even results
			_mm_store_si128(even_ptr++, even2_epi16);

			// Reconstruct the second eight pixels in the odd field
			odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
#if !LOSSLESS //This fixes luma/chroma offsets between A & B frames in a GOP.
		odd2_epi16 = _mm_adds_epi16(odd2_epi16, halftone_epi16);
#endif
			odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);

			// Store the second eight odd results
			_mm_store_si128(odd_ptr++, odd2_epi16);


			/***** Third phase of the loop *****/

			// Preload the lowpass and highpass coefficients for the next iteration
			low1_epi16 = _mm_load_si128(lowpass_ptr++);
			high1_epi16 = _mm_load_si128(highpass_ptr++);

			// Reconstruct the third eight pixels in the even field
			even3_epi16 = _mm_subs_epi16(low3_epi16, high3_epi16);
			even3_epi16 = _mm_srai_epi16(even3_epi16, 1);

			// Store the second eight even results
			_mm_store_si128(even_ptr++, even3_epi16);

			// Reconstruct the third eight pixels in the odd field
			odd3_epi16 = _mm_adds_epi16(low3_epi16, high3_epi16);
#if !LOSSLESS //This fixes luma/chroma offsets between A & B frames in a GOP.
		odd3_epi16 = _mm_adds_epi16(odd3_epi16, halftone_epi16);
#endif
			odd3_epi16 = _mm_srai_epi16(odd3_epi16, 1);

			// Store the second eight odd results
			_mm_store_si128(odd_ptr++, odd3_epi16);


			/***** Fourth phase of the loop *****/

			// Preload the lowpass and highpass coefficients for the next iteration
			low2_epi16 = _mm_load_si128(lowpass_ptr++);
			high2_epi16 = _mm_load_si128(highpass_ptr++);

			// Reconstruct the third eight pixels in the even field
			even4_epi16 = _mm_subs_epi16(low4_epi16, high4_epi16);
			even4_epi16 = _mm_srai_epi16(even4_epi16, 1);

			// Store the second eight even results
			_mm_store_si128(even_ptr++, even4_epi16);

			// Reconstruct the third eight pixels in the odd field
			odd4_epi16 = _mm_adds_epi16(low4_epi16, high4_epi16);
#if !LOSSLESS //This fixes luma/chroma offsets between A & B frames in a GOP.
		odd4_epi16 = _mm_adds_epi16(odd4_epi16, halftone_epi16);
#endif
			odd4_epi16 = _mm_srai_epi16(odd4_epi16, 1);

			// Store the second eight odd results
			_mm_store_si128(odd_ptr++, odd4_epi16);


			/***** Fifth phase of the loop *****/

			// Preload the lowpass and highpass coefficients for the next iteration
			low3_epi16 = _mm_load_si128(lowpass_ptr++);
			high3_epi16 = _mm_load_si128(highpass_ptr++);

			// Reconstruct the third eight pixels in the even field
			even5_epi16 = _mm_subs_epi16(low5_epi16, high5_epi16);
			even5_epi16 = _mm_srai_epi16(even5_epi16, 1);

			// Store the second eight even results
			_mm_store_si128(even_ptr++, even5_epi16);

			// Reconstruct the third eight pixels in the odd field
			odd5_epi16 = _mm_adds_epi16(low5_epi16, high5_epi16);
#if !LOSSLESS //This fixes luma/chroma offsets between A & B frames in a GOP.
		odd5_epi16 = _mm_adds_epi16(odd5_epi16, halftone_epi16);
#endif

			odd5_epi16 = _mm_srai_epi16(odd5_epi16, 1);

			// Store the second eight odd results
			_mm_store_si128(odd_ptr++, odd5_epi16);
		}

		// Should have terminated the loop at the post processing column
		assert(column == post_column);

#endif

		// Handle end of row processing for the remaining columns
		if(precision == 8)
		{
			for (; column < width; column++) {
				// Get the lowpass and highpass coefficients
				PIXEL low = lowpass[column];
				PIXEL high = highpass[column];

				// Reconstruct the pixels in the even and odd rows
				field1[column] = (low - high)>>1;
				field2[column] = (low + high
#if !LOSSLESS //This fixes luma/chroma offsets between A & B frames in a GOP.
				+ ((column+row) & 1)
#endif
				)>>1;
				//field1[column] = (low - high);
				//field2[column] = (low + high);
			}
		}
		else
		{
			for (; column < width; column++) {
				// Get the lowpass and highpass coefficients
				PIXEL low = lowpass[column];
				PIXEL high = highpass[column];

				// Reconstruct the pixels in the even and odd rows
				field1[column] = (low - high)>>1;
				field2[column] = (low + high)>>1;
				//field1[column] = (low - high);
				//field2[column] = (low + high);
			}
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}
}

#endif


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertTemporalQuant16s8sTo16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch,
								   PIXEL8S *highpass, int highpass_quantization, int highpass_pitch,
								   PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi,
								   PIXEL *buffer, size_t buffer_size)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Invert the temporal transform between two images of 16-bit signed pixels
// Version that uses MMX instructions and should run on generic processors
void InvertTemporalQuant16s8sTo16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch,
								   PIXEL8S *highpass, int highpass_quantization, int highpass_pitch,
								   PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi,
								   PIXEL *buffer, size_t buffer_size)
{
	int column_step = 8;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins
	PIXEL *field1ptr = field1;
	PIXEL *field2ptr = field2;
	PIXEL *highline = buffer;
	int width = roi.width;
	int row, column;

	// Check that the buffer is large enough for dequantizing the highpass coefficients
	assert(buffer_size >= (width * sizeof(PIXEL)));

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL8S);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 16) == 0);

	// Compute the column where end of row processing must begin
	post_column = width - (width % column_step);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
		__m64 *lowpass_ptr = (__m64 *)lowpass;
		__m64 *highpass_ptr = (__m64 *)highline;
		__m64 *even_ptr = (__m64 *)field1;
		__m64 *odd_ptr = (__m64 *)field2;

		__m64 low1_pi16;		// First four lowpass coefficients
		__m64 high1_pi16;		// First four highpass coefficients

		// Undo quantization for one highpass row
		DequantizeBandRow(highpass, width, highpass_quantization, highline);

		column = 0;

#if (1 && XMMOPT)

		// Preload the first four lowpass and highpass coefficients
		low1_pi16 = *(lowpass_ptr++);
		high1_pi16 = *(highpass_ptr++);

		// Process column elements in parallel until end of row processing is required
		for (; column < post_column; column += column_step)
		{
			__m64 even1_pi16;		// First four results for the even row
			__m64 even2_pi16;		// Second four results for the even row
			__m64 odd1_pi16;		// First four results for the odd row
			__m64 odd2_pi16;		// Second four results for the odd row
			__m64 low2_pi16;		// Second four lowpass coefficients
			__m64 high2_pi16;		// Second four highpass coefficients

			// Check that the pointers to the next groups of pixels are properly aligned
			//assert(ISALIGNED16(lowpass_ptr));
			//assert(ISALIGNED16(highpass_ptr));

			// Load the next four lowpass coefficients
			low2_pi16 = *(lowpass_ptr++);

			// Load the next four highpass coefficients
			high2_pi16 = *(highpass_ptr++);

			// Reconstruct the first four pixels in the even field
			even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
			even1_pi16 = _mm_srai_pi16(even1_pi16, 1);
			*(even_ptr++) = even1_pi16;

			// Reconstruct the first four pixels in the odd field
			odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
			odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1);
			*(odd_ptr++) = odd1_pi16;

			// Load the lowpass and highpass coefficients for the next iteration
			low1_pi16 = *(lowpass_ptr++);
			high1_pi16 = *(highpass_ptr++);

			// Reconstruct the second four pixels in the even field
			even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
			even2_pi16 = _mm_srai_pi16(even2_pi16, 1);
			*(even_ptr++) = even2_pi16;

			// Reconstruct the second four pixels in the odd field
			odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
			odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1);
			*(odd_ptr++) = odd2_pi16;
		}

		// Should have terminated the loop at the post processing column
		assert(column == post_column);

#endif

		// Handle end of row processing for the remaining columns
		for (; column < width; column++) {
			// Get the lowpass and highpass coefficients
			PIXEL low = lowpass[column];
			PIXEL high = highline[column];

			// Reconstruct the pixels in the even and odd rows
			field1[column] = (low - high)/2;
			field2[column] = (low + high)/2;
			//field1[column] = (low - high);
			//field2[column] = (low + high);
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Invert the temporal transform between two images of 16-bit signed pixels
// Version optimized for the integer SSE2 instructions of the Pentium 4
void InvertTemporalQuant16s8sTo16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch,
								   PIXEL8S *highpass, int highpass_quantization, int highpass_pitch,
								   PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi,
								   PIXEL *buffer, size_t buffer_size)
{
	int column_step = 8;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins
	//PIXEL *field1ptr = field1;
	//PIXEL *field2ptr = field2;
	PIXEL *highline = buffer;
	int width = roi.width;
	int row, column;

	// Check that the buffer is large enough for dequantizing the highpass coefficients
	assert(buffer_size >= (width * sizeof(PIXEL)));

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL8S);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 16) == 0);

	// Compute the column where end of row processing must begin
	post_column = width - (width % column_step);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
		__m128i *lowpass_ptr = (__m128i *)lowpass;
		__m128i *highpass_ptr = (__m128i *)highline;
		__m128i *even_ptr = (__m128i *)field1;
		__m128i *odd_ptr = (__m128i *)field2;

		// Undo quantization for one highpass row
		DequantizeBandRow(highpass, width, highpass_quantization, highline);

		column = 0;

#if (1 && XMMOPT)

		// Process column elements in parallel until end of row processing is required
		for (; column < post_column; column += column_step)
		{
			__m128i lowpass_epi16;		// Eight lowpass coefficient
			__m128i high_epi16;			// Eight highpass coefficients
			__m128i even_epi16;
			__m128i odd_epi16;

			// Check that the pointers to the next groups of pixels are properly aligned
			assert(ISALIGNED16(lowpass_ptr));
			assert(ISALIGNED16(highpass_ptr));

			// Load eight highpass coefficients
			high_epi16 = _mm_load_si128(highpass_ptr++);

			// Load eight lowpass coefficients
			lowpass_epi16 = _mm_load_si128(lowpass_ptr++);

			// Undo quantization and scaling
			//high_epi16 = _mm_mullo_epi16(high_epi16, quantization);

			// Reconstruct eight pixels in the first field
			even_epi16 = _mm_subs_epi16(lowpass_epi16, high_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);
			_mm_store_si128(even_ptr++, even_epi16);

			// Reconstruct eight pixels in the second field
			odd_epi16 = _mm_adds_epi16(lowpass_epi16, high_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
			_mm_store_si128(odd_ptr++, odd_epi16);
		}

		// Should have terminated the loop at the post processing column
		assert(column == post_column);

#endif

		// Handle end of row processing for the remaining columns
		for (; column < width; column++) {
			// Get the lowpass and highpass coefficients
			PIXEL low = lowpass[column];
			PIXEL high = highline[column];

			// Reconstruct the pixels in the even and odd rows
			field1[column] = (low - high)/2;
			field2[column] = (low + high)/2;
			//field1[column] = (low - high);
			//field2[column] = (low + high);
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}

	////_mm_empty();
}

#endif


#if 0
// Invert the temporal transform between two rows of 16-bit signed pixels.
// The lowpass coefficients are 16-bit signed, the highpass are 8-bit signed,
// This routine assumes that the coefficients have already been dequantized.
void InvertTemporalRow16s8sTo16s(PIXEL *lowpass, PIXEL8S *highpass,
								 PIXEL *even, PIXEL *odd, int width)
{
	// Number of elements processed per column iteration
	int column_step = 16;

	// Compute the column where end of row processing must begin
	int post_column = width - (width % column_step);

	__m128i *lowpass_ptr = (__m128i *)lowpass;
	__m128i *highpass_ptr = (__m128i *)highpass;
	__m128i *even_ptr = (__m128i *)even;
	__m128i *odd_ptr = (__m128i *)odd;

	int column;

	// Process column elements in parallel until end of row processing is required
	for (column = 0; column < post_column; column += column_step)
	{
		__m128i lowpass_epi16;		// Eight lowpass coefficient
		__m128i highpass_epi8;		// Sixteen highpass coefficients
		__m128i high_epi16;			// Eight highpass coefficients
		__m128i sign_epi8;			// High sign byte for unpacking
		__m128i even_epi16;			// Result in the even output row
		__m128i odd_epi16;			// Result in the odd output row

		// Check that the pointers to the next groups of pixels are properly aligned
		assert(ISALIGNED16(lowpass_ptr));
		assert(ISALIGNED16(highpass_ptr));

		// Load sixteen highpass coefficients
		highpass_epi8 = _mm_load_si128(highpass_ptr++);
		sign_epi8 = _mm_cmplt_epi8(highpass_epi8, _mm_setzero_si128());

		// Load the first eight lowpass coefficients
		lowpass_epi16 = _mm_load_si128(lowpass_ptr++);

		// Unpack the first (lower) eight highpass coefficients
		high_epi16 = _mm_unpacklo_epi8(highpass_epi8, sign_epi8);

		// Reconstruct eight pixels in the first field
		even_epi16 = _mm_subs_epi16(lowpass_epi16, high_epi16);
		even_epi16 = _mm_srai_epi16(even_epi16, 1);
		_mm_store_si128(even_ptr++, even_epi16);

		// Reconstruct eight pixels in the second field
		odd_epi16 = _mm_adds_epi16(lowpass_epi16, high_epi16);
		odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
		_mm_store_si128(odd_ptr++, odd_epi16);

		// Load the next eight lowpass coefficients
		lowpass_epi16 = _mm_load_si128(lowpass_ptr++);

		// Unpack the second (upper) eight highpass coefficients
		high_epi16 = _mm_unpackhi_epi8(highpass_epi8, sign_epi8);

		// Reconstruct eight pixels in the first field
		even_epi16 = _mm_subs_epi16(lowpass_epi16, high_epi16);
		even_epi16 = _mm_srai_epi16(even_epi16, 1);
		_mm_store_si128(even_ptr++, even_epi16);

		// Reconstruct eight pixels in the second field
		odd_epi16 = _mm_adds_epi16(lowpass_epi16, high_epi16);
		odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
		_mm_store_si128(odd_ptr++, odd_epi16);
	}

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

	// Handle end of row processing for the remaining columns
	for (; column < width; column++)
	{
		// Get the lowpass and highpass coefficients
		PIXEL low = lowpass[column];
		PIXEL high = highpass[column];

		// Reconstruct the pixels in the even and odd rows
		even[column] = (low - high)/2;
		odd[column] = (low + high)/2;
	}
}
#endif


#if 0
// Invert the temporal transform between two rows of 16-bit signed pixels.
// This routine assumes that the coefficients have already been dequantized.
void InvertTemporalRow16s(PIXEL *lowpass, PIXEL *highpass,
						  PIXEL *even, PIXEL *odd, int width)
{
	// Number of elements processed per column iteration
	int column_step = 8;

	// Compute the column where end of row processing must begin
	int post_column = width - (width % column_step);

	__m128i *lowpass_ptr = (__m128i *)lowpass;
	__m128i *highpass_ptr = (__m128i *)highpass;
	__m128i *even_ptr = (__m128i *)even;
	__m128i *odd_ptr = (__m128i *)odd;

	int column = 0;

#if (1 && XMMOPT)

	// Process column elements in parallel until end of row processing is required
	for (; column < post_column; column += column_step)
	{
		__m128i lowpass_epi16;		// Eight lowpass coefficient
		__m128i highpass_epi16;		// Eight highpass coefficients
		__m128i high_epi16;			// Eight highpass coefficients
		__m128i even_epi16;			// Result in the even output row
		__m128i odd_epi16;			// Result in the odd output row

		// Check that the pointers to the next groups of pixels are properly aligned
		assert(ISALIGNED16(lowpass_ptr));
		assert(ISALIGNED16(highpass_ptr));

		// Load sixteen highpass coefficients
		highpass_epi16 = _mm_load_si128(highpass_ptr++);

		// Load the first eight lowpass coefficients
		lowpass_epi16 = _mm_load_si128(lowpass_ptr++);

		// Reconstruct eight pixels in the first field
		even_epi16 = _mm_subs_epi16(lowpass_epi16, highpass_epi16);
		even_epi16 = _mm_srai_epi16(even_epi16, 1);
		_mm_store_si128(even_ptr++, even_epi16);

		// Reconstruct eight pixels in the second field
		odd_epi16 = _mm_adds_epi16(lowpass_epi16, highpass_epi16);
		odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
		_mm_store_si128(odd_ptr++, odd_epi16);
	}

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Handle end of row processing for the remaining columns
	for (; column < width; column++)
	{
		// Get the lowpass and highpass coefficients
		PIXEL low = lowpass[column];
		PIXEL high = highpass[column];

		// Reconstruct the pixels in the even and odd rows
		even[column] = (low - high)/2;
		odd[column] = (low + high)/2;
	}
}
#endif



#if 0

// Note: InvertTemporalQuarterRow16s was not finished before the routine
// was split into even and odd versions to handle each output seperately


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertTemporalQuarterRow16s(PIXEL *lowpass, PIXEL *highpass, PIXEL *even, PIXEL *odd, int width)
{
	// Stub routine for processor specific dispatch
}

#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Invert the temporal transform at quarter resolution
void InvertTemporalQuarterRow16s(PIXEL *lowpass, PIXEL *highpass, PIXEL *even, PIXEL *odd, int width)
{
	int column_step = 8;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins
	PIXEL *field1ptr = field1;
	PIXEL *field2ptr = field2;
	int width = roi.width;
	int row, column;

	// Convert pitch to units of pixels
	pitch1 /= sizeof(PIXEL);
	pitch2 /= sizeof(PIXEL);
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of eight word blocks
	//assert((roi.width % 16) == 0);

	// Compute the column where end of row processing must begin
	post_column = width - (width % column_step);

	// Process a pair of rows from each field
	for (row = 0; row < roi.height; row++)
	{
#if (1 && XMMOPT)
		__m64 *lowpass_ptr = (__m64 *)lowpass;
		__m64 *highpass_ptr;
		__m64 *even_ptr = (__m64 *)field1;
		__m64 *odd_ptr = (__m64 *)field2;

		__m64 low1_pi16;		// First four lowpass coefficients
		__m64 high1_pi16;		// First four highpass coefficients

		highpass_ptr = (__m64 *)highpass;
#endif
		// Start at the left column
		column = 0;

#if (1 && XMMOPT)

		// Preload the first four lowpass and highpass coefficients
		low1_pi16 = *(lowpass_ptr++);
		high1_pi16 = *(highpass_ptr++);

		// Process column elements in parallel until end of row processing is required
		for (; column < post_column; column += column_step)
		{
			__m64 even1_pi16;		// First four results for the even row
			__m64 even2_pi16;		// Second four results for the even row
			__m64 odd1_pi16;		// First four results for the odd row
			__m64 odd2_pi16;		// Second four results for the odd row
			__m64 low2_pi16;		// Second four lowpass coefficients
			__m64 high2_pi16;		// Second four highpass coefficients

			// Check that the pointers to the next groups of pixels are properly aligned
			//assert(ISALIGNED16(lowpass_ptr));
			//assert(ISALIGNED16(highpass_ptr));

			// Load the next four lowpass coefficients
			low2_pi16 = *(lowpass_ptr++);

			// Load the next four highpass coefficients
			high2_pi16 = *(highpass_ptr++);

			// Reconstruct the first four pixels in the even field
			even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
			even1_pi16 = _mm_srai_pi16(even1_pi16, 1);
			*(even_ptr++) = even1_pi16;

			// Reconstruct the first four pixels in the odd field
			odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
			odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1);
			*(odd_ptr++) = odd1_pi16;

			// Load the lowpass and highpass coefficients for the next iteration
			low1_pi16 = *(lowpass_ptr++);
			high1_pi16 = *(highpass_ptr++);

			// Reconstruct the second four pixels in the even field
			even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
			even2_pi16 = _mm_srai_pi16(even2_pi16, 1);
			*(even_ptr++) = even2_pi16;

			// Reconstruct the second four pixels in the odd field
			odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
			odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1);
			*(odd_ptr++) = odd2_pi16;
		}

		//_mm_empty();	// Clear the mmx register state

		// Should have terminated the loop at the post processing column
		assert(column == post_column);

#endif

		// Handle end of row processing for the remaining columns
		for (; column < width; column++) {
			// Get the lowpass and highpass coefficients
			PIXEL low = lowpass[column];
			PIXEL high = highpass[column];

			// Reconstruct the pixels in the even and odd rows
			field1[column] = (low - high)/2;
			field2[column] = (low + high)/2;
			//field1[column] = (low - high);
			//field2[column] = (low + high);
		}

		// Advance to the next input and output rows
		field1 += pitch1;
		field2 += pitch2;
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
	}
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Invert the temporal transform at quarter resolution
void InvertTemporalQuarterRow16s(PIXEL *lowpass, PIXEL *highpass, PIXEL *even, PIXEL *odd, int width)
{
	int column_step = 40;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins

	int column;

#if (1 && XMMOPT)
	__m128i *low_ptr = (__m128i *)lowpass;
	__m128i *high_ptr = (__m128i *)highpass;
	__m128i *even_ptr = (__m128i *)even;
	__m128i *odd_ptr = (__m128i *)odd;

	__m128i low1_epi16;		// First eight lowpass coefficients
	__m128i high1_epi16;	// First eight highpass coefficients

	__m128i low2_epi16;		// Second eight lowpass coefficients
	__m128i high2_epi16;	// Second eight highpass coefficients

	__m128i low3_epi16;		// Third eight lowpass coefficients
	__m128i high3_epi16;	// Third eight highpass coefficients
#endif

	// Compute the column where end of row processing must begin
	post_column = width - (width % column_step);

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	// Check that the input and output addresses are properly aligned
	assert(ISALIGNED16(low_ptr));
	assert(ISALIGNED16(high_ptr));
	assert(ISALIGNED16(even_ptr));
	assert(ISALIGNED16(odd_ptr));

	// Preload the first eight lowpass and highpass coefficients
	low1_epi16 = _mm_load_si128(low_ptr++);
	high1_epi16 = _mm_load_si128(high_ptr++);

	// Preload the second eight lowpass and highpass coefficients
	low2_epi16 = _mm_load_si128(low_ptr++);
	high2_epi16 = _mm_load_si128(high_ptr++);

	// Preload the third eight lowpass and highpass coefficients
	low3_epi16 = _mm_load_si128(low_ptr++);
	high3_epi16 = _mm_load_si128(high_ptr++);

	// Process column elements in parallel until end of row processing is required
	for (; column < post_column; column += column_step)
	{
		__m128i even1_epi16;	// First eight results for the even row
		__m128i even2_epi16;	// Second eight results for the even row
		__m128i even3_epi16;	// Third eight results for the even row
		__m128i even4_epi16;	// Fourth eight results for the even row
		__m128i even5_epi16;	// Fifth eight results for the even row

		__m128i odd1_epi16;		// First eight results for the odd row
		__m128i odd2_epi16;		// Second eight results for the odd row
		__m128i odd3_epi16;		// Third eight results for the odd row
		__m128i odd4_epi16;		// Fourth eight results for the odd row
		__m128i odd5_epi16;		// Fifth eight results for the odd row

		__m128i low4_epi16;		// Fourth eight lowpass coefficients
		__m128i low5_epi16;		// Fifth eight lowpass coefficients

		__m128i high4_epi16;	// Fourth eight highpass coefficients
		__m128i high5_epi16;	// Fifth eight highpass coefficients

		// Check that the pointers to the next groups of pixels are properly aligned
		//assert(ISALIGNED16(low_ptr));
		//assert(ISALIGNED16(high_ptr));

#if (0 && PREFETCH)
		// Prefetch input data that may be used in the near future
		_mm_prefetch((const char *)low_ptr + prefetch_offset, _MM_HINT_T2);
		_mm_prefetch((const char *)high_ptr + prefetch_offset, _MM_HINT_T2);
#endif

		/***** First phase of the loop *****/

		// Preload the next eight lowpass coefficients
		low4_epi16 = _mm_load_si128(low_ptr++);

		// Preload the next eight highpass coefficients
		high4_epi16 = _mm_load_si128(high_ptr++);

		// Reconstruct the first eight pixels in the even field
		even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		//even1_epi16 = _mm_srai_epi16(even1_epi16, 1);
		even1_epi16 = _mm_srai_epi16(even1_epi16, 3);

		// Store the first eight even results
		_mm_store_si128(even_ptr++, even1_epi16);

		// Reconstruct the first eight pixels in the odd field
		odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		//odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, 3);

		// Store the first eight odd results
		_mm_store_si128(odd_ptr++, odd1_epi16);


		/***** Second phase of the loop *****/

		// Preload the next eight lowpass coefficients
		low5_epi16 = _mm_load_si128(low_ptr++);

		// Preload the next eight highpass coefficients
		high5_epi16 = _mm_load_si128(high_ptr++);

		// Reconstruct the second eight pixels in the even field
		even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
		//even2_epi16 = _mm_srai_epi16(even2_epi16, 1);
		even2_epi16 = _mm_srai_epi16(even2_epi16, 3);

		// Store the second eight even results
		_mm_store_si128(even_ptr++, even2_epi16);

		// Reconstruct the second eight pixels in the odd field
		odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
		//odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);
		odd2_epi16 = _mm_srai_epi16(odd2_epi16, 3);

		// Store the second eight odd results
		_mm_store_si128(odd_ptr++, odd2_epi16);


		/***** Third phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low1_epi16 = _mm_load_si128(low_ptr++);
		high1_epi16 = _mm_load_si128(high_ptr++);

		// Reconstruct the third eight pixels in the even field
		even3_epi16 = _mm_subs_epi16(low3_epi16, high3_epi16);
		//even3_epi16 = _mm_srai_epi16(even3_epi16, 1);
		even3_epi16 = _mm_srai_epi16(even3_epi16, 3);

		// Store the second eight even results
		_mm_store_si128(even_ptr++, even3_epi16);

		// Reconstruct the third eight pixels in the odd field
		odd3_epi16 = _mm_adds_epi16(low3_epi16, high3_epi16);
		//odd3_epi16 = _mm_srai_epi16(odd3_epi16, 1);
		odd3_epi16 = _mm_srai_epi16(odd3_epi16, 3);

		// Store the second eight odd results
		_mm_store_si128(odd_ptr++, odd3_epi16);


		/***** Fourth phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low2_epi16 = _mm_load_si128(low_ptr++);
		high2_epi16 = _mm_load_si128(high_ptr++);

		// Reconstruct the third eight pixels in the even field
		even4_epi16 = _mm_subs_epi16(low4_epi16, high4_epi16);
		//even4_epi16 = _mm_srai_epi16(even4_epi16, 1);
		even4_epi16 = _mm_srai_epi16(even4_epi16, 3);

		// Store the second eight even results
		_mm_store_si128(even_ptr++, even4_epi16);

		// Reconstruct the third eight pixels in the odd field
		odd4_epi16 = _mm_adds_epi16(low4_epi16, high4_epi16);
		//odd4_epi16 = _mm_srai_epi16(odd4_epi16, 1);
		odd4_epi16 = _mm_srai_epi16(odd4_epi16, 3);

		// Store the second eight odd results
		_mm_store_si128(odd_ptr++, odd4_epi16);


		/***** Fifth phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low3_epi16 = _mm_load_si128(low_ptr++);
		high3_epi16 = _mm_load_si128(high_ptr++);

		// Reconstruct the third eight pixels in the even field
		even5_epi16 = _mm_subs_epi16(low5_epi16, high5_epi16);
		//even5_epi16 = _mm_srai_epi16(even5_epi16, 1);
		even5_epi16 = _mm_srai_epi16(even5_epi16, 3);

		// Store the second eight even results
		_mm_store_si128(even_ptr++, even5_epi16);

		// Reconstruct the third eight pixels in the odd field
		odd5_epi16 = _mm_adds_epi16(low5_epi16, high5_epi16);
		//odd5_epi16 = _mm_srai_epi16(odd5_epi16, 1);
		odd5_epi16 = _mm_srai_epi16(odd5_epi16, 3);

		// Store the second eight odd results
		_mm_store_si128(odd_ptr++, odd5_epi16);
	}

	// Should have terminated the loop at the post processing column
	assert(column == post_column);

#endif

	// Handle end of row processing for the remaining columns
	for (; column < width; column++)
	{
		// Get the lowpass and highpass coefficients
		PIXEL low = lowpass[column];
		PIXEL high = highpass[column];

		// Reconstruct the pixels in the even and odd rows
		even[column] = (low - high)/8;
		odd[column] = (low + high)/8;
	}
}

#endif

#endif


// Descale by a factor of sixteen and divide by two for the inverse temporal transform
#define QUARTER_RESOLUTION_DESCALING	5
#define QUARTER_RESOLUTION_ROUNDING		(1 << (QUARTER_RESOLUTION_DESCALING - 1))
#define QUARTER_RESOLUTION_PRESCALE		2


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertTemporalQuarterEvenRow16s(PIXEL *lowpass, PIXEL *highpass, PIXEL *output, int width, int precision)
{
	// Stub routine for processor specific dispatch
}

#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

void InvertTemporalQuarterEvenRow16s(PIXEL *lowpass, PIXEL *highpass, PIXEL *output, int width, int precision)
{
	int column_step = 20;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins

	const int descaling = QUARTER_RESOLUTION_DESCALING;
	const int rounding = QUARTER_RESOLUTION_ROUNDING;
	const int prescale = (precision - 8);

	int column;

#if (1 && XMMOPT)
	__m64 *low_ptr = (__m64 *)lowpass;
	__m64 *high_ptr = (__m64 *)highpass;
	__m64 *even_ptr = (__m64 *)output;

	__m64 low1_pi16;		// First eight lowpass coefficients
	__m64 high1_pi16;		// First eight highpass coefficients

	__m64 low2_pi16;		// Second eight lowpass coefficients
	__m64 high2_pi16;		// Second eight highpass coefficients

	__m64 low3_pi16;		// Third eight lowpass coefficients
	__m64 high3_pi16;		// Third eight highpass coefficients

	// Set the rounding constant used before scaling the coefficients
	const __m64 rounding_pi16 = _mm_set1_pi16(rounding);
#endif

	// Compute the column where end of row processing must begin
	post_column = width - (width % column_step);

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	// Check that the input and output addresses are properly aligned
	//assert(ISALIGNED16(low_ptr));
	//assert(ISALIGNED16(high_ptr));
	//assert(ISALIGNED16(even_ptr));

	// Preload the first four lowpass and highpass coefficients
	low1_pi16 = *(low_ptr++);
	high1_pi16 = *(high_ptr++);

	// Preload the second four lowpass and highpass coefficients
	low2_pi16 = *(low_ptr++);
	high2_pi16 = *(high_ptr++);

	// Preload the third four lowpass and highpass coefficients
	low3_pi16 = *(low_ptr++);
	high3_pi16 = *(high_ptr++);

	// Process column elements in parallel until end of row processing is required
	for (; column < post_column; column += column_step)
	{
		__m64 even1_pi16;	// First four results for the even row
		__m64 even2_pi16;	// Second four results for the even row
		__m64 even3_pi16;	// Third four results for the even row
		__m64 even4_pi16;	// Fourth four results for the even row
		__m64 even5_pi16;	// Fifth four results for the even row

		__m64 low4_pi16;	// Fourth four lowpass coefficients
		__m64 low5_pi16;	// Fifth four lowpass coefficients

		__m64 high4_pi16;	// Fourth four highpass coefficients
		__m64 high5_pi16;	// Fifth four highpass coefficients

		// Check that the pointers to the next groups of pixels are properly aligned
		//assert(ISALIGNED16(low_ptr));
		//assert(ISALIGNED16(high_ptr));

#if (0 && PREFETCH)
		// Prefetch input data that may be used in the near future
		_mm_prefetch((const char *)low_ptr + prefetch_offset, _MM_HINT_T2);
		_mm_prefetch((const char *)high_ptr + prefetch_offset, _MM_HINT_T2);
#endif

		/***** First phase of the loop *****/

		// Preload the next four lowpass coefficients
		low4_pi16 = *(low_ptr++);

		// Preload the next four highpass coefficients
		high4_pi16 = *(high_ptr++);

		// Prescale the highpass coefficients
		high1_pi16 = _mm_srai_pi16(high1_pi16, prescale);

		// Reconstruct the first four pixels in the even field
		even1_pi16 = _mm_subs_pi16(low1_pi16, high1_pi16);
		//even1_pi16 = _mm_srai_pi16(even1_pi16, 1);
		even1_pi16 = _mm_adds_pi16(even1_pi16, rounding_pi16);
		even1_pi16 = _mm_srai_pi16(even1_pi16, descaling);

		// Store the first four even results
		*(even_ptr++) = even1_pi16;


		/***** Second phase of the loop *****/

		// Preload the next four lowpass coefficients
		low5_pi16 = *(low_ptr++);

		// Preload the next four highpass coefficients
		high5_pi16 = *(high_ptr++);

		// Prescale the highpass coefficients
		high2_pi16 = _mm_srai_pi16(high2_pi16, prescale);

		// Reconstruct the second four pixels in the even field
		even2_pi16 = _mm_subs_pi16(low2_pi16, high2_pi16);
		//even2_pi16 = _mm_srai_pi16(even2_pi16, 1);
		even2_pi16 = _mm_adds_pi16(even2_pi16, rounding_pi16);
		even2_pi16 = _mm_srai_pi16(even2_pi16, descaling);

		// Store the second four even results
		*(even_ptr++) = even2_pi16;


		/***** Third phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low1_pi16 = *(low_ptr++);
		high1_pi16 = *(high_ptr++);

		// Prescale the highpass coefficients
		high3_pi16 = _mm_srai_pi16(high3_pi16, prescale);

		// Reconstruct the third four pixels in the even field
		even3_pi16 = _mm_subs_pi16(low3_pi16, high3_pi16);
		//even3_pi16 = _mm_srai_pi16(even3_pi16, 1);
		even3_pi16 = _mm_adds_pi16(even3_pi16, rounding_pi16);
		even3_pi16 = _mm_srai_pi16(even3_pi16, descaling);

		// Store the third four even results
		*(even_ptr++) = even3_pi16;


		/***** Fourth phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low2_pi16 = *(low_ptr++);
		high2_pi16 = *(high_ptr++);

		// Prescale the highpass coefficients
		high4_pi16 = _mm_srai_pi16(high4_pi16, prescale);

		// Reconstruct the third four pixels in the even field
		even4_pi16 = _mm_subs_pi16(low4_pi16, high4_pi16);
		//even4_pi16 = _mm_srai_pi16(even4_pi16, 1);
		even4_pi16 = _mm_adds_pi16(even4_pi16, rounding_pi16);
		even4_pi16 = _mm_srai_pi16(even4_pi16, descaling);

		// Store the fourth four even results
		*(even_ptr++) = even4_pi16;


		/***** Fifth phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low3_pi16 = *(low_ptr++);
		high3_pi16 = *(high_ptr++);

		// Prescale the highpass coefficients
		high5_pi16 = _mm_srai_pi16(high5_pi16, prescale);

		// Reconstruct the third four pixels in the even field
		even5_pi16 = _mm_subs_pi16(low5_pi16, high5_pi16);
		//even5_pi16 = _mm_srai_pi16(even5_pi16, 1);
		even5_pi16 = _mm_adds_pi16(even5_pi16, rounding_pi16);
		even5_pi16 = _mm_srai_pi16(even5_pi16, descaling);

		// Store the fifth four even results
		*(even_ptr++) = even5_pi16;
	}

	// Clear the mmx register state
	//_mm_empty();

	// Should have terminated the loop at the post processing column
	assert(column == post_column);

#endif

	// Handle end of row processing for the remaining columns
	for (; column < width; column++)
	{
		// Get the lowpass and highpass coefficients
		PIXEL low = lowpass[column];
		PIXEL high = highpass[column];

		// Prescale the highpass coefficient
		high >>= prescale;

		// Reconstruct the pixels in the even row
		output[column] = (low - high + rounding) >> descaling;
	}
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

void InvertTemporalQuarterEvenRow16s(PIXEL *lowpass, PIXEL *highpass, PIXEL *output, int width, int precision)
{
	int column_step = 40;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins

	const int descaling = QUARTER_RESOLUTION_DESCALING;
	const int rounding = QUARTER_RESOLUTION_ROUNDING;
	const int prescale = (precision - 8);

	int column;

#if (1 && XMMOPT)
	__m128i *low_ptr = (__m128i *)lowpass;
	__m128i *high_ptr = (__m128i *)highpass;
	__m128i *even_ptr = (__m128i *)output;

	__m128i low1_epi16;		// First eight lowpass coefficients
	__m128i high1_epi16;	// First eight highpass coefficients

	__m128i low2_epi16;		// Second eight lowpass coefficients
	__m128i high2_epi16;	// Second eight highpass coefficients

	__m128i low3_epi16;		// Third eight lowpass coefficients
	__m128i high3_epi16;	// Third eight highpass coefficients

	// Set the rounding constant used before scaling the coefficients
	const __m128i rounding_epi16 = _mm_set1_epi16(rounding);
#endif

	// Compute the column where end of row processing must begin
	post_column = width - (width % column_step);

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	// Check that the input and output addresses are properly aligned
	assert(ISALIGNED16(low_ptr));
	assert(ISALIGNED16(high_ptr));
	assert(ISALIGNED16(even_ptr));

	// Preload the first eight lowpass and highpass coefficients
	low1_epi16 = _mm_load_si128(low_ptr++);
	high1_epi16 = _mm_load_si128(high_ptr++);

	// Preload the second eight lowpass and highpass coefficients
	low2_epi16 = _mm_load_si128(low_ptr++);
	high2_epi16 = _mm_load_si128(high_ptr++);

	// Preload the third eight lowpass and highpass coefficients
	low3_epi16 = _mm_load_si128(low_ptr++);
	high3_epi16 = _mm_load_si128(high_ptr++);

	// Process column elements in parallel until end of row processing is required
	for (; column < post_column; column += column_step)
	{
		__m128i even1_epi16;	// First eight results for the even row
		__m128i even2_epi16;	// Second eight results for the even row
		__m128i even3_epi16;	// Third eight results for the even row
		__m128i even4_epi16;	// Fourth eight results for the even row
		__m128i even5_epi16;	// Fifth eight results for the even row

		__m128i low4_epi16;		// Fourth eight lowpass coefficients
		__m128i low5_epi16;		// Fifth eight lowpass coefficients

		__m128i high4_epi16;	// Fourth eight highpass coefficients
		__m128i high5_epi16;	// Fifth eight highpass coefficients

		// Check that the pointers to the next groups of pixels are properly aligned
		//assert(ISALIGNED16(low_ptr));
		//assert(ISALIGNED16(high_ptr));

#if (0 && PREFETCH)
		// Prefetch input data that may be used in the near future
		_mm_prefetch((const char *)low_ptr + prefetch_offset, _MM_HINT_T2);
		_mm_prefetch((const char *)high_ptr + prefetch_offset, _MM_HINT_T2);
#endif

		/***** First phase of the loop *****/

		// Preload the next eight lowpass coefficients
		low4_epi16 = _mm_load_si128(low_ptr++);

		// Preload the next eight highpass coefficients
		high4_epi16 = _mm_load_si128(high_ptr++);

		// Prescale the highpass coefficients
		high1_epi16 = _mm_srai_epi16(high1_epi16, prescale);

		// Reconstruct the first eight pixels in the even field
		even1_epi16 = _mm_subs_epi16(low1_epi16, high1_epi16);
		//even1_epi16 = _mm_srai_epi16(even1_epi16, 1);
		even1_epi16 = _mm_adds_epi16(even1_epi16, rounding_epi16);
		even1_epi16 = _mm_srai_epi16(even1_epi16, descaling);

		// Store the first eight even results
		_mm_store_si128(even_ptr++, even1_epi16);


		/***** Second phase of the loop *****/

		// Preload the next eight lowpass coefficients
		low5_epi16 = _mm_load_si128(low_ptr++);

		// Preload the next eight highpass coefficients
		high5_epi16 = _mm_load_si128(high_ptr++);

		// Prescale the highpass coefficients
		high2_epi16 = _mm_srai_epi16(high2_epi16, prescale);

		// Reconstruct the second eight pixels in the even field
		even2_epi16 = _mm_subs_epi16(low2_epi16, high2_epi16);
		//even2_epi16 = _mm_srai_epi16(even2_epi16, 1);
		even2_epi16 = _mm_adds_epi16(even2_epi16, rounding_epi16);
		even2_epi16 = _mm_srai_epi16(even2_epi16, descaling);

		// Store the second eight even results
		_mm_store_si128(even_ptr++, even2_epi16);


		/***** Third phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low1_epi16 = _mm_load_si128(low_ptr++);
		high1_epi16 = _mm_load_si128(high_ptr++);

		// Prescale the highpass coefficients
		high3_epi16 = _mm_srai_epi16(high3_epi16, prescale);

		// Reconstruct the third eight pixels in the even field
		even3_epi16 = _mm_subs_epi16(low3_epi16, high3_epi16);
		//even3_epi16 = _mm_srai_epi16(even3_epi16, 1);
		even3_epi16 = _mm_adds_epi16(even3_epi16, rounding_epi16);
		even3_epi16 = _mm_srai_epi16(even3_epi16, descaling);

		// Store the third eight even results
		_mm_store_si128(even_ptr++, even3_epi16);


		/***** Fourth phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low2_epi16 = _mm_load_si128(low_ptr++);
		high2_epi16 = _mm_load_si128(high_ptr++);

		// Prescale the highpass coefficients
		high4_epi16 = _mm_srai_epi16(high4_epi16, prescale);

		// Reconstruct the third eight pixels in the even field
		even4_epi16 = _mm_subs_epi16(low4_epi16, high4_epi16);
		//even4_epi16 = _mm_srai_epi16(even4_epi16, 1);
		even4_epi16 = _mm_adds_epi16(even4_epi16, rounding_epi16);
		even4_epi16 = _mm_srai_epi16(even4_epi16, descaling);

		// Store the fourth eight even results
		_mm_store_si128(even_ptr++, even4_epi16);


		/***** Fifth phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low3_epi16 = _mm_load_si128(low_ptr++);
		high3_epi16 = _mm_load_si128(high_ptr++);

		// Prescale the highpass coefficients
		high5_epi16 = _mm_srai_epi16(high5_epi16, prescale);

		// Reconstruct the third eight pixels in the even field
		even5_epi16 = _mm_subs_epi16(low5_epi16, high5_epi16);
		//even5_epi16 = _mm_srai_epi16(even5_epi16, 1);
		even5_epi16 = _mm_adds_epi16(even5_epi16, rounding_epi16);
		even5_epi16 = _mm_srai_epi16(even5_epi16, descaling);

		// Store the fifth eight even results
		_mm_store_si128(even_ptr++, even5_epi16);
	}

	// Should have terminated the loop at the post processing column
	assert(column == post_column);

#endif

	// Handle end of row processing for the remaining columns
	for (; column < width; column++)
	{
		// Get the lowpass and highpass coefficients
		PIXEL low = lowpass[column];
		PIXEL high = highpass[column];

		// Prescale the highpass coefficient
		high >>= prescale;

		// Reconstruct the pixels in the even row
		output[column] = (low - high + rounding) >> descaling;
	}
}

#endif


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertTemporalQuarterOddRow16s(PIXEL *lowpass, PIXEL *highpass, PIXEL *output, int width, int precision)
{
	// Stub routine for processor specific dispatch
}

#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

void InvertTemporalQuarterOddRow16s(PIXEL *lowpass, PIXEL *highpass, PIXEL *output, int width, int precision)
{
	int column_step = 20;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins

	const int descaling = QUARTER_RESOLUTION_DESCALING;
	const int rounding = QUARTER_RESOLUTION_ROUNDING;
	const int prescale = (precision - 8);

	int column;

#if (1 && XMMOPT)
	__m64 *low_ptr = (__m64 *)lowpass;
	__m64 *high_ptr = (__m64 *)highpass;
	__m64 *odd_ptr = (__m64 *)output;

	__m64 low1_pi16;		// First four lowpass coefficients
	__m64 high1_pi16;		// First four highpass coefficients

	__m64 low2_pi16;		// Second four lowpass coefficients
	__m64 high2_pi16;		// Second four highpass coefficients

	__m64 low3_pi16;		// Third four lowpass coefficients
	__m64 high3_pi16;		// Third four highpass coefficients

	// Set the rounding constant used before scaling the coefficients
	const __m64 rounding_pi16 = _mm_set1_pi16(rounding);
#endif

	// Compute the column where end of row processing must begin
	post_column = width - (width % column_step);

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	// Check that the input and output addresses are properly aligned
	//assert(ISALIGNED16(low_ptr));
	//assert(ISALIGNED16(high_ptr));
	//assert(ISALIGNED16(odd_ptr));

	// Preload the first four lowpass and highpass coefficients
	low1_pi16 = *(low_ptr++);
	high1_pi16 = *(high_ptr++);

	// Preload the second four lowpass and highpass coefficients
	low2_pi16 = *(low_ptr++);
	high2_pi16 = *(high_ptr++);

	// Preload the third four lowpass and highpass coefficients
	low3_pi16 = *(low_ptr++);
	high3_pi16 = *(high_ptr++);

	// Process column elements in parallel until end of row processing is required
	for (; column < post_column; column += column_step)
	{
		__m64 odd1_pi16;		// First four results for the odd row
		__m64 odd2_pi16;		// Second four results for the odd row
		__m64 odd3_pi16;		// Third four results for the odd row
		__m64 odd4_pi16;		// Fourth four results for the odd row
		__m64 odd5_pi16;		// Fifth four results for the odd row

		__m64 low4_pi16;		// Fourth four lowpass coefficients
		__m64 low5_pi16;		// Fifth four lowpass coefficients

		__m64 high4_pi16;		// Fourth four highpass coefficients
		__m64 high5_pi16;		// Fifth four highpass coefficients

		// Check that the pointers to the next groups of pixels are properly aligned
		//assert(ISALIGNED16(low_ptr));
		//assert(ISALIGNED16(high_ptr));

#if (0 && PREFETCH)
		// Prefetch input data that may be used in the near future
		_mm_prefetch((const char *)low_ptr + prefetch_offset, _MM_HINT_T2);
		_mm_prefetch((const char *)high_ptr + prefetch_offset, _MM_HINT_T2);
#endif

		/***** First phase of the loop *****/

		// Preload the next four lowpass coefficients
		low4_pi16 = *(low_ptr++);

		// Preload the next four highpass coefficients
		high4_pi16 = *(high_ptr++);

		// Prescale the highpass coefficients
		high1_pi16 = _mm_srai_pi16(high1_pi16, prescale);

		// Reconstruct the first four pixels in the odd field
		odd1_pi16 = _mm_adds_pi16(low1_pi16, high1_pi16);
		//odd1_pi16 = _mm_srai_pi16(odd1_pi16, 1);
		odd1_pi16 = _mm_adds_pi16(odd1_pi16, rounding_pi16);
		odd1_pi16 = _mm_srai_pi16(odd1_pi16, descaling);

		// Store the first four odd results
		*(odd_ptr++) = odd1_pi16;


		/***** Second phase of the loop *****/

		// Preload the next four lowpass coefficients
		low5_pi16 = *(low_ptr++);

		// Preload the next four highpass coefficients
		high5_pi16 = *(high_ptr++);

		// Prescale the highpass coefficients
		high2_pi16 = _mm_srai_pi16(high2_pi16, prescale);

		// Reconstruct the second four pixels in the odd field
		odd2_pi16 = _mm_adds_pi16(low2_pi16, high2_pi16);
		//odd2_pi16 = _mm_srai_pi16(odd2_pi16, 1);
		odd2_pi16 = _mm_adds_pi16(odd2_pi16, rounding_pi16);
		odd2_pi16 = _mm_srai_pi16(odd2_pi16, descaling);

		// Store the second four odd results
		*(odd_ptr++) = odd2_pi16;


		/***** Third phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low1_pi16 = *(low_ptr++);
		high1_pi16 = *(high_ptr++);

		// Prescale the highpass coefficients
		high3_pi16 = _mm_srai_pi16(high3_pi16, prescale);

		// Reconstruct the third four pixels in the odd field
		odd3_pi16 = _mm_adds_pi16(low3_pi16, high3_pi16);
		//odd3_pi16 = _mm_srai_pi16(odd3_pi16, 1);
		odd3_pi16 = _mm_adds_pi16(odd3_pi16, rounding_pi16);
		odd3_pi16 = _mm_srai_pi16(odd3_pi16, descaling);

		// Store the third four odd results
		*(odd_ptr++) = odd3_pi16;


		/***** Fourth phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low2_pi16 = *(low_ptr++);
		high2_pi16 = *(high_ptr++);

		// Prescale the highpass coefficients
		high4_pi16 = _mm_srai_pi16(high4_pi16, prescale);

		// Reconstruct the third four pixels in the odd field
		odd4_pi16 = _mm_adds_pi16(low4_pi16, high4_pi16);
		//odd4_pi16 = _mm_srai_pi16(odd4_pi16, 1);
		odd3_pi16 = _mm_adds_pi16(odd3_pi16, rounding_pi16);
		odd4_pi16 = _mm_srai_pi16(odd4_pi16, descaling);

		// Store the fourth four odd results
		*(odd_ptr++) = odd4_pi16;


		/***** Fifth phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low3_pi16 = *(low_ptr++);
		high3_pi16 = *(high_ptr++);

		// Prescale the highpass coefficients
		high5_pi16 = _mm_srai_pi16(high5_pi16, prescale);

		// Reconstruct the third four pixels in the odd field
		odd5_pi16 = _mm_adds_pi16(low5_pi16, high5_pi16);
		//odd5_pi16 = _mm_srai_pi16(odd5_pi16, 1);
		odd5_pi16 = _mm_adds_pi16(odd5_pi16, rounding_pi16);
		odd5_pi16 = _mm_srai_pi16(odd5_pi16, descaling);

		// Store the fifth four odd results
		*(odd_ptr++) = odd5_pi16;
	}

	// Clear the mmx register state
	//_mm_empty();

	// Should have terminated the loop at the post processing column
	assert(column == post_column);

#endif

	// Handle end of row processing for the remaining columns
	for (; column < width; column++)
	{
		// Get the lowpass and highpass coefficients
		PIXEL low = lowpass[column];
		PIXEL high = highpass[column];

		// Prescale the highpass coefficient
		high >>= prescale;

		// Reconstruct the pixels in the odd row
		output[column] = (low + high + rounding) >> descaling;
	}
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

void InvertTemporalQuarterOddRow16s(PIXEL *lowpass, PIXEL *highpass, PIXEL *output, int width, int precision)
{
	int column_step = 40;	// Number of elements processed per column iteration
	int post_column;		// Column where end of row processing begins

	const int descaling = QUARTER_RESOLUTION_DESCALING;
	const int rounding = QUARTER_RESOLUTION_ROUNDING;
	const int prescale = (precision - 8);

	int column;

#if (1 && XMMOPT)
	__m128i *low_ptr = (__m128i *)lowpass;
	__m128i *high_ptr = (__m128i *)highpass;
	__m128i *odd_ptr = (__m128i *)output;

	__m128i low1_epi16;		// First eight lowpass coefficients
	__m128i high1_epi16;	// First eight highpass coefficients

	__m128i low2_epi16;		// Second eight lowpass coefficients
	__m128i high2_epi16;	// Second eight highpass coefficients

	__m128i low3_epi16;		// Third eight lowpass coefficients
	__m128i high3_epi16;	// Third eight highpass coefficients

	// Set the rounding constant used before scaling the coefficients
	const __m128i rounding_epi16 = _mm_set1_epi16(rounding);
#endif

	// Compute the column where end of row processing must begin
	post_column = width - (width % column_step);

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	// Check that the input and output addresses are properly aligned
	assert(ISALIGNED16(low_ptr));
	assert(ISALIGNED16(high_ptr));
	assert(ISALIGNED16(odd_ptr));

	// Preload the first eight lowpass and highpass coefficients
	low1_epi16 = _mm_load_si128(low_ptr++);
	high1_epi16 = _mm_load_si128(high_ptr++);

	// Preload the second eight lowpass and highpass coefficients
	low2_epi16 = _mm_load_si128(low_ptr++);
	high2_epi16 = _mm_load_si128(high_ptr++);

	// Preload the third eight lowpass and highpass coefficients
	low3_epi16 = _mm_load_si128(low_ptr++);
	high3_epi16 = _mm_load_si128(high_ptr++);

	// Process column elements in parallel until end of row processing is required
	for (; column < post_column; column += column_step)
	{
		__m128i odd1_epi16;		// First eight results for the odd row
		__m128i odd2_epi16;		// Second eight results for the odd row
		__m128i odd3_epi16;		// Third eight results for the odd row
		__m128i odd4_epi16;		// Fourth eight results for the odd row
		__m128i odd5_epi16;		// Fifth eight results for the odd row

		__m128i low4_epi16;		// Fourth eight lowpass coefficients
		__m128i low5_epi16;		// Fifth eight lowpass coefficients

		__m128i high4_epi16;	// Fourth eight highpass coefficients
		__m128i high5_epi16;	// Fifth eight highpass coefficients

		// Check that the pointers to the next groups of pixels are properly aligned
		//assert(ISALIGNED16(low_ptr));
		//assert(ISALIGNED16(high_ptr));

#if (0 && PREFETCH)
		// Prefetch input data that may be used in the near future
		_mm_prefetch((const char *)low_ptr + prefetch_offset, _MM_HINT_T2);
		_mm_prefetch((const char *)high_ptr + prefetch_offset, _MM_HINT_T2);
#endif

		/***** First phase of the loop *****/

		// Preload the next eight lowpass coefficients
		low4_epi16 = _mm_load_si128(low_ptr++);

		// Preload the next eight highpass coefficients
		high4_epi16 = _mm_load_si128(high_ptr++);

		// Prescale the highpass coefficients
		high1_epi16 = _mm_srai_epi16(high1_epi16, prescale);

		// Reconstruct the first eight pixels in the odd field
		odd1_epi16 = _mm_adds_epi16(low1_epi16, high1_epi16);
		//odd1_epi16 = _mm_srai_epi16(odd1_epi16, 1);
		odd1_epi16 = _mm_adds_epi16(odd1_epi16, rounding_epi16);
		odd1_epi16 = _mm_srai_epi16(odd1_epi16, descaling);

		// Store the first eight odd results
		_mm_store_si128(odd_ptr++, odd1_epi16);


		/***** Second phase of the loop *****/

		// Preload the next eight lowpass coefficients
		low5_epi16 = _mm_load_si128(low_ptr++);

		// Preload the next eight highpass coefficients
		high5_epi16 = _mm_load_si128(high_ptr++);

		// Prescale the highpass coefficients
		high2_epi16 = _mm_srai_epi16(high2_epi16, prescale);

		// Reconstruct the second eight pixels in the odd field
		odd2_epi16 = _mm_adds_epi16(low2_epi16, high2_epi16);
		//odd2_epi16 = _mm_srai_epi16(odd2_epi16, 1);
		odd2_epi16 = _mm_adds_epi16(odd2_epi16, rounding_epi16);
		odd2_epi16 = _mm_srai_epi16(odd2_epi16, descaling);

		// Store the second eight odd results
		_mm_store_si128(odd_ptr++, odd2_epi16);


		/***** Third phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low1_epi16 = _mm_load_si128(low_ptr++);
		high1_epi16 = _mm_load_si128(high_ptr++);

		// Prescale the highpass coefficients
		high3_epi16 = _mm_srai_epi16(high3_epi16, prescale);

		// Reconstruct the third eight pixels in the odd field
		odd3_epi16 = _mm_adds_epi16(low3_epi16, high3_epi16);
		//odd3_epi16 = _mm_srai_epi16(odd3_epi16, 1);
		odd3_epi16 = _mm_adds_epi16(odd3_epi16, rounding_epi16);
		odd3_epi16 = _mm_srai_epi16(odd3_epi16, descaling);

		// Store the third eight odd results
		_mm_store_si128(odd_ptr++, odd3_epi16);


		/***** Fourth phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low2_epi16 = _mm_load_si128(low_ptr++);
		high2_epi16 = _mm_load_si128(high_ptr++);

		// Prescale the highpass coefficients
		high4_epi16 = _mm_srai_epi16(high4_epi16, prescale);

		// Reconstruct the third eight pixels in the odd field
		odd4_epi16 = _mm_adds_epi16(low4_epi16, high4_epi16);
		//odd4_epi16 = _mm_srai_epi16(odd4_epi16, 1);
		odd3_epi16 = _mm_adds_epi16(odd3_epi16, rounding_epi16);
		odd4_epi16 = _mm_srai_epi16(odd4_epi16, descaling);

		// Store the fourth eight odd results
		_mm_store_si128(odd_ptr++, odd4_epi16);


		/***** Fifth phase of the loop *****/

		// Preload the lowpass and highpass coefficients for the next iteration
		low3_epi16 = _mm_load_si128(low_ptr++);
		high3_epi16 = _mm_load_si128(high_ptr++);

		// Prescale the highpass coefficients
		high5_epi16 = _mm_srai_epi16(high5_epi16, prescale);

		// Reconstruct the third eight pixels in the odd field
		odd5_epi16 = _mm_adds_epi16(low5_epi16, high5_epi16);
		//odd5_epi16 = _mm_srai_epi16(odd5_epi16, 1);
		odd5_epi16 = _mm_adds_epi16(odd5_epi16, rounding_epi16);
		odd5_epi16 = _mm_srai_epi16(odd5_epi16, descaling);

		// Store the fifth eight odd results
		_mm_store_si128(odd_ptr++, odd5_epi16);
	}

	// Should have terminated the loop at the post processing column
	assert(column == post_column);

#endif

	// Handle end of row processing for the remaining columns
	for (; column < width; column++)
	{
		// Get the lowpass and highpass coefficients
		PIXEL low = lowpass[column];
		PIXEL high = highpass[column];

		// Prescale the highpass coefficient
		high >>= prescale;

		// Reconstruct the pixels in the odd row
		output[column] = (low + high + rounding) >> descaling;
	}
}

#endif

// Descale and pack the pixels in each output row
void CopyQuarterRowToBuffer(PIXEL **input,
							int num_channels,
							uint8_t *output,
							int width,
							int precision,
							int format)
{
	// Note: This routine swaps the chroma values
	PIXEL16U *y_input_ptr = (PIXEL16U *)input[0];
	PIXEL16U *u_input_ptr = (PIXEL16U *)input[2];
	PIXEL16U *v_input_ptr = (PIXEL16U *)input[1];

	uint8_t *yuv_output_ptr = output;

	// Right shift for converting 16-bit pixels to 8-bit pixels
	const int descale = 4;

	int column;


#if (1 && XMMOPT)

	// Process sixteen values of luma and chroma per loop iteration
	const int column_step = 16;

	// Compute the column where end of row processing must begin
	int post_column = width - (width % column_step);

	// Initialize the input pointers into each channel
	__m128i *y_ptr = (__m128i *)y_input_ptr;
	__m128i *u_ptr = (__m128i *)u_input_ptr;
	__m128i *v_ptr = (__m128i *)v_input_ptr;

	// Initialize the output pointer for the packed YUV results
	__m128i *yuv_ptr = (__m128i *)yuv_output_ptr;

#endif

	// Start procesing at the left column
	column = 0;

#if (1 && XMMOPT)

	for (; column < post_column; column += column_step)
	{
		__m128i y1_epi16;
		__m128i y2_epi16;
		__m128i u1_epi16;
		__m128i v1_epi16;
		__m128i uv_epi16;
		__m128i yuv1_epi16;
		__m128i yuv2_epi16;
		__m128i yuv_epi8;

		// Load eight u chroma values
		u1_epi16 = _mm_load_si128(u_ptr++);

		// Load eight v chroma values
		v1_epi16 = _mm_load_si128(v_ptr++);

		// Load the first eight luma values
		y1_epi16 = _mm_load_si128(y_ptr++);

		// Load the second eight luma values
		y2_epi16 = _mm_load_si128(y_ptr++);

		// Reduce the pixel values to eight bits
		u1_epi16 = _mm_srli_epi16(u1_epi16, descale);
		v1_epi16 = _mm_srli_epi16(v1_epi16, descale);
		y1_epi16 = _mm_srli_epi16(y1_epi16, descale);
		y2_epi16 = _mm_srli_epi16(y2_epi16, descale);

		// Interleave the first four chroma values
		uv_epi16 = _mm_unpacklo_epi16(u1_epi16, v1_epi16);

		// Interleave the first four luma values with the chroma pairs
		yuv1_epi16 = _mm_unpacklo_epi16(y1_epi16, uv_epi16);

		// Interleave the second four luma values with the chroma pairs
		yuv2_epi16 = _mm_unpackhi_epi16(y1_epi16, uv_epi16);

		// Pack the first eight luma and chroma pairs
		yuv_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

		// Store the first eight luma and chroma pairs
		_mm_store_si128(yuv_ptr++, yuv_epi8);

		// Interleave the second four chroma values
		uv_epi16 = _mm_unpackhi_epi16(u1_epi16, v1_epi16);

		if(format == DECODED_FORMAT_UYVY)
		{
			// Interleave the third four luma values with the chroma pairs
			yuv1_epi16 = _mm_unpacklo_epi16(uv_epi16, y2_epi16);

			// Interleave the fourth four luma values with the chroma pairs
			yuv2_epi16 = _mm_unpackhi_epi16(uv_epi16, y2_epi16);
		}
		else
		{
			// Interleave the third four luma values with the chroma pairs
			yuv1_epi16 = _mm_unpacklo_epi16(y2_epi16, uv_epi16);

			// Interleave the fourth four luma values with the chroma pairs
			yuv2_epi16 = _mm_unpackhi_epi16(y2_epi16, uv_epi16);
		}

		// Pack the second eight luma and chroma pairs
		yuv_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

		// Store the second eight luma and chroma pairs
		_mm_store_si128(yuv_ptr++, yuv_epi8);
	}

	// Should have terminated the loop at the post processing column
	assert(column == post_column);

	// Set the output pointer to the rest of the row of packed YUV
	yuv_output_ptr = (uint8_t *)yuv_ptr;

#endif

	// Handle end of row processing for the remaining columns
	assert((width % 2) == 0);
	for (; column < width; column += 2)
	{
		int chroma_column = column / 2;

		int y1 = y_input_ptr[column];
		int y2 = y_input_ptr[column + 1];

		int u = u_input_ptr[chroma_column];
		int v = v_input_ptr[chroma_column];

		// Descale the luma and chroma values
		y1 >>= descale;
		y2 >>= descale;

		u >>= descale;
		v >>= descale;

		if(format == DECODED_FORMAT_UYVY)
		{
			*(yuv_output_ptr++) = u;
			*(yuv_output_ptr++) = y1;
			*(yuv_output_ptr++) = v;
			*(yuv_output_ptr++) = y2;
		}
		else
		{
			*(yuv_output_ptr++) = y1;
			*(yuv_output_ptr++) = u;
			*(yuv_output_ptr++) = y2;
			*(yuv_output_ptr++) = v;
		}
	}
}
