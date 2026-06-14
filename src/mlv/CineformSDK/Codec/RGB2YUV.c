/*! @file RGB2YUV.c

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
#include <stdint.h>
#include <stdbool.h>

#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "RGB2YUV.h"

#define BITSUSED	13
#define SSE2		1

/*
const int tweak_CG709[9] = {3,8,3,-3,0,1,1,-3,0};
const int tweak_VS709[9] = {4,3,4,2,-4,2,2,0,-3};
const int tweak_CG601[9] = {4,7,2,0,-2,3,4,-1,1};
const int tweak_VS601[9] = {3,5,3,3,-1,7,4,0,0};
*/
#define TWEAK_RGB2YUV	0
#define TWEAK_YUV2RGB	1

#if TWEAK_RGB2YUV // RGB to YUV 
#define COOEF	9
int tweak[COOEF] =         {0,0,0,0,0,0,0,0,0};
const int tweak_CG709[9] = {0,0,0,0,0,0,0,0,0};
const int tweak_VS709[9] = {0,0,0,0,0,0,0,0,0};
const int tweak_CG601[9] = {0,0,0,0,0,0,0,0,0};
const int tweak_VS601[9] = {0,0,0,0,0,0,0,0,0};
#endif

#if TWEAK_YUV2RGB // YUV to RGB
#define COOEF	8
int tweak[COOEF] =					{0,0,0,0,0,0,0,0};
const int tweakYUV2RGB_CG709[8] =	{-32,11,6,-17,-6,0,22,22};
const int tweakYUV2RGB_VS709[8] =	{-35,9,-8,-3,2,2,18,15};
const int tweakYUV2RGB_CG601[8] =	{-28,14,6,1,7,3,23,23};
const int tweakYUV2RGB_VS601[8] =	{-26,12,9,-8,1,-6,15,14};
#endif

#ifdef _WIN32
#include <windows.h>
#endif

/*
void LoadTweak()
{
	FILE *fp = fopen("c:/tweak.txt","r");
	char txt[128];
	//int j;
	int len = fread(txt,1,100,fp);
	fclose(fp);

	sscanf(txt,"%d,%d,%d,%d,%d,%d,%d,%d,%d",&tweak[0],&tweak[1],&tweak[2],&tweak[3],&tweak[4],&tweak[5],&tweak[6],&tweak[7],&tweak[8]);

	txt[len] = 0;
#ifdef _WIN32
	OutputDebugString(txt);
#endif
}
*/

void ChunkyRGB16toPlanarRGB16(unsigned short *in_rgb16, unsigned short *out_rgb16, int width)
{
	int x=0;
	int width8 = width & 0xfff8;
	unsigned short *sptr = in_rgb16;
	bool unaligned = ((uintptr_t)out_rgb16) & 0x0F;

#if SSE2 && 1
	__m128i rrrrrrrr = _mm_set1_epi16(0);
	__m128i gggggggg = _mm_set1_epi16(0);
	__m128i bbbbbbbb = _mm_set1_epi16(0);
	if (unaligned)
	{
		for(;x<width8; x+=8)
		{
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[0], 0);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[1], 0);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[2], 0);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[3], 1);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[4], 1);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[5], 1);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[6], 2);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[7], 2);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[8], 2);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[9], 3);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[10], 3);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[11], 3);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[12], 4);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[13], 4);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[14], 4);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[15], 5);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[16], 5);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[17], 5);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[18], 6);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[19], 6);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[20], 6);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[21], 7);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[22], 7);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[23], 7);

			sptr += 24;
			
			_mm_storeu_si128((__m128i *)&out_rgb16[x], rrrrrrrr);
			_mm_storeu_si128((__m128i *)&out_rgb16[x+width], gggggggg);
			_mm_storeu_si128((__m128i *)&out_rgb16[x+width*2], bbbbbbbb);
		}
	}
	else
	{
		for(;x<width8; x+=8)
		{
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[0], 0);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[1], 0);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[2], 0);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[3], 1);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[4], 1);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[5], 1);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[6], 2);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[7], 2);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[8], 2);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[9], 3);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[10], 3);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[11], 3);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[12], 4);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[13], 4);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[14], 4);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[15], 5);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[16], 5);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[17], 5);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[18], 6);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[19], 6);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[20], 6);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[21], 7);
			gggggggg = _mm_insert_epi16(gggggggg, sptr[22], 7);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[23], 7);

			sptr += 24;
			
			_mm_store_si128((__m128i *)&out_rgb16[x], rrrrrrrr);
			_mm_store_si128((__m128i *)&out_rgb16[x+width], gggggggg);
			_mm_store_si128((__m128i *)&out_rgb16[x+width*2], bbbbbbbb);
		}
	}
#endif
	for(; x<width; x++)
	{
		out_rgb16[x] = in_rgb16[x*3+0];
		out_rgb16[x+width] = in_rgb16[x*3+1];
		out_rgb16[x+width*2] = in_rgb16[x*3+2];
	}
}


void ChunkyRGB8toPlanarRGB16(unsigned char *in_rgb8, unsigned short *out_rgb16, int width)
{
	int x=0;
	int width8 = width & 0xfff8;
	unsigned char *bptr = in_rgb8;
	bool unaligned = ((uintptr_t)out_rgb16) & 0x0F;

#if (SSE2 && 1)
	__m128i rrrrrrrr = _mm_set1_epi16(0);
	__m128i gggggggg = _mm_set1_epi16(0);
	__m128i bbbbbbbb = _mm_set1_epi16(0);
	if (unaligned)
	{
		for(;x<width8; x+=8)
		{
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[0], 0);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[1], 0);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[2], 0);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[3], 1);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[4], 1);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[5], 1);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[6], 2);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[7], 2);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[8], 2);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[9], 3);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[10], 3);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[11], 3);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[12], 4);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[13], 4);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[14], 4);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[15], 5);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[16], 5);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[17], 5);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[18], 6);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[19], 6);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[20], 6);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[21], 7);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[22], 7);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[23], 7);

			bptr += 24;

			rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 8);
			gggggggg = _mm_slli_epi16(gggggggg, 8);
			bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 8);
			
			_mm_storeu_si128((__m128i *)&out_rgb16[x], rrrrrrrr);
			_mm_storeu_si128((__m128i *)&out_rgb16[x+width], gggggggg);
			_mm_storeu_si128((__m128i *)&out_rgb16[x+width*2], bbbbbbbb);
		}
	}
	else
	{
		for(;x<width8; x+=8)
		{
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[0], 0);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[1], 0);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[2], 0);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[3], 1);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[4], 1);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[5], 1);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[6], 2);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[7], 2);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[8], 2);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[9], 3);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[10], 3);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[11], 3);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[12], 4);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[13], 4);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[14], 4);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[15], 5);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[16], 5);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[17], 5);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[18], 6);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[19], 6);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[20], 6);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[21], 7);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[22], 7);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[23], 7);

			bptr += 24;

			rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 8);
			gggggggg = _mm_slli_epi16(gggggggg, 8);
			bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 8);
			
			_mm_store_si128((__m128i *)&out_rgb16[x], rrrrrrrr);
			_mm_store_si128((__m128i *)&out_rgb16[x+width], gggggggg);
			_mm_store_si128((__m128i *)&out_rgb16[x+width*2], bbbbbbbb);
		}
	}
#endif
	for(; x<width; x++)
	{
		out_rgb16[x] = in_rgb8[x*3+0]<<8;
		out_rgb16[x+width] = in_rgb8[x*3+1]<<8;
		out_rgb16[x+width*2] = in_rgb8[x*3+2]<<8;
	}
}




void ChunkyRGB8toChunkyRGB16(unsigned char *in_rgb8, unsigned short *out_rgb16, int width)
{
	int x=0;
	int width16 = (width*3) & 0xfff0;
	//unsigned char *bptr = in_rgb8;
	__m128i rgb8;
	__m128i rgb16A;
	__m128i rgb16B;
	__m128i zero_si128 = _mm_setzero_si128();
	bool unaligned = (((uintptr_t)out_rgb16) & 0x0F) || (((uintptr_t)in_rgb8) & 0x0F);

#if (SSE2 && 1)
	if (unaligned)
	{
		for(;x<width16; x+=16)
		{
			rgb8 = _mm_loadu_si128((__m128i *)&in_rgb8[x]);

			rgb16A = _mm_unpacklo_epi8(zero_si128, rgb8);
			rgb16B = _mm_unpackhi_epi8(zero_si128, rgb8);
			
			_mm_storeu_si128((__m128i *)&out_rgb16[x], rgb16A);
			_mm_storeu_si128((__m128i *)&out_rgb16[x+8], rgb16B);
		}
	}
	else
	{
		for(;x<width16; x+=16)
		{
			rgb8 = _mm_load_si128((__m128i *)&in_rgb8[x]);

			rgb16A = _mm_unpacklo_epi8(zero_si128, rgb8);
			rgb16B = _mm_unpackhi_epi8(zero_si128, rgb8);
			
			_mm_store_si128((__m128i *)&out_rgb16[x], rgb16A);
			_mm_store_si128((__m128i *)&out_rgb16[x+8], rgb16B);
		}
	}
#endif
	for(; x<width*3; x++)
	{
		out_rgb16[x] = in_rgb8[x+0]<<8;
		out_rgb16[x] = in_rgb8[x+1]<<8;
		out_rgb16[x] = in_rgb8[x+2]<<8;
	}
}


void ChunkyBGR8toPlanarRGB16(unsigned char *in_rgb8, unsigned short *out_rgb16, int width)
{
	int x=0;
	int width8 = width & 0xfff8;
	unsigned char *bptr = in_rgb8;
	bool unaligned = ((uintptr_t)out_rgb16) & 0x0F;

#if SSE2 && 1
	__m128i rrrrrrrr = _mm_set1_epi16(0);
	__m128i gggggggg = _mm_set1_epi16(0);
	__m128i bbbbbbbb = _mm_set1_epi16(0);
	if(unaligned)
	{
		for(;x<width8; x+=8)
		{
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[0], 0);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[1], 0);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[2], 0);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[3], 1);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[4], 1);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[5], 1);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[6], 2);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[7], 2);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[8], 2);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[9], 3);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[10], 3);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[11], 3);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[12], 4);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[13], 4);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[14], 4);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[15], 5);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[16], 5);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[17], 5);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[18], 6);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[19], 6);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[20], 6);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[21], 7);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[22], 7);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[23], 7);

			bptr += 24;

			rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 8);
			gggggggg = _mm_slli_epi16(gggggggg, 8);
			bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 8);
			
			_mm_storeu_si128((__m128i *)&out_rgb16[x], rrrrrrrr);
			_mm_storeu_si128((__m128i *)&out_rgb16[x+width], gggggggg);
			_mm_storeu_si128((__m128i *)&out_rgb16[x+width*2], bbbbbbbb);
		}
	}
	else
	{
		for(;x<width8; x+=8)
		{
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[0], 0);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[1], 0);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[2], 0);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[3], 1);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[4], 1);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[5], 1);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[6], 2);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[7], 2);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[8], 2);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[9], 3);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[10], 3);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[11], 3);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[12], 4);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[13], 4);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[14], 4);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[15], 5);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[16], 5);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[17], 5);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[18], 6);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[19], 6);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[20], 6);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[21], 7);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[22], 7);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[23], 7);

			bptr += 24;

			rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 8);
			gggggggg = _mm_slli_epi16(gggggggg, 8);
			bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 8);
			
			_mm_store_si128((__m128i *)&out_rgb16[x], rrrrrrrr);
			_mm_store_si128((__m128i *)&out_rgb16[x+width], gggggggg);
			_mm_store_si128((__m128i *)&out_rgb16[x+width*2], bbbbbbbb);
		}
	}
#endif
	for(; x<width; x++)
	{
		out_rgb16[x] = in_rgb8[x*3+2]<<8;
		out_rgb16[x+width] = in_rgb8[x*3+1]<<8;
		out_rgb16[x+width*2] = in_rgb8[x*3+0]<<8;
	}
}



void ChunkyBGRA8toPlanarRGB16(unsigned char *in_rgb8, unsigned short *out_rgb16, int width)
{
	int x=0;
	int width8 = width & 0xfff8;
	unsigned char *bptr = in_rgb8;
	bool unaligned = ((uintptr_t)out_rgb16) & 0x0F;

#if (SSE2 && 1)
	__m128i rrrrrrrr = _mm_set1_epi16(0);
	__m128i gggggggg = _mm_set1_epi16(0);
	__m128i bbbbbbbb = _mm_set1_epi16(0);
	if (unaligned)
	{
		for(;x<width8; x+=8)
		{
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[0], 0); //b
			gggggggg = _mm_insert_epi16(gggggggg, bptr[1], 0); //g
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[2], 0); //r (skip a)
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[4], 1); //b
			gggggggg = _mm_insert_epi16(gggggggg, bptr[5], 1);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[6], 1);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[8], 2);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[9], 2);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[10], 2);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[12], 3);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[13], 3);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[14], 3);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[16], 4);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[17], 4);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[18], 4);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[20], 5);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[21], 5);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[22], 5);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[24], 6);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[25], 6);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[26], 6);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[28], 7);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[29], 7);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[30], 7);

			bptr += 32;

			rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 8);
			gggggggg = _mm_slli_epi16(gggggggg, 8);
			bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 8);
			
			_mm_storeu_si128((__m128i *)&out_rgb16[x], rrrrrrrr);
			_mm_storeu_si128((__m128i *)&out_rgb16[x+width], gggggggg);
			_mm_storeu_si128((__m128i *)&out_rgb16[x+width*2], bbbbbbbb);
		}
	}
	else
	{
		for(;x<width8; x+=8)
		{
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[0], 0);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[1], 0);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[2], 0);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[4], 1);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[5], 1);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[6], 1);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[8], 2);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[9], 2);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[10], 2);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[12], 3);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[13], 3);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[14], 3);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[16], 4);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[17], 4);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[18], 4);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[20], 5);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[21], 5);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[22], 5);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[24], 6);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[25], 6);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[26], 6);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[28], 7);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[29], 7);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[30], 7);

			bptr += 32;

			rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 8);
			gggggggg = _mm_slli_epi16(gggggggg, 8);
			bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 8);
			
			_mm_store_si128((__m128i *)&out_rgb16[x], rrrrrrrr);
			_mm_store_si128((__m128i *)&out_rgb16[x+width], gggggggg);
			_mm_store_si128((__m128i *)&out_rgb16[x+width*2], bbbbbbbb);
		}
	}
