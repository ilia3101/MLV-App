/*! @file quantize.c

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
#include "config.h"
#include "timing.h"

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif

#define TIMING (1 && _TIMING)
#define XMMOPT (1 && _XMMOPT)

#ifndef DEBUG_VBR
#define DEBUG_VBR 0
#endif

#include <assert.h>
#include <memory.h>
#ifdef __x86_64__
    #include <mmintrin.h>        // MMX intrinsics
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif
#include "quantize.h"
#include "filter.h"
#include "encoder.h"
#include "convert.h"
#include "limits.h"

// Datatype used for computing upper half of product of 16-bit multiplication
typedef union {
	uint32_t longword;
	struct {
		unsigned short lower;
		unsigned short upper;
	} halfword;
} LONGWORD;


// Switch that can be used to disable quantization for debugging
#ifndef _QUANT
#define _QUANT 1
#endif

// Quantization change made by David Newman 9/3/2002
//#ifndef _QUANTIZE_PATCH_1
#define _QUANTIZE_PATCH_1	0
//#endif

#ifndef FIXED_DATA_RATE
#define FIXED_DATA_RATE		1
#endif

#define QUANT_VSCALE_SHIFT	8		// Right shift used for highpass bands

// Control whether the code in this file uses prefetching
#define PREFETCH (1 && _PREFETCH)


// Quantization tables for each configuration of transform wavelets

// Field transform with one level of spatial wavelets for temporal lowpass band

#define QUANT_LIMITS	{0, 8192, 5120, 3072}		// (don't use),  low,  med,  high

#define LUMA_QUALITY_INITIALIZER	{LUMA_QUALITY_DEFAULT, LUMA_QUALITY_LOW, LUMA_QUALITY_MEDIUM, LUMA_QUALITY_HIGH}
#define CHROMA_QUALITY_INITIALIZER	{CHROMA_QUALITY_DEFAULT, CHROMA_QUALITY_LOW, CHROMA_QUALITY_MEDIUM, CHROMA_QUALITY_HIGH}





// Check that the number of subbands is within range
#if (NUM_QUANT_SUBBANDS > MAX_QUANT_SUBBANDS)
#error Too many subbands for the quantization data structure
#endif

// Minimum and maximum variable bit rate values
#define VBR_MIN (256)
#define VBR_MAX (512)

// Definitions of target bitrates
#define TARGET_VIDEO_BITRATE(q)		((q)->TargetBitRate)
#define TARGET_GOP_BITRATE(q)		(TARGET_VIDEO_BITRATE(q)/15)

#define GOP_BITRATE_120PERCENT(q)	(TARGET_GOP_BITRATE(q) /100 * 120)
#define GOP_BITRATE_75PERCENT(q)	(TARGET_GOP_BITRATE(q) /100 * 75)
#define GOP_BITRATE_95PERCENT(q)	(TARGET_GOP_BITRATE(q) /100 * 95)


// Quantization tables indexed by quality level and subband

static const int quantScaleFactor = QUANT_SCALE_FACTOR;
static const int LumaQuality[NUM_QUANT_LEVELS][NUM_QUANT_SUBBANDS] = LUMA_QUALITY_INITIALIZER;
static const int ChromaQuality[NUM_QUANT_LEVELS][NUM_QUANT_SUBBANDS] = CHROMA_QUALITY_INITIALIZER;
static const int quantLimit[NUM_QUANT_LEVELS] = QUANT_LIMITS;


// Global data structure for quantization
//static QUANTIZER q = QUANTIZER_INITIALIZER;


// Performance measurements
#if _TIMING
extern TIMER tk_quant;
#endif

#if _TIMING
COUNTER quantization_count = COUNTER_INITIALIZER;
#endif


void InitQuantizer(QUANTIZER *q)
{
	static int quantLuma[] = LUMA_QUALITY_DEFAULT;
	static int quantLumaMAX[] = LUMA_QUALITY_LOW;
	static int quantChroma[] = CHROMA_QUALITY_DEFAULT;
	static int quantChromaMAX[] = CHROMA_QUALITY_LOW;

	q->TargetBitRate = DEFAULT_TARGET_BITRATE;
	q->FixedQuality = DEFAULT_FIXED_QUALITY;
	q->quantLimit = DEFAULT_QUANT_LIMIT;

	q->midpoint_prequant = 2; // half way midpoint

	memcpy(q->quantLuma, quantLuma, sizeof(quantLuma));
	memcpy(q->quantLumaMAX, quantLumaMAX, sizeof(quantLumaMAX));
	memcpy(q->quantChroma, quantChroma, sizeof(quantChroma));
	memcpy(q->quantChromaMAX, quantChromaMAX, sizeof(quantChromaMAX));
}

#if 0
void InitDefaultQuantizer(void)
{
	InitQuantizer(&q);
}
#endif

#if DEBUG_VBR
void DumpText(char *txt, int hex)
{
	FILE *fp;

	fp = fopen("C:\\Cedoc\\Logfiles\\dump.txt","a");
	fprintf(fp, txt, hex);

	fclose(fp);
}
#endif

#if 0
void DumpEncoderQuant(ENCODER *encoder, int channel)
{
	//int num_channels = encoder->num_quant_channels;
	int num_subbands = encoder->num_quant_subbands;
	//int channel;
	int subband;
	FILE *logfile = encoder->logfile;

	fprintf(logfile, "Quant channel: %d, subbands: %d\n", channel, num_subbands);
	for (subband = 0; subband < num_subbands; subband++)
		fprintf(logfile, "%5d", encoder->quant[channel][subband]);
	fprintf(logfile, "\n");
}
#endif

#define QUANT_TABLES_FILENAME "c:\\cedoc\\quant.txt"

/*static*/ int g_midpoint_prequant = 2;

// Set the quantization quality (1: low,  2: medium,  3: high)
void QuantizationSetQuality(QUANTIZER *q,
							int quality,
							bool progressive,
							int precision,
							int goplength,
							bool ChromaFullRes,
							FRAME *frame,
							int64_t lastgopbytes,
							int video_channels)
{
	{
		int i;
		int size;
		int overrateFactor;
		int factor = quality & 0x00ff;
		int detail = (quality & 0x0e0000/*CFEncode_Premphasis_Mask*/) >> 17/*CFEncode_Premphasis_Shift*/;
		int rgb_quality = (quality & 0x06000000/*CFEncode_RGB_Quality_Mask*/) >> 25;
		int lowfreqquant = 4;
		if(video_channels < 1) video_channels = 1;
		lastgopbytes /= video_channels;

		if(rgb_quality > 2) rgb_quality = 2; // value range 0-2

		q->inputFixedQuality = quality;

		q->midpoint_prequant = detail + 2;
		if(q->midpoint_prequant > 8) q->midpoint_prequant = 0;
		g_midpoint_prequant = q->midpoint_prequant;

		if(quality & 0x1f00) // uncompressed
		{
			//factor = 10;
			factor = 5;
		}

		q->newQuality = factor;

		if(q->newQuality >= 5 && frame == 0)
		{
			if(q->newQuality == 5)
			{
				q->FSratelimiter = 8;
			}
			else if(q->newQuality == 6)
			{
				q->FSratelimiter = 4;
			}
		}

		if(q->newQuality >= 5 && frame) // Filmscan 2 or 3
		{
			if(lastgopbytes && !(quality & 0x1f00)) // first frame is zero, only rate control if not in uncompressed modes
			{
				// Using 10-bit uncompressed
				float gop_size = (float)(int32_t)lastgopbytes;
				float compression = (float)(frame->width*frame->height*frame->num_channels*precision/8) / gop_size;

				if(!ChromaFullRes)
					compression /= 1.5;

				switch(q->newQuality)
				{
				case 5: //filmscan 2  --- target 4.0:1 to 5.5:1
					if(compression > 5.5)
					{
						q->FSratelimiter--;
						if(compression > 6.5)
							q->FSratelimiter--;
						if(compression > 7.5)
							q->FSratelimiter-=2;
					}
					else if(compression < 4.0)
					{
						q->FSratelimiter++;
						if(compression < 3.5)
							q->FSratelimiter++;
						if(compression < 3.0)
							q->FSratelimiter++;
						if(compression < 2.5)
							q->FSratelimiter++;
						if(compression < 2.0)
							q->FSratelimiter++;
						if(compression < 1.5)
							q->FSratelimiter+=2;
					}
					break;

				case 6: //filmscan 3  --- target 3.0:1 to 4.5:1
				default:
					if(compression > 4.5)
					{
						q->FSratelimiter--;
						if(compression > 5.5)
							q->FSratelimiter--;
						if(compression > 6.5)
							q->FSratelimiter-=2;
					}
					else if(compression < 3.0)
					{
						q->FSratelimiter++;
						if(compression < 2.5)
							q->FSratelimiter++;
						if(compression < 2.0)
							q->FSratelimiter++;
						if(compression < 1.5)
							q->FSratelimiter+=2;
					}
					break;

				case 10:
					if(compression > 2.5)
						q->FSratelimiter--;
					else if(compression < 2.0)
					{
						q->FSratelimiter++;
						if(compression < 1.5)
							q->FSratelimiter+=2;
					}

					break;
				}
				
				if(q->FSratelimiter < 0)
					q->FSratelimiter = 0;
				if(q->FSratelimiter >= 20) // values 17 thru 20 reduce rgbquality
					q->FSratelimiter = 20;
#if (DEBUG && _WIN32)
				{
					char t[100];
					sprintf_s(t, sizeof(t), "compression = %f, FSratelimiter = %d (q%d)",
						compression, q->FSratelimiter, q->newQuality);
					OutputDebugString(t);
				}
#endif
			}
		}

		if(factor < 1 || factor > 10) factor = 0; // default
		if(factor > 3) factor = 3; // only 3 tables.

		overrateFactor = factor;
		if(overrateFactor >= 2)
			overrateFactor--;  //If high switch to medium or Medium switch to low.


		size = (TRANSFORM_NUM_SPATIAL*3+8)*4;

//#if _FIELDPLUS_TRANSFORM
//		size += 3*4;
//#endif


		memcpy(q->quantLuma, LumaQuality[factor], size);
		memcpy(q->quantLumaMAX, LumaQuality[overrateFactor], size);
		if(ChromaFullRes)
		{
			memcpy(q->quantChroma, LumaQuality[factor], size);
			memcpy(q->quantChromaMAX, LumaQuality[overrateFactor], size);
		}
		else
		{
			memcpy(q->quantChroma, ChromaQuality[factor], size);
			memcpy(q->quantChromaMAX, ChromaQuality[overrateFactor], size);
		}


#if 1	// limit the quality drop between levels
		for(i=0; i<MAX_QUANT_SUBBANDS; i++)
		{
			q->quantLumaMAX[i] = q->quantLuma[i] + ((q->quantLumaMAX[i] - q->quantLuma[i])/2);
			q->quantChromaMAX[i] = q->quantChroma[i] + ((q->quantChromaMAX[i] - q->quantChroma[i])/2);
		}
#endif

		if(precision >= CODEC_PRECISION_10BIT)
		{
			int i;
			int scale = 4*16;
			int scaleMAX = 4*16;
			int limiter = q->FSratelimiter;
			if(limiter > 16) limiter = 16;

			switch(q->newQuality)
			{
			case 4: // one above High
				lowfreqquant = 3;
				scale = 3*16;
				break;
			case 5: // two above High
			case 6: // three above High
			case 7:
			case 8:
			case 9:
			case 10:
				lowfreqquant = 2;
				scale = 1*16;
				scale += limiter*2;
				break;
			}

			if(q->newQuality >= 5)// && goplength > 1)
			{
				if(scale >= 4)
					scale >>= 1;
			}
			if(q->newQuality == 10)
			{
				if(scale >= 6)
				{
					scale *= 2;
					scale /= 3;
				}
			}

			// Less quantization on low pass for int32_t GOP
			if(q->newQuality >= 4)// && goplength > 1)
			{
				for(i=1;i<7;i++)
				{
					q->quantLuma[i] = lowfreqquant;  //was 4,4,6, make all 3s
					q->quantChroma[i] = lowfreqquant;
					q->quantLumaMAX[i] = lowfreqquant;
					q->quantChromaMAX[i] = lowfreqquant;
				}
			}
			for(i=8;i<17;i++)
			{
				q->quantLuma[i] *= scale;
				q->quantLuma[i] >>= 4;
				if(q->quantLuma[i] < 2) q->quantLuma[i] = 2;
				q->quantChroma[i] *= scale;
				q->quantChroma[i] >>= 4;
				if(q->quantChroma[i] < 2) q->quantChroma[i] = 2;

				q->quantLumaMAX[i] *= scaleMAX;
				q->quantLumaMAX[i] >>= 4;
				if(q->quantLumaMAX[i] < 2) q->quantLumaMAX[i] = 2;
				q->quantChromaMAX[i] *= scaleMAX;
				q->quantChromaMAX[i] >>= 4;
				if(q->quantChromaMAX[i] < 2) q->quantChromaMAX[i] = 2;
			}

			q->quantLuma[7] = 4;
			q->quantChroma[7] = 4;
			q->quantLumaMAX[7] = 4;
			q->quantChromaMAX[7] = 4;

			// scaling these bands causes overflows.
		/*	for(i=1;i<7;i++)
			{
				q->quantLuma[i] *= scale;			q->quantLuma[i] >>= 2;
				q->quantChroma[i] *= scale;			q->quantChroma[i] >>= 2;
				q->quantLumaMAX[i] *= scaleMAX;		q->quantLumaMAX[i] >>= 2;
				q->quantChromaMAX[i] *= scaleMAX;	q->quantChromaMAX[i] >>= 2;
			}*/
		}


		if(precision == CODEC_PRECISION_12BIT)
		{
			int chromagain = 4;

			
			if(q->newQuality >= 4)// && goplength > 1)
			{
				for(i=1;i<7;i++)
				{
					q->quantLuma[i] = lowfreqquant;  //was 4,4,6, make all 3s
					q->quantChroma[i] = lowfreqquant;
					q->quantLumaMAX[i] = lowfreqquant;
					q->quantChromaMAX[i] = lowfreqquant;
				}
			}

			for(i=4;i<7;i++)
			{
				q->quantLuma[i] *= 4;
				q->quantChroma[i] *= 4; // If coding as G, R-G and B-G else *4
				q->quantLumaMAX[i] *= 4;
				q->quantChromaMAX[i] *= 4; // If coding as G, R-G and B-G else *4
			}

			switch(rgb_quality)
			{
			case 0: chromagain = 8; break;
			case 1: chromagain = 6; break;
			case 2: chromagain = 4; break;
			case 3: chromagain = 4; break;
			}

			if(q->FSratelimiter > 16)
			{
				chromagain += (q->FSratelimiter-16);
				if(chromagain > 8) chromagain = 8;
			}

			for(i=11;i<17;i++)
			{
				q->quantLuma[i] *= 4;
				q->quantChroma[i] *= chromagain; // If coding as G, R-G and B-G else *4
				q->quantLumaMAX[i] *= 4;
				q->quantChromaMAX[i] *= chromagain; // If coding as G, R-G and B-G else *4
			}
		}


		if(!progressive)
		{
			//{4, 6,6,8, 6,6,8,   4,8,8,12,  		16,16,24, 	16,16,24}
			// becomes
			//{4, 6,6,8, 6,6,8,   4,8,8,12,  		24,12,24, 	24,12,24}

			if(factor == 2) // support High to Medium table conversion but not Medium to Low for these bands
			{
				q->quantLumaMAX[12] = q->quantLuma[12];
				q->quantLumaMAX[13] = q->quantLuma[13];
				q->quantLumaMAX[15] = q->quantLuma[15];
				q->quantLumaMAX[16] = q->quantLuma[16];
				q->quantChromaMAX[12] = q->quantChroma[12];
				q->quantChromaMAX[13] = q->quantChroma[13];
				q->quantChromaMAX[15] = q->quantChroma[15];
				q->quantChromaMAX[16] = q->quantChroma[16];
			}

			q->quantLuma[11] *= 3;
			q->quantLuma[11] /= 2;
			q->quantLuma[12] *= 2;
			q->quantLuma[12] /= 3;
			q->quantLuma[14] *= 3;
			q->quantLuma[14] /= 2;
			q->quantLuma[15] *= 2;
			q->quantLuma[15] /= 3;

			q->quantChroma[11] *= 3;
			q->quantChroma[11] /= 2;
			q->quantChroma[12] *= 2;
			q->quantChroma[12] /= 3;
			q->quantChroma[14] *= 3;
			q->quantChroma[14] /= 2;
			q->quantChroma[15] *= 2;
			q->quantChroma[15] /= 3;

			q->quantLumaMAX[11] *= 3;
			q->quantLumaMAX[11] /= 2;
			q->quantLumaMAX[12] *= 2;
			q->quantLumaMAX[12] /= 3;
			q->quantLumaMAX[14] *= 3;
			q->quantLumaMAX[14] /= 2;
			q->quantLumaMAX[15] *= 2;
			q->quantLumaMAX[15] /= 3;

			q->quantChromaMAX[11] *= 3;
			q->quantChromaMAX[11] /= 2;
			q->quantChromaMAX[12] *= 2;
			q->quantChromaMAX[12] /= 3;
			q->quantChromaMAX[14] *= 3;
			q->quantChromaMAX[14] /= 2;
			q->quantChromaMAX[15] *= 2;
			q->quantChromaMAX[15] /= 3;

		}

		if(goplength == 1)
		{
			q->quantLuma[7] = q->quantLuma[11];
			q->quantLuma[8] = q->quantLuma[12];
			q->quantLuma[9] = q->quantLuma[13];

			q->quantChroma[7] = q->quantChroma[11];
			q->quantChroma[8] = q->quantChroma[12];
			q->quantChroma[9] = q->quantChroma[13];

			q->quantLumaMAX[7] = q->quantLumaMAX[11];
			q->quantLumaMAX[8] = q->quantLumaMAX[12];
			q->quantLumaMAX[9] = q->quantLumaMAX[13];

			q->quantChromaMAX[7] = q->quantChromaMAX[11];
			q->quantChromaMAX[8] = q->quantChromaMAX[12];
			q->quantChromaMAX[9] = q->quantChromaMAX[13];
		}

		if (factor)
		{
			q->quantLimit = quantLimit[factor];
			q->FixedQuality = factor;
		}
		else
		{
			q->quantLimit = quantLimit[3];
			q->FixedQuality = 0;
			q->TargetBitRate = 16000000;
		}

		if(progressive)
			q->progressive = 1;
		else
			q->progressive = 0;
	}
}

