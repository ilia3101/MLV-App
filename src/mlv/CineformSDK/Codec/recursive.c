/*! @file recursive.c

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

#if _RECURSIVE

#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif


#include "recursive.h"
#include "encoder.h"
#include "wavelet.h"
#include "image.h"
#include "filter.h"
#include "assert.h"
#include "debug.h"
#include "spatial.h"
#include "temporal.h"
#include "convert.h"
#include "timing.h"

#define XMMOPT (1 && _XMMOPT)
#define DEBUG  (1 && _DEBUG)
#define TIMING (1 && _TIMING)

#if __APPLE__
#include "macdefs.h"
#else
#ifndef ZeroMemory
#define ZeroMemory(p,s)		memset(p,0,s)
#endif
#ifndef CopyMemory
#define CopyMemory(p,q,s)	memcpy(p,q,s)
#endif
#endif

// Datatype used for computing upper half of product of 16-bit multiplication
typedef union {
	uint32_t longword;
	struct {
		unsigned short lower;
		unsigned short upper;
	} halfword;
} LONGWORD;

// Performance measurements
#if _TIMING
extern TIMER tk_recursive;
#endif


#if _DEBUG

void indentf(FILE *logfile, int level)
{
	//fprintf(logfile, "[%d]", level);
	for (; level > 0; level--) {
		fprintf(logfile, "    ");
	}
}

void PrintArray(FILE *logfile, PIXEL *array, int width, int height)
{
	PIXEL *p = array;
	int row;
	int column;

	for (row = 0; row < height; row++)
	{
		for (column = 0; column < width; column++)
		{
			fprintf(logfile, "%5d", *(p++));
		}
		fprintf(logfile, "\n");
	}

	fprintf(logfile, "\n");
}

#endif


/***** Allocation routines *****/

BYTE *AllocateInputBuffer(BYTE *buffer, int width, PIXEL **input_buffer)
{
	BYTE *bufptr = buffer;
	int buffer_size = width * sizeof(PIXEL);

	// Force proper alignment
	buffer_size = ALIGN16(buffer_size);

	*input_buffer = (PIXEL *)buffer;
	bufptr += buffer_size;

	return bufptr;
}

BYTE *AllocateHorizontalBuffers(BYTE *buffer, int width,
								PIXEL *lowpass_buffer_array[6],
								PIXEL *highpass_buffer_array[6])
{
	int buffer_width = width/2;
	int buffer_size = buffer_width * sizeof(PIXEL);
	BYTE *bufptr = buffer;
	int row;

	// Force proper alignment
	buffer_size = ALIGN16(buffer_size);

	for (row = 0; row < NUM_WAVELET_ROWS; row++)
	{
		// The lowpass and highpass buffers must be contiguous so that
		// each lowpass/highpass pair can be used as a single row for the
		// output of the interlaced transform

		lowpass_buffer_array[row] = (PIXEL *)bufptr;
		bufptr += buffer_size;

		highpass_buffer_array[row] = (PIXEL *)bufptr;
		bufptr += buffer_size;
	}

	return bufptr;
}

BYTE *AllocateWaveletBandRows(BYTE *buffer, int width, PIXEL *output_buffer_array[4])
{
	int buffer_size = width * sizeof(PIXEL);
	BYTE *bufptr = buffer;
	int band;

	// Force proper alignment
	buffer_size = ALIGN16(buffer_size);

	for (band = 0; band < NUM_WAVELET_BANDS; band++)
	{
		output_buffer_array[band] = (PIXEL *)bufptr;
		bufptr += buffer_size;
	}

	return bufptr;
}

BYTE *AllocateInterlacedBuffers(TRANSFORM_STATE *state, BYTE *buffer, int width)
{
	int output_width = width/2;
	size_t buffer_size = width * sizeof(PIXEL);
	size_t output_size = output_width * sizeof(PIXEL);
	BYTE *bufptr = buffer;

	// Force proper alignment
	buffer_size = ALIGN16(buffer_size);
	output_size = ALIGN16(output_size);

	// Allocate two buffers for the temporal lowpass and highpass results
	state->buffers.interlaced.lowpass = (PIXEL *)bufptr;
	bufptr += buffer_size;

	state->buffers.interlaced.highpass = (PIXEL *)bufptr;
	bufptr += buffer_size;

	// Allocate output buffers for the rows from each wavelet band
	state->buffers.interlaced.lowlow = (PIXEL *)bufptr;
	bufptr += output_size;

	state->buffers.interlaced.lowhigh = (PIXEL *)bufptr;
	bufptr += output_size;

	state->buffers.interlaced.highlow = (PIXEL *)bufptr;
	bufptr += output_size;

	state->buffers.interlaced.highhigh = (PIXEL *)bufptr;
	bufptr += output_size;

	return bufptr;
}

/***** Recursive wavelet processing routines *****/

void ShiftHorizontalBuffers(PIXEL *lowpass_buffer_array[6], PIXEL *highpass_buffer_array[6])
{
	PIXEL *temp_lowpass_buffer_ptr[2];
	PIXEL *temp_highpass_buffer_ptr[2];
	int row;

	for (row = 0; row < 2; row++)
	{
		temp_lowpass_buffer_ptr[row] = lowpass_buffer_array[row];
		temp_highpass_buffer_ptr[row] = highpass_buffer_array[row];

	}

	for (row = 0; row < 4; row++)
	{
		lowpass_buffer_array[row] = lowpass_buffer_array[row + 2];
		highpass_buffer_array[row] = highpass_buffer_array[row + 2];
	}

	for (row = 0; row < 2; row++)
	{
		lowpass_buffer_array[row + 4] = temp_lowpass_buffer_ptr[row];
		highpass_buffer_array[row + 4] = temp_highpass_buffer_ptr[row];
	}
}

void FilterSpatialHorizontalRow(PIXEL *input, int width, PIXEL *lowpass, PIXEL *highpass)
{
	PIXEL *input_ptr = input;
	int output_width = width/2;

	PIXEL *lowpass_ptr = lowpass;
	PIXEL *highpass_ptr = highpass;

	const int last_column = width - 2;
	int column;
	int sum;

	// Handle the first output column as a special case
	*(lowpass_ptr++) = input[0] + input[1];

	// The highpass filter on the left border uses different coefficients
	sum = 0;
	sum +=  5 * input[0];
	sum -= 11 * input[1];
	sum +=  4 * input[2];
	sum +=  4 * input[3];
	sum -=  1 * input[4];
	sum -=  1 * input[5];
	sum += ROUNDING(sum,8);
	sum = DivideByShift(sum, 3);
	*(highpass_ptr++) = SATURATE(sum);

	// Handle the middle columns
	for (column = 2; column < last_column; column += 2)
	{
		// Compute the lowpass sum
		sum = input[column] + input[column + 1];

		// Store the lowpass sum
		*(lowpass_ptr++) = SATURATE(sum);

		// Compute the highpass sum
		sum = 0;
		sum -= input[column - 2];
		sum -= input[column - 1];
		sum += input[column + 2];
		sum += input[column + 3];
		sum += 4;
		sum >>= 3;
		sum += input[column + 0];
		sum -= input[column + 1];

		// Store the highpass sum
		*(highpass_ptr++) = SATURATE(sum);
	}

	// Handle the last column as a special case
	assert(column == last_column);

	// Compute the last lowpass value
	*(lowpass_ptr++) = input[column] + input[column + 1];

	// Compute the last highpass value using the special border coefficients
	sum = 0;
	sum += 11 * input[column + 0];
	sum -=  5 * input[column + 1];
	sum -=  4 * input[column - 1];
	sum -=  4 * input[column - 2];
	sum +=  1 * input[column - 3];
	sum +=  1 * input[column - 4];
	sum +=  ROUNDING(sum,8);
	sum = DivideByShift(sum, 3);
	*(highpass_ptr++) = SATURATE(sum);
}