#endif
	for(; x<width; x++)
	{
		out_rgb16[x] = in_rgb8[x*3+2]<<8;
		out_rgb16[x+width] = in_rgb8[x*3+1]<<8;
		out_rgb16[x+width*2] = in_rgb8[x*3+0]<<8;
	}
}



void ChunkyARGB8toPlanarRGB16(unsigned char *in_rgb8, unsigned short *out_rgb16, int width)
{
	int x=0;
	int width8 = width & 0xfff8;
	unsigned char *bptr = in_rgb8;
	bool unaligned = ((uintptr_t)out_rgb16) & 0x0F;

#if SSE2 && 1
	__m128i rrrrrrrr = _mm_set1_epi16(0);
	__m128i gggggggg = _mm_set1_epi16(0);
	__m128i bbbbbbbb = _mm_set1_epi16(0);
	if(unaligned)
	{
		for(;x<width8; x+=8)
		{
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[1], 0); //(skip a) then r
			gggggggg = _mm_insert_epi16(gggggggg, bptr[2], 0);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[3], 0);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[5], 1);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[6], 1);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[7], 1);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[9], 2);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[10], 2);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[11], 2);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[13], 3);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[14], 3);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[15], 3);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[17], 4);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[18], 4);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[19], 4);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[21], 5);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[22], 5);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[23], 5);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[25], 6);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[26], 6);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[27], 6);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[29], 7);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[30], 7);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[31], 7);

			bptr += 32;

			rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 8);
			gggggggg = _mm_slli_epi16(gggggggg, 8);
			bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 8);

//#ifdef _WIN32 // is QT32 mode different on the PC?
			_mm_storeu_si128((__m128i *)&out_rgb16[x], bbbbbbbb);
			_mm_storeu_si128((__m128i *)&out_rgb16[x+width], gggggggg);
			_mm_storeu_si128((__m128i *)&out_rgb16[x+width*2], rrrrrrrr);
//#else
//			_mm_storeu_si128((__m128i *)&out_rgb16[x], rrrrrrrr);
//			_mm_storeu_si128((__m128i *)&out_rgb16[x+width], gggggggg);
//			_mm_storeu_si128((__m128i *)&out_rgb16[x+width*2], bbbbbbbb);
//#endif		
		}
	}
	else
	{
		for(;x<width8; x+=8)
		{
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[1], 0);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[2], 0);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[3], 0);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[5], 1);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[6], 1);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[7], 1);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[9], 2);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[10], 2);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[11], 2);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[13], 3);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[14], 3);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[15], 3);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[17], 4);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[18], 4);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[19], 4);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[21], 5);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[22], 5);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[23], 5);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[25], 6);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[26], 6);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[27], 6);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, bptr[29], 7);
			gggggggg = _mm_insert_epi16(gggggggg, bptr[30], 7);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, bptr[31], 7);

			bptr += 32;

			rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 8);
			gggggggg = _mm_slli_epi16(gggggggg, 8);
			bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 8);

//#ifdef _WIN32 // is QT32 mode different on the PC?
			_mm_store_si128((__m128i *)&out_rgb16[x], bbbbbbbb);
			_mm_store_si128((__m128i *)&out_rgb16[x+width], gggggggg);
			_mm_store_si128((__m128i *)&out_rgb16[x+width*2], rrrrrrrr);
//#else			
//			_mm_store_si128((__m128i *)&out_rgb16[x], rrrrrrrr);
//			_mm_store_si128((__m128i *)&out_rgb16[x+width], gggggggg);
//			_mm_store_si128((__m128i *)&out_rgb16[x+width*2], bbbbbbbb);
//#endif
		}
	}
#endif
	for(; x<width; x++)
	{
		out_rgb16[x] = in_rgb8[x*3+1]<<8; //r
		out_rgb16[x+width] = in_rgb8[x*3+2]<<8; //g 
		out_rgb16[x+width*2] = in_rgb8[x*3+3]<<8; //b
	}
}


void PlanarRGB16toChunkyRGB16(unsigned short *in_rgb16, unsigned short *out_rgb16, int width)
{
	int x=0;
	int width8 = width & 0xfff8;
	unsigned short *sptr = out_rgb16;
	bool unaligned = ((uintptr_t)in_rgb16) & 0x0F;

#if (SSE2 && 1)
	__m128i rrrrrrrr;
	__m128i gggggggg;
	__m128i bbbbbbbb;
	if(unaligned)
	{
		for(;x<width8; x+=8)
		{
			rrrrrrrr = _mm_loadu_si128((__m128i *)&in_rgb16[x]);
			gggggggg = _mm_loadu_si128((__m128i *)&in_rgb16[x+width]);
			bbbbbbbb = _mm_loadu_si128((__m128i *)&in_rgb16[x+width*2]);

			sptr[0] = _mm_extract_epi16(rrrrrrrr, 0);
			sptr[1] = _mm_extract_epi16(gggggggg, 0);
			sptr[2] = _mm_extract_epi16(bbbbbbbb, 0);
			sptr[3] = _mm_extract_epi16(rrrrrrrr, 1);
			sptr[4] = _mm_extract_epi16(gggggggg, 1);
			sptr[5] = _mm_extract_epi16(bbbbbbbb, 1);
			sptr[6] = _mm_extract_epi16(rrrrrrrr, 2);
			sptr[7] = _mm_extract_epi16(gggggggg, 2);
			sptr[8] = _mm_extract_epi16(bbbbbbbb, 2);
			sptr[9] = _mm_extract_epi16(rrrrrrrr, 3);
			sptr[10] = _mm_extract_epi16(gggggggg, 3);
			sptr[11] = _mm_extract_epi16(bbbbbbbb, 3);
			sptr[12] = _mm_extract_epi16(rrrrrrrr, 4);
			sptr[13] = _mm_extract_epi16(gggggggg, 4);
			sptr[14] = _mm_extract_epi16(bbbbbbbb, 4);
			sptr[15] = _mm_extract_epi16(rrrrrrrr, 5);
			sptr[16] = _mm_extract_epi16(gggggggg, 5);
			sptr[17] = _mm_extract_epi16(bbbbbbbb, 5);
			sptr[18] = _mm_extract_epi16(rrrrrrrr, 6);
			sptr[19] = _mm_extract_epi16(gggggggg, 6);
			sptr[20] = _mm_extract_epi16(bbbbbbbb, 6);
			sptr[21] = _mm_extract_epi16(rrrrrrrr, 7);
			sptr[22] = _mm_extract_epi16(gggggggg, 7);
			sptr[23] = _mm_extract_epi16(bbbbbbbb, 7);

			sptr += 24;
		}
	}
	else
	{
		for(;x<width8; x+=8)
		{
			rrrrrrrr = _mm_load_si128((__m128i *)&in_rgb16[x]);
			gggggggg = _mm_load_si128((__m128i *)&in_rgb16[x+width]);
			bbbbbbbb = _mm_load_si128((__m128i *)&in_rgb16[x+width*2]);

			sptr[0] = _mm_extract_epi16(rrrrrrrr, 0);
			sptr[1] = _mm_extract_epi16(gggggggg, 0);
			sptr[2] = _mm_extract_epi16(bbbbbbbb, 0);
			sptr[3] = _mm_extract_epi16(rrrrrrrr, 1);
			sptr[4] = _mm_extract_epi16(gggggggg, 1);
			sptr[5] = _mm_extract_epi16(bbbbbbbb, 1);
			sptr[6] = _mm_extract_epi16(rrrrrrrr, 2);
			sptr[7] = _mm_extract_epi16(gggggggg, 2);
			sptr[8] = _mm_extract_epi16(bbbbbbbb, 2);
			sptr[9] = _mm_extract_epi16(rrrrrrrr, 3);
			sptr[10] = _mm_extract_epi16(gggggggg, 3);
			sptr[11] = _mm_extract_epi16(bbbbbbbb, 3);
			sptr[12] = _mm_extract_epi16(rrrrrrrr, 4);
			sptr[13] = _mm_extract_epi16(gggggggg, 4);
			sptr[14] = _mm_extract_epi16(bbbbbbbb, 4);
			sptr[15] = _mm_extract_epi16(rrrrrrrr, 5);
			sptr[16] = _mm_extract_epi16(gggggggg, 5);
			sptr[17] = _mm_extract_epi16(bbbbbbbb, 5);
			sptr[18] = _mm_extract_epi16(rrrrrrrr, 6);
			sptr[19] = _mm_extract_epi16(gggggggg, 6);
			sptr[20] = _mm_extract_epi16(bbbbbbbb, 6);
			sptr[21] = _mm_extract_epi16(rrrrrrrr, 7);
			sptr[22] = _mm_extract_epi16(gggggggg, 7);
			sptr[23] = _mm_extract_epi16(bbbbbbbb, 7);

			sptr += 24;
		}
	}
#endif
	for(; x<width; x++)
	{
		out_rgb16[x*3+0] = in_rgb16[x];
		out_rgb16[x*3+1] = in_rgb16[x+width];
		out_rgb16[x*3+2] = in_rgb16[x+width*2];
	}
}


void PlanarYUV16toChannelYUYV16(unsigned short *in_YUV, unsigned short *planar_output[], int width, int colorspace, int shift)
{
	int x=0;
	int width16 = width & 0xfff0;
	unsigned short *Yptr = planar_output[0];
	unsigned short *Uptr = planar_output[1];
	unsigned short *Vptr = planar_output[2];

#if SSE2 && 1
	__m128i yyyyyyyy,yyyyyyy2;
	__m128i uuuuuuuu,uuuuuuu2;
	__m128i vvvvvvvv,vvvvvvv2;
	const __m128i mask_epi32 = _mm_set1_epi32(0x0000ffff);

	if(0 && colorspace & COLOR_SPACE_422_TO_444) // TODO : Fix, seems to produce green-tinted frames
	{
		int lastU0, lastV0;

		// 4:4:4 to 4:2:2 // U = (U1+2.U2+U3)/4 (correct centre weighting)
		for(;x<width16;x+=16)
		{
			__m128i y1_output_epi16;
			__m128i y2_output_epi16;
			__m128i u1_output_epi16;
			__m128i u2_output_epi16;
			__m128i v1_output_epi16;
			__m128i v2_output_epi16;
			__m128i double1_epi16;
			__m128i double2_epi16;
			__m128i left1_epi16;
			__m128i left2_epi16;
			__m128i right1_epi16;
			__m128i right2_epi16;
			
			y1_output_epi16 = _mm_loadu_si128((__m128i *)&in_YUV[x]);
			u1_output_epi16 = _mm_loadu_si128((__m128i *)&in_YUV[x+width]);
			v1_output_epi16 = _mm_loadu_si128((__m128i *)&in_YUV[x+width*2]);
			y2_output_epi16 = _mm_loadu_si128((__m128i *)&in_YUV[x+8]);
			u2_output_epi16 = _mm_loadu_si128((__m128i *)&in_YUV[x+8+width]);
			v2_output_epi16 = _mm_loadu_si128((__m128i *)&in_YUV[x+8+width*2]);

			u1_output_epi16 = _mm_srli_epi16(u1_output_epi16,2);
			v1_output_epi16 = _mm_srli_epi16(v1_output_epi16,2);	
			u2_output_epi16 = _mm_srli_epi16(u2_output_epi16,2);
			v2_output_epi16 = _mm_srli_epi16(v2_output_epi16,2);		

			if(x == 0)
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

			u1_output_epi16 = _mm_packs_epi32 (u1_output_epi16, u2_output_epi16);
			v1_output_epi16 = _mm_packs_epi32 (v1_output_epi16, v2_output_epi16);
		
			y1_output_epi16 = _mm_srli_epi16(y1_output_epi16, shift);
			y2_output_epi16 = _mm_srli_epi16(y2_output_epi16, shift);
			u1_output_epi16 = _mm_srli_epi16(u1_output_epi16, shift);
			v1_output_epi16 = _mm_srli_epi16(v1_output_epi16, shift);
		
			_mm_storeu_si128((__m128i *)&Yptr[x], y1_output_epi16);
			_mm_storeu_si128((__m128i *)&Yptr[x+8], y2_output_epi16);
			_mm_storeu_si128((__m128i *)&Uptr[x>>1], u1_output_epi16);
			_mm_storeu_si128((__m128i *)&Vptr[x>>1], v1_output_epi16);
		}
	}
	else
	{
		for(;x<width16; x+=16)
		{
			yyyyyyyy = _mm_loadu_si128((__m128i *)&in_YUV[x]);
			uuuuuuuu = _mm_loadu_si128((__m128i *)&in_YUV[x+width]);
			vvvvvvvv = _mm_loadu_si128((__m128i *)&in_YUV[x+width*2]);
			yyyyyyy2 = _mm_loadu_si128((__m128i *)&in_YUV[x+8]);
			uuuuuuu2 = _mm_loadu_si128((__m128i *)&in_YUV[x+8+width]);
			vvvvvvv2 = _mm_loadu_si128((__m128i *)&in_YUV[x+8+width*2]);
			
			
			uuuuuuuu = _mm_srli_epi16(uuuuuuuu,1);
			uuuuuuu2 = _mm_srli_epi16(uuuuuuu2,1);
			vvvvvvvv = _mm_srli_epi16(vvvvvvvv,1);
			vvvvvvv2 = _mm_srli_epi16(vvvvvvv2,1);

			uuuuuuuu = _mm_and_si128(uuuuuuuu, mask_epi32);		
			vvvvvvvv = _mm_and_si128(vvvvvvvv, mask_epi32);
			uuuuuuu2 = _mm_and_si128(uuuuuuu2, mask_epi32);		
			vvvvvvv2 = _mm_and_si128(vvvvvvv2, mask_epi32);
			uuuuuuuu = _mm_packs_epi32(uuuuuuuu, uuuuuuu2);
			vvvvvvvv = _mm_packs_epi32(vvvvvvvv, vvvvvvv2);
			
			uuuuuuuu = _mm_slli_epi16(uuuuuuuu, 1);
			vvvvvvvv = _mm_slli_epi16(vvvvvvvv, 1);

			yyyyyyyy = _mm_srli_epi16(yyyyyyyy, shift);
			yyyyyyy2 = _mm_srli_epi16(yyyyyyy2, shift);
			uuuuuuuu = _mm_srli_epi16(uuuuuuuu, shift);
			vvvvvvvv = _mm_srli_epi16(vvvvvvvv, shift);

			_mm_storeu_si128((__m128i *)&Yptr[x],    yyyyyyyy);
			_mm_storeu_si128((__m128i *)&Yptr[x+8],  yyyyyyy2);
			_mm_storeu_si128((__m128i *)&Uptr[x>>1], uuuuuuuu);
			_mm_storeu_si128((__m128i *)&Vptr[x>>1], vvvvvvvv);
		}
	}
#endif
	for(; x<width; x+=2)
	{
		Yptr[x+0] = in_YUV[x]>>shift;
		Uptr[x>>1] = (in_YUV[x+width] + in_YUV[x+width+1])>>(shift+1);
		Yptr[x+1] = in_YUV[x+1]>>shift;
		Vptr[x>>1] = (in_YUV[x+width*2] + in_YUV[x+width*2+1])>>(shift+1);
	}
}


