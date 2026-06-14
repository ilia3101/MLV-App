/*! @file ImageConverter.cpp

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

//TODO: Eliminate coding problems that cause warnings about no EMMS instruction before call
#pragma warning(disable: 964)

// Define an assert macro that can be controlled in this file
#define ASSERT(x)	assert(x)

#include "ColorFlags.h"
#include "ImageConverter.h"
//#include "ConvertLib.h"

#if !defined(_WIN64) // certain SIMD instructions are NOT supported in Win64...
#ifndef _XMMOPT
#define _XMMOPT 1					// Use SIMD instructions in this program
#endif

#define XMMOPT (1 && _XMMOPT)		// Use SIMD instructions in this module
#endif

#if XMMOPT
    #ifdef __x86_64__
        #include <emmintrin.h>             // SSE2 intrinsics
    #else
        #include "sse2neon/sse2neon.h"
    #endif
#endif

#ifdef DEBUG
#undef DEBUG
#endif

#define DEBUG  (1 && _DEBUG)

// Copied from Adobe AfterEffects 6.5 SDK file AE_Effect.h
#define PF_MAX_CHAN8			255
#define PF_MAX_CHAN16			32768


#if _WIN32

//#include <stdlib.h>

// Use the byte swapping routines defined in the standard library
#if _DEBUG
 #define SwapInt16(x) ((((x)&0xff00)>>8)|(((x)&0xff)<<8))
#else
 #define SwapInt16(x)	_byteswap_ushort(x)
#endif
#define SwapInt32(x)	_byteswap_ulong(x)

#define _SWAPBYTES	1

#elif __APPLE__

#include "CoreFoundation/CoreFoundation.h"

// Use the byte swapping routines from the Core Foundation framework
#define SwapInt16(x)	_OSSwapInt16(x)
#define SwapInt32(x)	_OSSwapInt32(x)

#define _SWAPBYTES	1
#define UINT8_MAX 255

#else

// Use the byte swapping routines that are built into GCC
#define SwapInt16(x)	__builtin_bswap16(x)
#define SwapInt32(x)	__builtin_bswap32(x)

#define _SWAPBYTES	1

#endif


//TODO: Change the following routine to use integer coefficients exclusively

// Initialize the coefficients for YUV to RGB conversion
void CImageConverterYU64ToRGB::ComputeYUVToRGBCoefficients(int color_flags)
{
	// Initialize the color conversion constants (floating-point version)
	switch (color_flags & COLOR_FLAGS_MASK)
	{
		case 0:				// Computer systems 601
		luma_offset = 16;
		fp.ymult = 1.164f;
		fp.r_vmult = 1.596f;
		fp.g_vmult = 0.813f;
		fp.g_umult = 0.391f;
		fp.b_umult = 2.018f;
		break;

		case VSRGB:			// Video systems 601
		luma_offset = 0;
		fp.ymult = 1.0;
		fp.r_vmult = 1.371f;
		fp.g_vmult = 0.698f;
		fp.g_umult = 0.336f;
		fp.b_umult = 1.732f;
		break;

		case CS709:			// Computer systems 709
		luma_offset = 16;
		fp.ymult = 1.164f;
		fp.r_vmult = 1.793f;
		fp.g_vmult = 0.534f;
		fp.g_umult = 0.213f;
		fp.b_umult = 2.115f;
		break;

		case CS709+VSRGB:	// Video Systems 709
		luma_offset = 0;
		fp.ymult = 1.0;
		fp.r_vmult = 1.540f;
		fp.g_vmult = 0.459f;
		fp.g_umult = 0.183f;
		fp.b_umult = 1.816f;
		break;
	}

	// Initialize the color conversion constants (integer version)
	switch (color_flags & COLOR_FLAGS_MASK)
	{
		case 0:				// Computer systems 601
		luma_offset = 16;
		m_ccy = 9535;		// 1.164
		m_crv = 13074;		// 1.596
		m_cgv = 6660;		// 0.813
		m_cgu = 3203;		// 0.391
		m_cbu = 16531;		// 2.018
		break;

		case VSRGB:			// Video systems 601
		luma_offset = 0;
		m_ccy = 8192;		// 1.0
		m_crv = 11231;		// 1.371
		m_cgv = 5718;		// 0.698
		m_cgu = 2753;		// 0.336
		m_cbu = 14189;		// 1.732
		break;

		case CS709:			// Computer systems 709
		luma_offset = 16;
		m_ccy = 9535;		// 1.164
		m_crv = 14688;		// 1.793
		m_cgv = 4375;		// 0.534
		m_cgu = 1745;		// 0.213
		m_cbu = 17326;		// 2.115
		break;

		case CS709+VSRGB:	// Video Systems 709
		luma_offset = 0;
		m_ccy = 8192;		// 1.0
		m_crv = 12616;		// 1.540
		m_cgv = 3760;		// 0.459
		m_cgu = 1499;		// 0.183
		m_cbu = 14877;		// 1.816
		break;
	}

	//mid_luma = (luma_offset << offset_shift);
	//mid_chroma = (chroma_offset << offset_shift);
}

// Convert a YU64 pixel to QuickTime BGRA with 16 bits per channel
void CImageConverterYU64ToRGB::ConvertToBGRA64(int y, int u, int v, int &r, int &g, int &b)
{
	//const float y_scale = (255 << 8);
	//const float u_scale = (255 << 8);
	//const float v_scale = (255 << 8);

	// Convert the 8-bit luma and chroma offsets to 16 bits
	const int offset_shift = 8;
	int mid_luma = (luma_offset << offset_shift);
	int mid_chroma = (chroma_offset << offset_shift);

	int y1_input = y;
	//int y2_input;
	int u1_input = u;
	int v1_input = v;

	float y1;
	//float y2;
	float u1;
	float v1;

	float r1;
	float g1;
	float b1;

	float t1;
	float t2;

	int r1_out;
	int g1_out;
	int b1_out;

	// Subtract the luma and chroma offsets
	y1_input = y1_input - mid_luma;
	//y2_input = y2_input - mid_luma;
	u1_input = u1_input - mid_chroma;
	v1_input = v1_input - mid_chroma;

	// Convert YUV to floating-point
	y1 = (float)y1_input;
	//y2 = (float)y2_input;
	u1 = (float)u1_input;
	v1 = (float)v1_input;


	/***** First RGB tuple *****/

	// Convert YUV to RGB
	r1 = fp.ymult * y1;
	t1 = fp.r_vmult * u1;
	r1 += t1;

	g1 = fp.ymult * y1;
	t1 = fp.g_vmult * u1;
	g1 -= t1;
	t2 = fp.g_umult * v1;
	g1 -= t2;

	b1 = fp.ymult * y1;
	t1 = fp.b_umult * v1;
	b1 += t1;

	r1_out = (int)r1;
	g1_out = (int)g1;
	b1_out = (int)b1;

	// Force the RGB values into valid range
	if (r1_out < 0) r1_out = 0;
	if (g1_out < 0) g1_out = 0;
	if (b1_out < 0) b1_out = 0;

	if (r1_out > max_rgb) r1_out = max_rgb;
	if (g1_out > max_rgb) g1_out = max_rgb;
	if (b1_out > max_rgb) b1_out = max_rgb;

	r = r1_out;
	g = g1_out;
	b = b1_out;

#if 0
	/***** Second RGB tuple *****/

	// Convert YUV to RGB
	r1 = ymult * y2;
	t1 = r_vmult * u1;
	r1 += t1;

	g1 = ymult * y2;
	t1 = g_vmult * u1;
	g1 -= t1;
	t2 = g_umult * v1;
	g1 -= t2;

	b1 = ymult * y2;
	t1 = b_umult * v1;
	b1 += t1;

	r1_out = r1;
	g1_out = g1;
	b1_out = b1;

	// Force the RGB values into valid range
	if (r1_out < 0) r1_out = 0;
	if (g1_out < 0) g1_out = 0;
	if (b1_out < 0) b1_out = 0;

	if (r1_out > max_rgb) r1_out = max_rgb;
	if (g1_out > max_rgb) g1_out = max_rgb;
	if (b1_out > max_rgb) b1_out = max_rgb;

	r = r1_out;
	g = g1_out;
	b = b1_out;
#endif
}

// Convert a row of YU64 pixels to QuickTime BGRA with 16 bits per channel
void CImageConverterYU64ToRGB::ConvertToBGRA64(unsigned char *input,
											   unsigned char *output,
											   int length,
											   int swap_bytes_flag)
{
	unsigned short *yuvptr = (unsigned short *)input;
	unsigned short *outptr = (unsigned short *)output;

	// Compute the maximum value to apply before shifting up to full scale
	const int post_shift = 3;
	const int pre_clamp = (0x7FFF - (max_rgb >> post_shift));

	// Scale the conversion constants
	int cry = (m_ccy << 1);
	int cru = (m_crv << 1);

	int cgy = (m_ccy << 1);
	int cgu = (m_cgv << 1);
	int cgv = (m_cgu << 1);

	int cby = (m_ccy << 1);
	int cbv = (m_cbu << 0);

	// The luma and chroma are shifted right by one bit before subtracting the offset
	// so the offsets are shifted by 7 bits instead of 8 bits as would be expected
	const int offset_shift = 7;

	int mid_luma = (luma_offset << offset_shift);
	int mid_chroma = (chroma_offset << offset_shift);

	// The row length must be an even number
	assert((length % 2) == 0);

	// Start processing at the beginning of the row
	int column = 0;

#if (1 && XMMOPT)

	// Definitions used in the fast loop
	const int column_step = 4;
	const int post_column = length - (length % column_step);

	__m64 *yuv_input_ptr = (__m64 *)yuvptr;
	__m64 *argb_output_ptr = (__m64 *)outptr;

	__m64 luma_offset_pi16 = _mm_set1_pi16(mid_luma);
	__m64 chroma_offset_pi16 = _mm_set1_pi16(mid_chroma);

	__m64 alpha_pi16 = _mm_set1_pi16(alpha);


	// YUV to RGB conversion constants
	__m64 cry_pi16 = _mm_set1_pi16(cry);
	__m64 cru_pi16 = _mm_set1_pi16(cru);

	__m64 cgy_pi16 = _mm_set1_pi16(cgy);
	__m64 cgu_pi16 = _mm_set1_pi16(cgu);
	__m64 cgv_pi16 = _mm_set1_pi16(cgv);

	__m64 cby_pi16 = _mm_set1_pi16(cby);
	__m64 cbv_pi16 = _mm_set1_pi16(cbv);


	//TODO: The post processing code has not been debugged
	#pragma warning(push)
	#pragma warning(disable: 964)
	assert(post_column == length);
	#pragma warning(pop)

	for (; column < post_column; column += column_step)
	{
		__m64 yuv1_pi16;
		__m64 yuv2_pi16;

		__m64 uv1_pi16;
		__m64 uv2_pi16;

		__m64 y1_pi16;
		//__m64 y2_pi16;

		__m64 u1_pi16;
		//__m64 u2_pi16;

		//__m64 u1a_pi16;
		//__m64 v1a_pi16;

		__m64 v1_pi16;
		//__m64 v2_pi16;

		__m64 r1_pi16;
		__m64 g1_pi16;
		__m64 b1_pi16;

		//__m64 r2_pi16;
		//__m64 g2_pi16;
		//__m64 b2_pi16;

		__m64 t1_pi16;
		//__m64 t2_pi16;

		__m64 out1_pi16;
		__m64 out2_pi16;
		__m64 argb_pi16;

#if _SWAPBYTES
		__m64 tmp1_pi16;
		__m64 tmp2_pi16;
#endif
		//__m64 clamp_pi16 = _mm_set1_pi16(0x7FFF - max_rgb);
		//__m64 clamp_pi16 = _mm_set1_pi16(0x7FFF - ymult);
		__m64 clamp_pi16 = _mm_set1_pi16(pre_clamp);

#if (1 && DEBUG)
		int r1;
		int g1;
		int b1;
#endif
		// Load two quads of packed luma and chroma
		yuv1_pi16 = *(yuv_input_ptr++);
		yuv2_pi16 = *(yuv_input_ptr++);

		// Unpack and duplicate the chroma
		uv1_pi16 = _mm_shuffle_pi16(yuv1_pi16, _MM_SHUFFLE(3, 3, 1, 1));
		uv2_pi16 = _mm_shuffle_pi16(yuv2_pi16, _MM_SHUFFLE(3, 3, 1, 1));

		u1_pi16 = _mm_unpacklo_pi32(uv1_pi16, uv2_pi16);
		v1_pi16 = _mm_unpackhi_pi32(uv1_pi16, uv2_pi16);

		// Unpack the luma
		yuv1_pi16 = _mm_shuffle_pi16(yuv1_pi16, _MM_SHUFFLE(2, 0, 2, 0));
		yuv2_pi16 = _mm_shuffle_pi16(yuv2_pi16, _MM_SHUFFLE(2, 0, 2, 0));

		y1_pi16 = _mm_unpacklo_pi32(yuv1_pi16, yuv2_pi16);

#if (0 && DEBUG)
		y1_pi16 = _mm_set1_pi16(debug_luma);
		//y2_pi16 = _mm_set1_pi16(debug_luma);
		u1_pi16 = _mm_set1_pi16(debug_chroma);
		v1_pi16 = _mm_set1_pi16(debug_chroma);
#endif
		// Subtract the luma and chroma offsets
		y1_pi16 = _mm_srli_pi16(y1_pi16, 1);
		y1_pi16 = _mm_sub_pi16(y1_pi16, luma_offset_pi16);
		//y2_pi16 = _mm_sub_pi16(y2_pi16, luma_offset_pi16);
		u1_pi16 = _mm_srli_pi16(u1_pi16, 1);
		u1_pi16 = _mm_sub_pi16(u1_pi16, chroma_offset_pi16);
		v1_pi16 = _mm_srli_pi16(v1_pi16, 1);
		v1_pi16 = _mm_sub_pi16(v1_pi16, chroma_offset_pi16);

		// Duplicate the u and v chroma
		//u2_pi16 = _mm_unpackhi_pi16(u1_pi16, u1_pi16);
		//u1_pi16 = _mm_unpacklo_pi16(u1_pi16, u1_pi16);

		//v2_pi16 = _mm_unpackhi_pi16(v1_pi16, v1_pi16);
		//v1_pi16 = _mm_unpacklo_pi16(v1_pi16, v1_pi16);

		// Compute the red channel
		r1_pi16 = _mm_mulhi_pi16(cry_pi16, y1_pi16);
		t1_pi16 = _mm_mulhi_pi16(cru_pi16, u1_pi16);
		r1_pi16 = _mm_adds_pi16(r1_pi16, clamp_pi16);
		r1_pi16 = _mm_adds_pi16(r1_pi16, t1_pi16);
		r1_pi16 = _mm_subs_pu16(r1_pi16, clamp_pi16);
#if 0
		r2_pi16 = _mm_mulhi_pi16(cry_pi16, y2_pi16);
		t2_pi16 = _mm_mulhi_pi16(cru_pi16, u2_pi16);
		r2_pi16 = _mm_adds_pi16(r2_pi16, clamp_pi16);
		r2_pi16 = _mm_adds_pi16(r2_pi16, t2_pi16);
		r2_pi16 = _mm_subs_pu16(r2_pi16, clamp_pi16);
#endif
		// Compute the green channel
		g1_pi16 = _mm_mulhi_pi16(cgy_pi16, y1_pi16);
		t1_pi16 = _mm_mulhi_pi16(cgu_pi16, u1_pi16);
		g1_pi16 = _mm_adds_pi16(g1_pi16, clamp_pi16);
		g1_pi16 = _mm_subs_pi16(g1_pi16, t1_pi16);
		t1_pi16 = _mm_mulhi_pi16(cgv_pi16, v1_pi16);
		g1_pi16 = _mm_subs_pi16(g1_pi16, t1_pi16);
		g1_pi16 = _mm_subs_pu16(g1_pi16, clamp_pi16);
#if 0
		g2_pi16 = _mm_mulhi_pi16(cgy_pi16, y2_pi16);
		t2_pi16 = _mm_mulhi_pi16(cgu_pi16, u2_pi16);
		g2_pi16 = _mm_adds_pi16(g2_pi16, clamp_pi16);
		g2_pi16 = _mm_subs_pi16(g2_pi16, t2_pi16);
		t2_pi16 = _mm_mulhi_pi16(cgv_pi16, v2_pi16);
		g2_pi16 = _mm_subs_pi16(g2_pi16, t2_pi16);
		g2_pi16 = _mm_subs_pu16(g2_pi16, clamp_pi16);
#endif
		// Compute the blue channel
		b1_pi16 = _mm_mulhi_pi16(cby_pi16, y1_pi16);
		t1_pi16 = _mm_mulhi_pi16(cbv_pi16, v1_pi16);
		b1_pi16 = _mm_adds_pi16(b1_pi16, clamp_pi16);
		b1_pi16 = _mm_adds_pi16(b1_pi16, t1_pi16);
		b1_pi16 = _mm_adds_pi16(b1_pi16, t1_pi16); // cbv was at half scale
		b1_pi16 = _mm_subs_pu16(b1_pi16, clamp_pi16);
#if 0
		b2_pi16 = _mm_mulhi_pi16(cby_pi16, y2_pi16);
		t2_pi16 = _mm_mulhi_pi16(cbv_pi16, v2_pi16);
		b2_pi16 = _mm_adds_pi16(b2_pi16, clamp_pi16);
		b2_pi16 = _mm_adds_pi16(b2_pi16, t2_pi16);
		b2_pi16 = _mm_adds_pi16(b2_pi16, t2_pi16); // cbv was at half scale
		b2_pi16 = _mm_subs_pu16(b2_pi16, clamp_pi16);
#endif
#if (1 && DEBUG)
		r1 = _mm_extract_pi16(r1_pi16, 0);
		g1 = _mm_extract_pi16(g1_pi16, 0);
		b1 = _mm_extract_pi16(b1_pi16, 0);

		//r2 = _mm_extract_pi16(r2_pi16, 0);
		//g2 = _mm_extract_pi16(g2_pi16, 0);
		//b2 = _mm_extract_pi16(b2_pi16, 0);
#endif
		r1_pi16 = _mm_slli_pi16(r1_pi16, post_shift);
		g1_pi16 = _mm_slli_pi16(g1_pi16, post_shift);
		b1_pi16 = _mm_slli_pi16(b1_pi16, post_shift);
#if 0
		r2_pi16 = _mm_slli_pi16(r2_pi16, post_shift);
		g2_pi16 = _mm_slli_pi16(g2_pi16, post_shift);
		b2_pi16 = _mm_slli_pi16(b2_pi16, post_shift);
#endif
#if (1 && DEBUG)
		r1 = _mm_extract_pi16(r1_pi16, 0);
		g1 = _mm_extract_pi16(g1_pi16, 0);
		b1 = _mm_extract_pi16(b1_pi16, 0);

		//r2 = _mm_extract_pi16(r2_pi16, 0);
		//g2 = _mm_extract_pi16(g2_pi16, 0);
		//b2 = _mm_extract_pi16(b2_pi16, 0);
#endif
#if 0
		// Adjust the components to the requested size
		r1_pi16 = _mm_srli_pi16(r1_pi16, component_shift);
		g1_pi16 = _mm_srli_pi16(g1_pi16, component_shift);
		b1_pi16 = _mm_srli_pi16(b1_pi16, component_shift);
#endif
		// Interleave the ARGB values (first pair)
		out1_pi16 = _mm_unpacklo_pi16(alpha_pi16, g1_pi16);
		out2_pi16 = _mm_unpacklo_pi16(r1_pi16, b1_pi16);

		// Interleave and store the first ARGB quadruple
		argb_pi16 = _mm_unpacklo_pi16(out1_pi16, out2_pi16);

#if _SWAPBYTES
		if (swap_bytes_flag)
		{
			// Each color component is big endian on the Macintosh
			tmp1_pi16 = _mm_slli_pi16(argb_pi16, 8);
			tmp2_pi16 = _mm_srli_pi16(argb_pi16, 8);
			argb_pi16 = _mm_or_si64(tmp1_pi16, tmp2_pi16);
		}
#endif
		*(argb_output_ptr++) = argb_pi16;

		// Interleave and store the second ARGB quadruple
		argb_pi16 = _mm_unpackhi_pi16(out1_pi16, out2_pi16);

#if _SWAPBYTES
		if (swap_bytes_flag)
		{
			// Each color component is big endian on the Macintosh
			tmp1_pi16 = _mm_slli_pi16(argb_pi16, 8);
			tmp2_pi16 = _mm_srli_pi16(argb_pi16, 8);
			argb_pi16 = _mm_or_si64(tmp1_pi16, tmp2_pi16);
		}
#endif
		*(argb_output_ptr++) = argb_pi16;

		// Interleave the ARGB values (second pair)
		out1_pi16 = _mm_unpackhi_pi16(alpha_pi16, g1_pi16);
		out2_pi16 = _mm_unpackhi_pi16(r1_pi16, b1_pi16);

		// Interleave and store the third ARGB quadruple
		argb_pi16 = _mm_unpacklo_pi16(out1_pi16, out2_pi16);

#if _SWAPBYTES
		if (swap_bytes_flag)
		{
			// Each color component is big endian on the Macintosh
			tmp1_pi16 = _mm_slli_pi16(argb_pi16, 8);
			tmp2_pi16 = _mm_srli_pi16(argb_pi16, 8);
			argb_pi16 = _mm_or_si64(tmp1_pi16, tmp2_pi16);
		}
#endif
		// Store the third ARGB quadruple
		*(argb_output_ptr++) = argb_pi16;

		// Interleave and store the fourth ARGB quadruple
		argb_pi16 = _mm_unpackhi_pi16(out1_pi16, out2_pi16);

#if _SWAPBYTES
		if (swap_bytes_flag)
		{
			// Each color component is big endian on the Macintosh
			tmp1_pi16 = _mm_slli_pi16(argb_pi16, 8);
			tmp2_pi16 = _mm_srli_pi16(argb_pi16, 8);
			argb_pi16 = _mm_or_si64(tmp1_pi16, tmp2_pi16);
		}
#endif
		// Store the fourth ARGB quadruple
		*(argb_output_ptr++) = argb_pi16;
#if 0
		// Interleave the ARGB values (third pair)
		out1_pi16 = _mm_unpacklo_pi16(alpha_pi16, g2_pi16);
		out2_pi16 = _mm_unpacklo_pi16(r2_pi16, b2_pi16);

		// Interleave and store the fifth ARGB quadruple
		argb_pi16 = _mm_unpacklo_pi16(out1_pi16, out2_pi16);
		*(argb_output_ptr++) = argb_pi16;

		// Interleave and store the sixth ARGB quadruple
		argb_pi16 = _mm_unpackhi_pi16(out1_pi16, out2_pi16);
		*(argb_output_ptr++) = argb_pi16;

		// Interleave the ARGB values (fourth pair)
		out1_pi16 = _mm_unpackhi_pi16(alpha_pi16, g2_pi16);
		out2_pi16 = _mm_unpackhi_pi16(r2_pi16, b2_pi16);

		// Interleave and store the seventh ARGB quadruple
		argb_pi16 = _mm_unpacklo_pi16(out1_pi16, out2_pi16);
		*(argb_output_ptr++) = argb_pi16;

		// Interleave and store the eighth ARGB quadruple
		argb_pi16 = _mm_unpackhi_pi16(out1_pi16, out2_pi16);
		*(argb_output_ptr++) = argb_pi16;
#endif
	}

	//y_src_ptr = (UINT16 *)y_input_ptr;
	//u_src_ptr = (UINT16 *)u_input_ptr;
	//v_src_ptr = (UINT16 *)v_input_ptr;

	yuvptr = (unsigned short *)yuv_input_ptr;
	outptr = (unsigned short *)argb_output_ptr;

	_mm_empty();

#endif //(1 && XMMOPT)

#if 0
	// Process the rest of the row
	for(; column < width; column += 2)
	{
		int y1, y2;
		int u1, u2;
		int v1, v2;
		int r1, r2;
		int g1, g2;
		int b1, b2;
		int t1, t2;

		// Get the next two pixels
		y1 = *(y_src_ptr++);
		u1 = *(u_src_ptr++);
		y2 = *(y_src_ptr++);
		v1 = *(v_src_ptr++);

		// Subtract the luma and chroma offsets
		y1 = y1 - mid_luma;
		y2 = y2 - mid_luma;
		u1 = u1 - mid_chroma;
		v1 = v1 - mid_chroma;

		// Convert YUV to RGB
		r1 = mulhu(cry, y1);
		t1 = mulhi(cru, u1);
		r1 = adds(r1, t1);

		r2 = mulhu(cry, y2);
		t2 = mulhi(cru, u1);
		r2 = adds(r2, t2);

		g1 = mulhu(cgy, y1);
		t1 = mulhi(cgu, u1);
		g1 = subs(g1, t1);
		t2 = mulhi(cgv, v1);
		g1 = subs(g1, t2);

		g2 = mulhu(cgy, y2);
		t1 = mulhi(cgu, u1);
		g2 = subs(g1, t1);
		t2 = mulhi(cgv, v1);
		g2 = subs(g2, t2);

		b1 = mulhu(cby, y1);
		t1 = mulhi(cbv, v1);
		b1 = adds(b1, t1);

		b2 = mulhu(cby, y2);
		t2 = mulhi(cbv, v1);
		b2 = adds(b2, t2);

		// Scale the RGB values into the output range from zero to (1 << 15)
		r1 <<= 2;
		g1 <<= 2;
		b1 <<= 2;

		r2 <<= 2;
		g2 <<= 2;
		b2 <<= 2;

		// Force the RGB values into valid range
		if (r1 < 0) r1 = 0;
		if (g1 < 0) g1 = 0;
		if (b1 < 0) b1 = 0;

		if (r1 > PF_MAX_CHAN16) r1 = PF_MAX_CHAN16;
		if (g1 > PF_MAX_CHAN16) g1 = PF_MAX_CHAN16;
		if (b1 > PF_MAX_CHAN16) b1 = PF_MAX_CHAN16;

		if (r2 < 0) r2 = 0;
		if (g2 < 0) g2 = 0;
		if (b2 < 0) b2 = 0;

		if (r2 > PF_MAX_CHAN16) r2 = PF_MAX_CHAN16;
		if (g2 > PF_MAX_CHAN16) g2 = PF_MAX_CHAN16;
		if (b2 > PF_MAX_CHAN16) b2 = PF_MAX_CHAN16;

		// Store the RGB values in the output buffer
		*(outptr++) = alpha;
		*(outptr++) = r1;
		*(outptr++) = g1;
		*(outptr++) = b1;

		*(outptr++) = alpha;
		*(outptr++) = r2;
		*(outptr++) = g2;
		*(outptr++) = b2;
	}
#endif
}