void FilterVerticalTopStrip(PIXEL *input[6], int width, PIXEL *lowpass, PIXEL *highpass)
{
	PIXEL *input_ptr = input[0];
	PIXEL *lowpass_ptr = lowpass;
	PIXEL *highpass_ptr = highpass;
	int column;

	for (column = 0; column < width; column++)
	{
		int32_t sum;

		// Apply the lowpass vertical filter to the horizontal results
		sum  = input[0][column];
		sum += input[1][column];
		*(lowpass_ptr++) = SATURATE(sum);

		// Apply the highpass vertical filter to the horizontal results
		sum  =  5 * input[0][column];
		sum -= 11 * input[1][column];
		sum +=  4 * input[2][column];
		sum +=  4 * input[3][column];
		sum -=  1 * input[4][column];
		sum -=  1 * input[5][column];
		sum += ROUNDING(sum,8);
		sum = DivideByShift(sum, 3);
		*(highpass_ptr++) = SATURATE(sum);
	}
}

void FilterVerticalMiddleStrip(PIXEL *input[6], int width, PIXEL *lowpass, PIXEL *highpass)
{
	//PIXEL *input_ptr = input[2];
	PIXEL *lowpass_ptr = lowpass;
	PIXEL *highpass_ptr = highpass;
	int column;

#if (1 && XMMOPT)
	__m128i *lowpass_group_ptr = (__m128i *)lowpass_ptr;
	__m128i *highpass_group_ptr = (__m128i *)highpass_ptr;

	int column_step = 8;
	int post_column = width - (width % column_step);
#endif

	// Begin processing at the left hand column
	column = 0;

#if (1 && XMMOPT)

	// Process a group of eight pixels at a time
	for (; column < post_column; column += column_step)
	{
		__m128i low_epi16;
		__m128i sum_epi16;
		__m128i sum8_epi16;
		__m128i quad_epi16;
		__m128i mask_epi16;
		__m128i half_epi16;

		// Load the first row of pixels
		quad_epi16 = _mm_load_si128((__m128i *)&input[0][column]);

		// Initialize the highpass filter sum
		sum_epi16 = _mm_setzero_si128();

		// Multiply each pixel by the first filter coefficient and sum the result
		sum_epi16 = _mm_subs_epi16(sum_epi16, quad_epi16);

		// Load the second row of pixels
		quad_epi16 = _mm_load_si128((__m128i *)&input[1][column]);

		// Multiply each pixel by the second filter coefficient and sum the result
		sum_epi16 = _mm_subs_epi16(sum_epi16, quad_epi16);

		// Load the third row of pixels
		quad_epi16 = _mm_load_si128((__m128i *)&input[2][column]);

		// Initialize the lowpass sum
		low_epi16 = quad_epi16;

		// Multiply each pixel by the third filter coefficient and sum the result
		sum8_epi16 = _mm_setzero_si128();
		sum8_epi16 = _mm_adds_epi16(sum8_epi16, quad_epi16);

		// Load the fourth row of pixels
		quad_epi16 = _mm_load_si128((__m128i *)&input[3][column]);

		// Compute the lowpass results
		low_epi16 = _mm_adds_epi16(low_epi16, quad_epi16);

		// Store the lowpass results
		_mm_store_si128(lowpass_group_ptr++, low_epi16);

		// Multiply each pixel by the fourth filter coefficient and sum the result
		sum8_epi16 = _mm_subs_epi16(sum8_epi16, quad_epi16);

		// Load the fifth row of pixels
		quad_epi16 = _mm_load_si128((__m128i *)&input[4][column]);

		// Multiply each pixel by the fifth filter coefficient and sum the result
		sum_epi16 = _mm_adds_epi16(sum_epi16, quad_epi16);

		// Load the sixth (last) row of pixels
		quad_epi16 = _mm_load_si128((__m128i *)&input[5][column]);

		// Multiply each pixel by the sixth filter coefficient and sum the result
		sum_epi16 = _mm_adds_epi16(sum_epi16, quad_epi16);


		half_epi16 = _mm_set1_epi16(4); //was 4
		sum_epi16 = _mm_adds_epi16(sum_epi16, half_epi16); // +7 rounding
		sum_epi16 = _mm_srai_epi16(sum_epi16, 3); // divide 8

		sum_epi16 = _mm_adds_epi16(sum_epi16, sum8_epi16);

		// Store the highpass results
		_mm_store_si128(highpass_group_ptr++, sum_epi16);
	}

	// Should have terminated the fast loop at the post processing column
	assert(column == post_column);

	// Update the pointers to the lowpass and highpass results
	lowpass_ptr = (PIXEL *)lowpass_group_ptr;
	highpass_ptr = (PIXEL *)highpass_group_ptr;

#endif

	for (; column < width; column++)
	{
		int32_t sum;

		// Apply the lowpass vertical filter to the horizontal results
		sum  = input[2][column];
		sum += input[3][column];
		*(lowpass_ptr++) = SATURATE(sum);

		// Apply the highpass vertical filter to the horizontal results
		sum  = -1 * input[0][column];
		sum += -1 * input[1][column];
		sum +=  1 * input[4][column];
		sum +=  1 * input[5][column];
		sum +=	4;
		sum >>= 3;
		sum +=  1 * input[2][column];
		sum += -1 * input[3][column];
		*(highpass_ptr++) = SATURATE(sum);
	}
}



//TODO:  get rid of the global.
extern int g_midpoint_prequant; //HACK