void PlanarYUV16toChunkyYUYV16(unsigned short *in_YUV, unsigned short *out_YUYV, int width, int colorspace)
{
	int x=0;
	int width8 = width & 0xfff8;
	bool unaligned = (((uintptr_t)in_YUV) & 0x0F) || (((uintptr_t)out_YUYV) & 0x0F);

#if SSE2 && 1
	__m128i yyyyyyyy,yuyvyuyv,yuyvyuy2;
	__m128i uuuuuuuu,uuuuuuu2;
	__m128i vvvvvvvv,vvvvvvv2;
	__m128i uvuvuvuv;
	const __m128i mask_epi32 = _mm_set1_epi32(0x0000ffff);

	if(0 && colorspace & COLOR_SPACE_422_TO_444) // TODO : Fix, seems to produce green-tinted frames
	{
		int width16 = width & 0xfff0;
		int lastU0, lastV0;

		// 4:4:4 to 4:2:2 // U = (U1+2.U2+U3)/4 (correct centre weighting)
		for(;x<width16;x+=16)
		{
			__m128i y1_output_epi16;
			__m128i y2_output_epi16;
			__m128i u1_output_epi16;
			__m128i u2_output_epi16;
			__m128i v1_output_epi16;
			__m128i v2_output_epi16;
			__m128i double1_epi16;
			__m128i double2_epi16;
			__m128i left1_epi16;
			__m128i left2_epi16;
			__m128i right1_epi16;
			__m128i right2_epi16;
			
			y1_output_epi16 = _mm_loadu_si128((__m128i *)&in_YUV[x]);
			u1_output_epi16 = _mm_loadu_si128((__m128i *)&in_YUV[x+width]);
			v1_output_epi16 = _mm_loadu_si128((__m128i *)&in_YUV[x+width*2]);
			y2_output_epi16 = _mm_loadu_si128((__m128i *)&in_YUV[x+8]);
			u2_output_epi16 = _mm_loadu_si128((__m128i *)&in_YUV[x+8+width]);
			v2_output_epi16 = _mm_loadu_si128((__m128i *)&in_YUV[x+8+width*2]);

			u1_output_epi16 = _mm_srli_epi16(u1_output_epi16,2);
			v1_output_epi16 = _mm_srli_epi16(v1_output_epi16,2);	
			u2_output_epi16 = _mm_srli_epi16(u2_output_epi16,2);
			v2_output_epi16 = _mm_srli_epi16(v2_output_epi16,2);		

			if(x == 0)
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

			u1_output_epi16 = _mm_packs_epi32 (u1_output_epi16, u2_output_epi16);
			v1_output_epi16 = _mm_packs_epi32 (v1_output_epi16, v2_output_epi16);

			uvuvuvuv = _mm_unpacklo_epi16 (u1_output_epi16, v1_output_epi16);		
			uvuvuvuv = _mm_slli_epi16(uvuvuvuv,2);
			yuyvyuyv = _mm_unpacklo_epi16(y1_output_epi16, uvuvuvuv);
			yuyvyuy2 = _mm_unpackhi_epi16(y1_output_epi16, uvuvuvuv);

			_mm_storeu_si128((__m128i *)&out_YUYV[x*2], yuyvyuyv);
			_mm_storeu_si128((__m128i *)&out_YUYV[x*2+8], yuyvyuy2);

			
			uvuvuvuv = _mm_unpackhi_epi16 (u1_output_epi16, v1_output_epi16);		
			uvuvuvuv = _mm_slli_epi16(uvuvuvuv,2);
			yuyvyuyv = _mm_unpacklo_epi16(y2_output_epi16, uvuvuvuv);
			yuyvyuy2 = _mm_unpackhi_epi16(y2_output_epi16, uvuvuvuv);

			_mm_storeu_si128((__m128i *)&out_YUYV[x*2+16], yuyvyuyv);
			_mm_storeu_si128((__m128i *)&out_YUYV[x*2+24], yuyvyuy2);

		}
	}
	else
	{
		if(unaligned)
		{
			for(;x<width8; x+=8)
			{
				yyyyyyyy = _mm_loadu_si128((__m128i *)&in_YUV[x]);
				uuuuuuuu = _mm_loadu_si128((__m128i *)&in_YUV[x+width]);
				vvvvvvvv = _mm_loadu_si128((__m128i *)&in_YUV[x+width*2]);

				uuuuuuuu = _mm_srli_epi16(uuuuuuuu,2);
				vvvvvvvv = _mm_srli_epi16(vvvvvvvv,2);		
				uuuuuuu2 = _mm_srli_si128(uuuuuuuu, 2);
				vvvvvvv2 = _mm_srli_si128(vvvvvvvv, 2);		
				uuuuuuuu = _mm_adds_epi16(uuuuuuuu,uuuuuuu2);
				vvvvvvvv = _mm_adds_epi16(vvvvvvvv,vvvvvvv2);		
				uuuuuuuu = _mm_and_si128(uuuuuuuu, mask_epi32);		
				vvvvvvvv = _mm_and_si128(vvvvvvvv, mask_epi32);
				uuuuuuuu = _mm_packs_epi32(uuuuuuuu, uuuuuuuu);
				vvvvvvvv = _mm_packs_epi32(vvvvvvvv, vvvvvvvv);
				uvuvuvuv = _mm_unpackhi_epi16 (uuuuuuuu, vvvvvvvv);		
				uvuvuvuv = _mm_slli_epi16(uvuvuvuv,1);
				yuyvyuyv = _mm_unpacklo_epi16(yyyyyyyy, uvuvuvuv);
				yuyvyuy2 = _mm_unpackhi_epi16(yyyyyyyy, uvuvuvuv);

				_mm_storeu_si128((__m128i *)&out_YUYV[x*2], yuyvyuyv);
				_mm_storeu_si128((__m128i *)&out_YUYV[x*2+8], yuyvyuy2);
			}
		}
		else
		{
			for(;x<width8; x+=8)
			{
				yyyyyyyy = _mm_load_si128((__m128i *)&in_YUV[x]);
				uuuuuuuu = _mm_load_si128((__m128i *)&in_YUV[x+width]);
				vvvvvvvv = _mm_load_si128((__m128i *)&in_YUV[x+width*2]);

				uuuuuuuu = _mm_srli_epi16(uuuuuuuu,2);
				vvvvvvvv = _mm_srli_epi16(vvvvvvvv,2);		
				uuuuuuu2 = _mm_srli_si128(uuuuuuuu, 2);
				vvvvvvv2 = _mm_srli_si128(vvvvvvvv, 2);		
				uuuuuuuu = _mm_adds_epi16(uuuuuuuu,uuuuuuu2);
				vvvvvvvv = _mm_adds_epi16(vvvvvvvv,vvvvvvv2);		
				uuuuuuuu = _mm_and_si128(uuuuuuuu, mask_epi32);		
				vvvvvvvv = _mm_and_si128(vvvvvvvv, mask_epi32);
				uuuuuuuu = _mm_packs_epi32(uuuuuuuu, uuuuuuuu);
				vvvvvvvv = _mm_packs_epi32(vvvvvvvv, vvvvvvvv);
				uvuvuvuv = _mm_unpackhi_epi16 (uuuuuuuu, vvvvvvvv);		
				uvuvuvuv = _mm_slli_epi16(uvuvuvuv,1);
				yuyvyuyv = _mm_unpacklo_epi16(yyyyyyyy, uvuvuvuv);
				yuyvyuy2 = _mm_unpackhi_epi16(yyyyyyyy, uvuvuvuv);

				_mm_store_si128((__m128i *)&out_YUYV[x*2], yuyvyuyv);
				_mm_store_si128((__m128i *)&out_YUYV[x*2+8], yuyvyuy2);
			}
		}
	}
#endif
	for(; x<width; x+=2)
	{
		out_YUYV[x*2+0] = in_YUV[x];
		out_YUYV[x*2+1] = (in_YUV[x+width] + in_YUV[x+width+1])>>1;
		out_YUYV[x*2+2] = in_YUV[x+1];
		out_YUYV[x*2+3] = (in_YUV[x+width*2] + in_YUV[x+width*2+1])>>1;
	}
}


void PlanarYUV16toChunkyYUYV8(unsigned short *in_YUV, unsigned char *out_YUYV, int width, int colorspace)
{
	int x=0;

#if SSE2 && 0
	assert(0); //TODO
#endif
	for(; x<width; x+=2)
	{
		out_YUYV[x*2+0] = in_YUV[x]>>8;
		out_YUYV[x*2+1] = (in_YUV[x+width] + in_YUV[x+width+1])>>9;
		out_YUYV[x*2+2] = in_YUV[x+1]>>8;
		out_YUYV[x*2+3] = (in_YUV[x+width*2] + in_YUV[x+width*2+1])>>9;
	}
}