#if 0

// Convert an image of YU64 pixels to QuickTime BGRA with 16 bits per channel
void CImageConverterYU64ToRGB::ConvertToBGRA64(unsigned char *input, int input_pitch,
											   unsigned char *output, int output_pitch,
											   int width, int height)
{
	const int color_flags = 0;
	//const int color_flags = COLOR_FLAGS_CS709;
	const int pixel_size = 16;
	const int component_size = 16;
	const int alpha = (1 << pixel_size) - 1;

	ConvertYU64ToARGB64(input, input_pitch,
						output, output_pitch,
						width, height,
						color_flags, pixel_size,
						component_size, alpha);
}

#else

// Convert YU64 to ARGB with 16-bit pixels
void CImageConverterYU64ToRGB::ConvertToBGRA64(unsigned char *input, int input_pitch,
											   unsigned char *output, int output_pitch,
											   int width, int height, int swap_bytes_flag)
{
	unsigned char *output_row_ptr = output;

	// Compute the maximum value to apply before shifting up to full scale
	const int post_shift = 3;
	const int pre_clamp = (0x7FFF - (max_rgb >> post_shift));

	// Scale the conversion constants
	int cry = (m_ccy << 1);
	int cru = (m_crv << 1);

	int cgy = (m_ccy << 1);
	int cgu = (m_cgv << 1);
	int cgv = (m_cgu << 1);

	int cby = (m_ccy << 1);
	int cbv = (m_cbu << 0);

	// The luma and chroma are shifted right by one bit before subtracting the offset
	// so the offsets are shifted by 7 bits instead of 8 bits as would be expected
	const int offset_shift = 7;

	int mid_luma = (luma_offset << offset_shift);
	int mid_chroma = (chroma_offset << offset_shift);

	// The row length must be an even number
	assert((width % 2) == 0);

	for (int row = 0; row < height; row++)
	{
		unsigned short *yuvptr = (unsigned short *)(input + row * input_pitch);
		unsigned short *outptr = (unsigned short *)output_row_ptr;
#if 0
		ConvertToBGRA64(yuvptr, outptr, width);
#else
		// Start processing at the beginning of the row
		int column = 0;

#if (1 && XMMOPT)

		//TODO: Split the code into two routines to avoid the test for byte swapping in the fast loop

		// Definitions used in the fast loop
		const int column_step = 4;
		const int post_column = width - (width % column_step);

		__m64 *yuv_input_ptr = (__m64 *)yuvptr;
		__m64 *argb_output_ptr = (__m64 *)outptr;

		__m64 luma_offset_pi16 = _mm_set1_pi16(mid_luma);
		__m64 chroma_offset_pi16 = _mm_set1_pi16(mid_chroma);

		__m64 alpha_pi16 = _mm_set1_pi16(alpha);


		// YUV to RGB conversion constants
		__m64 cry_pi16 = _mm_set1_pi16(cry);
		__m64 cru_pi16 = _mm_set1_pi16(cru);

		__m64 cgy_pi16 = _mm_set1_pi16(cgy);
		__m64 cgu_pi16 = _mm_set1_pi16(cgu);
		__m64 cgv_pi16 = _mm_set1_pi16(cgv);

		__m64 cby_pi16 = _mm_set1_pi16(cby);
		__m64 cbv_pi16 = _mm_set1_pi16(cbv);


		//TODO: The post processing code has not been debugged
		#pragma warning(push)
		#pragma warning(disable: 964)
		assert(post_column == width);
		#pragma warning(pop)

		for (; column < post_column; column += column_step)
		{
			__m64 yuv1_pi16;
			__m64 yuv2_pi16;

			__m64 uv1_pi16;
			__m64 uv2_pi16;

			__m64 y1_pi16;
			//__m64 y2_pi16;

			__m64 u1_pi16;
			//__m64 u2_pi16;

			//__m64 u1a_pi16;
			//__m64 v1a_pi16;

			__m64 v1_pi16;
			//__m64 v2_pi16;

			__m64 r1_pi16;
			__m64 g1_pi16;
			__m64 b1_pi16;

			//__m64 r2_pi16;
			//__m64 g2_pi16;
			//__m64 b2_pi16;

			__m64 t1_pi16;
			//__m64 t2_pi16;

			__m64 out1_pi16;
			__m64 out2_pi16;
			__m64 argb_pi16;

#if _SWAPBYTES
			__m64 tmp1_pi16;
			__m64 tmp2_pi16;
#endif
			//__m64 clamp_pi16 = _mm_set1_pi16(0x7FFF - max_rgb);
			//__m64 clamp_pi16 = _mm_set1_pi16(0x7FFF - ymult);
			__m64 clamp_pi16 = _mm_set1_pi16(pre_clamp);

#if (1 && DEBUG)
			int r1;
			int g1;
			int b1;
#endif
			// Load two quads of packed luma and chroma
			yuv1_pi16 = *(yuv_input_ptr++);
			yuv2_pi16 = *(yuv_input_ptr++);

			// Unpack and duplicate the chroma
			uv1_pi16 = _mm_shuffle_pi16(yuv1_pi16, _MM_SHUFFLE(3, 3, 1, 1));
			uv2_pi16 = _mm_shuffle_pi16(yuv2_pi16, _MM_SHUFFLE(3, 3, 1, 1));

			u1_pi16 = _mm_unpacklo_pi32(uv1_pi16, uv2_pi16);
			v1_pi16 = _mm_unpackhi_pi32(uv1_pi16, uv2_pi16);

			// Unpack the luma
			yuv1_pi16 = _mm_shuffle_pi16(yuv1_pi16, _MM_SHUFFLE(2, 0, 2, 0));
			yuv2_pi16 = _mm_shuffle_pi16(yuv2_pi16, _MM_SHUFFLE(2, 0, 2, 0));

			y1_pi16 = _mm_unpacklo_pi32(yuv1_pi16, yuv2_pi16);

#if (0 && DEBUG)
			y1_pi16 = _mm_set1_pi16(debug_luma);
			//y2_pi16 = _mm_set1_pi16(debug_luma);
			u1_pi16 = _mm_set1_pi16(debug_chroma);
			v1_pi16 = _mm_set1_pi16(debug_chroma);
#endif
			// Subtract the luma and chroma offsets
			y1_pi16 = _mm_srli_pi16(y1_pi16, 1);
			y1_pi16 = _mm_sub_pi16(y1_pi16, luma_offset_pi16);
			//y2_pi16 = _mm_sub_pi16(y2_pi16, luma_offset_pi16);
			u1_pi16 = _mm_srli_pi16(u1_pi16, 1);
			u1_pi16 = _mm_sub_pi16(u1_pi16, chroma_offset_pi16);
			v1_pi16 = _mm_srli_pi16(v1_pi16, 1);
			v1_pi16 = _mm_sub_pi16(v1_pi16, chroma_offset_pi16);

			// Duplicate the u and v chroma
			//u2_pi16 = _mm_unpackhi_pi16(u1_pi16, u1_pi16);
			//u1_pi16 = _mm_unpacklo_pi16(u1_pi16, u1_pi16);

			//v2_pi16 = _mm_unpackhi_pi16(v1_pi16, v1_pi16);
			//v1_pi16 = _mm_unpacklo_pi16(v1_pi16, v1_pi16);

			// Compute the red channel
			r1_pi16 = _mm_mulhi_pi16(cry_pi16, y1_pi16);
			t1_pi16 = _mm_mulhi_pi16(cru_pi16, u1_pi16);
			r1_pi16 = _mm_adds_pi16(r1_pi16, clamp_pi16);
			r1_pi16 = _mm_adds_pi16(r1_pi16, t1_pi16);
			r1_pi16 = _mm_subs_pu16(r1_pi16, clamp_pi16);
#if 0
			r2_pi16 = _mm_mulhi_pi16(cry_pi16, y2_pi16);
			t2_pi16 = _mm_mulhi_pi16(cru_pi16, u2_pi16);
			r2_pi16 = _mm_adds_pi16(r2_pi16, clamp_pi16);
			r2_pi16 = _mm_adds_pi16(r2_pi16, t2_pi16);
			r2_pi16 = _mm_subs_pu16(r2_pi16, clamp_pi16);
#endif
			// Compute the green channel
			g1_pi16 = _mm_mulhi_pi16(cgy_pi16, y1_pi16);
			t1_pi16 = _mm_mulhi_pi16(cgu_pi16, u1_pi16);
			g1_pi16 = _mm_adds_pi16(g1_pi16, clamp_pi16);
			g1_pi16 = _mm_subs_pi16(g1_pi16, t1_pi16);
			t1_pi16 = _mm_mulhi_pi16(cgv_pi16, v1_pi16);
			g1_pi16 = _mm_subs_pi16(g1_pi16, t1_pi16);
			g1_pi16 = _mm_subs_pu16(g1_pi16, clamp_pi16);
#if 0
			g2_pi16 = _mm_mulhi_pi16(cgy_pi16, y2_pi16);
			t2_pi16 = _mm_mulhi_pi16(cgu_pi16, u2_pi16);
			g2_pi16 = _mm_adds_pi16(g2_pi16, clamp_pi16);
			g2_pi16 = _mm_subs_pi16(g2_pi16, t2_pi16);
			t2_pi16 = _mm_mulhi_pi16(cgv_pi16, v2_pi16);
			g2_pi16 = _mm_subs_pi16(g2_pi16, t2_pi16);
			g2_pi16 = _mm_subs_pu16(g2_pi16, clamp_pi16);
#endif
			// Compute the blue channel
			b1_pi16 = _mm_mulhi_pi16(cby_pi16, y1_pi16);
			t1_pi16 = _mm_mulhi_pi16(cbv_pi16, v1_pi16);
			b1_pi16 = _mm_adds_pi16(b1_pi16, clamp_pi16);
			b1_pi16 = _mm_adds_pi16(b1_pi16, t1_pi16);
			b1_pi16 = _mm_adds_pi16(b1_pi16, t1_pi16); // cbv was at half scale
			b1_pi16 = _mm_subs_pu16(b1_pi16, clamp_pi16);
#if 0
			b2_pi16 = _mm_mulhi_pi16(cby_pi16, y2_pi16);
			t2_pi16 = _mm_mulhi_pi16(cbv_pi16, v2_pi16);
			b2_pi16 = _mm_adds_pi16(b2_pi16, clamp_pi16);
			b2_pi16 = _mm_adds_pi16(b2_pi16, t2_pi16);
			b2_pi16 = _mm_adds_pi16(b2_pi16, t2_pi16); // cbv was at half scale
			b2_pi16 = _mm_subs_pu16(b2_pi16, clamp_pi16);
#endif
#if (1 && DEBUG)
			r1 = _mm_extract_pi16(r1_pi16, 0);
			g1 = _mm_extract_pi16(g1_pi16, 0);
			b1 = _mm_extract_pi16(b1_pi16, 0);

			//r2 = _mm_extract_pi16(r2_pi16, 0);
			//g2 = _mm_extract_pi16(g2_pi16, 0);
			//b2 = _mm_extract_pi16(b2_pi16, 0);
#endif
			r1_pi16 = _mm_slli_pi16(r1_pi16, post_shift);
			g1_pi16 = _mm_slli_pi16(g1_pi16, post_shift);
			b1_pi16 = _mm_slli_pi16(b1_pi16, post_shift);
#if 0
			r2_pi16 = _mm_slli_pi16(r2_pi16, post_shift);
			g2_pi16 = _mm_slli_pi16(g2_pi16, post_shift);
			b2_pi16 = _mm_slli_pi16(b2_pi16, post_shift);
#endif
#if (1 && DEBUG)
			r1 = _mm_extract_pi16(r1_pi16, 0);
			g1 = _mm_extract_pi16(g1_pi16, 0);
			b1 = _mm_extract_pi16(b1_pi16, 0);

			//r2 = _mm_extract_pi16(r2_pi16, 0);
			//g2 = _mm_extract_pi16(g2_pi16, 0);
			//b2 = _mm_extract_pi16(b2_pi16, 0);
#endif
#if 0
			// Adjust the components to the requested size
			r1_pi16 = _mm_srli_pi16(r1_pi16, component_shift);
			g1_pi16 = _mm_srli_pi16(g1_pi16, component_shift);
			b1_pi16 = _mm_srli_pi16(b1_pi16, component_shift);
#endif
			// Interleave the ARGB values (first pair)
			out1_pi16 = _mm_unpacklo_pi16(alpha_pi16, g1_pi16);
			out2_pi16 = _mm_unpacklo_pi16(r1_pi16, b1_pi16);

			// Interleave the first ARGB quadruple
			argb_pi16 = _mm_unpacklo_pi16(out1_pi16, out2_pi16);

#if _SWAPBYTES
			if (swap_bytes_flag)
			{
				// Each color component is big endian on the Macintosh
				tmp1_pi16 = _mm_slli_pi16(argb_pi16, 8);
				tmp2_pi16 = _mm_srli_pi16(argb_pi16, 8);
				argb_pi16 = _mm_or_si64(tmp1_pi16, tmp2_pi16);
			}
#endif
			// Store the first ARGB quadruple
			*(argb_output_ptr++) = argb_pi16;

			// Interleave the second ARGB quadruple
			argb_pi16 = _mm_unpackhi_pi16(out1_pi16, out2_pi16);

#if _SWAPBYTES
			if (swap_bytes_flag)
			{
				// Each color component is big endian on the Macintosh
				tmp1_pi16 = _mm_slli_pi16(argb_pi16, 8);
				tmp2_pi16 = _mm_srli_pi16(argb_pi16, 8);
				argb_pi16 = _mm_or_si64(tmp1_pi16, tmp2_pi16);
			}
#endif
			// Store the second ARGB quadruple
			*(argb_output_ptr++) = argb_pi16;

			// Interleave the ARGB values (second pair)
			out1_pi16 = _mm_unpackhi_pi16(alpha_pi16, g1_pi16);
			out2_pi16 = _mm_unpackhi_pi16(r1_pi16, b1_pi16);

			// Interleave the third ARGB quadruple
			argb_pi16 = _mm_unpacklo_pi16(out1_pi16, out2_pi16);

#if _SWAPBYTES
			if (swap_bytes_flag)
			{
				// Each color component is big endian on the Macintosh
				tmp1_pi16 = _mm_slli_pi16(argb_pi16, 8);
				tmp2_pi16 = _mm_srli_pi16(argb_pi16, 8);
				argb_pi16 = _mm_or_si64(tmp1_pi16, tmp2_pi16);
			}
#endif
			// Store the third ARGB quadruple
			*(argb_output_ptr++) = argb_pi16;

			// Interleave the fourth ARGB quadruple
			argb_pi16 = _mm_unpackhi_pi16(out1_pi16, out2_pi16);

#if _SWAPBYTES
			if (swap_bytes_flag)
			{
				// Each color component is big endian on the Macintosh
				tmp1_pi16 = _mm_slli_pi16(argb_pi16, 8);
				tmp2_pi16 = _mm_srli_pi16(argb_pi16, 8);
				argb_pi16 = _mm_or_si64(tmp1_pi16, tmp2_pi16);
			}
#endif
			// Store the fourth ARGB quadruple
			*(argb_output_ptr++) = argb_pi16;


#if 0
			// Interleave the ARGB values (third pair)
			out1_pi16 = _mm_unpacklo_pi16(alpha_pi16, g2_pi16);
			out2_pi16 = _mm_unpacklo_pi16(r2_pi16, b2_pi16);

			// Interleave and store the fifth ARGB quadruple
			argb_pi16 = _mm_unpacklo_pi16(out1_pi16, out2_pi16);
			*(argb_output_ptr++) = argb_pi16;

			// Interleave and store the sixth ARGB quadruple
			argb_pi16 = _mm_unpackhi_pi16(out1_pi16, out2_pi16);
			*(argb_output_ptr++) = argb_pi16;

			// Interleave the ARGB values (fourth pair)
			out1_pi16 = _mm_unpackhi_pi16(alpha_pi16, g2_pi16);
			out2_pi16 = _mm_unpackhi_pi16(r2_pi16, b2_pi16);

			// Interleave and store the seventh ARGB quadruple
			argb_pi16 = _mm_unpacklo_pi16(out1_pi16, out2_pi16);
			*(argb_output_ptr++) = argb_pi16;

			// Interleave and store the eighth ARGB quadruple
			argb_pi16 = _mm_unpackhi_pi16(out1_pi16, out2_pi16);
			*(argb_output_ptr++) = argb_pi16;
#endif
		}

		//y_src_ptr = (UINT16 *)y_input_ptr;
		//u_src_ptr = (UINT16 *)u_input_ptr;
		//v_src_ptr = (UINT16 *)v_input_ptr;

		yuvptr = (unsigned short *)yuv_input_ptr;
		outptr = (unsigned short *)argb_output_ptr;

		_mm_empty();

#endif //(1 && XMMOPT)

#if 0
		// Process the rest of the row
		for(; column < width; column += 2)
		{
			int y1, y2;
			int u1, u2;
			int v1, v2;
			int r1, r2;
			int g1, g2;
			int b1, b2;
			int t1, t2;

			// Get the next two pixels
			y1 = *(y_src_ptr++);
			u1 = *(u_src_ptr++);
			y2 = *(y_src_ptr++);
			v1 = *(v_src_ptr++);

			// Subtract the luma and chroma offsets
			y1 = y1 - mid_luma;
			y2 = y2 - mid_luma;
			u1 = u1 - mid_chroma;
			v1 = v1 - mid_chroma;

			// Convert YUV to RGB
			r1 = mulhu(cry, y1);
			t1 = mulhi(cru, u1);
			r1 = adds(r1, t1);

			r2 = mulhu(cry, y2);
			t2 = mulhi(cru, u1);
			r2 = adds(r2, t2);

			g1 = mulhu(cgy, y1);
			t1 = mulhi(cgu, u1);
			g1 = subs(g1, t1);
			t2 = mulhi(cgv, v1);
			g1 = subs(g1, t2);

			g2 = mulhu(cgy, y2);
			t1 = mulhi(cgu, u1);
			g2 = subs(g1, t1);
			t2 = mulhi(cgv, v1);
			g2 = subs(g2, t2);

			b1 = mulhu(cby, y1);
			t1 = mulhi(cbv, v1);
			b1 = adds(b1, t1);

			b2 = mulhu(cby, y2);
			t2 = mulhi(cbv, v1);
			b2 = adds(b2, t2);

			// Scale the RGB values into the output range from zero to (1 << 15)
			r1 <<= 2;
			g1 <<= 2;
			b1 <<= 2;

			r2 <<= 2;
			g2 <<= 2;
			b2 <<= 2;

			// Force the RGB values into valid range
			if (r1 < 0) r1 = 0;
			if (g1 < 0) g1 = 0;
			if (b1 < 0) b1 = 0;

			if (r1 > PF_MAX_CHAN16) r1 = PF_MAX_CHAN16;
			if (g1 > PF_MAX_CHAN16) g1 = PF_MAX_CHAN16;
			if (b1 > PF_MAX_CHAN16) b1 = PF_MAX_CHAN16;

			if (r2 < 0) r2 = 0;
			if (g2 < 0) g2 = 0;
			if (b2 < 0) b2 = 0;

			if (r2 > PF_MAX_CHAN16) r2 = PF_MAX_CHAN16;
			if (g2 > PF_MAX_CHAN16) g2 = PF_MAX_CHAN16;
			if (b2 > PF_MAX_CHAN16) b2 = PF_MAX_CHAN16;

			// Store the RGB values in the output buffer
			*(outptr++) = alpha;
			*(outptr++) = r1;
			*(outptr++) = g1;
			*(outptr++) = b1;

			*(outptr++) = alpha;
			*(outptr++) = r2;
			*(outptr++) = g2;
			*(outptr++) = b2;
		}
#endif //endrow
#endif
#if 0
		// Swap bytes for debugging
		unsigned short *p = (unsigned short *)output_row_ptr;
		for (int i = 0; i < width; i++)
		{
			*(p++) = SwapInt16(*p);
			*(p++) = SwapInt16(*p);
			*(p++) = SwapInt16(*p);
			*(p++) = SwapInt16(*p);
		}
#endif
		// Advance to the start of the next row
		output_row_ptr += output_pitch;
	}
}

