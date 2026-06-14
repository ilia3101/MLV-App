/*! @file ImageConverter.h

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

#pragma once

// The image converter uses packing and byte swapping from the DPX converter
#include "DPXConverter.h"

// Define the maximum value of a 16-bit unsigned integer
#ifndef UINT16_MAX
#define UINT16_MAX 0xFFFF
#endif

// Templated class for YUV to RGB conversion
template<typename CoefficientType>
class YUVToRGB
{
protected:

	YUVToRGB(ColorFlags color_flags)
	{
		// Compute the color conversion matrix when the class is instantiated
		ComputeCoefficients(color_flags);
	}

	void ComputeCoefficients(ColorFlags color_flags);

	CoefficientType C_y;
	CoefficientType C_rv;
	CoefficientType C_gv;
	CoefficientType C_gu;
	CoefficientType C_bu;

	CoefficientType luma_offset;
	CoefficientType chroma_offset;

};

// Templated class for RGB to YUV conversion
template<typename CoefficientType>
class RGBToYUV
{
protected:

	RGBToYUV(ColorFlags color_flags, uint32_t precisionArg)
	{
		precision = precisionArg;

		// Compute the color conversion matrix when the class is instantiated
		ComputeCoefficients(color_flags);
	}

	void ComputeCoefficients(ColorFlags color_flags);

	// coeffiecient must be POSITIVE
	// NOTE: How to handle non-integer CoeffiecientTypes?
	static CoefficientType ScaleCoefficientToPrecisionDiscrete(double coefficient, uint32_t precision)
	{
		return (CoefficientType)((coefficient * ((1<<precision)-1))+0.5);
	}

	CoefficientType C_yr;
	CoefficientType C_yg;
	CoefficientType C_yb;

	CoefficientType C_ur;
	CoefficientType C_ug;
	CoefficientType C_ub;

	CoefficientType C_vr;
	CoefficientType C_vg;
	CoefficientType C_vb;

	CoefficientType luma_offset;
	CoefficientType chroma_offset;

	uint32_t precision;
};

class CImageConverter
{
public:

	CImageConverter(bool sourceColorSpaceIs709 = false,
					bool sourceImageInterleaved = false) :
		m_sourceColorSpaceIs709(sourceColorSpaceIs709),
		m_sourceImageInterleaved(sourceImageInterleaved)
	{
	}

	//~CImageConverter();

protected:
	
	// Does the source image use the 709 color space?
	bool m_sourceColorSpaceIs709;

	// Is the source image interleaved?
	bool m_sourceImageInterleaved;

};


class CImageConverterYU64ToRGB : public CImageConverter
{
public:

	CImageConverterYU64ToRGB(bool sourceColorSpaceIs709 = false,
							 bool sourceImageInterlaced = false) :
		CImageConverter(sourceColorSpaceIs709,
						sourceImageInterlaced)
	{
		int color_flags = (sourceColorSpaceIs709 ? COLOR_FLAGS_CS709 : COLOR_FLAGS_BT601);
		ComputeYUVToRGBCoefficients(color_flags);
	}

	//TODO: Change the conversion code to use integer arithmetic exclusively

	// Convert a YU64 pixel to Adobe Premiere floating-point format
	void ConvertToVUYA_4444_32f(int y64, int u64, int v64, float &y, float &u, float &v)
	{
		const float adobeYoffset = -16.0f / 255.0f;
		const float adobeYgain = 255.0f / (235.0f - 16.0f);
		const float adobeCgain = 0.5f / (112.0f / 255.0f);

		float u1, v1;

		v1 = ((float)v64 / 65535.0f) - 0.5f;
		u1 = ((float)u64 / 65535.0f) - 0.5f;
		y =  ((float)y64 / 65535.0f);

		if (m_sourceColorSpaceIs709)
		{
			// Convert to 601 because Adobe floating-point is always 601
			y = y + 0.191027f * v1 + 0.099603f * u1;
			u =   - 0.109279f * v1 + 0.990013f * u1;
			v =     0.983392f * v1 - 0.072404f * u1;
		}
		else
		{
			u = u1;
			v = v1;
		}

		// convert to Adobe YUV
		y += adobeYoffset;
		y *= adobeYgain;
		u *= adobeCgain;
		v *= adobeCgain;
	}

	// Convert a row of packed YU64 pixels to Adobe floating-point format
	void ConvertToVUYA_4444_32f(int *yuv64, float *yuv, int length);

	// Convert an image of packed YU64 pixels to Adobe float-point format
	void ConvertToVUYA_4444_32f(int *yuv64, int input_pitch,
								float &yuv, int output_pitch,
								int width, int height);

	// Convert a YU64 pixel to QuickTime BGRA with 16 bits per channel
	void ConvertToBGRA64(int y, int u, int v, int &r, int &g, int &b);

	// Convert a row of YU64 pixels to QuickTime BGRA with 16 bits per channel
	void ConvertToBGRA64(unsigned char *input, unsigned char *output, int length, int swap_bytes_flag);

	// Convert an image of YU64 pixels to QuickTime BGRA with 16 bits per channel
	void ConvertToBGRA64(unsigned char *input, int input_pitch,
						 unsigned char *output, int output_pitch,
						 int width, int height, int swap_bytes_flag);

#if 0
	// Convert an image of YU64 pixels to BGRA with 15 bits per channel (for A1fter Effects)
	void ConvertToAfterEffectsBGRA64(unsigned char *input, long input_pitch,
									 unsigned char *output, long output_pitch,
									 int width, int height);
#endif
protected:

	// Initialize the coefficients for YUV to RGB conversion
	void ComputeYUVToRGBCoefficients(int color_flags);

	// The RGB limit must correspond to the coefficient for RGB = 1.0
	static const uint16_t max_rgb = UINT16_MAX;

	// Maximum alpha value for 16 bit pixels
	static const uint16_t alpha = UINT16_MAX;

	// Offset for chroma components when using 16 bits per channel
	static const int chroma_offset = 128;

	// The luma offset is set when the color conversion constants are computed
	int luma_offset;

	//TODO: Eliminate these coefficients and use integer arithmetic exclusively
	struct {
		float ymult;
		float r_vmult;
		float g_vmult;
		float g_umult;
		float b_umult;
	} fp;

	//TODO: Use integer arithmetic in the SSE2 optimized color conversion routines
	int m_ccy;		// Coefficient for converting luma to any color component
	int m_crv;		// Coefficient for converting Cr to red
	int m_cgv;		// Coefficient for converting Cr to green
	int m_cgu;		// Coefficient for converting Cb to green
	int m_cbu;		// Coefficient for converting Cb to blue

	// Could retain versions of the conversion routines that use floating-point
	// arithmetic, but the coefficients defined in this clas should be integer
	// for use by the routines that are optimized with SSE2.

};

class CImageConverterCbYCrY_10bit_2_8 : public CImageConverterYU64ToRGB
{
public:

	CImageConverterCbYCrY_10bit_2_8(bool sourceColorSpaceIs709 = false,
									bool sourceImageInterleaved = false) :
		CImageConverterYU64ToRGB(sourceColorSpaceIs709, sourceImageInterleaved)
	{
	}

	// Convert an image of Avid CbYCrY 10-bit pixels in 2.8 format to RGB48
	void ConvertToRGB48(unsigned char *input, int input_pitch,
						unsigned char *output, int output_pitch,
						int width, int height);

};

// This class does not derive from CImageConverter because it does
// not need any constants or functionality defined in that base class
class CImageConverterYU64ToYUV
{
public:

	// No color conversion constants need to be initialized for YU64 to YUV conversion
	CImageConverterYU64ToYUV()
	{
	}

	// Convert a row of YU64 pixels to the Final Cut Pro floating-point YUVA format
	void ConvertToFloatYUVA(unsigned char *input, unsigned char *output, int length);
	
	// Convert an image of YU64 pixels to the Final Cut Pro floating-point YUVA format
	void ConvertToFloatYUVA(unsigned char *input, long input_pitch,
							unsigned char *output, long output_pitch,
							int width, int height);

	// Convert an image of YU64 pixels to 8-bit YUV
	void ConvertToVUYA_4444_8u(uint8_t *input_buffer, int input_pitch,
							   uint8_t *output_buffer, int output_pitch,
							   int width, int height);

	// Convert an image of YU64 pixels to the Avid 10-bit 2.8 format
	void ConvertToAvid_CbYCrY_10bit_2_8(uint8_t *input_buffer, int input_pitch,
										uint8_t *output_buffer, int output_pitch,
										int width, int height);
};

class CImageConverterNV12ToRGB : public YUVToRGB<uint16_t>,
								 public DPX_PixelFormat
{
public:

	// Initialize the color conversion coefficients and set the byte swap flag
	CImageConverterNV12ToRGB(ColorFlags color_flags = COLOR_FLAGS_CS_709) :
		YUVToRGB<uint16_t>(color_flags),
		DPX_PixelFormat(true)
	{
	}

	// Convert a row of NV12 pixels to the DPX 10-bit RGB pixel format
	void ConvertToDPX0(uint8_t *luma_row_ptr, uint8_t *chroma_row_ptr, uint8_t *output_row_ptr, int width);

	// Convert an image of NV12 pixels to the DPX 10-bit RGB pixel format
	void ConvertToDPX0(void *input_buffer, size_t input_pitch,
					   void *output_buffer, size_t output_pitch,
					   int width, int height);

	template <typename PixelType>
	PixelType Clamp16u(PixelType value)
	{
		return (value < 0) ? 0 : ((value > UINT16_MAX) ? UINT16_MAX : value);
	}

};

class CImageConverterRGBToNV12: public RGBToYUV<uint16_t>
{
public:

	// Initialize the color conversion coefficients and set the byte swap flag
	CImageConverterRGBToNV12(ColorFlags color_flags = COLOR_FLAGS_CS_709) :
		RGBToYUV<uint16_t>(color_flags, 16 /* use 16Bit precision for all computations - which is NECESSARY for the SSE2 implementation!!!!! */ )
	{
	}

	// Converts a row of RGBA pixels to NV12
	void Convert8bitRGBAToNV12(uint8_t *src_row_ptr, uint8_t *src_row_ptr_next, uint8_t *dst_luma_row_ptr, uint8_t *dst_luma_row_ptr_next, uint8_t *dst_chroma_row_ptr, uint32_t pixWidth);

	// Converts a row of RGBA pixels to NV12 using SSE2
	// NOTE: - This SSE2 implementation dulls reds and blues and casts a slight tint to neutrals - this it is not yet fully robust...
	void Convert8bitRGBAToNV12_SSE2(uint8_t *src_row_ptr, uint8_t *src_row_ptr_next, uint8_t *dst_luma_row_ptr, uint8_t *dst_luma_row_ptr_next, uint8_t *dst_chroma_row_ptr, uint32_t pixWidth);

	// Convert a bitmap of 8Bit pixels to NV12
	// the *Index args signify the cadence of the source data - thus, if image is actually BGRA then: bIndex == 0, gIndex == 1, rIndex == 2, aIndex == 3
	void Convert8bitRGBAToNV12(void *input_buffer, size_t input_pitch,
								void *output_buffer, size_t output_pitch,
								uint32_t pixWidth, uint32_t pixHeight,
								uint32_t rIndexArg, uint32_t gIndexArg, uint32_t bIndexArg, uint32_t aIndexArg);

	// Convert a bitmap of 8Bit pixels to NV12
	// the *Index args signify the cadence of the source data - thus, if image is actually BGRA then: bIndex == 0, gIndex == 1, rIndex == 2, aIndex == 3
	void Convert8bitRGBAToNV12_SSE2(void *input_buffer, size_t input_pitch,
								void *output_buffer, size_t output_pitch,
								uint32_t pixWidth, uint32_t pixHeight,
								uint32_t rIndexArg, uint32_t gIndexArg, uint32_t bIndexArg, uint32_t aIndexArg);

	