void QuantizationSetRate(QUANTIZER *q,
						 int rate,
						 bool progressive,
						 int precision,
						 int goplength,
						 bool ChromaFullRes)  // 8 to 100 M b/s
{
#if 0
	{
		int size;

		size = (TRANSFORM_NUM_SPATIAL*3+8)*4;
//		#if _FIELDPLUS_TRANSFORM
//		size += 3*4;
//		#endif

		memcpy(q->quantLuma, LumaQuality[0], size);
		if(ChromaFullRes)
			memcpy(q->quantChroma, LumaQuality[0], size);
		else
			memcpy(q->quantChroma, ChromaQuality[0], size);


		if(precision >= CODEC_PRECISION_10BIT)
		{
			int i;
			for(i=8;i<17;i++)
			{
				q->quantLuma[i] *= 4;
				q->quantChroma[i] *= 4;
			}
			q->quantLuma[7] = 4;
			q->quantChroma[7] = 4;
		}

		if(precision == CODEC_PRECISION_12BIT)
		{
			int i,chromagain;
			for(i=4;i<7;i++)
			{
				q->quantLuma[i] *= 4;
				q->quantChroma[i] *= 4; // If coding as G, R-G and B-G else *4
				q->quantLumaMAX[i] *= 4;
				q->quantChromaMAX[i] *= 4; // If coding as G, R-G and B-G else *4
			}
			switch(rgb_quality)
			{
			case 0: chromagain = 8; break;
			case 1: chromagain = 6; break;
			case 2: chromagain = 4; break;
			case 3: chromagain = 4; break;
			}
			for(i=11;i<17;i++)
			{
				q->quantLuma[i] *= 4;
				q->quantChroma[i] *= chromagain; // If coding as G, R-G and B-G else *4
				q->quantLumaMAX[i] *= 4;
				q->quantChromaMAX[i] *= chromagain; // If coding as G, R-G and B-G else *4
			}
		}

		if(!progressive)
		{
			//{4, 6,6,8, 6,6,8,   9,8,8,12,  		16,16,24, 	16,16,24}
			q->quantLuma[12] >>= 1;

			q->quantLuma[13] *= 2;
			q->quantLuma[13] /= 3;

			q->quantLuma[15] >>= 1;

			q->quantLuma[16] *= 2;
			q->quantLuma[16] /= 3;


			q->quantChroma[12] >>= 1;

			q->quantChroma[13] *= 2;
			q->quantChroma[13] /= 3;

			q->quantChroma[15] >>= 1;

			q->quantChroma[16] *= 2;
			q->quantChroma[16] /= 3;
		}

		if(goplength == 1)
		{
			q->quantLuma[7] = q->quantLuma[11];
			q->quantLuma[8] = q->quantLuma[12];
			q->quantLuma[9] = q->quantLuma[13];
		}

		q->FixedQuality = 0;
		rate *= 10;
		if(rate < 10) rate = 10;
		if(rate > 150) rate = 150;
		q->TargetBitRate = rate * 1000000;

		if(rate >= 40)
			q->quantLimit = quantLimit[3];
		else
		{
			int diff = ((rate - 10)<<8)/(40-10);
			q->quantLimit = (quantLimit[3] * diff + quantLimit[1] * (256 - diff)) >> 8; // range values between quantLimit[1] and quantLimit[3]
		}


		if(progressive)
			q->progressive = 1;
		else
			q->progressive = 0;
	}
#endif
}

/*
void QuantizationLoadTables(QUANTIZER *q)
{
	FILE *fp;
	int *ptr;

	fp = fopen(QUANT_TABLES_FILENAME, "r");
	if (fp)
	{
		ptr = &q->quantLuma[0];
		fscanf(fp,"%d, %d,%d,%d, %d,%d,%d, %d,%d,%d,%d, %d,%d,%d, %d,%d,%d\n", &ptr[0], &ptr[1], &ptr[2], &ptr[3], &ptr[4], &ptr[5], &ptr[6], &ptr[7], &ptr[8], &ptr[9], &ptr[10], &ptr[11], &ptr[12], &ptr[13], &ptr[14], &ptr[15], &ptr[16] );

		ptr = &q->quantLumaMAX[0];
		fscanf(fp,"%d, %d,%d,%d, %d,%d,%d, %d,%d,%d,%d, %d,%d,%d, %d,%d,%d\n", &ptr[0], &ptr[1], &ptr[2], &ptr[3], &ptr[4], &ptr[5], &ptr[6], &ptr[7], &ptr[8], &ptr[9], &ptr[10], &ptr[11], &ptr[12], &ptr[13], &ptr[14], &ptr[15], &ptr[16] );

		ptr = &q->quantChroma[0];
		fscanf(fp,"%d, %d,%d,%d, %d,%d,%d, %d,%d,%d,%d, %d,%d,%d, %d,%d,%d\n", &ptr[0], &ptr[1], &ptr[2], &ptr[3], &ptr[4], &ptr[5], &ptr[6], &ptr[7], &ptr[8], &ptr[9], &ptr[10], &ptr[11], &ptr[12], &ptr[13], &ptr[14], &ptr[15], &ptr[16] );

		ptr = &q->quantChromaMAX[0];
		fscanf(fp,"%d, %d,%d,%d, %d,%d,%d, %d,%d,%d,%d, %d,%d,%d, %d,%d,%d\n", &ptr[0], &ptr[1], &ptr[2], &ptr[3], &ptr[4], &ptr[5], &ptr[6], &ptr[7], &ptr[8], &ptr[9], &ptr[10], &ptr[11], &ptr[12], &ptr[13], &ptr[14], &ptr[15], &ptr[16] );

		fscanf(fp,"scale=%d\n", &quantScaleFactor);
		fscanf(fp,"bitrate=%d\n", &q->TargetBitRate);
		fscanf(fp,"fixed quality=%d\n", &q->FixedQuality);
		fscanf(fp,"limit=%d\n", &q->quantLimit);

		fclose(fp);

		if (fp = fopen("c:\\cedoc\\quantnow.txt", "w"))
		{
			ptr = &q->quantLuma[0];
			fprintf(fp,"%d, %d,%d,%d, %d,%d,%d, %d,%d,%d,%d, %d,%d,%d, %d,%d,%d\n", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7], ptr[8], ptr[9], ptr[10], ptr[11], ptr[12], ptr[13], ptr[14], ptr[15], ptr[16] );

			ptr = &q->quantLumaMAX[0];
			fprintf(fp,"%d, %d,%d,%d, %d,%d,%d, %d,%d,%d,%d, %d,%d,%d, %d,%d,%d\n", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7], ptr[8], ptr[9], ptr[10], ptr[11], ptr[12], ptr[13], ptr[14], ptr[15], ptr[16] );

			ptr = &q->quantChroma[0];
			fprintf(fp,"%d, %d,%d,%d, %d,%d,%d, %d,%d,%d,%d, %d,%d,%d, %d,%d,%d\n", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7], ptr[8], ptr[9], ptr[10], ptr[11], ptr[12], ptr[13], ptr[14], ptr[15], ptr[16] );

			ptr = &q->quantChromaMAX[0];
			fprintf(fp,"%d, %d,%d,%d, %d,%d,%d, %d,%d,%d,%d, %d,%d,%d, %d,%d,%d\n", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7], ptr[8], ptr[9], ptr[10], ptr[11], ptr[12], ptr[13], ptr[14], ptr[15], ptr[16] );

			fprintf(fp,"scale=%d\n", quantScaleFactor);
			fprintf(fp,"bitrate=%d\n", q->TargetBitRate);
			fprintf(fp,"fixed quality=%d\n", q->FixedQuality);
			fprintf(fp,"limit=%d\n", q->quantLimit);

			fclose(fp);
		}
	}
}
*/

#if 0 //unused
void QuantizeBand(IMAGE *wavelet, int band, int divisor)
{
	int scale = wavelet->scale[band];

	// Check that the band index is valid
	assert(0 <= band && band < wavelet->num_bands);

	// Check that the quantization divisor is valid
	assert(divisor > 0);

	// Reduce the quantization by the amount that has already been applied to the band
	assert(wavelet->quantization[band] == 1);

	// Skip quantization if there is no quantization to be done
	if (divisor > 1)
	{
		// Quantize the coefficients in the specified wavelet band
		if (wavelet->pixel_type[band] == PIXEL_TYPE_16S)
		{
			//Quantize16sTo8s(wavelet->band[band], wavelet->width, wavelet->height, wavelet->pitch, divisor);
			//wavelet->pixel_type[band] = PIXEL_TYPE_8S;
			Quantize16s(wavelet->band[band], wavelet->width, wavelet->height, wavelet->pitch, divisor);
		}
		else {
			// The band must contain 8 bit signed coefficients
			PIXEL8S *data = (PIXEL8S *)(wavelet->band[band]);

			// Check that the pixel type is 8 bit signed
			assert(wavelet->pixel_type[band] == PIXEL_TYPE_8S);

			Quantize8s(data, wavelet->width, wavelet->height, wavelet->pitch, divisor);
		}

#if DEBUG
		quantization_count++;
#endif
	}

	// Reduce the wavelet display scale factor by the amount of quantization
	scale /= divisor;
	if (scale < 1) scale = 1;
	wavelet->scale[band] = scale;

#if 1
	// Force the highpass bands to be displayed as binary images
	// (Could also set the scale to zero, but this generates side effects in encoding)
	wavelet->highpass_display = HIGHPASS_DISPLAY_BINARY;
#else
	// Display the quantized coefficients as gray values
	wavelet->highpass_display = HIGHPASS_DISPLAY_GRAY;
#endif
}
#endif

