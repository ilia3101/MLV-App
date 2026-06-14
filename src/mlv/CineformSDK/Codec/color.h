/*! @file color.h

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

#ifndef _COLOR_H
#define _COLOR_H

#include "config.h"

#define _ENABLE_GAMMA_CORRECTION	0
#define _USE_YCBCR					1

#ifndef STRICT_SATURATE
#define STRICT_SATURATE		0			// Strict saturation on YUV components?
#endif

#define Y_MIN				16
#define Y_MAX				235
#define Cb_MIN				16
#define Cb_MAX				240
#define Cr_MIN				16
#define Cr_MAX				240

#if STRICT_SATURATE

#define SATURATE_Y(x)		_SATURATE(Y_MIN, (x), Y_MAX)
#define SATURATE_Cb(x)		_SATURATE(Cb_MIN, (x), Cb_MAX)
#define SATURATE_Cr(x)		_SATURATE(Cr_MIN, (x), Cr_MAX)

#else

#define SATURATE_Y(x)		(x)
#define SATURATE_Cb(x)		(x)
#define SATURATE_Cr(x)		(x)

#endif

#define COLOR_LUMA_BLACK	0		// Black luminance
#define COLOR_CHROMA_ZERO	128		// Value for encoding zero chroma
#define RGBA_DEFAULT_ALPHA	255		// Default alpha value for RGBA

// Color format codes used internally by the codec
typedef enum color_format
{
	COLOR_FORMAT_UNKNOWN = 0,

	COLOR_FORMAT_UYVY = 1,		// Supported color formats
	COLOR_FORMAT_YUYV = 2,
	COLOR_FORMAT_YVYU = 3,
	
	COLOR_FORMAT_RGB24 = 7,
	COLOR_FORMAT_RGB32 = 8,

	COLOR_FORMAT_RGB32_INVERTED  = 9,

	COLOR_FORMAT_V210 = 10,		// Packed 10 bit pixel formats
	COLOR_FORMAT_RGB10 = 11,
	COLOR_FORMAT_YU64 = 12,
	COLOR_FORMAT_YR16 = 13,		// Rows of 16-bit YUV luma and chroma

	//COLOR_FORMAT_YV12 = 4,		
	COLOR_FORMAT_I420 = 5,		// Unsupported color formats
	COLOR_FORMAT_RGB16 = 6,
	COLOR_FORMAT_YUVA = 14,
	
	COLOR_FORMAT_NV12 = 16,		// 4:2:0 pixel formats
	COLOR_FORMAT_YV12 = 17,		// 

	// New color formats added for QuickTime (fourcc listed as comment)
	COLOR_FORMAT_BGRA64 = 30,	// b64a
	COLOR_FORMAT_YUVA_FLOAT,	// r4fl
	COLOR_FORMAT_BGRA32,		// BGRA
	COLOR_FORMAT_2VUY,			// 2vuy
	COLOR_FORMAT_QT32,			// ARGB
	COLOR_FORMAT_AYUV_QTR,		// r408 // AYUV, A=0-255, Y = 0 to 219 (overs allowed)
	COLOR_FORMAT_UYVA_QT,		// v408 // UYVA, A=0-255, Y = 16 to 235 (overs allowed)

	// Other names for the QuickTime pixel formats (uses the FOURCC in the format name)
	COLOR_FORMAT_B64A = COLOR_FORMAT_BGRA64,		// ARGB 4:4:4:4 with 16 bits per component
	COLOR_FORMAT_R4FL = COLOR_FORMAT_YUVA_FLOAT,	// Final Cut Pro 32-bit floating point YUVA 4:4:4:4
	COLOR_FORMAT_BGRA = COLOR_FORMAT_BGRA32,		// BGRA 4:4:4:4 with 8 bits per component
	COLOR_FORMAT_R408 = COLOR_FORMAT_AYUV_QTR,
	COLOR_FORMAT_V408 = COLOR_FORMAT_UYVA_QT,

	// Avid color formats (from document "Avid Buffer Formats")
	COLOR_FORMAT_AVID = 64,
	COLOR_FORMAT_CbYCrY_8bit = 65,			// 8 bit pixels in range 0-255 (video safe luma 16-235, chroma 16-240)
	COLOR_FORMAT_CbYCrY_16bit = 66,			// 16 bit pixels in range 0-65535 (video safe luma 4096-60160, chroma 16-61440)
	COLOR_FORMAT_CbYCrY_10bit_2_8 = 67,		// 10 bit pixels in two planes (upper plane 2 bits, lower plane 8 bits)
	COLOR_FORMAT_CbYCrY_16bit_2_14 = 68,	// 16 bit pixels in fixed point 2.14 format
	COLOR_FORMAT_CbYCrY_16bit_10_6 = 69,	// 16 bit pixels in fixed point 10.6 format
	COLOR_FORMAT_AVID_END = 70,

	// Alternative names for the Avid pixel format
	COLOR_FORMAT_CT_UCHAR = COLOR_FORMAT_CbYCrY_8bit,
	COLOR_FORMAT_CT_SHORT = COLOR_FORMAT_CbYCrY_16bit,
	COLOR_FORMAT_CT_10BIT_2_8 = COLOR_FORMAT_CbYCrY_10bit_2_8,
	COLOR_FORMAT_CT_SHORT_2_14 = COLOR_FORMAT_CbYCrY_16bit_2_14,
	COLOR_FORMAT_CT_USHORT_10_6 = COLOR_FORMAT_CbYCrY_16bit_10_6,


	// Formats 100 and above require CODEC_TAG_INPUT_FORMAT tag, because they are
	// encoded in the source format (rather than converted to YUV 4:2:2)
	COLOR_FORMAT_INPUT_FORMAT_TAG_REQUIRED = 100,

	COLOR_FORMAT_BAYER = 100,
	COLOR_FORMAT_BYR1 = 101,		// Bayer 8 bits per channel
	COLOR_FORMAT_BYR2 = 102,		// Bayer 10 bits per channel
	COLOR_FORMAT_BYR3 = 103,		// Bayer 10 bits per channel planar
	COLOR_FORMAT_BYR4 = 104,		// Bayer 16 bits per channel
	COLOR_FORMAT_BYR5 = 105,		// Bayer 12 bits per channel planar 8/4
	COLOR_FORMAT_BAYER_END = 106,

	COLOR_FORMAT_RGB48 = 120,		//encoded as RGB
	COLOR_FORMAT_RGBA64,			//encoded as RGBA
	COLOR_FORMAT_RG30,				//Packed into 30 bit version of RGB48
	COLOR_FORMAT_R210,				//Packed into 30 bit version of RGB48, byte swapped RG30
	COLOR_FORMAT_AR10,				//MEDIASUBTYPE_A2R10G10B10 
	COLOR_FORMAT_AB10,				//MEDIASUBTYPE_A2B10G10R10 

	COLOR_FORMAT_RGB48_WP13,		//RGB48 with White point 13-bit RGB 4:4:4 16-bit signed 

	COLOR_FORMAT_RGB_8PIXEL_PLANAR,	// internal format for fast format conversions.
	
	COLOR_FORMAT_DPX0,				// Packed 30-bit RGB48 (byte swapped RG30)
	COLOR_FORMAT_DPX1,				// Packed 10-bit YUV 4:2:2 (byte swapped)
	COLOR_FORMAT_DPX2,				// Packed 16-bit CbYCrA 4:4:4:4
	COLOR_FORMAT_DPX3,				// Packed 10-bit CbYCrA 4:4:4:4

	// Alternative names for the DPX pixel formats (adapted from the DPX library)
	COLOR_FORMAT_DPX_RGB_10BIT_444 = COLOR_FORMAT_DPX0,
	COLOR_FORMAT_DPX_YUV_10BIT_422 = COLOR_FORMAT_DPX1,
	COLOR_FORMAT_DPX_YUVA_16BIT_4444 = COLOR_FORMAT_DPX2,
	COLOR_FORMAT_DPX_YUVA_10BIT_4444 = COLOR_FORMAT_DPX3,

	COLOR_FORMAT_RGBA64_W13A,		//RGBA64 with White point 13-bit RGB 4:4:4:4 16-bit signed 

	//TODO: Add new color formats to the routine DefaultEncodedFormat	


	// Other names for these formats (use the FOURCC in the format name)
	COLOR_FORMAT_RG48 = COLOR_FORMAT_RGB48,
	COLOR_FORMAT_WP13 = COLOR_FORMAT_RGB48_WP13,
	COLOR_FORMAT_RG64 = COLOR_FORMAT_RGBA64,
	COLOR_FORMAT_W13A = COLOR_FORMAT_RGBA64_W13A,

} COLOR_FORMAT;

// Define the mask for the color format witin the decoded format
#ifndef COLOR_FORMAT_MASK
#define COLOR_FORMAT_MASK 0xFFFF
#endif

#ifndef ColorFormat
#define ColorFormat(format) ((COLOR_FORMAT)((format) & COLOR_FORMAT_MASK))
#endif

typedef enum color_space
{
	COLOR_SPACE_UNDEFINED = 0,
	COLOR_SPACE_BT_601 = 1,
	COLOR_SPACE_BT_709 = 2,		// BT 709 versus 601 YUV source
	COLOR_SPACE_VS_RGB = 4,		// RGB that ranges normally from 16 to 235 just like luma
	COLOR_SPACE_422_TO_444 = 8,
	COLOR_SPACE_8_PIXEL_PLANAR = 16,

	COLOR_SPACE_VS_709 = (COLOR_SPACE_BT_709 | COLOR_SPACE_VS_RGB),
	COLOR_SPACE_VS_601 = (COLOR_SPACE_BT_601 | COLOR_SPACE_VS_RGB),
	COLOR_SPACE_CG_709 = (COLOR_SPACE_BT_709),
	COLOR_SPACE_CG_601 = (COLOR_SPACE_BT_601),

	COLOR_SPACE_DEFAULT = COLOR_SPACE_CG_709,

} COLOR_SPACE;

#define COLORSPACE_MASK		(COLOR_SPACE_BT_601|COLOR_SPACE_BT_709|COLOR_SPACE_VS_RGB)

// Range of valid color spaces encountered during decoding
#define MIN_DECODED_COLOR_SPACE		0
#define MAX_DECODED_COLOR_SPACE		15

#define COLOR_SPACE_DEFINED			1		// The color space enumeration has been defined


typedef enum frame_format
{
	FRAME_FORMAT_NORMAL = 0,		// First row is the top row
	FRAME_FORMAT_INVERTED = 1,		// First row is the bottom row

} FRAME_FORMAT;

#define FRAME_FORMAT_SHIFT		31
#define FRAME_FORMAT_MASK		0x01

#define MAKE_FORMAT(i,f)	(((i) << FRAME_FORMAT_SHIFT) | (f))

#define COLOR_SPACE_VSRGB_MASK		(COLOR_SPACE_VS_RGB << COLOR_SPACE_SHIFT)
#define COLOR_SPACE_BT709_MASK		(COLOR_SPACE_BT_709 << COLOR_SPACE_SHIFT)
#define COLOR_SPACE_422_TO_444_MASK	(COLOR_SPACE_422_TO_444 << COLOR_SPACE_SHIFT)


typedef struct {
	int red, green, blue;					// [in] Set the colors to the given values
	int brightness, saturation, contrast;	// [in] Set the color adjustments to the given values
} ColorParam;

#endif