void ChunkyYUYV16toPlanarYUV16(unsigned short *in_YUYV, unsigned short *out_YUV, int width, int colorspace)
{
	int x=0;
	int width8 = width & 0xfff8;
	bool unaligned = (((uintptr_t)in_YUYV) & 0x0F) || (((uintptr_t)out_YUV) & 0x0F);

#if SSE2 && 1
	__m128i yyyyyyyy = _mm_set1_epi16(0);
	__m128i uuuuuuuu = _mm_set1_epi16(0);
	__m128i vvvvvvvv = _mm_set1_epi16(0);
	__m128i uvuvuvuv, yuyvyuyv, yuyvyuy2,tttttttt,ttttttt2;
	__m128i mask_epi32 = _mm_set1_epi32(0x0000ffff);
	
	if(colorspace & COLOR_SPACE_422_TO_444)
	{
		int lastU0, lastV0;

		for(;x<width8; x+=8) // TODO 4:2:2 to 4:4:4 blend
		{
			yuyvyuyv = _mm_loadu_si128((__m128i *)&in_YUYV[x*2]);
			yuyvyuy2 = _mm_loadu_si128((__m128i *)&in_YUYV[x*2+8]);

			yuyvyuyv = _mm_srli_epi16(yuyvyuyv,1);
			yuyvyuy2 = _mm_srli_epi16(yuyvyuy2,1);				
			tttttttt = _mm_and_si128(yuyvyuyv, mask_epi32);		
			ttttttt2 = _mm_and_si128(yuyvyuy2, mask_epi32);		
			yyyyyyyy = _mm_packs_epi32(tttttttt, ttttttt2);
			yyyyyyyy = _mm_slli_epi16(yyyyyyyy,1);

			yuyvyuyv = _mm_srli_si128(yuyvyuyv, 2);
			yuyvyuy2 = _mm_srli_si128(yuyvyuy2, 2);
			tttttttt = _mm_and_si128(yuyvyuyv, mask_epi32);		
			ttttttt2 = _mm_and_si128(yuyvyuy2, mask_epi32);	
			uvuvuvuv = _mm_packs_epi32(tttttttt, ttttttt2);
			
			tttttttt = _mm_and_si128(uvuvuvuv, mask_epi32);	
			ttttttt2 = _mm_slli_si128(tttttttt, 2);	
			uuuuuuuu = _mm_adds_epi16(tttttttt, ttttttt2);
		//	uuuuuuuu = _mm_slli_epi16(uuuuuuuu,1);

			uvuvuvuv = _mm_srli_si128(uvuvuvuv, 2);
			tttttttt = _mm_and_si128(uvuvuvuv, mask_epi32);		
			ttttttt2 = _mm_slli_si128(tttttttt, 2);	
			vvvvvvvv = _mm_adds_epi16(tttttttt, ttttttt2);
		//	vvvvvvvv = _mm_slli_epi16(vvvvvvvv,1);

			
			if(x == 0)
			{
				lastU0 = _mm_extract_epi16(uuuuuuuu, 0);
				lastV0 = _mm_extract_epi16(vvvvvvvv, 0);
			}

			tttttttt = _mm_slli_si128(uuuuuuuu, 2);
			tttttttt = _mm_insert_epi16(tttttttt, lastU0, 0);
			lastU0 = _mm_extract_epi16(uuuuuuuu, 7);
			uuuuuuuu = _mm_adds_epu16(uuuuuuuu, tttttttt);

			ttttttt2 = _mm_slli_si128(vvvvvvvv, 2);
			ttttttt2 = _mm_insert_epi16(ttttttt2, lastV0, 0);
			lastV0 = _mm_extract_epi16(vvvvvvvv, 7);
			vvvvvvvv = _mm_adds_epu16(vvvvvvvv, ttttttt2);
			
			_mm_storeu_si128((__m128i *)&out_YUV[x], yyyyyyyy);
			_mm_storeu_si128((__m128i *)&out_YUV[x+width], uuuuuuuu);
			_mm_storeu_si128((__m128i *)&out_YUV[x+width*2], vvvvvvvv);
		}
	}
	else
	{
		if(unaligned)
		{
			for(;x<width8; x+=8)
			{
				yuyvyuyv = _mm_loadu_si128((__m128i *)&in_YUYV[x*2]);
				yuyvyuy2 = _mm_loadu_si128((__m128i *)&in_YUYV[x*2+8]);

				yuyvyuyv = _mm_srli_epi16(yuyvyuyv,1);
				yuyvyuy2 = _mm_srli_epi16(yuyvyuy2,1);				
				tttttttt = _mm_and_si128(yuyvyuyv, mask_epi32);		
				ttttttt2 = _mm_and_si128(yuyvyuy2, mask_epi32);		
				yyyyyyyy = _mm_packs_epi32(tttttttt, ttttttt2);
				yyyyyyyy = _mm_slli_epi16(yyyyyyyy,1);

				yuyvyuyv = _mm_srli_si128(yuyvyuyv, 2);
				yuyvyuy2 = _mm_srli_si128(yuyvyuy2, 2);
				tttttttt = _mm_and_si128(yuyvyuyv, mask_epi32);		
				ttttttt2 = _mm_and_si128(yuyvyuy2, mask_epi32);	
				uvuvuvuv = _mm_packs_epi32(tttttttt, ttttttt2);
				
				tttttttt = _mm_and_si128(uvuvuvuv, mask_epi32);	
				ttttttt2 = _mm_slli_si128(tttttttt, 2);	
				uuuuuuuu = _mm_adds_epi16(tttttttt, ttttttt2);
				uuuuuuuu = _mm_slli_epi16(uuuuuuuu,1);

				uvuvuvuv = _mm_srli_si128(uvuvuvuv, 2);
				tttttttt = _mm_and_si128(uvuvuvuv, mask_epi32);		
				ttttttt2 = _mm_slli_si128(tttttttt, 2);	
				vvvvvvvv = _mm_adds_epi16(tttttttt, ttttttt2);
				vvvvvvvv = _mm_slli_epi16(vvvvvvvv,1);
				
				_mm_storeu_si128((__m128i *)&out_YUV[x], yyyyyyyy);
				_mm_storeu_si128((__m128i *)&out_YUV[x+width], uuuuuuuu);
				_mm_storeu_si128((__m128i *)&out_YUV[x+width*2], vvvvvvvv);
			}
		}
		else
		{
			for(;x<width8; x+=8)
			{
				yuyvyuyv = _mm_load_si128((__m128i *)&in_YUYV[x*2]);
				yuyvyuy2 = _mm_load_si128((__m128i *)&in_YUYV[x*2+8]);

				yuyvyuyv = _mm_srli_epi16(yuyvyuyv,1);
				yuyvyuy2 = _mm_srli_epi16(yuyvyuy2,1);				
				tttttttt = _mm_and_si128(yuyvyuyv, mask_epi32);		
				ttttttt2 = _mm_and_si128(yuyvyuy2, mask_epi32);		
				yyyyyyyy = _mm_packs_epi32(tttttttt, ttttttt2);
				yyyyyyyy = _mm_slli_epi16(yyyyyyyy,1);

				yuyvyuyv = _mm_srli_si128(yuyvyuyv, 2);
				yuyvyuy2 = _mm_srli_si128(yuyvyuy2, 2);
				tttttttt = _mm_and_si128(yuyvyuyv, mask_epi32);		
				ttttttt2 = _mm_and_si128(yuyvyuy2, mask_epi32);	
				uvuvuvuv = _mm_packs_epi32(tttttttt, ttttttt2);
				
				tttttttt = _mm_and_si128(uvuvuvuv, mask_epi32);	
				ttttttt2 = _mm_slli_si128(tttttttt, 2);	
				uuuuuuuu = _mm_adds_epi16(tttttttt, ttttttt2);
				uuuuuuuu = _mm_slli_epi16(uuuuuuuu,1);

				uvuvuvuv = _mm_srli_si128(uvuvuvuv, 2);
				tttttttt = _mm_and_si128(uvuvuvuv, mask_epi32);		
				ttttttt2 = _mm_slli_si128(tttttttt, 2);	
				vvvvvvvv = _mm_adds_epi16(tttttttt, ttttttt2);
				vvvvvvvv = _mm_slli_epi16(vvvvvvvv,1);
				
				_mm_store_si128((__m128i *)&out_YUV[x], yyyyyyyy);
				_mm_store_si128((__m128i *)&out_YUV[x+width], uuuuuuuu);
				_mm_store_si128((__m128i *)&out_YUV[x+width*2], vvvvvvvv);
			}
		}
	}
#endif
	for(; x<width; x+=2)
	{
		out_YUV[x] = in_YUYV[x*2+0];
		out_YUV[x+width] = in_YUYV[x*2+1];
		out_YUV[x+width*2] = in_YUYV[x*2+3];
		out_YUV[x+1] = in_YUYV[x*2+2];
		out_YUV[x+width+1] = in_YUYV[x*2+1];
		out_YUV[x+width*2+1] = in_YUYV[x*2+3];
	}
}

void ChunkyYUYV8toPlanarYUV16(uint8_t *in_YUYV, uint8_t *out_YUV, int width, int colorspace)
{
	int x=0;
	//int width8 = width & 0xfff8;
	//bool unaligned = (((uintptr_t)in_YUYV) & 0x0F) || (((uintptr_t)out_YUV) & 0x0F);

#if (SSE2 && 0)
	assert(0); //TODO
#endif
	for(; x<width; x+=2)
	{
		out_YUV[x] = in_YUYV[x*2+0]<<8;
		out_YUV[x+width] = in_YUYV[x*2+1]<<8;
		out_YUV[x+width*2] = in_YUYV[x*2+3]<<8;
		out_YUV[x+1] = in_YUYV[x*2+2]<<8;
		out_YUV[x+width+1] = in_YUYV[x*2+1]<<8;
		out_YUV[x+width*2+1] = in_YUYV[x*2+3]<<8;
	}
}

void UpShift16(unsigned short *in_rgb16, int pixels, int upshift, int saturate)
{
	bool unaligned = ((uintptr_t)in_rgb16) & 0x0F;
	int x=0,width8 = pixels & 0xfff8;
	unsigned short *out_rgb16 = (unsigned short *)in_rgb16;
	short *signed_in_rgb16 = (short *)in_rgb16;
	__m128i *ptr128 = (__m128i *)in_rgb16;

	if(!saturate)
	{
		if(unaligned)
		{
			for(;x<width8; x+=8)
			{		
				__m128i tttttttt = _mm_loadu_si128(ptr128);
				tttttttt = _mm_slli_epi16(tttttttt, upshift);
				_mm_storeu_si128(ptr128++, tttttttt);
			}
		}
		else
		{
			for(x=0;x<width8; x+=8)
			{		
				__m128i tttttttt = _mm_load_si128(ptr128);
				tttttttt = _mm_slli_epi16(tttttttt, upshift);
				_mm_store_si128(ptr128++, tttttttt);
			}
		}
	}

	for(;x<pixels; x++)
	{		
		int val = signed_in_rgb16[x];
		val <<= upshift;
		if(val < 0) val = 0;
		if(val > 65535) val = 65535;
		out_rgb16[x] = val;
	}
}


void ChannelYUYV16toPlanarYUV16(unsigned short *planar_output[], unsigned short *out_YUV, int width, int colorspace)
{
	int x=0;
	int width16 = width & 0xfff0;
	unsigned short *Yptr = planar_output[0];
	unsigned short *Uptr = planar_output[2];
	unsigned short *Vptr = planar_output[1];

#if SSE2 && 1
	__m128i yyyyyyyy;
	__m128i yyyyyyy2;
	__m128i uuuuuuuu;
	__m128i vvvvvvvv;
	__m128i uuuuuuu1;
	__m128i vvvvvvv1;
	__m128i uuuuuuu2;
	__m128i vvvvvvv2;
	__m128i tttttttt,ttttttt2;
	
	if(colorspace & COLOR_SPACE_422_TO_444)
	{
		for(;x<width16; x+=16) // TODO 4:2:2 to 4:4:4 blend
		{
			int chromax = x>>1;
			yyyyyyyy = _mm_loadu_si128((__m128i *)&Yptr[x]);
			yyyyyyy2 = _mm_loadu_si128((__m128i *)&Yptr[x+8]);
			uuuuuuuu = _mm_loadu_si128((__m128i *)&Uptr[chromax]);
			vvvvvvvv = _mm_loadu_si128((__m128i *)&Vptr[chromax]);

			uuuuuuuu = _mm_srli_epi16(uuuuuuuu,1); //15-bit
			vvvvvvvv = _mm_srli_epi16(vvvvvvvv,1);

			uuuuuuu1 = _mm_unpacklo_epi16(uuuuuuuu, uuuuuuuu);
			uuuuuuu2 = _mm_unpackhi_epi16(uuuuuuuu, uuuuuuuu);
			vvvvvvv1 = _mm_unpacklo_epi16(vvvvvvvv, vvvvvvvv);
			vvvvvvv2 = _mm_unpackhi_epi16(vvvvvvvv, vvvvvvvv);

			tttttttt = _mm_srli_si128(uuuuuuu1, 2);
			tttttttt = _mm_insert_epi16(tttttttt, Uptr[chromax+4]>>1, 7);
			uuuuuuu1 = _mm_adds_epu16(uuuuuuu1, tttttttt); //16-bit

			ttttttt2 = _mm_srli_si128(vvvvvvv1, 2);
			ttttttt2 = _mm_insert_epi16(ttttttt2, Vptr[chromax+4]>>1, 7);
			vvvvvvv1 = _mm_adds_epu16(vvvvvvv1, ttttttt2); //16-bit

			tttttttt = _mm_srli_si128(uuuuuuu2, 2);
			tttttttt = _mm_insert_epi16(tttttttt, Uptr[chromax+8]>>1, 7);
			uuuuuuu2 = _mm_adds_epu16(uuuuuuu2, tttttttt); //16-bit

			ttttttt2 = _mm_srli_si128(vvvvvvv2, 2);
			ttttttt2 = _mm_insert_epi16(ttttttt2, Vptr[chromax+8]>>1, 7);
			vvvvvvv2 = _mm_adds_epu16(vvvvvvv2, ttttttt2); //16-bit
			
			_mm_storeu_si128((__m128i *)&out_YUV[x], yyyyyyyy);
			_mm_storeu_si128((__m128i *)&out_YUV[x+8], yyyyyyy2);
			_mm_storeu_si128((__m128i *)&out_YUV[x+width], uuuuuuu1);
			_mm_storeu_si128((__m128i *)&out_YUV[x+width+8], uuuuuuu2);
			_mm_storeu_si128((__m128i *)&out_YUV[x+width*2], vvvvvvv1);
			_mm_storeu_si128((__m128i *)&out_YUV[x+width*2+8], vvvvvvv2);
		}
	}
	else
	{
		for(;x<width16; x+=16)
		{
			yyyyyyyy = _mm_loadu_si128((__m128i *)&Yptr[x]);
			yyyyyyy2 = _mm_loadu_si128((__m128i *)&Yptr[x+8]);
			uuuuuuuu = _mm_loadu_si128((__m128i *)&Uptr[x>>1]);
			vvvvvvvv = _mm_loadu_si128((__m128i *)&Vptr[x>>1]);

			uuuuuuu1 = _mm_unpacklo_epi16(uuuuuuuu, uuuuuuuu);
			uuuuuuu2 = _mm_unpackhi_epi16(uuuuuuuu, uuuuuuuu);
			vvvvvvv1 = _mm_unpacklo_epi16(vvvvvvvv, vvvvvvvv);
			vvvvvvv2 = _mm_unpackhi_epi16(vvvvvvvv, vvvvvvvv);
			
			_mm_storeu_si128((__m128i *)&out_YUV[x], yyyyyyyy);
			_mm_storeu_si128((__m128i *)&out_YUV[x+8], yyyyyyy2);
			_mm_storeu_si128((__m128i *)&out_YUV[x+width], uuuuuuu1);
			_mm_storeu_si128((__m128i *)&out_YUV[x+width+8], uuuuuuu2);
			_mm_storeu_si128((__m128i *)&out_YUV[x+width*2], vvvvvvv1);
			_mm_storeu_si128((__m128i *)&out_YUV[x+width*2+8], vvvvvvv2);
		}
	}
#endif
	for(; x<width; x+=2)
	{
		out_YUV[x] = Yptr[x+0];
		out_YUV[x+width] = Uptr[x>>1];
		out_YUV[x+width*2] = Vptr[x>>1];
		out_YUV[x+1] = Yptr[x+1];
		out_YUV[x+width+1] = Uptr[x>>1];
		out_YUV[x+width*2+1] = Vptr[x>>1];
	}
}


