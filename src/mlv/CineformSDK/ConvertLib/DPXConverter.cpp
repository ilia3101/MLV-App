/*! @file DPXConverter.cpp

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
#include "ColorFlags.h"
#include "ColorMatrix.h"
#include "ImageConverter.h"
#include "DPXConverter.h"


/*!
	@brief Convert the common DPX 10-bit RGB format to Avid 10-bit CbYCrY
*/
void DPXConverter::ConvertRGB10ToCbYCrY_10bit_2_8(void *input_buffer, size_t input_pitch,
												  void *output_buffer, size_t output_pitch,
												  int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	// Instantiate a color converter for RGB to YCbCr
	RGBToYCbCr converter(COLOR_FLAGS_VS_709);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	size_t upper_row_pitch = width / 2;
	size_t lower_row_pitch = width * 2;

	uint8_t *upper_plane = reinterpret_cast<uint8_t *>(output_buffer);
	uint8_t *lower_plane = upper_plane + width * height / 2;

	uint8_t *upper_row_ptr = upper_plane;
	uint8_t *lower_row_ptr = lower_plane;

	for (int row = 0; row < height; row++)
	{
		// Output width must be a multiple of two
		assert((width % 2) == 0);

		uint32_t *input_pixel_ptr = reinterpret_cast<uint32_t *>(input_row_ptr);

		// Two columns of input yield one byte of output in the upper plane
		for (int column = 0; column < width; column += 2)
		{
			uint16_t R1, G1, B1;
			uint16_t R2, G2, B2;
			uint16_t Y1, U1, V1;
			uint16_t Y2, U2, V2;
			uint32_t Cr, Cb;
			uint16_t Y1_upper, Cr_upper, Y2_upper, Cb_upper;
			uint16_t Y1_lower, Cr_lower, Y2_lower, Cb_lower;
			uint16_t upper;

			// Note that the RGB and YCbCr components have 16 bits of precision

			// Unpack the next RGB tuple and convert to YCbCr
			Unpack10(*(input_pixel_ptr++), &R1, &G1, &B1);
			converter.Convert(R1, G1, B1, &Y1, &U1, &V1);

			// Unpack the next RGB tuple and convert to YCbCr
			Unpack10(*(input_pixel_ptr++), &R2, &G2, &B2);
			converter.Convert(R2, G2, B2, &Y2, &U2, &V2);

			// Downsample the chroma
			Cb = ((uint32_t)U1 + (uint32_t)U2) >> 1;
			Cr = ((uint32_t)V1 + (uint32_t)V2) >> 1;

			//TODO: Fold the shift to divide by two into the following shift operations

			// Process Y1
			Y1_upper = (Y1 >> 6) & 0x03;		// Least significant 2 bits
			Y1_lower = (Y1 >> 8) & 0xFF;		// Most significant 8 bits

			// Process Cr
			Cr_upper = (Cr >> 6) & 0x03;		// Least significant 2 bits
			Cr_lower = (Cr >> 8) & 0xFF;		// Most significant 8 bits

			// Process Y2
			Y2_upper = (Y2 >> 6) & 0x03;		// Least significant 2 bits
			Y2_lower = (Y2 >> 8) & 0xFF;		// Most significant 8 bits

			// Process Cb
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

/*!
	@brief Convert the common DPX 10-bit RGB format to Avid 10-bit ARGB
*/
void DPXConverter::ConvertRGB10ToARGB_10bit_2_8(void *input_buffer, size_t input_pitch,
												void *output_buffer, size_t output_pitch,
												int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	size_t upper_row_pitch = width;
	size_t lower_row_pitch = width * 4;

#if 1
	uint8_t *upper_plane = reinterpret_cast<uint8_t *>(output_buffer);
	uint8_t *lower_plane = upper_plane + width * height;
#else
	uint8_t *lower_plane = reinterpret_cast<uint8_t *>(output_buffer);
	uint8_t *upper_plane = lower_plane + 4 * width * height;
#endif

	uint8_t *upper_row_ptr = upper_plane;
	uint8_t *lower_row_ptr = lower_plane;

	for (int row = 0; row < height; row++)
	{
		uint32_t *input_pixel_ptr = reinterpret_cast<uint32_t *>(input_row_ptr);

		// Each RGB pixel in the input yields one ARGB pixel in the output
		for (int column = 0; column < width; column++)
		{
			uint16_t R, G, B;
			uint16_t R_upper, G_upper, B_upper;
			uint16_t R_lower, G_lower, B_lower;
			uint16_t upper;

			const uint16_t A_upper = 0x03;
			const uint16_t A_lower = 0xFF;

			// Note that the RGB components have 16 bits of precision

			// Unpack the next RGB tuple into 16-bit components
			Unpack10(*(input_pixel_ptr++), &R, &G, &B);

			// Split each RGB value into upper and lower parts
			R_upper = (R >> 6) & 0x03;		// Least significant 2 bits
			R_lower = (R >> 8) & 0xFF;		// Most significant 8 bits

			G_upper = (G >> 6) & 0x03;		// Least significant 2 bits
			G_lower = (G >> 8) & 0xFF;		// Most significant 8 bits

			B_upper = (B >> 6) & 0x03;		// Least significant 2 bits
			B_lower = (B >> 8) & 0xFF;		// Most significant 8 bits

			// Pack the least significant bits into one byte
			upper = (A_upper << 6) | (R_upper << 4) | (G_upper << 2) | B_upper;

			// Write the byte to the upper plane in the output image
			upper_row_ptr[column] = (uint8_t)upper;

			// Output the most significant bits of each component to the lower plane
			lower_row_ptr[4 * column + 0] = (uint8_t)A_lower;
			lower_row_ptr[4 * column + 1] = (uint8_t)R_lower;
			lower_row_ptr[4 * column + 2] = (uint8_t)G_lower;
			lower_row_ptr[4 * column + 3] = (uint8_t)B_lower;
		}

		input_row_ptr += input_row_pitch;
		upper_row_ptr += upper_row_pitch;
		lower_row_ptr += lower_row_pitch;
	}
}

/*!
	@Brief Convert B64A to the common DPX pixel format
*/
void DPXConverter::ConvertB64AToDPX0(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		uint16_t *input_pixel_ptr = reinterpret_cast<uint16_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		// Convert each 16-bit ARGB tuple to a packed 10-bit DPX pixel
		for (int column = 0; column < width; column++)
		{
			uint16_t A = *(input_pixel_ptr++);
			uint16_t R = *(input_pixel_ptr++);
			uint16_t G = *(input_pixel_ptr++);
			uint16_t B = *(input_pixel_ptr++);

			// Eliminate compiler warning about unused variable
			(void)A;

			// Pack 16-bit components into a 32-bit word of 10-bit components
			*(output_pixel_ptr++) = Pack10(R, G, B);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

/*!
	@Brief Convert 8-bit RGBA to the common DPX pixel format
*/
void DPXConverter::ConvertRGB32ToDPX0(void *input_buffer, size_t input_pitch,
									  void *output_buffer, size_t output_pitch,
									  int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	// The image is inverted
	input_row_ptr += (height - 1) * input_row_pitch;

	for (int row = 0; row < height; row++)
	{
		uint8_t *input_pixel_ptr = reinterpret_cast<uint8_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		// Convert each 8-bit BGRA tuple to a packed 10-bit DPX pixel
		for (int column = 0; column < width; column++)
		{
			uint16_t B = *(input_pixel_ptr++);
			uint16_t G = *(input_pixel_ptr++);
			uint16_t R = *(input_pixel_ptr++);
			uint16_t A = *(input_pixel_ptr++);

			// Ignore the alpha component
			(void)A;

			// Scale the 8-bit pixels to 16-bit precision
			B <<= 8;
			G <<= 8;
			R <<= 8;

			// Pack 16-bit components into a 32-bit word of 10-bit components
			*(output_pixel_ptr++) = Pack10(R, G, B);
		}

		//input_row_ptr += input_row_pitch;
		input_row_ptr -= input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

/*!
	@Brief Convert 16-bit RGB to the common DPX pixel format
*/
void DPXConverter::ConvertWP13ToDPX0(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		int16_t *input_pixel_ptr = reinterpret_cast<int16_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		// Convert each 8-bit BGRA tuple to a packed 10-bit DPX pixel
		for (int column = 0; column < width; column++)
		{
			int32_t R = *(input_pixel_ptr++);
			int32_t G = *(input_pixel_ptr++);
			int32_t B = *(input_pixel_ptr++);

			// Scale the 13-bit pixels to 16-bit precision
			R <<= 3;
			G <<= 3;
			B <<= 3;

			// Clamp the pixel values to the range of 16-bit unsigned integers
			if (R > UINT16_MAX) R = UINT16_MAX;
			else if (R < 0) R = 0;

			if (G > UINT16_MAX) G = UINT16_MAX;
			else if (G < 0) G = 0;

			if (B > UINT16_MAX) B = UINT16_MAX;
			else if (B < 0) B = 0;

			// Pack 16-bit components into a 32-bit word of 10-bit components
			*(output_pixel_ptr++) = Pack10(R, G, B);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}


/*!
	@Brief Convert 16-bit Bayer to the common DPX pixel format
*/
void DPXConverter::ConvertBYR4ToDPX0(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = 2 * input_pitch;
	size_t half_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;
	for (int row = 0; row < height; row++)
	{
		uint16_t *row1_pixel_ptr = reinterpret_cast<uint16_t *>(input_row_ptr);
		uint16_t *row2_pixel_ptr = reinterpret_cast<uint16_t *>(input_row_ptr + half_row_pitch);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		// Convert each 16-bit Bayer tuple to a packed 10-bit DPX pixel
		for (int column = 0; column < width; column++)
		{
			int32_t R = *(row1_pixel_ptr++);
			int32_t G1 = *(row1_pixel_ptr++);
			int32_t G2 = *(row2_pixel_ptr++);
			int32_t B = *(row2_pixel_ptr++);

			// Average the green pixels
			int32_t G = (G1 + G2) / 2;

			// Clamp the pixel values to the range of 16-bit unsigned integers
			if (R > UINT16_MAX) R = UINT16_MAX;
			else if (R < 0) R = 0;

			if (G > UINT16_MAX) G = UINT16_MAX;
			else if (G < 0) G = 0;

			if (B > UINT16_MAX) B = UINT16_MAX;
			else if (B < 0) B = 0;

			// Pack 16-bit components into a 32-bit word of 10-bit components
			*(output_pixel_ptr++) = Pack10(R, G, B);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}


/*!
	@Brief Convert 16-bit Bayer organized by rows to the common DPX pixel format
*/
void DPXConverter::ConvertBYR3ToDPX0(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = 2 * input_pitch;
	//size_t half_row_pitch = input_pitch;
	size_t quarter_row_pitch = input_pitch / 2;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;
	for (int row = 0; row < height; row++)
	{
		uint16_t *R_row_ptr = reinterpret_cast<uint16_t *>(input_row_ptr);
		uint16_t *G1_row_ptr = reinterpret_cast<uint16_t *>(input_row_ptr + quarter_row_pitch);
		uint16_t *G2_row_ptr = reinterpret_cast<uint16_t *>(input_row_ptr + 2 * quarter_row_pitch);
		uint16_t *B_row_ptr = reinterpret_cast<uint16_t *>(input_row_ptr + 3 * quarter_row_pitch);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		// Convert each 10-bit Bayer tuple to a packed 10-bit DPX pixel
		for (int column = 0; column < width; column++)
		{
			int32_t R = *(R_row_ptr++);
			int32_t G1 = *(G1_row_ptr++);
			int32_t G2 = *(G2_row_ptr++);
			int32_t B = *(B_row_ptr++);

			// Scale the values to 16 bits
			R <<= 6;
			G1 <<= 6;
			G2 <<= 6;
			B <<= 6;

			// Average the green pixels
			int32_t G = (G1 + G2) / 2;

			// Clamp the pixel values to the range of 16-bit unsigned integers
			if (R > UINT16_MAX) R = UINT16_MAX;
			else if (R < 0) R = 0;

			if (G > UINT16_MAX) G = UINT16_MAX;
			else if (G < 0) G = 0;

			if (B > UINT16_MAX) B = UINT16_MAX;
			else if (B < 0) B = 0;

			// Pack 16-bit components into a 32-bit word of 10-bit components
			*(output_pixel_ptr++) = Pack10(R, G, B);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}


/*!
	@Brief Convert YU64 to the common DPX 10-bit RGB 4:4:4 pixel format

	@todo Add code to properly convert YUV to RGB.
*/
void DPXConverter::ConvertYU64ToDPX0(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	// Color conversion coefficients for 709 full range
	uint32_t ymult = 8192;			// 1.0
	uint32_t r_vmult = 12616;		// 1.540
	uint32_t g_vmult = 3760;		// 0.459
	uint32_t g_umult = 1499;		// 0.183
	uint32_t b_umult = 14877;		// 1.816

	uint32_t chroma_offset = (1 << 15);

	for (int row = 0; row < height; row++)
	{
		uint16_t *input_pixel_ptr = reinterpret_cast<uint16_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		assert((width % 2) == 0);
		for (int column = 0; column < width; column += 2)
		{
#if 1
			int32_t Y1, Y2, U1, V1;
			int32_t R1, R2, G1, G2, B1, B2;

			// Load two pixels (four components)
			Y1 = *(input_pixel_ptr++);
			V1 = *(input_pixel_ptr++);

			Y2 = *(input_pixel_ptr++);
			U1 = *(input_pixel_ptr++);

			// Remove the chroma offset
			U1 -= chroma_offset;
			V1 -= chroma_offset;

			//TODO: Add code to correctly convert the pixels to RGB
			R1 = ymult * Y1 + r_vmult * V1;
			R2 = ymult * Y2 + r_vmult * V1;
			B1 = ymult * Y1 + b_umult * U1;
			B2 = ymult * Y2 + b_umult * U1;
			G1 = ymult * Y1 + g_umult * U1 + g_vmult * V1;
			G2 = ymult * Y2 + g_umult * U1 + g_vmult * V1;

			R1 = Saturate16u(R1 >> 13);
			G1 = Saturate16u(G1 >> 13);
			B1 = Saturate16u(B1 >> 13);
			R2 = Saturate16u(R2 >> 13);
			G2 = Saturate16u(G2 >> 13);
			B2 = Saturate16u(B2 >> 13);
#else
			uint16_t Y1, Y2, U1, V1;
			uint16_t R1, R2, G1, G2, B1, B2;

			// Load two pixels (four components)
			Y1 = *(input_pixel_ptr++);
			V1 = *(input_pixel_ptr++);

			Y2 = *(input_pixel_ptr++);
			U1 = *(input_pixel_ptr++);

			// Output only the luma channel (monochrome)
			R1 = Y1;
			R2 = Y2;
			B1 = Y1;
			B2 = Y2;
			G1 = Y1;
			G2 = Y2;
#endif
			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(R1, G1, B1);
			*(output_pixel_ptr++) = Pack10(R2, G2, B2);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

/*!
	@Brief Convert YU64 to the 10-bit YUV 4:2:2 DPX pixel format
*/
void DPXConverter::ConvertYU64ToDPX1(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		uint16_t *input_pixel_ptr = reinterpret_cast<uint16_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		assert((((size_t)width * 8) / 3) <= output_pitch);
		for (int column = 0; column < width; column += 6)
		{
			uint_least16_t Y1, Y2, Y3, Y4, Y5, Y6;
			uint_least16_t U1, U2, U3, V1, V2, V3;

			// Load six pixels (twelve components)
			Y1 = *(input_pixel_ptr++);
			V1 = *(input_pixel_ptr++);

			Y2 = *(input_pixel_ptr++);
			U1 = *(input_pixel_ptr++);

			Y3 = *(input_pixel_ptr++);
			V2 = *(input_pixel_ptr++);

			Y4 = *(input_pixel_ptr++);
			U2 = *(input_pixel_ptr++);

			Y5 = *(input_pixel_ptr++);
			V3 = *(input_pixel_ptr++);

			Y6 = *(input_pixel_ptr++);
			U3 = *(input_pixel_ptr++);

			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(U1, Y1, V1);
			*(output_pixel_ptr++) = Pack10(Y2, U2, Y3);
			*(output_pixel_ptr++) = Pack10(V2, Y4, U3);
			*(output_pixel_ptr++) = Pack10(Y5, V3, Y6);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

/*!
	@Brief Convert 8-but YUV 4:2:2 to the 10-bit RGB DPX pixel format
*/
void DPXConverter::ConvertYUYVToDPX0(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	// Color conversion coefficients for 709 full range
	uint32_t ymult = 8192;			// 1.0
	uint32_t r_vmult = 12616;		// 1.540
	uint32_t g_vmult = 3760;		// 0.459
	uint32_t g_umult = 1499;		// 0.183
	uint32_t b_umult = 14877;		// 1.816

	uint32_t chroma_offset = (1 << 7);

	// Reduce the output precision to 16 bits
	const int shift = (13 - 8);

	for (int row = 0; row < height; row++)
	{
		uint8_t *input_pixel_ptr = reinterpret_cast<uint8_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		assert((width % 2) == 0);
		for (int column = 0; column < width; column += 2)
		{
			int32_t Y1, Y2, U1, V1;
			int32_t R1, R2, G1, G2, B1, B2;

			// Load two pixels (four components)
			Y1 = *(input_pixel_ptr++);
			U1 = *(input_pixel_ptr++);

			Y2 = *(input_pixel_ptr++);
			V1 = *(input_pixel_ptr++);

			// Remove the chroma offset
			U1 -= chroma_offset;
			V1 -= chroma_offset;

			//TODO: Add code to correctly convert the pixels to RGB
			R1 = ymult * Y1 + r_vmult * V1;
			R2 = ymult * Y2 + r_vmult * V1;
			B1 = ymult * Y1 + b_umult * U1;
			B2 = ymult * Y2 + b_umult * U1;
			G1 = ymult * Y1 + g_umult * U1 + g_vmult * V1;
			G2 = ymult * Y2 + g_umult * U1 + g_vmult * V1;

			R1 = Saturate16u(R1 >> shift);
			G1 = Saturate16u(G1 >> shift);
			B1 = Saturate16u(B1 >> shift);
			R2 = Saturate16u(R2 >> shift);
			G2 = Saturate16u(G2 >> shift);
			B2 = Saturate16u(B2 >> shift);

			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(R1, G1, B1);
			*(output_pixel_ptr++) = Pack10(R2, G2, B2);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

/*!
	@Brief Convert 8-but YUV 4:2:2 to the 10-bit YUV 4:2:2 DPX pixel format
*/
void DPXConverter::ConvertYUYVToDPX1(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		uint8_t *input_pixel_ptr = reinterpret_cast<uint8_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		assert((((size_t)width * 8) / 3) <= output_pitch);
		for (int column = 0; column < width; column += 6)
		{
			const int scale = 8;

			uint_least16_t Y1, Y2, Y3, Y4, Y5, Y6;
			uint_least16_t U1, U2, U3, V1, V2, V3;

			// Load six pixels (twelve components)
			Y1 = *(input_pixel_ptr++);
			U1 = *(input_pixel_ptr++);

			Y2 = *(input_pixel_ptr++);
			V1 = *(input_pixel_ptr++);

			Y3 = *(input_pixel_ptr++);
			U2 = *(input_pixel_ptr++);

			Y4 = *(input_pixel_ptr++);
			V2 = *(input_pixel_ptr++);

			Y5 = *(input_pixel_ptr++);
			U3 = *(input_pixel_ptr++);

			Y6 = *(input_pixel_ptr++);
			V3 = *(input_pixel_ptr++);

			// Scale each component to 16-bit precision
			Y1 = (Y1 << scale);
			V1 = (V1 << scale);
			Y2 = (Y2 << scale);
			U1 = (U1 << scale);
			Y3 = (Y3 << scale);
			V2 = (V2 << scale);
			Y4 = (Y4 << scale);
			U2 = (U2 << scale);
			Y5 = (Y5 << scale);
			V3 = (V3 << scale);
			Y6 = (Y6 << scale);
			U3 = (U3 << scale);

			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(U1, Y1, V1);
			*(output_pixel_ptr++) = Pack10(Y2, U2, Y3);
			*(output_pixel_ptr++) = Pack10(V2, Y4, U3);
			*(output_pixel_ptr++) = Pack10(Y5, V3, Y6);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

/*!
	@Brief Convert 8-but UYVY 4:2:2 to the 10-bit YUV 4:2:2 DPX pixel format
*/
void DPXConverter::ConvertUYVYToDPX1(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		uint8_t *input_pixel_ptr = reinterpret_cast<uint8_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		assert((((size_t)width * 8) / 3) <= output_pitch);
		for (int column = 0; column < width; column += 6)
		{
			const int scale = 8;

			uint_least16_t Y1, Y2, Y3, Y4, Y5, Y6;
			uint_least16_t U1, U2, U3, V1, V2, V3;

			// Load six pixels (twelve components)
			U1 = *(input_pixel_ptr++);
			Y1 = *(input_pixel_ptr++);

			V1 = *(input_pixel_ptr++);
			Y2 = *(input_pixel_ptr++);

			U2 = *(input_pixel_ptr++);
			Y3 = *(input_pixel_ptr++);

			V2 = *(input_pixel_ptr++);
			Y4 = *(input_pixel_ptr++);

			U3 = *(input_pixel_ptr++);
			Y5 = *(input_pixel_ptr++);

			V3 = *(input_pixel_ptr++);
			Y6 = *(input_pixel_ptr++);

			// Scale each component to 16-bit precision
			Y1 = (Y1 << scale);
			V1 = (V1 << scale);
			Y2 = (Y2 << scale);
			U1 = (U1 << scale);
			Y3 = (Y3 << scale);
			V2 = (V2 << scale);
			Y4 = (Y4 << scale);
			U2 = (U2 << scale);
			Y5 = (Y5 << scale);
			V3 = (V3 << scale);
			Y6 = (Y6 << scale);
			U3 = (U3 << scale);

			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(U1, Y1, V1);
			*(output_pixel_ptr++) = Pack10(Y2, U2, Y3);
			*(output_pixel_ptr++) = Pack10(V2, Y4, U3);
			*(output_pixel_ptr++) = Pack10(Y5, V3, Y6);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

/*!
	@Brief Repack the v210 format into the 10-bit YUV 4:2:2 DPX pixel format
*/
void DPXConverter::ConvertV210ToDPX1(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		uint32_t *input_pixel_ptr = reinterpret_cast<uint32_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		assert((((size_t)width * 8) / 3) <= output_pitch);
		for (int column = 0; column < width; column += 6)
		{
			//const int scale = 8;

			uint16_t Y1, Y2, Y3, Y4, Y5, Y6;
			uint16_t U1, U2, U3, V1, V2, V3;

			// Unpack six pixels (twelve components)
			V210_PixelFormat::Unpack(*(input_pixel_ptr++), &U1, &Y1, &V1);
			V210_PixelFormat::Unpack(*(input_pixel_ptr++), &Y2, &U2, &Y3);
			V210_PixelFormat::Unpack(*(input_pixel_ptr++), &V2, &Y4, &U3);
			V210_PixelFormat::Unpack(*(input_pixel_ptr++), &Y5, &V3, &Y6);

			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(U1, Y1, V1);
			*(output_pixel_ptr++) = Pack10(Y2, U2, Y3);
			*(output_pixel_ptr++) = Pack10(V2, Y4, U3);
			*(output_pixel_ptr++) = Pack10(Y5, V3, Y6);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

/*!
	@Brief Convert 8-bit AYCbCr to the 10-bit YUV 4:2:2 DPX pixel format
*/
void DPXConverter::ConvertR408ToDPX1(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		uint8_t *input_pixel_ptr = reinterpret_cast<uint8_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		// Convert the 8-bit AYCbCr 4:4:4:4 tuples to 10-bit DPX CbYCr 4:2:2
		for (int column = 0; column < width; column += 6)
		{
			uint_least16_t A, Y, Cb, Cr;

			uint16_t Y1, Y2, Y3, Y4, Y5, Y6;
			uint16_t U1, U2, U3, V1, V2, V3;


			/***** First pair of CbYCr tuples *****/

			A = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cb = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			//A = (A << 8) + 255;
			Y1 = (Y + 16) << 8;
			U1 = (Cb << 8);
			V1 = (Cr << 8);

			A = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cb = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			//A = (A << 8) + 255;
			Y2 = (Y + 16) << 8;
			Cb = (Cb << 8);
			Cr = (Cr << 8);

			// Downsample the chroma
			U1 = (U1 + Cb) >> 1;
			V1 = (V1 + Cr) >> 1;


			/***** Second pair of CbYCr tuples *****/

			A = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cb = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			//A = (A << 8) + 255;
			Y3 = (Y + 16) << 8;
			U2 = (Cb << 8);
			V2 = (Cr << 8);

			A = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cb = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			//A = (A << 8) + 255;
			Y4 = (Y + 16) << 8;
			Cb = (Cb << 8);
			Cr = (Cr << 8);

			// Downsample the chroma
			U2 = (U2 + Cb) >> 1;
			V2 = (V2 + Cr) >> 1;


			/***** Third pair of CbYCr tuples *****/

			A = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cb = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			//A = (A << 8) + 255;
			Y5 = (Y + 16) << 8;
			U3 = (Cb << 8);
			V3 = (Cr << 8);

			A = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cb = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			//A = (A << 8) + 255;
			Y6 = (Y + 16) << 8;
			Cb = (Cb << 8);
			Cr = (Cr << 8);

			// Downsample the chroma
			U3 = (U3 + Cb) >> 1;
			V3 = (V3 + Cr) >> 1;


			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(U1, Y1, V1);
			*(output_pixel_ptr++) = Pack10(Y2, U2, Y3);
			*(output_pixel_ptr++) = Pack10(V2, Y4, U3);
			*(output_pixel_ptr++) = Pack10(Y5, V3, Y6);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

/*!
	@Brief Convert 8-bit AYCbCr to the 10-bit YUV 4:2:2 DPX pixel format
*/
void DPXConverter::ConvertV408ToDPX1(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		uint8_t *input_pixel_ptr = reinterpret_cast<uint8_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		// Convert the 8-bit AYCbCr 4:4:4:4 tuples to 10-bit DPX CbYCr 4:2:2
		for (int column = 0; column < width; column += 6)
		{
			uint_least16_t A, Y, Cb, Cr;

			uint16_t Y1, Y2, Y3, Y4, Y5, Y6;
			uint16_t U1, U2, U3, V1, V2, V3;


			/***** First pair of CbYCr tuples *****/

			Cb = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);
			A = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			//A = (A << 8) + 255;
			Y1 = (Y << 8);
			U1 = (Cb << 8);
			V1 = (Cr << 8);

			Cb = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);
			A = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			//A = (A << 8) + 255;
			Y2 = (Y << 8);
			Cb = (Cb << 8);
			Cr = (Cr << 8);

			// Downsample the chroma
			U1 = (U1 + Cb) >> 1;
			V1 = (V1 + Cr) >> 1;


			/***** Second pair of CbYCr tuples *****/

			Cb = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);
			A = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			//A = (A << 8) + 255;
			Y3 = (Y << 8);
			U2 = (Cb << 8);
			V2 = (Cr << 8);

			Cb = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);
			A = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			//A = (A << 8) + 255;
			Y4 = (Y << 8);
			Cb = (Cb << 8);
			Cr = (Cr << 8);

			// Downsample the chroma
			U2 = (U2 + Cb) >> 1;
			V2 = (V2 + Cr) >> 1;


			/***** Third pair of CbYCr tuples *****/

			Cb = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);
			A = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			//A = (A << 8) + 255;
			Y5 = (Y << 8);
			U3 = (Cb << 8);
			V3 = (Cr << 8);

			Cb = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);
			A = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			//A = (A << 8) + 255;
			Y6 = (Y << 8);
			Cb = (Cb << 8);
			Cr = (Cr << 8);

			// Downsample the chroma
			U3 = (U3 + Cb) >> 1;
			V3 = (V3 + Cr) >> 1;


			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(U1, Y1, V1);
			*(output_pixel_ptr++) = Pack10(Y2, U2, Y3);
			*(output_pixel_ptr++) = Pack10(V2, Y4, U3);
			*(output_pixel_ptr++) = Pack10(Y5, V3, Y6);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

/*!
	@Brief Convert Avid CbYCrY 10-bit 2.8 format to DPX 10-bit YUV 4:2:2
*/
void DPXConverter::ConvertCbYCrY_10bit_2_8ToDPX1(void *input_buffer, size_t input_pitch,
												 void *output_buffer, size_t output_pitch,
												 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *upper_plane = reinterpret_cast<uint8_t *>(input_buffer);
	uint8_t *lower_plane = upper_plane + width * height / 2;

	uint8_t *upper_row_ptr = upper_plane;
	uint8_t *lower_row_ptr = lower_plane;

	int upper_row_pitch = width / 2;
	int lower_row_pitch = width * 2;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		// The output width must be a multiple of six
		assert((width % 6) == 0);

		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		// Each byte in the upper plane yields two columns of output
		for (int column = 0; column < width; column += 6)
		{
			uint16_t Y1_upper, Cr_upper, Y2_upper, Cb_upper;
			uint16_t Y1_lower, Cr_lower, Y2_lower, Cb_lower;
			uint16_t Y1_pixel, Y2_pixel, Cr_pixel, Cb_pixel;

			uint16_t Y1, Y2, Y3, Y4, Y5, Y6;
			uint16_t U1, U2, U3, V1, V2, V3;

			uint16_t upper;


			/***** First four tuple of CbYCrY components *****/

			// Unpack the 2-bit pixels in the upper plane
			upper = upper_row_ptr[column/2];

			Cb_upper = (upper >> 6) & 0x03;
			Y1_upper = (upper >> 4) & 0x03;
			Cr_upper = (upper >> 2) & 0x03;
			Y2_upper = (upper >> 0) & 0x03;

			// Process the 8-bit pixels in the lower plane
			Cb_lower = lower_row_ptr[2 * column + 0];
			Y1_lower = lower_row_ptr[2 * column + 1];
			Cr_lower = lower_row_ptr[2 * column + 2];
			Y2_lower = lower_row_ptr[2 * column + 3];

			// Combine the upper and lower pixels into 10-bit pixels
			Y1_pixel = (Y1_lower << 2) | Y1_upper;
			Y2_pixel = (Y2_lower << 2) | Y2_upper;
			Cr_pixel = (Cr_lower << 2) | Cr_upper;
			Cb_pixel = (Cb_lower << 2) | Cb_upper;

			// Scale the 10-bit pixels to 16-bit precision
			Y1 = (Y1_pixel << 6);
			Y2 = (Y2_pixel << 6);
			V1 = (Cr_pixel << 6);
			U1 = (Cb_pixel << 6);


			/***** Second four tuple of CbYCrY components *****/

			// Unpack the 2-bit pixels in the upper plane
			upper = upper_row_ptr[column/2 + 1];

			Cb_upper = (upper >> 6) & 0x03;
			Y1_upper = (upper >> 4) & 0x03;
			Cr_upper = (upper >> 2) & 0x03;
			Y2_upper = (upper >> 0) & 0x03;

			// Process the 8-bit pixels in the lower plane
			Cb_lower = lower_row_ptr[2 * column + 4];
			Y1_lower = lower_row_ptr[2 * column + 5];
			Cr_lower = lower_row_ptr[2 * column + 6];
			Y2_lower = lower_row_ptr[2 * column + 7];

			// Combine the upper and lower pixels into 10-bit pixels
			Y1_pixel = (Y1_lower << 2) | Y1_upper;
			Y2_pixel = (Y2_lower << 2) | Y2_upper;
			Cr_pixel = (Cr_lower << 2) | Cr_upper;
			Cb_pixel = (Cb_lower << 2) | Cb_upper;

			// Scale the 10-bit pixels to 16-bit precision
			Y3 = (Y1_pixel << 6);
			Y4 = (Y2_pixel << 6);
			V2 = (Cr_pixel << 6);
			U2 = (Cb_pixel << 6);


			/***** Third four tuple of CbYCrY components *****/

			// Unpack the 2-bit pixels in the upper plane
			upper = upper_row_ptr[column/2 + 2];

			Cb_upper = (upper >> 6) & 0x03;
			Y1_upper = (upper >> 4) & 0x03;
			Cr_upper = (upper >> 2) & 0x03;
			Y2_upper = (upper >> 0) & 0x03;

			// Process the 8-bit pixels in the lower plane
			Cb_lower = lower_row_ptr[2 * column + 8];
			Y1_lower = lower_row_ptr[2 * column + 9];
			Cr_lower = lower_row_ptr[2 * column + 10];
			Y2_lower = lower_row_ptr[2 * column + 11];

			// Combine the upper and lower pixels into 10-bit pixels
			Y1_pixel = (Y1_lower << 2) | Y1_upper;
			Y2_pixel = (Y2_lower << 2) | Y2_upper;
			Cr_pixel = (Cr_lower << 2) | Cr_upper;
			Cb_pixel = (Cb_lower << 2) | Cb_upper;

			// Scale the 10-bit pixels to 16-bit precision
			Y5 = (Y1_pixel << 6);
			Y6 = (Y2_pixel << 6);
			V3 = (Cr_pixel << 6);
			U3 = (Cb_pixel << 6);


			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(U1, Y1, V1);
			*(output_pixel_ptr++) = Pack10(Y2, U2, Y3);
			*(output_pixel_ptr++) = Pack10(V2, Y4, U3);
			*(output_pixel_ptr++) = Pack10(Y5, V3, Y6);
		}

		upper_row_ptr += upper_row_pitch;
		lower_row_ptr += lower_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

/*!
	@Brief Convert Avid CbYCrY 16-bit 2.14 format to DPX 10-bit YUV 4:2:2

	@todo There seems to be a chroma reversal in the input image.
*/
void DPXConverter::ConvertCbYCrY_16bit_2_14ToDPX1(void *input_buffer, size_t input_pitch,
												  void *output_buffer, size_t output_pitch,
												  int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		// Output width must be a multiple of six
		assert((width % 6) == 0);

		int16_t *input_pixel_ptr = reinterpret_cast<int16_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		// Process six pairs of luma and chroma components per iteration
		for (int column = 0; column < width; column += 6)
		{
			int32_t Y1_signed, Cr_signed, Y2_signed, Cb_signed;
			int32_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

			uint16_t Y1, Y2, Y3, Y4, Y5, Y6;
			uint16_t U1, U2, U3, V1, V2, V3;


			/***** First tuple of CbYCrY components *****/

			// Convert Avid signed 2.14 format to 16-bit unsigned chroma
			Cb_signed = *(input_pixel_ptr++);
			Cb_unsigned = (((224 * (Cb_signed + 8192)) + (1 << 18)) >> 6);

			// Convert Avid signed 2.14 format to 16-bit unsigned luma
			Y1_signed = *(input_pixel_ptr++);
			Y1_unsigned = ((219 * Y1_signed + (1 << 18)) >> 6);

			// Convert Avid signed 2.14 format to 16-bit unsigned chroma
			Cr_signed = *(input_pixel_ptr++);
			Cr_unsigned = (((224 * (Cr_signed + 8192)) + (1 << 18)) >> 6);

			// Convert Avid signed 2.14 format to 16-bit unsigned luma
			Y2_signed = *(input_pixel_ptr++);
			Y2_unsigned = ((219 * Y2_signed + (1 << 18)) >> 6);

			// Save the tuple of CbYCrY components
			Y1 = Y1_unsigned;
			Y2 = Y2_unsigned;
			//V1 = Cr_unsigned;
			//U1 = Cb_unsigned;
			U1 = Cr_unsigned;		//*** Possible chroma reversal in the source
			V1 = Cb_unsigned;


			/***** Second tuple of CbYCrY components *****/

			// Convert Avid signed 2.14 format to 16-bit unsigned chroma
			Cb_signed = *(input_pixel_ptr++);
			Cb_unsigned = (((224 * (Cb_signed + 8192)) + (1 << 18)) >> 6);

			// Convert Avid signed 2.14 format to 16-bit unsigned luma
			Y1_signed = *(input_pixel_ptr++);
			Y1_unsigned = ((219 * Y1_signed + (1 << 18)) >> 6);

			// Convert Avid signed 2.14 format to 16-bit unsigned chroma
			Cr_signed = *(input_pixel_ptr++);
			Cr_unsigned = (((224 * (Cr_signed + 8192)) + (1 << 18)) >> 6);

			// Convert Avid signed 2.14 format to 16-bit unsigned luma
			Y2_signed = *(input_pixel_ptr++);
			Y2_unsigned = ((219 * Y2_signed + (1 << 18)) >> 6);

			// Save the tuple of CbYCrY components
			Y3 = Y1_unsigned;
			Y4 = Y2_unsigned;
			//V2 = Cr_unsigned;
			//U2 = Cb_unsigned;
			U2 = Cr_unsigned;		//*** Possible chroma reversal in the source
			V2 = Cb_unsigned;


			/***** Third tuple of CbYCrY components *****/

			// Convert Avid signed 2.14 format to 16-bit unsigned chroma
			Cb_signed = *(input_pixel_ptr++);
			Cb_unsigned = (((224 * (Cb_signed + 8192)) + (1 << 18)) >> 6);

			// Convert Avid signed 2.14 format to 16-bit unsigned luma
			Y1_signed = *(input_pixel_ptr++);
			Y1_unsigned = ((219 * Y1_signed + (1 << 18)) >> 6);

			// Convert Avid signed 2.14 format to 16-bit unsigned chroma
			Cr_signed = *(input_pixel_ptr++);
			Cr_unsigned = (((224 * (Cr_signed + 8192)) + (1 << 18)) >> 6);

			// Convert Avid signed 2.14 format to 16-bit unsigned luma
			Y2_signed = *(input_pixel_ptr++);
			Y2_unsigned = ((219 * Y2_signed + (1 << 18)) >> 6);

			// Save the tuple of CbYCrY components
			Y5 = Y1_unsigned;
			Y6 = Y2_unsigned;
			//V3 = Cr_unsigned;
			//U3 = Cb_unsigned;
			U3 = Cr_unsigned;		//*** Possible chroma reversal in the source
			V3 = Cb_unsigned;


			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(U1, Y1, V1);
			*(output_pixel_ptr++) = Pack10(Y2, U2, Y3);
			*(output_pixel_ptr++) = Pack10(V2, Y4, U3);
			*(output_pixel_ptr++) = Pack10(Y5, V3, Y6);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

#if 0
/*!
	@Brief Convert 8-bit AYCbCr to the 16-bit YUVA 4:4:4:4 DPX pixel format
*/
void DPXConverter::ConvertR408ToDPX2(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		uint8_t *input_pixel_ptr = reinterpret_cast<uint8_t *>(input_row_ptr);
		uint16_t *output_pixel_ptr = reinterpret_cast<uint16_t *>(output_row_ptr);

		// Convert each 8-bit AYCbCr tuple to 16-bit DPX CbYCrA 4:4:4:4
		for (int column = 0; column < width; column++)
		{
			uint_least16_t A = *(input_pixel_ptr++);
			uint_least16_t Y = *(input_pixel_ptr++);
			uint_least16_t Cb = *(input_pixel_ptr++);
			uint_least16_t Cr = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			A = (A << 8) + 255;
			Y = (Y + 16) << 8;
			Cb <<= 8;
			Cr <<= 8;

			// Output the 16-bit components with 16-bit precision
			*(output_pixel_ptr++) = Cb;
			*(output_pixel_ptr++) = Y;
			*(output_pixel_ptr++) = Cr;
			*(output_pixel_ptr++) = A;
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}
#endif
#if 0
/*!
	@Brief Convert 8-bit AYCbCr to the 10-bit YUVA 4:4:4:4 DPX pixel format
*/
void DPXConverter::ConvertR408ToDPX3(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		uint8_t *input_pixel_ptr = reinterpret_cast<uint8_t *>(input_row_ptr);
		uint16_t *output_pixel_ptr = reinterpret_cast<uint16_t *>(output_row_ptr);

		// Convert each 8-bit AYCbCr tuple to 16-bit DPX CbYCrA 4:4:4:4
		for (int column = 0; column < width; column += 3)
		{
			uint_least16_t A, Y, Cb, Cr;

			uint16_t Y1, Y2, Y3;
			uint16_t U1, U2, U3;
			uint16_t V1, V2, V3;
			uint16_t A1, A2, A3;


			/**** First tuple of CbYCrA components *****/

			A = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cb = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			A1 = (A << 8) + 255;
			Y1 = (Y + 16) << 8;
			U1 = (Cb << 8);
			V1 = (Cr << 8);


			/**** Second tuple of CbYCrA components *****/

			A = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cb = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			A2 = (A << 8) + 255;
			Y2 = (Y + 16) << 8;
			U2 = (Cb << 8);
			V2 = (Cr << 8);


			/**** Third tuple of CbYCrA components *****/

			A = *(input_pixel_ptr++);
			Y = *(input_pixel_ptr++);
			Cb = *(input_pixel_ptr++);
			Cr = *(input_pixel_ptr++);

			// Scale the color components to 16-bit precision
			A3 = (A << 8) + 255;
			Y3 = (Y + 16) << 8;
			U3 = (Cb << 8);
			V3 = (Cr << 8);


			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(U1, Y1, V1);
			*(output_pixel_ptr++) = Pack10(A1, U2, Y2);
			*(output_pixel_ptr++) = Pack10(V2, A2, U3);
			*(output_pixel_ptr++) = Pack10(Y3, V3, A3);
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}
#endif

/*!
	@Brief Convert NV12 to the common DPX 10-bit RGB 4:4:4 pixel format

	@todo Add code to properly convert YUV to RGB.
*/
void DPXConverter::ConvertNV12ToDPX0(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *luma_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t luma_row_pitch = input_pitch;

	uint8_t *chroma_row_ptr = luma_row_ptr + (width * height);
	size_t chroma_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	// Color conversion coefficients for 709 full range
	uint32_t ymult = 8192;			// 1.0
	uint32_t r_vmult = 12616;		// 1.540
	uint32_t g_vmult = 3760;		// 0.459
	uint32_t g_umult = 1499;		// 0.183
	uint32_t b_umult = 14877;		// 1.816

	uint32_t chroma_offset = (1 << 7);

	for (int row = 0; row < height; row++)
	{
		uint8_t *luma_pixel_ptr = reinterpret_cast<uint8_t *>(luma_row_ptr);
		uint8_t *chroma_pixel_ptr = reinterpret_cast<uint8_t *>(chroma_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		assert((width % 2) == 0);
		for (int column = 0; column < width; column += 2)
		{
#if 1
			// Remove the scale factor in the coefficients and convert to 16-bit precision
			const int shift = (13 - 8);

			int32_t Y1, Y2, U1, V1;
			int32_t R1, R2, G1, G2, B1, B2;

			// Load two luma components
			Y1 = *(luma_pixel_ptr++);
			Y2 = *(luma_pixel_ptr++);

			// Load two chroma components
			U1 = *(chroma_pixel_ptr++);
			V1 = *(chroma_pixel_ptr++);

			// Remove the chroma offset
			U1 -= chroma_offset;
			V1 -= chroma_offset;

			//TODO: Add code to correctly convert the pixels to RGB
			R1 = ymult * Y1 + r_vmult * V1;
			R2 = ymult * Y2 + r_vmult * V1;
			B1 = ymult * Y1 + b_umult * U1;
			B2 = ymult * Y2 + b_umult * U1;
			G1 = ymult * Y1 + g_umult * U1 + g_vmult * V1;
			G2 = ymult * Y2 + g_umult * U1 + g_vmult * V1;

			R1 = Saturate16u(R1 >> shift);
			G1 = Saturate16u(G1 >> shift);
			B1 = Saturate16u(B1 >> shift);
			R2 = Saturate16u(R2 >> shift);
			G2 = Saturate16u(G2 >> shift);
			B2 = Saturate16u(B2 >> shift);
#else
			uint16_t Y1, Y2, U1, V1;
			uint16_t R1, R2, G1, G2, B1, B2;

			// Load two luma components
			Y1 = *(luma_pixel_ptr++);
			Y2 = *(luma_pixel_ptr++);

			// Load two chroma components
			U1 = *(chroma_pixel_ptr++);
			V1 = *(chroma_pixel_ptr++);

			// Output only the luma channel (monochrome)
			R1 = Y1 << 8;
			R2 = Y2 << 8;
			B1 = Y1 << 8;
			B2 = Y2 << 8;
			G1 = Y1 << 8;
			G2 = Y2 << 8;
#endif
			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(R1, G1, B1);
			*(output_pixel_ptr++) = Pack10(R2, G2, B2);
		}

		luma_row_ptr += luma_row_pitch;
		output_row_ptr += output_row_pitch;

		if ((row % 2) == 1) {
			chroma_row_ptr += chroma_row_pitch;
		}
	}
}

/*!
	@Brief Convert YV12 to the common DPX 10-bit RGB 4:4:4 pixel format

	@todo Add code to properly convert YUV to RGB.
*/
void DPXConverter::ConvertYV12ToDPX0(void *input_buffer, size_t input_pitch,
									 void *output_buffer, size_t output_pitch,
									 int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *Y_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t Y_row_pitch = input_pitch;

	uint8_t *V_row_ptr = Y_row_ptr + (width * height);
	size_t V_row_pitch = input_pitch / 2;

	uint8_t *U_row_ptr = V_row_ptr + (width * height) / 4;
	size_t U_row_pitch = input_pitch / 2;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	// Color conversion coefficients for 709 full range
	uint32_t ymult = 8192;			// 1.0
	uint32_t r_vmult = 12616;		// 1.540
	uint32_t g_vmult = 3760;		// 0.459
	uint32_t g_umult = 1499;		// 0.183
	uint32_t b_umult = 14877;		// 1.816

	uint32_t chroma_offset = (1 << 7);

	for (int row = 0; row < height; row++)
	{
		uint8_t *Y_pixel_ptr = reinterpret_cast<uint8_t *>(Y_row_ptr);
		uint8_t *U_pixel_ptr = reinterpret_cast<uint8_t *>(U_row_ptr);
		uint8_t *V_pixel_ptr = reinterpret_cast<uint8_t *>(V_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		assert((width % 2) == 0);
		for (int column = 0; column < width; column += 2)
		{
#if 1
			// Remove the scale factor in the coefficients and convert to 16-bit precision
			const int shift = (13 - 8);

			int32_t Y1, Y2, U1, V1;
			int32_t R1, R2, G1, G2, B1, B2;

			// Load two luma components
			Y1 = *(Y_pixel_ptr++);
			Y2 = *(Y_pixel_ptr++);

			// Load two chroma components
			U1 = *(U_pixel_ptr++);
			V1 = *(V_pixel_ptr++);

			// Remove the chroma offset
			U1 -= chroma_offset;
			V1 -= chroma_offset;

			//TODO: Add code to correctly convert the pixels to RGB
			R1 = ymult * Y1 + r_vmult * V1;
			R2 = ymult * Y2 + r_vmult * V1;
			B1 = ymult * Y1 + b_umult * U1;
			B2 = ymult * Y2 + b_umult * U1;
			G1 = ymult * Y1 + g_umult * U1 + g_vmult * V1;
			G2 = ymult * Y2 + g_umult * U1 + g_vmult * V1;

			R1 = Saturate16u(R1 >> shift);
			G1 = Saturate16u(G1 >> shift);
			B1 = Saturate16u(B1 >> shift);
			R2 = Saturate16u(R2 >> shift);
			G2 = Saturate16u(G2 >> shift);
			B2 = Saturate16u(B2 >> shift);
#else
			uint16_t Y1, Y2, U1, V1;
			uint16_t R1, R2, G1, G2, B1, B2;

			// Load two luma components
			Y1 = *(Y_pixel_ptr++);
			Y2 = *(Y_pixel_ptr++);

			// Load two chroma components
			U1 = *(U_pixel_ptr++);
			V1 = *(V_pixel_ptr++);

			// Output only the luma channel (monochrome)
			R1 = Y1 << 8;
			R2 = Y2 << 8;
			B1 = Y1 << 8;
			B2 = Y2 << 8;
			G1 = Y1 << 8;
			G2 = Y2 << 8;
#endif
			// Pack 16-bit components into 32-bit words
			*(output_pixel_ptr++) = Pack10(R1, G1, B1);
			*(output_pixel_ptr++) = Pack10(R2, G2, B2);
		}

		Y_row_ptr += Y_row_pitch;
		output_row_ptr += output_row_pitch;

		if ((row % 2) == 1)
		{
			U_row_ptr += U_row_pitch;
			V_row_ptr += V_row_pitch;
		}
	}
}

void DPX_PixelFormat::SwapRGB10(void *input_buffer, size_t input_pitch,
								void *output_buffer, size_t output_pitch,
								int width, int height)
{
	assert(input_buffer != NULL && output_buffer != NULL);

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	size_t input_row_pitch = input_pitch;

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	size_t output_row_pitch = output_pitch;

	for (int row = 0; row < height; row++)
	{
		uint32_t *input_pixel_ptr = reinterpret_cast<uint32_t *>(input_row_ptr);
		uint32_t *output_pixel_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		// Swap each 10-bit RGB pixel
		for (int column = 0; column < width; column++)
		{
			// Byte swap the next word of 10-bit RGB components
			*(output_pixel_ptr++) = Swap32(*(input_pixel_ptr++));
		}

		input_row_ptr += input_row_pitch;
		output_row_ptr += output_row_pitch;
	}
}

/*!
	@brief Convert the common DPX 10-bit RGB format to Avid 10-bit RGB

	This routine uses in-place computation and does not perform any computation
	if the pixels do not require byte swapping.
*/
void DPX_PixelFormat::SwapRGB10(void *buffer, size_t pitch, int width, int height)
{
	if (byte_swap_flag)
	{
		assert(buffer != NULL);

		uint8_t *buffer_row_ptr = reinterpret_cast<uint8_t *>(buffer);
		size_t buffer_row_pitch = pitch;

		for (int row = 0; row < height; row++)
		{
			uint32_t *buffer_pixel_ptr = reinterpret_cast<uint32_t *>(buffer_row_ptr);

			// Swap each 10-bit RGB pixel in the row
			for (int column = 0; column < width; column++)
			{
				// Byte swap the next word of 10-bit RGB components
				buffer_pixel_ptr[column] = Swap32(buffer_pixel_ptr[column]);
			}

			buffer_row_ptr += buffer_row_pitch;
		}
	}
}
