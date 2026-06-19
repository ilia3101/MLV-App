/*! @file bayer.c

*  @brief CFABayer Image types, demosaic and wavelet tools
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


#include "stdafx.h"

#define XMMOPT (1 && _XMMOPT)

#include "image.h"
#include "codec.h"
#include "bayer.h"
#include "thread.h"
#include "decoder.h"
#include "convert.h"
#include "DemoasicFrames.h"		//TODO: Change filename to lower case
#include "swap.h"
#include "draw.h"
#include "RGB2YUV.h"			//TODO: Change filename to lower case?
#include "exception.h"
#if WARPSTUFF
#include "WarpLib.h"
#endif

#ifndef countof
#define countof(a)	((int)(sizeof(a)/sizeof(a[0])))
#endif

#ifndef neg
#define neg(x)		(-(x))
#endif

#ifndef ALIGNED_PTR
#define ALIGNED_PTR(p)	((((uintptr_t)(p)) + 0x0F) & (UINTPTR_MAX & ~0x0F))
#endif

#ifndef PTR_DIFF
#define PTR_DIFF(p, q) ((intptr_t)(p) - (intptr_t)(q))
#endif

#if 0
static const float rgb2yuv[4][4] =
{
	{ 0.183f,  0.614f,  0.062f, 16.0f/256.0f},
	{-0.101f, -0.338f,  0.439f, 0.5f},
	{ 0.439f, -0.399f, -0.040f, 0.5f},
	{ 0.000f,  0.000f,  0.000f, 0.0f},
};
#endif

static const float color_matrix[3][4] =
{
	{1.0f,   0,   0,  0},
	{  0, 1.0f,   0,  0},
	{  0,   0, 1.0f,  0},
};

static const float white_balance[3] =
{
	1.0f, 1.0f, 1.0f
};

static const float scale = 256.0f;


// Forward references
void *ApplyActiveMetaData4444(DECODER *decoder, int width, int height, int ypos,
							  uint32_t *src, uint32_t *dst, int colorformat,
							  int *whitebitdepth, int *flags);


void ConvertPackedBayerToRGB32(PIXEL16U *input_buffer, FRAME_INFO *info,
							   int input_pitch, uint8_t *output_buffer,
							   int output_pitch, int width, int height)
{
	uint8_t *output_line = output_buffer;
	PIXEL16U *bayer_line = input_buffer;

	PIXEL16U *bayer_ptr;
	uint8_t *bgra_ptr;

	//const int shift = 8;
	const int alpha = 255;

	int r_rmult  = (int)(color_matrix[0][0] * scale * white_balance[0]);
	int r_gmult  = (int)(color_matrix[0][1] * scale * white_balance[1]);
	int r_bmult  = (int)(color_matrix[0][2] * scale * white_balance[2]);
	int r_offset = (int)(color_matrix[0][3] * scale);

	int g_rmult  = (int)(color_matrix[1][0] * scale * white_balance[0]);
	int g_gmult  = (int)(color_matrix[1][1] * scale * white_balance[1]);
	int g_bmult  = (int)(color_matrix[1][2] * scale * white_balance[2]);
	int g_offset = (int)(color_matrix[1][3] * scale);

	int b_rmult  = (int)(color_matrix[2][0] * scale * white_balance[0]);
	int b_gmult  = (int)(color_matrix[2][1] * scale * white_balance[1]);
	int b_bmult  = (int)(color_matrix[2][2] * scale * white_balance[2]);
	int b_offset = (int)(color_matrix[2][3] * scale);

	//int luma_offset, ymult, r_vmult, g_vmult, g_umult, b_umult;

	const int matrix_non_unity = 0;
	//int wb_non_unity = 0;

	int row;
	int column;

	// The output frame is inverted
	output_line += (height - 1) * output_pitch;
	output_pitch = neg(output_pitch);

	for (row = 0; row < height; row++)
	{
		//PIXEL16U *G, *RG, *BG, *GD;
		PIXEL16U *g1_ptr, *rg_ptr, *bg_ptr, *g2_ptr;

		int noise_array[32];
		int i;

		bayer_ptr = bayer_line;
		bgra_ptr = output_line;

		g1_ptr = bayer_ptr;
		rg_ptr = g1_ptr + input_pitch/4;
		bg_ptr = rg_ptr + input_pitch/4;
		g2_ptr = rg_ptr + input_pitch/4;

		for (i = 0; i < countof(noise_array); i++)
		{
			// Need seven bits of random dithering
			noise_array[i] = (rand() & 0x7F);
		}

		for (column = 0; column < width; column++)
		{
			int R1,G1,B1;
			int rnd = noise_array[column & (countof(noise_array) - 1)];

			int r,g,b;
			//int g1,g2,gdiff,y1,y2,u,v;

			g = (*(g1_ptr++) >> 1);
			r = ((*(rg_ptr++) + 64) >> 0) - (256 << 7) + g;
			b = ((*(bg_ptr++) + 64) >> 0) - (256 << 7) + g;

			if (matrix_non_unity)
			{
				//TODO : need on convert to linear first.

				R1 = ((r*r_rmult + g*r_gmult + b*r_bmult + r_offset) >> 8) + rnd;
				G1 = ((r*g_rmult + g*g_gmult + b*g_bmult + g_offset) >> 8) + rnd;
				B1 = ((r*b_rmult + g*b_gmult + b*b_bmult + b_offset) >> 8) + rnd;

				//TODO : need on convert back to log/display curve.
			}
			else
			{
				R1 = r + rnd;
				G1 = g + rnd;
				B1 = b + rnd;
			}

			R1 >>= 7;
			G1 >>= 7;
			B1 >>= 7;

			if(R1 < 0) R1 = 0;
			if(R1 > 255) R1 = 255;
			if(G1 < 0) G1 = 0;
			if(G1 > 255) G1 = 255;
			if(B1 < 0) B1 = 0;
			if(B1 > 255) B1 = 255;


			*(bgra_ptr++) = B1;
			*(bgra_ptr++) = G1;
			*(bgra_ptr++) = R1;
			*(bgra_ptr++) = alpha;
		}

		// Advance to the next row in the Bayer data and output buffer
		bayer_line += input_pitch;
		output_line += output_pitch;
	}
}

#if 0
//TODO: Need to finish this routine
void ConvertPackedBayerToRGB24(PIXEL16U *input_buffer, FRAME_INFO *info,
							   int input_pitch, uint8_t *output_buffer,
							   int output_pitch, int width, int height)
{
	assert(0);

#if 0
	for(x=0; x<info->width; x++)
	{
		int R1,G1,B1;
		int rnd = noisearray[x&31];
		//	*ptr++ = *bayerptr++ >> 8;
		//	*ptr++ = 0x80;
		//	*ptr++ = *bayerptr++ >> 8;
		//	*ptr++ = 0x80;

		int r,g,b,g1,g2,gdiff,y1,y2,u,v;
		//g = (g1+g2)>>1;
		//*g_row_ptr++ = g;
		//*rg_row_ptr++ = (r-g+256)>>1;
		//*bg_row_ptr++ = (b-g+256)>>1;
		//*gdiff_row_ptr++ = (g1-g2+256)>>1;

		g = ((*G++)>>1);
		r = ((*RG++ + 64)>>0)-(256<<7)+g;
		b = ((*BG++ + 64)>>0)-(256<<7)+g;
		//	gdiff = ((*GD++ + 64)>>7)-256+g;

		if(matrix_non_unity)
		{
			//TODO: Need to convert to linear first

			R1 = ((r*r_rmult + g*r_gmult + b*r_bmult + r_offset)>>8) + rnd;
			G1 = ((r*g_rmult + g*g_gmult + b*g_bmult + g_offset)>>8) + rnd;
			B1 = ((r*b_rmult + g*b_gmult + b*b_bmult + b_offset)>>8) + rnd;

			//TODO: Need on convert back to log/display curve
		}
		else
		{
			R1 = r + rnd;
			G1 = g + rnd;
			B1 = b + rnd;
		}

		R1 >>= 7;
		G1 >>= 7;
		B1 >>= 7;

		if(R1 < 0) R1 = 0;
		if(R1 > 255) R1 = 255;
		if(G1 < 0) G1 = 0;
		if(G1 > 255) G1 = 255;
		if(B1 < 0) B1 = 0;
		if(B1 > 255) B1 = 255;


		*outyuv++ = B1;
		*outyuv++ = G1;
		*outyuv++ = R1;
	}
#endif
}
#endif

void ConvertPlanarBayerToRGB32(PIXEL16U *g1_plane, int g1_pitch,
							   PIXEL16U *rg_plane, int rg_pitch,
							   PIXEL16U *bg_plane, int bg_pitch,
							   PIXEL16U * g2_plane, int g2_pitch,
							   uint8_t *output_buffer, int output_pitch,
							   int width, int height)
{
	uint8_t *g1_row_ptr = (uint8_t *)g1_plane;
	uint8_t *rg_row_ptr = (uint8_t *)rg_plane;
	uint8_t *bg_row_ptr = (uint8_t *)bg_plane;
	//uint8_t *g2_row_ptr = (uint8_t *)g2_plane;

	uint8_t *output_row_ptr = output_buffer;

	int r_rmult = (int)(color_matrix[0][0] * scale);
	int r_gmult = (int)(color_matrix[0][1] * scale);
	int r_bmult = (int)(color_matrix[0][2] * scale);
	int r_offset= (int)(color_matrix[0][3] * scale);

	int g_rmult = (int)(color_matrix[1][0] * scale);
	int g_gmult = (int)(color_matrix[1][1] * scale);
	int g_bmult = (int)(color_matrix[1][2] * scale);
	int g_offset= (int)(color_matrix[1][3] * scale);

	int b_rmult = (int)(color_matrix[2][0] * scale);
	int b_gmult = (int)(color_matrix[2][1] * scale);
	int b_bmult = (int)(color_matrix[2][2] * scale);
	int b_offset= (int)(color_matrix[2][3] * scale);

	int matrix_non_unity = 0;
	const int alpha = 255;
	const int descale = 4;

#if 0
	if(decoder->cfhddata.MagicNumber == CFHDDATA_MAGIC_NUMBER && decoder->cfhddata.version >= 2)
	{
		float fval = 0.0;
		int i;
	}
#endif

	// The output frame is inverted
	output_row_ptr += (height - 1) * output_pitch;
	output_pitch = neg(output_pitch);

	{
		int row;
		int column;

		for (row = 0; row < height; row++)
		{
			PIXEL16U *g1_ptr = (PIXEL16U *)g1_row_ptr;
			PIXEL16U *rg_ptr = (PIXEL16U *)rg_row_ptr;
			PIXEL16U *bg_ptr = (PIXEL16U *)bg_row_ptr;
			//PIXEL16U *g2_ptr = (PIXEL16U *)g2_row_ptr;

			uint8_t *bgra_ptr = output_row_ptr;

			int i;

			// Array of random numbers for dithering the output pixels
			int noise_array[32];

			for (i = 0; i < countof(noise_array); i++)
			{
				// Need four bits of random dithering
				noise_array[i] = (rand() & 0x0F);
			}

			for (column = 0; column < width; column++)
			{
				int rnd1 = noise_array[(column + 0) % countof(noise_array)];
				//int rnd2 = noise_array[(column + 1) % countof(noise_array)];

				int r, g, b;
				//int g1, g2;
				int rg, bg;

				g = (*(g1_ptr++) + rnd1);
				if (g > 4095) g = 4095;

				rg = (*rg_ptr++);
				bg = (*bg_ptr++);

				r = (rg << 1) - (32768 >> 3) + g;
				b = (bg << 1) - (32768 >> 3) + g;

				if (matrix_non_unity)
				{
					int r1 = ((r_rmult * r + r_gmult * g + r_bmult * b + r_offset) >> 8);
					int g1 = ((g_rmult * r + g_gmult * g + g_bmult * b + g_offset) >> 8);
					int b1 = ((b_rmult * r + b_gmult * g + b_bmult * b + b_offset) >> 8);

					if (r1 < 0) r1 = 0; else if (r1 > USHRT_MAX) r1 = USHRT_MAX;
					if (g1 < 0) g1 = 0; else if (g1 > USHRT_MAX) g1 = USHRT_MAX;
					if (b1 < 0) b1 = 0; else if (b1 > USHRT_MAX) b1 = USHRT_MAX;

					r = r1;
					g = g1;
					b = b1;
				}

				r >>= descale;
				g >>= descale;
				b >>= descale;

				*(bgra_ptr++) = b;
				*(bgra_ptr++) = g;
				*(bgra_ptr++) = r;
				*(bgra_ptr++) = alpha;
			}

			g1_row_ptr += g1_pitch;
			rg_row_ptr += rg_pitch;
			bg_row_ptr += bg_pitch;
			//g2_row_ptr += g2_pitch;

			output_row_ptr += output_pitch;
		}
	}
}


void DrawBlankLUT(unsigned short *sptr, int width, int y, int lines)
{
	int offset = y*width;
	int i,X,Y,Z;
	unsigned short *slook_rgb48 = sptr;

	Z = offset & 63;
	offset >>= 6;
	Y = offset & 63;
	offset >>= 6;
	X = offset;


	if(X < 64)
	{
		for(i=0; i<width*lines; i++)
		{
			*slook_rgb48++ = Z*1040; //white = 65535
			*slook_rgb48++ = Y*1040;
			*slook_rgb48++ = X*1040;

			Z++;
			if(Z==64) Z=0, Y++;
			if(Y==64) Y=0, X++;
		}
	}
	else
	{
		for(i=0; i<width*lines; i++)
		{
			*slook_rgb48++ = 0;
			*slook_rgb48++ = 0;
			*slook_rgb48++ = 0;
		}
	}
/*
	for(x=0;x<64;x++) //b
	{
		for(y=0;y<64;y++) //g
		{
			for(z=0;z<64;z++) //r
			{
				*rgb48++ = z*1040; //white = 65535
				*rgb48++ = y*1040;
				*rgb48++ = x*1040;
			}
		}
	}
	*/
}


static const float rgb2yuv709[3][4] =
{
    {0.183f, 0.614f, 0.062f, 16.0f/255.0f},
    {-0.101f,-0.338f, 0.439f, 128.0f/255.0f},
    {0.439f,-0.399f,-0.040f, 128.0f/255.0f}
};
static const float rgb2yuv601[3][4] =
{
    {0.257f, 0.504f, 0.098f, 16.0f/255.0f},
    {-0.148f,-0.291f, 0.439f, 128.0f/255.0f},
    {0.439f,-0.368f,-0.071f, 128.0f/255.0f}
};
static const float rgb2yuvVS601[3][4] =
{
    {0.299f,0.587f,0.114f,0},
    {-0.172f,-0.339f,0.511f,128.0f/255.0f},
    {0.511f,-0.428f,-0.083f,128.0f/255.0f}
};
static const float rgb2yuvVS709[3][4] =
{
    {0.213f,0.715f,0.072f,0},
    {-0.117f,-0.394f,0.511f,128.0f/255.0f},
    {0.511f,-0.464f,-0.047f,128.0f/255.0f}
};


#define TEST_FONT	0
#if TEST_FONT
#include "test1234.h"
#endif

#define NEWDITHER	0
void ConvertLinesToOutput(DECODER *decoder, int width, int height, int linenum,
						    unsigned short *src, uint8_t *output, int pitch,
							int format, int whitepoint, int flags)
{
	//
	//	colorformatdone: TRUE = 3D LUT was used for color space conversion.  Only
	//								applies to YUV output formats
	//	planar: TRUE = row planar (YU16 for YUV)
	int x,lines;
	unsigned short *sptr = src;
	short *signed_sptr = (short *)src;
	//int white = (1<<whitepoint)-1;
	int dnshiftto8bit = whitepoint - 8;
	//int dnshiftto8bitmask = (1 << dnshiftto8bit) - 1;
	int dnshiftto10bit = whitepoint-10;
	int upshiftto16bit = 16-whitepoint;
	int dnshiftto13bit = whitepoint-13;
	int saturate = ((whitepoint < 16) && !(flags & ACTIVEMETADATA_PRESATURATED));
	int colorformatdone = (flags & ACTIVEMETADATA_COLORFORMATDONE);
	uint8_t *outA8 = output;
	int colorspace = decoder->frame.colorspace;
	int y_rmult,u_rmult,v_rmult;
	int y_gmult,u_gmult,v_gmult;
	int y_bmult,u_bmult,v_bmult;
	float rgb2yuv[3][4];
	int rgb2yuv_i[3][4];
	int yoffset = 16;
	int row;
	int cg2vs = 0;
#if NEWDITHER
	int seed = rand();
	//dnshiftto8bitmask >>= 1; //50% Dither
	//dnshiftto8bitmask = 0; //0% Dither
#endif

	if(!colorformatdone && LUTYUV(format))
	{
		switch(colorspace & COLORSPACE_MASK)
		{
		case COLOR_SPACE_CG_601:
			if(whitepoint == 16 || decoder->broadcastLimit)
			{
				memcpy(rgb2yuv, rgb2yuv601, 12*sizeof(float));
			}
			else
			{
				cg2vs = 1;
				memcpy(rgb2yuv, rgb2yuvVS601, 12*sizeof(float));
			}
			break;
		default: //assert(0);
		case COLOR_SPACE_CG_709:			
			if(whitepoint == 16 || decoder->broadcastLimit)
			{
				memcpy(rgb2yuv, rgb2yuv709, 12*sizeof(float));
			}
			else
			{
				cg2vs = 1;
				memcpy(rgb2yuv, rgb2yuvVS709, 12*sizeof(float));
			}
			break;
		case COLOR_SPACE_VS_601:
			memcpy(rgb2yuv, rgb2yuvVS601, 12*sizeof(float));
			break;
		case COLOR_SPACE_VS_709:
			memcpy(rgb2yuv, rgb2yuvVS709, 12*sizeof(float));
			break;
		}
		y_rmult = rgb2yuv_i[0][0] = (int)(rgb2yuv[0][0] * 32768.0f);
		y_gmult = rgb2yuv_i[0][1] = (int)(rgb2yuv[0][1] * 32768.0f);
		y_bmult = rgb2yuv_i[0][2] = (int)(rgb2yuv[0][2] * 32768.0f);
		u_rmult = rgb2yuv_i[1][0] = (int)(rgb2yuv[1][0] * 32768.0f);
		u_gmult = rgb2yuv_i[1][1] = (int)(rgb2yuv[1][1] * 32768.0f);
		u_bmult = rgb2yuv_i[1][2] = (int)(rgb2yuv[1][2] * 32768.0f);
		v_rmult = rgb2yuv_i[2][0] = (int)(rgb2yuv[2][0] * 32768.0f);
		v_gmult = rgb2yuv_i[2][1] = (int)(rgb2yuv[2][1] * 32768.0f);
		v_bmult = rgb2yuv_i[2][2] = (int)(rgb2yuv[2][2] * 32768.0f);

		if(rgb2yuv[0][3] == 0.0)
			yoffset = 0;
	}



	switch(format & 0x7ffffff)
	{
		case COLOR_FORMAT_RGB24:
			if(saturate && whitepoint<16 && !(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR))
			{
				int totalpixel = width * 3;
//				int totalpixel15 = totalpixel - (totalpixel % 15) - 15;
				
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;

					x=0;
/*					for(; x<totalpixel15; x+=15)
					{
						__m128i rgbrgbr1;
						__m128i rgbrgbr2;
						uint8_t b;

						rgbrgbr1 = _mm_loadu_si128((__m128i *)signed_sptr); signed_sptr+=8;
						rgbrgbr1 = _mm_adds_epi16(rgbrgbr1, overflowprotectRGB_epi16);
						rgbrgbr1 = _mm_subs_epu16(rgbrgbr1, overflowprotectRGB_epi16);
						rgbrgbr1 = _mm_srli_epi16(rgbrgbr1, dnshiftto8bit);

						rgbrgbr2 = _mm_loadu_si128((__m128i *)signed_sptr); signed_sptr+=7;
						rgbrgbr2 = _mm_adds_epi16(rgbrgbr2, overflowprotectRGB_epi16);
						rgbrgbr2 = _mm_subs_epu16(rgbrgbr2, overflowprotectRGB_epi16);
						rgbrgbr2 = _mm_srli_epi16(rgbrgbr2, dnshiftto8bit);

						rgbrgbr1 = _mm_packus_epi16(rgbrgbr1, rgbrgbr2);

						_mm_storeu_si128((__m128i *)outA8, rgbrgbr1);


						b = outA8[2];
						outA8[2] = outA8[0];//r
						outA8[0] = b;

						b = outA8[5];
						outA8[5] = outA8[3];//r
						outA8[3] = b;

						b = outA8[8];
						outA8[8] = outA8[6];//r
						outA8[6] = b;

						b = outA8[11];
						outA8[11] = outA8[9];//r
						outA8[9] = b;

						b = outA8[14];
						outA8[14] = outA8[12];//r
						outA8[12] = b;

						outA8+=15;
					}
*/
					for(; x<totalpixel; x+=3)
					{
#if NEWDITHER
						int dither = (seed >> 16) & dnshiftto8bitmask;
						int r = (signed_sptr[0] + dither) >> dnshiftto8bit;
						int g = (signed_sptr[1] + dither) >> dnshiftto8bit;
						int b = (signed_sptr[2] + dither) >> dnshiftto8bit;
#else
						int r = (signed_sptr[0]) >> dnshiftto8bit;
						int g = (signed_sptr[1]) >> dnshiftto8bit;
						int b = (signed_sptr[2]) >> dnshiftto8bit;
#endif

						signed_sptr+=3;

						if(r>255) r=255; if(r<0) r=0;
						if(g>255) g=255; if(g<0) g=0;
						if(b>255) b=255; if(b<0) b=0;

						outA8[2] = r;
						outA8[1] = g;
						outA8[0] = b;
						outA8+=3;

#if NEWDITHER
						seed = (214013 * seed + 2531011);
#endif
					}
					output += pitch;
				}
			}
			else
			{
				if(saturate && whitepoint<16)
				{
					for(lines=0; lines<height; lines++)
					{
						outA8 = output;
						if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
						{
							for(x=0; x<width; x+=8)
							{
								int xx;
								for(xx=0;xx<8; xx++)
								{
									int ri, gi, bi;
#if NEWDITHER
									int dither = (seed >> 16) & dnshiftto8bitmask;
									ri = (signed_sptr[0] + dither) >> dnshiftto8bit;
									gi = (signed_sptr[8] + dither) >> dnshiftto8bit;
									bi = (signed_sptr[16] + dither) >> dnshiftto8bit;
#else
									ri = (signed_sptr[0]) >> dnshiftto8bit;
									gi = (signed_sptr[8]) >> dnshiftto8bit;
									bi = (signed_sptr[16]) >> dnshiftto8bit;
#endif

									signed_sptr++;

									if(ri>255) ri=255; if(ri<0) ri=0;
									if(gi>255) gi=255; if(gi<0) gi=0;
									if(bi>255) bi=255; if(bi<0) bi=0;

									outA8[2] = ri;
									outA8[1] = gi;
									outA8[0] = bi;
									outA8+=3;

#if NEWDITHER
									seed = (214013 * seed + 2531011);
#endif

								}
								signed_sptr += 16;
							}
						}
						else if(flags & ACTIVEMETADATA_PLANAR)
						{
							for(x=0; x<width; x++)
							{
								int ri, gi, bi;
#if NEWDITHER
								int dither = (seed >> 16) & dnshiftto8bitmask;
								ri = (signed_sptr[0] + dither) >> dnshiftto8bit;
								gi = (signed_sptr[width] + dither) >> dnshiftto8bit;
								bi = (signed_sptr[width * 2] + dither) >> dnshiftto8bit;
#else
								ri = (signed_sptr[0]) >> dnshiftto8bit;
								gi = (signed_sptr[width]) >> dnshiftto8bit;
								bi = (signed_sptr[width * 2]) >> dnshiftto8bit;
#endif

								signed_sptr++;

								if(ri>255) ri=255; if(ri<0) ri=0;
								if(gi>255) gi=255; if(gi<0) gi=0;
								if(bi>255) bi=255; if(bi<0) bi=0;

								outA8[2] = ri;
								outA8[1] = gi;
								outA8[0] = bi;
								outA8 += 3;

#if NEWDITHER
								seed = (214013 * seed + 2531011);
#endif

							}
						}
						else
						{
							for(x=0; x<width; x++)
							{
								int ri, gi, bi;
#if NEWDITHER
								int dither = (seed >> 16) & dnshiftto8bitmask;
								ri = (signed_sptr[0] + dither) >> dnshiftto8bit;
								gi = (signed_sptr[1] + dither) >> dnshiftto8bit;
								bi = (signed_sptr[2] + dither) >> dnshiftto8bit;
#else
								ri = (signed_sptr[0]) >> dnshiftto8bit;
								gi = (signed_sptr[1]) >> dnshiftto8bit;
								bi = (signed_sptr[2]) >> dnshiftto8bit;
#endif

								signed_sptr+=3;
								
								if(ri>255) ri=255; if(ri<0) ri=0;
								if(gi>255) gi=255; if(gi<0) gi=0;
								if(bi>255) bi=255; if(bi<0) bi=0;

								outA8[2] = ri;
								outA8[1] = gi;
								outA8[0] = bi;
								outA8 += 3;

#if NEWDITHER
								seed = (214013 * seed + 2531011);
#endif

							}
						}
						output += pitch;
					}
				}
				else
				{
					for(lines=0; lines<height; lines++)
					{
						outA8 = output;
						if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
						{
							for(x=0; x<width; x+=8)
							{
								int xx;
								for(xx=0;xx<8; xx++)
								{
									int ri,gi,bi;
									ri = sptr[0]>>dnshiftto8bit;
									gi = sptr[8]>>dnshiftto8bit;
									bi = sptr[16]>>dnshiftto8bit;
									sptr++;

									outA8[2] = ri;
									outA8[1] = gi;
									outA8[0] = bi;
									outA8+=3;
								}
								sptr += 16;
							}
						}
						else if(flags & ACTIVEMETADATA_PLANAR)
						{
							for(x=0; x<width; x++)
							{
								int ri,gi,bi;
								ri = sptr[0]>>dnshiftto8bit;
								gi = sptr[width]>>dnshiftto8bit;
								bi = sptr[width*2]>>dnshiftto8bit;
								sptr++;

								outA8[2] = ri;
								outA8[1] = gi;
								outA8[0] = bi;
								outA8+=3;
							}
						}
						else
						{
							for(x=0; x<width; x++)
							{
								//TODO SSE2
								outA8[2] = sptr[0]>>dnshiftto8bit;
								outA8[1] = sptr[1]>>dnshiftto8bit;
								outA8[0] = sptr[2]>>dnshiftto8bit;
								outA8+=3;
								sptr+=3;
							}
						}
						output += pitch;
					}
				}
			}
			break;

		case COLOR_FORMAT_RGB32:
			if(saturate && whitepoint<16)
			{
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								int ri, gi, bi;
#if NEWDITHER
								int dither = (seed >> 16) & dnshiftto8bitmask;
								ri = (signed_sptr[0] + dither) >> dnshiftto8bit;
								gi = (signed_sptr[8] + dither) >> dnshiftto8bit;
								bi = (signed_sptr[16] + dither) >> dnshiftto8bit;
#else
								ri = (signed_sptr[0]) >> dnshiftto8bit;
								gi = (signed_sptr[8]) >> dnshiftto8bit;
								bi = (signed_sptr[16]) >> dnshiftto8bit;
#endif

								signed_sptr++;

								if(ri>255) ri=255; if(ri<0) ri=0;
								if(gi>255) gi=255; if(gi<0) gi=0;
								if(bi>255) bi=255; if(bi<0) bi=0;

								outA8[3] = 0xff;
								outA8[2] = ri;
								outA8[1] = gi;
								outA8[0] = bi;
								outA8 += 4;

#if NEWDITHER
								seed = (214013 * seed + 2531011);
#endif

							}
							signed_sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							int ri, gi, bi;
#if NEWDITHER
							int dither = (seed >> 16) & dnshiftto8bitmask;
							ri = (signed_sptr[0] + dither) >> dnshiftto8bit;
							gi = (signed_sptr[width] + dither) >> dnshiftto8bit;
							bi = (signed_sptr[width * 2] + dither) >> dnshiftto8bit;
#else
							ri = (signed_sptr[0]) >> dnshiftto8bit;
							gi = (signed_sptr[width]) >> dnshiftto8bit;
							bi = (signed_sptr[width * 2]) >> dnshiftto8bit;
#endif

							signed_sptr++;

							if(ri>255) ri=255; if(ri<0) ri=0;
							if(gi>255) gi=255; if(gi<0) gi=0;
							if(bi>255) bi=255; if(bi<0) bi=0;

							outA8[3] = 0xff;
							outA8[2] = ri;
							outA8[1] = gi;
							outA8[0] = bi;
							outA8 += 4;

#if NEWDITHER
							seed = (214013 * seed + 2531011);
#endif

						}
					}
					else
					{
						for(x=0; x<width; x++)
						{
#if NEWDITHER
							int dither = (seed >> 16) & dnshiftto8bitmask;
							int r = (signed_sptr[0] + dither) >> dnshiftto8bit;
							int g = (signed_sptr[1] + dither) >> dnshiftto8bit;
							int b = (signed_sptr[2] + dither) >> dnshiftto8bit;
#else
							int r = (signed_sptr[0]) >> dnshiftto8bit;
							int g = (signed_sptr[1]) >> dnshiftto8bit;
							int b = (signed_sptr[2]) >> dnshiftto8bit;
#endif

							signed_sptr+=3;

							if(r>255) r=255; if(r<0) r=0;
							if(g>255) g=255; if(g<0) g=0;
							if(b>255) b=255; if(b<0) b=0;

							outA8[3] = 0xff;
							outA8[2] = r;
							outA8[1] = g;
							outA8[0] = b;
							outA8+=4;

#if NEWDITHER
							seed = (214013 * seed + 2531011);
#endif

						}
					}
					output += pitch;
				}
			}
			else
			{
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								int ri,gi,bi;
								ri = sptr[0]>>dnshiftto8bit;
								gi = sptr[8]>>dnshiftto8bit;
								bi = sptr[16]>>dnshiftto8bit;
								sptr++;

								outA8[3] = 0xff;
								outA8[2] = ri;
								outA8[1] = gi;
								outA8[0] = bi;
								outA8+=4;
							}
							sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							int ri,gi,bi;
							ri = sptr[0]>>dnshiftto8bit;
							gi = sptr[width]>>dnshiftto8bit;
							bi = sptr[width*2]>>dnshiftto8bit;
							sptr++;

							outA8[3] = 0xff;
							outA8[2] = ri;
							outA8[1] = gi;
							outA8[0] = bi;
							outA8+=4;
						}
					}
					else
					{
						//int rounding = (1<<dnshiftto8bit)>>1;
						for(x=0; x<width; x++)
						{
							outA8[3] = 0xff;
							outA8[2] = (sptr[0]/*+rounding*/)>>dnshiftto8bit;
							outA8[1] = (sptr[1]/*+rounding*/)>>dnshiftto8bit;
							outA8[0] = (sptr[2]/*+rounding*/)>>dnshiftto8bit;
							outA8+=4;
							sptr+=3;
						}
					}
					output += pitch;
				}
			}
			break;

		case COLOR_FORMAT_B64A: //TODO SSe2
			if(whitepoint != 16 && whitepoint !=0 )
			{
				//int totalpixel = width * 4;
				//int totalpixel8 = totalpixel & 0xfff8;
				short *outA16;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (short *)outA8;

					x=0;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						//int shift = whitepoint - decoder->frame.white_point;

						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{								
								int r = signed_sptr[0]<<upshiftto16bit;
								int g = signed_sptr[8]<<upshiftto16bit;
								int b = signed_sptr[16]<<upshiftto16bit;
								if(r>65535) r=65535; if(r<0) r=0;
								if(g>65535) g=65535; if(g<0) g=0;
								if(b>65535) b=65535; if(b<0) b=0;

								outA16[0] = 0xffff;
								outA16[1] = r;
								outA16[2] = g;
								outA16[3] = b;
								signed_sptr++;
								outA16+=4;
							}
							signed_sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						//int shift = whitepoint - decoder->frame.white_point;

						for(x=0; x<width; x++)
						{							
							int r = signed_sptr[0]<<upshiftto16bit;
							int g = signed_sptr[width]<<upshiftto16bit;
							int b = signed_sptr[width*2]<<upshiftto16bit;
							if(r>65535) r=65535; if(r<0) r=0;
							if(g>65535) g=65535; if(g<0) g=0;
							if(b>65535) b=65535; if(b<0) b=0;

							outA16[0] = 0xffff;
							outA16[1] = r;
							outA16[2] = g;
							outA16[3] = b;

							signed_sptr++;
							outA16+=4;
						}
					}
					else
					{
						for(x=0;x<width; x++)
						{								
							int r = signed_sptr[0]<<upshiftto16bit;
							int g = signed_sptr[1]<<upshiftto16bit;
							int b = signed_sptr[2]<<upshiftto16bit;
							if(r>65535) r=65535; if(r<0) r=0;
							if(g>65535) g=65535; if(g<0) g=0;
							if(b>65535) b=65535; if(b<0) b=0;

							outA16[0] = 0xffff;
							outA16[1] = r;
							outA16[2] = g;
							outA16[3] = b;

							signed_sptr += 3;
							outA16+=4;
						}
					}
					output += pitch;
				}
			}
			else
			{
				unsigned short *outA16;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (unsigned short *)outA8;


					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								outA16[0] = 0xffff;
								outA16[1] = sptr[0];
								outA16[2] = sptr[8];
								outA16[3] = sptr[16];
								sptr++;
								outA16+=4;
							}
							sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							outA16[0] = 0xffff;
							outA16[1] = sptr[0];
							outA16[2] = sptr[width];
							outA16[3] = sptr[width*2];
							sptr++;
							outA16+=4;
						}
					}
					else
					{
						for(x=0; x<width; x++)
						{
							outA16[0] = 0xffff;
							outA16[1] = sptr[0];
							outA16[2] = sptr[1];
							outA16[3] = sptr[2];
							outA16+=4;
							sptr+=3;
						}
					}
					output += pitch;
				}
			}
			break;

		case COLOR_FORMAT_W13A: //TODO need own stuff
			if(whitepoint < 16)// assume white point is (1<13-1) && decoder->frame.white_point
			{
				//int totalpixel = width * 4;
				//int totalpixel8 = totalpixel & 0xfff8;
				short *outA16;
				int shift = whitepoint - decoder->frame.white_point;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (short *)outA8;

					x=0;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{

						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								outA16[0] = signed_sptr[0]>>shift;
								outA16[1] = signed_sptr[8]>>shift;
								outA16[2] = signed_sptr[16]>>shift;
								outA16[3] = 0x1fff;
								signed_sptr++;
								outA16+=4;
							}
							signed_sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						int shift = whitepoint - decoder->frame.white_point;

						for(x=0; x<width; x++)
						{
							outA16[0] = signed_sptr[0]>>shift;
							outA16[1] = signed_sptr[width]>>shift;
							outA16[2] = signed_sptr[width*2]>>shift;
							outA16[3] = 0x1fff;
							signed_sptr++;
							outA16+=4;
						}
					}
					else
					{
						if(decoder->frame.white_point == whitepoint)
						{
							for(x=0;x<width; x++)
							{
								outA16[0] = *signed_sptr++;
								outA16[1] = *signed_sptr++;
								outA16[2] = *signed_sptr++;
								outA16[3] = 0x1fff;

								outA16+=4;
							}
						}
						else
						{
							int shift = whitepoint - decoder->frame.white_point;

							for(x=0;x<width; x++)
							{
								outA16[0] = *signed_sptr++>>shift;
								outA16[1] = *signed_sptr++>>shift;
								outA16[2] = *signed_sptr++>>shift;
								outA16[3] = 0x1fff;

								outA16+=4;
							}
						}
					}
					output += pitch;
				}
			}
			else// if(whitepoint == 16)
			{
				//int totalpixel = width * 4;
				//int totalpixel8 = totalpixel & 0xfff8;
				short *outA16;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (short *)outA8;

					x=0;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{

						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								outA16[0] = sptr[0]>>dnshiftto13bit;
								outA16[1] = sptr[8]>>dnshiftto13bit;
								outA16[2] = sptr[16]>>dnshiftto13bit;
								outA16[3] = 0x1fff;
								sptr++;
								outA16+=4;
							}
							sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							outA16[0] = sptr[0]>>dnshiftto13bit;
							outA16[1] = sptr[width]>>dnshiftto13bit;
							outA16[2] = sptr[width*2]>>dnshiftto13bit;
							outA16[3] = 0x1fff;
							sptr++;
							outA16+=4;
						}
					}
					else
					{
						for(x=0;x<width*3; x+=3)
						{
							outA16[0] = sptr[x+0]>>dnshiftto13bit;
							outA16[1] = sptr[x+1]>>dnshiftto13bit;
							outA16[2] = sptr[x+2]>>dnshiftto13bit;
							outA16[3] = 0x1fff;

							outA16+=4;
						}
					}
					output += pitch;
				}
			}
			break;

		case COLOR_FORMAT_RG48:
			if(whitepoint != 16 && whitepoint !=0 )
			{
				//int totalpixel = width * 4;
				//int totalpixel8 = totalpixel & 0xfff8;
				short *outA16;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (short *)outA8;

					x=0;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						//int shift = whitepoint - decoder->frame.white_point;

						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{								
								int r = signed_sptr[0]<<upshiftto16bit;
								int g = signed_sptr[8]<<upshiftto16bit;
								int b = signed_sptr[16]<<upshiftto16bit;
								if(r>65535) r=65535; if(r<0) r=0;
								if(g>65535) g=65535; if(g<0) g=0;
								if(b>65535) b=65535; if(b<0) b=0;

								outA16[0] = r;
								outA16[1] = g;
								outA16[2] = b;
								signed_sptr++;
								outA16+=3;
							}
							signed_sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						//int shift = whitepoint - decoder->frame.white_point;

						for(x=0; x<width; x++)
						{							
							int r = signed_sptr[0]<<upshiftto16bit;
							int g = signed_sptr[width]<<upshiftto16bit;
							int b = signed_sptr[width*2]<<upshiftto16bit;
							if(r>65535) r=65535; if(r<0) r=0;
							if(g>65535) g=65535; if(g<0) g=0;
							if(b>65535) b=65535; if(b<0) b=0;

							outA16[0] = r;
							outA16[1] = g;
							outA16[2] = b;

							signed_sptr++;
							outA16+=3;
						}
					}
					else
					{
						for(x=0;x<width; x++)
						{								
							int r = signed_sptr[0]<<upshiftto16bit;
							int g = signed_sptr[1]<<upshiftto16bit;
							int b = signed_sptr[2]<<upshiftto16bit;
							if(r>65535) r=65535; if(r<0) r=0;
							if(g>65535) g=65535; if(g<0) g=0;
							if(b>65535) b=65535; if(b<0) b=0;

							outA16[0] = r;
							outA16[1] = g;
							outA16[2] = b;

							signed_sptr += 3;
							outA16+=3;
						}
					}
					output += pitch;
				}
			}
			else
			{
				unsigned short *outA16;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (unsigned short *)outA8;


					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								outA16[0] = sptr[0];
								outA16[1] = sptr[8];
								outA16[2] = sptr[16];
								sptr++;
								outA16+=3;
							}
							sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							outA16[0] = sptr[0];
							outA16[1] = sptr[width];
							outA16[2] = sptr[width*2];
							sptr++;
							outA16+=3;
						}
					}
					else
					{
						for(x=0; x<width; x++)
						{
							outA16[0] = sptr[0];
							outA16[1] = sptr[1];
							outA16[2] = sptr[2];
							outA16+=3;
							sptr+=3;
						}
					}
					output += pitch;
				}
			}
			break;

		case COLOR_FORMAT_WP13: //TODO need own stuff
			if(whitepoint < 16) // assume white point is (1<13-1) && decoder->frame.white_point
			{
				int totalpixel = width * 3;
				int totalpixel8 = totalpixel & 0xfff8;
				short *outA16;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (short *)outA8;

					x=0;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						int shift = whitepoint - decoder->frame.white_point;

						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								outA16[0] = sptr[0]>>shift;
								outA16[1] = sptr[8]>>shift;
								outA16[2] = sptr[16]>>shift;
								sptr++;
								outA16+=3;
							}
							sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						int shift = whitepoint - decoder->frame.white_point;

						for(x=0; x<width; x++)
						{
							outA16[0] = sptr[0]>>shift;
							outA16[1] = sptr[width]>>shift;
							outA16[2] = sptr[width*2]>>shift;
							sptr++;
							outA16+=3;
						}
					}
					else
					{/*    3D Fix for P3D CS3?
						if(format == COLOR_FORMAT_RG48 && decoder->frame.white_point == 13 && whitepoint == 13)
						{
							__m128i overflowprotectRGB_epi16 = _mm_set1_epi16(0x7fff-((1<<(15-(upshiftto16bit-1)))-1));
							
							for(; x<totalpixel8; x+=8)
							{
								__m128i rgbrgbrg = _mm_loadu_si128((__m128i *)sptr); sptr+=8;

								//limit 0 to white point
								rgbrgbrg = _mm_adds_epi16(rgbrgbrg, overflowprotectRGB_epi16);
								rgbrgbrg = _mm_subs_epu16(rgbrgbrg, overflowprotectRGB_epi16);

								rgbrgbrg = _mm_slli_epi16(rgbrgbrg, upshiftto16bit);
								_mm_storeu_si128((__m128i *)outA16, rgbrgbrg); outA16+=8;
							}
							for(; x<totalpixel; x++)
							{
								int value = *sptr++<<upshiftto16bit;
								if(value < 0) value = 0;
								if(value > 65535) value = 65535;
								*outA16++ = value;
							}
						}
						else	*/
						if(decoder->frame.white_point == whitepoint)
						{
							for(; x<totalpixel8; x+=8)
							{
								__m128i rgbrgbrg = _mm_loadu_si128((__m128i *)signed_sptr); signed_sptr+=8;
								_mm_storeu_si128((__m128i *)outA16, rgbrgbrg); outA16+=8;
							}

							for(; x<totalpixel; x++)
							{
								*outA16++ = *signed_sptr++;
							}
						}
						else
						{
							int shift = whitepoint - decoder->frame.white_point;
							for(; x<totalpixel8; x+=8)
							{
								__m128i rgbrgbrg = _mm_loadu_si128((__m128i *)sptr); sptr+=8;
								rgbrgbrg = _mm_srli_epi16(rgbrgbrg, shift);
								_mm_storeu_si128((__m128i *)outA16, rgbrgbrg); outA16+=8;
							}

							for(; x<totalpixel; x++)
							{
								*outA16++ = *sptr++>>shift;
							}
						}
					}
					output += pitch;
				}
			}
			else //16-bit unsigned shift to 13-bit
			{
				int totalpixel = width * 3;
				int totalpixel8 = totalpixel & 0xfff8;
				unsigned short *outA16;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (unsigned short *)outA8;

					x=0;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
#if 1
						int width8 = (width>>3)*8;
						for(x=0; x<width8*3; x+=8*3)
						{
							__m128i rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[0]);
							__m128i gggggggg = _mm_loadu_si128((__m128i *)&sptr[8]);
							__m128i bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[16]);

							rrrrrrrr = _mm_srli_epi16(rrrrrrrr, dnshiftto13bit);
							gggggggg = _mm_srli_epi16(gggggggg, dnshiftto13bit);
							bbbbbbbb = _mm_srli_epi16(bbbbbbbb, dnshiftto13bit);

							outA16[0]  = _mm_extract_epi16(rrrrrrrr, 0);
							outA16[1]  = _mm_extract_epi16(gggggggg, 0);
							outA16[2]  = _mm_extract_epi16(bbbbbbbb, 0);
							outA16[3]  = _mm_extract_epi16(rrrrrrrr, 1);
							outA16[4]  = _mm_extract_epi16(gggggggg, 1);
							outA16[5]  = _mm_extract_epi16(bbbbbbbb, 1);
							outA16[6]  = _mm_extract_epi16(rrrrrrrr, 2);
							outA16[7]  = _mm_extract_epi16(gggggggg, 2);
							outA16[8]  = _mm_extract_epi16(bbbbbbbb, 2);
							outA16[9]  = _mm_extract_epi16(rrrrrrrr, 3);
							outA16[10]  = _mm_extract_epi16(gggggggg, 3);
							outA16[11]  = _mm_extract_epi16(bbbbbbbb, 3);
							outA16[12]  = _mm_extract_epi16(rrrrrrrr, 4);
							outA16[13]  = _mm_extract_epi16(gggggggg, 4);
							outA16[14]  = _mm_extract_epi16(bbbbbbbb, 4);
							outA16[15]  = _mm_extract_epi16(rrrrrrrr, 5);
							outA16[16]  = _mm_extract_epi16(gggggggg, 5);
							outA16[17]  = _mm_extract_epi16(bbbbbbbb, 5);
							outA16[18]  = _mm_extract_epi16(rrrrrrrr, 6);
							outA16[19]  = _mm_extract_epi16(gggggggg, 6);
							outA16[20]  = _mm_extract_epi16(bbbbbbbb, 6);
							outA16[21]  = _mm_extract_epi16(rrrrrrrr, 7);
							outA16[22]  = _mm_extract_epi16(gggggggg, 7);
							outA16[23]  = _mm_extract_epi16(bbbbbbbb, 7);

							outA16+=24;
							sptr += 24;
						}
#else
						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								outA16[0] = sptr[0]>>dnshiftto13bit;
								outA16[1] = sptr[8]>>dnshiftto13bit;
								outA16[2] = sptr[16]>>dnshiftto13bit;
								sptr++;
								outA16+=3;
							}

							sptr += 16;
						}
#endif
					}
					else 
					if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							outA16[0] = sptr[0]>>dnshiftto13bit;
							outA16[1] = sptr[width]>>dnshiftto13bit;
							outA16[2] = sptr[width*2]>>dnshiftto13bit;
							sptr++;
							outA16+=3;
						}
					}
					else
					{
						for(; x<totalpixel8; x+=8)
						{
							__m128i rgbrgbrg = _mm_loadu_si128((__m128i *)sptr); sptr+=8;
							rgbrgbrg = _mm_srli_epi16(rgbrgbrg, dnshiftto13bit);
							_mm_storeu_si128((__m128i *)outA16, rgbrgbrg); outA16+=8;
						}

						for(; x<totalpixel; x++)
						{
							*outA16++ = *sptr++>>dnshiftto13bit;
						}
					}
					output += pitch;
				}
			}
			break;

		case COLOR_FORMAT_RG64:
			if(whitepoint < 16)// assume white point is (1<13-1) && decoder->frame.white_point
			{
				int totalpixel = width;
				short *outA16;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (short *)outA8;

					x=0;
					if(decoder->frame.white_point == whitepoint)
					{
						for(; x<totalpixel; x++)
						{
							*outA16++ = *signed_sptr++;
							*outA16++ = *signed_sptr++;
							*outA16++ = *signed_sptr++;
							*outA16++ = (1<<decoder->frame.white_point)-1;
						}
					}
					else
					{
						int shift = whitepoint - decoder->frame.white_point;

						for(; x<totalpixel; x++)
						{
							*outA16++ = *sptr++>>shift;
							*outA16++ = *sptr++>>shift;
							*outA16++ = *sptr++>>shift;
							*outA16++ = (1<<decoder->frame.white_point)-1;
						}
					}

					output += pitch;
				}
			}
			else
			if(saturate && upshiftto16bit)
			{
				int totalpixel = width * 4;
				int totalpixel8 = totalpixel & 0xfff8;
				unsigned short *outA16;
				for(lines=0; lines<height; lines++)
				{
					__m128i overflowprotectRGB_epi16 = _mm_set1_epi16(0x7fff-((1<<(15-(upshiftto16bit-1)))-1));

					outA8 = output;
					outA16 = (unsigned short *)outA8;

					x=0;
					for(; x<totalpixel8; x+=8)
					{
						__m128i rgbrgbrg = _mm_loadu_si128((__m128i *)sptr); sptr+=8;

						//limit 0 to white point
						rgbrgbrg = _mm_adds_epi16(rgbrgbrg, overflowprotectRGB_epi16);
						rgbrgbrg = _mm_subs_epu16(rgbrgbrg, overflowprotectRGB_epi16);

						rgbrgbrg = _mm_slli_epi16(rgbrgbrg, upshiftto16bit);		// 0 to 65535
						_mm_storeu_si128((__m128i *)outA16, rgbrgbrg); outA16+=8;
					}

					for(; x<totalpixel; x++)
					{
						int rgb = *sptr++<<upshiftto16bit;
						if(rgb>65535) rgb=65535; if(rgb<0) rgb=0;
						 *outA16++ = rgb;
					}
					output += pitch;
				}
			}
			else
			{
				int totalpixel = width * 4;
				int totalpixel8 = totalpixel & 0xfff8;
				unsigned short *outA16;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (unsigned short *)outA8;

					x=0;
					for(; x<totalpixel8; x+=8)
					{
						__m128i rgbrgbrg = _mm_loadu_si128((__m128i *)sptr); sptr+=8;
						rgbrgbrg = _mm_slli_epi16(rgbrgbrg, upshiftto16bit);
						_mm_storeu_si128((__m128i *)outA16, rgbrgbrg); outA16+=8;
					}

					for(; x<totalpixel; x++)
					{
						*outA16++ = *sptr++<<upshiftto16bit;
						if(whitepoint < 16)// assume white point is (1<13-1) && decoder->frame.white_point
						{
							int totalpixel = width * 3;
							int totalpixel8 = totalpixel & 0xfff8;
							short *outA16;
							for(lines=0; lines<height; lines++)
							{
								outA8 = output;
								outA16 = (short *)outA8;

								x=0;
								if(decoder->frame.white_point == whitepoint)
								{
									for(; x<totalpixel8; x+=8)
									{
										__m128i rgbrgbrg = _mm_loadu_si128((__m128i *)signed_sptr); signed_sptr+=8;
										_mm_storeu_si128((__m128i *)outA16, rgbrgbrg); outA16+=8;
									}

									for(; x<totalpixel; x++)
									{
										*outA16++ = *signed_sptr++;
									}
								}
								else
								{
									int shift = whitepoint - decoder->frame.white_point;
									for(; x<totalpixel8; x+=8)
									{
										__m128i rgbrgbrg = _mm_loadu_si128((__m128i *)sptr); sptr+=8;
										rgbrgbrg = _mm_srli_epi16(rgbrgbrg, shift);
										_mm_storeu_si128((__m128i *)outA16, rgbrgbrg); outA16+=8;
									}

									for(; x<totalpixel; x++)
									{
										*outA16++ = *sptr++>>shift;
									}
								}

								output += pitch;
							}
						}
						else
						if(saturate && upshiftto16bit)
						{
							int totalpixel = width * 3;
							int totalpixel8 = totalpixel & 0xfff8;
							unsigned short *outA16;
							for(lines=0; lines<height; lines++)
							{
								__m128i overflowprotectRGB_epi16 = _mm_set1_epi16(0x7fff-((1<<(15-(upshiftto16bit-1)))-1));

								outA8 = output;
								outA16 = (unsigned short *)outA8;

								x=0;
								for(; x<totalpixel8; x+=8)
								{
									__m128i rgbrgbrg = _mm_loadu_si128((__m128i *)sptr); sptr+=8;

									//limit 0 to white point
									rgbrgbrg = _mm_adds_epi16(rgbrgbrg, overflowprotectRGB_epi16);
									rgbrgbrg = _mm_subs_epu16(rgbrgbrg, overflowprotectRGB_epi16);

									rgbrgbrg = _mm_slli_epi16(rgbrgbrg, upshiftto16bit);		// 0 to 65535
									_mm_storeu_si128((__m128i *)outA16, rgbrgbrg); outA16+=8;
								}

								for(; x<totalpixel; x++)
								{
									int rgb = *sptr++<<upshiftto16bit;
									if(rgb>65535) rgb=65535; if(rgb<0) rgb=0;
									 *outA16++ = rgb;
								}
								output += pitch;
							}
						}
						else
						{
							int totalpixel = width * 3;
							int totalpixel8 = totalpixel & 0xfff8;
							unsigned short *outA16;
							for(lines=0; lines<height; lines++)
							{
								outA8 = output;
								outA16 = (unsigned short *)outA8;

								x=0;
								for(; x<totalpixel8; x+=8)
								{
									__m128i rgbrgbrg = _mm_loadu_si128((__m128i *)sptr); sptr+=8;
									rgbrgbrg = _mm_slli_epi16(rgbrgbrg, upshiftto16bit);
									_mm_storeu_si128((__m128i *)outA16, rgbrgbrg); outA16+=8;
								}

								for(; x<totalpixel; x++)
								{
									*outA16++ = *sptr++<<upshiftto16bit;
								}
								output += pitch;
							}
						}
						break;
					}
					output += pitch;
				}
			}
			break;


		case COLOR_FORMAT_AB10:
		case COLOR_FORMAT_AR10:
		case COLOR_FORMAT_RG30:
			if(saturate)
			{
				uint32_t *outA32;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA32 = (uint32_t *)outA8;

					if(format == COLOR_FORMAT_AR10)
					{
						if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
						{
							for(x=0; x<width; x+=8)
							{
								int xx;
								for(xx=0;xx<8; xx++)
								{
									int r,g,b;
									r = signed_sptr[0]>>dnshiftto10bit;
									g = signed_sptr[8]>>dnshiftto10bit;
									b = signed_sptr[16]>>dnshiftto10bit;
									signed_sptr++;

									if(r>1023) r=1023; if(r<0) r=0;
									if(g>1023) g=1023; if(g<0) g=0;
									if(b>1023) b=1023; if(b<0) b=0;

									*outA32++ = (r<<20)|(g<<10)|(b);
								}
								signed_sptr += 16;
							}
						}
						else
						{
							for(x=0; x<width; x++)
							{
								int r = signed_sptr[0]>>dnshiftto10bit;
								int g = signed_sptr[1]>>dnshiftto10bit;
								int b = signed_sptr[2]>>dnshiftto10bit;
								signed_sptr+=3;

								if(r>1023) r=1023; if(r<0) r=0;
								if(g>1023) g=1023; if(g<0) g=0;
								if(b>1023) b=1023; if(b<0) b=0;

								*outA32++ = (r<<20)|(g<<10)|(b);
							}
						}
					}
					else
					{
						if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
						{
							for(x=0; x<width; x+=8)
							{
								int xx;
								for(xx=0;xx<8; xx++)
								{
									int r,g,b;
									r = signed_sptr[0]>>dnshiftto10bit;
									g = signed_sptr[8]>>dnshiftto10bit;
									b = signed_sptr[16]>>dnshiftto10bit;
									signed_sptr++;

									if(r>1023) r=1023; if(r<0) r=0;
									if(g>1023) g=1023; if(g<0) g=0;
									if(b>1023) b=1023; if(b<0) b=0;

									*outA32++ = r|(g<<10)|(b<<20);
								}
								signed_sptr += 16;
							}
						}
						else
						{
							for(x=0; x<width; x++)
							{
								int r = signed_sptr[0]>>dnshiftto10bit;
								int g = signed_sptr[1]>>dnshiftto10bit;
								int b = signed_sptr[2]>>dnshiftto10bit;
								signed_sptr+=3;

								if(r>1023) r=1023; if(r<0) r=0;
								if(g>1023) g=1023; if(g<0) g=0;
								if(b>1023) b=1023; if(b<0) b=0;

								*outA32++ = r|(g<<10)|(b<<20);
							}
						}
					}
					output += pitch;
				}
			}
			else
			{
				uint32_t *outA32;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA32 = (uint32_t *)outA8;


					if(format == COLOR_FORMAT_AR10)
					{
						if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
						{
							for(x=0; x<width; x+=8)
							{
								int xx;
								for(xx=0;xx<8; xx++)
								{
									int r,g,b;
									r = sptr[0]>>dnshiftto10bit;
									g = sptr[8]>>dnshiftto10bit;
									b = sptr[16]>>dnshiftto10bit;
									sptr++;

									*outA32++ = (r<<20)|(g<<10)|(b);
								}
								sptr += 16;
							}
						}
						else if(flags & ACTIVEMETADATA_PLANAR)
						{
							for(x=0; x<width; x++)
							{
								int r,g,b;
								r = sptr[0]>>dnshiftto10bit;
								g = sptr[width]>>dnshiftto10bit;
								b = sptr[width*2]>>dnshiftto10bit;
								sptr++;

								*outA32++ = (r<<20)|(g<<10)|(b);
							}
						}
						else
						{
							for(x=0; x<width; x++)
							{
								int r = sptr[0]>>dnshiftto10bit;
								int g = sptr[1]>>dnshiftto10bit;
								int b = sptr[2]>>dnshiftto10bit;
								sptr+=3;

								*outA32++ = (r<<20)|(g<<10)|(b);
							}
						}
					}
                    else
					{
						if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
						{
							for(x=0; x<width; x+=8)
							{
								int xx;
								for(xx=0;xx<8; xx++)
								{
									int r,g,b;
									r = sptr[0]>>dnshiftto10bit;
									g = sptr[8]>>dnshiftto10bit;
									b = sptr[16]>>dnshiftto10bit;
									sptr++;

									*outA32++ = r|(g<<10)|(b<<20);
								}
								sptr += 16;
							}
						}
						else if(flags & ACTIVEMETADATA_PLANAR)
						{
							for(x=0; x<width; x++)
							{
								int r,g,b;
								r = sptr[0]>>dnshiftto10bit;
								g = sptr[width]>>dnshiftto10bit;
								b = sptr[width*2]>>dnshiftto10bit;
								sptr++;

								*outA32++ = r|(g<<10)|(b<<20);
							}
						}
						else
						{
							for(x=0; x<width; x++)
							{
								int r = sptr[0]>>dnshiftto10bit;
								int g = sptr[1]>>dnshiftto10bit;
								int b = sptr[2]>>dnshiftto10bit;
								sptr+=3;

								*outA32++ = r|(g<<10)|(b<<20);
							}
						}
					}
					output += pitch;
				}
			}
			break;

		case COLOR_FORMAT_R210:
			if(saturate)
			{
				uint32_t *outA32;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA32 = (uint32_t *)outA8;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								int r,g,b;
								r = signed_sptr[0]>>dnshiftto10bit;
								g = signed_sptr[8]>>dnshiftto10bit;
								b = signed_sptr[16]>>dnshiftto10bit;
								signed_sptr++;

								if(r>1023) r=1023; if(r<0) r=0;
								if(g>1023) g=1023; if(g<0) g=0;
								if(b>1023) b=1023; if(b<0) b=0;

								*outA32++ = _bswap((r<<20)|(g<<10)|(b));
							}
							signed_sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							int r,g,b;
							r = signed_sptr[0]>>dnshiftto10bit;
							g = signed_sptr[width]>>dnshiftto10bit;
							b = signed_sptr[width*2]>>dnshiftto10bit;
							signed_sptr++;

							if(r>1023) r=1023; if(r<0) r=0;
							if(g>1023) g=1023; if(g<0) g=0;
							if(b>1023) b=1023; if(b<0) b=0;

							*outA32++ = _bswap((r<<20)|(g<<10)|(b));
						}
					}
					else
					{
						for(x=0; x<width; x++)
						{
							int r = signed_sptr[0]>>dnshiftto10bit;
							int g = signed_sptr[1]>>dnshiftto10bit;
							int b = signed_sptr[2]>>dnshiftto10bit;

							if(r>1023) r=1023; if(r<0) r=0;
							if(g>1023) g=1023; if(g<0) g=0;
							if(b>1023) b=1023; if(b<0) b=0;

							*outA32++ = _bswap((r<<20)|(g<<10)|(b));
							signed_sptr+=3;
						}
					}
					output += pitch;
				}
			}
			else
			{
				uint32_t *outA32;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA32 = (uint32_t *)outA8;

					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								int r,g,b;
								r = sptr[0]>>dnshiftto10bit;
								g = sptr[8]>>dnshiftto10bit;
								b = sptr[16]>>dnshiftto10bit;
								sptr++;

								*outA32++ = _bswap((r<<20)|(g<<10)|(b));
							}
							sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							int r,g,b;
							r = sptr[0]>>dnshiftto10bit;
							g = sptr[width]>>dnshiftto10bit;
							b = sptr[width*2]>>dnshiftto10bit;
							sptr++;

							*outA32++ = _bswap((r<<20)|(g<<10)|(b));
						}
					}
					else
					{
						for(x=0; x<width; x++)
						{
							int r = sptr[0]>>dnshiftto10bit;
							int g = sptr[1]>>dnshiftto10bit;
							int b = sptr[2]>>dnshiftto10bit;
							sptr +=3;

							*outA32++ = _bswap((r<<20)|(g<<10)|(b));
						}
					}
					output += pitch;
				}
			}
			break;


		case COLOR_FORMAT_DPX0:
			if(saturate)
			{
				uint32_t *outA32;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA32 = (uint32_t *)outA8;

					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								int r,g,b;
								r = signed_sptr[0]>>dnshiftto10bit;
								g = signed_sptr[8]>>dnshiftto10bit;
								b = signed_sptr[16]>>dnshiftto10bit;
								signed_sptr++;

								if(r>1023) r=1023; if(r<0) r=0;
								if(g>1023) g=1023; if(g<0) g=0;
								if(b>1023) b=1023; if(b<0) b=0;

								*outA32++ = _bswap((r<<22)|(g<<12)|(b<<2));
							}
							signed_sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							int r,g,b;
							r = signed_sptr[0]>>dnshiftto10bit;
							g = signed_sptr[width]>>dnshiftto10bit;
							b = signed_sptr[width*2]>>dnshiftto10bit;
							signed_sptr++;

							if(r>1023) r=1023; if(r<0) r=0;
							if(g>1023) g=1023; if(g<0) g=0;
							if(b>1023) b=1023; if(b<0) b=0;

							*outA32++ = _bswap((r<<22)|(g<<12)|(b<<2));
						}
					}
					else
					{
						for(x=0; x<width; x++)
						{
							int r = signed_sptr[0]>>dnshiftto10bit;
							int g = signed_sptr[1]>>dnshiftto10bit;
							int b = signed_sptr[2]>>dnshiftto10bit;

							if(r>1023) r=1023; if(r<0) r=0;
							if(g>1023) g=1023; if(g<0) g=0;
							if(b>1023) b=1023; if(b<0) b=0;

							*outA32++ = _bswap((r<<22)|(g<<12)|(b<<2));
							signed_sptr+=3;
						}
					}
					output += pitch;
				}
			}
			else
			{
				uint32_t *outA32;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA32 = (uint32_t *)outA8;

					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								int r,g,b;
								r = sptr[0]>>dnshiftto10bit;
								g = sptr[8]>>dnshiftto10bit;
								b = sptr[16]>>dnshiftto10bit;
								sptr++;

								*outA32++ = _bswap((r<<22)|(g<<12)|(b<<2));
							}
							sptr += 16;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							int r,g,b;
							r = sptr[0]>>dnshiftto10bit;
							g = sptr[width]>>dnshiftto10bit;
							b = sptr[width*2]>>dnshiftto10bit;
							sptr++;

							*outA32++ = _bswap((r<<22)|(g<<12)|(b<<2));
						}
					}
					else
					{
						for(x=0; x<width; x++)
						{
							int r = sptr[0]>>dnshiftto10bit;
							int g = sptr[1]>>dnshiftto10bit;
							int b = sptr[2]>>dnshiftto10bit;

							*outA32++ = _bswap((r<<22)|(g<<12)|(b<<2));
							sptr+=3;
						}
					}
					output += pitch;
				}
			}
			break;


		case COLOR_FORMAT_V210:
		case COLOR_FORMAT_YU64:
		case COLOR_FORMAT_YR16:
			{
				// TODO: Cleanup possibility 
				//	for lines=0; lines<height; lines++
				//		if
				//			colorformatdone
				//				-centre weight conversion to planar 422 buffer
				//			true
				//				-convert line to planar 422 buffer
				//		ConvertYUVStripPlanarToV210() V210, YU64, YR16
				//int lines,y, y2, u, v, r, g, b, r2, g2, b2;

				if(colorformatdone)
				{
					if(flags & ACTIVEMETADATA_PLANAR)
					{
						PIXEL *plane_array[3];
						int plane_pitch[3];
						int colwidth = width & ~15;
						ROI newroi;
						int lastU0, lastV0;

						for(lines=0; lines<height; lines++)
						{
							__m128i *srcU = (__m128i *)&sptr[width];
							__m128i *srcV = (__m128i *)&sptr[width*2];
							__m128i *dstV = (__m128i *)&sptr[width];
							__m128i *dstU = (__m128i *)&sptr[width*2];
							const __m128i mask_epi32 = _mm_set1_epi32(0xffff);

							plane_array[0] = (PIXEL *)&sptr[0];
							plane_array[1] = (PIXEL *)&sptr[width];
							plane_array[2] = (PIXEL *)&sptr[width*2];

							plane_pitch[0] = width * 2 * 2;
							plane_pitch[1] = width * 2 * 2;
							plane_pitch[2] = width * 2 * 2;

							newroi.width = width;
							newroi.height = 1;

							// 4:4:4 to 4:2:2 // U = (U1+2.U2+U3)/4 (correct centre weighting)
							for(x=0;x<colwidth;x+=16)
							{
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

								u1_output_epi16 = _mm_load_si128(srcU++);
								u2_output_epi16 = _mm_load_si128(srcU++);
								v1_output_epi16 = _mm_load_si128(srcV++);
								v2_output_epi16 = _mm_load_si128(srcV++);


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

								_mm_store_si128(dstU++, u1_output_epi16);
								_mm_store_si128(dstV++, v1_output_epi16);
							}


							//TODO support YU64 as well, so we could YU64 this way
							ConvertYUVStripPlanarToV210(plane_array, plane_pitch, newroi, output,
													pitch, width, format, colorspace, whitepoint);

							sptr += width*3;
							output += pitch;
						}

					}
					else
					{
						assert(0);
					}
				}
				else //RGB to YUV required
				{
					//int lines,y, y2, u, v, r, g, b, r2, g2, b2;
					__m128i rrrrrrrr = _mm_set1_epi16(0);
					__m128i gggggggg = _mm_set1_epi16(0);
					__m128i bbbbbbbb = _mm_set1_epi16(0);
					__m128i yyyyyyyy;
					__m128i yyyyyyy2;
					__m128i uuuuuuuu;
					__m128i vvvvvvvv;
					__m128i tttttttt;
					
					__m128i overflowprotectYUV_epi16 = _mm_set1_epi16(0x7fff-0x3ff); //10-bit
					__m128i overflowprotectRGB_epi16 = _mm_set1_epi16(0x7fff-0x1fff);
					const __m128i mask_epi32 = _mm_set1_epi32(0xffff);
					PIXEL16U *YU64start;
					PIXEL16U *YU64;
					__m128i *sptr_m128i = (__m128i*)sptr;
									
					__m128i *sptrR_m128i = (__m128i*)sptr;
					__m128i *sptrG_m128i = (__m128i*)(sptr+width);
					__m128i *sptrB_m128i = (__m128i*)(sptr+width*2);


					for(lines=0; lines<height; lines++)
					{
						//int colwidth = width & ~15;
						YU64start = sptr;
						YU64 = sptr;

						{
							int lastU0;
							int lastV0;
							PIXEL16U *output16U = (PIXEL16U *)output;
							uint32_t *v210_output_ptr = (uint32_t *)output;
							__m128i *ptrYUYV = (__m128i *)output16U;
							__m128i *ptrY = (__m128i *)output16U;
							__m128i *ptrV = (__m128i *)&output16U[width];
							__m128i *ptrU = (__m128i *)&output16U[width*3/2];
							unsigned short Y[32],U[16],V[16];
							int width16 = (width >> 4) << 4;

							if(cg2vs)
							{
								ConvertCGRGBtoVSRGB((PIXEL *)sptr, width, whitepoint, flags);
							}

							for(x=0; x<width16; x+=16)
							{
								__m128i u1_output_epi16;
								__m128i u2_output_epi16;
								__m128i v1_output_epi16;
								__m128i v2_output_epi16;

								if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
								{
									rrrrrrrr = _mm_load_si128(sptr_m128i++);
									gggggggg = _mm_load_si128(sptr_m128i++);
									bbbbbbbb = _mm_load_si128(sptr_m128i++);
								}
								else if(flags & ACTIVEMETADATA_PLANAR)
								{
									rrrrrrrr = _mm_load_si128(sptrR_m128i++);
									gggggggg = _mm_load_si128(sptrG_m128i++);
									bbbbbbbb = _mm_load_si128(sptrB_m128i++);
								}
								else
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
								}


								if(dnshiftto13bit < 0)
								{
									rrrrrrrr = _mm_slli_epi16(rrrrrrrr, -dnshiftto13bit);		//13-bit
									gggggggg = _mm_slli_epi16(gggggggg, -dnshiftto13bit);
									bbbbbbbb = _mm_slli_epi16(bbbbbbbb, -dnshiftto13bit);
								}
								else if(whitepoint == 16)
								{
									rrrrrrrr = _mm_srli_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
									gggggggg = _mm_srli_epi16(gggggggg, dnshiftto13bit);
									bbbbbbbb = _mm_srli_epi16(bbbbbbbb, dnshiftto13bit);
								}
								else
								{
									rrrrrrrr = _mm_srai_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
									gggggggg = _mm_srai_epi16(gggggggg, dnshiftto13bit);
									bbbbbbbb = _mm_srai_epi16(bbbbbbbb, dnshiftto13bit);
								}

								if(saturate)
								{
									rrrrrrrr = _mm_adds_epi16(rrrrrrrr, overflowprotectRGB_epi16);
									rrrrrrrr = _mm_subs_epu16(rrrrrrrr, overflowprotectRGB_epi16);

									gggggggg = _mm_adds_epi16(gggggggg, overflowprotectRGB_epi16);
									gggggggg = _mm_subs_epu16(gggggggg, overflowprotectRGB_epi16);

									bbbbbbbb = _mm_adds_epi16(bbbbbbbb, overflowprotectRGB_epi16);
									bbbbbbbb = _mm_subs_epu16(bbbbbbbb, overflowprotectRGB_epi16);
								}

								yyyyyyyy = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(y_rmult)); //15 bit
								tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(y_gmult));
								yyyyyyyy = _mm_adds_epi16(yyyyyyyy, tttttttt);
								tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(y_bmult));
								yyyyyyyy = _mm_adds_epi16(yyyyyyyy, tttttttt);
								yyyyyyyy = _mm_srai_epi16(yyyyyyyy, 2); //12 to 10-bit
								yyyyyyyy = _mm_adds_epi16(yyyyyyyy, _mm_set1_epi16(yoffset*4)); //16 = 64 in 10-bit

								uuuuuuuu = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(u_rmult)); //15 bit
								tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(u_gmult));
								uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);
								tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(u_bmult));
								uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);
								uuuuuuuu = _mm_srai_epi16(uuuuuuuu, 2); //12 to 10-bit
								uuuuuuuu = _mm_adds_epi16(uuuuuuuu, _mm_set1_epi16(512));//128 = 512 in 10-bit

								vvvvvvvv = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(v_rmult)); //15 bit
								tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(v_gmult));
								vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);
								tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(v_bmult));
								vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);
								vvvvvvvv = _mm_srai_epi16(vvvvvvvv, 2);//12 to 10-bit
								vvvvvvvv = _mm_adds_epi16(vvvvvvvv, _mm_set1_epi16(512));//128 = 512 in 10-bit

								u1_output_epi16 = uuuuuuuu;
								v1_output_epi16 = vvvvvvvv;



								if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
								{
									rrrrrrrr = _mm_load_si128(sptr_m128i++);
									gggggggg = _mm_load_si128(sptr_m128i++);
									bbbbbbbb = _mm_load_si128(sptr_m128i++);
								}
								else if(flags & ACTIVEMETADATA_PLANAR)
								{
									rrrrrrrr = _mm_load_si128(sptrR_m128i++);
									gggggggg = _mm_load_si128(sptrG_m128i++);
									bbbbbbbb = _mm_load_si128(sptrB_m128i++);
								}
								else
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
								}


								if(dnshiftto13bit < 0)
								{
									rrrrrrrr = _mm_slli_epi16(rrrrrrrr, -dnshiftto13bit);		//13-bit
									gggggggg = _mm_slli_epi16(gggggggg, -dnshiftto13bit);
									bbbbbbbb = _mm_slli_epi16(bbbbbbbb, -dnshiftto13bit);
								}
								else if(whitepoint == 16)
								{
									rrrrrrrr = _mm_srli_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
									gggggggg = _mm_srli_epi16(gggggggg, dnshiftto13bit);
									bbbbbbbb = _mm_srli_epi16(bbbbbbbb, dnshiftto13bit);
								}
								else
								{
									rrrrrrrr = _mm_srai_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
									gggggggg = _mm_srai_epi16(gggggggg, dnshiftto13bit);
									bbbbbbbb = _mm_srai_epi16(bbbbbbbb, dnshiftto13bit);
								}


								if(saturate)
								{
									rrrrrrrr = _mm_adds_epi16(rrrrrrrr, overflowprotectRGB_epi16);
									rrrrrrrr = _mm_subs_epu16(rrrrrrrr, overflowprotectRGB_epi16);

									gggggggg = _mm_adds_epi16(gggggggg, overflowprotectRGB_epi16);
									gggggggg = _mm_subs_epu16(gggggggg, overflowprotectRGB_epi16);

									bbbbbbbb = _mm_adds_epi16(bbbbbbbb, overflowprotectRGB_epi16);
									bbbbbbbb = _mm_subs_epu16(bbbbbbbb, overflowprotectRGB_epi16);
								}

								yyyyyyy2 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(y_rmult)); //15 bit
								tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(y_gmult));
								yyyyyyy2 = _mm_adds_epi16(yyyyyyy2, tttttttt);
								tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(y_bmult));
								yyyyyyy2 = _mm_adds_epi16(yyyyyyy2, tttttttt);
								yyyyyyy2 = _mm_srai_epi16(yyyyyyy2, 2); //12 to 10-bit
								yyyyyyy2 = _mm_adds_epi16(yyyyyyy2, _mm_set1_epi16(yoffset*4)); //16 = 64 in 10-bit

								uuuuuuuu = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(u_rmult)); //15 bit
								tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(u_gmult));
								uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);
								tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(u_bmult));
								uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);
								uuuuuuuu = _mm_srai_epi16(uuuuuuuu, 2); //12 to 10-bit
								uuuuuuuu = _mm_adds_epi16(uuuuuuuu, _mm_set1_epi16(512));//128 = 512 in 10-bit

								vvvvvvvv = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(v_rmult)); //15 bit
								tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(v_gmult));
								vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);
								tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(v_bmult));
								vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);
								vvvvvvvv = _mm_srai_epi16(vvvvvvvv, 2);//12 to 10-bit
								vvvvvvvv = _mm_adds_epi16(vvvvvvvv, _mm_set1_epi16(512));//128 = 512 in 10-bit

								u2_output_epi16 = uuuuuuuu;
								v2_output_epi16 = vvvvvvvv;

								//4:4:4 to 4:2:2
								{
									__m128i double1_epi16;
									__m128i double2_epi16;
									__m128i left1_epi16;
									__m128i left2_epi16;
									__m128i right1_epi16;
									__m128i right2_epi16;


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

									uuuuuuuu = _mm_packs_epi32 (u1_output_epi16, u2_output_epi16);
									vvvvvvvv = _mm_packs_epi32 (v1_output_epi16, v2_output_epi16);

								}
								//_mm_store_si128(YU64_m128i++, yyyyyyyy);
								//_mm_store_si128(YU64_m128i++, yyyyyyy2);
								//_mm_store_si128(YU64_m128i++, u1_output_epi16);
								//_mm_store_si128(YU64_m128i++, v1_output_epi16);

								// limit to 10-bit
								{
									yyyyyyyy = _mm_adds_epi16(yyyyyyyy, overflowprotectYUV_epi16);
									yyyyyyyy = _mm_subs_epu16(yyyyyyyy, overflowprotectYUV_epi16);

									uuuuuuuu = _mm_adds_epi16(uuuuuuuu, overflowprotectYUV_epi16);
									uuuuuuuu = _mm_subs_epu16(uuuuuuuu, overflowprotectYUV_epi16);

									vvvvvvvv = _mm_adds_epi16(vvvvvvvv, overflowprotectYUV_epi16);
									vvvvvvvv = _mm_subs_epu16(vvvvvvvv, overflowprotectYUV_epi16);
								}

								if(format == COLOR_FORMAT_YR16)
								{
									yyyyyyyy = _mm_slli_epi16(yyyyyyyy, 6);
									yyyyyyy2 = _mm_slli_epi16(yyyyyyy2, 6);
									uuuuuuuu = _mm_slli_epi16(uuuuuuuu, 6);
									vvvvvvvv = _mm_slli_epi16(vvvvvvvv, 6);
									_mm_store_si128(ptrY++, yyyyyyyy);
									_mm_store_si128(ptrY++, yyyyyyy2);
									_mm_store_si128(ptrU++, uuuuuuuu);
									_mm_store_si128(ptrV++, vvvvvvvv);
								}
								else
								{
									uint32_t yuv;

									if(format == COLOR_FORMAT_V210)
									{
										switch(x % 12)
										{
										case 0:
											_mm_storeu_si128((__m128i *)&Y[0], yyyyyyyy);
											_mm_storeu_si128((__m128i *)&U[0], uuuuuuuu);
											_mm_storeu_si128((__m128i *)&V[0], vvvvvvvv);
											_mm_storeu_si128((__m128i *)&Y[8], yyyyyyy2);

											// Assemble and store the first word of packed values
											yuv = (V[0] << V210_VALUE3_SHIFT) | (Y[0] << V210_VALUE2_SHIFT) | (U[0] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the second word of packed values
											yuv = (Y[2] << V210_VALUE3_SHIFT) | (U[1] << V210_VALUE2_SHIFT) | (Y[1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the third word of packed values
											yuv = (U[2] << V210_VALUE3_SHIFT) | (Y[3] << V210_VALUE2_SHIFT) | (V[1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the fourth word of packed values
											yuv = (Y[5] << V210_VALUE3_SHIFT) | (V[2] << V210_VALUE2_SHIFT) | (Y[4] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;


											// Assemble and store the first word of packed values
											yuv = (V[3+0] << V210_VALUE3_SHIFT) | (Y[6+0] << V210_VALUE2_SHIFT) | (U[3+0] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the second word of packed values
											yuv = (Y[6+2] << V210_VALUE3_SHIFT) | (U[3+1] << V210_VALUE2_SHIFT) | (Y[6+1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the third word of packed values
											yuv = (U[3+2] << V210_VALUE3_SHIFT) | (Y[6+3] << V210_VALUE2_SHIFT) | (V[3+1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the fourth word of packed values
											yuv = (Y[6+5] << V210_VALUE3_SHIFT) | (V[3+2] << V210_VALUE2_SHIFT) | (Y[6+4] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											Y[0] = Y[12];
											Y[1] = Y[13];
											Y[2] = Y[14];
											Y[3] = Y[15];

											U[0] = U[6];
											U[1] = U[7];

											V[0] = V[6];
											V[1] = V[7];
											break;


										case 4:
											_mm_storeu_si128((__m128i *)&Y[4], yyyyyyyy);
											_mm_storeu_si128((__m128i *)&U[2], uuuuuuuu);
											_mm_storeu_si128((__m128i *)&V[2], vvvvvvvv);
											_mm_storeu_si128((__m128i *)&Y[12], yyyyyyy2);

											// Assemble and store the first word of packed values
											yuv = (V[0] << V210_VALUE3_SHIFT) | (Y[0] << V210_VALUE2_SHIFT) | (U[0] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the second word of packed values
											yuv = (Y[2] << V210_VALUE3_SHIFT) | (U[1] << V210_VALUE2_SHIFT) | (Y[1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the third word of packed values
											yuv = (U[2] << V210_VALUE3_SHIFT) | (Y[3] << V210_VALUE2_SHIFT) | (V[1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the fourth word of packed values
											yuv = (Y[5] << V210_VALUE3_SHIFT) | (V[2] << V210_VALUE2_SHIFT) | (Y[4] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;


											// Assemble and store the first word of packed values
											yuv = (V[3+0] << V210_VALUE3_SHIFT) | (Y[6+0] << V210_VALUE2_SHIFT) | (U[3+0] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the second word of packed values
											yuv = (Y[6+2] << V210_VALUE3_SHIFT) | (U[3+1] << V210_VALUE2_SHIFT) | (Y[6+1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the third word of packed values
											yuv = (U[3+2] << V210_VALUE3_SHIFT) | (Y[6+3] << V210_VALUE2_SHIFT) | (V[3+1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the fourth word of packed values
											yuv = (Y[6+5] << V210_VALUE3_SHIFT) | (V[3+2] << V210_VALUE2_SHIFT) | (Y[6+4] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;


											// Assemble and store the first word of packed values
											yuv = (V[6+0] << V210_VALUE3_SHIFT) | (Y[12+0] << V210_VALUE2_SHIFT) | (U[6+0] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the second word of packed values
											yuv = (Y[12+2] << V210_VALUE3_SHIFT) | (U[6+1] << V210_VALUE2_SHIFT) | (Y[12+1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the third word of packed values
											yuv = (U[6+2] << V210_VALUE3_SHIFT) | (Y[12+3] << V210_VALUE2_SHIFT) | (V[6+1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the fourth word of packed values
											yuv = (Y[12+5] << V210_VALUE3_SHIFT) | (V[6+2] << V210_VALUE2_SHIFT) | (Y[12+4] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;


											Y[0] = Y[18];
											Y[1] = Y[19];

											U[0] = U[9];

											V[0] = V[9];
											break;


										case 8:
											_mm_storeu_si128((__m128i *)&Y[2], yyyyyyyy);
											_mm_storeu_si128((__m128i *)&U[1], uuuuuuuu);
											_mm_storeu_si128((__m128i *)&V[1], vvvvvvvv);
											_mm_storeu_si128((__m128i *)&Y[10], yyyyyyy2);

											// Assemble and store the first word of packed values
											yuv = (V[0] << V210_VALUE3_SHIFT) | (Y[0] << V210_VALUE2_SHIFT) | (U[0] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the second word of packed values
											yuv = (Y[2] << V210_VALUE3_SHIFT) | (U[1] << V210_VALUE2_SHIFT) | (Y[1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the third word of packed values
											yuv = (U[2] << V210_VALUE3_SHIFT) | (Y[3] << V210_VALUE2_SHIFT) | (V[1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the fourth word of packed values
											yuv = (Y[5] << V210_VALUE3_SHIFT) | (V[2] << V210_VALUE2_SHIFT) | (Y[4] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;


											// Assemble and store the first word of packed values
											yuv = (V[3+0] << V210_VALUE3_SHIFT) | (Y[6+0] << V210_VALUE2_SHIFT) | (U[3+0] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the second word of packed values
											yuv = (Y[6+2] << V210_VALUE3_SHIFT) | (U[3+1] << V210_VALUE2_SHIFT) | (Y[6+1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the third word of packed values
											yuv = (U[3+2] << V210_VALUE3_SHIFT) | (Y[6+3] << V210_VALUE2_SHIFT) | (V[3+1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the fourth word of packed values
											yuv = (Y[6+5] << V210_VALUE3_SHIFT) | (V[3+2] << V210_VALUE2_SHIFT) | (Y[6+4] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;


											// Assemble and store the first word of packed values
											yuv = (V[6+0] << V210_VALUE3_SHIFT) | (Y[12+0] << V210_VALUE2_SHIFT) | (U[6+0] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the second word of packed values
											yuv = (Y[12+2] << V210_VALUE3_SHIFT) | (U[6+1] << V210_VALUE2_SHIFT) | (Y[12+1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the third word of packed values
											yuv = (U[6+2] << V210_VALUE3_SHIFT) | (Y[12+3] << V210_VALUE2_SHIFT) | (V[6+1] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the fourth word of packed values
											yuv = (Y[12+5] << V210_VALUE3_SHIFT) | (V[6+2] << V210_VALUE2_SHIFT) | (Y[12+4] << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											break;
										}
									}
									else if(format == COLOR_FORMAT_YU64) 
									{
										__m128i uvuvuvuv, yuyvyuyv;

										yyyyyyyy = _mm_slli_epi16(yyyyyyyy, 6);
										yyyyyyy2 = _mm_slli_epi16(yyyyyyy2, 6);
										uuuuuuuu = _mm_slli_epi16(uuuuuuuu, 6);
										vvvvvvvv = _mm_slli_epi16(vvvvvvvv, 6);

										uvuvuvuv = _mm_unpacklo_epi16(vvvvvvvv, uuuuuuuu);
										yuyvyuyv = _mm_unpacklo_epi16(yyyyyyyy, uvuvuvuv);
										_mm_store_si128(ptrYUYV++, yuyvyuyv);

										yuyvyuyv = _mm_unpackhi_epi16(yyyyyyyy, uvuvuvuv);
										_mm_store_si128(ptrYUYV++, yuyvyuyv);

										uvuvuvuv = _mm_unpackhi_epi16(vvvvvvvv, uuuuuuuu);
										yuyvyuyv = _mm_unpacklo_epi16(yyyyyyy2, uvuvuvuv);
										_mm_store_si128(ptrYUYV++, yuyvyuyv);

										yuyvyuyv = _mm_unpackhi_epi16(yyyyyyy2, uvuvuvuv);
										_mm_store_si128(ptrYUYV++, yuyvyuyv);
									}
								}
							}

							if(x<width)
							{
								unsigned long *yu64 = (unsigned long *)ptrYUYV;
								unsigned long *yr16Y = (unsigned long *)ptrY;
								unsigned long *yr16U = (unsigned long *)ptrU;
								unsigned long *yr16V = (unsigned long *)ptrV;
								

								for(;x<width;x+=4)
								{
									if(format == COLOR_FORMAT_YR16)
									{
										//TODO not fill with Black, very low priority
										*(yr16Y++) = (16<<16)|16;
										*(yr16Y++) = (16<<16)|16;
										*(yr16U++) = (128<<16)|16;
										*(yr16V++) = (128<<16)|16;
									}
									else
									{
										uint32_t yuv;

										// TODO not fill with Black, low priority

										if(format == COLOR_FORMAT_V210)
										{
											// Assemble and store the first word of packed values
											yuv = (512 << V210_VALUE3_SHIFT) | (64 << V210_VALUE2_SHIFT) | (512 << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the second word of packed values
											yuv = (64 << V210_VALUE3_SHIFT) | (512 << V210_VALUE2_SHIFT) | (64 << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

											// Assemble and store the third word of packed values
											yuv = (512 << V210_VALUE3_SHIFT) | (64 << V210_VALUE2_SHIFT) | (512 << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;
									
											yuv = (64 << V210_VALUE3_SHIFT) | (512 << V210_VALUE2_SHIFT) | (64 << V210_VALUE1_SHIFT);
											*(v210_output_ptr++) = yuv;

										}
										else if(format == COLOR_FORMAT_YU64) 
										{
										
											// TODO not fill with Black, low priority
											yuv = (128<<24)|(16<<8);
											*(yu64++) = yuv;
											*(yu64++) = yuv;
											*(yu64++) = yuv;
											*(yu64++) = yuv;
										}
									}
								}
							}
						}

					//	if(format == COLOR_FORMAT_V210)
					//		ConvertYUV16sRowToV210(YU64start, output, width);
					//	else if(format == COLOR_FORMAT_YU64)
					//		ConvertYUV16sRowToYU64(YU64start, output, width);

						output += pitch;
					}
				}
			}
			break;

		case COLOR_FORMAT_YVYU:
		case COLOR_FORMAT_UYVY:
		case COLOR_FORMAT_YUYV:
			{
				//int lines,y, y2, u, v, r, g, b, r2, g2, b2;

				if(colorformatdone)
				{
					__m128i yyyyyyyy;
					__m128i uuuuuuuu;
					__m128i vvvvvvvv;
					__m128i tttttttt;
					__m128i ditheryy;
					__m128i ditheruu;
					__m128i dithervv;
					__m128i overflowprotectYUV_epi16 = _mm_set1_epi16(0x7fff-0xff);

					for(lines=linenum; lines<linenum+height; lines++)
					{
						outA8 = output;
						if(lines & 1)
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

						for(x=0; x<width; x+=8)
						{
							if(flags & ACTIVEMETADATA_PLANAR)
							{
								yyyyyyyy = _mm_loadu_si128((__m128i *)&sptr[0]);
								uuuuuuuu = _mm_loadu_si128((__m128i *)&sptr[width]);
								vvvvvvvv = _mm_loadu_si128((__m128i *)&sptr[width*2]);
								sptr+=8;
							}
							else
							{
								yyyyyyyy = _mm_insert_epi16(yyyyyyyy, sptr[0], 0);
								uuuuuuuu = _mm_insert_epi16(uuuuuuuu, sptr[1], 0);
								vvvvvvvv = _mm_insert_epi16(vvvvvvvv, sptr[2], 0);
								yyyyyyyy = _mm_insert_epi16(yyyyyyyy, sptr[3], 1);
								uuuuuuuu = _mm_insert_epi16(uuuuuuuu, sptr[4], 1);
								vvvvvvvv = _mm_insert_epi16(vvvvvvvv, sptr[5], 1);
								yyyyyyyy = _mm_insert_epi16(yyyyyyyy, sptr[6], 2);
								uuuuuuuu = _mm_insert_epi16(uuuuuuuu, sptr[7], 2);
								vvvvvvvv = _mm_insert_epi16(vvvvvvvv, sptr[8], 2);
								yyyyyyyy = _mm_insert_epi16(yyyyyyyy, sptr[9], 3);
								uuuuuuuu = _mm_insert_epi16(uuuuuuuu, sptr[10], 3);
								vvvvvvvv = _mm_insert_epi16(vvvvvvvv, sptr[11], 3);
								yyyyyyyy = _mm_insert_epi16(yyyyyyyy, sptr[12], 4);
								uuuuuuuu = _mm_insert_epi16(uuuuuuuu, sptr[13], 4);
								vvvvvvvv = _mm_insert_epi16(vvvvvvvv, sptr[14], 4);
								yyyyyyyy = _mm_insert_epi16(yyyyyyyy, sptr[15], 5);
								uuuuuuuu = _mm_insert_epi16(uuuuuuuu, sptr[16], 5);
								vvvvvvvv = _mm_insert_epi16(vvvvvvvv, sptr[17], 5);
								yyyyyyyy = _mm_insert_epi16(yyyyyyyy, sptr[18], 6);
								uuuuuuuu = _mm_insert_epi16(uuuuuuuu, sptr[19], 6);
								vvvvvvvv = _mm_insert_epi16(vvvvvvvv, sptr[20], 6);
								yyyyyyyy = _mm_insert_epi16(yyyyyyyy, sptr[21], 7);
								uuuuuuuu = _mm_insert_epi16(uuuuuuuu, sptr[22], 7);
								vvvvvvvv = _mm_insert_epi16(vvvvvvvv, sptr[23], 7);

								sptr += 24;
							}

							yyyyyyyy = _mm_srai_epi16(yyyyyyyy, 1);
							uuuuuuuu = _mm_srai_epi16(uuuuuuuu, 1);
							vvvvvvvv = _mm_srai_epi16(vvvvvvvv, 1);

							yyyyyyyy = _mm_adds_epi16(yyyyyyyy, ditheryy);
							yyyyyyyy = _mm_srai_epi16(yyyyyyyy, 4);

							tttttttt = _mm_slli_si128(uuuuuuuu, 2);
							uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);
							uuuuuuuu = _mm_adds_epi16(uuuuuuuu, ditheruu);
							uuuuuuuu = _mm_srai_epi16(uuuuuuuu, 5);

							tttttttt = _mm_slli_si128(vvvvvvvv, 2);
							vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);
							vvvvvvvv = _mm_adds_epi16(vvvvvvvv, dithervv);
							vvvvvvvv = _mm_srai_epi16(vvvvvvvv, 5);

							{
								yyyyyyyy = _mm_adds_epi16(yyyyyyyy, overflowprotectYUV_epi16);
								yyyyyyyy = _mm_subs_epu16(yyyyyyyy, overflowprotectYUV_epi16);

								uuuuuuuu = _mm_adds_epi16(uuuuuuuu, overflowprotectYUV_epi16);
								uuuuuuuu = _mm_subs_epu16(uuuuuuuu, overflowprotectYUV_epi16);

								vvvvvvvv = _mm_adds_epi16(vvvvvvvv, overflowprotectYUV_epi16);
								vvvvvvvv = _mm_subs_epu16(vvvvvvvv, overflowprotectYUV_epi16);
							}

							if (format == COLOR_FORMAT_YUYV) {
								outA8[0] = _mm_extract_epi16(yyyyyyyy, 0);
								outA8[1] = _mm_extract_epi16(uuuuuuuu, 1);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 1);
								outA8[3] = _mm_extract_epi16(vvvvvvvv, 1);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(yyyyyyyy, 2);
								outA8[1] = _mm_extract_epi16(uuuuuuuu, 3);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 3);
								outA8[3] = _mm_extract_epi16(vvvvvvvv, 3);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(yyyyyyyy, 4);
								outA8[1] = _mm_extract_epi16(uuuuuuuu, 5);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 5);
								outA8[3] = _mm_extract_epi16(vvvvvvvv, 5);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(yyyyyyyy, 6);
								outA8[1] = _mm_extract_epi16(uuuuuuuu, 7);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 7);
								outA8[3] = _mm_extract_epi16(vvvvvvvv, 7);
								outA8+=4;
							} else if (format == COLOR_FORMAT_UYVY) {
								outA8[0] = _mm_extract_epi16(uuuuuuuu, 1);
								outA8[1] = _mm_extract_epi16(yyyyyyyy, 0);
								outA8[2] = _mm_extract_epi16(vvvvvvvv, 1);
								outA8[3] = _mm_extract_epi16(yyyyyyyy, 1);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(uuuuuuuu, 3);
								outA8[1] = _mm_extract_epi16(yyyyyyyy, 2);
								outA8[2] = _mm_extract_epi16(vvvvvvvv, 3);
								outA8[3] = _mm_extract_epi16(yyyyyyyy, 3);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(uuuuuuuu, 5);
								outA8[1] = _mm_extract_epi16(yyyyyyyy, 4);
								outA8[2] = _mm_extract_epi16(vvvvvvvv, 5);
								outA8[3] = _mm_extract_epi16(yyyyyyyy, 5);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(uuuuuuuu, 7);
								outA8[1] = _mm_extract_epi16(yyyyyyyy, 6);
								outA8[2] = _mm_extract_epi16(vvvvvvvv, 7);
								outA8[3] = _mm_extract_epi16(yyyyyyyy, 7);
								outA8+=4;
							} else if (format == COLOR_FORMAT_YVYU) {
								outA8[0] = _mm_extract_epi16(yyyyyyyy, 0);
								outA8[1] = _mm_extract_epi16(vvvvvvvv, 1);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 1);
								outA8[3] = _mm_extract_epi16(uuuuuuuu, 1);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(yyyyyyyy, 2);
								outA8[1] = _mm_extract_epi16(vvvvvvvv, 3);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 3);
								outA8[3] = _mm_extract_epi16(uuuuuuuu, 3);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(yyyyyyyy, 4);
								outA8[1] = _mm_extract_epi16(vvvvvvvv, 5);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 5);
								outA8[3] = _mm_extract_epi16(uuuuuuuu, 5);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(yyyyyyyy, 6);
								outA8[1] = _mm_extract_epi16(vvvvvvvv, 7);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 7);
								outA8[3] = _mm_extract_epi16(uuuuuuuu, 7);
								outA8+=4;
							}
						}
						if(flags & ACTIVEMETADATA_PLANAR)
						{
							sptr += width*2;
						}
						output += pitch;
					}
				}
				else
				{
					//int lines,y, y2, u, v, r, g, b, r2, g2, b2;
	#if 1
					__m128i rrrrrrrr = _mm_set1_epi16(0);
					__m128i gggggggg = _mm_set1_epi16(0);
					__m128i bbbbbbbb = _mm_set1_epi16(0);
					__m128i yyyyyyyy;
					__m128i uuuuuuuu;
					__m128i vvvvvvvv;
					__m128i tttttttt;
					__m128i ditheryy;
					__m128i ditheruu;
					__m128i dithervv;

					__m128i overflowprotectYUV_epi16 = _mm_set1_epi16(0x7fff-0xff);
					__m128i overflowprotectRGB_epi16 = _mm_set1_epi16(0x7fff-0x1fff);

					for(lines=linenum; lines<linenum+height; lines++)
					{
						outA8 = output;
						if(lines & 1)
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
						if(cg2vs)
						{
							ConvertCGRGBtoVSRGB((PIXEL *)sptr, width, whitepoint, flags);
						}

						for(x=0; x<width; x+=8)
						{
							if(flags & ACTIVEMETADATA_PLANAR)
							{
								rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[0]);
								gggggggg = _mm_loadu_si128((__m128i *)&sptr[width]);
								bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[width*2]);
								sptr+=8;
							}
							else if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
							{
								rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[0]);
								gggggggg = _mm_loadu_si128((__m128i *)&sptr[8]);
								bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[16]);
								sptr+=24;
							}
							else
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
							}

							if(dnshiftto13bit < 0)
							{
								rrrrrrrr = _mm_slli_epi16(rrrrrrrr, -dnshiftto13bit);		//13-bit
								gggggggg = _mm_slli_epi16(gggggggg, -dnshiftto13bit);
								bbbbbbbb = _mm_slli_epi16(bbbbbbbb, -dnshiftto13bit);
							}
							else if(whitepoint == 16)
							{
								rrrrrrrr = _mm_srli_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
								gggggggg = _mm_srli_epi16(gggggggg, dnshiftto13bit);
								bbbbbbbb = _mm_srli_epi16(bbbbbbbb, dnshiftto13bit);
							}
							else
							{
								rrrrrrrr = _mm_srai_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
								gggggggg = _mm_srai_epi16(gggggggg, dnshiftto13bit);
								bbbbbbbb = _mm_srai_epi16(bbbbbbbb, dnshiftto13bit);
							}

							if(saturate)
							{
								rrrrrrrr = _mm_adds_epi16(rrrrrrrr, overflowprotectRGB_epi16);
								rrrrrrrr = _mm_subs_epu16(rrrrrrrr, overflowprotectRGB_epi16);

								gggggggg = _mm_adds_epi16(gggggggg, overflowprotectRGB_epi16);
								gggggggg = _mm_subs_epu16(gggggggg, overflowprotectRGB_epi16);

								bbbbbbbb = _mm_adds_epi16(bbbbbbbb, overflowprotectRGB_epi16);
								bbbbbbbb = _mm_subs_epu16(bbbbbbbb, overflowprotectRGB_epi16);
							}

							yyyyyyyy = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(y_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(y_gmult));
							yyyyyyyy = _mm_adds_epi16(yyyyyyyy, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(y_bmult));
							yyyyyyyy = _mm_adds_epi16(yyyyyyyy, tttttttt);
							yyyyyyyy = _mm_adds_epi16(yyyyyyyy, ditheryy);
							yyyyyyyy = _mm_srai_epi16(yyyyyyyy, 4);
							yyyyyyyy = _mm_adds_epi16(yyyyyyyy, _mm_set1_epi16(yoffset));

							uuuuuuuu = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(u_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(u_gmult));
							uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(u_bmult));
							uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);
							tttttttt = _mm_slli_si128(uuuuuuuu, 2);      //DAN why are there two shifts (R3D2DPX doesn't need this.)
							uuuuuuuu = _mm_adds_epi16(uuuuuuuu, tttttttt);
							uuuuuuuu = _mm_adds_epi16(uuuuuuuu, ditheruu);
							uuuuuuuu = _mm_srai_epi16(uuuuuuuu, 5);
							uuuuuuuu = _mm_adds_epi16(uuuuuuuu, _mm_set1_epi16(128));

							vvvvvvvv = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(v_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(v_gmult));
							vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(v_bmult));
							vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);
							tttttttt = _mm_slli_si128(vvvvvvvv, 2);    //DAN why are there two shifts (R3D2DPX doesn't need this.)
							vvvvvvvv = _mm_adds_epi16(vvvvvvvv, tttttttt);
							vvvvvvvv = _mm_adds_epi16(vvvvvvvv, dithervv);
							vvvvvvvv = _mm_srai_epi16(vvvvvvvv, 5);
							vvvvvvvv = _mm_adds_epi16(vvvvvvvv, _mm_set1_epi16(128));

			
							{
								yyyyyyyy = _mm_adds_epi16(yyyyyyyy, overflowprotectYUV_epi16);
								yyyyyyyy = _mm_subs_epu16(yyyyyyyy, overflowprotectYUV_epi16);

								uuuuuuuu = _mm_adds_epi16(uuuuuuuu, overflowprotectYUV_epi16);
								uuuuuuuu = _mm_subs_epu16(uuuuuuuu, overflowprotectYUV_epi16);

								vvvvvvvv = _mm_adds_epi16(vvvvvvvv, overflowprotectYUV_epi16);
								vvvvvvvv = _mm_subs_epu16(vvvvvvvv, overflowprotectYUV_epi16);
							}


							if (format == COLOR_FORMAT_YUYV) {
								outA8[0] = _mm_extract_epi16(yyyyyyyy, 0);
								outA8[1] = _mm_extract_epi16(uuuuuuuu, 1);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 1);
								outA8[3] = _mm_extract_epi16(vvvvvvvv, 1);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(yyyyyyyy, 2);
								outA8[1] = _mm_extract_epi16(uuuuuuuu, 3);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 3);
								outA8[3] = _mm_extract_epi16(vvvvvvvv, 3);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(yyyyyyyy, 4);
								outA8[1] = _mm_extract_epi16(uuuuuuuu, 5);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 5);
								outA8[3] = _mm_extract_epi16(vvvvvvvv, 5);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(yyyyyyyy, 6);
								outA8[1] = _mm_extract_epi16(uuuuuuuu, 7);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 7);
								outA8[3] = _mm_extract_epi16(vvvvvvvv, 7);
								outA8+=4;
							} else if (format == COLOR_FORMAT_UYVY) {
								outA8[0] = _mm_extract_epi16(uuuuuuuu, 1);
								outA8[1] = _mm_extract_epi16(yyyyyyyy, 0);
								outA8[2] = _mm_extract_epi16(vvvvvvvv, 1);
								outA8[3] = _mm_extract_epi16(yyyyyyyy, 1);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(uuuuuuuu, 3);
								outA8[1] = _mm_extract_epi16(yyyyyyyy, 2);
								outA8[2] = _mm_extract_epi16(vvvvvvvv, 3);
								outA8[3] = _mm_extract_epi16(yyyyyyyy, 3);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(uuuuuuuu, 5);
								outA8[1] = _mm_extract_epi16(yyyyyyyy, 4);
								outA8[2] = _mm_extract_epi16(vvvvvvvv, 5);
								outA8[3] = _mm_extract_epi16(yyyyyyyy, 5);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(uuuuuuuu, 7);
								outA8[1] = _mm_extract_epi16(yyyyyyyy, 6);
								outA8[2] = _mm_extract_epi16(vvvvvvvv, 7);
								outA8[3] = _mm_extract_epi16(yyyyyyyy, 7);
								outA8+=4;
							} else if (format == COLOR_FORMAT_YVYU) {
								outA8[0] = _mm_extract_epi16(yyyyyyyy, 0);
								outA8[1] = _mm_extract_epi16(vvvvvvvv, 1);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 1);
								outA8[3] = _mm_extract_epi16(uuuuuuuu, 1);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(yyyyyyyy, 2);
								outA8[1] = _mm_extract_epi16(vvvvvvvv, 3);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 3);
								outA8[3] = _mm_extract_epi16(uuuuuuuu, 3);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(yyyyyyyy, 4);
								outA8[1] = _mm_extract_epi16(vvvvvvvv, 5);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 5);
								outA8[3] = _mm_extract_epi16(uuuuuuuu, 5);
								outA8+=4;

								outA8[0] = _mm_extract_epi16(yyyyyyyy, 6);
								outA8[1] = _mm_extract_epi16(vvvvvvvv, 7);
								outA8[2] = _mm_extract_epi16(yyyyyyyy, 7);
								outA8[3] = _mm_extract_epi16(uuuuuuuu, 7);
								outA8+=4;
							}
						}

						if(flags & ACTIVEMETADATA_PLANAR)
						{
							sptr += width*2;
						}
						output += pitch;
					}
	#else
					assert(0) // old code disabled
				/*	for(lines=0; lines<height; lines++)
					{
						outA8 = output;
						for(x=0; x<width; x+=2)
						{

							if(flags & ACTIVEMETADATA_PLANAR)
							{
								r = *sptr<<upshiftto16bit;
								g = sptr[width]<<upshiftto16bit;
								b = sptr[width*2]<<upshiftto16bit;
								sptr++;
								r2 = *sptr<<upshiftto16bit;
								g2 = sptr[width]<<upshiftto16bit;
								b2 = sptr[width*2]<<upshiftto16bit;
								sptr++;
							}
							else
							{
								r = *sptr++<<upshiftto16bit;
								g = *sptr++<<upshiftto16bit;
								b = *sptr++<<upshiftto16bit;
								r2 = *sptr++<<upshiftto16bit;
								g2 = *sptr++<<upshiftto16bit;
								b2 = *sptr++<<upshiftto16bit;
							}

							if(saturate)
							{
								if(r > 65535) r = 65535; if(r<0) r=0;
								if(g > 65535) g = 65535; if(g<0) g=0;
								if(b > 65535) b = 65535; if(b<0) b=0;
							}

							y = ((750*r + 2515*g + 254*b)>>20) + 16;
							y2 = ((750*r2 + 2515*g2 + 254*b2)>>20) + 16;
							u = ((-414*(r+r2) -1384*(g+g2) + 1798*(b+b2))>>21) + 128;
							v = ((1798*(r+r2) -1634*(g+g2)  -164*(b+b2))>>21) + 128;
							outA8[0] = y;
							outA8[1] = u;
							outA8[2] = y2;
							outA8[3] = v;
							outA8+=4;
						}
						if(flags & ACTIVEMETADATA_PLANAR)
						{
							sptr += width*2;
						}
						output += pitch;
					}*/
#endif
				}
			}
			break;

		case COLOR_FORMAT_R408:
		case COLOR_FORMAT_V408:
			{
				//int lines,y, y2, u, v, r, g, b, r2, g2, b2;

				if(colorformatdone)
				{
					__m128i yyyyyyyy1;
					__m128i uuuuuuuu1;
					__m128i vvvvvvvv1;
					__m128i yyyyyyyy2;
					__m128i uuuuuuuu2;
					__m128i vvvvvvvv2;
					__m128i a_epi8 =  _mm_set1_epi8(0xff);
					__m128i offsetR408_epi8 =  _mm_set1_epi8(16);
					__m128i y_epi8;
					__m128i u_epi8;
					__m128i v_epi8;
					//__m128i tttttttt;
					__m128i ditheryy =  _mm_set1_epi16(0);
					__m128i ditheruu =  _mm_set1_epi16(0);
					__m128i dithervv =  _mm_set1_epi16(0);


					for(lines=linenum; lines<linenum+height; lines++)
					{
						__m128i *out_epi8 = (__m128i *)output;
						outA8 = output;

						if(lines & 1)
						{
							ditheryy = _mm_set_epi16( 2, 30,  6, 26, 10, 22, 14, 18); // 5 bits of dither
							ditheruu = _mm_set_epi16( 2, 30,  6, 26, 10, 22, 14, 18);
							dithervv = _mm_set_epi16(18, 14, 22, 10, 26,  6, 30,  2);
						}
						else
						{
							ditheryy = _mm_set_epi16(18, 14, 22, 10, 26,  6, 30,  2);
							ditheruu = _mm_set_epi16(18, 14, 22, 10, 26,  6, 30,  2);
							dithervv = _mm_set_epi16( 2, 30,  6, 26, 10, 22, 14, 18);
						}

						for(x=0; x<width; x+=16)
						{
							if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
							{
								yyyyyyyy1 = _mm_loadu_si128((__m128i *)&sptr[0]);
								uuuuuuuu1 = _mm_loadu_si128((__m128i *)&sptr[8]);
								vvvvvvvv1 = _mm_loadu_si128((__m128i *)&sptr[16]);
								yyyyyyyy2 = _mm_loadu_si128((__m128i *)&sptr[24]);
								uuuuuuuu2 = _mm_loadu_si128((__m128i *)&sptr[32]);
								vvvvvvvv2 = _mm_loadu_si128((__m128i *)&sptr[40]);
								sptr+=48;
							}
							else if(flags & ACTIVEMETADATA_PLANAR)
							{
								yyyyyyyy1 = _mm_loadu_si128((__m128i *)&sptr[0]);
								uuuuuuuu1 = _mm_loadu_si128((__m128i *)&sptr[width]);
								vvvvvvvv1 = _mm_loadu_si128((__m128i *)&sptr[width*2]);
								sptr+=8;
								yyyyyyyy2 = _mm_loadu_si128((__m128i *)&sptr[0]);
								uuuuuuuu2 = _mm_loadu_si128((__m128i *)&sptr[width]);
								vvvvvvvv2 = _mm_loadu_si128((__m128i *)&sptr[width*2]);
								sptr+=8;
							}
							else
							{
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[0],  0);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[1],  0);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[2],  0);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[3],  1);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[4],  1);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[5],  1);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[6],  2);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[7],  2);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[8],  2);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[9],  3);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[10], 3);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[11], 3);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[12], 4);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[13], 4);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[14], 4);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[15], 5);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[16], 5);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[17], 5);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[18], 6);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[19], 6);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[20], 6);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[21], 7);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[22], 7);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[23], 7);
								sptr+=24;


								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[0], 0);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[1], 0);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[2], 0);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[3], 1);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[4], 1);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[5], 1);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[6], 2);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[7], 2);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[8], 2);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[9], 3);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[10],3);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[11],3);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[12],4);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[13],4);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[14],4);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[15],5);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[16],5);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[17],5);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[18],6);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[19],6);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[20],6);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[21],7);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[22],7);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[23],7);
								sptr+=24;
							}

							yyyyyyyy1 = _mm_srli_epi16(yyyyyyyy1, dnshiftto13bit);
							yyyyyyyy1 = _mm_adds_epi16(yyyyyyyy1, ditheryy);
							yyyyyyyy1 = _mm_srai_epi16(yyyyyyyy1, 5);
							uuuuuuuu1 = _mm_srli_epi16(uuuuuuuu1, dnshiftto13bit);
							uuuuuuuu1 = _mm_adds_epi16(uuuuuuuu1, ditheruu);
							uuuuuuuu1 = _mm_srai_epi16(uuuuuuuu1, 5);
							vvvvvvvv1 = _mm_srli_epi16(vvvvvvvv1, dnshiftto13bit);
							vvvvvvvv1 = _mm_adds_epi16(vvvvvvvv1, dithervv);
							vvvvvvvv1 = _mm_srai_epi16(vvvvvvvv1, 5);

							yyyyyyyy2 = _mm_srli_epi16(yyyyyyyy2, dnshiftto13bit);
							yyyyyyyy2 = _mm_adds_epi16(yyyyyyyy2, ditheryy);
							yyyyyyyy2 = _mm_srai_epi16(yyyyyyyy2, 5);
							uuuuuuuu2 = _mm_srli_epi16(uuuuuuuu2, dnshiftto13bit);
							uuuuuuuu2 = _mm_adds_epi16(uuuuuuuu2, ditheruu);
							uuuuuuuu2 = _mm_srai_epi16(uuuuuuuu2, 5);
							vvvvvvvv2 = _mm_srli_epi16(vvvvvvvv2, dnshiftto13bit);
							vvvvvvvv2 = _mm_adds_epi16(vvvvvvvv2, dithervv);
							vvvvvvvv2 = _mm_srai_epi16(vvvvvvvv2, 5);

							y_epi8 = _mm_packus_epi16(yyyyyyyy1, yyyyyyyy2); //pack to 8-bit
							u_epi8 = _mm_packus_epi16(uuuuuuuu1, uuuuuuuu2); //pack to 8-bit
							v_epi8 = _mm_packus_epi16(vvvvvvvv1, vvvvvvvv2); //pack to 8-bit

							if(format == COLOR_FORMAT_V408) // UYVA
							{
								__m128i UY,VA,UYVA;

								UY = _mm_unpacklo_epi8(u_epi8, y_epi8);
								VA = _mm_unpacklo_epi8(v_epi8, a_epi8);
								UYVA = _mm_unpacklo_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

								UYVA = _mm_unpackhi_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

								UY = _mm_unpackhi_epi8(u_epi8, y_epi8);
								VA = _mm_unpackhi_epi8(v_epi8, a_epi8);
								UYVA = _mm_unpacklo_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

								UYVA = _mm_unpackhi_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);
							}
							else //r408 AYUV
							{
								__m128i AY,UV,AYUV;

								y_epi8 = _mm_subs_epu8(y_epi8, offsetR408_epi8);

								AY = _mm_unpacklo_epi8(a_epi8, y_epi8);
								UV = _mm_unpacklo_epi8(u_epi8, v_epi8);
								AYUV = _mm_unpacklo_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);

								AYUV = _mm_unpackhi_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);

								AY = _mm_unpackhi_epi8(a_epi8, y_epi8);
								UV = _mm_unpackhi_epi8(u_epi8, v_epi8);
								AYUV = _mm_unpacklo_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);

								AYUV = _mm_unpackhi_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);
							}
						}
						if(flags & ACTIVEMETADATA_PLANAR)
						{
							sptr += width*2;
						}
						output += pitch;
					}
				}
				else
				{
					//int lines,y, y2, u, v, r, g, b, r2, g2, b2;
	#if 1
					__m128i rrrrrrrr = _mm_set1_epi16(0);
					__m128i gggggggg = _mm_set1_epi16(0);
					__m128i bbbbbbbb = _mm_set1_epi16(0);
					__m128i yyyyyyyy1;
					__m128i uuuuuuuu1;
					__m128i vvvvvvvv1;
					__m128i yyyyyyyy2;
					__m128i uuuuuuuu2;
					__m128i vvvvvvvv2;
					__m128i a_epi8 =  _mm_set1_epi8(0xff);
					__m128i offsetR408_epi8 =  _mm_set1_epi8(16);
					__m128i y_epi8;
					__m128i u_epi8;
					__m128i v_epi8;
					__m128i tttttttt;
					__m128i ditheryy =  _mm_set1_epi16(0);
					__m128i ditheruu =  _mm_set1_epi16(0);
					__m128i dithervv =  _mm_set1_epi16(0);

					__m128i overflowprotectRGB_epi16 = _mm_set1_epi16(0x7fff-0x1fff);


					for(lines=linenum; lines<linenum+height; lines++)
					{
						__m128i *out_epi8 = (__m128i *)output;\
						int width16 = (width >> 4) << 4;
						outA8 = output;

						if(cg2vs)
						{
							ConvertCGRGBtoVSRGB((PIXEL *)sptr, width, whitepoint, flags);
						}

						if(lines & 1)
						{
							ditheryy = _mm_set_epi16( 1, 15,  3, 13,  5, 11,  7,  9); // 5 bits of dither
							ditheruu = _mm_set_epi16( 1, 15,  3, 13,  5, 11,  7,  9);
							dithervv = _mm_set_epi16( 9,  7, 11,  5, 13,  3, 15,  1);
						}
						else
						{
							ditheryy = _mm_set_epi16( 9,  7, 11,  5, 13,  3, 15,  1);
							ditheruu = _mm_set_epi16( 9,  7, 11,  5, 13,  3, 15,  1);
							dithervv = _mm_set_epi16( 1, 15,  3, 13,  5, 11,  7,  9);
						}

						for(x=0; x<width16; x+=16)
						{
							if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
							{
								rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[0]);
								gggggggg = _mm_loadu_si128((__m128i *)&sptr[8]);
								bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[16]);
								sptr+=24;
							}
							else if(flags & ACTIVEMETADATA_PLANAR)
							{
								rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[0]);
								gggggggg = _mm_loadu_si128((__m128i *)&sptr[width]);
								bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[width*2]);
								sptr+=8;
							}
							else
							{
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[0],  0);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[1],  0);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[2],  0);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[3],  1);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[4],  1);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[5],  1);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[6],  2);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[7],  2);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[8],  2);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[9],  3);
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
								sptr+=24;
							}

							if(dnshiftto13bit < 0)
							{
								rrrrrrrr = _mm_slli_epi16(rrrrrrrr, -dnshiftto13bit);		//13-bit
								gggggggg = _mm_slli_epi16(gggggggg, -dnshiftto13bit);
								bbbbbbbb = _mm_slli_epi16(bbbbbbbb, -dnshiftto13bit);
							}
							else if(whitepoint == 16)
							{
								rrrrrrrr = _mm_srli_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
								gggggggg = _mm_srli_epi16(gggggggg, dnshiftto13bit);
								bbbbbbbb = _mm_srli_epi16(bbbbbbbb, dnshiftto13bit);
							}
							else
							{
								rrrrrrrr = _mm_srai_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
								gggggggg = _mm_srai_epi16(gggggggg, dnshiftto13bit);
								bbbbbbbb = _mm_srai_epi16(bbbbbbbb, dnshiftto13bit);
							}

							if(saturate)
							{
								rrrrrrrr = _mm_adds_epi16(rrrrrrrr, overflowprotectRGB_epi16);
								rrrrrrrr = _mm_subs_epu16(rrrrrrrr, overflowprotectRGB_epi16);

								gggggggg = _mm_adds_epi16(gggggggg, overflowprotectRGB_epi16);
								gggggggg = _mm_subs_epu16(gggggggg, overflowprotectRGB_epi16);

								bbbbbbbb = _mm_adds_epi16(bbbbbbbb, overflowprotectRGB_epi16);
								bbbbbbbb = _mm_subs_epu16(bbbbbbbb, overflowprotectRGB_epi16);
							}

							yyyyyyyy1 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(y_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(y_gmult));
							yyyyyyyy1 = _mm_adds_epi16(yyyyyyyy1, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(y_bmult));
							yyyyyyyy1 = _mm_adds_epi16(yyyyyyyy1, tttttttt);
							yyyyyyyy1 = _mm_adds_epi16(yyyyyyyy1, ditheryy);
							yyyyyyyy1 = _mm_srai_epi16(yyyyyyyy1, 4);
							yyyyyyyy1 = _mm_adds_epi16(yyyyyyyy1, _mm_set1_epi16(yoffset));

							uuuuuuuu1 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(u_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(u_gmult));
							uuuuuuuu1 = _mm_adds_epi16(uuuuuuuu1, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(u_bmult));
							uuuuuuuu1 = _mm_adds_epi16(uuuuuuuu1, tttttttt);
							uuuuuuuu1 = _mm_adds_epi16(uuuuuuuu1, ditheruu);
							uuuuuuuu1 = _mm_srai_epi16(uuuuuuuu1, 4);
							uuuuuuuu1 = _mm_adds_epi16(uuuuuuuu1, _mm_set1_epi16(128));

							vvvvvvvv1 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(v_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(v_gmult));
							vvvvvvvv1 = _mm_adds_epi16(vvvvvvvv1, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(v_bmult));
							vvvvvvvv1 = _mm_adds_epi16(vvvvvvvv1, tttttttt);
							vvvvvvvv1 = _mm_adds_epi16(vvvvvvvv1, dithervv);
							vvvvvvvv1 = _mm_srai_epi16(vvvvvvvv1, 4);
							vvvvvvvv1 = _mm_adds_epi16(vvvvvvvv1, _mm_set1_epi16(128));


							if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
							{
								rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[0]);
								gggggggg = _mm_loadu_si128((__m128i *)&sptr[8]);
								bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[16]);
								sptr+=24;
							}
							else if(flags & ACTIVEMETADATA_PLANAR)
							{
								rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[0]);
								gggggggg = _mm_loadu_si128((__m128i *)&sptr[width]);
								bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[width*2]);
								sptr+=8;
							}
							else
							{
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[0],  0);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[1],  0);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[2],  0);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[3],  1);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[4],  1);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[5],  1);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[6],  2);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[7],  2);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[8],  2);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[9],  3);
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
								sptr+=24;
							}

							if(dnshiftto13bit < 0)
							{
								rrrrrrrr = _mm_slli_epi16(rrrrrrrr, -dnshiftto13bit);		//13-bit
								gggggggg = _mm_slli_epi16(gggggggg, -dnshiftto13bit);
								bbbbbbbb = _mm_slli_epi16(bbbbbbbb, -dnshiftto13bit);
							}
							else if(whitepoint == 16)
							{
								rrrrrrrr = _mm_srli_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
								gggggggg = _mm_srli_epi16(gggggggg, dnshiftto13bit);
								bbbbbbbb = _mm_srli_epi16(bbbbbbbb, dnshiftto13bit);
							}
							else
							{
								rrrrrrrr = _mm_srai_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
								gggggggg = _mm_srai_epi16(gggggggg, dnshiftto13bit);
								bbbbbbbb = _mm_srai_epi16(bbbbbbbb, dnshiftto13bit);
							}

							if(saturate)
							{
								rrrrrrrr = _mm_adds_epi16(rrrrrrrr, overflowprotectRGB_epi16);
								rrrrrrrr = _mm_subs_epu16(rrrrrrrr, overflowprotectRGB_epi16);

								gggggggg = _mm_adds_epi16(gggggggg, overflowprotectRGB_epi16);
								gggggggg = _mm_subs_epu16(gggggggg, overflowprotectRGB_epi16);

								bbbbbbbb = _mm_adds_epi16(bbbbbbbb, overflowprotectRGB_epi16);
								bbbbbbbb = _mm_subs_epu16(bbbbbbbb, overflowprotectRGB_epi16);
							}

							yyyyyyyy2 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(y_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(y_gmult));
							yyyyyyyy2 = _mm_adds_epi16(yyyyyyyy2, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(y_bmult));
							yyyyyyyy2 = _mm_adds_epi16(yyyyyyyy2, tttttttt);
							yyyyyyyy2 = _mm_adds_epi16(yyyyyyyy2, ditheryy);
							yyyyyyyy2 = _mm_srai_epi16(yyyyyyyy2, 4);
							yyyyyyyy2 = _mm_adds_epi16(yyyyyyyy2, _mm_set1_epi16(yoffset));

							uuuuuuuu2 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(u_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(u_gmult));
							uuuuuuuu2 = _mm_adds_epi16(uuuuuuuu2, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(u_bmult));
							uuuuuuuu2 = _mm_adds_epi16(uuuuuuuu2, tttttttt);
							uuuuuuuu2 = _mm_adds_epi16(uuuuuuuu2, ditheruu);
							uuuuuuuu2 = _mm_srai_epi16(uuuuuuuu2, 4);
							uuuuuuuu2 = _mm_adds_epi16(uuuuuuuu2, _mm_set1_epi16(128));

							vvvvvvvv2 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(v_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(v_gmult));
							vvvvvvvv2 = _mm_adds_epi16(vvvvvvvv2, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(v_bmult));
							vvvvvvvv2 = _mm_adds_epi16(vvvvvvvv2, tttttttt);
							vvvvvvvv2 = _mm_adds_epi16(vvvvvvvv2, dithervv);
							vvvvvvvv2 = _mm_srai_epi16(vvvvvvvv2, 4);
							vvvvvvvv2 = _mm_adds_epi16(vvvvvvvv2, _mm_set1_epi16(128));



							y_epi8 = _mm_packus_epi16(yyyyyyyy1, yyyyyyyy2); //pack to 8-bit
							u_epi8 = _mm_packus_epi16(uuuuuuuu1, uuuuuuuu2); //pack to 8-bit
							v_epi8 = _mm_packus_epi16(vvvvvvvv1, vvvvvvvv2); //pack to 8-bit

							if(format == COLOR_FORMAT_V408) // UYVA
							{
								__m128i UY,VA,UYVA;

								UY = _mm_unpacklo_epi8(u_epi8, y_epi8);
								VA = _mm_unpacklo_epi8(v_epi8, a_epi8);
								UYVA = _mm_unpacklo_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

								UYVA = _mm_unpackhi_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

								UY = _mm_unpackhi_epi8(u_epi8, y_epi8);
								VA = _mm_unpackhi_epi8(v_epi8, a_epi8);
								UYVA = _mm_unpacklo_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

								UYVA = _mm_unpackhi_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);
							}
							else //r408 AYUV
							{
								__m128i AY,UV,AYUV;

								y_epi8 = _mm_subs_epu8(y_epi8, offsetR408_epi8);

								AY = _mm_unpacklo_epi8(a_epi8, y_epi8);
								UV = _mm_unpacklo_epi8(u_epi8, v_epi8);
								AYUV = _mm_unpacklo_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);

								AYUV = _mm_unpackhi_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);

								AY = _mm_unpackhi_epi8(a_epi8, y_epi8);
								UV = _mm_unpackhi_epi8(u_epi8, v_epi8);
								AYUV = _mm_unpacklo_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);

								AYUV = _mm_unpackhi_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);
							}
						}

						for(; x<width; x+=4)
						{
							//TODO not fill with black.
							if(format == COLOR_FORMAT_V408) // UYVA
							{
								__m128i UY,VA,UYVA;


								UY = _mm_unpacklo_epi8(_mm_set1_epi8(128), _mm_set1_epi8(16));
								VA = _mm_unpacklo_epi8(_mm_set1_epi8(128), _mm_set1_epi8(255));
								UYVA = _mm_unpacklo_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

							}
							else //r408 AYUV
							{
								__m128i UY,VA,UYVA;

								UY = _mm_unpacklo_epi8(_mm_set1_epi8(128), _mm_set1_epi8(0));
								VA = _mm_unpacklo_epi8(_mm_set1_epi8(128), _mm_set1_epi8(255));
								UYVA = _mm_unpacklo_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);
							}
						}

						if(flags & ACTIVEMETADATA_PLANAR)
						{
							sptr += width*2;
						}
						output += pitch;
					}
	#else
					assert(0) // old code disabled
#endif
				}
			}
			break;

		// Currently only supporting ROW16U YUV 4:2:2 sources

		case COLOR_FORMAT_CbYCrY_10bit_2_8:
			if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
				assert(0); // only RGB output should use ACTIVEMETADATA_SRC_8PIXEL_PLANAR
		
			if(upshiftto16bit)
					UpShift16(src, width*3, upshiftto16bit, 1);

			if(!(flags & ACTIVEMETADATA_COLORFORMATDONE))
			{
	
				if(flags & ACTIVEMETADATA_PLANAR)
					PlanarRGB16toPlanarYUV16(src, src, width, colorspace);
				else
					ChunkyRGB16toChunkyYUV16(src, src, width, colorspace); 
			}

			ConvertYUV16ToCbYCrY_10bit_2_8(decoder, width, height, linenum, src,
											output, pitch, format, whitepoint, flags);
			break;

		case COLOR_FORMAT_CbYCrY_16bit_2_14:
			if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
				assert(0); // only RGB output should use ACTIVEMETADATA_SRC_8PIXEL_PLANAR
		
			if(upshiftto16bit)
					UpShift16(src, width*3, upshiftto16bit, 1);

			if(!(flags & ACTIVEMETADATA_COLORFORMATDONE))
			{
	
				if(flags & ACTIVEMETADATA_PLANAR)
					PlanarRGB16toPlanarYUV16(src, src, width, colorspace);
				else
					ChunkyRGB16toChunkyYUV16(src, src, width, colorspace); 
			}

			ConvertYUV16ToCbYCrY_16bit_2_14(decoder, width, height, linenum, src,
											 output, pitch, format, whitepoint, flags);
			break;

		case COLOR_FORMAT_CbYCrY_16bit_10_6:
			if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
				assert(0); // only RGB output should use ACTIVEMETADATA_SRC_8PIXEL_PLANAR
		
			if(upshiftto16bit)
					UpShift16(src, width*3, upshiftto16bit, 1);

			if(!(flags & ACTIVEMETADATA_COLORFORMATDONE))
			{
	
				if(flags & ACTIVEMETADATA_PLANAR)
					PlanarRGB16toPlanarYUV16(src, src, width, colorspace);
				else
					ChunkyRGB16toChunkyYUV16(src, src, width, colorspace); 
			}

			ConvertYUV16ToCbYCrY_16bit_10_6(decoder, width, height, linenum, src,
											 output, pitch, format, whitepoint, flags);
			break;

		case COLOR_FORMAT_CbYCrY_8bit:
			if (flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR) {
				// only RGB output should use ACTIVEMETADATA_SRC_8PIXEL_PLANAR
				assert(0);
			}
			
			// Need to convert more than one row?
			for (row = 0; row < height; row++)
			{
				unsigned short *src2 = &src[row * width * 3];

				if (upshiftto16bit) {
					UpShift16(src2, width*3, upshiftto16bit, 1);
				}

				if (!(flags & ACTIVEMETADATA_COLORFORMATDONE))
				{
					// Perform inplace conversion to YUV

					if(flags & ACTIVEMETADATA_PLANAR)
					{
						// Input is unpacked pixels
						PlanarRGB16toPlanarYUV16(src2, src2, width, colorspace);
					}
					else
					{
						// Input is packed pixels
						ChunkyRGB16toChunkyYUV16(src2, src2, width, colorspace);
					}
				}
			}

			ConvertYUV16ToCbYCrY_8bit(decoder, width, height, linenum, src,
									  output, pitch, format, whitepoint, flags,
									  rgb2yuv_i, yoffset);
			break;

		case COLOR_FORMAT_CbYCrY_16bit:	
			if (flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR) {
				// only RGB output should use ACTIVEMETADATA_SRC_8PIXEL_PLANAR
				assert(0);
			}
			
			// Need to convert more than one row?
			for (row = 0; row < height; row++)
			{
				unsigned short *src2 = &src[row * width * 3];

				if (upshiftto16bit) {
					UpShift16(src2, width*3, upshiftto16bit, 1);
				}

				if (!(flags & ACTIVEMETADATA_COLORFORMATDONE))
				{
					if (flags & ACTIVEMETADATA_PLANAR)
					{
						// Input is unpacked pixels
						PlanarRGB16toPlanarYUV16(src2, src2, width, colorspace);
					}
					else
					{
						// Input is packed pixels
						ChunkyRGB16toChunkyYUV16(src2, src2, width, colorspace);
					}
				}
			}

			ConvertYUV16ToCbYCrY_16bit(decoder, width, height, linenum, src,
										output, pitch, format, whitepoint, flags);
			break;

		case COLOR_FORMAT_NV12:
			if (flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR) {
				// only RGB output should use ACTIVEMETADATA_SRC_8PIXEL_PLANAR
				assert(0);
			}
			
			// Need to convert more than one row?
			for (row = 0; row < height; row++)
			{
				unsigned short *src2 = &src[row * width * 3];

				if (upshiftto16bit) {
					UpShift16(src2, width*3, upshiftto16bit, 1);
				}

				if (!(flags & ACTIVEMETADATA_COLORFORMATDONE))
				{
					// Perform inplace conversion to YUV

					if(flags & ACTIVEMETADATA_PLANAR)
					{
						// Input is unpacked pixels
						PlanarRGB16toPlanarYUV16(src2, src2, width, colorspace);
					}
					else
					{
						// Input is packed pixels
						ChunkyRGB16toChunkyYUV16(src2, src2, width, colorspace);
					}
				}
			}

			ConvertYUV16ToNV12(decoder, width, height, linenum, src,
							   output, pitch, format, whitepoint, flags);
			break;

		case COLOR_FORMAT_YV12:
			if (flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR) {
				// only RGB output should use ACTIVEMETADATA_SRC_8PIXEL_PLANAR
				assert(0);
			}
			
			// Need to convert more than one row?
			for (row = 0; row < height; row++)
			{
				unsigned short *src2 = &src[row * width * 3];

				if (upshiftto16bit) {
					UpShift16(src2, width*3, upshiftto16bit, 1);
				}

				if (!(flags & ACTIVEMETADATA_COLORFORMATDONE))
				{
					// Perform inplace conversion to YUV

					if(flags & ACTIVEMETADATA_PLANAR)
					{
						// Input is unpacked pixels
						PlanarRGB16toPlanarYUV16(src2, src2, width, colorspace);
					}
					else
					{
						// Input is packed pixels
						ChunkyRGB16toChunkyYUV16(src2, src2, width, colorspace);
					}
				}
			}

			ConvertYUV16ToYV12(decoder, width, height, linenum, src,
							   output, pitch, format, whitepoint, flags);
			break;

		default:
			assert(0);
			break;
	}
}



#define CUBE_BASE		6						//4 best 6 (64*64*64)
#define CUBE_DEPTH		((1<<CUBE_BASE))		//17
#define CUBE_SHIFT_DN	(16-CUBE_BASE)			//12
#define CUBE_DEPTH_MASK	((1<<CUBE_SHIFT_DN)-1)	//(1<<10)-1 = 0x3ff


bool NeedCube(DECODER *decoder)
{
	//int coordbase = 0;
	//int matrix_non_unity = 0;
	//int wb_non_unity = 0;
	int cg_non_unity = 0;
	int curve_change = 0;
	float linear_mtrx[3][4] =
	{
        {1.0,  0,   0,   0},
        {0,  1.0,   0,   0},
        {0,    0, 1.0,   0}
	};
	float curved_mtrx[3][4] =
	{
        {1.0,  0,   0,   0},
        {0,  1.0,   0,   0},
        {0,    0, 1.0,   0}
	};
	float whitebalance[3] = { 1.0, 1.0, 1.0 };
	bool useLUT = false;
	CFHDDATA *cfhddata = &decoder->cfhddata;
	unsigned int process_path_flags = cfhddata->process_path_flags;
	//int colorformat = decoder->frame.format;
	//int colorspace = decoder->frame.colorspace;
	bool retcode = false;
	float encode_curvebase = 90.0;
	float decode_curvebase = 90.0;
	float red_gamma_tweak;
	float grn_gamma_tweak;
	float blu_gamma_tweak;
	float contrast;
	int encode_curve_type = cfhddata->encode_curve >> 16;
	int decode_curve_type = cfhddata->decode_curve >> 16;
	int encode_curve = cfhddata->encode_curve;
	int decode_curve = cfhddata->decode_curve;
	int linear_matrix_non_unity = 0;
	int curved_matrix_non_unity = 0;
	float cdl_sat = cfhddata->channel[decoder->channel_current+1].user_cdl_sat;
	float highlight_start = cfhddata->channel[0].user_highlight_point + 1.0f;
	int i,j;

/*	if(decoder->codec.encoded_format >= ENCODED_FORMAT_RGBA_4444 && decoder->codec.num_channels >= 4)
	{
		decoder->frame.white_point = 0;
		//decoder->frame.signed_pixels = 0;
		decoder->use_active_metadata_decoder = false;
		decoder->apply_color_active_metadata = false;
		return false;
	} */

	memcpy(&decoder->Cube_cfhddata.FileTimecodeData, &decoder->cfhddata.FileTimecodeData, sizeof(AVIFileMetaData2));
	if(	0==memcmp(&decoder->Cube_cfhddata, &decoder->cfhddata, sizeof(CFHDDATA)) &&
		decoder->Cube_format == decoder->frame.format &&
		decoder->Cube_output_colorspace == decoder->frame.colorspace)
	{
		return decoder->use_active_metadata_decoder;
	}

	if(cfhddata->process_path_flags_mask)
	{
		process_path_flags &= cfhddata->process_path_flags_mask;
		if((cfhddata->process_path_flags_mask & 0xffff) == 7) // CM+WB+ACTIVE hack to force CM on
		{
			process_path_flags |= PROCESSING_COLORMATRIX|PROCESSING_ACTIVE;  // DAN20080225
		}
	}


	if(encode_curve_type) //1 or 2
	{
		if(encode_curve_type & CURVE_TYPE_EXTENDED)
			encode_curvebase = (float)(cfhddata->encode_curve & 0xffff); // use all 16-bits for larger log bases
		else
			encode_curvebase = (float)((cfhddata->encode_curve >> 8) & 0xff) / (float)(cfhddata->encode_curve & 0xff);
	}
	else
	{
		encode_curve_type = CURVE_TYPE_LOG;
		encode_curvebase = 90.0f;

		if(cfhddata->cfhd_subtype > 1) //444+
		{
			encode_curve_type = CURVE_TYPE_GAMMA;
			encode_curvebase = 2.2f;
		}
	}

	if(decode_curve_type) //1 or 2
	{	
		if(decode_curve_type & CURVE_TYPE_EXTENDED)
			decode_curvebase = (float)(cfhddata->decode_curve & 0xffff); // use all 16-bits for larger log bases
		else
			decode_curvebase = (float)((cfhddata->decode_curve >> 8) & 0xff) / (float)(cfhddata->decode_curve & 0xff);
	}
	else
	{
		decode_curve = encode_curve;
		decode_curve_type = encode_curve_type;
		decode_curvebase = encode_curvebase;
	}

	if(encode_curvebase == 1.0 && encode_curve_type <= CURVE_TYPE_LINEAR)
		encode_curve_type = CURVE_TYPE_LINEAR;


	if(cfhddata->version >= 5 && process_path_flags == 0)
	{
		process_path_flags = PROCESSING_ACTIVE;
		if(useLUT)
			process_path_flags = PROCESSING_ACTIVE | PROCESSING_WHITEBALANCE | PROCESSING_LOOK_FILE;
		else
			process_path_flags = PROCESSING_ACTIVE | PROCESSING_WHITEBALANCE | PROCESSING_COLORMATRIX;
	}


	if(cfhddata->MagicNumber == CFHDDATA_MAGIC_NUMBER && cfhddata->version >= 2)
	{
		if(process_path_flags & PROCESSING_COLORMATRIX)
		{
			for(i=0; i<12; i++)
			{
				switch(cfhddata->use_base_matrix)
				{
					case 0: //unity
						// already initized
						break;
					case 1: //original camera matrix
						linear_mtrx[i>>2][i&3] = cfhddata->orig_colormatrix[i>>2][i&3];
						break;
					case 2: //custom matrix
						linear_mtrx[i>>2][i&3] = cfhddata->custom_colormatrix[i>>2][i&3];
						break;
				}
			}
		}

		if(cfhddata->version >= 5)
		{
			if(cfhddata->channel[decoder->channel_current+1].white_balance[0] > 0.0)
			{
				whitebalance[0] = cfhddata->channel[decoder->channel_current+1].white_balance[0];
				whitebalance[1] = cfhddata->channel[decoder->channel_current+1].white_balance[1];
				whitebalance[2] = cfhddata->channel[decoder->channel_current+1].white_balance[2];

				if(whitebalance[0] < 0.4f) whitebalance[0] = 0.4f;
				if(whitebalance[1] < 0.4f) whitebalance[1] = 0.4f;
				if(whitebalance[2] < 0.4f) whitebalance[2] = 0.4f;

#if 0
				if(whitebalance[0] < 1.0)
				{
					whitebalance[1] /= whitebalance[0];
					whitebalance[2] /= whitebalance[0];
					whitebalance[0] = 1.0;
				}
				if(whitebalance[1] < 1.0)
				{
					whitebalance[0] /= whitebalance[1];
					whitebalance[2] /= whitebalance[1];
					whitebalance[1] = 1.0;
				}
				if(whitebalance[2] < 1.0)
				{
					whitebalance[0] /= whitebalance[2];
					whitebalance[1] /= whitebalance[2];
					whitebalance[2] = 1.0;
				}
#endif
				if(whitebalance[0] > 10.0) whitebalance[0] = 10.0;
				if(whitebalance[1] > 10.0) whitebalance[1] = 10.0;
				if(whitebalance[2] > 10.0) whitebalance[2] = 10.0;

			}
		}
	}

	if(process_path_flags & PROCESSING_COLORMATRIX)
	{
		float desatMatrix[3][4] =
        {	{0.309f, 0.609f, 0.082f, 0.0},
            {0.309f, 0.609f, 0.082f, 0.0},
            {0.309f, 0.609f, 0.082f, 0.0} };
		float fullsatMatrix[3][4] =
        {   {4.042f, -2.681f, -0.361f, 0.0},
            {-1.358f, 2.719f, -0.361f, 0.0},
            {-1.358f, -2.681f, 5.039f, 0.0} };
		float sat = cfhddata->channel[decoder->channel_current+1].user_saturation + 1.0f;
		float exposure = cfhddata->channel[decoder->channel_current+1].user_exposure + 1.0f;

        //saturation
        for (i = 0; i < 3; i++)
        {
            for (j = 0; j < 3; j++)
            {
                if (sat < 1.0f)
                {
                    linear_mtrx[i][j] = ((1.0f - sat) * (desatMatrix[i][j]) + (sat) * (linear_mtrx[i][j]));
                }
                else if (sat > 1.0f)
                {
                    linear_mtrx[i][j] = (((sat - 1.0f) / 3.0f) * (fullsatMatrix[i][j]) + ((4.0f - sat) / 3.0f) * (linear_mtrx[i][j]));
                }
            }
        }

		if(cfhddata->PrimariesUseDecodeCurve == 1)
		{
			//r,g,b gains and black level
			for (i = 0; i < 3; i++)
			{
				curved_mtrx[i][0] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
				curved_mtrx[i][1] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
				curved_mtrx[i][2] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
				curved_mtrx[i][3] += cfhddata->channel[decoder->channel_current+1].user_rgb_lift[i];
			}
		}
		else
		{
			//r,g,b gains and black level
			for (i = 0; i < 3; i++)
			{
				linear_mtrx[i][0] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
				linear_mtrx[i][1] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
				linear_mtrx[i][2] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
				linear_mtrx[i][3] += cfhddata->channel[decoder->channel_current+1].user_rgb_lift[i];
			}
		}

		if(exposure != 1.0)
		{
			for (i = 0; i < 4; i++)
			{
				linear_mtrx[0][i] *= exposure;
				linear_mtrx[1][i] *= exposure;
				linear_mtrx[2][i] *= exposure;
			}
		}
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			for (j = 0; j < 4; j++)
			{
				if(i == j)
					linear_mtrx[i][j] = 1.0;
				else
					linear_mtrx[i][j] = 0.0;
			}
		}
	}

	if(process_path_flags & PROCESSING_WHITEBALANCE)
	{
        for (j = 0; j < 3; j++)
        {
			linear_mtrx[0][j] *= whitebalance[j];
			linear_mtrx[1][j] *= whitebalance[j];
			linear_mtrx[2][j] *= whitebalance[j];
        }

		//DAN20120802 -- This allows the custom color matrix black levels to work with white_balance
		for (j = 0; j < 3; j++)
        {
			linear_mtrx[j][3] *= whitebalance[j];
		}
	}

	for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 4; j++)
        {
			if(i == j)
			{
				if(linear_mtrx[i][j] != 1.0)
				{
					linear_matrix_non_unity = 1;
				}
				if(curved_mtrx[i][j] != 1.0)
				{
					curved_matrix_non_unity = 1;
				}
			}
			else
			{
				if(linear_mtrx[i][j] != 0.0)
				{
					linear_matrix_non_unity = 1;
				}
				if(curved_mtrx[i][j] != 0.0)
				{
					curved_matrix_non_unity = 1;
				}
			}
		}
	}


	if(cdl_sat != 0.0)
		linear_matrix_non_unity = 1;

	if(highlight_start < 1.0)
		linear_matrix_non_unity = 1;
		
	red_gamma_tweak = cfhddata->channel[decoder->channel_current+1].user_rgb_gamma[0];
	grn_gamma_tweak = cfhddata->channel[decoder->channel_current+1].user_rgb_gamma[1];
	blu_gamma_tweak = cfhddata->channel[decoder->channel_current+1].user_rgb_gamma[2];
	contrast = cfhddata->channel[decoder->channel_current+1].user_contrast + 1.0f;

	if(red_gamma_tweak == 0.0) red_gamma_tweak = 1.0f;
	if(grn_gamma_tweak == 0.0) grn_gamma_tweak = 1.0f;
	if(blu_gamma_tweak == 0.0) blu_gamma_tweak = 1.0f;

	if(!(process_path_flags & PROCESSING_GAMMA_TWEAKS))
	{
		red_gamma_tweak = 1.0f;
		grn_gamma_tweak = 1.0f;
		blu_gamma_tweak = 1.0f;
		contrast = 1.0f;
	}

	if(	red_gamma_tweak != 1.0f ||
		grn_gamma_tweak != 1.0f ||
		blu_gamma_tweak != 1.0f ||
		contrast != 1.0f)
	{
		cg_non_unity = 1;
	}


	if(process_path_flags & PROCESSING_LOOK_FILE)
	{
		if(cfhddata->user_look_CRC != 0)
		{
			useLUT = 1;
		}
		else
		{
			useLUT = 0;
		}
	}
	else
	{
		useLUT = 0;
	}

	if(	(decode_curve_type != encode_curve_type) ||
		(decode_curvebase != encode_curvebase) ||
		(decoder->frame.white_point != 16 && decoder->frame.white_point != 0) )
		curve_change = 1;



	if(1 && (useLUT || linear_matrix_non_unity || curved_matrix_non_unity || cg_non_unity || curve_change))
	{
		retcode = true; //use the 3D system
	}
	else
	{
		retcode = false; //don't need to use the cube
		if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
			retcode = true;
	}

	return retcode;
}






void BuildLUTCurves(DECODER *decoder, int unit, int max_units)
{
	int j;
	float *redgammatweak = decoder->redgammatweak;
	float *grngammatweak = decoder->grngammatweak;
	float *blugammatweak = decoder->blugammatweak;
	float contrast = decoder->contrast;
	float red_gamma_tweak = decoder->red_gamma_tweak;
	float grn_gamma_tweak = decoder->grn_gamma_tweak;
	float blu_gamma_tweak = decoder->blu_gamma_tweak;
	int work = 512+2048+1; //-512; j<=2048; j++) // -1 to +4
	//int workPerUnit = work / max_units;
	int start = -512 + (unit * work) / max_units;
	int end =  -512 + ((unit+1) * work) / max_units;

	// This data CAN change per keyframe
	if(decoder->cg_non_unity)
	{
		for(j=start; j<end; j++) // -1 to +4
		{
			float value;
			if(red_gamma_tweak != 1.0 || contrast != 1.0)
			{
				value = CURVE_LIN2GAM((float)j/512.0,red_gamma_tweak);
				if(contrast != 1.0)
					value = calc_contrast(value,contrast);

				if(value<-1.0) value=-1.0;
				if(value>4.0) value=4.0;
				redgammatweak[j+512] = value;
			}
		}

		for(j=start; j<end; j++) // -1 to +4
		{
			float value;
			if(grn_gamma_tweak != 1.0 || contrast != 1.0)
			{
				value = CURVE_LIN2GAM((float)j/512.0,grn_gamma_tweak);
				if(contrast != 1.0)
					value = calc_contrast(value,contrast);

				if(value<-1.0) value=-1.0;
				if(value>4.0) value=4.0;
				grngammatweak[j+512] = value;
			}
		}

		for(j=start; j<end; j++) // -1 to +4
		{
			float value;
			if(blu_gamma_tweak != 1.0 || contrast != 1.0)
			{
				value = CURVE_LIN2GAM((float)j/512.0,blu_gamma_tweak);
				if(contrast != 1.0)
					value = calc_contrast(value,contrast);

				if(value<-1.0) value=-1.0;
				if(value>4.0) value=4.0;
				blugammatweak[j+512] = value;
			}
		}
	}

	return;
}

void DoBuildLUTCurves(DECODER *decoder, int thread_index, int max_units)
{
	THREAD_ERROR error;

	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			BuildLUTCurves(decoder, work_index, max_units);
		}
		else
		{
			// No more work to do
			return;
		}
	}
}



void BuildCube(DECODER *decoder, int unit, int max_units)
{
	int r,g,b;
	int cube_depth = decoder->cube_depth;
	float *curve2lin = decoder->curve2lin;
	float *lin2curve = decoder->lin2curve;
	float *redgammatweak = decoder->redgammatweak;
	float *grngammatweak = decoder->grngammatweak;
	float *blugammatweak = decoder->blugammatweak;
	float contrast = decoder->contrast;
	float cdl_sat = decoder->cdl_sat;
	float red_gamma_tweak = decoder->red_gamma_tweak;
	float grn_gamma_tweak = decoder->grn_gamma_tweak;
	float blu_gamma_tweak = decoder->blu_gamma_tweak;
	float linear_mtrx[3][4];
	//float linear_mtrx_highlight_sat[3][4];
	float curved_mtrx[3][4];
	//int convert2YUV = decoder->convert2YUV;
	//int broadcastLimit = decoder->broadcastLimit;
	int useLUT = decoder->useLUT;
	float *LUT = decoder->LUT;
	int LUTsize = decoder->LUTsize;
	float LUTscale = ((float)(decoder->LUTsize-1)) - 0.00001f;
	CFHDDATA *cfhddata = &decoder->cfhddata;
	//int colorformat = decoder->frame.format;
	//int colorspace = decoder->frame.colorspace;
	short *RawCube = decoder->RawCube;
	int coordbase = 0;
	int change = decoder->linear_matrix_non_unity || decoder->curved_matrix_non_unity || decoder->cg_non_unity || decoder->curve_change || decoder->cdl_sat != 0.0;

	int work = cube_depth+1; //b=0;b<cube_depth+1;b++)
	//int workPerUnit = work / max_units;
	int start = 0 + (unit * work) / max_units;
	int end =  0 + ((unit+1) * work) / max_units;
	float highlight_start = cfhddata->channel[0].user_highlight_point + 1.0f;
	//float highlight_end = highlight_start * 1.5f;
	//float mixgain = 1.0f / (highlight_end - highlight_start + 0.0001f);
	int step = 0;
	float wbr = 1.0f,wbg = 1.0f,wbb = 1.0f;
	if(highlight_start > 0.99f) highlight_start = 100;

	if(highlight_start < 1.0)
	{
		wbr = decoder->highlight_desat_gains[0];
		wbg = decoder->highlight_desat_gains[1];
		wbb = decoder->highlight_desat_gains[2];
	}

	memcpy(linear_mtrx, decoder->linear_mtrx, sizeof(linear_mtrx));
	//memcpy(linear_mtrx_highlight_sat, decoder->linear_mtrx_highlight_sat, sizeof(linear_mtrx));
	memcpy(curved_mtrx, decoder->curved_mtrx, sizeof(curved_mtrx));

	if(cube_depth == 32)
		step = 1;

	for(b=start;b<end;b++)
	{
		for(g=0;g<cube_depth+1;g++)
		{
			coordbase = b * (cube_depth+1)*(cube_depth+1)*3 + g * (cube_depth+1)*3;
			for(r=0;r<cube_depth+1;r++)
			{
				int ri,gi,bi;
				float rs,gs,bs,ys,rf,gf,bf;
				int entry;
				float mix;

				if(change)
				{
					if(decoder->linear_matrix_non_unity)
					{
						float a,rn,gn,bn;
						rs = rn = curve2lin[r<<step] * (1.0f - (2.0f/cube_depth));
						gs = gn = curve2lin[g<<step] * (1.0f - (2.0f/cube_depth));
						bs = bn = curve2lin[b<<step] * (1.0f - (2.0f/cube_depth));	

						if(highlight_start < 1.0)
						{
							if(rs > highlight_start && gs > highlight_start*highlight_start && bs > highlight_start*highlight_start)
							{
								a = (rs - highlight_start)/(1.0f - highlight_start);
								rn = (1.0f-a)*rs  + a*(gs*0.85f+bs*0.15f)*wbr;
							}
							if(gs > highlight_start && rs > highlight_start*highlight_start && bs > highlight_start*highlight_start)
							{
								a = (gs - highlight_start)/(1.0f - highlight_start);
								gn = (1.0f-a)*gs  + a*(rs*0.65f + bs*0.35f)*wbg;
							}
							if(bs > highlight_start && gs > highlight_start*highlight_start && rs > highlight_start*highlight_start)
							{
								a = (bs - highlight_start)/(1.0f - highlight_start);
								bn = (1.0f-a)*bs  + a*(rs*0.2f + gs*0.8f)*wbb;
							}
							rs = rn;
							gs = gn;
							bs = bn;
						}
						
						if((linear_mtrx[0][1] * gs + linear_mtrx[0][2] * bs) < -1.0 && rs > 0.8f)
						{
							float weight = (-1.0f - (linear_mtrx[0][1] * gs + linear_mtrx[0][2] * bs)) * (rs - 0.8f) * 5.0f; if(weight > 1.0f) weight = 1.0f;

							rf = (linear_mtrx[0][0] * rs + linear_mtrx[0][3]) * weight + 
								 (linear_mtrx[0][0] * rs + linear_mtrx[0][1] * gs + linear_mtrx[0][2] * bs + linear_mtrx[0][3]) * (1.0f - weight);
						}
						else
							rf = linear_mtrx[0][0] * rs + linear_mtrx[0][1] * gs + linear_mtrx[0][2] * bs + linear_mtrx[0][3];	
						
						if((linear_mtrx[1][0] * rs + linear_mtrx[1][2] * bs) < -1.0f && gs > 0.8f)
						{
							float weight = (-1.0f - (linear_mtrx[1][0] * rs + linear_mtrx[1][2] * bs)) * (gs - 0.8f) * 5.0f; if(weight > 1.0f) weight = 1.0f;

							gf = (linear_mtrx[1][1] * gs + linear_mtrx[1][3]) * weight + 
								 (linear_mtrx[1][0] * rs + linear_mtrx[1][1] * gs + linear_mtrx[1][2] * bs + linear_mtrx[1][3]) * (1.0f - weight);
						}
						else
							gf = linear_mtrx[1][0] * rs + linear_mtrx[1][1] * gs + linear_mtrx[1][2] * bs + linear_mtrx[1][3];

						if((linear_mtrx[2][0] * rs + linear_mtrx[2][1] * gs) < -1.0f && bs > 0.8f)
						{
							float weight = (-1.0f - (linear_mtrx[2][0] * rs + linear_mtrx[2][1] * gs)) * (bs - 0.8f) * 5.0f; if(weight > 1.0f) weight = 1.0f;

							bf = (linear_mtrx[2][2] * bs + linear_mtrx[2][3]) * weight + 
								 (linear_mtrx[2][0] * rs + linear_mtrx[2][1] * gs + linear_mtrx[2][2] * bs + linear_mtrx[2][3]) * (1.0f - weight);
						}
						else
							bf = linear_mtrx[2][0] * rs + linear_mtrx[2][1] * gs + linear_mtrx[2][2] * bs + linear_mtrx[2][3];

						ys = rs * 0.3f + gs * 0.6f + bs * 0.1f;
				/*		if(ys > highlight_start)
						{
							rf2 = linear_mtrx_highlight_sat[0][0] * rs + linear_mtrx_highlight_sat[0][1] * gs + linear_mtrx_highlight_sat[0][2] * bs + linear_mtrx_highlight_sat[0][3];
							gf2 = linear_mtrx_highlight_sat[1][0] * rs + linear_mtrx_highlight_sat[1][1] * gs + linear_mtrx_highlight_sat[1][2] * bs + linear_mtrx_highlight_sat[1][3];
							bf2 = linear_mtrx_highlight_sat[2][0] * rs + linear_mtrx_highlight_sat[2][1] * gs + linear_mtrx_highlight_sat[2][2] * bs + linear_mtrx_highlight_sat[2][3];

							if(ys < highlight_end)
							{
								float mix = (ys - highlight_start)*mixgain;
							
								rf = rf2 * mix + rf * (1.0 - mix);
								gf = gf2 * mix + gf * (1.0 - mix);
								bf = bf2 * mix + bf * (1.0 - mix); 
							}
							else 
							{
								rf = rf2;
								gf = gf2;
								bf = bf2;
							}
						} */
					}
					else
					{
						rf = curve2lin[r<<step];
						gf = curve2lin[g<<step];
						bf = curve2lin[b<<step];
					}

					if(cfhddata->PrimariesUseDecodeCurve)
					{
						if(rf<-1.0f) rf=-1.0f;
						if(gf<-1.0f) gf=-1.0f;
						if(bf<-1.0f) bf=-1.0f;
						if(rf>4.0f) rf=4.0f;
						if(gf>4.0f) gf=4.0f;
						if(bf>4.0f) bf=4.0f;

						entry = (int)(rf*512.0f)+512;
						mix = (rf*512.0f+512.0f) - (float)entry;
						rf = lin2curve[entry]*(1.0f-mix) + lin2curve[entry+1]*mix;

						entry = (int)(gf*512.0f)+512;
						mix = (gf*512.0f+512.0f) - (float)entry;
						gf = lin2curve[entry]*(1.0f-mix) + lin2curve[entry+1]*mix;

						entry = (int)(bf*512.0f)+512;
						mix = (bf*512.0f+512.0f) - (float)entry;
						bf = lin2curve[entry]*(1.0f-mix) + lin2curve[entry+1]*mix;
					}

					if(decoder->curved_matrix_non_unity)
					{
						rs = rf;
						gs = gf;
						bs = bf;

						//apply curved offset and gain
						rf = curved_mtrx[0][0] * rs + curved_mtrx[0][1] * gs + curved_mtrx[0][2] * bs + curved_mtrx[0][3];
						gf = curved_mtrx[1][0] * rs + curved_mtrx[1][1] * gs + curved_mtrx[1][2] * bs + curved_mtrx[1][3];
						bf = curved_mtrx[2][0] * rs + curved_mtrx[2][1] * gs + curved_mtrx[2][2] * bs + curved_mtrx[2][3];

					}
					if(rf<-1.0f) rf=-1.0f;
					if(gf<-1.0f) gf=-1.0f;
					if(bf<-1.0f) bf=-1.0f;
					if(rf>4.0f) rf=4.0f;
					if(gf>4.0f) gf=4.0f;
					if(bf>4.0f) bf=4.0f;

					//apply gamma and contrast
						// WIP -- need to acclerate with tables.
					if(red_gamma_tweak != 1.0f || contrast != 1.0f)
					{
						entry = (int)(rf*512.0f)+512;
						mix = (rf*512.0f+512.0f) - (float)entry;
						rf = redgammatweak[entry]*(1.0f-mix) + redgammatweak[entry+1]*mix;
					}
					if(grn_gamma_tweak != 1.0f || contrast != 1.0f)
					{
						entry = (int)(gf*512.0f)+512;
						mix = (gf*512.0f+512.0f) - (float)entry;
						gf = grngammatweak[entry]*(1.0f-mix) + grngammatweak[entry+1]*mix;
					}
					if(blu_gamma_tweak != 1.0f || contrast != 1.0f)
					{
						entry = (int)(bf*512.0f)+512;
						mix = (bf*512.0f+512.0f) - (float)entry;
						bf = blugammatweak[entry]*(1.0f-mix) + blugammatweak[entry+1]*mix;
					}

					if(!cfhddata->PrimariesUseDecodeCurve)
					{
						if(rf<-1.0f) rf=-1.0f;
						if(gf<-1.0f) gf=-1.0f;
						if(bf<-1.0f) bf=-1.0f;
						if(rf>4.0f) rf=4.0f;
						if(gf>4.0f) gf=4.0f;
						if(bf>4.0f) bf=4.0f;

						//restore curve
						entry = (int)(rf*512.0f)+512;
						mix = (rf*512.0f+512.0f) - (float)entry;
						rf = lin2curve[entry]*(1.0f-mix) + lin2curve[entry+1]*mix;

						entry = (int)(gf*512.0f)+512;
						mix = (gf*512.0f+512.0f) - (float)entry;
						gf = lin2curve[entry]*(1.0f-mix) + lin2curve[entry+1]*mix;

						entry = (int)(bf*512.0f)+512;
						mix = (bf*512.0f+512.0f) - (float)entry;
						bf = lin2curve[entry]*(1.0f-mix) + lin2curve[entry+1]*mix;
					}
					if(cdl_sat != 0.0f)
					{
						float sat = cdl_sat + 1.0f;
						float luma = 0.2126f*rf + 0.7152f*gf + 0.0722f*bf; // Rec. 709
						rf = luma + sat * (rf - luma);
						gf = luma + sat * (gf - luma);
						bf = luma + sat * (bf - luma);
					}
				}
				else
				{
					rf = r / (float)(cube_depth); //DAN20120520 this was rf = r / (float)(cube_depth-1); was is wrong.
					gf = g / (float)(cube_depth);
					bf = b / (float)(cube_depth);
				}

				if(useLUT)
				{
					//float rr,gr,br;
					int rsrc,gsrc,bsrc;
					int rupp,gupp,bupp;
					float rmix,gmix,bmix;
					int offset[8];
					float *LUT1 = LUT+1;
					float *LUT2 = LUT+2;


					rsrc = (int)(rf*LUTscale);
					gsrc = (int)(gf*LUTscale);
					bsrc = (int)(bf*LUTscale);

					if(rsrc >= LUTsize-1) rsrc = LUTsize-2;  //for 16x16x16,  entry 0 thru 15, point to 14 and 15.
					if(gsrc >= LUTsize-1) gsrc = LUTsize-2;
					if(bsrc >= LUTsize-1) bsrc = LUTsize-2;
					if(rsrc < 0) rsrc = 0;
					if(gsrc < 0) gsrc = 0;
					if(bsrc < 0) bsrc = 0;

					rmix = rf*LUTscale - (float)rsrc;
					gmix = gf*LUTscale - (float)gsrc;
					bmix = bf*LUTscale - (float)bsrc;

					if(rmix < -1.0f) // extrapolate below the cube up to -1
						rmix = -1.0f;
					if(gmix < -1.0f)
						gmix = -1.0f;
					if(bmix < -1.0f)
						bmix = -1.0f;
					if(rmix > 4.0f) // extrapolate above the cube up to +4
						rmix = 4.0f;
					if(gmix > 4.0f)
						gmix = 4.0f;
					if(bmix > 4.0f)
						bmix = 4.0f;

					rupp = rsrc + 1;
					gupp = gsrc + 1;
					bupp = bsrc + 1;

					offset[0] = (bsrc*LUTsize*LUTsize + gsrc*LUTsize + rsrc)*3;
					offset[1] = (bsrc*LUTsize*LUTsize + gsrc*LUTsize + rupp)*3;
					offset[2] = (bsrc*LUTsize*LUTsize + gupp*LUTsize + rsrc)*3;
					offset[3] = (bsrc*LUTsize*LUTsize + gupp*LUTsize + rupp)*3;
					offset[4] = (bupp*LUTsize*LUTsize + gsrc*LUTsize + rsrc)*3;
					offset[5] = (bupp*LUTsize*LUTsize + gsrc*LUTsize + rupp)*3;
					offset[6] = (bupp*LUTsize*LUTsize + gupp*LUTsize + rsrc)*3;
					offset[7] = (bupp*LUTsize*LUTsize + gupp*LUTsize + rupp)*3;


					rf =( ((((LUT[offset[0]]*(1.0f-rmix) + LUT[offset[1]]*rmix))*(1.0f-gmix) +
						((LUT[offset[2]]*(1.0f-rmix) + LUT[offset[3]]*rmix))*gmix))*(1.0f-bmix) +
						((((LUT[offset[4]]*(1.0f-rmix) + LUT[offset[5]]*rmix))*(1.0f-gmix) +
						((LUT[offset[6]]*(1.0f-rmix) + LUT[offset[7]]*rmix))*gmix))*bmix);

					gf =( ((((LUT1[offset[0]]*(1.0f-rmix) + LUT1[offset[1]]*rmix))*(1.0f-gmix) +
						((LUT1[offset[2]]*(1.0f-rmix) + LUT1[offset[3]]*rmix))*gmix))*(1.0f-bmix) +
						((((LUT1[offset[4]]*(1.0f-rmix) + LUT1[offset[5]]*rmix))*(1.0f-gmix) +
						((LUT1[offset[6]]*(1.0f-rmix) + LUT1[offset[7]]*rmix))*gmix))*bmix);

					bf =( ((((LUT2[offset[0]]*(1.0f-rmix) + LUT2[offset[1]]*rmix))*(1.0f-gmix) +
						((LUT2[offset[2]]*(1.0f-rmix) + LUT2[offset[3]]*rmix))*gmix))*(1.0f-bmix) +
						((((LUT2[offset[4]]*(1.0f-rmix) + LUT2[offset[5]]*rmix))*(1.0f-gmix) +
						((LUT2[offset[6]]*(1.0f-rmix) + LUT2[offset[7]]*rmix))*gmix))*bmix);
				}

			/*	if(convert2YUV)
				{
					float yf,uf,vf;

					switch(colorspace & COLORSPACE_MASK)
					{
					case COLOR_SPACE_CG_601: //601

						if(broadcastLimit)
						{							
							// limit to CG RGB range
							if(rf>1.0) rf = 1.0;
							if(gf>1.0) gf = 1.0;
							if(bf>1.0) bf = 1.0;
							if(rf<0.0) rf = 0.0;
							if(gf<0.0) gf = 0.0;
							if(bf<0.0) bf = 0.0;

							// CG math
							yf =  0.257*rf + 0.504*gf + 0.098*bf + 0.0625;
							uf = -0.148*rf - 0.291*gf + 0.439*bf + 0.5;
							vf =  0.439*rf - 0.368*gf - 0.071*bf + 0.5;
							break;
						}
						// Convert to VS and fall through
						rf *= 219.0/255.0; 	rf += 16.0/255;
						gf *= 219.0/255.0; 	gf += 16.0/255;
						bf *= 219.0/255.0; 	bf += 16.0/255;
						
					case COLOR_SPACE_VS_601: //VS 601
						
						// limit to VS RGB range
						if(rf>1.0) rf = 1.0;
						if(gf>1.0) gf = 1.0;
						if(bf>1.0) bf = 1.0;
						if(rf<0.0) rf = 0.0;
						if(gf<0.0) gf = 0.0;
						if(bf<0.0) bf = 0.0;

						yf =  0.299*rf + 0.587*gf + 0.114*bf;
						uf = -0.172*rf - 0.339*gf + 0.511*bf + 0.5;
						vf =  0.511*rf - 0.428*gf - 0.083*bf + 0.5;
						break;

					default: assert(0);
					case COLOR_SPACE_CG_709:
						
						if(broadcastLimit)
						{							
							// limit to CG RGB range
							if(rf>1.0) rf = 1.0;
							if(gf>1.0) gf = 1.0;
							if(bf>1.0) bf = 1.0;
							if(rf<0.0) rf = 0.0;
							if(gf<0.0) gf = 0.0;
							if(bf<0.0) bf = 0.0;

							// CG math
							yf =  0.183*rf + 0.614*gf + 0.062*bf + 0.0625;
							uf = -0.101*rf - 0.338*gf + 0.439*bf + 0.5;
							vf =  0.439*rf - 0.399*gf - 0.040*bf + 0.5;
							break;
						}
						// Convert to VS and fall through
						rf *= 219.0/255.0; 	rf += 16.0/255;
						gf *= 219.0/255.0; 	gf += 16.0/255;
						bf *= 219.0/255.0; 	bf += 16.0/255;

					case COLOR_SPACE_VS_709:
						
						// limit to VS RGB range
						if(rf>1.0) rf = 1.0;
						if(gf>1.0) gf = 1.0;
						if(bf>1.0) bf = 1.0;
						if(rf<0.0) rf = 0.0;
						if(gf<0.0) gf = 0.0;
						if(bf<0.0) bf = 0.0;

						yf =  0.213*rf + 0.715*gf + 0.072*bf;
						uf = -0.117*rf - 0.394*gf + 0.511*bf + 0.5;
						vf =  0.511*rf - 0.464*gf - 0.047*bf + 0.5;
						break;
					}

					rf = yf;
					gf = uf;
					bf = vf;
						
					ri = rf * 8192.0;
					gi = gf * 8192.0;
					bi = bf * 8192.0;

					if(ri < 0) ri = 0;
					if(ri > 8191) ri = 8191;
					if(gi < 0) gi = 0;
					if(gi > 8191) gi = 8191;
					if(bi < 0) bi = 0;
					if(bi > 8191) bi = 8191;

					RawCube[coordbase++] = ri;  // 13-bit = white
					RawCube[coordbase++] = gi;
					RawCube[coordbase++] = bi;
				}
				else */
				{
					ri = (int)(rf * 8192.0f);
					gi = (int)(gf * 8192.0f);
					bi = (int)(bf * 8192.0f);

					if(ri < -32768) ri = -32768;
					if(ri > 32767) ri = 32767;
					if(gi < -32768) gi = -32768;
					if(gi > 32767) gi = 32767;
					if(bi < -32768) bi = -32768;
					if(bi > 32767) bi = 32767;

					RawCube[coordbase++] = ri;  // 13-bit = white
					RawCube[coordbase++] = gi;
					RawCube[coordbase++] = bi;
				}
			}
		}
	}
}


void DoBuildCube(DECODER *decoder, int thread_index, int max_units)
{
	THREAD_ERROR error;

	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			BuildCube(decoder, work_index, max_units);
		}
		else
		{
			// No more work to do
			return;
		}
	}
}

void Build1DCurves2Linear(DECODER *decoder, int unit, int max_units)
{
	int j,k,val;
	int encode_curve_type = decoder->encode_curve_type1D;
	int encode_curve_neg = decoder->encode_curve_type1D & CURVE_TYPE_NEGATIVE;
	float encode_curvebase = decoder->encode_curvebase1D;
	CFHDDATA *cfhddata = &decoder->cfhddata;
	//int decode_curve_type = cfhddata->decode_curve >> 16;
	//float decode_curvebase = decoder->decode_curvebase1D;

	int work = 16384*3; // -16384 to 32768 or -2 to 4
	//int workPerUnit = work / max_units;
	int start = -16384 + (unit * work) / max_units;
	int end =  -16384 + ((unit+1) * work) / max_units;

	for(k=start; k<end; k++) //-2 to 4         -2,-1, 0, 1, 2, 3, 4 
	{
		j = k;
		if(encode_curve_neg) // range 3 to -2   3, 2, 1, 0,-1,-2,-2
		{
			j = 8192-j-1;
			if(j<-8192*2) j = -8192*2;
		}

		switch(encode_curve_type & CURVE_TYPE_MASK)
		{
		case CURVE_TYPE_LOG:
			val = (int)(CURVE_LOG2LIN((float)j/8192.0f,
				(float)encode_curvebase) * 8192.0f);
			break;
		case CURVE_TYPE_GAMMA:
			val = (int)(CURVE_GAM2LIN((float)j/8192.0f,
				(float)encode_curvebase) * 8192.0f);
			break;
		case CURVE_TYPE_CINEON:
			val = (int)(CURVE_CINEON2LIN((float)j/8192.0f,
				(float)encode_curvebase) * 8192.0f);
			break;
		case CURVE_TYPE_CINE985:
			val = (int)(CURVE_CINE9852LIN((float)j/8192.0f,
				(float)encode_curvebase) * 8192.0f);
			break;
		case CURVE_TYPE_PARA:
			val = (int)(CURVE_PARA2LIN((float)j/8192.0f,
				(int)((cfhddata->encode_curve >> 8) & 0xff), (int)(cfhddata->encode_curve & 0xff)) * 8192.0f);
			break;
		case CURVE_TYPE_CSTYLE:
			val = (int)(CURVE_CSTYLE2LIN((float)j/8192.0f, (int)(cfhddata->encode_curve & 0xff)) * 8192.0f);
			break;
		case CURVE_TYPE_SLOG:
			val = (int)(CURVE_SLOG2LIN((float)j/8192.0f) * 8192.0f);
			break;
		case CURVE_TYPE_LOGC:
			val = (int)(CURVE_LOGC2LIN((float)j/8192.0f) * 8192.0f);
			break;
		case CURVE_TYPE_LINEAR:
		default:
			val = j;
			break;
		}
		if(val < -16384) val = -16384;
		if(val > 32767) val = 32767;
		
		decoder->Curve2Linear[k+16384] = val;
	}
}


void DoBuild1DCurves2Linear(DECODER *decoder, int thread_index, int max_units)
{
	THREAD_ERROR error;

	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			Build1DCurves2Linear(decoder, work_index, max_units);
		}
		else
		{
			// No more work to do
			return;
		}
	}
}

void Build1DLinear2Curves(DECODER *decoder, int unit, int max_units)
{
	int j;
	//int val;
	//int encode_curve_type = decoder->encode_curve_type1D;
	//float encode_curvebase = decoder->encode_curvebase1D;
	CFHDDATA *cfhddata = &decoder->cfhddata;
	int decode_curve_type = cfhddata->decode_curve >> 16;
	float red_gamma_tweak = decoder->red_gamma_tweak;
	float grn_gamma_tweak = decoder->grn_gamma_tweak;
	float blu_gamma_tweak = decoder->blu_gamma_tweak;
	float contrast = decoder->contrast;
	float decode_curvebase = decoder->decode_curvebase1D;	
	int work = 65536; // // -2 to +6, 13-bit
	//int workPerUnit = work / max_units;
	int start = 0 +(unit * work) / max_units;
	int end = 0 + ((unit+1) * work) / max_units;

	{
		int value;
		float oneunit = 8192.0;
		int gain,power;

		if(decode_curve_type & CURVE_TYPE_EXTENDED)
			gain = (cfhddata->decode_curve); // use all 16-bits for larger log bases
		else
		{
			gain = ((cfhddata->decode_curve >> 8) & 0xff);
			power = (cfhddata->decode_curve & 0xff);
		}

		if(decoder->cg_non_unity)
		{
			for(j=start; j<end; j++) // -2 to +6, 13-bit
			{
				float intensity = (float)(j - 16384);

				float valuer = CURVE_LIN2GAM(intensity/oneunit,red_gamma_tweak);
				float valueg = CURVE_LIN2GAM(intensity/oneunit,grn_gamma_tweak);
				float valueb = CURVE_LIN2GAM(intensity/oneunit,blu_gamma_tweak);

				if(contrast != 1.0)
				{
					valuer = calc_contrast(valuer,contrast);
					valueg = calc_contrast(valueg,contrast);
					valueb = calc_contrast(valueb,contrast);
				}

				value = (int)(valuer * oneunit);
				if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
				decoder->GammaContrastRed[j] = value;
				value = (int)(valueg * oneunit);
				if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
				decoder->GammaContrastGrn[j] = value;
				value = (int)(valueb * oneunit);
				if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
				decoder->GammaContrastBlu[j] = value;

				if(cfhddata->PrimariesUseDecodeCurve) // GammaContrast and Linear2Curve are separated, other Linear2Curve has both
					valuer = valueg = valueb = intensity / oneunit;

				switch(decode_curve_type & CURVE_TYPE_MASK)
				{
				case CURVE_TYPE_LOG:
					value = (int)(CURVE_LIN2LOG(valuer,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					value = (int)(CURVE_LIN2LOG(valueg,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveGrn[j] = value;
					value = (int)(CURVE_LIN2LOG(valueb,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveBlu[j] = value;
					break;
				case CURVE_TYPE_GAMMA:
					value = (int)(CURVE_LIN2GAM(valuer,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					value = (int)(CURVE_LIN2GAM(valueg,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveGrn[j] = value;
					value = (int)(CURVE_LIN2GAM(valueb,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveBlu[j] = value;
					break;
				case CURVE_TYPE_CINEON:
					value = (int)(CURVE_LIN2CINEON(valuer,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					value = (int)(CURVE_LIN2CINEON(valueg,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveGrn[j] = value;
					value = (int)(CURVE_LIN2CINEON(valueb,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveBlu[j] = value;
					break;
				case CURVE_TYPE_CINE985:
					value = (int)(CURVE_LIN2CINE985(valuer,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					value = (int)(CURVE_LIN2CINE985(valueg,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveGrn[j] = value;
					value = (int)(CURVE_LIN2CINE985(valueb,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveBlu[j] = value;
					break;
				case CURVE_TYPE_PARA:
					value = (int)(CURVE_LIN2PARA(valuer, gain, power) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					value = (int)(CURVE_LIN2PARA(valueg, gain, power) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveGrn[j] = value;
					value = (int)(CURVE_LIN2PARA(valueb, gain, power) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveBlu[j] = value;
					break;
				case CURVE_TYPE_CSTYLE:
					value = (int)(CURVE_LIN2CSTYLE(valuer,gain) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					value = (int)(CURVE_LIN2CSTYLE(valueg,gain) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveGrn[j] = value;
					value = (int)(CURVE_LIN2CSTYLE(valueb,gain) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveBlu[j] = value;
					break;
				case CURVE_TYPE_SLOG:
					value = (int)(CURVE_LIN2SLOG(valuer) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					value = (int)(CURVE_LIN2SLOG(valueg) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveGrn[j] = value;
					value = (int)(CURVE_LIN2SLOG(valueb) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveBlu[j] = value;
					break;
				case CURVE_TYPE_LOGC:
					value = (int)(CURVE_LIN2LOGC(valuer) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					value = (int)(CURVE_LIN2LOGC(valueg) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveGrn[j] = value;
					value = (int)(CURVE_LIN2LOGC(valueb) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveBlu[j] = value;
					break;
				case CURVE_TYPE_LINEAR:
				default:
					value = (int)(valuer * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;

					value = (int)(valueg * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveGrn[j] = value;

					value = (int)(valueb * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveBlu[j] = value;
					break;
				}
			}

			decoder->use_three_1DLUTS = 1;
		}
		else
		{	
			for(j=start; j<end; j++) // -2 to +6, 13-bit
			{
				float intensity = (float)(j - 16384);

				float valuer = CURVE_LIN2GAM(intensity/oneunit,red_gamma_tweak);
				if(contrast != 1.0)
					valuer = calc_contrast(valuer,contrast);

				value = (int)(valuer * oneunit);
				if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
				decoder->GammaContrastRed[j] = value;

				if(cfhddata->PrimariesUseDecodeCurve) // GammaContrast and Linear2Curve are separated, other Linear2Curve has both
					valuer = intensity / oneunit;

				switch(decode_curve_type & CURVE_TYPE_MASK)
				{
				case CURVE_TYPE_LOG:
					value = (int)(CURVE_LIN2LOG(valuer,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					break;
				case CURVE_TYPE_GAMMA:
					value = (int)(CURVE_LIN2GAM(valuer,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					break;
				case CURVE_TYPE_CINEON:
					value = (int)(CURVE_LIN2CINEON(valuer,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					break;
				case CURVE_TYPE_CINE985:
					value = (int)(CURVE_LIN2CINE985(valuer,decode_curvebase) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					break;
				case CURVE_TYPE_PARA:
					value = (int)(CURVE_LIN2PARA(valuer, gain, power) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					break;
				case CURVE_TYPE_CSTYLE:
					value = (int)(CURVE_LIN2CSTYLE(valuer, gain) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					break;
				case CURVE_TYPE_SLOG:
					value = (int)(CURVE_LIN2SLOG(valuer) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					break;
				case CURVE_TYPE_LOGC:
					value = (int)(CURVE_LIN2LOGC(valuer) * oneunit);
					if(value < -16384) value = -16384;  if(value > 32767) value = 32767;
					decoder->Linear2CurveRed[j] = value;
					break;
				case CURVE_TYPE_LINEAR:
				default:
					decoder->Linear2CurveRed[j] = (int)(valuer * oneunit);
					break;
				}
			}
			decoder->use_three_1DLUTS = 0;
		}
	}
}


void DoBuild1DLinear2Curves(DECODER *decoder, int thread_index, int max_units)
{
	THREAD_ERROR error;

	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			Build1DLinear2Curves(decoder, work_index, max_units);
		}
		else
		{
			// No more work to do
			return;
		}
	}
}


int TestCubeFor1Dness(DECODER *decoder)
{
	short *sptr,*cube = decoder->RawCube;
	int cube_base = decoder->cube_base;
	int cube_depth = ((1<<cube_base)+1);
	//int cube_shift_dn = (16-cube_base);
	//int cube_depth_mask = ((1<<cube_shift_dn)-1);
	int ri,gi,bi;


	for(bi=0; bi<cube_depth-1; bi++)
		for(gi=0; gi<cube_depth-1; gi++)
			for(ri=0; ri<cube_depth-1; ri++)
			{
				sptr = &cube[(bi*cube_depth*cube_depth + gi*cube_depth + ri)*3];

				if(	sptr[0] == sptr[cube_depth*3] && 
					sptr[0] == sptr[cube_depth*cube_depth*3] &&
					sptr[0] == sptr[cube_depth*cube_depth*3+cube_depth*3] &&

					sptr[1] == sptr[3+1] && 
					sptr[1] == sptr[cube_depth*cube_depth*3+1] &&
					sptr[1] == sptr[cube_depth*cube_depth*3+3+1] &&

					sptr[2] == sptr[3+2] && 
					sptr[2] == sptr[cube_depth*3+2] &&
					sptr[2] == sptr[cube_depth*3+3+2])
				{
					
				}
				else
				{
					return 0;
				}
			}

	return 1;
}

void ComputeCube(DECODER *decoder)
{
	//int r,g,b;
	//int coordbase = 0;
	int cg_non_unity = 0;
	int curve_change = 0;
	float linear_mtrx[3][4] =
	{
        {1.0,  0,   0,   0},
        {0,  1.0,   0,   0},
        {0,    0, 1.0,   0}
	};
/*	float linear_mtrx_highlight_sat[3][4] =
	{
		1.0,  0,   0,   0,
		0,  1.0,   0,   0,
		0,    0, 1.0,   0,
	};*/
	float curved_mtrx[3][4] =
    {
        {1.0,  0,   0,   0},
        {0,  1.0,   0,   0},
        {0,    0, 1.0,   0}
	};
	float whitebalance[3] = { 1.0, 1.0, 1.0 };
	bool useLUT = false;
	bool freeLUT = false;
	bool forceBuildLUT = false;
	int LUTsize = 64;
	float red_gamma_tweak;
	float grn_gamma_tweak;
	float blu_gamma_tweak;
	float contrast;
	float *LUT = NULL;
	CFHDDATA *cfhddata = &decoder->cfhddata;
	short *RawCube = decoder->RawCube;
	unsigned int process_path_flags = cfhddata->process_path_flags;
	int colorformat = decoder->frame.format;
	//int colorspace = decoder->frame.colorspace;
	bool retcode = false;
	float encode_curvebase = 90.0;
	float decode_curvebase = 90.0;
	int encode_curve_type = cfhddata->encode_curve >> 16;
	int decode_curve_type = cfhddata->decode_curve >> 16;
	int encode_curve = cfhddata->encode_curve;
	int decode_curve = cfhddata->decode_curve;
	float cdl_sat = cfhddata->channel[decoder->channel_current+1].user_cdl_sat;
	float highlight_start = cfhddata->channel[0].user_highlight_point + 1.0f;
	int cube_base = decoder->cube_base;
	int cube_depth = ((1<<cube_base));
	int i,j;

/*	if(decoder->codec.encoded_format >= ENCODED_FORMAT_RGBA_4444 && decoder->codec.num_channels >= 4)
	{
		decoder->frame.white_point = 0;
		//decoder->frame.signed_pixels = 0;
		decoder->use_active_metadata_decoder = false;
		decoder->apply_color_active_metadata = false;
		return;
	} */

	memcpy(&decoder->Cube_cfhddata.FileTimecodeData, &decoder->cfhddata.FileTimecodeData, sizeof(AVIFileMetaData2));
	if(	0==memcmp(&decoder->Cube_cfhddata, &decoder->cfhddata, sizeof(CFHDDATA)) &&
		decoder->Cube_format == decoder->frame.format &&
		decoder->Cube_output_colorspace == decoder->frame.colorspace)
	{
		//Cube is already valid
		/*if(decoder->use_active_metadata_decoder == false)
		{
			decoder->frame.white_point = 0;
			//decoder->frame.signed_pixels = 0;
		}*/
		return;
	}

	if(decoder->basic_only)
		return;

	memcpy(&decoder->Cube_cfhddata, &decoder->cfhddata, sizeof(CFHDDATA));
	decoder->Cube_format = decoder->frame.format;
	decoder->Cube_output_colorspace = decoder->frame.colorspace;

	if(cfhddata->process_path_flags_mask)
	{
		process_path_flags &= cfhddata->process_path_flags_mask;
		if((cfhddata->process_path_flags_mask & 0xffff) == 7) // CM+WB+ACTIVE hack to force CM on
		{
			process_path_flags |= PROCESSING_COLORMATRIX|PROCESSING_ACTIVE;  // DAN20080225
		}
	}


	if(encode_curve_type) //1 or 2
	{	
		if(encode_curve_type & CURVE_TYPE_EXTENDED)
			encode_curvebase = (float)(cfhddata->encode_curve & 0xffff); // use all 16-bits for larger log bases
		else
			encode_curvebase = (float)((cfhddata->encode_curve >> 8) & 0xff) / (float)(cfhddata->encode_curve & 0xff);
	}
	else
	{
		encode_curve_type = CURVE_TYPE_LOG;
		encode_curvebase = 90.0;
		encode_curve = CURVE_LOG_90;
		cfhddata->encode_curve = encode_curve;

		if(cfhddata->cfhd_subtype > 1) //444+
		{
			encode_curve_type = CURVE_TYPE_GAMMA;
			encode_curvebase = 2.2f;
			encode_curve = CURVE_GAMMA_2pt2;
			cfhddata->encode_curve = encode_curve;
		}
	}

	if(decode_curve_type) //1 or 2
	{	
		if(decode_curve_type & CURVE_TYPE_EXTENDED)
			decode_curvebase = (float)(cfhddata->decode_curve & 0xffff); // use all 16-bits for larger log bases
		else
			decode_curvebase = (float)((cfhddata->decode_curve >> 8) & 0xff) / (float)(cfhddata->decode_curve & 0xff);
	}
	else
	{
		decode_curve = encode_curve;
		decode_curve_type = encode_curve_type;
		decode_curvebase = encode_curvebase;
		cfhddata->decode_curve = encode_curve;
	}

	if(encode_curvebase == 1.0 && encode_curve_type <= CURVE_TYPE_LINEAR)
		encode_curve_type = CURVE_TYPE_LINEAR;

	if(cfhddata->version >= 5 && process_path_flags == 0)
	{
		process_path_flags = PROCESSING_ACTIVE;
		if(useLUT)
			process_path_flags = PROCESSING_ACTIVE | PROCESSING_WHITEBALANCE | PROCESSING_LOOK_FILE;
		else
			process_path_flags = PROCESSING_ACTIVE | PROCESSING_WHITEBALANCE | PROCESSING_COLORMATRIX;
	}


	if(cfhddata->MagicNumber == CFHDDATA_MAGIC_NUMBER && cfhddata->version >= 2)
	{
		if(process_path_flags & PROCESSING_COLORMATRIX)
		{
			for(i=0; i<12; i++)
			{
				switch(cfhddata->use_base_matrix)
				{
					case 0: //unity
						// already initized
						break;
					case 1: //original camera matrix
						linear_mtrx[i>>2][i&3] = cfhddata->orig_colormatrix[i>>2][i&3];
						//linear_mtrx_highlight_sat[i>>2][i&3] = cfhddata->orig_colormatrix[i>>2][i&3];
						break;
					case 2: //custom matrix
						linear_mtrx[i>>2][i&3] = cfhddata->custom_colormatrix[i>>2][i&3];
						//linear_mtrx_highlight_sat[i>>2][i&3] = cfhddata->custom_colormatrix[i>>2][i&3];
						break;
				}
			}
		}

		if(cfhddata->version >= 5)
		{
			if(cfhddata->channel[decoder->channel_current+1].white_balance[0] > 0.0)
			{
				whitebalance[0] = cfhddata->channel[decoder->channel_current+1].white_balance[0];
				whitebalance[1] = cfhddata->channel[decoder->channel_current+1].white_balance[1];
				whitebalance[2] = cfhddata->channel[decoder->channel_current+1].white_balance[2];


				if(whitebalance[0] < 0.4f) whitebalance[0] = 0.4f;
				if(whitebalance[1] < 0.4f) whitebalance[1] = 0.4f;
				if(whitebalance[2] < 0.4f) whitebalance[2] = 0.4f;
#if 0
				if(whitebalance[0] < 1.0)
				{
					whitebalance[1] /= whitebalance[0];
					whitebalance[2] /= whitebalance[0];
					whitebalance[0] = 1.0;
				}
				if(whitebalance[1] < 1.0)
				{
					whitebalance[0] /= whitebalance[1];
					whitebalance[2] /= whitebalance[1];
					whitebalance[1] = 1.0;
				}
				if(whitebalance[2] < 1.0)
				{
					whitebalance[0] /= whitebalance[2];
					whitebalance[1] /= whitebalance[2];
					whitebalance[2] = 1.0;
				}
#endif

				if(whitebalance[0] > 10.0) whitebalance[0] = 10.0;
				if(whitebalance[1] > 10.0) whitebalance[1] = 10.0;
				if(whitebalance[2] > 10.0) whitebalance[2] = 10.0;
			}
		}
	}

	if(process_path_flags & PROCESSING_COLORMATRIX)
	{
		float desatMatrix[3][4] =
        {	{0.309f, 0.609f, 0.082f, 0.0},
            {0.309f, 0.609f, 0.082f, 0.0},
            {0.309f, 0.609f, 0.082f, 0.0} };
		float fullsatMatrix[3][4] =
        {   {4.042f, -2.681f, -0.361f, 0.0},
            {-1.358f, 2.719f, -0.361f, 0.0},
            {-1.358f, -2.681f, 5.039f, 0.0} };
		float sat = cfhddata->channel[decoder->channel_current+1].user_saturation + 1.0f;
		float exposure = cfhddata->channel[decoder->channel_current+1].user_exposure + 1.0f;
		//float hisat = cfhddata->channel[0].user_highlight_sat + 1.0f;

        //saturation
        for (i = 0; i < 3; i++)
		{
            for (j = 0; j < 3; j++)
            {
                if (sat <= 1.0)
                {
                    linear_mtrx[i][j] = ((1.0f - sat) * (desatMatrix[i][j]) + (sat) * (linear_mtrx[i][j]));
                }
                else if (sat > 1.0)
                {
                    linear_mtrx[i][j] = (((sat - 1.0f) / 3.0f) * (fullsatMatrix[i][j]) + ((4.0f - sat) / 3.0f) * (linear_mtrx[i][j]));
                }
            }
        }

		 //highlight saturation
  /*      for (i = 0; i < 3; i++)
		{
            for (j = 0; j < 3; j++)
            {
                if (hisat <= 1.0)
                {
                    linear_mtrx_highlight_sat[i][j] = ((1.0 - hisat) * (desatMatrix[i][j]) + (hisat) * (linear_mtrx_highlight_sat[i][j]));
                }
                else if (hisat > 1.0)
                {
                    linear_mtrx_highlight_sat[i][j] = (((hisat - 1.0) / 3.0) * (fullsatMatrix[i][j]) + ((4.0 - hisat) / 3.0) * (linear_mtrx_highlight_sat[i][j]));
                }
            }
        } */

		if(cfhddata->PrimariesUseDecodeCurve == 1)
		{
			//r,g,b gains and black level
			for (i = 0; i < 3; i++)
			{
				curved_mtrx[i][0] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
				curved_mtrx[i][1] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
				curved_mtrx[i][2] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
				curved_mtrx[i][3] += cfhddata->channel[decoder->channel_current+1].user_rgb_lift[i];
			}
		}
		else
		{
			//r,g,b gains and black level
			for (i = 0; i < 3; i++)
			{
				linear_mtrx[i][0] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
				linear_mtrx[i][1] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
				linear_mtrx[i][2] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
				linear_mtrx[i][3] += cfhddata->channel[decoder->channel_current+1].user_rgb_lift[i];

//				linear_mtrx_highlight_sat[i][0] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
//				linear_mtrx_highlight_sat[i][1] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
//				linear_mtrx_highlight_sat[i][2] *= cfhddata->channel[decoder->channel_current+1].user_rgb_gain[i];
//				linear_mtrx_highlight_sat[i][3] += cfhddata->channel[decoder->channel_current+1].user_rgb_lift[i];
			}
		}

		if(exposure != 1.0)
		{
			for (i = 0; i < 4; i++)
			{
				linear_mtrx[0][i] *= exposure;
				linear_mtrx[1][i] *= exposure;
				linear_mtrx[2][i] *= exposure;

//				linear_mtrx_highlight_sat[0][i] *= exposure;
//				linear_mtrx_highlight_sat[1][i] *= exposure;
//				linear_mtrx_highlight_sat[2][i] *= exposure;
			}
		}
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			for (j = 0; j < 4; j++)
			{
				if(i == j)
				{
					linear_mtrx[i][j] = 1.0;
					//linear_mtrx_highlight_sat[i][j] = 1.0;
				}
				else
				{
					linear_mtrx[i][j] = 0.0;
					//linear_mtrx_highlight_sat[i][j] = 0.0;
				}
			}
		}
	}

	if(process_path_flags & PROCESSING_WHITEBALANCE)
	{
        for (j = 0; j < 3; j++)
        {
			linear_mtrx[0][j] *= whitebalance[j];
			linear_mtrx[1][j] *= whitebalance[j];
			linear_mtrx[2][j] *= whitebalance[j];

//			linear_mtrx_highlight_sat[0][j] *= whitebalance[j];
//			linear_mtrx_highlight_sat[1][j] *= whitebalance[j];
//			linear_mtrx_highlight_sat[2][j] *= whitebalance[j];
        }

		//DAN20120802 -- This allows the custom color matrix black levels to work with white_balance
		for (j = 0; j < 3; j++)
        {
			linear_mtrx[j][3] *= whitebalance[j];
//			linear_mtrx_highlight_sat[j][3] *= whitebalance[j];
		}
	}

	decoder->useFloatCC = false;
	for (j = 0; j < 3; j++)
    {
		if(linear_mtrx[0][j] > 31.0 || linear_mtrx[0][j] < -16.0)	decoder->useFloatCC = true;
		if(linear_mtrx[1][j] > 31.0 || linear_mtrx[1][j] < -16.0)	decoder->useFloatCC = true;
		if(linear_mtrx[2][j] > 31.0 || linear_mtrx[2][j] < -16.0)	decoder->useFloatCC = true;

//		if(linear_mtrx_highlight_sat[0][j] > 31.0 || linear_mtrx_highlight_sat[0][j] < -16.0)	decoder->useFloatCC = true;
//		if(linear_mtrx_highlight_sat[1][j] > 31.0 || linear_mtrx_highlight_sat[1][j] < -16.0)	decoder->useFloatCC = true;
//		if(linear_mtrx_highlight_sat[2][j] > 31.0 || linear_mtrx_highlight_sat[2][j] < -16.0)	decoder->useFloatCC = true;
    }


	// Strong negative values (colormatrix mult white_balance) will cause weird color in the highlights.  the LUT processing path has extra code to handle that.
	if((linear_mtrx[0][1]+linear_mtrx[0][2]) < -1.0)	forceBuildLUT = true; // G+B for red channel
	if((linear_mtrx[1][0]+linear_mtrx[1][2]) < -1.0)	forceBuildLUT = true; // R+B for green channel
	if((linear_mtrx[2][0]+linear_mtrx[2][1]) < -1.0)	forceBuildLUT = true; // R+G for blue channel

//	if((linear_mtrx_highlight_sat[0][1]+linear_mtrx_highlight_sat[0][2]) < -1.0)	forceBuildLUT = true; // G+B for red channel
//	if((linear_mtrx_highlight_sat[1][0]+linear_mtrx_highlight_sat[1][2]) < -1.0)	forceBuildLUT = true; // R+B for green channel
//	if((linear_mtrx_highlight_sat[2][0]+linear_mtrx_highlight_sat[2][1]) < -1.0)	forceBuildLUT = true; // R+G for blue channel

	decoder->linear_matrix_non_unity = 0;
	decoder->curved_matrix_non_unity = 0;
	for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 4; j++)
        {
			if(i == j)
			{
				if(linear_mtrx[i][j] != 1.0)
				{
					decoder->linear_matrix_non_unity = 1;
				}
				if(curved_mtrx[i][j] != 1.0)
				{
					decoder->curved_matrix_non_unity = 1;
				}
			}
			else
			{
				if(linear_mtrx[i][j] != 0.0)
				{
					decoder->linear_matrix_non_unity = 1;
				}
				if(curved_mtrx[i][j] != 0.0)
				{
					decoder->curved_matrix_non_unity = 1;
				}
			}
		}
	}

	if(cdl_sat != 0.0)
		decoder->linear_matrix_non_unity = 1;

	if(highlight_start < 1.0 && process_path_flags & PROCESSING_WHITEBALANCE)
	{
		float max = whitebalance[0];
		if(max < whitebalance[1])
			max = whitebalance[1];
		if(max < whitebalance[2])
			max = whitebalance[2];

		decoder->highlight_desat_gains[0] = max / whitebalance[0];
		decoder->highlight_desat_gains[1] = max / whitebalance[1];
		decoder->highlight_desat_gains[2] = max / whitebalance[2];
	
		if(max > 1.0) 
		{
			forceBuildLUT = true;
			decoder->linear_matrix_non_unity = 1;
		}		
	}
	else
	{
		decoder->highlight_desat_gains[0] = 1.0;
		decoder->highlight_desat_gains[1] = 1.0;
		decoder->highlight_desat_gains[2] = 1.0;
	}

	decoder->forceBuildLUT = forceBuildLUT;
	
	red_gamma_tweak = cfhddata->channel[decoder->channel_current+1].user_rgb_gamma[0];
	grn_gamma_tweak = cfhddata->channel[decoder->channel_current+1].user_rgb_gamma[1];
	blu_gamma_tweak = cfhddata->channel[decoder->channel_current+1].user_rgb_gamma[2];
	contrast = cfhddata->channel[decoder->channel_current+1].user_contrast + 1.0f;

	if(red_gamma_tweak == 0.0) red_gamma_tweak = 1.0f;
	if(grn_gamma_tweak == 0.0) grn_gamma_tweak = 1.0f;
	if(blu_gamma_tweak == 0.0) blu_gamma_tweak = 1.0f;

	if(!(process_path_flags & PROCESSING_GAMMA_TWEAKS))
	{
		red_gamma_tweak = 1.0f;
		grn_gamma_tweak = 1.0f;
		blu_gamma_tweak = 1.0f;
		contrast = 1.0f;
	}

	if(	red_gamma_tweak != 1.0f ||
		grn_gamma_tweak != 1.0f ||
		blu_gamma_tweak != 1.0f ||
		contrast != 1.0f)
	{
		cg_non_unity = 1;
	}

	//cube_base = 6; // use the biggest, quality LUT
	if(	decoder->frame.output_format == COLOR_FORMAT_B64A ||
		decoder->frame.output_format == COLOR_FORMAT_AR10 ||
		decoder->frame.output_format == COLOR_FORMAT_AB10 ||
		decoder->frame.output_format == COLOR_FORMAT_RG30 ||
		decoder->frame.output_format == COLOR_FORMAT_R210 ||
		decoder->frame.output_format == COLOR_FORMAT_DPX0 ||
		decoder->frame.output_format == COLOR_FORMAT_V210 ||
		decoder->frame.output_format == COLOR_FORMAT_YU64 ||
		decoder->frame.output_format == COLOR_FORMAT_YR16 ||
		decoder->frame.output_format == COLOR_FORMAT_RG48 ||
		decoder->frame.output_format == COLOR_FORMAT_B64A ||
		decoder->frame.output_format == COLOR_FORMAT_R4FL ||
		decoder->frame.output_format == COLOR_FORMAT_WP13 ||
		decoder->frame.output_format == COLOR_FORMAT_W13A ||
		decoder->frame.output_format == COLOR_FORMAT_RGB_8PIXEL_PLANAR
		)
	{
		cube_base = 6; // use the biggest, quality LUT
	}
	else
	{
		cube_base = 5; // use faster LUT for preview and 8-bit exports
	}
	
	if(process_path_flags & PROCESSING_LOOK_FILE)
	{
		if(cfhddata->user_look_CRC != 0)
		{
			LUT = LoadCube64_3DLUT(decoder, cfhddata, &LUTsize);
			if (LUT)
			{
				useLUT = 1;
			}
		}
		else
		{
			useLUT = 0;
		}
	}
	else
	{
		useLUT = 0;
	}

	if(cfhddata->export_look)
	{
		cube_base = 6;
		colorformat = COLOR_FORMAT_RG48; // non YUV so the LUT doesn't have a color space convertor in it.
		if(useLUT == 0)
		{
			LUT = ResetCube64_3DLUT(decoder, cube_base);
			if (LUT)
			{
				useLUT = 1;
				freeLUT = true;
			}
		}
	}


	decoder->cube_base = cube_base;
	cube_depth = ((1<<cube_base));

	if(	(decode_curve_type != encode_curve_type) ||
		(decode_curvebase != encode_curvebase) ||
		(!cfhddata->PrimariesUseDecodeCurve && (decoder->linear_matrix_non_unity || decoder->curved_matrix_non_unity)))
	{
		curve_change = 1;
	}


	decoder->contrast_gamma_non_unity = cg_non_unity;
	decoder->curve_change_active = curve_change;


	if(!useLUT && !forceBuildLUT && decoder->RawCube) //DAN20090529
	{
#if _ALLOCATOR
		FreeAligned(decoder->allocator, decoder->RawCube);
		RawCube = decoder->RawCube = 0;
#else
		MEMORY_ALIGNED_FREE(decoder->RawCube);
		RawCube = decoder->RawCube = 0;
#endif
	}

	if(useLUT || forceBuildLUT)
	{
		if(decoder->RawCube == NULL)
		{
#if _ALLOCATOR
			RawCube = decoder->RawCube = (short *)AllocAligned(decoder->allocator, 65*65*65*3*2, 16);
#else
			RawCube = decoder->RawCube = (short *)MEMORY_ALIGNED_ALLOC(65*65*65*3*2, 16);
#endif
		}
	}
	else if(decoder->linear_matrix_non_unity || decoder->curved_matrix_non_unity || cg_non_unity || curve_change)
	{
		if(decoder->Curve2Linear == NULL)
		{
#if _ALLOCATOR
			decoder->Curve2Linear = (short *)AllocAligned(decoder->allocator,(16384*3)*2, 16);
#else
			decoder->Curve2Linear = (short *)MEMORY_ALIGNED_ALLOC((16384*3)*2, 16);
#endif
		}
		if(decoder->Linear2CurveRed == NULL)
		{
#if _ALLOCATOR
			decoder->Linear2CurveRed = (short *)AllocAligned(decoder->allocator,65536*2, 16);
#else
			decoder->Linear2CurveRed = (short *)MEMORY_ALIGNED_ALLOC(65536*2, 16);
#endif
		}
		if(decoder->Linear2CurveGrn == NULL)
		{
#if _ALLOCATOR
			decoder->Linear2CurveGrn = (short *)AllocAligned(decoder->allocator,65536*2, 16);
#else
			decoder->Linear2CurveGrn = (short *)MEMORY_ALIGNED_ALLOC(65536*2, 16);
#endif
		}
		if(decoder->Linear2CurveBlu == NULL)
		{
#if _ALLOCATOR
			decoder->Linear2CurveBlu = (short *)AllocAligned(decoder->allocator,65536*2, 16);
#else
			decoder->Linear2CurveBlu = (short *)MEMORY_ALIGNED_ALLOC(65536*2, 16);
#endif
		}

		if(decoder->GammaContrastRed == NULL)
		{
#if _ALLOCATOR
			decoder->GammaContrastRed = (short *)AllocAligned(decoder->allocator,65536*2, 16);
#else
			decoder->GammaContrastRed = (short *)MEMORY_ALIGNED_ALLOC(65536*2, 16);
#endif
		}
		if(decoder->GammaContrastGrn == NULL)
		{
#if _ALLOCATOR
			decoder->GammaContrastGrn = (short *)AllocAligned(decoder->allocator,65536*2, 16);
#else
			decoder->GammaContrastGrn = (short *)MEMORY_ALIGNED_ALLOC(65536*2, 16);
#endif
		}
		if(decoder->GammaContrastBlu == NULL)
		{
#if _ALLOCATOR
			decoder->GammaContrastBlu = (short *)AllocAligned(decoder->allocator,65536*2, 16);
#else
			decoder->GammaContrastBlu = (short *)MEMORY_ALIGNED_ALLOC(65536*2, 16);
#endif
		}
	}

	if(1 && (useLUT || forceBuildLUT || decoder->linear_matrix_non_unity || decoder->curved_matrix_non_unity || cg_non_unity || curve_change))
	{
		if(RawCube && (useLUT || forceBuildLUT))
		{
			//float LUTscale = ((float)(LUTsize-1)) - 0.00001; // needed so that 63 * scale is 62.99 not 63
			int j,k;
			//int y;
			//float curve2lin[CUBE_DEPTH+1];
			//float lin2curve[2048+512+2];
			//float redgammatweak[2048+512+2];
			//float grngammatweak[2048+512+2];
			//float blugammatweak[2048+512+2];
			float *curve2lin = decoder->curve2lin;
			float *lin2curve = decoder->lin2curve;
			//float *redgammatweak = decoder->redgammatweak;
			//float *grngammatweak = decoder->grngammatweak;
			//float *blugammatweak = decoder->blugammatweak;
			//int convert2YUV = 0;

			//if(LUTYUV(colorformat))
			//	convert2YUV = 1;

			// This data doesn't change per keyframe
			if(decoder->curve2lin_type == encode_curve_type && decoder->curve2lin_base == encode_curvebase && decoder->last_cube_depth == cube_depth)
			{
				//calcs already done
			}
			else
			{
				
				int encode_curve_neg = encode_curve_type & CURVE_TYPE_NEGATIVE;

				for(k=0; k<64+1; k++)
				{
					j = k;
					if(encode_curve_neg)
						j = 64-j;

					switch(encode_curve_type & CURVE_TYPE_MASK)
					{
					case CURVE_TYPE_LOG:
						curve2lin[j] = CURVE_LOG2LIN((float)k/(float)(64-1),(float)encode_curvebase);
						break;
					case CURVE_TYPE_GAMMA:
						curve2lin[j] = CURVE_GAM2LIN((float)k/(float)(64-1),(float)encode_curvebase);
						break;
					case CURVE_TYPE_CINEON:
						curve2lin[j] = CURVE_CINEON2LIN((float)k/(float)(64-1),(float)encode_curvebase);
						break;
					case CURVE_TYPE_CINE985:
						curve2lin[j] = CURVE_CINE9852LIN((float)k/(float)(64-1),(float)encode_curvebase);
						break;
					case CURVE_TYPE_PARA:
						curve2lin[j] = CURVE_PARA2LIN((float)k/(float)(64-1),(int)((cfhddata->encode_curve >> 8) & 0xff), (int)(cfhddata->encode_curve & 0xff));
						break;
					case CURVE_TYPE_CSTYLE:
						curve2lin[j] = CURVE_CSTYLE2LIN((float)k/(float)(64-1),(int)((cfhddata->encode_curve >> 8) & 0xff));
						break;
					case CURVE_TYPE_SLOG:
						curve2lin[j] = CURVE_SLOG2LIN((float)k/(float)(64-1));
						break;
					case CURVE_TYPE_LOGC:
						curve2lin[j] = CURVE_LOGC2LIN((float)k/(float)(64-1));
						break;
					case CURVE_TYPE_LINEAR:
					default:
						curve2lin[j] = (float)k/(float)(64-1);
						break;
					}

					decoder->curve2lin_type = encode_curve_type;
					decoder->curve2lin_base = encode_curvebase;
					decoder->last_cube_depth = cube_depth;
				}
			}

			// This data doesn't change per keyframe
			if(decoder->lin2curve_type == decode_curve_type && decoder->lin2curve_base == decode_curvebase)
			{
				//calcs already done
			}
			else
			{
				for(j=-512; j<=2048; j++) // -1 to +4
				{
					switch(decode_curve_type & CURVE_TYPE_MASK)
					{
					case CURVE_TYPE_LOG:
						lin2curve[j+512] = CURVE_LIN2LOG((float)j/512.0f,(float)decode_curvebase);
						break;
					case CURVE_TYPE_GAMMA:
						lin2curve[j+512] = CURVE_LIN2GAM((float)j/512.0f,(float)decode_curvebase);
						break;
					case CURVE_TYPE_CINEON:
						lin2curve[j+512] = CURVE_LIN2CINEON((float)j/512.0f,(float)decode_curvebase);
						break;
					case CURVE_TYPE_CINE985:
						lin2curve[j+512] = CURVE_LIN2CINE985((float)j/512.0f,(float)decode_curvebase);
						break;
					case CURVE_TYPE_PARA:
						lin2curve[j+512] = CURVE_LIN2PARA((float)j/512.0f,(int)((cfhddata->decode_curve >> 8) & 0xff), (int)(cfhddata->decode_curve & 0xff));
						break;
					case CURVE_TYPE_CSTYLE:
						lin2curve[j+512] = CURVE_LIN2CSTYLE((float)j/512.0f,(int)((cfhddata->decode_curve >> 8) & 0xff));
						break;
					case CURVE_TYPE_LOGC:
						lin2curve[j+512] = CURVE_LIN2LOGC((float)j/512.0f);
						break;
					case CURVE_TYPE_LINEAR:
					default:
						lin2curve[j+512] = (float)j/512.0f;
						break;
					}
				}
				decoder->lin2curve_type = decode_curve_type;
				decoder->lin2curve_base = decode_curvebase;
			}
#if 1
			decoder->cg_non_unity = cg_non_unity;
			
			decoder->contrast = contrast;
			decoder->cdl_sat = cdl_sat;
			decoder->red_gamma_tweak = red_gamma_tweak;
			decoder->grn_gamma_tweak = grn_gamma_tweak;
			decoder->blu_gamma_tweak = blu_gamma_tweak;
			decoder->cg_non_unity = cg_non_unity;
			decoder->curve_change = curve_change;

			decoder->LUT = LUT;
			decoder->useLUT = useLUT;
			decoder->LUTsize = LUTsize;
			decoder->cube_depth = cube_depth;
			//decoder->convert2YUV = convert2YUV;
			memcpy(decoder->linear_mtrx, linear_mtrx, sizeof(linear_mtrx));
//			memcpy(decoder->linear_mtrx_highlight_sat, linear_mtrx_highlight_sat, sizeof(linear_mtrx));
			memcpy(decoder->curved_mtrx, curved_mtrx, sizeof(curved_mtrx));
#if 0
			BuildLUTCurves(decoder, 0,1);
			BuildCube(decoder, 0,1);
#else
		#if _DELAY_THREAD_START
			if(decoder->worker_thread.pool.thread_count == 0)
			{
				CreateLock(&decoder->worker_thread.lock);
				// Initialize the pool of transform worker threads
				ThreadPoolCreate(&decoder->worker_thread.pool,
								decoder->thread_cntrl.capabilities >> 16/*cpus*/,
								WorkerThreadProc,
								decoder);
			}
		#endif
			
			{
				WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

				mailbox->jobType = JOB_TYPE_BUILD_LUT_CURVES; 
				// Set the work count to the number of cpus
				ThreadPoolSetWorkCount(&decoder->worker_thread.pool, decoder->thread_cntrl.capabilities >> 16/*cpus*/);
				// Start the worker threads
				ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);
				// Wait for all of the worker threads to finish
				ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

				mailbox->jobType = JOB_TYPE_BUILD_CUBE; 
				// Set the work count to the number of cpus
				ThreadPoolSetWorkCount(&decoder->worker_thread.pool, decoder->thread_cntrl.capabilities >> 16/*cpus*/);
				// Start the worker threads
				ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);
				// Wait for all of the worker threads to finish
				ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

				decoder->RawCubeThree1Ds = TestCubeFor1Dness(decoder);
			}
#endif

#else
			// This data CAN change per keyframe
			if(cg_non_unity)
			{
				if(decoder->contrast == contrast && decoder->red_gamma_tweak == red_gamma_tweak)
				{
					//calcs already done
				}
				else
				{
					for(j=-512; j<=2048; j++) // -1 to +4
					{
						float value;
						if(red_gamma_tweak != 1.0 || contrast != 1.0)
						{
							value = CURVE_LIN2GAM((float)j/512.0,red_gamma_tweak);
							if(contrast != 1.0)
								value = calc_contrast(value,contrast);

							if(value<-1.0) value=-1.0;
							if(value>4.0) value=4.0;
							redgammatweak[j+512] = value;
						}
					}
				}
				if(decoder->contrast == contrast && decoder->grn_gamma_tweak == grn_gamma_tweak)
				{
					//calcs already done
				}
				else
				{
					for(j=-512; j<=2048; j++) // -1 to +4
					{
						float value;
						if(grn_gamma_tweak != 1.0 || contrast != 1.0)
						{
							value = CURVE_LIN2GAM((float)j/512.0,grn_gamma_tweak);
							if(contrast != 1.0)
								value = calc_contrast(value,contrast);

							if(value<-1.0) value=-1.0;
							if(value>4.0) value=4.0;
							grngammatweak[j+512] = value;
						}
					}
				}
				if(decoder->contrast == contrast && decoder->blu_gamma_tweak == blu_gamma_tweak)
				{
					//calcs already done
				}
				else
				{
					for(j=-512; j<=2048; j++) // -1 to +4
					{
						float value;
						if(blu_gamma_tweak != 1.0 || contrast != 1.0)
						{
							value = CURVE_LIN2GAM((float)j/512.0,blu_gamma_tweak);
							if(contrast != 1.0)
								value = calc_contrast(value,contrast);

							if(value<-1.0) value=-1.0;
							if(value>4.0) value=4.0;
							blugammatweak[j+512] = value;
						}
					}
				}
				decoder->contrast = contrast;
				decoder->cdl_sat = cdl_sat;
				decoder->red_gamma_tweak = red_gamma_tweak;
				decoder->grn_gamma_tweak = grn_gamma_tweak;
				decoder->blu_gamma_tweak = blu_gamma_tweak;
			}

			for(b=0;b<cube_depth+1;b++)
			{
				int change = decoder->linear_matrix_non_unity || decoder->curved_matrix_non_unity || cg_non_unity || curve_change;
				for(g=0;g<cube_depth+1;g++)
				{
					for(r=0;r<cube_depth+1;r++)
					{
						int ri,gi,bi;
						float rs,gs,bs,rf,gf,bf;
						int entry;
						float mix;

						if(change)
						{
							if(decoder->linear_matrix_non_unity)
							{
								rs = curve2lin[r];
								gs = curve2lin[g];
								bs = curve2lin[b];

								//apply maxtrix & WB
								rf = linear_mtrx[0][0] * rs + linear_mtrx[0][1] * gs + linear_mtrx[0][2] * bs + linear_mtrx[0][3];
								gf = linear_mtrx[1][0] * rs + linear_mtrx[1][1] * gs + linear_mtrx[1][2] * bs + linear_mtrx[1][3];
								bf = linear_mtrx[2][0] * rs + linear_mtrx[2][1] * gs + linear_mtrx[2][2] * bs + linear_mtrx[2][3];

								if(rf<-1.0) rf=-1.0;
								if(gf<-1.0) gf=-1.0;
								if(bf<-1.0) bf=-1.0;
								if(rf>4.0) rf=4.0;
								if(gf>4.0) gf=4.0;
								if(bf>4.0) bf=4.0;
							}
							else
							{
								rf = curve2lin[r];
								gf = curve2lin[g];
								bf = curve2lin[b];
							}

							if(cfhddata->PrimariesUseDecodeCurve)
							{
								entry = (int)(rf*512.0)+512;
								mix = (rf*512.0+512.0) - (float)entry;
								rf = lin2curve[entry]*(1.0-mix) + lin2curve[entry+1]*mix;

								entry = (int)(gf*512.0)+512;
								mix = (gf*512.0+512.0) - (float)entry;
								gf = lin2curve[entry]*(1.0-mix) + lin2curve[entry+1]*mix;

								entry = (int)(bf*512.0)+512;
								mix = (bf*512.0+512.0) - (float)entry;
								bf = lin2curve[entry]*(1.0-mix) + lin2curve[entry+1]*mix;
							}

							if(decoder->curved_matrix_non_unity)
							{
								rs = rf;
								gs = gf;
								bs = bf;

								//apply curved offset and gain
								rf = curved_mtrx[0][0] * rs + curved_mtrx[0][1] * gs + curved_mtrx[0][2] * bs + curved_mtrx[0][3];
								gf = curved_mtrx[1][0] * rs + curved_mtrx[1][1] * gs + curved_mtrx[1][2] * bs + curved_mtrx[1][3];
								bf = curved_mtrx[2][0] * rs + curved_mtrx[2][1] * gs + curved_mtrx[2][2] * bs + curved_mtrx[2][3];

							if(rf<-1.0) rf=-1.0;
							if(gf<-1.0) gf=-1.0;
							if(bf<-1.0) bf=-1.0;
							if(rf>4.0) rf=4.0;
							if(gf>4.0) gf=4.0;
							if(bf>4.0) bf=4.0;
							}
							else
							{
								if(rf<-1.0) rf=-1.0;
								if(gf<-1.0) gf=-1.0;
								if(bf<-1.0) bf=-1.0;
								if(rf>4.0) rf=4.0;
								if(gf>4.0) gf=4.0;
								if(bf>4.0) bf=4.0;
							}

							//apply gamma and contrast
								// WIP -- need to acclerate with tables.
							if(red_gamma_tweak != 1.0 || contrast != 1.0)
							{
								entry = (int)(rf*512.0)+512;
								mix = (rf*512.0+512.0) - (float)entry;
								rf = redgammatweak[entry]*(1.0-mix) + redgammatweak[entry+1]*mix;
							}
							if(grn_gamma_tweak != 1.0 || contrast != 1.0)
							{
								entry = (int)(gf*512.0)+512;
								mix = (gf*512.0+512.0) - (float)entry;
								gf = grngammatweak[entry]*(1.0-mix) + grngammatweak[entry+1]*mix;
							}
							if(blu_gamma_tweak != 1.0 || contrast != 1.0)
							{
								entry = (int)(bf*512.0)+512;
								mix = (bf*512.0+512.0) - (float)entry;
								bf = blugammatweak[entry]*(1.0-mix) + blugammatweak[entry+1]*mix;
							}

							if(!cfhddata->PrimariesUseDecodeCurve)
							{
								if(rf<-1.0) rf=-1.0;
								if(gf<-1.0) gf=-1.0;
								if(bf<-1.0) bf=-1.0;
								if(rf>4.0) rf=4.0;
								if(gf>4.0) gf=4.0;
								if(bf>4.0) bf=4.0;

								//restore curve
								entry = (int)(rf*512.0)+512;
								mix = (rf*512.0+512.0) - (float)entry;
								rf = lin2curve[entry]*(1.0-mix) + lin2curve[entry+1]*mix;

								entry = (int)(gf*512.0)+512;
								mix = (gf*512.0+512.0) - (float)entry;
								gf = lin2curve[entry]*(1.0-mix) + lin2curve[entry+1]*mix;

								entry = (int)(bf*512.0)+512;
								mix = (bf*512.0+512.0) - (float)entry;
								bf = lin2curve[entry]*(1.0-mix) + lin2curve[entry+1]*mix;
							}
						}
						else
						{
							rf = r / (float)(cube_depth-1);
							gf = g / (float)(cube_depth-1);
							bf = b / (float)(cube_depth-1);
						}

						if(useLUT)
						{
							float rr,gr,br;
							int rsrc,gsrc,bsrc;
							int rupp,gupp,bupp;
							float rmix,gmix,bmix;
							int offset[8];
							float *LUT1 = LUT+1;
							float *LUT2 = LUT+2;


							rsrc = (int)(rf*LUTscale);
							gsrc = (int)(gf*LUTscale);
							bsrc = (int)(bf*LUTscale);

							if(rsrc >= LUTsize-1) rsrc = LUTsize-2;
							if(gsrc >= LUTsize-1) gsrc = LUTsize-2;
							if(bsrc >= LUTsize-1) bsrc = LUTsize-2;
							if(rsrc < 0) rsrc = 0;
							if(gsrc < 0) gsrc = 0;
							if(bsrc < 0) bsrc = 0;

							rmix = rf*LUTscale - (float)rsrc;
							gmix = gf*LUTscale - (float)gsrc;
							bmix = bf*LUTscale - (float)bsrc;

							if(rmix < -1.0)
								rmix = -1.0;
							if(gmix < -1.0)
								gmix = -1.0;
							if(bmix < -1.0)
								bmix = -1.0;
							if(rmix > 4.0)
								rmix = 4.0;
							if(gmix > 4.0)
								gmix = 4.0;
							if(bmix > 4.0)
								bmix = 4.0;

							rupp = rsrc + 1;
							gupp = gsrc + 1;
							bupp = bsrc + 1;

							offset[0] = (bsrc*LUTsize*LUTsize + gsrc*LUTsize + rsrc)*3;
							offset[1] = (bsrc*LUTsize*LUTsize + gsrc*LUTsize + rupp)*3;
							offset[2] = (bsrc*LUTsize*LUTsize + gupp*LUTsize + rsrc)*3;
							offset[3] = (bsrc*LUTsize*LUTsize + gupp*LUTsize + rupp)*3;
							offset[4] = (bupp*LUTsize*LUTsize + gsrc*LUTsize + rsrc)*3;
							offset[5] = (bupp*LUTsize*LUTsize + gsrc*LUTsize + rupp)*3;
							offset[6] = (bupp*LUTsize*LUTsize + gupp*LUTsize + rsrc)*3;
							offset[7] = (bupp*LUTsize*LUTsize + gupp*LUTsize + rupp)*3;


							rf =( ((((LUT[offset[0]]*(1.0-rmix) + LUT[offset[1]]*rmix))*(1.0-gmix) +
								((LUT[offset[2]]*(1.0-rmix) + LUT[offset[3]]*rmix))*gmix))*(1.0-bmix) +
								((((LUT[offset[4]]*(1.0-rmix) + LUT[offset[5]]*rmix))*(1.0-gmix) +
								((LUT[offset[6]]*(1.0-rmix) + LUT[offset[7]]*rmix))*gmix))*bmix);

							gf =( ((((LUT1[offset[0]]*(1.0-rmix) + LUT1[offset[1]]*rmix))*(1.0-gmix) +
								((LUT1[offset[2]]*(1.0-rmix) + LUT1[offset[3]]*rmix))*gmix))*(1.0-bmix) +
								((((LUT1[offset[4]]*(1.0-rmix) + LUT1[offset[5]]*rmix))*(1.0-gmix) +
								((LUT1[offset[6]]*(1.0-rmix) + LUT1[offset[7]]*rmix))*gmix))*bmix);

							bf =( ((((LUT2[offset[0]]*(1.0-rmix) + LUT2[offset[1]]*rmix))*(1.0-gmix) +
								((LUT2[offset[2]]*(1.0-rmix) + LUT2[offset[3]]*rmix))*gmix))*(1.0-bmix) +
								((((LUT2[offset[4]]*(1.0-rmix) + LUT2[offset[5]]*rmix))*(1.0-gmix) +
								((LUT2[offset[6]]*(1.0-rmix) + LUT2[offset[7]]*rmix))*gmix))*bmix);

						}

					/*	if(convert2YUV)
						{
							float yf,uf,vf;

							if(rf>1.0) rf = 1.0;
							if(gf>1.0) gf = 1.0;
							if(bf>1.0) bf = 1.0;
							if(rf<0.0) rf = 0.0;
							if(gf<0.0) gf = 0.0;
							if(bf<0.0) bf = 0.0;

							switch(colorspace & COLORSPACE_MASK)
							{
							case COLOR_SPACE_CG_601: //601
								yf =  0.257*rf + 0.504*gf + 0.098*bf + 0.0625;
								uf = -0.148*rf - 0.291*gf + 0.439*bf + 0.5;
								vf =  0.439*rf - 0.368*gf - 0.071*bf + 0.5;
								break;
							default: assert(0);
							case COLOR_SPACE_CG_709:
								yf =  0.183*rf + 0.614*gf + 0.062*bf + 0.0625;
								uf = -0.101*rf - 0.338*gf + 0.439*bf + 0.5;
								vf =  0.439*rf - 0.399*gf - 0.040*bf + 0.5;
								break;
							case COLOR_SPACE_VS_601: //VS 601
								yf =  0.299*rf + 0.587*gf + 0.114*bf;
								uf = -0.172*rf - 0.339*gf + 0.511*bf + 0.5;
								vf =  0.511*rf - 0.428*gf - 0.083*bf + 0.5;
								break;
							case COLOR_SPACE_VS_709:
								yf =  0.213*rf + 0.715*gf + 0.072*bf;
								uf = -0.117*rf - 0.394*gf + 0.511*bf + 0.5;
								vf =  0.511*rf - 0.464*gf - 0.047*bf + 0.5;
								break;
							}

							rf = yf;
							gf = uf;
							bf = vf;
						}*/


						ri = rf * 8192.0;
						gi = gf * 8192.0;
						bi = bf * 8192.0;

						if(ri < -32768) ri = -32768;
						if(ri > 32767) ri = 32767;
						if(gi < -32768) gi = -32768;
						if(gi > 32767) gi = 32767;
						if(bi < -32768) bi = -32768;
						if(bi > 32767) bi = 32767;

						RawCube[coordbase++] = ri;  // 13-bit = white
						RawCube[coordbase++] = gi;
						RawCube[coordbase++] = bi;
					}
				}
			}
#endif

#if 1  //SAVE as .Look
			if (cfhddata->export_look)
			{
				FILE *fp = NULL;
				int err = 0;

				cfhddata->export_look = 0;

#ifdef _WIN32
				err = fopen_s(&fp, cfhddata->look_export_path, "w");
#else
				fp = fopen(cfhddata->look_export_path, "w");
#endif
				if (err == 0 && fp)
				{
					int i=0,r,g,b,s = cube_depth+1;
					//short *pLUT = (short *)RawCube;

					fprintf(fp, "<?xml version=\"1.0\" ?>\n");
					fprintf(fp, "<look>\n");
					fprintf(fp, "  <LUT>\n");
					fprintf(fp, "    <size>\"%d\"</size>\n", cube_depth);
					fprintf(fp, "    <data>\"");

					for(b=0; b<cube_depth; b++)
					{
						for(g=0; g<cube_depth; g++)
						{
							for(r=0; r<cube_depth; r++)
							{
								uint32_t longval;
								if(i==0) fprintf(fp, "\n      ");						
								*((float *)&longval) = (float)RawCube[(r+g*s+b*s*s)*3]/8191.0f;  
								fprintf(fp, "%08X", _bswap(longval));
								i++; i&=7;
								
								if(i==0) fprintf(fp, "\n      ");						
								*((float *)&longval) = (float)RawCube[(r+g*s+b*s*s)*3+1]/8191.0f;  
								fprintf(fp, "%08X", _bswap(longval));
								i++; i&=7;
								
								if(i==0) fprintf(fp, "\n      ");						
								*((float *)&longval) = (float)RawCube[(r+g*s+b*s*s)*3+2]/8191.0f;  
								fprintf(fp, "%08X", _bswap(longval));
								i++; i&=7;
							}
						}
					}

					fprintf(fp, "\"\n");
					fprintf(fp, "    </data>\n");
					fprintf(fp, "  </LUT>\n");
					fprintf(fp, "</look>\n");

					fclose(fp);


					{
						char cubename[260];
						int err = 0;

						
#ifdef _WIN32
						strcpy_s(cubename, sizeof(cubename), cfhddata->look_export_path);
						cubename[strlen(cubename) - 4] = 0;
						strcat_s(cubename, sizeof(cubename), "cube");
#else
						strcpy(cubename, cfhddata->look_export_path);
						strcat(cubename, "cube");
						cubename[strlen(cubename) - 4] = 0;
#endif

#ifdef _WIN32
						err = fopen_s(&fp, cubename, "w");
#else
						fp = fopen(cubename, "w");
#endif
						if (err == 0 && fp)
						{
							int r,g,b,s = cube_depth+1;
							char fname[260] = "CubeExport";
							//short *pLUT = (short *)RawCube;
							
#ifdef _WIN32
							//_splitpath(cubename, NULL, NULL, fname, NULL);
							_splitpath_s(cubename, NULL, 0, NULL, 0, fname, sizeof(fname), NULL, 0);
#endif

							fprintf(fp, "\nTITLE \"%s\"\n\n", fname);
							fprintf(fp, "LUT_3D_SIZE %d\n\n", cube_depth);

							for(b=0; b<cube_depth; b++)
							{
								for(g=0; g<cube_depth; g++)
								{
									for(r=0; r<cube_depth; r++)
									{
										fprintf(fp, "%1.4f %1.4f %1.4f\n", 
											(float)RawCube[(r+g*s+b*s*s)*3]/8191.0, 
											(float)RawCube[(r+g*s+b*s*s)*3+1]/8191.0,
											(float)RawCube[(r+g*s+b*s*s)*3+2]/8191.0);
									}
								}
							}
							fclose(fp);
						}
					}
				}
			}
#endif

			retcode = true; //use the 3D system
		}
		else if(decoder->Linear2CurveBlu)// simplied 1D LUT system
		{
			int j;
			//int y;
			//int val;
			float scale = 8192.0; // 13-bit

			//store maxtrix & WB
			for(j=0; j<12; j++)
			{
				decoder->linear_color_matrix[j] = (int)(linear_mtrx[j>>2][j&3] * scale);
		//		decoder->linear_color_matrix_highlight_sat[j] = (int)(linear_mtrx_highlight_sat[j>>2][j&3] * scale);
				decoder->curved_color_matrix[j] = (int)(curved_mtrx[j>>2][j&3] * scale);
			}

#if 1
			decoder->cg_non_unity = cg_non_unity;
			decoder->contrast = contrast;
			decoder->cdl_sat = cdl_sat;
			decoder->red_gamma_tweak = red_gamma_tweak;
			decoder->grn_gamma_tweak = grn_gamma_tweak;
			decoder->blu_gamma_tweak = blu_gamma_tweak;
			decoder->encode_curve_type1D = encode_curve_type;
			decoder->encode_curvebase1D = encode_curvebase;
			decoder->decode_curvebase1D = decode_curvebase;

	#if 0
			Build1DCurves2Linear(decoder, 0, 1);
			Build1DLinear2Curves(decoder, 0, 1);
	#else

		#if _DELAY_THREAD_START
			if(decoder->worker_thread.pool.thread_count == 0)
			{
				CreateLock(&decoder->worker_thread.lock);
				// Initialize the pool of transform worker threads
				ThreadPoolCreate(&decoder->worker_thread.pool,
								decoder->thread_cntrl.capabilities >> 16/*cpus*/,
								WorkerThreadProc,
								decoder);
			}
		#endif
			
			{
				WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

				mailbox->jobType = JOB_TYPE_BUILD_1DS_2LINEAR; 
				// Set the work count to the number of cpus
				ThreadPoolSetWorkCount(&decoder->worker_thread.pool, decoder->thread_cntrl.capabilities >> 16/*cpus*/);
				// Start the worker threads
				ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);
				// Wait for all of the worker threads to finish
				ThreadPoolWaitAllDone(&decoder->worker_thread.pool);

				mailbox->jobType = JOB_TYPE_BUILD_1DS_2CURVE; 
				// Set the work count to the number of cpus
				ThreadPoolSetWorkCount(&decoder->worker_thread.pool, decoder->thread_cntrl.capabilities >> 16/*cpus*/);
				// Start the worker threads
				ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);
				// Wait for all of the worker threads to finish
				ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
			}
	#endif

			retcode = true;
#else
			for(j=-16384; j<16384+16384; j++) //-2 to 4
			{
				
				int encode_curve_neg = encode_curve_type & CURVE_TYPE_NEGATIVE;

				switch(encode_curve_type & CURVE_TYPE_MASK)
				{
				case CURVE_TYPE_LOG:
					val = (int)(CURVE_LOG2LIN((double)j/(double)(8192.0),
						(double)encode_curvebase) * 8192.0);
					break;
				case CURVE_TYPE_GAMMA:
					val = (int)(CURVE_GAM2LIN((double)j/(double)(8192.0),
						(double)encode_curvebase) * 8192.0);
					break;
				case CURVE_TYPE_CINEON:
					val = (int)(CURVE_CINEON2LIN((double)j/(double)(8192.0),
						(double)encode_curvebase) * 8192.0);
					break;
				case CURVE_TYPE_CINE985:
					val = (int)(CURVE_CINE9852LIN((double)j/(double)(8192.0),
						(double)encode_curvebase) * 8192.0);
					break;
				case CURVE_TYPE_PARA:
					val = (int)(CURVE_PARA2LIN((double)j/(double)(8192.0),
						(int)((cfhddata->encode_curve >> 8) & 0xff), (int)(cfhddata->encode_curve & 0xff)) * 8192.0);
					break;
				case CURVE_TYPE_LINEAR:
				default:
					val = j;
					break;
				}
				if(val > 32767) val = 32767;
				decoder->Curve2Linear[j+16384] = val;
			}

			{
				int value;
				float oneunit = 8192.0;
				int gain,power;
				if(decode_curve_type == CURVE_TYPE_PARA)
				{
					if(cfhddata->decode_curve && cfhddata->decode_curve != cfhddata->encode_curve)
					{
						gain = ((cfhddata->decode_curve >> 8) & 0xff);
						power = (cfhddata->decode_curve & 0xff);
					}
					else
					{
						gain = ((cfhddata->encode_curve >> 8) & 0xff);
						power = (cfhddata->encode_curve & 0xff);
					}
				}

				if(cg_non_unity)
				{
					for(j=0; j<=65535; j++)  // -2 to +6, 13-bit
					{
						float intensity = j - 16384;

						float valuer = CURVE_LIN2GAM(intensity/oneunit,red_gamma_tweak);
						float valueg = CURVE_LIN2GAM(intensity/oneunit,grn_gamma_tweak);
						float valueb = CURVE_LIN2GAM(intensity/oneunit,blu_gamma_tweak);

						if(contrast != 1.0)
						{
							valuer = calc_contrast(valuer,contrast);
							valueg = calc_contrast(valueg,contrast);
							valueb = calc_contrast(valueb,contrast);
						}

						value = valuer * oneunit;
						if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
						decoder->GammaContrastRed[j] = value;
						value = valueg * oneunit;
						if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
						decoder->GammaContrastGrn[j] = value;
						value = valueb * oneunit;
						if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
						decoder->GammaContrastBlu[j] = value;

						if(cfhddata->PrimariesUseDecodeCurve) // GammaContrast and Linear2Curve are separated, other Linear2Curve has both
							valuer = valueg = valueb = intensity / oneunit;

						switch(decode_curve_type & CURVE_TYPE_MASK)
						{
						case CURVE_TYPE_LOG:
							value = (int)(CURVE_LIN2LOG(valuer,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveRed[j] = value;
							value = (int)(CURVE_LIN2LOG(valueg,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveGrn[j] = value;
							value = (int)(CURVE_LIN2LOG(valueb,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveBlu[j] = value;
							break;
						case CURVE_TYPE_GAMMA:
							value = (int)(CURVE_LIN2GAM(valuer,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveRed[j] = value;
							value = (int)(CURVE_LIN2GAM(valueg,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveGrn[j] = value;
							value = (int)(CURVE_LIN2GAM(valueb,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveBlu[j] = value;
							break;
						case CURVE_TYPE_CINEON:
							value = (int)(CURVE_LIN2CINEON(valuer,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveRed[j] = value;
							value = (int)(CURVE_LIN2CINEON(valueg,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveGrn[j] = value;
							value = (int)(CURVE_LIN2CINEON(valueb,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveBlu[j] = value;
							break;
						case CURVE_TYPE_CINE985:
							value = (int)(CURVE_LIN2CINE985(valuer,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveRed[j] = value;
							value = (int)(CURVE_LIN2CINE985(valueg,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveGrn[j] = value;
							value = (int)(CURVE_LIN2CINE985(valueb,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveBlu[j] = value;
							break;
						case CURVE_TYPE_PARA:
							value = (int)(CURVE_LIN2PARA(valuer, gain, power) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveRed[j] = value;
							value = (int)(CURVE_LIN2PARA(valueg, gain, power) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveGrn[j] = value;
							value = (int)(CURVE_LIN2PARA(valueb, gain, power) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveBlu[j] = value;
							break;
						case CURVE_TYPE_LINEAR:
						default:
							decoder->Linear2CurveRed[j] = (int)(valuer * oneunit);
							decoder->Linear2CurveGrn[j] = (int)(valueg * oneunit);
							decoder->Linear2CurveBlu[j] = (int)(valueb * oneunit);
							break;
						}
					}

					decoder->use_three_1DLUTS = 1;
				}
				else
				{
					for(j=0; j<=65535; j++) // -2 to +6, 13-bit
					{
						float intensity = j - 16384;

						float valuer = CURVE_LIN2GAM(intensity/oneunit,red_gamma_tweak);
						if(contrast != 1.0)
							valuer = calc_contrast(valuer,contrast);

						value = valuer * oneunit;
						if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
						decoder->GammaContrastRed[j] = value;

						if(cfhddata->PrimariesUseDecodeCurve) // GammaContrast and Linear2Curve are separated, other Linear2Curve has both
							valuer = intensity / oneunit;

						switch(decode_curve_type & CURVE_TYPE_MASK)
						{
						case CURVE_TYPE_LOG:
							value = (int)(CURVE_LIN2LOG(valuer,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveRed[j] = value;
							break;
						case CURVE_TYPE_GAMMA:
							value = (int)(CURVE_LIN2GAM(valuer,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveRed[j] = value;
							break;
						case CURVE_TYPE_CINEON:
							value = (int)(CURVE_LIN2CINEON(valuer,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveRed[j] = value;
							break;
						case CURVE_TYPE_CINE985:
							value = (int)(CURVE_LIN2CINE985(valuer,decode_curvebase) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveRed[j] = value;
							break;
						case CURVE_TYPE_PARA:
							value = (int)(CURVE_LIN2PARA(valuer, gain, power) * oneunit);
							if(value < -32767) value = -32767;  if(value > 32767) value = 32767;
							decoder->Linear2CurveRed[j] = value;
							break;
						case CURVE_TYPE_LINEAR:
						default:
							decoder->Linear2CurveRed[j] = (int)(valuer * oneunit);
							break;
						}
					}
					decoder->use_three_1DLUTS = 0;
				}
				retcode = true; //use the 1D system
			}
#endif
		}
		else
		{
			retcode = false; //don't need to use the cube or 1D system
		}
	}
	else
	{
		cfhddata->PrimariesUseDecodeCurve = 0;
		decoder->use_three_1DLUTS = 0;
		memset(decoder->curved_color_matrix,0,sizeof(decoder->curved_color_matrix));
		memset(decoder->linear_color_matrix_highlight_sat,0,sizeof(decoder->linear_color_matrix_highlight_sat));
		memset(decoder->linear_color_matrix,0,sizeof(decoder->linear_color_matrix));
		decoder->linear_color_matrix[0] = 8192;
		decoder->linear_color_matrix[5] = 8192;
		decoder->linear_color_matrix[10] = 8192;
		decoder->curved_color_matrix[0] = 8192;
		decoder->curved_color_matrix[5] = 8192;
		decoder->curved_color_matrix[10] = 8192;

		retcode = false; //don't need to use the cube
	}
//cleanup:
	if(LUT && freeLUT) 
	{
#if _ALLOCATOR
		Free(decoder->allocator, LUT);
#else
		MEMORY_FREE(LUT);
#endif
	}

	if(retcode == false)
	{
//		decoder->frame.white_point = 0;
//		//decoder->frame.signed_pixels = 0;
		decoder->use_active_metadata_decoder = false;
		decoder->apply_color_active_metadata = false;
		if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
		{
			decoder->use_active_metadata_decoder = true;
		}

		if(	(decoder->frame.white_point != 16 && decoder->frame.white_point != 0) ||  // WP13 only work with the new decoder path
			(decoder->codec.encoded_format == ENCODED_FORMAT_YUV_422 && decoder->frame.resolution == DECODED_RESOLUTION_QUARTER) ) // HACK: some quarter YUY2 to output formats are missing, but not will LUTs applied
		{
			decoder->use_active_metadata_decoder = true;
		}


		if(decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444 && ALPHAOUTPUT(decoder->frame.format))
		{
			decoder->use_active_metadata_decoder = true;
			//decoder->apply_color_active_metadata = true;
		}
	}
	else
	{
		decoder->use_active_metadata_decoder = true;
		decoder->apply_color_active_metadata = true;
	}

	cfhddata->process_path_flags = process_path_flags;

	return;
}

//TODO what SSE can be added
#if 1
#define CURVES_PROCESSING_MACRO		\
	{																										\
		if(decoder->useFloatCC)																				\
		{																									\
			if(!decoder->linear_matrix_non_unity && !decoder->curve_change_active)							\
			{																								\
				if(decoder->curved_matrix_non_unity)														\
				{																							\
					rn = (int)(((float)ccm[0] * (float)ri + (float)ccm[1] * (float)gi + (float)ccm[2]* (float)bi)/8192.0) + ccm[3]; /*13-bit output*/			\
					gn = (int)(((float)ccm[4] * (float)ri + (float)ccm[5] * (float)gi + (float)ccm[6]* (float)bi)/8192.0) + ccm[7];								\
					bn = (int)(((float)ccm[8] * (float)ri + (float)ccm[9] * (float)gi + (float)ccm[10]*(float)bi)/8192.0) + ccm[11];							\
																											\
					if(rn < -16384) rn = -16384; if(rn > 32767) rn = 32767;									\
					if(gn < -16384) gn = -16384; if(gn > 32767) gn = 32767;									\
					if(bn < -16384) bn = -16384; if(bn > 32767) bn = 32767;									\
																											\
					ri = rn;																				\
					gi = gn;																				\
					bi = bn;																				\
				}																							\
				if(decoder->contrast_gamma_non_unity)														\
				{																							\
					if(decoder->use_three_1DLUTS)															\
					{																						\
						ri = decoder->GammaContrastRed[ri+16384];/*13-bit output*/							\
						gi = decoder->GammaContrastGrn[gi+16384];											\
						bi = decoder->GammaContrastBlu[bi+16384];											\
					}																						\
					else																					\
					{																						\
						ri = decoder->GammaContrastRed[ri+16384];/*13-bit output*/							\
						gi = decoder->GammaContrastRed[gi+16384];											\
						bi = decoder->GammaContrastRed[bi+16384];											\
					}																						\
				}																							\
			}																								\
			else if(decoder->Curve2Linear)																	\
			{																								\
				ri = decoder->Curve2Linear[ri+16384]; /*13-bit output*/										\
				gi = decoder->Curve2Linear[gi+16384];														\
				bi = decoder->Curve2Linear[bi+16384];														\
				if(decoder->linear_matrix_non_unity)														\
				{																							\
					rn = (int)(((float)lcm[0] * (float)ri + (float)lcm[1] * (float)gi + (float)lcm[2]* (float)bi)/8192.0) + lcm[3]; /*13-bit output*/			\
					gn = (int)(((float)lcm[4] * (float)ri + (float)lcm[5] * (float)gi + (float)lcm[6]* (float)bi)/8192.0) + lcm[7];								\
					bn = (int)(((float)lcm[8] * (float)ri + (float)lcm[9] * (float)gi + (float)lcm[10]*(float)bi)/8192.0) + lcm[11];							\
																											\
					/* Linear2Cruve supports range -2 to +6	*/												\
					if(rn < -16384) rn = -16384;	if(rn > 49151) rn = 49151;								\
					if(gn < -16384) gn = -16384;	if(gn > 49151) gn = 49151;								\
					if(bn < -16384) bn = -16384;	if(bn > 49151) bn = 49151;								\
					ri = rn;																				\
					gi = gn;																				\
					bi = bn;																				\
				}																							\
				if(cfhddata->PrimariesUseDecodeCurve)														\
				{																							\
					if(decoder->use_three_1DLUTS)															\
					{																						\
						ri = decoder->Linear2CurveRed[ri+16384];/*13-bit output*/							\
						gi = decoder->Linear2CurveGrn[gi+16384];											\
						bi = decoder->Linear2CurveBlu[bi+16384];											\
					}																						\
					else																					\
					{																						\
						ri = decoder->Linear2CurveRed[ri+16384];/*13-bit output*/							\
						gi = decoder->Linear2CurveRed[gi+16384];											\
						bi = decoder->Linear2CurveRed[bi+16384];											\
					}																						\
					if(decoder->curved_matrix_non_unity)													\
					{																						\
						rn = (int)(((float)ccm[0] * (float)ri + (float)ccm[1] * (float)gi + (float)ccm[2]* (float)bi)/8192.0) + ccm[3]; /*13-bit output*/	\
						gn = (int)(((float)ccm[4] * (float)ri + (float)ccm[5] * (float)gi + (float)ccm[6]* (float)bi)/8192.0) + ccm[7];						\
						bn = (int)(((float)ccm[8] * (float)ri + (float)ccm[9] * (float)gi + (float)ccm[10]*(float)bi)/8192.0) + ccm[11];					\
																											\
						if(rn < -16384) rn = -16384; if(rn > 32767) rn = 32767;								\
						if(gn < -16384) gn = -16384; if(gn > 32767) gn = 32767;								\
						if(bn < -16384) bn = -16384; if(bn > 32767) bn = 32767;								\
																											\
						ri = rn;																			\
						gi = gn;																			\
						bi = bn;																			\
					}																						\
					if(decoder->contrast_gamma_non_unity)													\
					{																						\
						if(decoder->use_three_1DLUTS)														\
						{																					\
							ri = decoder->GammaContrastRed[ri+16384];/*13-bit output*/						\
							gi = decoder->GammaContrastGrn[gi+16384];										\
							bi = decoder->GammaContrastBlu[bi+16384];										\
						}																					\
						else																				\
						{																					\
							ri = decoder->GammaContrastRed[ri+16384];/*13-bit output*/						\
							gi = decoder->GammaContrastRed[gi+16384];										\
							bi = decoder->GammaContrastRed[bi+16384];										\
						}																					\
					}																						\
				}																							\
				else																						\
				{																							\
					if(decoder->use_three_1DLUTS)															\
					{																						\
						ri = decoder->Linear2CurveRed[ri+16384];/*13-bit output*/							\
						gi = decoder->Linear2CurveGrn[gi+16384];											\
						bi = decoder->Linear2CurveBlu[bi+16384];											\
					}																						\
					else																					\
					{																						\
						ri = decoder->Linear2CurveRed[ri+16384];/*13-bit output*/							\
						gi = decoder->Linear2CurveRed[gi+16384];											\
						bi = decoder->Linear2CurveRed[bi+16384];											\
					}																						\
				}																							\
			}																								\
																											\
			if(sat!=8192)																					\
			{																								\
				int luma = (1738*ri + 5889*gi + 591*bi)>>13;												\
				ri = luma + (sat * (ri - luma) >> 13);														\
				gi = luma + (sat * (gi - luma) >> 13);														\
				bi = luma + (sat * (bi - luma) >> 13);														\
				if(ri < -16384) ri = -16384; if(ri > 32767) ri = 32767;										\
				if(gi < -16384) gi = -16384; if(gi > 32767) gi = 32767;										\
				if(bi < -16384) bi = -16384; if(bi > 32767) bi = 32767;										\
			}																								\
		}																									\
		else																								\
		{																									\
			if(!decoder->linear_matrix_non_unity && !decoder->curve_change_active)							\
			{																								\
				if(decoder->curved_matrix_non_unity)														\
				{																							\
					rn = ((ccm[0] * ri + ccm[1] * gi + ccm[2]* bi)>>13) + ccm[3]; /*13-bit output*/			\
					gn = ((ccm[4] * ri + ccm[5] * gi + ccm[6]* bi)>>13) + ccm[7];							\
					bn = ((ccm[8] * ri + ccm[9] * gi + ccm[10]*bi)>>13) + ccm[11];							\
																											\
					if(rn < -16384) rn = -16384; if(rn > 32767) rn = 32767;									\
					if(gn < -16384) gn = -16384; if(gn > 32767) gn = 32767;									\
					if(bn < -16384) bn = -16384; if(bn > 32767) bn = 32767;									\
																											\
					ri = rn;																				\
					gi = gn;																				\
					bi = bn;																				\
				}																							\
				if(decoder->contrast_gamma_non_unity)														\
				{																							\
					if(decoder->use_three_1DLUTS)															\
					{																						\
						ri = decoder->GammaContrastRed[ri+16384];/*13-bit output*/							\
						gi = decoder->GammaContrastGrn[gi+16384];											\
						bi = decoder->GammaContrastBlu[bi+16384];											\
					}																						\
					else																					\
					{																						\
						ri = decoder->GammaContrastRed[ri+16384];/*13-bit output*/							\
						gi = decoder->GammaContrastRed[gi+16384];											\
						bi = decoder->GammaContrastRed[bi+16384];											\
					}																						\
				}																							\
			}																								\
			else if(decoder->Curve2Linear)																	\
			{																								\
				ri = decoder->Curve2Linear[ri+16384]; /*13-bit output*/										\
				gi = decoder->Curve2Linear[gi+16384];														\
				bi = decoder->Curve2Linear[bi+16384];														\
				if(decoder->linear_matrix_non_unity)														\
				{																							\
					rn = ((lcm[0] * ri + lcm[1] * gi + lcm[2]* bi)>>13) + lcm[3]; /*13-bit output*/			\
					gn = ((lcm[4] * ri + lcm[5] * gi + lcm[6]* bi)>>13) + lcm[7];							\
					bn = ((lcm[8] * ri + lcm[9] * gi + lcm[10]*bi)>>13) + lcm[11];							\
																											\
					/* Linear2Cruve supports range -2 to +6	*/												\
					if(rn < -16384) rn = -16384;	if(rn > 49151) rn = 49151;								\
					if(gn < -16384) gn = -16384;	if(gn > 49151) gn = 49151;								\
					if(bn < -16384) bn = -16384;	if(bn > 49151) bn = 49151;								\
					ri = rn;																				\
					gi = gn;																				\
					bi = bn;																				\
				}																							\
				if(cfhddata->PrimariesUseDecodeCurve)														\
				{																							\
					if(decoder->use_three_1DLUTS)															\
					{																						\
						ri = decoder->Linear2CurveRed[ri+16384];/*13-bit output*/							\
						gi = decoder->Linear2CurveGrn[gi+16384];											\
						bi = decoder->Linear2CurveBlu[bi+16384];											\
					}																						\
					else																					\
					{																						\
						ri = decoder->Linear2CurveRed[ri+16384];/*13-bit output*/							\
						gi = decoder->Linear2CurveRed[gi+16384];											\
						bi = decoder->Linear2CurveRed[bi+16384];											\
					}																						\
					if(decoder->curved_matrix_non_unity)													\
					{																						\
						rn = ((ccm[0] * ri + ccm[1] * gi + ccm[2]* bi)>>13) + ccm[3]; /*13-bit output*/		\
						gn = ((ccm[4] * ri + ccm[5] * gi + ccm[6]* bi)>>13) + ccm[7];						\
						bn = ((ccm[8] * ri + ccm[9] * gi + ccm[10]*bi)>>13) + ccm[11];						\
																											\
						if(rn < -16384) rn = -16384; if(rn > 32767) rn = 32767;								\
						if(gn < -16384) gn = -16384; if(gn > 32767) gn = 32767;								\
						if(bn < -16384) bn = -16384; if(bn > 32767) bn = 32767;								\
																											\
						ri = rn;																			\
						gi = gn;																			\
						bi = bn;																			\
					}																						\
					if(decoder->contrast_gamma_non_unity)													\
					{																						\
						if(decoder->use_three_1DLUTS)														\
						{																					\
							ri = decoder->GammaContrastRed[ri+16384];/*13-bit output*/						\
							gi = decoder->GammaContrastGrn[gi+16384];										\
							bi = decoder->GammaContrastBlu[bi+16384];										\
						}																					\
						else																				\
						{																					\
							ri = decoder->GammaContrastRed[ri+16384];/*13-bit output*/						\
							gi = decoder->GammaContrastRed[gi+16384];										\
							bi = decoder->GammaContrastRed[bi+16384];										\
						}																					\
					}																						\
				}																							\
				else																						\
				{																							\
					if(decoder->use_three_1DLUTS)															\
					{																						\
						ri = decoder->Linear2CurveRed[ri+16384];/*13-bit output*/							\
						gi = decoder->Linear2CurveGrn[gi+16384];											\
						bi = decoder->Linear2CurveBlu[bi+16384];											\
					}																						\
					else																					\
					{																						\
						ri = decoder->Linear2CurveRed[ri+16384];/*13-bit output*/							\
						gi = decoder->Linear2CurveRed[gi+16384];											\
						bi = decoder->Linear2CurveRed[bi+16384];											\
					}																						\
				}																							\
			}																								\
																											\
			if(sat!=8192)																					\
			{																								\
				int luma = (1738*ri + 5889*gi + 591*bi)>>13;												\
				ri = luma + (sat * (ri - luma) >> 13);														\
				gi = luma + (sat * (gi - luma) >> 13);														\
				bi = luma + (sat * (bi - luma) >> 13);														\
				if(ri < -16384) ri = -16384; if(ri > 32767) ri = 32767;										\
				if(gi < -16384) gi = -16384; if(gi > 32767) gi = 32767;										\
				if(bi < -16384) bi = -16384; if(bi > 32767) bi = 32767;										\
			}																								\
		}																									\
	}


#endif

#ifdef _WIN32
#pragma warning(disable: 4554)
#endif


//unsigned short *ApplyActiveMetaData(DECODER *decoder, int width, int height, int ypos, unsigned short *src, unsigned short *dst, int colorformat, int *whitebitdepth, int *flags)
void *ApplyActiveMetaData(DECODER *decoder, int width, int height, int ypos,
						  uint32_t *src, uint32_t *dst, int colorformat, int *whitebitdepth,
						  int *flags)
{
	CFHDDATA *cfhddata = &decoder->cfhddata;
	//CFHDDATA *Cube_cfhddata = &decoder->Cube_cfhddata;
	short *RawCube = decoder->RawCube;
	int process_path_flags = cfhddata->process_path_flags;
	int cube_base = decoder->cube_base;
	int cube_depth = ((1<<cube_base)+1);


	int cube_shift_dn = (16-cube_base);
	int cube_depth_mask = ((1<<cube_shift_dn)-1);
	int split = (int)(decoder->cfhddata.split_CC_position * (float)width) & 0xfff8;

	if(decoder->cfhddata.split_CC_position <= 0.0) split = 0;

	if(cfhddata->process_path_flags_mask)
	{
		process_path_flags &= cfhddata->process_path_flags_mask;
		if((cfhddata->process_path_flags_mask & 0xffff) == 7) // CM+WB+ACTIVE hack to force CM on
		{
			process_path_flags |= PROCESSING_COLORMATRIX|PROCESSING_ACTIVE;  // DAN20080225
		}
	}
	if((process_path_flags == 0 || process_path_flags == PROCESSING_ACTIVE) && cfhddata->encode_curve == cfhddata->decode_curve) //nothing on
	{
		if(*flags & ACTIVEMETADATA_PLANAR)
		{
int lines;
			for(lines=0; lines<height; lines++)
			{
				int x;
				//int xx;
				uint16_t *rgb = (uint16_t *)src;
				uint16_t *rptr = rgb;
				uint16_t *gptr = &rgb[width];
				uint16_t *bptr = &rgb[width*2];
				int16_t *rgbout = (int16_t *)dst;

				if(decoder->RGBFilterBufferPhase == 1) // decoder order
				{
					gptr = rgb;
					rptr = &rgb[width];
					bptr = &rgb[width*2];
				}

				rgb += width*lines*3;
				rptr += width*lines*3;
				gptr += width*lines*3;
				bptr += width*lines*3;
				rgbout += width*lines*3;

				for(x=0;x<width; x++)
				{
					*rgbout++ = *rptr++;
					*rgbout++ = *gptr++;
					*rgbout++ = *bptr++;
				}
			}
			if(*whitebitdepth == 16 || *whitebitdepth==0)  // might be signed 13 bit.
			{
				*whitebitdepth = 16;
				*flags = ACTIVEMETADATA_PRESATURATED;
			}
			return dst;
		}
		else if(*flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
		{
			int lines;

			*flags &= ~ACTIVEMETADATA_SRC_8PIXEL_PLANAR;

			for(lines=0; lines<height; lines++)
			{
				int x,xx;
				short *rgb = (short *)src;
				short *rgbout = (short *)dst;

				rgb += width*lines*3;
				rgbout += width*lines*3;

				for(x=0;x<width; x+=8)
				{
					short *rgbsegment = rgb;

					rgb+= 8*3;

					for(xx=0;xx<8; xx++)
					{
						int ri,gi,bi;
						ri = rgbsegment[0];
						gi = rgbsegment[8];
						bi = rgbsegment[16];
						rgbsegment++;

						*rgbout++ = ri;
						*rgbout++ = gi;
						*rgbout++ = bi;
					}
				}
			}

			if(*whitebitdepth == 16 || *whitebitdepth==0)  // might be signed 13 bit.
			{
				*whitebitdepth = 16;
				*flags = ACTIVEMETADATA_PRESATURATED;
			}
			return dst;
		}
		else
		{
			if(*whitebitdepth == 16 || *whitebitdepth==0)  // might be signed 13 bit.
			{
				*whitebitdepth = 16;
				*flags = ACTIVEMETADATA_PRESATURATED;
			}
			return src;
		}
	}
	else if((process_path_flags & PROCESSING_LOOK_FILE || decoder->forceBuildLUT) && RawCube)
	{
		int lines;
		short *cube = RawCube;
		//int convert2YUV = 0;
		//if(LUTYUV(colorformat))
		//	convert2YUV = 1;


		for(lines=0; lines<height; lines++)
		{
			int x,xx,y = lines;
			unsigned short *rgb = (unsigned short *)src;
			short *rgbout = (short *)dst;

			rgb += width*y*3;
			rgbout += width*lines*3;

			if(*flags & ACTIVEMETADATA_PLANAR) // Path used by 444 source.
			{
				if(*whitebitdepth == 0 || *whitebitdepth == 16)
				{	
					unsigned short *rptr,*gptr,*bptr;

					rptr = rgb;
					gptr = &rgb[width];
					bptr = &rgb[width*2];

					if(decoder->RGBFilterBufferPhase == 1) // decoder order
					{
						gptr = rgb;
						rptr = &rgb[width];
						bptr = &rgb[width*2];
					}

					for(x=0; x<split; x++)
					{
						*rgbout++ = *rptr++ >> 3;
						*rgbout++ = *gptr++ >> 3;
						*rgbout++ = *bptr++ >> 3;
					}

					if(decoder->RawCubeThree1Ds)
					{	
						if(cube_base == 5)
						{	
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = *rptr++;
								gi = *gptr++;
								bi = *bptr++;

								rmix = (ri & 0x7ff);
								gmix = (gi & 0x7ff);
								bmix = (bi & 0x7ff);

								ri>>=11;
								gi>>=11;
								bi>>=11;

								rmixd = 2048 - rmix;
								gmixd = 2048 - gmix;
								bmixd = 2048 - bmix;
										
								offset = bi*33*33*3 + gi*33*3 + ri*3;
								ri = (cube[offset+0]*rmixd + cube[offset+33*33*3+33*3+3]*rmix)>>11;
								gi = (cube[offset+1]*gmixd + cube[offset+33*33*3+33*3+4]*gmix)>>11;
								bi = (cube[offset+2]*bmixd + cube[offset+33*33*3+33*3+5]*bmix)>>11;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
						else if(cube_base == 6)
						{
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = *rptr++;
								gi = *gptr++;
								bi = *bptr++;

								rmix = (ri & 0x3ff);
								gmix = (gi & 0x3ff);
								bmix = (bi & 0x3ff);

								ri>>=10;
								gi>>=10;
								bi>>=10;

								rmixd = 1024 - rmix;
								gmixd = 1024 - gmix;
								bmixd = 1024 - bmix;
										
								offset = bi*65*65*3 + gi*65*3 + ri*3;
								ri = (cube[offset+0]*rmixd + cube[offset+65*65*3+65*3+3]*rmix)>>10;
								gi = (cube[offset+1]*gmixd + cube[offset+65*65*3+65*3+4]*gmix)>>10;
								bi = (cube[offset+2]*bmixd + cube[offset+65*65*3+65*3+5]*bmix)>>10;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
						else
						{	
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = *rptr++;
								gi = *gptr++;
								bi = *bptr++;

								rmix = (ri & cube_depth_mask);
								gmix = (gi & cube_depth_mask);
								bmix = (bi & cube_depth_mask);

								ri>>=cube_shift_dn;
								gi>>=cube_shift_dn;
								bi>>=cube_shift_dn;

								rmixd = cube_depth_mask+1 - rmix;
								gmixd = cube_depth_mask+1 - gmix;
								bmixd = cube_depth_mask+1 - bmix;

								offset = bi*cube_depth*cube_depth*3 + gi*cube_depth*3 + ri*3;

								ri = (cube[offset+0]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn;
								gi = (cube[offset+1]*gmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+4]*gmix)>>cube_shift_dn;
								bi = (cube[offset+2]*bmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+5]*bmix)>>cube_shift_dn;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
					}
					else
					{
						if(cube_base == 5)
						{	
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = *rptr++;
								gi = *gptr++;
								bi = *bptr++;

								rmix = (ri & 0x7ff);
								gmix = (gi & 0x7ff);
								bmix = (bi & 0x7ff);

								ri>>=11;
								gi>>=11;
								bi>>=11;

								rmixd = 2048 - rmix;
								gmixd = 2048 - gmix;
								bmixd = 2048 - bmix;
										
								offset = bi*33*33*3 + gi*33*3 + ri*3;	
								
								ri = ((((((cube[offset+0]*rmixd + cube[offset+3]*rmix)>>11)*gmixd +
									((cube[offset+33*3]*rmixd + cube[offset+33*3+3]*rmix)>>11)*gmix)>>11)*bmixd) +
									(((((cube[offset+33*33*3]*rmixd + cube[offset+33*33*3+3]*rmix)>>11)*gmixd +
									((cube[offset+33*33*3+33*3]*rmixd + cube[offset+33*33*3+33*3+3]*rmix)>>11)*gmix)>>11)*bmix))>>11;

								gi = ((((((cube[offset+1]*rmixd + cube[offset+4]*rmix)>>11)*gmixd +
									((cube[offset+33*3+1]*rmixd + cube[offset+33*3+4]*rmix)>>11)*gmix)>>11)*bmixd) +
									(((((cube[offset+33*33*3+1]*rmixd + cube[offset+33*33*3+4]*rmix)>>11)*gmixd +
									((cube[offset+33*33*3+33*3+1]*rmixd + cube[offset+33*33*3+33*3+4]*rmix)>>11)*gmix)>>11)*bmix))>>11;

								bi = ((((((cube[offset+2]*rmixd + cube[offset+5]*rmix)>>11)*gmixd +
									((cube[offset+33*3+2]*rmixd + cube[offset+33*3+5]*rmix)>>11)*gmix)>>11)*bmixd) +
									(((((cube[offset+33*33*3+2]*rmixd + cube[offset+33*33*3+5]*rmix)>>11)*gmixd +
									((cube[offset+33*33*3+33*3+2]*rmixd + cube[offset+33*33*3+33*3+5]*rmix)>>11)*gmix)>>11)*bmix))>>11;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
						else if(cube_base == 6)
						{					
							for(;x<width; x++)
							{			
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = *rptr++;
								gi = *gptr++;
								bi = *bptr++;
								
								rmix = (ri & 0x3ff);
								gmix = (gi & 0x3ff);
								bmix = (bi & 0x3ff);

								ri>>=10;
								gi>>=10;
								bi>>=10;

								rmixd = 1024 - rmix;
								gmixd = 1024 - gmix;
								bmixd = 1024 - bmix;
										
								offset = bi*65*65*3 + gi*65*3 + ri*3;
								ri = ((((((cube[offset+0]*rmixd + cube[offset+3]*rmix)>>10)*gmixd +
									((cube[offset+65*3]*rmixd + cube[offset+65*3+3]*rmix)>>10)*gmix)>>10)*bmixd) +
									(((((cube[offset+65*65*3]*rmixd + cube[offset+65*65*3+3]*rmix)>>10)*gmixd +
									((cube[offset+65*65*3+65*3]*rmixd + cube[offset+65*65*3+65*3+3]*rmix)>>10)*gmix)>>10)*bmix))>>10;

								gi = ((((((cube[offset+1]*rmixd + cube[offset+4]*rmix)>>10)*gmixd +
									((cube[offset+65*3+1]*rmixd + cube[offset+65*3+4]*rmix)>>10)*gmix)>>10)*bmixd) +
									(((((cube[offset+65*65*3+1]*rmixd + cube[offset+65*65*3+4]*rmix)>>10)*gmixd +
									((cube[offset+65*65*3+65*3+1]*rmixd + cube[offset+65*65*3+65*3+4]*rmix)>>10)*gmix)>>10)*bmix))>>10;

								bi = ((((((cube[offset+2]*rmixd + cube[offset+5]*rmix)>>10)*gmixd +
									((cube[offset+65*3+2]*rmixd + cube[offset+65*3+5]*rmix)>>10)*gmix)>>10)*bmixd) +
									(((((cube[offset+65*65*3+2]*rmixd + cube[offset+65*65*3+5]*rmix)>>10)*gmixd +
									((cube[offset+65*65*3+65*3+2]*rmixd + cube[offset+65*65*3+65*3+5]*rmix)>>10)*gmix)>>10)*bmix))>>10;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
						else
						{							
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = *rptr++;
								gi = *gptr++;
								bi = *bptr++;
								
								rmix = (ri & cube_depth_mask);
								gmix = (gi & cube_depth_mask);
								bmix = (bi & cube_depth_mask);

								ri>>=cube_shift_dn;
								gi>>=cube_shift_dn;
								bi>>=cube_shift_dn;

								rmixd = cube_depth_mask+1 - rmix;
								gmixd = cube_depth_mask+1 - gmix;
								bmixd = cube_depth_mask+1 - bmix;

								offset = bi*cube_depth*cube_depth*3 + gi*cube_depth*3 + ri*3;
								ri = ((((((cube[offset+0]*rmixd + cube[offset+3]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*3]*rmixd + cube[offset+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
									(((((cube[offset+cube_depth*cube_depth*3]*rmixd + cube[offset+cube_depth*cube_depth*3+3]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*cube_depth*3+cube_depth*3]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

								gi = ((((((cube[offset+1]*rmixd + cube[offset+4]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*3+1]*rmixd + cube[offset+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
									(((((cube[offset+cube_depth*cube_depth*3+1]*rmixd + cube[offset+cube_depth*cube_depth*3+4]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*cube_depth*3+cube_depth*3+1]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;
                                
								bi = ((((((cube[offset+2]*rmixd + cube[offset+5]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*3+2]*rmixd + cube[offset+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
									(((((cube[offset+cube_depth*cube_depth*3+2]*rmixd + cube[offset+cube_depth*cube_depth*3+5]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*cube_depth*3+cube_depth*3+2]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;
									
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
					}
				}
				else 
				{
					short *rptr,*gptr,*bptr;

					rptr = (short *)rgb;
					gptr = (short *)&rgb[width];
					bptr = (short *)&rgb[width*2];

					if(decoder->RGBFilterBufferPhase == 1) // decoder order
					{
						gptr = (short *)rgb;
						rptr = (short *)&rgb[width];
						bptr = (short *)&rgb[width*2];
					}

					for(x=0;x<width; x++)
					{
						int ri,gi,bi;
						int rmix,gmix,bmix;
						int rmixd,gmixd,bmixd;
						short *sptr;
						ri = *(short *)rptr++; // signed 13-bit
						gi = *(short *)gptr++;
						bi = *(short *)bptr++;

						
						
						if(x>=split)
						{  
							ri <<= 3;
							gi <<= 3;
							bi <<= 3;
							
							if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
							if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
							if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;

							rmix = (ri & cube_depth_mask);
							gmix = (gi & cube_depth_mask);
							bmix = (bi & cube_depth_mask);

							ri>>=cube_shift_dn;
							gi>>=cube_shift_dn;
							bi>>=cube_shift_dn;

							rmixd = cube_depth_mask+1 - rmix;
							gmixd = cube_depth_mask+1 - gmix;
							bmixd = cube_depth_mask+1 - bmix;

							sptr = &cube[(bi*cube_depth*cube_depth + gi*cube_depth + ri)*3];

							ri = ((((((sptr[0]*rmixd + sptr[3]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*3]*rmixd + sptr[cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
								(((((sptr[cube_depth*cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+3]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*cube_depth*3+cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

							gi = ((((((sptr[1]*rmixd + sptr[4]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*3+1]*rmixd + sptr[cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
								(((((sptr[cube_depth*cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+4]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*cube_depth*3+cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

							bi = ((((((sptr[2]*rmixd + sptr[5]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*3+2]*rmixd + sptr[cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
								(((((sptr[cube_depth*cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+5]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*cube_depth*3+cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

						/*	if(convert2YUV)
							{
								rgbout[0] = ri;
								rgbout[width] = gi;
								rgbout[width*2] = bi;
								rgbout++;
							}
							else */
						}

						rgbout[0] = ri;
						rgbout[1] = gi;
						rgbout[2] = bi;
						rgbout+=3;
					}
				}
			}
			else if(*flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
			{

				if(*whitebitdepth == 0 || *whitebitdepth == 16) // Haven't found when this path is used
				{
					//assert(0);// unoptimized, otherwise no issue.
					for(x=0;x<width; x+=8)
					{
						unsigned short *rgbsegment = rgb;

						rgb+= 8*3;

						for(xx=0;xx<8; xx++)
						{
							int ri,gi,bi;
							int rmix,gmix,bmix;
							int rmixd,gmixd,bmixd;
							short *sptr;
							ri = rgbsegment[0];
							gi = rgbsegment[8];
							bi = rgbsegment[16];
							rgbsegment++;
							
						
							if(x>=split)
							{

								rmix = (ri & cube_depth_mask);
								gmix = (gi & cube_depth_mask);
								bmix = (bi & cube_depth_mask);

								ri>>=cube_shift_dn;
								gi>>=cube_shift_dn;
								bi>>=cube_shift_dn;

								rmixd = cube_depth_mask+1 - rmix;
								gmixd = cube_depth_mask+1 - gmix;
								bmixd = cube_depth_mask+1 - bmix;

								sptr = &cube[(bi*cube_depth*cube_depth + gi*cube_depth + ri)*3];

								ri = ((((((sptr[0]*rmixd + sptr[3]*rmix)>>cube_shift_dn)*gmixd +
									((sptr[cube_depth*3]*rmixd + sptr[cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
									(((((sptr[cube_depth*cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+3]*rmix)>>cube_shift_dn)*gmixd +
									((sptr[cube_depth*cube_depth*3+cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

								gi = ((((((sptr[1]*rmixd + sptr[4]*rmix)>>cube_shift_dn)*gmixd +
									((sptr[cube_depth*3+1]*rmixd + sptr[cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
									(((((sptr[cube_depth*cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+4]*rmix)>>cube_shift_dn)*gmixd +
									((sptr[cube_depth*cube_depth*3+cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

								bi = ((((((sptr[2]*rmixd + sptr[5]*rmix)>>cube_shift_dn)*gmixd +
									((sptr[cube_depth*3+2]*rmixd + sptr[cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
									(((((sptr[cube_depth*cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+5]*rmix)>>cube_shift_dn)*gmixd +
									((sptr[cube_depth*cube_depth*3+cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

							/*	if(convert2YUV)
								{
									rgbout[0] = ri;
									rgbout[width] = gi;
									rgbout[width*2] = bi;
									rgbout++;
								}
								else*/
							}
							else
							{
								ri >>= 3;
								gi >>= 3;
								bi >>= 3;
							}

							rgbout[0] = ri;
							rgbout[1] = gi;
							rgbout[2] = bi;
							rgbout+=3;
						}
					}
				}
				else // 13-bit white point ACTIVEMETADATA_SRC_8PIXEL_PLANAR, used for 422 decodes
				{
					for(x=0;x<width; x+=8)
					{
						short *rgbsegment = (short *)rgb;

						rgb+= 8*3;

						if(x>=split)
						{
							if(decoder->RawCubeThree1Ds)
							{	
								if(cube_base == 5)
								{
									for(xx=0;xx<8; xx++)
									{
										int ri,gi,bi;
										int rmix,gmix,bmix;
										int rmixd,gmixd,bmixd;
										int offset;

										ri = rgbsegment[xx+0]; // signed 13-bit
										gi = rgbsegment[xx+8];
										bi = rgbsegment[xx+16];
									
										ri <<= 3; // signed 16-bit
										gi <<= 3;
										bi <<= 3;

										if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
										if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
										if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;

										rmix = (ri & 0x7ff);
										gmix = (gi & 0x7ff);
										bmix = (bi & 0x7ff);

										ri>>=11;
										gi>>=11;
										bi>>=11;

										rmixd = 2048 - rmix;
										gmixd = 2048 - gmix;
										bmixd = 2048 - bmix;
												
										offset = bi*33*33*3 + gi*33*3 + ri*3;
										ri = (cube[offset+0]*rmixd + cube[offset+33*33*3+33*3+3]*rmix)>>11;
										gi = (cube[offset+1]*gmixd + cube[offset+33*33*3+33*3+4]*gmix)>>11;
										bi = (cube[offset+2]*bmixd + cube[offset+33*33*3+33*3+5]*bmix)>>11;
										
										rgbout[0] = ri;
										rgbout[1] = gi;
										rgbout[2] = bi;
										rgbout+=3;
									}
								}
								else if(cube_base == 6)
								{
									for(xx=0;xx<8; xx++)
									{
										int ri,gi,bi;
										int rmix,gmix,bmix;
										int rmixd,gmixd,bmixd;
										int offset;

										ri = rgbsegment[xx+0]; // signed 13-bit
										gi = rgbsegment[xx+8];
										bi = rgbsegment[xx+16];
									
										ri <<= 3; // signed 16-bit
										gi <<= 3;
										bi <<= 3;

										if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
										if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
										if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;

										rmix = (ri & 0x3ff);
										gmix = (gi & 0x3ff);
										bmix = (bi & 0x3ff);

										ri>>=10;
										gi>>=10;
										bi>>=10;

										rmixd = 1024 - rmix;
										gmixd = 1024 - gmix;
										bmixd = 1024 - bmix;
												
										offset = bi*65*65*3 + gi*65*3 + ri*3;
										ri = (cube[offset+0]*rmixd + cube[offset+65*65*3+65*3+3]*rmix)>>10;
										gi = (cube[offset+1]*gmixd + cube[offset+65*65*3+65*3+4]*gmix)>>10;
										bi = (cube[offset+2]*bmixd + cube[offset+65*65*3+65*3+5]*bmix)>>10;
										
										rgbout[0] = ri;
										rgbout[1] = gi;
										rgbout[2] = bi;
										rgbout+=3;
									}
								}
								else
								{
									for(xx=0;xx<8; xx++)
									{
										int ri,gi,bi;
										int rmix,gmix,bmix;
										int rmixd,gmixd,bmixd;
										int offset;

										ri = rgbsegment[xx+0]; // signed 13-bit
										gi = rgbsegment[xx+8];
										bi = rgbsegment[xx+16];
									
										ri <<= 3; // signed 16-bit
										gi <<= 3;
										bi <<= 3;

										if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
										if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
										if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;

										rmix = (ri & cube_depth_mask);
										gmix = (gi & cube_depth_mask);
										bmix = (bi & cube_depth_mask);

										ri>>=cube_shift_dn;
										gi>>=cube_shift_dn;
										bi>>=cube_shift_dn;

										rmixd = cube_depth_mask+1 - rmix;
										gmixd = cube_depth_mask+1 - gmix;
										bmixd = cube_depth_mask+1 - bmix;

										offset = bi*cube_depth*cube_depth*3 + gi*cube_depth*3 + ri*3;

										ri = (cube[offset+0]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn;
										gi = (cube[offset+1]*gmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+4]*gmix)>>cube_shift_dn;
										bi = (cube[offset+2]*bmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+5]*bmix)>>cube_shift_dn;
										
										rgbout[0] = ri;
										rgbout[1] = gi;
										rgbout[2] = bi;
										rgbout+=3;
									}
								}
							}
							else // FULL 3D LUT
							{
								if(cube_base == 5)
								{
									for(xx=0;xx<8; xx++)
									{
										int ri,gi,bi;
										int rmix,gmix,bmix;
										int rmixd,gmixd,bmixd;
										int offset;

										ri = rgbsegment[xx+0]; // signed 13-bit
										gi = rgbsegment[xx+8];
										bi = rgbsegment[xx+16];
									
										ri <<= 3; // signed 16-bit
										gi <<= 3;
										bi <<= 3;

										if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
										if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
										if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;

										rmix = (ri & 0x7ff);
										gmix = (gi & 0x7ff);
										bmix = (bi & 0x7ff);

										ri>>=11;
										gi>>=11;
										bi>>=11;

										rmixd = 2048 - rmix;
										gmixd = 2048 - gmix;
										bmixd = 2048 - bmix;
												
										offset = bi*33*33*3 + gi*33*3 + ri*3;	
										
										ri = ((((((cube[offset+0]*rmixd + cube[offset+3]*rmix)>>11)*gmixd +
											((cube[offset+33*3]*rmixd + cube[offset+33*3+3]*rmix)>>11)*gmix)>>11)*bmixd) +
											(((((cube[offset+33*33*3]*rmixd + cube[offset+33*33*3+3]*rmix)>>11)*gmixd +
											((cube[offset+33*33*3+33*3]*rmixd + cube[offset+33*33*3+33*3+3]*rmix)>>11)*gmix)>>11)*bmix))>>11;

										gi = ((((((cube[offset+1]*rmixd + cube[offset+4]*rmix)>>11)*gmixd +
											((cube[offset+33*3+1]*rmixd + cube[offset+33*3+4]*rmix)>>11)*gmix)>>11)*bmixd) +
											(((((cube[offset+33*33*3+1]*rmixd + cube[offset+33*33*3+4]*rmix)>>11)*gmixd +
											((cube[offset+33*33*3+33*3+1]*rmixd + cube[offset+33*33*3+33*3+4]*rmix)>>11)*gmix)>>11)*bmix))>>11;

										bi = ((((((cube[offset+2]*rmixd + cube[offset+5]*rmix)>>11)*gmixd +
											((cube[offset+33*3+2]*rmixd + cube[offset+33*3+5]*rmix)>>11)*gmix)>>11)*bmixd) +
											(((((cube[offset+33*33*3+2]*rmixd + cube[offset+33*33*3+5]*rmix)>>11)*gmixd +
											((cube[offset+33*33*3+33*3+2]*rmixd + cube[offset+33*33*3+33*3+5]*rmix)>>11)*gmix)>>11)*bmix))>>11;
										
										rgbout[0] = ri;
										rgbout[1] = gi;
										rgbout[2] = bi;
										rgbout+=3;
									}
								}
								else if(cube_base == 6)
								{
									for(xx=0;xx<8; xx++)
									{
										int ri,gi,bi;
										int rmix,gmix,bmix;
										int rmixd,gmixd,bmixd;
										int offset;

										ri = rgbsegment[xx+0]; // signed 13-bit
										gi = rgbsegment[xx+8];
										bi = rgbsegment[xx+16];
									
										ri <<= 3; // signed 16-bit
										gi <<= 3;
										bi <<= 3;

										if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
										if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
										if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;

										rmix = (ri & 0x3ff);
										gmix = (gi & 0x3ff);
										bmix = (bi & 0x3ff);

										ri>>=10;
										gi>>=10;
										bi>>=10;

										rmixd = 1024 - rmix;
										gmixd = 1024 - gmix;
										bmixd = 1024 - bmix;
												
										offset = bi*65*65*3 + gi*65*3 + ri*3;
										ri = ((((((cube[offset+0]*rmixd + cube[offset+3]*rmix)>>10)*gmixd +
											((cube[offset+65*3]*rmixd + cube[offset+65*3+3]*rmix)>>10)*gmix)>>10)*bmixd) +
											(((((cube[offset+65*65*3]*rmixd + cube[offset+65*65*3+3]*rmix)>>10)*gmixd +
											((cube[offset+65*65*3+65*3]*rmixd + cube[offset+65*65*3+65*3+3]*rmix)>>10)*gmix)>>10)*bmix))>>10;

										gi = ((((((cube[offset+1]*rmixd + cube[offset+4]*rmix)>>10)*gmixd +
											((cube[offset+65*3+1]*rmixd + cube[offset+65*3+4]*rmix)>>10)*gmix)>>10)*bmixd) +
											(((((cube[offset+65*65*3+1]*rmixd + cube[offset+65*65*3+4]*rmix)>>10)*gmixd +
											((cube[offset+65*65*3+65*3+1]*rmixd + cube[offset+65*65*3+65*3+4]*rmix)>>10)*gmix)>>10)*bmix))>>10;

										bi = ((((((cube[offset+2]*rmixd + cube[offset+5]*rmix)>>10)*gmixd +
											((cube[offset+65*3+2]*rmixd + cube[offset+65*3+5]*rmix)>>10)*gmix)>>10)*bmixd) +
											(((((cube[offset+65*65*3+2]*rmixd + cube[offset+65*65*3+5]*rmix)>>10)*gmixd +
											((cube[offset+65*65*3+65*3+2]*rmixd + cube[offset+65*65*3+65*3+5]*rmix)>>10)*gmix)>>10)*bmix))>>10;
										
										rgbout[0] = ri;
										rgbout[1] = gi;
										rgbout[2] = bi;
										rgbout+=3;
									}
								}
								else
								{
									for(xx=0;xx<8; xx++)
									{
										int ri,gi,bi;
										int rmix,gmix,bmix;
										int rmixd,gmixd,bmixd;
										int offset;

										ri = rgbsegment[xx+0]; // signed 13-bit
										gi = rgbsegment[xx+8];
										bi = rgbsegment[xx+16];
									
										ri <<= 3; // signed 16-bit
										gi <<= 3;
										bi <<= 3;

										if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
										if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
										if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;

										rmix = (ri & cube_depth_mask);
										gmix = (gi & cube_depth_mask);
										bmix = (bi & cube_depth_mask);

										ri>>=cube_shift_dn;
										gi>>=cube_shift_dn;
										bi>>=cube_shift_dn;

										rmixd = cube_depth_mask+1 - rmix;
										gmixd = cube_depth_mask+1 - gmix;
										bmixd = cube_depth_mask+1 - bmix;

										offset = bi*cube_depth*cube_depth*3 + gi*cube_depth*3 + ri*3;
										ri = ((((((cube[offset+0]*rmixd + cube[offset+3]*rmix)>>cube_shift_dn)*gmixd +
											((cube[offset+cube_depth*3]*rmixd + cube[offset+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
											(((((cube[offset+cube_depth*cube_depth*3]*rmixd + cube[offset+cube_depth*cube_depth*3+3]*rmix)>>cube_shift_dn)*gmixd +
											((cube[offset+cube_depth*cube_depth*3+cube_depth*3]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

										gi = ((((((cube[offset+1]*rmixd + cube[offset+4]*rmix)>>cube_shift_dn)*gmixd +
											((cube[offset+cube_depth*3+1]*rmixd + cube[offset+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
											(((((cube[offset+cube_depth*cube_depth*3+1]*rmixd + cube[offset+cube_depth*cube_depth*3+4]*rmix)>>cube_shift_dn)*gmixd +
											((cube[offset+cube_depth*cube_depth*3+cube_depth*3+1]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

										bi = ((((((cube[offset+2]*rmixd + cube[offset+5]*rmix)>>cube_shift_dn)*gmixd +
											((cube[offset+cube_depth*3+2]*rmixd + cube[offset+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
											(((((cube[offset+cube_depth*cube_depth*3+2]*rmixd + cube[offset+cube_depth*cube_depth*3+5]*rmix)>>cube_shift_dn)*gmixd +
											((cube[offset+cube_depth*cube_depth*3+cube_depth*3+2]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;
										
										rgbout[0] = ri;
										rgbout[1] = gi;
										rgbout[2] = bi;
										rgbout+=3;
									}
								}
							}
						}
						else
						{
							for(xx=0;xx<8; xx++)
							{
								rgbout[0] = rgbsegment[xx+0]; // signed 13-bit
								rgbout[1] = rgbsegment[xx+8];
								rgbout[2] = rgbsegment[xx+16];
								rgbout+=3;
							}
						}
					}
				}
			}
			else
			{
				if(*whitebitdepth == 0 || *whitebitdepth == 16) // used for RAW decodes
				{
					for(x=0; x<split; x++)
					{
						*rgbout++ = *rgb++ >> 3;
						*rgbout++ = *rgb++ >> 3;
						*rgbout++ = *rgb++ >> 3;
					}

					if(decoder->RawCubeThree1Ds)
					{	
						if(cube_base == 5)
						{	
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = rgb[0];
								gi = rgb[1];
								bi = rgb[2];
								rgb+=3;

								rmix = (ri & 0x7ff);
								gmix = (gi & 0x7ff);
								bmix = (bi & 0x7ff);

								ri>>=11;
								gi>>=11;
								bi>>=11;

								rmixd = 2048 - rmix;
								gmixd = 2048 - gmix;
								bmixd = 2048 - bmix;
										
								offset = bi*33*33*3 + gi*33*3 + ri*3;
								ri = (cube[offset+0]*rmixd + cube[offset+33*33*3+33*3+3]*rmix)>>11;
								gi = (cube[offset+1]*gmixd + cube[offset+33*33*3+33*3+4]*gmix)>>11;
								bi = (cube[offset+2]*bmixd + cube[offset+33*33*3+33*3+5]*bmix)>>11;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
						else if(cube_base == 6)
						{
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = rgb[0];
								gi = rgb[1];
								bi = rgb[2];
								rgb+=3;

								rmix = (ri & 0x3ff);
								gmix = (gi & 0x3ff);
								bmix = (bi & 0x3ff);

								ri>>=10;
								gi>>=10;
								bi>>=10;

								rmixd = 1024 - rmix;
								gmixd = 1024 - gmix;
								bmixd = 1024 - bmix;
										
								offset = bi*65*65*3 + gi*65*3 + ri*3;
								ri = (cube[offset+0]*rmixd + cube[offset+65*65*3+65*3+3]*rmix)>>10;
								gi = (cube[offset+1]*gmixd + cube[offset+65*65*3+65*3+4]*gmix)>>10;
								bi = (cube[offset+2]*bmixd + cube[offset+65*65*3+65*3+5]*bmix)>>10;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
						else
						{	
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = rgb[0];
								gi = rgb[1];
								bi = rgb[2];
								rgb+=3;

								rmix = (ri & cube_depth_mask);
								gmix = (gi & cube_depth_mask);
								bmix = (bi & cube_depth_mask);

								ri>>=cube_shift_dn;
								gi>>=cube_shift_dn;
								bi>>=cube_shift_dn;

								rmixd = cube_depth_mask+1 - rmix;
								gmixd = cube_depth_mask+1 - gmix;
								bmixd = cube_depth_mask+1 - bmix;

								offset = bi*cube_depth*cube_depth*3 + gi*cube_depth*3 + ri*3;

								ri = (cube[offset+0]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn;
								gi = (cube[offset+1]*gmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+4]*gmix)>>cube_shift_dn;
								bi = (cube[offset+2]*bmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+5]*bmix)>>cube_shift_dn;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
					}
					else
					{
						if(cube_base == 5)
						{	
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = rgb[0];
								gi = rgb[1];
								bi = rgb[2];
								rgb+=3;

								rmix = (ri & 0x7ff);
								gmix = (gi & 0x7ff);
								bmix = (bi & 0x7ff);

								ri>>=11;
								gi>>=11;
								bi>>=11;

								rmixd = 2048 - rmix;
								gmixd = 2048 - gmix;
								bmixd = 2048 - bmix;
										
								offset = bi*33*33*3 + gi*33*3 + ri*3;	
								
								ri = ((((((cube[offset+0]*rmixd + cube[offset+3]*rmix)>>11)*gmixd +
									((cube[offset+33*3]*rmixd + cube[offset+33*3+3]*rmix)>>11)*gmix)>>11)*bmixd) +
									(((((cube[offset+33*33*3]*rmixd + cube[offset+33*33*3+3]*rmix)>>11)*gmixd +
									((cube[offset+33*33*3+33*3]*rmixd + cube[offset+33*33*3+33*3+3]*rmix)>>11)*gmix)>>11)*bmix))>>11;

								gi = ((((((cube[offset+1]*rmixd + cube[offset+4]*rmix)>>11)*gmixd +
									((cube[offset+33*3+1]*rmixd + cube[offset+33*3+4]*rmix)>>11)*gmix)>>11)*bmixd) +
									(((((cube[offset+33*33*3+1]*rmixd + cube[offset+33*33*3+4]*rmix)>>11)*gmixd +
									((cube[offset+33*33*3+33*3+1]*rmixd + cube[offset+33*33*3+33*3+4]*rmix)>>11)*gmix)>>11)*bmix))>>11;

								bi = ((((((cube[offset+2]*rmixd + cube[offset+5]*rmix)>>11)*gmixd +
									((cube[offset+33*3+2]*rmixd + cube[offset+33*3+5]*rmix)>>11)*gmix)>>11)*bmixd) +
									(((((cube[offset+33*33*3+2]*rmixd + cube[offset+33*33*3+5]*rmix)>>11)*gmixd +
									((cube[offset+33*33*3+33*3+2]*rmixd + cube[offset+33*33*3+33*3+5]*rmix)>>11)*gmix)>>11)*bmix))>>11;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
						else if(cube_base == 6)
						{					
							for(;x<width; x++)
							{			
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = rgb[0];
								gi = rgb[1];
								bi = rgb[2];
								rgb+=3;
								
								rmix = (ri & 0x3ff);
								gmix = (gi & 0x3ff);
								bmix = (bi & 0x3ff);

								ri>>=10;
								gi>>=10;
								bi>>=10;

								rmixd = 1024 - rmix;
								gmixd = 1024 - gmix;
								bmixd = 1024 - bmix;
										
								offset = bi*65*65*3 + gi*65*3 + ri*3;
								ri = ((((((cube[offset+0]*rmixd + cube[offset+3]*rmix)>>10)*gmixd +
									((cube[offset+65*3]*rmixd + cube[offset+65*3+3]*rmix)>>10)*gmix)>>10)*bmixd) +
									(((((cube[offset+65*65*3]*rmixd + cube[offset+65*65*3+3]*rmix)>>10)*gmixd +
									((cube[offset+65*65*3+65*3]*rmixd + cube[offset+65*65*3+65*3+3]*rmix)>>10)*gmix)>>10)*bmix))>>10;

								gi = ((((((cube[offset+1]*rmixd + cube[offset+4]*rmix)>>10)*gmixd +
									((cube[offset+65*3+1]*rmixd + cube[offset+65*3+4]*rmix)>>10)*gmix)>>10)*bmixd) +
									(((((cube[offset+65*65*3+1]*rmixd + cube[offset+65*65*3+4]*rmix)>>10)*gmixd +
									((cube[offset+65*65*3+65*3+1]*rmixd + cube[offset+65*65*3+65*3+4]*rmix)>>10)*gmix)>>10)*bmix))>>10;

								bi = ((((((cube[offset+2]*rmixd + cube[offset+5]*rmix)>>10)*gmixd +
									((cube[offset+65*3+2]*rmixd + cube[offset+65*3+5]*rmix)>>10)*gmix)>>10)*bmixd) +
									(((((cube[offset+65*65*3+2]*rmixd + cube[offset+65*65*3+5]*rmix)>>10)*gmixd +
									((cube[offset+65*65*3+65*3+2]*rmixd + cube[offset+65*65*3+65*3+5]*rmix)>>10)*gmix)>>10)*bmix))>>10;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
						else
						{							
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = rgb[0];
								gi = rgb[1];
								bi = rgb[2];
								rgb+=3;
								
								rmix = (ri & cube_depth_mask);
								gmix = (gi & cube_depth_mask);
								bmix = (bi & cube_depth_mask);

								ri>>=cube_shift_dn;
								gi>>=cube_shift_dn;
								bi>>=cube_shift_dn;

								rmixd = cube_depth_mask+1 - rmix;
								gmixd = cube_depth_mask+1 - gmix;
								bmixd = cube_depth_mask+1 - bmix;

								offset = bi*cube_depth*cube_depth*3 + gi*cube_depth*3 + ri*3;
								ri = ((((((cube[offset+0]*rmixd + cube[offset+3]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*3]*rmixd + cube[offset+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
									(((((cube[offset+cube_depth*cube_depth*3]*rmixd + cube[offset+cube_depth*cube_depth*3+3]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*cube_depth*3+cube_depth*3]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

								gi = ((((((cube[offset+1]*rmixd + cube[offset+4]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*3+1]*rmixd + cube[offset+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
									(((((cube[offset+cube_depth*cube_depth*3+1]*rmixd + cube[offset+cube_depth*cube_depth*3+4]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*cube_depth*3+cube_depth*3+1]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

								bi = ((((((cube[offset+2]*rmixd + cube[offset+5]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*3+2]*rmixd + cube[offset+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
									(((((cube[offset+cube_depth*cube_depth*3+2]*rmixd + cube[offset+cube_depth*cube_depth*3+5]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*cube_depth*3+cube_depth*3+2]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;
										
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
					}
				}
				else
				{
					short *rgb13 = (short *)rgb; 
					for(x=0; x<split; x++)
					{
						rgbout[0] = rgb13[0];
						rgbout[1] = rgb13[1];
						rgbout[2] = rgb13[2];
						rgb13+=3;
						rgbout+=3;
					}

					if(decoder->RawCubeThree1Ds)
					{	
						if(cube_base == 5)
						{
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = rgb13[0];
								gi = rgb13[1];
								bi = rgb13[2];
								rgb13+=3;

								ri <<= 3;
								gi <<= 3;
								bi <<= 3;
								
								if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
								if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
								if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;

								rmix = (ri & 0x7ff);
								gmix = (gi & 0x7ff);
								bmix = (bi & 0x7ff);

								ri>>=11;
								gi>>=11;
								bi>>=11;

								rmixd = 2048 - rmix;
								gmixd = 2048 - gmix;
								bmixd = 2048 - bmix;
										
								offset = bi*33*33*3 + gi*33*3 + ri*3;
								ri = (cube[offset+0]*rmixd + cube[offset+33*33*3+33*3+3]*rmix)>>11;
								gi = (cube[offset+1]*gmixd + cube[offset+33*33*3+33*3+4]*gmix)>>11;
								bi = (cube[offset+2]*bmixd + cube[offset+33*33*3+33*3+5]*bmix)>>11;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
						else if(cube_base == 6)
						{
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = rgb13[0];
								gi = rgb13[1];
								bi = rgb13[2];
								rgb13+=3;

								ri <<= 3;
								gi <<= 3;
								bi <<= 3;
								
								if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
								if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
								if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;

								rmix = (ri & 0x3ff);
								gmix = (gi & 0x3ff);
								bmix = (bi & 0x3ff);

								ri>>=10;
								gi>>=10;
								bi>>=10;

								rmixd = 1024 - rmix;
								gmixd = 1024 - gmix;
								bmixd = 1024 - bmix;
										
								offset = bi*65*65*3 + gi*65*3 + ri*3;
								ri = (cube[offset+0]*rmixd + cube[offset+65*65*3+65*3+3]*rmix)>>10;
								gi = (cube[offset+1]*gmixd + cube[offset+65*65*3+65*3+4]*gmix)>>10;
								bi = (cube[offset+2]*bmixd + cube[offset+65*65*3+65*3+5]*bmix)>>10;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
						else
						{	
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = rgb13[0];
								gi = rgb13[1];
								bi = rgb13[2];
								rgb13+=3;
								
								ri <<= 3;
								gi <<= 3;
								bi <<= 3;
								
								if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
								if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
								if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;

								rmix = (ri & cube_depth_mask);
								gmix = (gi & cube_depth_mask);
								bmix = (bi & cube_depth_mask);

								ri>>=cube_shift_dn;
								gi>>=cube_shift_dn;
								bi>>=cube_shift_dn;

								rmixd = cube_depth_mask+1 - rmix;
								gmixd = cube_depth_mask+1 - gmix;
								bmixd = cube_depth_mask+1 - bmix;

								offset = bi*cube_depth*cube_depth*3 + gi*cube_depth*3 + ri*3;

								ri = (cube[offset+0]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn;
								gi = (cube[offset+1]*gmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+4]*gmix)>>cube_shift_dn;
								bi = (cube[offset+2]*bmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+5]*bmix)>>cube_shift_dn;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
					}
					else
					{
						if(cube_base == 5)
						{	
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = rgb13[0];
								gi = rgb13[1];
								bi = rgb13[2];
								rgb13+=3;

								ri <<= 3;
								gi <<= 3;
								bi <<= 3;
								
								if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
								if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
								if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;

								rmix = (ri & 0x7ff);
								gmix = (gi & 0x7ff);
								bmix = (bi & 0x7ff);

								ri>>=11;
								gi>>=11;
								bi>>=11;

								rmixd = 2048 - rmix;
								gmixd = 2048 - gmix;
								bmixd = 2048 - bmix;
										
								offset = bi*33*33*3 + gi*33*3 + ri*3;	
								
								ri = ((((((cube[offset+0]*rmixd + cube[offset+3]*rmix)>>11)*gmixd +
									((cube[offset+33*3]*rmixd + cube[offset+33*3+3]*rmix)>>11)*gmix)>>11)*bmixd) +
									(((((cube[offset+33*33*3]*rmixd + cube[offset+33*33*3+3]*rmix)>>11)*gmixd +
									((cube[offset+33*33*3+33*3]*rmixd + cube[offset+33*33*3+33*3+3]*rmix)>>11)*gmix)>>11)*bmix))>>11;

								gi = ((((((cube[offset+1]*rmixd + cube[offset+4]*rmix)>>11)*gmixd +
									((cube[offset+33*3+1]*rmixd + cube[offset+33*3+4]*rmix)>>11)*gmix)>>11)*bmixd) +
									(((((cube[offset+33*33*3+1]*rmixd + cube[offset+33*33*3+4]*rmix)>>11)*gmixd +
									((cube[offset+33*33*3+33*3+1]*rmixd + cube[offset+33*33*3+33*3+4]*rmix)>>11)*gmix)>>11)*bmix))>>11;

								bi = ((((((cube[offset+2]*rmixd + cube[offset+5]*rmix)>>11)*gmixd +
									((cube[offset+33*3+2]*rmixd + cube[offset+33*3+5]*rmix)>>11)*gmix)>>11)*bmixd) +
									(((((cube[offset+33*33*3+2]*rmixd + cube[offset+33*33*3+5]*rmix)>>11)*gmixd +
									((cube[offset+33*33*3+33*3+2]*rmixd + cube[offset+33*33*3+33*3+5]*rmix)>>11)*gmix)>>11)*bmix))>>11;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
						else if(cube_base == 6)
						{					
							for(;x<width; x++)
							{			
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = rgb13[0];
								gi = rgb13[1];
								bi = rgb13[2];
								rgb13+=3;

								ri <<= 3;
								gi <<= 3;
								bi <<= 3;
								
								if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
								if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
								if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;
								
								rmix = (ri & 0x3ff);
								gmix = (gi & 0x3ff);
								bmix = (bi & 0x3ff);

								ri>>=10;
								gi>>=10;
								bi>>=10;

								rmixd = 1024 - rmix;
								gmixd = 1024 - gmix;
								bmixd = 1024 - bmix;
										
								offset = bi*65*65*3 + gi*65*3 + ri*3;
								ri = ((((((cube[offset+0]*rmixd + cube[offset+3]*rmix)>>10)*gmixd +
									((cube[offset+65*3]*rmixd + cube[offset+65*3+3]*rmix)>>10)*gmix)>>10)*bmixd) +
									(((((cube[offset+65*65*3]*rmixd + cube[offset+65*65*3+3]*rmix)>>10)*gmixd +
									((cube[offset+65*65*3+65*3]*rmixd + cube[offset+65*65*3+65*3+3]*rmix)>>10)*gmix)>>10)*bmix))>>10;

								gi = ((((((cube[offset+1]*rmixd + cube[offset+4]*rmix)>>10)*gmixd +
									((cube[offset+65*3+1]*rmixd + cube[offset+65*3+4]*rmix)>>10)*gmix)>>10)*bmixd) +
									(((((cube[offset+65*65*3+1]*rmixd + cube[offset+65*65*3+4]*rmix)>>10)*gmixd +
									((cube[offset+65*65*3+65*3+1]*rmixd + cube[offset+65*65*3+65*3+4]*rmix)>>10)*gmix)>>10)*bmix))>>10;

								bi = ((((((cube[offset+2]*rmixd + cube[offset+5]*rmix)>>10)*gmixd +
									((cube[offset+65*3+2]*rmixd + cube[offset+65*3+5]*rmix)>>10)*gmix)>>10)*bmixd) +
									(((((cube[offset+65*65*3+2]*rmixd + cube[offset+65*65*3+5]*rmix)>>10)*gmixd +
									((cube[offset+65*65*3+65*3+2]*rmixd + cube[offset+65*65*3+65*3+5]*rmix)>>10)*gmix)>>10)*bmix))>>10;
								
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
						else
						{							
							for(;x<width; x++)
							{
								int ri,gi,bi;
								int rmix,gmix,bmix;
								int rmixd,gmixd,bmixd;
								int offset;

								ri = rgb13[0];
								gi = rgb13[1];
								bi = rgb13[2];
								rgb13+=3;
								
								ri <<= 3;
								gi <<= 3;
								bi <<= 3;
								
								if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
								if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
								if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;
								
								rmix = (ri & cube_depth_mask);
								gmix = (gi & cube_depth_mask);
								bmix = (bi & cube_depth_mask);

								ri>>=cube_shift_dn;
								gi>>=cube_shift_dn;
								bi>>=cube_shift_dn;

								rmixd = cube_depth_mask+1 - rmix;
								gmixd = cube_depth_mask+1 - gmix;
								bmixd = cube_depth_mask+1 - bmix;

								offset = bi*cube_depth*cube_depth*3 + gi*cube_depth*3 + ri*3;
								ri = ((((((cube[offset+0]*rmixd + cube[offset+3]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*3]*rmixd + cube[offset+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
									(((((cube[offset+cube_depth*cube_depth*3]*rmixd + cube[offset+cube_depth*cube_depth*3+3]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*cube_depth*3+cube_depth*3]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

								gi = ((((((cube[offset+1]*rmixd + cube[offset+4]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*3+1]*rmixd + cube[offset+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
									(((((cube[offset+cube_depth*cube_depth*3+1]*rmixd + cube[offset+cube_depth*cube_depth*3+4]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*cube_depth*3+cube_depth*3+1]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

								bi = ((((((cube[offset+2]*rmixd + cube[offset+5]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*3+2]*rmixd + cube[offset+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
									(((((cube[offset+cube_depth*cube_depth*3+2]*rmixd + cube[offset+cube_depth*cube_depth*3+5]*rmix)>>cube_shift_dn)*gmixd +
									((cube[offset+cube_depth*cube_depth*3+cube_depth*3+2]*rmixd + cube[offset+cube_depth*cube_depth*3+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;
										
								rgbout[0] = ri;
								rgbout[1] = gi;
								rgbout[2] = bi;
								rgbout+=3;
							}
						}
					}
				}
			}
		}


		//if(convert2YUV)
		//{
		//	*flags = ACTIVEMETADATA_PRESATURATED | ACTIVEMETADATA_PLANAR | ACTIVEMETADATA_COLORFORMATDONE;
		//}
		//else
		{
			*flags = 0;
		}

		*whitebitdepth = 13;
		return dst;
	}
	else //1D simplied
	{
		int still16bit = 0;
		int lines;
		//int max = 65535;
		//float oneunit = 8192.0;
		int channels = 3;
		int sat = (int)((decoder->cdl_sat + 1.0) * 8192.0);	
	
		for(lines=0; lines<height; lines++)
		{
			int x,xx,y = lines;
			unsigned short *rgb = (unsigned short *)src;
			short *rgbout = (short *)dst;

			rgb += width*y*channels;
			rgbout += width*lines*channels;

			if(*flags & ACTIVEMETADATA_PLANAR)
			{
				unsigned short *rptr,*gptr,*bptr;
				int *lcm = &decoder->linear_color_matrix[0];
				//int *lcmns = &decoder->linear_color_matrix_highlight_sat[0];
				int *ccm = &decoder->curved_color_matrix[0];
			//	int highlight_start = (int)((decoder->cfhddata.channel[0].user_highlight_point + 1.0) * 81920.0);
			//	int highlight_end = highlight_start * 3 / 2;
			//	int mixgain256 = (highlight_end - highlight_start) / 256;
			//	if(mixgain256 == 0) mixgain256 = 1;
			//	if(highlight_start > 81900) highlight_start = 8190000; // switch off efficently

				rptr = rgb;
				gptr = &rgb[width];
				bptr = &rgb[width*2];

				if(decoder->RGBFilterBufferPhase == 1) // decoder order
				{
					gptr = rgb;
					rptr = &rgb[width];
				}


				if(*whitebitdepth == 13)
				{
					for(x=0;x<width; x++)
					{
						int ri,gi,bi;
						int rn,gn,bn;
						ri = *(short *)rptr++; // 13-bit
						gi = *(short *)gptr++;
						bi = *(short *)bptr++;

						// Curve2Linear range -2 to +4, or -16384 to 32768
						if(ri < -16384) ri = -16384;
						if(gi < -16384) gi = -16384;
						if(bi < -16384) bi = -16384;

						if(x>=split)
						{
							CURVES_PROCESSING_MACRO;
						}

						*rgbout++ = ri;
						*rgbout++ = gi;
						*rgbout++ = bi;
					}
				}
				else
				{
					for(x=0;x<width; x++)
					{
						int ri,gi,bi;
						int rn,gn,bn;
						ri = *rptr++>>3; // 13-bit unsigned
						gi = *gptr++>>3;
						bi = *bptr++>>3;

						if(x>=split)
						{
							CURVES_PROCESSING_MACRO;
						}

						*rgbout++ = ri;
						*rgbout++ = gi;
						*rgbout++ = bi;
					}
				}
				
			}
			else if(*flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
			{
				int *lcm = &decoder->linear_color_matrix[0];
				//int *lcmns = &decoder->linear_color_matrix_highlight_sat[0];
				int *ccm = &decoder->curved_color_matrix[0];
			//	int highlight_start = (int)((decoder->cfhddata.channel[0].user_highlight_point + 1.0) * 81920.0);
			//	int highlight_end = highlight_start * 3 / 2;
			//	int mixgain256 = (highlight_end - highlight_start) / 256;
			//	if(mixgain256 == 0) mixgain256 = 1;
			//	if(highlight_start > 81900) highlight_start = 8190000; // switch off efficently

				for(x=0;x<width; x+=8)
				{
					unsigned short *rgbsegment = rgb;
					short *signrgbsegment = (short *)rgb;
				
					rgb+= 8*3;

					for(xx=0;xx<8; xx++)
					{
						int ri,gi,bi;
						int rn,gn,bn;

						if(*whitebitdepth == 13)
						{
							ri = signrgbsegment[0]; // 13-bit
							gi = signrgbsegment[8];
							bi = signrgbsegment[16];
							signrgbsegment++;

							// Curve2Linear range -2 to +4, or -16384 to 32768
							if(ri < -16384) ri = -16384;
							if(gi < -16384) gi = -16384;
							if(bi < -16384) bi = -16384;
						}
						else
						{
							ri = rgbsegment[0]>>3; // 13-bit
							gi = rgbsegment[8]>>3;
							bi = rgbsegment[16]>>3;
							rgbsegment++;
						}
						
						
						if(x>=split)
						{
							CURVES_PROCESSING_MACRO;			
						}		

						*rgbout++ = ri;
						*rgbout++ = gi;
						*rgbout++ = bi;
					}
				}
			}
			else
			{

				if(*whitebitdepth == 16 && decoder->Curve2Linear == NULL)
				{
					memcpy(rgbout, rgb, width*3*2);
					rgb += width*3;
					rgbout += width*3;

					still16bit = 1;
				}
				else
				{
					int *lcm = &decoder->linear_color_matrix[0];
					//int *lcmns = &decoder->linear_color_matrix_highlight_sat[0];
					int *ccm = &decoder->curved_color_matrix[0];
				//	int highlight_start = (int)((decoder->cfhddata.channel[0].user_highlight_point + 1.0) * 81920.0);
				//	int highlight_end = highlight_start * 3 / 2;
				//	int mixgain256 = (highlight_end - highlight_start) / 256;
				//	if(mixgain256 == 0) mixgain256 = 1;
				//	if(highlight_start > 81900) highlight_start = 8190000; // switch off efficently

					for(x=0;x<width; x++)
					{
						int ri,gi,bi;
						int rn,gn,bn;

						if(*whitebitdepth == 13)
						{
							short *sptr = (short *)rgb;
							ri = sptr[0]; // 13-bit
							gi = sptr[1];
							bi = sptr[2];
							sptr+=3;
							rgb+=3;
						}
						else
						{
							ri = *rgb++>>3; // 13-bit
							gi = *rgb++>>3;
							bi = *rgb++>>3;
						}

						if(x>=split)
						{
							CURVES_PROCESSING_MACRO;
						}

						*rgbout++ = ri;
						*rgbout++ = gi;
						*rgbout++ = bi;
					}

				}
			}
		}

		if(still16bit)
		{
			*whitebitdepth = 16;
			*flags = ACTIVEMETADATA_PRESATURATED;
		}
		else
		{
			*whitebitdepth = 13;
			*flags = 0;
		}

		return dst;
	}
}




void FastBlendWP13(short *Aptr, short *Bptr, short *output, int bytes)
{
	int i=0;

	for(i=0; i<bytes; i+=16)
	{
		__m128i A_epi16 = _mm_load_si128((__m128i *)Aptr);
		__m128i B_epi16 = _mm_load_si128((__m128i *)Bptr);
		__m128i mix_epi16;

		Aptr+=8;
		Bptr+=8;

		A_epi16 = _mm_srai_epi16(A_epi16,1);
		B_epi16 = _mm_srai_epi16(B_epi16,1);

		mix_epi16 = _mm_adds_epi16(A_epi16, B_epi16);

		_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
	}

	for(;i<bytes;i+=2)
	{
		*output++ = (*Aptr++ + *Bptr++)>>1; //R
	}
}

void FastBlurV(unsigned short *Aptr, unsigned short *Bptr, unsigned short *Cptr,
			   unsigned short *output, int pixels)
{
	int i=0;

	for(i=0; i<pixels*3; i+=8)
	{
		__m128i A_epi16 = _mm_load_si128((__m128i *)Aptr);
		__m128i B_epi16 = _mm_load_si128((__m128i *)Bptr);
		__m128i C_epi16 = _mm_load_si128((__m128i *)Cptr);
		__m128i mix_epi16;

		Aptr+=8;
		Bptr+=8;
		Cptr+=8;

		A_epi16 = _mm_srli_epi16(A_epi16,2);
		B_epi16 = _mm_srli_epi16(B_epi16,2);
		C_epi16 = _mm_srli_epi16(C_epi16,2);

		mix_epi16 = _mm_adds_epu16(B_epi16, B_epi16);
		mix_epi16 = _mm_adds_epu16(mix_epi16, A_epi16);
		mix_epi16 = _mm_adds_epu16(mix_epi16, C_epi16);

		_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
	}

	for(;i<pixels;i++)
	{
		*output++ = (*Aptr++ + (*Bptr++)*2 + *Cptr++)>>2; //R
		*output++ = (*Aptr++ + (*Bptr++)*2 + *Cptr++)>>2; //G
		*output++ = (*Aptr++ + (*Bptr++)*2 + *Cptr++)>>2; //B
//		*output++ = ((*Bptr++)); //R
//		*output++ = ((*Bptr++)); //G
//		*output++ = ((*Bptr++)); //B
	}
}


void FastSharpeningBlurV(unsigned short *Aptr,
						 unsigned short *Bptr,
						 unsigned short *Cptr,
						 unsigned short *Dptr,
						 unsigned short *Eptr,
						 unsigned short *output, int pixels, int sharpness)
{
	int i=0,shift=2,B,C,preshift=0,shiftsse2,prescale=3;
	__m128i Bset;
	__m128i Cset;


	switch(sharpness)
	{
	default:
	case 3: //highest sharpen
		shift = 2; 	B = 1; 	C = 4;
		break;
	case 2: //nice sharpen
		shift = 3;	B = 2;	C = 6; prescale = 4;
		break;
	case 1: //small sharpen -1, 4, 10, 4, -1 = -0.5, 2, 5, 2, -0.5
		shift = 4;	B = 4;	C = 10; preshift = 1; prescale = 4;
		break;
	case 5: //highest sharpen
		shift = 1; 	B = 0;  C = 4;
		break;
	case 4: //highest sharpen
		shift = 1; 	B = 1;  C = 2;
		break;
	}

	Bset = _mm_set1_epi16(B);
	Cset = _mm_set1_epi16(C);

	shiftsse2 = shift - prescale;
	if(preshift)
	{
		Bset = _mm_srai_epi16(Bset, preshift);
		Cset = _mm_srai_epi16(Cset, preshift);
		shiftsse2 -= preshift;
	}

/*	for(i=0;i<pixels;i++)
	{
		*output++ = SATURATE((-(*Aptr++) + (*Bptr++)*B + (*Cptr++)*C + (*Dptr++)*B -(*Eptr++))>>shift); //R
		*output++ = SATURATE((-(*Aptr++) + (*Bptr++)*B + (*Cptr++)*C + (*Dptr++)*B -(*Eptr++))>>shift); //G
		*output++ = SATURATE((-(*Aptr++) + (*Bptr++)*B + (*Cptr++)*C + (*Dptr++)*B -(*Eptr++))>>shift); //B
	}
*/
	for(i=0; i<pixels*3; i+=8)
	{
		__m128i mix_epi16;
		__m128i tmp_epi16;
		__m128i A_epi16 = _mm_load_si128((__m128i *)Aptr);
		__m128i B_epi16 = _mm_load_si128((__m128i *)Bptr);
		__m128i C_epi16 = _mm_load_si128((__m128i *)Cptr);
		__m128i D_epi16 = _mm_load_si128((__m128i *)Dptr);
		__m128i E_epi16 = _mm_load_si128((__m128i *)Eptr);

		Aptr+=8;
		Bptr+=8;
		Cptr+=8;
		Dptr+=8;
		Eptr+=8;

		A_epi16 = _mm_srli_epi16(A_epi16,prescale);
		B_epi16 = _mm_srli_epi16(B_epi16,prescale);
		C_epi16 = _mm_srli_epi16(C_epi16,prescale);
		D_epi16 = _mm_srli_epi16(D_epi16,prescale);
		E_epi16 = _mm_srli_epi16(E_epi16,prescale);



		if(preshift)
		{
			A_epi16 = _mm_srai_epi16(A_epi16, preshift);
			E_epi16 = _mm_srai_epi16(E_epi16, preshift);
		}

		mix_epi16 = _mm_mullo_epi16(C_epi16, Cset);
		mix_epi16 = _mm_subs_epu16(mix_epi16, A_epi16);
		mix_epi16 = _mm_subs_epu16(mix_epi16, E_epi16);
		tmp_epi16 = _mm_mullo_epi16(B_epi16, Bset);
		mix_epi16 = _mm_adds_epu16(mix_epi16, tmp_epi16);
		tmp_epi16 = _mm_mullo_epi16(D_epi16, Bset);
		mix_epi16 = _mm_adds_epu16(mix_epi16, tmp_epi16);

		mix_epi16 = _mm_adds_epu16(mix_epi16, _mm_set1_epi16(0x8000));
		mix_epi16 = _mm_subs_epu16(mix_epi16, _mm_set1_epi16(0x8000));
		mix_epi16 = _mm_slli_epi16(mix_epi16, -shiftsse2);


		_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
	}
}



#if _THREADED

void DemosaicRAW(DECODER *decoder, FRAME_INFO *info, int thread_index, uint8_t *output, int pitch, uint8_t *scratch, int scratchsize)
{
	//int bayer_format = decoder->cfhddata.bayer_format;
	THREAD_ERROR error;
	int highquality = 0;
	int deripple = 0;
	int sharpening = -1;
	uint8_t *scratchptr = scratch;
	//int scratchremain = scratchsize;
	int debayerfilter = (decoder->cfhddata.process_path_flags_mask >> 16) & 0xf; // 8-bit debayer selector

/*	if(info->format == COLOR_FORMAT_YUYV)
	{
		debayerfilter = 1; // force YUV playback to bilinear
	}
*/
	if(	info->format == COLOR_FORMAT_B64A ||
		info->format == COLOR_FORMAT_W13A ||
		info->format == COLOR_FORMAT_WP13 ||
		info->format == COLOR_FORMAT_RG48 ||
		info->format == COLOR_FORMAT_RG64 ||
		info->format == COLOR_FORMAT_AR10 ||
		info->format == COLOR_FORMAT_AB10 ||
		info->format == COLOR_FORMAT_RG30 ||
		info->format == COLOR_FORMAT_R210 ||
		info->format == COLOR_FORMAT_DPX0 ||
		info->format == COLOR_FORMAT_YR16 ||
		info->format == COLOR_FORMAT_YU64 ||
		info->format == COLOR_FORMAT_V210 ||
		info->format == COLOR_FORMAT_R4FL)
	{
		debayerfilter = (decoder->cfhddata.process_path_flags_mask >> 20) & 0xf; // 16-bit debayer selector
		highquality = 1;
	}

	if(decoder->cfhddata.demosaic_type)
		debayerfilter = decoder->cfhddata.demosaic_type;


	switch(debayerfilter)
	{
	case 0: // unset
	case 15: // assume unset
	default:
		if(highquality) // 5x5 Enh (because 16-bit decode)
		{
			sharpening = 1; // Detail 1
		}
		else  // bilinear
		{
			sharpening = -1; // no smoothing
		}
		break;
	case 1: // blinear
		sharpening = -1;
		highquality = 0;
		deripple = 0;
		break;
	case 2: // 5x5 Enh
		sharpening = -1;
		highquality = 1;
		deripple = 1;
		break;
	case 3: // Advanced Smooth
		sharpening = 0;
		highquality = 1;
		deripple = 1;
		break;
	case 4: // Advanced Detail 1
		sharpening = 1;
		highquality = 1;
		deripple = 1;
		break;
	case 5: // Advanced Detail 2
		sharpening = 2;
		highquality = 1;
		deripple = 1;
		break;
	case 6: // Advanced Detail 3
		sharpening = 3;
		highquality = 1;
		deripple = 1;
		break;
	case 7: // bilinear Smoothed (needs sharpening + 1)
		sharpening = 1;
		highquality = 0;
		deripple = 1;
		break;
	case 8: // bilinear Detail 1
		sharpening = 2;
		highquality = 0;
		deripple = 1;
		break;
	case 9: // bilinear Detail 2
		sharpening = 3;
		highquality = 0;
		deripple = 1;
		break;
	}

	if(decoder->sample_uncompressed)
		deripple = 0;

	for (;;)
	{
		int work_index;
		int work_index1;
		int work_index2;
		int work_index3;
		//int work_index4;
		//int row;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			int bayer_format = decoder->cfhddata.bayer_format;
			unsigned char *outA8, *outB8;
			//unsigned short *lineStartA16, *lineStartB16;
			//unsigned short *lineA16, *lineB16;
			//uint8_t *line = output;
			PIXEL *bayer_line = (PIXEL *)decoder->RawBayer16;
			//PIXEL *bayerptr;
			int bayer_pitch = info->width*4;
			//int x;
			int y;
			int job = 0;


			// job level 0
			y = work_index;
			bayer_line = (PIXEL *)decoder->RawBayer16;
			bayer_line += bayer_pitch * y;

			ColorDifference2Bayer(info->width, (unsigned short *)bayer_line, bayer_pitch, bayer_format);

			// job level 1
			if(deripple)
			{
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index1, thread_index, job, 3))
				{
					y = work_index1;
					if(y>=3 && y<info->height-3) //middle scanline
					{
						unsigned short *delayptr = decoder->RawBayer16;
						delayptr += bayer_pitch * y;

						BayerRippleFilter(info->width,
							delayptr, bayer_pitch, bayer_format, decoder->RawBayer16);
					}
				}
			}


			if(sharpening < 0 || decoder->frame.generate_look)
			{
				// job level 2
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index2, thread_index, job, 3))
				{
					unsigned short *sptr;
					unsigned short *scanline;
					unsigned short *scanline2;
					uint8_t *line = output;
					int flags = 0;
					int whitebitdepth = 16;

					assert(scratchsize > (info->width*2)*3*2 * 2);

					scanline = (unsigned short *)scratchptr;
					scanline2 = scanline;
					scanline2 += (info->width*2)*3*2;


					y = work_index2;

					line += y * pitch * 2;
					outA8 = line;
					line += pitch;
					outB8 = line;
					line += pitch;

					sptr = scanline;


					if(decoder->frame.generate_look)
					{
						DrawBlankLUT(sptr,info->width*2,y*2,2);
					}
					else
					{
						DebayerLine(info->width*2, info->height*2, y*2,
							decoder->RawBayer16,  bayer_format, sptr, highquality, sharpening);
					}

					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width*2, 2, y*2,
							(uint32_t *)scanline, (uint32_t *)scanline2,
							info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width*2, 2, y, sptr, outA8, pitch,
							info->format, whitebitdepth, flags);
				}
			}
			else
			{
				// job level 2
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index2, thread_index, job, 3))
				{
					//unsigned short *sptr;
					unsigned short *RGBbuffer = decoder->RGBFilterBuffer16;

					y = work_index2;

					RGBbuffer += y*2 * info->width*2*3;

					DebayerLine(info->width*2, info->height*2, y*2,
						decoder->RawBayer16,  bayer_format, RGBbuffer, highquality, sharpening);
				}

				// job level 3
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index3, thread_index, job, 3))
				{
					int y = work_index3;

					unsigned short *Aptr;
					unsigned short *Bptr;
					unsigned short *Cptr;
					unsigned short *Dptr;
					unsigned short *Eptr;
					unsigned short *scanline;
					unsigned short *scanline2;
					unsigned short *sptr;
					uint8_t *line = output;
					int flags = 0;
					int whitebitdepth = 16;
					int rgbpitch16 = (info->width*2)*3;

					unsigned short *RGBbuffer = decoder->RGBFilterBuffer16;

					line += y * pitch * 2;
					outA8 = line;
					line += pitch;
					outB8 = line;
					line += pitch;

					RGBbuffer += y*2 * rgbpitch16;

					assert(scratchsize > rgbpitch16*2*2);
					scanline = (unsigned short *)scratchptr;
					scanline2 = scanline;
					scanline2 += rgbpitch16;

					Cptr = RGBbuffer;
					Bptr = Cptr;
					if(y>=1)
						Bptr -= rgbpitch16;
					Aptr = Bptr;
					if(y>=1)
						Aptr -= rgbpitch16;
					Dptr = Cptr;
					if(y < info->height-1)
						Dptr += rgbpitch16;
					Eptr = Dptr;
					if(y < info->height-1)
						Eptr += rgbpitch16;


					if(sharpening == 0)
					{
						FastBlurV(Bptr, Cptr, Dptr, scanline, info->width*2); // new C
					}
					else
					{
						FastSharpeningBlurV(Aptr, Bptr, Cptr, Dptr, Eptr,
								scanline, info->width*2, sharpening);
					}

					sptr = scanline;
					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width*2, 1, y*2,
							(uint32_t *)scanline, (uint32_t *)scanline2, info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width*2, 1, y, sptr, outA8, pitch,
							info->format, whitebitdepth, flags);

					Aptr += rgbpitch16;
					Bptr += rgbpitch16;
					Cptr += rgbpitch16;
					Dptr += rgbpitch16;
					Eptr += rgbpitch16;


					if(sharpening == 0)
					{
						FastBlurV(Bptr, Cptr, Dptr, scanline, info->width*2); // new C
					}
					else
					{
						FastSharpeningBlurV(Aptr, Bptr, Cptr, Dptr, Eptr,
								scanline, info->width*2, sharpening);
					}

					flags = 0;
					whitebitdepth = 16;
					sptr = scanline;
					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width*2, 1, y*2+1,
							(uint32_t *)scanline, (uint32_t *)scanline2, info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width*2, 1, y, sptr, outB8, pitch,
							info->format, whitebitdepth, flags);
				}
			}
		}
		else
		{
			int bayer_format = decoder->cfhddata.bayer_format;
			unsigned char *outA8, *outB8;
			//unsigned short *lineStartA16, *lineStartB16;
			//unsigned short *lineA16, *lineB16;
			//uint8_t *line = output;
			//PIXEL *bayer_line = (PIXEL *)decoder->RawBayer16;
			//PIXEL *bayerptr;
			int bayer_pitch = info->width*4;
			//int x;
			int y;
			int job = 0;

			// job level 1
			if(deripple)
			{
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index1, thread_index, job, 3))
				{
					y = work_index1;
					if(y>=3 && y<info->height-3) //middle scanlines
					{
						unsigned short *delayptr = decoder->RawBayer16;
						delayptr += bayer_pitch * y;

						BayerRippleFilter(info->width,
							delayptr, bayer_pitch, bayer_format, decoder->RawBayer16);
					}
				}
			}


			// job level 2
			if(sharpening < 0 || decoder->frame.generate_look)
			{
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index2, thread_index, job, 3))
				{
					int y = work_index2;
					unsigned short *sptr;
					unsigned short *scanline;
					unsigned short *scanline2;
					uint8_t *line = output;
					int flags = 0;
					int whitebitdepth = 16;

					assert(scratchsize > (info->width*2)*3*2 * 2);

					scanline = (unsigned short *)scratchptr;
					scanline2 = scanline;
					scanline2 +=  (info->width*2)*3*2;

					line += y * pitch * 2;
					outA8 = line;
					line += pitch;
					outB8 = line;
					line += pitch;

					sptr = scanline;

					if(decoder->frame.generate_look)
					{
						DrawBlankLUT(sptr,info->width*2,y*2,2);
					}
					else
					{
						DebayerLine(info->width*2, info->height*2, y*2,
							decoder->RawBayer16,  bayer_format, sptr, highquality, sharpening);
					}

					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width*2, 2, y*2,
							(uint32_t *)scanline, (uint32_t *)scanline2, info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width*2, 2, y, sptr, outA8, pitch,
							info->format, whitebitdepth, flags);
				}
			}

			else
			{
				// job level 2
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index2, thread_index, job, 3))
				{
					//unsigned short *sptr;
					unsigned short *RGBbuffer = decoder->RGBFilterBuffer16;

					y = work_index2;

					RGBbuffer += y*2 * info->width*2*3;

					DebayerLine(info->width*2, info->height*2, y*2,
						decoder->RawBayer16,  bayer_format, RGBbuffer, highquality, sharpening);
				}

				// job level 3
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index3, thread_index, job, 3))
				{

					int y = work_index3;

					unsigned short *Aptr;
					unsigned short *Bptr;
					unsigned short *Cptr;
					unsigned short *Dptr;
					unsigned short *Eptr;
					unsigned short *scanline;
					unsigned short *scanline2;
					unsigned short *sptr;
					uint8_t *line = output;
					int flags = 0;
					int whitebitdepth = 16;
					int rgbpitch16 = (info->width*2)*3;

					unsigned short *RGBbuffer = decoder->RGBFilterBuffer16;

					line += y * pitch * 2;
					outA8 = line;
					line += pitch;
					outB8 = line;
					line += pitch;

					RGBbuffer += y*2 * rgbpitch16;

					assert(scratchsize > rgbpitch16*2*2);
					scanline = (unsigned short *)scratchptr;
					scanline2 = scanline;
					scanline2 += rgbpitch16;

					Cptr = RGBbuffer;
					Bptr = Cptr;
					if(y>=1)
						Bptr -= rgbpitch16;
					Aptr = Bptr;
					if(y>=1)
						Aptr -= rgbpitch16;
					Dptr = Cptr;
					if(y < info->height-1)
						Dptr += rgbpitch16;
					Eptr = Dptr;
					if(y < info->height-1)
						Eptr += rgbpitch16;


					if(sharpening == 0)
					{
						FastBlurV(Bptr, Cptr, Dptr, scanline, info->width*2); // new C

					}
					else
					{
						FastSharpeningBlurV(Aptr, Bptr, Cptr, Dptr, Eptr,
								scanline, info->width*2, sharpening);
					}

					sptr = scanline;
					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width*2, 1, y*2,
							(uint32_t *)scanline, (uint32_t *)scanline2, info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width*2, 1, y, sptr, outA8, pitch,
							info->format, whitebitdepth, flags);

					Aptr += rgbpitch16;
					Bptr += rgbpitch16;
					Cptr += rgbpitch16;
					Dptr += rgbpitch16;
					Eptr += rgbpitch16;


					if(sharpening == 0)
					{
						FastBlurV(Bptr, Cptr, Dptr, scanline, info->width*2); // new C
					}
					else
					{
						FastSharpeningBlurV(Aptr, Bptr, Cptr, Dptr, Eptr,
								scanline, info->width*2, sharpening);
					}

					flags = 0;
					whitebitdepth = 16;
					sptr = scanline;
					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width*2, 1, y*2+1,
							(uint32_t *)scanline, (uint32_t *)scanline2, info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width*2, 1, y, sptr, outB8, pitch,
							info->format, whitebitdepth, flags);
				}
			}

			// No more work to do
			return;
		}
	}
}



void VerticalOnlyDemosaicRAWFast(DECODER *decoder, FRAME_INFO *info, int thread_index, uint8_t *output, int pitch, uint8_t *scratch, int scratchsize)
{
	//int bayer_format = decoder->cfhddata.bayer_format;
	THREAD_ERROR error;
	int highquality = 0;
	//int deripple = 0;
	int sharpening = -1;
	uint8_t *scratchptr = scratch;
	//int scratchremain = scratchsize;

	for (;;)
	{
		int work_index;
		int work_index2;
		//int work_index4;
		//int row;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			int bayer_format = decoder->cfhddata.bayer_format;
			unsigned char *outA8, *outB8;
			//unsigned short *lineStartA16, *lineStartB16;
			//unsigned short *lineA16, *lineB16;
			//uint8_t *line = output;
			PIXEL *bayer_line = (PIXEL *)decoder->RawBayer16;
			//PIXEL *bayerptr;
			int bayer_pitch = info->width*4;
			//int x;
			int y;
			int job = 0;


			// job level 0
			y = work_index;
			bayer_line = (PIXEL *)decoder->RawBayer16;
			bayer_line += bayer_pitch * y;

			ColorDifference2Bayer(info->width, (unsigned short *)bayer_line, bayer_pitch, bayer_format);

			// job level 2
			job++;
			while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
				&work_index2, thread_index, job, 3))
			{
				unsigned short *sptr;
				unsigned short *scanline;
				unsigned short *scanline2;
				uint8_t *line = output;
				int flags = 0;
				int whitebitdepth = 16;

				assert(scratchsize > (info->width)*3*2 * 2);

				scanline = (unsigned short *)scratchptr;
				scanline2 = scanline;
				scanline2 += (info->width*2)*3*2;


				y = work_index2;

				line += y * pitch * 2;
				outA8 = line;
				line += pitch;
				outB8 = line;
				line += pitch;

				sptr = scanline;


				if(decoder->frame.generate_look)
				{
					DrawBlankLUT(sptr,info->width,y*2,2);
				}
				else
				{
					VerticalOnlyDebayerLine(info->width*2, info->height*2, y*2,
						decoder->RawBayer16,  bayer_format, sptr, highquality, sharpening);
				}

				if(decoder->apply_color_active_metadata)
				{
					sptr = ApplyActiveMetaData(decoder, info->width, 1, y*2,
						(uint32_t *)scanline, (uint32_t *)scanline2,
						info->format, &whitebitdepth, &flags);
					
					ConvertLinesToOutput(decoder, info->width, 1, y*2, sptr, outA8, pitch,
							info->format, whitebitdepth, flags);

					flags = 0;
					whitebitdepth = 16;
					scanline += info->width * 3 * 2;
					outA8 += pitch;
					sptr = ApplyActiveMetaData(decoder, info->width, 1, y*2+1,
						(uint32_t *)scanline, (uint32_t *)scanline2,
						info->format, &whitebitdepth, &flags);
					
					ConvertLinesToOutput(decoder, info->width, 1, y*2+1, sptr, outA8, pitch,
							info->format, whitebitdepth, flags);
				}
				else
				{
					ConvertLinesToOutput(decoder, info->width, 1, y*2, sptr, outA8, pitch,
							info->format, whitebitdepth, flags);

					sptr += info->width * 3 * 2;
					outA8 += pitch;

					ConvertLinesToOutput(decoder, info->width, 1, y*2+1, sptr, outA8, pitch,
							info->format, whitebitdepth, flags);
				}
			}
		}
		else
		{
			int bayer_format = decoder->cfhddata.bayer_format;
			unsigned char *outA8, *outB8;
			//unsigned short *lineStartA16, *lineStartB16;
			//unsigned short *lineA16, *lineB16;
			//uint8_t *line = output;
			//PIXEL *bayer_line = (PIXEL *)decoder->RawBayer16;
			//PIXEL *bayerptr;
			//int bayer_pitch = info->width*4;
			//int x;
			int job = 0;

			// job level 2
			{
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index2, thread_index, job, 3))
				{
					int y = work_index2;
					unsigned short *sptr;
					unsigned short *scanline;
					unsigned short *scanline2;
					uint8_t *line = output;
					int flags = 0;
					int whitebitdepth = 16;

					assert(scratchsize > (info->width*2)*3*2 * 2);

					scanline = (unsigned short *)scratchptr;
					scanline2 = scanline;
					scanline2 +=  (info->width*2)*3*2;

					line += y * pitch * 2;
					outA8 = line;
					line += pitch;
					outB8 = line;
					line += pitch;

					sptr = scanline;

					if(decoder->frame.generate_look)
					{
						DrawBlankLUT(sptr,info->width,y*2,2);
					}
					else
					{
						VerticalOnlyDebayerLine(info->width*2, info->height*2, y*2,
							decoder->RawBayer16,  bayer_format, sptr, highquality, sharpening);
					}

						
					if(decoder->apply_color_active_metadata)
					{
						sptr = ApplyActiveMetaData(decoder, info->width, 1, y*2,
							(uint32_t *)scanline, (uint32_t *)scanline2,
							info->format, &whitebitdepth, &flags);
						
						ConvertLinesToOutput(decoder, info->width, 1, y*2, sptr, outA8, pitch,
								info->format, whitebitdepth, flags);

						flags = 0;
						whitebitdepth = 16;
						scanline += info->width * 3 * 2;
						outA8 += pitch;
						sptr = ApplyActiveMetaData(decoder, info->width, 1, y*2+1,
							(uint32_t *)scanline, (uint32_t *)scanline2,
							info->format, &whitebitdepth, &flags);
						
						ConvertLinesToOutput(decoder, info->width, 1, y*2+1, sptr, outA8, pitch,
								info->format, whitebitdepth, flags);
					}
					else
					{
						ConvertLinesToOutput(decoder, info->width, 1, y*2, sptr, outA8, pitch,
								info->format, whitebitdepth, flags);

						sptr += info->width * 3 * 2;
						outA8 += pitch;

						ConvertLinesToOutput(decoder, info->width, 1, y*2+1, sptr, outA8, pitch,
								info->format, whitebitdepth, flags);
					}
				}
			}

			// No more work to do
			return;
		}
	}
}

void NoDemosaicRAW(DECODER *decoder, FRAME_INFO *info, int thread_index, uint8_t *output, int pitch, uint8_t *scratch, int scratchsize)
{
	//int bayer_format = decoder->cfhddata.bayer_format;
	THREAD_ERROR error;
	uint8_t *scratchptr = scratch;
	//int scratchremain = scratchsize;


	for (;;)
	{
		int work_index;
		//int work_index1;
		//int work_index2;
		//int work_index3;
		//int work_index4;
		//int row;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			//int bayer_format = decoder->cfhddata.bayer_format;
			//unsigned char *outA8, *outB8;
			//unsigned short *lineStartA16, *lineStartB16;
			//unsigned short *lineA16, *lineB16;
			uint8_t *line = (uint8_t *)output;
			PIXEL *bayer_line = (PIXEL *)decoder->RawBayer16;
			PIXEL *bayerptr;
			int bayer_pitch = info->width*4;
			int x,y;
			//int job = 0;
			//int rgbpitch16 = (info->width)*3;
			unsigned short *sptr;
			unsigned short *scanline;
			unsigned short *scanline2;
			scanline = (unsigned short *)scratchptr;
			scanline2 = scanline;
			scanline2 += scratchsize/(sizeof(short)*2);

			// job level 0
			y = work_index;
			bayer_line = (PIXEL *)decoder->RawBayer16;
			bayer_line += bayer_pitch * y;

			line += y * pitch;

			{
				PIXEL16U *G,*RG,*BG,*GD;
				int r,g,b,rg,bg;
				//int y1,y2,u,v;
				//int r1,g1,b1;
				//int i;

				__m128i gggggggg,ggggggg2,rgrgrgrg,bgbgbgbg;
				__m128i rrrrrrrr,bbbbbbbb;
				__m128i mid8192 = _mm_set1_epi16(8192);
				//__m128i mid16384 = _mm_set1_epi16(16384);
				//__m128i mid32768 = _mm_set1_epi16(32768);

				__m128i overflowprotectRGB_epi16 = _mm_set1_epi16(0x7fff-0x3fff);
				int sse2width = info->width & 0xfff8;

				bayerptr = bayer_line;
				G = (PIXEL16U *)bayerptr;
				RG = G + bayer_pitch/4;
				BG = RG + bayer_pitch/4;
				GD = BG + bayer_pitch/4;

				sptr = scanline;



				x = 0;
				for(; x<sse2width; x+=8) //TODO SSE version
				{
					gggggggg = _mm_loadu_si128((__m128i *)G); G+=8;
					rgrgrgrg = _mm_loadu_si128((__m128i *)RG); RG+=8;
					bgbgbgbg = _mm_loadu_si128((__m128i *)BG); BG+=8;


					ggggggg2 = _mm_srli_epi16(gggggggg, 2);// 0-16383 14bit unsigned
					rgrgrgrg = _mm_srli_epi16(rgrgrgrg, 2);// 14bit unsigned
					bgbgbgbg = _mm_srli_epi16(bgbgbgbg, 2);// 14bit unsigned

					rrrrrrrr = _mm_subs_epi16(rgrgrgrg, mid8192);// -8191 to 8191 14bit signed
					rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 1);		// -16382 to 16382 15bit signed
					rrrrrrrr = _mm_adds_epi16(rrrrrrrr, ggggggg2); // -16382 to 32767

					bbbbbbbb = _mm_subs_epi16(bgbgbgbg, mid8192);// -8191 to 8191 14bit signed
					bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 1);		// -16382 to 16382 15bit signed
					bbbbbbbb = _mm_adds_epi16(bbbbbbbb, ggggggg2); // -16382 to 32767

					//limit to 0 to 16383
					rrrrrrrr = _mm_adds_epi16(rrrrrrrr, overflowprotectRGB_epi16);
					rrrrrrrr = _mm_subs_epu16(rrrrrrrr, overflowprotectRGB_epi16);

					//limit to 0 to 16383
					bbbbbbbb = _mm_adds_epi16(bbbbbbbb, overflowprotectRGB_epi16);
					bbbbbbbb = _mm_subs_epu16(bbbbbbbb, overflowprotectRGB_epi16);

					rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 2); // restore to 0 to 65535
					bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 2); // restore to 0 to 65535


					sptr[0]  = _mm_extract_epi16(rrrrrrrr, 0);
					sptr[1]  = _mm_extract_epi16(gggggggg, 0);
					sptr[2]  = _mm_extract_epi16(bbbbbbbb, 0);
					sptr[3]  = _mm_extract_epi16(rrrrrrrr, 1);
					sptr[4]  = _mm_extract_epi16(gggggggg, 1);
					sptr[5]  = _mm_extract_epi16(bbbbbbbb, 1);
					sptr[6]  = _mm_extract_epi16(rrrrrrrr, 2);
					sptr[7]  = _mm_extract_epi16(gggggggg, 2);
					sptr[8]  = _mm_extract_epi16(bbbbbbbb, 2);
					sptr[9]  = _mm_extract_epi16(rrrrrrrr, 3);
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
					sptr+=24;
				}

				for(; x<info->width; x++) //TODO SSE version
				{
					g = (*G++);
					rg = (*RG++);
					bg = (*BG++);

					r = ((rg - 32768)<<1) + g;
					b = ((bg - 32768)<<1) + g;

					if(r < 0) r = 0;  if(r > 0xffff) r = 0xffff;
					if(g < 0) g = 0;  if(g > 0xffff) g = 0xffff;
					if(b < 0) b = 0;  if(b > 0xffff) b = 0xffff;

					sptr[0] = r;
					sptr[1] = g;
					sptr[2] = b;
					sptr+=3;
				}

				if(decoder->frame.generate_look)
				{
					DrawBlankLUT(sptr,info->width,y,1);
				}

				{
					int flags = 0;
					int whitebitdepth = 16;

					sptr = scanline;
					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width, 1, y,
							(uint32_t *)scanline, (uint32_t *)scanline2,
							info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width, 1, y, sptr, line, pitch,
							info->format, whitebitdepth, flags);
				}
			}
		}
		else
		{
			// No more work to do
			return;
		}
	}
}




void VerticalOnlyDemosaicRAW(DECODER *decoder, FRAME_INFO *info, int thread_index, uint8_t *output, int pitch, uint8_t *scratch, int scratchsize)
{
	//int bayer_format = decoder->cfhddata.bayer_format;
	THREAD_ERROR error;
	int highquality = 0;
	int deripple = 0;
	int sharpening = -1;
	uint8_t *scratchptr = scratch;
	//int scratchremain = scratchsize;
	int debayerfilter = (decoder->cfhddata.process_path_flags_mask >> 16) & 0xf; // 8-bit debayer selector

/*	if(info->format == COLOR_FORMAT_YUYV)
	{
		debayerfilter = 1; // force YUV playback to bilinear
	}
*/
	if(	info->format == COLOR_FORMAT_B64A ||
		info->format == COLOR_FORMAT_W13A ||
		info->format == COLOR_FORMAT_WP13 ||
		info->format == COLOR_FORMAT_RG48 ||
		info->format == COLOR_FORMAT_RG64 ||
		info->format == COLOR_FORMAT_AR10 ||
		info->format == COLOR_FORMAT_AB10 ||
		info->format == COLOR_FORMAT_RG30 ||
		info->format == COLOR_FORMAT_R210 ||
		info->format == COLOR_FORMAT_DPX0 ||
		info->format == COLOR_FORMAT_YR16 ||
		info->format == COLOR_FORMAT_YU64 ||
		info->format == COLOR_FORMAT_V210 ||
		info->format == COLOR_FORMAT_R4FL)
	{
		debayerfilter = (decoder->cfhddata.process_path_flags_mask >> 20) & 0xf; // 16-bit debayer selector
		highquality = 1;
	}

	if(decoder->cfhddata.demosaic_type)
		debayerfilter = decoder->cfhddata.demosaic_type;


	switch(debayerfilter)
	{
	case 0: // unset
	case 15: // assume unset
	default:
		if(highquality) // 5x5 Enh (because 16-bit decode)
		{
			sharpening = 1; // Detail 1
		}
		else  // bilinear
		{
			sharpening = -1; // no smoothing
		}
		break;
	case 1: // blinear
		sharpening = -1;
		highquality = 0;
		deripple = 0;
		break;
	case 2: // 5x5 Enh
		sharpening = -1;
		highquality = 1;
		deripple = 1;
		break;
	case 3: // Advanced Smooth
		sharpening = 0;
		highquality = 1;
		deripple = 1;
		break;
	case 4: // Advanced Detail 1
		sharpening = 1;
		highquality = 1;
		deripple = 1;
		break;
	case 5: // Advanced Detail 2
		sharpening = 2;
		highquality = 1;
		deripple = 1;
		break;
	case 6: // Advanced Detail 3
		sharpening = 3;
		highquality = 1;
		deripple = 1;
		break;
	case 7: // bilinear Smoothed (needs sharpening + 1)
		sharpening = 1;
		highquality = 0;
		deripple = 1;
		break;
	case 8: // bilinear Detail 1
		sharpening = 2;
		highquality = 0;
		deripple = 1;
		break;
	case 9: // bilinear Detail 2
		sharpening = 3;
		highquality = 0;
		deripple = 1;
		break;
	}

	if(decoder->sample_uncompressed)
		deripple = 0;


	if(sharpening < 0)
	{
		VerticalOnlyDemosaicRAWFast(decoder, info, thread_index, output, pitch, scratch, scratchsize);
		
		// No more work to do
		return;
	}

	for (;;)
	{
		int work_index;
		int work_index1;
		int work_index2;
		int work_index3;
		//int work_index4;
		//int row;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			int bayer_format = decoder->cfhddata.bayer_format;
			unsigned char *outA8, *outB8;
			//unsigned short *lineStartA16, *lineStartB16;
			//unsigned short *lineA16, *lineB16;
			//uint8_t *line = output;
			PIXEL *bayer_line = (PIXEL *)decoder->RawBayer16;
			//PIXEL *bayerptr;
			int bayer_pitch = info->width*4;
			//int x;
			int y;
			int job = 0;


			// job level 0
			y = work_index;
			bayer_line = (PIXEL *)decoder->RawBayer16;
			bayer_line += bayer_pitch * y;

			ColorDifference2Bayer(info->width, (unsigned short *)bayer_line, bayer_pitch, bayer_format);

			// job level 1
			if(deripple)
			{
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index1, thread_index, job, 3))
				{
					y = work_index1;
					if(y>=3 && y<info->height-3) //middle scanline
					{
						unsigned short *delayptr = decoder->RawBayer16;
						delayptr += bayer_pitch * y;

						BayerRippleFilter(info->width,
							delayptr, bayer_pitch, bayer_format, decoder->RawBayer16);
					}
				}
			}

			{
				// job level 2
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index2, thread_index, job, 3))
				{
					//unsigned short *sptr;
					unsigned short *RGBbuffer = decoder->RGBFilterBuffer16;

					y = work_index2;

					RGBbuffer += y*2 * info->width*2*3;
					
					VerticalOnlyDebayerLine(info->width*2, info->height*2, y*2,
						decoder->RawBayer16,  bayer_format, RGBbuffer, highquality, sharpening);
				}

				// job level 3
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index3, thread_index, job, 3))
				{
					int y = work_index3;

					unsigned short *Aptr;
					unsigned short *Bptr;
					unsigned short *Cptr;
					unsigned short *Dptr;
					unsigned short *Eptr;
					unsigned short *scanline;
					unsigned short *scanline2;
					unsigned short *sptr;
					uint8_t *line = output;
					int flags = 0;
					int whitebitdepth = 16;
					int rgbpitch16 = (info->width*2)*3;

					unsigned short *RGBbuffer = decoder->RGBFilterBuffer16;

					line += y * pitch * 2;
					outA8 = line;
					line += pitch;
					outB8 = line;
					line += pitch;

					RGBbuffer += y*2 * rgbpitch16;

					assert(scratchsize > rgbpitch16*2*2);
					scanline = (unsigned short *)scratchptr;
					scanline2 = scanline;
					scanline2 += rgbpitch16;

					Cptr = RGBbuffer;
					Bptr = Cptr;
					if(y>=1)
						Bptr -= rgbpitch16;
					Aptr = Bptr;
					if(y>=1)
						Aptr -= rgbpitch16;
					Dptr = Cptr;
					if(y < info->height-1)
						Dptr += rgbpitch16;
					Eptr = Dptr;
					if(y < info->height-1)
						Eptr += rgbpitch16;


					if(sharpening == 0)
					{
						FastBlurV(Bptr, Cptr, Dptr, scanline, info->width); // new C
					}
					else
					{
						FastSharpeningBlurV(Aptr, Bptr, Cptr, Dptr, Eptr,
								scanline, info->width, sharpening);
					}

					sptr = scanline;
					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width, 1, y*2,
							(uint32_t *)scanline, (uint32_t *)scanline2, info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width, 1, y, sptr, outA8, pitch,
							info->format, whitebitdepth, flags);

					Aptr += rgbpitch16;
					Bptr += rgbpitch16;
					Cptr += rgbpitch16;
					Dptr += rgbpitch16;
					Eptr += rgbpitch16;


					if(sharpening == 0)
					{
						FastBlurV(Bptr, Cptr, Dptr, scanline, info->width); // new C
					}
					else
					{
						FastSharpeningBlurV(Aptr, Bptr, Cptr, Dptr, Eptr,
								scanline, info->width, sharpening);
					}

					flags = 0;
					whitebitdepth = 16;
					sptr = scanline;
					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width, 1, y*2+1,
							(uint32_t *)scanline, (uint32_t *)scanline2, info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width, 1, y, sptr, outB8, pitch,
							info->format, whitebitdepth, flags);
				}
			}
		}
		else
		{
			int bayer_format = decoder->cfhddata.bayer_format;
			unsigned char *outA8, *outB8;
			//unsigned short *lineStartA16, *lineStartB16;
			//unsigned short *lineA16, *lineB16;
			//uint8_t *line = output;
			//PIXEL *bayer_line = (PIXEL *)decoder->RawBayer16;
			//PIXEL *bayerptr;
			int bayer_pitch = info->width*4;
			//int x;
			int y;
			int job = 0;

			// job level 1
			if(deripple)
			{
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index1, thread_index, job, 3))
				{
					y = work_index1;
					if(y>=3 && y<info->height-3) //middle scanlines
					{
						unsigned short *delayptr = decoder->RawBayer16;
						delayptr += bayer_pitch * y;

						BayerRippleFilter(info->width,
							delayptr, bayer_pitch, bayer_format, decoder->RawBayer16);
					}
				}
			}


			// job level 2
			if(sharpening < 0 || decoder->frame.generate_look)
			{
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index2, thread_index, job, 3))
				{
					int y = work_index2;
					unsigned short *sptr;
					unsigned short *scanline;
					unsigned short *scanline2;
					uint8_t *line = output;
					int flags = 0;
					int whitebitdepth = 16;

					assert(scratchsize > (info->width*2)*3*2 * 2);

					scanline = (unsigned short *)scratchptr;
					scanline2 = scanline;
					scanline2 +=  (info->width*2)*3*2;

					line += y * pitch * 2;
					outA8 = line;
					line += pitch;
					outB8 = line;
					line += pitch;

					sptr = scanline;

					if(decoder->frame.generate_look)
					{
						DrawBlankLUT(sptr,info->width*2,y*2,2);
					}
					else
					{
						VerticalOnlyDebayerLine(info->width*2, info->height*2, y*2,
							decoder->RawBayer16,  bayer_format, sptr, highquality, sharpening);
					}

					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width, 2, y*2,
							(uint32_t *)scanline, (uint32_t *)scanline2, info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width, 2, y, sptr, outA8, pitch,
							info->format, whitebitdepth, flags);
				}
			}

			else
			{
				// job level 2
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index2, thread_index, job, 3))
				{
					//unsigned short *sptr;
					unsigned short *RGBbuffer = decoder->RGBFilterBuffer16;

					y = work_index2;

					RGBbuffer += y*2 * info->width*2*3;

					VerticalOnlyDebayerLine(info->width*2, info->height*2, y*2,
						decoder->RawBayer16,  bayer_format, RGBbuffer, highquality, sharpening);
				}

				// job level 3
				job++;
				while(THREAD_ERROR_OKAY == PoolThreadGetDependentJob(&decoder->worker_thread.pool,
					&work_index3, thread_index, job, 3))
				{

					int y = work_index3;

					unsigned short *Aptr;
					unsigned short *Bptr;
					unsigned short *Cptr;
					unsigned short *Dptr;
					unsigned short *Eptr;
					unsigned short *scanline;
					unsigned short *scanline2;
					unsigned short *sptr;
					uint8_t *line = output;
					int flags = 0;
					int whitebitdepth = 16;
					int rgbpitch16 = (info->width*2)*3;

					unsigned short *RGBbuffer = decoder->RGBFilterBuffer16;

					line += y * pitch * 2;
					outA8 = line;
					line += pitch;
					outB8 = line;
					line += pitch;

					RGBbuffer += y*2 * rgbpitch16;

					assert(scratchsize > rgbpitch16*2*2);
					scanline = (unsigned short *)scratchptr;
					scanline2 = scanline;
					scanline2 += rgbpitch16;

					Cptr = RGBbuffer;
					Bptr = Cptr;
					if(y>=1)
						Bptr -= rgbpitch16;
					Aptr = Bptr;
					if(y>=1)
						Aptr -= rgbpitch16;
					Dptr = Cptr;
					if(y < info->height-1)
						Dptr += rgbpitch16;
					Eptr = Dptr;
					if(y < info->height-1)
						Eptr += rgbpitch16;


					if(sharpening == 0)
					{
						FastBlurV(Bptr, Cptr, Dptr, scanline, info->width); // new C

					}
					else
					{
						FastSharpeningBlurV(Aptr, Bptr, Cptr, Dptr, Eptr,
								scanline, info->width, sharpening);
					}

					sptr = scanline;
					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width, 1, y*2,
							(uint32_t *)scanline, (uint32_t *)scanline2, info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width, 1, y, sptr, outA8, pitch,
							info->format, whitebitdepth, flags);

					Aptr += rgbpitch16;
					Bptr += rgbpitch16;
					Cptr += rgbpitch16;
					Dptr += rgbpitch16;
					Eptr += rgbpitch16;


					if(sharpening == 0)
					{
						FastBlurV(Bptr, Cptr, Dptr, scanline, info->width); // new C
					}
					else
					{
						FastSharpeningBlurV(Aptr, Bptr, Cptr, Dptr, Eptr,
								scanline, info->width, sharpening);
					}

					flags = 0;
					whitebitdepth = 16;
					sptr = scanline;
					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width, 1, y*2+1,
							(uint32_t *)scanline, (uint32_t *)scanline2, info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width, 1, y, sptr, outB8, pitch,
							info->format, whitebitdepth, flags);
				}
			}

			// No more work to do
			return;
		}
	}
}


extern void RGB48VerticalShift(DECODER *decoder, unsigned short *RGB48, unsigned short *buffer,
						int widthbytes, int height, int pitch, float offset/* -1.0 to 1.0 screen widths*/);
extern void RGB48VerticalShiftZoom(DECODER *decoder, unsigned short *RGB48, unsigned short *buffer,
						int widthbytes, int height, int pitch, float offset/* -1.0 to 1.0 screen widths*/, float zoom);
extern void RGB48VerticalShiftZoomFine(DECODER *decoder, unsigned short *RGB48, unsigned short *buffer,
						int widthbytes, int height, int pitch, float offset/* -1.0 to 1.0 screen widths*/, float zoom, int x);

extern void ProcessLine3D(DECODER *decoder, uint8_t *scratchptr, int scratchremain, uint8_t *output, int pitch,
												   uint8_t *local_output, int local_pitch, int channel_offset, int y, int flags);

extern void SharpenLine(DECODER *decoder, uint8_t *scratchptr, int scratchremain, uint8_t *output, int pitch,
												   uint8_t *local_output, int local_pitch, int channel_offset, int y, int thread_index);


void Do3DWork(DECODER *decoder, FRAME_INFO *info, int thread_index, uint8_t *output, int pitch,
			  uint8_t *scratch, int scratchsize, uint8_t *local_output, int local_pitch, int channel_offset,
			  int chunk_size, int line_max)
{
	THREAD_ERROR error;
	uint8_t *scratchptr = scratch;
	//uint8_t *blankline = scratch;
	int scratchremain = scratchsize;
	static FRAME_REGION emptyFrameMask = FRAME_REGION_INITIALIZER;
	int ymin = 0, ymax;
	int width,height;
	//int clearmem = 1;

	width = decoder->frame.width;
	height = decoder->frame.height;

	//blank line is at the beginning of the scratch
	scratchptr += abs(local_pitch);

	ymax = line_max;
	if((decoder->cfhddata.process_path_flags & PROCESSING_FRAMING) &&
		memcmp(&decoder->cfhddata.channel[0].FrameMask, &emptyFrameMask, 32))
	{
		ymin = (int)((float)line_max * decoder->cfhddata.channel[0].FrameMask.topLftY);
		ymax = (int)((float)line_max * decoder->cfhddata.channel[0].FrameMask.botLftY);
	}

	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			int y;

			// job level 0
			y = work_index*chunk_size;

			for(; y<(work_index*chunk_size)+chunk_size && y < line_max; y++)
			{
				if(y < ymin || y >= ymax)
				{
					//memset(blankline, 0, abs(local_pitch)); // zero one line;
					//ProcessLine3D(decoder, scratchptr, scratchremain, output, pitch, blankline, 0, 0, y, 1);
					ProcessLine3D(decoder, scratchptr, scratchremain, output, pitch, local_output, local_pitch, channel_offset, y, 1);
				}
				else
				{
					ProcessLine3D(decoder, scratchptr, scratchremain, output, pitch, local_output, local_pitch, channel_offset, y, 0);
				}
			}

		}
		else
		{
			// No more work to do
			return;
		}
	}
}

#if WARPSTUFF
void DoWarp(DECODER *decoder, void *mesh, uint8_t *output, int *lens_correct_buffer,
			  int thread_index, int line_max, int chunk_size)
{
	THREAD_ERROR error;

	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			int y, y2;

			// job level 0
			y = work_index*chunk_size;
			y2 = y + chunk_size;
			if(y2 > line_max) y2 = line_max;

			geomesh_apply_bilinear(mesh,(unsigned char *)output, (unsigned char *)lens_correct_buffer, y, y2);
		}
		else
		{
			// No more work to do
			return;
		}
	}
}


void DoWarpCache(DECODER *decoder, void *mesh, int thread_index, int line_max, int chunk_size, int flags)
{
	THREAD_ERROR error;

	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			int y, y2;

			// job level 0
			y = work_index*chunk_size;
			y2 = y + chunk_size;
			if(y2 > line_max) y2 = line_max;

			geomesh_cache_init_bilinear_range(mesh, y, y2);
		}
		else
		{
			// No more work to do
			return;
		}
	}
}
void DoWarpBlurV(DECODER *decoder, void *mesh, int thread_index, int line_max, int chunk_size, uint8_t *output, int pitch)
{
	THREAD_ERROR error;

	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			int x, x2;

			// job level 0
			x = work_index*chunk_size;
			x2 = x + chunk_size;
			if(x2 > line_max) x2 = line_max;

			geomesh_blur_vertical_range(mesh, x, x2, output, pitch);
		}
		else
		{
			// No more work to do
			return;
		}
	}
}
#endif //#if WARPSTUFF


void DoVertSharpen(DECODER *decoder, FRAME_INFO *info, int thread_index, uint8_t *output, int pitch,
			  uint8_t *scratch, int scratchsize, uint8_t *local_output, int local_pitch, int channel_offset,
			  int chunk_size, int line_max)
{
	THREAD_ERROR error;
	uint8_t *scratchptr = scratch;
	uint8_t *blankline = scratch;
	int scratchremain = scratchsize;
	static FRAME_REGION emptyFrameMask = FRAME_REGION_INITIALIZER;
	int ymin = 0, ymax;
	int width,height;
	//int clearmem = 1;

	width = decoder->frame.width;
	height = decoder->frame.height;

	//blank line is at the beginning of the scratch
	scratchptr += abs(local_pitch);

	ymax = line_max;
	if((decoder->cfhddata.process_path_flags & PROCESSING_FRAMING) &&
		memcmp(&decoder->cfhddata.channel[0].FrameMask, &emptyFrameMask, 32))
	{
		ymin = (int)((float)line_max * decoder->cfhddata.channel[0].FrameMask.topLftY);
		ymax = (int)((float)line_max * decoder->cfhddata.channel[0].FrameMask.botLftY);
	}

	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			int y;

			// job level 0
			y = work_index*chunk_size;

			for(; y<(work_index*chunk_size)+chunk_size && y < line_max; y++)
			{
				if(	decoder->channel_blend_type != BLEND_STACKED_ANAMORPHIC && 
					decoder->channel_blend_type != BLEND_FREEVIEW && 
					(y < ymin || y >= ymax))
				{
					memset(blankline, 0, abs(local_pitch)); // zero one line;
					SharpenLine(decoder, scratchptr, scratchremain, output, pitch, blankline, 0, 0, y, thread_index);
				}
				else
				{
					SharpenLine(decoder, scratchptr, scratchremain, output, pitch, local_output, local_pitch, channel_offset, y, thread_index);
				}
			}

		}
		else
		{
			// No more work to do
			return;
		}
	}
}

void Do3DVerticalWork(DECODER *decoder, FRAME_INFO *info, int thread_index, uint8_t *output, int pitch,
			  uint8_t *scratch, int scratchsize, uint8_t *local_output, int local_pitch, int channel_offset,
			  int chunk_size, int line_max, int fine_vertical)
{
	THREAD_ERROR error;
	uint8_t *scratchptr = scratch;
	//int scratchremain = scratchsize;
	int width = 0;//decoder->frame.width;
	int height = 0;//decoder->frame.height;  not suitable for interlaced 
	int channel_flip = decoder->cfhddata.channel_flip;
	float frameOffsetY = decoder->cfhddata.FrameOffsetY;
	float frameOffsetR = decoder->cfhddata.FrameOffsetR;
	float frameOffsetF = decoder->cfhddata.FrameOffsetF;
	float zoom;
	float zoomR;
	float frameZoom1 = decoder->cfhddata.channel[1].FrameZoom;
	float frameZoom2 = decoder->cfhddata.channel[2].FrameZoom;
	float frameAutoZoom = decoder->cfhddata.channel[0].FrameAutoZoom;
	float frameDiffZoom1 = decoder->cfhddata.channel[1].FrameDiffZoom;
	float frameDiffZoom2 = decoder->cfhddata.channel[2].FrameDiffZoom;	
	int aspectx,aspecty;
	float aspectfix;
	WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	width = mailbox->info.width;
	height = mailbox->info.height;

	GetDisplayAspectRatio(decoder, &aspectx, &aspecty); 
	aspectfix = (float)(aspectx*aspectx) / (float)(aspecty*aspecty);

	if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER) //HACK //DAN20110129
		width /= 2;

	if(decoder->source_channels < 2) // 2D
	{
		channel_flip &= 0x3;
		channel_flip |= channel_flip<<2;
		decoder->cfhddata.channel_flip = channel_flip;
	}

	if(!(decoder->cfhddata.process_path_flags & PROCESSING_FRAMING))
	{
		frameOffsetY = 0.0;
		frameOffsetR = 0.0;
		frameOffsetF = 0.0;
		frameZoom1 = 1.0;
		frameZoom2 = 1.0;
	}

	if(!(decoder->cfhddata.process_path_flags & PROCESSING_ORIENTATION))
	{
		frameAutoZoom = 1.0;
		frameDiffZoom1 = 1.0;
		frameDiffZoom2 = 1.0;
	}
	
	if(!(decoder->cfhddata.process_path_flags & PROCESSING_IMAGEFLIPS))
	{
		channel_flip = 0;
	}

	zoom = frameZoom1 * frameAutoZoom * frameDiffZoom1;
	zoomR = frameZoom2 * frameAutoZoom / frameDiffZoom2;

	
	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			int x,xbytes,xstep;
			uint8_t *base = local_output;
			unsigned short *scanline;
			float voffsetstep;
			float voffsetstepR;
			float voffset = decoder->cfhddata.channel[1].VerticalOffset;
			float roffset = decoder->cfhddata.channel[1].RotationOffset;
			float voffsetR = decoder->cfhddata.channel[2].VerticalOffset;
			float roffsetR = decoder->cfhddata.channel[2].RotationOffset;
			float voffset1, voffset2;
			float voffsetstep1, voffsetstep2;
			float zoffset1, zoffset2;
			float zoomstep1, zoomstep2;
			float frameOffY = frameOffsetY;
			float frameOffY1, frameOffY2;
			float frameOffR = frameOffsetR;
			float frameOffR1, frameOffR2;
			//float frameOffF = frameOffsetF;

			if(!(decoder->cfhddata.process_path_flags & PROCESSING_ORIENTATION))
			{
				voffset = roffset = 0;
				voffsetR = roffsetR = 0;
			}

			if(decoder->cfhddata.InvertOffset)
			{
				voffset = -voffset;
				roffset = -roffset;
				voffsetR = -voffsetR;
				roffsetR = -roffsetR;
				frameOffY = -frameOffY;
				frameOffR = -frameOffR;
			}

			frameOffY1 = frameOffY;
			frameOffY2 = frameOffY;

			frameOffR1 = frameOffR;
			frameOffR2 = frameOffR;


			// job level 0
			x = work_index*chunk_size;

			switch(decoder->StereoBufferFormat)
			{
			case DECODED_FORMAT_RGB32:
				xbytes = width*4;
				break;
			case DECODED_FORMAT_RGB32_INVERTED:
				xbytes = width*4;
				break;
			case DECODED_FORMAT_RGB24:
				xbytes = width*3;
				break;
			case DECODED_FORMAT_YUYV:
				xbytes = width*2;
				break;
			case DECODED_FORMAT_W13A:
			case DECODED_FORMAT_RG64:
				xbytes = width*8;
				break;
			case DECODED_FORMAT_WP13:
			case DECODED_FORMAT_RG48:
			default:
				xbytes = width*6;
				break;
			}
			xstep = (xbytes+line_max-1)/line_max;

			//DAN20100923 -- simplied
			//voffset += roffset * (float)(width*width) / (float)(height*height) * 0.5;
			//voffsetstep = -roffset  * (float)(width*width) / (float)(height*height) / (float)(xbytes/xstep);
			//voffsetR += roffsetR * (float)(width*width) / (float)(height*height) * 0.5;
			//voffsetstepR = -roffsetR  * (float)(width*width) / (float)(height*height) / (float)(xbytes/xstep);
			voffset += (roffset + frameOffR) * aspectfix * 0.5f;
			voffsetstep = -(roffset + frameOffR)  * aspectfix / (float)(xbytes/xstep);
			voffsetR += (roffsetR - frameOffR) * aspectfix * 0.5f;
			voffsetstepR = -(roffsetR - frameOffR)  * aspectfix / (float)(xbytes/xstep);


			scanline = (unsigned short *)scratchptr;

			zoffset1 = zoom;
			zoffset2 = zoomR;
			zoomstep1 = (decoder->cfhddata.channel[1].FrameKeyStone) / (float)(xbytes/xstep);
			zoomstep2 = -(decoder->cfhddata.channel[2].FrameKeyStone) / (float)(xbytes/xstep);

			zoffset1 -= decoder->cfhddata.channel[1].FrameKeyStone/2.0f;
			zoffset2 += decoder->cfhddata.channel[2].FrameKeyStone/2.0f;

			x *= xstep;
			base += x;

			voffset1 = voffset;
			voffset2 = voffsetR;
			voffsetstep1 = voffsetstep;
			voffsetstep2 = voffsetstepR;


			if(channel_flip & 0xf)
			{
				if(channel_flip & 2)
				{
					frameOffY1 = -frameOffY1;
					voffset1 = -voffset1;
					voffsetstep1 = -voffsetstep1;
				}
				if(channel_flip & 8)
				{
					frameOffY2 = -frameOffY2;
					voffset2 = -voffset2;
					voffsetstep2 = -voffsetstep2;
				}

				if(channel_flip & 1)
				{
					int xx;

					for(xx=0; xx<line_max*xstep; xx+=xstep*chunk_size)
					{
						voffset1 += voffsetstep1*chunk_size;
						zoffset1 += zoomstep1*chunk_size;
					}
					voffsetstep1 = -voffsetstep1;
					zoomstep1 = -zoomstep1;
				}
				if(channel_flip & 4)
				{
					int xx;
					for(xx=0; xx<line_max*xstep; xx+=xstep*chunk_size)
					{
						voffset2 += voffsetstep2*chunk_size;;
						zoffset2 += zoomstep2*chunk_size;
					}
					voffsetstep2 = -voffsetstep2;

					zoomstep2 = -zoomstep2;
				}
			}

			voffset1 += voffsetstep1 * (x/xstep);
			voffset2 += voffsetstep2 * (x/xstep);
			zoffset1 += zoomstep1 * (x/xstep);
			zoffset2 += zoomstep2 * (x/xstep);

			for(; x<(work_index*chunk_size*xstep)+chunk_size*xstep && x < xbytes; x+=xstep*chunk_size)
			{
				int processbytes = xstep*chunk_size;

				if(x + processbytes > xbytes)
					processbytes = xbytes - x;

				if(fine_vertical)
				{
					if(decoder->channel_decodes == 1 && decoder->channel_current == 1) // Right only
					{
						RGB48VerticalShiftZoomFine(decoder, (unsigned short *)base, (unsigned short *)scratch,
									processbytes, height, local_pitch, -voffset2 + frameOffY2, zoffset2, x);
					}
					else
					{
						RGB48VerticalShiftZoomFine(decoder, (unsigned short *)base, (unsigned short *)scratch,
									processbytes, height, local_pitch, voffset1 + frameOffY1, zoffset1, x);
					}

					if(decoder->channel_decodes == 2 && channel_offset != 0)
					{
						uint8_t *bptr = base + channel_offset;
						RGB48VerticalShiftZoomFine(decoder, (unsigned short *)bptr, (unsigned short *)scratch,
									processbytes, height, local_pitch, -voffset2 + frameOffY2, zoffset2, x);
					}
				}
				else if(zoffset1 == 1.0 && zoffset2 == 1.0 && zoomstep1 == 0.0)
				{
					if(decoder->channel_decodes == 1 && decoder->channel_current == 1) // Right only
					{
						RGB48VerticalShift(decoder, (unsigned short *)base, (unsigned short *)scratch,
									processbytes, height, local_pitch, -voffset2 + frameOffY2);
					}
					else
					{
						RGB48VerticalShift(decoder, (unsigned short *)base, (unsigned short *)scratch,
									processbytes, height, local_pitch, voffset1 + frameOffY1);
					}

					if(decoder->channel_decodes == 2 && channel_offset != 0)
					{
						uint8_t *bptr = base + channel_offset;
						RGB48VerticalShift(decoder, (unsigned short *)bptr, (unsigned short *)scratch,
									processbytes, height, local_pitch, -voffset2 + frameOffY2);
					}
				}
				else
				{
					if(decoder->channel_decodes == 1 && decoder->channel_current == 1) // Right only
					{
						RGB48VerticalShiftZoom(decoder, (unsigned short *)base, (unsigned short *)scratch,
									processbytes, height, local_pitch, -voffset2 + frameOffY2, zoffset2);
					}
					else
					{
						RGB48VerticalShiftZoom(decoder, (unsigned short *)base, (unsigned short *)scratch,
									processbytes, height, local_pitch, voffset1 + frameOffY1, zoffset1);
					}

					if(decoder->channel_decodes == 2 && channel_offset != 0)
					{
						uint8_t *bptr = base + channel_offset;
						RGB48VerticalShiftZoom(decoder, (unsigned short *)bptr, (unsigned short *)scratch,
									processbytes, height, local_pitch, -voffset2 + frameOffY2, zoffset2);
					}
				}


				base += xstep*chunk_size;
				voffset1 += voffsetstep1*chunk_size;
				voffset2 += voffsetstep2*chunk_size;
				zoffset1 += zoomstep1*chunk_size;
				zoffset2 += zoomstep2*chunk_size;
			}
		}
		else
		{
			// No more work to do
			return;
		}
	}
}

extern void HistogramLine(DECODER *decoder, unsigned short *sbase, int width, int format, int whitepoint);

void DoHistogramWork(DECODER *decoder, FRAME_INFO *info, int thread_index, uint8_t *output, int pitch,
			  uint8_t *scratch, int scratchsize, uint8_t *local_output, int local_pitch, int channel_offset,
			  int chunk_size, int line_max)
{
	THREAD_ERROR error;
	//uint8_t *scratchptr = scratch;
	//int scratchremain = scratchsize;
	int width = decoder->frame.width;
	//int height = decoder->frame.height;
			
	if(decoder->channel_blend_type == BLEND_SIDEBYSIDE_ANAMORPHIC ||
		decoder->channel_blend_type == BLEND_FREEVIEW)
		width >>= 1;


	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			uint8_t *bptr = output;
			bptr +=  pitch * work_index;

			HistogramLine(decoder, (unsigned short *)bptr, width, decoder->frame.output_format, 16);

			if(decoder->tools->histogram == 0)
				return; // don't know how to create Histogram for that format
		}
		else
		{
			// No more work to do
			return;
		}
	}
}


#if 0
void DoBurninWork(DECODER *decoder, FRAME_INFO *info, int thread_index, uint8_t *output, int pitch,
			  uint8_t *scratch, int scratchsize, uint8_t *local_output, int local_pitch, int channel_offset,
			  int chunk_size, int line_max)
{
	THREAD_ERROR error;
	uint8_t *scratchptr = scratch;
	int scratchremain = scratchsize;
	int width = decoder->frame.width;
	int height = decoder->frame.height;
	int targetW = width / 4;
	int targetH = height / 8;

	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			HistogramRender(decoder, output, pitch, decoder->frame.output_format, work_index, targetW, targetH);
		}
		else
		{
			// No more work to do
			return;
		}
	}
}
#endif

void QuarterRAW(DECODER *decoder, FRAME_INFO *info, int thread_index, uint8_t *output, int pitch, uint8_t *scratch, int scratchsize)
{
	//int bayer_format = decoder->cfhddata.bayer_format;
	THREAD_ERROR error;
	uint8_t *scratchptr = scratch;
	int scratchremain = scratchsize;
	int channel;

	TRANSFORM **transform_array = decoder->transform;
	IMAGE *lowpass_images[TRANSFORM_MAX_CHANNELS];

	unsigned short *scanline,*sptr;
	unsigned short *scanline2;
	char *buffer = (char *)scratchptr;
	size_t buffer_size = scratchremain;

	IMAGE *g_image;
	IMAGE *rg_image;
	IMAGE *bg_image;
	IMAGE *gd_image;

	uint8_t *line = output;
	PIXEL *G,*RG,*BG;
	int x,y;
	//int bayer_pitch = info->width*4;
	int format = info->format;
	bool inverted = false;
	int maxbound = 4095; //10-bit source
	int midpoint = 32768>>3;
	int shift = 4;

	for (channel = 0; channel < 3; channel++)
	{
		lowpass_images[channel] = transform_array[channel]->wavelet[decoder->gop_frame_num];//0/*frame*/];
	}
	g_image = lowpass_images[0];
	rg_image = lowpass_images[1];
	bg_image = lowpass_images[2];
	gd_image = lowpass_images[3];


	if(decoder->codec.precision == 12)
	{
		maxbound = 16383;
		midpoint = 32768>>1;
		shift = 2;
	}


	if (buffer_size < (size_t)(info->width) * 2 * 3 * 2) {
		// Not enough memory
		assert(0);
	}

	if (format == DECODED_FORMAT_RGB24 || format == DECODED_FORMAT_RGB32)
	{
		inverted = true;
		line += (info->height-1)*pitch;
		pitch = -pitch;
	}

	scanline = (unsigned short *)buffer;
	buffer += info->width * 2 * 3;
	scanline2 = (unsigned short *)buffer;

	G = g_image->band[0];
	RG = rg_image->band[0];
	BG = bg_image->band[0];



	for (;;)
	{
		int work_index;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			y = work_index;
			{
				uint8_t *newline = line;
				PIXEL *newG=G,*newRG=RG,*newBG=BG;
				PIXEL *gptr,*rgptr,*bgptr;
				int r,g,b,rg,bg;

				newline += pitch*y;

				newG += y * (g_image->pitch / sizeof(PIXEL));
				newRG += y * (rg_image->pitch / sizeof(PIXEL));
				newBG += y * (bg_image->pitch / sizeof(PIXEL));

				gptr = newG;
				rgptr = newRG;
				bgptr = newBG;

				sptr = scanline;

				for(x=0; x<info->width; x++)
				{
					g = (*gptr++);
					if(g > maxbound) g = maxbound;
					rg = (*rgptr++);
					bg = (*bgptr++);

					r = (rg<<1) - midpoint + g;
					b = (bg<<1) - midpoint + g;

					if(r > maxbound) r = maxbound;
					if(b > maxbound) b = maxbound;

					if(r < 0) r = 0;
					if(g < 0) g = 0;
					if(b < 0) b = 0;

					sptr[0] = r<<shift;
					sptr[1] = g<<shift;
					sptr[2] = b<<shift;
					sptr+=3;
				}

				{
					int flags = 0;
					int whitebitdepth = 16;

					sptr = scanline;
					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, info->width, 1, y,
							(uint32_t *)scanline, (uint32_t *)scanline2,
							info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, info->width, 1, y, sptr, newline, pitch,
							info->format, whitebitdepth, flags);
				}
			}
		}
		else
		{
			// No more work to do
			return;
		}
	}
}


void Row16uUncompressed2OutputFormat(DECODER *decoder, FRAME_INFO *info, int thread_index,
							 uint8_t *output, int output_pitch, uint8_t *scratch, int scratch_size,
							 int threading)
{

	THREAD_ERROR error = THREAD_ERROR_OKAY;
	//int channel;

	uint16_t *scanline;
	uint16_t *sptr;
	uint16_t *scanline2;
	//unsigned short *sptr2;
    char *buffer = (char *)scratch;
	//size_t buffer_size = scratch_size;

	int y=0;
	//int color_space = decoder->frame.colorspace;

	scanline = (uint16_t *)buffer;

	scanline2 = scanline;
	scanline2+= info->width*6;

	for (;;)
	{
		if(threading)
		{
			int work_index;
			// Wait for one row from each channel to process
			error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);
			if (error != THREAD_ERROR_OKAY)
				return; // No more work to do

			y = work_index;
		}

		if(y<info->height)
		{
			//int flags = 0;
			int width = decoder->frame.width;
			int height = decoder->frame.height;
			int format = decoder->frame.format;
			int resolution = decoder->frame.resolution;
			uint8_t *src = (uint8_t *)decoder->uncompressed_chunk;
			uint8_t *dst = output;

			{
				int orig_width = width;
				int orig_height = height;
				int row,lines = 1;
				int start,end;
				int unc_Stride;

				if(resolution == DECODED_RESOLUTION_HALF)
				{
					orig_width *= 2;
					orig_height *= 2;
					lines = 2;
				}
				if(resolution == DECODED_RESOLUTION_QUARTER)
				{
					orig_width *= 4;
					orig_height *= 4;
					lines = 4;
				}

				unc_Stride = decoder->uncompressed_size / orig_height;

				start = 0;
				end = height;
				if(decoder->image_dev_only)
				{
					if(decoder->frame.output_format == DECODED_FORMAT_RGB32 || decoder->frame.output_format == DECODED_FORMAT_RGB24)
					{
						src += unc_Stride * (height*lines-1);
						unc_Stride = -unc_Stride;
					}
				}
				else
				{
					if(format == DECODED_FORMAT_RGB32 || format == DECODED_FORMAT_RGB24)
					{			
						src += unc_Stride * (height*lines-1);
						unc_Stride = -unc_Stride;
					}
				}
				src += unc_Stride * y*lines;
				dst += output_pitch * y;

				row = y;

				if(decoder->codec.encoded_format == ENCODED_FORMAT_YUV_422) //YUY2
				{
					int whitebitdepth = 16;
					int flags = 0;
					uint8_t *planar_output[3];
					int planar_pitch[3];
					ROI roi;
					PIXEL16U *y_row_ptr;
					PIXEL16U *u_row_ptr;
					PIXEL16U *v_row_ptr;
					int i;

					y_row_ptr = (PIXEL16U *)scanline;
					u_row_ptr = y_row_ptr + orig_width;
					v_row_ptr = u_row_ptr + orig_width/2;
					for(i=0; i<lines; i++)
					{
						// Repack the row of 10-bit pixels into 16-bit pixels
						ConvertV210RowToYUV16((uint8_t *)src, y_row_ptr, u_row_ptr, v_row_ptr, orig_width, (uint8_t *)scanline2);

						// Advance to the next rows in the input and output images
						src += unc_Stride;
						y_row_ptr += orig_width*2;
						u_row_ptr = y_row_ptr + orig_width;
						v_row_ptr = u_row_ptr + orig_width/2;

					}


					y_row_ptr = (PIXEL16U *)scanline;
					u_row_ptr = y_row_ptr + width;
					v_row_ptr = u_row_ptr + width/2;
					if(lines == 2)
					{
						for(i=0; i<width*2;i++)
							y_row_ptr[i] = (y_row_ptr[i*2] + y_row_ptr[i*2+1] + y_row_ptr[orig_width*2+i*2] + y_row_ptr[orig_width*2+i*2+1]) >> 2;	
						
					}
					else if(lines == 4)
					{
						for(i=0; i<width*2;i++)
							y_row_ptr[i] = (y_row_ptr[i*4] + y_row_ptr[i*4+2] + y_row_ptr[orig_width*2*2+i*4] + y_row_ptr[orig_width*2*2+i*4+2]) >> 2;
					}

					
					roi.width = width;
					roi.height = 1;			

					planar_output[0] = (uint8_t *)y_row_ptr;
					planar_output[1] = (uint8_t *)v_row_ptr;
					planar_output[2] = (uint8_t *)u_row_ptr;
					planar_pitch[0] = 0;
					planar_pitch[1] = 0;
					planar_pitch[2] = 0;
					
					if(decoder->apply_color_active_metadata)
					{
						int colorspace = decoder->frame.colorspace & (8|3); // VSRGB is done in cube
						ConvertYUVRow16uToBGRA64(planar_output, planar_pitch, roi,
							(unsigned char *)scanline2, width, output_pitch,
							COLOR_FORMAT_RGB_8PIXEL_PLANAR, colorspace, &whitebitdepth, &flags);
						sptr = scanline2;

						sptr = ApplyActiveMetaData(decoder, width, 1, row,
								(uint32_t *)scanline2, (uint32_t *)scanline,
								info->format, &whitebitdepth, &flags);

						if(decoder->frame.colorspace & COLOR_SPACE_VS_RGB)
						{
							ConvertCGRGBtoVSRGB((PIXEL *)sptr, width, whitebitdepth, flags);
						}
					}
					else
					{
						ConvertYUVRow16uToBGRA64(planar_output, planar_pitch, roi,
							(unsigned char *)scanline2, width, output_pitch,
							COLOR_FORMAT_WP13, decoder->frame.colorspace, &whitebitdepth, &flags);
						sptr = scanline2;					
					}

					ConvertLinesToOutput(decoder, width, 1, row, sptr,
						dst, output_pitch, format, whitebitdepth, flags);

					dst += output_pitch;
				}
				else if(decoder->codec.encoded_format == ENCODED_FORMAT_RGB_444)//444
				{
					int whitebitdepth = 16;
					int flags = 0;
					ROI roi;
					unsigned short *sptr;
					int i;

					whitebitdepth = 13;
					flags = ACTIVEMETADATA_SRC_8PIXEL_PLANAR;
				
					roi.width = width;
					roi.height = 1;			

					if(lines == 1)
					{
						uint32_t j;
						uint32_t *lptr = (uint32_t *)src;
						uint16_t *sptr = (uint16_t *)src;
						uint8_t *bptr = (uint8_t *)src;
						PIXEL16U *ptr = (PIXEL16U *)scanline;
			
						for(i=0; i<width;i+=8)
						{
							int val,r,g,b;
							if(decoder->image_dev_only) // HACK, currently assuming RG48 input data.
							{
								switch(decoder->frame.output_format & 0x7fffffff)
								{
								case COLOR_FORMAT_RGB24:
									for(j=0; j<8; j++)
									{
										ptr[j] = bptr[2] << 5;
										ptr[j+8] = bptr[1] << 5;
										ptr[j+16] = bptr[0] << 5;

										bptr += 3;
									}
									break;
								case COLOR_FORMAT_RGB32:
								case COLOR_FORMAT_BGRA:
									for(j=0; j<8; j++)
									{
										ptr[j] = bptr[2] << 5;
										ptr[j+8] = bptr[1] << 5;
										ptr[j+16] = bptr[0] << 5;

										bptr += 4;
									}
									break;
								case COLOR_FORMAT_WP13:	
									for(j=0; j<8; j++)
									{
										ptr[j] = sptr[0];
										ptr[j+8] = sptr[1];
										ptr[j+16] = sptr[2];

										sptr += 3;
									}
									break;
								default:
								case COLOR_FORMAT_RG48:	
									for(j=0; j<8; j++)
									{
										ptr[j] = sptr[0] >> 3;
										ptr[j+8] = sptr[1] >> 3;
										ptr[j+16] = sptr[2] >> 3;

										sptr += 3;
									}
								}
							}
							else
							{
								for(j=0; j<8; j++)
								{
									val = SwapInt32(*lptr++);
									val >>= 2;
									b = (val & 0x3ff) << 3;
									val >>= 10;
									g = (val & 0x3ff) << 3;
									val >>= 10;
									r = (val & 0x3ff) << 3;

									ptr[j] = r;
									ptr[j+8] = g;
									ptr[j+16] = b;
								}
							}
							ptr += 24;
						}
					}
					else if(lines == 2)
					{
						uint32_t j;
						uint32_t *lptr = (uint32_t *)src;
						PIXEL16U *ptr = (PIXEL16U *)scanline;
			
						for(i=0; i<width;i+=8)
						{
							int val,r,g,b;
							for(j=0; j<8; j++)
							{
								val = SwapInt32(lptr[0]);
								val >>= 2;
								b = (val & 0x3ff) << 3;
								val >>= 10;
								g = (val & 0x3ff) << 3;
								val >>= 10;
								r = (val & 0x3ff) << 3;

								val = SwapInt32(lptr[1]);
								val >>= 2;
								b += (val & 0x3ff) << 3;
								val >>= 10;
								g += (val & 0x3ff) << 3;
								val >>= 10;
								r += (val & 0x3ff) << 3;

								val = SwapInt32(lptr[unc_Stride>>2]);
								val >>= 2;
								b += (val & 0x3ff) << 3;
								val >>= 10;
								g += (val & 0x3ff) << 3;
								val >>= 10;
								r += (val & 0x3ff) << 3;

								val = SwapInt32(lptr[(unc_Stride>>2)+1]);
								val >>= 2;
								b += (val & 0x3ff) << 3;
								val >>= 10;
								g += (val & 0x3ff) << 3;
								val >>= 10;
								r += (val & 0x3ff) << 3;

								ptr[j] = r>>2;
								ptr[j+8] = g>>2;
								ptr[j+16] = b>>2;

								lptr += lines;
							}
							ptr += 24;
						}
					}
					else if(lines == 4)
					{
						uint32_t j;
						uint32_t *lptr = (uint32_t *)src;
						PIXEL16U *ptr = (PIXEL16U *)scanline;
			
						for(i=0; i<width;i+=8)
						{
							int val,r,g,b;
							for(j=0; j<8; j++)
							{
								val = SwapInt32(lptr[0]);
								val >>= 2;
								b = (val & 0x3ff) << 3;
								val >>= 10;
								g = (val & 0x3ff) << 3;
								val >>= 10;
								r = (val & 0x3ff) << 3;

								val = SwapInt32(lptr[2]);
								val >>= 2;
								b += (val & 0x3ff) << 3;
								val >>= 10;
								g += (val & 0x3ff) << 3;
								val >>= 10;
								r += (val & 0x3ff) << 3;

								val = SwapInt32(lptr[unc_Stride>>1]);
								val >>= 2;
								b += (val & 0x3ff) << 3;
								val >>= 10;
								g += (val & 0x3ff) << 3;
								val >>= 10;
								r += (val & 0x3ff) << 3;

								val = SwapInt32(lptr[(unc_Stride>>1)+2]);
								val >>= 2;
								b += (val & 0x3ff) << 3;
								val >>= 10;
								g += (val & 0x3ff) << 3;
								val >>= 10;
								r += (val & 0x3ff) << 3;

								ptr[j] = r>>2;
								ptr[j+8] = g>>2;
								ptr[j+16] = b>>2;

								lptr += lines;
							}
							ptr += 24;
						}
					}

					sptr = scanline;
					if(decoder->apply_color_active_metadata)
						sptr = ApplyActiveMetaData(decoder, width, 1, row,
								(uint32_t *)scanline, (uint32_t *)scanline2,
								info->format, &whitebitdepth, &flags);

					ConvertLinesToOutput(decoder, width, 1, row, sptr,
						dst, output_pitch, format, whitebitdepth, flags);

					dst += output_pitch;
				}
			}
		}
		else
		{
			// No more work to do
			return;
		}
	}
}



void Row16uFull2OutputFormat(DECODER *decoder, FRAME_INFO *info, int thread_index,
							 uint8_t *output, int pitch, uint8_t *scratch, int scratch_size,
							 int threading)
{

	THREAD_ERROR error = THREAD_ERROR_OKAY;
	//int channel;

	uint16_t *scanline;
	uint16_t *sptr;
	uint16_t *scanline2;
	//unsigned short *sptr2;
    char *buffer = (char *)scratch;
	//size_t buffer_size = scratch_size;

	//uint8_t *outyuv;
	uint8_t *line = output;
	//int x;
	int y=0;
	int color_space = decoder->frame.colorspace;
	int need4444 = (decoder->codec.encoded_format == ENCODED_FORMAT_RGBA_4444 && ALPHAOUTPUT(info->format));

	scanline = (uint16_t *)buffer;

	scanline2 = scanline;
	if(need4444)
		scanline2+= info->width*8;
	else
		scanline2+= info->width*6;

	for (;;)
	{
		if(threading)
		{
			int work_index;
			// Wait for one row from each channel to process
			error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);
			if (error != THREAD_ERROR_OKAY)
				return; // No more work to do

			y = work_index;
		}

		if(y<info->height)
		{
			uint8_t *newline = line;
			int flags = 0;

			newline += pitch*y;

			//memcpy(scanline, newline, info->width*4);

			switch (decoder->codec.encoded_format)
			{	
			case ENCODED_FORMAT_RGBA_4444:		// Four plane with alpha
				if(ALPHAOUTPUT(info->format))
				{
					// already 444 RGB
					if(decoder->apply_color_active_metadata)
					{
						flags = (ACTIVEMETADATA_PRESATURATED|ACTIVEMETADATA_PLANAR);
						{
							int whitebitdepth = 16;
							unsigned short *sptr;
							unsigned short *src;

							src = decoder->RGBFilterBuffer16;
							src += info->width*4*y;

							if(decoder->frame.generate_look)
							{
								DrawBlankLUT(src,info->width,y,1);
								flags = ACTIVEMETADATA_PRESATURATED;
							}

							sptr = ApplyActiveMetaData4444(decoder, info->width, 1, y,
									(uint32_t *)src, (uint32_t *)scanline,
									info->format, &whitebitdepth, &flags);

							Convert4444LinesToOutput(decoder, info->width, 1, y, sptr,
									newline, pitch, info->format, whitebitdepth, flags);
						}
					}
					else
					{
						flags = (ACTIVEMETADATA_PRESATURATED|ACTIVEMETADATA_PLANAR);
						{
							int whitebitdepth = 16;
							unsigned short *src;

							src = decoder->RGBFilterBuffer16;
							src += info->width*4*y;

							if(decoder->frame.generate_look)
							{
								DrawBlankLUT(scanline,info->width,y,1);
								flags = ACTIVEMETADATA_PRESATURATED;
							}
							else
							{
								if(decoder->RGBFilterBufferPhase == 1) //GRB
									ConvertPlanarGRBAToPlanarRGBA((PIXEL *)scanline, (PIXEL *)src, info->width);
								else
									memcpy(scanline, src, info->width*4*2);
							}

							Convert4444LinesToOutput(decoder, info->width, 1, y, scanline,
								newline, pitch, info->format, whitebitdepth, flags);
						}
					}
					break;
				}
			case ENCODED_FORMAT_RGB_444:		// Three planes of RGB 4:4:4
				{
					// already 444 RGB
					if(decoder->apply_color_active_metadata)
					{
						flags = (ACTIVEMETADATA_PRESATURATED|ACTIVEMETADATA_PLANAR);
						{
							int whitebitdepth = 16;
							unsigned short *sptr;
							unsigned short *src;

							src = decoder->RGBFilterBuffer16;
							src += info->width*3*y;

							if(decoder->frame.generate_look)
							{
								DrawBlankLUT(src,info->width,y,1);
								flags = ACTIVEMETADATA_PRESATURATED;
							}

							sptr = ApplyActiveMetaData(decoder, info->width, 1, y,
									(uint32_t *)src, (uint32_t *)scanline,
									info->format, &whitebitdepth, &flags);

							ConvertLinesToOutput(decoder, info->width, 1, y, sptr,
									newline, pitch, info->format, whitebitdepth, flags);
						}
					}
					else
					{
						flags = (ACTIVEMETADATA_PRESATURATED|ACTIVEMETADATA_PLANAR);
						{
							int whitebitdepth = 16;
							unsigned short *src;

							src = decoder->RGBFilterBuffer16;
							src += info->width*3*y;

							if(decoder->frame.generate_look)
							{
								DrawBlankLUT(scanline,info->width,y,1);
								flags = ACTIVEMETADATA_PRESATURATED;
							}
							else
							{
								if(decoder->RGBFilterBufferPhase == 1) //GRB
									ConvertPlanarGRBToPlanarRGB((PIXEL *)scanline, (PIXEL *)src, info->width);
								else
									memcpy(scanline, src, info->width*3*2);
							}

							ConvertLinesToOutput(decoder, info->width, 1, y, scanline,
								newline, pitch, info->format, whitebitdepth, flags);
						}
					}
				}
				break;

			case ENCODED_FORMAT_YUV_422:		// Original encoding scheme for YUV 4:2:2
				{
					uint8_t *planar_output[3];
					int planar_pitch[3];
					int whitebitdepth = 16;
					ROI roi;
					roi.width = info->width;
					roi.height = 1;

					if(decoder->RGBFilterBufferPhase == 2) // YUV in a buffer
					{
						planar_output[0] = (uint8_t *)decoder->RGBFilterBuffer16;
						planar_output[1] = planar_output[0] + info->width*2;
						planar_output[2] = planar_output[0] + info->width*3;

						planar_output[0] += info->width*4*y;
						planar_output[1] += info->width*4*y;
						planar_output[2] += info->width*4*y;

						planar_pitch[0] = 0;
						planar_pitch[1] = 0;
						planar_pitch[2] = 0;
					}
					else
					{
						planar_output[0] = newline;
						planar_output[1] = newline + info->width*2;
						planar_output[2] = newline + info->width*3;
						planar_pitch[0] = 0;
						planar_pitch[1] = 0;
						planar_pitch[2] = 0;
					}

					if(LUTYUV(info->format) && decoder->use_active_metadata_decoder == false) // Output to YUV, therefore convert 422 to 444YUV
					{
						if(info->format == COLOR_FORMAT_V210 || info->format == COLOR_FORMAT_YU64)
						{
							ROI newroi;
							newroi.width = info->width;
							newroi.height = 1;

							memcpy(scanline, newline, info->width*2*2);
							planar_output[0] = (uint8_t *)scanline;
							planar_output[1] = planar_output[0] + info->width*2;
							planar_output[2] = planar_output[0] + info->width*3;

							ConvertYUVStripPlanarToV210((PIXEL **)planar_output, planar_pitch, newroi,
								newline, pitch, info->width, info->format, info->colorspace, 16);
						}
						else
						{
							if(decoder->frame.generate_look)
							{
								DrawBlankLUT(scanline,info->width,y,1);
								flags = ACTIVEMETADATA_PRESATURATED;
							}
							else
							{
								ConvertYUVRow16uToYUV444(planar_output, planar_pitch, roi,
								(uint8_t *)scanline, info->width, pitch, COLOR_FORMAT_RGB_8PIXEL_PLANAR);

								flags = (ACTIVEMETADATA_PRESATURATED|
									ACTIVEMETADATA_SRC_8PIXEL_PLANAR|
									ACTIVEMETADATA_COLORFORMATDONE);
							}
							sptr = scanline;

							ConvertLinesToOutput(decoder, info->width, 1, y, sptr,
								newline, pitch, info->format, whitebitdepth, flags);
						}
					}
					else // convert to 444 RGB
					{


						if(decoder->frame.generate_look)
						{
							DrawBlankLUT(scanline,info->width,y,1);
							flags = ACTIVEMETADATA_PRESATURATED;
						}
						else
						{
							if(decoder->apply_color_active_metadata)
							{
								int colorspace = color_space & (8|3); // VSRGB is done in cube
								ConvertYUVRow16uToBGRA64(planar_output, planar_pitch, roi,
									(unsigned char *)scanline, info->width, pitch,
									COLOR_FORMAT_RGB_8PIXEL_PLANAR, colorspace, &whitebitdepth, &flags);

								sptr = ApplyActiveMetaData(decoder, info->width, 1, y,
										(uint32_t *)scanline, (uint32_t *)scanline2,
										info->format, &whitebitdepth, &flags);
								
								if(color_space & COLOR_SPACE_VS_RGB)
								{
									ConvertCGRGBtoVSRGB((PIXEL *)sptr, roi.width, whitebitdepth, flags);
								}
							}
							else
							{
								ChannelYUYV16toPlanarYUV16((unsigned short **)planar_output, scanline, info->width, color_space);
								PlanarYUV16toPlanarRGB16(scanline, scanline2, info->width, color_space|COLOR_SPACE_8_PIXEL_PLANAR);
								sptr = scanline2;
								flags = COLOR_FORMAT_RGB_8PIXEL_PLANAR;
								whitebitdepth = 16;
							}
						}

						ConvertLinesToOutput(decoder, info->width, 1, y, sptr,
							newline, pitch, info->format, whitebitdepth, flags);
					}
				}
				break;

			default:
				assert(0);
				break;
			}

			y++;
		}
		else
		{
			// No more work to do
			return;
		}
	}
}



void Row16uHalf2OutputFormat(DECODER *decoder, FRAME_INFO *info, int thread_index, uint8_t *output, int pitch, int frame, uint8_t *scratch, int scratchsize, int threading)
{
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int channel;

	unsigned short *scanline,*sptr;
	unsigned short *scanline2;
    char *buffer = (char *)scratch;
	//size_t buffer_size = scratchsize;

	uint8_t *line = output;
	int x,y=0;
	int color_space = decoder->frame.colorspace;

	TRANSFORM **transform_array = decoder->transform;
	IMAGE *wavelet_array[TRANSFORM_MAX_CHANNELS];
	CODEC_STATE *codec = &decoder->codec;
	int num_channels = codec->num_channels;

	for (channel = 0; channel < num_channels; channel++)
	{
		wavelet_array[channel] = transform_array[channel]->wavelet[frame];
	}

	scanline = (unsigned short *)buffer;
	scanline2 = scanline;
	scanline2+= info->width*4;


	for (;;)
	{
		if(threading)
		{
			int work_index;
			// Wait for one row from each channel to process
			error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);
			if (error != THREAD_ERROR_OKAY)
				return; // No more work to do

			y = work_index;
		}

		if(y<info->height)
		{
			uint8_t *newline = line;
			int flags = 0;

			newline += pitch*y;

			//memcpy(scanline, newline, info->width*4);

			switch (decoder->codec.encoded_format)
			{
				
			case ENCODED_FORMAT_RGBA_4444:		// Four plane with alpha
				if(ALPHAOUTPUT(info->format))
				{
					// already 444 RGB
					{
						flags = (ACTIVEMETADATA_PRESATURATED|ACTIVEMETADATA_PLANAR);
						{
							int whitebitdepth = 16;
							unsigned short *sptr;
							unsigned short *src = scanline2;

							short *gptr = wavelet_array[0]->band[0];
							short *rptr = wavelet_array[1]->band[0];
							short *bptr = wavelet_array[2]->band[0];
							short *aptr = wavelet_array[3]->band[0];

							gptr += (wavelet_array[0]->pitch>>1) * y;//(info->height-1-y);
							rptr += (wavelet_array[1]->pitch>>1) * y;//(info->height-1-y);
							bptr += (wavelet_array[2]->pitch>>1) * y;//(info->height-1-y);
							aptr += (wavelet_array[3]->pitch>>1) * y;//(info->height-1-y);

#if 1
							{
								__m128i *g = (__m128i *)gptr;
								__m128i *r = (__m128i *)rptr;
								__m128i *b = (__m128i *)bptr;
								__m128i *a = (__m128i *)aptr;
								__m128i *d = (__m128i *)src;
								__m128i v;

								__m128i rgb_limit_epi16 = _mm_set1_epi16(0x7fff - 0x3fff);

								for(x=0;x<info->width;x+=8)
								{
									v = _mm_load_si128(g++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_store_si128(d++, v);
								}
								for(x=0;x<info->width;x+=8)
								{
									v = _mm_load_si128(r++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_store_si128(d++, v);
								}
								for(x=0;x<info->width;x+=8)
								{
									v = _mm_load_si128(b++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_store_si128(d++, v);
								}
								for(x=0;x<info->width;x+=8)
								{
									v = _mm_load_si128(a++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_store_si128(d++, v);
								}
							}
#else
							//non SSE2
							for(x=0;x<info->width;x++)
							{
								int val = *gptr++;
								if(val < 0) val = 0;
								if(val > 16383) val = 16383;
								val <<= 2;
								*src++ = val;
							}
							for(x=0;x<info->width;x++)
							{
								int val = *rptr++;
								if(val < 0) val = 0;
								if(val > 16383) val = 16383;
								val <<= 2;
								*src++ = val;
							}
							for(x=0;x<info->width;x++)
							{
								int val = *bptr++;
								if(val < 0) val = 0;
								if(val > 16383) val = 16383;
								val <<= 2;
								*src++ = val;
							}
							src = scanline2;
#endif
							if(decoder->apply_color_active_metadata)
							{
								sptr = ApplyActiveMetaData4444(decoder, info->width, 1, y,
										(uint32_t *)src, (uint32_t *)scanline,
										info->format, &whitebitdepth, &flags);
							}
							else
							{
								if(decoder->RGBFilterBufferPhase == 1) //GRB
								{
									ConvertPlanarGRBAToPlanarRGBA((PIXEL *)scanline, (PIXEL *)src, info->width);
								}
								else
								{
									memcpy(scanline, src, info->width*4*2);
								}
								sptr = scanline;
							}

							Convert4444LinesToOutput(decoder, info->width, 1, y, sptr,
								newline, pitch, info->format, whitebitdepth, flags);
						}
					}
					break;
				}
			case ENCODED_FORMAT_RGB_444:		// Three planes of RGB 4:4:4
				{
					// already 444 RGB
					{
						flags = (ACTIVEMETADATA_PRESATURATED|ACTIVEMETADATA_PLANAR);
						{
							int whitebitdepth = 16;
							unsigned short *sptr;
							unsigned short *src = scanline2;

							short *gptr = wavelet_array[0]->band[0];
							short *rptr = wavelet_array[1]->band[0];
							short *bptr = wavelet_array[2]->band[0];

							gptr += (wavelet_array[0]->pitch>>1) * y;//(info->height-1-y);
							rptr += (wavelet_array[1]->pitch>>1) * y;//(info->height-1-y);
							bptr += (wavelet_array[2]->pitch>>1) * y;//(info->height-1-y);

#if 1
							{
								__m128i *g = (__m128i *)gptr;
								__m128i *r = (__m128i *)rptr;
								__m128i *b = (__m128i *)bptr;
								__m128i *d = (__m128i *)src;
								__m128i v;

								__m128i rgb_limit_epi16 = _mm_set1_epi16(0x7fff - 0x3fff);

								for(x=0;x<info->width;x+=8)
								{
									v = _mm_load_si128(g++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_store_si128(d++, v);
								}
								for(x=0;x<info->width;x+=8)
								{
									v = _mm_load_si128(r++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_store_si128(d++, v);
								}
								for(x=0;x<info->width;x+=8)
								{
									v = _mm_load_si128(b++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_store_si128(d++, v);
								}
							}
#else
							//non SSE2
							for(x=0;x<info->width;x++)
							{
								int val = *gptr++;
								if(val < 0) val = 0;
								if(val > 16383) val = 16383;
								val <<= 2;
								*src++ = val;
							}
							for(x=0;x<info->width;x++)
							{
								int val = *rptr++;
								if(val < 0) val = 0;
								if(val > 16383) val = 16383;
								val <<= 2;
								*src++ = val;
							}
							for(x=0;x<info->width;x++)
							{
								int val = *bptr++;
								if(val < 0) val = 0;
								if(val > 16383) val = 16383;
								val <<= 2;
								*src++ = val;
							}
							src = scanline2;
#endif
							if(decoder->apply_color_active_metadata)
							{
								sptr = ApplyActiveMetaData(decoder, info->width, 1, y,
									(uint32_t *)src, (uint32_t *)scanline,
									info->format, &whitebitdepth, &flags);
							}
							else
							{
								if(decoder->RGBFilterBufferPhase == 1) //GRB
								{
									ConvertPlanarGRBToPlanarRGB((PIXEL *)scanline, (PIXEL *)src, info->width);
									sptr = scanline;
								}
								else
								{
									sptr = src;
								}
							}

							ConvertLinesToOutput(decoder, info->width, 1, y, sptr,
								newline, pitch, info->format, whitebitdepth, flags);
						}
					}
				}
				break;

			case ENCODED_FORMAT_YUV_422:		// Original encoding scheme for YUV 4:2:2
				{
					uint8_t *planar_output[3];
					int planar_pitch[3];
					short *yptr = wavelet_array[0]->band[0];
					short *uptr = wavelet_array[1]->band[0];
					short *vptr = wavelet_array[2]->band[0];
					unsigned short *src = scanline2;
					__m128i limit_epi16 = _mm_set1_epi16(0x7fff - 0x0fff);
					int shift = 4;
					int limit = 4095;
					ROI roi;
					roi.width = info->width;
					roi.height = 1;

					if(decoder->codec.precision == 8) // old YUV
					{
						shift = 6;
						limit_epi16 = _mm_set1_epi16(0x7fff - 0x03ff);
						limit = 1023;
					}

					yptr += (wavelet_array[0]->pitch>>1) * y;//(info->height-1-y);
					uptr += (wavelet_array[1]->pitch>>1) * y;//(info->height-1-y);
					vptr += (wavelet_array[2]->pitch>>1) * y;//(info->height-1-y);

#if 1
					{
						__m128i *g = (__m128i *)yptr;
						__m128i *r = (__m128i *)uptr;
						__m128i *b = (__m128i *)vptr;
						__m128i *d = (__m128i *)src;
						__m128i v;

						int widthY = info->width;
						int widthY8 = info->width & ~7;
						int widthC = info->width/2;
						int widthC8 = widthC & ~7;
						unsigned short *sptr;

						for(x=0;x<widthY8;x+=8)
						{
							v = _mm_load_si128(g++);
							v = _mm_adds_epi16(v, limit_epi16);
							v = _mm_subs_epu16(v, limit_epi16);
							v = _mm_slli_epi16(v, shift);
							_mm_storeu_si128(d++, v);
						}
						yptr = (short *)g;
						sptr = (unsigned short *)d;
						for(;x<widthY;x++)
						{
							int val = *yptr++;
							if(val < 0) val = 0;
							if(val > limit) val = limit;
							val <<= shift;
							*sptr++ = val;
						}
						d = (__m128i *)sptr;
						for(x=0;x<widthC8;x+=8)
						{
							v = _mm_load_si128(r++);
							v = _mm_adds_epi16(v, limit_epi16);
							v = _mm_subs_epu16(v, limit_epi16);
							v = _mm_slli_epi16(v, shift);
							_mm_storeu_si128(d++, v);
						}
						uptr = (short *)r;
						sptr = (unsigned short *)d;
						for(;x<widthC;x++)
						{
							int val = *uptr++;
							if(val < 0) val = 0;
							if(val > limit) val = limit;
							val <<= shift;
							*sptr++ = val;
						}
						d = (__m128i *)sptr;
						for(x=0;x<widthC8;x+=8)
						{
							v = _mm_load_si128(b++);
							v = _mm_adds_epi16(v, limit_epi16);
							v = _mm_subs_epu16(v, limit_epi16);
							v = _mm_slli_epi16(v, shift);
							_mm_storeu_si128(d++, v);
						}
						vptr = (short *)b;
						sptr = (unsigned short *)d;
						for(;x<widthC;x++)
						{
							int val = *vptr++;
							if(val < 0) val = 0;
							if(val > limit) val = limit;
							val <<= shift;
							*sptr++ = val;
						}
					}
#else
					//non SSE2
					for(x=0;x<info->width;x++)
					{
						int val = *yptr++;
						if(val < 0) val = 0;
						if(val > limit) val = limit;
						val <<= shift;
						*src++ = val;
					}
					for(x=0;x<info->width/2;x++)
					{
						int val = *uptr++;
						if(val < 0) val = 0;
						if(val > limit) val = limit;
						val <<= shift;
						*src++ = val;
					}
					for(x=0;x<info->width/2;x++)
					{
						int val = *vptr++;
						if(val < 0) val = 0;
						if(val > limit) val = limit;
						val <<= shift;
						*src++ = val;
					}
					src = scanline2;
#endif


					planar_output[0] = (uint8_t *)src;
					planar_output[1] = (uint8_t *)(src + info->width);
					planar_output[2] = (uint8_t *)(src + info->width*3/2);
					planar_pitch[0] = 0;
					planar_pitch[1] = 0;
					planar_pitch[2] = 0;

					if(!decoder->apply_color_active_metadata)
					{
						int whitebitdepth = 16;
						int flags = ACTIVEMETADATA_SRC_8PIXEL_PLANAR;
						ConvertYUVRow16uToBGRA64(planar_output, planar_pitch, roi,
							(unsigned char *)scanline, info->width, pitch,
							COLOR_FORMAT_RGB_8PIXEL_PLANAR, color_space, &whitebitdepth, &flags);

						ConvertLinesToOutput(decoder, info->width, 1, y, scanline,
							newline, pitch, info->format, whitebitdepth, flags);
					}
					else
					{
						//unsigned short *src = scanline2;
						int whitebitdepth = 16;

						{
							int targetformat = COLOR_FORMAT_RGB_8PIXEL_PLANAR;
							int flags = ACTIVEMETADATA_SRC_8PIXEL_PLANAR;
							int colorspace = color_space & (8|3); // VSRGB is done in cube

							if(info->width / 16 * 16 != info->width)
							{
								targetformat = COLOR_FORMAT_WP13;
								whitebitdepth = 13;
								flags = 0;
							}
							ConvertYUVRow16uToBGRA64(planar_output, planar_pitch, roi,
								(unsigned char *)scanline, info->width, pitch,
								targetformat, colorspace, &whitebitdepth, &flags);

							sptr = ApplyActiveMetaData(decoder, info->width, 1, y,
									(uint32_t *)scanline, (uint32_t *)scanline2,
									info->format, &whitebitdepth, &flags);

							if(color_space & COLOR_SPACE_VS_RGB)
							{
								ConvertCGRGBtoVSRGB((PIXEL *)sptr, info->width, whitebitdepth, flags);
							}

							ConvertLinesToOutput(decoder, info->width, 1, y, sptr,
								newline, pitch, info->format, whitebitdepth, flags);
						}
					}
				}
				break;
			}

			y++;
		}
		else
		{
			// No more work to do
			return;
		}
	}
}

void Row16uQuarter2OutputFormat(DECODER *decoder, FRAME_INFO *info, int thread_index,
	uint8_t *output, int pitch, int frame, uint8_t *scratch, size_t scratchsize, int threading,
	uint8_t *channeldata[TRANSFORM_MAX_CHANNELS], // used in quarter res decodes
	int channelpitch[TRANSFORM_MAX_CHANNELS]) // used in quarter res decodes)
{

	THREAD_ERROR error = THREAD_ERROR_OKAY;
	//int channel;

	unsigned short *scanline;
	unsigned short *sptr;
	unsigned short *scanline2;
	//unsigned short *sptr2;
    char *buffer = (char *)scratch;
	//size_t buffer_size = scratchsize;

	//uint8_t *outyuv;
	uint8_t *line = output;
	int x,y=0;
	int color_space = decoder->frame.colorspace;
	//CODEC_STATE *codec = &decoder->codec;
	//int num_channels = codec->num_channels;

	scanline = (unsigned short *)buffer;
	scanline2 = scanline;
	scanline2+= info->width*4;


	for (;;)
	{
		if(threading)
		{
			int work_index;
			// Wait for one row from each channel to process
			error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);
			if (error != THREAD_ERROR_OKAY)
				return; // No more work to do

			y = work_index;
		}

		if(y<info->height)
		{
			uint8_t *newline = line;
			int flags = 0;

			newline += pitch*y;

			//memcpy(scanline, newline, info->width*4);

			switch (decoder->codec.encoded_format)
			{
			case ENCODED_FORMAT_RGBA_4444:		// Four plane
				if(ALPHAOUTPUT(info->format))
				{
					// already 444 RGB
					if(decoder->use_active_metadata_decoder)
					{
						flags = (ACTIVEMETADATA_PRESATURATED|ACTIVEMETADATA_PLANAR);
						{
							int width8 = (info->width>>3)*8;
							int whitebitdepth = 16;
							unsigned short *sptr;
							unsigned short *src = scanline2;

							short *gptr = (short *)channeldata[0];
							short *rptr = (short *)channeldata[1];
							short *bptr = (short *)channeldata[2];
							short *aptr = (short *)channeldata[3];

							gptr += (channelpitch[0]>>1) * y;//(info->height-1-y);
							rptr += (channelpitch[1]>>1) * y;//(info->height-1-y);
							bptr += (channelpitch[2]>>1) * y;//(info->height-1-y);
							aptr += (channelpitch[3]>>1) * y;//(info->height-1-y);

							decoder->RGBFilterBufferPhase = 0;

#if 1
							{
								__m128i *g = (__m128i *)gptr;
								__m128i *r = (__m128i *)rptr;
								__m128i *b = (__m128i *)bptr;
								__m128i *a = (__m128i *)aptr;
								__m128i *d = (__m128i *)src;
								__m128i v;

								__m128i rgb_limit_epi16 = _mm_set1_epi16(0x7fff - 0x3fff);

								for(x=0;x<width8;x+=8)
								{
									v = _mm_load_si128(r++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_storeu_si128(d++, v);
								}
								src = (unsigned short *)d;
								gptr = (short *)g;
								for(;x<info->width;x++)
								{
									int val = *gptr++;
									if(val < 0) val = 0;
									if(val > 16383) val = 16383;
									val <<= 2;
									*src++ = val;
								}

								d = (__m128i *)src;
								for(x=0;x<width8;x+=8)
								{
									v = _mm_load_si128(g++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_storeu_si128(d++, v);
								}
								src = (unsigned short *)d;
								rptr = (short *)r;
								for(;x<info->width;x++)
								{
									int val = *rptr++;
									if(val < 0) val = 0;
									if(val > 16383) val = 16383;
									val <<= 2;
									*src++ = val;
								}

								d = (__m128i *)src;
								for(x=0;x<width8;x+=8)
								{
									v = _mm_load_si128(b++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_storeu_si128(d++, v);
								}
								src = (unsigned short *)d;
								bptr = (short *)b;
								for(;x<info->width;x++)
								{
									int val = *bptr++;
									if(val < 0) val = 0;
									if(val > 16383) val = 16383;
									val <<= 2;
									*src++ = val;
								}
								
								d = (__m128i *)src;
								for(x=0;x<width8;x+=8)
								{
									v = _mm_load_si128(a++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_storeu_si128(d++, v);
								}
								src = (unsigned short *)d;
								aptr = (short *)b;
								for(;x<info->width;x++)
								{
									int val = *aptr++;
									if(val < 0) val = 0;
									if(val > 16383) val = 16383;
									val <<= 2;
									*src++ = val;
								}
							}
#endif
							src = scanline2;
							if(decoder->apply_color_active_metadata)
								sptr = ApplyActiveMetaData4444(decoder, info->width, 1, y,
									(uint32_t *)src, (uint32_t *)scanline,
									info->format, &whitebitdepth, &flags);
							else
								sptr = src;

							Convert4444LinesToOutput(decoder, info->width, 1, y, sptr,
								newline, pitch, info->format, whitebitdepth, flags);
						}
					}
					else
					{
						flags = (ACTIVEMETADATA_PRESATURATED|ACTIVEMETADATA_PLANAR);
						{
							int whitebitdepth = 16;

							//sptr = ApplyActiveMetaData(decoder, info->width, 1, scanline, scanline2,
							//	info->format, &whitebitdepth, &flags);
							memcpy(scanline, newline, info->width*3*2);

							ConvertLinesToOutput(decoder, info->width, 1, y, scanline,
								newline, pitch, info->format, whitebitdepth, flags);
						}
					}
					break;
				}
			case ENCODED_FORMAT_RGB_444:		// Three planes of RGB 4:4:4
				{
					// already 444 RGB
					{
						flags = (ACTIVEMETADATA_PRESATURATED|ACTIVEMETADATA_PLANAR);
						{
							int width8 = (info->width>>3)*8;
							int whitebitdepth = 16;
							unsigned short *sptr;
							unsigned short *src = scanline2;

							short *gptr = (short *)channeldata[0];
							short *rptr = (short *)channeldata[1];
							short *bptr = (short *)channeldata[2];

							gptr += (channelpitch[0]>>1) * y;//(info->height-1-y);
							rptr += (channelpitch[1]>>1) * y;//(info->height-1-y);
							bptr += (channelpitch[2]>>1) * y;//(info->height-1-y);

#if 1
							{
								__m128i *g = (__m128i *)gptr;
								__m128i *r = (__m128i *)rptr;
								__m128i *b = (__m128i *)bptr;
								__m128i *d = (__m128i *)src;
								__m128i v;

								__m128i rgb_limit_epi16 = _mm_set1_epi16(0x7fff - 0x3fff);

								for(x=0;x<width8;x+=8)
								{
									v = _mm_load_si128(g++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_storeu_si128(d++, v);
								}
								src = (unsigned short *)d;
								gptr = (short *)g;
								for(;x<info->width;x++)
								{
									int val = *gptr++;
									if(val < 0) val = 0;
									if(val > 16383) val = 16383;
									val <<= 2;
									*src++ = val;
								}

								d = (__m128i *)src;
								for(x=0;x<width8;x+=8)
								{
									v = _mm_load_si128(r++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_storeu_si128(d++, v);
								}
								src = (unsigned short *)d;
								rptr = (short *)r;
								for(;x<info->width;x++)
								{
									int val = *rptr++;
									if(val < 0) val = 0;
									if(val > 16383) val = 16383;
									val <<= 2;
									*src++ = val;
								}

								d = (__m128i *)src;
								for(x=0;x<width8;x+=8)
								{
									v = _mm_load_si128(b++);
									v = _mm_adds_epi16(v, rgb_limit_epi16);
									v = _mm_subs_epu16(v, rgb_limit_epi16);
									v = _mm_slli_epi16(v, 2);
									_mm_storeu_si128(d++, v);
								}
								src = (unsigned short *)d;
								bptr = (short *)b;
								for(;x<info->width;x++)
								{
									int val = *bptr++;
									if(val < 0) val = 0;
									if(val > 16383) val = 16383;
									val <<= 2;
									*src++ = val;
								}
							}
#endif
							src = scanline2;
							if(decoder->apply_color_active_metadata)
							{
								sptr = ApplyActiveMetaData(decoder, info->width, 1, y,
									(uint32_t *)src, (uint32_t *)scanline,
									info->format, &whitebitdepth, &flags);
							}
							else
							{
								if(decoder->RGBFilterBufferPhase == 1) //GRB
								{
									ConvertPlanarGRBToPlanarRGB((PIXEL *)scanline, (PIXEL *)src, info->width);
									sptr = scanline;
								}
								else
								{
									sptr = src;
								}
							}

							ConvertLinesToOutput(decoder, info->width, 1, y, sptr,
								newline, pitch, info->format, whitebitdepth, flags);
						}
					}
				}
				break;

			case ENCODED_FORMAT_YUV_422:		// Original encoding scheme for YUV 4:2:2
				{
					int whitebitdepth = 16;
					uint8_t *planar_output[3];
					int planar_pitch[3];
					ROI roi;
					roi.width = info->width;
					roi.height = 1;



						/*	short *gptr = (short *)channeldata[0];
							short *rptr = (short *)channeldata[1];
							short *bptr = (short *)channeldata[2];

							gptr += (channelpitch[0]>>0) * y;//(info->height-1-y);
							rptr += (channelpitch[1]>>0) * y;//(info->height-1-y);
							bptr += (channelpitch[2]>>0) * y;//(info->height-1-y);
*/
					planar_output[0] = channeldata[0] + (channelpitch[0]) * y;
					planar_output[1] = channeldata[1] + (channelpitch[1]) * y;
					planar_output[2] = channeldata[2] + (channelpitch[2]) * y;
					planar_pitch[0] = 0;
					planar_pitch[1] = 0;
					planar_pitch[2] = 0;


					{
						unsigned short *src = scanline2;
						unsigned short *chns[3];
						int colorspace = color_space; 

#if 1
						{
							__m128i *Y = (__m128i *)planar_output[0];
							__m128i *U = (__m128i *)planar_output[1];
							__m128i *V = (__m128i *)planar_output[2];
							__m128i *d = (__m128i *)src;
							__m128i v;

							short *yptr, *uptr, *vptr;

							__m128i rgb_limit_epi16 = _mm_set1_epi16(0x7fff - 0x0fff);
							int shift = 4;
							int limit = 4095;

							int widthY = info->width;
							int widthY8 = info->width & ~7;
							int widthC = info->width/2;
							int widthC8 = widthC & ~7;
							chns[0] = (unsigned short *)d;
							for(x=0;x<widthY8;x+=8)
							{
								v = _mm_load_si128(Y++);
								v = _mm_adds_epi16(v, rgb_limit_epi16);
								v = _mm_subs_epu16(v, rgb_limit_epi16);
								v = _mm_slli_epi16(v, 4);
								_mm_storeu_si128(d++, v);
							}
							yptr = (short *)Y;
							sptr = (unsigned short *)d;
							for(;x<widthY;x++)
							{
								int val = *yptr++;
								if(val < 0) val = 0;
								if(val > limit) val = limit;
								val <<= shift;
								*sptr++ = val;
							}
							chns[1] = sptr;
							d = (__m128i *)sptr;
							for(x=0;x<widthC8;x+=8)
							{
								//v = _mm_load_si128(U++);
								v = _mm_loadu_si128(U++);
								v = _mm_adds_epi16(v, rgb_limit_epi16);
								v = _mm_subs_epu16(v, rgb_limit_epi16);
								v = _mm_slli_epi16(v, 4);
								_mm_storeu_si128(d++, v);
							}
							uptr = (short *)U;
							sptr = (unsigned short *)d;
							for(;x<widthC;x++)
							{
								int val = *uptr++;
								if(val < 0) val = 0;
								if(val > limit) val = limit;
								val <<= shift;
								*sptr++ = val;
							}
							chns[2] = sptr;
							d = (__m128i *)sptr;
							for(x=0;x<widthC8;x+=8)
							{
								//v = _mm_load_si128(V++);
								v = _mm_loadu_si128(V++);
								v = _mm_adds_epi16(v, rgb_limit_epi16);
								v = _mm_subs_epu16(v, rgb_limit_epi16);
								v = _mm_slli_epi16(v, 4);
								_mm_storeu_si128(d++, v);
							}
							vptr = (short *)V;
							sptr = (unsigned short *)d;
							for(;x<widthC;x++)
							{
								int val = *vptr++;
								if(val < 0) val = 0;
								if(val > limit) val = limit;
								val <<= shift;
								*sptr++ = val;
							}
						}
#else
						//non SSE2
						for(x=0;x<info->width*2;x++)
						{
							int val = *gptr++;
							if(val < 0) val = 0;
							if(val > 16383) val = 16383;
							val <<= 2;
							*src++ = val;
						}
						src = scanline2;
#endif

						//planar_output[0] = (uint8_t*)src;
						//planar_output[1] = planar_output[0] + info->width*2;
						//planar_output[2] = planar_output[1] + info->width;

						planar_output[0] = (uint8_t*)chns[0];
						planar_output[1] = (uint8_t*)chns[1];
						planar_output[2] = (uint8_t*)chns[2];

						{
							int targetformat = COLOR_FORMAT_RGB_8PIXEL_PLANAR;
							int flags = ACTIVEMETADATA_SRC_8PIXEL_PLANAR;
						
							if(decoder->apply_color_active_metadata)
								colorspace = color_space & (8|3); // VSRGB is done in cube

							if(info->width / 16 * 16 != info->width)
							{
								targetformat = COLOR_FORMAT_WP13;
								whitebitdepth = 13;
								flags = 0;
							}
							ConvertYUVRow16uToBGRA64(planar_output, planar_pitch, roi,
								(unsigned char *)scanline, info->width, pitch,
								targetformat, colorspace, &whitebitdepth, &flags);
						
							//flags = (ACTIVEMETADATA_PRESATURATED|ACTIVEMETADATA_SRC_8PIXEL_PLANAR);	
							sptr = scanline;
							if(decoder->apply_color_active_metadata)
								sptr = ApplyActiveMetaData(decoder, info->width, 1, y,
										(uint32_t *)scanline, (uint32_t *)scanline2,
										info->format, &whitebitdepth, &flags);

							if(decoder->apply_color_active_metadata && color_space & COLOR_SPACE_VS_RGB)
							{
								ConvertCGRGBtoVSRGB((PIXEL *)sptr, info->width, whitebitdepth, flags);
							}

							ConvertLinesToOutput(decoder, info->width, 1, y, sptr,
								newline, pitch, info->format, whitebitdepth, flags);
						}
					}
				}
				break;
			}

			if(!threading)
				y++;
		}
		else
		{
			// No more work to do
			return;
		}
	}
}

//Convert any decompressed planar ROWs of PIXEL16U into most output formats.
void ConvertRow16uToOutput(DECODER *decoder, int frame_index, int num_channels,
						   uint8_t *output, int pitch, FRAME_INFO *info,
						   int chroma_offset, int precision)
{
	// Identify unused parameters to suppress compiler warnings
	(void) frame_index;
	(void) num_channels;
	(void) chroma_offset;
	(void) precision;

#if _THREADED
	{
		WORKER_THREAD_DATA *mailbox = &decoder->worker_thread.data;

	#if _DELAY_THREAD_START
		if(decoder->worker_thread.pool.thread_count == 0)
		{
			CreateLock(&decoder->worker_thread.lock);
			// Initialize the pool of transform worker threads
			ThreadPoolCreate(&decoder->worker_thread.pool,
							decoder->thread_cntrl.capabilities >> 16/*cpus*/,
							WorkerThreadProc,
							decoder);
		}
	#endif
		// Post a message to the mailbox
		mailbox->output = output;
		mailbox->pitch = pitch;
		memcpy(&mailbox->info, info, sizeof(FRAME_INFO));
		mailbox->jobType = JOB_TYPE_OUTPUT;

		// Set the work count to the number of rows to process
		ThreadPoolSetWorkCount(&decoder->worker_thread.pool, info->height);

		// Start the transform worker threads
		ThreadPoolSendMessage(&decoder->worker_thread.pool, THREAD_MESSAGE_START);

		// Wait for all of the worker threads to finish
		ThreadPoolWaitAllDone(&decoder->worker_thread.pool);
	}
#else
	{
        char *buffer = decoder->scratch.free_ptr;
		size_t buffer_size = decoder->scratch.free_size;

		Row16uFull2OutputFormat(decoder, info, 1, output, pitch, buffer, buffer_size, false);
	}
#endif
}


void GenerateBYR2(DECODER *decoder, FRAME_INFO *info, int thread_index, uint8_t *output, int pitch)
{
    int bayer_format = decoder->cfhddata.bayer_format;
	THREAD_ERROR error;
	bool linearRestore = false;
	unsigned short *curve = decoder->BYR4LinearRestore;

	if(curve && decoder->frame.format == DECODED_FORMAT_BYR4 && decoder->cfhddata.encode_curve_preset == 0)
	{
		linearRestore = true;
	}

	for (;;)
	{
		int work_index;
		//int row;

		// Wait for one row from each channel to process
		error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);

		// Is there another row to process?
		if (error == THREAD_ERROR_OKAY)
		{
			// Compute the next row to process from the work index
			uint8_t *line = output;
			PIXEL *bayer_line = (PIXEL *)decoder->RawBayer16;
			PIXEL *bayerptr;
			int bayer_pitch = info->width;
			PIXEL16U *outA16, *outB16;
			//PIXEL16U *line16;
			PIXEL16U *G,*RG,*BG,*GD;
			int x;

			bayer_line += bayer_pitch * 4 * work_index;
			line += pitch * 2 * work_index;

			outA16 = (PIXEL16U *)line;
			line += pitch;
			outB16 = (PIXEL16U *)line;
			line += pitch;

			bayerptr = bayer_line;
			G = (PIXEL16U *)bayerptr;
			RG = G + bayer_pitch;
			BG = RG + bayer_pitch;
			GD = BG + bayer_pitch;
			for(x=0; x<info->width; x++)
			{
				int r,g,b,rg,bg,gd,g1,g2;
				//int y1,y2,u,v,dither;

				g = (*G++);
				rg = (*RG++);
				bg = (*BG++);
				gd = (*GD++) - 32768;

				r = ((rg - 32768)<<1) + g;
				b = ((bg - 32768)<<1) + g;
				g1 = g + gd;
				g2 = g - gd; //TODO:  Is there a DC offset to gd (causes a check in output )

			//	stats1+=g1;
			//	stats2+=g2;
			//	statsd+=gd;

				if(r < 0) r = 0;
				if(g1 < 0) g1 = 0;
				if(g2 < 0) g2 = 0;
				if(b < 0) b = 0;

				if(r > 0xffff) r = 0xffff;
				if(g1 > 0xffff) g1 = 0xffff;
				if(g2 > 0xffff) g2 = 0xffff;
				if(b > 0xffff) b = 0xffff;


				if(linearRestore)
				{
					r = curve[r>>2];
					g1 = curve[g1>>2];
					g2 = curve[g2>>2];
					b = curve[b>>2];
				}
				else
				{
					r &= 0xfffe;
					g1 &= 0xfffe;
					b &= 0xfffe;
					g2 &= 0xfffe;
				}


				switch(bayer_format)
				{
				case BAYER_FORMAT_RED_GRN: //Red-grn phase
					*outA16++ = r;
					*outA16++ = g1;
					*outB16++ = g2;
					*outB16++ = b;
					break;
				case BAYER_FORMAT_GRN_RED:// grn-red
					*outA16++ = g1;
					*outA16++ = r;
					*outB16++ = b;
					*outB16++ = g2;
					break;
				case BAYER_FORMAT_GRN_BLU:
					*outA16++ = g1;
					*outA16++ = b;
					*outB16++ = r;
					*outB16++ = g2;
					break;
				case BAYER_FORMAT_BLU_GRN:
					*outA16++ = b;
					*outA16++ = g1;
					*outB16++ = g2;
					*outB16++ = r;
					break;
				}
			}


#if 0 // never used for RAW outputs, Ripple filter is applied for high quality demosaics.
			if(decoder->flags & DECODER_FLAGS_HIGH_QUALITY)
			{
				if(work_index >=5 && work_index <= info->height)
				{
					y = work_index - 2;

					line = output; //0
					line += pitch * y * 2;

					// If on a red line, move to a blue line
					if(bayer_format == BAYER_FORMAT_GRN_RED || bayer_format == BAYER_FORMAT_RED_GRN)
						line -= pitch;



					{
						int offset = pitch>>1;
						outA16 = (PIXEL16U *)line;

						outA16++; //g //for BAYER_FORMAT_RED_GRN input
						outA16++; //b

						outA16++; //g
						outA16++; //b

						//point to green pixel with *outA16
						if(bayer_format == BAYER_FORMAT_GRN_RED || bayer_format == BAYER_FORMAT_GRN_BLU)
							outA16++;



						for(x=2; x<info->width-2; x++)
						{
							int mn,mx,g;
							int range = 8*256; //1<<11
							int shift = 11;
							int delta;
							int alpha;

							g =  *outA16;

							// lines below do not need to be tested for a corrected value
							mn = mx = outA16[offset+1];
							if(mn > outA16[offset-1]) mn = outA16[offset-1];
							if(mx < outA16[offset-1]) mx = outA16[offset-1];
							if((outA16[-offset-1] & 1)==0)
							{
								if(mn > outA16[-offset-1]) mn = outA16[-offset-1];
								if(mx < outA16[-offset-1]) mx = outA16[-offset-1];
							}
							if((outA16[-offset+1] & 1)==0)
							{
								if(mn > outA16[-offset+1]) mn = outA16[-offset+1];
								if(mx < outA16[-offset+1]) mx = outA16[-offset+1];
							}

							delta = mx - mn;

							if(delta < range && ((mn-range < g && g < mn) || (mx+range > g && g > mx)))
							{
								int gmn,gmx;

								gmn = gmx = g;
								if((outA16[-2*offset-2] & 1)==0)
								{
									if(gmn > outA16[-2*offset-2]) gmn = outA16[-2*offset-2];
									if(gmx < outA16[-2*offset-2]) gmx = outA16[-2*offset-2];
								}
								if((outA16[-2*offset] & 1)==0)
								{
									if(gmn > outA16[-2*offset]) gmn = outA16[-2*offset];
									if(gmx < outA16[-2*offset]) gmx = outA16[-2*offset];
								}
								if((outA16[-2*offset+2] & 1)==0)
								{
									if(gmn > outA16[-2*offset+2]) gmn = outA16[-2*offset+2];
									if(gmx < outA16[-2*offset+2]) gmx = outA16[-2*offset+2];
								}
								if((outA16[-2] & 1)==0)
								{
									if(gmn > outA16[-2]) gmn = outA16[-2];
									if(gmx < outA16[-2]) gmx = outA16[-2];
								}
								// lines below do not need to be tested for a corrected value
								if(gmn > outA16[2*offset-2]) gmn = outA16[2*offset-2];
								if(gmx < outA16[2*offset-2]) gmx = outA16[2*offset-2];
								if(gmn > outA16[2*offset]) gmn = outA16[2*offset];
								if(gmx < outA16[2*offset]) gmx = outA16[2*offset];
								if(gmn > outA16[2*offset+2]) gmn = outA16[2*offset+2];
								if(gmx < outA16[2*offset+2]) gmx = outA16[2*offset+2];
								if(gmn > outA16[2]) gmn = outA16[2];
								if(gmx < outA16[2]) gmx = outA16[2];


								if((gmx - gmn) < range)
								{
									alpha = range;//delta;

									if(g > mx)
									{
										alpha *= (g-mx); //max range
										alpha >>= shift;
									}
									else // g < mn
									{
										alpha *= (mn-g); //max range
										alpha >>= shift;
									}

									alpha *= alpha;
									alpha >>= shift;


								//	avg = (outA16[-offset-1] + outA16[offset-1] + outA16[-offset+1] + outA16[offset+1] + 2) >> 2;
								//	*outA16 = avg; //good
								//	*outA16 = mn; //spotty

									if( (abs(outA16[offset] - outA16[-offset]) < range)
										&& ((abs(outA16[1] - outA16[-1]) < range)))
									{
										int val = (alpha*g + (range - alpha)*((mn+mx)>>1))>>shift;
										if(val > 0xffff) val = 0xffff;
										if(val < 0) val = 0;
										val |= 1;
										*outA16 = val;

									//	*outA16 = ((mn+mx)>>1) | 1; // like avg but less compute
									}
								}
							}

							outA16++; //g
							outA16++; //b
						}
					}
				}
			}
#endif
		}
		else
		{
			// No more work to do
			return;
		}
	}
}


void GenerateHalfBYR2(DECODER *decoder, FRAME_INFO *info, int thread_index,
		uint8_t *output, int pitch, int frame, uint8_t *scratch, int scratchsize, int threading)
{
    int bayer_format = decoder->cfhddata.bayer_format;
 	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int channel;

	unsigned short *scanline;
	//unsigned short *sptr;
	unsigned short *scanline2;
	//unsigned short *sptr2;
    char *buffer = (char *)scratch;
	//size_t buffer_size = scratchsize;

	//uint8_t *outyuv;
    uint8_t *line = output;
	int x,y=0;
	//int color_space = decoder->frame.colorspace;

	TRANSFORM **transform_array = decoder->transform;
	IMAGE *wavelet_array[TRANSFORM_MAX_CHANNELS];
	CODEC_STATE *codec = &decoder->codec;
	int num_channels = codec->num_channels;

	for (channel = 0; channel < num_channels; channel++)
	{
		wavelet_array[channel] = transform_array[channel]->wavelet[frame];
	}

	scanline = (unsigned short *)buffer;
	scanline2 = scanline;
	scanline2+= info->width*4;


	for (;;)
	{
		if(threading)
		{
			int work_index;
			// Wait for one row from each channel to process
			error = PoolThreadWaitForWork(&decoder->worker_thread.pool, &work_index, thread_index);
			if (error != THREAD_ERROR_OKAY)
				return; // No more work to do

			y = work_index;
		}

		if(y<info->height)
		{
			unsigned short *newlineA;
			unsigned short *newlineB;
			//int flags = 0;

			newlineA = (unsigned short *)line;
			newlineA += (pitch>>1)*y*2;
			newlineB = newlineA;
			newlineB += (pitch>>1);



			//memcpy(scanline, newline, info->width*4);

			switch (decoder->codec.encoded_format)
			{
			case ENCODED_FORMAT_BAYER:
				{
					//unsigned short *sptr;
					unsigned short *src = scanline2;

					short *ggptr = wavelet_array[0]->band[0];
					short *rgptr = wavelet_array[1]->band[0];
					short *bgptr = wavelet_array[2]->band[0];
					short *gdptr = wavelet_array[3]->band[0];

					ggptr += (wavelet_array[0]->pitch>>1) * y;//(info->height-1-y);
					rgptr += (wavelet_array[1]->pitch>>1) * y;//(info->height-1-y);
					bgptr += (wavelet_array[2]->pitch>>1) * y;//(info->height-1-y);
					gdptr += (wavelet_array[3]->pitch>>1) * y;//(info->height-1-y);

					for(x=0; x<info->width; x++)
					{
						int r,g,b,rg,bg,gd,g1,g2;
						//int y1,y2,u,v,dither;

						g = (*ggptr++)<<2;
						rg = (*rgptr++)<<2;
						bg = (*bgptr++)<<2;
						gd = ((*gdptr++)<<2) - 32768;

						r = ((rg - 32768)<<1) + g;
						b = ((bg - 32768)<<1) + g;
						g1 = g + gd;
						g2 = g - gd;

						if(r < 0) r = 0;
						if(g1 < 0) g1 = 0;
						if(g2 < 0) g2 = 0;
						if(b < 0) b = 0;

						if(r > 0xffff) r = 0xffff;
						if(g1 > 0xffff) g1 = 0xffff;
						if(g2 > 0xffff) g2 = 0xffff;
						if(b > 0xffff) b = 0xffff;

						switch(bayer_format)
						{
						case BAYER_FORMAT_RED_GRN: //Red-grn phase
							*newlineA++ = r;
							*newlineA++ = g1;
							*newlineB++ = g2;
							*newlineB++ = b;
							break;
						case BAYER_FORMAT_GRN_RED:// grn-red
							*newlineA++ = g1;
							*newlineA++ = r;
							*newlineB++ = b;
							*newlineB++ = g2;
							break;
						case BAYER_FORMAT_GRN_BLU:
							*newlineA++ = g1;
							*newlineA++ = b;
							*newlineB++ = r;
							*newlineB++ = g2;
							break;
						case BAYER_FORMAT_BLU_GRN:
							*newlineA++ = b;
							*newlineA++ = g1;
							*newlineB++ = g2;
							*newlineB++ = r;
							break;
						}
					}

					src = scanline2;
				}
				break;
			}

			y++;
		}
		else
		{
			// No more work to do
			return;
		}
	}
}



extern void TransformInverseSpatialSectionToOutput(DECODER *decoder, int thread_index,
											int frame_index, int num_channels,
											uint8_t *output_buffer, int output_pitch, FRAME_INFO *info,
											int chroma_offset, int precision,
											HorizontalInverseFilterOutputProc horizontal_filter_proc);

THREAD_PROC(WorkerThreadProc, lpParam)
{
	DECODER *decoder = (DECODER *)lpParam;
	WORKER_THREAD_DATA *data = &decoder->worker_thread.data;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;
	//int initFsm = -1;
	//FSM fsm;

#ifdef _WIN32
	if(decoder->thread_cntrl.affinity)
	{
		HANDLE hCurrentThread = GetCurrentThread();
		SetThreadAffinityMask(hCurrentThread, decoder->thread_cntrl.affinity);
	}
#endif

	// Set the handler for system exceptions
	SetDefaultExceptionHandler();

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&decoder->worker_thread.pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < decoder->worker_thread.pool.thread_count);
	
	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&decoder->worker_thread.pool, thread_index, &message);

		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
			int i;
			//int type;				// Type of inverse transform to perform
			uint8_t *output;		// Output frame buffer
			int pitch;				// Output frame pitch
			int frame;				// 0 or 1, of a 2 frame GOP
			uint8_t *channeldata[TRANSFORM_MAX_CHANNELS]; // used in quarter res decodes
			int channelpitch[TRANSFORM_MAX_CHANNELS]; // used in quarter res decodes
			FRAME_INFO info;
			uint8_t *scratch = NULL;
			size_t scratchsize = 0;
			int jobType = JOB_TYPE_OUTPUT;
			uint8_t *local_output = NULL;
			int local_pitch = 0;
			int channel_offset = 0;
			int chunk_size = 0;
			int line_max = 0;
			uint32_t flags = 0;
			int *lens_correct_buffer = NULL;
			void *mesh;

			int frame_index=0;		// Index of output frame to produce
			int num_channels=0;		// Number of channels in the transform array
			int chroma_offset=0;	// Offset for the output chroma
			int precision=0;		// Source pixel bit depth
			int fine_vertical = 0;
			// Inverse horizontal filter that produces the correct output format
			HorizontalInverseFilterOutputProc horizontal_filter_proc;

			// Lock access to the transform data
			Lock(&decoder->worker_thread.lock);

			// Get the processing parameters
			output = data->output;
			pitch = data->pitch;
			frame = data->framenum;
			for(i=0;i<TRANSFORM_MAX_CHANNELS;i++)
			{
				channeldata[i] = data->channeldata[i];
				channelpitch[i] = data->channelpitch[i];
			}
			memcpy(&info, &data->info, sizeof(FRAME_INFO));

			scratch = decoder->threads_buffer[thread_index];
			scratchsize = decoder->threads_buffer_size;

			jobType = data->jobType;
			if(jobType == JOB_TYPE_HORIZONAL_3D || jobType == JOB_TYPE_SHARPEN) // horizontal and anaglyph
			{
				local_output = data->local_output;
				local_pitch = data->local_pitch;
				channel_offset = data->channel_offset;
				chunk_size = data->chunk_size;
				line_max = data->line_max;
			}
			if(jobType == JOB_TYPE_VERTICAL_3D) // vertical and rotation
			{
				local_output = data->local_output;
				local_pitch = data->local_pitch;
				channel_offset = data->channel_offset;
				chunk_size = data->chunk_size;
				line_max = data->line_max;
				fine_vertical = data->fine_vertical;
			}
			if(jobType == JOB_TYPE_WAVELET)
			{
				frame_index = data->frame;
				num_channels = data->num_channels;
				chroma_offset = data->chroma_offset;
				precision = data->precision;
				horizontal_filter_proc = data->horizontal_filter_proc;
			}
			if(jobType == JOB_TYPE_WARP || jobType == JOB_TYPE_WARP_CACHE || jobType == JOB_TYPE_WARP_BLURV)
			{				
				mesh = data->data; 
				output = data->output;
				lens_correct_buffer = (int *)data->local_output;
				line_max = data->line_max;
				chunk_size = data->chunk_size;
				flags = data->flags;
			}

			// Unlock access to the transform data
			Unlock(&decoder->worker_thread.lock);


			if(jobType == JOB_TYPE_HORIZONAL_3D)
			{
				Do3DWork(decoder, &info, thread_index, output, pitch, scratch, (int)scratchsize, local_output, local_pitch, channel_offset, chunk_size, line_max);
			}
			else if(jobType == JOB_TYPE_SHARPEN)
			{
				DoVertSharpen(decoder, &info, thread_index, output, pitch, scratch, (int)scratchsize, local_output, local_pitch, channel_offset, chunk_size, line_max);
			}
			else if(jobType == JOB_TYPE_VERTICAL_3D)
			{
				Do3DVerticalWork(decoder, &info, thread_index, output, pitch, scratch, (int)scratchsize, local_output, local_pitch, channel_offset, chunk_size, line_max, fine_vertical);
			}
			else if(jobType == JOB_TYPE_HISTOGRAM)
			{
				DoHistogramWork(decoder, &info, thread_index, output, pitch, scratch, (int)scratchsize, local_output, local_pitch, channel_offset, chunk_size, line_max);
			}		  
			else if(jobType == JOB_TYPE_BUILD_1DS_2LINEAR)
			{
				line_max = decoder->worker_thread.pool.work_start_count;
				DoBuild1DCurves2Linear(decoder, thread_index, line_max);
			}		  
			else if(jobType == JOB_TYPE_BUILD_1DS_2CURVE)
			{
				line_max = decoder->worker_thread.pool.work_start_count;
				DoBuild1DLinear2Curves(decoder, thread_index, line_max);
			}		  
			else if(jobType == JOB_TYPE_BUILD_LUT_CURVES)
			{
				line_max = decoder->worker_thread.pool.work_start_count;
				DoBuildLUTCurves(decoder, thread_index, line_max);
			}		  
			else if(jobType == JOB_TYPE_BUILD_CUBE)
			{
				line_max = decoder->worker_thread.pool.work_start_count;
				DoBuildCube(decoder, thread_index, line_max);
			}
#if WARPSTUFF
			else if(jobType == JOB_TYPE_WARP)
			{				
				DoWarp(decoder, mesh, output, lens_correct_buffer, thread_index, line_max, chunk_size);
			}
			else if(jobType == JOB_TYPE_WARP_CACHE)
			{				
				DoWarpCache(decoder, mesh, thread_index, line_max, chunk_size, flags);
			}
			else if(jobType == JOB_TYPE_WARP_BLURV)
			{				
				DoWarpBlurV(decoder, mesh, thread_index, line_max, chunk_size, (uint8_t *)lens_correct_buffer, pitch);
			}
#endif

#if 0
		  else if(jobType == JOB_TYPE_BURNINS)
		  {
			DoBurninWork(decoder, &info, thread_index, output, pitch, scratch, scratchsize, local_output, local_pitch, channel_offset, chunk_size, line_max);

		  }
#endif
		  else if(jobType == JOB_TYPE_WAVELET)
		  {
			// Apply the inverse transform to the section of wavelet assigned to this thread
			TransformInverseSpatialSectionToOutput(decoder, thread_index, frame_index, num_channels,
												   output, pitch, &info, chroma_offset, precision,
												   horizontal_filter_proc);
		  }
		  else if(jobType == JOB_TYPE_OUTPUT_UNCOMPRESSED)
		  {
			Row16uUncompressed2OutputFormat(decoder, &info, thread_index, output, pitch, scratch, (int)scratchsize, true);
		  }
		  else if(jobType == JOB_TYPE_OUTPUT)
		  {
			switch (decoder->codec.encoded_format)
			{
			case ENCODED_FORMAT_RGB_444:		// Three planes of RGB 4:4:4
			case ENCODED_FORMAT_RGBA_4444:		// Four planes of ARGB 4:4:4:4
			case ENCODED_FORMAT_YUV_422:		// Original encoding scheme for YUV 4:2:2
				if(info.resolution == DECODED_RESOLUTION_FULL || info.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL)
					Row16uFull2OutputFormat(decoder, &info, thread_index, output, pitch, scratch, (int)scratchsize, true);
				else if(info.resolution == DECODED_RESOLUTION_HALF)
					Row16uHalf2OutputFormat(decoder, &info, thread_index, output, pitch, frame, scratch, (int)scratchsize, true);
				else if(info.resolution == DECODED_RESOLUTION_QUARTER)
					Row16uQuarter2OutputFormat(decoder, &info, thread_index, output, pitch, frame, scratch, (int)scratchsize, true, channeldata, channelpitch);
				else //lowpass?
				{
				}
				decoder->frame.alpha_Companded = 1;
				break;

			case ENCODED_FORMAT_YUVA_4444:		// Four planes of YUVA 4:4:4:4
				// Not implemented
				assert(0);
				break;

			case ENCODED_FORMAT_BAYER:			// Bayer encoded data
				// Apply the inverse transform to the section of wavelet assigned to this thread
				if(info.format == DECODED_FORMAT_BYR2 || info.format == DECODED_FORMAT_BYR4)
				{
					if(info.resolution == DECODED_RESOLUTION_HALF_NODEBAYER)
						GenerateHalfBYR2(decoder, &info, thread_index, output, pitch, frame, scratch, (int)scratchsize, true);
					else
						GenerateBYR2(decoder, &info, thread_index, output, pitch);
				}
				else if(info.resolution == DECODED_RESOLUTION_FULL_DEBAYER)
				{
					DemosaicRAW(decoder, &info, thread_index, output, pitch, scratch, (int)scratchsize);
				}
				else if(info.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER)
				{
					VerticalOnlyDemosaicRAW(decoder, &info, thread_index, output, pitch, scratch, (int)scratchsize);
				}
				else if(info.resolution == DECODED_RESOLUTION_FULL) // half res,
				{
					//TODO half res bayer decodes
					NoDemosaicRAW(decoder, &info, thread_index, output, pitch, scratch, (int)scratchsize);
				}
				else if(info.resolution == DECODED_RESOLUTION_QUARTER_NODEBAYER_SCALED) // quarter res for uncompressed BYR3,
				{
					//TODO half res bayer decodes
					NoDemosaicRAW(decoder, &info, thread_index, output, pitch, scratch, (int)scratchsize);
				}
				else if(info.resolution == DECODED_RESOLUTION_HALF) // quarter res,
				{
					QuarterRAW(decoder, &info, thread_index, output, pitch, scratch, (int)scratchsize);
				}
				break;

			}
		  }
		  else
		  {
			  assert(0); //unknown job
		  }
			// Signal that this thread is done
			PoolThreadSignalDone(&decoder->worker_thread.pool, thread_index);

			// Loop and wait for the next message
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}
	
	return (THREAD_RETURN_TYPE)error;
}


#endif //_THREADED


// Used in RT YUYV playback, no demosaic applied
// Apply the inverse horizontal transform to reconstruct a strip of rows into packed ouput pixels thru LUT
void InvertHorizontalStrip16sBayerThruLUT(HorizontalFilterParams)
{
	int num_channels = CODEC_NUM_CHANNELS;
	int height = roi.height;
	int width = roi.width;

	// Note that the u and v chroma values are swapped
	PIXEL *gg_lowpass_ptr = lowpass_band[0];
	PIXEL *rg_lowpass_ptr = lowpass_band[1];
	PIXEL *bg_lowpass_ptr = lowpass_band[2];
	PIXEL *gg_highpass_ptr = highpass_band[0];
	PIXEL *rg_highpass_ptr = highpass_band[1];
	PIXEL *bg_highpass_ptr = highpass_band[2];

	uint8_t *output = output_image;
	unsigned short *sptr;
	unsigned short *sptr2;
	unsigned short *sptraligned;
	//unsigned short scanline[8192*3];
	//unsigned short scanline2[8192*3];

	// Process 8 luma coefficients per loop iteration
	const int column_step = 8;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int channel;
	int row;

	// Compute the amount of scaling required to reduce the output precision
	//int descale_shift = (precision - 8);
	//float scale;

	uint8_t *scratch = NULL;
	size_t scratchsize = 0;
	uint8_t *scanline;
	uint8_t *scanline2;
	//ROI output_strip = roi;

	scratch = decoder->threads_buffer[thread_index];
	scratchsize = decoder->threads_buffer_size;
	scanline = scratch;
	scanline2 = (uint8_t *)((uintptr_t)scratch + ((scratchsize & 0xffffffe0)/2));
	
	sptraligned = (unsigned short *)ALIGNED_PTR(scanline);
	sptr = sptraligned;

	// Convert the pitch to units of pixels
	for (channel = 0; channel < num_channels; channel++)
	{
		lowpass_pitch[channel] /= sizeof(PIXEL);
		highpass_pitch[channel] /= sizeof(PIXEL);
	}

	output_pitch /= sizeof(uint8_t);

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
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
		__m128i *outptr = (__m128i *)&output[0];
		__m128i *scanlineptr = (__m128i *)sptr;

		//const __m128i mask_epi32 = _mm_set1_epi32(0xffff);
		const __m128i value128_epi16 = _mm_set1_epi16((1<<precision)/2);

		__m128i limiterRGB = _mm_set1_epi16(0x7fff - ((1<<precision)-1));
	//	__m128i limiter = _mm_set1_epi16(0x7fff - 0x3fff);

#endif
		uint8_t *colptr = (uint8_t *)&output[0];

		int32_t gg_even_value;
		int32_t bg_even_value;
		int32_t rg_even_value;
		int32_t gg_odd_value;
		int32_t bg_odd_value;
		int32_t rg_odd_value;


		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		int32_t even = 0;
		int32_t odd = 0;

		sptr2 = sptr;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * gg_lowpass_ptr[column + 0];
		even -=  4 * gg_lowpass_ptr[column + 1];
		even +=  1 * gg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += gg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the value for use in the fast loop
		gg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * gg_lowpass_ptr[column + 0];
		odd += 4 * gg_lowpass_ptr[column + 1];
		odd -= 1 * gg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= gg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the value for use in the fast loop
		gg_odd_value = odd;


		// Process the first two u chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * bg_lowpass_ptr[column + 0];
		even -=  4 * bg_lowpass_ptr[column + 1];
		even +=  1 * bg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += bg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the value for use in the fast loop
		bg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * bg_lowpass_ptr[column + 0];
		odd += 4 * bg_lowpass_ptr[column + 1];
		odd -= 1 * bg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= bg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the value for use in the fast loop
		bg_odd_value = odd;


		// Process the first two v chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * rg_lowpass_ptr[column + 0];
		even -=  4 * rg_lowpass_ptr[column + 1];
		even +=  1 * rg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += rg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the value for use in the fast loop
		rg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * rg_lowpass_ptr[column + 0];
		odd += 4 * rg_lowpass_ptr[column + 1];
		odd -= 1 * rg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= rg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the value for use in the fast loop
		rg_odd_value = odd;

#if (1 && XMMOPT)

		// Preload the first eight lowpass luma coefficients
		gg_low1_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[0]);

		// Preload the first eight highpass luma coefficients
		gg_high1_epi16 = _mm_load_si128((__m128i *)&gg_highpass_ptr[0]);

		// Preload the first eight lowpass u chroma coefficients
		bg_low1_epi16 = _mm_load_si128((__m128i *)&bg_lowpass_ptr[0]);

		// Preload the first eight highpass u chroma coefficients
		bg_high1_epi16 = _mm_load_si128((__m128i *)&bg_highpass_ptr[0]);

		// Preload the first eight lowpass v chroma coefficients
		rg_low1_epi16 = _mm_load_si128((__m128i *)&rg_lowpass_ptr[0]);

		// Preload the first eight highpass v chroma coefficients
 		rg_high1_epi16 = _mm_load_si128((__m128i *)&rg_highpass_ptr[0]);


		// The reconstruction filters use pixels starting at the first column
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

			//__m128i y1_output_epi16;
			//__m128i y2_output_epi16;
			//__m128i u1_output_epi16;
			//__m128i u2_output_epi16;
			//__m128i v1_output_epi16;
			//__m128i v2_output_epi16;

			//__m128i urg_epi16;
			//__m128i yuv1_epi16;
			//__m128i yuv2_epi16;
			//__m128i yuv1_epi8;
			//__m128i yuv2_epi8;
			//__m128i yuv3_epi8;
			//__m128i yuv4_epi8;

			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			//__m128i temp_epi32;
			//__m128i tempB_epi32;
			//__m128i rgb_epi32;
			__m128i zero_epi128;

			__m128i out_epi16;		// Reconstructed data
			//__m128i mask_epi16;
			//__m128i lsb_epi16;
			//__m128i sign_epi16;
			//__m128i high_epi16;

			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;

			//DAN031304 -- correct inverse filter
			__m128i half_epi16 = _mm_set1_epi16(4);
			__m128i offset_epi16 = _mm_set1_epi16(2048);

			uint32_t temp;		// Temporary register for last two values


			/***** Compute the first eight luma output values *****/

			// Preload the second eight lowpass coefficients
			gg_low2_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			gg_high2_epi16 = _mm_load_si128((__m128i *)&gg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = gg_low1_epi16;
			high1_epi16 = gg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);


			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			//out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_odd_value, 1);

			// Save the eight luma values for packing later
			gg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			gg_even_value = (short)temp;
			gg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight luma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = gg_low2_epi16;
			high2_epi16 = gg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			//out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_odd_value, 1);

			// Save the eight luma values for packing later
			gg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			gg_even_value = (short)temp;
			gg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are used later in the loop
			gg_low1_epi16 = gg_low2_epi16;

			// The second eight highpass coefficients are used later in the loop
			gg_high1_epi16 = gg_high2_epi16;


			/***** Compute the first eight u chroma output values *****/

			// Preload the second eight lowpass coefficients
			bg_low2_epi16 = _mm_load_si128((__m128i *)&bg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			bg_high2_epi16 = _mm_load_si128((__m128i *)&bg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = bg_low1_epi16;
			high1_epi16 = bg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			//out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			bg_even_value = (short)temp;
			bg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight u chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = bg_low2_epi16;
			high2_epi16 = bg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			//out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			bg_even_value = (short)temp;
			bg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			bg_low1_epi16 = bg_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			bg_high1_epi16 = bg_high2_epi16;



			/***** Compute the first eight v chroma output values *****/

			// Preload the second eight lowpass coefficients
			rg_low2_epi16 = _mm_load_si128((__m128i *)&rg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			rg_high2_epi16 = _mm_load_si128((__m128i *)&rg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = rg_low1_epi16;
			high1_epi16 = rg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			//out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			rg_even_value = (short)temp;
			rg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight v chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = rg_low2_epi16;
			high2_epi16 = rg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			//out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			rg_even_value = (short)temp;
			rg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			rg_low1_epi16 = rg_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			rg_high1_epi16 = rg_high2_epi16;












			//r_output_epi16  = ((rg_output_epi16 - 32768)<<1)+gg_output_epi16
			//r_output_epi16  = ((rg_output_epi16>>3 - 32768>>3))+gg_output_epi16>>4


			g1_output_epi16 = gg1_output_epi16;

			r1_output_epi16 = rg1_output_epi16;//_mm_srli_epi16(rg1_output_epi16,2);
			r1_output_epi16 = _mm_subs_epi16(r1_output_epi16, value128_epi16);
			r1_output_epi16 = _mm_slli_epi16(r1_output_epi16,1);
			r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, g1_output_epi16);

			b1_output_epi16 = bg1_output_epi16;//_mm_srli_epi16(bg1_output_epi16,2);
			b1_output_epi16 = _mm_subs_epi16(b1_output_epi16, value128_epi16);
			b1_output_epi16 = _mm_slli_epi16(b1_output_epi16,1);
			b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, g1_output_epi16);


			 r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, limiterRGB);
			 r1_output_epi16 = _mm_subs_epu16(r1_output_epi16, limiterRGB);

			 g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, limiterRGB);
			 g1_output_epi16 = _mm_subs_epu16(g1_output_epi16, limiterRGB);

			 b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, limiterRGB);
			 b1_output_epi16 = _mm_subs_epu16(b1_output_epi16, limiterRGB);

			r1_output_epi16 = _mm_slli_epi16(r1_output_epi16,16-precision);
			g1_output_epi16 = _mm_slli_epi16(g1_output_epi16,16-precision);
			b1_output_epi16 = _mm_slli_epi16(b1_output_epi16,16-precision);

 			_mm_store_si128(scanlineptr++, r1_output_epi16);
			_mm_store_si128(scanlineptr++, g1_output_epi16);
			_mm_store_si128(scanlineptr++, b1_output_epi16);


			zero_epi128 = _mm_setzero_si128();


			g2_output_epi16 = gg2_output_epi16;//_mm_srli_epi16(gg2_output_epi16,2);

			r2_output_epi16 = rg2_output_epi16;//_mm_srli_epi16(rg2_output_epi16,2);
			r2_output_epi16 = _mm_subs_epi16(r2_output_epi16, value128_epi16);
			r2_output_epi16 = _mm_slli_epi16(r2_output_epi16,1);
			r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, g2_output_epi16);

			b2_output_epi16 = bg2_output_epi16;//_mm_srli_epi16(bg2_output_epi16,2);
			b2_output_epi16 = _mm_subs_epi16(b2_output_epi16, value128_epi16);
			b2_output_epi16 = _mm_slli_epi16(b2_output_epi16,1);
			b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, g2_output_epi16);



			 r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, limiterRGB);
			 r2_output_epi16 = _mm_subs_epu16(r2_output_epi16, limiterRGB);

			 g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, limiterRGB);
			 g2_output_epi16 = _mm_subs_epu16(g2_output_epi16, limiterRGB);

			 b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, limiterRGB);
			 b2_output_epi16 = _mm_subs_epu16(b2_output_epi16, limiterRGB);

			r2_output_epi16 = _mm_slli_epi16(r2_output_epi16,16-precision);
			g2_output_epi16 = _mm_slli_epi16(g2_output_epi16,16-precision);
			b2_output_epi16 = _mm_slli_epi16(b2_output_epi16,16-precision);

			_mm_store_si128(scanlineptr++, r2_output_epi16);
			_mm_store_si128(scanlineptr++, g2_output_epi16);
			_mm_store_si128(scanlineptr++, b2_output_epi16);

			// Store the first sixteen bytes of output values
		//	_mm_store_si128(outptr++, yuv1_epi8);

					// Store the second sixteen bytes of output values
		//	_mm_store_si128(outptr++, yuv2_epi8);

		}

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

		colptr = (uint8_t *)outptr;

		sptr = (unsigned short *)scanlineptr;
#endif

//////////////////////////////////////////// *******************************************************************************************************************
//TODO: Non-SSE code not upgraded for Bayer. *******************************************************************************************************************
//////////////////////////////////////////// *******************************************************************************************************************
#if 0
		/*
		// Process the rest of the columns up to the last column in the row
		for (; column < last_column; column += 2)
		{
			int y1_even_value;
			int y2_even_value;
			int y1_odd_value;
			int y2_odd_value;


			// First pair of luma output values

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += gg_lowpass_ptr[column - 1];
			even -= gg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += gg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += gg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the luma result for later output in the correct order
			y1_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= gg_lowpass_ptr[column - 1];
			odd += gg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += gg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= gg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the luma result for later output in the correct order
			y1_odd_value = odd;


			// Pair of u chroma output values

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += bg_lowpass_ptr[column - 1];
			even -= bg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += bg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += bg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the u chroma result for later output in the correct order
			bg_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= bg_lowpass_ptr[column - 1];
			odd += bg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += bg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= bg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the u chroma result for later output in the correct order
			bg_odd_value = odd;


			// Second pair of luma output values

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += gg_lowpass_ptr[column + 0];
			even -= gg_lowpass_ptr[column + 2];
			even += 4; //DAN20050921
			even >>= 3;
			even += gg_lowpass_ptr[column + 1];

			// Add the highpass correction
			even += gg_highpass_ptr[column + 1];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the luma result for later output in the correct order
			y2_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= gg_lowpass_ptr[column + 0];
			odd += gg_lowpass_ptr[column + 2];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += gg_lowpass_ptr[column + 1];

			// Subtract the highpass correction
			odd -= gg_highpass_ptr[column + 1];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the luma result for later output in the correct order
			y2_odd_value = odd;


			// Pair of v chroma output values

			// Apply the even reconstruction filter to the lowpass band
			even = 0;
			even += rg_lowpass_ptr[column - 1];
			even -= rg_lowpass_ptr[column + 1];
			even += 4; //DAN20050921
			even >>= 3;
			even += rg_lowpass_ptr[column + 0];

			// Add the highpass correction
			even += rg_highpass_ptr[column];
			even = DivideByShift(even, 1);

			// Reduce the precision to eight bits
			even >>= descale_shift;

			// Save the v chroma result for later output in the correct order
			rg_even_value = even;

			// Apply the odd reconstruction filter to the lowpass band
			odd = 0;
			odd -= rg_lowpass_ptr[column - 1];
			odd += rg_lowpass_ptr[column + 1];
			odd += 4; //DAN20050921
			odd >>= 3;
			odd += rg_lowpass_ptr[column + 0];

			// Subtract the highpass correction
			odd -= rg_highpass_ptr[column];
			odd = DivideByShift(odd, 1);

			// Reduce the precision to eight bits
			odd >>= descale_shift;

			// Save the v chroma result for later output in the correct order
			rg_odd_value = odd;


			// Output the luma and chroma values in the correct order
			*(colptr++) = SATURATE_8U(y1_even_value);
			*(colptr++) = SATURATE_8U(bg_even_value);
			*(colptr++) = SATURATE_8U(y1_odd_value);
			*(colptr++) = SATURATE_8U(rg_even_value);

			// Need to output the second set of values?
			if ((column + 1) < last_column)
			{
				*(colptr++) = SATURATE_8U(y2_even_value);
				*(colptr++) = SATURATE_8U(bg_odd_value);
				*(colptr++) = SATURATE_8U(y2_odd_value);
				*(colptr++) = SATURATE_8U(rg_odd_value);
			}
			else
			{
				column++;
				break;
			}
		}

		// Should have exited the loop at the column for right border processing
	//	assert(column == last_column);


		column = last_column - 1;
		colptr -= 4;

		// Process the last luma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * gg_lowpass_ptr[column + 0];
		even += 4 * gg_lowpass_ptr[column - 1];
		even -= 1 * gg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += gg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the luma result for later output in the correct order
		gg_even_value = even;

		// Apply the odd border filter to the lowpass band
		odd += 11 * gg_lowpass_ptr[column + 0];
		odd -=  4 * gg_lowpass_ptr[column - 1];
		odd +=  1 * gg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= gg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the luma result for later output in the correct order
		gg_odd_value = odd;

		// Process the last u chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * bg_lowpass_ptr[column + 0];
		even += 4 * bg_lowpass_ptr[column - 1];
		even -= 1 * bg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += bg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		bg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * bg_lowpass_ptr[column + 0];
		odd -=  4 * bg_lowpass_ptr[column - 1];
		odd +=  1 * bg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= bg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the u chroma result for later output in the correct order
		bg_odd_value = odd;

		// Process the last v chroma output points with special filters for the right border
		even = 0;
		odd = 0;

		// Apply the even border filter to the lowpass band
		even += 5 * rg_lowpass_ptr[column + 0];
		even += 4 * rg_lowpass_ptr[column - 1];
		even -= 1 * rg_lowpass_ptr[column - 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += rg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		even >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		rg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 11 * rg_lowpass_ptr[column + 0];
		odd -=  4 * rg_lowpass_ptr[column - 1];
		odd +=  1 * rg_lowpass_ptr[column - 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= rg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		odd >>= descale_shift;

		// Save the v chroma result for later output in the correct order
		rg_odd_value = odd;

		//DAN06052005 - Fix for PSNR errors in UV on right edge
		colptr-=4;
		colptr++; // Y fine
		*(colptr++) = SATURATE_8U(bg_even_value);
		colptr++; // Y2 fine
		*(colptr++) = SATURATE_8U(rg_even_value);

		// Output the last luma and chroma values in the correct order
		*(colptr++) = SATURATE_8U(gg_even_value);
		*(colptr++) = SATURATE_8U(bg_odd_value);
		*(colptr++) = SATURATE_8U(gg_odd_value);
		*(colptr++) = SATURATE_8U(rg_odd_value);


		// Advance the output pointer to the next row

		output += output_pitch;
		*/
#endif

		// Advance to the next row of coefficients in each channel
		gg_lowpass_ptr += lowpass_pitch[0];
		bg_lowpass_ptr += lowpass_pitch[1];
		rg_lowpass_ptr += lowpass_pitch[2];
		gg_highpass_ptr += highpass_pitch[0];
		bg_highpass_ptr += highpass_pitch[1];
		rg_highpass_ptr += highpass_pitch[2];
	}

	{
		int flags = ACTIVEMETADATA_SRC_8PIXEL_PLANAR;
		int whitebitdepth = 16;
		unsigned char *outA8, *outB8;
		int pitch;

		outA8 = (unsigned char *)output;
		output += output_pitch;
		outB8 = (unsigned char *)output;
		pitch = (int)((intptr_t)outB8 - (intptr_t)outA8);

		//TODO: Get the scanline number
		sptr2 = sptraligned;
		if(decoder->apply_color_active_metadata)
			sptr2 = ApplyActiveMetaData(decoder, width*2, height, -1,
				(uint32_t *)sptraligned, (uint32_t *)scanline2,
				format, &whitebitdepth, &flags);

		ConvertLinesToOutput(decoder, width*2, height, 0, sptr2, outA8, pitch,
			format, whitebitdepth, flags);
	}
}




// Used in RT YUYV playback
// Apply the inverse horizontal transform to reconstruct a strip of rows into packed YUV pixels
void InvertHorizontalStrip16s444ThruLUT(HorizontalFilterParams)
{
	int num_channels = CODEC_NUM_CHANNELS;
	int height = roi.height;
	int width = roi.width;

	// Note that the u and v chroma values are swapped
	PIXEL *gg_lowpass_ptr = lowpass_band[0];
	PIXEL *rg_lowpass_ptr = lowpass_band[1];
	PIXEL *bg_lowpass_ptr = lowpass_band[2];
	PIXEL *gg_highpass_ptr = highpass_band[0];
	PIXEL *rg_highpass_ptr = highpass_band[1];
	PIXEL *bg_highpass_ptr = highpass_band[2];

	uint8_t *output = output_image;
	unsigned short *sptr;
	unsigned short *sptr2;
	unsigned short *sptraligned;
	//unsigned short scanline[8192*3];
	//unsigned short scanline2[8192*3];

	// Process 8 luma coefficients per loop iteration
	const int column_step = 8;

	// Need to process two luma coefficients up to the last column to allow for chroma output
	const int last_column = width;
	int post_column = last_column - (last_column % column_step);

	// Need at least four luma values of border processing up to the last column
	//const int post_border = 2;

	int channel;
	int row;

	// Compute the amount of scaling required to reduce the output precision
	//int descale_shift = (precision - 8);
	//float scale;

	uint8_t *scratch = NULL;
	size_t scratchsize = 0;
	uint8_t *scanline;
	uint8_t *scanline2;
	//ROI output_strip = roi;

	scratch = (uint8_t *)decoder->threads_buffer[thread_index];
	scratchsize = decoder->threads_buffer_size;
	scanline = scratch;
	scanline2 = (scratch + ((scratchsize & 0xffffffe0)/2));
	
	sptraligned = (unsigned short *)ALIGNED_PTR(scanline);
	sptr = sptraligned;

	// Convert the pitch to units of pixels
	for (channel = 0; channel < num_channels; channel++)
	{
		lowpass_pitch[channel] /= sizeof(PIXEL);
		highpass_pitch[channel] /= sizeof(PIXEL);
	}

	output_pitch /= sizeof(uint8_t);

	// Adjust the end of the fast loop if necessary for border processing
	//if (post_column > (last_column - post_border))  // DAN08112004 - ignore end row calc -- use SSE to edge.
	//	post_column -= column_step;

	// Check that there is enough margin to accommodate border processing
//	assert(post_column <= (last_column - post_border));

	// Process each row of the strip
	for (row = 0; row < height; row++)
	{
#if (1 && XMMOPT)
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
		__m128i *outptr = (__m128i *)&output[0];
		__m128i *scanlineptr = (__m128i *)sptr;

		//const __m128i mask_epi32 = _mm_set1_epi32(0xffff);
		//const __m128i value128_epi16 = _mm_set1_epi16((1<<precision)/2);

		__m128i limiterRGB = _mm_set1_epi16(0x7fff - ((1<<precision)-1));
	//	__m128i limiter = _mm_set1_epi16(0x7fff - 0x3fff);

#endif
		uint8_t *colptr = (uint8_t *)&output[0];

		int32_t gg_even_value;
		int32_t bg_even_value;
		int32_t rg_even_value;
		int32_t gg_odd_value;
		int32_t bg_odd_value;
		int32_t rg_odd_value;


		// Start processing at the beginning of the row
		int column = 0;

		// Process the first two luma output points with special filters for the left border
		int32_t even = 0;
		int32_t odd = 0;

		sptr2 = sptr;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * gg_lowpass_ptr[column + 0];
		even -=  4 * gg_lowpass_ptr[column + 1];
		even +=  1 * gg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += gg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the value for use in the fast loop
		gg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * gg_lowpass_ptr[column + 0];
		odd += 4 * gg_lowpass_ptr[column + 1];
		odd -= 1 * gg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= gg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the value for use in the fast loop
		gg_odd_value = odd;


		// Process the first two u chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * bg_lowpass_ptr[column + 0];
		even -=  4 * bg_lowpass_ptr[column + 1];
		even +=  1 * bg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += bg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the value for use in the fast loop
		bg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * bg_lowpass_ptr[column + 0];
		odd += 4 * bg_lowpass_ptr[column + 1];
		odd -= 1 * bg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= bg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the value for use in the fast loop
		bg_odd_value = odd;


		// Process the first two v chroma output points with special filters for the left border
		even = 0;
		odd = 0;

		// Apply the even reconstruction filter to the lowpass band
		even += 11 * rg_lowpass_ptr[column + 0];
		even -=  4 * rg_lowpass_ptr[column + 1];
		even +=  1 * rg_lowpass_ptr[column + 2];
		even += ROUNDING(even,8);
		even = DivideByShift(even, 3);

		// Add the highpass correction
		even += rg_highpass_ptr[column];
		even = DivideByShift(even, 1);

		// Reduce the precision to eight bits
		//even >>= descale_shift;

		// Save the value for use in the fast loop
		rg_even_value = even;

		// Apply the odd reconstruction filter to the lowpass band
		odd += 5 * rg_lowpass_ptr[column + 0];
		odd += 4 * rg_lowpass_ptr[column + 1];
		odd -= 1 * rg_lowpass_ptr[column + 2];
		odd += ROUNDING(odd,8);
		odd = DivideByShift(odd, 3);

		// Subtract the highpass correction
		odd -= rg_highpass_ptr[column];
		odd = DivideByShift(odd, 1);

		// Reduce the precision to eight bits
		//odd >>= descale_shift;

		// Save the value for use in the fast loop
		rg_odd_value = odd;

#if (1 && XMMOPT)

		// Preload the first eight lowpass luma coefficients
		gg_low1_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[0]);

		// Preload the first eight highpass luma coefficients
		gg_high1_epi16 = _mm_load_si128((__m128i *)&gg_highpass_ptr[0]);

		// Preload the first eight lowpass u chroma coefficients
		bg_low1_epi16 = _mm_load_si128((__m128i *)&bg_lowpass_ptr[0]);

		// Preload the first eight highpass u chroma coefficients
		bg_high1_epi16 = _mm_load_si128((__m128i *)&bg_highpass_ptr[0]);

		// Preload the first eight lowpass v chroma coefficients
		rg_low1_epi16 = _mm_load_si128((__m128i *)&rg_lowpass_ptr[0]);

		// Preload the first eight highpass v chroma coefficients
 		rg_high1_epi16 = _mm_load_si128((__m128i *)&rg_highpass_ptr[0]);


		// The reconstruction filters use pixels starting at the first column
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

			//__m128i y1_output_epi16;
			//__m128i y2_output_epi16;
			//__m128i u1_output_epi16;
			//__m128i u2_output_epi16;
			//__m128i v1_output_epi16;
			//__m128i v2_output_epi16;

			//__m128i urg_epi16;
			//__m128i yuv1_epi16;
			//__m128i yuv2_epi16;
			//__m128i yuv1_epi8;
			//__m128i yuv2_epi8;
			//__m128i yuv3_epi8;
			//__m128i yuv4_epi8;

			__m128i even_epi16;		// Result of convolution with even filter
			__m128i odd_epi16;		// Result of convolution with odd filter
			__m128i temp_epi16;
			//__m128i temp_epi32;
			//__m128i tempB_epi32;
			//__m128i rgb_epi32;
			__m128i zero_epi128;

			__m128i out_epi16;		// Reconstructed data
			//__m128i mask_epi16;
			//__m128i lsb_epi16;
			//__m128i sign_epi16;
			//__m128i high_epi16;

			__m128i low1_epi16;
			__m128i low2_epi16;
			__m128i high1_epi16;
			__m128i high2_epi16;

			//DAN031304 -- correct inverse filter
			__m128i half_epi16 = _mm_set1_epi16(4);
			__m128i offset_epi16 = _mm_set1_epi16(2048);

			uint32_t temp;		// Temporary register for last two values


			/***** Compute the first eight luma output values *****/

			// Preload the second eight lowpass coefficients
			gg_low2_epi16 = _mm_load_si128((__m128i *)&gg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			gg_high2_epi16 = _mm_load_si128((__m128i *)&gg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = gg_low1_epi16;
			high1_epi16 = gg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);


			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			//out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_odd_value, 1);

			// Save the eight luma values for packing later
			gg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			gg_even_value = (short)temp;
			gg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight luma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = gg_low2_epi16;
			high2_epi16 = gg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			//out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, gg_odd_value, 1);

			// Save the eight luma values for packing later
			gg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			gg_even_value = (short)temp;
			gg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are used later in the loop
			gg_low1_epi16 = gg_low2_epi16;

			// The second eight highpass coefficients are used later in the loop
			gg_high1_epi16 = gg_high2_epi16;


			/***** Compute the first eight u chroma output values *****/

			// Preload the second eight lowpass coefficients
			bg_low2_epi16 = _mm_load_si128((__m128i *)&bg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			bg_high2_epi16 = _mm_load_si128((__m128i *)&bg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = bg_low1_epi16;
			high1_epi16 = bg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			//out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			bg_even_value = (short)temp;
			bg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight u chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = bg_low2_epi16;
			high2_epi16 = bg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			//out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, bg_odd_value, 1);

			// Save the eight u chroma values for packing later
			bg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			bg_even_value = (short)temp;
			bg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			bg_low1_epi16 = bg_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			bg_high1_epi16 = bg_high2_epi16;



			/***** Compute the first eight v chroma output values *****/

			// Preload the second eight lowpass coefficients
			rg_low2_epi16 = _mm_load_si128((__m128i *)&rg_lowpass_ptr[column + 8]);

			// Preload the second eight highpass coefficients
			rg_high2_epi16 = _mm_load_si128((__m128i *)&rg_highpass_ptr[column + 8]);

			// Move the current set of coefficients to working registers
			low1_epi16 = rg_low1_epi16;
			high1_epi16 = rg_high1_epi16;

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift the highpass correction by one column
			high1_epi16 = _mm_srli_si128(high1_epi16, 1*2);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			//out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg1_output_epi16 = out_epi16;

			// Save the remaining two output values
			rg_even_value = (short)temp;
			rg_odd_value = (short)(temp >> 16);


			/***** Compute the second eight v chroma output values *****/

			// Move the next set of coefficients to working registers
			low2_epi16 = rg_low2_epi16;
			high2_epi16 = rg_high2_epi16;

			// Shift in the new pixels for the next stage of the loop
			low1_epi16 = _mm_srli_si128(low1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(low2_epi16, 4*2);
			low1_epi16 = _mm_or_si128(low1_epi16, temp_epi16);

			// Apply the even reconstruction filter to the lowpass band
			even_epi16 = low1_epi16;
			temp_epi16 = _mm_srli_si128(even_epi16, 2*2);
			even_epi16 = _mm_subs_epi16(even_epi16, temp_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, half_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			even_epi16 = _mm_adds_epi16(even_epi16, temp_epi16);

			// Shift in the next four highpass coefficients
			high1_epi16 = _mm_srli_si128(high1_epi16, 4*2);
			temp_epi16 = _mm_slli_si128(high2_epi16, 3*2);
			high1_epi16 = _mm_or_si128(high1_epi16, temp_epi16);

			// Add the highpass correction and divide by two
			even_epi16 = _mm_adds_epi16(even_epi16, offset_epi16);
			even_epi16 = _mm_adds_epi16(even_epi16, high1_epi16);
			even_epi16 = _mm_subs_epu16(even_epi16, offset_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);

			// Apply the odd reconstruction filter to the lowpass band
			odd_epi16 = _mm_srli_si128(low1_epi16, 2*2);
			temp_epi16 = low1_epi16;
			odd_epi16 = _mm_subs_epi16(odd_epi16, temp_epi16);
			odd_epi16 = _mm_adds_epi16(odd_epi16, half_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 3);
			temp_epi16 = _mm_srli_si128(low1_epi16, 1*2);
			odd_epi16 = _mm_adds_epi16(odd_epi16, temp_epi16);

			// Subtract the highpass correction and divide by two
			odd_epi16 = _mm_adds_epi16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_subs_epi16(odd_epi16, high1_epi16);
			odd_epi16 = _mm_subs_epu16(odd_epi16, offset_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);

			// Interleave the four even and four odd results
			out_epi16 = _mm_unpacklo_epi16(even_epi16, odd_epi16);

			// Reduce the precision to eight bits
			//out_epi16 = _mm_srli_epi16(out_epi16, descale_shift);

			// Combine the new output values with the two values from the previous phase
			out_epi16 = _mm_shuffle_epi32(out_epi16, _MM_SHUFFLE(2, 1, 0, 3));
			temp = _mm_cvtsi128_si32(out_epi16);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_even_value, 0);
			out_epi16 = _mm_insert_epi16(out_epi16, rg_odd_value, 1);

			// Save the eight u chroma values for packing later
			rg2_output_epi16 = out_epi16;

			// Save the remaining two output values
			rg_even_value = (short)temp;
			rg_odd_value = (short)(temp >> 16);

			// The second eight lowpass coefficients are the current values in the next iteration
			rg_low1_epi16 = rg_low2_epi16;

			// The second eight highpass coefficients are the current values in the next iteration
			rg_high1_epi16 = rg_high2_epi16;












			//r_output_epi16  = ((rg_output_epi16 - 32768)<<1)+gg_output_epi16
			//r_output_epi16  = ((rg_output_epi16>>3 - 32768>>3))+gg_output_epi16>>4


			g1_output_epi16 = gg1_output_epi16;

			r1_output_epi16 = rg1_output_epi16;

			b1_output_epi16 = bg1_output_epi16;

			 r1_output_epi16 = _mm_adds_epi16(r1_output_epi16, limiterRGB);
			 r1_output_epi16 = _mm_subs_epu16(r1_output_epi16, limiterRGB);

			 g1_output_epi16 = _mm_adds_epi16(g1_output_epi16, limiterRGB);
			 g1_output_epi16 = _mm_subs_epu16(g1_output_epi16, limiterRGB);

			 b1_output_epi16 = _mm_adds_epi16(b1_output_epi16, limiterRGB);
			 b1_output_epi16 = _mm_subs_epu16(b1_output_epi16, limiterRGB);

			r1_output_epi16 = _mm_slli_epi16(r1_output_epi16,16-precision);
			g1_output_epi16 = _mm_slli_epi16(g1_output_epi16,16-precision);
			b1_output_epi16 = _mm_slli_epi16(b1_output_epi16,16-precision);

			_mm_store_si128(scanlineptr++, r1_output_epi16);
			_mm_store_si128(scanlineptr++, g1_output_epi16);
			_mm_store_si128(scanlineptr++, b1_output_epi16);


			zero_epi128 = _mm_setzero_si128();


			g2_output_epi16 = gg2_output_epi16;

			r2_output_epi16 = rg2_output_epi16;

			b2_output_epi16 = bg2_output_epi16;



			 r2_output_epi16 = _mm_adds_epi16(r2_output_epi16, limiterRGB);
			 r2_output_epi16 = _mm_subs_epu16(r2_output_epi16, limiterRGB);

			 g2_output_epi16 = _mm_adds_epi16(g2_output_epi16, limiterRGB);
			 g2_output_epi16 = _mm_subs_epu16(g2_output_epi16, limiterRGB);

			 b2_output_epi16 = _mm_adds_epi16(b2_output_epi16, limiterRGB);
			 b2_output_epi16 = _mm_subs_epu16(b2_output_epi16, limiterRGB);

			r2_output_epi16 = _mm_slli_epi16(r2_output_epi16,16-precision);
			g2_output_epi16 = _mm_slli_epi16(g2_output_epi16,16-precision);
			b2_output_epi16 = _mm_slli_epi16(b2_output_epi16,16-precision);

			_mm_store_si128(scanlineptr++, r2_output_epi16);
			_mm_store_si128(scanlineptr++, g2_output_epi16);
			_mm_store_si128(scanlineptr++, b2_output_epi16);

		}

		// Should have exited the loop with the column equal to the post processing column
	//	assert(column == post_column);

		colptr = (uint8_t *)outptr;

		sptr = (unsigned short *)scanlineptr;
#endif

//////////////////////////////////////////// *******************************************************************************************************************
//TODO: Non-SSE code not upgraded for Bayer. *******************************************************************************************************************
//////////////////////////////////////////// *******************************************************************************************************************
///
		////
		////  Non SSE2 code here
		////

		// Advance to the next row of coefficients in each channel
		gg_lowpass_ptr += lowpass_pitch[0];
		bg_lowpass_ptr += lowpass_pitch[1];
		rg_lowpass_ptr += lowpass_pitch[2];
		gg_highpass_ptr += highpass_pitch[0];
		bg_highpass_ptr += highpass_pitch[1];
		rg_highpass_ptr += highpass_pitch[2];
	}

	{
		int flags = ACTIVEMETADATA_SRC_8PIXEL_PLANAR;
		int whitebitdepth = 16;
		unsigned char *outA8, *outB8;
		int pitch;

		outA8 = (unsigned char *)output;
		output += output_pitch;
		outB8 = (unsigned char *)output;
		pitch = (int)PTR_DIFF(outB8, outA8);

		//TODO: Get the scanline number
		sptr2 = sptraligned;
		if(decoder->apply_color_active_metadata)
			sptr2 = ApplyActiveMetaData(decoder, width*2, height, -1,
				(uint32_t *)sptraligned, (uint32_t *)scanline2,
				format, &whitebitdepth, &flags);

		ConvertLinesToOutput(decoder, width*2, height, 0, sptr2, outA8, pitch,
			format, whitebitdepth, flags);
	}
}



void Convert4444LinesToOutput(DECODER *decoder, int width, int height, int linenum,
						    unsigned short *src, uint8_t *output, int pitch,
							int format, int whitepoint, int flags)
{
	//
	//	colorformatdone: TRUE = 3D LUT was used for color space conversion.  Only
	//								applies to YUV output formats
	//	planar: TRUE = row planar (YU16 for YUV)
	int x,lines;
	unsigned short *sptr = src;
	short *signed_sptr = (short *)src;
	//int white = (1<<whitepoint)-1;
	int dnshiftto8bit = whitepoint-8;
	//int dnshiftto10bit = whitepoint-10;
	//int upshiftto16bit = 16-whitepoint;
	int dnshiftto13bit = whitepoint-13;
	int saturate = ((whitepoint < 16) && !(flags & ACTIVEMETADATA_PRESATURATED));
	int colorformatdone = (flags & ACTIVEMETADATA_COLORFORMATDONE);
	uint8_t *outA8 = output;
	int colorspace = decoder->frame.colorspace;
	int y_rmult,u_rmult,v_rmult;
	int y_gmult,u_gmult,v_gmult;
	int y_bmult,u_bmult,v_bmult;
	float rgb2yuv[3][4];
	int rgb2yuv_i[3][4];
	int yoffset = 16;
	int cg2vs = 0;
	int mixdown = 0;
	int colorAr;
	int colorAg;
	int colorAb;
	int colorBr;
	int colorBg;
	int colorBb;
	int mixdownRes = 32;

	if(decoder->useAlphaMixDown[0] && decoder->use_local_buffer == 0)
	{
		mixdown = 1;

		colorAr = (decoder->useAlphaMixDown[0]>>24) & 0xff;
		colorAg = (decoder->useAlphaMixDown[0]>>16) & 0xff;
		colorAb = (decoder->useAlphaMixDown[0]>>8) & 0xff;
		colorBr = (decoder->useAlphaMixDown[1]>>24) & 0xff;
		colorBg = (decoder->useAlphaMixDown[1]>>16) & 0xff;
		colorBb = (decoder->useAlphaMixDown[1]>>8) & 0xff;

		switch(decoder->frame.resolution)
		{
		case DECODED_RESOLUTION_FULL:
		case DECODED_RESOLUTION_FULL_DEBAYER:
			mixdownRes = 32;
			break;
		case DECODED_RESOLUTION_HALF:
		case DECODED_RESOLUTION_HALF_NODEBAYER:
		case DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER:
		case DECODED_RESOLUTION_HALF_HORIZONTAL:
		case DECODED_RESOLUTION_HALF_VERTICAL:
			mixdownRes = 16;
			break;
		case DECODED_RESOLUTION_QUARTER:
		case DECODED_RESOLUTION_LOWPASS_ONLY:
		case DECODED_RESOLUTION_QUARTER_NODEBAYER_SCALED:
			mixdownRes = 8;
			break;
		}
	}

	if(decoder->frame.alpha_Companded == 0)
	{
		unsigned short *sptr = src;
		short *signed_sptr = (short *)src;
		__m128i limiterRGB12 = _mm_set1_epi16(0x7fff - 0x0fff);

		if(whitepoint==13)
		{
			for(lines=0; lines<height; lines++)
			{
				if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
				{
					for(x=0; x<width; x+=8)
					{
						__m128i a_epi16 = _mm_load_si128((__m128i *)&signed_sptr[24]); //13-bit

						a_epi16 = _mm_srai_epi16(a_epi16, 1);  //12-bit
						a_epi16 = _mm_adds_epi16(a_epi16, limiterRGB12);
						a_epi16 = _mm_subs_epu16(a_epi16, limiterRGB12);
						a_epi16 = _mm_subs_epu16(a_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
						a_epi16 = _mm_slli_epi16(a_epi16, 3);  //15-bit
						a_epi16 = _mm_mulhi_epi16(a_epi16, _mm_set1_epi16(alphacompandGain));
						a_epi16 = _mm_adds_epi16(a_epi16, limiterRGB12); //12-bit limit
						a_epi16 = _mm_subs_epu16(a_epi16, limiterRGB12);
						a_epi16 = _mm_slli_epi16(a_epi16, 1);  //13-bit

						_mm_store_si128((__m128i *)&signed_sptr[24], a_epi16);

/*
						int xx;
						for(xx=0;xx<8; xx++)
						{
							int ai;
							ai = signed_sptr[24]>>1; //12-bit

							ai -= alphacompandDCoffset;
							ai <<= 3; //15-bit
							ai *= alphacompandGain;
							ai >>= 15; //13-bit
							
							if(ai>8191) ai=8191; if(ai<0) ai=0;

							signed_sptr[24] = ai;			
							signed_sptr++;
						}*/

						signed_sptr += 24;
					}
				}
				else if(flags & ACTIVEMETADATA_PLANAR)
				{
					int width8 = (width >> 3) * 8;
					
					if((width*3) & ~15) // non-aligned memory access
						width8 = 0;

					for(x=0; x<width8; x+=8)
					{
						__m128i a_epi16 = _mm_load_si128((__m128i *)&signed_sptr[width*3]); //13-bit
						
						a_epi16 = _mm_srai_epi16(a_epi16, 1);  //12-bit
						a_epi16 = _mm_adds_epi16(a_epi16, limiterRGB12);
						a_epi16 = _mm_subs_epu16(a_epi16, limiterRGB12);
						a_epi16 = _mm_subs_epu16(a_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
						a_epi16 = _mm_slli_epi16(a_epi16, 3);  //15-bit
						a_epi16 = _mm_mulhi_epi16(a_epi16, _mm_set1_epi16(alphacompandGain));
						a_epi16 = _mm_adds_epi16(a_epi16, limiterRGB12); //12-bit limit
						a_epi16 = _mm_subs_epu16(a_epi16, limiterRGB12);
						a_epi16 = _mm_slli_epi16(a_epi16, 1);  //13-bit

						_mm_store_si128((__m128i *)&signed_sptr[width*3], a_epi16);

						signed_sptr+=8;
					}

					for(;x<width; x++)
					{
						int ai;
						ai = signed_sptr[width*3] >> 1; //12-bit
							
						ai -= alphacompandDCoffset;
						ai <<= 3; //15-bit
						ai *= alphacompandGain;
						ai >>= 15; //13-bit
						
						if(ai>8191) ai=8191; if(ai<0) ai=0;

						signed_sptr[width*3] = ai;
						signed_sptr++;
					}
				}
				else
				{
					for(x=0; x<width; x++)
					{
						int ai = signed_sptr[3]>>1; //12-bit

						ai -= alphacompandDCoffset;
						ai <<= 3; //15-bit
						ai *= alphacompandGain;
						ai >>= 15; //13-bit
						
						if(ai>8191) ai=8191; if(ai<0) ai=0;

						signed_sptr[3] = ai;
						signed_sptr+=4;
					}
				}
			}
		}
		else //16-bit unsigned
		{
			for(lines=0; lines<height; lines++)
			{
				if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
				{
					for(x=0; x<width; x+=8)
					{
						__m128i a_epi16 = _mm_load_si128((__m128i *)&sptr[24]); //16-bit

						a_epi16 = _mm_srli_epi16(a_epi16, 4);  //12-bit
						a_epi16 = _mm_subs_epu16(a_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
						a_epi16 = _mm_slli_epi16(a_epi16, 3);  //15-bit
						a_epi16 = _mm_mulhi_epi16(a_epi16, _mm_set1_epi16(alphacompandGain));
						a_epi16 = _mm_adds_epi16(a_epi16, limiterRGB12); //12-bit limit
						a_epi16 = _mm_subs_epu16(a_epi16, limiterRGB12);
						a_epi16 = _mm_slli_epi16(a_epi16, 4);  //16-bit

						_mm_store_si128((__m128i *)&sptr[24], a_epi16);

						/*
						int xx;
						for(xx=0;xx<8; xx++)
						{
							int ai;
							ai = sptr[24]>>1; //12-bit

							ai -= alphacompandDCoffset;
							ai <<= 3; //15-bit
							ai *= alphacompandGain;
							ai >>= 15; //13-bit
							
							if(ai>8191) ai=8191; if(ai<0) ai=0;

							sptr[24] = ai;			
							sptr++;

						}
						*/

						sptr += 24;
					}
				}
				else if(flags & ACTIVEMETADATA_PLANAR)
				{
					int width8 = (width >> 3) * 8;

					if((width*3) & ~15) // non-aligned memory access
						width8 = 0;


					for(x=0; x<width8; x+=8)
					{
						__m128i a_epi16 = _mm_load_si128((__m128i *)&sptr[width*3]); //16-bit
						
						a_epi16 = _mm_srli_epi16(a_epi16, 4);  //12-bit
						a_epi16 = _mm_subs_epu16(a_epi16, _mm_set1_epi16(alphacompandDCoffset)); //12-bit  -16
						a_epi16 = _mm_slli_epi16(a_epi16, 3);  //15-bit
						a_epi16 = _mm_mulhi_epi16(a_epi16, _mm_set1_epi16(alphacompandGain));
						a_epi16 = _mm_adds_epi16(a_epi16, limiterRGB12); //12-bit limit
						a_epi16 = _mm_subs_epu16(a_epi16, limiterRGB12);
						a_epi16 = _mm_slli_epi16(a_epi16, 4);  //16-bit

						_mm_store_si128((__m128i *)&sptr[width*3], a_epi16);

						sptr+=8;
					}

					for(;x<width; x++)
					{
						int ai;
						ai = sptr[width*3] >> 4; //12-bit
							
						ai -= alphacompandDCoffset;
						ai <<= 3; //15-bit
						ai *= alphacompandGain;
						ai >>= 12; //16-bit
						
						if(ai>65535) ai=65535; if(ai<0) ai=0;

						sptr[width*3] = ai;
						sptr++;
					}
				}
				else
				{
					for(x=0; x<width; x++)
					{
						int ai = sptr[3]>>4; //12-bit

						ai -= alphacompandDCoffset;
						ai <<= 3; //15-bit
						ai *= alphacompandGain;
						ai >>= 12; //16-bit
						
						if(ai>65535) ai=65535; if(ai<0) ai=0;

						sptr[3] = ai;
						sptr+=4;
					}
				}
			}
		}
	}


	if(!colorformatdone && LUTYUV(format))
	{
		switch(colorspace & COLORSPACE_MASK)
		{
		case COLOR_SPACE_CG_601:
			if(whitepoint == 16 || decoder->broadcastLimit)
			{
				memcpy(rgb2yuv, rgb2yuv601, 12*sizeof(float));
			}
			else
			{
				cg2vs = 1;
				memcpy(rgb2yuv, rgb2yuvVS601, 12*sizeof(float));
			}
			break;
		default: assert(0);
		case COLOR_SPACE_CG_709:			
			if(whitepoint == 16 || decoder->broadcastLimit)
			{
				memcpy(rgb2yuv, rgb2yuv709, 12*sizeof(float));
			}
			else
			{
				cg2vs = 1;
				memcpy(rgb2yuv, rgb2yuvVS709, 12*sizeof(float));
			}
			break;
		case COLOR_SPACE_VS_601:
			memcpy(rgb2yuv, rgb2yuvVS601, 12*sizeof(float));
			break;
		case COLOR_SPACE_VS_709:
			memcpy(rgb2yuv, rgb2yuvVS709, 12*sizeof(float));
			break;
		}
		y_rmult = rgb2yuv_i[0][0] = (int)(rgb2yuv[0][0] * 32768.0f);
		y_gmult = rgb2yuv_i[0][1] = (int)(rgb2yuv[0][1] * 32768.0f);
		y_bmult = rgb2yuv_i[0][2] = (int)(rgb2yuv[0][2] * 32768.0f);
		u_rmult = rgb2yuv_i[1][0] = (int)(rgb2yuv[1][0] * 32768.0f);
		u_gmult = rgb2yuv_i[1][1] = (int)(rgb2yuv[1][1] * 32768.0f);
		u_bmult = rgb2yuv_i[1][2] = (int)(rgb2yuv[1][2] * 32768.0f);
		v_rmult = rgb2yuv_i[2][0] = (int)(rgb2yuv[2][0] * 32768.0f);
		v_gmult = rgb2yuv_i[2][1] = (int)(rgb2yuv[2][1] * 32768.0f);
		v_bmult = rgb2yuv_i[2][2] = (int)(rgb2yuv[2][2] * 32768.0f);

		if(rgb2yuv[0][3] == 0.0)
			yoffset = 0;
	}



	switch(format & 0x7ffffff)
	{
		case COLOR_FORMAT_RGB32:
			if(saturate && whitepoint<16)
			{
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								int ri,gi,bi,ai;
								ri = signed_sptr[0]>>dnshiftto8bit;
								gi = signed_sptr[8]>>dnshiftto8bit;
								bi = signed_sptr[16]>>dnshiftto8bit;
								ai = signed_sptr[24]>>dnshiftto8bit;
								signed_sptr++;

								if(ri>255) ri=255; if(ri<0) ri=0;
								if(gi>255) gi=255; if(gi<0) gi=0;
								if(bi>255) bi=255; if(bi<0) bi=0;
								if(ai>255) ai=255; if(ai<0) ai=0;

								if(mixdown)
								{
									int xx,yy;
									xx = x / mixdownRes;
									yy = linenum / mixdownRes;
									if((xx+yy) & 1)
									{
										ri = (colorAr * (256 - ai) +  ri * ai) >> 8;
										gi = (colorAg * (256 - ai) +  gi * ai) >> 8;
										bi = (colorAb * (256 - ai) +  bi * ai) >> 8;
									}
									else
									{
										ri = (colorBr * (256 - ai) +  ri * ai) >> 8;
										gi = (colorBg * (256 - ai) +  gi * ai) >> 8;
										bi = (colorBb * (256 - ai) +  bi * ai) >> 8;
									}
									ai = 0xff;
								}

								outA8[3] = ai;
								outA8[2] = ri;
								outA8[1] = gi;
								outA8[0] = bi;
								outA8+=4;
							}
							signed_sptr += 24;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							int ri,gi,bi,ai;
							ri = signed_sptr[0]>>dnshiftto8bit;
							gi = signed_sptr[width]>>dnshiftto8bit;
							bi = signed_sptr[width*2]>>dnshiftto8bit;
							ai = signed_sptr[width*3]>>dnshiftto8bit;
							signed_sptr++;

							if(ri>255) ri=255; if(ri<0) ri=0;
							if(gi>255) gi=255; if(gi<0) gi=0;
							if(bi>255) bi=255; if(bi<0) bi=0;
							if(ai>255) ai=255; if(ai<0) ai=0;

							if(mixdown)
							{
								int xx,yy;
								xx = x / mixdownRes;
								yy = linenum / mixdownRes;
								if((xx+yy) & 1)
								{
									ri = (colorAr * (256 - ai) +  ri * ai) >> 8;
									gi = (colorAg * (256 - ai) +  gi * ai) >> 8;
									bi = (colorAb * (256 - ai) +  bi * ai) >> 8;
								}		  
								else	  
								{		  
									ri = (colorBr * (256 - ai) +  ri * ai) >> 8;
									gi = (colorBg * (256 - ai) +  gi * ai) >> 8;
									bi = (colorBb * (256 - ai) +  bi * ai) >> 8;
								}
								ai = 0xff;
							}

							outA8[3] = ai;
							outA8[2] = ri;
							outA8[1] = gi;
							outA8[0] = bi;
							outA8+=4;
						}
					}
					else
					{
						for(x=0; x<width; x++)
						{
							int ri = signed_sptr[0]>>dnshiftto8bit;
							int gi = signed_sptr[1]>>dnshiftto8bit;
							int bi = signed_sptr[2]>>dnshiftto8bit;
							int ai = signed_sptr[3]>>dnshiftto8bit;
							signed_sptr+=4;

							if(ri>255) ri=255; if(ri<0) ri=0;
							if(gi>255) gi=255; if(gi<0) gi=0;
							if(bi>255) bi=255; if(bi<0) bi=0;
							if(ai>255) ai=255; if(ai<0) ai=0;

							if(mixdown)
							{
								int xx,yy;
								xx = x / mixdownRes;
								yy = linenum / mixdownRes;
								if((xx+yy) & 1)
								{
									ri = (colorAr * (256 - ai) +  ri * ai) >> 8;
									gi = (colorAg * (256 - ai) +  gi * ai) >> 8;
									bi = (colorAb * (256 - ai) +  bi * ai) >> 8;
								}		  
								else	  
								{		  
									ri = (colorBr * (256 - ai) +  ri * ai) >> 8;
									gi = (colorBg * (256 - ai) +  gi * ai) >> 8;
									bi = (colorBb * (256 - ai) +  bi * ai) >> 8;
								}
								ai = 0xff;
							}

							outA8[3] = ai;
							outA8[2] = ri;
							outA8[1] = gi;
							outA8[0] = bi;
							outA8+=4;
						}
					}
					output += pitch;
				}
			}
			else
			{
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								int ri,gi,bi,ai;
								ri = sptr[0]>>dnshiftto8bit;
								gi = sptr[8]>>dnshiftto8bit;
								bi = sptr[16]>>dnshiftto8bit;
								ai = sptr[24]>>dnshiftto8bit;
								sptr++;

								if(mixdown)
								{
									int xx,yy;
									xx = x / mixdownRes;
									yy = linenum / mixdownRes;
									if((xx+yy) & 1)
									{
										ri = (colorAr * (256 - ai) +  ri * ai) >> 8;
										gi = (colorAg * (256 - ai) +  gi * ai) >> 8;
										bi = (colorAb * (256 - ai) +  bi * ai) >> 8;
									}		  
									else	  
									{		  
										ri = (colorBr * (256 - ai) +  ri * ai) >> 8;
										gi = (colorBg * (256 - ai) +  gi * ai) >> 8;
										bi = (colorBb * (256 - ai) +  bi * ai) >> 8;
									}
									ai = 0xff;
								}

								outA8[3] = ai;
								outA8[2] = ri;
								outA8[1] = gi;
								outA8[0] = bi;
								outA8+=4;
							}
							sptr += 24;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							int ri,gi,bi,ai;
							ri = sptr[0]>>dnshiftto8bit;
							gi = sptr[width]>>dnshiftto8bit;
							bi = sptr[width*2]>>dnshiftto8bit;
							ai = sptr[width*3]>>dnshiftto8bit;
							sptr++;

							if(mixdown)
							{
								int xx,yy;
								xx = x / mixdownRes;
								yy = linenum / mixdownRes;
								if((xx+yy) & 1)
								{
									ri = (colorAr * (256 - ai) +  ri * ai) >> 8;
									gi = (colorAg * (256 - ai) +  gi * ai) >> 8;
									bi = (colorAb * (256 - ai) +  bi * ai) >> 8;
								}		  
								else	  
								{		  
									ri = (colorBr * (256 - ai) +  ri * ai) >> 8;
									gi = (colorBg * (256 - ai) +  gi * ai) >> 8;
									bi = (colorBb * (256 - ai) +  bi * ai) >> 8;
								}
								ai = 0xff;
							}

							outA8[3] = ai;
							outA8[2] = ri;
							outA8[1] = gi;
							outA8[0] = bi;
							outA8+=4;
						}
					}
					else
					{
						//int rounding = (1<<dnshiftto8bit)>>1;
						for(x=0; x<width; x++)
						{
							int ri,gi,bi,ai;
							ri = (sptr[0]/*+rounding*/)>>dnshiftto8bit;
							gi = (sptr[1]/*+rounding*/)>>dnshiftto8bit;
							bi = (sptr[2]/*+rounding*/)>>dnshiftto8bit;
							ai = (sptr[3]/*+rounding*/)>>dnshiftto8bit;

							if(mixdown)
							{
								int xx,yy;
								xx = x / mixdownRes;
								yy = linenum / mixdownRes;
								if((xx+yy) & 1)
								{
									ri = (colorAr * (256 - ai) +  ri * ai) >> 8;
									gi = (colorAg * (256 - ai) +  gi * ai) >> 8;
									bi = (colorAb * (256 - ai) +  bi * ai) >> 8;
								}		  
								else	  
								{		  
									ri = (colorBr * (256 - ai) +  ri * ai) >> 8;
									gi = (colorBg * (256 - ai) +  gi * ai) >> 8;
									bi = (colorBb * (256 - ai) +  bi * ai) >> 8;
								}
								ai = 0xff;
							}

							outA8[3] = ai;
							outA8[2] = ri;
							outA8[1] = gi;
							outA8[0] = bi;


							outA8+=4;
							sptr+=4;
						}
					}
					output += pitch;
				}
			}
			break;

		case COLOR_FORMAT_W13A: //TODO need own stuff
			if(whitepoint < 16)// assume white point is (1<13-1) && decoder->frame.white_point
			{
				//int totalpixel = width * 4;
				//int totalpixel8 = totalpixel & 0xfff8;
				short *outA16;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (short *)outA8;

					x=0;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{

						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								outA16[0] = signed_sptr[0];
								outA16[1] = signed_sptr[8];
								outA16[2] = signed_sptr[16];
								outA16[3] = signed_sptr[24];
								signed_sptr++;
								outA16+=4;
							}
							signed_sptr += 24;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							outA16[0] = signed_sptr[0];
							outA16[1] = signed_sptr[width];
							outA16[2] = signed_sptr[width*2];
							outA16[3] = signed_sptr[width*3];
							signed_sptr++;
							outA16+=4;
						}
					}
					else
					{
						for(x=0;x<width; x++)
						{
							outA16[0] = *signed_sptr++;
							outA16[1] = *signed_sptr++;
							outA16[2] = *signed_sptr++;
							outA16[3] = *signed_sptr++;

							outA16+=4;
						}
					}
					output += pitch;
				}
			}
			else //if(whitepoint == 16)
			{
				//int totalpixel = width * 4;
				//int totalpixel8 = totalpixel & 0xfff8;
				short *outA16;
				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (short *)outA8;

					x=0;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{

						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								outA16[0] = sptr[0]>>dnshiftto13bit;
								outA16[1] = sptr[8]>>dnshiftto13bit;
								outA16[2] = sptr[16]>>dnshiftto13bit;
								outA16[3] = sptr[24]>>dnshiftto13bit;
								sptr++;
								outA16+=4;
							}
							sptr += 24;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						for(x=0; x<width; x++)
						{
							outA16[0] = sptr[0]>>dnshiftto13bit;
							outA16[1] = sptr[width]>>dnshiftto13bit;
							outA16[2] = sptr[width*2]>>dnshiftto13bit;
							outA16[3] = sptr[width*3]>>dnshiftto13bit;
							sptr++;
							outA16+=4;
						}
					}
					else
					{
						for(x=0;x<width*4; x+=4)
						{
							outA16[0] = sptr[x+0]>>dnshiftto13bit;
							outA16[1] = sptr[x+1]>>dnshiftto13bit;
							outA16[2] = sptr[x+2]>>dnshiftto13bit;
							outA16[3] = sptr[x+3]>>dnshiftto13bit;

							outA16+=4;
						}
					}
					output += pitch;
				}
			}
			break;

		case COLOR_FORMAT_B64A: //TODO SSe2
		case COLOR_FORMAT_RG64:
			if(whitepoint < 16)// assume white point is (1<13-1) && decoder->frame.white_point
			{
				//int totalpixel = width * 4;
				//int totalpixel8 = totalpixel & 0xfff8;
				unsigned short *outA16;
				int shift = 16 - whitepoint;
				int ri,gi,bi,ai;

				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (unsigned short *)outA8;

					x=0;
					if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
					{
						for(x=0; x<width; x+=8)
						{
							int xx;
							for(xx=0;xx<8; xx++)
							{
								ri = signed_sptr[0]<<shift;
								gi = signed_sptr[8]<<shift;
								bi = signed_sptr[16]<<shift;
								ai = signed_sptr[24]<<shift;
								
								if(mixdown)
								{
									int xx,yy;
									xx = x / mixdownRes;
									yy = linenum / mixdownRes;
									ai>>=8;
									if((xx+yy) & 1)
									{
										ri = (colorAr * (256 - ai) +  ri * ai) >> 8;
										gi = (colorAg * (256 - ai) +  gi * ai) >> 8;
										bi = (colorAb * (256 - ai) +  bi * ai) >> 8;
									}		  
									else	  
									{		  
										ri = (colorBr * (256 - ai) +  ri * ai) >> 8;
										gi = (colorBg * (256 - ai) +  gi * ai) >> 8;
										bi = (colorBb * (256 - ai) +  bi * ai) >> 8;
									}
									ai = 0xffff;
								}

								if(ri > 65535) ri=65535; if(ri<0) ri=0;
								if(gi > 65535) gi=65535; if(gi<0) gi=0;
								if(bi > 65535) bi=65535; if(bi<0) bi=0;
								if(ai > 65535) ai=65535; if(ai<0) ai=0;

								if(COLOR_FORMAT_B64A == format)
								{
									outA16[0] = ai;
									outA16[1] = ri;
									outA16[2] = gi;
									outA16[3] = bi;
								}
								else
								{
									outA16[0] = ri;
									outA16[1] = gi;
									outA16[2] = bi;
									outA16[3] = ai;
								}

								signed_sptr++;
								outA16+=4;
							}
							signed_sptr += 24;
						}
					}
					else if(flags & ACTIVEMETADATA_PLANAR)
					{
						int shift = whitepoint - decoder->frame.white_point;

						for(x=0; x<width; x++)
						{
							ri = signed_sptr[0]<<shift;
							gi = signed_sptr[width]<<shift;
							bi = signed_sptr[width*2]<<shift;
							ai = signed_sptr[width*3]<<shift;

							if(mixdown)
							{
								int xx,yy;
								xx = x / mixdownRes;
								yy = linenum / mixdownRes;
								ai>>=8;
								if((xx+yy) & 1)
								{
									ri = (colorAr * (256 - ai) +  ri * ai) >> 8;
									gi = (colorAg * (256 - ai) +  gi * ai) >> 8;
									bi = (colorAb * (256 - ai) +  bi * ai) >> 8;
								}		  
								else	  
								{		  
									ri = (colorBr * (256 - ai) +  ri * ai) >> 8;
									gi = (colorBg * (256 - ai) +  gi * ai) >> 8;
									bi = (colorBb * (256 - ai) +  bi * ai) >> 8;
								}
								ai = 0xffff;
							}
							
							if(ri > 65535) ri=65535; if(ri<0) ri=0;
							if(gi > 65535) gi=65535; if(gi<0) gi=0;
							if(bi > 65535) bi=65535; if(bi<0) bi=0;
							if(ai > 65535) ai=65535; if(ai<0) ai=0;

							if(COLOR_FORMAT_B64A == format)
							{
								outA16[0] = ai;
								outA16[1] = ri;
								outA16[2] = gi;
								outA16[3] = bi;
							}
							else
							{
								outA16[0] = ri;
								outA16[1] = gi;
								outA16[2] = bi;
								outA16[3] = ai;
							}

							signed_sptr++;
							outA16+=4;
						}
					}
					else
					{
						int shift = 16-whitepoint;

						if(mixdown || COLOR_FORMAT_B64A == format)
						{
							for(x=0; x<width; x++)
							{
								ri = signed_sptr[0]<<shift;
								gi = signed_sptr[1]<<shift;
								bi = signed_sptr[2]<<shift;
								ai = signed_sptr[3]<<shift;

								if(mixdown)
								{
									int xx,yy;
									xx = x / mixdownRes;
									yy = linenum / mixdownRes;
									ai>>=8;

									if((xx+yy) & 1)
									{
										ri = (colorAr * (256 - ai) +  ri * ai) >> 8;
										gi = (colorAg * (256 - ai) +  gi * ai) >> 8;
										bi = (colorAb * (256 - ai) +  bi * ai) >> 8;
									}		  
									else	  
									{		  
										ri = (colorBr * (256 - ai) +  ri * ai) >> 8;
										gi = (colorBg * (256 - ai) +  gi * ai) >> 8;
										bi = (colorBb * (256 - ai) +  bi * ai) >> 8;
									}
									ai = 0xffff;
								}
								
								if(ri > 65535) ri=65535; if(ri<0) ri=0;
								if(gi > 65535) gi=65535; if(gi<0) gi=0;
								if(bi > 65535) bi=65535; if(bi<0) bi=0;
								if(ai > 65535) ai=65535; if(ai<0) ai=0;

								if(COLOR_FORMAT_B64A == format)
								{
									outA16[0] = ai;
									outA16[1] = ri;
									outA16[2] = gi;
									outA16[3] = bi;
								}
								else
								{
									outA16[0] = ri;
									outA16[1] = gi;
									outA16[2] = bi;
									outA16[3] = ai;
								}						
								
								

								signed_sptr+=4;
								outA16+=4;
							}
						}
						else
						{
							__m128i limiterRGB13 = _mm_set1_epi16(0x7fff - 0x1fff);

							for(x=0;x<width; x+=2)
							{
								__m128i rgba_epi16 = _mm_load_si128((__m128i *)signed_sptr); //13-bit signed
							
								rgba_epi16 = _mm_adds_epi16(rgba_epi16, limiterRGB13); //13-bit limit
								rgba_epi16 = _mm_subs_epu16(rgba_epi16, limiterRGB13);
								rgba_epi16 = _mm_slli_epi16(rgba_epi16, 3);  //13-bit

								_mm_store_si128((__m128i *)outA16, rgba_epi16);

								signed_sptr+=8;
								outA16+=8;
							}
						}
					}
					output += pitch;
				}
			}
			else //if(whitepoint == 16)
			{
				//int totalpixel = width * 4;
				//int totalpixel8 = totalpixel & 0xfff8;
				unsigned short *outA16;

				for(lines=0; lines<height; lines++)
				{
					outA8 = output;
					outA16 = (unsigned short *)outA8;

					x=0;
					if(mixdown)
					{
						int ri,gi,bi,ai;

						if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
						{
							for(x=0; x<width; x+=8)
							{								
								int xx;
								for(xx=0;xx<8; xx++)
								{
									ri = sptr[0]>>8;
									gi = sptr[8]>>8;
									bi = sptr[16]>>8;
									ai = sptr[24]>>8;

									{
										int xx,yy;
										xx = x / mixdownRes;
										yy = linenum / mixdownRes;
										if((xx+yy) & 1)
										{
											ri = (colorAr * (256 - ai) +  ri * ai);
											gi = (colorAg * (256 - ai) +  gi * ai);
											bi = (colorAb * (256 - ai) +  bi * ai);
										}		  
										else	  
										{		  
											ri = (colorBr * (256 - ai) +  ri * ai);
											gi = (colorBg * (256 - ai) +  gi * ai);
											bi = (colorBb * (256 - ai) +  bi * ai);
										}
										ai = 0xffff;
									}

									if(COLOR_FORMAT_B64A == format)
									{
										outA16[0] = ai;
										outA16[1] = ri;
										outA16[2] = gi;
										outA16[3] = bi;
									}
									else
									{
										outA16[0] = ri;
										outA16[1] = gi;
										outA16[2] = bi;
										outA16[3] = ai;
									}			
									sptr++;
									outA16+=4;
								}
								sptr += 24;
							}
						}
						else if(flags & ACTIVEMETADATA_PLANAR)
						{
							for(x=0; x<width; x++)
							{
								ri = sptr[0]>>8;
								gi = sptr[width]>>8;
								bi = sptr[width*2]>>8;
								ai = sptr[width*3]>>8;
								{
									int xx,yy;
									xx = x / mixdownRes;
									yy = linenum / mixdownRes;
									if((xx+yy) & 1)
									{
										ri = (colorAr * (256 - ai) +  ri * ai);
										gi = (colorAg * (256 - ai) +  gi * ai);
										bi = (colorAb * (256 - ai) +  bi * ai);
									}		  
									else	  
									{		  
										ri = (colorBr * (256 - ai) +  ri * ai);
										gi = (colorBg * (256 - ai) +  gi * ai);
										bi = (colorBb * (256 - ai) +  bi * ai);
									}
									ai = 0xffff;
								}

								if(COLOR_FORMAT_B64A == format)
								{
									outA16[0] = ai;
									outA16[1] = ri;
									outA16[2] = gi;
									outA16[3] = bi;
								}
								else
								{
									outA16[0] = ri;
									outA16[1] = gi;
									outA16[2] = bi;
									outA16[3] = ai;
								}			
								sptr++;
								outA16+=4;
							}
						}
						else
						{
							for(x=0;x<width; x++)
							{
								ri = sptr[0]>>8;
								gi = sptr[1]>>8;
								bi = sptr[2]>>8;
								ai = sptr[3]>>8;

								{
									int xx,yy;
									xx = x / mixdownRes;
									yy = linenum / mixdownRes;
									if((xx+yy) & 1)
									{
										ri = (colorAr * (256 - ai) +  ri * ai);
										gi = (colorAg * (256 - ai) +  gi * ai);
										bi = (colorAb * (256 - ai) +  bi * ai);
									}		  
									else	  
									{		  
										ri = (colorBr * (256 - ai) +  ri * ai);
										gi = (colorBg * (256 - ai) +  gi * ai);
										bi = (colorBb * (256 - ai) +  bi * ai);
									}
									ai = 0xffff;
								}

								if(COLOR_FORMAT_B64A == format)
								{
									outA16[0] = ai;
									outA16[1] = ri;
									outA16[2] = gi;
									outA16[3] = bi;
								}
								else
								{
									outA16[0] = ri;
									outA16[1] = gi;
									outA16[2] = bi;
									outA16[3] = ai;
								}			

								sptr+=4;
								outA16+=4;
							}
						}
						output += pitch;
					}
					else
					{
						if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
						{
							for(x=0; x<width; x+=8)
							{
								int xx;
								for(xx=0;xx<8; xx++)
								{
									if(COLOR_FORMAT_B64A == format)
									{
										outA16[0] = sptr[24];
										outA16[1] = sptr[0];
										outA16[2] = sptr[8];
										outA16[3] = sptr[16];
									}
									else
									{
										outA16[0] = sptr[0];
										outA16[1] = sptr[8];
										outA16[2] = sptr[16];
										outA16[3] = sptr[24];
									}			
									sptr++;
									outA16+=4;
								}
								sptr += 24;
							}
						}
						else if(flags & ACTIVEMETADATA_PLANAR)
						{
							for(x=0; x<width; x++)
							{
								if(COLOR_FORMAT_B64A == format)
								{
									outA16[0] = sptr[width*3];
									outA16[1] = sptr[0];
									outA16[2] = sptr[width];
									outA16[3] = sptr[width*2];
								}
								else
								{
									outA16[0] = sptr[0];
									outA16[1] = sptr[width];
									outA16[2] = sptr[width*2];
									outA16[3] = sptr[width*3];
								}
								sptr++;
								outA16+=4;
							}
						}
						else
						{
							if(COLOR_FORMAT_B64A == format)
							{
								for(x=0; x<width; x++)
								{
									outA16[0] = sptr[3];
									outA16[1] = sptr[0];
									outA16[2] = sptr[1];
									outA16[3] = sptr[2];
									
									sptr+=4;
									outA16+=4;
								}
							}
							else
							{
								for(x=0;x<width; x+=2)
								{
									__m128i rgba_epi16 = _mm_load_si128((__m128i *)sptr); //13-bit signed
									_mm_store_si128((__m128i *)outA16, rgba_epi16);

									sptr+=8;
									outA16+=8;
								}
							}
						}
						output += pitch;
					}
				}
			}
			break;

		case COLOR_FORMAT_R408:
		case COLOR_FORMAT_V408:
			{
				//int lines,y, y2, u, v, r, g, b, r2, g2, b2;

				if(colorformatdone)
				{
					__m128i yyyyyyyy1;
					__m128i uuuuuuuu1;
					__m128i vvvvvvvv1;
					__m128i aaaaaaaa1;
					__m128i yyyyyyyy2;
					__m128i uuuuuuuu2;
					__m128i vvvvvvvv2;
					__m128i aaaaaaaa2;
					__m128i offsetR408_epi8 =  _mm_set1_epi8(16);
					__m128i y_epi8;
					__m128i u_epi8;
					__m128i v_epi8;
					__m128i a_epi8;
					//__m128i tttttttt;
					__m128i ditheryy =  _mm_set1_epi16(0);
					__m128i ditheruu =  _mm_set1_epi16(0);
					__m128i dithervv =  _mm_set1_epi16(0);


					for(lines=linenum; lines<linenum+height; lines++)
					{
						int width16 = (width>>4)<<4;
						__m128i *out_epi8 = (__m128i *)output;
						outA8 = output;

						if(lines & 1)
						{
							ditheryy = _mm_set_epi16( 2, 30,  6, 26, 10, 22, 14, 18); // 5 bits of dither
							ditheruu = _mm_set_epi16( 2, 30,  6, 26, 10, 22, 14, 18);
							dithervv = _mm_set_epi16(18, 14, 22, 10, 26,  6, 30,  2);
						}
						else
						{
							ditheryy = _mm_set_epi16(18, 14, 22, 10, 26,  6, 30,  2);
							ditheruu = _mm_set_epi16(18, 14, 22, 10, 26,  6, 30,  2);
							dithervv = _mm_set_epi16( 2, 30,  6, 26, 10, 22, 14, 18);
						}

						for(x=0; x<width16; x+=16)
						{
							if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
							{
								yyyyyyyy1 = _mm_loadu_si128((__m128i *)&sptr[0]);
								uuuuuuuu1 = _mm_loadu_si128((__m128i *)&sptr[8]);
								vvvvvvvv1 = _mm_loadu_si128((__m128i *)&sptr[16]);
								aaaaaaaa1 = _mm_loadu_si128((__m128i *)&sptr[24]);
								yyyyyyyy2 = _mm_loadu_si128((__m128i *)&sptr[32]);
								uuuuuuuu2 = _mm_loadu_si128((__m128i *)&sptr[40]);
								vvvvvvvv2 = _mm_loadu_si128((__m128i *)&sptr[48]);
								aaaaaaaa2 = _mm_loadu_si128((__m128i *)&sptr[56]);
								sptr+=64;
							}
							else if(flags & ACTIVEMETADATA_PLANAR)
							{
								yyyyyyyy1 = _mm_loadu_si128((__m128i *)&sptr[0]);
								uuuuuuuu1 = _mm_loadu_si128((__m128i *)&sptr[width]);
								vvvvvvvv1 = _mm_loadu_si128((__m128i *)&sptr[width*2]);
								aaaaaaaa1 = _mm_loadu_si128((__m128i *)&sptr[width*3]);
								sptr+=8;
								yyyyyyyy2 = _mm_loadu_si128((__m128i *)&sptr[0]);
								uuuuuuuu2 = _mm_loadu_si128((__m128i *)&sptr[width]);
								vvvvvvvv2 = _mm_loadu_si128((__m128i *)&sptr[width*2]);
								aaaaaaaa2 = _mm_loadu_si128((__m128i *)&sptr[width*3]);
								sptr+=8;
							}
							else
							{
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[0],  0);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[1],  0);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[2],  0);
								aaaaaaaa1 = _mm_insert_epi16(aaaaaaaa1, sptr[3],  0);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[4],  1);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[5],  1);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[6],  1);
								aaaaaaaa1 = _mm_insert_epi16(aaaaaaaa1, sptr[7],  1);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[8],  2);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[9],  2);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[10], 2);
								aaaaaaaa1 = _mm_insert_epi16(aaaaaaaa1, sptr[11], 2);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[12], 3);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[13], 3);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[14], 3);
								aaaaaaaa1 = _mm_insert_epi16(aaaaaaaa1, sptr[15], 3);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[16], 4);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[17], 4);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[18], 4);
								aaaaaaaa1 = _mm_insert_epi16(aaaaaaaa1, sptr[19], 4);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[20], 5);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[21], 5);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[22], 5);
								aaaaaaaa1 = _mm_insert_epi16(aaaaaaaa1, sptr[23], 5);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[24], 6);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[25], 6);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[26], 6);
								aaaaaaaa1 = _mm_insert_epi16(aaaaaaaa1, sptr[27], 6);
								yyyyyyyy1 = _mm_insert_epi16(yyyyyyyy1, sptr[28], 7);
								uuuuuuuu1 = _mm_insert_epi16(uuuuuuuu1, sptr[29], 7);
								vvvvvvvv1 = _mm_insert_epi16(vvvvvvvv1, sptr[30], 7);
								aaaaaaaa1 = _mm_insert_epi16(aaaaaaaa1, sptr[31], 7);
								sptr+=32;

								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[0],  0);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[1],  0);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[2],  0);
								aaaaaaaa2 = _mm_insert_epi16(aaaaaaaa2, sptr[3],  0);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[4],  1);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[5],  1);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[6],  1);
								aaaaaaaa2 = _mm_insert_epi16(aaaaaaaa2, sptr[7],  1);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[8],  2);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[9],  2);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[10], 2);
								aaaaaaaa2 = _mm_insert_epi16(aaaaaaaa2, sptr[11], 2);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[12], 3);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[13], 3);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[14], 3);
								aaaaaaaa2 = _mm_insert_epi16(aaaaaaaa2, sptr[15], 3);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[16], 4);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[17], 4);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[18], 4);
								aaaaaaaa2 = _mm_insert_epi16(aaaaaaaa2, sptr[19], 4);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[20], 5);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[21], 5);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[22], 5);
								aaaaaaaa2 = _mm_insert_epi16(aaaaaaaa2, sptr[23], 5);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[24], 6);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[25], 6);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[26], 6);
								aaaaaaaa2 = _mm_insert_epi16(aaaaaaaa2, sptr[27], 6);
								yyyyyyyy2 = _mm_insert_epi16(yyyyyyyy2, sptr[28], 7);
								uuuuuuuu2 = _mm_insert_epi16(uuuuuuuu2, sptr[29], 7);
								vvvvvvvv2 = _mm_insert_epi16(vvvvvvvv2, sptr[30], 7);
								aaaaaaaa2 = _mm_insert_epi16(aaaaaaaa2, sptr[31], 7);
								sptr+=32;
							}

							yyyyyyyy1 = _mm_srli_epi16(yyyyyyyy1, dnshiftto13bit);
							yyyyyyyy1 = _mm_adds_epi16(yyyyyyyy1, ditheryy);
							yyyyyyyy1 = _mm_srai_epi16(yyyyyyyy1, 5);
							uuuuuuuu1 = _mm_srli_epi16(uuuuuuuu1, dnshiftto13bit);
							uuuuuuuu1 = _mm_adds_epi16(uuuuuuuu1, ditheruu);
							uuuuuuuu1 = _mm_srai_epi16(uuuuuuuu1, 5);
							vvvvvvvv1 = _mm_srli_epi16(vvvvvvvv1, dnshiftto13bit);
							vvvvvvvv1 = _mm_adds_epi16(vvvvvvvv1, dithervv);
							vvvvvvvv1 = _mm_srai_epi16(vvvvvvvv1, 5);
							aaaaaaaa1 = _mm_srli_epi16(aaaaaaaa1, dnshiftto13bit);
							aaaaaaaa1 = _mm_srai_epi16(aaaaaaaa1, 5);


							yyyyyyyy2 = _mm_srli_epi16(yyyyyyyy2, dnshiftto13bit);
							yyyyyyyy2 = _mm_adds_epi16(yyyyyyyy2, ditheryy);
							yyyyyyyy2 = _mm_srai_epi16(yyyyyyyy2, 5);
							uuuuuuuu2 = _mm_srli_epi16(uuuuuuuu2, dnshiftto13bit);
							uuuuuuuu2 = _mm_adds_epi16(uuuuuuuu2, ditheruu);
							uuuuuuuu2 = _mm_srai_epi16(uuuuuuuu2, 5);
							vvvvvvvv2 = _mm_srli_epi16(vvvvvvvv2, dnshiftto13bit);
							vvvvvvvv2 = _mm_adds_epi16(vvvvvvvv2, dithervv);
							vvvvvvvv2 = _mm_srai_epi16(vvvvvvvv2, 5);
							aaaaaaaa2 = _mm_srli_epi16(aaaaaaaa2, dnshiftto13bit);
							aaaaaaaa2 = _mm_srai_epi16(aaaaaaaa2, 5);

							y_epi8 = _mm_packus_epi16(yyyyyyyy1, yyyyyyyy2); //pack to 8-bit
							u_epi8 = _mm_packus_epi16(uuuuuuuu1, uuuuuuuu2); //pack to 8-bit
							v_epi8 = _mm_packus_epi16(vvvvvvvv1, vvvvvvvv2); //pack to 8-bit
							a_epi8 = _mm_packus_epi16(aaaaaaaa1, aaaaaaaa2); //pack to 8-bit

							if(format == COLOR_FORMAT_V408) // UYVA
							{
								__m128i UY,VA,UYVA;

								UY = _mm_unpacklo_epi8(u_epi8, y_epi8);
								VA = _mm_unpacklo_epi8(v_epi8, a_epi8);
								UYVA = _mm_unpacklo_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

								UYVA = _mm_unpackhi_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

								UY = _mm_unpackhi_epi8(u_epi8, y_epi8);
								VA = _mm_unpackhi_epi8(v_epi8, a_epi8);
								UYVA = _mm_unpacklo_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

								UYVA = _mm_unpackhi_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);
							}
							else //r408 AYUV
							{
								__m128i AY,UV,AYUV;

								y_epi8 = _mm_subs_epu8(y_epi8, offsetR408_epi8);

								AY = _mm_unpacklo_epi8(a_epi8, y_epi8);
								UV = _mm_unpacklo_epi8(u_epi8, v_epi8);
								AYUV = _mm_unpacklo_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);

								AYUV = _mm_unpackhi_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);

								AY = _mm_unpackhi_epi8(a_epi8, y_epi8);
								UV = _mm_unpackhi_epi8(u_epi8, v_epi8);
								AYUV = _mm_unpacklo_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);

								AYUV = _mm_unpackhi_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);
							}
						}

						//for(; x<width; x++) //TODO
						//{
						//}
						if(flags & ACTIVEMETADATA_PLANAR)
						{
							sptr += width*3;
						}
						output += pitch;
					}
				}
				else
				{
					//int lines,y, y2, u, v, r, g, b, r2, g2, b2;
	#if 1
					__m128i rrrrrrrr = _mm_set1_epi16(0);
					__m128i gggggggg = _mm_set1_epi16(0);
					__m128i bbbbbbbb = _mm_set1_epi16(0);
					__m128i aaaaaaaa = _mm_set1_epi16(0);
					__m128i yyyyyyyy1;
					__m128i uuuuuuuu1;
					__m128i vvvvvvvv1;
					__m128i aaaaaaaa1;
					__m128i yyyyyyyy2;
					__m128i uuuuuuuu2;
					__m128i vvvvvvvv2;
					__m128i aaaaaaaa2;
					__m128i offsetR408_epi8 =  _mm_set1_epi8(16);
					__m128i y_epi8;
					__m128i u_epi8;
					__m128i v_epi8;
					__m128i a_epi8;
					__m128i tttttttt;
					__m128i ditheryy =  _mm_set1_epi16(0);
					__m128i ditheruu =  _mm_set1_epi16(0);
					__m128i dithervv =  _mm_set1_epi16(0);

					__m128i overflowprotectRGB_epi16 = _mm_set1_epi16(0x7fff-0x1fff);


					for(lines=linenum; lines<linenum+height; lines++)
					{
						__m128i *out_epi8 = (__m128i *)output;\
						int width16 = (width >> 4) << 4;
						outA8 = output;

						if(cg2vs)
						{
							ConvertCGRGBAtoVSRGBA((PIXEL *)sptr, width, whitepoint, flags);
						}

						if(lines & 1)
						{
							ditheryy = _mm_set_epi16( 1, 15,  3, 13,  5, 11,  7,  9); // 5 bits of dither
							ditheruu = _mm_set_epi16( 1, 15,  3, 13,  5, 11,  7,  9);
							dithervv = _mm_set_epi16( 9,  7, 11,  5, 13,  3, 15,  1);
						}
						else
						{
							ditheryy = _mm_set_epi16( 9,  7, 11,  5, 13,  3, 15,  1);
							ditheruu = _mm_set_epi16( 9,  7, 11,  5, 13,  3, 15,  1);
							dithervv = _mm_set_epi16( 1, 15,  3, 13,  5, 11,  7,  9);
						}

						for(x=0; x<width16; x+=16)
						{
							
							if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
							{
								rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[0]);
								gggggggg = _mm_loadu_si128((__m128i *)&sptr[8]);
								bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[16]);
								aaaaaaaa = _mm_loadu_si128((__m128i *)&sptr[24]);
								rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[32]);
								gggggggg = _mm_loadu_si128((__m128i *)&sptr[40]);
								bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[48]);
								aaaaaaaa = _mm_loadu_si128((__m128i *)&sptr[56]);
								sptr+=64;
							}
							else if(flags & ACTIVEMETADATA_PLANAR)
							{
								rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[0]);
								gggggggg = _mm_loadu_si128((__m128i *)&sptr[width]);
								bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[width*2]);
								aaaaaaaa = _mm_loadu_si128((__m128i *)&sptr[width*3]);
								sptr+=8;
							}
							else
							{								
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[0],  0);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[1],  0);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[2],  0);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[3],  0);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[4],  1);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[5],  1);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[6],  1);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[7],  1);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[8],  2);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[9],  2);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[10], 2);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[11], 2);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[12], 3);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[13], 3);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[14], 3);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[15], 3);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[16], 4);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[17], 4);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[18], 4);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[19], 4);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[20], 5);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[21], 5);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[22], 5);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[23], 5);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[24], 6);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[25], 6);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[26], 6);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[27], 6);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[28], 7);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[29], 7);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[30], 7);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[31], 7);
								sptr+=32;
							}

							if(dnshiftto13bit < 0)
							{
								rrrrrrrr = _mm_slli_epi16(rrrrrrrr, -dnshiftto13bit);		//13-bit
								gggggggg = _mm_slli_epi16(gggggggg, -dnshiftto13bit);
								bbbbbbbb = _mm_slli_epi16(bbbbbbbb, -dnshiftto13bit);
								aaaaaaaa = _mm_slli_epi16(aaaaaaaa, -dnshiftto13bit);
							}
							else if(whitepoint == 16)
							{
								rrrrrrrr = _mm_srli_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
								gggggggg = _mm_srli_epi16(gggggggg, dnshiftto13bit);
								bbbbbbbb = _mm_srli_epi16(bbbbbbbb, dnshiftto13bit);
								aaaaaaaa = _mm_srli_epi16(aaaaaaaa, dnshiftto13bit);
							}
							else
							{
								rrrrrrrr = _mm_srai_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
								gggggggg = _mm_srai_epi16(gggggggg, dnshiftto13bit);
								bbbbbbbb = _mm_srai_epi16(bbbbbbbb, dnshiftto13bit);
								aaaaaaaa = _mm_srai_epi16(aaaaaaaa, dnshiftto13bit);
							}

							if(saturate)
							{
								rrrrrrrr = _mm_adds_epi16(rrrrrrrr, overflowprotectRGB_epi16);
								rrrrrrrr = _mm_subs_epu16(rrrrrrrr, overflowprotectRGB_epi16);

								gggggggg = _mm_adds_epi16(gggggggg, overflowprotectRGB_epi16);
								gggggggg = _mm_subs_epu16(gggggggg, overflowprotectRGB_epi16);

								bbbbbbbb = _mm_adds_epi16(bbbbbbbb, overflowprotectRGB_epi16);
								bbbbbbbb = _mm_subs_epu16(bbbbbbbb, overflowprotectRGB_epi16);

								aaaaaaaa = _mm_adds_epi16(aaaaaaaa, overflowprotectRGB_epi16);
								aaaaaaaa = _mm_subs_epu16(aaaaaaaa, overflowprotectRGB_epi16);
							}

							yyyyyyyy1 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(y_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(y_gmult));
							yyyyyyyy1 = _mm_adds_epi16(yyyyyyyy1, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(y_bmult));
							yyyyyyyy1 = _mm_adds_epi16(yyyyyyyy1, tttttttt);
							yyyyyyyy1 = _mm_adds_epi16(yyyyyyyy1, ditheryy);
							yyyyyyyy1 = _mm_srai_epi16(yyyyyyyy1, 4);
							yyyyyyyy1 = _mm_adds_epi16(yyyyyyyy1, _mm_set1_epi16(yoffset));

							uuuuuuuu1 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(u_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(u_gmult));
							uuuuuuuu1 = _mm_adds_epi16(uuuuuuuu1, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(u_bmult));
							uuuuuuuu1 = _mm_adds_epi16(uuuuuuuu1, tttttttt);
							uuuuuuuu1 = _mm_adds_epi16(uuuuuuuu1, ditheruu);
							uuuuuuuu1 = _mm_srai_epi16(uuuuuuuu1, 4);
							uuuuuuuu1 = _mm_adds_epi16(uuuuuuuu1, _mm_set1_epi16(128));

							vvvvvvvv1 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(v_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(v_gmult));
							vvvvvvvv1 = _mm_adds_epi16(vvvvvvvv1, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(v_bmult));
							vvvvvvvv1 = _mm_adds_epi16(vvvvvvvv1, tttttttt);
							vvvvvvvv1 = _mm_adds_epi16(vvvvvvvv1, dithervv);
							vvvvvvvv1 = _mm_srai_epi16(vvvvvvvv1, 4);
							vvvvvvvv1 = _mm_adds_epi16(vvvvvvvv1, _mm_set1_epi16(128));

							aaaaaaaa1 = _mm_srai_epi16(aaaaaaaa, 5); // 8-bit


							if(flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
							{
								rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[0]);
								gggggggg = _mm_loadu_si128((__m128i *)&sptr[8]);
								bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[16]);
								aaaaaaaa = _mm_loadu_si128((__m128i *)&sptr[24]);
								rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[32]);
								gggggggg = _mm_loadu_si128((__m128i *)&sptr[40]);
								bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[48]);
								aaaaaaaa = _mm_loadu_si128((__m128i *)&sptr[56]);
								sptr+=64;
							}
							else if(flags & ACTIVEMETADATA_PLANAR)
							{
								rrrrrrrr = _mm_loadu_si128((__m128i *)&sptr[0]);
								gggggggg = _mm_loadu_si128((__m128i *)&sptr[width]);
								bbbbbbbb = _mm_loadu_si128((__m128i *)&sptr[width*2]);
								aaaaaaaa = _mm_loadu_si128((__m128i *)&sptr[width*3]);
								sptr+=8;
							}
							else
							{
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[0],  0);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[1],  0);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[2],  0);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[3],  0);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[4],  1);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[5],  1);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[6],  1);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[7],  1);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[8],  2);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[9],  2);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[10], 2);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[11], 2);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[12], 3);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[13], 3);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[14], 3);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[15], 3);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[16], 4);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[17], 4);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[18], 4);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[19], 4);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[20], 5);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[21], 5);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[22], 5);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[23], 5);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[24], 6);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[25], 6);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[26], 6);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[27], 6);
								rrrrrrrr = _mm_insert_epi16(rrrrrrrr, sptr[28], 7);
								gggggggg = _mm_insert_epi16(gggggggg, sptr[29], 7);
								bbbbbbbb = _mm_insert_epi16(bbbbbbbb, sptr[30], 7);
								aaaaaaaa = _mm_insert_epi16(aaaaaaaa, sptr[31], 7);
								sptr+=32;
							}

							if(dnshiftto13bit < 0)
							{
								rrrrrrrr = _mm_slli_epi16(rrrrrrrr, -dnshiftto13bit);		//13-bit
								gggggggg = _mm_slli_epi16(gggggggg, -dnshiftto13bit);
								bbbbbbbb = _mm_slli_epi16(bbbbbbbb, -dnshiftto13bit);
								aaaaaaaa = _mm_slli_epi16(aaaaaaaa, -dnshiftto13bit);
							}
							else if(whitepoint == 16)
							{
								rrrrrrrr = _mm_srli_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
								gggggggg = _mm_srli_epi16(gggggggg, dnshiftto13bit);
								bbbbbbbb = _mm_srli_epi16(bbbbbbbb, dnshiftto13bit);
								aaaaaaaa = _mm_srli_epi16(aaaaaaaa, dnshiftto13bit);
							}
							else
							{
								rrrrrrrr = _mm_srai_epi16(rrrrrrrr, dnshiftto13bit);		//13-bit
								gggggggg = _mm_srai_epi16(gggggggg, dnshiftto13bit);
								bbbbbbbb = _mm_srai_epi16(bbbbbbbb, dnshiftto13bit);
								aaaaaaaa = _mm_srai_epi16(aaaaaaaa, dnshiftto13bit);
							}

							if(saturate)
							{
								rrrrrrrr = _mm_adds_epi16(rrrrrrrr, overflowprotectRGB_epi16);
								rrrrrrrr = _mm_subs_epu16(rrrrrrrr, overflowprotectRGB_epi16);

								gggggggg = _mm_adds_epi16(gggggggg, overflowprotectRGB_epi16);
								gggggggg = _mm_subs_epu16(gggggggg, overflowprotectRGB_epi16);

								bbbbbbbb = _mm_adds_epi16(bbbbbbbb, overflowprotectRGB_epi16);
								bbbbbbbb = _mm_subs_epu16(bbbbbbbb, overflowprotectRGB_epi16);

								aaaaaaaa = _mm_adds_epi16(aaaaaaaa, overflowprotectRGB_epi16);
								aaaaaaaa = _mm_subs_epu16(aaaaaaaa, overflowprotectRGB_epi16);
							}

							yyyyyyyy2 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(y_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(y_gmult));
							yyyyyyyy2 = _mm_adds_epi16(yyyyyyyy2, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(y_bmult));
							yyyyyyyy2 = _mm_adds_epi16(yyyyyyyy2, tttttttt);
							yyyyyyyy2 = _mm_adds_epi16(yyyyyyyy2, ditheryy);
							yyyyyyyy2 = _mm_srai_epi16(yyyyyyyy2, 4);
							yyyyyyyy2 = _mm_adds_epi16(yyyyyyyy2, _mm_set1_epi16(yoffset));

							uuuuuuuu2 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(u_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(u_gmult));
							uuuuuuuu2 = _mm_adds_epi16(uuuuuuuu2, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(u_bmult));
							uuuuuuuu2 = _mm_adds_epi16(uuuuuuuu2, tttttttt);
							uuuuuuuu2 = _mm_adds_epi16(uuuuuuuu2, ditheruu);
							uuuuuuuu2 = _mm_srai_epi16(uuuuuuuu2, 4);
							uuuuuuuu2 = _mm_adds_epi16(uuuuuuuu2, _mm_set1_epi16(128));

							vvvvvvvv2 = _mm_mulhi_epi16(rrrrrrrr, _mm_set1_epi16(v_rmult)); //15 bit
							tttttttt = _mm_mulhi_epi16(gggggggg, _mm_set1_epi16(v_gmult));
							vvvvvvvv2 = _mm_adds_epi16(vvvvvvvv2, tttttttt);
							tttttttt = _mm_mulhi_epi16(bbbbbbbb, _mm_set1_epi16(v_bmult));
							vvvvvvvv2 = _mm_adds_epi16(vvvvvvvv2, tttttttt);
							vvvvvvvv2 = _mm_adds_epi16(vvvvvvvv2, dithervv);
							vvvvvvvv2 = _mm_srai_epi16(vvvvvvvv2, 4);
							vvvvvvvv2 = _mm_adds_epi16(vvvvvvvv2, _mm_set1_epi16(128));
							
							aaaaaaaa2 = _mm_srai_epi16(aaaaaaaa, 5); // 8-bit



							y_epi8 = _mm_packus_epi16(yyyyyyyy1, yyyyyyyy2); //pack to 8-bit
							u_epi8 = _mm_packus_epi16(uuuuuuuu1, uuuuuuuu2); //pack to 8-bit
							v_epi8 = _mm_packus_epi16(vvvvvvvv1, vvvvvvvv2); //pack to 8-bit
							a_epi8 = _mm_packus_epi16(aaaaaaaa1, aaaaaaaa2); //pack to 8-bit

							if(format == COLOR_FORMAT_V408) // UYVA
							{
								__m128i UY,VA,UYVA;

								UY = _mm_unpacklo_epi8(u_epi8, y_epi8);
								VA = _mm_unpacklo_epi8(v_epi8, a_epi8);
								UYVA = _mm_unpacklo_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

								UYVA = _mm_unpackhi_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

								UY = _mm_unpackhi_epi8(u_epi8, y_epi8);
								VA = _mm_unpackhi_epi8(v_epi8, a_epi8);
								UYVA = _mm_unpacklo_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

								UYVA = _mm_unpackhi_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);
							}
							else //r408 AYUV
							{
								__m128i AY,UV,AYUV;

								y_epi8 = _mm_subs_epu8(y_epi8, offsetR408_epi8);

								AY = _mm_unpacklo_epi8(a_epi8, y_epi8);
								UV = _mm_unpacklo_epi8(u_epi8, v_epi8);
								AYUV = _mm_unpacklo_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);

								AYUV = _mm_unpackhi_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);

								AY = _mm_unpackhi_epi8(a_epi8, y_epi8);
								UV = _mm_unpackhi_epi8(u_epi8, v_epi8);
								AYUV = _mm_unpacklo_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);

								AYUV = _mm_unpackhi_epi16(AY, UV);
								_mm_storeu_si128(out_epi8++, AYUV);
							}
						}

						for(; x<width; x+=4)
						{
							//TODO not fill with black.
							if(format == COLOR_FORMAT_V408) // UYVA
							{
								__m128i UY,VA,UYVA;


								UY = _mm_unpacklo_epi8(_mm_set1_epi8(128), _mm_set1_epi8(16));
								VA = _mm_unpacklo_epi8(_mm_set1_epi8(128), _mm_set1_epi8(255));
								UYVA = _mm_unpacklo_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);

							}
							else //r408 AYUV
							{
								__m128i UY,VA,UYVA;

								UY = _mm_unpacklo_epi8(_mm_set1_epi8(128), _mm_set1_epi8(0));
								VA = _mm_unpacklo_epi8(_mm_set1_epi8(128), _mm_set1_epi8(255));
								UYVA = _mm_unpacklo_epi16(UY, VA);
								_mm_storeu_si128(out_epi8++, UYVA);
							}
						}

						if(flags & ACTIVEMETADATA_PLANAR)
						{
							sptr += width*3;
						}
						output += pitch;
					}
	#else
					assert(0) // old code disabled
#endif
				}
			}
			break;

		default:
			assert(0);
			break;
	}
}



//unsigned short *ApplyActiveMetaData(DECODER *decoder, int width, int height, int ypos, unsigned short *src, unsigned short *dst, int colorformat, int *whitebitdepth, int *flags)
void *ApplyActiveMetaData4444(DECODER *decoder, int width, int height, int ypos,
						  uint32_t *src, uint32_t *dst, int colorformat, int *whitebitdepth,
						  int *flags)
{
	CFHDDATA *cfhddata = &decoder->cfhddata;
	//CFHDDATA *Cube_cfhddata = &decoder->Cube_cfhddata;
	short *RawCube = decoder->RawCube;
	int process_path_flags = cfhddata->process_path_flags;
	int cube_base = decoder->cube_base;
	int cube_depth = ((1<<cube_base)+1);
	int cube_shift_dn = (16-cube_base);
	int cube_depth_mask = ((1<<cube_shift_dn)-1);

	if(cfhddata->process_path_flags_mask)
	{
		process_path_flags &= cfhddata->process_path_flags_mask;
		if((cfhddata->process_path_flags_mask & 0xffff) == 7) // CM+WB+ACTIVE hack to force CM on
		{
			process_path_flags |= PROCESSING_COLORMATRIX|PROCESSING_ACTIVE;  // DAN20080225
		}
	}
	if((process_path_flags == 0 || process_path_flags == PROCESSING_ACTIVE) && cfhddata->encode_curve == cfhddata->decode_curve) //nothing on
	{
		if(*flags & ACTIVEMETADATA_PLANAR)
		{
int lines;
			for(lines=0; lines<height; lines++)
			{
				int x;
				//int xx;
				uint16_t *rgb = (uint16_t *)src;
				uint16_t *rptr = rgb;
				uint16_t *gptr = &rgb[width];
				uint16_t *bptr = &rgb[width*2];
				uint16_t *aptr = &rgb[width*3];
				int16_t *rgbout = (int16_t *)dst;

				if(decoder->RGBFilterBufferPhase == 1) // decoder order
				{
					gptr = rgb;
					rptr = &rgb[width];
					bptr = &rgb[width*2];
				}

				rgb += width*lines*4;
				rptr += width*lines*4;
				gptr += width*lines*4;
				bptr += width*lines*4;
				rgbout += width*lines*4;

				for(x=0;x<width; x++)
				{
					*rgbout++ = *rptr++;
					*rgbout++ = *gptr++;
					*rgbout++ = *bptr++;
					*rgbout++ = *aptr++;
				}
			}
			if(*whitebitdepth == 16 || *whitebitdepth==0)  // might be signed 13 bit.
			{
				*whitebitdepth = 16;
				*flags = ACTIVEMETADATA_PRESATURATED;
			}
			return dst;
		}
		else if(*flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
		{
			int lines;

			*flags &= ~ACTIVEMETADATA_SRC_8PIXEL_PLANAR;

			for(lines=0; lines<height; lines++)
			{
				int x,xx;
				short *rgb = (short *)src;
				short *rgbout = (short *)dst;

				rgb += width*lines*4;
				rgbout += width*lines*4;

				for(x=0;x<width; x+=8)
				{
					short *rgbsegment = rgb;

					rgb+= 8*4;

					for(xx=0;xx<8; xx++)
					{
						int ri,gi,bi,ai;
						ri = rgbsegment[0];
						gi = rgbsegment[8];
						bi = rgbsegment[16];
						ai = rgbsegment[24];
						rgbsegment++;

						*rgbout++ = ri;
						*rgbout++ = gi;
						*rgbout++ = bi;
						*rgbout++ = ai;
					}
				}
			}

			if(*whitebitdepth == 16 || *whitebitdepth==0)  // might be signed 13 bit.
			{
				*whitebitdepth = 16;
				*flags = ACTIVEMETADATA_PRESATURATED;
			}
			return dst;
		}
		else
		{
			if(*whitebitdepth == 16 || *whitebitdepth==0)  // might be signed 13 bit.
			{
				*whitebitdepth = 16;
				*flags = ACTIVEMETADATA_PRESATURATED;
			}
			return src;
		}
	}
	else if(process_path_flags & PROCESSING_LOOK_FILE && RawCube)
	{
		int lines;
		short *cube = RawCube;
		//int convert2YUV = 0;
		//if(LUTYUV(colorformat))
		//	convert2YUV = 1;


		for(lines=0; lines<height; lines++)
		{
			int x,xx,y = lines;
			unsigned short *rgb = (unsigned short *)src;
			short *rgbout = (short *)dst;

			rgb += width*y*4;
			rgbout += width*lines*4;

			if(*flags & ACTIVEMETADATA_PLANAR)
			{
				unsigned short *rptr,*gptr,*bptr,*aptr;

				rptr = rgb;
				gptr = &rgb[width];
				bptr = &rgb[width*2];
				aptr = &rgb[width*3];

				if(decoder->RGBFilterBufferPhase == 1) // decoder order
				{
					gptr = rgb;
					rptr = &rgb[width];
					bptr = &rgb[width*2];
					aptr = &rgb[width*3];
				}

				if(*whitebitdepth == 0 || *whitebitdepth == 16)
				{
					for(x=0;x<width; x++)
					{
						int ri,gi,bi,ai;
						int rmix,gmix,bmix;
						int rmixd,gmixd,bmixd;
						short *sptr;
						ri = *rptr++;
						gi = *gptr++;
						bi = *bptr++;
						ai = *aptr++;

						rmix = (ri & cube_depth_mask);
						gmix = (gi & cube_depth_mask);
						bmix = (bi & cube_depth_mask);

						ri>>=cube_shift_dn;
						gi>>=cube_shift_dn;
						bi>>=cube_shift_dn;

						sptr = &cube[(bi*cube_depth*cube_depth + gi*cube_depth + ri)*3];

						rmixd = cube_depth_mask+1 - rmix;
						gmixd = cube_depth_mask+1 - gmix;
						bmixd = cube_depth_mask+1 - bmix;

						ri = ((((((sptr[0]*rmixd + sptr[3]*rmix)>>cube_shift_dn)*gmixd +
							((sptr[cube_depth*3]*rmixd + sptr[cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
							(((((sptr[cube_depth*cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+3]*rmix)>>cube_shift_dn)*gmixd +
							((sptr[cube_depth*cube_depth*3+cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

						gi = ((((((sptr[1]*rmixd + sptr[4]*rmix)>>cube_shift_dn)*gmixd +
							((sptr[cube_depth*3+1]*rmixd + sptr[cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
							(((((sptr[cube_depth*cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+4]*rmix)>>cube_shift_dn)*gmixd +
							((sptr[cube_depth*cube_depth*3+cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

						bi = ((((((sptr[2]*rmixd + sptr[5]*rmix)>>cube_shift_dn)*gmixd +
							((sptr[cube_depth*3+2]*rmixd + sptr[cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
							(((((sptr[cube_depth*cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+5]*rmix)>>cube_shift_dn)*gmixd +
							((sptr[cube_depth*cube_depth*3+cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

						/*if(convert2YUV)
						{
							rgbout[0] = ri;
							rgbout[width] = gi;
							rgbout[width*2] = bi;
							rgbout[width*3] = ai>>3; // 13-bit
							rgbout++;
						}
						else */
						{
							*rgbout++ = ri;
							*rgbout++ = gi;
							*rgbout++ = bi;
							*rgbout++ = ai>>3; // 13-bit
						}
					}
				}
				else
				{
					for(x=0;x<width; x++)
					{
						int ri,gi,bi,ai;
						int rmix,gmix,bmix;
						int rmixd,gmixd,bmixd;
						short *sptr;
						ri = *(short *)rptr++<<3; // signed 16-bit
						gi = *(short *)gptr++<<3;
						bi = *(short *)bptr++<<3;
						ai = *(short *)aptr++<<3;

						if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
						if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
						if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;
						if(ai < 0) ai = 0; if(ai > 65535) ai = 65535;

						rmix = (ri & cube_depth_mask);
						gmix = (gi & cube_depth_mask);
						bmix = (bi & cube_depth_mask);

						ri>>=cube_shift_dn;
						gi>>=cube_shift_dn;
						bi>>=cube_shift_dn;

						sptr = &cube[(bi*cube_depth*cube_depth + gi*cube_depth + ri)*3];

						rmixd = cube_depth_mask+1 - rmix;
						gmixd = cube_depth_mask+1 - gmix;
						bmixd = cube_depth_mask+1 - bmix;

						ri = ((((((sptr[0]*rmixd + sptr[3]*rmix)>>cube_shift_dn)*gmixd +
							((sptr[cube_depth*3]*rmixd + sptr[cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
							(((((sptr[cube_depth*cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+3]*rmix)>>cube_shift_dn)*gmixd +
							((sptr[cube_depth*cube_depth*3+cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

						gi = ((((((sptr[1]*rmixd + sptr[4]*rmix)>>cube_shift_dn)*gmixd +
							((sptr[cube_depth*3+1]*rmixd + sptr[cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
							(((((sptr[cube_depth*cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+4]*rmix)>>cube_shift_dn)*gmixd +
							((sptr[cube_depth*cube_depth*3+cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

						bi = ((((((sptr[2]*rmixd + sptr[5]*rmix)>>cube_shift_dn)*gmixd +
							((sptr[cube_depth*3+2]*rmixd + sptr[cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
							(((((sptr[cube_depth*cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+5]*rmix)>>cube_shift_dn)*gmixd +
							((sptr[cube_depth*cube_depth*3+cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

						/* if(convert2YUV)
						{
							rgbout[0] = ri;
							rgbout[width] = gi;
							rgbout[width*2] = bi;
							rgbout[width*3] = ai>>3;
							rgbout++;
						}
						else */
						{
							*rgbout++ = ri;
							*rgbout++ = gi;
							*rgbout++ = bi;
							*rgbout++ = ai>>3;
						}
					}
				}
			}
			else if(*flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
			{

				if(*whitebitdepth == 0 || *whitebitdepth == 16)
				{
					for(x=0;x<width; x+=8)
					{
						unsigned short *rgbsegment = rgb;

						rgb+= 8*4;

						for(xx=0;xx<8; xx++)
						{
							int ri,gi,bi,ai;
							int rmix,gmix,bmix;
							int rmixd,gmixd,bmixd;
							short *sptr;
							ri = rgbsegment[0];
							gi = rgbsegment[8];
							bi = rgbsegment[16];
							ai = rgbsegment[24];
							rgbsegment++;

							rmix = (ri & cube_depth_mask);
							gmix = (gi & cube_depth_mask);
							bmix = (bi & cube_depth_mask);

							ri>>=cube_shift_dn;
							gi>>=cube_shift_dn;
							bi>>=cube_shift_dn;

						//	if(ri == 63) ri = 62,rmix=0x3ff;
						//	if(gi == 63) gi = 62,gmix=0x3ff;
						//	if(bi == 63) bi = 62,bmix=0x3ff;

							sptr = &cube[(bi*cube_depth*cube_depth + gi*cube_depth + ri)*3];

							rmixd = cube_depth_mask+1 - rmix;
							gmixd = cube_depth_mask+1 - gmix;
							bmixd = cube_depth_mask+1 - bmix;

							ri = ((((((sptr[0]*rmixd + sptr[3]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*3]*rmixd + sptr[cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
								(((((sptr[cube_depth*cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+3]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*cube_depth*3+cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

							gi = ((((((sptr[1]*rmixd + sptr[4]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*3+1]*rmixd + sptr[cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
								(((((sptr[cube_depth*cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+4]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*cube_depth*3+cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

							bi = ((((((sptr[2]*rmixd + sptr[5]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*3+2]*rmixd + sptr[cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
								(((((sptr[cube_depth*cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+5]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*cube_depth*3+cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

						/*	if(ri < -32768) ri = -32768; //table will be limited so this should not be required.
							if(ri > 32767) ri = 32767;
							if(gi < -32768) gi = -32768;
							if(gi > 32767) gi = 32767;
							if(bi < -32768) bi = -32768;
							if(bi > 32767) bi = 32767;*/

							/* if(convert2YUV)
							{
								rgbout[0] = ri;
								rgbout[width] = gi;
								rgbout[width*2] = bi;
								rgbout[width*3] = ai>>3;
								rgbout++;
							}
							else */
							{
								*rgbout++ = ri;
								*rgbout++ = gi;
								*rgbout++ = bi;
								*rgbout++ = ai>>3;
							}
						}
					}
				}
				else
				{
					for(x=0;x<width; x+=8)
					{
						short *rgbsegment = (short *)rgb;

						rgb+= 8*4;

						for(xx=0;xx<8; xx++)
						{
							int ri,gi,bi,ai;
							int rmix,gmix,bmix;
							int rmixd,gmixd,bmixd;
							short *sptr;
							ri = rgbsegment[0]<<3; // signed 16-bit
							gi = rgbsegment[8]<<3;
							bi = rgbsegment[16]<<3;
							ai = rgbsegment[16]<<3;
							rgbsegment++;

							if(ri < 0) ri = 0; if(ri > 65535) ri = 65535;
							if(gi < 0) gi = 0; if(gi > 65535) gi = 65535;
							if(bi < 0) bi = 0; if(bi > 65535) bi = 65535;
							if(ai < 0) ai = 0; if(ai > 65535) ai = 65535;

							rmix = (ri & cube_depth_mask);
							gmix = (gi & cube_depth_mask);
							bmix = (bi & cube_depth_mask);

							ri>>=cube_shift_dn;
							gi>>=cube_shift_dn;
							bi>>=cube_shift_dn;

						//	if(ri == 63) ri = 62,rmix=0x3ff;
						//	if(gi == 63) gi = 62,gmix=0x3ff;
						//	if(bi == 63) bi = 62,bmix=0x3ff;

							sptr = &cube[(bi*cube_depth*cube_depth + gi*cube_depth + ri)*3];

							rmixd = cube_depth_mask+1 - rmix;
							gmixd = cube_depth_mask+1 - gmix;
							bmixd = cube_depth_mask+1 - bmix;

							ri = ((((((sptr[0]*rmixd + sptr[3]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*3]*rmixd + sptr[cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
								(((((sptr[cube_depth*cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+3]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*cube_depth*3+cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

							gi = ((((((sptr[1]*rmixd + sptr[4]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*3+1]*rmixd + sptr[cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
								(((((sptr[cube_depth*cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+4]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*cube_depth*3+cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

							bi = ((((((sptr[2]*rmixd + sptr[5]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*3+2]*rmixd + sptr[cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
								(((((sptr[cube_depth*cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+5]*rmix)>>cube_shift_dn)*gmixd +
								((sptr[cube_depth*cube_depth*3+cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

						/*	if(ri < -32768) ri = -32768; //table will be limited so this should not be required.
							if(ri > 32767) ri = 32767;
							if(gi < -32768) gi = -32768;
							if(gi > 32767) gi = 32767;
							if(bi < -32768) bi = -32768;
							if(bi > 32767) bi = 32767;*/

						/*	if(convert2YUV)
							{
								rgbout[0] = ri;
								rgbout[width] = gi;
								rgbout[width*2] = bi;
								rgbout[width*3] = ai>>3;
								rgbout++;
							}
							else */
							{
								*rgbout++ = ri;
								*rgbout++ = gi;
								*rgbout++ = bi;
								*rgbout++ = ai>>3;
							}
						}
					}
				}
			}
			else
			{

				for(x=0;x<width; x++)
				{
					int ri,gi,bi,ai;
					int rmix,gmix,bmix;
					int rmixd,gmixd,bmixd;
					short *sptr;
					ri = *rgb++;
					gi = *rgb++;
					bi = *rgb++;
					ai = *rgb++;

					rmix = (ri & cube_depth_mask);
					gmix = (gi & cube_depth_mask);
					bmix = (bi & cube_depth_mask);

					ri>>=cube_shift_dn;
					gi>>=cube_shift_dn;
					bi>>=cube_shift_dn;

					sptr = &cube[(bi*cube_depth*cube_depth + gi*cube_depth + ri)*3];
#if 1
					rmixd = cube_depth_mask - rmix;
					gmixd = cube_depth_mask - gmix;
					bmixd = cube_depth_mask - bmix;

					ri = ((((((sptr[0]*rmixd + sptr[3]*rmix)>>cube_shift_dn)*gmixd +
						((sptr[cube_depth*3]*rmixd + sptr[cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
						(((((sptr[cube_depth*cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+3]*rmix)>>cube_shift_dn)*gmixd +
						((sptr[cube_depth*cube_depth*3+cube_depth*3]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+3]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

					gi = ((((((sptr[1]*rmixd + sptr[4]*rmix)>>cube_shift_dn)*gmixd +
						((sptr[cube_depth*3+1]*rmixd + sptr[cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
						(((((sptr[cube_depth*cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+4]*rmix)>>cube_shift_dn)*gmixd +
						((sptr[cube_depth*cube_depth*3+cube_depth*3+1]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+4]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

					bi = ((((((sptr[2]*rmixd + sptr[5]*rmix)>>cube_shift_dn)*gmixd +
						((sptr[cube_depth*3+2]*rmixd + sptr[cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
						(((((sptr[cube_depth*cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+5]*rmix)>>cube_shift_dn)*gmixd +
						((sptr[cube_depth*cube_depth*3+cube_depth*3+2]*rmixd + sptr[cube_depth*cube_depth*3+cube_depth*3+5]*rmix)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix))>>cube_shift_dn;

#else  // Band like crazy and is slower.
					rmix <<= 5;
					gmix <<= 5;
					bmix <<= 5;

					rmixd = 32767 - rmix;
					gmixd = 32767 - gmix;
					bmixd = 32767 - bmix;

					{

			__m128i rgbrgbA_epi16;
			__m128i rgbrgbB_epi16;
			__m128i rgbrgbC_epi16;
			__m128i rgbrgbD_epi16;
			__m128i rgbArgbB_epi16;
			__m128i rgbCrgbD_epi16;
			__m128i rgbABCD_epi16;

			__m128i *sptrA_ptr = (__m128i *)&sptr[0];
			__m128i *sptrB_ptr = (__m128i *)&sptr[cube_depth*3];
			__m128i *sptrC_ptr = (__m128i *)&sptr[cube_depth*cube_depth*3];
			__m128i *sptrD_ptr = (__m128i *)&sptr[cube_depth*cube_depth*3+cube_depth*3];

			__m128i	rmix_epi16 = _mm_set_epi16(0,0,rmixd,rmixd,rmixd,rmix,rmix,rmix);
			__m128i	gmix_epi16 = _mm_set_epi16(0,gmixd,gmixd,gmixd,0,gmix,gmix,gmix);
			__m128i	bmix_epi16 = _mm_set_epi16(0,bmixd,bmixd,bmixd,0,bmix,bmix,bmix);

			// Load sixteen highpass coefficients
			rgbrgbA_epi16 = _mm_loadu_si128(sptrA_ptr); //15bit signed
			rgbrgbB_epi16 = _mm_loadu_si128(sptrB_ptr); //15bit signed
			rgbrgbC_epi16 = _mm_loadu_si128(sptrC_ptr); //15bit signed
			rgbrgbD_epi16 = _mm_loadu_si128(sptrD_ptr); //15bit signed

			//rgbrgbA = sptr[0],[1],[2],[3],[4],[5],0,0 * rmixd,rmixd,rmixd,rmix,rmix,rmix,0,0
		    //rgbrgbB = sptr[cube_depth*3+0],[1],[2],[3],[4],[5],0,0 * rmixd,rmixd,rmixd,rmix,rmix,rmix,0,0
			//rgbrgbC = sptr[cube_depth*cube_depth3+0],[1],[2],[3],[4],[5],0,0 * rmixd,rmixd,rmixd,rmix,rmix,rmix,0,0
	        //rgbrgbD = sptr[cube_depth*cube_depth*cube_depth3+0],[1],[2],[3],[4],[5],0,0 * rmixd,rmixd,rmixd,rmix,rmix,rmix,0,0
			rgbrgbA_epi16 = _mm_mulhi_epi16(rgbrgbA_epi16, rmix_epi16); //14bit signed
			rgbrgbB_epi16 = _mm_mulhi_epi16(rgbrgbB_epi16, rmix_epi16); //14bit signed
			rgbrgbC_epi16 = _mm_mulhi_epi16(rgbrgbC_epi16, rmix_epi16); //14bit signed
			rgbrgbD_epi16 = _mm_mulhi_epi16(rgbrgbD_epi16, rmix_epi16); //14bit signed

			//rigibi = ((((((rgbrgbA + rgbrgbA)>>cube_shift_dn)*gmixd +
			//    ((rgbrgbB + rgbrgbB)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmixd) +
			//    (((((rgbrgbC + rgbrgbC)>>cube_shift_dn)*gmixd +
			//    ((rgbrgbD + rgbrgbD)>>cube_shift_dn)*gmix)>>cube_shift_dn)*bmix)>>cube_shift_dn);
			//rgbrgbA = rgbrgbA + rgbrgbA<<48;
			//rgbrgbB = rgbrgbB + rgbrgbB<<48;
			//rgbrgbD = rgbrgbC + rgbrgbC<<48;
			//rgbrgbC = rgbrgbD + rgbrgbD<<48;
			rgbrgbA_epi16 = _mm_adds_epi16(rgbrgbA_epi16, _mm_srli_si128(rgbrgbA_epi16,6)); //15bit signed
			rgbrgbB_epi16 = _mm_adds_epi16(rgbrgbB_epi16, _mm_srli_si128(rgbrgbB_epi16,6)); //15bit signed
			rgbrgbC_epi16 = _mm_adds_epi16(rgbrgbC_epi16, _mm_srli_si128(rgbrgbC_epi16,6)); //15bit signed
			rgbrgbD_epi16 = _mm_adds_epi16(rgbrgbD_epi16, _mm_srli_si128(rgbrgbD_epi16,6)); //15bit signed


			//rigibi = ((((rgbrgbA*gmixd +    rgbrgbB*gmix)>>cube_shift_dn)*bmixd) +
			//    (((rgbrgbC*gmixd + rgbrgbD*gmix)>>cube_shift_dn)*bmix)>>cube_shift_dn);
	        //rgbArgbB = rgbrgbA + (rgbrgbB>>48)
		    //rgbCrgbD = rgbrgbC + (rgbrgbD>>48)
			rgbArgbB_epi16 = _mm_unpacklo_epi64(rgbrgbA_epi16, rgbrgbB_epi16); //15bit signed
			rgbCrgbD_epi16 = _mm_unpacklo_epi64(rgbrgbC_epi16, rgbrgbD_epi16); //15bit signed

			rgbArgbB_epi16 = _mm_slli_epi16(rgbArgbB_epi16,1);
			rgbCrgbD_epi16 = _mm_slli_epi16(rgbCrgbD_epi16,1);

	        //rgbArgbB *= gmixd,gmixd,gmixd,gmix,gmix,gmix,0,0
		    //rgbArgbD *= gmixd,gmixd,gmixd,gmix,gmix,gmix,0,0
			rgbArgbB_epi16 = _mm_mulhi_epi16(rgbArgbB_epi16, gmix_epi16); //14bit signed
			rgbCrgbD_epi16 = _mm_mulhi_epi16(rgbCrgbD_epi16, gmix_epi16); //14bit signed

			//rgbArgbB = rgbArgbB + rgbArgbB<<48;
		    //rgbCrgbD = rgbArgbD + rgbArgbD<<48;
			rgbArgbB_epi16 = _mm_adds_epi16(rgbArgbB_epi16, _mm_srli_si128(rgbArgbB_epi16,8)); //15bit signed
			rgbCrgbD_epi16 = _mm_adds_epi16(rgbCrgbD_epi16, _mm_srli_si128(rgbCrgbD_epi16,8)); //15bit signed


	        //rigibi = (((rgbArgbB*bmixd + rgbCrgbD*bmix)>>cube_shift_dn);
		    //rgbABCD = rgbArgbB + (rgbrgbD>>48)
			rgbABCD_epi16 = _mm_unpacklo_epi64(rgbArgbB_epi16, rgbCrgbD_epi16); //15bit signed

			rgbABCD_epi16 = _mm_slli_epi16(rgbABCD_epi16,1);

			//rgbABCD *= bmixd,bmixd,bmixd,bmix,bmix,bmix,0,0
			rgbABCD_epi16 = _mm_mulhi_epi16(rgbABCD_epi16, bmix_epi16); //14bit signed

			//rgbABCD += rgbABCD<<48;
			rgbABCD_epi16 = _mm_adds_epi16(rgbABCD_epi16, _mm_srli_si128(rgbABCD_epi16,8)); //15bit signed

			rgbABCD_epi16 = _mm_slli_epi16(rgbABCD_epi16,1);

			ri =  _mm_extract_epi16(rgbABCD_epi16, 0);
			gi =  _mm_extract_epi16(rgbABCD_epi16, 1);
			bi =  _mm_extract_epi16(rgbABCD_epi16, 2);

					}

#endif

				/*	if(convert2YUV)
					{
						rgbout[0] = ri;
						rgbout[width] = gi;
						rgbout[width*2] = bi;
						rgbout[width*3] = ai>>3;
						rgbout++;
					}
					else */
					{
						*rgbout++ = ri;
						*rgbout++ = gi;
						*rgbout++ = bi;
						*rgbout++ = ai>>3;
					}
				}
			}
		}


		//if(convert2YUV)
		//{
		//	*flags = ACTIVEMETADATA_PRESATURATED | ACTIVEMETADATA_PLANAR | ACTIVEMETADATA_COLORFORMATDONE;
		//}
		//else
		{
			*flags = 0;
		}

		*whitebitdepth = 13;
		return dst;
	}
	else //1D simplied
	{
		int still16bit = 0;
		int lines;
		//int max = 65535;
		//float oneunit = 8192.0;
		int channels = 4;
		int sat = (int)((decoder->cdl_sat + 1.0) * 8192.0);	

		for(lines=0; lines<height; lines++)
		{
			int x,xx,y = lines;
			unsigned short *rgb = (unsigned short *)src;
			short *rgbout = (short *)dst;

			rgb += width*y*channels;
			rgbout += width*lines*channels;

			if(*flags & ACTIVEMETADATA_PLANAR)
			{
				unsigned short *rptr,*gptr,*bptr,*aptr;
				int *lcm = &decoder->linear_color_matrix[0];
				//int *lcmns = &decoder->linear_color_matrix_highlight_sat[0];
				int *ccm = &decoder->curved_color_matrix[0];
			//	int highlight_start = (int)((decoder->cfhddata.channel[0].user_highlight_point + 1.0) * 81920.0);
			//	int highlight_end = highlight_start * 3 / 2;
			//	int mixgain256 = (highlight_end - highlight_start) / 256;
			//	if(mixgain256 == 0) mixgain256 = 1;
			//	if(highlight_start > 81900) highlight_start = 8190000; // switch off efficently

				rptr = rgb;
				gptr = &rgb[width];
				bptr = &rgb[width*2];
				aptr = &rgb[width*3];

				if(decoder->RGBFilterBufferPhase == 1) // decoder order
				{
					gptr = rgb;
					rptr = &rgb[width];
				}


				for(x=0;x<width; x++)
				{
					int ri,gi,bi,ai;
					int rn,gn,bn;
					if(*whitebitdepth == 13)
					{
						ri = *(short *)rptr++; // 13-bit
						gi = *(short *)gptr++;
						bi = *(short *)bptr++;
						ai = *(short *)aptr++;

						// Curve2Linear range -2 to +4, or -16384 to 32768
						if(ri < -16384) ri = -16384;
						if(gi < -16384) gi = -16384;
						if(bi < -16384) bi = -16384;
					}
					else
					{
						ri = *rptr++>>3; // 13-bit unsigned
						gi = *gptr++>>3;
						bi = *bptr++>>3;
						ai = *aptr++>>3;
					}

					CURVES_PROCESSING_MACRO;
																										
					*rgbout++ = ri;
					*rgbout++ = gi;
					*rgbout++ = bi;
					*rgbout++ = ai;
				}
			}
			else if(*flags & ACTIVEMETADATA_SRC_8PIXEL_PLANAR)
			{
				int *lcm = &decoder->linear_color_matrix[0];
				//int *lcmns = &decoder->linear_color_matrix_highlight_sat[0];
				int *ccm = &decoder->curved_color_matrix[0];
			//	int highlight_start = (int)((decoder->cfhddata.channel[0].user_highlight_point + 1.0) * 81920.0);
			//	int highlight_end = highlight_start * 3 / 2;
			//	int mixgain256 = (highlight_end - highlight_start) / 256;
			//	if(mixgain256 == 0) mixgain256 = 1;
			//	if(highlight_start > 81900) highlight_start = 8190000; // switch off efficently

				for(x=0;x<width; x+=8)
				{
					unsigned short *rgbsegment = rgb;
					short *signrgbsegment = (short *)rgb;

					rgb+= 8*4;

					for(xx=0;xx<8; xx++)
					{
						int ri,gi,bi,ai;
						int rn,gn,bn;

						if(*whitebitdepth == 13)
						{
							ri = signrgbsegment[0]; // 13-bit
							gi = signrgbsegment[8];
							bi = signrgbsegment[16];
							ai = signrgbsegment[24];
							signrgbsegment++;

							// Curve2Linear range -2 to +4, or -16384 to 32768
							if(ri < -16384) ri = -16384;
							if(gi < -16384) gi = -16384;
							if(bi < -16384) bi = -16384;
						}
						else
						{
							ri = rgbsegment[0]>>3; // 13-bit
							gi = rgbsegment[8]>>3;
							bi = rgbsegment[16]>>3;
							ai = rgbsegment[24]>>3;
							rgbsegment++;
						}
						
						CURVES_PROCESSING_MACRO;		

						*rgbout++ = ri;
						*rgbout++ = gi;
						*rgbout++ = bi;
						*rgbout++ = ai;
					}
				}
			}
			else
			{

				if(*whitebitdepth == 16 && decoder->Curve2Linear == NULL)
				{
					memcpy(rgbout, rgb, width*4*2);
					rgb += width*4;
					rgbout += width*4;

					still16bit = 1;
				}
				else
				{
					int *lcm = &decoder->linear_color_matrix[0];
					//int *lcmns = &decoder->linear_color_matrix_highlight_sat[0];
					int *ccm = &decoder->curved_color_matrix[0];
				//	int highlight_start = (int)((decoder->cfhddata.channel[0].user_highlight_point + 1.0) * 81920.0);
				//	int highlight_end = highlight_start * 3 / 2;
				//	int mixgain256 = (highlight_end - highlight_start) / 256;
				//	if(mixgain256 == 0) mixgain256 = 1;
				//	if(highlight_start > 81900) highlight_start = 8190000; // switch off efficently

					for(x=0;x<width; x++)
					{
						int ri,gi,bi,ai;
						int rn,gn,bn;

						if(*whitebitdepth == 13)
						{
							short *sptr = (short *)rgb;
							ri = sptr[0]; // 13-bit
							gi = sptr[1];
							bi = sptr[2];
							ai = sptr[2];
							sptr+=4;
							rgb+=4;
						}
						else
						{
							ri = *rgb++>>3; // 13-bit
							gi = *rgb++>>3;
							bi = *rgb++>>3;
							ai = *rgb++>>3;
						}

						CURVES_PROCESSING_MACRO;

						*rgbout++ = ri;
						*rgbout++ = gi;
						*rgbout++ = bi;
						*rgbout++ = ai;
					}

				}
			}
		}

		if(still16bit)
		{
			*whitebitdepth = 16;
			*flags = ACTIVEMETADATA_PRESATURATED;
		}
		else
		{
			*whitebitdepth = 13;
			*flags = 0;
		}

		return dst;
	}
}