void FilterVerticalMiddleStripQuantBoth(PIXEL *input[6], int width,
										PIXEL *lowpass, PIXEL *highpass,
										int lowpass_quant, int highpass_quant)
{
	//PIXEL *input_ptr = input[2];
	PIXEL *lowpass_ptr = lowpass;
	PIXEL *highpass_ptr = highpass;

	// Change division to multiplication by a fraction
	short lowpass_multiplier = (uint32_t)(1 << 16) / lowpass_quant;
	short highpass_multiplier = (uint32_t)(1 << 16) / highpass_quant;

#if MIDPOINT_PREQUANT
	int lowpass_midpoint = 0;//lowpass_quant/2;
	int highpass_midpoint = 0;//highpass_quant/2;
	if(g_midpoint_prequant >= 2 && g_midpoint_prequant < 9)
	{
		lowpass_midpoint = lowpass_quant / g_midpoint_prequant;
		lowpass_midpoint = lowpass_quant / g_midpoint_prequant;
	}
#endif

#if (1 && XMMOPT)
	__m128i *lowpass_group_ptr = (__m128i *)lowpass_ptr;
	__m128i *highpass_group_ptr = (__m128i *)highpass_ptr;

	int column_step = 8;
	int post_column = width - (width % column_step);
#endif

	// Begin processing at the left hand column
	int column = 0;

#if (1 && XMMOPT)

	// Process a group of eight pixels at a time
	for (; column < post_column; column += column_step)
	{
		__m128i low_epi16;
		__m128i sum_epi16;
		__m128i sum8_epi16;
		__m128i quad_epi16;
		__m128i mask_epi16;
		__m128i half_epi16;
		__m128i sign_epi16;

#if MIDPOINT_PREQUANT
		__m128i lowpass_quant_epi16 = _mm_set1_epi16(lowpass_multiplier);
		__m128i highpass_quant_epi16 = _mm_set1_epi16(highpass_multiplier);

		__m128i lowpass_offset_epi16 = _mm_set1_epi16(lowpass_midpoint);
		__m128i highpass_offset_epi16 = _mm_set1_epi16(highpass_midpoint);
#endif
		// Load the first row of pixels
		quad_epi16 = _mm_load_si128((__m128i *)&input[0][column]);

		// Initialize the highpass filter sum
		sum_epi16 = _mm_setzero_si128();

		// Multiply each pixel by the first filter coefficient and sum the result
		sum_epi16 = _mm_subs_epi16(sum_epi16, quad_epi16);

		// Load the second row of pixels
		quad_epi16 = _mm_load_si128((__m128i *)&input[1][column]);

		// Multiply each pixel by the second filter coefficient and sum the result
		sum_epi16 = _mm_subs_epi16(sum_epi16, quad_epi16);

		// Load the third row of pixels
		quad_epi16 = _mm_load_si128((__m128i *)&input[2][column]);

		// Initialize the lowpass sum
		low_epi16 = quad_epi16;

		// Multiply each pixel by the third filter coefficient and sum the result
		sum8_epi16 = _mm_setzero_si128();
		sum8_epi16 = _mm_adds_epi16(sum8_epi16, quad_epi16);

		// Load the fourth row of pixels
		quad_epi16 = _mm_load_si128((__m128i *)&input[3][column]);

		// Compute the lowpass results
		low_epi16 = _mm_adds_epi16(low_epi16, quad_epi16);

		// Quantize the lowpass results

		// Compute the sign
		//sign_epi16 = _mm_cmplt_epi16(low_epi16, zero_si128);
		sign_epi16 = _mm_cmplt_epi16(low_epi16, _mm_setzero_si128());

		// Compute the absolute value
		low_epi16 = _mm_xor_si128(low_epi16, sign_epi16);
		low_epi16 = _mm_sub_epi16(low_epi16, sign_epi16);

#if MIDPOINT_PREQUANT
		// Add the prequant_midpoint for quantization rounding
		low_epi16 = _mm_add_epi16(low_epi16, lowpass_offset_epi16);
#endif
		// Multiply by the quantization factor
		low_epi16 = _mm_mulhi_epu16(low_epi16, lowpass_quant_epi16);

		// Restore the sign
		low_epi16 = _mm_xor_si128(low_epi16, sign_epi16);
		low_epi16 = _mm_sub_epi16(low_epi16, sign_epi16);

		// Store the lowpass results
		_mm_store_si128(lowpass_group_ptr++, low_epi16);
		//_mm_stream_si128(lowpass_group_ptr++, low_epi16);

		// Multiply each pixel by the fourth filter coefficient and sum the result
		sum8_epi16 = _mm_subs_epi16(sum8_epi16, quad_epi16);

		// Load the fifth row of pixels
		quad_epi16 = _mm_load_si128((__m128i *)&input[4][column]);

		// Multiply each pixel by the fifth filter coefficient and sum the result
		sum_epi16 = _mm_adds_epi16(sum_epi16, quad_epi16);

		// Load the sixth (last) row of pixels
		quad_epi16 = _mm_load_si128((__m128i *)&input[5][column]);

		// Multiply each pixel by the sixth filter coefficient and sum the result
		sum_epi16 = _mm_adds_epi16(sum_epi16, quad_epi16);

		half_epi16 = _mm_set1_epi16(4); //was 4
		sum_epi16 = _mm_adds_epi16(sum_epi16, half_epi16); // +7 rounding
		sum_epi16 = _mm_srai_epi16(sum_epi16, 3); // divide 8

		sum_epi16 = _mm_adds_epi16(sum_epi16, sum8_epi16);

		// Quantize the highpass results

		// Compute the sign
		//sign_epi16 = _mm_cmplt_epi16(sum_epi16, zero_si128);
		sign_epi16 = _mm_cmplt_epi16(sum_epi16, _mm_setzero_si128());

		// Compute the absolute value
		sum_epi16 = _mm_xor_si128(sum_epi16, sign_epi16);
		sum_epi16 = _mm_sub_epi16(sum_epi16, sign_epi16);

#if MIDPOINT_PREQUANT
		// Add the prequant_midpoint for quantization rounding
		sum_epi16 = _mm_add_epi16(sum_epi16, highpass_offset_epi16);
#endif
		// Multiply by the quantization factor
		sum_epi16 = _mm_mulhi_epu16(sum_epi16, highpass_quant_epi16);

		// Restore the sign
		sum_epi16 = _mm_xor_si128(sum_epi16, sign_epi16);
		sum_epi16 = _mm_sub_epi16(sum_epi16, sign_epi16);

		// Store the highpass results
		_mm_store_si128(highpass_group_ptr++, sum_epi16);
		//_mm_stream_si128(highpass_group_ptr++, sum_epi16);
	}

	// Should have terminated the fast loop at the post processing column
	assert(column == post_column);

	// Update the pointers to the lowpass and highpass results
	lowpass_ptr = (PIXEL *)lowpass_group_ptr;
	highpass_ptr = (PIXEL *)highpass_group_ptr;

#endif

	for (; column < width; column++)
	{
		LONGWORD result;
		int32_t sum;

		// Apply the lowpass vertical filter to the horizontal results
		sum  = input[2][column];
		sum += input[3][column];

		// Quantize the lowpass results
		if (sum >= 0)
		{
#if MIDPOINT_PREQUANT
			result.longword = (sum + lowpass_midpoint) * lowpass_multiplier;
#else
			result.longword = sum * lowpass_multiplier;
#endif
			sum = (short)result.halfword.upper;
		}
		else
		{
			sum = NEG(sum);

#if MIDPOINT_PREQUANT
			result.longword = (sum + lowpass_midpoint) * lowpass_multiplier;
#else
			result.longword = sum * lowpass_multiplier;
#endif
			sum = NEG((short)result.halfword.upper);
		}

		// Store the lowpass results
		*(lowpass_ptr++) = SATURATE(sum);

		// Apply the highpass vertical filter to the horizontal results
		sum  = -1 * input[0][column];
		sum += -1 * input[1][column];
		sum +=  1 * input[4][column];
		sum +=  1 * input[5][column];
		sum +=	4;
		sum >>= 3;
		sum +=  1 * input[2][column];
		sum += -1 * input[3][column];

		/// Quantize the highpass results
		if (sum >= 0)
		{
#if MIDPOINT_PREQUANT
			result.longword = (sum + highpass_midpoint) * highpass_multiplier;
#else
			result.longword = sum * highpass_multiplier;
#endif
			sum = (short)result.halfword.upper;
		}
		else
		{
			sum = NEG(sum);

#if MIDPOINT_PREQUANT
			result.longword = (sum + highpass_midpoint) * highpass_multiplier;
#else
			result.longword = sum * highpass_multiplier;
#endif
			sum = NEG((short)result.halfword.upper);
		}

		// Store the highpass results
		*(highpass_ptr++) = SATURATE(sum);
	}
}

void FilterVerticalBottomStrip(PIXEL *input[6], int width, PIXEL *lowpass, PIXEL *highpass)
{
	PIXEL *input_ptr = input[4];
	PIXEL *lowpass_ptr = lowpass;
	PIXEL *highpass_ptr = highpass;
	int column;

	for (column = 0; column < width; column++)
	{
		int32_t sum;

		// Apply the lowpass vertical filter to the horizontal results
		sum  = input[4][column];
		sum += input[5][column];
		*(lowpass_ptr++) = SATURATE(sum);

		// Apply the highpass vertical filter to the horizontal results
		sum  = 11 * input[4][column];
		sum -=  5 * input[5][column];
		sum -=  4 * input[3][column];
		sum -=  4 * input[2][column];
		sum +=  1 * input[1][column];
		sum +=  1 * input[0][column];
		sum +=  ROUNDING(sum,8);
		sum = DivideByShift(sum, 3);
		*(highpass_ptr++) = SATURATE(sum);
	}
}

/***** Routines for the transform state *****/

void InitTransformState(TRANSFORM_STATE *state, TRANSFORM *transform)
{
	int row;
	int band;

	assert(state != NULL);
	if (! (state != NULL)) return;

#if 0

	state->num_processed = 0;
	state->width = 0;
	state->height = 0;
	state->level = 0;
	state->num_rows = 0;

	for (row = 0; row < NUM_WAVELET_ROWS; row++)
	{
		state->buffers.spatial.lowpass[row] = NULL;
		state->buffers.spatial.highpass[row] = NULL;
	}

	for (band = 0; band < NUM_WAVELET_BANDS; band++)
	{
		state->buffers.spatial.output[band] = NULL;
	}

	state->buffers.interlaced.lowpass = NULL;
	state->buffers.interlaced.highpass = NULL;

	state->buffers.interlaced.lowlow = NULL;
	state->buffers.interlaced.lowhigh = NULL;

	state->buffers.interlaced.highlow = NULL;
	state->buffers.interlaced.highhigh = NULL;

	// Note: Can replace the individual initializers with a single
	// call to a routine that clears the entire union of buffers

#else

	// Clear the entire state structure at once
	ZeroMemory(state, sizeof(TRANSFORM_STATE));

#endif

	// Remember the transform that contains this level in the recursion
	state->transform = transform;
}