void PlanarRGB16toPlanarYUV16(unsigned short *linebufRGB, unsigned short *linebufYUV, int width, int colorspace)
{
	int column;
	int shift = BITSUSED;
	float fprecision = (float)(1<<shift);
	int y_rmult;
	int y_gmult;
	int y_bmult;
	int y_offset;
	int u_rmult;
	int u_gmult;
	int u_bmult;
	int	u_offset;
	int v_rmult;
	int v_gmult;
	int v_bmult;
	int	v_offset;
	int width8 = width & 0xfff8;
	unsigned short *R_ptr;
	unsigned short *G_ptr;
	unsigned short *B_ptr;
	unsigned short *Y_ptr;
	unsigned short *U_ptr;
	unsigned short *V_ptr;
	bool unaligned = (((uintptr_t)linebufRGB) & 0x0F) || (((uintptr_t)linebufYUV) & 0x0F);

	
	switch(colorspace & COLOR_SPACE_MASK)
	{
		case COLOR_SPACE_CG_601:
			// sRGB + 601
			// Floating point arithmetic is 
			// Y  = 0.257R + 0.504G + 0.098B + 16;
			// Cb =-0.148R - 0.291G + 0.439B + 128;
			// Cr = 0.439R - 0.368G - 0.071B + 128;
			y_rmult = (int)(fprecision * 0.257f);
			y_gmult = (int)(fprecision * 0.504f);
			y_bmult = (int)(fprecision * 0.098f);
			y_offset= (65536 * 16) >> 8; 
										 
			u_rmult = (int)(fprecision * 0.148f);
			u_gmult = (int)(fprecision * 0.291f);
			u_bmult = (int)(fprecision * 0.439f);
			u_offset= 32768;			 
										 
			v_rmult = (int)(fprecision * 0.439f);
			v_gmult = (int)(fprecision * 0.368f);
			v_bmult = (int)(fprecision * 0.071f);
			v_offset= 32768;
#if TWEAK_RGB2YUV
			y_rmult += tweak_CG601[0];
			y_gmult += tweak_CG601[1];
			y_bmult += tweak_CG601[2];
			u_rmult += tweak_CG601[3];
			u_gmult += tweak_CG601[4];
			u_bmult += tweak_CG601[5];
			v_rmult += tweak_CG601[6];
			v_gmult += tweak_CG601[7];
			v_bmult += tweak_CG601[8];
#endif
			break;

		case COLOR_SPACE_VS_709:
			// video systems RGB + 709
			// Floating point arithmetic is 
			// Y = 0.213R + 0.715G + 0.072B
			// Cb = -0.117R - 0.394G + 0.511B + 128
			// Cr = 0.511R - 0.464G - 0.047B + 128	
			y_rmult = (int)(fprecision * 0.213f);
			y_gmult = (int)(fprecision * 0.715f);
			y_bmult = (int)(fprecision * 0.072f);
			y_offset= 0;
			
			u_rmult = (int)(fprecision * 0.117f);
			u_gmult = (int)(fprecision * 0.394f);
			u_bmult = (int)(fprecision * 0.511f);
			u_offset= 32768;

			v_rmult = (int)(fprecision * 0.511f);
			v_gmult = (int)(fprecision * 0.464f);
			v_bmult = (int)(fprecision * 0.047f);
			v_offset= 32768;

#if TWEAK_RGB2YUV
			y_rmult += tweak_VS709[0];
			y_gmult += tweak_VS709[1];
			y_bmult += tweak_VS709[2];
			u_rmult += tweak_VS709[3];
			u_gmult += tweak_VS709[4];
			u_bmult += tweak_VS709[5];
			v_rmult += tweak_VS709[6];
			v_gmult += tweak_VS709[7];
			v_bmult += tweak_VS709[8];
#endif
			break;		

		case COLOR_SPACE_VS_601:
			// video systems RGB + 601
			// Floating point arithmetic is 
			// Y = 0.299R + 0.587G + 0.114B
			// Cb = -0.172R - 0.339G + 0.511B + 128
			// Cr = 0.511R - 0.428G - 0.083B + 128;	
			y_rmult = (int)(fprecision * 0.299f);
			y_gmult = (int)(fprecision * 0.587f);
			y_bmult = (int)(fprecision * 0.114f);
			y_offset= 0;
			
			u_rmult = (int)(fprecision * 0.172f);
			u_gmult = (int)(fprecision * 0.339f);
			u_bmult = (int)(fprecision * 0.511f);
			u_offset= 32768;

			v_rmult = (int)(fprecision * 0.511f);
			v_gmult = (int)(fprecision * 0.428f);
			v_bmult = (int)(fprecision * 0.083f);
			v_offset= 32768;

#if TWEAK_RGB2YUV
			y_rmult += tweak_VS601[0];
			y_gmult += tweak_VS601[1];
			y_bmult += tweak_VS601[2];
			u_rmult += tweak_VS601[3];
			u_gmult += tweak_VS601[4];
			u_bmult += tweak_VS601[5];
			v_rmult += tweak_VS601[6];
			v_gmult += tweak_VS601[7];
			v_bmult += tweak_VS601[8];
#endif
			break;			
			
		default:
		case COLOR_SPACE_CG_709:
			// sRGB + 709
			// Y = 0.183R + 0.614G + 0.062B + 16
			// Cb = -0.101R - 0.338G + 0.439B + 128
			// Cr = 0.439R - 0.399G - 0.040B + 128
			y_rmult = (int)(fprecision * 0.183f);
			y_gmult = (int)(fprecision * 0.614f);
			y_bmult = (int)(fprecision * 0.062f);
			y_offset= (65536 * 16) >> 8;
			
			u_rmult = (int)(fprecision * 0.101f);
			u_gmult = (int)(fprecision * 0.338f);
			u_bmult = (int)(fprecision * 0.439f);
			u_offset= 32768;

			v_rmult = (int)(fprecision * 0.439f);
			v_gmult = (int)(fprecision * 0.399f);
			v_bmult = (int)(fprecision * 0.040f);
			v_offset= 32768;

#if TWEAK_RGB2YUV
			y_rmult += tweak_CG709[0];
			y_gmult += tweak_CG709[1];
			y_bmult += tweak_CG709[2];
			u_rmult += tweak_CG709[3];
			u_gmult += tweak_CG709[4];
			u_bmult += tweak_CG709[5];
			v_rmult += tweak_CG709[6];
			v_gmult += tweak_CG709[7];
			v_bmult += tweak_CG709[8];
#endif
			break;
	}	

	y_offset >>= 2;
	u_offset >>= 2;
	v_offset >>= 2;

#if TWEAK_RGB2YUV
	y_rmult += tweak[0];
	y_gmult += tweak[1];
	y_bmult += tweak[2];
	u_rmult += tweak[3];
	u_gmult += tweak[4];
	u_bmult += tweak[5];
	v_rmult += tweak[6];
	v_gmult += tweak[7];
	v_bmult += tweak[8];
#endif

	
	R_ptr = &linebufRGB[0];
	G_ptr = &linebufRGB[width];
	B_ptr = &linebufRGB[width*2];

	Y_ptr = &linebufYUV[0];
	U_ptr = &linebufYUV[width];
	V_ptr = &linebufYUV[width*2];
	
	column = 0;
#if SSE2 && 1
	{
		__m128i yoff_epi16 = _mm_set1_epi16(y_offset);
		__m128i uoff_epi16 = _mm_set1_epi16(u_offset);
		__m128i voff_epi16 = _mm_set1_epi16(v_offset);

		__m128i y_rmult_epi16 = _mm_set1_epi16(y_rmult);
		__m128i y_gmult_epi16 = _mm_set1_epi16(y_gmult);
		__m128i y_bmult_epi16 = _mm_set1_epi16(y_bmult);
		__m128i u_rmult_epi16 = _mm_set1_epi16(-u_rmult);
		__m128i u_gmult_epi16 = _mm_set1_epi16(-u_gmult);
		__m128i u_bmult_epi16 = _mm_set1_epi16(u_bmult);
		__m128i v_rmult_epi16 = _mm_set1_epi16(v_rmult);
		__m128i v_gmult_epi16 = _mm_set1_epi16(-v_gmult);
		__m128i v_bmult_epi16 = _mm_set1_epi16(-v_bmult);

		__m128i overflowprotect_epi16 = _mm_set1_epi16(0x7fff-0x3fff);
	
		if(unaligned)
		{
			for(; column < width8; column+=8)
			{
				__m128i yyyyyyyy, uuuuuuuu, vvvvvvvv, tttttttt;
				__m128i rrrrrrrr = _mm_loadu_si128((__m128i *)&R_ptr[column]);
				__m128i gggggggg = _mm_loadu_si128((__m128i *)&G_ptr[column]);
				__m128i bbbbbbbb = _mm_loadu_si128((__m128i *)&B_ptr[column]);

				rrrrrrrr = _mm_srli_epi16(rrrrrrrr, 1);//15-bit
				gggggggg = _mm_srli_epi16(gggggggg, 1);//15-bit
				bbbbbbbb = _mm_srli_epi16(bbbbbbbb, 1);//15-bit

				yyyyyyyy = _mm_mulhi_epi16(rrrrrrrr, y_rmult_epi16);
				tttttttt = _mm_mulhi_epi16(gggggggg, y_gmult_epi16);
				yyyyyyyy = _mm_adds_epi16(yyyyyyyy, tttttttt);
				tttttttt = _mm_mulhi_epi16(bbbbbbbb, y_bmult_epi16);
				yyyyyyyy = _mm_adds_epi16(yyyyyyyy, tttttttt);
				yyyyyyyy = _mm_slli_epi16(yyyyyyyy, 2); //12 to 14-bit
				yyyyyyyy = _mm_adds_epi16(yyyyyyyy, yoff_epi16); 
				
				uuuuuuuu = _mm_mulhi_epi16(rrrrrrrr, u_rmult_epi16); //15 bit
				tttttttt = _mm_mulhi_epi16(gggggggg, u_gmult_epi16);
				uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);
				tttttttt = _mm_mulhi_epi16(bbbbbbbb, u_bmult_epi16);
				uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);;
				uuuuuuuu = _mm_slli_epi16(uuuuuuuu, 2); //12 to 14-bit
				uuuuuuuu = _mm_adds_epi16(uuuuuuuu, uoff_epi16); 
				
				vvvvvvvv = _mm_mulhi_epi16(rrrrrrrr, v_rmult_epi16); //15 bit
				tttttttt = _mm_mulhi_epi16(gggggggg, v_gmult_epi16);
				vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);
				tttttttt = _mm_mulhi_epi16(bbbbbbbb, v_bmult_epi16);
				vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);;
				vvvvvvvv = _mm_slli_epi16(vvvvvvvv, 2); //12 to 14-bit
				vvvvvvvv = _mm_adds_epi16(vvvvvvvv, voff_epi16); 

				yyyyyyyy = _mm_adds_epi16(yyyyyyyy, overflowprotect_epi16); 
				yyyyyyyy = _mm_subs_epu16(yyyyyyyy, overflowprotect_epi16); 
				uuuuuuuu = _mm_adds_epi16(uuuuuuuu, overflowprotect_epi16); 
				uuuuuuuu = _mm_subs_epu16(uuuuuuuu, overflowprotect_epi16); 
				vvvvvvvv = _mm_adds_epi16(vvvvvvvv, overflowprotect_epi16); 
				vvvvvvvv = _mm_subs_epu16(vvvvvvvv, overflowprotect_epi16); 

				yyyyyyyy = _mm_slli_epi16(yyyyyyyy, 2); //14 to 16-bit
				uuuuuuuu = _mm_slli_epi16(uuuuuuuu, 2); //14 to 16-bit
				vvvvvvvv = _mm_slli_epi16(vvvvvvvv, 2); //14 to 16-bit

				_mm_storeu_si128((__m128i *)&Y_ptr[column], yyyyyyyy);
				_mm_storeu_si128((__m128i *)&U_ptr[column], uuuuuuuu);
				_mm_storeu_si128((__m128i *)&V_ptr[column], vvvvvvvv);
			}
		}
		else
		{
			for(; column < width8; column+=8)
			{
				__m128i yyyyyyyy, uuuuuuuu, vvvvvvvv, tttttttt;
				__m128i rrrrrrrr = _mm_load_si128((__m128i *)&R_ptr[column]);
				__m128i gggggggg = _mm_load_si128((__m128i *)&G_ptr[column]);
				__m128i bbbbbbbb = _mm_load_si128((__m128i *)&B_ptr[column]);

				rrrrrrrr = _mm_srli_epi16(rrrrrrrr, 1);//15-bit
				gggggggg = _mm_srli_epi16(gggggggg, 1);//15-bit
				bbbbbbbb = _mm_srli_epi16(bbbbbbbb, 1);//15-bit

				yyyyyyyy = _mm_mulhi_epi16(rrrrrrrr, y_rmult_epi16);
				tttttttt = _mm_mulhi_epi16(gggggggg, y_gmult_epi16);
				yyyyyyyy = _mm_adds_epi16(yyyyyyyy, tttttttt);
				tttttttt = _mm_mulhi_epi16(bbbbbbbb, y_bmult_epi16);
				yyyyyyyy = _mm_adds_epi16(yyyyyyyy, tttttttt);
				yyyyyyyy = _mm_slli_epi16(yyyyyyyy, 2); //12 to 14-bit
				yyyyyyyy = _mm_adds_epi16(yyyyyyyy, yoff_epi16); 
				
				uuuuuuuu = _mm_mulhi_epi16(rrrrrrrr, u_rmult_epi16); //15 bit
				tttttttt = _mm_mulhi_epi16(gggggggg, u_gmult_epi16);
				uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);
				tttttttt = _mm_mulhi_epi16(bbbbbbbb, u_bmult_epi16);
				uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);;
				uuuuuuuu = _mm_slli_epi16(uuuuuuuu, 2); //12 to 14-bit
				uuuuuuuu = _mm_adds_epi16(uuuuuuuu, uoff_epi16); 
				
				vvvvvvvv = _mm_mulhi_epi16(rrrrrrrr, v_rmult_epi16); //15 bit
				tttttttt = _mm_mulhi_epi16(gggggggg, v_gmult_epi16);
				vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);
				tttttttt = _mm_mulhi_epi16(bbbbbbbb, v_bmult_epi16);
				vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);;
				vvvvvvvv = _mm_slli_epi16(vvvvvvvv, 2); //12 to 14-bit
				vvvvvvvv = _mm_adds_epi16(vvvvvvvv, voff_epi16); 

				yyyyyyyy = _mm_adds_epi16(yyyyyyyy, overflowprotect_epi16); 
				yyyyyyyy = _mm_subs_epu16(yyyyyyyy, overflowprotect_epi16); 
				uuuuuuuu = _mm_adds_epi16(uuuuuuuu, overflowprotect_epi16); 
				uuuuuuuu = _mm_subs_epu16(uuuuuuuu, overflowprotect_epi16); 
				vvvvvvvv = _mm_adds_epi16(vvvvvvvv, overflowprotect_epi16); 
				vvvvvvvv = _mm_subs_epu16(vvvvvvvv, overflowprotect_epi16); 

				yyyyyyyy = _mm_slli_epi16(yyyyyyyy, 2); //14 to 16-bit
				uuuuuuuu = _mm_slli_epi16(uuuuuuuu, 2); //14 to 16-bit
				vvvvvvvv = _mm_slli_epi16(vvvvvvvv, 2); //14 to 16-bit

				_mm_store_si128((__m128i *)&Y_ptr[column], yyyyyyyy);
				_mm_store_si128((__m128i *)&U_ptr[column], uuuuuuuu);
				_mm_store_si128((__m128i *)&V_ptr[column], vvvvvvvv);
			}
		}
	}