private:
	uint32_t rIndex, gIndex, bIndex, aIndex;
};

// for Debug only (used to determine veracity of CImageConverterRGBToNV12 output)
class CImageConverterRGBToNV12_Debug: public RGBToYUV<double>
{
public:

	// Initialize the color conversion coefficients and set the byte swap flag
	CImageConverterRGBToNV12_Debug(ColorFlags color_flags = COLOR_FLAGS_CS_709) :
		RGBToYUV<double>(color_flags, 0 /* using floating point precision... */)
	{
	}

	// Converts a row of RGBA pixels to NV12
	void Convert8bitRGBAToNV12(uint8_t *src_row_ptr, uint8_t *src_row_ptr_next, uint8_t *dst_luma_row_ptr, uint8_t *dst_luma_row_ptr_next, uint8_t *dst_chroma_row_ptr, uint32_t pixWidth);

	// Convert a bitmap of 8Bit pixels to NV12
	// the *Index args signify the cadence of the source data - thus, if image is actually BGRA then: bIndex == 0, gIndex == 1, rIndex == 2, aIndex == 3
	void Convert8bitRGBAToNV12(void *input_buffer, size_t input_pitch,
								void *output_buffer, size_t output_pitch,
								uint32_t pixWidth, uint32_t pixHeight,
								uint32_t rIndexArg, uint32_t gIndexArg, uint32_t bIndexArg, uint32_t aIndexArg);

	

private:
	uint32_t rIndex, gIndex, bIndex, aIndex;
};

