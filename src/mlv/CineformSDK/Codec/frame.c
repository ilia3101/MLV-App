/*! @file frame.c

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
#include <string.h>
#include <assert.h>
#include <math.h>
#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif

#include "config.h"
#include "frame.h"
#include "wavelet.h"
#include "color.h"
#include "timing.h"
#include "convert.h"

#include "decoder.h"
#include "swap.h"
#include "RGB2YUV.h"

#include <stdlib.h>
#include <stdio.h>

#define DEBUG  (1 && _DEBUG)
#define TIMING (1 && _TIMING)
#define XMMOPT (1 && _XMMOPT)

// Performance measurements
#if TIMING
extern TIMER tk_convert;				// Time for image format conversion
extern COUNTER alloc_frame_count;		// Number of frames allocated
#endif

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif

#if _ENABLE_GAMMA_CORRECTION		// Color conversion macros
#include "gamma_table.inc"
#endif

#define COLOR_CONVERSION_16BITS		1		// Use 16-bit fixed point for color conversion
#define	INTERPOLATE_CHROMA			0		// This caused shear in multi-generation tests

#define YU16_MAX	65535					// Maximum for 16 bit pixels
#define YU10_MAX	 1023					// maximum for 10 bit pixels


#define SATURATE_10U(x) _saturate10u(x)
#define SATURATE_12U(x) _saturate12u(x)

#if 0
	#ifdef _WIN32

		#include <stdlib.h>

		// Use the byte swapping routines defined in the standard library
		#ifndef SwapInt16
			#define SwapInt16(x)	_byteswap_ushort(x)
		#endif

		#ifndef SwapInt32
			#define SwapInt32(x)	_byteswap_ulong(x)
		#endif

	#elif __APPLE__

		#include "CoreFoundation/CoreFoundation.h"

		// Use the byte swapping routines from the Core Foundation framework
		#define SwapInt16(x)	_OSSwapInt16(x)
		#define SwapInt32(x)	_OSSwapInt32(x)

	#else

		#define SwapInt32(x)	_builtin_bswap32(x)

	#endif
#else
	#include "swap.h"
#endif

//TODO: Replace uses of _bswap with SwapInt32


#if __APPLE__
#include "../Common/macdefs.h"
#else
#ifndef CopyMemory
#define CopyMemory(p,q,s)	memcpy(p,q,s)
#endif
#endif

typedef union
{
    unsigned long long u64[2];
             long long s64[2];
    unsigned int       u32[4];
             int       s32[4];
    unsigned short     u16[8];
             short     s16[8];
    unsigned char      u8[16];
             char      s8[16];
    __m128i            m128;
} m128i;


INLINE static int _saturate10u(int x)
{
	const int upper_limit = 1023;

	if (x < 0) x = 0;
	else
	if (x > upper_limit) x = upper_limit;

	return x;
}

INLINE static int _saturate12u(int x)
{
	const int upper_limit = 4095;

	if (x < 0) x = 0;
	else
	if (x > upper_limit) x = upper_limit;

	return x;
}

#if _ALLOCATOR
FRAME *CreateFrame(ALLOCATOR *allocator, int width, int height, int display_height, int format)
#else
FRAME *CreateFrame(int width, int height, int display_height, int format)
#endif
{
	int chroma_width, chroma_height;
	
#if _ALLOCATOR
	FRAME *frame = (FRAME *)Alloc(allocator, sizeof(FRAME));
#else
	FRAME *frame = (FRAME *)MEMORY_ALLOC(sizeof(FRAME));
#endif
	if (frame == NULL)
	{
#if (DEBUG && _WIN32)
		OutputDebugString("sizeof(FRAME)");
#endif
		return NULL;
	}

	// Clear all fields in the frame
	memset(frame, 0, sizeof(FRAME));

	if (format == FRAME_FORMAT_GRAY)
	{
		frame->num_channels = 1;
#if _ALLOCATOR
		frame->channel[0] = CreateImage(allocator, width, height);
#else
		frame->channel[0] = CreateImage(width, height);
#endif
	}
	else if(format == FRAME_FORMAT_YUV)
	{
		// Currently only handle color frames in YUV format
		assert(format == FRAME_FORMAT_YUV);

		frame->num_channels = 3;
#if _ALLOCATOR
		frame->channel[0] = CreateImage(allocator, width, height);
#else
		frame->channel[0] = CreateImage(width, height);
#endif
#if _YUV422
		chroma_width = width / 2;
		chroma_height = height;
#if _ALLOCATOR
		frame->channel[1] = CreateImage(allocator, chroma_width, chroma_height);
		frame->channel[2] = CreateImage(allocator, chroma_width, chroma_height);
#else
		frame->channel[1] = CreateImage(chroma_width, chroma_height);
		frame->channel[2] = CreateImage(chroma_width, chroma_height);
#endif
#else
#if _ALLOCATOR
		frame->channel[1] = CreateImage(allocator, width, height);
		frame->channel[2] = CreateImage(allocator, width, height);
#else
		frame->channel[1] = CreateImage(width, height);
		frame->channel[2] = CreateImage(width, height);
#endif
#endif
	}
	else if(format == FRAME_FORMAT_RGBA)
	{
		frame->num_channels = 4;
#if _ALLOCATOR
		frame->channel[0] = CreateImage(allocator, width, height);
		frame->channel[1] = CreateImage(allocator, width, height);
		frame->channel[2] = CreateImage(allocator, width, height);
		frame->channel[3] = CreateImage(allocator, width, height);
#else
		frame->channel[0] = CreateImage(width, height);
		frame->channel[1] = CreateImage(width, height);
		frame->channel[2] = CreateImage(width, height);
		frame->channel[3] = CreateImage(width, height);
#endif
	}
	else if(format == FRAME_FORMAT_RGB)
	{
		frame->num_channels = 3;
#if _ALLOCATOR
		frame->channel[0] = CreateImage(allocator, width, height);
		frame->channel[1] = CreateImage(allocator, width, height);
		frame->channel[2] = CreateImage(allocator, width, height);
#else
		frame->channel[0] = CreateImage(width, height);
		frame->channel[1] = CreateImage(width, height);
		frame->channel[2] = CreateImage(width, height);
#endif
	}

	// Save the frame dimensions and format
	frame->width = width;
	frame->height = height;
	frame->display_height = display_height;
	frame->format = format;

	// Assume that this is not a key frame
	frame->iskey = false;

#if TIMING
	alloc_frame_count++;
#endif

	return frame;
}

#if _ALLOCATOR
FRAME *ReallocFrame(ALLOCATOR *allocator, FRAME *frame, int width, int height, int display_height, int format)
#else
FRAME *ReallocFrame(FRAME *frame, int width, int height, int display_height, int format)
#endif
{
	if (frame != NULL)
	{
		if (frame->width == width &&
			frame->height == height &&
			frame->format == format &&
			frame->display_height == display_height) {
			return frame;
		}
#if _ALLOCATOR
		DeleteFrame(allocator, frame);
#else
		DeleteFrame(frame);
#endif
	}

#if _ALLOCATOR
	return CreateFrame(allocator, width, height, display_height, format);
#else
	return CreateFrame(width, height, display_height, format);
#endif
}

// Set the frame dimensions without allocating memory for the planes
void SetFrameDimensions(FRAME *frame, int width, int height, int display_height, int format)
{
	//int chroma_width;
	//int chroma_height;

	// Clear all fields in the frame
	memset(frame, 0, sizeof(FRAME));

	switch (format)
	{
	case FRAME_FORMAT_GRAY:
		frame->num_channels = 1;
		break;

	case FRAME_FORMAT_YUV:
		frame->num_channels = 3;
		break;

	case FRAME_FORMAT_RGBA:
		frame->num_channels = 4;
		break;

	case FRAME_FORMAT_RGB:
		frame->num_channels = 3;
		break;
	}

	// Save the frame dimensions and format
	frame->width = width;
	frame->height = height;
	frame->display_height = display_height;
	frame->format = format;

	// Assume that this is not a key frame
	frame->iskey = false;
}

// Create a frame with the same dimensions and format as another frame
#if _ALLOCATOR
FRAME *CreateFrameFromFrame(ALLOCATOR *allocator, FRAME *frame)
#else
FRAME *CreateFrameFromFrame(FRAME *frame)
#endif
{
	IMAGE *image = frame->channel[0];
	int width = image->width;
	int height = image->height;
	int display_height = frame->display_height;

	// Note: This code should be extended to duplicate the bands

#if _ALLOCATOR
	FRAME *new_frame = CreateFrame(allocator, width, height, display_height, frame->format);
#else
	FRAME *new_frame = CreateFrame(width, height, display_height, frame->format);
#endif

	return new_frame;
}

#if 0
// Create an image data structure from planar video frame data
FRAME *CreateFrameFromPlanes(ALLOCATOR *allocator, LPBYTE data, int width, int height, int pitch, int format)
{
	// To be written
	assert(0);

	return NULL;
}
#endif

void ConvertPackedToFrame(uint8_t *data, int width, int height, int pitch, FRAME *frame)
{
	IMAGE *image = frame->channel[0];
	uint8_t *rowptr = data;
	PIXEL *outptr = image->band[0];
	int data_pitch = pitch;
	int image_pitch = image->pitch/sizeof(PIXEL);
	int row, column;

	for (row = 0; row < height; row++) {
		for (column = 0; column < width; column++) {
			PIXEL value = rowptr[2 * column];
			outptr[column] = SATURATE(value);
		}
		rowptr += data_pitch;
		outptr += image_pitch;
	}
}


// Faster version of ConvertRGBToFrame8uNoIPP using MMX intrinsics
void ConvertRGB32to10bitYUVFrame(uint8_t *rgb, int pitch, FRAME *frame,  uint8_t *scratch, int scratchsize, int color_space,
								 int precision, int srcHasAlpha, int rgbaswap)
{
	ROI roi;

	int display_height,height,width;

	int shift = 6; // using 10-bit math

	assert(MIN_DECODED_COLOR_SPACE <= color_space && color_space <= MAX_DECODED_COLOR_SPACE);

	{
		PIXEL8U *RGB_row;
		unsigned short *color_plane[3];
		int color_pitch[3];
		PIXEL8U *Y_row, *U_row, *V_row;
		PIXEL *Y_row16, *U_row16, *V_row16;
		int Y_pitch, U_pitch, V_pitch;
		int row;
		int i;
		//int precisionshift = 10 - precision;
		unsigned short *scanline, *scanline2;

		// The frame format should be three channels of YUV (4:2:2 format)
		assert(frame->num_channels == 3);
		assert(frame->format == FRAME_FORMAT_YUV);
		display_height = frame->display_height;
		height = frame->height;
		width = frame->width;

		assert(scratch);
		assert(scratchsize > width * 12);

		scanline = (unsigned short *)scratch;
		scanline2 = scanline + width*3;

		// Get pointers to the image planes and set the pitch for each plane
		for (i = 0; i < 3; i++) {
			IMAGE *image = frame->channel[i];

			// Set the pointer to the individual planes and pitch for each channel
			color_plane[i] = (PIXEL16U *)image->band[0];
			color_pitch[i] = image->pitch;

			// The first channel establishes the processing dimensions
			if (i == 0) {
				roi.width = image->width;
				roi.height = image->height;
			}
		}

		// Input RGB image is upside down so reverse it
		// by starting from the end of the image and going back
		RGB_row = &rgb[0];
		RGB_row += (display_height - 1) * pitch;
		pitch = -pitch;

		//U and V are swapped
		{
			PIXEL16U *t = color_plane[1];
			color_plane[1] = color_plane[2];
			color_plane[2] = t;
		}


		Y_row = (PIXEL8U *)color_plane[0];	Y_pitch = color_pitch[0];
		U_row = (PIXEL8U *)color_plane[1];	U_pitch = color_pitch[1];
		V_row = (PIXEL8U *)color_plane[2];	V_pitch = color_pitch[2];

		for (row = 0; row < display_height; row++)
		{
			//int column = 0;

			if(srcHasAlpha)
			{
				if(rgbaswap)
					ChunkyARGB8toPlanarRGB16((unsigned char *)RGB_row, scanline, width);
				else
					ChunkyBGRA8toPlanarRGB16((unsigned char *)RGB_row, scanline, width);
			}
			else
				ChunkyBGR8toPlanarRGB16((unsigned char *)RGB_row, scanline, width);
			PlanarRGB16toPlanarYUV16(scanline, scanline2, width, color_space);
			PlanarYUV16toChannelYUYV16(scanline2, (unsigned short **)color_plane, width, color_space, shift);

			// Advance the RGB pointers
			RGB_row += pitch;

			// Advance the YUV pointers
			Y_row += Y_pitch;
			U_row += U_pitch;
			V_row += V_pitch;

			color_plane[0] = (PIXEL16U*)Y_row;
			color_plane[1] = (PIXEL16U*)U_row;
			color_plane[2] = (PIXEL16U*)V_row;
		}

		for (; row < height; row++)
		{
			int column = 0;

#if (1 && XMMOPT)
			int column_step = 16;
			int post_column = roi.width - (roi.width % column_step);

			__m128i *Y_ptr = (__m128i *)Y_row;
			__m128i *U_ptr = (__m128i *)U_row;
			__m128i *V_ptr = (__m128i *)V_row;
			__m128i Y = _mm_set1_epi16(64);
			__m128i UV = _mm_set1_epi16(512);
			// Convert to YUYV in sets of 2 pixels

			for(; column < post_column; column += column_step)
			{
				*Y_ptr++ = Y;
				*Y_ptr++ = Y;
				*U_ptr++ = UV;
				*V_ptr++ = UV;
			}

#endif
			// Process the rest of the column

			Y_row16 = (PIXEL *)Y_row;
			U_row16 = (PIXEL *)U_row;
			V_row16 = (PIXEL *)V_row;
			for(; column < roi.width; column += 2)
			{
				int Y = 64, UV = 512;

				Y_row16[column] = Y;

				U_row16[column/2] = UV;
				V_row16[column/2] = UV;
				Y_row16[column+1] = Y;
			}

			// Advance the YUV pointers
			Y_row += Y_pitch;
			U_row += U_pitch;
			V_row += V_pitch;
		}


		// Set the image parameters for each channel
		for (i = 0; i < 3; i++)
		{
			IMAGE *image = frame->channel[i];
			int band;

			// Set the image scale
			for (band = 0; band < IMAGE_NUM_BANDS; band++)
				image->scale[band] = 1;

			// Set the pixel type
			image->pixel_type[0] = PIXEL_TYPE_16S;
		}

#if _MONOCHROME
		// Continue with the gray channel only (useful for debugging)
		frame->num_channels = 1;
		frame->format = FRAME_FORMAT_GRAY;
#endif
	}
}


// Faster version of ConvertRGBToFrame8uNoIPP using MMX intrinsics
void ConvertNV12to10bitYUVFrame(uint8_t *nv12, int pitch, FRAME *frame,  uint8_t *scratch, int scratchsize, 
								int color_space, int precision, int progressive)
{
	ROI roi;

	int display_height,height,width;

	//int shift = 6; // using 10-bit math

	assert(MIN_DECODED_COLOR_SPACE <= color_space && color_space <= MAX_DECODED_COLOR_SPACE);

	{
		unsigned short *color_plane[3];
		int color_pitch[3];
		PIXEL8U *Y_row, *U_row, *V_row;
		PIXEL *Y_row16, *U_row16, *V_row16;
		int Y_pitch, U_pitch, V_pitch;
		int row;
		int i;
		//int precisionshift = 10 - precision;
		unsigned short *scanline, *scanline2;
		uint8_t *nv12Yline;
		uint8_t *nv12UVline,*nv12UVnext;

		// The frame format should be three channels of YUV (4:2:2 format)
		assert(frame->num_channels == 3);
		assert(frame->format == FRAME_FORMAT_YUV);
		display_height = frame->display_height;
		height = frame->height;
		width = frame->width;

		assert(scratch);
		assert(scratchsize > width * 12);

		scanline = (unsigned short *)scratch;
		scanline2 = scanline + width*3;

		// Get pointers to the image planes and set the pitch for each plane
		for (i = 0; i < 3; i++) {
			IMAGE *image = frame->channel[i];

			// Set the pointer to the individual planes and pitch for each channel
			color_plane[i] = (PIXEL16U *)image->band[0];
			color_pitch[i] = image->pitch;

			// The first channel establishes the processing dimensions
			if (i == 0) {
				roi.width = image->width;
				roi.height = image->height;
			}
		}

		Y_row = (PIXEL8U *)color_plane[0];	Y_pitch = color_pitch[0];
		U_row = (PIXEL8U *)color_plane[1];	U_pitch = color_pitch[1];
		V_row = (PIXEL8U *)color_plane[2];	V_pitch = color_pitch[2];


		if(progressive)
		{				
			nv12Yline   = nv12;
			nv12UVline  = nv12Yline + width*display_height;
			nv12UVnext  = nv12UVline + width;

			for (row = 0; row < display_height; row++)
			{
				int column = 0;

				Y_row16 = (PIXEL *)Y_row;
				U_row16 = (PIXEL *)U_row;
				V_row16 = (PIXEL *)V_row;

				if(row == 0 || row >= display_height-2)
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						V_row16[column/2] = nv12UVline[column]<<2; 
						U_row16[column/2] = nv12UVline[column+1]<<2; 
					}
					nv12Yline += width;
				}
				else if(row & 1) 
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						V_row16[column/2] = (nv12UVline[column]*3 + nv12UVnext[column]); 
						U_row16[column/2] = (nv12UVline[column+1]*3 + nv12UVnext[column+1]); 
					}
					nv12Yline += width;
				}
				else
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						V_row16[column/2] = (nv12UVline[column] + nv12UVnext[column]*3); 
						U_row16[column/2] = (nv12UVline[column+1] + nv12UVnext[column+1]*3); 
					}
					nv12Yline += width;
					nv12UVline = nv12UVnext;
					nv12UVnext = nv12UVline + width;
				}

				// Advance the YUV pointers
				Y_row += Y_pitch;
				U_row += U_pitch;
				V_row += V_pitch;
			}
		}
		else
		{			
			uint8_t *nv12UVline2, *nv12UVnext2;

			nv12Yline   = nv12;
			nv12UVline  = nv12Yline + width*display_height;
			nv12UVnext  = nv12UVline + width*2;

			nv12UVline2 = nv12UVline + width;
			nv12UVnext2 = nv12UVline2 + width*2;

			//Top field
			for (row = 0; row < display_height; row+=2)
			{
				int column = 0;

				Y_row16 = (PIXEL *)Y_row;
				U_row16 = (PIXEL *)U_row;
				V_row16 = (PIXEL *)V_row;

				//Top field
				if(row == 0 || row >= display_height-2)
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						V_row16[column/2] = nv12UVline[column]<<2; 
						U_row16[column/2] = nv12UVline[column+1]<<2; 
					}
					nv12Yline += width;
				}
				else if(row & 2) 
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						V_row16[column/2] = (nv12UVline[column]*5 + nv12UVnext[column]*3)>>1; 
						U_row16[column/2] = (nv12UVline[column+1]*5 + nv12UVnext[column+1]*3)>>1; 
					}
					nv12Yline += width;
				}
				else
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						V_row16[column/2] = (nv12UVline[column] + nv12UVnext[column]*7)>>1; 
						U_row16[column/2] = (nv12UVline[column+1] + nv12UVnext[column+1]*7)>>1; 
					}
					nv12Yline += width;
					nv12UVline = nv12UVnext;
					nv12UVnext = nv12UVline + width*2;
				}

				// Advance the YUV pointers
				Y_row += Y_pitch;
				U_row += U_pitch;
				V_row += V_pitch;
				Y_row16 = (PIXEL *)Y_row;
				U_row16 = (PIXEL *)U_row;
				V_row16 = (PIXEL *)V_row;



				//Bottom field
				if(row <= 2 || row >= display_height-2)
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						V_row16[column/2] = nv12UVline2[column]<<2; 
						U_row16[column/2] = nv12UVline2[column+1]<<2; 
					}
					nv12Yline += width;
				}
				else if(row & 2) 
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						V_row16[column/2] = (nv12UVline2[column] + nv12UVnext2[column]*7)>>1; 
						U_row16[column/2] = (nv12UVline2[column+1] + nv12UVnext2[column+1]*7)>>1; 
					}
					nv12Yline += width;
					nv12UVline2 = nv12UVnext2;
					nv12UVnext2 = nv12UVline2 + width*2;
				}
				else
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						V_row16[column/2] = (nv12UVline2[column]*3 + nv12UVnext2[column]*5)>>1; 
						U_row16[column/2] = (nv12UVline2[column+1]*3 + nv12UVnext2[column+1]*5)>>1; 
					}
					nv12Yline += width;
				}

				// Advance the YUV pointers
				Y_row += Y_pitch;
				U_row += U_pitch;
				V_row += V_pitch;
			}
		}

		for (; row < height; row++)
		{
			int column = 0;

			Y_row16 = (PIXEL *)Y_row;
			U_row16 = (PIXEL *)U_row;
			V_row16 = (PIXEL *)V_row;
			for(; column < roi.width; column += 2)
			{
				int Y = 64, UV = 512;

				Y_row16[column] = Y;

				U_row16[column/2] = UV;
				V_row16[column/2] = UV;
				Y_row16[column+1] = Y;
			}

			// Advance the YUV pointers
			Y_row += Y_pitch;
			U_row += U_pitch;
			V_row += V_pitch;
		}


		// Set the image parameters for each channel
		for (i = 0; i < 3; i++)
		{
			IMAGE *image = frame->channel[i];
			int band;

			// Set the image scale
			for (band = 0; band < IMAGE_NUM_BANDS; band++)
				image->scale[band] = 1;

			// Set the pixel type
			image->pixel_type[0] = PIXEL_TYPE_16S;
		}

#if _MONOCHROME
		// Continue with the gray channel only (useful for debugging)
		frame->num_channels = 1;
		frame->format = FRAME_FORMAT_GRAY;
#endif
	}
}


void ConvertYV12to10bitYUVFrame(uint8_t *nv12, int pitch, FRAME *frame,  uint8_t *scratch, int scratchsize, 
								int color_space, int precision, int progressive)
{
	ROI roi;

	int display_height,height,width;

	//int shift = 6; // using 10-bit math

	assert(MIN_DECODED_COLOR_SPACE <= color_space && color_space <= MAX_DECODED_COLOR_SPACE);

	{
		unsigned short *color_plane[3];
		int color_pitch[3];
		PIXEL8U *Y_row, *U_row, *V_row;
		PIXEL *Y_row16, *U_row16, *V_row16;
		int Y_pitch, U_pitch, V_pitch;
		int row;
		int i;
		//int precisionshift = 10 - precision;
		unsigned short *scanline, *scanline2;
		uint8_t *nv12Yline;
		uint8_t *nv12Uline,*nv12Unext;
		uint8_t *nv12Vline,*nv12Vnext;

		// The frame format should be three channels of YUV (4:2:2 format)
		assert(frame->num_channels == 3);
		assert(frame->format == FRAME_FORMAT_YUV);
		display_height = frame->display_height;
		height = frame->height;
		width = frame->width;

		assert(scratch);
		assert(scratchsize > width * 12);

		scanline = (unsigned short *)scratch;
		scanline2 = scanline + width*3;

		// Get pointers to the image planes and set the pitch for each plane
		for (i = 0; i < 3; i++) {
			IMAGE *image = frame->channel[i];

			// Set the pointer to the individual planes and pitch for each channel
			color_plane[i] = (PIXEL16U *)image->band[0];
			color_pitch[i] = image->pitch;

			// The first channel establishes the processing dimensions
			if (i == 0) {
				roi.width = image->width;
				roi.height = image->height;
			}
		}

		Y_row = (PIXEL8U *)color_plane[0];	Y_pitch = color_pitch[0];
		U_row = (PIXEL8U *)color_plane[1];	U_pitch = color_pitch[1];
		V_row = (PIXEL8U *)color_plane[2];	V_pitch = color_pitch[2];


		if(progressive)
		{				
			nv12Yline   = nv12;
			nv12Uline  =  nv12Yline + width*display_height;
			nv12Vline  =  nv12Uline + (width/2)*(display_height/2);
			nv12Unext  = nv12Uline + width/2;
			nv12Vnext  = nv12Vline + width/2;

			for (row = 0; row < display_height; row++)
			{
				int column = 0;

				Y_row16 = (PIXEL *)Y_row;
				U_row16 = (PIXEL *)U_row;
				V_row16 = (PIXEL *)V_row;

				if(row == 0 || row == display_height-1)
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						U_row16[column/2] = nv12Uline[column/2]<<2; 
						V_row16[column/2] = nv12Vline[column/2]<<2; 
					}
					nv12Yline += width;
				}
				else if(row & 1) 
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						U_row16[column/2] = (nv12Uline[column/2]*3 + nv12Unext[column/2]); 
						V_row16[column/2] = (nv12Vline[column/2]*3 + nv12Vnext[column/2]); 
					}
					nv12Yline += width;
				}
				else
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						U_row16[column/2] = (nv12Uline[column/2] + nv12Unext[column/2]*3); 
						V_row16[column/2] = (nv12Vline[column/2] + nv12Vnext[column/2]*3); 
					}
					nv12Yline += width;
					nv12Uline = nv12Unext;
					nv12Vline = nv12Vnext;
					nv12Unext = nv12Uline + width/2;
					nv12Vnext = nv12Vline + width/2;
				}

				// Advance the YUV pointers
				Y_row += Y_pitch;
				U_row += U_pitch;
				V_row += V_pitch;
			}
		}
		else
		{			
			uint8_t *nv12Uline2, *nv12Unext2;
			uint8_t *nv12Vline2, *nv12Vnext2;
			
			nv12Yline  = nv12;
			nv12Uline  = nv12Yline + width*display_height;
			nv12Vline  =  nv12Uline + (width/2)*(display_height/2);
			nv12Unext  = nv12Uline + width;
			nv12Vnext  = nv12Vline + width;

			nv12Uline2 = nv12Uline + width/2;
			nv12Unext2 = nv12Uline2 + width;
			nv12Vline2 = nv12Vline + width/2;
			nv12Vnext2 = nv12Vline2 + width;

			//Top field
			for (row = 0; row < display_height; row+=2)
			{
				int column = 0;

				Y_row16 = (PIXEL *)Y_row;
				U_row16 = (PIXEL *)U_row;
				V_row16 = (PIXEL *)V_row;

				//Top field
				if(row == 0)
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						U_row16[column/2] = nv12Uline[column/2]<<2; 
						V_row16[column/2] = nv12Vline[column/2]<<2; 
					}
					nv12Yline += width;
				}
				else if(row & 2) 
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						U_row16[column/2] = (nv12Uline[column/2]*5 + nv12Unext[column/2]*3)>>1; 
						V_row16[column/2] = (nv12Vline[column/2]*5 + nv12Vnext[column/2]*3)>>1; 
					}
					nv12Yline += width;
				}
				else
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						U_row16[column/2] = (nv12Uline[column/2] + nv12Unext[column/2]*7)>>1; 
						V_row16[column/2] = (nv12Vline[column/2] + nv12Vnext[column/2]*7)>>1; 
					}
					nv12Yline += width;
					nv12Uline = nv12Unext;
					nv12Vline = nv12Vnext;
					nv12Unext = nv12Uline + width;
					nv12Vnext = nv12Vline + width;
				}

				// Advance the YUV pointers
				Y_row += Y_pitch;
				U_row += U_pitch;
				V_row += V_pitch;
				Y_row16 = (PIXEL *)Y_row;
				U_row16 = (PIXEL *)U_row;
				V_row16 = (PIXEL *)V_row;



				//Bottom field
				if(row <= 2 || row >= display_height-2)
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						U_row16[column/2] = nv12Uline2[column/2]<<2; 
						V_row16[column/2] = nv12Vline2[column/2]<<2; 
					}
					nv12Yline += width;
				}
				else if(row & 2) 
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						U_row16[column/2] = (nv12Uline2[column/2] + nv12Unext2[column/2]*7)>>1; 
						V_row16[column/2] = (nv12Vline2[column/2] + nv12Vnext2[column/2]*7)>>1; 
					}
					nv12Yline += width;
					nv12Uline2 = nv12Unext2;
					nv12Vline2 = nv12Vnext2;
					nv12Unext2 = nv12Uline2 + width;
					nv12Vnext2 = nv12Vline2 + width;
				}
				else
				{
					for(column = 0; column < roi.width; column += 2)
					{
						Y_row16[column] = nv12Yline[column]<<2;
						Y_row16[column+1] = nv12Yline[column+1]<<2;
						U_row16[column/2] = (nv12Uline2[column/2]*3 + nv12Unext2[column/2]*5)>>1; 
						V_row16[column/2] = (nv12Vline2[column/2]*3 + nv12Vnext2[column/2]*5)>>1; 
					}
					nv12Yline += width;
				}

				// Advance the YUV pointers
				Y_row += Y_pitch;
				U_row += U_pitch;
				V_row += V_pitch;
			}
		}

		for (; row < height; row++)
		{
			int column = 0;

			Y_row16 = (PIXEL *)Y_row;
			U_row16 = (PIXEL *)U_row;
			V_row16 = (PIXEL *)V_row;
			for(; column < roi.width; column += 2)
			{
				int Y = 64, UV = 512;

				Y_row16[column] = Y;

				U_row16[column/2] = UV;
				V_row16[column/2] = UV;
				Y_row16[column+1] = Y;
			}

			// Advance the YUV pointers
			Y_row += Y_pitch;
			U_row += U_pitch;
			V_row += V_pitch;
		}


		// Set the image parameters for each channel
		for (i = 0; i < 3; i++)
		{
			IMAGE *image = frame->channel[i];
			int band;

			// Set the image scale
			for (band = 0; band < IMAGE_NUM_BANDS; band++)
				image->scale[band] = 1;

			// Set the pixel type
			image->pixel_type[0] = PIXEL_TYPE_16S;
		}

#if _MONOCHROME
		// Continue with the gray channel only (useful for debugging)
		frame->num_channels = 1;
		frame->format = FRAME_FORMAT_GRAY;
#endif
	}
}


void ConvertYUYVToFrame16s(uint8_t *yuv, int pitch, FRAME *frame, uint8_t *buffer)
{
	IMAGE *y_image;
	IMAGE *u_image;
	IMAGE *v_image;
	PIXEL8U *yuyv_row_ptr;
	PIXEL16S *y_row_ptr;
	PIXEL16S *u_row_ptr;
	PIXEL16S *v_row_ptr;
	int yuyv_pitch;
	int y_pitch;
	int u_pitch;
	int v_pitch;
	int width;
	int height;
	int row;
	int column;
	int i;
	int display_height;

	// Process sixteen luma values per loop iteration
	const int column_step = 16;

	// Column at which post processing must begin
	int post_column;

	if (frame == NULL) return;

	// The frame format should be three channels of YUV (4:2:2 format)
	assert(frame->num_channels == 3);
	assert(frame->format == FRAME_FORMAT_YUV);

	y_image = frame->channel[0];					// Get the individual planes
	u_image = frame->channel[1];
	v_image = frame->channel[2];

	yuyv_row_ptr = yuv;								// Pointers to the rows
	y_row_ptr = (PIXEL16S *)(y_image->band[0]);
	u_row_ptr = (PIXEL16S *)(u_image->band[0]);
	v_row_ptr = (PIXEL16S *)(v_image->band[0]);

	yuyv_pitch = pitch/sizeof(PIXEL8U);				// Convert pitch from bytes to pixels
	y_pitch = y_image->pitch/sizeof(PIXEL16S);
	u_pitch = u_image->pitch/sizeof(PIXEL16S);
	v_pitch = v_image->pitch/sizeof(PIXEL16S);

	width = y_image->width;							// Dimensions of the luma image
	height = y_image->height;
	display_height = frame->display_height;

	post_column = width - (width % column_step);

	// The output pitch should be a positive number (no image inversion)
	assert(yuyv_pitch > 0);

	for (row = 0; row < display_height; row++)
	{
#if MMXSUPPORTED //TODO DANREMOVE
#if 1
		__m128i *yuyv_ptr = (__m128i *)yuyv_row_ptr;
		__m128i *yyyy_ptr = (__m128i *)y_row_ptr;
		__m128i *uuuu_ptr = (__m128i *)u_row_ptr;
		__m128i *vvvv_ptr = (__m128i *)v_row_ptr;
#endif
#endif
		// Begin processing at the leftmost column
		column = 0;

#if MMXSUPPORTED //TODO DANREMOVE
#if (1 && XMMOPT)

		for (; column < post_column; column += column_step)
		{
			__m128i yuyv1_epu8;		// Interleaved bytes of luma and chroma
			__m128i yuyv2_epu8;		// Interleaved bytes of luma and chroma
			__m128i yyyy1_epu16;
			__m128i yyyy2_epu16;
			__m128i yyyy1_epu8;
			__m128i yyyy2_epu8;
			__m128i vvvv_epu8;
			__m128i uuuu_epu8;
			__m128i vvvv1_epi32;
			__m128i vvvv2_epi32;
			__m128i uuuu1_epi32;
			__m128i uuuu2_epi32;
			__m128i vvvv1_epi16;
			__m128i vvvv2_epi16;
			__m128i uuuu1_epi16;
			__m128i uuuu2_epi16;
#if 0
			__m128i black_epi16 = _mm_set1_epi16(COLOR_CHROMA_ZERO);
#endif
			// Load sixteen bytes of packed luma and chroma
			yuyv1_epu8 = _mm_load_si128(yuyv_ptr++);

			// Zero the chroma bytes
			//yyyy1_epu16 = _mm_slli_epi16(yuyv1_epu8, 8);
			//yyyy1_epu16 = _mm_srli_epi16(yyyy1_epu16, 8);
			yyyy1_epu16 = _mm_and_si128(yuyv1_epu8, _mm_set1_epi16(0x00FF));

			// Load sixteen bytes of packed luma and chroma
			yuyv2_epu8 = _mm_load_si128(yuyv_ptr++);

			// Store eight words of luma
			_mm_store_si128(yyyy_ptr++, yyyy1_epu16);

			// Zero the chroma bytes
			//yyyy2_epu16 = _mm_slli_epi16(yuyv2_epu8, 8);
			//yyyy2_epu16 = _mm_srli_epi16(yyyy2_epu16, 8);
			yyyy2_epu16 = _mm_and_si128(yuyv2_epu8, _mm_set1_epi16(0x00FF));

			// Pack eight bytes of luma and store
			//yyyy1_epu8 = _mm_packs_epu16(yyyy1_epu16, yyyy2_epu16);
			//*(yyyy_eptr++) = yyyy1_epu8;

			// Store eight words of luma
			_mm_store_si128(yyyy_ptr++, yyyy2_epu16);

			// Zero the luma in each of the yuyv registers
			//uvuv1_epi16 = _mm_srli_epi16(yuyv1_epu8, 8);
			//uvuv2_epi16 = _mm_srli_epi16(yuyv2_epu8, 8);

			// Zero the luma and u chroma
			//vvvv1_epi32 = _mm_slli_epi32(uvuv1_epi16, 24);
			//vvvv1_epi32 = _mm_srli_epi32(vvvv1_epi32, 24);
			vvvv1_epi32 = _mm_and_si128(yuyv1_epu8, _mm_set1_epi32(0x0000FF00));
			vvvv1_epi32 = _mm_srli_si128(vvvv1_epi32, 1);

			//vvvv2_epi32 = _mm_slli_epi32(uvuv2_epi16, 24);
			//vvvv2_epi32 = _mm_srli_epi32(vvvv2_epi32, 24);
			vvvv2_epi32 = _mm_and_si128(yuyv2_epu8, _mm_set1_epi32(0x0000FF00));
			vvvv2_epi32 = _mm_srli_si128(vvvv2_epi32, 1);

			// Pack eight words of v chroma
			vvvv1_epi16 = _mm_packs_epi32(vvvv1_epi32, vvvv2_epi32);

			// Store eight words of v chroma
			_mm_store_si128(vvvv_ptr++, vvvv1_epi16);

			// Zero the luma and v chroma
			//uuuu1_epi32 = _mm_srli_epi32(uvuv1_epi16, 16);
			//uuuu2_epi32 = _mm_srli_epi32(uvuv2_epi16, 16);
			uuuu1_epi32 = _mm_and_si128(yuyv1_epu8, _mm_set1_epi32(0xFF000000));
			uuuu1_epi32 = _mm_srli_si128(uuuu1_epi32, 3);

			uuuu2_epi32 = _mm_and_si128(yuyv2_epu8, _mm_set1_epi32(0xFF000000));
			uuuu2_epi32 = _mm_srli_si128(uuuu2_epi32, 3);

			// Pack eight words of u chroma
			uuuu1_epi16 = _mm_packs_epi32(uuuu1_epi32, uuuu2_epi32);

			// Store eight words of u chroma
			_mm_store_si128(uuuu_ptr++, uuuu1_epi16);
		}

		// Should have exited the loop at the post processing column
		assert(column == post_column);

#endif
#endif

		// Process the rest of the column
		for (; column < width; column += 2)
		{
			int index = 2 * column;
			int c0 = column;
			int c1 = column + 1;
			int c2 = column/2;

			// Unpack two luminance values and two chroma (which are reversed)
			PIXEL8U y1 = yuyv_row_ptr[index++];
			PIXEL8U v  = yuyv_row_ptr[index++];
			PIXEL8U y2 = yuyv_row_ptr[index++];
			PIXEL8U u  = yuyv_row_ptr[index++];

			// Output the luminance and chrominance values to separate planes
			y_row_ptr[c0] = y1;
			y_row_ptr[c1] = y2;
			u_row_ptr[c2] = u;
			v_row_ptr[c2] = v;
		}

		// Should have exited the loop just after the last column
		assert(column == width);

		// Advance to the next rows in the input and output images
		yuyv_row_ptr += yuyv_pitch;
		y_row_ptr += y_pitch;
		u_row_ptr += u_pitch;
		v_row_ptr += v_pitch;
	}

#if MMXSUPPORTED //TODO DANREMOVE
	//_mm_empty();		// Clear the mmx register state
#endif

	// Set the image parameters for each channel
	for (i = 0; i < 3; i++)
	{
		IMAGE *image = frame->channel[i];
		int band;

		// Set the image scale
		for (band = 0; band < IMAGE_NUM_BANDS; band++)
			image->scale[band] = 1;

		// Set the pixel type
		image->pixel_type[0] = PIXEL_TYPE_16S;
	}
}


//#if BUILD_PROSPECT
// Convert the packed 10-bit YUV 4:2:2 to planes of 8-bit YUV
#if DANREMOVE 
void ConvertV210ToFrame8u(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer)
{
	IMAGE *y_image;
	IMAGE *u_image;
	IMAGE *v_image;
	uint32_t *v210_row_ptr;
	PIXEL8U *y_row_ptr;
	PIXEL8U *u_row_ptr;
	PIXEL8U *v_row_ptr;
	int v210_pitch;
	int y_pitch;
	int u_pitch;
	int v_pitch;
	int width;
	int height;
	int row;
	int i;
	int display_height;

	// Process 16 bytes each of luma and chroma per loop iteration
	//const int column_step = 2 * sizeof(__m64);

	// Column at which post processing must begin
	//int post_column;

	if (frame == NULL) return;

	// The frame format should be three channels of YUV (4:2:2 format)
	assert(frame->num_channels == 3);
	assert(frame->format == FRAME_FORMAT_YUV);

	y_image = frame->channel[0];					// Get the individual planes
	u_image = frame->channel[1];
	v_image = frame->channel[2];

	v210_row_ptr = (uint32_t *)data;					// Pointers to the rows
	y_row_ptr = (PIXEL8U *)(y_image->band[0]);
	u_row_ptr = (PIXEL8U *)(u_image->band[0]);
	v_row_ptr = (PIXEL8U *)(v_image->band[0]);

	v210_pitch = pitch/sizeof(uint32_t);				// Convert pitch from bytes to pixels
	y_pitch = y_image->pitch/sizeof(PIXEL8U);
	u_pitch = u_image->pitch/sizeof(PIXEL8U);
	v_pitch = v_image->pitch/sizeof(PIXEL8U);

	width = y_image->width;							// Dimensions of the luma image
	height = y_image->height;
	display_height = frame->display_height;

	//post_column = width - (width % column_step);

	// The output pitch should be a positive number (no image inversion)
	assert(v210_pitch > 0);

	for (row = 0; row < display_height; row++)
	{
#if 0
		// Start processing the row at the first column
		int column = 0;

#if (0 && XMMOPT)

		/***** Add code for the fast loop here *****/


		assert(column == post_column);
#endif

		// Process the rest of the row
		for (; column < width; column += 2)
		{
		}

		// Should have exited the loop just after the last column
		assert(column == width);
#else
		// Unpack the row of 10-bit pixels
		ConvertV210RowToYUV((uint8_t *)v210_row_ptr, (PIXEL *)buffer, width);

		// Convert the unpacked pixels to 8-bit planes
		ConvertYUVPacked16sRowToPlanar8u((PIXEL *)buffer, width, y_row_ptr, u_row_ptr, v_row_ptr);
#endif
		// Advance to the next rows in the input and output images
		v210_row_ptr += v210_pitch;
		y_row_ptr += y_pitch;
		u_row_ptr += u_pitch;
		v_row_ptr += v_pitch;
	}

	//_mm_empty();		// Clear the mmx register state

	// Set the image parameters for each channel
	for (i = 0; i < 3; i++)
	{
		IMAGE *image = frame->channel[i];
		int band;

		// Set the image scale
		for (band = 0; band < IMAGE_NUM_BANDS; band++)
			image->scale[band] = 1;

		// Set the pixel type
		image->pixel_type[0] = PIXEL_TYPE_8U;
	}
}
#endif
//#endif


//#if BUILD_PROSPECT
// Convert the packed 10-bit YUV 4:2:2 to planes of 16-bit YUV
void ConvertV210ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer)
{
	IMAGE *y_image;
	IMAGE *u_image;
	IMAGE *v_image;
	uint32_t *v210_row_ptr;
	PIXEL *y_row_ptr;
	PIXEL *u_row_ptr;
	PIXEL *v_row_ptr;
	int v210_pitch;
	int y_pitch;
	int u_pitch;
	int v_pitch;
	int width;
	int height;
	int row;
	int i;
	int display_height;

	// Process 16 bytes each of luma and chroma per loop iteration
	//const int column_step = 2 * sizeof(__m64);

	// Column at which post processing must begin
	//int post_column;

	if (frame == NULL) return;

	// The frame format should be three channels of YUV (4:2:2 format)
	assert(frame->num_channels == 3);
	assert(frame->format == FRAME_FORMAT_YUV);

	y_image = frame->channel[0];					// Get the individual planes
	u_image = frame->channel[1];
	v_image = frame->channel[2];

	v210_row_ptr = (uint32_t *)data;					// Pointers to the rows
	y_row_ptr = y_image->band[0];
	u_row_ptr = u_image->band[0];
	v_row_ptr = v_image->band[0];

	v210_pitch = pitch/sizeof(uint32_t);				// Convert pitch from bytes to pixels
	y_pitch = y_image->pitch/sizeof(PIXEL16S);
	u_pitch = u_image->pitch/sizeof(PIXEL16S);
	v_pitch = v_image->pitch/sizeof(PIXEL16S);

	width = y_image->width;							// Dimensions of the luma image
	height = y_image->height;
	display_height = frame->display_height;

	//post_column = width - (width % column_step);

	// The output pitch should be a positive number (no image inversion)
	assert(v210_pitch > 0);

	for (row = 0; row < display_height; row++)
	{
#if 0
		// Start processing the row at the first column
		int column = 0;

#if (0 && XMMOPT)

		/***** Add code for the fast loop here *****/


		assert(column == post_column);
#endif

		// Process the rest of the row
		for (; column < width; column += 2)
		{
		}

		// Should have exited the loop just after the last column
		assert(column == width);
#elif 0
		// Unpack the row of 10-bit pixels
		ConvertV210RowToYUV((uint8_t *)v210_row_ptr, (PIXEL *)buffer, width);

		// Convert the unpacked pixels to 16-bit planes
		ConvertYUVPacked16sRowToPlanar16s((PIXEL *)buffer, width, y_row_ptr, u_row_ptr, v_row_ptr);
#else
		// Does the input row have the required alignment for fast unpacking?
		if (ISALIGNED16(v210_row_ptr))
		{
			// Unpack the row of 10-bit pixels to 16-bit planes
			ConvertV210RowToPlanar16s((uint8_t *)v210_row_ptr, width, y_row_ptr, u_row_ptr, v_row_ptr);
		}
		else
		{
			// Check that the buffer is properly aligned
			assert(ISALIGNED16(buffer));

			// Copy the row into aligned memory
			CopyMemory(buffer, v210_row_ptr, pitch);

			// Unpack the row of 10-bit pixels to 16-bit planes
			ConvertV210RowToPlanar16s(buffer, width, y_row_ptr, u_row_ptr, v_row_ptr);
		}
#endif
		// Advance to the next rows in the input and output images
		v210_row_ptr += v210_pitch;
		y_row_ptr += y_pitch;
		u_row_ptr += u_pitch;
		v_row_ptr += v_pitch;
	}

	// Set the image parameters for each channel
	for (i = 0; i < 3; i++)
	{
		IMAGE *image = frame->channel[i];
		int band;

		// Set the image scale
		for (band = 0; band < IMAGE_NUM_BANDS; band++)
			image->scale[band] = 1;

		// Set the pixel type
		image->pixel_type[0] = PIXEL_TYPE_16S;
	}
}
//#endif

//#if BUILD_PROSPECT
// Convert the unpacked 16-bit YUV 4:2:2 to planes of 16-bit YUV
void ConvertYU64ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer)
{
	IMAGE *y_image;
	IMAGE *u_image;
	IMAGE *v_image;
	PIXEL *y_row_ptr;
	PIXEL *u_row_ptr;
	PIXEL *v_row_ptr;
	int yu64_pitch;
	int y_pitch;
	int u_pitch;
	int v_pitch;
	int width;
	int height;
	int rowp;
	int i;
	int display_height;

	// Process 16 bytes each of luma and chroma per loop iteration
	//const int column_step = 2 * sizeof(__m64);

	// Column at which post processing must begin
	//int post_column;

	if (frame == NULL) return;

	// The frame format should be three channels of YUV (4:2:2 format)
	assert(frame->num_channels == 3);
	assert(frame->format == FRAME_FORMAT_YUV);

	y_image = frame->channel[0];					// Get the individual planes
	u_image = frame->channel[1];
	v_image = frame->channel[2];

	y_row_ptr = y_image->band[0];
	u_row_ptr = u_image->band[0];
	v_row_ptr = v_image->band[0];

	yu64_pitch = pitch/sizeof(uint32_t);				// Convert pitch from bytes to pixels
	y_pitch = y_image->pitch/sizeof(PIXEL16S);
	u_pitch = u_image->pitch/sizeof(PIXEL16S);
	v_pitch = v_image->pitch/sizeof(PIXEL16S);

	width = y_image->width;							// Dimensions of the luma image
	height = y_image->height;
	display_height = frame->display_height;

	//post_column = width - (width % column_step);

	// The output pitch should be a positive number (no image inversion)
	assert(yu64_pitch > 0);

	for (rowp = 0; rowp < height/*display_height*/; rowp++) //DAN20090215 File the frame with edge to prevent ringing artifacts.
	{
		int row = rowp < display_height ? rowp : display_height-1;
		uint32_t *yu64_row_ptr = (uint32_t *)data;	// Pointers to the rows

		yu64_row_ptr += yu64_pitch * row;

		// Unpack the row of 10-bit pixels
		ConvertYU64RowToYUV10bit((uint8_t *)yu64_row_ptr, (PIXEL *)buffer, width);

		// Convert the unpacked pixels to 16-bit planes
		ConvertYUVPacked16sRowToPlanar16s((PIXEL *)buffer, width, y_row_ptr, u_row_ptr, v_row_ptr);

		// Advance to the next rows in the input and output images
		y_row_ptr += y_pitch;
		u_row_ptr += u_pitch;
		v_row_ptr += v_pitch;
	}
	
	// Set the image parameters for each channel
	for (i = 0; i < 3; i++)
	{
		IMAGE *image = frame->channel[i];
		int band;

		// Set the image scale
		for (band = 0; band < IMAGE_NUM_BANDS; band++)
			image->scale[band] = 1;

		// Set the pixel type
		image->pixel_type[0] = PIXEL_TYPE_16S;
	}
}
//#endif


// Convert the packed 8-bit BAYER RGB to planes of 16-bit RGBA
void ConvertBYR1ToFrame16s(int bayer_format, uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer)
{
	IMAGE *g_image;
	IMAGE *rg_diff_image;
	IMAGE *bg_diff_image;
	IMAGE *gdiff_image;
	uint8_t *byr1_row_ptr;
	PIXEL *g_row_ptr;
	PIXEL *gdiff_row_ptr;
	PIXEL *rg_row_ptr;
	PIXEL *bg_row_ptr;
	int byr1_pitch;
	int width;
	int height;
	int row;
	int i,x;
	int display_height;

	// Process 16 bytes each of luma and chroma per loop iteration
	//const int column_step = 2 * sizeof(__m64);

	// Column at which post processing must begin
	//int post_column;

	if (frame == NULL) return;

	// The frame format should be four channels of RGBA
	assert(frame->num_channels == 4);
	assert(frame->format == FRAME_FORMAT_RGBA);

	g_image = frame->channel[0];					// Get the individual planes
	rg_diff_image = frame->channel[1];
	bg_diff_image = frame->channel[2];
	gdiff_image = frame->channel[3];

	byr1_row_ptr = (uint8_t *)data;					// Pointers to the rows
	g_row_ptr = g_image->band[0];
	rg_row_ptr = rg_diff_image->band[0];
	bg_row_ptr = bg_diff_image->band[0];
	gdiff_row_ptr = gdiff_image->band[0];

	byr1_pitch = g_image->pitch/sizeof(PIXEL16S);

	width = g_image->width;							// Dimensions of the luma image
	height = g_image->height;
	display_height = frame->display_height;

	//post_column = width - (width % column_step);

	// The output pitch should be a positive number (no image inversion)
	assert(byr1_pitch > 0);

	for (row = 0; row < display_height; row++)
	{
		uint8_t *line1 = &byr1_row_ptr[row * pitch];
		uint8_t *line2 = line1 + (pitch>>1);

		__m128i *line1ptr_epi16 = (__m128i *)line1;
		__m128i *line2ptr_epi16 = (__m128i *)line2;
		__m128i *gptr_epi16 = (__m128i *)g_row_ptr;
		__m128i *gdiffptr_epi16 = (__m128i *)gdiff_row_ptr;
		__m128i *rgptr_epi16 = (__m128i *)rg_row_ptr;
		__m128i *bgptr_epi16 = (__m128i *)bg_row_ptr;


		__m128i row_epi16;
		__m128i row1a_epi16;
		__m128i row2a_epi16;
		__m128i row1b_epi16;
		__m128i row2b_epi16;

		__m128i g1_epi16;
		__m128i g2_epi16;
		__m128i r_epi16;
		__m128i b_epi16;

		__m128i temp_epi16;
		__m128i g_epi16;
		__m128i gdiff_epi16;
		__m128i rg_epi16;
		__m128i bg_epi16;

		const __m128i rounding_epi16 = _mm_set1_epi16(512);
		const __m128i rounding256_epi16 = _mm_set1_epi16(256);
		const __m128i zero_epi16 = _mm_set1_epi16(0);
		const __m128i one_epi16 = _mm_set1_epi16(1);

		switch(bayer_format)
		{
			case BAYER_FORMAT_RED_GRN:
				for(x=0; x<width; x+=8)
				{
					// Read the first group of 16 8-bit packed pixels
					row_epi16 = _mm_load_si128(line1ptr_epi16++);
					row1a_epi16 = _mm_unpacklo_epi8(row_epi16,zero_epi16);

					// Read the first group of 8 16-bit packed 12-bit pixels
					//row1a_epi16 = _mm_load_si128(line1ptr_epi16++);
					//g1 and r  r0 g0 r1 g1 r2 g2 r3 g3
					row1a_epi16 = _mm_shufflehi_epi16(row1a_epi16,_MM_SHUFFLE(2,0,3,1));
					//g0 g1 r0 r1 r2 g2 r3 g3  _mm_shufflehi_epi16
					row1a_epi16 = _mm_shufflelo_epi16(row1a_epi16,_MM_SHUFFLE(2,0,3,1));
					//g0 g1 r0 r1 g2 g3 r2 r3  _mm_shufflelo_epi16
					row1a_epi16 = _mm_shuffle_epi32(row1a_epi16,_MM_SHUFFLE(3,1,2,0));
					//g0g1 g2g3 r0r1 r2r3  _mm_shuffle_epi32

					row1b_epi16 = _mm_unpackhi_epi8(row_epi16,zero_epi16);
					//row1b_epi16 = _mm_load_si128(line1ptr_epi16++);
					//g1 and r  r4 g4 r5 g5 r6 g6 r7 g7
					row1b_epi16 = _mm_shufflehi_epi16(row1b_epi16,_MM_SHUFFLE(2,0,3,1));
					//g4 g5 r4 r5 r6 g6 r7 g7  _mm_shufflehi_epi16
					row1b_epi16 = _mm_shufflelo_epi16(row1b_epi16,_MM_SHUFFLE(2,0,3,1));
					//g4 g5 r4 r5 g6 g7 r6 r7  _mm_shufflelo_epi16
					row1b_epi16 = _mm_shuffle_epi32(row1b_epi16,_MM_SHUFFLE(3,1,2,0));
					//g4g5 g6g7 r4r5 r6r7  _mm_shuffle_epi32


					r_epi16 = 	_mm_unpackhi_epi32(row1a_epi16, row1b_epi16);
					//g0g1 g4g5 g2g3 g6g7  _mm_unpackhi_epi32
					r_epi16 = 	_mm_shuffle_epi32(r_epi16, _MM_SHUFFLE(3,1,2,0));
					//g0g1 g2g3 g4g5 g6g7  _mm_shuffle_epi32

					g1_epi16 = 	_mm_unpacklo_epi32(row1a_epi16, row1b_epi16);
					//r0r1 r4r5 r2r3 r6r7  _mm_unpacklo_epi32
					g1_epi16 = 	_mm_shuffle_epi32(g1_epi16, _MM_SHUFFLE(3,1,2,0));
					//r0r1 r2r3 r4r5 r6r7  _mm_shuffle_epi32




					// Read the first group of 16 8-bit packed pixels
					row_epi16 = _mm_load_si128(line2ptr_epi16++);
					row2a_epi16 = _mm_unpacklo_epi8(row_epi16,zero_epi16);

					//row2a_epi16 = _mm_load_si128(line2ptr_epi16++);  //b and g2  g b g b g b g b
					row2a_epi16 = _mm_shufflehi_epi16(row2a_epi16,_MM_SHUFFLE(2,0,3,1));
					row2a_epi16 = _mm_shufflelo_epi16(row2a_epi16,_MM_SHUFFLE(2,0,3,1));
					row2a_epi16 = _mm_shuffle_epi32(row2a_epi16,_MM_SHUFFLE(3,1,2,0));
					//b0b1 b2b3 g0g1 g2g3  _mm_shuffle_epi32

					row2b_epi16 = _mm_unpackhi_epi8(row_epi16,zero_epi16);
					//row2b_epi16 = _mm_load_si128(line2ptr_epi16++);  //b and g2  g b g b g b g b
					row2b_epi16 = _mm_shufflehi_epi16(row2b_epi16,_MM_SHUFFLE(2,0,3,1));
					row2b_epi16 = _mm_shufflelo_epi16(row2b_epi16,_MM_SHUFFLE(2,0,3,1));
					row2b_epi16 = _mm_shuffle_epi32(row2b_epi16,_MM_SHUFFLE(3,1,2,0));
					//b4b5 b6b7 g4g5 g6g7  _mm_shuffle_epi32


					g2_epi16 = 	_mm_unpackhi_epi32(row2a_epi16, row2b_epi16);
					//b0b1 b4b5 b2b3 b6b7  _mm_unpackhi_epi32
					g2_epi16 = 	_mm_shuffle_epi32(g2_epi16, _MM_SHUFFLE(3,1,2,0));
					//b0b1 b2b3 b4b5 b6b7  _mm_shuffle_epi32


					b_epi16 = 	_mm_unpacklo_epi32(row2a_epi16, row2b_epi16);
					//g0g1 g4g5 g2g3 g6g7  _mm_unpacklo_epi32
					b_epi16 = 	_mm_shuffle_epi32(b_epi16, _MM_SHUFFLE(3,1,2,0));
					//g0g1 g2g3 g4g5 g6g7  _mm_shuffle_epi32


					//*g_row_ptr++ = (g<<1)+1;
					g_epi16 = _mm_adds_epi16(g1_epi16,g2_epi16);
					temp_epi16 = _mm_slli_epi16(g_epi16, 1);
					temp_epi16 = _mm_adds_epi16(temp_epi16,one_epi16);
					_mm_store_si128(gptr_epi16++, temp_epi16);

					//*rg_row_ptr++ = (r<<1)-g+512;
					rg_epi16 = _mm_slli_epi16(r_epi16, 1);
					rg_epi16 = _mm_subs_epi16(rg_epi16, g_epi16);
					rg_epi16 = _mm_adds_epi16(rg_epi16,rounding_epi16);
					_mm_store_si128(rgptr_epi16++, rg_epi16);

					//*bg_row_ptr++ = (b<<1)-g+512;
					bg_epi16 = _mm_slli_epi16(b_epi16, 1);
					bg_epi16 = _mm_subs_epi16(bg_epi16, g_epi16);
					bg_epi16 = _mm_adds_epi16(bg_epi16,rounding_epi16);
					_mm_store_si128(bgptr_epi16++, bg_epi16);

					//*gdiff_row_ptr++ = (g1-g2+256)<<1;
					gdiff_epi16 = _mm_subs_epi16(g1_epi16, g2_epi16);
					gdiff_epi16 = _mm_adds_epi16(gdiff_epi16,rounding256_epi16);
					gdiff_epi16 = _mm_slli_epi16(gdiff_epi16, 1);
					_mm_store_si128(gdiffptr_epi16++, gdiff_epi16);



				/*	r = *line1++;
					g1 = *line1++;
					g2 = *line2++;
					b = *line2++;

					// 10 bit
					g = (g1+g2);
					*g_row_ptr++ = (g<<1)+1;
					*rg_row_ptr++ = (r<<1)-g+512;
					*bg_row_ptr++ = (b<<1)-g+512;
					*gdiff_row_ptr++ = (g1-g2+256)<<1;
					*/
				}
				break;

			case BAYER_FORMAT_GRN_RED:
				for(x=0; x<width; x+=8)
				{
					// Read the first group of 16 8-bit packed pixels
					row_epi16 = _mm_load_si128(line1ptr_epi16++);
					row1a_epi16 = _mm_unpacklo_epi8(row_epi16,zero_epi16);

					// Read the first group of 8 16-bit packed 12-bit pixels
					//row1a_epi16 = _mm_load_si128(line1ptr_epi16++);
					//g1 and r  g0 r0 g1 r1 g2 r2 g3 r3
					row1a_epi16 = _mm_shufflehi_epi16(row1a_epi16,_MM_SHUFFLE(3,1,2,0));
					//g0 g1 r0 r1 g2 r2 g3 r3  _mm_shufflehi_epi16
					row1a_epi16 = _mm_shufflelo_epi16(row1a_epi16,_MM_SHUFFLE(3,1,2,0));
					//g0 g1 r0 r1 g2 g3 r2 r3  _mm_shufflelo_epi16
					row1a_epi16 = _mm_shuffle_epi32(row1a_epi16,_MM_SHUFFLE(3,1,2,0));
					//g0g1 g2g3 r0r1 r2r3  _mm_shuffle_epi32

					row1b_epi16 = _mm_unpackhi_epi8(row_epi16,zero_epi16);
					//row1b_epi16 = _mm_load_si128(line1ptr_epi16++);
					//g1 and r  g4 r4 g5 r5 g6 r6 g7 r7
					row1b_epi16 = _mm_shufflehi_epi16(row1b_epi16,_MM_SHUFFLE(3,1,2,0));
					//g4 g5 r4 r5 g6 r6 g7 r7  _mm_shufflehi_epi16
					row1b_epi16 = _mm_shufflelo_epi16(row1b_epi16,_MM_SHUFFLE(3,1,2,0));
					//g4 g5 r4 r5 g6 g7 r6 r7  _mm_shufflelo_epi16
					row1b_epi16 = _mm_shuffle_epi32(row1b_epi16,_MM_SHUFFLE(3,1,2,0));
					//g4g5 g6g7 r4r5 r6r7  _mm_shuffle_epi32


					r_epi16 = 	_mm_unpackhi_epi32(row1a_epi16, row1b_epi16);
					//g0g1 g4g5 g2g3 g6g7  _mm_unpackhi_epi32
					r_epi16 = 	_mm_shuffle_epi32(r_epi16, _MM_SHUFFLE(3,1,2,0));
					//g0g1 g2g3 g4g5 g6g7  _mm_shuffle_epi32

					g1_epi16 = 	_mm_unpacklo_epi32(row1a_epi16, row1b_epi16);
					//r0r1 r4r5 r2r3 r6r7  _mm_unpacklo_epi32
					g1_epi16 = 	_mm_shuffle_epi32(g1_epi16, _MM_SHUFFLE(3,1,2,0));
					//r0r1 r2r3 r4r5 r6r7  _mm_shuffle_epi32




					// Read the first group of 16 8-bit packed pixels
					row_epi16 = _mm_load_si128(line2ptr_epi16++);
					row2a_epi16 = _mm_unpacklo_epi8(row_epi16,zero_epi16);

					//row2a_epi16 = _mm_load_si128(line2ptr_epi16++);  //b and g2  b g b g b g b g
					row2a_epi16 = _mm_shufflehi_epi16(row2a_epi16,_MM_SHUFFLE(3,1,2,0));
					row2a_epi16 = _mm_shufflelo_epi16(row2a_epi16,_MM_SHUFFLE(3,1,2,0));
					row2a_epi16 = _mm_shuffle_epi32(row2a_epi16,_MM_SHUFFLE(3,1,2,0));
					//b0b1 b2b3 g0g1 g2g3  _mm_shuffle_epi32

					row2b_epi16 = _mm_unpackhi_epi8(row_epi16,zero_epi16);
					//row2b_epi16 = _mm_load_si128(line2ptr_epi16++);  //b and g2  b g b g b g b g
					row2b_epi16 = _mm_shufflehi_epi16(row2b_epi16,_MM_SHUFFLE(3,1,2,0));
					row2b_epi16 = _mm_shufflelo_epi16(row2b_epi16,_MM_SHUFFLE(3,1,2,0));
					row2b_epi16 = _mm_shuffle_epi32(row2b_epi16,_MM_SHUFFLE(3,1,2,0));
					//b4b5 b6b7 g4g5 g6g7  _mm_shuffle_epi32


					g2_epi16 = 	_mm_unpackhi_epi32(row2a_epi16, row2b_epi16);
					//b0b1 b4b5 b2b3 b6b7  _mm_unpackhi_epi32
					g2_epi16 = 	_mm_shuffle_epi32(g2_epi16, _MM_SHUFFLE(3,1,2,0));
					//b0b1 b2b3 b4b5 b6b7  _mm_shuffle_epi32


					b_epi16 = 	_mm_unpacklo_epi32(row2a_epi16, row2b_epi16);
					//g0g1 g4g5 g2g3 g6g7  _mm_unpacklo_epi32
					b_epi16 = 	_mm_shuffle_epi32(b_epi16, _MM_SHUFFLE(3,1,2,0));
					//g0g1 g2g3 g4g5 g6g7  _mm_shuffle_epi32


					//*g_row_ptr++ = (g<<1)+1;
					g_epi16 = _mm_adds_epi16(g1_epi16,g2_epi16);
					temp_epi16 = _mm_slli_epi16(g_epi16, 1);
					temp_epi16 = _mm_adds_epi16(temp_epi16,one_epi16);
					_mm_store_si128(gptr_epi16++, temp_epi16);

					//*rg_row_ptr++ = (r<<1)-g+512;
					rg_epi16 = _mm_slli_epi16(r_epi16, 1);
					rg_epi16 = _mm_subs_epi16(rg_epi16, g_epi16);
					rg_epi16 = _mm_adds_epi16(rg_epi16,rounding_epi16);
					_mm_store_si128(rgptr_epi16++, rg_epi16);

					//*bg_row_ptr++ = (b<<1)-g+512;
					bg_epi16 = _mm_slli_epi16(b_epi16, 1);
					bg_epi16 = _mm_subs_epi16(bg_epi16, g_epi16);
					bg_epi16 = _mm_adds_epi16(bg_epi16,rounding_epi16);
					_mm_store_si128(bgptr_epi16++, bg_epi16);

					//*gdiff_row_ptr++ = (g1-g2+256)<<1;
					gdiff_epi16 = _mm_subs_epi16(g1_epi16, g2_epi16);
					gdiff_epi16 = _mm_adds_epi16(gdiff_epi16,rounding256_epi16);
					gdiff_epi16 = _mm_slli_epi16(gdiff_epi16, 1);
					_mm_store_si128(gdiffptr_epi16++, gdiff_epi16);



				/*	g1 = *line1++;
					r = *line1++;
					b = *line2++;
					g2 = *line2++;

					// 10 bit
					g = (g1+g2);
					*g_row_ptr++ = (g<<1)+1;
					*rg_row_ptr++ = (r<<1)-g+512;
					*bg_row_ptr++ = (b<<1)-g+512;
					*gdiff_row_ptr++ = (g1-g2+256)<<1;
					*/
				}
				break;

			case BAYER_FORMAT_BLU_GRN:
				for(x=0; x<width; x+=8)
				{
					// Read the first group of 16 8-bit packed pixels
					row_epi16 = _mm_load_si128(line1ptr_epi16++);
					row1a_epi16 = _mm_unpacklo_epi8(row_epi16,zero_epi16);

					// Read the first group of 8 16-bit packed 12-bit pixels
					//row1a_epi16 = _mm_load_si128(line1ptr_epi16++);
					//g1 and r  r0 g0 r1 g1 r2 g2 r3 g3
					row1a_epi16 = _mm_shufflehi_epi16(row1a_epi16,_MM_SHUFFLE(2,0,3,1));
					//g0 g1 r0 r1 r2 g2 r3 g3  _mm_shufflehi_epi16
					row1a_epi16 = _mm_shufflelo_epi16(row1a_epi16,_MM_SHUFFLE(2,0,3,1));
					//g0 g1 r0 r1 g2 g3 r2 r3  _mm_shufflelo_epi16
					row1a_epi16 = _mm_shuffle_epi32(row1a_epi16,_MM_SHUFFLE(3,1,2,0));
					//g0g1 g2g3 r0r1 r2r3  _mm_shuffle_epi32

					row1b_epi16 = _mm_unpackhi_epi8(row_epi16,zero_epi16);
					//row1b_epi16 = _mm_load_si128(line1ptr_epi16++);
					//g1 and r  r4 g4 r5 g5 r6 g6 r7 g7
					row1b_epi16 = _mm_shufflehi_epi16(row1b_epi16,_MM_SHUFFLE(2,0,3,1));
					//g4 g5 r4 r5 r6 g6 r7 g7  _mm_shufflehi_epi16
					row1b_epi16 = _mm_shufflelo_epi16(row1b_epi16,_MM_SHUFFLE(2,0,3,1));
					//g4 g5 r4 r5 g6 g7 r6 r7  _mm_shufflelo_epi16
					row1b_epi16 = _mm_shuffle_epi32(row1b_epi16,_MM_SHUFFLE(3,1,2,0));
					//g4g5 g6g7 r4r5 r6r7  _mm_shuffle_epi32


					b_epi16 = 	_mm_unpackhi_epi32(row1a_epi16, row1b_epi16);
					//g0g1 g4g5 g2g3 g6g7  _mm_unpackhi_epi32
					b_epi16 = 	_mm_shuffle_epi32(b_epi16, _MM_SHUFFLE(3,1,2,0));
					//g0g1 g2g3 g4g5 g6g7  _mm_shuffle_epi32

					g1_epi16 = 	_mm_unpacklo_epi32(row1a_epi16, row1b_epi16);
					//r0r1 r4r5 r2r3 r6r7  _mm_unpacklo_epi32
					g1_epi16 = 	_mm_shuffle_epi32(g1_epi16, _MM_SHUFFLE(3,1,2,0));
					//r0r1 r2r3 r4r5 r6r7  _mm_shuffle_epi32




					// Read the first group of 16 8-bit packed pixels
					row_epi16 = _mm_load_si128(line2ptr_epi16++);
					row2a_epi16 = _mm_unpacklo_epi8(row_epi16,zero_epi16);

					//row2a_epi16 = _mm_load_si128(line2ptr_epi16++);  //b and g2  g b g b g b g b
					row2a_epi16 = _mm_shufflehi_epi16(row2a_epi16,_MM_SHUFFLE(2,0,3,1));
					row2a_epi16 = _mm_shufflelo_epi16(row2a_epi16,_MM_SHUFFLE(2,0,3,1));
					row2a_epi16 = _mm_shuffle_epi32(row2a_epi16,_MM_SHUFFLE(3,1,2,0));
					//b0b1 b2b3 g0g1 g2g3  _mm_shuffle_epi32

					row2b_epi16 = _mm_unpackhi_epi8(row_epi16,zero_epi16);
					//row2b_epi16 = _mm_load_si128(line2ptr_epi16++);  //b and g2  g b g b g b g b
					row2b_epi16 = _mm_shufflehi_epi16(row2b_epi16,_MM_SHUFFLE(2,0,3,1));
					row2b_epi16 = _mm_shufflelo_epi16(row2b_epi16,_MM_SHUFFLE(2,0,3,1));
					row2b_epi16 = _mm_shuffle_epi32(row2b_epi16,_MM_SHUFFLE(3,1,2,0));
					//b4b5 b6b7 g4g5 g6g7  _mm_shuffle_epi32


					g2_epi16 = 	_mm_unpackhi_epi32(row2a_epi16, row2b_epi16);
					//b0b1 b4b5 b2b3 b6b7  _mm_unpackhi_epi32
					g2_epi16 = 	_mm_shuffle_epi32(g2_epi16, _MM_SHUFFLE(3,1,2,0));
					//b0b1 b2b3 b4b5 b6b7  _mm_shuffle_epi32


					r_epi16 = 	_mm_unpacklo_epi32(row2a_epi16, row2b_epi16);
					//g0g1 g4g5 g2g3 g6g7  _mm_unpacklo_epi32
					r_epi16 = 	_mm_shuffle_epi32(r_epi16, _MM_SHUFFLE(3,1,2,0));
					//g0g1 g2g3 g4g5 g6g7  _mm_shuffle_epi32


					//*g_row_ptr++ = (g<<1)+1;
					g_epi16 = _mm_adds_epi16(g1_epi16,g2_epi16);
					temp_epi16 = _mm_slli_epi16(g_epi16, 1);
					temp_epi16 = _mm_adds_epi16(temp_epi16,one_epi16);
					_mm_store_si128(gptr_epi16++, temp_epi16);

					//*rg_row_ptr++ = (r<<1)-g+512;
					rg_epi16 = _mm_slli_epi16(r_epi16, 1);
					rg_epi16 = _mm_subs_epi16(rg_epi16, g_epi16);
					rg_epi16 = _mm_adds_epi16(rg_epi16,rounding_epi16);
					_mm_store_si128(rgptr_epi16++, rg_epi16);

					//*bg_row_ptr++ = (b<<1)-g+512;
					bg_epi16 = _mm_slli_epi16(b_epi16, 1);
					bg_epi16 = _mm_subs_epi16(bg_epi16, g_epi16);
					bg_epi16 = _mm_adds_epi16(bg_epi16,rounding_epi16);
					_mm_store_si128(bgptr_epi16++, bg_epi16);

					//*gdiff_row_ptr++ = (g1-g2+256)<<1;
					gdiff_epi16 = _mm_subs_epi16(g1_epi16, g2_epi16);
					gdiff_epi16 = _mm_adds_epi16(gdiff_epi16,rounding256_epi16);
					gdiff_epi16 = _mm_slli_epi16(gdiff_epi16, 1);
					_mm_store_si128(gdiffptr_epi16++, gdiff_epi16);


				/*	b = *line1++;
					g1 = *line1++;
					g2 = *line2++;
					r = *line2++;

					// 10 bit
					g = (g1+g2);
					*g_row_ptr++ = (g<<1)+1;
					*rg_row_ptr++ = (r<<1)-g+512;
					*bg_row_ptr++ = (b<<1)-g+512;
					*gdiff_row_ptr++ = (g1-g2+256)<<1;
					*/
				}
				break;

			case BAYER_FORMAT_GRN_BLU:
				for(x=0; x<width; x+=8)
				{
					// Read the first group of 16 8-bit packed pixels
					row_epi16 = _mm_load_si128(line1ptr_epi16++);
					row1a_epi16 = _mm_unpacklo_epi8(row_epi16,zero_epi16);

					// Read the first group of 8 16-bit packed 12-bit pixels
					//row1a_epi16 = _mm_load_si128(line1ptr_epi16++);
					//g1 and r  g0 r0 g1 r1 g2 r2 g3 r3
					row1a_epi16 = _mm_shufflehi_epi16(row1a_epi16,_MM_SHUFFLE(3,1,2,0));
					//g0 g1 r0 r1 g2 r2 g3 r3  _mm_shufflehi_epi16
					row1a_epi16 = _mm_shufflelo_epi16(row1a_epi16,_MM_SHUFFLE(3,1,2,0));
					//g0 g1 r0 r1 g2 g3 r2 r3  _mm_shufflelo_epi16
					row1a_epi16 = _mm_shuffle_epi32(row1a_epi16,_MM_SHUFFLE(3,1,2,0));
					//g0g1 g2g3 r0r1 r2r3  _mm_shuffle_epi32

					row1b_epi16 = _mm_unpackhi_epi8(row_epi16,zero_epi16);
					//row1b_epi16 = _mm_load_si128(line1ptr_epi16++);
					//g1 and r  g4 r4 g5 r5 g6 r6 g7 r7
					row1b_epi16 = _mm_shufflehi_epi16(row1b_epi16,_MM_SHUFFLE(3,1,2,0));
					//g4 g5 r4 r5 g6 r6 g7 r7  _mm_shufflehi_epi16
					row1b_epi16 = _mm_shufflelo_epi16(row1b_epi16,_MM_SHUFFLE(3,1,2,0));
					//g4 g5 r4 r5 g6 g7 r6 r7  _mm_shufflelo_epi16
					row1b_epi16 = _mm_shuffle_epi32(row1b_epi16,_MM_SHUFFLE(3,1,2,0));
					//g4g5 g6g7 r4r5 r6r7  _mm_shuffle_epi32


					b_epi16 = 	_mm_unpackhi_epi32(row1a_epi16, row1b_epi16);
					//g0g1 g4g5 g2g3 g6g7  _mm_unpackhi_epi32
					b_epi16 = 	_mm_shuffle_epi32(b_epi16, _MM_SHUFFLE(3,1,2,0));
					//g0g1 g2g3 g4g5 g6g7  _mm_shuffle_epi32

					g1_epi16 = 	_mm_unpacklo_epi32(row1a_epi16, row1b_epi16);
					//r0r1 r4r5 r2r3 r6r7  _mm_unpacklo_epi32
					g1_epi16 = 	_mm_shuffle_epi32(g1_epi16, _MM_SHUFFLE(3,1,2,0));
					//r0r1 r2r3 r4r5 r6r7  _mm_shuffle_epi32




					// Read the first group of 16 8-bit packed pixels
					row_epi16 = _mm_load_si128(line2ptr_epi16++);
					row2a_epi16 = _mm_unpacklo_epi8(row_epi16,zero_epi16);

					//row2a_epi16 = _mm_load_si128(line2ptr_epi16++);  //b and g2  b g b g b g b g
					row2a_epi16 = _mm_shufflehi_epi16(row2a_epi16,_MM_SHUFFLE(3,1,2,0));
					row2a_epi16 = _mm_shufflelo_epi16(row2a_epi16,_MM_SHUFFLE(3,1,2,0));
					row2a_epi16 = _mm_shuffle_epi32(row2a_epi16,_MM_SHUFFLE(3,1,2,0));
					//b0b1 b2b3 g0g1 g2g3  _mm_shuffle_epi32

					row2b_epi16 = _mm_unpackhi_epi8(row_epi16,zero_epi16);
					//row2b_epi16 = _mm_load_si128(line2ptr_epi16++);  //b and g2  b g b g b g b g
					row2b_epi16 = _mm_shufflehi_epi16(row2b_epi16,_MM_SHUFFLE(3,1,2,0));
					row2b_epi16 = _mm_shufflelo_epi16(row2b_epi16,_MM_SHUFFLE(3,1,2,0));
					row2b_epi16 = _mm_shuffle_epi32(row2b_epi16,_MM_SHUFFLE(3,1,2,0));
					//b4b5 b6b7 g4g5 g6g7  _mm_shuffle_epi32


					g2_epi16 = 	_mm_unpackhi_epi32(row2a_epi16, row2b_epi16);
					//b0b1 b4b5 b2b3 b6b7  _mm_unpackhi_epi32
					g2_epi16 = 	_mm_shuffle_epi32(g2_epi16, _MM_SHUFFLE(3,1,2,0));
					//b0b1 b2b3 b4b5 b6b7  _mm_shuffle_epi32


					r_epi16 = 	_mm_unpacklo_epi32(row2a_epi16, row2b_epi16);
					//g0g1 g4g5 g2g3 g6g7  _mm_unpacklo_epi32
					r_epi16 = 	_mm_shuffle_epi32(r_epi16, _MM_SHUFFLE(3,1,2,0));
					//g0g1 g2g3 g4g5 g6g7  _mm_shuffle_epi32


					//*g_row_ptr++ = (g<<1)+1;
					g_epi16 = _mm_adds_epi16(g1_epi16,g2_epi16);
					temp_epi16 = _mm_slli_epi16(g_epi16, 1);
					temp_epi16 = _mm_adds_epi16(temp_epi16,one_epi16);
					_mm_store_si128(gptr_epi16++, temp_epi16);

					//*rg_row_ptr++ = (r<<1)-g+512;
					rg_epi16 = _mm_slli_epi16(r_epi16, 1);
					rg_epi16 = _mm_subs_epi16(rg_epi16, g_epi16);
					rg_epi16 = _mm_adds_epi16(rg_epi16,rounding_epi16);
					_mm_store_si128(rgptr_epi16++, rg_epi16);

					//*bg_row_ptr++ = (b<<1)-g+512;
					bg_epi16 = _mm_slli_epi16(b_epi16, 1);
					bg_epi16 = _mm_subs_epi16(bg_epi16, g_epi16);
					bg_epi16 = _mm_adds_epi16(bg_epi16,rounding_epi16);
					_mm_store_si128(bgptr_epi16++, bg_epi16);

					//*gdiff_row_ptr++ = (g1-g2+256)<<1;
					gdiff_epi16 = _mm_subs_epi16(g1_epi16, g2_epi16);
					gdiff_epi16 = _mm_adds_epi16(gdiff_epi16,rounding256_epi16);
					gdiff_epi16 = _mm_slli_epi16(gdiff_epi16, 1);
					_mm_store_si128(gdiffptr_epi16++, gdiff_epi16);


				/*	g1 = *line1++;
					b = *line1++;
					r = *line2++;
					g2 = *line2++;

					// 10 bit
					g = (g1+g2);
					*g_row_ptr++ = (g<<1)+1;
					*rg_row_ptr++ = (r<<1)-g+512;
					*bg_row_ptr++ = (b<<1)-g+512;
					*gdiff_row_ptr++ = (g1-g2+256)<<1;
					*/
				}
				break;

		}

		// Advance to the next rows in the input and output images
		g_row_ptr += byr1_pitch;// - width;
		rg_row_ptr += byr1_pitch;// - width;
		bg_row_ptr += byr1_pitch;// - width;
		gdiff_row_ptr += byr1_pitch;// - width;
	}

	// Set the image parameters for each channel
	for (i = 0; i < 4; i++)
	{
		IMAGE *image = frame->channel[i];
		int band;

		// Set the image scale
		for (band = 0; band < IMAGE_NUM_BANDS; band++)
			image->scale[band] = 1;

		// Set the pixel type
		image->pixel_type[0] = PIXEL_TYPE_16S;
	}
}


#include <math.h>


#define BYR2_USE_GAMMA_TABLE  0
#define BYR2_HORIZONTAL_BAYER_SHIFT 1
#define BYR2_SWAP_R_B	0

// Convert the packed 16-bit BAYER RGB to planes of 16-bit RGBA
void ConvertBYR2ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer)
{
	IMAGE *g_image;
	IMAGE *rg_diff_image;
	IMAGE *bg_diff_image;
	IMAGE *gdiff_image;
	PIXEL *byr2_row_ptr;
	PIXEL *g_row_ptr;
	PIXEL *gdiff_row_ptr;
	PIXEL *rg_row_ptr;
	PIXEL *bg_row_ptr;
	int byr1_pitch;
	int width;
	int height;
	int display_height;
	int row;
	int i,x;
#if BYR2_USE_GAMMA_TABLE
	unsigned short gamma12bit[4096];
#endif

	// Process 16 bytes each of luma and chroma per loop iteration
	//const int column_step = 2 * sizeof(__m64);

	// Column at which post processing must begin
	//int post_column;

	if (frame == NULL) return;

	// The frame format should be four channels of RGBA
	assert(frame->num_channels == 4);
	assert(frame->format == FRAME_FORMAT_RGBA);

	g_image = frame->channel[0];					// Get the individual planes
#if BYR2_SWAP_R_B
	rg_diff_image = frame->channel[2];
	bg_diff_image = frame->channel[1];
#else
	rg_diff_image = frame->channel[1];
	bg_diff_image = frame->channel[2];
#endif
	gdiff_image = frame->channel[3];

	byr2_row_ptr = (PIXEL *)data;					// Pointers to the rows
	g_row_ptr = g_image->band[0];
	rg_row_ptr = rg_diff_image->band[0];
	bg_row_ptr = bg_diff_image->band[0];
	gdiff_row_ptr = gdiff_image->band[0];

	byr1_pitch = g_image->pitch/sizeof(PIXEL16S);

	width = g_image->width;							// Dimensions of the luma image
	height = g_image->height;
	display_height = frame->display_height;

	//post_column = width - (width % column_step);

	// The output pitch should be a positive number (no image inversion)
	assert(byr1_pitch > 0);

	// for the SEQ speed test on my 2.5 P4 I get 56fps this the C code.
#if BYR2_USE_GAMMA_TABLE

 #define BYR2_GAMMATABLE(x,y)  (  (int)(pow( (double)(x)/4095.0, (y) )*1023.0)  )

// #define BYR2_GAMMA2(x)  ((x)>>2)
#define BYR2_GAMMA2(x)  ( gamma12bit[(x)] )
//#define BYR2_GAMMA2(x)  (  (int)(pow( double(x)/4096.0, 1.0/2.0 )*256.0)  )
//inline int BYR2_GAMMA2(int x)  {  int v = 4095-(int)(x);  return ((4095 - ((v*v)>>12))>>4);  }

	{
		int blacklevel = 0;//100;
		float fgamma = 2.2;

		for(i=0;i<4096;i++)
		{
			int j = (i - blacklevel)*4096/(4096-blacklevel);
			if(j<0) j=0;
			gamma12bit[i] = BYR2_GAMMATABLE(j, 1.0/fgamma);
		}
	}

	{
	#define LINMAX 40
		float linearmax = (float)gamma12bit[LINMAX];
		float linearstep = linearmax/(float)LINMAX;
		float accum = 0.0;

		for(i=0; i<40; i++)
		{
			gamma12bit[i] = accum;
			accum += linearstep;
		}
	}

	for (row = 0; row < display_height; row++)
	{
		PIXEL g, g1, g2, r, b;
		PIXEL *line1,*line2;

		line1 = &byr2_row_ptr[row * pitch/2];
		line2 = line1 + (pitch>>2);

		for(x=0; x<width; x++)
		{
		/*	g1 = *line1++ >> 2;
			r = *line1++ >> 2;
			b = *line2++ >> 2;
			g2 = *line2++ >> 2;*/
		#if BYR2_HORIZONTAL_BAYER_SHIFT
			r= BYR2_GAMMA2(*line1++);
			g1 = BYR2_GAMMA2(*line1++);
			g2 = BYR2_GAMMA2(*line2++);
			b= BYR2_GAMMA2(*line2++);
		#else
			g1= BYR2_GAMMA2(*line1++);
			r = BYR2_GAMMA2(*line1++);
			b = BYR2_GAMMA2(*line2++);
			g2= BYR2_GAMMA2(*line2++);
		#endif

			/* 10 bit */
			g = (g1+g2)>>1;
			*g_row_ptr++ = g;
			*rg_row_ptr++ = ((r-g)>>1)+512;
			*bg_row_ptr++ = ((b-g)>>1)+512;
			*gdiff_row_ptr++ = (g1-g2+1024)>>1;
		}

		// Advance to the next rows in the input and output images
		g_row_ptr += byr1_pitch - width;
		rg_row_ptr += byr1_pitch - width;
		bg_row_ptr += byr1_pitch - width;
		gdiff_row_ptr += byr1_pitch - width;
	}
	for (; row < height; row++)
	{
		PIXEL g, g1, g2, r, b;
		PIXEL *line1,*line2;

		for(x=0; x<width; x++)
		{
			*g_row_ptr++ = 0;
			*rg_row_ptr++ = 0;
			*bg_row_ptr++ = 0;
			*gdiff_row_ptr++ = 0;
		}

		// Advance to the next rows in the input and output images
		g_row_ptr += byr1_pitch - width;
		rg_row_ptr += byr1_pitch - width;
		bg_row_ptr += byr1_pitch - width;
		gdiff_row_ptr += byr1_pitch - width;
	}


#else

	// for the SEQ speed test on my 2.5 P4 I get 65fps in the SSE2 code.
	for (row = 0; row < display_height; row++)
	{

		PIXEL *line1 = &byr2_row_ptr[row * pitch/2];
		PIXEL *line2 = line1 + (pitch>>2);

		__m128i *line1ptr_epi16 = (__m128i *)line1;
		__m128i *line2ptr_epi16 = (__m128i *)line2;
		__m128i *gptr_epi16 = (__m128i *)g_row_ptr;
		__m128i *gdiffptr_epi16 = (__m128i *)gdiff_row_ptr;
		__m128i *rgptr_epi16 = (__m128i *)rg_row_ptr;
		__m128i *bgptr_epi16 = (__m128i *)bg_row_ptr;

		__m128i row1a_epi16;
		__m128i row2a_epi16;
		__m128i row1b_epi16;
		__m128i row2b_epi16;

		__m128i g1_epi16;
		__m128i g2_epi16;
		__m128i r_epi16;
		__m128i b_epi16;

		__m128i g_epi16;
		__m128i gdiff_epi16;
		__m128i rg_epi16;
		__m128i bg_epi16;

		const __m128i rounding_epi16 = _mm_set1_epi16(512);


		for(x=0; x<width; x+=8)
		{
			// Read the first group of 8 16-bit packed 12-bit pixels
			row1a_epi16 = _mm_load_si128(line1ptr_epi16++);
			//g1 and r  g0 r0 g1 r1 g2 r2 g3 r3
			row1a_epi16 = _mm_shufflehi_epi16(row1a_epi16,_MM_SHUFFLE(3,1,2,0));
			//g0 g1 r0 r1 g2 r2 g3 r3  _mm_shufflehi_epi16
			row1a_epi16 = _mm_shufflelo_epi16(row1a_epi16,_MM_SHUFFLE(3,1,2,0));
			//g0 g1 r0 r1 g2 g3 r2 r3  _mm_shufflelo_epi16
			row1a_epi16 = _mm_shuffle_epi32(row1a_epi16,_MM_SHUFFLE(3,1,2,0));
			//g0g1 g2g3 r0r1 r2r3  _mm_shuffle_epi32

			row1b_epi16 = _mm_load_si128(line1ptr_epi16++);
			//g1 and r  g4 r4 g5 r5 g6 r6 g7 r7
			row1b_epi16 = _mm_shufflehi_epi16(row1b_epi16,_MM_SHUFFLE(3,1,2,0));
			//g4 g5 r4 r5 g6 r6 g7 r7  _mm_shufflehi_epi16
			row1b_epi16 = _mm_shufflelo_epi16(row1b_epi16,_MM_SHUFFLE(3,1,2,0));
			//g4 g5 r4 r5 g6 g7 r6 r7  _mm_shufflelo_epi16
			row1b_epi16 = _mm_shuffle_epi32(row1b_epi16,_MM_SHUFFLE(3,1,2,0));
			//g4g5 g6g7 r4r5 r6r7  _mm_shuffle_epi32

#if	BYR2_HORIZONTAL_BAYER_SHIFT
			g1_epi16 = 	_mm_unpackhi_epi32(row1a_epi16, row1b_epi16);
			//g0g1 g4g5 g2g3 g6g7  _mm_unpackhi_epi32
			g1_epi16 = 	_mm_shuffle_epi32(g1_epi16, _MM_SHUFFLE(3,1,2,0));
			//g0g1 g2g3 g4g5 g6g7  _mm_shuffle_epi32

			r_epi16 = 	_mm_unpacklo_epi32(row1a_epi16, row1b_epi16);
			//r0r1 r4r5 r2r3 r6r7  _mm_unpacklo_epi32
			r_epi16 = 	_mm_shuffle_epi32(r_epi16, _MM_SHUFFLE(3,1,2,0));
			//r0r1 r2r3 r4r5 r6r7  _mm_shuffle_epi32
#else
			r_epi16 = 	_mm_unpackhi_epi32(row1a_epi16, row1b_epi16);
			//g0g1 g4g5 g2g3 g6g7  _mm_unpackhi_epi32
			r_epi16 = 	_mm_shuffle_epi32(r_epi16, _MM_SHUFFLE(3,1,2,0));
			//g0g1 g2g3 g4g5 g6g7  _mm_shuffle_epi32

			g1_epi16 = 	_mm_unpacklo_epi32(row1a_epi16, row1b_epi16);
			//r0r1 r4r5 r2r3 r6r7  _mm_unpacklo_epi32
			g1_epi16 = 	_mm_shuffle_epi32(g1_epi16, _MM_SHUFFLE(3,1,2,0));
			//r0r1 r2r3 r4r5 r6r7  _mm_shuffle_epi32
#endif



			// Read the first group of 8 16-bit packed 12-bit pixels
			row2a_epi16 = _mm_load_si128(line2ptr_epi16++);  //b and g2  b g b g b g b g
			row2a_epi16 = _mm_shufflehi_epi16(row2a_epi16,_MM_SHUFFLE(3,1,2,0));
			row2a_epi16 = _mm_shufflelo_epi16(row2a_epi16,_MM_SHUFFLE(3,1,2,0));
			row2a_epi16 = _mm_shuffle_epi32(row2a_epi16,_MM_SHUFFLE(3,1,2,0));
			//b0b1 b2b3 g0g1 g2g3  _mm_shuffle_epi32

			row2b_epi16 = _mm_load_si128(line2ptr_epi16++);  //b and g2  b g b g b g b g
			row2b_epi16 = _mm_shufflehi_epi16(row2b_epi16,_MM_SHUFFLE(3,1,2,0));
			row2b_epi16 = _mm_shufflelo_epi16(row2b_epi16,_MM_SHUFFLE(3,1,2,0));
			row2b_epi16 = _mm_shuffle_epi32(row2b_epi16,_MM_SHUFFLE(3,1,2,0));
			//b4b5 b6b7 g4g5 g6g7  _mm_shuffle_epi32


#if	BYR2_HORIZONTAL_BAYER_SHIFT
			b_epi16 = 	_mm_unpackhi_epi32(row2a_epi16, row2b_epi16);
			//b0b1 b4b5 b2b3 b6b7  _mm_unpackhi_epi32
			b_epi16 = 	_mm_shuffle_epi32(b_epi16, _MM_SHUFFLE(3,1,2,0));
			//b0b1 b2b3 b4b5 b6b7  _mm_shuffle_epi32

			g2_epi16 = 	_mm_unpacklo_epi32(row2a_epi16, row2b_epi16);
			//g0g1 g4g5 g2g3 g6g7  _mm_unpacklo_epi32
			g2_epi16 = 	_mm_shuffle_epi32(g2_epi16, _MM_SHUFFLE(3,1,2,0));
			//g0g1 g2g3 g4g5 g6g7  _mm_shuffle_epi32
#else
			g2_epi16 = 	_mm_unpackhi_epi32(row2a_epi16, row2b_epi16);
			//b0b1 b4b5 b2b3 b6b7  _mm_unpackhi_epi32
			g2_epi16 = 	_mm_shuffle_epi32(g2_epi16, _MM_SHUFFLE(3,1,2,0));
			//b0b1 b2b3 b4b5 b6b7  _mm_shuffle_epi32

			b_epi16 = 	_mm_unpacklo_epi32(row2a_epi16, row2b_epi16);
			//g0g1 g4g5 g2g3 g6g7  _mm_unpacklo_epi32
			b_epi16 = 	_mm_shuffle_epi32(b_epi16, _MM_SHUFFLE(3,1,2,0));
			//g0g1 g2g3 g4g5 g6g7  _mm_shuffle_epi32
#endif

			g1_epi16 = _mm_srai_epi16(g1_epi16, 2);
			g2_epi16 = _mm_srai_epi16(g2_epi16, 2);
			r_epi16 = _mm_srai_epi16(r_epi16, 2);
			b_epi16 = _mm_srai_epi16(b_epi16, 2);

			g_epi16 = _mm_adds_epi16(g1_epi16,g2_epi16);
			g_epi16 = _mm_srai_epi16(g_epi16, 1);
			_mm_store_si128(gptr_epi16++, g_epi16);

			rg_epi16 = _mm_subs_epi16(r_epi16, g_epi16);
			rg_epi16 = _mm_srai_epi16(rg_epi16, 1);
			rg_epi16 = _mm_adds_epi16(rg_epi16,rounding_epi16);
			_mm_store_si128(rgptr_epi16++, rg_epi16);

			bg_epi16 = _mm_subs_epi16(b_epi16, g_epi16);
			bg_epi16 = _mm_srai_epi16(bg_epi16, 1);
			bg_epi16 = _mm_adds_epi16(bg_epi16,rounding_epi16);
			_mm_store_si128(bgptr_epi16++, bg_epi16);

			gdiff_epi16 = _mm_subs_epi16(g1_epi16, g2_epi16);
			gdiff_epi16 = _mm_adds_epi16(gdiff_epi16,rounding_epi16);
			gdiff_epi16 = _mm_adds_epi16(gdiff_epi16,rounding_epi16);
			gdiff_epi16 = _mm_srai_epi16(gdiff_epi16, 1);
			_mm_store_si128(gdiffptr_epi16++, gdiff_epi16);



		/*	g1 = *line1++ >> 2;
			r = *line1++ >> 2;
			b = *line2++ >> 2;
			g2 = *line2++ >> 2;

			// 10 bit
			g = (g1+g2)>>1;
			*g_row_ptr++ = g;
			*rg_row_ptr++ = ((r-g)>>1)+512;
			*bg_row_ptr++ = ((b-g)>>1)+512;
			*gdiff_row_ptr++ = (g1-g2+1024)>>1;
			*/
		}

		// Advance to the next rows in the input and output images
		g_row_ptr += byr1_pitch;// - width;
		rg_row_ptr += byr1_pitch;// - width;
		bg_row_ptr += byr1_pitch;// - width;
		gdiff_row_ptr += byr1_pitch;// - width;
	}
#endif


	// Set the image parameters for each channel
	for (i = 0; i < 4; i++)
	{
		IMAGE *image = frame->channel[i];
		int band;

		// Set the image scale
		for (band = 0; band < IMAGE_NUM_BANDS; band++)
			image->scale[band] = 1;

		// Set the pixel type
		image->pixel_type[0] = PIXEL_TYPE_16S;
	}
}





#define BYR3_USE_GAMMA_TABLE  0
#define BYR3_HORIZONTAL_BAYER_SHIFT 0
#define BYR3_SWAP_R_B	1


int ConvertPackedToRawBayer16(int width, int height, uint32_t *uncompressed_chunk,
						uint32_t uncompressed_size, PIXEL16U *RawBayer16, PIXEL16U *scratch,
						int resolution)
{
	int row;
	int x;
	int srcwidth;
	int linestep = 1;

	if (uncompressed_size < ((uint32_t)width * height * 4 * 3/2)) {
		// Not the correct data format
		return 0;
	}

	srcwidth = width;
	if(resolution == DECODED_RESOLUTION_QUARTER_NODEBAYER_SCALED)
	{
		srcwidth = width * 2;
		linestep = 2;
	}

	for (row = 0; row < height; row++)
	{
		PIXEL16U *tptr, *dptr;
		uint8_t *outB, *outN;

		tptr = scratch;
		dptr = RawBayer16;
		dptr += row * (width*4);

		outB = (uint8_t *)uncompressed_chunk;
		outB += row * linestep * srcwidth * 4 * 3/2; //12-bit
		outN = outB;
		outN += srcwidth * 4;

		{
			__m128i g1_epi16;
			__m128i g2_epi16;
			__m128i g3_epi16;
			__m128i g4_epi16;
			__m128i B1_epi16;
			__m128i N1_epi16;
			__m128i B2_epi16;
			__m128i N2_epi16;
			__m128i B3_epi16;
			__m128i N3_epi16;
			__m128i B4_epi16;
			__m128i N4_epi16;
			__m128i zero = _mm_set1_epi16(0);
			__m128i MaskUp = _mm_set1_epi16(0xf0f0);
			__m128i MaskDn = _mm_set1_epi16(0x0f0f);

			__m128i *tmp_epi16 = (__m128i *)tptr;
			__m128i *outB_epi16 = (__m128i *)outB;
			__m128i *outN_epi16 = (__m128i *)outN;

			for(x=0; x<srcwidth*4; x+=32)
			{
				B1_epi16 = _mm_loadu_si128(outB_epi16++);
				B2_epi16 = _mm_loadu_si128(outB_epi16++);
				N1_epi16 = _mm_loadu_si128(outN_epi16++);

				N2_epi16 = _mm_and_si128(N1_epi16, MaskDn);
				N1_epi16 = _mm_and_si128(N1_epi16, MaskUp);
				N1_epi16 = _mm_srli_epi16(N1_epi16, 4);

				N3_epi16 = _mm_unpacklo_epi8(N2_epi16, N1_epi16);
				N4_epi16 = _mm_unpackhi_epi8(N2_epi16, N1_epi16);

				g4_epi16 = _mm_unpackhi_epi8 (N4_epi16, zero);
				g3_epi16 = _mm_unpacklo_epi8 (N4_epi16, zero);
				g2_epi16 = _mm_unpackhi_epi8 (N3_epi16, zero);
				g1_epi16 = _mm_unpacklo_epi8 (N3_epi16, zero);

				B4_epi16 = _mm_unpackhi_epi8 (B2_epi16, zero);
				B3_epi16 = _mm_unpacklo_epi8 (B2_epi16, zero);
				B2_epi16 = _mm_unpackhi_epi8 (B1_epi16, zero);
				B1_epi16 = _mm_unpacklo_epi8 (B1_epi16, zero);

				B4_epi16 = _mm_slli_epi16(B4_epi16, 4);
				B3_epi16 = _mm_slli_epi16(B3_epi16, 4);
				B2_epi16 = _mm_slli_epi16(B2_epi16, 4);
				B1_epi16 = _mm_slli_epi16(B1_epi16, 4);

				g1_epi16 = _mm_or_si128(g1_epi16, B1_epi16);
				g2_epi16 = _mm_or_si128(g2_epi16, B2_epi16);
				g3_epi16 = _mm_or_si128(g3_epi16, B3_epi16);
				g4_epi16 = _mm_or_si128(g4_epi16, B4_epi16);

				_mm_store_si128(tmp_epi16++, g1_epi16);
				_mm_store_si128(tmp_epi16++, g2_epi16);
				_mm_store_si128(tmp_epi16++, g3_epi16);
				_mm_store_si128(tmp_epi16++, g4_epi16);
			}

			if(linestep == 1)
			{
				__m128i *rp_epi16 = (__m128i *)tptr;
				__m128i *g1p_epi16 = (__m128i *)&tptr[width];
				__m128i *g2p_epi16 = (__m128i *)&tptr[width*2];
				__m128i *bp_epi16 = (__m128i *)&tptr[width*3];
				__m128i *dgg_epi16 = (__m128i *)dptr;
				__m128i *drg_epi16 = (__m128i *)&dptr[width];
				__m128i *dbg_epi16 = (__m128i *)&dptr[width*2];
				__m128i *ddg_epi16 = (__m128i *)&dptr[width*3];
				__m128i mid11bit = _mm_set1_epi16(1<<(13-1));

				for(x=0; x<srcwidth; x+=8)
				{
					__m128i r_epi16 = _mm_load_si128(rp_epi16++);
					__m128i g1_epi16 = _mm_load_si128(g1p_epi16++);
					__m128i g2_epi16 = _mm_load_si128(g2p_epi16++);
					__m128i b_epi16 = _mm_load_si128(bp_epi16++);

					__m128i gg = _mm_adds_epu16(g1_epi16, g2_epi16); //13-bit
					__m128i rg = _mm_adds_epu16(r_epi16, r_epi16); //13-bit
					__m128i bg = _mm_adds_epu16(b_epi16, b_epi16); //13-bit
					__m128i dg = _mm_subs_epi16(g1_epi16, g2_epi16); //signed 12-bit

					rg = _mm_subs_epi16(rg, gg); //13-bit
					bg = _mm_subs_epi16(bg, gg); //13-bit
					rg = _mm_srai_epi16(rg, 1); //12-bit signed
					bg = _mm_srai_epi16(bg, 1); //12-bit signed
					rg = _mm_adds_epi16(rg, mid11bit); //13-bit unsigned
					bg = _mm_adds_epi16(bg, mid11bit); //13-bit unsigned
					dg = _mm_adds_epi16(dg, mid11bit); //13-bit unsigned
					gg = _mm_slli_epi16(gg, 3); //16-bit unsigned
					rg = _mm_slli_epi16(rg, 3); //16-bit unsigned
					bg = _mm_slli_epi16(bg, 3); //16-bit unsigned
					dg = _mm_slli_epi16(dg, 3); //16-bit unsigned

					_mm_store_si128(dgg_epi16++, gg);
					_mm_store_si128(drg_epi16++, rg);
					_mm_store_si128(dbg_epi16++, bg);
					_mm_store_si128(ddg_epi16++, dg);
				}

				for(; x<srcwidth; x++)
				{
					int G = (scratch[x+width] + scratch[x+width*2])<<2;
					int RG = (scratch[x]<<3)-G+32768;
					int BG = (scratch[x+width*3]<<3)-G+32768;
					int DG = ((scratch[x+width] - scratch[x+width*2])<<3) + 32768;
					dptr[x] = G<<1;
					dptr[x+width] = RG;//scratch[x+width];
					dptr[x+width*2] = BG;//scratch[x+width*2];
					dptr[x+width*3] = DG;//scratch[x+width*3];
				}
			}
			else
			{
				for(x=0; x<width; x++)
				{
					int G = (scratch[x*2+srcwidth] + scratch[x*2+srcwidth*2])<<2;
					int RG = (scratch[x*2]<<3)-G+32768;
					int BG = (scratch[x*2+srcwidth*3]<<3)-G+32768;
					int DG = ((scratch[x*2+srcwidth] - scratch[x*2+srcwidth*2])<<3) + 32768;
					dptr[x] = G<<1;
					dptr[x+width] = RG;//scratch[x+width];
					dptr[x+width*2] = BG;//scratch[x+width*2];
					dptr[x+width*3] = DG;//scratch[x+width*3];
				}
			}
		}
	}

	return 0;
}



int ConvertPackedToBYR2(int width, int height, uint32_t *uncompressed_chunk, uint32_t uncompressed_size, uint8_t *output_buffer, int output_pitch, unsigned short *curve)
{
	int row, x;	

	if (uncompressed_size < ((uint32_t)width * height * 4 * 3/2)) {
		// Not the correct data format
		return 0;
	}

	for (row = 0; row < height; row++)
	{
		PIXEL16U *dptrRG, *dptrGB;
		uint8_t *outB, *outN;

		dptrRG = (PIXEL16U *)output_buffer;
		dptrRG += row * (width*4);
		dptrGB = dptrRG;
		dptrGB += (width*2);

		outB = (uint8_t *)uncompressed_chunk;
		outB += row * width * 4 * 3/2; //12-bit
		outN = outB;
		outN += width * 4;

		{
			__m128i gA1_epi16;
			__m128i gA2_epi16;
			__m128i gA3_epi16;
			__m128i gA4_epi16;

			__m128i gB1_epi16;
			__m128i gB2_epi16;
			__m128i gB3_epi16;
			__m128i gB4_epi16;

			__m128i r1_epi16;
			__m128i r2_epi16;
			__m128i r3_epi16;
			__m128i r4_epi16;

			__m128i b1_epi16;
			__m128i b2_epi16;
			__m128i b3_epi16;
			__m128i b4_epi16;

			__m128i B1_epi16;
			__m128i N1_epi16;
			__m128i B2_epi16;
			__m128i N2_epi16;
			__m128i B3_epi16;
			__m128i N3_epi16;
			__m128i B4_epi16;
			__m128i N4_epi16;
			__m128i zero = _mm_set1_epi16(0);
			__m128i MaskUp = _mm_set1_epi16(0xf0f0);
			__m128i MaskDn = _mm_set1_epi16(0x0f0f);

			__m128i *dstRG_epi16 = (__m128i *)dptrRG;
			__m128i *dstGB_epi16 = (__m128i *)dptrGB;
			__m128i *outBr_epi16 = (__m128i *)outB;
			__m128i *outNr_epi16 = (__m128i *)outN;
			__m128i *outBgA_epi16;
			__m128i *outNgA_epi16;
			__m128i *outBgB_epi16;
			__m128i *outNgB_epi16;
			__m128i *outBb_epi16;
			__m128i *outNb_epi16;

			outB += width;
			outN += width>>1;
			outBgA_epi16 = (__m128i *)outB;
			outNgA_epi16 = (__m128i *)outN;

			outB += width;
			outN += width>>1;
			outBgB_epi16 = (__m128i *)outB;
			outNgB_epi16 = (__m128i *)outN;

			outB += width;
			outN += width>>1;
			outBb_epi16 = (__m128i *)outB;
			outNb_epi16 = (__m128i *)outN;


			for(x=0; x<width; x+=32)
			{
				B1_epi16 = _mm_loadu_si128(outBr_epi16++);
				B2_epi16 = _mm_loadu_si128(outBr_epi16++);
				N1_epi16 = _mm_loadu_si128(outNr_epi16++);

				N2_epi16 = _mm_and_si128(N1_epi16, MaskDn);
				N1_epi16 = _mm_and_si128(N1_epi16, MaskUp);
				N1_epi16 = _mm_srli_epi16(N1_epi16, 4);

				N3_epi16 = _mm_unpacklo_epi8(N2_epi16, N1_epi16);
				N4_epi16 = _mm_unpackhi_epi8(N2_epi16, N1_epi16);

				r4_epi16 = _mm_unpackhi_epi8 (N4_epi16, zero);
				r3_epi16 = _mm_unpacklo_epi8 (N4_epi16, zero);
				r2_epi16 = _mm_unpackhi_epi8 (N3_epi16, zero);
				r1_epi16 = _mm_unpacklo_epi8 (N3_epi16, zero);

				B4_epi16 = _mm_unpackhi_epi8 (B2_epi16, zero);
				B3_epi16 = _mm_unpacklo_epi8 (B2_epi16, zero);
				B2_epi16 = _mm_unpackhi_epi8 (B1_epi16, zero);
				B1_epi16 = _mm_unpacklo_epi8 (B1_epi16, zero);

				B4_epi16 = _mm_slli_epi16(B4_epi16, 4);
				B3_epi16 = _mm_slli_epi16(B3_epi16, 4);
				B2_epi16 = _mm_slli_epi16(B2_epi16, 4);
				B1_epi16 = _mm_slli_epi16(B1_epi16, 4);

				r1_epi16 = _mm_or_si128(r1_epi16, B1_epi16);
				r2_epi16 = _mm_or_si128(r2_epi16, B2_epi16);
				r3_epi16 = _mm_or_si128(r3_epi16, B3_epi16);
				r4_epi16 = _mm_or_si128(r4_epi16, B4_epi16);

				r1_epi16 = _mm_slli_epi16(r1_epi16, 4);
				r2_epi16 = _mm_slli_epi16(r2_epi16, 4);
				r3_epi16 = _mm_slli_epi16(r3_epi16, 4);
				r4_epi16 = _mm_slli_epi16(r4_epi16, 4);




				B1_epi16 = _mm_loadu_si128(outBgA_epi16++);
				B2_epi16 = _mm_loadu_si128(outBgA_epi16++);
				N1_epi16 = _mm_loadu_si128(outNgA_epi16++);

				N2_epi16 = _mm_and_si128(N1_epi16, MaskDn);
				N1_epi16 = _mm_and_si128(N1_epi16, MaskUp);
				N1_epi16 = _mm_srli_epi16(N1_epi16, 4);

				N3_epi16 = _mm_unpacklo_epi8(N2_epi16, N1_epi16);
				N4_epi16 = _mm_unpackhi_epi8(N2_epi16, N1_epi16);

				gA4_epi16 = _mm_unpackhi_epi8 (N4_epi16, zero);
				gA3_epi16 = _mm_unpacklo_epi8 (N4_epi16, zero);
				gA2_epi16 = _mm_unpackhi_epi8 (N3_epi16, zero);
				gA1_epi16 = _mm_unpacklo_epi8 (N3_epi16, zero);

				B4_epi16 = _mm_unpackhi_epi8 (B2_epi16, zero);
				B3_epi16 = _mm_unpacklo_epi8 (B2_epi16, zero);
				B2_epi16 = _mm_unpackhi_epi8 (B1_epi16, zero);
				B1_epi16 = _mm_unpacklo_epi8 (B1_epi16, zero);

				B4_epi16 = _mm_slli_epi16(B4_epi16, 4);
				B3_epi16 = _mm_slli_epi16(B3_epi16, 4);
				B2_epi16 = _mm_slli_epi16(B2_epi16, 4);
				B1_epi16 = _mm_slli_epi16(B1_epi16, 4);

				gA1_epi16 = _mm_or_si128(gA1_epi16, B1_epi16);
				gA2_epi16 = _mm_or_si128(gA2_epi16, B2_epi16);
				gA3_epi16 = _mm_or_si128(gA3_epi16, B3_epi16);
				gA4_epi16 = _mm_or_si128(gA4_epi16, B4_epi16);

				gA1_epi16 = _mm_slli_epi16(gA1_epi16, 4);
				gA2_epi16 = _mm_slli_epi16(gA2_epi16, 4);
				gA3_epi16 = _mm_slli_epi16(gA3_epi16, 4);
				gA4_epi16 = _mm_slli_epi16(gA4_epi16, 4);


				_mm_store_si128(dstRG_epi16++, _mm_unpacklo_epi16(r1_epi16, gA1_epi16));
				_mm_store_si128(dstRG_epi16++, _mm_unpackhi_epi16(r1_epi16, gA1_epi16));
				_mm_store_si128(dstRG_epi16++, _mm_unpacklo_epi16(r2_epi16, gA2_epi16));
				_mm_store_si128(dstRG_epi16++, _mm_unpackhi_epi16(r2_epi16, gA2_epi16));
				_mm_store_si128(dstRG_epi16++, _mm_unpacklo_epi16(r3_epi16, gA3_epi16));
				_mm_store_si128(dstRG_epi16++, _mm_unpackhi_epi16(r3_epi16, gA3_epi16));
				_mm_store_si128(dstRG_epi16++, _mm_unpacklo_epi16(r4_epi16, gA4_epi16));
				_mm_store_si128(dstRG_epi16++, _mm_unpackhi_epi16(r4_epi16, gA4_epi16));


				B1_epi16 = _mm_loadu_si128(outBgB_epi16++);
				B2_epi16 = _mm_loadu_si128(outBgB_epi16++);
				N1_epi16 = _mm_loadu_si128(outNgB_epi16++);

				N2_epi16 = _mm_and_si128(N1_epi16, MaskDn);
				N1_epi16 = _mm_and_si128(N1_epi16, MaskUp);
				N1_epi16 = _mm_srli_epi16(N1_epi16, 4);

				N3_epi16 = _mm_unpacklo_epi8(N2_epi16, N1_epi16);
				N4_epi16 = _mm_unpackhi_epi8(N2_epi16, N1_epi16);

				gB4_epi16 = _mm_unpackhi_epi8 (N4_epi16, zero);
				gB3_epi16 = _mm_unpacklo_epi8 (N4_epi16, zero);
				gB2_epi16 = _mm_unpackhi_epi8 (N3_epi16, zero);
				gB1_epi16 = _mm_unpacklo_epi8 (N3_epi16, zero);

				B4_epi16 = _mm_unpackhi_epi8 (B2_epi16, zero);
				B3_epi16 = _mm_unpacklo_epi8 (B2_epi16, zero);
				B2_epi16 = _mm_unpackhi_epi8 (B1_epi16, zero);
				B1_epi16 = _mm_unpacklo_epi8 (B1_epi16, zero);

				B4_epi16 = _mm_slli_epi16(B4_epi16, 4);
				B3_epi16 = _mm_slli_epi16(B3_epi16, 4);
				B2_epi16 = _mm_slli_epi16(B2_epi16, 4);
				B1_epi16 = _mm_slli_epi16(B1_epi16, 4);

				gB1_epi16 = _mm_or_si128(gB1_epi16, B1_epi16);
				gB2_epi16 = _mm_or_si128(gB2_epi16, B2_epi16);
				gB3_epi16 = _mm_or_si128(gB3_epi16, B3_epi16);
				gB4_epi16 = _mm_or_si128(gB4_epi16, B4_epi16);

				gB1_epi16 = _mm_slli_epi16(gB1_epi16, 4);
				gB2_epi16 = _mm_slli_epi16(gB2_epi16, 4);
				gB3_epi16 = _mm_slli_epi16(gB3_epi16, 4);
				gB4_epi16 = _mm_slli_epi16(gB4_epi16, 4);




				B1_epi16 = _mm_loadu_si128(outBb_epi16++);
				B2_epi16 = _mm_loadu_si128(outBb_epi16++);
				N1_epi16 = _mm_loadu_si128(outNb_epi16++);

				N2_epi16 = _mm_and_si128(N1_epi16, MaskDn);
				N1_epi16 = _mm_and_si128(N1_epi16, MaskUp);
				N1_epi16 = _mm_srli_epi16(N1_epi16, 4);

				N3_epi16 = _mm_unpacklo_epi8(N2_epi16, N1_epi16);
				N4_epi16 = _mm_unpackhi_epi8(N2_epi16, N1_epi16);

				b4_epi16 = _mm_unpackhi_epi8 (N4_epi16, zero);
				b3_epi16 = _mm_unpacklo_epi8 (N4_epi16, zero);
				b2_epi16 = _mm_unpackhi_epi8 (N3_epi16, zero);
				b1_epi16 = _mm_unpacklo_epi8 (N3_epi16, zero);

				B4_epi16 = _mm_unpackhi_epi8 (B2_epi16, zero);
				B3_epi16 = _mm_unpacklo_epi8 (B2_epi16, zero);
				B2_epi16 = _mm_unpackhi_epi8 (B1_epi16, zero);
				B1_epi16 = _mm_unpacklo_epi8 (B1_epi16, zero);

				B4_epi16 = _mm_slli_epi16(B4_epi16, 4);
				B3_epi16 = _mm_slli_epi16(B3_epi16, 4);
				B2_epi16 = _mm_slli_epi16(B2_epi16, 4);
				B1_epi16 = _mm_slli_epi16(B1_epi16, 4);

				b1_epi16 = _mm_or_si128(b1_epi16, B1_epi16);
				b2_epi16 = _mm_or_si128(b2_epi16, B2_epi16);
				b3_epi16 = _mm_or_si128(b3_epi16, B3_epi16);
				b4_epi16 = _mm_or_si128(b4_epi16, B4_epi16);

				b1_epi16 = _mm_slli_epi16(b1_epi16, 4);
				b2_epi16 = _mm_slli_epi16(b2_epi16, 4);
				b3_epi16 = _mm_slli_epi16(b3_epi16, 4);
				b4_epi16 = _mm_slli_epi16(b4_epi16, 4);



				_mm_store_si128(dstGB_epi16++, _mm_unpacklo_epi16(gB1_epi16, b1_epi16));
				_mm_store_si128(dstGB_epi16++, _mm_unpackhi_epi16(gB1_epi16, b1_epi16));
				_mm_store_si128(dstGB_epi16++, _mm_unpacklo_epi16(gB2_epi16, b2_epi16));
				_mm_store_si128(dstGB_epi16++, _mm_unpackhi_epi16(gB2_epi16, b2_epi16));
				_mm_store_si128(dstGB_epi16++, _mm_unpacklo_epi16(gB3_epi16, b3_epi16));
				_mm_store_si128(dstGB_epi16++, _mm_unpackhi_epi16(gB3_epi16, b3_epi16));
				_mm_store_si128(dstGB_epi16++, _mm_unpacklo_epi16(gB4_epi16, b4_epi16));
				_mm_store_si128(dstGB_epi16++, _mm_unpackhi_epi16(gB4_epi16, b4_epi16));
			}
		}

		if(curve)
		{
			for(x=0; x<width*2; x++)
			{
				dptrRG[x] = curve[dptrRG[x]>>2];
				dptrGB[x] = curve[dptrGB[x]>>2];
			}
		}
	}

	return 0;
}

int ConvertPackedToBYR3(int width, int height, uint32_t *uncompressed_chunk, uint32_t uncompressed_size, uint8_t *output_buffer, int output_pitch)
{
	int row, x;

	if (uncompressed_size < ((uint32_t)width * height * 4 * 3/2)) {
		// Not the correct data format
		return 0;
	}

	for (row = 0; row < height; row++)
	{
		PIXEL16U *dptr;
		uint8_t *outB, *outN;

		dptr = (PIXEL16U *)output_buffer;
		dptr += row * (width*4);

		outB = (uint8_t *)uncompressed_chunk;
		outB += row * width * 4 * 3/2; //12-bit
		outN = outB;
		outN += width * 4;

		{
			__m128i g1_epi16;
			__m128i g2_epi16;
			__m128i g3_epi16;
			__m128i g4_epi16;
			__m128i B1_epi16;
			__m128i N1_epi16;
			__m128i B2_epi16;
			__m128i N2_epi16;
			__m128i B3_epi16;
			__m128i N3_epi16;
			__m128i B4_epi16;
			__m128i N4_epi16;
			__m128i zero = _mm_set1_epi16(0);
			__m128i MaskUp = _mm_set1_epi16(0xf0f0);
			__m128i MaskDn = _mm_set1_epi16(0x0f0f);

			__m128i *dst_epi16 = (__m128i *)dptr;
			__m128i *outB_epi16 = (__m128i *)outB;
			__m128i *outN_epi16 = (__m128i *)outN;


			for(x=0; x<width*4; x+=32)
			{
				B1_epi16 = _mm_loadu_si128(outB_epi16++);
				B2_epi16 = _mm_loadu_si128(outB_epi16++);
				N1_epi16 = _mm_loadu_si128(outN_epi16++);

				N2_epi16 = _mm_and_si128(N1_epi16, MaskDn);
				N1_epi16 = _mm_and_si128(N1_epi16, MaskUp);
				N1_epi16 = _mm_srli_epi16(N1_epi16, 4);

				N3_epi16 = _mm_unpacklo_epi8(N2_epi16, N1_epi16);
				N4_epi16 = _mm_unpackhi_epi8(N2_epi16, N1_epi16);

				g4_epi16 = _mm_unpackhi_epi8 (N4_epi16, zero);
				g3_epi16 = _mm_unpacklo_epi8 (N4_epi16, zero);
				g2_epi16 = _mm_unpackhi_epi8 (N3_epi16, zero);
				g1_epi16 = _mm_unpacklo_epi8 (N3_epi16, zero);

				B4_epi16 = _mm_unpackhi_epi8 (B2_epi16, zero);
				B3_epi16 = _mm_unpacklo_epi8 (B2_epi16, zero);
				B2_epi16 = _mm_unpackhi_epi8 (B1_epi16, zero);
				B1_epi16 = _mm_unpacklo_epi8 (B1_epi16, zero);

				B4_epi16 = _mm_slli_epi16(B4_epi16, 4);
				B3_epi16 = _mm_slli_epi16(B3_epi16, 4);
				B2_epi16 = _mm_slli_epi16(B2_epi16, 4);
				B1_epi16 = _mm_slli_epi16(B1_epi16, 4);

				g1_epi16 = _mm_or_si128(g1_epi16, B1_epi16);
				g2_epi16 = _mm_or_si128(g2_epi16, B2_epi16);
				g3_epi16 = _mm_or_si128(g3_epi16, B3_epi16);
				g4_epi16 = _mm_or_si128(g4_epi16, B4_epi16);

				g1_epi16 = _mm_srli_epi16(g1_epi16, 2); //12-bit down 10-bit.
				g2_epi16 = _mm_srli_epi16(g2_epi16, 2);
				g3_epi16 = _mm_srli_epi16(g3_epi16, 2);
				g4_epi16 = _mm_srli_epi16(g4_epi16, 2);

				_mm_store_si128(dst_epi16++, g1_epi16);
				_mm_store_si128(dst_epi16++, g2_epi16);
				_mm_store_si128(dst_epi16++, g3_epi16);
				_mm_store_si128(dst_epi16++, g4_epi16);
			}
		}
	}

	return 0;
}

int ConvertBYR3ToPacked(uint8_t *data, int pitch, int width, int height, uint8_t *buffer)
{
	int row,x;


	for(row=0; row<height; row++)
	{
		PIXEL16U *sptr;
		uint8_t *outB, *outN;

		sptr = (PIXEL16U *)data;
		sptr += row * (pitch>>1);

		outB = (uint8_t *)buffer;
		outB += row * width * 4 * 3/2; //12-bit
		outN = outB;
		outN += width * 4;

#if 1
		{
			__m128i g1_epi16;
			__m128i g2_epi16;
			__m128i g3_epi16;
			__m128i g4_epi16;
			__m128i B1_epi16;
			__m128i N1_epi16;
			__m128i B2_epi16;
			__m128i N2_epi16;
			__m128i B3_epi16;
			__m128i N3_epi16;
			__m128i B4_epi16;
			__m128i N4_epi16;
			__m128i MaskHi = _mm_set1_epi16(0x00f0);

			__m128i *src_epi16 = (__m128i *)sptr;
			__m128i *outB_epi16 = (__m128i *)outB;
			__m128i *outN_epi16 = (__m128i *)outN;


			for(x=0; x<width*4; x+=32)
			{
				// Read the first group of 8 16-bit packed 12-bit pixels
				g1_epi16 = _mm_load_si128(src_epi16++);
				g2_epi16 = _mm_load_si128(src_epi16++);
				g3_epi16 = _mm_load_si128(src_epi16++);
				g4_epi16 = _mm_load_si128(src_epi16++);

				// boost to 12-bit first
				g1_epi16 = _mm_slli_epi16(g1_epi16, 2);
				g2_epi16 = _mm_slli_epi16(g2_epi16, 2);
				g3_epi16 = _mm_slli_epi16(g3_epi16, 2);
				g4_epi16 = _mm_slli_epi16(g4_epi16, 2);

				B1_epi16 = _mm_srli_epi16(g1_epi16, 4);
				N1_epi16 = _mm_slli_epi16(g1_epi16, 4);
				B2_epi16 = _mm_srli_epi16(g2_epi16, 4);
				N2_epi16 = _mm_slli_epi16(g2_epi16, 4);
				B3_epi16 = _mm_srli_epi16(g3_epi16, 4);
				N3_epi16 = _mm_slli_epi16(g3_epi16, 4);
				B4_epi16 = _mm_srli_epi16(g4_epi16, 4);
				N4_epi16 = _mm_slli_epi16(g4_epi16, 4);

				N1_epi16 = _mm_and_si128(N1_epi16, MaskHi);
				N2_epi16 = _mm_and_si128(N2_epi16, MaskHi);
				N3_epi16 = _mm_and_si128(N3_epi16, MaskHi);
				N4_epi16 = _mm_and_si128(N4_epi16, MaskHi);

				B1_epi16 = _mm_packus_epi16(B1_epi16, B2_epi16);
				N1_epi16 = _mm_packus_epi16(N1_epi16, N2_epi16);

				B2_epi16 = _mm_packus_epi16(B3_epi16, B4_epi16);
				N2_epi16 = _mm_packus_epi16(N3_epi16, N4_epi16);

				N2_epi16 = _mm_srli_epi16(N2_epi16, 4);
				N1_epi16 = _mm_or_si128(N1_epi16, N2_epi16);

				_mm_store_si128(outB_epi16++, B1_epi16);
				_mm_store_si128(outB_epi16++, B2_epi16);
				_mm_store_si128(outN_epi16++, N1_epi16);
			}
		}
#else
		for(x=0; x<width * 4; x+=32) //R, G1,G2, B
		{
			int xx;
			for(xx=0;xx<32;xx++)
			{
				g1[xx] = *sptr++<<2;
			}

			for(xx=0;xx<32;xx++)
			{
				*outB++ = g1[xx] >> 4;
			}
			for(xx=0;xx<32;xx+=2)
			{
				*outN++ = ((g1[xx] << 4) & 0xf0) | (g1[xx+1] & 0xf);
			}
		}
#endif
	}

	return 3 * width * 4 * height / 2;
}



int ConvertRGB10ToDPX0(uint8_t *data, int pitch, int width, int height, int unc_format)
{
	int row,x;

	for(row=0; row<height; row++)
	{
		uint32_t val,*sptr;
		int r,g,b;

		sptr = (uint32_t *)data;
		sptr += row * (pitch>>2);

		switch(unc_format)
		{
			case COLOR_FORMAT_RG30://rg30 A2B10G10R10
			case COLOR_FORMAT_AB10:
				for(x=0; x<width; x++)
				{
					val = *sptr;
					r = val & 0x3ff;
					val >>= 10;
					g = val & 0x3ff;
					val >>= 10;
					b = val & 0x3ff;
					
					r <<= 22;
					g <<= 12;
					b <<= 2;
					val = r;
					val |= g;
					val |= b;
					*sptr++ = SwapInt32(val);
				}
				break;
			case COLOR_FORMAT_R210:
				for(x=0; x<width; x++)
				{
					val = SwapInt32(*sptr);
					b = val & 0x3ff;
					val >>= 10;
					g = val & 0x3ff;
					val >>= 10;
					r = val & 0x3ff;
					
					r <<= 22;
					g <<= 12;
					b <<= 2;
					val = r;
					val |= g;
					val |= b;
					*sptr++ = SwapInt32(val);					
				}
				break;
			case COLOR_FORMAT_AR10: 
				for(x=0; x<width; x++)
				{	
					val = *sptr;
					b = val & 0x3ff;
					val >>= 10;
					g = val & 0x3ff;
					val >>= 10;
					r = val & 0x3ff;
					
					r <<= 22;
					g <<= 12;
					b <<= 2;
					val = r;
					val |= g;
					val |= b;
					*sptr++ = SwapInt32(val);					
				}
				break;
		}
	}

	return width * 4 * height;
}


int ConvertDPX0ToRGB10(uint8_t *data, int pitch, int width, int height, int unc_format)
{
	int row,x;

	for(row=0; row<height; row++)
	{
		uint32_t val,*sptr;
		int r,g,b;

		sptr = (uint32_t *)data;
		sptr += row * (pitch>>2);

		switch(unc_format)
		{
			case COLOR_FORMAT_RG30://rg30 A2B10G10R10
			case COLOR_FORMAT_AB10:
				for(x=0; x<width; x++)
				{
					val = SwapInt32(*sptr);
					val >>= 2;
					b = val & 0x3ff;
					val >>= 10;
					g = val & 0x3ff;
					val >>= 10;
					r = val & 0x3ff;
		
					r <<= 0;
					g <<= 10;
					b <<= 20;
					val = r;
					val |= g;
					val |= b;
					*sptr++ = val;
				}
				break;
			case COLOR_FORMAT_R210:
				for(x=0; x<width; x++)
				{
					val = SwapInt32(*sptr);
					val >>= 2;
					b = val & 0x3ff;
					val >>= 10;
					g = val & 0x3ff;
					val >>= 10;
					r = val & 0x3ff;
					
					r <<= 20;
					g <<= 10;
					b <<= 0;
					val = r;
					val |= g;
					val |= b;
					*sptr++ = SwapInt32(val);					
				}
				break;
			case COLOR_FORMAT_AR10: 
				for(x=0; x<width; x++)
				{	
					val = SwapInt32(*sptr);
					val >>= 2;
					b = val & 0x3ff;
					val >>= 10;
					g = val & 0x3ff;
					val >>= 10;
					r = val & 0x3ff;

					r <<= 20;
					g <<= 10;
					b <<= 0;
					val = r;
					val |= g;
					val |= b;
					*sptr++ = val;					
				}
				break;
		}
	}

	return width * 4 * height;
}


int ConvertBYR4ToPacked(uint8_t *data, int pitch, int width, int height, uint8_t *buffer, int bayer_format)
{
	int row,x;


	for(row=0; row<height; row++)
	{
		PIXEL16U *sptr1, *sptr2;
		uint8_t *outB, *outN;
		uint8_t *outBR, *outNR;
		uint8_t *outBG1, *outNG1;
		uint8_t *outBG2, *outNG2;
		uint8_t *outBB, *outNB;

		sptr1 = (PIXEL16U *)data;
		sptr1 += row * (pitch>>1);
		sptr2 = sptr1 + (pitch>>2);

		outB = (uint8_t *)buffer;
		outB += row * width * 4 * 3/2; //12-bit
		outN = outB;
		outN += width * 4;

		outBR = outB;
		outBG1 = outBR + width;
		outBG2 = outBG1 + width;
		outBB = outBG2 + width;
		outNR = outBB + width;
		outNG1 = outNR + (width>>1);
		outNG2 = outNG1 + (width>>1);
		outNB = outNG2 + (width>>1);

#if 1
		{
			__m128i rg_epi16;
			__m128i gb_epi16;
			__m128i gr_epi16;
			__m128i bg_epi16;
			__m128i t_epi16;
			__m128i r_epi16;
			__m128i g1_epi16;
			__m128i g2_epi16;
			__m128i b_epi16;
			__m128i Br_epi16;
			__m128i Bg1_epi16;
			__m128i Bg2_epi16;
			__m128i Bb_epi16;
			__m128i Nr_epi16;
			__m128i Ng1_epi16;
			__m128i Ng2_epi16;
			__m128i Nb_epi16;
			__m128i Brb_epi16;
			__m128i Bg1b_epi16;
			__m128i Bg2b_epi16;
			__m128i Bbb_epi16;
			__m128i Nra_epi16;
			__m128i Ng1a_epi16;
			__m128i Ng2a_epi16;
			__m128i Nba_epi16;
			__m128i Nrb_epi16;
			__m128i Ng1b_epi16;
			__m128i Ng2b_epi16;
			__m128i Nbb_epi16;
			__m128i Nrc_epi16;
			__m128i Ng1c_epi16;
			__m128i Ng2c_epi16;
			__m128i Nbc_epi16;
			__m128i Nrd_epi16;
			__m128i Ng1d_epi16;
			__m128i Ng2d_epi16;
			__m128i Nbd_epi16;
			__m128i ZeroHi = _mm_set1_epi32(0x0000ffff);
			__m128i MaskHi = _mm_set1_epi16(0x00f0);

			__m128i *src1_epi16 = (__m128i *)sptr1;
			__m128i *src2_epi16 = (__m128i *)sptr2;
			__m128i *outBR_epi16 = (__m128i *)outBR;
			__m128i *outBG1_epi16 = (__m128i *)outBG1;
			__m128i *outBG2_epi16 = (__m128i *)outBG2;
			__m128i *outBB_epi16 = (__m128i *)outBB;
			__m128i *outNR_epi16 = (__m128i *)outNR;
			__m128i *outNG1_epi16 = (__m128i *)outNG1;
			__m128i *outNG2_epi16 = (__m128i *)outNG2;
			__m128i *outNB_epi16 = (__m128i *)outNB;


			switch(bayer_format)
			{
				case BAYER_FORMAT_RED_GRN: //Red-grn phase
					for(x=0; x<width; x+=32) //R,G1,R,G1... G2,B,G2,B...
					{
					/*	int xx;
						sptr1 = (PIXEL16U *)src1_epi16;
						sptr2 = (PIXEL16U *)src2_epi16;
						for(xx=0;xx<32;xx++)
						{
							sptr1[xx*2] = (x+xx)<<4;
							sptr1[xx*2+1] = (x+xx)<<4;
							sptr2[xx*2] = (x+xx)<<4;
							sptr2[xx*2+1] = (x+xx)<<4;
						}*/

						rg_epi16 = _mm_load_si128(src1_epi16++);
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);	 //shift 16-bit to 12-bit
						g1_epi16 = _mm_srli_epi32(rg_epi16, 16);
						r_epi16 = _mm_and_si128(rg_epi16, ZeroHi);
						rg_epi16 = _mm_load_si128(src1_epi16++);
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(rg_epi16, 16);
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);
						t_epi16 = _mm_and_si128(rg_epi16, ZeroHi);
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);


						gb_epi16 = _mm_load_si128(src2_epi16++);
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);	 //shift 16-bit to 12-bit
						b_epi16 = _mm_srli_epi32(gb_epi16, 16);
						g2_epi16 = _mm_and_si128(gb_epi16, ZeroHi);
						gb_epi16 = _mm_load_si128(src2_epi16++);
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gb_epi16, 16);
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);
						t_epi16 = _mm_and_si128(gb_epi16, ZeroHi);
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);


						Br_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_and_si128(Nra_epi16, MaskHi); //0x00f0
						Bg1_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_and_si128(Ng1a_epi16, MaskHi); //0x00f0
						Bg2_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_and_si128(Ng2a_epi16, MaskHi); //0x00f0
						Bb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_and_si128(Nba_epi16, MaskHi); //0x00f0



						rg_epi16 = _mm_load_si128(src1_epi16++);
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);	 //shift 16-bit to 12-bit
						g1_epi16 = _mm_srli_epi32(rg_epi16, 16);
						r_epi16 = _mm_and_si128(rg_epi16, ZeroHi);
						rg_epi16 = _mm_load_si128(src1_epi16++);
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(rg_epi16, 16);
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);
						t_epi16 = _mm_and_si128(rg_epi16, ZeroHi);
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);


						gb_epi16 = _mm_load_si128(src2_epi16++);
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);	 //shift 16-bit to 12-bit
						b_epi16 = _mm_srli_epi32(gb_epi16, 16);
						g2_epi16 = _mm_and_si128(gb_epi16, ZeroHi);
						gb_epi16 = _mm_load_si128(src2_epi16++);
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gb_epi16, 16);
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);
						t_epi16 = _mm_and_si128(gb_epi16, ZeroHi);
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);

						Brb_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_and_si128(Nrb_epi16, MaskHi); //0x00f0
						Bg1b_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_and_si128(Ng1b_epi16, MaskHi); //0x00f0
						Bg2b_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_and_si128(Ng2b_epi16, MaskHi); //0x00f0
						Bbb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_and_si128(Nbb_epi16, MaskHi); //0x00f0


						Br_epi16 = _mm_packus_epi16(Br_epi16, Brb_epi16);
						Bg1_epi16 = _mm_packus_epi16(Bg1_epi16, Bg1b_epi16);
						Bg2_epi16 = _mm_packus_epi16(Bg2_epi16, Bg2b_epi16);
						Bb_epi16 = _mm_packus_epi16(Bb_epi16, Bbb_epi16);

						_mm_store_si128(outBR_epi16++, Br_epi16);
						_mm_store_si128(outBG1_epi16++, Bg1_epi16);
						_mm_store_si128(outBG2_epi16++, Bg2_epi16);
						_mm_store_si128(outBB_epi16++, Bb_epi16);

						Nrc_epi16 = _mm_packus_epi16(Nra_epi16,Nrb_epi16);
						Ng1c_epi16 = _mm_packus_epi16(Ng1a_epi16,Ng1b_epi16);
						Ng2c_epi16 = _mm_packus_epi16(Ng2a_epi16,Ng2b_epi16);
						Nbc_epi16 = _mm_packus_epi16(Nba_epi16,Nbb_epi16);


						rg_epi16 = _mm_load_si128(src1_epi16++);
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);	 //shift 16-bit to 12-bit
						g1_epi16 = _mm_srli_epi32(rg_epi16, 16);
						r_epi16 = _mm_and_si128(rg_epi16, ZeroHi);
						rg_epi16 = _mm_load_si128(src1_epi16++);
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(rg_epi16, 16);
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);
						t_epi16 = _mm_and_si128(rg_epi16, ZeroHi);
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);


						gb_epi16 = _mm_load_si128(src2_epi16++);
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);	 //shift 16-bit to 12-bit
						b_epi16 = _mm_srli_epi32(gb_epi16, 16);
						g2_epi16 = _mm_and_si128(gb_epi16, ZeroHi);
						gb_epi16 = _mm_load_si128(src2_epi16++);
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gb_epi16, 16);
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);
						t_epi16 = _mm_and_si128(gb_epi16, ZeroHi);
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);

						Br_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_and_si128(Nra_epi16, MaskHi); //0x00f0
						Bg1_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_and_si128(Ng1a_epi16, MaskHi); //0x00f0
						Bg2_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_and_si128(Ng2a_epi16, MaskHi); //0x00f0
						Bb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_and_si128(Nba_epi16, MaskHi); //0x00f0



						rg_epi16 = _mm_load_si128(src1_epi16++);
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);	 //shift 16-bit to 12-bit
						g1_epi16 = _mm_srli_epi32(rg_epi16, 16);
						r_epi16 = _mm_and_si128(rg_epi16, ZeroHi);
						rg_epi16 = _mm_load_si128(src1_epi16++);
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(rg_epi16, 16);
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);
						t_epi16 = _mm_and_si128(rg_epi16, ZeroHi);
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);


						gb_epi16 = _mm_load_si128(src2_epi16++);
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);	 //shift 16-bit to 12-bit
						b_epi16 = _mm_srli_epi32(gb_epi16, 16);
						g2_epi16 = _mm_and_si128(gb_epi16, ZeroHi);
						gb_epi16 = _mm_load_si128(src2_epi16++);
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gb_epi16, 16);
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);
						t_epi16 = _mm_and_si128(gb_epi16, ZeroHi);
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);

						Brb_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_and_si128(Nrb_epi16, MaskHi); //0x00f0
						Bg1b_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_and_si128(Ng1b_epi16, MaskHi); //0x00f0
						Bg2b_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_and_si128(Ng2b_epi16, MaskHi); //0x00f0
						Bbb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_and_si128(Nbb_epi16, MaskHi); //0x00f0


						Br_epi16 = _mm_packus_epi16(Br_epi16, Brb_epi16);
						Bg1_epi16 = _mm_packus_epi16(Bg1_epi16, Bg1b_epi16);
						Bg2_epi16 = _mm_packus_epi16(Bg2_epi16, Bg2b_epi16);
						Bb_epi16 = _mm_packus_epi16(Bb_epi16, Bbb_epi16);

						_mm_store_si128(outBR_epi16++, Br_epi16);
						_mm_store_si128(outBG1_epi16++, Bg1_epi16);
						_mm_store_si128(outBG2_epi16++, Bg2_epi16);
						_mm_store_si128(outBB_epi16++, Bb_epi16);

						Nrd_epi16 = _mm_packus_epi16(Nra_epi16,Nrb_epi16);
						Ng1d_epi16 = _mm_packus_epi16(Ng1a_epi16,Ng1b_epi16);
						Ng2d_epi16 = _mm_packus_epi16(Ng2a_epi16,Ng2b_epi16);
						Nbd_epi16 = _mm_packus_epi16(Nba_epi16,Nbb_epi16);


						Nra_epi16 = _mm_and_si128(Nrc_epi16, MaskHi); //00600040002000000
						Nrc_epi16 = _mm_srli_epi16(Nrc_epi16, 8);	  //00706050403020100
						Nrc_epi16 = _mm_and_si128(Nrc_epi16, MaskHi); //00700050003000100
						Nrb_epi16 = _mm_and_si128(Nrd_epi16, MaskHi); //00e000c000a000800
						Nrd_epi16 = _mm_srli_epi16(Nrd_epi16, 8);	  //00f0e0d0c0b0a0908
						Nrd_epi16 = _mm_and_si128(Nrd_epi16, MaskHi); //00f000d000b000900
						Nra_epi16 = _mm_packus_epi16(Nra_epi16,Nrb_epi16);
						Nra_epi16 = _mm_srli_epi16(Nra_epi16,4);
						Nrb_epi16 = _mm_packus_epi16(Nrc_epi16,Nrd_epi16);
						Nr_epi16 = _mm_or_si128(Nra_epi16, Nrb_epi16);

						Ng1a_epi16 = _mm_and_si128(Ng1c_epi16, MaskHi); //00600040002000000
						Ng1c_epi16 = _mm_srli_epi16(Ng1c_epi16, 8);	  //00706050403020100
						Ng1c_epi16 = _mm_and_si128(Ng1c_epi16, MaskHi); //00700050003000100
						Ng1b_epi16 = _mm_and_si128(Ng1d_epi16, MaskHi); //00e000c000a000800
						Ng1d_epi16 = _mm_srli_epi16(Ng1d_epi16, 8);	  //00f0e0d0c0b0a0908
						Ng1d_epi16 = _mm_and_si128(Ng1d_epi16, MaskHi); //00f000d000b000900
						Ng1a_epi16 = _mm_packus_epi16(Ng1a_epi16,Ng1b_epi16);
						Ng1a_epi16 = _mm_srli_epi16(Ng1a_epi16,4);
						Ng1b_epi16 = _mm_packus_epi16(Ng1c_epi16,Ng1d_epi16);
						Ng1_epi16 = _mm_or_si128(Ng1a_epi16, Ng1b_epi16);

						Ng2a_epi16 = _mm_and_si128(Ng2c_epi16, MaskHi); //00600040002000000
						Ng2c_epi16 = _mm_srli_epi16(Ng2c_epi16, 8);	  //00706050403020100
						Ng2c_epi16 = _mm_and_si128(Ng2c_epi16, MaskHi); //00700050003000100
						Ng2b_epi16 = _mm_and_si128(Ng2d_epi16, MaskHi); //00e000c000a000800
						Ng2d_epi16 = _mm_srli_epi16(Ng2d_epi16, 8);	  //00f0e0d0c0b0a0908
						Ng2d_epi16 = _mm_and_si128(Ng2d_epi16, MaskHi); //00f000d000b000900
						Ng2a_epi16 = _mm_packus_epi16(Ng2a_epi16,Ng2b_epi16);
						Ng2a_epi16 = _mm_srli_epi16(Ng2a_epi16,4);
						Ng2b_epi16 = _mm_packus_epi16(Ng2c_epi16,Ng2d_epi16);
						Ng2_epi16 = _mm_or_si128(Ng2a_epi16, Ng2b_epi16);

						Nba_epi16 = _mm_and_si128(Nbc_epi16, MaskHi); //00600040002000000
						Nbc_epi16 = _mm_srli_epi16(Nbc_epi16, 8);	  //00706050403020100
						Nbc_epi16 = _mm_and_si128(Nbc_epi16, MaskHi); //00700050003000100
						Nbb_epi16 = _mm_and_si128(Nbd_epi16, MaskHi); //00e000c000a000800
						Nbd_epi16 = _mm_srli_epi16(Nbd_epi16, 8);	  //00f0e0d0c0b0a0908
						Nbd_epi16 = _mm_and_si128(Nbd_epi16, MaskHi); //00f000d000b000900
						Nba_epi16 = _mm_packus_epi16(Nba_epi16,Nbb_epi16);
						Nba_epi16 = _mm_srli_epi16(Nba_epi16,4);
						Nbb_epi16 = _mm_packus_epi16(Nbc_epi16,Nbd_epi16);
						Nb_epi16 = _mm_or_si128(Nba_epi16, Nbb_epi16);


						_mm_store_si128(outNR_epi16++, Nr_epi16);
						_mm_store_si128(outNG1_epi16++, Ng1_epi16);
						_mm_store_si128(outNG2_epi16++, Ng2_epi16);
						_mm_store_si128(outNB_epi16++, Nb_epi16);

					}
					break;
				case BAYER_FORMAT_GRN_RED:// grn-red
					for(x=0; x<width; x+=32) //R,G1,R,G1... G2,B,G2,B...
					{
					/*	int xx;
						sptr1 = (PIXEL16U *)src1_epi16;
						sptr2 = (PIXEL16U *)src2_epi16;
						for(xx=0;xx<32;xx++)
						{
							sptr1[xx*2] = (x+xx)<<4;
							sptr1[xx*2+1] = (x+xx)<<4;
							sptr2[xx*2] = (x+xx)<<4;
							sptr2[xx*2+1] = (x+xx)<<4;
						}*/

						rg_epi16 = _mm_load_si128(src1_epi16++);		//r,g,r,g,r,g,r,g
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);			//shift 16-bit to 12-bit
						r_epi16 = _mm_srli_epi32(rg_epi16, 16);			//0,r,0,r,0,r,0,r
						g1_epi16 = _mm_and_si128(rg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						rg_epi16 = _mm_load_si128(src1_epi16++);  		//r,g,r,g,r,g,r,g
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(rg_epi16, 16);			//0,r,0,r,0,r,0,r
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);	//r,r,r,r,r,r,r,r
						t_epi16 = _mm_and_si128(rg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);	//g,g,g,g,g,g,g,g


						gb_epi16 = _mm_load_si128(src2_epi16++);		//g,b,g,b,g,b,g,b
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);			//shift 16-bit to 12-bit
						g2_epi16 = _mm_srli_epi32(gb_epi16, 16);		//0,g,0,g,0,g,0,g
						b_epi16 = _mm_and_si128(gb_epi16, ZeroHi);		//0,b,0,b,0,b,0,b
						gb_epi16 = _mm_load_si128(src2_epi16++);  		//g,b,g,b,g,b,g,b
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gb_epi16, 16);			//0,g,0,g,0,g,0,g
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);	//g,g,g,g,g,g,g,g
						t_epi16 = _mm_and_si128(gb_epi16, ZeroHi);		//0,b,0,b,0,b,0,b
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);	//b,b,b,b,b,b,b,b


						Br_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_and_si128(Nra_epi16, MaskHi); //0x00f0
						Bg1_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_and_si128(Ng1a_epi16, MaskHi); //0x00f0
						Bg2_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_and_si128(Ng2a_epi16, MaskHi); //0x00f0
						Bb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_and_si128(Nba_epi16, MaskHi); //0x00f0



						rg_epi16 = _mm_load_si128(src1_epi16++);		//r,g,r,g,r,g,r,g
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);			//shift 16-bit to 12-bit
						r_epi16 = _mm_srli_epi32(rg_epi16, 16);			//0,r,0,r,0,r,0,r
						g1_epi16 = _mm_and_si128(rg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						rg_epi16 = _mm_load_si128(src1_epi16++);  		//r,g,r,g,r,g,r,g
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(rg_epi16, 16);			//0,r,0,r,0,r,0,r
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);	//r,r,r,r,r,r,r,r
						t_epi16 = _mm_and_si128(rg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);	//g,g,g,g,g,g,g,g


						gb_epi16 = _mm_load_si128(src2_epi16++);		//g,b,g,b,g,b,g,b
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);			//shift 16-bit to 12-bit
						g2_epi16 = _mm_srli_epi32(gb_epi16, 16);		//0,g,0,g,0,g,0,g
						b_epi16 = _mm_and_si128(gb_epi16, ZeroHi);		//0,b,0,b,0,b,0,b
						gb_epi16 = _mm_load_si128(src2_epi16++);  		//g,b,g,b,g,b,g,b
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gb_epi16, 16);			//0,g,0,g,0,g,0,g
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);	//g,g,g,g,g,g,g,g
						t_epi16 = _mm_and_si128(gb_epi16, ZeroHi);		//0,b,0,b,0,b,0,b
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);	//b,b,b,b,b,b,b,b

						Brb_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_and_si128(Nrb_epi16, MaskHi); //0x00f0
						Bg1b_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_and_si128(Ng1b_epi16, MaskHi); //0x00f0
						Bg2b_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_and_si128(Ng2b_epi16, MaskHi); //0x00f0
						Bbb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_and_si128(Nbb_epi16, MaskHi); //0x00f0


						Br_epi16 = _mm_packus_epi16(Br_epi16, Brb_epi16);
						Bg1_epi16 = _mm_packus_epi16(Bg1_epi16, Bg1b_epi16);
						Bg2_epi16 = _mm_packus_epi16(Bg2_epi16, Bg2b_epi16);
						Bb_epi16 = _mm_packus_epi16(Bb_epi16, Bbb_epi16);

						_mm_store_si128(outBR_epi16++, Br_epi16);
						_mm_store_si128(outBG1_epi16++, Bg1_epi16);
						_mm_store_si128(outBG2_epi16++, Bg2_epi16);
						_mm_store_si128(outBB_epi16++, Bb_epi16);

						Nrc_epi16 = _mm_packus_epi16(Nra_epi16,Nrb_epi16);
						Ng1c_epi16 = _mm_packus_epi16(Ng1a_epi16,Ng1b_epi16);
						Ng2c_epi16 = _mm_packus_epi16(Ng2a_epi16,Ng2b_epi16);
						Nbc_epi16 = _mm_packus_epi16(Nba_epi16,Nbb_epi16);


						rg_epi16 = _mm_load_si128(src1_epi16++);		//r,g,r,g,r,g,r,g
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);			//shift 16-bit to 12-bit
						r_epi16 = _mm_srli_epi32(rg_epi16, 16);			//0,r,0,r,0,r,0,r
						g1_epi16 = _mm_and_si128(rg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						rg_epi16 = _mm_load_si128(src1_epi16++);  		//r,g,r,g,r,g,r,g
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(rg_epi16, 16);			//0,r,0,r,0,r,0,r
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);	//r,r,r,r,r,r,r,r
						t_epi16 = _mm_and_si128(rg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);	//g,g,g,g,g,g,g,g


						gb_epi16 = _mm_load_si128(src2_epi16++);		//g,b,g,b,g,b,g,b
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);			//shift 16-bit to 12-bit
						g2_epi16 = _mm_srli_epi32(gb_epi16, 16);		//0,g,0,g,0,g,0,g
						b_epi16 = _mm_and_si128(gb_epi16, ZeroHi);		//0,b,0,b,0,b,0,b
						gb_epi16 = _mm_load_si128(src2_epi16++);  		//g,b,g,b,g,b,g,b
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gb_epi16, 16);			//0,g,0,g,0,g,0,g
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);	//g,g,g,g,g,g,g,g
						t_epi16 = _mm_and_si128(gb_epi16, ZeroHi);		//0,b,0,b,0,b,0,b
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);	//b,b,b,b,b,b,b,b

						Br_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_and_si128(Nra_epi16, MaskHi); //0x00f0
						Bg1_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_and_si128(Ng1a_epi16, MaskHi); //0x00f0
						Bg2_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_and_si128(Ng2a_epi16, MaskHi); //0x00f0
						Bb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_and_si128(Nba_epi16, MaskHi); //0x00f0



						rg_epi16 = _mm_load_si128(src1_epi16++);		//r,g,r,g,r,g,r,g
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);			//shift 16-bit to 12-bit
						r_epi16 = _mm_srli_epi32(rg_epi16, 16);			//0,r,0,r,0,r,0,r
						g1_epi16 = _mm_and_si128(rg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						rg_epi16 = _mm_load_si128(src1_epi16++);  		//r,g,r,g,r,g,r,g
						rg_epi16 = _mm_srli_epi16(rg_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(rg_epi16, 16);			//0,r,0,r,0,r,0,r
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);	//r,r,r,r,r,r,r,r
						t_epi16 = _mm_and_si128(rg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);	//g,g,g,g,g,g,g,g


						gb_epi16 = _mm_load_si128(src2_epi16++);		//g,b,g,b,g,b,g,b
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);			//shift 16-bit to 12-bit
						g2_epi16 = _mm_srli_epi32(gb_epi16, 16);		//0,g,0,g,0,g,0,g
						b_epi16 = _mm_and_si128(gb_epi16, ZeroHi);		//0,b,0,b,0,b,0,b
						gb_epi16 = _mm_load_si128(src2_epi16++);  		//g,b,g,b,g,b,g,b
						gb_epi16 = _mm_srli_epi16(gb_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gb_epi16, 16);			//0,g,0,g,0,g,0,g
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);	//g,g,g,g,g,g,g,g
						t_epi16 = _mm_and_si128(gb_epi16, ZeroHi);		//0,b,0,b,0,b,0,b
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);	//b,b,b,b,b,b,b,b

						Brb_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_and_si128(Nrb_epi16, MaskHi); //0x00f0
						Bg1b_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_and_si128(Ng1b_epi16, MaskHi); //0x00f0
						Bg2b_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_and_si128(Ng2b_epi16, MaskHi); //0x00f0
						Bbb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_and_si128(Nbb_epi16, MaskHi); //0x00f0


						Br_epi16 = _mm_packus_epi16(Br_epi16, Brb_epi16);
						Bg1_epi16 = _mm_packus_epi16(Bg1_epi16, Bg1b_epi16);
						Bg2_epi16 = _mm_packus_epi16(Bg2_epi16, Bg2b_epi16);
						Bb_epi16 = _mm_packus_epi16(Bb_epi16, Bbb_epi16);

						_mm_store_si128(outBR_epi16++, Br_epi16);
						_mm_store_si128(outBG1_epi16++, Bg1_epi16);
						_mm_store_si128(outBG2_epi16++, Bg2_epi16);
						_mm_store_si128(outBB_epi16++, Bb_epi16);

						Nrd_epi16 = _mm_packus_epi16(Nra_epi16,Nrb_epi16);
						Ng1d_epi16 = _mm_packus_epi16(Ng1a_epi16,Ng1b_epi16);
						Ng2d_epi16 = _mm_packus_epi16(Ng2a_epi16,Ng2b_epi16);
						Nbd_epi16 = _mm_packus_epi16(Nba_epi16,Nbb_epi16);


						Nra_epi16 = _mm_and_si128(Nrc_epi16, MaskHi); //00600040002000000
						Nrc_epi16 = _mm_srli_epi16(Nrc_epi16, 8);	  //00706050403020100
						Nrc_epi16 = _mm_and_si128(Nrc_epi16, MaskHi); //00700050003000100
						Nrb_epi16 = _mm_and_si128(Nrd_epi16, MaskHi); //00e000c000a000800
						Nrd_epi16 = _mm_srli_epi16(Nrd_epi16, 8);	  //00f0e0d0c0b0a0908
						Nrd_epi16 = _mm_and_si128(Nrd_epi16, MaskHi); //00f000d000b000900
						Nra_epi16 = _mm_packus_epi16(Nra_epi16,Nrb_epi16);
						Nra_epi16 = _mm_srli_epi16(Nra_epi16,4);
						Nrb_epi16 = _mm_packus_epi16(Nrc_epi16,Nrd_epi16);
						Nr_epi16 = _mm_or_si128(Nra_epi16, Nrb_epi16);

						Ng1a_epi16 = _mm_and_si128(Ng1c_epi16, MaskHi); //00600040002000000
						Ng1c_epi16 = _mm_srli_epi16(Ng1c_epi16, 8);	  //00706050403020100
						Ng1c_epi16 = _mm_and_si128(Ng1c_epi16, MaskHi); //00700050003000100
						Ng1b_epi16 = _mm_and_si128(Ng1d_epi16, MaskHi); //00e000c000a000800
						Ng1d_epi16 = _mm_srli_epi16(Ng1d_epi16, 8);	  //00f0e0d0c0b0a0908
						Ng1d_epi16 = _mm_and_si128(Ng1d_epi16, MaskHi); //00f000d000b000900
						Ng1a_epi16 = _mm_packus_epi16(Ng1a_epi16,Ng1b_epi16);
						Ng1a_epi16 = _mm_srli_epi16(Ng1a_epi16,4);
						Ng1b_epi16 = _mm_packus_epi16(Ng1c_epi16,Ng1d_epi16);
						Ng1_epi16 = _mm_or_si128(Ng1a_epi16, Ng1b_epi16);

						Ng2a_epi16 = _mm_and_si128(Ng2c_epi16, MaskHi); //00600040002000000
						Ng2c_epi16 = _mm_srli_epi16(Ng2c_epi16, 8);	  //00706050403020100
						Ng2c_epi16 = _mm_and_si128(Ng2c_epi16, MaskHi); //00700050003000100
						Ng2b_epi16 = _mm_and_si128(Ng2d_epi16, MaskHi); //00e000c000a000800
						Ng2d_epi16 = _mm_srli_epi16(Ng2d_epi16, 8);	  //00f0e0d0c0b0a0908
						Ng2d_epi16 = _mm_and_si128(Ng2d_epi16, MaskHi); //00f000d000b000900
						Ng2a_epi16 = _mm_packus_epi16(Ng2a_epi16,Ng2b_epi16);
						Ng2a_epi16 = _mm_srli_epi16(Ng2a_epi16,4);
						Ng2b_epi16 = _mm_packus_epi16(Ng2c_epi16,Ng2d_epi16);
						Ng2_epi16 = _mm_or_si128(Ng2a_epi16, Ng2b_epi16);

						Nba_epi16 = _mm_and_si128(Nbc_epi16, MaskHi); //00600040002000000
						Nbc_epi16 = _mm_srli_epi16(Nbc_epi16, 8);	  //00706050403020100
						Nbc_epi16 = _mm_and_si128(Nbc_epi16, MaskHi); //00700050003000100
						Nbb_epi16 = _mm_and_si128(Nbd_epi16, MaskHi); //00e000c000a000800
						Nbd_epi16 = _mm_srli_epi16(Nbd_epi16, 8);	  //00f0e0d0c0b0a0908
						Nbd_epi16 = _mm_and_si128(Nbd_epi16, MaskHi); //00f000d000b000900
						Nba_epi16 = _mm_packus_epi16(Nba_epi16,Nbb_epi16);
						Nba_epi16 = _mm_srli_epi16(Nba_epi16,4);
						Nbb_epi16 = _mm_packus_epi16(Nbc_epi16,Nbd_epi16);
						Nb_epi16 = _mm_or_si128(Nba_epi16, Nbb_epi16);


						_mm_store_si128(outNR_epi16++, Nr_epi16);
						_mm_store_si128(outNG1_epi16++, Ng1_epi16);
						_mm_store_si128(outNG2_epi16++, Ng2_epi16);
						_mm_store_si128(outNB_epi16++, Nb_epi16);

					}
					break;
				case BAYER_FORMAT_GRN_BLU:
					for(x=0; x<width; x+=32) //G1,B,G1,B... R,G2,R,G2...
					{
					/*	int xx;
						sptr1 = (PIXEL16U *)src1_epi16;
						sptr2 = (PIXEL16U *)src2_epi16;
						for(xx=0;xx<32;xx++)
						{
							sptr1[xx*2] = (x+xx)<<4;
							sptr1[xx*2+1] = (x+xx)<<4;
							sptr2[xx*2] = (x+xx)<<4;
							sptr2[xx*2+1] = (x+xx)<<4;
						}*/

						bg_epi16 = _mm_load_si128(src1_epi16++);		//b,g,b,g,b,g,b,g
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);			//shift 16-bit to 12-bit
						b_epi16 = _mm_srli_epi32(bg_epi16, 16);			//0,b,0,b,0,b,0,b
						g1_epi16 = _mm_and_si128(bg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						bg_epi16 = _mm_load_si128(src1_epi16++);  		//b,g,b,g,b,g,b,g
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(bg_epi16, 16);			//0,b,0,b,0,b,0,b
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);	//b,b,b,b,b,b,b,b
						t_epi16 = _mm_and_si128(bg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);	//g,g,g,g,g,g,g,g


						gr_epi16 = _mm_load_si128(src2_epi16++);		//g,r,g,r,g,r,g,r
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);			//shift 16-bit to 12-bit
						g2_epi16 = _mm_srli_epi32(gr_epi16, 16);		//0,g,0,g,0,g,0,g
						r_epi16 = _mm_and_si128(gr_epi16, ZeroHi);		//0,r,0,r,0,r,0,r
						gr_epi16 = _mm_load_si128(src2_epi16++);  		//g,r,g,r,g,r,g,r
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gr_epi16, 16);			//0,g,0,g,0,g,0,g
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);	//g,g,g,g,g,g,g,g
						t_epi16 = _mm_and_si128(gr_epi16, ZeroHi);		//0,r,0,r,0,r,0,r
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);	//r,r,r,r,r,r,r,r


						Br_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_and_si128(Nra_epi16, MaskHi); //0x00f0
						Bg1_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_and_si128(Ng1a_epi16, MaskHi); //0x00f0
						Bg2_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_and_si128(Ng2a_epi16, MaskHi); //0x00f0
						Bb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_and_si128(Nba_epi16, MaskHi); //0x00f0



						bg_epi16 = _mm_load_si128(src1_epi16++);		//b,g,b,g,b,g,b,g
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);			//shift 16-bit to 12-bit
						b_epi16 = _mm_srli_epi32(bg_epi16, 16);			//0,b,0,b,0,b,0,b
						g1_epi16 = _mm_and_si128(bg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						bg_epi16 = _mm_load_si128(src1_epi16++);  		//b,g,b,g,b,g,b,g
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(bg_epi16, 16);			//0,b,0,b,0,b,0,b
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);	//b,b,b,b,b,b,b,b
						t_epi16 = _mm_and_si128(bg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);	//g,g,g,g,g,g,g,g


						gr_epi16 = _mm_load_si128(src2_epi16++);		//g,r,g,r,g,r,g,r
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);			//shift 16-bit to 12-bit
						g2_epi16 = _mm_srli_epi32(gr_epi16, 16);		//0,g,0,g,0,g,0,g
						r_epi16 = _mm_and_si128(gr_epi16, ZeroHi);		//0,r,0,r,0,r,0,r
						gr_epi16 = _mm_load_si128(src2_epi16++);  		//g,r,g,r,g,r,g,r
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gr_epi16, 16);			//0,g,0,g,0,g,0,g
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);	//g,g,g,g,g,g,g,g
						t_epi16 = _mm_and_si128(gr_epi16, ZeroHi);		//0,r,0,r,0,r,0,r
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);	//r,r,r,r,r,r,r,r


						Brb_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_and_si128(Nrb_epi16, MaskHi); //0x00f0
						Bg1b_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_and_si128(Ng1b_epi16, MaskHi); //0x00f0
						Bg2b_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_and_si128(Ng2b_epi16, MaskHi); //0x00f0
						Bbb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_and_si128(Nbb_epi16, MaskHi); //0x00f0


						Br_epi16 = _mm_packus_epi16(Br_epi16, Brb_epi16);
						Bg1_epi16 = _mm_packus_epi16(Bg1_epi16, Bg1b_epi16);
						Bg2_epi16 = _mm_packus_epi16(Bg2_epi16, Bg2b_epi16);
						Bb_epi16 = _mm_packus_epi16(Bb_epi16, Bbb_epi16);

						_mm_store_si128(outBR_epi16++, Br_epi16);
						_mm_store_si128(outBG1_epi16++, Bg1_epi16);
						_mm_store_si128(outBG2_epi16++, Bg2_epi16);
						_mm_store_si128(outBB_epi16++, Bb_epi16);

						Nrc_epi16 = _mm_packus_epi16(Nra_epi16,Nrb_epi16);
						Ng1c_epi16 = _mm_packus_epi16(Ng1a_epi16,Ng1b_epi16);
						Ng2c_epi16 = _mm_packus_epi16(Ng2a_epi16,Ng2b_epi16);
						Nbc_epi16 = _mm_packus_epi16(Nba_epi16,Nbb_epi16);


						bg_epi16 = _mm_load_si128(src1_epi16++);		//b,g,b,g,b,g,b,g
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);			//shift 16-bit to 12-bit
						b_epi16 = _mm_srli_epi32(bg_epi16, 16);			//0,b,0,b,0,b,0,b
						g1_epi16 = _mm_and_si128(bg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						bg_epi16 = _mm_load_si128(src1_epi16++);  		//b,g,b,g,b,g,b,g
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(bg_epi16, 16);			//0,b,0,b,0,b,0,b
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);	//b,b,b,b,b,b,b,b
						t_epi16 = _mm_and_si128(bg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);	//g,g,g,g,g,g,g,g


						gr_epi16 = _mm_load_si128(src2_epi16++);		//g,r,g,r,g,r,g,r
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);			//shift 16-bit to 12-bit
						g2_epi16 = _mm_srli_epi32(gr_epi16, 16);		//0,g,0,g,0,g,0,g
						r_epi16 = _mm_and_si128(gr_epi16, ZeroHi);		//0,r,0,r,0,r,0,r
						gr_epi16 = _mm_load_si128(src2_epi16++);  		//g,r,g,r,g,r,g,r
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gr_epi16, 16);			//0,g,0,g,0,g,0,g
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);	//g,g,g,g,g,g,g,g
						t_epi16 = _mm_and_si128(gr_epi16, ZeroHi);		//0,r,0,r,0,r,0,r
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);	//r,r,r,r,r,r,r,r

						Br_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_and_si128(Nra_epi16, MaskHi); //0x00f0
						Bg1_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_and_si128(Ng1a_epi16, MaskHi); //0x00f0
						Bg2_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_and_si128(Ng2a_epi16, MaskHi); //0x00f0
						Bb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_and_si128(Nba_epi16, MaskHi); //0x00f0



						bg_epi16 = _mm_load_si128(src1_epi16++);		//b,g,b,g,b,g,b,g
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);			//shift 16-bit to 12-bit
						b_epi16 = _mm_srli_epi32(bg_epi16, 16);			//0,b,0,b,0,b,0,b
						g1_epi16 = _mm_and_si128(bg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						bg_epi16 = _mm_load_si128(src1_epi16++);  		//b,g,b,g,b,g,b,g
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(bg_epi16, 16);			//0,b,0,b,0,b,0,b
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);	//b,b,b,b,b,b,b,b
						t_epi16 = _mm_and_si128(bg_epi16, ZeroHi);		//0,g,0,g,0,g,0,g
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);	//g,g,g,g,g,g,g,g


						gr_epi16 = _mm_load_si128(src2_epi16++);		//g,r,g,r,g,r,g,r
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);			//shift 16-bit to 12-bit
						g2_epi16 = _mm_srli_epi32(gr_epi16, 16);		//0,g,0,g,0,g,0,g
						r_epi16 = _mm_and_si128(gr_epi16, ZeroHi);		//0,r,0,r,0,r,0,r
						gr_epi16 = _mm_load_si128(src2_epi16++);  		//g,r,g,r,g,r,g,r
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);			//shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gr_epi16, 16);			//0,g,0,g,0,g,0,g
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);	//g,g,g,g,g,g,g,g
						t_epi16 = _mm_and_si128(gr_epi16, ZeroHi);		//0,r,0,r,0,r,0,r
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);	//r,r,r,r,r,r,r,r

						Brb_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_and_si128(Nrb_epi16, MaskHi); //0x00f0
						Bg1b_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_and_si128(Ng1b_epi16, MaskHi); //0x00f0
						Bg2b_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_and_si128(Ng2b_epi16, MaskHi); //0x00f0
						Bbb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_and_si128(Nbb_epi16, MaskHi); //0x00f0


						Br_epi16 = _mm_packus_epi16(Br_epi16, Brb_epi16);
						Bg1_epi16 = _mm_packus_epi16(Bg1_epi16, Bg1b_epi16);
						Bg2_epi16 = _mm_packus_epi16(Bg2_epi16, Bg2b_epi16);
						Bb_epi16 = _mm_packus_epi16(Bb_epi16, Bbb_epi16);

						_mm_store_si128(outBR_epi16++, Br_epi16);
						_mm_store_si128(outBG1_epi16++, Bg1_epi16);
						_mm_store_si128(outBG2_epi16++, Bg2_epi16);
						_mm_store_si128(outBB_epi16++, Bb_epi16);

						Nrd_epi16 = _mm_packus_epi16(Nra_epi16,Nrb_epi16);
						Ng1d_epi16 = _mm_packus_epi16(Ng1a_epi16,Ng1b_epi16);
						Ng2d_epi16 = _mm_packus_epi16(Ng2a_epi16,Ng2b_epi16);
						Nbd_epi16 = _mm_packus_epi16(Nba_epi16,Nbb_epi16);


						Nra_epi16 = _mm_and_si128(Nrc_epi16, MaskHi); //00600040002000000
						Nrc_epi16 = _mm_srli_epi16(Nrc_epi16, 8);	  //00706050403020100
						Nrc_epi16 = _mm_and_si128(Nrc_epi16, MaskHi); //00700050003000100
						Nrb_epi16 = _mm_and_si128(Nrd_epi16, MaskHi); //00e000c000a000800
						Nrd_epi16 = _mm_srli_epi16(Nrd_epi16, 8);	  //00f0e0d0c0b0a0908
						Nrd_epi16 = _mm_and_si128(Nrd_epi16, MaskHi); //00f000d000b000900
						Nra_epi16 = _mm_packus_epi16(Nra_epi16,Nrb_epi16);
						Nra_epi16 = _mm_srli_epi16(Nra_epi16,4);
						Nrb_epi16 = _mm_packus_epi16(Nrc_epi16,Nrd_epi16);
						Nr_epi16 = _mm_or_si128(Nra_epi16, Nrb_epi16);

						Ng1a_epi16 = _mm_and_si128(Ng1c_epi16, MaskHi); //00600040002000000
						Ng1c_epi16 = _mm_srli_epi16(Ng1c_epi16, 8);	  //00706050403020100
						Ng1c_epi16 = _mm_and_si128(Ng1c_epi16, MaskHi); //00700050003000100
						Ng1b_epi16 = _mm_and_si128(Ng1d_epi16, MaskHi); //00e000c000a000800
						Ng1d_epi16 = _mm_srli_epi16(Ng1d_epi16, 8);	  //00f0e0d0c0b0a0908
						Ng1d_epi16 = _mm_and_si128(Ng1d_epi16, MaskHi); //00f000d000b000900
						Ng1a_epi16 = _mm_packus_epi16(Ng1a_epi16,Ng1b_epi16);
						Ng1a_epi16 = _mm_srli_epi16(Ng1a_epi16,4);
						Ng1b_epi16 = _mm_packus_epi16(Ng1c_epi16,Ng1d_epi16);
						Ng1_epi16 = _mm_or_si128(Ng1a_epi16, Ng1b_epi16);

						Ng2a_epi16 = _mm_and_si128(Ng2c_epi16, MaskHi); //00600040002000000
						Ng2c_epi16 = _mm_srli_epi16(Ng2c_epi16, 8);	  //00706050403020100
						Ng2c_epi16 = _mm_and_si128(Ng2c_epi16, MaskHi); //00700050003000100
						Ng2b_epi16 = _mm_and_si128(Ng2d_epi16, MaskHi); //00e000c000a000800
						Ng2d_epi16 = _mm_srli_epi16(Ng2d_epi16, 8);	  //00f0e0d0c0b0a0908
						Ng2d_epi16 = _mm_and_si128(Ng2d_epi16, MaskHi); //00f000d000b000900
						Ng2a_epi16 = _mm_packus_epi16(Ng2a_epi16,Ng2b_epi16);
						Ng2a_epi16 = _mm_srli_epi16(Ng2a_epi16,4);
						Ng2b_epi16 = _mm_packus_epi16(Ng2c_epi16,Ng2d_epi16);
						Ng2_epi16 = _mm_or_si128(Ng2a_epi16, Ng2b_epi16);

						Nba_epi16 = _mm_and_si128(Nbc_epi16, MaskHi); //00600040002000000
						Nbc_epi16 = _mm_srli_epi16(Nbc_epi16, 8);	  //00706050403020100
						Nbc_epi16 = _mm_and_si128(Nbc_epi16, MaskHi); //00700050003000100
						Nbb_epi16 = _mm_and_si128(Nbd_epi16, MaskHi); //00e000c000a000800
						Nbd_epi16 = _mm_srli_epi16(Nbd_epi16, 8);	  //00f0e0d0c0b0a0908
						Nbd_epi16 = _mm_and_si128(Nbd_epi16, MaskHi); //00f000d000b000900
						Nba_epi16 = _mm_packus_epi16(Nba_epi16,Nbb_epi16);
						Nba_epi16 = _mm_srli_epi16(Nba_epi16,4);
						Nbb_epi16 = _mm_packus_epi16(Nbc_epi16,Nbd_epi16);
						Nb_epi16 = _mm_or_si128(Nba_epi16, Nbb_epi16);


						_mm_store_si128(outNR_epi16++, Nr_epi16);
						_mm_store_si128(outNG1_epi16++, Ng1_epi16);
						_mm_store_si128(outNG2_epi16++, Ng2_epi16);
						_mm_store_si128(outNB_epi16++, Nb_epi16);
					}
					break;

				case BAYER_FORMAT_BLU_GRN:
					for(x=0; x<width; x+=32) //B,G1,B,G1... G2,R,G2,R...
					{
					/*	int xx;
						sptr1 = (PIXEL16U *)src1_epi16;
						sptr2 = (PIXEL16U *)src2_epi16;
						for(xx=0;xx<32;xx++)
						{
							sptr1[xx*2] = (x+xx)<<4;
							sptr1[xx*2+1] = (x+xx)<<4;
							sptr2[xx*2] = (x+xx)<<4;
							sptr2[xx*2+1] = (x+xx)<<4;
						}*/

						bg_epi16 = _mm_load_si128(src1_epi16++);
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);	 //shift 16-bit to 12-bit
						g1_epi16 = _mm_srli_epi32(bg_epi16, 16);
						b_epi16 = _mm_and_si128(bg_epi16, ZeroHi);
						bg_epi16 = _mm_load_si128(src1_epi16++);
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(bg_epi16, 16);
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);
						t_epi16 = _mm_and_si128(bg_epi16, ZeroHi);
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);


						gr_epi16 = _mm_load_si128(src2_epi16++);
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);	 //shift 16-bit to 12-bit
						r_epi16 = _mm_srli_epi32(gr_epi16, 16);
						g2_epi16 = _mm_and_si128(gr_epi16, ZeroHi);
						gr_epi16 = _mm_load_si128(src2_epi16++);
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gr_epi16, 16);
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);
						t_epi16 = _mm_and_si128(gr_epi16, ZeroHi);
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);


						Br_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_and_si128(Nra_epi16, MaskHi); //0x00f0
						Bg1_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_and_si128(Ng1a_epi16, MaskHi); //0x00f0
						Bg2_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_and_si128(Ng2a_epi16, MaskHi); //0x00f0
						Bb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_and_si128(Nba_epi16, MaskHi); //0x00f0



						bg_epi16 = _mm_load_si128(src1_epi16++);
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);	 //shift 16-bit to 12-bit
						g1_epi16 = _mm_srli_epi32(bg_epi16, 16);
						b_epi16 = _mm_and_si128(bg_epi16, ZeroHi);
						bg_epi16 = _mm_load_si128(src1_epi16++);
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(bg_epi16, 16);
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);
						t_epi16 = _mm_and_si128(bg_epi16, ZeroHi);
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);


						gr_epi16 = _mm_load_si128(src2_epi16++);
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);	 //shift 16-bit to 12-bit
						r_epi16 = _mm_srli_epi32(gr_epi16, 16);
						g2_epi16 = _mm_and_si128(gr_epi16, ZeroHi);
						gr_epi16 = _mm_load_si128(src2_epi16++);
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gr_epi16, 16);
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);
						t_epi16 = _mm_and_si128(gr_epi16, ZeroHi);
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);

						Brb_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_and_si128(Nrb_epi16, MaskHi); //0x00f0
						Bg1b_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_and_si128(Ng1b_epi16, MaskHi); //0x00f0
						Bg2b_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_and_si128(Ng2b_epi16, MaskHi); //0x00f0
						Bbb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_and_si128(Nbb_epi16, MaskHi); //0x00f0


						Br_epi16 = _mm_packus_epi16(Br_epi16, Brb_epi16);
						Bg1_epi16 = _mm_packus_epi16(Bg1_epi16, Bg1b_epi16);
						Bg2_epi16 = _mm_packus_epi16(Bg2_epi16, Bg2b_epi16);
						Bb_epi16 = _mm_packus_epi16(Bb_epi16, Bbb_epi16);

						_mm_store_si128(outBR_epi16++, Br_epi16);
						_mm_store_si128(outBG1_epi16++, Bg1_epi16);
						_mm_store_si128(outBG2_epi16++, Bg2_epi16);
						_mm_store_si128(outBB_epi16++, Bb_epi16);

						Nrc_epi16 = _mm_packus_epi16(Nra_epi16,Nrb_epi16);
						Ng1c_epi16 = _mm_packus_epi16(Ng1a_epi16,Ng1b_epi16);
						Ng2c_epi16 = _mm_packus_epi16(Ng2a_epi16,Ng2b_epi16);
						Nbc_epi16 = _mm_packus_epi16(Nba_epi16,Nbb_epi16);


						bg_epi16 = _mm_load_si128(src1_epi16++);
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);	 //shift 16-bit to 12-bit
						g1_epi16 = _mm_srli_epi32(bg_epi16, 16);
						b_epi16 = _mm_and_si128(bg_epi16, ZeroHi);
						bg_epi16 = _mm_load_si128(src1_epi16++);
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(bg_epi16, 16);
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);
						t_epi16 = _mm_and_si128(bg_epi16, ZeroHi);
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);


						gr_epi16 = _mm_load_si128(src2_epi16++);
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);	 //shift 16-bit to 12-bit
						r_epi16 = _mm_srli_epi32(gr_epi16, 16);
						g2_epi16 = _mm_and_si128(gr_epi16, ZeroHi);
						gr_epi16 = _mm_load_si128(src2_epi16++);
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gr_epi16, 16);
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);
						t_epi16 = _mm_and_si128(gr_epi16, ZeroHi);
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);

						Br_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nra_epi16 = _mm_and_si128(Nra_epi16, MaskHi); //0x00f0
						Bg1_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1a_epi16 = _mm_and_si128(Ng1a_epi16, MaskHi); //0x00f0
						Bg2_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2a_epi16 = _mm_and_si128(Ng2a_epi16, MaskHi); //0x00f0
						Bb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nba_epi16 = _mm_and_si128(Nba_epi16, MaskHi); //0x00f0



						bg_epi16 = _mm_load_si128(src1_epi16++);
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);	 //shift 16-bit to 12-bit
						g1_epi16 = _mm_srli_epi32(bg_epi16, 16);
						b_epi16 = _mm_and_si128(bg_epi16, ZeroHi);
						bg_epi16 = _mm_load_si128(src1_epi16++);
						bg_epi16 = _mm_srli_epi16(bg_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(bg_epi16, 16);
						g1_epi16 = _mm_packs_epi32(g1_epi16, t_epi16);
						t_epi16 = _mm_and_si128(bg_epi16, ZeroHi);
						b_epi16 = _mm_packs_epi32(b_epi16, t_epi16);


						gr_epi16 = _mm_load_si128(src2_epi16++);
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);	 //shift 16-bit to 12-bit
						r_epi16 = _mm_srli_epi32(gr_epi16, 16);
						g2_epi16 = _mm_and_si128(gr_epi16, ZeroHi);
						gr_epi16 = _mm_load_si128(src2_epi16++);
						gr_epi16 = _mm_srli_epi16(gr_epi16, 4);	 //shift 16-bit to 12-bit
						t_epi16 = _mm_srli_epi32(gr_epi16, 16);
						r_epi16 = _mm_packs_epi32(r_epi16, t_epi16);
						t_epi16 = _mm_and_si128(gr_epi16, ZeroHi);
						g2_epi16 = _mm_packs_epi32(g2_epi16, t_epi16);

						Brb_epi16 = _mm_srli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_slli_epi16(r_epi16, 4);
						Nrb_epi16 = _mm_and_si128(Nrb_epi16, MaskHi); //0x00f0
						Bg1b_epi16 = _mm_srli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_slli_epi16(g1_epi16, 4);
						Ng1b_epi16 = _mm_and_si128(Ng1b_epi16, MaskHi); //0x00f0
						Bg2b_epi16 = _mm_srli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_slli_epi16(g2_epi16, 4);
						Ng2b_epi16 = _mm_and_si128(Ng2b_epi16, MaskHi); //0x00f0
						Bbb_epi16 = _mm_srli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_slli_epi16(b_epi16, 4);
						Nbb_epi16 = _mm_and_si128(Nbb_epi16, MaskHi); //0x00f0


						Br_epi16 = _mm_packus_epi16(Br_epi16, Brb_epi16);
						Bg1_epi16 = _mm_packus_epi16(Bg1_epi16, Bg1b_epi16);
						Bg2_epi16 = _mm_packus_epi16(Bg2_epi16, Bg2b_epi16);
						Bb_epi16 = _mm_packus_epi16(Bb_epi16, Bbb_epi16);

						_mm_store_si128(outBR_epi16++, Br_epi16);
						_mm_store_si128(outBG1_epi16++, Bg1_epi16);
						_mm_store_si128(outBG2_epi16++, Bg2_epi16);
						_mm_store_si128(outBB_epi16++, Bb_epi16);

						Nrd_epi16 = _mm_packus_epi16(Nra_epi16,Nrb_epi16);
						Ng1d_epi16 = _mm_packus_epi16(Ng1a_epi16,Ng1b_epi16);
						Ng2d_epi16 = _mm_packus_epi16(Ng2a_epi16,Ng2b_epi16);
						Nbd_epi16 = _mm_packus_epi16(Nba_epi16,Nbb_epi16);


						Nra_epi16 = _mm_and_si128(Nrc_epi16, MaskHi); //00600040002000000
						Nrc_epi16 = _mm_srli_epi16(Nrc_epi16, 8);	  //00706050403020100
						Nrc_epi16 = _mm_and_si128(Nrc_epi16, MaskHi); //00700050003000100
						Nrb_epi16 = _mm_and_si128(Nrd_epi16, MaskHi); //00e000c000a000800
						Nrd_epi16 = _mm_srli_epi16(Nrd_epi16, 8);	  //00f0e0d0c0b0a0908
						Nrd_epi16 = _mm_and_si128(Nrd_epi16, MaskHi); //00f000d000b000900
						Nra_epi16 = _mm_packus_epi16(Nra_epi16,Nrb_epi16);
						Nra_epi16 = _mm_srli_epi16(Nra_epi16,4);
						Nrb_epi16 = _mm_packus_epi16(Nrc_epi16,Nrd_epi16);
						Nr_epi16 = _mm_or_si128(Nra_epi16, Nrb_epi16);

						Ng1a_epi16 = _mm_and_si128(Ng1c_epi16, MaskHi); //00600040002000000
						Ng1c_epi16 = _mm_srli_epi16(Ng1c_epi16, 8);	  //00706050403020100
						Ng1c_epi16 = _mm_and_si128(Ng1c_epi16, MaskHi); //00700050003000100
						Ng1b_epi16 = _mm_and_si128(Ng1d_epi16, MaskHi); //00e000c000a000800
						Ng1d_epi16 = _mm_srli_epi16(Ng1d_epi16, 8);	  //00f0e0d0c0b0a0908
						Ng1d_epi16 = _mm_and_si128(Ng1d_epi16, MaskHi); //00f000d000b000900
						Ng1a_epi16 = _mm_packus_epi16(Ng1a_epi16,Ng1b_epi16);
						Ng1a_epi16 = _mm_srli_epi16(Ng1a_epi16,4);
						Ng1b_epi16 = _mm_packus_epi16(Ng1c_epi16,Ng1d_epi16);
						Ng1_epi16 = _mm_or_si128(Ng1a_epi16, Ng1b_epi16);

						Ng2a_epi16 = _mm_and_si128(Ng2c_epi16, MaskHi); //00600040002000000
						Ng2c_epi16 = _mm_srli_epi16(Ng2c_epi16, 8);	  //00706050403020100
						Ng2c_epi16 = _mm_and_si128(Ng2c_epi16, MaskHi); //00700050003000100
						Ng2b_epi16 = _mm_and_si128(Ng2d_epi16, MaskHi); //00e000c000a000800
						Ng2d_epi16 = _mm_srli_epi16(Ng2d_epi16, 8);	  //00f0e0d0c0b0a0908
						Ng2d_epi16 = _mm_and_si128(Ng2d_epi16, MaskHi); //00f000d000b000900
						Ng2a_epi16 = _mm_packus_epi16(Ng2a_epi16,Ng2b_epi16);
						Ng2a_epi16 = _mm_srli_epi16(Ng2a_epi16,4);
						Ng2b_epi16 = _mm_packus_epi16(Ng2c_epi16,Ng2d_epi16);
						Ng2_epi16 = _mm_or_si128(Ng2a_epi16, Ng2b_epi16);

						Nba_epi16 = _mm_and_si128(Nbc_epi16, MaskHi); //00600040002000000
						Nbc_epi16 = _mm_srli_epi16(Nbc_epi16, 8);	  //00706050403020100
						Nbc_epi16 = _mm_and_si128(Nbc_epi16, MaskHi); //00700050003000100
						Nbb_epi16 = _mm_and_si128(Nbd_epi16, MaskHi); //00e000c000a000800
						Nbd_epi16 = _mm_srli_epi16(Nbd_epi16, 8);	  //00f0e0d0c0b0a0908
						Nbd_epi16 = _mm_and_si128(Nbd_epi16, MaskHi); //00f000d000b000900
						Nba_epi16 = _mm_packus_epi16(Nba_epi16,Nbb_epi16);
						Nba_epi16 = _mm_srli_epi16(Nba_epi16,4);
						Nbb_epi16 = _mm_packus_epi16(Nbc_epi16,Nbd_epi16);
						Nb_epi16 = _mm_or_si128(Nba_epi16, Nbb_epi16);


						_mm_store_si128(outNR_epi16++, Nr_epi16);
						_mm_store_si128(outNG1_epi16++, Ng1_epi16);
						_mm_store_si128(outNG2_epi16++, Ng2_epi16);
						_mm_store_si128(outNB_epi16++, Nb_epi16);
					}
					break;
			}
		}
#else
		for(x=0; x<width; x+=8) //R,G1,R,G1... G2,B,G2,B...
		{
			int xx;
			/*for(xx=0;xx<8;xx++)
			{
				sptr1[xx*2] = (x+xx)<<4;
				sptr1[xx*2+1] = (x+xx)<<4;
				sptr2[xx*2] = (x+xx)<<4;
				sptr2[xx*2+1] = (x+xx)<<4;
			}*/
			switch(bayer_format)
			{
				case BAYER_FORMAT_RED_GRN: //Red-grn phase
					for(xx=0;xx<8;xx++)
					{
						r[xx] = *sptr1++>>4;
						g1[xx] = *sptr1++>>4;
						g2[xx] = *sptr2++>>4;
						b[xx] = *sptr2++>>4;
					}
					break;
				case BAYER_FORMAT_GRN_RED:// grn-red
					for(xx=0;xx<8;xx++)
					{
						g1[xx] = *sptr1++>>4;
						r[xx] = *sptr1++>>4;
						b[xx] = *sptr2++>>4;
						g2[xx] = *sptr2++>>4;
					}
					break;
				case BAYER_FORMAT_GRN_BLU:
					for(xx=0;xx<8;xx++)
					{
						g1[xx] = *sptr1++>>4;
						b[xx] = *sptr1++>>4;
						r[xx] = *sptr2++>>4;
						g2[xx] = *sptr2++>>4;
					}
					break;
				case BAYER_FORMAT_BLU_GRN:
					for(xx=0;xx<8;xx++)
					{
						b[xx] = *sptr1++>>4;
						g1[xx] = *sptr1++>>4;
						g2[xx] = *sptr2++>>4;
						r[xx] = *sptr2++>>4;
					}
					break;
			}

			for(xx=0;xx<8;xx++)
			{
				*outBR++ = r[xx] >> 4; //  top 8-bits
				*outBG1++ = g1[xx] >> 4;
				*outBG2++ = g2[xx] >> 4;
				*outBB++ = b[xx] >> 4;
			}
			for(xx=0;xx<8;xx+=2)
			{
				*outNR++ = ((r[xx+1] << 4) & 0xf0) | (r[xx] & 0xf);
				*outNG1++ = ((g1[xx+1] << 4) & 0xf0) | (g1[xx] & 0xf);
				*outNG2++ = ((g2[xx+1] << 4) & 0xf0) | (g2[xx] & 0xf);
				*outNB++ = ((b[xx+1] << 4) & 0xf0) | (b[xx] & 0xf);
			}
		}
#endif
	}

	return 3 * width * 4 * height / 2;
}


// Convert the packed 16-bit BAYER RGB to planes of 16-bit RGBA
void ConvertBYR3ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer)
{
	IMAGE *g_image;
	IMAGE *rg_diff_image;
	IMAGE *bg_diff_image;
	IMAGE *gdiff_image;
	PIXEL *byr2_row_ptr;
	PIXEL *g_row_ptr;
	PIXEL *gdiff_row_ptr;
	PIXEL *rg_row_ptr;
	PIXEL *bg_row_ptr;
	int byr1_pitch;
	int width;
	int height;
	int display_height;
	int row;
	int i,x;
#if USE_GAMMA_TABLE
	unsigned short gamma12bit[4096];
#endif

	// Process 16 bytes each of luma and chroma per loop iteration
	//const int column_step = 2 * sizeof(__m64);

	// Column at which post processing must begin
	//int post_column;

	if (frame == NULL) return;

	// The frame format should be four channels of RGBA
	assert(frame->num_channels == 4);
	assert(frame->format == FRAME_FORMAT_RGBA);

	g_image = frame->channel[0];					// Get the individual planes
	rg_diff_image = frame->channel[1];
	bg_diff_image = frame->channel[2];
	gdiff_image = frame->channel[3];

	byr2_row_ptr = (PIXEL *)data;					// Pointers to the rows
	g_row_ptr = g_image->band[0];
	rg_row_ptr = rg_diff_image->band[0];
	bg_row_ptr = bg_diff_image->band[0];
	gdiff_row_ptr = gdiff_image->band[0];

	byr1_pitch = g_image->pitch/sizeof(PIXEL16S);

	width = g_image->width;							// Dimensions of the luma image
	height = g_image->height;
	display_height = frame->display_height;

	//post_column = width - (width % column_step);

	// The output pitch should be a positive number (no image inversion)
	assert(byr1_pitch > 0);

	// for the SEQ speed test on my 2.5 P4 I get 56fps this the C code.
#if BYR3_USE_GAMMA_TABLE

 #define BYR3_GAMMATABLE(x,y)  (  (int)(pow( (double)(x)/4095.0, (y) )*1023.0)  )

 #define BYR3_GAMMA2(x)  ((x)>>2)
//#define GAMMA2(x)  ( gamma12bit[(x)] )
//#define GAMMA2(x)  (  (int)(pow( double(x)/4096.0, 1.0/2.0 )*256.0)  )
//inline int GAMMA2(int x)  {  int v = 4095-(int)(x);  return ((4095 - ((v*v)>>12))>>4);  }

	{
		int blacklevel = 0;//100;
		float fgamma = 1.0;//2.2;

		for(i=0;i<4096;i++)
		{
			int j = (i - blacklevel)*4096/(4096-blacklevel);
			if(j<0) j=0;
			gamma12bit[i] = BYR3_GAMMATABLE(j, 1.0/fgamma);
		}
	}

	{
	#define LINMAX 40
		float linearmax = (float)gamma12bit[LINMAX];
		float linearstep = linearmax/(float)LINMAX;
		float accum = 0.0;

		for(i=0; i<40; i++)
		{
			gamma12bit[i] = accum;
			accum += linearstep;
		}
	}

	for (row = 0; row < display_height; row++)
	{
		PIXEL g, g1, g2, r, b;
		PIXEL *line1,*line2;

		line1 = &byr2_row_ptr[row * pitch/2];
		line2 = line1 + (pitch>>2);

		for(x=0; x<width; x++)
		{
		/*	g1 = *line1++ >> 2;
			r = *line1++ >> 2;
			b = *line2++ >> 2;
			g2 = *line2++ >> 2;*/
		#if BYR3_HORIZONTAL_BAYER_SHIFT
			r= BYR3_GAMMA2(*line1++);
			g1 = BYR3_GAMMA2(*line1++);
			g2 = BYR3_GAMMA2(*line2++);
			b= BYR3_GAMMA2(*line2++);
		#else
			g1= BYR3_GAMMA2(*line1++);
			r = BYR3_GAMMA2(*line1++);
			b = BYR3_GAMMA2(*line2++);
			g2= BYR3_GAMMA2(*line2++);
		#endif

			/* 10 bit */
			g = (g1+g2)>>1;
			*g_row_ptr++ = g;
			*rg_row_ptr++ = ((r-g)>>1)+512;
			*bg_row_ptr++ = ((b-g)>>1)+512;
			*gdiff_row_ptr++ = (g1-g2+1024)>>1;
		}

		// Advance to the next rows in the input and output images
		g_row_ptr += byr1_pitch - width;
		rg_row_ptr += byr1_pitch - width;
		bg_row_ptr += byr1_pitch - width;
		gdiff_row_ptr += byr1_pitch - width;
	}
	for (; row < height; row++)
	{
		PIXEL g, g1, g2, r, b;
		PIXEL *line1,*line2;

		for(x=0; x<width; x++)
		{
			*g_row_ptr++ = 0;
			*rg_row_ptr++ = 0;
			*bg_row_ptr++ = 0;
			*gdiff_row_ptr++ = 0;
		}

		// Advance to the next rows in the input and output images
		g_row_ptr += byr1_pitch - width;
		rg_row_ptr += byr1_pitch - width;
		bg_row_ptr += byr1_pitch - width;
		gdiff_row_ptr += byr1_pitch - width;
	}


#else

  #if 0 // non-MMX   approx 32fps Medium

 	for (row = 0; row < display_height; row++)
	{
		PIXEL g, g1, g2, r, b;
		PIXEL *line1a,*line2a;
		PIXEL *line1b,*line2b;

		line1a = &byr2_row_ptr[row * pitch/2];
		line2a = line1a + (pitch>>2);
		line1b = line1a + (pitch>>3);
		line2b = line2a + (pitch>>3);

		for(x=0; x<width; x++)
		{
			r = (*line1a++);
			g1= (*line1b++);
			g2= (*line2a++);
			b = (*line2b++);

			/* 10 bit */
			g = (g1+g2)>>1;
			*g_row_ptr++ = g;
			*rg_row_ptr++ = ((r-g)>>1)+512;
			*bg_row_ptr++ = ((b-g)>>1)+512;
			*gdiff_row_ptr++ = (g1-g2+1024)>>1;
		}

		// Advance to the next rows in the input and output images
		g_row_ptr += byr1_pitch - width;
		rg_row_ptr += byr1_pitch - width;
		bg_row_ptr += byr1_pitch - width;
		gdiff_row_ptr += byr1_pitch - width;
	}
	for (; row < height; row++)
	{
		PIXEL g, g1, g2, r, b;
		PIXEL *line1,*line2;

		for(x=0; x<width; x++)
		{
			*g_row_ptr++ = 0;
			*rg_row_ptr++ = 0;
			*bg_row_ptr++ = 0;
			*gdiff_row_ptr++ = 0;
		}

		// Advance to the next rows in the input and output images
		g_row_ptr += byr1_pitch - width;
		rg_row_ptr += byr1_pitch - width;
		bg_row_ptr += byr1_pitch - width;
		gdiff_row_ptr += byr1_pitch - width;
	}

  #else  // 38fps Medium

	for (row = 0; row < display_height; row++)
	{
		PIXEL *line1a,*line2a;
		PIXEL *line1b,*line2b;

		line1a = &byr2_row_ptr[row * pitch/2];
		line2a = line1a + (pitch>>2);
		line1b = line1a + (pitch>>3);
		line2b = line2a + (pitch>>3);

		{

			__m128i *line1aptr_epi16 = (__m128i *)line1a;
			__m128i *line2aptr_epi16 = (__m128i *)line2a;
			__m128i *line1bptr_epi16 = (__m128i *)line1b;
			__m128i *line2bptr_epi16 = (__m128i *)line2b;
			__m128i *gptr_epi16 = (__m128i *)g_row_ptr;
			__m128i *gdiffptr_epi16 = (__m128i *)gdiff_row_ptr;
			__m128i *rgptr_epi16 = (__m128i *)rg_row_ptr;
			__m128i *bgptr_epi16 = (__m128i *)bg_row_ptr;

			__m128i g1_epi16;
			__m128i g2_epi16;
			__m128i r_epi16;
			__m128i b_epi16;

			__m128i g_epi16;
			__m128i gdiff_epi16;
			__m128i rg_epi16;
			__m128i bg_epi16;

			const __m128i rounding_epi16 = _mm_set1_epi16(512);


			for(x=0; x<width; x+=8)
			{
				// Read the first group of 8 16-bit packed 12-bit pixels
				r_epi16 = _mm_load_si128(line1aptr_epi16++);
				g1_epi16 = _mm_load_si128(line1bptr_epi16++);
				g2_epi16 = _mm_load_si128(line2aptr_epi16++);
				b_epi16 = _mm_load_si128(line2bptr_epi16++);

				g_epi16 = _mm_adds_epi16(g1_epi16,g2_epi16);
				g_epi16 = _mm_srai_epi16(g_epi16, 1);
				_mm_store_si128(gptr_epi16++, g_epi16);

				rg_epi16 = _mm_subs_epi16(r_epi16, g_epi16);
				rg_epi16 = _mm_srai_epi16(rg_epi16, 1);
				rg_epi16 = _mm_adds_epi16(rg_epi16,rounding_epi16);
				_mm_store_si128(rgptr_epi16++, rg_epi16);

				bg_epi16 = _mm_subs_epi16(b_epi16, g_epi16);
				bg_epi16 = _mm_srai_epi16(bg_epi16, 1);
				bg_epi16 = _mm_adds_epi16(bg_epi16,rounding_epi16);
				_mm_store_si128(bgptr_epi16++, bg_epi16);

				gdiff_epi16 = _mm_subs_epi16(g1_epi16, g2_epi16);
				gdiff_epi16 = _mm_adds_epi16(gdiff_epi16,rounding_epi16);
				gdiff_epi16 = _mm_adds_epi16(gdiff_epi16,rounding_epi16);
				gdiff_epi16 = _mm_srai_epi16(gdiff_epi16, 1);
				_mm_store_si128(gdiffptr_epi16++, gdiff_epi16);

			}
		}

		// Advance to the next rows in the input and output images
		g_row_ptr += byr1_pitch;// - width;
		rg_row_ptr += byr1_pitch;// - width;
		bg_row_ptr += byr1_pitch;// - width;
		gdiff_row_ptr += byr1_pitch;// - width;
	}
	for (; row < height; row++)
	{
		for(x=0; x<width; x++)
		{
			*g_row_ptr++ = 0;
			*rg_row_ptr++ = 0;
			*bg_row_ptr++ = 0;
			*gdiff_row_ptr++ = 0;
		}

		// Advance to the next rows in the input and output images
		g_row_ptr += byr1_pitch - width;
		rg_row_ptr += byr1_pitch - width;
		bg_row_ptr += byr1_pitch - width;
		gdiff_row_ptr += byr1_pitch - width;
	}
  #endif
#endif


	// Set the image parameters for each channel
	for (i = 0; i < 4; i++)
	{
		IMAGE *image = frame->channel[i];
		int band;

		// Set the image scale
		for (band = 0; band < IMAGE_NUM_BANDS; band++)
			image->scale[band] = 1;

		// Set the pixel type
		image->pixel_type[0] = PIXEL_TYPE_16S;
	}
}

/*!
	@brief Maximum precision for the encoding curve lookpu table
 
	The maximum is 14 bits as 12 for SI2K/ArriD20, 14 for Dalsa
*/
#define MAX_INPUT_PRECISION	14

void AddCurveToUncompressedBYR4(uint32_t encode_curve, uint32_t encode_curve_preset,
					uint8_t *data, int pitch, FRAME *frame)
{
	unsigned short curve[1<<MAX_INPUT_PRECISION];
	int precision = 16;


	if(encode_curve_preset == 0)
	{
		int i,row,max_value = 1<<MAX_INPUT_PRECISION;

		//int greylevels =  (1<<precision); // 10-bit = 1024;
		//int midpoint =  greylevels/2; // 10-bit = 512;
		int width = frame->width * 2;
		int height = frame->display_height * 2;
		int encode_curve_type = (encode_curve>>16);										
		//int encode_curve_neg = encode_curve_type & CURVE_TYPE_NEGATIVE;

#define LOGBASE	90
#define BYR4_LOGTABLE(x)  (  (int)( CURVE_LIN2LOG(x,LOGBASE) * (float)((1<<precision)-1))  )
#define BYR4_CURVE(x)  ( curve[(x)] )

		for(i=0;i<max_value;i++)
		{
			if(encode_curve == 0 || encode_curve == CURVE_LOG_90)
			{
				if(i)
					BYR4_CURVE(i) = BYR4_LOGTABLE((float)i/(float)max_value);
				else
					BYR4_CURVE(0) = 0;
			}
			else if((encode_curve_type & CURVE_TYPE_MASK) == CURVE_TYPE_LOG)
			{
				float logbase;

				if(encode_curve_type & CURVE_TYPE_EXTENDED)
				{
					logbase = (float)(encode_curve & 0xffff);
				}
				else
				{
					float num,den;
					num = (float)((encode_curve>>8) & 0xff);
					den = (float)(encode_curve & 0xff);
					logbase = num/den;
				}

				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2LOG((float)i/(float)max_value,logbase) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_CINEON)
			{
				float num,den,logbase;

				num = (float)((encode_curve>>8) & 0xff);
				den = (float)(encode_curve & 0xff);
				logbase = num/den;

				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2CINEON((float)i/(float)max_value,logbase) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_CINE985)
			{
				float num,den,logbase;

				num = (float)((encode_curve>>8) & 0xff);
				den = (float)(encode_curve & 0xff);
				logbase = num/den;

				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2CINE985((float)i/(float)max_value,logbase) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_PARA)
			{
				int gain, power;

				gain = (int)((encode_curve>>8) & 0xff);
				power = (int)(encode_curve & 0xff);

				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2PARA((float)i/(float)max_value, gain, power) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_GAMMA)
			{
				double num,den,gamma;

				num = (double)((encode_curve>>8) & 0xff);
				den = (double)(encode_curve & 0xff);
				gamma = num/den;

				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2GAM((double)((float)i/(float)max_value),gamma) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_CSTYLE)
			{
				int num;
				num = ((encode_curve>>8) & 0xff);

				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2CSTYLE((float)i,num) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_SLOG)
			{
				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2SLOG((float)i) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_LOGC)
			{
				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2LOGC((float)i) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else //if(encode_curve == CURVE_LINEAR) // or for pre-curved sources.
			{
				BYR4_CURVE(i) = (int)(((float)i/(float)max_value)* (float)((1<<precision)-1));
			}

		}

		for (row = 0; row < height; row++)
		{
			int x;
			uint16_t *line = (uint16_t *)(data + (pitch>>1) * row);
			for(x=0; x<width; x++)
			{
				//line[x] = 0x1010;
				line[x] = BYR4_CURVE(line[x]>>(16-MAX_INPUT_PRECISION));
			}
		}
	}
}

// Convert the packed 16-bit BAYER RGB to planes of 16-bit RGBA
void ConvertBYR4ToFrame16s(int bayer_format, uint32_t encode_curve, uint32_t encode_curve_preset,
						   uint8_t *data, int pitch, FRAME *frame, int precision)
{
	IMAGE *g_image;
	IMAGE *rg_diff_image;
	IMAGE *bg_diff_image;
	IMAGE *gdiff_image;
	PIXEL *byr4_row_ptr;
	PIXEL *g_row_ptr;
	PIXEL *gdiff_row_ptr;
	PIXEL *rg_row_ptr;
	PIXEL *bg_row_ptr;
	int byr1_pitch;
	int width;
	int height;
	int display_height;
	int row;
	int i,x;
	int max_value = 1<<MAX_INPUT_PRECISION;

	int greylevels =  (1<<precision); // 10-bit = 1024;
	int midpoint =  greylevels/2; // 10-bit = 512;

	// Process 16 bytes each of luma and chroma per loop iteration
	//const int column_step = 2 * sizeof(__m64);

	// Column at which post processing must begin
	//int post_column;

	if (frame == NULL) return;

	// The frame format should be four channels of RGBA
	assert(frame->num_channels == 4);
	assert(frame->format == FRAME_FORMAT_RGBA);

	g_image = frame->channel[0];					// Get the individual planes
	rg_diff_image = frame->channel[1];
	bg_diff_image = frame->channel[2];
	gdiff_image = frame->channel[3];

	byr4_row_ptr = (PIXEL *)data;					// Pointers to the rows
	g_row_ptr = g_image->band[0];
	rg_row_ptr = rg_diff_image->band[0];
	bg_row_ptr = bg_diff_image->band[0];
	gdiff_row_ptr = gdiff_image->band[0];

	pitch /= sizeof(PIXEL16S);
	byr1_pitch = g_image->pitch/sizeof(PIXEL16S);

	width = g_image->width;							// Dimensions of the luma image
	height = g_image->height;
	display_height = frame->display_height;

	//post_column = width - (width % column_step);

	if(encode_curve_preset)
	{ 
		int mid11bit = (1<<(13-1));

		for (row = 0; row < height; row++)
		{
			PIXEL16U g1, g2, r, b;
			int gg,rg,bg,dg;
			PIXEL16U *line1,*line2;
			int srcrow = row;

			if(row >= display_height)
				srcrow = display_height-1;

			line1 = (PIXEL16U *)&byr4_row_ptr[srcrow * pitch];
			line2 = line1 + (pitch>>1);



			switch(bayer_format)
			{
			case BAYER_FORMAT_RED_GRN:
				for(x=0; x<width; x++)
				{
					r = (*line1++>>(16-precision));
					g1= (*line1++>>(16-precision));
					g2= (*line2++>>(16-precision));
					b = (*line2++>>(16-precision));

					/*	g = (g1+g2)>>1;
					*g_row_ptr++ = g;
					*rg_row_ptr++ = ((r-g)>>1)+midpoint;
					*bg_row_ptr++ = ((b-g)>>1)+midpoint;
					*gdiff_row_ptr++ = (g1-g2+greylevels)>>1;*/

					gg = (g1+g2)>>1;
					dg = g1-g2;

					rg = r - gg;
					bg = b - gg;
					rg += mid11bit;
					bg += mid11bit;
					dg += mid11bit;
					rg >>= 1;
					bg >>= 1;
					dg >>= 1;

					*g_row_ptr++ = gg;
					*rg_row_ptr++ = rg;
					*bg_row_ptr++ = bg;
					*gdiff_row_ptr++ = dg;
				}
				break;
			case BAYER_FORMAT_GRN_RED:
				for(x=0; x<width; x++)
				{
					g1= (*line1++>>(16-precision));
					r = (*line1++>>(16-precision));
					b = (*line2++>>(16-precision));
					g2= (*line2++>>(16-precision));
					
					/*	g = (g1+g2)>>1;
					*g_row_ptr++ = g;
					*rg_row_ptr++ = ((r-g)>>1)+midpoint;
					*bg_row_ptr++ = ((b-g)>>1)+midpoint;
					*gdiff_row_ptr++ = (g1-g2+greylevels)>>1;*/

					gg = (g1+g2)>>1;
					dg = g1-g2;

					rg = r - gg;
					bg = b - gg;
					rg += mid11bit;
					bg += mid11bit;
					dg += mid11bit;
					rg >>= 1;
					bg >>= 1;
					dg >>= 1;

					*g_row_ptr++ = gg;
					*rg_row_ptr++ = rg;
					*bg_row_ptr++ = bg;
					*gdiff_row_ptr++ = dg;
				}
				break;
			case BAYER_FORMAT_BLU_GRN:
				for(x=0; x<width; x++)
				{
					b = (*line1++>>(16-precision));
					g1= (*line1++>>(16-precision));
					g2= (*line2++>>(16-precision));
					r = (*line2++>>(16-precision));

					/*	g = (g1+g2)>>1;
					*g_row_ptr++ = g;
					*rg_row_ptr++ = ((r-g)>>1)+midpoint;
					*bg_row_ptr++ = ((b-g)>>1)+midpoint;
					*gdiff_row_ptr++ = (g1-g2+greylevels)>>1;*/

					gg = (g1+g2)>>1;
					dg = g1-g2;

					rg = r - gg;
					bg = b - gg;
					rg += mid11bit;
					bg += mid11bit;
					dg += mid11bit;
					rg >>= 1;
					bg >>= 1;
					dg >>= 1;

					*g_row_ptr++ = gg;
					*rg_row_ptr++ = rg;
					*bg_row_ptr++ = bg;
					*gdiff_row_ptr++ = dg;
				}
				break;
			case BAYER_FORMAT_GRN_BLU:
				for(x=0; x<width; x++)
				{
					g1= (*line1++>>(16-precision));
					b = (*line1++>>(16-precision));
					r = (*line2++>>(16-precision));
					g2= (*line2++>>(16-precision));
					
					/*	g = (g1+g2)>>1;
					*g_row_ptr++ = g;
					*rg_row_ptr++ = ((r-g)>>1)+midpoint;
					*bg_row_ptr++ = ((b-g)>>1)+midpoint;
					*gdiff_row_ptr++ = (g1-g2+greylevels)>>1;*/

					gg = (g1+g2)>>1;
					dg = g1-g2;

					rg = r - gg;
					bg = b - gg;
					rg += mid11bit;
					bg += mid11bit;
					dg += mid11bit;
					rg >>= 1;
					bg >>= 1;
					dg >>= 1;

					*g_row_ptr++ = gg;
					*rg_row_ptr++ = rg;
					*bg_row_ptr++ = bg;
					*gdiff_row_ptr++ = dg;
				}
				break;
			}

	

			// Advance to the next rows in the input and output images
			g_row_ptr += byr1_pitch - width;
			rg_row_ptr += byr1_pitch - width;
			bg_row_ptr += byr1_pitch - width;
			gdiff_row_ptr += byr1_pitch - width;
		}
	}
	else
	{
		unsigned short curve[1<<MAX_INPUT_PRECISION];
		int encode_curve_type = (encode_curve>>16);										
		//int encode_curve_neg = encode_curve_type & CURVE_TYPE_NEGATIVE;

#define LOGBASE	90
//#define BYR4_LOGTABLE(x)  (  (int)((log((((double)(x))/((double)((1<<MAX_INPUT_PRECISION)-1))) * (double)(LOG-1) + 1.0)/log(LOG))*(double)((1<<precision)-1))  )
#define BYR4_LOGTABLE(x)  (  (int)( CURVE_LIN2LOG(x,LOGBASE) * (float)((1<<precision)-1))  )
#define BYR4_CURVE(x)  ( curve[(x)] )

		for(i=0;i<max_value;i++)
		{

			if(encode_curve == 0 || encode_curve == CURVE_LOG_90)
			{
				if(i)
					BYR4_CURVE(i) = BYR4_LOGTABLE((float)i/(float)max_value);
				else
					BYR4_CURVE(0) = 0;

			}
			else if((encode_curve_type & CURVE_TYPE_MASK) == CURVE_TYPE_LOG)
			{
				float logbase;

				if(encode_curve_type & CURVE_TYPE_EXTENDED)
				{
					logbase = (float)(encode_curve & 0xffff);
				}
				else
				{
					float num,den;
					num = (float)((encode_curve>>8) & 0xff);
					den = (float)(encode_curve & 0xff);
					logbase = num/den;
				}

				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2LOG((float)i/(float)max_value,logbase) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_CINEON)
			{
				float num,den,logbase;

				num = (float)((encode_curve>>8) & 0xff);
				den = (float)(encode_curve & 0xff);
				logbase = num/den;

				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2CINEON((float)i/(float)max_value,logbase) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_CINE985)
			{
				float num,den,logbase;

				num = (float)((encode_curve>>8) & 0xff);
				den = (float)(encode_curve & 0xff);
				logbase = num/den;

				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2CINE985((float)i/(float)max_value,logbase) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_PARA)
			{
				int gain, power;

				gain = (int)((encode_curve>>8) & 0xff);
				power = (int)(encode_curve & 0xff);

				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2PARA((float)i/(float)max_value, gain, power) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_GAMMA)
			{
				double num,den,gamma;

				num = (double)((encode_curve>>8) & 0xff);
				den = (double)(encode_curve & 0xff);
				gamma = num/den;

				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2GAM((double)((float)i/(float)max_value),gamma) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_CSTYLE)
			{
				int num;
				num = ((encode_curve>>8) & 0xff);

				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2CSTYLE((float)i,num) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_SLOG)
			{
				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2SLOG((float)i) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else if(encode_curve_type == CURVE_TYPE_LOGC)
			{
				if(i)
					BYR4_CURVE(i) = (int)(CURVE_LIN2LOGC((float)i) * (float)((1<<precision)-1));
				else
					BYR4_CURVE(0) = 0;
			}
			else //if(encode_curve == CURVE_LINEAR) // or for pre-curved sources.
			{
				BYR4_CURVE(i) = (int)(((float)i/(float)max_value)* (float)((1<<precision)-1));
			}

		}

	/*	if(*bayer_format == 0) // unset, therefore scan for pixel order -- really we shouldn't be guessing - WIP
		{
			// Red in the first line
			int tl=0,tr=0,bl=0,br=0,total = 0;
			for (row = 20; row < display_height-20; row++)
			{
				PIXEL16U *line1,*line2;

				line1 = &byr4_row_ptr[row * pitch/2];
				line2 = line1 + (pitch>>2);

				for(x=0; x<width; x++)
				{
					tl += (*line1++);
					tr += (*line1++);
					bl += (*line2++);
					br += (*line2++);
					total++;
				}

				if(total > 10000)
					break;
			}

			if(abs(tl-br) > abs(tr-bl))
			{
				*bayer_format = BAYER_FORMAT_RED_GRN+4; // +4 to flag as set as RED_GRN is 0
			}
			else
			{
				*bayer_format = BAYER_FORMAT_GRN_RED;
			}
		}*/


		for (row = 0; row < height; row++)
		{
			PIXEL16U g, g1, g2, r, b;
			PIXEL16U *line1,*line2;
			int srcrow = row;
			
			if(row >= display_height)
				srcrow = display_height-1;

			line1 = (PIXEL16U *)&byr4_row_ptr[srcrow * width*4];
			line2 = line1 + (width*2);

			switch(bayer_format)
			{
			case BAYER_FORMAT_RED_GRN:
				for(x=0; x<width; x++)
				{
					r = BYR4_CURVE(*line1++>>(16-MAX_INPUT_PRECISION));
					g1= BYR4_CURVE(*line1++>>(16-MAX_INPUT_PRECISION));
					g2= BYR4_CURVE(*line2++>>(16-MAX_INPUT_PRECISION));
					b = BYR4_CURVE(*line2++>>(16-MAX_INPUT_PRECISION));

					/* 10 bit */
					g = (g1+g2)>>1;
					*g_row_ptr++ = g;
					*rg_row_ptr++ = ((r-g)>>1)+midpoint;
					*bg_row_ptr++ = ((b-g)>>1)+midpoint;
					*gdiff_row_ptr++ = (g1-g2+greylevels)>>1;
				}
				break;
			case BAYER_FORMAT_GRN_RED:
				for(x=0; x<width; x++)
				{
					g1= BYR4_CURVE(*line1++>>(16-MAX_INPUT_PRECISION));
					r = BYR4_CURVE(*line1++>>(16-MAX_INPUT_PRECISION));
					b = BYR4_CURVE(*line2++>>(16-MAX_INPUT_PRECISION));
					g2= BYR4_CURVE(*line2++>>(16-MAX_INPUT_PRECISION));

					/* 10 bit */
					g = (g1+g2)>>1;
					*g_row_ptr++ = g;
					*rg_row_ptr++ = ((r-g)>>1)+midpoint;
					*bg_row_ptr++ = ((b-g)>>1)+midpoint;
					*gdiff_row_ptr++ = (g1-g2+greylevels)>>1;
				}
				break;
			case BAYER_FORMAT_BLU_GRN:
				for(x=0; x<width; x++)
				{
					b = BYR4_CURVE(*line1++>>(16-MAX_INPUT_PRECISION));
					g1= BYR4_CURVE(*line1++>>(16-MAX_INPUT_PRECISION));
					g2= BYR4_CURVE(*line2++>>(16-MAX_INPUT_PRECISION));
					r = BYR4_CURVE(*line2++>>(16-MAX_INPUT_PRECISION));

					/* 10 bit */
					g = (g1+g2)>>1;
					*g_row_ptr++ = g;
					*rg_row_ptr++ = ((r-g)>>1)+midpoint;
					*bg_row_ptr++ = ((b-g)>>1)+midpoint;
					*gdiff_row_ptr++ = (g1-g2+greylevels)>>1;
				}
				break;
			case BAYER_FORMAT_GRN_BLU:
				for(x=0; x<width; x++)
				{
					g1= BYR4_CURVE(*line1++>>(16-MAX_INPUT_PRECISION));
					b = BYR4_CURVE(*line1++>>(16-MAX_INPUT_PRECISION));
					r = BYR4_CURVE(*line2++>>(16-MAX_INPUT_PRECISION));
					g2= BYR4_CURVE(*line2++>>(16-MAX_INPUT_PRECISION));

					/* 10 bit */
					g = (g1+g2)>>1;
					*g_row_ptr++ = g;
					*rg_row_ptr++ = ((r-g)>>1)+midpoint;
					*bg_row_ptr++ = ((b-g)>>1)+midpoint;
					*gdiff_row_ptr++ = (g1-g2+greylevels)>>1;
				}
				break;
			}

			// Advance to the next rows in the input and output images
			g_row_ptr += byr1_pitch - width;
			rg_row_ptr += byr1_pitch - width;
			bg_row_ptr += byr1_pitch - width;
			gdiff_row_ptr += byr1_pitch - width;
		}
	}

	// Set the image parameters for each channel
	for (i = 0; i < 4; i++)
	{
		IMAGE *image = frame->channel[i];
		int band;

		// Set the image scale
		for (band = 0; band < IMAGE_NUM_BANDS; band++)
			image->scale[band] = 1;

		// Set the pixel type
		image->pixel_type[0] = PIXEL_TYPE_16S;
	}
}



void ConvertBYR5ToFrame16s(int bayer_format, uint8_t *uncompressed_chunk, int pitch, FRAME *frame, uint8_t *scratch)
{
	IMAGE *g_image;
	IMAGE *rg_diff_image;
	IMAGE *bg_diff_image;
	IMAGE *gdiff_image;
	PIXEL *g_row_ptr;
	PIXEL *gdiff_row_ptr;
	PIXEL *rg_row_ptr;
	PIXEL *bg_row_ptr;
	int byr1_pitch;
	int width;
	int height;
	int display_height;
	int i;
	//int max_value = 1<<MAX_INPUT_PRECISION;

	//int greylevels =  (1<<12);
	//int midpoint =  greylevels/2;

	if (frame == NULL) return;

	// The frame format should be four channels of RGBA
	assert(frame->num_channels == 4);
	assert(frame->format == FRAME_FORMAT_RGBA);

	g_image = frame->channel[0];					// Get the individual planes
	rg_diff_image = frame->channel[1];
	bg_diff_image = frame->channel[2];
	gdiff_image = frame->channel[3];

	g_row_ptr = g_image->band[0];
	rg_row_ptr = rg_diff_image->band[0];
	bg_row_ptr = bg_diff_image->band[0];
	gdiff_row_ptr = gdiff_image->band[0];

	pitch /= sizeof(PIXEL16S);
	byr1_pitch = g_image->pitch/sizeof(PIXEL16S);

	width = g_image->width;							// Dimensions of the luma image
	height = g_image->height;
	display_height = frame->display_height;


	{
		int row,x,srcwidth;

		srcwidth = width;

		for (row = 0; row < height; row++)
		{
			PIXEL16U *tptr;
			uint8_t *outB, *outN;
			int srcrow = row;

			tptr = (PIXEL16U *)scratch;
			if(row >= display_height)
				srcrow = display_height-1;

			outB = (uint8_t *)uncompressed_chunk;
			outB += srcrow * srcwidth * 4 * 3/2; //12-bit
			outN = outB;
			outN += srcwidth * 4;

			{
				__m128i g1_epi16;
				__m128i g2_epi16;
				__m128i g3_epi16;
				__m128i g4_epi16;
				__m128i B1_epi16;
				__m128i N1_epi16;
				__m128i B2_epi16;
				__m128i N2_epi16;
				__m128i B3_epi16;
				__m128i N3_epi16;
				__m128i B4_epi16;
				__m128i N4_epi16;
				__m128i zero = _mm_set1_epi16(0);
				__m128i MaskUp = _mm_set1_epi16(0xf0f0);
				__m128i MaskDn = _mm_set1_epi16(0x0f0f);

				__m128i *tmp_epi16 = (__m128i *)tptr;
				__m128i *outB_epi16 = (__m128i *)outB;
				__m128i *outN_epi16 = (__m128i *)outN;


				for(x=0; x<srcwidth*4; x+=32)
				{
					B1_epi16 = _mm_loadu_si128(outB_epi16++);
					B2_epi16 = _mm_loadu_si128(outB_epi16++);
					N1_epi16 = _mm_loadu_si128(outN_epi16++);

					N2_epi16 = _mm_and_si128(N1_epi16, MaskDn);
					N1_epi16 = _mm_and_si128(N1_epi16, MaskUp);
					N1_epi16 = _mm_srli_epi16(N1_epi16, 4);

					N3_epi16 = _mm_unpacklo_epi8(N2_epi16, N1_epi16);
					N4_epi16 = _mm_unpackhi_epi8(N2_epi16, N1_epi16);

					g4_epi16 = _mm_unpackhi_epi8 (N4_epi16, zero);
					g3_epi16 = _mm_unpacklo_epi8 (N4_epi16, zero);
					g2_epi16 = _mm_unpackhi_epi8 (N3_epi16, zero);
					g1_epi16 = _mm_unpacklo_epi8 (N3_epi16, zero);

					B4_epi16 = _mm_unpackhi_epi8 (B2_epi16, zero);
					B3_epi16 = _mm_unpacklo_epi8 (B2_epi16, zero);
					B2_epi16 = _mm_unpackhi_epi8 (B1_epi16, zero);
					B1_epi16 = _mm_unpacklo_epi8 (B1_epi16, zero);

					B4_epi16 = _mm_slli_epi16(B4_epi16, 4);
					B3_epi16 = _mm_slli_epi16(B3_epi16, 4);
					B2_epi16 = _mm_slli_epi16(B2_epi16, 4);
					B1_epi16 = _mm_slli_epi16(B1_epi16, 4);

					g1_epi16 = _mm_or_si128(g1_epi16, B1_epi16);
					g2_epi16 = _mm_or_si128(g2_epi16, B2_epi16);
					g3_epi16 = _mm_or_si128(g3_epi16, B3_epi16);
					g4_epi16 = _mm_or_si128(g4_epi16, B4_epi16);

					_mm_store_si128(tmp_epi16++, g1_epi16);
					_mm_store_si128(tmp_epi16++, g2_epi16);
					_mm_store_si128(tmp_epi16++, g3_epi16);
					_mm_store_si128(tmp_epi16++, g4_epi16);
				}

				{
					__m128i *rp_epi16; 
					__m128i *g1p_epi16;
					__m128i *g2p_epi16;
					__m128i *bp_epi16;
					__m128i *dgg_epi16;
					__m128i *drg_epi16;
					__m128i *dbg_epi16;
					__m128i *ddg_epi16;
					__m128i mid11bit = _mm_set1_epi16(1<<(13-1));

					dgg_epi16 = (__m128i *)g_row_ptr;
					drg_epi16 = (__m128i *)rg_row_ptr;
					dbg_epi16 = (__m128i *)bg_row_ptr;
					ddg_epi16 = (__m128i *)gdiff_row_ptr;

					switch(bayer_format)
					{
					case BAYER_FORMAT_RED_GRN:
						rp_epi16 = (__m128i *)tptr;
						g1p_epi16 = (__m128i *)&tptr[width];
						g2p_epi16 = (__m128i *)&tptr[width*2];
						bp_epi16 = (__m128i *)&tptr[width*3];
						break;
					case BAYER_FORMAT_GRN_RED:
						g1p_epi16 = (__m128i *)tptr;
						rp_epi16 = (__m128i *)&tptr[width];
						bp_epi16 = (__m128i *)&tptr[width*2];
						g2p_epi16 = (__m128i *)&tptr[width*3];
						break;
					case BAYER_FORMAT_GRN_BLU:
						g1p_epi16 = (__m128i *)tptr;
						bp_epi16 = (__m128i *)&tptr[width];
						rp_epi16 = (__m128i *)&tptr[width*2];
						g2p_epi16 = (__m128i *)&tptr[width*3];
						break;
					case BAYER_FORMAT_BLU_GRN:
						bp_epi16 = (__m128i *)tptr;
						g1p_epi16 = (__m128i *)&tptr[width];
						g2p_epi16 = (__m128i *)&tptr[width*2];
						rp_epi16 = (__m128i *)&tptr[width*3];
						break;
					}


					for(x=0; x<srcwidth; x+=8)
					{
						__m128i r_epi16 = _mm_load_si128(rp_epi16++);
						__m128i g1_epi16 = _mm_load_si128(g1p_epi16++);
						__m128i g2_epi16 = _mm_load_si128(g2p_epi16++);
						__m128i b_epi16 = _mm_load_si128(bp_epi16++);
						__m128i gg = _mm_adds_epu16(g1_epi16, g2_epi16); //13-bit
						__m128i dg = _mm_subs_epi16(g1_epi16, g2_epi16); //signed 12-bit
						__m128i rg;
						__m128i bg;

						gg = _mm_srai_epi16(gg, 1); //12-bit unsigned
						rg = _mm_subs_epi16(r_epi16, gg); //13-bit
						bg = _mm_subs_epi16(b_epi16, gg); //13-bit
						rg = _mm_adds_epi16(rg, mid11bit); //13-bit unsigned
						bg = _mm_adds_epi16(bg, mid11bit); //13-bit unsigned
						dg = _mm_adds_epi16(dg, mid11bit); //13-bit unsigned
						rg = _mm_srai_epi16(rg, 1); //12-bit unsigned
						bg = _mm_srai_epi16(bg, 1); //12-bit unsigned
						dg = _mm_srai_epi16(dg, 1); //12-bit unsigned
					
						_mm_store_si128(dgg_epi16++, gg);
						_mm_store_si128(drg_epi16++, rg);
						_mm_store_si128(dbg_epi16++, bg);
						_mm_store_si128(ddg_epi16++, dg);
					}

					for(; x<srcwidth; x++)
					{
						int G; 
						int RG;
						int BG;
						int DG;

						switch(bayer_format)
						{
						case BAYER_FORMAT_RED_GRN:
							G = (scratch[x+width] + scratch[x+width*2]);
							RG = (scratch[x]<<3)-G+32768;
							BG = (scratch[x+width*3]<<3)-G+32768;
							DG = ((scratch[x+width] - scratch[x+width*2])<<3) + 32768;
							break;
						case BAYER_FORMAT_GRN_RED:
							G = (scratch[x] + scratch[x+width*3]);
							RG = (scratch[x+width]<<3)-G+32768;
							BG = (scratch[x+width*2]<<3)-G+32768;
							DG = ((scratch[x] - scratch[x+width*3])<<3) + 32768;
							break;
						case BAYER_FORMAT_GRN_BLU:
							G = (scratch[x] + scratch[x+width*3]);
							RG = (scratch[x+width*2]<<3)-G+32768;
							BG = (scratch[x+width]<<3)-G+32768;
							DG = ((scratch[x] - scratch[x+width*3])<<3) + 32768;
							break;
						case BAYER_FORMAT_BLU_GRN:
							G = (scratch[x+width] + scratch[x+width*2]);
							RG = (scratch[x+width*3]<<3)-G+32768;
							BG = (scratch[x]<<3)-G+32768;
							DG = ((scratch[x+width] - scratch[x+width*2])<<3) + 32768;
							break;
						}
						
						g_row_ptr[x] =  G>>1;
						rg_row_ptr[x] = RG>>4;
						bg_row_ptr[x] = BG>>4;
						gdiff_row_ptr[x] = DG>>4;
					}
				}
			}		
			
			// Advance to the next rows in the input and output images
			g_row_ptr += byr1_pitch;
			rg_row_ptr += byr1_pitch;
			bg_row_ptr += byr1_pitch;
			gdiff_row_ptr += byr1_pitch;
		}
	}

	// Set the image parameters for each channel
	for (i = 0; i < 4; i++)
	{
		IMAGE *image = frame->channel[i];
		int band;

		// Set the image scale
		for (band = 0; band < IMAGE_NUM_BANDS; band++)
			image->scale[band] = 1;

		// Set the pixel type
		image->pixel_type[0] = PIXEL_TYPE_16S;
	}
}


void ConvertRGBA64ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int precision, int origformat, int alpha)
{
	const int num_channels = alpha ? 4 : 3;

	uint8_t *rgb_row_ptr = data;
	int rgb_row_pitch = pitch;

	PIXEL *color_plane[4];
	int color_pitch[4];
	int frame_width = 0;
	int frame_height = 0;
	int display_height;
	int rowp;
	int i;

	uint8_t *r_row_ptr;
	uint8_t *g_row_ptr;
	uint8_t *b_row_ptr;
	uint8_t *a_row_ptr = NULL;

	int r_row_pitch;
	int g_row_pitch;
	int b_row_pitch;
	int a_row_pitch = 0;

	//int shift = 20;			// Shift down to form a 10 bit pixel

	//const int max_rgb = USHRT_MAX;

	assert(frame != NULL);
	if (! (frame != NULL)) return;

	display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < num_channels; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		color_plane[i] = image->band[0];
		color_pitch[i] = image->pitch;

		// The first channel establishes the processing dimensions
		if (i == 0) {
			frame_width = image->width;
			frame_height = image->height;
		}
	}

	// Swap the chroma values
	r_row_ptr = (uint8_t *)color_plane[0];			r_row_pitch = color_pitch[0];
	g_row_ptr = (uint8_t *)color_plane[1];			g_row_pitch = color_pitch[1];
	b_row_ptr = (uint8_t *)color_plane[2];			b_row_pitch = color_pitch[2];
	if (alpha)
	{
		a_row_ptr = (uint8_t *)color_plane[3];		a_row_pitch = color_pitch[3];
	}

	for (rowp = 0; rowp < frame_height/*display_height*/; rowp++) //DAN20090215 File the frame with edge to prevent ringing artifacts.
	{
		// Start at the leftmost column
		int column = 0;
		int row = rowp < display_height ? rowp : display_height-1;

		// Start at the leftmost column

		//TODO: Add optimized code
#if (1 && XMMOPT)

#endif

		// Pointer into the RGB input row
		PIXEL16U *rgb_ptr = (PIXEL16U *)rgb_row_ptr;

		// Pointers into the YUV output rows
		PIXEL16U *r_ptr = (PIXEL16U *)r_row_ptr;
		PIXEL16U *g_ptr = (PIXEL16U *)g_row_ptr;
		PIXEL16U *b_ptr = (PIXEL16U *)b_row_ptr;
		PIXEL16U *a_ptr = (PIXEL16U *)a_row_ptr;

		//int channeldepth = pitch * 8 / frame_width;

		rgb_ptr += (rgb_row_pitch/2)*row;

		if(origformat == COLOR_FORMAT_RG30 || origformat == COLOR_FORMAT_AB10 ) // RG30
		{
			unsigned int *rgb_Lptr = (unsigned int *)rgb_row_ptr;
			int shift = precision - 10;
			// Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int val;
				int r, g, b;

				// Load the first set of ARGB values (skip the alpha value)
				val = *(rgb_Lptr++)<<shift;
				r = val & 0xffc; val >>= 10;
				g = val & 0xffc; val >>= 10;
				b = val & 0xffc;

				*(r_ptr++) = g;
				*(g_ptr++) = r;
				*(b_ptr++) = b;
			}
		}
		else if(origformat == COLOR_FORMAT_AR10 ) // AR10
		{
			unsigned int *rgb_Lptr = (unsigned int *)rgb_row_ptr;
			int shift = precision - 10;
			// Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int val;
				int r, g, b;

				// Load the first set of ARGB values (skip the alpha value)
				val = *(rgb_Lptr++)<<shift;
				b = val & 0xffc; val >>= 10;
				g = val & 0xffc; val >>= 10;
				r = val & 0xffc;

				*(r_ptr++) = g;
				*(g_ptr++) = r;
				*(b_ptr++) = b;
			}
		}
		else if(origformat == COLOR_FORMAT_R210 ) // R210
		{
			unsigned int *rgb_Lptr = (unsigned int *)rgb_row_ptr;
			int shift = 12 - precision;
			// Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int val;
				int r, g, b;

				// Load the first set of ARGB values (skip the alpha value)
				val = _bswap(*(rgb_Lptr++));
				b = val & 0xffc; val >>= 10;
				g = val & 0xffc; val >>= 10;
				r = val & 0xffc;

				*(r_ptr++) = g>>shift;
				*(g_ptr++) = r>>shift;
				*(b_ptr++) = b>>shift;
			}
		}
		else if(origformat == COLOR_FORMAT_DPX0 ) // DPX0
		{
			unsigned int *rgb_Lptr = (unsigned int *)rgb_row_ptr;
			int shift = 12 - precision;
			// Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int val;
				int r, g, b;

				// Load the first set of ARGB values (skip the alpha value)
				val = _bswap(*(rgb_Lptr++));
				r = val & 0xffc; val >>= 10;
				g = val & 0xffc; val >>= 10;
				b = val & 0xffc;

				*(r_ptr++) = g>>shift;
				*(g_ptr++) = r>>shift;
				*(b_ptr++) = b>>shift;
			}
		}
		else
		{
			int shift = 16 - precision;
			// Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int r, g, b, a;

				// Load the first set of ARGB values (skip the alpha value)
				r = *(rgb_ptr++);
				g = *(rgb_ptr++);
				b = *(rgb_ptr++);
				a = *(rgb_ptr++);

				// Clamp the values
			/*	if (r < 0) r = 0;
				if (r > YU10_MAX) r = YU10_MAX;

				if (g < 0) g = 0;
				if (g > YU10_MAX) g = YU10_MAX;

				if (b < 0) b = 0;
				if (b > YU10_MAX) b = YU10_MAX;*/

				// Store
	#if 1
				*(r_ptr++) = g>>shift;
				*(g_ptr++) = r>>shift;
				*(b_ptr++) = b>>shift;
	#elif 0
				*(r_ptr++) = (g)>>shift;
				*(g_ptr++) = (((r-g)>>1) + 32768)>>shift; //r
				*(b_ptr++) = (((b-g)>>1) + 32768)>>shift; //b;
	#endif
				if(alpha)
				{
					a >>= shift;
					// This help preserve the encoding of alpha channel extremes 0 and 1.  Alpha encoding curve
					if(a > 0 && a < 4095)
					{
						// step function 0 = 0, 0.0001 = 16/255, 0.9999 = 239/256, 1 = 255/256
						a *= 223;
						a += 128;
						a >>= 8;
						a += 16<<4;
					}
				//	if (a < 0) a = 0;
				//	if (a > YU10_MAX) a = YU10_MAX;
					*(a_ptr++) = a;
				}
			}
		}

		// Advance the row pointers
		r_row_ptr += r_row_pitch;
		g_row_ptr += g_row_pitch;
		b_row_ptr += b_row_pitch;
		a_row_ptr += a_row_pitch;
	}
}


void ConvertRGB48ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int precision, int origformat)
{
	const int num_channels = 3;

	uint8_t *rgb_row_ptr = data;
	int rgb_row_pitch = pitch;

	PIXEL *color_plane[3];
	int color_pitch[3];
	int frame_width = 0;
	int frame_height = 0;
	int display_height;
	int rowp;
	int i;

	uint8_t *r_row_ptr;
	uint8_t *g_row_ptr;
	uint8_t *b_row_ptr;

	int r_row_pitch;
	int g_row_pitch;
	int b_row_pitch;

	//int shift = 20;			// Shift down to form a 10 bit pixel

	//const int max_rgb = USHRT_MAX;

	assert(frame != NULL);
	if (! (frame != NULL)) return;

	display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < num_channels; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		color_plane[i] = image->band[0];
		color_pitch[i] = image->pitch;

		// The first channel establishes the processing dimensions
		if (i == 0) {
			frame_width = image->width;
			frame_height = image->height;
		}
	}

	// Swap the chroma values
	r_row_ptr = (uint8_t *)color_plane[0];			r_row_pitch = color_pitch[0];
	g_row_ptr = (uint8_t *)color_plane[1];			g_row_pitch = color_pitch[1];
	b_row_ptr = (uint8_t *)color_plane[2];			b_row_pitch = color_pitch[2];

	for (rowp = 0; rowp < frame_height/*display_height*/; rowp++) //DAN20090215 File the frame with edge to prevent ringing artifacts.
	{
		// Start at the leftmost column
		int column = 0;
		int row = rowp < display_height ? rowp : display_height-1;

		// Start at the leftmost column

		//TODO: Add optimized code
#if (1 && XMMOPT)

#endif

		// Pointer into the RGB input row
		PIXEL16U *rgb_ptr = (PIXEL16U *)rgb_row_ptr;

		// Pointers into the YUV output rows
		PIXEL16U *r_ptr = (PIXEL16U *)r_row_ptr;
		PIXEL16U *g_ptr = (PIXEL16U *)g_row_ptr;
		PIXEL16U *b_ptr = (PIXEL16U *)b_row_ptr;

		//int channeldepth = pitch * 8 / frame_width;

		rgb_ptr += (rgb_row_pitch/2)*row;

		if(origformat == COLOR_FORMAT_RG30 || origformat == COLOR_FORMAT_AB10 ) // RG30
		{
			unsigned int *rgb_Lptr = (unsigned int *)rgb_row_ptr;
			int shift = precision - 10;
			// Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int val;
				int r, g, b;

				// Load the first set of ARGB values (skip the alpha value)
				val = *(rgb_Lptr++)<<shift;
				r = val & 0xffc; val >>= 10;
				g = val & 0xffc; val >>= 10;
				b = val & 0xffc;

				*(r_ptr++) = g;
				*(g_ptr++) = r;
				*(b_ptr++) = b;
			}
		}
		else if(origformat == COLOR_FORMAT_AR10 ) // AR10
		{
			unsigned int *rgb_Lptr = (unsigned int *)rgb_row_ptr;
			int shift = precision - 10;
			// Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int val;
				int r, g, b;

				// Load the first set of ARGB values (skip the alpha value)
				val = *(rgb_Lptr++)<<shift;
				b = val & 0xffc; val >>= 10;
				g = val & 0xffc; val >>= 10;
				r = val & 0xffc;

				*(r_ptr++) = g;
				*(g_ptr++) = r;
				*(b_ptr++) = b;
			}
		}
		else if(origformat == COLOR_FORMAT_R210 ) // R210
		{
			unsigned int *rgb_Lptr = (unsigned int *)rgb_row_ptr;
			int shift = 12 - precision;
			// Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int val;
				int r, g, b;

				// Load the first set of ARGB values (skip the alpha value)
				val = _bswap(*(rgb_Lptr++));
				b = val & 0xffc; val >>= 10;
				g = val & 0xffc; val >>= 10;
				r = val & 0xffc;

				*(r_ptr++) = g>>shift;
				*(g_ptr++) = r>>shift;
				*(b_ptr++) = b>>shift;
			}
		}
		else if(origformat == COLOR_FORMAT_DPX0 ) // DPX0
		{
			unsigned int *rgb_Lptr = (unsigned int *)rgb_row_ptr;
			int shift = 12 - precision;
			// Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int val;
				int r, g, b;

				// Load the first set of ARGB values (skip the alpha value)
				val = _bswap(*(rgb_Lptr++));
				r = val & 0xffc; val >>= 10;
				g = val & 0xffc; val >>= 10;
				b = val & 0xffc;

				*(r_ptr++) = g>>shift;
				*(g_ptr++) = r>>shift;
				*(b_ptr++) = b>>shift;
			}
		}
		else
		{
			int shift = 16 - precision;
			// Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int r, g, b;

				// Load the first set of ARGB values (skip the alpha value)
				r = *(rgb_ptr++);
				g = *(rgb_ptr++);
				b = *(rgb_ptr++);

				// Clamp the values
			/*	if (r < 0) r = 0;
				if (r > YU10_MAX) r = YU10_MAX;

				if (g < 0) g = 0;
				if (g > YU10_MAX) g = YU10_MAX;

				if (b < 0) b = 0;
				if (b > YU10_MAX) b = YU10_MAX;*/

				// Store
	#if 1
				*(r_ptr++) = g>>shift;
				*(g_ptr++) = r>>shift;
				*(b_ptr++) = b>>shift;
	#elif 0
				*(r_ptr++) = (g)>>shift;
				*(g_ptr++) = (((r-g)>>1) + 32768)>>shift; //r
				*(b_ptr++) = (((b-g)>>1) + 32768)>>shift; //b;
	#endif
			}
		}

		// Advance the row pointers
		r_row_ptr += r_row_pitch;
		g_row_ptr += g_row_pitch;
		b_row_ptr += b_row_pitch;
	}
}

void ConvertRGBtoRGB48(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int precision)
{
	const int num_channels = 3;

	uint8_t *rgb_row_ptr = data;
	int rgb_row_pitch = pitch;

	PIXEL *color_plane[4];
	int color_pitch[4];
	int frame_width;
	int frame_height;
	int display_height;
	int row;
	int i;

	uint8_t *r_row_ptr;
	uint8_t *g_row_ptr;
	uint8_t *b_row_ptr;

	int r_row_pitch;
	int g_row_pitch;
	int b_row_pitch;

	//int shift = 20;			// Shift down to form a 10 bit pixel

	//const int max_rgb = USHRT_MAX;

	assert(frame != NULL);
	if (! (frame != NULL)) return;

	display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < num_channels; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		color_plane[i] = image->band[0];
		color_pitch[i] = image->pitch;

		// The first channel establishes the processing dimensions
		if (i == 0) {
			frame_width = image->width;
			frame_height = image->height;
		}
	}

	// Swap the chroma values
	r_row_ptr = (uint8_t *)color_plane[0];		r_row_pitch = color_pitch[0];
	g_row_ptr = (uint8_t *)color_plane[1];		g_row_pitch = color_pitch[1];
	b_row_ptr = (uint8_t *)color_plane[2];		b_row_pitch = color_pitch[2];
//	if(alpha)
//	{
//		a_row_ptr = (uint8_t *)color_plane[3];		a_row_pitch = color_pitch[3];
//	}


	for (row = 0; row < display_height; row++)
	{
		// Start at the leftmost column
		int column = 0;

		//TODO: Add optimized code
#if (1 && XMMOPT)

#endif

		// Pointer into the RGB input row
		uint8_t *rgb_ptr = (uint8_t *)rgb_row_ptr;

		// Pointers into the YUV output rows
		PIXEL16U *r_ptr = (PIXEL16U *)r_row_ptr;
		PIXEL16U *g_ptr = (PIXEL16U *)g_row_ptr;
		PIXEL16U *b_ptr = (PIXEL16U *)b_row_ptr;


		rgb_ptr += (display_height - 1 - row) * rgb_row_pitch;
		{
			// Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int r, g, b;

				// Load the first set of ARGB values (skip the alpha value)
				b = *(rgb_ptr++);
				g = *(rgb_ptr++);
				r = *(rgb_ptr++);

				// Store
				*(r_ptr++) = g<<4;  // 8bit to 12bit
				*(g_ptr++) = r<<4;
				*(b_ptr++) = b<<4;
			//	if(alpha)
			//	{
			//		a = *(rgb_ptr++)>>shift;
			//		*(a_ptr++) = a;
			//	}
			}
		}

		// Advance the row pointers
		r_row_ptr += r_row_pitch;
		g_row_ptr += g_row_pitch;
		b_row_ptr += b_row_pitch;
	}
}






void ConvertRGBAtoRGB48(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int precision, int rgbaswap)
{
	const int num_channels = 3;

	uint8_t *rgb_row_ptr = data;
	int rgb_row_pitch = pitch;

	PIXEL *color_plane[4];
	int color_pitch[4];
	int frame_width;
	int frame_height;
	int display_height;
	int rowp;
	int i;

	uint8_t *r_row_ptr;
	uint8_t *g_row_ptr;
	uint8_t *b_row_ptr;

	int r_row_pitch;
	int g_row_pitch;
	int b_row_pitch;

	//int shift = 20;			// Shift down to form a 10 bit pixel

	//const int max_rgb = USHRT_MAX;

	assert(frame != NULL);
	if (! (frame != NULL)) return;

	display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < num_channels; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		color_plane[i] = image->band[0];
		color_pitch[i] = image->pitch;

		// The first channel establishes the processing dimensions
		if (i == 0) {
			frame_width = image->width;
			frame_height = image->height;
		}
	}

	// Swap the chroma values
	r_row_ptr = (uint8_t *)color_plane[0];		r_row_pitch = color_pitch[0];
	g_row_ptr = (uint8_t *)color_plane[1];		g_row_pitch = color_pitch[1];
	b_row_ptr = (uint8_t *)color_plane[2];		b_row_pitch = color_pitch[2];
//	if(alpha)
//	{
//		a_row_ptr = (uint8_t *)color_plane[3];		a_row_pitch = color_pitch[3];
//	}


	for (rowp = 0; rowp < frame_height/*display_height*/; rowp++) //DAN20090215 File the frame with edge to prevent ringing artifacts.
	{
		// Start at the leftmost column
		int column = 0;
		int row = rowp < display_height ? rowp : display_height-1;

		//TODO: Add optimized code
#if (1 && XMMOPT)

#endif

		// Pointer into the RGB input row
		uint8_t *rgb_ptr = (uint8_t *)rgb_row_ptr;

		// Pointers into the YUV output rows
		PIXEL16U *r_ptr = (PIXEL16U *)r_row_ptr;
		PIXEL16U *g_ptr = (PIXEL16U *)g_row_ptr;
		PIXEL16U *b_ptr = (PIXEL16U *)b_row_ptr;


		rgb_ptr += (display_height - 1 - row) * rgb_row_pitch;

		if(rgbaswap) //ARGB
		{
            // Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int r, g, b;

				// Load the first set of ARGB values (skip the alpha value)
				rgb_ptr++;
				r = *(rgb_ptr++);
				g = *(rgb_ptr++);
				b = *(rgb_ptr++);

				// Store
				*(r_ptr++) = g<<4;  // 8bit to 12bit
				*(g_ptr++) = r<<4;
				*(b_ptr++) = b<<4;
			}
		}
		else //BGRA
		{
			// Process the rest of the column
			for (; column < frame_width; column ++)
			{
				int r, g, b;

				// Load the first set of ARGB values (skip the alpha value)
				b = *(rgb_ptr++);
				g = *(rgb_ptr++);
				r = *(rgb_ptr++);
				rgb_ptr++;

				// Store
				*(r_ptr++) = g<<4;  // 8bit to 12bit
				*(g_ptr++) = r<<4;
				*(b_ptr++) = b<<4;
			}
		}

		// Advance the row pointers
		r_row_ptr += r_row_pitch;
		g_row_ptr += g_row_pitch;
		b_row_ptr += b_row_pitch;
	}
}




void ConvertRGBAtoRGBA64(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int precision, int rgbaswap)
{
	const int num_channels = 4;

	uint8_t *rgb_row_ptr = data;
	int rgb_row_pitch = pitch;

	PIXEL *color_plane[4];
	int color_pitch[4];
	int frame_width;
	int frame_height;
	int display_height;
	int row;
	int i;

	uint8_t *r_row_ptr;
	uint8_t *g_row_ptr;
	uint8_t *b_row_ptr;
	uint8_t *a_row_ptr;

	int r_row_pitch;
	int g_row_pitch;
	int b_row_pitch;
	int a_row_pitch;

	//int shift = 20;			// Shift down to form a 10 bit pixel

	//const int max_rgb = USHRT_MAX;

	assert(frame != NULL);
	if (! (frame != NULL)) return;

	display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < num_channels; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		color_plane[i] = image->band[0];
		color_pitch[i] = image->pitch;

		// The first channel establishes the processing dimensions
		if (i == 0) {
			frame_width = image->width;
			frame_height = image->height;
		}
	}

	// Swap the chroma values
	r_row_ptr = (uint8_t *)color_plane[0];		r_row_pitch = color_pitch[0];
	g_row_ptr = (uint8_t *)color_plane[1];		g_row_pitch = color_pitch[1];
	b_row_ptr = (uint8_t *)color_plane[2];		b_row_pitch = color_pitch[2];
	a_row_ptr = (uint8_t *)color_plane[3];		a_row_pitch = color_pitch[3];


	for (row = 0; row < display_height; row++)
	{
		// Start at the leftmost column
		int column = 0;

		//TODO: Add optimized code
#if (1 && XMMOPT)

#endif

		// Pointer into the RGB input row
		uint8_t *rgb_ptr = (uint8_t *)rgb_row_ptr;

		// Pointers into the YUV output rows
		PIXEL16U *r_ptr = (PIXEL16U *)r_row_ptr;
		PIXEL16U *g_ptr = (PIXEL16U *)g_row_ptr;
		PIXEL16U *b_ptr = (PIXEL16U *)b_row_ptr;
		PIXEL16U *a_ptr = (PIXEL16U *)a_row_ptr;


		rgb_ptr += (display_height - 1 - row) * rgb_row_pitch;

		if(rgbaswap) //ARGB
		{
			for (; column < frame_width; column ++)
			{
				int r, g, b, a;

				// Load the first set of ARGB values (skip the alpha value)
				a = *(rgb_ptr++)<<4; // 8bit to 12bit
				r = *(rgb_ptr++)<<4;
				g = *(rgb_ptr++)<<4;
				b = *(rgb_ptr++)<<4;

				// This help preserve the encoding of alpha channel extremes 0 and 1. Alpha encoding curve
				if(a > 0 && a < (255<<4))
				{
					// step function 0 = 0, 0.0001 = 16/255, 0.9999 = 239/256, 1 = 255/256
					a *= 223;
					a += 128;
					a >>= 8;
					a += 16<<4;
				}

				// Store
				*(r_ptr++) = g;
				*(g_ptr++) = r;
				*(b_ptr++) = b;
				*(a_ptr++) = a;
			}
		}
		else //BGRA
		{
			// Process the rest of the column

			for (; column < frame_width; column ++)
			{
				int r, g, b, a;

				// Load the first set of ARGB values (skip the alpha value)
				b = *(rgb_ptr++)<<4; // 8bit to 12bit
				g = *(rgb_ptr++)<<4;
				r = *(rgb_ptr++)<<4;
				a = *(rgb_ptr++)<<4;

				// This help preserve the encoding of alpha channel extremes 0 and 1. Alpha encoding curve
				if(a > 0 && a < (255<<4))
				{
					// step function 0 = 0, 0.0001 = 16/255, 0.9999 = 239/256, 1 = 255/256
					a *= 223;
					a += 128;
					a >>= 8;
					a += 16<<4;
				}

				// Store
				*(r_ptr++) = g;
				*(g_ptr++) = r;
				*(b_ptr++) = b;
				*(a_ptr++) = a;
			}
		}

		// Advance the row pointers
		r_row_ptr += r_row_pitch;
		g_row_ptr += g_row_pitch;
		b_row_ptr += b_row_pitch;
		a_row_ptr += a_row_pitch;
	}
}


/*
	Convert QuickTime format b64a to a frame of planar RGBA.
	This routine was adapted from ConvertBGRA64ToFrame16s.
	The alpha channel is currently ignored.
*/
CODEC_ERROR ConvertBGRA64ToFrame_4444_16s(uint8_t *data, int pitch, FRAME *frame,
										  uint8_t *buffer, int precision)
{
	//#pragma unused(buffer);

	//TODO: Add code to write the alpha channel into the fourth plane
	int num_channels;

	uint8_t *rgb_row_ptr = data;
	int rgb_row_pitch = pitch;

	PIXEL *color_plane[FRAME_MAX_CHANNELS];
	int color_pitch[FRAME_MAX_CHANNELS];
	int frame_width;
	int frame_height;
	int display_height;
	int row;
	int i;

	uint8_t *r_row_ptr;
	uint8_t *g_row_ptr;
	uint8_t *b_row_ptr;
	uint8_t *a_row_ptr = NULL;

	int r_row_pitch;
	int g_row_pitch;
	int b_row_pitch;
	int a_row_pitch = 0;

	//int shift = 20;			// Shift down to form a 10-bit pixel
	int shift = 16 - precision;
	int channel_depth;

	bool alpha_flag;

	//const int max_rgb = USHRT_MAX;

	//TODO: Need to return error codes
	assert(frame != NULL);
	if (! (frame != NULL)) {
		return CODEC_ERROR_INVALID_ARGUMENT;
	}

	assert(frame->format == FRAME_FORMAT_RGB || frame->format == FRAME_FORMAT_RGBA);
	if (! (frame->format == FRAME_FORMAT_RGB || frame->format == FRAME_FORMAT_RGBA)) {
		return CODEC_ERROR_BAD_FRAME;
	}

	alpha_flag = (frame->format == FRAME_FORMAT_RGBA);
	num_channels = (alpha_flag ? 4 : 3);

	// Check that the frame was allocated with enough channels
	assert(frame->num_channels >= num_channels);

	//TODO: Set the alpha flag and number of channels using the values in the frame data structure

	display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < num_channels; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		color_plane[i] = image->band[0];
		color_pitch[i] = image->pitch;

		// The first channel establishes the frame dimensions
		if (i == 0) {
			frame_width = image->width;
			frame_height = image->height;
		}
	}

	// This routine does not handle the RG30 format
	channel_depth = pitch * 8 / frame_width;
	assert(channel_depth != 32);
	if (! (channel_depth != 32)) {
		return CODEC_ERROR_BADFORMAT;
	}

	// Set the row pointers for each channel to the correct plane
	r_row_ptr = (uint8_t *)color_plane[1];		r_row_pitch = color_pitch[1];
	g_row_ptr = (uint8_t *)color_plane[0];		g_row_pitch = color_pitch[0];
	b_row_ptr = (uint8_t *)color_plane[2];		b_row_pitch = color_pitch[2];
	if (alpha_flag)
	{
		a_row_ptr = (uint8_t *)color_plane[3];	a_row_pitch = color_pitch[3];
	}

	for (row = 0; row < display_height; row++)
	{
		// Start at the leftmost column
		int column = 0;

		//TODO: Process each row by calling an optimized subroutine
#if (1 && XMMOPT)

#endif

		// Pointer into the RGB input row
		PIXEL16U *rgb_ptr = (PIXEL16U *)rgb_row_ptr;

		// Pointers into the output rows for each plane
		PIXEL16U *r_ptr = (PIXEL16U *)r_row_ptr;
		PIXEL16U *g_ptr = (PIXEL16U *)g_row_ptr;
		PIXEL16U *b_ptr = (PIXEL16U *)b_row_ptr;
		PIXEL16U *a_ptr = (PIXEL16U *)a_row_ptr;

		// Process the rest of the column
		for (; column < frame_width; column ++)
		{
			int r, g, b, a;

			// Load the first set of ARGB values
			a = *(rgb_ptr++);
			r = *(rgb_ptr++);
			g = *(rgb_ptr++);
			b = *(rgb_ptr++);

			// Shift the 16-bit pixels to the encoded precision
			*(r_ptr++) = r >> shift;
			*(g_ptr++) = g >> shift;
			*(b_ptr++) = b >> shift;

			if (alpha_flag)
			{
				//*(a_ptr++) = a >> shift;
				a >>= shift;
				// This help preserve the encoding of alpha channel extremes 0 and 1.  Alpha encoding curve
				if(a > 0 && a < 4095)
				{
					// step function 0 = 0, 0.0001 = 16/255, 0.9999 = 239/256, 1 = 255/256
					a *= 223;
					a += 128;
					a >>= 8;
					a += 16<<4;
				}
			//	if (a < 0) a = 0;
			//	if (a > YU10_MAX) a = YU10_MAX;
				*(a_ptr++) = a;
			}
		}

		// Advance the input row pointer
		rgb_row_ptr += rgb_row_pitch;

		// Advance the output row pointers
		r_row_ptr += r_row_pitch;
		g_row_ptr += g_row_pitch;
		b_row_ptr += b_row_pitch;
		a_row_ptr += a_row_pitch;
	}

	// Successful conversion
	return CODEC_ERROR_OKAY;
}


/*
	Convert QuickTime format b64a to a frame of planar YUV.
*/
void ConvertAnyDeep444to422(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int color_space, int origformat)
{
	//#pragma unused(buffer);

	const int num_channels = 3;

	uint8_t *rgb_row_ptr = data;
	int rgb_row_pitch = pitch;

	PIXEL *color_plane[3];
	int color_pitch[3];
	int frame_width;
	int frame_height;
	int display_height;
	int row;
	int i;

	uint8_t *y_row_ptr;
	uint8_t *u_row_ptr;
	uint8_t *v_row_ptr;

	int y_row_pitch;
	int u_row_pitch;
	int v_row_pitch;

	//int shift = 14;		// Shift down to form a 16 bit pixel
	int shift = 20;			// Shift down to form a 10 bit pixel

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

	//const int max_rgb = USHRT_MAX;

	assert(frame != NULL);
	if (! (frame != NULL)) return;

	// The frame format should be three channels of YUV (4:2:2 format)
	assert(frame->num_channels == num_channels);
	assert(frame->format == FRAME_FORMAT_YUV);

	display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < num_channels; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		color_plane[i] = image->band[0];
		color_pitch[i] = image->pitch;

		// The first channel establishes the processing dimensions
		if (i == 0) {
			frame_width = image->width;
			frame_height = image->height;
		}
	}

#if 0
	// Invert the input image
	rgb_row_ptr += (display_height - 1) * rgb_row_pitch;
	rgb_row_pitch = NEG(rgb_row_pitch);
#endif

	// Swap the chroma values
	y_row_ptr = (uint8_t *)color_plane[0];		y_row_pitch = color_pitch[0];
	u_row_ptr = (uint8_t *)color_plane[2];		u_row_pitch = color_pitch[2];
	v_row_ptr = (uint8_t *)color_plane[1];		v_row_pitch = color_pitch[1];


	// Select the coefficients corresponding to the color space
	switch(color_space & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:		// Computer systems 601

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

		//
		// Fixed point approximation (14-bit) is
		//
		// Y  = ( 4211.R + 8258.G + 1606.B) >> 14) + y_offset;
		// Cb = (-2425.R - 4768.G + 7193.B) >> 14) + u_offset;
		// Cr = ( 7193.R - 6029.G - 1163.B) >> 14) + v_offset;

		y_rmult = 4211;
		y_gmult = 8258;
		y_bmult = 1606;
		y_offset = 64;

		u_rmult = 2425;
		u_gmult = 4768;
		u_bmult = 7193;
		u_offset = 512;

		v_rmult = 7193;
		v_gmult = 6029;
		v_bmult = 1163;
		v_offset = 512;

		break;

	case COLOR_SPACE_VS_601:		// Video systems 601

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
		//
		// Fixed point approximation (14-bit) is
		//
		// Y  = ( 4899.R + 9617.G + 1868.B) >> 14) + y_offset;
		// Cb = (-2818.R - 5554.G + 8372.B) >> 14) + u_offset;
		// Cr = ( 8372.R - 7012.G - 1360.B) >> 14) + v_offset;

		y_rmult = 4899;
		y_gmult = 9617;
		y_bmult = 1868;
		y_offset = 0;

		u_rmult = 2818;
		u_gmult = 5554;
		u_bmult = 8372;
		u_offset = 512;

		v_rmult = 8372;
		v_gmult = 7012;
		v_bmult = 1360;
		v_offset = 512;

		break;

	default: assert(0);
	case COLOR_SPACE_CG_709:		// Computer systems 709

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
		//
		// Fixed point approximation (14-bit) is
		//
		// Y  = ( 2998.R + 10060.G + 1016.B) >> 14) + y_offset;
		// Cb = (-1655.R -  5538.G + 7193.B) >> 14) + u_offset;
		// Cr = ( 7193.R -  6537.G -  655.B) >> 14) + v_offset;

		y_rmult = 2998;
		y_gmult = 10060;
		y_bmult = 1016;
		y_offset = 64;

		u_rmult = 1655;
		u_gmult = 5538;
		u_bmult = 7193;
		u_offset = 512;

		v_rmult = 7193;
		v_gmult = 6537;
		v_bmult = 655;
		v_offset = 512;

		break;

	case COLOR_SPACE_VS_709:		// Video systems 709

		// video systems RGB + 709
		// Floating point arithmetic is
		// Y = 0.213R + 0.715G + 0.072B
		// Cb = -0.117R - 0.394G + 0.511B + 128
		// Cr = 0.511R - 0.464G - 0.047B + 128
		//
		// Fixed point approximation (8-bit) is
		//
		// Y  = ( 55R + 183G +  18B +  128) >> 4;
		// Cb = (-30R - 101G + 131B + 32896) >> 4;
		// Cr = (131R - 119G -  12B + 32896) >> 4;
		//
		// Fixed point approximation (14-bit) is
		//
		// Y  = ( 3490.R + 11715.G + 1180.B) >> 14) + y_offset;
		// Cb = (-1917.R -  6455.G + 8372.B) >> 14) + u_offset;
		// Cr = ( 8372.R -  7602.G -  770.B) >> 14) + v_offset;

		y_rmult = 3490;
		y_gmult = 11715;
		y_bmult = 1180;
		y_offset = 0;

		u_rmult = 1917;
		u_gmult = 6455;
		u_bmult = 8372;
		u_offset = 512;

		v_rmult = 8372;
		v_gmult = 7602;
		v_bmult = 770;
		v_offset = 512;
		break;
	}

	for (row = 0; row < frame->height; row++) //DAN20170725 Fix of odd heights
	{
		// Start at the leftmost column
		int column = 0;

		//TODO: Add optimized code
#if (1 && XMMOPT)

#endif

		// Pointer into the RGB input row
		PIXEL16U *rgb_ptr = (PIXEL16U *)rgb_row_ptr;
		uint32_t *rgb10_ptr = (uint32_t *)rgb_row_ptr;

		// Pointers into the YUV output rows
		PIXEL16U *y_ptr = (PIXEL16U *)y_row_ptr;
		PIXEL16U *u_ptr = (PIXEL16U *)u_row_ptr;
		PIXEL16U *v_ptr = (PIXEL16U *)v_row_ptr;

		// Process the rest of the column
		for (; column < frame_width; column += 2)
		{
			int r, g, b;
			int y, u, v;
			uint32_t val;

			// Load the first set of ARGB values (skip the alpha value)
			switch(origformat)
			{
			case COLOR_FORMAT_R210:
				//*outA32++ = _bswap((r<<20)|(g<<10)|(b));
				val = _bswap(*(rgb10_ptr++));
				r = (val>>14) & 0xffc0;
				g = (val>>4) & 0xffc0;
				b = (val<<6) & 0xffc0;
				break;
			case COLOR_FORMAT_DPX0:
				//*outA32++ = _bswap((r<<22)|(g<<12)|(b<<2));
				val = _bswap(*(rgb10_ptr++));
				r = (val>>16) & 0xffc0;
				g = (val>>6) & 0xffc0;
				b = (val<<4) & 0xffc0;
				break;
			case COLOR_FORMAT_RG30:
			case COLOR_FORMAT_AB10:
				//*outA32++ = r|(g<<10)|(b<<20);
				val = *(rgb10_ptr++);
				b = (val>>14) & 0xffc0;
				g = (val>>4) & 0xffc0;
				r = (val<<6) & 0xffc0;
				break;
			case COLOR_FORMAT_AR10:
				//*outA32++ = (r<<20)|(g<<10)|(b);	
				val = *(rgb10_ptr++);
				r = (val>>14) & 0xffc0;
				g = (val>>4) & 0xffc0;
				b = (val<<6) & 0xffc0;
				break;				
			case COLOR_FORMAT_RG48:
				r = *(rgb_ptr++);
				g = *(rgb_ptr++);
				b = *(rgb_ptr++);
				break;
			case COLOR_FORMAT_RG64:
				r = *(rgb_ptr++);
				g = *(rgb_ptr++);
				b = *(rgb_ptr++);
				rgb_ptr++;
				break;
			case COLOR_FORMAT_B64A:
				rgb_ptr++;
				r = *(rgb_ptr++);
				g = *(rgb_ptr++);
				b = *(rgb_ptr++);
				break;

			default:
				// Eliminate compiler warning about using uninitialized variable
				r = g = b = 0;
				break;
			}

			// Convert RGB to YCbCr
			y = (( y_rmult * r + y_gmult * g + y_bmult * b) >> shift) + y_offset;
			u = ((-u_rmult * r - u_gmult * g + u_bmult * b) >> shift);
			v = (( v_rmult * r - v_gmult * g - v_bmult * b) >> shift);

			// Clamp and store the first luma value
			if (y < 0) y = 0;
			if (y > YU10_MAX) y = YU10_MAX;
			*(y_ptr++) = y;

			// Load the second set of ARGB values (skip the alpha value)
			switch(origformat)
			{
			case COLOR_FORMAT_R210:
				//*outA32++ = _bswap((r<<20)|(g<<10)|(b));
				val = _bswap(*(rgb10_ptr++));
				r = (val>>14) & 0xffc0;
				g = (val>>4) & 0xffc0;
				b = (val<<6) & 0xffc0;
				break;
			case COLOR_FORMAT_DPX0:
				//*outA32++ = _bswap((r<<22)|(g<<12)|(b<<2));
				val = _bswap(*(rgb10_ptr++));
				r = (val>>16) & 0xffc0;
				g = (val>>6) & 0xffc0;
				b = (val<<4) & 0xffc0;
				break;
			case COLOR_FORMAT_RG30:
			case COLOR_FORMAT_AB10:
				//*outA32++ = r|(g<<10)|(b<<20);
				val = *(rgb10_ptr++);
				b = (val>>14) & 0xffc0;
				g = (val>>4) & 0xffc0;
				r = (val<<6) & 0xffc0;
				break;
			case COLOR_FORMAT_AR10:
				//*outA32++ = (r<<20)|(g<<10)|(b);	
				val = *(rgb10_ptr++);
				r = (val>>14) & 0xffc0;
				g = (val>>4) & 0xffc0;
				b = (val<<6) & 0xffc0;
				break;				
			case COLOR_FORMAT_RG48:
				r = *(rgb_ptr++);
				g = *(rgb_ptr++);
				b = *(rgb_ptr++);
				break;
			case COLOR_FORMAT_RG64:
				r = *(rgb_ptr++);
				g = *(rgb_ptr++);
				b = *(rgb_ptr++);
				rgb_ptr++;
				break;
			case COLOR_FORMAT_B64A:
				rgb_ptr++;
				r = *(rgb_ptr++);
				g = *(rgb_ptr++);
				b = *(rgb_ptr++);
				break;
			}

			// Convert RGB to YCbCr
			y = (( y_rmult * r + y_gmult * g + y_bmult * b) >> shift) + y_offset;

#if !INTERPOLATE_CHROMA
			// The OneRiver Media filter test requires this for correct rendering of the red vertical stripes
			u += ((-u_rmult * r - u_gmult * g + u_bmult * b) >> shift);
			v += (( v_rmult * r - v_gmult * g - v_bmult * b) >> shift);
			u >>= 1;
			v >>= 1;
#endif
			u += u_offset;
			v += v_offset;

			// Clamp the luma and chroma values
			if (y < 0) y = 0;
			if (y > YU10_MAX) y = YU10_MAX;

			if (u < 0) u = 0;
			if (u > YU10_MAX) u = YU10_MAX;

			if (v < 0) v = 0;
			if (v > YU10_MAX) v = YU10_MAX;

			// Store the second luma value
			*(y_ptr++) = y;

			// Store the chroma values
			*(u_ptr++) = u;
			*(v_ptr++) = v;
		}

		// Advance the row pointers
		if(row < display_height-1)	//DAN20170725 - Fix for odd vertical heights
			rgb_row_ptr += rgb_row_pitch;
		y_row_ptr += y_row_pitch;
		u_row_ptr += u_row_pitch;
		v_row_ptr += v_row_pitch;
	}
}

// Pack the lowpass band of RGB 4:4:4 into the specified RGB format
void ConvertLowpassRGB444ToRGB(IMAGE *image_array[], uint8_t *output_buffer,
							   int output_width, int output_height,
							   int32_t output_pitch, int format,
							   bool inverted, int shift, int num_channels)
{
	PIXEL *plane_array[TRANSFORM_MAX_CHANNELS] = {0};
	int pitch_array[TRANSFORM_MAX_CHANNELS] = {0};
	ROI roi = {0, 0};
	int channel;
	//int saturate = 1;

	// Only 24 and 32 bit true color RGB formats are supported
	//assert(format == COLOR_FORMAT_RGB24 || format == COLOR_FORMAT_RGB32);

	// Convert from pixel to byte data
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *image = image_array[channel];

		plane_array[channel] = image->band[0];
		pitch_array[channel] = image->pitch;

		// The overall frame dimensions are determined by the first channel
		if (channel == 0)
		{
			roi.width = image->width;
			roi.height = output_height;// image->height;  //DAN20170725 Fix of odd heights
		}
	}

	switch (format & 0x7ffffff)
	{
	case COLOR_FORMAT_RGB24:
		ConvertLowpassRGB444ToRGB24(plane_array, pitch_array,
									output_buffer, output_pitch,
									roi, inverted, shift);
		break;

	case COLOR_FORMAT_RGB32:
	case COLOR_FORMAT_RGB32_INVERTED:
		ConvertLowpassRGB444ToRGB32(plane_array, pitch_array,
									output_buffer, output_pitch,
									roi, inverted, shift, num_channels);
		break;

	case COLOR_FORMAT_RG48:
		ConvertLowpassRGB444ToRGB48(plane_array, pitch_array,
								   output_buffer, output_pitch,
								   roi, inverted, shift);
		break;
	case COLOR_FORMAT_RG64:
		ConvertLowpassRGB444ToRGBA64(plane_array, pitch_array,
								   output_buffer, output_pitch,
								   roi, inverted, shift);
		break;
	case COLOR_FORMAT_B64A:
		ConvertLowpassRGB444ToB64A(plane_array, pitch_array,
								   output_buffer, output_pitch,
								   roi, inverted, shift, num_channels);
		break;
	case COLOR_FORMAT_RG30:
	case COLOR_FORMAT_AR10:
	case COLOR_FORMAT_AB10:
	case COLOR_FORMAT_R210:
	case COLOR_FORMAT_DPX0:
		ConvertLowpassRGB444ToRGB30(plane_array, pitch_array,
								   output_buffer, output_pitch,
								   roi, inverted, shift, format);
		break;

	default:
		assert(0);		// Unsupported pixel format
		break;
	}
}

void ConvertLowpassRGB444ToRGB24(PIXEL *plane_array[], int pitch_array[],
								 uint8_t *output_buffer, int output_pitch,
								 ROI roi, bool inverted, int shift)
{
	if (inverted && output_pitch>0)
	{
		output_buffer += output_pitch * (roi.height - 1);
		output_pitch = -output_pitch;
	}

	ConvertPlanarRGB16uToPackedRGB24(plane_array, pitch_array, roi,
									 output_buffer, output_pitch, roi.width, 6);
}


void ConvertLowpassRGB444ToRGB32(PIXEL *plane_array[], int pitch_array[],
								 uint8_t *output_buffer, int output_pitch,
								 ROI roi, bool inverted, int shift, int num_channels)
{
	if (inverted && output_pitch>0)
	{
		output_buffer += output_pitch * (roi.height - 1);
		output_pitch = -output_pitch;
	}

	ConvertPlanarRGB16uToPackedRGB32(plane_array, pitch_array, roi,
									 output_buffer, output_pitch, roi.width, 6, num_channels);

}

void ConvertLowpassRGB444ToRGB48(PIXEL *plane_array[], int pitch_array[],
								 uint8_t *output_buffer, int output_pitch,
								 ROI roi, bool inverted, int shift)
{
	PIXEL *r_row_ptr;
	PIXEL *g_row_ptr;
	PIXEL *b_row_ptr;
	int r_row_pitch;
	int g_row_pitch;
	int b_row_pitch;
	//int r_prescale;
	//int g_prescale;
	//int b_prescale;
	PIXEL16U *argb_row_ptr;

	int output_height = roi.height;

	//size_t output_row_size = output_pitch;

	int row;
	int column;

	const bool saturate = true;
	const int rgb_max = USHRT_MAX;
	//const int alpha = USHRT_MAX;

	// Get pointers to the rows in the lowpass band of each channel
	r_row_ptr = plane_array[1];		r_row_pitch = pitch_array[1]/sizeof(PIXEL);
	g_row_ptr = plane_array[0];		g_row_pitch = pitch_array[0]/sizeof(PIXEL);
	b_row_ptr = plane_array[2];		b_row_pitch = pitch_array[2]/sizeof(PIXEL);

	// Convert the output pitch to units of pixels
	output_pitch /= sizeof(PIXEL);

	argb_row_ptr = (PIXEL16U *)output_buffer;
	if (inverted) {
		argb_row_ptr += (output_height - 1) * output_pitch;
		output_pitch = NEG(output_pitch);
	}

	for (row = 0; row < output_height; row++)
	{
		PIXEL16U *argb_ptr = argb_row_ptr;
#if (0 && XMMOPT)
		int column_step = 16;
		int post_column = roi.width - (roi.width % column_step);
		__m64 *r_ptr = (__m64 *)r_row_ptr;
		__m64 *g_ptr = (__m64 *)g_row_ptr;
		__m64 *b_ptr = (__m64 *)b_row_ptr;
		__m64 *argb_ptr = (__m64 *)argb_row_ptr;
#endif
		// Start at the leftmost column
		column = 0;

		// Clear the output row (for debugging)
		//memset(argb_row_ptr, 0, output_row_size);

#if (0 && XMMOPT)

		// Should have exited the loop at the post processing column
		assert(column == post_column);
#endif
		// Process the rest of the row
		for(; column < roi.width; column++)
		{
			int r;
			int g;
			int b;

			// Load the tuple of RGB values
			r = r_row_ptr[column];
			g = g_row_ptr[column];
			b = b_row_ptr[column];

			//r >>= r_prescale;
			//g >>= g_prescale;
			//b >>= b_prescale;

			// Scale the lowpass values to 16 bits
			r <<= shift;
			g <<= shift;
			b <<= shift;

			if (saturate)
			{
				if (r < 0) r = 0; else if (r > rgb_max) r = rgb_max;
				if (g < 0) g = 0; else if (g > rgb_max) g = rgb_max;
				if (b < 0) b = 0; else if (b > rgb_max) b = rgb_max;
			}

			*(argb_ptr++) = r;
			*(argb_ptr++) = g;
			*(argb_ptr++) = b;
		}

		// Advance the row pointers into each lowpass band
		r_row_ptr += r_row_pitch;
		g_row_ptr += g_row_pitch;
		b_row_ptr += b_row_pitch;

		// Advance the row pointer into the output buffer
		argb_row_ptr += output_pitch;
	}
}

void ConvertLowpassRGB444ToRGBA64(PIXEL *plane_array[], int pitch_array[],
								  uint8_t *output_buffer, int output_pitch,
								  ROI roi, bool inverted, int shift)
{
	PIXEL *r_row_ptr;
	PIXEL *g_row_ptr;
	PIXEL *b_row_ptr;
	PIXEL *a_row_ptr;
	int r_row_pitch;
	int g_row_pitch;
	int b_row_pitch;
	int a_row_pitch;
	//int r_prescale;
	//int g_prescale;
	//int b_prescale;
	PIXEL16U *argb_row_ptr;

	int output_height = roi.height;

	//size_t output_row_size = output_pitch;

	int row;
	int column;

	const bool saturate = true;
	const int rgb_max = USHRT_MAX;
	//const int alpha = USHRT_MAX;

	//alpha_Companded likely doesn't set as the is not does to go through Convert4444LinesToOutput

	// Get pointers to the rows in the lowpass band of each channel
	r_row_ptr = plane_array[1];		r_row_pitch = pitch_array[1]/sizeof(PIXEL);
	g_row_ptr = plane_array[0];		g_row_pitch = pitch_array[0]/sizeof(PIXEL);
	b_row_ptr = plane_array[2];		b_row_pitch = pitch_array[2]/sizeof(PIXEL);
	a_row_ptr = plane_array[3];		a_row_pitch = pitch_array[3]/sizeof(PIXEL);

	// Convert the output pitch to units of pixels
	output_pitch /= sizeof(PIXEL);

	argb_row_ptr = (PIXEL16U *)output_buffer;
	if (inverted) {
		argb_row_ptr += (output_height - 1) * output_pitch;
		output_pitch = NEG(output_pitch);
	}

	for (row = 0; row < output_height; row++)
	{
		PIXEL16U *argb_ptr = argb_row_ptr;
#if (0 && XMMOPT)
		int column_step = 16;
		int post_column = roi.width - (roi.width % column_step);
		__m64 *r_ptr = (__m64 *)r_row_ptr;
		__m64 *g_ptr = (__m64 *)g_row_ptr;
		__m64 *b_ptr = (__m64 *)b_row_ptr;
		__m64 *argb_ptr = (__m64 *)argb_row_ptr;
#endif
		// Start at the leftmost column
		column = 0;

		// Clear the output row (for debugging)
		//memset(argb_row_ptr, 0, output_row_size);

#if (0 && XMMOPT)

		// Should have exited the loop at the post processing column
		assert(column == post_column);
#endif
		// Process the rest of the row
		for(; column < roi.width; column++)
		{
			int r;
			int g;
			int b;
			int a;

			// Load the tuple of RGB values
			r = r_row_ptr[column];
			g = g_row_ptr[column];
			b = b_row_ptr[column];
			a = a_row_ptr[column];

			//r >>= r_prescale;
			//g >>= g_prescale;
			//b >>= b_prescale;

			// Scale the lowpass values to 16 bits
			r <<= shift;
			g <<= shift;
			b <<= shift;
			a <<= shift;

			// Remove the alpha encoding curve.
			//a -= 16<<8;
			//a <<= 8;
			//a += 111;
			//a /= 223;
			//12-bit SSE calibrated code
			//a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
			//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
			//a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));

			a >>= 4; //12-bit
			a -= alphacompandDCoffset;
			a <<= 3; //15-bit
			a *= alphacompandGain;
			a >>= 16; //12-bit
			a <<= 4; // 16-bit;

			if (saturate)
			{
				if (r < 0) r = 0; else if (r > rgb_max) r = rgb_max;
				if (g < 0) g = 0; else if (g > rgb_max) g = rgb_max;
				if (b < 0) b = 0; else if (b > rgb_max) b = rgb_max;
				if (a < 0) a = 0; else if (a > rgb_max) a = rgb_max;
			}

			*(argb_ptr++) = r;
			*(argb_ptr++) = g;
			*(argb_ptr++) = b;
			*(argb_ptr++) = a;
		}

		// Advance the row pointers into each lowpass band
		r_row_ptr += r_row_pitch;
		g_row_ptr += g_row_pitch;
		b_row_ptr += b_row_pitch;
		a_row_ptr += a_row_pitch;

		// Advance the row pointer into the output buffer
		argb_row_ptr += output_pitch;
	}
}

void ConvertLowpassRGB444ToB64A(PIXEL *plane_array[], int pitch_array[],
								uint8_t *output_buffer, int output_pitch,
								ROI roi, bool inverted, int shift, int num_channels)
{
	PIXEL *r_row_ptr;
	PIXEL *g_row_ptr;
	PIXEL *b_row_ptr;
	PIXEL *a_row_ptr = NULL;
	int r_row_pitch;
	int g_row_pitch;
	int b_row_pitch;
	int a_row_pitch;
	//int r_prescale;
	//int g_prescale;
	//int b_prescale;
	PIXEL16U *argb_row_ptr;

	int output_height = roi.height;

	//size_t output_row_size = output_pitch;

	int row;
	int column;

	const bool saturate = 1;
	const int rgb_max = USHRT_MAX;
	const int alpha = USHRT_MAX;

	// Get pointers to the rows in the lowpass band of each channel
	r_row_ptr = (PIXEL *)plane_array[1];		r_row_pitch = pitch_array[1]/sizeof(PIXEL);
	g_row_ptr = (PIXEL *)plane_array[0];		g_row_pitch = pitch_array[0]/sizeof(PIXEL);
	b_row_ptr = (PIXEL *)plane_array[2];		b_row_pitch = pitch_array[2]/sizeof(PIXEL);
	if (num_channels == 4) {
		a_row_ptr = (PIXEL *)plane_array[3];	a_row_pitch = pitch_array[3]/sizeof(PIXEL);
	}

	// Convert the output pitch to units of pixels
	output_pitch /= sizeof(PIXEL);

	argb_row_ptr = (PIXEL16U *)output_buffer;
	if (inverted) {
		argb_row_ptr += (output_height - 1) * output_pitch;
		output_pitch = NEG(output_pitch);
	}

	for (row = 0; row < output_height; row++)
	{
#if (0 && XMMOPT)
		int column_step = 16;
		int post_column = roi.width - (roi.width % column_step);
		__m64 *r_ptr = (__m64 *)r_row_ptr;
		__m64 *g_ptr = (__m64 *)g_row_ptr;
		__m64 *b_ptr = (__m64 *)b_row_ptr;
		__m64 *a_ptr = (__m64 *)a_row_ptr;
		__m64 *argb_ptr = (__m64 *)argb_row_ptr;
#endif
		// Start at the leftmost column
		column = 0;

		// Clear the output row (for debugging)
		//memset(argb_row_ptr, 0, output_row_size);

#if (0 && XMMOPT)

		// Should have exited the loop at the post processing column
		assert(column == post_column);
#endif
		// Process the rest of the row
		if(num_channels == 4)
		{
			for(; column < roi.width; column++)
			{
				PIXEL16U *argb_ptr = &argb_row_ptr[column*4];
				int r,g,b,a;

				// Load the tuple of RGB values
				r = r_row_ptr[column];
				g = g_row_ptr[column];
				b = b_row_ptr[column];
				a = a_row_ptr[column];

				// Scale the lowpass values to 16 bits
				r <<= shift;
				g <<= shift;
				b <<= shift;
				a <<= shift;

				// Remove the alpha encoding curve.
				//a -= 16<<8;
				//a <<= 8;
				//a += 111;
				//a /= 223;
				//12-bit SSE calibrated code
				//a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
				//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
				//a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));

				a >>= 4; //12-bit
				a -= alphacompandDCoffset;
				a <<= 3; //15-bit
				a *= alphacompandGain;
				a >>= 16; //12-bit
				a <<= 4; // 16-bit;


				if (saturate)
				{
					if (r < 0) r = 0; else if (r > rgb_max) r = rgb_max;
					if (g < 0) g = 0; else if (g > rgb_max) g = rgb_max;
					if (b < 0) b = 0; else if (b > rgb_max) b = rgb_max;
					if (a < 0) a = 0; else if (a > rgb_max) a = rgb_max;
				}

				*(argb_ptr++) = a;
				*(argb_ptr++) = r;
				*(argb_ptr++) = g;
				*(argb_ptr++) = b;
			}
		}
		else
		{
			for(; column < roi.width; column++)
			{
				PIXEL16U *argb_ptr = &argb_row_ptr[column*4];
				int r;
				int g;
				int b;

				// Load the tuple of RGB values
				r = r_row_ptr[column];
				g = g_row_ptr[column];
				b = b_row_ptr[column];

				//r >>= r_prescale;
				//g >>= g_prescale;
				//b >>= b_prescale;

				// Scale the lowpass values to 16 bits
				r <<= shift;
				g <<= shift;
				b <<= shift;

				if (saturate)
				{
					if (r < 0) r = 0; else if (r > rgb_max) r = rgb_max;
					if (g < 0) g = 0; else if (g > rgb_max) g = rgb_max;
					if (b < 0) b = 0; else if (b > rgb_max) b = rgb_max;
				}

				*(argb_ptr++) = alpha;
				*(argb_ptr++) = r;
				*(argb_ptr++) = g;
				*(argb_ptr++) = b;
			}
		}

		// Advance the row pointers into each lowpass band
		r_row_ptr += r_row_pitch;
		g_row_ptr += g_row_pitch;
		b_row_ptr += b_row_pitch;
		if (num_channels == 4)
			a_row_ptr += a_row_pitch;

		// Advance the row pointer into the output buffer
		argb_row_ptr += output_pitch;
	}
}

void ConvertLowpassRGB444ToRGB30(PIXEL *plane_array[], int pitch_array[],
								uint8_t *output_buffer, int output_pitch,
								ROI roi, bool inverted, int shift, int format)
{
	PIXEL *r_row_ptr;
	PIXEL *g_row_ptr;
	PIXEL *b_row_ptr;
	int r_row_pitch;
	int g_row_pitch;
	int b_row_pitch;
	//int r_prescale;
	//int g_prescale;
	//int b_prescale;
	unsigned int *rgb_row_ptr;

	int output_height = roi.height;

	//size_t output_row_size = output_pitch;

	int row;
	int column;

	const bool saturate = 1;
	const int rgb_max = USHRT_MAX;

	// Get pointers to the rows in the lowpass band of each channel
	r_row_ptr = plane_array[1];		r_row_pitch = pitch_array[1]/sizeof(PIXEL);
	g_row_ptr = plane_array[0];		g_row_pitch = pitch_array[0]/sizeof(PIXEL);
	b_row_ptr = plane_array[2];		b_row_pitch = pitch_array[2]/sizeof(PIXEL);

	// Convert the output pitch to units of pixels
	output_pitch /= sizeof(int);

	rgb_row_ptr = (unsigned int *)output_buffer;
	if (inverted) {
		rgb_row_ptr += (output_height - 1) * output_pitch;
		output_pitch = NEG(output_pitch);
	}

	for (row = 0; row < output_height; row++)
	{
		unsigned int *rgb_ptr = &rgb_row_ptr[0];
		// Start at the leftmost column
		column = 0;


		// Process the rest of the row
		for(; column < roi.width; column++)
		{
			int r;
			int g;
			int b;
			int rgb=0;

			// Load the tuple of RGB values
			r = r_row_ptr[column];
			g = g_row_ptr[column];
			b = b_row_ptr[column];

			// Scale the lowpass values to 16 bits
			r <<= shift;
			g <<= shift;
			b <<= shift;

			if (saturate)
			{
				if (r < 0) r = 0; else if (r > rgb_max) r = rgb_max;
				if (g < 0) g = 0; else if (g > rgb_max) g = rgb_max;
				if (b < 0) b = 0; else if (b > rgb_max) b = rgb_max;
			}

			r >>= 6; // 10-bit
			g >>= 6; // 10-bit
			b >>= 6; // 10-bit

			switch(format)
			{
				case DECODED_FORMAT_RG30:
				case DECODED_FORMAT_AB10:
					g <<= 10;
					b <<= 20;
					rgb |= r;
					rgb |= g;
					rgb |= b;

					*rgb_ptr++ = rgb;
					break;

				case DECODED_FORMAT_AR10:
					g <<= 10;
					r <<= 20;
					rgb |= r;
					rgb |= g;
					rgb |= b;

					*rgb_ptr++ = rgb;
					break;

				case DECODED_FORMAT_R210:
					//b <<= 0;
					g <<= 10;
					r <<= 20;
					rgb |= r;
					rgb |= g;
					rgb |= b;

					*rgb_ptr++ = _bswap(rgb);
					break;
				case DECODED_FORMAT_DPX0:
					r <<= 22;
					g <<= 12;
					b <<= 2;
					rgb |= r;
					rgb |= g;
					rgb |= b;

					*rgb_ptr++ = _bswap(rgb);
					break;
			}
		}

		// Advance the row pointers into each lowpass band
		r_row_ptr += r_row_pitch;
		g_row_ptr += g_row_pitch;
		b_row_ptr += b_row_pitch;

		// Advance the row pointer into the output buffer
		rgb_row_ptr += output_pitch;
	}
}


// Convert QuickTime format r408, v408 to a frame of planar YUV 4:2:2
void ConvertYUVAToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int format)
{
	//#pragma unused(buffer);

	const int num_channels = 3;

	uint8_t *yuva_row_ptr = data;
	int yuva_row_pitch = pitch;

	PIXEL *color_plane[3];
	int color_pitch[3];
	int frame_width;
	int frame_height;
	int display_height;
	int row;
	int i;

	uint8_t *y_row_ptr;
	uint8_t *u_row_ptr;
	uint8_t *v_row_ptr;

	int y_row_pitch;
	int u_row_pitch;
	int v_row_pitch;

	//const int max_yuv = 1023;			// Maximum pixel value at 10 bit precision

	// CCIR black and white for 10-bit pixels
	//const int yuv_black = (16 << 2);
	//const int yuv_white = (235 << 2);
	//const int yuv_scale = (yuv_white - yuv_black);

	// Neutral chroma value for 10-bit pixels
	//const int yuv_neutral = (128 << 2);

	assert(frame != NULL);
	if (! (frame != NULL)) return;

	// The frame format should be three channels of YUV (4:2:2 format)
	assert(frame->num_channels == num_channels);
	assert(frame->format == FRAME_FORMAT_YUV);

	display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < num_channels; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		color_plane[i] = image->band[0];
		color_pitch[i] = image->pitch;

		// The first channel establishes the processing dimensions
		if (i == 0) {
			frame_width = image->width;
			frame_height = image->height;
		}
	}


	// Swap the chroma values
	y_row_ptr = (uint8_t *)color_plane[0];		y_row_pitch = color_pitch[0];
	u_row_ptr = (uint8_t *)color_plane[2];		u_row_pitch = color_pitch[2];
	v_row_ptr = (uint8_t *)color_plane[1];		v_row_pitch = color_pitch[1];

	for (row = 0; row < display_height; row++)
	{
		// Start at the leftmost column
		int column = 0;

		// Pointer into the YUVA input row
		uint8_t *yuva_ptr = (uint8_t *)yuva_row_ptr;

		// Pointers into the YUV output rows
		PIXEL16U *y_ptr = (PIXEL16U *)y_row_ptr;
		PIXEL16U *u_ptr = (PIXEL16U *)u_row_ptr;
		PIXEL16U *v_ptr = (PIXEL16U *)v_row_ptr;

		// Process the rest of the column

		switch(format)
		{
		case COLOR_FORMAT_V408: //UYVA
			for (; column < frame_width; column += 2)
			{
				PIXEL16U y1,y2;
				PIXEL16U u;
				PIXEL16U v;

				// Load the first set of UYVA values
				u = *(yuva_ptr++)<<1; // bump to 10-bit (u1+u2) = 10-bit
				y1 = *(yuva_ptr++)<<2;
				v = *(yuva_ptr++)<<1;
				yuva_ptr++; // alpha

				// Load the second set of YUVA values (skip the alpha value)
				u += *(yuva_ptr++)<<1; // bump to 10-bit
				y2 = *(yuva_ptr++)<<2;
				v += *(yuva_ptr++)<<1;
				yuva_ptr++; // alpha

				// Output the first pixel
				*(y_ptr++) = y1;
				*(u_ptr++) = u;
				// Output the second pixel
				*(y_ptr++) = y2;
				*(v_ptr++) = v;
			}
			break;

		case COLOR_FORMAT_R408: //AYUV
			for (; column < frame_width; column += 2)
			{
				PIXEL16U y1,y2;
				PIXEL16U u;
				PIXEL16U v;

				// Load the first set of UYVA values
				yuva_ptr++; // alpha
				y1 = *(yuva_ptr++)<<2;
				u = *(yuva_ptr++)<<1; // bump to 10-bit (u1+u2) = 10-bit
				v = *(yuva_ptr++)<<1;

				// Load the second set of YUVA values (skip the alpha value)
				yuva_ptr++; // alpha
				y2 = *(yuva_ptr++)<<2;
				u += *(yuva_ptr++)<<1; // bump to 10-bit
				v += *(yuva_ptr++)<<1;

				// Output the first pixel
				*(y_ptr++) = y1+64;  // convert 0-219 range to 16-235 before encoding
				*(u_ptr++) = u;
				// Output the second pixel
				*(y_ptr++) = y2+64;
				*(v_ptr++) = v;
			}
			break;
		}

		// Advance the row pointers
		yuva_row_ptr += yuva_row_pitch;
		y_row_ptr += y_row_pitch;
		u_row_ptr += u_row_pitch;
		v_row_ptr += v_row_pitch;
	}
}



// Convert QuickTime format r4fl to a frame of planar YUV 4:2:2
void ConvertYUVAFloatToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer)
{
	//#pragma unused(buffer);

	const int num_channels = 3;

	uint8_t *yuva_row_ptr = data;
	int yuva_row_pitch = pitch;

	PIXEL *color_plane[3];
	int color_pitch[3];
	int frame_width;
	int frame_height;
	int display_height;
	int row;
	int i;

	uint8_t *y_row_ptr;
	uint8_t *u_row_ptr;
	uint8_t *v_row_ptr;

	int y_row_pitch;
	int u_row_pitch;
	int v_row_pitch;

	const int max_yuv = 1023;			// Maximum pixel value at 10 bit precision

	const float r4fl_white = 0.859f;		// CCIR white in the r4fl pixel format
	const float r4fl_neutral = 0.502f;	// Neutral chroma in the r4fl pixel format

#if 1
	// CCIR black and white for 10-bit pixels
	const int yuv_black = (16 << 2);
	const int yuv_white = (235 << 2);
	const int yuv_scale = (yuv_white - yuv_black);
#else
	// CCIR black and white for 10-bit pixels
	const int yuv_black = 0;
	const int yuv_white = (235 << 2);
	const int yuv_scale = (yuv_white - yuv_black);
#endif

	// Neutral chroma value for 10-bit pixels
	const int yuv_neutral = (128 << 2);

	assert(frame != NULL);
	if (! (frame != NULL)) return;

	// The frame format should be three channels of YUV (4:2:2 format)
	assert(frame->num_channels == num_channels);
	assert(frame->format == FRAME_FORMAT_YUV);

	display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < num_channels; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		color_plane[i] = image->band[0];
		color_pitch[i] = image->pitch;

		// The first channel establishes the processing dimensions
		if (i == 0) {
			frame_width = image->width;
			frame_height = image->height;
		}
	}

#if 0
	// Invert the input image
	rgb_row_ptr += (display_height - 1) * rgb_row_pitch;
	rgb_row_pitch = NEG(rgb_row_pitch);
#endif

	// Swap the chroma values
	y_row_ptr = (uint8_t *)color_plane[0];		y_row_pitch = color_pitch[0];
	u_row_ptr = (uint8_t *)color_plane[2];		u_row_pitch = color_pitch[2];
	v_row_ptr = (uint8_t *)color_plane[1];		v_row_pitch = color_pitch[1];

	for (row = 0; row < display_height; row++)
	{
		// Start at the leftmost column
		int column = 0;

		//TODO: Add optimized code
#if (1 && XMMOPT)

#endif

		// Pointer into the YUVA input row
		float *yuva_ptr = (float *)yuva_row_ptr;

		// Pointers into the YUV output rows
		PIXEL16U *y_ptr = (PIXEL16U *)y_row_ptr;
		PIXEL16U *u_ptr = (PIXEL16U *)u_row_ptr;
		PIXEL16U *v_ptr = (PIXEL16U *)v_row_ptr;

		// Process the rest of the column
		for (; column < frame_width; column += 2)
		{
			float y;
			float uA,uB;
			float vA,vB;

			int y1;
			int y2;
			int u1;
			int v1;

			// Load the first set of YUVA values (skip the alpha value)
			yuva_ptr++;
			y = *(yuva_ptr++);
			uA = *(yuva_ptr++);
			vA = *(yuva_ptr++);

			// Clamp to black (this removes superblack)
			if (y < 0.0) y = 0.0;

			// Convert floating-point to 10-bit integer
			y1 = (int)((y / r4fl_white) * yuv_scale + yuv_black);
			if (y1 < 0) y1 = 0; else if (y1 > max_yuv) y1 = max_yuv;

			// Load the second set of YUVA values (skip the alpha value)
			yuva_ptr++;
			y = *(yuva_ptr++);
			uB = *(yuva_ptr++);
			vB = *(yuva_ptr++);

			// Clamp to black (this removes superblack)
			if (y < 0.0) y = 0.0;

			// Convert floating-point to 10-bit integer
			// Clamp the luma and chroma values
			y2 = (int)((y / r4fl_white) * yuv_scale + yuv_black);
			if (y2 < 0) y2 = 0; else if (y2 > max_yuv) y2 = max_yuv;


			// Clamp the luma and chroma values to 10 bits
			u1 = (int)(((uA+uB) / r4fl_neutral) * yuv_neutral * 0.5f);
			if (u1 < 0) u1 = 0; else if (u1 > max_yuv) u1 = max_yuv;
			v1 = (int)(((vA+vB) / r4fl_neutral) * yuv_neutral * 0.5f);
			if (v1 < 0) v1 = 0; else if (v1 > max_yuv) v1 = max_yuv;

			// Output the first pixel
			*(y_ptr++) = y1;
			*(u_ptr++) = u1;

			// Output the second pixel
			*(y_ptr++) = y2;
			*(v_ptr++) = v1;
		}

		// Advance the row pointers
		yuva_row_ptr += yuva_row_pitch;
		y_row_ptr += y_row_pitch;
		u_row_ptr += u_row_pitch;
		v_row_ptr += v_row_pitch;
	}
}

// Convert QuickTime format r4fl to a frame of planar RGB 4:4:4
void ConvertYUVAFloatToFrame_RGB444_16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer)
{
	//#pragma unused(buffer);

	const int num_channels = 3;

	// Assume computer systems 709 color space for r4fl
	//DAN20080716 -- not sure if r4fl is always CG 709
	COLOR_SPACE color_space = (COLOR_SPACE)COLOR_SPACE_BT_709;

	uint8_t *yuva_row_ptr = data;
	int yuva_row_pitch = pitch;

	PIXEL *color_plane[3];
	int color_pitch[3];
	int frame_width;
	int frame_height;
	int display_height;
	int row;
	int i;

	uint8_t *r_row_ptr;
	uint8_t *g_row_ptr;
	uint8_t *b_row_ptr;

	int r_row_pitch;
	int g_row_pitch;
	int b_row_pitch;

	int luma_offset;

	float ymult;
	float r_vmult;
	float g_vmult;
	float g_umult;
	float b_umult;

	const int max_rgb = 4095;			// Maximum pixel value at 12 bit precision

	//const float r4fl_white = 0.859f;		// CCIR white in the r4fl pixel format
	const float r4fl_neutral = 0.502f;	// Neutral chroma in the r4fl pixel format

	assert(frame != NULL);
	if (! (frame != NULL)) return;

	// The frame format should be three channels of RGB (4:4:4 format)
	assert(frame->num_channels == num_channels);
	assert(frame->format == FRAME_FORMAT_RGB);

	display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < num_channels; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		color_plane[i] = image->band[0];
		color_pitch[i] = image->pitch;

		// The first channel establishes the processing dimensions
		if (i == 0) {
			frame_width = image->width;
			frame_height = image->height;
		}
	}

	// RGB planes are stored in the order G, R, B
	r_row_ptr = (uint8_t *)color_plane[1];
	g_row_ptr = (uint8_t *)color_plane[0];
	b_row_ptr = (uint8_t *)color_plane[2];

	r_row_pitch = color_pitch[1];
	g_row_pitch = color_pitch[0];
	b_row_pitch = color_pitch[2];

	// Initialize the color conversion constants (floating-point version)
	switch(color_space & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:	// Computer systems 601
		luma_offset = 16;
		ymult =   1.164f;
		r_vmult = 1.596f;
		g_vmult = 0.813f;
		g_umult = 0.391f;
		b_umult = 2.018f;
		break;

	case COLOR_SPACE_VS_601:	// Video systems 601
		luma_offset = 0;
		ymult =     1.0f;
		r_vmult = 1.371f;
		g_vmult = 0.698f;
		g_umult = 0.336f;
		b_umult = 1.732f;
		break;

	default: assert(0);
	case COLOR_SPACE_CG_709:	// Computer systems 709
		luma_offset = 16;
		ymult =   1.164f;
		r_vmult = 1.793f;
		g_vmult = 0.534f;
		g_umult = 0.213f;
		b_umult = 2.115f;
		break;

	case COLOR_SPACE_VS_709:	// Video Systems 709
		luma_offset = 0;
		ymult =     1.0f;
		r_vmult = 1.540f;
		g_vmult = 0.459f;
		g_umult = 0.183f;
		b_umult = 1.816f;
		break;
	}

	for (row = 0; row < display_height; row++)
	{
		// Start at the leftmost column
		int column = 0;

#if (1 && XMMOPT)
		//TODO: Add optimized code
#endif
		// Pointer into the YUVA input row
		float *yuva_ptr = (float *)yuva_row_ptr;

		// Pointers into the RGB output rows
		PIXEL16U *r_ptr = (PIXEL16U *)r_row_ptr;
		PIXEL16U *g_ptr = (PIXEL16U *)g_row_ptr;
		PIXEL16U *b_ptr = (PIXEL16U *)b_row_ptr;

		// Process the rest of the column
		for (; column < frame_width; column++)
		{
			// Get the next pixel
			//float a1 = *(yuva_ptr++);
			float y1 = *(yuva_ptr++);
			float v1 = *(yuva_ptr++);	// Cb
			float u1 = *(yuva_ptr++);	// Cr

			float r1, g1, b1, t1, t2;

			int r1_out, g1_out, b1_out;

			// Subtract the chroma offsets
			u1 -= r4fl_neutral;
			v1 -= r4fl_neutral;

			r1 = ymult * y1;
			t1 = r_vmult * u1;
			r1 += t1;

			g1 = ymult * y1;
			t1 = g_vmult * u1;
			g1 -= t1;
			t2 = g_umult * v1;
			g1 -= t2;

			b1 = ymult * y1;
			t1 = b_umult * v1;
			b1 += t1;

			// Convert to integer values
			r1_out = (int)(r1 * (float)max_rgb);
			g1_out = (int)(g1 * (float)max_rgb);
			b1_out = (int)(b1 * (float)max_rgb);

			// Force the RGB values into valid range
			if (r1_out < 0) r1_out = 0;
			if (g1_out < 0) g1_out = 0;
			if (b1_out < 0) b1_out = 0;

			if (r1_out > max_rgb) r1_out = max_rgb;
			if (g1_out > max_rgb) g1_out = max_rgb;
			if (b1_out > max_rgb) b1_out = max_rgb;

			*(r_ptr++) = r1_out;
			*(g_ptr++) = g1_out;
			*(b_ptr++) = b1_out;
		}

		// Advance the row pointers
		yuva_row_ptr += yuva_row_pitch;
		r_row_ptr += r_row_pitch;
		g_row_ptr += g_row_pitch;
		b_row_ptr += b_row_pitch;
	}
}

// Convert QuickTime format r4fl to a frame of planar RGBA 4:4:4:4
void ConvertYUVAFloatToFrame_RGBA4444_16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer)
{
	const int num_channels = FRAME_MAX_CHANNELS;
	
	// Assume computer systems 709 color space for r4fl
	//DAN20080716 -- not sure if r4fl is always CG 709
	COLOR_SPACE color_space = (COLOR_SPACE)COLOR_SPACE_BT_709;
	
	uint8_t *yuva_row_ptr = data;
	int yuva_row_pitch = pitch;
	
	PIXEL *color_plane[FRAME_MAX_CHANNELS];
	int color_pitch[FRAME_MAX_CHANNELS];
	int frame_width;
	int frame_height;
	int display_height;
	int row;
	int i;
	
	uint8_t *r_row_ptr;
	uint8_t *g_row_ptr;
	uint8_t *b_row_ptr;
	uint8_t *a_row_ptr = NULL;
	
	int r_row_pitch;
	int g_row_pitch;
	int b_row_pitch;
	int a_row_pitch = 0;
	
	int luma_offset;
	
	float ymult;
	float r_vmult;
	float g_vmult;
	float g_umult;
	float b_umult;
	
	const int max_rgb = 4095;			// Maximum pixel value at 12 bit precision
	
	//const float r4fl_white = 0.859f;		// CCIR white in the r4fl pixel format
	const float r4fl_neutral = 0.502f;	// Neutral chroma in the r4fl pixel format
	
	assert(frame != NULL);
	if (! (frame != NULL)) return;
	
	// The frame format should be four channels of RGBA (4:4:4:4 format)
	assert(frame->num_channels == num_channels);
	assert(frame->format == FRAME_FORMAT_RGBA);
	
	display_height = frame->display_height;
	
	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < num_channels; i++)
	{
		IMAGE *image = frame->channel[i];
				
		assert(frame->channel[i] != NULL);
		
		// Set the pointer to the individual planes and pitch for each channel
		color_plane[i] = image->band[0];
		color_pitch[i] = image->pitch;
		
		// The first channel establishes the processing dimensions
		if (i == 0) {
			frame_width = image->width;
			frame_height = image->height;
		}
	}
	
	// RGB planes are stored in the order G, R, B
	r_row_ptr = (uint8_t *)color_plane[1];
	g_row_ptr = (uint8_t *)color_plane[0];
	b_row_ptr = (uint8_t *)color_plane[2];
	a_row_ptr = (uint8_t *)color_plane[3];
	
	r_row_pitch = color_pitch[1];
	g_row_pitch = color_pitch[0];
	b_row_pitch = color_pitch[2];
	a_row_pitch = color_pitch[3];
		
	// Initialize the color conversion constants (floating-point version)
	switch(color_space & COLORSPACE_MASK)
	{
		case COLOR_SPACE_CG_601:	// Computer systems 601
			luma_offset = 16;
			ymult =   1.164f;
			r_vmult = 1.596f;
			g_vmult = 0.813f;
			g_umult = 0.391f;
			b_umult = 2.018f;
			break;
			
		case COLOR_SPACE_VS_601:	// Video systems 601
			luma_offset = 0;
			ymult =     1.0f;
			r_vmult = 1.371f;
			g_vmult = 0.698f;
			g_umult = 0.336f;
			b_umult = 1.732f;
			break;
			
		default: assert(0);
		case COLOR_SPACE_CG_709:	// Computer systems 709
			luma_offset = 16;
			ymult =   1.164f;
			r_vmult = 1.793f;
			g_vmult = 0.534f;
			g_umult = 0.213f;
			b_umult = 2.115f;
			break;
			
		case COLOR_SPACE_VS_709:	// Video Systems 709
			luma_offset = 0;
			ymult =     1.0f;
			r_vmult = 1.540f;
			g_vmult = 0.459f;
			g_umult = 0.183f;
			b_umult = 1.816f;
			break;
	}
	
	for (row = 0; row < display_height; row++)
	{
		// Start at the leftmost column
		int column = 0;
		
#if (1 && XMMOPT)
		//TODO: Add optimized code
#endif
		// Pointer into the YUVA input row
		float *yuva_ptr = (float *)yuva_row_ptr;
		
		// Pointers into the RGB output rows
		PIXEL16U *r_ptr = (PIXEL16U *)r_row_ptr;
		PIXEL16U *g_ptr = (PIXEL16U *)g_row_ptr;
		PIXEL16U *b_ptr = (PIXEL16U *)b_row_ptr;
		PIXEL16U *a_ptr = (PIXEL16U *)a_row_ptr;
		
		// Process the rest of the column
		for (; column < frame_width; column++)
		{
			// Get the next pixel
			float a1 = *(yuva_ptr++);
			float y1 = *(yuva_ptr++);
			float v1 = *(yuva_ptr++);	// Cb
			float u1 = *(yuva_ptr++);	// Cr
			
			float r1, g1, b1, t1, t2;
			
			int r1_out, g1_out, b1_out, a1_out;
			
			// Subtract the chroma offsets
			u1 -= r4fl_neutral;
			v1 -= r4fl_neutral;
			
			r1 = ymult * y1;
			t1 = r_vmult * u1;
			r1 += t1;
			
			g1 = ymult * y1;
			t1 = g_vmult * u1;
			g1 -= t1;
			t2 = g_umult * v1;
			g1 -= t2;
			
			b1 = ymult * y1;
			t1 = b_umult * v1;
			b1 += t1;
			
			// Convert to integer values
			r1_out = (int)(r1 * (float)max_rgb);
			g1_out = (int)(g1 * (float)max_rgb);
			b1_out = (int)(b1 * (float)max_rgb);
			a1_out = (int)(a1 * (float)max_rgb);
			
			// Force the RGB values into valid range
			if (r1_out < 0) r1_out = 0;
			if (g1_out < 0) g1_out = 0;
			if (b1_out < 0) b1_out = 0;
			if (a1_out < 0) a1_out = 0;
			
			if (r1_out > max_rgb) r1_out = max_rgb;
			if (g1_out > max_rgb) g1_out = max_rgb;
			if (b1_out > max_rgb) b1_out = max_rgb;
			if (a1_out > max_rgb) b1_out = max_rgb;
			
			*(r_ptr++) = r1_out;
			*(g_ptr++) = g1_out;
			*(b_ptr++) = b1_out;
			*(a_ptr++) = a1_out;
		}
		
		// Advance the row pointers
		yuva_row_ptr += yuva_row_pitch;
		r_row_ptr += r_row_pitch;
		g_row_ptr += g_row_pitch;
		b_row_ptr += b_row_pitch;
		a_row_ptr += a_row_pitch;
	}
}


// Convert the input frame from YUV422 to RGB
void ConvertLowpass16sToRGBNoIPPFast(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch, int format, int colorspace, bool inverted, int descale)
{
	PIXEL *plane[3];
	int pitch[3];
	ROI roi;
	int channel;

	//CG_601
	int y_offset = 16; // not VIDEO_RGB & not YUV709
	int ymult = 128*149;	//7bit 1.164
	int r_vmult = 204;		//7bit 1.596
	int g_vmult = 208;		//8bit 0.813
	int g_umult = 100;		//8bit 0.391
	int b_umult = 129;		//6bit 2.018
	int saturate = 1;

	//if(colorspace & COLOR_SPACE_422_TO_444)
	//{
	//	upconvert422to444 = 1;
	//}

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



#if (_USE_YCBCR == 0) || (_ENABLE_GAMMA_CORRECTION == 1)
	// Check that the correct compiler time switches are set correctly
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


	// Only 24 and 32 bit true color RGB formats are supported
	assert(format == COLOR_FORMAT_RGB24 || format == COLOR_FORMAT_RGB32);

	// Convert from pixel to byte data
	for (channel = 0; channel < 3; channel++)
	{
		IMAGE *image = images[channel];

		plane[channel] = (PIXEL *)(image->band[0]);
		pitch[channel] = image->pitch/sizeof(PIXEL);

		if (channel == 0) {
			roi.width = image->width;
			roi.height = image->height;
		}
	}

	// Output to RGB24 format?
	if (format == COLOR_FORMAT_RGB24)
	{
		PIXEL *Y_row, *U_row, *V_row;
		int Y_pitch, U_pitch, V_pitch;
		int Y_prescale, U_prescale, V_prescale;
		uint8_t *RGB_row;
		int row, column;

		Y_row = plane[0]; Y_pitch = pitch[0]; Y_prescale = descale + PRESCALE_LUMA;
		U_row = plane[1]; U_pitch = pitch[1]; U_prescale = descale + PRESCALE_CHROMA;
		V_row = plane[2]; V_pitch = pitch[2]; V_prescale = descale + PRESCALE_CHROMA;

		RGB_row = &output_buffer[0];
		if (inverted) {
			RGB_row += (output_height - 1) * output_pitch;
			output_pitch = -output_pitch;
		}

		for (row = 0; row < output_height; row++)
		{
			//int column_step = 16;
			//int post_column = roi.width - (roi.width % column_step);
			//uint8_t *RGB_ptr = RGB_row;
			//int *RGB_int_ptr = (int *)RGB_ptr;

#if MMXSUPPORTED //TODO DANREMOVE
			__m64 *Y_ptr = (__m64 *)Y_row;
			__m64 *U_ptr = (__m64 *)U_row;
			__m64 *V_ptr = (__m64 *)V_row;
			__m64 *output_ptr = (__m64 *)RGB_ptr;
#endif

			column = 0;
#if MMXSUPPORTED //TODO DANREMOVE
			// Convert the YUV422 frame back into RGB in sets of 2 pixels
			for (; column < post_column; column += column_step)
			{
				__m64 R1, G1, B1;
				__m64 R2, G2, B2;
				__m64 R_pi8, G_pi8, B_pi8;
				__m64 Y, U, V;
				__m64 Y_pi8, U_pi8, V_pi8;
				__m64 temp, temp2;
				__m64 RGB;
				__m64 RG;
				__m64 BZ;
				__m64 RGBZ;

				/***** Load the first eight YCbCr values *****/

				// Convert 16-bit signed lowpass pixels into 8-bit unsigned frame pixels
				temp = *Y_ptr++; temp2 = *Y_ptr++;
				temp = _mm_srai_pi16(temp, Y_prescale);
				temp2 = _mm_srai_pi16(temp2, Y_prescale);
				Y_pi8 = _mm_packs_pu16(temp, temp2);

				temp = *U_ptr++; temp2 = *U_ptr++;
				temp = _mm_srai_pi16(temp, V_prescale);
				temp2 = _mm_srai_pi16(temp2, V_prescale);
				V_pi8 = _mm_packs_pu16(temp, temp2);

				temp = *V_ptr++; temp2 = *V_ptr++;
				temp = _mm_srai_pi16(temp, U_prescale);
				temp2 = _mm_srai_pi16(temp2, U_prescale);
				U_pi8 = _mm_packs_pu16(temp, temp2);

#if STRICT_SATURATE		// Perform strict saturation on YUV if required
				if (saturate)
				{
					Y_pi8 = _mm_subs_pu8(Y_pi8, _mm_set1_pi8(16));
					Y_pi8 = _mm_adds_pu8(Y_pi8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
					Y_pi8 = _mm_subs_pu8(Y_pi8, _mm_set1_pi8(20));

					U_pi8 = _mm_subs_pu8(U_pi8, _mm_set1_pi8(16));
					U_pi8 = _mm_adds_pu8(U_pi8, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
					U_pi8 = _mm_subs_pu8(U_pi8, _mm_set1_pi8(15));

					V_pi8 = _mm_subs_pu8(V_pi8, _mm_set1_pi8(16));
					V_pi8 = _mm_adds_pu8(V_pi8, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
					V_pi8 = _mm_subs_pu8(V_pi8, _mm_set1_pi8(15));
				}
#endif

				/***** Calculate the first four RGB values *****/
				// Unpack the first four Y value
				Y = _mm_unpacklo_pi8(Y_pi8, _mm_setzero_si64());

				// Set the first four CbCr values
				U = _mm_unpacklo_pi8(U_pi8, _mm_setzero_si64());
				V = _mm_unpacklo_pi8(V_pi8, _mm_setzero_si64());
				U = _mm_shuffle_pi16(U, _MM_SHUFFLE(1, 1, 0, 0));
				V = _mm_shuffle_pi16(V, _MM_SHUFFLE(1, 1, 0, 0));

				// Convert YCbCr to RGB
	//			Y = _mm_slli_pi16(Y, 1);
	//			U = _mm_slli_pi16(U, 1);
	//			V = _mm_slli_pi16(V, 1);

				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7);			// This code fix an overflow case where very bright
				temp = _mm_set1_pi16(ymult);		//pixel with some color produced interim values over 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
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
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				B1 = _mm_adds_pi16(Y, temp);
				B1 = _mm_srai_pi16(B1, 6);


				/***** Calculate the second four RGB values *****/
				// Unpack the second four Y value
				Y = _mm_unpackhi_pi8(Y_pi8, _mm_setzero_si64());

				// Set the second four CbCr values
				U = _mm_unpacklo_pi8(U_pi8, _mm_setzero_si64());
				V = _mm_unpacklo_pi8(V_pi8, _mm_setzero_si64());
				U = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 2));
				V = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 2));

				// Convert YCbCr to RGB
//				Y = _mm_slli_pi16(Y, 1);
//				U = _mm_slli_pi16(U, 1);
//				V = _mm_slli_pi16(V, 1);

				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7);			// This code fix an overflow case where very bright
				temp = _mm_set1_pi16(ymult);		//pixel with some color produced interim values over 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
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
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				B2 = _mm_adds_pi16(Y, temp);
				B2 = _mm_srai_pi16(B2, 6);


				// Prepare to store the eight RGB values
				B_pi8 = _mm_packs_pu16(R1, R2);
				G_pi8 = _mm_packs_pu16(G1, G2);
				R_pi8 = _mm_packs_pu16(B1, B2);




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
				RGBZ = _mm_slli_si64(RGBZ, 3 * 8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have two RGB tuples with two zero bytes in the rest of the word

				// Interleave the second two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Insert the first two red and green values into the output word
				RGB = _mm_or_si64(RGB, _mm_slli_si64(RGBZ, 6 * 8));

				// Store the first group of eight bytes of RGB values
				*(output_ptr++) = RGB;

				// Shift the blue value into position
				RGB = _mm_slli_si64(RGBZ, 5 * 8);
				RGB = _mm_srli_si64(RGB, 7 * 8);

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
				temp = _mm_srli_si64(RGBZ, 4 * 8);
				temp = _mm_slli_si64(temp, 7 * 8);

				// Insert the red value into the upper byte of the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the second group of eight bytes of RGB values
				*(output_ptr++) = RGB;

				// Shift the green and blue values into position
				RGB = _mm_srli_si64(RGBZ, 5 * 8);

				// Interleave the fourth two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Shift the next RGB tuple into position
				temp = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 2 * 8);

				// Insert the RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Shift the last RGB tuple into position
				temp = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 5 * 8);

				// Insert the last RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the third group of eight bytes of RGB values
				*(output_ptr++) = RGB;


				/***** Load the second eight Y values *****/

				// Convert 16-bit signed lowpass pixels into 8-bit unsigned frame pixels
				temp = *Y_ptr++; temp2 = *Y_ptr++;
				temp = _mm_srai_pi16(temp, Y_prescale);
				temp2 = _mm_srai_pi16(temp2, Y_prescale);
				Y_pi8 = _mm_packs_pu16(temp, temp2);

#if STRICT_SATURATE
				if (saturate)
				{
					// Perform strict saturation on YUV if required
					Y_pi8 = _mm_subs_pu8(Y_pi8, _mm_set1_pi8(16));
					Y_pi8 = _mm_adds_pu8(Y_pi8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
					Y_pi8 = _mm_subs_pu8(Y_pi8, _mm_set1_pi8(20));
				}
#endif

				/***** Calculate the third four RGB values *****/

				// Unpack the first four Y value
				Y = _mm_unpacklo_pi8(Y_pi8, _mm_setzero_si64());

				// Set the first four CbCr values
				U = _mm_unpackhi_pi8(U_pi8, _mm_setzero_si64());
				V = _mm_unpackhi_pi8(V_pi8, _mm_setzero_si64());
				U = _mm_shuffle_pi16(U, _MM_SHUFFLE(1, 1, 0, 0));
				V = _mm_shuffle_pi16(V, _MM_SHUFFLE(1, 1, 0, 0));

				// Convert YCbCr to RGB
//				Y = _mm_slli_pi16(Y, 1);
//				U = _mm_slli_pi16(U, 1);
//				V = _mm_slli_pi16(V, 1);

				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7);			// This code fix an overflow case where very bright
				temp = _mm_set1_pi16(ymult);		//pixel with some color produced interim values over 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
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
				G1 = _mm_subs_pi16(G1, temp);
				G1 = _mm_srai_pi16(G1, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				B1 = _mm_adds_pi16(Y, temp);
				B1 = _mm_srai_pi16(B1, 6);


				/***** Calculate the fourth four RGB values *****/

				// Unpack the second four Y value
				Y = _mm_unpackhi_pi8(Y_pi8, _mm_setzero_si64());

				// Set the second four CbCr values
				U = _mm_unpackhi_pi8(U_pi8, _mm_setzero_si64());
				V = _mm_unpackhi_pi8(V_pi8, _mm_setzero_si64());
				U = _mm_shuffle_pi16(U, _MM_SHUFFLE(3, 3, 2, 2));
				V = _mm_shuffle_pi16(V, _MM_SHUFFLE(3, 3, 2, 2));

				// Convert YCbCr to RGB
//				Y = _mm_slli_pi16(Y, 1);
//				U = _mm_slli_pi16(U, 1);
//				V = _mm_slli_pi16(V, 1);

				temp = _mm_set1_pi16(y_offset);
				Y = _mm_subs_pi16(Y, temp);
				temp = _mm_set1_pi16(128);
				U = _mm_subs_pi16(U, temp);
				V = _mm_subs_pi16(V, temp);

				Y = _mm_slli_pi16(Y, 7);			// This code fix an overflow case where very bright
				temp = _mm_set1_pi16(ymult);		//pixel with some color produced interim values over 32768
				Y = _mm_mulhi_pi16(Y, temp);
				Y = _mm_slli_pi16(Y, 1);

				// Calculate R
				temp = _mm_set1_pi16(r_vmult);
				temp = _mm_mullo_pi16(V, temp);
				temp = _mm_srai_pi16(temp, 1); //7bit to 6
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
				G2 = _mm_subs_pi16(G2, temp);
				G2 = _mm_srai_pi16(G2, 6);

				// Calculate B
				temp = _mm_set1_pi16(b_umult);
				temp = _mm_mullo_pi16(U, temp);
				B2 = _mm_adds_pi16(Y, temp);
				B2 = _mm_srai_pi16(B2, 6);



				// Prepare to store the eight RGB values
				B_pi8 = _mm_packs_pu16(R1, R2);
				G_pi8 = _mm_packs_pu16(G1, G2);
				R_pi8 = _mm_packs_pu16(B1, B2);

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
				RGBZ = _mm_slli_si64(RGBZ, 3 * 8);

				// Insert the second RGB tuple into the output word
				RGB = _mm_or_si64(RGB, RGBZ);

				// Now have two RGB tuples with two zero bytes in the rest of the word

				// Interleave the second two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Insert the first two red and green values into the output word
				RGB = _mm_or_si64(RGB, _mm_slli_si64(RGBZ, 6 * 8));

				// Store the first group of eight bytes of RGB values
				*(output_ptr++) = RGB;

				// Shift the blue value into position
				RGB = _mm_slli_si64(RGBZ, 5 * 8);
				RGB = _mm_srli_si64(RGB, 7 * 8);

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
				temp = _mm_srli_si64(RGBZ, 4 * 8);
				temp = _mm_slli_si64(temp, 7 * 8);

				// Insert the red value into the upper byte of the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the second group of eight bytes of RGB values
				*(output_ptr++) = RGB;

				// Shift the green and blue values into position
				RGB = _mm_srli_si64(RGBZ, 5 * 8);

				// Interleave the fourth two RGBZ tuples
				RGBZ = _mm_unpackhi_pi16(RG, BZ);

				// Shift the next RGB tuple into position
				temp = _mm_unpacklo_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 2 * 8);

				// Insert the RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Shift the last RGB tuple into position
				temp = _mm_unpackhi_pi32(RGBZ, _mm_setzero_si64());
				temp = _mm_slli_si64(temp, 5 * 8);

				// Insert the last RGB tuple into the output word
				RGB = _mm_or_si64(RGB, temp);

				// Store the third group of eight bytes of RGB values
				*(output_ptr++) = RGB;

			}

			// Clear the MMX registers
			//_mm_empty();

			// Check that the loop ends at the right position
			assert(column == post_column);
#endif
			// Process the rest of the row with 7-bit fixed point arithmetic
			for(; column < roi.width; column += 2) {
				int R, G, B;
				int Y, U, V;
				uint8_t *RGB_ptr = &RGB_row[column*3];

				// Convert the first set of YCbCr values
				if(saturate)
				{
					Y = SATURATE_Y(Y_row[column]    >> Y_prescale);
					V = SATURATE_Cr(U_row[column/2] >> V_prescale);
					U = SATURATE_Cb(V_row[column/2] >> U_prescale);
				}
				else
				{
					Y = (Y_row[column]   >> Y_prescale);
					V = (U_row[column/2] >> V_prescale);
					U = (V_row[column/2] >> U_prescale);
				}

				Y = Y - y_offset;
				U = U - 128;
				V = V - 128;

				Y = Y * ymult >> 7;

				R = (Y           + r_vmult * V) >> 7;
				G = (Y*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y + 2 * b_umult * U) >> 7;

				RGB_ptr[0] = SATURATE_8U(B);
				RGB_ptr[1] = SATURATE_8U(G);
				RGB_ptr[2] = SATURATE_8U(R);

				// Convert the second set of YCbCr values
				if(saturate)
					Y = SATURATE_Y(Y_row[column+1] >> Y_prescale);
				else
					Y = (Y_row[column+1] >> Y_prescale);

				Y = Y - y_offset;
				Y = Y * ymult >> 7;

				R = (Y           + r_vmult * V) >> 7;
				G = (Y*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y + 2 * b_umult * U) >> 7;

				RGB_ptr[3] = SATURATE_8U(B);
				RGB_ptr[4] = SATURATE_8U(G);
				RGB_ptr[5] = SATURATE_8U(R);
			}

			// Fill the rest of the output row with black
			for (; column < output_width; column++)
			{
				uint8_t *RGB_ptr = &RGB_row[column*3];

				RGB_ptr[0] = 0;
				RGB_ptr[1] = 0;
				RGB_ptr[2] = 0;
			}

			// Advance the YUV pointers
			Y_row += Y_pitch;
			U_row += U_pitch;
			V_row += V_pitch;

			// Advance the RGB pointers
			RGB_row += output_pitch;
		}
	}

	else	// Output format is RGB32 so set the alpha channel to the default
	{
		PIXEL *Y_row, *U_row, *V_row;
		int Y_pitch, U_pitch, V_pitch;
		int Y_prescale, U_prescale, V_prescale;
		uint8_t *RGBA_row;
		int row, column;
		//int column_step = 2;

		Y_row = plane[0]; Y_pitch = pitch[0]; Y_prescale = descale + PRESCALE_LUMA;
		U_row = plane[1]; U_pitch = pitch[1]; U_prescale = descale + PRESCALE_CHROMA;
		V_row = plane[2]; V_pitch = pitch[2]; V_prescale = descale + PRESCALE_CHROMA;

		RGBA_row = &output_buffer[0];
		if(inverted) {
			RGBA_row += (output_height - 1) * output_pitch;
			output_pitch = -output_pitch;
		}

		for (row = 0; row < output_height; row++)
		{
			int column_step = 16;
			int post_column = roi.width - (roi.width % column_step);
			__m128i *Y_ptr = (__m128i *)Y_row;
			__m128i *U_ptr = (__m128i *)U_row;
			__m128i *V_ptr = (__m128i *)V_row;
			__m128i *RGBA_ptr = (__m128i *)RGBA_row;

			column = 0;
#if 1
			// Convert the YUV422 frame back into RGB in sets of 2 pixels
			for(; column < post_column; column += column_step)
			{
				__m128i R1, G1, B1;
				__m128i R2, G2, B2;
				__m128i R_pi8, G_pi8, B_pi8;
				__m128i Y, U, V;
				__m128i Y_pi8, U_pi8, V_pi8;
				__m128i temp, temp2;
				__m128i RGBA;

				/***** Load sixteen YCbCr values and eight each U, V value *****/

				// Convert 16-bit signed lowpass pixels into 8-bit unsigned pixels,
                // packing into three 128-bit SSE vectors (one per channel).
                // Otto: Yes, I see that the U comes from the V pointer, and the V
                // comes from the U pointer. I don't know why.
				temp  = *Y_ptr++;
                temp2 = *Y_ptr++;
				temp  = _mm_srai_epi16(temp,  Y_prescale);
				temp2 = _mm_srai_epi16(temp2, Y_prescale);
				Y_pi8 = _mm_packus_epi16(temp, temp2);

				temp  = *U_ptr++;
				temp  = _mm_srai_epi16(temp, V_prescale);
				V_pi8 = _mm_packus_epi16(temp, _mm_setzero_si128());

				temp  = *V_ptr++;
				temp  = _mm_srai_epi16(temp, U_prescale);
				U_pi8 = _mm_packus_epi16(temp, _mm_setzero_si128());

#if STRICT_SATURATE
				// Perform strict saturation on YUV if required
				if(saturate)
				{
					Y_pi8 = _mm_subs_epu8(Y_pi8, _mm_set1_pi8(16));
					Y_pi8 = _mm_adds_epu8(Y_pi8, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
					Y_pi8 = _mm_subs_epu8(Y_pi8, _mm_set1_pi8(20));

					U_pi8 = _mm_subs_epu8(U_pi8, _mm_set1_pi8(16));
					U_pi8 = _mm_adds_epu8(U_pi8, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
					U_pi8 = _mm_subs_epu8(U_pi8, _mm_set1_pi8(15));

					V_pi8 = _mm_subs_epu8(V_pi8, _mm_set1_pi8(16));
					V_pi8 = _mm_adds_epu8(V_pi8, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
					V_pi8 = _mm_subs_epu8(V_pi8, _mm_set1_pi8(15));
				}
#endif

				/***** Calculate the first eight RGB values *****/
                
				// Unpack the first eight Y values
				Y = _mm_unpacklo_epi8(Y_pi8, _mm_setzero_si128());

				// Set the first eight U,V values (duplicating the first four)
				U = _mm_unpacklo_epi8(U_pi8, _mm_setzero_si128());
				V = _mm_unpacklo_epi8(V_pi8, _mm_setzero_si128());
                {
                    m128i lo, hi;
                    m128i mask;
                    mask.u64[0] = ~0ULL;
                    mask.u64[1] = 0;
                    lo.m128 = _mm_shufflelo_epi16(U, _MM_SHUFFLE(1, 1, 0, 0));
                    lo.m128 = _mm_and_si128(lo.m128, mask.m128);
                    hi.m128 = _mm_shufflelo_epi16(U, _MM_SHUFFLE(3, 3, 2, 2));
                    hi.m128 = _mm_slli_si128(hi.m128, 8);
                    U = _mm_or_si128(lo.m128, hi.m128);
                    
                    lo.m128 = _mm_shufflelo_epi16(V, _MM_SHUFFLE(1, 1, 0, 0));
                    lo.m128 = _mm_and_si128(lo.m128, mask.m128);
                    hi.m128 = _mm_shufflelo_epi16(V, _MM_SHUFFLE(3, 3, 2, 2));
                    hi.m128 = _mm_slli_si128(hi.m128, 8);
                    V = _mm_or_si128(lo.m128, hi.m128);
                }


				// Convert YUV to RGB
                
//				Y = _mm_slli_pi16(Y, 1);
//				U = _mm_slli_pi16(U, 1);
//				V = _mm_slli_pi16(V, 1);

				temp = _mm_set1_epi16( y_offset);
				Y = _mm_subs_epi16(Y, temp);
				temp = _mm_set1_epi16(128);
				U = _mm_subs_epi16(U, temp);
				V = _mm_subs_epi16(V, temp);

				Y = _mm_slli_epi16(Y, 7);			// This code fix an overflow case where very bright
				temp = _mm_set1_epi16(ymult);		//pixel with some color produced interim values over 32768
				Y = _mm_mulhi_epi16(Y, temp);
				Y = _mm_slli_epi16(Y, 1);

				// Calculate R
				temp = _mm_set1_epi16(r_vmult);
				temp = _mm_mullo_epi16(V, temp);
				temp = _mm_srai_epi16(temp, 1); //7bit to 6
				R1 = _mm_adds_epi16(Y, temp);
				R1 = _mm_srai_epi16(R1, 6);

				// Calculate G
				temp = _mm_set1_epi16(g_vmult);
				temp = _mm_mullo_epi16(V, temp);
				temp = _mm_srai_epi16(temp, 2); //8bit to 6
				G1 = _mm_subs_epi16(Y, temp);
				temp = _mm_set1_epi16(g_umult);
				temp = _mm_mullo_epi16(U, temp);
				temp = _mm_srai_epi16(temp, 2); //8bit to 6
				G1 = _mm_subs_epi16(G1, temp);
				G1 = _mm_srai_epi16(G1, 6);

				// Calculate B
				temp = _mm_set1_epi16(b_umult);
				temp = _mm_mullo_epi16(U, temp);
				B1 = _mm_adds_epi16(Y, temp);
				B1 = _mm_srai_epi16(B1, 6);


				/***** Calculate the second eight RGB values *****/
				
                // Unpack the second eight Y values
				Y = _mm_unpackhi_epi8(Y_pi8, _mm_setzero_si128());

				// Unpack the second eight U,V values (duplicating the
                // second four)
				U = _mm_unpacklo_epi8(U_pi8, _mm_setzero_si128());
				V = _mm_unpacklo_epi8(V_pi8, _mm_setzero_si128());
                {
                    m128i lo, hi;
                    m128i mask;
                    mask.u64[0] = 0;
                    mask.u64[1] = ~0ULL;
                    
                    lo.m128 = _mm_shufflehi_epi16(U, _MM_SHUFFLE(1, 1, 0, 0));
                    lo.m128 = _mm_srli_si128(lo.m128, 8);
                    hi.m128 = _mm_shufflehi_epi16(U, _MM_SHUFFLE(3, 3, 2, 2));
                    hi.m128 = _mm_and_si128(hi.m128, mask.m128);
                    U = _mm_or_si128(lo.m128, hi.m128);
                    
                    lo.m128 = _mm_shufflehi_epi16(V, _MM_SHUFFLE(1, 1, 0, 0));
                    lo.m128 = _mm_srli_si128(lo.m128, 8);
                    hi.m128 = _mm_shufflehi_epi16(V, _MM_SHUFFLE(3, 3, 2, 2));
                    hi.m128 = _mm_and_si128(hi.m128, mask.m128);
                    V = _mm_or_si128(lo.m128, hi.m128);
                }

				// Convert YUV to RGB
                
//				Y = _mm_slli_pi16(Y, 1);
//				U = _mm_slli_pi16(U, 1);
//				V = _mm_slli_pi16(V, 1);

				temp = _mm_set1_epi16( y_offset);
				Y = _mm_subs_epi16(Y, temp);
				temp = _mm_set1_epi16(128);
				U = _mm_subs_epi16(U, temp);
				V = _mm_subs_epi16(V, temp);

				Y = _mm_slli_epi16(Y, 7);			// This code fix an overflow case where very bright
				temp = _mm_set1_epi16(ymult);		//pixel with some color produced interim values over 32768
				Y = _mm_mulhi_epi16(Y, temp);
				Y = _mm_slli_epi16(Y, 1);

				// Calculate R
				temp = _mm_set1_epi16(r_vmult);
				temp = _mm_mullo_epi16(V, temp);
				temp = _mm_srai_epi16(temp, 1); //7bit to 6
				R2 = _mm_adds_epi16(Y, temp);
				R2 = _mm_srai_epi16(R2, 6);

				// Calculate G
				temp = _mm_set1_epi16(g_vmult);
				temp = _mm_mullo_epi16(V, temp);
				temp = _mm_srai_epi16(temp, 2); //8bit to 6
				G2 = _mm_subs_epi16(Y, temp);
				temp = _mm_set1_epi16(g_umult);
				temp = _mm_mullo_epi16(U, temp);
				temp = _mm_srai_epi16(temp, 2); //8bit to 6
				G2 = _mm_subs_epi16(G2, temp);
				G2 = _mm_srai_epi16(G2, 6);

				// Calculate B
				temp = _mm_set1_epi16(b_umult);
				temp = _mm_mullo_epi16(U, temp);
				B2 = _mm_adds_epi16(Y, temp);
				B2 = _mm_srai_epi16(B2, 6);


				// Prepare to store the sixteen RGB values
				B_pi8 = _mm_packus_epi16(R1, R2);
				G_pi8 = _mm_packus_epi16(G1, G2);
				R_pi8 = _mm_packus_epi16(B1, B2);

				temp  = _mm_unpacklo_epi8(R_pi8, G_pi8);
				temp2 = _mm_unpacklo_epi8(B_pi8, _mm_set1_epi8(RGBA_DEFAULT_ALPHA));

				// Store the first four RGB values
				RGBA = _mm_unpacklo_epi16(temp, temp2);
				*RGBA_ptr++ = RGBA;

				// Store the second four RGB values
				RGBA = _mm_unpackhi_epi16(temp, temp2);
				*RGBA_ptr++ = RGBA;

				temp = _mm_unpackhi_epi8(R_pi8, G_pi8);
				temp2 = _mm_unpackhi_epi8(B_pi8, _mm_set1_epi8(RGBA_DEFAULT_ALPHA));

				// Store the third four RGB values
				RGBA = _mm_unpacklo_epi16(temp, temp2);
				*RGBA_ptr++ = RGBA;

				// Store the fourth four RGB values
				RGBA = _mm_unpackhi_epi16(temp, temp2);
				*RGBA_ptr++ = RGBA;

			}

			// Check that the loop ends at the right position
			assert(column == post_column);
#endif

			// Process the rest of the row with 7-bit fixed point arithmetic
			for(; column < roi.width; column ++) {
				int R, G, B;
				int Y, U, V;
				uint8_t *RGBA_ptr = &RGBA_row[column*4];

				// Convert the first set of YCbCr values
				if(saturate)
				{
					Y = SATURATE_Y(Y_row[column]    >> Y_prescale);
					V = SATURATE_Cr(U_row[column/2] >> V_prescale);
					U = SATURATE_Cb(V_row[column/2] >> U_prescale);
				}
				else
				{
					Y = (Y_row[column]   >> Y_prescale);
					V = (U_row[column/2] >> V_prescale);
					U = (V_row[column/2] >> U_prescale);
				}

				Y = Y - y_offset;
				U = U - 128;
				V = V - 128;

				Y = Y * ymult >> 7;

				R = (Y     + r_vmult * V) >> 7;
				G = (Y*2  -  g_umult * U - g_vmult * V) >> 8;
				B = (Y + 2 * b_umult * U) >> 7;

				RGBA_ptr[0] = SATURATE_8U(B);
				RGBA_ptr[1] = SATURATE_8U(G);
				RGBA_ptr[2] = SATURATE_8U(R);
				RGBA_ptr[3] = RGBA_DEFAULT_ALPHA;

				// Convert the second set of YCbCr values
				if(saturate)
					Y = SATURATE_Y(Y_row[column+1] >> Y_prescale);
				else
					Y = (Y_row[column+1] >> Y_prescale);


				Y = Y - y_offset;
				Y = Y * ymult >> 7;

				R = (Y           + r_vmult * V) >> 7;
				G = (Y*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y + 2 * b_umult * U) >> 7;

				RGBA_ptr[4] = SATURATE_8U(B);
				RGBA_ptr[5] = SATURATE_8U(G);
				RGBA_ptr[6] = SATURATE_8U(R);
				RGBA_ptr[7] = RGBA_DEFAULT_ALPHA;
			}

			// Advance the YUV pointers
			Y_row += Y_pitch;
			U_row += U_pitch;
			V_row += V_pitch;

			// Advance the RGB pointers
			RGBA_row += output_pitch;
		}
	}
}

// Convert the input frame from YUV422 to RGB
void ConvertLowpass16sYUVtoRGB48(IMAGE *images[], uint8_t *output_buffer, int output_width,
								int output_height, int32_t output_pitch, int colorspace,
								bool inverted, int descale, int format, int whitebitdepth)
{
	PIXEL *plane[3];
	int pitch[3];
	ROI roi;
	int channel;

	int y_offset = 16; // not VIDEO_RGB & not YUV709
	int ymult = 128*149;	//7bit 1.164
	int r_vmult = 204;		//7bit 1.596
	int g_vmult = 208;		//8bit 0.813
	int g_umult = 100;		//8bit 0.391
	int b_umult = 129;		//6bit 2.018
	int saturate = 1;
	int mmx_y_offset = (y_offset<<7);
	int upconvert422to444 = 0;
	int dnshift = 0;

	if(whitebitdepth)
		dnshift = 16 - whitebitdepth;

	//colorspace |= COLOR_SPACE_422_TO_444; //DAN20090601
	output_pitch /= sizeof(PIXEL16U);

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


#if (_USE_YCBCR == 0) || (_ENABLE_GAMMA_CORRECTION == 1)
	// Check that the correct compiler time switches are set correctly
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


	// Convert from pixel to byte data
	for (channel = 0; channel < 3; channel++)
	{
		IMAGE *image = images[channel];

		plane[channel] = (PIXEL *)(image->band[0]);
		pitch[channel] = image->pitch/sizeof(PIXEL);

		if (channel == 0) {
			roi.width = image->width;
			roi.height = image->height;
		}
	}

	{
		PIXEL16U *Y_row, *U_row, *V_row;
		int Y_pitch, U_pitch, V_pitch;
		int Y_prescale, U_prescale, V_prescale;
		PIXEL16U *RGBA_row;
		int row, column;
		//int column_step = 2;

		Y_row = (PIXEL16U *)plane[0]; Y_pitch = pitch[0]; Y_prescale = descale + PRESCALE_LUMA;
		U_row = (PIXEL16U *)plane[1]; U_pitch = pitch[1]; U_prescale = descale + PRESCALE_CHROMA;
		V_row = (PIXEL16U *)plane[2]; V_pitch = pitch[2]; V_prescale = descale + PRESCALE_CHROMA;

		RGBA_row = (PIXEL16U *)&output_buffer[0];
		if(inverted) {
			RGBA_row += (output_height - 1) * output_pitch;
			output_pitch = -output_pitch;
		}

		//TODO SSE2

		for (row = 0; row < output_height; row++)
		{
			unsigned short *RGB_ptr = &RGBA_row[0];
			// Process the rest of the row with 7-bit fixed point arithmetic
			for(column=0; column<roi.width; column+=2)
			{
				int R, G, B;
				int Y, U, V;

				// Convert the first set of YCbCr values
				if(saturate)
				{
					Y = SATURATE_Y(Y_row[column]    << (8-Y_prescale));
					V = SATURATE_Cr(U_row[column/2] << (8-V_prescale));
					U = SATURATE_Cb(V_row[column/2] << (8-U_prescale));
				}
				else
				{
					Y = Y_row[column]   << (8-Y_prescale);
					V = U_row[column/2] << (8-V_prescale);
					U = V_row[column/2] << (8-U_prescale);
				}

				Y = Y - (y_offset<<8);
				U = U - 32768;
				V = V - 32768;

				Y = Y * ymult >> 7;

				R = (Y     + r_vmult * V) >> 7;
				G = (Y*2  -  g_umult * U - g_vmult * V) >> 8;
				B = (Y + 2 * b_umult * U) >> 7;

				if(dnshift)
				{
					R >>= dnshift;
					G >>= dnshift;
					B >>= dnshift;
				}
				else
				{
					R = SATURATE_16U(R);
					G = SATURATE_16U(G);
					B = SATURATE_16U(B);
				}

				switch(format)
				{
				case DECODED_FORMAT_B64A: //b64a
					*RGB_ptr++ = 0xffff;
					*RGB_ptr++ = R;
					*RGB_ptr++ = G;
					*RGB_ptr++ = B;
					break;
				case DECODED_FORMAT_R210: //r210 byteswap(R10G10B10A2)
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

						*RGB = _bswap(rgb);
						RGB_ptr+=2;
					}
					break;
				case DECODED_FORMAT_DPX0: //r210 byteswap(R10G10B10A2)
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

						*RGB = _bswap(rgb);
						RGB_ptr+=2;
					}
					break;
				case DECODED_FORMAT_RG30: //rg30 A2B10G10R10
				case DECODED_FORMAT_AB10:
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
				case DECODED_FORMAT_AR10:
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
				case DECODED_FORMAT_RG64: //RGBA64
					*RGB_ptr++ = R;
					*RGB_ptr++ = G;
					*RGB_ptr++ = B;
					*RGB_ptr++ = 0xffff;
					break;
				case DECODED_FORMAT_RG48: //RGB48
					*RGB_ptr++ = R;
					*RGB_ptr++ = G;
					*RGB_ptr++ = B;
					break;
				}

				// Convert the second set of YCbCr values
				if(saturate)
					Y = SATURATE_Y(Y_row[column+1] << (8-U_prescale));
				else
					Y = (Y_row[column+1] << (8-U_prescale));


				Y = Y - (y_offset<<8);
				Y = Y * ymult >> 7;

				R = (Y           + r_vmult * V) >> 7;
				G = (Y*2 -  g_umult * U - g_vmult * V) >> 8;
				B = (Y + 2 * b_umult * U) >> 7;


				if(dnshift)
				{
					R >>= dnshift;
					G >>= dnshift;
					B >>= dnshift;
				}
				else
				{
					R = SATURATE_16U(R);
					G = SATURATE_16U(G);
					B = SATURATE_16U(B);
				}


				switch(format)
				{
				case DECODED_FORMAT_B64A: //b64a
					*RGB_ptr++ = 0xffff;
					*RGB_ptr++ = R;
					*RGB_ptr++ = G;
					*RGB_ptr++ = B;
					break;
				case DECODED_FORMAT_R210: //r210 byteswap(R10G10B10A2)
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

						*RGB = _bswap(rgb);
						RGB_ptr+=2;
					}
					break;
				case DECODED_FORMAT_DPX0: //r210 byteswap(R10G10B10A2)
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

						*RGB = _bswap(rgb);
						RGB_ptr+=2;
					}
					break;
				case DECODED_FORMAT_RG30: //rg30 A2B10G10R10
				case DECODED_FORMAT_AB10:
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
				case DECODED_FORMAT_AR10:
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
				case DECODED_FORMAT_RG64: //RGBA64
					*RGB_ptr++ = R;
					*RGB_ptr++ = G;
					*RGB_ptr++ = B;
					*RGB_ptr++ = 0xffff;
					break;
				case DECODED_FORMAT_RG48: //RGB48
					*RGB_ptr++ = R;
					*RGB_ptr++ = G;
					*RGB_ptr++ = B;
					break;
				}

			}

			// Advance the YUV pointers
			Y_row += Y_pitch;
			U_row += U_pitch;
			V_row += V_pitch;

			// Advance the RGB pointer
			RGBA_row += output_pitch;
		}
	}
}




// Convert the input frame from YUV422 to RGB
void ConvertLowpass16sRGB48ToRGB(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch, int format, int colorspace, bool inverted, int descale, int num_channels)
{
	PIXEL *plane[CODEC_MAX_CHANNELS];
	int pitch[CODEC_MAX_CHANNELS];
	ROI roi;
	int channel;
	int saturate = 1;

	// Only 24 and 32 bit true color RGB formats are supported
	assert(format == COLOR_FORMAT_RGB24 || format == COLOR_FORMAT_RGB32);

	
	//alpha_Companded likely doesn't set as the is not does to go through Convert4444LinesToOutput

	// Convert from pixel to byte data
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *image = images[channel];

		plane[channel] = (PIXEL *)(image->band[0]);
		pitch[channel] = image->pitch/sizeof(PIXEL);

		if (channel == 0) {
			roi.width = image->width;
			roi.height = image->height;
		}
	}

	// Output to RGB24 format?
	if (format == COLOR_FORMAT_RGB24)
	{
		PIXEL *R_row, *G_row, *B_row;
		int R_pitch, G_pitch, B_pitch;
		int R_prescale, G_prescale, B_prescale;
		uint8_t *RGB_row;
		int row, column;

		G_row = plane[0]; G_pitch = pitch[0]; G_prescale = descale + PRESCALE_LUMA;
		R_row = plane[1]; R_pitch = pitch[1]; R_prescale = descale + PRESCALE_LUMA;
		B_row = plane[2]; B_pitch = pitch[2]; B_prescale = descale + PRESCALE_LUMA;

		RGB_row = &output_buffer[0];
		if (inverted && output_pitch > 0) {
			RGB_row += (output_height - 1) * output_pitch;
			output_pitch = -output_pitch;
		}

		for (row = 0; row < output_height; row++)
		{
			//int column_step = 16;
			//int post_column = roi.width - (roi.width % column_step);
			//__m64 *R_ptr = (__m64 *)R_row;
			//__m64 *G_ptr = (__m64 *)G_row;
			//__m64 *B_ptr = (__m64 *)B_row;
			//uint8_t *RGB_ptr = RGB_row;
			//int *RGB_int_ptr = (int *)RGB_ptr;
			//__m64 *output_ptr = (__m64 *)RGB_ptr;

			column = 0;
			// Process the rest of the row with 7-bit fixed point arithmetic
			for(; column < roi.width; column ++) {
				int R, G, B;
				uint8_t *RGB_ptr = &RGB_row[column*3];

				// Convert the first set of YCbCr values
				R = (R_row[column] >> R_prescale);
				G = (G_row[column] >> G_prescale);
				B = (B_row[column] >> B_prescale);
				if(saturate)
				{
					if(R < 0) R=0; if(R > 255) R=255;
					if(G < 0) G=0; if(G > 255) G=255;
					if(B < 0) B=0; if(B > 255) B=255;
				}

				RGB_ptr[0] = B;
				RGB_ptr[1] = G;
				RGB_ptr[2] = R;
			}

			// Fill the rest of the output row with black
			for (; column < output_width; column++)
			{
				uint8_t *RGB_ptr = &RGB_row[column*3];

				RGB_ptr[0] = 0;
				RGB_ptr[1] = 0;
				RGB_ptr[2] = 0;
			}

			// Advance the YUV pointers
			R_row += R_pitch;
			G_row += G_pitch;
			B_row += B_pitch;

			// Advance the RGB pointers
			RGB_row += output_pitch;
		}
	}

	else	// Output format is RGB32 so set the alpha channel to the default
	{
		PIXEL *R_row, *G_row, *B_row, *A_row;
		int R_pitch, G_pitch, B_pitch, A_pitch;
		int R_prescale, G_prescale, B_prescale, A_prescale;
		uint8_t *RGBA_row;
		int row, column;

		G_row = plane[0]; G_pitch = pitch[0]; G_prescale = descale + PRESCALE_LUMA;
		R_row = plane[1]; R_pitch = pitch[1]; R_prescale = descale + PRESCALE_LUMA;
		B_row = plane[2]; B_pitch = pitch[2]; B_prescale = descale + PRESCALE_LUMA;

		if(num_channels == 4)
		{
			A_row = plane[3]; A_pitch = pitch[3]; A_prescale = descale + PRESCALE_LUMA;
		}


		RGBA_row = &output_buffer[0];
		if(inverted) {
			RGBA_row += (output_height - 1) * output_pitch;
			output_pitch = -output_pitch;
		}

		for (row = 0; row < output_height; row++)
		{
			//int column_step = 16;
			//int post_column = roi.width - (roi.width % column_step);
			/*
			__m64 *R_ptr = (__m64 *)R_row;
			__m64 *G_ptr = (__m64 *)G_row;
			__m64 *B_ptr = (__m64 *)B_row;
			__m64 *A_ptr = (__m64 *)A_row;
			__m64 *RGBA_ptr = (__m64 *)RGBA_row;*/

			column = 0;
			// Process the rest of the row with 7-bit fixed point arithmetic
			for(; column < roi.width; column ++) {
				int R, G, B;
				uint8_t *RGBA_ptr = &RGBA_row[column*4];

				// Convert the first set of YCbCr values
				R = (R_row[column] >> R_prescale);
				G = (G_row[column] >> G_prescale);
				B = (B_row[column] >> B_prescale);
				if(saturate)
				{
					if(R < 0) R=0; if(R > 255) R=255;
					if(G < 0) G=0; if(G > 255) G=255;
					if(B < 0) B=0; if(B > 255) B=255;
				}

				RGBA_ptr[0] = B;
				RGBA_ptr[1] = G;
				RGBA_ptr[2] = R;

				if(num_channels == 4)
				{
					int A = A_row[column];

					// Remove the alpha encoding curve.
					//A -= 16<<A_prescale;
					//A <<= 8;
					//A += 111;
					//A /= 223;
					//12-bit SSE calibrated code
					//a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
					//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
					//a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));

					A >>= A_prescale; // 8-bit
					A <<= 4;// 12-bit
					A -= alphacompandDCoffset;
					A <<= 3; //15-bit
					A *= alphacompandGain;
					A >>= 16; //12-bit

					A >>= A_prescale;

					if(saturate)
					{
						if(A < 0) A=0; if(A > 255) A=255;
					}
					RGBA_ptr[3] = A;
				}
				else
					RGBA_ptr[3] = RGBA_DEFAULT_ALPHA;
			}

			// Advance the YUV pointers
			R_row += R_pitch;
			G_row += G_pitch;
			B_row += B_pitch;
			A_row += A_pitch;

			// Advance the RGB pointers
//			R_row += output_pitch;
//			G_row += output_pitch;
//			B_row += output_pitch;
			RGBA_row += output_pitch;
		}
	}
}



// Convert the input frame from YUV422 to RGB
void ConvertLowpass16sRGB48ToRGB48(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch, int descale, int num_channels)
{
	PIXEL *plane[CODEC_MAX_CHANNELS];
	int pitch[CODEC_MAX_CHANNELS];
	ROI roi;
	int channel;
	//int saturate = 1;


	// Convert from pixel to byte data
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *image = images[channel];

		plane[channel] = (PIXEL *)(image->band[0]);
		pitch[channel] = image->pitch/sizeof(PIXEL);

		if (channel == 0) {
			roi.width = image->width;
			roi.height = image->height;
		}
	}

	// Output to RGB48 format?
	{
		PIXEL *R_row, *G_row, *B_row;
		int R_pitch, G_pitch, B_pitch;
		int R_prescale, G_prescale, B_prescale;
		unsigned short *RGB_row;
		int row, column;

		G_row = plane[0]; G_pitch = pitch[0]; G_prescale = descale + PRESCALE_LUMA;
		R_row = plane[1]; R_pitch = pitch[1]; R_prescale = descale + PRESCALE_LUMA;
		B_row = plane[2]; B_pitch = pitch[2]; B_prescale = descale + PRESCALE_LUMA;

		RGB_row = (uint16_t *)&output_buffer[0];

		for (row = 0; row < output_height; row++)
		{
			column = 0;
			// Process the rest of the row with 7-bit fixed point arithmetic
			for(; column < roi.width; column ++)
			{
				int R, G, B;
				unsigned short *RGB_ptr = &RGB_row[column*3];

				// Convert the first set of YCbCr values
				R = R_row[column];
				G = G_row[column];
				B = B_row[column];

			/*	R >>= R_prescale;
				G >>= G_prescale;
				B >>= B_prescale;
				if(saturate)
				{
					if(R < 0) R=0; if(R > 255) R=255;
					if(G < 0) G=0; if(G > 255) G=255;
					if(B < 0) B=0; if(B > 255) B=255;
				}*/

				RGB_ptr[0] = R<<descale;
				RGB_ptr[1] = G<<descale;
				RGB_ptr[2] = B<<descale;
			}

			// Fill the rest of the output row with black
			for (; column < output_width; column++)
			{
				uint8_t *RGB_ptr = (uint8_t *)&RGB_row[column*3];

				RGB_ptr[0] = 0;
				RGB_ptr[1] = 0;
				RGB_ptr[2] = 0;
			}

			// Advance the YUV pointers
			R_row += R_pitch;
			G_row += G_pitch;
			B_row += B_pitch;

			// Advance the RGB pointers
			RGB_row += output_pitch>>1;
		}
	}
}


void ConvertLowpass16sBayerToRGB48(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch, int descale, int num_channels)
{
	PIXEL *plane[CODEC_MAX_CHANNELS];
	int pitch[CODEC_MAX_CHANNELS];
	ROI roi;
	int channel;
	//int saturate = 1;


	// Convert from pixel to byte data
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *image = images[channel];

		plane[channel] = (PIXEL *)(image->band[0]);
		pitch[channel] = image->pitch/sizeof(PIXEL);

		if (channel == 0) {
			roi.width = image->width;
			roi.height = image->height;
		}
	}

	// Output to RGB48 format?
	{
		PIXEL *R_row, *G_row, *B_row;
		int R_pitch, G_pitch, B_pitch;
		int R_prescale, G_prescale, B_prescale;
		unsigned short *RGB_row;
		int row, column;

		G_row = plane[0]; G_pitch = pitch[0]; G_prescale = descale + PRESCALE_LUMA;
		R_row = plane[1]; R_pitch = pitch[1]; R_prescale = descale + PRESCALE_LUMA;
		B_row = plane[2]; B_pitch = pitch[2]; B_prescale = descale + PRESCALE_LUMA;

		RGB_row = (uint16_t *)&output_buffer[0];

		for (row = 0; row < output_height; row++)
		{
			unsigned short *RGB_ptr = &RGB_row[0];
			column = 0;
			// Process the rest of the row with 7-bit fixed point arithmetic
			for(; column < roi.width; column ++)
			{
				int R, G, B;

				// Convert the first set of YCbCr values
				R = R_row[column]<<descale;
				G = G_row[column]<<descale;
				B = B_row[column]<<descale;

				R = G+(R*2-65535);//*2); //DAN200080816 -- fixed grn bayer thumbnails
				B = G+(B*2-65535);//*2);

			/*	R >>= R_prescale;
				G >>= G_prescale;
				B >>= B_prescale;
				if(saturate)
				{
					if(R < 0) R=0; if(R > 255) R=255;
					if(G < 0) G=0; if(G > 255) G=255;
					if(B < 0) B=0; if(B > 255) B=255;
				}*/


				if(R < 0) R=0; if(R > 65535) R=65535;
				if(G < 0) G=0; if(G > 65535) G=65535;
				if(B < 0) B=0; if(B > 65535) B=65535;

				*RGB_ptr++  = R;
				*RGB_ptr++  = G;
				*RGB_ptr++  = B;
				*RGB_ptr++  = R;
				*RGB_ptr++  = G;
				*RGB_ptr++  = B;
			}

			// Fill the rest of the output row with black
			for (; column < output_width; column++)
			{
				*RGB_ptr++ = 0;
				*RGB_ptr++ = 0;
				*RGB_ptr++ = 0;
				*RGB_ptr++ = 0;
				*RGB_ptr++ = 0;
				*RGB_ptr++ = 0;
			}

			// Advance the YUV pointers
			if(row&1)
			{
				R_row += R_pitch;
				G_row += G_pitch;
				B_row += B_pitch;
			}

			// Advance the RGB pointers
			RGB_row += output_pitch>>1;
		}
	}
}

// Convert the input frame from YUV422 to RGB
void ConvertLowpass16sRGBA64ToRGBA64(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch, int descale, int num_channels, int format)
{
	PIXEL *plane[CODEC_MAX_CHANNELS];
	int pitch[CODEC_MAX_CHANNELS];
	ROI roi;
	int channel;
	//int saturate = 1;

	// Convert from pixel to byte data
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *image = images[channel];

		plane[channel] = (PIXEL *)(image->band[0]);
		pitch[channel] = image->pitch/sizeof(PIXEL);

		if (channel == 0) {
			roi.width = image->width;
			roi.height = image->height;
		}
	}

	// Output to RGB48 format?
	if (num_channels == 3)
	{
		PIXEL *R_row, *G_row, *B_row;
		int R_pitch, G_pitch, B_pitch;
		int R_prescale, G_prescale, B_prescale;
		unsigned short *RGB_row;
		int row, column;

		G_row = plane[0]; G_pitch = pitch[0]; G_prescale = descale + PRESCALE_LUMA;
		R_row = plane[1]; R_pitch = pitch[1]; R_prescale = descale + PRESCALE_LUMA;
		B_row = plane[2]; B_pitch = pitch[2]; B_prescale = descale + PRESCALE_LUMA;

		RGB_row = (unsigned short *)&output_buffer[0];

		for (row = 0; row < output_height; row++)
		{
			//int column_step = 16;
			//int post_column = roi.width - (roi.width % column_step);
			//__m64 *R_ptr = (__m64 *)R_row;
			//__m64 *G_ptr = (__m64 *)G_row;
			//__m64 *B_ptr = (__m64 *)B_row;
			//uint8_t *RGB_ptr = (uint8_t *)RGB_row;
			//int *RGB_int_ptr = (int *)RGB_ptr;
			//__m64 *output_ptr = (__m64 *)RGB_ptr;

			column = 0;
			// Process the rest of the row with 7-bit fixed point arithmetic
			for(; column < roi.width; column ++) {
				int R, G, B;
				unsigned short *RGB_ptr = &RGB_row[column*4];

				// Convert the first set of YCbCr values
				R = R_row[column]<<R_prescale;
				G = G_row[column]<<G_prescale;
				B = B_row[column]<<B_prescale;

				switch(format)
				{
				case DECODED_FORMAT_B64A:
					RGB_ptr[0] = 0xffff;
					RGB_ptr[1] = B;
					RGB_ptr[2] = G;
					RGB_ptr[3] = R;
					break;

				case DECODED_FORMAT_R210: //byteswap(R10G10B10A2)
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

						*RGB = _bswap(rgb);
					}
					break;

				case DECODED_FORMAT_DPX0: //byteswap(R10G10B10A2)
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

						*RGB = _bswap(rgb);
					}
					break;
				case DECODED_FORMAT_RG30: //rg30 A2B10G10R10
				case DECODED_FORMAT_AB10:
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
					}
					break;
				case DECODED_FORMAT_AR10: //A2R10G10B10
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
					}
					break;
				case DECODED_FORMAT_RG64:
					RGB_ptr[0] = R;
					RGB_ptr[1] = G;
					RGB_ptr[2] = B;
					RGB_ptr[3] = 0xffff;
					break;
				}
			}
			// Fill the rest of the output row with black
			for (; column < output_width; column++)
			{
				uint8_t *RGB_ptr = (uint8_t *)&RGB_row[column*4];

				RGB_ptr[0] = 0;
				RGB_ptr[1] = 0;
				RGB_ptr[2] = 0;
				RGB_ptr[3] = 0;
			}

			// Advance the YUV pointers
			R_row += R_pitch;
			G_row += G_pitch;
			B_row += B_pitch;

			// Advance the RGB pointers
			RGB_row += output_pitch>>1;
		}
	}
	else	// Output format is RGB32 so set the alpha channel to the default
	{
		PIXEL *R_row, *G_row, *B_row, *A_row;
		int R_pitch, G_pitch, B_pitch, A_pitch;
		int R_prescale, G_prescale, B_prescale, A_prescale;
		unsigned short *RGBA_row;
		int row, column;

		G_row = plane[0]; G_pitch = pitch[0]; G_prescale = descale + PRESCALE_LUMA;
		R_row = plane[1]; R_pitch = pitch[1]; R_prescale = descale + PRESCALE_LUMA;
		B_row = plane[2]; B_pitch = pitch[2]; B_prescale = descale + PRESCALE_LUMA;
		A_row = plane[3]; A_pitch = pitch[3]; A_prescale = descale + PRESCALE_LUMA;

		RGBA_row = (uint16_t *)&output_buffer[0];

		for (row = 0; row < output_height; row++)
		{
			//int column_step = 16;
			//int post_column = roi.width - (roi.width % column_step);
			/*
			__m64 *R_ptr = (__m64 *)R_row;
			__m64 *G_ptr = (__m64 *)G_row;
			__m64 *B_ptr = (__m64 *)B_row;
			__m64 *A_ptr = (__m64 *)A_row;
			__m64 *RGBA_ptr = (__m64 *)RGBA_row;*/

			column = 0;
			// Process the rest of the row with 7-bit fixed point arithmetic
			for(; column < roi.width; column ++)
			{
				int R, G, B, A;
				unsigned short *RGB_ptr = &RGBA_row[column*4];

				// Convert the first set of YCbCr values
				R = R_row[column];
				G = G_row[column];
				B = B_row[column];
				A = A_row[column];

				{
					int A = A_row[column];
					A <<= 1;

					// Remove the alpha encoding curve.
					//A -= 16<<8;
					//A <<= 8;
					//A += 111;
					//A /= 223;
					//12-bit SSE calibrated code
					//a2_output_epi16 = _mm_subs_epu16(a2_output_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
					//a2_output_epi16 = _mm_slli_epi16(a2_output_epi16, 3);  //15-bit
					//a2_output_epi16 = _mm_mulhi_epi16(a2_output_epi16, _mm_set1_epi16(alphacompandGain));

					A >>= 4; //12-bit
					A -= alphacompandDCoffset;
					A <<= 3; //15-bit
					A *= alphacompandGain;
					A >>= 16; //12-bit
					A <<= 4; //16-bit

					if(A < 0) A=0; if(A > 0xffff) A=0xffff;
				}

				switch(format)
				{
				case DECODED_FORMAT_B64A:
					RGB_ptr[0] = A;
					RGB_ptr[1] = B;
					RGB_ptr[2] = G;
					RGB_ptr[3] = R;
					break;
				case DECODED_FORMAT_R210: //byteswap(R10G10B10A2)
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

						*RGB = _bswap(rgb);
					}
					break;
				case DECODED_FORMAT_DPX0: //byteswap(R10G10B10A2)
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

						*RGB = _bswap(rgb);
					}
					break;
				case DECODED_FORMAT_RG30: //rg30 A2B10G10R10
				case DECODED_FORMAT_AB10:
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
					}
					break;
				case DECODED_FORMAT_AR10: //A2R10G10B10
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
					}
					break;
				case DECODED_FORMAT_RG64: //RGBA64
					RGB_ptr[0] = R;
					RGB_ptr[1] = G;
					RGB_ptr[2] = B;
					RGB_ptr[3] = A;
					break;
				}
			}

			// Advance the YUV pointers
			R_row += R_pitch;
			G_row += G_pitch;
			B_row += B_pitch;
			A_row += A_pitch;

			// Advance the RGB pointers
//			R_row += output_pitch;
//			G_row += output_pitch;
//			B_row += output_pitch;
			RGBA_row += output_pitch>>1;
		}
	}
}


void ConvertLowpass16sToYUV(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch,
							int format, bool inverted)
{
	IMAGE *y_image = images[0];
	IMAGE *u_image = images[1];
	IMAGE *v_image = images[2];
	int width = y_image->width;

	PIXEL *y_row_ptr = y_image->band[0];
	PIXEL *u_row_ptr = u_image->band[0];
	PIXEL *v_row_ptr = v_image->band[0];
	int y_pitch = y_image->pitch/sizeof(PIXEL);
	int u_pitch = u_image->pitch/sizeof(PIXEL);
	int v_pitch = v_image->pitch/sizeof(PIXEL);
	//int y_prescale = PRESCALE_LUMA;
	//int u_prescale = PRESCALE_CHROMA;
	//int v_prescale = PRESCALE_CHROMA;

	//size_t output_size = height * output_pitch;
	//size_t output_width = output_pitch / 2;
	uint8_t *outrow = output_buffer;
	uint8_t *outptr;
	int row, column;

	// Definitions for optimization
#if MMXSUPPORTED //TODO DANREMOVE
	const int column_step = 2 * sizeof(__m64);
	__m64 *yuvptr;

	// Column at which post processing must begin
	int post_column = width - (width % column_step);
#endif

	// The output pitch should be a positive number before inversion
	assert(output_pitch > 0);

	// Should the image be inverted?
	if (inverted) {
		outrow += (output_height - 1) * output_pitch;		// Start at the bottom row
		output_pitch = (- output_pitch);			// Negate the pitch to go up
	}

	if ((format&0xffff) == COLOR_FORMAT_YUYV)
	{
		for (row = 0; row < output_height; row++)
		{
			column = 0;
			outptr = (uint8_t *)outrow;

#if MMXSUPPORTED //TODO DANREMOVE
#if (1 && XMMOPT)
			yuvptr = (__m64 *)outrow;

			for (; column < post_column; column += column_step)
			{
				__m64 first_pi16;	// First four signed shorts of color components
				__m64 second_pi16;	// Second four signed shorts of color components

				__m64 yyyy;		// Eight unsigned bytes of color components
				__m64 uuuu;
				__m64 vvvv;
				__m64 uvuv;
				__m64 yuyv;		// Interleaved bytes of luma and chroma

				__m64 mask;		// Mask for zero chroma values

				// Adjust the column for YUV 4:2:2 frame format
				int chroma_column = column/2;

				// Load eight signed shorts of luma
				first_pi16  = *((__m64 *)&y_row_ptr[column]);
				second_pi16 = *((__m64 *)&y_row_ptr[column+4]);

				// Convert to eight unsigned bytes of luma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_LUMA);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_LUMA);
				yyyy = _mm_packs_pu16(first_pi16, second_pi16);

#if (0 && STRICT_SATURATE)
				// Perform strict saturation on luma if required
				yyyy = _mm_subs_pu8(yyyy, _mm_set1_pi8(16));
				yyyy = _mm_adds_pu8(yyyy, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
				yyyy = _mm_subs_pu8(yyyy, _mm_set1_pi8(20));
#endif
				// Load eight signed shorts of chroma
				first_pi16  = *((__m64 *)&u_row_ptr[chroma_column]);
				second_pi16 = *((__m64 *)&u_row_ptr[chroma_column+4]);

				// Convert to eight unsigned bytes of chroma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_CHROMA);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_CHROMA);
				uuuu = _mm_packs_pu16(first_pi16, second_pi16);

#if (0 && STRICT_SATURATE)
				// Perform strict saturation on chroma if required
				uuuu = _mm_subs_pu8(uuuu, _mm_set1_pi8(16));
				uuuu = _mm_adds_pu8(uuuu, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
				uuuu = _mm_subs_pu8(uuuu, _mm_set1_pi8(15));
#endif
				// Load eight signed shorts of luma
				first_pi16  = *((__m64 *)&v_row_ptr[chroma_column]);
				second_pi16 = *((__m64 *)&v_row_ptr[chroma_column+4]);

				// Convert to eight unsigned bytes of chroma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_CHROMA);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_CHROMA);
				vvvv = _mm_packs_pu16(first_pi16, second_pi16);

#if (0 && STRICT_SATURATE)
				// Perform strict saturation on chroma if required
				vvvv = _mm_subs_pu8(vvvv, _mm_set1_pi8(16));
				vvvv = _mm_adds_pu8(vvvv, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
				vvvv = _mm_subs_pu8(vvvv, _mm_set1_pi8(15));
#endif
				// Pack eight bytes of luma with alternating bytes of chroma

				uvuv = _mm_unpacklo_pi8(vvvv, uuuu);	// Interleave first four chroma
				yuyv = _mm_unpacklo_pi8(yyyy, uvuv);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the first four yuyv pairs

				yuyv = _mm_unpackhi_pi8(yyyy, uvuv);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the second four yuyv pairs

				// Interleave eight more luma values with the remaining chroma

				// Load the next eight signed shorts of luma
				first_pi16  = *((__m64 *)&y_row_ptr[column+8]);
				second_pi16 = *((__m64 *)&y_row_ptr[column+12]);

				// Convert to eight unsigned bytes of luma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_LUMA);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_LUMA);
				yyyy = _mm_packs_pu16(first_pi16, second_pi16);

#if (0 && STRICT_SATURATE)
				// Perform strict saturation on luma if required
				yyyy = _mm_subs_pu8(yyyy, _mm_set1_pi8(16));
				yyyy = _mm_adds_pu8(yyyy, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
				yyyy = _mm_subs_pu8(yyyy, _mm_set1_pi8(20));
#endif
				uvuv = _mm_unpackhi_pi8(vvvv, uuuu);	// Interleave second four chroma
				yuyv = _mm_unpacklo_pi8(yyyy, uvuv);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the third four yuyv pairs

				yuyv = _mm_unpackhi_pi8(yyyy, uvuv);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the fourth four yuyv pairs

				// Done interleaving eight bytes of each chroma channel with sixteen luma
			}

			//_mm_empty();

			// Should have exited the loop at the post processing column
			assert(column == post_column);

			// Get the byte pointer to the rest of the row
			outptr = (uint8_t *)yuvptr;
#endif
#endif

			// Process the rest of the row
			for (; column < width; column++)
			{
				PIXEL value;

				// Copy the luminance byte to the output
				value = y_row_ptr[column] >> PRESCALE_LUMA;
				*(outptr++) = SATURATE_8U(value);

				// Copy the chroma to the output
				value = v_row_ptr[column/2] >> PRESCALE_CHROMA;
				*(outptr++) = SATURATE_8U(value);

				// Copy the luminance to the output
				value = y_row_ptr[++column] >> PRESCALE_LUMA;
				*(outptr++) = SATURATE_8U(value);

				// Copy the chroma to the output
				value = u_row_ptr[column/2] >> PRESCALE_CHROMA;
				*(outptr++) = SATURATE_8U(value);
			}

			// Should have exited the loop just after the last column
			assert(column == width);

#if 1
			// Check that the output width is valid
			assert(output_width >= width);

			// Fill the rest of the output row
			for (; column < output_width; column++)
			{
				// Set the luminance byte to black
				*(outptr++) = COLOR_LUMA_BLACK;

				// Zero the chroma byte
				*(outptr++) = COLOR_CHROMA_ZERO;

				// Set the luminance to the black
				*(outptr++) = COLOR_LUMA_BLACK;

				// Zero the chroma byte
				*(outptr++) = COLOR_CHROMA_ZERO;
			}
#endif
			// Advance to the next rows in the input and output images
			y_row_ptr += y_pitch;
			u_row_ptr += u_pitch;
			v_row_ptr += v_pitch;
			outrow += output_pitch;
		}

#if MMXSUPPORTED //TODO DANREMOVE
		//_mm_empty();		// Clear the mmx register state
#endif
	}
	else if ((format&0xffff) == COLOR_FORMAT_UYVY)
	{
		for (row = 0; row < output_height; row++)
		{
			column = 0;
			outptr = (uint8_t *)outrow;

#if MMXSUPPORTED //TODO DANREMOVE
			yuvptr = (__m64 *)outrow;
			for (; column < post_column; column += column_step)
			{
				__m64 first_pi16;	// First four signed shorts of color components
				__m64 second_pi16;	// Second four signed shorts of color components

				__m64 yyyy;		// Eight unsigned bytes of color components
				__m64 uuuu;
				__m64 vvvv;
				__m64 uvuv;
				__m64 yuyv;		// Interleaved bytes of luma and chroma

				__m64 mask;		// Mask for zero chroma values

				// Adjust the column for YUV 4:2:2 frame format
				int chroma_column = column/2;

				// Load eight signed shorts of luma
				first_pi16  = *((__m64 *)&y_row_ptr[column]);
				second_pi16 = *((__m64 *)&y_row_ptr[column+4]);

				// Convert to eight unsigned bytes of luma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_LUMA);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_LUMA);
				yyyy = _mm_packs_pu16(first_pi16, second_pi16);

				// Load eight signed shorts of chroma
				first_pi16  = *((__m64 *)&u_row_ptr[chroma_column]);
				second_pi16 = *((__m64 *)&u_row_ptr[chroma_column+4]);

				// Convert to eight unsigned bytes of chroma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_CHROMA);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_CHROMA);
				uuuu = _mm_packs_pu16(first_pi16, second_pi16);

				// Load eight signed shorts of luma
				first_pi16  = *((__m64 *)&v_row_ptr[chroma_column]);
				second_pi16 = *((__m64 *)&v_row_ptr[chroma_column+4]);

				// Convert to eight unsigned bytes of chroma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_CHROMA);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_CHROMA);
				vvvv = _mm_packs_pu16(first_pi16, second_pi16);

				// Pack eight bytes of luma with alternating bytes of chroma

				uvuv = _mm_unpacklo_pi8(vvvv, uuuu);	// Interleave first four chroma
				yuyv = _mm_unpacklo_pi8(uvuv, yyyy);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the first four yuyv pairs

				yuyv = _mm_unpackhi_pi8(uvuv, yyyy);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the second four yuyv pairs

				// Interleave eight more luma values with the remaining chroma

				// Load the next eight signed shorts of luma
				first_pi16  = *((__m64 *)&y_row_ptr[column+8]);
				second_pi16 = *((__m64 *)&y_row_ptr[column+12]);

				// Convert to eight unsigned bytes of luma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_LUMA);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_LUMA);
				yyyy = _mm_packs_pu16(first_pi16, second_pi16);

				uvuv = _mm_unpackhi_pi8(vvvv, uuuu);	// Interleave second four chroma
				yuyv = _mm_unpacklo_pi8(uvuv, yyyy);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the third four yuyv pairs

				yuyv = _mm_unpackhi_pi8(uvuv, yyyy);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the fourth four yuyv pairs

				// Done interleaving eight bytes of each chroma channel with sixteen luma
			}

			//_mm_empty();

			// Should have exited the loop at the post processing column
			assert(column == post_column);

			// Get the byte pointer to the rest of the row
			outptr = (uint8_t *)yuvptr;
#endif

			// Process the rest of the row
			for (; column < width; column++)
			{
				PIXEL value;

				// Copy the chroma to the output
				value = v_row_ptr[column/2] >> PRESCALE_CHROMA;
				*(outptr++) = SATURATE_8U(value);

				// Copy the luminance byte to the output
				value = y_row_ptr[column] >> PRESCALE_LUMA;
				*(outptr++) = SATURATE_8U(value);

				// Copy the chroma to the output
				value = u_row_ptr[column/2] >> PRESCALE_CHROMA;
				*(outptr++) = SATURATE_8U(value);

				// Copy the luminance to the output
				value = y_row_ptr[++column] >> PRESCALE_LUMA;
				*(outptr++) = SATURATE_8U(value);
			}

			// Should have exited the loop just after the last column
			assert(column == width);

			// Check that the output width is valid
			assert(output_width >= width);

			// Fill the rest of the output row
			for (; column < output_width; column++)
			{
				// Zero the chroma byte
				*(outptr++) = COLOR_CHROMA_ZERO;

				// Set the luminance byte to black
				*(outptr++) = COLOR_LUMA_BLACK;

				// Zero the chroma byte
				*(outptr++) = COLOR_CHROMA_ZERO;

				// Set the luminance to the black
				*(outptr++) = COLOR_LUMA_BLACK;
			}

			// Advance to the next rows in the input and output images
			y_row_ptr += y_pitch;
			u_row_ptr += u_pitch;
			v_row_ptr += v_pitch;
			outrow += output_pitch;
		}

#if MMXSUPPORTED //TODO DANREMOVE
		//_mm_empty();	// Clear the mmx register state
#endif
	}
	else assert(0);		// Only support YUYV and UYVY formats
}


//TODO DAN04262004 make the routine XMM
void ConvertLowpass16sToYUV64(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch,
							  int format, bool inverted, int precision)
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
	//int y_prescale = PRESCALE_LUMA;
	//int u_prescale = PRESCALE_CHROMA;
	//int v_prescale = PRESCALE_CHROMA;

	//size_t output_size = height * output_pitch;
	//size_t output_width = output_pitch / 2;
	PIXEL *outrow = (PIXEL *)output_buffer;
	PIXEL *outptr;
	int row, column;

	// Definitions for optimization
	//const int column_step = 2 * sizeof(__m64);
	__m64 *yuvptr;

	// Column at which post processing must begin
	//int post_column = width - (width % column_step);

	// The output pitch should be a positive number before inversion
	assert(output_pitch > 0);

	// Should the image be inverted?
	if (inverted) {
		outrow += (height - 1) * output_pitch;		// Start at the bottom row
		output_pitch = (- output_pitch);			// Negate the pitch to go up
	}

	if (format == COLOR_FORMAT_YU64)
	{
		for (row = 0; row < height; row++)
		{
			yuvptr = (__m64 *)outrow;
			column = 0;

#if (0 && XMMOPT)

			for (; column < post_column; column += column_step)
			{
				__m64 first_pi16;	// First four signed shorts of color components
				__m64 second_pi16;	// Second four signed shorts of color components

				__m64 yyyy;		// Eight unsigned bytes of color components
				__m64 uuuu;
				__m64 vvvv;
				__m64 uvuv;
				__m64 yuyv;		// Interleaved bytes of luma and chroma

				__m64 mask;		// Mask for zero chroma values

				// Adjust the column for YUV 4:2:2 frame format
				int chroma_column = column/2;

				// Load eight signed shorts of luma
				first_pi16  = *((__m64 *)&y_row_ptr[column]);
				second_pi16 = *((__m64 *)&y_row_ptr[column+4]);

				// Convert to eight unsigned bytes of luma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_LUMA);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_LUMA);
				yyyy = _mm_packs_pu16(first_pi16, second_pi16);

#if (0 && STRICT_SATURATE)
				// Perform strict saturation on luma if required
				yyyy = _mm_subs_pu8(yyyy, _mm_set1_pi8(16));
				yyyy = _mm_adds_pu8(yyyy, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
				yyyy = _mm_subs_pu8(yyyy, _mm_set1_pi8(20));
#endif
				// Load eight signed shorts of chroma
				first_pi16  = *((__m64 *)&u_row_ptr[chroma_column]);
				second_pi16 = *((__m64 *)&u_row_ptr[chroma_column+4]);

				// Convert to eight unsigned bytes of chroma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_CHROMA);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_CHROMA);
				uuuu = _mm_packs_pu16(first_pi16, second_pi16);

#if (0 && STRICT_SATURATE)
				// Perform strict saturation on chroma if required
				uuuu = _mm_subs_pu8(uuuu, _mm_set1_pi8(16));
				uuuu = _mm_adds_pu8(uuuu, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
				uuuu = _mm_subs_pu8(uuuu, _mm_set1_pi8(15));
#endif
				// Load eight signed shorts of luma
				first_pi16  = *((__m64 *)&v_row_ptr[chroma_column]);
				second_pi16 = *((__m64 *)&v_row_ptr[chroma_column+4]);

				// Convert to eight unsigned bytes of chroma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_CHROMA);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_CHROMA);
				vvvv = _mm_packs_pu16(first_pi16, second_pi16);

#if (0 && STRICT_SATURATE)
				// Perform strict saturation on chroma if required
				vvvv = _mm_subs_pu8(vvvv, _mm_set1_pi8(16));
				vvvv = _mm_adds_pu8(vvvv, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
				vvvv = _mm_subs_pu8(vvvv, _mm_set1_pi8(15));
#endif
				// Pack eight bytes of luma with alternating bytes of chroma

				uvuv = _mm_unpacklo_pi8(vvvv, uuuu);	// Interleave first four chroma
				yuyv = _mm_unpacklo_pi8(yyyy, uvuv);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the first four yuyv pairs

				yuyv = _mm_unpackhi_pi8(yyyy, uvuv);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the second four yuyv pairs

				// Interleave eight more luma values with the remaining chroma

				// Load the next eight signed shorts of luma
				first_pi16  = *((__m64 *)&y_row_ptr[column+8]);
				second_pi16 = *((__m64 *)&y_row_ptr[column+12]);

				// Convert to eight unsigned bytes of luma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_LUMA);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_LUMA);
				yyyy = _mm_packs_pu16(first_pi16, second_pi16);

#if (0 && STRICT_SATURATE)
				// Perform strict saturation on luma if required
				yyyy = _mm_subs_pu8(yyyy, _mm_set1_pi8(16));
				yyyy = _mm_adds_pu8(yyyy, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
				yyyy = _mm_subs_pu8(yyyy, _mm_set1_pi8(20));
#endif
				uvuv = _mm_unpackhi_pi8(vvvv, uuuu);	// Interleave second four chroma
				yuyv = _mm_unpacklo_pi8(yyyy, uvuv);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the third four yuyv pairs

				yuyv = _mm_unpackhi_pi8(yyyy, uvuv);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the fourth four yuyv pairs

				// Done interleaving eight bytes of each chroma channel with sixteen luma
			}

			//_mm_empty();

			// Should have exited the loop at the post processing column
			assert(column == post_column);
#endif
#if 1
			// Get the byte pointer to the rest of the row
			outptr = (PIXEL *)yuvptr;


			if(precision == 13) // weird mode
			{
				//int maxval = 32767;

				// Process the rest of the row
				for (; column < width; column++)
				{
					PIXEL value;

					// Copy the luminance byte to the output
					value = y_row_ptr[column];
				//	if(value < 0) value = 0;
				//	if(value > maxval) value = maxval;
				//	value <<= 16-precision;
					*(outptr++) = value<<1; //SATURATE_Y(value);	//SATURATE_8U(value);

					// Copy the chroma to the output
					value = u_row_ptr[column/2];
				//	if(value < 0) value = 0;
				//	if(value > maxval) value = maxval;
				//	value <<= 16-precision;
					*(outptr++) = value<<1; //SATURATE_Cr(value);	//SATURATE_8U(value);

					// Copy the luminance to the output
					value = y_row_ptr[++column];
				//	if(value < 0) value = 0;
				//	if(value > maxval) value = maxval;
				//	value <<= 16-precision;
					*(outptr++) = value<<1; //SATURATE_Y(value);	//SATURATE_8U(value);

					// Copy the chroma to the output
					value = v_row_ptr[column/2];
				//	if(value < 0) value = 0;
				//	if(value > maxval) value = maxval;
				//	value <<= 16-precision;
					*(outptr++) = value<<1; //SATURATE_Cb(value);	//SATURATE_8U(value);
				}
			}
			else if(precision == CODEC_PRECISION_12BIT)
			{
				// Process the rest of the row
				for (; column < width; column++)
				{
					PIXEL value;

					// Copy the luminance byte to the output
					value = y_row_ptr[column];
					if(value < 0) value = 0;
					if(value > 16383) value = 16383;
					value <<= 2;
					*(outptr++) = value; //SATURATE_Y(value);	//SATURATE_8U(value);

					// Copy the chroma to the output
					value = u_row_ptr[column/2];
					if(value < 0) value = 0;
					if(value > 16383) value = 16383;
					value <<= 2;
					*(outptr++) = value; //SATURATE_Cr(value);	//SATURATE_8U(value);

					// Copy the luminance to the output
					value = y_row_ptr[++column];
					if(value < 0) value = 0;
					if(value > 16383) value = 16383;
					value <<= 2;
					*(outptr++) = value; //SATURATE_Y(value);	//SATURATE_8U(value);

					// Copy the chroma to the output
					value = v_row_ptr[column/2];
					if(value < 0) value = 0;
					if(value > 16383) value = 16383;
					value <<= 2;
					*(outptr++) = value; //SATURATE_Cb(value);	//SATURATE_8U(value);
				}
			}
			else if(precision == CODEC_PRECISION_10BIT)
			{
				// Process the rest of the row
				for (; column < width; column++)
				{
					PIXEL value;

					// Copy the luminance byte to the output
					value = y_row_ptr[column];
					if(value < 0) value = 0;
					if(value > 4095) value = 4095;
					value <<= 4;
					*(outptr++) = value; //SATURATE_Y(value);	//SATURATE_8U(value);

					// Copy the chroma to the output
					value = u_row_ptr[column/2];
					if(value < 0) value = 0;
					if(value > 4095) value = 4095;
					value <<= 4;
					*(outptr++) = value; //SATURATE_Cr(value);	//SATURATE_8U(value);

					// Copy the luminance to the output
					value = y_row_ptr[++column];
					if(value < 0) value = 0;
					if(value > 4095) value = 4095;
					value <<= 4;
					*(outptr++) = value; //SATURATE_Y(value);	//SATURATE_8U(value);

					// Copy the chroma to the output
					value = v_row_ptr[column/2];
					if(value < 0) value = 0;
					if(value > 4095) value = 4095;
					value <<= 4;
					*(outptr++) = value; //SATURATE_Cb(value);	//SATURATE_8U(value);
				}
			}
			else
			{
				// Process the rest of the row
				for (; column < width; column++)
				{
					PIXEL value;

					// Copy the luminance byte to the output
					value = y_row_ptr[column];
					if(value < 0) value = 0;
					if(value > 1023) value = 1023;
					value <<= 6;
					*(outptr++) = value; //SATURATE_Y(value);	//SATURATE_8U(value);

					// Copy the chroma to the output
					value = u_row_ptr[column/2];
					if(value < 0) value = 0;
					if(value > 1023) value = 1023;
					value <<= 6;
					*(outptr++) = value; //SATURATE_Cr(value);	//SATURATE_8U(value);

					// Copy the luminance to the output
					value = y_row_ptr[++column];
					if(value < 0) value = 0;
					if(value > 1023) value = 1023;
					value <<= 6;
					*(outptr++) = value; //SATURATE_Y(value);	//SATURATE_8U(value);

					// Copy the chroma to the output
					value = v_row_ptr[column/2];
					if(value < 0) value = 0;
					if(value > 1023) value = 1023;
					value <<= 6;
					*(outptr++) = value; //SATURATE_Cb(value);	//SATURATE_8U(value);
				}
			}


			// Should have exited the loop just after the last column
			assert(column == width);
#endif
#if 1
			// Check that the output width is valid
			assert(output_width >= width);

			// Fill the rest of the output row
			for (; column < output_width; column++)
			{
				// Set the luminance byte to black
				*(outptr++) = COLOR_LUMA_BLACK;

				// Zero the chroma byte
				*(outptr++) = COLOR_CHROMA_ZERO<<8;

				// Set the luminance to the black
				*(outptr++) = COLOR_LUMA_BLACK;

				// Zero the chroma byte
				*(outptr++) = COLOR_CHROMA_ZERO<<8;
			}
#endif
			// Advance to the next rows in the input and output images
			y_row_ptr += y_pitch;
			u_row_ptr += u_pitch;
			v_row_ptr += v_pitch;
			outrow += output_pitch/2;
		}

#if MMXSUPPORTED //TODO DANREMOVE
		//_mm_empty();		// Clear the mmx register state
#endif
	}
	else assert(0);		// Only support YUYV and UYVY formats
}


//#if BUILD_PROSPECT
// Convert the lowpass band to rows of unpacked 16-bit YUV
void ConvertLowpass16sToYR16(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch,
							 int format, bool inverted, int precision)
{
	IMAGE *y_image = images[0];
	IMAGE *u_image = images[1];
	IMAGE *v_image = images[2];

	int width = y_image->width;
	int height = output_height;

	PIXEL *y_input_ptr = y_image->band[0];
	PIXEL *u_input_ptr = u_image->band[0];
	PIXEL *v_input_ptr = v_image->band[0];
	int y_pitch = y_image->pitch/sizeof(PIXEL);
	int u_pitch = u_image->pitch/sizeof(PIXEL);
	int v_pitch = v_image->pitch/sizeof(PIXEL);
	//int y_prescale = PRESCALE_LUMA;
	//int u_prescale = PRESCALE_CHROMA;
	//int v_prescale = PRESCALE_CHROMA;

	//size_t output_size = height * output_pitch;
	//size_t output_width = output_pitch / 2;
	//PIXEL *outrow = (PIXEL *)output_buffer;
	//PIXEL *outptr;

	// Each output row starts with a row of luma
	uint8_t *output_row_ptr = output_buffer;

	int row;

	// Process eight columns of luma per iteration of the fast loop
	//const int column_step = 8;

	// Column at which post processing must begin
	//int post_column = width - (width % column_step);

	// The output pitch should be a positive number before inversion
	assert(output_pitch > 0);

	// Should the image be inverted?
	if (inverted) {
		output_row_ptr += (height - 1) * output_pitch;	// Start at the bottom row
		output_pitch = NEG(output_pitch);				// Negate the pitch to go up
	}

	if (format == COLOR_FORMAT_YR16)
	{
		for (row = 0; row < height; row++)
		{
			PIXEL *y_output_ptr = (PIXEL *)output_row_ptr;
			PIXEL *u_output_ptr = y_output_ptr + output_width;
			PIXEL *v_output_ptr = u_output_ptr + output_width/2;

			int column = 0;
#if MMXSUPPORTED //TODO DANREMOVE
#if (1 && XMMOPT)

			for (; column < post_column; column += column_step)
			{
				__m64 y1_pi16;	// Two groups of four luma
				__m64 y2_pi16;
				__m64 u1_pi16;	// Two groups of four chroma
				__m64 v1_pi16;

				// Adjust the column for 4:2:2 chroma sampling
				int chroma_column = column/2;

				const int scale_shift = ((precision == CODEC_PRECISION_10BIT) ? 4 : 6);

				// Load eight luma values
				y1_pi16 = *((__m64 *)&y_input_ptr[column]);
				y2_pi16 = *((__m64 *)&y_input_ptr[column+4]);

				// Scale the luma values
				y1_pi16 = _mm_slli_pi16(y1_pi16, scale_shift);
				y2_pi16 = _mm_slli_pi16(y2_pi16, scale_shift);

#if (0 && STRICT_SATURATE)
				// Perform strict saturation on luma if required
				y1_pi16 = _mm_subs_pu16(y1_pi16, _mm_set1_pi16(16));
				y1_pi16 = _mm_adds_pu16(y1_pi16, _mm_set1_pi16(36));	// 36 = 16 + 20 = 16 + (255-235)
				y1_pi16 = _mm_subs_pu16(y1_pi16, _mm_set1_pi8(20));

				y2_pi16 = _mm_subs_pu16(y2_pi16, _mm_set1_pi16(16));
				y2_pi16 = _mm_adds_pu16(y2_pi16, _mm_set1_pi16(36));	// 36 = 16 + 20 = 16 + (255-235)
				y2_pi16 = _mm_subs_pu16(y2_pi16, _mm_set1_pi8(20));
#endif
				// Load four u chroma values
				u1_pi16  = *((__m64 *)&u_input_ptr[chroma_column]);

				// Scale the u chroma values
				u1_pi16  = _mm_slli_pi16(u1_pi16,  scale_shift);

#if (0 && STRICT_SATURATE)
				// Perform strict saturation on chroma if required
				u1_pi16 = _mm_subs_pu8(u1_pi16, _mm_set1_pi8(16));
				u1_pi16 = _mm_adds_pu8(u1_pi16, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
				u1_pi16 = _mm_subs_pu8(u1_pi16, _mm_set1_pi8(15));
#endif
				// Load four v chroma values
				v1_pi16  = *((__m64 *)&v_input_ptr[chroma_column]);

				// Scale the v chroma values
				v1_pi16  = _mm_slli_pi16(v1_pi16,  scale_shift);
#if (0 && STRICT_SATURATE)
				// Perform strict saturation on chroma if required
				v1_pi16 = _mm_subs_pu8(v1_pi16, _mm_set1_pi8(16));
				v1_pi16 = _mm_adds_pu8(v1_pi16, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
				v1_pi16 = _mm_subs_pu8(v1_pi16, _mm_set1_pi8(15));
#endif
				// Store the scaled luma and chroma values
				*((__m64 *)&y_output_ptr[column  ]) = y1_pi16;
				*((__m64 *)&y_output_ptr[column+4]) = y2_pi16;
				*((__m64 *)&u_output_ptr[column/2]) = u1_pi16;
				*((__m64 *)&v_output_ptr[column/2]) = v1_pi16;
			}

			//_mm_empty();

			// Should have exited the loop at the post processing column
			assert(column == post_column);
#endif
#endif

			if (precision == CODEC_PRECISION_10BIT)
			{
				// Process the rest of the row
				for (; column < width; column += 2)
				{
					PIXEL value;

					// Copy the scaled luma to the output
					value = y_input_ptr[column];
					value = SATURATE_12U(value);
					value <<= 4;
					*(y_output_ptr++) = value;

					// Copy the scaled u chroma to the output
					value = u_input_ptr[column/2];
					value = SATURATE_12U(value);
					value <<= 4;
					*(u_output_ptr++) = value;

					// Copy the scaled luma to the output
					value = y_input_ptr[column+1];
					value = SATURATE_12U(value);
					value <<= 4;
					*(y_output_ptr++) = value;

					// Copy the scaled v chroma to the output
					value = v_input_ptr[column/2];
					value = SATURATE_12U(value);
					value <<= 4;
					*(v_output_ptr++) = value;
				}
			}
			else
			{
				assert(precision == CODEC_PRECISION_8BIT);

				// Process the rest of the row
				for (; column < width; column += 2)
				{
					PIXEL value;

					// Copy the scaled luma to the output
					value = y_input_ptr[column];
					value = SATURATE_10U(value);
					value <<= 6;
					*(y_output_ptr++) = value;

					// Copy the scaled u chroma to the output
					value = u_input_ptr[column/2];
					value = SATURATE_10U(value);
					value <<= 6;
					*(u_output_ptr++) = value;

					// Copy the scaled luma to the output
					value = y_input_ptr[column+1];
					value = SATURATE_10U(value);
					value <<= 6;
					*(y_output_ptr++) = value;

					// Copy the scaled v chroma to the output
					value = v_input_ptr[column/2];
					value = SATURATE_10U(value);
					value <<= 6;
					*(v_output_ptr++) = value;
				}
			}


			// Should have exited the loop just after the last column
			assert(column == width);

			// Check that the output width is valid
			assert(output_width >= width);

			// Fill the rest of the output row
			for (; column < output_width; column++)
			{
				const int luma_value = COLOR_LUMA_BLACK;
				const int chroma_value = (COLOR_CHROMA_ZERO << 8);

				// Set the luminance byte to black
				*(y_output_ptr++) = luma_value;

				// Zero the chroma byte
				*(u_output_ptr++) = chroma_value;

				// Set the luminance to the black
				*(y_output_ptr++) = luma_value;

				// Zero the chroma byte
				*(v_output_ptr++) = chroma_value;
			}

			// Advance to the next rows in the input and output images
			y_input_ptr += y_pitch;
			u_input_ptr += u_pitch;
			v_input_ptr += v_pitch;

			output_row_ptr += output_pitch;
		}

#if MMXSUPPORTED //TODO DANREMOVE
		//_mm_empty();	// Clear the mmx register state
#endif
	}
	else
	{
		assert(0);		// Only support YR16 format
	}
}
//#endif


#if 0
#ifdef PRESCALE_LUMA10
#undef PRESCALE_LUMA10
#endif

#define PRESCALE_LUMA10		4

#ifdef PRESCALE_CHROMA10
#undef PRESCALE_CHROMA10
#endif

#define PRESCALE_CHROMA10	5
#endif

void ConvertLowpass16s10bitToYUV(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height,
								 int32_t output_pitch, int format, bool inverted, int lineskip)
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
	//int y_prescale = PRESCALE_LUMA;
	//int u_prescale = PRESCALE_CHROMA;
	//int v_prescale = PRESCALE_CHROMA;

	//size_t output_size = height * output_pitch;
	//size_t output_width = output_pitch / 2;
	uint8_t *outrow = output_buffer;
	uint8_t *outptr;
	int row, column;

	// Definitions for optimization
	//const int column_step = 2 * 8;
#if MMXSUPPORTED //TODO DANREMOVE
	__m64 *yuvptr;
#endif

	// Column at which post processing must begin
	//int post_column = width - (width % column_step);

	// The output pitch should be a positive number before inversion
	assert(output_pitch > 0);

	// Should the image be inverted?
	if (inverted) {
		outrow += (height - 1) * output_pitch;		// Start at the bottom row
		output_pitch = NEG(output_pitch);			// Negate the pitch to go up
	}

	if ((format&0xffff) == COLOR_FORMAT_YUYV)
	{
		for (row = 0; row < height; row+=lineskip)
		{
			column = 0;
			outptr = outrow;

#if MMXSUPPORTED //TODO DANREMOVE
#if (1 && XMMOPT)
			{
				__m64 ditherY_pi16;
				__m64 ditherC_pi16;
				yuvptr = (__m64 *)outrow;
				if (row & 1)
				{
					// Set the dithering values for an odd row
					ditherY_pi16 = _mm_set_pi16(14, 2, 14, 2);
				}
				else
				{
					// Set the dithering values for an even row
					ditherY_pi16 = _mm_set_pi16(6, 10, 6, 10);
				}

				ditherC_pi16 = _mm_slli_pi16(ditherY_pi16, 1);

				for (; column < post_column; column += column_step)
				{
					__m64 first_pi16;	// First four signed shorts of color components
					__m64 second_pi16;	// Second four signed shorts of color components

					__m64 yyyy;		// Eight unsigned bytes of color components
					__m64 uuuu;
					__m64 vvvv;
					__m64 uvuv;
					__m64 yuyv;		// Interleaved bytes of luma and chroma

					__m64 mask;		// Mask for zero chroma values

					// Adjust the column for YUV 4:2:2 frame format
					int chroma_column = column / 2;

					// Load eight signed shorts of luma
					first_pi16 = *((__m64 *)&y_row_ptr[column]);
					second_pi16 = *((__m64 *)&y_row_ptr[column + 4]);

					// Convert to eight unsigned bytes of luma
					first_pi16 = _mm_adds_pi16(first_pi16, ditherY_pi16);
					first_pi16 = _mm_srai_pi16(first_pi16, PRESCALE_LUMA10);
					second_pi16 = _mm_adds_pi16(second_pi16, ditherY_pi16);
					second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_LUMA10);
					yyyy = _mm_packs_pu16(first_pi16, second_pi16);

#if (0 && STRICT_SATURATE)
					// Perform strict saturation on luma if required
					yyyy = _mm_subs_pu8(yyyy, _mm_set1_pi8(16));
					yyyy = _mm_adds_pu8(yyyy, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
					yyyy = _mm_subs_pu8(yyyy, _mm_set1_pi8(20));
#endif
					// Load eight signed shorts of chroma
					first_pi16 = *((__m64 *)&u_row_ptr[chroma_column]);
					second_pi16 = *((__m64 *)&u_row_ptr[chroma_column + 4]);

					// Convert to eight unsigned bytes of chroma
					first_pi16 = _mm_adds_pi16(first_pi16, ditherC_pi16);
					first_pi16 = _mm_srai_pi16(first_pi16, PRESCALE_CHROMA10);
					second_pi16 = _mm_adds_pi16(second_pi16, ditherC_pi16);
					second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_CHROMA10);
					uuuu = _mm_packs_pu16(first_pi16, second_pi16);

#if (0 && STRICT_SATURATE)
					// Perform strict saturation on chroma if required
					uuuu = _mm_subs_pu8(uuuu, _mm_set1_pi8(16));
					uuuu = _mm_adds_pu8(uuuu, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
					uuuu = _mm_subs_pu8(uuuu, _mm_set1_pi8(15));
#endif
					// Load eight signed shorts of chroma
					first_pi16 = *((__m64 *)&v_row_ptr[chroma_column]);
					second_pi16 = *((__m64 *)&v_row_ptr[chroma_column + 4]);

					// Convert to eight unsigned bytes of chroma
					first_pi16 = _mm_adds_pi16(first_pi16, ditherC_pi16);
					first_pi16 = _mm_srai_pi16(first_pi16, PRESCALE_CHROMA10);
					second_pi16 = _mm_adds_pi16(second_pi16, ditherC_pi16);
					second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_CHROMA10);
					vvvv = _mm_packs_pu16(first_pi16, second_pi16);

#if (0 && STRICT_SATURATE)
					// Perform strict saturation on chroma if required
					vvvv = _mm_subs_pu8(vvvv, _mm_set1_pi8(16));
					vvvv = _mm_adds_pu8(vvvv, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
					vvvv = _mm_subs_pu8(vvvv, _mm_set1_pi8(15));
#endif
					// Pack eight bytes of luma with alternating bytes of chroma

					uvuv = _mm_unpacklo_pi8(vvvv, uuuu);	// Interleave first four chroma
					//uvuv = _mm_set1_pi8(128);

					yuyv = _mm_unpacklo_pi8(yyyy, uvuv);	// Interleave the luma and chroma
					_mm_stream_pi(yuvptr++, yuyv);			// Store the first four yuyv pairs

					yuyv = _mm_unpackhi_pi8(yyyy, uvuv);	// Interleave the luma and chroma
					_mm_stream_pi(yuvptr++, yuyv);			// Store the second four yuyv pairs

					// Interleave eight more luma values with the remaining chroma

					// Load the next eight signed shorts of luma
					first_pi16 = *((__m64 *)&y_row_ptr[column + 8]);
					second_pi16 = *((__m64 *)&y_row_ptr[column + 12]);

					// Convert to eight unsigned bytes of luma
					first_pi16 = _mm_adds_pi16(first_pi16, ditherY_pi16);
					first_pi16 = _mm_srai_pi16(first_pi16, PRESCALE_LUMA10);
					second_pi16 = _mm_adds_pi16(second_pi16, ditherY_pi16);
					second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_LUMA10);
					yyyy = _mm_packs_pu16(first_pi16, second_pi16);

#if (0 && STRICT_SATURATE)
					// Perform strict saturation on luma if required
					yyyy = _mm_subs_pu8(yyyy, _mm_set1_pi8(16));
					yyyy = _mm_adds_pu8(yyyy, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
					yyyy = _mm_subs_pu8(yyyy, _mm_set1_pi8(20));
#endif
					uvuv = _mm_unpackhi_pi8(vvvv, uuuu);	// Interleave second four chroma
					//uvuv = _mm_set1_pi8(128);

					yuyv = _mm_unpacklo_pi8(yyyy, uvuv);	// Interleave the luma and chroma
					_mm_stream_pi(yuvptr++, yuyv);			// Store the third four yuyv pairs

					yuyv = _mm_unpackhi_pi8(yyyy, uvuv);	// Interleave the luma and chroma
					_mm_stream_pi(yuvptr++, yuyv);			// Store the fourth four yuyv pairs

					// Done interleaving eight bytes of each chroma channel with sixteen luma
				}

				//_mm_empty();

				// Should have exited the loop at the post processing column
				assert(column == post_column);
			}
			// Get the byte pointer to the rest of the row
			outptr = (uint8_t *)yuvptr;
#endif
#endif

			// Process the rest of the row
			for (; column < width; column++)
			{
				PIXEL value;

				// Copy the luminance byte to the output
				value = y_row_ptr[column] >> PRESCALE_LUMA10;
				*(outptr++) = SATURATE_8U(value);

				// Copy the chroma to the output
				value = v_row_ptr[column/2] >> PRESCALE_CHROMA10;
				*(outptr++) = SATURATE_8U(value);

				// Copy the luminance to the output
				value = y_row_ptr[++column] >> PRESCALE_LUMA10;
				*(outptr++) = SATURATE_8U(value);

				// Copy the chroma to the output
				value = u_row_ptr[column/2] >> PRESCALE_CHROMA10;
				*(outptr++) = SATURATE_8U(value);
			}

			// Should have exited the loop just after the last column
			assert(column == width);

			// Check that the output width is valid
			assert(output_width >= width);

			// Fill the rest of the output row
			for (; column < output_width; column++)
			{
				// Set the luminance byte to black
				*(outptr++) = COLOR_LUMA_BLACK;

				// Zero the chroma byte
				*(outptr++) = COLOR_CHROMA_ZERO;

				// Set the luminance to the black
				*(outptr++) = COLOR_LUMA_BLACK;

				// Zero the chroma byte
				*(outptr++) = COLOR_CHROMA_ZERO;
			}

			// Advance to the next rows in the input and output images
			y_row_ptr += y_pitch*lineskip;// 3D Work
			u_row_ptr += u_pitch*lineskip;
			v_row_ptr += v_pitch*lineskip;

			outrow += output_pitch;
		}

#if MMXSUPPORTED //TODO DANREMOVE
		//_mm_empty();		// Clear the mmx register state
#endif
	}
	else if ((format&0xffff) == COLOR_FORMAT_UYVY)
	{
		for (row = 0; row < height; row+=lineskip)
		{
			column = 0;
			outptr = outrow;

#if MMXSUPPORTED //TODO DANREMOVE
#if (1 && XMMOPT)
			yuvptr = (__m64 *)outrow;
			for (; column < post_column; column += column_step)
			{
				__m64 first_pi16;	// First four signed shorts of color components
				__m64 second_pi16;	// Second four signed shorts of color components

				__m64 yyyy;		// Eight unsigned bytes of color components
				__m64 uuuu;
				__m64 vvvv;
				__m64 uvuv;
				__m64 yuyv;		// Interleaved bytes of luma and chroma

				__m64 mask;		// Mask for zero chroma values

				// Adjust the column for YUV 4:2:2 frame format
				int chroma_column = column/2;

				// Load eight signed shorts of luma
				first_pi16  = *((__m64 *)&y_row_ptr[column]);
				second_pi16 = *((__m64 *)&y_row_ptr[column+4]);

				// Convert to eight unsigned bytes of luma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_LUMA10);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_LUMA10);
				yyyy = _mm_packs_pu16(first_pi16, second_pi16);

				// Load eight signed shorts of chroma
				first_pi16  = *((__m64 *)&u_row_ptr[chroma_column]);
				second_pi16 = *((__m64 *)&u_row_ptr[chroma_column+4]);

				// Convert to eight unsigned bytes of chroma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_CHROMA10);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_CHROMA10);
				uuuu = _mm_packs_pu16(first_pi16, second_pi16);

				// Load eight signed shorts of luma
				first_pi16  = *((__m64 *)&v_row_ptr[chroma_column]);
				second_pi16 = *((__m64 *)&v_row_ptr[chroma_column+4]);

				// Convert to eight unsigned bytes of chroma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_CHROMA10);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_CHROMA10);
				vvvv = _mm_packs_pu16(first_pi16, second_pi16);

				// Pack eight bytes of luma with alternating bytes of chroma

				uvuv = _mm_unpacklo_pi8(vvvv, uuuu);	// Interleave first four chroma
				yuyv = _mm_unpacklo_pi8(uvuv, yyyy);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the first four yuyv pairs

				yuyv = _mm_unpackhi_pi8(uvuv, yyyy);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the second four yuyv pairs

				// Interleave eight more luma values with the remaining chroma

				// Load the next eight signed shorts of luma
				first_pi16  = *((__m64 *)&y_row_ptr[column+8]);
				second_pi16 = *((__m64 *)&y_row_ptr[column+12]);

				// Convert to eight unsigned bytes of luma
				first_pi16  = _mm_srai_pi16(first_pi16,  PRESCALE_LUMA10);
				second_pi16 = _mm_srai_pi16(second_pi16, PRESCALE_LUMA10);
				yyyy = _mm_packs_pu16(first_pi16, second_pi16);

				uvuv = _mm_unpackhi_pi8(vvvv, uuuu);	// Interleave second four chroma
				yuyv = _mm_unpacklo_pi8(uvuv, yyyy);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the third four yuyv pairs

				yuyv = _mm_unpackhi_pi8(uvuv, yyyy);	// Interleave the luma and chroma
				_mm_stream_pi(yuvptr++, yuyv);			// Store the fourth four yuyv pairs

				// Done interleaving eight bytes of each chroma channel with sixteen luma
			}

			//_mm_empty();

			// Should have exited the loop at the post processing column
			assert(column == post_column);

			// Get the byte pointer to the rest of the row
			outptr = (uint8_t *)yuvptr;
#endif
#endif

			// Process the rest of the row
			for (; column < width; column++)
			{
				PIXEL value;

				// Copy the chroma to the output
				value = v_row_ptr[column/2] >> PRESCALE_CHROMA10;
				*(outptr++) = SATURATE_8U(value);

				// Copy the luminance byte to the output
				value = y_row_ptr[column] >> PRESCALE_LUMA10;
				*(outptr++) = SATURATE_8U(value);

				// Copy the chroma to the output
				value = u_row_ptr[column/2] >> PRESCALE_CHROMA10;
				*(outptr++) = SATURATE_8U(value);

				// Copy the luminance to the output
				value = y_row_ptr[++column] >> PRESCALE_LUMA10;
				*(outptr++) = SATURATE_8U(value);
			}

			// Should have exited the loop just after the last column
			assert(column == width);

			// Check that the output width is valid
			assert(output_width >= width);

			// Fill the rest of the output row
			for (; column < output_width; column++)
			{
				// Zero the chroma byte
				*(outptr++) = COLOR_CHROMA_ZERO;

				// Set the luminance byte to black
				*(outptr++) = COLOR_LUMA_BLACK;

				// Zero the chroma byte
				*(outptr++) = COLOR_CHROMA_ZERO;

				// Set the luminance to the black
				*(outptr++) = COLOR_LUMA_BLACK;
			}

			// Advance to the next rows in the input and output images
			y_row_ptr += y_pitch*lineskip;// 3D Work
			u_row_ptr += u_pitch*lineskip;
			v_row_ptr += v_pitch*lineskip;
			outrow += output_pitch;
		}

#if MMXSUPPORTED //TODO DANREMOVE
		//_mm_empty();	// Clear the mmx register state
#endif
	}
	else assert(0);		// Only support YUYV and UYVY formats
}


//#if BUILD_PROSPECT
void ConvertLowpass16s10bitToV210(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height,
								  int32_t output_pitch, int format, bool inverted)
{
	// Note: This routine swaps the chroma values
	IMAGE *y_image = images[0];
	IMAGE *u_image = images[2];
	IMAGE *v_image = images[1];

	int width = y_image->width;
	int height = output_height;

	PIXEL *y_row_ptr = y_image->band[0];
	PIXEL *u_row_ptr = u_image->band[0];
	PIXEL *v_row_ptr = v_image->band[0];
	int y_pitch = y_image->pitch/sizeof(PIXEL);
	int u_pitch = u_image->pitch/sizeof(PIXEL);
	int v_pitch = v_image->pitch/sizeof(PIXEL);

	uint32_t *outrow = (uint32_t *)output_buffer;

	const int v210_column_step = 6;

#if (0 && XMMOPT)
	// Process four bytes each of luma and chroma per loop iteration
	const int column_step = 16;

	// Column at which post processing must begin
	int post_column = width - (width % column_step);
#endif

	int row;

	// The output pitch should be a positive number before inversion
	assert(output_pitch > 0);
	output_pitch /= sizeof(uint32_t);

#if 1
	// This routine does not handle inversion
	assert(!inverted);
#else
	// Should the image be inverted?
	if (inverted) {
		outrow += (height - 1) * output_pitch;		// Start at the bottom row
		output_pitch = (- output_pitch);			// Negate the pitch to go up
	}
#endif

	// Adjust the width to a multiple of the number of pixels packed into four words
	width -= (width % v210_column_step);

	if (format == COLOR_FORMAT_V210)
	{
		for (row = 0; row < height; row++)
		{
			int column = 0;
			int output_column = 0;

			// Process the rest of the row
			for (; column < width; column += v210_column_step)
			{
				int y1, y2;
				int u;
				int v;
				uint32_t yuv;

				// Get the first u chroma value
				u = (u_row_ptr[column/2] >> PRESCALE_CHROMA);
				if(u<0) u=0;
				if(u>1023) u=1023;

				// Get the first luma value
				y1 = (y_row_ptr[column] >> PRESCALE_LUMA);
				if(y1<0) y1=0;
				if(y1>1023) y1=1023;

				// Get the first v chroma value
				v = (v_row_ptr[column/2] >> PRESCALE_CHROMA);
				if(v<0) v=0;
				if(v>1023) v=1023;

				// Assemble and store the first packed word
				yuv = (v << V210_VALUE3_SHIFT) | (y1 << V210_VALUE2_SHIFT) | (u << V210_VALUE1_SHIFT);
				outrow[output_column++] = yuv;


				// Get the second luma value
				y1 = (y_row_ptr[column + 1] >> PRESCALE_LUMA);
				if(y1<0) y1=0;
				if(y1>1023) y1=1023;

				// Get the second u chroma value
				u = (u_row_ptr[column/2 + 1] >> PRESCALE_CHROMA);
				if(u<0) u=0;
				if(u>1023) u=1023;

				// Get the third luma value
				y2 = (y_row_ptr[column + 2] >> PRESCALE_LUMA);
				if(y2<0) y2=0;
				if(y2>1023) y2=1023;

				// Assemble and store the second packed word
				yuv = (y2 << V210_VALUE3_SHIFT) | (u << V210_VALUE2_SHIFT) | (y1 << V210_VALUE1_SHIFT);
				outrow[output_column++] = yuv;


				// Get the second v chroma value
				v = (v_row_ptr[column/2 + 1] >> PRESCALE_CHROMA);
				if(v<0) v=0;
				if(v>1023) v=1023;

				// Get the fourth luma value
				y1 = (y_row_ptr[column + 3] >> PRESCALE_LUMA);
				if(y1<0) y1=0;
				if(y1>1023) y1=1023;

				// Get the third u chroma value
				u = (u_row_ptr[column/2 + 2] >> PRESCALE_CHROMA);
				if(u<0) u=0;
				if(u>1023) u=1023;

				// Assemble and store the third packed word
				yuv = (u << V210_VALUE3_SHIFT) | (y1 << V210_VALUE2_SHIFT) | (v << V210_VALUE1_SHIFT);
				outrow[output_column++] = yuv;


				// Get the fifth luma value
				y1 = (y_row_ptr[column + 4] >> PRESCALE_LUMA);
				if(y1<0) y1=0;
				if(y1>1023) y1=1023;
				// Get the third v chroma value
				v = (v_row_ptr[column/2 + 2] >> PRESCALE_CHROMA);
				if(v<0) v=0;
				if(v>1023) v=1023;

				// Get the sixth luma value
				y2 = (y_row_ptr[column + 5] >> PRESCALE_LUMA);
				if(y2<0) y2=0;
				if(y2>1023) y2=1023;

				// Assemble and store the fourth packed word
				yuv = (y2 << V210_VALUE3_SHIFT) | (v << V210_VALUE2_SHIFT) | (y1 << V210_VALUE1_SHIFT);
				outrow[output_column++] = yuv;
			}

			// Should have exited the loop just after the last column
			assert(column == width);

			// Advance to the next rows in the input and output images
			y_row_ptr += y_pitch;
			u_row_ptr += u_pitch;
			v_row_ptr += v_pitch;
			outrow += output_pitch;
		}

		////_mm_empty();		// Clear the mmx register state
	}
	else assert(0);		// Only support V210 format
}
//#endif

#if 0
// Convert QuickTime 'BGRA' to planar YUV
void ConvertQuickTimeBGRAToFrame16s(uint8_t *bgra, int pitch, FRAME *frame, uint8_t *buffer, int color_space, int precision, int rgbaswap)
{
	ROI roi;
	int row, column;

	int display_height;

	int shift = 6; // using 10-bit math

	int y_rmult;
	int y_gmult;
	int y_bmult;
	int Y_offset;

	int u_rmult;
	int u_gmult;
	int u_bmult;

	int v_rmult;
	int v_gmult;
	int v_bmult;

	if (frame == NULL) return;

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

	assert(MIN_DECODED_COLOR_SPACE <= color_space && color_space <= MAX_DECODED_COLOR_SPACE);

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
		Y_offset = 64; //10-bit math

		u_rmult = 38;
		u_gmult = 74;
		u_bmult = 112;

		v_rmult = 112;
		v_gmult = 94;
		v_bmult = 18;

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
		Y_offset = 0;

		u_rmult = 30;
		u_gmult = 101;
		u_bmult = 131;

		v_rmult = 131;
		v_gmult = 119;
		v_bmult = 12;

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
		Y_offset = 0;

		u_rmult = 44;
		u_gmult = 87;
		u_bmult = 131;

		v_rmult = 131;
		v_gmult = 110;
		v_bmult = 21;

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
		Y_offset = 64;//10-bit math

		u_rmult = 26;
		u_gmult = 87-1;
		u_bmult = 112;

		v_rmult = 112;
		v_gmult = 102;
		v_bmult = 10;
		break;
	}


	{
		PIXEL8U *R_row, *G_row, *B_row;
		PIXEL8U *color_plane[3];
		int color_pitch[3];
		PIXEL8U *Y_row, *U_row, *V_row;
		PIXEL *Y_row16, *U_row16, *V_row16;
		int Y_pitch, U_pitch, V_pitch;
		int row, column;
		int i;
		int precisionshift = 10 - precision;

		// The frame format should be three channels of YUV (4:2:2 format)
		assert(frame->num_channels == 3);
		assert(frame->format == FRAME_FORMAT_YUV);
		display_height = frame->display_height;

		// Get pointers to the image planes and set the pitch for each plane
		for (i = 0; i < 3; i++)
		{
			IMAGE *image = frame->channel[i];

			// Set the pointer to the individual planes and pitch for each channel
			color_plane[i] = (PIXEL8U *)image->band[0];
			color_pitch[i] = image->pitch;

			// The first channel establishes the processing dimensions
			if (i == 0) {
				roi.width = image->width;
				roi.height = image->height;
			}
		}

		if(rgbaswap)//ARGB
		{
			R_row = &bgra[3]; G_row = &bgra[2]; B_row = &bgra[1];
		}
		else //BGRA
		{
			R_row = &bgra[0]; G_row = &bgra[1]; B_row = &bgra[2];
		}
#if 0
		R_row += (display_height - 1) * pitch;
		G_row += (display_height - 1) * pitch;
		B_row += (display_height - 1) * pitch;
		pitch = -pitch;
#endif
#if _YUV422		// Convert RGBA data to 4:2:2 YCbCr in one pass

#if (_USE_YCBCR == 0) || (_ENABLE_GAMMA_CORRECTION == 1)
		//Currently only handles YCbCr space without gamma correction
		assert(0);
#endif

		// Chroma is swapped
		Y_row = color_plane[0];		Y_pitch = color_pitch[0];
		U_row = color_plane[1];		U_pitch = color_pitch[1];
		V_row = color_plane[2];		V_pitch = color_pitch[2];

		for(row = 0; row < display_height; row++)
		{
			int column = 0;

			PIXEL8U *R_ptr = R_row;
			PIXEL8U *G_ptr = G_row;
			PIXEL8U *B_ptr = B_row;


#if (1 && XMMOPT)

			int column_step = 16;
			int post_column = roi.width - (roi.width % column_step);

			__m64 *Y_ptr = (__m64 *)Y_row;
			__m64 *U_ptr = (__m64 *)U_row;
			__m64 *V_ptr = (__m64 *)V_row;
			__m64 limiter = _mm_set1_pi16(0x7fff - 0x3ff); // chroma range 0 to 1023
			__m64 rounding16 = _mm_set1_pi16(1 << (shift-1));
			__m64 chromaoffset = _mm_set1_pi16(512);
			__m64 lumaoffset = _mm_set1_pi16(Y_offset);

			// Convert to YUYV in sets of 2 pixels
			for(; column < post_column; column += column_step)
			{
				__m64 R, G, B;
				__m64 R2, G2, B2;
				__m64 Y1, U1, V1;
				__m64 Y2, U2, V2;
				__m64 temp;

				/***** Calculate the first eight Y values *****/

				/***** Load the first four sets of RGB values ****/

				B = _mm_set_pi16(R_ptr[12], R_ptr[8], R_ptr[4], R_ptr[0]);
				G = _mm_set_pi16(G_ptr[12], G_ptr[8], G_ptr[4], G_ptr[0]);
				R = _mm_set_pi16(B_ptr[12], B_ptr[8], B_ptr[4], B_ptr[0]);

				// Compute Y
				temp = _mm_set1_pi16(y_rmult);
				Y1 = _mm_mullo_pi16(temp, R);
				temp = _mm_set1_pi16(y_bmult);
				temp = _mm_mullo_pi16(temp, B);
				Y1 = _mm_adds_pu16(Y1, temp);
				temp = _mm_set1_pi16(y_gmult); // As this is greater than 127, we need to shift off the result
				temp = _mm_mullo_pi16(temp, G);

				Y1 = _mm_adds_pu16(Y1, temp);
				Y1 = _mm_adds_pu16(Y1, rounding16);
				Y1 = _mm_srli_pi16(Y1, shift);
				Y1 = _mm_add_pi16(Y1, lumaoffset);
				Y1 = _mm_srai_pi16(Y1, precisionshift);

				/***** Load the second four sets of RGB values ****/

				B = _mm_set_pi16(R_ptr[28], R_ptr[24], R_ptr[20], R_ptr[16]);
				G = _mm_set_pi16(G_ptr[28], G_ptr[24], G_ptr[20], G_ptr[16]);
				R = _mm_set_pi16(B_ptr[28], B_ptr[24], B_ptr[20], B_ptr[16]);


			// Compute Y
				temp = _mm_set1_pi16(y_rmult);
				Y2 = _mm_mullo_pi16(temp, R);
				temp = _mm_set1_pi16(y_bmult);
				temp = _mm_mullo_pi16(temp, B);
				Y2 = _mm_adds_pu16(Y2, temp);
				temp = _mm_set1_pi16(y_gmult); // As this is greater than 127, we need to shift off the result
				temp = _mm_mullo_pi16(temp, G);
				Y2 = _mm_adds_pu16(Y2, temp);
				Y2 = _mm_adds_pu16(Y2, rounding16);
				Y2 = _mm_srli_pi16(Y2, shift);
				Y2 = _mm_add_pi16(Y2, lumaoffset);
				Y2 = _mm_srai_pi16(Y2, precisionshift);


//#if BUILD_PROSPECT//10-bit for everyone
				*Y_ptr++ = Y1;
				*Y_ptr++ = Y2;
//#else
				// Store the Y value
//				temp = _mm_packs_pu16(Y1, Y2);
//				*Y_ptr++ = temp;
//#endif

				/***** Calculate the first four Cb and Cr values *****/

				// Load four RGB for Cb and Cr
				B = _mm_set_pi16(R_ptr[24], R_ptr[16], R_ptr[8], R_ptr[0]);
				G = _mm_set_pi16(G_ptr[24], G_ptr[16], G_ptr[8], G_ptr[0]);
				R = _mm_set_pi16(B_ptr[24], B_ptr[16], B_ptr[8], B_ptr[0]);

				B2 = _mm_set_pi16(R_ptr[28], R_ptr[20], R_ptr[12], R_ptr[4]);
				G2 = _mm_set_pi16(G_ptr[28], G_ptr[20], G_ptr[12], G_ptr[4]);
				R2 = _mm_set_pi16(B_ptr[28], B_ptr[20], B_ptr[12], B_ptr[4]);

				B = _mm_add_pi16(B, B2);
				G = _mm_add_pi16(G, G2);
				R = _mm_add_pi16(R, R2);

				B = _mm_srli_pi16(B, 1);
				G = _mm_srli_pi16(G, 1);
				R = _mm_srli_pi16(R, 1);

				// Compute Cb
				U1 = _mm_set1_pi16(0);
				temp = _mm_set1_pi16(u_rmult);
				temp = _mm_mullo_pi16(temp, R);
				U1 = _mm_sub_pi16(U1, temp);
				temp = _mm_set1_pi16(u_gmult);
				temp = _mm_mullo_pi16(temp, G);
				U1 = _mm_subs_pi16(U1, temp);  // Added this saturation of case where R and G are 255
				temp = _mm_set1_pi16(u_bmult); // As this is sometimes greater than 127, we need to shift off the result
				temp = _mm_mullo_pi16(temp, B);
				temp = _mm_adds_pu16(temp, _mm_set1_pi16(0x8000)); // u_bmult * 255 can overflow, this saturates that overflow
				temp = _mm_subs_pu16(temp, _mm_set1_pi16(0x8000));
				U1 = _mm_adds_pi16(U1, temp);
				U1 = _mm_adds_pi16(U1, rounding16);
				U1 = _mm_srai_pi16(U1, shift);
				U1 = _mm_add_pi16(U1, chromaoffset);
				U1 = _mm_adds_pi16(U1, limiter);
				U1 = _mm_subs_pu16(U1, limiter);
				U1 = _mm_srai_pi16(U1, precisionshift);

				// Compute Cr
				V1 = _mm_set1_pi16(0);
				temp = _mm_set1_pi16(v_gmult);
				temp = _mm_mullo_pi16(temp, G);
				V1 = _mm_sub_pi16(V1, temp);
				temp = _mm_set1_pi16(v_bmult);
				temp = _mm_mullo_pi16(temp, B);
				V1 = _mm_subs_pi16(V1, temp);// Added this saturation of case where R and G are 255
				temp = _mm_set1_pi16(v_rmult); // As this is sometimes greater than 127, we need to shift off the result
				temp = _mm_mullo_pi16(temp, R);
				temp = _mm_adds_pu16(temp, _mm_set1_pi16(0x8000));// v_rmult * 255 can overflow, this saturates that overflow
				temp = _mm_subs_pu16(temp, _mm_set1_pi16(0x8000));
				V1 = _mm_adds_pi16(V1, temp);
				V1 = _mm_adds_pi16(V1, rounding16);
				V1 = _mm_srai_pi16(V1, shift);
				V1 = _mm_add_pi16(V1, chromaoffset);
				V1 = _mm_adds_pi16(V1, limiter);
				V1 = _mm_subs_pu16(V1, limiter);
				V1 = _mm_srai_pi16(V1, precisionshift);

				// Advance the RGB pointers
				R_ptr = &R_ptr[32]; G_ptr = &G_ptr[32]; B_ptr = &B_ptr[32];


				/***** Calculate the second eight Y values *****/

				/***** Load the third four sets of RGB values ****/

				B = _mm_set_pi16(R_ptr[12], R_ptr[8], R_ptr[4], R_ptr[0]);
				G = _mm_set_pi16(G_ptr[12], G_ptr[8], G_ptr[4], G_ptr[0]);
				R = _mm_set_pi16(B_ptr[12], B_ptr[8], B_ptr[4], B_ptr[0]);

				// Compute Y
				temp = _mm_set1_pi16(y_rmult);
				Y1 = _mm_mullo_pi16(temp, R);
				temp = _mm_set1_pi16(y_bmult);
				temp = _mm_mullo_pi16(temp, B);
				Y1 = _mm_adds_pu16(Y1, temp);
				temp = _mm_set1_pi16(y_gmult); // As this is greater than 127, we need to shift off the result
				temp = _mm_mullo_pi16(temp, G);
				Y1 = _mm_adds_pu16(Y1, temp);
				Y1 = _mm_adds_pu16(Y1, rounding16);
				Y1 = _mm_srli_pi16(Y1, shift);
				Y1 = _mm_add_pi16(Y1, lumaoffset);
				Y1 = _mm_srai_pi16(Y1, precisionshift);

				/***** Load the fourth four sets of RGB values ****/

				B = _mm_set_pi16(R_ptr[28], R_ptr[24], R_ptr[20], R_ptr[16]);
				G = _mm_set_pi16(G_ptr[28], G_ptr[24], G_ptr[20], G_ptr[16]);
				R = _mm_set_pi16(B_ptr[28], B_ptr[24], B_ptr[20], B_ptr[16]);

				// Compute Y
				temp = _mm_set1_pi16(y_rmult);
				Y2 = _mm_mullo_pi16(temp, R);
				temp = _mm_set1_pi16(y_bmult);
				temp = _mm_mullo_pi16(temp, B);
				Y2 = _mm_adds_pu16(Y2, temp);
				temp = _mm_set1_pi16(y_gmult); // As this is greater than 127, we need to shift off the result
				temp = _mm_mullo_pi16(temp, G);
				Y2 = _mm_adds_pu16(Y2, temp);
				Y2 = _mm_adds_pu16(Y2, rounding16);
				Y2 = _mm_srli_pi16(Y2, shift);
				Y2 = _mm_add_pi16(Y2, lumaoffset);
				Y2 = _mm_srai_pi16(Y2, precisionshift);


//#if BUILD_PROSPECT//10-bit for everyone
				*Y_ptr++ = Y1;
				*Y_ptr++ = Y2;
//#else
				// Store the Y value
//				temp = _mm_packs_pu16(Y1, Y2);
//				*Y_ptr++ = temp;
//#endif


				/***** Calculate the second four Cb and Cr values *****/

				// Load four RGB for Cb and Cr
				B = _mm_set_pi16(R_ptr[24], R_ptr[16], R_ptr[8], R_ptr[0]);
				G = _mm_set_pi16(G_ptr[24], G_ptr[16], G_ptr[8], G_ptr[0]);
				R = _mm_set_pi16(B_ptr[24], B_ptr[16], B_ptr[8], B_ptr[0]);

				B2 = _mm_set_pi16(R_ptr[28], R_ptr[20], R_ptr[12], R_ptr[4]);
				G2 = _mm_set_pi16(G_ptr[28], G_ptr[20], G_ptr[12], G_ptr[4]);
				R2 = _mm_set_pi16(B_ptr[28], B_ptr[20], B_ptr[12], B_ptr[4]);

				B = _mm_add_pi16(B, B2);
				G = _mm_add_pi16(G, G2);
				R = _mm_add_pi16(R, R2);

				B = _mm_srli_pi16(B, 1);
				G = _mm_srli_pi16(G, 1);
				R = _mm_srli_pi16(R, 1);

				// Compute Cb
				U2 = _mm_set1_pi16(0);
				temp = _mm_set1_pi16(u_rmult);
				temp = _mm_mullo_pi16(temp, R);
				U2 = _mm_sub_pi16(U2, temp);
				temp = _mm_set1_pi16(u_gmult);
				temp = _mm_mullo_pi16(temp, G);
				U2 = _mm_subs_pi16(U2, temp);// Added this saturation of case where R and G are 255
				temp = _mm_set1_pi16(u_bmult); // As this is sometimes greater than 127, we need to shift off the result
				temp = _mm_mullo_pi16(temp, B);
				temp = _mm_adds_pu16(temp, _mm_set1_pi16(0x8000));// u_bmult * 255 can overflow, this saturates that overflow
				temp = _mm_subs_pu16(temp, _mm_set1_pi16(0x8000));
				U2 = _mm_adds_pi16(U2, temp);
				U2 = _mm_adds_pi16(U2, rounding16);
				U2 = _mm_srai_pi16(U2, shift);
				U2 = _mm_add_pi16(U2, chromaoffset);
				U2 = _mm_adds_pi16(U2, limiter);
				U2 = _mm_subs_pu16(U2, limiter);
				U2 = _mm_srai_pi16(U2, precisionshift);

				// Compute Cr
				V2 = _mm_set1_pi16(0);
				temp = _mm_set1_pi16(v_gmult);
				temp = _mm_mullo_pi16(temp, G);
				V2 = _mm_sub_pi16(V2, temp);
				temp = _mm_set1_pi16(v_bmult);
				temp = _mm_mullo_pi16(temp, B);
				V2 = _mm_subs_pi16(V2, temp);// Added this saturation of case where B and G are 255
				temp = _mm_set1_pi16(v_rmult); // As this is sometimes greater than 127, we need to shift off the result
				temp = _mm_mullo_pi16(temp, R);
				temp = _mm_adds_pu16(temp, _mm_set1_pi16(0x8000));// v_rmult * 255 can overflow, this saturates that overflow
				temp = _mm_subs_pu16(temp, _mm_set1_pi16(0x8000));
				V2 = _mm_adds_pi16(V2, temp);
				V2 = _mm_adds_pi16(V2, rounding16);
				V2 = _mm_srai_pi16(V2, shift);
				V2 = _mm_add_pi16(V2, chromaoffset);
				V2 = _mm_adds_pi16(V2, limiter);
				V2 = _mm_subs_pu16(V2, limiter);
				V2 = _mm_srai_pi16(V2, precisionshift);


//#if BUILD_PROSPECT//10-bit for everyone
				*U_ptr++ = V1;
				*U_ptr++ = V2;
//#else
				// Store the CbCr values
//				temp = _mm_packs_pu16(V1, V2);
//				*U_ptr++ = temp;
//#endif


//#if BUILD_PROSPECT//10-bit for everyone
				*V_ptr++ = U1;
				*V_ptr++ = U2;
//#else
//				temp = _mm_packs_pu16(U1, U2);
//				*V_ptr++ = temp;
//#endif

				// Advance the RGB pointers
				R_ptr = &R_ptr[32]; G_ptr = &G_ptr[32]; B_ptr = &B_ptr[32];
			}

			//_mm_empty();					// Clear the registers

			// Check that the loop ended correctly
			assert(column == post_column);
#endif
			// Process the rest of the column

//#if BUILD_PROSPECT//10-bit for everyone
			Y_row16 = (PIXEL *)Y_row;
			U_row16 = (PIXEL *)U_row;
			V_row16 = (PIXEL *)V_row;
//#endif
			for(; column < roi.width; column += 2) {
				int R, G, B;
				int Y, U, V;
				int rounding = 1 << (shift-1);

				/***** Load  the first set of RGB values *****/

				B = R_ptr[0];
				G = G_ptr[0];
				R = B_ptr[0];

				// Advance the RGB pointers
				R_ptr = &R_ptr[4]; G_ptr = &G_ptr[4]; B_ptr = &B_ptr[4];

				// Convert to YCbCr
				Y = ( y_rmult * R + y_gmult * G + y_bmult * B + rounding) >> shift;
				U = (-u_rmult * R - u_gmult * G + u_bmult * B + rounding);
				V = ( v_rmult * R - v_gmult * G - v_bmult * B + rounding);

				// Store the YCbCr values
				Y += Y_offset;
//#if BUILD_PROSPECT//10-bit for everyone
				Y_row16[column] = Y;
//#else
//				Y >>= precisionshift;
//				Y_row[column] = Y;//SATURATE_Y(Y);
//#endif

				/***** Load  the second set of RGB values *****/

				B = R_ptr[0];
				G = G_ptr[0];
				R = B_ptr[0];

				// Advance the RGB pointers
				R_ptr = &R_ptr[4]; G_ptr = &G_ptr[4]; B_ptr = &B_ptr[4];

				// Convert to YCbCr
				Y =  ( y_rmult * R + y_gmult * G + y_bmult * B + rounding) >> shift;
				U += (-u_rmult * R - u_gmult * G + u_bmult * B + rounding);
				V += ( v_rmult * R - v_gmult * G - v_bmult * B + rounding);

				U >>= (shift+1);
				V >>= (shift+1);

				Y += Y_offset;
				U += 512;
				V += 512;
//#if BUILD_PROSPECT//10-bit for everyone
				U_row16[column/2] = V;
				V_row16[column/2] = U;
				Y_row16[column+1] = Y;
/*#else
				Y >>= precisionshift;
				U >>= precisionshift;
				V >>= precisionshift;
				U_row[column/2] = V;//SATURATE_Cr(V);
				V_row[column/2] = U;//SATURATE_Cb(U);
				Y_row[column+1] = Y;//SATURATE_Y(Y);
#endif
*/
			}

			// Advance the RGB pointers
			R_row += pitch;
			G_row += pitch;
			B_row += pitch;

			// Advance the YUV pointers
			Y_row += Y_pitch;
			U_row += U_pitch;
			V_row += V_pitch;
		}

#else		// Convert the RGB data to full resolution color (YUV 4:4:4 format) in one pass

#if (_USE_YCBCR == 0) || (_ENABLE_GAMMA_CORRECTION == 1)
		//Currently only handles YCbCr space without gamma correction
		assert(0);
#endif

		// Check that the chroma images are wide enough for full color
		Y_row = color_plane[0];	Y_pitch = color_pitch[0];
		U_row = color_plane[1];	U_pitch = color_pitch[1];
		V_row = color_plane[2];	V_pitch = color_pitch[2];
		assert(U_pitch == Y_pitch && V_pitch == Y_pitch);

		for(row = 0; row < display_height; row++)
		{
			int column_step = 8;
			int post_column = roi.width - (roi.width % column_step);
			PIXEL8U *R_ptr = R_row;
			PIXEL8U *G_ptr = R_row;
			PIXEL8U *B_ptr = R_row;
			__m64 *Y_ptr = (__m64 *)Y_row;
			__m64 *U_ptr = (__m64 *)U_row;
			__m64 *V_ptr = (__m64 *)V_row;

			// Convert to YUYV in sets of 2 pixels
			for(column = 0; column < post_column; column += column_step) {
				__m64 R, G, B;
				__m64 Y1, U1, V1;
				__m64 Y2, U2, V2;
				__m64 temp;

				/***** Calculate the first four Y, Cb and Cr values *****/

				/***** Load the first four sets of RGB values ****/

				B = _mm_set_pi16(R_ptr[12], R_ptr[8], R_ptr[4], R_ptr[0]);
				G = _mm_set_pi16(G_ptr[12], G_ptr[8], G_ptr[4], G_ptr[0]);
				R = _mm_set_pi16(B_ptr[12], B_ptr[8], B_ptr[4], B_ptr[0]);

				// Compute Y
				temp = _mm_set_pi16(66, 66, 66, 66);
				Y1 = _mm_mullo_pi16(temp, R);
				temp = _mm_set_pi16(129, 129, 129, 129);
				temp = _mm_mullo_pi16(temp, G);
				Y1 = _mm_add_pi16(Y1, temp);
				temp = _mm_set_pi16(25, 25, 25, 25);
				temp = _mm_mullo_pi16(temp, B);
				Y1 = _mm_add_pi16(Y1, temp);
				temp = _mm_set_pi16(4224, 4224, 4224, 4224);
				Y1 = _mm_add_pi16(Y1, temp);
				Y1 = _mm_srli_pi16(Y1, 8);

				// Compute Cb
				U1 = _mm_set_pi16(32896, 32896, 32896, 32896);
				temp = _mm_set_pi16(38, 38, 38, 38);
				temp = _mm_mullo_pi16(temp, R);
				U1 = _mm_sub_pi16(U1, temp);
				temp = _mm_set_pi16(74, 74, 74, 74);
				temp = _mm_mullo_pi16(temp, G);
				U1 = _mm_sub_pi16(U1, temp);
				temp = _mm_set_pi16(112, 112, 112, 112);
				temp = _mm_mullo_pi16(temp, B);
				U1 = _mm_add_pi16(U1, temp);
				U1 = _mm_srli_pi16(U1, 8);

				// Compute Cr
				V1 = _mm_set_pi16(32896, 32896, 32896, 32896);
				temp = _mm_set_pi16(94, 94, 94, 94);
				temp = _mm_mullo_pi16(temp, G);
				V1 = _mm_sub_pi16(V1, temp);
				temp = _mm_set_pi16(18, 18, 18, 18);
				temp = _mm_mullo_pi16(temp, B);
				V1 = _mm_sub_pi16(V1, temp);
				temp = _mm_set_pi16(112, 112, 112, 112);
				temp = _mm_mullo_pi16(temp, R);
				V1 = _mm_add_pi16(V1, temp);
				V1 = _mm_srli_pi16(V1, 8);

				// Advance the RGB pointers
				R_ptr = &R_ptr[16]; G_ptr = &G_ptr[16]; B_ptr = &B_ptr[16];


				/***** Calculate the second four Y, Cb and Cr values *****/

				/***** Load the second four sets of RGB values ****/

				B = _mm_set_pi16(R_ptr[12], R_ptr[8], R_ptr[4], R_ptr[0]);
				G = _mm_set_pi16(G_ptr[12], G_ptr[8], G_ptr[4], G_ptr[0]);
				R = _mm_set_pi16(B_ptr[12], B_ptr[8], B_ptr[4], B_ptr[0]);

				// Compute Y
				temp = _mm_set_pi16(66, 66, 66, 66);
				Y2 = _mm_mullo_pi16(temp, R);
				temp = _mm_set_pi16(129, 129, 129, 129);
				temp = _mm_mullo_pi16(temp, G);
				Y2 = _mm_add_pi16(Y2, temp);
				temp = _mm_set_pi16(25, 25, 25, 25);
				temp = _mm_mullo_pi16(temp, B);
				Y2 = _mm_add_pi16(Y2, temp);
				temp = _mm_set_pi16(4224, 4224, 4224, 4224);
				Y2 = _mm_add_pi16(Y2, temp);
				Y2 = _mm_srli_pi16(Y2, 8);

				// Compute Cb
				U2 = _mm_set_pi16(32896, 32896, 32896, 32896);
				temp = _mm_set_pi16(38, 38, 38, 38);
				temp = _mm_mullo_pi16(temp, R);
				U2 = _mm_sub_pi16(U2, temp);
				temp = _mm_set_pi16(74, 74, 74, 74);
				temp = _mm_mullo_pi16(temp, G);
				U2 = _mm_sub_pi16(U2, temp);
				temp = _mm_set_pi16(112, 112, 112, 112);
				temp = _mm_mullo_pi16(temp, B);
				U2 = _mm_add_pi16(U2, temp);
				U2 = _mm_srli_pi16(U2, 8);

				// Compute Cr
				V2 = _mm_set_pi16(32896, 32896, 32896, 32896);
				temp = _mm_set_pi16(94, 94, 94, 94);
				temp = _mm_mullo_pi16(temp, G);
				V2 = _mm_sub_pi16(V2, temp);
				temp = _mm_set_pi16(18, 18, 18, 18);
				temp = _mm_mullo_pi16(temp, B);
				V2 = _mm_sub_pi16(V2, temp);
				temp = _mm_set_pi16(112, 112, 112, 112);
				temp = _mm_mullo_pi16(temp, R);
				V2 = _mm_add_pi16(V2, temp);
				V2 = _mm_srli_pi16(V2, 8);

				// Store the Y value
				temp = _mm_packs_pu16(Y1, Y2);

#if STRICT_SATURATE		// Perform strict saturation on YUV if required
				temp = _mm_subs_pu8(temp, _mm_set1_pi8(16));
				temp = _mm_adds_pu8(temp, _mm_set1_pi8(36));	// 36 = 16 + 20 = 16 + (255-235)
				temp = _mm_subs_pu8(temp, _mm_set1_pi8(20));
#endif

				*Y_ptr++ = temp;

				// Store the Cr value
				temp = _mm_packs_pu16(V1, V2);

#if STRICT_SATURATE		// Perform strict saturation on YUV if required
				temp = _mm_subs_pu8(temp, _mm_set1_pi8(16));
				temp = _mm_adds_pu8(temp, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
				temp = _mm_subs_pu8(temp, _mm_set1_pi8(15));
#endif

				*U_ptr++ = temp;

				// Store the Cb value
				temp = _mm_packs_pu16(U1, U2);

#if STRICT_SATURATE		// Perform strict saturation on YUV if required
				temp = _mm_subs_pu8(temp, _mm_set1_pi8(16));
				temp = _mm_adds_pu8(temp, _mm_set1_pi8(31));	// 31 = 16 + 15 = 16 + (255-240)
				temp = _mm_subs_pu8(temp, _mm_set1_pi8(15));
#endif

				*V_ptr++ = temp;

				// Advance the RGB pointers
				R_ptr = &R_ptr[16]; G_ptr = &G_ptr[16]; B_ptr = &B_ptr[16];
			}

			//_mm_empty();					// Clear the registers

			// Check that the loop ended correctly
			assert(column == post_column);

			// Process the rest of the column
			for(; column < roi.width; column++) {
				int R, G, B;
				int Y, U, V;

				/***** Load one set of RGB values *****/

				B = R_ptr[0];
				G = G_ptr[0];
				R = B_ptr[0];

				// Advance the RGB pointers
				R_ptr = &R_ptr[4]; G_ptr = &G_ptr[4]; B_ptr = &B_ptr[4];

				// Convert to YCbCr
				Y = ( 66 * R + 129 * G +  25 * B +  4224) >> 8;
				U = (-38 * R -  74 * G + 112 * B + 32896) >> 8;
				V = (112 * R -  94 * G -  18 * B + 32896) >> 8;

				// Store the YCbCr values
				Y_row[column] = SATURATE_Y(Y);
				U_row[column] = SATURATE_Cr(V);
				V_row[column] = SATURATE_Cb(U);
			}

			// Advance the RGB pointers
			R_row += pitch;
			G_row += pitch;
			B_row += pitch;

			// Advance the YUV pointers
			Y_row += Y_pitch;
			U_row += U_pitch;
			V_row += V_pitch;
		}
#endif

		// Set the image parameters for each channel
		for (i = 0; i < 3; i++)
		{
			IMAGE *image = frame->channel[i];
			int band;

			// Set the image scale
			for (band = 0; band < IMAGE_NUM_BANDS; band++)
				image->scale[band] = 1;

			// Set the pixel type
//#if BUILD_PROSPECT//10-bit for everyone
			image->pixel_type[0] = PIXEL_TYPE_16S;
//#else
//			image->pixel_type[0] = PIXEL_TYPE_8U;
//#endif
		}

#if _MONOCHROME
		// Continue with the gray channel only (useful for debugging)
		frame->num_channels = 1;
		frame->format = FRAME_FORMAT_GRAY;
#endif
	}

	//_mm_empty();	// Clear the mmx register state
}
#endif
/*!
	@brief Convert the Avid 2.8 packed format to planes of 10-bit unsigned pixels
*/
void ConvertCbYCrY_10bit_2_8ToFrame16s(void *data, int pitch, FRAME *frame, void *buffer, int precision, int alpha)
{
	uint8_t *upper_plane;
	uint8_t *lower_plane;
	uint8_t *upper_row_ptr;
	uint8_t *lower_row_ptr;
	int upper_row_pitch;
	int lower_row_pitch;
	PIXEL16U *plane_array[3];
	int plane_pitch[3];
	ROI roi;
	int row, column;
	int i;

	// The frame format should be three channels of YUV (4:2:2 format)
	assert(frame->num_channels == 3);
	assert(frame->format == FRAME_FORMAT_YUV);
	//display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < 3; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		plane_array[i] = (PIXEL16U *)image->band[0];
		plane_pitch[i] = image->pitch / sizeof(PIXEL16U);

		// The first channel establishes the processing dimensions
		if (i == 0) {
			roi.width = image->width;
			roi.height = image->height;
		}
	}

	upper_plane = data;
	lower_plane = upper_plane + roi.width * roi.height / 2;

	upper_row_ptr = upper_plane;
	lower_row_ptr = lower_plane;

	upper_row_pitch = roi.width / 2;
	lower_row_pitch = roi.width * 2;

	for (row = 0; row < roi.height; row++)
	{
		// Process two pixels per iteration
		for (column = 0; column < roi.width; column += 2)
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

			plane_array[0][column + 0] = Y1;
			plane_array[0][column + 1] = Y2;
			plane_array[1][column/2] = Cr;
			plane_array[2][column/2] = Cb;
		}

		upper_row_ptr += upper_row_pitch;
		lower_row_ptr += lower_row_pitch;

		for (i = 0; i < 3; i++)
		{
			plane_array[i] += plane_pitch[i];
		}
	}
}

/*!
	@brief Convert the Avid 2.14 packed format to planes of 10-bit unsigned pixels
*/
void ConvertCbYCrY_16bit_2_14ToFrame16s(void *data, int pitch, FRAME *frame, void *buffer, int precision, int alpha)
{
	PIXEL16S *input_row_ptr = (PIXEL16S *)data;
	int input_row_pitch = pitch / sizeof(PIXEL16S);
	PIXEL16U *plane_array[3];
	int plane_pitch[3];
	ROI roi;
	int row, column;
	int i;

	// The frame format should be three channels of YUV (4:2:2 format)
	assert(frame->num_channels == 3);
	assert(frame->format == FRAME_FORMAT_YUV);
	//display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < 3; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		plane_array[i] = (PIXEL16U *)image->band[0];
		plane_pitch[i] = image->pitch / sizeof(PIXEL16U);

		// The first channel establishes the processing dimensions
		if (i == 0) {
			roi.width = image->width;
			roi.height = image->height;
		}
	}

	for (row = 0; row < roi.height; row++)
	{
		// Process two pixels per iteration
		for (column = 0; column < roi.width; column += 2)
		{
			int32_t Y1_signed, Cr_signed, Y2_signed, Cb_signed;
			int32_t Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

			// Convert Avid signed 2.14 format to 16-bit unsigned chroma
			Cb_signed = input_row_ptr[2 * column + 0];
			Cb_unsigned = (((224 * (Cb_signed + 8192)) / 16384 + 16) << 2);

			// Convert Avid signed 2.14 format to 16-bit unsigned luma
			Y1_signed = input_row_ptr[2 * column + 1];
			Y1_unsigned = (((219 * Y1_signed) / 16384 + 16) << 2);

			// Convert Avid signed 2.14 format to 16-bit unsigned chroma
			Cr_signed = input_row_ptr[2 * column + 2];
			Cr_unsigned = (((224 * (Cr_signed + 8192)) / 16384 + 16) << 2);

			// Convert Avid signed 2.14 format to 16-bit unsigned luma
			Y2_signed = input_row_ptr[2 * column + 3];
			Y2_unsigned = (((219 * Y2_signed) / 16384 + 16) << 2);

			Cb_unsigned = SATURATE_10U(Cb_unsigned);
			Y1_unsigned = SATURATE_10U(Y1_unsigned);
			Cr_unsigned = SATURATE_10U(Cr_unsigned);
			Y2_unsigned = SATURATE_10U(Y2_unsigned);

			// Output the unsigned 10-bit components for the next two pixels
			plane_array[0][column + 0] = Y1_unsigned;
			plane_array[0][column + 1] = Y2_unsigned;
			plane_array[1][column/2] = Cr_unsigned;
			plane_array[2][column/2] = Cb_unsigned;
		}

		input_row_ptr += input_row_pitch;

		for (i = 0; i < 3; i++)
		{
			plane_array[i] += plane_pitch[i];
		}
	}
}

/*!
	@brief Convert the Avid 10.6 packed format to planes of 10-bit unsigned pixels

	Note that the Avid 10.6 format is exactly the same as the CineForm 16-bit unsigned format
	except that the color channels are ordered Cb, Y1, Cr, Y2.  Within each color channel, the
	values are in big endian order according to the Avid Buffer Formats document.

	@todo Need to check big versus little endian.
*/
void ConvertCbYCrY_16bit_10_6ToFrame16s(void *data, int pitch, FRAME *frame, void *buffer, int precision, int alpha)
{
	PIXEL16U *input_row_ptr = (PIXEL16U *)data;
	int input_row_pitch = pitch / sizeof(PIXEL16U);
	PIXEL16U *output_plane_array[3];
	int output_plane_pitch[3];
	ROI roi;
	int row, column;
	int i;

	// The frame format should be three channels of YUV (4:2:2 format)
	assert(frame->num_channels == 3);
	assert(frame->format == FRAME_FORMAT_YUV);
	//display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < 3; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		output_plane_array[i] = (PIXEL16U *)image->band[0];
		output_plane_pitch[i] = image->pitch / sizeof(PIXEL16U);

		// The first channel establishes the processing dimensions
		if (i == 0) {
			roi.width = image->width;
			roi.height = image->height;
		}
	}

	for (row = 0; row < roi.height; row++)
	{
		// Process two pixels per iteration
		for (column = 0; column < roi.width; column += 2)
		{
			unsigned short Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

			// Extract the color components and shift off the fractional part
			Cb_unsigned = input_row_ptr[2 * column + 0];
			Y1_unsigned = input_row_ptr[2 * column + 1];
			Cr_unsigned = input_row_ptr[2 * column + 2];
			Y2_unsigned = input_row_ptr[2 * column + 3];

			Cb_unsigned >>= 6;
			Y1_unsigned >>= 6;
			Cr_unsigned >>= 6;
			Y2_unsigned >>= 6;

			output_plane_array[0][column + 0] = Y1_unsigned;
			output_plane_array[0][column + 1] = Y2_unsigned;
			output_plane_array[1][column/2] = Cr_unsigned;
			output_plane_array[2][column/2] = Cb_unsigned;
		}

		input_row_ptr += input_row_pitch;

		for (i = 0; i < 3; i++)
		{
			output_plane_array[i] += output_plane_pitch[i];
		}
	}
}

/*!
	@brief Convert Avid unsigned char format to planes of 10-bit unsigned pixels
*/
void ConvertCbYCrY_8bitToFrame16s(void *data, int pitch, FRAME *frame, void *buffer, int precision, int alpha)
{
	PIXEL8U *input_row_ptr = (PIXEL8U *)data;
	int input_row_pitch = pitch / sizeof(PIXEL8U);
	PIXEL16U *output_plane_array[3];
	int output_plane_pitch[3];
	ROI roi;
	int row, column;
	int i;

	// The frame format should be three channels of YUV (4:2:2 format)
	assert(frame->num_channels == 3);
	assert(frame->format == FRAME_FORMAT_YUV);
	//display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < 3; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		output_plane_array[i] = (PIXEL16U *)image->band[0];
		output_plane_pitch[i] = image->pitch / sizeof(PIXEL16U);

		// The first channel establishes the processing dimensions
		if (i == 0) {
			roi.width = image->width;
			roi.height = image->height;
		}
	}

	for (row = 0; row < roi.height; row++)
	{
		// Process two pixels per iteration
		for (column = 0; column < roi.width; column += 2)
		{
			unsigned short Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

			// Extract the color components and shift to 10 bits
			Cb_unsigned = input_row_ptr[2 * column + 0];
			Y1_unsigned = input_row_ptr[2 * column + 1];
			Cr_unsigned = input_row_ptr[2 * column + 2];
			Y2_unsigned = input_row_ptr[2 * column + 3];

			Cb_unsigned <<= 2;
			Y1_unsigned <<= 2;
			Cr_unsigned <<= 2;
			Y2_unsigned <<= 2;

			output_plane_array[0][column + 0] = Y1_unsigned;
			output_plane_array[0][column + 1] = Y2_unsigned;
			output_plane_array[1][column/2] = Cr_unsigned;
			output_plane_array[2][column/2] = Cb_unsigned;
		}

		input_row_ptr += input_row_pitch;

		for (i = 0; i < 3; i++)
		{
			output_plane_array[i] += output_plane_pitch[i];
		}
	}
}

/*!
	@brief Convert Avid short format to planes of 10-bit unsigned pixels
*/
void ConvertCbYCrY_16bitToFrame16s(void *data, int pitch, FRAME *frame, void *buffer, int precision, int alpha)
{
	PIXEL16U *input_row_ptr = (PIXEL16U *)data;
	int input_row_pitch = pitch / sizeof(PIXEL16U);
	PIXEL16U *output_plane_array[3];
	int output_plane_pitch[3];
	ROI roi;
	int row, column;
	int i;

	// The frame format should be three channels of YUV (4:2:2 format)
	assert(frame->num_channels == 3);
	assert(frame->format == FRAME_FORMAT_YUV);
	//display_height = frame->display_height;

	// Get pointers to the image planes and set the pitch for each plane
	for (i = 0; i < 3; i++)
	{
		IMAGE *image = frame->channel[i];

		// Set the pointer to the individual planes and pitch for each channel
		output_plane_array[i] = (PIXEL16U *)image->band[0];
		output_plane_pitch[i] = image->pitch / sizeof(PIXEL16U);

		// The first channel establishes the processing dimensions
		if (i == 0) {
			roi.width = image->width;
			roi.height = image->height;
		}
	}

	for (row = 0; row < roi.height; row++)
	{
		// Process two pixels per iteration
		for (column = 0; column < roi.width; column += 2)
		{
			unsigned short Y1_unsigned, Cr_unsigned, Y2_unsigned, Cb_unsigned;

			// Extract the color components and shift to 10 bits
			Cb_unsigned = input_row_ptr[2 * column + 0];
			Y1_unsigned = input_row_ptr[2 * column + 1];
			Cr_unsigned = input_row_ptr[2 * column + 2];
			Y2_unsigned = input_row_ptr[2 * column + 3];

			Cb_unsigned >>= 6;
			Y1_unsigned >>= 6;
			Cr_unsigned >>= 6;
			Y2_unsigned >>= 6;

			output_plane_array[0][column + 0] = Y1_unsigned;
			output_plane_array[0][column + 1] = Y2_unsigned;
			output_plane_array[1][column/2] = Cr_unsigned;
			output_plane_array[2][column/2] = Cb_unsigned;
		}

		input_row_ptr += input_row_pitch;

		for (i = 0; i < 3; i++)
		{
			output_plane_array[i] += output_plane_pitch[i];
		}
	}
}


#if _ALLOCATOR
void DeleteFrame(ALLOCATOR *allocator, FRAME *frame)
#else
void DeleteFrame(FRAME *frame)
#endif
{
	int i;

	if (frame == NULL) return;

	for (i = 0; i < frame->num_channels; i++)
	{
		IMAGE *image = frame->channel[i];
		if (image != NULL)
		{
#if _ALLOCATOR
			DeleteImage(allocator, image);
#else
			DeleteImage(image);
#endif
		}
	}

#if _ALLOCATOR
	Free(allocator, frame);
#else
	MEMORY_FREE(frame);
#endif
}

#if 0
/* GIMP RGBA C-Source image dump 1-byte-run-length-encoded (LicenseErrExpired.c) */
#define RUN_LENGTH_DECODE(image_buf, rle_data, size, bpp) do \
{ unsigned int __bpp; unsigned char *__ip; const unsigned char *__il, *__rd; \
  __bpp = (bpp); __ip = (image_buf); __il = __ip + (size) * __bpp; \
  __rd = (rle_data); if (__bpp > 3) { /* RGBA */ \
    while (__ip < __il) { unsigned int __l = *(__rd++); \
      if (__l & 128) { __l = __l - 128; \
        do { memcpy (__ip, __rd, 4); __ip += 4; } while (--__l); __rd += 4; \
      } else { __l *= 4; memcpy (__ip, __rd, __l); \
               __ip += __l; __rd += __l; } } \
  } else { /* RGB */ \
    while (__ip < __il) { unsigned int __l = *(__rd++); \
      if (__l & 128) { __l = __l - 128; \
        do { memcpy (__ip, __rd, 3); __ip += 3; } while (--__l); __rd += 3; \
      } else { __l *= 3; memcpy (__ip, __rd, __l); \
               __ip += __l; __rd += __l; } } \
  } } while (0)
#endif
#if 0
static const struct {
  unsigned int 	 width;
  unsigned int 	 height;
  unsigned int 	 bytes_per_pixel; /* 3:RGB, 4:RGBA */ 
  unsigned char	 rle_pixel_data[4888 + 1];
} errExpired = {
  128, 16, 4,
  "\244\0\0\0\0\204\0\0\0\1\224\0\0\0\0\203\0\0\0\1\205\0\0\0\0\202\0\0\0\1"
  "\202\0\0\0\0\204\0\0\0\1\327\0\0\0\0\2\0\0\0\1\0\0\0\5\202\0\0\0\14\2\0\0"
  "\0\6\0\0\0\1\222\0\0\0\0\5\0\0\0\3\0\0\0\10\0\0\0\14\0\0\0\11\0\0\0\3\202"
  "\0\0\0\0\2\0\0\0\1\0\0\0\5\202\0\0\0\14\3\0\0\0\6\0\0\0\2\0\0\0\5\202\0\0"
  "\0\14\2\0\0\0\6\0\0\0\1\326\0\0\0\0\6\0\0\0\3\0\0\0\26NS\0ONR\0R\24\25\0"
  "\35\0\0\0\5\221\0\0\0\0\23\0\0\0\2\0\0\0\16<@\0""8LP\0T=@\0;\0\0\0\17\0\0"
  "\0\2\0\0\0\0\0\0\0\3\0\0\0\27KP\0RLP\0T\24\25\0\36\0\0\0\10\0\0\0\27NS\0"
  "ONR\0R\24\25\0\35\0\0\0\5\300\0\0\0\0\2\0\0\0\2\0\0\0\4\202\0\0\0\6\10\0"
  "\0\0\4\0\0\0\2\0\0\0\1\0\0\0\3\0\0\0\4\0\0\0\3\0\0\0\1\0\0\0\2\202\0\0\0"
  "\4\202\0\0\0\2\203\0\0\0\4\12\0\0\0\5\0\0\0\6\0\0\0\4\0\0\0\7\0\0\0-\307"
  "\323\0\375\310\324\0\377JO\0K\0\0\0\13\0\0\0\2\205\0\0\0\4\202\0\0\0\2\1"
  "\0\0\0\4\202\0\0\0\6\33\0\0\0\4\0\0\0\2\0\0\0\0\0\0\0\1\0\0\0\2\0\0\0\5\0"
  "\0\0\12\0\0\0$\226\237\0\265\310\324\0\377\233\245\0\276\0\0\0\"\0\0\0\4"
  "\0\0\0\1\0\0\0\7\0\0\0""4\306\322\0\376\310\324\0\377BF\0T\0\0\0\23\0\0\0"
  ".\307\323\0\375\310\324\0\377JO\0K\0\0\0\13\0\0\0\2\0\0\0\4\202\0\0\0\6\5"
  "\0\0\0\5\0\0\0\2\0\0\0\1\0\0\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\202"
  "\0\0\0\1\203\0\0\0\4\202\0\0\0\5\1\0\0\0\4\202\0\0\0\1\2\0\0\0\3\0\0\0\5"
  "\202\0\0\0\6\2\0\0\0\4\0\0\0\2\202\0\0\0\0\2\0\0\0\2\0\0\0\4\202\0\0\0\6"
  "\2\0\0\0\4\0\0\0\2\232\0\0\0\0\15\0\0\0\4\0\0\0\16\0\0\0\37\0\0\0+\0\0\0"
  ",\0\0\0!\0\0\0\21\0\0\0\12\0\0\0\24\0\0\0\40\0\0\0\33\0\0\0\16\0\0\0\21\202"
  "\0\0\0\37\15\0\0\0\21\0\0\0\17\0\0\0\37\0\0\0$\0\0\0!\0\0\0'\0\0\0*\0\0\0"
  "\36\0\0\0\22\0\0\0""2\226\237\0\306\226\237\0\312/1\0D\202\0\0\0\15\5\0\0"
  "\0\37\0\0\0$\0\0\0!\0\0\0%\0\0\0\40\202\0\0\0\20\5\0\0\0\37\0\0\0+\0\0\0"
  ",\0\0\0!\0\0\0\20\202\0\0\0\5;\0\0\0\22\0\0\0$\0\0\0/\0\0\0F\210\220\0\307"
  "\310\324\0\377\222\233\0\312\0\0\0,\0\0\0\6\0\0\0\1\0\0\0\11\0\0\0C\306\322"
  "\0\376\310\324\0\3778;\0c\0\0\0\27\0\0\0""1\226\237\0\306\226\237\0\312."
  "1\0E\0\0\0\16\0\0\0\17\0\0\0\37\0\0\0,\0\0\0.\0\0\0$\0\0\0\22\0\0\0\10\0"
  "\0\0\17\0\0\0\37\0\0\0+\0\0\0,\0\0\0!\0\0\0\20\0\0\0\6\0\0\0\14\0\0\0\36"
  "\0\0\0$\0\0\0\40\0\0\0'\0\0\0*\0\0\0\36\0\0\0\15\0\0\0\13\0\0\0\32\0\0\0"
  ")\0\0\0/\0\0\0,\0\0\0\40\0\0\0\15\0\0\0\3\0\0\0\4\0\0\0\16\0\0\0\37\0\0\0"
  "+\0\0\0,\0\0\0!\0\0\0\20\0\0\0\4\230\0\0\0\0i\0\0\0\3\0\0\0\24\22\23\0@~"
  "\206\0\244\240\252\0\322\244\256\0\326\210\220\0\260\40\"\0J\0\0\0#\202\212"
  "\0m\234\245\0\270\203\213\0\233\0\0\0;\16\17\0E\226\237\0\264\240\251\0\264"
  "^c\0D\0\0\0,\232\243\0\267\224\235\0\302BF\0\204\227\240\0\304\243\255\0"
  "\326y\201\0\234\0\0\0""6\0\0\0>\217\227\0\305\220\230\0\310*,\0G\0\0\0\21"
  "\0\0\0&\232\243\0\267\224\235\0\302>B\0\203\227\240\0\303\243\255\0\300\0"
  "\0\0""6\21\22\0D~\206\0\244\240\252\0\322\244\256\0\326\210\220\0\260\40"
  "\"\0I\0\0\0\32\0\0\0\33""25\0T\225\236\0\301\243\255\0\326ry\0\266\177\207"
  "\0\327\310\324\0\377\221\231\0\314\0\0\0-\0\0\0\6\0\0\0\1\0\0\0\12\0\0\0"
  "F\306\322\0\376\310\324\0\3776:\0f\0\0\0\27\0\0\0""3\217\230\0\304\220\230"
  "\0\310),\0H\0\0\0\37\14\15\0>z\202\0\241\235\247\0\321\245\257\0\330\233"
  "\245\0\276RV\0N\0\0\0!\22\23\0B~\206\0\244\240\252\0\322\244\256\0\326\210"
  "\220\0\260\40\"\0I\0\0\0\33\0\0\0(\232\243\0\267\224\235\0\302=A\0\202\224"
  "\235\0\303\243\255\0\327}\205\0\237\0\0\0""3\0\0\0/kq\0\210\234\245\0\315"
  "\245\257\0\331\240\252\0\317\223\234\0\25347\0""2\0\0\0\13\0\0\0\24\22\23"
  "\0@~\206\0\244\240\252\0\322\244\256\0\326\210\220\0\260\40\"\0I\0\0\0\26"
  "\0\0\0\4\226\0\0\0\0V\0\0\0\1\0\0\0\14\7\7\0""9\251\263\0\336\310\324\0\377"
  "\262\274\0\357\251\264\0\351\310\324\0\377\261\274\0\347\22\23\0M$'\0L\271"
  "\304\0\355\307\323\0\377NS\0\246\205\215\0\312\310\324\0\377\243\255\0\313"
  "\0\0\0.\0\0\0B\306\322\0\376\310\324\0\377\275\310\0\367\262\275\0\355\306"
  "\322\0\376\310\324\0\377z\201\0\255\0\0\0`\306\322\0\376\310\324\0\377<?"
  "\0]\0\0\0\27\0\0\0=\306\322\0\376\310\324\0\377\272\305\0\366\307\323\0\376"
  "\274\307\0\357\4\4\0`\250\262\0\337\310\324\0\377\262\274\0\357\251\264\0"
  "\351\310\324\0\377\261\274\0\347\21\23\0N\12\13\0L\262\275\0\352\310\324"
  "\0\377\275\310\0\370\271\304\0\364\275\311\0\373\310\324\0\377\220\231\0"
  "\315\0\0\0-\0\0\0\6\0\0\0\1\0\0\0\12\0\0\0F\306\322\0\376\310\324\0\3776"
  ":\0f\0\0\0\31\0\0\0?\306\322\0\376\310\324\0\37769\0g\2\3\0F\247\261\0\335"
  "\310\324\0\377\304\317\0\374\257\272\0\351\301\315\0\366|\203\0\201\5\5\0"
  "I\251\263\0\336\310\324\0\377\262\274\0\357\251\264\0\351\310\324\0\377\261"
  "\274\0\347\22\24\0J\0\0\0J\306\322\0\376\310\324\0\377\276\311\0\370\272"
  "\306\0\366\202\310\324\0\377\23dj\0\231LP\0\203\307\323\0\377\276\311\0\372"
  "\236\247\0\340\252\265\0\343\304\320\0\372X^\0Q\0\0\0\27\6\7\0:\251\263\0"
  "\336\310\324\0\377\262\274\0\357\251\264\0\351\310\324\0\377\261\274\0\347"
  "\25\26\0B\0\0\0\16\0\0\0\1\225\0\0\0\0k\0\0\0\3\0\0\0\30io\0\213\310\324"
  "\0\377\257\272\0\365\5\5\0\235\0\0\0\226\235\247\0\354\310\324\0\377v~\0"
  "\236\0\0\0:MQ\0\204\304\320\0\375\272\305\0\372\306\322\0\377\265\300\0\361"
  "\36\40\0V\0\0\0\35\0\0\0G\306\322\0\376\310\324\0\377nt\0\271\0\0\0emt\0"
  "\272\310\324\0\377\267\302\0\363\0\0\0}\306\322\0\376\310\324\0\3777:\0e"
  "\0\0\0\32\0\0\0F\306\322\0\376\310\324\0\377\222\232\0\327\14\15\0`&(\0X"
  "_e\0\231\310\324\0\377\257\272\0\365\5\5\0\235\0\0\0\226\235\247\0\354\310"
  "\324\0\377nu\0\252[a\0\237\310\324\0\377\277\312\0\372\31\33\0}\5\5\0n\254"
  "\266\0\357\310\324\0\377\221\231\0\314\0\0\0-\0\0\0\6\0\0\0\1\0\0\0\12\0"
  "\0\0F\306\322\0\376\310\324\0\3776:\0f\0\0\0\32\0\0\0F\306\322\0\376\310"
  "\324\0\377-0\0z`f\0\227\310\324\0\377\301\315\0\37448\0\210\0\0\0F\26\30"
  "\0=:=\0Hdj\0\222\310\324\0\377\257\272\0\365\5\5\0\235\0\0\0\226\235\247"
  "\0\354\310\324\0\377t{\0\242\0\0\0`\306\322\0\376\310\324\0\377y\200\0\303"
  "\0\0\0u\224\235\0\336\310\324\0\377\213\223\0\320fm\0\255\310\324\0\377\223"
  "\234\0\347'*\0\241\3\3\0{\26\27\0c\32\34\0""4\0\0\0\"hn\0\214\310\324\0\377"
  "\257\272\0\365\5\5\0\235\0\0\0\226\235\247\0\354\310\324\0\377y\200\0\233"
  "\0\0\0\33\0\0\0\3\225\0\0\0\0\3\0\0\0\4\0\0\0!\204\214\0\262\206\310\324"
  "\0\377\4\225\236\0\271\0\0\0/\0\0\0J\200\210\0\322\202\310\324\0\377\11R"
  "W\0\251\0\0\0""3\0\0\0\23\0\0\0G\306\322\0\376\310\324\0\3777;\0\201\0\0"
  "\0;7;\0\201\202\310\324\0\377\14\10\11\0\214\306\322\0\376\310\324\0\377"
  "6:\0f\0\0\0\32\0\0\0G\306\322\0\376\310\324\0\377BF\0\204\0\0\0'\0\0\0,\203"
  "\212\0\264\206\310\324\0\377\36\212\223\0\307t{\0\276\310\324\0\377\244\256"
  "\0\350\0\0\0O\0\0\0F\212\222\0\320\310\324\0\377\221\231\0\314\0\0\0-\0\0"
  "\0\6\0\0\0\1\0\0\0\12\0\0\0F\306\322\0\376\310\324\0\3776:\0f\0\0\0\32\0"
  "\0\0G\306\322\0\376\310\324\0\377+-\0\202}\205\0\272\310\324\0\377\243\255"
  "\0\351\0\0\0L\0\0\0\30\0\0\0\22\0\0\0)\203\212\0\264\206\310\324\0\377\13"
  "\222\233\0\274\0\0\0e\306\322\0\376\310\324\0\3779<\0~\0\0\0H\203\213\0\304"
  "\310\324\0\377\221\232\0\326&(\0}\274\307\0\365\202\310\324\0\377\5\302\316"
  "\0\373\240\252\0\326-/\0T\0\0\0""2\203\213\0\263\206\310\324\0\377\3\226"
  "\237\0\270\0\0\0\37\0\0\0\4\225\0\0\0\0\6\0\0\0\3\0\0\0\35{\203\0\245\310"
  "\324\0\377\253\265\0\366QV\0\273\202Y_\0\251!]c\0\242OT\0j\0\0\0""0$'\0g"
  "\270\303\0\366\307\323\0\377\310\324\0\377\241\253\0\342\2\3\0F\0\0\0\33"
  "\0\0\0H\306\322\0\376\310\324\0\377KO\0\232\0\0\0QKO\0\232\310\324\0\377"
  "\303\316\0\374\1\1\0\205\306\322\0\376\310\324\0\3776:\0f\0\0\0\32\0\0\0"
  "G\306\322\0\376\310\324\0\37758\0i\0\0\0\27\0\0\0\40{\203\0\245\310\324\0"
  "\377\253\265\0\366QV\0\273\202Y_\0\251\"\\b\0\244BF\0\177nu\0\257\310\324"
  "\0\377\260\273\0\361\0\0\0b\0\0\0Y\226\237\0\335\310\324\0\377\221\231\0"
  "\314\0\0\0-\0\0\0\6\0\0\0\1\0\0\0\12\0\0\0F\306\322\0\376\310\324\0\3776"
  ":\0f\0\0\0\32\0\0\0G\306\322\0\376\310\324\0\377+.\0\200t{\0\255\310\324"
  "\0\377\263\276\0\364\2\2\0_\0\0\0-\0\0\0$\0\0\0""1x\177\0\251\310\324\0\377"
  "\253\265\0\366QV\0\273\202Y_\0\251\26]c\0\242KP\0o\0\0\0]\306\322\0\376\310"
  "\324\0\37736\0m\0\0\0;\206\216\0\277\310\324\0\377\223\234\0\324\0\0\0Y\24"
  "\26\0i]c\0\246w~\0\312\254\267\0\363\310\324\0\377\214\224\0\265\0\0\0<z"
  "\201\0\247\310\324\0\377\253\265\0\366QV\0\273\202Y_\0\251\4]c\0\242PU\0"
  "h\0\0\0\27\0\0\0\3\225\0\0\0\0k\0\0\0\2\0\0\0\21""8;\0\\\303\316\0\373\307"
  "\323\0\376sz\0\313LQ\0\242fm\0\257\243\255\0\323ek\0x\0\0\0J\244\256\0\332"
  "\310\324\0\377sz\0\313\240\252\0\346\310\324\0\377\203\213\0\262\0\0\0""3"
  "\0\0\0N\306\322\0\376\310\324\0\377\254\266\0\355ov\0\274\253\265\0\356\310"
  "\324\0\377\235\247\0\331\0\0\0p\306\322\0\376\310\324\0\3778;\0c\0\0\0\31"
  "\0\0\0E\306\322\0\376\310\324\0\3778;\0c\0\0\0\22\0\0\0\23""8;\0\\\303\316"
  "\0\373\307\323\0\376sz\0\313LQ\0\242fm\0\257\243\255\0\324bh\0|25\0p\304"
  "\320\0\375\310\324\0\377\210\220\0\322{\203\0\305\304\317\0\375\310\324\0"
  "\377\221\232\0\313\0\0\0,\0\0\0\6\0\0\0\1\0\0\0\11\0\0\0C\306\322\0\376\310"
  "\324\0\3778;\0c\0\0\0\31\0\0\0E\306\322\0\376\310\324\0\37714\0r.0\0h\302"
  "\316\0\372\310\324\0\377\227\240\0\332kr\0\245\221\231\0\257nt\0t14\0j\303"
  "\316\0\373\307\323\0\376sz\0\313LQ\0\242fm\0\257\243\255\0\323gm\0v\0\0\0"
  "V\306\322\0\376\310\324\0\37759\0h\0\0\0""7\210\220\0\274\310\324\0\377\223"
  "\234\0\324SX\0\204\221\231\0\272Z`\0\245FJ\0\256\227\240\0\351\310\324\0"
  "\377\216\227\0\266\0\0\0""16:\0_\303\316\0\373\307\323\0\376sz\0\313LQ\0"
  "\242fm\0\257\243\255\0\323mt\0o\0\0\0\22\0\0\0\2\225\0\0\0\0\5\0\0\0\1\0"
  "\0\0\6\0\0\0\"ag\0\205\267\302\0\356\203\310\324\0\377\40\270\303\0\347O"
  "S\0e\217\230\0\235\310\324\0\377\254\267\0\335\4\5\0S-0\0j\277\312\0\365"
  "\306\322\0\374jq\0e\0\0\0Q\306\322\0\376\310\324\0\377\203\213\0\327\305"
  "\321\0\375\310\324\0\377\263\276\0\35247\0c\0\0\0H\306\322\0\376\310\324"
  "\0\377EI\0Q\0\0\0\23\0\0\0""3\307\323\0\375\310\324\0\377EI\0Q\0\0\0\14\0"
  "\0\0\7\0\0\0\"ag\0\205\267\302\0\356\203\310\324\0\377\32\270\303\0\347W"
  "]\0[\0\0\0""4x\177\0\241\304\317\0\374\310\324\0\377\267\302\0\360\234\245"
  "\0\335\310\324\0\377\234\246\0\275\0\0\0!\0\0\0\4\0\0\0\1\0\0\0\7\0\0\0""2"
  "\307\323\0\375\310\324\0\377EI\0Q\0\0\0\23\0\0\0""3\307\323\0\375\310\324"
  "\0\377@D\0W\0\0\0.]c\0\204\265\300\0\356\202\310\324\0\377\5\307\323\0\375"
  "t{\0u\0\0\0""3af\0\206\267\302\0\356\203\310\324\0\377\14\270\303\0\347W"
  "]\0[\0\0\0@\306\322\0\376\310\324\0\377AE\0U\0\0\0)\223\234\0\255\310\324"
  "\0\377\234\245\0\310Y^\0\200\303\316\0\367\203\310\324\0\377\6\257\271\0"
  "\3439<\0T\0\0\0\30\0\0\0$ag\0\205\267\302\0\356\203\310\324\0\377\4\270\303"
  "\0\347]b\0V\0\0\0\16\0\0\0\1\226\0\0\0\0O\0\0\0\1\0\0\0\11\0\0\0\35\0\0\0"
  """7:>\0\\LP\0i')\0Q\0\0\0""2\0\0\0\37\0\0\0$\0\0\0""3\0\0\0,\0\0\0\30\0\0"
  "\0\33\0\0\0""1\0\0\0""0\0\0\0!\0\0\0J\306\322\0\376\310\324\0\377*-\0\203"
  "\"$\0XHL\0a\3\3\0""6\0\0\0\32\0\0\0\31\0\0\0""1\0\0\0""2\0\0\0\27\0\0\0\7"
  "\0\0\0\24\0\0\0""1\0\0\0""2\0\0\0\27\0\0\0\5\0\0\0\2\0\0\0\11\0\0\0\35\0"
  "\0\0""7:>\0\\LP\0i')\0Q\0\0\0""1\0\0\0\30\0\0\0\20\0\0\0%#%\0IIM\0b\11\11"
  "\0B\0\0\0;\0\0\0""8\0\0\0%\0\0\0\15\0\0\0\2\0\0\0\0\0\0\0\3\0\0\0\23\0\0"
  "\0""1\0\0\0""2\0\0\0\27\0\0\0\7\0\0\0\24\0\0\0""1\0\0\0""2\0\0\0\30\0\0\0"
  "\15\0\0\0\35\0\0\0""78;\0\\MR\0g!#\0B\0\0\0\36\0\0\0\20\0\0\0\36\0\0\0""7"
  ":>\0\\LP\0i')\0Q\0\0\0""1\202\0\0\0\31\31\0\0\0""1\0\0\0""2\0\0\0\31\0\0"
  "\0\20\0\0\0#\0\0\0""3\0\0\0,\0\0\0&\3\3\0""8<@\0\\MR\0j47\0X\0\0\0""2\0\0"
  "\0\26\0\0\0\6\0\0\0\11\0\0\0\35\0\0\0""7:>\0\\LP\0i')\0Q\0\0\0""1\0\0\0\26"
  "\0\0\0\5\0\0\0\1\227\0\0\0\0\7\0\0\0\1\0\0\0\4\0\0\0\11\0\0\0\16\0\0\0\20"
  "\0\0\0\15\0\0\0\10\202\0\0\0\5\2\0\0\0\7\0\0\0\6\202\0\0\0\3\12\0\0\0\6\0"
  "\0\0\7\0\0\0\14\0\0\0<\306\322\0\376\310\324\0\377:=\0`\0\0\0\31\0\0\0\16"
  "\0\0\0\11\202\0\0\0\3\202\0\0\0\7\3\0\0\0\3\0\0\0\1\0\0\0\3\202\0\0\0\7\24"
  "\0\0\0\3\0\0\0\1\0\0\0\0\0\0\0\1\0\0\0\4\0\0\0\11\0\0\0\16\0\0\0\20\0\0\0"
  "\15\0\0\0\10\0\0\0\3\0\0\0\2\0\0\0\6\0\0\0\13\0\0\0\16\0\0\0\13\0\0\0\11"
  "\0\0\0\10\0\0\0\5\0\0\0\2\203\0\0\0\0\1\0\0\0\3\202\0\0\0\7\3\0\0\0\3\0\0"
  "\0\1\0\0\0\3\202\0\0\0\7\17\0\0\0\3\0\0\0\2\0\0\0\4\0\0\0\11\0\0\0\16\0\0"
  "\0\20\0\0\0\13\0\0\0\5\0\0\0\2\0\0\0\4\0\0\0\11\0\0\0\16\0\0\0\20\0\0\0\15"
  "\0\0\0\10\202\0\0\0\4\202\0\0\0\7\14\0\0\0\3\0\0\0\2\0\0\0\5\0\0\0\7\0\0"
  "\0\6\0\0\0\5\0\0\0\11\0\0\0\16\0\0\0\21\0\0\0\16\0\0\0\10\0\0\0\3\202\0\0"
  "\0\1\10\0\0\0\4\0\0\0\11\0\0\0\16\0\0\0\20\0\0\0\15\0\0\0\10\0\0\0\3\0\0"
  "\0\1\233\0\0\0\0\203\0\0\0\1\211\0\0\0\0\10\0\0\0\5\0\0\0#\224\235\0\252"
  "\223\234\0\25536\0""6\0\0\0\11\0\0\0\2\0\0\0\1\217\0\0\0\0\203\0\0\0\1\204"
  "\0\0\0\0\203\0\0\0\1\223\0\0\0\0\203\0\0\0\1\204\0\0\0\0\203\0\0\0\1\214"
  "\0\0\0\0\203\0\0\0\1\206\0\0\0\0\203\0\0\0\1\252\0\0\0\0\2\0\0\0\1\0\0\0"
  "\13\202\0\0\0\34\2\0\0\0\15\0\0\0\2\337\0\0\0\0",
};
#endif
#if 0
/* GIMP RGBA C-Source image dump 1-byte-run-length-encoded (LicenseErrHD.c) */
static const struct {
  unsigned int 	 width;
  unsigned int 	 height;
  unsigned int 	 bytes_per_pixel; /* 3:RGB, 4:RGBA */ 
  unsigned char	 rle_pixel_data[5009 + 1];
} errHD = {
  128, 16, 4,
  "\212\0\0\0\0\202\0\0\0\1\1\0\0\0\0\204\0\0\0\1\367\0\0\0\0\2\0\0\0\1\0\0"
  "\0\5\202\0\0\0\14\202\0\0\0\6\202\0\0\0\14\2\0\0\0\6\0\0\0\1\321\0\0\0\0"
  "\4\0\0\0\2\0\0\0\4\0\0\0\5\0\0\0\3\202\0\0\0\1\1\0\0\0\3\202\0\0\0\5\202"
  "\0\0\0\2\2\0\0\0\5\0\0\0\6\202\0\0\0\7\3\0\0\0\6\0\0\0\4\0\0\0\2\223\0\0"
  "\0\0\12\0\0\0\3\0\0\0\27KP\0RLP\0T\22\23\0!\0\0\0\34MR\0PNR\0R\24\25\0\35"
  "\0\0\0\5\320\0\0\0\0\12\0\0\0\2\0\0\0\16\0\0\0$\0\0\0)\0\0\0\30\0\0\0\10"
  "\0\0\0\7\0\0\0\27\0\0\0)\0\0\0%\202\0\0\0\20\11\0\0\0%\0\0\0""3\0\0\0""6"
  "\0\0\0""4\0\0\0-\0\0\0!\0\0\0\20\0\0\0\5\0\0\0\1\221\0\0\0\0\14\0\0\0\7\0"
  "\0\0""4\306\322\0\376\310\324\0\377>B\0Y\0\0\0""9\307\323\0\375\310\324\0"
  "\377JO\0K\0\0\0\13\0\0\0\2\0\0\0\4\202\0\0\0\6\5\0\0\0\5\0\0\0\2\0\0\0\1"
  "\0\0\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\202\0\0\0\1\203\0\0\0\4\202"
  "\0\0\0\5\6\0\0\0\4\0\0\0\1\0\0\0\0\0\0\0\1\0\0\0\3\0\0\0\5\202\0\0\0\6\5"
  "\0\0\0\4\0\0\0\2\0\0\0\1\0\0\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\206"
  "\0\0\0\0\2\0\0\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\202\0\0\0\1\203"
  "\0\0\0\4\3\0\0\0\5\0\0\0\4\0\0\0\3\203\0\0\0\4\3\0\0\0\5\0\0\0\4\0\0\0\3"
  "\202\0\0\0\4\2\0\0\0\3\0\0\0\1\204\0\0\0\0\1\0\0\0\2\202\0\0\0\6\2\0\0\0"
  "\4\0\0\0\2\204\0\0\0\0\26\0\0\0\5\0\0\0'\247\261\0\314\255\270\0\335sz\0"
  "w\0\0\0\25\0\0\0\24ms\0n\255\270\0\335\252\264\0\323\0\0\0.\0\0\0-\246\257"
  "\0\316\250\262\0\344\247\261\0\344\244\256\0\340\233\244\0\322\177\206\0"
  "\252%'\0P\0\0\0\36\0\0\0\7\0\0\0\1\220\0\0\0\0""0\0\0\0\11\0\0\0C\306\322"
  "\0\376\310\324\0\37758\0i\0\0\0?\225\236\0\307\226\237\0\312.1\0E\0\0\0\16"
  "\0\0\0\17\0\0\0\37\0\0\0,\0\0\0.\0\0\0$\0\0\0\22\0\0\0\10\0\0\0\17\0\0\0"
  "\37\0\0\0+\0\0\0,\0\0\0!\0\0\0\20\0\0\0\6\0\0\0\14\0\0\0\36\0\0\0$\0\0\0"
  "\40\0\0\0'\0\0\0*\0\0\0\35\0\0\0\13\0\0\0\4\0\0\0\11\0\0\0\32\0\0\0)\0\0"
  "\0/\0\0\0,\0\0\0\40\0\0\0\15\0\0\0\6\0\0\0\16\0\0\0\37\0\0\0+\0\0\0,\0\0"
  "\0!\0\0\0\20\0\0\0\4\204\0\0\0\0\32\0\0\0\4\0\0\0\16\0\0\0\37\0\0\0+\0\0"
  "\0,\0\0\0!\0\0\0\20\0\0\0\6\0\0\0\14\0\0\0\37\0\0\0$\0\0\0!\0\0\0%\0\0\0"
  "\"\0\0\0\31\0\0\0\40\0\0\0$\0\0\0!\0\0\0%\0\0\0!\0\0\0\25\0\0\0\32\0\0\0"
  "\37\0\0\0\25\0\0\0\6\0\0\0\1\202\0\0\0\0\10\0\0\0\2\0\0\0\16,/\0-\0\0\0+"
  "\0\0\0\37\0\0\0\20\0\0\0\10\0\0\0\4\202\0\0\0\1\20\0\0\0\10\0\0\0:\272\306"
  "\0\364\310\324\0\377u|\0\235\0\0\0#\0\0\0!ls\0\223\310\324\0\377\300\314"
  "\0\371\0\0\0D\0\0\0B\272\306\0\364\310\324\0\377\304\317\0\375\304\320\0"
  "\375\202\310\324\0\377\4\272\306\0\364LQ\0s\0\0\0\33\0\0\0\4\220\0\0\0\0"
  """1\0\0\0\12\0\0\0F\306\322\0\376\310\324\0\37736\0l\0\0\0B\217\227\0\305"
  "\220\230\0\310),\0H\0\0\0\37\14\15\0>z\202\0\241\235\247\0\321\245\257\0"
  "\330\233\245\0\276RV\0N\0\0\0!\22\23\0B~\206\0\244\240\252\0\322\244\256"
  "\0\326\210\220\0\260\40\"\0I\0\0\0\33\0\0\0(\232\243\0\267\224\235\0\302"
  "=A\0\202\224\235\0\303\243\255\0\327}\205\0\237\0\0\0-\0\0\0\22\0\0\0&lr"
  "\0\207\234\245\0\315\245\257\0\331\240\252\0\317\223\234\0\25314\0""5\0\0"
  "\0\34\22\23\0A~\206\0\244\240\252\0\322\244\256\0\326\210\220\0\260\40\""
  "\0I\0\0\0\26\0\0\0\4\202\0\0\0\0\33\0\0\0\3\0\0\0\24\22\23\0@~\206\0\244"
  "\240\252\0\322\244\256\0\326\210\220\0\260\40\"\0I\0\0\0\33\0\0\0(\232\243"
  "\0\267\224\235\0\302>B\0\203\227\240\0\303\242\254\0\301\0\0\0G\230\241\0"
  "\271\224\235\0\302>B\0\203\227\240\0\303\242\254\0\301\0\0\0<\203\213\0\217"
  "\233\245\0\271u|\0o\0\0\0\23\0\0\0\2\202\0\0\0\0!\0\0\0\4\0\0\0\36\262\275"
  "\0\316\254\266\0\327w~\0\232\33\35\0O\0\0\0/\0\0\0\34\0\0\0\16\0\0\0\6\0"
  "\0\0\12\0\0\0@\272\305\0\365\310\324\0\377ip\0\256\0\0\0A\0\0\0?`f\0\246"
  "\310\324\0\377\300\313\0\372\0\0\0K\0\0\0I\271\304\0\366\310\324\0\377bh"
  "\0\272\0\0\0cGK\0\212\256\271\0\356\310\324\0\377\262\275\0\354\3\3\0;\0"
  "\0\0\12\0\0\0\1\217\0\0\0\0\35\0\0\0\12\0\0\0F\306\322\0\376\310\324\0\377"
  "36\0m\0\0\0N\306\322\0\376\310\324\0\37769\0g\2\3\0F\247\261\0\335\310\324"
  "\0\377\304\317\0\374\257\272\0\351\301\315\0\366|\203\0\201\5\5\0I\251\263"
  "\0\336\310\324\0\377\262\274\0\357\251\264\0\351\310\324\0\377\261\274\0"
  "\347\22\24\0J\0\0\0J\306\322\0\376\310\324\0\377\276\311\0\370\272\306\0"
  "\366\202\310\324\0\377\22lr\0\216\0\0\0,X]\0q\307\323\0\377\276\311\0\372"
  "\236\247\0\340\252\265\0\343\304\320\0\372PU\0Y\5\6\0C\251\263\0\336\310"
  "\324\0\377\262\274\0\357\251\264\0\351\310\324\0\377\261\274\0\347\25\26"
  "\0B\0\0\0\16\202\0\0\0\1\33\0\0\0\14\7\7\0""9\251\263\0\336\310\324\0\377"
  "\262\274\0\357\251\264\0\351\310\324\0\377\261\274\0\347\22\24\0J\0\0\0J"
  "\306\322\0\376\310\324\0\377\272\305\0\366\307\323\0\376\275\310\0\356\0"
  "\0\0d\306\322\0\376\310\324\0\377\272\305\0\366\307\323\0\376\275\310\0\356"
  "\0\0\0M\243\254\0\316\310\324\0\377\214\225\0\245\0\0\0\34\0\0\0\4\202\0"
  "\0\0\0\17\0\0\0\3\0\0\0\30{\202\0\200\252\264\0\331\306\322\0\376\303\316"
  "\0\372\236\247\0\316dj\0\214\10\11\0D\0\0\0$\0\0\0\24\0\0\0B\271\304\0\366"
  "\310\324\0\377\217\227\0\341\202u|\0\270\13\212\223\0\336\310\324\0\377\300"
  "\313\0\372\0\0\0L\0\0\0I\271\304\0\366\310\324\0\377nu\0\246\0\0\0.\0\0\0"
  "-EI\0\213\202\310\324\0\377\3RV\0u\0\0\0\23\0\0\0\2\217\0\0\0\0""1\0\0\0"
  "\12\0\0\0F\306\322\0\376\310\324\0\37725\0n\0\0\0U\306\322\0\376\310\324"
  "\0\377-0\0z`f\0\227\310\324\0\377\301\315\0\37448\0\210\0\0\0F\26\30\0=:"
  "=\0Hdj\0\222\310\324\0\377\257\272\0\365\5\5\0\235\0\0\0\226\235\247\0\354"
  "\310\324\0\377t{\0\242\0\0\0`\306\322\0\376\310\324\0\377y\200\0\303\0\0"
  "\0u\224\235\0\336\310\324\0\377\221\232\0\307\0\0\0>v}\0\226\310\324\0\377"
  "\223\234\0\347'*\0\241\3\3\0{\25\26\0f\23\24\0Hci\0\223\310\324\0\377\257"
  "\272\0\365\5\5\0\235\0\0\0\226\235\247\0\354\310\324\0\377y\200\0\233\0\0"
  "\0\33\202\0\0\0\3\33\0\0\0\30io\0\213\310\324\0\377\257\272\0\365\5\5\0\235"
  "\0\0\0\226\235\247\0\354\310\324\0\377t{\0\242\0\0\0`\306\322\0\376\310\324"
  "\0\377\222\233\0\326\14\15\0^-0\0J\0\0\0W\306\322\0\376\310\324\0\377\222"
  "\233\0\326\14\15\0^14\0E\0\0\0""0\217\230\0\244\252\264\0\323}\205\0\201"
  "\0\0\0\26\0\0\0\3\202\0\0\0\0\15\0\0\0\1\0\0\0\10\0\0\0\34\0\0\0""6+-\0a"
  "y\201\0\256\251\263\0\346\307\323\0\377\274\307\0\365\215\225\0\247\0\0\0"
  "'\0\0\0D\271\304\0\366\206\310\324\0\377\11\300\313\0\372\0\0\0L\0\0\0I\271"
  "\304\0\366\310\324\0\377pw\0\243\0\0\0#\0\0\0\27\4\4\0X\202\310\324\0\377"
  "\3kr\0\221\0\0\0\30\0\0\0\3\217\0\0\0\0\21\0\0\0\12\0\0\0F\306\322\0\376"
  "\310\324\0\37725\0o\0\0\0V\306\322\0\376\310\324\0\377+-\0\202}\205\0\272"
  "\310\324\0\377\243\255\0\351\0\0\0L\0\0\0\30\0\0\0\22\0\0\0)\203\212\0\264"
  "\206\310\324\0\377\14\222\233\0\274\0\0\0e\306\322\0\376\310\324\0\3779<"
  "\0~\0\0\0H\203\213\0\304\310\324\0\377\226\237\0\320\0\0\0=58\0Z\275\310"
  "\0\364\202\310\324\0\377\4\302\316\0\373\237\250\0\330\"$\0n~\205\0\273\206"
  "\310\324\0\377\2\226\237\0\270\0\0\0\37\202\0\0\0\4\2\0\0\0!\204\214\0\262"
  "\206\310\324\0\377\23\222\233\0\274\0\0\0e\306\322\0\376\310\324\0\377BF"
  "\0\204\0\0\0$\0\0\0\25\0\0\0I\306\322\0\376\310\324\0\377BF\0\204\0\0\0#"
  "\0\0\0\16\0\0\0\23\0\0\0,\0\0\0""8\0\0\0$\0\0\0\13\0\0\0\1\203\0\0\0\0\33"
  "\0\0\0\4\0\0\0\16\0\0\0\"\0\0\0?\5\6\0fNS\0\255\234\246\0\350\310\324\0\377"
  "\262\274\0\335\0\0\0-\0\0\0E\271\304\0\366\310\324\0\377\202\212\0\327^d"
  "\0\241_d\0\240}\204\0\323\310\324\0\377\300\313\0\372\0\0\0L\0\0\0I\271\304"
  "\0\366\310\324\0\377nu\0\246\0\0\0*\0\0\0$/1\0w\202\310\324\0\377\3^d\0\201"
  "\0\0\0\25\0\0\0\2\217\0\0\0\0\24\0\0\0\12\0\0\0F\306\322\0\376\310\324\0"
  "\37725\0o\0\0\0V\306\322\0\376\310\324\0\377+.\0\200t{\0\255\310\324\0\377"
  "\263\276\0\364\2\2\0_\0\0\0-\0\0\0$\0\0\0""1x\177\0\251\310\324\0\377\253"
  "\265\0\366QV\0\273\202Y_\0\251\26]c\0\242KP\0o\0\0\0]\306\322\0\376\310\324"
  "\0\37736\0m\0\0\0;\206\216\0\277\310\324\0\377\225\236\0\321\0\0\0""9\0\0"
  "\0""0\26\27\0c]c\0\246w~\0\312\254\267\0\363\310\324\0\377\202\212\0\302"
  "nu\0\270\310\324\0\377\253\265\0\366QV\0\273\202Y_\0\251\12]c\0\242PU\0h"
  "\0\0\0\27\0\0\0\3\0\0\0\4\0\0\0\35{\203\0\245\310\324\0\377\253\265\0\366"
  "QV\0\273\202Y_\0\251\24]c\0\242KP\0o\0\0\0]\306\322\0\376\310\324\0\3775"
  "8\0i\0\0\0\24\0\0\0\14\0\0\0F\306\322\0\376\310\324\0\37758\0i\0\0\0\23\0"
  "\0\0\5\0\0\0\25^d\0]mt\0|QU\0J\0\0\0\20\0\0\0\2\202\0\0\0\0!\0\0\0\2\0\0"
  "\0\17\10\10\0""1bh\0\200\230\241\0\305\276\312\0\364\307\323\0\377\252\264"
  "\0\340z\201\0\242(*\0F\0\0\0\33\0\0\0B\271\304\0\366\310\324\0\377jp\0\255"
  "\0\0\0;\0\0\0""9ag\0\244\310\324\0\377\300\313\0\372\0\0\0L\0\0\0I\271\304"
  "\0\366\310\324\0\377ek\0\266\0\0\0V\14\15\0a\230\241\0\333\310\324\0\377"
  "\276\311\0\370\26\27\0H\0\0\0\15\0\0\0\1\217\0\0\0\0""1\0\0\0\11\0\0\0C\306"
  "\322\0\376\310\324\0\37747\0k\0\0\0S\306\322\0\376\310\324\0\37714\0r.0\0"
  "h\302\316\0\372\310\324\0\377\227\240\0\332kr\0\245\221\231\0\257nt\0t14"
  "\0j\303\316\0\373\307\323\0\376sz\0\313LQ\0\242fm\0\257\243\255\0\323gm\0"
  "v\0\0\0V\306\322\0\376\310\324\0\37759\0h\0\0\0""7\210\220\0\274\310\324"
  "\0\377\226\237\0\317\0\0\0<ls\0e\223\234\0\267[`\0\244FJ\0\256\227\240\0"
  "\351\310\324\0\377\210\221\0\276+-\0x\303\316\0\373\307\323\0\376sz\0\313"
  "LQ\0\242fm\0\257\243\255\0\323mt\0o\0\0\0\22\202\0\0\0\2\33\0\0\0\21""8;"
  "\0\\\303\316\0\373\307\323\0\376sz\0\313LQ\0\242fm\0\257\243\255\0\323gm"
  "\0v\0\0\0V\306\322\0\376\310\324\0\3778;\0c\0\0\0\21\0\0\0\13\0\0\0C\306"
  "\322\0\376\310\324\0\3778;\0c\0\0\0\20\0\0\0\6\0\0\0%\244\256\0\314\310\324"
  "\0\377\214\225\0\245\0\0\0\34\0\0\0\4\202\0\0\0\0\40\0\0\0\4\0\0\0\36\256"
  "\270\0\312\310\324\0\377\266\300\0\352\210\220\0\261;>\0e\0\0\0""7\0\0\0"
  "!\0\0\0\17\0\0\0\14\0\0\0>\272\305\0\365\310\324\0\377qx\0\242\0\0\0%\0\0"
  "\0\"ip\0\227\310\324\0\377\300\313\0\372\0\0\0I\0\0\0G\272\305\0\365\310"
  "\324\0\377\253\265\0\362\241\253\0\346\274\310\0\370\310\324\0\377\305\321"
  "\0\376ou\0\231\0\0\0\"\0\0\0\5\220\0\0\0\0\14\0\0\0\7\0\0\0""2\307\323\0"
  "\375\310\324\0\377?C\0X\0\0\0>\307\323\0\375\310\324\0\377@D\0W\0\0\0.]c"
  "\0\204\265\300\0\356\202\310\324\0\377\5\307\323\0\375t{\0u\0\0\0""3af\0"
  "\206\267\302\0\356\203\310\324\0\377\15\270\303\0\347W]\0[\0\0\0@\306\322"
  "\0\376\310\324\0\377AE\0U\0\0\0)\223\234\0\255\310\324\0\377\240\251\0\303"
  "\0\0\0""1mt\0h\303\316\0\367\203\310\324\0\377\5\257\271\0\34359\0Y\0\0\0"
  """3af\0\206\267\302\0\356\203\310\324\0\377\11\270\303\0\347]b\0V\0\0\0\16"
  "\0\0\0\2\0\0\0\1\0\0\0\6\0\0\0\"ag\0\205\267\302\0\356\203\310\324\0\377"
  "\24\270\303\0\347W]\0[\0\0\0@\306\322\0\376\310\324\0\377EI\0Q\0\0\0\14\0"
  "\0\0\10\0\0\0""2\307\323\0\375\310\324\0\377EI\0Q\0\0\0\14\0\0\0\6\0\0\0"
  "#\247\261\0\311\310\324\0\377\220\230\0\241\0\0\0\32\0\0\0\3\202\0\0\0\0"
  "\27\0\0\0\3\0\0\0\30\216\227\0\205[a\0m\0\0\0;\0\0\0&\0\0\0\25\0\0\0\12\0"
  "\0\0\4\0\0\0\2\0\0\0\7\0\0\0.\275\310\0\361\310\324\0\377\200\210\0\217\0"
  "\0\0\32\0\0\0\30x\177\0\205\310\324\0\377\302\315\0\367\0\0\0""7\0\0\0""5"
  "\275\310\0\361\202\310\324\0\377\7\304\320\0\375\272\306\0\364\240\251\0"
  "\327X]\0}\0\0\0)\0\0\0\13\0\0\0\1\220\0\0\0\0\27\0\0\0\3\0\0\0\23\0\0\0""1"
  "\0\0\0""2\0\0\0\32\0\0\0\30\0\0\0""1\0\0\0""2\0\0\0\30\0\0\0\15\0\0\0\35"
  "\0\0\0""78;\0\\MR\0g!#\0B\0\0\0\36\0\0\0\20\0\0\0\36\0\0\0""7:>\0\\LP\0i"
  "')\0Q\0\0\0""1\202\0\0\0\31\"\0\0\0""1\0\0\0""2\0\0\0\31\0\0\0\20\0\0\0#"
  "\0\0\0""3\0\0\0&\0\0\0\23\0\0\0\33\3\3\0""6<@\0\\MR\0j47\0X\0\0\0""2\0\0"
  "\0\27\0\0\0\16\0\0\0\36\0\0\0""7:>\0\\LP\0i')\0Q\0\0\0""1\0\0\0\26\0\0\0"
  "\5\0\0\0\1\0\0\0\0\0\0\0\1\0\0\0\11\0\0\0\35\0\0\0""7:>\0\\LP\0i')\0Q\0\0"
  "\0""1\202\0\0\0\31\21\0\0\0""1\0\0\0""2\0\0\0\27\0\0\0\5\0\0\0\3\0\0\0\23"
  "\0\0\0""1\0\0\0""2\0\0\0\27\0\0\0\5\0\0\0\2\0\0\0\16\0\0\0'\0\0\0""2\0\0"
  "\0!\0\0\0\12\0\0\0\1\202\0\0\0\0\2\0\0\0\1\0\0\0\10\202\0\0\0\22\4\0\0\0"
  "\13\0\0\0\5\0\0\0\2\0\0\0\1\202\0\0\0\0\25\0\0\0\2\0\0\0\22\0\0\0.\0\0\0"
  """5\0\0\0\37\0\0\0\12\0\0\0\11\0\0\0\35\0\0\0""5\0\0\0""0\0\0\0\25\0\0\0"
  "\24\0\0\0""0\0\0\0B\0\0\0D\0\0\0B\0\0\0<\0\0\0.\0\0\0\32\0\0\0\11\0\0\0\2"
  "\222\0\0\0\0\1\0\0\0\3\202\0\0\0\7\2\0\0\0\4\0\0\0\3\202\0\0\0\7\17\0\0\0"
  "\3\0\0\0\2\0\0\0\4\0\0\0\11\0\0\0\16\0\0\0\20\0\0\0\13\0\0\0\5\0\0\0\2\0"
  "\0\0\4\0\0\0\11\0\0\0\16\0\0\0\20\0\0\0\15\0\0\0\10\202\0\0\0\4\202\0\0\0"
  "\7\26\0\0\0\3\0\0\0\2\0\0\0\5\0\0\0\7\0\0\0\5\0\0\0\3\0\0\0\4\0\0\0\11\0"
  "\0\0\16\0\0\0\21\0\0\0\16\0\0\0\10\0\0\0\3\0\0\0\2\0\0\0\4\0\0\0\11\0\0\0"
  "\16\0\0\0\20\0\0\0\15\0\0\0\10\0\0\0\3\0\0\0\1\203\0\0\0\0\7\0\0\0\1\0\0"
  "\0\4\0\0\0\11\0\0\0\16\0\0\0\20\0\0\0\15\0\0\0\10\202\0\0\0\4\202\0\0\0\7"
  "\4\0\0\0\3\0\0\0\1\0\0\0\0\0\0\0\3\202\0\0\0\7\10\0\0\0\3\0\0\0\1\0\0\0\0"
  "\0\0\0\2\0\0\0\5\0\0\0\7\0\0\0\4\0\0\0\1\204\0\0\0\0\1\0\0\0\1\202\0\0\0"
  "\2\1\0\0\0\1\206\0\0\0\0\4\0\0\0\3\0\0\0\7\0\0\0\10\0\0\0\5\202\0\0\0\1\1"
  "\0\0\0\4\202\0\0\0\7\202\0\0\0\3\10\0\0\0\7\0\0\0\11\0\0\0\12\0\0\0\11\0"
  "\0\0\10\0\0\0\6\0\0\0\3\0\0\0\1\236\0\0\0\0\203\0\0\0\1\204\0\0\0\0\203\0"
  "\0\0\1\215\0\0\0\0\203\0\0\0\1\205\0\0\0\0\203\0\0\0\1\211\0\0\0\0\203\0"
  "\0\0\1\377\0\0\0\0\277\0\0\0\0",
};
#endif
#if 0
/* GIMP RGBA C-Source image dump 1-byte-run-length-encoded (LicenseErr444.c) */
static const struct {
  unsigned int 	 width;
  unsigned int 	 height;
  unsigned int 	 bytes_per_pixel; /* 3:RGB, 4:RGBA */ 
  unsigned char	 rle_pixel_data[4825 + 1];
} err444 = {
  128, 16, 4,
  "\213\0\0\0\0\202\0\0\0\1\1\0\0\0\0\204\0\0\0\1\367\0\0\0\0\2\0\0\0\1\0\0"
  "\0\5\202\0\0\0\14\202\0\0\0\6\202\0\0\0\14\2\0\0\0\6\0\0\0\1\311\0\0\0\0"
  "\6\0\0\0\1\0\0\0\3\0\0\0\5\0\0\0\6\0\0\0\4\0\0\0\1\202\0\0\0\0\6\0\0\0\1"
  "\0\0\0\3\0\0\0\5\0\0\0\6\0\0\0\4\0\0\0\1\203\0\0\0\0\6\0\0\0\1\0\0\0\3\0"
  "\0\0\5\0\0\0\6\0\0\0\4\0\0\0\1\226\0\0\0\0\12\0\0\0\3\0\0\0\27KP\0RLP\0T"
  "\22\23\0!\0\0\0\34MR\0PNR\0R\24\25\0\35\0\0\0\5\310\0\0\0\0\7\0\0\0\1\0\0"
  "\0\6\0\0\0\27\0\0\0+\0\0\0.\0\0\0\35\0\0\0\12\202\0\0\0\1\20\0\0\0\6\0\0"
  "\0\27\0\0\0+\0\0\0.\0\0\0\35\0\0\0\12\0\0\0\1\0\0\0\0\0\0\0\1\0\0\0\6\0\0"
  "\0\27\0\0\0+\0\0\0.\0\0\0\35\0\0\0\12\0\0\0\1\225\0\0\0\0\14\0\0\0\7\0\0"
  "\0""4\306\322\0\376\310\324\0\377>B\0Y\0\0\0""9\307\323\0\375\310\324\0\377"
  "JO\0K\0\0\0\13\0\0\0\2\0\0\0\4\202\0\0\0\6\5\0\0\0\5\0\0\0\2\0\0\0\1\0\0"
  "\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\202\0\0\0\1\203\0\0\0\4\202\0"
  "\0\0\5\6\0\0\0\4\0\0\0\1\0\0\0\0\0\0\0\1\0\0\0\3\0\0\0\5\202\0\0\0\6\5\0"
  "\0\0\4\0\0\0\2\0\0\0\1\0\0\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\206"
  "\0\0\0\0\2\0\0\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\202\0\0\0\1\203"
  "\0\0\0\4\3\0\0\0\5\0\0\0\4\0\0\0\3\203\0\0\0\4\3\0\0\0\5\0\0\0\4\0\0\0\3"
  "\202\0\0\0\4\2\0\0\0\3\0\0\0\1\205\0\0\0\0\31\0\0\0\4\0\0\0\32KO\0h\253\265"
  "\0\337\253\265\0\340\211\221\0\231\0\0\0\33\0\0\0\4\0\0\0\5\0\0\0\32KO\0"
  "h\253\265\0\337\253\265\0\340\211\221\0\231\0\0\0\33\0\0\0\3\0\0\0\1\0\0"
  "\0\4\0\0\0\32KO\0h\253\265\0\337\253\265\0\340\211\221\0\231\0\0\0\33\0\0"
  "\0\3\225\0\0\0\0""0\0\0\0\11\0\0\0C\306\322\0\376\310\324\0\37758\0i\0\0"
  "\0?\225\236\0\307\226\237\0\312.1\0E\0\0\0\16\0\0\0\17\0\0\0\37\0\0\0,\0"
  "\0\0.\0\0\0$\0\0\0\22\0\0\0\10\0\0\0\17\0\0\0\37\0\0\0+\0\0\0,\0\0\0!\0\0"
  "\0\20\0\0\0\6\0\0\0\14\0\0\0\36\0\0\0$\0\0\0\40\0\0\0'\0\0\0*\0\0\0\35\0"
  "\0\0\13\0\0\0\4\0\0\0\11\0\0\0\32\0\0\0)\0\0\0/\0\0\0,\0\0\0\40\0\0\0\15"
  "\0\0\0\6\0\0\0\16\0\0\0\37\0\0\0+\0\0\0,\0\0\0!\0\0\0\20\0\0\0\4\204\0\0"
  "\0\0\32\0\0\0\4\0\0\0\16\0\0\0\37\0\0\0+\0\0\0,\0\0\0!\0\0\0\20\0\0\0\6\0"
  "\0\0\14\0\0\0\37\0\0\0$\0\0\0!\0\0\0%\0\0\0\"\0\0\0\31\0\0\0\40\0\0\0$\0"
  "\0\0!\0\0\0%\0\0\0!\0\0\0\25\0\0\0\32\0\0\0\37\0\0\0\25\0\0\0\6\0\0\0\1\203"
  "\0\0\0\0\4\0\0\0\2\0\0\0\21\10\11\0C\255\267\0\346\202\310\324\0\377\6\220"
  "\230\0\302\0\0\0'\0\0\0\7\0\0\0\21\10\11\0C\255\267\0\346\202\310\324\0\377"
  "\7\220\230\0\302\0\0\0'\0\0\0\5\0\0\0\2\0\0\0\21\10\11\0C\255\267\0\346\202"
  "\310\324\0\377\3\220\230\0\302\0\0\0'\0\0\0\5\225\0\0\0\0""1\0\0\0\12\0\0"
  "\0F\306\322\0\376\310\324\0\37736\0l\0\0\0B\217\227\0\305\220\230\0\310)"
  ",\0H\0\0\0\37\14\15\0>z\202\0\241\235\247\0\321\245\257\0\330\233\245\0\276"
  "RV\0N\0\0\0!\22\23\0B~\206\0\244\240\252\0\322\244\256\0\326\210\220\0\260"
  "\40\"\0I\0\0\0\33\0\0\0(\232\243\0\267\224\235\0\302=A\0\202\224\235\0\303"
  "\243\255\0\327}\205\0\237\0\0\0-\0\0\0\22\0\0\0&lr\0\207\234\245\0\315\245"
  "\257\0\331\240\252\0\317\223\234\0\25314\0""5\0\0\0\34\22\23\0A~\206\0\244"
  "\240\252\0\322\244\256\0\326\210\220\0\260\40\"\0I\0\0\0\26\0\0\0\4\202\0"
  "\0\0\0\33\0\0\0\3\0\0\0\24\22\23\0@~\206\0\244\240\252\0\322\244\256\0\326"
  "\210\220\0\260\40\"\0I\0\0\0\33\0\0\0(\232\243\0\267\224\235\0\302>B\0\203"
  "\227\240\0\303\242\254\0\301\0\0\0G\230\241\0\271\224\235\0\302>B\0\203\227"
  "\240\0\303\242\254\0\301\0\0\0<\203\213\0\217\233\245\0\271u|\0o\0\0\0\23"
  "\0\0\0\2\202\0\0\0\0\33\0\0\0\1\0\0\0\11\0\0\0,\205\215\0\262\304\320\0\375"
  "\237\251\0\362\310\324\0\377\214\225\0\307\0\0\0,\0\0\0\17\0\0\0-\205\215"
  "\0\262\304\320\0\375\237\251\0\362\310\324\0\377\214\225\0\307\0\0\0+\0\0"
  "\0\6\0\0\0\11\0\0\0,\205\215\0\262\304\320\0\375\237\251\0\362\310\324\0"
  "\377\214\225\0\307\0\0\0+\0\0\0\6\225\0\0\0\0\35\0\0\0\12\0\0\0F\306\322"
  "\0\376\310\324\0\37736\0m\0\0\0N\306\322\0\376\310\324\0\37769\0g\2\3\0F"
  "\247\261\0\335\310\324\0\377\304\317\0\374\257\272\0\351\301\315\0\366|\203"
  "\0\201\5\5\0I\251\263\0\336\310\324\0\377\262\274\0\357\251\264\0\351\310"
  "\324\0\377\261\274\0\347\22\24\0J\0\0\0J\306\322\0\376\310\324\0\377\276"
  "\311\0\370\272\306\0\366\202\310\324\0\377\22lr\0\216\0\0\0,X]\0q\307\323"
  "\0\377\276\311\0\372\236\247\0\340\252\265\0\343\304\320\0\372PU\0Y\5\6\0"
  "C\251\263\0\336\310\324\0\377\262\274\0\357\251\264\0\351\310\324\0\377\261"
  "\274\0\347\25\26\0B\0\0\0\16\202\0\0\0\1\33\0\0\0\14\7\7\0""9\251\263\0\336"
  "\310\324\0\377\262\274\0\357\251\264\0\351\310\324\0\377\261\274\0\347\22"
  "\24\0J\0\0\0J\306\322\0\376\310\324\0\377\272\305\0\366\307\323\0\376\275"
  "\310\0\356\0\0\0d\306\322\0\376\310\324\0\377\272\305\0\366\307\323\0\376"
  "\275\310\0\356\0\0\0M\243\254\0\316\310\324\0\377\214\225\0\245\0\0\0\34"
  "\0\0\0\4\202\0\0\0\0\33\0\0\0\4\0\0\0\33HL\0q\304\320\0\374y\201\0\312\205"
  "\215\0\336\310\324\0\377\214\225\0\307\0\0\0""0\0\0\0!HL\0q\304\320\0\374"
  "y\201\0\312\205\215\0\336\310\324\0\377\214\225\0\307\0\0\0-\0\0\0\12\0\0"
  "\0\34HL\0q\304\320\0\374y\201\0\312\205\215\0\336\310\324\0\377\214\225\0"
  "\307\0\0\0,\0\0\0\6\225\0\0\0\0""1\0\0\0\12\0\0\0F\306\322\0\376\310\324"
  "\0\37725\0n\0\0\0U\306\322\0\376\310\324\0\377-0\0z`f\0\227\310\324\0\377"
  "\301\315\0\37448\0\210\0\0\0F\26\30\0=:=\0Hdj\0\222\310\324\0\377\257\272"
  "\0\365\5\5\0\235\0\0\0\226\235\247\0\354\310\324\0\377t{\0\242\0\0\0`\306"
  "\322\0\376\310\324\0\377y\200\0\303\0\0\0u\224\235\0\336\310\324\0\377\221"
  "\232\0\307\0\0\0>v}\0\226\310\324\0\377\223\234\0\347'*\0\241\3\3\0{\25\26"
  "\0f\23\24\0Hci\0\223\310\324\0\377\257\272\0\365\5\5\0\235\0\0\0\226\235"
  "\247\0\354\310\324\0\377y\200\0\233\0\0\0\33\202\0\0\0\3""9\0\0\0\30io\0"
  "\213\310\324\0\377\257\272\0\365\5\5\0\235\0\0\0\226\235\247\0\354\310\324"
  "\0\377t{\0\242\0\0\0`\306\322\0\376\310\324\0\377\222\233\0\326\14\15\0^"
  "-0\0J\0\0\0W\306\322\0\376\310\324\0\377\222\233\0\326\14\15\0^14\0E\0\0"
  "\0""0\217\230\0\244\252\264\0\323}\205\0\201\0\0\0\26\0\0\0\3\0\0\0\0\0\0"
  "\0\1\0\0\0\16\11\12\0?\260\273\0\345\255\267\0\351\4\4\0\201\211\221\0\330"
  "\310\324\0\377\211\222\0\313\0\0\0D\10\10\0I\260\273\0\345\255\267\0\351"
  "\4\4\0\201\211\221\0\330\310\324\0\377\211\222\0\313\0\0\0""8\0\0\0\30\11"
  "\11\0@\260\273\0\345\255\267\0\351\4\4\0\201\211\221\0\330\310\324\0\377"
  "\211\222\0\313\0\0\0""6\0\0\0\13\0\0\0\1\224\0\0\0\0\21\0\0\0\12\0\0\0F\306"
  "\322\0\376\310\324\0\37725\0o\0\0\0V\306\322\0\376\310\324\0\377+-\0\202"
  "}\205\0\272\310\324\0\377\243\255\0\351\0\0\0L\0\0\0\30\0\0\0\22\0\0\0)\203"
  "\212\0\264\206\310\324\0\377\14\222\233\0\274\0\0\0e\306\322\0\376\310\324"
  "\0\3779<\0~\0\0\0H\203\213\0\304\310\324\0\377\226\237\0\320\0\0\0=58\0Z"
  "\275\310\0\364\202\310\324\0\377\4\302\316\0\373\237\250\0\330\"$\0n~\205"
  "\0\273\206\310\324\0\377\2\226\237\0\270\0\0\0\37\202\0\0\0\4\2\0\0\0!\204"
  "\214\0\262\206\310\324\0\3771\222\233\0\274\0\0\0e\306\322\0\376\310\324"
  "\0\377BF\0\204\0\0\0$\0\0\0\25\0\0\0I\306\322\0\376\310\324\0\377BF\0\204"
  "\0\0\0#\0\0\0\16\0\0\0\23\0\0\0,\0\0\0""8\0\0\0$\0\0\0\13\0\0\0\1\0\0\0\0"
  "\0\0\0\3\0\0\0\32}\204\0\233\310\324\0\377ek\0\307EI\0\254\225\236\0\347"
  "\310\324\0\377\223\234\0\342DH\0\212rx\0\252\310\324\0\377ek\0\307EI\0\254"
  "\225\236\0\347\310\324\0\377\224\235\0\341NS\0x\0\0\0""3|\203\0\234\310\324"
  "\0\377ek\0\307EI\0\254\225\236\0\347\310\324\0\377\224\235\0\341PU\0u\0\0"
  "\0\31\0\0\0\3\224\0\0\0\0\24\0\0\0\12\0\0\0F\306\322\0\376\310\324\0\377"
  "25\0o\0\0\0V\306\322\0\376\310\324\0\377+.\0\200t{\0\255\310\324\0\377\263"
  "\276\0\364\2\2\0_\0\0\0-\0\0\0$\0\0\0""1x\177\0\251\310\324\0\377\253\265"
  "\0\366QV\0\273\202Y_\0\251\26]c\0\242KP\0o\0\0\0]\306\322\0\376\310\324\0"
  "\37736\0m\0\0\0;\206\216\0\277\310\324\0\377\225\236\0\321\0\0\0""9\0\0\0"
  """0\26\27\0c]c\0\246w~\0\312\254\267\0\363\310\324\0\377\202\212\0\302nu"
  "\0\270\310\324\0\377\253\265\0\366QV\0\273\202Y_\0\251\12]c\0\242PU\0h\0"
  "\0\0\27\0\0\0\3\0\0\0\4\0\0\0\35{\203\0\245\310\324\0\377\253\265\0\366Q"
  "V\0\273\202Y_\0\251\30]c\0\242KP\0o\0\0\0]\306\322\0\376\310\324\0\37758"
  "\0i\0\0\0\24\0\0\0\14\0\0\0F\306\322\0\376\310\324\0\37758\0i\0\0\0\23\0"
  "\0\0\5\0\0\0\25^d\0]mt\0|QU\0J\0\0\0\20\0\0\0\2\0\0\0\0\0\0\0\4\0\0\0\34"
  "\212\223\0\247\206\310\324\0\377\2\257\271\0\351{\202\0\274\206\310\324\0"
  "\377\3\262\275\0\344\0\0\0B\210\220\0\252\206\310\324\0\377\3\263\276\0\343"
  "\0\0\0%\0\0\0\5\224\0\0\0\0""1\0\0\0\11\0\0\0C\306\322\0\376\310\324\0\377"
  "47\0k\0\0\0S\306\322\0\376\310\324\0\37714\0r.0\0h\302\316\0\372\310\324"
  "\0\377\227\240\0\332kr\0\245\221\231\0\257nt\0t14\0j\303\316\0\373\307\323"
  "\0\376sz\0\313LQ\0\242fm\0\257\243\255\0\323gm\0v\0\0\0V\306\322\0\376\310"
  "\324\0\37759\0h\0\0\0""7\210\220\0\274\310\324\0\377\226\237\0\317\0\0\0"
  "<ls\0e\223\234\0\267[`\0\244FJ\0\256\227\240\0\351\310\324\0\377\210\221"
  "\0\276+-\0x\303\316\0\373\307\323\0\376sz\0\313LQ\0\242fm\0\257\243\255\0"
  "\323mt\0o\0\0\0\22\202\0\0\0\2""9\0\0\0\21""8;\0\\\303\316\0\373\307\323"
  "\0\376sz\0\313LQ\0\242fm\0\257\243\255\0\323gm\0v\0\0\0V\306\322\0\376\310"
  "\324\0\3778;\0c\0\0\0\21\0\0\0\13\0\0\0C\306\322\0\376\310\324\0\3778;\0"
  "c\0\0\0\20\0\0\0\6\0\0\0%\244\256\0\314\310\324\0\377\214\225\0\245\0\0\0"
  "\34\0\0\0\4\0\0\0\0\0\0\0\2\0\0\0\21ek\0]\201\210\0\241x\200\0\254qw\0\270"
  "\241\253\0\354\310\324\0\377\240\252\0\351io\0\234QU\0t\177\207\0\243x\200"
  "\0\254qw\0\270\241\253\0\354\310\324\0\377\241\252\0\350pw\0\222\0\0\0-b"
  "h\0_\201\210\0\241x\200\0\254qw\0\270\241\253\0\354\310\324\0\377\241\252"
  "\0\350qx\0\220\0\0\0\34\0\0\0\4\224\0\0\0\0\14\0\0\0\7\0\0\0""2\307\323\0"
  "\375\310\324\0\377?C\0X\0\0\0>\307\323\0\375\310\324\0\377@D\0W\0\0\0.]c"
  "\0\204\265\300\0\356\202\310\324\0\377\5\307\323\0\375t{\0u\0\0\0""3af\0"
  "\206\267\302\0\356\203\310\324\0\377\15\270\303\0\347W]\0[\0\0\0@\306\322"
  "\0\376\310\324\0\377AE\0U\0\0\0)\223\234\0\255\310\324\0\377\240\251\0\303"
  "\0\0\0""1mt\0h\303\316\0\367\203\310\324\0\377\5\257\271\0\34359\0Y\0\0\0"
  """3af\0\206\267\302\0\356\203\310\324\0\377\11\270\303\0\347]b\0V\0\0\0\16"
  "\0\0\0\2\0\0\0\1\0\0\0\6\0\0\0\"ag\0\205\267\302\0\356\203\310\324\0\377"
  "2\270\303\0\347W]\0[\0\0\0@\306\322\0\376\310\324\0\377EI\0Q\0\0\0\14\0\0"
  "\0\10\0\0\0""2\307\323\0\375\310\324\0\377EI\0Q\0\0\0\14\0\0\0\6\0\0\0#\247"
  "\261\0\311\310\324\0\377\220\230\0\241\0\0\0\32\0\0\0\3\0\0\0\0\0\0\0\1\0"
  "\0\0\5\0\0\0\21\0\0\0\35\0\0\0%\0\0\0=\227\240\0\303\310\324\0\377\224\235"
  "\0\275\0\0\0""4\0\0\0\34\0\0\0\36\0\0\0%\0\0\0=\227\240\0\303\310\324\0\377"
  "\224\235\0\274\0\0\0""0\0\0\0\20\0\0\0\22\0\0\0\35\0\0\0%\0\0\0=\227\240"
  "\0\303\310\324\0\377\224\235\0\274\0\0\0/\0\0\0\13\0\0\0\1\224\0\0\0\0\27"
  "\0\0\0\3\0\0\0\23\0\0\0""1\0\0\0""2\0\0\0\32\0\0\0\30\0\0\0""1\0\0\0""2\0"
  "\0\0\30\0\0\0\15\0\0\0\35\0\0\0""78;\0\\MR\0g!#\0B\0\0\0\36\0\0\0\20\0\0"
  "\0\36\0\0\0""7:>\0\\LP\0i')\0Q\0\0\0""1\202\0\0\0\31\"\0\0\0""1\0\0\0""2"
  "\0\0\0\31\0\0\0\20\0\0\0#\0\0\0""3\0\0\0&\0\0\0\23\0\0\0\33\3\3\0""6<@\0"
  "\\MR\0j47\0X\0\0\0""2\0\0\0\27\0\0\0\16\0\0\0\36\0\0\0""7:>\0\\LP\0i')\0"
  "Q\0\0\0""1\0\0\0\26\0\0\0\5\0\0\0\1\0\0\0\0\0\0\0\1\0\0\0\11\0\0\0\35\0\0"
  "\0""7:>\0\\LP\0i')\0Q\0\0\0""1\202\0\0\0\31\21\0\0\0""1\0\0\0""2\0\0\0\27"
  "\0\0\0\5\0\0\0\3\0\0\0\23\0\0\0""1\0\0\0""2\0\0\0\27\0\0\0\5\0\0\0\2\0\0"
  "\0\16\0\0\0'\0\0\0""2\0\0\0!\0\0\0\12\0\0\0\1\202\0\0\0\0\11\0\0\0\1\0\0"
  "\0\2\0\0\0\3\0\0\0\5\0\0\0\20\0\0\0'\0\0\0""4\0\0\0&\0\0\0\16\202\0\0\0\4"
  "\20\0\0\0\5\0\0\0\20\0\0\0'\0\0\0""4\0\0\0&\0\0\0\16\0\0\0\3\0\0\0\2\0\0"
  "\0\3\0\0\0\5\0\0\0\20\0\0\0'\0\0\0""4\0\0\0&\0\0\0\16\0\0\0\2\226\0\0\0\0"
  "\1\0\0\0\3\202\0\0\0\7\2\0\0\0\4\0\0\0\3\202\0\0\0\7\17\0\0\0\3\0\0\0\2\0"
  "\0\0\4\0\0\0\11\0\0\0\16\0\0\0\20\0\0\0\13\0\0\0\5\0\0\0\2\0\0\0\4\0\0\0"
  "\11\0\0\0\16\0\0\0\20\0\0\0\15\0\0\0\10\202\0\0\0\4\202\0\0\0\7\26\0\0\0"
  "\3\0\0\0\2\0\0\0\5\0\0\0\7\0\0\0\5\0\0\0\3\0\0\0\4\0\0\0\11\0\0\0\16\0\0"
  "\0\21\0\0\0\16\0\0\0\10\0\0\0\3\0\0\0\2\0\0\0\4\0\0\0\11\0\0\0\16\0\0\0\20"
  "\0\0\0\15\0\0\0\10\0\0\0\3\0\0\0\1\203\0\0\0\0\7\0\0\0\1\0\0\0\4\0\0\0\11"
  "\0\0\0\16\0\0\0\20\0\0\0\15\0\0\0\10\202\0\0\0\4\202\0\0\0\7\4\0\0\0\3\0"
  "\0\0\1\0\0\0\0\0\0\0\3\202\0\0\0\7\10\0\0\0\3\0\0\0\1\0\0\0\0\0\0\0\2\0\0"
  "\0\5\0\0\0\7\0\0\0\4\0\0\0\1\207\0\0\0\0\5\0\0\0\2\0\0\0\5\0\0\0\7\0\0\0"
  "\5\0\0\0\2\203\0\0\0\0\5\0\0\0\2\0\0\0\5\0\0\0\7\0\0\0\5\0\0\0\2\204\0\0"
  "\0\0\5\0\0\0\2\0\0\0\5\0\0\0\7\0\0\0\5\0\0\0\2\242\0\0\0\0\203\0\0\0\1\204"
  "\0\0\0\0\203\0\0\0\1\215\0\0\0\0\203\0\0\0\1\205\0\0\0\0\203\0\0\0\1\211"
  "\0\0\0\0\203\0\0\0\1\377\0\0\0\0\276\0\0\0\0",
};
#endif
#if 0
/* GIMP RGBA C-Source image dump 1-byte-run-length-encoded (LicenseErrRAW.c) */
static const struct {
  unsigned int 	 width;
  unsigned int 	 height;
  unsigned int 	 bytes_per_pixel; /* 3:RGB, 4:RGBA */ 
  unsigned char	 rle_pixel_data[5198 + 1];
} errRAW = {
  128, 16, 4,
  "\211\0\0\0\0\202\0\0\0\1\1\0\0\0\0\204\0\0\0\1\367\0\0\0\0\2\0\0\0\1\0\0"
  "\0\5\202\0\0\0\14\202\0\0\0\6\202\0\0\0\14\2\0\0\0\6\0\0\0\1\307\0\0\0\0"
  "\3\0\0\0\2\0\0\0\5\0\0\0\6\202\0\0\0\7\3\0\0\0\6\0\0\0\4\0\0\0\2\204\0\0"
  "\0\0\24\0\0\0\2\0\0\0\5\0\0\0\6\0\0\0\5\0\0\0\3\0\0\0\2\0\0\0\4\0\0\0\5\0"
  "\0\0\4\0\0\0\2\0\0\0\1\0\0\0\4\0\0\0\6\0\0\0\4\0\0\0\2\0\0\0\1\0\0\0\3\0"
  "\0\0\5\0\0\0\4\0\0\0\2\217\0\0\0\0\12\0\0\0\3\0\0\0\27KP\0RLP\0T\22\23\0"
  "!\0\0\0\34MR\0PNR\0R\24\25\0\35\0\0\0\5\306\0\0\0\0\12\0\0\0\2\0\0\0\16\0"
  "\0\0%\0\0\0""4\0\0\0""6\0\0\0""5\0\0\0/\0\0\0!\0\0\0\16\0\0\0\3\202\0\0\0"
  "\0\26\0\0\0\2\0\0\0\17\0\0\0%\0\0\0""2\0\0\0+\0\0\0\25\0\0\0\16\0\0\0\34"
  "\0\0\0)\0\0\0\37\0\0\0\14\0\0\0\13\0\0\0\36\0\0\0,\0\0\0#\0\0\0\16\0\0\0"
  "\11\0\0\0\31\0\0\0(\0\0\0!\0\0\0\14\0\0\0\2\216\0\0\0\0\14\0\0\0\7\0\0\0"
  """4\306\322\0\376\310\324\0\377>B\0Y\0\0\0""9\307\323\0\375\310\324\0\377"
  "JO\0K\0\0\0\13\0\0\0\2\0\0\0\4\202\0\0\0\6\5\0\0\0\5\0\0\0\2\0\0\0\1\0\0"
  "\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\202\0\0\0\1\203\0\0\0\4\202\0"
  "\0\0\5\6\0\0\0\4\0\0\0\1\0\0\0\0\0\0\0\1\0\0\0\3\0\0\0\5\202\0\0\0\6\5\0"
  "\0\0\4\0\0\0\2\0\0\0\1\0\0\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\206"
  "\0\0\0\0\2\0\0\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\202\0\0\0\0\1\0"
  "\0\0\1\203\0\0\0\4\3\0\0\0\5\0\0\0\4\0\0\0\3\203\0\0\0\4\204\0\0\0\5\2\0"
  "\0\0\3\0\0\0\1\203\0\0\0\0\"\0\0\0\5\0\0\0'\246\257\0\316\251\263\0\343\250"
  "\262\0\344\250\262\0\342\240\251\0\331\203\213\0\254\26\30\0=\0\0\0\20\0"
  "\0\0\2\0\0\0\1\0\0\0\11\0\0\0/\237\250\0\312\251\263\0\343\254\266\0\336"
  "6:\0T\0\0\0'\216\226\0\225\256\271\0\334\214\224\0\244\0\0\0%\0\0\0#\206"
  "\216\0\237\254\266\0\337\234\245\0\301\0\0\0+\0\0\0\36qx\0}\256\271\0\334"
  "\240\252\0\267\0\0\0\37\0\0\0\4\216\0\0\0\0""0\0\0\0\11\0\0\0C\306\322\0"
  "\376\310\324\0\37758\0i\0\0\0?\225\236\0\307\226\237\0\312.1\0E\0\0\0\16"
  "\0\0\0\17\0\0\0\37\0\0\0,\0\0\0.\0\0\0$\0\0\0\22\0\0\0\10\0\0\0\17\0\0\0"
  "\37\0\0\0+\0\0\0,\0\0\0!\0\0\0\20\0\0\0\6\0\0\0\14\0\0\0\36\0\0\0$\0\0\0"
  "\40\0\0\0'\0\0\0*\0\0\0\35\0\0\0\13\0\0\0\4\0\0\0\11\0\0\0\32\0\0\0)\0\0"
  "\0/\0\0\0,\0\0\0\40\0\0\0\15\0\0\0\6\0\0\0\16\0\0\0\37\0\0\0+\0\0\0,\0\0"
  "\0!\0\0\0\20\0\0\0\4\204\0\0\0\0\32\0\0\0\4\0\0\0\16\0\0\0\37\0\0\0+\0\0"
  "\0,\0\0\0!\0\0\0\20\0\0\0\4\0\0\0\2\0\0\0\14\0\0\0\36\0\0\0$\0\0\0!\0\0\0"
  "%\0\0\0\"\0\0\0\31\0\0\0\40\0\0\0$\0\0\0!\0\0\0&\0\0\0)\0\0\0%\0\0\0!\0\0"
  "\0\25\0\0\0\6\0\0\0\1\202\0\0\0\0\6\0\0\0\10\0\0\0:\272\306\0\364\310\324"
  "\0\377\276\311\0\372\274\310\0\371\202\310\324\0\377\6\233\244\0\310\0\0"
  "\0'\0\0\0\5\0\0\0\3\0\0\0\27HL\0t\203\310\324\0\377\21\177\207\0\263\0\0"
  "\0?sy\0\231\310\324\0\377\260\273\0\352\0\0\0A\0\0\0?\253\265\0\345\310\324"
  "\0\377\304\317\0\374\17\17\0P\0\0\0""9\224\234\0\310\310\324\0\377\224\235"
  "\0\302\0\0\0%\0\0\0\5\216\0\0\0\0""1\0\0\0\12\0\0\0F\306\322\0\376\310\324"
  "\0\37736\0l\0\0\0B\217\227\0\305\220\230\0\310),\0H\0\0\0\37\14\15\0>z\202"
  "\0\241\235\247\0\321\245\257\0\330\233\245\0\276RV\0N\0\0\0!\22\23\0B~\206"
  "\0\244\240\252\0\322\244\256\0\326\210\220\0\260\40\"\0I\0\0\0\33\0\0\0("
  "\232\243\0\267\224\235\0\302=A\0\202\224\235\0\303\243\255\0\327}\205\0\237"
  "\0\0\0-\0\0\0\22\0\0\0&lr\0\207\234\245\0\315\245\257\0\331\240\252\0\317"
  "\223\234\0\25314\0""5\0\0\0\34\22\23\0A~\206\0\244\240\252\0\322\244\256"
  "\0\326\210\220\0\260\40\"\0I\0\0\0\26\0\0\0\4\202\0\0\0\0\33\0\0\0\3\0\0"
  "\0\24\22\23\0@~\206\0\244\240\252\0\322\244\256\0\326\210\220\0\260\40\""
  "\0I\0\0\0\26\0\0\0\11\0\0\0%\232\243\0\267\224\235\0\302>B\0\203\227\240"
  "\0\303\242\254\0\301\0\0\0G\230\241\0\271\224\235\0\302>A\0\204\227\240\0"
  "\304\234\245\0\311t{\0\242\232\243\0\273u|\0o\0\0\0\23\0\0\0\2\202\0\0\0"
  "\0\"\0\0\0\11\0\0\0@\271\304\0\366\310\324\0\377`f\0\276\0\0\0|\177\207\0"
  "\325\310\324\0\377\267\302\0\360\0\0\0""5\0\0\0\7\0\0\0\6\0\0\0+\213\224"
  "\0\304\310\324\0\377\253\265\0\367\310\324\0\377\260\272\0\355\0\0\0Q03\0"
  "p\310\324\0\377\307\323\0\376\35\36\0g\16\17\0^\304\317\0\375\267\302\0\372"
  "\310\324\0\377TY\0\214\0\0\0V\257\272\0\356\310\324\0\377ms\0\232\0\0\0\35"
  "\0\0\0\3\216\0\0\0\0\35\0\0\0\12\0\0\0F\306\322\0\376\310\324\0\37736\0m"
  "\0\0\0N\306\322\0\376\310\324\0\37769\0g\2\3\0F\247\261\0\335\310\324\0\377"
  "\304\317\0\374\257\272\0\351\301\315\0\366|\203\0\201\5\5\0I\251\263\0\336"
  "\310\324\0\377\262\274\0\357\251\264\0\351\310\324\0\377\261\274\0\347\22"
  "\24\0J\0\0\0J\306\322\0\376\310\324\0\377\276\311\0\370\272\306\0\366\202"
  "\310\324\0\377\22lr\0\216\0\0\0,X]\0q\307\323\0\377\276\311\0\372\236\247"
  "\0\340\252\265\0\343\304\320\0\372PU\0Y\5\6\0C\251\263\0\336\310\324\0\377"
  "\262\274\0\357\251\264\0\351\310\324\0\377\261\274\0\347\25\26\0B\0\0\0\16"
  "\202\0\0\0\1\33\0\0\0\14\7\7\0""9\251\263\0\336\310\324\0\377\262\274\0\357"
  "\251\264\0\351\310\324\0\377\261\274\0\347\25\26\0B\0\0\0\26\0\0\0=\306\322"
  "\0\376\310\324\0\377\272\305\0\366\307\323\0\376\275\310\0\356\0\0\0d\306"
  "\322\0\376\310\324\0\377\272\305\0\366\307\323\0\376\271\305\0\362\232\244"
  "\0\331\310\324\0\377\214\225\0\245\0\0\0\34\0\0\0\4\202\0\0\0\0\"\0\0\0\11"
  "\0\0\0@\271\304\0\366\310\324\0\377`f\0\276\0\0\0}~\205\0\323\310\324\0\377"
  "\247\261\0\335\0\0\0.\0\0\0\7\0\0\0\16\5\6\0F\272\306\0\365\310\324\0\377"
  "BF\0\275\274\310\0\372\310\324\0\377HL\0\205\0\0\0Y\266\301\0\363\310\324"
  "\0\377Y^\0\244OS\0\232\310\324\0\377kq\0\326\310\324\0\377~\205\0\276\31"
  "\32\0\177\307\323\0\376\310\324\0\377.1\0c\0\0\0\22\0\0\0\2\216\0\0\0\0""1"
  "\0\0\0\12\0\0\0F\306\322\0\376\310\324\0\37725\0n\0\0\0U\306\322\0\376\310"
  "\324\0\377-0\0z`f\0\227\310\324\0\377\301\315\0\37448\0\210\0\0\0F\26\30"
  "\0=:=\0Hdj\0\222\310\324\0\377\257\272\0\365\5\5\0\235\0\0\0\226\235\247"
  "\0\354\310\324\0\377t{\0\242\0\0\0`\306\322\0\376\310\324\0\377y\200\0\303"
  "\0\0\0u\224\235\0\336\310\324\0\377\221\232\0\307\0\0\0>v}\0\226\310\324"
  "\0\377\223\234\0\347'*\0\241\3\3\0{\25\26\0f\23\24\0Hci\0\223\310\324\0\377"
  "\257\272\0\365\5\5\0\235\0\0\0\226\235\247\0\354\310\324\0\377y\200\0\233"
  "\0\0\0\33\202\0\0\0\3\33\0\0\0\30io\0\213\310\324\0\377\257\272\0\365\5\5"
  "\0\235\0\0\0\226\235\247\0\354\310\324\0\377x\177\0\234\0\0\0%\0\0\0H\306"
  "\322\0\376\310\324\0\377\222\233\0\326\14\15\0^-0\0J\0\0\0W\306\322\0\376"
  "\310\324\0\377\222\232\0\327\14\15\0a$&\0]\211\221\0\254\251\264\0\324}\205"
  "\0\201\0\0\0\26\0\0\0\3\202\0\0\0\0\"\0\0\0\11\0\0\0@\271\304\0\366\310\324"
  "\0\377\274\307\0\372\271\304\0\370\310\324\0\377\251\263\0\357<?\0{\0\0\0"
  "\36\0\0\0\7\0\0\0\35`f\0\221\310\324\0\377\267\302\0\366\1\1\0\206\215\225"
  "\0\332\310\324\0\377\214\225\0\313\0\0\0^\226\237\0\327\310\324\0\377~\206"
  "\0\324w~\0\313\310\324\0\377!#\0\232\267\302\0\364\236\250\0\343QV\0\270"
  "\310\324\0\377\264\277\0\362\0\0\0@\0\0\0\12\0\0\0\1\216\0\0\0\0\21\0\0\0"
  "\12\0\0\0F\306\322\0\376\310\324\0\37725\0o\0\0\0V\306\322\0\376\310\324"
  "\0\377+-\0\202}\205\0\272\310\324\0\377\243\255\0\351\0\0\0L\0\0\0\30\0\0"
  "\0\22\0\0\0)\203\212\0\264\206\310\324\0\377\14\222\233\0\274\0\0\0e\306"
  "\322\0\376\310\324\0\3779<\0~\0\0\0H\203\213\0\304\310\324\0\377\226\237"
  "\0\320\0\0\0=58\0Z\275\310\0\364\202\310\324\0\377\4\302\316\0\373\237\250"
  "\0\330\"$\0n~\205\0\273\206\310\324\0\377\2\226\237\0\270\0\0\0\37\202\0"
  "\0\0\4\2\0\0\0!\204\214\0\262\206\310\324\0\377\23\226\237\0\270\0\0\0)\0"
  "\0\0J\306\322\0\376\310\324\0\377BF\0\204\0\0\0$\0\0\0\25\0\0\0I\306\322"
  "\0\376\310\324\0\377BF\0\204\0\0\0%\0\0\0\33\0\0\0/\0\0\0""9\0\0\0$\0\0\0"
  "\13\0\0\0\1\202\0\0\0\0!\0\0\0\11\0\0\0@\271\304\0\366\310\324\0\377\250"
  "\262\0\362\252\264\0\360\310\324\0\377\273\306\0\371@D\0\202\0\0\0\40\0\0"
  "\0\14\0\0\0""3\231\242\0\327\310\324\0\377\224\235\0\351AE\0\254ms\0\321"
  "\310\324\0\377\273\307\0\370\7\7\0lpw\0\263\310\324\0\377\241\252\0\362\232"
  "\243\0\353\267\302\0\362\0\0\0n\227\240\0\327\276\311\0\372z\201\0\342\310"
  "\324\0\377\226\237\0\323\0\0\0""0\0\0\0\6\217\0\0\0\0\24\0\0\0\12\0\0\0F"
  "\306\322\0\376\310\324\0\37725\0o\0\0\0V\306\322\0\376\310\324\0\377+.\0"
  "\200t{\0\255\310\324\0\377\263\276\0\364\2\2\0_\0\0\0-\0\0\0$\0\0\0""1x\177"
  "\0\251\310\324\0\377\253\265\0\366QV\0\273\202Y_\0\251\26]c\0\242KP\0o\0"
  "\0\0]\306\322\0\376\310\324\0\37736\0m\0\0\0;\206\216\0\277\310\324\0\377"
  "\225\236\0\321\0\0\0""9\0\0\0""0\26\27\0c]c\0\246w~\0\312\254\267\0\363\310"
  "\324\0\377\202\212\0\302nu\0\270\310\324\0\377\253\265\0\366QV\0\273\202"
  "Y_\0\251\12]c\0\242PU\0h\0\0\0\27\0\0\0\3\0\0\0\4\0\0\0\35{\203\0\245\310"
  "\324\0\377\253\265\0\366QV\0\273\202Y_\0\251\24]c\0\242PU\0h\0\0\0!\0\0\0"
  "I\306\322\0\376\310\324\0\37758\0i\0\0\0\24\0\0\0\14\0\0\0F\306\322\0\376"
  "\310\324\0\37758\0i\0\0\0\26\0\0\0\30]c\0^mt\0|QU\0J\0\0\0\20\0\0\0\2\202"
  "\0\0\0\0\15\0\0\0\11\0\0\0@\271\304\0\366\310\324\0\377ci\0\271\0\0\0p\233"
  "\245\0\336\310\324\0\377\251\263\0\345\0\0\0>\0\0\0\35\35\36\0Y\303\316\0"
  "\374\206\310\324\0\377\16\\b\0\2468<\0\215\310\324\0\377\303\316\0\376\276"
  "\311\0\374\230\241\0\325\0\0\0Rt{\0\255\310\324\0\377\270\304\0\374\310\324"
  "\0\377t{\0\250\0\0\0!\0\0\0\4\217\0\0\0\0""1\0\0\0\11\0\0\0C\306\322\0\376"
  "\310\324\0\37747\0k\0\0\0S\306\322\0\376\310\324\0\37714\0r.0\0h\302\316"
  "\0\372\310\324\0\377\227\240\0\332kr\0\245\221\231\0\257nt\0t14\0j\303\316"
  "\0\373\307\323\0\376sz\0\313LQ\0\242fm\0\257\243\255\0\323gm\0v\0\0\0V\306"
  "\322\0\376\310\324\0\37759\0h\0\0\0""7\210\220\0\274\310\324\0\377\226\237"
  "\0\317\0\0\0<ls\0e\223\234\0\267[`\0\244FJ\0\256\227\240\0\351\310\324\0"
  "\377\210\221\0\276+-\0x\303\316\0\373\307\323\0\376sz\0\313LQ\0\242fm\0\257"
  "\243\255\0\323mt\0o\0\0\0\22\202\0\0\0\2\33\0\0\0\21""8;\0\\\303\316\0\373"
  "\307\323\0\376sz\0\313LQ\0\242fm\0\257\243\255\0\323mt\0o\0\0\0\34\0\0\0"
  "E\306\322\0\376\310\324\0\3778;\0c\0\0\0\21\0\0\0\13\0\0\0C\306\322\0\376"
  "\310\324\0\3778;\0d\0\0\0\25\0\0\0'\244\256\0\314\310\324\0\377\214\225\0"
  "\245\0\0\0\34\0\0\0\4\202\0\0\0\0\26\0\0\0\11\0\0\0>\272\305\0\365\310\324"
  "\0\377pv\0\244\0\0\0<?C\0z\307\323\0\376\310\324\0\377_e\0\214\0\0\0:v}\0"
  "\246\310\324\0\377\265\300\0\366ov\0\272x\177\0\255sz\0\264\233\244\0\345"
  "\310\324\0\377\234\245\0\334\0\0\0t\274\310\0\370\202\310\324\0\377\3w~\0"
  "\253\0\0\0""7CG\0v\203\310\324\0\377\3CG\0q\0\0\0\24\0\0\0\2\217\0\0\0\0"
  "\14\0\0\0\7\0\0\0""2\307\323\0\375\310\324\0\377?C\0X\0\0\0>\307\323\0\375"
  "\310\324\0\377@D\0W\0\0\0.]c\0\204\265\300\0\356\202\310\324\0\377\5\307"
  "\323\0\375t{\0u\0\0\0""3af\0\206\267\302\0\356\203\310\324\0\377\15\270\303"
  "\0\347W]\0[\0\0\0@\306\322\0\376\310\324\0\377AE\0U\0\0\0)\223\234\0\255"
  "\310\324\0\377\240\251\0\303\0\0\0""1mt\0h\303\316\0\367\203\310\324\0\377"
  "\5\257\271\0\34359\0Y\0\0\0""3af\0\206\267\302\0\356\203\310\324\0\377\11"
  "\270\303\0\347]b\0V\0\0\0\16\0\0\0\2\0\0\0\1\0\0\0\6\0\0\0\"ag\0\205\267"
  "\302\0\356\203\310\324\0\377\24\270\303\0\347]b\0V\0\0\0\25\0\0\0""4\307"
  "\323\0\375\310\324\0\377EI\0Q\0\0\0\14\0\0\0\10\0\0\0""2\307\323\0\375\310"
  "\324\0\377EI\0Q\0\0\0\21\0\0\0$\247\261\0\311\310\324\0\377\220\230\0\241"
  "\0\0\0\32\0\0\0\3\202\0\0\0\0\26\0\0\0\6\0\0\0.\275\310\0\361\310\324\0\377"
  "\177\207\0\220\0\0\0!\0\0\0""4\241\253\0\317\310\324\0\377\261\274\0\331"
  "\0\0\0A\261\273\0\332\310\324\0\377\214\224\0\270\0\0\0=\0\0\0(\0\0\0""2"
  "SX\0\177\310\324\0\377\305\321\0\374#%\0f\243\255\0\325\202\310\324\0\377"
  "\11PU\0h\0\0\0\35\0\0\0""9\277\312\0\365\310\324\0\377\276\312\0\364\0\0"
  "\0""7\0\0\0\12\0\0\0\1\217\0\0\0\0\27\0\0\0\3\0\0\0\23\0\0\0""1\0\0\0""2"
  "\0\0\0\32\0\0\0\30\0\0\0""1\0\0\0""2\0\0\0\30\0\0\0\15\0\0\0\35\0\0\0""7"
  "8;\0\\MR\0g!#\0B\0\0\0\36\0\0\0\20\0\0\0\36\0\0\0""7:>\0\\LP\0i')\0Q\0\0"
  "\0""1\202\0\0\0\31""5\0\0\0""1\0\0\0""2\0\0\0\31\0\0\0\20\0\0\0#\0\0\0""3"
  "\0\0\0&\0\0\0\23\0\0\0\33\3\3\0""6<@\0\\MR\0j47\0X\0\0\0""2\0\0\0\27\0\0"
  "\0\16\0\0\0\36\0\0\0""7:>\0\\LP\0i')\0Q\0\0\0""1\0\0\0\26\0\0\0\5\0\0\0\1"
  "\0\0\0\0\0\0\0\1\0\0\0\11\0\0\0\35\0\0\0""7:>\0\\LP\0i')\0Q\0\0\0""1\0\0"
  "\0\26\0\0\0\10\0\0\0\24\0\0\0""1\0\0\0""2\0\0\0\27\0\0\0\5\0\0\0\3\0\0\0"
  "\23\0\0\0""1\0\0\0""2\0\0\0\27\0\0\0\6\0\0\0\16\0\0\0'\0\0\0""2\0\0\0!\0"
  "\0\0\12\0\0\0\1\202\0\0\0\0\40\0\0\0\2\0\0\0\22\0\0\0.\0\0\0""5\0\0\0\37"
  "\0\0\0\13\0\0\0\21\0\0\0*\0\0\0""8\0\0\0+\0\0\0\35\0\0\0+\0\0\0""5\0\0\0"
  "&\0\0\0\17\0\0\0\6\0\0\0\12\0\0\0\35\0\0\0""5\0\0\0""2\0\0\0#\0\0\0.\0\0"
  "\0>\0\0\0""6\0\0\0\32\0\0\0\11\0\0\0\24\0\0\0""1\0\0\0?\0\0\0""1\0\0\0\23"
  "\0\0\0\3\221\0\0\0\0\1\0\0\0\3\202\0\0\0\7\2\0\0\0\4\0\0\0\3\202\0\0\0\7"
  "\17\0\0\0\3\0\0\0\2\0\0\0\4\0\0\0\11\0\0\0\16\0\0\0\20\0\0\0\13\0\0\0\5\0"
  "\0\0\2\0\0\0\4\0\0\0\11\0\0\0\16\0\0\0\20\0\0\0\15\0\0\0\10\202\0\0\0\4\202"
  "\0\0\0\7\26\0\0\0\3\0\0\0\2\0\0\0\5\0\0\0\7\0\0\0\5\0\0\0\3\0\0\0\4\0\0\0"
  "\11\0\0\0\16\0\0\0\21\0\0\0\16\0\0\0\10\0\0\0\3\0\0\0\2\0\0\0\4\0\0\0\11"
  "\0\0\0\16\0\0\0\20\0\0\0\15\0\0\0\10\0\0\0\3\0\0\0\1\203\0\0\0\0\12\0\0\0"
  "\1\0\0\0\4\0\0\0\11\0\0\0\16\0\0\0\20\0\0\0\15\0\0\0\10\0\0\0\3\0\0\0\1\0"
  "\0\0\3\202\0\0\0\7\4\0\0\0\3\0\0\0\1\0\0\0\0\0\0\0\3\202\0\0\0\7\7\0\0\0"
  "\3\0\0\0\1\0\0\0\2\0\0\0\5\0\0\0\7\0\0\0\4\0\0\0\1\204\0\0\0\0\4\0\0\0\3"
  "\0\0\0\7\0\0\0\10\0\0\0\5\202\0\0\0\2\13\0\0\0\6\0\0\0\10\0\0\0\6\0\0\0\4"
  "\0\0\0\6\0\0\0\7\0\0\0\5\0\0\0\2\0\0\0\0\0\0\0\1\0\0\0\4\202\0\0\0\7\13\0"
  "\0\0\5\0\0\0\6\0\0\0\11\0\0\0\10\0\0\0\4\0\0\0\1\0\0\0\3\0\0\0\7\0\0\0\11"
  "\0\0\0\7\0\0\0\3\235\0\0\0\0\203\0\0\0\1\204\0\0\0\0\203\0\0\0\1\215\0\0"
  "\0\0\203\0\0\0\1\205\0\0\0\0\203\0\0\0\1\211\0\0\0\0\203\0\0\0\1\377\0\0"
  "\0\0\300\0\0\0\0",
};
#endif
#if 0
/* GIMP RGBA C-Source image dump 1-byte-run-length-encoded (LicenseErr3D.c) */
static const struct {
  unsigned int 	 width;
  unsigned int 	 height;
  unsigned int 	 bytes_per_pixel; /* 3:RGB, 4:RGBA */ 
  unsigned char	 rle_pixel_data[4598 + 1];
} err3D = {
  128, 16, 4,
  "\214\0\0\0\0\202\0\0\0\1\1\0\0\0\0\204\0\0\0\1\367\0\0\0\0\2\0\0\0\1\0\0"
  "\0\5\202\0\0\0\14\202\0\0\0\6\202\0\0\0\14\2\0\0\0\6\0\0\0\1\307\0\0\0\0"
  "\14\0\0\0\1\0\0\0\4\0\0\0\7\0\0\0\11\0\0\0\10\0\0\0\6\0\0\0\3\0\0\0\1\0\0"
  "\0\0\0\0\0\2\0\0\0\5\0\0\0\6\202\0\0\0\7\3\0\0\0\6\0\0\0\4\0\0\0\2\236\0"
  "\0\0\0\12\0\0\0\3\0\0\0\27PR\0RQR\0T\23\23\0!\0\0\0\34RT\0PST\0R\26\26\0"
  "\35\0\0\0\5\306\0\0\0\0\24\0\0\0\1\0\0\0\13\0\0\0!\0\0\0""3\0\0\0;\0\0\0"
  """9\0\0\0.\0\0\0\31\0\0\0\10\0\0\0\3\0\0\0\16\0\0\0%\0\0\0""3\0\0\0""6\0"
  "\0\0""4\0\0\0-\0\0\0!\0\0\0\20\0\0\0\5\0\0\0\1\234\0\0\0\0\14\0\0\0\7\0\0"
  "\0""4\324\327\0\376\326\331\0\377CD\0Y\0\0\0""9\325\330\0\375\326\331\0\377"
  "OQ\0K\0\0\0\13\0\0\0\2\0\0\0\4\202\0\0\0\6\5\0\0\0\5\0\0\0\2\0\0\0\1\0\0"
  "\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\202\0\0\0\1\203\0\0\0\4\202\0"
  "\0\0\5\6\0\0\0\4\0\0\0\1\0\0\0\0\0\0\0\1\0\0\0\3\0\0\0\5\202\0\0\0\6\5\0"
  "\0\0\4\0\0\0\2\0\0\0\1\0\0\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\206"
  "\0\0\0\0\2\0\0\0\2\0\0\0\4\202\0\0\0\6\2\0\0\0\4\0\0\0\2\202\0\0\0\0\1\0"
  "\0\0\1\203\0\0\0\4\3\0\0\0\5\0\0\0\4\0\0\0\3\203\0\0\0\4\204\0\0\0\5\2\0"
  "\0\0\3\0\0\0\1\203\0\0\0\0\25\0\0\0\4\0\0\0\37\243\245\0\256\276\301\0\346"
  "\310\313\0\363\304\306\0\361\256\261\0\331ab\0y\0\0\0\36\0\0\0\12\0\0\0("
  "\261\264\0\316\264\266\0\344\263\265\0\344\257\262\0\340\246\250\0\322\207"
  "\211\0\252((\0P\0\0\0\36\0\0\0\7\0\0\0\1\233\0\0\0\0""0\0\0\0\11\0\0\0C\324"
  "\327\0\376\326\331\0\37799\0i\0\0\0?\240\242\0\307\241\243\0\31212\0E\0\0"
  "\0\16\0\0\0\17\0\0\0\37\0\0\0,\0\0\0.\0\0\0$\0\0\0\22\0\0\0\10\0\0\0\17\0"
  "\0\0\37\0\0\0+\0\0\0,\0\0\0!\0\0\0\20\0\0\0\6\0\0\0\14\0\0\0\36\0\0\0$\0"
  "\0\0\40\0\0\0'\0\0\0*\0\0\0\35\0\0\0\13\0\0\0\4\0\0\0\11\0\0\0\32\0\0\0)"
  "\0\0\0/\0\0\0,\0\0\0\40\0\0\0\15\0\0\0\6\0\0\0\16\0\0\0\37\0\0\0+\0\0\0,"
  "\0\0\0!\0\0\0\20\0\0\0\4\204\0\0\0\0\32\0\0\0\4\0\0\0\16\0\0\0\37\0\0\0+"
  "\0\0\0,\0\0\0!\0\0\0\20\0\0\0\4\0\0\0\2\0\0\0\14\0\0\0\36\0\0\0$\0\0\0!\0"
  "\0\0%\0\0\0\"\0\0\0\31\0\0\0\40\0\0\0$\0\0\0!\0\0\0&\0\0\0)\0\0\0%\0\0\0"
  "!\0\0\0\25\0\0\0\6\0\0\0\1\202\0\0\0\0\17\0\0\0\5\0\0\0\"\302\305\0\334\265"
  "\267\0\335\246\251\0\332\306\311\0\367\326\331\0\377\317\322\0\372''\0G\0"
  "\0\0\23\0\0\0;\307\312\0\364\326\331\0\377\321\324\0\375\322\325\0\375\202"
  "\326\331\0\377\4\307\312\0\364QS\0s\0\0\0\33\0\0\0\4\233\0\0\0\0""1\0\0\0"
  "\12\0\0\0F\324\327\0\376\326\331\0\37778\0l\0\0\0B\231\233\0\305\232\234"
  "\0\310,-\0H\0\0\0\37\15\16\0>\203\205\0\241\250\253\0\321\261\263\0\330\246"
  "\251\0\276WY\0N\0\0\0!\23\23\0B\207\211\0\244\254\256\0\322\260\262\0\326"
  "\221\223\0\260##\0I\0\0\0\33\0\0\0(\244\247\0\267\236\241\0\302AB\0\202\237"
  "\241\0\303\257\261\0\327\206\210\0\237\0\0\0-\0\0\0\22\0\0\0&su\0\207\247"
  "\251\0\315\261\264\0\331\253\256\0\317\235\237\0\25345\0""5\0\0\0\34\23\24"
  "\0A\207\211\0\244\254\256\0\322\260\262\0\326\221\223\0\260##\0I\0\0\0\26"
  "\0\0\0\4\202\0\0\0\0\33\0\0\0\3\0\0\0\24\24\24\0@\207\211\0\244\254\256\0"
  "\322\260\262\0\326\221\223\0\260##\0I\0\0\0\26\0\0\0\11\0\0\0%\244\247\0"
  "\267\236\241\0\302BC\0\203\242\244\0\303\256\260\0\301\0\0\0G\243\245\0\271"
  "\236\241\0\302BC\0\204\241\243\0\304\247\251\0\311|}\0\242\244\247\0\273"
  "}\177\0o\0\0\0\23\0\0\0\2\202\0\0\0\0\6\0\0\0\2\0\0\0\20\26\26\0""0\0\0\0"
  "C\0\0\0[23\0\241\202\326\331\0\377\16QR\0d\0\0\0\26\0\0\0A\306\311\0\366"
  "\326\331\0\377ik\0\272\0\0\0cKM\0\212\273\275\0\356\326\331\0\377\277\302"
  "\0\354\3\3\0;\0\0\0\12\0\0\0\1\232\0\0\0\0\35\0\0\0\12\0\0\0F\324\327\0\376"
  "\326\331\0\37767\0m\0\0\0N\324\327\0\376\326\331\0\377::\0g\3\3\0F\263\265"
  "\0\335\326\331\0\377\321\324\0\374\274\276\0\351\317\321\0\366\204\206\0"
  "\201\5\5\0I\265\267\0\336\326\331\0\377\276\301\0\357\265\270\0\351\326\331"
  "\0\377\275\300\0\347\24\24\0J\0\0\0J\324\327\0\376\326\331\0\377\313\316"
  "\0\370\310\312\0\366\202\326\331\0\377\22tu\0\216\0\0\0,^`\0q\325\330\0\377"
  "\313\316\0\372\251\253\0\340\266\271\0\343\322\325\0\372VW\0Y\6\6\0C\265"
  "\267\0\336\326\331\0\377\276\301\0\357\265\270\0\351\326\331\0\377\275\300"
  "\0\347\26\27\0B\0\0\0\16\202\0\0\0\1\33\0\0\0\14\7\7\0""9\265\267\0\336\326"
  "\331\0\377\276\301\0\357\265\270\0\351\326\331\0\377\275\300\0\347\26\27"
  "\0B\0\0\0\26\0\0\0=\324\327\0\376\326\331\0\377\307\312\0\366\325\330\0\376"
  "\312\315\0\356\0\0\0d\324\327\0\376\326\331\0\377\307\312\0\366\325\330\0"
  "\376\306\311\0\362\245\250\0\331\326\331\0\377\226\230\0\245\0\0\0\34\0\0"
  "\0\4\203\0\0\0\0\20\0\0\0\5\0\0\0\31^_\0_ab\0\221\223\225\0\330\326\331\0"
  "\377\265\270\0\347\7\7\0=\0\0\0\23\0\0\0A\306\311\0\366\326\331\0\377vx\0"
  "\246\0\0\0.\0\0\0-IJ\0\213\202\326\331\0\377\3WY\0u\0\0\0\23\0\0\0\2\232"
  "\0\0\0\0""1\0\0\0\12\0\0\0F\324\327\0\376\326\331\0\37767\0n\0\0\0U\324\327"
  "\0\376\326\331\0\37711\0zgh\0\227\326\331\0\377\317\322\0\37489\0\210\0\0"
  "\0F\30\30\0=>?\0Hkl\0\222\326\331\0\377\273\276\0\365\5\5\0\235\0\0\0\226"
  "\250\253\0\354\326\331\0\377|}\0\242\0\0\0`\324\327\0\376\326\331\0\377\201"
  "\203\0\303\0\0\0u\237\241\0\336\326\331\0\377\233\236\0\307\0\0\0>~\200\0"
  "\226\326\331\0\377\235\237\0\347*+\0\241\3\3\0{\27\27\0f\24\25\0Hjk\0\223"
  "\326\331\0\377\273\276\0\365\5\5\0\235\0\0\0\226\250\253\0\354\326\331\0"
  "\377\201\203\0\233\0\0\0\33\202\0\0\0\3\33\0\0\0\30pq\0\213\326\331\0\377"
  "\273\276\0\365\5\5\0\235\0\0\0\226\250\253\0\354\326\331\0\377\200\202\0"
  "\234\0\0\0%\0\0\0H\324\327\0\376\326\331\0\377\235\237\0\326\15\15\0^11\0"
  "J\0\0\0W\324\327\0\376\326\331\0\377\234\236\0\327\15\15\0a''\0]\222\224"
  "\0\254\265\270\0\324\206\210\0\201\0\0\0\26\0\0\0\3\203\0\0\0\0\3\0\0\0\4"
  "\0\0\0\36\272\275\0\316\202\326\331\0\377\13\312\315\0\373YZ\0\254\0\0\0"
  """6\0\0\0\22\0\0\0A\306\311\0\366\326\331\0\377xz\0\243\0\0\0#\0\0\0\27\4"
  "\4\0X\202\326\331\0\377\3st\0\221\0\0\0\30\0\0\0\3\232\0\0\0\0\21\0\0\0\12"
  "\0\0\0F\324\327\0\376\326\331\0\37756\0o\0\0\0V\324\327\0\376\326\331\0\377"
  "..\0\202\206\210\0\272\326\331\0\377\257\261\0\351\0\0\0L\0\0\0\30\0\0\0"
  "\22\0\0\0)\214\216\0\264\206\326\331\0\377\14\235\237\0\274\0\0\0e\324\327"
  "\0\376\326\331\0\377=>\0~\0\0\0H\214\216\0\304\326\331\0\377\240\242\0\320"
  "\0\0\0=99\0Z\312\315\0\364\202\326\331\0\377\4\320\322\0\373\252\254\0\330"
  "$%\0n\207\210\0\273\206\326\331\0\377\2\240\242\0\270\0\0\0\37\202\0\0\0"
  "\4\2\0\0\0!\215\217\0\262\206\326\331\0\377\23\240\242\0\270\0\0\0)\0\0\0"
  "J\324\327\0\376\326\331\0\377GH\0\204\0\0\0$\0\0\0\25\0\0\0I\324\327\0\376"
  "\326\331\0\377GH\0\204\0\0\0%\0\0\0\33\0\0\0/\0\0\0""9\0\0\0$\0\0\0\13\0"
  "\0\0\1\202\0\0\0\0\21\0\0\0\1\0\0\0\5\0\0\0\27\\^\0Zac\0\212\207\211\0\313"
  "\326\331\0\377\324\327\0\376NO\0h\0\0\0\33\0\0\0B\306\311\0\366\326\331\0"
  "\377vx\0\246\0\0\0*\0\0\0$23\0w\202\326\331\0\377\3ef\0\201\0\0\0\25\0\0"
  "\0\2\232\0\0\0\0\24\0\0\0\12\0\0\0F\324\327\0\376\326\331\0\37756\0o\0\0"
  "\0V\324\327\0\376\326\331\0\377./\0\200|~\0\255\326\331\0\377\300\302\0\364"
  "\2\2\0_\0\0\0-\0\0\0$\0\0\0""1\201\202\0\251\326\331\0\377\267\272\0\366"
  "VX\0\273\202`a\0\251\26de\0\242PR\0o\0\0\0]\324\327\0\376\326\331\0\3776"
  "7\0m\0\0\0;\217\221\0\277\326\331\0\377\237\241\0\321\0\0\0""9\0\0\0""0\27"
  "\30\0cde\0\246\200\201\0\312\270\273\0\363\326\331\0\377\214\216\0\302vx"
  "\0\270\326\331\0\377\267\272\0\366VX\0\273\202`a\0\251\12de\0\242VW\0h\0"
  "\0\0\27\0\0\0\3\0\0\0\4\0\0\0\35\204\206\0\245\326\331\0\377\267\272\0\366"
  "VX\0\273\202`a\0\251,de\0\242VW\0h\0\0\0!\0\0\0I\324\327\0\376\326\331\0"
  "\37799\0i\0\0\0\24\0\0\0\14\0\0\0F\324\327\0\376\326\331\0\37799\0i\0\0\0"
  "\26\0\0\0\30de\0^uw\0|VW\0J\0\0\0\20\0\0\0\2\0\0\0\0\0\0\0\1\0\0\0\5\0\0"
  "\0\24\0\0\0)\0\0\0""6\0\0\0G\0\0\0{\307\312\0\372\326\331\0\377\200\202\0"
  "\227\0\0\0!\0\0\0C\306\311\0\366\326\331\0\377lm\0\266\0\0\0V\15\15\0a\243"
  "\245\0\333\326\331\0\377\313\316\0\370\27\30\0H\0\0\0\15\0\0\0\1\232\0\0"
  "\0\0""1\0\0\0\11\0\0\0C\324\327\0\376\326\331\0\37788\0k\0\0\0S\324\327\0"
  "\376\326\331\0\37745\0r12\0h\320\322\0\372\326\331\0\377\241\244\0\332su"
  "\0\245\233\235\0\257vw\0t45\0j\320\323\0\373\325\330\0\376{}\0\313QS\0\242"
  "no\0\257\257\261\0\323np\0v\0\0\0V\324\327\0\376\326\331\0\3779:\0h\0\0\0"
  """7\221\223\0\274\326\331\0\377\241\243\0\317\0\0\0<tv\0e\235\240\0\267a"
  "c\0\244KL\0\256\241\243\0\351\326\331\0\377\222\224\0\276./\0x\320\323\0"
  "\373\325\330\0\376{}\0\313QS\0\242no\0\257\257\261\0\323uw\0o\0\0\0\22\202"
  "\0\0\0\2""2\0\0\0\21<=\0\\\320\323\0\373\325\330\0\376{}\0\313QS\0\242no"
  "\0\257\257\261\0\323uw\0o\0\0\0\34\0\0\0E\324\327\0\376\326\331\0\377<=\0"
  "c\0\0\0\21\0\0\0\13\0\0\0C\324\327\0\376\326\331\0\377;<\0d\0\0\0\25\0\0"
  "\0'\260\262\0\314\326\331\0\377\226\230\0\245\0\0\0\34\0\0\0\4\0\0\0\0\0"
  "\0\0\1\0\0\0\15tv\0Z\270\273\0\322\207\211\0\261yz\0\257\240\242\0\334\326"
  "\331\0\377\325\330\0\377YZ\0n\0\0\0\33\0\0\0@\307\311\0\365\326\331\0\377"
  "\267\271\0\362\255\257\0\346\311\314\0\370\326\331\0\377\323\326\0\376vx"
  "\0\231\0\0\0\"\0\0\0\5\233\0\0\0\0\14\0\0\0\7\0\0\0""2\325\330\0\375\326"
  "\331\0\377DE\0X\0\0\0>\325\330\0\375\326\331\0\377DE\0W\0\0\0.de\0\204\302"
  "\304\0\356\202\326\331\0\377\5\325\330\0\375|~\0u\0\0\0""3gi\0\206\304\306"
  "\0\356\203\326\331\0\377\15\305\310\0\347^_\0[\0\0\0@\324\327\0\376\326\331"
  "\0\377FG\0U\0\0\0)\236\240\0\255\326\331\0\377\253\255\0\303\0\0\0""1uv\0"
  "h\320\323\0\367\203\326\331\0\377\5\273\276\0\3439:\0Y\0\0\0""3gi\0\206\304"
  "\306\0\356\203\326\331\0\377\11\305\310\0\347cd\0V\0\0\0\16\0\0\0\2\0\0\0"
  "\1\0\0\0\6\0\0\0\"hj\0\205\304\306\0\356\203\326\331\0\377\31\305\310\0\347"
  "cd\0V\0\0\0\25\0\0\0""4\325\330\0\375\326\331\0\377IK\0Q\0\0\0\14\0\0\0\10"
  "\0\0\0""2\325\330\0\375\326\331\0\377IK\0Q\0\0\0\21\0\0\0$\262\265\0\311"
  "\326\331\0\377\232\234\0\241\0\0\0\32\0\0\0\3\0\0\0\0\0\0\0\1\0\0\0\15bc"
  "\0U\316\320\0\364\203\326\331\0\377\6\307\312\0\364\201\203\0\237\0\0\0("
  "\0\0\0\16\0\0\0/\312\315\0\361\202\326\331\0\377\7\322\325\0\375\307\312"
  "\0\364\253\255\0\327^_\0}\0\0\0)\0\0\0\13\0\0\0\1\233\0\0\0\0\27\0\0\0\3"
  "\0\0\0\23\0\0\0""1\0\0\0""2\0\0\0\32\0\0\0\30\0\0\0""1\0\0\0""2\0\0\0\30"
  "\0\0\0\15\0\0\0\35\0\0\0""7<=\0\\ST\0g#$\0B\0\0\0\36\0\0\0\20\0\0\0\36\0"
  "\0\0""7>?\0\\QR\0i**\0Q\0\0\0""1\202\0\0\0\31K\0\0\0""1\0\0\0""2\0\0\0\31"
  "\0\0\0\20\0\0\0#\0\0\0""3\0\0\0&\0\0\0\23\0\0\0\33\3\4\0""6AB\0\\RS\0j78"
  "\0X\0\0\0""2\0\0\0\27\0\0\0\16\0\0\0\36\0\0\0""7>?\0\\QR\0i**\0Q\0\0\0""1"
  "\0\0\0\26\0\0\0\5\0\0\0\1\0\0\0\0\0\0\0\1\0\0\0\11\0\0\0\35\0\0\0""7>?\0"
  "\\QR\0i**\0Q\0\0\0""1\0\0\0\26\0\0\0\10\0\0\0\24\0\0\0""1\0\0\0""2\0\0\0"
  "\27\0\0\0\5\0\0\0\3\0\0\0\23\0\0\0""1\0\0\0""2\0\0\0\27\0\0\0\6\0\0\0\16"
  "\0\0\0'\0\0\0""2\0\0\0!\0\0\0\12\0\0\0\1\0\0\0\0\0\0\0\1\0\0\0\5\0\0\0\27"
  "\0\0\0""4<=\0\\RS\0k=>\0^\0\0\0;\0\0\0\"\0\0\0\13\0\0\0\4\0\0\0\22\0\0\0"
  """0\0\0\0B\0\0\0D\0\0\0B\0\0\0<\0\0\0.\0\0\0\32\0\0\0\11\0\0\0\2\235\0\0"
  "\0\0\1\0\0\0\3\202\0\0\0\7\2\0\0\0\4\0\0\0\3\202\0\0\0\7\17\0\0\0\3\0\0\0"
  "\2\0\0\0\4\0\0\0\11\0\0\0\16\0\0\0\20\0\0\0\13\0\0\0\5\0\0\0\2\0\0\0\4\0"
  "\0\0\11\0\0\0\16\0\0\0\20\0\0\0\15\0\0\0\10\202\0\0\0\4\202\0\0\0\7\26\0"
  "\0\0\3\0\0\0\2\0\0\0\5\0\0\0\7\0\0\0\5\0\0\0\3\0\0\0\4\0\0\0\11\0\0\0\16"
  "\0\0\0\21\0\0\0\16\0\0\0\10\0\0\0\3\0\0\0\2\0\0\0\4\0\0\0\11\0\0\0\16\0\0"
  "\0\20\0\0\0\15\0\0\0\10\0\0\0\3\0\0\0\1\203\0\0\0\0\12\0\0\0\1\0\0\0\4\0"
  "\0\0\11\0\0\0\16\0\0\0\20\0\0\0\15\0\0\0\10\0\0\0\3\0\0\0\1\0\0\0\3\202\0"
  "\0\0\7\4\0\0\0\3\0\0\0\1\0\0\0\0\0\0\0\3\202\0\0\0\7\7\0\0\0\3\0\0\0\1\0"
  "\0\0\2\0\0\0\5\0\0\0\7\0\0\0\4\0\0\0\1\203\0\0\0\0\10\0\0\0\1\0\0\0\4\0\0"
  "\0\11\0\0\0\17\0\0\0\21\0\0\0\17\0\0\0\12\0\0\0\5\202\0\0\0\1\11\0\0\0\3"
  "\0\0\0\7\0\0\0\11\0\0\0\12\0\0\0\11\0\0\0\10\0\0\0\6\0\0\0\3\0\0\0\1\251"
  "\0\0\0\0\203\0\0\0\1\204\0\0\0\0\203\0\0\0\1\215\0\0\0\0\203\0\0\0\1\205"
  "\0\0\0\0\203\0\0\0\1\211\0\0\0\0\203\0\0\0\1\231\0\0\0\0\203\0\0\0\1\377"
  "\0\0\0\0\241\0\0\0\0",
};
#endif
#if 0
static const struct {
  unsigned int 	 width;
  unsigned int 	 height;
  unsigned int 	 bytes_per_pixel; /* 3:RGB, 4:RGBA */ 
  unsigned char	 pixel_data[135 * 14 * 3 + 1];
} dpxc_image = {
  135, 14, 3,
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0zzz\372\372\372\377\377\377\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0zzz\372\372\372\377\377"
  "\377\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\353\353\353\35\35\35\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0'''\340\340\340\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\377\377\377\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\353\353\353\35\35\35\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\377\377\377\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\377\377"
  "\377\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0fff\242\242"
  "\242\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\377\377\377\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\377\377"
  "\377\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0""111\316"
  "\316\316\374\374\374\377\377\377\0\0\0\377\377\377\0\0\0\377\377\377}}}\360"
  "\360\360\354\354\354^^^\0\0\0)))\306\306\306\372\372\372\331\331\331;;;\0"
  "\0\0\377\377\377\377\377\377\377\377\377\0\0\0""333\321\321\321\373\373\373"
  "\321\321\321111\0\0\0\377\377\377\222\222\222\372\372\372\0\0\0\377\377\377"
  "|||\357\357\357\351\351\351WWW\201\201\201\356\356\356\355\355\355\\\\\\"
  "\0\0\0\0\0\0\0\0\0\0\0\0""111\316\316\316\374\374\374\377\377\377\0\0\0""3"
  "33\321\321\321\373\373\373\321\321\321111\0\0\0\377\377\377|||\357\357\357"
  "\351\351\351WWW\201\201\201\356\356\356\355\355\355\\\\\\\0\0\0\245\245\245"
  "ccc\0\0\0""777\332\332\332\365\365\365\210\210\210\377\377\377\0\0\0\377"
  "\377\377\210\210\210\365\365\365\332\332\332777\0\0\0\227\227\227UUU\0\0"
  "\0VVV\227\227\227\0\0\0\0\0\0\0\0\0\0\0\0\377\377\377\377\377\377\377\377"
  "\377\0\0\0""333\321\321\321\373\373\373\321\321\321111\0\0\0\377\377\377"
  "\222\222\222\372\372\372\0\0\0\0\0\0\0\0\0\377\377\377\377\377\377\377\377"
  "\377\377\377\377\0\0\0""333\321\321\321\373\373\373\321\321\321111\0\0\0"
  """333\321\321\321\373\373\373\321\321\321111\0\0\0\377\377\377\0\0\0\177"
  "\177\177\353\353\353\377\377\377\377\377\377\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\321\321\321ooo\7\7\7\0\0\0\0\0\0\377\377\377\0\0\0\377\377\377lll\11\11"
  "\11HHH\352\352\352\0\0\0\312\312\312___\7\7\7III\330\330\330\0\0\0\0\0\0"
  "\377\377\377\0\0\0\0\0\0\322\322\322mmm\13\13\13mmm\321\321\321\0\0\0\377"
  "\377\377^^^\3\3\3\0\0\0\377\377\377lll\11\11\11GGG\372\372\372mmm\11\11\11"
  "GGG\351\351\351\0\0\0\0\0\0\0\0\0\0\0\0\321\321\321ooo\7\7\7\0\0\0\0\0\0"
  "\322\322\322mmm\13\13\13mmm\321\321\321\0\0\0\377\377\377lll\11\11\11GGG"
  "\372\372\372mmm\11\11\11GGG\351\351\351\0\0\0\344\344\344%%%\0\0\0\322\322"
  "\322jjj\12\12\12mmm\377\377\377\0\0\0\377\377\377jjj\12\12\12mmm\322\322"
  "\322\0\0\0\5\5\5\272\272\272kkk\272\272\272\5\5\5\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\377\377\377\0\0\0\0\0\0\322\322\322mmm\13\13\13mmm\321\321\321\0\0"
  "\0\377\377\377^^^\3\3\3\0\0\0\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\0"
  "\0\0\322\322\322mmm\13\13\13mmm\321\321\321\0\0\0\322\322\322mmm\13\13\13"
  "mmm\321\321\321\0\0\0\377\377\377\0\0\0\365\365\365ddd\11\11\11\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\373\373\373\12\12\12\0\0\0\0\0\0\0\0\0\377\377"
  "\377\0\0\0\377\377\377\2\2\2\0\0\0\0\0\0\377\377\377\0\0\0\373\373\373\377"
  "\377\377\377\377\377\377\377\377\376\376\376\0\0\0\0\0\0\377\377\377\0\0"
  "\0\0\0\0\373\373\373\11\11\11\0\0\0\13\13\13\373\373\373\0\0\0\377\377\377"
  "\4\4\4\0\0\0\0\0\0\377\377\377\2\2\2\0\0\0\0\0\0\377\377\377\2\2\2\0\0\0"
  "\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\0\0\0\373\373\373\12\12\12\0\0\0\0\0"
  "\0\0\0\0\373\373\373\11\11\11\0\0\0\13\13\13\373\373\373\0\0\0\377\377\377"
  "\2\2\2\0\0\0\0\0\0\377\377\377\2\2\2\0\0\0\0\0\0\377\377\377###\346\346\346"
  "\0\0\0\0\0\0\373\373\373\11\11\11\0\0\0\12\12\12\377\377\377\0\0\0\377\377"
  "\377\11\11\11\0\0\0\12\12\12\373\373\373\0\0\0\0\0\0CCC\377\377\377:::\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\373\373\373\11"
  "\11\11\0\0\0\13\13\13\373\373\373\0\0\0\377\377\377\4\4\4\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\373\373\373\11\11\11\0\0\0\13"
  "\13\13\373\373\373\0\0\0\373\373\373\11\11\11\0\0\0\13\13\13\373\373\373"
  "\0\0\0\377\377\377\0\0\0ZZZ\307\307\307\360\360\360\202\202\202\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\320\320\320ooo\7\7\7\0\0\0\0\0\0\377\377\377\0\0\0\377"
  "\377\377\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\313\313\313___\12\12\12\0\0"
  "\0\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\323\323\323hhh\12\12\12mmm\321"
  "\321\321\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\0\0"
  "\0\377\377\377\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\0\0\0\320"
  "\320\320ooo\7\7\7\0\0\0\0\0\0\323\323\323hhh\12\12\12mmm\321\321\321\0\0"
  "\0\377\377\377\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\377\377\377"
  "bbb\250\250\250\0\0\0\0\0\0\322\322\322hhh\11\11\11jjj\377\377\377\0\0\0"
  "\377\377\377hhh\11\11\11jjj\322\322\322\0\0\0\13\13\13\304\304\304GGG\277"
  "\277\277\10\10\10\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\323"
  "\323\323hhh\12\12\12mmm\321\321\321\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\372\372\372\31\31\31\0\0\0\0\0\0\323\323\323hhh\12\12\12"
  "mmm\321\321\321\0\0\0\323\323\323hhh\12\12\12mmm\321\321\321\0\0\0\377\377"
  "\377\0\0\0\0\0\0\0\0\0""000\367\367\367\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0""2"
  "22\321\321\321\376\376\376\377\377\377\0\0\0\377\377\377\0\0\0\377\377\377"
  "\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0%%%\274\274\274\367\367\367\377\377\377"
  "\377\377\377\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0""666\323\323\323\373\373"
  "\373\322\322\322333\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0"
  "\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\377\377\377"
  "\0\0\0\0\0\0""222\321\321\321\376\376\376\377\377\377\0\0\0""666\323\323"
  "\323\373\373\373\322\322\322333\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\377\377"
  "\377\0\0\0\0\0\0\0\0\0\377\377\377\241\241\241iii\0\0\0\0\0\0""777\332\332"
  "\332\365\365\365\210\210\210\377\377\377\0\0\0\377\377\377\210\210\210\365"
  "\365\365\332\332\332777\0\0\0\235\235\235EEE\0\0\0JJJ\233\233\233\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0""666\323\323\323\373\373\373"
  "\322\322\322333\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\223"
  "\223\223\366\366\366\377\377\377\0\0\0""666\323\323\323\373\373\373\322\322"
  "\322333\0\0\0""666\323\323\323\373\373\373\322\322\322333\0\0\0\377\377\377"
  "\0\0\0\377\377\377\377\377\377\353\353\353}}}\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\340\340\340+++\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\377\377\377\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\377\377"
  "\377",
};
#endif

#if 0
//void Generate10bitThumbnail(BITSTREAM *output, int type)
int GenerateThumbnail(void *samplePtr,
						size_t sampleSize,
						void *outputBuffer,
						size_t outputSize, 
						size_t flags,
						size_t *retWidth,
						size_t *retHeight,
						size_t *retSize)
{
	BITSTREAM input;
	SAMPLE_HEADER header;
	BYTE *ptr = samplePtr;
	uint32_t  *optr = (uint32_t  *)outputBuffer;
	
	InitBitstreamBuffer(&input, (uint8_t  *)samplePtr, (DWORD)sampleSize, BITSTREAM_ACCESS_READ);

	
	memset(&header, 0, sizeof(SAMPLE_HEADER));
	header.find_lowpass_bands = 1;

	if(ParseSampleHeader(&input, &header))
	{
		uint32_t *yptr;
		uint32_t *uptr;
		uint32_t *vptr;
		uint32_t *gptr;
		uint32_t *gptr2;
		uint32_t *rptr;
		uint32_t *rptr2;
		uint32_t *bptr;
		uint32_t *bptr2;
		int x,y,width,height,watermark = flags>>8;

		width = (header.width+7)/8;
		height = (header.height+7)/8;

		if(watermark) // watermark the source
		{
			unsigned char buf[8192];
			int yoff=0,w,h;

			if(watermark & 1)// expired
			{
				w = errExpired.width;
				h = errExpired.height;
				if(w * h * errExpired.bytes_per_pixel <= 8192)
				{
					RUN_LENGTH_DECODE(buf, errExpired.rle_pixel_data, w * h, errExpired.bytes_per_pixel);
				}
			}
			else if(watermark & 2)// 444
			{
				w = err444.width;
				h = err444.height;
				if(w * h * err444.bytes_per_pixel <= 8192)
				{
					RUN_LENGTH_DECODE(buf, err444.rle_pixel_data, w * h, err444.bytes_per_pixel);
				}
			}
			else if(watermark & 4)// RAW
			{
				w = errRAW.width;
				h = errRAW.height;
				if(w * h * errRAW.bytes_per_pixel <= 8192)
				{
					RUN_LENGTH_DECODE(buf, errRAW.rle_pixel_data, w * h, errRAW.bytes_per_pixel);
				}
			}
			else if(watermark & 8)// 1080p
			{
				w = errHD.width;
				h = errHD.height;
				if(w * h * errHD.bytes_per_pixel <= 8192)
				{
					RUN_LENGTH_DECODE(buf, errHD.rle_pixel_data, w * h, errHD.bytes_per_pixel);
				}
			}
			else if(watermark & 16)// 3D
			{
				w = err3D.width;
				h = err3D.height;
				if(w * h * err3D.bytes_per_pixel <= 8192)
				{
					RUN_LENGTH_DECODE(buf, err3D.rle_pixel_data, w * h, err3D.bytes_per_pixel);
				}
			}
			else
			{
				w = errExpired.width;
				h = errExpired.height;
				if(w * h * errExpired.bytes_per_pixel <= 8192)
				{
					RUN_LENGTH_DECODE(buf, errExpired.rle_pixel_data, w * h, errExpired.bytes_per_pixel);
				}
			}

			switch(header.encoded_format)
			{
				case ENCODED_FORMAT_UNKNOWN:
				case ENCODED_FORMAT_YUV_422:
					for(y=height/2 - h/2; y<height/2 + h/2; y++, yoff++)
					{
						yptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[0]);
						if(y>=0 && y<height)
							yptr += (width/2)*y;

						for(x=0; x<width; x+=2)
						{
							int y1,y2,a,pp;

							pp = _bswap(*yptr);
							y1 = ((pp>>20) & 0x3ff) - 64;
							y2 = ((pp>>4) & 0x3ff) - 64;

							a = buf[yoff*w*4 + (x%w)*4 + 3]+50;
							y1 *= 256-a;
							y1 += (buf[yoff*w*4 + (x%w)*4 + 1]<<2)*a*6/10;
							y1 >>= 8;

							a = buf[yoff*w*4 + ((x+1)%w)*4 + 3]+50;
							y2 *= 256-a;
							y2 += (buf[yoff*w*4 + ((x+1)%w)*4 + 1]<<2)*a*6/10;
							y2 >>= 8;

							pp = ((y1+64)<<20) | ((y2+64)<<4);

							*yptr++ = _bswap(pp);
						}
					}
					break;
				case ENCODED_FORMAT_BAYER:
					
					for(y=height/4 - h/2; y<height/4 + h/2; y++, yoff++)
					{	
						gptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[0]);
						rptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[1]);
						bptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[2]);

						if(y>=0 && y<height)
						{
							gptr += (y) * (width/4);
							rptr += (y) * (width/4);
							bptr += (y) * (width/4);
						}

						for(x=0; x<width/2; x+=2)
						{
							int g1,g2,r1,r2,b1,b2,a,pp,r,g,b;
							

							pp = _bswap(*gptr);
							g1 = ((pp>>20) & 0x3ff);
							g2 = ((pp>>4) & 0x3ff);
							pp = _bswap(*rptr);
							r1 = ((pp>>20) & 0x3ff);
							r2 = ((pp>>4) & 0x3ff);
							pp = _bswap(*bptr);
							b1 = ((pp>>20) & 0x3ff);
							b2 = ((pp>>4) & 0x3ff);
							g = (buf[yoff*w*4 + (x%w)*4 + 1]<<2);
							r = (buf[yoff*w*4 + (x%w)*4 + 0]<<2);
							b = (buf[yoff*w*4 + (x%w)*4 + 2]<<2);
							r -= g; r>>=1;
							r += 512;
							b -= g; b>>=1;
							b += 512;
							a = buf[yoff*w*4 + (x%w)*4 + 3]+50;
							g1 *= 256-a;
							g1 += g*a*6/10;
							g1 >>= 8;
							r1 *= 256-a;
							r1 += r*a*6/10;
							r1 >>= 8;
							b1 *= 256-a;
							b1 += b*a*6/10;
							b1 >>= 8;

							g = (buf[yoff*w*4 + ((x+1)%w)*4 + 1]<<2);
							r = (buf[yoff*w*4 + ((x+1)%w)*4 + 0]<<2);
							b = (buf[yoff*w*4 + ((x+1)%w)*4 + 2]<<2);
							r -= g; r>>=1;
							r += 512;
							b -= g; b>>=1;
							b += 512;
							a = buf[yoff*w*4 + ((x+1)%w)*4 + 3]+50;
							g2 *= 256-a;
							g2 += g*a*6/10;
							g2 >>= 8;
							r2 *= 256-a;
							r2 += r*a*6/10;
							r2 >>= 8;
							b2 *= 256-a;
							b2 += b*a*6/10;
							b2 >>= 8;

							pp = ((g1)<<20) | ((g2)<<4);
							*gptr++ = _bswap(pp);
							pp = ((r1)<<20) | ((r2)<<4);
							*rptr++ = _bswap(pp);
							pp = ((b1)<<20) | ((b2)<<4);
							*bptr++ = _bswap(pp);
						}
					}
					break;

				case ENCODED_FORMAT_RGB_444:
				case ENCODED_FORMAT_RGBA_4444:
					for(y=height/2 - h/2; y<height/2 + h/2; y++, yoff++)
					{	
						gptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[0]);
						rptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[1]);
						bptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[2]);

						if(y>=0 && y<height)
						{
							gptr += (y) * (width/2);
							rptr += (y) * (width/2);
							bptr += (y) * (width/2);
						}

						for(x=0; x<width; x+=2)
						{
							int g1,g2,r1,r2,b1,b2,a,pp;
							

							pp = _bswap(*gptr);
							g1 = ((pp>>20) & 0x3ff) - 64;
							g2 = ((pp>>4) & 0x3ff) - 64;
							pp = _bswap(*rptr);
							r1 = ((pp>>20) & 0x3ff) - 64;
							r2 = ((pp>>4) & 0x3ff) - 64;
							pp = _bswap(*bptr);
							b1 = ((pp>>20) & 0x3ff) - 64;
							b2 = ((pp>>4) & 0x3ff) - 64;

							a = buf[yoff*w*4 + (x%w)*4 + 3]+50;
							g1 *= 256-a;
							g1 += (buf[yoff*w*4 + (x%w)*4 + 1]<<2)*a*6/10;
							g1 >>= 8;
							r1 *= 256-a;
							r1 += (buf[yoff*w*4 + (x%w)*4 + 0]<<2)*a*6/10;
							r1 >>= 8;
							b1 *= 256-a;
							b1 += (buf[yoff*w*4 + (x%w)*4 + 2]<<2)*a*6/10;
							b1 >>= 8;

							a = buf[yoff*w*4 + ((x+1)%w)*4 + 3]+50;
							g2 *= 256-a;
							g2 += (buf[yoff*w*4 + ((x+1)%w)*4 + 1]<<2)*a*6/10;
							g2 >>= 8;
							r2 *= 256-a;
							r2 += (buf[yoff*w*4 + ((x+1)%w)*4 + 0]<<2)*a*6/10;
							r2 >>= 8;
							b2 *= 256-a;
							b2 += (buf[yoff*w*4 + ((x+1)%w)*4 + 2]<<2)*a*6/10;
							b2 >>= 8;

							pp = ((g1+64)<<20) | ((g2+64)<<4);
							*gptr++ = _bswap(pp);
							pp = ((r1+64)<<20) | ((r2+64)<<4);
							*rptr++ = _bswap(pp);
							pp = ((b1+64)<<20) | ((b2+64)<<4);
							*bptr++ = _bswap(pp);
						}
					}
					break;
			}

		}

		if(flags & 3)
		{
			if(outputSize < width * height * 4)
				return 0; // failed

			switch(header.encoded_format)
			{
				case ENCODED_FORMAT_UNKNOWN:
				case ENCODED_FORMAT_YUV_422:
					yptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[0]);
					uptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[1]);
					vptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[2]);
					for(y=0; y<height; y++)
					{
						for(x=0; x<width; x+=4)
						{
							int y1,u1,v1,y2,u2,v2,r,g,b,rgb,pp;

							pp = _bswap(*yptr++);
							y1 = ((pp>>20) & 0x3ff) - 64;
							y2 = ((pp>>4) & 0x3ff) - 64;
							pp = _bswap(*uptr++);
							u1 = ((pp>>20) & 0x3ff) - 0x200;
							u2 = ((pp>>4) & 0x3ff) - 0x200;
							pp = _bswap(*vptr++);
							v1 = ((pp>>20) & 0x3ff) - 0x200;
							v2 = ((pp>>4) & 0x3ff) - 0x200;

							r = (1192*y1 + 1836*u1)>>10;
							g = (1192*y1 - 547*u1 - 218*v1)>>10;
							b = (1192*y1 + 2166*v1)>>10;
							if(r<0) r=0; if(r>0x3ff) r=0x3ff;
							if(g<0) g=0; if(g>0x3ff) g=0x3ff;
							if(b<0) b=0; if(b>0x3ff) b=0x3ff;
							rgb = ((r<<22)|(g<<12)|(b<<2));
							*(optr++) = _bswap(rgb);

							r = (1192*y2 + 1836*u1)>>10;
							g = (1192*y2 - 547*u1 - 218*v1)>>10;
							b = (1192*y2 + 2166*v1)>>10;
							if(r<0) r=0; if(r>0x3ff) r=0x3ff;
							if(g<0) g=0; if(g>0x3ff) g=0x3ff;
							if(b<0) b=0; if(b>0x3ff) b=0x3ff;
							rgb = ((r<<22)|(g<<12)|(b<<2));
							*(optr++) = _bswap(rgb);


							pp = _bswap(*yptr++);
							y1 = ((pp>>20) & 0x3ff) - 64;
							y2 = ((pp>>4) & 0x3ff) - 64;

							r = (1192*y1 + 1836*u2)>>10;
							g = (1192*y1 - 547*u2 - 218*v2)>>10;
							b = (1192*y1 + 2166*v2)>>10;
							if(r<0) r=0; if(r>0x3ff) r=0x3ff;
							if(g<0) g=0; if(g>0x3ff) g=0x3ff;
							if(b<0) b=0; if(b>0x3ff) b=0x3ff;
							rgb = ((r<<22)|(g<<12)|(b<<2));
							*(optr++) = _bswap(rgb);

							r = (1192*y2 + 1836*u2)>>10;
							g = (1192*y2 - 547*u2 - 218*v2)>>10;
							b = (1192*y2 + 2166*v2)>>10;
							if(r<0) r=0; if(r>0x3ff) r=0x3ff;
							if(g<0) g=0; if(g>0x3ff) g=0x3ff;
							if(b<0) b=0; if(b>0x3ff) b=0x3ff;
							rgb = ((r<<22)|(g<<12)|(b<<2));
							*(optr++) = _bswap(rgb);
						}
					}
					break;
				case ENCODED_FORMAT_BAYER:
					for(y=0; y<height; y++)
					{
						int y1 = (y)/2;
						int y2 = (y+1)/2;
						if(y2 == height/2)
							y2--;
						if(y1 == height/2)
							y1--;
						gptr = gptr2 = (uint32_t *)(ptr + header.thumbnail_channel_offsets[0]);
						gptr += (y1) * (width/4);
						rptr = rptr2 = (uint32_t *)(ptr + header.thumbnail_channel_offsets[1]);
						rptr += (y1) * (width/4);
						bptr = bptr2 = (uint32_t *)(ptr + header.thumbnail_channel_offsets[2]);
						bptr += (y1) * (width/4);

						if(y1!=y2)
						{
							gptr2 += (y2) * (width/4);
							rptr2 += (y2) * (width/4);
							bptr2 += (y2) * (width/4);

							for(x=0; x<width; x+=4)
							{
								int r,g,b,r1,g1,b1,r2,g2,b2,r3,g3,b3,r4,g4,b4,rgb,pp;

								pp = _bswap(*gptr++);
								g1 = (pp>>20) & 0x3ff;
								g2 = (pp>>4) & 0x3ff;
								pp = _bswap(*gptr2++);
								g3 = (pp>>20) & 0x3ff;
								g4 = (pp>>4) & 0x3ff;
								pp = _bswap(*rptr++);
								r1 = (pp>>20) & 0x3ff;
								r2 = (pp>>4) & 0x3ff;
								pp = _bswap(*rptr2++);
								r3 = (pp>>20) & 0x3ff;
								r4 = (pp>>4) & 0x3ff;
								pp = _bswap(*bptr++);
								b1 = (pp>>20) & 0x3ff;
								b2 = (pp>>4) & 0x3ff;
								pp = _bswap(*bptr2++);
								b3 = (pp>>20) & 0x3ff;
								b4 = (pp>>4) & 0x3ff;

								r1 = (r1-0x200)*2 + g1;
								if(r1<0) r1=0;
								if(r1>0x3ff) r1=0x3ff;
								b1 = (b1-0x200)*2 + g1;
								if(b1<0) b1=0;
								if(b1>0x3ff) b1=0x3ff;

								r2 = (r2-0x200)*2 + g2;
								if(r2<0) r2=0;
								if(r2>0x3ff) r2=0x3ff;
								b2 = (b2-0x200)*2 + g2;
								if(b2<0) b2=0;
								if(b2>0x3ff) b2=0x3ff;

								r3 = (r3-0x200)*2 + g3;
								if(r3<0) r3=0;
								if(r3>0x3ff) r3=0x3ff;
								b3 = (b3-0x200)*2 + g3;
								if(b3<0) b3=0;
								if(b3>0x3ff) b3=0x3ff;

								r4 = (r4-0x200)*2 + g4;
								if(r4<0) r4=0;
								if(r4>0x3ff) r4=0x3ff;
								b4 = (b2-0x200)*2 + g4;
								if(b4<0) b4=0;
								if(b4>0x3ff) b4=0x3ff;

								r = (r1+r3)>>1;
								g = (g1+g3)>>1;
								b = (b1+b3)>>1;
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*(optr++) = _bswap(rgb);
								
								r = (r1+r2+r3+r4)>>2;
								g = (g1+g2+g3+g4)>>2;
								b = (b1+b2+b3+b4)>>2;
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*(optr++) = _bswap(rgb);

								r = (r2+r4)>>1;
								g = (g2+g4)>>1;
								b = (b2+b4)>>1;
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*(optr++) = _bswap(rgb);
								
								
								pp = _bswap(*gptr);
								g1 = (pp>>20) & 0x3ff;
								pp = _bswap(*gptr2);
								g3 = (pp>>20) & 0x3ff;
								pp = _bswap(*rptr);
								r1 = (pp>>20) & 0x3ff;
								pp = _bswap(*rptr2);
								r3 = (pp>>20) & 0x3ff;
								pp = _bswap(*bptr);
								b1 = (pp>>20) & 0x3ff;
								pp = _bswap(*bptr2);
								b3 = (pp>>20) & 0x3ff;

								
								r1 = (r1-0x200)*2 + g1;
								if(r1<0) r1=0;
								if(r1>0x3ff) r1=0x3ff;
								b1 = (b1-0x200)*2 + g1;
								if(b1<0) b1=0;
								if(b1>0x3ff) b1=0x3ff;

								r3 = (r3-0x200)*2 + g3;
								if(r3<0) r3=0;
								if(r3>0x3ff) r3=0x3ff;
								b3 = (b3-0x200)*2 + g3;
								if(b3<0) b3=0;
								if(b3>0x3ff) b3=0x3ff;

								r = (r1+r2+r3+r4)>>2;
								g = (g1+g2+g3+g4)>>2;
								b = (b1+b2+b3+b4)>>2;
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*(optr++) = _bswap(rgb);
							}
						}
						else
						{
							for(x=0; x<width; x+=4)
							{
								int r,g,b,r1,g1,b1,r2,g2,b2,rgb,pp;

								pp = _bswap(*gptr++);
								g1 = (pp>>20) & 0x3ff;
								g2 = (pp>>4) & 0x3ff;
								pp = _bswap(*rptr++);
								r1 = (pp>>20) & 0x3ff;
								r2 = (pp>>4) & 0x3ff;
								pp = _bswap(*bptr++);
								b1 = (pp>>20) & 0x3ff;
								b2 = (pp>>4) & 0x3ff;
								
								r1 = (r1-0x200)*2 + g1;
								if(r1<0) r1=0;
								if(r1>0x3ff) r1=0x3ff;
								b1 = (b1-0x200)*2 + g1;
								if(b1<0) b1=0;
								if(b1>0x3ff) b1=0x3ff;

								r2 = (r2-0x200)*2 + g2;
								if(r2<0) r2=0;
								if(r2>0x3ff) r2=0x3ff;
								b2 = (b2-0x200)*2 + g2;
								if(b2<0) b2=0;
								if(b2>0x3ff) b2=0x3ff;

								rgb = ((r1<<22)|(g1<<12)|(b1<<2));
								*(optr++) = _bswap(rgb);
								
								r = (r1+r2)>>1;
								g = (g1+g2)>>1;
								b = (b1+b2)>>1;
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*(optr++) = _bswap(rgb);

								rgb = ((r2<<22)|(g2<<12)|(b2<<2));
								*(optr++) = _bswap(rgb);
								
								pp = _bswap(*gptr);
								g1 = (pp>>20) & 0x3ff;
								pp = _bswap(*rptr);
								r1 = (pp>>20) & 0x3ff;
								pp = _bswap(*bptr);
								b1 = (pp>>20) & 0x3ff;
								
								r1 = (r1-0x200)*2 + g1;
								if(r1<0) r1=0;
								if(r1>0x3ff) r1=0x3ff;
								b1 = (b1-0x200)*2 + g1;
								if(b1<0) b1=0;
								if(b1>0x3ff) b1=0x3ff;

								r = (r1+r2)>>1;
								g = (g1+g2)>>1;
								b = (b1+b2)>>1;
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*(optr++) = _bswap(rgb);
							}
						}
					}
					break;

				case ENCODED_FORMAT_RGB_444:
				case ENCODED_FORMAT_RGBA_4444:
					gptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[0]);
					rptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[1]);
					bptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[2]);
					for(y=0; y<height; y++)
					{
						for(x=0; x<width; x+=2)
						{
							int r1,g1,b1,r2,g2,b2,rgb,pp;

							pp = _bswap(*gptr++);
							g1 = (pp>>20) & 0x3ff;
							g2 = (pp>>4) & 0x3ff;
							pp = _bswap(*rptr++);
							r1 = (pp>>20) & 0x3ff;
							r2 = (pp>>4) & 0x3ff;
							pp = _bswap(*bptr++);
							b1 = (pp>>20) & 0x3ff;
							b2 = (pp>>4) & 0x3ff;
							rgb = ((r1<<22)|(g1<<12)|(b1<<2));
							*(optr++) = _bswap(rgb);

							rgb = ((r2<<22)|(g2<<12)|(b2<<2));
							*(optr++) = _bswap(rgb);
						}
					}
					break;
			}

			if(flags & 2) // DXP-c watermark
			{
				uint32_t *lptr = (uint32_t *)outputBuffer;
				int yy = 0;
				for(y=height-dpxc_image.height; y<height; y++)
				{
					lptr = (uint32_t *)outputBuffer;
					lptr += y*width;
					if(y>=0)
					{
						for(x=0; x<dpxc_image.width; x++)
						{
							if(x<width)
							{
								int r,g,b,rgb;
								int alpha = 128;

								rgb = _bswap(*lptr);
								r = (rgb >> 22) & 0x3ff;
								g = (rgb >> 12) & 0x3ff;
								b = (rgb >> 2) & 0x3ff;

								r = (r * alpha + dpxc_image.pixel_data[x*3 + yy*dpxc_image.width*3 + 0]*4*(256-alpha))>>8;
								g = (g * alpha + dpxc_image.pixel_data[x*3 + yy*dpxc_image.width*3 + 1]*4*(256-alpha))>>8;
								b = (b * alpha + dpxc_image.pixel_data[x*3 + yy*dpxc_image.width*3 + 2]*4*(256-alpha))>>8;
											
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*lptr++ = _bswap(rgb);
							}
						}
						yy++;
					}
				}
			}
		}
	}

	if(retWidth)
		*retWidth = (header.width+7)/8;
	if(retHeight)
		*retHeight = (header.height+7)/8;
	if(retSize)
		*retSize = (*retWidth * *retHeight * 4);

	return 1;
}
#endif