void ClearTransformState(TRANSFORM_STATE *state)
{
};

#if 0
void SetTransformStateQuantization(TRANSFORM *transform)
{
	int level;

	for (level = 0; level < transform->num_wavelets; level++)
	{
		IMAGE *wavelet = transform->wavelet[level];
		TRANSFORM_STATE *state = &transform->state[level];
		int band;

		if (wavelet == NULL) break;

		for (band = 0; band < wavelet->num_bands; band++)
		{
			state->quant[band] = wavelet->quant[band];
		}
	}
}
#endif

void SetTransformDescriptors(ENCODER *encoder, TRANSFORM *transform)
{
	TRANSFORM_DESCRIPTOR *descriptor_array = transform->descriptor;
	TRANSFORM_DESCRIPTOR *descriptor;
	int gop_length = encoder->gop_length;
	int num_spatial = encoder->num_spatial;
	int num_wavelets = transform->num_wavelets;
	int progressive = encoder->progressive;
	int index = 0;
	int type;

	// The transform parameters should have already been initialized
	assert(transform->num_frames == encoder->gop_length);
	assert(transform->num_levels == encoder->num_levels);
	assert(transform->num_spatial == encoder->num_spatial);

	// The transforms in the first level depend on the input format
	type = progressive ? TRANSFORM_FILTER_SPATIAL : TRANSFORM_FILTER_INTERLACED;
	for (; index < gop_length; index++)
	{
		descriptor = &descriptor_array[index];
		memset(descriptor, 0, sizeof(TRANSFORM_DESCRIPTOR));
		//descriptor->type = TRANSFORM_FILTER_UNSPECIFIED;
		descriptor->type = type;
	}

	// The sequence of transforms is determined by the GOP length
	if (gop_length == 1)
	{
		assert(num_wavelets == num_spatial + 1);

		for (; index < num_wavelets; index++)
		{
			descriptor = &descriptor_array[index];
			memset(descriptor, 0, sizeof(TRANSFORM_DESCRIPTOR));
			descriptor->type = TRANSFORM_FILTER_SPATIAL;

			// The wavelet is computed from the lowpass band in the previous wavelet
			descriptor->wavelet1 = index - 1;
			descriptor->band1 = LOWPASS_BAND;
		}
	}
	else
	{
		assert(gop_length == 2);
		assert(num_spatial == 3);
		assert(num_wavelets == (gop_length + num_spatial + 1));

		// The transform at the second level is a temporal transform
		descriptor = &descriptor_array[index];
		memset(descriptor, 0, sizeof(TRANSFORM_DESCRIPTOR));
		descriptor->type = TRANSFORM_FILTER_TEMPORAL;

		// The temporal wavelet is computed from the first two wavelets
		descriptor->wavelet1 = 0;
		descriptor->band1 = LOWPASS_BAND;

		descriptor->wavelet2 = 1;
		descriptor->band2 = LOWPASS_BAND;

		// Advance to the spatial wavelets above the temporal highpass band
		index++;

		// One spatial wavelet above the temporal highpass band
		descriptor = &descriptor_array[index];
		memset(descriptor, 0, sizeof(TRANSFORM_DESCRIPTOR));
		descriptor->type = TRANSFORM_FILTER_SPATIAL;

		// The wavelet is computed from the highass band in the temporal wavelet
		descriptor->wavelet1 = 2;
		descriptor->band1 = HIGHPASS_BAND;

		// Advance to the first spatial wavelet above the temporal lowpass band
		num_spatial--;
		index++;

		descriptor = &descriptor_array[index];
		memset(descriptor, 0, sizeof(TRANSFORM_DESCRIPTOR));
		descriptor->type = TRANSFORM_FILTER_SPATIAL;

		// The wavelet is computed from the lowass band in the temporal wavelet
		descriptor->wavelet1 = 2;
		descriptor->band1 = LOWPASS_BAND;

		// Advance to the second spatial wavelet above the temporal lowpass band
		index++;

		// The rest of the wavelets are spatial wavelets above the temporal lowpass band
		for (; index < num_wavelets; index++)
		{
			descriptor = &descriptor_array[index];
			memset(descriptor, 0, sizeof(TRANSFORM_DESCRIPTOR));
			descriptor->type = TRANSFORM_FILTER_SPATIAL;

			// The wavelet is computed from the lowpass band in the previous wavelet
			descriptor->wavelet1 = index - 1;
			descriptor->band1 = LOWPASS_BAND;
		}
	}
}

// Allocate processing buffers using the input level and input dimensions
BYTE *AllocateTransformStateBuffers(TRANSFORM_STATE *state, int width, int height,
									int level, int type, BYTE *buffer)
{
	// Allocate processing buffers from the space passed as an argument
	BYTE *bufptr = buffer;

	// Set the dimensions and level of the output wavelet
	state->width = width / 2;
	state->height = height / 2;
	state->level = level + 1;

	state->num_processed = 0;
	state->num_rows = 0;

	if (type == TRANSFORM_FILTER_SPATIAL)
	{
		// Allocate six rows of lowpass and highpass horizontal results
		bufptr = AllocateHorizontalBuffers(bufptr, width, state->buffers.spatial.lowpass, state->buffers.spatial.highpass);

		// Allocate four buffers for the wavelet bands
		bufptr = AllocateWaveletBandRows(bufptr, state->width, state->buffers.spatial.output);
	}

	else if (type == TRANSFORM_FILTER_INTERLACED)
	{
		bufptr = AllocateInterlacedBuffers(state, bufptr, width);
	}
	else
	{
		// Unsupported transform
		assert(0);
	}

	return bufptr;
}

