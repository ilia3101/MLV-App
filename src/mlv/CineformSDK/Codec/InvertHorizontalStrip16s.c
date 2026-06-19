/*! @file InvertHorizontalStrip16s.c

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

#define PREFETCH (1 && _PREFETCH)

#include <assert.h>
#include <math.h>
#include <limits.h>
#ifdef __x86_64__
    #include <mmintrin.h>        // MMX intrinsics
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif


#include "spatial.h"
#include "filter.h"			// Declarations of filter routines
//#include "image.h"		// Image processing data types
//#include "ipp.h"			// Use Intel Performance Primitives
//#include "debug.h"
#include "codec.h"
#include "buffer.h"
#include "quantize.h"
#include "convert.h"
#include "decoder.h"
#include "bayer.h"
#include "swap.h"
#include "RGB2YUV.h"

#define _PREROLL 1		// Enable loop preprocessing for memory alignment

// Forward reference (to avoid including encoder.h)
//typedef struct encoder ENCODER;
struct encoder;

#if DEBUG
// Make the logfile available for debugging
#include <stdio.h>
extern FILE *logfile;
#endif

#ifndef _QUANTIZE_SPATIAL_LOWPASS
#define _QUANTIZE_SPATIAL_LOWPASS	0
#endif

//#ifndef _UNALIGNED
//#define _UNALIGNED	0
//#endif
#ifndef _UNALIGNED
#define _UNALIGNED	0
//#elif (_UNALIGNED == 1)    // Hack for VS2012 and beyond as default behavior for VS sets _UNALIGNED == __unaligned keyword)
//do nothing
#else
#undef _UNALIGNED
#define _UNALIGNED	0
#endif

#ifndef _FASTLOOP
#define _FASTLOOP	1
#endif


// Shifts used to remove prescaling in the thumbnail spatial transform
#define V210_HORIZONTAL_SHIFT	2
#define V210_VERTICAL_SHIFT		0





#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertHorizontalStrip16s(PIXEL *lowpass_band,	// Horizontal lowpass coefficients
							  int lowpass_pitch,	// Distance between rows in bytes
							  PIXEL *highpass_band,	// Horizontal highpass coefficients
							  int highpass_pitch,	// Distance between rows in bytes
							  PIXEL *output_image,	// Row of reconstructed results
							  int output_pitch,		// Distance between rows in bytes
							  ROI roi)			// Height and width of the strip
{
	// Stub routine for processor specific dispatch
}

#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif


// Apply the inverse horizontal transform to reconstruct a strip of rows
void InvertHorizontalStrip16s(PIXEL *lowpass_band,	// Horizontal lowpass coefficients
							  int lowpass_pitch,	// Distance between rows in bytes
							  PIXEL *highpass_band,	// Horizontal highpass coefficients
							  int highpass_pitch,	// Distance between rows in bytes
							  PIXEL *output_image,	// Row of reconstructed results
							  int output_pitch,		// Distance between rows in bytes
							  ROI roi)			// Height and width of the strip
{



	int height = roi.height;
	int width = roi.width;
	PIXEL *lowpass = lowpass_band;
	PIXEL *highpass = highpass_band;
	PIXEL *output = output_image;
	const int column_step = 4;
	const int last_column = width - 1;
	int post_column = last_column - (last_column % column_step);
	int row;

	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL);

	// Adjust the end of the fast loop if necessary
	if (post_column == last_column)
		post_column -= column_step;

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		__m64 low1_pi16;	// Lowpass coefficients
		__m64 low2_pi16;
		__m64 high1_pi16;	// Highpass coefficients
		__m64 high2_pi16;

		PIXEL *colptr;

		int32_t even;
		int32_t odd;

		// The fast loop computes output points starting at the third column
		__m64 *outptr = (__m64 *)&output[2];

		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * lowpass[column + 0];
		even -=  4 * lowpass[column + 1];
		even +=  1 * lowpass[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even = DivideByShift(even, 1);

		// Place the even result in the even column
		output[0] = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * lowpass[column + 0];
		odd += 4 * lowpass[column + 1];
		odd -= 1 * lowpass[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd = DivideByShift(odd, 1);

		// Place the odd result in the odd column
		output[1] = SATURATE(odd);

#if (1 && XMMOPT)

		// Preload the first four lowpass coefficients
		low1_pi16 = *((__m64 *)&lowpass[column]);

		// Preload the first four highpass coefficients
		high1_pi16 = *((__m64 *)&highpass[column]);

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m64 even_pi16;	// Result of convolution with even filter
			__m64 odd_pi16;		// Result of convolution with odd filter
			__m64 temp_pi16;
			//__m64 temp2_pi16;
			__m64 out_pi16;		// Reconstructed data
			__m64 mask_pi16;
			__m64 half_pi16;
			__m64 lsb_pi16;
			__m64 sign_pi16;
			__m64 high_pi16;

			// Preload the next four lowpass coefficients
			low2_pi16 = *((__m64 *)&lowpass[column+4]);


			half_pi16 = _mm_set1_pi16(4);


			// Compute the first two even and two odd output points //

			// Apply the even reconstruction filter to the lowpass band
			/*even_pi16 = low1_pi16;  // +1
			temp_pi16 = _mm_slli_pi16(low1_pi16, 3);
			temp_pi16 = _mm_shuffle_pi16(temp_pi16, _MM_SHUFFLE(0, 3, 2, 1)); +8
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16); +1 +8
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2)); -1
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16); -1
			// Divide by eight
			even_pi16 = _mm_srai_pi16(even_pi16, 3);*/

//DAN031304 -- correct inverse filter
			even_pi16 = low1_pi16;//+1x
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2)); //-1x
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16); //+1a -1c
			even_pi16 = _mm_adds_pi16(even_pi16, half_pi16); //+4
			even_pi16 = _mm_srai_pi16(even_pi16, 3);  // (+1a -1c) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(0, 3, 2, 1)); //+8x
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16); // (+1a +8b -1c) >> 3


			// Shift the highpass correction by one column
			high1_pi16 = _mm_srli_si64(high1_pi16, 16);

			high_pi16 = high1_pi16;

			// Add the highpass correction
			even_pi16 = _mm_adds_pi16(even_pi16, high_pi16);
			even_pi16 = _mm_srai_pi16(even_pi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			/*odd_pi16 = _mm_slli_pi16(low1_pi16, 3);
			odd_pi16 = _mm_shuffle_pi16(odd_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16);
			odd_pi16 = _mm_subs_pi16(odd_pi16, low1_pi16);
			// Divide by eight
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);*/

			odd_pi16 = low1_pi16;//-1x
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2)); //+1x
			odd_pi16 = _mm_subs_pi16(temp_pi16, odd_pi16); //-1a +1c
			odd_pi16 = _mm_adds_pi16(odd_pi16, half_pi16); //+4
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);  // (-1a +1c) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(0, 3, 2, 1)); //+8x
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16); // (-1a +8b +1c) >> 3

			// Subtract the highpass correction
			odd_pi16 = _mm_subs_pi16(odd_pi16, high_pi16);
			odd_pi16 = _mm_srai_pi16(odd_pi16, 1);

			// Interleave the even and odd results
			out_pi16 = _mm_unpacklo_pi16(even_pi16, odd_pi16);
			//out_pi16 = _mm_max_pi16(out_pi16, _mm_setzero_si64());
			*(outptr++) = out_pi16;


			// Compute the second two even and two odd output points //

			// Preload the highpass correction
			high2_pi16 = *((__m64 *)&highpass[column+4]);

			// Shift in the new pixels for the next stage of the loop
			low1_pi16 = _mm_srli_si64(low1_pi16, 32);
			temp_pi16 = _mm_slli_si64(low2_pi16, 32);
			low1_pi16 = _mm_or_si64(low1_pi16, temp_pi16);

			// Apply the even reconstruction filter to the lowpass band
		/*	even_pi16 = low1_pi16;
			temp_pi16 = _mm_slli_pi16(low1_pi16, 3);
			temp_pi16 = _mm_shuffle_pi16(temp_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16);
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16);
			// Divide by eight
			even_pi16 = _mm_srai_pi16(even_pi16, 3);*/

			even_pi16 = low1_pi16;//+1x
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2)); //-1x
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16); //+1a -1c
			even_pi16 = _mm_adds_pi16(even_pi16, half_pi16); //+4
			even_pi16 = _mm_srai_pi16(even_pi16, 3);  // (+1a -1c) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(0, 3, 2, 1)); //+8x
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16); // (+1a +8b -1c) >> 3

			// Shift in the next two highpass coefficients
			high1_pi16 = _mm_srli_si64(high1_pi16, 2*16);
			high1_pi16 = _mm_or_si64(high1_pi16, _mm_slli_si64(high2_pi16, 16));

			// Prescale for 8bit output - DAN 4/5/02
			high_pi16 = high1_pi16;

			// Add the highpass correction
			even_pi16 = _mm_adds_pi16(even_pi16, high_pi16);
			even_pi16 = _mm_srai_pi16(even_pi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
		/*	odd_pi16 = _mm_slli_pi16(low1_pi16, 3);
			odd_pi16 = _mm_shuffle_pi16(odd_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16);
			odd_pi16 = _mm_subs_pi16(odd_pi16, low1_pi16);
			// Divide by eight
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);*/

			odd_pi16 = low1_pi16;//-1x
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2)); //+1x
			odd_pi16 = _mm_subs_pi16(temp_pi16, odd_pi16); //-1a +1c
			odd_pi16 = _mm_adds_pi16(odd_pi16, half_pi16); //+4
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);  // (-1a +1c) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(0, 3, 2, 1)); //+8x
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16); // (-1a +8b +1c) >> 3


			// Subtract the highpass correction
			odd_pi16 = _mm_subs_pi16(odd_pi16, high_pi16);
			odd_pi16 = _mm_srai_pi16(odd_pi16, 1);

			// Interleave the even and odd results
			out_pi16 = _mm_unpacklo_pi16(even_pi16, odd_pi16);
			//out_pi16 = _mm_max_pi16(out_pi16, _mm_setzero_si64());
			*(outptr++) = out_pi16;

			// The second four lowpass coefficients will be the current values
			low1_pi16 = low2_pi16;

			// The second four highpass coefficients will be the current values
			high1_pi16 = high2_pi16;
		}

		// Should have exited the loop with the column equal to the post processing column
		assert(column == post_column);

#endif

		// The fast processing loop is one column behind the actual column
		column++;

		// Process the rest of the columns up to the last column in the row
		colptr = (PIXEL *)outptr;

		for (; column < last_column; column++)
		{
			int32_t even = 0;		// Result of convolution with even filter
			int32_t odd = 0;		// Result of convolution with odd filter

			// Apply the even reconstruction filter to the lowpass band
			even += lowpass[column - 1];
			even -= lowpass[column + 1];
			even += ROUNDING(even,8);
			even = DivideByShift(even, 3);
			even += lowpass[column + 0];

			// Add the highpass correction
			even += highpass[column];
			even = DivideByShift(even, 1);

			// Place the even result in the even column
			*(colptr++) = SATURATE(even);

			// Apply the odd reconstruction filter to the lowpass band
			odd -= lowpass[column - 1];
			odd += lowpass[column + 1];
			odd += ROUNDING(odd,8);
			odd = DivideByShift(odd, 3);
			odd += lowpass[column + 0];

			// Subtract the highpass correction
			odd -= highpass[column];
			odd = DivideByShift(odd, 1);

			// Place the odd result in the odd column
			*(colptr++) = SATURATE(odd);
		}

		// Should have exited the loop at the column for right border processing
		assert(column == last_column);

		// Process the last two output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 5 * lowpass[column + 0];
		even += 4 * lowpass[column - 1];
		even -= 1 * lowpass[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even = DivideByShift(even, 1);

		// Place the even result in the even column
		*(colptr++) = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * lowpass[column + 0];
		odd -=  4 * lowpass[column - 1];
		odd +=  1 * lowpass[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd = DivideByShift(odd, 1);

		// Place the odd result in the odd column
		*(colptr++) = SATURATE(odd);

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		output += output_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Apply the inverse horizontal transform to reconstruct a strip of rows
void InvertHorizontalStrip16s(PIXEL *lowpass_band,	// Horizontal lowpass coefficients
							  int lowpass_pitch,	// Distance between rows in bytes
							  PIXEL *highpass_band,	// Horizontal highpass coefficients
							  int highpass_pitch,	// Distance between rows in bytes
							  PIXEL *output_image,	// Row of reconstructed results
							  int output_pitch,		// Distance between rows in bytes
							  ROI roi)				// Height and width of the strip
{
	int height = roi.height;
	int width = roi.width;
	PIXEL *lowpass = lowpass_band;
	PIXEL *highpass = highpass_band;
	PIXEL *output = output_image;
	const int column_step = 8;
	const int last_column = width - 1;
	int post_column = last_column - (last_column % column_step);
	int row;

	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL);

	// Adjust the end of the fast loop if necessary
	if (post_column == last_column)
		post_column -= column_step;

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		__m128i low1_epi16;		// Lowpass coefficients
		__m128i low2_epi16;
		__m128i high1_epi16;	// Highpass coefficients
		__m128i high2_epi16;

		PIXEL *colptr;

		int32_t even;
		int32_t odd;

#if _UNALIGNED
		// The fast loop computes output points starting at the third column
		__m128i *outptr = (__m128i *)&output[2];
#else
		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr = (__m128i *)&output[0];

		// Two 16-bit coefficients from the previous loop iteration
		//short remainder[2];
#endif
		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * lowpass[column + 0];
		even -=  4 * lowpass[column + 1];
		even +=  1 * lowpass[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even >>= 1;//DAN20050913 - DivideByShift(even, 1);

#if _UNALIGNED
		// Place the even result in the even column
		output[0] = SATURATE(even);
#else
		// The output value will be stored later
		//remainder[0] = SATURATE(even);
#endif
		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * lowpass[column + 0];
		odd += 4 * lowpass[column + 1];
		odd -= 1 * lowpass[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd >>= 1;//DAN20050913 - DivideByShift(odd, 1);

#if _UNALIGNED
		// Place the odd result in the odd column
		output[1] = SATURATE(odd);
#else
		// The output value will be stored later
		//remainder[1] = SATURATE(odd);
#endif

#if (_FASTLOOP && XMMOPT && 1)

		// Preload the first four lowpass coefficients
		low1_epi16 = _mm_load_si128((__m128i *)&lowpass[column]);
		//low1 : a,b,c,d,e,f,g,h

		// Preload the first four highpass coefficients
		high1_epi16 = _mm_load_si128((__m128i *)&highpass[column]);
		//high1 : A,B,C,D,E,F,G,H

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i out_epi16;		// Reconstructed data
			//__m128i mask_epi16;
			__m128i half_epi16;
			//__m128i lsb_epi16;
			//__m128i sign_epi16;
			__m128i high_epi16;

			uint32_t temp;		// Temporary register for last two values

			// Preload the next four lowpass coefficients
			low2_epi16 = _mm_load_si128((__m128i *)&lowpass[column+8]);


			// Compute the first two even and two odd output points //


//DAN031304 -- correct inverse filter
			half_epi16 = _mm_set1_epi16(4); //was 4 but 7 makes for more accurate rounding to prevent luma/chroma shifts -- DAN 6/2/03

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			//even = low1
			//even : a,b,c,d,e,f,g,h
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			//temp >>= 16*2
			//temp : 0,0,a,b,c,d,e,f
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			//even -= temp
			//even : a,b,c-a,d-b,e-c,f-d,g-e,h-f
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			//even += 4,4,4,4,4,4,4,4;
			//even : a+4,b+4,c-a+4,d-b+4,e-c+4,f-d+4,g-e+4,h-f+4
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			//even >>= 3;
			//even : (a+4)/8,(b+4)/8,(c-a+4)/8,(d-b+4)/8,(e-c+4)/8,(f-d+4)/8,(g-e+4)/8,(h-f+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);
			//even += temp
			//even : (a+4)/8+0,(b+4)/8+a,(c-a+4)/8+b,(d-b+4)/8+c,(e-c+4)/8+d,(f-d+4)/8+e,(g-e+4)/8+f,(h-f+4)/8+g



			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);
			//high1 >>= 8*2; 	//128bit
			//high1 : 0,A,B,C,D,E,F,G

			// Prescale for 8bit output - DAN 4/5/02
			high_epi16 = high1_epi16;
			//high = high1
			//high " 0,A,B,C,D,E,F,G

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, high_epi16);
			//even += high
			//even : a/8,A+(b+8a)/8,B+(c+8b-a)/8,C+(d+8c-b)/8,D+(e+8d-c)/8,E+(f+8e-d)/8,F+(g+8f-e)/8,G+(h+8g-f)/8
			even_epi16 = _mm_srai_epi16(even_epi16, 1);
			//even >>= 1
			//even : a/16,(A+(b+8a)/8)/2,(B+(c+8b-a)/8)/2,(C+(d+8c-b)/8)/2,(D+(e+8d-c)/8)/2,(E+(f+8e-d)/8)/2,(F+(g+8f-e)/8)/2,(G+(h+8g-f)/8)/2

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			//odd = low1
			//odd : 0,0,a,b,c,d,e,f,g,h
			temp_epi16 = low1_epi16;
			//temp >>= 16*2
			//temp : a,b,c,d,e,f,g,h
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			//odd -= temp
			//odd : a,b,a-c,b-d,c-e,d-f,e-g,f-h
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			//odd += 4,4,4,4,4,4,4,4;
			//odd : a+4,b+4,a-c+4,b-d+4,c-e+4,d-f+4,e-g+4,f-h+4
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			//odd >>= 3;
			//odd : (a+4)/8,(b+4)/8,(a-c+4)/8,(b-d+4)/8,(c-e+4)/8,(d-f+4)/8,(e-g+4)/8,(f-h+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);
			//odd += temp
			//odd : (a+4)/8+0,(b+4)/8+a,(a-c+4)/8+b,(b-d+4)/8+c,(c-e+4)/8+d,(d-f+4)/8+e,(e-g+4)/8+f,(f-h+4)/8+g



			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_subs_epi16(odd_epi16, high_epi16);
			//odd -= high
			//odd : -a/8,(8a-b)/8-A,(8b+a-c)/8-B,(8c+b-d)/8-C,(8d+c-e)/8-D,(8e+d-f)/8-E,(8f+e-g)/8-F,(8g+f-h)/8-G
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
			//odd >>= 1;
			//odd : -a/16,((8a-b)/8-A)/2,((8b+a-c)/8-B)/2,((8c+b-d)/8-C)/2,((8d+c-e)/8-D)/2,((8e+d-f)/8-E)/2,((8f+e-g)/8-F)/2,((8g+f-h)/8-G)/2

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
			//out = ((8d+c-e)/8-D)/2,(D+(e+8d-c)/8)/2, ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2


			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			//out : ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2, ((8d+c-e)/8-D)/2,(D+(e+8d-c)/8)/2
			temp = _mm_cvtsi128_si32(out_epi16);
			//temp32 =  ((8d+c-e)/8-D)/2,(D+(e+8d-c)/8)/2
			out_epi16 = _mm_insert_epi16(out_epi16, even, 0);
			//out : ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2, ((8d+c-e)/8-D)/2, even32
			out_epi16 = _mm_insert_epi16(out_epi16, odd, 1);
			//out : ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2, odd16, even16

			// Store eight output values
			_mm_store_si128(outptr++, out_epi16);

			// Save the remaining two output values
			even = (short)temp;
			odd = (short)(temp >> 16);




			// Compute the second two even and two odd output points //

			// Preload the highpass correction
			high2_epi16 = _mm_load_si128((__m128i *)&highpass[column+8]);

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);


			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			//even = low1
			//even : a,b,c,d,e,f,g,h
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			//temp >>= 16*2
			//temp : 0,0,a,b,c,d,e,f
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			//even -= temp
			//even : a,b,c-a,d-b,e-c,f-d,g-e,h-f
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			//even += 4,4,4,4,4,4,4,4;
			//even : a+4,b+4,c-a+4,d-b+4,e-c+4,f-d+4,g-e+4,h-f+4
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			//even >>= 3;
			//even : (a+4)/8,(b+4)/8,(c-a+4)/8,(d-b+4)/8,(e-c+4)/8,(f-d+4)/8,(g-e+4)/8,(h-f+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);
			//even += temp
			//even : (a+4)/8+0,(b+4)/8+a,(c-a+4)/8+b,(d-b+4)/8+c,(e-c+4)/8+d,(f-d+4)/8+e,(g-e+4)/8+f,(h-f+4)/8+g



			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			high_epi16 = high1_epi16;

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, high_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			//odd = low1
			//odd : 0,0,a,b,c,d,e,f,g,h
			temp_epi16 = low1_epi16;
			//temp >>= 16*2
			//temp : a,b,c,d,e,f,g,h
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			//odd -= temp
			//odd : a,b,a-c,b-d,c-e,d-f,e-g,f-h
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			//odd += 4,4,4,4,4,4,4,4;
			//odd : a+4,b+4,a-c+4,b-d+4,c-e+4,d-f+4,e-g+4,f-h+4
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			//odd >>= 3;
			//odd : (a+4)/8,(b+4)/8,(a-c+4)/8,(b-d+4)/8,(c-e+4)/8,(d-f+4)/8,(e-g+4)/8,(f-h+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);
			//odd += temp
			//odd : (a+4)/8+0,(b+4)/8+a,(a-c+4)/8+b,(b-d+4)/8+c,(c-e+4)/8+d,(d-f+4)/8+e,(e-g+4)/8+f,(f-h+4)/8+g



			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_subs_epi16(odd_epi16, high_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
			//out_epi16 = _mm_max_epi16(out_epi16, _mm_setzero_si64());



			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, even, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, odd, 1);

			// Store eight output values
			_mm_store_si128(outptr++, out_epi16);

			// Save the remaining two output values
			even = (short)temp;
			odd = (short)(temp >> 16);

			// The second four lowpass coefficients will be the current values
			low1_epi16 = low2_epi16;

			// The second four highpass coefficients will be the current values
			high1_epi16 = high2_epi16;
		}

		// Should have exited the loop with the column equal to the post processing column
		assert(column == post_column);

#endif

		// The fast processing loop is one column behind the actual column
		column++;

		// Get the pointer to the next output value
		colptr = (PIXEL *)outptr;

#if _UNALIGNED
		// The last two output points have already been stored
#elif 0
		// Store the last two output points produced by the loop
		*(colptr++) = remainder[0];
		*(colptr++) = remainder[1];
#else
		// Store the last two output points produced by the loop
		*(colptr++) = SATURATE(even);
		*(colptr++) = SATURATE(odd);
#endif

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column++)
		{
			short even = 0;		// Result of convolution with even filter
			short odd = 0;		// Result of convolution with odd filter

			// Apply the even reconstruction filter to the lowpass band

			even += lowpass[column - 1];
			even -= lowpass[column + 1];
			even += 4;  //DAN20050921
			even >>= 3;
			even += lowpass[column + 0];

			// Add the highpass correction
			even += highpass[column];
			even >>= 1;//DAN20050913 - DivideByShift(even, 1);

			// Place the even result in the even column
			*(colptr++) = SATURATE(even);

			// Apply the odd reconstruction filter to the lowpass band
			odd -= lowpass[column - 1];
			odd += lowpass[column + 1];
			odd += 4;   //DAN20050921
			odd >>= 3;
			odd += lowpass[column + 0];

			// Subtract the highpass correction
			odd -= highpass[column];
			odd >>= 1;//DAN20050913 - DivideByShift(odd, 1);

			// Place the odd result in the odd column
			*(colptr++) = SATURATE(odd);
		}

		// Should have exited the loop at the column for right border processing
		assert(column == last_column);

		// Process the last two output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 5 * lowpass[column + 0];
		even += 4 * lowpass[column - 1];
		even -= 1 * lowpass[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even >>= 1;//DAN20050913 - DivideByShift(even, 1);

		// Place the even result in the even column
		*(colptr++) = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * lowpass[column + 0];
		odd -=  4 * lowpass[column - 1];
		odd +=  1 * lowpass[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd >>= 1;//DAN20050913 - DivideByShift(odd, 1);

		// Place the odd result in the odd column
		*(colptr++) = SATURATE(odd);

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		output += output_pitch;
	}
}

#endif



// Apply the inverse horizontal transform to reconstruct a strip of rows
void InvertHorizontalStrip16s10bitLimit(PIXEL *lowpass_band,	// Horizontal lowpass coefficients
							  int lowpass_pitch,	// Distance between rows in bytes
							  PIXEL *highpass_band,	// Horizontal highpass coefficients
							  int highpass_pitch,	// Distance between rows in bytes
							  PIXEL *output_image,	// Row of reconstructed results
							  int output_pitch,		// Distance between rows in bytes
							  ROI roi)				// Height and width of the strip
{
	int height = roi.height;
	int width = roi.width;
	PIXEL *lowpass = lowpass_band;
	PIXEL *highpass = highpass_band;
	PIXEL *output = output_image;
	const int column_step = 8;
	const int last_column = width - 1;
	int post_column = last_column - (last_column % column_step);
	int row;

	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL);

	// Adjust the end of the fast loop if necessary
	if (post_column == last_column)
		post_column -= column_step;

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		__m128i low1_epi16;		// Lowpass coefficients
		__m128i low2_epi16;
		__m128i high1_epi16;	// Highpass coefficients
		__m128i high2_epi16;

		__m128i overflowprotect_epi16 = _mm_set1_epi16(0x7fff-2047);

		PIXEL *colptr;

		int32_t even;
		int32_t odd;

#if _UNALIGNED
		// The fast loop computes output points starting at the third column
		__m128i *outptr = (__m128i *)&output[2];
#else
		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr = (__m128i *)&output[0];

		// Two 16-bit coefficients from the previous loop iteration
		//short remainder[2];
#endif
		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * lowpass[column + 0];
		even -=  4 * lowpass[column + 1];
		even +=  1 * lowpass[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even = DivideByShift(even, 1);

#if _UNALIGNED
		// Place the even result in the even column
		output[0] = SATURATE(even);
#else
		// The output value will be stored later
		//remainder[0] = SATURATE(even);
#endif
		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * lowpass[column + 0];
		odd += 4 * lowpass[column + 1];
		odd -= 1 * lowpass[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd = DivideByShift(odd, 1);

#if _UNALIGNED
		// Place the odd result in the odd column
		output[1] = SATURATE(odd);
#else
		// The output value will be stored later
		//remainder[1] = SATURATE(odd);
#endif

#if (_FASTLOOP && XMMOPT)

		// Preload the first four lowpass coefficients
		low1_epi16 = _mm_load_si128((__m128i *)&lowpass[column]);
		//low1 : a,b,c,d,e,f,g,h

		// Preload the first four highpass coefficients
		high1_epi16 = _mm_load_si128((__m128i *)&highpass[column]);
		//high1 : A,B,C,D,E,F,G,H

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i out_epi16;		// Reconstructed data
			//__m128i mask_epi16;
			__m128i half_epi16;
			//__m128i lsb_epi16;
			//__m128i sign_epi16;
			__m128i high_epi16;

			uint32_t temp;		// Temporary register for last two values

			// Preload the next four lowpass coefficients
			low2_epi16 = _mm_load_si128((__m128i *)&lowpass[column+8]);


			// Compute the first two even and two odd output points //


//DAN031304 -- correct inverse filter
			half_epi16 = _mm_set1_epi16(4); //was 4 but 7 makes for more accurate rounding to prevent luma/chroma shifts -- DAN 6/2/03

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			//even = low1
			//even : a,b,c,d,e,f,g,h
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			//temp >>= 16*2
			//temp : 0,0,a,b,c,d,e,f
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			//even -= temp
			//even : a,b,c-a,d-b,e-c,f-d,g-e,h-f
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			//even += 4,4,4,4,4,4,4,4;
			//even : a+4,b+4,c-a+4,d-b+4,e-c+4,f-d+4,g-e+4,h-f+4
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			//even >>= 3;
			//even : (a+4)/8,(b+4)/8,(c-a+4)/8,(d-b+4)/8,(e-c+4)/8,(f-d+4)/8,(g-e+4)/8,(h-f+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);
			//even += temp
			//even : (a+4)/8+0,(b+4)/8+a,(c-a+4)/8+b,(d-b+4)/8+c,(e-c+4)/8+d,(f-d+4)/8+e,(g-e+4)/8+f,(h-f+4)/8+g



			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);
			//high1 >>= 8*2; 	//128bit
			//high1 : 0,A,B,C,D,E,F,G

			// Prescale for 8bit output - DAN 4/5/02
			high_epi16 = high1_epi16;
			//high = high1
			//high " 0,A,B,C,D,E,F,G

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, high_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, overflowprotect_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, overflowprotect_epi16);
			//even += high
			//even : a/8,A+(b+8a)/8,B+(c+8b-a)/8,C+(d+8c-b)/8,D+(e+8d-c)/8,E+(f+8e-d)/8,F+(g+8f-e)/8,G+(h+8g-f)/8
			even_epi16 = _mm_srai_epi16(even_epi16, 1);
			//even >>= 1
			//even : a/16,(A+(b+8a)/8)/2,(B+(c+8b-a)/8)/2,(C+(d+8c-b)/8)/2,(D+(e+8d-c)/8)/2,(E+(f+8e-d)/8)/2,(F+(g+8f-e)/8)/2,(G+(h+8g-f)/8)/2

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			//odd = low1
			//odd : 0,0,a,b,c,d,e,f,g,h
			temp_epi16 = low1_epi16;
			//temp >>= 16*2
			//temp : a,b,c,d,e,f,g,h
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			//odd -= temp
			//odd : a,b,a-c,b-d,c-e,d-f,e-g,f-h
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			//odd += 4,4,4,4,4,4,4,4;
			//odd : a+4,b+4,a-c+4,b-d+4,c-e+4,d-f+4,e-g+4,f-h+4
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			//odd >>= 3;
			//odd : (a+4)/8,(b+4)/8,(a-c+4)/8,(b-d+4)/8,(c-e+4)/8,(d-f+4)/8,(e-g+4)/8,(f-h+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);
			//odd += temp
			//odd : (a+4)/8+0,(b+4)/8+a,(a-c+4)/8+b,(b-d+4)/8+c,(c-e+4)/8+d,(d-f+4)/8+e,(e-g+4)/8+f,(f-h+4)/8+g



			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_subs_epi16(odd_epi16, high_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, overflowprotect_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, overflowprotect_epi16);
			//odd -= high
			//odd : -a/8,(8a-b)/8-A,(8b+a-c)/8-B,(8c+b-d)/8-C,(8d+c-e)/8-D,(8e+d-f)/8-E,(8f+e-g)/8-F,(8g+f-h)/8-G
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
			//odd >>= 1;
			//odd : -a/16,((8a-b)/8-A)/2,((8b+a-c)/8-B)/2,((8c+b-d)/8-C)/2,((8d+c-e)/8-D)/2,((8e+d-f)/8-E)/2,((8f+e-g)/8-F)/2,((8g+f-h)/8-G)/2

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
			//out = ((8d+c-e)/8-D)/2,(D+(e+8d-c)/8)/2, ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2


			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			//out : ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2, ((8d+c-e)/8-D)/2,(D+(e+8d-c)/8)/2
			temp = _mm_cvtsi128_si32(out_epi16);
			//temp32 =  ((8d+c-e)/8-D)/2,(D+(e+8d-c)/8)/2
			out_epi16 = _mm_insert_epi16(out_epi16, even, 0);
			//out : ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2, ((8d+c-e)/8-D)/2, even32
			out_epi16 = _mm_insert_epi16(out_epi16, odd, 1);
			//out : ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2, odd16, even16

			// Store eight output values
			_mm_store_si128(outptr++, out_epi16);

			// Save the remaining two output values
			even = (short)temp;
			odd = (short)(temp >> 16);




			// Compute the second two even and two odd output points //

			// Preload the highpass correction
			high2_epi16 = _mm_load_si128((__m128i *)&highpass[column+8]);

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);


			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			//even = low1
			//even : a,b,c,d,e,f,g,h
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			//temp >>= 16*2
			//temp : 0,0,a,b,c,d,e,f
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			//even -= temp
			//even : a,b,c-a,d-b,e-c,f-d,g-e,h-f
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			//even += 4,4,4,4,4,4,4,4;
			//even : a+4,b+4,c-a+4,d-b+4,e-c+4,f-d+4,g-e+4,h-f+4
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			//even >>= 3;
			//even : (a+4)/8,(b+4)/8,(c-a+4)/8,(d-b+4)/8,(e-c+4)/8,(f-d+4)/8,(g-e+4)/8,(h-f+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);
			//even += temp
			//even : (a+4)/8+0,(b+4)/8+a,(c-a+4)/8+b,(d-b+4)/8+c,(e-c+4)/8+d,(f-d+4)/8+e,(g-e+4)/8+f,(h-f+4)/8+g



			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			high_epi16 = high1_epi16;

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, high_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, overflowprotect_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, overflowprotect_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			//odd = low1
			//odd : 0,0,a,b,c,d,e,f,g,h
			temp_epi16 = low1_epi16;
			//temp >>= 16*2
			//temp : a,b,c,d,e,f,g,h
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			//odd -= temp
			//odd : a,b,a-c,b-d,c-e,d-f,e-g,f-h
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			//odd += 4,4,4,4,4,4,4,4;
			//odd : a+4,b+4,a-c+4,b-d+4,c-e+4,d-f+4,e-g+4,f-h+4
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			//odd >>= 3;
			//odd : (a+4)/8,(b+4)/8,(a-c+4)/8,(b-d+4)/8,(c-e+4)/8,(d-f+4)/8,(e-g+4)/8,(f-h+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);
			//odd += temp
			//odd : (a+4)/8+0,(b+4)/8+a,(a-c+4)/8+b,(b-d+4)/8+c,(c-e+4)/8+d,(d-f+4)/8+e,(e-g+4)/8+f,(f-h+4)/8+g



			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_subs_epi16(odd_epi16, high_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, overflowprotect_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, overflowprotect_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
			//out_epi16 = _mm_max_epi16(out_epi16, _mm_setzero_si64());



			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, even, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, odd, 1);

			// Store eight output values
			_mm_store_si128(outptr++, out_epi16);

			// Save the remaining two output values
			even = (short)temp;
			odd = (short)(temp >> 16);

			// The second four lowpass coefficients will be the current values
			low1_epi16 = low2_epi16;

			// The second four highpass coefficients will be the current values
			high1_epi16 = high2_epi16;
		}

		// Should have exited the loop with the column equal to the post processing column
		assert(column == post_column);

#endif

		// The fast processing loop is one column behind the actual column
		column++;

		// Get the pointer to the next output value
		colptr = (PIXEL *)outptr;

#if _UNALIGNED
		// The last two output points have already been stored
#elif 0
		// Store the last two output points produced by the loop
		*(colptr++) = remainder[0];
		*(colptr++) = remainder[1];
#else
		// Store the last two output points produced by the loop
		*(colptr++) = SATURATE(even);
		*(colptr++) = SATURATE(odd);
#endif

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column++)
		{
			short even = 0;		// Result of convolution with even filter
			short odd = 0;		// Result of convolution with odd filter

			// Apply the even reconstruction filter to the lowpass band

			even += lowpass[column - 1];
			even -= lowpass[column + 1];
			even += 4;   //DAN20050921
			even >>= 3;
			even += lowpass[column + 0];

			// Add the highpass correction
			even += highpass[column];
			even = DivideByShift(even, 1);

			if(even < 0) even = 0;
			if(even > 1023) even = 1023;

			// Place the even result in the even column
			*(colptr++) = SATURATE(even);

			// Apply the odd reconstruction filter to the lowpass band
			odd -= lowpass[column - 1];
			odd += lowpass[column + 1];
			odd += 4;  //DAN20050921
			odd >>= 3;
			odd += lowpass[column + 0];

			// Subtract the highpass correction
			odd -= highpass[column];
			odd = DivideByShift(odd, 1);

			if(odd < 0) odd = 0;
			if(odd > 1023) odd = 1023;

			// Place the odd result in the odd column
			*(colptr++) = SATURATE(odd);
		}

		// Should have exited the loop at the column for right border processing
		assert(column == last_column);

		// Process the last two output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 5 * lowpass[column + 0];
		even += 4 * lowpass[column - 1];
		even -= 1 * lowpass[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even = DivideByShift(even, 1);

		if(even < 0) even = 0;
		if(even > 1023) even = 1023;

		// Place the even result in the even column
		*(colptr++) = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * lowpass[column + 0];
		odd -=  4 * lowpass[column - 1];
		odd +=  1 * lowpass[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd = DivideByShift(odd, 1);

		if(odd < 0) odd = 0;
		if(odd > 1023) odd = 1023;

		// Place the odd result in the odd column
		*(colptr++) = SATURATE(odd);

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		output += output_pitch;
	}
}





#if _PROCESSOR_DISPATCH
__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertHorizontalStripDescale16s(PIXEL *lowpass_band, int lowpass_pitch,
									 PIXEL *highpass_band, int highpass_pitch,
									 PIXEL *output_image, int output_pitch,
									 ROI roi, int descale)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC
#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

void InvertHorizontalStripDescale16s(PIXEL *lowpass_band, int lowpass_pitch,
									 PIXEL *highpass_band, int highpass_pitch,
									 PIXEL *output_image, int output_pitch,
									 ROI roi, int descale)
{
//	int InvertHorizontalStripDescale16s_MMX_not_done =  1;
//	assert(InvertHorizontalStripDescale16s_MMX_not_done);

	int height = roi.height;
	int width = roi.width;
	PIXEL *lowpass = lowpass_band;
	PIXEL *highpass = highpass_band;
	PIXEL *output = output_image;
	const int column_step = 4;
	const int last_column = width - 1;
	int post_column = last_column - (last_column % column_step);
	int row;
	int descaleshift = 0;

	// The algorithm incorporates descaling by a factor of two
	if(descale == 2)
		descaleshift = 1;

	// Check that the descaling value is reasonable
	assert(descaleshift >= 0);

	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL);

	// Adjust the end of the fast loop if necessary
	if (post_column == last_column)
		post_column -= column_step;

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		__m64 low1_pi16;	// Lowpass coefficients
		__m64 low2_pi16;
		__m64 high1_pi16;	// Highpass coefficients
		__m64 high2_pi16;

		PIXEL *colptr;

		int32_t even;
		int32_t odd;

		// The fast loop computes output points starting at the third column
		__m64 *outptr = (__m64 *)&output[2];

		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * lowpass[column + 0];
		even -=  4 * lowpass[column + 1];
		even +=  1 * lowpass[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		//even = DivideByShift(even, 1);
		even <<= descaleshift;
		// Place the even result in the even column
		output[0] = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * lowpass[column + 0];
		odd += 4 * lowpass[column + 1];
		odd -= 1 * lowpass[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		//odd = DivideByShift(odd, 1);
		odd <<= descaleshift;
		// Place the odd result in the odd column
		output[1] = SATURATE(odd);

#if (1 && XMMOPT)

		// Preload the first four lowpass coefficients
		low1_pi16 = *((__m64 *)&lowpass[column]);

		// Preload the first four highpass coefficients
		high1_pi16 = *((__m64 *)&highpass[column]);

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m64 even_pi16;	// Result of convolution with even filter
			__m64 odd_pi16;		// Result of convolution with odd filter
			__m64 temp_pi16;
			//__m64 temp2_pi16;
			__m64 out_pi16;		// Reconstructed data
			__m64 mask_pi16;
			__m64 half_pi16;
			__m64 lsb_pi16;
			__m64 sign_pi16;
			__m64 high_pi16;


			half_pi16 = _mm_set1_pi16(4);


			// Preload the next four lowpass coefficients
			low2_pi16 = *((__m64 *)&lowpass[column+4]);


			// Compute the first two even and two odd output points //
			even_pi16 = low1_pi16;//+1x
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2)); //-1x
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16); //+1a -1c
			even_pi16 = _mm_adds_pi16(even_pi16, half_pi16); // rounding
			even_pi16 = _mm_srai_pi16(even_pi16, 3);  // (+1a -1c) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(0, 3, 2, 1)); //+8x
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16); // (+1a +8b -1c) >> 3


			// Shift the highpass correction by one column
			high1_pi16 = _mm_srli_si64(high1_pi16, 16);

			high_pi16 = high1_pi16;

			// Add the highpass correction
			even_pi16 = _mm_adds_pi16(even_pi16, high_pi16);
			//even_pi16 = _mm_srai_pi16(even_pi16, 1);

			odd_pi16 = low1_pi16;//-1x
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2)); //+1x
			odd_pi16 = _mm_subs_pi16(temp_pi16, odd_pi16); //-1a +1c
			odd_pi16 = _mm_adds_pi16(odd_pi16, half_pi16); // rounding
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);  // (-1a +1c) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(0, 3, 2, 1)); //+8x
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16); // (-1a +8b +1c) >> 3

			// Subtract the highpass correction
			odd_pi16 = _mm_subs_pi16(odd_pi16, high_pi16);
			//odd_pi16 = _mm_srai_pi16(odd_pi16, 1);

			// Interleave the even and odd results
			out_pi16 = _mm_unpacklo_pi16(even_pi16, odd_pi16);
			//out_pi16 = _mm_max_pi16(out_pi16, _mm_setzero_si64());

			//descale
			out_pi16 = _mm_slli_pi16(out_pi16, descaleshift);

			*(outptr++) = out_pi16;


			// Compute the second two even and two odd output points //

			// Preload the highpass correction
			high2_pi16 = *((__m64 *)&highpass[column+4]);

			// Shift in the new pixels for the next stage of the loop
			low1_pi16 = _mm_srli_si64(low1_pi16, 32);
			temp_pi16 = _mm_slli_si64(low2_pi16, 32);
			low1_pi16 = _mm_or_si64(low1_pi16, temp_pi16);

			even_pi16 = low1_pi16;//+1x
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2)); //-1x
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16); //+1a -1c
			even_pi16 = _mm_adds_pi16(even_pi16, half_pi16); // rounding
			even_pi16 = _mm_srai_pi16(even_pi16, 3);  // (+1a -1c) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(0, 3, 2, 1)); //+8x
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16); // (+1a +8b -1c) >> 3

			// Shift in the next two highpass coefficients
			high1_pi16 = _mm_srli_si64(high1_pi16, 2*16);
			high1_pi16 = _mm_or_si64(high1_pi16, _mm_slli_si64(high2_pi16, 16));

			// Prescale for 8bit output - DAN 4/5/02
			high_pi16 = high1_pi16;

			// Add the highpass correction
			even_pi16 = _mm_adds_pi16(even_pi16, high_pi16);
			//even_pi16 = _mm_srai_pi16(even_pi16, 1);

			odd_pi16 = low1_pi16;//-1x
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2)); //+1x
			odd_pi16 = _mm_subs_pi16(temp_pi16, odd_pi16); //-1a +1c
			odd_pi16 = _mm_adds_pi16(odd_pi16, half_pi16); // rounding
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);  // (-1a +1c) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(0, 3, 2, 1)); //+8x
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16); // (-1a +8b +1c) >> 3


			// Subtract the highpass correction
			odd_pi16 = _mm_subs_pi16(odd_pi16, high_pi16);
			//odd_pi16 = _mm_srai_pi16(odd_pi16, 1);

			// Interleave the even and odd results
			out_pi16 = _mm_unpacklo_pi16(even_pi16, odd_pi16);
			//out_pi16 = _mm_max_pi16(out_pi16, _mm_setzero_si64());

			//descale
			out_pi16 = _mm_slli_pi16(out_pi16, descaleshift);

			*(outptr++) = out_pi16;

			// The second four lowpass coefficients will be the current values
			low1_pi16 = low2_pi16;

			// The second four highpass coefficients will be the current values
			high1_pi16 = high2_pi16;
		}


		// Should have exited the loop with the column equal to the post processing column
		assert(column == post_column);

#endif

		// The fast processing loop is one column behind the actual column
		column++;

		// Get the pointer to the next output value
		colptr = (PIXEL *)outptr;

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column++)
		{
			short even = 0;		// Result of convolution with even filter
			short odd = 0;		// Result of convolution with odd filter

			// Apply the even reconstruction filter to the lowpass band
			even += lowpass[column - 1];
			even -= lowpass[column + 1];
			even += 4;  //DAN20050921
			even >>= 3;
			even += lowpass[column + 0];

			// Add the highpass correction
			even += highpass[column];
			//even = DivideByShift(even, 1);

			// Remove any scaling used during encoding
			even <<= descaleshift;

			// Place the even result in the even column
			*(colptr++) = SATURATE(even);

			// Apply the odd reconstruction filter to the lowpass band
			odd -= lowpass[column - 1];
			odd += lowpass[column + 1];
			odd += 4;  //DAN20050921
			odd >>= 3;
			odd += lowpass[column + 0];

			// Subtract the highpass correction
			odd -= highpass[column];
			//odd = DivideByShift(odd, 1);

			// Remove any scaling used during encoding
			odd <<= descaleshift;

			// Place the odd result in the odd column
			*(colptr++) = SATURATE(odd);
		}

		// Should have exited the loop at the column for right border processing
		assert(column == last_column);

		// Process the last two output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 5 * lowpass[column + 0];
		even += 4 * lowpass[column - 1];
		even -= 1 * lowpass[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		//even = DivideByShift(even, 1);

		// Remove any scaling used during encoding
		even <<= descaleshift;

		// Place the even result in the even column
		*(colptr++) = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * lowpass[column + 0];
		odd -=  4 * lowpass[column - 1];
		odd +=  1 * lowpass[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		//odd = DivideByShift(odd, 1);

		// Remove any scaling used during encoding
		odd <<= descaleshift;

		// Place the odd result in the odd column
		*(colptr++) = SATURATE(odd);

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		output += output_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}
#endif // _PROCESSOR_GENERIC


#if _PROCESSOR_PENTIUM_4
#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Apply the inverse horizontal transform to reconstruct a strip of rows
void InvertHorizontalStripDescale16s(PIXEL *lowpass_band, int lowpass_pitch,
									 PIXEL *highpass_band, int highpass_pitch,
									 PIXEL *output_image, int output_pitch,
									 ROI roi, int descale)
{
	int height = roi.height;
	int width = roi.width;
	PIXEL *lowpass = lowpass_band;
	PIXEL *highpass = highpass_band;
	PIXEL *output = output_image;
	const int column_step = 8;
	const int last_column = width - 1;
	int post_column = last_column - (last_column % column_step);
	int row;
	int descaleshift = 0;

	// The algorithm incorporates descaling by a factor of two
	if(descale == 2)
		descaleshift = 1;

	// Check that the descaling value is reasonable
	assert(descaleshift >= 0);

	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL);

	// Adjust the end of the fast loop if necessary
	if (post_column == last_column)
		post_column -= column_step;

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		__m128i low1_epi16;		// Lowpass coefficients
		__m128i low2_epi16;
		__m128i high1_epi16;	// Highpass coefficients
		__m128i high2_epi16;

		PIXEL *colptr;

		int32_t even;
		int32_t odd;

#if _UNALIGNED
		// The fast loop computes output points starting at the third column
		__m128i *outptr = (__m128i *)&output[2];
#else
		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr = (__m128i *)&output[0];

		// Two 16-bit coefficients from the previous loop iteration
		//short remainder[2];
#endif
		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * lowpass[column + 0];
		even -=  4 * lowpass[column + 1];
		even +=  1 * lowpass[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		//even = DivideByShift(even, 1);

		// Remove any scaling used during encoding
		//even <<= descaleshift;

#if _UNALIGNED
		// Place the even result in the even column
		output[0] = SATURATE(even);
#else
		// The output value will be stored later
		//remainder[0] = SATURATE(even);
#endif
		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * lowpass[column + 0];
		odd += 4 * lowpass[column + 1];
		odd -= 1 * lowpass[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		//odd = DivideByShift(odd, 1);

		// Remove any scaling used during encoding
		//odd <<= descaleshift;

#if _UNALIGNED
		// Place the odd result in the odd column
		output[1] = SATURATE(odd);
#else
		// The output value will be stored later
		//remainder[1] = SATURATE(odd);
#endif

#if (_FASTLOOP && XMMOPT)

		// Preload the first four lowpass coefficients
		low1_epi16 = _mm_load_si128((__m128i *)&lowpass[column]);
		//low1 : a,b,c,d,e,f,g,h

		// Preload the first four highpass coefficients
		high1_epi16 = _mm_load_si128((__m128i *)&highpass[column]);
		//high1 : A,B,C,D,E,F,G,H

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i out_epi16;		// Reconstructed data
			//__m128i mask_epi16;
			__m128i half_epi16;
			//__m128i lsb_epi16;
			//__m128i sign_epi16;
			__m128i high_epi16;

		//	__m128i descale_si128 = _mm_cvtsi32_si128(descaleshift);

			uint32_t temp;		// Temporary register for last two values

			// Preload the next four lowpass coefficients
			low2_epi16 = _mm_load_si128((__m128i *)&lowpass[column+8]);


			// ***** Compute the first two even and two odd output points ****


			//was 4 but 7 makes for more accurate rounding to prevent luma/chroma shifts -- DAN 6/2/03

//DAN031304 -- correct inverse filter
			half_epi16 = _mm_set1_epi16(4);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			//even = low1
			//even : a,b,c,d,e,f,g,h
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			//temp >>= 16*2
			//temp : 0,0,a,b,c,d,e,f
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			//even -= temp
			//even : a,b,c-a,d-b,e-c,f-d,g-e,h-f
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			//even += 4,4,4,4,4,4,4,4;
			//even : a+4,b+4,c-a+4,d-b+4,e-c+4,f-d+4,g-e+4,h-f+4
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			//even >>= 3;
			//even : (a+4)/8,(b+4)/8,(c-a+4)/8,(d-b+4)/8,(e-c+4)/8,(f-d+4)/8,(g-e+4)/8,(h-f+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);
			//even += temp
			//even : (a+4)/8+0,(b+4)/8+a,(c-a+4)/8+b,(d-b+4)/8+c,(e-c+4)/8+d,(f-d+4)/8+e,(g-e+4)/8+f,(h-f+4)/8+g


			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);
			//high1 >>= 8*2; 	//128bit
			//high1 : 0,A,B,C,D,E,F,G

			// Prescale for 8bit output - DAN 4/5/02
			high_epi16 = high1_epi16;
			//high = high1
			//high " 0,A,B,C,D,E,F,G

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, high_epi16);
			//even += high
			//even : a/8,A+(b+8a)/8,B+(c+8b-a)/8,C+(d+8c-b)/8,D+(e+8d-c)/8,E+(f+8e-d)/8,F+(g+8f-e)/8,G+(h+8g-f)/8
			//even_epi16 = _mm_srai_epi16(even_epi16, 1);
			//even >>= 1
			//even : a/16,(A+(b+8a)/8)/2,(B+(c+8b-a)/8)/2,(C+(d+8c-b)/8)/2,(D+(e+8d-c)/8)/2,(E+(f+8e-d)/8)/2,(F+(g+8f-e)/8)/2,(G+(h+8g-f)/8)/2

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			//odd = low1
			//odd : 0,0,a,b,c,d,e,f,g,h
			temp_epi16 = low1_epi16;
			//temp >>= 16*2
			//temp : a,b,c,d,e,f,g,h
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			//odd -= temp
			//odd : a,b,a-c,b-d,c-e,d-f,e-g,f-h
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			//odd += 4,4,4,4,4,4,4,4;
			//odd : a+4,b+4,a-c+4,b-d+4,c-e+4,d-f+4,e-g+4,f-h+4
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			//odd >>= 3;
			//odd : (a+4)/8,(b+4)/8,(a-c+4)/8,(b-d+4)/8,(c-e+4)/8,(d-f+4)/8,(e-g+4)/8,(f-h+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);
			//odd += temp
			//odd : (a+4)/8+0,(b+4)/8+a,(a-c+4)/8+b,(b-d+4)/8+c,(c-e+4)/8+d,(d-f+4)/8+e,(e-g+4)/8+f,(f-h+4)/8+g


			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_subs_epi16(odd_epi16, high_epi16);
			//odd -= high
			//odd : -a/8,(8a-b)/8-A,(8b+a-c)/8-B,(8c+b-d)/8-C,(8d+c-e)/8-D,(8e+d-f)/8-E,(8f+e-g)/8-F,(8g+f-h)/8-G
			//odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
			//odd >>= 1;
			//odd : -a/16,((8a-b)/8-A)/2,((8b+a-c)/8-B)/2,((8c+b-d)/8-C)/2,((8d+c-e)/8-D)/2,((8e+d-f)/8-E)/2,((8f+e-g)/8-F)/2,((8g+f-h)/8-G)/2

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
			//out = ((8d+c-e)/8-D)/2,(D+(e+8d-c)/8)/2, ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2


			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			//out : ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2, ((8d+c-e)/8-D)/2,(D+(e+8d-c)/8)/2
			temp = _mm_cvtsi128_si32(out_epi16);
			//temp32 =  ((8d+c-e)/8-D)/2,(D+(e+8d-c)/8)/2
			out_epi16 = _mm_insert_epi16(out_epi16, even, 0);
			//out : ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2, ((8d+c-e)/8-D)/2, even32
			out_epi16 = _mm_insert_epi16(out_epi16, odd, 1);
			//out : ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2, odd16, even16

			// Remove any scaling used during encoding
		//	out_epi16 = _mm_sll_epi16(out_epi16, descale_si128);
			out_epi16 = _mm_adds_epi16(out_epi16, out_epi16);

			// Store eight output values
			_mm_store_si128(outptr++, out_epi16);

			// Save the remaining two output values
			even = (short)temp;
			odd = (short)(temp >> 16);


			// ***** Compute the second two even and two odd output points *****

			// Preload the highpass correction
			high2_epi16 = _mm_load_si128((__m128i *)&highpass[column+8]);

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);


			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			//even = low1
			//even : a,b,c,d,e,f,g,h
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			//temp >>= 16*2
			//temp : 0,0,a,b,c,d,e,f
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			//even -= temp
			//even : a,b,c-a,d-b,e-c,f-d,g-e,h-f
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			//even += 4,4,4,4,4,4,4,4;
			//even : a+4,b+4,c-a+4,d-b+4,e-c+4,f-d+4,g-e+4,h-f+4
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			//even >>= 3;
			//even : (a+4)/8,(b+4)/8,(c-a+4)/8,(d-b+4)/8,(e-c+4)/8,(f-d+4)/8,(g-e+4)/8,(h-f+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);
			//even += temp
			//even : (a+4)/8+0,(b+4)/8+a,(c-a+4)/8+b,(d-b+4)/8+c,(e-c+4)/8+d,(f-d+4)/8+e,(g-e+4)/8+f,(h-f+4)/8+g


			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			high_epi16 = high1_epi16;

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, high_epi16);
			//even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			//odd = low1
			//odd : 0,0,a,b,c,d,e,f,g,h
			temp_epi16 = low1_epi16;
			//temp >>= 16*2
			//temp : a,b,c,d,e,f,g,h
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			//odd -= temp
			//odd : a,b,a-c,b-d,c-e,d-f,e-g,f-h
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			//odd += 4,4,4,4,4,4,4,4;
			//odd : a+4,b+4,a-c+4,b-d+4,c-e+4,d-f+4,e-g+4,f-h+4
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			//odd >>= 3;
			//odd : (a+4)/8,(b+4)/8,(a-c+4)/8,(b-d+4)/8,(c-e+4)/8,(d-f+4)/8,(e-g+4)/8,(f-h+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);
			//odd += temp
			//odd : (a+4)/8+0,(b+4)/8+a,(a-c+4)/8+b,(b-d+4)/8+c,(c-e+4)/8+d,(d-f+4)/8+e,(e-g+4)/8+f,(f-h+4)/8+g


			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_subs_epi16(odd_epi16, high_epi16);
			//odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
			//out_epi16 = _mm_max_epi16(out_epi16, _mm_setzero_si64());


			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, even, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, odd, 1);

			// Remove any scaling used during encoding
		//	out_epi16 = _mm_sll_epi16(out_epi16, descale_si128);
			out_epi16 = _mm_adds_epi16(out_epi16, out_epi16);

			// Store eight output values
			_mm_store_si128(outptr++, out_epi16);

			// Save the remaining two output values
			even = (short)temp;
			odd = (short)(temp >> 16);

			// The second four lowpass coefficients will be the current values
			low1_epi16 = low2_epi16;

			// The second four highpass coefficients will be the current values
			high1_epi16 = high2_epi16;
		}

		// Should have exited the loop with the column equal to the post processing column
		assert(column == post_column);

#endif

		// The fast processing loop is one column behind the actual column
		column++;

		// Get the pointer to the next output value
		colptr = (PIXEL *)outptr;

#if _UNALIGNED
		// The last two output points have already been stored
#elif 0
		// Store the last two output points produced by the loop
		*(colptr++) = remainder[0];
		*(colptr++) = remainder[1];
#else
		// Store the last two output points produced by the loop
		even <<= descaleshift;
		odd <<= descaleshift;

		*(colptr++) = SATURATE(even);
		*(colptr++) = SATURATE(odd);
#endif

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column++)
		{
			int even = 0;		// Result of convolution with even filter
			int odd = 0;		// Result of convolution with odd filter

			// Apply the even reconstruction filter to the lowpass band
			even += lowpass[column - 1];
			even -= lowpass[column + 1];
			even += 4;  //DAN20050921
			even >>= 3;
			even += lowpass[column + 0];

			// Add the highpass correction
			even += highpass[column];
			//even = DivideByShift(even, 1);

			// Remove any scaling used during encoding
			even <<= descaleshift;

			// Place the even result in the even column
			*(colptr++) = SATURATE(even);

			// Apply the odd reconstruction filter to the lowpass band
			odd -= lowpass[column - 1];
			odd += lowpass[column + 1];
			odd += 4;  //DAN20050921
			odd >>= 3;
			odd += lowpass[column + 0];

			// Subtract the highpass correction
			odd -= highpass[column];
			//odd = DivideByShift(odd, 1);

			// Remove any scaling used during encoding
			odd <<= descaleshift;

			// Place the odd result in the odd column
			*(colptr++) = SATURATE(odd);
		}

		// Should have exited the loop at the column for right border processing
		assert(column == last_column);

		// Process the last two output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 5 * lowpass[column + 0];
		even += 4 * lowpass[column - 1];
		even -= 1 * lowpass[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		//even = DivideByShift(even, 1);

		// Remove any scaling used during encoding
		even <<= descaleshift;

		// Place the even result in the even column
		*(colptr++) = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * lowpass[column + 0];
		odd -=  4 * lowpass[column - 1];
		odd +=  1 * lowpass[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		//odd = DivideByShift(odd, 1);

		// Remove any scaling used during encoding
		odd <<= descaleshift;

		// Place the odd result in the odd column
		*(colptr++) = SATURATE(odd);

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		output += output_pitch;
	}
}
#endif //P4



#if _PROCESSOR_DISPATCH
__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertHorizontalStrip1x16s(PIXEL *lowpass_band, int lowpass_pitch,
								PIXEL *highpass_band, int highpass_pitch,
								PIXEL *output_image, int output_pitch,
								ROI roi)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC
#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

void InvertHorizontalStrip1x16s(PIXEL *lowpass_band, int lowpass_pitch,
								PIXEL *highpass_band, int highpass_pitch,
								PIXEL *output_image, int output_pitch,
								ROI roi)
{
	int InvertHorizontalStrip1x16s_MMX_not_done =  1;
	assert(InvertHorizontalStrip1x16s_MMX_not_done);
}
#endif // _PROCESSOR_GENERIC


#if _PROCESSOR_PENTIUM_4
#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Apply the inverse horizontal transform to reconstruct a strip of rows
void InvertHorizontalStrip1x16s(PIXEL *lowpass_band, int lowpass_pitch,
								PIXEL *highpass_band, int highpass_pitch,
								PIXEL *output_image, int output_pitch,
								ROI roi)
{
	int height = roi.height;
	int width = roi.width;
	PIXEL *lowpass = lowpass_band;
	PIXEL *highpass = highpass_band;
	PIXEL *output = output_image;
	const int column_step = 8;
	const int last_column = width - 1;
	int post_column = last_column - (last_column % column_step);
	int row;

	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL);

	// Adjust the end of the fast loop if necessary
	if (post_column == last_column)
		post_column -= column_step;

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		__m128i low1_epi16;		// Lowpass coefficients
		__m128i low2_epi16;
		__m128i high1_epi16;	// Highpass coefficients
		__m128i high2_epi16;

		PIXEL *colptr;

		int32_t even;
		int32_t odd;

#if _UNALIGNED
		// The fast loop computes output points starting at the third column
		__m128i *outptr = (__m128i *)&output[2];
#else
		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr = (__m128i *)&output[0];

		// Two 16-bit coefficients from the previous loop iteration
		//short remainder[2];
#endif
		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * lowpass[column + 0];
		even -=  4 * lowpass[column + 1];
		even +=  1 * lowpass[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		//even = DivideByShift(even, 1);

#if _UNALIGNED
		// Place the even result in the even column
		output[0] = SATURATE(even);
#else
		// The output value will be stored later
		//remainder[0] = SATURATE(even);
#endif
		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * lowpass[column + 0];
		odd += 4 * lowpass[column + 1];
		odd -= 1 * lowpass[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		//odd = DivideByShift(odd, 1);

#if _UNALIGNED
		// Place the odd result in the odd column
		output[1] = SATURATE(odd);
#else
		// The output value will be stored later
		//remainder[1] = SATURATE(odd);
#endif

#if (_FASTLOOP && XMMOPT)

		// Preload the first four lowpass coefficients
		low1_epi16 = _mm_load_si128((__m128i *)&lowpass[column]);
		//low1 : a,b,c,d,e,f,g,h

		// Preload the first four highpass coefficients
		high1_epi16 = _mm_load_si128((__m128i *)&highpass[column]);
		//high1 : A,B,C,D,E,F,G,H

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i out_epi16;		// Reconstructed data
			//__m128i mask_epi16;
			__m128i half_epi16;
			//__m128i lsb_epi16;
			//__m128i sign_epi16;
			__m128i high_epi16;

			uint32_t temp;		// Temporary register for last two values

			// Preload the next four lowpass coefficients
			low2_epi16 = _mm_load_si128((__m128i *)&lowpass[column+8]);


			// Compute the first two even and two odd output points //


			half_epi16 = _mm_set1_epi16(4); //was 4 but 7 makes for more accurate rounding to prevent luma/chroma shifts -- DAN 6/2/03

//DAN031304 -- correct inverse filter
			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			//even = low1
			//even : a,b,c,d,e,f,g,h
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			//temp >>= 16*2
			//temp : 0,0,a,b,c,d,e,f
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			//even -= temp
			//even : a,b,c-a,d-b,e-c,f-d,g-e,h-f
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			//even += 4,4,4,4,4,4,4,4;
			//even : a+4,b+4,c-a+4,d-b+4,e-c+4,f-d+4,g-e+4,h-f+4
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			//even >>= 3;
			//even : (a+4)/8,(b+4)/8,(c-a+4)/8,(d-b+4)/8,(e-c+4)/8,(f-d+4)/8,(g-e+4)/8,(h-f+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);
			//even += temp
			//even : (a+4)/8+0,(b+4)/8+a,(c-a+4)/8+b,(d-b+4)/8+c,(e-c+4)/8+d,(f-d+4)/8+e,(g-e+4)/8+f,(h-f+4)/8+g



			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);
			//high1 >>= 8*2; 	//128bit
			//high1 : 0,A,B,C,D,E,F,G

			// Prescale for 8bit output - DAN 4/5/02
			high_epi16 = high1_epi16;
			//high = high1
			//high " 0,A,B,C,D,E,F,G

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, high_epi16);
			//even += high
			//even : a/8,A+(b+8a)/8,B+(c+8b-a)/8,C+(d+8c-b)/8,D+(e+8d-c)/8,E+(f+8e-d)/8,F+(g+8f-e)/8,G+(h+8g-f)/8
			//even_epi16 = _mm_srai_epi16(even_epi16, 1);
			//even >>= 1
			//even : a/16,(A+(b+8a)/8)/2,(B+(c+8b-a)/8)/2,(C+(d+8c-b)/8)/2,(D+(e+8d-c)/8)/2,(E+(f+8e-d)/8)/2,(F+(g+8f-e)/8)/2,(G+(h+8g-f)/8)/2

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			//odd = low1
			//odd : 0,0,a,b,c,d,e,f,g,h
			temp_epi16 = low1_epi16;
			//temp >>= 16*2
			//temp : a,b,c,d,e,f,g,h
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			//odd -= temp
			//odd : a,b,a-c,b-d,c-e,d-f,e-g,f-h
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			//odd += 4,4,4,4,4,4,4,4;
			//odd : a+4,b+4,a-c+4,b-d+4,c-e+4,d-f+4,e-g+4,f-h+4
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			//odd >>= 3;
			//odd : (a+4)/8,(b+4)/8,(a-c+4)/8,(b-d+4)/8,(c-e+4)/8,(d-f+4)/8,(e-g+4)/8,(f-h+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);
			//odd += temp
			//odd : (a+4)/8+0,(b+4)/8+a,(a-c+4)/8+b,(b-d+4)/8+c,(c-e+4)/8+d,(d-f+4)/8+e,(e-g+4)/8+f,(f-h+4)/8+g



			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_subs_epi16(odd_epi16, high_epi16);
			//odd -= high
			//odd : -a/8,(8a-b)/8-A,(8b+a-c)/8-B,(8c+b-d)/8-C,(8d+c-e)/8-D,(8e+d-f)/8-E,(8f+e-g)/8-F,(8g+f-h)/8-G
			//odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
			//odd >>= 1;
			//odd : -a/16,((8a-b)/8-A)/2,((8b+a-c)/8-B)/2,((8c+b-d)/8-C)/2,((8d+c-e)/8-D)/2,((8e+d-f)/8-E)/2,((8f+e-g)/8-F)/2,((8g+f-h)/8-G)/2

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
			//out = ((8d+c-e)/8-D)/2,(D+(e+8d-c)/8)/2, ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2


			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			//out : ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2, ((8d+c-e)/8-D)/2,(D+(e+8d-c)/8)/2
			temp = _mm_cvtsi128_si32(out_epi16);
			//temp32 =  ((8d+c-e)/8-D)/2,(D+(e+8d-c)/8)/2
			out_epi16 = _mm_insert_epi16(out_epi16, even, 0);
			//out : ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2, ((8d+c-e)/8-D)/2, even32
			out_epi16 = _mm_insert_epi16(out_epi16, odd, 1);
			//out : ((8e+d-f)/8-E)/2,(E+(f+8e-d)/8)/2, ((8f+e-g)/8-F)/2,(F+(g+8f-e)/8)/2, ((8g+f-h)/8-G)/2,(G+(h+8g-f)/8)/2, odd16, even16

			// Store eight output values
			_mm_store_si128(outptr++, out_epi16);

			// Save the remaining two output values
			even = (short)temp;
			odd = (short)(temp >> 16);




			// Compute the second two even and two odd output points //

			// Preload the highpass correction
			high2_epi16 = _mm_load_si128((__m128i *)&highpass[column+8]);

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);


			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			//even = low1
			//even : a,b,c,d,e,f,g,h
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			//temp >>= 16*2
			//temp : 0,0,a,b,c,d,e,f
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			//even -= temp
			//even : a,b,c-a,d-b,e-c,f-d,g-e,h-f
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			//even += 4,4,4,4,4,4,4,4;
			//even : a+4,b+4,c-a+4,d-b+4,e-c+4,f-d+4,g-e+4,h-f+4
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			//even >>= 3;
			//even : (a+4)/8,(b+4)/8,(c-a+4)/8,(d-b+4)/8,(e-c+4)/8,(f-d+4)/8,(g-e+4)/8,(h-f+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);
			//even += temp
			//even : (a+4)/8+0,(b+4)/8+a,(c-a+4)/8+b,(d-b+4)/8+c,(e-c+4)/8+d,(f-d+4)/8+e,(g-e+4)/8+f,(h-f+4)/8+g



			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			high_epi16 = high1_epi16;

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, high_epi16);
			//even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			//odd = low1
			//odd : 0,0,a,b,c,d,e,f,g,h
			temp_epi16 = low1_epi16;
			//temp >>= 16*2
			//temp : a,b,c,d,e,f,g,h
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			//odd -= temp
			//odd : a,b,a-c,b-d,c-e,d-f,e-g,f-h
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			//odd += 4,4,4,4,4,4,4,4;
			//odd : a+4,b+4,a-c+4,b-d+4,c-e+4,d-f+4,e-g+4,f-h+4
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			//odd >>= 3;
			//odd : (a+4)/8,(b+4)/8,(a-c+4)/8,(b-d+4)/8,(c-e+4)/8,(d-f+4)/8,(e-g+4)/8,(f-h+4)/8
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			//temp = low1 >> 16
			//temp : 0,a,b,c,d,e,f,g
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);
			//odd += temp
			//odd : (a+4)/8+0,(b+4)/8+a,(a-c+4)/8+b,(b-d+4)/8+c,(c-e+4)/8+d,(d-f+4)/8+e,(e-g+4)/8+f,(f-h+4)/8+g



			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_subs_epi16(odd_epi16, high_epi16);
			//odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
			//out_epi16 = _mm_max_epi16(out_epi16, _mm_setzero_si64());



			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, even, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, odd, 1);

			// Store eight output values
			_mm_store_si128(outptr++, out_epi16);

			// Save the remaining two output values
			even = (short)temp;
			odd = (short)(temp >> 16);

			// The second four lowpass coefficients will be the current values
			low1_epi16 = low2_epi16;

			// The second four highpass coefficients will be the current values
			high1_epi16 = high2_epi16;
		}

		// Should have exited the loop with the column equal to the post processing column
		assert(column == post_column);

#endif

		// The fast processing loop is one column behind the actual column
		column++;

		// Get the pointer to the next output value
		colptr = (PIXEL *)outptr;

#if _UNALIGNED
		// The last two output points have already been stored
#elif 0
		// Store the last two output points produced by the loop
		*(colptr++) = remainder[0];
		*(colptr++) = remainder[1];
#else
		// Store the last two output points produced by the loop
		*(colptr++) = SATURATE(even);
		*(colptr++) = SATURATE(odd);
#endif

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column++)
		{
			short even = 0;		// Result of convolution with even filter
			short odd = 0;		// Result of convolution with odd filter

			// Apply the even reconstruction filter to the lowpass band
			even += lowpass[column - 1];
			even -= lowpass[column + 1];
			even += 4;   //DAN20050921
			even >>= 3;
			even += lowpass[column + 0];

			// Add the highpass correction
			even += highpass[column];
			//even = DivideByShift(even, 1);

			// Place the even result in the even column
			*(colptr++) = SATURATE(even);

			// Apply the odd reconstruction filter to the lowpass band
			odd -= lowpass[column - 1];
			odd += lowpass[column + 1];
			odd += 4;  //DAN20050921
			odd >>= 3;
			odd += lowpass[column + 0];

			// Subtract the highpass correction
			odd -= highpass[column];
			//odd = DivideByShift(odd, 1);

			// Place the odd result in the odd column
			*(colptr++) = SATURATE(odd);
		}

		// Should have exited the loop at the column for right border processing
		assert(column == last_column);

		// Process the last two output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 5 * lowpass[column + 0];
		even += 4 * lowpass[column - 1];
		even -= 1 * lowpass[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		//even = DivideByShift(even, 1);

		// Place the even result in the even column
		*(colptr++) = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * lowpass[column + 0];
		odd -=  4 * lowpass[column - 1];
		odd +=  1 * lowpass[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		//odd = DivideByShift(odd, 1);

		// Place the odd result in the odd column
		*(colptr++) = SATURATE(odd);

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		output += output_pitch;
	}
}
#endif //P4

#if 0
// Apply the inverse horizontal transform to reconstruct a strip of prescaled rows
void InvertHorizontalStripScaled16s(PIXEL *lowpass_band,	// Horizontal lowpass coefficients
									int lowpass_pitch,		// Distance between rows in bytes
									PIXEL *highpass_band,	// Horizontal highpass coefficients
									int highpass_pitch,		// Distance between rows in bytes
									PIXEL *output_image,	// Row of reconstructed results
									int output_pitch,		// Distance between rows in bytes
									ROI roi)				// Height and width of the strip
{
	int height = roi.height;
	int width = roi.width;
	PIXEL *lowpass = lowpass_band;
	PIXEL *highpass = highpass_band;
	PIXEL *output = output_image;
	const int column_step = 4;
	const int last_column = width - 1;
	int post_column = last_column - (last_column % column_step);
	int row;

	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL);

	// Adjust the end of the fast loop if necessary
	if (post_column == last_column)
		post_column -= column_step;

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		__m64 low1_pi16;	// Lowpass coefficients
		__m64 low2_pi16;
		__m64 high1_pi16;	// Highpass coefficients
		__m64 high2_pi16;

		PIXEL *colptr;

		int32_t even;
		int32_t odd;

		// The fast loop computes output points starting at the third column
		__m64 *outptr = (__m64 *)&output[2];

		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * lowpass[column + 0];
		even -=  4 * lowpass[column + 1];
		even +=  1 * lowpass[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even = DivideByShift(even, 1);

		// Place the even result in the even column
		even <<= V210_HORIZONTAL_SHIFT;
		output[0] = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * lowpass[column + 0];
		odd += 4 * lowpass[column + 1];
		odd -= 1 * lowpass[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd = DivideByShift(odd, 1);

		// Place the odd result in the odd column
		odd <<= V210_HORIZONTAL_SHIFT;
		output[1] = SATURATE(odd);

#if (0 && XMMOPT)

		// Preload the first four lowpass coefficients
		low1_pi16 = *((__m64 *)&lowpass[column]);

		// Preload the first four highpass coefficients
		high1_pi16 = *((__m64 *)&highpass[column]);

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m64 even_pi16;	// Result of convolution with even filter
			__m64 odd_pi16;		// Result of convolution with odd filter
			__m64 temp_pi16;
			__m64 out_pi16;		// Reconstructed data
			__m64 mask_pi16;
			__m64 half_pi16;
			__m64 lsb_pi16;
			__m64 sign_pi16;
			__m64 high_pi16;

			// Preload the next four lowpass coefficients
			low2_pi16 = *((__m64 *)&lowpass[column+4]);


			/***** Compute the first two even and two odd output points *****/

			// Apply the even reconstruction filter to the lowpass band
			even_pi16 = low1_pi16;
//DAN031304 -- wrong inverse filter
			assert(0); //fix filter
			temp_pi16 = _mm_slli_pi16(low1_pi16, 3);
			temp_pi16 = _mm_shuffle_pi16(temp_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16);
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16);

			// Apply the rounding adjustment
			even_pi16 = _mm_adds_pi16(even_pi16, _mm_set1_pi16(4));
			// Divide by eight
			even_pi16 = _mm_srai_pi16(even_pi16, 3);
			//even_pi16 = _mm_srli_pi16(even_pi16, 3);

			// Shift the highpass correction by one column
			high1_pi16 = _mm_srli_si64(high1_pi16, 16);

			// Prescale for 8bit output - DAN 4/5/02
			high_pi16 = high1_pi16;

			// Add the highpass correction
			even_pi16 = _mm_adds_pi16(even_pi16, high_pi16);
			even_pi16 = _mm_srai_pi16(even_pi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_pi16 = _mm_slli_pi16(low1_pi16, 3);
			odd_pi16 = _mm_shuffle_pi16(odd_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16);
			odd_pi16 = _mm_subs_pi16(odd_pi16, low1_pi16);

			// Apply the rounding adjustment
			odd_pi16 = _mm_adds_pi16(odd_pi16, _mm_set1_pi16(4));
			// Divide by eight
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);
			//odd_pi16 = _mm_srli_pi16(odd_pi16, 3);

			// Subtract the highpass correction
			odd_pi16 = _mm_subs_pi16(odd_pi16, high_pi16);
			odd_pi16 = _mm_srai_pi16(odd_pi16, 1);

			// Interleave the even and odd results
			out_pi16 = _mm_unpacklo_pi16(even_pi16, odd_pi16);
			//out_pi16 = _mm_max_pi16(out_pi16, _mm_setzero_si64());

			// Scale the results
			out_pi16 = _mm_slli_pi16(out_pi16, V210_HORIZONTAL_SHIFT);

			// Store the results
			*(outptr++) = out_pi16;


			/***** Compute the second two even and two odd output points *****/

			// Preload the highpass correction
			high2_pi16 = *((__m64 *)&highpass[column+4]);

			// Shift in the new pixels for the next stage of the loop
			low1_pi16 = _mm_srli_si64(low1_pi16, 32);
			temp_pi16 = _mm_slli_si64(low2_pi16, 32);
			low1_pi16 = _mm_or_si64(low1_pi16, temp_pi16);

			// Apply the even reconstruction filter to the lowpass band
			even_pi16 = low1_pi16;
			temp_pi16 = _mm_slli_pi16(low1_pi16, 3);
			temp_pi16 = _mm_shuffle_pi16(temp_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16);
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16);

			// Apply the rounding adjustment
			even_pi16 = _mm_adds_pi16(even_pi16, _mm_set1_pi16(4));
			// Divide by eight
			even_pi16 = _mm_srai_pi16(even_pi16, 3);
			//even_pi16 = _mm_srli_pi16(even_pi16, 3);

			// Shift in the next two highpass coefficients
			high1_pi16 = _mm_srli_si64(high1_pi16, 2*16);
			high1_pi16 = _mm_or_si64(high1_pi16, _mm_slli_si64(high2_pi16, 16));

			// Prescale for 8-bit output - DAN 4/5/02
			high_pi16 = high1_pi16;

			// Add the highpass correction
			even_pi16 = _mm_adds_pi16(even_pi16, high_pi16);
			even_pi16 = _mm_srai_pi16(even_pi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_pi16 = _mm_slli_pi16(low1_pi16, 3);
			odd_pi16 = _mm_shuffle_pi16(odd_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16);
			odd_pi16 = _mm_subs_pi16(odd_pi16, low1_pi16);

			// Apply the rounding adjustment
			odd_pi16 = _mm_adds_pi16(odd_pi16, _mm_set1_pi16(4));
			// Divide by eight
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);
			//odd_pi16 = _mm_srli_pi16(odd_pi16, 3);

			// Subtract the highpass correction
			odd_pi16 = _mm_subs_pi16(odd_pi16, high_pi16);
			odd_pi16 = _mm_srai_pi16(odd_pi16, 1);

			// Interleave the even and odd results
			out_pi16 = _mm_unpacklo_pi16(even_pi16, odd_pi16);
			//out_pi16 = _mm_max_pi16(out_pi16, _mm_setzero_si64());

			// Scale the results
			out_pi16 = _mm_slli_pi16(out_pi16, V210_HORIZONTAL_SHIFT);

			// Store the results
			*(outptr++) = out_pi16;

			// The second four lowpass coefficients will be the current values
			low1_pi16 = low2_pi16;

			// The second four highpass coefficients will be the current values
			high1_pi16 = high2_pi16;
		}

		// Should have exited the loop with the column equal to the post processing column
		assert(column == post_column);

#endif

		// The fast processing loop is one column behind the actual column
		column++;

		// Process the rest of the columns up to the last column in the row
		colptr = (PIXEL *)outptr;

		for (; column < last_column; column++)
		{
			int32_t even = 0;		// Result of convolution with even filter
			int32_t odd = 0;		// Result of convolution with odd filter

			// Apply the even reconstruction filter to the lowpass band
			even += lowpass[column - 1];
			even -= lowpass[column + 1];
			even += ROUNDING(even,8);
			even = DivideByShift(even, 3);
			even += lowpass[column + 0];

			// Add the highpass correction
			even += highpass[column];
			even = DivideByShift(even, 1);

			// Place the even result in the even column
			even <<= V210_HORIZONTAL_SHIFT;
			*(colptr++) = SATURATE(even);

			// Apply the odd reconstruction filter to the lowpass band
			odd -= lowpass[column - 1];
			odd += lowpass[column + 1];
			odd += ROUNDING(odd,8);
			odd = DivideByShift(odd, 3);
			odd += lowpass[column + 0];

			// Subtract the highpass correction
			odd -= highpass[column];
			odd = DivideByShift(odd, 1);

			// Place the odd result in the odd column
			odd <<= V210_HORIZONTAL_SHIFT;
			*(colptr++) = SATURATE(odd);
		}

		// Should have exited the loop at the column for right border processing
		assert(column == last_column);

		// Process the last two output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 5 * lowpass[column + 0];
		even += 4 * lowpass[column - 1];
		even -= 1 * lowpass[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even = DivideByShift(even, 1);

		// Place the even result in the even column
		even <<= V210_HORIZONTAL_SHIFT;
		*(colptr++) = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * lowpass[column + 0];
		odd -=  4 * lowpass[column - 1];
		odd +=  1 * lowpass[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd = DivideByShift(odd, 1);

		// Place the odd result in the odd column
		odd <<= V210_HORIZONTAL_SHIFT;
		*(colptr++) = SATURATE(odd);

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		output += output_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}
#endif

#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void InvertHorizontalStripPrescaled16s(PIXEL *lowpass_band,		// Horizontal lowpass coefficients
									   int lowpass_pitch,		// Distance between rows in bytes
									   PIXEL *highpass_band,	// Horizontal highpass coefficients
									   int highpass_pitch,		// Distance between rows in bytes
									   PIXEL *output_image,		// Row of reconstructed results
									   int output_pitch,		// Distance between rows in bytes
									   ROI roi)					// Height and width of the strip
{
	// Stub routine for processor specific dispatch
}

#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Apply the inverse horizontal transform to reconstruct a strip of prescaled rows
void InvertHorizontalStripPrescaled16s(PIXEL *lowpass_band,		// Horizontal lowpass coefficients
									   int lowpass_pitch,		// Distance between rows in bytes
									   PIXEL *highpass_band,	// Horizontal highpass coefficients
									   int highpass_pitch,		// Distance between rows in bytes
									   PIXEL *output_image,		// Row of reconstructed results
									   int output_pitch,		// Distance between rows in bytes
									   ROI roi)					// Height and width of the strip
{
	int height = roi.height;
	int width = roi.width;
	PIXEL *lowpass = lowpass_band;
	PIXEL *highpass = highpass_band;
	PIXEL *output = output_image;
	const int column_step = 4;
	const int last_column = width - 1;
	int post_column = last_column - (last_column % column_step);
	int row;

	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL);

	// Adjust the end of the fast loop if necessary
	if (post_column == last_column)
		post_column -= column_step;

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		__m64 low1_pi16;	// Lowpass coefficients
		__m64 low2_pi16;
		__m64 high1_pi16;	// Highpass coefficients
		__m64 high2_pi16;

		PIXEL *colptr;

		int32_t even;
		int32_t odd;

		// The fast loop computes output points starting at the third column
		__m64 *outptr = (__m64 *)&output[2];

		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * lowpass[column + 0];
		even -=  4 * lowpass[column + 1];
		even +=  1 * lowpass[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];

#if (_LOWPASS_PRESCALE == 0)
		even = DivideByShift(even, 1);
#elif (_LOWPASS_PRESCALE > 1)
		even <<= (_LOWPASS_PRESCALE - 1);
#endif
		// Place the even result in the even column
		output[0] = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * lowpass[column + 0];
		odd += 4 * lowpass[column + 1];
		odd -= 1 * lowpass[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];

#if (_LOWPASS_PRESCALE == 0)
		odd = DivideByShift(odd, 1);
#elif (_LOWPASS_PRESCALE > 1)
		odd <<= (_LOWPASS_PRESCALE - 1);
#endif
		// Place the odd result in the odd column
		output[1] = SATURATE(odd);

#if (1 && XMMOPT)

		// Preload the first four lowpass coefficients
		low1_pi16 = *((__m64 *)&lowpass[column]);

		// Preload the first four highpass coefficients
		high1_pi16 = *((__m64 *)&highpass[column]);

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m64 even_pi16;	// Result of convolution with even filter
			__m64 odd_pi16;		// Result of convolution with odd filter
			__m64 temp_pi16;
			__m64 out_pi16;		// Reconstructed data
			__m64 mask_pi16;
			__m64 half_pi16;
			__m64 lsb_pi16;
			__m64 sign_pi16;
			__m64 high_pi16;

			// Preload the next four lowpass coefficients
			low2_pi16 = *((__m64 *)&lowpass[column+4]);


			// Compute the first two even and two odd output points //

			// Apply the even reconstruction filter to the lowpass band
//DAN031304 -- wrong inverse filter
			assert(0); //fix filter
			even_pi16 = low1_pi16;
			temp_pi16 = _mm_slli_pi16(low1_pi16, 3);
			temp_pi16 = _mm_shuffle_pi16(temp_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16);
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16);

			// Apply the rounding adjustment
			even_pi16 = _mm_adds_pi16(even_pi16, _mm_set1_pi16(4));
			// Divide by eight
			even_pi16 = _mm_srai_pi16(even_pi16, 3);

			// Shift the highpass correction by one column
			high1_pi16 = _mm_srli_si64(high1_pi16, 16);

			// Prescale for 8bit output - DAN 4/5/02
			high_pi16 = high1_pi16;

			// Add the highpass correction
			even_pi16 = _mm_adds_pi16(even_pi16, high_pi16);

#if (_LOWPASS_PRESCALE == 0)
			even_pi16 = _mm_srai_pi16(even_pi16, 1);
#elif (_LOWPASS_PRESCALE > 1)
			even_pi16 = _mm_slli_pi16(even_pi16, (_LOWPASS_PRESCALE - 1));
#endif
			// Apply the odd reconstruction filter to the lowpass band
			odd_pi16 = _mm_slli_pi16(low1_pi16, 3);
			odd_pi16 = _mm_shuffle_pi16(odd_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16);
			odd_pi16 = _mm_subs_pi16(odd_pi16, low1_pi16);

			// Apply the rounding adjustment
			odd_pi16 = _mm_adds_pi16(odd_pi16, _mm_set1_pi16(4));
			// Divide by eight
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);

			// Subtract the highpass correction
			odd_pi16 = _mm_subs_pi16(odd_pi16, high_pi16);

#if (_LOWPASS_PRESCALE == 0)
			odd_pi16 = _mm_srai_pi16(odd_pi16, 1);
#elif (_LOWPASS_PRESCALE > 1)
			odd_pi16 = _mm_slli_pi16(odd_pi16, (_LOWPASS_PRESCALE - 1));
#endif
			// Interleave the even and odd results
			out_pi16 = _mm_unpacklo_pi16(even_pi16, odd_pi16);
			//out_pi16 = _mm_max_pi16(out_pi16, _mm_setzero_si64());
			*(outptr++) = out_pi16;


			// Compute the second two even and two odd output points //

			// Preload the highpass correction
			high2_pi16 = *((__m64 *)&highpass[column+4]);

			// Shift in the new pixels for the next stage of the loop
			low1_pi16 = _mm_srli_si64(low1_pi16, 32);
			temp_pi16 = _mm_slli_si64(low2_pi16, 32);
			low1_pi16 = _mm_or_si64(low1_pi16, temp_pi16);

			// Apply the even reconstruction filter to the lowpass band
			even_pi16 = low1_pi16;
			temp_pi16 = _mm_slli_pi16(low1_pi16, 3);
			temp_pi16 = _mm_shuffle_pi16(temp_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16);
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16);

			// Apply the rounding adjustment
			even_pi16 = _mm_adds_pi16(even_pi16, _mm_set1_pi16(4));
			// Divide by eight
			even_pi16 = _mm_srai_pi16(even_pi16, 3);

			// Shift in the next two highpass coefficients
			high1_pi16 = _mm_srli_si64(high1_pi16, 2*16);
			high1_pi16 = _mm_or_si64(high1_pi16, _mm_slli_si64(high2_pi16, 16));

			// Prescale for 8-bit output - DAN 4/5/02
			high_pi16 = high1_pi16;

			// Add the highpass correction
			even_pi16 = _mm_adds_pi16(even_pi16, high_pi16);

#if (_LOWPASS_PRESCALE == 0)
			even_pi16 = _mm_srai_pi16(even_pi16, 1);
#elif (_LOWPASS_PRESCALE > 1)
			even_pi16 = _mm_slli_pi16(even_pi16, (_LOWPASS_PRESCALE - 1));
#endif
			// Apply the odd reconstruction filter to the lowpass band
			odd_pi16 = _mm_slli_pi16(low1_pi16, 3);
			odd_pi16 = _mm_shuffle_pi16(odd_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16);
			odd_pi16 = _mm_subs_pi16(odd_pi16, low1_pi16);

			// Apply the rounding adjustment
			odd_pi16 = _mm_adds_pi16(odd_pi16, _mm_set1_pi16(4));
			// Divide by eight
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);

			// Subtract the highpass correction
			odd_pi16 = _mm_subs_pi16(odd_pi16, high_pi16);
#if (_LOWPASS_PRESCALE == 0)
			odd_pi16 = _mm_srai_pi16(odd_pi16, 1);
#elif (_LOWPASS_PRESCALE > 1)
			odd_pi16 = _mm_slli_pi16(odd_pi16, (_LOWPASS_PRESCALE - 1));
#endif
			// Interleave the even and odd results
			out_pi16 = _mm_unpacklo_pi16(even_pi16, odd_pi16);
			//out_pi16 = _mm_max_pi16(out_pi16, _mm_setzero_si64());
			*(outptr++) = out_pi16;

			// The second four lowpass coefficients will be the current values
			low1_pi16 = low2_pi16;

			// The second four highpass coefficients will be the current values
			high1_pi16 = high2_pi16;
		}

		// Should have exited the loop with the column equal to the post processing column
		assert(column == post_column);

#endif

		// The fast processing loop is one column behind the actual column
		column++;

		// Process the rest of the columns up to the last column in the row
		colptr = (PIXEL *)outptr;

		for (; column < last_column; column++)
		{
			int32_t even = 0;		// Result of convolution with even filter
			int32_t odd = 0;		// Result of convolution with odd filter

			// Apply the even reconstruction filter to the lowpass band
			even += lowpass[column - 1];
			even -= lowpass[column + 1];
			even += ROUNDING(even,8);
			even = DivideByShift(even, 3);
			even += lowpass[column + 0];

			// Add the highpass correction
			even += highpass[column];

#if (_LOWPASS_PRESCALE == 0)
			even = DivideByShift(even, 1);
#elif (_LOWPASS_PRESCALE > 1)
			even <<= (_LOWPASS_PRESCALE - 1);
#endif
			// Place the even result in the even column
			*(colptr++) = SATURATE(even);

			// Apply the odd reconstruction filter to the lowpass band
			odd -= lowpass[column - 1];
			odd += lowpass[column + 1];
			odd += ROUNDING(odd,8);
			odd = DivideByShift(odd, 3);
			odd += lowpass[column + 0];

			// Subtract the highpass correction
			odd -= highpass[column];

#if (_LOWPASS_PRESCALE == 0)
			odd = DivideByShift(odd, 1);
#elif (_LOWPASS_PRESCALE > 1)
			odd <<= (_LOWPASS_PRESCALE - 1);
#endif
			// Place the odd result in the odd column
			*(colptr++) = SATURATE(odd);
		}

		// Should have exited the loop at the column for right border processing
		assert(column == last_column);

		// Process the last two output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 5 * lowpass[column + 0];
		even += 4 * lowpass[column - 1];
		even -= 1 * lowpass[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];

#if (_LOWPASS_PRESCALE == 0)
		even = DivideByShift(even, 1);
#elif (_LOWPASS_PRESCALE > 1)
		even <<= (_LOWPASS_PRESCALE - 1);
#endif
		// Place the even result in the even column
		*(colptr++) = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * lowpass[column + 0];
		odd -=  4 * lowpass[column - 1];
		odd +=  1 * lowpass[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];

#if (_LOWPASS_PRESCALE == 0)
		odd = DivideByShift(odd, 1);
#elif (_LOWPASS_PRESCALE > 1)
		odd <<= (_LOWPASS_PRESCALE - 1);
#endif
		// Place the odd result in the odd column
		*(colptr++) = SATURATE(odd);

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		output += output_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Apply the inverse horizontal transform to reconstruct a strip of prescaled rows
void InvertHorizontalStripPrescaled16s(PIXEL *lowpass_band,		// Horizontal lowpass coefficients
									   int lowpass_pitch,		// Distance between rows in bytes
									   PIXEL *highpass_band,	// Horizontal highpass coefficients
									   int highpass_pitch,		// Distance between rows in bytes
									   PIXEL *output_image,		// Row of reconstructed results
									   int output_pitch,		// Distance between rows in bytes
									   ROI roi)					// Height and width of the strip
{
	int height = roi.height;
	int width = roi.width;
	PIXEL *lowpass = lowpass_band;
	PIXEL *highpass = highpass_band;
	PIXEL *output = output_image;
	const int column_step = 8;
	const int last_column = width - 1;
	int post_column = last_column - (last_column % column_step);
	int row;

	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL);

	// Adjust the end of the fast loop if necessary
	if (post_column == last_column)
		post_column -= column_step;

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		__m128i low1_epi16;		// Lowpass coefficients
		__m128i low2_epi16;
		__m128i high1_epi16;	// Highpass coefficients
		__m128i high2_epi16;

		PIXEL *colptr;

		int32_t even;
		int32_t odd;

#if _UNALIGNED
		// The fast loop computes output points starting at the third column
		__m128i *outptr = (__m128i *)&output[2];
#else
		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr = (__m128i *)&output[0];

		// Two 16-bit coefficients from the previous loop iteration
		//short remainder[2];
#endif
		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * lowpass[column + 0];
		even -=  4 * lowpass[column + 1];
		even +=  1 * lowpass[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];

#if (_LOWPASS_PRESCALE == 0)
		even = DivideByShift(even, 1);
#elif (_LOWPASS_PRESCALE > 1)
		even <<= (_LOWPASS_PRESCALE - 1);
#endif
#if _UNALIGNED
		// Place the even result in the even column
		output[0] = SATURATE(even);
#else
		// The even result will be stored later
		//remainder[0] = SATURATE(even);
#endif
		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * lowpass[column + 0];
		odd += 4 * lowpass[column + 1];
		odd -= 1 * lowpass[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];

#if (_LOWPASS_PRESCALE == 0)
		odd = DivideByShift(odd, 1);
#elif (_LOWPASS_PRESCALE > 1)
		odd <<= (_LOWPASS_PRESCALE - 1);
#endif
#if _UNALIGNED
		// Place the odd result in the odd column
		output[1] = SATURATE(odd);
#else
		// The odd result will be stored later
		//remainder[1] = SATURATE(odd);
#endif

#if (_FASTLOOP && XMMOPT)

		// Check that the input and output addresses are properly aligned
		assert(ISALIGNED16(lowpass));
		assert(ISALIGNED16(highpass));
		//assert(ISALIGNED16(outptr));

		// Preload the first eight lowpass coefficients
		low1_epi16 = _mm_load_si128((__m128i *)&lowpass[column]);

		// Preload the first four highpass coefficients
		high1_epi16 = _mm_load_si128((__m128i *)&highpass[column]);

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i out_epi16;		// Reconstructed data
			//__m128i mask_epi16;
			//__m128i half_epi16;
			//__m128i lsb_epi16;
			//__m128i sign_epi16;
			__m128i high_epi16;
			uint32_t temp;		// Temporary register for last two values

			// Preload the next four lowpass coefficients
			low2_epi16 = _mm_load_si128((__m128i *)&lowpass[column+8]);


			// Compute the first two even and two odd output points //

			// Apply the even reconstruction filter to the lowpass band
//DAN031304 -- wrong inverse filter
			assert(0); //fix filter
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_slli_epi16(low1_epi16, 3);
			temp_epi16 = _mm_srli_si128(temp_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);
			temp_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);

			// Apply the rounding adjustment
			even_epi16 = _mm_adds_epi16(even_epi16, _mm_set1_epi16(4));
			// Divide by eight
			even_epi16 = _mm_srai_epi16(even_epi16, 3);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Prescale for 8bit output - DAN 4/5/02
			high_epi16 = high1_epi16;

			// Add the highpass correction
			even_epi16 = _mm_adds_epi16(even_epi16, high_epi16);

#if (_LOWPASS_PRESCALE == 0)
			even_epi16 = _mm_srai_epi16(even_epi16, 1);
#elif (_LOWPASS_PRESCALE > 1)
			even_epi16 = _mm_slli_epi16(even_epi16, (_LOWPASS_PRESCALE - 1));
#endif
			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_slli_epi16(low1_epi16, 3);
			odd_epi16 = _mm_srli_si128(odd_epi16, 1*2);
			temp_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, low1_epi16);

			// Apply the rounding adjustment
			odd_epi16 = _mm_adds_epi16(odd_epi16, _mm_set1_epi16(4));
			// Divide by eight
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);

			// Subtract the highpass correction
			odd_epi16 = _mm_subs_epi16(odd_epi16, high_epi16);

#if (_LOWPASS_PRESCALE == 0)
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
#elif (_LOWPASS_PRESCALE > 1)
			odd_epi16 = _mm_slli_epi16(odd_epi16, (_LOWPASS_PRESCALE - 1));
#endif
			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
			//out_epi16 = _mm_max_epi16(out_epi16, _mm_setzero_si128());

#if _UNALIGNED
			// Store the first eight output values
			_mm_storeu_si128(outptr++, out_epi16);
#elif 0
			// Combine the new output values with the two values from the previous phase
			temp_epi16 = _mm_srli_si128(out_epi16, 6*2);
			temp = _mm_cvtsi128_si32(temp_epi16);
			out_epi16 = _mm_slli_si128(out_epi16, 2*2);
			out_epi16 = _mm_or_si128(out_epi16, _mm_cvtsi32_si128(*((int *)remainder)));

			// Save the remaining two output values
			*((int *)remainder) = temp;

			// Store the first eight output values
			_mm_store_si128(outptr++, out_epi16);
#else
			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, even, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, odd, 1);

			// Store the first eight output values
			_mm_store_si128(outptr++, out_epi16);

			// Save the remaining two output values
			even = (short)temp;
			odd = (short)(temp >> 16);
#endif

			// Compute the second four even and four odd output points //

			// Preload the highpass correction
			high2_epi16 = _mm_load_si128((__m128i *)&highpass[column+8]);

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_slli_epi16(low1_epi16, 3);
			temp_epi16 = _mm_srli_si128(temp_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);
			temp_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);

			// Apply the rounding adjustment
			even_epi16 = _mm_adds_epi16(even_epi16, _mm_set1_epi16(4));
			// Divide by eight
			even_epi16 = _mm_srai_epi16(even_epi16, 3);

			// Shift in the next five highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Prescale for 8-bit output - DAN 4/5/02
			high_epi16 = high1_epi16;

			// Add the highpass correction
			even_epi16 = _mm_adds_epi16(even_epi16, high_epi16);

#if (_LOWPASS_PRESCALE == 0)
			even_epi16 = _mm_srai_epi16(even_epi16, 1);
#elif (_LOWPASS_PRESCALE > 1)
			even_epi16 = _mm_slli_epi16(even_epi16, (_LOWPASS_PRESCALE - 1));
#endif
			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_slli_epi16(low1_epi16, 3);
			odd_epi16 = _mm_srli_si128(odd_epi16, 1*2);
			temp_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, low1_epi16);

			// Apply the rounding adjustment
			odd_epi16 = _mm_adds_epi16(odd_epi16, _mm_set1_epi16(4));
			// Divide by eight
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);

			// Subtract the highpass correction
			odd_epi16 = _mm_subs_epi16(odd_epi16, high_epi16);
#if (_LOWPASS_PRESCALE == 0)
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
#elif (_LOWPASS_PRESCALE > 1)
			odd_epi16 = _mm_slli_epi16(odd_epi16, (_LOWPASS_PRESCALE - 1));
#endif
			// Interleave the even and odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
			//out_epi16 = _mm_max_epi16(out_epi16, _mm_setzero_si128());

#if _UNALIGNED
			// Store the second eight output values
			_mm_storeu_si128(outptr++, out_epi16);
#elif 0
			// Combine the new output values with the two values from the previous phase
			temp_epi16 = _mm_srli_si128(out_epi16, 6*2);
			temp = _mm_cvtsi128_si32(temp_epi16);
			out_epi16 = _mm_slli_si128(out_epi16, 2*2);
			out_epi16 = _mm_or_si128(out_epi16, _mm_cvtsi32_si128(*((int *)remainder)));

			// Save the remaining two output values
			*((int *)remainder) = temp;

			// Store the second eight output values
			_mm_store_si128(outptr++, out_epi16);
#else
			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, even, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, odd, 1);

			// Store the first eight output values
			_mm_store_si128(outptr++, out_epi16);

			// Save the remaining two output values
			even = (short)temp;
			odd = (short)(temp >> 16);
#endif
			// The second four lowpass coefficients will be the current values
			low1_epi16 = low2_epi16;

			// The second four highpass coefficients will be the current values
			high1_epi16 = high2_epi16;
		}

		// Should have exited the loop with the column equal to the post processing column
		assert(column == post_column);

#endif

		// The fast processing loop is one column behind the actual column
		column++;

		// Get the pointer to the next output value
		colptr = (PIXEL *)outptr;

#if _UNALIGNED
		// The last two output points have already been stored
#elif 0
		// Store the last two output points produced by the loop
		*(colptr++) = remainder[0];
		*(colptr++) = remainder[1];
#else
		// Store the last two output points produced by the loop
		*(colptr++) = SATURATE(even);
		*(colptr++) = SATURATE(odd);
#endif

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column++)
		{
			int32_t even = 0;		// Result of convolution with even filter
			int32_t odd = 0;		// Result of convolution with odd filter

			// Apply the even reconstruction filter to the lowpass band
			even += lowpass[column - 1];
			even -= lowpass[column + 1];
			even += ROUNDING(even,8);
			even = DivideByShift(even, 3);
			even += lowpass[column + 0];

			// Add the highpass correction
			even += highpass[column];

#if (_LOWPASS_PRESCALE == 0)
			even = DivideByShift(even, 1);
#elif (_LOWPASS_PRESCALE > 1)
			even <<= (_LOWPASS_PRESCALE - 1);
#endif
			// Place the even result in the even column
			*(colptr++) = SATURATE(even);

			// Apply the odd reconstruction filter to the lowpass band
			odd -= lowpass[column - 1];
			odd += lowpass[column + 1];
			odd += ROUNDING(odd,8);
			odd = DivideByShift(odd, 3);
			odd += lowpass[column + 0];

			// Subtract the highpass correction
			odd -= highpass[column];

#if (_LOWPASS_PRESCALE == 0)
			odd = DivideByShift(odd, 1);
#elif (_LOWPASS_PRESCALE > 1)
			odd <<= (_LOWPASS_PRESCALE - 1);
#endif
			// Place the odd result in the odd column
			*(colptr++) = SATURATE(odd);
		}

		// Should have exited the loop at the column for right border processing
		assert(column == last_column);

		// Process the last two output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 5 * lowpass[column + 0];
		even += 4 * lowpass[column - 1];
		even -= 1 * lowpass[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];

#if (_LOWPASS_PRESCALE == 0)
		even = DivideByShift(even, 1);
#elif (_LOWPASS_PRESCALE > 1)
		even <<= (_LOWPASS_PRESCALE - 1);
#endif
		// Place the even result in the even column
		*(colptr++) = SATURATE(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * lowpass[column + 0];
		odd -=  4 * lowpass[column - 1];
		odd +=  1 * lowpass[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];

#if (_LOWPASS_PRESCALE == 0)
		odd = DivideByShift(odd, 1);
#elif (_LOWPASS_PRESCALE > 1)
		odd <<= (_LOWPASS_PRESCALE - 1);
#endif
		// Place the odd result in the odd column
		*(colptr++) = SATURATE(odd);

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		output += output_pitch;
	}
}

#endif


// Apply the inverse horizontal transform to reconstruct a strip of rows into packed YUV pixels
void InvertHorizontalStrip16sToYUYV(PIXEL *lowpass_band[],	// Horizontal lowpass coefficients
									int lowpass_pitch[],	// Distance between rows in bytes
									PIXEL *highpass_band[],	// Horizontal highpass coefficients
									int highpass_pitch[],	// Distance between rows in bytes
									uint8_t *output_image,		// Row of reconstructed results
									int output_pitch,		// Distance between rows in bytes
									ROI roi,				// Height and width of the strip
									int precision)			// Precision of the original video
{
	int num_channels = CODEC_NUM_CHANNELS;
	int height = roi.height;
	int width = roi.width;

	// Note that the u and v chroma values are swapped
	PIXEL *y_lowpass_ptr = lowpass_band[0];
	PIXEL *u_lowpass_ptr = lowpass_band[2];
	PIXEL *v_lowpass_ptr = lowpass_band[1];
	PIXEL *y_highpass_ptr = highpass_band[0];
	PIXEL *u_highpass_ptr = highpass_band[2];
	PIXEL *v_highpass_ptr = highpass_band[1];

	uint8_t *output = output_image;

	// Process sixteen luma coefficients per loop iteration
	const int column_step = 16;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int channel;
	int row;

	// Compute the amount of scaling required to reduce the output precision
	const int descale_shift = (precision - 8); //DAN060725 -- dither is > 8-bit
	const int descale_offset = (precision - 8); //DAN060725 -- dither is > 8-bit

	// Convert the pitch to units of pixels
	for (channel = 0; channel < num_channels; channel++)
	{
		lowpass_pitch[channel] /= sizeof(PIXEL);
		highpass_pitch[channel] /= sizeof(PIXEL);
	}

	output_pitch /= sizeof(uint8_t);

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
		__m128i y_low1_epi16;		// Lowpass coefficients
		__m128i y_low2_epi16;
		__m128i u_low1_epi16;
		__m128i u_low2_epi16;
		__m128i v_low1_epi16;
		__m128i v_low2_epi16;

		__m128i y_high1_epi16;		// Highpass coefficients
		__m128i y_high2_epi16;
		__m128i u_high1_epi16;
		__m128i u_high2_epi16;
		__m128i v_high1_epi16;
		__m128i v_high2_epi16;

		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr = (__m128i *)&output[0];
#endif
		uint8_t *colptr = (uint8_t *)&output[0];

		int32_t y_even_value;
		int32_t u_even_value;
		int32_t v_even_value;
		int32_t y_odd_value;
		int32_t u_odd_value;
		int32_t v_odd_value;


		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		int32_t even = 0;
		int32_t odd = 0;

		int chroma_column;

		__m128i rounding1_pi16 = _mm_set1_epi16(0); // for 6bit matm
		__m128i rounding2_pi16 = _mm_set1_epi16(0); // for 6bit matm

		if(descale_offset>=2)
		{
			int mask = (1<<(descale_offset-1))-1; //DAN20090601
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 0);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 1);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 2);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 3);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 4);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 5);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 6);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 7);

			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 0);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 1);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 2);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 3);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 4);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 5);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 6);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 7);

			rounding1_pi16 = _mm_adds_epi16(rounding1_pi16, _mm_set1_epi16(10*mask/32)); //DAN20090601
			rounding2_pi16 = _mm_adds_epi16(rounding2_pi16, _mm_set1_epi16(10*mask/32));
		}

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * y_lowpass_ptr[column + 0];
		even -=  4 * y_lowpass_ptr[column + 1];
		even +=  1 * y_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += y_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		y_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * y_lowpass_ptr[column + 0];
		odd += 4 * y_lowpass_ptr[column + 1];
		odd -= 1 * y_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= y_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		y_odd_value = odd;


		// Process the first two u chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * u_lowpass_ptr[column + 0];
		even -=  4 * u_lowpass_ptr[column + 1];
		even +=  1 * u_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += u_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		u_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * u_lowpass_ptr[column + 0];
		odd += 4 * u_lowpass_ptr[column + 1];
		odd -= 1 * u_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= u_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		u_odd_value = odd;


		// Process the first two v chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * v_lowpass_ptr[column + 0];
		even -=  4 * v_lowpass_ptr[column + 1];
		even +=  1 * v_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += v_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		v_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * v_lowpass_ptr[column + 0];
		odd += 4 * v_lowpass_ptr[column + 1];
		odd -= 1 * v_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= v_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		v_odd_value = odd;

#if (1 && XMMOPT)

		// Preload the first eight lowpass luma coefficients
		y_low1_epi16 = _mm_load_si128((__m128i *)&y_lowpass_ptr[0]);

		// Preload the first eight highpass luma coefficients
		y_high1_epi16 = _mm_load_si128((__m128i *)&y_highpass_ptr[0]);

		// Preload the first eight lowpass u chroma coefficients
		u_low1_epi16 = _mm_load_si128((__m128i *)&u_lowpass_ptr[0]);

		// Preload the first eight highpass u chroma coefficients
		u_high1_epi16 = _mm_load_si128((__m128i *)&u_highpass_ptr[0]);

		// Preload the first eight lowpass v chroma coefficients
		v_low1_epi16 = _mm_load_si128((__m128i *)&v_lowpass_ptr[0]);

		// Preload the first eight highpass v chroma coefficients
 		v_high1_epi16 = _mm_load_si128((__m128i *)&v_highpass_ptr[0]);


		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i y1_output_epi16;
			__m128i y2_output_epi16;
			__m128i y3_output_epi16;
			__m128i y4_output_epi16;
			__m128i u1_output_epi16;
			__m128i u2_output_epi16;
			__m128i v1_output_epi16;
			__m128i v2_output_epi16;
			__m128i uv_epi16;
			__m128i yuv1_epi16;
			__m128i yuv2_epi16;
			__m128i yuv1_epi8;
			__m128i yuv2_epi8;
			__m128i yuv3_epi8;
			__m128i yuv4_epi8;

			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i out_epi16;		// Reconstructed data
			////__m128i mask_epi16;
			////__m128i lsb_epi16;
			////__m128i sign_epi16;
			//__m128i high_epi16;

			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;

			//DAN031304 -- correct inverse filter
			__m128i half_epi16 = _mm_set1_epi16(4);
			__m128i offset_epi16 = _mm_set1_epi16(2048);

			uint32_t temp;		// Temporary register for last two values

			chroma_column = column/2;


			/***** Compute the first eight luma output values *****/

			// Preload the second eight lowpass coefficients
			y_low2_epi16 = _mm_load_si128((__m128i *)&y_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			y_high2_epi16 = _mm_load_si128((__m128i *)&y_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = y_low1_epi16;
			high1_epi16 = y_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);


			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, y_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, y_odd_value, 1);

			// Save the eight luma values for packing later
			y1_output_epi16 = out_epi16;

			// Save the remaining two output values
			y_even_value = (short)temp;
			y_odd_value = (short)(temp >> 16);


			/***** Compute the second eight luma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = y_low2_epi16;
			high2_epi16 = y_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, y_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, y_odd_value, 1);

			// Save the eight luma values for packing later
			y2_output_epi16 = out_epi16;

			// Save the remaining two output values
			y_even_value = (short)temp;
			y_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are used later in the loop
			y_low1_epi16 = y_low2_epi16;

			// The second eight highpass coefficients are used later in the loop
			y_high1_epi16 = y_high2_epi16;


			/***** Compute the first eight u chroma output values *****/

			// Preload the second eight lowpass coefficients
			u_low2_epi16 = _mm_load_si128((__m128i *)&u_lowpass_ptr[chroma_column + 8]);

			// Preload the second eight highpass coefficients
			u_high2_epi16 = _mm_load_si128((__m128i *)&u_highpass_ptr[chroma_column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = u_low1_epi16;
			high1_epi16 = u_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, u_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, u_odd_value, 1);

			// Save the eight u chroma values for packing later
			u1_output_epi16 = out_epi16;

			// Save the remaining two output values
			u_even_value = (short)temp;
			u_odd_value = (short)(temp >> 16);


			/***** Compute the second eight u chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = u_low2_epi16;
			high2_epi16 = u_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, u_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, u_odd_value, 1);

			// Save the eight u chroma values for packing later
			u2_output_epi16 = out_epi16;

			// Save the remaining two output values
			u_even_value = (short)temp;
			u_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			u_low1_epi16 = u_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			u_high1_epi16 = u_high2_epi16;


			/***** Compute the third eight luma output values *****/

			// Preload the third eight lowpass coefficients
			y_low2_epi16 = _mm_load_si128((__m128i *)&y_lowpass_ptr[column + 16]);

			// Preload the third eight highpass coefficients
			y_high2_epi16 = _mm_load_si128((__m128i *)&y_highpass_ptr[column + 16]);

			// Move the current set of coefficients to working registers
			low1_epi16 = y_low1_epi16;
			high1_epi16 = y_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, y_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, y_odd_value, 1);

			// Save the eight luma values for packing later
			y3_output_epi16 = out_epi16;

			// Save the remaining two output values
			y_even_value = (short)temp;
			y_odd_value = (short)(temp >> 16);


			/***** Compute the fourth eight luma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = y_low2_epi16;
			high2_epi16 = y_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, y_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, y_odd_value, 1);

			// Save the eight luma values for packing later
			y4_output_epi16 = out_epi16;

			// Save the remaining two output values
			y_even_value = (short)temp;
			y_odd_value = (short)(temp >> 16);

			// The third eight lowpass coefficients are the current values in the next iteration
			y_low1_epi16 = y_low2_epi16;

			// The third eight highpass coefficients are the current values in the next iteration
			y_high1_epi16 = y_high2_epi16;


			/***** Compute the first eight v chroma output values *****/

			// Preload the second eight lowpass coefficients
			v_low2_epi16 = _mm_load_si128((__m128i *)&v_lowpass_ptr[chroma_column + 8]);

			// Preload the second eight highpass coefficients
			v_high2_epi16 = _mm_load_si128((__m128i *)&v_highpass_ptr[chroma_column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = v_low1_epi16;
			high1_epi16 = v_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, v_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, v_odd_value, 1);

			// Save the eight u chroma values for packing later
			v1_output_epi16 = out_epi16;

			// Save the remaining two output values
			v_even_value = (short)temp;
			v_odd_value = (short)(temp >> 16);


			/***** Compute the second eight v chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = v_low2_epi16;
			high2_epi16 = v_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, v_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, v_odd_value, 1);

			// Save the eight u chroma values for packing later
			v2_output_epi16 = out_epi16;

			// Save the remaining two output values
			v_even_value = (short)temp;
			v_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			v_low1_epi16 = v_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			v_high1_epi16 = v_high2_epi16;


			/***** Interleave the luma and chroma values *****/

			// Interleave the first four values from each chroma channel
			uv_epi16 = _mm_unpacklo_epi16(u1_output_epi16, v1_output_epi16);

			// Interleave the first eight chroma values with the first eight luma values
			yuv1_epi16 = _mm_unpacklo_epi16(y1_output_epi16, uv_epi16);
			yuv2_epi16 = _mm_unpackhi_epi16(y1_output_epi16, uv_epi16);

			// Pack the first sixteen bytes of luma and chroma
			yuv1_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

			// Store the first sixteen bytes of output values
			_mm_store_si128(outptr++, yuv1_epi8);

			// Interleave the second four values from each chroma channel
			uv_epi16 = _mm_unpackhi_epi16(u1_output_epi16, v1_output_epi16);

			// Interleave the second eight chroma values with the second eight luma values
			yuv1_epi16 = _mm_unpacklo_epi16(y2_output_epi16, uv_epi16);
			yuv2_epi16 = _mm_unpackhi_epi16(y2_output_epi16, uv_epi16);

			// Pack the second sixteen bytes of luma and chroma
			yuv2_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

			// Store the second sixteen bytes of output values
			_mm_store_si128(outptr++, yuv2_epi8);

			// Interleave the third four values from each chroma channel
			uv_epi16 = _mm_unpacklo_epi16(u2_output_epi16, v2_output_epi16);

			// Interleave the third eight chroma values with the third eight luma values
			yuv1_epi16 = _mm_unpacklo_epi16(y3_output_epi16, uv_epi16);
			yuv2_epi16 = _mm_unpackhi_epi16(y3_output_epi16, uv_epi16);

			// Pack the first sixteen bytes of luma and chroma
			yuv3_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

			// Store the first sixteen bytes of output values
			_mm_store_si128(outptr++, yuv3_epi8);

			// Interleave the fourth four values from each chroma channel
			uv_epi16 = _mm_unpackhi_epi16(u2_output_epi16, v2_output_epi16);

			// Interleave the second eight chroma values with the second eight luma values
			yuv1_epi16 = _mm_unpacklo_epi16(y4_output_epi16, uv_epi16);
			yuv2_epi16 = _mm_unpackhi_epi16(y4_output_epi16, uv_epi16);

			// Pack the second sixteen bytes of luma and chroma
			yuv4_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

			// Store the second sixteen bytes of output values
			_mm_store_si128(outptr++, yuv4_epi8);
		}

		// Should have exited the loop with the column equal to the post processing column
		//	assert(column == post_column);

		colptr = (uint8_t *)outptr;
#endif

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column += 2)
		{
			int y1_even_value;
			int y2_even_value;
			int y1_odd_value;
			int y2_odd_value;

			chroma_column = column/2;


			/***** First pair of luma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += y_lowpass_ptr[column - 1];
			even -= y_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += y_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += y_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the luma result for later output in the correct order
			y1_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= y_lowpass_ptr[column - 1];
			odd += y_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += y_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= y_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the luma result for later output in the correct order
			y1_odd_value = odd;


			/***** Pair of u chroma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += u_lowpass_ptr[chroma_column - 1];
			even -= u_lowpass_ptr[chroma_column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += u_lowpass_ptr[chroma_column + 0];

			// Add the highpass correction
			even += u_highpass_ptr[chroma_column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the u chroma result for later output in the correct order
			u_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= u_lowpass_ptr[chroma_column - 1];
			odd += u_lowpass_ptr[chroma_column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += u_lowpass_ptr[chroma_column + 0];

			// Subtract the highpass correction
			odd -= u_highpass_ptr[chroma_column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the u chroma result for later output in the correct order
			u_odd_value = odd;


			/***** Second pair of luma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += y_lowpass_ptr[column + 0];
			even -= y_lowpass_ptr[column + 2];
			even += 4; //DAN20050921
			even >>= 3;
			even += y_lowpass_ptr[column + 1];

			// Add the highpass correction
			even += y_highpass_ptr[column + 1];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the luma result for later output in the correct order
			y2_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= y_lowpass_ptr[column + 0];
			odd += y_lowpass_ptr[column + 2];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += y_lowpass_ptr[column + 1];

			// Subtract the highpass correction
			odd -= y_highpass_ptr[column + 1];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the luma result for later output in the correct order
			y2_odd_value = odd;


			/***** Pair of v chroma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += v_lowpass_ptr[chroma_column - 1];
			even -= v_lowpass_ptr[chroma_column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += v_lowpass_ptr[chroma_column + 0];

			// Add the highpass correction
			even += v_highpass_ptr[chroma_column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the v chroma result for later output in the correct order
			v_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= v_lowpass_ptr[chroma_column - 1];
			odd += v_lowpass_ptr[chroma_column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += v_lowpass_ptr[chroma_column + 0];

			// Subtract the highpass correction
			odd -= v_highpass_ptr[chroma_column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the v chroma result for later output in the correct order
			v_odd_value = odd;


			// Output the luma and chroma values in the correct order
			*(colptr++) = SATURATE_8U(y1_even_value);
			*(colptr++) = SATURATE_8U(u_even_value);
			*(colptr++) = SATURATE_8U(y1_odd_value);
			*(colptr++) = SATURATE_8U(v_even_value);

			// Need to output the second set of values?
			if ((column + 1) < last_column)
			{
				*(colptr++) = SATURATE_8U(y2_even_value);
				*(colptr++) = SATURATE_8U(u_odd_value);
				*(colptr++) = SATURATE_8U(y2_odd_value);
				*(colptr++) = SATURATE_8U(v_odd_value);
			}
			else
			{
				column++;
				break;
			}
		}

		// Should have exited the loop at the column for right border processing
		//	assert(column == last_column);

		column = last_column - 1;
		colptr -= 4;

		// Compute the last chroma column
		chroma_column = column/2;

		// Process the last luma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * y_lowpass_ptr[column + 0];
		even += 4 * y_lowpass_ptr[column - 1];
		even -= 1 * y_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += y_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the luma result for later output in the correct order
		y_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * y_lowpass_ptr[column + 0];
		odd -=  4 * y_lowpass_ptr[column - 1];
		odd +=  1 * y_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= y_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the luma result for later output in the correct order
		y_odd_value = odd;

		// Process the last u chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * u_lowpass_ptr[chroma_column + 0];
		even += 4 * u_lowpass_ptr[chroma_column - 1];
		even -= 1 * u_lowpass_ptr[chroma_column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += u_highpass_ptr[chroma_column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		u_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * u_lowpass_ptr[chroma_column + 0];
		odd -=  4 * u_lowpass_ptr[chroma_column - 1];
		odd +=  1 * u_lowpass_ptr[chroma_column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= u_highpass_ptr[chroma_column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		u_odd_value = odd;

		// Process the last v chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * v_lowpass_ptr[chroma_column + 0];
		even += 4 * v_lowpass_ptr[chroma_column - 1];
		even -= 1 * v_lowpass_ptr[chroma_column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += v_highpass_ptr[chroma_column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		v_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * v_lowpass_ptr[chroma_column + 0];
		odd -=  4 * v_lowpass_ptr[chroma_column - 1];
		odd +=  1 * v_lowpass_ptr[chroma_column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= v_highpass_ptr[chroma_column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		v_odd_value = odd;

		//DAN06052005 - Fix for PSNR errors in UV on right edge
		colptr-=4;
		colptr++; // Y fine
		*(colptr++) = SATURATE_8U(u_even_value);
		colptr++; // Y2 fine
		*(colptr++) = SATURATE_8U(v_even_value);

		// Output the last luma and chroma values in the correct order
		*(colptr++) = SATURATE_8U(y_even_value);
		*(colptr++) = SATURATE_8U(u_odd_value);
		*(colptr++) = SATURATE_8U(y_odd_value);
		*(colptr++) = SATURATE_8U(v_odd_value);

		// Advance to the next row of coefficients in each channel
		y_lowpass_ptr += lowpass_pitch[0];
		u_lowpass_ptr += lowpass_pitch[1];
		v_lowpass_ptr += lowpass_pitch[2];
		y_highpass_ptr += highpass_pitch[0];
		u_highpass_ptr += highpass_pitch[1];
		v_highpass_ptr += highpass_pitch[2];

		// Advance the output pointer to the next row
		output += output_pitch;
	}
}

// Apply the inverse horizontal transform to reconstruct a strip of rows into packed UYVY
void InvertHorizontalStrip16sToUYVY(PIXEL *lowpass_band[],	// Horizontal lowpass coefficients
									int lowpass_pitch[],	// Distance between rows in bytes
									PIXEL *highpass_band[],	// Horizontal highpass coefficients
									int highpass_pitch[],	// Distance between rows in bytes
									uint8_t *output_image,		// Row of reconstructed results
									int output_pitch,		// Distance between rows in bytes
									ROI roi,				// Height and width of the strip
									int precision)			// Precision of the original video
{
	int num_channels = CODEC_NUM_CHANNELS;
	int height = roi.height;
	int width = roi.width;

	// Note that the u and v chroma values are swapped
	PIXEL *y_lowpass_ptr = lowpass_band[0];
	PIXEL *u_lowpass_ptr = lowpass_band[2];
	PIXEL *v_lowpass_ptr = lowpass_band[1];
	PIXEL *y_highpass_ptr = highpass_band[0];
	PIXEL *u_highpass_ptr = highpass_band[2];
	PIXEL *v_highpass_ptr = highpass_band[1];

	uint8_t *output = output_image;

	// Process sixteen luma coefficients per loop iteration
	const int column_step = 16;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	int channel;
	int row;

	// Compute the amount of scaling required to reduce the output precision
	int descale_shift = (precision - 8); //DAN060725 -- dither is > 8-bit
	int descale_offset = (precision - 8); //DAN060725 -- dither is > 8-bit

	// Convert the pitch to units of pixels
	for (channel = 0; channel < num_channels; channel++)
	{
		lowpass_pitch[channel] /= sizeof(PIXEL);
		highpass_pitch[channel] /= sizeof(PIXEL);
	}

	output_pitch /= sizeof(uint8_t);

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
		__m128i y_low1_epi16;		// Lowpass coefficients
		__m128i y_low2_epi16;
		__m128i u_low1_epi16;
		__m128i u_low2_epi16;
		__m128i v_low1_epi16;
		__m128i v_low2_epi16;

		__m128i y_high1_epi16;		// Highpass coefficients
		__m128i y_high2_epi16;
		__m128i u_high1_epi16;
		__m128i u_high2_epi16;
		__m128i v_high1_epi16;
		__m128i v_high2_epi16;

		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr = (__m128i *)&output[0];
#endif
		uint8_t *colptr = (uint8_t *)&output[0];

		int32_t y_even_value;
		int32_t u_even_value;
		int32_t v_even_value;
		int32_t y_odd_value;
		int32_t u_odd_value;
		int32_t v_odd_value;


		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		int32_t even = 0;
		int32_t odd = 0;

		int chroma_column;

		__m128i rounding1_pi16 = _mm_set1_epi16(0); // for 6bit matm
		__m128i rounding2_pi16 = _mm_set1_epi16(0); // for 6bit matm

		if(descale_offset>=2)
		{
			int mask = (1<<(descale_offset-1))-1; //DAN20090601
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 0);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 1);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 2);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 3);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 4);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 5);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 6);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 7);

			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 0);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 1);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 2);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 3);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 4);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 5);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 6);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 7);

			rounding1_pi16 = _mm_adds_epi16(rounding1_pi16, _mm_set1_epi16(10*mask/32)); //DAN20090601
			rounding2_pi16 = _mm_adds_epi16(rounding2_pi16, _mm_set1_epi16(10*mask/32));
		}

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * y_lowpass_ptr[column + 0];
		even -=  4 * y_lowpass_ptr[column + 1];
		even +=  1 * y_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += y_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		y_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * y_lowpass_ptr[column + 0];
		odd += 4 * y_lowpass_ptr[column + 1];
		odd -= 1 * y_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= y_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		y_odd_value = odd;


		// Process the first two u chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * u_lowpass_ptr[column + 0];
		even -=  4 * u_lowpass_ptr[column + 1];
		even +=  1 * u_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += u_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		u_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * u_lowpass_ptr[column + 0];
		odd += 4 * u_lowpass_ptr[column + 1];
		odd -= 1 * u_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= u_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		u_odd_value = odd;


		// Process the first two v chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * v_lowpass_ptr[column + 0];
		even -=  4 * v_lowpass_ptr[column + 1];
		even +=  1 * v_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += v_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		v_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * v_lowpass_ptr[column + 0];
		odd += 4 * v_lowpass_ptr[column + 1];
		odd -= 1 * v_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= v_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		v_odd_value = odd;

#if (1 && XMMOPT)

		// Preload the first eight lowpass luma coefficients
		y_low1_epi16 = _mm_load_si128((__m128i *)&y_lowpass_ptr[0]);

		// Preload the first eight highpass luma coefficients
		y_high1_epi16 = _mm_load_si128((__m128i *)&y_highpass_ptr[0]);

		// Preload the first eight lowpass u chroma coefficients
		u_low1_epi16 = _mm_load_si128((__m128i *)&u_lowpass_ptr[0]);

		// Preload the first eight highpass u chroma coefficients
		u_high1_epi16 = _mm_load_si128((__m128i *)&u_highpass_ptr[0]);

		// Preload the first eight lowpass v chroma coefficients
		v_low1_epi16 = _mm_load_si128((__m128i *)&v_lowpass_ptr[0]);

		// Preload the first eight highpass v chroma coefficients
 		v_high1_epi16 = _mm_load_si128((__m128i *)&v_highpass_ptr[0]);

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i y1_output_epi16;
			__m128i y2_output_epi16;
			__m128i y3_output_epi16;
			__m128i y4_output_epi16;
			__m128i u1_output_epi16;
			__m128i u2_output_epi16;
			__m128i v1_output_epi16;
			__m128i v2_output_epi16;
			__m128i uv_epi16;
			__m128i yuv1_epi16;
			__m128i yuv2_epi16;
			__m128i yuv1_epi8;
			__m128i yuv2_epi8;
			__m128i yuv3_epi8;
			__m128i yuv4_epi8;

			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i out_epi16;		// Reconstructed data
			////__m128i mask_epi16;
			////__m128i lsb_epi16;
			////__m128i sign_epi16;
			//__m128i high_epi16;

			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;

			//DAN031304 -- correct inverse filter
			__m128i half_epi16 = _mm_set1_epi16(4);
			__m128i offset_epi16 = _mm_set1_epi16(2048);

			uint32_t temp;		// Temporary register for last two values

			chroma_column = column/2;


			/***** Compute the first eight luma output values *****/

			// Preload the second eight lowpass coefficients
			y_low2_epi16 = _mm_load_si128((__m128i *)&y_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			y_high2_epi16 = _mm_load_si128((__m128i *)&y_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = y_low1_epi16;
			high1_epi16 = y_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);


			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, y_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, y_odd_value, 1);

			// Save the eight luma values for packing later
			y1_output_epi16 = out_epi16;

			// Save the remaining two output values
			y_even_value = (short)temp;
			y_odd_value = (short)(temp >> 16);


			/***** Compute the second eight luma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = y_low2_epi16;
			high2_epi16 = y_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, y_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, y_odd_value, 1);

			// Save the eight luma values for packing later
			y2_output_epi16 = out_epi16;

			// Save the remaining two output values
			y_even_value = (short)temp;
			y_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are used later in the loop
			y_low1_epi16 = y_low2_epi16;

			// The second eight highpass coefficients are used later in the loop
			y_high1_epi16 = y_high2_epi16;


			/***** Compute the first eight u chroma output values *****/

			// Preload the second eight lowpass coefficients
			u_low2_epi16 = _mm_load_si128((__m128i *)&u_lowpass_ptr[chroma_column + 8]);

			// Preload the second eight highpass coefficients
			u_high2_epi16 = _mm_load_si128((__m128i *)&u_highpass_ptr[chroma_column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = u_low1_epi16;
			high1_epi16 = u_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, u_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, u_odd_value, 1);

			// Save the eight u chroma values for packing later
			u1_output_epi16 = out_epi16;

			// Save the remaining two output values
			u_even_value = (short)temp;
			u_odd_value = (short)(temp >> 16);


			/***** Compute the second eight u chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = u_low2_epi16;
			high2_epi16 = u_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, u_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, u_odd_value, 1);

			// Save the eight u chroma values for packing later
			u2_output_epi16 = out_epi16;

			// Save the remaining two output values
			u_even_value = (short)temp;
			u_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			u_low1_epi16 = u_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			u_high1_epi16 = u_high2_epi16;


			/***** Compute the third eight luma output values *****/

			// Preload the third eight lowpass coefficients
			y_low2_epi16 = _mm_load_si128((__m128i *)&y_lowpass_ptr[column + 16]);

			// Preload the third eight highpass coefficients
			y_high2_epi16 = _mm_load_si128((__m128i *)&y_highpass_ptr[column + 16]);

			// Move the current set of coefficients to working registers
			low1_epi16 = y_low1_epi16;
			high1_epi16 = y_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, y_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, y_odd_value, 1);

			// Save the eight luma values for packing later
			y3_output_epi16 = out_epi16;

			// Save the remaining two output values
			y_even_value = (short)temp;
			y_odd_value = (short)(temp >> 16);


			/***** Compute the fourth eight luma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = y_low2_epi16;
			high2_epi16 = y_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, y_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, y_odd_value, 1);

			// Save the eight luma values for packing later
			y4_output_epi16 = out_epi16;

			// Save the remaining two output values
			y_even_value = (short)temp;
			y_odd_value = (short)(temp >> 16);

			// The third eight lowpass coefficients are the current values in the next iteration
			y_low1_epi16 = y_low2_epi16;

			// The third eight highpass coefficients are the current values in the next iteration
			y_high1_epi16 = y_high2_epi16;


			/***** Compute the first eight v chroma output values *****/

			// Preload the second eight lowpass coefficients
			v_low2_epi16 = _mm_load_si128((__m128i *)&v_lowpass_ptr[chroma_column + 8]);

			// Preload the second eight highpass coefficients
			v_high2_epi16 = _mm_load_si128((__m128i *)&v_highpass_ptr[chroma_column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = v_low1_epi16;
			high1_epi16 = v_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, v_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, v_odd_value, 1);

			// Save the eight u chroma values for packing later
			v1_output_epi16 = out_epi16;

			// Save the remaining two output values
			v_even_value = (short)temp;
			v_odd_value = (short)(temp >> 16);


			/***** Compute the second eight v chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = v_low2_epi16;
			high2_epi16 = v_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, v_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, v_odd_value, 1);

			// Save the eight u chroma values for packing later
			v2_output_epi16 = out_epi16;

			// Save the remaining two output values
			v_even_value = (short)temp;
			v_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			v_low1_epi16 = v_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			v_high1_epi16 = v_high2_epi16;


			/***** Interleave the luma and chroma values *****/

			// Interleave the first four values from each chroma channel
			uv_epi16 = _mm_unpacklo_epi16(u1_output_epi16, v1_output_epi16);

			// Interleave the first eight chroma values with the first eight luma values
			yuv1_epi16 = _mm_unpacklo_epi16(uv_epi16, y1_output_epi16);
			yuv2_epi16 = _mm_unpackhi_epi16(uv_epi16, y1_output_epi16);

			// Pack the first sixteen bytes of luma and chroma
			yuv1_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

			// Store the first sixteen bytes of output values
			_mm_store_si128(outptr++, yuv1_epi8);

			// Interleave the second four values from each chroma channel
			uv_epi16 = _mm_unpackhi_epi16(u1_output_epi16, v1_output_epi16);

			// Interleave the second eight chroma values with the second eight luma values
			yuv1_epi16 = _mm_unpacklo_epi16(uv_epi16, y2_output_epi16);
			yuv2_epi16 = _mm_unpackhi_epi16(uv_epi16, y2_output_epi16);

			// Pack the second sixteen bytes of luma and chroma
			yuv2_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

			// Store the second sixteen bytes of output values
			_mm_store_si128(outptr++, yuv2_epi8);

			// Interleave the third four values from each chroma channel
			uv_epi16 = _mm_unpacklo_epi16(u2_output_epi16, v2_output_epi16);

			// Interleave the third eight chroma values with the third eight luma values
			yuv1_epi16 = _mm_unpacklo_epi16(uv_epi16, y3_output_epi16);
			yuv2_epi16 = _mm_unpackhi_epi16(uv_epi16, y3_output_epi16);

			// Pack the first sixteen bytes of luma and chroma
			yuv3_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

			// Store the first sixteen bytes of output values
			_mm_store_si128(outptr++, yuv3_epi8);

			// Interleave the fourth four values from each chroma channel
			uv_epi16 = _mm_unpackhi_epi16(u2_output_epi16, v2_output_epi16);

			// Interleave the second eight chroma values with the second eight luma values
			yuv1_epi16 = _mm_unpacklo_epi16(uv_epi16, y4_output_epi16);
			yuv2_epi16 = _mm_unpackhi_epi16(uv_epi16, y4_output_epi16);

			// Pack the second sixteen bytes of luma and chroma
			yuv4_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

			// Store the second sixteen bytes of output values
			_mm_store_si128(outptr++, yuv4_epi8);
		}

		// Should have exited the loop with the column equal to the post processing column
		//	assert(column == post_column);

		colptr = (uint8_t *)outptr;
#endif

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column += 2)
		{
			int y1_even_value;
			int y2_even_value;
			int y1_odd_value;
			int y2_odd_value;

			chroma_column = column/2;


			/***** First pair of luma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += y_lowpass_ptr[column - 1];
			even -= y_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += y_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += y_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the luma result for later output in the correct order
			y1_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= y_lowpass_ptr[column - 1];
			odd += y_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += y_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= y_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the luma result for later output in the correct order
			y1_odd_value = odd;


			/***** Pair of u chroma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += u_lowpass_ptr[chroma_column - 1];
			even -= u_lowpass_ptr[chroma_column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += u_lowpass_ptr[chroma_column + 0];

			// Add the highpass correction
			even += u_highpass_ptr[chroma_column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the u chroma result for later output in the correct order
			u_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= u_lowpass_ptr[chroma_column - 1];
			odd += u_lowpass_ptr[chroma_column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += u_lowpass_ptr[chroma_column + 0];

			// Subtract the highpass correction
			odd -= u_highpass_ptr[chroma_column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the u chroma result for later output in the correct order
			u_odd_value = odd;


			/***** Second pair of luma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += y_lowpass_ptr[column + 0];
			even -= y_lowpass_ptr[column + 2];
			even += 4; //DAN20050921
			even >>= 3;
			even += y_lowpass_ptr[column + 1];

			// Add the highpass correction
			even += y_highpass_ptr[column + 1];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the luma result for later output in the correct order
			y2_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= y_lowpass_ptr[column + 0];
			odd += y_lowpass_ptr[column + 2];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += y_lowpass_ptr[column + 1];

			// Subtract the highpass correction
			odd -= y_highpass_ptr[column + 1];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the luma result for later output in the correct order
			y2_odd_value = odd;


			/***** Pair of v chroma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += v_lowpass_ptr[chroma_column - 1];
			even -= v_lowpass_ptr[chroma_column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += v_lowpass_ptr[chroma_column + 0];

			// Add the highpass correction
			even += v_highpass_ptr[chroma_column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the v chroma result for later output in the correct order
			v_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= v_lowpass_ptr[chroma_column - 1];
			odd += v_lowpass_ptr[chroma_column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += v_lowpass_ptr[chroma_column + 0];

			// Subtract the highpass correction
			odd -= v_highpass_ptr[chroma_column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the v chroma result for later output in the correct order
			v_odd_value = odd;


			// Output the luma and chroma values in the correct order
			*(colptr++) = SATURATE_8U(u_even_value);
			*(colptr++) = SATURATE_8U(y1_even_value);
			*(colptr++) = SATURATE_8U(v_even_value);
			*(colptr++) = SATURATE_8U(y1_odd_value);

			// Need to output the second set of values?
			if ((column + 1) < last_column)
			{
				*(colptr++) = SATURATE_8U(u_odd_value);
				*(colptr++) = SATURATE_8U(y2_even_value);
				*(colptr++) = SATURATE_8U(v_odd_value);
				*(colptr++) = SATURATE_8U(y2_odd_value);
			}
			else
			{
				column++;
				break;
			}
		}

		// Should have exited the loop at the column for right border processing
		//	assert(column == last_column);

		column = last_column - 1;
		colptr -= 4;

		// Compute the last chroma column
		chroma_column = column/2;

		// Process the last luma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * y_lowpass_ptr[column + 0];
		even += 4 * y_lowpass_ptr[column - 1];
		even -= 1 * y_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += y_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the luma result for later output in the correct order
		y_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * y_lowpass_ptr[column + 0];
		odd -=  4 * y_lowpass_ptr[column - 1];
		odd +=  1 * y_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= y_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the luma result for later output in the correct order
		y_odd_value = odd;

		// Process the last u chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * u_lowpass_ptr[chroma_column + 0];
		even += 4 * u_lowpass_ptr[chroma_column - 1];
		even -= 1 * u_lowpass_ptr[chroma_column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += u_highpass_ptr[chroma_column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		u_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * u_lowpass_ptr[chroma_column + 0];
		odd -=  4 * u_lowpass_ptr[chroma_column - 1];
		odd +=  1 * u_lowpass_ptr[chroma_column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= u_highpass_ptr[chroma_column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		u_odd_value = odd;

		// Process the last v chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * v_lowpass_ptr[chroma_column + 0];
		even += 4 * v_lowpass_ptr[chroma_column - 1];
		even -= 1 * v_lowpass_ptr[chroma_column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += v_highpass_ptr[chroma_column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		v_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * v_lowpass_ptr[chroma_column + 0];
		odd -=  4 * v_lowpass_ptr[chroma_column - 1];
		odd +=  1 * v_lowpass_ptr[chroma_column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= v_highpass_ptr[chroma_column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		v_odd_value = odd;

		//DAN06052005 - Fix for PSNR errors in UV on right edge
		colptr-=4;
		*(colptr++) = SATURATE_8U(u_even_value);
		colptr++; // Y fine
		*(colptr++) = SATURATE_8U(v_even_value);
		colptr++; // Y2 fine

		// Output the last luma and chroma values in the correct order
		*(colptr++) = SATURATE_8U(u_odd_value);
		*(colptr++) = SATURATE_8U(y_even_value);
		*(colptr++) = SATURATE_8U(v_odd_value);
		*(colptr++) = SATURATE_8U(y_odd_value);

		// Advance to the next row of coefficients in each channel
		y_lowpass_ptr += lowpass_pitch[0];
		u_lowpass_ptr += lowpass_pitch[1];
		v_lowpass_ptr += lowpass_pitch[2];
		y_highpass_ptr += highpass_pitch[0];
		u_highpass_ptr += highpass_pitch[1];
		v_highpass_ptr += highpass_pitch[2];

		// Advance the output pointer to the next row
		output += output_pitch;
	}
}


void HalfHorizontalStrip16sToYUYV(PIXEL *lowpass_band[],	// Horizontal lowpass coefficients
									int lowpass_pitch[],	// Distance between rows in bytes
									PIXEL *highpass_band[],	// Horizontal highpass coefficients
									int highpass_pitch[],	// Distance between rows in bytes
									uint8_t *output_image,		// Row of reconstructed results
									int output_pitch,		// Distance between rows in bytes
									ROI roi,				// Height and width of the strip
									int precision,			// Precision of the original video
									int format)				// COLOR_FORMAT_YUYV or COLOR_FORMAT_UYVY
{
	int num_channels = CODEC_NUM_CHANNELS;
	int height = roi.height;
	int width = roi.width;

	// Note that the u and v chroma values are swapped
	PIXEL *y_lowpass_ptr = lowpass_band[0];
	PIXEL *u_lowpass_ptr = lowpass_band[2];
	PIXEL *v_lowpass_ptr = lowpass_band[1];
	PIXEL *y_highpass_ptr = highpass_band[0];
	PIXEL *u_highpass_ptr = highpass_band[2];
	PIXEL *v_highpass_ptr = highpass_band[1];

	uint8_t *output = output_image;
	uint8_t *colptr;

	// Process sixteen luma coefficients per loop iteration
//	const int column_step = 16;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;

	// Need at least four luma values of border processing up to the last column
//	const int post_border = 2;

	int channel;
	int row;

	// Compute the amount of scaling required to reduce the output precision
	int descale_shift = (precision - 8); //DAN060725 -- dither is > 8-bit

	// Convert the pitch to units of pixels
	for (channel = 0; channel < num_channels; channel++)
	{
		lowpass_pitch[channel] /= sizeof(PIXEL);
		highpass_pitch[channel] /= sizeof(PIXEL);
	}

	output_pitch /= sizeof(uint8_t);


	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		int column;
		int shift = descale_shift+1;
		colptr = (uint8_t *)output;
		// Process the rest of the columns up to the last column in the row

		if(format == COLOR_FORMAT_YUYV)
		{
			for (column = 0; column < last_column; column += 2)
			{
				int chroma_column = column>>1;

				*(colptr++) = SATURATE_8U(y_lowpass_ptr[column]>>shift);
				*(colptr++) = SATURATE_8U(u_lowpass_ptr[chroma_column]>>shift);
				*(colptr++) = SATURATE_8U(y_lowpass_ptr[column+1]>>shift);
				*(colptr++) = SATURATE_8U(v_lowpass_ptr[chroma_column]>>shift);
			}
		}
		else
		{
			for (column = 0; column < last_column; column += 2)
			{
				int chroma_column = column>>1;

				*(colptr++) = SATURATE_8U(u_lowpass_ptr[chroma_column]>>shift);
				*(colptr++) = SATURATE_8U(y_lowpass_ptr[column]>>shift);
				*(colptr++) = SATURATE_8U(v_lowpass_ptr[chroma_column]>>shift);
				*(colptr++) = SATURATE_8U(y_lowpass_ptr[column+1]>>shift);
			}
		}

		// Advance to the next row of coefficients in each channel
		y_lowpass_ptr += lowpass_pitch[0];
		u_lowpass_ptr += lowpass_pitch[1];
		v_lowpass_ptr += lowpass_pitch[2];
		y_highpass_ptr += highpass_pitch[0];
		u_highpass_ptr += highpass_pitch[1];
		v_highpass_ptr += highpass_pitch[2];

		// Advance the output pointer to the next row
		output += output_pitch;
	}
}

// Apply the inverse horizontal transform to reconstruct a strip of rows into packed YUV pixels
void InvertHorizontalStrip16sToYUV(HorizontalFilterParams)
{
	if((decoder->channel_blend_type == 2 || decoder->channel_blend_type == 7) && decoder->frame.format == DECODED_FORMAT_YUYV) //3d work
	{
		HalfHorizontalStrip16sToYUYV(lowpass_band, lowpass_pitch,
										highpass_band, highpass_pitch,
										output_image, output_pitch,
										roi, precision, format);
	}
	else
	{
		if (format == COLOR_FORMAT_YUYV)
		{
			InvertHorizontalStrip16sToYUYV(lowpass_band, lowpass_pitch,
										highpass_band, highpass_pitch,
										output_image, output_pitch,
										roi, precision);
		}
		else
		{
			assert(format == COLOR_FORMAT_UYVY);

			InvertHorizontalStrip16sToUYVY(lowpass_band, lowpass_pitch,
										highpass_band, highpass_pitch,
										output_image, output_pitch,
										roi, precision);
		}
	}
}

// Apply the inverse horizontal transform to reconstruct a strip of rows into packed YUV pixels
void InvertHorizontalStrip16sToOutput(HorizontalFilterParams)
{
	int i;
	int channels = decoder->codec.num_channels;
	//uint8_t *chroma_buffer = output_buffer;
	uint8_t *plane_array[TRANSFORM_MAX_CHANNELS] = {0};
	int plane_pitch[TRANSFORM_MAX_CHANNELS] = {0};

	uint8_t *output_row_ptr = output_image;

	// Scratch buffer for the reconstructed rows allocated on the stack
	//unsigned short scanline2[6 * 2048];

	void *scratch = NULL;
	size_t scratchsize = 0;
	int local_pitch = roi.width * 2 * 2 * 2;
	uint8_t *sptr;
	uint8_t *sptr2;
	ROI output_strip = roi;
	int color_space = decoder->frame.colorspace;

	scratch = decoder->threads_buffer[thread_index];
	scratchsize = decoder->threads_buffer_size;
	if((int)scratchsize < local_pitch)
	{
		assert(0);
		return;
	}


	// Two rows of 4:2:2 YUV components, two components per pixel, each component is 16 bits
	output_strip.width *= 2;

	// Pointer for allocating row buffers that are 16 byte aligned
	//sptr = (uint8_t *)&scanline2[0];
	sptr = (uint8_t *)scratch;
	sptr = sptr2 = (uint8_t *)((((uintptr_t)sptr)+15) & ~0x0F);
	sptr2 += output_strip.width * 6;

	// Invert the horizontal strip in each channel
	for (i = 0; i < channels; i++)
	{
		ROI temp_strip = roi;

		if (i > 0)
		{
			temp_strip.width >>= 1;
		}

		InvertHorizontalStrip16sToRow16u(lowpass_band[i], lowpass_pitch[i],
										 highpass_band[i], highpass_pitch[i],
										 (PIXEL16U *)sptr2, local_pitch, temp_strip,
										 precision);
		plane_array[i] = (uint8_t *)sptr2;
		plane_pitch[i] = local_pitch;

		// Move the buffer allocation pointer to the next row
		sptr2 += temp_strip.width*2*2;
	}

	// Convert the strip to the output format
	for (i = 0; i < roi.height; i++)
	{
		int flags = ACTIVEMETADATA_PLANAR;		// Unpacked rows of YUV422
		int white_bit_depth = 16;

		//convert to YUV444
		ChannelYUYV16toPlanarYUV16((unsigned short **)plane_array, (PIXEL16U *)sptr, output_strip.width, color_space);
		if(LUTYUV(decoder->frame.format))
			flags |= ACTIVEMETADATA_COLORFORMATDONE;  // leave in YUV
		else
			PlanarYUV16toPlanarRGB16((PIXEL16U *)sptr, (PIXEL16U *)sptr, output_strip.width, color_space);

		ConvertLinesToOutput(decoder, output_strip.width, 1, 1, (PIXEL16U *)sptr,
					output_row_ptr, output_pitch, decoder->frame.format,
					white_bit_depth, flags);

		plane_array[0] += plane_pitch[0];
		plane_array[1] += plane_pitch[1];
		plane_array[2] += plane_pitch[2];

		output_row_ptr += output_pitch;
	}
}

// Apply the inverse horizontal transform to reconstruct a strip of rows into packed YUV pixels
void InvertHorizontalYUVStrip16sToYUVOutput(HorizontalFilterParams)
{
	int i;
	int channels = decoder->codec.num_channels;
	//uint8_t *chroma_buffer = output_buffer;
	PIXEL *plane_array[TRANSFORM_MAX_CHANNELS] = {0};
	int plane_pitch[TRANSFORM_MAX_CHANNELS] = {0};

	uint8_t *output_row_ptr = output_image;

	// Scratch buffer for the reconstructed rows allocated on the stack
	//unsigned short scanline2[6 * 2048];
	
	void *scratch = NULL;
	size_t scratchsize = 0;
	int local_pitch = roi.width * 2 * 2 * 2;
	uint8_t *sptr;
	uint8_t *sptr2;
	ROI output_strip = roi;

	scratch = decoder->threads_buffer[thread_index];
	scratchsize = decoder->threads_buffer_size;
	if((int)scratchsize < local_pitch)
	{
		assert(0);
		return;
	}


	// Two rows of 4:2:2 YUV components, two components per pixel, each component is 16 bits
	output_strip.width *= 2;

	// Pointer for allocating row buffers that are 16 byte aligned
	//sptr = (uint8_t *)&scanline2[0];
	sptr = (uint8_t *)scratch;
	sptr = sptr2 = (uint8_t *)((((uintptr_t)sptr)+15) & ~0x0F);

	if(format == COLOR_FORMAT_V210 || format == COLOR_FORMAT_YU64) //DAN20081222 //works
	{

		// Invert the horizontal strip in each channel
		for (i = 0; i < channels; i++)
		{
			ROI temp_strip = roi;

			if (i > 0)
			{
				temp_strip.width >>= 1;
			}

			InvertHorizontalStrip16sToRow16u(lowpass_band[i], lowpass_pitch[i],
											highpass_band[i], highpass_pitch[i],
											(PIXEL16U *)sptr2, local_pitch, temp_strip,
											precision);
			plane_array[i] = (PIXEL *)sptr2;
			plane_pitch[i] = local_pitch;

			// Move the buffer allocation pointer to the next row
			sptr2 += temp_strip.width*2*2;
		}

		// Convert the strip to the output format
		for (i = 0; i < roi.height; i++)
		{
//			int flags = ACTIVEMETADATA_PLANAR;		// Unpacked rows of YUV
//			int whitebitdepth = 16;

			ROI new_strip = output_strip;
			new_strip.height = 1;

			ConvertYUVStripPlanarToV210(plane_array, plane_pitch, new_strip,
				output_row_ptr, output_pitch, new_strip.width, format, decoder->frame.colorspace, 16);

			plane_array[0] += plane_pitch[0]/sizeof(PIXEL);
			plane_array[1] += plane_pitch[1]/sizeof(PIXEL);
			plane_array[2] += plane_pitch[2]/sizeof(PIXEL);

			output_row_ptr += output_pitch;
		}
	}
	else
	{
		assert(0);
	}
}

// Used in RT YUYV playback
// Apply the inverse horizontal transform to reconstruct a strip of rows into packed YUV pixels
void InvertHorizontalStrip16sToBayerYUV(HorizontalFilterParams)
{
	int num_channels = CODEC_NUM_CHANNELS;
	int height = roi.height;
	int width = roi.width;

	// Note that the u and v chroma values are swapped
	PIXEL *gg_lowpass_ptr = lowpass_band[0];
	PIXEL *rg_lowpass_ptr = lowpass_band[1];
	PIXEL *bg_lowpass_ptr = lowpass_band[2];
	PIXEL *gg_highpass_ptr = highpass_band[0];
	PIXEL *rg_highpass_ptr = highpass_band[1];
	PIXEL *bg_highpass_ptr = highpass_band[2];

	uint8_t *output = output_image;

	// Process 8 luma coefficients per loop iteration
	const int column_step = 8;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int channel;
	int row;

	// Compute the amount of scaling required to reduce the output precision
	int descale_shift = (precision - 8);

	int shift = 8;

	//int y_rmult,y_gmult,y_bmult,y_offset;
	//int u_rmult,u_gmult,u_bmult,u_offset;
	//int v_rmult,v_gmult,v_bmult,v_offset;

	float fy_rmult,fy_gmult,fy_bmult,fy_offset;
	float fu_rmult,fu_gmult,fu_bmult,fu_offset;
	float fv_rmult,fv_gmult,fv_bmult,fv_offset;

	float fr_rmult,fr_gmult,fr_bmult;
	float fg_rmult,fg_gmult,fg_bmult;
	float fb_rmult,fb_gmult,fb_bmult;

	//int y_rmult_sign,y_gmult_sign,y_bmult_sign;
	//int u_rmult_sign,u_gmult_sign,u_bmult_sign;
	//int v_rmult_sign,v_gmult_sign,v_bmult_sign;

	//int color_space = decoder->frame.colorspace;
	int matrix_non_unity = 0;

	float rgb2yuv[3][4] =
	{
        {0.183f, 0.614f, 0.062f, 16.0f/255.0f},
        {-0.101f,-0.338f, 0.439f, 128.0f/255.0f},
        {0.439f,-0.399f,-0.040f, 128.0f/255.0f}
	};

	// this can't be a static (lines of cross talk between two decoders using different color matrices)
	float mtrx[3][4] =
	{
        {1.0f,  0,   0,   0},
        {0,  1.0f,   0,   0},
        {0,    0, 1.0f,   0}
	};

	//3560
	/*	float mtrx[3][4] =
	{
		 1.60,   -0.38,   -0.22,    0,
		-0.45,    2.31,   -0.86,    0,
		-0.35,   -0.24,    1.59,    0
	};*/

/*	//3570
	float mtrx[3][4] =
	{
		 1.095,   0.405,    -0.500,    0,
		-0.491,   2.087,    -0.596,    0,
		-0.179,  -0.994,     2.173,    0
	};
*/


	float scale;

	// We use 16-bit fixed-point arithmetic to approximate the color conversion coefficients
	//
	// Floating point arithmetic is
	//
	// Y  = 0.257 * R + 0.504 * G + 0.098 * B + 16.5;
	// Cb =-0.148 * R - 0.291 * G + 0.439 * B + 128.5;
	// Cr = 0.439 * R - 0.368 * G - 0.071 * B + 128.5;
	//
	// Fixed point approximation (8-bit) is
	//
	// Y  = ( 66 * R + 129 * G +  25 * B +  4224) >> 8;
	// Cb = (-38 * R -  74 * G + 112 * B + 32896) >> 8;
	// Cr = (112 * R -  94 * G -  18 * B + 32896) >> 8;

	// 601 video systems
	// Y = 0.299R + 0.587G + 0.114B
	// Cb = -0.172R - 0.339G + 0.511B + 128
	// Cr = 0.511R - 0.428G - 0.083B + 128

	// 709 video systems
	// Y = 0.213R + 0.715G + 0.072B
	// Cb = -0.117R - 0.394G + 0.511B + 128
	// Cr = 0.511R - 0.464G - 0.047B + 128

	// 709 sRGB
	// Y = 0.183R + 0.614G + 0.062B + 16
	// Cb = -0.101R - 0.338G + 0.439B + 128
	// Cr = 0.439R - 0.399G - 0.040B + 128

#if MMXSUPPORTED //TODO DANREMOVE
	//_mm_empty();
#endif
/*
	scale = 256.0; //TODO We can;t multiple the matrices together as the R' = aR+bG+cB muast be saturated, so all this must change.
	y_rmult = (int)((rgb2yuv[0][0]*mtrx[0][0] + rgb2yuv[1][0]*mtrx[0][1] + rgb2yuv[2][0]*mtrx[0][2]) * scale);
	y_gmult = (int)((rgb2yuv[0][1]*mtrx[0][0] + rgb2yuv[1][1]*mtrx[0][1] + rgb2yuv[2][1]*mtrx[0][2]) * scale);
	y_bmult = (int)((rgb2yuv[0][2]*mtrx[0][0] + rgb2yuv[1][2]*mtrx[0][1] + rgb2yuv[2][2]*mtrx[0][2]) * scale);
	y_offset= (int)((rgb2yuv[0][3]*mtrx[0][0] + rgb2yuv[1][3]*mtrx[0][1] + rgb2yuv[2][3]*mtrx[0][2]) * 65536.0);

	u_rmult = (int)((rgb2yuv[0][0]*mtrx[1][0] + rgb2yuv[1][0]*mtrx[1][1] + rgb2yuv[2][0]*mtrx[1][2]) * scale);
	u_gmult = (int)((rgb2yuv[0][1]*mtrx[1][0] + rgb2yuv[1][1]*mtrx[1][1] + rgb2yuv[2][1]*mtrx[1][2]) * scale);
	u_bmult = (int)((rgb2yuv[0][2]*mtrx[1][0] + rgb2yuv[1][2]*mtrx[1][1] + rgb2yuv[2][2]*mtrx[1][2]) * scale);
	u_offset= (int)((rgb2yuv[0][3]*mtrx[1][0] + rgb2yuv[1][3]*mtrx[1][1] + rgb2yuv[2][3]*mtrx[1][2]) * 65536.0);

	v_rmult = (int)((rgb2yuv[0][0]*mtrx[2][0] + rgb2yuv[1][0]*mtrx[2][1] + rgb2yuv[2][0]*mtrx[2][2]) * scale);
	v_gmult = (int)((rgb2yuv[0][1]*mtrx[2][0] + rgb2yuv[1][1]*mtrx[2][1] + rgb2yuv[2][1]*mtrx[2][2]) * scale);
	v_bmult = (int)((rgb2yuv[0][2]*mtrx[2][0] + rgb2yuv[1][2]*mtrx[2][1] + rgb2yuv[2][2]*mtrx[2][2]) * scale);
	v_offset= (int)((rgb2yuv[0][3]*mtrx[2][0] + rgb2yuv[1][3]*mtrx[2][1] + rgb2yuv[2][3]*mtrx[2][2]) * 65536.0);
	*/
	scale = 64.0;
/*	fy_rmult = ((rgb2yuv[0][0]*mtrx[0][0] + rgb2yuv[1][0]*mtrx[0][1] + rgb2yuv[2][0]*mtrx[0][2]) * scale);
	fy_gmult = ((rgb2yuv[0][1]*mtrx[0][0] + rgb2yuv[1][1]*mtrx[0][1] + rgb2yuv[2][1]*mtrx[0][2]) * scale);
	fy_bmult = ((rgb2yuv[0][2]*mtrx[0][0] + rgb2yuv[1][2]*mtrx[0][1] + rgb2yuv[2][2]*mtrx[0][2]) * scale);
	fy_offset= ((rgb2yuv[0][3]*mtrx[0][0] + rgb2yuv[1][3]*mtrx[0][1] + rgb2yuv[2][3]*mtrx[0][2]) * 16384.0);

	fu_rmult = ((rgb2yuv[0][0]*mtrx[1][0] + rgb2yuv[1][0]*mtrx[1][1] + rgb2yuv[2][0]*mtrx[1][2]) * scale);
	fu_gmult = ((rgb2yuv[0][1]*mtrx[1][0] + rgb2yuv[1][1]*mtrx[1][1] + rgb2yuv[2][1]*mtrx[1][2]) * scale);
	fu_bmult = ((rgb2yuv[0][2]*mtrx[1][0] + rgb2yuv[1][2]*mtrx[1][1] + rgb2yuv[2][2]*mtrx[1][2]) * scale);
	fu_offset= ((rgb2yuv[0][3]*mtrx[1][0] + rgb2yuv[1][3]*mtrx[1][1] + rgb2yuv[2][3]*mtrx[1][2]) * 16384.0);

	fv_rmult = ((rgb2yuv[0][0]*mtrx[2][0] + rgb2yuv[1][0]*mtrx[2][1] + rgb2yuv[2][0]*mtrx[2][2]) * scale);
	fv_gmult = ((rgb2yuv[0][1]*mtrx[2][0] + rgb2yuv[1][1]*mtrx[2][1] + rgb2yuv[2][1]*mtrx[2][2]) * scale);
	fv_bmult = ((rgb2yuv[0][2]*mtrx[2][0] + rgb2yuv[1][2]*mtrx[2][1] + rgb2yuv[2][2]*mtrx[2][2]) * scale);
	fv_offset= ((rgb2yuv[0][3]*mtrx[2][0] + rgb2yuv[1][3]*mtrx[2][1] + rgb2yuv[2][3]*mtrx[2][2]) * 16384.0);
*/

/*
	if(decoder->cfhddata.MagicNumber == CFHDDATA_MAGIC_NUMBER && decoder->cfhddata.version >= 2)
	{
		float fval = 0.0;
		int i;

#if 0 // Matrix disabled as it can only be correct handled by the 3D LUT due to the required linear conversions
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
#endif
	}
	*/

	fy_rmult = ((rgb2yuv[0][0]) * scale);
	fy_gmult = ((rgb2yuv[0][1]) * scale);
	fy_bmult = ((rgb2yuv[0][2]) * scale);
	fy_offset= ((rgb2yuv[0][3]) * 16384.0f);

	fu_rmult = ((rgb2yuv[1][0]) * scale);
	fu_gmult = ((rgb2yuv[1][1]) * scale);
	fu_bmult = ((rgb2yuv[1][2]) * scale);
	fu_offset= ((rgb2yuv[1][3]) * 16384.0f);

	fv_rmult = ((rgb2yuv[2][0]) * scale);
	fv_gmult = ((rgb2yuv[2][1]) * scale);
	fv_bmult = ((rgb2yuv[2][2]) * scale);
	fv_offset= ((rgb2yuv[2][3]) * 16384.0f);


	fr_rmult= (mtrx[0][0]);
	fr_gmult= (mtrx[0][1]);
	fr_bmult= (mtrx[0][2]);

	fg_rmult= (mtrx[1][0]);
	fg_gmult= (mtrx[1][1]);
	fg_bmult= (mtrx[1][2]);

	fb_rmult= (mtrx[2][0]);
	fb_gmult= (mtrx[2][1]);
	fb_bmult= (mtrx[2][2]);

/*	y_rmult_sign = (abs(y_rmult) != y_rmult);
	y_gmult_sign = (abs(y_gmult) != y_gmult);
	y_bmult_sign = (abs(y_bmult) != y_bmult);

	u_rmult_sign = (abs(u_rmult) != u_rmult);
	u_gmult_sign = (abs(u_gmult) != u_gmult);
	u_bmult_sign = (abs(u_bmult) != u_bmult);

	v_rmult_sign = (abs(v_rmult) != v_rmult);
	v_gmult_sign = (abs(v_gmult) != v_gmult);
	v_bmult_sign = (abs(v_bmult) != v_bmult);

	y_rmult = abs(y_rmult);
	y_gmult = abs(y_gmult);
	y_bmult = abs(y_bmult);
	y_offset= abs(y_offset);

	u_rmult = abs(u_rmult);
	u_gmult = abs(u_gmult);
	u_bmult = abs(u_bmult);
	u_offset= abs(u_offset);

	v_rmult = abs(v_rmult);
	v_gmult = abs(v_gmult);
	v_bmult = abs(v_bmult);
	v_offset= abs(v_offset);

	y_offset>>=2;
	u_offset>>=2;
	v_offset>>=2;*/
	shift-=2;


	// Convert the pitch to units of pixels
	for (channel = 0; channel < num_channels; channel++)
	{
		lowpass_pitch[channel] /= sizeof(PIXEL);
		highpass_pitch[channel] /= sizeof(PIXEL);
	}

	output_pitch /= sizeof(uint8_t);

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
		__m128i gg_low1_epi16;		// Lowpass coefficients
		__m128i gg_low2_epi16;
		__m128i bg_low1_epi16;
		__m128i bg_low2_epi16;
		__m128i rg_low1_epi16;
		__m128i rg_low2_epi16;

		__m128i gg_high1_epi16;		// Highpass coefficients
		__m128i gg_high2_epi16;
		__m128i bg_high1_epi16;
		__m128i bg_high2_epi16;
		__m128i rg_high1_epi16;
		__m128i rg_high2_epi16;

		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr = (__m128i *)&output[0];

		const __m128i mask_epi32 = _mm_set1_epi32(0xffff);
		const __m128i value128_epi32 = _mm_set1_epi16(128);

		__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x00ff);
		__m128i limiter = _mm_set1_epi16(0x7fff - 0x3fff);

#endif
		uint8_t *colptr = (uint8_t *)&output[0];

		int32_t gg_even_value;
		int32_t bg_even_value;
		int32_t rg_even_value;
		int32_t gg_odd_value;
		int32_t bg_odd_value;
		int32_t rg_odd_value;


		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		int32_t even = 0;
		int32_t odd = 0;

		__m128i rounding1_epi16 = _mm_set1_epi16(0); // for 6bit matm
		__m128i rounding2_epi16 = _mm_set1_epi16(0); // for 6bit matm

		if(descale_shift>=2)
		{
			int mask = (1<<(descale_shift-1))-1; //DAN20090601
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

			rounding1_epi16 = _mm_adds_epi16(rounding1_epi16, _mm_set1_epi16(10*mask/32)); //DAN20090601
			rounding2_epi16 = _mm_adds_epi16(rounding2_epi16, _mm_set1_epi16(10*mask/32));
		}




		// Apply the even reconstruction filter to the lowpass band
		even += 11 * gg_lowpass_ptr[column + 0];
		even -=  4 * gg_lowpass_ptr[column + 1];
		even +=  1 * gg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += gg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		gg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * gg_lowpass_ptr[column + 0];
		odd += 4 * gg_lowpass_ptr[column + 1];
		odd -= 1 * gg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= gg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		gg_odd_value = odd;


		// Process the first two u chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * bg_lowpass_ptr[column + 0];
		even -=  4 * bg_lowpass_ptr[column + 1];
		even +=  1 * bg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += bg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		bg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * bg_lowpass_ptr[column + 0];
		odd += 4 * bg_lowpass_ptr[column + 1];
		odd -= 1 * bg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= bg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		bg_odd_value = odd;


		// Process the first two v chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * rg_lowpass_ptr[column + 0];
		even -=  4 * rg_lowpass_ptr[column + 1];
		even +=  1 * rg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += rg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		rg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * rg_lowpass_ptr[column + 0];
		odd += 4 * rg_lowpass_ptr[column + 1];
		odd -= 1 * rg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= rg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		rg_odd_value = odd;

#if (1 && XMMOPT)

		// Preload the first eight lowpass luma coefficients
		gg_low1_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[0]);

		// Preload the first eight highpass luma coefficients
		gg_high1_epi16 = _mm_load_si128((__m128i *)&gg_highpass_ptr[0]);

		// Preload the first eight lowpass u chroma coefficients
		bg_low1_epi16 = _mm_load_si128((__m128i *)&bg_lowpass_ptr[0]);

		// Preload the first eight highpass u chroma coefficients
		bg_high1_epi16 = _mm_load_si128((__m128i *)&bg_highpass_ptr[0]);

		// Preload the first eight lowpass v chroma coefficients
		rg_low1_epi16 = _mm_load_si128((__m128i *)&rg_lowpass_ptr[0]);

		// Preload the first eight highpass v chroma coefficients
 		rg_high1_epi16 = _mm_load_si128((__m128i *)&rg_highpass_ptr[0]);


		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i g1_output_epi16;
			__m128i g2_output_epi16;
			__m128i r1_output_epi16;
			__m128i r2_output_epi16;
			__m128i b1_output_epi16;
			__m128i b2_output_epi16;

			__m128i gg1_output_epi16;
			__m128i gg2_output_epi16;
			__m128i rg1_output_epi16;
			__m128i rg2_output_epi16;
			__m128i bg1_output_epi16;
			__m128i bg2_output_epi16;

			__m128i y1_output_epi16;
			__m128i y2_output_epi16;
			__m128i u1_output_epi16;
			__m128i u2_output_epi16;
			__m128i v1_output_epi16;
			__m128i v2_output_epi16;

			__m128i urg_epi16;
			__m128i yuv1_epi16;
			__m128i yuv2_epi16;
			__m128i yuv1_epi8;
			__m128i yuv2_epi8;

			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i temp_epi32;
			__m128i tempB_epi32;
			__m128i rgb_epi32;
			__m128i zero_epi128;
			__m128  temp_ps;
			__m128  rgb_ps;
			__m128	y1a_ps;
			__m128	y1b_ps;
			__m128	u1a_ps;
			__m128	u1b_ps;
			__m128	v1a_ps;
			__m128	v1b_ps;


			__m128i out_epi16;		// Reconstructed data
			////__m128i mask_epi16;
			////__m128i lsb_epi16;
			////__m128i sign_epi16;
			//__m128i high_epi16;

			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;

			//DAN031304 -- correct inverse filter
			__m128i half_epi16 = _mm_set1_epi16(4);
			__m128i offset_epi16 = _mm_set1_epi16(2048);

			uint32_t temp;		// Temporary register for last two values


			/***** Compute the first eight luma output values *****/

			// Preload the second eight lowpass coefficients
			gg_low2_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			gg_high2_epi16 = _mm_load_si128((__m128i *)&gg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = gg_low1_epi16;
			high1_epi16 = gg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);


			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_epi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_odd_value, 1);

			// Save the eight luma values for packing later
			gg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			gg_even_value = (short)temp;
			gg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight luma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = gg_low2_epi16;
			high2_epi16 = gg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_epi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_odd_value, 1);

			// Save the eight luma values for packing later
			gg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			gg_even_value = (short)temp;
			gg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are used later in the loop
			gg_low1_epi16 = gg_low2_epi16;

			// The second eight highpass coefficients are used later in the loop
			gg_high1_epi16 = gg_high2_epi16;


			/***** Compute the first eight u chroma output values *****/

			// Preload the second eight lowpass coefficients
			bg_low2_epi16 = _mm_load_si128((__m128i *)&bg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			bg_high2_epi16 = _mm_load_si128((__m128i *)&bg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = bg_low1_epi16;
			high1_epi16 = bg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			bg_even_value = (short)temp;
			bg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight u chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = bg_low2_epi16;
			high2_epi16 = bg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			bg_even_value = (short)temp;
			bg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			bg_low1_epi16 = bg_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			bg_high1_epi16 = bg_high2_epi16;



			/***** Compute the first eight v chroma output values *****/

			// Preload the second eight lowpass coefficients
			rg_low2_epi16 = _mm_load_si128((__m128i *)&rg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			rg_high2_epi16 = _mm_load_si128((__m128i *)&rg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = rg_low1_epi16;
			high1_epi16 = rg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			rg_even_value = (short)temp;
			rg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight v chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = rg_low2_epi16;
			high2_epi16 = rg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			rg_even_value = (short)temp;
			rg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			rg_low1_epi16 = rg_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			rg_high1_epi16 = rg_high2_epi16;












			//r_output_epi16  = ((rg_output_epi16 - 32768)<<1)+gg_output_epi16
			//r_output_epi16  = ((rg_output_epi16>>3 - 32768>>3))+gg_output_epi16>>4


			g1_output_epi16 = gg1_output_epi16;

			r1_output_epi16 = rg1_output_epi16;//_mm_srli_epi16(rg1_output_epi16,2);
			r1_output_epi16 = _mm_subs_epi16(r1_output_epi16, value128_epi32);
			r1_output_epi16 = _mm_slli_epi16(r1_output_epi16,1);
			r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, g1_output_epi16);

			b1_output_epi16 = bg1_output_epi16;//_mm_srli_epi16(bg1_output_epi16,2);
			b1_output_epi16 = _mm_subs_epi16(b1_output_epi16, value128_epi32);
			b1_output_epi16 = _mm_slli_epi16(b1_output_epi16,1);
			b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, g1_output_epi16);


			 r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, limiterRGB);
			 r1_output_epi16 = _mm_subs_epu16(r1_output_epi16, limiterRGB);

			 g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, limiterRGB);
			 g1_output_epi16 = _mm_subs_epu16(g1_output_epi16, limiterRGB);

			 b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, limiterRGB);
			 b1_output_epi16 = _mm_subs_epu16(b1_output_epi16, limiterRGB);


			zero_epi128 = _mm_setzero_si128();



			// Compute R'G'B'
			if(matrix_non_unity)
			{
				rgb_epi32 = _mm_unpacklo_epi16(r1_output_epi16, zero_epi128);
				rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
				y1a_ps = _mm_mul_ps(_mm_set_ps1(fr_rmult), rgb_ps);
				u1a_ps = _mm_mul_ps(_mm_set_ps1(fg_rmult), rgb_ps);
				v1a_ps = _mm_mul_ps(_mm_set_ps1(fb_rmult), rgb_ps);
				rgb_epi32 = _mm_unpackhi_epi16(r1_output_epi16, zero_epi128);
				rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
				y1b_ps = _mm_mul_ps(_mm_set_ps1(fr_rmult), rgb_ps);
				u1b_ps = _mm_mul_ps(_mm_set_ps1(fg_rmult), rgb_ps);
				v1b_ps = _mm_mul_ps(_mm_set_ps1(fb_rmult), rgb_ps);

				rgb_epi32 = _mm_unpacklo_epi16(g1_output_epi16, zero_epi128);
				rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fr_gmult), rgb_ps);
				y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fg_gmult), rgb_ps);
				u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fb_gmult), rgb_ps);
				v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
				rgb_epi32 = _mm_unpackhi_epi16(g1_output_epi16, zero_epi128);
				rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fr_gmult), rgb_ps);
				y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fg_gmult), rgb_ps);
				u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fb_gmult), rgb_ps);
				v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

				rgb_epi32 = _mm_unpacklo_epi16(b1_output_epi16, zero_epi128);
				rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fr_bmult), rgb_ps);
				y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fg_bmult), rgb_ps);
				u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fb_bmult), rgb_ps);
				v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
				rgb_epi32 = _mm_unpackhi_epi16(b1_output_epi16, zero_epi128);
				rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fr_bmult), rgb_ps);
				y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fg_bmult), rgb_ps);
				u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fb_bmult), rgb_ps);
				v1b_ps = _mm_add_ps(v1b_ps, temp_ps);


				temp_epi32 = _mm_cvtps_epi32(y1a_ps);
				tempB_epi32 = _mm_cvtps_epi32(y1b_ps);
				r1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
				r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, limiterRGB);
				r1_output_epi16 = _mm_subs_epu16(r1_output_epi16, limiterRGB);

				temp_epi32 = _mm_cvtps_epi32(u1a_ps);
				tempB_epi32 = _mm_cvtps_epi32(u1b_ps);
				g1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
				g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, limiterRGB);
				g1_output_epi16 = _mm_subs_epu16(g1_output_epi16, limiterRGB);

				temp_epi32 = _mm_cvtps_epi32(v1a_ps);
				tempB_epi32 = _mm_cvtps_epi32(v1b_ps);
				b1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
				b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, limiterRGB);
				b1_output_epi16 = _mm_subs_epu16(b1_output_epi16, limiterRGB);
			}

			// Compute Y,U,V
			rgb_epi32 = _mm_unpacklo_epi16(r1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1a_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1a_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1a_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);
			rgb_epi32 = _mm_unpackhi_epi16(r1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1b_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1b_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1b_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);

			rgb_epi32 = _mm_unpacklo_epi16(g1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(g1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			rgb_epi32 = _mm_unpacklo_epi16(b1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(b1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_ps = _mm_set_ps1(fy_offset);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fu_offset);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fv_offset);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_epi32 = _mm_cvtps_epi32(y1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(y1b_ps);
			y1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			y1_output_epi16 = _mm_adds_epi16(y1_output_epi16, limiter);
			y1_output_epi16 = _mm_subs_epu16(y1_output_epi16, limiter);
			y1_output_epi16 = _mm_srli_epi16(y1_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(u1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(u1b_ps);
			u1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			u1_output_epi16 = _mm_adds_epi16(u1_output_epi16, limiter);
			u1_output_epi16 = _mm_subs_epu16(u1_output_epi16, limiter);
			u1_output_epi16 = _mm_srli_epi16(u1_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(v1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(v1b_ps);
			v1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			v1_output_epi16 = _mm_adds_epi16(v1_output_epi16, limiter);
			v1_output_epi16 = _mm_subs_epu16(v1_output_epi16, limiter);
			v1_output_epi16 = _mm_srli_epi16(v1_output_epi16, shift);






			g2_output_epi16 = gg2_output_epi16;//_mm_srli_epi16(gg2_output_epi16,2);

			r2_output_epi16 = rg2_output_epi16;//_mm_srli_epi16(rg2_output_epi16,2);
			r2_output_epi16 = _mm_subs_epi16(r2_output_epi16, value128_epi32);
			r2_output_epi16 = _mm_slli_epi16(r2_output_epi16,1);
			r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, g2_output_epi16);

			b2_output_epi16 = bg2_output_epi16;//_mm_srli_epi16(bg2_output_epi16,2);
			b2_output_epi16 = _mm_subs_epi16(b2_output_epi16, value128_epi32);
			b2_output_epi16 = _mm_slli_epi16(b2_output_epi16,1);
			b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, g2_output_epi16);



			 r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, limiterRGB);
			 r2_output_epi16 = _mm_subs_epu16(r2_output_epi16, limiterRGB);

			 g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, limiterRGB);
			 g2_output_epi16 = _mm_subs_epu16(g2_output_epi16, limiterRGB);

			 b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, limiterRGB);
			 b2_output_epi16 = _mm_subs_epu16(b2_output_epi16, limiterRGB);


			 // Compute R'G'B'
			if(matrix_non_unity)
			{
				rgb_epi32 = _mm_unpacklo_epi16(r2_output_epi16, zero_epi128);
				rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
				y1a_ps = _mm_mul_ps(_mm_set_ps1(fr_rmult), rgb_ps);
				u1a_ps = _mm_mul_ps(_mm_set_ps1(fg_rmult), rgb_ps);
				v1a_ps = _mm_mul_ps(_mm_set_ps1(fb_rmult), rgb_ps);
				rgb_epi32 = _mm_unpackhi_epi16(r2_output_epi16, zero_epi128);
				rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
				y1b_ps = _mm_mul_ps(_mm_set_ps1(fr_rmult), rgb_ps);
				u1b_ps = _mm_mul_ps(_mm_set_ps1(fg_rmult), rgb_ps);
				v1b_ps = _mm_mul_ps(_mm_set_ps1(fb_rmult), rgb_ps);

				rgb_epi32 = _mm_unpacklo_epi16(g2_output_epi16, zero_epi128);
				rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fr_gmult), rgb_ps);
				y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fg_gmult), rgb_ps);
				u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fb_gmult), rgb_ps);
				v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
				rgb_epi32 = _mm_unpackhi_epi16(g2_output_epi16, zero_epi128);
				rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fr_gmult), rgb_ps);
				y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fg_gmult), rgb_ps);
				u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fb_gmult), rgb_ps);
				v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

				rgb_epi32 = _mm_unpacklo_epi16(b2_output_epi16, zero_epi128);
				rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fr_bmult), rgb_ps);
				y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fg_bmult), rgb_ps);
				u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fb_bmult), rgb_ps);
				v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
				rgb_epi32 = _mm_unpackhi_epi16(b2_output_epi16, zero_epi128);
				rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fr_bmult), rgb_ps);
				y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fg_bmult), rgb_ps);
				u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
				temp_ps = _mm_mul_ps(_mm_set_ps1(fb_bmult), rgb_ps);
				v1b_ps = _mm_add_ps(v1b_ps, temp_ps);


				temp_epi32 = _mm_cvtps_epi32(y1a_ps);
				tempB_epi32 = _mm_cvtps_epi32(y1b_ps);
				r2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
				r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, limiterRGB);
				r2_output_epi16 = _mm_subs_epu16(r2_output_epi16, limiterRGB);

				temp_epi32 = _mm_cvtps_epi32(u1a_ps);
				tempB_epi32 = _mm_cvtps_epi32(u1b_ps);
				g2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
				g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, limiterRGB);
				g2_output_epi16 = _mm_subs_epu16(g2_output_epi16, limiterRGB);

				temp_epi32 = _mm_cvtps_epi32(v1a_ps);
				tempB_epi32 = _mm_cvtps_epi32(v1b_ps);
				b2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
				b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, limiterRGB);
				b2_output_epi16 = _mm_subs_epu16(b2_output_epi16, limiterRGB);
			}


			// Compute Y,U,V
			rgb_epi32 = _mm_unpacklo_epi16(r2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1a_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1a_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1a_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);
			rgb_epi32 = _mm_unpackhi_epi16(r2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1b_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1b_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1b_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);

			rgb_epi32 = _mm_unpacklo_epi16(g2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(g2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			rgb_epi32 = _mm_unpacklo_epi16(b2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(b2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_ps = _mm_set_ps1(fy_offset);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fu_offset);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fv_offset);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_epi32 = _mm_cvtps_epi32(y1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(y1b_ps);
			y2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			y2_output_epi16 = _mm_adds_epi16(y2_output_epi16, limiter);
			y2_output_epi16 = _mm_subs_epu16(y2_output_epi16, limiter);
			y2_output_epi16 = _mm_srli_epi16(y2_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(u1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(u1b_ps);
			u2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			u2_output_epi16 = _mm_adds_epi16(u2_output_epi16, limiter);
			u2_output_epi16 = _mm_subs_epu16(u2_output_epi16, limiter);
			u2_output_epi16 = _mm_srli_epi16(u2_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(v1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(v1b_ps);
			v2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			v2_output_epi16 = _mm_adds_epi16(v2_output_epi16, limiter);
			v2_output_epi16 = _mm_subs_epu16(v2_output_epi16, limiter);
			v2_output_epi16 = _mm_srli_epi16(v2_output_epi16, shift);


			// 4:4:4 to 4:2:2
			temp_epi16 = _mm_srli_si128(u1_output_epi16, 2);
			u1_output_epi16 = _mm_adds_epi16(u1_output_epi16, temp_epi16);
			u1_output_epi16 = _mm_srli_epi16(u1_output_epi16, 1);
			temp_epi16 = _mm_srli_si128(u2_output_epi16, 2);
			u2_output_epi16 = _mm_adds_epi16(u2_output_epi16, temp_epi16);
			u2_output_epi16 = _mm_srli_epi16(u2_output_epi16, 1);
			temp_epi16 = _mm_srli_si128(v1_output_epi16, 2);
			v1_output_epi16 = _mm_adds_epi16(v1_output_epi16, temp_epi16);
			v1_output_epi16 = _mm_srli_epi16(v1_output_epi16, 1);
			temp_epi16 = _mm_srli_si128(v2_output_epi16, 2);
			v2_output_epi16 = _mm_adds_epi16(v2_output_epi16, temp_epi16);
			v2_output_epi16 = _mm_srli_epi16(v2_output_epi16, 1);
			u1_output_epi16 = _mm_and_si128(u1_output_epi16, mask_epi32);
			u2_output_epi16 = _mm_and_si128(u2_output_epi16, mask_epi32);
			v1_output_epi16 = _mm_and_si128(v1_output_epi16, mask_epi32);
			v2_output_epi16 = _mm_and_si128(v2_output_epi16, mask_epi32);

			u1_output_epi16 = _mm_packs_epi32 (u1_output_epi16, u2_output_epi16);
			v1_output_epi16 = _mm_packs_epi32 (v1_output_epi16, v2_output_epi16);


			/***** Interleave the luma and chroma values *****/

			if(format == DECODED_FORMAT_YUYV)
			{
				// Interleave the first four values from each chroma channel
				urg_epi16 = _mm_unpacklo_epi16(u1_output_epi16, v1_output_epi16);
				// Interleave the first eight chroma values with the first eight luma values
				yuv1_epi16 = _mm_unpacklo_epi16(y1_output_epi16, urg_epi16);
				yuv2_epi16 = _mm_unpackhi_epi16(y1_output_epi16, urg_epi16);

				// Pack the first sixteen bytes of luma and chroma
				yuv1_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

				// Store the first sixteen bytes of output values
				_mm_store_si128(outptr++, yuv1_epi8);

				// Interleave the second four values from each chroma channel
				urg_epi16 = _mm_unpackhi_epi16(u1_output_epi16, v1_output_epi16);

				// Interleave the second eight chroma values with the second eight luma values
				yuv1_epi16 = _mm_unpacklo_epi16(y2_output_epi16, urg_epi16);
				yuv2_epi16 = _mm_unpackhi_epi16(y2_output_epi16, urg_epi16);

				// Pack the second sixteen bytes of luma and chroma
				yuv2_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

				// Store the second sixteen bytes of output values
				_mm_store_si128(outptr++, yuv2_epi8);
			}
			else
			{
				// Interleave the first four values from each chroma channel
				urg_epi16 = _mm_unpacklo_epi16(u1_output_epi16, v1_output_epi16);
				// Interleave the first eight chroma values with the first eight luma values
				yuv1_epi16 = _mm_unpacklo_epi16(urg_epi16, y1_output_epi16);
				yuv2_epi16 = _mm_unpackhi_epi16(urg_epi16, y1_output_epi16);

				// Pack the first sixteen bytes of luma and chroma
				yuv1_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

				// Store the first sixteen bytes of output values
				_mm_store_si128(outptr++, yuv1_epi8);

				// Interleave the second four values from each chroma channel
				urg_epi16 = _mm_unpackhi_epi16(u1_output_epi16, v1_output_epi16);

				// Interleave the second eight chroma values with the second eight luma values
				yuv1_epi16 = _mm_unpacklo_epi16(urg_epi16, y2_output_epi16);
				yuv2_epi16 = _mm_unpackhi_epi16(urg_epi16, y2_output_epi16);

				// Pack the second sixteen bytes of luma and chroma
				yuv2_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

				// Store the second sixteen bytes of output values
				_mm_store_si128(outptr++, yuv2_epi8);
			}
		}

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

		colptr = (uint8_t *)outptr;
#endif

//////////////////////////////////////////// *******************************************************************************************************************
//TODO: Non-SSE code not upgraded for Bayer. *******************************************************************************************************************
//////////////////////////////////////////// *******************************************************************************************************************

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column += 2)
		{
			int y1_even_value;
			int y2_even_value;
			int y1_odd_value;
			int y2_odd_value;


			/***** First pair of luma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += gg_lowpass_ptr[column - 1];
			even -= gg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += gg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += gg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the luma result for later output in the correct order
			y1_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= gg_lowpass_ptr[column - 1];
			odd += gg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += gg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= gg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the luma result for later output in the correct order
			y1_odd_value = odd;


			/***** Pair of u chroma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += bg_lowpass_ptr[column - 1];
			even -= bg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += bg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += bg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the u chroma result for later output in the correct order
			bg_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= bg_lowpass_ptr[column - 1];
			odd += bg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += bg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= bg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the u chroma result for later output in the correct order
			bg_odd_value = odd;


			/***** Second pair of luma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += gg_lowpass_ptr[column + 0];
			even -= gg_lowpass_ptr[column + 2];
			even += 4; //DAN20050921
			even >>= 3;
			even += gg_lowpass_ptr[column + 1];

			// Add the highpass correction
			even += gg_highpass_ptr[column + 1];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the luma result for later output in the correct order
			y2_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= gg_lowpass_ptr[column + 0];
			odd += gg_lowpass_ptr[column + 2];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += gg_lowpass_ptr[column + 1];

			// Subtract the highpass correction
			odd -= gg_highpass_ptr[column + 1];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the luma result for later output in the correct order
			y2_odd_value = odd;


			/***** Pair of v chroma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += rg_lowpass_ptr[column - 1];
			even -= rg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += rg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += rg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the v chroma result for later output in the correct order
			rg_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= rg_lowpass_ptr[column - 1];
			odd += rg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += rg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= rg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the v chroma result for later output in the correct order
			rg_odd_value = odd;

			
			if(format == DECODED_FORMAT_YUYV)
			{
				// Output the luma and chroma values in the correct order
				*(colptr++) = SATURATE_8U(y1_even_value);
				*(colptr++) = SATURATE_8U(bg_even_value);
				*(colptr++) = SATURATE_8U(y1_odd_value);
				*(colptr++) = SATURATE_8U(rg_even_value);

				// Need to output the second set of values?
				if ((column + 1) < last_column)
				{
					*(colptr++) = SATURATE_8U(y2_even_value);
					*(colptr++) = SATURATE_8U(bg_odd_value);
					*(colptr++) = SATURATE_8U(y2_odd_value);
					*(colptr++) = SATURATE_8U(rg_odd_value);
				}
				else
				{
					column++;
					break;
				}
			}
			else
			{
				// Output the luma and chroma values in the correct order
				*(colptr++) = SATURATE_8U(bg_even_value);
				*(colptr++) = SATURATE_8U(y1_even_value);
				*(colptr++) = SATURATE_8U(rg_even_value);
				*(colptr++) = SATURATE_8U(y1_odd_value);

				// Need to output the second set of values?
				if ((column + 1) < last_column)
				{
					*(colptr++) = SATURATE_8U(bg_odd_value);
					*(colptr++) = SATURATE_8U(y2_even_value);
					*(colptr++) = SATURATE_8U(rg_odd_value);
					*(colptr++) = SATURATE_8U(y2_odd_value);
				}
				else
				{
					column++;
					break;
				}
			}
		}

		// Should have exited the loop at the column for right border processing
	//	assert(column == last_column);


		column = last_column - 1;
		colptr -= 4;

		// Process the last luma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * gg_lowpass_ptr[column + 0];
		even += 4 * gg_lowpass_ptr[column - 1];
		even -= 1 * gg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += gg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the luma result for later output in the correct order
		gg_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * gg_lowpass_ptr[column + 0];
		odd -=  4 * gg_lowpass_ptr[column - 1];
		odd +=  1 * gg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= gg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the luma result for later output in the correct order
		gg_odd_value = odd;

		// Process the last u chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * bg_lowpass_ptr[column + 0];
		even += 4 * bg_lowpass_ptr[column - 1];
		even -= 1 * bg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += bg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		bg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * bg_lowpass_ptr[column + 0];
		odd -=  4 * bg_lowpass_ptr[column - 1];
		odd +=  1 * bg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= bg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		bg_odd_value = odd;

		// Process the last v chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * rg_lowpass_ptr[column + 0];
		even += 4 * rg_lowpass_ptr[column - 1];
		even -= 1 * rg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += rg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		rg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * rg_lowpass_ptr[column + 0];
		odd -=  4 * rg_lowpass_ptr[column - 1];
		odd +=  1 * rg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= rg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		rg_odd_value = odd;

			
		if(format == DECODED_FORMAT_YUYV)
		{
			//DAN06052005 - Fix for PSNR errors in UV on right edge
			colptr-=4;
			colptr++; // Y fine
			*(colptr++) = SATURATE_8U(bg_even_value);
			colptr++; // Y2 fine
			*(colptr++) = SATURATE_8U(rg_even_value);

			// Output the last luma and chroma values in the correct order
			*(colptr++) = SATURATE_8U(gg_even_value);
			*(colptr++) = SATURATE_8U(bg_odd_value);
			*(colptr++) = SATURATE_8U(gg_odd_value);
			*(colptr++) = SATURATE_8U(rg_odd_value);
		}
		else
		{
			//DAN06052005 - Fix for PSNR errors in UV on right edge
			colptr-=4;
			*(colptr++) = SATURATE_8U(bg_even_value);
			colptr++; // Y fine
			*(colptr++) = SATURATE_8U(rg_even_value);
			colptr++; // Y2 fine

			// Output the last luma and chroma values in the correct order
			*(colptr++) = SATURATE_8U(bg_odd_value);
			*(colptr++) = SATURATE_8U(gg_even_value);
			*(colptr++) = SATURATE_8U(rg_odd_value);
			*(colptr++) = SATURATE_8U(gg_odd_value);
		}

		// Advance to the next row of coefficients in each channel
		gg_lowpass_ptr += lowpass_pitch[0];
		bg_lowpass_ptr += lowpass_pitch[1];
		rg_lowpass_ptr += lowpass_pitch[2];
		gg_highpass_ptr += highpass_pitch[0];
		bg_highpass_ptr += highpass_pitch[1];
		rg_highpass_ptr += highpass_pitch[2];

		// Advance the output pointer to the next row
		output += output_pitch;
	}
}



// Used in RT YUYV playback
// Apply the inverse horizontal transform to reconstruct a strip of rows into packed YUV pixels
void InvertHorizontalStrip16sRGB2YUV(HorizontalFilterParams)
{
	int num_channels = CODEC_NUM_CHANNELS;
	int height = roi.height;
	int width = roi.width;

	// Note that the u and v chroma values are swapped
	PIXEL *gg_lowpass_ptr = lowpass_band[0];
	PIXEL *rg_lowpass_ptr = lowpass_band[1];
	PIXEL *bg_lowpass_ptr = lowpass_band[2];
	PIXEL *gg_highpass_ptr = highpass_band[0];
	PIXEL *rg_highpass_ptr = highpass_band[1];
	PIXEL *bg_highpass_ptr = highpass_band[2];

	uint8_t *output = output_image;

	// Process 8 luma coefficients per loop iteration
	const int column_step = 8;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int channel;
	int row;

	// Compute the amount of scaling required to reduce the output precision
	int descale_shift = (precision - 8);

	int shift = 8;

	float fy_rmult,fy_gmult,fy_bmult,fy_offset;
	float fu_rmult,fu_gmult,fu_bmult,fu_offset;
	float fv_rmult,fv_gmult,fv_bmult,fv_offset;

	int color_space = decoder->frame.colorspace;

	float rgb2yuv[3][4];
	float scale;

	switch(color_space & COLORSPACE_MASK)
	{
		case COLOR_SPACE_CG_601: //601
			{
			float rgb2yuv601[3][4] =
			{	{0.257f, 0.504f, 0.098f, 16.0f/255.0f},
                {-0.148f,-0.291f, 0.439f, 128.0f/255.0f},
                {0.439f,-0.368f,-0.071f, 128.0f/255.0f}
			};
			memcpy(rgb2yuv,rgb2yuv601, sizeof(rgb2yuv));
			}
			break;
		default: assert(0);
		case COLOR_SPACE_CG_709:
			{
			float rgb2yuv709[3][4] =
			{
                {0.183f, 0.614f, 0.062f, 16.0f/255.0f},
                {-0.101f,-0.338f, 0.439f, 128.0f/255.0f},
                {0.439f,-0.399f,-0.040f, 128.0f/255.0f}
			};
			memcpy(rgb2yuv,rgb2yuv709, sizeof(rgb2yuv));
			}
			break;
		case COLOR_SPACE_VS_601: //VS 601
			{
			float rgb2yuvVS601[3][4] =
			{
                {0.299f,0.587f,0.114f,0},
                {-0.172f,-0.339f,0.511f,128.0f/255.0f},
                {0.511f,-0.428f,-0.083f,128.0f/255.0f}
			};
			memcpy(rgb2yuv,rgb2yuvVS601, sizeof(rgb2yuv));
			}
			break;
		case COLOR_SPACE_VS_709:
			{
			float rgb2yuvVS709[3][4] =
			{
                {0.213f,0.715f,0.072f,0},
                {-0.117f,-0.394f,0.511f,128.0f/255.0f},
                {0.511f,-0.464f,-0.047f,128.0f/255.0f}
			};
			memcpy(rgb2yuv,rgb2yuvVS709, sizeof(rgb2yuv));
			}
			break;
	}



	// We use 16-bit fixed-point arithmetic to approximate the color conversion coefficients
	//
	// Floating point arithmetic is
	//
	// Y  = 0.257 * R + 0.504 * G + 0.098 * B + 16.5;
	// Cb =-0.148 * R - 0.291 * G + 0.439 * B + 128.5;
	// Cr = 0.439 * R - 0.368 * G - 0.071 * B + 128.5;
	//
	// Fixed point approximation (8-bit) is
	//
	// Y  = ( 66 * R + 129 * G +  25 * B +  4224) >> 8;
	// Cb = (-38 * R -  74 * G + 112 * B + 32896) >> 8;
	// Cr = (112 * R -  94 * G -  18 * B + 32896) >> 8;

	// 601 video systems
	// Y = 0.299R + 0.587G + 0.114B
	// Cb = -0.172R - 0.339G + 0.511B + 128
	// Cr = 0.511R - 0.428G - 0.083B + 128

	// 709 video systems
	// Y = 0.213R + 0.715G + 0.072B
	// Cb = -0.117R - 0.394G + 0.511B + 128
	// Cr = 0.511R - 0.464G - 0.047B + 128

	// 709 sRGB
	// Y = 0.183R + 0.614G + 0.062B + 16
	// Cb = -0.101R - 0.338G + 0.439B + 128
	// Cr = 0.439R - 0.399G - 0.040B + 128

	scale = 64.0;

	fy_rmult = ((rgb2yuv[0][0]) * scale);
	fy_gmult = ((rgb2yuv[0][1]) * scale);
	fy_bmult = ((rgb2yuv[0][2]) * scale);
	fy_offset= ((rgb2yuv[0][3]) * 16384.0f);

	fu_rmult = ((rgb2yuv[1][0]) * scale);
	fu_gmult = ((rgb2yuv[1][1]) * scale);
	fu_bmult = ((rgb2yuv[1][2]) * scale);
	fu_offset= ((rgb2yuv[1][3]) * 16384.0f);

	fv_rmult = ((rgb2yuv[2][0]) * scale);
	fv_gmult = ((rgb2yuv[2][1]) * scale);
	fv_bmult = ((rgb2yuv[2][2]) * scale);
	fv_offset= ((rgb2yuv[2][3]) * 16384.0f);

	shift-=2;


	// Convert the pitch to units of pixels
	for (channel = 0; channel < num_channels; channel++)
	{
		lowpass_pitch[channel] /= sizeof(PIXEL);
		highpass_pitch[channel] /= sizeof(PIXEL);
	}

	output_pitch /= sizeof(uint8_t);

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
		__m128i gg_low1_epi16;		// Lowpass coefficients
		__m128i gg_low2_epi16;
		__m128i bg_low1_epi16;
		__m128i bg_low2_epi16;
		__m128i rg_low1_epi16;
		__m128i rg_low2_epi16;

		__m128i gg_high1_epi16;		// Highpass coefficients
		__m128i gg_high2_epi16;
		__m128i bg_high1_epi16;
		__m128i bg_high2_epi16;
		__m128i rg_high1_epi16;
		__m128i rg_high2_epi16;

		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr = (__m128i *)&output[0];

		const __m128i mask_epi32 = _mm_set1_epi32(0xffff);

		__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x00ff);
		__m128i limiter = _mm_set1_epi16(0x7fff - 0x3fff);

#endif
		uint8_t *colptr = (uint8_t *)&output[0];

		int32_t gg_even_value;
		int32_t bg_even_value;
		int32_t rg_even_value;
		int32_t gg_odd_value;
		int32_t bg_odd_value;
		int32_t rg_odd_value;


		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		int32_t even = 0;
		int32_t odd = 0;


		__m128i rounding1_pi16 = _mm_set1_epi16(0); // for 6bit matm
		__m128i rounding2_pi16 = _mm_set1_epi16(0); // for 6bit matm

		if(descale_shift>=2)
		{
			int mask = (1<<(descale_shift-1))-1;//DAN20090601
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 0);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 1);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 2);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 3);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 4);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 5);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 6);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 7);

			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 0);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 1);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 2);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 3);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 4);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 5);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 6);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 7);

			rounding1_pi16 = _mm_adds_epi16(rounding1_pi16, _mm_set1_epi16(10*mask/32)); //DAN20090601
			rounding2_pi16 = _mm_adds_epi16(rounding2_pi16, _mm_set1_epi16(10*mask/32));
		}



		// Apply the even reconstruction filter to the lowpass band
		even += 11 * gg_lowpass_ptr[column + 0];
		even -=  4 * gg_lowpass_ptr[column + 1];
		even +=  1 * gg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += gg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		gg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * gg_lowpass_ptr[column + 0];
		odd += 4 * gg_lowpass_ptr[column + 1];
		odd -= 1 * gg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= gg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		gg_odd_value = odd;


		// Process the first two u chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * bg_lowpass_ptr[column + 0];
		even -=  4 * bg_lowpass_ptr[column + 1];
		even +=  1 * bg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += bg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		bg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * bg_lowpass_ptr[column + 0];
		odd += 4 * bg_lowpass_ptr[column + 1];
		odd -= 1 * bg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= bg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		bg_odd_value = odd;


		// Process the first two v chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * rg_lowpass_ptr[column + 0];
		even -=  4 * rg_lowpass_ptr[column + 1];
		even +=  1 * rg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += rg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		rg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * rg_lowpass_ptr[column + 0];
		odd += 4 * rg_lowpass_ptr[column + 1];
		odd -= 1 * rg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= rg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		rg_odd_value = odd;

#if (1 && XMMOPT)

		// Preload the first eight lowpass luma coefficients
		gg_low1_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[0]);

		// Preload the first eight highpass luma coefficients
		gg_high1_epi16 = _mm_load_si128((__m128i *)&gg_highpass_ptr[0]);

		// Preload the first eight lowpass u chroma coefficients
		bg_low1_epi16 = _mm_load_si128((__m128i *)&bg_lowpass_ptr[0]);

		// Preload the first eight highpass u chroma coefficients
		bg_high1_epi16 = _mm_load_si128((__m128i *)&bg_highpass_ptr[0]);

		// Preload the first eight lowpass v chroma coefficients
		rg_low1_epi16 = _mm_load_si128((__m128i *)&rg_lowpass_ptr[0]);

		// Preload the first eight highpass v chroma coefficients
 		rg_high1_epi16 = _mm_load_si128((__m128i *)&rg_highpass_ptr[0]);


		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i g1_output_epi16;
			__m128i g2_output_epi16;
			__m128i r1_output_epi16;
			__m128i r2_output_epi16;
			__m128i b1_output_epi16;
			__m128i b2_output_epi16;

			__m128i gg1_output_epi16;
			__m128i gg2_output_epi16;
			__m128i rg1_output_epi16;
			__m128i rg2_output_epi16;
			__m128i bg1_output_epi16;
			__m128i bg2_output_epi16;

			__m128i y1_output_epi16;
			__m128i y2_output_epi16;
			__m128i u1_output_epi16;
			__m128i u2_output_epi16;
			__m128i v1_output_epi16;
			__m128i v2_output_epi16;

			__m128i urg_epi16;
			__m128i yuv1_epi16;
			__m128i yuv2_epi16;
			__m128i yuv1_epi8;
			__m128i yuv2_epi8;

			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i temp_epi32;
			__m128i tempB_epi32;
			__m128i rgb_epi32;
			__m128i zero_epi128;
			__m128  temp_ps;
			__m128  rgb_ps;
			__m128	y1a_ps;
			__m128	y1b_ps;
			__m128	u1a_ps;
			__m128	u1b_ps;
			__m128	v1a_ps;
			__m128	v1b_ps;


			__m128i out_epi16;		// Reconstructed data
			////__m128i mask_epi16;
			////__m128i lsb_epi16;
			////__m128i sign_epi16;
			//__m128i high_epi16;

			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;

			//DAN031304 -- correct inverse filter
			__m128i half_epi16 = _mm_set1_epi16(4);
			__m128i offset_epi16 = _mm_set1_epi16(2048);

			uint32_t temp;		// Temporary register for last two values


			/***** Compute the first eight luma output values *****/

			// Preload the second eight lowpass coefficients
			gg_low2_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			gg_high2_epi16 = _mm_load_si128((__m128i *)&gg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = gg_low1_epi16;
			high1_epi16 = gg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);


			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_odd_value, 1);

			// Save the eight luma values for packing later
			gg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			gg_even_value = (short)temp;
			gg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight luma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = gg_low2_epi16;
			high2_epi16 = gg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_odd_value, 1);

			// Save the eight luma values for packing later
			gg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			gg_even_value = (short)temp;
			gg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are used later in the loop
			gg_low1_epi16 = gg_low2_epi16;

			// The second eight highpass coefficients are used later in the loop
			gg_high1_epi16 = gg_high2_epi16;


			/***** Compute the first eight u chroma output values *****/

			// Preload the second eight lowpass coefficients
			bg_low2_epi16 = _mm_load_si128((__m128i *)&bg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			bg_high2_epi16 = _mm_load_si128((__m128i *)&bg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = bg_low1_epi16;
			high1_epi16 = bg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			bg_even_value = (short)temp;
			bg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight u chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = bg_low2_epi16;
			high2_epi16 = bg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			bg_even_value = (short)temp;
			bg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			bg_low1_epi16 = bg_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			bg_high1_epi16 = bg_high2_epi16;



			/***** Compute the first eight v chroma output values *****/

			// Preload the second eight lowpass coefficients
			rg_low2_epi16 = _mm_load_si128((__m128i *)&rg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			rg_high2_epi16 = _mm_load_si128((__m128i *)&rg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = rg_low1_epi16;
			high1_epi16 = rg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			rg_even_value = (short)temp;
			rg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight v chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = rg_low2_epi16;
			high2_epi16 = rg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			rg_even_value = (short)temp;
			rg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			rg_low1_epi16 = rg_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			rg_high1_epi16 = rg_high2_epi16;


			//r_output_epi16  = ((rg_output_epi16 - 32768)<<1)+gg_output_epi16
			//r_output_epi16  = ((rg_output_epi16>>3 - 32768>>3))+gg_output_epi16>>4


			g1_output_epi16 = gg1_output_epi16;

			r1_output_epi16 = rg1_output_epi16;//_mm_srli_epi16(rg1_output_epi16,2);
		//	r1_output_epi16 = _mm_subs_epi16(r1_output_epi16, value128_epi32);
		//	r1_output_epi16 = _mm_slli_epi16(r1_output_epi16,1);
		//	r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, g1_output_epi16);

			b1_output_epi16 = bg1_output_epi16;//_mm_srli_epi16(bg1_output_epi16,2);
		//	b1_output_epi16 = _mm_subs_epi16(b1_output_epi16, value128_epi32);
		//	b1_output_epi16 = _mm_slli_epi16(b1_output_epi16,1);
		//	b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, g1_output_epi16);


			 r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, limiterRGB);
			 r1_output_epi16 = _mm_subs_epu16(r1_output_epi16, limiterRGB);

			 g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, limiterRGB);
			 g1_output_epi16 = _mm_subs_epu16(g1_output_epi16, limiterRGB);

			 b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, limiterRGB);
			 b1_output_epi16 = _mm_subs_epu16(b1_output_epi16, limiterRGB);


			zero_epi128 = _mm_setzero_si128();


			// Compute Y,U,V
			rgb_epi32 = _mm_unpacklo_epi16(r1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1a_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1a_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1a_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);
			rgb_epi32 = _mm_unpackhi_epi16(r1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1b_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1b_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1b_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);

			rgb_epi32 = _mm_unpacklo_epi16(g1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(g1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			rgb_epi32 = _mm_unpacklo_epi16(b1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(b1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_ps = _mm_set_ps1(fy_offset);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fu_offset);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fv_offset);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_epi32 = _mm_cvtps_epi32(y1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(y1b_ps);
			y1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			y1_output_epi16 = _mm_adds_epi16(y1_output_epi16, limiter);
			y1_output_epi16 = _mm_subs_epu16(y1_output_epi16, limiter);
			y1_output_epi16 = _mm_srli_epi16(y1_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(u1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(u1b_ps);
			u1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			u1_output_epi16 = _mm_adds_epi16(u1_output_epi16, limiter);
			u1_output_epi16 = _mm_subs_epu16(u1_output_epi16, limiter);
			u1_output_epi16 = _mm_srli_epi16(u1_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(v1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(v1b_ps);
			v1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			v1_output_epi16 = _mm_adds_epi16(v1_output_epi16, limiter);
			v1_output_epi16 = _mm_subs_epu16(v1_output_epi16, limiter);
			v1_output_epi16 = _mm_srli_epi16(v1_output_epi16, shift);


			g2_output_epi16 = gg2_output_epi16;//_mm_srli_epi16(gg2_output_epi16,2);

			r2_output_epi16 = rg2_output_epi16;//_mm_srli_epi16(rg2_output_epi16,2);
		//	r2_output_epi16 = _mm_subs_epi16(r2_output_epi16, value128_epi32);
		//	r2_output_epi16 = _mm_slli_epi16(r2_output_epi16,1);
		//	r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, g2_output_epi16);

			b2_output_epi16 = bg2_output_epi16;//_mm_srli_epi16(bg2_output_epi16,2);
		//	b2_output_epi16 = _mm_subs_epi16(b2_output_epi16, value128_epi32);
		//	b2_output_epi16 = _mm_slli_epi16(b2_output_epi16,1);
		//	b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, g2_output_epi16);


			 r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, limiterRGB);
			 r2_output_epi16 = _mm_subs_epu16(r2_output_epi16, limiterRGB);

			 g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, limiterRGB);
			 g2_output_epi16 = _mm_subs_epu16(g2_output_epi16, limiterRGB);

			 b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, limiterRGB);
			 b2_output_epi16 = _mm_subs_epu16(b2_output_epi16, limiterRGB);


			// Compute Y,U,V
			rgb_epi32 = _mm_unpacklo_epi16(r2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1a_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1a_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1a_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);
			rgb_epi32 = _mm_unpackhi_epi16(r2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1b_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1b_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1b_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);

			rgb_epi32 = _mm_unpacklo_epi16(g2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(g2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			rgb_epi32 = _mm_unpacklo_epi16(b2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(b2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_ps = _mm_set_ps1(fy_offset);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fu_offset);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fv_offset);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_epi32 = _mm_cvtps_epi32(y1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(y1b_ps);
			y2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			y2_output_epi16 = _mm_adds_epi16(y2_output_epi16, limiter);
			y2_output_epi16 = _mm_subs_epu16(y2_output_epi16, limiter);
			y2_output_epi16 = _mm_srli_epi16(y2_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(u1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(u1b_ps);
			u2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			u2_output_epi16 = _mm_adds_epi16(u2_output_epi16, limiter);
			u2_output_epi16 = _mm_subs_epu16(u2_output_epi16, limiter);
			u2_output_epi16 = _mm_srli_epi16(u2_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(v1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(v1b_ps);
			v2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			v2_output_epi16 = _mm_adds_epi16(v2_output_epi16, limiter);
			v2_output_epi16 = _mm_subs_epu16(v2_output_epi16, limiter);
			v2_output_epi16 = _mm_srli_epi16(v2_output_epi16, shift);

			/***** Interleave the luma and chroma values *****/
			if(decoder->frame.format == DECODED_FORMAT_R408 || decoder->frame.format == DECODED_FORMAT_V408)
			{
				__m128i y_epi8 = _mm_packus_epi16(y1_output_epi16, y2_output_epi16); //pack to 8-bit
				__m128i u_epi8 = _mm_packus_epi16(u1_output_epi16, u2_output_epi16); //pack to 8-bit
				__m128i v_epi8 = _mm_packus_epi16(v1_output_epi16, v2_output_epi16); //pack to 8-bit
				__m128i a_epi8 =  _mm_set1_epi8(0xff);


				if(decoder->frame.format == COLOR_FORMAT_V408) // UYVA
				{
					__m128i UY,VA,UYVA;

					UY = _mm_unpacklo_epi8(u_epi8, y_epi8);
					VA = _mm_unpacklo_epi8(v_epi8, a_epi8);
					UYVA = _mm_unpacklo_epi16(UY, VA);
					_mm_storeu_si128(outptr++, UYVA);

					UYVA = _mm_unpackhi_epi16(UY, VA);
					_mm_storeu_si128(outptr++, UYVA);

					UY = _mm_unpackhi_epi8(u_epi8, y_epi8);
					VA = _mm_unpackhi_epi8(v_epi8, a_epi8);
					UYVA = _mm_unpacklo_epi16(UY, VA);
					_mm_storeu_si128(outptr++, UYVA);

					UYVA = _mm_unpackhi_epi16(UY, VA);
					_mm_storeu_si128(outptr++, UYVA);
				}
				else //r408 AYUV
				{
					__m128i AY,UV,AYUV;
					__m128i offsetR408_epi8 =  _mm_set1_epi8(16);

					y_epi8 = _mm_subs_epu8(y_epi8, offsetR408_epi8);

					AY = _mm_unpacklo_epi8(a_epi8, y_epi8);
					UV = _mm_unpacklo_epi8(u_epi8, v_epi8);
					AYUV = _mm_unpacklo_epi16(AY, UV);
					_mm_storeu_si128(outptr++, AYUV);

					AYUV = _mm_unpackhi_epi16(AY, UV);
					_mm_storeu_si128(outptr++, AYUV);

					AY = _mm_unpackhi_epi8(a_epi8, y_epi8);
					UV = _mm_unpackhi_epi8(u_epi8, v_epi8);
					AYUV = _mm_unpacklo_epi16(AY, UV);
					_mm_storeu_si128(outptr++, AYUV);

					AYUV = _mm_unpackhi_epi16(AY, UV);
					_mm_storeu_si128(outptr++, AYUV);
				}
			}
			else
			{
				// 4:4:4 to 4:2:2
				temp_epi16 = _mm_srli_si128(u1_output_epi16, 2);
				u1_output_epi16 = _mm_adds_epi16(u1_output_epi16, temp_epi16);
				u1_output_epi16 = _mm_srli_epi16(u1_output_epi16, 1);
				temp_epi16 = _mm_srli_si128(u2_output_epi16, 2);
				u2_output_epi16 = _mm_adds_epi16(u2_output_epi16, temp_epi16);
				u2_output_epi16 = _mm_srli_epi16(u2_output_epi16, 1);
				temp_epi16 = _mm_srli_si128(v1_output_epi16, 2);
				v1_output_epi16 = _mm_adds_epi16(v1_output_epi16, temp_epi16);
				v1_output_epi16 = _mm_srli_epi16(v1_output_epi16, 1);
				temp_epi16 = _mm_srli_si128(v2_output_epi16, 2);
				v2_output_epi16 = _mm_adds_epi16(v2_output_epi16, temp_epi16);
				v2_output_epi16 = _mm_srli_epi16(v2_output_epi16, 1);
				u1_output_epi16 = _mm_and_si128(u1_output_epi16, mask_epi32);
				u2_output_epi16 = _mm_and_si128(u2_output_epi16, mask_epi32);
				v1_output_epi16 = _mm_and_si128(v1_output_epi16, mask_epi32);
				v2_output_epi16 = _mm_and_si128(v2_output_epi16, mask_epi32);

				u1_output_epi16 = _mm_packs_epi32 (u1_output_epi16, u2_output_epi16);
				v1_output_epi16 = _mm_packs_epi32 (v1_output_epi16, v2_output_epi16);

				if(decoder->frame.format == DECODED_FORMAT_YUYV) // UYVA
				{
					// Interleave the first four values from each chroma channel
					urg_epi16 = _mm_unpacklo_epi16(u1_output_epi16, v1_output_epi16);

					// Interleave the first eight chroma values with the first eight luma values
					yuv1_epi16 = _mm_unpacklo_epi16(y1_output_epi16, urg_epi16);
					yuv2_epi16 = _mm_unpackhi_epi16(y1_output_epi16, urg_epi16);

					// Pack the first sixteen bytes of luma and chroma
					yuv1_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

					// Store the first sixteen bytes of output values
					_mm_store_si128(outptr++, yuv1_epi8);

					// Interleave the second four values from each chroma channel
					urg_epi16 = _mm_unpackhi_epi16(u1_output_epi16, v1_output_epi16);

					// Interleave the second eight chroma values with the second eight luma values
					yuv1_epi16 = _mm_unpacklo_epi16(y2_output_epi16, urg_epi16);
					yuv2_epi16 = _mm_unpackhi_epi16(y2_output_epi16, urg_epi16);

					// Pack the second sixteen bytes of luma and chroma
					yuv2_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

					// Store the second sixteen bytes of output values
					_mm_store_si128(outptr++, yuv2_epi8);
				}
				else
				{
					// This block of code only works for 8-bit CbYCrY with 4:2:2 sampling
					//int format = decoder->frame.format;
					assert(decoder->frame.format == DECODED_FORMAT_UYVY || decoder->frame.format == DECODED_FORMAT_CbYCrY_8bit);

					// Interleave the first four values from each chroma channel
					urg_epi16 = _mm_unpacklo_epi16(u1_output_epi16, v1_output_epi16);

					// Interleave the first eight chroma values with the first eight luma values
					yuv1_epi16 = _mm_unpacklo_epi16(urg_epi16, y1_output_epi16);
					yuv2_epi16 = _mm_unpackhi_epi16(urg_epi16, y1_output_epi16);

					// Pack the first sixteen bytes of luma and chroma
					yuv1_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

					// Store the first sixteen bytes of output values
					_mm_store_si128(outptr++, yuv1_epi8);

					// Interleave the second four values from each chroma channel
					urg_epi16 = _mm_unpackhi_epi16(u1_output_epi16, v1_output_epi16);

					// Interleave the second eight chroma values with the second eight luma values
					yuv1_epi16 = _mm_unpacklo_epi16(urg_epi16, y2_output_epi16);
					yuv2_epi16 = _mm_unpackhi_epi16(urg_epi16, y2_output_epi16);

					// Pack the second sixteen bytes of luma and chroma
					yuv2_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

					// Store the second sixteen bytes of output values
					_mm_store_si128(outptr++, yuv2_epi8);
				}
			}
		}

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

		colptr = (uint8_t *)outptr;
#endif

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column ++)
		{
			int re,ge,be;
			int ro,go,bo;
			int ye,yo,u,v;
			int ue,uo,ve,vo;


			/***** First pair of luma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += gg_lowpass_ptr[column - 1];
			even -= gg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += gg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += gg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
		//	even >>= descale_shift;

			// Save the luma result for later output in the correct order
		//	y1_even_value = even;
			ge = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= gg_lowpass_ptr[column - 1];
			odd += gg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += gg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= gg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
		//	odd >>= descale_shift;

			// Save the luma result for later output in the correct order
		//	y1_odd_value = odd;
			go = odd;


			/***** Pair of u chroma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += bg_lowpass_ptr[column - 1];
			even -= bg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += bg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += bg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
		//	even >>= descale_shift;

			// Save the u chroma result for later output in the correct order
		//	bg_even_value = even;
			be = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= bg_lowpass_ptr[column - 1];
			odd += bg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += bg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= bg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
		//	odd >>= descale_shift;

			// Save the u chroma result for later output in the correct order
		//	bg_odd_value = odd;
			bo = odd;



			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += rg_lowpass_ptr[column - 1];
			even -= rg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += rg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += rg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
		//	even >>= descale_shift;

			// Save the v chroma result for later output in the correct order
		//	rg_even_value = even;
			re = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= rg_lowpass_ptr[column - 1];
			odd += rg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += rg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= rg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
		//	odd >>= descale_shift;

			// Save the v chroma result for later output in the correct order
		//	rg_odd_value = odd;
			ro = odd;


		// We use 16-bit fixed-point arithmetic to approximate the color conversion coefficients
		//
		// Floating point arithmetic is
		//

			switch(decoder->frame.format)
			{
			case DECODED_FORMAT_R408: //AYUV
			case DECODED_FORMAT_V408: //UYVA
				ye = ((int)((fy_rmult * (float)re + fy_gmult * (float)ge + fy_bmult * (float)be)) >> (descale_shift + 6)) + 16;
				yo = ((int)((fy_rmult * (float)ro + fy_gmult * (float)go + fy_bmult * (float)bo)) >> (descale_shift + 6)) + 16;
				ue = ((int)((fu_rmult * (float)re + fu_gmult * (float)ge + fu_bmult * (float)be)) >> (descale_shift + 6)) + 128;
				uo = ((int)((fu_rmult * (float)ro + fu_gmult * (float)go + fu_bmult * (float)bo)) >> (descale_shift + 6)) + 128;
				ve = ((int)((fv_rmult * (float)re + fv_gmult * (float)ge + fv_bmult * (float)be)) >> (descale_shift + 6)) + 128;
				vo = ((int)((fv_rmult * (float)ro + fv_gmult * (float)go + fv_bmult * (float)bo)) >> (descale_shift + 6)) + 128;
			
				if(decoder->frame.format == DECODED_FORMAT_R408)//AYUV
				{
					*(colptr++) = 0xff;
					*(colptr++) = SATURATE_8U(ye);
					*(colptr++) = SATURATE_8U(ue);
					*(colptr++) = SATURATE_8U(ve);

					*(colptr++) = 0xff;
					*(colptr++) = SATURATE_8U(yo);
					*(colptr++) = SATURATE_8U(uo);
					*(colptr++) = SATURATE_8U(vo);
				}
				else	//UYVA
				{
					*(colptr++) = SATURATE_8U(ue);
					*(colptr++) = SATURATE_8U(ye);
					*(colptr++) = SATURATE_8U(ve);
					*(colptr++) = 0xff;

					*(colptr++) = SATURATE_8U(vo);
					*(colptr++) = SATURATE_8U(yo);
					*(colptr++) = SATURATE_8U(uo);
					*(colptr++) = 0xff;
				}
				break;
			case DECODED_FORMAT_YUYV:// Output the luma and chroma values in the correct order	
			case DECODED_FORMAT_UYVY:
				ye = ((int)((fy_rmult * (float)re + fy_gmult * (float)ge + fy_bmult * (float)be)) >> (descale_shift + 6)) + 16;
				yo = ((int)((fy_rmult * (float)ro + fy_gmult * (float)go + fy_bmult * (float)bo)) >> (descale_shift + 6)) + 16;
				u  = ((int)((fu_rmult * (float)(re+ro) + fu_gmult * (float)(ge+go) + fu_bmult * (float)(be+bo))) >> (1 + descale_shift + 6)) + 128;
				v  = ((int)((fv_rmult * (float)(re+ro) + fv_gmult * (float)(ge+go) + fv_bmult * (float)(be+bo))) >> (1 + descale_shift + 6)) + 128;

				if(decoder->frame.format == DECODED_FORMAT_YUYV)
				{
					*(colptr++) = SATURATE_8U(ye);
					*(colptr++) = SATURATE_8U(u);
					*(colptr++) = SATURATE_8U(yo);
					*(colptr++) = SATURATE_8U(v);
				}
				else
				{
					*(colptr++) = SATURATE_8U(u);
					*(colptr++) = SATURATE_8U(ye);
					*(colptr++) = SATURATE_8U(v);
					*(colptr++) = SATURATE_8U(yo);
				}
				break;
			}
		}

		// Should have exited the loop at the column for right border processing
	//	assert(column == last_column);


		// Redo the last two RGB444 pixels.
		column = last_column - 1;
		colptr -= 4; //two pixels

		// Process the last luma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * gg_lowpass_ptr[column + 0];
		even += 4 * gg_lowpass_ptr[column - 1];
		even -= 1 * gg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += gg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the luma result for later output in the correct order
		gg_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * gg_lowpass_ptr[column + 0];
		odd -=  4 * gg_lowpass_ptr[column - 1];
		odd +=  1 * gg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= gg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the luma result for later output in the correct order
		gg_odd_value = odd;

		// Process the last u chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * bg_lowpass_ptr[column + 0];
		even += 4 * bg_lowpass_ptr[column - 1];
		even -= 1 * bg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += bg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		bg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * bg_lowpass_ptr[column + 0];
		odd -=  4 * bg_lowpass_ptr[column - 1];
		odd +=  1 * bg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= bg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		bg_odd_value = odd;

		// Process the last v chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * rg_lowpass_ptr[column + 0];
		even += 4 * rg_lowpass_ptr[column - 1];
		even -= 1 * rg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += rg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		rg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * rg_lowpass_ptr[column + 0];
		odd -=  4 * rg_lowpass_ptr[column - 1];
		odd +=  1 * rg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= rg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		rg_odd_value = odd;

		{
			int ye,yo,ue,uo,ve,vo,u,v;
			int re = rg_even_value;
			int ro = rg_odd_value;
			int ge = gg_even_value;
			int go = gg_odd_value;
			int be = bg_even_value;
			int bo = bg_odd_value;

			switch(decoder->frame.format)
			{
			case DECODED_FORMAT_R408: //AYUV
			case DECODED_FORMAT_V408: //UYVA
				ye = ((int)((fy_rmult * (float)re + fy_gmult * (float)ge + fy_bmult * (float)be)) >> (descale_shift + 6)) + 16;
				yo = ((int)((fy_rmult * (float)ro + fy_gmult * (float)go + fy_bmult * (float)bo)) >> (descale_shift + 6)) + 16;
				ue = ((int)((fu_rmult * (float)re + fu_gmult * (float)ge + fu_bmult * (float)be)) >> (descale_shift + 6)) + 128;
				uo = ((int)((fu_rmult * (float)ro + fu_gmult * (float)go + fu_bmult * (float)bo)) >> (descale_shift + 6)) + 128;
				ve = ((int)((fv_rmult * (float)re + fv_gmult * (float)ge + fv_bmult * (float)be)) >> (descale_shift + 6)) + 128;
				vo = ((int)((fv_rmult * (float)ro + fv_gmult * (float)go + fv_bmult * (float)bo)) >> (descale_shift + 6)) + 128;
			
				if(decoder->frame.format == DECODED_FORMAT_R408)//AYUV
				{
					*(colptr++) = 0xff;
					*(colptr++) = SATURATE_8U(ye);
					*(colptr++) = SATURATE_8U(ue);
					*(colptr++) = SATURATE_8U(ve);

					*(colptr++) = 0xff;
					*(colptr++) = SATURATE_8U(yo);
					*(colptr++) = SATURATE_8U(uo);
					*(colptr++) = SATURATE_8U(vo);
				}
				else	//UYVA
				{
					*(colptr++) = SATURATE_8U(ue);
					*(colptr++) = SATURATE_8U(ye);
					*(colptr++) = SATURATE_8U(ve);
					*(colptr++) = 0xff;

					*(colptr++) = SATURATE_8U(vo);
					*(colptr++) = SATURATE_8U(yo);
					*(colptr++) = SATURATE_8U(uo);
					*(colptr++) = 0xff;
				}
				break;
			case DECODED_FORMAT_YUYV:// Output the luma and chroma values in the correct order	
			case DECODED_FORMAT_UYVY:
				ye = ((int)((fy_rmult * (float)re + fy_gmult * (float)ge + fy_bmult * (float)be)) >> (descale_shift + 6)) + 16;
				yo = ((int)((fy_rmult * (float)ro + fy_gmult * (float)go + fy_bmult * (float)bo)) >> (descale_shift + 6)) + 16;
				u  = ((int)((fu_rmult * (float)(re+ro) + fu_gmult * (float)(ge+go) + fu_bmult * (float)(be+bo))) >> (1 + descale_shift + 6)) + 128;
				v  = ((int)((fv_rmult * (float)(re+ro) + fv_gmult * (float)(ge+go) + fv_bmult * (float)(be+bo))) >> (1 + descale_shift + 6)) + 128;

				if(decoder->frame.format == DECODED_FORMAT_YUYV)
				{
					*(colptr++) = SATURATE_8U(ye);
					*(colptr++) = SATURATE_8U(u);
					*(colptr++) = SATURATE_8U(yo);
					*(colptr++) = SATURATE_8U(v);
				}
				else
				{
					*(colptr++) = SATURATE_8U(u);
					*(colptr++) = SATURATE_8U(ye);
					*(colptr++) = SATURATE_8U(v);
					*(colptr++) = SATURATE_8U(yo);
				}
				break;
			}
		}


		// Advance to the next row of coefficients in each channel
		gg_lowpass_ptr += lowpass_pitch[0];
		bg_lowpass_ptr += lowpass_pitch[1];
		rg_lowpass_ptr += lowpass_pitch[2];
		gg_highpass_ptr += highpass_pitch[0];
		bg_highpass_ptr += highpass_pitch[1];
		rg_highpass_ptr += highpass_pitch[2];

		// Advance the output pointer to the next row
		output += output_pitch;
	}
}


void InvertHorizontalStrip16sRGBA2YUVA(HorizontalFilterParams)
{
	int num_channels = CODEC_MAX_CHANNELS; // need alpha
	int height = roi.height;
	int width = roi.width;

	// Note that the u and v chroma values are swapped
	PIXEL *g_lowpass_ptr = lowpass_band[0];
	PIXEL *r_lowpass_ptr = lowpass_band[1];
	PIXEL *b_lowpass_ptr = lowpass_band[2];
	PIXEL *a_lowpass_ptr = lowpass_band[3];
	PIXEL *g_highpass_ptr = highpass_band[0];
	PIXEL *r_highpass_ptr = highpass_band[1];
	PIXEL *b_highpass_ptr = highpass_band[2];
	PIXEL *a_highpass_ptr = highpass_band[3];

	uint8_t *output = output_image;

	// Process 8 luma coefficients per loop iteration
	const int column_step = 8;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int channel;
	int row;

	// Compute the amount of scaling required to reduce the output precision
	int descale_shift = (precision - 8);

	int shift = 8;

	float fy_rmult,fy_gmult,fy_bmult,fy_offset;
	float fu_rmult,fu_gmult,fu_bmult,fu_offset;
	float fv_rmult,fv_gmult,fv_bmult,fv_offset;

	int color_space = decoder->frame.colorspace;

	float rgb2yuv[3][4];
	float scale;

	
	decoder->frame.alpha_Companded = 1;

	switch(color_space & COLORSPACE_MASK)
	{
		case COLOR_SPACE_CG_601: //601
			{
			float rgb2yuv601[3][4] =
                {	{0.257f, 0.504f, 0.098f, 16.0f/255.0f},
                    {-0.148f,-0.291f, 0.439f, 128.0f/255.0f},
                    {0.439f,-0.368f,-0.071f, 128.0f/255.0f}
			};
			memcpy(rgb2yuv,rgb2yuv601, sizeof(rgb2yuv));
			}
			break;
		default: assert(0);
		case COLOR_SPACE_CG_709:
			{
			float rgb2yuv709[3][4] =
			{
                {0.183f, 0.614f, 0.062f, 16.0f/255.0f},
                {-0.101f,-0.338f, 0.439f, 128.0f/255.0f},
                {0.439f,-0.399f,-0.040f, 128.0f/255.0f}
			};
			memcpy(rgb2yuv,rgb2yuv709, sizeof(rgb2yuv));
			}
			break;
		case COLOR_SPACE_VS_601: //VS 601
			{
			float rgb2yuvVS601[3][4] =
			{
                {0.299f,0.587f,0.114f,0},
                {-0.172f,-0.339f,0.511f,128.0f/255.0f},
                {0.511f,-0.428f,-0.083f,128.0f/255.0f}
			};
			memcpy(rgb2yuv,rgb2yuvVS601, sizeof(rgb2yuv));
			}
			break;
		case COLOR_SPACE_VS_709:
			{
			float rgb2yuvVS709[3][4] =
			{
                {0.213f,0.715f,0.072f,0},
                {-0.117f,-0.394f,0.511f,128.0f/255.0f},
                {0.511f,-0.464f,-0.047f,128.0f/255.0f}
			};
			memcpy(rgb2yuv,rgb2yuvVS709, sizeof(rgb2yuv));
			}
			break;
	}



	// We use 16-bit fixed-point arithmetic to approximate the color conversion coefficients
	//
	// Floating point arithmetic is
	//
	// Y  = 0.257 * R + 0.504 * G + 0.098 * B + 16.5;
	// Cb =-0.148 * R - 0.291 * G + 0.439 * B + 128.5;
	// Cr = 0.439 * R - 0.368 * G - 0.071 * B + 128.5;
	//
	// Fixed point approximation (8-bit) is
	//
	// Y  = ( 66 * R + 129 * G +  25 * B +  4224) >> 8;
	// Cb = (-38 * R -  74 * G + 112 * B + 32896) >> 8;
	// Cr = (112 * R -  94 * G -  18 * B + 32896) >> 8;

	// 601 video systems
	// Y = 0.299R + 0.587G + 0.114B
	// Cb = -0.172R - 0.339G + 0.511B + 128
	// Cr = 0.511R - 0.428G - 0.083B + 128

	// 709 video systems
	// Y = 0.213R + 0.715G + 0.072B
	// Cb = -0.117R - 0.394G + 0.511B + 128
	// Cr = 0.511R - 0.464G - 0.047B + 128

	// 709 sRGB
	// Y = 0.183R + 0.614G + 0.062B + 16
	// Cb = -0.101R - 0.338G + 0.439B + 128
	// Cr = 0.439R - 0.399G - 0.040B + 128

	scale = 64.0;

	fy_rmult = ((rgb2yuv[0][0]) * scale);
	fy_gmult = ((rgb2yuv[0][1]) * scale);
	fy_bmult = ((rgb2yuv[0][2]) * scale);
	fy_offset= ((rgb2yuv[0][3]) * 16384.0f);

	fu_rmult = ((rgb2yuv[1][0]) * scale);
	fu_gmult = ((rgb2yuv[1][1]) * scale);
	fu_bmult = ((rgb2yuv[1][2]) * scale);
	fu_offset= ((rgb2yuv[1][3]) * 16384.0f);

	fv_rmult = ((rgb2yuv[2][0]) * scale);
	fv_gmult = ((rgb2yuv[2][1]) * scale);
	fv_bmult = ((rgb2yuv[2][2]) * scale);
	fv_offset= ((rgb2yuv[2][3]) * 16384.0f);

	shift-=2;


	// Convert the pitch to units of pixels
	for (channel = 0; channel < num_channels; channel++)
	{
		lowpass_pitch[channel] /= sizeof(PIXEL);
		highpass_pitch[channel] /= sizeof(PIXEL);
	}

	output_pitch /= sizeof(uint8_t);

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
		__m128i g_low1_epi16;		// Lowpass coefficients
		__m128i g_low2_epi16;
		__m128i b_low1_epi16;
		__m128i b_low2_epi16;
		__m128i r_low1_epi16;
		__m128i r_low2_epi16;
		__m128i a_low1_epi16;
		__m128i a_low2_epi16;

		__m128i g_high1_epi16;		// Highpass coefficients
		__m128i g_high2_epi16;
		__m128i b_high1_epi16;
		__m128i b_high2_epi16;
		__m128i r_high1_epi16;
		__m128i r_high2_epi16;
		__m128i a_high1_epi16;
		__m128i a_high2_epi16;

		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr = (__m128i *)&output[0];

		__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x00ff);
		__m128i limiter = _mm_set1_epi16(0x7fff - 0x3fff);

#endif
		uint8_t *colptr = (uint8_t *)&output[0];

		int32_t g_even_value;
		int32_t b_even_value;
		int32_t r_even_value;
		int32_t a_even_value;
		int32_t g_odd_value;
		int32_t b_odd_value;
		int32_t r_odd_value;
		int32_t a_odd_value;


		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		int32_t even = 0;
		int32_t odd = 0;


		__m128i rounding1_pi16 = _mm_set1_epi16(0); // for 6bit matm
		__m128i rounding2_pi16 = _mm_set1_epi16(0); // for 6bit matm

		if(descale_shift>=2)
		{
			int mask = (1<<(descale_shift-1))-1;//DAN20090601
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 0);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 1);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 2);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 3);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 4);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 5);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 6);
			rounding1_pi16 = _mm_insert_epi16(rounding1_pi16, rand()&mask, 7);

			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 0);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 1);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 2);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 3);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 4);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 5);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 6);
			rounding2_pi16 = _mm_insert_epi16(rounding2_pi16, rand()&mask, 7);

			rounding1_pi16 = _mm_adds_epi16(rounding1_pi16, _mm_set1_epi16(10*mask/32)); //DAN20090601
			rounding2_pi16 = _mm_adds_epi16(rounding2_pi16, _mm_set1_epi16(10*mask/32));
		}



		// Apply the even reconstruction filter to the lowpass band
		even += 11 * g_lowpass_ptr[column + 0];
		even -=  4 * g_lowpass_ptr[column + 1];
		even +=  1 * g_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += g_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		g_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * g_lowpass_ptr[column + 0];
		odd += 4 * g_lowpass_ptr[column + 1];
		odd -= 1 * g_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= g_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		g_odd_value = odd;


		// Process the first two u chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * b_lowpass_ptr[column + 0];
		even -=  4 * b_lowpass_ptr[column + 1];
		even +=  1 * b_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += b_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		b_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * b_lowpass_ptr[column + 0];
		odd += 4 * b_lowpass_ptr[column + 1];
		odd -= 1 * b_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= b_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		b_odd_value = odd;


		// Process the first two v chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * r_lowpass_ptr[column + 0];
		even -=  4 * r_lowpass_ptr[column + 1];
		even +=  1 * r_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += r_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the value for use in the fast loop
		r_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * r_lowpass_ptr[column + 0];
		odd += 4 * r_lowpass_ptr[column + 1];
		odd -= 1 * r_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= r_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the value for use in the fast loop
		r_odd_value = odd;
		
		
		
		// Process the first two v chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		if(decoder->codec.num_channels == 4)
		{
			even += 11 * a_lowpass_ptr[column + 0];
			even -=  4 * a_lowpass_ptr[column + 1];
			even +=  1 * a_lowpass_ptr[column + 2];
			even += ROUNDING(even,8);
			even = DivideByShift(even, 3);

			// Add the highpass correction
			even += a_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the value for use in the fast loop
			a_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd += 5 * a_lowpass_ptr[column + 0];
			odd += 4 * a_lowpass_ptr[column + 1];
			odd -= 1 * a_lowpass_ptr[column + 2];
			odd += ROUNDING(odd,8);
			odd = DivideByShift(odd, 3);

			// Subtract the highpass correction
			odd -= a_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the value for use in the fast loop
			a_odd_value = odd;
		}
		else
		{
			a_odd_value = a_even_value = 255;
		}



#if (1 && XMMOPT)

		// Preload the first eight lowpass luma coefficients
		g_low1_epi16 = _mm_load_si128((__m128i *)&g_lowpass_ptr[0]);

		// Preload the first eight highpass luma coefficients
		g_high1_epi16 = _mm_load_si128((__m128i *)&g_highpass_ptr[0]);

		// Preload the first eight lowpass u chroma coefficients
		b_low1_epi16 = _mm_load_si128((__m128i *)&b_lowpass_ptr[0]);

		// Preload the first eight highpass u chroma coefficients
		b_high1_epi16 = _mm_load_si128((__m128i *)&b_highpass_ptr[0]);

		// Preload the first eight lowpass v chroma coefficients
		r_low1_epi16 = _mm_load_si128((__m128i *)&r_lowpass_ptr[0]);

		// Preload the first eight highpass v chroma coefficients
 		r_high1_epi16 = _mm_load_si128((__m128i *)&r_highpass_ptr[0]);
 		
		if(decoder->codec.num_channels == 4)
		{
			// Preload the first eight lowpass v chroma coefficients
			a_low1_epi16 = _mm_load_si128((__m128i *)&a_lowpass_ptr[0]);

			// Preload the first eight highpass v chroma coefficients
 			a_high1_epi16 = _mm_load_si128((__m128i *)&a_highpass_ptr[0]);
		}


		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i g1_output_epi16;
			__m128i g2_output_epi16;
			__m128i r1_output_epi16;
			__m128i r2_output_epi16;
			__m128i b1_output_epi16;
			__m128i b2_output_epi16;
			__m128i a1_output_epi16;
			__m128i a2_output_epi16;

			__m128i gg1_output_epi16;
			__m128i gg2_output_epi16;
			__m128i rg1_output_epi16;
			__m128i rg2_output_epi16;
			__m128i bg1_output_epi16;
			__m128i bg2_output_epi16;
			__m128i ag1_output_epi16;
			__m128i ag2_output_epi16;

			__m128i y1_output_epi16;
			__m128i y2_output_epi16;
			__m128i u1_output_epi16;
			__m128i u2_output_epi16;
			__m128i v1_output_epi16;
			__m128i v2_output_epi16;
			
			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i temp_epi32;
			__m128i tempB_epi32;
			__m128i rgb_epi32;
			__m128i zero_epi128;
			__m128  temp_ps;
			__m128  rgb_ps;
			__m128	y1a_ps;
			__m128	y1b_ps;
			__m128	u1a_ps;
			__m128	u1b_ps;
			__m128	v1a_ps;
			__m128	v1b_ps;


			__m128i out_epi16;		// Reconstructed data
			////__m128i mask_epi16;
			////__m128i lsb_epi16;
			////__m128i sign_epi16;
			//__m128i high_epi16;

			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;

			//DAN031304 -- correct inverse filter
			__m128i half_epi16 = _mm_set1_epi16(4);
			__m128i offset_epi16 = _mm_set1_epi16(2048);

			uint32_t temp;		// Temporary register for last two values


			/***** Compute the first eight luma output values *****/

			// Preload the second eight lowpass coefficients
			g_low2_epi16 = _mm_load_si128((__m128i *)&g_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			g_high2_epi16 = _mm_load_si128((__m128i *)&g_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = g_low1_epi16;
			high1_epi16 = g_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);


			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, g_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, g_odd_value, 1);

			// Save the eight luma values for packing later
			gg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			g_even_value = (short)temp;
			g_odd_value = (short)(temp >> 16);


			/***** Compute the second eight luma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = g_low2_epi16;
			high2_epi16 = g_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, g_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, g_odd_value, 1);

			// Save the eight luma values for packing later
			gg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			g_even_value = (short)temp;
			g_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are used later in the loop
			g_low1_epi16 = g_low2_epi16;

			// The second eight highpass coefficients are used later in the loop
			g_high1_epi16 = g_high2_epi16;


			/***** Compute the first eight u chroma output values *****/

			// Preload the second eight lowpass coefficients
			b_low2_epi16 = _mm_load_si128((__m128i *)&b_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			b_high2_epi16 = _mm_load_si128((__m128i *)&b_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = b_low1_epi16;
			high1_epi16 = b_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, b_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, b_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			b_even_value = (short)temp;
			b_odd_value = (short)(temp >> 16);


			/***** Compute the second eight u chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = b_low2_epi16;
			high2_epi16 = b_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, b_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, b_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			b_even_value = (short)temp;
			b_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			b_low1_epi16 = b_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			b_high1_epi16 = b_high2_epi16;



			/***** Compute the first eight v chroma output values *****/

			// Preload the second eight lowpass coefficients
			r_low2_epi16 = _mm_load_si128((__m128i *)&r_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			r_high2_epi16 = _mm_load_si128((__m128i *)&r_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = r_low1_epi16;
			high1_epi16 = r_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, r_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, r_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			r_even_value = (short)temp;
			r_odd_value = (short)(temp >> 16);


			/***** Compute the second eight v chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = r_low2_epi16;
			high2_epi16 = r_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
			out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, r_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, r_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			r_even_value = (short)temp;
			r_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			r_low1_epi16 = r_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			r_high1_epi16 = r_high2_epi16;

			
			
			
			
			
			/***** Compute the first eight v chroma output values *****/
			if(decoder->codec.num_channels == 4)
			{
				// Preload the second eight lowpass coefficients
				a_low2_epi16 = _mm_load_si128((__m128i *)&a_lowpass_ptr[column + 8]);

				// Preload the second eight highpass coefficients
				a_high2_epi16 = _mm_load_si128((__m128i *)&a_highpass_ptr[column + 8]);

				// Move the current set of coefficients to working registers
				low1_epi16 = a_low1_epi16;
				high1_epi16 = a_high1_epi16;

				// Apply the even reconstruction filter to the lowpass band
				even_epi16 = low1_epi16;
				temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
				even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
				even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
				even_epi16 = _mm_srai_epi16(even_epi16, 3);
				temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
				even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

				// Shift the highpass correction by one column
				high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

				// Add the highpass correction and divide by two
				even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
				even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
				even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
				even_epi16 = _mm_srai_epi16(even_epi16, 1);

				// Apply the odd reconstruction filter to the lowpass band
				odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
				temp_epi16 = low1_epi16;
				odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
				odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
				odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
				temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
				odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

				// Subtract the highpass correction and divide by two
				odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
				odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
				odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
				odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

				// Interleave the four even and four odd results
				out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

				// Reduce the precision to eight bits
				out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
				out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

				// Combine the new output values with the two values from the previous phase
				out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
				temp = _mm_cvtsi128_si32(out_epi16);
				out_epi16 = _mm_insert_epi16(out_epi16, a_even_value, 0);
				out_epi16 = _mm_insert_epi16(out_epi16, a_odd_value, 1);

				// Save the eight u chroma values for packing later
				ag1_output_epi16 = out_epi16;

				// Save the remaining two output values
				a_even_value = (short)temp;
				a_odd_value = (short)(temp >> 16);


				/***** Compute the second eight v chroma output values *****/

				// Move the next set of coefficients to working registers
				low2_epi16 = a_low2_epi16;
				high2_epi16 = a_high2_epi16;

				// Shift in the new pixels for the next stage of the loop
				low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
				temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
				low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

				// Apply the even reconstruction filter to the lowpass band
				even_epi16 = low1_epi16;
				temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
				even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
				even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
				even_epi16 = _mm_srai_epi16(even_epi16, 3);
				temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
				even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

				// Shift in the next four highpass coefficients
				high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
				temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
				high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

				// Add the highpass correction and divide by two
				even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
				even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
				even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
				even_epi16 = _mm_srai_epi16(even_epi16, 1);

				// Apply the odd reconstruction filter to the lowpass band
				odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
				temp_epi16 = low1_epi16;
				odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
				odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
				odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
				temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
				odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

				// Subtract the highpass correction and divide by two
				odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
				odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
				odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
				odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

				// Interleave the four even and four odd results
				out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

				// Reduce the precision to eight bits
				out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
				out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

				// Combine the new output values with the two values from the previous phase
				out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
				temp = _mm_cvtsi128_si32(out_epi16);
				out_epi16 = _mm_insert_epi16(out_epi16, a_even_value, 0);
				out_epi16 = _mm_insert_epi16(out_epi16, a_odd_value, 1);

				// Save the eight u chroma values for packing later
				ag2_output_epi16 = out_epi16;

				// Save the remaining two output values
				a_even_value = (short)temp;
				a_odd_value = (short)(temp >> 16);

				// The second eight lowpass coefficients are the current values in the next iteration
				a_low1_epi16 = a_low2_epi16;

				// The second eight highpass coefficients are the current values in the next iteration
				a_high1_epi16 = a_high2_epi16;

			
				a1_output_epi16 = ag1_output_epi16;

				a1_output_epi16 = _mm_adds_epi16(a1_output_epi16, limiterRGB); 
				a1_output_epi16 = _mm_subs_epu16(a1_output_epi16, limiterRGB);

				a1_output_epi16 = _mm_slli_epi16(a1_output_epi16, 4);  //12-bit
				a1_output_epi16 = _mm_subs_epu16(a1_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				a1_output_epi16 = _mm_slli_epi16(a1_output_epi16, 3);  //15-bit
				a1_output_epi16 = _mm_mulhi_epi16(a1_output_epi16, _mm_set1_epi16(alphacompandGain));

				a1_output_epi16 = _mm_adds_epi16(a1_output_epi16, limiterRGB); //8-bit limit
				a1_output_epi16 = _mm_subs_epu16(a1_output_epi16, limiterRGB);
			}
			
			

			//r_output_epi16  = ((rg_output_epi16 - 32768)<<1)+gg_output_epi16
			//r_output_epi16  = ((rg_output_epi16>>3 - 32768>>3))+gg_output_epi16>>4


			g1_output_epi16 = gg1_output_epi16;

			r1_output_epi16 = rg1_output_epi16;			

			b1_output_epi16 = bg1_output_epi16;


			r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, limiterRGB);
			r1_output_epi16 = _mm_subs_epu16(r1_output_epi16, limiterRGB);

			g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, limiterRGB);
			g1_output_epi16 = _mm_subs_epu16(g1_output_epi16, limiterRGB);

			b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, limiterRGB);
			b1_output_epi16 = _mm_subs_epu16(b1_output_epi16, limiterRGB);
			
			
			 

			zero_epi128 = _mm_setzero_si128();


			// Compute Y,U,V
			rgb_epi32 = _mm_unpacklo_epi16(r1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1a_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1a_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1a_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);
			rgb_epi32 = _mm_unpackhi_epi16(r1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1b_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1b_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1b_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);

			rgb_epi32 = _mm_unpacklo_epi16(g1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(g1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			rgb_epi32 = _mm_unpacklo_epi16(b1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(b1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_ps = _mm_set_ps1(fy_offset);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fu_offset);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fv_offset);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_epi32 = _mm_cvtps_epi32(y1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(y1b_ps);
			y1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			y1_output_epi16 = _mm_adds_epi16(y1_output_epi16, limiter);
			y1_output_epi16 = _mm_subs_epu16(y1_output_epi16, limiter);
			y1_output_epi16 = _mm_srli_epi16(y1_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(u1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(u1b_ps);
			u1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			u1_output_epi16 = _mm_adds_epi16(u1_output_epi16, limiter);
			u1_output_epi16 = _mm_subs_epu16(u1_output_epi16, limiter);
			u1_output_epi16 = _mm_srli_epi16(u1_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(v1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(v1b_ps);
			v1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			v1_output_epi16 = _mm_adds_epi16(v1_output_epi16, limiter);
			v1_output_epi16 = _mm_subs_epu16(v1_output_epi16, limiter);
			v1_output_epi16 = _mm_srli_epi16(v1_output_epi16, shift);


			g2_output_epi16 = gg2_output_epi16;

			r2_output_epi16 = rg2_output_epi16;

			b2_output_epi16 = bg2_output_epi16;
			


			 r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, limiterRGB);
			 r2_output_epi16 = _mm_subs_epu16(r2_output_epi16, limiterRGB);

			 g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, limiterRGB);
			 g2_output_epi16 = _mm_subs_epu16(g2_output_epi16, limiterRGB);

			 b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, limiterRGB);
			 b2_output_epi16 = _mm_subs_epu16(b2_output_epi16, limiterRGB);

			 if(decoder->codec.num_channels == 4)
			 {
				a2_output_epi16 = ag2_output_epi16;
				 
				a2_output_epi16 = _mm_adds_epi16(a2_output_epi16, limiterRGB); //12-bit limit
				a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, limiterRGB);

				a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 4);  //12-bit
				a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
				a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));

				a2_output_epi16 = _mm_adds_epi16(a2_output_epi16, limiterRGB); //8-bit limit
				a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, limiterRGB);
			 }	

			// Compute Y,U,V
			rgb_epi32 = _mm_unpacklo_epi16(r2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1a_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1a_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1a_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);
			rgb_epi32 = _mm_unpackhi_epi16(r2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1b_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1b_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1b_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);

			rgb_epi32 = _mm_unpacklo_epi16(g2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(g2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			rgb_epi32 = _mm_unpacklo_epi16(b2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(b2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_ps = _mm_set_ps1(fy_offset);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fu_offset);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fv_offset);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_epi32 = _mm_cvtps_epi32(y1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(y1b_ps);
			y2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			y2_output_epi16 = _mm_adds_epi16(y2_output_epi16, limiter);
			y2_output_epi16 = _mm_subs_epu16(y2_output_epi16, limiter);
			y2_output_epi16 = _mm_srli_epi16(y2_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(u1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(u1b_ps);
			u2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			u2_output_epi16 = _mm_adds_epi16(u2_output_epi16, limiter);
			u2_output_epi16 = _mm_subs_epu16(u2_output_epi16, limiter);
			u2_output_epi16 = _mm_srli_epi16(u2_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(v1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(v1b_ps);
			v2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
			v2_output_epi16 = _mm_adds_epi16(v2_output_epi16, limiter);
			v2_output_epi16 = _mm_subs_epu16(v2_output_epi16, limiter);
			v2_output_epi16 = _mm_srli_epi16(v2_output_epi16, shift);

			/***** Interleave the luma and chroma values *****/
			if(decoder->frame.format == DECODED_FORMAT_R408 || decoder->frame.format == DECODED_FORMAT_V408)
			{
				__m128i y_epi8 = _mm_packus_epi16(y1_output_epi16, y2_output_epi16); //pack to 8-bit
				__m128i u_epi8 = _mm_packus_epi16(u1_output_epi16, u2_output_epi16); //pack to 8-bit
				__m128i v_epi8 = _mm_packus_epi16(v1_output_epi16, v2_output_epi16); //pack to 8-bit
				__m128i a_epi8 = _mm_packus_epi16(a1_output_epi16, a2_output_epi16); //pack to 8-bit


				if(decoder->codec.num_channels == 3)
					a_epi8 = _mm_set1_epi8(0xff);
			 

				if(decoder->frame.format == COLOR_FORMAT_V408) // UYVA
				{
					__m128i UY,VA,UYVA;

					UY = _mm_unpacklo_epi8(u_epi8, y_epi8);
					VA = _mm_unpacklo_epi8(v_epi8, a_epi8);
					UYVA = _mm_unpacklo_epi16(UY, VA);
					_mm_storeu_si128(outptr++, UYVA);

					UYVA = _mm_unpackhi_epi16(UY, VA);
					_mm_storeu_si128(outptr++, UYVA);

					UY = _mm_unpackhi_epi8(u_epi8, y_epi8);
					VA = _mm_unpackhi_epi8(v_epi8, a_epi8);
					UYVA = _mm_unpacklo_epi16(UY, VA);
					_mm_storeu_si128(outptr++, UYVA);

					UYVA = _mm_unpackhi_epi16(UY, VA);
					_mm_storeu_si128(outptr++, UYVA);
				}
				else //r408 AYUV
				{
					__m128i AY,UV,AYUV;
					__m128i offsetR408_epi8 =  _mm_set1_epi8(16);

					y_epi8 = _mm_subs_epu8(y_epi8, offsetR408_epi8);

					AY = _mm_unpacklo_epi8(a_epi8, y_epi8);
					UV = _mm_unpacklo_epi8(u_epi8, v_epi8);
					AYUV = _mm_unpacklo_epi16(AY, UV);
					_mm_storeu_si128(outptr++, AYUV);

					AYUV = _mm_unpackhi_epi16(AY, UV);
					_mm_storeu_si128(outptr++, AYUV);

					AY = _mm_unpackhi_epi8(a_epi8, y_epi8);
					UV = _mm_unpackhi_epi8(u_epi8, v_epi8);
					AYUV = _mm_unpacklo_epi16(AY, UV);
					_mm_storeu_si128(outptr++, AYUV);

					AYUV = _mm_unpackhi_epi16(AY, UV);
					_mm_storeu_si128(outptr++, AYUV);
				}
			}
		}

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

		colptr = (uint8_t *)outptr;
#endif

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column ++)
		{
			int re,ge,be,ae;
			int ro,go,bo,ao;
			int ye,yo;
			int ue,uo,ve,vo;


			/***** First pair of luma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += g_lowpass_ptr[column - 1];
			even -= g_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += g_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += g_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
		//	even >>= descale_shift;

			// Save the luma result for later output in the correct order
		//	y1_even_value = even;
			ge = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= g_lowpass_ptr[column - 1];
			odd += g_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += g_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= g_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
		//	odd >>= descale_shift;

			// Save the luma result for later output in the correct order
		//	y1_odd_value = odd;
			go = odd;


			/***** Pair of u chroma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += b_lowpass_ptr[column - 1];
			even -= b_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += b_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += b_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
		//	even >>= descale_shift;

			// Save the u chroma result for later output in the correct order
		//	b_even_value = even;
			be = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= b_lowpass_ptr[column - 1];
			odd += b_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += b_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= b_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
		//	odd >>= descale_shift;

			// Save the u chroma result for later output in the correct order
		//	b_odd_value = odd;
			bo = odd;



			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += r_lowpass_ptr[column - 1];
			even -= r_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += r_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += r_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
		//	even >>= descale_shift;

			// Save the v chroma result for later output in the correct order
		//	r_even_value = even;
			re = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= r_lowpass_ptr[column - 1];
			odd += r_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += r_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= r_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
		//	odd >>= descale_shift;

			// Save the v chroma result for later output in the correct order
		//	r_odd_value = odd;
			ro = odd;


			// Apply the even reconstruction filter to the lowpass band
			if(decoder->codec.num_channels == 4)
			{
				even = 0;
				even += a_lowpass_ptr[column - 1];
				even -= a_lowpass_ptr[column + 1];
				even += 4; //DAN20050921
				even >>= 3;
				even += a_lowpass_ptr[column + 0];

				// Add the highpass correction
				even += a_highpass_ptr[column];
				even = DivideByShift(even, 1);

				// Reduce the precision to eight bits
			//	even >>= descale_shift;

				// Save the v chroma result for later output in the correct order
			//	a_even_value = even;
				ae = even << 4; //12-bit
				
				ae -= alphacompandDCoffset;
				ae <<= 3; //15-bit
				ae *= alphacompandGain;
				ae >>= 16; //12-bit
				ae >>= 4; //8-bit
				if (ae < 0) ae = 0; else if (ae > 255) ae = 255;

				// Apply the odd reconstruction filter to the lowpass band
				odd = 0;
				odd -= a_lowpass_ptr[column - 1];
				odd += a_lowpass_ptr[column + 1];
				odd += 4; //DAN20050921
				odd >>= 3;
				odd += a_lowpass_ptr[column + 0];

				// Subtract the highpass correction
				odd -= a_highpass_ptr[column];
				odd = DivideByShift(odd, 1);

				// Reduce the precision to eight bits
			//	odd >>= descale_shift;

				// Save the v chroma result for later output in the correct order
			//	a_odd_value = odd;
				ao = odd << 4; //12-bit
				
				ao -= alphacompandDCoffset;
				ao <<= 3; //15-bit
				ao *= alphacompandGain;
				ao >>= 16; //12-bit
				ao >>= 4; //8-bit
				if (ao < 0) ao = 0; else if (ao > 255) ao = 255;
			}
			else
			{
				ae = ao = 255;
			}


		// We use 16-bit fixed-point arithmetic to approximate the color conversion coefficients
		//
		// Floating point arithmetic is
		//

			switch(decoder->frame.format)
			{
			case DECODED_FORMAT_R408: //AYUV
			case DECODED_FORMAT_V408: //UYVA
				ye = ((int)((fy_rmult * (float)re + fy_gmult * (float)ge + fy_bmult * (float)be)) >> (descale_shift + 6)) + 16;
				yo = ((int)((fy_rmult * (float)ro + fy_gmult * (float)go + fy_bmult * (float)bo)) >> (descale_shift + 6)) + 16;
				ue = ((int)((fu_rmult * (float)re + fu_gmult * (float)ge + fu_bmult * (float)be)) >> (descale_shift + 6)) + 128;
				uo = ((int)((fu_rmult * (float)ro + fu_gmult * (float)go + fu_bmult * (float)bo)) >> (descale_shift + 6)) + 128;
				ve = ((int)((fv_rmult * (float)re + fv_gmult * (float)ge + fv_bmult * (float)be)) >> (descale_shift + 6)) + 128;
				vo = ((int)((fv_rmult * (float)ro + fv_gmult * (float)go + fv_bmult * (float)bo)) >> (descale_shift + 6)) + 128;
			
				if(decoder->frame.format == DECODED_FORMAT_R408)//AYUV
				{
					*(colptr++) = SATURATE_8U(ae);
					*(colptr++) = SATURATE_8U(ye);
					*(colptr++) = SATURATE_8U(ue);
					*(colptr++) = SATURATE_8U(ve);

					*(colptr++) = SATURATE_8U(ao);
					*(colptr++) = SATURATE_8U(yo);
					*(colptr++) = SATURATE_8U(uo);
					*(colptr++) = SATURATE_8U(vo);
				}
				else	//UYVA
				{
					*(colptr++) = SATURATE_8U(ue);
					*(colptr++) = SATURATE_8U(ye);
					*(colptr++) = SATURATE_8U(ve);
					*(colptr++) = SATURATE_8U(ae);

					*(colptr++) = SATURATE_8U(vo);
					*(colptr++) = SATURATE_8U(yo);
					*(colptr++) = SATURATE_8U(uo);
					*(colptr++) = SATURATE_8U(ao);
				}
				break;
			}
		}

		// Should have exited the loop at the column for right border processing
	//	assert(column == last_column);


		// Redo the last two RGB444 pixels.
		column = last_column - 1;
		colptr -= 4; //two pixels

		// Process the last luma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * g_lowpass_ptr[column + 0];
		even += 4 * g_lowpass_ptr[column - 1];
		even -= 1 * g_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += g_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the luma result for later output in the correct order
		g_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * g_lowpass_ptr[column + 0];
		odd -=  4 * g_lowpass_ptr[column - 1];
		odd +=  1 * g_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= g_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the luma result for later output in the correct order
		g_odd_value = odd;

		// Process the last u chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * b_lowpass_ptr[column + 0];
		even += 4 * b_lowpass_ptr[column - 1];
		even -= 1 * b_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += b_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		b_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * b_lowpass_ptr[column + 0];
		odd -=  4 * b_lowpass_ptr[column - 1];
		odd +=  1 * b_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= b_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		b_odd_value = odd;

		
		// Process the last v chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * r_lowpass_ptr[column + 0];
		even += 4 * r_lowpass_ptr[column - 1];
		even -= 1 * r_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += r_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		r_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * r_lowpass_ptr[column + 0];
		odd -=  4 * r_lowpass_ptr[column - 1];
		odd +=  1 * r_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= r_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		r_odd_value = odd;
		
		
		// Process the last v chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		if(decoder->codec.num_channels == 4)
		{
			// Apply the even border filter to the lowpass band
			even += 5 * a_lowpass_ptr[column + 0];
			even += 4 * a_lowpass_ptr[column - 1];
			even -= 1 * a_lowpass_ptr[column - 2];
			even += ROUNDING(even,8);
			even = DivideByShift(even, 3);

			// Add the highpass correction
			even += a_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			//even >>= descale_shift;

			// Save the v chroma result for later output in the correct order
			a_even_value = even; //12-bit
			
			a_even_value -= alphacompandDCoffset;
			a_even_value <<= 3; //15-bit
			a_even_value *= alphacompandGain;
			a_even_value >>= 16; //12-bit
			a_even_value >>= 4; //8-bit
			if (a_even_value < 0) a_even_value = 0; else if (a_even_value > 255) a_even_value = 255;

			// Apply the odd reconstruction filter to the lowpass band
			odd += 11 * a_lowpass_ptr[column + 0];
			odd -=  4 * a_lowpass_ptr[column - 1];
			odd +=  1 * a_lowpass_ptr[column - 2];
			odd += ROUNDING(odd,8);
			odd = DivideByShift(odd, 3);

			// Subtract the highpass correction
			odd -= a_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			//odd >>= descale_shift;

			// Save the v chroma result for later output in the correct order
			a_odd_value = odd; //12-bit
					
			a_odd_value -= alphacompandDCoffset;
			a_odd_value <<= 3; //15-bit
			a_odd_value *= alphacompandGain;
			a_odd_value >>= 16; //12-bit
			a_odd_value >>= 4; //8-bit
			if (a_odd_value < 0) a_odd_value = 0; else if (a_odd_value > 255) a_odd_value = 255;
		}
		else
		{
			a_even_value = a_odd_value = 255;
		}
	
		{
			int ye,yo,ue,uo,ve,vo;
			int re = r_even_value;
			int ro = r_odd_value;
			int ge = g_even_value;
			int go = g_odd_value;
			int be = b_even_value;
			int bo = b_odd_value;
			int ae = a_even_value;
			int ao = a_odd_value;

			switch(decoder->frame.format)
			{
			case DECODED_FORMAT_R408: //AYUV
			case DECODED_FORMAT_V408: //UYVA
				ye = ((int)((fy_rmult * (float)re + fy_gmult * (float)ge + fy_bmult * (float)be)) >> (descale_shift + 6)) + 16;
				yo = ((int)((fy_rmult * (float)ro + fy_gmult * (float)go + fy_bmult * (float)bo)) >> (descale_shift + 6)) + 16;
				ue = ((int)((fu_rmult * (float)re + fu_gmult * (float)ge + fu_bmult * (float)be)) >> (descale_shift + 6)) + 128;
				uo = ((int)((fu_rmult * (float)ro + fu_gmult * (float)go + fu_bmult * (float)bo)) >> (descale_shift + 6)) + 128;
				ve = ((int)((fv_rmult * (float)re + fv_gmult * (float)ge + fv_bmult * (float)be)) >> (descale_shift + 6)) + 128;
				vo = ((int)((fv_rmult * (float)ro + fv_gmult * (float)go + fv_bmult * (float)bo)) >> (descale_shift + 6)) + 128;
			
				if(decoder->frame.format == DECODED_FORMAT_R408)//AYUV
				{
					*(colptr++) = SATURATE_8U(ae);
					*(colptr++) = SATURATE_8U(ye);
					*(colptr++) = SATURATE_8U(ue);
					*(colptr++) = SATURATE_8U(ve);

					*(colptr++) = SATURATE_8U(ao);
					*(colptr++) = SATURATE_8U(yo);
					*(colptr++) = SATURATE_8U(uo);
					*(colptr++) = SATURATE_8U(vo);
				}
				else	//UYVA
				{
					*(colptr++) = SATURATE_8U(ue);
					*(colptr++) = SATURATE_8U(ye);
					*(colptr++) = SATURATE_8U(ve);
					*(colptr++) = SATURATE_8U(ae);

					*(colptr++) = SATURATE_8U(vo);
					*(colptr++) = SATURATE_8U(yo);
					*(colptr++) = SATURATE_8U(uo);
					*(colptr++) = SATURATE_8U(ao);
				}
				break;
			}
		}


		// Advance to the next row of coefficients in each channel
		g_lowpass_ptr += lowpass_pitch[0];
		b_lowpass_ptr += lowpass_pitch[1];
		r_lowpass_ptr += lowpass_pitch[2];
		g_highpass_ptr += highpass_pitch[0];
		b_highpass_ptr += highpass_pitch[1];
		r_highpass_ptr += highpass_pitch[2];
		if(decoder->codec.num_channels == 4)
		{
			a_lowpass_ptr += lowpass_pitch[3];
			a_highpass_ptr += highpass_pitch[3];
		}

		// Advance the output pointer to the next row
		output += output_pitch;
	}
}


// Used in RT YR16 playback
// Apply the inverse horizontal transform to reconstruct a strip of rows into packed YUV pixels
void InvertHorizontalStrip16sRGB2YR16(HorizontalFilterParams)
{
	int num_channels = CODEC_NUM_CHANNELS;
	int height = roi.height;
	int width = roi.width;

	// Note that the u and v chroma values are swapped
	PIXEL *gg_lowpass_ptr = lowpass_band[0];
	PIXEL *rg_lowpass_ptr = lowpass_band[1];
	PIXEL *bg_lowpass_ptr = lowpass_band[2];
	PIXEL *gg_highpass_ptr = highpass_band[0];
	PIXEL *rg_highpass_ptr = highpass_band[1];
	PIXEL *bg_highpass_ptr = highpass_band[2];

	uint8_t *output = output_image;

	// Process 8 luma coefficients per loop iteration
	const int column_step = 8;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int channel;
	int row;

	// Compute the amount of scaling required to reduce the output precision
	//int descale_shift = (precision - 8);

	int shift = 8;
	float scale;

	float fy_rmult,fy_gmult,fy_bmult,fy_offset;
	float fu_rmult,fu_gmult,fu_bmult,fu_offset;
	float fv_rmult,fv_gmult,fv_bmult,fv_offset;

	int color_space = decoder->frame.colorspace;

	const float rgb2yuv709[3][4] =
	{
        {0.183f, 0.614f, 0.062f, 16.0f/255.0f},
        {-0.101f,-0.338f, 0.439f, 128.0f/255.0f},
        {0.439f,-0.399f,-0.040f, 128.0f/255.0f}
	};
	const float rgb2yuv601[3][4] =
	{
        {0.257f, 0.504f, 0.098f, 16.0f/255.0f},
        {-0.148f,-0.291f, 0.439f, 128.0f/255.0f},
        {0.439f,-0.368f,-0.071f, 128.0f/255.0f}
	};
	const float rgb2yuvVS601[3][4] =
	{
        {0.299f,0.587f,0.114f,0},
        {-0.172f,-0.339f,0.511f,128.0f/255.0f},
        {0.511f,-0.428f,-0.083f,128.0f/255.0f}
	};
	const float rgb2yuvVS709[3][4] =
	{
        {0.213f,0.715f,0.072f,0},
        {-0.117f,-0.394f,0.511f,128.0f/255.0f},
        {0.511f,-0.464f,-0.047f,128.0f/255.0f}
	};
	float rgb2yuv[3][4];
	//int yoffset = 16;

	switch(color_space & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:
		memcpy(rgb2yuv, rgb2yuv601, 12*sizeof(float));
		break;
	default: assert(0);
	case COLOR_SPACE_CG_709:
		memcpy(rgb2yuv, rgb2yuv709, 12*sizeof(float));
		break;
	case COLOR_SPACE_VS_601:
		memcpy(rgb2yuv, rgb2yuvVS601, 12*sizeof(float));
		break;
	case COLOR_SPACE_VS_709:
		memcpy(rgb2yuv, rgb2yuvVS709, 12*sizeof(float));
		break;
	}

	scale = 4.0;

	fy_rmult = ((rgb2yuv[0][0]) * scale);
	fy_gmult = ((rgb2yuv[0][1]) * scale);
	fy_bmult = ((rgb2yuv[0][2]) * scale);
	fy_offset= ((rgb2yuv[0][3]) * 16384.0f);

	fu_rmult = ((rgb2yuv[1][0]) * scale);
	fu_gmult = ((rgb2yuv[1][1]) * scale);
	fu_bmult = ((rgb2yuv[1][2]) * scale);
	fu_offset= ((rgb2yuv[1][3]) * 16384.0f);

	fv_rmult = ((rgb2yuv[2][0]) * scale);
	fv_gmult = ((rgb2yuv[2][1]) * scale);
	fv_bmult = ((rgb2yuv[2][2]) * scale);
	fv_offset= ((rgb2yuv[2][3]) * 16384.0f);

	shift-=2;


	// Convert the pitch to units of pixels
	for (channel = 0; channel < num_channels; channel++)
	{
		lowpass_pitch[channel] /= sizeof(PIXEL);
		highpass_pitch[channel] /= sizeof(PIXEL);
	}

	output_pitch /= sizeof(uint8_t);

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
		__m128i gg_low1_epi16;		// Lowpass coefficients
		__m128i gg_low2_epi16;
		__m128i bg_low1_epi16;
		__m128i bg_low2_epi16;
		__m128i rg_low1_epi16;
		__m128i rg_low2_epi16;

		__m128i gg_high1_epi16;		// Highpass coefficients
		__m128i gg_high2_epi16;
		__m128i bg_high1_epi16;
		__m128i bg_high2_epi16;
		__m128i rg_high1_epi16;
		__m128i rg_high2_epi16;

		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr = (__m128i *)&output[0];
		__m128i *Yptr128 = (__m128i *)&output[0];
		__m128i *Vptr128 = (__m128i *)&output[width*4];
		__m128i *Uptr128 = (__m128i *)&output[width*6];

		const __m128i mask_epi32 = _mm_set1_epi32(0xffff);

		__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x0fff);

#endif
		uint8_t *colptr = (uint8_t *)&output[0];
		PIXEL16U *Yptr = (PIXEL16U *)&output[0];
		PIXEL16U *Vptr = Yptr + width*2;
		PIXEL16U *Uptr = Vptr + (width);
		int32_t lastU0=0,lastV0=0;

		int32_t gg_even_value;
		int32_t bg_even_value;
		int32_t rg_even_value;
		int32_t gg_odd_value;
		int32_t bg_odd_value;
		int32_t rg_odd_value;


		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		int32_t even = 0;
		int32_t odd = 0;


		// Apply the even reconstruction filter to the lowpass band
		even += 11 * gg_lowpass_ptr[column + 0];
		even -=  4 * gg_lowpass_ptr[column + 1];
		even +=  1 * gg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += gg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
	//	even >>= descale_shift;

		// Save the value for use in the fast loop
		gg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * gg_lowpass_ptr[column + 0];
		odd += 4 * gg_lowpass_ptr[column + 1];
		odd -= 1 * gg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= gg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
	//	odd >>= descale_shift;

		// Save the value for use in the fast loop
		gg_odd_value = odd;


		// Process the first two u chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * bg_lowpass_ptr[column + 0];
		even -=  4 * bg_lowpass_ptr[column + 1];
		even +=  1 * bg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += bg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
	//	even >>= descale_shift;

		// Save the value for use in the fast loop
		bg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * bg_lowpass_ptr[column + 0];
		odd += 4 * bg_lowpass_ptr[column + 1];
		odd -= 1 * bg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= bg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
	//	odd >>= descale_shift;

		// Save the value for use in the fast loop
		bg_odd_value = odd;


		// Process the first two v chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * rg_lowpass_ptr[column + 0];
		even -=  4 * rg_lowpass_ptr[column + 1];
		even +=  1 * rg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += rg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
	//	even >>= descale_shift;

		// Save the value for use in the fast loop
		rg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * rg_lowpass_ptr[column + 0];
		odd += 4 * rg_lowpass_ptr[column + 1];
		odd -= 1 * rg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= rg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
	//	odd >>= descale_shift;

		// Save the value for use in the fast loop
		rg_odd_value = odd;

#if (1 && XMMOPT)

		// Preload the first eight lowpass luma coefficients
		gg_low1_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[0]);

		// Preload the first eight highpass luma coefficients
		gg_high1_epi16 = _mm_load_si128((__m128i *)&gg_highpass_ptr[0]);

		// Preload the first eight lowpass u chroma coefficients
		bg_low1_epi16 = _mm_load_si128((__m128i *)&bg_lowpass_ptr[0]);

		// Preload the first eight highpass u chroma coefficients
		bg_high1_epi16 = _mm_load_si128((__m128i *)&bg_highpass_ptr[0]);

		// Preload the first eight lowpass v chroma coefficients
		rg_low1_epi16 = _mm_load_si128((__m128i *)&rg_lowpass_ptr[0]);

		// Preload the first eight highpass v chroma coefficients
 		rg_high1_epi16 = _mm_load_si128((__m128i *)&rg_highpass_ptr[0]);


		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i g1_output_epi16;
			__m128i g2_output_epi16;
			__m128i r1_output_epi16;
			__m128i r2_output_epi16;
			__m128i b1_output_epi16;
			__m128i b2_output_epi16;

			__m128i gg1_output_epi16;
			__m128i gg2_output_epi16;
			__m128i rg1_output_epi16;
			__m128i rg2_output_epi16;
			__m128i bg1_output_epi16;
			__m128i bg2_output_epi16;

			__m128i y1_output_epi16;
			__m128i y2_output_epi16;
			__m128i u1_output_epi16;
			__m128i u2_output_epi16;
			__m128i v1_output_epi16;
			__m128i v2_output_epi16;

			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i temp_epi32;
			__m128i tempB_epi32;
			__m128i rgb_epi32;
			__m128i zero_epi128;
			__m128  temp_ps;
			__m128  rgb_ps;
			__m128	y1a_ps;
			__m128	y1b_ps;
			__m128	u1a_ps;
			__m128	u1b_ps;
			__m128	v1a_ps;
			__m128	v1b_ps;


			__m128i out_epi16;		// Reconstructed data
			////__m128i mask_epi16;
			////__m128i lsb_epi16;
			////__m128i sign_epi16;
			//__m128i high_epi16;

			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;

			//DAN031304 -- correct inverse filter
			__m128i half_epi16 = _mm_set1_epi16(4);
			__m128i offset_epi16 = _mm_set1_epi16(2048);

			uint32_t temp;		// Temporary register for last two values


			/***** Compute the first eight luma output values *****/

			// Preload the second eight lowpass coefficients
			gg_low2_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			gg_high2_epi16 = _mm_load_si128((__m128i *)&gg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = gg_low1_epi16;
			high1_epi16 = gg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);


			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_odd_value, 1);

			// Save the eight luma values for packing later
			gg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			gg_even_value = (short)temp;
			gg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight luma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = gg_low2_epi16;
			high2_epi16 = gg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_odd_value, 1);

			// Save the eight luma values for packing later
			gg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			gg_even_value = (short)temp;
			gg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are used later in the loop
			gg_low1_epi16 = gg_low2_epi16;

			// The second eight highpass coefficients are used later in the loop
			gg_high1_epi16 = gg_high2_epi16;


			/***** Compute the first eight u chroma output values *****/

			// Preload the second eight lowpass coefficients
			bg_low2_epi16 = _mm_load_si128((__m128i *)&bg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			bg_high2_epi16 = _mm_load_si128((__m128i *)&bg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = bg_low1_epi16;
			high1_epi16 = bg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			bg_even_value = (short)temp;
			bg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight u chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = bg_low2_epi16;
			high2_epi16 = bg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			bg_even_value = (short)temp;
			bg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			bg_low1_epi16 = bg_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			bg_high1_epi16 = bg_high2_epi16;



			/***** Compute the first eight v chroma output values *****/

			// Preload the second eight lowpass coefficients
			rg_low2_epi16 = _mm_load_si128((__m128i *)&rg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			rg_high2_epi16 = _mm_load_si128((__m128i *)&rg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = rg_low1_epi16;
			high1_epi16 = rg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			rg_even_value = (short)temp;
			rg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight v chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = rg_low2_epi16;
			high2_epi16 = rg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			rg_even_value = (short)temp;
			rg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			rg_low1_epi16 = rg_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			rg_high1_epi16 = rg_high2_epi16;












			//r_output_epi16  = ((rg_output_epi16 - 32768)<<1)+gg_output_epi16
			//r_output_epi16  = ((rg_output_epi16>>3 - 32768>>3))+gg_output_epi16>>4


			g1_output_epi16 = gg1_output_epi16;

			r1_output_epi16 = rg1_output_epi16;//_mm_srli_epi16(rg1_output_epi16,2);
		//	r1_output_epi16 = _mm_subs_epi16(r1_output_epi16, value128_epi32);
		//	r1_output_epi16 = _mm_slli_epi16(r1_output_epi16,1);
		//	r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, g1_output_epi16);

			b1_output_epi16 = bg1_output_epi16;//_mm_srli_epi16(bg1_output_epi16,2);
		//	b1_output_epi16 = _mm_subs_epi16(b1_output_epi16, value128_epi32);
		//	b1_output_epi16 = _mm_slli_epi16(b1_output_epi16,1);
		//	b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, g1_output_epi16);


			 r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, limiterRGB);
			 r1_output_epi16 = _mm_subs_epu16(r1_output_epi16, limiterRGB);

			 g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, limiterRGB);
			 g1_output_epi16 = _mm_subs_epu16(g1_output_epi16, limiterRGB);

			 b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, limiterRGB);
			 b1_output_epi16 = _mm_subs_epu16(b1_output_epi16, limiterRGB);


			zero_epi128 = _mm_setzero_si128();


			// Compute Y,U,V
			rgb_epi32 = _mm_unpacklo_epi16(r1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1a_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1a_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1a_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);
			rgb_epi32 = _mm_unpackhi_epi16(r1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1b_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1b_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1b_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);

			rgb_epi32 = _mm_unpacklo_epi16(g1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(g1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			rgb_epi32 = _mm_unpacklo_epi16(b1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(b1_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_ps = _mm_set_ps1(fy_offset);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fu_offset);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fv_offset);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_epi32 = _mm_cvtps_epi32(y1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(y1b_ps);
			y1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);

			y1_output_epi16 = _mm_slli_epi16(y1_output_epi16, 2);
			_mm_store_si128(Yptr128++, y1_output_epi16);
	//		y1_output_epi16 = _mm_adds_epi16(y1_output_epi16, limiter);
	//		y1_output_epi16 = _mm_subs_epu16(y1_output_epi16, limiter);
	//		y1_output_epi16 = _mm_srli_epi16(y1_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(u1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(u1b_ps);
			u1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
	//		u1_output_epi16 = _mm_adds_epi16(u1_output_epi16, limiter);
	//		u1_output_epi16 = _mm_subs_epu16(u1_output_epi16, limiter);
	//		u1_output_epi16 = _mm_srli_epi16(u1_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(v1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(v1b_ps);
			v1_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
	//		v1_output_epi16 = _mm_adds_epi16(v1_output_epi16, limiter);
	//		v1_output_epi16 = _mm_subs_epu16(v1_output_epi16, limiter);
	//		v1_output_epi16 = _mm_srli_epi16(v1_output_epi16, shift);






			g2_output_epi16 = gg2_output_epi16;//_mm_srli_epi16(gg2_output_epi16,2);

			r2_output_epi16 = rg2_output_epi16;//_mm_srli_epi16(rg2_output_epi16,2);
		//	r2_output_epi16 = _mm_subs_epi16(r2_output_epi16, value128_epi32);
		//	r2_output_epi16 = _mm_slli_epi16(r2_output_epi16,1);
		//	r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, g2_output_epi16);

			b2_output_epi16 = bg2_output_epi16;//_mm_srli_epi16(bg2_output_epi16,2);
		//	b2_output_epi16 = _mm_subs_epi16(b2_output_epi16, value128_epi32);
		//	b2_output_epi16 = _mm_slli_epi16(b2_output_epi16,1);
		//	b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, g2_output_epi16);



			 r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, limiterRGB);
			 r2_output_epi16 = _mm_subs_epu16(r2_output_epi16, limiterRGB);

			 g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, limiterRGB);
			 g2_output_epi16 = _mm_subs_epu16(g2_output_epi16, limiterRGB);

			 b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, limiterRGB);
			 b2_output_epi16 = _mm_subs_epu16(b2_output_epi16, limiterRGB);


			// Compute Y,U,V
			rgb_epi32 = _mm_unpacklo_epi16(r2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1a_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1a_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1a_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);
			rgb_epi32 = _mm_unpackhi_epi16(r2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			y1b_ps = _mm_mul_ps(_mm_set_ps1(fy_rmult), rgb_ps);
			u1b_ps = _mm_mul_ps(_mm_set_ps1(fu_rmult), rgb_ps);
			v1b_ps = _mm_mul_ps(_mm_set_ps1(fv_rmult), rgb_ps);

			rgb_epi32 = _mm_unpacklo_epi16(g2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(g2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_gmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_gmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_gmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			rgb_epi32 = _mm_unpacklo_epi16(b2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			rgb_epi32 = _mm_unpackhi_epi16(b2_output_epi16, zero_epi128);
			rgb_ps = _mm_cvtepi32_ps(rgb_epi32);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fy_bmult), rgb_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fu_bmult), rgb_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_mul_ps(_mm_set_ps1(fv_bmult), rgb_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_ps = _mm_set_ps1(fy_offset);
			y1a_ps = _mm_add_ps(y1a_ps, temp_ps);
			y1b_ps = _mm_add_ps(y1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fu_offset);
			u1a_ps = _mm_add_ps(u1a_ps, temp_ps);
			u1b_ps = _mm_add_ps(u1b_ps, temp_ps);
			temp_ps = _mm_set_ps1(fv_offset);
			v1a_ps = _mm_add_ps(v1a_ps, temp_ps);
			v1b_ps = _mm_add_ps(v1b_ps, temp_ps);

			temp_epi32 = _mm_cvtps_epi32(y1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(y1b_ps);
			y2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);

			y2_output_epi16 = _mm_slli_epi16(y2_output_epi16, 2);
			_mm_store_si128(Yptr128++, y2_output_epi16);
	//		y2_output_epi16 = _mm_adds_epi16(y2_output_epi16, limiter);
	//		y2_output_epi16 = _mm_subs_epu16(y2_output_epi16, limiter);
	//		y2_output_epi16 = _mm_srli_epi16(y2_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(u1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(u1b_ps);
			u2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
	//		u2_output_epi16 = _mm_adds_epi16(u2_output_epi16, limiter);
	//		u2_output_epi16 = _mm_subs_epu16(u2_output_epi16, limiter);
	//		u2_output_epi16 = _mm_srli_epi16(u2_output_epi16, shift);

			temp_epi32 = _mm_cvtps_epi32(v1a_ps);
			tempB_epi32 = _mm_cvtps_epi32(v1b_ps);
			v2_output_epi16 = _mm_packs_epi32(temp_epi32, tempB_epi32);
	//		v2_output_epi16 = _mm_adds_epi16(v2_output_epi16, limiter);
	//		v2_output_epi16 = _mm_subs_epu16(v2_output_epi16, limiter);
	//		v2_output_epi16 = _mm_srli_epi16(v2_output_epi16, shift);


			// 4:4:4 to 4:2:2 // U = (U1+U2)/2
#if 0
			temp_epi16 = _mm_srli_si128(u1_output_epi16, 2);
			u1_output_epi16 = _mm_adds_epi16(u1_output_epi16, temp_epi16);
			u1_output_epi16 = _mm_srli_epi16(u1_output_epi16, 1);
			temp_epi16 = _mm_srli_si128(u2_output_epi16, 2);
			u2_output_epi16 = _mm_adds_epi16(u2_output_epi16, temp_epi16);
			u2_output_epi16 = _mm_srli_epi16(u2_output_epi16, 1);
			temp_epi16 = _mm_srli_si128(v1_output_epi16, 2);
			v1_output_epi16 = _mm_adds_epi16(v1_output_epi16, temp_epi16);
			v1_output_epi16 = _mm_srli_epi16(v1_output_epi16, 1);
			temp_epi16 = _mm_srli_si128(v2_output_epi16, 2);
			v2_output_epi16 = _mm_adds_epi16(v2_output_epi16, temp_epi16);
			v2_output_epi16 = _mm_srli_epi16(v2_output_epi16, 1);
			u1_output_epi16 = _mm_and_si128(u1_output_epi16, mask_epi32);
			u2_output_epi16 = _mm_and_si128(u2_output_epi16, mask_epi32);
			v1_output_epi16 = _mm_and_si128(v1_output_epi16, mask_epi32);
			v2_output_epi16 = _mm_and_si128(v2_output_epi16, mask_epi32);
#else
			// 4:4:4 to 4:2:2 // U = (U1+2.U2+U3)/4 (correct centre weighting)
			{
				__m128i double1_epi16;
				__m128i double2_epi16;
				__m128i left1_epi16;
				__m128i left2_epi16;
				__m128i right1_epi16;
				__m128i right2_epi16;
				/*int i;
				for(i=0;i<16;i++)
                    gg_lowpass_ptr[i] = i+1;
				u1_output_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[0]);
				u2_output_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[8]);
				v1_output_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[0]);
				v2_output_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[8]);*/

				if(column == 0)
				{
					lastU0 = _mm_extract_epi16(u1_output_epi16, 0);
					lastV0 = _mm_extract_epi16(v1_output_epi16, 0);
				}

				double1_epi16 = _mm_adds_epu16(u1_output_epi16, u1_output_epi16);
				double2_epi16 = _mm_adds_epu16(u2_output_epi16, u2_output_epi16);
				left1_epi16 = _mm_slli_si128(u1_output_epi16, 2);
				left2_epi16 = _mm_slli_si128(u2_output_epi16, 2);
				left1_epi16 = _mm_insert_epi16(left1_epi16, lastU0, 0);
				left2_epi16 = _mm_insert_epi16(left2_epi16, _mm_extract_epi16(u1_output_epi16, 7), 0);
				right1_epi16 = _mm_srli_si128(u1_output_epi16, 2);
				right2_epi16 = _mm_srli_si128(u2_output_epi16, 2);
				lastU0 = _mm_extract_epi16(u2_output_epi16, 7);

				u1_output_epi16 = _mm_adds_epu16(double1_epi16, left1_epi16);
				u1_output_epi16 = _mm_adds_epu16(u1_output_epi16, right1_epi16);
				u1_output_epi16 = _mm_srli_epi16(u1_output_epi16, 2);
				u2_output_epi16 = _mm_adds_epu16(double2_epi16, left2_epi16);
				u2_output_epi16 = _mm_adds_epu16(u2_output_epi16, right2_epi16);
				u2_output_epi16 = _mm_srli_epi16(u2_output_epi16, 2);

				u1_output_epi16 = _mm_and_si128(u1_output_epi16, mask_epi32);
				u2_output_epi16 = _mm_and_si128(u2_output_epi16, mask_epi32);

				double1_epi16 = _mm_adds_epu16(v1_output_epi16, v1_output_epi16);
				double2_epi16 = _mm_adds_epu16(v2_output_epi16, v2_output_epi16);
				left1_epi16 = _mm_slli_si128(v1_output_epi16, 2);
				left2_epi16 = _mm_slli_si128(v2_output_epi16, 2);
				left1_epi16 = _mm_insert_epi16(left1_epi16, lastV0, 0);
				left2_epi16 = _mm_insert_epi16(left2_epi16, _mm_extract_epi16(v1_output_epi16, 7), 0);
				right1_epi16 = _mm_srli_si128(v1_output_epi16, 2);
				right2_epi16 = _mm_srli_si128(v2_output_epi16, 2);
				lastV0 = _mm_extract_epi16(v2_output_epi16, 7);

				v1_output_epi16 = _mm_adds_epu16(double1_epi16, left1_epi16);
				v1_output_epi16 = _mm_adds_epu16(v1_output_epi16, right1_epi16);
				v1_output_epi16 = _mm_srli_epi16(v1_output_epi16, 2);
				v2_output_epi16 = _mm_adds_epu16(double2_epi16, left2_epi16);
				v2_output_epi16 = _mm_adds_epu16(v2_output_epi16, right2_epi16);
				v2_output_epi16 = _mm_srli_epi16(v2_output_epi16, 2);

				v1_output_epi16 = _mm_and_si128(v1_output_epi16, mask_epi32);
				v2_output_epi16 = _mm_and_si128(v2_output_epi16, mask_epi32);



			}
#endif
			u1_output_epi16 = _mm_packs_epi32 (u1_output_epi16, u2_output_epi16);
			v1_output_epi16 = _mm_packs_epi32 (v1_output_epi16, v2_output_epi16);


			u1_output_epi16 = _mm_slli_epi16(u1_output_epi16, 2);
			v1_output_epi16 = _mm_slli_epi16(v1_output_epi16, 2);

			_mm_store_si128(Vptr128++, v1_output_epi16);
			_mm_store_si128(Uptr128++, u1_output_epi16);

		}

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

		colptr = (uint8_t *)outptr;

		Yptr = (PIXEL16U *)outptr;
		Vptr = Yptr + width*2;
		Uptr = Vptr + (width);

#endif

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column ++)
		{
			int re,ge,be;
			int ro,go,bo;
			int ye,yo,u,v;


			/***** First pair of luma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += gg_lowpass_ptr[column - 1];
			even -= gg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += gg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += gg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
		//	even >>= descale_shift;

			// Save the luma result for later output in the correct order
		//	y1_even_value = even;
			ge = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= gg_lowpass_ptr[column - 1];
			odd += gg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += gg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= gg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
		//	odd >>= descale_shift;

			// Save the luma result for later output in the correct order
		//	y1_odd_value = odd;
			go = odd;


			/***** Pair of u chroma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += bg_lowpass_ptr[column - 1];
			even -= bg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += bg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += bg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
		//	even >>= descale_shift;

			// Save the u chroma result for later output in the correct order
		//	bg_even_value = even;
			be = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= bg_lowpass_ptr[column - 1];
			odd += bg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += bg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= bg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
		//	odd >>= descale_shift;

			// Save the u chroma result for later output in the correct order
		//	bg_odd_value = odd;
			bo = odd;



			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += rg_lowpass_ptr[column - 1];
			even -= rg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += rg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += rg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
		//	even >>= descale_shift;

			// Save the v chroma result for later output in the correct order
		//	rg_even_value = even;
			re = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= rg_lowpass_ptr[column - 1];
			odd += rg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += rg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= rg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
		//	odd >>= descale_shift;

			// Save the v chroma result for later output in the correct order
		//	rg_odd_value = odd;
			ro = odd;


		// We use 16-bit fixed-point arithmetic to approximate the color conversion coefficients
		//
		// Floating point arithmetic is
		//
			ye = ((int)((fy_rmult * (float)re + fy_gmult * (float)ge + fy_bmult * (float)be))) + (int)fy_offset;
			yo = ((int)((fy_rmult * (float)ro + fy_gmult * (float)go + fy_bmult * (float)bo))) + (int)fy_offset;



#if 1	// 4:4:4 to 4:2:2 // U = (U1+U2)/2
			u  = ((int)((fu_rmult * (float)(re+ro) + fu_gmult * (float)(ge+go) + fu_bmult * (float)(be+bo))) >> (1)) + (int)fu_offset;
			v  = ((int)((fv_rmult * (float)(re+ro) + fv_gmult * (float)(ge+go) + fv_bmult * (float)(be+bo))) >> (1)) + (int)fv_offset;
#else	// 4:4:4 to 4:2:2 // U = (U1+2.U2+U3)/4 (correct centre weighting)
			//TODO non-SSE2 version of this down sample
#endif
			// Output the luma and chroma values in the correct order
			*(Yptr++) = SATURATE_16U(ye<<2);
			*(Yptr++) = SATURATE_16U(yo<<2);
			*(Vptr++) = SATURATE_16U(v<<2);
			*(Uptr++) = SATURATE_16U(u<<2);
		}




		// Redo the last two RGB444 pixels.
		column = last_column - 1;
		Yptr -= 2; //two pixels
		Uptr--;
		Vptr--;

		// Process the last luma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * gg_lowpass_ptr[column + 0];
		even += 4 * gg_lowpass_ptr[column - 1];
		even -= 1 * gg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += gg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the luma result for later output in the correct order
		gg_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * gg_lowpass_ptr[column + 0];
		odd -=  4 * gg_lowpass_ptr[column - 1];
		odd +=  1 * gg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= gg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the luma result for later output in the correct order
		gg_odd_value = odd;

		// Process the last u chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * bg_lowpass_ptr[column + 0];
		even += 4 * bg_lowpass_ptr[column - 1];
		even -= 1 * bg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += bg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		bg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * bg_lowpass_ptr[column + 0];
		odd -=  4 * bg_lowpass_ptr[column - 1];
		odd +=  1 * bg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= bg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		bg_odd_value = odd;

		// Process the last v chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * rg_lowpass_ptr[column + 0];
		even += 4 * rg_lowpass_ptr[column - 1];
		even -= 1 * rg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += rg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		rg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * rg_lowpass_ptr[column + 0];
		odd -=  4 * rg_lowpass_ptr[column - 1];
		odd +=  1 * rg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= rg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		rg_odd_value = odd;

		{
			int ye,yo,u,v;
			int re = rg_even_value;
			int ro = rg_odd_value;
			int ge = gg_even_value;
			int go = gg_odd_value;
			int be = bg_even_value;
			int bo = bg_odd_value;

			// We use 16-bit fixed-point arithmetic to approximate the color conversion coefficients
			//
			// Floating point arithmetic is
		//
			ye = ((int)((fy_rmult * (float)re + fy_gmult * (float)ge + fy_bmult * (float)be))) + (int)fy_offset;
			yo = ((int)((fy_rmult * (float)ro + fy_gmult * (float)go + fy_bmult * (float)bo))) + (int)fy_offset;


#if 1	// 4:4:4 to 4:2:2 // U = (U1+U2)/2
			u  = ((int)((fu_rmult * (float)(re+ro) + fu_gmult * (float)(ge+go) + fu_bmult * (float)(be+bo))) >> (1)) + (int)fu_offset;
			v  = ((int)((fv_rmult * (float)(re+ro) + fv_gmult * (float)(ge+go) + fv_bmult * (float)(be+bo))) >> (1)) + (int)fv_offset;
#else	// 4:4:4 to 4:2:2 // U = (U1+2.U2+U3)/4 (correct centre weighting)
			//TODO non-SSE2 version of this down sample
#endif
			// Output the luma and chroma values in the correct order
			*(Yptr++) = SATURATE_16U(ye<<2);
			*(Yptr++) = SATURATE_16U(yo<<2);
			*(Vptr++) = SATURATE_16U(v<<2);
			*(Uptr++) = SATURATE_16U(u<<2);
		}


		// Should have exited the loop at the column for right border processing
	//	assert(column == last_column);
		// Advance to the next row of coefficients in each channel
		gg_lowpass_ptr += lowpass_pitch[0];
		bg_lowpass_ptr += lowpass_pitch[1];
		rg_lowpass_ptr += lowpass_pitch[2];
		gg_highpass_ptr += highpass_pitch[0];
		bg_highpass_ptr += highpass_pitch[1];
		rg_highpass_ptr += highpass_pitch[2];

		// Advance the output pointer to the next row
		output += output_pitch;
	}
}

void InvertHorizontalStrip16sRGB2v210(HorizontalFilterParams)
{
	uint8_t buffer[8200*4],*bptr = buffer;
	int width = roi.width;
	//int height = roi.height;

	//align for SSE2
	bptr += 15;
	bptr = (uint8_t*)(((uintptr_t)bptr) & ~15);

	InvertHorizontalStrip16sRGB2YR16(decoder, thread_index,
			lowpass_band, lowpass_pitch,
			highpass_band, highpass_pitch,
			bptr,
			width * 2 * 4,
			roi,
			precision,
			format);

	{
		PIXEL *plane_array[3];
		int plane_pitch[3];
		ROI newroi;

		plane_array[0] = (PIXEL *)bptr;
		plane_array[1] = (PIXEL *)(bptr + width * 4);
		plane_array[2] = (PIXEL *)(bptr + width * 6);

		plane_pitch[0] = width * 4 * 2;
		plane_pitch[1] = width * 4 * 2;
		plane_pitch[2] = width * 4 * 2;

		newroi.width = width*2;
		newroi.height = 2;

		//TODO support YU64 as well, so we could YU64 this way
		ConvertYUVStripPlanarToV210(plane_array, plane_pitch, newroi, output_image,
			output_pitch, width * 2, format, decoder->frame.colorspace, 16);

	}
}

// Used in RT B64A playback
// Apply the inverse horizontal transform to reconstruct a strip of rows into packed RG30 pixels
void InvertHorizontalStrip16sRGB2B64A(HorizontalFilterParams)
{
	int num_channels = CODEC_NUM_CHANNELS;
	int height = roi.height;
	int width = roi.width;

	// Note that the u and v chroma values are swapped
	PIXEL *g_lowpass_ptr = lowpass_band[0];
	PIXEL *r_lowpass_ptr = lowpass_band[1];
	PIXEL *b_lowpass_ptr = lowpass_band[2];
	PIXEL *a_lowpass_ptr = lowpass_band[3];
	PIXEL *g_highpass_ptr = highpass_band[0];
	PIXEL *r_highpass_ptr = highpass_band[1];
	PIXEL *b_highpass_ptr = highpass_band[2];
	PIXEL *a_highpass_ptr = highpass_band[3];

	uint8_t *output = output_image;

	// Process 8 luma coefficients per loop iteration
	const int column_step = 8;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int channel;
	int row;

	// Compute the amount of scaling required to reduce the output precision
	//int descale_shift = (precision - 8);

	int shift = 8;

	float scale;

	
	decoder->frame.alpha_Companded = 1;

	num_channels = decoder->codec.num_channels;


	scale = 4.0;

	shift-=2;


	// Convert the pitch to units of pixels
	for (channel = 0; channel < num_channels; channel++)
	{
		lowpass_pitch[channel] /= sizeof(PIXEL);
		highpass_pitch[channel] /= sizeof(PIXEL);
	}

	output_pitch /= sizeof(uint8_t);

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
		__m128i g_low1_epi16;		// Lowpass coefficients
		__m128i g_low2_epi16;
		__m128i b_low1_epi16;
		__m128i b_low2_epi16;
		__m128i r_low1_epi16;
		__m128i r_low2_epi16;
		__m128i a_low1_epi16;
		__m128i a_low2_epi16;

		__m128i g_high1_epi16;		// Highpass coefficients
		__m128i g_high2_epi16;
		__m128i b_high1_epi16;
		__m128i b_high2_epi16;
		__m128i r_high1_epi16;
		__m128i r_high2_epi16;
		__m128i a_high1_epi16;
		__m128i a_high2_epi16;

		// The fast loop merges values from different phases to allow aligned stores
		//__m128i *outptr = (__m128i *)&output[0];
		__m128i *B64Aptr128 = (__m128i *)&output[0];

		const __m128i a_epi16 = _mm_set1_epi16(0xfff);

		__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x0fff);

#endif
		PIXEL16U *colptr = (PIXEL16U *)&output[0];
		//uint32_t *RG30ptr = (uint32_t *)&output[0];

		int32_t g_even_value;
		int32_t b_even_value;
		int32_t r_even_value;
		int32_t a_even_value;
		int32_t g_odd_value;
		int32_t b_odd_value;
		int32_t r_odd_value;
		int32_t a_odd_value;


		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		int32_t even = 0;
		int32_t odd = 0;


		// Apply the even reconstruction filter to the lowpass band
		even += 11 * g_lowpass_ptr[column + 0];
		even -=  4 * g_lowpass_ptr[column + 1];
		even +=  1 * g_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += g_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
	//	even >>= descale_shift;

		// Save the value for use in the fast loop
		g_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * g_lowpass_ptr[column + 0];
		odd += 4 * g_lowpass_ptr[column + 1];
		odd -= 1 * g_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= g_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
	//	odd >>= descale_shift;

		// Save the value for use in the fast loop
		g_odd_value = odd;


		// Process the first two u chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * b_lowpass_ptr[column + 0];
		even -=  4 * b_lowpass_ptr[column + 1];
		even +=  1 * b_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += b_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
	//	even >>= descale_shift;

		// Save the value for use in the fast loop
		b_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * b_lowpass_ptr[column + 0];
		odd += 4 * b_lowpass_ptr[column + 1];
		odd -= 1 * b_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= b_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
	//	odd >>= descale_shift;

		// Save the value for use in the fast loop
		b_odd_value = odd;


		// Process the first two v chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * r_lowpass_ptr[column + 0];
		even -=  4 * r_lowpass_ptr[column + 1];
		even +=  1 * r_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += r_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
	//	even >>= descale_shift;

		// Save the value for use in the fast loop
		r_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * r_lowpass_ptr[column + 0];
		odd += 4 * r_lowpass_ptr[column + 1];
		odd -= 1 * r_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= r_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the value for use in the fast loop
		r_odd_value = odd;


		if(num_channels == 4)
		{
			// Process the first two v chroma output points with special filters for the left border
			even = 0;
			odd = 0;

			// Apply the even reconstruction filter to the lowpass band
			even += 11 * a_lowpass_ptr[column + 0];
			even -=  4 * a_lowpass_ptr[column + 1];
			even +=  1 * a_lowpass_ptr[column + 2];
			even += ROUNDING(even,8);
			even = DivideByShift(even, 3);

			// Add the highpass correction
			even += a_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			//even >>= descale_shift;

			// Save the value for use in the fast loop
			a_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd += 5 * a_lowpass_ptr[column + 0];
			odd += 4 * a_lowpass_ptr[column + 1];
			odd -= 1 * a_lowpass_ptr[column + 2];
			odd += ROUNDING(odd,8);
			odd = DivideByShift(odd, 3);

			// Subtract the highpass correction
			odd -= a_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
		//	odd >>= descale_shift;

			// Save the value for use in the fast l}oop
			a_odd_value = odd;
		}

#if (1 && XMMOPT)

		// Preload the first eight lowpass luma coefficients
		g_low1_epi16 = _mm_load_si128((__m128i *)&g_lowpass_ptr[0]);

		// Preload the first eight highpass luma coefficients
		g_high1_epi16 = _mm_load_si128((__m128i *)&g_highpass_ptr[0]);

		// Preload the first eight lowpass u chroma coefficients
		b_low1_epi16 = _mm_load_si128((__m128i *)&b_lowpass_ptr[0]);

		// Preload the first eight highpass u chroma coefficients
		b_high1_epi16 = _mm_load_si128((__m128i *)&b_highpass_ptr[0]);

		// Preload the first eight lowpass v chroma coefficients
		r_low1_epi16 = _mm_load_si128((__m128i *)&r_lowpass_ptr[0]);

		// Preload the first eight highpass v chroma coefficients
 		r_high1_epi16 = _mm_load_si128((__m128i *)&r_highpass_ptr[0]);

		if(num_channels == 4)
		{
			// Preload the first eight lowpass v chroma coefficients
			a_low1_epi16 = _mm_load_si128((__m128i *)&a_lowpass_ptr[0]);

			// Preload the first eight highpass v chroma coefficients
 			a_high1_epi16 = _mm_load_si128((__m128i *)&a_highpass_ptr[0]);
		}


		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i g1_output_epi16;
			__m128i g2_output_epi16;
			__m128i r1_output_epi16;
			__m128i r2_output_epi16;
			__m128i b1_output_epi16;
			__m128i b2_output_epi16;
			__m128i a1_output_epi16;
			__m128i a2_output_epi16;

			__m128i gg1_output_epi16;
			__m128i gg2_output_epi16;
			__m128i rg1_output_epi16;
			__m128i rg2_output_epi16;
			__m128i bg1_output_epi16;
			__m128i bg2_output_epi16;
			__m128i ag1_output_epi16;
			__m128i ag2_output_epi16;

			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i zero_epi128;

			//__m128i rr_epi32;
			//__m128i g_epi32;
			//__m128i bb_epi32;

			__m128i out_epi16;		// Reconstructed data
			////__m128i mask_epi16;
			////__m128i lsb_epi16;
			////__m128i sign_epi16;
			//__m128i high_epi16;

			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;

			//DAN031304 -- correct inverse filter
			__m128i half_epi16 = _mm_set1_epi16(4);
			__m128i offset_epi16 = _mm_set1_epi16(2048);

			uint32_t temp;		// Temporary register for last two values

			/***** Compute the first eight luma output values *****/

			// Preload the second eight lowpass coefficients
			g_low2_epi16 = _mm_load_si128((__m128i *)&g_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			g_high2_epi16 = _mm_load_si128((__m128i *)&g_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = g_low1_epi16;
			high1_epi16 = g_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);


			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, g_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, g_odd_value, 1);

			// Save the eight luma values for packing later
			gg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			g_even_value = (short)temp;
			g_odd_value = (short)(temp >> 16);


			/***** Compute the second eight luma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = g_low2_epi16;
			high2_epi16 = g_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, g_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, g_odd_value, 1);

			// Save the eight luma values for packing later
			gg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			g_even_value = (short)temp;
			g_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are used later in the loop
			g_low1_epi16 = g_low2_epi16;

			// The second eight highpass coefficients are used later in the loop
			g_high1_epi16 = g_high2_epi16;




			/***** Compute the first eight u chroma output values *****/

			// Preload the second eight lowpass coefficients
			b_low2_epi16 = _mm_load_si128((__m128i *)&b_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			b_high2_epi16 = _mm_load_si128((__m128i *)&b_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = b_low1_epi16;
			high1_epi16 = b_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, b_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, b_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			b_even_value = (short)temp;
			b_odd_value = (short)(temp >> 16);


			/***** Compute the second eight u chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = b_low2_epi16;
			high2_epi16 = b_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, b_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, b_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			b_even_value = (short)temp;
			b_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			b_low1_epi16 = b_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			b_high1_epi16 = b_high2_epi16;



			
			
			/***** Compute the first eight v chroma output values *****/

			// Preload the second eight lowpass coefficients
			r_low2_epi16 = _mm_load_si128((__m128i *)&r_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			r_high2_epi16 = _mm_load_si128((__m128i *)&r_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = r_low1_epi16;
			high1_epi16 = r_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, r_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, r_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			r_even_value = (short)temp;
			r_odd_value = (short)(temp >> 16);


			/***** Compute the second eight v chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = r_low2_epi16;
			high2_epi16 = r_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, r_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, r_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			r_even_value = (short)temp;
			r_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			r_low1_epi16 = r_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			r_high1_epi16 = r_high2_epi16;




			if(num_channels == 4)
			{
				/***** Compute the first eight v chroma output values *****/

				// Preload the second eight lowpass coefficients
				a_low2_epi16 = _mm_load_si128((__m128i *)&a_lowpass_ptr[column + 8]);

				// Preload the second eight highpass coefficients
				a_high2_epi16 = _mm_load_si128((__m128i *)&a_highpass_ptr[column + 8]);

				// Move the current set of coefficients to working registers
				low1_epi16 = a_low1_epi16;
				high1_epi16 = a_high1_epi16;

				// Apply the even reconstruction filter to the lowpass band
				even_epi16 = low1_epi16;
				temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
				even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
				even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
				even_epi16 = _mm_srai_epi16(even_epi16, 3);
				temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
				even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

				// Shift the highpass correction by one column
				high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

				// Add the highpass correction and divide by two
				even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
				even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
				even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
				even_epi16 = _mm_srai_epi16(even_epi16, 1);

				// Apply the odd reconstruction filter to the lowpass band
				odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
				temp_epi16 = low1_epi16;
				odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
				odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
				odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
				temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
				odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

				// Subtract the highpass correction and divide by two
				odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
				odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
				odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
				odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

				// Interleave the four even and four odd results
				out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

				// Reduce the precision to eight bits
		//		out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
		//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

				// Combine the new output values with the two values from the previous phase
				out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
				temp = _mm_cvtsi128_si32(out_epi16);
				out_epi16 = _mm_insert_epi16(out_epi16, a_even_value, 0);
				out_epi16 = _mm_insert_epi16(out_epi16, a_odd_value, 1);

				// Save the eight u chroma values for packing later
				ag1_output_epi16 = out_epi16;

				// Save the remaining two output values
				a_even_value = (short)temp;
				a_odd_value = (short)(temp >> 16);


				/***** Compute the second eight v chroma output values *****/

				// Move the next set of coefficients to working registers
				low2_epi16 = a_low2_epi16;
				high2_epi16 = a_high2_epi16;

				// Shift in the new pixels for the next stage of the loop
				low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
				temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
				low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

				// Apply the even reconstruction filter to the lowpass band
				even_epi16 = low1_epi16;
				temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
				even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
				even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
				even_epi16 = _mm_srai_epi16(even_epi16, 3);
				temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
				even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

				// Shift in the next four highpass coefficients
				high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
				temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
				high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

				// Add the highpass correction and divide by two
				even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
				even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
				even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
				even_epi16 = _mm_srai_epi16(even_epi16, 1);

				// Apply the odd reconstruction filter to the lowpass band
				odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
				temp_epi16 = low1_epi16;
				odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
				odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
				odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
				temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
				odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

				// Subtract the highpass correction and divide by two
				odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
				odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
				odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
				odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

				// Interleave the four even and four odd results
				out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

				// Reduce the precision to eight bits
		//		out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
		//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

				// Combine the new output values with the two values from the previous phase
				out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
				temp = _mm_cvtsi128_si32(out_epi16);
				out_epi16 = _mm_insert_epi16(out_epi16, a_even_value, 0);
				out_epi16 = _mm_insert_epi16(out_epi16, a_odd_value, 1);

				// Save the eight u chroma values for packing later
				ag2_output_epi16 = out_epi16;

				// Save the remaining two output values
				a_even_value = (short)temp;
				a_odd_value = (short)(temp >> 16);

				// The second eight lowpass coefficients are the current values in the next iteration
				a_low1_epi16 = a_low2_epi16;

				// The second eight highpass coefficients are the current values in the next iteration
				a_high1_epi16 = a_high2_epi16;
			}








			//r_output_epi16  = ((r_output_epi16 - 32768)<<1)+g_output_epi16
			//r_output_epi16  = ((r_output_epi16>>3 - 32768>>3))+g_output_epi16>>4


			g1_output_epi16 = gg1_output_epi16;

			r1_output_epi16 = rg1_output_epi16;//_mm_srli_epi16(rg1_output_epi16,2);
		//	r1_output_epi16 = _mm_subs_epi16(r1_output_epi16, value128_epi32);
		//	r1_output_epi16 = _mm_slli_epi16(r1_output_epi16,1);
		//	r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, g1_output_epi16);

			b1_output_epi16 = bg1_output_epi16;//_mm_srli_epi16(bg1_output_epi16,2);
		//	b1_output_epi16 = _mm_subs_epi16(b1_output_epi16, value128_epi32);
		//	b1_output_epi16 = _mm_slli_epi16(b1_output_epi16,1);
		//	b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, g1_output_epi16);


			 r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, limiterRGB);
			 r1_output_epi16 = _mm_subs_epu16(r1_output_epi16, limiterRGB);

			 g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, limiterRGB);
			 g1_output_epi16 = _mm_subs_epu16(g1_output_epi16, limiterRGB);

			 b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, limiterRGB);
			 b1_output_epi16 = _mm_subs_epu16(b1_output_epi16, limiterRGB);

			 
			if(num_channels == 4)
			{
				a1_output_epi16 = ag1_output_epi16;

				a1_output_epi16 = _mm_adds_epi16(a1_output_epi16, limiterRGB); //12-bit limit
				a1_output_epi16 = _mm_subs_epu16(a1_output_epi16, limiterRGB);

				a1_output_epi16 = _mm_subs_epu16(a1_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				a1_output_epi16 = _mm_slli_epi16(a1_output_epi16, 3);  //15-bit
				a1_output_epi16 = _mm_mulhi_epi16(a1_output_epi16, _mm_set1_epi16(alphacompandGain));

				a1_output_epi16 = _mm_adds_epi16(a1_output_epi16, limiterRGB); //12-bit limit
				a1_output_epi16 = _mm_subs_epu16(a1_output_epi16, limiterRGB);
			}

			zero_epi128 = _mm_setzero_si128();


			{

				__m128i bg_epi16;
				__m128i ra_epi16;
				__m128i bgra1_epi16;
				__m128i bgra2_epi16;

				// Interleave the first four blue and green values
				if(num_channels == 4)
					bg_epi16 = _mm_unpacklo_epi16(a1_output_epi16, r1_output_epi16);
				else
					bg_epi16 = _mm_unpacklo_epi16(a_epi16, r1_output_epi16);


				// Interleave the first four red and alpha values
				ra_epi16 = _mm_unpacklo_epi16(g1_output_epi16, b1_output_epi16);

				// Interleave the first pair of BGRA tuples
				bgra1_epi16 = _mm_unpacklo_epi32(bg_epi16, ra_epi16);

				// Interleave the second pair of BGRA tuples
				bgra2_epi16 = _mm_unpackhi_epi32(bg_epi16, ra_epi16);

				//12bit to 16-bit
				bgra1_epi16 = _mm_slli_epi16(bgra1_epi16, 4);
				bgra2_epi16 = _mm_slli_epi16(bgra2_epi16, 4);

				_mm_store_si128(B64Aptr128++, bgra1_epi16);
				_mm_store_si128(B64Aptr128++, bgra2_epi16);



				// Interleave the first four blue and green values
				if(num_channels == 4)
					bg_epi16 = _mm_unpackhi_epi16(a1_output_epi16, r1_output_epi16);
				else
					bg_epi16 = _mm_unpackhi_epi16(a_epi16, r1_output_epi16);

				// Interleave the first four red and alpha values
				ra_epi16 = _mm_unpackhi_epi16(g1_output_epi16, b1_output_epi16);

				// Interleave the first pair of BGRA tuples
				bgra1_epi16 = _mm_unpacklo_epi32(bg_epi16, ra_epi16);

				// Interleave the second pair of BGRA tuples
				bgra2_epi16 = _mm_unpackhi_epi32(bg_epi16, ra_epi16);

				//12bit to 16-bit
				bgra1_epi16 = _mm_slli_epi16(bgra1_epi16, 4);
				bgra2_epi16 = _mm_slli_epi16(bgra2_epi16, 4);

				_mm_store_si128(B64Aptr128++, bgra1_epi16);
				_mm_store_si128(B64Aptr128++, bgra2_epi16);
			}




			g2_output_epi16 = gg2_output_epi16;//_mm_srli_epi16(gg2_output_epi16,2);

			r2_output_epi16 = rg2_output_epi16;//_mm_srli_epi16(rg2_output_epi16,2);

			b2_output_epi16 = bg2_output_epi16;//_mm_srli_epi16(bg2_output_epi16,2);




			r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, limiterRGB);
			r2_output_epi16 = _mm_subs_epu16(r2_output_epi16, limiterRGB);

			g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, limiterRGB);
			g2_output_epi16 = _mm_subs_epu16(g2_output_epi16, limiterRGB);

			b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, limiterRGB);
			b2_output_epi16 = _mm_subs_epu16(b2_output_epi16, limiterRGB);
			 
			if(num_channels == 4)
			{
				a2_output_epi16 = ag2_output_epi16;
				
				a2_output_epi16 = _mm_adds_epi16(a2_output_epi16, limiterRGB); //12-bit limit
				a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, limiterRGB);

				a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
				a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));

				a2_output_epi16 = _mm_adds_epi16(a2_output_epi16, limiterRGB); //12-bit limit
				a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, limiterRGB);
			}

			{

				__m128i bg_epi16;
				__m128i ra_epi16;
				__m128i bgra1_epi16;
				__m128i bgra2_epi16;

				// Interleave the first four blue and green values
				if(num_channels == 4)	
					bg_epi16 = _mm_unpacklo_epi16(a2_output_epi16, r2_output_epi16);
				else
					bg_epi16 = _mm_unpacklo_epi16(a_epi16, r2_output_epi16);

				// Interleave the first four red and alpha values
				ra_epi16 = _mm_unpacklo_epi16(g2_output_epi16, b2_output_epi16);

				// Interleave the first pair of BGRA tuples
				bgra1_epi16 = _mm_unpacklo_epi32(bg_epi16, ra_epi16);

				// Interleave the second pair of BGRA tuples
				bgra2_epi16 = _mm_unpackhi_epi32(bg_epi16, ra_epi16);

				//12bit to 16-bit
				bgra1_epi16 = _mm_slli_epi16(bgra1_epi16, 4);
				bgra2_epi16 = _mm_slli_epi16(bgra2_epi16, 4);

				_mm_store_si128(B64Aptr128++, bgra1_epi16);
				_mm_store_si128(B64Aptr128++, bgra2_epi16);



				// Interleave the first four blue and green values
				if(num_channels == 4)	
					bg_epi16 = _mm_unpackhi_epi16(a2_output_epi16, r2_output_epi16);
				else
					bg_epi16 = _mm_unpackhi_epi16(a_epi16, r2_output_epi16);

				// Interleave the first four red and alpha values
				ra_epi16 = _mm_unpackhi_epi16(g2_output_epi16, b2_output_epi16);

				// Interleave the first pair of BGRA tuples
				bgra1_epi16 = _mm_unpacklo_epi32(bg_epi16, ra_epi16);

				// Interleave the second pair of BGRA tuples
				bgra2_epi16 = _mm_unpackhi_epi32(bg_epi16, ra_epi16);

				//12bit to 16-bit
				bgra1_epi16 = _mm_slli_epi16(bgra1_epi16, 4);
				bgra2_epi16 = _mm_slli_epi16(bgra2_epi16, 4);

				_mm_store_si128(B64Aptr128++, bgra1_epi16);
				_mm_store_si128(B64Aptr128++, bgra2_epi16);
			}
		}

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

		colptr = (PIXEL16U *)B64Aptr128;


#endif

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column ++)
		{
			int re,ge,be,ae;
			int ro,go,bo,ao;

			/***** First pair of luma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += g_lowpass_ptr[column - 1];
			even -= g_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += g_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += g_highpass_ptr[column];
			even = DivideByShift(even, 1);

			ge = even;


			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= g_lowpass_ptr[column - 1];
			odd += g_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += g_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= g_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			go = odd;


			/***** Pair of u chroma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += b_lowpass_ptr[column - 1];
			even -= b_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += b_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += b_highpass_ptr[column];
			even = DivideByShift(even, 1);

			be = even;


			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= b_lowpass_ptr[column - 1];
			odd += b_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += b_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= b_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			bo = odd;



			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += r_lowpass_ptr[column - 1];
			even -= r_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += r_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += r_highpass_ptr[column];
			even = DivideByShift(even, 1);

			re = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= r_lowpass_ptr[column - 1];
			odd += r_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += r_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= r_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			ro = odd;

//TODO -- alpha companding
			if(num_channels == 4)
			{
				// Apply the even reconstruction filter to the lowpass band
				even = 0;
				even += a_lowpass_ptr[column - 1];
				even -= a_lowpass_ptr[column + 1];
				even += 4; //DAN20050921
				even >>= 3;
				even += a_lowpass_ptr[column + 0];

				// Add the highpass correction
				even += a_highpass_ptr[column];
				even = DivideByShift(even, 1);

				ae = even;

				// Apply the odd reconstruction filter to the lowpass band
				odd = 0;
				odd -= a_lowpass_ptr[column - 1];
				odd += a_lowpass_ptr[column + 1];
				odd += 4; //DAN20050921
				odd >>= 3;
				odd += a_lowpass_ptr[column + 0];

				// Subtract the highpass correction
				odd -= a_highpass_ptr[column];
				odd = DivideByShift(odd, 1);

				ao = odd;

				// Remove the alpha encoding curve.
				//ae -= 16<<4;
				//ae <<= 8;
				//ae += 111;
				//ae /= 223;
				//12-bit SSE calibrated code
				//a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
				//a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));
				ae -= alphacompandDCoffset;
				ae <<= 3; //15-bit
				ae *= alphacompandGain;
				ae >>= 16; //12-bit
				if (ae < 0) ae = 0; else if (ae > 4095) ae = 4095;



				//ao -= 16<<4;
				//ao <<= 8;
				//ao += 111;
				//ao /= 223;
				//12-bit SSE calibrated code
				//a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
				//a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));
				ao -= alphacompandDCoffset;
				ao <<= 3; //15-bit
				ao *= alphacompandGain;
				ao >>= 16; //12-bit
				if (ao < 0) ao = 0; else if (ao > 4095) ao = 4095;

				
				*(colptr++) = SATURATE_16U(ae<<4);
				*(colptr++) = SATURATE_16U(re<<4);
				*(colptr++) = SATURATE_16U(ge<<4);
				*(colptr++) = SATURATE_16U(be<<4);
				*(colptr++) = SATURATE_16U(ao<<4);
				*(colptr++) = SATURATE_16U(ro<<4);
				*(colptr++) = SATURATE_16U(go<<4);
				*(colptr++) = SATURATE_16U(bo<<4);
			}
			else
			{
				*(colptr++) = 0xfff0; //a
				*(colptr++) = SATURATE_16U(re<<4);
				*(colptr++) = SATURATE_16U(ge<<4);
				*(colptr++) = SATURATE_16U(be<<4);
				*(colptr++) = 0xfff0; //a
				*(colptr++) = SATURATE_16U(ro<<4);
				*(colptr++) = SATURATE_16U(go<<4);
				*(colptr++) = SATURATE_16U(bo<<4);
			}
		}


		assert(column == last_column);


		//right hand border processing
		column = last_column - 1;
		colptr -= 8; // two B64A pixels

		// Process the last luma output points with special filters for the right border

		//Green
		even = 0;
		odd = 0;
		// Apply the even border filter to the lowpass band
		even += 5 * g_lowpass_ptr[column + 0];
		even += 4 * g_lowpass_ptr[column - 1];
		even -= 1 * g_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += g_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Save the luma result for later output in the correct order
		g_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * g_lowpass_ptr[column + 0];
		odd -=  4 * g_lowpass_ptr[column - 1];
		odd +=  1 * g_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= g_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Save the luma result for later output in the correct order
		g_odd_value = odd;



		//Red
		even = 0;
		odd = 0;
		// Apply the even border filter to the lowpass band
		even += 5 * r_lowpass_ptr[column + 0];
		even += 4 * r_lowpass_ptr[column - 1];
		even -= 1 * r_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += r_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Save the luma result for later output in the correct order
		r_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * r_lowpass_ptr[column + 0];
		odd -=  4 * r_lowpass_ptr[column - 1];
		odd +=  1 * r_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= r_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Save the luma result for later output in the correct order
		r_odd_value = odd;




		//Blue
		even = 0;
		odd = 0;
		// Apply the even border filter to the lowpass band
		even += 5 * b_lowpass_ptr[column + 0];
		even += 4 * b_lowpass_ptr[column - 1];
		even -= 1 * b_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += b_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Save the luma result for later output in the correct order
		b_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * b_lowpass_ptr[column + 0];
		odd -=  4 * b_lowpass_ptr[column - 1];
		odd +=  1 * b_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= b_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Save the luma result for later output in the correct order
		b_odd_value = odd;


		//Alpha
		if(num_channels == 4)
		{
			even = 0;
			odd = 0;
			// Apply the even border filter to the lowpass band
			even += 5 * a_lowpass_ptr[column + 0];
			even += 4 * a_lowpass_ptr[column - 1];
			even -= 1 * a_lowpass_ptr[column - 2];
			even += ROUNDING(even,8);
			even = DivideByShift(even, 3);

			// Add the highpass correction
			even += a_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Save the luma result for later output in the correct order
			a_even_value = even;

			// Apply the odd border filter to the lowpass band
			odd += 11 * a_lowpass_ptr[column + 0];
			odd -=  4 * a_lowpass_ptr[column - 1];
			odd +=  1 * a_lowpass_ptr[column - 2];
			odd += ROUNDING(odd,8);
			odd = DivideByShift(odd, 3);

			// Subtract the highpass correction
			odd -= a_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Save the luma result for later output in the correct order
			a_odd_value = odd;

			// Remove the alpha encoding curve.
			//a_even_value -= 16<<4;
			//a_even_value <<= 8;
			//a_even_value += 111;
			//a_even_value /= 223;
			//12-bit SSE calibrated code
			//a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
			//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
			//a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));
			a_even_value -= alphacompandDCoffset;
			a_even_value <<= 3; //15-bit
			a_even_value *= alphacompandGain;
			a_even_value >>= 16; //12-bit
			if (a_even_value < 0) a_even_value = 0; else if (a_even_value > 4095) a_even_value = 4095;

			//a_odd_value -= 16<<4;
			//a_odd_value <<= 8;
			//a_odd_value += 111;
			//a_odd_value /= 223;
			//12-bit SSE calibrated code
			//a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
			//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
			//a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));
			a_odd_value -= alphacompandDCoffset;
			a_odd_value <<= 3; //15-bit
			a_odd_value *= alphacompandGain;
			a_odd_value >>= 16; //12-bit
			if (a_odd_value < 0) a_odd_value = 0; else if (a_odd_value > 4095) a_odd_value = 4095;


			*colptr = SATURATE_16U(a_even_value<<4);
			*(colptr+4) = SATURATE_16U(a_odd_value<<4);
		}


		colptr++; //a
		*(colptr++) = SATURATE_16U(r_even_value<<4);
		*(colptr++) = SATURATE_16U(g_even_value<<4);
		*(colptr++) = SATURATE_16U(b_even_value<<4);
		colptr++; //a
		*(colptr++) = SATURATE_16U(r_odd_value<<4);
		*(colptr++) = SATURATE_16U(g_odd_value<<4);
		*(colptr++) = SATURATE_16U(b_odd_value<<4);


		// Should have exited the loop at the column for right border processing
	//	assert(column == last_column);
		// Advance to the next row of coefficients in each channel
		g_lowpass_ptr += lowpass_pitch[0];
		b_lowpass_ptr += lowpass_pitch[1];
		r_lowpass_ptr += lowpass_pitch[2];
		g_highpass_ptr += highpass_pitch[0];
		b_highpass_ptr += highpass_pitch[1];
		r_highpass_ptr += highpass_pitch[2];

		if(num_channels == 4)
		{
			a_lowpass_ptr += lowpass_pitch[3];
			a_highpass_ptr += highpass_pitch[3];
		}

		// Advance the output pointer to the next row
		output += output_pitch;
	}
}

// Used in RT RG30 playback
// Apply the inverse horizontal transform to reconstruct a strip of rows into packed RG30 pixels
void InvertHorizontalStrip16sRGB2RG30(HorizontalFilterParams)
{
	int num_channels = CODEC_NUM_CHANNELS;
	int height = roi.height;
	int width = roi.width;

	// Note that the u and v chroma values are swapped
	PIXEL *gg_lowpass_ptr = lowpass_band[0];
	PIXEL *rg_lowpass_ptr = lowpass_band[1];
	PIXEL *bg_lowpass_ptr = lowpass_band[2];
	PIXEL *gg_highpass_ptr = highpass_band[0];
	PIXEL *rg_highpass_ptr = highpass_band[1];
	PIXEL *bg_highpass_ptr = highpass_band[2];

	uint8_t *output = output_image;

	// Process 8 luma coefficients per loop iteration
	const int column_step = 8;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int channel;
	int row;

	// Compute the amount of scaling required to reduce the output precision
	//int descale_shift = (precision - 8);

	int shift = 8;

	float scale;

	scale = 4.0;

	shift-=2;

	format = format & 0xffff;// mask off color_space

	// Convert the pitch to units of pixels
	for (channel = 0; channel < num_channels; channel++)
	{
		lowpass_pitch[channel] /= sizeof(PIXEL);
		highpass_pitch[channel] /= sizeof(PIXEL);
	}

	output_pitch /= sizeof(uint8_t);

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
		__m128i gg_low1_epi16;		// Lowpass coefficients
		__m128i gg_low2_epi16;
		__m128i bg_low1_epi16;
		__m128i bg_low2_epi16;
		__m128i rg_low1_epi16;
		__m128i rg_low2_epi16;

		__m128i gg_high1_epi16;		// Highpass coefficients
		__m128i gg_high2_epi16;
		__m128i bg_high1_epi16;
		__m128i bg_high2_epi16;
		__m128i rg_high1_epi16;
		__m128i rg_high2_epi16;

		// The fast loop merges values from different phases to allow aligned stores
		//__m128i *outptr = (__m128i *)&output[0];
		__m128i *RG30ptr128 = (__m128i *)&output[0];
		
		__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x0fff);

#endif
		//PIXEL16U *colptr = (PIXEL16U *)&output[0];
		uint32_t *RG30ptr = (uint32_t *)&output[0];

		int32_t gg_even_value;
		int32_t bg_even_value;
		int32_t rg_even_value;
		int32_t gg_odd_value;
		int32_t bg_odd_value;
		int32_t rg_odd_value;


		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		int32_t even = 0;
		int32_t odd = 0;


		// Apply the even reconstruction filter to the lowpass band
		even += 11 * gg_lowpass_ptr[column + 0];
		even -=  4 * gg_lowpass_ptr[column + 1];
		even +=  1 * gg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += gg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
	//	even >>= descale_shift;

		// Save the value for use in the fast loop
		gg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * gg_lowpass_ptr[column + 0];
		odd += 4 * gg_lowpass_ptr[column + 1];
		odd -= 1 * gg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= gg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
	//	odd >>= descale_shift;

		// Save the value for use in the fast loop
		gg_odd_value = odd;


		// Process the first two u chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * bg_lowpass_ptr[column + 0];
		even -=  4 * bg_lowpass_ptr[column + 1];
		even +=  1 * bg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += bg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
	//	even >>= descale_shift;

		// Save the value for use in the fast loop
		bg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * bg_lowpass_ptr[column + 0];
		odd += 4 * bg_lowpass_ptr[column + 1];
		odd -= 1 * bg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= bg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
	//	odd >>= descale_shift;

		// Save the value for use in the fast loop
		bg_odd_value = odd;


		// Process the first two v chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * rg_lowpass_ptr[column + 0];
		even -=  4 * rg_lowpass_ptr[column + 1];
		even +=  1 * rg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += rg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
	//	even >>= descale_shift;

		// Save the value for use in the fast loop
		rg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * rg_lowpass_ptr[column + 0];
		odd += 4 * rg_lowpass_ptr[column + 1];
		odd -= 1 * rg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= rg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
	//	odd >>= descale_shift;

		// Save the value for use in the fast loop
		rg_odd_value = odd;

#if (1 && XMMOPT)

		// Preload the first eight lowpass luma coefficients
		gg_low1_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[0]);

		// Preload the first eight highpass luma coefficients
		gg_high1_epi16 = _mm_load_si128((__m128i *)&gg_highpass_ptr[0]);

		// Preload the first eight lowpass u chroma coefficients
		bg_low1_epi16 = _mm_load_si128((__m128i *)&bg_lowpass_ptr[0]);

		// Preload the first eight highpass u chroma coefficients
		bg_high1_epi16 = _mm_load_si128((__m128i *)&bg_highpass_ptr[0]);

		// Preload the first eight lowpass v chroma coefficients
		rg_low1_epi16 = _mm_load_si128((__m128i *)&rg_lowpass_ptr[0]);

		// Preload the first eight highpass v chroma coefficients
 		rg_high1_epi16 = _mm_load_si128((__m128i *)&rg_highpass_ptr[0]);


		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i g1_output_epi16;
			__m128i g2_output_epi16;
			__m128i r1_output_epi16;
			__m128i r2_output_epi16;
			__m128i b1_output_epi16;
			__m128i b2_output_epi16;

			__m128i gg1_output_epi16;
			__m128i gg2_output_epi16;
			__m128i rg1_output_epi16;
			__m128i rg2_output_epi16;
			__m128i bg1_output_epi16;
			__m128i bg2_output_epi16;

			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			__m128i zero_epi128;

			__m128i rr_epi32;
			__m128i gg_epi32;
			__m128i bb_epi32;

			__m128i out_epi16;		// Reconstructed data
			////__m128i mask_epi16;
			////__m128i lsb_epi16;
			////__m128i sign_epi16;
			//__m128i high_epi16;

			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;

			//DAN031304 -- correct inverse filter
			__m128i half_epi16 = _mm_set1_epi16(4);
			__m128i offset_epi16 = _mm_set1_epi16(2048);

			uint32_t temp;		// Temporary register for last two values


			/***** Compute the first eight luma output values *****/

			// Preload the second eight lowpass coefficients
			gg_low2_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			gg_high2_epi16 = _mm_load_si128((__m128i *)&gg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = gg_low1_epi16;
			high1_epi16 = gg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);


			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_odd_value, 1);

			// Save the eight luma values for packing later
			gg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			gg_even_value = (short)temp;
			gg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight luma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = gg_low2_epi16;
			high2_epi16 = gg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_odd_value, 1);

			// Save the eight luma values for packing later
			gg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			gg_even_value = (short)temp;
			gg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are used later in the loop
			gg_low1_epi16 = gg_low2_epi16;

			// The second eight highpass coefficients are used later in the loop
			gg_high1_epi16 = gg_high2_epi16;


			/***** Compute the first eight u chroma output values *****/

			// Preload the second eight lowpass coefficients
			bg_low2_epi16 = _mm_load_si128((__m128i *)&bg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			bg_high2_epi16 = _mm_load_si128((__m128i *)&bg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = bg_low1_epi16;
			high1_epi16 = bg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			bg_even_value = (short)temp;
			bg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight u chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = bg_low2_epi16;
			high2_epi16 = bg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			bg_even_value = (short)temp;
			bg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			bg_low1_epi16 = bg_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			bg_high1_epi16 = bg_high2_epi16;



			/***** Compute the first eight v chroma output values *****/

			// Preload the second eight lowpass coefficients
			rg_low2_epi16 = _mm_load_si128((__m128i *)&rg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			rg_high2_epi16 = _mm_load_si128((__m128i *)&rg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = rg_low1_epi16;
			high1_epi16 = rg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding1_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			rg_even_value = (short)temp;
			rg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight v chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = rg_low2_epi16;
			high2_epi16 = rg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
	//		out_epi16 = _mm_adds_epi16(out_epi16, rounding2_pi16);
	//		out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			rg_even_value = (short)temp;
			rg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			rg_low1_epi16 = rg_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			rg_high1_epi16 = rg_high2_epi16;












			//r_output_epi16  = ((rg_output_epi16 - 32768)<<1)+gg_output_epi16
			//r_output_epi16  = ((rg_output_epi16>>3 - 32768>>3))+gg_output_epi16>>4


			g1_output_epi16 = gg1_output_epi16;

			r1_output_epi16 = rg1_output_epi16;//_mm_srli_epi16(rg1_output_epi16,2);
		//	r1_output_epi16 = _mm_subs_epi16(r1_output_epi16, value128_epi32);
		//	r1_output_epi16 = _mm_slli_epi16(r1_output_epi16,1);
		//	r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, g1_output_epi16);

			b1_output_epi16 = bg1_output_epi16;//_mm_srli_epi16(bg1_output_epi16,2);
		//	b1_output_epi16 = _mm_subs_epi16(b1_output_epi16, value128_epi32);
		//	b1_output_epi16 = _mm_slli_epi16(b1_output_epi16,1);
		//	b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, g1_output_epi16);


			 r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, limiterRGB);
			 r1_output_epi16 = _mm_subs_epu16(r1_output_epi16, limiterRGB);

			 g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, limiterRGB);
			 g1_output_epi16 = _mm_subs_epu16(g1_output_epi16, limiterRGB);

			 b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, limiterRGB);
			 b1_output_epi16 = _mm_subs_epu16(b1_output_epi16, limiterRGB);


			zero_epi128 = _mm_setzero_si128();




			r1_output_epi16 = _mm_srli_epi16(r1_output_epi16, 2);
			g1_output_epi16 = _mm_srli_epi16(g1_output_epi16, 2);
			b1_output_epi16 = _mm_srli_epi16(b1_output_epi16, 2);

			// Interleave the first four blue and green values
			rr_epi32 = _mm_unpacklo_epi16(r1_output_epi16, zero_epi128);
			gg_epi32 = _mm_unpacklo_epi16(g1_output_epi16, zero_epi128);
			bb_epi32 = _mm_unpacklo_epi16(b1_output_epi16, zero_epi128);


			switch(format)
			{
			case DECODED_FORMAT_RG30:
			case DECODED_FORMAT_AB10:
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				bb_epi32 = _mm_slli_epi32(bb_epi32, 20);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);
				break;
			case DECODED_FORMAT_R210:
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				// the algorithm is:
				// 1) [A B C D] => [B A D C]
				// 2) [B A D C] => [D C B A]

				// do first swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi16( rr_epi32, 8 ),
										_mm_srli_epi16( rr_epi32, 8 ) ); //swap it
				// do second swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi32( rr_epi32, 16 ),
										_mm_srli_epi32( rr_epi32, 16 ) ); //swap it
				break;

			case DECODED_FORMAT_DPX0:
				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				rr_epi32 = _mm_slli_epi32(rr_epi32, 2);

				// the algorithm is:
				// 1) [A B C D] => [B A D C]
				// 2) [B A D C] => [D C B A]

				// do first swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi16( rr_epi32, 8 ),
										_mm_srli_epi16( rr_epi32, 8 ) ); //swap it
				// do second swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi32( rr_epi32, 16 ),
										_mm_srli_epi32( rr_epi32, 16 ) ); //swap it
				break;

			case DECODED_FORMAT_AR10:
				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);
				break;
			default:
				assert(0); //unknown format
				break;
			}

			_mm_store_si128(RG30ptr128++, rr_epi32);


			rr_epi32 = _mm_unpackhi_epi16(r1_output_epi16, zero_epi128);
			gg_epi32 = _mm_unpackhi_epi16(g1_output_epi16, zero_epi128);
			bb_epi32 = _mm_unpackhi_epi16(b1_output_epi16, zero_epi128);

			switch(format)
			{
			case DECODED_FORMAT_RG30:
			case DECODED_FORMAT_AB10:
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				bb_epi32 = _mm_slli_epi32(bb_epi32, 20);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);
				break;

			case DECODED_FORMAT_R210:
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				// the algorithm is:
				// 1) [A B C D] => [B A D C]
				// 2) [B A D C] => [D C B A]

				// do first swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi16( rr_epi32, 8 ),
										_mm_srli_epi16( rr_epi32, 8 ) ); //swap it
				// do second swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi32( rr_epi32, 16 ),
										_mm_srli_epi32( rr_epi32, 16 ) ); //swap it

				break;

			case DECODED_FORMAT_DPX0:
				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				rr_epi32 = _mm_slli_epi32(rr_epi32, 2);

				// the algorithm is:
				// 1) [A B C D] => [B A D C]
				// 2) [B A D C] => [D C B A]

				// do first swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi16( rr_epi32, 8 ),
										_mm_srli_epi16( rr_epi32, 8 ) ); //swap it
				// do second swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi32( rr_epi32, 16 ),
										_mm_srli_epi32( rr_epi32, 16 ) ); //swap it

				break;

			case DECODED_FORMAT_AR10:
				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);
				break;
			default:
				assert(0); //unknown format
				break;
			}
			_mm_store_si128(RG30ptr128++, rr_epi32);







			g2_output_epi16 = gg2_output_epi16;//_mm_srli_epi16(gg2_output_epi16,2);

			r2_output_epi16 = rg2_output_epi16;//_mm_srli_epi16(rg2_output_epi16,2);

			b2_output_epi16 = bg2_output_epi16;//_mm_srli_epi16(bg2_output_epi16,2);



			 r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, limiterRGB);
			 r2_output_epi16 = _mm_subs_epu16(r2_output_epi16, limiterRGB);

			 g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, limiterRGB);
			 g2_output_epi16 = _mm_subs_epu16(g2_output_epi16, limiterRGB);

			 b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, limiterRGB);
			 b2_output_epi16 = _mm_subs_epu16(b2_output_epi16, limiterRGB);



			r1_output_epi16 = _mm_srli_epi16(r2_output_epi16, 2);
			g1_output_epi16 = _mm_srli_epi16(g2_output_epi16, 2);
			b1_output_epi16 = _mm_srli_epi16(b2_output_epi16, 2);

			// Interleave the first four blue and green values
			rr_epi32 = _mm_unpacklo_epi16(r1_output_epi16, zero_epi128);
			gg_epi32 = _mm_unpacklo_epi16(g1_output_epi16, zero_epi128);
			bb_epi32 = _mm_unpacklo_epi16(b1_output_epi16, zero_epi128);


			switch(format)
			{
			case DECODED_FORMAT_RG30:
			case DECODED_FORMAT_AB10:
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				bb_epi32 = _mm_slli_epi32(bb_epi32, 20);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				_mm_store_si128(RG30ptr128++, rr_epi32);

				rr_epi32 = _mm_unpackhi_epi16(r1_output_epi16, zero_epi128);
				gg_epi32 = _mm_unpackhi_epi16(g1_output_epi16, zero_epi128);
				bb_epi32 = _mm_unpackhi_epi16(b1_output_epi16, zero_epi128);

				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				bb_epi32 = _mm_slli_epi32(bb_epi32, 20);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				_mm_store_si128(RG30ptr128++, rr_epi32);
				break;
			case DECODED_FORMAT_R210:
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				// the algorithm is:
				// 1) [A B C D] => [B A D C]
				// 2) [B A D C] => [D C B A]

				// do first swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi16( rr_epi32, 8 ),
										_mm_srli_epi16( rr_epi32, 8 ) ); //swap it
				// do second swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi32( rr_epi32, 16 ),
										_mm_srli_epi32( rr_epi32, 16 ) ); //swap it

				_mm_store_si128(RG30ptr128++, rr_epi32);

				rr_epi32 = _mm_unpackhi_epi16(r1_output_epi16, zero_epi128);
				gg_epi32 = _mm_unpackhi_epi16(g1_output_epi16, zero_epi128);
				bb_epi32 = _mm_unpackhi_epi16(b1_output_epi16, zero_epi128);

				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				// the algorithm is:
				// 1) [A B C D] => [B A D C]
				// 2) [B A D C] => [D C B A]

				// do first swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi16( rr_epi32, 8 ),
										_mm_srli_epi16( rr_epi32, 8 ) ); //swap it
				// do second swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi32( rr_epi32, 16 ),
										_mm_srli_epi32( rr_epi32, 16 ) ); //swap it

				_mm_store_si128(RG30ptr128++, rr_epi32);
				break;

			case DECODED_FORMAT_DPX0:
				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				rr_epi32 = _mm_slli_epi32(rr_epi32, 2);

				// the algorithm is:
				// 1) [A B C D] => [B A D C]
				// 2) [B A D C] => [D C B A]

				// do first swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi16( rr_epi32, 8 ),
										_mm_srli_epi16( rr_epi32, 8 ) ); //swap it
				// do second swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi32( rr_epi32, 16 ),
										_mm_srli_epi32( rr_epi32, 16 ) ); //swap it

				_mm_store_si128(RG30ptr128++, rr_epi32);

				rr_epi32 = _mm_unpackhi_epi16(r1_output_epi16, zero_epi128);
				gg_epi32 = _mm_unpackhi_epi16(g1_output_epi16, zero_epi128);
				bb_epi32 = _mm_unpackhi_epi16(b1_output_epi16, zero_epi128);

				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				rr_epi32 = _mm_slli_epi32(rr_epi32, 2);

				// the algorithm is:
				// 1) [A B C D] => [B A D C]
				// 2) [B A D C] => [D C B A]

				// do first swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi16( rr_epi32, 8 ),
										_mm_srli_epi16( rr_epi32, 8 ) ); //swap it
				// do second swap
				rr_epi32 = _mm_or_si128( _mm_slli_epi32( rr_epi32, 16 ),
										_mm_srli_epi32( rr_epi32, 16 ) ); //swap it

				_mm_store_si128(RG30ptr128++, rr_epi32);
				break;

			case DECODED_FORMAT_AR10:
				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				_mm_store_si128(RG30ptr128++, rr_epi32);

				rr_epi32 = _mm_unpackhi_epi16(r1_output_epi16, zero_epi128);
				gg_epi32 = _mm_unpackhi_epi16(g1_output_epi16, zero_epi128);
				bb_epi32 = _mm_unpackhi_epi16(b1_output_epi16, zero_epi128);

				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				_mm_store_si128(RG30ptr128++, rr_epi32);
				break;
			default:
				assert(0); //unknown format
				break;
			}
		}

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

		RG30ptr = (uint32_t *)RG30ptr128;
#endif

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column ++)
		{
			int re,ge,be;
			int ro,go,bo;

			/***** First pair of luma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += gg_lowpass_ptr[column - 1];
			even -= gg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += gg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += gg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			ge = even;


			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= gg_lowpass_ptr[column - 1];
			odd += gg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += gg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= gg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			go = odd;


			/***** Pair of u chroma output values *****/

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += bg_lowpass_ptr[column - 1];
			even -= bg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += bg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += bg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			be = even;


			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= bg_lowpass_ptr[column - 1];
			odd += bg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += bg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= bg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			bo = odd;



			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += rg_lowpass_ptr[column - 1];
			even -= rg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += rg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += rg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			re = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= rg_lowpass_ptr[column - 1];
			odd += rg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += rg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= rg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			ro = odd;

			re >>= 2;
			ge >>= 2;
			be >>= 2;
			ro >>= 2;
			go >>= 2;
			bo >>= 2;

			if(re < 0) re = 0; if(re > 1023) re = 1023;
			if(ge < 0) ge = 0; if(ge > 1023) ge = 1023;
			if(be < 0) be = 0; if(be > 1023) be = 1023;
			if(ro < 0) ro = 0; if(ro > 1023) ro = 1023;
			if(go < 0) go = 0; if(go > 1023) go = 1023;
			if(bo < 0) bo = 0; if(bo > 1023) bo = 1023;

			switch(format)
			{
			case DECODED_FORMAT_RG30:
			case DECODED_FORMAT_AB10:
				*(RG30ptr++) = (be<<20)+(ge<<10)+re;
				*(RG30ptr++) = (bo<<20)+(go<<10)+ro;
				break;
			case DECODED_FORMAT_AR10:
				*(RG30ptr++) = (re<<20)+(ge<<10)+be;
				*(RG30ptr++) = (ro<<20)+(go<<10)+bo;
				break;
			case DECODED_FORMAT_R210:
				*(RG30ptr++) = SwapInt32((re<<20)+(ge<<10)+(be));
				*(RG30ptr++) = SwapInt32((ro<<20)+(go<<10)+(bo));
				break;
			case DECODED_FORMAT_DPX0:
				*(RG30ptr++) = SwapInt32((re<<22)+(ge<<12)+(be<<2));
				*(RG30ptr++) = SwapInt32((ro<<22)+(go<<12)+(bo<<2));
				break;
			}
		}



		assert(column == last_column);


		//right hand border processing
		column = last_column - 1;
		RG30ptr -= 2; // two pixels

		// Process the last luma output points with special filters for the right border

		//Green
		even = 0;
		odd = 0;
		// Apply the even border filter to the lowpass band
		even += 5 * gg_lowpass_ptr[column + 0];
		even += 4 * gg_lowpass_ptr[column - 1];
		even -= 1 * gg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += gg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Save the luma result for later output in the correct order
		gg_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * gg_lowpass_ptr[column + 0];
		odd -=  4 * gg_lowpass_ptr[column - 1];
		odd +=  1 * gg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= gg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Save the luma result for later output in the correct order
		gg_odd_value = odd;



		//Red
		even = 0;
		odd = 0;
		// Apply the even border filter to the lowpass band
		even += 5 * rg_lowpass_ptr[column + 0];
		even += 4 * rg_lowpass_ptr[column - 1];
		even -= 1 * rg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += rg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Save the luma result for later output in the correct order
		rg_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * rg_lowpass_ptr[column + 0];
		odd -=  4 * rg_lowpass_ptr[column - 1];
		odd +=  1 * rg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= rg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Save the luma result for later output in the correct order
		rg_odd_value = odd;




		//Blue
		even = 0;
		odd = 0;
		// Apply the even border filter to the lowpass band
		even += 5 * bg_lowpass_ptr[column + 0];
		even += 4 * bg_lowpass_ptr[column - 1];
		even -= 1 * bg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += bg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Save the luma result for later output in the correct order
		bg_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * bg_lowpass_ptr[column + 0];
		odd -=  4 * bg_lowpass_ptr[column - 1];
		odd +=  1 * bg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= bg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Save the luma result for later output in the correct order
		bg_odd_value = odd;

		rg_even_value >>= 2;
		gg_even_value >>= 2;
		bg_even_value >>= 2;
		rg_odd_value >>= 2;
		gg_odd_value >>= 2;
		bg_odd_value >>= 2;

		if(rg_even_value < 0) rg_even_value = 0; if(rg_even_value > 1023) rg_even_value = 1023;
		if(gg_even_value < 0) gg_even_value = 0; if(gg_even_value > 1023) gg_even_value = 1023;
		if(bg_even_value < 0) bg_even_value = 0; if(bg_even_value > 1023) bg_even_value = 1023;
		if(rg_odd_value < 0) rg_odd_value = 0; if(rg_odd_value > 1023) rg_odd_value = 1023;
		if(gg_odd_value < 0) gg_odd_value = 0; if(gg_odd_value > 1023) gg_odd_value = 1023;
		if(bg_odd_value < 0) bg_odd_value = 0; if(bg_odd_value > 1023) bg_odd_value = 1023;

		switch(format)
		{
		case DECODED_FORMAT_RG30:
		case DECODED_FORMAT_AB10:
			*(RG30ptr++) = (bg_even_value<<20)+(gg_even_value<<10)+rg_even_value;
			*(RG30ptr++) = (bg_odd_value<<20)+(gg_odd_value<<10)+rg_odd_value;
			break;
		case DECODED_FORMAT_AR10:
			*(RG30ptr++) = (rg_even_value<<20)+(gg_even_value<<10)+bg_even_value;
			*(RG30ptr++) = (rg_odd_value<<20)+(gg_odd_value<<10)+bg_odd_value;
			break;
		case DECODED_FORMAT_R210:
			*(RG30ptr++) = SwapInt32((rg_even_value<<20)+(gg_even_value<<10)+(bg_even_value));
			*(RG30ptr++) = SwapInt32((rg_odd_value<<20)+(gg_odd_value<<10)+(bg_odd_value));
			break;
		case DECODED_FORMAT_DPX0:
			*(RG30ptr++) = SwapInt32((rg_even_value<<22)+(gg_even_value<<12)+(bg_even_value<<2));
			*(RG30ptr++) = SwapInt32((rg_odd_value<<22)+(gg_odd_value<<12)+(bg_odd_value<<2));
			break;
		}


		// Should have exited the loop at the column for right border processing
	//	assert(column == last_column);
		// Advance to the next row of coefficients in each channel
		gg_lowpass_ptr += lowpass_pitch[0];
		bg_lowpass_ptr += lowpass_pitch[1];
		rg_lowpass_ptr += lowpass_pitch[2];
		gg_highpass_ptr += highpass_pitch[0];
		bg_highpass_ptr += highpass_pitch[1];
		rg_highpass_ptr += highpass_pitch[2];

		// Advance the output pointer to the next row
		output += output_pitch;
	}
}



#if 0

// Apply the inverse horizontal transform to reconstruct a strip of rows (old version using MMX)
void InvertHorizontalStrip16sToRow16u(PIXEL *lowpass_band, int lowpass_pitch,
									  PIXEL *highpass_band, int highpass_pitch,
									  PIXEL16U *output, int output_pitch,
									  ROI roi, int precision)
{
	int height = roi.height;
	int width = roi.width;
	PIXEL *lowpass = lowpass_band;
	PIXEL *highpass = highpass_band;
	const int column_step = 4;
	const int last_column = width - 1;
	int post_column = last_column - (last_column % column_step);
	int row;

	// Calculate the shift required to output 16-bit pixels
	int scale_shift = (16 - precision);
//	int protection = 0x7fff - 2047;
	int protection = 0x7fff - (2<<precision) + 1;


	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL16U);

	// Adjust the end of the fast loop if necessary
	if (post_column == last_column)
		post_column -= column_step;

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		__m64 low1_pi16;	// Lowpass coefficients
		__m64 low2_pi16;
		__m64 high1_pi16;	// Highpass coefficients
		__m64 high2_pi16;

		__m64 overflowprotect_pi16 = _mm_set1_pi16(protection);

		PIXEL16U *colptr;

		int32_t even;
		int32_t odd;

		// The fast loop computes output points starting at the third column
		__m64 *outptr = (__m64 *)&output[2];

		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * lowpass[column + 0];
		even -=  4 * lowpass[column + 1];
		even +=  1 * lowpass[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even = DivideByShift(even, 1);

		// Scale the result to the full 16-bit range
		even <<= scale_shift;

		// Place the even result in the even column
		//even >>= _INVERSE_TEMPORAL_PRESCALE;
		output[0] = SATURATE_16U(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * lowpass[column + 0];
		odd += 4 * lowpass[column + 1];
		odd -= 1 * lowpass[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd = DivideByShift(odd, 1);

		// Scale the result to the full 16-bit range
		odd <<= scale_shift;

		// Place the odd result in the odd column
		//odd >>= _INVERSE_TEMPORAL_PRESCALE;
		output[1] = SATURATE_16U(odd);

		// Preload the first four lowpass coefficients
		low1_pi16 = *((__m64 *)&lowpass[column]);

		// Preload the first four highpass coefficients
		high1_pi16 = *((__m64 *)&highpass[column]);

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m64 even_pi16;	// Result of convolution with even filter
			__m64 odd_pi16;		// Result of convolution with odd filter
			__m64 temp_pi16;
			__m64 out1_pi16;	// Reconstructed data - first set of four
			__m64 out2_pi16;	// Reconstructed data - second set of four
			__m64 mask_pi16;
			__m64 lsb_pi16;
			__m64 sign_pi16;
			__m64 high_pi16;
			__m64 half_pi16 = _mm_set1_pi16(4); //DAN031604 4 to 6

			// Right shift to reduce the output pixel size to one byte
			//__m64 scale_si64 = _mm_cvtsi32_si64(scale_shift);

			// Preload the next four lowpass coefficients
			low2_pi16 = *((__m64 *)&lowpass[column+4]);

#define COLUMN_0		_MM_SHUFFLE(0, 3, 2, 1)
#define COLUMN_PLUS_1	_MM_SHUFFLE(1, 0, 3, 2)

			/***** Compute the first two even and two odd output points *****/

			// Apply the even reconstruction filter to the lowpass band
		/*	even_pi16 = low1_pi16;  //- 1
			temp_pi16 = _mm_slli_pi16(low1_pi16, 3);  // temp = low[0]<<3
			temp_pi16 = _mm_shuffle_pi16(temp_pi16, _MM_SHUFFLE(0, 3, 2, 1)); // + 0
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16);
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2)); // + 1
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16);

			#if 1
			// Apply the rounding adjustment
			even_pi16 = _mm_adds_pi16(even_pi16, _mm_set1_pi16(4));
			#endif
			// Divide by eight
			even_pi16 = _mm_srai_pi16(even_pi16, 3);
	*/
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_PLUS_1); 			// [col+1]
			even_pi16 = _mm_subs_pi16(low1_pi16, temp_pi16);  					// [col-1] - [col+1]
			even_pi16 = _mm_adds_pi16(even_pi16, half_pi16);  					// [col-1] - [col+1] + 4
			even_pi16 = _mm_srai_pi16(even_pi16, 3);		  					// ([col-1] - [col+1] + 4) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_0); 	//
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16); 					// (([col-1] - [col+1] + 4) >> 3) + [col+0]

			// Shift the highpass correction by one column
			high1_pi16 = _mm_srli_si64(high1_pi16, 16);

			// Prescale for 8bit output - DAN 4/5/02
			high_pi16 = high1_pi16;

			// Add the highpass correction
			even_pi16 = _mm_adds_pi16(even_pi16, high_pi16);
			even_pi16 = _mm_adds_pi16(even_pi16, overflowprotect_pi16);
			even_pi16 = _mm_subs_pu16(even_pi16, overflowprotect_pi16);
			even_pi16 = _mm_srai_pi16(even_pi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
	/*		odd_pi16 = _mm_slli_pi16(low1_pi16, 3);
			odd_pi16 = _mm_shuffle_pi16(odd_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16);
			odd_pi16 = _mm_subs_pi16(odd_pi16, low1_pi16);

			#if 1
			// Apply the rounding adjustment
			odd_pi16 = _mm_adds_pi16(odd_pi16, _mm_set1_pi16(4));
			#endif
			// Divide by eight
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);
	*/

			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_PLUS_1); 			// [col+1]
			odd_pi16 = _mm_subs_pi16(temp_pi16, low1_pi16);  					// [col+1] - [col-1]
			odd_pi16 = _mm_adds_pi16(odd_pi16, half_pi16);  					// [col+1] - [col-1] + 4
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);		  						// ([col+1] - [col-1] + 4) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_0); 	//
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16); 						// (([col+1] - [col-1] + 4) >> 3) + [col+0]

			// Subtract the highpass correction
			odd_pi16 = _mm_subs_pi16(odd_pi16, high_pi16);
			odd_pi16 = _mm_adds_pi16(odd_pi16, overflowprotect_pi16);
			odd_pi16 = _mm_subs_pu16(odd_pi16, overflowprotect_pi16);
			odd_pi16 = _mm_srai_pi16(odd_pi16, 1);

			// Interleave the even and odd results
			out1_pi16 = _mm_unpacklo_pi16(even_pi16, odd_pi16);
			//out_pi16 = _mm_max_pi16(out_pi16, _mm_setzero_si64());

			// Scale the result to the full 16-bit range
			out1_pi16 = _mm_slli_pi16(out1_pi16, scale_shift);

			// Store the first four results
			*(outptr++) = out1_pi16;


			/***** Compute the second two even and two odd output points *****/

			// Preload the highpass correction
			high2_pi16 = *((__m64 *)&highpass[column+4]);

			// Shift in the new pixels for the next stage of the loop
			low1_pi16 = _mm_srli_si64(low1_pi16, 32);
			temp_pi16 = _mm_slli_si64(low2_pi16, 32);
			low1_pi16 = _mm_or_si64(low1_pi16, temp_pi16);

			// Apply the even reconstruction filter to the lowpass band
	/*		even_pi16 = low1_pi16;
			temp_pi16 = _mm_slli_pi16(low1_pi16, 3);
			temp_pi16 = _mm_shuffle_pi16(temp_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16);
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16);

			#if 1
			// Apply the rounding adjustment
			even_pi16 = _mm_adds_pi16(even_pi16, _mm_set1_pi16(4));
			#endif
			// Divide by eight
			even_pi16 = _mm_srai_pi16(even_pi16, 3);
	*/
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_PLUS_1); 			// [col+1]
			even_pi16 = _mm_subs_pi16(low1_pi16, temp_pi16);  					// [col-1] - [col+1]
			even_pi16 = _mm_adds_pi16(even_pi16, half_pi16);  					// [col-1] - [col+1] + 4
			even_pi16 = _mm_srai_pi16(even_pi16, 3);		  					// ([col-1] - [col+1] + 4) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_0); 	//
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16); 					// (([col-1] - [col+1] + 4) >> 3) + [col+0]


			// Shift in the next two highpass coefficients
			high1_pi16 = _mm_srli_si64(high1_pi16, 2*16);
			high1_pi16 = _mm_or_si64(high1_pi16, _mm_slli_si64(high2_pi16, 16));

			// Prescale for 8bit output - DAN 4/5/02
			high_pi16 = high1_pi16;

			// Add the highpass correction
			even_pi16 = _mm_adds_pi16(even_pi16, high_pi16);
			even_pi16 = _mm_adds_pi16(even_pi16, overflowprotect_pi16);
			even_pi16 = _mm_subs_pu16(even_pi16, overflowprotect_pi16);
			even_pi16 = _mm_srai_pi16(even_pi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
	/*		odd_pi16 = _mm_slli_pi16(low1_pi16, 3);
			odd_pi16 = _mm_shuffle_pi16(odd_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16);
			odd_pi16 = _mm_subs_pi16(odd_pi16, low1_pi16);

			#if 1
			// Apply the rounding adjustment
			odd_pi16 = _mm_adds_pi16(odd_pi16, _mm_set1_pi16(4));
			#endif
			// Divide by eight
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);
	*/
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_PLUS_1); 			// [col+1]
			odd_pi16 = _mm_subs_pi16(temp_pi16, low1_pi16);  					// [col+1] - [col-1]
			odd_pi16 = _mm_adds_pi16(odd_pi16, half_pi16);  					// [col+1] - [col-1] + 4
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);		  						// ([col+1] - [col-1] + 4) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_0); 	//
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16); 						// (([col+1] - [col-1] + 4) >> 3) + [col+0]

			// Subtract the highpass correction
			odd_pi16 = _mm_subs_pi16(odd_pi16, high_pi16);
			odd_pi16 = _mm_adds_pi16(odd_pi16, overflowprotect_pi16);
			odd_pi16 = _mm_subs_pu16(odd_pi16, overflowprotect_pi16);
			odd_pi16 = _mm_srai_pi16(odd_pi16, 1);

			// Interleave the even and odd results
			out2_pi16 = _mm_unpacklo_pi16(even_pi16, odd_pi16);
			//out_pi16 = _mm_max_pi16(out_pi16, _mm_setzero_si64());

			// Scale the result to the full 16-bit range
			out2_pi16 = _mm_slli_pi16(out2_pi16, scale_shift);

			// Store the second four results
			*(outptr++) = out2_pi16;

			// The second four lowpass coefficients will be the current values
			low1_pi16 = low2_pi16;

			// The second four highpass coefficients will be the current values
			high1_pi16 = high2_pi16;
		}

		// Should have exited the loop with the column equal to the post processing column
		assert(column == post_column);

		// The fast processing loop is one column behind the actual column
		column++;

		// Process the rest of the columns up to the last column in the row
		colptr = (PIXEL16U *)outptr;

		for (; column < last_column; column++)
		{
			int32_t even = 0;		// Result of convolution with even filter
			int32_t odd = 0;		// Result of convolution with odd filter

			// Apply the even reconstruction filter to the lowpass band
			even += lowpass[column - 1];
			even -= lowpass[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += lowpass[column + 0];

			// Add the highpass correction
			even += highpass[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even <<= scale_shift;

			// Place the even result in the even column
			//even >>= _INVERSE_TEMPORAL_PRESCALE;
			*(colptr++) = SATURATE_16U(even);

			// Apply the odd reconstruction filter to the lowpass band
			odd -= lowpass[column - 1];
			odd += lowpass[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += lowpass[column + 0];

			// Subtract the highpass correction
			odd -= highpass[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd <<= scale_shift;

			// Place the odd result in the odd column
			//odd >>= _INVERSE_TEMPORAL_PRESCALE;
			*(colptr++) = SATURATE_16U(odd);
		}

		// Should have exited the loop at the column for right border processing
		assert(column == last_column);

		// Process the last two output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 5 * lowpass[column + 0];
		even += 4 * lowpass[column - 1];
		even -= 1 * lowpass[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even = DivideByShift(even, 1);

		// Scale the result to the full 16-bit range
		even <<= scale_shift;

		// Place the even result in the even column
		//even >>= _INVERSE_TEMPORAL_PRESCALE;
		*(colptr++) = SATURATE_16U(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * lowpass[column + 0];
		odd -=  4 * lowpass[column - 1];
		odd +=  1 * lowpass[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd = DivideByShift(odd, 1);

		// Scale the result to the full 16-bit range
		odd <<= scale_shift;

		// Place the odd result in the odd column
		//odd >>= _INVERSE_TEMPORAL_PRESCALE;
		*(colptr++) = SATURATE_16U(odd);

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		output += output_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}

#else

// Apply the inverse horizontal transform to reconstruct a strip of rows (new version using SSE2)
void InvertHorizontalStrip16sToRow16u(PIXEL *lowpass_band, int lowpass_pitch,
									  PIXEL *highpass_band, int highpass_pitch,
									  PIXEL16U *output, int output_pitch,
									  ROI roi, int precision)
{
	int height = roi.height;
	int width = roi.width;
	PIXEL *lowpass = lowpass_band;
	PIXEL *highpass = highpass_band;
	const int last_column = width - 1;

	// The fast loop processes eight columns per iteration
	const int column_step = 8;

	// The fast loop preloads eight columns of coefficients for the next iteration
	const int fast_loop_width = 2 * column_step;

	// Must terminate the fast loop when it reaches the post processing column
	const int fast_loop_column = width - (width % column_step) - fast_loop_width;
	const int post_column = fast_loop_column - (fast_loop_width % column_step);

	// Calculate the shift required to output 16-bit pixels
	int scale_shift = (16 - precision);

	//	int protection = 0x7fff - 2047;
	int protection = 0x7fff - (2 << precision) + 1;

	int row;

	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL);

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		__m128i low1_epi16;		// Lowpass coefficients
		__m128i low2_epi16;
		__m128i high1_epi16;	// Highpass coefficients
		__m128i high2_epi16;

		__m128i lsh1_epi16;		// Coefficients left shifted by one column
		__m128i rsh1_epi16;		// Coefficients right shifted by one column

		__m128i even_epi16;		// Result of convolution with even filter
		__m128i odd_epi16;		// Result of convolution with odd filter
		//__m128i temp_epi16;
		__m128i out1_epi16;		// First group of eight output values
		__m128i out2_epi16;		// Second group of eight output values

		__m128i half_epi16 = _mm_set1_epi16(4);		//DAN031604 4 to 6

		__m128i protection_epi16 = _mm_set1_epi16(protection);

		PIXEL16U *colptr;

		int low1;

		int32_t even;
		int32_t odd;

		// The prolog to the fast loop computes output points starting at the first column
		__m128i *outptr = (__m128i *)&output[0];

		// Start processing at the beginning of the row
		int column = 0;

		// Load the first eight lowpass coefficients
		//low1_epi16 = *((__m128i *)&lowpass[column]);
		low1_epi16 = _mm_load_si128((__m128i *)&lowpass[0]);

		// Load the first eight highpass coefficients
		//high1_epi16 = *((__m128i *)&highpass[column]);
		high1_epi16 = _mm_load_si128((__m128i *)&highpass[0]);

		// Process the first two output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * lowpass[column + 0];
		even -=  4 * lowpass[column + 1];
		even +=  1 * lowpass[column + 2];
		even += ROUNDING(even, 8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even = DivideByShift(even, 1);

		// Scale the result to the full 16-bit range
		//even <<= scale_shift;

		// Place the even result in the even column
		//even >>= _INVERSE_TEMPORAL_PRESCALE;
		//output[0] = SATURATE_16U(even);
		even <<= scale_shift;
		even = SATURATE_16U(even); //DAN20070813 -- fix for left edge of alpha channels.
		even >>= scale_shift;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * lowpass[column + 0];
		odd += 4 * lowpass[column + 1];
		odd -= 1 * lowpass[column + 2];
		odd += ROUNDING(odd, 8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd = DivideByShift(odd, 1);

		// Scale the result to the full 16-bit range
		//odd <<= scale_shift;

		// Place the odd result in the odd column
		//odd >>= _INVERSE_TEMPORAL_PRESCALE;
		//output[1] = SATURATE_16U(odd);
		odd <<= scale_shift;
		odd = SATURATE_16U(odd);//DAN20070813 -- fix for left edge of alpha channels.
		odd >>= scale_shift;

		// Save the even and odd output values for use in the prolog to the fast loop


		// Must have enough coefficients to use the fast loop
		if (fast_loop_width <= width)
		{
			// Compute the first group of eight output pairs (even and odd)

			// Preload the next eight lowpass coefficients
			//low1_epi16 = *((__m128i *)&lowpass[column]);
			low2_epi16 = _mm_load_si128((__m128i *)&lowpass[column_step]);

			// Preload the next eight highpass coefficients
			//high1_epi16 = *((__m128i *)&highpass[column]);
			high2_epi16 = _mm_load_si128((__m128i *)&highpass[column_step]);

			// Shift the coefficients by one column to the left and right
			lsh1_epi16 = _mm_slli_si128(low1_epi16, 2);				// [col-1]
			rsh1_epi16 = _mm_srli_si128(low1_epi16, 2);				// [col+1]

			// Insert the first coefficient from the next group into the last coefficient in this group
			rsh1_epi16 = _mm_insert_epi16(rsh1_epi16, _mm_extract_epi16(low2_epi16, 0), 7);

			// Apply the three point filter for the even output values
			even_epi16 = _mm_subs_epi16(lsh1_epi16, rsh1_epi16);	// [col-1] - [col+1]
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);  	// [col-1] - [col+1] + 4
			even_epi16 = _mm_srai_epi16(even_epi16, 3);				// ([col-1] - [col+1] + 4) >> 3
			even_epi16 = _mm_adds_epi16(even_epi16, low1_epi16); 	// (([col-1] - [col+1] + 4) >> 3) + [col+0]

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, protection_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, protection_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the three point filter for the odd output values
			odd_epi16 = _mm_subs_epi16(rsh1_epi16, lsh1_epi16);		// [col+1] - [col-1]
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);  	// [col+1] - [col-1] + 4
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);				// ([col+1] - [col-1] + 4) >> 3
			odd_epi16 = _mm_adds_epi16(odd_epi16, low1_epi16);		// (([col+1] - [col-1] + 4) >> 3) + [col+0]

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, protection_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, protection_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Insert the even and odd output values from the left border
			even_epi16 = _mm_insert_epi16(even_epi16, even, 0);
			odd_epi16 = _mm_insert_epi16(odd_epi16, odd, 0);

			// Interleave the even and odd results
			out1_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
			out2_epi16 = _mm_unpackhi_epi16(even_epi16, odd_epi16);

			// Scale the result to the full 16-bit range
			out1_epi16 = _mm_slli_epi16(out1_epi16, scale_shift);
			out2_epi16 = _mm_slli_epi16(out2_epi16, scale_shift);

			// Store the first eight even and odd pairs from the first eight coefficients
			_mm_storeu_si128(outptr++, out1_epi16);

			// Store the second eight even and odd pairs from the first eight coefficients
			_mm_storeu_si128(outptr++, out2_epi16);

			// Save the last coefficient from this group of coefficients
			low1 = _mm_extract_epi16(low1_epi16, 7);

			// The fast loop starts with the second group of coefficients
			low1_epi16 = low2_epi16;
			high1_epi16 = high2_epi16;

			// Done processing the first eight columns of lowpass and highpass coefficients
			column += column_step;
		}
		else
		{
			// Scale the even and odd output values to the full 16-bit range
			even <<= scale_shift;
			odd <<= scale_shift;

			// Store the even and odd output values from the left border
			output[0] = SATURATE_16U(even);
			output[1] = SATURATE_16U(odd);
		}


		// Process eight lowpass and highpass coefficients per iteration of the fast loop
		for (; column < post_column; column += column_step)
		{
			////__m128i mask_epi16;
			////__m128i lsb_epi16;
			////__m128i sign_epi16;
			//__m128i high_epi16;

			// Preload the next eight lowpass coefficients
			low2_epi16 = _mm_load_si128((__m128i *)&lowpass[column + column_step]);

			// Preload the next eight highpass coefficients
			high2_epi16 = _mm_load_si128((__m128i *)&highpass[column + column_step]);

			// Shift the coefficients by one column to the left and right
			lsh1_epi16 = _mm_slli_si128(low1_epi16, 2);				// [col-1]
			rsh1_epi16 = _mm_srli_si128(low1_epi16, 2);				// [col+1]

			// Fill the first coefficient with the last coefficient from the previous group
			lsh1_epi16 = _mm_insert_epi16(lsh1_epi16, low1, 0);

			// Fill the last coefficient with the first coefficient from the next group
			rsh1_epi16 = _mm_insert_epi16(rsh1_epi16, _mm_extract_epi16(low2_epi16, 0), 7);

			// Apply the three point filter for the even output values
			even_epi16 = _mm_subs_epi16(lsh1_epi16, rsh1_epi16);	// [col-1] - [col+1]
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);  	// [col-1] - [col+1] + 4
			even_epi16 = _mm_srai_epi16(even_epi16, 3);				// ([col-1] - [col+1] + 4) >> 3
			even_epi16 = _mm_adds_epi16(even_epi16, low1_epi16); 	// (([col-1] - [col+1] + 4) >> 3) + [col+0]

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, protection_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, protection_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the three point filter for the odd output values
			odd_epi16 = _mm_subs_epi16(rsh1_epi16, lsh1_epi16);		// [col+1] - [col-1]
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);  	// [col+1] - [col-1] + 4
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);				// ([col+1] - [col-1] + 4) >> 3
			odd_epi16 = _mm_adds_epi16(odd_epi16, low1_epi16);		// (([col+1] - [col-1] + 4) >> 3) + [col+0]

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, protection_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, protection_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the even and odd results
			out1_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
			out2_epi16 = _mm_unpackhi_epi16(even_epi16, odd_epi16);

			// Scale the result to the full 16-bit range
			out1_epi16 = _mm_slli_epi16(out1_epi16, scale_shift);
			out2_epi16 = _mm_slli_epi16(out2_epi16, scale_shift);

			// Store the first eight even and odd pairs from the first eight coefficients
			_mm_storeu_si128(outptr++, out1_epi16);

			// Store the second eight even and odd pairs from the first eight coefficients
			_mm_storeu_si128(outptr++, out2_epi16);

			// Save the last coefficient from this group of coefficients
			low1 = _mm_extract_epi16(low1_epi16, 7);

			// The next iteration of this loop starts with the second group of coefficients
			low1_epi16 = low2_epi16;
			high1_epi16 = high2_epi16;
		}

		// Should have exited the loop at the post processing column
		assert(column == post_column);

		// The next phase will store eight pairs of output values (the last pair is bad)
		colptr = (PIXEL16U *)outptr;

		// Process the next eight coefficients which have already been loaded

		// Shift the coefficients by one column to the left and right
		lsh1_epi16 = _mm_slli_si128(low1_epi16, 2);				// [col-1]
		rsh1_epi16 = _mm_srli_si128(low1_epi16, 2);				// [col+1]

		// Fill the first coefficient with the last coefficient from the previous group
		lsh1_epi16 = _mm_insert_epi16(lsh1_epi16, low1, 0);

		// Apply the three point filter for the even output values
		even_epi16 = _mm_subs_epi16(lsh1_epi16, rsh1_epi16);	// [col-1] - [col+1]
		even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);  	// [col-1] - [col+1] + 4
		even_epi16 = _mm_srai_epi16(even_epi16, 3);				// ([col-1] - [col+1] + 4) >> 3
		even_epi16 = _mm_adds_epi16(even_epi16, low1_epi16); 	// (([col-1] - [col+1] + 4) >> 3) + [col+0]

		// Add the highpass correction and divide by two
		even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
		even_epi16 = _mm_adds_epi16(even_epi16, protection_epi16);
		even_epi16 = _mm_subs_epu16(even_epi16, protection_epi16);
		even_epi16 = _mm_srai_epi16(even_epi16, 1);

		// Apply the three point filter for the odd output values
		odd_epi16 = _mm_subs_epi16(rsh1_epi16, lsh1_epi16);		// [col+1] - [col-1]
		odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);  	// [col+1] - [col-1] + 4
		odd_epi16 = _mm_srai_epi16(odd_epi16, 3);				// ([col+1] - [col-1] + 4) >> 3
		odd_epi16 = _mm_adds_epi16(odd_epi16, low1_epi16);		// (([col+1] - [col-1] + 4) >> 3) + [col+0]

		// Subtract the highpass correction and divide by two
		odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
		odd_epi16 = _mm_adds_epi16(odd_epi16, protection_epi16);
		odd_epi16 = _mm_subs_epu16(odd_epi16, protection_epi16);
		odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

		// Interleave the even and odd results
		out1_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);
		out2_epi16 = _mm_unpackhi_epi16(even_epi16, odd_epi16);

		// Scale the result to the full 16-bit range
		out1_epi16 = _mm_slli_epi16(out1_epi16, scale_shift);
		out2_epi16 = _mm_slli_epi16(out2_epi16, scale_shift);

		// Store the first eight even and odd pairs from the first eight coefficients
		_mm_storeu_si128(outptr++, out1_epi16);

		// Store the second eight even and odd pairs from the first eight coefficients
		_mm_storeu_si128(outptr++, out2_epi16);

		// Have processed only seven columns because the last column is bad
		column += 7;
		colptr += (2 * 7);

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column++)
		{
			even = 0;
			odd = 0;

			// Apply the even reconstruction filter to the lowpass band
			even += lowpass[column - 1];
			even -= lowpass[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += lowpass[column + 0];

			// Add the highpass correction
			even += highpass[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even <<= scale_shift;

			// Place the even result in the even column
			//even >>= _INVERSE_TEMPORAL_PRESCALE;
			*(colptr++) = SATURATE_16U(even);

			// Apply the odd reconstruction filter to the lowpass band
			odd -= lowpass[column - 1];
			odd += lowpass[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += lowpass[column + 0];

			// Subtract the highpass correction
			odd -= highpass[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd <<= scale_shift;

			// Place the odd result in the odd column
			//odd >>= _INVERSE_TEMPORAL_PRESCALE;
			*(colptr++) = SATURATE_16U(odd);
		}

		// Should have exited the loop at the column for right border processing
		assert(column == last_column);

		// Process the last two output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 5 * lowpass[column + 0];
		even += 4 * lowpass[column - 1];
		even -= 1 * lowpass[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even = DivideByShift(even, 1);

		// Scale the result to the full 16-bit range
		even <<= scale_shift;

		// Place the even result in the even column
		//even >>= _INVERSE_TEMPORAL_PRESCALE;
		*(colptr++) = SATURATE_16U(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * lowpass[column + 0];
		odd -=  4 * lowpass[column - 1];
		odd +=  1 * lowpass[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd = DivideByShift(odd, 1);

		// Scale the result to the full 16-bit range
		odd <<= scale_shift;

		// Place the odd result in the odd column
		//odd >>= _INVERSE_TEMPORAL_PRESCALE;
		*(colptr++) = SATURATE_16U(odd);

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		output += output_pitch;
	}
}

#endif



// Apply the inverse horizontal transform to reconstruct a strip of rows (new version using SSE2)
void InvertHorizontalBypassStrip16sToRow16u(PIXEL *lowpass_band, int lowpass_pitch,
									  PIXEL16U *output, int output_pitch,
									  ROI roi, int precision)
{
	int height = roi.height;
	int width = roi.width<<1;
	PIXEL *lowpass = lowpass_band;

	// Calculate the shift required to output 16-bit pixels
	int scale_shift = (16 - precision)-1;

	//	int protection = 0x7fff - 2047;
	int protection = 0x7fff - (2 << precision) + 1;

	int row;

	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL);

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{	
		PIXEL16U *colptr = output;
		__m128i low_epi16;
		int column = 0;
		int width8 = (width>>3)<<3;
		__m128i protection_epi16 = _mm_set1_epi16(protection);
		// Process the rest of the columns up to the last column in the row

		if(ISALIGNED16(lowpass) && ISALIGNED16(colptr))
		{
			for (; column < width8; column+=8)
			{
				low_epi16 = _mm_load_si128((__m128i *)&lowpass[column]);
				low_epi16 = _mm_adds_epi16(low_epi16, protection_epi16);
				low_epi16 = _mm_subs_epu16(low_epi16, protection_epi16);
				low_epi16 = _mm_slli_epi16(low_epi16, scale_shift);
				_mm_store_si128((__m128i *)&colptr[column], low_epi16);
			}
		}
		else
		{
			for (; column < width8; column+=8)
			{
				low_epi16 = _mm_loadu_si128((__m128i *)&lowpass[column]);
				low_epi16 = _mm_adds_epi16(low_epi16, protection_epi16);
				low_epi16 = _mm_subs_epu16(low_epi16, protection_epi16);
				low_epi16 = _mm_slli_epi16(low_epi16, scale_shift);
				_mm_storeu_si128((__m128i *)&colptr[column], low_epi16);
			}
		}

		for (; column < width; column++)
		{
			colptr[column] = lowpass[column] << scale_shift;
		}

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		output += output_pitch;
	}
}


#if 0
// Apply the inverse horizontal transform to reconstruct a strip of rows
void InvertHorizontalStripRGB444ToB64A(PIXEL *lowpass_band, int lowpass_pitch,
									   PIXEL *highpass_band, int highpass_pitch,
									   PIXEL16U *output, int output_pitch,
									   ROI roi, int precision)
{
	int height = roi.height;
	int width = roi.width;
	PIXEL *lowpass = lowpass_band;
	PIXEL *highpass = highpass_band;
	const int column_step = 4;
	const int last_column = width - 1;
	int post_column = last_column - (last_column % column_step);
	int row;

	// Calculate the shift required to output 16-bit pixels
	int scale_shift = (16 - precision);
//	int protection = 0x7fff - 2047;
	int protection = 0x7fff - (2<<precision) + 1;

	// Convert the pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	output_pitch /= sizeof(PIXEL16U);

	// Adjust the end of the fast loop if necessary
	if (post_column == last_column)
		post_column -= column_step;

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
		__m64 low1_pi16;	// Lowpass coefficients
		__m64 low2_pi16;
		__m64 high1_pi16;	// Highpass coefficients
		__m64 high2_pi16;

		__m64 overflowprotect_pi16 = _mm_set1_pi16(protection);

		PIXEL16U *colptr;

		int32_t even;
		int32_t odd;

		// The fast loop computes output points starting at the third column
		__m64 *outptr = (__m64 *)&output[2];

		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * lowpass[column + 0];
		even -=  4 * lowpass[column + 1];
		even +=  1 * lowpass[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even = DivideByShift(even, 1);

		// Scale the result to the full 16-bit range
		even <<= scale_shift;

		// Place the even result in the even column
		//even >>= _INVERSE_TEMPORAL_PRESCALE;
		output[0] = SATURATE_16U(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * lowpass[column + 0];
		odd += 4 * lowpass[column + 1];
		odd -= 1 * lowpass[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd = DivideByShift(odd, 1);

		// Scale the result to the full 16-bit range
		odd <<= scale_shift;

		// Place the odd result in the odd column
		//odd >>= _INVERSE_TEMPORAL_PRESCALE;
		output[1] = SATURATE_16U(odd);

		// Preload the first four lowpass coefficients
		low1_pi16 = *((__m64 *)&lowpass[column]);

		// Preload the first four highpass coefficients
		high1_pi16 = *((__m64 *)&highpass[column]);

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m64 even_pi16;	// Result of convolution with even filter
			__m64 odd_pi16;		// Result of convolution with odd filter
			__m64 temp_pi16;
			__m64 out1_pi16;	// Reconstructed data - first set of four
			__m64 out2_pi16;	// Reconstructed data - second set of four
			__m64 mask_pi16;
			__m64 lsb_pi16;
			__m64 sign_pi16;
			__m64 high_pi16;
			__m64 half_pi16 = _mm_set1_pi16(4); //DAN031604 4 to 6

			// Right shift to reduce the output pixel size to one byte
			//__m64 scale_si64 = _mm_cvtsi32_si64(scale_shift);

			// Preload the next four lowpass coefficients
			low2_pi16 = *((__m64 *)&lowpass[column+4]);

#define COLUMN_0		_MM_SHUFFLE(0, 3, 2, 1)
#define COLUMN_PLUS_1	_MM_SHUFFLE(1, 0, 3, 2)

			/***** Compute the first two even and two odd output points *****/

			// Apply the even reconstruction filter to the lowpass band
		/*	even_pi16 = low1_pi16;  //- 1
			temp_pi16 = _mm_slli_pi16(low1_pi16, 3);  // temp = low[0]<<3
			temp_pi16 = _mm_shuffle_pi16(temp_pi16, _MM_SHUFFLE(0, 3, 2, 1)); // + 0
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16);
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2)); // + 1
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16);

			#if 1
			// Apply the rounding adjustment
			even_pi16 = _mm_adds_pi16(even_pi16, _mm_set1_pi16(4));
			#endif
			// Divide by eight
			even_pi16 = _mm_srai_pi16(even_pi16, 3);
	*/
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_PLUS_1); 			// [col+1]
			even_pi16 = _mm_subs_pi16(low1_pi16, temp_pi16);  					// [col-1] - [col+1]
			even_pi16 = _mm_adds_pi16(even_pi16, half_pi16);  					// [col-1] - [col+1] + 4
			even_pi16 = _mm_srai_pi16(even_pi16, 3);		  					// ([col-1] - [col+1] + 4) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_0); 	//
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16); 					// (([col-1] - [col+1] + 4) >> 3) + [col+0]

			// Shift the highpass correction by one column
			high1_pi16 = _mm_srli_si64(high1_pi16, 16);

			// Prescale for 8bit output - DAN 4/5/02
			high_pi16 = high1_pi16;

			// Add the highpass correction
			even_pi16 = _mm_adds_pi16(even_pi16, high_pi16);
			even_pi16 = _mm_adds_pi16(even_pi16, overflowprotect_pi16);
			even_pi16 = _mm_subs_pu16(even_pi16, overflowprotect_pi16);
			even_pi16 = _mm_srai_pi16(even_pi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
	/*		odd_pi16 = _mm_slli_pi16(low1_pi16, 3);
			odd_pi16 = _mm_shuffle_pi16(odd_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16);
			odd_pi16 = _mm_subs_pi16(odd_pi16, low1_pi16);

			#if 1
			// Apply the rounding adjustment
			odd_pi16 = _mm_adds_pi16(odd_pi16, _mm_set1_pi16(4));
			#endif
			// Divide by eight
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);
	*/

			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_PLUS_1); 			// [col+1]
			odd_pi16 = _mm_subs_pi16(temp_pi16, low1_pi16);  					// [col+1] - [col-1]
			odd_pi16 = _mm_adds_pi16(odd_pi16, half_pi16);  					// [col+1] - [col-1] + 4
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);		  						// ([col+1] - [col-1] + 4) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_0); 	//
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16); 						// (([col+1] - [col-1] + 4) >> 3) + [col+0]

			// Subtract the highpass correction
			odd_pi16 = _mm_subs_pi16(odd_pi16, high_pi16);
			odd_pi16 = _mm_adds_pi16(odd_pi16, overflowprotect_pi16);
			odd_pi16 = _mm_subs_pu16(odd_pi16, overflowprotect_pi16);
			odd_pi16 = _mm_srai_pi16(odd_pi16, 1);

			// Interleave the even and odd results
			out1_pi16 = _mm_unpacklo_pi16(even_pi16, odd_pi16);
			//out_pi16 = _mm_max_pi16(out_pi16, _mm_setzero_si64());

			// Scale the result to the full 16-bit range
			out1_pi16 = _mm_slli_pi16(out1_pi16, scale_shift);

			// Store the first four results
			*(outptr++) = out1_pi16;


			/***** Compute the second two even and two odd output points *****/

			// Preload the highpass correction
			high2_pi16 = *((__m64 *)&highpass[column+4]);

			// Shift in the new pixels for the next stage of the loop
			low1_pi16 = _mm_srli_si64(low1_pi16, 32);
			temp_pi16 = _mm_slli_si64(low2_pi16, 32);
			low1_pi16 = _mm_or_si64(low1_pi16, temp_pi16);

			// Apply the even reconstruction filter to the lowpass band
	/*		even_pi16 = low1_pi16;
			temp_pi16 = _mm_slli_pi16(low1_pi16, 3);
			temp_pi16 = _mm_shuffle_pi16(temp_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16);
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			even_pi16 = _mm_subs_pi16(even_pi16, temp_pi16);

			#if 1
			// Apply the rounding adjustment
			even_pi16 = _mm_adds_pi16(even_pi16, _mm_set1_pi16(4));
			#endif
			// Divide by eight
			even_pi16 = _mm_srai_pi16(even_pi16, 3);
	*/
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_PLUS_1); 			// [col+1]
			even_pi16 = _mm_subs_pi16(low1_pi16, temp_pi16);  					// [col-1] - [col+1]
			even_pi16 = _mm_adds_pi16(even_pi16, half_pi16);  					// [col-1] - [col+1] + 4
			even_pi16 = _mm_srai_pi16(even_pi16, 3);		  					// ([col-1] - [col+1] + 4) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_0); 	//
			even_pi16 = _mm_adds_pi16(even_pi16, temp_pi16); 					// (([col-1] - [col+1] + 4) >> 3) + [col+0]


			// Shift in the next two highpass coefficients
			high1_pi16 = _mm_srli_si64(high1_pi16, 2*16);
			high1_pi16 = _mm_or_si64(high1_pi16, _mm_slli_si64(high2_pi16, 16));

			// Prescale for 8bit output - DAN 4/5/02
			high_pi16 = high1_pi16;

			// Add the highpass correction
			even_pi16 = _mm_adds_pi16(even_pi16, high_pi16);
			even_pi16 = _mm_adds_pi16(even_pi16, overflowprotect_pi16);
			even_pi16 = _mm_subs_pu16(even_pi16, overflowprotect_pi16);
			even_pi16 = _mm_srai_pi16(even_pi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
	/*		odd_pi16 = _mm_slli_pi16(low1_pi16, 3);
			odd_pi16 = _mm_shuffle_pi16(odd_pi16, _MM_SHUFFLE(0, 3, 2, 1));
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, _MM_SHUFFLE(1, 0, 3, 2));
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16);
			odd_pi16 = _mm_subs_pi16(odd_pi16, low1_pi16);

			#if 1
			// Apply the rounding adjustment
			odd_pi16 = _mm_adds_pi16(odd_pi16, _mm_set1_pi16(4));
			#endif
			// Divide by eight
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);
	*/
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_PLUS_1); 			// [col+1]
			odd_pi16 = _mm_subs_pi16(temp_pi16, low1_pi16);  					// [col+1] - [col-1]
			odd_pi16 = _mm_adds_pi16(odd_pi16, half_pi16);  					// [col+1] - [col-1] + 4
			odd_pi16 = _mm_srai_pi16(odd_pi16, 3);		  						// ([col+1] - [col-1] + 4) >> 3
			temp_pi16 = _mm_shuffle_pi16(low1_pi16, COLUMN_0); 	//
			odd_pi16 = _mm_adds_pi16(odd_pi16, temp_pi16); 						// (([col+1] - [col-1] + 4) >> 3) + [col+0]

			// Subtract the highpass correction
			odd_pi16 = _mm_subs_pi16(odd_pi16, high_pi16);
			odd_pi16 = _mm_adds_pi16(odd_pi16, overflowprotect_pi16);
			odd_pi16 = _mm_subs_pu16(odd_pi16, overflowprotect_pi16);
			odd_pi16 = _mm_srai_pi16(odd_pi16, 1);

			// Interleave the even and odd results
			out2_pi16 = _mm_unpacklo_pi16(even_pi16, odd_pi16);
			//out_pi16 = _mm_max_pi16(out_pi16, _mm_setzero_si64());

			// Scale the result to the full 16-bit range
			out2_pi16 = _mm_slli_pi16(out2_pi16, scale_shift);

			// Store the second four results
			*(outptr++) = out2_pi16;

			// The second four lowpass coefficients will be the current values
			low1_pi16 = low2_pi16;

			// The second four highpass coefficients will be the current values
			high1_pi16 = high2_pi16;
		}

		// Should have exited the loop with the column equal to the post processing column
		assert(column == post_column);

		// The fast processing loop is one column behind the actual column
		column++;

		// Process the rest of the columns up to the last column in the row
		colptr = (PIXEL16U *)outptr;

		for (; column < last_column; column++)
		{
			int32_t even = 0;		// Result of convolution with even filter
			int32_t odd = 0;		// Result of convolution with odd filter

			// Apply the even reconstruction filter to the lowpass band
			even += lowpass[column - 1];
			even -= lowpass[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += lowpass[column + 0];

			// Add the highpass correction
			even += highpass[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even <<= scale_shift;

			// Place the even result in the even column
			//even >>= _INVERSE_TEMPORAL_PRESCALE;
			*(colptr++) = SATURATE_16U(even);

			// Apply the odd reconstruction filter to the lowpass band
			odd -= lowpass[column - 1];
			odd += lowpass[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += lowpass[column + 0];

			// Subtract the highpass correction
			odd -= highpass[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd <<= scale_shift;

			// Place the odd result in the odd column
			//odd >>= _INVERSE_TEMPORAL_PRESCALE;
			*(colptr++) = SATURATE_16U(odd);
		}

		// Should have exited the loop at the column for right border processing
		assert(column == last_column);

		// Process the last two output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 5 * lowpass[column + 0];
		even += 4 * lowpass[column - 1];
		even -= 1 * lowpass[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += highpass[column];
		even = DivideByShift(even, 1);

		// Scale the result to the full 16-bit range
		even <<= scale_shift;

		// Place the even result in the even column
		//even >>= _INVERSE_TEMPORAL_PRESCALE;
		*(colptr++) = SATURATE_16U(even);

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * lowpass[column + 0];
		odd -=  4 * lowpass[column - 1];
		odd +=  1 * lowpass[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= highpass[column];
		odd = DivideByShift(odd, 1);

		// Scale the result to the full 16-bit range
		odd <<= scale_shift;

		// Place the odd result in the odd column
		//odd >>= _INVERSE_TEMPORAL_PRESCALE;
		*(colptr++) = SATURATE_16U(odd);

		// Advance to the next row of coefficients and output values
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		output += output_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}
#endif



void
InvertHorizontalStrip16sToRow16uPlanar(HorizontalFilterParams)
{
	int i;
	int channels = decoder->codec.num_channels;
	int stripwidthC = roi.width/2;

	if(false == ALPHAOUTPUT(decoder->frame.format) && decoder->codec.encoded_format != ENCODED_FORMAT_BAYER)
		channels = 3;

	if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
	{
		roi.width >>= 1;
		stripwidthC >>= 1;
	}

	for(i=0; i<channels; i++)
	{
		if(i > 0 && decoder->codec.encoded_format == ENCODED_FORMAT_YUV_422)
			roi.width = stripwidthC; //DAN20080730 -- added to allow this routine to handle 4:2:2 sources.

		if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
		{
			InvertHorizontalBypassStrip16sToRow16u(
				lowpass_band[i], lowpass_pitch[i],
				(PIXEL16U*)output_image, output_pitch, roi,
				precision);
		}
		else
		{
			InvertHorizontalStrip16sToRow16u(
				lowpass_band[i], lowpass_pitch[i],
				highpass_band[i], highpass_pitch[i],
				(PIXEL16U*)output_image, output_pitch, roi,
				precision);
		}

		//output_buffer += output_pitch/channels;  //DAN20080702 -- this only works for 4:4:4:(4) and bayer sources.
		//output_buffer += lowpass_pitch[i];
		output_image += roi.width * 4;
	}
}



// Apply the inverse horizontal transform to reconstruct a strip of rows (new version using SSE2)
void
InvertHorizontalStrip16sYUVtoRGB(HorizontalFilterParams)
{
	int i;
	int channels = decoder->codec.num_channels;
	//uint8_t *chroma_buffer = output_image;
	uint8_t *plane_array[TRANSFORM_MAX_CHANNELS] = {0};
	int plane_pitch[TRANSFORM_MAX_CHANNELS] = {0};
	//unsigned short scanline[4096*6];
	uint8_t *sptr, *sptr2;
	int local_pitch = roi.width * 2 * 2 * 2; // 422 yuv 16-bit per channel

	void *scratch = NULL;
	size_t scratchsize = 0;

	scratch = decoder->threads_buffer[thread_index];
	scratchsize = decoder->threads_buffer_size;
	if(((int)scratchsize) < local_pitch)
	{
		assert(0);
		return;
	}

	//sptr = (uint8_t *)&scanline[0];
	sptr = scratch;
	sptr = sptr2 = (uint8_t *)((((uintptr_t)sptr)+15) & ~0xf);

	for(i=0; i<channels; i++)
	{
		int channel_pitch;
		ROI newstrip = roi;


		if(i>0)
		{
			newstrip.width >>= 1;
		}
		channel_pitch = newstrip.width*2*2;

		InvertHorizontalStrip16sToRow16u(
			lowpass_band[i], lowpass_pitch[i],
			highpass_band[i], highpass_pitch[i],
			(PIXEL16U*)sptr2, channel_pitch, newstrip,
			precision);

		plane_array[i] = (uint8_t *)sptr2;
		plane_pitch[i] = channel_pitch;

		if(i == 0)
			sptr2 += channel_pitch*2;
		else
			sptr2 += channel_pitch*2;
	}

	{
		ROI newstrip;

		newstrip.width = roi.width*2;
		newstrip.height = roi.height;
		ConvertRow16uToDitheredBuffer(decoder, plane_array, plane_pitch, newstrip,
								   output_image, output_pitch, newstrip.width*2,
								   format, decoder->frame.colorspace);
	}
}


// Apply the inverse horizontal transform to reconstruct a strip of rows (new version using SSE2)
void
InvertHorizontalStrip16sThruActiveMetadata(HorizontalFilterParams)
{
	int i;
	int channels = decoder->codec.num_channels;
	//uint8_t *chroma_buffer = output_image;
	uint8_t *plane_array[TRANSFORM_MAX_CHANNELS] = {0};
	int plane_pitch[TRANSFORM_MAX_CHANNELS] = {0};
	unsigned short *scanline2;
	unsigned short *scanline3;
	unsigned short *scanline4;

	uint8_t *sptr, *sptr2;
	int local_pitch;

	uint8_t *scratch = decoder->threads_buffer[thread_index];
	size_t scratchsize = decoder->threads_buffer_size;

	scanline2 = (unsigned short *)scratch;
	scanline3 = (unsigned short *)(scratch + ((scratchsize/3)&0xffffff00));
	scanline4 = (unsigned short *)(scratch + ((scratchsize*2/3)&0xffffff00));


	sptr = (uint8_t *)&scanline2[0];
	sptr = sptr2 = (uint8_t *)((((uintptr_t)sptr)+15) & ~0xf);

	if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
	{
		roi.width >>= 1;
	}
	
	local_pitch = roi.width * 2 * 2 * 2; // 422 yuv 16-bit per channel

	for(i=0; i<channels; i++)
	{
		ROI newstrip = roi;

		if(i>0)
		{
			newstrip.width >>= 1;
		}

		if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
		{
			InvertHorizontalBypassStrip16sToRow16u(
				lowpass_band[i], lowpass_pitch[i],
				(PIXEL16U*)sptr2, local_pitch, newstrip,
				precision);
		}
		else
		{
			InvertHorizontalStrip16sToRow16u(
				lowpass_band[i], lowpass_pitch[i],
				highpass_band[i], highpass_pitch[i],
				(PIXEL16U*)sptr2, local_pitch, newstrip,
				precision);
		}

		plane_array[i] = (uint8_t *)sptr2;
		plane_pitch[i] = local_pitch;

		sptr2 += newstrip.width*2*2;
	}

	{
		ROI newstrip;
		uint8_t *sptr3, *sptr4;

		sptr = (uint8_t *)&scanline3[0];
		sptr = sptr3 = (uint8_t *)((((uintptr_t)sptr)+15) & ~0xf);
		sptr = (uint8_t *)&scanline4[0];
		sptr = sptr4 = (uint8_t *)((((uintptr_t)sptr)+15) & ~0xf);
		
		newstrip.width = roi.width*2;
		newstrip.height = 1;
		{
			unsigned char *output = (unsigned char *)output_image;
			int i;
			int height = roi.height;

			for(i=0; i<height; i++)
			{
				int whitebitdepth = 16;
				int flags = 0;
				int colorspace = decoder->frame.colorspace & (8|3); // VSRGB is done in cube

				ConvertYUVRow16uToBGRA64(plane_array, plane_pitch, newstrip,
					(unsigned char *)sptr3, newstrip.width, output_pitch,
					COLOR_FORMAT_RGB_8PIXEL_PLANAR, colorspace, &whitebitdepth, &flags);

				//flags = (ACTIVEMETADATA_PRESATURATED|ACTIVEMETADATA_SRC_8PIXEL_PLANAR);

				//TODO: Get the scanline number
				sptr = sptr3;
				if(decoder->apply_color_active_metadata)
					sptr = (uint8_t *)ApplyActiveMetaData(decoder, newstrip.width, 1, -1,
						(uint32_t *)sptr3, (uint32_t *)sptr4,
						decoder->frame.format, &whitebitdepth, &flags);					
				
				if(decoder->frame.colorspace & COLOR_SPACE_VS_RGB)
				{
					ConvertCGRGBtoVSRGB((PIXEL *)sptr, newstrip.width, whitebitdepth, flags);
				}

				ConvertLinesToOutput(decoder, newstrip.width, 1, 1, (PIXEL16U *)sptr,
					output, output_pitch, decoder->frame.format, whitebitdepth, flags);

				plane_array[0] += plane_pitch[0];
				plane_array[1] += plane_pitch[1];
				plane_array[2] += plane_pitch[2];

				output += output_pitch;
			}
		}
	}
}

