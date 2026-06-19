/*! @file ConvertYUV8.cpp

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
#include "ConvertYUV8.h"

// Convert CbYCrY 8-bit to the NV12 format used by MPEG-2
void ConvertCbYCrY_8bitToNV12(void *input_buffer, int input_pitch,
							  void *output_buffer, int output_pitch,
							  int width, int height)
{
	uint8_t *input_row_ptr = (uint8_t *)input_buffer;
	int input_row_pitch = input_pitch;

	uint8_t *luma_row_ptr = (uint8_t *)output_buffer;
	int luma_row_pitch = output_pitch;

	// The pitch of the chroma plane must be the same as the luma plane
	int chroma_row_pitch = luma_row_pitch;

	// The interleaved Cb and Cr chroma plane follows the luma plane
	//uint8_t *chroma_row_ptr = luma_row_ptr + height * output_pitch;

	// The image width and height must be a multiple of two
	assert((width % 2) == 0 && (height % 2) == 0);

	// Convert the luma plane
	for (int row = 0; row < height; row++)
	{
		// Process two columns per iteration
		for (int column = 0; column < width; column += 2)
		{
			//uint8_t Y1, Cr, Y2, Cb;
			uint8_t Y1, Y2;

			// Get the input luma and chroma
			//Cb = input_row_ptr[2 * column + 0];
			Y1 = input_row_ptr[2 * column + 1];
			//Cr = input_row_ptr[2 * column + 2];
			Y2 = input_row_ptr[2 * column + 3];

			// Store the luma values to the luma plane
			luma_row_ptr[column + 0] = Y1;
			luma_row_ptr[column + 1] = Y2;

			// Store interleaved chroma values in the choma plane
			//chroma_row_ptr[column + 0] = Cb;
			//chroma_row_ptr[column + 1] = Cr;
		}

		input_row_ptr += input_row_pitch;
		luma_row_ptr += luma_row_pitch;
		//chroma_row_ptr += chroma_row_pitch;
	}

	// The interleaved Cb and Cr chroma plane follows the luma plane
	uint8_t *chroma_row_ptr = luma_row_ptr;

	// Convert the chroma plane
	input_row_ptr = (uint8_t *)input_buffer;
	for (int row = 0; row < height; row += 2)
	{
		// Process two columns per iteration
		for (int column = 0; column < width; column += 2)
		{
			int Cr, Cb;

			// Get the input luma and chroma
			Cb = input_row_ptr[2 * column + 0];
			//Y1 = input_row_ptr[2 * column + 1];
			Cr = input_row_ptr[2 * column + 2];
			//Y2 = input_row_ptr[2 * column + 3];

			//TODO: Enable averaging of the chroma values after debugging
			//Cb += input_row_ptr[2 * column + 0 + chroma_row_pitch];
			//Cr += input_row_ptr[2 * column + 1 + chroma_row_pitch];

			//Cb /= 2;
			//Cr /= 2;

			//TODO: Check that the chroma outputs are video safe

			// Store the luma values to the luma plane
			//luma_row_ptr[column + 0] = Y1;
			//luma_row_ptr[column + 1] = Y2;

			// Store interleaved chroma values in the choma plane
			chroma_row_ptr[column + 0] = Cb;
			chroma_row_ptr[column + 1] = Cr;
		}

		input_row_ptr += input_row_pitch * 2;
		//luma_row_ptr += luma_row_pitch;
		chroma_row_ptr += chroma_row_pitch;
	}
}



// Convert CbYCrY 8-bit to the NV12 format used by MPEG-2
void ConvertYCrYCb_8bitToNV12(void *input_buffer, int input_pitch,
							  void *output_buffer, int output_pitch,
							  int width, int height)
{
	uint8_t *input_row_ptr = (uint8_t *)input_buffer;
	int input_row_pitch = input_pitch;

	uint8_t *luma_row_ptr = (uint8_t *)output_buffer;
	int luma_row_pitch = output_pitch;

	// The pitch of the chroma plane must be the same as the luma plane
	int chroma_row_pitch = luma_row_pitch;

	// The interleaved Cb and Cr chroma plane follows the luma plane
	//uint8_t *chroma_row_ptr = luma_row_ptr + height * output_pitch;

	// The image width and height must be a multiple of two
	assert((width % 2) == 0 && (height % 2) == 0);

	// Convert the luma plane
	for (int row = 0; row < height; row++)
	{
		// Process two columns per iteration
		for (int column = 0; column < width; column += 2)
		{
			//uint8_t Y1, Cr, Y2, Cb;
			uint8_t Y1, Y2;

			// Get the input luma and chroma
			Y1 = input_row_ptr[2 * column + 0];
			Y2 = input_row_ptr[2 * column + 2];

			// Store the luma values to the luma plane
			luma_row_ptr[column + 0] = Y1;
			luma_row_ptr[column + 1] = Y2;

			// Store interleaved chroma values in the choma plane
			//chroma_row_ptr[column + 0] = Cb;
			//chroma_row_ptr[column + 1] = Cr;
		}

		input_row_ptr += input_row_pitch;
		luma_row_ptr += luma_row_pitch;
		//chroma_row_ptr += chroma_row_pitch;
	}

	// The interleaved Cb and Cr chroma plane follows the luma plane
	uint8_t *chroma_row_ptr = luma_row_ptr;

	// Convert the chroma plane
	input_row_ptr = (uint8_t *)input_buffer;
	for (int row = 0; row < height; row += 2)
	{
		// Process two columns per iteration
		for (int column = 0; column < width; column += 2)
		{
			int Cr, Cb;

			// Get the input luma and chroma
			Cb = input_row_ptr[2 * column + 1];
			Cr = input_row_ptr[2 * column + 3];

			//TODO: Enable averaging of the chroma values after debugging
			//Cb += input_row_ptr[2 * column + 0 + chroma_row_pitch];
			//Cr += input_row_ptr[2 * column + 1 + chroma_row_pitch];

			//Cb /= 2;
			//Cr /= 2;

			//TODO: Check that the chroma outputs are video safe

			// Store the luma values to the luma plane
			//luma_row_ptr[column + 0] = Y1;
			//luma_row_ptr[column + 1] = Y2;

			// Store interleaved chroma values in the choma plane
			chroma_row_ptr[column + 0] = Cb;
			chroma_row_ptr[column + 1] = Cr;
		}

		input_row_ptr += input_row_pitch * 2;
		//luma_row_ptr += luma_row_pitch;
		chroma_row_ptr += chroma_row_pitch;
	}
}
