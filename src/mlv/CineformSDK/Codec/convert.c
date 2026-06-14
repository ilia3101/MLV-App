/*! @file convert.c

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
//#include <stdint.h>
#include "stdint.h"			// Use a local copy until this file is available on Windows

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

#include <memory.h>

#include "convert.h"
#include "filter.h"			// Declarations of filter routines
#include "codec.h"
#include "quantize.h"
#include "image.h"
#include "decoder.h"
#include "bayer.h"

#include "swap.h"

#if __APPLE__
#include "../Common/macdefs.h"
#else
#ifndef ZeroMemory
#define ZeroMemory(p,s)		memset(p,0,s)
#endif
#ifndef CopyMemory
#define CopyMemory(p,q,s)	memcpy(p,q,s)
#endif
#endif

// Performance measurements
#if _TIMING
extern TIMER tk_convert;
#endif


#define CHROMA422to444 1


int Timecode2frames(char *tc, int rate)
{
	int frms = 0,hr,mn,sc,fr,mult = 1;
	int ret;

	if(rate == 0)  rate = 24;
	if(rate == 23) rate = 24;
	if(rate == 29) rate = 30;
	if(rate == 50) rate = 50, mult = 2;
	if(rate == 59) rate = 60, mult = 2;

	if (tc)
	{
#ifdef _WIN32
		ret = sscanf_s(tc, "%02d:%02d:%02d:%02d", &hr, &mn, &sc, &fr);
#else
		ret = sscanf(tc, "%02d:%02d:%02d:%02d", &hr, &mn, &sc, &fr);
#endif		
		if (4 == ret)
		{
			frms = fr * mult;
			frms += sc * rate;
			frms += mn * rate * 60;
			frms += hr * rate * 60 * 60;
		}
	}

	return frms;
}


void Convert8sTo16s(PIXEL8S *input, int input_pitch, PIXEL16S *output, int output_pitch, ROI roi)
{
	int row, column;

	input_pitch /= sizeof(PIXEL8S);
	output_pitch /= sizeof(PIXEL16S);

	for (row = 0; row < roi.height; row++)
	{
		for (column = 0; column < roi.width; column++) {
			output[column] = input[column];
		}

		input += input_pitch;
		output += output_pitch;
	}
}

void Convert16sTo8u(PIXEL16S *input, int input_pitch, PIXEL8U *output, int output_pitch, ROI roi)
{
	int row, column;

	input_pitch /= sizeof(PIXEL16S);
	output_pitch /= sizeof(PIXEL8U);

	for (row = 0; row < roi.height; row++)
	{
		for (column = 0; column < roi.width; column++) {
			output[column] = SATURATE_8U(input[column]);
		}

		input += input_pitch;
		output += output_pitch;
	}
}

void Copy16sTo16s(PIXEL16S *input, int input_pitch, PIXEL16S *output, int output_pitch, ROI roi)
{
	int row, column;

	input_pitch /= sizeof(PIXEL16S);
	output_pitch /= sizeof(PIXEL16S);

	for (row = 0; row < roi.height; row++)
	{
		for (column = 0; column < roi.width; column++) {
			output[column] = input[column];
		}

		input += input_pitch;
		output += output_pitch;
	}
}

void Convert16sTo8s(PIXEL16S *input, int input_pitch, PIXEL8S *output, int output_pitch, ROI roi)
{
	int row, column;

	input_pitch /= sizeof(PIXEL16S);
	output_pitch /= sizeof(PIXEL8S);

	for (row = 0; row < roi.height; row++)
	{
		for (column = 0; column < roi.width; column++) {
			output[column] = SATURATE_8S(input[column]);
		}

		input += input_pitch;
		output += output_pitch;
	}
}

void ConvertRow16sTo8s(PIXEL16S *input, PIXEL8S *output, int length)
{
	int i;

	for (i = 0; i < length; i++) {
		output[i] = SATURATE_8S(input[i]);
	}
}

#if _DECODE_FRAME_8U

#if (_USE_YCBCR == 0) || (_ENABLE_GAMMA_CORRECTION == 1)
#error The compile-time switches are not set correctly
#endif

void ConvertYUYVRowToRGB(uint8_t *input, uint8_t *output, int length, int format, int colorspace, int precision)
{
	uint8_t *output_ptr = output;
	// Start processing at the first column
	int column = 0;

#if (0 && DEBUG)
	static uint8_t test_input[] = {
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
		0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C, 0x1A, 0x7B, 0x1A, 0x9C,
	};
#endif
	//if(colorspace & COLOR_SPACE_CG_601)
	int y_offset = 16; // not VIDEO_RGB & not YUV709
	int ymult = 128*149;	//7bit 1.164
	int r_vmult = 204;		//7bit 1.596
	int g_vmult = 208;		//8bit 0.813
	int g_umult = 100;		//8bit 0.391
	int b_umult = 129;		//6bit 2.018
	int saturate = 1;
	int upconvert422to444 = 0;

	if(colorspace & COLOR_SPACE_422_TO_444)
	{
		upconvert422to444 = 1;
	}

	switch(colorspace & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:
		y_offset = 16;		// not VIDEO_RGB & not YUV709
		ymult = 128*149;	//7bit 1.164
		r_vmult = 204;		//7bit 1.596
		g_vmult = 208;		//8bit 0.813
		g_umult = 100;		//8bit 0.391
		b_umult = 129;		//6bit 2.018
		saturate = 1;
		break;

	default: assert(0);
	case COLOR_SPACE_CG_709:
		y_offset = 16;
		ymult = 128*149;	//7bit 1.164
		r_vmult = 230;		//7bit 1.793
		g_vmult = 137;		//8bit 0.534
		g_umult = 55;		//8bit 0.213
		b_umult = 135;		//6bit 2.115
		saturate = 1;
		break;

	case COLOR_SPACE_VS_601:
		y_offset = 0;
		ymult = 128*128;	//7bit 1.0
		r_vmult = 175;		//7bit 1.371
		g_vmult = 179;		//8bit 0.698
		g_umult = 86;		//8bit 0.336
		b_umult = 111;		//6bit 1.732
		saturate = 0;
		break;

	case COLOR_SPACE_VS_709:
		y_offset = 0;
		ymult = 128*128;	//7bit 1.0
		r_vmult = 197;		//7bit 1.540
		g_vmult = 118;		//8bit 0.459
		g_umult = 47;		//8bit 0.183
		b_umult = 116;		//6bit 1.816
		saturate = 0;
		break;
	}


	// Convert the row length from pixels to bytes
	length *= 2;

	// Only 24 and 32 bit true color RGB formats are supported
	//assert(format == COLOR_FORMAT_RGB24 || format == COLOR_FORMAT_RGB32);

	// Only 24 bit true color RGB has been tested (but RGBA has been coded)
//	assert(format == COLOR_FORMAT_RGB24);

	// Force RGB32 format for debugging
	//format = COLOR_FORMAT_RGB32;

	// Is the output format RGB24?
	if(precision == 8) // source data is 8-bit YUYV data
	{
		if (format == COLOR_FORMAT_RGB24)
		{

	#if (0 && XMMOPT) //DANREMOVED

			int column_step = 16;
			int post_column = length - (length % column_step);
			__m64 *YUV_ptr = (__m64 *)input;
			//__m64 *YUV_ptr = (__m64 *)test_input;
			__m64 *RGB_ptr = (__m64 *)output;

			// Check that the post processing column is a multiple of the column step
			assert((post_column % column_step) == 0);

			// Convert the YUV to RGB24 in groups of eight pixels
			for (; column < post_column; column += column_step)
			{
				__m64 R1, G1, B1;
				__m64 R2, G2, B2;
				__m64 R_pi8, G_pi8, B_pi8;
				//__m64 YUV;
				__m64 temp;
				//__m64 temp2;
				__m64 RGB;

				__m64 Y;
				__m64 U;
				__m64 V;
				__m64 UV;

				__m64 RG;
				__m64 BZ;
				__m64 RGBZ;

				__m64 rounding_pi16 = _mm_set1_pi16(32); // 6bit half pt.

				// Load the first eight YCbCr values
				__m64 YUV_pi8 = *(YUV_ptr++);

	#if STRICT_SATURATE
				if(saturate)
				{
					// Perform strict saturation on YUV if required
					YUV_pi8 = _mm_subs_pu8(YUV_pi8, _mm_set1_pi8(16));
					YUV_pi8 = _mm_adds_pu8(YUV_pi8, _mm_setr_pi8(36, 31, 36, 31, 36, 31, 36, 31));
					YUV_pi8 = _mm_subs_pu8(YUV_pi8, _mm_setr_pi8(20, 15, 20, 15, 20, 15, 20, 15));
				}
	#endif

				/***** Calculate the first four RGB values *****/

				// Unpack the first four luma values
				Y = _mm_slli_pi16(YUV_pi8, 8);
				Y = _mm_srli_pi16(Y, 8);

				// Unpack the first four chroma values
				UV = _mm_srli_pi16(YUV_pi8, 8);

				// Preload the next eight YCbCr values
				YUV_pi8 = *(YUV_ptr++);

				// Shuffle the chroma values into halfwords
				UV = _mm_shuffle_pi16(UV, _MM_SHUFFLE(3, 1, 2, 0));

				// Duplicate the chroma values
				U = _mm_unpacklo_pi16(UV, UV);
				V = _mm_unpackhi_pi16(UV, UV);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 u1a_pi16;
					__m64 v1a_pi16;
					int extracted_u;
					int extracted_v;
					uint8_t *byuv = (uint8_t *)YUV_ptr;

					extracted_u = byuv[1-8];
					extracted_v = byuv[3-8];

					u1a_pi16 = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 1));
					v1a_pi16 = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 1));

					u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
					v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

					U = _mm_adds_pu16(U, u1a_pi16);
					V = _mm_adds_pu16(V, v1a_pi16);


					U = _mm_srai_pi16(U, 1);
					V = _mm_srai_pi16(V, 1);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7);			// This code fixed overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				temp = _mm_adds_pi16(temp, rounding_pi16);
				R1 = _mm_adds_pi16(Y, temp);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				temp = _mm_adds_pi16(temp, rounding_pi16);
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_adds_pi16(temp, rounding_pi16);
				B1 = _mm_adds_pi16(Y, temp);
				B1 = _mm_srai_pi16(B1, 6);


				/***** Calculate the second four RGB values *****/

	#if STRICT_SATURATE
				if(saturate)
				{
					// Perform strict saturation on YUV if required
					YUV_pi8 = _mm_subs_pu8(YUV_pi8, _mm_set1_pi8(16));
					YUV_pi8 = _mm_adds_pu8(YUV_pi8, _mm_setr_pi8(36, 31, 36, 31, 36, 31, 36, 31));
					YUV_pi8 = _mm_subs_pu8(YUV_pi8, _mm_setr_pi8(20, 15, 20, 15, 20, 15, 20, 15));
				}
	#endif
				// Unpack the second four luma values
				Y = _mm_slli_pi16(YUV_pi8, 8);
				Y = _mm_srli_pi16(Y, 8);

				// Unpack the second four chroma values
				UV = _mm_srli_pi16(YUV_pi8, 8);

				// Shuffle the chroma values into halfwords
				UV = _mm_shuffle_pi16(UV, _MM_SHUFFLE(3, 1, 2, 0));

				// Duplicate the chroma values
				U = _mm_unpacklo_pi16(UV, UV);
				V = _mm_unpackhi_pi16(UV, UV);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 u1a_pi16;
					__m64 v1a_pi16;
					int extracted_u;
					int extracted_v;
					uint8_t *byuv = (uint8_t *)YUV_ptr;

					if(column < post_column-column_step)
					{
						extracted_u = byuv[1];
						extracted_v = byuv[3];
					}
					else
					{
						extracted_u = byuv[1-4];
						extracted_v = byuv[3-4];
					}

					u1a_pi16 = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 1));
					v1a_pi16 = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 1));

					u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
					v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

					U = _mm_adds_pu16(U, u1a_pi16);
					V = _mm_adds_pu16(V, v1a_pi16);


					U = _mm_srai_pi16(U, 1);
					V = _mm_srai_pi16(V, 1);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7);			// This code fixed an overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				temp = _mm_adds_pi16(temp, rounding_pi16);
				R2 = _mm_adds_pi16(Y, temp);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				temp = _mm_adds_pi16(temp, rounding_pi16);
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_adds_pi16(temp, rounding_pi16);
				B2 = _mm_adds_pi16(Y, temp);
				B2 = _mm_srai_pi16(B2, 6);


				/***** Pack and store the RGB tuples *****/

	#if (0 && DEBUG)
				B_pi8 = _mm_setr_pi8(0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8);
				G_pi8 = _mm_setr_pi8(0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8);
				R_pi8 = _mm_setr_pi8(0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8);
	#else
				// Pack the RGB values
				R_pi8 = _mm_packs_pu16(B1, B2); // swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); // swapped with B
	#endif

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with zero
				BZ = _mm_unpacklo_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the first two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Get the first RGB tuple with zeros in the rest of the word
				RGB = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());

				// Get the second RGB tuple with zeros in the rest of the word
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 3*8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have two RGB tuples with two zero bytes in the rest of the word

				// Interleave the second two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Insert the first two red and green values into the output word
				RGB = _mm_or_si64(RGB, _mm_slli_si64(RGBZ, 6*8));

				// Store the first eight bytes of RGB values
				*(RGB_ptr++) = RGB;

				// Shift the blue value into position
				RGB = _mm_slli_si64(RGBZ, 5*8);
				RGB = _mm_srli_si64(RGB, 7*8);

				// Shift the second RGB tuple into position
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have four output bytes in the lower half of the word

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with zero
				BZ = _mm_unpackhi_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the third two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Insert the first RGB tuple into the output word
				RGB = _mm_unpacklo_pi32(RGB, RGBZ);

				// Shift the red value from the second RGB tuple into position
				temp = _mm_srli_si64(RGBZ, 4*8);
				temp = _mm_slli_si64(temp, 7*8);

				// Insert the red value into the upper byte of the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the second eight bytes of RGB values
				*(RGB_ptr++) = RGB;

				// Shift the green and blue values into position
				RGB = _mm_srli_si64(RGBZ, 5*8);

				// Interleave the fourth two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Shift the next RGB tuple into position
				temp = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 2*8);

				// Insert the RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Shift the last RGB tuple into position
				temp = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 5*8);

				// Insert the last RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the third eight bytes of RGB values
				*(RGB_ptr++) = RGB;
			}

			// Clear the MMX registers
			//_mm_empty();

			// Check that the loop terminated at the post processing column
			assert(column == post_column);

	#endif

			// Process the rest of the row with 7-bit fixed point arithmetic
			for (; column < length; column += 4)
			{
				int R, G, B;
				int Y1, Y2, U, V;

				// Convert the first set of YCbCr values
				if(saturate)
				{
					Y1 = SATURATE_Y(input[column]);
					U = SATURATE_Cr(input[column + 1]);
					Y2 = SATURATE_Y(input[column + 2]);
					V = SATURATE_Cb(input[column + 3]);
				}
				else
				{
					Y1 = (input[column]);
					U = (input[column + 1]);
					Y2 = (input[column + 2]);
					V = (input[column + 3]);
				}

				Y1 = Y1 - y_offset;
				Y2 = Y2 - y_offset;
				U = U - 128;
				V = V - 128;

				Y1 = Y1 * ymult >> 7;
				Y2 = Y2 * ymult >> 7;

				R = (Y1           + r_vmult * V) >> 7;
				G = (Y1*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y1 + 2 * b_umult * U) >> 7;

				*(output_ptr++)= SATURATE_8U(B);
				*(output_ptr++)= SATURATE_8U(G);
				*(output_ptr++)= SATURATE_8U(R);

				// Convert the second set of YCbCr values

				R = (Y2           + r_vmult * V) >> 7;
				G = (Y2*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y2 + 2 * b_umult * U) >> 7;

				*(output_ptr++)= SATURATE_8U(B);
				*(output_ptr++)= SATURATE_8U(G);
				*(output_ptr++)= SATURATE_8U(R);
			}
		}

		// The output format is RGB32 with the alpha channel set to the default value
		else
		{

	#if (0 && XMMOPT) //DANREMOVED

			int column_step = 16;
			int post_column = length - (length % column_step);
			__m64 *YUV_ptr = (__m64 *)input;
			//__m64 *YUV_ptr = (__m64 *)test_input;
			__m64 *RGBA_ptr = (__m64 *)output;

			// Check that the post processing column is a multiple of the column step
			assert((post_column % column_step) == 0);

			// Convert the YUV to RGB32 in groups of four pixels
			for (; column < post_column; column += column_step)
			{
				__m64 R1, G1, B1;
				__m64 R2, G2, B2;
				__m64 R_pi8, G_pi8, B_pi8;
				__m64 temp;
				//__m64 temp2;

				__m64 Y;
				__m64 U;
				__m64 V;
				__m64 UV;

				__m64 RG;
				__m64 BA;
				__m64 RGBA;

				__m64 rounding_pi16 = _mm_set1_pi16(32);// 6 bit half pt.

				// Load the first eight YCbCr values
				__m64 YUV_pi8 = *(YUV_ptr++);

	#if STRICT_SATURATE
				if(saturate)
				{
					// Perform strict saturation on YUV if required
					YUV_pi8 = _mm_subs_pu8(YUV_pi8, _mm_set1_pi8(16));
					YUV_pi8 = _mm_adds_pu8(YUV_pi8, _mm_setr_pi8(36, 31, 36, 31, 36, 31, 36, 31));
					YUV_pi8 = _mm_subs_pu8(YUV_pi8, _mm_setr_pi8(20, 15, 20, 15, 20, 15, 20, 15));
				}
	#endif

				/***** Calculate the first four RGB values *****/

				// Unpack the first four luma values
				Y = _mm_slli_pi16(YUV_pi8, 8);
				Y = _mm_srli_pi16(Y, 8);

				// Unpack the first four chroma values
				UV = _mm_srli_pi16(YUV_pi8, 8);

				// Preload the next eight YCbCr values
				YUV_pi8 = *(YUV_ptr++);

				// Shuffle the chroma values into halfwords
				UV = _mm_shuffle_pi16(UV, _MM_SHUFFLE(3, 1, 2, 0));

				// Duplicate the chroma values
				U = _mm_unpacklo_pi16(UV, UV);
				V = _mm_unpackhi_pi16(UV, UV);


#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 u1a_pi16;
					__m64 v1a_pi16;
					int extracted_u;
					int extracted_v;
					uint8_t *byuv = (uint8_t *)YUV_ptr;

					extracted_u = byuv[1-8];
					extracted_v = byuv[3-8];

					u1a_pi16 = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 1));
					v1a_pi16 = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 1));

					u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
					v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

					U = _mm_adds_pu16(U, u1a_pi16);
					V = _mm_adds_pu16(V, v1a_pi16);


					U = _mm_srai_pi16(U, 1);
					V = _mm_srai_pi16(V, 1);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7);			// This code fixed overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				temp = _mm_adds_pi16(temp, rounding_pi16);
				R1 = _mm_adds_pi16(Y, temp);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				temp = _mm_adds_pi16(temp, rounding_pi16);
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_adds_pi16(temp, rounding_pi16);
				B1 = _mm_adds_pi16(Y, temp);
				B1 = _mm_srai_pi16(B1, 6);

	#if STRICT_SATURATE
				if(saturate)
				{
					// Perform strict saturation on YUV if required
					YUV_pi8 = _mm_subs_pu8(YUV_pi8, _mm_set1_pi8(16));
					YUV_pi8 = _mm_adds_pu8(YUV_pi8, _mm_setr_pi8(36, 31, 36, 31, 36, 31, 36, 31));
					YUV_pi8 = _mm_subs_pu8(YUV_pi8, _mm_setr_pi8(20, 15, 20, 15, 20, 15, 20, 15));
				}
	#endif
				// Unpack the second four luma values
				Y = _mm_slli_pi16(YUV_pi8, 8);
				Y = _mm_srli_pi16(Y, 8);

				// Unpack the second four chroma values
				UV = _mm_srli_pi16(YUV_pi8, 8);

				// Shuffle the chroma values into halfwords
				UV = _mm_shuffle_pi16(UV, _MM_SHUFFLE(3, 1, 2, 0));

				// Duplicate the chroma values
				U = _mm_unpacklo_pi16(UV, UV);
				V = _mm_unpackhi_pi16(UV, UV);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 u1a_pi16;
					__m64 v1a_pi16;
					int extracted_u;
					int extracted_v;
					uint8_t *byuv = (uint8_t *)YUV_ptr;

					if(column < post_column-column_step)
					{
						extracted_u = byuv[1];
						extracted_v = byuv[3];
					}
					else
					{
						extracted_u = byuv[1-4];
						extracted_v = byuv[3-4];
					}

					u1a_pi16 = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 1));
					v1a_pi16 = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 1));

					u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
					v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

					U = _mm_adds_pu16(U, u1a_pi16);
					V = _mm_adds_pu16(V, v1a_pi16);


					U = _mm_srai_pi16(U, 1);
					V = _mm_srai_pi16(V, 1);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7);			// This code fixed an overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				temp = _mm_adds_pi16(temp, rounding_pi16);
				R2 = _mm_adds_pi16(Y, temp);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				temp = _mm_adds_pi16(temp, rounding_pi16);
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_adds_pi16(temp, rounding_pi16);
				B2 = _mm_adds_pi16(Y, temp);
				B2 = _mm_srai_pi16(B2, 6);


				/***** Pack and store the RGB tuples *****/

	#if (0 && DEBUG)
				B_pi8 = _mm_setr_pi8(0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8);
				G_pi8 = _mm_setr_pi8(0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8);
				R_pi8 = _mm_setr_pi8(0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8);
	#else
				// Pack the RGB values
				R_pi8 = _mm_packs_pu16(B1, B2); //swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); //swapped with B
	#endif
				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with the default alpha value
				BA = _mm_unpacklo_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the first two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);
				//RGBA = _mm_setr_pi8(0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF);

				// Store the first eight bytes of RGBA values
				*(RGBA_ptr++) = RGBA;

				// Interleave the second two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);
				//RGBA = _mm_setr_pi8(0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF);

				// Store the second eight bytes of RGBA values
				*(RGBA_ptr++) = RGBA;

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with the default alpha value
				BA = _mm_unpackhi_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the third two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);
				//RGBA = _mm_setr_pi8(0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF);

				// Store the third eight bytes of RGBA values
				*(RGBA_ptr++) = RGBA;

				// Interleave the fourth two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);
				//RGBA = _mm_setr_pi8(0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF);

				// Store the fourth eight bytes of RGBA values
				*(RGBA_ptr++) = RGBA;
			}

			// Clear the MMX registers
			//_mm_empty();

			// Check that the loop terminated at the post processing column
			assert(column == post_column);
	#endif

			// Process the rest of the row with 7-bit fixed point arithmetic
			for(; column < length; column += 4)
			{
				int R, G, B;
				int Y1, Y2, U, V;

				// Convert the first set of YCbCr values
				if(saturate)
				{
					Y1 = SATURATE_Y(input[column]);
					U = SATURATE_Cr(input[column + 1]);
					Y2 = SATURATE_Y(input[column + 2]);
					V = SATURATE_Cb(input[column + 3]);
				}
				else
				{
					Y1 = (input[column]);
					U = (input[column + 1]);
					Y2 = (input[column + 2]);
					V = (input[column + 3]);
				}

				Y1 = Y1 - y_offset;
				Y2 = Y2 - y_offset;
				U = U - 128;
				V = V - 128;

				Y1 = Y1 * ymult >> 7;
				Y2 = Y2 * ymult >> 7;

				R = (Y1           + r_vmult * V) >> 7;
				G = (Y1*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y1 + 2 * b_umult * U) >> 7;

				*(output_ptr++) = SATURATE_8U(B);
				*(output_ptr++) = SATURATE_8U(G);
				*(output_ptr++) = SATURATE_8U(R);
				*(output_ptr++) = RGBA_DEFAULT_ALPHA;

				// Convert the second set of YCbCr values

				R = (Y2           + r_vmult * V) >> 7;
				G = (Y2*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y2 + 2 * b_umult * U) >> 7;

				*(output_ptr++) = SATURATE_8U(B);
				*(output_ptr++) = SATURATE_8U(G);
				*(output_ptr++) = SATURATE_8U(R);
				*(output_ptr++) = RGBA_DEFAULT_ALPHA;
			}
		}
	}
	else // source data is 16-bit 16yuv packed like YYYYYYYYUUUUVVVVYYYYYYYYUUUUVVVV
	{
		length /= 2; // back to pixels

		y_offset <<= precision-8;

		if (format == COLOR_FORMAT_RGB24)
		{

	#if (0 && XMMOPT)  //DANREMOVED

			int column_step = 16;
			int post_column = length - (length % column_step);
			__m64 *YUV_ptr = (__m64 *)input;
			//__m64 *YUV_ptr = (__m64 *)test_input;
			__m64 *RGB_ptr = (__m64 *)output;

			int mask = (1 << (precision - 8)) - 1;
			__m64 rounding1_pi16 = _mm_set1_pi16(32); // for 6bit matm
			__m64 rounding2_pi16 = _mm_set1_pi16(32); // for 6bit matm
			__m64 rounding3_pi16 = _mm_set1_pi16(32); // for 6bit matm
			__m64 rounding4_pi16 = _mm_set1_pi16(32); // for 6bit matm
			__m64 roundinguv1_pi16 = _mm_set1_pi16(precision - 8); // for 2bit matm
			__m64 roundinguv2_pi16 = _mm_set1_pi16(precision - 8); // for 2bit matm
			__m64 overflowprotect_pi16 = _mm_set1_pi16(0x7fff - ((1 << precision) - 1));
			rounding1_pi16 = _mm_insert_pi16(rounding1_pi16, rand() & 63, 0);
			rounding1_pi16 = _mm_insert_pi16(rounding1_pi16, rand() & 63, 1);
			rounding1_pi16 = _mm_insert_pi16(rounding1_pi16, rand() & 63, 2);
			rounding1_pi16 = _mm_insert_pi16(rounding1_pi16, rand() & 63, 3);

			rounding2_pi16 = _mm_insert_pi16(rounding2_pi16, rand() & 63, 0);
			rounding2_pi16 = _mm_insert_pi16(rounding2_pi16, rand() & 63, 1);
			rounding2_pi16 = _mm_insert_pi16(rounding2_pi16, rand() & 63, 2);
			rounding2_pi16 = _mm_insert_pi16(rounding2_pi16, rand() & 63, 3);

			rounding3_pi16 = _mm_insert_pi16(rounding3_pi16, rand() & 63, 0);
			rounding3_pi16 = _mm_insert_pi16(rounding3_pi16, rand() & 63, 1);
			rounding3_pi16 = _mm_insert_pi16(rounding3_pi16, rand() & 63, 2);
			rounding3_pi16 = _mm_insert_pi16(rounding3_pi16, rand() & 63, 3);

			rounding4_pi16 = _mm_insert_pi16(rounding4_pi16, rand() & 63, 0);
			rounding4_pi16 = _mm_insert_pi16(rounding4_pi16, rand() & 63, 1);
			rounding4_pi16 = _mm_insert_pi16(rounding4_pi16, rand() & 63, 2);
			rounding4_pi16 = _mm_insert_pi16(rounding4_pi16, rand() & 63, 3);


			roundinguv1_pi16 = _mm_insert_pi16(roundinguv1_pi16, rand()&mask, 0);
			roundinguv1_pi16 = _mm_insert_pi16(roundinguv1_pi16, rand()&mask, 1);
			roundinguv1_pi16 = _mm_insert_pi16(roundinguv1_pi16, rand()&mask, 2);
			roundinguv1_pi16 = _mm_insert_pi16(roundinguv1_pi16, rand()&mask, 3);

			roundinguv2_pi16 = _mm_insert_pi16(roundinguv2_pi16, rand()&mask, 0);
			roundinguv2_pi16 = _mm_insert_pi16(roundinguv2_pi16, rand()&mask, 1);
			roundinguv2_pi16 = _mm_insert_pi16(roundinguv2_pi16, rand()&mask, 2);
			roundinguv2_pi16 = _mm_insert_pi16(roundinguv2_pi16, rand()&mask, 3);

			// Check that the post processing column is a multiple of the column step
			assert((post_column % column_step) == 0);

			// Convert the YUV to RGB24 in groups of eight pixels
			for (; column < post_column; column += column_step)
			{
				__m64 R1, G1, B1;
				__m64 R2, G2, B2;
				__m64 R_pi8, G_pi8, B_pi8;
				//__m64 YUV;
				__m64 temp;
				//__m64 temp2;
				__m64 RGB;

				__m64 Y;
				__m64 Y2;
				__m64 Y3;
				__m64 Y4;
				__m64 U;
				__m64 V;
				__m64 UU;
				__m64 VV;
				__m64 UU2;
				__m64 VV2;
				__m64 UU3;
				__m64 VV3;

				__m64 RG;
				__m64 BZ;
				__m64 RGBZ;

				// Load the first eight YCbCr values
				Y = *(YUV_ptr++);
				Y = _mm_adds_pi16(Y,overflowprotect_pi16);
				Y = _mm_subs_pu16(Y,overflowprotect_pi16);

				Y2 = *(YUV_ptr++);
				Y2 = _mm_adds_pi16(Y2,overflowprotect_pi16);
				Y2 = _mm_subs_pu16(Y2,overflowprotect_pi16);

				Y3 = *(YUV_ptr++);
				Y3 = _mm_adds_pi16(Y3,overflowprotect_pi16);
				Y3 = _mm_subs_pu16(Y3,overflowprotect_pi16);

				Y4 = *(YUV_ptr++);
				Y4 = _mm_adds_pi16(Y4,overflowprotect_pi16);
				Y4 = _mm_subs_pu16(Y4,overflowprotect_pi16);

				VV = *(YUV_ptr++);
				VV2 = *(YUV_ptr++);
				UU = *(YUV_ptr++);
				UU2 = *(YUV_ptr++);

				UU = _mm_adds_pi16(UU, roundinguv1_pi16);
				UU2 = _mm_adds_pi16(UU2, roundinguv2_pi16);
				VV = _mm_adds_pi16(VV, roundinguv1_pi16);
				VV2 = _mm_adds_pi16(VV2, roundinguv2_pi16);

				UU = _mm_srai_pi16(UU, precision-8);
				VV = _mm_srai_pi16(VV, precision-8);
				UU2 = _mm_srai_pi16(UU2, precision-8);
				VV2 = _mm_srai_pi16(VV2, precision-8);

#if CHROMA422to444
				if(upconvert422to444 && column < post_column-column_step)
				{
					VV3 = YUV_ptr[4];
					UU3 = YUV_ptr[6];
					UU3 = _mm_adds_pi16(UU3, roundinguv1_pi16);
					VV3 = _mm_adds_pi16(VV3, roundinguv1_pi16);
					UU3 = _mm_srai_pi16(UU3, precision-8);
					VV3 = _mm_srai_pi16(VV3, precision-8);
				}
#endif

				// Duplicate the chroma values
				U = _mm_unpacklo_pi16(UU, UU);
				V = _mm_unpacklo_pi16(VV, VV);



#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 u1a_pi16;
					__m64 v1a_pi16;
					int extracted_u;
					int extracted_v;
					//unsigned short *syuv = (unsigned short *)YUV_ptr;

					extracted_u = _mm_extract_pi16(UU, 2);
					extracted_v = _mm_extract_pi16(VV, 2);

					u1a_pi16 = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 1));
					v1a_pi16 = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 1));

					u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
					v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

					U = _mm_adds_pu16(U, u1a_pi16);
					V = _mm_adds_pu16(V, v1a_pi16);


					U = _mm_srai_pi16(U, 1);
					V = _mm_srai_pi16(V, 1);
				}
#endif


				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7-(precision-8));			// This code fixed overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				temp = _mm_adds_pi16(temp, rounding1_pi16);
				R1 = _mm_adds_pi16(Y, temp);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				temp = _mm_adds_pi16(temp, rounding1_pi16);
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_adds_pi16(temp, rounding1_pi16);
				B1 = _mm_adds_pi16(Y, temp);
				B1 = _mm_srai_pi16(B1, 6);


				// Load the second eight YCbCr values
				Y = Y2;
				// Duplicate the chroma values
				U = _mm_unpackhi_pi16(UU, UU);
				V = _mm_unpackhi_pi16(VV, VV);


#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 u1a_pi16;
					__m64 v1a_pi16;
					int extracted_u;
					int extracted_v;
					//unsigned short *syuv = (unsigned short *)YUV_ptr;

					extracted_u = _mm_extract_pi16(UU2, 0);
					extracted_v = _mm_extract_pi16(VV2, 0);

					u1a_pi16 = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 1));
					v1a_pi16 = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 1));

					u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
					v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

					U = _mm_adds_pu16(U, u1a_pi16);
					V = _mm_adds_pu16(V, v1a_pi16);


					U = _mm_srai_pi16(U, 1);
					V = _mm_srai_pi16(V, 1);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7-(precision-8));			// This code fixed an overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				temp = _mm_adds_pi16(temp, rounding2_pi16);
				R2 = _mm_adds_pi16(Y, temp);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				temp = _mm_adds_pi16(temp, rounding2_pi16);
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_adds_pi16(temp, rounding2_pi16);
				B2 = _mm_adds_pi16(Y, temp);
				B2 = _mm_srai_pi16(B2, 6);


				/***** Pack and store the RGB tuples *****/

				// Pack the RGB values
				R_pi8 = _mm_packs_pu16(B1, B2); // swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); // swapped with B

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with zero
				BZ = _mm_unpacklo_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the first two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Get the first RGB tuple with zeros in the rest of the word
				RGB = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());

				// Get the second RGB tuple with zeros in the rest of the word
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 3*8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have two RGB tuples with two zero bytes in the rest of the word

				// Interleave the second two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Insert the first two red and green values into the output word
				RGB = _mm_or_si64(RGB, _mm_slli_si64(RGBZ, 6*8));

				// Store the first eight bytes of RGB values
				*(RGB_ptr++) = RGB;

				// Shift the blue value into position
				RGB = _mm_slli_si64(RGBZ, 5*8);
				RGB = _mm_srli_si64(RGB, 7*8);

				// Shift the second RGB tuple into position
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have four output bytes in the lower half of the word

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with zero
				BZ = _mm_unpackhi_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the third two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Insert the first RGB tuple into the output word
				RGB = _mm_unpacklo_pi32(RGB, RGBZ);

				// Shift the red value from the second RGB tuple into position
				temp = _mm_srli_si64(RGBZ, 4*8);
				temp = _mm_slli_si64(temp, 7*8);

				// Insert the red value into the upper byte of the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the second eight bytes of RGB values
				*(RGB_ptr++) = RGB;

				// Shift the green and blue values into position
				RGB = _mm_srli_si64(RGBZ, 5*8);

				// Interleave the fourth two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Shift the next RGB tuple into position
				temp = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 2*8);

				// Insert the RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Shift the last RGB tuple into position
				temp = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 5*8);

				// Insert the last RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the third eight bytes of RGB values
				*(RGB_ptr++) = RGB;






				Y = Y3;
				// Duplicate the chroma values
				U = _mm_unpacklo_pi16(UU2, UU2);
				V = _mm_unpacklo_pi16(VV2, VV2);


#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 u1a_pi16;
					__m64 v1a_pi16;
					int extracted_u;
					int extracted_v;
					//unsigned short *syuv = (unsigned short *)YUV_ptr;

					extracted_u = _mm_extract_pi16(UU2, 2);
					extracted_v = _mm_extract_pi16(VV2, 2);

					u1a_pi16 = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 1));
					v1a_pi16 = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 1));

					u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
					v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

					U = _mm_adds_pu16(U, u1a_pi16);
					V = _mm_adds_pu16(V, v1a_pi16);


					U = _mm_srai_pi16(U, 1);
					V = _mm_srai_pi16(V, 1);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7-(precision-8));			// This code fixed overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				temp = _mm_adds_pi16(temp, rounding3_pi16);
				R1 = _mm_adds_pi16(Y, temp);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				temp = _mm_adds_pi16(temp, rounding3_pi16);
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_adds_pi16(temp, rounding3_pi16);
				B1 = _mm_adds_pi16(Y, temp);
				B1 = _mm_srai_pi16(B1, 6);


				// Load the second eight YCbCr values
				Y = Y4;
				// Duplicate the chroma values
				U = _mm_unpackhi_pi16(UU2, UU2);
				V = _mm_unpackhi_pi16(VV2, VV2);


#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 u1a_pi16;
					__m64 v1a_pi16;
					int extracted_u;
					int extracted_v;
					//unsigned short *syuv = (unsigned short *)YUV_ptr;

					if(column < post_column-column_step)
					{
						extracted_u = _mm_extract_pi16(UU3, 0);
						extracted_v = _mm_extract_pi16(VV3, 0);
					}
					else
					{
						extracted_u = _mm_extract_pi16(UU2, 3);
						extracted_v = _mm_extract_pi16(VV2, 3);
					}
					u1a_pi16 = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 1));
					v1a_pi16 = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 1));

					u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
					v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

					U = _mm_adds_pu16(U, u1a_pi16);
					V = _mm_adds_pu16(V, v1a_pi16);


					U = _mm_srai_pi16(U, 1);
					V = _mm_srai_pi16(V, 1);
				}
#endif




				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7-(precision-8));			// This code fixed an overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				temp = _mm_adds_pi16(temp, rounding4_pi16);
				R2 = _mm_adds_pi16(Y, temp);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				temp = _mm_adds_pi16(temp, rounding4_pi16);
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_adds_pi16(temp, rounding4_pi16);
				B2 = _mm_adds_pi16(Y, temp);
				B2 = _mm_srai_pi16(B2, 6);


				/***** Pack and store the RGB tuples *****/

				// Pack the RGB values
				R_pi8 = _mm_packs_pu16(B1, B2); // swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); // swapped with B

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with zero
				BZ = _mm_unpacklo_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the first two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Get the first RGB tuple with zeros in the rest of the word
				RGB = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());

				// Get the second RGB tuple with zeros in the rest of the word
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 3*8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have two RGB tuples with two zero bytes in the rest of the word

				// Interleave the second two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Insert the first two red and green values into the output word
				RGB = _mm_or_si64(RGB, _mm_slli_si64(RGBZ, 6*8));

				// Store the first eight bytes of RGB values
				*(RGB_ptr++) = RGB;

				// Shift the blue value into position
				RGB = _mm_slli_si64(RGBZ, 5*8);
				RGB = _mm_srli_si64(RGB, 7*8);

				// Shift the second RGB tuple into position
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have four output bytes in the lower half of the word

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with zero
				BZ = _mm_unpackhi_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the third two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Insert the first RGB tuple into the output word
				RGB = _mm_unpacklo_pi32(RGB, RGBZ);

				// Shift the red value from the second RGB tuple into position
				temp = _mm_srli_si64(RGBZ, 4*8);
				temp = _mm_slli_si64(temp, 7*8);

				// Insert the red value into the upper byte of the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the second eight bytes of RGB values
				*(RGB_ptr++) = RGB;

				// Shift the green and blue values into position
				RGB = _mm_srli_si64(RGBZ, 5*8);

				// Interleave the fourth two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Shift the next RGB tuple into position
				temp = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 2*8);

				// Insert the RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Shift the last RGB tuple into position
				temp = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 5*8);

				// Insert the last RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the third eight bytes of RGB values
				*(RGB_ptr++) = RGB;
			}

			// Clear the MMX registers
			//_mm_empty();

			// Check that the loop terminated at the post processing column
			assert(column == post_column);

	#endif

			// Process the rest of the row with 7-bit fixed point arithmetic
			for (; column < length; column += 4)
			{
				int R, G, B;
				int Y1, Y2, U, V;

				// Convert the first set of YCbCr values
				if(saturate)
				{
					Y1 = SATURATE_Y(input[column]);
					U = SATURATE_Cr(input[column + 1]);
					Y2 = SATURATE_Y(input[column + 2]);
					V = SATURATE_Cb(input[column + 3]);
				}
				else
				{
					Y1 = (input[column]);
					U = (input[column + 1]);
					Y2 = (input[column + 2]);
					V = (input[column + 3]);
				}

				Y1 = Y1 - y_offset;
				Y2 = Y2 - y_offset;
				U = U - 128;
				V = V - 128;

				Y1 = Y1 * ymult >> 7;
				Y2 = Y2 * ymult >> 7;

				R = (Y1           + r_vmult * V) >> 7;
				G = (Y1*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y1 + 2 * b_umult * U) >> 7;

				*(output_ptr++)= SATURATE_8U(B);
				*(output_ptr++)= SATURATE_8U(G);
				*(output_ptr++)= SATURATE_8U(R);

				// Convert the second set of YCbCr values

				R = (Y2           + r_vmult * V) >> 7;
				G = (Y2*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y2 + 2 * b_umult * U) >> 7;

				*(output_ptr++)= SATURATE_8U(B);
				*(output_ptr++)= SATURATE_8U(G);
				*(output_ptr++)= SATURATE_8U(R);
			}
		}

		// The output format is RGB32 with the alpha channel set to the default value
		else
		{

	#if (0 && XMMOPT) //DANREMOVED

			int column_step = 16;
			int post_column = length - (length % column_step);
			__m64 *YUV_ptr = (__m64 *)input;
			//__m64 *YUV_ptr = (__m64 *)test_input;
			__m64 *RGBA_ptr = (__m64 *)output;

			// Check that the post processing column is a multiple of the column step
			assert((post_column % column_step) == 0);

			// Convert the YUV to RGB32 in groups of four pixels
			for (; column < post_column; column += column_step)
			{
				__m64 R1, G1, B1;
				__m64 R2, G2, B2;
				__m64 R_pi8, G_pi8, B_pi8;
				__m64 temp;
				//__m64 temp2;

				__m64 Y;
				__m64 Y2;
				__m64 Y3;
				__m64 Y4;
				__m64 U;
				__m64 V;
				__m64 UU;
				__m64 VV;
				__m64 UU2;
				__m64 VV2;
				__m64 UU3;
				__m64 VV3;

				__m64 RG;
				__m64 BA;
				__m64 RGBA;

				//__m64 rounding_pi16 = _mm_set1_pi16(32);// 6 bit half pt.

				// Load the first eight YCbCr values
				Y = *(YUV_ptr++);
				Y = _mm_adds_pi16(Y,overflowprotect_pi16);
				Y = _mm_subs_pu16(Y,overflowprotect_pi16);

				Y2 = *(YUV_ptr++);
				Y2 = _mm_adds_pi16(Y2,overflowprotect_pi16);
				Y2 = _mm_subs_pu16(Y2,overflowprotect_pi16);

				Y3 = *(YUV_ptr++);
				Y3 = _mm_adds_pi16(Y3,overflowprotect_pi16);
				Y3 = _mm_subs_pu16(Y3,overflowprotect_pi16);

				Y4 = *(YUV_ptr++);
				Y4 = _mm_adds_pi16(Y4,overflowprotect_pi16);
				Y4 = _mm_subs_pu16(Y4,overflowprotect_pi16);

				VV = *(YUV_ptr++);
				VV2 = *(YUV_ptr++);
				UU = *(YUV_ptr++);
				UU2 = *(YUV_ptr++);

				UU = _mm_srai_pi16(UU, precision-8);
				VV = _mm_srai_pi16(VV, precision-8);
				UU2 = _mm_srai_pi16(UU2, precision-8);
				VV2 = _mm_srai_pi16(VV2, precision-8);


#if CHROMA422to444
				if(upconvert422to444 && column < post_column-column_step)
				{
					VV3 = YUV_ptr[4];
					UU3 = YUV_ptr[6];
					UU3 = _mm_adds_pi16(UU3, roundinguv1_pi16);
					VV3 = _mm_adds_pi16(VV3, roundinguv1_pi16);
					UU3 = _mm_srai_pi16(UU3, precision-8);
					VV3 = _mm_srai_pi16(VV3, precision-8);
				}
#endif


				// Duplicate the chroma values
				U = _mm_unpacklo_pi16(UU, UU);
				V = _mm_unpacklo_pi16(VV, VV);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 u1a_pi16;
					__m64 v1a_pi16;
					int extracted_u;
					int extracted_v;
					//unsigned short *syuv = (unsigned short *)YUV_ptr;

					extracted_u = _mm_extract_pi16(UU, 2);
					extracted_v = _mm_extract_pi16(VV, 2);

					u1a_pi16 = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 1));
					v1a_pi16 = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 1));

					u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
					v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

					U = _mm_adds_pu16(U, u1a_pi16);
					V = _mm_adds_pu16(V, v1a_pi16);


					U = _mm_srai_pi16(U, 1);
					V = _mm_srai_pi16(V, 1);
				}
#endif
				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7-(precision-8));			// This code fixed overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				temp = _mm_adds_pi16(temp, rounding1_pi16);
				R1 = _mm_adds_pi16(Y, temp);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				temp = _mm_adds_pi16(temp, rounding1_pi16);
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_adds_pi16(temp, rounding1_pi16);
				B1 = _mm_adds_pi16(Y, temp);
				B1 = _mm_srai_pi16(B1, 6);


			// Load the second eight YCbCr values
				Y = Y2;
				// Duplicate the chroma values
				U = _mm_unpackhi_pi16(UU, UU);
				V = _mm_unpackhi_pi16(VV, VV);


#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 u1a_pi16;
					__m64 v1a_pi16;
					int extracted_u;
					int extracted_v;
					//unsigned short *syuv = (unsigned short *)YUV_ptr;

					extracted_u = _mm_extract_pi16(UU2, 0);
					extracted_v = _mm_extract_pi16(VV2, 0);

					u1a_pi16 = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 1));
					v1a_pi16 = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 1));

					u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
					v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

					U = _mm_adds_pu16(U, u1a_pi16);
					V = _mm_adds_pu16(V, v1a_pi16);


					U = _mm_srai_pi16(U, 1);
					V = _mm_srai_pi16(V, 1);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7-(precision-8));			// This code fixed an overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				temp = _mm_adds_pi16(temp, rounding2_pi16);
				R2 = _mm_adds_pi16(Y, temp);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				temp = _mm_adds_pi16(temp, rounding2_pi16);
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_adds_pi16(temp, rounding2_pi16);
				B2 = _mm_adds_pi16(Y, temp);
				B2 = _mm_srai_pi16(B2, 6);


				/***** Pack and store the RGB tuples *****/

				// Pack the RGB values
				R_pi8 = _mm_packs_pu16(B1, B2); //swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); //swapped with B

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with the default alpha value
				BA = _mm_unpacklo_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the first two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);
				//RGBA = _mm_setr_pi8(0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF);

				// Store the first eight bytes of RGBA values
				*(RGBA_ptr++) = RGBA;

				// Interleave the second two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);
				//RGBA = _mm_setr_pi8(0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF);

				// Store the second eight bytes of RGBA values
				*(RGBA_ptr++) = RGBA;

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with the default alpha value
				BA = _mm_unpackhi_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the third two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);
				//RGBA = _mm_setr_pi8(0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF);

				// Store the third eight bytes of RGBA values
				*(RGBA_ptr++) = RGBA;

				// Interleave the fourth two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);
				//RGBA = _mm_setr_pi8(0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF);

				// Store the fourth eight bytes of RGBA values
				*(RGBA_ptr++) = RGBA;



				Y = Y3;
				// Duplicate the chroma values
				U = _mm_unpacklo_pi16(UU2, UU2);
				V = _mm_unpacklo_pi16(VV2, VV2);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 u1a_pi16;
					__m64 v1a_pi16;
					int extracted_u;
					int extracted_v;
					//unsigned short *syuv = (unsigned short *)YUV_ptr;

					extracted_u = _mm_extract_pi16(UU2, 2);
					extracted_v = _mm_extract_pi16(VV2, 2);

					u1a_pi16 = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 1));
					v1a_pi16 = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 1));

					u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
					v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

					U = _mm_adds_pu16(U, u1a_pi16);
					V = _mm_adds_pu16(V, v1a_pi16);


					U = _mm_srai_pi16(U, 1);
					V = _mm_srai_pi16(V, 1);
				}
#endif
				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7-(precision-8));			// This code fixed overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				temp = _mm_adds_pi16(temp, rounding3_pi16);
				R1 = _mm_adds_pi16(Y, temp);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				temp = _mm_adds_pi16(temp, rounding3_pi16);
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_adds_pi16(temp, rounding3_pi16);
				B1 = _mm_adds_pi16(Y, temp);
				B1 = _mm_srai_pi16(B1, 6);


				// Load the second eight YCbCr values
				Y = Y4;
				// Duplicate the chroma values
				U = _mm_unpackhi_pi16(UU2, UU2);
				V = _mm_unpackhi_pi16(VV2, VV2);


#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 u1a_pi16;
					__m64 v1a_pi16;
					int extracted_u;
					int extracted_v;
					//unsigned short *syuv = (unsigned short *)YUV_ptr;

					if(column < post_column-column_step)
					{
						extracted_u = _mm_extract_pi16(UU3, 0);
						extracted_v = _mm_extract_pi16(VV3, 0);
					}
					else
					{
						extracted_u = _mm_extract_pi16(UU2, 3);
						extracted_v = _mm_extract_pi16(VV2, 3);
					}
					u1a_pi16 = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 1));
					v1a_pi16 = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 1));

					u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
					v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

					U = _mm_adds_pu16(U, u1a_pi16);
					V = _mm_adds_pu16(V, v1a_pi16);


					U = _mm_srai_pi16(U, 1);
					V = _mm_srai_pi16(V, 1);
				}
#endif


				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7-(precision-8));			// This code fixed an overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				temp = _mm_adds_pi16(temp, rounding4_pi16);
				R2 = _mm_adds_pi16(Y, temp);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				temp = _mm_adds_pi16(temp, rounding4_pi16);
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				temp = _mm_adds_pi16(temp, rounding4_pi16);
				B2 = _mm_adds_pi16(Y, temp);
				B2 = _mm_srai_pi16(B2, 6);


				/***** Pack and store the RGB tuples *****/

				// Pack the RGB values
				R_pi8 = _mm_packs_pu16(B1, B2); //swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); //swapped with B

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with the default alpha value
				BA = _mm_unpacklo_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the first two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);
				//RGBA = _mm_setr_pi8(0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF);

				// Store the first eight bytes of RGBA values
				*(RGBA_ptr++) = RGBA;

				// Interleave the second two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);
				//RGBA = _mm_setr_pi8(0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF);

				// Store the second eight bytes of RGBA values
				*(RGBA_ptr++) = RGBA;

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with the default alpha value
				BA = _mm_unpackhi_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the third two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);
				//RGBA = _mm_setr_pi8(0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF);

				// Store the third eight bytes of RGBA values
				*(RGBA_ptr++) = RGBA;

				// Interleave the fourth two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);
				//RGBA = _mm_setr_pi8(0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF);

				// Store the fourth eight bytes of RGBA values
				*(RGBA_ptr++) = RGBA;


			}

			// Clear the MMX registers
			//_mm_empty();

			// Check that the loop terminated at the post processing column
			assert(column == post_column);
	#endif

			// Process the rest of the row with 7-bit fixed point arithmetic
			for(; column < length; column += 4)
			{
				int R, G, B;
				int Y1, Y2, U, V;

				// Convert the first set of YCbCr values
				if(saturate)
				{
					Y1 = SATURATE_Y(input[column]);
					U = SATURATE_Cr(input[column + 1]);
					Y2 = SATURATE_Y(input[column + 2]);
					V = SATURATE_Cb(input[column + 3]);
				}
				else
				{
					Y1 = (input[column]);
					U = (input[column + 1]);
					Y2 = (input[column + 2]);
					V = (input[column + 3]);
				}

				Y1 = Y1 - y_offset;
				Y2 = Y2 - y_offset;
				U = U - 128;
				V = V - 128;

				Y1 = Y1 * ymult >> 7;
				Y2 = Y2 * ymult >> 7;

				R = (Y1           + r_vmult * V) >> 7;
				G = (Y1*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y1 + 2 * b_umult * U) >> 7;

				*(output_ptr++) = SATURATE_8U(B);
				*(output_ptr++) = SATURATE_8U(G);
				*(output_ptr++) = SATURATE_8U(R);
				*(output_ptr++) = RGBA_DEFAULT_ALPHA;

				// Convert the second set of YCbCr values

				R = (Y2           + r_vmult * V) >> 7;
				G = (Y2*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y2 + 2 * b_umult * U) >> 7;

				*(output_ptr++) = SATURATE_8U(B);
				*(output_ptr++) = SATURATE_8U(G);
				*(output_ptr++) = SATURATE_8U(R);
				*(output_ptr++) = RGBA_DEFAULT_ALPHA;
			}
		}
	}
}

void ConvertYUYVRowToUYVY(uint8_t *input, uint8_t *output, int length, int format)
{
	// Start processing at the first column
	int column = 0;

#if (1 && XMMOPT)

	// Process four bytes each of luma and chroma per loop iteration
	//const int column_step = sizeof(__m64);

	// Determine the column at which post processing must begin
	//int post_column = length - (length % column_step);

	//__m64 *input_ptr = (__m64 *)input;
	//__m64 *output_ptr = (__m64 *)output;

#endif

	// Convert the row length from pixels to bytes
	length *= 2;

#if (0 && XMMOPT)  //DANREMOVED

	for (; column < post_column; column += column_step)
	{
		__m64 yuv1_pu8;		// Interleaved bytes of luma and chroma
		//__m64 yuv2_pu8;	// Interleaved bytes of luma and chroma
		__m64 u_pu8;

		// Load eight bytes of luma and chroma
		yuv1_pu8 = *(input_ptr++);

		// Shift the u chroma values to the least significant byte
		u_pu8 = _mm_srli_pi32(yuv1_pu8, 24);

		// Shift away the u chroma from the original pixels
		yuv1_pu8 = _mm_slli_pi32(yuv1_pu8, 8);

		// Insert the u chroma in the least significant byte
		yuv1_pu8 = _mm_or_si64(yuv1_pu8, u_pu8);

		// Store the result
		*(output_ptr++) = yuv1_pu8;
	}

	//_mm_empty();	// Clear the mmx register state

	// Should have exited the loop at the post processing column
	assert(column == post_column);

#endif

	// Process the rest of the column
	for (; column < length; column += 4)
	{
		// Unpack two luminance values and two chroma (which are reversed)
		uint8_t y1 = input[column + 0];
		uint8_t v  = input[column + 1];
		uint8_t y2 = input[column + 2];
		uint8_t u  = input[column + 3];

		// Output the reordered luminance and chrominance values
		output[column + 0] = y1;
		output[column + 1] = y2;
		output[column + 2] = u;
		output[column + 3] = v;
	}

	// Should have exited the loop just after the last column
	assert(column == length);
}

#endif


//#if BUILD_PROSPECT
//***** Not Finished: Must expand 8 bit YUV to V210 *****/
void ConvertYUYVRowToV210(uint8_t *input, uint8_t *output, int length, int format)
{
	// Start processing at the first column
	int column = 0;

#if (1 && XMMOPT)

	// Process four bytes each of luma and chroma per loop iteration
	//const int column_step = sizeof(__m64);

	// Determine the column at which post processing must begin
	//int post_column = length - (length % column_step);

	//__m64 *input_ptr = (__m64 *)input;
	//__m64 *output_ptr = (__m64 *)output;

#endif


	/***** Not Finished *****/
	assert(0);


	// Convert the row length from pixels to bytes
	length *= 2;

#if (0 && XMMOPT)  //DANREMOVE

	for (; column < post_column; column += column_step)
	{
		__m64 yuv1_pu8;		// Interleaved bytes of luma and chroma
		//__m64 yuv2_pu8;	// Interleaved bytes of luma and chroma
		__m64 u_pu8;

		// Load eight bytes of luma and chroma
		yuv1_pu8 = *(input_ptr++);

		// Shift the u chroma values to the least significant byte
		u_pu8 = _mm_srli_pi32(yuv1_pu8, 24);

		// Shift away the u chroma from the original pixels
		yuv1_pu8 = _mm_slli_pi32(yuv1_pu8, 8);

		// Insert the u chroma in the least significant byte
		yuv1_pu8 = _mm_or_si64(yuv1_pu8, u_pu8);

		// Store the result
		*(output_ptr++) = yuv1_pu8;
	}

	//_mm_empty();	// Clear the mmx register state

	// Should have exited the loop at the post processing column
	assert(column == post_column);

#endif

	// Process the rest of the column
	for (; column < length; column += 4)
	{
		// Unpack two luminance values and two chroma (which are reversed)
		uint8_t y1 = input[column + 0];
		uint8_t v  = input[column + 1];
		uint8_t y2 = input[column + 2];
		uint8_t u  = input[column + 3];

		// Output the reordered luminance and chrominance values
		output[column + 0] = y1;
		output[column + 1] = y2;
		output[column + 2] = u;
		output[column + 3] = v;
	}

	// Should have exited the loop just after the last column
	assert(column == length);
}
//#endif

//#if BUILD_PROSPECT
void ConvertYUYVRowToYU64(uint8_t *input, uint8_t *output, int length, int format)
{
	// Start processing at the first column
	int column = 0;

#if (0 && XMMOPT)  //DANREMOVED

	// Process four bytes each of luma and chroma per loop iteration
	const int column_step = sizeof(__m64);

	// Determine the column at which post processing must begin
	int post_column = length - (length % column_step);

	__m64 *input_ptr = (__m64 *)input;
	__m64 *output_ptr = (__m64 *)output;

#endif


	/***** Not Finished *****/
	assert(0);


	// Convert the row length from pixels to bytes
	length *= 2;

#if (0 && XMMOPT)  //DANREMOVED

	for (; column < post_column; column += column_step)
	{
		__m64 yuv1_pu8;		// Interleaved bytes of luma and chroma
		//__m64 yuv2_pu8;	// Interleaved bytes of luma and chroma
		__m64 u_pu8;

		// Load eight bytes of luma and chroma
		yuv1_pu8 = *(input_ptr++);

		// Shift the u chroma values to the least significant byte
		u_pu8 = _mm_srli_pi32(yuv1_pu8, 24);

		// Shift away the u chroma from the original pixels
		yuv1_pu8 = _mm_slli_pi32(yuv1_pu8, 8);

		// Insert the u chroma in the least significant byte
		yuv1_pu8 = _mm_or_si64(yuv1_pu8, u_pu8);

		// Store the result
		*(output_ptr++) = yuv1_pu8;
	}

	//_mm_empty();	// Clear the mmx register state

	// Should have exited the loop at the post processing column
	assert(column == post_column);

#endif

	// Process the rest of the column
	for (; column < length; column += 4)
	{
		// Unpack two luminance values and two chroma (which are reversed)
		uint8_t y1 = input[column + 0];
		uint8_t v  = input[column + 1];
		uint8_t y2 = input[column + 2];
		uint8_t u  = input[column + 3];

		// Output the reordered luminance and chrominance values
		output[column + 0] = y1;
		output[column + 1] = y2;
		output[column + 2] = u;
		output[column + 3] = v;
	}

	// Should have exited the loop just after the last column
	assert(column == length);
}
//#endif

// Convert a row of RGB pixels to packed YUV
void ConvertRGBRowToYUYV(uint8_t *input, uint8_t *output, int length)
{
	int count = length;		// Number of pixels to process
	uint8_t *input_ptr = input;
	uint8_t *output_ptr = output;

	// Must have an even number of pixels in the row
	assert((count % 2) == 0);

	// Process the row of RGB pixels
	for (; count > 0; count -= 2)
	{
		int r, g, b;
		int y, u, v;

		// Load the first RGB pixel
		b = *(input_ptr++);
		g = *(input_ptr++);
		r = *(input_ptr++);

		// Convert to YCbCr
		y = ( 66 * r + 129 * g +  25 * b +  4224) >> 8;
		u = (-38 * r -  74 * g + 112 * b + 32896) >> 9;
		v = (112 * r -  94 * g -  18 * b + 32896) >> 9;

		// Store the first luma value
		*(output_ptr++) = SATURATE_Y(y);

		// Load the second RGB pixel
		b = *(input_ptr++);
		g = *(input_ptr++);
		r = *(input_ptr++);

		// Convert to YCbCr
		y =  ( 66 * r + 129 * g +  25 * b +  4224) >> 8;
		u += (-38 * r -  74 * g + 112 * b + 32896) >> 9;
		v += (112 * r -  94 * g -  18 * b + 32896) >> 9;

		// Store the chroma values and the second luma value
		*(output_ptr++) = SATURATE_Cr(v);
		*(output_ptr++) = SATURATE_Y(y);
		*(output_ptr++) = SATURATE_Cb(u);
	}
}

// Convert a row of ARGB pixels into packed YUYV
void ConvertARGBRowToYUYV(uint8_t *input, uint8_t *output, int length)
{
	int count = length;		// Number of pixels to process
	uint8_t *input_ptr = input;
	uint8_t *output_ptr = output;

	// Must have an even number of pixels in the row
	assert((count % 2) == 0);

	// Process the row of ARGB pixels
	for (; count > 0; count -= 2)
	{
		int r, g, b, alpha;
		int y, u, v;

		// Load the first ARGB pixel
		b = *(input_ptr++);
		g = *(input_ptr++);
		r = *(input_ptr++);
		alpha = *(input_ptr++);

		// Convert to YCbCr
		y = ( 66 * r + 129 * g +  25 * b +  4224) >> 8;
		u = (-38 * r -  74 * g + 112 * b + 32896) >> 9;
		v = (112 * r -  94 * g -  18 * b + 32896) >> 9;

		// Store the first luma value
		*(output_ptr++) = SATURATE_Y(y);

		// Load the second ARGB pixel
		b = *(input_ptr++);
		g = *(input_ptr++);
		r = *(input_ptr++);
		alpha = *(input_ptr++);

		// Convert to YCbCr
		y =  ( 66 * r + 129 * g +  25 * b +  4224) >> 8;
		u += (-38 * r -  74 * g + 112 * b + 32896) >> 9;
		v += (112 * r -  94 * g -  18 * b + 32896) >> 9;

		// Store the chroma values and the second luma value
		*(output_ptr++) = SATURATE_Cr(v);
		*(output_ptr++) = SATURATE_Y(y);
		*(output_ptr++) = SATURATE_Cb(u);
	}
}


//#if BUILD_PROSPECT

#define RGB10_RED_SHIFT		20
#define RGB10_GREEN_SHIFT	10
#define RGB10_BLUE_SHIFT	 0

#define RGB10_VALUE_MASK	0x03FF

// Convert one row of 10-bit RGB padded to 32 bits to 16-bit YUV
void ConvertRGB10RowToYUV(uint8_t *input, uint8_t *output, int length)
{
	uint32_t *input_ptr = (uint32_t *)input;
	PIXEL *output_ptr = (PIXEL *)output;
	int i;

	// Must have an even number of pixels
	assert((length % 2) == 0);

	for (i = 0; i < length; i += 2)
	{
		uint32_t rgb;

		int r1;
		int g1;
		int b1;

		int r2;
		int g2;
		int b2;

		int y, u, v;

		rgb = input_ptr[i];

		r1 = (rgb >> RGB10_RED_SHIFT) & RGB10_VALUE_MASK;
		g1 = (rgb >> RGB10_GREEN_SHIFT) & RGB10_VALUE_MASK;
		b1 = (rgb >> RGB10_BLUE_SHIFT) & RGB10_VALUE_MASK;

		rgb = input_ptr[i + 1];

		r2 = (rgb >> RGB10_RED_SHIFT) & RGB10_VALUE_MASK;
		g2 = (rgb >> RGB10_GREEN_SHIFT) & RGB10_VALUE_MASK;
		b2 = (rgb >> RGB10_BLUE_SHIFT) & RGB10_VALUE_MASK;

		// Convert the first RGB tuple to YCbCr
		y =  ( 66 * r1 + 129 * g1 +  25 * b1 +  4224) >> 8;
		u =  (-38 * r1 -  74 * g1 + 112 * b1 + 32896) >> 9;
		v =  (112 * r1 -  94 * g1 -  18 * b1 + 32896) >> 9;

		*(output_ptr++) = SATURATE_Y(y);

		// Convert the first RGB tuple to YCbCr
		y =  ( 66 * r2 + 129 * g2 +  25 * b2 +  4224) >> 8;
		u += (-38 * r2 -  74 * g2 + 112 * b2 + 32896) >> 9;
		v += (112 * r2 -  94 * g2 -  18 * b2 + 32896) >> 9;

		*(output_ptr++) = SATURATE_Cr(v);
		*(output_ptr++) = SATURATE_Y(y);
		*(output_ptr++) = SATURATE_Cb(u);
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert packed 10-bit YUV 4:2:2 to packed 8-bit YUV
#if DANREMOVE 
void ConvertV210ToYUV(uint8_t *input, int width, int height, int input_pitch,
					  uint8_t *output, int output_pitch, uint8_t *buffer)
{
	uint32_t *v210_row_ptr = (uint32_t *)input;
	uint8_t *output_row_ptr = output;
	int v210_pitch = input_pitch/sizeof(uint32_t);
	int row;

	// The output pitch should be a positive number (no image inversion)
	assert(v210_pitch > 0);

	for (row = 0; row < height; row++)
	{
		// Repack the row of 10-bit pixels into 8-bit pixels
		ConvertV210RowToPackedYUV((uint8_t *)v210_row_ptr, output_row_ptr, width, buffer);

		// Advance to the next rows in the input and output images
		v210_row_ptr += v210_pitch;
		output_row_ptr += output_pitch;
	}
}
#endif
//#endif


//#if BUILD_PROSPECT
// Convert packed 10-bit YUV 4:2:2 to rows of 16-bit luma and chroma
void ConvertV210ToYR16(uint8_t *input, int width, int height, int input_pitch,
					   uint8_t *output, int output_pitch, uint8_t *buffer)
{
	uint32_t *v210_row_ptr = (uint32_t *)input;
	uint8_t *output_row_ptr = output;
	int v210_pitch = input_pitch/sizeof(uint32_t);
	int row;

	// The input pitch should be a positive number (no image inversion)
	assert(v210_pitch > 0);

	for (row = 0; row < height; row++)
	{
		PIXEL16U *y_row_ptr = (PIXEL16U *)output_row_ptr;
		PIXEL16U *u_row_ptr = y_row_ptr + width;
		PIXEL16U *v_row_ptr = u_row_ptr + width/2;

		// Repack the row of 10-bit pixels into 16-bit pixels
		ConvertV210RowToYUV16((uint8_t *)v210_row_ptr, y_row_ptr, u_row_ptr, v_row_ptr, width, buffer);

		// Advance to the next rows in the input and output images
		v210_row_ptr += v210_pitch;
		output_row_ptr += output_pitch;
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert one row of 10-bit YUV padded to 32 bits to 16-bit YUV
void ConvertV210RowToYUV(uint8_t *input, PIXEL *output, int length)
{
	uint32_t *input_ptr = (uint32_t *)input;
	PIXEL *output_ptr = output;
	int i;

	// Must have an even number of pixels
	assert((length % 2) == 0);

	for (i = 0; i < length; i += 6)
	{
		uint32_t yuv;

		int y;
		int u;
		int v;

		// Note: This routine swaps the chroma values

		// Read the first word
		yuv = *(input_ptr++);

		u = (yuv >> V210_VALUE1_SHIFT) & (V210_VALUE_MASK);
		y = (yuv >> V210_VALUE2_SHIFT) & (V210_VALUE_MASK);
		v = (yuv >> V210_VALUE3_SHIFT) & (V210_VALUE_MASK);

		*(output_ptr++) = y;
		*(output_ptr++) = v;

		// Read the second word
		yuv = *(input_ptr++);

		y = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;

		*(output_ptr++) = y;
		*(output_ptr++) = u;

		u = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
		y = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

		*(output_ptr++) = y;
		*(output_ptr++) = v;

		// Read the third word
		yuv = *(input_ptr++);

		v = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;
		y = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;

		*(output_ptr++) = y;
		*(output_ptr++) = u;

		u = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

		// Read the fourth word
		yuv = *(input_ptr++);
		y = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;

		*(output_ptr++) = y;
		*(output_ptr++) = v;

		v = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
		y = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

		*(output_ptr++) = y;
		*(output_ptr++) = u;
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert one row of 10-bit YUV padded to packed 8-bit YUV

#if DANREMOVE 
void ConvertV210RowToPackedYUV(uint8_t *input, uint8_t *output, int length, uint8_t *buffer)
{
	uint32_t *input_ptr = (uint32_t *)input;
	uint8_t *output_ptr = output;
	int column = 0;

#if (1 && XMMOPT)

	const int column_step = 24;
	int post_column = length - (length % column_step);

	__m128i *input_group_ptr;
	__m128i *output_group_ptr;

	// Does the input row have the required alignment for fast unpacking?
	if (!ISALIGNED16(input))
	{
		// Compute the size of the input row (in bytes)
		size_t row_size = (2 * length * sizeof(uint32_t)) / 3;

		// Check that the buffer is properly aligned
		assert(ISALIGNED16(buffer));

		// Copy the input data into a buffer with aligned memory
		CopyMemory(buffer, input, row_size);

		// Load the input from the buffer
		input_ptr = (uint32_t *)buffer;
	}

	input_group_ptr = (__m128i *)input_ptr;
	output_group_ptr = (__m128i *)output_ptr;

	for (; column < post_column; column += column_step)
	{
		const __m128i mask_epi32 = _mm_set1_epi32(V210_VALUE_MASK);

		__m128i input_si128;

		__m128i yuv1_epi32;
		__m128i yuv2_epi32;
		__m128i yuv3_epi32;

		__m128i y1_epi64;
		__m128i y2_epi64;
		__m128i y3_epi64;

		__m128i yy1_epi32;
		__m128i yy2_epi32;
		__m128i yy3_epi32;
		__m128i yy4_epi32;

		__m128i yyyy1_epi32;
		__m128i yyyy2_epi32;
		__m128i yyyy3_epi32;

		__m128i uv1_epi64;
		__m128i uv2_epi64;
		__m128i uv3_epi64;
		__m128i uv4_epi64;
		__m128i uv5_epi64;
		__m128i uv6_epi64;

		__m128i u1_epi32;
		__m128i v1_epi32;
		__m128i u2_epi32;
		__m128i v2_epi32;

		__m128i u3_epi32;
		__m128i v3_epi32;
		__m128i u4_epi32;
		__m128i v4_epi32;

		__m128i u5_epi32;
		__m128i v5_epi32;
		__m128i u6_epi32;
		__m128i v6_epi32;

		__m128i uuuu1_epi32;
		__m128i uuuu2_epi32;
		__m128i uuuu3_epi32;
		__m128i vvvv1_epi32;
		__m128i vvvv2_epi32;
		__m128i vvvv3_epi32;

		__m128i uv1_epi32;
		__m128i uv2_epi32;
		__m128i uv3_epi32;
		__m128i uv4_epi32;
		__m128i uv5_epi32;
		__m128i uv6_epi32;

		__m128i uv1_epi16;
		__m128i uv2_epi16;
		__m128i uv3_epi16;
		//__m128i uv4_epi16;

		__m128i y1_epi16;
		__m128i y2_epi16;
		__m128i y3_epi16;
		//__m128i u1_epi16;
		//__m128i u2_epi16;
		//__m128i u3_epi16;
		//__m128i v1_epi16;
		//__m128i v2_epi16;
		//__m128i v3_epi16;

		__m128i yuv1_epi16;
		__m128i yuv2_epi16;

		__m128i yuv1_epi8;
		__m128i yuv2_epi8;
		__m128i yuv3_epi8;


		/***** Phase One *****/

		// Read the first group of six packed 10-bit pixels
		input_si128 = _mm_load_si128(input_group_ptr++);

		// Unpack the twelve luma and chroma values into three sets of values
		yuv1_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv2_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv3_epi32 = _mm_and_si128(input_si128, mask_epi32);

		// Unpack the luma values
		y1_epi64 = _mm_and_si128(yuv2_epi32, _mm_set1_epi64(_m_from_int(V210_VALUE_MASK)));
		y2_epi64 = _mm_srli_epi64(yuv1_epi32, 32);
		y3_epi64 = _mm_srli_epi64(yuv3_epi32, 32);

		// Repack the luma values in the correct order
		yy1_epi32 = _mm_unpacklo_epi64(y1_epi64, y2_epi64);
		yy2_epi32 = _mm_unpackhi_epi64(y1_epi64, y2_epi64);

		yy3_epi32 = _mm_unpacklo_epi64(y3_epi64, yy2_epi32);
		yy4_epi32 = _mm_unpackhi_epi64(yy2_epi32, y3_epi64);

		// Pack the first four luma values
		yyyy1_epi32 = _mm_packs_epi32(yy1_epi32, yy3_epi32);

		// Unpack the chroma values
		uv1_epi64 = _mm_and_si128(yuv1_epi32, _mm_set1_epi64(_m_from_int(V210_VALUE_MASK)));
		uv2_epi64 = _mm_srli_epi64(yuv2_epi32, 32);
		uv3_epi64 = _mm_and_si128(yuv3_epi32, _mm_set1_epi64(_m_from_int(V210_VALUE_MASK)));

		// Repack the first pair of chroma values
		u1_epi32 = _mm_unpacklo_epi32(uv1_epi64, uv2_epi64);
		uv3_epi64 = _mm_shuffle_epi32(uv3_epi64, _MM_SHUFFLE(1, 0, 3, 2));
		v1_epi32 = _mm_unpackhi_epi32(uv3_epi64, uv1_epi64);


		/***** Phase Two *****/

		// Read the second group of six packed 10-bit pixels
		input_si128 = _mm_load_si128(input_group_ptr++);

		// Unpack the twelve luma and chroma values into three sets of values
		yuv1_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv2_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv3_epi32 = _mm_and_si128(input_si128, mask_epi32);

		// Unpack the luma values
		y1_epi64 = _mm_and_si128(yuv2_epi32, _mm_set1_epi64(_m_from_int(V210_VALUE_MASK)));
		y2_epi64 = _mm_srli_epi64(yuv1_epi32, 32);
		y3_epi64 = _mm_srli_epi64(yuv3_epi32, 32);

		// Repack the luma values in the correct order
		yy1_epi32 = _mm_unpacklo_epi64(y1_epi64, y2_epi64);
		yy2_epi32 = _mm_unpackhi_epi64(y1_epi64, y2_epi64);

		// Combine the first two luma values with two values from the previous phase
		yyyy2_epi32 = _mm_packs_epi32(yy4_epi32, yy1_epi32);

		// Repack the last four of six luma values from this phase
		yyyy3_epi32 = _mm_packs_epi32(y3_epi64, yy2_epi32);
		yyyy3_epi32 = _mm_shuffle_epi32(yyyy3_epi32, _MM_SHUFFLE(1, 3, 2, 0));

		// Unpack the chroma values
		uv4_epi64 = _mm_and_si128(yuv1_epi32, _mm_set1_epi64(_m_from_int(V210_VALUE_MASK)));
		uv5_epi64 = _mm_srli_epi64(yuv2_epi32, 32);
		uv6_epi64 = _mm_and_si128(yuv3_epi32, _mm_set1_epi64(_m_from_int(V210_VALUE_MASK)));

		// Repack the second pair of chroma values
		u2_epi32 = _mm_unpacklo_epi32(uv3_epi64, uv4_epi64);
		uv2_epi64 = _mm_shuffle_epi32(uv2_epi64, _MM_SHUFFLE(1, 0, 3, 2));
		v2_epi32 = _mm_unpacklo_epi32(uv2_epi64, uv6_epi64);

		// Repack the third pair of chroma values
		v3_epi32 = _mm_unpackhi_epi32(uv4_epi64, uv5_epi64);
		uv6_epi64 = _mm_shuffle_epi32(uv6_epi64, _MM_SHUFFLE(1, 0, 3, 2));
		u3_epi32 = _mm_unpacklo_epi32(uv5_epi64, uv6_epi64);

		// Combine the first and second pairs of chroma values
		uuuu1_epi32 = _mm_unpacklo_epi64(u1_epi32, u2_epi32);
		vvvv1_epi32 = _mm_unpacklo_epi64(v1_epi32, v2_epi32);

		// Interleave the first four chroma values
		uv1_epi32 = _mm_unpacklo_epi32(uuuu1_epi32, vvvv1_epi32);
		uv2_epi32 = _mm_unpackhi_epi32(uuuu1_epi32, vvvv1_epi32);

		// Pack the eight chroma values
		uv1_epi16 = _mm_packs_epi32(uv1_epi32, uv2_epi32);

		// Combine the first eight luma values
		y1_epi16 = _mm_packs_epi32(yyyy1_epi32, yyyy2_epi32);

		// Interleave the luma and chroma values
		yuv1_epi16 = _mm_unpacklo_epi16(y1_epi16, uv1_epi16);
		yuv2_epi16 = _mm_unpackhi_epi16(y1_epi16, uv1_epi16);

		// Reduce the number of bits
		yuv1_epi16 = _mm_srli_epi16(yuv1_epi16, 2);
		yuv2_epi16 = _mm_srli_epi16(yuv2_epi16, 2);

		// Pack the luma and chroma as eight bit pixels
		yuv1_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

		// Store the first eight pixels (luma and chroma values)
		_mm_store_si128(output_group_ptr++, yuv1_epi8);


		/***** Phase Three *****/

		// Read the third group of six packed 10-bit pixels
		input_si128 = _mm_load_si128(input_group_ptr++);

		// Unpack the twelve luma and chroma values into three sets of values
		yuv1_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv2_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv3_epi32 = _mm_and_si128(input_si128, mask_epi32);

		// Unpack the luma values
		y1_epi64 = _mm_and_si128(yuv2_epi32, _mm_set1_epi64(_m_from_int(V210_VALUE_MASK)));
		y2_epi64 = _mm_srli_epi64(yuv1_epi32, 32);
		y3_epi64 = _mm_srli_epi64(yuv3_epi32, 32);

		// Repack the luma values in the correct order
		yy1_epi32 = _mm_unpacklo_epi64(y1_epi64, y2_epi64);
		yy2_epi32 = _mm_unpackhi_epi64(y1_epi64, y2_epi64);

		yy3_epi32 = _mm_unpacklo_epi64(y3_epi64, yy2_epi32);
		yy4_epi32 = _mm_unpackhi_epi64(yy2_epi32, y3_epi64);

		// Pack the first four luma values from this phase
		yyyy1_epi32 = _mm_packs_epi32(yy1_epi32, yy3_epi32);

		// Combine the four luma values with four luma from the last phase
		y2_epi16 = _mm_packs_epi32(yyyy3_epi32, yyyy1_epi32);

		// Unpack the chroma values
		uv1_epi64 = _mm_and_si128(yuv1_epi32, _mm_set1_epi64(_m_from_int(V210_VALUE_MASK)));
		uv2_epi64 = _mm_srli_epi64(yuv2_epi32, 32);
		uv3_epi64 = _mm_and_si128(yuv3_epi32, _mm_set1_epi64(_m_from_int(V210_VALUE_MASK)));

		// Repack the fourth pair of chroma values
		u4_epi32 = _mm_unpacklo_epi32(uv1_epi64, uv2_epi64);
		uv3_epi64 = _mm_shuffle_epi32(uv3_epi64, _MM_SHUFFLE(1, 0, 3, 2));
		v4_epi32 = _mm_unpackhi_epi32(uv3_epi64, uv1_epi64);

		// Combine the third and fourth pairs of chroma values
		uuuu2_epi32 = _mm_unpacklo_epi64(u3_epi32, u4_epi32);
		vvvv2_epi32 = _mm_unpacklo_epi64(v3_epi32, v4_epi32);

		// Interleave the chroma values
		uv3_epi32 = _mm_unpacklo_epi32(uuuu2_epi32, vvvv2_epi32);
		uv4_epi32 = _mm_unpackhi_epi32(uuuu2_epi32, vvvv2_epi32);

		// Pack the eight chroma values
		uv2_epi16 = _mm_packs_epi32(uv3_epi32, uv4_epi32);

		// Interleave the luma and chroma values
		yuv1_epi16 = _mm_unpacklo_epi16(y2_epi16, uv2_epi16);
		yuv2_epi16 = _mm_unpackhi_epi16(y2_epi16, uv2_epi16);

		// Reduce the number of bits
		yuv1_epi16 = _mm_srli_epi16(yuv1_epi16, 2);
		yuv2_epi16 = _mm_srli_epi16(yuv2_epi16, 2);

		// Pack the luma and chroma as eight bit pixels
		yuv2_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

		// Store the second eight pixels (luma and chroma values)
		_mm_store_si128(output_group_ptr++, yuv2_epi8);


		/***** Phase Four *****/

		// Read the fourth group of six packed 10-bit pixels
		input_si128 = _mm_load_si128(input_group_ptr++);

		// Unpack the twelve luma and chroma values into three sets of values
		yuv1_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv2_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv3_epi32 = _mm_and_si128(input_si128, mask_epi32);

		// Unpack the luma values
		y1_epi64 = _mm_and_si128(yuv2_epi32, _mm_set1_epi64(_m_from_int(V210_VALUE_MASK)));
		y2_epi64 = _mm_srli_epi64(yuv1_epi32, 32);
		y3_epi64 = _mm_srli_epi64(yuv3_epi32, 32);

		// Repack the luma values in the correct order
		yy1_epi32 = _mm_unpacklo_epi64(y1_epi64, y2_epi64);
		yy2_epi32 = _mm_unpackhi_epi64(y1_epi64, y2_epi64);

		// Combine the first two luma values with two values from the previous phase
		yyyy2_epi32 = _mm_packs_epi32(yy4_epi32, yy1_epi32);

		// Repack the last four of six luma values from this phase
		yyyy3_epi32 = _mm_packs_epi32(y3_epi64, yy2_epi32);
		yyyy3_epi32 = _mm_shuffle_epi32(yyyy3_epi32, _MM_SHUFFLE(1, 3, 2, 0));

		// Unpack the chroma values
		uv4_epi64 = _mm_and_si128(yuv1_epi32, _mm_set1_epi64(_m_from_int(V210_VALUE_MASK)));
		uv5_epi64 = _mm_srli_epi64(yuv2_epi32, 32);
		uv6_epi64 = _mm_and_si128(yuv3_epi32, _mm_set1_epi64(_m_from_int(V210_VALUE_MASK)));

		// Repack the fifth pair of chroma values
		u5_epi32 = _mm_unpacklo_epi32(uv3_epi64, uv4_epi64);
		uv2_epi64 = _mm_shuffle_epi32(uv2_epi64, _MM_SHUFFLE(1, 0, 3, 2));
		v5_epi32 = _mm_unpacklo_epi32(uv2_epi64, uv6_epi64);

		// Repack the sixth pair of chroma values
		v6_epi32 = _mm_unpackhi_epi32(uv4_epi64, uv5_epi64);
		uv6_epi64 = _mm_shuffle_epi32(uv6_epi64, _MM_SHUFFLE(1, 0, 3, 2));
		u6_epi32 = _mm_unpacklo_epi32(uv5_epi64, uv6_epi64);

		// Combine the third eight luma values
		y3_epi16 = _mm_packs_epi32(yyyy2_epi32, yyyy3_epi32);

		// Combine the fifth and sixth pairs of chroma values
		uuuu3_epi32 = _mm_unpacklo_epi64(u5_epi32, u6_epi32);
		vvvv3_epi32 = _mm_unpacklo_epi64(v5_epi32, v6_epi32);

		// Interleave the chroma values
		uv5_epi32 = _mm_unpacklo_epi32(uuuu3_epi32, vvvv3_epi32);
		uv6_epi32 = _mm_unpackhi_epi32(uuuu3_epi32, vvvv3_epi32);

		// Pack the eight chroma values
		uv3_epi16 = _mm_packs_epi32(uv5_epi32, uv6_epi32);

		// Interleave the luma and chroma values
		yuv1_epi16 = _mm_unpacklo_epi16(y3_epi16, uv3_epi16);
		yuv2_epi16 = _mm_unpackhi_epi16(y3_epi16, uv3_epi16);

		// Reduce the number of bits
		yuv1_epi16 = _mm_srli_epi16(yuv1_epi16, 2);
		yuv2_epi16 = _mm_srli_epi16(yuv2_epi16, 2);

		// Pack the luma and chroma as eight bit pixels
		yuv3_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

		// Store the third eight pixels (luma and chroma values)
		_mm_store_si128(output_group_ptr++, yuv3_epi8);
	}

	//_mm_empty();		// Clear the mmx register state

	// Should have exited the fast loop at the post processing column
	assert(column == post_column);

#endif

	// Must have an integer number of four word groups
//	assert((length % 6) == 0);
//	length -= length % 6; //DAN03252004 -- fix a memory overflow.

	for (; column < length - (length % 6); column += 6)
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

		// Reduce the pixels to eight bits
		u >>= 2;
		y >>= 2;
		v >>= 2;

		*(output_ptr++) = SATURATE_8U(y);
		*(output_ptr++) = SATURATE_8U(u);

		// Read the second word
		yuv = *(input_ptr++);

		y = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;

		y >>= 2;

		*(output_ptr++) = SATURATE_8U(y);
		*(output_ptr++) = SATURATE_8U(v);

		u = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
		y = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

		u >>= 2;
		y >>= 2;

		*(output_ptr++) = SATURATE_8U(y);
		*(output_ptr++) = SATURATE_8U(u);

		// Read the third word
		yuv = *(input_ptr++);

		v = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;
		y = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;

		v >>= 2;
		y >>= 2;

		*(output_ptr++) = SATURATE_8U(y);
		*(output_ptr++) = SATURATE_8U(v);

		u = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;
		u >>= 2;

		// Read the fourth word
		yuv = *(input_ptr++);

		y = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;
		y >>= 2;

		*(output_ptr++) = SATURATE_8U(y);
		*(output_ptr++) = SATURATE_8U(u);

		v = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
		y = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

		v >>= 2;
		y >>= 2;

		*(output_ptr++) = SATURATE_8U(y);
		*(output_ptr++) = SATURATE_8U(v);
	}
}
#endif
//#endif

//#if BUILD_PROSPECT
// Convert one row of 10-bit YUV padded to rows of 16-bit luma and chroma
void ConvertV210RowToYUV16(uint8_t *input, PIXEL16U *y_output, PIXEL16U *u_output, PIXEL16U *v_output,
						   int length, uint8_t *buffer)
{
	uint32_t *input_ptr = (uint32_t *)input;
	PIXEL16U *y_output_ptr = y_output;
	PIXEL16U *u_output_ptr = u_output;
	PIXEL16U *v_output_ptr = v_output;
	int column = 0;

	// Identify unused parameters to suppress compiler warnings
	(void) buffer;

	// Must have an integer number of four word groups
	assert((length % 6) == 0);
	length -= length % 6; //DAN03252004 -- fix a memory overflow.

	for (; column < length; column += 6)
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

		*(y_output_ptr++) = SATURATE_16U(y);
		*(u_output_ptr++) = SATURATE_16U(u);

		// Read the second word
		yuv = *(input_ptr++);

		y = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;

		y <<= 6;

		*(y_output_ptr++) = SATURATE_16U(y);
		*(v_output_ptr++) = SATURATE_16U(v);

		u = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
		y = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

		u <<= 6;
		y <<= 6;

		*(y_output_ptr++) = SATURATE_16U(y);
		*(u_output_ptr++) = SATURATE_16U(u);

		// Read the third word
		yuv = *(input_ptr++);

		v = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;
		y = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;

		v <<= 6;
		y <<= 6;

		*(y_output_ptr++) = SATURATE_16U(y);
		*(v_output_ptr++) = SATURATE_16U(v);

		u = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;
		u <<= 6;

		// Read the fourth word
		yuv = *(input_ptr++);

		y = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;
		y <<= 6;

		*(y_output_ptr++) = SATURATE_16U(y);
		*(u_output_ptr++) = SATURATE_16U(u);

		v = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
		y = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

		v <<= 6;
		y <<= 6;

		*(y_output_ptr++) = SATURATE_16U(y);
		*(v_output_ptr++) = SATURATE_16U(v);
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert one row of 64-bit YUV padded to 32 bits to 10-bit precision YUV
void ConvertYU64RowToYUV10bit(uint8_t *input, PIXEL *output, int length)
{
	uint32_t *input_ptr = (uint32_t *)input;
	uint32_t *output_ptr = (uint32_t *)output;
	int i;

	// Must have an even number of pixels
	assert((length % 2) == 0);

	for (i = 0; i < length; i+=2)
	{
		*output_ptr++ = ((*input_ptr++)>>6) & 0x03ff03ff;
		*output_ptr++ = ((*input_ptr++)>>6) & 0x03ff03ff;
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert one row of YUV bytes to 10-bit YUV padded to 32 bits
void ConvertYUVRowToV210(uint8_t *input, uint8_t *output, int length)
{
	uint8_t *input_ptr = input;
	uint32_t *output_ptr = (uint32_t *)output;
	int i;

	// Must have an integral number of packed pixels
	assert((length % 6) == 0);

	for (i = 0; i < length; i += 6)
	{
		uint32_t yuv;

		// Read the first twelve bytes of YUV
		int y1 = *(input_ptr++);
		int u1 = *(input_ptr++);
		int y2 = *(input_ptr++);
		int v1 = *(input_ptr++);

		int y3 = *(input_ptr++);
		int u2 = *(input_ptr++);
		int y4 = *(input_ptr++);
		int v2 = *(input_ptr++);

		int y5 = *(input_ptr++);
		int u3 = *(input_ptr++);
		int y6 = *(input_ptr++);
		int v3 = *(input_ptr++);

		// Assemble and store the first word of packed values
		yuv = (v1 << V210_VALUE3_SHIFT) | (y1 << V210_VALUE2_SHIFT) | (u1 << V210_VALUE1_SHIFT);
		*(output_ptr++) = yuv;

		// Assemble and store the second word of packed values
		yuv = (y3 << V210_VALUE3_SHIFT) | (u2 << V210_VALUE2_SHIFT) | (y2 << V210_VALUE1_SHIFT);
		*(output_ptr++) = yuv;

		// Assemble and store the third word of packed values
		yuv = (u3 << V210_VALUE3_SHIFT) | (y4 << V210_VALUE2_SHIFT) | (v2 << V210_VALUE1_SHIFT);
		*(output_ptr++) = yuv;

		// Assemble and store the fourth word of packed values
		yuv = (y6 << V210_VALUE3_SHIFT) | (v3 << V210_VALUE2_SHIFT) | (y5 << V210_VALUE1_SHIFT);
		*(output_ptr++) = yuv;
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert one row of 16-bit YUV values to 10-bit YUV padded to 32 bits
void ConvertYUV16sRowToV210(PIXEL *input, uint8_t *output, int frame_width)
{
	PIXEL *input_ptr = input;
	uint32_t *v210_output_ptr = (uint32_t *)output;

	// Process six pixels in each group of four double words
	const int v210_column_step = 6;

	// Reduce the width to a multiple pixels packed in four double words
	//int v210_fast_width = output_width - (output_width % v210_column_step);
	int v210_fast_width = frame_width - (frame_width % v210_column_step);

	// Compute the ending column in the row of luma and chroma values
	int end_column = 2 * frame_width;

	// Start processing at the leftmost column
	int column = 0;

#if (1 && XMMOPT)

	// Process twelve values of luma and chroma per loop iteration
	const int column_step = 2 * v210_column_step;
	const int fast_width = 2 * v210_fast_width;

	// Column at which post processing must begin
	int post_column = fast_width - (fast_width % column_step);

	__m128i *output_ptr = (__m128i *)v210_output_ptr;

	// Must process an integer number of four double word groups
	assert((post_column % column_step) == 0);

	for (; column < post_column; column += column_step)
	{
		__m128i yuv1_epi32;
		__m128i yuv2_epi32;
		__m128i yuv3_epi32;
		//__m128i mask_epi32;

		// Four double words in packed V210 format
		__m128i v210_epi32;

		//TODO: Implement faster algorithm for packing v210

		// Load the first group of four pixels
		yuv1_epi32 = _mm_setr_epi32(input[column + 3],  //v0
									input[column + 4],  //y2
									input[column + 9],  //u2
									input[column + 10]);//y5

		// Load the second group of four pixels
		yuv2_epi32 = _mm_setr_epi32(input[column + 0],  //y0
									input[column + 5],  //u1
									input[column + 6],  //y3
									input[column + 11]);//v2

		// Load the third group of four pixels
		yuv3_epi32 = _mm_setr_epi32(input[column + 1],  //u0
									input[column + 2],  //y1
									input[column + 7],  //v1
									input[column + 8]); //y4

		// Pack the first group of pixels into the V210 output
		v210_epi32 = yuv1_epi32;
		v210_epi32 = _mm_slli_epi32(v210_epi32, V210_VALUE2_SHIFT);

		// Pack the second group of pixels into the V210 output
		v210_epi32 = _mm_or_si128(v210_epi32, yuv2_epi32);
		v210_epi32 = _mm_slli_epi32(v210_epi32, V210_VALUE2_SHIFT);

		// Pack the third group of pixels into the V210 output
		v210_epi32 = _mm_or_si128(v210_epi32, yuv3_epi32);

		// Store the group of V210 packed pixels
		_mm_store_si128(output_ptr++, v210_epi32);
	}

	// Should have exited the loop at the post processing column
	assert(column == post_column);

	input_ptr = &input[column];
	v210_output_ptr = (uint32_t *)output_ptr;

#endif

	// Must process an integer number of four double word groups
	//assert((end_column % column_step) == 0);

	// Note: The fast loop processes 12 luma and chroma values per iteration and
	// has the same end condition so the post processing loop is not necessary

	// Pack luma and chroma values (12 per iteration) up to the edge of the frame
	for (; column < end_column; column += column_step)
	{
		uint32_t yuv;

		// Read the first twelve YUV values
		int y1 = *(input_ptr++);
		int u1 = *(input_ptr++);
		int y2 = *(input_ptr++);
		int v1 = *(input_ptr++);

		int y3 = *(input_ptr++);
		int u2 = *(input_ptr++);
		int y4 = *(input_ptr++);
		int v2 = *(input_ptr++);

		int y5 = *(input_ptr++);
		int u3 = *(input_ptr++);
		int y6 = *(input_ptr++);
		int v3 = *(input_ptr++);

		// Assemble and store the first word of packed values
		yuv = (v1 << V210_VALUE3_SHIFT) | (y1 << V210_VALUE2_SHIFT) | (u1 << V210_VALUE1_SHIFT);
		*(v210_output_ptr++) = yuv;

		// Assemble and store the second word of packed values
		yuv = (y3 << V210_VALUE3_SHIFT) | (u2 << V210_VALUE2_SHIFT) | (y2 << V210_VALUE1_SHIFT);
		*(v210_output_ptr++) = yuv;

		// Assemble and store the third word of packed values
		yuv = (u3 << V210_VALUE3_SHIFT) | (y4 << V210_VALUE2_SHIFT) | (v2 << V210_VALUE1_SHIFT);
		*(v210_output_ptr++) = yuv;

		// Assemble and store the fourth word of packed values
		yuv = (y6 << V210_VALUE3_SHIFT) | (v3 << V210_VALUE2_SHIFT) | (y5 << V210_VALUE1_SHIFT);
		*(v210_output_ptr++) = yuv;
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert one row of 16-bit luma and chroma to packed 8-bit YUV
void ConvertYUV16uRowToYUV(PIXEL16U *y_input, PIXEL16U *u_input, PIXEL16U *v_input, uint8_t *yuv_output, int length)
{
	// Note: This routine swaps the chroma values
	PIXEL16U *y_input_ptr = y_input;
	PIXEL16U *u_input_ptr = v_input;
	PIXEL16U *v_input_ptr = u_input;

	uint8_t *yuv_output_ptr = yuv_output;

	// The scale shift for converting 16-bit pixels to 8-bit pixels
	const int descale = 8;

	// Start processing at the leftmost column
	int column = 0;

#if (1 && XMMOPT)

	// Process sixteen values of luma and chroma per loop iteration
	const int column_step = 16;

	// Column at which post processing must begin
	//int post_column = fast_width - (fast_width % column_step);
	int post_column = length - (length % column_step);

	// Initialize the input pointers into each channel
	__m128i *y_ptr = (__m128i *)y_input_ptr;
	__m128i *u_ptr = (__m128i *)u_input_ptr;
	__m128i *v_ptr = (__m128i *)v_input_ptr;

	// Initialize the output pointer for the packed YUV results
	__m128i *yuv_ptr = (__m128i *)yuv_output_ptr;

	for (; column < post_column; column += column_step)
	{
		//int chroma_column = column/2;

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

		// Interleave the third four luma values with the chroma pairs
		yuv1_epi16 = _mm_unpacklo_epi16(y2_epi16, uv_epi16);

		// Interleave the fourth four luma values with the chroma pairs
		yuv2_epi16 = _mm_unpackhi_epi16(y2_epi16, uv_epi16);

		// Pack the second eight luma and chroma pairs
		yuv_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

		// Store the second eight luma and chroma pairs
		_mm_store_si128(yuv_ptr++, yuv_epi8);
	}

#endif

	// Need to handle the rest of the conversion outside of the fast loop
}
//#endif


//#if BUILD_PROSPECT
// Convert one row of 16-bit YUV values to 10-bit YUV padded to 32 bits
void ConvertYUV16uRowToV210(PIXEL16U *y_input, PIXEL16U *u_input, PIXEL16U *v_input, uint32_t *output, int length)
{
	// Note: This routine swaps the chroma values
	PIXEL16U *y_input_ptr = y_input;
	PIXEL16U *u_input_ptr = v_input;
	PIXEL16U *v_input_ptr = u_input;

	uint32_t *v210_output_ptr = output;

	// Process six pixels in each group of four double words
	const int v210_column_step = 6;

	// Reduce the width to a multiple pixels packed in four double words
	int v210_width = length - (length % v210_column_step);

	// The scale shift for converting 16-bit pixels to 10-bit pixels
	const int descale = 6;

	// Start processing at the leftmost column
	int column = 0;

#if (1 && XMMOPT)

	// Process twelve values of luma and chroma per loop iteration
	const int column_step = 6;
	//const int fast_width = 2 * v210_width;

	// Column at which post processing must begin
	//int post_column = fast_width - (fast_width % column_step);
	int post_column = v210_width - (v210_width % column_step);

	__m128i *output_ptr = (__m128i *)v210_output_ptr;

	// Must process and integer number of four double word groups
	assert((post_column % v210_column_step) == 0);

	for (; column < post_column; column += column_step)
	{
		int chroma_column = column/2;

		__m128i yuv1_epi32;
		__m128i yuv2_epi32;
		__m128i yuv3_epi32;
		//__m128i mask_epi32;

		//__m128i limit_epi32 = _mm_set1_epi32(_m_from_int(V210_VALUE_MASK));

		// Four double words in packed V210 format
		__m128i v210_epi32;

		// Load the first group of four pixels
		yuv1_epi32 = _mm_setr_epi32(v_input_ptr[chroma_column + 0],
									y_input_ptr[column + 2],
									u_input_ptr[chroma_column + 2],
									y_input_ptr[column + 5]);

		// Load the second group of four pixels
		yuv2_epi32 = _mm_setr_epi32(y_input_ptr[column + 0],
									u_input_ptr[chroma_column + 1],
									y_input_ptr[column + 3],
									v_input_ptr[chroma_column + 2]);

		// Load the third group of four pixels
		yuv3_epi32 = _mm_setr_epi32(u_input_ptr[chroma_column + 0],
									y_input_ptr[column + 1],
									v_input_ptr[chroma_column + 1],
									y_input_ptr[column + 4]);

		// Reduce the scale to 10 bits
		yuv1_epi32 = _mm_srli_epi32(yuv1_epi32, descale);
		yuv2_epi32 = _mm_srli_epi32(yuv2_epi32, descale);
		yuv3_epi32 = _mm_srli_epi32(yuv3_epi32, descale);
#if 0
		// Saturate the pixels
		mask_epi32 = _mm_cmpgt_epi32(yuv1_epi32, limit_epi32);
		yuv1_epi32 = _mm_andnot_si128(mask_epi32, yuv1_epi32);
		mask_epi32 = _mm_and_si128(mask_epi32, limit_epi32);
		yuv1_epi32 = _mm_or_si128(yuv1_epi32, mask_epi32);

		// Saturate the pixels
		mask_epi32 = _mm_cmpgt_epi32(yuv2_epi32, limit_epi32);
		yuv2_epi32 = _mm_andnot_si128(mask_epi32, yuv2_epi32);
		mask_epi32 = _mm_and_si128(mask_epi32, limit_epi32);
		yuv2_epi32 = _mm_or_si128(yuv2_epi32, mask_epi32);

		// Saturate the pixels
		mask_epi32 = _mm_cmpgt_epi32(yuv3_epi32, limit_epi32);
		yuv3_epi32 = _mm_andnot_si128(mask_epi32, yuv3_epi32);
		mask_epi32 = _mm_and_si128(mask_epi32, limit_epi32);
		yuv3_epi32 = _mm_or_si128(yuv3_epi32, mask_epi32);
#endif
		// Pack the first group of pixels into the V210 output
		v210_epi32 = yuv1_epi32;
		v210_epi32 = _mm_slli_epi32(v210_epi32, V210_VALUE2_SHIFT);

		// Pack the second group of pixels into the V210 output
		v210_epi32 = _mm_or_si128(v210_epi32, yuv2_epi32);
		v210_epi32 = _mm_slli_epi32(v210_epi32, V210_VALUE2_SHIFT);

		// Pack the third group of pixels into the V210 output
		v210_epi32 = _mm_or_si128(v210_epi32, yuv3_epi32);

		// Store the group of V210 packed pixels
		_mm_store_si128(output_ptr++, v210_epi32);
	}

	// Should have exited the loop at the post processing column
	assert(column == post_column);

	y_input_ptr = &y_input_ptr[column];
	u_input_ptr = &u_input_ptr[column/2];
	v_input_ptr = &v_input_ptr[column/2];
	v210_output_ptr = (uint32_t *)output_ptr;

#endif

	// Must process an integer number of four double word groups
	assert((v210_width % v210_column_step) == 0);

	//for (; column < v210_width; column += v210_column_step)
	if(length > v210_width)
	{
		uint32_t yuv;

		// Read the first twelve YUV values
		int y1 = *(y_input_ptr++) >> descale;
		int u1 = *(u_input_ptr++) >> descale;
		int y2 = *(y_input_ptr++) >> descale;
		int v1 = *(v_input_ptr++) >> descale;


		// Assemble and store the first word of packed values
		yuv = (v1 << V210_VALUE3_SHIFT) | (y1 << V210_VALUE2_SHIFT) | (u1 << V210_VALUE1_SHIFT);
		*(v210_output_ptr++) = yuv;

		// Assemble and store the second word of packed values
		yuv = (y1 << V210_VALUE3_SHIFT) | (u1 << V210_VALUE2_SHIFT) | (y2 << V210_VALUE1_SHIFT);
		*(v210_output_ptr++) = yuv;

		// Assemble and store the third word of packed values
		yuv = (u1 << V210_VALUE3_SHIFT) | (y2 << V210_VALUE2_SHIFT) | (v1 << V210_VALUE1_SHIFT);
		*(v210_output_ptr++) = yuv;

		// Assemble and store the fourth word of packed values
		yuv = (y2 << V210_VALUE3_SHIFT) | (v1 << V210_VALUE2_SHIFT) | (y1 << V210_VALUE1_SHIFT);
		*(v210_output_ptr++) = yuv;
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert rows of 16-bit luma and chroma to packed YUV
void ConvertYUV16ToYUV(uint8_t *input, int width, int height, int input_pitch,
					   uint8_t *output, int output_pitch, uint8_t *buffer)
{
	uint8_t *input_row_ptr = input;
	uint8_t *output_row_ptr = output;
	int row;

	// The input pitch should be a positive number (no image inversion)
	assert(input_pitch > 0);

	// The output pitch should be a positive number (no image inversion)
	assert(output_pitch > 0);

	for (row = 0; row < height; row++)
	{
		PIXEL16U *y_row_ptr;
		PIXEL16U *u_row_ptr;
		PIXEL16U *v_row_ptr;

		if (!ISALIGNED16(input))
		{
			// Compute the size of the input row (in bytes)
			size_t input_row_size = 2 * width * sizeof(PIXEL16U);

			// Check that the buffer was allocated
			assert(buffer != NULL);

			// Check that the buffer is properly aligned
			assert(ISALIGNED16(buffer));

			// Copy the input data into a buffer with aligned memory
			CopyMemory(buffer, input_row_ptr, input_row_size);

			// Load the input from the buffer
			y_row_ptr = (PIXEL16U *)buffer;
			u_row_ptr = y_row_ptr + width;
			v_row_ptr = u_row_ptr + width/2;
		}
		else
		{
			y_row_ptr = (PIXEL16U *)input_row_ptr;
			u_row_ptr = y_row_ptr + width;
			v_row_ptr = u_row_ptr + width/2;
		}

		// Convert one row of 16-bit luma and chroma to packed 8-bit YUV
		ConvertYUV16uRowToYUV(y_row_ptr, u_row_ptr, v_row_ptr, output_row_ptr, width);

		// Advance to the next rows in the input and output images
		input_row_ptr += input_pitch;
		output_row_ptr += output_pitch;
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert rows of 16-bit luma and chroma to V210 format
void ConvertYUV16ToV210(uint8_t *input, int width, int height, int input_pitch,
						uint8_t *output, int output_pitch, uint8_t *buffer)
{
	uint8_t *input_row_ptr = input;
	uint32_t *output_row_ptr = (uint32_t *)output;
	int row;

	// The input pitch should be a positive number (no image inversion)
	assert(input_pitch > 0);

	// Convert the input pitch to units of 16-bit pixels
	//input_pitch /= sizeof(PIXEL16U);

	// The output pitch should be a positive number (no image inversion)
	assert(output_pitch > 0);

	// Convert the output pitch to units of V210 32-bit words
	output_pitch /= sizeof(uint32_t);

	for (row = 0; row < height; row++)
	{
		PIXEL16U *y_row_ptr;
		PIXEL16U *u_row_ptr;
		PIXEL16U *v_row_ptr;

		if (!ISALIGNED16(input))
		{
			// Compute the size of the input row (in bytes)
			size_t input_row_size = 2 * width * sizeof(PIXEL16U);

			// Check that the buffer was allocated
			assert(buffer != NULL);

			// Check that the buffer is properly aligned
			assert(ISALIGNED16(buffer));

			// Copy the input data into a buffer with aligned memory
			CopyMemory(buffer, input_row_ptr, input_row_size);

			// Load the input from the buffer
			y_row_ptr = (PIXEL16U *)buffer;
			u_row_ptr = y_row_ptr + width;
			v_row_ptr = u_row_ptr + width/2;
		}
		else
		{
			y_row_ptr = (PIXEL16U *)input_row_ptr;
			u_row_ptr = y_row_ptr + width;
			v_row_ptr = u_row_ptr + width/2;
		}

		// Convert one row of 16-bit luma and chroma to V210 format
		ConvertYUV16uRowToV210(y_row_ptr, u_row_ptr, v_row_ptr, output_row_ptr, width);

		// Advance to the next rows in the input and output images
		input_row_ptr += input_pitch;
		output_row_ptr += output_pitch;
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert one row of 16-bit YUV values to 10-bit YUV padded to 32 bits
void ConvertYUV16sRowToYU64(PIXEL *input, uint8_t *output, int length)
{
	PIXEL *input_ptr = input;
	PIXEL *output_ptr = (PIXEL *)output;

	// Process six pixels in each group of four double words
	// Start processing at the leftmost column
	int column = 0;

	for (; column < length; column += 2)
	{
		// Read the first twelve YUV values
		int y1 = *(input_ptr++)<<6;
		int u = *(input_ptr++)<<6;
		int y2 = *(input_ptr++)<<6;
		int v = *(input_ptr++)<<6;

	/*	if(y1 > 0xffff) y1 = 0xffff;
		if(y1 < 0) y1 = 0;

		if(u > 0xffff) u = 0xffff;
		if(u < 0) u = 0;

		if(y2 > 0xffff) y2 = 0xffff;
		if(y2 < 0) y2 = 0;

		if(v > 0xffff) v = 0xffff;
		if(v < 0) v = 0;*/

		*(output_ptr++) = y1;
		*(output_ptr++) = v;
		*(output_ptr++) = y2;
		*(output_ptr++) = u;
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert one row of 10-bit YUV padded to three channels of 16-bit YUV
void ConvertV210RowToPlanar16s(uint8_t *input, int length, PIXEL *y_output, PIXEL *u_output, PIXEL *v_output)
{
	// Note: This routine swaps the chroma values

	uint32_t *yuv_ptr;
	PIXEL *y_ptr;
	PIXEL *u_ptr;
	PIXEL *v_ptr;

	// Start processing at the leftmost column
	int column = 0;

	__m128i *input_ptr = (__m128i *)input;
	__m128i *y_output_ptr = (__m128i *)y_output;
	__m128i *u_output_ptr = (__m128i *)v_output;
	__m128i *v_output_ptr = (__m128i *)u_output;

#if (1 && XMMOPT)

	int column_step = 48;
	int post_column = length - (length % column_step);

	//which replaces _mm_set1_epi64(_m_from_int(V210_VALUE_MASK))?
	const __m128i mask64_epi32 = _mm_setr_epi32(V210_VALUE_MASK, 0, V210_VALUE_MASK, 0);  

	// Must have an even number of pixels
	assert((length % 2) == 0);

	for (; column < post_column; column += column_step)
	{
		const __m128i mask_epi32 = _mm_set1_epi32(V210_VALUE_MASK);

		__m128i input_si128;

		__m128i yuv1_epi32;
		__m128i yuv2_epi32;
		__m128i yuv3_epi32;

		__m128i y1_epi32;
		__m128i y2_epi32;
		__m128i y3_epi32;

		__m128i yy1_epi32;
		__m128i yy2_epi32;
		__m128i yy3_epi32;
		__m128i yy4_epi32;

		__m128i yyyy1_epi32;
		__m128i yyyy2_epi32;
		__m128i yyyy3_epi32;

		__m128i uv1_epi32;
		__m128i uv2_epi32;
		__m128i uv3_epi32;
		__m128i uv4_epi32;
		__m128i uv5_epi32;
		__m128i uv6_epi32;

		__m128i u1_epi32;
		__m128i v1_epi32;
		__m128i u2_epi32;
		__m128i v2_epi32;

		__m128i u3_epi32;
		__m128i v3_epi32;
		__m128i u4_epi32;
		__m128i v4_epi32;

		__m128i u5_epi32;
		__m128i v5_epi32;
		__m128i u6_epi32;
		__m128i v6_epi32;

		__m128i uuuu1_epi32;
		__m128i uuuu2_epi32;
		__m128i uuuu3_epi32;
		__m128i vvvv1_epi32;
		__m128i vvvv2_epi32;
		__m128i vvvv3_epi32;

		__m128i y1_epi16;
		__m128i y2_epi16;
		__m128i y3_epi16;
		__m128i u1_epi16;
		__m128i u2_epi16;
		__m128i u3_epi16;
		__m128i v1_epi16;
		__m128i v2_epi16;
		__m128i v3_epi16;


		/***** Phase One *****/

		// Read the first group of six packed 10-bit pixels
		//input_si128 = _mm_loadu_si128(input_ptr++);
		input_si128 = _mm_load_si128(input_ptr++);

		// Unpack the twelve luma and chroma values into three sets of values
		yuv1_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv2_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv3_epi32 = _mm_and_si128(input_si128, mask_epi32);

		// Unpack the luma values
		y1_epi32 = _mm_and_si128(yuv2_epi32, mask64_epi32);
		y2_epi32 = _mm_srli_epi64(yuv1_epi32, 32);
		y3_epi32 = _mm_srli_epi64(yuv3_epi32, 32);

		// Repack the luma values in the correct order
		yy1_epi32 = _mm_unpacklo_epi64(y1_epi32, y2_epi32);
		yy2_epi32 = _mm_unpackhi_epi64(y1_epi32, y2_epi32);

		yy3_epi32 = _mm_unpacklo_epi64(y3_epi32, yy2_epi32);
		yy4_epi32 = _mm_unpackhi_epi64(yy2_epi32, y3_epi32);

		// Pack the first four luma values
		yyyy1_epi32 = _mm_packs_epi32(yy1_epi32, yy3_epi32);

		// Unpack the chroma values
		uv1_epi32 = _mm_and_si128(yuv1_epi32, mask64_epi32);
		uv2_epi32 = _mm_srli_epi64(yuv2_epi32, 32);
		uv3_epi32 = _mm_and_si128(yuv3_epi32, mask64_epi32);

		// Repack the first pair of chroma values
		u1_epi32 = _mm_unpacklo_epi32(uv1_epi32, uv2_epi32);
		uv3_epi32 = _mm_shuffle_epi32(uv3_epi32, _MM_SHUFFLE(1, 0, 3, 2));
		v1_epi32 = _mm_unpackhi_epi32(uv3_epi32, uv1_epi32);


		/***** Phase Two *****/

		// Read the second group of six packed 10-bit pixels
		//input_si128 = _mm_loadu_si128(input_ptr++);
		input_si128 = _mm_load_si128(input_ptr++);

		// Unpack the twelve luma and chroma values into three sets of values
		yuv1_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv2_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv3_epi32 = _mm_and_si128(input_si128, mask_epi32);

		// Unpack the luma values
		y1_epi32 = _mm_and_si128(yuv2_epi32, mask64_epi32);
		y2_epi32 = _mm_srli_epi64(yuv1_epi32, 32);
		y3_epi32 = _mm_srli_epi64(yuv3_epi32, 32);

		// Repack the luma values in the correct order
		yy1_epi32 = _mm_unpacklo_epi64(y1_epi32, y2_epi32);
		yy2_epi32 = _mm_unpackhi_epi64(y1_epi32, y2_epi32);

		// Combine the first two luma values with two values from the previous phase
		yyyy2_epi32 = _mm_packs_epi32(yy4_epi32, yy1_epi32);

		// Repack the last four of six luma values from this phase
		yyyy3_epi32 = _mm_packs_epi32(y3_epi32, yy2_epi32);
		yyyy3_epi32 = _mm_shuffle_epi32(yyyy3_epi32, _MM_SHUFFLE(1, 3, 2, 0));

		// Unpack the chroma values
		uv4_epi32 = _mm_and_si128(yuv1_epi32, mask64_epi32);
		uv5_epi32 = _mm_srli_epi64(yuv2_epi32, 32);
		uv6_epi32 = _mm_and_si128(yuv3_epi32, mask64_epi32);

		// Repack the second pair of chroma values
		u2_epi32 = _mm_unpacklo_epi32(uv3_epi32, uv4_epi32);
		uv2_epi32 = _mm_shuffle_epi32(uv2_epi32, _MM_SHUFFLE(1, 0, 3, 2));
		v2_epi32 = _mm_unpacklo_epi32(uv2_epi32, uv6_epi32);

		// Repack the third pair of chroma values
		v3_epi32 = _mm_unpackhi_epi32(uv4_epi32, uv5_epi32);
		uv6_epi32 = _mm_shuffle_epi32(uv6_epi32, _MM_SHUFFLE(1, 0, 3, 2));
		u3_epi32 = _mm_unpacklo_epi32(uv5_epi32, uv6_epi32);

		// Combine the first and second pairs of chroma values
		uuuu1_epi32 = _mm_unpacklo_epi64(u1_epi32, u2_epi32);
		vvvv1_epi32 = _mm_unpacklo_epi64(v1_epi32, v2_epi32);

		// Combine the first eight luma values
		y1_epi16 = _mm_packs_epi32(yyyy1_epi32, yyyy2_epi32);

		// Store the first eight luma values
		_mm_store_si128(y_output_ptr++, y1_epi16);


		/***** Phase Three *****/

		// Read the third group of six packed 10-bit pixels
		//input_si128 = _mm_loadu_si128(input_ptr++);
		input_si128 = _mm_load_si128(input_ptr++);

		// Unpack the twelve luma and chroma values into three sets of values
		yuv1_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv2_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv3_epi32 = _mm_and_si128(input_si128, mask_epi32);

		// Unpack the luma values
		y1_epi32 = _mm_and_si128(yuv2_epi32, mask64_epi32);
		y2_epi32 = _mm_srli_epi64(yuv1_epi32, 32);
		y3_epi32 = _mm_srli_epi64(yuv3_epi32, 32);

		// Repack the luma values in the correct order
		yy1_epi32 = _mm_unpacklo_epi64(y1_epi32, y2_epi32);
		yy2_epi32 = _mm_unpackhi_epi64(y1_epi32, y2_epi32);

		yy3_epi32 = _mm_unpacklo_epi64(y3_epi32, yy2_epi32);
		yy4_epi32 = _mm_unpackhi_epi64(yy2_epi32, y3_epi32);

		// Pack the first four luma values from this phase
		yyyy1_epi32 = _mm_packs_epi32(yy1_epi32, yy3_epi32);

		// Combine the four luma values with four luma from the last phase
		y2_epi16 = _mm_packs_epi32(yyyy3_epi32, yyyy1_epi32);

		// Store the second eight luma values
		_mm_store_si128(y_output_ptr++, y2_epi16);

		// Unpack the chroma values
		uv1_epi32 = _mm_and_si128(yuv1_epi32, mask64_epi32);
		uv2_epi32 = _mm_srli_epi64(yuv2_epi32, 32);
		uv3_epi32 = _mm_and_si128(yuv3_epi32, mask64_epi32);

		// Repack the fourth pair of chroma values
		u4_epi32 = _mm_unpacklo_epi32(uv1_epi32, uv2_epi32);
		uv3_epi32 = _mm_shuffle_epi32(uv3_epi32, _MM_SHUFFLE(1, 0, 3, 2));
		v4_epi32 = _mm_unpackhi_epi32(uv3_epi32, uv1_epi32);

		// Combine the third and fourth pairs of chroma values
		uuuu2_epi32 = _mm_unpacklo_epi64(u3_epi32, u4_epi32);
		vvvv2_epi32 = _mm_unpacklo_epi64(v3_epi32, v4_epi32);

		// Combine the first eight chroma values
		u1_epi16 = _mm_packs_epi32(uuuu1_epi32, uuuu2_epi32);
		v1_epi16 = _mm_packs_epi32(vvvv1_epi32, vvvv2_epi32);

		// Store the first eight chroma values
		_mm_store_si128(u_output_ptr++, u1_epi16);
		_mm_store_si128(v_output_ptr++, v1_epi16);


		/***** Phase Four *****/

		// Read the fourth group of six packed 10-bit pixels
		//input_si128 = _mm_loadu_si128(input_ptr++);
		input_si128 = _mm_load_si128(input_ptr++);

		// Unpack the twelve luma and chroma values into three sets of values
		yuv1_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv2_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv3_epi32 = _mm_and_si128(input_si128, mask_epi32);

		// Unpack the luma values
		y1_epi32 = _mm_and_si128(yuv2_epi32, mask64_epi32);
		y2_epi32 = _mm_srli_epi64(yuv1_epi32, 32);
		y3_epi32 = _mm_srli_epi64(yuv3_epi32, 32);

		// Repack the luma values in the correct order
		yy1_epi32 = _mm_unpacklo_epi64(y1_epi32, y2_epi32);
		yy2_epi32 = _mm_unpackhi_epi64(y1_epi32, y2_epi32);

		// Combine the first two luma values with two values from the previous phase
		yyyy2_epi32 = _mm_packs_epi32(yy4_epi32, yy1_epi32);

		// Repack the last four of six luma values from this phase
		yyyy3_epi32 = _mm_packs_epi32(y3_epi32, yy2_epi32);
		yyyy3_epi32 = _mm_shuffle_epi32(yyyy3_epi32, _MM_SHUFFLE(1, 3, 2, 0));

		// Unpack the chroma values
		uv4_epi32 = _mm_and_si128(yuv1_epi32, mask64_epi32);
		uv5_epi32 = _mm_srli_epi64(yuv2_epi32, 32);
		uv6_epi32 = _mm_and_si128(yuv3_epi32, mask64_epi32);

		// Repack the fifth pair of chroma values
		u5_epi32 = _mm_unpacklo_epi32(uv3_epi32, uv4_epi32);
		uv2_epi32 = _mm_shuffle_epi32(uv2_epi32, _MM_SHUFFLE(1, 0, 3, 2));
		v5_epi32 = _mm_unpacklo_epi32(uv2_epi32, uv6_epi32);

		// Repack the sixth pair of chroma values
		v6_epi32 = _mm_unpackhi_epi32(uv4_epi32, uv5_epi32);
		uv6_epi32 = _mm_shuffle_epi32(uv6_epi32, _MM_SHUFFLE(1, 0, 3, 2));
		u6_epi32 = _mm_unpacklo_epi32(uv5_epi32, uv6_epi32);

		// Combine the third eight luma values
		y3_epi16 = _mm_packs_epi32(yyyy2_epi32, yyyy3_epi32);

		// Store the third eight luma values
		_mm_store_si128(y_output_ptr++, y3_epi16);

		// Combine the fifth and sixth pairs of chroma values
		uuuu3_epi32 = _mm_unpacklo_epi64(u5_epi32, u6_epi32);
		vvvv3_epi32 = _mm_unpacklo_epi64(v5_epi32, v6_epi32);


		/***** Phase Five *****/

		// Read the fifth group of six packed 10-bit pixels
		//input_si128 = _mm_loadu_si128(input_ptr++);
		input_si128 = _mm_load_si128(input_ptr++);

		// Unpack the twelve luma and chroma values into three sets of values
		yuv1_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv2_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv3_epi32 = _mm_and_si128(input_si128, mask_epi32);

		// Unpack the luma values
		y1_epi32 = _mm_and_si128(yuv2_epi32, mask64_epi32);
		y2_epi32 = _mm_srli_epi64(yuv1_epi32, 32);
		y3_epi32 = _mm_srli_epi64(yuv3_epi32, 32);

		// Repack the luma values in the correct order
		yy1_epi32 = _mm_unpacklo_epi64(y1_epi32, y2_epi32);
		yy2_epi32 = _mm_unpackhi_epi64(y1_epi32, y2_epi32);

		yy3_epi32 = _mm_unpacklo_epi64(y3_epi32, yy2_epi32);
		yy4_epi32 = _mm_unpackhi_epi64(yy2_epi32, y3_epi32);

		// Pack the first four luma values from this phase
		yyyy1_epi32 = _mm_packs_epi32(yy1_epi32, yy3_epi32);

		// Unpack the chroma values
		uv1_epi32 = _mm_and_si128(yuv1_epi32, mask64_epi32);
		uv2_epi32 = _mm_srli_epi64(yuv2_epi32, 32);
		uv3_epi32 = _mm_and_si128(yuv3_epi32, mask64_epi32);

		// Repack the seventh pair of chroma values
		u1_epi32 = _mm_unpacklo_epi32(uv1_epi32, uv2_epi32);
		uv3_epi32 = _mm_shuffle_epi32(uv3_epi32, _MM_SHUFFLE(1, 0, 3, 2));
		v1_epi32 = _mm_unpackhi_epi32(uv3_epi32, uv1_epi32);


		/***** Phase Six *****/

		// Read the sixth group of six packed 10-bit pixels
		//input_si128 = _mm_loadu_si128(input_ptr++);
		input_si128 = _mm_load_si128(input_ptr++);

		// Unpack the twelve luma and chroma values into three sets of values
		yuv1_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv2_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv3_epi32 = _mm_and_si128(input_si128, mask_epi32);

		// Unpack the luma values
		y1_epi32 = _mm_and_si128(yuv2_epi32, mask64_epi32);
		y2_epi32 = _mm_srli_epi64(yuv1_epi32, 32);
		y3_epi32 = _mm_srli_epi64(yuv3_epi32, 32);

		// Repack the luma values in the correct order
		yy1_epi32 = _mm_unpacklo_epi64(y1_epi32, y2_epi32);
		yy2_epi32 = _mm_unpackhi_epi64(y1_epi32, y2_epi32);

		// Combine the first two luma values with two values from the previous phase
		yyyy2_epi32 = _mm_packs_epi32(yy4_epi32, yy1_epi32);

		// Repack the last four of six luma values from this phase
		yyyy3_epi32 = _mm_packs_epi32(y3_epi32, yy2_epi32);
		yyyy3_epi32 = _mm_shuffle_epi32(yyyy3_epi32, _MM_SHUFFLE(1, 3, 2, 0));

		// Unpack the chroma values
		uv4_epi32 = _mm_and_si128(yuv1_epi32, mask64_epi32);
		uv5_epi32 = _mm_srli_epi64(yuv2_epi32, 32);
		uv6_epi32 = _mm_and_si128(yuv3_epi32, mask64_epi32);

		// Repack the eighth pair of chroma values
		u2_epi32 = _mm_unpacklo_epi32(uv3_epi32, uv4_epi32);
		uv2_epi32 = _mm_shuffle_epi32(uv2_epi32, _MM_SHUFFLE(1, 0, 3, 2));
		v2_epi32 = _mm_unpacklo_epi32(uv2_epi32, uv6_epi32);

		// Repack the ninth pair of chroma values
		v3_epi32 = _mm_unpackhi_epi32(uv4_epi32, uv5_epi32);
		uv6_epi32 = _mm_shuffle_epi32(uv6_epi32, _MM_SHUFFLE(1, 0, 3, 2));
		u3_epi32 = _mm_unpacklo_epi32(uv5_epi32, uv6_epi32);

		// Combine the seventh and eighth pairs of chroma values
		uuuu1_epi32 = _mm_unpacklo_epi64(u1_epi32, u2_epi32);
		vvvv1_epi32 = _mm_unpacklo_epi64(v1_epi32, v2_epi32);

		// Combine the fourth eight luma values
		y1_epi16 = _mm_packs_epi32(yyyy1_epi32, yyyy2_epi32);

		// Store the fourth eight luma values
		_mm_store_si128(y_output_ptr++, y1_epi16);

		// Combine the second eight chroma values
		u2_epi16 = _mm_packs_epi32(uuuu3_epi32, uuuu1_epi32);
		v2_epi16 = _mm_packs_epi32(vvvv3_epi32, vvvv1_epi32);

		// Store the second eight chroma values
		_mm_store_si128(u_output_ptr++, u2_epi16);
		_mm_store_si128(v_output_ptr++, v2_epi16);


		/***** Phase Seven *****/

		// Read the seventh group of six packed 10-bit pixels
		//input_si128 = _mm_loadu_si128(input_ptr++);
		input_si128 = _mm_load_si128(input_ptr++);

		// Unpack the twelve luma and chroma values into three sets of values
		yuv1_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv2_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv3_epi32 = _mm_and_si128(input_si128, mask_epi32);

		// Unpack the luma values
		y1_epi32 = _mm_and_si128(yuv2_epi32, mask64_epi32);
		y2_epi32 = _mm_srli_epi64(yuv1_epi32, 32);
		y3_epi32 = _mm_srli_epi64(yuv3_epi32, 32);

		// Repack the luma values in the correct order
		yy1_epi32 = _mm_unpacklo_epi64(y1_epi32, y2_epi32);
		yy2_epi32 = _mm_unpackhi_epi64(y1_epi32, y2_epi32);

		yy3_epi32 = _mm_unpacklo_epi64(y3_epi32, yy2_epi32);
		yy4_epi32 = _mm_unpackhi_epi64(yy2_epi32, y3_epi32);

		// Pack the first four luma values from this phase
		yyyy1_epi32 = _mm_packs_epi32(yy1_epi32, yy3_epi32);

		// Combine the four luma values with four luma from the last phase
		y2_epi16 = _mm_packs_epi32(yyyy3_epi32, yyyy1_epi32);

		// Store the fifth eight luma values
		_mm_store_si128(y_output_ptr++, y2_epi16);

		// Unpack the chroma values
		uv1_epi32 = _mm_and_si128(yuv1_epi32, mask64_epi32);
		uv2_epi32 = _mm_srli_epi64(yuv2_epi32, 32);
		uv3_epi32 = _mm_and_si128(yuv3_epi32, mask64_epi32);

		// Repack the tenth pair of chroma values
		u4_epi32 = _mm_unpacklo_epi32(uv1_epi32, uv2_epi32);
		uv3_epi32 = _mm_shuffle_epi32(uv3_epi32, _MM_SHUFFLE(1, 0, 3, 2));
		v4_epi32 = _mm_unpackhi_epi32(uv3_epi32, uv1_epi32);

		// Combine the ninth and tenth pairs of chroma values
		uuuu2_epi32 = _mm_unpacklo_epi64(u3_epi32, u4_epi32);
		vvvv2_epi32 = _mm_unpacklo_epi64(v3_epi32, v4_epi32);


		/***** Phase Eight *****/

		// Read the eighth group of six packed 10-bit pixels
		//input_si128 = _mm_loadu_si128(input_ptr++);
		input_si128 = _mm_loadu_si128(input_ptr++);

		// Unpack the twelve luma and chroma values into three sets of values
		yuv1_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv2_epi32 = _mm_and_si128(input_si128, mask_epi32);

		input_si128 = _mm_srli_epi32(input_si128, V210_VALUE2_SHIFT);
		yuv3_epi32 = _mm_and_si128(input_si128, mask_epi32);

		// Unpack the luma values
		y1_epi32 = _mm_and_si128(yuv2_epi32, mask64_epi32);
		y2_epi32 = _mm_srli_epi64(yuv1_epi32, 32);
		y3_epi32 = _mm_srli_epi64(yuv3_epi32, 32);

		// Repack the luma values in the correct order
		yy1_epi32 = _mm_unpacklo_epi64(y1_epi32, y2_epi32);
		yy2_epi32 = _mm_unpackhi_epi64(y1_epi32, y2_epi32);

		// Combine the first two luma values with two values from the previous phase
		yyyy2_epi32 = _mm_packs_epi32(yy4_epi32, yy1_epi32);

		// Repack the last four of six luma values from this phase
		yyyy3_epi32 = _mm_packs_epi32(y3_epi32, yy2_epi32);
		yyyy3_epi32 = _mm_shuffle_epi32(yyyy3_epi32, _MM_SHUFFLE(1, 3, 2, 0));

		// Unpack the chroma values
		uv4_epi32 = _mm_and_si128(yuv1_epi32, mask64_epi32);
		uv5_epi32 = _mm_srli_epi64(yuv2_epi32, 32);
		uv6_epi32 = _mm_and_si128(yuv3_epi32, mask64_epi32);

		// Repack the eleventh pair of chroma values
		u5_epi32 = _mm_unpacklo_epi32(uv3_epi32, uv4_epi32);
		uv2_epi32 = _mm_shuffle_epi32(uv2_epi32, _MM_SHUFFLE(1, 0, 3, 2));
		v5_epi32 = _mm_unpacklo_epi32(uv2_epi32, uv6_epi32);

		// Repack the twelfth pair of chroma values
		v6_epi32 = _mm_unpackhi_epi32(uv4_epi32, uv5_epi32);
		uv6_epi32 = _mm_shuffle_epi32(uv6_epi32, _MM_SHUFFLE(1, 0, 3, 2));
		u6_epi32 = _mm_unpacklo_epi32(uv5_epi32, uv6_epi32);

		// Combine the sixth eight luma values
		y3_epi16 = _mm_packs_epi32(yyyy2_epi32, yyyy3_epi32);

		// Store the sixth eight luma values
		_mm_store_si128(y_output_ptr++, y3_epi16);

		// Combine the eleventh and twelfth pairs of chroma values
		uuuu3_epi32 = _mm_unpacklo_epi64(u5_epi32, u6_epi32);
		vvvv3_epi32 = _mm_unpacklo_epi64(v5_epi32, v6_epi32);

		// Combine the third eight chroma values
		u3_epi16 = _mm_packs_epi32(uuuu2_epi32, uuuu3_epi32);
		v3_epi16 = _mm_packs_epi32(vvvv2_epi32, vvvv3_epi32);

		// Store the third eight chroma values
		_mm_store_si128(u_output_ptr++, u3_epi16);
		_mm_store_si128(v_output_ptr++, v3_epi16);
	}
	
	// Check that the fast loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Must have an even number of pixels
	assert((length % 2) == 0);

	yuv_ptr = (uint32_t *)input_ptr;
	y_ptr = (PIXEL *)y_output_ptr;
	u_ptr = (PIXEL *)u_output_ptr;
	v_ptr = (PIXEL *)v_output_ptr;
	{

		uint32_t yuv;

		int y;
		int u;
		int v;

		for (; column < length - (length % 6); column += 6)
		{
			// Note: This routine swaps the chroma values

			// Read the first word
			yuv = *(yuv_ptr++);

			u = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;
			y = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
			v = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

			*(y_ptr++) = y;
			*(v_ptr++) = v;

			// Read the second word
			yuv = *(yuv_ptr++);

			y = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;

			*(y_ptr++) = y;
			*(u_ptr++) = u;

			u = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
			y = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

			*(y_ptr++) = y;
			*(v_ptr++) = v;

			// Read the third word
			yuv = *(yuv_ptr++);

			v = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;
			y = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;

			*(y_ptr++) = y;
			*(u_ptr++) = u;

			u = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

			// Read the fourth word
			yuv = *(yuv_ptr++);
			y = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;

			*(y_ptr++) = y;
			*(v_ptr++) = v;

			v = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
			y = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

			*(y_ptr++) = y;
			*(u_ptr++) = u;
		}

		if(column < length)
		{
			// Note: This routine swaps the chroma values

			// Read the first word
			yuv = *(yuv_ptr++);

			u = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;
			y = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
			v = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

			*(y_ptr++) = y;
			*(v_ptr++) = v;

			// Read the second word
			yuv = *(yuv_ptr++);

			y = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;

			*(y_ptr++) = y;
			*(u_ptr++) = u;

			column+=2;
		}

		if(column < length)
		{
			u = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
			y = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

			*(y_ptr++) = y;
			*(v_ptr++) = v;

			// Read the third word
			yuv = *(yuv_ptr++);

			v = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;
			y = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;

			*(y_ptr++) = y;
			*(u_ptr++) = u;

			column+=2;
		}

		if(column < length)
		{
			u = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

			// Read the fourth word
			yuv = *(yuv_ptr++);
			y = (yuv >> V210_VALUE1_SHIFT) & V210_VALUE_MASK;

			*(y_ptr++) = y;
			*(v_ptr++) = v;

			v = (yuv >> V210_VALUE2_SHIFT) & V210_VALUE_MASK;
			y = (yuv >> V210_VALUE3_SHIFT) & V210_VALUE_MASK;

			*(y_ptr++) = y;
			*(u_ptr++) = u;

			column+=2;
		}
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert the RGB24 data to V210
void ConvertRGB24ToV210(uint8_t *data, int width, int height, int pitch, uint8_t *buffer)
{
	uint8_t *rowptr = data;
	int row;

	for (row = 0; row < height; row++)
	{
		// Convert a row of RGB24 to YUV
		ConvertRGB24RowToYUV(rowptr, buffer, width);

		if ((width % 6) != 0)
		{
			// Pad the output to a multiple of six pixels
			ZeroMemory(&buffer[2 * width], 12);
			width += 6 - (width % 6);
		}

		// Expand the 8-bit YUV to V210
		ConvertYUVRowToV210(buffer, rowptr, width);

		// Advance to the next row
		rowptr += pitch;
	}
}
//#endif


// Unpack the row of pixel values for the specified channel
void UnpackRowYUV16s(uint8_t *input, PIXEL *output, int width, int channel, 
					 int format, int shift, int limit_yuv, int conv_601_709) 
{
	// Compute the length of the input row (in bytes) from the output width
	int length = width * ((channel == 0) ? 2 : 4);

	// Process two groups of eight bytes per loop iteration
	int column_step = 16;
	int post_column = length - (length % column_step);
	int column;

	// Start processing at the leftmost column
	column = 0;

	if(format == COLOR_FORMAT_YUYV)
	{

	#if (1 && XMMOPT)
		{
			__m128i *input_ptr = (__m128i *)input;
			__m128i *output_ptr = (__m128i *)output;
			__m128i input_epu8;
			__m128i y1y2_epi16;
			__m128i u1v1_epi16;
			__m128i u1u1_epi16;
			__m128i v1v1_epi16;
			__m128i tmp_epi16;
			
			__m128i limit_epi16 = _mm_set1_epi16(0x7fff - 0x03ff);

			if (channel == 0)
			{
				for (; column < post_column; column += column_step)
				{
					input_epu8 = _mm_load_si128(input_ptr++); //yuyvyuyvyuyvyuyv

					// Unpack the first eight luma values
					y1y2_epi16 = _mm_and_si128(input_epu8, _mm_set1_epi16(0x00FF));

					if(limit_yuv && shift == 2)//luma range and 709 fix for Canon 5D
					{					
						if(conv_601_709)// 601  to 709 conversion
						{
							// Unpack the first eight luma values
							u1v1_epi16 = _mm_and_si128(input_epu8, _mm_set1_epi16(0xFF00));
							u1v1_epi16 = _mm_srli_epi16(u1v1_epi16, 8);

							y1y2_epi16 = _mm_mullo_epi16(y1y2_epi16, _mm_set1_epi16(55));
							y1y2_epi16 = _mm_srai_epi16(y1y2_epi16, 4);
							y1y2_epi16 = _mm_adds_epi16(y1y2_epi16, _mm_set1_epi16(64));

							// assume 601 to 709 for Canon limit_yuv fixes
							u1v1_epi16 = _mm_mullo_epi16(u1v1_epi16, _mm_set1_epi16(56));
							u1v1_epi16 = _mm_srai_epi16(u1v1_epi16, 4);
							u1v1_epi16 = _mm_adds_epi16(u1v1_epi16, _mm_set1_epi16(64));

							u1v1_epi16 = _mm_subs_epi16(u1v1_epi16, _mm_set1_epi16(512));

							u1u1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));
							tmp_epi16  = _mm_slli_epi32(u1v1_epi16, 16);
							u1u1_epi16 = _mm_or_si128(u1u1_epi16, tmp_epi16); 

							v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
							tmp_epi16  = _mm_srli_epi32(v1v1_epi16, 16);
							v1v1_epi16 = _mm_or_si128(v1v1_epi16, tmp_epi16); 

							u1u1_epi16 = _mm_mulhi_epi16(u1u1_epi16, _mm_set1_epi16(118<<6));  //10*10<<6 = 10bit
							v1v1_epi16 = _mm_mulhi_epi16(v1v1_epi16, _mm_set1_epi16(212<<6));  //10*10<<6 = 10bit

							// convert Luma 601 to 709
							//y1 = (1024*y1 - 212*(v-512) - 118*(u-512))>>10;
							//y2 = (1024*y2 - 212*(v-512) - 118*(u-512))>>10;
							y1y2_epi16 = _mm_subs_epi16(y1y2_epi16, v1v1_epi16); 
							y1y2_epi16 = _mm_subs_epi16(y1y2_epi16, u1u1_epi16);

							// limit to 10-bit rnge - 0 to 1023						
							y1y2_epi16 = _mm_adds_epi16(y1y2_epi16, limit_epi16);
							y1y2_epi16 = _mm_subs_epu16(y1y2_epi16, limit_epi16);
						}
						else
						{
							y1y2_epi16 = _mm_mullo_epi16(y1y2_epi16, _mm_set1_epi16(55));
							y1y2_epi16 = _mm_srai_epi16(y1y2_epi16, 4);
							y1y2_epi16 = _mm_adds_epi16(y1y2_epi16, _mm_set1_epi16(64));
						}
					}
					else if(conv_601_709 && shift == 2)//bump 8-bit 601 to 10-bit 709
					{					
						// Unpack the first eight luma values
						u1v1_epi16 = _mm_and_si128(input_epu8, _mm_set1_epi16(0xFF00));
						u1v1_epi16 = _mm_srli_epi16(u1v1_epi16, 6); // 10-bit
						y1y2_epi16 = _mm_slli_epi16(y1y2_epi16, 2); // 10-bit

						u1v1_epi16 = _mm_subs_epi16(u1v1_epi16, _mm_set1_epi16(512));

						u1u1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));
						tmp_epi16  = _mm_slli_epi32(u1v1_epi16, 16);
						u1u1_epi16 = _mm_or_si128(u1u1_epi16, tmp_epi16); 

						v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
						tmp_epi16  = _mm_srli_epi32(v1v1_epi16, 16);
						v1v1_epi16 = _mm_or_si128(v1v1_epi16, tmp_epi16); 

						u1u1_epi16 = _mm_mulhi_epi16(u1u1_epi16, _mm_set1_epi16(118<<6));  //10*10<<6 = 10bit
						v1v1_epi16 = _mm_mulhi_epi16(v1v1_epi16, _mm_set1_epi16(212<<6));  //10*10<<6 = 10bit

						// convert Luma 601 to 709
						//y1 = (1024*y1 - 212*(v-512) - 118*(u-512))>>10;
						//y2 = (1024*y2 - 212*(v-512) - 118*(u-512))>>10;
						y1y2_epi16 = _mm_subs_epi16(y1y2_epi16, v1v1_epi16); 
						y1y2_epi16 = _mm_subs_epi16(y1y2_epi16, u1u1_epi16);

						// limit to 10-bit rnge - 0 to 1023						
						y1y2_epi16 = _mm_adds_epi16(y1y2_epi16, limit_epi16);
						y1y2_epi16 = _mm_subs_epu16(y1y2_epi16, limit_epi16);
					}
					else
					{
						y1y2_epi16 = _mm_slli_epi16(y1y2_epi16, shift);
					}


					// Store the unpacked luma values
					_mm_store_si128(output_ptr++, y1y2_epi16);
				}
			}
			else if (channel == 2) // U channel
			{
				__m128i u1uA_epi16;

				for (; column < post_column; column += column_step*2)
				{
					input_epu8 = _mm_load_si128(input_ptr++); //yuyvyuyvyuyvyuyv

					// Unpack the first eight uv pairs values
					u1v1_epi16 = _mm_and_si128(input_epu8, _mm_set1_epi16(0xFF00));
					u1v1_epi16 = _mm_srli_epi16(u1v1_epi16, 8);

					if(limit_yuv && shift == 2) //luma range and 709 fix for Canon 5D
					{
						if(conv_601_709)// 601  to 709 conversion
						{
							u1v1_epi16 = _mm_mullo_epi16(u1v1_epi16, _mm_set1_epi16(56));
							u1v1_epi16 = _mm_srai_epi16(u1v1_epi16, 1);
							u1v1_epi16 = _mm_adds_epi16(u1v1_epi16, _mm_set1_epi16(64<<3)); //should be 64 -- fixing a rounding error 

							u1v1_epi16 = _mm_subs_epi16(u1v1_epi16, _mm_set1_epi16(4096));

							u1u1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));
							tmp_epi16  = _mm_slli_epi32(u1v1_epi16, 16);
							u1u1_epi16 = _mm_or_si128(u1u1_epi16, tmp_epi16); 

							v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
							tmp_epi16  = _mm_srli_epi32(v1v1_epi16, 16);
							v1v1_epi16 = _mm_or_si128(v1v1_epi16, tmp_epi16); 

							// convert Chroma 601 to 709
							//u = (( 116*(v-512) + 1043*(u-512))>>10) + 512
							//v1v1_epi16 = _mm_slli_epi32(v1v1_epi16, 3);
							tmp_epi16  = _mm_mulhi_epi16(v1v1_epi16, _mm_set1_epi16(116<<3));    //10<<3*10<<3 = 10bit
							//u1u1_epi16 = _mm_slli_epi32(u1u1_epi16, 3);
							u1u1_epi16 = _mm_mulhi_epi16(u1u1_epi16, _mm_set1_epi16(1043<<3));     //10<<3*10<<3 = 10bit
							u1u1_epi16 = _mm_adds_epi16(u1u1_epi16, tmp_epi16);
							u1u1_epi16 = _mm_adds_epi16(u1u1_epi16, _mm_set1_epi16(512));

							// limit to 10-bit rnge - 0 to 1023						
							u1u1_epi16 = _mm_adds_epi16(u1u1_epi16, limit_epi16);
							u1u1_epi16 = _mm_subs_epu16(u1u1_epi16, limit_epi16);
							
							u1uA_epi16 = _mm_and_si128(u1u1_epi16, _mm_set1_epi32(0xFFFF));  //00uu00uu00uu00uu
						}
						else
						{
							u1v1_epi16 = _mm_mullo_epi16(u1v1_epi16, _mm_set1_epi16(56));
							u1v1_epi16 = _mm_srai_epi16(u1v1_epi16, 4);
							u1v1_epi16 = _mm_adds_epi16(u1v1_epi16, _mm_set1_epi16(64));
														
							u1uA_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));  //00uu00uu00uu00uu
						}
					}
					else if(conv_601_709 && shift == 2)//bump 8-bit 601 to 10-bit 709
					{			
						u1v1_epi16 = _mm_slli_epi16(u1v1_epi16, 2); // 10-bit
						u1v1_epi16 = _mm_subs_epi16(u1v1_epi16, _mm_set1_epi16(512));

						u1u1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));
						tmp_epi16  = _mm_slli_epi32(u1v1_epi16, 16);
						u1u1_epi16 = _mm_or_si128(u1u1_epi16, tmp_epi16); 

						v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
						tmp_epi16  = _mm_srli_epi32(v1v1_epi16, 16);
						v1v1_epi16 = _mm_or_si128(v1v1_epi16, tmp_epi16); 

						// convert Chroma 601 to 709
						//u = (( 116*(v-512) + 1043*(u-512))>>10) + 512
						v1v1_epi16 = _mm_slli_epi32(v1v1_epi16, 3);
						tmp_epi16  = _mm_mulhi_epi16(v1v1_epi16, _mm_set1_epi16(116<<3));    //10<<3*10<<3 = 10bit
						u1u1_epi16 = _mm_slli_epi32(u1u1_epi16, 3);
						u1u1_epi16 = _mm_mulhi_epi16(u1u1_epi16, _mm_set1_epi16(1043<<3));     //10<<3*10<<3 = 10bit
						u1u1_epi16 = _mm_adds_epi16(u1u1_epi16, tmp_epi16);
						u1u1_epi16 = _mm_adds_epi16(u1u1_epi16, _mm_set1_epi16(512));

						// limit to 10-bit rnge - 0 to 1023						
						u1u1_epi16 = _mm_adds_epi16(u1u1_epi16, limit_epi16);
						u1u1_epi16 = _mm_subs_epu16(u1u1_epi16, limit_epi16);
						
						u1uA_epi16 = _mm_and_si128(u1u1_epi16, _mm_set1_epi32(0xFFFF));  //00uu00uu00uu00uu
					}
					else
					{
						u1v1_epi16 = _mm_slli_epi16(u1v1_epi16, shift);
						u1uA_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));  //00uu00uu00uu00uu
					}



					input_epu8 = _mm_load_si128(input_ptr++); //yuyvyuyvyuyvyuyv

					// Unpack the first eight uv pairs values
					u1v1_epi16 = _mm_and_si128(input_epu8, _mm_set1_epi16(0xFF00));
					u1v1_epi16 = _mm_srli_epi16(u1v1_epi16, 8);

					if(limit_yuv && shift == 2) //luma range and 709 fix for Canon 5D
					{
						if(conv_601_709)// 601  to 709 conversion
						{
							u1v1_epi16 = _mm_mullo_epi16(u1v1_epi16, _mm_set1_epi16(56));
							u1v1_epi16 = _mm_srai_epi16(u1v1_epi16, 1);
							u1v1_epi16 = _mm_adds_epi16(u1v1_epi16, _mm_set1_epi16(64<<3));

							u1v1_epi16 = _mm_subs_epi16(u1v1_epi16, _mm_set1_epi16(4096));

							u1u1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));
							tmp_epi16  = _mm_slli_epi32(u1v1_epi16, 16);
							u1u1_epi16 = _mm_or_si128(u1u1_epi16, tmp_epi16); 

							v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
							tmp_epi16  = _mm_srli_epi32(v1v1_epi16, 16);
							v1v1_epi16 = _mm_or_si128(v1v1_epi16, tmp_epi16); 

							// convert Chroma 601 to 709
							//u = (( 116*(v-512) + 1043*(u-512))>>10) + 512
							//v1v1_epi16 = _mm_slli_epi32(v1v1_epi16, 3);
							tmp_epi16  = _mm_mulhi_epi16(v1v1_epi16, _mm_set1_epi16(116<<3));    //10<<3*10<<3 = 10bit
							//u1u1_epi16 = _mm_slli_epi32(u1u1_epi16, 3);
							u1u1_epi16 = _mm_mulhi_epi16(u1u1_epi16, _mm_set1_epi16(1043<<3));     //10<<3*10<<3 = 10bit
							u1u1_epi16 = _mm_adds_epi16(u1u1_epi16, tmp_epi16);
							u1u1_epi16 = _mm_adds_epi16(u1u1_epi16, _mm_set1_epi16(512));

							// limit to 10-bit rnge - 0 to 1023						
							u1u1_epi16 = _mm_adds_epi16(u1u1_epi16, limit_epi16);
							u1u1_epi16 = _mm_subs_epu16(u1u1_epi16, limit_epi16);

							u1u1_epi16 = _mm_and_si128(u1u1_epi16, _mm_set1_epi32(0xFFFF));  //00uu00uu00uu00uu
						}
						else
						{
							u1v1_epi16 = _mm_mullo_epi16(u1v1_epi16, _mm_set1_epi16(56));
							u1v1_epi16 = _mm_srai_epi16(u1v1_epi16, 4);
							u1v1_epi16 = _mm_adds_epi16(u1v1_epi16, _mm_set1_epi16(64));
														
							u1u1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));  //00uu00uu00uu00uu
						}
					}
					else if(conv_601_709 && shift == 2)//bump 8-bit 601 to 10-bit 709
					{	
						u1v1_epi16 = _mm_slli_epi16(u1v1_epi16, 2); //10-bit

						u1v1_epi16 = _mm_subs_epi16(u1v1_epi16, _mm_set1_epi16(512));

						u1u1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));
						tmp_epi16  = _mm_slli_epi32(u1v1_epi16, 16);
						u1u1_epi16 = _mm_or_si128(u1u1_epi16, tmp_epi16); 

						v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
						tmp_epi16  = _mm_srli_epi32(v1v1_epi16, 16);
						v1v1_epi16 = _mm_or_si128(v1v1_epi16, tmp_epi16); 

						// convert Chroma 601 to 709
						//u = (( 116*(v-512) + 1043*(u-512))>>10) + 512
						v1v1_epi16 = _mm_slli_epi32(v1v1_epi16, 3);
						tmp_epi16  = _mm_mulhi_epi16(v1v1_epi16, _mm_set1_epi16(116<<3));    //10<<3*10<<3 = 10bit
						u1u1_epi16 = _mm_slli_epi32(u1u1_epi16, 3);
						u1u1_epi16 = _mm_mulhi_epi16(u1u1_epi16, _mm_set1_epi16(1043<<3));     //10<<3*10<<3 = 10bit
						u1u1_epi16 = _mm_adds_epi16(u1u1_epi16, tmp_epi16);
						u1u1_epi16 = _mm_adds_epi16(u1u1_epi16, _mm_set1_epi16(512));

						// limit to 10-bit rnge - 0 to 1023						
						u1u1_epi16 = _mm_adds_epi16(u1u1_epi16, limit_epi16);
						u1u1_epi16 = _mm_subs_epu16(u1u1_epi16, limit_epi16);

						u1u1_epi16 = _mm_and_si128(u1u1_epi16, _mm_set1_epi32(0xFFFF));  //00uu00uu00uu00uu
					}
					else
					{
						u1v1_epi16 = _mm_slli_epi16(u1v1_epi16, shift);
						u1u1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));  //00uu00uu00uu00uu
					}



					u1u1_epi16 = _mm_packs_epi32 (u1uA_epi16, u1u1_epi16);

					// Store the unpacked chroma values
					_mm_store_si128(output_ptr++, u1u1_epi16);
				}
			}
			else
			{
				__m128i v1vA_epi16;

				for (; column < post_column; column += column_step*2)
				{
					input_epu8 = _mm_load_si128(input_ptr++); //yuyvyuyvyuyvyuyv

					// Unpack the first eight uv pairs values
					u1v1_epi16 = _mm_and_si128(input_epu8, _mm_set1_epi16(0xFF00));
					u1v1_epi16 = _mm_srli_epi16(u1v1_epi16, 8);

					if(limit_yuv && shift == 2) //luma range and 709 fix for Canon 5D
					{
						if(conv_601_709)// 601  to 709 conversion
						{
							u1v1_epi16 = _mm_mullo_epi16(u1v1_epi16, _mm_set1_epi16(56));
							u1v1_epi16 = _mm_srai_epi16(u1v1_epi16, 1);
							u1v1_epi16 = _mm_adds_epi16(u1v1_epi16, _mm_set1_epi16(64<<3));

							u1v1_epi16 = _mm_subs_epi16(u1v1_epi16, _mm_set1_epi16(4096));

							u1u1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));
							tmp_epi16  = _mm_slli_epi32(u1v1_epi16, 16);
							u1u1_epi16 = _mm_or_si128(u1u1_epi16, tmp_epi16); 

							v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
							tmp_epi16  = _mm_srli_epi32(v1v1_epi16, 16);
							v1v1_epi16 = _mm_or_si128(v1v1_epi16, tmp_epi16); 

							// convert Chroma 601 to 709
							//v = ((1049*(v-512) +   76*(u-512))>>10) + 512;
							//u1u1_epi16 = _mm_slli_epi32(u1u1_epi16, 3);
							tmp_epi16  = _mm_mulhi_epi16(u1u1_epi16, _mm_set1_epi16(76<<3));    //10<<3*10<<3 = 10bit
							//v1v1_epi16 = _mm_slli_epi32(v1v1_epi16, 3);
							v1v1_epi16 = _mm_mulhi_epi16(v1v1_epi16, _mm_set1_epi16(1049<<3));     //10<<3*10<<3 = 10bit
							v1v1_epi16 = _mm_adds_epi16(v1v1_epi16, tmp_epi16);
							v1v1_epi16 = _mm_adds_epi16(v1v1_epi16, _mm_set1_epi16(512));

							// limit to 10-bit rnge - 0 to 1023						
							v1v1_epi16 = _mm_adds_epi16(v1v1_epi16, limit_epi16);
							v1v1_epi16 = _mm_subs_epu16(v1v1_epi16, limit_epi16);
							
							v1v1_epi16 = _mm_and_si128(v1v1_epi16, _mm_set1_epi32(0xFFFF0000));
							v1vA_epi16  = _mm_srli_epi32(v1v1_epi16, 16); //00vv00vv00vv00vv
						}
						else
						{
							u1v1_epi16 = _mm_mullo_epi16(u1v1_epi16, _mm_set1_epi16(56));
							u1v1_epi16 = _mm_srai_epi16(u1v1_epi16, 4);
							u1v1_epi16 = _mm_adds_epi16(u1v1_epi16, _mm_set1_epi16(64));
							
							v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
							v1vA_epi16  = _mm_srli_epi32(v1v1_epi16, 16); //00vv00vv00vv00vv
						}
					}
					else if(conv_601_709 && shift == 2)//bump 8-bit 601 to 10-bit 709
					{	
						u1v1_epi16 = _mm_slli_epi16(u1v1_epi16, 2); //10-bit

						u1v1_epi16 = _mm_subs_epi16(u1v1_epi16, _mm_set1_epi16(512));

						u1u1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));
						tmp_epi16  = _mm_slli_epi32(u1v1_epi16, 16);
						u1u1_epi16 = _mm_or_si128(u1u1_epi16, tmp_epi16); 

						v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
						tmp_epi16  = _mm_srli_epi32(v1v1_epi16, 16);
						v1v1_epi16 = _mm_or_si128(v1v1_epi16, tmp_epi16); 

						// convert Chroma 601 to 709
						//v = ((1049*(v-512) +   76*(u-512))>>10) + 512;
						u1u1_epi16 = _mm_slli_epi32(u1u1_epi16, 3);
						tmp_epi16  = _mm_mulhi_epi16(u1u1_epi16, _mm_set1_epi16(76<<3));    //10<<3*10<<3 = 10bit
						v1v1_epi16 = _mm_slli_epi32(v1v1_epi16, 3);
						v1v1_epi16 = _mm_mulhi_epi16(v1v1_epi16, _mm_set1_epi16(1049<<3));     //10<<3*10<<3 = 10bit
						v1v1_epi16 = _mm_adds_epi16(v1v1_epi16, tmp_epi16);
						v1v1_epi16 = _mm_adds_epi16(v1v1_epi16, _mm_set1_epi16(512));

						// limit to 10-bit rnge - 0 to 1023						
						v1v1_epi16 = _mm_adds_epi16(v1v1_epi16, limit_epi16);
						v1v1_epi16 = _mm_subs_epu16(v1v1_epi16, limit_epi16);
						
						v1v1_epi16 = _mm_and_si128(v1v1_epi16, _mm_set1_epi32(0xFFFF0000));
						v1vA_epi16  = _mm_srli_epi32(v1v1_epi16, 16); //00vv00vv00vv00vv
					}
					else
					{
						u1v1_epi16 = _mm_slli_epi16(u1v1_epi16, shift);

						v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
						v1vA_epi16  = _mm_srli_epi32(v1v1_epi16, 16); //00vv00vv00vv00vv
					}




					input_epu8 = _mm_load_si128(input_ptr++); //yuyvyuyvyuyvyuyv

					// Unpack the first eight uv pairs values
					u1v1_epi16 = _mm_and_si128(input_epu8, _mm_set1_epi16(0xFF00));
					u1v1_epi16 = _mm_srli_epi16(u1v1_epi16, 8);

					if(limit_yuv && shift == 2) //luma range and 709 fix for Canon 5D
					{
						if(conv_601_709)
						{
							u1v1_epi16 = _mm_mullo_epi16(u1v1_epi16, _mm_set1_epi16(56));
							u1v1_epi16 = _mm_srai_epi16(u1v1_epi16, 1);
							u1v1_epi16 = _mm_adds_epi16(u1v1_epi16, _mm_set1_epi16(64<<3));

							u1v1_epi16 = _mm_subs_epi16(u1v1_epi16, _mm_set1_epi16(4096));

							u1u1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));
							tmp_epi16  = _mm_slli_epi32(u1v1_epi16, 16);
							u1u1_epi16 = _mm_or_si128(u1u1_epi16, tmp_epi16); 

							v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
							tmp_epi16  = _mm_srli_epi32(v1v1_epi16, 16);
							v1v1_epi16 = _mm_or_si128(v1v1_epi16, tmp_epi16); 

							// convert Chroma 601 to 709
							//v = ((1049*(v-512) +   76*(u-512))>>10) + 512;
							//u1u1_epi16 = _mm_slli_epi32(u1u1_epi16, 3);
							tmp_epi16  = _mm_mulhi_epi16(u1u1_epi16, _mm_set1_epi16(76<<3));    //10<<3*10<<3 = 10bit
							//v1v1_epi16 = _mm_slli_epi32(v1v1_epi16, 3);
							v1v1_epi16 = _mm_mulhi_epi16(v1v1_epi16, _mm_set1_epi16(1049<<3));     //10<<3*10<<3 = 10bit
							v1v1_epi16 = _mm_adds_epi16(v1v1_epi16, tmp_epi16);
							v1v1_epi16 = _mm_adds_epi16(v1v1_epi16, _mm_set1_epi16(512));

							// limit to 10-bit rnge - 0 to 1023						
							v1v1_epi16 = _mm_adds_epi16(v1v1_epi16, limit_epi16);
							v1v1_epi16 = _mm_subs_epu16(v1v1_epi16, limit_epi16);

							v1v1_epi16 = _mm_and_si128(v1v1_epi16, _mm_set1_epi32(0xFFFF0000));
							v1v1_epi16  = _mm_srli_epi32(v1v1_epi16, 16); //00vv00vv00vv00vv
						}
						else
						{
							u1v1_epi16 = _mm_mullo_epi16(u1v1_epi16, _mm_set1_epi16(56));
							u1v1_epi16 = _mm_srai_epi16(u1v1_epi16, 4);
							u1v1_epi16 = _mm_adds_epi16(u1v1_epi16, _mm_set1_epi16(64));
							
							v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
							v1v1_epi16  = _mm_srli_epi32(v1v1_epi16, 16); //00vv00vv00vv00vv
						}
					}					
					else if(conv_601_709 && shift == 2)//bump 8-bit 601 to 10-bit 709
					{	
						u1v1_epi16 = _mm_slli_epi16(u1v1_epi16, 2); // 10-bit

						u1v1_epi16 = _mm_subs_epi16(u1v1_epi16, _mm_set1_epi16(512));

						u1u1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF));
						tmp_epi16  = _mm_slli_epi32(u1v1_epi16, 16);
						u1u1_epi16 = _mm_or_si128(u1u1_epi16, tmp_epi16); 

						v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
						tmp_epi16  = _mm_srli_epi32(v1v1_epi16, 16);
						v1v1_epi16 = _mm_or_si128(v1v1_epi16, tmp_epi16); 

						// convert Chroma 601 to 709
						//v = ((1049*(v-512) +   76*(u-512))>>10) + 512;
						u1u1_epi16 = _mm_slli_epi32(u1u1_epi16, 3);
						tmp_epi16  = _mm_mulhi_epi16(u1u1_epi16, _mm_set1_epi16(76<<3));    //10<<3*10<<3 = 10bit
						v1v1_epi16 = _mm_slli_epi32(v1v1_epi16, 3);
						v1v1_epi16 = _mm_mulhi_epi16(v1v1_epi16, _mm_set1_epi16(1049<<3));     //10<<3*10<<3 = 10bit
						v1v1_epi16 = _mm_adds_epi16(v1v1_epi16, tmp_epi16);
						v1v1_epi16 = _mm_adds_epi16(v1v1_epi16, _mm_set1_epi16(512));

						// limit to 10-bit rnge - 0 to 1023						
						v1v1_epi16 = _mm_adds_epi16(v1v1_epi16, limit_epi16);
						v1v1_epi16 = _mm_subs_epu16(v1v1_epi16, limit_epi16);

						v1v1_epi16 = _mm_and_si128(v1v1_epi16, _mm_set1_epi32(0xFFFF0000));
						v1v1_epi16  = _mm_srli_epi32(v1v1_epi16, 16); //00vv00vv00vv00vv
					}
					else
					{
						u1v1_epi16 = _mm_slli_epi16(u1v1_epi16, shift);
						
						v1v1_epi16 = _mm_and_si128(u1v1_epi16, _mm_set1_epi32(0xFFFF0000));
						v1v1_epi16  = _mm_srli_epi32(v1v1_epi16, 16); //00vv00vv00vv00vv
					}



					v1v1_epi16 = _mm_packs_epi32 (v1vA_epi16, v1v1_epi16);
					// Store the unpacked chroma values
					_mm_store_si128(output_ptr++, v1v1_epi16);
				}
			}

			// Should have exited the loop at the post processing column
			assert(column == post_column);
		}

	#endif

		// Process the rest of the pixels in the pair of rows
		if(channel == 0)
		{
			for (; column < length; column += 4)
			{
				int y1 = input[column + 0];
				int u  = input[column + 1];
				int y2 = input[column + 2];
				int v  = input[column + 3];

				if(limit_yuv && shift == 2)
				{
					y1 *= 55;
					u  *= 56;
					y2 *= 55;
					v  *= 56;
					//y1 += rand() & 0x7f; y1 -= 0x3f; if(y1 < 0) y1 = 0;  -- idea for adding noise to smooth out shadows -- didn't work that well.
					//y2 += rand() & 0x7f; y2 -= 0x3f; if(y2 < 0) y2 = 0;
					y1 >>= 4;
					u  >>= 4;
					y2 >>= 4;
					v  >>= 4;
					y1 += 64;
					u  += 64;
					y2 += 64;
					v  += 64;		
					
					if(conv_601_709)
					{
						y1 = (1024*y1 - 212*(v-512) - 118*(u-512))>>10;
						y2 = (1024*y2 - 212*(v-512) - 118*(u-512))>>10;		
					}
				}
				else if(conv_601_709 && shift == 2)
				{
					y1 = (1024*y1 - 212*(v-128) - 118*(u-128))>>8;
					y2 = (1024*y2 - 212*(v-128) - 118*(u-128))>>8;		
				}
				else
				{
					y1 <<= shift;
					y2 <<= shift;
				}

				output[column/2 + 0] = y1;
				output[column/2 + 1] = y2;
			}
		}
		else if (channel == 1) //V
		{
			for (; column < length; column += 4)
			{				
				int u  = input[column + 1];
				int v  = input[column + 3];

				if(limit_yuv && shift == 2)
				{
					u  *= 56;
					v  *= 56;
					u  >>= 4;
					v  >>= 4;
					u  += 64;
					v  += 64;

					if(conv_601_709)
					{
						v = ((1049*(v-512) +   76*(u-512))>>10) + 512;
					}
				}
				else if(conv_601_709 && shift == 2)
				{
					v = ((1049*(v-128) +   76*(u-128))>>8) + 512;
				}
				else
				{
					v  <<= shift;
				}

				output[column/4] = v;
			}
		}
		else //U
		{
			for (; column < length; column += 4)
			{
				int u  = input[column + 1];
				int v  = input[column + 3];

				if(limit_yuv && shift == 2)
				{
					u  *= 56;
					v  *= 56;
					u  >>= 4;
					v  >>= 4;
					u  += 64;
					v  += 64;

					if(conv_601_709)
					{
						u = (( 116*(v-512) + 1043*(u-512))>>10) + 512;
					}
				}
				else if(conv_601_709 && shift == 2)
				{
					u = (( 116*(v-128) + 1043*(u-128))>>8) + 512;
				}
				else
				{
					u  <<= shift;
				}	
				
				output[column/4] = u;
			}
		}
	}

	else // UYVY
	{

#if (0 && XMMOPT)

		//TODO: Added shift instructions but have not tested this code

		// Preload the packed pixels
		input1_pu8 = *(input_ptr++);

		{
			__m64 input1_pi16;
			__m64 input2_pi16;


			if (channel == 0)
			{
				for (; column < post_column-column_step; column += column_step)
				{
					// Preload the next set of packed pixels
					input2_pu8 = *(input_ptr++);

					// Unpack the first four luma values
					//input1_pi16 = _mm_and_si64(input1_pu8, _mm_set1_pi16(0x00FF));
					input1_pi16 = _mm_srli_pi16(input1_pu8, 8);
					input1_pi16 = _mm_slli_pi16(input1_pi16, shift);

					// Store the unpacked luma values
					*(output_ptr++) = input1_pi16;

					// Preload the next set of packed pixels
					input1_pu8 = *(input_ptr++);

					// Unpack the second four luma values
					//input2_pi16 = _mm_and_si64(input2_pu8, _mm_set1_pi16(0x00FF));
					input2_pi16 = _mm_srli_pi16(input2_pu8, 8);
					input2_pi16 = _mm_slli_pi16(input2_pi16, shift);

					// Store the unpacked luma values
					*(output_ptr++) = input2_pi16;
				}
			}
			else if (channel == 1)
			{
				for (; column < post_column-column_step; column += column_step)
				{
					// Preload the next set of packed pixels
					input2_pu8 = *(input_ptr++);

					// Unpack the first two u chroma values
					//input1_pi16 = _mm_srli_pi32(input1_pu8, 24);
					input1_pi16 = _mm_slli_pi32(input1_pu8, 24);
					input1_pi16 = _mm_srli_pi32(input1_pi16, 24);

					// Preload the next set of packed pixels
					input1_pu8 = *(input_ptr++);

					// Unpack the second two u chroma values
					//input2_pi16 = _mm_srli_pi32(input2_pu8, 24);
					input2_pi16 = _mm_slli_pi32(input2_pu8, 24);
					input2_pi16 = _mm_srli_pi32(input2_pi16, 24);

					// Combine the two sets of u chroma values
					input1_pi16 = _mm_packs_pi32(input1_pi16, input2_pi16);
					input1_pi16 = _mm_slli_pi16(input1_pi16, shift);

					// Store the unpacked chroma values
					*(output_ptr++) = input1_pi16;
				}
			}
			else
			{
				for (; column < post_column-column_step; column += column_step)
				{
					// Preload the next set of packed pixels
					input2_pu8 = *(input_ptr++);

					// Unpack the first two v chroma values
					input1_pi16 = _mm_slli_pi32(input1_pu8, 8);
					input1_pi16 = _mm_srli_pi32(input1_pi16, 24);

					// Preload the next set of packed pixels
					input1_pu8 = *(input_ptr++);

					// Unpack the second two v chroma values
					input2_pi16 = _mm_slli_pi32(input2_pu8, 8);
					input2_pi16 = _mm_srli_pi32(input2_pi16, 24);

					// Combine the two sets of v chroma values
					input1_pi16 = _mm_packs_pi32(input1_pi16, input2_pi16);
					input1_pi16 = _mm_slli_pi16(input1_pi16, shift);

					// Store the unpacked chroma values
					*(output_ptr++) = input1_pi16;
				}
			}


			// Do the last column.
			column += column_step;
			input2_pu8 = *(input_ptr++);
			if (channel == 0)
			{
				// Unpack the first four luma values
				//input1_pi16 = _mm_and_si64(input1_pu8, _mm_set1_pi16(0x00FF));
				input1_pi16 = _mm_srli_pi16(input1_pu8, 8);
				input1_pi16 = _mm_slli_pi16(input1_pi16, shift);

				// Store the unpacked luma values
				*(output_ptr++) = input1_pi16;

				// Unpack the second four luma values
				//input2_pi16 = _mm_and_si64(input2_pu8, _mm_set1_pi16(0x00FF));
				input2_pi16 = _mm_srli_pi16(input2_pu8, 8);
				input2_pi16 = _mm_slli_pi16(input2_pi16, shift);

				// Store the unpacked luma values
				*(output_ptr++) = input2_pi16;
			}
			else if (channel == 1)
			{
				// Unpack the first two u chroma values
				input1_pi16 = _mm_slli_pi32(input1_pu8, 8);
				input1_pi16 = _mm_srli_pi32(input1_pi16, 24);

				// Unpack the second two u chroma values
				input2_pi16 = _mm_slli_pi32(input2_pu8, 8);
				input2_pi16 = _mm_srli_pi32(input2_pi16, 24);

				// Combine the two sets of u chroma values
				input1_pi16 = _mm_packs_pi32(input1_pi16, input2_pi16);
				input1_pi16 = _mm_slli_pi16(input1_pi16, shift);

				// Store the unpacked chroma values
				*(output_ptr++) = input1_pi16;
			}
			else
			{
				// Unpack the first two v chroma values
				input1_pi16 = _mm_slli_pi32(input1_pu8, 24);
				input1_pi16 = _mm_srli_pi32(input1_pi16, 24);

				// Unpack the second two v chroma values
				input2_pi16 = _mm_slli_pi32(input2_pu8, 24);
				input2_pi16 = _mm_srli_pi32(input2_pi16, 24);

				// Combine the two sets of v chroma values
				input1_pi16 = _mm_packs_pi32(input1_pi16, input2_pi16);
				input1_pi16 = _mm_slli_pi16(input1_pi16, shift);

				// Store the unpacked chroma values
				*(output_ptr++) = input1_pi16;
			}
		}

		//_mm_empty();		// Clear the mmx register state

		// Should have exited the loop at the post processing column
		assert(column == post_column);

#endif

		// Process the rest of the pixels in the pair of rows
		if(channel == 0)
		{
			for (; column < length; column += 4)
			{
				int y1 = input[column + 1];
				int u  = input[column + 0];
				int y2 = input[column + 3];
				int v  = input[column + 2];

				if(limit_yuv && shift == 2)
				{
					y1 *= 55;
					u  *= 56;
					y2 *= 55;
					v  *= 56;
					//y1 += rand() & 0x7f; y1 -= 0x3f; if(y1 < 0) y1 = 0;  -- idea for adding noise to smooth out shadows -- didn't work that well.
					//y2 += rand() & 0x7f; y2 -= 0x3f; if(y2 < 0) y2 = 0;
					y1 >>= 4;
					u  >>= 4;
					y2 >>= 4;
					v  >>= 4;
					y1 += 64;
					u  += 64;
					y2 += 64;
					v  += 64;		
					
					if(conv_601_709)
					{
						y1 = (1024*y1 - 212*(v-512) - 118*(u-512))>>10;
						y2 = (1024*y2 - 212*(v-512) - 118*(u-512))>>10;		
					}
				}
				else if(conv_601_709 && shift == 2)
				{
					y1 = (1024*y1 - 212*(v-128) - 118*(u-128))>>8;
					y2 = (1024*y2 - 212*(v-128) - 118*(u-128))>>8;		
				}
				else
				{
					y1 <<= shift;
					y2 <<= shift;
				}

				output[column/2 + 0] = y1;
				output[column/2 + 1] = y2;
			}
		}
		else if (channel == 1) //V
		{
			for (; column < length; column += 4)
			{				
				int u  = input[column + 0];
				int v  = input[column + 2];

				if(limit_yuv && shift == 2)
				{
					u  *= 56;
					v  *= 56;
					u  >>= 4;
					v  >>= 4;
					u  += 64;
					v  += 64;

					if(conv_601_709)
					{
						v = ((1049*(v-512) +   76*(u-512))>>10) + 512;
					}
				}
				else if(conv_601_709 && shift == 2)
				{
					v = ((1049*(v-128) +   76*(u-128))>>8) + 512;
				}
				else
				{
					v  <<= shift;
				}

				output[column/4] = v;
			}
		}
		else //U
		{
			for (; column < length; column += 4)
			{
				int u  = input[column + 0];
				int v  = input[column + 2];

				if(limit_yuv && shift == 2)
				{
					u  *= 56;
					v  *= 56;
					u  >>= 4;
					v  >>= 4;
					u  += 64;
					v  += 64;

					if(conv_601_709)
					{
						u = (( 116*(v-512) + 1043*(u-512))>>10) + 512;
					}
				}
				else if(conv_601_709 && shift == 2)
				{
					u = (( 116*(v-128) + 1043*(u-128))>>8) + 512;
				}
				else
				{
					u  <<= shift;
				}	
				
				output[column/4] = u;
			}
		}
	}
}



void UnpackYUVRow16s(uint8_t *input, int width, PIXEL *output[3])
{
	// The chroma values are swapped during unpacking
	PIXEL *y_output = output[0];
	PIXEL *u_output = output[1];
	PIXEL *v_output = output[2];

#if (1 && XMMOPT)

	__m128i *input_ptr = (__m128i *)input;
	//__m128i *input_ptr = (__m128i *)test_input;

	__m128i *y_ptr = (__m128i *)y_output;
	__m128i *u_ptr = (__m128i *)u_output;
	__m128i *v_ptr = (__m128i *)v_output;

	__m128i input1_epu8;
	__m128i input2_epu8;
	__m128i input3_epu8;
	__m128i input4_epu8;

#endif

	// Compute the length of the row (in bytes) from the row width (in pixels)
	int length = 2 * width;
	//int length = sizeof(test_input);

	// Process 64 bytes to get 32 bytes of luma and 32 bytes of chroma per loop iteration
	int column_step = 64;
	int post_column = length - (length % column_step);

	int column = 0;

#if (1 && XMMOPT)

	//__m128i input1_epi16;
	//__m128i input2_epi16;
	//__m128i input3_epi16;
	//__m128i input4_epi16;

	__m128i y1_epi16;
	__m128i y2_epi16;

	__m128i u1_epi16;
	__m128i u2_epi16;
	__m128i u3_epi16;
	__m128i u4_epi16;

	__m128i v1_epi16;
	__m128i v2_epi16;
	__m128i v3_epi16;
	__m128i v4_epi16;

	//__m128i output_epu8;

	// Preload the first set of packed pixels
	input1_epu8 = _mm_load_si128(input_ptr++);

	for (; column < post_column - column_step; column += column_step)
	{
		// Preload the second set of packed pixels
		input2_epu8 = _mm_load_si128(input_ptr++);

		// Unpack the first four luma values
		y1_epi16 = _mm_and_si128(input1_epu8, _mm_set1_epi16(0x00FF));

		// Unpack the first two u chroma values
		u1_epi16 = _mm_srli_epi32(input1_epu8, 24);

		// Unpack the first two v chroma values
		v1_epi16 = _mm_slli_epi32(input1_epu8, 16);
		v1_epi16 = _mm_srli_epi32(v1_epi16, 24);

		// Preload the next set of packed pixels
		input3_epu8 = _mm_load_si128(input_ptr++);

		// Unpack the second four luma values
		y2_epi16 = _mm_and_si128(input2_epu8, _mm_set1_epi16(0x00FF));

		// Pack and store eight bytes of luma
		//output_pu8 = _mm_packs_epu16(y1_epi16, y2_epi16);

		// Store the luma values
		_mm_store_si128(y_ptr++, y1_epi16);
		_mm_store_si128(y_ptr++, y2_epi16);

		// Unpack the second two u chroma values
		u2_epi16 = _mm_srli_epi32(input2_epu8, 24);

		// Unpack the second two v chroma values
		v2_epi16 = _mm_slli_epi32(input2_epu8, 16);
		v2_epi16 = _mm_srli_epi32(v2_epi16, 24);

		// Combine the first and second sets of u chroma values
		u1_epi16 = _mm_packs_epi32(u1_epi16, u2_epi16);

		// Combine the first and second sets of v chroma values
		v1_epi16 = _mm_packs_epi32(v1_epi16, v2_epi16);

		// Preload the next set of packed pixels
		input4_epu8 = _mm_load_si128(input_ptr++);

		// Unpack the third four luma values
		y1_epi16 = _mm_and_si128(input3_epu8, _mm_set1_epi16(0x00FF));

		// Unpack the third two u chroma values
		u3_epi16 = _mm_srli_epi32(input3_epu8, 24);

		// Unpack the third two v chroma values
		v3_epi16 = _mm_slli_epi32(input3_epu8, 16);
		v3_epi16 = _mm_srli_epi32(v3_epi16, 24);

		// Preload the next set of packed pixels
		input1_epu8 = *(input_ptr++);

		// Unpack the fourth four luma values
		y2_epi16 = _mm_and_si128(input4_epu8, _mm_set1_epi16(0x00FF));

		// Pack and store eight bytes of luma
		//output_pu8 = _mm_packs_epu16(y1_epi16, y2_epi16);
		//*(y_ptr++) = output_epu8;

		// Store the luma values
		_mm_store_si128(y_ptr++, y1_epi16);
		_mm_store_si128(y_ptr++, y2_epi16);

		// Unpack the fourth two u chroma values
		u4_epi16 = _mm_srli_epi32(input4_epu8, 24);

		// Unpack the fourth two v chroma values
		v4_epi16 = _mm_slli_epi32(input4_epu8, 16);
		v4_epi16 = _mm_srli_epi32(v4_epi16, 24);

		// Combine the third and fourth sets of u chroma values
		u3_epi16 = _mm_packs_epi32(u3_epi16, u4_epi16);

		// Pack and store eight bytes of u chroma
		//output_epu8 = _mm_packs_epu16(u1_epi16, u3_epi16);
		//*(u_ptr++) = output_epu8;
		//_mm_stream_pi(u_eptr++, output_epu8);

		// Store the chroma values
		_mm_store_si128(u_ptr++, u1_epi16);
		_mm_store_si128(u_ptr++, u3_epi16);

		// Combine the third and fourth sets of v chroma values
		v3_epi16 = _mm_packs_epi32(v3_epi16, v4_epi16);

		// Pack and store eight bytes of v chroma
		//output_epu8 = _mm_packs_epu16(v1_epi16, v3_epi16);
		//*(v_ptr++) = output_epu8;
		//_mm_stream_pi(v_eptr++, output_epu8);

		// Store the chroma values
		_mm_store_si128(v_ptr++, v1_epi16);
		_mm_store_si128(v_ptr++, v3_epi16);
	}

	// Process the last group of columns without preloading the last set
	column += column_step;
	input2_epu8 = *(input_ptr++);

	// Unpack the first four luma values
	y1_epi16 = _mm_and_si128(input1_epu8, _mm_set1_epi16(0x00FF));

	// Unpack the first two u chroma values
	u1_epi16 = _mm_srli_epi32(input1_epu8, 24);

	// Unpack the first two v chroma values
	v1_epi16 = _mm_slli_epi32(input1_epu8, 16);
	v1_epi16 = _mm_srli_epi32(v1_epi16, 24);

	// Preload the next set of packed pixels
	input3_epu8 = *(input_ptr++);

	// Unpack the second four luma values
	y2_epi16 = _mm_and_si128(input2_epu8, _mm_set1_epi16(0x00FF));

	// Pack and store eight bytes of luma
	//output_epu8 = _mm_packs_epu16(y1_epi16, y2_epi16);
	//*(y_ptr++) = output_epu8;

	// Store the luma values
	*(y_ptr++) = y1_epi16;
	*(y_ptr++) = y2_epi16;

	// Unpack the second two u chroma values
	u2_epi16 = _mm_srli_epi32(input2_epu8, 24);

	// Unpack the second two v chroma values
	v2_epi16 = _mm_slli_epi32(input2_epu8, 16);
	v2_epi16 = _mm_srli_epi32(v2_epi16, 24);

	// Combine the first and second sets of u chroma values
	u1_epi16 = _mm_packs_epi32(u1_epi16, u2_epi16);

	// Combine the first and second sets of v chroma values
	v1_epi16 = _mm_packs_epi32(v1_epi16, v2_epi16);

	// Preload the next set of packed pixels
	input4_epu8 = *(input_ptr++);

	// Unpack the third four luma values
	y1_epi16 = _mm_and_si128(input3_epu8, _mm_set1_epi16(0x00FF));

	// Unpack the third two u chroma values
	u3_epi16 = _mm_srli_epi32(input3_epu8, 24);

	// Unpack the third two v chroma values
	v3_epi16 = _mm_slli_epi32(input3_epu8, 16);
	v3_epi16 = _mm_srli_epi32(v3_epi16, 24);

	// Unpack the fourth four luma values
	y1_epi16 = _mm_and_si128(input4_epu8, _mm_set1_epi16(0x00FF));

	// Pack and store eight bytes of luma
	//output_epu8 = _mm_packs_epu16(y1_epi16, y2_epi16);
	//*(y_ptr++) = output_epu8;

	// Store the luma values
	*(y_ptr++) = y1_epi16;
	*(y_ptr++) = y2_epi16;

	// Unpack the fourth two u chroma values
	u4_epi16 = _mm_srli_epi32(input4_epu8, 24);

	// Unpack the fourth two v chroma values
	v4_epi16 = _mm_slli_epi32(input4_epu8, 16);
	v4_epi16 = _mm_srli_epi32(v4_epi16, 24);

	// Combine the third and fourth sets of u chroma values
	u3_epi16 = _mm_packs_epi32(u3_epi16, u4_epi16);

	// Pack and store eight bytes of u chroma
	//output_epu8 = _mm_packs_epu16(u1_epi16, u3_epi16);
	//*(u_ptr++) = output_epu8;
	//_mm_stream_pi(u_eptr++, output_epu8);

	// Store the chroma values
	*(u_ptr++) = u1_epi16;
	*(u_ptr++) = u3_epi16;

	// Combine the third and fourth sets of v chroma values
	v3_epi16 = _mm_packs_epi32(v3_epi16, v4_epi16);

	// Pack and store eight bytes of v chroma
	//output_epu8 = _mm_packs_epu16(v1_epi16, v3_epi16);
	//*(v_eptr++) = output_pu8;
	//_mm_stream_pi(v_eptr++, output_epu8);

	// Store the chroma values
	*(v_ptr++) = v1_epi16;
	*(v_ptr++) = v3_epi16;

	// Should have exited the loop at the post processing column
	assert(column == post_column);

#endif

	// Process the rest of the pixels in the row
	for (; column < length; column += 4)
	{
		y_output[column/2 + 0] = input[column + 0];
		y_output[column/2 + 1] = input[column + 2];

		u_output[column/4] = input[column + 3];
		v_output[column/4] = input[column + 1];
	}
}



void ConvertYUVStripPlanarToPacked(uint8_t *planar_output[], int planar_pitch[], ROI strip,
								   uint8_t *output, int output_pitch, int frame_width, int format)
{
	int width = strip.width;
	int height = strip.height;
	uint8_t *luma_row_ptr = planar_output[0];
	uint8_t *u_row_ptr = planar_output[1];
	uint8_t *v_row_ptr = planar_output[2];
	uint8_t *output_row_ptr = output;
	int row;

	// Must have an even number of output pixels
	assert((width % 2) == 0);

	if(format == DECODED_FORMAT_UYVY)
	{
		for (row = 0; row < height; row++)
		{
			int column = 0;

#if (0 && XMMOPT) //DANREMOVED

			int column_step = 16;
			int post_column = width - (width % column_step);

			__m64 *y_ptr = (__m64 *)luma_row_ptr;
			__m64 *u_ptr = (__m64 *)u_row_ptr;
			__m64 *v_ptr = (__m64 *)v_row_ptr;
			__m64 *yuv_ptr = (__m64 *)output_row_ptr;

			for (; column < post_column; column += column_step)
			{
				__m64 y1_pi8 = *(y_ptr++);
				__m64 y2_pi8 = *(y_ptr++);
				__m64 u_pi8 = *(u_ptr++);
				__m64 v_pi8 = *(v_ptr++);
				__m64 uv_pi8;
				__m64 yuv_pi8;

				// Interleave the first four u and v chroma values
				uv_pi8 = _mm_unpacklo_pi8(v_pi8, u_pi8);

				// Interleave and store the first group of four luma and chroma pairs
				yuv_pi8 = _mm_unpacklo_pi8(uv_pi8, y1_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave and store the second group of four luma and chroma pairs
				yuv_pi8 = _mm_unpackhi_pi8(uv_pi8, y1_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave the second four u and v chroma values
				uv_pi8 = _mm_unpackhi_pi8(v_pi8, u_pi8);

				// Interleave and store the third group of four luma and chroma pairs
				yuv_pi8 = _mm_unpacklo_pi8(uv_pi8, y2_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave and store the fourth group of four luma and chroma pairs
				yuv_pi8 = _mm_unpackhi_pi8(uv_pi8, y2_pi8);
				*(yuv_ptr++) = yuv_pi8;
			}

			//_mm_empty();		// Clear the mmx register state

			// Should have exited the loop at the post processing column
			assert(column == post_column);
#endif

			// Process the rest of the row
			for (; column < width; column += 2)
			{
				int y1 = luma_row_ptr[column + 0];
				int y2 = luma_row_ptr[column + 1];

				int u = u_row_ptr[column/2];
				int v = v_row_ptr[column/2];

				output_row_ptr[2 * column + 0] = v;
				output_row_ptr[2 * column + 1] = y1;
				output_row_ptr[2 * column + 2] = u;
				output_row_ptr[2 * column + 3] = y2;
			}

			// Fill the rest of the frame with black
			for (; column < frame_width; column += 2)
			{
				output_row_ptr[2 * column + 0] = COLOR_CHROMA_ZERO;
				output_row_ptr[2 * column + 1] = COLOR_LUMA_BLACK;
			}

			// Advance the input and output row pointers
			luma_row_ptr += planar_pitch[0];
			u_row_ptr += planar_pitch[1];
			v_row_ptr += planar_pitch[2];
			output_row_ptr += output_pitch;
		}
	}
	else if (format == DECODED_FORMAT_YUYV)
	{
		for (row = 0; row < height; row++)
		{
			int column = 0;

#if (0 && XMMOPT) //DANREMOVED

			int column_step = 16;
			int post_column = width - (width % column_step);

			__m64 *y_ptr = (__m64 *)luma_row_ptr;
			__m64 *u_ptr = (__m64 *)u_row_ptr;
			__m64 *v_ptr = (__m64 *)v_row_ptr;
			__m64 *yuv_ptr = (__m64 *)output_row_ptr;

			for (; column < post_column; column += column_step)
			{
				__m64 y1_pi8 = *(y_ptr++);
				__m64 y2_pi8 = *(y_ptr++);
				__m64 u_pi8 = *(u_ptr++);
				__m64 v_pi8 = *(v_ptr++);
				__m64 uv_pi8;
				__m64 yuv_pi8;

				// Interleave the first four u and v chroma values
				uv_pi8 = _mm_unpacklo_pi8(v_pi8, u_pi8);

				// Interleave and store the first group of four luma and chroma pairs
				yuv_pi8 = _mm_unpacklo_pi8(y1_pi8, uv_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave and store the second group of four luma and chroma pairs
				yuv_pi8 = _mm_unpackhi_pi8(y1_pi8, uv_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave the second four u and v chroma values
				uv_pi8 = _mm_unpackhi_pi8(v_pi8, u_pi8);

				// Interleave and store the third group of four luma and chroma pairs
				yuv_pi8 = _mm_unpacklo_pi8(y2_pi8, uv_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave and store the fourth group of four luma and chroma pairs
				yuv_pi8 = _mm_unpackhi_pi8(y2_pi8, uv_pi8);
				*(yuv_ptr++) = yuv_pi8;
			}

			//_mm_empty();		// Clear the mmx register state

			// Should have exited the loop at the post processing column
			assert(column == post_column);
#endif

			// Process the rest of the row
			for (; column < width; column += 2)
			{
				int y1 = luma_row_ptr[column + 0];
				int y2 = luma_row_ptr[column + 1];

				int u = u_row_ptr[column/2];
				int v = v_row_ptr[column/2];

				output_row_ptr[2 * column + 0] = y1;
				output_row_ptr[2 * column + 1] = v;
				output_row_ptr[2 * column + 2] = y2;
				output_row_ptr[2 * column + 3] = u;
			}

			// Fill the rest of the frame with black
			for (; column < frame_width; column += 2)
			{
				output_row_ptr[2 * column + 0] = COLOR_LUMA_BLACK;
				output_row_ptr[2 * column + 1] = COLOR_CHROMA_ZERO;
			}

			// Advance the input and output row pointers
			luma_row_ptr += planar_pitch[0];
			u_row_ptr += planar_pitch[1];
			v_row_ptr += planar_pitch[2];
			output_row_ptr += output_pitch;
		}
	}
}

void ConvertPlanarGRBToPlanarRGB(PIXEL *dstline, PIXEL *srcline, int frame_width)
{
	memcpy(dstline+frame_width, srcline, frame_width*2); // Src Green
	memcpy(dstline, srcline+frame_width, frame_width*2); // Src Red
	memcpy(dstline+frame_width*2, srcline+frame_width*2, frame_width*2); // Src Blue
}

void ConvertPlanarGRBAToPlanarRGBA(PIXEL *dstline, PIXEL *srcline, int frame_width)
{
	memcpy(dstline+frame_width, srcline, frame_width*2); // Src Green
	memcpy(dstline, srcline+frame_width, frame_width*2); // Src Red
	memcpy(dstline+frame_width*2, srcline+frame_width*2, frame_width*2); // Src Blue
	memcpy(dstline+frame_width*3, srcline+frame_width*3, frame_width*2); // Src Alpah
}


void ConvertPlanarRGB16uToPackedB64A(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									 uint8_t *output, int output_pitch, int frame_width)
{
	int width = strip.width;
	int height = strip.height;
	uint8_t *r_row_ptr = (uint8_t *)planar_output[1];
	uint8_t *g_row_ptr = (uint8_t *)planar_output[0];
	uint8_t *b_row_ptr = (uint8_t *)planar_output[2];
	uint8_t *output_row_ptr = output;
	int row;

	const int alpha = USHRT_MAX;

	// Must have an even number of output pixels
	//assert((width % 2) == 0);

	for (row = 0; row < height; row++)
	{
		int column = 0;

#if (1 && XMMOPT)

		int column_step = 8;
		int post_column = width - (width % column_step);

		__m128i *r_ptr = (__m128i *)r_row_ptr;
		__m128i *g_ptr = (__m128i *)g_row_ptr;
		__m128i *b_ptr = (__m128i *)b_row_ptr;
		__m128i *argb_ptr = (__m128i *)output_row_ptr;

		__m128i a_epi16 = _mm_set1_epi16(alpha);

		for (; column < post_column; column += column_step)
		{
			__m128i ar_epi16;
			__m128i gb_epi16;
			__m128i argb_epi16;

			// Load eight of each RGB component
			__m128i r_epi16 = _mm_load_si128(r_ptr++);
			__m128i g_epi16 = _mm_load_si128(g_ptr++);
			__m128i b_epi16 = _mm_load_si128(b_ptr++);

			// Shift the RGB components to 16 bits
			//r_epi16 = _mm_slli_epi16(r_epi16, 4);
			//g_epi16 = _mm_slli_epi16(g_epi16, 4);
			//b_epi16 = _mm_slli_epi16(b_epi16, 4);

			// Interleave the first four alpha and red values
			ar_epi16 = _mm_unpacklo_epi16(a_epi16, r_epi16);

			// Interleave the first four green and blue values
			gb_epi16 = _mm_unpacklo_epi16(g_epi16, b_epi16);

			// Interleave and store the first pair of ARGB tuples
			argb_epi16 = _mm_unpacklo_epi32(ar_epi16, gb_epi16);
			*(argb_ptr++) = argb_epi16;

			// Interleave and store the second pair of ARGB tuples
			argb_epi16 = _mm_unpackhi_epi32(ar_epi16, gb_epi16);
			*(argb_ptr++) = argb_epi16;

			// Interleave the second four alpha and red values
			ar_epi16 = _mm_unpackhi_epi16(a_epi16, r_epi16);

			// Interleave the second four green and blue values
			gb_epi16 = _mm_unpackhi_epi16(g_epi16, b_epi16);

			// Interleave and store the third pair of ARGB tuples
			argb_epi16 = _mm_unpacklo_epi32(ar_epi16, gb_epi16);
			*(argb_ptr++) = argb_epi16;

			// Interleave and store the fourth pair of ARGB tuples
			argb_epi16 = _mm_unpackhi_epi32(ar_epi16, gb_epi16);
			*(argb_ptr++) = argb_epi16;
		}

		// Should have exited the loop at the post processing column
		assert(column == post_column);
#endif

		// Process the rest of the row
		for (; column < width; column++)
		{
			int r = r_row_ptr[column];
			int g = g_row_ptr[column];
			int b = b_row_ptr[column];

			output_row_ptr[4 * column + 0] = alpha;
			output_row_ptr[4 * column + 1] = r;
			output_row_ptr[4 * column + 2] = g;
			output_row_ptr[4 * column + 3] = b;
		}

		// Fill the rest of the frame with black
		for (; column < frame_width; column++)
		{
			output_row_ptr[4 * column + 0] = 0;		//alpha;
			output_row_ptr[4 * column + 1] = 0;
			output_row_ptr[4 * column + 2] = 0;
			output_row_ptr[4 * column + 3] = 0;
		}

		// Advance the input and output row pointers
		r_row_ptr += planar_pitch[0];
		g_row_ptr += planar_pitch[1];
		b_row_ptr += planar_pitch[2];
		output_row_ptr += output_pitch;
	}
}

void ConvertPlanarRGB16uToPackedRGB32(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									  uint8_t *output, int output_pitch, int frame_width,
									  int shift, int num_channels)
{
	int width = strip.width;
	int height = strip.height;
	PIXEL16U *r_row_ptr = (PIXEL16U *)planar_output[1];
	PIXEL16U *g_row_ptr = (PIXEL16U *)planar_output[0];
	PIXEL16U *b_row_ptr = (PIXEL16U *)planar_output[2];
	PIXEL16U *a_row_ptr = (PIXEL16U *)planar_output[3];
	uint8_t *output_row_ptr = output;
	int row;

	const int alpha = UCHAR_MAX;
	const int mask = (1<<(shift-1))-1; //DAN20090601

	//alpha_Companded likely doesn't set as the is not does to go through Convert4444LinesToOutput

	for (row = 0; row < height; row++)
	{
		int column = 0;

#if (1 && XMMOPT)

		int column_step = 8,pos=0;
		int post_column = width - (width % column_step);

		__m128i *r_ptr = (__m128i *)r_row_ptr;
		__m128i *g_ptr = (__m128i *)g_row_ptr;
		__m128i *b_ptr = (__m128i *)b_row_ptr;
		__m128i *a_ptr = (__m128i *)a_row_ptr;
		__m128i *bgra_ptr = (__m128i *)output_row_ptr;

		__m128i a_epi16 = _mm_set1_epi16(alpha);
		__m128i rounding1 = _mm_set1_epi16(0);
		__m128i rounding2 = _mm_set1_epi16(0);
		__m128i overflowprotect_epi16 = _mm_set1_epi16(0x7fff-0x3fff);
		__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x0fff);

		if(shift >= 2)
		{
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 0);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 1);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 2);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 3);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 4);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 5);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 6);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 7);

			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 0);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 1);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 2);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 3);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 4);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 5);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 6);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 7);

			rounding1 = _mm_adds_epi16(rounding1, _mm_set1_epi16(10*mask/32)); //DAN20090601
			rounding2 = _mm_adds_epi16(rounding2, _mm_set1_epi16(10*mask/32));
		}

		for (; column < post_column; column += column_step,pos++)
		{
			// Load eight of each RGB component
			__m128i r_epi16 = _mm_load_si128(r_ptr++);
			__m128i g_epi16 = _mm_load_si128(g_ptr++);
			__m128i b_epi16 = _mm_load_si128(b_ptr++);
			__m128i bg_epi16;
			__m128i ra_epi16;
			__m128i bgra1_epi16;
			__m128i bgra2_epi16;
			__m128i bgra_epi8;

			if(shift < 8) // input data may be signed
			{
				if(pos & 1)
				{
					r_epi16 = _mm_adds_epi16(r_epi16, rounding1);
					g_epi16 = _mm_adds_epi16(g_epi16, rounding1);
					b_epi16 = _mm_adds_epi16(b_epi16, rounding1);
				}
				else
				{
					r_epi16 = _mm_adds_epi16(r_epi16, rounding2);
					g_epi16 = _mm_adds_epi16(g_epi16, rounding2);
					b_epi16 = _mm_adds_epi16(b_epi16, rounding2);
				}


				r_epi16 = _mm_adds_epi16(r_epi16, overflowprotect_epi16);
				r_epi16 = _mm_subs_epu16(r_epi16, overflowprotect_epi16);
				g_epi16 = _mm_adds_epi16(g_epi16, overflowprotect_epi16);
				g_epi16 = _mm_subs_epu16(g_epi16, overflowprotect_epi16);
				b_epi16 = _mm_adds_epi16(b_epi16, overflowprotect_epi16);
				b_epi16 = _mm_subs_epu16(b_epi16, overflowprotect_epi16);
			}
			else
			{
				if(pos & 1)
				{
					r_epi16 = _mm_adds_epu16(r_epi16, rounding1);
					g_epi16 = _mm_adds_epu16(g_epi16, rounding1);
					b_epi16 = _mm_adds_epu16(b_epi16, rounding1);
				}
				else
				{
					r_epi16 = _mm_adds_epu16(r_epi16, rounding2);
					g_epi16 = _mm_adds_epu16(g_epi16, rounding2);
					b_epi16 = _mm_adds_epu16(b_epi16, rounding2);
				}
			}

			// Shift the RGB components to 8 bits of precision
			r_epi16 = _mm_srli_epi16(r_epi16, shift);
			g_epi16 = _mm_srli_epi16(g_epi16, shift);
			b_epi16 = _mm_srli_epi16(b_epi16, shift);

			if(num_channels == 4)
			{
				a_epi16 = _mm_load_si128(a_ptr++);

				if(shift < 8) // input data may be signed
				{
					a_epi16 = _mm_adds_epi16(a_epi16, overflowprotect_epi16);
					a_epi16 = _mm_subs_epu16(a_epi16, overflowprotect_epi16);
				}

				a_epi16 = _mm_srli_epi16(a_epi16, shift); //8-bit

				// Remove the alpha encoding curve. // 12-bit precision
				a_epi16 = _mm_slli_epi16(a_epi16, 4); // 12-bit
				a_epi16 = _mm_subs_epu16(a_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				a_epi16 = _mm_slli_epi16(a_epi16, 3);  //15-bit
				a_epi16 = _mm_mulhi_epi16(a_epi16, _mm_set1_epi16(alphacompandGain));

				a_epi16 = _mm_adds_epi16(a_epi16, limiterRGB); //12-bit limit
				a_epi16 = _mm_subs_epu16(a_epi16, limiterRGB);
				
				a_epi16 = _mm_srli_epi16(a_epi16, 4); // 8-bit
			}
			else
			{
				a_epi16 = _mm_set1_epi16(0xff);
			}

#if (0 && DEBUG)
			r_epi16 = _mm_set_epi16(0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8);
			g_epi16 = _mm_set_epi16(0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8);
			b_epi16 = _mm_set_epi16(0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8);
#endif
			// Interleave the first four blue and green values
			bg_epi16 = _mm_unpacklo_epi16(b_epi16, g_epi16);

			// Interleave the first four red and alpha values
			ra_epi16 = _mm_unpacklo_epi16(r_epi16, a_epi16);

			// Interleave the first pair of BGRA tuples
			bgra1_epi16 = _mm_unpacklo_epi32(bg_epi16, ra_epi16);

			// Interleave the second pair of BGRA tuples
			bgra2_epi16 = _mm_unpackhi_epi32(bg_epi16, ra_epi16);

			// Pack the BGRA values in 8 bits
			bgra_epi8 = _mm_packus_epi16(bgra1_epi16, bgra2_epi16);

			// Store the first four BGRA tuples
			if((uintptr_t)bgra_ptr & 15)
				_mm_storeu_si128(bgra_ptr++, bgra_epi8);
			else
				_mm_store_si128(bgra_ptr++, bgra_epi8);

			// Interleave the second four blue and green values
			bg_epi16 = _mm_unpackhi_epi16(b_epi16, g_epi16);

			// Interleave the second four red and alpha values
			ra_epi16 = _mm_unpackhi_epi16(r_epi16, a_epi16);

			// Interleave the third pair of BGRA tuples
			bgra1_epi16 = _mm_unpacklo_epi32(bg_epi16, ra_epi16);

			// Interleave the fourth pair of BGRA tuples
			bgra2_epi16 = _mm_unpackhi_epi32(bg_epi16, ra_epi16);

			// Pack the BGRA values in 8 bits
			bgra_epi8 = _mm_packus_epi16(bgra1_epi16, bgra2_epi16);

			// Store the second four BGRA tuples
			if((uintptr_t)bgra_ptr & 15)
				_mm_storeu_si128(bgra_ptr++, bgra_epi8);
			else
				_mm_store_si128(bgra_ptr++, bgra_epi8);
		}

		// Should have exited the loop at the post processing column
		assert(column == post_column);
#endif

		//TODO: Finish this post processing code

		// Process the rest of the row
		if(shift < 8) // signed input data from lowpass decode
		{
			PIXEL *rs_row_ptr = (PIXEL *)r_row_ptr;
			PIXEL *gs_row_ptr = (PIXEL *)g_row_ptr;
			PIXEL *bs_row_ptr = (PIXEL *)b_row_ptr;
			PIXEL *as_row_ptr = (PIXEL *)a_row_ptr;
			for (; column < width; column++)
			{
				int r = rs_row_ptr[column];
				int g = gs_row_ptr[column];
				int b = bs_row_ptr[column];
				int a = alpha;
				int rnd = rand() & mask;

				r += rnd;
				g += rnd;
				b += rnd;

				r >>= shift;
				g >>= shift;
				b >>= shift;

				if(num_channels == 4)
				{
					a = as_row_ptr[column];
					a >>= shift;

					// Remove the alpha encoding curve.
					//a -= 16;
					//a <<= 8;
					//a += 111;
					//a /= 223;

					//12-bit SSE calibrated code
					//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 4); // 12-bit
					//a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
					//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
					//a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));
					//a2_output_epi16 = _mm_srai_epi16(a2_output_epi16, 4); // 8-bit

					a <<= 4; //12-bit
					a -= alphacompandDCoffset;
					a <<= 3; //15-bit
					a *= alphacompandGain;
					a >>= 16; //12-bit
					a >>= 4; // 8-bit;


					if(a < 0) a = 0;
					if(a > 255) a = 255;
				}

				if(r < 0) r = 0;
				if(g < 0) g = 0;
				if(b < 0) b = 0;
				if(r > 255) r = 255;
				if(g > 255) g = 255;
				if(b > 255) b = 255;

				output_row_ptr[4 * column + 0] = a;
				output_row_ptr[4 * column + 1] = r;
				output_row_ptr[4 * column + 2] = g;
				output_row_ptr[4 * column + 3] = b;
			}
		}
		else
		{
			for (; column < width; column++)
			{
				int r = r_row_ptr[column];
				int g = g_row_ptr[column];
				int b = b_row_ptr[column];
				int a = alpha;
				int rnd = rand() & mask;

				r += rnd;
				g += rnd;
				b += rnd;

				r >>= shift;
				g >>= shift;
				b >>= shift;

				if(num_channels == 4)
				{
					a = a_row_ptr[column];
					a >>= shift;
					if(a < 0) a = 0;
					if(a > 255) a = 255;
				}

				if(r > 255) r = 255;
				if(g > 255) g = 255;
				if(b > 255) b = 255;

				output_row_ptr[4 * column + 0] = a;
				output_row_ptr[4 * column + 1] = r;
				output_row_ptr[4 * column + 2] = g;
				output_row_ptr[4 * column + 3] = b;
			}
		}

		// Fill the rest of the frame with black
		for (; column < frame_width; column++)
		{
			output_row_ptr[4 * column + 0] = 0;		//alpha;
			output_row_ptr[4 * column + 1] = 0;
			output_row_ptr[4 * column + 2] = 0;
			output_row_ptr[4 * column + 3] = 0;
		}

		// Advance the input and output row pointers
		r_row_ptr += planar_pitch[0]/sizeof(PIXEL16U);
		g_row_ptr += planar_pitch[1]/sizeof(PIXEL16U);
		b_row_ptr += planar_pitch[2]/sizeof(PIXEL16U);
		if(num_channels == 4)
			a_row_ptr += planar_pitch[3]/sizeof(PIXEL16U);
		output_row_ptr += output_pitch;
	}
}


void ConvertPlanarRGB16uToPackedRGB24(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									  uint8_t *output, int output_pitch, int frame_width, int shift)
{
	int width = strip.width;
	int height = strip.height;
	PIXEL16U *r_row_ptr = (PIXEL16U *)planar_output[1];
	PIXEL16U *g_row_ptr = (PIXEL16U *)planar_output[0];
	PIXEL16U *b_row_ptr = (PIXEL16U *)planar_output[2];
	uint8_t *output_row_ptr = output;
	int row;
	const int mask = (1<<(shift-1))-1; //DAN20090601

	for (row = 0; row < height; row++)
	{
		int column = 0;


#if (1 && XMMOPT)

		int column_step = 8,pos=0;
		int post_column = width - (width % column_step);

		__m128i *r_ptr = (__m128i *)r_row_ptr;
		__m128i *g_ptr = (__m128i *)g_row_ptr;
		__m128i *b_ptr = (__m128i *)b_row_ptr;

		__m128i *rgba_group1_ptr = (__m128i *)r_row_ptr;
		__m128i *rgba_group2_ptr = (__m128i *)g_row_ptr;

		//__m128i *bgra_ptr = (__m128i *)output_row_ptr;

		__m128i rounding1 = _mm_set1_epi16(0);
		__m128i rounding2 = _mm_set1_epi16(0);
		__m128i overflowprotect_epi16 = _mm_set1_epi16(0x7fff-0x3fff);
		__m128i a_epi16 = _mm_set1_epi16(0);

		if(shift>=2)
		{
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 0);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 1);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 2);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 3);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 4);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 5);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 6);
			rounding1 = _mm_insert_epi16(rounding1, rand()&mask, 7);

			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 0);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 1);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 2);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 3);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 4);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 5);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 6);
			rounding2 = _mm_insert_epi16(rounding2, rand()&mask, 7);

			rounding1 = _mm_adds_epi16(rounding1, _mm_set1_epi16(10*mask/32)); //DAN20090601
			rounding2 = _mm_adds_epi16(rounding2, _mm_set1_epi16(10*mask/32));
		}

		for (; column < post_column; column += column_step,pos++)
		{
			// Load eight of each RGB component
			__m128i r_epi16 = _mm_load_si128(r_ptr++);
			__m128i g_epi16 = _mm_load_si128(g_ptr++);
			__m128i b_epi16 = _mm_load_si128(b_ptr++);
			__m128i bg_epi16;
			__m128i ra_epi16;
			__m128i bgra1_epi16;
			__m128i bgra2_epi16;
			__m128i bgra_epi8;

			if(shift < 8) // input data may be signed
			{
				if(pos & 1)
				{
					r_epi16 = _mm_adds_epi16(r_epi16, rounding1);
					g_epi16 = _mm_adds_epi16(g_epi16, rounding1);
					b_epi16 = _mm_adds_epi16(b_epi16, rounding1);
				}
				else
				{
					r_epi16 = _mm_adds_epi16(r_epi16, rounding2);
					g_epi16 = _mm_adds_epi16(g_epi16, rounding2);
					b_epi16 = _mm_adds_epi16(b_epi16, rounding2);
				}


				r_epi16 = _mm_adds_epi16(r_epi16, overflowprotect_epi16);
				r_epi16 = _mm_subs_epu16(r_epi16, overflowprotect_epi16);
				g_epi16 = _mm_adds_epi16(g_epi16, overflowprotect_epi16);
				g_epi16 = _mm_subs_epu16(g_epi16, overflowprotect_epi16);
				b_epi16 = _mm_adds_epi16(b_epi16, overflowprotect_epi16);
				b_epi16 = _mm_subs_epu16(b_epi16, overflowprotect_epi16);
			}
			else
			{
				if(pos & 1)
				{
					r_epi16 = _mm_adds_epu16(r_epi16, rounding1);
					g_epi16 = _mm_adds_epu16(g_epi16, rounding1);
					b_epi16 = _mm_adds_epu16(b_epi16, rounding1);
				}
				else
				{
					r_epi16 = _mm_adds_epu16(r_epi16, rounding2);
					g_epi16 = _mm_adds_epu16(g_epi16, rounding2);
					b_epi16 = _mm_adds_epu16(b_epi16, rounding2);
				}
			}

			// Shift the RGB components to 8 bits of precision
			r_epi16 = _mm_srli_epi16(r_epi16, shift);
			g_epi16 = _mm_srli_epi16(g_epi16, shift);
			b_epi16 = _mm_srli_epi16(b_epi16, shift);

			// Interleave the first four blue and green values
			bg_epi16 = _mm_unpacklo_epi16(b_epi16, g_epi16);

			// Interleave the first four red and alpha values
			ra_epi16 = _mm_unpacklo_epi16(r_epi16, a_epi16);

			// Interleave the first pair of BGRA tuples
			bgra1_epi16 = _mm_unpacklo_epi32(bg_epi16, ra_epi16);

			// Interleave the second pair of BGRA tuples
			bgra2_epi16 = _mm_unpackhi_epi32(bg_epi16, ra_epi16);

			// Pack the BGRA values in 8 bits
			bgra_epi8 = _mm_packus_epi16(bgra1_epi16, bgra2_epi16);

			// Store the first four BGRA tuples
			if((uintptr_t)rgba_group1_ptr & 15)
				_mm_storeu_si128(rgba_group1_ptr++, bgra_epi8);
			else
				_mm_store_si128(rgba_group1_ptr++, bgra_epi8);

			// Interleave the second four blue and green values
			bg_epi16 = _mm_unpackhi_epi16(b_epi16, g_epi16);

			// Interleave the second four red and alpha values
			ra_epi16 = _mm_unpackhi_epi16(r_epi16, a_epi16);

			// Interleave the third pair of BGRA tuples
			bgra1_epi16 = _mm_unpacklo_epi32(bg_epi16, ra_epi16);

			// Interleave the fourth pair of BGRA tuples
			bgra2_epi16 = _mm_unpackhi_epi32(bg_epi16, ra_epi16);

			// Pack the BGRA values in 8 bits
			bgra_epi8 = _mm_packus_epi16(bgra1_epi16, bgra2_epi16);

			// Store the second four BGRA tuples
			if((uintptr_t)rgba_group2_ptr & 15)
				_mm_storeu_si128(rgba_group2_ptr++, bgra_epi8);
			else
				_mm_store_si128(rgba_group2_ptr++, bgra_epi8);
		}

		// Should have exited the loop at the post processing column
		assert(column == post_column);

		{
			uint8_t *rgba_group1_ptr = (uint8_t *)r_row_ptr;
			uint8_t *rgba_group2_ptr = (uint8_t *)g_row_ptr;
			for (column=0; column < post_column; column+=column_step)
			{
				int group = column;
				for(;group < column+4; group++)
				{
					int r = *rgba_group1_ptr++;
					int g = *rgba_group1_ptr++;
					int b = *rgba_group1_ptr++;
					rgba_group1_ptr++;

					output_row_ptr[3 * group + 0] = r;
					output_row_ptr[3 * group + 1] = g;
					output_row_ptr[3 * group + 2] = b;
				}
				for(;group < column+8; group++)
				{
					int r = *rgba_group2_ptr++;
					int g = *rgba_group2_ptr++;
					int b = *rgba_group2_ptr++;
					rgba_group2_ptr++;

					output_row_ptr[3 * group + 0] = r;
					output_row_ptr[3 * group + 1] = g;
					output_row_ptr[3 * group + 2] = b;
				}
			}
		}
#endif

		//TODO: Finish this post processing code

		// Process the rest of the row
		if(shift < 8) // signed input data from lowpass decode
		{
			PIXEL *rs_row_ptr = (PIXEL *)r_row_ptr;
			PIXEL *gs_row_ptr = (PIXEL *)g_row_ptr;
			PIXEL *bs_row_ptr = (PIXEL *)b_row_ptr;
			for (; column < width; column++)
			{
				int r = rs_row_ptr[column];
				int g = gs_row_ptr[column];
				int b = bs_row_ptr[column];
				int rnd = rand() & mask;

				r += rnd;
				g += rnd;
				b += rnd;

				r >>= shift;
				g >>= shift;
				b >>= shift;

				if(r < 0) r = 0;
				if(g < 0) g = 0;
				if(b < 0) b = 0;
				if(r > 255) r = 255;
				if(g > 255) g = 255;
				if(b > 255) b = 255;

				output_row_ptr[3 * column + 0] = b;
				output_row_ptr[3 * column + 1] = g;
				output_row_ptr[3 * column + 2] = r;
			}
		}
		else
		{
			for (; column < width; column++)
			{
				int r = r_row_ptr[column];
				int g = g_row_ptr[column];
				int b = b_row_ptr[column];
				int rnd = rand() & mask;

				r += rnd;
				g += rnd;
				b += rnd;

				r >>= shift;
				g >>= shift;
				b >>= shift;

				if(r > 255) r = 255;
				if(g > 255) g = 255;
				if(b > 255) b = 255;

				output_row_ptr[3 * column + 0] = b;
				output_row_ptr[3 * column + 1] = g;
				output_row_ptr[3 * column + 2] = r;
			}
		}

		// Fill the rest of the frame with black
		for (; column < frame_width; column++)
		{
			output_row_ptr[3 * column + 0] = 0;
			output_row_ptr[3 * column + 1] = 0;
			output_row_ptr[3 * column + 2] = 0;
		}

		// Advance the input and output row pointers
		r_row_ptr += planar_pitch[0]/sizeof(PIXEL16U);
		g_row_ptr += planar_pitch[1]/sizeof(PIXEL16U);
		b_row_ptr += planar_pitch[2]/sizeof(PIXEL16U);
		output_row_ptr += output_pitch;
	}
}

void ConvertPlanarRGB16uToPackedRGB48(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									  uint8_t *output_buffer, int output_pitch, int frame_width)
{
	uint8_t *r_row_ptr = (uint8_t *)planar_output[1];
	uint8_t *g_row_ptr = (uint8_t *)planar_output[0];
	uint8_t *b_row_ptr = (uint8_t *)planar_output[2];

	uint8_t *output_row_ptr = output_buffer;

	//const int rgb_max = UINT16_MAX;

	int width = strip.width;
	int height = strip.height;
	int row;

	for (row = 0; row < height; row++)
	{
		int column = 0;

		//TODO: Need to finish the optimized code

#if (0 && XMMOPT)

		int column_step = 8;
		int post_column = width - (width % column_step);

		__m128i *r_ptr = (__m128i *)r_row_ptr;
		__m128i *g_ptr = (__m128i *)g_row_ptr;
		__m128i *b_ptr = (__m128i *)b_row_ptr;
		__m128i *rgb_ptr = (__m128i *)output_row_ptr;

		__m128i a_epi16 = _mm_set1_epi16(alpha);

		for (; column < post_column; column += column_step)
		{
			// Load eight of each RGB component
			__m128i r_epi16 = _mm_load_si128(r_ptr++);
			__m128i g_epi16 = _mm_load_si128(g_ptr++);
			__m128i b_epi16 = _mm_load_si128(b_ptr++);
			__m128i bg_epi16;
			__m128i ra_epi16;
			__m128i rgb1_epi16;
			__m128i rgb2_epi16;
			__m128i rgb_epi8;

			// Shift the RGB components to 8 bits of precision
			r_epi16 = _mm_srli_epi16(r_epi16, 8);
			g_epi16 = _mm_srli_epi16(g_epi16, 8);
			b_epi16 = _mm_srli_epi16(b_epi16, 8);

#if (0 && DEBUG)
			r_epi16 = _mm_set_epi16(0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8);
			g_epi16 = _mm_set_epi16(0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8);
			b_epi16 = _mm_set_epi16(0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8);
#endif
			// Interleave the first four blue and green values
			bg_epi16 = _mm_unpacklo_epi16(b_epi16, g_epi16);

			// Interleave the first four red and alpha values
			ra_epi16 = _mm_unpacklo_epi16(r_epi16, a_epi16);

			// Interleave the first pair of BGRA tuples
			bgra1_epi16 = _mm_unpacklo_epi32(bg_epi16, ra_epi16);

			// Interleave the second pair of BGRA tuples
			bgra2_epi16 = _mm_unpackhi_epi32(bg_epi16, ra_epi16);

			// Pack the BGRA values in 8 bits
			bgra_epi8 = _mm_packus_epi16(bgra1_epi16, bgra2_epi16);

			// Store the first four BGRA tuples
			_mm_store_si128(rgb_ptr++, bgra_epi8);

			// Interleave the second four blue and green values
			bg_epi16 = _mm_unpackhi_epi16(b_epi16, g_epi16);

			// Interleave the second four red and alpha values
			ra_epi16 = _mm_unpackhi_epi16(r_epi16, a_epi16);

			// Interleave the third pair of BGRA tuples
			bgra1_epi16 = _mm_unpacklo_epi32(bg_epi16, ra_epi16);

			// Interleave the fourth pair of BGRA tuples
			bgra2_epi16 = _mm_unpackhi_epi32(bg_epi16, ra_epi16);

			// Pack the BGRA values in 8 bits
			bgra_epi8 = _mm_packus_epi16(bgra1_epi16, bgra2_epi16);

			// Store the second four BGRA tuples
			_mm_store_si128(rgb_ptr++, rgb_epi8);
		}

		// Should have exited the loop at the post processing column
		assert(column == post_column);
#endif

		if (column < frame_width)
		{
			PIXEL16U *r_input_ptr = (PIXEL16U *)r_row_ptr;
			PIXEL16U *g_input_ptr = (PIXEL16U *)g_row_ptr;
			PIXEL16U *b_input_ptr = (PIXEL16U *)b_row_ptr;

			PIXEL16U *rgb_output_ptr = (PIXEL16U *)output_row_ptr;

			// Process the rest of the input and output rows
			for (; column < width; column++)
			{
				int r = r_input_ptr[column];
				int g = g_input_ptr[column];
				int b = b_input_ptr[column];
#if 0
				if (r < 0) r = 0; else if (r > rgb_max) r = rgb_max;
				if (g < 0) g = 0; else if (g > rgb_max) g = rgb_max;
				if (b < 0) b = 0; else if (b > rgb_max) b = rgb_max;
#endif
				rgb_output_ptr[3 * column + 0] = r;
				rgb_output_ptr[3 * column + 1] = g;
				rgb_output_ptr[3 * column + 2] = b;
			}

			// Fill the rest of the frame with black
			for (; column < frame_width; column++)
			{
				rgb_output_ptr[3 * column + 0] = 0;
				rgb_output_ptr[3 * column + 1] = 0;
				rgb_output_ptr[3 * column + 2] = 0;
			}
		}

		// Advance the input row pointers
		r_row_ptr += planar_pitch[1];
		g_row_ptr += planar_pitch[0];
		b_row_ptr += planar_pitch[2];

		// Advance the output row pointer
		output_row_ptr += output_pitch;
	}
}




void ConvertPlanarRGB16uToPackedRGBA64(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									  uint8_t *output_buffer, int output_pitch, int frame_width)
{
	uint8_t *r_row_ptr = (uint8_t *)planar_output[1];
	uint8_t *g_row_ptr = (uint8_t *)planar_output[0];
	uint8_t *b_row_ptr = (uint8_t *)planar_output[2];
	uint8_t *a_row_ptr = (uint8_t *)planar_output[3];

	uint8_t *output_row_ptr = output_buffer;

	const int rgb_max = USHRT_MAX;

	int width = strip.width;
	int height = strip.height;
	int row;

	//alpha_Companded likely doesn't set as the is not does to go through Convert4444LinesToOutput

	for (row = 0; row < height; row++)
	{
		int column = 0;

		//TODO: Need to finish the optimized code

#if (0 && XMMOPT)

		int column_step = 8;
		int post_column = width - (width % column_step);

		__m128i *r_ptr = (__m128i *)r_row_ptr;
		__m128i *g_ptr = (__m128i *)g_row_ptr;
		__m128i *b_ptr = (__m128i *)b_row_ptr;
		__m128i *rgb_ptr = (__m128i *)output_row_ptr;

		__m128i a_epi16 = _mm_set1_epi16(alpha);

		for (; column < post_column; column += column_step)
		{
			// Load eight of each RGB component
			__m128i r_epi16 = _mm_load_si128(r_ptr++);
			__m128i g_epi16 = _mm_load_si128(g_ptr++);
			__m128i b_epi16 = _mm_load_si128(b_ptr++);
			__m128i bg_epi16;
			__m128i ra_epi16;
			__m128i rgb1_epi16;
			__m128i rgb2_epi16;
			__m128i rgb_epi8;

			// Shift the RGB components to 8 bits of precision
			r_epi16 = _mm_srli_epi16(r_epi16, 8);
			g_epi16 = _mm_srli_epi16(g_epi16, 8);
			b_epi16 = _mm_srli_epi16(b_epi16, 8);

#if (0 && DEBUG)
			r_epi16 = _mm_set_epi16(0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8);
			g_epi16 = _mm_set_epi16(0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8);
			b_epi16 = _mm_set_epi16(0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8);
#endif
			// Interleave the first four blue and green values
			bg_epi16 = _mm_unpacklo_epi16(b_epi16, g_epi16);

			// Interleave the first four red and alpha values
			ra_epi16 = _mm_unpacklo_epi16(r_epi16, a_epi16);

			// Interleave the first pair of BGRA tuples
			bgra1_epi16 = _mm_unpacklo_epi32(bg_epi16, ra_epi16);

			// Interleave the second pair of BGRA tuples
			bgra2_epi16 = _mm_unpackhi_epi32(bg_epi16, ra_epi16);

			// Pack the BGRA values in 8 bits
			bgra_epi8 = _mm_packus_epi16(bgra1_epi16, bgra2_epi16);

			// Store the first four BGRA tuples
			_mm_store_si128(rgb_ptr++, bgra_epi8);

			// Interleave the second four blue and green values
			bg_epi16 = _mm_unpackhi_epi16(b_epi16, g_epi16);

			// Interleave the second four red and alpha values
			ra_epi16 = _mm_unpackhi_epi16(r_epi16, a_epi16);

			// Interleave the third pair of BGRA tuples
			bgra1_epi16 = _mm_unpacklo_epi32(bg_epi16, ra_epi16);

			// Interleave the fourth pair of BGRA tuples
			bgra2_epi16 = _mm_unpackhi_epi32(bg_epi16, ra_epi16);

			// Pack the BGRA values in 8 bits
			bgra_epi8 = _mm_packus_epi16(bgra1_epi16, bgra2_epi16);

			// Store the second four BGRA tuples
			_mm_store_si128(rgb_ptr++, rgb_epi8);
		}

		// Should have exited the loop at the post processing column
		assert(column == post_column);
#endif

		if (column < frame_width)
		{
			PIXEL16U *r_input_ptr = (PIXEL16U *)r_row_ptr;
			PIXEL16U *g_input_ptr = (PIXEL16U *)g_row_ptr;
			PIXEL16U *b_input_ptr = (PIXEL16U *)b_row_ptr;
			PIXEL16U *a_input_ptr = (PIXEL16U *)a_row_ptr;

			PIXEL16U *rgb_output_ptr = (PIXEL16U *)output_row_ptr;

			// Process the rest of the input and output rows
			for (; column < width; column++)
			{
				int r = r_input_ptr[column];
				int g = g_input_ptr[column];
				int b = b_input_ptr[column];
				int a = a_input_ptr[column];

				// Remove the alpha encoding curve.
				//a -= 16<<8;
				//a <<= 8;
				//a += 111;
				//a /= 223;
				//if (a < 0) a = 0; else if (a > rgb_max) a = rgb_max;

				//12-bit SSE calibrated code
				//a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
				//a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));

				a >>= 4; //12-bit
				a -= alphacompandDCoffset;
				a <<= 3; //15-bit
				a *= alphacompandGain;
				a >>= 16; //12-bit
				a <<= 4;  //16-bit;
				if (a < 0) a = 0; else if (a > rgb_max) a = rgb_max;

#if 0
				if (r < 0) r = 0; else if (r > rgb_max) r = rgb_max;
				if (g < 0) g = 0; else if (g > rgb_max) g = rgb_max;
				if (b < 0) b = 0; else if (b > rgb_max) b = rgb_max;
#endif
				rgb_output_ptr[4 * column + 0] = r;
				rgb_output_ptr[4 * column + 1] = g;
				rgb_output_ptr[4 * column + 2] = b;
				rgb_output_ptr[4 * column + 3] = a;
			}

			// Fill the rest of the frame with black
			for (; column < frame_width; column++)
			{
				rgb_output_ptr[4 * column + 0] = 0;
				rgb_output_ptr[4 * column + 1] = 0;
				rgb_output_ptr[4 * column + 2] = 0;
				rgb_output_ptr[4 * column + 3] = 0;
			}
		}

		// Advance the input row pointers
		r_row_ptr += planar_pitch[1];
		g_row_ptr += planar_pitch[0];
		b_row_ptr += planar_pitch[2];
		a_row_ptr += planar_pitch[3];

		// Advance the output row pointer
		output_row_ptr += output_pitch;
	}
}


void ConvertPlanarRGB16uToPackedRGB30(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									  uint8_t *output_buffer, int output_pitch, int frame_width,
									  int format, int colorspace)
{
	uint8_t *r_row_ptr = (uint8_t *)planar_output[1];
	uint8_t *g_row_ptr = (uint8_t *)planar_output[0];
	uint8_t *b_row_ptr = (uint8_t *)planar_output[2];

	uint8_t *output_row_ptr = output_buffer;

	//const int rgb_max = UINT16_MAX;

	int width = strip.width;
	int height = strip.height;
	int row;

	for (row = 0; row < height; row++)
	{
		int column = 0;

		//TODO: Need to finish the optimized code

#if (1 && XMMOPT)

		int column_step = 8;
		int post_column = width - (width % column_step);

		__m128i *r_ptr = (__m128i *)r_row_ptr;
		__m128i *g_ptr = (__m128i *)g_row_ptr;
		__m128i *b_ptr = (__m128i *)b_row_ptr;
		__m128i *rgb_ptr = (__m128i *)output_row_ptr;

		__m128i zero_epi16 = _mm_set1_epi16(0);

		for (; column < post_column; column += column_step)
		{
			// Load eight of each RGB component
			__m128i r_epi16 = _mm_load_si128(r_ptr++);
			__m128i g_epi16 = _mm_load_si128(g_ptr++);
			__m128i b_epi16 = _mm_load_si128(b_ptr++);
			__m128i rr_epi32;
			__m128i gg_epi32;
			__m128i bb_epi32;
			//__m128i rgb_epi8;

			// Shift the RGB components to 8 bits of precision
			r_epi16 = _mm_srli_epi16(r_epi16, 6);
			g_epi16 = _mm_srli_epi16(g_epi16, 6);
			b_epi16 = _mm_srli_epi16(b_epi16, 6);


			switch(format)
			{
			case DECODED_FORMAT_RG30:
			case DECODED_FORMAT_AB10:
				// Interleave the first four blue and green values
				rr_epi32 = _mm_unpacklo_epi16(r_epi16, zero_epi16);
				gg_epi32 = _mm_unpacklo_epi16(g_epi16, zero_epi16);
				bb_epi32 = _mm_unpacklo_epi16(b_epi16, zero_epi16);

				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				bb_epi32 = _mm_slli_epi32(bb_epi32, 20);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				_mm_store_si128(rgb_ptr++, rr_epi32);


				rr_epi32 = _mm_unpackhi_epi16(r_epi16, zero_epi16);
				gg_epi32 = _mm_unpackhi_epi16(g_epi16, zero_epi16);
				bb_epi32 = _mm_unpackhi_epi16(b_epi16, zero_epi16);

				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				bb_epi32 = _mm_slli_epi32(bb_epi32, 20);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				_mm_store_si128(rgb_ptr++, rr_epi32);
				break;
			case DECODED_FORMAT_AR10:
				// Interleave the first four blue and green values
				rr_epi32 = _mm_unpacklo_epi16(r_epi16, zero_epi16);
				gg_epi32 = _mm_unpacklo_epi16(g_epi16, zero_epi16);
				bb_epi32 = _mm_unpacklo_epi16(b_epi16, zero_epi16);

				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				_mm_store_si128(rgb_ptr++, rr_epi32);


				rr_epi32 = _mm_unpackhi_epi16(r_epi16, zero_epi16);
				gg_epi32 = _mm_unpackhi_epi16(g_epi16, zero_epi16);
				bb_epi32 = _mm_unpackhi_epi16(b_epi16, zero_epi16);

				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);

				rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
				rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

				_mm_store_si128(rgb_ptr++, rr_epi32);
				break;
			case DECODED_FORMAT_R210:
				// Interleave the first four blue and green values
				rr_epi32 = _mm_unpacklo_epi16(r_epi16, zero_epi16);
				gg_epi32 = _mm_unpacklo_epi16(g_epi16, zero_epi16);
				bb_epi32 = _mm_unpacklo_epi16(b_epi16, zero_epi16);

				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				//bb_epi32 = _mm_slli_epi32(bb_epi32, 0);

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

				_mm_store_si128(rgb_ptr++, rr_epi32);


				rr_epi32 = _mm_unpackhi_epi16(r_epi16, zero_epi16);
				gg_epi32 = _mm_unpackhi_epi16(g_epi16, zero_epi16);
				bb_epi32 = _mm_unpackhi_epi16(b_epi16, zero_epi16);

				rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
				//bb_epi32 = _mm_slli_epi32(bb_epi32, 0);

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

				_mm_store_si128(rgb_ptr++, rr_epi32);
				break;
			case DECODED_FORMAT_DPX0:
				// Interleave the first four blue and green values
				rr_epi32 = _mm_unpacklo_epi16(r_epi16, zero_epi16);
				gg_epi32 = _mm_unpacklo_epi16(g_epi16, zero_epi16);
				bb_epi32 = _mm_unpacklo_epi16(b_epi16, zero_epi16);

				rr_epi32 = _mm_slli_epi32(rr_epi32, 22);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 12);
				bb_epi32 = _mm_slli_epi32(bb_epi32, 2);

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

				_mm_store_si128(rgb_ptr++, rr_epi32);


				rr_epi32 = _mm_unpackhi_epi16(r_epi16, zero_epi16);
				gg_epi32 = _mm_unpackhi_epi16(g_epi16, zero_epi16);
				bb_epi32 = _mm_unpackhi_epi16(b_epi16, zero_epi16);

				rr_epi32 = _mm_slli_epi32(rr_epi32, 22);
				gg_epi32 = _mm_slli_epi32(gg_epi32, 12);
				bb_epi32 = _mm_slli_epi32(bb_epi32, 2);

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

				_mm_store_si128(rgb_ptr++, rr_epi32);
				break;
			}
		}

		// Should have exited the loop at the post processing column
		assert(column == post_column);
#endif

		if (column < frame_width)
		{
			PIXEL16U *r_input_ptr = (PIXEL16U *)r_row_ptr;
			PIXEL16U *g_input_ptr = (PIXEL16U *)g_row_ptr;
			PIXEL16U *b_input_ptr = (PIXEL16U *)b_row_ptr;

			uint32_t *rgb_output_ptr = (uint32_t *)output_row_ptr;

			// Process the rest of the input and output rows
			for (; column < width; column++)
			{
				int r = r_input_ptr[column]>>6;
				int g = g_input_ptr[column]>>6;
				int b = b_input_ptr[column]>>6;

				//r = g = b = column & 1023;
				rgb_output_ptr[column] = (b<<20)+(g<<10)+r;
			}

			// Fill the rest of the frame with black
			for (; column < frame_width; column++)
			{
				rgb_output_ptr[column] = 0;
			}
		}

		// Advance the input row pointers
		r_row_ptr += planar_pitch[1];
		g_row_ptr += planar_pitch[0];
		b_row_ptr += planar_pitch[2];

		// Advance the output row pointer
		output_row_ptr += output_pitch;
	}
}

// Adapted from InvertHorizontalStripRGB16sToPackedYUV8u
void ConvertPlanarRGB16uToPackedYU64(PIXEL *input_plane[], int input_pitch[], ROI strip,
	uint8_t *output_image, int output_pitch, int frame_width,
	int color_space)
{
	//int num_channels = CODEC_NUM_CHANNELS;
	int height = strip.height;
	int width = strip.width;

	uint8_t *output_row_ptr = output_image;

	// RGB 4:4:4 is always encoded with 12 bits of precision
	//const int precision = 12;

	// Process 8 luma coefficients per loop iteration
	//const int column_step = 8;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	//int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	//int channel;
	int row;

	// Compute the amount of scaling for the output precision
	//int descale_shift = (precision - 8);
	int shift;

	float fy_rmult, fy_gmult, fy_bmult, fy_offset;
	float fu_rmult, fu_gmult, fu_bmult, fu_offset;
	float fv_rmult, fv_gmult, fv_bmult, fv_offset;

	const float rgb2yuv709[3][4] =
	{
        {0.183f, 0.614f, 0.062f, 16.0f / 255.0f},
        {-0.101f,-0.338f, 0.439f, 128.0f / 255.0f},
        {0.439f,-0.399f,-0.040f, 128.0f / 255.0f}
	};
	const float rgb2yuv601[3][4] =
	{
        {0.257f, 0.504f, 0.098f, 16.0f / 255.0f},
        {-0.148f,-0.291f, 0.439f, 128.0f / 255.0f},
        {0.439f,-0.368f,-0.071f, 128.0f / 255.0f}
	};
	const float rgb2yuvVS601[3][4] =
	{
        {0.299f,0.587f,0.114f,0},
        {-0.172f,-0.339f,0.511f,128.0f / 255.0f},
        {0.511f,-0.428f,-0.083f,128.0f / 255.0f}
	};
	const float rgb2yuvVS709[3][4] =
	{
        {0.213f,0.715f,0.072f,0},
        {-0.117f,-0.394f,0.511f,128.0f / 255.0f},
        {0.511f,-0.464f,-0.047f,128.0f / 255.0f}
	};
	float rgb2yuv[3][4];
	//int yoffset = 16;
	float scale;

	const int luma_offset = (16 << 8);
	const int chroma_offset = (128 << 8);


	switch (color_space & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:
		memcpy(rgb2yuv, rgb2yuv601, 12 * sizeof(float));
		break;
	default: assert(0);
	case COLOR_SPACE_CG_709:
		memcpy(rgb2yuv, rgb2yuv709, 12 * sizeof(float));
		break;
	case COLOR_SPACE_VS_601:
		memcpy(rgb2yuv, rgb2yuvVS601, 12 * sizeof(float));
		break;
	case COLOR_SPACE_VS_709:
		memcpy(rgb2yuv, rgb2yuvVS709, 12 * sizeof(float));
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

	// 601 sRGB
	// Y  = 0.257R + 0.504G + 0.098B + 16;
	// Cb = -0.148R - 0.291G + 0.439B + 128;
	// Cr = 0.439R - 0.368G - 0.071B + 128;

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

	shift = 6;

	fy_rmult = ((rgb2yuv[0][0]) * scale);
	fy_gmult = ((rgb2yuv[0][1]) * scale);
	fy_bmult = ((rgb2yuv[0][2]) * scale);
	fy_offset = ((rgb2yuv[0][3]) * 16384.0f);

	fu_rmult = ((rgb2yuv[1][0]) * scale);
	fu_gmult = ((rgb2yuv[1][1]) * scale);
	fu_bmult = ((rgb2yuv[1][2]) * scale);
	fu_offset = ((rgb2yuv[1][3]) * 16384.0f);

	fv_rmult = ((rgb2yuv[2][0]) * scale);
	fv_gmult = ((rgb2yuv[2][1]) * scale);
	fv_bmult = ((rgb2yuv[2][2]) * scale);
	fv_offset = ((rgb2yuv[2][3]) * 16384.0f);

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
#if (0 && XMMOPT)
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
		__m128i *outptr = (__m128i *)&output_row_ptr[0];

		const __m128i mask_epi32 = _mm_set1_epi32(0xffff);
		const __m128i value128_epi32 = _mm_set1_epi16(128);

		__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x00ff);
		__m128i limiter = _mm_set1_epi16(0x7fff - 0x3fff);

#endif
		// Start processing at the beginning of the row
		int column = 0;

		uint8_t *r_row_ptr = (uint8_t *)input_plane[1];
		uint8_t *g_row_ptr = (uint8_t *)input_plane[0];
		uint8_t *b_row_ptr = (uint8_t *)input_plane[2];

#if (0 && XMMOPT)

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
			__m128i yuv3_epi8;
			__m128i yuv4_epi8;

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

			u1_output_epi16 = _mm_packs_epi32(u1_output_epi16, u2_output_epi16);
			v1_output_epi16 = _mm_packs_epi32(v1_output_epi16, v2_output_epi16);



			/***** Interleave the luma and chroma values *****/

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

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

		//colptr = (uint8_t *)outptr;
#endif

		uint16_t *r_ptr = (uint16_t *)r_row_ptr;
		uint16_t *g_ptr = (uint16_t *)g_row_ptr;
		uint16_t *b_ptr = (uint16_t *)b_row_ptr;

		uint16_t *outptr = (uint16_t *)output_row_ptr;

		// Advance the input points to the next row in each plane
		r_ptr += (input_pitch[1] >> 1) * row;
		g_ptr += (input_pitch[0] >> 1) * row;
		b_ptr += (input_pitch[2] >> 1) * row;

		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column += 2)
		{
			int r1, g1, b1;
			int r2, g2, b2;

			int y1, y2;
			int u1, v1;

			r1 = *(r_ptr++);
			g1 = *(g_ptr++);
			b1 = *(b_ptr++);

			r2 = *(r_ptr++);
			g2 = *(g_ptr++);
			b2 = *(b_ptr++);

			// We use 16-bit fixed-point arithmetic to approximate the color conversion coefficients
			//
			// Floating point arithmetic is

			y1 = (int)(fy_rmult * (float)r1 + fy_gmult * (float)g1 + fy_bmult * (float)b1);
			y2 = (int)(fy_rmult * (float)r2 + fy_gmult * (float)g2 + fy_bmult * (float)b2);

			// Average the chroma values to account for 4:2:2 downsampling (must divide by two later)
			u1 = (int)(fu_rmult * (float)(r1 + r2) + fu_gmult * (float)(g1 + g2) + fu_bmult * (float)(b1 + b2));
			v1 = (int)(fv_rmult * (float)(r1 + r2) + fv_gmult * (float)(g1 + g2) + fv_bmult * (float)(b1 + b2));

			// Shift the output pixels to 16 bits
			y1 >>= shift;
			y2 >>= shift;
			u1 >>= (shift + 1);		// Chroma must be divided by two
			v1 >>= (shift + 1);

			// Apply the chroma offset
			y1 += luma_offset;
			y2 += luma_offset;
			u1 += chroma_offset;
			v1 += chroma_offset;

			// Output the luma and chroma values in the correct order
			*(outptr++) = SATURATE_16U(y1);
			*(outptr++) = SATURATE_16U(v1);
			*(outptr++) = SATURATE_16U(y2);
			*(outptr++) = SATURATE_16U(u1);
		}

		// Advance the output pointer
		output_row_ptr += output_pitch;
	}
}

// Used in RT YUYV playback
void ConvertBAYER2YUVStripPlanarToPacked(uint8_t *planar_output[], int planar_pitch[], ROI strip,
								   uint8_t *output, int output_pitch, int frame_width, int format, int color_space)
{
	int width = strip.width;
	int height = strip.height;
	uint8_t *luma_row_ptr = planar_output[0];
	uint8_t *u_row_ptr = planar_output[1];
	uint8_t *v_row_ptr = planar_output[2];
	uint8_t *output_row_ptr = output;
	int row;

	int shift = 8;

	int y_rmult;
	int y_gmult;
	int y_bmult;
	int y_offset;

	int u_rmult;
	int u_gmult;
	int u_bmult;
	int u_offset;

	int v_rmult;
	int v_gmult;
	int v_bmult;
	int v_offset;

	int r_rmult;
	int r_gmult;
	int r_bmult;
	//int r_offset;
	int g_rmult;
	int g_gmult;
	int g_bmult;
	//int g_offset;
	int b_rmult;
	int b_gmult;
	int b_bmult;
	//int b_offset;

#if 0
	float rgb2yuv[4][4] =
	{
		 0.183, 0.614, 0.062, 16.0/256.0,
		-0.101,-0.338, 0.439, 0.5,
		 0.439,-0.399,-0.040, 0.5,
		 0.000, 0.000, 0.000, 0.000,
	};
#endif

	static float mtrx[3][4] =
	{
        {1.0,  0,   0,   0},
        {0,  1.0,   0,   0},
        {0,    0, 1.0,   0}
	};

	//3560
	/*	float mtrx[4][4] =
	{
		 1.60,   -0.38,   -0.22,    0,
		-0.45,    2.31,   -0.86,    0,
		-0.35,   -0.24,    1.59,    0,
		 0,          0,       0,  1.0,
	};*/

/*	//3570
	float mtrx[4][4] =
	{
		 1.095,   0.405,    -0.500,    0,
		-0.491,   2.087,    -0.596,    0,
		-0.179,  -0.994,     2.173,    0,
		 0,       0,         0,      1.0,
	};
*/
	int matrix_non_unity = 0;

	float scale = 256.0;
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






// WIP need to convert to decoder->cfhddata






/*	if(decoder->cfhddata.MagicNumber == CFHDDATA_MAGIC_NUMBER && decoder->cfhddata.version >= 2)
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
	}
*/

#if 1
	switch(color_space & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:

		// sRGB + 601
		// Floating point arithmetic is
		//
		// Y  = 0.257R + 0.504G + 0.098B + 16.5;
		// Cb =-0.148R - 0.291G + 0.439B + 128.5;
		// Cr = 0.439R - 0.368G - 0.071B + 128.5;
		//
		// Fixed point approximation (8-bit) is
		//
		// Y  = ( 66R + 129G +  25B +  4224) >> 8;
		// Cb = (-38R -  74G + 112B + 32896) >> 8;
		// Cr = (112R -  94G -  18B + 32896) >> 8;

		// Upgraded conversion DAN20041112

		y_rmult = 66;
		y_gmult = 129;
		y_bmult = 25;
		y_offset = 4224-8;

		u_rmult = 38;
		u_gmult = 74;
		u_bmult = 112;
		u_offset = 32896;

		v_rmult = 112;
		v_gmult = 94;
		v_bmult = 18;
		v_offset = 32896;

		//shift = 8;

		break;

	case COLOR_SPACE_VS_709:

		// video systems RGB + 709
		// Floating point arithmetic is
		// Y = 0.213R + 0.715G + 0.072B
		// Cb = -0.117R - 0.394G + 0.511B + 128
		// Cr = 0.511R - 0.464G - 0.047B + 128
		//
		// Fixed point approximation (8-bit) is
		//
		// Y  = ( 55R + 183G +  18B +  128) >> 8;
		// Cb = (-30R - 101G + 131B + 32896) >> 8;
		// Cr = (131R - 119G -  12B + 32896) >> 8;

		y_rmult = 55;
		y_gmult = 183;
		y_bmult = 18;
		y_offset = 128;

		u_rmult = 30;
		u_gmult = 101;
		u_bmult = 131;
		u_offset = 32896-9;

		v_rmult = 131;
		v_gmult = 119;
		v_bmult = 12;
		v_offset = 32896;

		//shift = 8;

		break;

	case COLOR_SPACE_VS_601:

		// video systems RGB + 601
		// Floating point arithmetic is
		// Y = 0.299R + 0.587G + 0.114B
		// Cb = -0.172R - 0.339G + 0.511B + 128
		// Cr = 0.511R - 0.428G - 0.083B + 128
		//
		// Fixed point approximation (8-bit) is
		//
		// Y  = ( 77R + 150G +  29B + 128) >> 8;
		// Cb = (-44R -  87G + 131B + 32896) >> 8;
		// Cr = (131R - 110G -  21B + 32896) >> 8;

		y_rmult = 77;
		y_gmult = 150;
		y_bmult = 29;
		y_offset = 128;

		u_rmult = 44;
		u_gmult = 87;
		u_bmult = 131;
		u_offset = 32896-2;

		v_rmult = 131;
		v_gmult = 110;
		v_bmult = 21;
		v_offset = 32896-1;

		//shift = 8;

		break;

	default: assert(0);
	case COLOR_SPACE_CG_709:

		// sRGB + 709
		// Y = 0.183R + 0.614G + 0.062B + 16
		// Cb = -0.101R - 0.338G + 0.439B + 128
		// Cr = 0.439R - 0.399G - 0.040B + 128
		//
		// Fixed point approximation (8-bit) is
		//
		// Y  = ( 47R + 157G +  16B +  4224) >> 8;
		// Cb = (-26R -  87G + 112B + 32896) >> 8;
		// Cr = (112R - 102G -  10B + 32896) >> 8;

		y_rmult = 47;
		y_gmult = 157;
		y_bmult = 16;
		y_offset = 4224-2;

		u_rmult = 26;
		u_gmult = 87-1;
		u_bmult = 112;
		u_offset = 32896-2;

		v_rmult = 112;
		v_gmult = 102;
		v_bmult = 10;
		v_offset = 32896-2;

		//shift = 8;

		break;
	}

	r_rmult= (int)(mtrx[0][0] * scale);
	r_gmult= (int)(mtrx[0][1] * scale);
	r_bmult= (int)(mtrx[0][2] * scale);

	g_rmult= (int)(mtrx[1][0] * scale);
	g_gmult= (int)(mtrx[1][1] * scale);
	g_bmult= (int)(mtrx[1][2] * scale);

	b_rmult= (int)(mtrx[2][0] * scale);
	b_gmult= (int)(mtrx[2][1] * scale);
	b_bmult= (int)(mtrx[2][2] * scale);


#else

	y_rmult = (rgb2yuv[0][0]*mtrx[0][0] + rgb2yuv[1][0]*mtrx[0][1] + rgb2yuv[2][0]*mtrx[0][2]) * scale;
	y_gmult = (rgb2yuv[0][1]*mtrx[0][0] + rgb2yuv[1][1]*mtrx[0][1] + rgb2yuv[2][1]*mtrx[0][2]) * scale;
	y_bmult = (rgb2yuv[0][2]*mtrx[0][0] + rgb2yuv[1][2]*mtrx[0][1] + rgb2yuv[2][2]*mtrx[0][2]) * scale;
	y_offset= (rgb2yuv[0][3]*mtrx[0][0] + rgb2yuv[1][3]*mtrx[0][1] + rgb2yuv[2][3]*mtrx[0][2]) * 65536.0;

	u_rmult = (rgb2yuv[0][0]*mtrx[1][0] + rgb2yuv[1][0]*mtrx[1][1] + rgb2yuv[2][0]*mtrx[1][2]) * scale;
	u_gmult = (rgb2yuv[0][1]*mtrx[1][0] + rgb2yuv[1][1]*mtrx[1][1] + rgb2yuv[2][1]*mtrx[1][2]) * scale;
	u_bmult = (rgb2yuv[0][2]*mtrx[1][0] + rgb2yuv[1][2]*mtrx[1][1] + rgb2yuv[2][2]*mtrx[1][2]) * scale;
	u_offset= (rgb2yuv[0][3]*mtrx[1][0] + rgb2yuv[1][3]*mtrx[1][1] + rgb2yuv[2][3]*mtrx[1][2]) * 65536.0;

	v_rmult = (rgb2yuv[0][0]*mtrx[2][0] + rgb2yuv[1][0]*mtrx[2][1] + rgb2yuv[2][0]*mtrx[2][2]) * scale;
	v_gmult = (rgb2yuv[0][1]*mtrx[2][0] + rgb2yuv[1][1]*mtrx[2][1] + rgb2yuv[2][1]*mtrx[2][2]) * scale;
	v_bmult = (rgb2yuv[0][2]*mtrx[2][0] + rgb2yuv[1][2]*mtrx[2][1] + rgb2yuv[2][2]*mtrx[2][2]) * scale;
	v_offset= (rgb2yuv[0][3]*mtrx[2][0] + rgb2yuv[1][3]*mtrx[2][1] + rgb2yuv[2][3]*mtrx[2][2]) * 65536.0;

	//TODO:  fix this -- signs should not have to be removed
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
#endif

	// Must have an even number of output pixels
	assert((width % 2) == 0);

	if(format == DECODED_FORMAT_UYVY)
	{
		for (row = 0; row < height; row++)
		{
			int column = 0;

#if (0 && XMMOPT) //TODO

			int column_step = 16;
			int post_column = width - (width % column_step);

			__m64 *y_ptr = (__m64 *)luma_row_ptr;
			__m64 *u_ptr = (__m64 *)u_row_ptr;
			__m64 *v_ptr = (__m64 *)v_row_ptr;
			__m64 *yuv_ptr = (__m64 *)output_row_ptr;

			for (; column < post_column; column += column_step)
			{
				__m64 y1_pi8 = *(y_ptr++);
				__m64 y2_pi8 = *(y_ptr++);
				__m64 u_pi8 = *(u_ptr++);
				__m64 v_pi8 = *(v_ptr++);
				__m64 uv_pi8;
				__m64 yuv_pi8;

				// Interleave the first four u and v chroma values
				uv_pi8 = _mm_unpacklo_pi8(v_pi8, u_pi8);

				// Interleave and store the first group of four luma and chroma pairs
				yuv_pi8 = _mm_unpacklo_pi8(uv_pi8, y1_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave and store the second group of four luma and chroma pairs
				yuv_pi8 = _mm_unpackhi_pi8(uv_pi8, y1_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave the second four u and v chroma values
				uv_pi8 = _mm_unpackhi_pi8(v_pi8, u_pi8);

				// Interleave and store the third group of four luma and chroma pairs
				yuv_pi8 = _mm_unpacklo_pi8(uv_pi8, y2_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave and store the fourth group of four luma and chroma pairs
				yuv_pi8 = _mm_unpackhi_pi8(uv_pi8, y2_pi8);
				*(yuv_ptr++) = yuv_pi8;
			}

			//_mm_empty();		// Clear the mmx register state

			// Should have exited the loop at the post processing column
			assert(column == post_column);
#endif

			// Process the rest of the row
			for (; column < width; column += 2)
			{
				int y1,u,y2,v,R,G,B;
				int gg = luma_row_ptr[column];
				int rg = u_row_ptr[column];
				int bg = v_row_ptr[column];


				R = ((rg - 128)<<1) + gg;
				G = gg;
				B = ((bg - 128)<<1) + gg;

				if(R<0) R=0;
				if(B<0) B=0;
				if(R>255) R=255;
				if(B>255) B=255;

				if(matrix_non_unity)
				{
					int r1,g1,b1;
					r1= (( r_rmult * R + r_gmult * G + r_bmult * B)>>8);
					g1= (( g_rmult * R + g_gmult * G + g_bmult * B)>>8);
					b1= (( b_rmult * R + b_gmult * G + b_bmult * B)>>8);

					R = r1;
					G = g1;
					B = b1;

					if(R < 0) R = 0;
					if(R > 255) R = 255;
					if(G < 0) G = 0;
					if(G > 255) G = 255;
					if(B < 0) B = 0;
					if(B > 255) B = 255;
				}


				y1 =( y_rmult * R + y_gmult * G + y_bmult * B + y_offset) >> shift;
				u = (-u_rmult * R - u_gmult * G + u_bmult * B + u_offset) >> (shift+1);
				v = ( v_rmult * R - v_gmult * G - v_bmult * B + v_offset) >> (shift+1);


				gg = luma_row_ptr[column+1];
				rg = u_row_ptr[column+1];
				bg = v_row_ptr[column+1];


				R = ((rg - 128)<<1) + gg;
				G = gg;
				B = ((bg - 128)<<1) + gg;

				if(R<0) R=0;
				if(B<0) B=0;
				if(R>255) R=255;
				if(B>255) B=255;

				if(matrix_non_unity)
				{
					int r1,g1,b1;
					r1= (( r_rmult * R + r_gmult * G + r_bmult * B)>>8);
					g1= (( g_rmult * R + g_gmult * G + g_bmult * B)>>8);
					b1= (( b_rmult * R + b_gmult * G + b_bmult * B)>>8);

					R = r1;
					G = g1;
					B = b1;

					if(R < 0) R = 0;
					if(R > 255) R = 255;
					if(G < 0) G = 0;
					if(G > 255) G = 255;
					if(B < 0) B = 0;
					if(B > 255) B = 255;
				}


				y2 = ( y_rmult * R + y_gmult * G + y_bmult * B + y_offset) >> shift;
				u += (-u_rmult * R - u_gmult * G + u_bmult * B + u_offset) >> (shift+1);
				v += ( v_rmult * R - v_gmult * G - v_bmult * B + v_offset) >> (shift+1);

				output_row_ptr[2 * column + 0] = SATURATE_Cr(v);
				output_row_ptr[2 * column + 1] = SATURATE_Y(y1);
				output_row_ptr[2 * column + 2] = SATURATE_Cb(u);
				output_row_ptr[2 * column + 3] = SATURATE_Y(y2);
			}

			// Fill the rest of the frame with black
			for (; column < frame_width; column += 2)
			{
				output_row_ptr[2 * column + 0] = COLOR_CHROMA_ZERO;
				output_row_ptr[2 * column + 1] = COLOR_LUMA_BLACK;
			}

			// Advance the input and output row pointers
			luma_row_ptr += planar_pitch[0];
			u_row_ptr += planar_pitch[1];
			v_row_ptr += planar_pitch[2];
			output_row_ptr += output_pitch;
		}
	}
	else if (format == DECODED_FORMAT_YUYV)
	{
		for (row = 0; row < height; row++)
		{
			int column = 0;

#if (0 && XMMOPT) //TODO

			int column_step = 16;
			int post_column = width - (width % column_step);

			__m64 *y_ptr = (__m64 *)luma_row_ptr;
			__m64 *u_ptr = (__m64 *)u_row_ptr;
			__m64 *v_ptr = (__m64 *)v_row_ptr;
			__m64 *yuv_ptr = (__m64 *)output_row_ptr;

			for (; column < post_column; column += column_step)
			{
				__m64 y1_pi8 = *(y_ptr++);
				__m64 y2_pi8 = *(y_ptr++);
				__m64 u_pi8 = *(u_ptr++);
				__m64 v_pi8 = *(v_ptr++);
				__m64 uv_pi8;
				__m64 yuv_pi8;

				// Interleave the first four u and v chroma values
				uv_pi8 = _mm_unpacklo_pi8(v_pi8, u_pi8);

				// Interleave and store the first group of four luma and chroma pairs
				yuv_pi8 = _mm_unpacklo_pi8(y1_pi8, uv_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave and store the second group of four luma and chroma pairs
				yuv_pi8 = _mm_unpackhi_pi8(y1_pi8, uv_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave the second four u and v chroma values
				uv_pi8 = _mm_unpackhi_pi8(v_pi8, u_pi8);

				// Interleave and store the third group of four luma and chroma pairs
				yuv_pi8 = _mm_unpacklo_pi8(y2_pi8, uv_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave and store the fourth group of four luma and chroma pairs
				yuv_pi8 = _mm_unpackhi_pi8(y2_pi8, uv_pi8);
				*(yuv_ptr++) = yuv_pi8;
			}

			//_mm_empty();		// Clear the mmx register state

			// Should have exited the loop at the post processing column
			assert(column == post_column);
#endif

			// Process the rest of the row
			for (; column < width; column+=2)
			{
				int y1,u,y2,v,R,G,B;
				int gg = luma_row_ptr[column];
				int rg = u_row_ptr[column];
				int bg = v_row_ptr[column];


				R = ((rg - 128)<<1) + gg;
				G = gg;
				B = ((bg - 128)<<1) + gg;

				if(R<0) R=0;
				if(B<0) B=0;
				if(R>255) R=255;
				if(B>255) B=255;

				if(matrix_non_unity)
				{
					int r1,g1,b1;
					r1= (( r_rmult * R + r_gmult * G + r_bmult * B)>>8);
					g1= (( g_rmult * R + g_gmult * G + g_bmult * B)>>8);
					b1= (( b_rmult * R + b_gmult * G + b_bmult * B)>>8);

					R = r1;
					G = g1;
					B = b1;

					if(R < 0) R = 0;
					if(R > 255) R = 255;
					if(G < 0) G = 0;
					if(G > 255) G = 255;
					if(B < 0) B = 0;
					if(B > 255) B = 255;
				}

				y1 =( y_rmult * R + y_gmult * G + y_bmult * B + y_offset) >> shift;
				u = (-u_rmult * R - u_gmult * G + u_bmult * B + u_offset) >> (shift+1);
				v = ( v_rmult * R - v_gmult * G - v_bmult * B + v_offset) >> (shift+1);


				gg = luma_row_ptr[column+1];
				rg = u_row_ptr[column+1];
				bg = v_row_ptr[column+1];


				R = ((rg - 128)<<1) + gg;
				G = gg;
				B = ((bg - 128)<<1) + gg;

				if(R<0) R=0;
				if(B<0) B=0;
				if(R>255) R=255;
				if(B>255) B=255;

				if(matrix_non_unity)
				{
					int r1,g1,b1;
					r1= (( r_rmult * R + r_gmult * G + r_bmult * B)>>8);
					g1= (( g_rmult * R + g_gmult * G + g_bmult * B)>>8);
					b1= (( b_rmult * R + b_gmult * G + b_bmult * B)>>8);

					R = r1;
					G = g1;
					B = b1;

					if(R < 0) R = 0;
					if(R > 255) R = 255;
					if(G < 0) G = 0;
					if(G > 255) G = 255;
					if(B < 0) B = 0;
					if(B > 255) B = 255;
				}

				y2 = ( y_rmult * R + y_gmult * G + y_bmult * B + y_offset) >> shift;
				u += (-u_rmult * R - u_gmult * G + u_bmult * B + u_offset) >> (shift+1);
				v += ( v_rmult * R - v_gmult * G - v_bmult * B + v_offset) >> (shift+1);



				output_row_ptr[2 * column + 0] = SATURATE_Y(y1);
				output_row_ptr[2 * column + 1] = SATURATE_Cr(u);
				output_row_ptr[2 * column + 2] = SATURATE_Y(y2);
				output_row_ptr[2 * column + 3] = SATURATE_Cb(v);
			}

			// Fill the rest of the frame with black
			for (; column < frame_width; column += 2)
			{
				output_row_ptr[2 * column + 0] = COLOR_LUMA_BLACK;
				output_row_ptr[2 * column + 1] = COLOR_CHROMA_ZERO;
			}

			// Advance the input and output row pointers
			luma_row_ptr += planar_pitch[0];
			u_row_ptr += planar_pitch[1];
			v_row_ptr += planar_pitch[2];
			output_row_ptr += output_pitch;
		}
	}
}

/*
void ConvertRGB2RG30StripPlanarToPacked(uint8_t *planar_output[], int planar_pitch[], ROI strip,
									   uint8_t *output, int output_pitch, int frame_width)
{
	PIXEL16U *plane_array[TRANSFORM_MAX_CHANNELS];
	int plane_pitch[TRANSFORM_MAX_CHANNELS];
	int i;
	for(i=0; i<TRANSFORM_MAX_CHANNELS; i++)
	{
		plane_array[i] = (PIXEL16U *)planar_output[i];
		plane_pitch[i] = planar_pitch[i];
	}

	ConvertPlanarRGB16uToPackedRGB30((PIXEL **)plane_array, plane_pitch, strip,
									output, output_pitch, frame_width, format, colorspace);
}*/

// Used in RGB to YUYV playback
void ConvertRGB2YUVStripPlanarToPacked(uint8_t *planar_output[], int planar_pitch[], ROI strip,
									   uint8_t *output, int output_pitch, int frame_width,
									   int format, int color_space)
{
	int width = strip.width;
	int height = strip.height;
	uint8_t *luma_row_ptr = planar_output[0];
	uint8_t *u_row_ptr = planar_output[1];
	uint8_t *v_row_ptr = planar_output[2];
	uint8_t *output_row_ptr = output;
	int row;

	int shift = 8;

	int y_rmult;
	int y_gmult;
	int y_bmult;
	int y_offset;

	int u_rmult;
	int u_gmult;
	int u_bmult;
	int u_offset;

	int v_rmult;
	int v_gmult;
	int v_bmult;
	int v_offset;

	int r_rmult;
	int r_gmult;
	int r_bmult;
	//int r_offset;
	int g_rmult;
	int g_gmult;
	int g_bmult;
	//int g_offset;
	int b_rmult;
	int b_gmult;
	int b_bmult;
	//int b_offset;

#if 0
	float rgb2yuv[4][4] =
	{
		 0.183, 0.614, 0.062, 16.0/256.0,
		-0.101,-0.338, 0.439, 0.5,
		 0.439,-0.399,-0.040, 0.5,
		 0.000, 0.000, 0.000, 0.000,
	};
#endif

	static float mtrx[3][4] =
	{
        {1.0,  0,   0,   0},
        {0,  1.0,   0,   0},
        {0,    0, 1.0,   0}
	};

	//3560
	/*	float mtrx[4][4] =
	{
		 1.60,   -0.38,   -0.22,    0,
		-0.45,    2.31,   -0.86,    0,
		-0.35,   -0.24,    1.59,    0,
		 0,          0,       0,  1.0,
	};*/

/*	//3570
	float mtrx[4][4] =
	{
		 1.095,   0.405,    -0.500,    0,
		-0.491,   2.087,    -0.596,    0,
		-0.179,  -0.994,     2.173,    0,
		 0,       0,         0,      1.0,
	};
*/
	int matrix_non_unity = 0;

	float scale = 256.0;
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






// WIP need to convert to decoder->cfhddata






/*	if(decoder->cfhddata.MagicNumber == CFHDDATA_MAGIC_NUMBER && decoder->cfhddata.version >= 2)
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
	}
*/

#if 1
	switch(color_space & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:

		// sRGB + 601
		// Floating point arithmetic is
		//
		// Y  = 0.257R + 0.504G + 0.098B + 16.5;
		// Cb =-0.148R - 0.291G + 0.439B + 128.5;
		// Cr = 0.439R - 0.368G - 0.071B + 128.5;
		//
		// Fixed point approximation (8-bit) is
		//
		// Y  = ( 66R + 129G +  25B +  4224) >> 8;
		// Cb = (-38R -  74G + 112B + 32896) >> 8;
		// Cr = (112R -  94G -  18B + 32896) >> 8;

		// Upgraded conversion DAN20041112

		y_rmult = 66;
		y_gmult = 129;
		y_bmult = 25;
		y_offset = 4224-8;

		u_rmult = 38;
		u_gmult = 74;
		u_bmult = 112;
		u_offset = 32896;

		v_rmult = 112;
		v_gmult = 94;
		v_bmult = 18;
		v_offset = 32896;

		//shift = 8;

		break;

	case COLOR_SPACE_VS_709:

		// video systems RGB + 709
		// Floating point arithmetic is
		// Y = 0.213R + 0.715G + 0.072B
		// Cb = -0.117R - 0.394G + 0.511B + 128
		// Cr = 0.511R - 0.464G - 0.047B + 128
		//
		// Fixed point approximation (8-bit) is
		//
		// Y  = ( 55R + 183G +  18B +  128) >> 8;
		// Cb = (-30R - 101G + 131B + 32896) >> 8;
		// Cr = (131R - 119G -  12B + 32896) >> 8;

		y_rmult = 55;
		y_gmult = 183;
		y_bmult = 18;
		y_offset = 128;

		u_rmult = 30;
		u_gmult = 101;
		u_bmult = 131;
		u_offset = 32896-9;

		v_rmult = 131;
		v_gmult = 119;
		v_bmult = 12;
		v_offset = 32896;

		//shift = 8;

		break;

	case COLOR_SPACE_VS_601:

		// video systems RGB + 601
		// Floating point arithmetic is
		// Y = 0.299R + 0.587G + 0.114B
		// Cb = -0.172R - 0.339G + 0.511B + 128
		// Cr = 0.511R - 0.428G - 0.083B + 128
		//
		// Fixed point approximation (8-bit) is
		//
		// Y  = ( 77R + 150G +  29B + 128) >> 8;
		// Cb = (-44R -  87G + 131B + 32896) >> 8;
		// Cr = (131R - 110G -  21B + 32896) >> 8;

		y_rmult = 77;
		y_gmult = 150;
		y_bmult = 29;
		y_offset = 128;

		u_rmult = 44;
		u_gmult = 87;
		u_bmult = 131;
		u_offset = 32896-2;

		v_rmult = 131;
		v_gmult = 110;
		v_bmult = 21;
		v_offset = 32896-1;

		//shift = 8;

		break;

	default: assert(0);
	case COLOR_SPACE_CG_709:

		// sRGB + 709
		// Y = 0.183R + 0.614G + 0.062B + 16
		// Cb = -0.101R - 0.338G + 0.439B + 128
		// Cr = 0.439R - 0.399G - 0.040B + 128
		//
		// Fixed point approximation (8-bit) is
		//
		// Y  = ( 47R + 157G +  16B +  4224) >> 8;
		// Cb = (-26R -  87G + 112B + 32896) >> 8;
		// Cr = (112R - 102G -  10B + 32896) >> 8;

		y_rmult = 47;
		y_gmult = 157;
		y_bmult = 16;
		y_offset = 4224-2;

		u_rmult = 26;
		u_gmult = 87-1;
		u_bmult = 112;
		u_offset = 32896-2;

		v_rmult = 112;
		v_gmult = 102;
		v_bmult = 10;
		v_offset = 32896-2;

		//shift = 8;
		break;
	}

	r_rmult= (int)(mtrx[0][0] * scale);
	r_gmult= (int)(mtrx[0][1] * scale);
	r_bmult= (int)(mtrx[0][2] * scale);
	g_rmult= (int)(mtrx[1][0] * scale);
	g_gmult= (int)(mtrx[1][1] * scale);
	g_bmult= (int)(mtrx[1][2] * scale);
	b_rmult= (int)(mtrx[2][0] * scale);
	b_gmult= (int)(mtrx[2][1] * scale);
	b_bmult= (int)(mtrx[2][2] * scale);


#else

	y_rmult = (rgb2yuv[0][0]*mtrx[0][0] + rgb2yuv[1][0]*mtrx[0][1] + rgb2yuv[2][0]*mtrx[0][2]) * scale;
	y_gmult = (rgb2yuv[0][1]*mtrx[0][0] + rgb2yuv[1][1]*mtrx[0][1] + rgb2yuv[2][1]*mtrx[0][2]) * scale;
	y_bmult = (rgb2yuv[0][2]*mtrx[0][0] + rgb2yuv[1][2]*mtrx[0][1] + rgb2yuv[2][2]*mtrx[0][2]) * scale;
	y_offset= (rgb2yuv[0][3]*mtrx[0][0] + rgb2yuv[1][3]*mtrx[0][1] + rgb2yuv[2][3]*mtrx[0][2]) * 65536.0;

	u_rmult = (rgb2yuv[0][0]*mtrx[1][0] + rgb2yuv[1][0]*mtrx[1][1] + rgb2yuv[2][0]*mtrx[1][2]) * scale;
	u_gmult = (rgb2yuv[0][1]*mtrx[1][0] + rgb2yuv[1][1]*mtrx[1][1] + rgb2yuv[2][1]*mtrx[1][2]) * scale;
	u_bmult = (rgb2yuv[0][2]*mtrx[1][0] + rgb2yuv[1][2]*mtrx[1][1] + rgb2yuv[2][2]*mtrx[1][2]) * scale;
	u_offset= (rgb2yuv[0][3]*mtrx[1][0] + rgb2yuv[1][3]*mtrx[1][1] + rgb2yuv[2][3]*mtrx[1][2]) * 65536.0;

	v_rmult = (rgb2yuv[0][0]*mtrx[2][0] + rgb2yuv[1][0]*mtrx[2][1] + rgb2yuv[2][0]*mtrx[2][2]) * scale;
	v_gmult = (rgb2yuv[0][1]*mtrx[2][0] + rgb2yuv[1][1]*mtrx[2][1] + rgb2yuv[2][1]*mtrx[2][2]) * scale;
	v_bmult = (rgb2yuv[0][2]*mtrx[2][0] + rgb2yuv[1][2]*mtrx[2][1] + rgb2yuv[2][2]*mtrx[2][2]) * scale;
	v_offset= (rgb2yuv[0][3]*mtrx[2][0] + rgb2yuv[1][3]*mtrx[2][1] + rgb2yuv[2][3]*mtrx[2][2]) * 65536.0;

	//TODO:  fix this -- signs should not have to be removed
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
#endif

	// Must have an even number of output pixels
	assert((width % 2) == 0);

	if(format == DECODED_FORMAT_UYVY)
	{
		for (row = 0; row < height; row++)
		{
			int column = 0;

#if (0 && XMMOPT) //TODO

			int column_step = 16;
			int post_column = width - (width % column_step);

			__m64 *y_ptr = (__m64 *)luma_row_ptr;
			__m64 *u_ptr = (__m64 *)u_row_ptr;
			__m64 *v_ptr = (__m64 *)v_row_ptr;
			__m64 *yuv_ptr = (__m64 *)output_row_ptr;

			for (; column < post_column; column += column_step)
			{
				__m64 y1_pi8 = *(y_ptr++);
				__m64 y2_pi8 = *(y_ptr++);
				__m64 u_pi8 = *(u_ptr++);
				__m64 v_pi8 = *(v_ptr++);
				__m64 uv_pi8;
				__m64 yuv_pi8;

				// Interleave the first four u and v chroma values
				uv_pi8 = _mm_unpacklo_pi8(v_pi8, u_pi8);

				// Interleave and store the first group of four luma and chroma pairs
				yuv_pi8 = _mm_unpacklo_pi8(uv_pi8, y1_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave and store the second group of four luma and chroma pairs
				yuv_pi8 = _mm_unpackhi_pi8(uv_pi8, y1_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave the second four u and v chroma values
				uv_pi8 = _mm_unpackhi_pi8(v_pi8, u_pi8);

				// Interleave and store the third group of four luma and chroma pairs
				yuv_pi8 = _mm_unpacklo_pi8(uv_pi8, y2_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave and store the fourth group of four luma and chroma pairs
				yuv_pi8 = _mm_unpackhi_pi8(uv_pi8, y2_pi8);
				*(yuv_ptr++) = yuv_pi8;
			}

			//_mm_empty();		// Clear the mmx register state

			// Should have exited the loop at the post processing column
			assert(column == post_column);
#endif

			// Process the rest of the row
			for (; column < width; column += 2)
			{
				int y1,u,y2,v,R,G,B;
				int gg = luma_row_ptr[column];
				int rg = u_row_ptr[column];
				int bg = v_row_ptr[column];


				R = rg;//((rg - 128)<<1) + gg;
				G = gg;
				B = bg;//((bg - 128)<<1) + gg;

				if(R<0) R=0;
				if(B<0) B=0;
				if(R>255) R=255;
				if(B>255) B=255;

				if(matrix_non_unity)
				{
					int r1,g1,b1;
					r1= (( r_rmult * R + r_gmult * G + r_bmult * B)>>8);
					g1= (( g_rmult * R + g_gmult * G + g_bmult * B)>>8);
					b1= (( b_rmult * R + b_gmult * G + b_bmult * B)>>8);

					R = r1;
					G = g1;
					B = b1;

					if(R < 0) R = 0;
					if(R > 255) R = 255;
					if(G < 0) G = 0;
					if(G > 255) G = 255;
					if(B < 0) B = 0;
					if(B > 255) B = 255;
				}


				y1 =( y_rmult * R + y_gmult * G + y_bmult * B + y_offset) >> shift;
				u = (-u_rmult * R - u_gmult * G + u_bmult * B + u_offset) >> (shift+1);
				v = ( v_rmult * R - v_gmult * G - v_bmult * B + v_offset) >> (shift+1);


				gg = luma_row_ptr[column+1];
				rg = u_row_ptr[column+1];
				bg = v_row_ptr[column+1];


				R = rg;//((rg - 128)<<1) + gg;
				G = gg;
				B = bg;//((bg - 128)<<1) + gg;

				if(R<0) R=0;
				if(B<0) B=0;
				if(R>255) R=255;
				if(B>255) B=255;

				if(matrix_non_unity)
				{
					int r1,g1,b1;
					r1= (( r_rmult * R + r_gmult * G + r_bmult * B)>>8);
					g1= (( g_rmult * R + g_gmult * G + g_bmult * B)>>8);
					b1= (( b_rmult * R + b_gmult * G + b_bmult * B)>>8);

					R = r1;
					G = g1;
					B = b1;

					if(R < 0) R = 0;
					if(R > 255) R = 255;
					if(G < 0) G = 0;
					if(G > 255) G = 255;
					if(B < 0) B = 0;
					if(B > 255) B = 255;
				}


				y2 = ( y_rmult * R + y_gmult * G + y_bmult * B + y_offset) >> shift;
				u += (-u_rmult * R - u_gmult * G + u_bmult * B + u_offset) >> (shift+1);
				v += ( v_rmult * R - v_gmult * G - v_bmult * B + v_offset) >> (shift+1);

				output_row_ptr[2 * column + 0] = SATURATE_Cr(v);
				output_row_ptr[2 * column + 1] = SATURATE_Y(y1);
				output_row_ptr[2 * column + 2] = SATURATE_Cb(u);
				output_row_ptr[2 * column + 3] = SATURATE_Y(y2);
			}

			// Fill the rest of the frame with black
			for (; column < frame_width; column += 2)
			{
				output_row_ptr[2 * column + 0] = COLOR_CHROMA_ZERO;
				output_row_ptr[2 * column + 1] = COLOR_LUMA_BLACK;
			}

			// Advance the input and output row pointers
			luma_row_ptr += planar_pitch[0];
			u_row_ptr += planar_pitch[1];
			v_row_ptr += planar_pitch[2];
			output_row_ptr += output_pitch;
		}
	}
	else if (format == DECODED_FORMAT_YUYV)
	{
		for (row = 0; row < height; row++)
		{
			int column = 0;

#if (0 && XMMOPT) //TODO

			int column_step = 16;
			int post_column = width - (width % column_step);

			__m64 *y_ptr = (__m64 *)luma_row_ptr;
			__m64 *u_ptr = (__m64 *)u_row_ptr;
			__m64 *v_ptr = (__m64 *)v_row_ptr;
			__m64 *yuv_ptr = (__m64 *)output_row_ptr;

			for (; column < post_column; column += column_step)
			{
				__m64 y1_pi8 = *(y_ptr++);
				__m64 y2_pi8 = *(y_ptr++);
				__m64 u_pi8 = *(u_ptr++);
				__m64 v_pi8 = *(v_ptr++);
				__m64 uv_pi8;
				__m64 yuv_pi8;

				// Interleave the first four u and v chroma values
				uv_pi8 = _mm_unpacklo_pi8(v_pi8, u_pi8);

				// Interleave and store the first group of four luma and chroma pairs
				yuv_pi8 = _mm_unpacklo_pi8(y1_pi8, uv_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave and store the second group of four luma and chroma pairs
				yuv_pi8 = _mm_unpackhi_pi8(y1_pi8, uv_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave the second four u and v chroma values
				uv_pi8 = _mm_unpackhi_pi8(v_pi8, u_pi8);

				// Interleave and store the third group of four luma and chroma pairs
				yuv_pi8 = _mm_unpacklo_pi8(y2_pi8, uv_pi8);
				*(yuv_ptr++) = yuv_pi8;

				// Interleave and store the fourth group of four luma and chroma pairs
				yuv_pi8 = _mm_unpackhi_pi8(y2_pi8, uv_pi8);
				*(yuv_ptr++) = yuv_pi8;
			}

			//_mm_empty();		// Clear the mmx register state

			// Should have exited the loop at the post processing column
			assert(column == post_column);
#endif

			// Process the rest of the row
			for (; column < width; column+=2)
			{
				int y1,u,y2,v,R,G,B;
				int gg = luma_row_ptr[column];
				int rg = u_row_ptr[column];
				int bg = v_row_ptr[column];


				R = rg;//((rg - 128)<<1) + gg;
				G = gg;
				B = bg;//((bg - 128)<<1) + gg;

				if(R<0) R=0;
				if(B<0) B=0;
				if(R>255) R=255;
				if(B>255) B=255;

				if(matrix_non_unity)
				{
					int r1,g1,b1;
					r1= (( r_rmult * R + r_gmult * G + r_bmult * B)>>8);
					g1= (( g_rmult * R + g_gmult * G + g_bmult * B)>>8);
					b1= (( b_rmult * R + b_gmult * G + b_bmult * B)>>8);

					R = r1;
					G = g1;
					B = b1;

					if(R < 0) R = 0;
					if(R > 255) R = 255;
					if(G < 0) G = 0;
					if(G > 255) G = 255;
					if(B < 0) B = 0;
					if(B > 255) B = 255;
				}

				y1 =( y_rmult * R + y_gmult * G + y_bmult * B + y_offset) >> shift;
				u = (-u_rmult * R - u_gmult * G + u_bmult * B + u_offset) >> (shift+1);
				v = ( v_rmult * R - v_gmult * G - v_bmult * B + v_offset) >> (shift+1);


				gg = luma_row_ptr[column+1];
				rg = u_row_ptr[column+1];
				bg = v_row_ptr[column+1];


				R = rg;//((rg - 128)<<1) + gg;
				G = gg;
				B = bg;//((bg - 128)<<1) + gg;

				if(R<0) R=0;
				if(B<0) B=0;
				if(R>255) R=255;
				if(B>255) B=255;

				if(matrix_non_unity)
				{
					int r1,g1,b1;
					r1= (( r_rmult * R + r_gmult * G + r_bmult * B)>>8);
					g1= (( g_rmult * R + g_gmult * G + g_bmult * B)>>8);
					b1= (( b_rmult * R + b_gmult * G + b_bmult * B)>>8);

					R = r1;
					G = g1;
					B = b1;

					if(R < 0) R = 0;
					if(R > 255) R = 255;
					if(G < 0) G = 0;
					if(G > 255) G = 255;
					if(B < 0) B = 0;
					if(B > 255) B = 255;
				}

				y2 = ( y_rmult * R + y_gmult * G + y_bmult * B + y_offset) >> shift;
				u += (-u_rmult * R - u_gmult * G + u_bmult * B + u_offset) >> (shift+1);
				v += ( v_rmult * R - v_gmult * G - v_bmult * B + v_offset) >> (shift+1);



				output_row_ptr[2 * column + 0] = SATURATE_Y(y1);
				output_row_ptr[2 * column + 1] = SATURATE_Cr(u);
				output_row_ptr[2 * column + 2] = SATURATE_Y(y2);
				output_row_ptr[2 * column + 3] = SATURATE_Cb(v);
			}

			// Fill the rest of the frame with black
			for (; column < frame_width; column += 2)
			{
				output_row_ptr[2 * column + 0] = COLOR_LUMA_BLACK;
				output_row_ptr[2 * column + 1] = COLOR_CHROMA_ZERO;
			}

			// Advance the input and output row pointers
			luma_row_ptr += planar_pitch[0];
			u_row_ptr += planar_pitch[1];
			v_row_ptr += planar_pitch[2];
			output_row_ptr += output_pitch;
		}
	}

	else if (format == DECODED_FORMAT_YR16) //this code uses 16-bit source to maintain precision.
	{

		PIXEL16U *luma_row_ptr = (PIXEL16U *)planar_output[0];
		PIXEL16U *u_row_ptr = (PIXEL16U *)planar_output[1];
		PIXEL16U *v_row_ptr = (PIXEL16U *)planar_output[2];

		for (row = 0; row < height; row++)
		{
			int column = 0;
			PIXEL16U *Yptr = (PIXEL16U *)output_row_ptr;
			PIXEL16U *Vptr = Yptr + width;
			PIXEL16U *Uptr = Vptr + (width>>1);

			// Process the rest of the row
			for (; column < width; column+=2)
			{
				int y1,u,y2,v,R,G,B;
				int gg = luma_row_ptr[column];
				int rg = u_row_ptr[column];
				int bg = v_row_ptr[column];


				R = rg;//((rg - 128)<<1) + gg;
				G = gg;
				B = bg;//((bg - 128)<<1) + gg;

				y1 =(( y_rmult * R + y_gmult * G + y_bmult * B) >> shift) + y_offset;
				u = ((-u_rmult * R - u_gmult * G + u_bmult * B) >> (shift+1));
				v = (( v_rmult * R - v_gmult * G - v_bmult * B) >> (shift+1));


				gg = luma_row_ptr[column+1];
				rg = u_row_ptr[column+1];
				bg = v_row_ptr[column+1];


				R = rg;//((rg - 128)<<1) + gg;
				G = gg;
				B = bg;//((bg - 128)<<1) + gg;

				y2 = (( y_rmult * R + y_gmult * G + y_bmult * B) >> shift) + y_offset;
				u += ((-u_rmult * R - u_gmult * G + u_bmult * B) >> (shift+1));
				v += (( v_rmult * R - v_gmult * G - v_bmult * B) >> (shift+1));
				u += u_offset;
				v += v_offset;

				*Yptr++ = SATURATE_16U(y1);
				*Yptr++ = SATURATE_16U(y2);
				*Vptr++ = SATURATE_16U(v);
				*Uptr++ = SATURATE_16U(u);
			}

			// Fill the rest of the frame with black
			for (; column < frame_width; column += 2)
			{
				*Yptr++ = SATURATE_16U(COLOR_LUMA_BLACK<<8);
				*Yptr++ = SATURATE_16U(COLOR_LUMA_BLACK<<8);
				*Vptr++ = SATURATE_16U(COLOR_CHROMA_ZERO<<8);
				*Uptr++ = SATURATE_16U(COLOR_CHROMA_ZERO<<8);
			}

			// Advance the input and output row pointers
			luma_row_ptr += planar_pitch[0]/2;
			u_row_ptr += planar_pitch[1]/2;
			v_row_ptr += planar_pitch[2]/2;
			output_row_ptr += output_pitch;
		}
	}
}


//#if BUILD_PROSPECT
void ConvertYUVStripPlanar16uToPacked(PIXEL16U *planar_output[], int planar_pitch[], ROI strip,
									  PIXEL16U *output, int output_pitch, int frame_width, int format)
{
	int width = strip.width;
	int height = strip.height;
	PIXEL16U *y_row_ptr = planar_output[0];
	PIXEL16U *u_row_ptr = planar_output[1];
	PIXEL16U *v_row_ptr = planar_output[2];
	PIXEL16U *output_row_ptr = output;

	// Convert pitch to units of pixels
	int y_pitch = planar_pitch[0]/sizeof(PIXEL16U);
	int u_pitch = planar_pitch[1]/sizeof(PIXEL16U);
	int v_pitch = planar_pitch[2]/sizeof(PIXEL16U);

	int row;

	// Must have an even number of output pixels
	assert((width % 2) == 0);

	// This routine should only be called to output 16-bit pixels
	assert(format == DECODED_FORMAT_YR16);

	for (row = 0; row < height; row++)
	{
		int column = 0;

#if (1 && XMMOPT)

		int column_step = 8;
		int post_column = width - (width % column_step);

		__m128i *y_ptr = (__m128i *)y_row_ptr;
		__m128i *u_ptr = (__m128i *)u_row_ptr;
		__m128i *v_ptr = (__m128i *)v_row_ptr;
		__m128i *yuv_ptr = (__m128i *)output_row_ptr;

		for (; column < post_column; column += column_step)
		{
			__m128i y1_epi16 = _mm_load_si128(y_ptr++);
			__m128i y2_epi16 = _mm_load_si128(y_ptr++);
			__m128i u_epi16 = _mm_load_si128(u_ptr++);
			__m128i v_epi16 = _mm_load_si128(v_ptr++);
			__m128i uv_epi16;
			__m128i yuv_epi16;

			// Interleave the first four pairs of chroma values
			uv_epi16 = _mm_unpacklo_epi16(v_epi16, u_epi16);

			// Interleave and store the first group of luma and chroma pairs
			yuv_epi16 = _mm_unpacklo_epi16(uv_epi16, y1_epi16);
			_mm_store_si128(yuv_ptr++, yuv_epi16);

			// Interleave and store the second group of luma and chroma pairs
			yuv_epi16 = _mm_unpackhi_epi16(uv_epi16, y1_epi16);
			_mm_store_si128(yuv_ptr++, yuv_epi16);

			// Interleave the second four pairs of chroma values
			uv_epi16 = _mm_unpackhi_epi16(v_epi16, u_epi16);

			// Interleave and store the third group of luma and chroma pairs
			yuv_epi16 = _mm_unpacklo_epi16(uv_epi16, y2_epi16);
			_mm_store_si128(yuv_ptr++, yuv_epi16);

			// Interleave and store the fourth group of luma and chroma pairs
			yuv_epi16 = _mm_unpackhi_epi16(uv_epi16, y2_epi16);
			_mm_store_si128(yuv_ptr++, yuv_epi16);
		}
		
		// Should have exited the loop at the post processing column
		assert(column == post_column);
#endif

		// Process the rest of the row
		for (; column < width; column += 2)
		{
			int y1 = y_row_ptr[column + 0];
			int y2 = y_row_ptr[column + 1];

			int u = u_row_ptr[column/2];
			int v = v_row_ptr[column/2];

			output_row_ptr[2 * column + 0] = v;
			output_row_ptr[2 * column + 1] = y1;
			output_row_ptr[2 * column + 2] = u;
			output_row_ptr[2 * column + 3] = y2;
		}

		// Fill the rest of the frame with black
		for (; column < frame_width; column += 2)
		{
			output_row_ptr[2 * column + 0] = COLOR_CHROMA_ZERO;
			output_row_ptr[2 * column + 1] = COLOR_LUMA_BLACK;
		}

		// Advance the input and output row pointers
		y_row_ptr += y_pitch;
		u_row_ptr += u_pitch;
		v_row_ptr += v_pitch;
		output_row_ptr += output_pitch;
	}
}
//#endif

void ConvertPlanarYUVToRGB(uint8_t *planar_output[], int planar_pitch[], ROI roi,
						   uint8_t *output_buffer, int output_width, int output_pitch,
						   int format, int colorspace, bool inverted)
{
	int width = roi.width;
	int height = roi.height;

	// Note that this routine is called with the YUV channels in our own
	// internal order so the chroma values have not already been reversed

	uint8_t *y_row_ptr = planar_output[0];
	uint8_t *u_row_ptr = planar_output[2];		// Reverse the chroma order
	uint8_t *v_row_ptr = planar_output[1];

	int y_pitch = planar_pitch[0];
	int u_pitch = planar_pitch[2];			// Reverse the chroma order
	int v_pitch = planar_pitch[1];

	uint8_t *output_row_ptr = output_buffer;

	// Definitions for optimization
	//const int column_step = 16;

	// Column at which post processing must begin
	//int post_column = width - (width % column_step);

	//CG_601
	int y_offset = 16; // not VIDEO_RGB & not YUV709
	int ymult = 128*149;	//7bit 1.164
	int r_vmult = 204;		//7bit 1.596
	int g_vmult = 208;		//8bit 0.813
	int g_umult = 100;		//8bit 0.391
	int b_umult = 129;		//6bit 2.018
	int saturate = 1;
	int upconvert422to444 = 0;

	if(colorspace & COLOR_SPACE_422_TO_444)
	{
		upconvert422to444 = 1;
	}

	switch(colorspace & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:
		y_offset = 16;		// not VIDEO_RGB & not YUV709
		ymult = 128*149;	//7bit 1.164
		r_vmult = 204;		//7bit 1.596
		g_vmult = 208;		//8bit 0.813
		g_umult = 100;		//8bit 0.391
		b_umult = 129;		//6bit 2.018
		saturate = 1;
		break;

	default: assert(0);
	case COLOR_SPACE_CG_709:
		y_offset = 16;
		ymult = 128*149;	//7bit 1.164
		r_vmult = 230;		//7bit 1.793
		g_vmult = 137;		//8bit 0.534
		g_umult = 55;		//8bit 0.213
		b_umult = 135;		//6bit 2.115
		saturate = 1;
		break;

	case COLOR_SPACE_VS_601:
		y_offset = 0;
		ymult = 128*128;	//7bit 1.0
		r_vmult = 175;		//7bit 1.371
		g_vmult = 179;		//8bit 0.698
		g_umult = 86;		//8bit 0.336
		b_umult = 111;		//6bit 1.732
		saturate = 0;
		break;

	case COLOR_SPACE_VS_709:
		y_offset = 0;
		ymult = 128*128;	//7bit 1.0
		r_vmult = 197;		//7bit 1.540
		g_vmult = 118;		//8bit 0.459
		g_umult = 47;		//8bit 0.183
		b_umult = 116;		//6bit 1.816
		saturate = 0;
		break;
	}

	// Should the image be inverted?
	if (inverted && output_pitch > 0) {
		output_row_ptr += (height - 1) * output_pitch;	// Start at the bottom row
		output_pitch = (- output_pitch);				// Negate the pitch to go up
	}


	// Is the output color format RGB24?
	if (format == COLOR_FORMAT_RGB24)
	{
		int row;

		for (row = 0; row < height; row++)
		{
			int column = 0;


#if (0 && XMMOPT) //DANREMOVED

			__m64 *output_ptr = (__m64 *)output_row_ptr;

			// Load 16 bytes of luma and 8 bytes of each chroma channel
			// to compute and store 3 times 16 = 48 bytes of RGB tuples.

			for (; column < post_column; column += column_step)
			{
				__m64 y_pi8;		// Eight unsigned bytes of color values
				__m64 u_pi8;
				__m64 v_pi8;

				__m64 y_pi16;		// Four unpacked color values
				__m64 u_pi16;
				__m64 v_pi16;

				__m64 uu_pi16;		// Duplicated chroma values
				__m64 vv_pi16;

				__m64 R1, G1, B1;
				__m64 R2, G2, B2;
				__m64 R_pi8, G_pi8, B_pi8;
				__m64 temp;
				__m64 RGB;

				__m64 RG;
				__m64 BZ;
				__m64 RGBZ;

				__m64 rounding_pi16 = _mm_set1_pi16(32); // for 6bit matm

				// Adjust the column for YUV 4:2:2 frame format
				int chroma_column = column/2;


				/***** Process the first eight bytes of luma *****/

				// Load the first eight bytes of luma
				y_pi8 = *((__m64 *)&y_row_ptr[column]);

				// Load eight bytes of chroma (u channel)
				u_pi8 = *((__m64 *)&u_row_ptr[chroma_column]);

				// Load eight bytes of chroma (v channel)
				v_pi8 = *((__m64 *)&v_row_ptr[chroma_column]);

#if STRICT_SATURATE
				if(saturate)
				{
					// Perform strict saturation on YUV if required
					y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(16));
					y_pi8 = _mm_adds_pu8(y_pi8, _mm_set1_pi8(36));
					y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(20));

					u_pi8 = _mm_subs_pu8(u_pi8, _mm_set1_pi8(16));
					u_pi8 = _mm_adds_pu8(u_pi8, _mm_set1_pi8(31));
					u_pi8 = _mm_subs_pu8(u_pi8, _mm_set1_pi8(15));

					v_pi8 = _mm_subs_pu8(v_pi8, _mm_set1_pi8(16));
					v_pi8 = _mm_adds_pu8(v_pi8, _mm_set1_pi8(31));
					v_pi8 = _mm_subs_pu8(v_pi8, _mm_set1_pi8(15));
				}
#endif
				// Unpack the first group of four bytes of luma
				y_pi16 = _mm_unpacklo_pi8(y_pi8, _mm_setzero_si64());

				// Unpack the first group of four bytes of u chroma
				u_pi16 = _mm_unpacklo_pi8(u_pi8, _mm_setzero_si64());

				// Unpack the first group of four bytes of v chroma
				v_pi16 = _mm_unpacklo_pi8(v_pi8, _mm_setzero_si64());

				// Duplicate the first two chroma values
				uu_pi16 = _mm_unpacklo_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpacklo_pi16(v_pi16, v_pi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					extracted_u = u_row_ptr[chroma_column+2]>>1;
					extracted_v = v_row_ptr[chroma_column+2]>>1;

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);

				y_pi16 = _mm_slli_pi16(y_pi16, 7);	// This code fixed overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R1 = _mm_adds_pi16(y_pi16, temp);
					R1 = _mm_adds_pi16(R1, rounding_pi16);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(G1, temp);
					G1 = _mm_adds_pi16(G1, rounding_pi16);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B1 = _mm_adds_pi16(y_pi16, temp);
					B1 = _mm_adds_pi16(B1, rounding_pi16);
				B1 = _mm_srai_pi16(B1, 6);


				// Unpack the second group of four bytes of luma
				y_pi16 = _mm_unpackhi_pi8(y_pi8, _mm_setzero_si64());

				// Preload the second eight bytes of luma
				y_pi8 = *((__m64 *)&y_row_ptr[column + 8]);

				// Duplicate the second two chroma values
				uu_pi16 = _mm_unpackhi_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpackhi_pi16(v_pi16, v_pi16);


#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					extracted_u = u_row_ptr[chroma_column+4]>>1;
					extracted_v = v_row_ptr[chroma_column+4]>>1;

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif


				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);

				y_pi16 = _mm_slli_pi16(y_pi16, 7);	// This code fixed an overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R2 = _mm_adds_pi16(y_pi16, temp);
					R2 = _mm_adds_pi16(R2, rounding_pi16);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(G2, temp);
					G2 = _mm_adds_pi16(G2, rounding_pi16);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B2 = _mm_adds_pi16(y_pi16, temp);
					B2 = _mm_adds_pi16(B2, rounding_pi16);
				B2 = _mm_srai_pi16(B2, 6);


				/**** Pack and store the first set of eight RGB tuples (24 bytes) *****/

				// Pack the RGB tuples
				R_pi8 = _mm_packs_pu16(B1, B2); // swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); // swapped with B

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with zero
				BZ = _mm_unpacklo_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the first two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Get the first RGB tuple with zeros in the rest of the word
				RGB = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());

				// Get the second RGB tuple with zeros in the rest of the word
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 3*8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have two RGB tuples with two zero bytes in the rest of the word

				// Interleave the second two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Insert the first two red and green values into the output word
				RGB = _mm_or_si64(RGB, _mm_slli_si64(RGBZ, 6*8));

				// Store the first group of eight bytes of RGB values
				*(output_ptr++) = RGB;

				// Shift the blue value into position
				RGB = _mm_slli_si64(RGBZ, 5*8);
				RGB = _mm_srli_si64(RGB, 7*8);

				// Shift the second RGB tuple into position
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have four output bytes in the lower half of the word

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with zero
				BZ = _mm_unpackhi_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the third two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Insert the first RGB tuple into the output word
				RGB = _mm_unpacklo_pi32(RGB, RGBZ);

				// Shift the red value from the second RGB tuple into position
				temp = _mm_srli_si64(RGBZ, 4*8);
				temp = _mm_slli_si64(temp, 7*8);

				// Insert the red value into the upper byte of the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the second group of eight bytes of RGB values
				*(output_ptr++) = RGB;

				// Shift the green and blue values into position
				RGB = _mm_srli_si64(RGBZ, 5*8);

				// Interleave the fourth two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Shift the next RGB tuple into position
				temp = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 2*8);

				// Insert the RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Shift the last RGB tuple into position
				temp = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 5*8);

				// Insert the last RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the third group of eight bytes of RGB values
				*(output_ptr++) = RGB;


				/***** Process the second eight bytes of luma *****/

#if STRICT_SATURATE
				if(saturate)
				{
					// Perform strict saturation on YUV if required
					y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(16));
					y_pi8 = _mm_adds_pu8(y_pi8, _mm_set1_pi8(36));
					y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(20));
				}
#endif
				// Unpack the third group of four bytes of luma
				y_pi16 = _mm_unpacklo_pi8(y_pi8, _mm_setzero_si64());

				// Unpack the second group of four bytes of u chroma
				u_pi16 = _mm_unpackhi_pi8(u_pi8, _mm_setzero_si64());

				// Unpack the second group of four bytes of v chroma
				v_pi16 = _mm_unpackhi_pi8(v_pi8, _mm_setzero_si64());

				// Duplicate the third two chroma values
				uu_pi16 = _mm_unpacklo_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpacklo_pi16(v_pi16, v_pi16);


#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					extracted_u = u_row_ptr[chroma_column+6]>>1;
					extracted_v = v_row_ptr[chroma_column+6]>>1;

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);

				y_pi16 = _mm_slli_pi16(y_pi16, 7);		// This code fixed an overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);			// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R1 = _mm_adds_pi16(y_pi16, temp);
					R1 = _mm_adds_pi16(R1, rounding_pi16);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(G1, temp);
					G1 = _mm_adds_pi16(G1, rounding_pi16);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B1 = _mm_adds_pi16(y_pi16, temp);
					B1 = _mm_adds_pi16(B1, rounding_pi16);
				B1 = _mm_srai_pi16(B1, 6);


				// Unpack the fourth group of four bytes of luma
				y_pi16 = _mm_unpackhi_pi8(y_pi8, _mm_setzero_si64());

				// Duplicate the fourth two chroma values
				uu_pi16 = _mm_unpackhi_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpackhi_pi16(v_pi16, v_pi16);


#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					if(column < width-2)
					{
						extracted_u = u_row_ptr[chroma_column+8]>>1;
						extracted_v = v_row_ptr[chroma_column+8]>>1;
					}
					else
					{
						extracted_u = u_row_ptr[chroma_column+7]>>1;
						extracted_v = v_row_ptr[chroma_column+7]>>1;
					}

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);

				y_pi16 = _mm_slli_pi16(y_pi16, 7);		// This code fixed an overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);			// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R2 = _mm_adds_pi16(y_pi16, temp);
					R2 = _mm_adds_pi16(R2, rounding_pi16);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(G2, temp);
					G2 = _mm_adds_pi16(G2, rounding_pi16);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B2 = _mm_adds_pi16(y_pi16, temp);
					B2 = _mm_adds_pi16(B2, rounding_pi16);
				B2 = _mm_srai_pi16(B2, 6);


				/**** Pack and store the second set of eight RGB tuples (24 bytes) *****/

				// Pack the RGB tuples
				R_pi8 = _mm_packs_pu16(B1, B2); // swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); // swapped with B

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with zero
				BZ = _mm_unpacklo_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the first two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Get the first RGB tuple with zeros in the rest of the word
				RGB = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());

				// Get the second RGB tuple with zeros in the rest of the word
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 3*8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have two RGB tuples with two zero bytes in the rest of the word

				// Interleave the second two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Insert the first two red and green values into the output word
				RGB = _mm_or_si64(RGB, _mm_slli_si64(RGBZ, 6*8));

				// Store the fourth group of eight bytes of RGB values
				*(output_ptr++) = RGB;

				// Shift the blue value into position
				RGB = _mm_slli_si64(RGBZ, 5*8);
				RGB = _mm_srli_si64(RGB, 7*8);

				// Shift the second RGB tuple into position
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have four output bytes in the lower half of the word

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with zero
				BZ = _mm_unpackhi_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the third two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Insert the first RGB tuple into the output word
				RGB = _mm_unpacklo_pi32(RGB, RGBZ);

				// Shift the red value from the second RGB tuple into position
				temp = _mm_srli_si64(RGBZ, 4*8);
				temp = _mm_slli_si64(temp, 7*8);

				// Insert the red value into the upper byte of the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the fifth group of eight bytes of RGB values
				*(output_ptr++) = RGB;

				// Shift the green and blue values into position
				RGB = _mm_srli_si64(RGBZ, 5*8);

				// Interleave the fourth two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Shift the next RGB tuple into position
				temp = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 2*8);

				// Insert the RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Shift the last RGB tuple into position
				temp = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 5*8);

				// Insert the last RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the sixth group of eight bytes of RGB values
				*(output_ptr++) = RGB;
			}

			//_mm_empty();

			// Should have exited the loop at the post processing column
			assert(column == post_column);

#endif

			// Process the rest of the row
			for (; column < width; column += 2)
			{
				int Y1,Y2,U,V;
				// Convert to RGB
				int output_column = 3 * column;
				int R, G, B;

				if(saturate)
				{
					Y1 = SATURATE_Y(y_row_ptr[column]);
					U = SATURATE_Cr(u_row_ptr[column/2]);
					Y2 = SATURATE_Y(y_row_ptr[column + 1]);
					V = SATURATE_Cb(v_row_ptr[column/2]);
				}
				else
				{
					Y1 = (y_row_ptr[column]);
					U = (u_row_ptr[column/2]);
					Y2 = (y_row_ptr[column + 1]);
					V = (v_row_ptr[column/2]);
				}


				// Convert the first set of YCbCr values

				Y1 = Y1 - y_offset;
				Y2 = Y2 - y_offset;
				U = U - 128;
				V = V - 128;

				Y1 = Y1 * ymult >> 7;
				Y2 = Y2 * ymult >> 7;

				R = (Y1           + r_vmult * V + 64) >> 7;
				G = (Y1*2 -  g_umult * U - g_vmult * V + 128) >> 8;
				B = (Y1 + 2 * b_umult * U + 64) >> 7;

			//	R = (int)(1.164*((double)Y1) + 1.596*((double)V) + 0.5);
			//	G = (int)(1.164*((double)Y1) - 0.813*((double)V) - 0.391*((double)U) + 0.5);
			//	B = (int)(1.164*((double)Y1) + 2.018*((double)U) + 0.5);

				output_row_ptr[output_column + 0] = SATURATE_8U(B);
				output_row_ptr[output_column + 1] = SATURATE_8U(G);
				output_row_ptr[output_column + 2] = SATURATE_8U(R);

				// Convert the second set of YCbCr values

				R = (Y2           + r_vmult * V + 64) >> 7;
				G = (Y2*2 -  g_umult * U - g_vmult * V + 128) >> 8;
				B = (Y2 + 2 * b_umult * U + 64) >> 7;

			//	R = (int)(1.164*((double)Y2) + 1.596*((double)V) + 0.5);
			//	G = (int)(1.164*((double)Y2) - 0.813*((double)V) - 0.391*((double)U) + 0.5);
			//	B = (int)(1.164*((double)Y2) + 2.018*((double)U) + 0.5);

				output_row_ptr[output_column + 3] = SATURATE_8U(B);
				output_row_ptr[output_column + 4] = SATURATE_8U(G);
				output_row_ptr[output_column + 5] = SATURATE_8U(R);
			}

			// Should have exited the loop just after the last column
			assert(column == width);

			// Advance to the next rows in the input and output arrays
			y_row_ptr += y_pitch;
			u_row_ptr += u_pitch;
			v_row_ptr += v_pitch;
			output_row_ptr += output_pitch;
		}
	}
	else	// The output color format must be RGB32
	{
		int row;

		assert(format == COLOR_FORMAT_RGB32);

		for (row = 0; row < height; row++)
		{
			int column = 0;


#if (0 && XMMOPT) //DANREMOVED

			__m64 *output_ptr = (__m64 *)output_row_ptr;

			// Load 16 bytes of luma and 8 bytes of each chroma channel
			// to compute and store 3 times 16 = 48 bytes of RGB tuples.

			for (; column < post_column; column += column_step)
			{
				__m64 y_pi8;		// Eight unsigned bytes of color values
				__m64 u_pi8;
				__m64 v_pi8;

				__m64 y_pi16;		// Four unpacked color values
				__m64 u_pi16;
				__m64 v_pi16;

				__m64 uu_pi16;		// Duplicated chroma values
				__m64 vv_pi16;

				__m64 R1, G1, B1;
				__m64 R2, G2, B2;
				__m64 R_pi8, G_pi8, B_pi8;
				__m64 temp;

				__m64 RG;
				__m64 BA;
				__m64 RGBA;

				__m64 rounding_pi16 = _mm_set1_pi16(32); // 6bit half pt.

				// Adjust the column for YUV 4:2:2 frame format
				int chroma_column = column/2;


				/***** Process the first eight bytes of luma *****/

				// Load the first eight bytes of luma
				y_pi8 = *((__m64 *)&y_row_ptr[column]);

				// Load eight bytes of chroma (u channel)
				u_pi8 = *((__m64 *)&u_row_ptr[chroma_column]);

				// Load eight bytes of chroma (v channel)
				v_pi8 = *((__m64 *)&v_row_ptr[chroma_column]);

#if STRICT_SATURATE
				if(saturate)
				{
					// Perform strict saturation on YUV if required
					y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(16));
					y_pi8 = _mm_adds_pu8(y_pi8, _mm_set1_pi8(36));
					y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(20));

					u_pi8 = _mm_subs_pu8(u_pi8, _mm_set1_pi8(16));
					u_pi8 = _mm_adds_pu8(u_pi8, _mm_set1_pi8(31));
					u_pi8 = _mm_subs_pu8(u_pi8, _mm_set1_pi8(15));

					v_pi8 = _mm_subs_pu8(v_pi8, _mm_set1_pi8(16));
					v_pi8 = _mm_adds_pu8(v_pi8, _mm_set1_pi8(31));
					v_pi8 = _mm_subs_pu8(v_pi8, _mm_set1_pi8(15));
				}
#endif
				// Unpack the first group of four bytes of luma
				y_pi16 = _mm_unpacklo_pi8(y_pi8, _mm_setzero_si64());

				// Unpack the first group of four bytes of u chroma
				u_pi16 = _mm_unpacklo_pi8(u_pi8, _mm_setzero_si64());

				// Unpack the first group of four bytes of v chroma
				v_pi16 = _mm_unpacklo_pi8(v_pi8, _mm_setzero_si64());

				// Duplicate the first two chroma values
				uu_pi16 = _mm_unpacklo_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpacklo_pi16(v_pi16, v_pi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					extracted_u = u_row_ptr[chroma_column+2]>>1;
					extracted_v = v_row_ptr[chroma_column+2]>>1;

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);

				y_pi16 = _mm_slli_pi16(y_pi16, 7);	// This code fixed overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R1 = _mm_adds_pi16(y_pi16, temp);
		//		R1 = _mm_adds_pi16(y_pi16, temp);
				R1 = _mm_adds_pi16(R1, rounding_pi16);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_adds_pi16(G1, rounding_pi16);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B1 = _mm_adds_pi16(y_pi16, temp);
				B1 = _mm_adds_pi16(B1, rounding_pi16);
				B1 = _mm_srai_pi16(B1, 6);


				// Unpack the second group of four bytes of luma
				y_pi16 = _mm_unpackhi_pi8(y_pi8, _mm_setzero_si64());

				// Preload the second eight bytes of luma
				y_pi8 = *((__m64 *)&y_row_ptr[column + 8]);

				// Duplicate the second two chroma values
				uu_pi16 = _mm_unpackhi_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpackhi_pi16(v_pi16, v_pi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					extracted_u = u_row_ptr[chroma_column+4]>>1;
					extracted_v = v_row_ptr[chroma_column+4]>>1;

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);

				y_pi16 = _mm_slli_pi16(y_pi16, 7);	// This code fixed an overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R2 = _mm_adds_pi16(y_pi16, temp);
				R2 = _mm_adds_pi16(R2, rounding_pi16);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_adds_pi16(G2, rounding_pi16);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B2 = _mm_adds_pi16(y_pi16, temp);
				B2 = _mm_adds_pi16(B2, rounding_pi16);
				B2 = _mm_srai_pi16(B2, 6);


				/**** Pack and store the first set of eight RGBA tuples (32 bytes) *****/

				// Pack the RGB values
				R_pi8 = _mm_packs_pu16(B1, B2); //swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); //swapped with B

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with the default alpha value
				BA = _mm_unpacklo_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the first two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);

				// Store the first group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;

				// Interleave the second two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);

				// Store the second group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with the default alpha value
				BA = _mm_unpackhi_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the third two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);

				// Store the third group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;

				// Interleave the fourth two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);

				// Store the fourth group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;


				/***** Process the second eight bytes of luma *****/

#if STRICT_SATURATE
				if(saturate)
				{
					// Perform strict saturation on YUV if required
					y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(16));
					y_pi8 = _mm_adds_pu8(y_pi8, _mm_set1_pi8(36));
					y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(20));
				}
#endif
				// Unpack the third group of four bytes of luma
				y_pi16 = _mm_unpacklo_pi8(y_pi8, _mm_setzero_si64());

				// Unpack the second group of four bytes of u chroma
				u_pi16 = _mm_unpackhi_pi8(u_pi8, _mm_setzero_si64());

				// Unpack the second group of four bytes of v chroma
				v_pi16 = _mm_unpackhi_pi8(v_pi8, _mm_setzero_si64());

				// Duplicate the third two chroma values
				uu_pi16 = _mm_unpacklo_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpacklo_pi16(v_pi16, v_pi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					extracted_u = u_row_ptr[chroma_column+6]>>1;
					extracted_v = v_row_ptr[chroma_column+6]>>1;

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif

				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);


				y_pi16 = _mm_slli_pi16(y_pi16, 7);		// This code fixed an overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);			// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R1 = _mm_adds_pi16(y_pi16, temp);
					R1 = _mm_adds_pi16(R1, rounding_pi16);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(G1, temp);
					G1 = _mm_adds_pi16(G1, rounding_pi16);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B1 = _mm_adds_pi16(y_pi16, temp);
					B1 = _mm_adds_pi16(B1, rounding_pi16);
				B1 = _mm_srai_pi16(B1, 6);


				// Unpack the fourth group of four bytes of luma
				y_pi16 = _mm_unpackhi_pi8(y_pi8, _mm_setzero_si64());

				// Duplicate the fourth two chroma values
				uu_pi16 = _mm_unpackhi_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpackhi_pi16(v_pi16, v_pi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					if(column < width-2)
					{
						extracted_u = u_row_ptr[chroma_column+8]>>1;
						extracted_v = v_row_ptr[chroma_column+8]>>1;
					}
					else
					{
						extracted_u = u_row_ptr[chroma_column+7]>>1;
						extracted_v = v_row_ptr[chroma_column+7]>>1;
					}
					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif
				// Convert to RGB
				temp = _mm_set1_pi16(y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);


				y_pi16 = _mm_slli_pi16(y_pi16, 7);		// This code fixed an overflow case where very bright pixels
				temp = _mm_set1_pi16(ymult);			// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R2 = _mm_adds_pi16(y_pi16, temp);
					R2 = _mm_adds_pi16(R2, rounding_pi16);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(G2, temp);
					G2 = _mm_adds_pi16(G2, rounding_pi16);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B2 = _mm_adds_pi16(y_pi16, temp);
					B2 = _mm_adds_pi16(B2, rounding_pi16);
				B2 = _mm_srai_pi16(B2, 6);


				/**** Pack and store the second set of eight RGB tuples (32 bytes) *****/

				// Pack the RGB values
				R_pi8 = _mm_packs_pu16(B1, B2); //swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); //swapped with B

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with the default alpha value
				BA = _mm_unpacklo_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the first two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);

				// Store the first group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;

				// Interleave the second two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);

				// Store the second group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with the default alpha value
				BA = _mm_unpackhi_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the third two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);

				// Store the third group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;

				// Interleave the fourth two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);

				// Store the fourth group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;
			}

			//_mm_empty();

			// Should have exited the loop at the post processing column
			assert(column == post_column);

#endif

			// Process the rest of the row
			for (; column < width; column += 2)
			{
				// Convert to RGB
				int output_column = 4 * column;
				int R, G, B;
				int Y1,Y2,U,V;

				if(saturate)
				{
					Y1 = SATURATE_Y(y_row_ptr[column]);
					U = SATURATE_Cr(u_row_ptr[column/2]);
					Y2 = SATURATE_Y(y_row_ptr[column + 1]);
					V = SATURATE_Cb(v_row_ptr[column/2]);
				}
				else
				{
					Y1 = (y_row_ptr[column]);
					U = (u_row_ptr[column/2]);
					Y2 = (y_row_ptr[column + 1]);
					V = (v_row_ptr[column/2]);
				}


				Y1 = Y1 - y_offset;
				Y2 = Y2 - y_offset;
				U = U - 128;
				V = V - 128;

				Y1 = Y1 * ymult >> 7;
				Y2 = Y2 * ymult >> 7;

				R = (Y1           + r_vmult * V + 64) >> 7;
				G = (Y1*2 -  g_umult * U - g_vmult * V + 128) >> 8;
				B = (Y1 + 2 * b_umult * U + 64) >> 7;

				output_row_ptr[output_column + 0] = SATURATE_8U(B);
				output_row_ptr[output_column + 1] = SATURATE_8U(G);
				output_row_ptr[output_column + 2] = SATURATE_8U(R);
				output_row_ptr[output_column + 3] = RGBA_DEFAULT_ALPHA;

				// Convert the second set of YCbCr values
				R = (Y2           + r_vmult * V + 64) >> 7;
				G = (Y2*2 -  g_umult * U - g_vmult * V + 128) >> 8;
				B = (Y2 + 2 * b_umult * U + 64) >> 7;

				output_row_ptr[output_column + 4] = SATURATE_8U(B);
				output_row_ptr[output_column + 5] = SATURATE_8U(G);
				output_row_ptr[output_column + 6] = SATURATE_8U(R);
				output_row_ptr[output_column + 7] = RGBA_DEFAULT_ALPHA;
			}

			// Should have exited the loop just after the last column
			assert(column == width);

			// Advance to the next rows in the input and output arrays
			y_row_ptr += y_pitch;
			u_row_ptr += u_pitch;
			v_row_ptr += v_pitch;
			output_row_ptr += output_pitch;
		}
	}
}

void ConvertRow16uToDitheredRGB(DECODER *decoder, uint8_t *planar_output[], int planar_pitch[], ROI roi,
						   uint8_t *output_buffer, int output_width, int output_pitch,
						   int format, int colorspace, bool inverted)
{
	int width = roi.width;
	int height = roi.height;

	// Note that this routine is called with the YUV channels in our own
	// internal order so the chroma values have not already been reversed255

	PIXEL16U *y_row_ptr = (PIXEL16U *)planar_output[0];
	PIXEL16U *u_row_ptr = (PIXEL16U *)planar_output[2];		// Reverse the chroma order
	PIXEL16U *v_row_ptr = (PIXEL16U *)planar_output[1];

	int y_pitch = planar_pitch[0]/sizeof(PIXEL16U);
	int u_pitch = planar_pitch[2]/sizeof(PIXEL16U);			// Reverse the chroma order
	int v_pitch = planar_pitch[1]/sizeof(PIXEL16U);

	uint8_t *output_row_ptr = output_buffer;

	// Definitions for optimization
	//const int column_step = 16;

	// Column at which post processing must begin
	//int post_column = width - (width % column_step);

	int y_offset = 16; // not VIDEO_RGB & not YUV709
	int ymult = 128*149;	//7bit 1.164
	int r_vmult = 204;		//7bit 1.596
	int g_vmult = 208;		//8bit 0.813
	int g_umult = 100;		//8bit 0.391
	int b_umult = 129;		//6bit 2.018
	int saturate = 1;
	int mmx_y_offset = (y_offset<<7);
	int upconvert422to444 = 0;

	if(colorspace & COLOR_SPACE_422_TO_444)
	{
		upconvert422to444 = 1;
	}

	switch(colorspace & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:
		y_offset = 16;		// not VIDEO_RGB & not YUV709
		ymult = 128*149;	//7bit 1.164
		r_vmult = 204;		//7bit 1.596
		g_vmult = 208;		//8bit 0.813
		g_umult = 100;		//8bit 0.391
		b_umult = 129;		//6bit 2.018
		saturate = 1;
		break;

	default: assert(0);
	case COLOR_SPACE_CG_709:
		y_offset = 16;
		ymult = 128*149;	//7bit 1.164
		r_vmult = 230;		//7bit 1.793
		g_vmult = 137;		//8bit 0.534
		g_umult = 55;		//8bit 0.213
		b_umult = 135;		//6bit 2.115
		saturate = 1;
		break;

	case COLOR_SPACE_VS_601:
		y_offset = 0;
		ymult = 128*128;	//7bit 1.0
		r_vmult = 175;		//7bit 1.371
		g_vmult = 179;		//8bit 0.698
		g_umult = 86;		//8bit 0.336
		b_umult = 111;		//6bit 1.732
		saturate = 0;
		break;

	case COLOR_SPACE_VS_709:
		y_offset = 0;
		ymult = 128*128;	//7bit 1.0
		r_vmult = 197;		//7bit 1.540
		g_vmult = 118;		//8bit 0.459
		g_umult = 47;		//8bit 0.183
		b_umult = 116;		//6bit 1.816
		saturate = 0;
		break;
	}

	mmx_y_offset = (y_offset<<7);


	// Should the image be inverted?
	if (inverted && output_pitch > 0) {
		output_row_ptr += (height - 1) * output_pitch;	// Start at the bottom row
		output_pitch = (- output_pitch);				// Negate the pitch to go up
	}

	// Is the output color format RGB24?
	if (format == COLOR_FORMAT_RGB24)
	{
		int row;

		for (row = 0; row < height; row++)
		{
			int column = 0;


#if (0 && XMMOPT) //DANREMOVED

			__m64 *output_ptr = (__m64 *)output_row_ptr;

			// Load 16 bytes of luma and 8 bytes of each chroma channel
			// to compute and store 3 times 16 = 48 bytes of RGB tuples.
			__m64 rounding_pi16 = _mm_set1_pi16(32); // for 6bit matm
			__m64 rounding2_pi16 = _mm_set1_pi16(32); // for 6bit matm
			__m64 rounding3_pi16 = _mm_set1_pi16(32); // for 6bit matm
			__m64 rounding4_pi16 = _mm_set1_pi16(32); // for 6bit matm

			rounding_pi16 = _mm_insert_pi16(rounding_pi16, rand()&63, 0);
			rounding_pi16 = _mm_insert_pi16(rounding_pi16, rand()&63, 1);
			rounding_pi16 = _mm_insert_pi16(rounding_pi16, rand()&63, 2);
			rounding_pi16 = _mm_insert_pi16(rounding_pi16, rand()&63, 3);

			rounding2_pi16 = _mm_insert_pi16(rounding2_pi16, rand()&63, 0);
			rounding2_pi16 = _mm_insert_pi16(rounding2_pi16, rand()&63, 1);
			rounding2_pi16 = _mm_insert_pi16(rounding2_pi16, rand()&63, 2);
			rounding2_pi16 = _mm_insert_pi16(rounding2_pi16, rand()&63, 3);

			rounding3_pi16 = _mm_insert_pi16(rounding3_pi16, rand()&63, 0);
			rounding3_pi16 = _mm_insert_pi16(rounding3_pi16, rand()&63, 1);
			rounding3_pi16 = _mm_insert_pi16(rounding3_pi16, rand()&63, 2);
			rounding3_pi16 = _mm_insert_pi16(rounding3_pi16, rand()&63, 3);

			rounding4_pi16 = _mm_insert_pi16(rounding4_pi16, rand()&63, 0);
			rounding4_pi16 = _mm_insert_pi16(rounding4_pi16, rand()&63, 1);
			rounding4_pi16 = _mm_insert_pi16(rounding4_pi16, rand()&63, 2);
			rounding4_pi16 = _mm_insert_pi16(rounding4_pi16, rand()&63, 3);

			for (; column < post_column; column += column_step)
			{
			//	__m64 y_pi8;		// Eight unsigned bytes of color values
			//	__m64 u_pi8;
			//	__m64 v_pi8;

				__m64 y_pi16;		// Four unpacked color values
				__m64 u_pi16;
				__m64 v_pi16;
				__m64 u2_pi16;
				__m64 v2_pi16;

				__m64 uu_pi16;		// Duplicated chroma values
				__m64 vv_pi16;

				__m64 R1, G1, B1;
				__m64 R2, G2, B2;
				__m64 R_pi8, G_pi8, B_pi8;
				__m64 temp;
				__m64 RGB;

				__m64 RG;
				__m64 BZ;
				__m64 RGBZ;

				// Adjust the column for YUV 4:2:2 frame format
				int chroma_column = column/2;

				/***** Process the first eight bytes of luma *****/

				// Load the first eight bytes of luma
				y_pi16 = *((__m64 *)&y_row_ptr[column]);

				// Load eight bytes of chroma (u channel)
				u_pi16 = *((__m64 *)&u_row_ptr[chroma_column]);

				// Load eight bytes of chroma (v channel)
				v_pi16 = *((__m64 *)&v_row_ptr[chroma_column]);

				y_pi16 = _mm_srli_pi16(y_pi16, 1);
				u_pi16 = _mm_srli_pi16(u_pi16, 2);
				v_pi16 = _mm_srli_pi16(v_pi16, 2);

				uu_pi16 = _mm_unpacklo_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpacklo_pi16(v_pi16, v_pi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					extracted_u = u_row_ptr[chroma_column+2]>>3;
					extracted_v = v_row_ptr[chroma_column+2]>>3;

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif

				uu_pi16 = _mm_adds_pi16(uu_pi16, rounding4_pi16);
				vv_pi16 = _mm_adds_pi16(vv_pi16, rounding3_pi16);


				uu_pi16 = _mm_srli_pi16(uu_pi16, 6);
				vv_pi16 = _mm_srli_pi16(vv_pi16, 6);

				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);



				// Convert to RGB
				temp = _mm_set1_pi16(mmx_y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);

				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R1 = _mm_adds_pi16(y_pi16, temp);
				R1 = _mm_adds_pi16(R1, rounding_pi16);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_adds_pi16(G1, rounding_pi16);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B1 = _mm_adds_pi16(y_pi16, temp);
				B1 = _mm_adds_pi16(B1, rounding_pi16);
				B1 = _mm_srai_pi16(B1, 6);


				// Unpack the second group of four bytes of luma
				//y_pi16 = _mm_unpackhi_pi8(y_pi8, _mm_setzero_si64());
				y_pi16 = *((__m64 *)&y_row_ptr[column + 4]);
				y_pi16 = _mm_srli_pi16(y_pi16, 1);


				// Duplicate the second two chroma values
				uu_pi16 = _mm_unpackhi_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpackhi_pi16(v_pi16, v_pi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					extracted_u = u_row_ptr[chroma_column+4]>>3;
					extracted_v = v_row_ptr[chroma_column+4]>>3;

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif

				uu_pi16 = _mm_adds_pi16(uu_pi16, rounding_pi16);
				vv_pi16 = _mm_adds_pi16(vv_pi16, rounding2_pi16);

				uu_pi16 = _mm_srli_pi16(uu_pi16, 6);
				vv_pi16 = _mm_srli_pi16(vv_pi16, 6);

				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);



				// Convert to RGB
				temp = _mm_set1_pi16(mmx_y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);

				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R2 = _mm_adds_pi16(y_pi16, temp);
				R2 = _mm_adds_pi16(R2, rounding2_pi16);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_adds_pi16(G2, rounding2_pi16);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B2 = _mm_adds_pi16(y_pi16, temp);
				B2 = _mm_adds_pi16(B2, rounding2_pi16);
				B2 = _mm_srai_pi16(B2, 6);


				/**** Pack and store the first set of eight RGB tuples (24 bytes) *****/

				// Pack the RGB tuples
				R_pi8 = _mm_packs_pu16(B1, B2); // swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); // swapped with B

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with zero
				BZ = _mm_unpacklo_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the first two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Get the first RGB tuple with zeros in the rest of the word
				RGB = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());

				// Get the second RGB tuple with zeros in the rest of the word
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 3*8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have two RGB tuples with two zero bytes in the rest of the word

				// Interleave the second two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Insert the first two red and green values into the output word
				RGB = _mm_or_si64(RGB, _mm_slli_si64(RGBZ, 6*8));

				// Store the first group of eight bytes of RGB values
				*(output_ptr++) = RGB;

				// Shift the blue value into position
				RGB = _mm_slli_si64(RGBZ, 5*8);
				RGB = _mm_srli_si64(RGB, 7*8);

				// Shift the second RGB tuple into position
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have four output bytes in the lower half of the word

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with zero
				BZ = _mm_unpackhi_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the third two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Insert the first RGB tuple into the output word
				RGB = _mm_unpacklo_pi32(RGB, RGBZ);

				// Shift the red value from the second RGB tuple into position
				temp = _mm_srli_si64(RGBZ, 4*8);
				temp = _mm_slli_si64(temp, 7*8);

				// Insert the red value into the upper byte of the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the second group of eight bytes of RGB values
				*(output_ptr++) = RGB;

				// Shift the green and blue values into position
				RGB = _mm_srli_si64(RGBZ, 5*8);

				// Interleave the fourth two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Shift the next RGB tuple into position
				temp = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 2*8);

				// Insert the RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Shift the last RGB tuple into position
				temp = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 5*8);

				// Insert the last RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the third group of eight bytes of RGB values
				*(output_ptr++) = RGB;


				/***** Process the second eight bytes of luma *****/


				y_pi16 = *((__m64 *)&y_row_ptr[column + 8]);

				u_pi16 = *((__m64 *)&u_row_ptr[chroma_column + 4]);

				v_pi16 = *((__m64 *)&v_row_ptr[chroma_column + 4]);

				y_pi16 = _mm_srli_pi16(y_pi16, 1);
				u_pi16 = _mm_srli_pi16(u_pi16, 2);
				v_pi16 = _mm_srli_pi16(v_pi16, 2);

				uu_pi16 = _mm_unpacklo_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpacklo_pi16(v_pi16, v_pi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					extracted_u = u_row_ptr[chroma_column+6]>>3;
					extracted_v = v_row_ptr[chroma_column+6]>>3;

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif
				uu_pi16 = _mm_adds_pi16(uu_pi16, rounding4_pi16);
				vv_pi16 = _mm_adds_pi16(vv_pi16, rounding3_pi16);

				uu_pi16 = _mm_srli_pi16(uu_pi16, 6);
				vv_pi16 = _mm_srli_pi16(vv_pi16, 6);

				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);



				// Convert to RGB
				temp = _mm_set1_pi16(mmx_y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);

				temp = _mm_set1_pi16(ymult);			// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R1 = _mm_adds_pi16(y_pi16, temp);
				R1 = _mm_adds_pi16(R1, rounding3_pi16);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_adds_pi16(G1, rounding3_pi16);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B1 = _mm_adds_pi16(y_pi16, temp);
				B1 = _mm_adds_pi16(B1, rounding3_pi16);
				B1 = _mm_srai_pi16(B1, 6);


				// Unpack the fourth group of four bytes of luma
				//y_pi16 = _mm_unpackhi_pi8(y_pi8, _mm_setzero_si64());
				y_pi16 = *((__m64 *)&y_row_ptr[column + 12]);
				y_pi16 = _mm_srli_pi16(y_pi16, 1);


				// Duplicate the fourth two chroma values
				uu_pi16 = _mm_unpackhi_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpackhi_pi16(v_pi16, v_pi16);
#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					if(column < width-2)
					{
						extracted_u = u_row_ptr[chroma_column+8]>>3;
						extracted_v = v_row_ptr[chroma_column+8]>>3;
					}
					else
					{
						extracted_u = u_row_ptr[chroma_column+7]>>3;
						extracted_v = v_row_ptr[chroma_column+7]>>3;
					}
					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif

				uu_pi16 = _mm_adds_pi16(uu_pi16, rounding_pi16);
				vv_pi16 = _mm_adds_pi16(vv_pi16, rounding2_pi16);

				uu_pi16 = _mm_srli_pi16(uu_pi16, 6);
				vv_pi16 = _mm_srli_pi16(vv_pi16, 6);

				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);



				// Convert to RGB
				temp = _mm_set1_pi16(mmx_y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);

				temp = _mm_set1_pi16(ymult);			// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R2 = _mm_adds_pi16(y_pi16, temp);
				R2 = _mm_adds_pi16(R2, rounding4_pi16);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_adds_pi16(G2, rounding4_pi16);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B2 = _mm_adds_pi16(y_pi16, temp);
				B2 = _mm_adds_pi16(B2, rounding4_pi16);
				B2 = _mm_srai_pi16(B2, 6);


				/**** Pack and store the second set of eight RGB tuples (24 bytes) *****/

				// Pack the RGB tuples
				R_pi8 = _mm_packs_pu16(B1, B2); // swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); // swapped with B

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with zero
				BZ = _mm_unpacklo_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the first two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Get the first RGB tuple with zeros in the rest of the word
				RGB = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());

				// Get the second RGB tuple with zeros in the rest of the word
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 3*8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have two RGB tuples with two zero bytes in the rest of the word

				// Interleave the second two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Insert the first two red and green values into the output word
				RGB = _mm_or_si64(RGB, _mm_slli_si64(RGBZ, 6*8));

				// Store the fourth group of eight bytes of RGB values
				*(output_ptr++) = RGB;

				// Shift the blue value into position
				RGB = _mm_slli_si64(RGBZ, 5*8);
				RGB = _mm_srli_si64(RGB, 7*8);

				// Shift the second RGB tuple into position
				RGBZ = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				RGBZ = _mm_slli_si64(RGBZ, 8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have four output bytes in the lower half of the word

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with zero
				BZ = _mm_unpackhi_pi8(B_pi8, _mm_setzero_si64());

				// Interleave the third two RGBZ tuples
				RGBZ = _mm_unpacklo_pi16(RG, BZ);

				// Insert the first RGB tuple into the output word
				RGB = _mm_unpacklo_pi32(RGB, RGBZ);

				// Shift the red value from the second RGB tuple into position
				temp = _mm_srli_si64(RGBZ, 4*8);
				temp = _mm_slli_si64(temp, 7*8);

				// Insert the red value into the upper byte of the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the fifth group of eight bytes of RGB values
				*(output_ptr++) = RGB;

				// Shift the green and blue values into position
				RGB = _mm_srli_si64(RGBZ, 5*8);

				// Interleave the fourth two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Shift the next RGB tuple into position
				temp = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 2*8);

				// Insert the RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Shift the last RGB tuple into position
				temp = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 5*8);

				// Insert the last RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the sixth group of eight bytes of RGB values
				*(output_ptr++) = RGB;
			}

			//_mm_empty();

			// Should have exited the loop at the post processing column
			assert(column == post_column);

#endif

			// Process the rest of the row
			for (; column < width; column += 2)
			{
				int Y1,Y2,U,V;
				int dither;
				// Convert to RGB
				int output_column = 3 * column;
				int R, G, B;

				if(saturate)
				{
					Y1 = SATURATE_Y(y_row_ptr[column]);
					U = SATURATE_Cr(u_row_ptr[column/2]);
					Y2 = SATURATE_Y(y_row_ptr[column + 1]);
					V = SATURATE_Cb(v_row_ptr[column/2]);
				}
				else
				{
					Y1 = (y_row_ptr[column]);
					U = (u_row_ptr[column/2]);
					Y2 = (y_row_ptr[column + 1]);
					V = (v_row_ptr[column/2]);
				}


				// Convert the first set of YCbCr values

				Y1 = Y1 - (y_offset<<8);
				Y2 = Y2 - (y_offset<<8);
				U = U - 32768;
				V = V - 32768;

				Y1 = Y1 * ymult >> 7;
				Y2 = Y2 * ymult >> 7;

				dither = rand() & 0x7fff;

				R = (Y1           + r_vmult * V + dither) >> 15;
				G = (Y1 -  g_umult * (U>>1) - g_vmult * (V>>1) + dither) >> 15;
				B = (Y1 + 2 * b_umult * U + dither) >> 15;

			//	R = (int)(1.164*((double)Y1) + 1.596*((double)V) + 0.5);
			//	G = (int)(1.164*((double)Y1) - 0.813*((double)V) - 0.391*((double)U) + 0.5);
			//	B = (int)(1.164*((double)Y1) + 2.018*((double)U) + 0.5);

				output_row_ptr[output_column + 0] = SATURATE_8U(B);
				output_row_ptr[output_column + 1] = SATURATE_8U(G);
				output_row_ptr[output_column + 2] = SATURATE_8U(R);

				// Convert the second set of YCbCr values
				dither = rand() & 0x7fff;

				R = (Y2           + r_vmult * V + dither) >> 15;
				G = (Y2 -  g_umult * (U>>1) - g_vmult * (V>>1) + dither) >> 15;
				B = (Y2 + 2 * b_umult * U + dither) >> 15;

			//	R = (int)(1.164*((double)Y2) + 1.596*((double)V) + 0.5);
			//	G = (int)(1.164*((double)Y2) - 0.813*((double)V) - 0.391*((double)U) + 0.5);
			//	B = (int)(1.164*((double)Y2) + 2.018*((double)U) + 0.5);

				output_row_ptr[output_column + 3] = SATURATE_8U(B);
				output_row_ptr[output_column + 4] = SATURATE_8U(G);
				output_row_ptr[output_column + 5] = SATURATE_8U(R);
			}

			// Should have exited the loop just after the last column
			assert(column == width);

			// Advance to the next rows in the input and output arrays
			y_row_ptr += y_pitch;

			u_row_ptr += u_pitch;

			v_row_ptr += v_pitch;
			output_row_ptr += output_pitch;
		}
	}
	else	// The output color format must be RGB32
	{
		int row;

		assert(format == COLOR_FORMAT_RGB32);

		for (row = 0; row < height; row++)
		{
			int column = 0;


#if (0 && XMMOPT) //DANREMOVED

			__m64 *output_ptr = (__m64 *)output_row_ptr;

			// Load 16 bytes of luma and 8 bytes of each chroma channel
			// to compute and store 3 times 16 = 48 bytes of RGB tuples.
			__m64 rounding_pi16 = _mm_set1_pi16(32); // for 6bit matm
			__m64 rounding2_pi16 = _mm_set1_pi16(32); // for 6bit matm
			__m64 rounding3_pi16 = _mm_set1_pi16(32); // for 6bit matm
			__m64 rounding4_pi16 = _mm_set1_pi16(32); // for 6bit matm

			rounding_pi16 = _mm_insert_pi16(rounding_pi16, rand()&63, 0);
			rounding_pi16 = _mm_insert_pi16(rounding_pi16, rand()&63, 1);
			rounding_pi16 = _mm_insert_pi16(rounding_pi16, rand()&63, 2);
			rounding_pi16 = _mm_insert_pi16(rounding_pi16, rand()&63, 3);

			rounding2_pi16 = _mm_insert_pi16(rounding2_pi16, rand()&63, 0);
			rounding2_pi16 = _mm_insert_pi16(rounding2_pi16, rand()&63, 1);
			rounding2_pi16 = _mm_insert_pi16(rounding2_pi16, rand()&63, 2);
			rounding2_pi16 = _mm_insert_pi16(rounding2_pi16, rand()&63, 3);

			rounding3_pi16 = _mm_insert_pi16(rounding3_pi16, rand()&63, 0);
			rounding3_pi16 = _mm_insert_pi16(rounding3_pi16, rand()&63, 1);
			rounding3_pi16 = _mm_insert_pi16(rounding3_pi16, rand()&63, 2);
			rounding3_pi16 = _mm_insert_pi16(rounding3_pi16, rand()&63, 3);

			rounding4_pi16 = _mm_insert_pi16(rounding4_pi16, rand()&63, 0);
			rounding4_pi16 = _mm_insert_pi16(rounding4_pi16, rand()&63, 1);
			rounding4_pi16 = _mm_insert_pi16(rounding4_pi16, rand()&63, 2);
			rounding4_pi16 = _mm_insert_pi16(rounding4_pi16, rand()&63, 3);

			// Load 16 bytes of luma and 8 bytes of each chroma channel
			// to compute and store 3 times 16 = 48 bytes of RGB tuples.

			for (; column < post_column; column += column_step)
			{
				__m64 y_pi8;		// Eight unsigned bytes of color values
				__m64 u_pi8;
				__m64 v_pi8;

				__m64 y_pi16;		// Four unpacked color values
				__m64 u_pi16;
				__m64 v_pi16;

				__m64 uu_pi16;		// Duplicated chroma values
				__m64 vv_pi16;

				__m64 R1, G1, B1;
				__m64 R2, G2, B2;
				__m64 R_pi8, G_pi8, B_pi8;
				__m64 temp;

				__m64 RG;
				__m64 BA;
				__m64 RGBA;

				// Adjust the column for YUV 4:2:2 frame format
				int chroma_column = column/2;


				/***** Process the first eight bytes of luma *****/

					// Load the first eight bytes of luma
				y_pi16 = *((__m64 *)&y_row_ptr[column]);

				// Load eight bytes of chroma (u channel)
				u_pi16 = *((__m64 *)&u_row_ptr[chroma_column]);

				// Load eight bytes of chroma (v channel)
				v_pi16 = *((__m64 *)&v_row_ptr[chroma_column]);

				y_pi16 = _mm_srli_pi16(y_pi16, 1);
				u_pi16 = _mm_srli_pi16(u_pi16, 2);
				v_pi16 = _mm_srli_pi16(v_pi16, 2);

				uu_pi16 = _mm_unpacklo_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpacklo_pi16(v_pi16, v_pi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					extracted_u = u_row_ptr[chroma_column+2]>>3;
					extracted_v = v_row_ptr[chroma_column+2]>>3;

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif

				uu_pi16 = _mm_adds_pi16(uu_pi16, rounding4_pi16);
				vv_pi16 = _mm_adds_pi16(vv_pi16, rounding3_pi16);

				uu_pi16 = _mm_srli_pi16(uu_pi16, 6);
				vv_pi16 = _mm_srli_pi16(vv_pi16, 6);

				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);

				// Convert to RGB
				temp = _mm_set1_pi16(mmx_y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);

				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R1 = _mm_adds_pi16(y_pi16, temp);
	//			R1 = _mm_adds_pi16(y_pi16, temp);
				R1 = _mm_adds_pi16(R1, rounding_pi16);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_adds_pi16(G1, rounding_pi16);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B1 = _mm_adds_pi16(y_pi16, temp);
				B1 = _mm_adds_pi16(B1, rounding_pi16);
				B1 = _mm_srai_pi16(B1, 6);


				// Unpack the second group of four bytes of luma
				y_pi16 = *((__m64 *)&y_row_ptr[column + 4]);
				y_pi16 = _mm_srli_pi16(y_pi16, 1);


				// Duplicate the second two chroma values
				uu_pi16 = _mm_unpackhi_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpackhi_pi16(v_pi16, v_pi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					extracted_u = u_row_ptr[chroma_column+4]>>3;
					extracted_v = v_row_ptr[chroma_column+4]>>3;

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif
				uu_pi16 = _mm_adds_pi16(uu_pi16, rounding_pi16);
				vv_pi16 = _mm_adds_pi16(vv_pi16, rounding2_pi16);

				uu_pi16 = _mm_srli_pi16(uu_pi16, 6);
				vv_pi16 = _mm_srli_pi16(vv_pi16, 6);

				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);


				// Convert to RGB
				temp = _mm_set1_pi16(mmx_y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);

				temp = _mm_set1_pi16(ymult);		// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R2 = _mm_adds_pi16(y_pi16, temp);
				R2 = _mm_adds_pi16(R2, rounding2_pi16);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_adds_pi16(G2, rounding2_pi16);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B2 = _mm_adds_pi16(y_pi16, temp);
				B2 = _mm_adds_pi16(B2, rounding2_pi16);
				B2 = _mm_srai_pi16(B2, 6);


				/**** Pack and store the first set of eight RGBA tuples (32 bytes) *****/

				// Pack the RGB values
				R_pi8 = _mm_packs_pu16(B1, B2); //swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); //swapped with B

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with the default alpha value
				BA = _mm_unpacklo_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the first two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);

				// Store the first group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;

				// Interleave the second two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);

				// Store the second group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with the default alpha value
				BA = _mm_unpackhi_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the third two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);

				// Store the third group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;

				// Interleave the fourth two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);

				// Store the fourth group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;


				/***** Process the second eight bytes of luma *****/

				y_pi16 = *((__m64 *)&y_row_ptr[column + 8]);

				u_pi16 = *((__m64 *)&u_row_ptr[chroma_column + 4]);

				v_pi16 = *((__m64 *)&v_row_ptr[chroma_column + 4]);

				y_pi16 = _mm_srli_pi16(y_pi16, 1);
				u_pi16 = _mm_srli_pi16(u_pi16, 2);
				v_pi16 = _mm_srli_pi16(v_pi16, 2);

				uu_pi16 = _mm_unpacklo_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpacklo_pi16(v_pi16, v_pi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					extracted_u = u_row_ptr[chroma_column+6]>>3;
					extracted_v = v_row_ptr[chroma_column+6]>>3;

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif
				uu_pi16 = _mm_adds_pi16(uu_pi16, rounding4_pi16);
				vv_pi16 = _mm_adds_pi16(vv_pi16, rounding3_pi16);

				uu_pi16 = _mm_srli_pi16(uu_pi16, 6);
				vv_pi16 = _mm_srli_pi16(vv_pi16, 6);

				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);



				// Convert to RGB
				temp = _mm_set1_pi16(mmx_y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);

				temp = _mm_set1_pi16(ymult);			// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R1 = _mm_adds_pi16(y_pi16, temp);
				R1 = _mm_adds_pi16(R1, rounding3_pi16);
				R1 = _mm_srai_pi16(R1, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_adds_pi16(G1, rounding3_pi16);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B1 = _mm_adds_pi16(y_pi16, temp);
				B1 = _mm_adds_pi16(B1, rounding3_pi16);
				B1 = _mm_srai_pi16(B1, 6);


				// Unpack the fourth group of four bytes of luma
				//y_pi16 = _mm_unpackhi_pi8(y_pi8, _mm_setzero_si64());
				y_pi16 = *((__m64 *)&y_row_ptr[column + 12]);
				y_pi16 = _mm_srli_pi16(y_pi16, 1);


				// Duplicate the fourth two chroma values
				uu_pi16 = _mm_unpackhi_pi16(u_pi16, u_pi16);
				vv_pi16 = _mm_unpackhi_pi16(v_pi16, v_pi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m64 uua_pi16;
					__m64 vva_pi16;
					int extracted_u;
					int extracted_v;

					if(column < width-2)
					{
						extracted_u = u_row_ptr[chroma_column+8]>>3;
						extracted_v = v_row_ptr[chroma_column+8]>>3;
					}
					else
					{
						extracted_u = u_row_ptr[chroma_column+7]>>3;
						extracted_v = v_row_ptr[chroma_column+7]>>3;
					}

					uu_pi16 = _mm_srli_pi16(uu_pi16, 1);
					vv_pi16 = _mm_srli_pi16(vv_pi16, 1);

					uua_pi16 = _mm_shuffle_pi16(uu_pi16, _MM_SHUFFLE(3, 3, 2, 1));
					vva_pi16 = _mm_shuffle_pi16(vv_pi16, _MM_SHUFFLE(3, 3, 2, 1));

					uua_pi16 = _mm_insert_pi16(uua_pi16, extracted_u, 3);
					vva_pi16 = _mm_insert_pi16(vva_pi16, extracted_v, 3);

					uu_pi16 = _mm_adds_pu16(uu_pi16, uua_pi16);
					vv_pi16 = _mm_adds_pu16(vv_pi16, vva_pi16);
				}
#endif
				uu_pi16 = _mm_adds_pi16(uu_pi16, rounding_pi16);
				vv_pi16 = _mm_adds_pi16(vv_pi16, rounding2_pi16);

				uu_pi16 = _mm_srli_pi16(uu_pi16, 6);
				vv_pi16 = _mm_srli_pi16(vv_pi16, 6);

				temp = _mm_set1_pi16(128);
				uu_pi16 = _mm_subs_pi16(uu_pi16, temp);
				vv_pi16 = _mm_subs_pi16(vv_pi16, temp);



				// Convert to RGB
				temp = _mm_set1_pi16(mmx_y_offset);
				y_pi16 = _mm_subs_pi16(y_pi16, temp);

				temp = _mm_set1_pi16(ymult);			// with some color produced interim values larger than 32768
				y_pi16 = _mm_mulhi_pi16(y_pi16, temp);
				y_pi16 = _mm_slli_pi16(y_pi16, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
				R2 = _mm_adds_pi16(y_pi16, temp);
				R2 = _mm_adds_pi16(R2, rounding4_pi16);
				R2 = _mm_srai_pi16(R2, 6);

				// Calculate G
				temp = _mm_set1_pi16(g_vmult);
				temp = _mm_mullo_pi16(vv_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(y_pi16, temp);
				temp = _mm_set1_pi16(g_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				temp = _mm_srai_pi16(temp, 2); //8bit to 6
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_adds_pi16(G2, rounding4_pi16);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(uu_pi16, temp);
				B2 = _mm_adds_pi16(y_pi16, temp);
				B2 = _mm_adds_pi16(B2, rounding4_pi16);
				B2 = _mm_srai_pi16(B2, 6);


				/**** Pack and store the second set of eight RGB tuples (32 bytes) *****/

				// Pack the RGB values
				R_pi8 = _mm_packs_pu16(B1, B2); //swapped with R
				G_pi8 = _mm_packs_pu16(G1, G2);
				B_pi8 = _mm_packs_pu16(R1, R2); //swapped with B

				// Interleave the first four red and green values
				RG = _mm_unpacklo_pi8(R_pi8, G_pi8);

				// Interleave the first four blue values with the default alpha value
				BA = _mm_unpacklo_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the first two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);

				// Store the first group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;

				// Interleave the second two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);

				// Store the second group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;

				// Interleave the second four red and green values
				RG = _mm_unpackhi_pi8(R_pi8, G_pi8);

				// Interleave the second four blue values with the default alpha value
				BA = _mm_unpackhi_pi8(B_pi8, _mm_set1_pi8(RGBA_DEFAULT_ALPHA));

				// Interleave the third two RGBA tuples
				RGBA = _mm_unpacklo_pi16(RG, BA);

				// Store the third group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;

				// Interleave the fourth two RGBA tuples
				RGBA = _mm_unpackhi_pi16(RG, BA);

				// Store the fourth group of eight bytes of RGBA values
				*(output_ptr++) = RGBA;
			}

			//_mm_empty();

			// Should have exited the loop at the post processing column
			assert(column == post_column);

#endif

			// Process the rest of the row
			for (; column < width; column += 2)
			{
				// Convert to RGB
				int output_column = 4 * column;
				int R, G, B;
				int Y1,Y2,U,V;

				if(saturate)
				{
					Y1 = SATURATE_Y(y_row_ptr[column]);
					U = SATURATE_Cr(u_row_ptr[column/2]);
					Y2 = SATURATE_Y(y_row_ptr[column + 1]);
					V = SATURATE_Cb(v_row_ptr[column/2]);
				}
				else
				{
					Y1 = (y_row_ptr[column]);
					U = (u_row_ptr[column/2]);
					Y2 = (y_row_ptr[column + 1]);
					V = (v_row_ptr[column/2]);
				}


				Y1 = Y1 - y_offset;
				Y2 = Y2 - y_offset;
				U = U - 128;
				V = V - 128;

				Y1 = Y1 * ymult >> 7;
				Y2 = Y2 * ymult >> 7;

				R = (Y1           + r_vmult * V + 64) >> 7;
				G = (Y1*2 -  g_umult * U - g_vmult * V + 128) >> 8;
				B = (Y1 + 2 * b_umult * U + 64) >> 7;

				output_row_ptr[output_column + 0] = SATURATE_8U(B);
				output_row_ptr[output_column + 1] = SATURATE_8U(G);
				output_row_ptr[output_column + 2] = SATURATE_8U(R);
				output_row_ptr[output_column + 3] = RGBA_DEFAULT_ALPHA;

				// Convert the second set of YCbCr values
				R = (Y2           + r_vmult * V + 64) >> 7;
				G = (Y2*2 -  g_umult * U - g_vmult * V + 128) >> 8;
				B = (Y2 + 2 * b_umult * U + 64) >> 7;

				output_row_ptr[output_column + 4] = SATURATE_8U(B);
				output_row_ptr[output_column + 5] = SATURATE_8U(G);
				output_row_ptr[output_column + 6] = SATURATE_8U(R);
				output_row_ptr[output_column + 7] = RGBA_DEFAULT_ALPHA;
			}

			// Should have exited the loop just after the last column
			assert(column == width);

			// Advance to the next rows in the input and output arrays
			y_row_ptr += y_pitch;
			u_row_ptr += u_pitch;
			v_row_ptr += v_pitch;
			output_row_ptr += output_pitch;
		}
	}
}

void ConvertCGRGBtoVSRGB(PIXEL *sptr, int width, int whitebitdepth, int flags)
{
	int i=0;
	__m128i *src_ptr = (__m128i *)sptr;
	__m128i blk16,mult16,inp16;
	int endcol = width*3;
	endcol -= (endcol % 8);

	if(flags & ACTIVEMETADATA_COLORFORMATDONE) //YUV ouptut does need cgRGB to vsRGB processing
		return;

	if(whitebitdepth == 16)
	{
		PIXEL16U *usptr = (PIXEL16U *)sptr;		
		uint32_t black = (1<<16)>>4;
		uint32_t mult = 65535 * 219 / 255;

		mult16 = _mm_set1_epi16(mult);
		blk16 = _mm_set1_epi16(black);
		
		for(i=0;i<endcol;i+=8)
		{
			inp16 = _mm_load_si128(src_ptr);			
			inp16 = _mm_mulhi_epu16(inp16, mult16);
			inp16 = _mm_adds_epu16(inp16, blk16);
			_mm_store_si128(src_ptr++, inp16);
		}

		usptr = (PIXEL16U *)src_ptr;
		for(;i<width*3;i++)
		{
			int val = *usptr;
			val *= 219;
			val /= 255;
			val += black;
			*usptr++ = val;
		}
	}
	else
	{
		uint32_t black = (1<<whitebitdepth)>>4;	
		uint32_t mult = 32767 * 219 / 255;

		mult16 = _mm_set1_epi16(mult);
		blk16 = _mm_set1_epi16(black);
		
		for(i=0;i<endcol;i+=8)
		{
			inp16 = _mm_load_si128(src_ptr);			
			inp16 = _mm_mulhi_epi16(inp16, mult16);
			inp16 = _mm_slli_epi16(inp16, 1);
			inp16 = _mm_adds_epi16(inp16, blk16);
			_mm_store_si128(src_ptr++, inp16);
		}

		sptr = (PIXEL *)src_ptr;
		for(;i<width*3;i++)
		{
			int val = *sptr;
			val *= 219;
			val /= 255;
			val += black;
			*sptr++ = val;
		}
	}
}



void ConvertCGRGBAtoVSRGBA(PIXEL *sptr, int width, int whitebitdepth, int flags)
{
	int i=0;
	int endcol = width*3;
	endcol -= (endcol % 8);

	if(flags & ACTIVEMETADATA_COLORFORMATDONE) //YUV ouptut does need cgRGB to vsRGB processing
		return;

	if(whitebitdepth == 16)
	{
		PIXEL16U *usptr = (PIXEL16U *)sptr;		
		uint32_t black = (1<<16)>>4;

		for(i=0;i<width;i++)
		{
			int r,g,b; 
			r = usptr[0];
			r *= 219;
			r /= 255;
			r += black;
			usptr[0] = r;

			g = usptr[1];
			g *= 219;
			g /= 255;
			g += black;
			usptr[1] = g;

			b = usptr[2];
			b *= 219;
			b /= 255;
			b += black;
			usptr[2] = b;

			usptr+=4;
		}
	}
	else
	{
		uint32_t black = (1<<whitebitdepth)>>4;	
		
		for(i=0;i<width;i++)
		{
			int r,g,b; 

			r = sptr[0];
			r *= 219;
			r /= 255;
			r += black;
			sptr[0] = r;

			g = sptr[1];
			g *= 219;
			g /= 255;
			g += black;
			sptr[1] = g;

			b = sptr[2];
			b *= 219;
			b /= 255;
			b += black;
			sptr[2] = b;

			sptr+=4;
		}
	}
}

void ConvertYUVRow16uToBGRA64(uint8_t *planar_output[], int planar_pitch[], ROI roi,
						   uint8_t *output_buffer, int output_width, int output_pitch,
						   int format, int colorspace, int *whitebitdepth, int *ret_flags)
{
	int width = roi.width;
	int height = roi.height;

	// Note that this routine is called with the YUV channels in our own
	// internal order so the chroma values have not already been reversed255

	PIXEL16U *y_row_ptr = (PIXEL16U *)planar_output[0];
	PIXEL16U *u_row_ptr = (PIXEL16U *)planar_output[2];		// Reverse the chroma order
	PIXEL16U *v_row_ptr = (PIXEL16U *)planar_output[1];

	int y_pitch = planar_pitch[0]/sizeof(PIXEL16U);
	int u_pitch = planar_pitch[2]/sizeof(PIXEL16U);			// Reverse the chroma order
	int v_pitch = planar_pitch[1]/sizeof(PIXEL16U);

	uint8_t *output_row_ptr = output_buffer;

	// Definitions for optimization
	const int column_step = 16;

	// Column at which post processing must begin
	int post_column = width - (width % column_step);

	//CG601
	int y_offset = 16; // not VIDEO_RGB & not YUV709
	int i_mathprecision = (1<<13);
	float mathprecision;
	int ymult;
	int r_vmult;
	int g_vmult;
	int g_umult;
	int b_umult;
	int saturate;
	int mmx_y_offset;
	int upconvert422to444 = 0;
	int inverted = 0;
	
	mathprecision = (float)i_mathprecision;

	if(ret_flags)
		*ret_flags = 0;

	if(format == COLOR_FORMAT_RGB_8PIXEL_PLANAR || format == COLOR_FORMAT_WP13)
	{
		if(ret_flags && format == COLOR_FORMAT_RGB_8PIXEL_PLANAR)
			*ret_flags |= ACTIVEMETADATA_SRC_8PIXEL_PLANAR;
		//colorspace |= COLOR_SPACE_422_TO_444; //DAN20090601

		saturate = 0;
		if(whitebitdepth)
			*whitebitdepth = 13;

	}
	else
	{
		saturate = 1;
		if(whitebitdepth)
			*whitebitdepth = 16;
	}

	if(colorspace & COLOR_SPACE_422_TO_444)
	{
		upconvert422to444 = 1;
	}

	switch(colorspace & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:
		y_offset = 16;
		ymult =   (int)(mathprecision*1.164f);
		r_vmult = (int)(mathprecision*1.596f);
		g_vmult = (int)(mathprecision*0.813f);
		g_umult = (int)(mathprecision*0.391f);
		b_umult = (int)(mathprecision*2.018f);
		break;

	default:// assert(0);
	case COLOR_SPACE_CG_709:
		y_offset = 16;
		ymult =   (int)(mathprecision*1.164f);
		r_vmult = (int)(mathprecision*1.793f);
		g_vmult = (int)(mathprecision*0.534f);
		g_umult = (int)(mathprecision*0.213f);
		b_umult = (int)(mathprecision*2.115f);
		break;

	case COLOR_SPACE_VS_601:
		y_offset = 0;
		ymult =   (int)(mathprecision*1.000f);
		r_vmult = (int)(mathprecision*1.371f);
		g_vmult = (int)(mathprecision*0.698f);
		g_umult = (int)(mathprecision*0.336f);
		b_umult = (int)(mathprecision*1.732f);
		break;

	case COLOR_SPACE_VS_709:
		y_offset = 0;
		ymult =   (int)(mathprecision*1.000f);
		r_vmult = (int)(mathprecision*1.540f);
		g_vmult = (int)(mathprecision*0.459f);
		g_umult = (int)(mathprecision*0.183f);
		b_umult = (int)(mathprecision*1.816f);
		break;
	}


	if(ret_flags && saturate)
		*ret_flags |= ACTIVEMETADATA_PRESATURATED;


	y_offset <<= 7;
	mmx_y_offset = (y_offset);


	// Should the image be inverted?
	if (inverted && output_pitch > 0) {
		output_row_ptr += (height - 1) * output_pitch;	// Start at the bottom row
		output_pitch = (- output_pitch);				// Negate the pitch to go up
	}

	{
		int row;

		for (row = 0; row < height; row++)
		{
			int column = 0;

#if (1 && XMMOPT)

			__m128i *output_ptr = (__m128i *)output_row_ptr;
			__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x3fff);
			__m128i AA = _mm_set1_epi16(0xffff);
			__m128i ZERO = _mm_set1_epi16(0);

			// Load 16 bytes of luma and 8 bytes of each chroma channel
			// to compute and store 3 times 16 = 48 bytes of RGB tuples.

			for (; column < post_column; column += column_step)
			{
				__m128i y_epi16;		// Four unpacked color values
				__m128i u_epi16;
				__m128i v_epi16;

				__m128i uu_epi16;		// Duplicated chroma values
				__m128i vv_epi16;

				__m128i R1, G1, B1;
				__m128i temp;

				__m128i BG;
				__m128i RA;
				__m128i RGBA;

				// Adjust the column for YUV 4:2:2 frame format
				int chroma_column = column>>1;


				/***** Process the first eight bytes of luma *****/

				// Load the first eight bytes of luma
				y_epi16 = _mm_loadu_si128((__m128i *)&y_row_ptr[column]);

				// Load eight bytes of chroma (u channel)
				u_epi16 = _mm_loadu_si128((__m128i *)&u_row_ptr[chroma_column]);

				// Load eight bytes of chroma (v channel)
				v_epi16 = _mm_loadu_si128((__m128i *)&v_row_ptr[chroma_column]);

				y_epi16 = _mm_srli_epi16(y_epi16, 1); //15-bit
				u_epi16 = _mm_srli_epi16(u_epi16, 1); //15-bit
				v_epi16 = _mm_srli_epi16(v_epi16, 1); //15-bit

				uu_epi16 = _mm_unpacklo_epi16(u_epi16, u_epi16);
				vv_epi16 = _mm_unpacklo_epi16(v_epi16, v_epi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m128i uua_epi16;
					__m128i vva_epi16;
					int next = chroma_column+4;
					//if(next >= (output_width>>1))
					//	next = (output_width>>1)-2;

					uu_epi16 = _mm_srli_epi16(uu_epi16, 1); //14-bit
					vv_epi16 = _mm_srli_epi16(vv_epi16, 1); //14-bit

					uua_epi16 = _mm_srli_si128(uu_epi16, 2);
					uua_epi16 = _mm_insert_epi16(uua_epi16, u_row_ptr[next]>>2, 7);

					vva_epi16 = _mm_srli_si128(vv_epi16, 2);
					vva_epi16 = _mm_insert_epi16(vva_epi16, v_row_ptr[next]>>2, 7);

					uu_epi16 = _mm_adds_epu16(uu_epi16, uua_epi16); //15-bit
					vv_epi16 = _mm_adds_epu16(vv_epi16, vva_epi16); //15-bit
				}
#endif

			//	uu_epi16 = _mm_srli_epi16(uu_epi16, 6);
			//	vv_epi16 = _mm_srli_epi16(vv_epi16, 6);
			//	temp = _mm_set1_epi16(128);

				temp = _mm_set1_epi16(16384); //15-bit mid point chroma
				uu_epi16 = _mm_subs_epi16(uu_epi16, temp); //15-bit
				vv_epi16 = _mm_subs_epi16(vv_epi16, temp); //15-bit

				// Convert to RGB
				temp = _mm_set1_epi16(mmx_y_offset); //15-bit offset
				y_epi16 = _mm_subs_epi16(y_epi16, temp); //15-bit

				temp = _mm_set1_epi16(ymult);		// 13-bit with some color produced interim values larger than 32768
				y_epi16 = _mm_mulhi_epi16(y_epi16, temp); // 15-bit * 13-bit = 12-bit
				y_epi16 = _mm_slli_epi16(y_epi16, 2);  //14-bit

				// Calculate R
				temp = _mm_set1_epi16(r_vmult);	//13-bit
				temp = _mm_mulhi_epi16(vv_epi16, temp); // 15-bit * 13-bit = 12-bit
				temp = _mm_slli_epi16(temp, 2); //14bit
				R1 = _mm_adds_epi16(y_epi16, temp);//14-bit
				if(saturate)
				{
					R1 = _mm_adds_epi16(R1, limiterRGB);
					R1 = _mm_subs_epu16(R1, limiterRGB);
					R1 = _mm_slli_epi16(R1, 2);//16-bit
				}
				else
				{
					R1 = _mm_srai_epi16(R1, 1);//13-bit signed
				}

				// Calculate G
				temp = _mm_set1_epi16(g_vmult);//13-bit
				temp = _mm_mulhi_epi16(vv_epi16, temp);// 15-bit * 13-bit = 12-bit
				temp = _mm_slli_epi16(temp, 2); //14-bit
				G1 = _mm_subs_epi16(y_epi16, temp);//14-bit
				temp = _mm_set1_epi16(g_umult);//13-bit
				temp = _mm_mulhi_epi16(uu_epi16, temp);// 15-bit * 13-bit = 12-bit
				temp = _mm_slli_epi16(temp, 2); //14-bit
				G1 = _mm_subs_epi16(G1, temp);//14-bit
				if(saturate)
				{
					G1 = _mm_adds_epi16(G1, limiterRGB);
					G1 = _mm_subs_epu16(G1, limiterRGB);
					G1 = _mm_slli_epi16(G1, 2);//16-bit
				}
				else
				{
					G1 = _mm_srai_epi16(G1, 1);//13-bit signed
				}

				// Calculate B
				temp = _mm_set1_epi16(b_umult);//13-bit
				temp = _mm_mulhi_epi16(uu_epi16, temp);// 15-bit * 13-bit = 12-bit
				temp = _mm_slli_epi16(temp, 2); //14-bit
				B1 = _mm_adds_epi16(y_epi16, temp);//14-bit
				if(saturate)
				{
					B1 = _mm_adds_epi16(B1, limiterRGB);
					B1 = _mm_subs_epu16(B1, limiterRGB);
					B1 = _mm_slli_epi16(B1, 2);//16-bit
				}
				else
				{
					B1 = _mm_srai_epi16(B1, 1);//13-bit signed
				}

				switch(format)
				{
				case COLOR_FORMAT_B64A: //BGRA64
					BG = _mm_unpacklo_epi16(AA,R1);
					RA = _mm_unpacklo_epi16(G1,B1);
					RGBA = _mm_unpacklo_epi32(BG, RA);
					_mm_storeu_si128(output_ptr++, RGBA);// Store the first group of eight bytes of RGBA values
					RGBA = _mm_unpackhi_epi32(BG, RA);
					_mm_storeu_si128(output_ptr++, RGBA);// Store the second group of eight bytes of RGBA values

					BG = _mm_unpackhi_epi16(AA,R1);
					RA = _mm_unpackhi_epi16(G1,B1);
					RGBA = _mm_unpacklo_epi32(BG, RA);
					_mm_storeu_si128(output_ptr++, RGBA);// Store the first group of eight bytes of RGBA values
					RGBA = _mm_unpackhi_epi32(BG, RA);
					_mm_storeu_si128(output_ptr++, RGBA);// Store the second group of eight bytes of RGBA values
					break;

				case COLOR_FORMAT_RG48: //RGB48
				case COLOR_FORMAT_WP13: //WP13
					{
						unsigned short *sptr = (unsigned short *)output_ptr;

						sptr[0] = _mm_extract_epi16(R1, 0);
						sptr[1] = _mm_extract_epi16(G1, 0);
						sptr[2] = _mm_extract_epi16(B1, 0);

						sptr[3] = _mm_extract_epi16(R1, 1);
						sptr[4] = _mm_extract_epi16(G1, 1);
						sptr[5] = _mm_extract_epi16(B1, 1);

						sptr[6] = _mm_extract_epi16(R1, 2);
						sptr[7] = _mm_extract_epi16(G1, 2);
						sptr[8] = _mm_extract_epi16(B1, 2);

						sptr[9] = _mm_extract_epi16(R1, 3);
						sptr[10] = _mm_extract_epi16(G1, 3);
						sptr[11] = _mm_extract_epi16(B1, 3);

						sptr[12] = _mm_extract_epi16(R1, 4);
						sptr[13] = _mm_extract_epi16(G1, 4);
						sptr[14] = _mm_extract_epi16(B1, 4);
							
						sptr[15] = _mm_extract_epi16(R1, 5);
						sptr[16] = _mm_extract_epi16(G1, 5);
						sptr[17] = _mm_extract_epi16(B1, 5);
							
						sptr[18] = _mm_extract_epi16(R1, 6);
						sptr[19] = _mm_extract_epi16(G1, 6);
						sptr[20] = _mm_extract_epi16(B1, 6);
							
						sptr[21] = _mm_extract_epi16(R1, 7);
						sptr[22] = _mm_extract_epi16(G1, 7);
						sptr[23] = _mm_extract_epi16(B1, 7);

						output_ptr++;
						output_ptr++;
						output_ptr++;
					}
					break;

				case COLOR_FORMAT_RGB_8PIXEL_PLANAR:
					_mm_storeu_si128(output_ptr++, R1);
					_mm_storeu_si128(output_ptr++, G1);
					_mm_storeu_si128(output_ptr++, B1);
					break;

				case COLOR_FORMAT_R210: //r210
					{
						__m128i RL,GL,BL;
						R1 = _mm_srli_epi16(R1, 6); //10bit
						G1 = _mm_srli_epi16(G1, 6);
						B1 = _mm_srli_epi16(B1, 6);

						RL = _mm_unpacklo_epi16(R1, ZERO);
						GL = _mm_unpacklo_epi16(G1, ZERO);
						BL = _mm_unpacklo_epi16(B1, ZERO);
						GL = _mm_slli_epi32(GL, 10);
						RL = _mm_slli_epi32(RL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);

						// the algorithm is:
						// 1) [A B C D] => [B A D C]
						// 2) [B A D C] => [D C B A]

						// do first swap
						RGBA = _mm_or_si128( _mm_slli_epi16( RGBA, 8 ),
												_mm_srli_epi16( RGBA, 8 ) ); //swap it
						// do second swap
						RGBA = _mm_or_si128( _mm_slli_epi32( RGBA, 16 ),
												_mm_srli_epi32( RGBA, 16 ) ); //swap it

						_mm_storeu_si128(output_ptr++, RGBA);// Store the second group of eight bytes of RGB values


						RL = _mm_unpackhi_epi16(R1, ZERO);
						GL = _mm_unpackhi_epi16(G1, ZERO);
						BL = _mm_unpackhi_epi16(B1, ZERO);
						GL = _mm_slli_epi32(GL, 10);
						RL = _mm_slli_epi32(RL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);

						// the algorithm is:
						// 1) [A B C D] => [B A D C]
						// 2) [B A D C] => [D C B A]

						// do first swap
						RGBA = _mm_or_si128( _mm_slli_epi16( RGBA, 8 ),
												_mm_srli_epi16( RGBA, 8 ) ); //swap it
						// do second swap
						RGBA = _mm_or_si128( _mm_slli_epi32( RGBA, 16 ),
												_mm_srli_epi32( RGBA, 16 ) ); //swap it

						_mm_storeu_si128(output_ptr++, RGBA);// Store the second group of eight bytes of RGB values
					}
					break;
				case COLOR_FORMAT_DPX0: //DPX0
					{
						__m128i RL,GL,BL;
						R1 = _mm_srli_epi16(R1, 6); //10bit
						G1 = _mm_srli_epi16(G1, 6);
						B1 = _mm_srli_epi16(B1, 6);

						RL = _mm_unpacklo_epi16(R1, ZERO);
						GL = _mm_unpacklo_epi16(G1, ZERO);
						BL = _mm_unpacklo_epi16(B1, ZERO);
						GL = _mm_slli_epi32(GL, 10);
						RL = _mm_slli_epi32(RL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);

						RGBA = _mm_slli_epi32(RGBA, 2);

						// the algorithm is:
						// 1) [A B C D] => [B A D C]
						// 2) [B A D C] => [D C B A]

						// do first swap
						RGBA = _mm_or_si128( _mm_slli_epi16( RGBA, 8 ),
												_mm_srli_epi16( RGBA, 8 ) ); //swap it
						// do second swap
						RGBA = _mm_or_si128( _mm_slli_epi32( RGBA, 16 ),
												_mm_srli_epi32( RGBA, 16 ) ); //swap it

						_mm_storeu_si128(output_ptr++, RGBA);// Store the second group of eight bytes of RGB values


						RL = _mm_unpackhi_epi16(R1, ZERO);
						GL = _mm_unpackhi_epi16(G1, ZERO);
						BL = _mm_unpackhi_epi16(B1, ZERO);
						GL = _mm_slli_epi32(GL, 10);
						RL = _mm_slli_epi32(RL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);

						RGBA = _mm_slli_epi32(RGBA, 2);

						// the algorithm is:
						// 1) [A B C D] => [B A D C]
						// 2) [B A D C] => [D C B A]

						// do first swap
						RGBA = _mm_or_si128( _mm_slli_epi16( RGBA, 8 ),
												_mm_srli_epi16( RGBA, 8 ) ); //swap it
						// do second swap
						RGBA = _mm_or_si128( _mm_slli_epi32( RGBA, 16 ),
												_mm_srli_epi32( RGBA, 16 ) ); //swap it

						_mm_storeu_si128(output_ptr++, RGBA);// Store the second group of eight bytes of RGB values
					}
					break;
				case COLOR_FORMAT_RG30: //RG30
				case COLOR_FORMAT_AB10: //AB10
					{
						__m128i RL,GL,BL;

						R1 = _mm_srli_epi16(R1, 6); //10bit
						G1 = _mm_srli_epi16(G1, 6);
						B1 = _mm_srli_epi16(B1, 6);

						RL = _mm_unpacklo_epi16(R1, ZERO);
						GL = _mm_unpacklo_epi16(G1, ZERO);
						BL = _mm_unpacklo_epi16(B1, ZERO);

						GL = _mm_slli_epi32(GL, 10);
						BL = _mm_slli_epi32(BL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);
						_mm_storeu_si128(output_ptr++, RGBA);// Store the second group of eight bytes of RGB values


						RL = _mm_unpackhi_epi16(R1, ZERO);
						GL = _mm_unpackhi_epi16(G1, ZERO);
						BL = _mm_unpackhi_epi16(B1, ZERO);

						GL = _mm_slli_epi32(GL, 10);
						BL = _mm_slli_epi32(BL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);
						_mm_storeu_si128(output_ptr++, RGBA);// Store the second group of eight bytes of RGB values

					}
					break;

				case COLOR_FORMAT_AR10: //AR10
					{
						__m128i RL,GL,BL;

						R1 = _mm_srli_epi16(R1, 6); //10bit
						G1 = _mm_srli_epi16(G1, 6);
						B1 = _mm_srli_epi16(B1, 6);

						RL = _mm_unpacklo_epi16(R1, ZERO);
						GL = _mm_unpacklo_epi16(G1, ZERO);
						BL = _mm_unpacklo_epi16(B1, ZERO);

						GL = _mm_slli_epi32(GL, 10);
						RL = _mm_slli_epi32(RL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);
						_mm_storeu_si128(output_ptr++, RGBA);// Store the second group of eight bytes of RGB values


						RL = _mm_unpackhi_epi16(R1, ZERO);
						GL = _mm_unpackhi_epi16(G1, ZERO);
						BL = _mm_unpackhi_epi16(B1, ZERO);

						GL = _mm_slli_epi32(GL, 10);
						RL = _mm_slli_epi32(RL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);
						_mm_storeu_si128(output_ptr++, RGBA);// Store the second group of eight bytes of RGB values

					}
					break;
				}

				// Unpack the second group of four bytes of luma
				y_epi16 = _mm_loadu_si128((__m128i *)&y_row_ptr[column + 8]);
				y_epi16 = _mm_srli_epi16(y_epi16, 1);


				// Duplicate the second two chroma values
				uu_epi16 = _mm_unpackhi_epi16(u_epi16, u_epi16);
				vv_epi16 = _mm_unpackhi_epi16(v_epi16, v_epi16);

#if CHROMA422to444
				if(upconvert422to444)
				{
					__m128i uua_epi16;
					__m128i vva_epi16;
					int extracted_u;
					int extracted_v;
					int next = chroma_column+8;
					if(next >= (output_width>>1))
						next = (output_width>>1)-2;

					extracted_u = u_row_ptr[next]>>2;
					extracted_v = v_row_ptr[next]>>2;



					uu_epi16 = _mm_srli_epi16(uu_epi16, 1); //14-bit
					vv_epi16 = _mm_srli_epi16(vv_epi16, 1); //14-bit

					uua_epi16 = _mm_srli_si128(uu_epi16, 2);
					uua_epi16 = _mm_insert_epi16(uua_epi16, extracted_u, 7);

					vva_epi16 = _mm_srli_si128(vv_epi16, 2);
					vva_epi16 = _mm_insert_epi16(vva_epi16, extracted_v, 7);

					uu_epi16 = _mm_adds_epu16(uu_epi16, uua_epi16); //15-bit
					vv_epi16 = _mm_adds_epu16(vv_epi16, vva_epi16); //15-bit
				}
#endif
			//	uu_epi16 = _mm_srli_epi16(uu_epi16, 6);
			//	vv_epi16 = _mm_srli_epi16(vv_epi16, 6);
			//	temp = _mm_set1_epi16(128);

				temp = _mm_set1_epi16(16384); //15-bit mid point chroma
				uu_epi16 = _mm_subs_epi16(uu_epi16, temp); //15-bit
				vv_epi16 = _mm_subs_epi16(vv_epi16, temp); //15-bit

				// Convert to RGB
				temp = _mm_set1_epi16(mmx_y_offset); //15-bit offset
				y_epi16 = _mm_subs_epi16(y_epi16, temp); //15-bit

				temp = _mm_set1_epi16(ymult);		// 13-bit with some color produced interim values larger than 32768
				y_epi16 = _mm_mulhi_epi16(y_epi16, temp); // 15-bit * 13-bit = 12-bit
				y_epi16 = _mm_slli_epi16(y_epi16, 2);  //14-bit

				// Calculate R
				temp = _mm_set1_epi16(r_vmult);	//13-bit
				temp = _mm_mulhi_epi16(vv_epi16, temp); // 15-bit * 13-bit = 12-bit
				temp = _mm_slli_epi16(temp, 2); //14bit
				R1 = _mm_adds_epi16(y_epi16, temp);//14-bit
				if(saturate)
				{
					R1 = _mm_adds_epi16(R1, limiterRGB);
					R1 = _mm_subs_epu16(R1, limiterRGB);
					R1 = _mm_slli_epi16(R1, 2);//16-bit
				}
				else
				{
					R1 = _mm_srai_epi16(R1, 1);//13-bit signed
				}

				// Calculate G
				temp = _mm_set1_epi16(g_vmult);//13-bit
				temp = _mm_mulhi_epi16(vv_epi16, temp);// 15-bit * 13-bit = 12-bit
				temp = _mm_slli_epi16(temp, 2); //14-bit
				G1 = _mm_subs_epi16(y_epi16, temp);//14-bit
				temp = _mm_set1_epi16(g_umult);//13-bit
				temp = _mm_mulhi_epi16(uu_epi16, temp);// 15-bit * 13-bit = 12-bit
				temp = _mm_slli_epi16(temp, 2); //14-bit
				G1 = _mm_subs_epi16(G1, temp);//14-bit
				if(saturate)
				{
					G1 = _mm_adds_epi16(G1, limiterRGB);
					G1 = _mm_subs_epu16(G1, limiterRGB);
					G1 = _mm_slli_epi16(G1, 2);//16-bit
				}
				else
				{
					G1 = _mm_srai_epi16(G1, 1);//13-bit signed
				}

				// Calculate B
				temp = _mm_set1_epi16(b_umult);//13-bit
				temp = _mm_mulhi_epi16(uu_epi16, temp);// 15-bit * 13-bit = 12-bit
				temp = _mm_slli_epi16(temp, 2); //14-bit
				B1 = _mm_adds_epi16(y_epi16, temp);//14-bit
				if(saturate)
				{
					B1 = _mm_adds_epi16(B1, limiterRGB);
					B1 = _mm_subs_epu16(B1, limiterRGB);
					B1 = _mm_slli_epi16(B1, 1);//16-bit
				}
				else
				{
					B1 = _mm_srai_epi16(B1, 1);//13-bit signed
				}

				switch(format)
				{
				case COLOR_FORMAT_B64A: //BGRA64
					BG = _mm_unpacklo_epi16(AA,R1);
					RA = _mm_unpacklo_epi16(G1,B1);
					RGBA = _mm_unpacklo_epi32(BG, RA);
					_mm_storeu_si128(output_ptr++, RGBA);// Store the first group of eight bytes of RGBA values
					RGBA = _mm_unpackhi_epi32(BG, RA);
					_mm_storeu_si128(output_ptr++, RGBA);// Store the second group of eight bytes of RGBA values

					BG = _mm_unpackhi_epi16(AA,R1);
					RA = _mm_unpackhi_epi16(G1,B1);
					RGBA = _mm_unpacklo_epi32(BG, RA);
					_mm_storeu_si128(output_ptr++, RGBA);// Store the first group of eight bytes of RGBA values
					RGBA = _mm_unpackhi_epi32(BG, RA);
					_mm_storeu_si128(output_ptr++, RGBA);// Store the second group of eight bytes of RGBA values
					break;

				case COLOR_FORMAT_RG48: //RGB48
				case COLOR_FORMAT_WP13: //WP13
					{
						unsigned short *sptr = (unsigned short *)output_ptr;

						sptr[0] = _mm_extract_epi16(R1, 0);
						sptr[1] = _mm_extract_epi16(G1, 0);
						sptr[2] = _mm_extract_epi16(B1, 0);

						sptr[3] = _mm_extract_epi16(R1, 1);
						sptr[4] = _mm_extract_epi16(G1, 1);
						sptr[5] = _mm_extract_epi16(B1, 1);

						sptr[6] = _mm_extract_epi16(R1, 2);
						sptr[7] = _mm_extract_epi16(G1, 2);
						sptr[8] = _mm_extract_epi16(B1, 2);

						sptr[9] = _mm_extract_epi16(R1, 3);
						sptr[10] = _mm_extract_epi16(G1, 3);
						sptr[11] = _mm_extract_epi16(B1, 3);

						sptr[12] = _mm_extract_epi16(R1, 4);
						sptr[13] = _mm_extract_epi16(G1, 4);
						sptr[14] = _mm_extract_epi16(B1, 4);
							
						sptr[15] = _mm_extract_epi16(R1, 5);
						sptr[16] = _mm_extract_epi16(G1, 5);
						sptr[17] = _mm_extract_epi16(B1, 5);
							
						sptr[18] = _mm_extract_epi16(R1, 6);
						sptr[19] = _mm_extract_epi16(G1, 6);
						sptr[20] = _mm_extract_epi16(B1, 6);
							
						sptr[21] = _mm_extract_epi16(R1, 7);
						sptr[22] = _mm_extract_epi16(G1, 7);
						sptr[23] = _mm_extract_epi16(B1, 7);

						output_ptr++;
						output_ptr++;
						output_ptr++;
					}
					break;

				case COLOR_FORMAT_RGB_8PIXEL_PLANAR:
					_mm_storeu_si128(output_ptr++, R1);
					_mm_storeu_si128(output_ptr++, G1);
					_mm_storeu_si128(output_ptr++, B1);
					break;

				case COLOR_FORMAT_R210: //r210
					{
						__m128i RL,GL,BL;
						R1 = _mm_srli_epi16(R1, 6); //10bit
						G1 = _mm_srli_epi16(G1, 6);
						B1 = _mm_srli_epi16(B1, 6);

						RL = _mm_unpacklo_epi16(R1, ZERO);
						GL = _mm_unpacklo_epi16(G1, ZERO);
						BL = _mm_unpacklo_epi16(B1, ZERO);
						GL = _mm_slli_epi32(GL, 10);
						RL = _mm_slli_epi32(RL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);

						// the algorithm is:
						// 1) [A B C D] => [B A D C]
						// 2) [B A D C] => [D C B A]

						// do first swap
						RGBA = _mm_or_si128( _mm_slli_epi16( RGBA, 8 ),
												_mm_srli_epi16( RGBA, 8 ) ); //swap it
						// do second swap
						RGBA = _mm_or_si128( _mm_slli_epi32( RGBA, 16 ),
												_mm_srli_epi32( RGBA, 16 ) ); //swap it

						_mm_storeu_si128(output_ptr++,RGBA);// Store the second group of eight bytes of RGB values


						RL = _mm_unpackhi_epi16(R1, ZERO);
						GL = _mm_unpackhi_epi16(G1, ZERO);
						BL = _mm_unpackhi_epi16(B1, ZERO);
						GL = _mm_slli_epi32(GL, 10);
						RL = _mm_slli_epi32(RL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);

						// the algorithm is:
						// 1) [A B C D] => [B A D C]
						// 2) [B A D C] => [D C B A]

						// do first swap
						RGBA = _mm_or_si128( _mm_slli_epi16( RGBA, 8 ),
												_mm_srli_epi16( RGBA, 8 ) ); //swap it
						// do second swap
						RGBA = _mm_or_si128( _mm_slli_epi32( RGBA, 16 ),
												_mm_srli_epi32( RGBA, 16 ) ); //swap it

						_mm_storeu_si128(output_ptr++,RGBA);// Store the second group of eight bytes of RGB values
					}
					break;

				case COLOR_FORMAT_DPX0: //DPX0
					{
						__m128i RL,GL,BL;
						R1 = _mm_srli_epi16(R1, 6); //10bit
						G1 = _mm_srli_epi16(G1, 6);
						B1 = _mm_srli_epi16(B1, 6);

						RL = _mm_unpacklo_epi16(R1, ZERO);
						GL = _mm_unpacklo_epi16(G1, ZERO);
						BL = _mm_unpacklo_epi16(B1, ZERO);
						GL = _mm_slli_epi32(GL, 10);
						RL = _mm_slli_epi32(RL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);

						RGBA = _mm_slli_epi32(RGBA, 2);

						// the algorithm is:
						// 1) [A B C D] => [B A D C]
						// 2) [B A D C] => [D C B A]

						// do first swap
						RGBA = _mm_or_si128( _mm_slli_epi16( RGBA, 8 ),
												_mm_srli_epi16( RGBA, 8 ) ); //swap it
						// do second swap
						RGBA = _mm_or_si128( _mm_slli_epi32( RGBA, 16 ),
												_mm_srli_epi32( RGBA, 16 ) ); //swap it

						_mm_storeu_si128(output_ptr++,RGBA);// Store the second group of eight bytes of RGB values


						RL = _mm_unpackhi_epi16(R1, ZERO);
						GL = _mm_unpackhi_epi16(G1, ZERO);
						BL = _mm_unpackhi_epi16(B1, ZERO);
						GL = _mm_slli_epi32(GL, 10);
						RL = _mm_slli_epi32(RL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);

						RGBA = _mm_slli_epi32(RGBA, 2);

						// the algorithm is:
						// 1) [A B C D] => [B A D C]
						// 2) [B A D C] => [D C B A]

						// do first swap
						RGBA = _mm_or_si128( _mm_slli_epi16( RGBA, 8 ),
												_mm_srli_epi16( RGBA, 8 ) ); //swap it
						// do second swap
						RGBA = _mm_or_si128( _mm_slli_epi32( RGBA, 16 ),
												_mm_srli_epi32( RGBA, 16 ) ); //swap it

						_mm_storeu_si128(output_ptr++,RGBA);// Store the second group of eight bytes of RGB values
					}
					break;
				case COLOR_FORMAT_RG30: //RG30
				case COLOR_FORMAT_AB10: //AB10
					{
						__m128i RL,GL,BL;

						R1 = _mm_srli_epi16(R1, 6); //10bit
						G1 = _mm_srli_epi16(G1, 6);
						B1 = _mm_srli_epi16(B1, 6);

						RL = _mm_unpacklo_epi16(R1, ZERO);
						GL = _mm_unpacklo_epi16(G1, ZERO);
						BL = _mm_unpacklo_epi16(B1, ZERO);

						GL = _mm_slli_epi32(GL, 10);
						BL = _mm_slli_epi32(BL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);
						_mm_storeu_si128(output_ptr++,RGBA);// Store the second group of eight bytes of RGB values


						RL = _mm_unpackhi_epi16(R1, ZERO);
						GL = _mm_unpackhi_epi16(G1, ZERO);
						BL = _mm_unpackhi_epi16(B1, ZERO);

						GL = _mm_slli_epi32(GL, 10);
						BL = _mm_slli_epi32(BL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);
						_mm_storeu_si128(output_ptr++,RGBA);// Store the second group of eight bytes of RGB values

					}
					break;

				case COLOR_FORMAT_AR10: //AR10
					{
						__m128i RL,GL,BL;

						R1 = _mm_srli_epi16(R1, 6); //10bit
						G1 = _mm_srli_epi16(G1, 6);
						B1 = _mm_srli_epi16(B1, 6);

						RL = _mm_unpacklo_epi16(R1, ZERO);
						GL = _mm_unpacklo_epi16(G1, ZERO);
						BL = _mm_unpacklo_epi16(B1, ZERO);

						GL = _mm_slli_epi32(GL, 10);
						RL = _mm_slli_epi32(RL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);
						_mm_storeu_si128(output_ptr++,RGBA);// Store the second group of eight bytes of RGB values


						RL = _mm_unpackhi_epi16(R1, ZERO);
						GL = _mm_unpackhi_epi16(G1, ZERO);
						BL = _mm_unpackhi_epi16(B1, ZERO);

						GL = _mm_slli_epi32(GL, 10);
						RL = _mm_slli_epi32(RL, 20);

						RGBA = _mm_add_epi32(RL, GL);
						RGBA = _mm_add_epi32(RGBA, BL);
						_mm_storeu_si128(output_ptr++,RGBA);// Store the second group of eight bytes of RGB values

					}
					break;
				}
			}

			// Should have exited the loop at the post processing column
			assert(column == post_column);

#endif

			// Process the rest of the row
			for (; column < width; column += 2)
			{
				// Convert to RGB
				int R, G, B;
				int Y1,Y2,U,V;
				PIXEL16U *outptr = (PIXEL16U *)output_row_ptr;

				Y1 = (y_row_ptr[column]);
				U = (u_row_ptr[column/2]);
				Y2 = (y_row_ptr[column + 1]);
				V = (v_row_ptr[column/2]);

				Y1 >>= 1;
				Y2 >>= 1;
				U >>= 1;
				V >>= 1;

				Y1 = Y1 - y_offset;
				Y2 = Y2 - y_offset;
				U = U - 16384;
				V = V - 16384;

				Y1 = Y1 * ymult; // 28-bit
				Y2 = Y2 * ymult;

				R = (Y1                + r_vmult * V + 64) >> 12;
				G = (Y1 -  g_umult * U - g_vmult * V + 128) >> 12;
				B = (Y1 +  b_umult * U + 64) >> 12;

				switch(format)
				{
				case COLOR_FORMAT_B64A: //BGRA64
					outptr[ 4 * column + 0] = SATURATE_16U(B);
					outptr[ 4 * column + 1] = SATURATE_16U(G);
					outptr[ 4 * column + 2] = SATURATE_16U(R);
					outptr[ 4 * column + 3] = 65535;
					break;

				case COLOR_FORMAT_RG48: //RGB48
					outptr[ 3 * column + 0] = SATURATE_16U(R);
					outptr[ 3 * column + 1] = SATURATE_16U(G);
					outptr[ 3 * column + 2] = SATURATE_16U(B);
					break;

				case COLOR_FORMAT_WP13: //WP13
					R>>=3;
					G>>=3;
					B>>=3;
				//	if(R > 32767) R = 32767;  // This shouldn't every be out of range DAN20121026
				//	if(G > 32767) G = 32767;
				//	if(B > 32767) B = 32767;
				//	if(R < -32767) R = -32767;
				//	if(G < -32767) G = -32767;
				//	if(B < -32767) B = -32767;
					((short *)outptr)[3 * column + 0] = (R);
					((short *)outptr)[3 * column + 1] = (G);
					((short *)outptr)[3 * column + 2] = (B);
					break;

				case COLOR_FORMAT_RGB_8PIXEL_PLANAR:
				case COLOR_FORMAT_R210:
				case COLOR_FORMAT_DPX0: //DPX0
				case COLOR_FORMAT_RG30: //RG30
				case COLOR_FORMAT_AB10: //AB10
				case COLOR_FORMAT_AR10: //AR10
				default :
					//TODO all the pixelformats
					//assert(0);
					break;
				}

				// Convert the second set of YCbCr values
				R = (Y2                + r_vmult * V + 64) >> 12;
				G = (Y2 -  g_umult * U - g_vmult * V + 128) >> 12;
				B = (Y2 +  b_umult * U + 64) >> 12;

				switch(format)
				{
				case COLOR_FORMAT_B64A: //BGRA64
					outptr[4 * column + 4] = SATURATE_16U(B);
					outptr[4 * column + 5] = SATURATE_16U(G);
					outptr[4 * column + 6] = SATURATE_16U(R);
					outptr[4 * column + 7] = 65535;
					break;
				case COLOR_FORMAT_RG48: //RGB48
					outptr[3 * column + 3] = SATURATE_16U(R);
					outptr[3 * column + 4] = SATURATE_16U(G);
					outptr[3 * column + 5] = SATURATE_16U(B);
					break;

				case COLOR_FORMAT_WP13: //WP13
					R>>=3;
					G>>=3;
					B>>=3;
				//	if(R > 32767) R = 32767;  // This shouldn't every be out of range DAN20121026
				//	if(G > 32767) G = 32767;
				//	if(B > 32767) B = 32767;
				//	if(R < -32767) R = -32767;
				//	if(G < -32767) G = -32767;
				//	if(B < -32767) B = -32767;
					((short *)outptr)[3 * column + 3] = (R);
					((short *)outptr)[3 * column + 4] = (G);
					((short *)outptr)[3 * column + 5] = (B);
					break;

				case COLOR_FORMAT_RGB_8PIXEL_PLANAR:
				case COLOR_FORMAT_R210:
				case COLOR_FORMAT_DPX0: //DPX0
				case COLOR_FORMAT_RG30: //RG30
				case COLOR_FORMAT_AB10: //AB10
				case COLOR_FORMAT_AR10: //AR10
				default :
					//TODO all the pixelformats
					//assert(0);
					break;
				}
			}

			// Should have exited the loop just after the last column
			assert(column == width);

			// Advance to the next rows in the input and output arrays
			y_row_ptr += y_pitch;
			u_row_ptr += u_pitch;
			v_row_ptr += v_pitch;
			output_row_ptr += output_pitch;
		}
	}
}






void ConvertYUVRow16uToYUV444(uint8_t *planar_output[], int planar_pitch[], ROI roi,
						   uint8_t *output_buffer, int output_width, int output_pitch,
						   int format)
{
	int width = roi.width;
	int height = roi.height;

	// Note that this routine is called with the YUV channels in our own
	// internal order so the chroma values have not already been reversed255

	PIXEL16U *y_row_ptr = (PIXEL16U *)planar_output[0];
	PIXEL16U *u_row_ptr = (PIXEL16U *)planar_output[2];		// Reverse the chroma order
	PIXEL16U *v_row_ptr = (PIXEL16U *)planar_output[1];

	int y_pitch = planar_pitch[0]/sizeof(PIXEL16U);
	int u_pitch = planar_pitch[2]/sizeof(PIXEL16U);			// Reverse the chroma order
	int v_pitch = planar_pitch[1]/sizeof(PIXEL16U);

	uint8_t *output_row_ptr = output_buffer;

	// Definitions for optimization
	const int column_step = 16;

	// Column at which post processing must begin
	int post_column = width - (width % column_step);

	{
		int row;

		for (row = 0; row < height; row++)
		{
			int column = 0;

#if (1 && XMMOPT)

			__m128i *yptr = (__m128i *)y_row_ptr;
			__m128i *uptr = (__m128i *)u_row_ptr;
			__m128i *vptr = (__m128i *)v_row_ptr;

			__m128i *output_ptr = (__m128i *)output_row_ptr;
			int lastu = u_row_ptr[0];
			int lastv = v_row_ptr[0];

			// Load 16 bytes of luma and 8 bytes of each chroma channel
			// to compute and store 3 times 16 = 48 bytes of RGB tuples.

			for (; column < post_column; column += column_step)
			{
				__m128i y1,y2,u,v,u1,u2,v1,v2;

				y1 = _mm_load_si128(yptr++);
				y2 = _mm_load_si128(yptr++);
				u = _mm_load_si128(uptr++);
				v = _mm_load_si128(vptr++);


				// Duplicate the second two chroma values
				u1 = _mm_unpacklo_epi16(u, u);
				v1 = _mm_unpacklo_epi16(v, v);
				{
					__m128i ut,vt;

					ut = _mm_slli_si128(u1, 2);
					ut = _mm_insert_epi16(ut, lastu, 0);
					vt = _mm_slli_si128(v1, 2);
					vt = _mm_insert_epi16(vt, lastv, 0);

					ut = _mm_srli_epi16(ut, 1);
					vt = _mm_srli_epi16(vt, 1);
					u1 = _mm_srli_epi16(u1, 1);
					v1 = _mm_srli_epi16(v1, 1);

					u1 = _mm_adds_epu16(ut, u1);
					v1 = _mm_adds_epu16(vt, v1);
				}

				_mm_storeu_si128(output_ptr++,y1);
				_mm_storeu_si128(output_ptr++,u1);
				_mm_storeu_si128(output_ptr++,v1);

				lastu = _mm_extract_epi16(u, 3);
				lastv = _mm_extract_epi16(v, 3);


				u2 = _mm_unpackhi_epi16(u, u);
				v2 = _mm_unpackhi_epi16(v, v);
				{
					__m128i ut,vt;

					ut = _mm_slli_si128(u2, 2);
					ut = _mm_insert_epi16(ut, lastu, 0);
					vt = _mm_slli_si128(v2, 2);
					vt = _mm_insert_epi16(vt, lastv, 0);

					ut = _mm_srli_epi16(ut, 1);
					vt = _mm_srli_epi16(vt, 1);
					u2 = _mm_srli_epi16(u2, 1);
					v2 = _mm_srli_epi16(v2, 1);

					u2 = _mm_adds_epu16(ut, u2);
					v2 = _mm_adds_epu16(vt, v2);
				}

				_mm_storeu_si128(output_ptr++,y2);
				_mm_storeu_si128(output_ptr++,u2);
				_mm_storeu_si128(output_ptr++,v2);

				lastu = _mm_extract_epi16(u, 7);
				lastv = _mm_extract_epi16(v, 7);
			}

			// Should have exited the loop at the post processing column
			assert(column == post_column);

#endif
			// Should have exited the loop just after the last column
			assert(column == width);

			// Advance to the next rows in the input and output arrays
			y_row_ptr += y_pitch;
			u_row_ptr += u_pitch;
			v_row_ptr += v_pitch;
			output_row_ptr += output_pitch;
		}
	}
}



void ConvertPlanarYUVToUYVY(uint8_t *planar_output[], int planar_pitch[], ROI roi,
							uint8_t *output_buffer, int output_width, int output_pitch,
							int format, int colorspace, bool inverted)
{
	int width = roi.width;
	int height = roi.height;

	uint8_t *y_row_ptr = planar_output[0];
	uint8_t *u_row_ptr = planar_output[1];
	uint8_t *v_row_ptr = planar_output[2];

	int y_pitch = planar_pitch[0];
	int u_pitch = planar_pitch[1];
	int v_pitch = planar_pitch[2];

	uint8_t *output_row_ptr = output_buffer;

	// Process four bytes each of luma and chroma per loop iteration
	//const int column_step = 16;

	// Column at which post processing must begin
	//int post_column = width - (width % column_step);

	int row;

	// The output pitch should be a positive number
	assert(output_pitch > 0);

#if 1
	// This routine does not handle inversion
	assert(inverted == false);
#else
	// Should the image be inverted?
	if (inverted) {
		output_row_ptr += (height - 1) * output_pitch;	// Start at the bottom row
		output_pitch = (- output_pitch);				// Negate the pitch to go up
	}
#endif

	for (row = 0; row < height; row++)
	{
		int column = 0;


#if (0 && XMMOPT) //DANREMOVED

		__m64 *output_ptr = (__m64 *)output_row_ptr;

		for (; column < post_column; column += column_step)
		{
			__m64 y_pi8;		// Eight unsigned bytes of color values
			__m64 u_pi8;
			__m64 v_pi8;
			__m64 uv_pi8;		// Eight bytes of interleaved chroma
			__m64 uyvy_pi8;		// Eight bytes of luma and chroma

			// Adjust the column for YUV 4:2:2 frame format
			int chroma_column = column/2;

			// Load the first eight bytes of luma
			y_pi8 = *((__m64 *)&y_row_ptr[column]);

			// Load eight bytes of chroma (u channel)
			u_pi8 = *((__m64 *)&u_row_ptr[chroma_column]);

			// Load eight bytes of chroma (v channel)
			v_pi8 = *((__m64 *)&v_row_ptr[chroma_column]);

			// Interleave the first group of four bytes of chroma
			uv_pi8 = _mm_unpacklo_pi8(u_pi8, v_pi8);

			// Interleave the first group of luma and chroma bytes
			uyvy_pi8 = _mm_unpacklo_pi8(uv_pi8, y_pi8);
			*(output_ptr++) = uyvy_pi8;

			// Interleave the second group of luma and chroma bytes
			uyvy_pi8 = _mm_unpackhi_pi8(uv_pi8, y_pi8);
			*(output_ptr++) = uyvy_pi8;

			// Load the second eight bytes of luma
			y_pi8 = *((__m64 *)&y_row_ptr[column + 8]);

			// Interleave the second group of four bytes of chroma
			uv_pi8 = _mm_unpackhi_pi8(u_pi8, v_pi8);

			// Interleave the third group of luma and chroma bytes
			uyvy_pi8 = _mm_unpacklo_pi8(uv_pi8, y_pi8);
			*(output_ptr++) = uyvy_pi8;

			// Interleave the fourth group of luma and chroma bytes
			uyvy_pi8 = _mm_unpackhi_pi8(uv_pi8, y_pi8);
			*(output_ptr++) = uyvy_pi8;
		}

		//_mm_empty();	// Clear the mmx register state

		// Should have exited the loop at the post processing column
		assert(column == post_column);

#endif

		// Process the rest of the column
		for (; column < width; column += 2)
		{
			// Get the first luma value
			int y1 = y_row_ptr[column];

			// Get the u chroma value
			int u = u_row_ptr[column/2];

			// Get the second luma value
			int y2 = y_row_ptr[column + 1];

			// Get the v chroma value
			int v = v_row_ptr[column/2];

			output_row_ptr[2 * column + 0] = u;
			output_row_ptr[2 * column + 1] = y1;
			output_row_ptr[2 * column + 2] = v;
			output_row_ptr[2 * column + 3] = y2;
		}

		// Should have exited the loop just after the last column
		assert(column == width);

		// Advance to the next rows in the input and output images
		y_row_ptr += y_pitch;
		u_row_ptr += u_pitch;
		v_row_ptr += v_pitch;
		output_row_ptr += output_pitch;
	}
}

//#if BUILD_PROSPECT
#if (0 && DEBUG)
void ConvertPlanarYUVToV210(PIXEL *planar_output[], int planar_pitch[], ROI roi,
							DWORD *output_buffer, int output_width, int output_pitch,
							int format, int colorspace, bool inverted)
{
	int width = roi.width;
	int height = roi.height;

	PIXEL *y_row_ptr = planar_output[0];
	PIXEL *u_row_ptr = planar_output[1];
	PIXEL *v_row_ptr = planar_output[2];

	int y_pitch = planar_pitch[0] / sizeof(PIXEL);
	int u_pitch = planar_pitch[1] / sizeof(PIXEL);
	int v_pitch = planar_pitch[2] / sizeof(PIXEL);

	DWORD *output_row_ptr = output_buffer;
	int row;

	// The output pitch should be a positive number
	assert(output_pitch > 0);
	output_pitch /= sizeof(DWORD);

	// This routine does not handle inversion
	assert(inverted == false);

	for (row = 0; row < height; row++)
	{
		int column = 0;

		// Process the rest of the row
		for (; column < width; column += 2)
		{
			int y1, y2;
			int u;
			int v;
			DWORD yuyv;

			// Get the first u chroma value
			u = (u_row_ptr[column/2] & V210_VALUE_MASK) >> 2;

			// Get the first luma value
			y1 = (y_row_ptr[column] & V210_VALUE_MASK) >> 2;

			// Get the first v chroma value
			v = (v_row_ptr[column/2] & V210_VALUE_MASK) >> 2;

			// Get the second luma value
			y2 = (y_row_ptr[column + 1] & V210_VALUE_MASK) >> 2;

			// Assemble and store the a packed word
			yuyv = (v << 24) | (y2 << 16) | (u << 8) | (y1 << 0);
			output_row_ptr[column/2] = yuyv;
		}

		// Should have exited the loop just after the last column
		assert(column == width);

		// Advance to the next rows in the input and output images
		y_row_ptr += y_pitch;
		u_row_ptr += u_pitch;
		v_row_ptr += v_pitch;
		output_row_ptr += output_pitch;
	}
}

#else

void ConvertPlanarYUVToV210(PIXEL *planar_output[], int planar_pitch[], ROI roi,
							uint8_t *output_buffer, int output_width, int output_pitch,
							int format, int colorspace, bool inverted, int precision)
{
	int width = roi.width;
	int height = roi.height;
	int upshift = 10 - precision;// shift to 16 bit unsigned precision


	// Note that the u and v chroma values are swapped
	PIXEL16U *y_row_ptr = (PIXEL16U *)planar_output[0];
	PIXEL16U *u_row_ptr = (PIXEL16U *)planar_output[2];
	PIXEL16U *v_row_ptr = (PIXEL16U *)planar_output[1];

	PIXEL16U *y_row_ptr16u = (PIXEL16U *)planar_output[0];
	PIXEL16U *u_row_ptr16u = (PIXEL16U *)planar_output[2];
	PIXEL16U *v_row_ptr16u = (PIXEL16U *)planar_output[1];

	int y_pitch = planar_pitch[0] / sizeof(PIXEL16U);
	int u_pitch = planar_pitch[1] / sizeof(PIXEL16U);
	int v_pitch = planar_pitch[2] / sizeof(PIXEL16U);

	uint32_t *output_row_ptr = (uint32_t *)output_buffer;

	// Process six pixels in each group of four double words
	const int v210_column_step = 6;

	// Reduce the width to a multiple pixels packed in four double words
	int v210_width = width - (width % v210_column_step);

	int row;

	// Must process and integer number of four double word groups
	assert((v210_width % v210_column_step) == 0);

	// The output pitch should be a positive number
	assert(output_pitch > 0);
	output_pitch /= sizeof(uint32_t);

#if 1
	// This routine does not handle inversion
	assert(inverted == false);
#else
	// Should the image be inverted?
	if (inverted) {
		output_row_ptr += (height - 1) * output_pitch;	// Start at the bottom row
		output_pitch = (- output_pitch);				// Negate the pitch to go up
	}
#endif


	for (row = 0; row < height; row++)
	{
		int column = 0;
		int output_column = 0;

		int y1, y2;
		int u;
		int v;
		uint32_t yuv;

#if (1 && XMMOPT)

		// Process twelve bytes each of luma and chroma per loop iteration
		//const int column_step = 12;
		const int column_step = 6;

		// Column at which post processing must begin
		int post_column = v210_width - (v210_width % column_step);

		__m128i *output_ptr = (__m128i *)output_row_ptr;
		__m128i overflowprotect_epi16 = _mm_set1_epi16(0x7fff-1023);

		// Must process and integer number of four double word groups
		assert((post_column % v210_column_step) == 0);

		if(upshift > 0)
		{
			for (; column < post_column; column += column_step)
			{
				__m128i yuv1_epi32;
				__m128i yuv2_epi32;
				__m128i yuv3_epi32;
				//__m128i mask_epi32;


				// Four double words in packed V210 format
				__m128i v210_epi32;

				// Adjust the column for the YUV 4:2:2 frame format
				int chroma_column = column/2;

				// Load the first group of four pixels
				yuv1_epi32 = _mm_setr_epi32(v_row_ptr[chroma_column + 0],
											y_row_ptr[column + 2],
											u_row_ptr[chroma_column + 2],
											y_row_ptr[column + 5]);

				// Load the second group of four pixels
				yuv2_epi32 = _mm_setr_epi32(y_row_ptr[column + 0],
											u_row_ptr[chroma_column + 1],
											y_row_ptr[column + 3],
											v_row_ptr[chroma_column + 2]);

				// Load the third group of four pixels
				yuv3_epi32 = _mm_setr_epi32(u_row_ptr[chroma_column + 0],
											y_row_ptr[column + 1],
											v_row_ptr[chroma_column + 1],
											y_row_ptr[column + 4]);

				// Saturate the pixels and upshift
				// this is need becuase the 8 bit precision decode doesn't clamp like the 10bit.
				yuv1_epi32 = _mm_slli_epi16(yuv1_epi32, upshift);
				yuv1_epi32 = _mm_adds_epi16(yuv1_epi32, overflowprotect_epi16);
				yuv1_epi32 = _mm_subs_epu16(yuv1_epi32, overflowprotect_epi16);

				yuv2_epi32 = _mm_slli_epi16(yuv2_epi32, upshift);
				yuv2_epi32 = _mm_adds_epi16(yuv2_epi32, overflowprotect_epi16);
				yuv2_epi32 = _mm_subs_epu16(yuv2_epi32, overflowprotect_epi16);

				yuv3_epi32 = _mm_slli_epi16(yuv3_epi32, upshift);
				yuv3_epi32 = _mm_adds_epi16(yuv3_epi32, overflowprotect_epi16);
				yuv3_epi32 = _mm_subs_epu16(yuv3_epi32, overflowprotect_epi16);

				// Pack the first group of pixels into the V210 output
				v210_epi32 = yuv1_epi32;
				v210_epi32 = _mm_slli_epi32(v210_epi32, V210_VALUE2_SHIFT);

				// Pack the second group of pixels into the V210 output
				v210_epi32 = _mm_or_si128(v210_epi32, yuv2_epi32);
				v210_epi32 = _mm_slli_epi32(v210_epi32, V210_VALUE2_SHIFT);

				// Pack the third group of pixels into the V210 output
				v210_epi32 = _mm_or_si128(v210_epi32, yuv3_epi32);

				// Store the group of V210 packed pixels
				_mm_store_si128(output_ptr++, v210_epi32);
			}
		}
		else if(upshift < 0)
		{
			for (; column < post_column; column += column_step)
			{
				__m128i yuv1_epi32;
				__m128i yuv2_epi32;
				__m128i yuv3_epi32;
				//__m128i mask_epi32;


				// Four double words in packed V210 format
				__m128i v210_epi32;

				// Adjust the column for the YUV 4:2:2 frame format
				int chroma_column = column/2;

				// Load the first group of four pixels
				yuv1_epi32 = _mm_setr_epi32(v_row_ptr16u[chroma_column + 0],
											y_row_ptr16u[column + 2],
											u_row_ptr16u[chroma_column + 2],
											y_row_ptr16u[column + 5]);

				// Load the second group of four pixels
				yuv2_epi32 = _mm_setr_epi32(y_row_ptr16u[column + 0],
											u_row_ptr16u[chroma_column + 1],
											y_row_ptr16u[column + 3],
											v_row_ptr16u[chroma_column + 2]);

				// Load the third group of four pixels
				yuv3_epi32 = _mm_setr_epi32(u_row_ptr16u[chroma_column + 0],
											y_row_ptr16u[column + 1],
											v_row_ptr16u[chroma_column + 1],
											y_row_ptr16u[column + 4]);

				// Saturate the pixels and upshift
				yuv1_epi32 = _mm_srli_epi16(yuv1_epi32, -upshift);
				yuv2_epi32 = _mm_srli_epi16(yuv2_epi32, -upshift);
				yuv3_epi32 = _mm_srli_epi16(yuv3_epi32, -upshift);

				// Pack the first group of pixels into the V210 output
				v210_epi32 = yuv1_epi32;
				v210_epi32 = _mm_slli_epi32(v210_epi32, V210_VALUE2_SHIFT);

				// Pack the second group of pixels into the V210 output
				v210_epi32 = _mm_or_si128(v210_epi32, yuv2_epi32);
				v210_epi32 = _mm_slli_epi32(v210_epi32, V210_VALUE2_SHIFT);

				// Pack the third group of pixels into the V210 output
				v210_epi32 = _mm_or_si128(v210_epi32, yuv3_epi32);

				// Store the group of V210 packed pixels
				_mm_store_si128(output_ptr++, v210_epi32);
			}
		}
		else
		{
			for (; column < post_column; column += column_step)
			{
				__m128i yuv1_epi32;
				__m128i yuv2_epi32;
				__m128i yuv3_epi32;
				//__m128i mask_epi32;


				// Four double words in packed V210 format
				__m128i v210_epi32;

				// Adjust the column for the YUV 4:2:2 frame format
				int chroma_column = column/2;

				// Load the first group of four pixels
				yuv1_epi32 = _mm_setr_epi32(v_row_ptr[chroma_column + 0],
											y_row_ptr[column + 2],
											u_row_ptr[chroma_column + 2],
											y_row_ptr[column + 5]);

				// Load the second group of four pixels
				yuv2_epi32 = _mm_setr_epi32(y_row_ptr[column + 0],
											u_row_ptr[chroma_column + 1],
											y_row_ptr[column + 3],
											v_row_ptr[chroma_column + 2]);

				// Load the third group of four pixels
				yuv3_epi32 = _mm_setr_epi32(u_row_ptr[chroma_column + 0],
											y_row_ptr[column + 1],
											v_row_ptr[chroma_column + 1],
											y_row_ptr[column + 4]);

				// Saturate the pixels
	#if 0 //DAN fixed saturation to 0 to 1023
				yuv1_epi32 = _mm_adds_epi16(yuv1_epi32, overflowprotect_epi16);
				yuv1_epi32 = _mm_subs_epu16(yuv1_epi32, overflowprotect_epi16);
				yuv2_epi32 = _mm_adds_epi16(yuv2_epi32, overflowprotect_epi16);
				yuv2_epi32 = _mm_subs_epu16(yuv2_epi32, overflowprotect_epi16);
				yuv3_epi32 = _mm_adds_epi16(yuv3_epi32, overflowprotect_epi16);
				yuv3_epi32 = _mm_subs_epu16(yuv3_epi32, overflowprotect_epi16);

			/*

				__m128i limit_epi32 = _mm_set1_epi32(_m_from_int(V210_VALUE_MASK));

				mask_epi32 = _mm_cmpgt_epi32(yuv1_epi32, limit_epi32);
				yuv1_epi32 = _mm_andnot_si128(mask_epi32, yuv1_epi32);
				mask_epi32 = _mm_and_si128(mask_epi32, limit_epi32);
				yuv1_epi32 = _mm_or_si128(yuv1_epi32, mask_epi32);

				// Saturate the pixels
				mask_epi32 = _mm_cmpgt_epi32(yuv2_epi32, limit_epi32);
				yuv2_epi32 = _mm_andnot_si128(mask_epi32, yuv2_epi32);
				mask_epi32 = _mm_and_si128(mask_epi32, limit_epi32);
				yuv2_epi32 = _mm_or_si128(yuv2_epi32, mask_epi32);

				// Saturate the pixels
				mask_epi32 = _mm_cmpgt_epi32(yuv3_epi32, limit_epi32);
				yuv3_epi32 = _mm_andnot_si128(mask_epi32, yuv3_epi32);
				mask_epi32 = _mm_and_si128(mask_epi32, limit_epi32);
				yuv3_epi32 = _mm_or_si128(yuv3_epi32, mask_epi32);*/
	#endif

				// Pack the first group of pixels into the V210 output
				v210_epi32 = yuv1_epi32;
				v210_epi32 = _mm_slli_epi32(v210_epi32, V210_VALUE2_SHIFT);

				// Pack the second group of pixels into the V210 output
				v210_epi32 = _mm_or_si128(v210_epi32, yuv2_epi32);
				v210_epi32 = _mm_slli_epi32(v210_epi32, V210_VALUE2_SHIFT);

				// Pack the third group of pixels into the V210 output
				v210_epi32 = _mm_or_si128(v210_epi32, yuv3_epi32);

				// Store the group of V210 packed pixels
				_mm_store_si128(output_ptr++, v210_epi32);
			}
		}

		// Should have exited the loop at the post processing column
		assert(column == post_column);

#endif


		output_column = (int)((uint32_t *)output_ptr - output_row_ptr);

		// Process the rest of the column
		for (; column < width/*v210_width*/; column += v210_column_step)
		{
			// Get the first u chroma value
			if(upshift > 0)
			{
				u = SATURATE_V210(u_row_ptr[column/2])<<upshift;
				if(u > 1023) u = 1023; // this is need becuase the 8 bit precision decode doesn't clamp like the 10bit.

				// Get the first luma value
				y1 = y2 = SATURATE_V210(y_row_ptr[column])<<upshift;
				if(y1 > 1023) y1 = 1023;

				// Get the first v chroma value
				v = SATURATE_V210(v_row_ptr[column/2])<<upshift;
				if(v > 1023) v = 1023;

				// Assemble and store the first packed word
				yuv = (v << V210_VALUE3_SHIFT) | (y1 << V210_VALUE2_SHIFT) | (u << V210_VALUE1_SHIFT);
				output_row_ptr[output_column++] = yuv;

				// Get the second luma value
				if(column + 1 < width)
					y1 = SATURATE_V210(y_row_ptr[column + 1])<<upshift;
				if(y1 > 1023) y1 = 1023;

				// Get the second u chroma value
				if(column + 2 < width)
					u = SATURATE_V210(u_row_ptr[column/2 + 1])<<upshift;
				if(u > 1023) u = 1023;

				// Get the third luma value
				if(column + 2 < width)
					y2 = SATURATE_V210(y_row_ptr[column + 2])<<upshift;
				if(y2 > 1023) y2 = 1023;

				// Assemble and store the second packed word
				yuv = (y2 << V210_VALUE3_SHIFT) | (u << V210_VALUE2_SHIFT) | (y1 << V210_VALUE1_SHIFT);
				output_row_ptr[output_column++] = yuv;


				// Get the second v chroma value
				if(column + 2 < width)
					v = SATURATE_V210(v_row_ptr[column/2 + 1])<<upshift;
				if(v > 1023) v = 1023;

				// Get the fourth luma value
				if(column + 3 < width)
					y1 = SATURATE_V210(y_row_ptr[column + 3])<<upshift;
				if(y1 > 1023) y1 = 1023;

				// Get the third u chroma value
				if(column + 4 < width)
					u = SATURATE_V210(u_row_ptr[column/2 + 2])<<upshift;
				if(u > 1023) u = 1023;

				// Assemble and store the third packed word
				yuv = (u << V210_VALUE3_SHIFT) | (y1 << V210_VALUE2_SHIFT) | (v << V210_VALUE1_SHIFT);
				output_row_ptr[output_column++] = yuv;


				// Get the fifth luma value
				if(column + 4 < width)
					y1 = SATURATE_V210(y_row_ptr[column + 4])<<upshift;
				if(y1 > 1023) y1 = 1023;

				// Get the third v chroma value
				if(column + 4 < width)
					v = SATURATE_V210(v_row_ptr[column/2 + 2])<<upshift;
				if(v > 1023) v = 1023;

				// Get the sixth luma value
				if(column + 5 < width)
					y2 = SATURATE_V210(y_row_ptr[column + 5])<<upshift;
				if(y2 > 1023) y2 = 1023;

				// Assemble and store the fourth packed word
				yuv = (y2 << V210_VALUE3_SHIFT) | (v << V210_VALUE2_SHIFT) | (y1 << V210_VALUE1_SHIFT);
				output_row_ptr[output_column++] = yuv;
			}
			else
			{	
				int dnshift = -upshift;
				u = SATURATE_V210(u_row_ptr[column/2]>>dnshift);
				if(u > 1023) u = 1023; // this is need becuase the 8 bit precision decode doesn't clamp like the 10bit.

				// Get the first luma value
				y1 = y2 = SATURATE_V210(y_row_ptr[column]>>dnshift);
				if(y1 > 1023) y1 = 1023;

				// Get the first v chroma value
				v = SATURATE_V210(v_row_ptr[column/2]>>dnshift);
				if(v > 1023) v = 1023;

				// Assemble and store the first packed word
				yuv = (v << V210_VALUE3_SHIFT) | (y1 << V210_VALUE2_SHIFT) | (u << V210_VALUE1_SHIFT);
				output_row_ptr[output_column++] = yuv;


				// Get the second luma value
				if(column + 1 < width)
					y1 = SATURATE_V210(y_row_ptr[column + 1]>>dnshift);
				if(y1 > 1023) y1 = 1023;

				// Get the second u chroma value
				if(column + 2 < width)
					u = SATURATE_V210(u_row_ptr[column/2 + 1]>>dnshift);
				if(u > 1023) u = 1023;

				// Get the third luma value
				if(column + 2 < width)
					y2 = SATURATE_V210(y_row_ptr[column + 2]>>dnshift);
				if(y2 > 1023) y2 = 1023;

				// Assemble and store the second packed word
				yuv = (y2 << V210_VALUE3_SHIFT) | (u << V210_VALUE2_SHIFT) | (y1 << V210_VALUE1_SHIFT);
				output_row_ptr[output_column++] = yuv;


				// Get the second v chroma value
				if(column + 2 < width)
					v = SATURATE_V210(v_row_ptr[column/2 + 1]>>dnshift);
				if(v > 1023) v = 1023;

				// Get the fourth luma value
				if(column + 3 < width)
					y1 = SATURATE_V210(y_row_ptr[column + 3]>>dnshift);
				if(y1 > 1023) y1 = 1023;

				// Get the third u chroma value
				if(column + 3 < width)
					u = SATURATE_V210(u_row_ptr[column/2 + 2]>>dnshift);
				if(u > 1023) u = 1023;

				// Assemble and store the third packed word
				yuv = (u << V210_VALUE3_SHIFT) | (y1 << V210_VALUE2_SHIFT) | (v << V210_VALUE1_SHIFT);
				output_row_ptr[output_column++] = yuv;


				// Get the fifth luma value
				if(column + 4 < width)
					y1 = SATURATE_V210(y_row_ptr[column + 4]>>dnshift);
				if(y1 > 1023) y1 = 1023;

				// Get the third v chroma value
				if(column + 4 < width)
					v = SATURATE_V210(v_row_ptr[column/2 + 2]>>dnshift);
				if(v > 1023) v = 1023;

				// Get the sixth luma value
				if(column + 5 < width)
					y2 = SATURATE_V210(y_row_ptr[column + 5]>>dnshift);
				if(y2 > 1023) y2 = 1023;

				// Assemble and store the fourth packed word
				yuv = (y2 << V210_VALUE3_SHIFT) | (v << V210_VALUE2_SHIFT) | (y1 << V210_VALUE1_SHIFT);
				output_row_ptr[output_column++] = yuv;
			}
		}

		// Should have exited the loop just after the last column
		//assert(column == width);

		// Advance to the next rows in the input and output images
		y_row_ptr += y_pitch;
		u_row_ptr += u_pitch;
		v_row_ptr += v_pitch;
		y_row_ptr16u += y_pitch;
		u_row_ptr16u += u_pitch;
		v_row_ptr16u += v_pitch;
		output_row_ptr += output_pitch;
	}
}
#endif
//#endif

//#if BUILD_PROSPECT
void ConvertPlanarYUVToYU64(PIXEL *planar_output[], int planar_pitch[], ROI roi,
							uint8_t *output_buffer, int output_width, int output_pitch,
							int format, int colorspace, bool inverted, int precision)
{
	int width = roi.width;
	int height = roi.height;
	int upshift = 16 - precision;// shift to 16 bit unsigned precision

	// Note that the U and v chroma values are swapped
	PIXEL *y_row_ptr = planar_output[0];
	PIXEL *u_row_ptr = planar_output[2];
	PIXEL *v_row_ptr = planar_output[1];

	int y_pitch = planar_pitch[0] / sizeof(PIXEL);
	int u_pitch = planar_pitch[1] / sizeof(PIXEL);
	int v_pitch = planar_pitch[2] / sizeof(PIXEL);

	uint32_t *output_row_ptr = (uint32_t *)output_buffer;

	const int yu64_column_step = 2;

#if (0 && XMMOPT)
	// Process four bytes each of luma and chroma per loop iteration
	const int column_step = 16;

	// Column at which post processing must begin
	int post_column = width - (width % column_step);
#endif

	int row;

	// The output pitch should be a positive number
	assert(output_pitch > 0);
	output_pitch /= sizeof(uint32_t);
	//output_pitch /= ((2 * v210_column_step) / sizeof(uint32_t));

#if 1
	// This routine does not handle inversion
	assert(inverted == false);
#else
	// Should the image be inverted?
	if (inverted) {
		output_row_ptr += (height - 1) * output_pitch;	// Start at the bottom row
		output_pitch = (- output_pitch);				// Negate the pitch to go up
	}
#endif

	// Adjust the width to a multiple of the number of pixels packed into four words
	width -= (width % yu64_column_step);

	for (row = 0; row < height; row++)
	{
		int column = 0;

		int output_column = 0;

#if (0 && XMMOPT)

		__m64 *output_ptr = (__m64 *)output_row_ptr;

		for (; column < post_column; column += column_step)
		{
			__m64 y_pi16;		// Four unsigned words of color values
			__m64 u_pi16;
			__m64 v_pi16;
			__m64 uv_pi16;		// Four words of interleaved chroma
			__m64 uyvy_pi16;	// Four words of luma and chroma

			// Adjust the column for YUV 4:2:2 frame format
			int chroma_column = column/2;

			// Load the first four words of luma
			y_pi16 = *((__m64 *)&y_row_ptr[column]);

			// Load four words of chroma (u channel)
			u_pi16 = *((__m64 *)&u_row_ptr[chroma_column]);

			// Load four words of chroma (v channel)
			v_pi16 = *((__m64 *)&v_row_ptr[chroma_column]);

			// Interleave the first group of two words of chroma
			uv_pi16 = _mm_unpacklo_pi16(u_pi16, v_pi16);

			// Interleave the first group of luma and chroma words
			uyvy_pi16 = _mm_unpacklo_pi16(uv_pi16, y_pi16);
			//*(output_ptr++) = uyvy_pi16;

			// Interleave the second group of luma and chroma words
			uyvy_pi16 = _mm_unpackhi_pi16(uv_pi16, y_pi16);
			//*(output_ptr++) = uyvy_pi8;

			// Load the second four words of luma
			y_pi16 = *((__m64 *)&y_row_ptr[column + 4]);

			// Interleave the second group of two words of chroma
			uv_pi16 = _mm_unpackhi_pi16(u_pi16, v_pi16);

			// Interleave the third group of luma and chroma words
			uyvy_pi16 = _mm_unpacklo_pi16(uv_pi16, y_pi16);
			//*(output_ptr++) = uyvy_pi16;

			// Interleave the fourth group of luma and chroma words
			uyvy_pi16 = _mm_unpackhi_pi16(uv_pi16, y_pi16);
			//*(output_ptr++) = uyvy_pi16;
		}

		//_mm_empty();	// Clear the mmx register state

		// Should have exited the loop at the post processing column
		assert(column == post_column);

#endif

		if(precision == 16)
		{

			PIXEL16U *y_row_ptr16u = (PIXEL16U *)y_row_ptr;
			PIXEL16U *u_row_ptr16u = (PIXEL16U *)u_row_ptr;
			PIXEL16U *v_row_ptr16u = (PIXEL16U *)v_row_ptr;

			// Process the rest of the column
			for (; column < width; column += yu64_column_step)
			{
				int y1, y2;
				int u;
				int v;
				uint32_t yuv;

				// Get the first luma value
				y1 = y_row_ptr16u[column];

				// Get the u chroma value
				u = u_row_ptr16u[column/2];

				// Get the second luma value
				y2 = y_row_ptr16u[column+1];

				// Get the v chroma value
				v = v_row_ptr16u[column/2];

				yuv = (v << 16) | y1;
				output_row_ptr[output_column++] = yuv;
				yuv = (u << 16) | y2;
				output_row_ptr[output_column++] = yuv;
			}
		}
		else
		{
			// Process the rest of the column
			for (; column < width; column += yu64_column_step)
			{
				int y1, y2;
				int u;
				int v;
				uint32_t yuv;

				// Get the first luma value
				y1 = y_row_ptr[column]<<upshift;
				if(y1 > 0xffff) y1 = 0xffff;
				if(y1 < 0) y1 = 0;

				// Get the u chroma value
				u = u_row_ptr[column/2]<<upshift;
				if(u > 0xffff) u = 0xffff;
				if(u < 0) u = 0;

				// Get the second luma value
				y2 = y_row_ptr[column+1]<<upshift;
				if(y2 > 0xffff) y2 = 0xffff;
				if(y2 < 0) y2 = 0;

				// Get the v chroma value
				v = v_row_ptr[column/2]<<upshift;
				if(v > 0xffff) v = 0xffff;
				if(v < 0) v = 0;

				yuv = (v << 16) | y1;
				output_row_ptr[output_column++] = yuv;
				yuv = (u << 16) | y2;
				output_row_ptr[output_column++] = yuv;
			}
		}

		// Should have exited the loop just after the last column
		assert(column == width);

		// Advance to the next rows in the input and output images
		y_row_ptr += y_pitch;
		u_row_ptr += u_pitch;
		v_row_ptr += v_pitch;
		output_row_ptr += output_pitch;
	}
}
//#endif



void ConvertPlanarYUVToYR16(PIXEL *planar_output[], int planar_pitch[], ROI roi,
							uint8_t *output_buffer, int output_width, int output_pitch,
							int format, int colorspace, bool inverted, int precision)
{
	int width = roi.width;
	int height = roi.height;
	int upshift = 16 - precision;// shift to 16 bit unsigned precision

	// Note that the U and v chroma values are swapped
	PIXEL *y_row_ptr = planar_output[0];
	PIXEL *u_row_ptr = planar_output[2];
	PIXEL *v_row_ptr = planar_output[1];

	int y_pitch = planar_pitch[0] / sizeof(PIXEL);
	int u_pitch = planar_pitch[1] / sizeof(PIXEL);
	int v_pitch = planar_pitch[2] / sizeof(PIXEL);

	uint32_t *output_row_ptr = (uint32_t *)output_buffer;
	PIXEL16U *output_row_ptr16U = (PIXEL16U *)output_buffer;

	const int yu64_column_step = 2;

#if (0 && XMMOPT)
	// Process four bytes each of luma and chroma per loop iteration
	const int column_step = 16;

	// Column at which post processing must begin
	int post_column = width - (width % column_step);
#endif

	int row;

	// The output pitch should be a positive number
	assert(output_pitch > 0);
	output_pitch /= sizeof(uint32_t);
	//output_pitch /= ((2 * v210_column_step) / sizeof(uint32_t));

#if 1
	// This routine does not handle inversion
	assert(inverted == false);
#else
	// Should the image be inverted?
	if (inverted) {
		output_row_ptr += (height - 1) * output_pitch;	// Start at the bottom row
		output_pitch = (- output_pitch);				// Negate the pitch to go up
	}
#endif

	// Adjust the width to a multiple of the number of pixels packed into four words
	width -= (width % yu64_column_step);

	for (row = 0; row < height; row++)
	{
		int column = 0;

		//int output_column = 0;

		output_row_ptr16U = (PIXEL16U *)output_row_ptr;

		if(precision == 16)
		{

			PIXEL16U *y_row_ptr16u = (PIXEL16U *)y_row_ptr;
			//PIXEL16U *u_row_ptr16u = (PIXEL16U *)u_row_ptr;
			//PIXEL16U *v_row_ptr16u = (PIXEL16U *)v_row_ptr;

			memcpy(output_row_ptr16U, y_row_ptr16u, width*2);
			output_row_ptr16U += width;
			memcpy(output_row_ptr16U, y_row_ptr16u, width);
			output_row_ptr16U += width>>1;
			memcpy(output_row_ptr16U, y_row_ptr16u, width);
			output_row_ptr16U += width>>1;
		}
		else
		{
			int Uoffset = width+(width>>1);
			int Voffset = width;
			__m128i yy;
			__m128i limiter = _mm_set1_epi16(0x7fff - (1<<(16-upshift))-1);
			int sse2width = (width / 16) * 16;
			int sse2widthchroma = sse2width>>1;

			for (column = 0; column < sse2width; column+=8) //Y
			{
				yy =  _mm_loadu_si128((__m128i *)&y_row_ptr[column]);
				yy = _mm_adds_epi16(yy, limiter);
				yy = _mm_subs_epu16(yy, limiter);
				yy = _mm_slli_epi16(yy,upshift);
				_mm_storeu_si128((__m128i *)&output_row_ptr16U[column], yy);
			}
			for (column = 0; column<sse2widthchroma; column+=8) //U
			{
				yy =  _mm_loadu_si128((__m128i *)&u_row_ptr[column]);
				yy = _mm_adds_epi16(yy, limiter);
				yy = _mm_subs_epu16(yy, limiter);
				yy = _mm_slli_epi16(yy,upshift);
				_mm_storeu_si128((__m128i *)&output_row_ptr16U[Uoffset+column], yy);
			}
			for (column = 0; column<sse2widthchroma; column+=8) //V
			{
				yy =  _mm_loadu_si128((__m128i *)&v_row_ptr[column]);
				yy = _mm_adds_epi16(yy, limiter);
				yy = _mm_subs_epu16(yy, limiter);
				yy = _mm_slli_epi16(yy,upshift);
				_mm_storeu_si128((__m128i *)&output_row_ptr16U[Voffset+column], yy);
			}

			column = sse2width;
			// Process the rest of the column
			for (; column < width; column+=2)
			{
				int y1, y2;
				int u;
				int v;

				// Get the first luma value
				y1 = y_row_ptr[column]<<upshift;
				if(y1 > 0xffff) y1 = 0xffff;
				if(y1 < 0) y1 = 0;
				output_row_ptr16U[column]=y1;

				y2 = y_row_ptr[column+1]<<upshift;
				if(y2 > 0xffff) y2 = 0xffff;
				if(y2 < 0) y2 = 0;
				output_row_ptr16U[column+1]=y2;

				// Get the u chroma value
				u = u_row_ptr[column>>1]<<upshift;
				if(u > 0xffff) u = 0xffff;
				if(u < 0) u = 0;
				output_row_ptr16U[Uoffset+(column>>1)]=u;

				// Get the v chroma value
				v = v_row_ptr[column>>1]<<upshift;
				if(v > 0xffff) v = 0xffff;
				if(v < 0) v = 0;
				output_row_ptr16U[Voffset+(column>>1)]=v;
			}
		}

		// Should have exited the loop just after the last column
		assert(column == width);

		// Advance to the next rows in the input and output images
		y_row_ptr += y_pitch;
		u_row_ptr += u_pitch;
		v_row_ptr += v_pitch;
		output_row_ptr += output_pitch;
	}
}


//#if BUILD_PROSPECT
// Convert a row of 16-bit YUV to 8-bit planes
void ConvertYUVPacked16sRowToPlanar8u(PIXEL *input, int length, uint8_t *y_output, uint8_t *u_output, uint8_t *v_output)
{
	PIXEL *input_ptr = input;
	uint8_t *y_ptr = y_output;
	uint8_t *u_ptr = u_output;
	uint8_t *v_ptr = v_output;
	int i;

	// Must have an even number of pixels
	assert((length % 2) == 0);

	for (i = 0; i < length; i += 2)
	{
		// Load two input pixels
		int y1 = *(input_ptr++);
		int u1 = *(input_ptr++);
		int y2 = *(input_ptr++);
		int v1 = *(input_ptr++);

		// Reduce to eight bits
		y1 >>= 2;
		u1 >>= 2;
		y2 >>= 2;
		v1 >>= 2;

		// Store the luma
		*(y_ptr++) = SATURATE_8U(y1);
		*(y_ptr++) = SATURATE_8U(y2);

		// Store the chroma
		*(u_ptr++) = SATURATE_8U(u1);
		*(v_ptr++) = SATURATE_8U(v1);
	}
}
//#endif


//#if BUILD_PROSPECT
// Convert a row of 16-bit YUV to 16-bit planes
void ConvertYUVPacked16sRowToPlanar16s(PIXEL *input, int length, PIXEL *y_output, PIXEL *u_output, PIXEL *v_output)
{
	PIXEL *input_ptr = input;
	PIXEL *y_ptr = y_output;
	PIXEL *u_ptr = u_output;
	PIXEL *v_ptr = v_output;
	int i;

	// Must have an even number of pixels
	assert((length % 2) == 0);

	for (i = 0; i < length; i += 2)
	{
		// Load two input pixels
		int y1 = *(input_ptr++);
		int u1 = *(input_ptr++);
		int y2 = *(input_ptr++);
		int v1 = *(input_ptr++);

#if (PRESCALE_V210_INPUT > 0)
		// Reduce the pixel range to avoid overflows in the wavelet transforms
		y1 >>= PRESCALE_V210_INPUT;
		u1 >>= PRESCALE_V210_INPUT;
		y2 >>= PRESCALE_V210_INPUT;
		v1 >>= PRESCALE_V210_INPUT;
#endif
		// Store the luma
		*(y_ptr++) = SATURATE_16S(y1);
		*(y_ptr++) = SATURATE_16S(y2);

		// Store the chroma
		*(u_ptr++) = SATURATE_16S(u1);
		*(v_ptr++) = SATURATE_16S(v1);
	}
}
//#endif


// Convert a row of RGB24 data to YUV (can use in place computation)
void ConvertRGB24RowToYUV(uint8_t *input, uint8_t *output, int length)
{
	uint8_t *input_ptr = input;
	uint8_t *output_ptr = output;
	int i;

	// Must have an even number of pixels
	assert((length % 2) == 0);

	// Process the row or RGB24 data
	for(i = 0; i < length; i += 2)
	{
		int r, g, b;
		int y, u, v;

		// Load the first tuple of RGB values
		r = *(input_ptr++);
		g = *(input_ptr++);
		b = *(input_ptr++);

		// Convert RGB to YCbCr
		y = ( 66 * r + 129 * g +  25 * b +  4224) >> 8;
		u = (-38 * r -  74 * g + 112 * b + 32896) >> 9;
		v = (112 * r -  94 * g -  18 * b + 32896) >> 9;

		// Store the first luma value
		*(output_ptr++) = SATURATE_Y(y);

		// Load the second tuple of RGB values
		r = *(input_ptr++);
		g = *(input_ptr++);
		b = *(input_ptr++);

		// Convert RGB to YCbCr
		y =  ( 66 * r + 129 * g +  25 * b +  4224) >> 8;
		u += (-38 * r -  74 * g + 112 * b + 32896) >> 9;
		v += (112 * r -  94 * g -  18 * b + 32896) >> 9;

		// Store the chroma values and the second luma value
		*(output_ptr++) = SATURATE_Cb(u);
		*(output_ptr++) = SATURATE_Y(y);
		*(output_ptr++) = SATURATE_Cr(v);
	}
}


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

// Pack one row of 16-bit unpacked luma and chroma into 8-bit luma and chroma (4:2:2 sampling)
void ConvertUnpacked16sRowToPacked8u(PIXEL **channel_row_ptr, int num_channels,
									 uint8_t *output_ptr, int length, int format)
{
	// Stub routine for processor specific dispatch
}

#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Pack one row of 16-bit unpacked luma and chroma into 8-bit luma and chroma (4:2:2 sampling)
void ConvertUnpacked16sRowToPacked8u(PIXEL **channel_row_ptr, int num_channels,
									 uint8_t *output_ptr, int length, int format)
{
	// Note: This routine swaps the chroma values
	PIXEL16U *y_input_ptr = channel_row_ptr[0];
	PIXEL16U *u_input_ptr = channel_row_ptr[2];
	PIXEL16U *v_input_ptr = channel_row_ptr[1];

	uint8_t *yuv_output_ptr = output_ptr;

	// The scale shift for converting 16-bit pixels to 8-bit pixels
	//const int descale = 8;

	// Start processing at the leftmost column
	int column = 0;

#if (1 && XMMOPT)

	// Process eight values of luma and chroma per loop iteration
	const int column_step = 8;

	// Column at which post processing must begin
	int post_column = length - (length % column_step);

	// Initialize the input pointers into each channel
	__m64 *y_ptr = (__m64 *)y_input_ptr;
	__m64 *u_ptr = (__m64 *)u_input_ptr;
	__m64 *v_ptr = (__m64 *)v_input_ptr;

	// Initialize the output pointer for the packed YUV results
	__m64 *yuv_ptr = (__m64 *)yuv_output_ptr;

	for (; column < post_column; column += column_step)
	{
		int chroma_column = column/2;

		__m64 y1_pi16;
		__m64 y2_pi16;
		__m64 u1_pi16;
		__m64 v1_pi16;
		__m64 uv_pi16;
		__m64 yuv1_pi16;
		__m64 yuv2_pi16;
		__m64 yuv_pi8;

		// Load four u chroma values
		u1_pi16 = *(u_ptr++);

		// Load four v chroma values
		v1_pi16 = *(v_ptr++);

		// Load the first four luma values
		y1_pi16 = *(y_ptr++);

		// Load the second four luma values
		y2_pi16 = *(y_ptr++);
#if 0
		// Reduce the pixel values to eight bits
		u1_pi16 = _mm_srli_pi16(u1_pi16, descale);
		v1_pi16 = _mm_srli_pi16(v1_pi16, descale);
		y1_pi16 = _mm_srli_pi16(y1_pi16, descale);
		y2_pi16 = _mm_srli_pi16(y2_pi16, descale);
#endif
		// Interleave the first two chroma values
		uv_pi16 = _mm_unpacklo_pi16(u1_pi16, v1_pi16);

		// Interleave the first two luma values with the first two chroma values
		yuv1_pi16 = _mm_unpacklo_pi16(y1_pi16, uv_pi16);

		// Interleave the second two luma values with the second two chroma values
		yuv2_pi16 = _mm_unpackhi_pi16(y1_pi16, uv_pi16);

		// Pack the first four interleaved luma and chroma values
		yuv_pi8 = _mm_packs_pu16(yuv1_pi16, yuv2_pi16);

		// Store the first four interleaved luma and chroma values
		*(yuv_ptr++) = yuv_pi8;

		// Interleave the second two chroma values
		uv_pi16 = _mm_unpackhi_pi16(u1_pi16, v1_pi16);

		// Interleave the third two luma values with the third two chroma values
		yuv1_pi16 = _mm_unpacklo_pi16(y2_pi16, uv_pi16);

		// Interleave the fourth two luma values with the fourth two chroma values
		yuv2_pi16 = _mm_unpackhi_pi16(y2_pi16, uv_pi16);

		// Pack the second four interleaved luma and chroma values
		yuv_pi8 = _mm_packs_pu16(yuv1_pi16, yuv2_pi16);

		// Store the second four interleaved luma and chroma values
		*(yuv_ptr++) = yuv_pi8;
	}

	// Clear the mmx register state
	//_mm_empty();

	// Check that the fast loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Need to handle the rest of the conversion outside of the fast loop
	y_input_ptr = (PIXEL16U *)y_ptr;
	u_input_ptr = (PIXEL16U *)u_ptr;
	v_input_ptr = (PIXEL16U *)v_ptr;
	yuv_output_ptr = (uint8_t *)yuv_ptr;

	for (; column < length; column += 2)
	{
		int y1 = *(y_input_ptr++);
		int y2 = *(y_input_ptr++);

		int u = *(u_input_ptr++);
		int v = *(v_input_ptr++);

		*(yuv_output_ptr++) = SATURATE_8U(y1);
		*(yuv_output_ptr++) = SATURATE_8U(u);
		*(yuv_output_ptr++) = SATURATE_8U(y2);
		*(yuv_output_ptr++) = SATURATE_8U(v);
	}
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Pack one row of 16-bit unpacked luma and chroma into 8-bit luma and chroma (4:2:2 sampling)
void ConvertUnpacked16sRowToPacked8u(PIXEL **channel_row_ptr, int num_channels,
									 uint8_t *output_ptr, int length, int format)
{
	// Note: This routine swaps the chroma values
	PIXEL16U *y_input_ptr = (PIXEL16U *)channel_row_ptr[0];
	PIXEL16U *u_input_ptr = (PIXEL16U *)channel_row_ptr[2];
	PIXEL16U *v_input_ptr = (PIXEL16U *)channel_row_ptr[1];

	uint8_t *yuv_output_ptr = output_ptr;

	// The scale shift for converting 16-bit pixels to 8-bit pixels
	//const int descale = 8;

	// Start processing at the leftmost column
	int column = 0;

#if (1 && XMMOPT)

	// Process sixteen values of luma and chroma per loop iteration
	const int column_step = 16;

	// Column at which post processing must begin
	int post_column = length - (length % column_step);

	// Initialize the input pointers into each channel
	__m128i *y_ptr = (__m128i *)y_input_ptr;
	__m128i *u_ptr = (__m128i *)u_input_ptr;
	__m128i *v_ptr = (__m128i *)v_input_ptr;

	// Initialize the output pointer for the packed YUV results
	__m128i *yuv_ptr = (__m128i *)yuv_output_ptr;

	for (; column < post_column; column += column_step)
	{
		//int chroma_column = column/2;

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
#if 0
		// Reduce the pixel values to eight bits
		u1_epi16 = _mm_srli_epi16(u1_epi16, descale);
		v1_epi16 = _mm_srli_epi16(v1_epi16, descale);
		y1_epi16 = _mm_srli_epi16(y1_epi16, descale);
		y2_epi16 = _mm_srli_epi16(y2_epi16, descale);
#endif
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

	// Check that the fast loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Need to handle the rest of the conversion outside of the fast loop
	y_input_ptr = (PIXEL16U *)y_ptr;
	u_input_ptr = (PIXEL16U *)u_ptr;
	v_input_ptr = (PIXEL16U *)v_ptr;
	yuv_output_ptr = (uint8_t *)yuv_ptr;

	for (; column < length; column += 2)
	{
		int y1 = *(y_input_ptr++);
		int y2 = *(y_input_ptr++);

		int u = *(u_input_ptr++);
		int v = *(v_input_ptr++);

		if(format == DECODED_FORMAT_UYVY)
		{
			*(yuv_output_ptr++) = SATURATE_8U(u);
			*(yuv_output_ptr++) = SATURATE_8U(y1);
			*(yuv_output_ptr++) = SATURATE_8U(v);
			*(yuv_output_ptr++) = SATURATE_8U(y2);
		}
		else
		{
			*(yuv_output_ptr++) = SATURATE_8U(y1);
			*(yuv_output_ptr++) = SATURATE_8U(u);
			*(yuv_output_ptr++) = SATURATE_8U(y2);
			*(yuv_output_ptr++) = SATURATE_8U(v);
		}
	}
}

#endif



#if 0	//_PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

// Convert one row of 16-bit unpacked luma and chroma into 8-bit RGB
void ConvertUnpacked16sRowToRGB24(PIXEL **channel_row_ptr, int num_channels,
								  uint8_t *output_row_ptr, int width, int descale,
								  int format, int color_space)
{
	// Stub routine for processor specific dispatch
}

#endif


#if 1	//_PROCESSOR_GENERIC

#if 0	//_PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Convert one row of 16-bit unpacked luma and chroma into 8-bit RGB
void ConvertUnpacked16sRowToRGB24(PIXEL **channel_row_ptr, int num_channels,
								  uint8_t *output_row_ptr, int width, int descale,
								  int format, int color_space)
{
	// Note: This routine swaps the chroma values
	PIXEL16U *y_input_ptr = (PIXEL16U *)channel_row_ptr[0];
	PIXEL16U *u_input_ptr = (PIXEL16U *)channel_row_ptr[2];
	PIXEL16U *v_input_ptr = (PIXEL16U *)channel_row_ptr[1];

	uint8_t *rgb_output_ptr = output_row_ptr;

	int y_offset;
	int ymult;
	int r_vmult;
	int g_vmult;
	int g_umult;
	int b_umult;
	int saturate;

	// Select the appropriate color conversion coefficients (u is Cb and v is Cr);
	switch(color_space & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:	// Computer systems 601
		y_offset = 16;
		ymult = 128*149;		// 7-bit 1.164
		r_vmult = 204;			// 7-bit 1.596
		g_vmult = 208;			// 8-bit 0.813
		g_umult = 100;			// 8-bit 0.391
		b_umult = 129;			// 6-bit 2.018
		saturate = 1;
		break;

	default: assert(0);
	case COLOR_SPACE_CG_709:	// Computer systems 709
		y_offset = 16;
		ymult = 128*149;		// 7-bit 1.164
		r_vmult = 230;			// 7-bit 1.793
		g_vmult = 137;			// 8-bit 0.534
		g_umult = 55;			// 8-bit 0.213
		b_umult = 135;			// 8-bit 2.115
		saturate = 1;
		break;

	case COLOR_SPACE_VS_601:	// Video systems 601
		y_offset = 0;
		ymult = 128*128;		// 7-bit 1.000
		r_vmult = 175;			// 7-bit 1.371
		g_vmult = 179;			// 8-bit 0.698
		g_umult = 86;			// 8-bit 0.336
		b_umult = 111;			// 8-bit 1.732
		saturate = 0;
		break;

	case COLOR_SPACE_VS_709:	// Video systems 709
		y_offset = 0;
		ymult = 128*128;		// 7-bit 1.000
		r_vmult = 197;			// 7-bit 1.540
		g_vmult = 118;			// 8-bit 0.459
		g_umult = 47;			// 8-bit 0.183
		b_umult = 116;			// 8-bit 1.816
		saturate = 0;
		break;
	 }

	 // Check that the correct compiler time switches are set correctly
#if (_USE_YCBCR == 0) || (_ENABLE_GAMMA_CORRECTION == 1)
#error Must have YCBCR set to zero or gamma correction set to one
	assert(0);
#endif

	// We use 16-bit fixed-point arithmetic to approximate the color conversion coefficients
	//
	// Floating point arithmetic is
	//
	// R = 1.164 * (Y - 16) + 1.596 * (V - 128);
	// G = 1.164 * (Y - 16) - 0.813 * (V - 128) - 0.392 * (U - 128);
	// B = 1.164 * (Y - 16) + 2.017 * (U - 128);
	//
	// Fixed point approximation (8-bit) is
	//
	// Y = (Y << 1) -  32;
	// U = (U << 1) - 256;
	// V = (V << 1) - 256;
	// R = (149 * Y + 204 * V) >> 8;
	// G = (149 * Y - 104 * V - 50 * U) >> 8;
	// B = (149 * Y + 258 * U) >> 8;
	//
	// Fixed point approximation (7-bit) is
	//
	// Y = (Y << 1) -  16;
	// U = (U << 1) - 256;
	// V = (V << 1) - 256;
	// R = (74 * Y + 102 * V) >> 7;
	// G = (74 * Y -  52 * V - 25 * U) >> 7;
	// B = (74 * Y + 129 * U) >> 7;
	//
	// We use 7-bit arithmetic

	// New 7 bit version to fix rounding errors 2/26/03
	// Y = Y - 16 ;
	// U = U - 128;
	// V = V - 128;
	// R = (149 * Y           + 204 * V) >> 7;
	// G = (149 * Y -  50 * U - 104 * V) >> 7;
	// B = (149 * Y + 258 * U) >> 7;
	//
	// New 6 bit version to fix rounding errors
	// Y = Y - 16 ;
	// U = U - 128;
	// V = V - 128;
	// R = ((149 * Y>>1)           + 102 * V) >> 6;
	// G = ((149 * Y>>1) -  25 * U - 52 * V) >> 6;
	// B = ((149 * Y>>1) + 129 * U) >> 6;



	// Bt.709
	// R = 1.164 * (Y - 16)                     + 1.793 * (V - 128);
	// G = 1.164 * (Y - 16) - 0.213 * (U - 128) - 0.534 * (V - 128);
	// B = 1.164 * (Y - 16) + 2.115 * (U - 128);
	//
	//
	// We use 7-bit arithmetic
	// Y = Y - 16;
	// U = U - 128;
	// V = V - 128;
	// R = (149 * Y           + 229 * V) >> 7;     // 229.5
	// G = (149 * Y -  27 * U - 68 * V) >> 7;		 //27.264  68.35
	// B = (149 * Y + 271 * U) >> 7;				 // 270.72
	//
	// New 6 bit version to fix rounding errors
	// Y = Y - 16 ;
	// U = U - 128;
	// V = V - 128;
	// R = ((149 * Y>>1)           + 115 * V - (V>>2)) >> 6;		//114.752
	// G = ((149 * Y>>1) -  ((109*U)>>3) - ((137*V)>>2) + (V>>2)) >> 6;			//13.632 approx 8 * 13.632 = 109,  137-0.25
	// B = ((149 * Y>>1) + 135 * U + (U>>2)) >> 6;
	//
	// New 6 bit version crude
	// Y = Y - 16 ;
	// U = U - 128;
	// V = V - 128;
	// R = ((149 * Y>>1)           + 115 * V ) >> 6;		//114.752
	// G = ((149 * Y>>1) -  14 * U -  34 * V ) >> 6;		//13.632 approx 8 * 13.632 = 109,  137-0.25
	// B = ((149 * Y>>1) + 135 * U           ) >> 6;


	// Only 24 bit true color RGB is supported
	assert(format == COLOR_FORMAT_RGB24);

	// Output to RGB24 format?
	if (format == COLOR_FORMAT_RGB24)
	{
		//int y_prescale = descale + PRESCALE_LUMA;
		//int u_prescale = descale + PRESCALE_CHROMA;
		//int v_prescale = descale + PRESCALE_CHROMA;

		// Start processing at the leftmost column
		int column = 0;


#if (0 && XMMOPT) //DANREMOVED

		int column_step = 16;
		int post_column = width - (width % column_step);

		// Initialize the input pointers into each channel
		__m64 *y_ptr = (__m64 *)y_input_ptr;
		__m64 *u_ptr = (__m64 *)u_input_ptr;
		__m64 *v_ptr = (__m64 *)v_input_ptr;

		// Initialize the output pointer for the RGB results
		__m64 *rgb_ptr = (__m64 *)rgb_output_ptr;

		__m64 luma_offset = _mm_set1_pi16(y_offset);
		__m64 chroma_offset = _mm_set1_pi16(128);

		__m64 ymult_pi16 = _mm_set1_pi16(ymult);
		__m64 crv_pi16 = _mm_set1_pi16(r_vmult);
		__m64 cgv_pi16 = _mm_set1_pi16(g_vmult);
		__m64 cgu_pi16 = _mm_set1_pi16(g_umult);
		__m64 cbu_pi16 = _mm_set1_pi16(b_umult);

		for (; column < post_column; column += column_step)
		{
			__m64 y1_pi16;
			__m64 y2_pi16;

			__m64 u1_pi16;
			__m64 u2_pi16;

			__m64 v1_pi16;
			__m64 v2_pi16;

			__m64 y_pi8;
			__m64 u_pi8;
			__m64 v_pi8;

			__m64 r1_pi16;
			__m64 g1_pi16;
			__m64 b1_pi16;

			__m64 r2_pi16;
			__m64 g2_pi16;
			__m64 b2_pi16;

			__m64 t1_pi16;
			__m64 t2_pi16;

			__m64 r1_pi8;
			__m64 g1_pi8;
			__m64 b1_pi8;

			__m64 r2_pi8;
			__m64 g2_pi8;
			__m64 b2_pi8;

			__m64 b3_pi8;

			__m64 t1_pi8;
			__m64 t2_pi8;
			__m64 t3_pi8;

			__m64 p1_pi8;
			__m64 p2_pi8;
			__m64 p3_pi8;

			__m64 rgb1_pi8;
			__m64 rgb2_pi8;
			__m64 rgb3_pi8;


			/***** Load the first eight YCbCr values *****/

			// Convert 16-bit signed lowpass pixels into 8-bit unsigned frame pixels
			y1_pi16 = *(y_ptr++);
			y2_pi16 = *(y_ptr++);
			y1_pi16 = _mm_srai_pi16(y1_pi16, descale);
			y2_pi16 = _mm_srai_pi16(y2_pi16, descale);
			y_pi8 = _mm_packs_pu16(y1_pi16, y2_pi16);

			u1_pi16 = *(u_ptr++);
			u2_pi16 = *(u_ptr++);
			u1_pi16 = _mm_srai_pi16(u1_pi16, descale);
			u2_pi16 = _mm_srai_pi16(u2_pi16, descale);
			u_pi8 = _mm_packs_pu16(u1_pi16, u2_pi16);

			v1_pi16 = *(v_ptr++);
			v2_pi16 = *(v_ptr++);
			v1_pi16 = _mm_srai_pi16(v1_pi16, descale);
			v2_pi16 = _mm_srai_pi16(v2_pi16, descale);
			v_pi8 = _mm_packs_pu16(v1_pi16, v2_pi16);

#if STRICT_SATURATE
			// Perform strict saturation on YUV if required
			if (saturate)
			{
				y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(16));
				y_pi8 = _mm_adds_pu8(y_pi8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
				y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(20));

				u_pi8 = _mm_subs_pu8(u_pi8, _mm_set1_pi8(16));
				u_pi8 = _mm_adds_pu8(u_pi8, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
				u_pi8 = _mm_subs_pu8(u_pi8, _mm_set1_pi8(15));

				v_pi8 = _mm_subs_pu8(v_pi8, _mm_set1_pi8(16));
				v_pi8 = _mm_adds_pu8(v_pi8, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
				v_pi8 = _mm_subs_pu8(v_pi8, _mm_set1_pi8(15));
			}
#endif

			/***** Calculate the first four RGB values *****/

			// Unpack the first four luma values
			y1_pi16 = _mm_unpacklo_pi8(y_pi8, _mm_setzero_si64());

			// Unpack the first four chroma values
			u2_pi16 = _mm_unpacklo_pi8(u_pi8, _mm_setzero_si64());
			v2_pi16 = _mm_unpacklo_pi8(v_pi8, _mm_setzero_si64());

			// Duplicate the first two chroma values
			u1_pi16 = _mm_shuffle_pi16(u2_pi16, _MM_SHUFFLE(1, 1, 0, 0));
			v1_pi16 = _mm_shuffle_pi16(v2_pi16, _MM_SHUFFLE(1, 1, 0, 0));

			// Subtract the luma and chroma offsets
			y1_pi16 = _mm_subs_pi16(y1_pi16, luma_offset);
			u1_pi16 = _mm_subs_pi16(u1_pi16, chroma_offset);
			v1_pi16 = _mm_subs_pi16(v1_pi16, chroma_offset);

			// This code fixes a case of overflow where very bright pixels
			// with some chroma produced interim values that exceeded 32768
			y1_pi16 = _mm_slli_pi16(y1_pi16, 7);
			y1_pi16 = _mm_mulhi_pi16(y1_pi16, ymult_pi16);
			y1_pi16 = _mm_slli_pi16(y1_pi16, 1);

			// Calculate red
			r1_pi16 = _mm_mullo_pi16(crv_pi16, v1_pi16);
			r1_pi16 = _mm_srai_pi16(r1_pi16, 1);			 // Reduce 7 bits to 6
			r1_pi16 = _mm_adds_pi16(r1_pi16, y1_pi16);
			r1_pi16 = _mm_srai_pi16(r1_pi16, 6);

			// Calculate green
			g1_pi16 = _mm_mullo_pi16(cgv_pi16, v1_pi16);
			g1_pi16 = _mm_srai_pi16(g1_pi16, 2);			// Reduce 8 bits to 6
			g1_pi16 = _mm_subs_pi16(y1_pi16, g1_pi16);
			t1_pi16 = _mm_mullo_pi16(cgu_pi16, u1_pi16);
			t1_pi16 = _mm_srai_pi16(t1_pi16, 2);			// Reduce 8 bits to 6
			g1_pi16 = _mm_subs_pi16(g1_pi16, t1_pi16);
			g1_pi16 = _mm_srai_pi16(g1_pi16, 6);

			// Calculate blue
			b1_pi16 = _mm_mullo_pi16(cbu_pi16, u1_pi16);
			b1_pi16 = _mm_adds_pi16(b1_pi16, y1_pi16);
			b1_pi16 = _mm_srai_pi16(b1_pi16, 6);


			/***** Calculate the second four RGB values *****/

			// Unpack the second four luma values
			y2_pi16 = _mm_unpackhi_pi8(y_pi8, _mm_setzero_si64());

			// Duplicate the second two chroma values
			u2_pi16 = _mm_shuffle_pi16(u2_pi16, _MM_SHUFFLE(3, 3, 2, 2));
			v2_pi16 = _mm_shuffle_pi16(v2_pi16, _MM_SHUFFLE(3, 3, 2, 2));

			y2_pi16 = _mm_subs_pi16(y2_pi16, luma_offset);
			u2_pi16 = _mm_subs_pi16(u2_pi16, chroma_offset);
			v2_pi16 = _mm_subs_pi16(v2_pi16, chroma_offset);

			// This code fixes a case of overflow where very bright pixels
			// with some chroma produced interim values that exceeded 32768
			y2_pi16 = _mm_slli_pi16(y2_pi16, 7);
			y2_pi16 = _mm_mulhi_pi16(y2_pi16, ymult_pi16);
			y2_pi16 = _mm_slli_pi16(y2_pi16, 1);

			// Calculate red
			r2_pi16 = _mm_mullo_pi16(crv_pi16, v2_pi16);
			r2_pi16 = _mm_srai_pi16(r2_pi16, 1);			 // Reduce 7 bits to 6
			r2_pi16 = _mm_adds_pi16(r2_pi16, y2_pi16);
			r2_pi16 = _mm_srai_pi16(r2_pi16, 6);

			// Calculate green
			g2_pi16 = _mm_mullo_pi16(cgv_pi16, v2_pi16);
			g2_pi16 = _mm_srai_pi16(g2_pi16, 2);			// Reduce 8 bits to 6
			g2_pi16 = _mm_subs_pi16(y2_pi16, g2_pi16);
			t2_pi16 = _mm_mullo_pi16(cgu_pi16, u2_pi16);
			t2_pi16 = _mm_srai_pi16(t2_pi16, 2);			// Reduce 8 bits to 6
			g2_pi16 = _mm_subs_pi16(g2_pi16, t2_pi16);
			g2_pi16 = _mm_srai_pi16(g2_pi16, 6);

			// Calculate blue
			b2_pi16 = _mm_mullo_pi16(cbu_pi16, u2_pi16);
			b2_pi16 = _mm_adds_pi16(b2_pi16, y2_pi16);
			b2_pi16 = _mm_srai_pi16(b2_pi16, 6);


			/***** Pack and store the first eight RGB tuples *****/
#if 0
			// Pack the RGB values into bytes
			r1_pi8 = _mm_packs_pu16(r1_pi16, r2_pi16);
			g1_pi8 = _mm_packs_pu16(g1_pi16, g2_pi16);
			b1_pi8 = _mm_packs_pu16(b1_pi16, b2_pi16);
#else
			// Red and blue are reversed on Intel processors

			// Pack the RGB values into bytes
			r1_pi8 = _mm_packs_pu16(b1_pi16, b2_pi16);
			g1_pi8 = _mm_packs_pu16(g1_pi16, g2_pi16);
			b1_pi8 = _mm_packs_pu16(r1_pi16, r2_pi16);
#endif

#if (0 && DEBUG)
			r1_pi8 = _mm_setr_pi8(0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8);
			g1_pi8 = _mm_setr_pi8(0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8);
			b1_pi8 = _mm_setr_pi8(0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8);
#endif
			r2_pi8 = r1_pi8;
			g2_pi8 = g1_pi8;
			b2_pi8 = b1_pi8;

			// Discard every second value
			r1_pi8 = _mm_slli_pi16(r1_pi8, 8);
			r1_pi8 = _mm_srli_pi16(r1_pi8, 8);
			r1_pi8 = _mm_packs_pu16(r1_pi8, _mm_setzero_si64());

			g1_pi8 = _mm_slli_pi16(g1_pi8, 8);
			g1_pi8 = _mm_srli_pi16(g1_pi8, 8);
			g1_pi8 = _mm_packs_pu16(g1_pi8, _mm_setzero_si64());

			b1_pi8 = _mm_slli_pi16(b1_pi8, 8);
			b1_pi8 = _mm_srli_pi16(b1_pi8, 8);
			b1_pi8 = _mm_packs_pu16(b1_pi8, _mm_setzero_si64());

			// Discard every second value starting with the first
			r2_pi8 = _mm_srli_pi16(r2_pi8, 8);
			r2_pi8 = _mm_packs_pu16(r2_pi8, _mm_setzero_si64());

			//b3_pi8 = b1_pi8;


			// Interleave pairs of values
			p1_pi8 = _mm_unpacklo_pi8(r1_pi8, g1_pi8);

			g2_pi8 = _mm_srli_pi16(g2_pi8, 8);
			g2_pi8 = _mm_packs_pu16(g2_pi8, _mm_setzero_si64());

			b2_pi8 = _mm_srli_pi16(b2_pi8, 8);
			b2_pi8 = _mm_packs_pu16(b2_pi8, _mm_setzero_si64());

			p2_pi8 = _mm_unpacklo_pi8(g2_pi8, b2_pi8);

			//p3_pi8 = _mm_unpacklo_pi8(b3_pi8, r2_pi8);
			p3_pi8 = _mm_unpacklo_pi8(b1_pi8, r2_pi8);


			// Combine pairs of values to form the first RGB group
			t1_pi8 = _mm_unpacklo_pi16(p2_pi8, p3_pi8);

			rgb1_pi8 = _mm_unpacklo_pi16(p1_pi8, t1_pi8);
			rgb1_pi8 = _mm_shuffle_pi16(rgb1_pi8, _MM_SHUFFLE(2, 1, 3, 0));

			// Store the first group of RGB values
			*(rgb_ptr++) = rgb1_pi8;


			// Combine pairs of values to form the second RGB group
			t2_pi8 = _mm_unpackhi_pi16(p1_pi8, p3_pi8);
			t3_pi8 = _mm_slli_si64(t2_pi8, 32);

			rgb2_pi8 = _mm_unpackhi_pi32(t1_pi8, t3_pi8);
			rgb2_pi8 = _mm_shuffle_pi16(rgb2_pi8, _MM_SHUFFLE(3, 2, 0, 1));

			// Store the second group of RGB values
			*(rgb_ptr++) = rgb2_pi8;


			// Combine pairs of values to form the third RGB group
			rgb3_pi8 = _mm_unpackhi_pi16(p2_pi8, t2_pi8);
			rgb3_pi8 = _mm_shuffle_pi16(rgb3_pi8, _MM_SHUFFLE(2, 3, 1, 0));

			// Store the third group of RGB values
			*(rgb_ptr++) = rgb3_pi8;


			/***** Load the second eight luma values *****/

			// Convert 16-bit signed lowpass pixels into 8-bit unsigned frame pixels
			y1_pi16 = *(y_ptr++);
			y2_pi16 = *(y_ptr++);
			y1_pi16 = _mm_srai_pi16(y1_pi16, descale);
			y2_pi16 = _mm_srai_pi16(y2_pi16, descale);
			y_pi8 = _mm_packs_pu16(y1_pi16, y2_pi16);

#if STRICT_SATURATE
			// Perform strict saturation on the new luma values if required
			if (saturate)
			{
				y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(16));
				y_pi8 = _mm_adds_pu8(y_pi8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
				y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(20));
			}
#endif

			/***** Calculate the third four RGB values *****/

			// Unpack the first four luma values
			y1_pi16 = _mm_unpacklo_pi8(y_pi8, _mm_setzero_si64());

			// Unpack the second four chroma values
			u2_pi16 = _mm_unpackhi_pi8(u_pi8, _mm_setzero_si64());
			v2_pi16 = _mm_unpackhi_pi8(v_pi8, _mm_setzero_si64());

			// Duplicate the first two chroma values
			u1_pi16 = _mm_shuffle_pi16(u2_pi16, _MM_SHUFFLE(1, 1, 0, 0));
			v1_pi16 = _mm_shuffle_pi16(v2_pi16, _MM_SHUFFLE(1, 1, 0, 0));

			// Subtract the luma and chroma offsets
			y1_pi16 = _mm_subs_pi16(y1_pi16, luma_offset);
			u1_pi16 = _mm_subs_pi16(u1_pi16, chroma_offset);
			v1_pi16 = _mm_subs_pi16(v1_pi16, chroma_offset);

			// This code fixes a case of overflow where very bright pixels
			// with some chroma produced interim values that exceeded 32768
			y1_pi16 = _mm_slli_pi16(y1_pi16, 7);
			y1_pi16 = _mm_mulhi_pi16(y1_pi16, ymult_pi16);
			y1_pi16 = _mm_slli_pi16(y1_pi16, 1);

			// Calculate red
			r1_pi16 = _mm_mullo_pi16(crv_pi16, v1_pi16);
			r1_pi16 = _mm_srai_pi16(r1_pi16, 1);			 // Reduce 7 bits to 6
			r1_pi16 = _mm_adds_pi16(r1_pi16, y1_pi16);
			r1_pi16 = _mm_srai_pi16(r1_pi16, 6);

			// Calculate green
			g1_pi16 = _mm_mullo_pi16(cgv_pi16, v1_pi16);
			g1_pi16 = _mm_srai_pi16(g1_pi16, 2);			// Reduce 8 bits to 6
			g1_pi16 = _mm_subs_pi16(y1_pi16, g1_pi16);
			t1_pi16 = _mm_mullo_pi16(cgu_pi16, u1_pi16);
			t1_pi16 = _mm_srai_pi16(t1_pi16, 2);			// Reduce 8 bits to 6
			g1_pi16 = _mm_subs_pi16(g1_pi16, t1_pi16);
			g1_pi16 = _mm_srai_pi16(g1_pi16, 6);

			// Calculate blue
			b1_pi16 = _mm_mullo_pi16(cbu_pi16, u1_pi16);
			b1_pi16 = _mm_adds_pi16(b1_pi16, y1_pi16);
			b1_pi16 = _mm_srai_pi16(b1_pi16, 6);


			/***** Calculate the fourth four RGB values *****/

			// Unpack the second four luma values
			y2_pi16 = _mm_unpackhi_pi8(y_pi8, _mm_setzero_si64());

			// Duplicate the second two chroma values
			u2_pi16 = _mm_shuffle_pi16(u2_pi16, _MM_SHUFFLE(3, 3, 2, 2));
			v2_pi16 = _mm_shuffle_pi16(v2_pi16, _MM_SHUFFLE(3, 3, 2, 2));

			y2_pi16 = _mm_subs_pi16(y2_pi16, luma_offset);
			u2_pi16 = _mm_subs_pi16(u2_pi16, chroma_offset);
			v2_pi16 = _mm_subs_pi16(v2_pi16, chroma_offset);

			// This code fixes a case of overflow where very bright pixels
			// with some chroma produced interim values that exceeded 32768
			y2_pi16 = _mm_slli_pi16(y2_pi16, 7);
			y2_pi16 = _mm_mulhi_pi16(y2_pi16, ymult_pi16);
			y2_pi16 = _mm_slli_pi16(y2_pi16, 1);

			// Calculate red
			r2_pi16 = _mm_mullo_pi16(crv_pi16, v2_pi16);
			r2_pi16 = _mm_srai_pi16(r2_pi16, 1);			 // Reduce 7 bits to 6
			r2_pi16 = _mm_adds_pi16(r2_pi16, y2_pi16);
			r2_pi16 = _mm_srai_pi16(r2_pi16, 6);

			// Calculate green
			g2_pi16 = _mm_mullo_pi16(cgv_pi16, v2_pi16);
			g2_pi16 = _mm_srai_pi16(g2_pi16, 2);			// Reduce 8 bits to 6
			g2_pi16 = _mm_subs_pi16(y2_pi16, g2_pi16);
			t2_pi16 = _mm_mullo_pi16(cgu_pi16, u2_pi16);
			t2_pi16 = _mm_srai_pi16(t2_pi16, 2);			// Reduce 8 bits to 6
			g2_pi16 = _mm_subs_pi16(g2_pi16, t2_pi16);
			g2_pi16 = _mm_srai_pi16(g2_pi16, 6);

			// Calculate blue
			b2_pi16 = _mm_mullo_pi16(cbu_pi16, u2_pi16);
			b2_pi16 = _mm_adds_pi16(b2_pi16, y2_pi16);
			b2_pi16 = _mm_srai_pi16(b2_pi16, 6);


			/***** Pack and store the second eight RGB tuples *****/
#if 0
			// Pack the RGB values into bytes
			r1_pi8 = _mm_packs_pu16(r1_pi16, r2_pi16);
			g1_pi8 = _mm_packs_pu16(g1_pi16, g2_pi16);
			b1_pi8 = _mm_packs_pu16(b1_pi16, b2_pi16);
#else
			// Red and blue are reversed on Intel processors

			// Pack the RGB values into bytes
			r1_pi8 = _mm_packs_pu16(b1_pi16, b2_pi16);
			g1_pi8 = _mm_packs_pu16(g1_pi16, g2_pi16);
			b1_pi8 = _mm_packs_pu16(r1_pi16, r2_pi16);
#endif

#if (0 && DEBUG)
			r1_pi8 = _mm_setr_pi8(0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8);
			g1_pi8 = _mm_setr_pi8(0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8);
			b1_pi8 = _mm_setr_pi8(0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8);
#endif
			r2_pi8 = r1_pi8;
			g2_pi8 = g1_pi8;
			b2_pi8 = b1_pi8;

			// Discard every second value
			r1_pi8 = _mm_slli_pi16(r1_pi8, 8);
			r1_pi8 = _mm_srli_pi16(r1_pi8, 8);
			r1_pi8 = _mm_packs_pu16(r1_pi8, _mm_setzero_si64());

			g1_pi8 = _mm_slli_pi16(g1_pi8, 8);
			g1_pi8 = _mm_srli_pi16(g1_pi8, 8);
			g1_pi8 = _mm_packs_pu16(g1_pi8, _mm_setzero_si64());

			b1_pi8 = _mm_slli_pi16(b1_pi8, 8);
			b1_pi8 = _mm_srli_pi16(b1_pi8, 8);
			b1_pi8 = _mm_packs_pu16(b1_pi8, _mm_setzero_si64());

			// Discard every second value starting with the first
			r2_pi8 = _mm_srli_pi16(r2_pi8, 8);
			r2_pi8 = _mm_packs_pu16(r2_pi8, _mm_setzero_si64());

			//b3_pi8 = b1_pi8;


			// Interleave pairs of values
			p1_pi8 = _mm_unpacklo_pi8(r1_pi8, g1_pi8);

			g2_pi8 = _mm_srli_pi16(g2_pi8, 8);
			g2_pi8 = _mm_packs_pu16(g2_pi8, _mm_setzero_si64());

			b2_pi8 = _mm_srli_pi16(b2_pi8, 8);
			b2_pi8 = _mm_packs_pu16(b2_pi8, _mm_setzero_si64());

			p2_pi8 = _mm_unpacklo_pi8(g2_pi8, b2_pi8);

			//p3_pi8 = _mm_unpacklo_pi8(b3_pi8, r2_pi8);
			p3_pi8 = _mm_unpacklo_pi8(b1_pi8, r2_pi8);


			// Combine pairs of values to form the first RGB group
			t1_pi8 = _mm_unpacklo_pi16(p2_pi8, p3_pi8);

			rgb1_pi8 = _mm_unpacklo_pi16(p1_pi8, t1_pi8);
			rgb1_pi8 = _mm_shuffle_pi16(rgb1_pi8, _MM_SHUFFLE(2, 1, 3, 0));

			// Store the first group of RGB values
			*(rgb_ptr++) = rgb1_pi8;


			// Combine pairs of values to form the second RGB group
			t2_pi8 = _mm_unpackhi_pi16(p1_pi8, p3_pi8);
			t3_pi8 = _mm_slli_si64(t2_pi8, 32);

			rgb2_pi8 = _mm_unpackhi_pi32(t1_pi8, t3_pi8);
			rgb2_pi8 = _mm_shuffle_pi16(rgb2_pi8, _MM_SHUFFLE(3, 2, 0, 1));

			// Store the second group of RGB values
			*(rgb_ptr++) = rgb2_pi8;


			// Combine pairs of values to form the third RGB group
			rgb3_pi8 = _mm_unpackhi_pi16(p2_pi8, t2_pi8);
			rgb3_pi8 = _mm_shuffle_pi16(rgb3_pi8, _MM_SHUFFLE(2, 3, 1, 0));

			// Store the third group of RGB values
			*(rgb_ptr++) = rgb3_pi8;
		}

		// Clear the MMX registers
		//_mm_empty();

		// Check that the loop ends at the right position
		assert(column == post_column);
#endif
#if 1
		// Process the rest of the row with 7-bit fixed point arithmetic
		for (; column < width; column += 2)
		{
			uint8_t *rgb_ptr = &rgb_output_ptr[column * 3];
			int y, u, v;
			int r, g, b;

			y = y_input_ptr[column];
			u = u_input_ptr[column/2];
			v = v_input_ptr[column/2];

			// Convert the pixels values to 8 bits
			y >>= descale;
			u >>= descale;
			v >>= descale;

			// Convert the first set of YCbCr values
			if (saturate)
			{
				y = SATURATE_Y(y);
				u = SATURATE_Cr(u);
				v = SATURATE_Cb(v);
			}

			y = y - y_offset;
			u = u - 128;
			v = v - 128;

			y = y * ymult >> 7;

			r = (    y                + r_vmult * v) >> 7;
			g = (2 * y -  g_umult * u - g_vmult * v) >> 8;
			b = (    y            + 2 * b_umult * u) >> 7;

			*(rgb_ptr++) = SATURATE_8U(b);
			*(rgb_ptr++) = SATURATE_8U(g);
			*(rgb_ptr++) = SATURATE_8U(r);

			// Convert the second set of YCbCr values
			y = y_input_ptr[column+1];
			y >>= descale;
			if (saturate) {
				y = SATURATE_Y(y);
			}

			y = y - y_offset;
			y = y * ymult >> 7;

			r = (    y                + r_vmult * v) >> 7;
			g = (2 * y -  g_umult * u - g_vmult * v) >> 8;
			b = (    y            + 2 * b_umult * u) >> 7;

			*(rgb_ptr++) = SATURATE_8U(b);
			*(rgb_ptr++) = SATURATE_8U(g);
			*(rgb_ptr++) = SATURATE_8U(r);
		}
#endif
#if 0
		// Fill the rest of the output row with black
		for (; column < output_width; column++)
		{
			uint8_t *rgb_ptr = &rgb_row_ptr[column * 3];

			*(rgb_ptr++) = 0;
			*(rgb_ptr++) = 0;
			*(rgb_ptr++) = 0;
		}
#endif
	}
}

#endif


#if 0	//_PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Convert one row of 16-bit unpacked luma and chroma into 8-bit RGB24
void ConvertUnpacked16sRowToRGB24(PIXEL **channel_row_ptr, int num_channels,
								  uint8_t *output_row_ptr, int width, int descale,
								  int format, int color_space)
{
	// Have not coded SSE2 versions of the color conversion code
	assert(0);
}

#endif


#if 0	//_PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

// Convert one row of 16-bit unpacked luma and chroma into 8-bit RGBA32
void ConvertUnpacked16sRowToRGB32(PIXEL **channel_row_ptr, int num_channels,
								  uint8_t *output_row_ptr, int width, int descale,
								  int format, int color_space, int alpha)
{
	// Stub routine for processor specific dispatch
}

#endif


#if 1	//_PROCESSOR_GENERIC

#if 0	//_PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Convert one row of 16-bit unpacked luma and chroma into 8-bit RGBA32
void ConvertUnpacked16sRowToRGB32(PIXEL **channel_row_ptr, int num_channels,
								  uint8_t *output_row_ptr, int width, int descale,
								  int format, int color_space, int alpha)
{
	// Note: This routine swaps the chroma values
	PIXEL16U *y_input_ptr = (PIXEL16U *)channel_row_ptr[0];
	PIXEL16U *u_input_ptr = (PIXEL16U *)channel_row_ptr[2];
	PIXEL16U *v_input_ptr = (PIXEL16U *)channel_row_ptr[1];

	uint8_t *rgba_output_ptr = output_row_ptr;

	int y_offset;
	int ymult;
	int r_vmult;
	int g_vmult;
	int g_umult;
	int b_umult;
	int saturate;

	// Select the appropriate color conversion coefficients (u is Cb and v is Cr);
	switch(color_space & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:	// Computer systems 601
		y_offset = 16;
		ymult = 128*149;		// 7-bit 1.164
		r_vmult = 204;			// 7-bit 1.596
		g_vmult = 208;			// 8-bit 0.813
		g_umult = 100;			// 8-bit 0.391
		b_umult = 129;			// 6-bit 2.018
		saturate = 1;
		break;

	default: assert(0);
	case COLOR_SPACE_CG_709:	// Computer systems 709
		y_offset = 16;
		ymult = 128*149;		// 7-bit 1.164
		r_vmult = 230;			// 7-bit 1.793
		g_vmult = 137;			// 8-bit 0.534
		g_umult = 55;			// 8-bit 0.213
		b_umult = 135;			// 8-bit 2.115
		saturate = 1;
		break;

	case COLOR_SPACE_VS_601:	// Video systems 601
		y_offset = 0;
		ymult = 128*128;		// 7-bit 1.000
		r_vmult = 175;			// 7-bit 1.371
		g_vmult = 179;			// 8-bit 0.698
		g_umult = 86;			// 8-bit 0.336
		b_umult = 111;			// 8-bit 1.732
		saturate = 0;
		break;

	case COLOR_SPACE_VS_709:	// Video systems 709
		y_offset = 0;
		ymult = 128*128;		// 7-bit 1.000
		r_vmult = 197;			// 7-bit 1.540
		g_vmult = 118;			// 8-bit 0.459
		g_umult = 47;			// 8-bit 0.183
		b_umult = 116;			// 8-bit 1.816
		saturate = 0;
		break;
	 }

	 // Check that the correct compiler time switches are set correctly
#if (_USE_YCBCR == 0) || (_ENABLE_GAMMA_CORRECTION == 1)
#error Must have YCBCR set to zero or gamma correction set to one
	assert(0);
#endif

	// We use 16-bit fixed-point arithmetic to approximate the color conversion coefficients
	//
	// Floating point arithmetic is
	//
	// R = 1.164 * (Y - 16) + 1.596 * (V - 128);
	// G = 1.164 * (Y - 16) - 0.813 * (V - 128) - 0.392 * (U - 128);
	// B = 1.164 * (Y - 16) + 2.017 * (U - 128);
	//
	// Fixed point approximation (8-bit) is
	//
	// Y = (Y << 1) -  32;
	// U = (U << 1) - 256;
	// V = (V << 1) - 256;
	// R = (149 * Y + 204 * V) >> 8;
	// G = (149 * Y - 104 * V - 50 * U) >> 8;
	// B = (149 * Y + 258 * U) >> 8;
	//
	// Fixed point approximation (7-bit) is
	//
	// Y = (Y << 1) -  16;
	// U = (U << 1) - 256;
	// V = (V << 1) - 256;
	// R = (74 * Y + 102 * V) >> 7;
	// G = (74 * Y -  52 * V - 25 * U) >> 7;
	// B = (74 * Y + 129 * U) >> 7;
	//
	// We use 7-bit arithmetic

	// New 7 bit version to fix rounding errors 2/26/03
	// Y = Y - 16 ;
	// U = U - 128;
	// V = V - 128;
	// R = (149 * Y           + 204 * V) >> 7;
	// G = (149 * Y -  50 * U - 104 * V) >> 7;
	// B = (149 * Y + 258 * U) >> 7;
	//
	// New 6 bit version to fix rounding errors
	// Y = Y - 16 ;
	// U = U - 128;
	// V = V - 128;
	// R = ((149 * Y>>1)           + 102 * V) >> 6;
	// G = ((149 * Y>>1) -  25 * U - 52 * V) >> 6;
	// B = ((149 * Y>>1) + 129 * U) >> 6;



	// Bt.709
	// R = 1.164 * (Y - 16)                     + 1.793 * (V - 128);
	// G = 1.164 * (Y - 16) - 0.213 * (U - 128) - 0.534 * (V - 128);
	// B = 1.164 * (Y - 16) + 2.115 * (U - 128);
	//
	//
	// We use 7-bit arithmetic
	// Y = Y - 16;
	// U = U - 128;
	// V = V - 128;
	// R = (149 * Y           + 229 * V) >> 7;     // 229.5
	// G = (149 * Y -  27 * U - 68 * V) >> 7;		 //27.264  68.35
	// B = (149 * Y + 271 * U) >> 7;				 // 270.72
	//
	// New 6 bit version to fix rounding errors
	// Y = Y - 16 ;
	// U = U - 128;
	// V = V - 128;
	// R = ((149 * Y>>1)           + 115 * V - (V>>2)) >> 6;		//114.752
	// G = ((149 * Y>>1) -  ((109*U)>>3) - ((137*V)>>2) + (V>>2)) >> 6;			//13.632 approx 8 * 13.632 = 109,  137-0.25
	// B = ((149 * Y>>1) + 135 * U + (U>>2)) >> 6;
	//
	// New 6 bit version crude
	// Y = Y - 16 ;
	// U = U - 128;
	// V = V - 128;
	// R = ((149 * Y>>1)           + 115 * V ) >> 6;		//114.752
	// G = ((149 * Y>>1) -  14 * U -  34 * V ) >> 6;		//13.632 approx 8 * 13.632 = 109,  137-0.25
	// B = ((149 * Y>>1) + 135 * U           ) >> 6;


	// Only 24 bit true color RGB is supported
	assert(format == COLOR_FORMAT_RGB32);

	// Output to RGB24 format?
	if (format == COLOR_FORMAT_RGB32)
	{
		//int y_prescale = descale + PRESCALE_LUMA;
		//int u_prescale = descale + PRESCALE_CHROMA;
		//int v_prescale = descale + PRESCALE_CHROMA;

		// Start processing at the leftmost column
		int column = 0;

#if (0 && XMMOPT) //DANREMOVED

		int column_step = 16;
		int post_column = width - (width % column_step);

		// Initialize the input pointers into each channel
		__m64 *y_ptr = (__m64 *)y_input_ptr;
		__m64 *u_ptr = (__m64 *)u_input_ptr;
		__m64 *v_ptr = (__m64 *)v_input_ptr;

		// Initialize the output pointer for the RGB results
		__m64 *rgba_ptr = (__m64 *)rgba_output_ptr;

		__m64 luma_offset = _mm_set1_pi16(y_offset);
		__m64 chroma_offset = _mm_set1_pi16(128);

		__m64 ymult_pi16 = _mm_set1_pi16(ymult);
		__m64 crv_pi16 = _mm_set1_pi16(r_vmult);
		__m64 cgv_pi16 = _mm_set1_pi16(g_vmult);
		__m64 cgu_pi16 = _mm_set1_pi16(g_umult);
		__m64 cbu_pi16 = _mm_set1_pi16(b_umult);

		for (; column < post_column; column += column_step)
		{
			__m64 y1_pi16;
			__m64 y2_pi16;

			__m64 u1_pi16;
			__m64 u2_pi16;

			__m64 v1_pi16;
			__m64 v2_pi16;

			__m64 y_pi8;
			__m64 u_pi8;
			__m64 v_pi8;

			__m64 r1_pi16;
			__m64 g1_pi16;
			__m64 b1_pi16;

			__m64 r2_pi16;
			__m64 g2_pi16;
			__m64 b2_pi16;

			__m64 t1_pi16;
			__m64 t2_pi16;

			__m64 r1_pi8;
			__m64 g1_pi8;
			__m64 b1_pi8;

			__m64 p1_pi8;
			__m64 p2_pi8;
			__m64 p3_pi8;
			__m64 p4_pi8;

			__m64 rgba1_pi8;
			__m64 rgba2_pi8;
			__m64 rgba3_pi8;
			__m64 rgba4_pi8;

			__m64 alpha_pi8 = _mm_set1_pi8(alpha);


			/***** Load the first eight YCbCr values *****/

			// Convert 16-bit signed lowpass pixels into 8-bit unsigned frame pixels
			y1_pi16 = *(y_ptr++);
			y2_pi16 = *(y_ptr++);
			y1_pi16 = _mm_srai_pi16(y1_pi16, descale);
			y2_pi16 = _mm_srai_pi16(y2_pi16, descale);
			y_pi8 = _mm_packs_pu16(y1_pi16, y2_pi16);

			u1_pi16 = *(u_ptr++);
			u2_pi16 = *(u_ptr++);
			u1_pi16 = _mm_srai_pi16(u1_pi16, descale);
			u2_pi16 = _mm_srai_pi16(u2_pi16, descale);
			u_pi8 = _mm_packs_pu16(u1_pi16, u2_pi16);

			v1_pi16 = *(v_ptr++);
			v2_pi16 = *(v_ptr++);
			v1_pi16 = _mm_srai_pi16(v1_pi16, descale);
			v2_pi16 = _mm_srai_pi16(v2_pi16, descale);
			v_pi8 = _mm_packs_pu16(v1_pi16, v2_pi16);

#if STRICT_SATURATE
			// Perform strict saturation on YUV if required
			if (saturate)
			{
				y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(16));
				y_pi8 = _mm_adds_pu8(y_pi8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
				y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(20));

				u_pi8 = _mm_subs_pu8(u_pi8, _mm_set1_pi8(16));
				u_pi8 = _mm_adds_pu8(u_pi8, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
				u_pi8 = _mm_subs_pu8(u_pi8, _mm_set1_pi8(15));

				v_pi8 = _mm_subs_pu8(v_pi8, _mm_set1_pi8(16));
				v_pi8 = _mm_adds_pu8(v_pi8, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
				v_pi8 = _mm_subs_pu8(v_pi8, _mm_set1_pi8(15));
			}
#endif

			/***** Calculate the first four RGB values *****/

			// Unpack the first four luma values
			y1_pi16 = _mm_unpacklo_pi8(y_pi8, _mm_setzero_si64());

			// Unpack the first four chroma values
			u2_pi16 = _mm_unpacklo_pi8(u_pi8, _mm_setzero_si64());
			v2_pi16 = _mm_unpacklo_pi8(v_pi8, _mm_setzero_si64());

			// Duplicate the first two chroma values
			u1_pi16 = _mm_shuffle_pi16(u2_pi16, _MM_SHUFFLE(1, 1, 0, 0));
			v1_pi16 = _mm_shuffle_pi16(v2_pi16, _MM_SHUFFLE(1, 1, 0, 0));

			// Subtract the luma and chroma offsets
			y1_pi16 = _mm_subs_pi16(y1_pi16, luma_offset);
			u1_pi16 = _mm_subs_pi16(u1_pi16, chroma_offset);
			v1_pi16 = _mm_subs_pi16(v1_pi16, chroma_offset);

			// This code fixes a case of overflow where very bright pixels
			// with some chroma produced interim values that exceeded 32768
			y1_pi16 = _mm_slli_pi16(y1_pi16, 7);
			y1_pi16 = _mm_mulhi_pi16(y1_pi16, ymult_pi16);
			y1_pi16 = _mm_slli_pi16(y1_pi16, 1);

			// Calculate red
			r1_pi16 = _mm_mullo_pi16(crv_pi16, v1_pi16);
			r1_pi16 = _mm_srai_pi16(r1_pi16, 1);			 // Reduce 7 bits to 6
			r1_pi16 = _mm_adds_pi16(r1_pi16, y1_pi16);
			r1_pi16 = _mm_srai_pi16(r1_pi16, 6);

			// Calculate green
			g1_pi16 = _mm_mullo_pi16(cgv_pi16, v1_pi16);
			g1_pi16 = _mm_srai_pi16(g1_pi16, 2);			// Reduce 8 bits to 6
			g1_pi16 = _mm_subs_pi16(y1_pi16, g1_pi16);
			t1_pi16 = _mm_mullo_pi16(cgu_pi16, u1_pi16);
			t1_pi16 = _mm_srai_pi16(t1_pi16, 2);			// Reduce 8 bits to 6
			g1_pi16 = _mm_subs_pi16(g1_pi16, t1_pi16);
			g1_pi16 = _mm_srai_pi16(g1_pi16, 6);

			// Calculate blue
			b1_pi16 = _mm_mullo_pi16(cbu_pi16, u1_pi16);
			b1_pi16 = _mm_adds_pi16(b1_pi16, y1_pi16);
			b1_pi16 = _mm_srai_pi16(b1_pi16, 6);


			/***** Calculate the second four RGB values *****/

			// Unpack the second four luma values
			y2_pi16 = _mm_unpackhi_pi8(y_pi8, _mm_setzero_si64());

			// Duplicate the second two chroma values
			u2_pi16 = _mm_shuffle_pi16(u2_pi16, _MM_SHUFFLE(3, 3, 2, 2));
			v2_pi16 = _mm_shuffle_pi16(v2_pi16, _MM_SHUFFLE(3, 3, 2, 2));

			y2_pi16 = _mm_subs_pi16(y2_pi16, luma_offset);
			u2_pi16 = _mm_subs_pi16(u2_pi16, chroma_offset);
			v2_pi16 = _mm_subs_pi16(v2_pi16, chroma_offset);

			// This code fixes a case of overflow where very bright pixels
			// with some chroma produced interim values that exceeded 32768
			y2_pi16 = _mm_slli_pi16(y2_pi16, 7);
			y2_pi16 = _mm_mulhi_pi16(y2_pi16, ymult_pi16);
			y2_pi16 = _mm_slli_pi16(y2_pi16, 1);

			// Calculate red
			r2_pi16 = _mm_mullo_pi16(crv_pi16, v2_pi16);
			r2_pi16 = _mm_srai_pi16(r2_pi16, 1);			 // Reduce 7 bits to 6
			r2_pi16 = _mm_adds_pi16(r2_pi16, y2_pi16);
			r2_pi16 = _mm_srai_pi16(r2_pi16, 6);

			// Calculate green
			g2_pi16 = _mm_mullo_pi16(cgv_pi16, v2_pi16);
			g2_pi16 = _mm_srai_pi16(g2_pi16, 2);			// Reduce 8 bits to 6
			g2_pi16 = _mm_subs_pi16(y2_pi16, g2_pi16);
			t2_pi16 = _mm_mullo_pi16(cgu_pi16, u2_pi16);
			t2_pi16 = _mm_srai_pi16(t2_pi16, 2);			// Reduce 8 bits to 6
			g2_pi16 = _mm_subs_pi16(g2_pi16, t2_pi16);
			g2_pi16 = _mm_srai_pi16(g2_pi16, 6);

			// Calculate blue
			b2_pi16 = _mm_mullo_pi16(cbu_pi16, u2_pi16);
			b2_pi16 = _mm_adds_pi16(b2_pi16, y2_pi16);
			b2_pi16 = _mm_srai_pi16(b2_pi16, 6);


			/***** Pack and store the first eight RGB tuples *****/

			// Pack the RGB values into bytes
			r1_pi8 = _mm_packs_pu16(r1_pi16, r2_pi16);
			g1_pi8 = _mm_packs_pu16(g1_pi16, g2_pi16);
			b1_pi8 = _mm_packs_pu16(b1_pi16, b2_pi16);

			//Note: Red and blue are reversed on Intel processors

			// Interleave pairs of blue and green values
			p1_pi8 = _mm_unpacklo_pi8(b1_pi8, g1_pi8);
			p2_pi8 = _mm_unpackhi_pi8(b1_pi8, g1_pi8);

			// Interleave pairs of red and alpha values
			p3_pi8 = _mm_unpacklo_pi8(r1_pi8, alpha_pi8);
			p4_pi8 = _mm_unpackhi_pi8(r1_pi8, alpha_pi8);

			// Interleave blue-green pairs with red-alpha pairs
			rgba1_pi8 = _mm_unpacklo_pi16(p1_pi8, p3_pi8);
			rgba2_pi8 = _mm_unpackhi_pi16(p1_pi8, p3_pi8);

			rgba3_pi8 = _mm_unpacklo_pi16(p2_pi8, p4_pi8);
			rgba4_pi8 = _mm_unpackhi_pi16(p2_pi8, p4_pi8);

			// Store the RGBA tuples
			*(rgba_ptr++) = rgba1_pi8;
			*(rgba_ptr++) = rgba2_pi8;
			*(rgba_ptr++) = rgba3_pi8;
			*(rgba_ptr++) = rgba4_pi8;


			/***** Load the second eight luma values *****/

			// Convert 16-bit signed lowpass pixels into 8-bit unsigned frame pixels
			y1_pi16 = *(y_ptr++);
			y2_pi16 = *(y_ptr++);
			y1_pi16 = _mm_srai_pi16(y1_pi16, descale);
			y2_pi16 = _mm_srai_pi16(y2_pi16, descale);
			y_pi8 = _mm_packs_pu16(y1_pi16, y2_pi16);

#if STRICT_SATURATE
			// Perform strict saturation on the new luma values if required
			if (saturate)
			{
				y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(16));
				y_pi8 = _mm_adds_pu8(y_pi8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
				y_pi8 = _mm_subs_pu8(y_pi8, _mm_set1_pi8(20));
			}
#endif

			/***** Calculate the third four RGB values *****/

			// Unpack the first four luma values
			y1_pi16 = _mm_unpacklo_pi8(y_pi8, _mm_setzero_si64());

			// Unpack the second four chroma values
			u2_pi16 = _mm_unpackhi_pi8(u_pi8, _mm_setzero_si64());
			v2_pi16 = _mm_unpackhi_pi8(v_pi8, _mm_setzero_si64());

			// Duplicate the first two chroma values
			u1_pi16 = _mm_shuffle_pi16(u2_pi16, _MM_SHUFFLE(1, 1, 0, 0));
			v1_pi16 = _mm_shuffle_pi16(v2_pi16, _MM_SHUFFLE(1, 1, 0, 0));

			// Subtract the luma and chroma offsets
			y1_pi16 = _mm_subs_pi16(y1_pi16, luma_offset);
			u1_pi16 = _mm_subs_pi16(u1_pi16, chroma_offset);
			v1_pi16 = _mm_subs_pi16(v1_pi16, chroma_offset);

			// This code fixes a case of overflow where very bright pixels
			// with some chroma produced interim values that exceeded 32768
			y1_pi16 = _mm_slli_pi16(y1_pi16, 7);
			y1_pi16 = _mm_mulhi_pi16(y1_pi16, ymult_pi16);
			y1_pi16 = _mm_slli_pi16(y1_pi16, 1);

			// Calculate red
			r1_pi16 = _mm_mullo_pi16(crv_pi16, v1_pi16);
			r1_pi16 = _mm_srai_pi16(r1_pi16, 1);			 // Reduce 7 bits to 6
			r1_pi16 = _mm_adds_pi16(r1_pi16, y1_pi16);
			r1_pi16 = _mm_srai_pi16(r1_pi16, 6);

			// Calculate green
			g1_pi16 = _mm_mullo_pi16(cgv_pi16, v1_pi16);
			g1_pi16 = _mm_srai_pi16(g1_pi16, 2);			// Reduce 8 bits to 6
			g1_pi16 = _mm_subs_pi16(y1_pi16, g1_pi16);
			t1_pi16 = _mm_mullo_pi16(cgu_pi16, u1_pi16);
			t1_pi16 = _mm_srai_pi16(t1_pi16, 2);			// Reduce 8 bits to 6
			g1_pi16 = _mm_subs_pi16(g1_pi16, t1_pi16);
			g1_pi16 = _mm_srai_pi16(g1_pi16, 6);

			// Calculate blue
			b1_pi16 = _mm_mullo_pi16(cbu_pi16, u1_pi16);
			b1_pi16 = _mm_adds_pi16(b1_pi16, y1_pi16);
			b1_pi16 = _mm_srai_pi16(b1_pi16, 6);


			/***** Calculate the fourth four RGB values *****/

			// Unpack the second four luma values
			y2_pi16 = _mm_unpackhi_pi8(y_pi8, _mm_setzero_si64());

			// Duplicate the second two chroma values
			u2_pi16 = _mm_shuffle_pi16(u2_pi16, _MM_SHUFFLE(3, 3, 2, 2));
			v2_pi16 = _mm_shuffle_pi16(v2_pi16, _MM_SHUFFLE(3, 3, 2, 2));

			y2_pi16 = _mm_subs_pi16(y2_pi16, luma_offset);
			u2_pi16 = _mm_subs_pi16(u2_pi16, chroma_offset);
			v2_pi16 = _mm_subs_pi16(v2_pi16, chroma_offset);

			// This code fixes a case of overflow where very bright pixels
			// with some chroma produced interim values that exceeded 32768
			y2_pi16 = _mm_slli_pi16(y2_pi16, 7);
			y2_pi16 = _mm_mulhi_pi16(y2_pi16, ymult_pi16);
			y2_pi16 = _mm_slli_pi16(y2_pi16, 1);

			// Calculate red
			r2_pi16 = _mm_mullo_pi16(crv_pi16, v2_pi16);
			r2_pi16 = _mm_srai_pi16(r2_pi16, 1);			 // Reduce 7 bits to 6
			r2_pi16 = _mm_adds_pi16(r2_pi16, y2_pi16);
			r2_pi16 = _mm_srai_pi16(r2_pi16, 6);

			// Calculate green
			g2_pi16 = _mm_mullo_pi16(cgv_pi16, v2_pi16);
			g2_pi16 = _mm_srai_pi16(g2_pi16, 2);			// Reduce 8 bits to 6
			g2_pi16 = _mm_subs_pi16(y2_pi16, g2_pi16);
			t2_pi16 = _mm_mullo_pi16(cgu_pi16, u2_pi16);
			t2_pi16 = _mm_srai_pi16(t2_pi16, 2);			// Reduce 8 bits to 6
			g2_pi16 = _mm_subs_pi16(g2_pi16, t2_pi16);
			g2_pi16 = _mm_srai_pi16(g2_pi16, 6);

			// Calculate blue
			b2_pi16 = _mm_mullo_pi16(cbu_pi16, u2_pi16);
			b2_pi16 = _mm_adds_pi16(b2_pi16, y2_pi16);
			b2_pi16 = _mm_srai_pi16(b2_pi16, 6);


			/***** Pack and store the second eight RGB tuples *****/

			// Pack the RGB values into bytes
			r1_pi8 = _mm_packs_pu16(r1_pi16, r2_pi16);
			g1_pi8 = _mm_packs_pu16(g1_pi16, g2_pi16);
			b1_pi8 = _mm_packs_pu16(b1_pi16, b2_pi16);

			//Note: Red and blue are reversed on Intel processors

			// Interleave pairs of blue and green values
			p1_pi8 = _mm_unpacklo_pi8(b1_pi8, g1_pi8);
			p2_pi8 = _mm_unpackhi_pi8(b1_pi8, g1_pi8);

			// Interleave pairs of red and alpha values
			p3_pi8 = _mm_unpacklo_pi8(r1_pi8, alpha_pi8);
			p4_pi8 = _mm_unpackhi_pi8(r1_pi8, alpha_pi8);

			// Interleave blue-green pairs with red-alpha pairs
			rgba1_pi8 = _mm_unpacklo_pi16(p1_pi8, p3_pi8);
			rgba2_pi8 = _mm_unpackhi_pi16(p1_pi8, p3_pi8);

			rgba3_pi8 = _mm_unpacklo_pi16(p2_pi8, p4_pi8);
			rgba4_pi8 = _mm_unpackhi_pi16(p2_pi8, p4_pi8);

			// Store the RGBA tuples
			*(rgba_ptr++) = rgba1_pi8;
			*(rgba_ptr++) = rgba2_pi8;
			*(rgba_ptr++) = rgba3_pi8;
			*(rgba_ptr++) = rgba4_pi8;
		}

		// Clear the MMX registers
		//_mm_empty();

		// Check that the loop ends at the right position
		assert(column == post_column);
#endif

		// Process the rest of the row with 7-bit fixed point arithmetic
		for (; column < width; column += 2)
		{
			uint8_t *rgba_ptr = &rgba_output_ptr[column * 4];
			int y, u, v;
			int r, g, b;

			y = y_input_ptr[column];
			u = u_input_ptr[column/2];
			v = v_input_ptr[column/2];

			// Convert the pixels values to 8 bits
			y >>= descale;
			u >>= descale;
			v >>= descale;

			// Convert the first set of YCbCr values
			if (saturate)
			{
				y = SATURATE_Y(y);
				u = SATURATE_Cr(u);
				v = SATURATE_Cb(v);
			}

			y = y - y_offset;
			u = u - 128;
			v = v - 128;

			y = y * ymult >> 7;

			r = (    y                + r_vmult * v) >> 7;
			g = (2 * y -  g_umult * u - g_vmult * v) >> 8;
			b = (    y            + 2 * b_umult * u) >> 7;

			*(rgba_ptr++) = SATURATE_8U(b);
			*(rgba_ptr++) = SATURATE_8U(g);
			*(rgba_ptr++) = SATURATE_8U(r);
			*(rgba_ptr++) = alpha;

			// Convert the second set of YCbCr values
			y = y_input_ptr[column+1];
			y >>= descale;
			if (saturate) {
				y = SATURATE_Y(y);
			}

			y = y - y_offset;
			y = y * ymult >> 7;

			r = (    y                + r_vmult * v) >> 7;
			g = (2 * y -  g_umult * u - g_vmult * v) >> 8;
			b = (    y            + 2 * b_umult * u) >> 7;

			*(rgba_ptr++) = SATURATE_8U(b);
			*(rgba_ptr++) = SATURATE_8U(g);
			*(rgba_ptr++) = SATURATE_8U(r);
			*(rgba_ptr++) = alpha;
		}
	}
}

#endif



void ConvertYUVStripPlanarToV210(PIXEL *planar_output[], int planar_pitch[], ROI roi,
								 uint8_t *output_buffer, int output_pitch, int frame_width,
								 int format, int colorspace, int precision)
{
	bool inverted = false;
	int output_width = roi.width;

	START(tk_convert);

	// Determine the type of conversion
	switch(format)
	{
	case DECODED_FORMAT_V210:
		ConvertPlanarYUVToV210(planar_output, planar_pitch, roi, output_buffer, output_width, output_pitch,
							   COLOR_FORMAT_V210, colorspace, inverted, precision);
		break;

	case DECODED_FORMAT_YU64:
		ConvertPlanarYUVToYU64(planar_output, planar_pitch, roi, output_buffer, output_width, output_pitch,
							   COLOR_FORMAT_YU64, colorspace, inverted, precision);
		break;


	case DECODED_FORMAT_YR16:

		ConvertPlanarYUVToYR16(planar_output, planar_pitch, roi, output_buffer, output_width, output_pitch,
							   COLOR_FORMAT_YR16, colorspace, inverted, precision);
		break;

	default:
		assert(0);
		break;
	}

	STOP(tk_convert);
}


#if 0	//_PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Convert one row of 16-bit unpacked luma and chroma into 8-bit RGBA32
void ConvertUnpacked16sRowToRGB32(PIXEL **channel_row_ptr, int num_channels,
								  uint8_t *output_row_ptr, int width, int descale,
								  int format, int color_space, int alpha)
{
	// Have not coded SSE2 versions of the color conversion code
	assert(0);
}

#endif

void ConvertUnpacked16sRowToYU64(PIXEL **input, int num_channels, uint8_t *output, int width,
								 const int descale, int precision, int format)
{
	// Note: This routine does not swap the chroma values
	PIXEL16U *y_input_ptr = (PIXEL16U *)input[0];
	PIXEL16U *u_input_ptr = (PIXEL16U *)input[1];
	PIXEL16U *v_input_ptr = (PIXEL16U *)input[2];

	unsigned short *yuv_output_ptr = (unsigned short *)output;

	// Left shift for converting intermediate wavelet coefficients into output pixels
	//const int descale = 4;

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

	if (format == COLOR_FORMAT_V210)
	{
		PIXEL *plane_array[3];
		int plane_pitch[3];
		ROI newroi;
		newroi.width = width;
		newroi.height = 1;

		plane_array[0] = input[0];
		plane_array[1] = input[1];
		plane_array[2] = input[2];

		plane_pitch[0] = 0;
		plane_pitch[1] = 0;
		plane_pitch[2] = 0;

		ConvertYUVStripPlanarToV210(plane_array, plane_pitch, newroi, output,
									width*2, width, format, 0/*colorspace*/, 12);

		return;
	}
	//else YU64

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
		//__m128i yuv_epi8;

		// Load eight u chroma values
		u1_epi16 = _mm_load_si128(u_ptr++);

		// Load eight v chroma values
		v1_epi16 = _mm_load_si128(v_ptr++);

		// Load the first eight luma values
		y1_epi16 = _mm_load_si128(y_ptr++);

		// Load the second eight luma values
		y2_epi16 = _mm_load_si128(y_ptr++);

		// Expand the pixel values to 16 bits
		u1_epi16 = _mm_slli_epi16(u1_epi16, descale);
		v1_epi16 = _mm_slli_epi16(v1_epi16, descale);
		y1_epi16 = _mm_slli_epi16(y1_epi16, descale);
		y2_epi16 = _mm_slli_epi16(y2_epi16, descale);

		// Interleave the first four chroma values
		uv_epi16 = _mm_unpacklo_epi16(u1_epi16, v1_epi16);

		// Interleave the first four luma values with the chroma pairs
		yuv1_epi16 = _mm_unpacklo_epi16(y1_epi16, uv_epi16);

		// Store the first group of 16-bit luma and chroma
		_mm_store_si128(yuv_ptr++, yuv1_epi16);

		// Interleave the second four luma values with the chroma pairs
		yuv2_epi16 = _mm_unpackhi_epi16(y1_epi16, uv_epi16);

		// Store the second group of 16-bit luma and chroma
		_mm_store_si128(yuv_ptr++, yuv2_epi16);

		// Pack the first eight luma and chroma pairs
		//yuv_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

		// Store the first eight luma and chroma pairs
		//_mm_store_si128(yuv_ptr++, yuv_epi8);

		// Interleave the second four chroma values
		uv_epi16 = _mm_unpackhi_epi16(u1_epi16, v1_epi16);

		// Interleave the third four luma values with the chroma pairs
		yuv1_epi16 = _mm_unpacklo_epi16(y2_epi16, uv_epi16);

		// Store the third group of 16-bit luma and chroma
		_mm_store_si128(yuv_ptr++, yuv1_epi16);

		// Interleave the fourth four luma values with the chroma pairs
		yuv2_epi16 = _mm_unpackhi_epi16(y2_epi16, uv_epi16);

		// Store the fourth group of 16-bit luma and chroma
		_mm_store_si128(yuv_ptr++, yuv2_epi16);

		// Pack the second eight luma and chroma pairs
		//yuv_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

		// Store the second eight luma and chroma pairs
		//_mm_store_si128(yuv_ptr++, yuv_epi8);
	}

	// Should have terminated the loop at the post processing column
	assert(column == post_column);

	// Set the output pointer to the rest of the row of packed YUV
	yuv_output_ptr = (unsigned short *)yuv_ptr;

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
		y1 <<= descale;
		y2 <<= descale;
		u  <<= descale;
		v  <<= descale;

		*(yuv_output_ptr++) = y1;
		*(yuv_output_ptr++) = u;
		*(yuv_output_ptr++) = y2;
		*(yuv_output_ptr++) = v;
	}
}

void ConvertUnpacked16sRowToB64A(PIXEL **input_plane, int num_channels,
								 uint8_t *output, int width,
								 const int descale, int precision)
{
	PIXEL *r_input_ptr = input_plane[1];
	PIXEL *g_input_ptr = input_plane[0];
	PIXEL *b_input_ptr = input_plane[2];
	PIXEL *a_input_ptr = input_plane[3];

	PIXEL16U *argb_output_ptr = (PIXEL16U *)output;

	const unsigned short alpha = USHRT_MAX;

	// Left shift for converting intermediate wavelet coefficients into output pixels
	const int shift = 16 - precision - descale;
	//const int shift = 2;

	const int rgb_max = USHRT_MAX;

	// Clamp the RGB values to 14 bits using saturated addition to 15 bits
	const int clamp = 0x7FFF - 0x3FFF;

	int column;

	

	//alpha_Companded likely doesn't set as the is not does to go through Convert4444LinesToOutput

#if (1 && XMMOPT)

	// Process eight ARGB tuples per loop iteration
	const int column_step = 8;

	// Compute the column where end of row processing must begin
	int post_column = width - (width % column_step);

	// Initialize the input pointers into each channel
	__m128i *r_ptr = (__m128i *)r_input_ptr;
	__m128i *g_ptr = (__m128i *)g_input_ptr;
	__m128i *b_ptr = (__m128i *)b_input_ptr;
	__m128i *a_ptr = (__m128i *)a_input_ptr;

	// Initialize the output pointer for the packed YUV results
	__m128i *argb_ptr = (__m128i *)argb_output_ptr;

	// Use a default value for the alpha channel
	__m128i a_epi16 = _mm_set1_epi16(alpha);

	__m128i clamp_epi16 = _mm_set1_epi16(clamp);
	__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x0fff);

#endif

	// The amount of left shift should be zero
	//assert(shift == 0);

	// Start processing at the left column
	column = 0;

#if (1 && XMMOPT)

	for (; column < post_column; column += column_step)
	{
		__m128i r_epi16;
		__m128i g_epi16;
		__m128i b_epi16;

		__m128i ar_epi16;
		__m128i gb_epi16;
		__m128i argb_epi16;

		// Load eight values from each channel
		r_epi16 = _mm_load_si128(r_ptr++);
		g_epi16 = _mm_load_si128(g_ptr++);
		b_epi16 = _mm_load_si128(b_ptr++);

		// Clamp the values to 14 bits
		r_epi16 = _mm_adds_epi16(r_epi16, clamp_epi16);
		g_epi16 = _mm_adds_epi16(g_epi16, clamp_epi16);
		b_epi16 = _mm_adds_epi16(b_epi16, clamp_epi16);

		r_epi16 = _mm_subs_epu16(r_epi16, clamp_epi16);
		g_epi16 = _mm_subs_epu16(g_epi16, clamp_epi16);
		b_epi16 = _mm_subs_epu16(b_epi16, clamp_epi16);

		// Expand the pixel values to 16 bits
		r_epi16 = _mm_slli_epi16(r_epi16, shift);
		g_epi16 = _mm_slli_epi16(g_epi16, shift);
		b_epi16 = _mm_slli_epi16(b_epi16, shift);

		if(num_channels == 4)
		{
			a_epi16 = _mm_load_si128(a_ptr++);
			a_epi16 = _mm_adds_epi16(a_epi16, clamp_epi16);
			a_epi16 = _mm_subs_epu16(a_epi16, clamp_epi16);
			a_epi16 = _mm_slli_epi16(a_epi16, shift);
			
			//12-bit SSE calibrated code
			a_epi16 = _mm_srli_epi16(a_epi16, 4); //12-bit

			a_epi16 = _mm_subs_epu16(a_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
			a_epi16 = _mm_slli_epi16(a_epi16, 3);  //15-bit
			a_epi16 = _mm_mulhi_epi16(a_epi16, _mm_set1_epi16(alphacompandGain));//12-bit
			a_epi16 = _mm_adds_epi16(a_epi16, limiterRGB); //12-bit limit
			a_epi16 = _mm_subs_epu16(a_epi16, limiterRGB);

			a_epi16 = _mm_slli_epi16(a_epi16, 4); //16-bit

		}

		// Interleave the first four alpha and red values
		ar_epi16 = _mm_unpacklo_epi16(a_epi16, r_epi16);

		// Interleave the first four green and blue values
		gb_epi16 = _mm_unpacklo_epi16(g_epi16, b_epi16);

		// Interleave and store the first pair of ARGB tuples
		argb_epi16 = _mm_unpacklo_epi32(ar_epi16, gb_epi16);
		_mm_store_si128(argb_ptr++, argb_epi16);

		// Interleave and store the second pair of ARGB tuples
		argb_epi16 = _mm_unpackhi_epi32(ar_epi16, gb_epi16);
		_mm_store_si128(argb_ptr++, argb_epi16);

		// Interleave the second four alpha and red values
		ar_epi16 = _mm_unpackhi_epi16(a_epi16, r_epi16);

		// Interleave the second four green and blue values
		gb_epi16 = _mm_unpackhi_epi16(g_epi16, b_epi16);

		// Interleave and store the third pair of ARGB tuples
		argb_epi16 = _mm_unpacklo_epi32(ar_epi16, gb_epi16);
		_mm_store_si128(argb_ptr++, argb_epi16);

		// Interleave and store the fourth pair of ARGB tuples
		argb_epi16 = _mm_unpackhi_epi32(ar_epi16, gb_epi16);
		_mm_store_si128(argb_ptr++, argb_epi16);
	}

	// Should have terminated the loop at the post processing column
	assert(column == post_column);

	// Set the output pointer to the rest of the row of packed YUV
	argb_output_ptr = (unsigned short *)argb_ptr;

#endif

	// Handle end of row processing for the remaining columns
	for (; column < width; column++)
	{
		int r = r_input_ptr[column];
		int g = g_input_ptr[column];
		int b = b_input_ptr[column];
		int a = a_input_ptr[column];

		// Descale the values
		r <<= shift;
		g <<= shift;
		b <<= shift;

		if (r < 0) r = 0; else if (r > rgb_max) r = rgb_max;
		if (g < 0) g = 0; else if (g > rgb_max) g = rgb_max;
		if (b < 0) b = 0; else if (b > rgb_max) b = rgb_max;

		if(num_channels == 4)
		{
			a <<= shift;
			
			a >>= 4; //12-bit
			a -= alphacompandDCoffset;
			a <<= 3; //15-bit
			a *= alphacompandGain;
			a >>= 16; //12-bit
			a <<= 4; // 16-bit;
			if (a < 0) a = 0; else if (a > rgb_max) a = rgb_max;
		}
		else
		{
			a = alpha;
		}


		*(argb_output_ptr++) = a;
		*(argb_output_ptr++) = r;
		*(argb_output_ptr++) = g;
		*(argb_output_ptr++) = b;
	}
}


// Convert the input frame from YUV422 to RGB
void ConvertUnpackedYUV16sRowToRGB48(PIXEL **input_plane, int num_channels,
								 uint8_t *output, int width,
								 const int descale, int precision, int format, int colorspace)
{
	void *plane[3];
	int channel;

	int y_offset = 16; // not VIDEO_RGB & not YUV709
	int ymult = 128*149;	//7bit 1.164
	int r_vmult = 204;		//7bit 1.596
	int g_vmult = 208;		//8bit 0.813
	int g_umult = 100;		//8bit 0.391
	int b_umult = 129;		//6bit 2.018
	int saturate = 1;
	//int mmx_y_offset = (y_offset<<7);
	int upconvert422to444 = 0;
	//colorspace |= COLOR_SPACE_422_TO_444; //DAN20090601

	if(colorspace & COLOR_SPACE_422_TO_444)
	{
		upconvert422to444 = 1;
	}

	switch(colorspace & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:
		y_offset = 16;		// not VIDEO_RGB & not YUV709
		ymult = 128*149;	//7bit 1.164
		r_vmult = 204;		//7bit 1.596
		g_vmult = 208;		//8bit 0.813
		g_umult = 100;		//8bit 0.391
		b_umult = 129;		//6bit 2.018
		saturate = 1;
		break;

	default: assert(0);
	case COLOR_SPACE_CG_709:
		y_offset = 16;
		ymult = 128*149;	//7bit 1.164
		r_vmult = 230;		//7bit 1.793
		g_vmult = 137;		//8bit 0.534
		g_umult = 55;		//8bit 0.213
		b_umult = 135;		//6bit 2.115
		saturate = 1;
		break;

	case COLOR_SPACE_VS_601:
		y_offset = 0;
		ymult = 128*128;	//7bit 1.0
		r_vmult = 175;		//7bit 1.371
		g_vmult = 179;		//8bit 0.698
		g_umult = 86;		//8bit 0.336
		b_umult = 111;		//6bit 1.732
		saturate = 0;
		break;

	case COLOR_SPACE_VS_709:
		y_offset = 0;
		ymult = 128*128;	//7bit 1.0
		r_vmult = 197;		//7bit 1.540
		g_vmult = 118;		//8bit 0.459
		g_umult = 47;		//8bit 0.183
		b_umult = 116;		//6bit 1.816
		saturate = 0;
		break;
	}


	// Convert from pixel to byte data
	for (channel = 0; channel < 3; channel++)
	{
		plane[channel] = input_plane[channel];
	}

	{
		PIXEL16U *Y_row, *U_row, *V_row;
		PIXEL16U *RGBA_row;
		int column;
		//int column_step = 2;

		Y_row = plane[0];
		U_row = plane[1];
		V_row = plane[2];

		RGBA_row = (PIXEL16U *)output;

		//TODO SSE2

		{
			unsigned short *RGB_ptr = &RGBA_row[0];
			// Process the rest of the row with 7-bit fixed point arithmetic
			for(column=0; column<width; column+=2)
			{
				int R, G, B;
				int Y, U, V;

				// Convert the first set of YCbCr values
				if(saturate)
				{
					Y = SATURATE_Y(Y_row[column]    << 8);
					V = SATURATE_Cr(U_row[column/2] << 8);
					U = SATURATE_Cb(V_row[column/2] << 8);
				}
				else
				{
					Y = Y_row[column]   << 8;
					V = U_row[column/2] << 8;
					U = V_row[column/2] << 8;
				}

				Y = Y - (y_offset<<8);
				U = U - 32768;
				V = V - 32768;

				Y = Y * ymult >> 7;

				R = (Y     + r_vmult * V) >> 7;
				G = (Y*2  -  g_umult * U - g_vmult * V) >> 8;
				B = (Y + 2 * b_umult * U) >> 7;


				R = SATURATE_16U(R);
				G = SATURATE_16U(G);
				B = SATURATE_16U(B);

				switch(format)
				{
				case COLOR_FORMAT_B64A: //b64a
					*RGB_ptr++ = 0xffff;
					*RGB_ptr++ = R;
					*RGB_ptr++ = G;
					*RGB_ptr++ = B;
					break;
				case COLOR_FORMAT_R210: //r210 byteswap(R10G10B10A2)
					R >>= 6; // 10-bit
					G >>= 6; // 10-bit
					B >>= 6; // 10-bit
					{
						int rgb;
						unsigned int *RGB = (unsigned int *)RGB_ptr;

						R <<= 20;
						G <<= 10;
						//B <<= 0;
						rgb = R;
						rgb |= G;
						rgb |= B;

						*RGB = SwapInt32(rgb);
						RGB_ptr+=2;
					}
					break;
				case COLOR_FORMAT_DPX0: //dpx0 byteswap(R10G10B10A2)
					R >>= 6; // 10-bit
					G >>= 6; // 10-bit
					B >>= 6; // 10-bit
					{
						int rgb;
						unsigned int *RGB = (unsigned int *)RGB_ptr;

						R <<= 22;
						G <<= 12;
						B <<= 2;
						rgb = R;
						rgb |= G;
						rgb |= B;

						*RGB = SwapInt32(rgb);
						RGB_ptr+=2;
					}
					break;
				case COLOR_FORMAT_RG30://rg30 A2B10G10R10
				case COLOR_FORMAT_AB10:
					R >>= 6; // 10-bit
					G >>= 6; // 10-bit
					B >>= 6; // 10-bit
					{
						int rgb;
						unsigned int *RGB = (unsigned int *)RGB_ptr;

						B <<= 20;
						G <<= 10;
						rgb = R;
						rgb |= G;
						rgb |= B;

						*RGB = rgb;
						RGB_ptr+=2;
					}
					break;
				case COLOR_FORMAT_AR10: //rg30 A2R10G10B10
					R >>= 6; // 10-bit
					G >>= 6; // 10-bit
					B >>= 6; // 10-bit
					{
						int rgb;
						unsigned int *RGB = (unsigned int *)RGB_ptr;

						R <<= 20;
						G <<= 10;
						rgb = R;
						rgb |= G;
						rgb |= B;

						*RGB = rgb;
						RGB_ptr+=2;
					}
					break;
				case COLOR_FORMAT_RG64: //RGBA64
					*RGB_ptr++ = R;
					*RGB_ptr++ = G;
					*RGB_ptr++ = B;
					*RGB_ptr++ = 0xffff;
					break;
				}

				// Convert the second set of YCbCr values
				if(saturate)
					Y = SATURATE_Y(Y_row[column+1] << 8);
				else
					Y = (Y_row[column+1] << 8);


				Y = Y - (y_offset<<8);
				Y = Y * ymult >> 7;

				R = (Y           + r_vmult * V) >> 7;
				G = (Y*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y + 2 * b_umult * U) >> 7;


				R = SATURATE_16U(R);
				G = SATURATE_16U(G);
				B = SATURATE_16U(B);

				switch(format)
				{
				case COLOR_FORMAT_B64A: //b64a
					*RGB_ptr++ = 0xffff;
					*RGB_ptr++ = R;
					*RGB_ptr++ = G;
					*RGB_ptr++ = B;
					break;
				case COLOR_FORMAT_R210: //r210 byteswap(R10G10B10A2)
					R >>= 6; // 10-bit
					G >>= 6; // 10-bit
					B >>= 6; // 10-bit
					{
						int rgb;
						unsigned int *RGB = (unsigned int *)RGB_ptr;

						R <<= 20;
						G <<= 10;
						//B <<= 0;
						rgb = R;
						rgb |= G;
						rgb |= B;

						*RGB = SwapInt32(rgb);
						RGB_ptr+=2;
					}
					break;
				case COLOR_FORMAT_DPX0: //DPX0 byteswap(R10G10B10A2)
					R >>= 6; // 10-bit
					G >>= 6; // 10-bit
					B >>= 6; // 10-bit
					{
						int rgb;
						unsigned int *RGB = (unsigned int *)RGB_ptr;

						R <<= 22;
						G <<= 12;
						B <<= 2;
						rgb = R;
						rgb |= G;
						rgb |= B;

						*RGB = SwapInt32(rgb);
						RGB_ptr+=2;
					}
					break;
				case COLOR_FORMAT_RG30://rg30 A2B10G10R10
				case COLOR_FORMAT_AB10:
					R >>= 6; // 10-bit
					G >>= 6; // 10-bit
					B >>= 6; // 10-bit
					{
						int rgb;
						unsigned int *RGB = (unsigned int *)RGB_ptr;

						B <<= 20;
						G <<= 10;
						rgb = R;
						rgb |= G;
						rgb |= B;

						*RGB = rgb;
						RGB_ptr+=2;
					}
					break;
				case COLOR_FORMAT_AR10: //rg30 A2R10G10B10
					R >>= 6; // 10-bit
					G >>= 6; // 10-bit
					B >>= 6; // 10-bit
					{
						int rgb;
						unsigned int *RGB = (unsigned int *)RGB_ptr;

						R <<= 20;
						G <<= 10;
						rgb = R;
						rgb |= G;
						rgb |= B;

						*RGB = rgb;
						RGB_ptr+=2;
					}
					break;
				case COLOR_FORMAT_RG64: //RGBA64
					*RGB_ptr++ = R;
					*RGB_ptr++ = G;
					*RGB_ptr++ = B;
					*RGB_ptr++ = 0xffff;
					break;
				}
			}
		}
	}
}


void ConvertUnpacked16sRowToRGB30(PIXEL **input_plane, int num_channels,
								 uint8_t *output, int width,
								 const int descale, int precision, int format, int colorspace)
{
	PIXEL *r_input_ptr = input_plane[1];
	PIXEL *g_input_ptr = input_plane[0];
	PIXEL *b_input_ptr = input_plane[2];

	unsigned int *argb_output_ptr = (unsigned int *)output;

	//const unsigned short alpha = USHRT_MAX;

	// Left shift for converting intermediate wavelet coefficients into output pixels
	const int shift = 16 - precision - descale;
	//const int shift = 2;

	const int rgb_max = USHRT_MAX;

	// Clamp the RGB values to 14 bits using saturated addition to 15 bits
	const int clamp = 0x7FFF - 0x3FFF;

	int column;

#if (1 && XMMOPT)

	// Process eight ARGB tuples per loop iteration
	const int column_step = 8;

	// Compute the column where end of row processing must begin
	int post_column = width - (width % column_step);

	// Initialize the input pointers into each channel
	__m128i *r_ptr = (__m128i *)r_input_ptr;
	__m128i *g_ptr = (__m128i *)g_input_ptr;
	__m128i *b_ptr = (__m128i *)b_input_ptr;

	// Initialize the output pointer for the packed YUV results
	__m128i *argb_ptr = (__m128i *)argb_output_ptr;
	
	__m128i clamp_epi16 = _mm_set1_epi16(clamp);
	__m128i zero128 = _mm_setzero_si128();

#endif

	// The amount of left shift should be zero
	//assert(shift == 0);

	// Start processing at the left column
	column = 0;

#if (1 && XMMOPT)

	for (; column < post_column; column += column_step)
	{
		__m128i r_epi16;
		__m128i g_epi16;
		__m128i b_epi16;
		
		__m128i rr_epi32;
		__m128i gg_epi32;
		__m128i bb_epi32;

		// Load eight values from each channel
		r_epi16 = _mm_load_si128(r_ptr++);
		g_epi16 = _mm_load_si128(g_ptr++);
		b_epi16 = _mm_load_si128(b_ptr++);

		// Clamp the values to 14 bits
		r_epi16 = _mm_adds_epi16(r_epi16, clamp_epi16);
		g_epi16 = _mm_adds_epi16(g_epi16, clamp_epi16);
		b_epi16 = _mm_adds_epi16(b_epi16, clamp_epi16);

		r_epi16 = _mm_subs_epu16(r_epi16, clamp_epi16);
		g_epi16 = _mm_subs_epu16(g_epi16, clamp_epi16);
		b_epi16 = _mm_subs_epu16(b_epi16, clamp_epi16);

		// Expand the pixel values to 16 bits
		r_epi16 = _mm_slli_epi16(r_epi16, shift);
		g_epi16 = _mm_slli_epi16(g_epi16, shift);
		b_epi16 = _mm_slli_epi16(b_epi16, shift);

		// Expand the pixel values to 10 bits
		r_epi16 = _mm_srli_epi16(r_epi16, 6);
		g_epi16 = _mm_srli_epi16(g_epi16, 6);
		b_epi16 = _mm_srli_epi16(b_epi16, 6);

		// Interleave the first four blue and green values
		rr_epi32 = _mm_unpacklo_epi16(r_epi16, zero128);
		gg_epi32 = _mm_unpacklo_epi16(g_epi16, zero128);
		bb_epi32 = _mm_unpacklo_epi16(b_epi16, zero128);

		//r210 bswap(R10G10B10A2)
		//RG30 A2B10G10R10
		switch(format)
		{
		case DECODED_FORMAT_RG30:
		case DECODED_FORMAT_AB10:
			gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
			bb_epi32 = _mm_slli_epi32(bb_epi32, 20);

			rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
			rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

			_mm_store_si128(argb_ptr++, rr_epi32);

			rr_epi32 = _mm_unpackhi_epi16(r_epi16, zero128);
			gg_epi32 = _mm_unpackhi_epi16(g_epi16, zero128);
			bb_epi32 = _mm_unpackhi_epi16(b_epi16, zero128);

			gg_epi32 = _mm_slli_epi32(gg_epi32, 10);
			bb_epi32 = _mm_slli_epi32(bb_epi32, 20);

			rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
			rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

			_mm_store_si128(argb_ptr++, rr_epi32);
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

			_mm_store_si128(argb_ptr++, rr_epi32);


			rr_epi32 = _mm_unpackhi_epi16(r_epi16, zero128);
			gg_epi32 = _mm_unpackhi_epi16(g_epi16, zero128);
			bb_epi32 = _mm_unpackhi_epi16(b_epi16, zero128);


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

			_mm_store_si128(argb_ptr++, rr_epi32);
			break;

		case DECODED_FORMAT_DPX0:
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

			_mm_store_si128(argb_ptr++, rr_epi32);


			rr_epi32 = _mm_unpackhi_epi16(r_epi16, zero128);
			gg_epi32 = _mm_unpackhi_epi16(g_epi16, zero128);
			bb_epi32 = _mm_unpackhi_epi16(b_epi16, zero128);


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

			_mm_store_si128(argb_ptr++, rr_epi32);
			break;

		case DECODED_FORMAT_AR10:
			rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
			gg_epi32 = _mm_slli_epi32(gg_epi32, 10);

			rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
			rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

			_mm_store_si128(argb_ptr++, rr_epi32);

			rr_epi32 = _mm_unpackhi_epi16(r_epi16, zero128);
			gg_epi32 = _mm_unpackhi_epi16(g_epi16, zero128);
			bb_epi32 = _mm_unpackhi_epi16(b_epi16, zero128);

			rr_epi32 = _mm_slli_epi32(rr_epi32, 20);
			gg_epi32 = _mm_slli_epi32(gg_epi32, 10);

			rr_epi32 = _mm_add_epi32(rr_epi32, gg_epi32);
			rr_epi32 = _mm_add_epi32(rr_epi32, bb_epi32);

			_mm_store_si128(argb_ptr++, rr_epi32);
			break;
		default:
			assert(0); //unknown format
			break;
		}
	}

	// Should have terminated the loop at the post processing column
	assert(column == post_column);

	// Set the output pointer to the rest of the row of packed YUV
	argb_output_ptr = (unsigned int *)argb_ptr;

#endif

	// Handle end of row processing for the remaining columns
	for (; column < width; column++)
	{
		int rgb = 0;
		int r = r_input_ptr[column];
		int g = g_input_ptr[column];
		int b = b_input_ptr[column];

		// Descale the values
		r <<= shift;
		g <<= shift;
		b <<= shift;

		if (r < 0) r = 0; else if (r > rgb_max) r = rgb_max;
		if (g < 0) g = 0; else if (g > rgb_max) g = rgb_max;
		if (b < 0) b = 0; else if (b > rgb_max) b = rgb_max;

		r >>= 6; // 10-bit
		g >>= 6; // 10-bit
		b >>= 6; // 10-bit



		switch(format)
		{
		case DECODED_FORMAT_RG30:
		case DECODED_FORMAT_AB10:
			b <<= 20;
			g <<= 10;
			rgb |= r;
			rgb |= g;
			rgb |= b;

			*(argb_output_ptr++) = rgb;
			break;
		case DECODED_FORMAT_AR10:
			r <<= 20;
			g <<= 10;
			b <<= 0;
			rgb |= r;
			rgb |= g;
			rgb |= b;

			*(argb_output_ptr++) = (rgb);
			break;
		case DECODED_FORMAT_R210:
			r <<= 20;
			g <<= 10;
			//b <<= 0;
			rgb |= r;
			rgb |= g;
			rgb |= b;

			*(argb_output_ptr++) = SwapInt32(rgb);
			break;
		case DECODED_FORMAT_DPX0:
			r <<= 22;
			g <<= 12;
			b <<= 2;
			rgb |= r;
			rgb |= g;
			rgb |= b;

			*(argb_output_ptr++) = SwapInt32(rgb);
			break;
		}
	}
}


void ConvertUnpacked16sRowToRGBA64(PIXEL **input_plane, int num_channels,
								 uint8_t *output, int width,
								 const int descale, int precision)
{
	PIXEL *r_input_ptr = input_plane[1];
	PIXEL *g_input_ptr = input_plane[0];
	PIXEL *b_input_ptr = input_plane[2];
	PIXEL *a_input_ptr = input_plane[3];

	PIXEL16U *rgba_output_ptr = (PIXEL16U *)output;

	const unsigned short alpha = USHRT_MAX;

	// Left shift for converting intermediate wavelet coefficients into output pixels
	const int shift = 16 - precision - descale;
	//const int shiftTo12 = 12 - precision - descale;
	//const int shift = 2;

	const int rgb_max = USHRT_MAX;

	// Clamp the RGB values to 14 bits using saturated addition to 15 bits
	const int clamp = 0x7FFF - 0x3FFF;

	int column;

#if (1 && XMMOPT)

	// Process eight ARGB tuples per loop iteration
	const int column_step = 8;

	// Compute the column where end of row processing must begin
	int post_column = width - (width % column_step);

	// Initialize the input pointers into each channel
	__m128i *r_ptr = (__m128i *)r_input_ptr;
	__m128i *g_ptr = (__m128i *)g_input_ptr;
	__m128i *b_ptr = (__m128i *)b_input_ptr;
	__m128i *a_ptr = (__m128i *)a_input_ptr;

	// Initialize the output pointer for the packed YUV results
	__m128i *rgba_ptr = (__m128i *)rgba_output_ptr;

	// Use a default value for the alpha channel
	__m128i a_epi16 = _mm_set1_epi16(alpha);

	__m128i clamp_epi16 = _mm_set1_epi16(clamp);

#endif

	// The amount of left shift should be zero
	//assert(shift == 0);

	//alpha_Companded likely doesn't set as the is not does to go through Convert4444LinesToOutput

	// Start processing at the left column
	column = 0;

#if (1 && XMMOPT)

	for (; column < post_column; column += column_step)
	{
		__m128i r_epi16;
		__m128i g_epi16;
		__m128i b_epi16;

		__m128i rg_epi16;
		__m128i ba_epi16;
		__m128i rgba_epi16;

		// Load eight values from each channel
		r_epi16 = _mm_load_si128(r_ptr++);
		g_epi16 = _mm_load_si128(g_ptr++);
		b_epi16 = _mm_load_si128(b_ptr++);
		if(num_channels == 4)
		{
			a_epi16 = _mm_load_si128(a_ptr++);


		//	if(shift == 8)
			{
				// Remove the alpha encoding curve.

				if(shift < 8)
					a_epi16 = _mm_srai_epi16(a_epi16, 8-shift);

				/*
				//a -= 16;
				a_epi16 = _mm_subs_epu16(a_epi16, _mm_set1_epi16(16));
				//a <<= 8;
				a_epi16 = _mm_slli_epi16(a_epi16, 8);
				//a += 111;
				a_epi16 = _mm_adds_epu16(a_epi16, _mm_set1_epi16(111));
				a_epi16 = _mm_srli_epi16(a_epi16, 1);
				//a /= 223; // * high 294
				a_epi16 = _mm_mulhi_epi16(a_epi16, _mm_set1_epi16(588));
				*/

				//12-bit SSE calibrated code
				a_epi16 = _mm_slli_epi16(a_epi16, 4); //12-bit
				a_epi16 = _mm_subs_epu16(a_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				a_epi16 = _mm_slli_epi16(a_epi16, 3);  //15-bit
				a_epi16 = _mm_mulhi_epi16(a_epi16, _mm_set1_epi16(alphacompandGain));//12-bit
				a_epi16 = _mm_srai_epi16(a_epi16, 4); //8-bit

				if(shift < 8)
					a_epi16 = _mm_slli_epi16(a_epi16, 8-shift);
			}
		//	else
		//	{
		//		assert(0);
		//	}
		}

		// Clamp the values to 14 bits
		r_epi16 = _mm_adds_epi16(r_epi16, clamp_epi16);
		g_epi16 = _mm_adds_epi16(g_epi16, clamp_epi16);
		b_epi16 = _mm_adds_epi16(b_epi16, clamp_epi16);
		a_epi16 = _mm_adds_epi16(a_epi16, clamp_epi16);

		r_epi16 = _mm_subs_epu16(r_epi16, clamp_epi16);
		g_epi16 = _mm_subs_epu16(g_epi16, clamp_epi16);
		b_epi16 = _mm_subs_epu16(b_epi16, clamp_epi16);
		a_epi16 = _mm_subs_epu16(a_epi16, clamp_epi16);

		// Expand the pixel values to 16 bits
		r_epi16 = _mm_slli_epi16(r_epi16, shift);
		g_epi16 = _mm_slli_epi16(g_epi16, shift);
		b_epi16 = _mm_slli_epi16(b_epi16, shift);
		a_epi16 = _mm_slli_epi16(a_epi16, shift);

		// Interleave the first four red and green values
		rg_epi16 = _mm_unpacklo_epi16(r_epi16, g_epi16);

		// Interleave the first four blue and alpha values
		ba_epi16 = _mm_unpacklo_epi16(b_epi16, a_epi16);

		// Interleave and store the first pair of RGBA tuples
		rgba_epi16 = _mm_unpacklo_epi32(rg_epi16, ba_epi16);
		_mm_store_si128(rgba_ptr++, rgba_epi16);

		// Interleave and store the second pair of RGBA tuples
		rgba_epi16 = _mm_unpackhi_epi32(rg_epi16, ba_epi16);
		_mm_store_si128(rgba_ptr++, rgba_epi16);

		// Interleave the second four red and green values
		rg_epi16 = _mm_unpackhi_epi16(r_epi16, g_epi16);

		// Interleave the second four blue and alpha values
		ba_epi16 = _mm_unpackhi_epi16(b_epi16, a_epi16);

		// Interleave and store the third pair of RGBA tuples
		rgba_epi16 = _mm_unpacklo_epi32(rg_epi16, ba_epi16);
		_mm_store_si128(rgba_ptr++, rgba_epi16);

		// Interleave and store the fourth pair of RGBA tuples
		rgba_epi16 = _mm_unpackhi_epi32(rg_epi16, ba_epi16);
		_mm_store_si128(rgba_ptr++, rgba_epi16);
	}

	// Should have terminated the loop at the post processing column
	assert(column == post_column);

	// Set the output pointer to the rest of the row of packed YUV
	rgba_output_ptr = (unsigned short *)rgba_ptr;

#endif

	// Handle end of row processing for the remaining columns
	for (; column < width; column++)
	{
		int r = r_input_ptr[column];
		int g = g_input_ptr[column];
		int b = b_input_ptr[column];
		int a = alpha;

		// Descale the values
		r <<= shift;
		g <<= shift;
		b <<= shift;

		if (r < 0) r = 0; else if (r > rgb_max) r = rgb_max;
		if (g < 0) g = 0; else if (g > rgb_max) g = rgb_max;
		if (b < 0) b = 0; else if (b > rgb_max) b = rgb_max;

		if(num_channels == 4)
		{
			a = a_input_ptr[column];

			if(shift == 8)
			{
				// Remove the alpha encoding curve.
				//a -= 16;
				//a <<= 8;
				//a += 111;
				//a /= 223;
				
				//12-bit SSE calibrated code
				//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 4); // 12-bit
				//a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
				//a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));
				//a2_output_epi16 = _mm_srai_epi16(a2_output_epi16, 4); // 8-bit

				a <<= 4; //12-bit
				a -= alphacompandDCoffset;
				a <<= 3; //15-bit
				a *= alphacompandGain;
				a >>= 16; //12-bit
				a >>= 4; // 8-bit;
			}
			else
			{
				assert(0);
			}

			a <<= shift;

			if (a < 0) a = 0; else if (a > rgb_max) a = rgb_max;
		}

		*(rgba_output_ptr++) = r;
		*(rgba_output_ptr++) = g;
		*(rgba_output_ptr++) = b;
		*(rgba_output_ptr++) = a;
	}
}

void ConvertUnpacked16sRowToRGB48(PIXEL **input_plane, int num_channels,
								 uint8_t *output, int width,
								 const int descale, int precision)
{
	PIXEL *r_input_ptr = input_plane[1];
	PIXEL *g_input_ptr = input_plane[0];
	PIXEL *b_input_ptr = input_plane[2];

	PIXEL16U *rgb_output_ptr = (PIXEL16U *)output;

	//const unsigned short alpha = USHRT_MAX;

	// Left shift for converting intermediate wavelet coefficients into output pixels
	const int shift = 16 - precision - descale;

	const int rgb_max = USHRT_MAX;

	// Clamp the RGB values to 14 bits using saturated addition to 15 bits
	//const int clamp = 0x7FFF - 0x3FFF;

	int column;

#if (0 && XMMOPT)

	// Process eight ARGB tuples per loop iteration
	const int column_step = 8;

	// Compute the column where end of row processing must begin
	int post_column = width - (width % column_step);

	// Initialize the input pointers into each channel
	__m128i *r_ptr = (__m128i *)r_input_ptr;
	__m128i *g_ptr = (__m128i *)g_input_ptr;
	__m128i *b_ptr = (__m128i *)b_input_ptr;

	// Initialize the output pointer for the packed YUV results
	__m128i *rgb_ptr = (__m128i *)rgb_output_ptr;

	// Use a default value for the alpha channel
	__m128i a_epi16 = _mm_set1_epi16(alpha);

	__m128i clamp_epi16 = _mm_set1_epi16(clamp);

#endif

	// The amount of left shift should be zero
	//assert(shift == 0);

	// Start processing at the left column
	column = 0;

#if (0 && XMMOPT)

	for (; column < post_column; column += column_step)
	{
		__m128i r_epi16;
		__m128i g_epi16;
		__m128i b_epi16;

		__m128i ar_epi16;
		__m128i gb_epi16;
		__m128i rgb_epi16;

		// Load eight values from each channel
		r_epi16 = _mm_load_si128(r_ptr++);
		g_epi16 = _mm_load_si128(g_ptr++);
		b_epi16 = _mm_load_si128(b_ptr++);

		// Clamp the values to 14 bits
		r_epi16 = _mm_adds_epi16(r_epi16, clamp_epi16);
		g_epi16 = _mm_adds_epi16(g_epi16, clamp_epi16);
		b_epi16 = _mm_adds_epi16(b_epi16, clamp_epi16);

		r_epi16 = _mm_subs_epu16(r_epi16, clamp_epi16);
		g_epi16 = _mm_subs_epu16(g_epi16, clamp_epi16);
		b_epi16 = _mm_subs_epu16(b_epi16, clamp_epi16);

		// Expand the pixel values to 16 bits
		r_epi16 = _mm_slli_epi16(r_epi16, shift);
		g_epi16 = _mm_slli_epi16(g_epi16, shift);
		b_epi16 = _mm_slli_epi16(b_epi16, shift);

		// Interleave the first four alpha and red values
		ar_epi16 = _mm_unpacklo_epi16(a_epi16, r_epi16);

		// Interleave the first four green and blue values
		gb_epi16 = _mm_unpacklo_epi16(g_epi16, b_epi16);

		// Interleave and store the first pair of ARGB tuples
		rgb_epi16 = _mm_unpacklo_epi32(ar_epi16, gb_epi16);
		_mm_store_si128(rgb_ptr++, rgb_epi16);

		// Interleave and store the second pair of ARGB tuples
		rgb_epi16 = _mm_unpackhi_epi32(ar_epi16, gb_epi16);
		_mm_store_si128(rgb_ptr++, rgb_epi16);

		// Interleave the second four alpha and red values
		ar_epi16 = _mm_unpackhi_epi16(a_epi16, r_epi16);

		// Interleave the second four green and blue values
		gb_epi16 = _mm_unpackhi_epi16(g_epi16, b_epi16);

		// Interleave and store the third pair of ARGB tuples
		rgb_epi16 = _mm_unpacklo_epi32(ar_epi16, gb_epi16);
		_mm_store_si128(rgb_ptr++, rgb_epi16);

		// Interleave and store the fourth pair of ARGB tuples
		rgb_epi16 = _mm_unpackhi_epi32(ar_epi16, gb_epi16);
		_mm_store_si128(rgb_ptr++, rgb_epi16);
	}

	// Should have terminated the loop at the post processing column
	assert(column == post_column);

	// Set the output pointer to the rest of the row of packed YUV
	rgb_output_ptr = (unsigned short *)rgb_ptr;

#endif

	// Handle end of row processing for the remaining columns
	for (; column < width; column++)
	{
		int r = r_input_ptr[column];
		int g = g_input_ptr[column];
		int b = b_input_ptr[column];

		// Descale the values
		r <<= shift;
		g <<= shift;
		b <<= shift;

		if (r < 0) r = 0; else if (r > rgb_max) r = rgb_max;
		if (g < 0) g = 0; else if (g > rgb_max) g = rgb_max;
		if (b < 0) b = 0; else if (b > rgb_max) b = rgb_max;

		*(rgb_output_ptr++) = r;
		*(rgb_output_ptr++) = g;
		*(rgb_output_ptr++) = b;
	}
}



// Used in RT YUYV playback
// Apply the inverse horizontal transform to reconstruct a strip of rows into packed YUV pixels
void ConvertRGB2YUV(PIXEL *rlineptr, PIXEL *glineptr, PIXEL *blineptr,
					int pitchr, int pitchg, int pitchb,
					uint8_t *output_image,		// Row of reconstructed results
					int output_pitch,		// Distance between rows in bytes
					int width,
					int height,
					int precision,
					int color_space,
					int format)
{
	//int num_channels = CODEC_NUM_CHANNELS;

	uint8_t *output = output_image;

	// Process 16 r,g,b coefficients per loop iteration
	const int column_step = 16;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int row;

	// Compute the amount of scaling required to reduce the output precision
	int descale_shift = (precision - 8);

	int shift = 8;

	float fy_rmult,fy_gmult,fy_bmult,fy_offset;
	float fu_rmult,fu_gmult,fu_bmult,fu_offset;
	float fv_rmult,fv_gmult,fv_bmult,fv_offset;
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
	float scale;

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

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip


	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr;

		const __m128i mask_epi32 = _mm_set1_epi32(0xffff);

		__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x00ff);
		__m128i limiter = _mm_set1_epi16(0x7fff - 0x3fff);

#endif
		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		//int32_t even = 0;
		//int32_t odd = 0;

		PIXEL *gptr = glineptr,*rptr = rlineptr,*bptr = blineptr;
		uint8_t *outputline = &output[0];

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


	// Advance to the next row of coefficients in each channel
		rptr += (pitchr / sizeof(PIXEL)) * row;
		gptr += (pitchg / sizeof(PIXEL)) * row;
		bptr += (pitchb / sizeof(PIXEL)) * row;

		// Advance the output pointer to the next row
		outputline += output_pitch * row;
		outptr = (__m128i *)outputline;

#if (1 && XMMOPT)

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i g1_output_epi16;
			__m128i g2_output_epi16;
			__m128i r1_output_epi16;
			__m128i r2_output_epi16;
			__m128i b1_output_epi16;
			__m128i b2_output_epi16;

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

			r1_output_epi16 = _mm_load_si128((__m128i *)rptr);	rptr += 8;
			r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, rounding1_pi16);
			r1_output_epi16 = _mm_srai_epi16(r1_output_epi16, descale_shift);

			g1_output_epi16 = _mm_load_si128((__m128i *)gptr); gptr += 8;
			g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, rounding1_pi16);
			g1_output_epi16 = _mm_srai_epi16(g1_output_epi16, descale_shift);

			b1_output_epi16 = _mm_load_si128((__m128i *)bptr); bptr += 8;
			b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, rounding1_pi16);
			b1_output_epi16 = _mm_srai_epi16(b1_output_epi16, descale_shift);


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







			r2_output_epi16 = _mm_load_si128((__m128i *)rptr);	rptr += 8;
			r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, rounding2_pi16);
			r2_output_epi16 = _mm_srai_epi16(r2_output_epi16, descale_shift);

			g2_output_epi16 = _mm_load_si128((__m128i *)gptr); gptr += 8;
			g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, rounding2_pi16);
			g2_output_epi16 = _mm_srai_epi16(g2_output_epi16, descale_shift);

			b2_output_epi16 = _mm_load_si128((__m128i *)bptr); bptr += 8;
			b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, rounding2_pi16);
			b2_output_epi16 = _mm_srai_epi16(b2_output_epi16, descale_shift);



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

			// Interleave the first four values from each chroma channel
			urg_epi16 = _mm_unpacklo_epi16(u1_output_epi16, v1_output_epi16);

			
			// Interleave the first eight chroma values with the first eight luma values
			if(format == DECODED_FORMAT_UYVY)
			{
				yuv1_epi16 = _mm_unpacklo_epi16(urg_epi16, y1_output_epi16);
				yuv2_epi16 = _mm_unpackhi_epi16(urg_epi16, y1_output_epi16);
			}
			else
			{
				yuv1_epi16 = _mm_unpacklo_epi16(y1_output_epi16, urg_epi16);
				yuv2_epi16 = _mm_unpackhi_epi16(y1_output_epi16, urg_epi16);
			}

			// Pack the first sixteen bytes of luma and chroma
			yuv1_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

			// Store the first sixteen bytes of output values
			_mm_store_si128(outptr++, yuv1_epi8);

			// Interleave the second four values from each chroma channel
			urg_epi16 = _mm_unpackhi_epi16(u1_output_epi16, v1_output_epi16);

			// Interleave the second eight chroma values with the second eight luma values
			if(format == DECODED_FORMAT_UYVY)
			{
				yuv1_epi16 = _mm_unpacklo_epi16(urg_epi16, y2_output_epi16);
				yuv2_epi16 = _mm_unpackhi_epi16(urg_epi16, y2_output_epi16);
			}
			else
			{
				yuv1_epi16 = _mm_unpacklo_epi16(y2_output_epi16, urg_epi16);
				yuv2_epi16 = _mm_unpackhi_epi16(y2_output_epi16, urg_epi16);
			}

			// Pack the second sixteen bytes of luma and chroma
			yuv2_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

			// Store the second sixteen bytes of output values
			_mm_store_si128(outptr++, yuv2_epi8);

		}

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

#endif
	}
}

// Apply the inverse horizontal transform to reconstruct a strip of rows into packed UYVY pixels
void ConvertRGB2UYVY(PIXEL *rlineptr, PIXEL *glineptr, PIXEL *blineptr,
					 int pitchr, int pitchg, int pitchb,
					 uint8_t *output_image,		// Row of reconstructed results
					 int output_pitch,			// Distance between rows in bytes
					 int width,
					 int height,
					 int precision,
					 int color_space,
					 int format)
{
	//int num_channels = CODEC_NUM_CHANNELS;

	uint8_t *output = output_image;

	// Process 16 r,g,b coefficients per loop iteration
	const int column_step = 16;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int row;

	// Compute the amount of scaling required to reduce the output precision
	int descale_shift = (precision - 8);

	int shift = 8;

	float fy_rmult,fy_gmult,fy_bmult,fy_offset;
	float fu_rmult,fu_gmult,fu_bmult,fu_offset;
	float fv_rmult,fv_gmult,fv_bmult,fv_offset;
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
	float scale;

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

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip


	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr;

		const __m128i mask_epi32 = _mm_set1_epi32(0xffff);

		__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x00ff);
		__m128i limiter = _mm_set1_epi16(0x7fff - 0x3fff);

#endif

		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		//int32_t even = 0;
		//int32_t odd = 0;

		PIXEL *gptr = glineptr,*rptr = rlineptr,*bptr = blineptr;
		uint8_t *outputline = &output[0];

		__m128i rounding1_pi16 = _mm_set1_epi16(0); // for 6bit matm
		__m128i rounding2_pi16 = _mm_set1_epi16(0); // for 6bit matm

		if(descale_shift>=2)
		{
			int mask = (1<<(descale_shift-1))-1; //DAN20090601
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


	// Advance to the next row of coefficients in each channel
		rptr += (pitchr / sizeof(PIXEL)) * row;
		gptr += (pitchg / sizeof(PIXEL)) * row;
		bptr += (pitchb / sizeof(PIXEL)) * row;

		// Advance the output pointer to the next row
		outputline += output_pitch * row;
		outptr = (__m128i *)outputline;

#if (1 && XMMOPT)

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			__m128i g1_output_epi16;
			__m128i g2_output_epi16;
			__m128i r1_output_epi16;
			__m128i r2_output_epi16;
			__m128i b1_output_epi16;
			__m128i b2_output_epi16;

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

			r1_output_epi16 = _mm_load_si128((__m128i *)rptr);	rptr += 8;
			r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, rounding1_pi16);
			r1_output_epi16 = _mm_srai_epi16(r1_output_epi16, descale_shift);

			g1_output_epi16 = _mm_load_si128((__m128i *)gptr); gptr += 8;
			g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, rounding1_pi16);
			g1_output_epi16 = _mm_srai_epi16(g1_output_epi16, descale_shift);

			b1_output_epi16 = _mm_load_si128((__m128i *)bptr); bptr += 8;
			b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, rounding1_pi16);
			b1_output_epi16 = _mm_srai_epi16(b1_output_epi16, descale_shift);

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

			r2_output_epi16 = _mm_load_si128((__m128i *)rptr);	rptr += 8;
			r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, rounding2_pi16);
			r2_output_epi16 = _mm_srai_epi16(r2_output_epi16, descale_shift);

			g2_output_epi16 = _mm_load_si128((__m128i *)gptr); gptr += 8;
			g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, rounding2_pi16);
			g2_output_epi16 = _mm_srai_epi16(g2_output_epi16, descale_shift);

			b2_output_epi16 = _mm_load_si128((__m128i *)bptr); bptr += 8;
			b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, rounding2_pi16);
			b2_output_epi16 = _mm_srai_epi16(b2_output_epi16, descale_shift);

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

			// Interleave the first four values from each chroma channel
			urg_epi16 = _mm_unpacklo_epi16(u1_output_epi16, v1_output_epi16);

			// Interleave the first eight chroma values with the first eight luma values
			//yuv1_epi16 = _mm_unpacklo_epi16(y1_output_epi16, urg_epi16);
			//yuv2_epi16 = _mm_unpackhi_epi16(y1_output_epi16, urg_epi16);
			yuv1_epi16 = _mm_unpacklo_epi16(urg_epi16, y1_output_epi16);
			yuv2_epi16 = _mm_unpackhi_epi16(urg_epi16, y1_output_epi16);

			// Pack the first sixteen bytes of luma and chroma
			yuv1_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

			// Store the first sixteen bytes of output values
			_mm_store_si128(outptr++, yuv1_epi8);

			// Interleave the second four values from each chroma channel
			urg_epi16 = _mm_unpackhi_epi16(u1_output_epi16, v1_output_epi16);

			// Interleave the second eight chroma values with the second eight luma values
			//yuv1_epi16 = _mm_unpacklo_epi16(y2_output_epi16, urg_epi16);
			//yuv2_epi16 = _mm_unpackhi_epi16(y2_output_epi16, urg_epi16);
			yuv1_epi16 = _mm_unpacklo_epi16(urg_epi16, y2_output_epi16);
			yuv2_epi16 = _mm_unpackhi_epi16(urg_epi16, y2_output_epi16);

			// Pack the second sixteen bytes of luma and chroma
			yuv2_epi8 = _mm_packus_epi16(yuv1_epi16, yuv2_epi16);

			// Store the second sixteen bytes of output values
			_mm_store_si128(outptr++, yuv2_epi8);
		}

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

#endif
	}
}

void ConvertRGBA48toRGB32(PIXEL *rlineptr, PIXEL *glineptr, PIXEL *blineptr, PIXEL *alineptr,
					int input_pitch,
					uint8_t *output_image,		// Row of reconstructed results
					int output_pitch,		// Distance between rows in bytes
					int width, int height, int precision, int color_space, int num_channels)
{
	uint8_t *output = output_image;

	// Process 16 r,g,b coefficients per loop iteration
	const int column_step = 16;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int row;

	// Compute the amount of scaling required to reduce the output precision
	int descale_shift = (precision - 8);

	int shift = 8;
	float scale;

	//alpha_Companded likely doesn't set as the is not does to go through Convert4444LinesToOutput

	scale = 64.0;
	shift-=2;

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip


	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr;

		const __m128i value128_epi16 = _mm_set1_epi16(128);
		const __m128i value128_epi8 = _mm_set1_epi8(128);

		__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x00ff);
#endif

		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		//int32_t even = 0;
		//int32_t odd = 0;

		PIXEL *gptr = glineptr,*rptr = rlineptr,*bptr = blineptr,*aptr = alineptr;
		uint8_t *outputline = &output[0];
		unsigned char *outbyteptr;

		__m128i rounding1_pi16 = _mm_set1_epi16(0); // for 6bit matm
		__m128i rounding2_pi16 = _mm_set1_epi16(0); // for 6bit matm

		if(descale_shift>=2)
		{
			int mask = (1<<(descale_shift-1))-1;
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


	// Advance to the next row of coefficients in each channel
		rptr += (input_pitch / sizeof(PIXEL)) * row;
		gptr += (input_pitch / sizeof(PIXEL)) * row;
		bptr += (input_pitch / sizeof(PIXEL)) * row;
		aptr += (input_pitch / sizeof(PIXEL)) * row;

		// Advance the output pointer to the next row
		outputline += output_pitch * row;
		outptr = (__m128i *)outputline;

		outbyteptr = (unsigned char *)outputline;

#if (1 && XMMOPT)

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

			__m128i R_epi8, G_epi8, B_epi8, A_epi8, RG, BA, RGBA;

			r1_output_epi16 = _mm_load_si128((__m128i *)rptr);	rptr += 8;
			r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, rounding1_pi16);
			r1_output_epi16 = _mm_srai_epi16(r1_output_epi16, descale_shift);

			g1_output_epi16 = _mm_load_si128((__m128i *)gptr); gptr += 8;
			g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, rounding1_pi16);
			g1_output_epi16 = _mm_srai_epi16(g1_output_epi16, descale_shift);

			b1_output_epi16 = _mm_load_si128((__m128i *)bptr); bptr += 8;
			b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, rounding1_pi16);
			b1_output_epi16 = _mm_srai_epi16(b1_output_epi16, descale_shift);


			 r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, limiterRGB);
			 r1_output_epi16 = _mm_subs_epu16(r1_output_epi16, limiterRGB);
			 r1_output_epi16 = _mm_subs_epi16(r1_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign saturate will work

			 g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, limiterRGB);
			 g1_output_epi16 = _mm_subs_epu16(g1_output_epi16, limiterRGB);
			 g1_output_epi16 = _mm_subs_epi16(g1_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign saturate will work

			 b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, limiterRGB);
			 b1_output_epi16 = _mm_subs_epu16(b1_output_epi16, limiterRGB);
			 b1_output_epi16 = _mm_subs_epi16(b1_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign saturate will work


			if(num_channels == 4)
			{
				a1_output_epi16 = _mm_load_si128((__m128i *)aptr); aptr += 8;
				a1_output_epi16 = _mm_srai_epi16(a1_output_epi16, descale_shift);

				a1_output_epi16 = _mm_adds_epi16(a1_output_epi16, limiterRGB);
				a1_output_epi16 = _mm_subs_epu16(a1_output_epi16, limiterRGB);

					
				// Remove the alpha encoding curve. // 12-bit precision
				a1_output_epi16 = _mm_slli_epi16(a1_output_epi16, 4); // 12-bit
				a1_output_epi16 = _mm_subs_epu16(a1_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				a1_output_epi16 = _mm_slli_epi16(a1_output_epi16, 3);  //15-bit
				a1_output_epi16 = _mm_mulhi_epi16(a1_output_epi16, _mm_set1_epi16(alphacompandGain));
				a1_output_epi16 = _mm_srai_epi16(a1_output_epi16, 4); // 8-bit


				a1_output_epi16 = _mm_adds_epi16(a1_output_epi16, limiterRGB);
				a1_output_epi16 = _mm_subs_epu16(a1_output_epi16, limiterRGB);
				a1_output_epi16 = _mm_subs_epi16(a1_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign
			}


			r2_output_epi16 = _mm_load_si128((__m128i *)rptr);	rptr += 8;
			r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, rounding2_pi16);
			r2_output_epi16 = _mm_srai_epi16(r2_output_epi16, descale_shift);

			g2_output_epi16 = _mm_load_si128((__m128i *)gptr); gptr += 8;
			g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, rounding2_pi16);
			g2_output_epi16 = _mm_srai_epi16(g2_output_epi16, descale_shift);

			b2_output_epi16 = _mm_load_si128((__m128i *)bptr); bptr += 8;
			b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, rounding2_pi16);
			b2_output_epi16 = _mm_srai_epi16(b2_output_epi16, descale_shift);



			 r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, limiterRGB);
			 r2_output_epi16 = _mm_subs_epu16(r2_output_epi16, limiterRGB);
			 r2_output_epi16 = _mm_subs_epi16(r2_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign saturate will work

			 g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, limiterRGB);
			 g2_output_epi16 = _mm_subs_epu16(g2_output_epi16, limiterRGB);
			 g2_output_epi16 = _mm_subs_epi16(g2_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign saturate will work

			 b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, limiterRGB);
			 b2_output_epi16 = _mm_subs_epu16(b2_output_epi16, limiterRGB);
			 b2_output_epi16 = _mm_subs_epi16(b2_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign saturate will work


			if(num_channels == 4)
			{
				a2_output_epi16 = _mm_load_si128((__m128i *)aptr); aptr += 8;
				a2_output_epi16 = _mm_srai_epi16(a2_output_epi16, descale_shift);

				a2_output_epi16 = _mm_adds_epi16(a2_output_epi16, limiterRGB);
				a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, limiterRGB);

				// Remove the alpha encoding curve. // 12-bit precision
				a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 4); // 12-bit
				a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
				a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));
				a2_output_epi16 = _mm_srai_epi16(a2_output_epi16, 4); // 8-bit

				a2_output_epi16 = _mm_adds_epi16(a2_output_epi16, limiterRGB);
				a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, limiterRGB);
				a2_output_epi16 = _mm_subs_epi16(a2_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign
			}


			// Pack the RGB values
			R_epi8 = _mm_packs_epi16(b1_output_epi16, b2_output_epi16); //swapped with R
			R_epi8 = _mm_add_epi8(R_epi8,value128_epi8); //+128; 0 to 255
			G_epi8 = _mm_packs_epi16(g1_output_epi16, g2_output_epi16);
			G_epi8 = _mm_add_epi8(G_epi8,value128_epi8); //+128; 0 to 255
			B_epi8 = _mm_packs_epi16(r1_output_epi16, r2_output_epi16); //swapped with B
			B_epi8 = _mm_add_epi8(B_epi8,value128_epi8); //+128; 0 to 255

			if(num_channels == 4)
			{
				A_epi8 = _mm_packs_epi16(a1_output_epi16, a2_output_epi16); //swapped with B
				A_epi8 = _mm_add_epi8(A_epi8,value128_epi8); //+128; 0 to 255
			}
			else
			{
				A_epi8 = _mm_set1_epi8(RGBA_DEFAULT_ALPHA);
			}

			RG = _mm_unpacklo_epi8(R_epi8, G_epi8);
			BA = _mm_unpacklo_epi8(B_epi8, A_epi8);
			RGBA = _mm_unpacklo_epi16(RG, BA);
			_mm_storeu_si128(outptr++, RGBA);

			RGBA = _mm_unpackhi_epi16(RG, BA);
			_mm_storeu_si128(outptr++, RGBA);

			RG = _mm_unpackhi_epi8(R_epi8, G_epi8);
			BA = _mm_unpackhi_epi8(B_epi8, A_epi8);
			RGBA = _mm_unpacklo_epi16(RG, BA);
			_mm_storeu_si128(outptr++, RGBA);

			RGBA = _mm_unpackhi_epi16(RG, BA);
			_mm_storeu_si128(outptr++, RGBA);

		}

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

#endif
	}
}

void ConvertRGB48toRGB24(PIXEL *rlineptr, PIXEL *glineptr, PIXEL *blineptr,
					int pitchr, int pitchg, int pitchb,
					uint8_t *output_image,		// Row of reconstructed results
					int output_pitch,		// Distance between rows in bytes
					int width, int height, int precision, int color_space)
{
	//int num_channels = CODEC_NUM_CHANNELS;

	uint8_t *output = output_image;

	// Process 16 r,g,b coefficients per loop iteration
	const int column_step = 16;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int row;

	// Compute the amount of scaling required to reduce the output precision
	int descale_shift = (precision - 8);

	int shift = 8;
	float scale;

	scale = 64.0;
	shift-=2;

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip


	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
		// The fast loop merges values from different phases to allow aligned stores
		__m128i *outptr;

		const __m128i value128_epi16 = _mm_set1_epi16(128);
		const __m128i value128_epi8 = _mm_set1_epi8(128);

		__m128i limiterRGB = _mm_set1_epi16(0x7fff - 0x00ff);
		unsigned char Rbuffer[16];
		unsigned char Gbuffer[16];
		unsigned char Bbuffer[16];

		__m128i *r_ptr = (__m128i *)&Rbuffer[0];
		__m128i *g_ptr = (__m128i *)&Gbuffer[0];
		__m128i *b_ptr = (__m128i *)&Bbuffer[0];

#endif

		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		//int32_t even = 0;
		//int32_t odd = 0;

		PIXEL *gptr = glineptr,*rptr = rlineptr,*bptr = blineptr;
		uint8_t *outputline = &output[0];
		unsigned char *outbyteptr;

		__m128i rounding1_pi16 = _mm_set1_epi16(0); // for 6bit matm
		__m128i rounding2_pi16 = _mm_set1_epi16(0); // for 6bit matm

		if(descale_shift>=2)
		{
			int mask = (1<<(descale_shift-1))-1;
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


	// Advance to the next row of coefficients in each channel
		rptr += (pitchr / sizeof(PIXEL)) * row;
		gptr += (pitchg / sizeof(PIXEL)) * row;
		bptr += (pitchb / sizeof(PIXEL)) * row;

		// Advance the output pointer to the next row
		outputline += output_pitch * row;
		outptr = (__m128i *)outputline;

		outbyteptr = (unsigned char *)outputline;

#if (1 && XMMOPT)

		// The reconstruction filters use pixels starting at the first column
		for (; column < post_column; column += column_step)
		{
			int i;
			__m128i g1_output_epi16;
			__m128i g2_output_epi16;
			__m128i r1_output_epi16;
			__m128i r2_output_epi16;
			__m128i b1_output_epi16;
			__m128i b2_output_epi16;
			
			__m128i R_epi8, G_epi8, B_epi8;

			r1_output_epi16 = _mm_load_si128((__m128i *)rptr);	rptr += 8;
			r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, rounding1_pi16);
			r1_output_epi16 = _mm_srai_epi16(r1_output_epi16, descale_shift);

			g1_output_epi16 = _mm_load_si128((__m128i *)gptr); gptr += 8;
			g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, rounding1_pi16);
			g1_output_epi16 = _mm_srai_epi16(g1_output_epi16, descale_shift);

			b1_output_epi16 = _mm_load_si128((__m128i *)bptr); bptr += 8;
			b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, rounding1_pi16);
			b1_output_epi16 = _mm_srai_epi16(b1_output_epi16, descale_shift);


			 r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, limiterRGB);
			 r1_output_epi16 = _mm_subs_epu16(r1_output_epi16, limiterRGB);
			 r1_output_epi16 = _mm_subs_epi16(r1_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign saturate will work

			 g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, limiterRGB);
			 g1_output_epi16 = _mm_subs_epu16(g1_output_epi16, limiterRGB);
			 g1_output_epi16 = _mm_subs_epi16(g1_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign saturate will work

			 b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, limiterRGB);
			 b1_output_epi16 = _mm_subs_epu16(b1_output_epi16, limiterRGB);
			 b1_output_epi16 = _mm_subs_epi16(b1_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign saturate will work




			r2_output_epi16 = _mm_load_si128((__m128i *)rptr);	rptr += 8;
			r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, rounding2_pi16);
			r2_output_epi16 = _mm_srai_epi16(r2_output_epi16, descale_shift);

			g2_output_epi16 = _mm_load_si128((__m128i *)gptr); gptr += 8;
			g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, rounding2_pi16);
			g2_output_epi16 = _mm_srai_epi16(g2_output_epi16, descale_shift);

			b2_output_epi16 = _mm_load_si128((__m128i *)bptr); bptr += 8;
			b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, rounding2_pi16);
			b2_output_epi16 = _mm_srai_epi16(b2_output_epi16, descale_shift);



			 r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, limiterRGB);
			 r2_output_epi16 = _mm_subs_epu16(r2_output_epi16, limiterRGB);
			 r2_output_epi16 = _mm_subs_epi16(r2_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign saturate will work

			 g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, limiterRGB);
			 g2_output_epi16 = _mm_subs_epu16(g2_output_epi16, limiterRGB);
			 g2_output_epi16 = _mm_subs_epi16(g2_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign saturate will work

			 b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, limiterRGB);
			 b2_output_epi16 = _mm_subs_epu16(b2_output_epi16, limiterRGB);
			 b2_output_epi16 = _mm_subs_epi16(b2_output_epi16, value128_epi16);  //-128 to 127 so the 16 to 8 sign saturate will work


			// Pack the RGB values
			R_epi8 = _mm_packs_epi16(b1_output_epi16, b2_output_epi16); //swapped with R
			R_epi8 = _mm_add_epi8(R_epi8,value128_epi8); //+128; 0 to 255
			G_epi8 = _mm_packs_epi16(g1_output_epi16, g2_output_epi16);
			G_epi8 = _mm_add_epi8(G_epi8,value128_epi8); //+128; 0 to 255
			B_epi8 = _mm_packs_epi16(r1_output_epi16, r2_output_epi16); //swapped with B
			B_epi8 = _mm_add_epi8(B_epi8,value128_epi8); //+128; 0 to 255

			_mm_storeu_si128(r_ptr, R_epi8);
			_mm_storeu_si128(g_ptr, G_epi8);
			_mm_storeu_si128(b_ptr, B_epi8);

			for(i=0; i<16; i++)
			{
				*outbyteptr++ = Rbuffer[i];
				*outbyteptr++ = Gbuffer[i];
				*outbyteptr++ = Bbuffer[i];
			}
/*


			RG = _mm_unpacklo_epi8(R_epi8, G_epi8);
			BA = _mm_unpacklo_epi8(B_epi8, _mm_set1_epi8(RGBA_DEFAULT_ALPHA));
			RGBA = _mm_unpacklo_epi16(RG, BA);
			_mm_storeu_si128(outptr++, RGBA);

			RGBA = _mm_unpackhi_epi16(RG, BA);
			_mm_storeu_si128(outptr++, RGBA);

			RG = _mm_unpackhi_epi8(R_epi8, G_epi8);
			BA = _mm_unpackhi_epi8(B_epi8, _mm_set1_epi8(RGBA_DEFAULT_ALPHA));
			RGBA = _mm_unpacklo_epi16(RG, BA);
			_mm_storeu_si128(outptr++, RGBA);

			RGBA = _mm_unpackhi_epi16(RG, BA);
			_mm_storeu_si128(outptr++, RGBA);
			*/
		}

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

#endif
	}
}

/*!
	@brief Convert unpacked YCbYCr 4:2:2 organized by row into the Avid 10-bit 2.8 format

	The luma and chroma channels are organized by row.  Each input row of the input contains
	unpacked luma and chroma.  This routine assumes that the chroma channels are already
	in the correct order on input.  The Cb chroma channel comes before the Cr chroma channel.
*/
void ConvertYUV16ToCbYCrY_10bit_2_8(DECODER *decoder,
									 int width,
									 int height,
									 int linenum,
									 PIXEL16U *input,
									 uint8_t *output,
									 int pitch,
									 int format,
									 int whitepoint,
									 int flags)
{
	int planar = (flags & ACTIVEMETADATA_PLANAR);

	PIXEL16U *input_row_ptr = input;
	size_t input_row_pitch = width * 3;		// Input row pitch in units of pixels

	int row, column;

	uint8_t *upper_row_ptr;
	uint8_t *lower_row_ptr;

	size_t upper_row_pitch = width / 2;
	size_t lower_row_pitch = width * 2;

	if (decoder)
	{
		uint8_t *upper_plane = decoder->upper_plane;
		uint8_t *lower_plane = decoder->lower_plane;
		intptr_t line;

		// The output pointer argument is the lower row pointer
		lower_row_ptr = output;

		// Compute the line number from the lower row pointer and the base address of the lower plane
		assert((lower_row_ptr - lower_plane) % lower_row_pitch == 0);
		line = (lower_row_ptr - lower_plane) / lower_row_pitch;

		// Compute the address of the output for the upper plane
		upper_row_ptr = upper_plane + line * upper_row_pitch;
	}
	else
	{
		uint8_t *upper_plane = output;
		uint8_t *lower_plane = upper_plane + width * height / 2;

		upper_row_ptr = upper_plane;
		lower_row_ptr = lower_plane;
	}


	if (planar)
	{
		PIXEL16U *plane_array[3];
		//int plane_pitch[3];
		//const int input_pitch = width * 2 * 2;

		for (row = 0; row < height; row++)
		{
			// Chroma is already in the correct order on entry to this routine
			plane_array[0] = (PIXEL16U *)&input_row_ptr[0];
			plane_array[1] = (PIXEL16U *)&input_row_ptr[width];
			plane_array[2] = (PIXEL16U *)&input_row_ptr[width*2];

			//plane_pitch[0] = input_row_pitch;
			//plane_pitch[1] = input_row_pitch;
			//plane_pitch[2] = input_row_pitch;

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the upper plane
			for (column = 0; column < width; column += 2)
			{
				uint16_t Y1, Cr, Y2, Cb;
				uint16_t Y1_upper, Cr_upper, Y2_upper, Cb_upper;
				uint16_t Y1_lower, Cr_lower, Y2_lower, Cb_lower;
				uint16_t upper;

				// Process Y1
				Y1 = plane_array[0][column];
				Y1_upper = (Y1 >> 6) & 0x03;		// Least significant 2 bits
				Y1_lower = (Y1 >> 8) & 0xFF;		// Most significant 8 bits

				// Process Cb
				Cb = plane_array[1][column];
				Cb_upper = (Cb >> 6) & 0x03;		// Least significant 2 bits
				Cb_lower = (Cb >> 8) & 0xFF;		// Most significant 8 bits

				// Process Y2
				Y2 = plane_array[0][column + 1];
				Y2_upper = (Y2 >> 6) & 0x03;		// Least significant 2 bits
				Y2_lower = (Y2 >> 8) & 0xFF;		// Most significant 8 bits

				// Process Cr
				Cr = plane_array[2][column];
				Cr_upper = (Cr >> 6) & 0x03;		// Least significant 2 bits
				Cr_lower = (Cr >> 8) & 0xFF;		// Most significant 8 bits

				// Pack the least significant bits into a byte
				upper = (Cb_upper << 6) | (Y1_upper << 4) | (Cr_upper << 2) | Y2_upper;

				// Write the byte to the upper plane in the output image
				upper_row_ptr[column/2] = (uint8_t)upper;

				// Output the most significant bits of each component to the lower plane
				lower_row_ptr[2 * column + 0] = (uint8_t)Cb_lower;
				lower_row_ptr[2 * column + 1] = (uint8_t)Y1_lower;
				lower_row_ptr[2 * column + 2] = (uint8_t)Cr_lower;
				lower_row_ptr[2 * column + 3] = (uint8_t)Y2_lower;
			}

			input_row_ptr += input_row_pitch;
			upper_row_ptr += upper_row_pitch;
			lower_row_ptr += lower_row_pitch;
		}
	}
	else
	{	
		for (row = 0; row < height; row++)
		{
			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the upper plane
			for (column = 0; column < width; column += 2)
			{
				uint16_t Y1, Cr, Y2, Cb;
				uint16_t Y1_upper, Cr_upper, Y2_upper, Cb_upper;
				uint16_t Y1_lower, Cr_lower, Y2_lower, Cb_lower;
				uint16_t upper;

				// Process Y1
				Y1 = input_row_ptr[column*3];
				Y1_upper = (Y1 >> 6) & 0x03;		// Least significant 2 bits
				Y1_lower = (Y1 >> 8) & 0xFF;		// Most significant 8 bits

				// Process Cr
				Cr = input_row_ptr[column*3+1];
				Cr_upper = (Cr >> 6) & 0x03;		// Least significant 2 bits
				Cr_lower = (Cr >> 8) & 0xFF;		// Most significant 8 bits

				// Process Y2
				Y2 = input_row_ptr[(column + 1)*3];
				Y2_upper = (Y2 >> 6) & 0x03;		// Least significant 2 bits
				Y2_lower = (Y2 >> 8) & 0xFF;		// Most significant 8 bits

				// Process Cb
				Cb = input_row_ptr[column*3+2];
				Cb_upper = (Cb >> 6) & 0x03;		// Least significant 2 bits
				Cb_lower = (Cb >> 8) & 0xFF;		// Most significant 8 bits

				// Pack the least significant bits into a byte
				upper = (Cb_upper << 6) | (Y1_upper << 4) | (Cr_upper << 2) | Y2_upper;

				// Write the byte to the upper plane in the output image
				upper_row_ptr[column/2] = (uint8_t)upper;

				// Output the most significant bits of each component to the lower plane
				lower_row_ptr[2 * column + 0] = (uint8_t)Cb_lower;
				lower_row_ptr[2 * column + 1] = (uint8_t)Y1_lower;
				lower_row_ptr[2 * column + 2] = (uint8_t)Cr_lower;
				lower_row_ptr[2 * column + 3] = (uint8_t)Y2_lower;
			}

			input_row_ptr += input_row_pitch;
			upper_row_ptr += upper_row_pitch;
			lower_row_ptr += lower_row_pitch;
		}
	}
}

void ConvertCbYCrY_10bit_2_8ToRow16u(DECODER *decoder,
									 int width,
									 int height,
									 int linenum,
									 uint8_t *input,
									 PIXEL16U *output,
									 int pitch,
									 int format,
									 int whitepoint,
									 int flags)
{
	int planar = (flags & ACTIVEMETADATA_PLANAR);
	//PIXEL16U *input_row_ptr = input;
	//int input_row_pitch = width * 2;

	uint8_t *upper_plane = input;
	uint8_t *lower_plane = upper_plane + width * height / 2;

	uint8_t *upper_row_ptr = upper_plane;
	uint8_t *lower_row_ptr = lower_plane;

	int upper_row_pitch = width / 2;
	int lower_row_pitch = width * 2;

	uint8_t *output_row_ptr = (uint8_t *)output;
	int output_row_pitch = pitch;

	int row, column;

	// This routine only handles the planar case
	assert(planar);

	if (planar)
	{
		PIXEL16U *plane_array[3];
		//int plane_pitch[3];
		//const int input_pitch = width * 2 * 2;

		for (row = 0; row < height; row++)
		{
			plane_array[0] = (PIXEL16U *)&output_row_ptr[0];
			plane_array[1] = (PIXEL16U *)&output_row_ptr[2 * width];
			plane_array[2] = (PIXEL16U *)&output_row_ptr[3 * width];

			//plane_pitch[0] = output_row_pitch;
			//plane_pitch[1] = output_row_pitch;
			//plane_pitch[2] = output_row_pitch;

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Each byte in the upper plane yields two columns of output
			for (column = 0; column < width; column += 2)
			{
				PIXEL16U Y1_upper, Cr_upper, Y2_upper, Cb_upper;
				PIXEL16U Y1_lower, Cr_lower, Y2_lower, Cb_lower;
				PIXEL16U Y1, Cr, Y2, Cb;
				PIXEL16U upper;

				upper = upper_row_ptr[column/2];

				Cb_upper = (upper >> 6) & 0x03;
				Y1_upper = (upper >> 4) & 0x03;
				Cr_upper = (upper >> 2) & 0x03;
				Y2_upper = (upper >> 0) & 0x03;

				Cb_lower = lower_row_ptr[2 * column + 0];
				Y1_lower = lower_row_ptr[2 * column + 1];
				Cr_lower = lower_row_ptr[2 * column + 2];
				Y2_lower = lower_row_ptr[2 * column + 3];

				Y1 = (Y1_lower << 2) | Y1_upper;
				Y2 = (Y2_lower << 2) | Y2_upper;
				Cr = (Cr_lower << 2) | Cr_upper;
				Cb = (Cb_lower << 2) | Cb_upper;

				Y1 <<= 6;
				Y2 <<= 6;
				Cr <<= 6;
				Cb <<= 6;

				plane_array[0][column + 0] = Y1;
				plane_array[0][column + 1] = Y2;
				plane_array[1][column/2] = Cr;
				plane_array[2][column/2] = Cb;
			}

			upper_row_ptr += upper_row_pitch;
			lower_row_ptr += lower_row_pitch;
			output_row_ptr += output_row_pitch;
		}
	}
}

void ConvertYUV16ToCbYCrY_16bit_2_14(DECODER *decoder,
									  int width,
									  int height,
									  int linenum,
									  PIXEL16U *input,
									  uint8_t *output,
									  int pitch,
									  int format,
									  int whitepoint,
									  int flags)
{
	int planar = (flags & ACTIVEMETADATA_PLANAR);
	PIXEL16U *input_row_ptr = input;
	PIXEL16S *output_row_ptr = (PIXEL16S *)output;
	int input_row_pitch = width * 3;
	int output_row_pitch = width * 2;
	int row, column;

	if (planar)
	{
		PIXEL16U *plane_array[3];
		//int plane_pitch[3];
		//const int input_pitch = width * 2 * 2;

		for (row = 0; row < height; row++)
		{
			plane_array[0] = (PIXEL16U *)&input_row_ptr[0];
			plane_array[1] = (PIXEL16U *)&input_row_ptr[width];
			plane_array[2] = (PIXEL16U *)&input_row_ptr[width*2];

			//plane_pitch[0] = input_pitch;
			//plane_pitch[1] = input_pitch;
			//plane_pitch[2] = input_pitch;

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the upper plane
			for (column = 0; column < width; column += 2)
			{
				int32_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;
				int32_t Y1_signed, Cr_signed, Y2_signed, Cb_signed;

				// Convert 16-bit unsigned luma to Avid signed 2.14 format
				Y1_unsigned = plane_array[0][column];
				Y1_signed = Clamp16s(((Y1_unsigned - 4096) << 6) / 219);

				Y2_unsigned = plane_array[0][column + 1];
				Y2_signed = Clamp16s(((Y2_unsigned - 4096) << 6) / 219);

				// Convert 16-bit unsigned chroma to Avid signed 2.14 format
				Cr_unsigned = plane_array[1][column];
				Cr_signed = Clamp16s((((Cr_unsigned - 4096) << 6) / 224) - 8192);

				Cb_unsigned = plane_array[2][column];
				Cb_signed = Clamp16s((((Cb_unsigned - 4096) << 6) / 224) - 8192);

				// Output the signed 2.14 components for the next two pixels
				output_row_ptr[2 * column + 0] = Cb_signed;
				output_row_ptr[2 * column + 1] = Y1_signed;
				output_row_ptr[2 * column + 2] = Cr_signed;
				output_row_ptr[2 * column + 3] = Y2_signed;
			}

			input_row_ptr += input_row_pitch;
			output_row_ptr += output_row_pitch;
		}
	}
	else
	{
		for (row = 0; row < height; row++)
		{
			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the upper plane
			for (column = 0; column < width; column += 2)
			{
				int32_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;
				int32_t Y1_signed, Cr_signed, Y2_signed, Cb_signed;

				// Convert 16-bit unsigned luma to Avid signed 2.14 format
				Y1_unsigned = input_row_ptr[column*3];
				Y1_signed = Clamp16s(((Y1_unsigned - 4096) << 6) / 219);

				Y2_unsigned = input_row_ptr[(column + 1)*3];
				Y2_signed = Clamp16s(((Y2_unsigned - 4096) << 6) / 219);

				// Convert 16-bit unsigned chroma to Avid signed 2.14 format
				Cr_unsigned = input_row_ptr[column*3+1];
				Cr_signed = Clamp16s((((Cr_unsigned - 4096) << 6) / 224) - 8192);

				Cb_unsigned = input_row_ptr[column*3+2];
				Cb_signed = Clamp16s((((Cb_unsigned - 4096) << 6) / 224) - 8192);

				// Output the signed 2.14 components for the next two pixels
				output_row_ptr[2 * column + 0] = Cb_signed;
				output_row_ptr[2 * column + 1] = Y1_signed;
				output_row_ptr[2 * column + 2] = Cr_signed;
				output_row_ptr[2 * column + 3] = Y2_signed;
			}

			input_row_ptr += input_row_pitch;
			output_row_ptr += output_row_pitch;
		}
	}
}

void ConvertCbYCrY_16bit_2_14ToRow16u(DECODER *decoder,
									  int width,
									  int height,
									  int linenum,
									  uint8_t *input,
									  PIXEL16U *output,
									  int pitch,
									  int format,
									  int whitepoint,
									  int flags)
{
	int planar = (flags & ACTIVEMETADATA_PLANAR);
	PIXEL16S *input_row_ptr = (PIXEL16S *)input;
	PIXEL16U *output_row_ptr = (PIXEL16U *)output;
	int input_row_pitch = width * 2;
	int output_row_pitch = width * 2;
	int row, column;

	// This routine only handles the planar case
	assert(planar);

	if (planar)
	{
		PIXEL16U *plane_array[3];
		//int plane_pitch[3];
		//const int input_pitch = width * 2 * 2;

		for (row = 0; row < height; row++)
		{
			plane_array[0] = (PIXEL16U *)&output_row_ptr[0];
			plane_array[1] = (PIXEL16U *)&output_row_ptr[width];
			plane_array[2] = (PIXEL16U *)&output_row_ptr[width*3/2];

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the upper plane
			for (column = 0; column < width; column += 2)
			{
				int32_t Y1_signed, Cr_signed, Y2_signed, Cb_signed;
				int32_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

				// Convert Avid signed 2.14 format to 16-bit unsigned chroma
				Cb_signed = input_row_ptr[2 * column + 0];
				//Cb_unsigned = (((224 * (Cb_signed + 8192)) / 16384 + 16) << 8);
				Cb_unsigned = (((224 * (Cb_signed + 8192)) + (1 << 18)) >> 6);

				// Convert Avid signed 2.14 format to 16-bit unsigned luma
				Y1_signed = input_row_ptr[2 * column + 1];
				//Y1_unsigned = (((219 * Y1_signed) / 16384 + 16) << 8);
				Y1_unsigned = ((219 * Y1_signed + (1 << 18)) >> 6);

				// Convert Avid signed 2.14 format to 16-bit unsigned chroma
				Cr_signed = input_row_ptr[2 * column + 2];
				//Cr_unsigned = (((224 * (Cr_signed + 8192)) / 16384 + 16) << 8);
				Cr_unsigned = (((224 * (Cr_signed + 8192)) + (1 << 18)) >> 6);

				// Convert Avid signed 2.14 format to 16-bit unsigned luma
				Y2_signed = input_row_ptr[2 * column + 3];
				//Y2_unsigned = (((219 * Y2_signed) / 16384 + 16) << 8);
				Y2_unsigned = ((219 * Y2_signed + (1 << 18)) >> 6);

				Cb_unsigned = SATURATE_16U(Cb_unsigned);
				Y1_unsigned = SATURATE_16U(Y1_unsigned);
				Cr_unsigned = SATURATE_16U(Cr_unsigned);
				Y2_unsigned = SATURATE_16U(Y2_unsigned);

				// Output the unsigned 16-bit components for the next two pixels
				plane_array[0][column + 0] = Y1_unsigned;
				plane_array[0][column + 1] = Y2_unsigned;
				plane_array[1][column/2] = Cr_unsigned;
				plane_array[2][column/2] = Cb_unsigned;
			}

			input_row_ptr += input_row_pitch;
			output_row_ptr += output_row_pitch;
		}
	}
}

void ConvertYUV16ToCbYCrY_16bit_10_6(DECODER *decoder,
									  int width,
									  int height,
									  int linenum,
									  PIXEL16U *input,
									  uint8_t *output,
									  int pitch,
									  int format,
									  int whitepoint,
									  int flags)
{
	int planar = (flags & ACTIVEMETADATA_PLANAR);
	PIXEL16U *input_row_ptr = (PIXEL16U *)input;
	PIXEL16U *output_row_ptr = (PIXEL16U *)output;
	int input_row_pitch = width * 3;
	int output_row_pitch = width * 2;
	int row, column;

	if (planar)
	{
		PIXEL16U *input_plane_array[3];

		for (row = 0; row < height; row++)
		{
			input_plane_array[0] = (PIXEL16U *)&input_row_ptr[0];
			input_plane_array[1] = (PIXEL16U *)&input_row_ptr[width];
			input_plane_array[2] = (PIXEL16U *)&input_row_ptr[width*2];

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the upper plane
			for (column = 0; column < width; column += 2)
			{
				uint16_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

				// Convert 16-bit unsigned values to Avid unsigned 10.6 format
				Y1_unsigned = input_plane_array[0][column];
				Y2_unsigned = input_plane_array[0][column + 1];
				Cr_unsigned = input_plane_array[1][column];
				Cb_unsigned = input_plane_array[2][column];

				// Reorder the components into Avid 10.6 format
				output_row_ptr[2 * column + 0] = Cb_unsigned;
				output_row_ptr[2 * column + 1] = Y1_unsigned;
				output_row_ptr[2 * column + 2] = Cr_unsigned;
				output_row_ptr[2 * column + 3] = Y2_unsigned;
			}

			input_row_ptr += input_row_pitch;
			output_row_ptr += output_row_pitch;
		}
	}
	else
	{
		for (row = 0; row < height; row++)
		{
			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the upper plane
			for (column = 0; column < width; column += 2)
			{
				uint16_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

				// Convert 16-bit unsigned values to Avid unsigned 10.6 format
				Y1_unsigned = input_row_ptr[column*3];
				Y2_unsigned = input_row_ptr[(column + 1)*3];
				Cr_unsigned = input_row_ptr[column*3+1];
				Cb_unsigned = input_row_ptr[column*3+2];

				// Reorder the components into Avid 10.6 format
				output_row_ptr[2 * column + 0] = Cb_unsigned;
				output_row_ptr[2 * column + 1] = Y1_unsigned;
				output_row_ptr[2 * column + 2] = Cr_unsigned;
				output_row_ptr[2 * column + 3] = Y2_unsigned;
			}

			input_row_ptr += input_row_pitch;
			output_row_ptr += output_row_pitch;
		}
	}
}

void ConvertCbYCrY_16bit_10_6ToRow16u(DECODER *decoder,
									  int width,
									  int height,
									  int linenum,
									  uint8_t *input,
									  PIXEL16U *output,
									  int pitch,
									  int format,
									  int whitepoint,
									  int flags)
{
	int planar = (flags & ACTIVEMETADATA_PLANAR);
	PIXEL16U *input_row_ptr = (PIXEL16U *)input;
	PIXEL16U *output_row_ptr = (PIXEL16U *)output;
	int input_row_pitch = width * 2;
	int output_row_pitch = width * 2;
	int row, column;

	// This routine only handles the planar case
	assert(planar);

	if (planar)
	{
		PIXEL16U *output_plane_array[3];

		for (row = 0; row < height; row++)
		{
			output_plane_array[0] = (PIXEL16U *)&output_row_ptr[0];
			output_plane_array[1] = (PIXEL16U *)&output_row_ptr[width];
			output_plane_array[2] = (PIXEL16U *)&output_row_ptr[width*3/2];

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the upper plane
			for (column = 0; column < width; column += 2)
			{
				uint16_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

				// Reorder the components into CineForm 16-bit format
				Cb_unsigned = input_row_ptr[2 * column + 0];
				Y1_unsigned = input_row_ptr[2 * column + 1];
				Cr_unsigned = input_row_ptr[2 * column + 2];
				Y2_unsigned = input_row_ptr[2 * column + 3];

				//TODO: Need to byte swap the color components from big endian?

				// Output the unsigned 16-bit components for the next two pixels
				output_plane_array[0][column + 0] = Y1_unsigned;
				output_plane_array[0][column + 1] = Y2_unsigned;
				output_plane_array[1][column/2] = Cr_unsigned;
				output_plane_array[2][column/2] = Cb_unsigned;
			}

			input_row_ptr += input_row_pitch;
			output_row_ptr += output_row_pitch;
		}
	}
}

void ConvertYUV16ToCbYCrY_8bit(DECODER *decoder,
								int width,
								int height,
								int linenum,
								PIXEL16U *input,
								uint8_t *output,
								int pitch,
								int format,
								int whitepoint,
								int flags,
								int rgb2yuv[3][4],
								int yoffset)
{
	int planar = (flags & ACTIVEMETADATA_PLANAR);
	//int saturate = !(flags & ACTIVEMETADATA_PRESATURATED);
	PIXEL16U *input_row_ptr = (PIXEL16U *)input;
	PIXEL8U *output_row_ptr = (PIXEL8U *)output;
	//int input_row_pitch = width * 3;
	int output_row_pitch = width * 2;
	int row, column;


	// based on logic in bayer.c for other YUV conversions.
	// If the colorformatdone is set, we can have either planar or chunky 444 YUV
	//		use some dithering to create 8-bit 422 YUV values
	// If colorformat is not done, we have RGB data.  This needs to be converted to YUV first

	__m128i yyyyyyyy;
	__m128i uuuuuuuu;
	__m128i vvvvvvvv;
	__m128i tttttttt;
	__m128i ditheryy;
	__m128i ditheruu;
	__m128i dithervv;

	for(row=linenum; row<linenum+height; row++)
	{
		// set up dithering based on the line number
		if(row & 1)
		{
			ditheryy = _mm_set_epi16( 1, 15,  3, 13,  5, 11,  7,  9); // 5 bits of dither
			ditheruu = _mm_set_epi16( 2, 30,  6, 26, 10, 22, 14, 18);
			dithervv = _mm_set_epi16(18, 14, 22, 10, 26,  6, 30,  2);
		}
		else
		{
			ditheryy = _mm_set_epi16( 9,  7, 11,  5, 13,  3, 15,  1);
			ditheruu = _mm_set_epi16(18, 14, 22, 10, 26,  6, 30,  2);
			dithervv = _mm_set_epi16( 2, 30,  6, 26, 10, 22, 14, 18);
		}

		for(column=0; column<width; column+=8)
		{
			if(planar)
			{
				// load YUV from planes
				yyyyyyyy = _mm_loadu_si128((__m128i *)&input_row_ptr[0]);
				uuuuuuuu = _mm_loadu_si128((__m128i *)&input_row_ptr[width]);
				vvvvvvvv = _mm_loadu_si128((__m128i *)&input_row_ptr[width*2]);
				input_row_ptr+=8;

				yyyyyyyy = _mm_srli_epi16(yyyyyyyy, 4); //12-bit
				uuuuuuuu = _mm_srli_epi16(uuuuuuuu, 4); //12-bit
				vvvvvvvv = _mm_srli_epi16(vvvvvvvv, 4); //12-bit

			}
			else
			{
				// load YUV individually
				yyyyyyyy = _mm_insert_epi16(yyyyyyyy, input_row_ptr[0], 0);
				uuuuuuuu = _mm_insert_epi16(uuuuuuuu, input_row_ptr[1], 0);
				vvvvvvvv = _mm_insert_epi16(vvvvvvvv, input_row_ptr[2], 0);
				yyyyyyyy = _mm_insert_epi16(yyyyyyyy, input_row_ptr[3], 1);
				uuuuuuuu = _mm_insert_epi16(uuuuuuuu, input_row_ptr[4], 1);
				vvvvvvvv = _mm_insert_epi16(vvvvvvvv, input_row_ptr[5], 1);
				yyyyyyyy = _mm_insert_epi16(yyyyyyyy, input_row_ptr[6], 2);
				uuuuuuuu = _mm_insert_epi16(uuuuuuuu, input_row_ptr[7], 2);
				vvvvvvvv = _mm_insert_epi16(vvvvvvvv, input_row_ptr[8], 2);
				yyyyyyyy = _mm_insert_epi16(yyyyyyyy, input_row_ptr[9], 3);
				uuuuuuuu = _mm_insert_epi16(uuuuuuuu, input_row_ptr[10], 3);
				vvvvvvvv = _mm_insert_epi16(vvvvvvvv, input_row_ptr[11], 3);
				yyyyyyyy = _mm_insert_epi16(yyyyyyyy, input_row_ptr[12], 4);
				uuuuuuuu = _mm_insert_epi16(uuuuuuuu, input_row_ptr[13], 4);
				vvvvvvvv = _mm_insert_epi16(vvvvvvvv, input_row_ptr[14], 4);
				yyyyyyyy = _mm_insert_epi16(yyyyyyyy, input_row_ptr[15], 5);
				uuuuuuuu = _mm_insert_epi16(uuuuuuuu, input_row_ptr[16], 5);
				vvvvvvvv = _mm_insert_epi16(vvvvvvvv, input_row_ptr[17], 5);
				yyyyyyyy = _mm_insert_epi16(yyyyyyyy, input_row_ptr[18], 6);
				uuuuuuuu = _mm_insert_epi16(uuuuuuuu, input_row_ptr[19], 6);
				vvvvvvvv = _mm_insert_epi16(vvvvvvvv, input_row_ptr[20], 6);
				yyyyyyyy = _mm_insert_epi16(yyyyyyyy, input_row_ptr[21], 7);
				uuuuuuuu = _mm_insert_epi16(uuuuuuuu, input_row_ptr[22], 7);
				vvvvvvvv = _mm_insert_epi16(vvvvvvvv, input_row_ptr[23], 7);

				input_row_ptr += 24;
				
				yyyyyyyy = _mm_srli_epi16(yyyyyyyy, 4); //12-bit
				uuuuuuuu = _mm_srli_epi16(uuuuuuuu, 4); //12-bit
				vvvvvvvv = _mm_srli_epi16(vvvvvvvv, 4); //12-bit
			}
	
			//
			// dither to convert to 422, shift right to 8 bits
			//
			{
				yyyyyyyy = _mm_srai_epi16(yyyyyyyy, 1);
				uuuuuuuu = _mm_srai_epi16(uuuuuuuu, 1);
				vvvvvvvv = _mm_srai_epi16(vvvvvvvv, 1);

				yyyyyyyy = _mm_adds_epi16(yyyyyyyy, ditheryy);
				yyyyyyyy = _mm_srai_epi16(yyyyyyyy, 3);

				tttttttt = _mm_slli_si128(uuuuuuuu, 2);
				uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);
				uuuuuuuu = _mm_adds_epi16(uuuuuuuu, ditheruu);
				uuuuuuuu = _mm_srai_epi16(uuuuuuuu, 4);//5);

				tttttttt = _mm_slli_si128(vvvvvvvv, 2);
				vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);
				vvvvvvvv = _mm_adds_epi16(vvvvvvvv, dithervv);
				vvvvvvvv = _mm_srai_epi16(vvvvvvvv, 4);//5);
			}
			//
			// store the pixel values
			//
			output_row_ptr[2 * column + 0] = _mm_extract_epi16(uuuuuuuu, 1);
			output_row_ptr[2 * column + 1] = _mm_extract_epi16(yyyyyyyy, 0);
			output_row_ptr[2 * column + 2] = _mm_extract_epi16(vvvvvvvv, 1);
			output_row_ptr[2 * column + 3] = _mm_extract_epi16(yyyyyyyy, 1);

			output_row_ptr[2 * column + 4] = _mm_extract_epi16(uuuuuuuu, 3);
			output_row_ptr[2 * column + 5] = _mm_extract_epi16(yyyyyyyy, 2);
			output_row_ptr[2 * column + 6] = _mm_extract_epi16(vvvvvvvv, 3);
			output_row_ptr[2 * column + 7] = _mm_extract_epi16(yyyyyyyy, 3);

			output_row_ptr[2 * column + 8] = _mm_extract_epi16(uuuuuuuu, 5);
			output_row_ptr[2 * column + 9] = _mm_extract_epi16(yyyyyyyy, 4);
			output_row_ptr[2 * column + 10] = _mm_extract_epi16(vvvvvvvv, 5);
			output_row_ptr[2 * column + 11] = _mm_extract_epi16(yyyyyyyy, 5);

			output_row_ptr[2 * column + 12] = _mm_extract_epi16(uuuuuuuu, 7);
			output_row_ptr[2 * column + 13] = _mm_extract_epi16(yyyyyyyy, 6);
			output_row_ptr[2 * column + 14] = _mm_extract_epi16(vvvvvvvv, 7);
			output_row_ptr[2 * column + 15] = _mm_extract_epi16(yyyyyyyy, 7);
		}

		if(planar)
		{
			input_row_ptr += width*2;
		}
		output_row_ptr += output_row_pitch;
	}

}

void ConvertCbYCrY_8bitToRow16u(DECODER *decoder,
								int width,
								int height,
								int linenum,
								uint8_t *input,
								PIXEL16U *output,
								int pitch,
								int format,
								int whitepoint,
								int flags)
{
	int planar = (flags & ACTIVEMETADATA_PLANAR);
	PIXEL8U *input_row_ptr = (PIXEL8U *)input;
	PIXEL16U *output_row_ptr = (PIXEL16U *)output;
	int input_row_pitch = width * 2;
	int output_row_pitch = width * 2;
	int row, column;

	// This routine only handles the planar case
	assert(planar);

	if (planar)
	{
		PIXEL16U *output_plane_array[3];

		for (row = 0; row < height; row++)
		{
			output_plane_array[0] = (PIXEL16U *)&output_row_ptr[0];
			output_plane_array[1] = (PIXEL16U *)&output_row_ptr[width];
			output_plane_array[2] = (PIXEL16U *)&output_row_ptr[width*3/2];

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the upper plane
			for (column = 0; column < width; column += 2)
			{
				uint16_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

				// Reorder the components into CineForm 16-bit format
				Cb_unsigned = input_row_ptr[2 * column + 0];
				Y1_unsigned = input_row_ptr[2 * column + 1];
				Cr_unsigned = input_row_ptr[2 * column + 2];
				Y2_unsigned = input_row_ptr[2 * column + 3];

				// Upscale the 8-bit components to 16 bits
				Cb_unsigned <<= 8;
				Y1_unsigned <<= 8;
				Cr_unsigned <<= 8;
				Y2_unsigned <<= 8;

				// Output the unsigned 16-bit components for the next two pixels
				output_plane_array[0][column + 0] = Y1_unsigned;
				output_plane_array[0][column + 1] = Y2_unsigned;
				output_plane_array[1][column/2] = Cr_unsigned;
				output_plane_array[2][column/2] = Cb_unsigned;
			}

			input_row_ptr += input_row_pitch;
			output_row_ptr += output_row_pitch;
		}
	}
}

void ConvertYUV16ToCbYCrY_16bit(DECODER *decoder,
								 int width,
								 int height,
								 int linenum,
								 PIXEL16U *input,
								 uint8_t *output,
								 int pitch,
								 int format,
								 int whitepoint,
								 int flags)
{
	int planar = (flags & ACTIVEMETADATA_PLANAR);
	uint16_t *input_row_ptr = (uint16_t *)input;
	uint16_t *output_row_ptr = (uint16_t *)output;
	int input_row_pitch = width * 3;
	int output_row_pitch = width * 2;
	int row, column;

	// This routine only handles the planar case
	
	if (planar)
	{
		uint16_t *input_plane_array[3];

		for (row = 0; row < height; row++)
		{
			input_plane_array[0] = (uint16_t *)&input_row_ptr[0];
			input_plane_array[1] = (uint16_t *)&input_row_ptr[width];
			input_plane_array[2] = (uint16_t *)&input_row_ptr[width*2];

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the upper plane
			for (column = 0; column < width; column += 2)
			{
				uint16_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

				// Reorder the components into Avid format
				Y1_unsigned = input_plane_array[0][column];
				Y2_unsigned = input_plane_array[0][column + 1];
				Cr_unsigned = input_plane_array[1][column];
				Cb_unsigned = input_plane_array[2][column];

				output_row_ptr[2 * column + 0] = Cb_unsigned;
				output_row_ptr[2 * column + 1] = Y1_unsigned;
				output_row_ptr[2 * column + 2] = Cr_unsigned;
				output_row_ptr[2 * column + 3] = Y2_unsigned;
			}

			input_row_ptr += input_row_pitch;
			output_row_ptr += output_row_pitch;
		}
	}
	else
	{
		//uint16_t *input_plane_array[3];

		for (row = 0; row < height; row++)
		{
			//input_plane_array[0] = (uint16_t *)&input_row_ptr[0];
			//input_plane_array[1] = (uint16_t *)&input_row_ptr[width];
			//input_plane_array[2] = (uint16_t *)&input_row_ptr[width*2];

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the upper plane
			for (column = 0; column < width; column += 2)
			{
				uint16_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

				// Reorder the components into Avid format
				Y1_unsigned = input_row_ptr[column*3];
				Y2_unsigned = input_row_ptr[(column + 1)*3];
				Cr_unsigned = input_row_ptr[column*3+1];
				Cb_unsigned = input_row_ptr[column*3+2];

				output_row_ptr[2 * column + 0] = Cb_unsigned;
				output_row_ptr[2 * column + 1] = Y1_unsigned;
				output_row_ptr[2 * column + 2] = Cr_unsigned;
				output_row_ptr[2 * column + 3] = Y2_unsigned;
			}

			input_row_ptr += input_row_pitch;
			output_row_ptr += output_row_pitch;
		}
	}
}

void ConvertCbYCrY_16bitToRow16u(DECODER *decoder,
								 int width,
								 int height,
								 int linenum,
								 uint8_t *input,
								 PIXEL16U *output,
								 int pitch,
								 int format,
								 int whitepoint,
								 int flags)
{
	int planar = (flags & ACTIVEMETADATA_PLANAR);
	PIXEL16U *input_row_ptr = (PIXEL16U *)input;
	PIXEL16U *output_row_ptr = (PIXEL16U *)output;
	int input_row_pitch = width * 2;
	int output_row_pitch = width * 2;
	int row, column;

	// This routine only handles the planar case
	assert(planar);

	if (planar)
	{
		PIXEL16U *output_plane_array[3];

		for (row = 0; row < height; row++)
		{
			output_plane_array[0] = (PIXEL16U *)&output_row_ptr[0];
			output_plane_array[1] = (PIXEL16U *)&output_row_ptr[width];
			output_plane_array[2] = (PIXEL16U *)&output_row_ptr[width*3/2];

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the upper plane
			for (column = 0; column < width; column += 2)
			{
				uint16_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

				// Reorder the components into CineForm 16-bit format
				Cb_unsigned = input_row_ptr[2 * column + 0];
				Y1_unsigned = input_row_ptr[2 * column + 1];
				Cr_unsigned = input_row_ptr[2 * column + 2];
				Y2_unsigned = input_row_ptr[2 * column + 3];

				output_plane_array[0][column + 0] = Y1_unsigned;
				output_plane_array[0][column + 1] = Y2_unsigned;
				output_plane_array[1][column/2] = Cr_unsigned;
				output_plane_array[2][column/2] = Cb_unsigned;
			}

			input_row_ptr += input_row_pitch;
			output_row_ptr += output_row_pitch;
		}
	}
}

/*!
	@brief Convert 16-bit YUV 4:2:2 to luma and chroma planes in NV12 format

	The output is an upper plane of 8-bit luma and a lower plane of interleaved
	8-bit chroma with 4:2:0 sampling.  The chroma plane is half the height of the
	luma plane.

	This routine has not been optimized for performance and does not average the
	chroma rows when downsampling to 4:2:0.
*/
void ConvertYUV16ToNV12(DECODER *decoder,
						int width,
						int height,
						int linenum,
						uint16_t *input,
						uint8_t *output,
						int pitch,
						int format,
						int whitepoint,
						int flags)
{
	int planar = (flags & ACTIVEMETADATA_PLANAR);
	uint16_t *input_row_ptr = input;
	uint8_t *luma_row_ptr = output;
	uint8_t *chroma_row_ptr = NULL;
	int input_row_pitch = width * 3;
	int output_row_pitch = width;
	int row, column;

	size_t diffline = ((size_t)luma_row_ptr - (size_t)decoder->local_output) / width;

	chroma_row_ptr = decoder->local_output + width * decoder->frame.height + width * (diffline/2);

	// The routine processes a single row or an even number of rows
	assert(height == 1 || ((height % 2) == 0));

	if (planar)
	{
		uint16_t *input_plane_array[3];

		for (row = 0; row < height; row++)
		{
			input_plane_array[0] = (uint16_t *)&input_row_ptr[0];
			input_plane_array[1] = (uint16_t *)&input_row_ptr[width];
			input_plane_array[2] = (uint16_t *)&input_row_ptr[width*2];

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the luma plane
			for (column = 0; column < width; column += 2)
			{
				uint32_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

				// Get the luma and chroma components from the separate planes
				Y1_unsigned = input_plane_array[0][column + 0];
				Y2_unsigned = input_plane_array[0][column + 1];
				Cr_unsigned = input_plane_array[1][column + 0] + input_plane_array[1][column + 1];
				Cb_unsigned = input_plane_array[2][column + 0] + input_plane_array[2][column + 1];

				// Reduce the input components to eight bits
				Y1_unsigned >>= 8;
				Y2_unsigned >>= 8;
				Cr_unsigned >>= 9;
				Cb_unsigned >>= 9;

				// Output two luma components to the upper plane
				luma_row_ptr[column + 0] = Y1_unsigned;
				luma_row_ptr[column + 1] = Y2_unsigned;

				//TODO: Average the chroma rows
				if ((height == 1 && (linenum % 2) == 1) || (row % 2) == 1)
				{
					// Output two interleaved chroma components to the lower plane
					chroma_row_ptr[column + 0] = Cr_unsigned;
					chroma_row_ptr[column + 1] = Cb_unsigned;
				}
			}

			input_row_ptr += input_row_pitch;
			luma_row_ptr += output_row_pitch;

			if ((height == 1 && (linenum % 2) == 1) || (row % 2) == 1) {
				chroma_row_ptr += output_row_pitch;
			}
		}
	}
	else
	{
		//uint16_t *input_plane_array[3];

		for (row = 0; row < height; row++)
		{
			//input_plane_array[0] = (uint16_t *)&input_row_ptr[0];
			//input_plane_array[1] = (uint16_t *)&input_row_ptr[width];
			//input_plane_array[2] = (uint16_t *)&input_row_ptr[width*2];

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the luma plane
			for (column = 0; column < width; column += 2)
			{
				uint32_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

				// Get the luma and chroma components from the row
				Y1_unsigned = input_row_ptr[column*3];
				Y2_unsigned = input_row_ptr[(column + 1)*3];
				Cr_unsigned = input_row_ptr[column*3+1] + input_row_ptr[(column + 1)*3+1];
				Cb_unsigned = input_row_ptr[column*3+2] + input_row_ptr[(column + 1)*3+2];

				// Reduce the input components to eight bits
				Y1_unsigned >>= 8;
				Y2_unsigned >>= 8;
				Cr_unsigned >>= 9;
				Cb_unsigned >>= 9;

				// Output two luma components to the upper plane
				luma_row_ptr[column + 0] = Y1_unsigned;
				luma_row_ptr[column + 1] = Y2_unsigned;

				//TODO: Average the chroma rows
				if ((height == 1 && (linenum % 2) == 1) || (row % 2) == 1)
				{
					// Output two interleaved chroma components to the lower plane
					chroma_row_ptr[column + 0] = Cr_unsigned;
					chroma_row_ptr[column + 1] = Cb_unsigned;
				}
			}

			input_row_ptr += input_row_pitch;
			luma_row_ptr += output_row_pitch;

			if ((height == 1 && (linenum % 2) == 1) || (row % 2) == 1) {
				chroma_row_ptr += output_row_pitch;
			}
		}
	}
}

/*!
	@brief Convert 16-bit YUV 4:2:2 to luma and chroma planes in YV12 format

	The output is an upper plane of 8-bit luma and two lower planes of 8-bit
	chroma with 4:2:0 sampling.  Each chroma plane is half the height and half
	the width of the luma plane.

	This routine has not been optimized for performance and does not average the
	chroma rows when downsampling to 4:2:0.
*/
void ConvertYUV16ToYV12(DECODER *decoder,
						int width,
						int height,
						int linenum,
						uint16_t *input,
						uint8_t *output,
						int pitch,
						int format,
						int whitepoint,
						int flags)
{
	int planar = (flags & ACTIVEMETADATA_PLANAR);
	uint16_t *input_row_ptr = input;
	uint8_t *Y_row_ptr = output;
	uint8_t *V_row_ptr = Y_row_ptr + width * height;
	uint8_t *U_row_ptr = V_row_ptr + (width * height) / 4;
	int input_row_pitch = width * 3;
	int Y_row_pitch = width;
	int V_row_pitch = width / 2;
	int U_row_pitch = width / 2;
	int row, column;

	// The routine processes a single row or an even number of rows
	assert(height == 1 || ((height % 2) == 0));

	if (planar)
	{
		uint16_t *input_plane_array[3];

		for (row = 0; row < height; row++)
		{
			input_plane_array[0] = (uint16_t *)&input_row_ptr[0];
			input_plane_array[1] = (uint16_t *)&input_row_ptr[width];
			input_plane_array[2] = (uint16_t *)&input_row_ptr[width*2];

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the luma plane
			for (column = 0; column < width; column += 2)
			{
				uint16_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

				// Get the luma and chroma components from the separate planes
				Y1_unsigned = input_plane_array[0][column + 0];
				Y2_unsigned = input_plane_array[0][column + 1];
				Cr_unsigned = input_plane_array[1][column + 0];
				Cb_unsigned = input_plane_array[2][column + 0];

				// Reduce the input components to eight bits
				Y1_unsigned >>= 8;
				Y2_unsigned >>= 8;
				Cr_unsigned >>= 8;
				Cb_unsigned >>= 8;

				// Output two luma components to the upper plane
				Y_row_ptr[column + 0] = (uint8_t)Y1_unsigned;
				Y_row_ptr[column + 1] = (uint8_t)Y2_unsigned;

				//TODO: Average the chroma rows
				if ((height == 1 && (linenum % 2) == 1) || (row % 2) == 1)
				{
					// Output two interleaved chroma components to the lower plane
					U_row_ptr[column/2] = (uint8_t)Cb_unsigned;
					V_row_ptr[column/2] = (uint8_t)Cr_unsigned;
				}
			}

			input_row_ptr += input_row_pitch;
			Y_row_ptr += Y_row_pitch;

			if ((height == 1 && (linenum % 2) == 1) || (row % 2) == 1)
			{
				U_row_ptr += U_row_pitch;
				V_row_ptr += V_row_pitch;
			}
		}
	}
	else
	{
		//uint16_t *input_plane_array[3];

		for (row = 0; row < height; row++)
		{
			//input_plane_array[0] = (uint16_t *)&input_row_ptr[0];
			//input_plane_array[1] = (uint16_t *)&input_row_ptr[width];
			//input_plane_array[2] = (uint16_t *)&input_row_ptr[width*2];

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Two columns of input yield one byte of output in the luma plane
			for (column = 0; column < width; column += 2)
			{
				uint16_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

				// Get the luma and chroma components from the row
				Y1_unsigned = input_row_ptr[column*3];
				Y2_unsigned = input_row_ptr[(column + 1)*3];
				Cr_unsigned = input_row_ptr[column*3+1];
				Cb_unsigned = input_row_ptr[column*3+2];

				// Reduce the input components to eight bits
				Y1_unsigned >>= 8;
				Y2_unsigned >>= 8;
				Cr_unsigned >>= 8;
				Cb_unsigned >>= 8;

				// Output two luma components to the upper plane
				Y_row_ptr[column + 0] = (uint8_t)Y1_unsigned;
				Y_row_ptr[column + 1] = (uint8_t)Y2_unsigned;

				//TODO: Average the chroma rows
				if ((height == 1 && (linenum % 2) == 1) || (row % 2) == 1)
				{
					// Output two interleaved chroma components to the lower plane
					U_row_ptr[column/2] = (uint8_t)Cb_unsigned;
					V_row_ptr[column/2] = (uint8_t)Cr_unsigned;
				}
			}

			input_row_ptr += input_row_pitch;
			Y_row_ptr += Y_row_pitch;

			if ((height == 1 && (linenum % 2) == 1) || (row % 2) == 1)
			{
				U_row_ptr += U_row_pitch;
				V_row_ptr += V_row_pitch;
			}
		}
	}
}



bool ConvertPreformatted3D(DECODER *decoder, int use_local_buffer, int internal_format, int channel_mask, uint8_t *local_output, int local_pitch, int *channel_offset_ptr)
{
	bool ret = true;
	int channel_offset = *channel_offset_ptr;
	int swapLR = decoder->cfhddata.FramingFlags & 2 ? 1 : 0;
	
	int leftonly = ((channel_mask & 1) == 1);
	int righonly = ((channel_mask & 2) == 2);
	int temp;

	if(swapLR)
	{
		temp = leftonly;
		leftonly = righonly;
		righonly = temp;
	}

	if(decoder->channel_decodes == 2 && decoder->source_channels <= 1)
	{
		//fake a second channel for 3D engine

		if(use_local_buffer && decoder->preformatted_3D_type == BLEND_STACKED_ANAMORPHIC)
		{
		//	if(decoder->channel_blend_type == BLEND_STACKED_ANAMORPHIC) // special case as it is a native decode mode
		//	{						
		//		decoder->frame.resolution = DECODED_RESOLUTION_HALF_VERTICAL;
		//		decoder->frame.height /= 2;
		//		channel_offset /= 2;
		//	}
		//	else
			{
				if(internal_format == DECODED_FORMAT_W13A)
				{
					PIXEL *srcLeftRGB = (PIXEL *)local_output;
					PIXEL *srcRighRGB = (PIXEL *)(local_output+channel_offset/2);
					PIXEL *newRighRGB = (PIXEL *)(local_output+channel_offset);
					PIXEL *newLeftRGB = (PIXEL *)local_output;
					int x,y,lwidth = 0;
					int lheight = channel_offset / local_pitch;

					int newline = local_pitch / sizeof(PIXEL);
					lwidth = local_pitch / 8;			
					for(y=0;y<lheight/2-1;y++)
					{
						for(x=0;x<lwidth;x++)
						{
							newRighRGB[x*4+0] = srcRighRGB[x*4+0];
							newRighRGB[x*4+1] = srcRighRGB[x*4+1];
							newRighRGB[x*4+2] = srcRighRGB[x*4+2];
							newRighRGB[x*4+3] = srcRighRGB[x*4+3];
							newRighRGB[x*4+0+newline] = (srcRighRGB[x*4+0]+srcRighRGB[x*4+0+newline])>>1;
							newRighRGB[x*4+1+newline] = (srcRighRGB[x*4+1]+srcRighRGB[x*4+1+newline])>>1;
							newRighRGB[x*4+2+newline] = (srcRighRGB[x*4+2]+srcRighRGB[x*4+2+newline])>>1;
							newRighRGB[x*4+3+newline] = (srcRighRGB[x*4+3]+srcRighRGB[x*4+3+newline])>>1;
						}
						
						newRighRGB += 2*newline;
						srcRighRGB += newline;
					}
					for(x=0;x<lwidth;x++)
					{
						newRighRGB[x*4+0] = srcRighRGB[x*4+0];
						newRighRGB[x*4+1] = srcRighRGB[x*4+1];
						newRighRGB[x*4+2] = srcRighRGB[x*4+2];
						newRighRGB[x*4+3] = srcRighRGB[x*4+3];
						newRighRGB[x*4+0+newline] = srcRighRGB[x*4+0];
						newRighRGB[x*4+1+newline] = srcRighRGB[x*4+1];
						newRighRGB[x*4+2+newline] = srcRighRGB[x*4+2];
						newRighRGB[x*4+3+newline] = srcRighRGB[x*4+3];
					}

	
					y=lheight/2-1;
					newLeftRGB += y*2*newline;
					srcLeftRGB += y*newline;

					for(;y>=0;y--)
					{
						if(y == lheight/2-1)
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*4+0] = srcLeftRGB[x*4+0];
								newLeftRGB[x*4+1] = srcLeftRGB[x*4+1];
								newLeftRGB[x*4+2] = srcLeftRGB[x*4+2];
								newLeftRGB[x*4+3] = srcLeftRGB[x*4+3];
								newLeftRGB[x*4+0+newline] = srcLeftRGB[x*4+0];
								newLeftRGB[x*4+1+newline] = srcLeftRGB[x*4+1];
								newLeftRGB[x*4+2+newline] = srcLeftRGB[x*4+2];
								newLeftRGB[x*4+3+newline] = srcLeftRGB[x*4+3];
							}
						}
						else
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*4+0] = srcLeftRGB[x*4+0];
								newLeftRGB[x*4+1] = srcLeftRGB[x*4+1];
								newLeftRGB[x*4+2] = srcLeftRGB[x*4+2];
								newLeftRGB[x*4+3] = srcLeftRGB[x*4+3];
								newLeftRGB[x*4+0+newline] = (srcLeftRGB[x*4+0]+srcLeftRGB[x*4+0+newline])>>1;
								newLeftRGB[x*4+1+newline] = (srcLeftRGB[x*4+1]+srcLeftRGB[x*4+1+newline])>>1;
								newLeftRGB[x*4+2+newline] = (srcLeftRGB[x*4+2]+srcLeftRGB[x*4+2+newline])>>1;
								newLeftRGB[x*4+3+newline] = (srcLeftRGB[x*4+3]+srcLeftRGB[x*4+3+newline])>>1;
							}
						}
						newLeftRGB -= 2*newline;
						srcLeftRGB -= newline;
					}
				}
				else if(internal_format == DECODED_FORMAT_WP13)
				{
					PIXEL *srcLeftRGB = (PIXEL *)local_output;
					PIXEL *srcRighRGB = (PIXEL *)(local_output+channel_offset/2);
					PIXEL *newRighRGB = (PIXEL *)(local_output+channel_offset);
					PIXEL *newLeftRGB = (PIXEL *)local_output;
					int x,y,lwidth = 0;
					int lheight = channel_offset / local_pitch;		

					int newline = local_pitch / sizeof(PIXEL);
					lwidth = local_pitch / 6;			
					for(y=0;y<lheight/2-1;y++)
					{
						for(x=0;x<lwidth;x++)
						{
							newRighRGB[x*3+0] = srcRighRGB[x*3+0];
							newRighRGB[x*3+1] = srcRighRGB[x*3+1];
							newRighRGB[x*3+2] = srcRighRGB[x*3+2];
							newRighRGB[x*3+0+newline] = (srcRighRGB[x*3+0]+srcRighRGB[x*3+0+newline])>>1;
							newRighRGB[x*3+1+newline] = (srcRighRGB[x*3+1]+srcRighRGB[x*3+1+newline])>>1;
							newRighRGB[x*3+2+newline] = (srcRighRGB[x*3+2]+srcRighRGB[x*3+2+newline])>>1;
						}
						
						newRighRGB += 2*newline;
						srcRighRGB += newline;
					}
					for(x=0;x<lwidth;x++)
					{
						newRighRGB[x*3+0] = srcRighRGB[x*3+0];
						newRighRGB[x*3+1] = srcRighRGB[x*3+1];
						newRighRGB[x*3+2] = srcRighRGB[x*3+2];
						newRighRGB[x*3+0+newline] = srcRighRGB[x*3+0];
						newRighRGB[x*3+1+newline] = srcRighRGB[x*3+1];
						newRighRGB[x*3+2+newline] = srcRighRGB[x*3+2];
					}
		
					y=lheight/2-1;							
					newLeftRGB += y*2*newline;
					srcLeftRGB += y*newline;

					for(;y>=0;y--)
					{
						if(y == lheight/2-1)
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*3+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*3+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*3+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*3+0+newline] = srcLeftRGB[x*3+0];
								newLeftRGB[x*3+1+newline] = srcLeftRGB[x*3+1];
								newLeftRGB[x*3+2+newline] = srcLeftRGB[x*3+2];
							}
						}
						else
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*3+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*3+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*3+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*3+0+newline] = (srcLeftRGB[x*3+0]+srcLeftRGB[x*3+0+newline])>>1;
								newLeftRGB[x*3+1+newline] = (srcLeftRGB[x*3+1]+srcLeftRGB[x*3+1+newline])>>1;
								newLeftRGB[x*3+2+newline] = (srcLeftRGB[x*3+2]+srcLeftRGB[x*3+2+newline])>>1;
							}
						}
						newLeftRGB -= 2*newline;
						srcLeftRGB -= newline;
					}
				}
				else if(internal_format == DECODED_FORMAT_RGB32)
				{
					uint8_t *srcRighRGB = local_output; // upside down
					uint8_t  *srcLeftRGB = (local_output+channel_offset/2);
					uint8_t *newRighRGB = (local_output+channel_offset);
					uint8_t *newLeftRGB = local_output;
					int x,y,lwidth = 0;
					int lheight = channel_offset / local_pitch;
					int newline = local_pitch;			
					lwidth = local_pitch / 4;	
					
					if(swapLR)
					{		
						void *temp = srcRighRGB;
						srcRighRGB = srcLeftRGB;
						srcLeftRGB = temp;
					}

					for(y=0;y<lheight/2-1;y++)
					{
						for(x=0;x<lwidth;x++)
						{
							newRighRGB[x*4+0] = srcRighRGB[x*4+0];
							newRighRGB[x*4+1] = srcRighRGB[x*4+1];
							newRighRGB[x*4+2] = srcRighRGB[x*4+2];
							newRighRGB[x*4+3] = srcRighRGB[x*4+3];
							newRighRGB[x*4+0+newline] = (srcRighRGB[x*4+0]+srcRighRGB[x*4+0+newline])>>1;
							newRighRGB[x*4+1+newline] = (srcRighRGB[x*4+1]+srcRighRGB[x*4+1+newline])>>1;
							newRighRGB[x*4+2+newline] = (srcRighRGB[x*4+2]+srcRighRGB[x*4+2+newline])>>1;
							newRighRGB[x*4+3+newline] = (srcRighRGB[x*4+3]+srcRighRGB[x*4+3+newline])>>1;
						}
						
						newRighRGB += 2*newline;
						srcRighRGB += newline;
					}
					for(x=0;x<lwidth;x++)
					{
						newRighRGB[x*4+0] = srcRighRGB[x*4+0];
						newRighRGB[x*4+1] = srcRighRGB[x*4+1];
						newRighRGB[x*4+2] = srcRighRGB[x*4+2];
						newRighRGB[x*4+3] = srcRighRGB[x*4+3];
						newRighRGB[x*4+0+newline] = srcRighRGB[x*4+0];
						newRighRGB[x*4+1+newline] = srcRighRGB[x*4+1];
						newRighRGB[x*4+2+newline] = srcRighRGB[x*4+2];
						newRighRGB[x*4+3+newline] = srcRighRGB[x*4+3];
					}

					if(swapLR)
					{
						y=lheight/2-1;
						newLeftRGB += y*2*newline;
						srcLeftRGB += y*newline;			
						for(x=0;x<lwidth;x++)
						{
							newLeftRGB[x*4+0] = srcLeftRGB[x*4+0];
							newLeftRGB[x*4+1] = srcLeftRGB[x*4+1];
							newLeftRGB[x*4+2] = srcLeftRGB[x*4+2];
							newLeftRGB[x*4+3] = srcLeftRGB[x*4+3];
							newLeftRGB[x*4+0+newline] = srcLeftRGB[x*4+0];
							newLeftRGB[x*4+1+newline] = srcLeftRGB[x*4+1];
							newLeftRGB[x*4+2+newline] = srcLeftRGB[x*4+2];
							newLeftRGB[x*4+3+newline] = srcLeftRGB[x*4+3];
						}
						for(;y>0;y--) // for some reason using y>=0 there is black output???
						{
							newLeftRGB -= 2*newline;
							srcLeftRGB -= newline;
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*4+0] = srcLeftRGB[x*4+0];
								newLeftRGB[x*4+1] = srcLeftRGB[x*4+1];
								newLeftRGB[x*4+2] = srcLeftRGB[x*4+2];
								newLeftRGB[x*4+3] = srcLeftRGB[x*4+3];
								newLeftRGB[x*4+0+newline] = (srcLeftRGB[x*4+0]+srcLeftRGB[x*4+0+newline])>>1;
								newLeftRGB[x*4+1+newline] = (srcLeftRGB[x*4+1]+srcLeftRGB[x*4+1+newline])>>1;
								newLeftRGB[x*4+2+newline] = (srcLeftRGB[x*4+2]+srcLeftRGB[x*4+2+newline])>>1;
								newLeftRGB[x*4+3+newline] = (srcLeftRGB[x*4+3]+srcLeftRGB[x*4+3+newline])>>1;
							}
						}				
					}
					else
					{
						for(y=0;y<lheight/2-1;y++)
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*4+0] = srcLeftRGB[x*4+0];
								newLeftRGB[x*4+1] = srcLeftRGB[x*4+1];
								newLeftRGB[x*4+2] = srcLeftRGB[x*4+2];
								newLeftRGB[x*4+3] = srcLeftRGB[x*4+3];
								newLeftRGB[x*4+0+newline] = (srcLeftRGB[x*4+0]+srcLeftRGB[x*4+0+newline])>>1;
								newLeftRGB[x*4+1+newline] = (srcLeftRGB[x*4+1]+srcLeftRGB[x*4+1+newline])>>1;
								newLeftRGB[x*4+2+newline] = (srcLeftRGB[x*4+2]+srcLeftRGB[x*4+2+newline])>>1;
								newLeftRGB[x*4+3+newline] = (srcLeftRGB[x*4+3]+srcLeftRGB[x*4+3+newline])>>1;
							}

							newLeftRGB += 2*newline;
							srcLeftRGB += newline;
						}							
						for(x=0;x<lwidth;x++)
						{
							newLeftRGB[x*4+0] = srcLeftRGB[x*4+0];
							newLeftRGB[x*4+1] = srcLeftRGB[x*4+1];
							newLeftRGB[x*4+2] = srcLeftRGB[x*4+2];
							newLeftRGB[x*4+3] = srcLeftRGB[x*4+3];
							newLeftRGB[x*4+0+newline] = srcLeftRGB[x*4+0];
							newLeftRGB[x*4+1+newline] = srcLeftRGB[x*4+1];
							newLeftRGB[x*4+2+newline] = srcLeftRGB[x*4+2];
							newLeftRGB[x*4+3+newline] = srcLeftRGB[x*4+3];
						}
					}
				}
				else if(internal_format == DECODED_FORMAT_RGB24)
				{
					uint8_t *srcRighRGB = local_output; // upside down
					uint8_t *srcLeftRGB = (local_output+channel_offset/2);
					uint8_t *newRighRGB = (local_output+channel_offset);
					uint8_t *newLeftRGB = local_output;
					int x,y,lwidth = 0;
					int lheight = channel_offset / local_pitch;
					int newline = local_pitch;
					lwidth = local_pitch / 3;		

					if(swapLR)
					{		
						void *temp = srcRighRGB;
						srcRighRGB = srcLeftRGB;
						srcLeftRGB = temp;
					}

					for(y=0;y<lheight/2-1;y++)
					{
						for(x=0;x<lwidth;x++)
						{
							newRighRGB[x*3+0] = srcRighRGB[x*3+0];
							newRighRGB[x*3+1] = srcRighRGB[x*3+1];
							newRighRGB[x*3+2] = srcRighRGB[x*3+2];
							newRighRGB[x*3+0+newline] = (srcRighRGB[x*3+0]+srcRighRGB[x*3+0+newline])>>1;
							newRighRGB[x*3+1+newline] = (srcRighRGB[x*3+1]+srcRighRGB[x*3+1+newline])>>1;
							newRighRGB[x*3+2+newline] = (srcRighRGB[x*3+2]+srcRighRGB[x*3+2+newline])>>1;
						}
						
						newRighRGB += 2*newline;
						srcRighRGB += newline;
					}							
					for(x=0;x<lwidth;x++)
					{
						newRighRGB[x*3+0] = srcRighRGB[x*3+0];
						newRighRGB[x*3+1] = srcRighRGB[x*3+1];
						newRighRGB[x*3+2] = srcRighRGB[x*3+2];
						newRighRGB[x*3+0+newline] = srcRighRGB[x*3+0];
						newRighRGB[x*3+1+newline] = srcRighRGB[x*3+1];
						newRighRGB[x*3+2+newline] = srcRighRGB[x*3+2];
					}
	

					if(swapLR)
					{
						y=lheight/2-1;
						newLeftRGB += y*2*newline;
						srcLeftRGB += y*newline;			
						for(x=0;x<lwidth;x++)
						{
							newLeftRGB[x*3+0] = srcLeftRGB[x*3+0];
							newLeftRGB[x*3+1] = srcLeftRGB[x*3+1];
							newLeftRGB[x*3+2] = srcLeftRGB[x*3+2];
							newLeftRGB[x*3+0+newline] = srcLeftRGB[x*3+0];
							newLeftRGB[x*3+1+newline] = srcLeftRGB[x*3+1];
							newLeftRGB[x*3+2+newline] = srcLeftRGB[x*3+2];
						}
						for(;y>0;y--) // for some reason using y>=0 there is black output???
						{
							newLeftRGB -= 2*newline;
							srcLeftRGB -= newline;
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*3+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*3+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*3+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*3+0+newline] = (srcLeftRGB[x*3+0]+srcLeftRGB[x*3+0+newline])>>1;
								newLeftRGB[x*3+1+newline] = (srcLeftRGB[x*3+1]+srcLeftRGB[x*3+1+newline])>>1;
								newLeftRGB[x*3+2+newline] = (srcLeftRGB[x*3+2]+srcLeftRGB[x*3+2+newline])>>1;
							}
						}				
					}
					else
					{
						for(y=0;y<lheight/2-1;y++)
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*3+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*3+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*3+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*3+0+newline] = (srcLeftRGB[x*3+0]+srcLeftRGB[x*3+0+newline])>>1;
								newLeftRGB[x*3+1+newline] = (srcLeftRGB[x*3+1]+srcLeftRGB[x*3+1+newline])>>1;
								newLeftRGB[x*3+2+newline] = (srcLeftRGB[x*3+2]+srcLeftRGB[x*3+2+newline])>>1;
							}
							newLeftRGB += 2*newline;
							srcLeftRGB += newline;
						}							
						for(x=0;x<lwidth;x++)
						{
							newLeftRGB[x*3+0] = srcLeftRGB[x*3+0];
							newLeftRGB[x*3+1] = srcLeftRGB[x*3+1];
							newLeftRGB[x*3+2] = srcLeftRGB[x*3+2];
							newLeftRGB[x*3+0+newline] = srcLeftRGB[x*3+0];
							newLeftRGB[x*3+1+newline] = srcLeftRGB[x*3+1];
							newLeftRGB[x*3+2+newline] = srcLeftRGB[x*3+2];
						}
					}
				}
				else
				{
					assert(0); // no other pixel format supported
				}						
			}
		}
		else if(use_local_buffer && decoder->preformatted_3D_type == BLEND_SIDEBYSIDE_ANAMORPHIC)
		{
			if(decoder->channel_blend_type == BLEND_SIDEBYSIDE_ANAMORPHIC &&
				decoder->frame.resolution == DECODED_RESOLUTION_FULL && !swapLR) // special case as it is a native decode mode
			{						
				decoder->frame.resolution = DECODED_RESOLUTION_HALF_HORIZONTAL;
				decoder->frame.width /= 2;
				channel_offset = local_pitch/2;
			}
			else
			{
				if(internal_format == DECODED_FORMAT_W13A)
				{
					PIXEL *srcLeftRGB = (PIXEL *)local_output;
					PIXEL *srcRighRGB = (PIXEL *)local_output;
					PIXEL *newRighRGB = (PIXEL *)(local_output+channel_offset);
					PIXEL *newLeftRGB = (PIXEL *)local_output;
					int x,y,lwidth = 0;
					int lheight = channel_offset / local_pitch;

					lwidth = local_pitch / 8;						
					srcRighRGB += (lwidth / 2) * 4;

					for(y=0;y<lheight;y++)
					{
						for(x=0;x<lwidth/2-1;x++)
						{
							newRighRGB[x*8+0] = srcRighRGB[x*4+0];
							newRighRGB[x*8+1] = srcRighRGB[x*4+1];
							newRighRGB[x*8+2] = srcRighRGB[x*4+2];
							newRighRGB[x*8+3] = srcRighRGB[x*4+3];
							newRighRGB[x*8+4] = (srcRighRGB[x*4+0]+srcRighRGB[x*4+4])>>1;
							newRighRGB[x*8+5] = (srcRighRGB[x*4+1]+srcRighRGB[x*4+5])>>1;
							newRighRGB[x*8+6] = (srcRighRGB[x*4+2]+srcRighRGB[x*4+6])>>1;
							newRighRGB[x*8+7] = (srcRighRGB[x*4+3]+srcRighRGB[x*4+7])>>1;
						}
						newRighRGB[x*8+0] = srcRighRGB[x*4+0];
						newRighRGB[x*8+1] = srcRighRGB[x*4+1];
						newRighRGB[x*8+2] = srcRighRGB[x*4+2];
						newRighRGB[x*8+3] = srcRighRGB[x*4+3];
						newRighRGB[x*8+4] = srcRighRGB[x*4+0];
						newRighRGB[x*8+5] = srcRighRGB[x*4+1];
						newRighRGB[x*8+6] = srcRighRGB[x*4+2];
						newRighRGB[x*8+7] = srcRighRGB[x*4+3];
						
						if(swapLR)
						{
							for(x=0;x<lwidth/2-1;x++)
							{
								newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*6+3] = (srcLeftRGB[x*3+0]+srcLeftRGB[x*3+3])>>1;
								newLeftRGB[x*6+4] = (srcLeftRGB[x*3+1]+srcLeftRGB[x*3+4])>>1;
								newLeftRGB[x*6+5] = (srcLeftRGB[x*3+2]+srcLeftRGB[x*3+5])>>1;
							}			
							newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
							newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
							newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
							newLeftRGB[x*6+3] = srcLeftRGB[x*3+0];
							newLeftRGB[x*6+4] = srcLeftRGB[x*3+1];
							newLeftRGB[x*6+5] = srcLeftRGB[x*3+2];
						}
						else
						{
							x=lwidth/2-1;
							newLeftRGB[x*8+0] = srcLeftRGB[x*4+0];
							newLeftRGB[x*8+1] = srcLeftRGB[x*4+1];
							newLeftRGB[x*8+2] = srcLeftRGB[x*4+2];
							newLeftRGB[x*8+3] = srcLeftRGB[x*4+3];
							newLeftRGB[x*8+4] = srcLeftRGB[x*4+0];
							newLeftRGB[x*8+5] = srcLeftRGB[x*4+1];
							newLeftRGB[x*8+6] = srcLeftRGB[x*4+2];
							newLeftRGB[x*8+7] = srcLeftRGB[x*4+3];
							for(;x>=0;x--)
							{
								newLeftRGB[x*8+0] = srcLeftRGB[x*4+0];
								newLeftRGB[x*8+1] = srcLeftRGB[x*4+1];
								newLeftRGB[x*8+2] = srcLeftRGB[x*4+2];
								newLeftRGB[x*8+3] = srcLeftRGB[x*4+3];
								newLeftRGB[x*8+4] = (srcLeftRGB[x*4+0]+srcLeftRGB[x*4+4])>>1;
								newLeftRGB[x*8+5] = (srcLeftRGB[x*4+1]+srcLeftRGB[x*4+5])>>1;
								newLeftRGB[x*8+6] = (srcLeftRGB[x*4+2]+srcLeftRGB[x*4+6])>>1;
								newLeftRGB[x*8+7] = (srcLeftRGB[x*4+3]+srcLeftRGB[x*4+7])>>1;
							}
						}

						newLeftRGB += local_pitch/sizeof(PIXEL);
						newRighRGB += local_pitch/sizeof(PIXEL);
						srcLeftRGB += local_pitch/sizeof(PIXEL);
						srcRighRGB += local_pitch/sizeof(PIXEL);
					}
				}
				else if(internal_format == DECODED_FORMAT_WP13)
				{
					PIXEL *srcLeftRGB = (PIXEL *)local_output;
					PIXEL *srcRighRGB = (PIXEL *)local_output;
					PIXEL *newRighRGB = (PIXEL *)(local_output+channel_offset);
					PIXEL *newLeftRGB = (PIXEL *)local_output;
					int x,y,lwidth = 0;
					int lheight = channel_offset / local_pitch;

					lwidth = local_pitch / 6;						
					srcRighRGB += (lwidth / 2) * 3;

					for(y=0;y<lheight;y++)
					{
						for(x=0;x<lwidth/2-1;x++)
						{
							newRighRGB[x*6+0] = srcRighRGB[x*3+0];
							newRighRGB[x*6+1] = srcRighRGB[x*3+1];
							newRighRGB[x*6+2] = srcRighRGB[x*3+2];
							newRighRGB[x*6+3] = (srcRighRGB[x*3+0]+srcRighRGB[x*3+3])>>1;
							newRighRGB[x*6+4] = (srcRighRGB[x*3+1]+srcRighRGB[x*3+4])>>1;
							newRighRGB[x*6+5] = (srcRighRGB[x*3+2]+srcRighRGB[x*3+5])>>1;
						}
						newRighRGB[x*6+0] = srcRighRGB[x*3+0];
						newRighRGB[x*6+1] = srcRighRGB[x*3+1];
						newRighRGB[x*6+2] = srcRighRGB[x*3+2];
						newRighRGB[x*6+3] = srcRighRGB[x*3+0];
						newRighRGB[x*6+4] = srcRighRGB[x*3+1];
						newRighRGB[x*6+5] = srcRighRGB[x*3+2];
						
						if(swapLR)
						{
							for(x=0;x<lwidth/2-1;x++)
							{
								newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*6+3] = (srcLeftRGB[x*3+0]+srcLeftRGB[x*3+3])>>1;
								newLeftRGB[x*6+4] = (srcLeftRGB[x*3+1]+srcLeftRGB[x*3+4])>>1;
								newLeftRGB[x*6+5] = (srcLeftRGB[x*3+2]+srcLeftRGB[x*3+5])>>1;
							}			
							newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
							newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
							newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
							newLeftRGB[x*6+3] = srcLeftRGB[x*3+0];
							newLeftRGB[x*6+4] = srcLeftRGB[x*3+1];
							newLeftRGB[x*6+5] = srcLeftRGB[x*3+2];
						}
						else
						{
							x=lwidth/2-1;
							newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
							newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
							newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
							newLeftRGB[x*6+3] = srcLeftRGB[x*3+0];
							newLeftRGB[x*6+4] = srcLeftRGB[x*3+1];
							newLeftRGB[x*6+5] = srcLeftRGB[x*3+2];
							for(;x>=0;x--)
							{
								newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*6+3] = (srcLeftRGB[x*3+0]+srcLeftRGB[x*3+3])>>1;
								newLeftRGB[x*6+4] = (srcLeftRGB[x*3+1]+srcLeftRGB[x*3+4])>>1;
								newLeftRGB[x*6+5] = (srcLeftRGB[x*3+2]+srcLeftRGB[x*3+5])>>1;
							}				
						}

						newLeftRGB += local_pitch/sizeof(PIXEL);
						newRighRGB += local_pitch/sizeof(PIXEL);
						srcLeftRGB += local_pitch/sizeof(PIXEL);
						srcRighRGB += local_pitch/sizeof(PIXEL);
					}
				}
				else if(internal_format == DECODED_FORMAT_RGB32)
				{
					uint8_t *srcLeftRGB = local_output;
					uint8_t *srcRighRGB = local_output;
					uint8_t *newRighRGB = (local_output+channel_offset);
					uint8_t *newLeftRGB = local_output;
					int x,y,lwidth = 0;
					int lheight = channel_offset / local_pitch;

					lwidth = local_pitch / 4;						
					srcRighRGB += (lwidth / 2) * 4;

					for(y=0;y<lheight;y++)
					{
						for(x=0;x<lwidth/2-1;x++)
						{
							newRighRGB[x*8+0] = srcRighRGB[x*4+0];
							newRighRGB[x*8+1] = srcRighRGB[x*4+1];
							newRighRGB[x*8+2] = srcRighRGB[x*4+2];
							newRighRGB[x*8+3] = srcRighRGB[x*4+3];
							newRighRGB[x*8+4] = ((int)srcRighRGB[x*4+0]+(int)srcRighRGB[x*4+4])>>1;
							newRighRGB[x*8+5] = ((int)srcRighRGB[x*4+1]+(int)srcRighRGB[x*4+5])>>1;
							newRighRGB[x*8+6] = ((int)srcRighRGB[x*4+2]+(int)srcRighRGB[x*4+6])>>1;
							newRighRGB[x*8+7] = ((int)srcRighRGB[x*4+3]+(int)srcRighRGB[x*4+7])>>1;
						}
						newRighRGB[x*8+0] = srcRighRGB[x*4+0];
						newRighRGB[x*8+1] = srcRighRGB[x*4+1];
						newRighRGB[x*8+2] = srcRighRGB[x*4+2];
						newRighRGB[x*8+3] = srcRighRGB[x*4+3];
						newRighRGB[x*8+4] = srcRighRGB[x*4+0];
						newRighRGB[x*8+5] = srcRighRGB[x*4+1];
						newRighRGB[x*8+6] = srcRighRGB[x*4+2];
						newRighRGB[x*8+7] = srcRighRGB[x*4+3];

						if(swapLR)
						{
							for(x=0;x<lwidth/2-1;x++)
							{
								newLeftRGB[x*8+0] = srcLeftRGB[x*4+0];
								newLeftRGB[x*8+1] = srcLeftRGB[x*4+1];
								newLeftRGB[x*8+2] = srcLeftRGB[x*4+2];
								newLeftRGB[x*8+3] = srcLeftRGB[x*4+3];
								newLeftRGB[x*8+4] = ((int)srcLeftRGB[x*4+0]+(int)srcLeftRGB[x*4+4])>>1;
								newLeftRGB[x*8+5] = ((int)srcLeftRGB[x*4+1]+(int)srcLeftRGB[x*4+5])>>1;
								newLeftRGB[x*8+6] = ((int)srcLeftRGB[x*4+2]+(int)srcLeftRGB[x*4+6])>>1;
								newLeftRGB[x*8+7] = ((int)srcLeftRGB[x*4+3]+(int)srcLeftRGB[x*4+7])>>1;
							}			
							newLeftRGB[x*8+0] = srcLeftRGB[x*4+0];
							newLeftRGB[x*8+1] = srcLeftRGB[x*4+1];
							newLeftRGB[x*8+2] = srcLeftRGB[x*4+2];
							newLeftRGB[x*8+3] = srcLeftRGB[x*4+3];
							newLeftRGB[x*8+4] = srcLeftRGB[x*4+0];
							newLeftRGB[x*8+5] = srcLeftRGB[x*4+1];
							newLeftRGB[x*8+6] = srcLeftRGB[x*4+2];
							newLeftRGB[x*8+7] = srcLeftRGB[x*4+3];
						}
						else
						{
							x=lwidth/2-1;
							newLeftRGB[x*8+0] = srcLeftRGB[x*4+0];
							newLeftRGB[x*8+1] = srcLeftRGB[x*4+1];
							newLeftRGB[x*8+2] = srcLeftRGB[x*4+2];
							newLeftRGB[x*8+3] = srcLeftRGB[x*4+3];
							newLeftRGB[x*8+4] = srcLeftRGB[x*4+0];
							newLeftRGB[x*8+5] = srcLeftRGB[x*4+1];
							newLeftRGB[x*8+6] = srcLeftRGB[x*4+2];
							newLeftRGB[x*8+7] = srcLeftRGB[x*4+3];
							for(;x>=0;x--)
							{
								newLeftRGB[x*8+0] = srcLeftRGB[x*4+0];
								newLeftRGB[x*8+1] = srcLeftRGB[x*4+1];
								newLeftRGB[x*8+2] = srcLeftRGB[x*4+2];
								newLeftRGB[x*8+3] = srcLeftRGB[x*4+3];
								newLeftRGB[x*8+4] = ((int)srcLeftRGB[x*4+0]+(int)srcLeftRGB[x*4+4])>>1;
								newLeftRGB[x*8+5] = ((int)srcLeftRGB[x*4+1]+(int)srcLeftRGB[x*4+5])>>1;
								newLeftRGB[x*8+6] = ((int)srcLeftRGB[x*4+2]+(int)srcLeftRGB[x*4+6])>>1;
								newLeftRGB[x*8+7] = ((int)srcLeftRGB[x*4+3]+(int)srcLeftRGB[x*4+7])>>1;
							}
						}

						newLeftRGB += local_pitch;
						newRighRGB += local_pitch;
						srcLeftRGB += local_pitch;
						srcRighRGB += local_pitch;
					}
				}
				else if(internal_format == DECODED_FORMAT_RGB24)
				{
					uint8_t *srcLeftRGB = local_output;
					uint8_t *srcRighRGB = local_output;
					uint8_t *newRighRGB = (local_output+channel_offset);
					uint8_t *newLeftRGB = local_output;
					int x,y,lwidth = 0;
					int lheight = channel_offset / local_pitch;

					lwidth = local_pitch / 3;						
					srcRighRGB += (lwidth / 2) * 3;

					
					if(swapLR)
					{		
						void *temp = srcRighRGB;
						srcRighRGB = srcLeftRGB;
						srcLeftRGB = temp;
					}

					for(y=0;y<lheight;y++)
					{
						for(x=0;x<lwidth/2-1;x++)
						{
							newRighRGB[x*6+0] = srcRighRGB[x*3+0];
							newRighRGB[x*6+1] = srcRighRGB[x*3+1];
							newRighRGB[x*6+2] = srcRighRGB[x*3+2];
							newRighRGB[x*6+3] = ((int)srcRighRGB[x*3+0]+(int)srcRighRGB[x*3+3])>>1;
							newRighRGB[x*6+4] = ((int)srcRighRGB[x*3+1]+(int)srcRighRGB[x*3+4])>>1;
							newRighRGB[x*6+5] = ((int)srcRighRGB[x*3+2]+(int)srcRighRGB[x*3+5])>>1;
						}
						newRighRGB[x*6+0] = srcRighRGB[x*3+0];
						newRighRGB[x*6+1] = srcRighRGB[x*3+1];
						newRighRGB[x*6+2] = srcRighRGB[x*3+2];
						newRighRGB[x*6+3] = srcRighRGB[x*3+0];
						newRighRGB[x*6+4] = srcRighRGB[x*3+1];
						newRighRGB[x*6+5] = srcRighRGB[x*3+2];
						
						if(swapLR)
						{
							for(x=0;x<lwidth/2-1;x++)
							{
								newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*6+3] = ((int)srcLeftRGB[x*3+0]+(int)srcLeftRGB[x*3+3])>>1;
								newLeftRGB[x*6+4] = ((int)srcLeftRGB[x*3+1]+(int)srcLeftRGB[x*3+4])>>1;
								newLeftRGB[x*6+5] = ((int)srcLeftRGB[x*3+2]+(int)srcLeftRGB[x*3+5])>>1;
							}			
							newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
							newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
							newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
							newLeftRGB[x*6+3] = srcLeftRGB[x*3+0];
							newLeftRGB[x*6+4] = srcLeftRGB[x*3+1];
							newLeftRGB[x*6+5] = srcLeftRGB[x*3+2];
						}
						else
						{
							x=lwidth/2-1;
							newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
							newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
							newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
							newLeftRGB[x*6+3] = srcLeftRGB[x*3+0];
							newLeftRGB[x*6+4] = srcLeftRGB[x*3+1];
							newLeftRGB[x*6+5] = srcLeftRGB[x*3+2];
							for(;x>=0;x--)
							{
								newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*6+3] = ((int)srcLeftRGB[x*3+0]+(int)srcLeftRGB[x*3+3])>>1;
								newLeftRGB[x*6+4] = ((int)srcLeftRGB[x*3+1]+(int)srcLeftRGB[x*3+4])>>1;
								newLeftRGB[x*6+5] = ((int)srcLeftRGB[x*3+2]+(int)srcLeftRGB[x*3+5])>>1;
							}			
						}

						newLeftRGB += local_pitch;
						newRighRGB += local_pitch;
						srcLeftRGB += local_pitch;
						srcRighRGB += local_pitch;
					}
				}
				else
				{
					assert(0); // no other pixel format supported
				}
			}
		}
		else
		{
			memcpy((local_output + channel_offset), local_output, channel_offset);
			//channel_offset = 0; // Equilavent to copying the eye to the second buffer, 
								// just make the second buffer point to the say eye. 
		}
	}
	else if(use_local_buffer && decoder->channel_decodes == 1 && decoder->preformatted_3D_type != BLEND_NONE)
	{
		//fake a second channel for 3D engine

		if(decoder->preformatted_3D_type == BLEND_STACKED_ANAMORPHIC)
		{
			PIXEL *srcLeftRGB = (PIXEL *)local_output;
			PIXEL *srcRighRGB = (PIXEL *)(local_output+channel_offset/2);
			PIXEL *newRighRGB = (PIXEL *)(local_output+channel_offset);
			PIXEL *newLeftRGB = (PIXEL *)local_output;
			int x,y,lwidth = 0;
			int lheight = channel_offset / local_pitch;


			if((channel_mask & 3) == 3) // extract both channels.
			{
				
				if(swapLR)
				{		
					PIXEL *temp = srcRighRGB;
					srcRighRGB = srcLeftRGB;
					srcLeftRGB = temp;
				}

				if(internal_format == DECODED_FORMAT_W13A)
				{
					int newline = local_pitch / sizeof(PIXEL);
					lwidth = local_pitch / 8;			
					for(y=0;y<lheight/2-1;y++)
					{
						for(x=0;x<lwidth;x++)
						{
							newRighRGB[x*4+0] = srcRighRGB[x*4+0];
							newRighRGB[x*4+1] = srcRighRGB[x*4+1];
							newRighRGB[x*4+2] = srcRighRGB[x*4+2];
							newRighRGB[x*4+3] = srcRighRGB[x*4+3];
							newRighRGB[x*4+0+newline] = (srcRighRGB[x*4+0]+srcRighRGB[x*4+0+newline])>>1;
							newRighRGB[x*4+1+newline] = (srcRighRGB[x*4+1]+srcRighRGB[x*4+1+newline])>>1;
							newRighRGB[x*4+2+newline] = (srcRighRGB[x*4+2]+srcRighRGB[x*4+2+newline])>>1;
							newRighRGB[x*4+3+newline] = (srcRighRGB[x*4+3]+srcRighRGB[x*4+3+newline])>>1;
						}
						
						newRighRGB += 2*newline;
						srcRighRGB += newline;
					}
					newRighRGB[x*4+0] = srcRighRGB[x*4+0];
					newRighRGB[x*4+1] = srcRighRGB[x*4+1];
					newRighRGB[x*4+2] = srcRighRGB[x*4+2];
					newRighRGB[x*4+3] = srcRighRGB[x*4+3];
					newRighRGB[x*4+0+newline] = srcRighRGB[x*4+0];
					newRighRGB[x*4+1+newline] = srcRighRGB[x*4+1];
					newRighRGB[x*4+2+newline] = srcRighRGB[x*4+2];
					newRighRGB[x*4+3+newline] = srcRighRGB[x*4+3];

	
					y=lheight/2-1;
					newLeftRGB += y*2*newline;
					srcLeftRGB += y*newline;

					for(;y>=0;y--)
					{
						if(y == lheight/2-1)
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*4+0] = srcLeftRGB[x*4+0];
								newLeftRGB[x*4+1] = srcLeftRGB[x*4+1];
								newLeftRGB[x*4+2] = srcLeftRGB[x*4+2];
								newLeftRGB[x*4+3] = srcLeftRGB[x*4+3];
								newLeftRGB[x*4+0+newline] = srcLeftRGB[x*4+0];
								newLeftRGB[x*4+1+newline] = srcLeftRGB[x*4+1];
								newLeftRGB[x*4+2+newline] = srcLeftRGB[x*4+2];
								newLeftRGB[x*4+3+newline] = srcLeftRGB[x*4+3];
							}
						}
						else
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*4+0] = srcLeftRGB[x*4+0];
								newLeftRGB[x*4+1] = srcLeftRGB[x*4+1];
								newLeftRGB[x*4+2] = srcLeftRGB[x*4+2];
								newLeftRGB[x*4+3] = srcLeftRGB[x*4+3];
								newLeftRGB[x*4+0+newline] = (srcLeftRGB[x*4+0]+srcLeftRGB[x*4+0+newline])>>1;
								newLeftRGB[x*4+1+newline] = (srcLeftRGB[x*4+1]+srcLeftRGB[x*4+1+newline])>>1;
								newLeftRGB[x*4+2+newline] = (srcLeftRGB[x*4+2]+srcLeftRGB[x*4+2+newline])>>1;
								newLeftRGB[x*4+3+newline] = (srcLeftRGB[x*4+3]+srcLeftRGB[x*4+3+newline])>>1;
							}
						}
						newLeftRGB -= 2*newline;
						srcLeftRGB -= newline;
					}
				}
				else if(internal_format == DECODED_FORMAT_WP13)
				{
					int newline = local_pitch / sizeof(PIXEL);
					lwidth = local_pitch / 6;			
					for(y=0;y<lheight/2-1;y++)
					{
						for(x=0;x<lwidth;x++)
						{
							newRighRGB[x*3+0] = srcRighRGB[x*3+0];
							newRighRGB[x*3+1] = srcRighRGB[x*3+1];
							newRighRGB[x*3+2] = srcRighRGB[x*3+2];
							newRighRGB[x*3+0+newline] = (srcRighRGB[x*3+0]+srcRighRGB[x*3+0+newline])>>1;
							newRighRGB[x*3+1+newline] = (srcRighRGB[x*3+1]+srcRighRGB[x*3+1+newline])>>1;
							newRighRGB[x*3+2+newline] = (srcRighRGB[x*3+2]+srcRighRGB[x*3+2+newline])>>1;
						}
						
						newRighRGB += 2*newline;
						srcRighRGB += newline;
					}
					newRighRGB[x*3+0] = srcRighRGB[x*3+0];
					newRighRGB[x*3+1] = srcRighRGB[x*3+1];
					newRighRGB[x*3+2] = srcRighRGB[x*3+2];
					newRighRGB[x*3+0+newline] = srcRighRGB[x*3+0];
					newRighRGB[x*3+1+newline] = srcRighRGB[x*3+1];
					newRighRGB[x*3+2+newline] = srcRighRGB[x*3+2];
	
					y=lheight/2-1;							
					newLeftRGB += y*2*newline;
					srcLeftRGB += y*newline;

					for(;y>=0;y--)
					{
						if(y == lheight/2-1)
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*3+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*3+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*3+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*3+0+newline] = srcLeftRGB[x*3+0];
								newLeftRGB[x*3+1+newline] = srcLeftRGB[x*3+1];
								newLeftRGB[x*3+2+newline] = srcLeftRGB[x*3+2];
							}
						}
						else
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*3+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*3+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*3+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*3+0+newline] = (srcLeftRGB[x*3+0]+srcLeftRGB[x*3+0+newline])>>1;
								newLeftRGB[x*3+1+newline] = (srcLeftRGB[x*3+1]+srcLeftRGB[x*3+1+newline])>>1;
								newLeftRGB[x*3+2+newline] = (srcLeftRGB[x*3+2]+srcLeftRGB[x*3+2+newline])>>1;
							}
						}
						newLeftRGB -= 2*newline;
						srcLeftRGB -= newline;
					}
				}
				else
				{
					assert(0); // no other pixel format supported
				}
				decoder->source_channels = 2;
			}
			else if(leftonly) // extract Left channel only.
			{
				if(internal_format == DECODED_FORMAT_W13A)
				{
					int newline = local_pitch / sizeof(PIXEL);
					lwidth = local_pitch / 8;						
					
					y=lheight/2-1;
					newLeftRGB += y*2*(local_pitch/sizeof(PIXEL));
					srcLeftRGB += y*(local_pitch/sizeof(PIXEL));

					for(;y>=0;y--)
					{
						if(y == lheight/2-1)
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*4+0] = srcLeftRGB[x*4+0];
								newLeftRGB[x*4+1] = srcLeftRGB[x*4+1];
								newLeftRGB[x*4+2] = srcLeftRGB[x*4+2];
								newLeftRGB[x*4+3] = srcLeftRGB[x*4+3];
								newLeftRGB[x*4+0+newline] = srcLeftRGB[x*4+0];
								newLeftRGB[x*4+1+newline] = srcLeftRGB[x*4+1];
								newLeftRGB[x*4+2+newline] = srcLeftRGB[x*4+2];
								newLeftRGB[x*4+3+newline] = srcLeftRGB[x*4+3];
							}
						}
						else
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*4+0] = srcLeftRGB[x*4+0];
								newLeftRGB[x*4+1] = srcLeftRGB[x*4+1];
								newLeftRGB[x*4+2] = srcLeftRGB[x*4+2];
								newLeftRGB[x*4+3] = srcLeftRGB[x*4+3];
								newLeftRGB[x*4+0+newline] = (srcLeftRGB[x*4+0]+srcLeftRGB[x*4+0+newline])>>1;
								newLeftRGB[x*4+1+newline] = (srcLeftRGB[x*4+1]+srcLeftRGB[x*4+1+newline])>>1;
								newLeftRGB[x*4+2+newline] = (srcLeftRGB[x*4+2]+srcLeftRGB[x*4+2+newline])>>1;
								newLeftRGB[x*4+3+newline] = (srcLeftRGB[x*4+3]+srcLeftRGB[x*4+3+newline])>>1;
							}
						}
						newLeftRGB -= 2*newline;
						srcLeftRGB -= newline;
					}
				}
				else if(internal_format == DECODED_FORMAT_WP13)
				{
					int newline = local_pitch / sizeof(PIXEL);
					lwidth = local_pitch / 6;						
					
					y=lheight/2-1;							
					newLeftRGB += y*2*(local_pitch/sizeof(PIXEL));
					srcLeftRGB += y*(local_pitch/sizeof(PIXEL));

					for(;y>=0;y--)
					{
						if(y == lheight/2-1)
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*3+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*3+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*3+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*3+0+newline] = srcLeftRGB[x*3+0];
								newLeftRGB[x*3+1+newline] = srcLeftRGB[x*3+1];
								newLeftRGB[x*3+2+newline] = srcLeftRGB[x*3+2];
							}
						}
						else
						{
							for(x=0;x<lwidth;x++)
							{
								newLeftRGB[x*3+0] = srcLeftRGB[x*3+0];
								newLeftRGB[x*3+1] = srcLeftRGB[x*3+1];
								newLeftRGB[x*3+2] = srcLeftRGB[x*3+2];
								newLeftRGB[x*3+0+newline] = (srcLeftRGB[x*3+0]+srcLeftRGB[x*3+0+newline])>>1;
								newLeftRGB[x*3+1+newline] = (srcLeftRGB[x*3+1]+srcLeftRGB[x*3+1+newline])>>1;
								newLeftRGB[x*3+2+newline] = (srcLeftRGB[x*3+2]+srcLeftRGB[x*3+2+newline])>>1;
							}
						}
						newLeftRGB -= 2*newline;
						srcLeftRGB -= newline;
					}
				}
				else
				{
					assert(0); // no other pixel format supported
				}
			}
			else if(righonly) // extract Right channel only.
			{
				if(internal_format == DECODED_FORMAT_W13A)
				{
					int newline = local_pitch / sizeof(PIXEL);
					lwidth = local_pitch / 8;			
					for(y=0;y<lheight/2-1;y++)
					{
						for(x=0;x<lwidth;x++)
						{
							newLeftRGB[x*4+0] = srcRighRGB[x*4+0];
							newLeftRGB[x*4+1] = srcRighRGB[x*4+1];
							newLeftRGB[x*4+2] = srcRighRGB[x*4+2];
							newLeftRGB[x*4+3] = srcRighRGB[x*4+3];
							newLeftRGB[x*4+0+newline] = (srcRighRGB[x*4+0]+srcRighRGB[x*4+0+newline])>>1;
							newLeftRGB[x*4+1+newline] = (srcRighRGB[x*4+1]+srcRighRGB[x*4+1+newline])>>1;
							newLeftRGB[x*4+2+newline] = (srcRighRGB[x*4+2]+srcRighRGB[x*4+2+newline])>>1;
							newLeftRGB[x*4+3+newline] = (srcRighRGB[x*4+3]+srcRighRGB[x*4+3+newline])>>1;
						}
						
						newLeftRGB += 2*newline;
						srcRighRGB += newline;
					}
					for(x=0;x<lwidth;x++)
					{
						newLeftRGB[x*4+0] = srcRighRGB[x*4+0];
						newLeftRGB[x*4+1] = srcRighRGB[x*4+1];
						newLeftRGB[x*4+2] = srcRighRGB[x*4+2];
						newLeftRGB[x*4+3] = srcRighRGB[x*4+3];
						newLeftRGB[x*4+0+newline] = srcRighRGB[x*4+0];
						newLeftRGB[x*4+1+newline] = srcRighRGB[x*4+1];
						newLeftRGB[x*4+2+newline] = srcRighRGB[x*4+2];
						newLeftRGB[x*4+3+newline] = srcRighRGB[x*4+3];
					}
				}
				else if(internal_format == DECODED_FORMAT_WP13)
				{
					int newline = local_pitch / sizeof(PIXEL);
					lwidth = local_pitch / 6;			
					for(y=0;y<lheight/2-1;y++)
					{
						for(x=0;x<lwidth;x++)
						{
							newLeftRGB[x*3+0] = srcRighRGB[x*3+0];
							newLeftRGB[x*3+1] = srcRighRGB[x*3+1];
							newLeftRGB[x*3+2] = srcRighRGB[x*3+2];
							newLeftRGB[x*3+0+newline] = (srcRighRGB[x*3+0]+srcRighRGB[x*3+0+newline])>>1;
							newLeftRGB[x*3+1+newline] = (srcRighRGB[x*3+1]+srcRighRGB[x*3+1+newline])>>1;
							newLeftRGB[x*3+2+newline] = (srcRighRGB[x*3+2]+srcRighRGB[x*3+2+newline])>>1;
						}
						
						newLeftRGB += 2*newline;
						srcRighRGB += newline;
					}
					for(x=0;x<lwidth;x++)
					{
						newLeftRGB[x*3+0] = srcRighRGB[x*3+0];
						newLeftRGB[x*3+1] = srcRighRGB[x*3+1];
						newLeftRGB[x*3+2] = srcRighRGB[x*3+2];
						newLeftRGB[x*3+0+newline] = srcRighRGB[x*3+0];
						newLeftRGB[x*3+1+newline] = srcRighRGB[x*3+1];
						newLeftRGB[x*3+2+newline] = srcRighRGB[x*3+2];
					}
				}
				else
				{
					assert(0); // no other pixel format supported
				}
			} 
		}
		else if(decoder->preformatted_3D_type == BLEND_SIDEBYSIDE_ANAMORPHIC)
		{
			PIXEL *srcLeftRGB = (PIXEL *)local_output;
			PIXEL *srcRighRGB = (PIXEL *)local_output;
			PIXEL *newRighRGB = (PIXEL *)(local_output+channel_offset);
			PIXEL *newLeftRGB = (PIXEL *)local_output;
			int x,y,lwidth = 0;
			int lheight = channel_offset / local_pitch;

			if((channel_mask & 3) == 3) // extract both channels.
			{
				if(internal_format == DECODED_FORMAT_W13A)
				{
					lwidth = local_pitch / 8;						
					srcRighRGB += (lwidth / 2) * 4;

					if(swapLR)
					{		
						PIXEL *temp = srcRighRGB;
						srcRighRGB = srcLeftRGB;
						srcLeftRGB = temp;
					}

					for(y=0;y<lheight;y++)
					{
						for(x=0;x<lwidth/2-1;x++)
						{
							newRighRGB[x*8+0] = srcRighRGB[x*4+0];
							newRighRGB[x*8+1] = srcRighRGB[x*4+1];
							newRighRGB[x*8+2] = srcRighRGB[x*4+2];
							newRighRGB[x*8+3] = srcRighRGB[x*4+3];
							newRighRGB[x*8+4] = (srcRighRGB[x*4+0]+srcRighRGB[x*4+4])>>1;
							newRighRGB[x*8+5] = (srcRighRGB[x*4+1]+srcRighRGB[x*4+5])>>1;
							newRighRGB[x*8+6] = (srcRighRGB[x*4+2]+srcRighRGB[x*4+6])>>1;
							newRighRGB[x*8+7] = (srcRighRGB[x*4+3]+srcRighRGB[x*4+7])>>1;
						}
						newRighRGB[x*8+0] = srcRighRGB[x*4+0];
						newRighRGB[x*8+1] = srcRighRGB[x*4+1];
						newRighRGB[x*8+2] = srcRighRGB[x*4+2];
						newRighRGB[x*8+3] = srcRighRGB[x*4+3];
						newRighRGB[x*8+4] = srcRighRGB[x*4+0];
						newRighRGB[x*8+5] = srcRighRGB[x*4+1];
						newRighRGB[x*8+6] = srcRighRGB[x*4+2];
						newRighRGB[x*8+7] = srcRighRGB[x*4+3];

						x=lwidth/2-1;
						newLeftRGB[x*8+0] = srcLeftRGB[x*4+0];
						newLeftRGB[x*8+1] = srcLeftRGB[x*4+1];
						newLeftRGB[x*8+2] = srcLeftRGB[x*4+2];
						newLeftRGB[x*8+3] = srcLeftRGB[x*4+3];
						newLeftRGB[x*8+4] = srcLeftRGB[x*4+0];
						newLeftRGB[x*8+5] = srcLeftRGB[x*4+1];
						newLeftRGB[x*8+6] = srcLeftRGB[x*4+2];
						newLeftRGB[x*8+7] = srcLeftRGB[x*4+3];
						for(;x>=0;x--)
						{
							newLeftRGB[x*8+0] = srcLeftRGB[x*4+0];
							newLeftRGB[x*8+1] = srcLeftRGB[x*4+1];
							newLeftRGB[x*8+2] = srcLeftRGB[x*4+2];
							newLeftRGB[x*8+3] = srcLeftRGB[x*4+3];
							newLeftRGB[x*8+4] = (srcLeftRGB[x*4+0]+srcLeftRGB[x*4+4])>>1;
							newLeftRGB[x*8+5] = (srcLeftRGB[x*4+1]+srcLeftRGB[x*4+5])>>1;
							newLeftRGB[x*8+6] = (srcLeftRGB[x*4+2]+srcLeftRGB[x*4+6])>>1;
							newLeftRGB[x*8+7] = (srcLeftRGB[x*4+3]+srcLeftRGB[x*4+7])>>1;
						}

						newLeftRGB += local_pitch/sizeof(PIXEL);
						newRighRGB += local_pitch/sizeof(PIXEL);
						srcLeftRGB += local_pitch/sizeof(PIXEL);
						srcRighRGB += local_pitch/sizeof(PIXEL);
					}
				}
				else if(internal_format == DECODED_FORMAT_WP13)
				{
					lwidth = local_pitch / 6;						
					srcRighRGB += (lwidth / 2) * 3;
					
					if(swapLR)
					{		
						PIXEL *temp = srcRighRGB;
						srcRighRGB = srcLeftRGB;
						srcLeftRGB = temp;
					}

					for(y=0;y<lheight;y++)
					{
						for(x=0;x<lwidth/2-1;x++)
						{
							newRighRGB[x*6+0] = srcRighRGB[x*3+0];
							newRighRGB[x*6+1] = srcRighRGB[x*3+1];
							newRighRGB[x*6+2] = srcRighRGB[x*3+2];
							newRighRGB[x*6+3] = (srcRighRGB[x*3+0]+srcRighRGB[x*3+3])>>1;
							newRighRGB[x*6+4] = (srcRighRGB[x*3+1]+srcRighRGB[x*3+4])>>1;
							newRighRGB[x*6+5] = (srcRighRGB[x*3+2]+srcRighRGB[x*3+5])>>1;
						}
						newRighRGB[x*6+0] = srcRighRGB[x*3+0];
						newRighRGB[x*6+1] = srcRighRGB[x*3+1];
						newRighRGB[x*6+2] = srcRighRGB[x*3+2];
						newRighRGB[x*6+3] = srcRighRGB[x*3+0];
						newRighRGB[x*6+4] = srcRighRGB[x*3+1];
						newRighRGB[x*6+5] = srcRighRGB[x*3+2];
						
						x=lwidth/2-1;
						newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
						newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
						newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
						newLeftRGB[x*6+3] = srcLeftRGB[x*3+0];
						newLeftRGB[x*6+4] = srcLeftRGB[x*3+1];
						newLeftRGB[x*6+5] = srcLeftRGB[x*3+2];
						for(;x>=0;x--)
						{
							newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
							newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
							newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
							newLeftRGB[x*6+3] = (srcLeftRGB[x*3+0]+srcLeftRGB[x*3+3])>>1;
							newLeftRGB[x*6+4] = (srcLeftRGB[x*3+1]+srcLeftRGB[x*3+4])>>1;
							newLeftRGB[x*6+5] = (srcLeftRGB[x*3+2]+srcLeftRGB[x*3+5])>>1;
						}							

						newLeftRGB += local_pitch/sizeof(PIXEL);
						newRighRGB += local_pitch/sizeof(PIXEL);
						srcLeftRGB += local_pitch/sizeof(PIXEL);
						srcRighRGB += local_pitch/sizeof(PIXEL);
					}
				}
				else
				{
					assert(0); // no other pixel format supported
				}
				decoder->source_channels = 2;
			}
			else if(leftonly) // extract Left channel only.
			{
				if(internal_format == DECODED_FORMAT_W13A)
				{
					lwidth = local_pitch / 8;						
					srcRighRGB += (lwidth / 2) * 4;

					for(y=0;y<lheight;y++)
					{
						x=lwidth/2-1;
						newLeftRGB[x*8+0] = srcRighRGB[x*4+0];
						newLeftRGB[x*8+1] = srcRighRGB[x*4+1];
						newLeftRGB[x*8+2] = srcRighRGB[x*4+2];
						newLeftRGB[x*8+3] = srcRighRGB[x*4+3];
						newLeftRGB[x*8+4] = srcRighRGB[x*4+0];
						newLeftRGB[x*8+5] = srcRighRGB[x*4+1];
						newLeftRGB[x*8+6] = srcRighRGB[x*4+2];
						newLeftRGB[x*8+7] = srcRighRGB[x*4+3];
						for(;x>=0;x--)
						{
							newLeftRGB[x*8+0] = srcRighRGB[x*4+0];
							newLeftRGB[x*8+1] = srcRighRGB[x*4+1];
							newLeftRGB[x*8+2] = srcRighRGB[x*4+2];
							newLeftRGB[x*8+3] = srcRighRGB[x*4+3];
							newLeftRGB[x*8+4] = (srcRighRGB[x*4+0]+srcRighRGB[x*4+4])>>1;
							newLeftRGB[x*8+5] = (srcRighRGB[x*4+1]+srcRighRGB[x*4+5])>>1;
							newLeftRGB[x*8+6] = (srcRighRGB[x*4+2]+srcRighRGB[x*4+6])>>1;
							newLeftRGB[x*8+7] = (srcRighRGB[x*4+3]+srcRighRGB[x*4+7])>>1;
						}

						newLeftRGB += local_pitch/sizeof(PIXEL);
						newRighRGB += local_pitch/sizeof(PIXEL);
						srcLeftRGB += local_pitch/sizeof(PIXEL);
						srcRighRGB += local_pitch/sizeof(PIXEL);
					}
				}
				else if(internal_format == DECODED_FORMAT_WP13)
				{
					lwidth = local_pitch / 6;						
					srcRighRGB += (lwidth / 2) * 3;

					for(y=0;y<lheight;y++)
					{							
						x=lwidth/2-1;
						newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
						newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
						newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
						newLeftRGB[x*6+3] = srcLeftRGB[x*3+0];
						newLeftRGB[x*6+4] = srcLeftRGB[x*3+1];
						newLeftRGB[x*6+5] = srcLeftRGB[x*3+2];
						for(;x>=0;x--)
						{
							newLeftRGB[x*6+0] = srcLeftRGB[x*3+0];
							newLeftRGB[x*6+1] = srcLeftRGB[x*3+1];
							newLeftRGB[x*6+2] = srcLeftRGB[x*3+2];
							newLeftRGB[x*6+3] = (srcLeftRGB[x*3+0]+srcLeftRGB[x*3+3])>>1;
							newLeftRGB[x*6+4] = (srcLeftRGB[x*3+1]+srcLeftRGB[x*3+4])>>1;
							newLeftRGB[x*6+5] = (srcLeftRGB[x*3+2]+srcLeftRGB[x*3+5])>>1;
						}							

						newLeftRGB += local_pitch/sizeof(PIXEL);
						newRighRGB += local_pitch/sizeof(PIXEL);
						srcLeftRGB += local_pitch/sizeof(PIXEL);
						srcRighRGB += local_pitch/sizeof(PIXEL);
					}
				}
				else
				{
					assert(0); // no other pixel format supported
				}
			}
			else if(righonly) // extract Right channel only.
			{
				if(internal_format == DECODED_FORMAT_W13A)
				{
					lwidth = local_pitch / 8;						
					srcRighRGB += (lwidth / 2) * 4;

					for(y=0;y<lheight;y++)
					{
						for(x=0;x<lwidth/2-1;x++)
						{
							newLeftRGB[x*8+0] = srcRighRGB[x*4+0];
							newLeftRGB[x*8+1] = srcRighRGB[x*4+1];
							newLeftRGB[x*8+2] = srcRighRGB[x*4+2];
							newLeftRGB[x*8+3] = srcRighRGB[x*4+3];
							newLeftRGB[x*8+4] = (srcRighRGB[x*4+0]+srcRighRGB[x*4+4])>>1;
							newLeftRGB[x*8+5] = (srcRighRGB[x*4+1]+srcRighRGB[x*4+5])>>1;
							newLeftRGB[x*8+6] = (srcRighRGB[x*4+2]+srcRighRGB[x*4+6])>>1;
							newLeftRGB[x*8+7] = (srcRighRGB[x*4+3]+srcRighRGB[x*4+7])>>1;
						}
						newLeftRGB[x*8+0] = srcRighRGB[x*4+0];
						newLeftRGB[x*8+1] = srcRighRGB[x*4+1];
						newLeftRGB[x*8+2] = srcRighRGB[x*4+2];
						newLeftRGB[x*8+3] = srcRighRGB[x*4+3];
						newLeftRGB[x*8+4] = srcRighRGB[x*4+0];
						newLeftRGB[x*8+5] = srcRighRGB[x*4+1];
						newLeftRGB[x*8+6] = srcRighRGB[x*4+2];
						newLeftRGB[x*8+7] = srcRighRGB[x*4+3];

						newLeftRGB += local_pitch/sizeof(PIXEL);
						newRighRGB += local_pitch/sizeof(PIXEL);
						srcLeftRGB += local_pitch/sizeof(PIXEL);
						srcRighRGB += local_pitch/sizeof(PIXEL);
					}
				}
				else if(internal_format == DECODED_FORMAT_WP13)
				{
					lwidth = local_pitch / 6;						
					srcRighRGB += (lwidth / 2) * 3;

					for(y=0;y<lheight;y++)
					{
						for(x=0;x<lwidth/2-1;x++)
						{
							newLeftRGB[x*6+0] = srcRighRGB[x*3+0];
							newLeftRGB[x*6+1] = srcRighRGB[x*3+1];
							newLeftRGB[x*6+2] = srcRighRGB[x*3+2];
							newLeftRGB[x*6+3] = (srcRighRGB[x*3+0]+srcRighRGB[x*3+3])>>1;
							newLeftRGB[x*6+4] = (srcRighRGB[x*3+1]+srcRighRGB[x*3+4])>>1;
							newLeftRGB[x*6+5] = (srcRighRGB[x*3+2]+srcRighRGB[x*3+5])>>1;
						}
						newLeftRGB[x*6+0] = srcRighRGB[x*3+0];
						newLeftRGB[x*6+1] = srcRighRGB[x*3+1];
						newLeftRGB[x*6+2] = srcRighRGB[x*3+2];
						newLeftRGB[x*6+3] = srcRighRGB[x*3+0];
						newLeftRGB[x*6+4] = srcRighRGB[x*3+1];
						newLeftRGB[x*6+5] = srcRighRGB[x*3+2];
						
						newLeftRGB += local_pitch/sizeof(PIXEL);
						newRighRGB += local_pitch/sizeof(PIXEL);
						srcLeftRGB += local_pitch/sizeof(PIXEL);
						srcRighRGB += local_pitch/sizeof(PIXEL);
					}
				}
				else
				{
					assert(0); // no other pixel format supported
				}
			}
		}

		ret = true;
	}
	else
	{
		ret = false;
	}


	*channel_offset_ptr = channel_offset;
	return ret;
}