#endif

#if 0
// Convert an image of YU64 pixels to BGRA with 15 bits per channel (for After Effects)
void CImageConverterYU64ToRGB::ConvertToAfterEffectsBGRA64(unsigned char *input, long input_pitch,
														   unsigned char *output, long output_pitch,
														   int width, int height)
{
	unsigned char *output_row_ptr = output;

	// Compute the offset to the start of the next row
	//const int bytes_per_pixel = 8;
	//const int output_row_gap = output_pitch - (bytes_per_pixel * width);

	const int chroma_offset = 128;

	// The RGB limit must correspond to the coefficient for RGB = 1.0
	//const int min_rgb = 0;
	const int max_rgb = PF_MAX_CHAN16;

	// Compute the maximum value to apply before shifting up to full scale
	const int post_shift = 2;
	const int pre_clamp = (0x7FFF - (max_rgb >> post_shift));

	const int alpha = 32768;

	// Scaling for the color conversion constants
	//const int scale = 0;

	// Color conversion constants
	int cry;
	int cru;
	int cgy;
	int cgu;
	int cgv;
	int cby;
	int cbv;

	int mid_luma;
	int mid_chroma;

	// The luma and chroma are shifted right by one bit before subtracting the offset
	// so the offsets are shifted by 7 bits instead of 8 bits as would be expected
	const int offset_shift = 7;

	// Shift the result to match the requested component size
	//int component_shift = (16 - component_size);

	int row;

	// The pixel size must be 15-bits (right justified in the 16-bit word) or 16-bits
	//assert(15 <= pixel_size && pixel_size <= 16);

	// The row length must be an even number
	assert((width % 2) == 0);

	// Scale the conversion constants
	cry = (m_ccy << 1);
	cru = (m_crv << 1);

	cgy = (m_ccy << 1);
	cgu = (m_cgv << 1);
	cgv = (m_cgu << 1);

	cby = (m_ccy << 1);
	cbv = (m_cbu << 0);

	mid_luma = (luma_offset << offset_shift);
	mid_chroma = (chroma_offset << offset_shift);

	for (row = 0; row < height; row++)
	{
		unsigned short *yuvptr = (unsigned short *)(input + row * input_pitch);
		unsigned short *outptr = (unsigned short *)output_row_ptr;

		// Start processing at the beginning of the row
		int column = 0;

#if (1 && XMMOPT)

		// Definitions used in the fast loop
		const int column_step = 4;
		const int post_column = width - (width % column_step);

		//__m64 *y_input_ptr = (__m64 *)y_src_ptr;
		//__m64 *u_input_ptr = (__m64 *)u_src_ptr;
		//__m64 *v_input_ptr = (__m64 *)v_src_ptr;

		__m64 *yuv_input_ptr = (__m64 *)yuvptr;
		__m64 *argb_output_ptr = (__m64 *)outptr;

		__m64 luma_offset_pi16 = _mm_set1_pi16(mid_luma);
		__m64 chroma_offset_pi16 = _mm_set1_pi16(mid_chroma);

		__m64 alpha_pi16 = _mm_set1_pi16(alpha);


		// YUV to RGB conversion constants
		__m64 cry_pi16 = _mm_set1_pi16(cry);
		__m64 cru_pi16 = _mm_set1_pi16(cru);

		__m64 cgy_pi16 = _mm_set1_pi16(cgy);
		__m64 cgu_pi16 = _mm_set1_pi16(cgu);
		__m64 cgv_pi16 = _mm_set1_pi16(cgv);

		__m64 cby_pi16 = _mm_set1_pi16(cby);
		__m64 cbv_pi16 = _mm_set1_pi16(cbv);


		//TODO: The post processing code has not been debugged
		#pragma warning(push)
		#pragma warning(disable: 964)
		assert(post_column == width);
		#pragma warning(pop)

		for (; column < post_column; column += column_step)
		{
			__m64 yuv1_pi16;
			__m64 yuv2_pi16;

			__m64 uv1_pi16;
			__m64 uv2_pi16;

			__m64 y1_pi16;
			//__m64 y2_pi16;

			__m64 u1_pi16;
			//__m64 u2_pi16;

			//__m64 u1a_pi16;
			//__m64 v1a_pi16;

			__m64 v1_pi16;
			//__m64 v2_pi16;

			__m64 r1_pi16;
			__m64 g1_pi16;
			__m64 b1_pi16;

			//__m64 r2_pi16;
			//__m64 g2_pi16;
			//__m64 b2_pi16;

			__m64 t1_pi16;
			//__m64 t2_pi16;

			__m64 out1_pi16;
			__m64 out2_pi16;
			__m64 argb_pi16;

			//__m64 clamp_pi16 = _mm_set1_pi16(0x7FFF - max_rgb);
			//__m64 clamp_pi16 = _mm_set1_pi16(0x7FFF - ymult);
			__m64 clamp_pi16 = _mm_set1_pi16(pre_clamp);

#if INTERPOLATE_CHROMA
			int extracted_u;
			int extracted_v;
#endif
#if (1 && DEBUG)
			//int y1, y2;
			//int u1, u2;
			//int v1, v2;
			int r1;
			//int r2;
			int g1;
			//int g2;
			int b1;
			//int b2;

			//int debug_luma = (1 << 16) - 1;
			//int debug_chroma = (chroma_offset << 8);
#endif
#if 0
			// Load eight luma values and four of each chroma
			y1_pi16 = *(y_input_ptr++);
			y2_pi16 = *(y_input_ptr++);
			u1_pi16 = *(u_input_ptr++);
			v1_pi16 = *(v_input_ptr++);
#else
			// Load two quads of packed luma and chroma
			yuv1_pi16 = *(yuv_input_ptr++);
			yuv2_pi16 = *(yuv_input_ptr++);

#if INTERPOLATE_CHROMA
			if(column < post_column - column_step)
			{
				extracted_u = (_mm_extract_pi16(*yuv_input_ptr, 1))>>1;
				extracted_v = (_mm_extract_pi16(*yuv_input_ptr, 3))>>1;
			}
			else
			{
				extracted_u = (_mm_extract_pi16(yuv2_pi16, 1))>>1;
				extracted_v = (_mm_extract_pi16(yuv2_pi16, 3))>>1;
			}
#endif
			// Unpack and duplicate the chroma
			uv1_pi16 = _mm_shuffle_pi16(yuv1_pi16, _MM_SHUFFLE(3, 3, 1, 1));
			uv2_pi16 = _mm_shuffle_pi16(yuv2_pi16, _MM_SHUFFLE(3, 3, 1, 1));

			u1_pi16 = _mm_unpacklo_pi32(uv1_pi16, uv2_pi16);
			v1_pi16 = _mm_unpackhi_pi32(uv1_pi16, uv2_pi16);

#if INTERPOLATE_CHROMA
			// Blur chroma over RGB pairs, caused shear in multiple generations
			u1_pi16 = _mm_srli_pi16(u1_pi16, 1);
			v1_pi16 = _mm_srli_pi16(v1_pi16, 1);

			u1a_pi16 = _mm_shuffle_pi16(u1_pi16, _MM_SHUFFLE(3, 3, 2, 1));
			v1a_pi16 = _mm_shuffle_pi16(v1_pi16, _MM_SHUFFLE(3, 3, 2, 1));

			u1a_pi16 = _mm_insert_pi16(u1a_pi16, extracted_u, 3);
			v1a_pi16 = _mm_insert_pi16(v1a_pi16, extracted_v, 3);

			u1_pi16 = _mm_adds_pu16(u1_pi16, u1a_pi16);
			v1_pi16 = _mm_adds_pu16(v1_pi16, v1a_pi16);
#endif
			// Unpack the luma
			yuv1_pi16 = _mm_shuffle_pi16(yuv1_pi16, _MM_SHUFFLE(2, 0, 2, 0));
			yuv2_pi16 = _mm_shuffle_pi16(yuv2_pi16, _MM_SHUFFLE(2, 0, 2, 0));

			y1_pi16 = _mm_unpacklo_pi32(yuv1_pi16, yuv2_pi16);
#endif

#if (0 && DEBUG)
			y1_pi16 = _mm_set1_pi16(debug_luma);
			//y2_pi16 = _mm_set1_pi16(debug_luma);
			u1_pi16 = _mm_set1_pi16(debug_chroma);
			v1_pi16 = _mm_set1_pi16(debug_chroma);
#endif
			// Subtract the luma and chroma offsets
			y1_pi16 = _mm_srli_pi16(y1_pi16, 1);
			y1_pi16 = _mm_sub_pi16(y1_pi16, luma_offset_pi16);
			//y2_pi16 = _mm_sub_pi16(y2_pi16, luma_offset_pi16);
			u1_pi16 = _mm_srli_pi16(u1_pi16, 1);
			u1_pi16 = _mm_sub_pi16(u1_pi16, chroma_offset_pi16);
			v1_pi16 = _mm_srli_pi16(v1_pi16, 1);
			v1_pi16 = _mm_sub_pi16(v1_pi16, chroma_offset_pi16);

			// Duplicate the u and v chroma
			//u2_pi16 = _mm_unpackhi_pi16(u1_pi16, u1_pi16);
			//u1_pi16 = _mm_unpacklo_pi16(u1_pi16, u1_pi16);

			//v2_pi16 = _mm_unpackhi_pi16(v1_pi16, v1_pi16);
			//v1_pi16 = _mm_unpacklo_pi16(v1_pi16, v1_pi16);

			// Compute the red channel
			r1_pi16 = _mm_mulhi_pi16(cry_pi16, y1_pi16);
			t1_pi16 = _mm_mulhi_pi16(cru_pi16, u1_pi16);
			r1_pi16 = _mm_adds_pi16(r1_pi16, clamp_pi16);
			r1_pi16 = _mm_adds_pi16(r1_pi16, t1_pi16);
			r1_pi16 = _mm_subs_pu16(r1_pi16, clamp_pi16);
#if 0
			r2_pi16 = _mm_mulhi_pi16(cry_pi16, y2_pi16);
			t2_pi16 = _mm_mulhi_pi16(cru_pi16, u2_pi16);
			r2_pi16 = _mm_adds_pi16(r2_pi16, clamp_pi16);
			r2_pi16 = _mm_adds_pi16(r2_pi16, t2_pi16);
			r2_pi16 = _mm_subs_pu16(r2_pi16, clamp_pi16);
#endif
			// Compute the green channel
			g1_pi16 = _mm_mulhi_pi16(cgy_pi16, y1_pi16);
			t1_pi16 = _mm_mulhi_pi16(cgu_pi16, u1_pi16);
			g1_pi16 = _mm_adds_pi16(g1_pi16, clamp_pi16);
			g1_pi16 = _mm_subs_pi16(g1_pi16, t1_pi16);
			t1_pi16 = _mm_mulhi_pi16(cgv_pi16, v1_pi16);
			g1_pi16 = _mm_subs_pi16(g1_pi16, t1_pi16);
			g1_pi16 = _mm_subs_pu16(g1_pi16, clamp_pi16);
#if 0
			g2_pi16 = _mm_mulhi_pi16(cgy_pi16, y2_pi16);
			t2_pi16 = _mm_mulhi_pi16(cgu_pi16, u2_pi16);
			g2_pi16 = _mm_adds_pi16(g2_pi16, clamp_pi16);
			g2_pi16 = _mm_subs_pi16(g2_pi16, t2_pi16);
			t2_pi16 = _mm_mulhi_pi16(cgv_pi16, v2_pi16);
			g2_pi16 = _mm_subs_pi16(g2_pi16, t2_pi16);
			g2_pi16 = _mm_subs_pu16(g2_pi16, clamp_pi16);
#endif
			// Compute the blue channel
			b1_pi16 = _mm_mulhi_pi16(cby_pi16, y1_pi16);
			t1_pi16 = _mm_mulhi_pi16(cbv_pi16, v1_pi16);
			b1_pi16 = _mm_adds_pi16(b1_pi16, clamp_pi16);
			b1_pi16 = _mm_adds_pi16(b1_pi16, t1_pi16);
			b1_pi16 = _mm_adds_pi16(b1_pi16, t1_pi16); // cbv was at half scale
			b1_pi16 = _mm_subs_pu16(b1_pi16, clamp_pi16);
#if 0
			b2_pi16 = _mm_mulhi_pi16(cby_pi16, y2_pi16);
			t2_pi16 = _mm_mulhi_pi16(cbv_pi16, v2_pi16);
			b2_pi16 = _mm_adds_pi16(b2_pi16, clamp_pi16);
			b2_pi16 = _mm_adds_pi16(b2_pi16, t2_pi16);
			b2_pi16 = _mm_adds_pi16(b2_pi16, t2_pi16); // cbv was at half scale
			b2_pi16 = _mm_subs_pu16(b2_pi16, clamp_pi16);
#endif
#if 0
			// Saturate the values to the range from 0 to max RGB
			t1_pi16 = _mm_setzero_si64();
			t1_pi16 = _mm_cmpgt_pi16(r1_pi16, t1_pi16);
			r1_pi16 = _mm_and_si64(r1_pi16, t1_pi16);

			r1_pi16 = _mm_adds_pi16(r1_pi16, clamp_pi16);
			r1_pi16 = _mm_subs_pi16(r1_pi16, clamp_pi16);

			t2_pi16 = _mm_setzero_si64();
			t2_pi16 = _mm_cmpgt_pi16(r2_pi16, t2_pi16);
			r2_pi16 = _mm_and_si64(r2_pi16, t2_pi16);

			r2_pi16 = _mm_adds_pi16(r2_pi16, clamp_pi16);
			r2_pi16 = _mm_subs_pi16(r2_pi16, clamp_pi16);

			t1_pi16 = _mm_setzero_si64();
			t1_pi16 = _mm_cmpgt_pi16(g1_pi16, t1_pi16);
			g1_pi16 = _mm_and_si64(g1_pi16, t1_pi16);

			g1_pi16 = _mm_adds_pi16(g1_pi16, clamp_pi16);
			g1_pi16 = _mm_subs_pi16(g1_pi16, clamp_pi16);

			t2_pi16 = _mm_setzero_si64();
			t2_pi16 = _mm_cmpgt_pi16(g2_pi16, t2_pi16);
			g2_pi16 = _mm_and_si64(g2_pi16, t2_pi16);

			g2_pi16 = _mm_adds_pi16(g2_pi16, clamp_pi16);
			g2_pi16 = _mm_subs_pi16(g2_pi16, clamp_pi16);

			t1_pi16 = _mm_setzero_si64();
			t1_pi16 = _mm_cmpgt_pi16(b1_pi16, t1_pi16);
			b1_pi16 = _mm_and_si64(b1_pi16, t1_pi16);

			b1_pi16 = _mm_adds_pi16(b1_pi16, clamp_pi16);
			b1_pi16 = _mm_subs_pi16(b1_pi16, clamp_pi16);

			t2_pi16 = _mm_setzero_si64();
			t2_pi16 = _mm_cmpgt_pi16(b2_pi16, t2_pi16);
			b2_pi16 = _mm_and_si64(b2_pi16, t2_pi16);

			b2_pi16 = _mm_adds_pi16(b2_pi16, clamp_pi16);
			b2_pi16 = _mm_subs_pi16(b2_pi16, clamp_pi16);
#endif
#if (1 && DEBUG)
			r1 = _mm_extract_pi16(r1_pi16, 0);
			g1 = _mm_extract_pi16(g1_pi16, 0);
			b1 = _mm_extract_pi16(b1_pi16, 0);

			//r2 = _mm_extract_pi16(r2_pi16, 0);
			//g2 = _mm_extract_pi16(g2_pi16, 0);
			//b2 = _mm_extract_pi16(b2_pi16, 0);
#endif
			r1_pi16 = _mm_slli_pi16(r1_pi16, post_shift);
			g1_pi16 = _mm_slli_pi16(g1_pi16, post_shift);
			b1_pi16 = _mm_slli_pi16(b1_pi16, post_shift);
#if 0
			r2_pi16 = _mm_slli_pi16(r2_pi16, post_shift);
			g2_pi16 = _mm_slli_pi16(g2_pi16, post_shift);
			b2_pi16 = _mm_slli_pi16(b2_pi16, post_shift);
#endif
#if (1 && DEBUG)
			r1 = _mm_extract_pi16(r1_pi16, 0);
			g1 = _mm_extract_pi16(g1_pi16, 0);
			b1 = _mm_extract_pi16(b1_pi16, 0);

			//r2 = _mm_extract_pi16(r2_pi16, 0);
			//g2 = _mm_extract_pi16(g2_pi16, 0);
			//b2 = _mm_extract_pi16(b2_pi16, 0);
#endif
#if 0
			// Adjust the components to the requested size
			r1_pi16 = _mm_srli_pi16(r1_pi16, component_shift);
			g1_pi16 = _mm_srli_pi16(g1_pi16, component_shift);
			b1_pi16 = _mm_srli_pi16(b1_pi16, component_shift);
#endif
			// Interleave the ARGB values (first pair)
			out1_pi16 = _mm_unpacklo_pi16(alpha_pi16, g1_pi16);
			out2_pi16 = _mm_unpacklo_pi16(r1_pi16, b1_pi16);

			// Interleave and store the first ARGB quadruple
			argb_pi16 = _mm_unpacklo_pi16(out1_pi16, out2_pi16);
			*(argb_output_ptr++) = argb_pi16;

			// Interleave and store the second ARGB quadruple
			argb_pi16 = _mm_unpackhi_pi16(out1_pi16, out2_pi16);
			*(argb_output_ptr++) = argb_pi16;

			// Interleave the ARGB values (second pair)
			out1_pi16 = _mm_unpackhi_pi16(alpha_pi16, g1_pi16);
			out2_pi16 = _mm_unpackhi_pi16(r1_pi16, b1_pi16);

			// Interleave and store the third ARGB quadruple
			argb_pi16 = _mm_unpacklo_pi16(out1_pi16, out2_pi16);
			*(argb_output_ptr++) = argb_pi16;

			// Interleave and store the fourth ARGB quadruple
			argb_pi16 = _mm_unpackhi_pi16(out1_pi16, out2_pi16);
			*(argb_output_ptr++) = argb_pi16;
#if 0
			// Interleave the ARGB values (third pair)
			out1_pi16 = _mm_unpacklo_pi16(alpha_pi16, g2_pi16);
			out2_pi16 = _mm_unpacklo_pi16(r2_pi16, b2_pi16);

			// Interleave and store the fifth ARGB quadruple
			argb_pi16 = _mm_unpacklo_pi16(out1_pi16, out2_pi16);
			*(argb_output_ptr++) = argb_pi16;

			// Interleave and store the sixth ARGB quadruple
			argb_pi16 = _mm_unpackhi_pi16(out1_pi16, out2_pi16);
			*(argb_output_ptr++) = argb_pi16;

			// Interleave the ARGB values (fourth pair)
			out1_pi16 = _mm_unpackhi_pi16(alpha_pi16, g2_pi16);
			out2_pi16 = _mm_unpackhi_pi16(r2_pi16, b2_pi16);

			// Interleave and store the seventh ARGB quadruple
			argb_pi16 = _mm_unpacklo_pi16(out1_pi16, out2_pi16);
			*(argb_output_ptr++) = argb_pi16;

			// Interleave and store the eighth ARGB quadruple
			argb_pi16 = _mm_unpackhi_pi16(out1_pi16, out2_pi16);
			*(argb_output_ptr++) = argb_pi16;
#endif
		}

		//y_src_ptr = (UINT16 *)y_input_ptr;
		//u_src_ptr = (UINT16 *)u_input_ptr;
		//v_src_ptr = (UINT16 *)v_input_ptr;

		yuvptr = (unsigned short *)yuv_input_ptr;
		outptr = (unsigned short *)argb_output_ptr;

		_mm_empty();

#endif //(1 && XMMOPT)

#if 0
		// Process the rest of the row
		for(; column < width; column += 2)
		{
			int y1, y2;
			int u1, u2;
			int v1, v2;
			int r1, r2;
			int g1, g2;
			int b1, b2;
			int t1, t2;

			// Get the next two pixels
			y1 = *(y_src_ptr++);
			u1 = *(u_src_ptr++);
			y2 = *(y_src_ptr++);
			v1 = *(v_src_ptr++);

			// Subtract the luma and chroma offsets
			y1 = y1 - mid_luma;
			y2 = y2 - mid_luma;
			u1 = u1 - mid_chroma;
			v1 = v1 - mid_chroma;

			// Convert YUV to RGB
			r1 = mulhu(cry, y1);
			t1 = mulhi(cru, u1);
			r1 = adds(r1, t1);

			r2 = mulhu(cry, y2);
			t2 = mulhi(cru, u1);
			r2 = adds(r2, t2);

			g1 = mulhu(cgy, y1);
			t1 = mulhi(cgu, u1);
			g1 = subs(g1, t1);
			t2 = mulhi(cgv, v1);
			g1 = subs(g1, t2);

			g2 = mulhu(cgy, y2);
			t1 = mulhi(cgu, u1);
			g2 = subs(g1, t1);
			t2 = mulhi(cgv, v1);
			g2 = subs(g2, t2);

			b1 = mulhu(cby, y1);
			t1 = mulhi(cbv, v1);
			b1 = adds(b1, t1);

			b2 = mulhu(cby, y2);
			t2 = mulhi(cbv, v1);
			b2 = adds(b2, t2);

			// Scale the RGB values into the output range from zero to (1 << 15)
			r1 <<= 2;
			g1 <<= 2;
			b1 <<= 2;

			r2 <<= 2;
			g2 <<= 2;
			b2 <<= 2;

			// Force the RGB values into valid range
			if (r1 < 0) r1 = 0;
			if (g1 < 0) g1 = 0;
			if (b1 < 0) b1 = 0;

			if (r1 > PF_MAX_CHAN16) r1 = PF_MAX_CHAN16;
			if (g1 > PF_MAX_CHAN16) g1 = PF_MAX_CHAN16;
			if (b1 > PF_MAX_CHAN16) b1 = PF_MAX_CHAN16;

			if (r2 < 0) r2 = 0;
			if (g2 < 0) g2 = 0;
			if (b2 < 0) b2 = 0;

			if (r2 > PF_MAX_CHAN16) r2 = PF_MAX_CHAN16;
			if (g2 > PF_MAX_CHAN16) g2 = PF_MAX_CHAN16;
			if (b2 > PF_MAX_CHAN16) b2 = PF_MAX_CHAN16;

			// Store the RGB values in the output buffer
			*(outptr++) = alpha;
			*(outptr++) = r1;
			*(outptr++) = g1;
			*(outptr++) = b1;

			*(outptr++) = alpha;
			*(outptr++) = r2;
			*(outptr++) = g2;
			*(outptr++) = b2;
		}
#endif //endrow

		// Advance to the start of the next row
		output_row_ptr += output_pitch;
	}
}
#endif