// Called with the next row to perform the recursive wavelet transform
void FilterSpatialRecursiveRow(TRANSFORM_STATE *state, PIXEL *input, int width, BYTE *buffer, FILE *logfile)
{
	// Allocate scratch memory as needed
	BYTE *bufptr = buffer;

	// There should be enough space in the processing buffers for the new row
	assert(state->num_rows < 6);

	// The input width should be twice the wavelet band width
	assert(width == (2 * state->width));

#if (0 && DEBUG)
	if (logfile) {
		size_t row_size = width * sizeof(PIXEL);
		indentf(logfile, state->level);
		fprintf(logfile, "Horizontal transform size: %d\n", row_size);
	}
#endif

	// Apply the horizontal transform to the new row
	//FilterSpatialHorizontalRow(input, width, state->buffers.spatial.lowpass[state->num_rows], state->buffers.spatial.highpass[state->num_rows]);
	FilterHorizontalRow16s(input, state->buffers.spatial.lowpass[state->num_rows], state->buffers.spatial.highpass[state->num_rows], width);
	state->num_rows++;

	// Enough rows to apply the vertical transform?
	assert(state->num_rows <= 6);
	if (state->num_rows == 6)
	{
		// Perform the vertical transform

		// Is this the first output row?
		if (state->num_processed == 0)
		{
			/***** Use the vertical transform for the top row *****/

#if (0 && DEBUG)
			if (logfile) {
				size_t row_size = state->width * sizeof(PIXEL);
				indentf(logfile, state->level);
				fprintf(logfile, "Top left vertical transform size: %d\n", row_size);
			}
#endif
			// Apply the vertical transform to the lowpass horizontal results
			FilterVerticalTopStrip(state->buffers.spatial.lowpass, state->width, state->buffers.spatial.output[0], state->buffers.spatial.output[2]);

#if (0 && DEBUG)
			if (logfile) {
				size_t row_size = state->width * sizeof(PIXEL);
				indentf(logfile, state->level);
				fprintf(logfile, "Top right vertical transform size: %d\n", row_size);
			}
#endif
			// Apply the vertical transform to the highpass horizontal results
			FilterVerticalTopStrip(state->buffers.spatial.highpass, state->width, state->buffers.spatial.output[1], state->buffers.spatial.output[3]);

			// Deliver the lowpass (band zero) output row to the next level
			//FilterSpatialRecursiveAux(state->transform, state->buffers.spatial.output[0], state->width, state->level, bufptr);
			FilterRecursive(state->transform, state->buffers.spatial.output[0], state->width, state->level, bufptr);

			// Finished processing the first two input rows (one output row)
			state->num_processed++;

			// Store the new output row in each wavelet band
			//OutputWaveletBandRows(state->transform, state->buffers.spatial.output, state->width, state->level);
			StoreWaveletBandRows(state->transform, state->buffers.spatial.output, state->width, state->level);
		}

		/***** Use the vertical transform for the middle rows *****/

#if (0 && DEBUG)
		if (logfile) {
			size_t row_size = state->width * sizeof(PIXEL);
			indentf(logfile, state->level);
			fprintf(logfile, "Middle left vertical transform size: %d\n", row_size);
		}
#endif
		// Apply the vertical transform to the lowpass horizontal results
		//FilterVerticalMiddleStrip(state->buffers.spatial.lowpass, state->width, state->buffers.spatial.output[0], state->buffers.spatial.output[2]);
		FilterVerticalMiddleStrip(state->buffers.spatial.lowpass, state->width, state->buffers.spatial.lowpass[0], state->buffers.spatial.lowpass[1]);

#if (0 && DEBUG)
		if (logfile) {
			size_t row_size = state->width * sizeof(PIXEL);
			indentf(logfile, state->level);
			fprintf(logfile, "Middle right vertical transform size: %d\n", row_size);
		}
#endif
		// Apply the vertical transform to the highpass horizontal results
		//FilterVerticalMiddleStrip(state->buffers.spatial.highpass, state->width, state->buffers.spatial.output[1], state->buffers.spatial.output[3]);
#if 1
		FilterVerticalMiddleStrip(state->buffers.spatial.highpass, state->width, state->buffers.spatial.highpass[0], state->buffers.spatial.highpass[1]);
#else
		FilterVerticalMiddleStripQuantBoth(state->buffers.spatial.highpass, state->width,
										   state->buffers.spatial.highpass[0], state->buffers.spatial.highpass[1],
										   state->quant[1], state->quant[3]);
#endif
		// Deliver the lowpass (band zero) output row to the next level
		//FilterSpatialRecursiveAux(state->transform, state->buffers.spatial.output[0], state->width, state->level, bufptr);
		//FilterSpatialRecursiveAux(state->transform, state->buffers.spatial.lowpass[0], state->width, state->level, bufptr);
		FilterRecursive(state->transform, state->buffers.spatial.lowpass[0], state->width, state->level, bufptr);

		// Finished processing the two input rows (one output row)
		state->num_processed++;

		// Store the new output row in each wavelet band
		//OutputWaveletBandRows(state->transform, state->buffers.spatial.output, state->width, state->level);
		//StoreWaveletBandRows(state->transform, state->buffers.spatial.output, state->width, state->level);
		StoreWaveletHighpassRows(state->transform,
								 state->buffers.spatial.lowpass[0], state->buffers.spatial.highpass[0],
								 state->buffers.spatial.lowpass[1], state->buffers.spatial.highpass[1],
								 state->width, state->level);

		// Is this the last output row?
		if (state->num_processed == state->height - 1)
		{
			/***** Use the vertical transform for the bottom row *****/

#if (0 && DEBUG)
			if (logfile) {
				size_t row_size = state->width * sizeof(PIXEL);
				indentf(logfile, state->level);
				fprintf(logfile, "Bottom left vertical transform size: %d\n", row_size);
			}
#endif
			// Apply the vertical transform to the lowpass horizontal results
			FilterVerticalBottomStrip(state->buffers.spatial.lowpass, state->width, state->buffers.spatial.output[0], state->buffers.spatial.output[2]);

#if (0 && DEBUG)
			if (logfile) {
				size_t row_size = state->width * sizeof(PIXEL);
				indentf(logfile, state->level);
				fprintf(logfile, "Bottom right vertical transform size: %d\n", row_size);
			}
#endif
			// Apply the vertical transform to the highpass horizontal results
			FilterVerticalBottomStrip(state->buffers.spatial.highpass, state->width, state->buffers.spatial.output[1], state->buffers.spatial.output[3]);

			// Deliver the lowpass (band zero) output row to the next level
			//FilterSpatialRecursiveAux(state->transform, state->buffers.spatial.output[0], state->width, state->level, bufptr);
			FilterRecursive(state->transform, state->buffers.spatial.output[0], state->width, state->level, bufptr);

			// Finished processing the two input rows (one output row)
			state->num_processed++;

			// Store the new output row in each wavelet band
			//OutputWaveletBandRows(state->transform, state->buffers.spatial.output, state->width, state->level);
			StoreWaveletBandRows(state->transform, state->buffers.spatial.output, state->width, state->level);
		}
		else
		{
			// Shift the horizontal result buffers by two rows
			ShiftHorizontalBuffers(state->buffers.spatial.lowpass, state->buffers.spatial.highpass);
			state->num_rows -= 2;
		}
	}
}

// Called with the next two rows to perform the interlaced transform
void FilterInterlacedRecursiveStrip(TRANSFORM_STATE *state, PIXEL *row1, PIXEL *row2,
									int width, BYTE *buffer, FILE *logfile)
{
	// Allocate scratch memory as needed
	BYTE *bufptr = buffer;

	// The input width should be twice the wavelet band width
	assert(width == (2 * state->width));

	// Check that the buffers for the interlaced transform results have been allocated
	assert(state->buffers.interlaced.lowpass != NULL);
	assert(state->buffers.interlaced.highpass != NULL);

	assert(state->buffers.interlaced.lowlow != NULL);
	assert(state->buffers.interlaced.lowhigh != NULL);

	assert(state->buffers.interlaced.highlow != NULL);
	assert(state->buffers.interlaced.highhigh != NULL);

#if (0 && DEBUG)
	if (logfile) {
		size_t row_size = width * sizeof(PIXEL);
		indentf(logfile, state->level);
		fprintf(logfile, "Interlaced transform size: %d\n", row_size);
	}
#endif

	// Apply the interlaced transform to the new pair of rows
	FilterTemporalRow16s(row1, row2, width, state->buffers.interlaced.lowpass, state->buffers.interlaced.highpass, 0);

	// Apply the horizontal transform to the temporal lowpass result
	FilterHorizontalRow16s(state->buffers.interlaced.lowpass, state->buffers.interlaced.lowlow, state->buffers.interlaced.lowhigh, width);

	// Apply the horizontal transform to the temporal highpass result
	FilterHorizontalRow16s(state->buffers.interlaced.highpass, state->buffers.interlaced.highlow, state->buffers.interlaced.highhigh, width);

	// Deliver the lowpass (band zero) output row to the next level
	FilterRecursive(state->transform, state->buffers.interlaced.lowlow, state->width, state->level, bufptr);

	// Finished processing the two input rows (one output row)
	state->num_processed++;

	// Store the new output row in each wavelet band
	StoreWaveletHighpassRows(state->transform,
							 state->buffers.interlaced.lowlow, state->buffers.interlaced.lowhigh,
							 state->buffers.interlaced.highlow, state->buffers.interlaced.highhigh,
							 state->width, state->level);
}

// Called with the next row from the first frame to perform the recursive wavelet transform
void FilterTemporalRecursiveAux(TRANSFORM_STATE *state, int frame_index, BYTE *buffer, FILE *logfile)
{

	// This routine should be called to determine the first row of input pixels
	assert(frame_index == 0);

	// Save the next input row from the first frame
	state->buffers.temporal.input1 = state->buffers.temporal.input_row_ptr;

	state->buffers.temporal.input_row_ptr += state->buffers.temporal.input_row_pitch;




}