#endif


	// Process the rest of the column
	for(; column < width; column ++) 
	{
		int R, G, B;
		int Y, U, V;

		/***** Load  the first set of RGB values *****/
		
		R = R_ptr[column] >> 1;
		G = G_ptr[column] >> 1;
		B = B_ptr[column] >> 1;

		// Convert to YCbCr
		Y = (((( y_rmult * R)>>16) + (( y_gmult * G)>>16) + (( y_bmult * B)>>16))<<2) + y_offset;
		U = ((((-u_rmult * R)>>16) + ((-u_gmult * G)>>16) + (( u_bmult * B)>>16))<<2) + u_offset;
		V = (((( v_rmult * R)>>16) + ((-v_gmult * G)>>16) + ((-v_bmult * B)>>16))<<2) + v_offset;

		// Store the YCbCr values 
		if(Y < 0) Y = 0;
		if(Y > 16383) Y = 16383;
		if(U < 0) U = 0;
		if(U > 16383) U = 16383;
		if(V < 0) V = 0;
		if(V > 16383) V = 16383;

		Y <<= 2;
		U <<= 2;
		V <<= 2;


		Y_ptr[column] = Y;
		U_ptr[column] = U;
		V_ptr[column] = V;
	}		
}


void PlanarYUV16toPlanarRGB16(unsigned short *linebufYUV, unsigned short *linebufRGB, int width, int colorspace)
{
	int column;
	int shift = BITSUSED;
	float fprecision = (float)(1<<shift);
	unsigned short *R_ptr;
	unsigned short *G_ptr;
	unsigned short *B_ptr;
	unsigned short *Y_ptr;
	unsigned short *U_ptr;
	unsigned short *V_ptr;
	bool unaligned = (((uintptr_t)linebufRGB) & 0x0F) || (((uintptr_t)linebufYUV) & 0x0F);
	int planar8pixel = colorspace & COLOR_SPACE_8_PIXEL_PLANAR;

	// R = (Y           + r_vmult * V);
	// G = (Y*2 -  g_umult * U - g_vmult * V)
	// B = (Y + 2 * b_umult * U);
	
	// sRGB + 601
	// Floating point arithmetic is 
	// Y = Y - y_offset;
	// Y2 = Y2 - y_offset;
	// U = U - 0.5;
	// V = V - 0.5;
	// Y = Y * 1.164;
	// R = (Y             + 1.596*V);
	// G = (Y*2 - 0.391*U - 0.813*V)
	// B = (Y +   2.018*U);
	
	//COLOR_SPACE_CG_601
	int y_offset = 2048;
	int ymult;
	int r_vmult;
	int g_vmult;
	int g_umult;
	int b_umult;
	int u_offset = 1<<14;
	int v_offset = 1<<14;
	int saturate = 1;


	switch(colorspace & COLOR_SPACE_MASK)
	{
	case COLOR_SPACE_CG_601:
		y_offset = 2048;
		ymult   = (int)(fprecision * 1.164f);
		r_vmult = (int)(fprecision * 1.596f);
		g_vmult = (int)(fprecision * 0.813f);
		g_umult = (int)(fprecision * 0.391f);
		b_umult = (int)(fprecision * 2.018f);
		saturate = 1;
		
#if TWEAK_YUV2RGB
		y_offset+= tweakYUV2RGB_CG601[0];
		ymult   += tweakYUV2RGB_CG601[1];
		r_vmult += tweakYUV2RGB_CG601[2];
		g_vmult += tweakYUV2RGB_CG601[3];
		g_umult += tweakYUV2RGB_CG601[4];
		b_umult += tweakYUV2RGB_CG601[5];
		u_offset+= tweakYUV2RGB_CG601[6];
		v_offset+= tweakYUV2RGB_CG601[7];
#endif
		break;

	case COLOR_SPACE_CG_709:
		y_offset = 2048;
		ymult = (int)(fprecision * 1.164f);
		r_vmult = (int)(fprecision * 1.793f);
		g_vmult = (int)(fprecision * 0.534f);
		g_umult = (int)(fprecision * 0.213f);
		b_umult = (int)(fprecision * 2.115f);
		saturate = 1;
		
#if TWEAK_YUV2RGB
		y_offset+= tweakYUV2RGB_CG709[0];
		ymult   += tweakYUV2RGB_CG709[1];
		r_vmult += tweakYUV2RGB_CG709[2];
		g_vmult += tweakYUV2RGB_CG709[3];
		g_umult += tweakYUV2RGB_CG709[4];
		b_umult += tweakYUV2RGB_CG709[5];
		u_offset+= tweakYUV2RGB_CG709[6];
		v_offset+= tweakYUV2RGB_CG709[7];
#endif
		break;

	case COLOR_SPACE_VS_601:
		y_offset = 0;
		ymult = (int)(fprecision * 1.0f);
		r_vmult = (int)(fprecision * 1.371f);
		g_vmult = (int)(fprecision * 0.698f);
		g_umult = (int)(fprecision * 0.336f);
		b_umult = (int)(fprecision * 1.732f);
		saturate = 0;

		y_offset+= tweakYUV2RGB_VS601[0];
		ymult   += tweakYUV2RGB_VS601[1];
		r_vmult += tweakYUV2RGB_VS601[2];
		g_vmult += tweakYUV2RGB_VS601[3];
		g_umult += tweakYUV2RGB_VS601[4];
		b_umult += tweakYUV2RGB_VS601[5];
		u_offset+= tweakYUV2RGB_VS601[6];
		v_offset+= tweakYUV2RGB_VS601[7];
		break;

	default:
	case COLOR_SPACE_VS_709:
		y_offset = 0;
		ymult = (int)(fprecision * 1.0f);
		r_vmult = (int)(fprecision * 1.540f);
		g_vmult = (int)(fprecision * 0.459f);
		g_umult = (int)(fprecision * 0.183f);
		b_umult = (int)(fprecision * 1.816f);
		saturate = 0;

#if TWEAK_YUV2RGB
		y_offset+= tweakYUV2RGB_VS709[0];
		ymult   += tweakYUV2RGB_VS709[1];
		r_vmult += tweakYUV2RGB_VS709[2];
		g_vmult += tweakYUV2RGB_VS709[3];
		g_umult += tweakYUV2RGB_VS709[4];
		b_umult += tweakYUV2RGB_VS709[5];
		u_offset+= tweakYUV2RGB_VS709[6];
		v_offset+= tweakYUV2RGB_VS709[7];
#endif
		break;
	}
		
	
#if TWEAK_YUV2RGB && 0
	y_offset+= tweak[0];
	ymult   += tweak[1];
	r_vmult += tweak[2];
	g_vmult += tweak[3];
	g_umult += tweak[4];
	b_umult += tweak[5];
	u_offset+= tweak[6];
	v_offset+= tweak[7];
#endif


	Y_ptr = &linebufYUV[0];
	U_ptr = &linebufYUV[width];
	V_ptr = &linebufYUV[width*2];

	R_ptr = &linebufRGB[0];
	G_ptr = &linebufRGB[width];
	B_ptr = &linebufRGB[width*2];
	
	column = 0;

	
#if SSE2 && 1
	{
		int width8 = width & 0xfff8;
		__m128i yoff_epi16 = _mm_set1_epi16(y_offset);
		__m128i uoff_epi16 = _mm_set1_epi16(u_offset);
		__m128i voff_epi16 = _mm_set1_epi16(v_offset);
		__m128i ymult_epi16 = _mm_set1_epi16(ymult);
		__m128i r_vmult_epi16 = _mm_set1_epi16(r_vmult);
		__m128i g_vmult_epi16 = _mm_set1_epi16(-g_vmult);
		__m128i g_umult_epi16 = _mm_set1_epi16(-g_umult);
		__m128i b_umult_epi16 = _mm_set1_epi16(b_umult);

		__m128i overflowprotect_epi16 = _mm_set1_epi16(0x7fff-0x3fff);

		if(unaligned)
		{	
			for(; column < width8; column+=8)
			{
				__m128i rrrrrrrr, gggggggg, bbbbbbbb, tttttttt;
				__m128i yyyyyyyy = _mm_loadu_si128((__m128i *)&Y_ptr[column]);
				__m128i uuuuuuuu = _mm_loadu_si128((__m128i *)&U_ptr[column]);
				__m128i vvvvvvvv = _mm_loadu_si128((__m128i *)&V_ptr[column]);

				yyyyyyyy = _mm_srli_epi16(yyyyyyyy, 1);//15-bit
				uuuuuuuu = _mm_srli_epi16(uuuuuuuu, 1);//15-bit
				vvvvvvvv = _mm_srli_epi16(vvvvvvvv, 1);//15-bit

				yyyyyyyy = _mm_subs_epi16(yyyyyyyy, yoff_epi16); 
				uuuuuuuu = _mm_subs_epi16(uuuuuuuu, uoff_epi16); 
				vvvvvvvv = _mm_subs_epi16(vvvvvvvv, voff_epi16); 

				yyyyyyyy = _mm_mulhi_epi16(yyyyyyyy, ymult_epi16);

				rrrrrrrr = _mm_mulhi_epi16(vvvvvvvv, r_vmult_epi16);
				rrrrrrrr = _mm_adds_epi16(rrrrrrrr, yyyyyyyy); 
				rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 2); //12 to 14-bit

				tttttttt = _mm_mulhi_epi16(uuuuuuuu, g_umult_epi16);
				gggggggg = _mm_adds_epi16(yyyyyyyy, tttttttt);
				tttttttt = _mm_mulhi_epi16(vvvvvvvv, g_vmult_epi16);
				gggggggg = _mm_adds_epi16(gggggggg, tttttttt);
				gggggggg = _mm_slli_epi16(gggggggg, 2); //12 to 14-bit

				bbbbbbbb = _mm_mulhi_epi16(uuuuuuuu, b_umult_epi16);
				bbbbbbbb = _mm_adds_epi16(bbbbbbbb, yyyyyyyy); 
				bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 2); //12 to 14-bit

				rrrrrrrr = _mm_adds_epi16(rrrrrrrr, overflowprotect_epi16); 
				rrrrrrrr = _mm_subs_epu16(rrrrrrrr, overflowprotect_epi16); 
				gggggggg = _mm_adds_epi16(gggggggg, overflowprotect_epi16); 
				gggggggg = _mm_subs_epu16(gggggggg, overflowprotect_epi16); 
				bbbbbbbb = _mm_adds_epi16(bbbbbbbb, overflowprotect_epi16); 
				bbbbbbbb = _mm_subs_epu16(bbbbbbbb, overflowprotect_epi16); 

				rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 2); //14 to 16-bit
				gggggggg = _mm_slli_epi16(gggggggg, 2); //14 to 16-bit
				bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 2); //14 to 16-bit

				if(planar8pixel)
				{
					_mm_storeu_si128((__m128i *)&R_ptr[column*3], rrrrrrrr);
					_mm_storeu_si128((__m128i *)&R_ptr[column*3+8], gggggggg);
					_mm_storeu_si128((__m128i *)&R_ptr[column*3+16], bbbbbbbb);
				}
				else
				{
					_mm_storeu_si128((__m128i *)&R_ptr[column], rrrrrrrr);
					_mm_storeu_si128((__m128i *)&G_ptr[column], gggggggg);
					_mm_storeu_si128((__m128i *)&B_ptr[column], bbbbbbbb);
				}

			}
		}
		else
		{
			for(; column < width8; column+=8)
			{
				__m128i rrrrrrrr, gggggggg, bbbbbbbb, tttttttt;
				__m128i yyyyyyyy = _mm_load_si128((__m128i *)&Y_ptr[column]);
				__m128i uuuuuuuu = _mm_load_si128((__m128i *)&U_ptr[column]);
				__m128i vvvvvvvv = _mm_load_si128((__m128i *)&V_ptr[column]);

				yyyyyyyy = _mm_srli_epi16(yyyyyyyy, 1);//15-bit
				uuuuuuuu = _mm_srli_epi16(uuuuuuuu, 1);//15-bit
				vvvvvvvv = _mm_srli_epi16(vvvvvvvv, 1);//15-bit

				yyyyyyyy = _mm_subs_epi16(yyyyyyyy, yoff_epi16); 
				uuuuuuuu = _mm_subs_epi16(uuuuuuuu, uoff_epi16); 
				vvvvvvvv = _mm_subs_epi16(vvvvvvvv, voff_epi16); 

				yyyyyyyy = _mm_mulhi_epi16(yyyyyyyy, ymult_epi16);

				rrrrrrrr = _mm_mulhi_epi16(vvvvvvvv, r_vmult_epi16);
				rrrrrrrr = _mm_adds_epi16(rrrrrrrr, yyyyyyyy); 
				rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 2); //12 to 14-bit

				tttttttt = _mm_mulhi_epi16(uuuuuuuu, g_umult_epi16);
				gggggggg = _mm_adds_epi16(yyyyyyyy, tttttttt);
				tttttttt = _mm_mulhi_epi16(vvvvvvvv, g_vmult_epi16);
				gggggggg = _mm_adds_epi16(gggggggg, tttttttt);
				gggggggg = _mm_slli_epi16(gggggggg, 2); //12 to 14-bit

				bbbbbbbb = _mm_mulhi_epi16(uuuuuuuu, b_umult_epi16);
				bbbbbbbb = _mm_adds_epi16(bbbbbbbb, yyyyyyyy); 
				bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 2); //12 to 14-bit

				rrrrrrrr = _mm_adds_epi16(rrrrrrrr, overflowprotect_epi16); 
				rrrrrrrr = _mm_subs_epu16(rrrrrrrr, overflowprotect_epi16); 
				gggggggg = _mm_adds_epi16(gggggggg, overflowprotect_epi16); 
				gggggggg = _mm_subs_epu16(gggggggg, overflowprotect_epi16); 
				bbbbbbbb = _mm_adds_epi16(bbbbbbbb, overflowprotect_epi16); 
				bbbbbbbb = _mm_subs_epu16(bbbbbbbb, overflowprotect_epi16); 

				rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 2); //14 to 16-bit
				gggggggg = _mm_slli_epi16(gggggggg, 2); //14 to 16-bit
				bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 2); //14 to 16-bit

				if(planar8pixel)
				{
					_mm_storeu_si128((__m128i *)&R_ptr[column*3], rrrrrrrr);
					_mm_storeu_si128((__m128i *)&R_ptr[column*3+8], gggggggg);
					_mm_storeu_si128((__m128i *)&R_ptr[column*3+16], bbbbbbbb);
				}
				else
				{
					_mm_store_si128((__m128i *)&R_ptr[column], rrrrrrrr);
					_mm_store_si128((__m128i *)&G_ptr[column], gggggggg);
					_mm_store_si128((__m128i *)&B_ptr[column], bbbbbbbb);
				}
			}
		}
	}