// Convert a row of YU64 pixels to the Final Cut Pro floating-point YUVA format
void CImageConverterYU64ToYUV::ConvertToFloatYUVA(unsigned char *input, unsigned char *output, int length)
{
	unsigned short *yuvptr = (unsigned short *)input;
	float *outptr = (float *)output;

	// Start processing at the beginning of the row
	int column = 0;

	// Process the rest of the row
	// Should be the same as the code for the full image conversion
	for(; column < length; column += 2)
	{
		int y1;
		int y2;
		int u1;
		int v1;

		float y;
		float u;
		float v;

		const float a = 1.0;
		int black = (16 << 8);
		float luma_divisor = ((float)(219 << 8)) / 0.859f;
		float chroma_divisor = ((float)(128 << 8)) / 0.502f;

		// Get the next two pixels
		y1 = *(yuvptr++);
		u1 = *(yuvptr++);
		y2 = *(yuvptr++);
		v1 = *(yuvptr++);

		// Adjust the luma so that CCIR black is zero
		y1 -= black;
		//if (y1 < 0) y1 = 0;

		y2 -= black;
		//if (y2 < 0) y2 = 0;

		// Convert the first pixel to floating-point
		y = ((float)y1) / luma_divisor;
		u = ((float)u1) / chroma_divisor;
		v = ((float)v1) / chroma_divisor;

		// Clamp the luma to the maximum output value
		if (y > 1.0) y = 1.0;
		// Output the first pixel
		*(outptr++) = a;
		*(outptr++) = y;
		*(outptr++) = v;	// Cb
		*(outptr++) = u;	// Cr
		// Convert the second pixel to floating-point
		y = ((float)y2) / luma_divisor;

		// Clamp the luma to the maximum output value
		if (y > 1.0) y = 1.0;
		// Output the second pixel
		*(outptr++) = a;
		*(outptr++) = y;
		*(outptr++) = v;	// Cb
		*(outptr++) = u;	// Cr
	}
}