// Called with the next row from the second frame to perform the recursive wavelet transform
void FilterTemporalRecursiveRow(TRANSFORM_STATE *state, PIXEL *input, int width,
								int frame_index, BYTE *buffer, FILE *logfile)
{
	// Allocate scratch memory as needed
	BYTE *bufptr = buffer;

	PIXEL *lowpass_buffer = state->buffers.temporal.lowpass;
	PIXEL *highpass_buffer = state->buffers.temporal.highpass;

	PIXEL *input1 = state->buffers.temporal.input1;
	PIXEL *input2 = input;

	// NOTE: Move the scratch memory allocation to the allocation routine for the transform state

	// This routine should be called with the second row of input pixels
	assert(frame_index == 1);

	// Allocate a buffer for the temporal lowpass result
	bufptr = AllocateInputBuffer(bufptr, width, &lowpass_buffer);

	// Allocate a buffer for the temporal highpass result
	bufptr = AllocateInputBuffer(bufptr, width, &highpass_buffer);

	// Apply the temporal transform
	FilterTemporalRow16s(input1, input2, width, lowpass_buffer, highpass_buffer, 0);

	// Deliver the highpass output row to the next level
	FilterRecursive(state->transform, highpass_buffer, state->width, state->level, bufptr);

	// Deliver the lowpass output row to the next level
	FilterRecursive(state->transform, lowpass_buffer, state->width, state->level + 1, bufptr);
}

#if 0
void InitEncoder(ENCODER *encoder, FILE *logfile, int width, int height, int num_levels, BYTE *buffer)
{
	BYTE *bufptr = buffer;

	assert(encoder != NULL);
	if (! (encoder != NULL)) return;

	encoder->logfile = logfile;
	encoder->width = width;
	encoder->height = height;
	encoder->num_levels = num_levels;

	// Initialize the encoder state at each wavelet level
	for (int level = 0; level < num_levels; level++)
	{
		InitEncoderState(&encoder->state[level], encoder);
	}

	//ZeroMemory(encoder->output, sizeof(encoder->output));
	bufptr = AllocateRecursiveEncoder(encoder, width, height, num_levels, bufptr);

	// Allocate buffers for saving the wavelet bands (for debugging)
	AllocateRecursiveWavelets(encoder, bufptr);
}
#endif


BYTE *AllocateRecursiveTransform(TRANSFORM *transform, int width, int height, int num_levels, BYTE *buffer)
{
	BYTE *bufptr = buffer;
	int level;

	// Allocate a buffer for the input row
	//bufptr = AllocateInputBuffer(bufptr, width, &transform->row_buffer);

	// Allocate processing buffers at each wavelet level
	for (level = 0; level < num_levels; level++)
	{
		int type = transform->descriptor[level].type;

		// Allocate processing buffers for the wavelet at this level
		bufptr = AllocateTransformStateBuffers(&transform->state[level],
											   width, height,
											   level, type, bufptr);

		// Reduce the input dimensions for the wavelet at the next level
		width /= 2;
		height /= 2;
	}

	return bufptr;
}

#if 0
void AllocateRecursiveWavelets(ENCODER *encoder, BYTE *buffer)
{
	BYTE *bufptr = buffer;
	int level;

	int width = encoder->input.width;
	int height = encoder->input.height;

	// Allocate buffers for saving the wavelets at each level
	for (level = 0; level < encoder->num_levels; level++)
	{
		size_t band_size;
		int band;

		width /= 2;
		height /= 2;

		band_size = ALIGN16(width * height * sizeof(PIXEL));

		for (band = 0; band < NUM_WAVELET_BANDS; band++)
		{
			encoder->output[level][band] = (PIXEL *)bufptr;
			encoder->output_ptr[level][band] = (PIXEL *)bufptr;
			bufptr += band_size;
		}
	}
}
#endif

void InitializeRecursiveOutput(TRANSFORM *transform)
{
	int index;

	for (index = 0; index < transform->num_wavelets; index++)
	{
		IMAGE *wavelet = transform->wavelet[index];
		int band;

		if (wavelet == NULL) break;

		// Initialize the pointers to the start of each wavelet band
		for (band = 0; band < wavelet->num_bands; band++)
		{
			transform->rowptr[index][band] = wavelet->band[band];
		}
	}
}

// Entry point for the intermediate levels in the recursive wavelet transform
void FilterSpatialRecursive(TRANSFORM *transform, PIXEL *image, int width, int height, int pitch, BYTE *buffer)
{
	PIXEL *rowptr = image;
	size_t row_size = width * sizeof(PIXEL);
	BYTE *bufptr = buffer;
	//FILE *logfile = transform->logfile;
	int row;

	pitch /= sizeof(PIXEL);

	// Pass each row into the recursive wavelet transform
	for (row = 0; row < height; row++)
	{
		// Load the next input row into the row buffer
#if (0 && DEBUG)
		if (logfile) {
			indentf(logfile, 0);
			fprintf(logfile, "Loading row size: %d\n", row_size);
		}
#endif
#if 0
		CopyMemory(transform->row_buffer, rowptr, row_size);
		rowptr += pitch;

		// Apply the wavelet transform to this row
		FilterSpatialRecursiveRow(&transform->state[0], transform->row_buffer, width, bufptr, NULL);
#else
		// Apply the wavelet transform to this row
		FilterSpatialRecursiveRow(&transform->state[0], rowptr, width, bufptr, NULL);
		rowptr += pitch;
#endif
	}
}

//void FilterSpatialRecursiveAux(TRANSFORM *transform, PIXEL *input, int width, int level, BYTE *buffer)
void FilterRecursive(TRANSFORM *transform, PIXEL *input, int width, int level, BYTE *buffer)
{
	int frame_index = 0;

	assert(transform != NULL);
	if (! (transform != NULL)) return;

	// Terminate the recursion at the highest level in the wavelet tree
	if (level == transform->num_levels) return;

#if (0 && DEBUG)
	if (m_logfile) {
		size_t row_size = width * sizeof(PIXEL);
		int next_level = level + 1;
		indentf(m_logfile, next_level);
		fprintf(m_logfile, "Applying forward transform to row size: %d\n", row_size);
	}
#endif

	// Apply the wavelet transform for the next level in the wavelet tree
	switch (transform->descriptor[level].type)
	{
	case TRANSFORM_FILTER_SPATIAL:
		FilterSpatialRecursiveRow(&transform->state[level], input, width, buffer, NULL);
		break;

	case TRANSFORM_FILTER_TEMPORAL:
		// Pass the row from the first frame to the temporal transform
		frame_index = transform->descriptor[level].wavelet1;
		FilterTemporalRecursiveAux(&transform->state[level], frame_index, buffer, NULL);

		// Pass the row from the second frame to the temporal transform
		frame_index = transform->descriptor[level].wavelet2;
		FilterTemporalRecursiveRow(&transform->state[level], input, width, frame_index, buffer, NULL);
		break;

	default:
		assert(0);
		break;
	}
}

#if 0
void OutputWaveletBandRows(ENCODER *encoder, PIXEL *wavelet[4], int width, int level)
{
	int index = level - 1;
	int band;

	assert(index >= 0);

	assert(encoder != NULL);
	if (! (encoder != NULL)) return;

#if (0 && DEBUG)
	if (encoder->logfile && level == encoder->num_levels)
	{
		FILE *logfile = encoder->logfile;
		indentf(logfile, level);
		fprintf(logfile, "Storing lowpass row at the final level\n");
	}
#endif

	for (band = 0; band < 4; band++)
	{
		PIXEL *input_ptr = wavelet[band];
		PIXEL *output_ptr = encoder->output_ptr[index][band];
		FILE *logfile = encoder->logfile;
		int column;

		assert(input_ptr != NULL && output_ptr != NULL);
#if (0 && DEBUG)
		if (logfile)
		{
			FILE *logfile = encoder->logfile;
			indentf(logfile, level);
			if (band > 0) {
				fprintf(logfile, "Storing highpass band %d row for level %d\n", band, level);
			}
			else {
				fprintf(logfile, "Storing lowpass band %d row for level %d\n", band, level);
			}
		}
#endif
		for (column = 0; column < width; column++)
		{
			if (logfile) fprintf(logfile, "%5d", *input_ptr);
			*(output_ptr++) = *(input_ptr++);
		}
#if (0 && DEBUG)
		if (logfile) fprintf(logfile, "\n");
#endif
		encoder->output_ptr[index][band] = output_ptr;
	}
}
#endif