class CImageConverterRGB32ToQuickTime : public CImageConverter
{
public:
	CImageConverterRGB32ToQuickTime(bool sourceColorSpaceIs709 = false,
									bool sourceImageInterleaved = false) :
		CImageConverter(sourceColorSpaceIs709,
						sourceImageInterleaved)
	{
	}

};

class CImageConverterB64A : public CImageConverter
{
public:
	CImageConverterB64A(bool sourceColorSpaceIs709 = false,
						bool sourceImageInterleaved = false) :
		CImageConverter(sourceColorSpaceIs709,
						sourceImageInterleaved)
	{
	}

};

class CImageConverterRG48 : public CImageConverter
{
public:
	CImageConverterRG48(bool sourceColorSpaceIs709 = false,
						bool sourceImageInterleaved = false) :
		CImageConverter(sourceColorSpaceIs709,
						sourceImageInterleaved)
	{
	}

};

class CImageConverterRGB32 : public CImageConverter
{
public:
	CImageConverterRGB32(bool sourceColorSpaceIs709 = false,
						 bool sourceImageInterleaved = false) :
		CImageConverter(sourceColorSpaceIs709,
						sourceImageInterleaved)
	{
	}

	void ConvertToB64A(unsigned char *input, long input_pitch,
					   unsigned char *output, long output_pitch,
					   int width, int height);
};

// Convert BGRA to other RGB pixel formats
class CImageConverterBGRA : public DPX_PixelFormat
{
public:
#if 0
	CImageConverterBGRA(bool sourceColorSpaceIs709 = false,
						bool sourceImageInterleaved = false) :
		CImageConverter(sourceColorSpaceIs709,
						sourceImageInterleaved)
	{
	}
#endif

	void ConvertToDPX0(void *input_buffer, size_t input_pitch,
					   void *output_buffer, size_t output_pitch,
					   int width, int height);
};

#if _USE_SIMPLE_NAMES

// Add support for simpler class names
namespace Converter
{
	typedef CImageConverterNV12ToRGB NV12ToRGB;
}

#endif