// Convert an image of YU64 pixels to the Final Cut Pro floating-point YUVA format
void CImageConverterYU64ToYUV::ConvertToFloatYUVA(unsigned char *input, long input_pitch,
												  unsigned char *output, long output_pitch,
												  int width, int height)
{
	unsigned char *yu64_row_ptr = input;
	unsigned char *yuva_row_ptr = output;
	int row;

	for (row = 0; row < height; row++)
	{
		unsigned short *yuvptr = (unsigned short *)yu64_row_ptr;
		float *outptr = (float *)yuva_row_ptr;

		// Start processing at the beginning of the row
		int column = 0;

		// Process the rest of the row
		for(; column < width; column += 2)
		{
			int y1;
			int y2;
			int u1;
			int v1;

			float y;
			float u;
			float v;

			const float a = 1.0;

			int black = (16 << 8);
			float luma_divisor = ((float)(219 << 8)) / 0.859f;
			float chroma_divisor = ((float)(128 << 8)) / 0.502f;

			// Get the next two pixels
			y1 = *(yuvptr++);
			u1 = *(yuvptr++);
			y2 = *(yuvptr++);
			v1 = *(yuvptr++);

			// Adjust the luma so that CCIR black is zero
			y1 -= black;
			//if (y1 < 0) y1 = 0;

			y2 -= black;
			//if (y2 < 0) y2 = 0;

			// Convert the first pixel to floating-point
			y = ((float)y1) / luma_divisor;
			u = ((float)u1) / chroma_divisor;
			v = ((float)v1) / chroma_divisor;

			// Clamp the luma to the maximum output value
			if (y > 1.0) y = 1.0;
#if 1
			// Output the first pixel
			*(outptr++) = a;
			*(outptr++) = y;
			*(outptr++) = v;	// Cb
			*(outptr++) = u;	// Cr
#else
			// Output debug values
			*(outptr++) = 1.000;
			//*(outptr++) = ((float)(column % 220) / 219) * 0.859;
			*(outptr++) = ((float)column / (float)width) * 0.859;
			*(outptr++) = 0.502;
			*(outptr++) = 0.502;
#endif
			// Convert the second pixel to floating-point
			y = ((float)y2) / luma_divisor;
			//u = ((float)u1) / divisor;
			//v = ((float)v1) / divisor;

			// Clamp the luma to the maximum output value
			if (y > 1.0) y = 1.0;
#if 1
			// Output the second pixel
			*(outptr++) = a;
			*(outptr++) = y;
			*(outptr++) = v;	// Cb
			*(outptr++) = u;	// Cr
#else
			// Output debug values
			*(outptr++) = 1.000;
			//*(outptr++) = ((float)((column + 1) % 220) / 219) * 0.859;
			*(outptr++) = ((float)(column + 1) / (float)width) * 0.859;
			*(outptr++) = 0.502;
			*(outptr++) = 0.502;
#endif
		}

		// Advance to the start of the next row in the input and output buffers
		yu64_row_ptr += input_pitch;
		yuva_row_ptr += output_pitch;
	}
}

												  
/*!
	@brief Convert from CineForm YUV to Adobe Premiere VUYA

	Perform color conversion from CineForm YUV in the 709 color space
	to Adobe Premiere VUYA in the 601 color space.

	The first set of equations are from code implemented by David Newman.
	The second set of equations were derived from the equations published
	by Kieth Jack, Video Demystified (Third Edition).  The matrix for
	709 to RGB color conversion was multipled by the matrix for RGB to 601
	color conversion to get the overall matrix for 709 to 601 conversion.
	The floating-point coefficients were scaled to integers.

	This routine was used in the Adobe Premier importer and was copied
	from the RTConvert library.
*/
void CImageConverterYU64ToYUV::ConvertToVUYA_4444_8u(uint8_t *input_buffer, int input_pitch,
													 uint8_t *output_buffer, int output_pitch,
													 int width, int height)
{
	const int alpha = 255;
	int row = 0;

	const int luma_offset = 16;
	const int chroma_offset = 128;

	//#pragma omp parallel for 
	for (row = 0; row < height; row++)
	{
		unsigned char *rowptr = input_buffer + (height - row - 1) * input_pitch;
		unsigned char *output_row_ptr = output_buffer;
		output_row_ptr += output_pitch*row;
		unsigned char *outptr = output_row_ptr;
		

		int column = 0;

#if (0 && XMMOPT)

		// Process eight pixels per loop iteration
		int column_step = 8;
		int post_column = width - (width % column_step);

		__m128i offset_epi16 = _mm_unpacklo_epi16(_mm_set1_epi16(chroma_offset), _mm_set1_epi16(luma_offset));

		__m128i *yuyv_ptr = (__m128i *)rowptr;
		__m128i *vuya_ptr = (__m128i *)outptr;

		__m128i yuyv_epi8;
		__m128i yuyv_epi16;

		__m128i y_epi8;
		__m128i u_epi8;
		__m128i v_epi8;

		for (; column < post_column; column += column_step)
		{
			// Load the next eight pixels (16 bytes)
			yuyv_epi8 = _mm_loadu_si128(yuyv_ptr++);

			// Extract eight luma values
			y_epi16 = _mm_slli_epi16(yuyv_epi8, 8);
			y_epi16 = _mm_srli_epi16(y_epi16, 8);

			// Subtract the luma offset
			y_epi16 = _mm_sub_spi16(y_epi16, luma_offset_epi16);

			// Extract four u chroma values
			u_epi32 = _mm_slli_epi32(yuyv_epi8, 16);
			u_epi32 = _mm_srli_epi32(u_epi32, 24);
			u_epi16 = _mm_pack_epi32(u_epi32, u_epi32);

			// Duplicate the u chroma values
			u_epi16 = _mm_unpacklo_epi16(u_epi16, u_epi16);

			// Subtract the chroma offset
			u_epi16 = _mm_sub_epi16(u_epi16, chroma_offset_epi16);

			// Extract four v chroma values
			v_epi32 = _mm_srli_epi32(v_epi32, 24);
			v_epi16 = _mm_pack_epi32(v_epi32, v_epi32);

			// Duplicate the v chroma values
			v_epi16 = _mm_unpacklo_epi16(v_epi16, v_epi16);

			// Subtract the chroma offset
			v_epi16 = _mm_sub_epi16(v_epi16, chroma_offset_epi16);

			// Convert the luma from 709 to 601 color space
			yc_epi16 = _mm_mul_epi16(y_epi16, yy_epi16);
			t1_epi16 = _mm_mul_epi16(u_epi16, yu_epi16);
			yc_epi16 = _mm_add_epi16(yc_epi16, t1_epi16);
			t2_epi16 = _mm_mul_spi16(v_epi16, yv_epi16);
			yc_epi16 = _mm_add_epi16(yc_epi16, t2_epi16);

			// Convert the u chroma from 709 to 601 color space
			uc_epi16 = _mm_mul_epi16(y_epi16, uy_epi16);
			t1_epi16 = _mm_mul_epi16(u_epi16, uu_epi16);
			uc_epi16 = _mm_add_epi16(uc_epi16, t1_epi16);
			t2_epi16 = _mm_mul_spi16(v_epi16, uv_epi16);
			uc_epi16 = _mm_add_epi16(uc_epi16, t2_epi16);

			// Convert the v chroma from 709 to 601 color space
			vc_epi16 = _mm_mul_epi16(y_epi16, vy_epi16);
			t1_epi16 = _mm_mul_epi16(u_epi16, vu_epi16);
			vc_epi16 = _mm_add_epi16(vc_epi16, t1_epi16);
			t2_epi16 = _mm_mul_spi16(v_epi16, vv_epi16);
			vc_epi16 = _mm_add_epi16(vc_epi16, t2_epi16);

			// Pack the luma and chroma components
			y_epi8 = _mm_pack_epi16(yc_epi16, _mm_setzero_si128());
			u_epi8 = _mm_pack_epi16(uc_epi16, _mm_setzero_si128());
			v_epi8 = _mm_pack_epi16(vc_epi16, _mm_setzero_si128());

			// Interleave the first set of luma and chroma components
			vu_epi8 = _mm_unpacklo_epi8(v_epi8, v_epi8);
			ya_epi8 = _mm_unpacklo_epi8(y_epi8, a_epi8);
			vuya_epi8 = _mm_unpacklo_epi16(vu_epi8, ya_epi8);

			// Output the first set of luma and chroma components
			_mm_store_si128(vuya_ptr++, vuya_epi8);

			// Interleave the second set of luma and chroma components
			vuya_epi8 = _mm_unpacklo_epi16(vu_epi8, ya_epi8);

			// Output the second set of luma and chroma components
			_mm_store_si128(vuya_ptr++, vuya_epi8);
		}

		// Set the pointer to the remainder of the row
		rowptr = (unsigned char *)yuyv_ptr;
		outptr = (unsigned char *)vuya_ptr;

#endif

		for (; column < width; column += 2)
		{
#if 0
			int y1, y2;
			int u1, v1;

			int y1_out, y2_out;
			int u1_out, v1_out;

			// Get the input pixel
			y1 = *(rowptr++);
			u1 = *(rowptr++);
			y2 = *(rowptr++);
			v1 = *(rowptr++);

			// Convert from the 709 to 601 color space
			int luma_offset = ((49 * v1 + 25 * u1) >> 8) - 37;
			y1_out = y1 + luma_offset;
			y2_out = y2 + luma_offset;

			u1_out = u1 - ((28 * v1) >> 8) + 14;
			v1_out = v1 - ((20 * u1) >> 8) + 10;

			// Clamp the results to the 8-bit range
			if (y1_out < 0) y1_out = 0;
			else if (y1_out > UINT8_MAX) y1_out = UINT8_MAX;

			if (y2_out < 0) y2_out = 0;
			else if (y2_out > UINT8_MAX) y2_out = UINT8_MAX;

			if (u1_out < 0) u1_out = 0;
			else if (u1_out > UINT8_MAX) u1_out = UINT8_MAX;

			if (v1_out < 0) v1_out = 0;
			else if (v1_out > UINT8_MAX) v1_out = UINT8_MAX;

			// Output the first pixel
			*(outptr++) = v1_out;
			*(outptr++) = u1_out;
			*(outptr++) = y1_out;
			*(outptr++) = alpha;

			// Output the second pixel
			*(outptr++) = v1_out;
			*(outptr++) = u1_out;
			*(outptr++) = y2_out;
			*(outptr++) = alpha;
#else
			int Y1, Y2;
			int Cr, Cb;

			int Y1_out, Y2_out;
			int Cr_out, Cb_out;

			// Get the input pixel
			Y1 = *(rowptr++);
			Cb = *(rowptr++);
			Y2 = *(rowptr++);
			Cr = *(rowptr++);

			// Subtract the luma and chroma offsets
			Y1 -= luma_offset;
			Y2 -= luma_offset;
			Cb -= chroma_offset;
			Cr -= chroma_offset;

			// Convert from the 709 to 601 color space (coefficients scaled by 8192)
			Y1_out = (Y1 << 13) + 815 * Cb + 1568 * Cr;
			Y2_out = (Y2 << 13) + 815 * Cb + 1568 * Cr;
			Cb_out = 8110 * Cb - 895 * Cr;
			Cr_out = 8056 * Cr - 590 * Cb;

			// Remove the scale factor in the coefficients
			Y1_out >>= 13;
			Y2_out >>= 13;
			Cb_out >>= 13;
			Cr_out >>= 13;

			// Add the luma and chroma offsets
			Y1_out += luma_offset;
			Y2_out += luma_offset;
			Cb_out += chroma_offset;
			Cr_out += chroma_offset;

			// Clamp the results to the 8-bit range
			if (Y1_out < 0) Y1_out = 0;
			else if (Y1_out > UINT8_MAX) Y1_out = UINT8_MAX;

			if (Y2_out < 0) Y2_out = 0;
			else if (Y2_out > UINT8_MAX) Y2_out = UINT8_MAX;

			if (Cb_out < 0) Cb_out = 0;
			else if (Cb_out > UINT8_MAX) Cb_out = UINT8_MAX;

			if (Cr_out < 0) Cr_out = 0;
			else if (Cr_out > UINT8_MAX) Cr_out = UINT8_MAX;

			// Output the first pixel
			*(outptr++) = Cr_out;
			*(outptr++) = Cb_out;
			*(outptr++) = Y1_out;
			*(outptr++) = alpha;

			// Output the second pixel
			*(outptr++) = Cr_out;
			*(outptr++) = Cb_out;
			*(outptr++) = Y2_out;
			*(outptr++) = alpha;
#endif
		}
	}
}

