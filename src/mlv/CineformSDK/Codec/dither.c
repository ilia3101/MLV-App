/*! @file dither.c

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

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include "../Common/macdefs.h"
#endif

#include <assert.h>
#include <limits.h>
#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif
#include <string.h>			// Memory routines

#include "config.h"
#include "color.h"
#include "dither.h"

#if 0

//TODO: Need to finish the dithering routine

// Color conversion coefficients (floating point)

static float YUVFromRGB_CS709[3][4] =
{
     0.183,  0.614,  0.062, 16.0/255.0,
    -0.101, -0.338,  0.439, 128.0/255.0,
     0.439, -0.399, -0.040, 128.0/255.0,
};

// Compute the floating-point color matrix for computer system 709
void ComputeColorMatrix(COLOR_SPACE color_space, COLOR_MATRIX *color_matrix)
{

	switch(color_space & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:
	case COLOR_SPACE_VS_601:
		//TODO: Need to implement the other cases
		assert(0);
		break;

	default: assert(0);
	case COLOR_SPACE_CG_709:
		memcpy(color_matrix->array, YUVFromRGB_CS709, sizeof(color_matrix->array));
		color_matrix->scale = 256.0;
		color_matrix->color_space = color_space;
		break;

	case COLOR_SPACE_VS_709:
		//TODO: Need to implement the other cases
		assert(0);
		break;
	}
}

#endif


// Compute the RGB to YUV color conversion coefficients as integers
void ComputeColorCoefficientsRGBToYUV(COLOR_CONVERSION *conversion,
									  int color_space)
{
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

	int shift;

	// Clear all of the color conversion parameters
	memset(conversion, 0, sizeof(COLOR_CONVERSION));

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

		shift = 8;

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

		shift = 8;

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

		shift = 8;

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

		shift = 8;
		break;
	}

	// Store the color conversion coefficients in the conversion data structure
	conversion->array[0][0] = y_rmult;
	conversion->array[0][1] = y_gmult;
	conversion->array[0][2] = y_bmult;
	conversion->array[0][3] = y_offset;

	conversion->array[1][0] = u_rmult;
	conversion->array[1][1] = u_gmult;
	conversion->array[1][2] = u_bmult;
	conversion->array[1][3] = u_offset;

	conversion->array[2][0] = v_rmult;
	conversion->array[2][1] = v_gmult;
	conversion->array[2][2] = v_bmult;
	conversion->array[2][3] = v_offset;

	// Store the scale of the coefficients
	conversion->shift = shift;

	// Remember the color space associated with the coefficients
	conversion->color_space = color_space;
}

// Compute the YUV to RGB color conversion coefficients as integers
void ComputeColorCoefficientsYUVToRGB(COLOR_CONVERSION *conversion,
									  int color_space)
{
	int ymult;
	int r_vmult;
	int g_vmult;
	int g_umult;
	int b_umult;
	int y_offset;

	int shift = 8;

	// Clear all of the color conversion parameters
	memset(conversion, 0, sizeof(COLOR_CONVERSION));

	switch(color_space & COLORSPACE_MASK)
	{
	case COLOR_SPACE_CG_601:
		ymult = 128*149;	//7bit 1.164
		r_vmult = 204;		//7bit 1.596
		g_vmult = 208;		//8bit 0.813
		g_umult = 100;		//8bit 0.391
		b_umult = 129;		//6bit 2.018
		y_offset = 16;
		//saturate = 1;
		break;

	default: assert(0);
	case COLOR_SPACE_CG_709:
		ymult = 128*149;	//7bit 1.164
		r_vmult = 230;		//7bit 1.793
		g_vmult = 137;		//8bit 0.534
		g_umult = 55;		//8bit 0.213
		b_umult = 135;		//6bit 2.115
		y_offset = 16;
		//saturate = 1;
		break;

	case COLOR_SPACE_VS_601:
		ymult = 128*128;	//7bit 1.164
		r_vmult = 175;		//7bit 1.371
		g_vmult = 179;		//8bit 0.698
		g_umult = 86;		//8bit 0.336
		b_umult = 111;		//6bit 1.732
		y_offset = 0;
		//saturate = 0;
		break;

	case COLOR_SPACE_VS_709:
		ymult = 128*128;	//7bit 1.164
		r_vmult = 197;		//7bit 1.540
		g_vmult = 118;		//8bit 0.459
		g_umult = 47;		//8bit 0.183
		b_umult = 116;		//6bit 1.816
		y_offset = 0;
		//saturate = 0;
		break;
	}

	// Store the color conversion coefficients in the conversion data structure
	conversion->array[0][0] = ymult;
	//conversion->array[0][1] = r_umult;
	conversion->array[0][2] = r_vmult;
	conversion->array[0][3] = y_offset;

	conversion->array[1][0] = ymult;
	conversion->array[1][1] = g_umult;
	conversion->array[1][2] = g_vmult;
	conversion->array[1][3] = y_offset;

	conversion->array[2][0] = ymult;
	conversion->array[2][1] = b_umult;
	//conversion->array[2][2] = b_vmult;
	conversion->array[2][3] = y_offset;

	// Store the scale of the coefficients
	conversion->shift = shift;

	// Store the video safe luma offset
	conversion->luma_offset = y_offset;

	// Remember the color space associated with the coefficients
	conversion->color_space = color_space;
}