void StoreWaveletBandRows(TRANSFORM *transform, PIXEL *result[4], int width, int level)
{
	int index = level - 1;
	int band;

	// Get the pitch for updating the row pointers into the wavelet bands
	IMAGE *wavelet = transform->wavelet[index];
	int num_bands = wavelet->num_bands;
	int pitch = wavelet->pitch / sizeof(PIXEL);

	assert(index >= 0);

	assert(transform != NULL);
	if (! (transform != NULL)) return;

#if (0 && DEBUG)
	if (transform->logfile && level == transform->num_levels)
	{
		FILE *logfile = transform->logfile;
		indentf(logfile, level);
		fprintf(logfile, "Storing lowpass row at the final level\n");
	}
#endif

	for (band = 0; band < num_bands; band++)
	{
#if 0
		PIXEL *input_ptr = result[band];
		PIXEL *output_ptr = transform->rowptr[index][band];
		//FILE *logfile = transform->logfile;
		int column;

		assert(input_ptr != NULL && output_ptr != NULL);
#if (0 && DEBUG)
		if (logfile)
		{
			FILE *logfile = transform->logfile;
			indentf(logfile, level);
			if (band > 0) {
				fprintf(logfile, "Storing highpass band %d row for level %d\n", band, level);
			}
			else {
				fprintf(logfile, "Storing lowpass band %d row for level %d\n", band, level);
			}
		}
#endif
		for (column = 0; column < width; column++)
		{
			//if (logfile) fprintf(logfile, "%5d", *input_ptr);
			*(output_ptr++) = *(input_ptr++);
		}
#else
		//if (wavelet->quant[band] > 1)
		if (band > 0)
		{
			// Quantize the transform results
			QuantizeRow16sTo16s(result[band], transform->rowptr[index][band], width, wavelet->quant[band]);
		}
		else if (level == transform->num_levels)
		{
			size_t row_size = width * sizeof(PIXEL);
			CopyMemory(transform->rowptr[index][band], result[band], row_size);
		}

		// Record the quantization that was applied to the band
		wavelet->quantization[band] = wavelet->quant[band];

		// Record the pixel type
		wavelet->pixel_type[band] = PIXEL_TYPE_16S;
#endif

		//if (logfile) fprintf(logfile, "\n");

		//transform->rowptr[index][band] = output_ptr;

		// Advance to the next row in the wavelet band
		transform->rowptr[index][band] += pitch;
	}
}

void StoreWaveletHighpassRows(TRANSFORM *transform,
							  PIXEL *lowlow_result, PIXEL *lowhigh_result,
							  PIXEL *highlow_result, PIXEL *highhigh_result,
							  int width, int level)
{
	int index = level - 1;
	int band;

	// Get the pitch for updating the row pointers into the wavelet bands
	IMAGE *wavelet = transform->wavelet[index];
	int num_bands = wavelet->num_bands;
	int pitch = wavelet->pitch / sizeof(PIXEL);

	assert(index >= 0);

	assert(transform != NULL);
	if (! (transform != NULL)) return;

#if (0 && DEBUG)
	if (transform->logfile && level == transform->num_levels)
	{
		FILE *logfile = transform->logfile;
		indentf(logfile, level);
		fprintf(logfile, "Storing lowpass row at the final level\n");
	}
#endif

	// Is this the highest level in the transform?
	if (level == transform->num_levels)
	{
		// Store the lowpass result in the wavelet
		size_t row_size = width * sizeof(PIXEL);
		CopyMemory(transform->rowptr[index][0], lowlow_result, row_size);
	}

	// Quantize and store the lowhigh band
#if 1
	QuantizeRow16sTo16s(lowhigh_result, transform->rowptr[index][1], width, wavelet->quant[1]);
#else
	{
		size_t row_size = width * sizeof(PIXEL);
		CopyMemory(transform->rowptr[index][1], lowhigh_result, row_size);
	}
#endif

	// Quantize and store the highlow band
	QuantizeRow16sTo16s(highlow_result, transform->rowptr[index][2], width, wavelet->quant[2]);

	// Quantize and store the highhigh band
#if 1
	QuantizeRow16sTo16s(highhigh_result, transform->rowptr[index][3], width, wavelet->quant[3]);
#else
	{
		size_t row_size = width * sizeof(PIXEL);
		CopyMemory(transform->rowptr[index][3], highhigh_result, row_size);
	}
#endif

	// Record the quantization that was applied to each band
	for (band = 0; band < num_bands; band++)
	{
		wavelet->quantization[band] = wavelet->quant[band];

		// Record the pixel type
		wavelet->pixel_type[band] = PIXEL_TYPE_16S;

		// Advance to the next row in the wavelet band
		transform->rowptr[index][band] += pitch;
	}
}

// Perform the full intra frame transform on a progressive frame using recursive wavelets
void TransformForwardProgressiveIntraFrameRecursive(ENCODER *encoder, IMAGE *image,
													TRANSFORM *transform, int channel,
													BYTE *buffer, size_t buffer_size)
{
	int width = transform->width;
	int height = transform->height;
	int num_levels = transform->num_levels;

	PIXEL *data;
	int pitch;

	// Memory for the recursive transforms is allocated from scratch space
	BYTE *bufptr = buffer;

	assert(image != NULL);
	if (! (image != NULL)) return;

	assert(buffer != NULL);
	if (! (buffer != NULL)) return;

#if _DEBUG
	// Write transform debug output to the encoder logfile
	transform->logfile = encoder->logfile;
#endif

#if (1 && DEBUG)
	if (transform->logfile) {
		fprintf(transform->logfile, "Calling recursive transform for channel: %d\n", channel);
	}
#endif

	// Allocate all of the buffers required for the recursive transform
	bufptr = AllocateRecursiveTransform(transform, width, height, num_levels, bufptr);

	// Initialize the pointers into the wavelet bands
	InitializeRecursiveOutput(transform);

	START(tk_recursive);

	// Apply the recursive wavelet transform
	data = image->band[0];
	pitch = image->pitch;
	FilterSpatialRecursive(transform, data, width, height, pitch, bufptr);

	STOP(tk_recursive);
}

// Perform the full intra frame recursive transform on a progressive frame of packed YUV
void TransformForwardProgressiveIntraFrameRecursiveYUV(ENCODER *encoder, BYTE *frame,
													   int width, int height, int pitch,
													   TRANSFORM *transform_array[], int num_transforms,
													   BYTE *buffer, size_t buffer_size)
{
	BYTE *rowptr = frame;

	PIXEL *unpacked_buffer[3];

	// Memory for the recursive transforms is allocated from scratch space
	BYTE *bufptr = buffer;

	int channel;
	int row;

	assert(frame != NULL);
	if (! (frame != NULL)) return;

	assert(buffer != NULL);
	if (! (buffer != NULL)) return;

	// Allocate buffers for the wavelet transforms
	for (channel = 0; channel < num_transforms; channel++)
	{
		TRANSFORM *transform = transform_array[channel];

		int channel_width = transform->width;
		int channel_height = transform->height;
		int num_levels = transform->num_levels;

		// Allocate a buffer for the unpacked pixels
		bufptr = AllocateInputBuffer(bufptr, channel_width, &unpacked_buffer[channel]);

		// Allocate all of the buffers required for the recursive transform
		bufptr = AllocateRecursiveTransform(transform, channel_width, channel_height, num_levels, bufptr);

		// Initialize the pointers into the wavelet bands
		InitializeRecursiveOutput(transform);

#if _DEBUG
		// Write transform debug output to the encoder logfile
		transform->logfile = encoder->logfile;
#endif
	}

	START(tk_recursive);

	// Process each row in the input frame
	for (row = 0; row < height; row++)
	{
		// Unpack the row in the input frame
		UnpackYUVRow16s(rowptr, width, unpacked_buffer);

		// Apply the wavelet transform to each channel
		for (channel = 0; channel < num_transforms; channel++)
		{
			TRANSFORM *transform = transform_array[channel];
			int channel_width = transform->width;

#if (1 && DEBUG)
			if (transform->logfile) {
				fprintf(transform->logfile,
						"Calling recursive transform, row: %d, channel: %d\n",
						row, channel);
			}
#endif
			// Apply the wavelet transform to this row
			FilterSpatialRecursiveRow(&transform->state[0], unpacked_buffer[channel], channel_width, bufptr, NULL);
		}

		// Advance to the next row in the input frame
		rowptr += pitch;
	}

	STOP(tk_recursive);
}