/*!
	@brief Convert an image of YU64 pixels to the Avid 10-bit 2.8 format
*/
void CImageConverterYU64ToYUV::ConvertToAvid_CbYCrY_10bit_2_8(uint8_t *input_buffer, int input_pitch,
															  uint8_t *output_buffer, int output_pitch,
															  int width, int height)
{
	uint16_t *input_row_ptr = (uint16_t *)input_buffer;
	size_t input_row_pitch = width * 2;		// Input row pitch in units of pixels

	int row, column;

	size_t upper_row_pitch = width / 2;
	size_t lower_row_pitch = width * 2;

	uint8_t *upper_plane = output_buffer;
	uint8_t *lower_plane = upper_plane + width * height / 2;

	uint8_t *upper_row_ptr = upper_plane;
	uint8_t *lower_row_ptr = lower_plane;

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
			Y1 = input_row_ptr[2 * column + 0];
			Y1_upper = (Y1 >> 6) & 0x03;		// Least significant 2 bits
			Y1_lower = (Y1 >> 8) & 0xFF;		// Most significant 8 bits

			// Process Cr
			Cr = input_row_ptr[2 * column + 1];
			Cr_upper = (Cr >> 6) & 0x03;		// Least significant 2 bits
			Cr_lower = (Cr >> 8) & 0xFF;		// Most significant 8 bits

			// Process Y2
			Y2 = input_row_ptr[2 * column + 2];
			Y2_upper = (Y2 >> 6) & 0x03;		// Least significant 2 bits
			Y2_lower = (Y2 >> 8) & 0xFF;		// Most significant 8 bits

			// Process Cb
			Cb = input_row_ptr[2 * column + 3];
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

void CImageConverterCbYCrY_10bit_2_8::ConvertToRGB48(uint8_t *input_buffer, int input_pitch,
													 uint8_t *output_buffer, int output_pitch,
													 int width, int height)
{
	//int planar = (flags & ACTIVEMETADATA_PLANAR);
	//uint8_t *input_row_ptr = input_buffer;
	//int input_row_pitch = width * 2;

	uint8_t *upper_plane = input_buffer;
	uint8_t *lower_plane = upper_plane + width * height / 2;

	uint8_t *upper_row_ptr = upper_plane;
	uint8_t *lower_row_ptr = lower_plane;

	int upper_row_pitch = width / 8;
	int lower_row_pitch = input_pitch;

	uint16_t *output_row_ptr = (uint16_t *)output_buffer;
	int output_row_pitch = output_pitch / sizeof(uint16_t);

	const int offset_shift = 8;
	int luma_offset_shifted = (luma_offset << offset_shift);
	int chroma_offset_shifted = (chroma_offset << offset_shift);

	int row, column;

	// This routine only handles the planar case
	//assert(planar);

	//if (planar)
	if (1)
	{
		uint16_t *plane_array[3];
		int plane_pitch[3];
		//const int input_pitch = width * 2 * 2;

		for (row = 0; row < height; row++)
		{
			plane_array[0] = (uint16_t *)&output_row_ptr[0];
			plane_array[1] = (uint16_t *)&output_row_ptr[width];
			plane_array[2] = (uint16_t *)&output_row_ptr[width*3/2];

			plane_pitch[0] = input_pitch;
			plane_pitch[1] = input_pitch;
			plane_pitch[2] = input_pitch;

			// Output width must be a multiple of two
			assert((width % 2) == 0);

			// Each byte in the upper plane corresponds to two columns of output
			for (column = 0; column < width; column += 2)
			{
				uint16_t Y1, Cr, Y2, Cb;
				uint8_t Y1_upper, Cr_upper, Y2_upper, Cb_upper;
				uint8_t Y1_lower, Cr_lower, Y2_lower, Cb_lower;
				//uint16_t upper;

				uint8_t upper_byte = upper_row_ptr[column / 2];

				uint16_t R1, G1, B1;
				uint16_t R2, G2, B2;

				float Y1_float, Y2_float, Cr_float, Cb_float;
				float R1_float, G1_float, B1_float;
				float R2_float, G2_float, B2_float;

				//const uint16_t RGB_max = max_rgb;

				// Reconstruct Cb
				Cb_upper = (upper_byte >> 6) & 0x03;		// Least significant 2 bits
				Cb_lower = lower_row_ptr[2 * column + 0];	// Most significant 8 bits
				Cb = (Cb_lower << 8) | (Cb_upper << 6);

				// Reconstruct Y1
				Y1_upper = (upper_byte >> 4) & 0x03;		// Least significant 2 bits
				Y1_lower = lower_row_ptr[2 * column + 1];	// Most significant 8 bits
				Y1 = (Y1_lower << 8) | (Y1_upper << 6);

				// Reconstruct Cr
				Cr_upper = (upper_byte >> 2) & 0x03;		// Least significant 2 bits
				Cr_lower = lower_row_ptr[2 * column + 2];	// Most significant 8 bits
				Cr = (Cr_lower << 8) | (Cr_upper << 6);

				// Reconstruct Y2
				Y2_upper = (upper_byte >> 0) & 0x03;		// Least significant 2 bits
				Y2_lower = lower_row_ptr[2 * column + 3];	// Most significant 8 bits
				Y2 = (Y2_lower << 8) | (Y2_upper << 6);

				// Subtract the luma and chroma offsets and convert to floating point
				Y1_float = (float)(Y1 - luma_offset_shifted);
				Y2_float = (float)(Y2 - luma_offset_shifted);
				Cr_float = (float)(Cr - chroma_offset_shifted);
				Cb_float = (float)(Cb - chroma_offset_shifted);

				// Convert to RGB and scale to 16 bits (first RGB tuple)
				R1_float = fp.ymult * Y1_float;
				R1_float += (fp.r_vmult * Cr);

				G1_float = fp.ymult * Y1_float;
				G1_float -= (fp.g_vmult * Cr);
				G1_float -= (fp.g_umult * Cb);

				B1_float = fp.ymult * Y1_float;
				B1_float += (fp.b_umult * Cb);

				R1 = (int)R1_float;
				G1 = (int)G1_float;
				B1 = (int)B1_float;

				// Force the RGB values into valid range
				if (R1_float < 0) R1 = 0;
				if (G1_float < 0) G1 = 0;
				if (B1_float < 0) B1 = 0;
#if 0
				// Comparison is always false due to limited range of data type
				if (R1 > RGB_max) R1 = RGB_max;
				if (G1 > RGB_max) G1 = RGB_max;
				if (B1 > RGB_max) B1 = RGB_max;
#endif
				// Convert to RGB and scale to 16 bits (second RGB tuple)
				R2_float = fp.ymult * Y2_float;
				R2_float += (fp.r_vmult * Cr);

				G2_float = fp.ymult * Y2_float;
				G2_float -= (fp.g_vmult * Cr);
				G2_float -= (fp.g_umult * Cb);

				B2_float = fp.ymult * Y2_float;
				B2_float += (fp.b_umult * Cb);

				R2 = (int)R2_float;
				G2 = (int)G2_float;
				B2 = (int)B2_float;

				// Force the RGB values into valid range
				if (R2_float < 0) R2 = 0;
				if (G2_float < 0) G2 = 0;
				if (B2_float < 0) B2 = 0;
#if 0
				// Comparison is always false due to limited range of data type
				if (R2 > RGB_max) R2 = RGB_max;
				if (G2 > RGB_max) G2 = RGB_max;
				if (B2 > RGB_max) B2 = RGB_max;
#endif
				// Output two pixels of RGB color components
				output_row_ptr[3 * column + 0] = R1;
				output_row_ptr[3 * column + 1] = G1;
				output_row_ptr[3 * column + 2] = B1;

				output_row_ptr[3 * column + 3] = R2;
				output_row_ptr[3 * column + 4] = G2;
				output_row_ptr[3 * column + 5] = B2;
			}

			upper_row_ptr += upper_row_pitch;
			lower_row_ptr += lower_row_pitch;
			output_row_ptr += output_row_pitch;
		}
	}
}

void CImageConverterRGB32::ConvertToB64A(unsigned char *input, long input_pitch,
										 unsigned char *output, long output_pitch,
										 int width, int height)
{
	uint8_t *input_row_ptr = (uint8_t *)input;
	uint8_t *output_row_ptr = (uint8_t *)output;

	for (int row = 0; row < height; row++)
	{
		uint8_t *bgra8_ptr = input_row_ptr;
		uint16_t *bgra16_ptr = (uint16_t *)output_row_ptr;

		for (int column = 0; column < width; column++)
		{
			// Get the next ARGB tuple
			uint16_t A = *(bgra8_ptr++);
			uint16_t R = *(bgra8_ptr++);
			uint16_t G = *(bgra8_ptr++);
			uint16_t B = *(bgra8_ptr++);

			// Shift to 16 bits
			A <<= 8;
			R <<= 8;
			G <<= 8;
			B <<= 8;

			// Output the 16-bit BGRA tuple
			*(bgra16_ptr++) = B;
			*(bgra16_ptr++) = G;
			*(bgra16_ptr++) = R;
			*(bgra16_ptr++) = A;
		}

		input_row_ptr += input_pitch;
		output_row_ptr += output_pitch;
	}
}

// Compute the coefficients for YUV to RGB conversion (unsigned 16-bit coefficients)
//template <typename uint16_t>
template<>
void YUVToRGB<uint16_t>::ComputeCoefficients(ColorFlags color_flags)
{
	// Initialize the color conversion coefficients
	switch (color_flags & COLOR_FLAGS_MASK)
	{
		case 0:				// Computer systems 601
		luma_offset = 16;
		C_y = 9535;			// 1.164
		C_rv = 13074;		// 1.596
		C_gv = 6660;		// 0.813
		C_gu = 3203;		// 0.391
		C_bu = 16531;		// 2.018
		break;

		case VSRGB:			// Video systems 601
		luma_offset = 0;
		C_y = 8192;			// 1.0
		C_rv = 11231;		// 1.371
		C_gv = 5718;		// 0.698
		C_gu = 2753;		// 0.336
		C_bu = 14189;		// 1.732
		break;

		case CS709:			// Computer systems 709
		luma_offset = 16;
		C_y = 9535;			// 1.164
		C_rv = 14688;		// 1.793
		C_gv = 4375;		// 0.534
		C_gu = 1745;		// 0.213
		C_bu = 17326;		// 2.115
		break;

		case CS709+VSRGB:	// Video Systems 709
		luma_offset = 0;
		C_y = 8192;			// 1.0
		C_rv = 12616;		// 1.540
		C_gv = 3760;		// 0.459
		C_gu = 1499;		// 0.183
		C_bu = 14877;		// 1.816
		break;
	}

	chroma_offset = 128;
}


// Compute the coefficients for RGB to YUV conversion (unsigned 16-bit coefficients)
//template <typename uint16_t>
template<>
void RGBToYUV<uint16_t>::ComputeCoefficients(ColorFlags color_flags)
{
	// Initialize the color conversion coefficients
	switch (color_flags & COLOR_FLAGS_MASK)
	{
		case 0:				// Computer systems 601
		luma_offset = 0;
		
		C_yr = ScaleCoefficientToPrecisionDiscrete(0.299, precision); 
		C_yg = ScaleCoefficientToPrecisionDiscrete(0.587, precision); 
		C_yb = ScaleCoefficientToPrecisionDiscrete(0.114, precision); 

		C_ur = ScaleCoefficientToPrecisionDiscrete(0.172, precision); // (used negatively)
		C_ug = ScaleCoefficientToPrecisionDiscrete(0.339, precision); // (used negatively)
		C_ub = ScaleCoefficientToPrecisionDiscrete(0.511, precision); 

		C_vr = ScaleCoefficientToPrecisionDiscrete(0.511, precision); 
		C_vg = ScaleCoefficientToPrecisionDiscrete(0.428, precision); // (used negatively)
		C_vb = ScaleCoefficientToPrecisionDiscrete(0.083, precision); // (used negatively)

		break;

		case VSRGB:			// Video systems 601
		luma_offset = 16;
		
		C_yr = ScaleCoefficientToPrecisionDiscrete(0.257, precision); 
		C_yg = ScaleCoefficientToPrecisionDiscrete(0.504, precision); 
		C_yb = ScaleCoefficientToPrecisionDiscrete(0.098, precision); 

		C_ur = ScaleCoefficientToPrecisionDiscrete(0.148, precision); // (used negatively)
		C_ug = ScaleCoefficientToPrecisionDiscrete(0.291, precision); // (used negatively)
		C_ub = ScaleCoefficientToPrecisionDiscrete(0.439, precision); 

		C_vr = ScaleCoefficientToPrecisionDiscrete(0.439, precision); 
		C_vg = ScaleCoefficientToPrecisionDiscrete(0.368, precision); // (used negatively)
		C_vb = ScaleCoefficientToPrecisionDiscrete(0.071, precision); // (used negatively)

		break;

		case CS709:			// Computer systems 709
		luma_offset = 0;
		
		C_yr = ScaleCoefficientToPrecisionDiscrete(0.213, precision); 
		C_yg = ScaleCoefficientToPrecisionDiscrete(0.715, precision); 
		C_yb = ScaleCoefficientToPrecisionDiscrete(0.072, precision); 

		C_ur = ScaleCoefficientToPrecisionDiscrete(0.117, precision); // (used negatively)
		C_ug = ScaleCoefficientToPrecisionDiscrete(0.394, precision); // (used negatively)
		C_ub = ScaleCoefficientToPrecisionDiscrete(0.511, precision); 

		C_vr = ScaleCoefficientToPrecisionDiscrete(0.511, precision); 
		C_vg = ScaleCoefficientToPrecisionDiscrete(0.464, precision); // (used negatively)
		C_vb = ScaleCoefficientToPrecisionDiscrete(0.047, precision); // (used negatively)
		
		break;

		case CS709+VSRGB:	// Video Systems 709
		luma_offset = 16;
		
		C_yr = ScaleCoefficientToPrecisionDiscrete(0.183, precision); 
		C_yg = ScaleCoefficientToPrecisionDiscrete(0.614, precision); 
		C_yb = ScaleCoefficientToPrecisionDiscrete(0.062, precision); 

		C_ur = ScaleCoefficientToPrecisionDiscrete(0.101, precision); // (used negatively)
		C_ug = ScaleCoefficientToPrecisionDiscrete(0.338, precision); // (used negatively)
		C_ub = ScaleCoefficientToPrecisionDiscrete(0.439, precision); 

		C_vr = ScaleCoefficientToPrecisionDiscrete(0.439, precision); 
		C_vg = ScaleCoefficientToPrecisionDiscrete(0.399, precision); // (used negatively)
		C_vb = ScaleCoefficientToPrecisionDiscrete(0.040, precision); // (used negatively)

		break;
	}

	chroma_offset = 128;
}

// Compute the coefficients for RGB to YUV conversion (unsigned 16-bit coefficients)
//template <typename uint16_t>
template<>
void RGBToYUV<double>::ComputeCoefficients(ColorFlags color_flags)
{
	// Initialize the color conversion coefficients
	switch (color_flags & COLOR_FLAGS_MASK)
	{
		case 0:				// Computer systems 601
		luma_offset = 0;
		
		C_yr = 0.299; 
		C_yg = 0.587; 
		C_yb = 0.114; 

		C_ur = 0.172; // (used negatively)
		C_ug = 0.339; // (used negatively)
		C_ub = 0.511; 

		C_vr = 0.511; 
		C_vg = 0.428; // (used negatively)
		C_vb = 0.083; // (used negatively)

		break;

		case VSRGB:			// Video systems 601
		luma_offset = 16;
		
		C_yr = 0.257; 
		C_yg = 0.504; 
		C_yb = 0.098; 

		C_ur = 0.148; // (used negatively)
		C_ug = 0.291; // (used negatively)
		C_ub = 0.439; 

		C_vr = 0.439; 
		C_vg = 0.368; // (used negatively)
		C_vb = 0.071; // (used negatively)

		break;

		case CS709:			// Computer systems 709
		luma_offset = 0;
		
		C_yr = 0.213; 
		C_yg = 0.715; 
		C_yb = 0.072; 

		C_ur = 0.117; // (used negatively)
		C_ug = 0.394; // (used negatively)
		C_ub = 0.511; 

		C_vr = 0.511; 
		C_vg = 0.464; // (used negatively)
		C_vb = 0.047; // (used negatively)
		
		break;

		case CS709+VSRGB:	// Video Systems 709
		luma_offset = 16;
		
		C_yr = 0.183; 
		C_yg = 0.614; 
		C_yb = 0.062; 

		C_ur = 0.101; // (used negatively)
		C_ug = 0.338; // (used negatively)
		C_ub = 0.439; 

		C_vr = 0.439; 
		C_vg = 0.399; // (used negatively)
		C_vb = 0.040; // (used negatively)

		break;
	}

	chroma_offset = 128;
}

// Convert an image of NV12 pixels to the DPX 10-bit RGB pixel format
void CImageConverterNV12ToRGB::ConvertToDPX0(uint8_t *luma_row_ptr,
											 uint8_t *chroma_row_ptr,
											 uint8_t *output_row_ptr,
											 int width)
{
	// Scale the intermediate results to 16-bit precision
	const int shift = 5;

	// Process two columns per iteration
	for (int column = 0; column < width; column += 2)
	{
		uint8_t *luma_ptr = luma_row_ptr + column;
		uint8_t *chroma_ptr = chroma_row_ptr + column;
		uint32_t *output_ptr = reinterpret_cast<uint32_t *>(output_row_ptr + column * sizeof(uint32_t));

		int32_t Y1 = *(luma_ptr++);
		int32_t Y2 = *(luma_ptr++);
		int32_t U = *(chroma_ptr++);
		int32_t V = *(chroma_ptr++);

		// Subtract the luma and chroma offsets
		Y1 -= luma_offset;
		Y2 -= luma_offset;
		U -= chroma_offset;
		V -= chroma_offset;

		// Convert YUV to the first RGB tuple
		int32_t R1 = C_y * Y1 + C_rv * V;
		int32_t G1 = C_y * Y1 - C_gv * V - C_gu * U;
		int32_t B1 = C_y * Y1 + C_bu * U;

		// Convert YUV to the second RGB tuple
		int32_t R2 = C_y * Y2 + C_rv * V;
		int32_t G2 = C_y * Y2 - C_gv * V - C_gu * U;
		int32_t B2 = C_y * Y2 + C_bu * U;

		// Scale the output values to 16 bits
		R1 = Clamp16u(R1 >> shift);
		R2 = Clamp16u(R2 >> shift);
		G1 = Clamp16u(G1 >> shift);
		G2 = Clamp16u(G2 >> shift);
		B1 = Clamp16u(B1 >> shift);
		B2 = Clamp16u(B2 >> shift);

		// Pack and output the pair of RGB tuples
		*(output_ptr++) = Pack10(R1, G1, B1);
		*(output_ptr++) = Pack10(R2, G2, B2);
	}
}

//TODO: Check that the colorspace setting is correct
void CImageConverterNV12ToRGB::ConvertToDPX0(void *input_buffer, size_t input_pitch,
											 void *output_buffer, size_t output_pitch,
											 int width, int height)
{
	uint8_t *luma_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	uint8_t *chroma_row_ptr = luma_row_ptr + (width * height);

	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);

	// Scale the intermediate results to 16-bit precision
	//const int shift = 5;

	for (int row = 0; row < height; row++)
	{
		ConvertToDPX0(luma_row_ptr, chroma_row_ptr, output_row_ptr, width);

		// Advance to the next row in the luma plane
		luma_row_ptr += input_pitch;

		// Each row of chroma is used twice (4:2:0 sampling)
		if (row % 2) {
			chroma_row_ptr += input_pitch;
		}

		// Advance to the next output row
		output_row_ptr += output_pitch;
	}
}

// Converts a row of RGBA pixels to NV12
void CImageConverterRGBToNV12::Convert8bitRGBAToNV12(uint8_t *src_row_ptr, uint8_t *src_row_ptr_next, uint8_t *dst_luma_row_ptr, uint8_t *dst_luma_row_ptr_next, uint8_t *dst_chroma_row_ptr, uint32_t pixWidth)
{
	// shift using base class member variable 'precision'

	// Process two columns per iteration
	for (int column = 0; column < (int)pixWidth; column += 2)
	{
		uint8_t *luma_ptr = dst_luma_row_ptr + column;
		uint8_t *luma_ptr_next = dst_luma_row_ptr_next + column;
		uint8_t *chroma_ptr = dst_chroma_row_ptr + column;
		uint8_t *src_ptr = reinterpret_cast<uint8_t *>(src_row_ptr + (column * 4));
		uint8_t *src_ptr_next = reinterpret_cast<uint8_t *>(src_row_ptr_next + (column * 4));
		
		int32_t R1 = *((src_ptr + rIndex));
		int32_t G1 = *((src_ptr + gIndex));
		int32_t B1 = *((src_ptr + bIndex));
		// Ignore the alpha
		src_ptr+=4;

		int32_t R2 = *((src_ptr + rIndex));
		int32_t G2 = *((src_ptr + gIndex));
		int32_t B2 = *((src_ptr + bIndex));
		// Ignore the alpha
		src_ptr+=4;

		int32_t R1Next = *((src_ptr_next + rIndex));
		int32_t G1Next = *((src_ptr_next + gIndex));
		int32_t B1Next = *((src_ptr_next + bIndex));
		// Ignore the alpha
		src_ptr_next+=4;

		int32_t R2Next = *((src_ptr_next + rIndex));
		int32_t G2Next = *((src_ptr_next + gIndex));
		int32_t B2Next = *((src_ptr_next + bIndex));
		// Ignore the alpha
		src_ptr_next+=4;

		uint32_t Y1, U1, V1, Y2, U2, V2, Y1Next, U1Next, V1Next, Y2Next, U2Next, V2Next, UOut, VOut;

		Y1		= (( (C_yr*R1) + (C_yg*G1) + (C_yb*B1))>>precision) + luma_offset;
		U1		= ((-(C_ur*R1) - (C_ug*G1) + (C_ub*B1))>>precision) + chroma_offset;
		V1		= (( (C_vr*R1) - (C_vg*G1) - (C_vb*B1))>>precision) + chroma_offset;

		Y2		= (( (C_yr*R2) + (C_yg*G2) + (C_yb*B2))>>precision) + luma_offset;
		U2		= ((-(C_ur*R2) - (C_ug*G2) + (C_ub*B2))>>precision) + chroma_offset;
		V2		= (( (C_vr*R2) - (C_vg*G2) - (C_vb*B2))>>precision) + chroma_offset;

		Y1Next	= (( (C_yr*R1Next) + (C_yg*G1Next) + (C_yb*B1Next))>>precision) + luma_offset;
		U1Next	= ((-(C_ur*R1Next) - (C_ug*G1Next) + (C_ub*B1Next))>>precision) + chroma_offset;
		V1Next	= (( (C_vr*R1Next) - (C_vg*G1Next) - (C_vb*B1Next))>>precision) + chroma_offset;

		Y2Next	= (( (C_yr*R2Next) + (C_yg*G2Next) + (C_yb*B2Next))>>precision) + luma_offset;
		U2Next	= ((-(C_ur*R2Next) - (C_ug*G2Next) + (C_ub*B2Next))>>precision) + chroma_offset;
		V2Next	= (( (C_vr*R2Next) - (C_vg*G2Next) - (C_vb*B2Next))>>precision) + chroma_offset;

		UOut	= (U1+U2+U1Next+U2Next)>>2;
		VOut	= (V1+V2+V1Next+V2Next)>>2;

		*(luma_ptr++) = Y1;
		*(luma_ptr++) = Y2;

		*(luma_ptr_next++) = Y1Next;
		*(luma_ptr_next++) = Y2Next;

		*(chroma_ptr++) = UOut;
		*(chroma_ptr++) = VOut;
	}
}

