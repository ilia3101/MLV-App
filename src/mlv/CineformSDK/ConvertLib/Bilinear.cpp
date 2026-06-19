/*! @file Bilinear.cpp

*  @brief Scaling tools
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

// Define an assert macro that can be controlled in this file
#define ASSERT(x)	assert(x)

#include "ConvertLib.h"

#if _WIN32

#if !defined(_OPENMP)
// Turn off warnings about the Open MP pragmas
#pragma warning(disable: 161 4068)
#endif

//TODO: Eliminate warnings about loss of precision in double to float conversion
#pragma warning(disable: 4244 4305)

#endif

#define SYSLOG (0)

#ifndef NEG
#define NEG(x)	(-(x))
#endif


// Enable use of threaded code for scaling
#define _THREADS	1


#if _THREADS

#include "thread.h"			// Platform independent threads API

typedef struct
{
	uint8_t *input_buffer;
	int input_width;
    int input_height;
	int input_pitch;

    uint8_t *output_buffer;
    int output_width;
    int output_height;
    int output_pitch;

	int start_row;
	int row_step;

	int reorder;		// True if BGRA should be changed to ARGB

} BilinearData;

THREAD_PROC(BilinearScaler, param)
{
    //
    //  TODO: Generalize to allow scaling +/- 800%
    //
	BilinearData *data = (BilinearData *)param;

	uint8_t *input_buffer = data->input_buffer;
	int input_width = data->input_width;
    int input_height = data->input_height;
	int input_pitch = data->input_pitch;

    uint8_t *output_buffer = data->output_buffer;
    int output_width = data->output_width;
    int output_height = data->output_height;
    int output_pitch = data->output_pitch;

	int start_row = data->start_row;
	int row_step = data->row_step;

	uint8_t *input_row_ptr = input_buffer;
	uint8_t *output_row_ptr = output_buffer;

	const int scale = 4096;

	int xscale = scale * input_width / output_width;
	int yscale = scale * input_height / output_height;
	int ystep = yscale >> 1;

	int last_row = input_height - 1;

	// Advance to the first row processed by this thread
	ystep += start_row * yscale;

	for (int row = start_row; row < output_height; row += row_step)
	{
		uint8_t *outptr = output_row_ptr;

		uint8_t *rgb_row1_ptr;
		uint8_t *rgb_row2_ptr;

		int ypos = ystep >> 12;                     // first row to look at.  Mix with the next row as found using input_pitch
		int yremainder  = (ystep & 4095) >> 5;      // Mix value to use for weighting the second row
		int yremainder2 = 128 - yremainder;         // Mix value to use for weighting the first row

		int xstep = xscale >> 1;                    // number of pixels to skip horizontally
        
        // Make sure we do not start past the end of the image
        
        if (ypos>last_row) {
            ypos = last_row;
        }

		input_row_ptr = input_buffer + ypos * input_pitch;

		rgb_row1_ptr = input_row_ptr;
		rgb_row2_ptr = (input_row_ptr + input_pitch);

		// Advance to the next input row for this thread
        //  this may advance ystep by more than one full row.
        //  To make sure we do not go over the end of the buffer, this should be checked to limit the ypos value to last_row
        
		ystep += (row_step * yscale);       

        //  Because of the logic above, ypos cannot be larger than last_row, so this should always work
        
		if (ypos == last_row) {
			rgb_row2_ptr = rgb_row1_ptr;
		}

		// Process the entire row assigned to this thread
		for (int column = 0; column < output_width; column++)
		{
			int xpos = xstep >> 12;
			int xremainder;
			int xremainder2;

			int r1, g1, b1, a1;
			int r2, g2, b2, a2;

			int r, g, b, a;

            if (xpos > (input_width-2)) {
                xpos = input_width-2;
            }
			uint8_t *rgbptr1 = rgb_row1_ptr + xpos*4;
			uint8_t *rgbptr2 = rgb_row2_ptr + xpos*4;

			xremainder = (xstep & 4095) >> 5;
			xremainder2 = 128 - xremainder;
			xstep += xscale;

			b1  = *(rgbptr1++) * xremainder2;
			g1  = *(rgbptr1++) * xremainder2;
			r1  = *(rgbptr1++) * xremainder2;
			a1  = *(rgbptr1++) * xremainder2;

			b1 += *(rgbptr1++) * xremainder;
			g1 += *(rgbptr1++) * xremainder;
			r1 += *(rgbptr1++) * xremainder;
			a1 += *(rgbptr1++) * xremainder;

			b2  = *(rgbptr2++) * xremainder2;
			g2  = *(rgbptr2++) * xremainder2;
			r2  = *(rgbptr2++) * xremainder2;
			a2  = *(rgbptr2++) * xremainder2;

			b2 += *(rgbptr2++) * xremainder;
			g2 += *(rgbptr2++) * xremainder;
			r2 += *(rgbptr2++) * xremainder;
			a2 += *(rgbptr2++) * xremainder;

			b = (b1 * yremainder2 + b2 * yremainder) >> 14;
			g = (g1 * yremainder2 + g2 * yremainder) >> 14;
			r = (r1 * yremainder2 + r2 * yremainder) >> 14;
			a = (a1 * yremainder2 + a2 * yremainder) >> 14;

			if (data->reorder)
			{
				*(outptr++) = a;
				*(outptr++) = r;
				*(outptr++) = g;
				*(outptr++) = b;
			}
			else
			{
				*(outptr++) = b;
				*(outptr++) = g;
				*(outptr++) = r;
				*(outptr++) = a;
			}
		}

		//The pitch has been adjusted to skip rows if necessary
		output_row_ptr += output_pitch;
	}

	return (THREAD_RETURN_TYPE)THREAD_ERROR_OKAY;
}

void CBilinearScalerRGB32::ScaleToBGRA(uint8_t *input_buffer,
									   int input_width,
									   int input_height,
									   int input_pitch,
									   uint8_t *output_buffer,
									   int output_width,
									   int output_height,
									   int output_pitch,
									   int flipped,
									   int reorder)
{
	//uint8_t *input_row_ptr = input_buffer;
	uint8_t *output_row_ptr = output_buffer;

	int last_row = output_height - 1;

	if (flipped)
	{
		// The output image is flipped
		output_row_ptr += (last_row * output_pitch);
		output_pitch = NEG(output_pitch);
	}

	// Each thread processes every second row
	BilinearData data[2];

	// First thread starts at the first row
	data[0].input_buffer = input_buffer;
	data[0].input_width = input_width;
	data[0].input_height = input_height;
	data[0].input_pitch = input_pitch;
	data[0].output_buffer = output_row_ptr;
	data[0].output_width = output_width;
	data[0].output_height = output_height;
	data[0].output_pitch = (2 * output_pitch);
	data[0].start_row = 0;
	data[0].row_step = 2;
	data[0].reorder = reorder;

	// Second thread starts at the second row
	data[1].input_buffer = input_buffer;
	data[1].input_width = input_width;
	data[1].input_height = input_height;
	data[1].input_pitch = input_pitch;
	data[1].output_buffer = output_row_ptr + output_pitch;
	data[1].output_width = output_width;
	data[1].output_height = output_height;
	data[1].output_pitch = (2 * output_pitch);
	data[1].start_row = 1;
	data[1].row_step = 2;
	data[1].reorder = reorder;

	// Create worker threads for processing the rows in parallel
	THREAD thread[2];
	ThreadCreate(&thread[0], BilinearScaler, (void *)&data[0]);
	ThreadCreate(&thread[1], BilinearScaler, (void *)&data[1]);

	// Wait for both threads to finish
	for (int i = 0; i < 2; i++)
	{
		ThreadWait(&thread[i]);
	}

	// Delete both threads
	for (int j = 0; j < 2; j++)
	{
		ThreadDelete(&thread[j]);
	}
}

#else

void CBilinearScalerRGB32::ScaleToBGRA(uint8_t *input_buffer,
									   int input_width,
									   int input_height,
									   int input_pitch,
									   uint8_t *output_buffer,
									   int output_width,
									   int output_height,
									   int output_pitch)
{
	uint8_t *input_row_ptr = input_buffer;
	uint8_t *output_row_ptr = output_buffer;

	//int row;
	//int column;

	const int scale = 4096;

	int xscale = scale * input_width / output_width;
	int yscale = scale * input_height / output_height;

	//uint8_t *outptr = outputBuffer;

	// Compute the offset to the start of the next row
	//long output_row_gap = output_pitch - (8 * width);

	int ystep = yscale >> 1;
	//int offset = input_pitch - 8;

	int last_row = output_height - 1;

	//int x, y;

#if 0
	float curve2pt2lin[4096];
	if(outdepth == 128)
	{
		int j;
		//#pragma omp parallel for
		for(j=0; j<4096; j++)
		{
			curve2pt2lin[j] = (float)pow((double)j/4096.0,(double)GAMMA_FACTOR);
		}
	}
#endif

	// The output image is flipped
	output_row_ptr += (last_row * output_pitch);
	output_pitch = NEG(output_pitch);

	//for (y = 0; y < height; y++)
	for (int row = 0; row < output_height; row++)
	{
		//unsigned char *srcptr;

		//unsigned short *RGBptr1;
		//unsigned short *RGBptr2;
		//unsigned short *RGBa;
		//unsigned short *RGBb;

		//uint8_t *outptr = outputBuffer;
		uint8_t *outptr = output_row_ptr;

		//int outcol = (input_width - width)/2;
		//unsigned short *outYU64 = (unsigned short *)outptr;

		// Store the scaled YUV values in the scratch buffer

		// Convert YUV to RGB and store in the output row
		//unsigned char *outRGB32 = (unsigned char *)outptr;
		//unsigned short *outRGB64 = (unsigned short *)outptr;
		//float *outRGB128 = (float *)outptr;

		uint8_t *rgb_row1_ptr;
		uint8_t *rgb_row2_ptr;

		int ypos = ystep >> 12;
		int yremainder  = (ystep & 4095) >> 5;
		int yremainder2 = 128 - yremainder;

		int xstep = xscale >> 1;

		input_row_ptr = input_buffer + ypos * input_pitch;

		rgb_row1_ptr = input_row_ptr;
		rgb_row2_ptr = (input_row_ptr + input_pitch);

		ystep += yscale;

		//srcptr = input + ypos * input_pitch;

		//RGBptr1 = (unsigned short *)srcptr;
		//RGBptr2 = (unsigned short *)(srcptr + input_pitch);
		//if (y == last_row) {
		if (row == last_row) {
			//RGBptr2 = RGBptr1;
			rgb_row2_ptr = rgb_row1_ptr;
		}

		//for (x = 0; x < width; x ++)
		for (int column = 0; column < output_width; column++)
		{
			int xpos = xstep >> 12;
			int xremainder;
			int xremainder2;

			int r1, g1, b1, a1;
			int r2, g2, b2, a2;

			int r, g, b, a;

			uint8_t *rgbptr1 = rgb_row1_ptr + xpos*4;
			uint8_t *rgbptr2 = rgb_row2_ptr + xpos*4;

			//xpos = xstep >> 12;
			xremainder = (xstep & 4095) >> 5;
			xremainder2 = 128 - xremainder;
			xstep += xscale;

			//RGBa = RGBptr1 + xpos*4;
			//RGBb = RGBptr2 + xpos*4;
#if 0
			r1 = *rgbptr1++ * xremainder2;
			g1 = *rgbptr1++ * xremainder2;
			b1 = *rgbptr1++ * xremainder2;
			a1 = *rgbptr1++ * xremainder2;
			r1 += *rgbptr1++ * xremainder;
			g1 += *rgbptr1++ * xremainder;
			b1 += *rgbptr1++ * xremainder;
			a1 += *rgbptr1++ * xremainder;

			r2 = *RGBb++ * xremainder2;
			g2 = *RGBb++ * xremainder2;
			b2 = *RGBb++ * xremainder2;
			a2 = *RGBb++ * xremainder2;
			r2 += *RGBb++ * xremainder;
			g2 += *RGBb++ * xremainder;
			b2 += *RGBb++ * xremainder;
			a2 += *RGBb++ * xremainder;
#else
			b1  = *(rgbptr1++) * xremainder2;
			g1  = *(rgbptr1++) * xremainder2;
			r1  = *(rgbptr1++) * xremainder2;
			a1  = *(rgbptr1++) * xremainder2;

			b1 += *(rgbptr1++) * xremainder;
			g1 += *(rgbptr1++) * xremainder;
			r1 += *(rgbptr1++) * xremainder;
			a1 += *(rgbptr1++) * xremainder;

			b2  = *(rgbptr2++) * xremainder2;
			g2  = *(rgbptr2++) * xremainder2;
			r2  = *(rgbptr2++) * xremainder2;
			a2  = *(rgbptr2++) * xremainder2;

			b2 += *(rgbptr2++) * xremainder;
			g2 += *(rgbptr2++) * xremainder;
			r2 += *(rgbptr2++) * xremainder;
			a2 += *(rgbptr2++) * xremainder;
#endif
			b = (b1 * yremainder2 + b2 * yremainder) >> 14;
			g = (g1 * yremainder2 + g2 * yremainder) >> 14;
			r = (r1 * yremainder2 + r2 * yremainder) >> 14;
			a = (a1 * yremainder2 + a2 * yremainder) >> 14;
#if 0
			if(outdepth == 32)
			{
				*(outRGB32++) = a>>8;
				*(outRGB32++) = r>>8;
				*(outRGB32++) = g>>8;
				*(outRGB32++) = b>>8;
			}
			else if(outdepth == 64)
			{
				*(outRGB64++) = a>>1;
				*(outRGB64++) = r>>1;
				*(outRGB64++) = g>>1;
				*(outRGB64++) = b>>1;
			}
			else
			{
				*(outRGB128++) = (float)a/65535.0;
				*(outRGB128++) = curve2pt2lin[r>>4];
				*(outRGB128++) = curve2pt2lin[g>>4];
				*(outRGB128++) = curve2pt2lin[b>>4];
			}
#else
			*(outptr++) = b;
			*(outptr++) = g;
			*(outptr++) = r;
			*(outptr++) = a;
#endif
			//outcol++;
		}

		//input_row_ptr += input_pitch;
		output_row_ptr += output_pitch;
	}
}

#endif