// Perform the full intra frame recursive transform on an interlaced frame of packed YUV
void TransformForwardInterlacedIntraFrameRecursiveYUV(ENCODER *encoder, BYTE *frame,
													  int width, int height, int pitch,
													  TRANSFORM *transform_array[], int num_transforms,
													  BYTE *buffer, size_t buffer_size)
{
	BYTE *rowptr = frame;

	PIXEL *unpacked_buffer1[3];
	PIXEL *unpacked_buffer2[3];

	// Memory for the recursive transforms is allocated from scratch space
	BYTE *bufptr = buffer;

	int channel;
	int row;

	assert(frame != NULL);
	if (! (frame != NULL)) return;

	assert(buffer != NULL);
	if (! (buffer != NULL)) return;

	// Allocate buffers for the wavelet transforms
	for (channel = 0; channel < num_transforms; channel++)
	{
		TRANSFORM *transform = transform_array[channel];

		int channel_width = transform->width;
		int channel_height = transform->height;
		int num_levels = transform->num_levels;

		// Allocate buffers for the unpacked pixels
		bufptr = AllocateInputBuffer(bufptr, channel_width, &unpacked_buffer1[channel]);
		bufptr = AllocateInputBuffer(bufptr, channel_width, &unpacked_buffer2[channel]);

		// Allocate all of the buffers required for the recursive transform
		bufptr = AllocateRecursiveTransform(transform, channel_width, channel_height, num_levels, bufptr);

		// Initialize the pointers into the wavelet bands
		InitializeRecursiveOutput(transform);

#if _DEBUG
		// Write transform debug output to the encoder logfile
		transform->logfile = encoder->logfile;
#endif
	}

	START(tk_recursive);

	// Process each pair of rows in the input frame
	for (row = 0; row < height; row += 2)
	{
		// Unpack the first row in the input frame
		UnpackYUVRow16s(rowptr, width, unpacked_buffer1);
		rowptr += pitch;

		// Unpack the second row in the input frame
		UnpackYUVRow16s(rowptr, width, unpacked_buffer2);
		rowptr += pitch;

		// Apply the wavelet transform to each channel
		for (channel = 0; channel < num_transforms; channel++)
		{
			TRANSFORM *transform = transform_array[channel];
			int channel_width = transform->width;

#if (1 && DEBUG)
			if (transform->logfile) {
				fprintf(transform->logfile,
						"Calling recursive transform, row: %d, channel: %d\n",
						row, channel);
			}
#endif
			// Apply the wavelet transform to this row
			FilterInterlacedRecursiveStrip(&transform->state[0], unpacked_buffer1[channel], unpacked_buffer2[channel],
										   channel_width, bufptr, NULL);
		}
	}

	STOP(tk_recursive);
}

void TransformForwardProgressiveInterFrameRecursiveYUV(ENCODER *encoder, BYTE *frame, int frame_index,
													   int width, int height, int pitch,
													   TRANSFORM *transform_array[], int num_transforms,
													   BYTE *buffer, size_t buffer_size)
{

}


void TransformForwardInterlacedInterFrameRecursiveYUV(ENCODER *encoder, BYTE *frame, int frame_index,
													  int width, int height, int pitch,
													  TRANSFORM *transform_array[], int num_transforms,
													  BYTE *buffer, size_t buffer_size)
{

}

// Perform a recursive transform on a packed YUV frame
void TransformForwardRecursiveYUYV(ENCODER *encoder, BYTE *frame, int frame_index,
								   int width, int height, int pitch,
								   TRANSFORM *transform_array[], int num_transforms,
								   BYTE *buffer, size_t buffer_size)
{
	int gop_length = encoder->gop_length;
	int progressive = encoder->progressive;

	if (progressive && gop_length == 1)
	{
		assert(frame_index == 0);

		// Apply the progressive intra frame transform to the packed YUV frame
		TransformForwardProgressiveIntraFrameRecursiveYUV(encoder, frame, width, height, pitch,
														  transform_array, num_transforms,
														  buffer, buffer_size);
	}
	else if (!progressive && gop_length == 1)
	{
		assert(frame_index == 0);

		// Apply the interlaced intra frame transform to the packed YUV frame
		TransformForwardInterlacedIntraFrameRecursiveYUV(encoder, frame, width, height, pitch,
														 transform_array, num_transforms,
														 buffer, buffer_size);
	}
	else if (progressive && gop_length == 2)
	{
		assert(0 <= frame_index && frame_index <= 1);

		// Apply the progressive inter frame transform to the packed YUV frames
		TransformForwardProgressiveInterFrameRecursiveYUV(encoder, frame, frame_index,
														 width, height, pitch,
														 transform_array, num_transforms,
														 buffer, buffer_size);
	}
	else if (!progressive && gop_length == 2)
	{
		assert(0 <= frame_index && frame_index <= 1);

		// Apply the interlaced inter frame transform to the packed YUV frames
		TransformForwardProgressiveInterFrameRecursiveYUV(encoder, frame, frame_index,
														 width, height, pitch,
														 transform_array, num_transforms,
														 buffer, buffer_size);
	}
	else
	{
		// Invalid combination of encoder parameters
		assert(0);
	}
}

#if 0
// Perform a recursive transform on a packed UYVY frame
void TransformForwardRecursiveUYVY(ENCODER *encoder, BYTE *frame, int width, int height, int pitch,
								   TRANSFORM *transform_array[], int num_transforms,
								   BYTE *buffer, size_t buffer_size)
{
	CODEC_STATE *codec = &encoder->codec;

	if (encoder->progressive)
	{
		// Progressive frame transform (implemented using the spatial transform)
		codec->progressive = 1;

		// Apply the progressive intra frame transform to the packed YUV frame
		TransformForwardProgressiveIntraFrameRecursiveYUV(encoder, frame, width, height, pitch,
														  transform_array, num_transforms,
														  buffer, buffer_size);
	}
	else
	{
		// Interlaced frame encoding (implemented using the frame transform)
		codec->progressive = 0;

		FRAME_INFO info = {width, height, format};
		//int frame_index = (j == 0) ? 1 : 0;
		//int frame_index = j;
		//int chroma_offset = encoder->codec.chroma_offset;

		// Apply the frame transform directly to the frame
		TransformForwardFrameYUV(data, pitch, &info, transform, frame_index, num_transforms,
								 (char *)buffer, buffer_size, chroma_offset, codec->precision, 0);

		assert(0);

		// Need to set an error code and return indication of failure
	}
}
#endif

#if _DEBUG

#if 0
void PrintRecursiveWavelets(ENCODER *encoder)
{
	FILE *logfile = encoder->logfile;
	int width = encoder->input.width;
	int height = encoder->input.height;
	int level;

	// Print the wavelet bands at each level
	for (level = 0; level < encoder->num_levels; level++)
	{
		int band;

		width /= 2;
		height /= 2;

		for (band = 0; band < NUM_WAVELET_BANDS; band++)
		{
			PrintArray(logfile, encoder->output[level][band], width, height);
		}
	}
}
#endif

#endif

#endif