// Converts a row of RGBA pixels to NV12
void CImageConverterRGBToNV12::Convert8bitRGBAToNV12_SSE2(uint8_t *src_row_ptr, uint8_t *src_row_ptr_next, uint8_t *dst_luma_row_ptr, uint8_t *dst_luma_row_ptr_next, uint8_t *dst_chroma_row_ptr, uint32_t pixWidth)
{
	uint8_t *luma_ptr, *luma_ptr_next, *chroma_ptr, *src_ptr, *src_ptr_next;

	__m128i bThisRowFront128, bNextRowFront128, gThisRowFront128, gNextRowFront128, rThisRowFront128, rNextRowFront128,
			bThisRowBack128, bNextRowBack128, gThisRowBack128, gNextRowBack128, rThisRowBack128, rNextRowBack128,
			yThisRowFront128, yNextRowFront128, uThisRowFront128, uNextRowFront128, vThisRowFront128, vNextRowFront128,
			yThisRowBack128, yNextRowBack128, uThisRowBack128, uNextRowBack128, vThisRowBack128, vNextRowBack128,
			uAFront128, uABack128, uBFront128, uBBack128, vAFront128, vABack128, vBFront128, vBBack128, uvFront128, uvBack128,
			C_yb128, C_yg128, C_yr128, C_ub128, C_ug128, C_ur128, C_vb128, C_vg128, C_vr128,
			lumaOffset128, chromaOffset128, one128, uSubsampleAndMask128, vSubsampleAndMask128;

	__m128i *yThisPtr128, *yNextPtr128, *uvPtr128;
			
	const uint32_t sseBitePerIteration = 16; // process 16 pixels per iteration - THIS SHOULD NEVER CHANGE AS THIS IMPLEMENTATION DEPENDS UPON THIS VALUE BEING 16!!!!!!!
	const uint32_t ssePortion = pixWidth - (pixWidth%sseBitePerIteration);

	//these 128 values will never change, so we can load them here
	C_yr128 = _mm_set1_epi16(C_yr);
	C_yg128 = _mm_set1_epi16(C_yg);
	C_yb128 = _mm_set1_epi16(C_yb);
	
	C_ur128 = _mm_set1_epi16(-C_ur);
	C_ug128 = _mm_set1_epi16(-C_ug);
	C_ub128 = _mm_set1_epi16(C_ub);
	
	C_vr128 = _mm_set1_epi16(C_vr);
	C_vg128 = _mm_set1_epi16(-C_vg);
	C_vb128 = _mm_set1_epi16(-C_vb);
	
	

	lumaOffset128 = _mm_set1_epi16(luma_offset);

	chromaOffset128 = _mm_set1_epi16(chroma_offset);

	one128 = _mm_set1_epi16(1);

	uSubsampleAndMask128 = _mm_set_epi16(UINT16_MAX, 0, UINT16_MAX, 0, UINT16_MAX, 0, UINT16_MAX, 0);
	vSubsampleAndMask128 = _mm_set_epi16(0, UINT16_MAX, 0, UINT16_MAX, 0, UINT16_MAX, 0, UINT16_MAX);
	
	uint32_t column;

	// Process 16 columns per iteration
	for (column = 0; column < ssePortion; column += sseBitePerIteration)
	{
		// get to the necessary point in the buffers
		src_ptr = reinterpret_cast<uint8_t *>(src_row_ptr + (column * 4));
		src_ptr_next = reinterpret_cast<uint8_t *>(src_row_ptr_next + (column * 4));
		luma_ptr = dst_luma_row_ptr + column;
		luma_ptr_next = dst_luma_row_ptr_next + column;
		chroma_ptr = dst_chroma_row_ptr + column;

		yThisPtr128 = (__m128i*)luma_ptr;
		yNextPtr128 = (__m128i*)luma_ptr_next;
		uvPtr128    = (__m128i*)chroma_ptr;

		// clear out these values
		yThisRowFront128 = yNextRowFront128 = uThisRowFront128 = uNextRowFront128 = vThisRowFront128 = vNextRowFront128 = 
		yThisRowBack128 = yNextRowBack128 = uThisRowBack128 = uNextRowBack128 = vThisRowBack128 = vNextRowBack128 = _mm_setzero_si128();

		// load up the [0, 255] b values of the 16 pixels of this row, and then the next row
		bThisRowFront128 = _mm_set_epi16(*(src_ptr      + bIndex + 0), *(src_ptr      + bIndex + 4), *(src_ptr      + bIndex + 8), *(src_ptr      + bIndex + 12), *(src_ptr      + bIndex + 16), *(src_ptr      + bIndex + 20), *(src_ptr      + bIndex + 24), *(src_ptr      + bIndex + 28));
		bThisRowBack128  = _mm_set_epi16(*(src_ptr      + bIndex + 32), *(src_ptr      + bIndex + 36), *(src_ptr      + bIndex + 40), *(src_ptr      + bIndex + 44), *(src_ptr      + bIndex + 48), *(src_ptr      + bIndex + 52), *(src_ptr      + bIndex + 56), *(src_ptr      + bIndex + 60));
		bNextRowFront128 = _mm_set_epi16(*(src_ptr_next + bIndex + 0), *(src_ptr_next + bIndex + 4), *(src_ptr_next + bIndex + 8), *(src_ptr_next + bIndex + 12), *(src_ptr_next + bIndex + 16), *(src_ptr_next + bIndex + 20), *(src_ptr_next + bIndex + 24), *(src_ptr_next + bIndex + 28));
		bNextRowBack128  = _mm_set_epi16(*(src_ptr_next + bIndex + 32), *(src_ptr_next + bIndex + 36), *(src_ptr_next + bIndex + 40), *(src_ptr_next + bIndex + 44), *(src_ptr_next + bIndex + 48), *(src_ptr_next + bIndex + 52), *(src_ptr_next + bIndex + 56), *(src_ptr_next + bIndex + 60));
		
		// load up the [0, 255] g values of the 16 pixels of this row, and then the next row
		gThisRowFront128 = _mm_set_epi16(*(src_ptr      + gIndex + 0), *(src_ptr      + gIndex + 4), *(src_ptr      + gIndex + 8), *(src_ptr      + gIndex + 12), *(src_ptr      + gIndex + 16), *(src_ptr      + gIndex + 20), *(src_ptr      + gIndex + 24), *(src_ptr      + gIndex + 28));
		gThisRowBack128  = _mm_set_epi16(*(src_ptr      + gIndex + 32), *(src_ptr      + gIndex + 36), *(src_ptr      + gIndex + 40), *(src_ptr      + gIndex + 44), *(src_ptr      + gIndex + 48), *(src_ptr      + gIndex + 52), *(src_ptr      + gIndex + 56), *(src_ptr      + gIndex + 60));
		gNextRowFront128 = _mm_set_epi16(*(src_ptr_next + gIndex + 0), *(src_ptr_next + gIndex + 4), *(src_ptr_next + gIndex + 8), *(src_ptr_next + gIndex + 12), *(src_ptr_next + gIndex + 16), *(src_ptr_next + gIndex + 20), *(src_ptr_next + gIndex + 24), *(src_ptr_next + gIndex + 28));
		gNextRowBack128  = _mm_set_epi16(*(src_ptr_next + gIndex + 32), *(src_ptr_next + gIndex + 36), *(src_ptr_next + gIndex + 40), *(src_ptr_next + gIndex + 44), *(src_ptr_next + gIndex + 48), *(src_ptr_next + gIndex + 52), *(src_ptr_next + gIndex + 56), *(src_ptr_next + gIndex + 60));
		
		// load up the [0, 255] r values of the 16 pixels of this row, and then the next row
		rThisRowFront128 = _mm_set_epi16(*(src_ptr      + rIndex + 0), *(src_ptr      + rIndex + 4), *(src_ptr      + rIndex + 8), *(src_ptr      + rIndex + 12), *(src_ptr      + rIndex + 16), *(src_ptr      + rIndex + 20), *(src_ptr      + rIndex + 24), *(src_ptr      + rIndex + 28));
		rThisRowBack128  = _mm_set_epi16(*(src_ptr      + rIndex + 32), *(src_ptr      + rIndex + 36), *(src_ptr      + rIndex + 40), *(src_ptr      + rIndex + 44), *(src_ptr      + rIndex + 48), *(src_ptr      + rIndex + 52), *(src_ptr      + rIndex + 56), *(src_ptr      + rIndex + 60));
		rNextRowFront128 = _mm_set_epi16(*(src_ptr_next + rIndex + 0), *(src_ptr_next + rIndex + 4), *(src_ptr_next + rIndex + 8), *(src_ptr_next + rIndex + 12), *(src_ptr_next + rIndex + 16), *(src_ptr_next + rIndex + 20), *(src_ptr_next + rIndex + 24), *(src_ptr_next + rIndex + 28));
		rNextRowBack128  = _mm_set_epi16(*(src_ptr_next + rIndex + 32), *(src_ptr_next + rIndex + 36), *(src_ptr_next + rIndex + 40), *(src_ptr_next + rIndex + 44), *(src_ptr_next + rIndex + 48), *(src_ptr_next + rIndex + 52), *(src_ptr_next + rIndex + 56), *(src_ptr_next + rIndex + 60));
		
		// add one to everyone we just set to get the range to [1, 256]
		bThisRowFront128  = _mm_add_epi16(bThisRowFront128, one128);
		bThisRowBack128   = _mm_add_epi16(bThisRowBack128, one128);
		bNextRowFront128  = _mm_add_epi16(bNextRowFront128, one128);
		bNextRowBack128  = _mm_add_epi16(bNextRowBack128, one128);

		gThisRowFront128  = _mm_add_epi16(gThisRowFront128, one128);
		gThisRowBack128   = _mm_add_epi16(gThisRowBack128, one128);
		gNextRowFront128  = _mm_add_epi16(gNextRowFront128, one128);
		gNextRowBack128  = _mm_add_epi16(gNextRowBack128, one128);

		rThisRowFront128  = _mm_add_epi16(rThisRowFront128, one128);
		rThisRowBack128   = _mm_add_epi16(rThisRowBack128, one128);
		rNextRowFront128  = _mm_add_epi16(rNextRowFront128, one128);
		rNextRowBack128  = _mm_add_epi16(rNextRowBack128, one128);
		
		// Compute Y, front, back, this and next
		// -----------------------------------------------------------------------------------------------------------------------------------------------------

		// perform a mulhi with the respective coefficient on each to perform "discrete normalization" using the current 'precision', accumulate the result in Y
		yThisRowFront128 = _mm_adds_epi16(yThisRowFront128, _mm_mulhi_epu16(bThisRowFront128, C_yb128));
		yThisRowFront128 = _mm_adds_epi16(yThisRowFront128, _mm_mulhi_epu16(gThisRowFront128, C_yg128));
		yThisRowFront128 = _mm_adds_epi16(yThisRowFront128, _mm_mulhi_epu16(rThisRowFront128, C_yr128));

		yThisRowBack128 = _mm_adds_epi16(yThisRowBack128, _mm_mulhi_epu16(bThisRowBack128, C_yb128));
		yThisRowBack128 = _mm_adds_epi16(yThisRowBack128, _mm_mulhi_epu16(gThisRowBack128, C_yg128));
		yThisRowBack128 = _mm_adds_epi16(yThisRowBack128, _mm_mulhi_epu16(rThisRowBack128, C_yr128));
		
		yNextRowFront128 = _mm_adds_epi16(yNextRowFront128, _mm_mulhi_epu16(bNextRowFront128, C_yb128));
		yNextRowFront128 = _mm_adds_epi16(yNextRowFront128, _mm_mulhi_epu16(gNextRowFront128, C_yg128));
		yNextRowFront128 = _mm_adds_epi16(yNextRowFront128, _mm_mulhi_epu16(rNextRowFront128, C_yr128));

		yNextRowBack128 = _mm_adds_epi16(yNextRowBack128, _mm_mulhi_epu16(bNextRowBack128, C_yb128));
		yNextRowBack128 = _mm_adds_epi16(yNextRowBack128, _mm_mulhi_epu16(gNextRowBack128, C_yg128));
		yNextRowBack128 = _mm_adds_epi16(yNextRowBack128, _mm_mulhi_epu16(rNextRowBack128, C_yr128));

		// tack on the luma offset
		yThisRowFront128 = _mm_adds_epu16(yThisRowFront128, lumaOffset128);
		yThisRowBack128 = _mm_adds_epu16(yThisRowBack128, lumaOffset128);

		yNextRowFront128 = _mm_adds_epu16(yNextRowFront128, lumaOffset128);
		yNextRowBack128 = _mm_adds_epu16(yNextRowBack128, lumaOffset128);

		// Compute U, front, back, this and next
		// -----------------------------------------------------------------------------------------------------------------------------------------------------

		// perform a mulhi with the respective coefficient on each to perform "discrete normalization" using the current 'precision', accumulate the result in U
		uThisRowFront128 = _mm_adds_epi16(uThisRowFront128, _mm_mulhi_epi16(bThisRowFront128, C_ub128));
		uThisRowFront128 = _mm_adds_epi16(uThisRowFront128, _mm_mulhi_epi16(gThisRowFront128, C_ug128));
		uThisRowFront128 = _mm_adds_epi16(uThisRowFront128, _mm_mulhi_epi16(rThisRowFront128, C_ur128));

		uThisRowBack128 = _mm_adds_epi16(uThisRowBack128, _mm_mulhi_epi16(bThisRowBack128, C_ub128));
		uThisRowBack128 = _mm_adds_epi16(uThisRowBack128, _mm_mulhi_epi16(gThisRowBack128, C_ug128));
		uThisRowBack128 = _mm_adds_epi16(uThisRowBack128, _mm_mulhi_epi16(rThisRowBack128, C_ur128));
		
		uNextRowFront128 = _mm_adds_epi16(uNextRowFront128, _mm_mulhi_epi16(bNextRowFront128, C_ub128));
		uNextRowFront128 = _mm_adds_epi16(uNextRowFront128, _mm_mulhi_epi16(gNextRowFront128, C_ug128));
		uNextRowFront128 = _mm_adds_epi16(uNextRowFront128, _mm_mulhi_epi16(rNextRowFront128, C_ur128));

		uNextRowBack128 = _mm_adds_epi16(uNextRowBack128, _mm_mulhi_epi16(bNextRowBack128, C_ub128));
		uNextRowBack128 = _mm_adds_epi16(uNextRowBack128, _mm_mulhi_epi16(gNextRowBack128, C_ug128));
		uNextRowBack128 = _mm_adds_epi16(uNextRowBack128, _mm_mulhi_epi16(rNextRowBack128, C_ur128));

		// tack on the chroma offset
		uThisRowFront128 = _mm_adds_epi16(uThisRowFront128, chromaOffset128);
		uThisRowBack128 = _mm_adds_epi16(uThisRowBack128, chromaOffset128);

		uNextRowFront128 = _mm_adds_epi16(uNextRowFront128, chromaOffset128);
		uNextRowBack128 = _mm_adds_epi16(uNextRowBack128, chromaOffset128);
		
		// Compute V, front, back, this and next
		// -----------------------------------------------------------------------------------------------------------------------------------------------------

		// perform a mulhi with the respective coefficient on each to perform "discrete normalization" using the current 'precision', accumulate the result in U
		vThisRowFront128 = _mm_adds_epi16(vThisRowFront128, _mm_mulhi_epi16(bThisRowFront128, C_vb128));
		vThisRowFront128 = _mm_adds_epi16(vThisRowFront128, _mm_mulhi_epi16(gThisRowFront128, C_vg128));
		vThisRowFront128 = _mm_adds_epi16(vThisRowFront128, _mm_mulhi_epi16(rThisRowFront128, C_vr128));

		vThisRowBack128 = _mm_adds_epi16(vThisRowBack128, _mm_mulhi_epi16(bThisRowBack128, C_vb128));
		vThisRowBack128 = _mm_adds_epi16(vThisRowBack128, _mm_mulhi_epi16(gThisRowBack128, C_vg128));
		vThisRowBack128 = _mm_adds_epi16(vThisRowBack128, _mm_mulhi_epi16(rThisRowBack128, C_vr128));
		
		vNextRowFront128 = _mm_adds_epi16(vNextRowFront128, _mm_mulhi_epi16(bNextRowFront128, C_vb128));
		vNextRowFront128 = _mm_adds_epi16(vNextRowFront128, _mm_mulhi_epi16(gNextRowFront128, C_vg128));
		vNextRowFront128 = _mm_adds_epi16(vNextRowFront128, _mm_mulhi_epi16(rNextRowFront128, C_vr128));

		vNextRowBack128 = _mm_adds_epi16(vNextRowBack128, _mm_mulhi_epi16(bNextRowBack128, C_vb128));
		vNextRowBack128 = _mm_adds_epi16(vNextRowBack128, _mm_mulhi_epi16(gNextRowBack128, C_vg128));
		vNextRowBack128 = _mm_adds_epi16(vNextRowBack128, _mm_mulhi_epi16(rNextRowBack128, C_vr128));

		// tack on the chroma offset
		vThisRowFront128 = _mm_adds_epi16(vThisRowFront128, chromaOffset128);
		vThisRowBack128 = _mm_adds_epi16(vThisRowBack128, chromaOffset128);

		vNextRowFront128 = _mm_adds_epi16(vNextRowFront128, chromaOffset128);
		vNextRowBack128 = _mm_adds_epi16(vNextRowBack128, chromaOffset128);

		// Perform the supsampling of U, moving it into position for ORing with V which is conceptually [Val, 0, Val, 0, Val, 0, Val, 0]
		// -----------------------------------------------------------------------------------------------------------------------------------------------------

		// add Next to This, storing value in This 
		uThisRowFront128 = _mm_adds_epi16(uThisRowFront128, uNextRowFront128);
		uThisRowBack128 = _mm_adds_epi16(uThisRowBack128, uNextRowBack128);
	
		// pull out (and shift if necesary) the values we will be adding
		uAFront128 = _mm_andnot_si128(uSubsampleAndMask128, uThisRowFront128);
		uBFront128 = _mm_and_si128(uSubsampleAndMask128, uThisRowFront128);
		uBFront128 = _mm_srli_si128(uBFront128, 2); // shift if in BYTES

		uABack128 = _mm_andnot_si128(uSubsampleAndMask128, uThisRowBack128);
		uBBack128 = _mm_and_si128(uSubsampleAndMask128, uThisRowBack128);
		uBBack128 = _mm_srli_si128(uBBack128, 2); // shift if in BYTES

		// add A and B together, storing the value in A
		uAFront128 = _mm_adds_epi16(uAFront128, uBFront128);
		uABack128 = _mm_adds_epi16(uABack128, uBBack128);

		// kick result down 2 bits to divide by for and, essentially, find the average of the four U values we just added together
		uAFront128 = _mm_srli_epi16(uAFront128, 2); // shift if in BITS
		uABack128 = _mm_srli_epi16(uABack128, 2); // shift if in BITS
		
		// Perform the supsampling of V, moving it into position for ORing with U which is conceptually [0, Val, 0, Val, 0, Val, 0, Val]
		// -----------------------------------------------------------------------------------------------------------------------------------------------------

		// add Next to This, storing value in This 
		vThisRowFront128 = _mm_adds_epi16(vThisRowFront128, vNextRowFront128);
		vThisRowBack128 = _mm_adds_epi16(vThisRowBack128, vNextRowBack128);
	
		// pull out (and shift if necesary) the values we will be adding
		vAFront128 = _mm_andnot_si128(vSubsampleAndMask128, vThisRowFront128);
		vBFront128 = _mm_and_si128(vSubsampleAndMask128, vThisRowFront128);
		vBFront128 = _mm_slli_si128(vBFront128, 2); // shift if in BYTES

		vABack128 = _mm_andnot_si128(vSubsampleAndMask128, vThisRowBack128);
		vBBack128 = _mm_and_si128(vSubsampleAndMask128, vThisRowBack128);
		vBBack128 = _mm_slli_si128(vBBack128, 2); // shift if in BYTES

		// add A and B together, storing the value in A
		vAFront128 = _mm_adds_epi16(vAFront128, vBFront128);
		vABack128 = _mm_adds_epi16(vABack128, vBBack128);

		// kick result down 2 bits to divide by for and, essentially, find the average of the four V values we just added together
		vAFront128 = _mm_srli_epi16(vAFront128, 2); // shift if in BITS
		vABack128 = _mm_srli_epi16(vABack128, 2); // shift if in BITS

		// Or U and V together -> U[Val, 0, Val, 0, Val, 0, Val, 0] | V[0, Val, 0, Val, 0, Val, 0, Val]
		// -----------------------------------------------------------------------------------------------------------------------------------------------------
		uvFront128 = _mm_or_si128(uAFront128, vAFront128);
		uvBack128 = _mm_or_si128(uABack128, vABack128);

		// Now we have the following - existing as 8bit values stored in 16bit space
		// [yThisFront0, yThisFront1, yThisFront2, yThisFront3, yThisFront4, yThisFront5, yThisFront6, yThisFront7][yThisBack0, yThisBack1, yThisBack2, yThisBack3, yThisBack4, yThisBack5, yThisBack6, yThisBack7]
		// [yNextFront0, yNextFront1, yNextFront2, yNextFront3, yNextFront4, yNextFront5, yNextFront6, yNextFront7][yNextBack0, yNextBack1, yNextBack2, yNextBack3, yNextBack4, yNextBack5, yNextBack6, yNextBack7]
		// [uAvrgFront0, vAvrgFront0, uAvrgFront1, vAvrgFront1, uAvrgFront2, vAvrgFront2, uAvrgFront3, vAvrgFront3][uAvrgBack0, vAvrgBack0, uAvrgBack1, vAvrgBack1, uAvrgBack2, vAvrgBack2, uAvrgBack3, vAvrgBack3]
		//
		// we want to pack the values down to 8bit values stored in 8bit space, and output to the native space at the same time
		_mm_storeu_si128(yThisPtr128, _mm_packus_epi16(yThisRowFront128, yThisRowBack128));
		_mm_storeu_si128(yNextPtr128, _mm_packus_epi16(yNextRowFront128, yNextRowBack128));

		_mm_storeu_si128(uvPtr128, _mm_packus_epi16(uvFront128, uvBack128));
	}

	// finish up in C
	if(column < pixWidth)
	{
		for (/*use existing value for column as the starting value*/; column < pixWidth; column += 2)
		{
			luma_ptr = dst_luma_row_ptr + column;
			luma_ptr_next = dst_luma_row_ptr_next + column;
			chroma_ptr = dst_chroma_row_ptr + column;
			src_ptr = reinterpret_cast<uint8_t *>(src_row_ptr + (column * 4));
			src_ptr_next = reinterpret_cast<uint8_t *>(src_row_ptr_next + (column * 4));
			
			int32_t R1 = *((src_ptr + rIndex));
			int32_t G1 = *((src_ptr + gIndex));
			int32_t B1 = *((src_ptr + bIndex));
			// Ignore the alpha
			src_ptr+=4;

			int32_t R2 = *((src_ptr + rIndex));
			int32_t G2 = *((src_ptr + gIndex));
			int32_t B2 = *((src_ptr + bIndex));
			// Ignore the alpha
			src_ptr+=4;

			int32_t R1Next = *((src_ptr_next + rIndex));
			int32_t G1Next = *((src_ptr_next + gIndex));
			int32_t B1Next = *((src_ptr_next + bIndex));
			// Ignore the alpha
			src_ptr_next+=4;

			int32_t R2Next = *((src_ptr_next + rIndex));
			int32_t G2Next = *((src_ptr_next + gIndex));
			int32_t B2Next = *((src_ptr_next + bIndex));
			// Ignore the alpha
			src_ptr_next+=4;

			uint32_t Y1, U1, V1, Y2, U2, V2, Y1Next, U1Next, V1Next, Y2Next, U2Next, V2Next, UOut, VOut;

			// NOTE: To match with the SSE implementation, we cut the precision at each operation
			Y1		= ( ((C_yr*R1)>>precision) + ((C_yg*G1)>>precision) + ((C_yb*B1)>>precision)) + luma_offset;
			U1		= (-((C_ur*R1)>>precision) - ((C_ug*G1)>>precision) + ((C_ub*B1)>>precision)) + chroma_offset;
			V1		= ( ((C_vr*R1)>>precision) - ((C_vg*G1)>>precision) - ((C_vb*B1)>>precision)) + chroma_offset;

			Y2		= ( ((C_yr*R2)>>precision) + ((C_yg*G2)>>precision) + ((C_yb*B2)>>precision)) + luma_offset;
			U2		= (-((C_ur*R2)>>precision) - ((C_ug*G2)>>precision) + ((C_ub*B2)>>precision)) + chroma_offset;
			V2		= ( ((C_vr*R2)>>precision) - ((C_vg*G2)>>precision) - ((C_vb*B2)>>precision)) + chroma_offset;

			Y1Next	= ( ((C_yr*R1Next)>>precision) + ((C_yg*G1Next)>>precision) + ((C_yb*B1Next)>>precision)) + luma_offset;
			U1Next	= (-((C_ur*R1Next)>>precision) - ((C_ug*G1Next)>>precision) + ((C_ub*B1Next)>>precision)) + chroma_offset;
			V1Next	= ( ((C_vr*R1Next)>>precision) - ((C_vg*G1Next)>>precision) - ((C_vb*B1Next)>>precision)) + chroma_offset;

			Y2Next	= ( ((C_yr*R2Next)>>precision) + ((C_yg*G2Next)>>precision) + ((C_yb*B2Next)>>precision)) + luma_offset;
			U2Next	= (-((C_ur*R2Next)>>precision) - ((C_ug*G2Next)>>precision) + ((C_ub*B2Next)>>precision)) + chroma_offset;
			V2Next	= ( ((C_vr*R2Next)>>precision) - ((C_vg*G2Next)>>precision) - ((C_vb*B2Next)>>precision)) + chroma_offset;

			UOut	= (U1+U2+U1Next+U2Next)>>2;
			VOut	= (V1+V2+V1Next+V2Next)>>2;

			*(luma_ptr++) = Y1;
			*(luma_ptr++) = Y2;

			*(luma_ptr_next++) = Y1Next;
			*(luma_ptr_next++) = Y2Next;

			*(chroma_ptr++) = UOut;
			*(chroma_ptr++) = VOut;
		}
	}
}