#endif
	// Process the rest of the column
	for(; column < width; column ++) 
	{
		int R, G, B;
		int Y, U, V;

		/***** Load  the first set of RGB values *****/
		
		Y = Y_ptr[column]>>1;
		U = U_ptr[column]>>1;
		V = V_ptr[column]>>1;
		Y = Y - y_offset;
		U = U - u_offset;
		V = V - v_offset;
		Y *= ymult;
		Y >>= 16;
		R = (Y			              + (( r_vmult * V)>>16))<<2;
		G = (Y + ((-g_umult * U)>>16) + ((-g_vmult * V)>>16))<<2;
		B = (Y + (( b_umult * U)>>16)                       )<<2;

		if(R < 0) R = 0; if(R > 16383) R = 16383;
		if(G < 0) G = 0; if(G > 16383) G = 16383;
		if(B < 0) B = 0; if(B > 16383) B = 16383;
		
		// Advance the RGB pointers 
		if(planar8pixel)
		{
			assert(0);
		}
		else
		{
			R_ptr[column] = R<<2;
			G_ptr[column] = G<<2;
			B_ptr[column] = B<<2;
		}
	}
}

void ChunkyRGB16toChunkyYUV16(unsigned short *linebufRGB, unsigned short *linebufYUV, int width, int colorspace)
{
	int column;
	int shift = BITSUSED;
	float fprecision = (float)(1<<shift);
	int y_rmult;
	int y_gmult;
	int y_bmult;
	int y_offset;
	int u_rmult;
	int u_gmult;
	int u_bmult;
	int	u_offset;
	int v_rmult;
	int v_gmult;
	int v_bmult;
	int	v_offset;
	int width8 = width & 0xfff8;
	
	switch(colorspace & COLOR_SPACE_MASK)
	{
		case COLOR_SPACE_CG_601:
			// sRGB + 601
			// Floating point arithmetic is 
			// Y  = 0.257R + 0.504G + 0.098B + 16;
			// Cb =-0.148R - 0.291G + 0.439B + 128;
			// Cr = 0.439R - 0.368G - 0.071B + 128;
			y_rmult = (int)(fprecision * 0.257f);
			y_gmult = (int)(fprecision * 0.504f);
			y_bmult = (int)(fprecision * 0.098f);
			y_offset = (65536 * 16) >> 8;

			u_rmult = (int)(fprecision * 0.148f);
			u_gmult = (int)(fprecision * 0.291f);
			u_bmult = (int)(fprecision * 0.439f);
			u_offset= 32768;

			v_rmult = (int)(fprecision * 0.439f);
			v_gmult = (int)(fprecision * 0.368f);
			v_bmult = (int)(fprecision * 0.071f);
			v_offset= 32768;
#if TWEAK_RGB2YUV
			y_rmult += tweak_CG601[0];
			y_gmult += tweak_CG601[1];
			y_bmult += tweak_CG601[2];
			u_rmult += tweak_CG601[3];
			u_gmult += tweak_CG601[4];
			u_bmult += tweak_CG601[5];
			v_rmult += tweak_CG601[6];
			v_gmult += tweak_CG601[7];
			v_bmult += tweak_CG601[8];
#endif
			break;

		case COLOR_SPACE_VS_709:
			// video systems RGB + 709
			// Floating point arithmetic is 
			// Y = 0.213R + 0.715G + 0.072B
			// Cb = -0.117R - 0.394G + 0.511B + 128
			// Cr = 0.511R - 0.464G - 0.047B + 128	
			y_rmult = (int)(fprecision * 0.213f);
			y_gmult = (int)(fprecision * 0.715f);
			y_bmult = (int)(fprecision * 0.072f);
			y_offset= 0;
			
			u_rmult = (int)(fprecision * 0.117f);
			u_gmult = (int)(fprecision * 0.394f);
			u_bmult = (int)(fprecision * 0.511f);
			u_offset= 32768;

			v_rmult = (int)(fprecision * 0.511f);
			v_gmult = (int)(fprecision * 0.464f);
			v_bmult = (int)(fprecision * 0.047f);
			v_offset= 32768;

#if TWEAK_RGB2YUV
			y_rmult += tweak_VS709[0];
			y_gmult += tweak_VS709[1];
			y_bmult += tweak_VS709[2];
			u_rmult += tweak_VS709[3];
			u_gmult += tweak_VS709[4];
			u_bmult += tweak_VS709[5];
			v_rmult += tweak_VS709[6];
			v_gmult += tweak_VS709[7];
			v_bmult += tweak_VS709[8];
#endif
			break;		

		case COLOR_SPACE_VS_601:
			// video systems RGB + 601
			// Floating point arithmetic is 
			// Y = 0.299R + 0.587G + 0.114B
			// Cb = -0.172R - 0.339G + 0.511B + 128
			// Cr = 0.511R - 0.428G - 0.083B + 128;	
			y_rmult = (int)(fprecision * 0.299f);
			y_gmult = (int)(fprecision * 0.587f);
			y_bmult = (int)(fprecision * 0.114f);
			y_offset= 0;
			
			u_rmult = (int)(fprecision * 0.172f);
			u_gmult = (int)(fprecision * 0.339f);
			u_bmult = (int)(fprecision * 0.511f);
			u_offset= 32768;

			v_rmult = (int)(fprecision * 0.511f);
			v_gmult = (int)(fprecision * 0.428f);
			v_bmult = (int)(fprecision * 0.083f);
			v_offset= 32768;

#if TWEAK_RGB2YUV
			y_rmult += tweak_VS601[0];
			y_gmult += tweak_VS601[1];
			y_bmult += tweak_VS601[2];
			u_rmult += tweak_VS601[3];
			u_gmult += tweak_VS601[4];
			u_bmult += tweak_VS601[5];
			v_rmult += tweak_VS601[6];
			v_gmult += tweak_VS601[7];
			v_bmult += tweak_VS601[8];
#endif
			break;			
			
		default:
		case COLOR_SPACE_CG_709:
			// sRGB + 709
			// Y = 0.183R + 0.614G + 0.062B + 16
			// Cb = -0.101R - 0.338G + 0.439B + 128
			// Cr = 0.439R - 0.399G - 0.040B + 128
			y_rmult = (int)(fprecision * 0.183f);
			y_gmult = (int)(fprecision * 0.614f);
			y_bmult = (int)(fprecision * 0.062f);
			y_offset= (65536 * 16) >> 8;
			
			u_rmult = (int)(fprecision * 0.101f);
			u_gmult = (int)(fprecision * 0.338f);
			u_bmult = (int)(fprecision * 0.439f);
			u_offset= 32768;

			v_rmult = (int)(fprecision * 0.439f);
			v_gmult = (int)(fprecision * 0.399f);
			v_bmult = (int)(fprecision * 0.040f);
			v_offset= 32768;

#if TWEAK_RGB2YUV
			y_rmult += tweak_CG709[0];
			y_gmult += tweak_CG709[1];
			y_bmult += tweak_CG709[2];
			u_rmult += tweak_CG709[3];
			u_gmult += tweak_CG709[4];
			u_bmult += tweak_CG709[5];
			v_rmult += tweak_CG709[6];
			v_gmult += tweak_CG709[7];
			v_bmult += tweak_CG709[8];
#endif
			break;
	}	

	y_offset >>= 2;
	u_offset >>= 2;
	v_offset >>= 2;

#if TWEAK_RGB2YUV
	y_rmult += tweak[0];
	y_gmult += tweak[1];
	y_bmult += tweak[2];
	u_rmult += tweak[3];
	u_gmult += tweak[4];
	u_bmult += tweak[5];
	v_rmult += tweak[6];
	v_gmult += tweak[7];
	v_bmult += tweak[8];
#endif

	
	column = 0;
#if SSE2 && 1
	{
		__m128i yoff_epi16 = _mm_set1_epi16(y_offset);
		__m128i uoff_epi16 = _mm_set1_epi16(u_offset);
		__m128i voff_epi16 = _mm_set1_epi16(v_offset);

		__m128i y_rmult_epi16 = _mm_set1_epi16(y_rmult);
		__m128i y_gmult_epi16 = _mm_set1_epi16(y_gmult);
		__m128i y_bmult_epi16 = _mm_set1_epi16(y_bmult);
		__m128i u_rmult_epi16 = _mm_set1_epi16(-u_rmult);
		__m128i u_gmult_epi16 = _mm_set1_epi16(-u_gmult);
		__m128i u_bmult_epi16 = _mm_set1_epi16(u_bmult);
		__m128i v_rmult_epi16 = _mm_set1_epi16(v_rmult);
		__m128i v_gmult_epi16 = _mm_set1_epi16(-v_gmult);
		__m128i v_bmult_epi16 = _mm_set1_epi16(-v_bmult);
		__m128i rrrrrrrr = _mm_set1_epi16(0);
		__m128i gggggggg = _mm_set1_epi16(0);
		__m128i bbbbbbbb = _mm_set1_epi16(0);


		__m128i overflowprotect_epi16 = _mm_set1_epi16(0x7fff-0x3fff);
	
		for(; column < width8; column+=8)
		{
			__m128i yyyyyyyy, uuuuuuuu, vvvvvvvv, tttttttt;

			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, linebufRGB[0], 0);
			gggggggg = _mm_insert_epi16(gggggggg, linebufRGB[1], 0);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, linebufRGB[2], 0);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, linebufRGB[3], 1);
			gggggggg = _mm_insert_epi16(gggggggg, linebufRGB[4], 1);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, linebufRGB[5], 1);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, linebufRGB[6], 2);
			gggggggg = _mm_insert_epi16(gggggggg, linebufRGB[7], 2);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, linebufRGB[8], 2);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, linebufRGB[9], 3);
			gggggggg = _mm_insert_epi16(gggggggg, linebufRGB[10], 3);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, linebufRGB[11], 3);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, linebufRGB[12], 4);
			gggggggg = _mm_insert_epi16(gggggggg, linebufRGB[13], 4);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, linebufRGB[14], 4);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, linebufRGB[15], 5);
			gggggggg = _mm_insert_epi16(gggggggg, linebufRGB[16], 5);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, linebufRGB[17], 5);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, linebufRGB[18], 6);
			gggggggg = _mm_insert_epi16(gggggggg, linebufRGB[19], 6);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, linebufRGB[20], 6);
			rrrrrrrr = _mm_insert_epi16(rrrrrrrr, linebufRGB[21], 7);
			gggggggg = _mm_insert_epi16(gggggggg, linebufRGB[22], 7);
			bbbbbbbb = _mm_insert_epi16(bbbbbbbb, linebufRGB[23], 7);
			linebufRGB += 24;

			rrrrrrrr = _mm_srli_epi16(rrrrrrrr, 1);//15-bit
			gggggggg = _mm_srli_epi16(gggggggg, 1);//15-bit
			bbbbbbbb = _mm_srli_epi16(bbbbbbbb, 1);//15-bit

			yyyyyyyy = _mm_mulhi_epi16(rrrrrrrr, y_rmult_epi16);
			tttttttt = _mm_mulhi_epi16(gggggggg, y_gmult_epi16);
			yyyyyyyy = _mm_adds_epi16(yyyyyyyy, tttttttt);
			tttttttt = _mm_mulhi_epi16(bbbbbbbb, y_bmult_epi16);
			yyyyyyyy = _mm_adds_epi16(yyyyyyyy, tttttttt);
			yyyyyyyy = _mm_slli_epi16(yyyyyyyy, 2); //12 to 14-bit
			yyyyyyyy = _mm_adds_epi16(yyyyyyyy, yoff_epi16); 
			
			uuuuuuuu = _mm_mulhi_epi16(rrrrrrrr, u_rmult_epi16); //15 bit
			tttttttt = _mm_mulhi_epi16(gggggggg, u_gmult_epi16);
			uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);
			tttttttt = _mm_mulhi_epi16(bbbbbbbb, u_bmult_epi16);
			uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);;
			uuuuuuuu = _mm_slli_epi16(uuuuuuuu, 2); //12 to 14-bit
			uuuuuuuu = _mm_adds_epi16(uuuuuuuu, uoff_epi16); 
			
			vvvvvvvv = _mm_mulhi_epi16(rrrrrrrr, v_rmult_epi16); //15 bit
			tttttttt = _mm_mulhi_epi16(gggggggg, v_gmult_epi16);
			vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);
			tttttttt = _mm_mulhi_epi16(bbbbbbbb, v_bmult_epi16);
			vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);;
			vvvvvvvv = _mm_slli_epi16(vvvvvvvv, 2); //12 to 14-bit
			vvvvvvvv = _mm_adds_epi16(vvvvvvvv, voff_epi16); 

			yyyyyyyy = _mm_adds_epi16(yyyyyyyy, overflowprotect_epi16); 
			yyyyyyyy = _mm_subs_epu16(yyyyyyyy, overflowprotect_epi16); 
			uuuuuuuu = _mm_adds_epi16(uuuuuuuu, overflowprotect_epi16); 
			uuuuuuuu = _mm_subs_epu16(uuuuuuuu, overflowprotect_epi16); 
			vvvvvvvv = _mm_adds_epi16(vvvvvvvv, overflowprotect_epi16); 
			vvvvvvvv = _mm_subs_epu16(vvvvvvvv, overflowprotect_epi16); 

			yyyyyyyy = _mm_slli_epi16(yyyyyyyy, 2); //14 to 16-bit
			uuuuuuuu = _mm_slli_epi16(uuuuuuuu, 2); //14 to 16-bit
			vvvvvvvv = _mm_slli_epi16(vvvvvvvv, 2); //14 to 16-bit

			
			linebufYUV[0] = _mm_extract_epi16(yyyyyyyy, 0);
			linebufYUV[1] = _mm_extract_epi16(uuuuuuuu, 0);
			linebufYUV[2] = _mm_extract_epi16(vvvvvvvv, 0);
			linebufYUV[3] = _mm_extract_epi16(yyyyyyyy, 1);
			linebufYUV[4] = _mm_extract_epi16(uuuuuuuu, 1);
			linebufYUV[5] = _mm_extract_epi16(vvvvvvvv, 1);
			linebufYUV[6] = _mm_extract_epi16(yyyyyyyy, 2);
			linebufYUV[7] = _mm_extract_epi16(uuuuuuuu, 2);
			linebufYUV[8] = _mm_extract_epi16(vvvvvvvv, 2);
			linebufYUV[9] = _mm_extract_epi16(yyyyyyyy, 3);
			linebufYUV[10] = _mm_extract_epi16(uuuuuuuu, 3);
			linebufYUV[11] = _mm_extract_epi16(vvvvvvvv, 3);
			linebufYUV[12] = _mm_extract_epi16(yyyyyyyy, 4);
			linebufYUV[13] = _mm_extract_epi16(uuuuuuuu, 4);
			linebufYUV[14] = _mm_extract_epi16(vvvvvvvv, 4);
			linebufYUV[15] = _mm_extract_epi16(yyyyyyyy, 5);
			linebufYUV[16] = _mm_extract_epi16(uuuuuuuu, 5);
			linebufYUV[17] = _mm_extract_epi16(vvvvvvvv, 5);
			linebufYUV[18] = _mm_extract_epi16(yyyyyyyy, 6);
			linebufYUV[19] = _mm_extract_epi16(uuuuuuuu, 6);
			linebufYUV[20] = _mm_extract_epi16(vvvvvvvv, 6);
			linebufYUV[21] = _mm_extract_epi16(yyyyyyyy, 7);
			linebufYUV[22] = _mm_extract_epi16(uuuuuuuu, 7);
			linebufYUV[23] = _mm_extract_epi16(vvvvvvvv, 7);
			linebufYUV += 24;
		}
	}