#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void QuantizeRow16s(PIXEL16S *rowptr, int length, int divisor)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Quantize a row of 16-bit signed coefficients using inplace computation
void QuantizeRow16s(PIXEL16S *rowptr, int length, int divisor)
{
	short multiplier;
	int column;

#if (1 && XMMOPT)
	__m64 *group_ptr = (__m64 *)rowptr;
	__m64 zero_si64 = _mm_setzero_si64();
	__m64 round_pi16 = _mm_set1_pi16(1);
	__m64 quant_pi16;
	__m64 sign1_pi16;
	__m64 sign2_pi16;
	__m64 input1_pi16;
	__m64 input2_pi16;
	__m64 output1_pi16;
	__m64 output2_pi16;

	const int column_step = 4;
	const int post_column = length - (length % column_step);
#endif

	if (divisor <= 1) {
		//_mm_empty();	// Clear the MMX register state
		return;
	}

	//START(tk_quant);

	// Change division to multiplication by a fraction
	multiplier = (uint32_t)(1 << 16) / divisor;

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	quant_pi16 = _mm_set1_pi16(multiplier);

	// Quantize eight signed coefficients in parallel
	for (; column < post_column; column += column_step)
	{
		// Load eight pixels and compute the sign
		input1_pi16 = *(group_ptr);
		sign1_pi16 = _mm_cmpgt_pi16(zero_si64, input1_pi16);

		// Compute the absolute value
		input1_pi16 = _mm_xor_si64(input1_pi16, sign1_pi16);
		input1_pi16 = _mm_sub_pi16(input1_pi16, sign1_pi16);

		// Multiply by the quantization factor
		output1_pi16 = _mm_mulhi_pu16(input1_pi16, quant_pi16);

		// Restore the sign
		output1_pi16 = _mm_xor_si64(output1_pi16, sign1_pi16);
		output1_pi16 = _mm_sub_pi16(output1_pi16, sign1_pi16);

		// Save the packed results and advance to the next group
		*(group_ptr++) = output1_pi16;
	}

	//_mm_empty();	// Clear the MMX register state

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Finish the rest of the row
	for (; column < length; column++)
	{
		int value = rowptr[column];
		LONGWORD result;

		if (value >= 0)
		{
			result.longword = value * multiplier;
			value = (short)result.halfword.upper;
			rowptr[column] = SATURATE_16S(value);
		}
		else {
			value = (- value);
			result.longword = value * multiplier;
			value = -(short)result.halfword.upper;
			rowptr[column] = SATURATE_16S(value);
		}
	}

	//STOP(tk_quant);
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Quantize a row of 16-bit signed coefficients using inplace computation
void QuantizeRow16s(PIXEL16S *rowptr, int length, int divisor)
{
	short multiplier;
	int column;

#if (1 && XMMOPT)
	__m128i *group_ptr = (__m128i *)rowptr;
	__m128i zero_si128 = _mm_setzero_si128();
	__m128i quant_epi16;
	__m128i sign1_epi16;
	__m128i input1_epi16;
	__m128i output1_epi16;

	const int column_step = 8;
	const int post_column = length - (length % column_step);
#endif

	if (divisor <= 1) return;

	// Change division to multiplication by a fraction
	multiplier = (uint32_t)(1 << 16) / divisor;

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	quant_epi16 = _mm_set1_epi16(multiplier);

	// Quantize eight signed coefficients in parallel
	for (; column < post_column; column += column_step)
	{
		// Load eight pixels and compute the sign
		input1_epi16 = _mm_load_si128(group_ptr);
		sign1_epi16 = _mm_cmplt_epi16(input1_epi16, zero_si128);

		// Compute the absolute value
		input1_epi16 = _mm_xor_si128(input1_epi16, sign1_epi16);
		input1_epi16 = _mm_sub_epi16(input1_epi16, sign1_epi16);

		// Multiply by the quantization factor
		output1_epi16 = _mm_mulhi_epu16(input1_epi16, quant_epi16);

		// Restore the sign
		output1_epi16 = _mm_xor_si128(output1_epi16, sign1_epi16);
		output1_epi16 = _mm_sub_epi16(output1_epi16, sign1_epi16);

		// Save the packed results and advance to the next group
		_mm_store_si128(group_ptr++, output1_epi16);
	}

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Finish the rest of the row
	for (; column < length; column++)
	{
		int value = rowptr[column];
		LONGWORD result;

		if (value >= 0)
		{
			result.longword = value * multiplier;
			value = (short)result.halfword.upper;
			rowptr[column] = SATURATE_16S(value);
		}
		else {
			value = (- value);
			result.longword = value * multiplier;
			value = -(short)result.halfword.upper;
			rowptr[column] = SATURATE_16S(value);
		}
	}
}

#endif


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void QuantizeRow8s(PIXEL8S *rowptr, int length, int divisor)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Quantize a row of 8-bit signed coefficients using inplace computation
void QuantizeRow8s(PIXEL8S *rowptr, int length, int divisor)
{
	short multiplier;
	int column;

#if (1 && XMMOPT)
	__m64 *group_ptr = (__m64 *)rowptr;
	__m64 zero_si64 = _mm_setzero_si64();
	__m64 round_pi16 = _mm_set1_pi16(1);
	__m64 quant_pi16;
	__m64 group_pi8;
	__m64 sign_pi8;
	__m64 input1_pi16;
	__m64 input2_pi16;
	__m64 result_pi8;

	const int column_step = 8;
	const int post_column = length - (length % column_step);
#endif

	if (divisor <= 1) {
		//_mm_empty();	// Clear the MMX register state
		return;
	}

	//START(tk_quant);

	// Change division to multiplication by a fraction
	multiplier = (uint32_t)(1 << 16) / divisor;

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	quant_pi16 = _mm_set1_pi16(multiplier);

	// Quantize sixteen signed bytes in parallel
	for (; column < post_column; column += column_step)
	{
		// Load sixteen pixels and compute the sign
		group_pi8 = *(group_ptr);
		sign_pi8 = _mm_cmpgt_pi8(zero_si64, group_pi8);

		// Compute the absolute value
		group_pi8 = _mm_xor_si64(group_pi8, sign_pi8);
		group_pi8 = _mm_sub_pi8(group_pi8, sign_pi8);

		// Unpack the first (lower) eight pixels
		input1_pi16 = _mm_unpacklo_pi8(group_pi8, zero_si64);
		input1_pi16 = _mm_add_pi16(input1_pi16, round_pi16);

		// Multiply by the quantization factor
		input1_pi16 = _mm_mulhi_pu16(input1_pi16, quant_pi16);

		// Unpack the second (upper) eight pixels
		input2_pi16 = _mm_unpackhi_pi8(group_pi8, zero_si64);
		input2_pi16 = _mm_add_pi16(input2_pi16, round_pi16);

		// Multiply by the quantization factor
		input2_pi16 = _mm_mulhi_pu16(input2_pi16, quant_pi16);

		// Pack the results
		result_pi8 = _mm_packs_pi16(input1_pi16, input2_pi16);

		// Restore the sign
		result_pi8 = _mm_xor_si64(result_pi8, sign_pi8);
		result_pi8 = _mm_sub_pi8(result_pi8, sign_pi8);

		// Save the packed results and advance to the next group
		*(group_ptr++) = result_pi8;
	}

	//_mm_empty();	// Clear the MMX register state

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Finish the rest of the row
	for (; column < length; column++)
	{
		int value = rowptr[column];
		LONGWORD result;

		if (value >= 0)
		{
			result.longword = value * multiplier;
			value = (short)result.halfword.upper;
			rowptr[column] = SATURATE_8S(value);
		}
		else {
			value = (- value);
			result.longword = value * multiplier;
			value = -(short)result.halfword.upper;
			rowptr[column] = SATURATE_8S(value);
		}
	}

	//STOP(tk_quant);
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Quantize a row of 8-bit signed coefficients using inplace computation
void QuantizeRow8s(PIXEL8S *rowptr, int length, int divisor)
{
	short multiplier;
	int column;

#if (1 && XMMOPT)
	__m128i *group_ptr = (__m128i *)rowptr;
	__m128i zero_si128 = _mm_setzero_si128();
	//__m128i round_epi16 = _mm_set1_epi16(1);
	__m128i quant_epi16;
	__m128i group_epi8;
	__m128i sign_epi8;
	__m128i input1_epi16;
	__m128i input2_epi16;
	__m128i result_epi8;

	const int column_step = 16;
	const int post_column = length - (length % column_step);
#endif

	if (divisor <= 1) return;

	// Change division to multiplication by a fraction
	multiplier = (uint32_t)(1 << 16) / divisor;

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	quant_epi16 = _mm_set1_epi16(multiplier);

	// Quantize sixteen signed bytes in parallel
	for (; column < post_column; column += column_step)
	{
		// Load sixteen pixels and compute the sign
		group_epi8 = _mm_load_si128(group_ptr);
		sign_epi8 = _mm_cmplt_epi8(group_epi8, zero_si128);

		// Compute the absolute value
		group_epi8 = _mm_xor_si128(group_epi8, sign_epi8);
		group_epi8 = _mm_sub_epi8(group_epi8, sign_epi8);

		// Unpack the first (lower) eight pixels
		input1_epi16 = _mm_unpacklo_epi8(group_epi8, zero_si128);
		//input1_epi16 = _mm_add_epi16(input1_epi16, round_epi16);

		// Multiply by the quantization factor
		input1_epi16 = _mm_mulhi_epu16(input1_epi16, quant_epi16);

		// Unpack the second (upper) eight pixels
		input2_epi16 = _mm_unpackhi_epi8(group_epi8, zero_si128);
		//input2_epi16 = _mm_add_epi16(input2_epi16, round_epi16);

		// Multiply by the quantization factor
		input2_epi16 = _mm_mulhi_epu16(input2_epi16, quant_epi16);

		// Pack the results
		result_epi8 = _mm_packs_epi16(input1_epi16, input2_epi16);

		// Restore the sign
		result_epi8 = _mm_xor_si128(result_epi8, sign_epi8);
		result_epi8 = _mm_sub_epi8(result_epi8, sign_epi8);

		// Save the packed results and advance to the next group
		_mm_store_si128(group_ptr++, result_epi8);
	}

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Finish the rest of the row
	for (; column < length; column++)
	{
		int value = rowptr[column];
		LONGWORD result;

		if (value >= 0)
		{
			result.longword = value * multiplier;
			value = (short)result.halfword.upper;
			rowptr[column] = SATURATE_8S(value);
		}
		else {
			value = (- value);
			result.longword = value * multiplier;
			value = -(short)result.halfword.upper;
			rowptr[column] = SATURATE_8S(value);
		}
	}
}

#endif



#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void QuantizeRow16sTo16s(PIXEL *input, PIXEL *output, int length, int divisor)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif


// Quantize a row of 16-bit signed coefficients without overwriting the input
void QuantizeRow16sTo16s(PIXEL *input, PIXEL *output, int length, int divisor)
{
	short multiplier;
	int column;

// MMX version
 #if (1 && XMMOPT)
	__m64 *input_ptr = (__m64 *)input;
	__m64 *output_ptr = (__m64 *)output;
	__m64 zero_si64 = _mm_setzero_si64();
	__m64 quant_pi16;
	__m64 sign1_pi16;
	__m64 sign2_pi16;
	__m64 input1_pi16;
	__m64 input2_pi16;
	__m64 output1_pi16;
	__m64 output2_pi16;
	__m64 offset_pi16;

	const int column_step = 4;
	const int post_column = length - (length % column_step);
 #endif

#if MIDPOINT_PREQUANT
	int prequant_midpoint = 0;// divisor/2;
	if(g_midpoint_prequant >= 2 && g_midpoint_prequant < 9)
	{
		prequant_midpoint = divisor / g_midpoint_prequant;

		if(g_midpoint_prequant == 2) //CFEncode_Premphasis_Original
		{
			if(prequant_midpoint)
				prequant_midpoint--;
		}
	}
#endif

	if (divisor <= 1)
	{
		// Clear the MMX register state
		//_mm_empty();

		// Copy the input to the output without quantization
		memcpy(output, input, length * sizeof(PIXEL));

		return;
	}

	//START(tk_quant);

	// Change division to multiplication by a fraction
	multiplier = (uint32_t)(1 << 16) / divisor;

	// Start at the left end of the row
	column = 0;

 #if (1 && XMMOPT)

	quant_pi16 = _mm_set1_pi16(multiplier);

  #if MIDPOINT_PREQUANT
	offset_pi16 = _mm_set1_pi16(prequant_midpoint);
  #endif
	// Quantize eight signed coefficients in parallel
	for (; column < post_column; column += column_step)
	{
		// Load eight pixels and compute the sign
		input1_pi16 = *(input_ptr++);
		sign1_pi16 = _mm_cmpgt_pi16(zero_si64, input1_pi16);

		// Compute the absolute value
		input1_pi16 = _mm_xor_si64(input1_pi16, sign1_pi16);
		input1_pi16 = _mm_sub_pi16(input1_pi16, sign1_pi16);

  #if MIDPOINT_PREQUANT
		// Add the prequant_midpoint for quantization rounding
		input1_pi16 = _mm_add_pi16(input1_pi16, offset_pi16);
  #endif

		// Multiply by the quantization factor
		output1_pi16 = _mm_mulhi_pu16(input1_pi16, quant_pi16);

		// Restore the sign
		output1_pi16 = _mm_xor_si64(output1_pi16, sign1_pi16);
		output1_pi16 = _mm_sub_pi16(output1_pi16, sign1_pi16);

		// Save the packed results and advance to the next group
		*(output_ptr++) = output1_pi16;
	}

	//_mm_empty();	// Clear the MMX register state

	// Check that the loop terminated at the post processing column
	assert(column == post_column);
 #endif

	// Finish the rest of the row
	for (; column < length; column++)
	{
		int value = input[column];
		LONGWORD result;

		if (value >= 0)
		{
#if MIDPOINT_PREQUANT
			result.longword = (value + prequant_midpoint) * multiplier;
#else
			result.longword = value * multiplier;
#endif
			value = (short)result.halfword.upper;
			output[column] = SATURATE_16S(value);
		}
		else {
			value = (- value);
#if MIDPOINT_PREQUANT
			result.longword = (value + prequant_midpoint) * multiplier;
#else
			result.longword = value * multiplier;
#endif
			value = -(short)result.halfword.upper;
			output[column] = SATURATE_16S(value);
		}
	}

	//STOP(tk_quant);
}

#endif

#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

#if 1

// Quantize a row of 16-bit signed coefficients without overwriting the input
// Original version that did not unroll the fast loop
void QuantizeRow16sTo16s(PIXEL *input, PIXEL *output, int length, int divisor)
{
	uint32_t multiplier;
	int column;

 #if (1 && XMMOPT)

	__m128i *input_ptr = (__m128i *)input;
	__m128i *output_ptr = (__m128i *)output;
	__m128i zero_si128 = _mm_setzero_si128();
	__m128i quant_epi16;
	__m128i sign1_epi16;
	__m128i input1_epi16;
	__m128i output1_epi16;
	__m128i offset_epi16;

	const int column_step = 8;
	const int post_column = length - (length % column_step);
 #endif

#if MIDPOINT_PREQUANT
	int prequant_midpoint = 0;// divisor/2;
	if(g_midpoint_prequant >= 2 && g_midpoint_prequant < 9)
	{
		prequant_midpoint = divisor / g_midpoint_prequant;

		if(g_midpoint_prequant == 2) //CFEncode_Premphasis_Original
		{
			if(prequant_midpoint)
				prequant_midpoint--;
		}
	}
#endif

	if (divisor <= 1) {
		// Copy the input to the output without quantization
		memcpy(output, input, length * sizeof(PIXEL));

		return;
	}

	//START(tk_quant);

	// Change division to multiplication by a fraction
	multiplier = (uint32_t)(1 << 16) / divisor;

	// Start at the left column
	column = 0;

 #if (1 && XMMOPT)

	quant_epi16 = _mm_set1_epi16(multiplier);

  #if MIDPOINT_PREQUANT
	offset_epi16 = _mm_set1_epi16(prequant_midpoint);
  #endif

	// Quantize and pack eight signed coefficients per loop iteration
	for (; column < post_column; column += column_step)
	{
		input1_epi16 = _mm_load_si128(input_ptr++);

		// Compute the sign
		sign1_epi16 = _mm_cmplt_epi16(input1_epi16, zero_si128);

		// Compute the absolute value
		input1_epi16 = _mm_xor_si128(input1_epi16, sign1_epi16);
		input1_epi16 = _mm_sub_epi16(input1_epi16, sign1_epi16);

  #if MIDPOINT_PREQUANT
		// Add the prequant_midpoint for quantization rounding
		input1_epi16 = _mm_add_epi16(input1_epi16, offset_epi16);
  #endif

		// Multiply by the quantization factor
		output1_epi16 = _mm_mulhi_epu16(input1_epi16, quant_epi16);

		// Restore the sign
		output1_epi16 = _mm_xor_si128(output1_epi16, sign1_epi16);
		output1_epi16 = _mm_sub_epi16(output1_epi16, sign1_epi16);

  #if 0
		_mm_store_si128(output_ptr++, output1_epi16);
  #else
		_mm_stream_si128(output_ptr++, output1_epi16);
  #endif
	}

	// Check that the loop terminated at the post processing column
	assert(column == post_column);
 #endif

	// Finish the rest of the row
	for (; column < length; column++)
	{
		int value = input[column];
		LONGWORD result;

		if (value >= 0)
		{
#if MIDPOINT_PREQUANT
			result.longword = (value + prequant_midpoint) * multiplier;
#else
			result.longword = value * multiplier;
#endif
			value = (short)result.halfword.upper;
			output[column] = SATURATE_16S(value);
		}
		else {
			value = (- value);
#if MIDPOINT_PREQUANT
			result.longword = (value + prequant_midpoint) * multiplier;
#else
			result.longword = value * multiplier;
#endif
			value = -(short)result.halfword.upper;
			output[column] = SATURATE_16S(value);
		}
	}

	//STOP(tk_quant);
}

#elif 0

// Quantize a row of 16-bit signed coefficients without overwriting the input
// New version that unrolled the fast loop for faster performance
void QuantizeRow16sTo16s(PIXEL *input, PIXEL *output, int length, int divisor)
{
	short multiplier;
	int column;

#if MIDPOINT_PREQUANT
	int prequant_midpoint = 0;// divisor/2;
	if(g_midpoint_prequant >= 2 && g_midpoint_prequant < 9)
	{
		prequant_midpoint = divisor / g_midpoint_prequant;

		if(g_midpoint_prequant == 2) //CFEncode_Premphasis_Original
		{
			if(prequant_midpoint)
				prequant_midpoint--;
		}
	}
#endif

#if (1 && XMMOPT)

	__m128i *input_ptr = (__m128i *)input;
	__m128i *output_ptr = (__m128i *)output;
	__m128i zero_si128 = _mm_setzero_si128();
	__m128i quant_epi16;
	__m128i sign1_epi16;
	__m128i sign2_epi16;
	__m128i input1_epi16;
	__m128i input2_epi16;
	__m128i output1_epi16;
	__m128i output2_epi16;
	//__m128i output_epi8;
	__m128i offset_epi16;

	const int column_step = 16;
	const int post_column = length - (length % column_step);

#endif

	if (divisor <= 1) {
		// Copy the input to the output without quantization
		memcpy(output, input, length * sizeof(PIXEL));

		return;
	}

	//START(tk_quant);

	// Change division to multiplication by a fraction
	multiplier = (uint32_t)(1 << 16) / divisor;

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	quant_epi16 = _mm_set1_epi16(multiplier);

#if MIDPOINT_PREQUANT
	offset_epi16 = _mm_set1_epi16(prequant_midpoint);
#endif

	// Preload the first set of input values
	input1_epi16 = _mm_load_si128(input_ptr++);

	// Quantize and pack eight signed coefficients per loop iteration
	for (; column < post_column - column_step/2; column += column_step)
	{
		/***** First phase of the unrolled loop *****/

		// Preload the second set of input values
		input2_epi16 = _mm_load_si128(input_ptr++);

		// Compute the sign
		sign1_epi16 = _mm_cmplt_epi16(input1_epi16, zero_si128);

		// Compute the absolute value
		input1_epi16 = _mm_xor_si128(input1_epi16, sign1_epi16);
		input1_epi16 = _mm_sub_epi16(input1_epi16, sign1_epi16);

#if MIDPOINT_PREQUANT
		// Add the prequant_midpoint for quantization rounding
		input1_epi16 = _mm_add_epi16(input1_epi16, offset_epi16);
#endif
		// Multiply by the quantization factor
		output1_epi16 = _mm_mulhi_epu16(input1_epi16, quant_epi16);

		// Restore the sign
		output1_epi16 = _mm_xor_si128(output1_epi16, sign1_epi16);
		output1_epi16 = _mm_sub_epi16(output1_epi16, sign1_epi16);

		//_mm_store_si128(output_ptr++, output1_epi16);
		_mm_stream_si128(output_ptr++, output1_epi16);


		/***** Second phase of the unrolled loop *****/

		// Preload the next set of input values
		input1_epi16 = _mm_load_si128(input_ptr++);

		// Compute the sign
		sign2_epi16 = _mm_cmplt_epi16(input2_epi16, zero_si128);

		// Compute the absolute value
		input2_epi16 = _mm_xor_si128(input2_epi16, sign2_epi16);
		input2_epi16 = _mm_sub_epi16(input2_epi16, sign2_epi16);

#if MIDPOINT_PREQUANT
		// Add the prequant_midpoint for quantization rounding
		input2_epi16 = _mm_add_epi16(input2_epi16, offset_epi16);
#endif
		// Multiply by the quantization factor
		output2_epi16 = _mm_mulhi_epu16(input2_epi16, quant_epi16);

		// Restore the sign
		output2_epi16 = _mm_xor_si128(output2_epi16, sign2_epi16);
		output2_epi16 = _mm_sub_epi16(output2_epi16, sign2_epi16);

		//_mm_store_si128(output_ptr++, output1_epi16);
		_mm_stream_si128(output_ptr++, output2_epi16);
	}

	// Process the input values that were preloaded

	// Compute the sign
	sign1_epi16 = _mm_cmplt_epi16(input1_epi16, zero_si128);

	// Compute the absolute value
	input1_epi16 = _mm_xor_si128(input1_epi16, sign1_epi16);
	input1_epi16 = _mm_sub_epi16(input1_epi16, sign1_epi16);

#if MIDPOINT_PREQUANT
	// Add the prequant_midpoint for quantization rounding
	input1_epi16 = _mm_add_epi16(input1_epi16, offset_epi16);
#endif
	// Multiply by the quantization factor
	output1_epi16 = _mm_mulhi_epu16(input1_epi16, quant_epi16);

	// Restore the sign
	output1_epi16 = _mm_xor_si128(output1_epi16, sign1_epi16);
	output1_epi16 = _mm_sub_epi16(output1_epi16, sign1_epi16);

	//_mm_store_si128(output_ptr++, output1_epi16);
	_mm_stream_si128(output_ptr++, output1_epi16);

	column += column_step/2;

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Finish the rest of the row
	for (; column < length; column++)
	{
		int value = input[column];
		LONGWORD result;

		if (value >= 0)
		{
#if MIDPOINT_PREQUANT
			result.longword = (value + prequant_midpoint) * multiplier;
#else
			result.longword = value * multiplier;
#endif
			value = (short)result.halfword.upper;
			output[column] = SATURATE_16S(value);
		}
		else
		{
			value = (- value);
#if MIDPOINT_PREQUANT
			result.longword = (value + prequant_midpoint) * multiplier;
#else
			result.longword = value * multiplier;
#endif
			value = -(short)result.halfword.upper;
			output[column] = SATURATE_16S(value);
		}
	}

	//STOP(tk_quant);
}

#else

// Quantize a row of 16-bit signed coefficients without overwriting the input
// Third version with the two phases of computation interleaved
void QuantizeRow16sTo16s(PIXEL *input, PIXEL *output, int length, int divisor)
{
	short multiplier;
	int column;

#if MIDPOINT_PREQUANT
	int prequant_midpoint = 0;// divisor/2;
	if(g_midpoint_prequant >= 2 && g_midpoint_prequant < 9)
	{
		prequant_midpoint = divisor / g_midpoint_prequant;

		if(g_midpoint_prequant == 2) //CFEncode_Premphasis_Original
		{
			if(prequant_midpoint)
				prequant_midpoint--;
		}
	}
#endif

#if (1 && XMMOPT)

	__m128i *input_ptr = (__m128i *)input;
	__m128i *output_ptr = (__m128i *)output;
	__m128i zero_si128 = _mm_setzero_si128();
	__m128i quant_epi16;
	__m128i sign1_epi16;
	__m128i sign2_epi16;
	__m128i input1_epi16;
	__m128i input2_epi16;
	__m128i output1_epi16;
	__m128i output2_epi16;
	//__m128i output_epi8;
	__m128i offset_epi16;

	const int column_step = 16;
	const int post_column = length - (length % column_step);

#endif

	if (divisor <= 1) {
		// Copy the input to the output without quantization
		memcpy(output, input, length * sizeof(PIXEL));

		return;
	}

	//START(tk_quant);

	// Change division to multiplication by a fraction
	multiplier = (uint32_t)(1 << 16) / divisor;

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	quant_epi16 = _mm_set1_epi16(multiplier);

#if MIDPOINT_PREQUANT
	offset_epi16 = _mm_set1_epi16(prequant_midpoint);
#endif

	// Preload the first set of input values
	//input1_epi16 = _mm_load_si128(input_ptr++);

	// Quantize and pack eight signed coefficients per loop iteration
	for (; column < post_column - column_step; column += column_step)
	{
		// Preload the second set of input values
		//input2_epi16 = _mm_load_si128(input_ptr++);

		// Load the first set of input values
		input1_epi16 = _mm_load_si128(input_ptr++);

		// Load the second set of input values
		input2_epi16 = _mm_load_si128(input_ptr++);

		// Compute the sign
		sign1_epi16 = _mm_cmplt_epi16(input1_epi16, zero_si128);
		sign2_epi16 = _mm_cmplt_epi16(input2_epi16, zero_si128);

		// Compute the absolute value
		input1_epi16 = _mm_xor_si128(input1_epi16, sign1_epi16);
		input1_epi16 = _mm_sub_epi16(input1_epi16, sign1_epi16);

		input2_epi16 = _mm_xor_si128(input2_epi16, sign2_epi16);
		input2_epi16 = _mm_sub_epi16(input2_epi16, sign2_epi16);

#if MIDPOINT_PREQUANT
		// Add the prequant_midpoint for quantization rounding
		input1_epi16 = _mm_add_epi16(input1_epi16, offset_epi16);
		input2_epi16 = _mm_add_epi16(input2_epi16, offset_epi16);
#endif
		// Multiply by the quantization factor
		output1_epi16 = _mm_mulhi_epu16(input1_epi16, quant_epi16);
		output2_epi16 = _mm_mulhi_epu16(input2_epi16, quant_epi16);

		// Restore the sign
		output1_epi16 = _mm_xor_si128(output1_epi16, sign1_epi16);
		output1_epi16 = _mm_sub_epi16(output1_epi16, sign1_epi16);

		output2_epi16 = _mm_xor_si128(output2_epi16, sign2_epi16);
		output2_epi16 = _mm_sub_epi16(output2_epi16, sign2_epi16);

		//_mm_store_si128(output_ptr++, output1_epi16);
		_mm_stream_si128(output_ptr++, output1_epi16);

		//_mm_store_si128(output_ptr++, output2_epi16);
		_mm_stream_si128(output_ptr++, output2_epi16);
	}

#if 0
	// Process the input values that were preloaded

	// Compute the sign
	sign1_epi16 = _mm_cmplt_epi16(input1_epi16, zero_si128);

	// Compute the absolute value
	input1_epi16 = _mm_xor_si128(input1_epi16, sign1_epi16);
	input1_epi16 = _mm_sub_epi16(input1_epi16, sign1_epi16);

#if MIDPOINT_PREQUANT
	// Add the prequant_midpoint for quantization rounding
	input1_epi16 = _mm_add_epi16(input1_epi16, offset_epi16);
#endif
	// Multiply by the quantization factor
	output1_epi16 = _mm_mulhi_epu16(input1_epi16, quant_epi16);

	// Restore the sign
	output1_epi16 = _mm_xor_si128(output1_epi16, sign1_epi16);
	output1_epi16 = _mm_sub_epi16(output1_epi16, sign1_epi16);

	//_mm_store_si128(output_ptr++, output1_epi16);
	_mm_stream_si128(output_ptr++, output1_epi16);

	column += column_step/2;
#endif

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Finish the rest of the row
	for (; column < length; column++)
	{
		int value = input[column];
		LONGWORD result;

		if (value >= 0)
		{
#if MIDPOINT_PREQUANT
			result.longword = (value + prequant_midpoint) * multiplier;
#else
			result.longword = value * multiplier;
#endif
			value = (short)result.halfword.upper;
			output[column] = SATURATE_16S(value);
		}
		else
		{
			value = (- value);
#if MIDPOINT_PREQUANT
			result.longword = (value + prequant_midpoint) * multiplier;
#else
			result.longword = value * multiplier;
#endif
			value = -(short)result.halfword.upper;
			output[column] = SATURATE_16S(value);
		}
	}

	//STOP(tk_quant);
}

#endif

#endif


#if _HIGHPASS_CODED

// Quantize a row of 16-bit signed coefficients (overwriting the input row) and then
// encode the coefficients using run lengths and entropy codes for the final output
void QuantizeRow16sToCoded(ENCODER *encoder, BITSTREAM *stream, PIXEL *input, int length,
						   int gap, int divisor, int *zero_count_ptr, BOOL output_runs_flag)
{
	short multiplier;
	int column;

#if MIDPOINT_PREQUANT
	int prequant_midpoint = 0;// divisor/2;
	if(g_midpoint_prequant >= 2 && g_midpoint_prequant < 9)
	{
		prequant_midpoint = divisor / g_midpoint_prequant;

		if(g_midpoint_prequant == 2) //CFEncode_Premphasis_Original
		{
			if(prequant_midpoint)
				prequant_midpoint--;
		}
	}
#endif

	// Overwrite the input
	PIXEL *output = input;

	if (divisor > 1)
	{
#if (1 && XMMOPT)
		__m64 *input_ptr = (__m64 *)input;
		__m64 *output_ptr = (__m64 *)output;
		__m64 zero_si64 = _mm_setzero_si64();
		__m64 quant_pi16;
		__m64 sign1_pi16;
		__m64 sign2_pi16;
		__m64 input1_pi16;
		__m64 input2_pi16;
		__m64 output1_pi16;
		__m64 output2_pi16;
		__m64 offset_pi16;

		const int column_step = 4;
		const int post_column = length - (length % column_step);
#endif
		//START(tk_quant);

		// Change division to multiplication by a fraction
		multiplier = (uint32_t)(1 << 16) / divisor;

		// Start at the left end of the row
		column = 0;

#if (1 && XMMOPT)

		quant_pi16 = _mm_set1_pi16(multiplier);

#if MIDPOINT_PREQUANT
		offset_pi16 = _mm_set1_pi16(prequant_midpoint);
#endif
		// Quantize eight signed coefficients in parallel
		for (; column < post_column; column += column_step)
		{
			// Load eight pixels and compute the sign
			input1_pi16 = *(input_ptr++);
			sign1_pi16 = _mm_cmpgt_pi16(zero_si64, input1_pi16);

			// Compute the absolute value
			input1_pi16 = _mm_xor_si64(input1_pi16, sign1_pi16);
			input1_pi16 = _mm_sub_pi16(input1_pi16, sign1_pi16);

#if MIDPOINT_PREQUANT
			// Add the prequant_midpoint for quantization rounding
			input1_pi16 = _mm_add_pi16(input1_pi16, offset_pi16);
#endif
			// Multiply by the quantization factor
			output1_pi16 = _mm_mulhi_pu16(input1_pi16, quant_pi16);

			// Restore the sign
			output1_pi16 = _mm_xor_si64(output1_pi16, sign1_pi16);
			output1_pi16 = _mm_sub_pi16(output1_pi16, sign1_pi16);

			// Save the packed results and advance to the next group
			*(output_ptr++) = output1_pi16;
		}

		//_mm_empty();	// Clear the MMX register state

		// Check that the loop terminated at the post processing column
		assert(column == post_column);

#endif

		// Finish the rest of the row
		for (; column < length; column++)
		{
			int value = input[column];
			LONGWORD result;

			if (value >= 0)
			{
#if MIDPOINT_PREQUANT
				result.longword = (value + prequant_midpoint) * multiplier;
#else
				result.longword = value * multiplier;
#endif
				value = (short)result.halfword.upper;
				output[column] = SATURATE_16S(value);
			}
			else
			{
				value = NEG(value);
#if MIDPOINT_PREQUANT
				result.longword = (value + prequant_midpoint) * multiplier;
#else
				result.longword = value * multiplier;
#endif
				value = -(short)result.halfword.upper;
				output[column] = SATURATE_16S(value);
			}
		}

		//STOP(tk_quant);
	}

	// Encode the row of quantized coefficients
	EncodeQuantizedCoefficients(encoder, stream, input, length, gap, zero_count_ptr, output_runs_flag);
}

#endif


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void QuantizeRow16sTo8s(PIXEL16S *input, PIXEL8S *output, int length, int divisor)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Quantize a row of 16-bit signed coefficients and pack to 8 bits
void QuantizeRow16sTo8s(PIXEL16S *input, PIXEL8S *output, int length, int divisor)
{
	short multiplier;
	int column;

#if (1 && XMMOPT)
	__m64 *input_ptr = (__m64 *)input;
	__m64 *output_ptr = (__m64 *)output;
	__m64 zero_si64 = _mm_setzero_si64();
	//__m64 round_pi16 = _mm_set1_pi16(1);
	__m64 quant_pi16;
	__m64 sign1_pi16;
	__m64 sign2_pi16;
	__m64 input1_pi16;
	__m64 input2_pi16;
	__m64 output1_pi16;
	__m64 output2_pi16;
	__m64 offset_pi16;

	const int column_step = 8;
	const int post_column = length - (length % column_step);
#endif

#if MIDPOINT_PREQUANT
	int prequant_midpoint = 0;// divisor/2;
	if(g_midpoint_prequant >= 2 && g_midpoint_prequant < 9)
	{
		prequant_midpoint = divisor / g_midpoint_prequant;

		if(g_midpoint_prequant == 2) //CFEncode_Premphasis_Original
		{
			if(prequant_midpoint)
				prequant_midpoint--;
		}
	}
#endif

	if (divisor <= 1)
	{
		ROI roi = {length, 1};

		//_mm_empty();	// Clear the MMX register state

		// Convert the pixels without quantization
		Convert16sTo8s(input, 0, output, 0, roi);

		return;
	}

	//START(tk_quant);

	// Change division to multiplication by a fraction
	multiplier = (uint32_t)(1 << 16) / divisor;

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	quant_pi16 = _mm_set1_pi16(multiplier);

#if MIDPOINT_PREQUANT
	offset_pi16 = _mm_set1_pi16(prequant_midpoint);
#endif

	// Quantize and pack eight signed coefficients per loop iteration
	for (; column < post_column; column += column_step)
	{
		// Load the first four coefficients and compute the sign
		input1_pi16 = *(input_ptr++);

		// Add the rounding adjustment
		//input1_pi16 = _mm_add_pi16(input1_pi16, round_pi16);

		// Compute the sign
		sign1_pi16 = _mm_cmpgt_pi16(zero_si64, input1_pi16);

		// Compute the absolute value
		input1_pi16 = _mm_xor_si64(input1_pi16, sign1_pi16);
		input1_pi16 = _mm_sub_pi16(input1_pi16, sign1_pi16);

#if MIDPOINT_PREQUANT
		// Add the prequant_midpoint for quantization rounding
		input1_pi16 = _mm_add_pi16(input1_pi16, offset_pi16);
#endif

		// Multiply by the quantization factor
		output1_pi16 = _mm_mulhi_pu16(input1_pi16, quant_pi16);

		// Restore the sign
		output1_pi16 = _mm_xor_si64(output1_pi16, sign1_pi16);
		output1_pi16 = _mm_sub_pi16(output1_pi16, sign1_pi16);

		// Load the second four coefficients and compute the sign
		input2_pi16 = *(input_ptr++);

		// Add the rounding adjustment
		//input2_pi16 = _mm_add_pi16(input2_pi16, round_pi16);

		// Compute the sign
		sign2_pi16 = _mm_cmpgt_pi16(zero_si64, input2_pi16);

		// Compute the absolute value
		input2_pi16 = _mm_xor_si64(input2_pi16, sign2_pi16);
		input2_pi16 = _mm_sub_pi16(input2_pi16, sign2_pi16);

#if MIDPOINT_PREQUANT
		// Add the prequant_midpoint for quantization rounding
		input2_pi16 = _mm_add_pi16(input2_pi16, offset_pi16);
#endif

		// Multiply by the quantization factor
		output2_pi16 = _mm_mulhi_pu16(input2_pi16, quant_pi16);

		// Restore the sign
		output2_pi16 = _mm_xor_si64(output2_pi16, sign2_pi16);
		output2_pi16 = _mm_sub_pi16(output2_pi16, sign2_pi16);

		// Pack the two sets of four output values
		*(output_ptr++) = _mm_packs_pi16(output1_pi16, output2_pi16);
	}

	//_mm_empty();	// Clear the MMX register state

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Finish the rest of the row
	for (; column < length; column++)
	{
		int value = input[column];
		LONGWORD result;

		if (value >= 0)
		{
#if MIDPOINT_PREQUANT
			result.longword = (value + prequant_midpoint) * multiplier;
#else
			result.longword = value * multiplier;
#endif
			value = (short)result.halfword.upper;
			output[column] = SATURATE_8S(value);
		}
		else {
			value = (- value);
#if MIDPOINT_PREQUANT
			result.longword = (value + prequant_midpoint) * multiplier;
#else
			result.longword = value * multiplier;
#endif
			value = -(short)result.halfword.upper;
			output[column] = SATURATE_8S(value);
		}
	}

	//STOP(tk_quant);
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Quantize a row of 16-bit signed coefficients and pack to 8 bits
void QuantizeRow16sTo8s(PIXEL16S *input, PIXEL8S *output, int length, int divisor)
{
	unsigned short multiplier;
	int column;

#if (1 && XMMOPT)
	__m128i *input_ptr = (__m128i *)input;
	__m128i *output_ptr = (__m128i *)output;
	__m128i zero_si128 = _mm_setzero_si128();
	__m128i quant_epi16;
	__m128i sign1_epi16;
	__m128i sign2_epi16;
	__m128i input1_epi16;
	__m128i input2_epi16;
	__m128i output1_epi16;
	__m128i output2_epi16;
	__m128i output_epi8;
	__m128i offset_epi16;

	const int column_step = 16;
	const int post_column = length - (length % column_step);
#endif

#if MIDPOINT_PREQUANT
	int prequant_midpoint = 0;// divisor/2;
	if(g_midpoint_prequant >= 2 && g_midpoint_prequant < 9)
	{
		prequant_midpoint = divisor / g_midpoint_prequant;

		if(g_midpoint_prequant == 2) //CFEncode_Premphasis_Original
		{
			if(prequant_midpoint)
				prequant_midpoint--;
		}
	}
#endif

	if (divisor <= 1) {
		ROI roi = {length, 1};
		Convert16sTo8s(input, 0, output, 0, roi);
		goto finish;
	}

	//START(tk_quant);

	// Change division to multiplication by a fraction
	multiplier = (uint32_t)(1 << 16) / divisor;

	// Start at the left column
	column = 0;

#if (1 && XMMOPT)

	quant_epi16 = _mm_set1_epi16(multiplier);

	// Preload the first eight coefficients
	input1_epi16 = _mm_load_si128(input_ptr++);

#if MIDPOINT_PREQUANT
	offset_epi16 = _mm_set1_epi16(prequant_midpoint);
#endif

	// Quantize and pack eight signed coefficients per loop iteration
	for (; column < post_column; column += column_step)
	{
		// Preload the second eight coefficients
		input2_epi16 = _mm_load_si128(input_ptr++);

		// Add the rounding adjustment
		//input1_epi16 = _mm_add_epi16(input1_epi16, round_epi16);

		// Compute the sign
		sign1_epi16 = _mm_cmplt_epi16(input1_epi16, zero_si128);

		// Compute the absolute value
		input1_epi16 = _mm_xor_si128(input1_epi16, sign1_epi16);
		input1_epi16 = _mm_sub_epi16(input1_epi16, sign1_epi16);

#if MIDPOINT_PREQUANT
		// Add the prequant_midpoint for quantization rounding
		input1_epi16 = _mm_add_epi16(input1_epi16, offset_epi16);
#endif

		// Multiply by the quantization factor
		output1_epi16 = _mm_mulhi_epu16(input1_epi16, quant_epi16);

		// Restore the sign
		output1_epi16 = _mm_xor_si128(output1_epi16, sign1_epi16);
		output1_epi16 = _mm_sub_epi16(output1_epi16, sign1_epi16);

		// Preload the first eight coefficients
		input1_epi16 = _mm_load_si128(input_ptr++);

		// Add the rounding adjustment
		//input2_epi16 = _mm_add_epi16(input2_epi16, round_epi16);

		// Compute the sign
		sign2_epi16 = _mm_cmplt_epi16(input2_epi16, zero_si128);

		// Compute the absolute value
		input2_epi16 = _mm_xor_si128(input2_epi16, sign2_epi16);
		input2_epi16 = _mm_sub_epi16(input2_epi16, sign2_epi16);

#if MIDPOINT_PREQUANT
		// Add the prequant_midpoint for quantization rounding
		input2_epi16 = _mm_add_epi16(input2_epi16, offset_epi16);
#endif

		// Multiply by the quantization factor
		output2_epi16 = _mm_mulhi_epu16(input2_epi16, quant_epi16);

		// Restore the sign
		output2_epi16 = _mm_xor_si128(output2_epi16, sign2_epi16);
		output2_epi16 = _mm_sub_epi16(output2_epi16, sign2_epi16);

		// Pack the two sets of eight output values
		output_epi8 = _mm_packs_epi16(output1_epi16, output2_epi16);
#if 1
		_mm_store_si128(output_ptr++, output_epi8);
#else
		_mm_stream_si128(output_ptr++, output_epi8);
#endif
	}

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Finish the rest of the row
	for (; column < length; column++)
	{
		int value = input[column];
		LONGWORD result;

		if (value >= 0)
		{
			result.longword = value * multiplier;
			value = (short)result.halfword.upper;
			output[column] = SATURATE_8S(value);
		}
		else {
			value = (- value);
			result.longword = value * multiplier;
			value = -(short)result.halfword.upper;
			output[column] = SATURATE_8S(value);
		}
	}

	//STOP(tk_quant);

finish:
	//_mm_empty();	// Clear the mmx register state
	return;
}

#endif

#if 1

// This routine should be called DequantizeBandRow8sTo16s because it converts
// eight bit coefficients to 16 bit coefficients during dequantization

// This version computes the midpoint of the quantization interval


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void DequantizeBandRow(PIXEL8S *input, int width, int quantization, PIXEL *output)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

void DequantizeBandRow(PIXEL8S *input, int width, int quantization, PIXEL *output)
{
	// Start at the left column
	int column = 0;

	// Compute the offset within the quantization interval
#if !MIDPOINT_PREQUANT
	const int midpoint = (1 * quantization) / 2;
#else
	const int midpoint = 0;
#endif

#if (1 && XMMOPT)

	const int column_step = 8;
	const int post_column = width - (width % column_step);

	__m64 *input_ptr = (__m64 *)input;
	__m64 *output_ptr = (__m64 *)output;
	__m64 quant_pi16 = _mm_set1_pi16(quantization);
	__m64 input1_pi8;
	__m64 input2_pi8;
	__m64 input1_pi16;
	__m64 input2_pi16;
	__m64 output_pi16;
	__m64 sign_pi8;
	__m64 sign1_pi16;
	__m64 sign2_pi16;
	__m64 offset_pi16;
	__m64 zero_si64 = _mm_setzero_si64();

	// Preload the first eight input coefficients
	input1_pi8 = *(input_ptr++);
	//input1_pi8 = _mm_set_pi8(0, 1, 2, 3, 4, -1, -2, -3);

	// Compute the sign of the coefficients
	sign_pi8 = _mm_cmpgt_pi8(zero_si64, input1_pi8);

	// Compute the absolute value of the coefficients
	input1_pi8 = _mm_xor_si64(input1_pi8, sign_pi8);
	input1_pi8 = _mm_subs_pi8(input1_pi8, sign_pi8);

	// Unpack the first four coefficients and the sign
	input1_pi16 = _mm_unpacklo_pi8(input1_pi8, zero_si64);
	sign1_pi16 = _mm_unpacklo_pi8(sign_pi8, sign_pi8);

	// Dequantize eight coefficients per loop iteration
	for (; column < post_column; column += column_step)
	{
		// Load the next eight input coefficients
		input2_pi8 = *(input_ptr++);

		// Unpack the second four coefficients and the sign
		input2_pi16 = _mm_unpackhi_pi8(input1_pi8, zero_si64);
		sign2_pi16 = _mm_unpackhi_pi8(sign_pi8, sign_pi8);

		// Dequantize the first four coefficients
		output_pi16 = _mm_mullo_pi16(input1_pi16, quant_pi16);

#if !MIDPOINT_PREQUANT
		// Shift the nonzero results to the midpoint of the quantization interval
		offset_pi16 = _mm_set1_pi16(midpoint);
		offset_pi16 = _mm_andnot_si64(_mm_cmpeq_pi16(output_pi16, zero_si64), offset_pi16);
		output_pi16 = _mm_add_pi16(output_pi16, offset_pi16);
#endif

		// Restore the sign
		output_pi16 = _mm_xor_si64(output_pi16, sign1_pi16);
		output_pi16 = _mm_sub_pi16(output_pi16, sign1_pi16);

		// Store the dequantized coefficients
		*(output_ptr++) = output_pi16;

		// Dequantize the second four coefficients
		output_pi16 = _mm_mullo_pi16(input2_pi16, quant_pi16);

		// Shift the nonzero results to the midpoint of the quantization interval
//		offset_pi16 = _mm_set1_pi16(midpoint);
//		offset_pi16 = _mm_andnot_si64(_mm_cmpeq_pi16(output_pi16, zero_si64), offset_pi16);
//		output_pi16 = _mm_add_pi16(output_pi16, offset_pi16);

		// Restore the sign
		output_pi16 = _mm_xor_si64(output_pi16, sign2_pi16);
		output_pi16 = _mm_sub_pi16(output_pi16, sign2_pi16);

		// Store the dequantized coefficients
		*(output_ptr++) = output_pi16;

		// Use the next eight input coefficients for the next loop iteration
		input1_pi8 = input2_pi8;

		// Compute the sign of the coefficients
		sign_pi8 = _mm_cmpgt_pi8(zero_si64, input1_pi8);

		// Compute the absolute value of the coefficients
		input1_pi8 = _mm_xor_si64(input1_pi8, sign_pi8);
		input1_pi8 = _mm_subs_pi8(input1_pi8, sign_pi8);

		// Unpack the first four coefficients
		input1_pi16 = _mm_unpacklo_pi8(input1_pi8, zero_si64);
		sign1_pi16 = _mm_unpacklo_pi8(sign_pi8, sign_pi8);
	}

	//_mm_empty();	// Clear the mmx register state

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Undo quantization for the rest of the row
	for (; column < width; column++) {
		int value = input[column];

		// Dequantize the absolute value
		if (value >= 0) {
			value *= quantization;
		}
		else {
			value = (- value);
			value *= quantization;
			value = (- value);
		}

		// Store the dequantized coefficient
		output[column] = value;
	}
}

#endif

#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

void DequantizeBandRow(PIXEL8S *input, int width, int quantization, PIXEL *output)
{
	// Start at the left column
	int column = 0;

	// Compute the offset within the quantization interval
#if !MIDPOINT_PREQUANT
	const int midpoint = (1 * quantization) / 2;
#else
	const int midpoint = 0;
#endif

	// Length of the row in bytes
	const int row_size = width * sizeof(PIXEL);

	// Compute the cache line size for the prefetch cache
	const size_t prefetch_size = 2 * _CACHE_LINE_SIZE;

	// The distance (in bytes) for prefetching the next block of input data
	const size_t prefetch_offset = 1 * ALIGN(row_size, prefetch_size);

	// Address of the cache line for the current block
	//const char *block_address = (char *)ALIGN(input, prefetch_size);

#if (1 && XMMOPT)

	const int column_step = 16;
	const int post_column = width - (width % column_step);

	__m128i *input_ptr = (__m128i *)input;
	__m128i *output_ptr = (__m128i *)output;
	__m128i quant_epi16 = _mm_set1_epi16(quantization);
	__m128i input1_epi8;
	__m128i input2_epi8;
	__m128i input1_epi16;
	__m128i input2_epi16;
	__m128i output_epi16;
	__m128i sign_epi8;
	__m128i sign1_epi16;
	__m128i sign2_epi16;
	__m128i zero_si128 = _mm_setzero_si128();

#endif

	// Check that the addresses are properly aligned
	assert(ISALIGNED16(input));
	assert(ISALIGNED16(output));

	// Prefetch rows that may be used in the near future
	//_mm_prefetch(block_address + prefetch_offset, _MM_HINT_T2);

#if (1 && XMMOPT)

	// Preload the first sixteen input coefficients
	input1_epi8 = _mm_load_si128(input_ptr++);

	// Compute the sign of the coefficients
	sign_epi8 = _mm_cmpgt_epi8(zero_si128, input1_epi8);

	// Compute the absolute value of the coefficients
	input1_epi8 = _mm_xor_si128(input1_epi8, sign_epi8);
	input1_epi8 = _mm_subs_epi8(input1_epi8, sign_epi8);

	// Unpack the first eight coefficients and the sign
	input1_epi16 = _mm_unpacklo_epi8(input1_epi8, zero_si128);
	sign1_epi16 = _mm_unpacklo_epi8(sign_epi8, sign_epi8);

	// Dequantize sixteen coefficients per loop iteration
	for (; column < post_column; column += column_step)
	{

#if (1 && PREFETCH)
		// Prefetch input data that may be used in the near future
		//if (ISALIGNED(input_ptr, prefetch_size))
		{
			_mm_prefetch((const char *)input_ptr + prefetch_offset, _MM_HINT_T2);
		}
#endif

		// Load the next sixteen input coefficients
		input2_epi8 = _mm_load_si128(input_ptr++);

		// Unpack the second eight coefficients and the sign
		input2_epi16 = _mm_unpackhi_epi8(input1_epi8, zero_si128);
		sign2_epi16 = _mm_unpackhi_epi8(sign_epi8, sign_epi8);

		// Dequantize the first eight coefficients
		output_epi16 = _mm_mullo_epi16(input1_epi16, quant_epi16);

#if !MIDPOINT_PREQUANT
		// Shift the nonzero results to the midpoint of the quantization interval
		offset_epi16 = _mm_set1_epi16(midpoint);
		mask_epi16 = _mm_cmpeq_epi16(output_epi16, zero_si128);
		offset_epi16 = _mm_andnot_si128(mask_epi16, offset_epi16);
		output_epi16 = _mm_add_epi16(output_epi16, offset_epi16);
#endif

		// Restore the sign
		output_epi16 = _mm_xor_si128(output_epi16, sign1_epi16);
		output_epi16 = _mm_sub_epi16(output_epi16, sign1_epi16);

		// Store the dequantized coefficients
		_mm_store_si128(output_ptr++, output_epi16);

		// Dequantize the second eight coefficients
		output_epi16 = _mm_mullo_epi16(input2_epi16, quant_epi16);

#if !MIDPOINT_PREQUANT
		// Shift the nonzero results to the midpoint of the quantization interval
		offset_epi16 = _mm_set1_epi16(midpoint);
		mask_epi16 = _mm_cmpeq_epi16(output_epi16, zero_si128);
		offset_epi16 = _mm_andnot_si128(mask_epi16, offset_epi16);
		output_epi16 = _mm_add_epi16(output_epi16, offset_epi16);
#endif

		// Restore the sign
		output_epi16 = _mm_xor_si128(output_epi16, sign2_epi16);
		output_epi16 = _mm_sub_epi16(output_epi16, sign2_epi16);

		// Store the dequantized coefficients
		_mm_store_si128(output_ptr++, output_epi16);

		// Use the next eight input coefficients for the next loop iteration
		input1_epi8 = input2_epi8;

		// Compute the sign of the coefficients
		sign_epi8 = _mm_cmpgt_epi8(zero_si128, input1_epi8);

		// Compute the absolute value of the coefficients
		input1_epi8 = _mm_xor_si128(input1_epi8, sign_epi8);
		input1_epi8 = _mm_subs_epi8(input1_epi8, sign_epi8);

		// Unpack the first eight coefficients
		input1_epi16 = _mm_unpacklo_epi8(input1_epi8, zero_si128);
		sign1_epi16 = _mm_unpacklo_epi8(sign_epi8, sign_epi8);
	}

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

#endif

	// Undo quantization for the rest of the row
	for (; column < width; column++) {
		int value = input[column];

		// Dequantize the absolute value
		if (value > 0) {
			value = (quantization * value) + midpoint;
		}
		else if (value < 0) {
			value = (- value);
			value = (quantization * value) + midpoint;
			value = (- value);
		}

		// Store the dequantized coefficient
		output[column] = value;
	}
}


void DequantizeBandRow16s(PIXEL *input, int width, int quantization, PIXEL *output)
{
	// Start at the left column234
	int column = 0;

	// Compute the offset within the quantization interval
#if !MIDPOINT_PREQUANT
	const int midpoint = (1 * quantization) / 2;
#else
	const int midpoint = 0;
#endif

	// Length of the row in bytes
	//const int row_size = width * sizeof(PIXEL);

	// Compute the cache line size for the prefetch cache
	//const size_t prefetch_size = 2 * _CACHE_LINE_SIZE;

	// The distance (in bytes) for prefetching the next block of input data
	//const size_t prefetch_offset = 1 * ALIGN(row_size, prefetch_size);

	// Address of the cache line for the current block
	//const char *block_address = (char *)ALIGN(input, prefetch_size);
/*  Faulty???? ----------------------------------------------------------------------------------------------------------------
#if (1 && XMMOPT)

	const int column_step = 16;
	const int post_column = width - (width % column_step);

	__m128i *input_ptr = (__m128i *)input;
	__m128i *output_ptr = (__m128i *)output;
	__m128i quant_epi16 = _mm_set1_epi16(quantization);
	__m128i input1_epi8;
	__m128i input2_epi8;
	__m128i input1_epi16;
	__m128i input2_epi16;
	__m128i output_epi16;
	__m128i sign_epi8;
	__m128i sign1_epi16;
	__m128i sign2_epi16;
	__m128i offset_epi16;
	__m128i zero_si128 = _mm_setzero_si128();
	__m128i mask_epi16;

#endif

	// Check that the addresses are properly aligned
	assert(ISALIGNED16(input));
	assert(ISALIGNED16(output));

	// Prefetch rows that may be used in the near future
	//_mm_prefetch(block_address + prefetch_offset, _MM_HINT_T2);

#if (1 && XMMOPT)

	// Preload the first sixteen input coefficients
	input1_epi8 = _mm_load_si128(input_ptr++);

	// Compute the sign of the coefficients
	sign_epi8 = _mm_cmpgt_epi8(zero_si128, input1_epi8);

	// Compute the absolute value of the coefficients
	input1_epi8 = _mm_xor_si128(input1_epi8, sign_epi8);
	input1_epi8 = _mm_subs_epi8(input1_epi8, sign_epi8);

	// Unpack the first eight coefficients and the sign
	input1_epi16 = _mm_unpacklo_epi8(input1_epi8, zero_si128);
	sign1_epi16 = _mm_unpacklo_epi8(sign_epi8, sign_epi8);

	// Dequantize sixteen coefficients per loop iteration
	for (; column < post_column; column += column_step)
	{

	#if (1 && PREFETCH)
		// Prefetch input data that may be used in the near future
		//if (ISALIGNED(input_ptr, prefetch_size))
		{
			_mm_prefetch((const char *)input_ptr + prefetch_offset, _MM_HINT_T2);
		}
	#endif

		// Load the next sixteen input coefficients
		input2_epi8 = _mm_load_si128(input_ptr++);

		// Unpack the second eight coefficients and the sign
		input2_epi16 = _mm_unpackhi_epi8(input1_epi8, zero_si128);
		sign2_epi16 = _mm_unpackhi_epi8(sign_epi8, sign_epi8);

		// Dequantize the first eight coefficients
		output_epi16 = _mm_mullo_epi16(input1_epi16, quant_epi16);

	#if !MIDPOINT_PREQUANT
		// Shift the nonzero results to the midpoint of the quantization interval
		offset_epi16 = _mm_set1_epi16(midpoint);
		mask_epi16 = _mm_cmpeq_epi16(output_epi16, zero_si128);
		offset_epi16 = _mm_andnot_si128(mask_epi16, offset_epi16);
		output_epi16 = _mm_add_epi16(output_epi16, offset_epi16);
	#endif

		// Restore the sign
		output_epi16 = _mm_xor_si128(output_epi16, sign1_epi16);
		output_epi16 = _mm_sub_epi16(output_epi16, sign1_epi16);

		// Store the dequantized coefficients
		_mm_store_si128(output_ptr++, output_epi16);

		// Dequantize the second eight coefficients
		output_epi16 = _mm_mullo_epi16(input2_epi16, quant_epi16);

	#if !MIDPOINT_PREQUANT
		// Shift the nonzero results to the midpoint of the quantization interval
		offset_epi16 = _mm_set1_epi16(midpoint);
		mask_epi16 = _mm_cmpeq_epi16(output_epi16, zero_si128);
		offset_epi16 = _mm_andnot_si128(mask_epi16, offset_epi16);
		output_epi16 = _mm_add_epi16(output_epi16, offset_epi16);
	#endif

		// Restore the sign
		output_epi16 = _mm_xor_si128(output_epi16, sign2_epi16);
		output_epi16 = _mm_sub_epi16(output_epi16, sign2_epi16);

		// Store the dequantized coefficients
		_mm_store_si128(output_ptr++, output_epi16);

		// Use the next eight input coefficients for the next loop iteration
		input1_epi8 = input2_epi8;

		// Compute the sign of the coefficients
		sign_epi8 = _mm_cmpgt_epi8(zero_si128, input1_epi8);

		// Compute the absolute value of the coefficients
		input1_epi8 = _mm_xor_si128(input1_epi8, sign_epi8);
		input1_epi8 = _mm_subs_epi8(input1_epi8, sign_epi8);

		// Unpack the first eight coefficients
		input1_epi16 = _mm_unpacklo_epi8(input1_epi8, zero_si128);
		sign1_epi16 = _mm_unpacklo_epi8(sign_epi8, sign_epi8);
	}

	// Check that the loop terminated at the post processing column
	assert(column == post_column);

#endif
*/
	// Undo quantization for the rest of the row
	for (; column < width; column++) {
		int value = input[column];

		// Dequantize the absolute value
		if (value > 0) {
			value = (quantization * value) + midpoint;
		}
		else if (value < 0) {
			value = (- value);
			value = (quantization * value) + midpoint;
			value = (- value);
		}

		// Store the dequantized coefficient
		output[column] = value;
	}
}

#endif

#endif



// (quantMAX[subband] - quant[subband])*vbrscale - 256*quantMAX[subband] + 512*quant[subband]
#define VSCALE(q,m,v) (((m) - (q))*(v) - 256*(m) + 512*(q))

// Set the quantization divisors in the transform wavelets
void SetTransformQuantization(ENCODER *encoder, TRANSFORM *transform, int channel, float framerate)
{
	QUANTIZER *q = &encoder->q;
	//int previousbitcnt = encoder->lastgopbitcount;
	int64_t previousbitcnt = encoder->lastgopbitcount;
	int transform_type = transform->type;
	int num_frames = transform->num_frames;
	int num_levels = transform->num_levels;
	int num_wavelets = transform->num_wavelets;
	int num_spatial = transform->num_spatial;
	//static int vbrscale = 256;
	int vbrscale = encoder->vbrscale;
	int subband_count = 0;
	int num_lowpass_spatial;
	int num_highpass_spatial;
	IMAGE *wavelet;
	IMAGE *lowpass;
	int subband;
	int quant[MAX_QUANT_SUBBANDS];
	int quantMAX[MAX_QUANT_SUBBANDS];
	int index;
	int vscale;
	int quantization;
	int k;
	int currentbitrate;

	// Need to increase the quantization to compensate for higher precision?
	//int quant_10bit_shift = (encoder->codec.precision == CODEC_PRECISION_10BIT) ? 2 : 0;
	int quant_10bit_shift = 0;

	// Check that the number of levels and wavelets are in range
	assert(0 <= num_levels && num_levels <= CODEC_MAX_LEVELS);
	assert(0 <= num_wavelets && num_wavelets <= TRANSFORM_MAX_LEVELS);

	// Some formulas below only work for two frame (four field) GOP
	//assert(num_frames == 2);

	if (framerate > 10.0 && framerate < 120.0)
	{
		float bitcnt = (float)(int32_t)previousbitcnt;
		currentbitrate =  (int)(bitcnt * framerate / (float)encoder->gop_length); // 15 = 30fps;
	}
	else
	{
		float bitcnt = (float)(int32_t)previousbitcnt;
		currentbitrate =  (int)(bitcnt * 30.0 / (float)encoder->gop_length); // 15 = 30fps;
	}


#if TRACK_BITRATE
	if(channel == 0) // luma
	{
		FILE *fp = fopen("c:/rate.txt","a");

		fprintf(fp, "rate = %3.3f MBytes/s  @ %ffps (previousbitcnt=%d)", (float)currentbitrate / (8.0 * 1024.0 * 1024.0), framerate, previousbitcnt);
		fprintf(fp, "\n");

		fclose(fp);
	}
#endif

	START(tk_quant);

#if 1
	/***** Compute the number of subbands *****/

	if (num_frames > 1)
	{
		// One horizontal-temporal wavelet (three highpass bands) per frame
		subband_count += 3 * num_frames;

		// Temporal transform (one highpass band) between frames or
		// one lowpass band in the case of the field plus transform
		subband_count += 1;

		// Three highpass bands for each level in the spatial wavelet pyramid
		subband_count += 3 * num_spatial;

		// One lowpass band for the spatial wavelets from the temporal lowpass band
		subband_count += 1;
	}
	else
	{
		assert(num_frames == 1);

		// One horizontal-temporal wavelet (three highpass bands) per frame
		subband_count += 3 * num_frames;

		// Three highpass bands for each level in the spatial wavelet pyramid
		subband_count += 3 * num_spatial;

		// One lowpass band for the spatial wavelets from the temporal lowpass band
		subband_count += 1;
	}

	// The subbands are numbered with zero assigned to the lowpass band and
	// with the subband index increasing moving down the wavelet transform
#endif

#if DEBUG_VBR
	DumpText("prev bit count =%d\n", previousbitcnt);
#endif

	// Select the quantization limits for luma or chroma
	if (channel > 0)
	{
		memcpy(quant,q->quantChroma, MAX_QUANT_SUBBANDS*4);
		memcpy(quantMAX,q->quantChromaMAX, MAX_QUANT_SUBBANDS*4);
	//	quantMAX = q->quantChromaMAX;
	}
	else
	{
	//	quant = q->quantLuma;
	//	quantMAX = q->quantLumaMAX;
		memcpy(quant,q->quantLuma, MAX_QUANT_SUBBANDS*4);
		memcpy(quantMAX,q->quantLumaMAX, MAX_QUANT_SUBBANDS*4);
	}


	/***** Compute the factor that controls the bitrate by scaling the quantization *****/

//	{
//		char message[256];
//		sprintf(message, "currentbitrate = %d Mbs q->overbitrate = %d\n", currentbitrate/1000000, q->overbitrate);
//		OutputDebugString(message);
//	}


#if FIXED_DATA_RATE

	if (q->FixedQuality)
	{
		bool limiter_on = true;
		//static int over = 0;
		vbrscale = 256;

		if(q->overbitrate < 0 || q->overbitrate > 16)
			q->overbitrate = 0;

		// The 0 is for testing 4k (we don;t want the bit-rate limiter to function.
		if( encoder->frame->width > 1920 ||
			encoder->frame->height > 1080 ||
			encoder->frame->num_channels > 3 /* Bayer */ ||
			q->newQuality > 3 /*film Scan modes*/ ||
			encoder->frame->format == FRAME_FORMAT_RGB)
		{
			limiter_on = false;
		}

//#if BUILD_PROSPECT//10-bit for everyone
 #define BR_LIMIT 130000000	//130Mb/s
//#else
// #define BR_LIMIT 110000000	//110Mb/s
//#endif

#define BR_STEPS  10000000	//(BR_LIMIT/10)

		if(/*channel == 0 &&*/ limiter_on) //luma
		{
			int upperlimit = BR_LIMIT;

			switch(q->FixedQuality)
			{
				case 1://low
					upperlimit = BR_LIMIT - BR_STEPS*2; //90mb/s, 110mb/s
					break;
				case 2://medium
					upperlimit = BR_LIMIT;				//110mb/s 130mb/s
					break;
				case 3://high
					upperlimit = BR_LIMIT + BR_STEPS*2; //130mb/s 150mb/s
					break;
			}

			if(currentbitrate > upperlimit)		// If we exceed our limit, switch to the need quality level.
			{
			//	quant = quantMAX;				// switching to the next level
				memcpy(quant,quantMAX, MAX_QUANT_SUBBANDS*4);

				if(channel == 0)
				{
					if(q->overbitrate == 0)			// overbitrate of 0 means bit-rate is OK (under)
						q->overbitrate = 1;			// overbitrate of 1 means we have switched to a lower table

					if(currentbitrate > upperlimit*12/10 /* 120% */)
						q->overbitrate++;			// for way over bit-rate allow the quantization to change

					if(q->overbitrate > 16)			// limit the maximum quantization changes.
						q->overbitrate = 16;
				}
			}
			else if(q->overbitrate>0)
			{
				if(channel == 0)
				{
					if(q->overbitrate > 1 && currentbitrate < upperlimit /* 100% */)
						q->overbitrate--;
					else if(q->overbitrate == 1 && currentbitrate < upperlimit*8/10 /* 80% */)
						q->overbitrate=0;
				}

				if(q->overbitrate > 0)
				{
				//	quant = quantMAX;
					memcpy(quant,quantMAX, MAX_QUANT_SUBBANDS*4);
				}
			}

			if(q->overbitrate > 1)
			{
				int i;
				int ratecontrl = q->overbitrate - 1;

				if(q->progressive)
				{
					for(i=11; i<17; i++)
					{
						quant[i] *= (ratecontrl+4);
						quant[i] >>= 2;
					}
				}
				else
				{
//#if BUILD_PROSPECT	//10-bit for everyone
					// This gives good interlaced quality but high bit-rates.
					quant[11] *= (ratecontrl+4);
					quant[11] >>= 2;

					quant[14] *= (ratecontrl+4);
					quant[14] >>= 2;

					// horizontal lo-pass, vertical hi-pass
					quant[12] *= (ratecontrl/8+4);
					quant[12] >>= 2;

					quant[15] *= (ratecontrl/8+4);
					quant[15] >>= 2;

					// horizontal hi-pass, vertical hi-pass
					quant[13] *= (ratecontrl/8+4);
					quant[13] >>= 2;

					quant[16] *= (ratecontrl/8+4);
					quant[16] >>= 2;

				/*	if(ratecontrl == 15) // way over -- this reduces the data rate a little but too risking with more testing.
					{
						for(i=1; i<7; i++)
						{
							quant[i] *= 2;
						}
						for(i=8; i<11; i++)
						{
							quant[i] *= 2;
						}
					}*/
/*
#else

					// horizontal hi-pass, vertical lo-pass
					quant[11] *= (ratecontrl*3/2+4); // original
					quant[11] >>= 2;

					quant[14] *= (ratecontrl*3/2+4);
					quant[14] >>= 2;

					// horizontal lo-pass, vertical hi-pass
					quant[12] *= (ratecontrl/2+4);
					quant[12] >>= 2;

					quant[15] *= (ratecontrl/2+4);
					quant[15] >>= 2;

					// horizontal hi-pass, vertical hi-pass
					quant[13] *= (ratecontrl+4);
					quant[13] >>= 2;

					quant[16] *= (ratecontrl+4);
					quant[16] >>= 2;
#endif
*/
				}
			}
		}
	}
	else if (channel == 0)
	{
		if (previousbitcnt > TARGET_GOP_BITRATE(q))				// Approx 10Mbit/s
		{
			if (previousbitcnt > GOP_BITRATE_120PERCENT(q))		// Approx 12Mbit/s
				vbrscale *= 270;
			else
				vbrscale *= 260;

			vbrscale >>= 8;

#if DEBUG_VBR
			DumpText("Bitrate over = %d, ", previousbitcnt);
#endif
			if (vbrscale > VBR_MAX) vbrscale = VBR_MAX;
		}
		else if (previousbitcnt < GOP_BITRATE_95PERCENT(q))		// Approx 9.5MB/s
		{
			if (previousbitcnt < GOP_BITRATE_75PERCENT(q))		// Approx 7.5Mbit/s
				vbrscale *= 240;
			else
				vbrscale *= 250;

			vbrscale >>= 8;

#if DEBUG_VBR
			DumpText("Bitrate under = %d, ", previousbitcnt);
#endif
			if (vbrscale < VBR_MIN) vbrscale = VBR_MIN;
		}

#if DEBUG_VBR
		else
			DumpText("Bitrate on = %d, ", previousbitcnt);

		DumpText("vbrscale = %d\n", vbrscale);
#endif
	}

#else
	// No bitrate scaling
	vbrscale = 256;
#endif

	/***** Need to compute the scale of the transform wavelet bands *****/

	SetTransformScale(transform);


	/***** Compute the quantization for each subband that will be encoded *****/

	// Start with the last wavelet
	index = num_wavelets - 1;

	// Compute the quantization for the lowpass band (first band encoded)
	lowpass = transform->wavelet[index];
	quantization = quant[0] * lowpass->scale[0] >> quantScaleFactor;

	// Record the quantization in the encoder
	//encoder->quant[channel][0] = quantization;
	encoder->q.LowPassQuant[channel] = quantization;

	// Adjust the quantization to compensate for higher precision
	quantization <<= quant_10bit_shift;

	// The lowpass band is quantized during encoding
	lowpass->quant[0] = 1;
#if DEBUG_VBR
	DumpText("\n0:subband = %d, ", 0);
	DumpText("quantization = %d\n", quantization);
#endif
	// Set the quantization for the highpass bands
	switch (transform_type)
	{
	case TRANSFORM_TYPE_SPATIAL:

		subband = 1;

		index = num_wavelets - 1;

		// Compute the quantization for the lowpass band

		num_lowpass_spatial = num_spatial;
		num_levels = num_lowpass_spatial + 1;
		num_wavelets = num_levels;

		// Compute the quantization for the spatial transforms
		for (k = num_lowpass_spatial; k > 0; k--)
		{
			int band;

			// Get the wavelet that is used for scaling
			assert(index >= 0);
			wavelet = transform->wavelet[index--];

			// Set the quantization for the lowpass band (if not already set)
			if (wavelet != lowpass) wavelet->quant[0] = 1;

			// Set the quantization for the highpass bands
			for (band = 1; band < IMAGE_NUM_BANDS; band++)
			{
				// Check that the subband index is in range
				assert(0 <= subband && subband < CODEC_MAX_SUBBANDS);

				// Compute the variable bit rate scale factor
				//vscale = (quantMAX[subband] - quant[subband])*vbrscale - 256*quantMAX[subband] + 512*quant[subband];
				vscale = VSCALE(quant[subband], quantMAX[subband], vbrscale);

				// Compute the quantization
				quantization = ((vscale * wavelet->scale[band]) >> QUANT_VSCALE_SHIFT);

				// Record the quantization in the wavelet
				//encoder->quant[channel][subband] = quantization;

				quantization >>= quantScaleFactor;
				// Clamp the variable bit rate scale factor to the upper limit
				//if (quantization > limit[band]) quantization = limit[band];

				// Adjust the quantization to compensate for higher precision
				quantization <<= quant_10bit_shift;

#if MIDPOINT_PREQUANT //&& BUILD_PROSPECT//10-bit for everyone
				if(!(encoder->encoder_quality & 0x10000000 /*CFEncode_MicrosoftMode*/))
				{
					if(q->midpoint_prequant) // default is 2
					{
						quantization *= (q->midpoint_prequant);
						quantization /= ((q->midpoint_prequant-1)*2);
					}
					else
					{
						quantization /= 2;
					}
				}
#endif


#if LOSSLESS
	quantization = 1;
#endif
				wavelet->quant[band] = quantization;
#if DEBUG_VBR
				DumpText("1:subband = %d, ", subband);
				DumpText("wavelet->scale[band] = %d, ", wavelet->scale[band]);
				DumpText("quantization = %d\n", quantization);
#endif
				subband++;
			}
		}

		// Compute the quantization for the frame transform (three highpass bands)
		{
			int band;

			// Get the wavelet that is used for scaling
			assert(index == 0);
			//wavelet = transform->wavelet[index];
			wavelet = transform->wavelet[0];

			// Set the quantization for the lowpass band
			wavelet->quant[0] = 1;

			for (band = 1; band < IMAGE_NUM_BANDS; band++)
			{
				// Compute the variable bit rate scale factor
				vscale = VSCALE(quant[subband], quantMAX[subband], vbrscale);

				// Clamp the variable bit rate scale factor to the upper limit
				//if (vscale > limit[band]) vscale = limit[band];

				// Compute the quantization
				quantization = (vscale >> QUANT_VSCALE_SHIFT);

				vscale >>= quantScaleFactor;

				// Adjust the quantization to compensate for higher precision
				quantization <<= quant_10bit_shift;

#if MIDPOINT_PREQUANT
				if(q->midpoint_prequant) // default is 2
				{
					quantization *= (q->midpoint_prequant);
					quantization /= ((q->midpoint_prequant-1)*2);
				}
				else
				{
					quantization /= 2;
				}
#endif
				// Record the quantization in the wavelet
				//encoder->quant[channel][subband] = quantization;
#if LOSSLESS
	quantization = 1;
#endif
				wavelet->quant[band] = quantization;

#if DEBUG_VBR
				DumpText("3:subband = %d, ", subband);
				DumpText("wavelet->scale[band] = %d, ", wavelet->scale[band]);
				DumpText("quantization = %d\n", quantization);
#endif
				subband++;
			}
		}


		break;		// Done computing the quantization values for the spatial transform

	case TRANSFORM_TYPE_FIELD:

		/***** The code for this case needs to be updated and tested *****/

		subband = 1;

		num_lowpass_spatial = num_spatial;
		num_highpass_spatial = 0;
		num_levels = num_lowpass_spatial + 2;
		num_wavelets = num_spatial + 3;

		// Compute the quantization for the spatial wavelets
		for (k = num_lowpass_spatial; k > 0; k--)
		{
			int band;

			wavelet = transform->wavelet[index--];

			for (band = 1; band < IMAGE_NUM_BANDS; band++)
			{
				// Check that the subband index is in range
				assert(0 <= subband && subband < CODEC_MAX_SUBBANDS);

				// Compute the variable bit rate scale factor
				//vscale = (quantMAX[subband] - quant[subband])*vbrscale - 256*quantMAX[subband] + 512*quant[subband];
				vscale = VSCALE(quant[subband], quantMAX[subband], vbrscale);
				vscale >>= quantScaleFactor;

				// Clamp the variable bit rate scale factor to the upper limit
				if (vscale > q->quantLimit) vscale = q->quantLimit;

				// Compute the quantization
				quantization = (vscale * wavelet->scale[band]) >> QUANT_VSCALE_SHIFT;

#if MIDPOINT_PREQUANT //&& BUILD_PROSPECT//10-bit for everyone
				if(!(encoder->encoder_quality & 0x10000000 /*CFEncode_MicrosoftMode*/))
				{
					if(q->midpoint_prequant) // default is 2
					{
						quantization *= (q->midpoint_prequant);
						quantization /= ((q->midpoint_prequant-1)*2);
					}
					else
					{
						quantization /= 2;
					}
				}
#endif
				// Record the quantization in the wavelet
				//encoder->quant[channel][subband] = quantization;
#if LOSSLESS
	quantization = 1;
#endif
				wavelet->quant[band] = quantization;
				subband++;
			}
		}

		// Compute the quantization for the temporal wavelet between the frame transforms
		wavelet = transform->wavelet[index--];

		// Compute the variable bit rate scale factor
		//vscale = (quantMAX[subband] - quant[subband])*vbrscale - 256*quantMAX[subband] + 512*quant[subband];
		vscale = VSCALE(quant[subband], quantMAX[subband], vbrscale);
		vscale >>= quantScaleFactor;

		// Clamp the variable bit rate scale factor to the upper limit
		if (vscale > q->quantLimit) vscale = q->quantLimit;

		// Compute the quantization
		quantization = vscale >> QUANT_VSCALE_SHIFT;

		//encoder->quant[channel][subband] = vscale;
		wavelet->quantization[0] = 1;
#if LOSSLESS
	quantization = 1;
#endif
		wavelet->quant[1] = quantization;
		subband++;

		// Compute the quantization for the two frame transforms (three highpass bands each)
		for (k = 2; k > 0; k--)
		{
			int band;

			wavelet = transform->wavelet[index--];

			for (band = 1; band < IMAGE_NUM_BANDS; band++)
			{
				// Compute the variable bit rate scale factor
				vscale = VSCALE(quant[subband], quantMAX[subband], vbrscale);
				vscale >>= quantScaleFactor;

				// Clamp the variable bit rate scale factor to the upper limit
				if (vscale > q->quantLimit) vscale = q->quantLimit;

				// Compute the quantization
				quantization = (vscale >> QUANT_VSCALE_SHIFT) << _FRAME_PRESCALE;

#if MIDPOINT_PREQUANT
				if(q->midpoint_prequant) // default is 2
				{
					quantization *= (q->midpoint_prequant);
					quantization /= ((q->midpoint_prequant-1)*2);
				}
				else
				{
					quantization /= 2;
				}
#endif
				// Record the quantization in the wavelet
				//encoder->quant[channel][subband] = quantization;
#if LOSSLESS
	quantization = 1;
#endif
				wavelet->quant[band] = quantization;
				subband++;
			}
		}

		break;		// Done computing the quantization values for the field transform

	case TRANSFORM_TYPE_FIELDPLUS:

		subband = 1;

		index = num_wavelets - 1;

		// Compute the quantization for the lowpass band

		num_highpass_spatial = 1;
		num_lowpass_spatial = num_spatial - num_highpass_spatial;
		num_levels = num_lowpass_spatial + 2;
		num_wavelets = num_spatial + 3;

		// Compute the quantization for the spatial transforms from the lowpass temporal band
		for (k = num_lowpass_spatial; k > 0; k--)
		{
			int band;

			// Get the wavelet that is used for scaling
			assert(index >= 0);
			wavelet = transform->wavelet[index--];

			// Set the quantization for the lowpass band (if not already set)
			if (wavelet != lowpass) wavelet->quant[0] = 1;

			// Set the quantization for the highpass bands
			for (band = 1; band < IMAGE_NUM_BANDS; band++)
			{
				// Check that the subband index is in range
				assert(0 <= subband && subband < CODEC_MAX_SUBBANDS);

				// Compute the variable bit rate scale factor
				//vscale = (quantMAX[subband] - quant[subband])*vbrscale - 256*quantMAX[subband] + 512*quant[subband];
				vscale = VSCALE(quant[subband], quantMAX[subband], vbrscale);

				// Compute the quantization
				quantization = ((vscale * wavelet->scale[band]) >> QUANT_VSCALE_SHIFT);

				// Record the quantization in the wavelet
				//encoder->quant[channel][subband] = quantization;

				quantization >>= quantScaleFactor;
				// Clamp the variable bit rate scale factor to the upper limit
				//if (quantization > limit[band]) quantization = limit[band];

#if MIDPOINT_PREQUANT //&& BUILD_PROSPECT//10-bit for everyone
				if(!(encoder->encoder_quality & 0x10000000 /*CFEncode_MicrosoftMode*/))
				{
					if(q->midpoint_prequant) // default is 2
					{
						quantization *= (q->midpoint_prequant);
						quantization /= ((q->midpoint_prequant-1)*2);
					}
					else
					{
						quantization /= 2;
					}
				}
#endif

#if LOSSLESS
	quantization = 1;
#endif
				wavelet->quant[band] = quantization;
#if DEBUG_VBR
				DumpText("1:subband = %d, ", subband);
				DumpText("wavelet->scale[band] = %d, ", wavelet->scale[band]);
				DumpText("quantization = %d\n", quantization);
#endif
				subband++;
			}
		}

		// Compute the quantization for the lowpass band at the top of the wavelet tree
		// that is computed from the highpass band in the temporal transform between frames

		lowpass = transform->wavelet[index];

		// Compute the variable bit rate scale factor
		vscale = VSCALE(quant[subband], quantMAX[subband], vbrscale);

		// Compute the quantization
		quantization = (vscale * lowpass->scale[0]) >> QUANT_VSCALE_SHIFT;

		// Record the quantization in the wavelet
		//encoder->quant[channel][subband] = quantization;

		quantization >>= quantScaleFactor;
		// Clamp the variable bit rate scale factor to the upper limit
		//if (quantization > q->quantLimit) quantization = q->quantLimit;

#if LOSSLESS
	quantization = 1;
#endif
		lowpass->quant[0] = quantization;

#if DEBUG_VBR
		DumpText("5:subband = %d, ", subband);
		DumpText("wavelet->scale[band] = %d, ", lowpass->scale[0]);
		DumpText("quantization = %d\n", quantization);
#endif

		subband++;

		// Compute the quantization for the spatial transforms from the highpass temporal band
		for (k = num_highpass_spatial; k > 0; k--)
		{
			int band;

			// Get the wavelet that is used for scaling
			assert(index >= 0);
			wavelet = transform->wavelet[index--];

			// Set the quantization for the lowpass band (if not already set)
			if (wavelet != lowpass) wavelet->quant[0] = 1;

			for (band = 1; band < IMAGE_NUM_BANDS; band++)
			{
				// Check that the subband index is in range
				assert(0 <= subband && subband < CODEC_MAX_SUBBANDS);

				// Compute the variable bit rate scale factor
				vscale = VSCALE(quant[subband], quantMAX[subband], vbrscale);

				// Compute the quantization
				quantization = (vscale * wavelet->scale[band]) >> QUANT_VSCALE_SHIFT;

				// Record the quantization in the wavelet
				//encoder->quant[channel][subband] = quantization;

				quantization >>= quantScaleFactor;
				// Clamp the variable bit rate scale factor to the upper limit
				//if (quantization > limit[band]) quantization = limit[band];

#if MIDPOINT_PREQUANT //&& BUILD_PROSPECT//10-bit for everyone
				if(!(encoder->encoder_quality & 0x10000000 /*CFEncode_MicrosoftMode*/))
				{
					if(q->midpoint_prequant) // default is 2
					{
						quantization *= (q->midpoint_prequant);
						quantization /= ((q->midpoint_prequant-1)*2);
					}
					else
					{
						quantization /= 2;
					}
				}
#endif

#if LOSSLESS
	quantization = 1;
#endif
				wavelet->quant[band] = quantization;
#if DEBUG_VBR
				DumpText("2:subband = %d, ", subband);
				DumpText("wavelet->scale[band] = %d, ", wavelet->scale[band]);
				DumpText("quantization = %d\n", quantization);
#endif
				subband++;
			}
		}

		// Skip the temporal transform
		index--;

		// Compute the quantization for the two frame transforms (three highpass bands each)
		for (k = 2; k > 0; k--)
		{
			int band;

			// Get the wavelet that is used for scaling
			assert(index >= 0);
			wavelet = transform->wavelet[index--];

			// Set the quantization for the lowpass band
			wavelet->quant[0] = 1;

			for (band = 1; band < IMAGE_NUM_BANDS; band++)
			{
				// Compute the variable bit rate scale factor
				vscale = VSCALE(quant[subband], quantMAX[subband], vbrscale);

				// Clamp the variable bit rate scale factor to the upper limit
				//if (vscale > limit[band]) vscale = limit[band];

				// Compute the quantization
				quantization = (vscale >> QUANT_VSCALE_SHIFT);

				vscale >>= quantScaleFactor;

#if MIDPOINT_PREQUANT
				if(q->midpoint_prequant) // default is 2
				{
					quantization *= (q->midpoint_prequant);
					quantization /= ((q->midpoint_prequant-1)*2);
				}
				else
				{
					quantization /= 2;
				}
#endif
				// Record the quantization in the wavelet
				//encoder->quant[channel][subband] = quantization;
#if LOSSLESS
	quantization = 1;
#endif
				wavelet->quant[band] = quantization;

#if DEBUG_VBR
				DumpText("3:subband = %d, ", subband);
				DumpText("wavelet->scale[band] = %d, ", wavelet->scale[band]);
				DumpText("quantization = %d\n", quantization);
#endif
				subband++;
			}
		}

		break;			// Done computing the quantization values for the fieldplus transform

	default:
		assert(0);		// Other type of transforms not yet defined
		break;
	}

	// Should have processed all subbands
	assert(subband == subband_count);

	// Save encoding information in the encoder state
	//encoder->gop_length = gop_length;
	//encoder->num_spatial = num_spatial;
	encoder->num_levels = num_levels;
	encoder->num_subbands = subband_count;

	// Assume that all channels have the same number of quantization subbands
	encoder->num_quant_subbands = subband_count;

	// Save the updated variable bitrate scale factor
	encoder->vbrscale = vbrscale;

	STOP(tk_quant);
}


#if (_DEBUG || _TIMING)

void PrintTransformPrescale(TRANSFORM *transform, FILE *file)
{
	if (transform != NULL)
	{
		fprintf(file, "Transform prescale: %d %d %d %d\n",
			transform->prescale[0], transform->prescale[0], transform->prescale[0], transform->prescale[0]);
	}
}

#endif


#if (_DEBUG || _TIMING)

void PrintTransformQuantization(TRANSFORM *transform, FILE *file)
{
	int num_wavelets = transform->num_wavelets;
	int k;

	for (k = 0; k < num_wavelets; k++)
	{
		IMAGE *wavelet = transform->wavelet[k];

		assert(wavelet != NULL);

		switch (wavelet->wavelet_type)
		{

		case WAVELET_TYPE_HORIZONTAL:		// One highpass band
		case WAVELET_TYPE_VERTICAL:
		case WAVELET_TYPE_TEMPORAL:
			fprintf(file, "Wavelet quant: %d %d\n", wavelet->quant[0], wavelet->quant[1]);
			break;

		case WAVELET_TYPE_SPATIAL:			// Three highpass bands
		case WAVELET_TYPE_HORZTEMP:
		case WAVELET_TYPE_VERTTEMP:
			fprintf(file, "Wavelet quant: %d %d %d %d\n",
					wavelet->quant[0], wavelet->quant[1], wavelet->quant[2], wavelet->quant[3]);
			break;

		case WAVELET_TYPE_IMAGE:			// Not really a wavelet
		case WAVELET_TYPE_TEMPQUAD:			// Should not occur in normal code
		case WAVELET_TYPE_HORZQUAD:
		default:
			assert(0);
			break;
		}
	}
}

#endif


#if (_DEBUG || _TIMING)

// Print the values in a quantizer
void PrintQuantizer(QUANTIZER *q, FILE *logfile)
{
	int num_channels = CODEC_MAX_CHANNELS;
	int num_subbands = NUM_QUANT_SUBBANDS;
	int i;

	fprintf(logfile, "%16s: %d\n", "TargetBitRate", q->TargetBitRate);
	fprintf(logfile, "%16s: %d\n", "FixedQuality", q->FixedQuality);
	fprintf(logfile, "%16s: %d\n", "quantlimit", q->quantLimit);

	fprintf(logfile, "%16s:", "LowPassQuant");
	for (i = 0; i < num_channels; i++) {
		fprintf(logfile, " %3d", q->LowPassQuant[i]);
	}
	fprintf(logfile, "\n");

	fprintf(logfile, "%16s:", "quantLuma");
	for (i = 0; i < num_subbands; i++) {
		fprintf(logfile, " %3d", q->quantLuma[i]);
	}
	fprintf(logfile, "\n");

	fprintf(logfile, "%16s:", "quantLumaMax");
	for (i = 0; i < num_subbands; i++) {
		fprintf(logfile, " %3d", q->quantLumaMAX[i]);
	}
	fprintf(logfile, "\n");

	fprintf(logfile, "%16s:", "quantChroma");
	for (i = 0; i < num_subbands; i++) {
		fprintf(logfile, " %3d", q->quantChroma[i]);
	}
	fprintf(logfile, "\n");

	fprintf(logfile, "%16s:", "quantChromaMax");
	for (i = 0; i < num_subbands; i++) {
		fprintf(logfile, " %3d", q->quantChromaMAX[i]);
	}
	fprintf(logfile, "\n");

	fprintf(logfile, "%16s: %d\n", "overbitrate", q->overbitrate);
	fprintf(logfile, "%16s: %d\n", "progressive", q->progressive);
	fprintf(logfile, "%16s: %d\n", "newQuality", q->newQuality);
	fprintf(logfile, "%16s: %d\n", "midpoint_prequant", q->midpoint_prequant);
	fprintf(logfile, "%16s: %d\n", "FSratelimiter", q->FSratelimiter);
	fprintf(logfile, "%16s: %d\n", "inputFixedQuality", q->inputFixedQuality);

	fprintf(logfile, "%16s:", "codebookflags");
	for (i = 0; i < num_subbands; i++) {
		fprintf(logfile, " %3d", q->codebookflags[i]);
	}
	fprintf(logfile, "\n");
}

#endif


#if _TEST

// Test quantization of signed byte coefficients
int32_t TestQuantization(unsigned int seed, FILE *logfile)
{
	static PIXEL8S test[] = {0, 1, 2, 3, 4, 5, -1, -2, -3, -5, 10, 20, 30, 40, 50, 60,
							 0, 1, 2, 3, 4, 5, -1, -2, -3, -5, 10, 20, 30, 40, 50, 60,
							 0, 1, 2, 3, 4, 5, -1, -2, -3, -5, 10, 20, 30, 40, 50, 60};
	__declspec(align(16)) static PIXEL8S test1[sizeof(test)/sizeof(test[0])];
	__declspec(align(16)) static PIXEL8S test2[sizeof(test)/sizeof(test[0])];
	int height = 3;
	int width = sizeof(test)/(height * sizeof(test[0]));
	int pitch = width * sizeof(test[0]);
	int divisor = 5;
	int error;
	int sum;
	int i;

	// Fill the input data with random numbers if a seed was provided
	if (seed > 0) {
		srand(seed);
		for (i = 0; i < sizeof(test)/sizeof(test[0]); i++)
			test[i] = (char)(rand() % 256);

		divisor = rand() % 128;
	}

	// Quantize the test data
	memcpy(test1, test, sizeof(test));
	Quantize8s(test1, width, height, pitch, divisor);

	// Quantize the copy of the test data
	memcpy(test2, test, sizeof(test));
	for (i = 0; i < sizeof(test2)/sizeof(test2[0]); i++) {
		if (divisor > 1) test2[i] /= divisor;
	}

	// Report the difference between the quantization methods
	if (logfile) fprintf(logfile, "Test run: %d, divisor: %d\n", seed, divisor);
	sum = 0;
	error = 0;
	for (i = 0; i < sizeof(test)/sizeof(test[0]); i++) {
		int delta = test1[i] - test2[i];
		sum += abs(delta);
		error += delta;
		if (logfile)
			//if (delta != 0)
				fprintf(logfile, "%5d %5d %5d %5d\n", test[i], test1[i], test2[i], delta);
	}

	return error;
}

// Test quantization of a single row of coefficients
int32_t TestQuantizeRow(unsigned int seed, FILE *logfile)
{
	__declspec(align(16))
	static PIXEL16S input[] = {
//		2, 3, 0, 0, 0, 3, 4, 3, -2, -2, 4, 5, 4, 3, 1, 2, 2, 3, -4, -6, 2, 3, 3, 2
		16, 24, 0, 0, 0, 24, 32, 24, -16, -16, 32, 40, 32, 24, 8, 16, 16, 24, -32, -48, 16, 24, 24, 16
	};
	__declspec(align(16))
	static PIXEL8S output[24];

	__declspec(align(16))
	static PIXEL16S result[24];

	static PIXEL16S residual[24];

	int length = sizeof(input)/sizeof(input[0]);
	int divisor = 8;
	int midpoint = (3 * divisor) / 4;
	int32_t error = 0;
	int i;
	int j;

	// Fill the input data with random numbers if a seed was provided
	if (seed > 0)
	{
		int nominal = 0;
		int range = 127 * divisor;

		srand(seed);

		for (i = 0; i < length; i++) {
			int value = nominal + (rand() % range) - range/2;
#if 0
			input[i] = value;
#else
			value /= divisor;
			input[i] = value * divisor;
#endif
		}
	}

	// Quantize and pack the results
	QuantizeRow16sTo8s(input, output, length, divisor);

	// Expand the quantized results
	DequantizeBandRow(output, length, divisor, result);

	// Tabulate differences between the input and output
	for (i = 0; i < sizeof(input)/sizeof(input[0]); i++)
	{
#if 0
		int delta = result[i] - input[i];
		if (abs(delta) < divisor) {
			residual[i] = 0;
		}
		else {
			assert(0);
			residual[i] = delta;
			error += abs(delta);
		}
#else
		int delta = result[i] - input[i];

		// Add the midpoint correction
		if (input[i] > 0) delta -= midpoint;
		else if (input[i] < 0) delta += midpoint;

		// Store the residual
		residual[i] = delta;

		// Compute the sum of the absolute error
		error += abs(delta);
#endif
	}

	return error;
}

#endif