// Convert a bitmap of 8Bit pixels to NV12
// the *Index args signify the cadence of the source data - thus, if image is actually BGRA then: bIndex == 0, gIndex == 1, rIndex == 2, aIndex == 3
void CImageConverterRGBToNV12::Convert8bitRGBAToNV12(void *input_buffer, size_t input_pitch,
														void *output_buffer, size_t output_pitch,
														uint32_t pixWidth, uint32_t pixHeight,
														uint32_t rIndexArg, uint32_t gIndexArg, uint32_t bIndexArg, uint32_t aIndexArg)
{
	// hold on to these
	rIndex = rIndexArg;
	gIndex = gIndexArg;
	bIndex = bIndexArg;
	aIndex = aIndexArg;

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t*>(input_buffer);

	uint8_t *luma_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	uint8_t *chroma_row_ptr = luma_row_ptr + (pixWidth * pixHeight);

	for (int row = 0; row < (int)pixHeight; row += 2)
	{
		Convert8bitRGBAToNV12(input_row_ptr, input_row_ptr + input_pitch, luma_row_ptr, luma_row_ptr + output_pitch, chroma_row_ptr, pixWidth);
		
		luma_row_ptr += output_pitch<<1;
		chroma_row_ptr += output_pitch;

		input_row_ptr += input_pitch<<1;
	}
}

// Convert a bitmap of 8Bit pixels to NV12
// the *Index args signify the cadence of the source data - thus, if image is actually BGRA then: bIndex == 0, gIndex == 1, rIndex == 2, aIndex == 3
void CImageConverterRGBToNV12::Convert8bitRGBAToNV12_SSE2(void *input_buffer, size_t input_pitch,
														void *output_buffer, size_t output_pitch,
														uint32_t pixWidth, uint32_t pixHeight,
														uint32_t rIndexArg, uint32_t gIndexArg, uint32_t bIndexArg, uint32_t aIndexArg)
{
	// hold on to these
	rIndex = rIndexArg;
	gIndex = gIndexArg;
	bIndex = bIndexArg;
	aIndex = aIndexArg;

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t*>(input_buffer);

	uint8_t *luma_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	uint8_t *chroma_row_ptr = luma_row_ptr + (pixWidth * pixHeight);

	for (int row = 0; row < (int)pixHeight; row += 2)
	{
		Convert8bitRGBAToNV12_SSE2(input_row_ptr, input_row_ptr + input_pitch, luma_row_ptr, luma_row_ptr + output_pitch, chroma_row_ptr, pixWidth);
		
		luma_row_ptr += output_pitch<<1;
		chroma_row_ptr += output_pitch;

		input_row_ptr += input_pitch<<1;
	}
}

// Converts a row of RGBA pixels to NV12 using floating point precision to determine veracity of integer-based implementation
void CImageConverterRGBToNV12_Debug::Convert8bitRGBAToNV12(uint8_t *src_row_ptr, uint8_t *src_row_ptr_next, uint8_t *dst_luma_row_ptr, uint8_t *dst_luma_row_ptr_next, uint8_t *dst_chroma_row_ptr, uint32_t pixWidth)
{
	// shift using base class member variable 'precision'

	// Process two columns per iteration
	for (int column = 0; column < (int)pixWidth; column += 2)
	{
		uint8_t *luma_ptr = dst_luma_row_ptr + column;
		uint8_t *luma_ptr_next = dst_luma_row_ptr_next + column;
		uint8_t *chroma_ptr = dst_chroma_row_ptr + column;
		uint8_t *src_ptr = reinterpret_cast<uint8_t *>(src_row_ptr + (column * 4));
		uint8_t *src_ptr_next = reinterpret_cast<uint8_t *>(src_row_ptr_next + (column * 4));
		
		int32_t R1 = *((src_ptr + rIndex));
		int32_t G1 = *((src_ptr + gIndex));
		int32_t B1 = *((src_ptr + bIndex));
		// Ignore the alpha
		src_ptr+=4;

		int32_t R2 = *((src_ptr + rIndex));
		int32_t G2 = *((src_ptr + gIndex));
		int32_t B2 = *((src_ptr + bIndex));
		// Ignore the alpha
		src_ptr+=4;

		int32_t R1Next = *((src_ptr_next + rIndex));
		int32_t G1Next = *((src_ptr_next + gIndex));
		int32_t B1Next = *((src_ptr_next + bIndex));
		// Ignore the alpha
		src_ptr_next+=4;

		int32_t R2Next = *((src_ptr_next + rIndex));
		int32_t G2Next = *((src_ptr_next + gIndex));
		int32_t B2Next = *((src_ptr_next + bIndex));
		// Ignore the alpha
		src_ptr_next+=4;

		double Y1, U1, V1, Y2, U2, V2, Y1Next, U1Next, V1Next, Y2Next, U2Next, V2Next, UOut, VOut;

		Y1		= (( (C_yr*R1) + (C_yg*G1) + (C_yb*B1))) + luma_offset;
		U1		= ((-(C_ur*R1) - (C_ug*G1) + (C_ub*B1))) + chroma_offset;
		V1		= (( (C_vr*R1) - (C_vg*G1) - (C_vb*B1))) + chroma_offset;

		Y2		= (( (C_yr*R2) + (C_yg*G2) + (C_yb*B2))) + luma_offset;
		U2		= ((-(C_ur*R2) - (C_ug*G2) + (C_ub*B2))) + chroma_offset;
		V2		= (( (C_vr*R2) - (C_vg*G2) - (C_vb*B2))) + chroma_offset;

		Y1Next	= (( (C_yr*R1Next) + (C_yg*G1Next) + (C_yb*B1Next))) + luma_offset;
		U1Next	= ((-(C_ur*R1Next) - (C_ug*G1Next) + (C_ub*B1Next))) + chroma_offset;
		V1Next	= (( (C_vr*R1Next) - (C_vg*G1Next) - (C_vb*B1Next))) + chroma_offset;

		Y2Next	= (( (C_yr*R2Next) + (C_yg*G2Next) + (C_yb*B2Next))) + luma_offset;
		U2Next	= ((-(C_ur*R2Next) - (C_ug*G2Next) + (C_ub*B2Next))) + chroma_offset;
		V2Next	= (( (C_vr*R2Next) - (C_vg*G2Next) - (C_vb*B2Next))) + chroma_offset;

		UOut	= (U1+U2+U1Next+U2Next)/4.0;
		VOut	= (V1+V2+V1Next+V2Next)/4.0;

		*(luma_ptr++) = (uint8_t)(Y1+0.5);
		*(luma_ptr++) = (uint8_t)(Y2+0.5);

		*(luma_ptr_next++) = (uint8_t)(Y1Next+0.5);
		*(luma_ptr_next++) = (uint8_t)(Y2Next+0.5);

		*(chroma_ptr++) = (uint8_t)(UOut+0.5);
		*(chroma_ptr++) = (uint8_t)(VOut+0.5);
	}
}

// Convert a bitmap of 8Bit pixels to NV12
// the *Index args signify the cadence of the source data - thus, if image is actually BGRA then: bIndex == 0, gIndex == 1, rIndex == 2, aIndex == 3
void CImageConverterRGBToNV12_Debug::Convert8bitRGBAToNV12(void *input_buffer, size_t input_pitch,
														void *output_buffer, size_t output_pitch,
														uint32_t pixWidth, uint32_t pixHeight,
														uint32_t rIndexArg, uint32_t gIndexArg, uint32_t bIndexArg, uint32_t aIndexArg)
{
	// hold on to these
	rIndex = rIndexArg;
	gIndex = gIndexArg;
	bIndex = bIndexArg;
	aIndex = aIndexArg;

	uint8_t *input_row_ptr = reinterpret_cast<uint8_t*>(input_buffer);

	uint8_t *luma_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);
	uint8_t *chroma_row_ptr = luma_row_ptr + (pixWidth * pixHeight);

	for (int row = 0; row < (int)pixHeight; row += 2)
	{
		Convert8bitRGBAToNV12(input_row_ptr, input_row_ptr + input_pitch, luma_row_ptr, luma_row_ptr + output_pitch, chroma_row_ptr, pixWidth);
		
		luma_row_ptr += output_pitch<<1;
		chroma_row_ptr += output_pitch;

		input_row_ptr += input_pitch<<1;
	}
}

// Convert an image of BGRA pixels to the DPX 10-bit RGB pixel format
void CImageConverterBGRA::ConvertToDPX0(void *input_buffer, size_t input_pitch,
										void *output_buffer, size_t output_pitch,
										int width, int height)
{
	uint8_t *input_row_ptr = reinterpret_cast<uint8_t *>(input_buffer);
	uint8_t *output_row_ptr = reinterpret_cast<uint8_t *>(output_buffer);

	for (int row = 0; row < height; row++)
	{
		uint8_t *bgra_ptr = input_row_ptr;
		uint32_t *dpx0_ptr = reinterpret_cast<uint32_t *>(output_row_ptr);

		for (int column = 0; column < width; column++)
		{
			// Get the next BGRA tuple
			uint16_t A = *(bgra_ptr++);
			uint16_t R = *(bgra_ptr++);
			uint16_t G = *(bgra_ptr++);
			uint16_t B = *(bgra_ptr++);

			// Shift to 16-bit precision
			//A <<= 8;
			(void)A;
			R <<= 8;
			G <<= 8;
			B <<= 8;

			// Pack the RGB values into a DPX0 pixel with 10 bits per component
			*(dpx0_ptr++) = Pack10(R, G, B);
		}

		input_row_ptr += input_pitch;
		output_row_ptr += output_pitch;
	}
}