#endif


	// Process the rest of the column
	for(; column < width; column ++) 
	{
		int R, G, B;
		int Y, U, V;

		/***** Load  the first set of RGB values *****/
		
		R = linebufRGB[column*3] >> 1;
		G = linebufRGB[column*3+1] >> 1;
		B = linebufRGB[column*3+2] >> 1;

		// Convert to YCbCr
		Y = (((( y_rmult * R)>>16) + (( y_gmult * G)>>16) + (( y_bmult * B)>>16))<<2) + y_offset;
		U = ((((-u_rmult * R)>>16) + ((-u_gmult * G)>>16) + (( u_bmult * B)>>16))<<2) + u_offset;
		V = (((( v_rmult * R)>>16) + ((-v_gmult * G)>>16) + ((-v_bmult * B)>>16))<<2) + v_offset;

		// Store the YCbCr values 
		if(Y < 0) Y = 0;
		if(Y > 16383) Y = 16383;
		if(U < 0) U = 0;
		if(U > 16383) U = 16383;
		if(V < 0) V = 0;
		if(V > 16383) V = 16383;

		Y <<= 2;
		U <<= 2;
		V <<= 2;


		linebufYUV[column*3] = Y;
		linebufYUV[column*3+1] = U;
		linebufYUV[column*3+2] = V;
	}		
}


void ChunkyRGB16toChunkyYUYV16(int width, int height, 
							  unsigned short *rgb16, int RGBpitch, 
							  unsigned short *yuyv16, int YUVpitch, 
							  unsigned short *scratch, int scratchsize,
							  int colorspace)
{
	int row;
	unsigned short *RGB_row;
	unsigned short *YUYV_row; 
	unsigned short *linebufRGB;
	unsigned short *linebufYUV;
	int allocatedLines = 0;

	if(scratch && scratchsize > width*6*2)
	{
		linebufRGB = (unsigned short *)scratch;
		linebufYUV = (unsigned short *)scratch;
		linebufYUV += width*3;
	}
	else
	{
#ifdef __APPLE__
		linebufRGB = (unsigned short *)malloc(width * 6);
		linebufYUV = (unsigned short *)malloc(width * 6);
#else
		linebufRGB = (unsigned short *)_mm_malloc(width*6,16);
		linebufYUV = (unsigned short *)_mm_malloc(width*6,16);
#endif
		allocatedLines = 1;
	}

	RGB_row = rgb16;	
	YUYV_row = yuyv16;

	for(row = 0; row < height; row++) 
	{
		ChunkyRGB16toPlanarRGB16(RGB_row, linebufRGB, width);
		PlanarRGB16toPlanarYUV16(linebufRGB, linebufYUV, width, colorspace);
		PlanarYUV16toChunkyYUYV16(linebufYUV, YUYV_row, width, colorspace);

		// Advance pointers
		RGB_row += RGBpitch>>1;
		YUYV_row += YUVpitch>>1;
	}

	if(allocatedLines)
	{
#ifdef __APPLE__
		if (linebufRGB)
			free(linebufRGB);
		if (linebufYUV)
			free(linebufYUV);
#else
		if (linebufRGB)
			_mm_free(linebufRGB);
		if (linebufYUV)
			_mm_free(linebufYUV);
#endif
	}
}



void ChunkyYUYV16toChunkyRGB16(int width, int height,
							  unsigned short *yuyv16, int YUVpitch, 
							  unsigned short *rgb16, int RGBpitch, 
							  unsigned short *scratch, int scratchsize,
							  int colorspace)
{
	int row;
	unsigned short *RGB_row;
	unsigned short *YUYV_row;
	unsigned short *linebufRGB;
	unsigned short *linebufYUV;
	int allocatedLines = 0;
	RGB_row = rgb16;	
	YUYV_row = yuyv16;
	
	if(scratch && scratchsize > width*6*2)
	{
		linebufRGB = (unsigned short *)scratch;
		linebufYUV = (unsigned short *)scratch;
		linebufYUV += width*3;
	}
	else
	{
#ifdef __APPLE__
		linebufRGB = (unsigned short *)malloc(width * 6);
		linebufYUV = (unsigned short *)malloc(width * 6);
#else
		linebufRGB = (unsigned short *)_mm_malloc(width*6,16);
		linebufYUV = (unsigned short *)_mm_malloc(width*6,16);
#endif
		allocatedLines = 1;
	}

	for(row = 0; row < height; row++) 
	{
		ChunkyYUYV16toPlanarYUV16(YUYV_row, linebufYUV, width, colorspace);
		PlanarYUV16toPlanarRGB16(linebufYUV, linebufRGB, width, colorspace);
		PlanarRGB16toChunkyRGB16(linebufRGB, RGB_row, width);

		// Advance pointers
		RGB_row += RGBpitch>>1;
		YUYV_row += YUVpitch>>1;
	}

	if(allocatedLines)
	{
#ifdef __APPLE__
		if (linebufRGB)
			free(linebufRGB);
		if (linebufYUV)
			free(linebufYUV);
#else
		if (linebufRGB)
			_mm_free(linebufRGB);
		if (linebufYUV)
			_mm_free(linebufYUV);
#endif
	}
}
