/*! @file DemoasicFrames.cpp

*  @brief CFA Bayer tools
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

#define DEMOSAICFRAMESLIB_EXPORTS

#define __STDC_LIMIT_MACROS

#include <stdint.h>
#include <math.h>
#include <assert.h>
#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif

#ifdef _WIN32
#include <windows.h>
//#include <atlbase.h> // Required for VS2005 but not 2003
#elif __APPLE__
#include <sys/stat.h>		// for _mkdir()
#include <unistd.h>			// for usleep()
#include <CoreFoundation/CoreFoundation.h>		// for propertylist/preferences
#else
#include <sys/stat.h>		// for _mkdir()
#include <unistd.h>			// for usleep()
#include <xmmintrin.h>
#endif

#include "../Common/AVIExtendedHeader.h"
#define MAX_PATH	260

#include "codec.h"
//#include "swap.h"

#define SwapInt32(x)	((((x)&0xff000000)>>24)|(((x)&0xff0000)>>8)|(((x)&0xff00)<<8)|(((x)&0xff)<<24))

#if 0
#include "DemoasicFrames.h"
#else
#include "config.h"
#include "encoder.h"
#include "color.h"
#include "metadata.h"
#include "convert.h"
#include "draw.h"
#include "lutpath.h"

#include "DemoasicFrames.h"

#ifdef SPI_LOADER
#include "spi.h"
#include "keyframes.h"
#endif

typedef int DEBAYER_ORDERING;

#define BAYER_FORMAT_RED_GRN		0
#define BAYER_FORMAT_GRN_RED		1
#define BAYER_FORMAT_GRN_BLU		2
#define BAYER_FORMAT_BLU_GRN		3

#ifdef __cplusplus
extern "C" {
#endif

// Forward references
void NewControlPoint(DECODER *decoder, unsigned char *ptr, int len, int delta, int priority);
uint32_t gencrc(unsigned char *buf, int len);
void GetCurrentID(DECODER *decoder, unsigned char *ptr, unsigned int len, char *id, unsigned int id_size);

void UpdateCFHDDATA(DECODER *decoder, unsigned char *ptr, int len, int delta, int priority);

void DebayerLine(int width, int height, int linenum,
				unsigned short *bayer_source,
				DEBAYER_ORDERING order,
				unsigned short *RGB_output,
				int highquality,
				int sharpening);

void ColorDifference2Bayer(int width,
						   unsigned short *srcptr,
						   int bayer_pitch,
						   int bayer_format);

void BayerRippleFilter(	int width,
						   unsigned short *srcptr,
						   int bayer_pitch,
						   int bayer_formatm,
						   unsigned short *srcbase);

void FastVignetteInplaceWP13(DECODER *decoder, int displayWidth, int width, int height, int y, float r1, float r2, float gain,
							  int16_t *sptr, int resolution, int pixelsize);
void FastSharpeningBlurHinplaceWP13(int width, int16_t *sptr, float sharpness, int resolution, int pixelsize);

void FastSharpeningBlurVWP13( short *Aptr,
						 short *Bptr,
						 short *Cptr,
						 short *Dptr,
						 short *Eptr,
						 int pitch,
						 int edgenear,
						 short *output, 
						 int pixels, 
						 float sharpness,
						 int resolution,
						 int channel_blend_type);
void FastSharpeningBlurVW13A( short *Aptr,
						 short *Bptr,
						 short *Cptr,
						 short *Dptr,
						 short *Eptr,
						 int pitch,
						 int edgenear,
						 short *output, 
						 int pixels, 
						 float sharpness,
						 int resolution,
						 int channel_blend_type);

//float *LoadCube64_3DLUT(DECODER *decoder, CFHDDATA *cfhddata, int *lutsize);

#ifdef __cplusplus
}
#endif

#endif

#define MAKEID(a,b,c,d) ((a<<24)|(b<<16)|(c<<8)|(d))
#define MAKEID_SWAP(d,c,b,a) ((a<<24)|(b<<16)|(c<<8)|(d))


#define T_VALUE		15*256

#define SATURATE16(x) (((x) > 0) ? (((x) <= 65535) ? (x) : 65535) : 0)

#define DEBAYER5x5		1
#define CF_ENHANCE		1	//CineForm Enhancement Debayer

inline void REDCELL(unsigned short *rgbptr, unsigned short *bayerptr, int width)
{
	int r,g,b;
	{/* normal 5x5 */
#if CF_ENHANCE
		int diffR = abs((int)bayerptr[-2] - (int)bayerptr[2])>>10;
		int diffG = abs((int)bayerptr[-1] - (int)bayerptr[1])>>10;
		int diffB = abs((int)bayerptr[-1*width-1] - (int)bayerptr[1*width+1])>>10;
		int factorR = (2+(2*diffR*diffR/(2+diffG*diffG))); 
		int factorB = (4+(4*diffG*diffG/(2+diffB*diffB))); 

		r = bayerptr[0]; //r

		g = ( bayerptr[-2*width]*-1
					+ bayerptr[-1*width]*factorR
					+ bayerptr[-2]*-1 + bayerptr[-1]*factorR + bayerptr[0]*4 + bayerptr[1]*factorR + bayerptr[2]*-1
					+ bayerptr[+1*width]*factorR
					+ bayerptr[+2*width]*-1 ) /(4*factorR); //g
		
		b = ( bayerptr[-2*width]*-3
			+ bayerptr[-1*width-1]*factorB + bayerptr[-1*width+1]*factorB
			+ bayerptr[-2]*-3 + bayerptr[0]*12 + bayerptr[2]*-3
			+ bayerptr[+1*width-1]*factorB + bayerptr[+1*width+1]*factorB
			+ bayerptr[+2*width]*-3 ) / (4*factorB); //b
#else		
		r = bayerptr[0]; //r

		g = ( bayerptr[-2*width]*-1
					+ bayerptr[-1*width]*2
					+ bayerptr[-2]*-1 + bayerptr[-1]*2 + bayerptr[0]*4 + bayerptr[1]*2 + bayerptr[2]*-1
					+ bayerptr[+1*width]*2
					+ bayerptr[+2*width]*-1 ) >> 3; //g

		b = ( bayerptr[-2*width]*-3
			+ bayerptr[-1*width-1]*4 + bayerptr[-1*width+1]*4
			+ bayerptr[-2]*-3 + bayerptr[0]*12 + bayerptr[2]*-3
			+ bayerptr[+1*width-1]*4 + bayerptr[+1*width+1]*4
			+ bayerptr[+2*width]*-3 ) >> 4; //b
#endif
	}

	*rgbptr++ = (r);
	*rgbptr++ = SATURATE16(g);
	*rgbptr++ = SATURATE16(b);
}

inline void GRNREDCELL(unsigned short *rgbptr, unsigned short *bayerptr, int width)
{
	int r,g,b;
	{/* normal 5x5 */

#if CF_ENHANCE
		int diffR = abs((int)bayerptr[-1] - (int)bayerptr[1])>>10;
		int diffG = abs((int)bayerptr[-2] - (int)bayerptr[2])>>10;
		int diffB = abs((int)bayerptr[-1*width] - (int)bayerptr[1*width])>>10;
		int factorR = (8+(4*diffG*diffG/(2+diffR*diffR)));
		int factorB = (8+(4*diffG*diffG/(2+diffB*diffB)));
		r = ( bayerptr[-2*width]*1
					+ bayerptr[-1*width-1]*-2 + bayerptr[-1*width+1]*-2
					+ bayerptr[-2]*-2 + bayerptr[-1]*factorR + bayerptr[0]*10 + bayerptr[1]*factorR + bayerptr[2]*-2
					+ bayerptr[+1*width-1]*-2 + bayerptr[+1*width+1]*-2
					+ bayerptr[+2*width]*1 ) /(factorR*2); //r

		g =  bayerptr[0]; //g

		b =  ( bayerptr[-2*width]*-2
				+ bayerptr[-1*width-1]*-2 + bayerptr[-1*width]*factorB + bayerptr[-1*width+1]*-2
				+ bayerptr[-2]*1 + bayerptr[0]*10 + bayerptr[2]*1
				+ bayerptr[+1*width-1]*-2 + bayerptr[+1*width]*factorB + bayerptr[+1*width+1]*-2
				+ bayerptr[+2*width]*-2 ) / (factorB*2); //b
#else
		r = (( bayerptr[-2*width]*1
					+ bayerptr[-1*width-1]*-2 + bayerptr[-1*width+1]*-2
					+ bayerptr[-2]*-2 + bayerptr[-1]*8 + bayerptr[0]*10 + bayerptr[1]*8 + bayerptr[2]*-2
					+ bayerptr[+1*width-1]*-2 + bayerptr[+1*width+1]*-2
					+ bayerptr[+2*width]*1 ) >> 4); //r

		g =  bayerptr[0]; //g

		b =  ( bayerptr[-2*width]*-2
				+ bayerptr[-1*width-1]*-2 + bayerptr[-1*width]*8 + bayerptr[-1*width+1]*-2
				+ bayerptr[-2]*1 + bayerptr[0]*10 + bayerptr[2]*1
				+ bayerptr[+1*width-1]*-2 + bayerptr[+1*width]*8 + bayerptr[+1*width+1]*-2
				+ bayerptr[+2*width]*-2 ) >> 4; //b
#endif
	}
	*rgbptr++ = SATURATE16(r);
	*rgbptr++ = (g);
	*rgbptr++ = SATURATE16(b);
}



inline void GRNBLUCELL(unsigned short *rgbptr, unsigned short *bayerptr, int width)
{
	int r,g,b;
	{/* normal 5x5 */
		
#if CF_ENHANCE
		int diffR = abs((int)bayerptr[-1*width] - (int)bayerptr[1*width])>>10;
		int diffG = abs((int)bayerptr[-2*width] - (int)bayerptr[2*width])>>10;
		int diffB = abs((int)bayerptr[-1] - (int)bayerptr[1])>>10;
		int factorR = (8+(4*diffG*diffG/(2+diffR*diffR)));
		int factorB = (8+(4*diffG*diffG/(2+diffB*diffB)));
		r = ( bayerptr[-2*width]*-2
					+ bayerptr[-1*width-1]*-2 + bayerptr[-1*width]*factorR + bayerptr[-1*width+1]*-2
					+ bayerptr[-2]*1 + bayerptr[0]*10 + bayerptr[2]*1
					+ bayerptr[+1*width-1]*-2 + bayerptr[+1*width]*factorR + bayerptr[+1*width+1]*-2
					+ bayerptr[+2*width]*-2 ) / (factorR*2); //r

		g =  bayerptr[0]; //g

		b = ( bayerptr[-2*width]*1
				+ bayerptr[-1*width-1]*-2 + bayerptr[-1*width+1]*-2
				+ bayerptr[-2]*-2 + bayerptr[-1]*factorB + bayerptr[0]*10 + bayerptr[1]*factorB + bayerptr[2]*-2
				+ bayerptr[+1*width-1]*-2 + bayerptr[+1*width+1]*-2
				+ bayerptr[+2*width]*1 ) / (factorB*2); //b

#else
		r = (( bayerptr[-2*width]*-2
					+ bayerptr[-1*width-1]*-2 + bayerptr[-1*width]*8 + bayerptr[-1*width+1]*-2
					+ bayerptr[-2]*1 + bayerptr[0]*10 + bayerptr[2]*1
					+ bayerptr[+1*width-1]*-2 + bayerptr[+1*width]*8 + bayerptr[+1*width+1]*-2
					+ bayerptr[+2*width]*-2 ) >> 4);

		g =  bayerptr[0]; //g

		b =  ( bayerptr[-2*width]*1
				+ bayerptr[-1*width-1]*-2 + bayerptr[-1*width+1]*-2
				+ bayerptr[-2]*-2 + bayerptr[-1]*8 + bayerptr[0]*10 + bayerptr[1]*8 + bayerptr[2]*-2
				+ bayerptr[+1*width-1]*-2 + bayerptr[+1*width+1]*-2
				+ bayerptr[+2*width]*1 ) >> 4; //b
#endif
	}

	*rgbptr++ = SATURATE16(r);
	*rgbptr++ = (g);
	*rgbptr++ = SATURATE16(b);
}


inline void BLUCELL(unsigned short *rgbptr, unsigned short *bayerptr, int width)
{
	int r,g,b;
	{/* normal 5x5 */

#if CF_ENHANCE
		int diffR = abs((int)bayerptr[-1*width-1] - (int)bayerptr[+1*width+1])>>10;
		int diffG = abs((int)bayerptr[-1] - (int)bayerptr[1])>>10;
		int diffB = abs((int)bayerptr[-2] - (int)bayerptr[2])>>10;
		int factorR = (4+(4*diffG*diffG/(2+diffR*diffR)));
		int factorB = (2+(2*diffB*diffB/(2+diffG*diffG)));
		r = ( bayerptr[-2*width]*-3
					+ bayerptr[-1*width-1]*factorR + bayerptr[-1*width+1]*factorR
					+ bayerptr[-2]*-3 + bayerptr[0]*12 + bayerptr[2]*-3
					+ bayerptr[+1*width-1]*factorR + bayerptr[+1*width+1]*factorR
					+ bayerptr[+2*width]*-3 ) / (factorR*4);
		
		g =  ( bayerptr[-2*width]*-1
					+ bayerptr[-1*width]*factorB
					+ bayerptr[-2]*-1 + bayerptr[-1]*factorB + bayerptr[0]*4 + bayerptr[1]*factorB + bayerptr[2]*-1
					+ bayerptr[+1*width]*factorB
					+ bayerptr[+2*width]*-1 ) / (factorB*4); //g

		b =  bayerptr[0]; //b
#else
		r = (( bayerptr[-2*width]*-3
					+ bayerptr[-1*width-1]*4 + bayerptr[-1*width+1]*4
					+ bayerptr[-2]*-3 + bayerptr[0]*12 + bayerptr[2]*-3
					+ bayerptr[+1*width-1]*4 + bayerptr[+1*width+1]*4
					+ bayerptr[+2*width]*-3 ) >> 4);

		g =  ( bayerptr[-2*width]*-1
					+ bayerptr[-1*width]*2
					+ bayerptr[-2]*-1 + bayerptr[-1]*2 + bayerptr[0]*4 + bayerptr[1]*2 + bayerptr[2]*-1
					+ bayerptr[+1*width]*2
					+ bayerptr[+2*width]*-1 ) >> 3; //g

		b =  bayerptr[0]; //b
#endif
	
	}

	*rgbptr++ = SATURATE16(r);
	*rgbptr++ = SATURATE16(g);
	*rgbptr++ = (b);
}


void FastSharpeningBlurHinplace(int width, unsigned short *sptr, int sharpness)
{
	int i=0,shift=2,B,C;
	uint16_t *outptr = sptr;
	int rneg1,rneg2;
	int gneg1,gneg2;
	int bneg1,bneg2;
//	*outptr++ = *sptr++; //R
//	*outptr++ = *sptr++; //G
//	*outptr++ = *sptr++; //B
	rneg2 = *sptr++; //R
	gneg2 = *sptr++; //G
	bneg2 = *sptr++; //B

	// blur 1,2,1
//	*outptr++ = (sptr[-3] + sptr[0]*2 + sptr[3])>>2; sptr++; //R
//	*outptr++ = (sptr[-3] + sptr[0]*2 + sptr[3])>>2; sptr++; //G
//	*outptr++ = (sptr[-3] + sptr[0]*2 + sptr[3])>>2; sptr++; //B

	rneg1 = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //R
	gneg1 = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //G
	bneg1 = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //B

	switch(sharpness)
	{
	default:
	case 3: //highest sharpen
		shift = 2; 	B = 1; 	C = 4;
		break;
	case 2: //nice sharpen
		shift = 3;	B = 2;	C = 6;
		break;
	case 1: //small sharpen
		shift = 4;	B = 4;	C = 10;
		break;
	}

	for(i=2;i<width-2;i++)
	{
		*outptr++ = SATURATE16((-sptr[-6] + sptr[-3]*B + sptr[0]*C + sptr[3]*B -sptr[6])>>shift); sptr++; //R
		*outptr++ = SATURATE16((-sptr[-6] + sptr[-3]*B + sptr[0]*C + sptr[3]*B -sptr[6])>>shift); sptr++; //G
		*outptr++ = SATURATE16((-sptr[-6] + sptr[-3]*B + sptr[0]*C + sptr[3]*B -sptr[6])>>shift); sptr++; //B
	}

	// blur 1,2,1
	*outptr++ = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //R
	*outptr++ = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //G
	*outptr++ = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //B

	*outptr++ = *sptr++; //R
	*outptr++ = *sptr++; //G
	*outptr++ = *sptr++; //B

	outptr += 5;

	for(i=2;i<width;i++)
	{
		//TODO: The GCC compiler warns that the operation on outptr may be undefined
        //	*outptr-- = outptr[-6];
        //	*outptr-- = outptr[-6];
        //	*outptr-- = outptr[-6];
        outptr[0] = outptr[-6];
        outptr[-1] = outptr[-7];
        outptr[-2] = outptr[-8];
        
        outptr -= 3;
	}

	*outptr-- = bneg1;
	*outptr-- = gneg1;
	*outptr-- = rneg1;

	*outptr-- = bneg2;
	*outptr-- = gneg2;
	*outptr-- = rneg2;
}


void FastVignetteInplaceWP13(DECODER *decoder, int displayWidth, int width, int height, int y, float r1, float r2, float gain,
							  int16_t *sptr, int resolution, int pixelsize)
{
	//int i=0,shift=2,D=0;
	int16_t *outptr = sptr;
	int16_t *outpt2 = sptr + (displayWidth-1) * pixelsize / 2;
	int xinner,xouter;
	int x;
	float xstep = 2.0f/(float)(displayWidth);
	float xpos = -1.0f; // far left
	float ypos = 2.0f * (float)(height/2 - y)/(float)width;

	ypos *= ypos;
	r1 *= r1;
	r2 *= r2;



	xinner = (width/2) - (int)((float)(width/2) * r1);
	xouter = (width/2) - (int)((float)(width/2) * r2);

	
	if(pixelsize == 6)
	{
		for(x=0;x<displayWidth/2;x++)
		{
			float r = xpos*xpos + ypos;

			if(r2 < r)
			{
				if(gain == 0.0)
				{
					outptr[0] = 0; //R
					outptr[1] = 0; //G
					outptr[2] = 0; //B
					outpt2[0] = 0; //R
					outpt2[1] = 0; //G
					outpt2[2] = 0; //B
				}
				else
				{
					float Af = gain;
					if(outptr[0] >= 0)
						outptr[0] = (int)sqrtf((float)outptr[0]*(float)outptr[0]*Af); //R
					else
						outptr[0] = -(int)sqrtf((float)outptr[0]*(float)outptr[0]*Af); //R

					if(outptr[1] >= 0)
						outptr[1] = (int)sqrtf((float)outptr[1]*(float)outptr[1]*Af); //G
					else
						outptr[1] = -(int)sqrtf((float)outptr[1]*(float)outptr[1]*Af); //G

					if(outptr[2] >= 0)
						outptr[2] = (int)sqrtf((float)outptr[2]*(float)outptr[2]*Af); //B
					else
						outptr[2] = -(int)sqrtf((float)outptr[2]*(float)outptr[2]*Af); //B
					

					if(outpt2[0] >= 0)
						outpt2[0] = (int)sqrtf((float)outpt2[0]*(float)outpt2[0]*Af); //R
					else
						outpt2[0] = -(int)sqrtf((float)outpt2[0]*(float)outpt2[0]*Af); //R

					if(outpt2[1] >= 0)
						outpt2[1] = (int)sqrtf((float)outpt2[1]*(float)outpt2[1]*Af); //G
					else
						outpt2[1] = -(int)sqrtf((float)outpt2[1]*(float)outpt2[1]*Af); //G

					if(outpt2[2] >= 0)
						outpt2[2] = (int)sqrtf((float)outpt2[2]*(float)outpt2[2]*Af); //B
					else
						outpt2[2] = -(int)sqrtf((float)outpt2[2]*(float)outpt2[2]*Af); //B
				}
			}
			else if(r1 < r)
			{
				float Af =  (r2 - r) / (r2 - r1);

				Af -= 0.5f;
				Af *= 2.0f;
				Af /= (1.0f + fabsf(Af)); 
				Af += 0.5f;

				Af *= (1.0f-gain);
				Af += gain;
				
				if(outptr[0] >= 0)
					outptr[0] = (int)sqrtf((float)outptr[0]*(float)outptr[0]*Af); //R
				else
					outptr[0] = -(int)sqrtf((float)outptr[0]*(float)outptr[0]*Af); //R

				if(outptr[1] >= 0)
					outptr[1] = (int)sqrtf((float)outptr[1]*(float)outptr[1]*Af); //G
				else
					outptr[1] = -(int)sqrtf((float)outptr[1]*(float)outptr[1]*Af); //G

				if(outptr[2] >= 0)
					outptr[2] = (int)sqrtf((float)outptr[2]*(float)outptr[2]*Af); //B
				else
					outptr[2] = -(int)sqrtf((float)outptr[2]*(float)outptr[2]*Af); //B
				

				if(outpt2[0] >= 0)
					outpt2[0] = (int)sqrtf((float)outpt2[0]*(float)outpt2[0]*Af); //R
				else
					outpt2[0] = -(int)sqrtf((float)outpt2[0]*(float)outpt2[0]*Af); //R

				if(outpt2[1] >= 0)
					outpt2[1] = (int)sqrtf((float)outpt2[1]*(float)outpt2[1]*Af); //G
				else
					outpt2[1] = -(int)sqrtf((float)outpt2[1]*(float)outpt2[1]*Af); //G

				if(outpt2[2] >= 0)
					outpt2[2] = (int)sqrtf((float)outpt2[2]*(float)outpt2[2]*Af); //B
				else
					outpt2[2] = -(int)sqrtf((float)outpt2[2]*(float)outpt2[2]*Af); //B
				
			}
			else
			{
				break;
			}
			outptr+=3;
			outpt2-=3;
			xpos += xstep;
		}
	}
	else
	{
	}
}



void FastSharpeningBlurHinplaceWP13(int width, int16_t *sptr, float sharpness, int resolution, int pixelsize)
{
	int i=0,shift=2,A,B,C,taps=1;
	int16_t *outptr = sptr;
	int rneg1,rneg2;
	int gneg1,gneg2;
	int bneg1,bneg2;
	int aneg1,aneg2;
	int diff;
	int adiff;

	switch(resolution)
	{
	case DECODED_RESOLUTION_FULL:
	case DECODED_RESOLUTION_FULL_DEBAYER:
	case DECODED_RESOLUTION_HALF_VERTICAL:
		taps = 5;
		break;
	case DECODED_RESOLUTION_HALF:
	case DECODED_RESOLUTION_HALF_NODEBAYER:
	case DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER:
	case DECODED_RESOLUTION_HALF_HORIZONTAL:
		taps = 3;
		break;
	case DECODED_RESOLUTION_QUARTER:
	case DECODED_RESOLUTION_LOWPASS_ONLY:
	case DECODED_RESOLUTION_QUARTER_NODEBAYER_SCALED:
		taps = 1;
		break;
	}


	if(sharpness < 0.0)
	{
		diff = (int)(256.0f * (-sharpness * 4.0f - (float)((int)(-sharpness * 4.0f))));
		adiff = 256 - diff;

		if(pixelsize == 6)
		{
			if(taps == 5)
			{
				switch(-1 + (int)(sharpness * 4.0))
				{

				case -5://highest blur
					diff = 256;
				case -4: //blur
					sptr += 4*3;
					shift = 4+8;  //A = 2;  B = 4;	C = 4   0,4,8,4,0  
					
					A = 4096 / 9 * diff / 256; 
					B = (4096 - A*2) / 7;

					for(i=4;i<width-4;i++)
					{
						outptr[0] = (((sptr[-12] + sptr[12])*A + (sptr[-9] + sptr[-6] + sptr[-3] + sptr[0] + sptr[3] + sptr[6] +  sptr[9])*B)>>shift);  //R
						outptr[1] = (((sptr[-11] + sptr[13])*A + (sptr[-8] + sptr[-5] + sptr[-2] + sptr[1] + sptr[4] + sptr[7] + sptr[10])*B)>>shift);  //G
						outptr[2] = (((sptr[-10] + sptr[14])*A + (sptr[-7] + sptr[-4] + sptr[-1] + sptr[2] + sptr[5] + sptr[8] + sptr[11])*B)>>shift);  //B

						sptr += 3;
						outptr += 3;
					}
					
					for(i=4;i<width-8;i++)
					{
						outptr -= 3;
						outptr[0] = outptr[-12];
						outptr[1] = outptr[-11];
						outptr[2] = outptr[-10];
					}
					outptr -= 3;
					outptr[0] = outptr[-9];
					outptr[1] = outptr[-8];
					outptr[2] = outptr[-7];

					outptr -= 3;
					outptr[0] = outptr[-6];
					outptr[1] = outptr[-5];
					outptr[2] = outptr[-4];
					
					outptr -= 3;
					outptr[0] = outptr[-3];
					outptr[1] = outptr[-2];
					outptr[2] = outptr[-1];
					break;
				case -3: //blur
					sptr += 3*3;
					shift = 4+8;  //A = 2;  B = 4;	C = 4   0,4,8,4,0  
					
					A = 4096 / 7 * diff / 256; 
					B = (4096 - A*2) / 5;



					for(i=3;i<width-3;i++)
					{
						outptr[0] = (((sptr[-9]+ sptr[9])*A + (sptr[-6] + sptr[-3] + sptr[0] + sptr[3] + sptr[6])*B )>>shift);  //R
						outptr[1] = (((sptr[-8]+sptr[10])*A + (sptr[-5] + sptr[-2] + sptr[1] + sptr[4] + sptr[7])*B )>>shift);  //G
						outptr[2] = (((sptr[-7]+sptr[11])*A + (sptr[-4] + sptr[-1] + sptr[2] + sptr[5] + sptr[8])*B )>>shift);  //B

						sptr += 3;
						outptr += 3;
					}
					
					for(i=3;i<width-6;i++)
					{
						outptr -= 3;
						outptr[0] = outptr[-9];
						outptr[1] = outptr[-8];
						outptr[2] = outptr[-7];
					}
					outptr -= 3;
					outptr[0] = outptr[-6];
					outptr[1] = outptr[-5];
					outptr[2] = outptr[-4];
					
					outptr -= 3;
					outptr[0] = outptr[-3];
					outptr[1] = outptr[-2];
					outptr[2] = outptr[-1];
					break;
				case -2: //blur
					sptr += 2*3;
					shift = 4+8;  //A = 2;  B = 4;	C = 4   0,4,8,4,0  
					
					A = 0 * adiff + 2 * diff; 
					B = 4 * adiff + 4 * diff; 
					C = 8 * adiff + 4 * diff; 

					for(i=2;i<width-2;i++)
					{
						outptr[0] = ((sptr[-6]*A + sptr[-3]*B + sptr[0]*C + sptr[3]*B + sptr[6]*A)>>shift);  //R
						outptr[1] = ((sptr[-5]*A + sptr[-2]*B + sptr[1]*C + sptr[4]*B + sptr[7]*A)>>shift);  //G
						outptr[2] = ((sptr[-4]*A + sptr[-1]*B + sptr[2]*C + sptr[5]*B + sptr[8]*A)>>shift);  //B

						sptr += 3;
						outptr += 3;
					}
					
					for(i=2;i<width-4;i++)
					{
						outptr -= 3;
						outptr[0] = outptr[-6];
						outptr[1] = outptr[-5];
						outptr[2] = outptr[-4];
					}
					outptr -= 3;
					outptr[0] = outptr[-3];
					outptr[1] = outptr[-2];
					outptr[2] = outptr[-1];
					break;
				case -1: //small blur
					sptr += 2*3;
					shift = 4+8;  //A = 0;  B = 1;	C = 14   0,2,12,2,0  
					
					A = 0 * adiff + 0 * diff; 
					B = 0 * adiff + 4 * diff; 
					C = 16 * adiff + 8 * diff; 
					
					for(i=2;i<width-2;i++)
					{
						outptr[0] = ((sptr[-6]*A + sptr[-3]*B + sptr[0]*C + sptr[3]*B + sptr[6]*A)>>shift);  //R
						outptr[1] = ((sptr[-5]*A + sptr[-2]*B + sptr[1]*C + sptr[4]*B + sptr[7]*A)>>shift);  //G
						outptr[2] = ((sptr[-4]*A + sptr[-1]*B + sptr[2]*C + sptr[5]*B + sptr[8]*A)>>shift);  //B

						sptr += 3;
						outptr += 3;
					}					
					for(i=2;i<width-4;i++)
					{
						outptr -= 3;
						outptr[0] = outptr[-6];
						outptr[1] = outptr[-5];
						outptr[2] = outptr[-4];
					}
					outptr -= 3;
					outptr[0] = outptr[-3];
					outptr[1] = outptr[-2];
					outptr[2] = outptr[-1];
					break;

				}
			}
			else if(taps == 3)
			{						
				diff = (int)(256.0f * (-sharpness - (float)((int)(-sharpness*0.999f))));
				adiff = 256 - diff;

				{
					sptr += 2*3;
					shift = 4+8;  //A = 0;  B = 1;	C = 14   0,2,12,2,0  
					
					A = 0 * adiff + 2 * diff; 
					B = 0 * adiff + 4 * diff; 
					C = 16 * adiff + 4 * diff; 
					
					for(i=2;i<width-2;i++)
					{
						outptr[0] = ((sptr[-6]*A + sptr[-3]*B + sptr[0]*C + sptr[3]*B + sptr[6]*A)>>shift);  //R
						outptr[1] = ((sptr[-5]*A + sptr[-2]*B + sptr[1]*C + sptr[4]*B + sptr[7]*A)>>shift);  //G
						outptr[2] = ((sptr[-4]*A + sptr[-1]*B + sptr[2]*C + sptr[5]*B + sptr[8]*A)>>shift);  //B

						sptr += 3;
						outptr += 3;
					}					
					for(i=2;i<width-4;i++)
					{
						outptr -= 3;
						outptr[0] = outptr[-6];
						outptr[1] = outptr[-5];
						outptr[2] = outptr[-4];
					}
					outptr -= 3;
					outptr[0] = outptr[-3];
					outptr[1] = outptr[-2];
					outptr[2] = outptr[-1];
				}
			}
		}
		else
		{
			if(taps == 5)
			{
				switch((int)(sharpness * 5.0))
				{

				case -5://highest blur
					diff = 256;
				case -4: //blur
					sptr += 4*4;
					shift = 4+8;  //A = 2;  B = 4;	C = 4   0,4,8,4,0  
					
					A = 4096 / 9 * diff / 256; 
					B = (4096 - A*2) / 7;

					for(i=4;i<width-4;i++)
					{
						outptr[0] = (((sptr[-16] + sptr[16])*A + (sptr[-12] + sptr[-8] + sptr[-4] + sptr[0] + sptr[4] + sptr[8] + sptr[12])*B)>>shift);  //R
						outptr[1] = (((sptr[-15] + sptr[17])*A + (sptr[-11] + sptr[-7] + sptr[-3] + sptr[1] + sptr[5] + sptr[9] + sptr[13])*B)>>shift);  //G
						outptr[2] = (((sptr[-14] + sptr[18])*A + (sptr[-10] + sptr[-6] + sptr[-2] + sptr[2] + sptr[6] + sptr[10]+ sptr[14])*B)>>shift); 
						outptr[3] = sptr[3]; //A //B

						sptr += 4;
						outptr += 4;
					}
					
					for(i=4;i<width-8;i++)
					{
						outptr -= 4;
						outptr[0] = outptr[-16];
						outptr[1] = outptr[-15];
						outptr[2] = outptr[-14];
						outptr[3] = outptr[-13]; //A
					}
					outptr -= 4;
					outptr[0] = outptr[-12];
					outptr[1] = outptr[-11];
					outptr[2] = outptr[-10];
					outptr[3] = outptr[-9];

					outptr -= 4;
					outptr[0] = outptr[-8];
					outptr[1] = outptr[-7];
					outptr[2] = outptr[-6];
					outptr[3] = outptr[-5];
					
					outptr -= 4;
					outptr[0] = outptr[-4];
					outptr[1] = outptr[-3];
					outptr[2] = outptr[-2];
					outptr[3] = outptr[-1];
					break;
				case -3: //blur
					sptr += 3*4;
					shift = 4+8;  //A = 2;  B = 4;	C = 4   0,4,8,4,0  
					
					A = 4096 / 7 * diff / 256; 
					B = (4096 - A*2) / 5;



					for(i=3;i<width-3;i++)
					{	
						outptr[0] = (((sptr[-12] + sptr[12])*A + (sptr[-8] + sptr[-4] + sptr[0] + sptr[4] + sptr[8] )*B)>>shift);  //R
						outptr[1] = (((sptr[-11] + sptr[13])*A + (sptr[-7] + sptr[-3] + sptr[1] + sptr[5] + sptr[9] )*B)>>shift);  //G
						outptr[2] = (((sptr[-10] + sptr[14])*A + (sptr[-6] + sptr[-2] + sptr[2] + sptr[6] + sptr[10])*B)>>shift); 
						outptr[3] = sptr[3]; //A //B

						sptr += 4;
						outptr += 4;
					}
					
					for(i=3;i<width-6;i++)
					{
						outptr -= 4;
						outptr[0] = outptr[-12];
						outptr[1] = outptr[-11];
						outptr[2] = outptr[-10];
						outptr[3] = outptr[-9];
					}
					outptr -= 4;
					outptr[0] = outptr[-8];
					outptr[1] = outptr[-7];
					outptr[2] = outptr[-6];
					outptr[3] = outptr[-5];
					
					outptr -= 4;
					outptr[0] = outptr[-4];
					outptr[1] = outptr[-3];
					outptr[2] = outptr[-2];
					outptr[3] = outptr[-1];
					break;
				case -2: //blur
					sptr += 2*4;
					shift = 4+8;  //A = 2;  B = 4;	C = 4   0,4,8,4,0  
					
					A = 0 * adiff + 2 * diff; 
					B = 4 * adiff + 4 * diff; 
					C = 8 * adiff + 4 * diff; 

					for(i=2;i<width-2;i++)
					{
						outptr[0] = ((sptr[-8]*A + sptr[-4]*B + sptr[0]*C + sptr[4]*B + sptr[8]*A)>>shift);  //R
						outptr[1] = ((sptr[-7]*A + sptr[-3]*B + sptr[1]*C + sptr[5]*B + sptr[9]*A)>>shift);  //G
						outptr[2] = ((sptr[-6]*A + sptr[-2]*B + sptr[2]*C + sptr[6]*B + sptr[10]*A)>>shift);  //B
						outptr[3] = sptr[3];

						sptr += 4;
						outptr += 4;
					}
					
					for(i=2;i<width-4;i++)
					{
						outptr -= 4;
						outptr[0] = outptr[-8];
						outptr[1] = outptr[-7];
						outptr[2] = outptr[-6];
						outptr[3] = outptr[-5];
					}
					
					outptr -= 4;
					outptr[0] = outptr[-4];
					outptr[1] = outptr[-3];
					outptr[2] = outptr[-2];
					outptr[3] = outptr[-1];
					break;
				case -1: //small blur
					sptr += 2*4;
					shift = 4+8;  //A = 0;  B = 1;	C = 14   0,2,12,2,0  
					
					A = 0 * adiff + 0 * diff; 
					B = 0 * adiff + 4 * diff; 
					C = 16 * adiff + 8 * diff; 
					
					for(i=2;i<width-2;i++)
					{
						outptr[0] = ((sptr[-8]*A + sptr[-4]*B + sptr[0]*C + sptr[4]*B + sptr[8]*A)>>shift);  //R
						outptr[1] = ((sptr[-7]*A + sptr[-3]*B + sptr[1]*C + sptr[5]*B + sptr[9]*A)>>shift);  //G
						outptr[2] = ((sptr[-6]*A + sptr[-2]*B + sptr[2]*C + sptr[6]*B + sptr[10]*A)>>shift);  //B
						outptr[3] = sptr[3];

						sptr += 4;
						outptr += 4;
					}					
					for(i=2;i<width-4;i++)
					{
						outptr -= 4;
						outptr[0] = outptr[-8];
						outptr[1] = outptr[-7];
						outptr[2] = outptr[-6];
						outptr[3] = outptr[-5];
					}
					
					outptr -= 4;
					outptr[0] = outptr[-4];
					outptr[1] = outptr[-3];
					outptr[2] = outptr[-2];
					outptr[3] = outptr[-1];
					break;

				}
			}
			else if(taps == 3)
			{						
				diff = (int)(256.0f * (-sharpness - (float)((int)(-sharpness*0.999f))));
				adiff = 256 - diff;

				{
					sptr += 2*4;
					shift = 4+8;  //A = 0;  B = 1;	C = 14   0,2,12,2,0  
					
					A = 0 * adiff + 2 * diff; 
					B = 0 * adiff + 4 * diff; 
					C = 16 * adiff + 4 * diff; 
					
					for(i=2;i<width-2;i++)
					{
						outptr[0] = ((sptr[-8]*A + sptr[-4]*B + sptr[0]*C + sptr[4]*B + sptr[8]*A)>>shift);  //R
						outptr[1] = ((sptr[-7]*A + sptr[-3]*B + sptr[1]*C + sptr[5]*B + sptr[9]*A)>>shift);  //G
						outptr[2] = ((sptr[-6]*A + sptr[-2]*B + sptr[2]*C + sptr[6]*B + sptr[10]*A)>>shift);  //B
						outptr[3] = sptr[3];

						sptr += 4;
						outptr += 4;
					}							
					for(i=2;i<width-4;i++)
					{
						outptr -= 4;
						outptr[0] = outptr[-8];
						outptr[1] = outptr[-7];
						outptr[2] = outptr[-6];
						outptr[3] = outptr[-5];
					}
					
					outptr -= 4;
					outptr[0] = outptr[-4];
					outptr[1] = outptr[-3];
					outptr[2] = outptr[-2];
					outptr[3] = outptr[-1];
				}
			}
		}
	}
	else
	{
		diff = (int)(256.0f * (sharpness * 5.0f - (float)((int)(sharpness * 5.0f))));
		adiff = 256 - diff;

		if(pixelsize == 6)
		{
			if(taps == 5)
			{
				rneg2 = *sptr++; //R
				gneg2 = *sptr++; //G
				bneg2 = *sptr++; //B

				// blur 1,2,1
				rneg1 = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //R
				gneg1 = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //G
				bneg1 = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //B

				switch((int)(sharpness * 5.0))
				{

				case -5://highest blur
					shift = 4+8;  //A = 2;  B = 4;	C = 4   2,3,2,3,2  
					
					A = 2 * adiff + 2 * diff; 
					B = 4 * adiff + 4 * diff; 
					C = 4 * adiff + 4 * diff; 
					break;
				case -4: //blur
					shift = 4+8;  //A = 2;  B = 4;	C = 4   2,4,4,4,2  
					
					A = 2 * adiff + 2 * diff; 
					B = 4 * adiff + 4 * diff; 
					C = 4 * adiff + 4 * diff; 
					break;
				case -3: //blur
					shift = 4+8;  //A = 2;  B = 4;	C = 4   2,4,4,4,2  
					
					A = 0 * adiff + 2 * diff; 
					B = 4 * adiff + 4 * diff; 
					C = 8 * adiff + 4 * diff; 
					break;
				case -2: //blur
					shift = 4+8;  //A = 2;  B = 4;	C = 4   0,4,8,4,0  
					
					A = 0 * adiff + 2 * diff; 
					B = 4 * adiff + 4 * diff; 
					C = 8 * adiff + 4 * diff; 
					break;
				case -1: //small blur
					shift = 4+8;  //A = 0;  B = 1;	C = 14   0,2,12,2,0  
					
					A = 0 * adiff + 0 * diff; 
					B = 0 * adiff + 4 * diff; 
					C = 16 * adiff + 8 * diff; 
					break;


				case 0:
					shift = 4+8;  //A = 0;   B = 0;	C = 16<<8;
					
					A = 0 * adiff - 1 * diff; 
					B = 0 * adiff + 4 * diff; 
					C = 16 * adiff + 10 * diff; 
					break;
				case 1: //small sharpen
					shift = 4+8;	//A = -1<<8;   B = 4<<8;	C = 10<<8;
					A = -1 * adiff - 2 * diff; 
					B =  4 * adiff + 4 * diff; 
					C = 10 * adiff + 12 * diff; 
					break;
				case 2: //nice sharpen
					shift = 4+8;	//A = -2<<8;   B = 4<<8;	C = 12<<8;
					A = -2 * adiff - 4 * diff; 
					B =  4 * adiff + 4 * diff; 
					C = 12 * adiff + 16 * diff; 
					break;
				case 3: //highest sharpen
					shift = 4+8; 	//A = -4<<8;   B = 4<<8; 	C = 16<<8;
					A = -4 * adiff - 8 * diff; 
					B =  4 * adiff + 8 * diff; 
					C = 16 * adiff + 16 * diff; 
					break;
				case 4: //highest sharpen
					shift = 4+8; 	//A = -8<<8;   B = 8<<8;  C = 16<<8;
					A = -8 * adiff - 8 * diff; 
					B =  8 * adiff + 0 * diff; 
					C = 16 * adiff + 32 * diff; 
					break;
				case 5: //highest sharpen
					shift = 4; 	//A = -8;   B = 0;  C = 32;
					A = -8; 
					B = 0; 
					C = 32;              
					break;
				}
				for(i=2;i<width-2;i++)
				{
					if(sptr[6] < 0) sptr[6] = 0;
					if(sptr[7] < 0) sptr[7] = 0;
					if(sptr[8] < 0) sptr[8] = 0;
					outptr[0] = ((sptr[-6]*A + sptr[-3]*B + sptr[0]*C + sptr[3]*B + sptr[6]*A)>>shift);  //R
					outptr[1] = ((sptr[-5]*A + sptr[-2]*B + sptr[1]*C + sptr[4]*B + sptr[7]*A)>>shift);  //G
					outptr[2] = ((sptr[-4]*A + sptr[-1]*B + sptr[2]*C + sptr[5]*B + sptr[8]*A)>>shift);  //B

					sptr += 3;
					outptr += 3;
				}

				// blur 1,2,1
				*outptr++ = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //R
				*outptr++ = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //G
				*outptr++ = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //B

				*outptr++ = *sptr++; //R
				*outptr++ = *sptr++; //G
				*outptr++ = *sptr++; //B

				outptr += 5;

				for(i=2;i<width;i++)
				{
					//TODO: The GCC compiler warns that the operation on outptr may be undefined
					//*outptr-- = outptr[-6];
					//*outptr-- = outptr[-6];
					//*outptr-- = outptr[-6];
                    outptr[0] = outptr[-6];
                    outptr[-1] = outptr[-7];
                    outptr[-2] = outptr[-8];
                    outptr -= 3;
				}

				*outptr-- = bneg1;
				*outptr-- = gneg1;
				*outptr-- = rneg1;

				*outptr-- = bneg2;
				*outptr-- = gneg2;
				*outptr-- = rneg2;
			}
			else if(taps == 3)
			{
				switch((int)(sharpness * 5.0))
				{
				case 0:
					shift = 4+8;  //A = 0;   B = 0;	C = 16<<8;
					
					A = 0 * adiff - 1 * diff; 
					C = 16 * adiff + 18 * diff; 
					break;
				case 1: //small sharpen
					shift = 4+8;	//A = -1<<8;   B = 4<<8;	C = 10<<8;
					A = -1 * adiff - 2 * diff; 
					C = 18 * adiff + 20 * diff; 
					break;
				case 2: //nice sharpen
					shift = 4+8;	//A = -2<<8;   B = 4<<8;	C = 12<<8;
					A = -2 * adiff - 4 * diff; 
					C = 20 * adiff + 24 * diff; 
					break;
				case 3: //highest sharpen
					shift = 4+8; 	//A = -4<<8;   B = 4<<8; 	C = 16<<8;
					A = -4 * adiff - 8 * diff; 
					C = 24 * adiff + 32 * diff; 
					break;
				case 4: //highest sharpen
					shift = 4+8; 	//A = -8<<8;   B = 8<<8;  C = 16<<8;
					A = -8 * adiff - 8 * diff; 
					C = 32 * adiff + 32 * diff; 
					break;
				case 5: //highest sharpen
					shift = 4; 	//A = -8;   B = 0;  C = 32;
					A = -8; 
					C = 32;              
					break;
				}

				// copy
				*outptr++ = sptr[0]; sptr++; //R
				*outptr++ = sptr[0]; sptr++; //G
				*outptr++ = sptr[0]; sptr++; //B
				for(i=1;i<width-1;i++)
				{				
					if(sptr[3] < 0) sptr[3] = 0;
					if(sptr[4] < 0) sptr[4] = 0;
					if(sptr[5] < 0) sptr[5] = 0;
					outptr[0] = ((sptr[-3]*A + sptr[0]*C + sptr[3]*A)>>shift);  //R
					outptr[1] = ((sptr[-2]*A + sptr[1]*C + sptr[4]*A)>>shift);  //G
					outptr[2] = ((sptr[-1]*A + sptr[2]*C + sptr[5]*A)>>shift);  //B

					sptr += 3;
					outptr += 3;
				}

				// copy
				*outptr++ = sptr[0]; sptr++; //R
				*outptr++ = sptr[0]; sptr++; //G
				*outptr++ = sptr[0]; sptr++; //B
			}
		}
		else
		{
			if(taps == 5)
			{
				rneg2 = *sptr++; //R
				gneg2 = *sptr++; //G
				bneg2 = *sptr++; //B
				aneg2 = *sptr++; //A

				// blur 1,2,1
				rneg1 = ((sptr[-4] + sptr[0]*2 + sptr[4])>>2); sptr++; //R
				gneg1 = ((sptr[-4] + sptr[0]*2 + sptr[4])>>2); sptr++; //G
				bneg1 = ((sptr[-4] + sptr[0]*2 + sptr[4])>>2); sptr++; //B
				aneg1 = *sptr++; //A

				switch((int)(sharpness * 5.0))
				{
				case 0:
					shift = 4+8;  //A = 0;   B = 0;	C = 16<<8;
					
					A = 0 * adiff - 1 * diff; 
					B = 0 * adiff + 4 * diff; 
					C = 16 * adiff + 10 * diff; 
					break;
				case 1: //small sharpen
					shift = 4+8;	//A = -1<<8;   B = 4<<8;	C = 10<<8;
					A = -1 * adiff - 2 * diff; 
					B =  4 * adiff + 4 * diff; 
					C = 10 * adiff + 12 * diff; 
					break;
				case 2: //nice sharpen
					shift = 4+8;	//A = -2<<8;   B = 4<<8;	C = 12<<8;
					A = -2 * adiff - 4 * diff; 
					B =  4 * adiff + 4 * diff; 
					C = 12 * adiff + 16 * diff; 
					break;
				case 3: //highest sharpen
					shift = 4+8; 	//A = -4<<8;   B = 4<<8; 	C = 16<<8;
					A = -4 * adiff - 8 * diff; 
					B =  4 * adiff + 8 * diff; 
					C = 16 * adiff + 16 * diff; 
					break;
				case 4: //highest sharpen
					shift = 4+8; 	//A = -8<<8;   B = 8<<8;  C = 16<<8;
					A = -8 * adiff - 8 * diff; 
					B =  8 * adiff + 0 * diff; 
					C = 16 * adiff + 32 * diff; 
					break;
				case 5: //highest sharpen
					shift = 4; 	//A = -8;   B = 0;  C = 32;
					A = -8; 
					B = 0; 
					C = 32;              
					break;
				}
				for(i=2;i<width-2;i++)
				{				
					if(sptr[8] < 0) sptr[8] = 0;
					if(sptr[9] < 0) sptr[9] = 0;
					if(sptr[10] < 0) sptr[10] = 0;

					outptr[0] = ((sptr[-8]*A + sptr[-4]*B + sptr[0]*C + sptr[4]*B + sptr[8]*A)>>shift);  //R
					outptr[1] = ((sptr[-7]*A + sptr[-3]*B + sptr[1]*C + sptr[5]*B + sptr[9]*A)>>shift);  //G
					outptr[2] = ((sptr[-6]*A + sptr[-2]*B + sptr[2]*C + sptr[6]*B + sptr[10]*A)>>shift); //B
					outptr[3] = sptr[3]; //A
					
					sptr += 4;
					outptr += 4;
				}

				// blur 1,2,1
				*outptr++ = ((sptr[-4] + sptr[0]*2 + sptr[4])>>2); sptr++; //R
				*outptr++ = ((sptr[-4] + sptr[0]*2 + sptr[4])>>2); sptr++; //G
				*outptr++ = ((sptr[-4] + sptr[0]*2 + sptr[4])>>2); sptr++; //B
				*outptr++ = *sptr++; //A

				*outptr++ = *sptr++; //R
				*outptr++ = *sptr++; //G
				*outptr++ = *sptr++; //B
				*outptr++ = *sptr++; //A

				outptr += 7;

				for(i=2;i<width;i++)
				{
					//TODO: The GCC compiler warns that the operation on outptr may be undefined
					//*outptr-- = outptr[-8];
					//*outptr-- = outptr[-8];
					//*outptr-- = outptr[-8];
					//*outptr-- = outptr[-8];
                    outptr[0] = outptr[-8];
                    outptr[-1] = outptr[-9];
                    outptr[-2] = outptr[-10];
                    outptr[-3] = outptr[-11];
                    outptr -= 4;

				}

				*outptr-- = aneg1;
				*outptr-- = bneg1;
				*outptr-- = gneg1;
				*outptr-- = rneg1;

				*outptr-- = aneg2;
				*outptr-- = bneg2;
				*outptr-- = gneg2;
				*outptr-- = rneg2;
			}
			else if(taps == 3)
			{
				switch((int)(sharpness * 5.0))
				{
				case 0:
					shift = 4+8;  //A = 0;   B = 0;	C = 16<<8;
					
					A = 0 * adiff - 1 * diff; 
					C = 16 * adiff + 18 * diff; 
					break;
				case 1: //small sharpen
					shift = 4+8;	//A = -1<<8;   B = 4<<8;	C = 10<<8;
					A = -1 * adiff - 2 * diff; 
					C = 18 * adiff + 20 * diff; 
					break;
				case 2: //nice sharpen
					shift = 4+8;	//A = -2<<8;   B = 4<<8;	C = 12<<8;
					A = -2 * adiff - 4 * diff; 
					C = 20 * adiff + 24 * diff; 
					break;
				case 3: //highest sharpen
					shift = 4+8; 	//A = -4<<8;   B = 4<<8; 	C = 16<<8;
					A = -4 * adiff - 8 * diff; 
					C = 24 * adiff + 32 * diff; 
					break;
				case 4: //highest sharpen
					shift = 4+8; 	//A = -8<<8;   B = 8<<8;  C = 16<<8;
					A = -8 * adiff - 8 * diff; 
					C = 32 * adiff + 32 * diff; 
					break;
				case 5: //highest sharpen
					shift = 4; 	//A = -8;   B = 0;  C = 32;
					A = -8; 
					C = 32;              
					break;
				}

				// copy
				outptr[0] = sptr[0]; //R
				outptr[1] = sptr[1]; //G
				outptr[2] = sptr[2]; //B
				outptr[3] = sptr[3]; //A
				outptr += 4;
				sptr += 4;
				for(i=1;i<width-1;i++)
				{ 
					if(sptr[4] < 0) sptr[4] = 0;
					if(sptr[5] < 0) sptr[5] = 0;
					if(sptr[6] < 0) sptr[6] = 0;

					outptr[0] = ((sptr[-4]*A + sptr[0]*C + sptr[4]*A)>>shift); //R
					outptr[1] = ((sptr[-3]*A + sptr[1]*C + sptr[5]*A)>>shift); //G
					outptr[2] = ((sptr[-2]*A + sptr[2]*C + sptr[6]*A)>>shift); //B
					outptr[3] = sptr[3]; //A

					outptr += 4;
					sptr += 4;
				}

				// copy
				outptr[0] = sptr[0]; //R
				outptr[1] = sptr[1]; //G
				outptr[2] = sptr[2]; //B
				outptr[3] = sptr[3]; //A
				outptr += 4;
				sptr += 4;
			}
		}
	}
}





void FastSharpeningBlurVWP13(short *Aptr,
						 short *Bptr,
						 short *Cptr,
						 short *Dptr,
						 short *Eptr,
						 int pitch,
						 int edgenear,
						 short *output, 
						 int pixels, 
						 float sharpness,
						 int resolution,
						 int channel_blend_type)
{
	int i=0,taps=1;
	__m128i zero_epi16;
	__m128 a,b,c;

	int FIRsize = 5;
	float af,bf,cf;

	float diff = sharpness * 5.0f - (float)((int)(sharpness * 5.0f));
	float adiff = 1.0f - diff;


	
	switch(resolution)
	{
	case DECODED_RESOLUTION_FULL:
	case DECODED_RESOLUTION_FULL_DEBAYER:
	case DECODED_RESOLUTION_HALF_HORIZONTAL:
		taps = 5;

		if(	channel_blend_type == BLEND_STACKED_ANAMORPHIC ||
			channel_blend_type == BLEND_LINE_INTERLEAVED ||
			channel_blend_type == BLEND_FREEVIEW)
		{
			taps = 3;
		}
		break;
	case DECODED_RESOLUTION_HALF:
	case DECODED_RESOLUTION_HALF_NODEBAYER:
	case DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER:
	case DECODED_RESOLUTION_HALF_VERTICAL:
		taps = 3;
		
		if(	channel_blend_type == BLEND_STACKED_ANAMORPHIC ||
			channel_blend_type == BLEND_LINE_INTERLEAVED ||
			channel_blend_type == BLEND_FREEVIEW)
		{
			taps = 1;
		}
		break;
	case DECODED_RESOLUTION_QUARTER:
	case DECODED_RESOLUTION_LOWPASS_ONLY:
	case DECODED_RESOLUTION_QUARTER_NODEBAYER_SCALED:
		taps = 1;
		break;
	}


	if(sharpness < 0.0)
	{
		if(taps == 5)
		{
			diff = -sharpness * 4.0f - (float)((int)(-sharpness * 4.0f));
			adiff = 1.0f - diff;

			switch(-1 + (int)(sharpness * 4.0f))
			{
				case -5://highest blur
					diff = 1.0f;
				case -4: //blur		
					FIRsize = 9;
					af = 1.0f / 9.0f * diff; 
					bf = (1.0f - af*2.0f) / 7.0f;
					if(edgenear)
					{
						FIRsize = 5;
						af = 0.2f; 
						bf = 0.2f; 
						cf = 0.2f;   
					}
					break;
					
				case -3: //blur	
					FIRsize = 7;
					af = 1.0f / 7.0f * diff; 
					bf = (1.0f - af*2.0f) / 5.0f;
					if(edgenear)
					{
						FIRsize = 5;
						af = 0.2f; 
						bf = 0.2f; 
						cf = 0.2f;   
					}
					break;
				case -2: //blur	
					FIRsize = 5;
					af = 0.00f * adiff + 0.125f * diff; 
					bf = 0.25f * adiff + 0.25f * diff; 
					cf = 0.50f * adiff + 0.25f * diff;   
					break;
				default:
				case -1: //blur	
					FIRsize = 5;
					af = 0.00f * adiff + 0.00f * diff; 
					bf = 0.00f * adiff + 0.25f * diff; 
					cf = 1.00f * adiff + 0.50f * diff;   
					break;
			}
		}
		else if(taps == 3)
		{
			diff = -sharpness;
			adiff = 1.0f - diff;
		
			FIRsize = 5;
			af = 0.00f * adiff + 0.125f * diff; 
			bf = 0.00f * adiff + 0.25f * diff; 
			cf = 1.00f * adiff + 0.25f * diff;   
			
		}
		else
		{
			memcpy(output, Cptr, pixels*3*2);
			FIRsize = 1;
		}

		switch(FIRsize)
		{
		case 9:
			{
				int pixels8 = (pixels*3) & 0xfff8;
				a = _mm_set1_ps(af);
				b = _mm_set1_ps(bf);

				short *A2ptr = Aptr - (pitch>>1)*2;
				short *A1ptr = Aptr - (pitch>>1);
				short *E1ptr = Eptr + (pitch>>1);
				short *E2ptr = Eptr + (pitch>>1)*2;

				zero_epi16 = _mm_set1_epi16(0);

				for(i=0; i<pixels8; i+=8)
				{
					__m128i mix_epi16;
					__m128i A2_epi16 = _mm_load_si128((__m128i *)A2ptr);
					__m128i A1_epi16 = _mm_load_si128((__m128i *)A1ptr);
					__m128i A_epi16 = _mm_load_si128((__m128i *)Aptr);
					__m128i B_epi16 = _mm_load_si128((__m128i *)Bptr);
					__m128i C_epi16 = _mm_load_si128((__m128i *)Cptr);
					__m128i D_epi16 = _mm_load_si128((__m128i *)Dptr);
					__m128i E_epi16 = _mm_load_si128((__m128i *)Eptr);
					__m128i E1_epi16 = _mm_load_si128((__m128i *)E1ptr);
					__m128i E2_epi16 = _mm_load_si128((__m128i *)E2ptr);

					A2ptr+=8;
					A1ptr+=8;
					Aptr+=8;
					Bptr+=8;
					Cptr+=8;
					Dptr+=8;
					Eptr+=8;
					E1ptr+=8;
					E2ptr+=8;

					__m128i A2_epi32a =  _mm_unpackhi_epi16(zero_epi16, A2_epi16);
					__m128i A1_epi32a =  _mm_unpackhi_epi16(zero_epi16, A1_epi16);
					__m128i A_epi32a =  _mm_unpackhi_epi16(zero_epi16, A_epi16);
					__m128i B_epi32a =  _mm_unpackhi_epi16(zero_epi16, B_epi16);
					__m128i C_epi32a =  _mm_unpackhi_epi16(zero_epi16, C_epi16);
					__m128i D_epi32a =  _mm_unpackhi_epi16(zero_epi16, D_epi16);
					__m128i E_epi32a =  _mm_unpackhi_epi16(zero_epi16, E_epi16);
					__m128i E1_epi32a =  _mm_unpackhi_epi16(zero_epi16, E1_epi16);
					__m128i E2_epi32a =  _mm_unpackhi_epi16(zero_epi16, E2_epi16);
																	 
					__m128i A2_epi32b =  _mm_unpacklo_epi16(zero_epi16, A2_epi16);
					__m128i A1_epi32b =  _mm_unpacklo_epi16(zero_epi16, A1_epi16);
					__m128i A_epi32b =  _mm_unpacklo_epi16(zero_epi16, A_epi16);
					__m128i B_epi32b =  _mm_unpacklo_epi16(zero_epi16, B_epi16);
					__m128i C_epi32b =  _mm_unpacklo_epi16(zero_epi16, C_epi16);
					__m128i D_epi32b =  _mm_unpacklo_epi16(zero_epi16, D_epi16);
					__m128i E_epi32b =  _mm_unpacklo_epi16(zero_epi16, E_epi16);
					__m128i E1_epi32b =  _mm_unpacklo_epi16(zero_epi16, E1_epi16);
					__m128i E2_epi32b =  _mm_unpacklo_epi16(zero_epi16, E2_epi16);

					__m128 A2aps = _mm_cvtepi32_ps(A2_epi32a);
					__m128 A1aps = _mm_cvtepi32_ps(A1_epi32a);
					__m128 Aaps = _mm_cvtepi32_ps(A_epi32a);
					__m128 Baps = _mm_cvtepi32_ps(B_epi32a);
					__m128 Caps = _mm_cvtepi32_ps(C_epi32a);
					__m128 Daps = _mm_cvtepi32_ps(D_epi32a);
					__m128 Eaps = _mm_cvtepi32_ps(E_epi32a);
					__m128 E1aps = _mm_cvtepi32_ps(E1_epi32a);
					__m128 E2aps = _mm_cvtepi32_ps(E2_epi32a);

					__m128 A2bps = _mm_cvtepi32_ps(A2_epi32b);
					__m128 A1bps = _mm_cvtepi32_ps(A1_epi32b);
					__m128 Abps = _mm_cvtepi32_ps(A_epi32b);
					__m128 Bbps = _mm_cvtepi32_ps(B_epi32b);
					__m128 Cbps = _mm_cvtepi32_ps(C_epi32b);
					__m128 Dbps = _mm_cvtepi32_ps(D_epi32b);
					__m128 Ebps = _mm_cvtepi32_ps(E_epi32b);
					__m128 E1bps = _mm_cvtepi32_ps(E1_epi32b);
					__m128 E2bps = _mm_cvtepi32_ps(E2_epi32b);



					A2aps = _mm_mul_ps(A2aps, a);
					A1aps = _mm_mul_ps(A1aps, b);
					Aaps = _mm_mul_ps(Aaps, b);
					Baps = _mm_mul_ps(Baps, b);
					Caps = _mm_mul_ps(Caps, b);
					Daps = _mm_mul_ps(Daps, b);
					Eaps = _mm_mul_ps(Eaps, b);
					E1aps = _mm_mul_ps(E1aps, b);
					E2aps = _mm_mul_ps(E2aps, a);

					A2bps = _mm_mul_ps(A2bps, a);
					A1bps = _mm_mul_ps(A1bps, b);
					Abps = _mm_mul_ps(Abps, b);
					Bbps = _mm_mul_ps(Bbps, b);
					Cbps = _mm_mul_ps(Cbps, b);
					Dbps = _mm_mul_ps(Dbps, b);
					Ebps = _mm_mul_ps(Ebps, b);
					E1bps = _mm_mul_ps(E1bps, b);
					E2bps = _mm_mul_ps(E2bps, a);

					Aaps = _mm_add_ps(Aaps, A2aps);
					Aaps = _mm_add_ps(Aaps, A1aps);
					Aaps = _mm_add_ps(Aaps, Baps);
					Aaps = _mm_add_ps(Aaps, Caps);
					Aaps = _mm_add_ps(Aaps, Daps);
					Aaps = _mm_add_ps(Aaps, Eaps);
					Aaps = _mm_add_ps(Aaps, E1aps);
					Aaps = _mm_add_ps(Aaps, E2aps);

					Abps = _mm_add_ps(Abps, A2bps);
					Abps = _mm_add_ps(Abps, A1bps);
					Abps = _mm_add_ps(Abps, Bbps);
					Abps = _mm_add_ps(Abps, Cbps);
					Abps = _mm_add_ps(Abps, Dbps);
					Abps = _mm_add_ps(Abps, Ebps);
					Abps = _mm_add_ps(Abps, E1bps);
					Abps = _mm_add_ps(Abps, E2bps);


					C_epi32a = _mm_cvtps_epi32(Aaps);
					C_epi32b = _mm_cvtps_epi32(Abps);


					C_epi32a = _mm_srai_epi32(C_epi32a,16);
					C_epi32b = _mm_srai_epi32(C_epi32b,16);

					mix_epi16 = _mm_packs_epi32(C_epi32b,C_epi32a);

					_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
				}
			}
			break;

		case 7:
			{
				int pixels8 = (pixels*3) & 0xfff8;
				a = _mm_set1_ps(af);
				b = _mm_set1_ps(bf);

				short *A1ptr = Aptr - (pitch>>1);
				short *E1ptr = Eptr + (pitch>>1);

				zero_epi16 = _mm_set1_epi16(0);

				for(i=0; i<pixels8; i+=8)
				{
					__m128i mix_epi16;
					__m128i A1_epi16 = _mm_load_si128((__m128i *)A1ptr);
					__m128i A_epi16 = _mm_load_si128((__m128i *)Aptr);
					__m128i B_epi16 = _mm_load_si128((__m128i *)Bptr);
					__m128i C_epi16 = _mm_load_si128((__m128i *)Cptr);
					__m128i D_epi16 = _mm_load_si128((__m128i *)Dptr);
					__m128i E_epi16 = _mm_load_si128((__m128i *)Eptr);
					__m128i E1_epi16 = _mm_load_si128((__m128i *)E1ptr);

					A1ptr+=8;
					Aptr+=8;
					Bptr+=8;
					Cptr+=8;
					Dptr+=8;
					Eptr+=8;
					E1ptr+=8;

					__m128i A1_epi32a =  _mm_unpackhi_epi16(zero_epi16, A1_epi16);
					__m128i A_epi32a =  _mm_unpackhi_epi16(zero_epi16, A_epi16);
					__m128i B_epi32a =  _mm_unpackhi_epi16(zero_epi16, B_epi16);
					__m128i C_epi32a =  _mm_unpackhi_epi16(zero_epi16, C_epi16);
					__m128i D_epi32a =  _mm_unpackhi_epi16(zero_epi16, D_epi16);
					__m128i E_epi32a =  _mm_unpackhi_epi16(zero_epi16, E_epi16);
					__m128i E1_epi32a =  _mm_unpackhi_epi16(zero_epi16, E1_epi16);
																	 
					__m128i A1_epi32b =  _mm_unpacklo_epi16(zero_epi16, A1_epi16);
					__m128i A_epi32b =  _mm_unpacklo_epi16(zero_epi16, A_epi16);
					__m128i B_epi32b =  _mm_unpacklo_epi16(zero_epi16, B_epi16);
					__m128i C_epi32b =  _mm_unpacklo_epi16(zero_epi16, C_epi16);
					__m128i D_epi32b =  _mm_unpacklo_epi16(zero_epi16, D_epi16);
					__m128i E_epi32b =  _mm_unpacklo_epi16(zero_epi16, E_epi16);
					__m128i E1_epi32b =  _mm_unpacklo_epi16(zero_epi16, E1_epi16);

					__m128 A1aps = _mm_cvtepi32_ps(A1_epi32a);
					__m128 Aaps = _mm_cvtepi32_ps(A_epi32a);
					__m128 Baps = _mm_cvtepi32_ps(B_epi32a);
					__m128 Caps = _mm_cvtepi32_ps(C_epi32a);
					__m128 Daps = _mm_cvtepi32_ps(D_epi32a);
					__m128 Eaps = _mm_cvtepi32_ps(E_epi32a);
					__m128 E1aps = _mm_cvtepi32_ps(E1_epi32a);

					__m128 A1bps = _mm_cvtepi32_ps(A1_epi32b);
					__m128 Abps = _mm_cvtepi32_ps(A_epi32b);
					__m128 Bbps = _mm_cvtepi32_ps(B_epi32b);
					__m128 Cbps = _mm_cvtepi32_ps(C_epi32b);
					__m128 Dbps = _mm_cvtepi32_ps(D_epi32b);
					__m128 Ebps = _mm_cvtepi32_ps(E_epi32b);
					__m128 E1bps = _mm_cvtepi32_ps(E1_epi32b);



					A1aps = _mm_mul_ps(A1aps, a);
					Aaps = _mm_mul_ps(Aaps, b);
					Baps = _mm_mul_ps(Baps, b);
					Caps = _mm_mul_ps(Caps, b);
					Daps = _mm_mul_ps(Daps, b);
					Eaps = _mm_mul_ps(Eaps, b);
					E1aps = _mm_mul_ps(E1aps, a);

					A1bps = _mm_mul_ps(A1bps, a);
					Abps = _mm_mul_ps(Abps, b);
					Bbps = _mm_mul_ps(Bbps, b);
					Cbps = _mm_mul_ps(Cbps, b);
					Dbps = _mm_mul_ps(Dbps, b);
					Ebps = _mm_mul_ps(Ebps, b);
					E1bps = _mm_mul_ps(E1bps, a);

					Aaps = _mm_add_ps(Aaps, A1aps);
					Aaps = _mm_add_ps(Aaps, Baps);
					Aaps = _mm_add_ps(Aaps, Caps);
					Aaps = _mm_add_ps(Aaps, Daps);
					Aaps = _mm_add_ps(Aaps, Eaps);
					Aaps = _mm_add_ps(Aaps, E1aps);

					Abps = _mm_add_ps(Abps, A1bps);
					Abps = _mm_add_ps(Abps, Bbps);
					Abps = _mm_add_ps(Abps, Cbps);
					Abps = _mm_add_ps(Abps, Dbps);
					Abps = _mm_add_ps(Abps, Ebps);
					Abps = _mm_add_ps(Abps, E1bps);


					C_epi32a = _mm_cvtps_epi32(Aaps);
					C_epi32b = _mm_cvtps_epi32(Abps);


					C_epi32a = _mm_srai_epi32(C_epi32a,16);
					C_epi32b = _mm_srai_epi32(C_epi32b,16);

					mix_epi16 = _mm_packs_epi32(C_epi32b,C_epi32a);

					_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
				}
			}
			break;
		case 5:
			{
				int pixels8 = (pixels*3) & 0xfff8;

				a = _mm_set1_ps(af);
				b = _mm_set1_ps(bf);
				c = _mm_set1_ps(cf);

				zero_epi16 = _mm_set1_epi16(0);

				for(i=0; i<pixels8; i+=8)
				{
					__m128i mix_epi16;
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


					__m128i A_epi32a =  _mm_unpackhi_epi16(zero_epi16, A_epi16);
					__m128i B_epi32a =  _mm_unpackhi_epi16(zero_epi16, B_epi16);
					__m128i C_epi32a =  _mm_unpackhi_epi16(zero_epi16, C_epi16);
					__m128i D_epi32a =  _mm_unpackhi_epi16(zero_epi16, D_epi16);
					__m128i E_epi32a =  _mm_unpackhi_epi16(zero_epi16, E_epi16);
																	 
					__m128i A_epi32b =  _mm_unpacklo_epi16(zero_epi16, A_epi16);
					__m128i B_epi32b =  _mm_unpacklo_epi16(zero_epi16, B_epi16);
					__m128i C_epi32b =  _mm_unpacklo_epi16(zero_epi16, C_epi16);
					__m128i D_epi32b =  _mm_unpacklo_epi16(zero_epi16, D_epi16);
					__m128i E_epi32b =  _mm_unpacklo_epi16(zero_epi16, E_epi16);

					__m128 Aaps = _mm_cvtepi32_ps(A_epi32a);
					__m128 Baps = _mm_cvtepi32_ps(B_epi32a);
					__m128 Caps = _mm_cvtepi32_ps(C_epi32a);
					__m128 Daps = _mm_cvtepi32_ps(D_epi32a);
					__m128 Eaps = _mm_cvtepi32_ps(E_epi32a);

					__m128 Abps = _mm_cvtepi32_ps(A_epi32b);
					__m128 Bbps = _mm_cvtepi32_ps(B_epi32b);
					__m128 Cbps = _mm_cvtepi32_ps(C_epi32b);
					__m128 Dbps = _mm_cvtepi32_ps(D_epi32b);
					__m128 Ebps = _mm_cvtepi32_ps(E_epi32b);



					Aaps = _mm_mul_ps(Aaps, a);
					Baps = _mm_mul_ps(Baps, b);
					Caps = _mm_mul_ps(Caps, c);
					Daps = _mm_mul_ps(Daps, b);
					Eaps = _mm_mul_ps(Eaps, a);

					Abps = _mm_mul_ps(Abps, a);
					Bbps = _mm_mul_ps(Bbps, b);
					Cbps = _mm_mul_ps(Cbps, c);
					Dbps = _mm_mul_ps(Dbps, b);
					Ebps = _mm_mul_ps(Ebps, a);

					Aaps = _mm_add_ps(Aaps, Baps);
					Aaps = _mm_add_ps(Aaps, Caps);
					Aaps = _mm_add_ps(Aaps, Daps);
					Aaps = _mm_add_ps(Aaps, Eaps);

					Abps = _mm_add_ps(Abps, Bbps);
					Abps = _mm_add_ps(Abps, Cbps);
					Abps = _mm_add_ps(Abps, Dbps);
					Abps = _mm_add_ps(Abps, Ebps);


					C_epi32a = _mm_cvtps_epi32(Aaps);
					C_epi32b = _mm_cvtps_epi32(Abps);


					C_epi32a = _mm_srai_epi32(C_epi32a,16);
					C_epi32b = _mm_srai_epi32(C_epi32b,16);

					mix_epi16 = _mm_packs_epi32(C_epi32b,C_epi32a);

					_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
				}
			}
			break;
		}
	}
	else
	{
		if(taps == 5)
		{
			int pixels8 = (pixels*3) & 0xfff8;

			switch((int)(sharpness * 5.0))
			{
			case 0:
			default:
				af = 0.000f * adiff - 0.0625f * diff; 
				bf = 0.000f * adiff + 0.2500f * diff; 
				cf = 1.000f * adiff + 0.6250f * diff; 
				break;
			case 1: //small sharpen
				//a = _mm_set1_ps(-0.0625);
				//b = _mm_set1_ps(0.25);
				//c = _mm_set1_ps(0.625);
				af =-0.0625f * adiff - 0.1250f * diff;
				bf = 0.2500f * adiff + 0.2500f * diff;
				cf = 0.6250f * adiff + 0.7500f * diff;
				break;
			case 2: //nice sharpen
				//a = _mm_set1_ps(-0.125);
				//b = _mm_set1_ps(0.25);
				//c = _mm_set1_ps(0.75);
				af =-0.1250f * adiff - 0.2500f * diff;
				bf = 0.2500f * adiff + 0.2500f * diff;
				cf = 0.7500f * adiff + 1.0000f * diff;
				break;
			case 3: //higher 
				//a = _mm_set1_ps(-0.25);
				//b = _mm_set1_ps(0.25);
				//c = _mm_set1_ps(1.0);
				af =-0.2500f * adiff - 0.5000f * diff;
				bf = 0.2500f * adiff + 0.5000f * diff;
				cf = 1.0000f * adiff + 1.0000f * diff;
				break;
			case 4: //overkill sharpen
				//a = _mm_set1_ps(-0.5);
				//b =  _mm_set1_ps(0.5);
				//c = _mm_set1_ps(1.0);
				af =-0.5000f * adiff - 0.5000f * diff;
				bf = 0.5000f * adiff + 0.0000f * diff;
				cf = 1.0000f * adiff + 2.0000f * diff;
				break;
			case 5: //highest sharpen
				//a = _mm_set1_ps(-0.5);
				//b = _mm_set1_ps(0.0);
				//c = _mm_set1_ps(2.0);
				af =-0.5000f;
				bf = 0.0000f;
				cf = 2.0000f;
				break;
			}

			
			a = _mm_set1_ps(af);
			b = _mm_set1_ps(bf);
			c = _mm_set1_ps(cf);

			zero_epi16 = _mm_set1_epi16(0);

			/*Bset = _mm_set1_epi16(B);
			Cset = _mm_set1_epi16(C);

			shiftsse2 = shift - prescale;
			if(preshift)
			{
				Bset = _mm_srai_epi16(Bset, preshift);
				Cset = _mm_srai_epi16(Cset, preshift);
				shiftsse2 -= preshift;
			}
			*/

			for(i=0; i<pixels8; i+=8)
			{
				__m128i mix_epi16;
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


				__m128i A_epi32a =  _mm_unpackhi_epi16(zero_epi16, A_epi16);
				__m128i B_epi32a =  _mm_unpackhi_epi16(zero_epi16, B_epi16);
				__m128i C_epi32a =  _mm_unpackhi_epi16(zero_epi16, C_epi16);
				__m128i D_epi32a =  _mm_unpackhi_epi16(zero_epi16, D_epi16);
				__m128i E_epi32a =  _mm_unpackhi_epi16(zero_epi16, E_epi16);
																 
				__m128i A_epi32b =  _mm_unpacklo_epi16(zero_epi16, A_epi16);
				__m128i B_epi32b =  _mm_unpacklo_epi16(zero_epi16, B_epi16);
				__m128i C_epi32b =  _mm_unpacklo_epi16(zero_epi16, C_epi16);
				__m128i D_epi32b =  _mm_unpacklo_epi16(zero_epi16, D_epi16);
				__m128i E_epi32b =  _mm_unpacklo_epi16(zero_epi16, E_epi16);

				__m128 Aaps = _mm_cvtepi32_ps(A_epi32a);
				__m128 Baps = _mm_cvtepi32_ps(B_epi32a);
				__m128 Caps = _mm_cvtepi32_ps(C_epi32a);
				__m128 Daps = _mm_cvtepi32_ps(D_epi32a);
				__m128 Eaps = _mm_cvtepi32_ps(E_epi32a);

				__m128 Abps = _mm_cvtepi32_ps(A_epi32b);
				__m128 Bbps = _mm_cvtepi32_ps(B_epi32b);
				__m128 Cbps = _mm_cvtepi32_ps(C_epi32b);
				__m128 Dbps = _mm_cvtepi32_ps(D_epi32b);
				__m128 Ebps = _mm_cvtepi32_ps(E_epi32b);



				Aaps = _mm_mul_ps(Aaps, a);
				Baps = _mm_mul_ps(Baps, b);
				Caps = _mm_mul_ps(Caps, c);
				Daps = _mm_mul_ps(Daps, b);
				Eaps = _mm_mul_ps(Eaps, a);

				Abps = _mm_mul_ps(Abps, a);
				Bbps = _mm_mul_ps(Bbps, b);
				Cbps = _mm_mul_ps(Cbps, c);
				Dbps = _mm_mul_ps(Dbps, b);
				Ebps = _mm_mul_ps(Ebps, a);

				Aaps = _mm_add_ps(Aaps, Baps);
				Aaps = _mm_add_ps(Aaps, Caps);
				Aaps = _mm_add_ps(Aaps, Daps);
				Aaps = _mm_add_ps(Aaps, Eaps);

				Abps = _mm_add_ps(Abps, Bbps);
				Abps = _mm_add_ps(Abps, Cbps);
				Abps = _mm_add_ps(Abps, Dbps);
				Abps = _mm_add_ps(Abps, Ebps);


				C_epi32a = _mm_cvtps_epi32(Aaps);
				C_epi32b = _mm_cvtps_epi32(Abps);


				C_epi32a = _mm_srai_epi32(C_epi32a,16);
				C_epi32b = _mm_srai_epi32(C_epi32b,16);

				mix_epi16 = _mm_packs_epi32(C_epi32b,C_epi32a);


		/*
				if(prescale)
				{
					A_epi16 = _mm_srai_epi16(A_epi16,prescale);
					B_epi16 = _mm_srai_epi16(B_epi16,prescale);
					C_epi16 = _mm_srai_epi16(C_epi16,prescale);
					D_epi16 = _mm_srai_epi16(D_epi16,prescale);
					E_epi16 = _mm_srai_epi16(E_epi16,prescale);
				}

				if(preshift)
				{
					A_epi16 = _mm_srai_epi16(A_epi16, preshift);
					E_epi16 = _mm_srai_epi16(E_epi16, preshift);
				}

				mix_epi16 = _mm_mullo_epi16(C_epi16, Cset);
				mix_epi16 = _mm_subs_epi16(mix_epi16, A_epi16);
				mix_epi16 = _mm_subs_epi16(mix_epi16, E_epi16);
				tmp_epi16 = _mm_mullo_epi16(B_epi16, Bset);
				mix_epi16 = _mm_adds_epi16(mix_epi16, tmp_epi16);
				tmp_epi16 = _mm_mullo_epi16(D_epi16, Bset);
				mix_epi16 = _mm_adds_epi16(mix_epi16, tmp_epi16);

				mix_epi16 = _mm_srai_epi16(mix_epi16, shiftsse2);
				*/



				_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
			}
		}
		else if(taps == 3)
		{
			int pixels8 = (pixels*3) & 0xfff8;
			

			switch((int)(sharpness * 5.0))
			{
			case 0:
			default:
				af = 0.000f * adiff - 0.0625f * diff; 
				cf = 1.000f * adiff + 1.1250f * diff; 
				break;
			case 1: //small sharpen
				//a = _mm_set1_ps(-0.0625);
				//b = _mm_set1_ps(0.25);
				//c = _mm_set1_ps(0.625);
				af =-0.0625f * adiff - 0.1250f * diff;
				cf = 1.1250f * adiff + 1.2500f * diff;
				break;
			case 2: //nice sharpen
				//a = _mm_set1_ps(-0.125);
				//b = _mm_set1_ps(0.25);
				//c = _mm_set1_ps(0.75);
				af =-0.1250f * adiff - 0.2500f * diff;
				cf = 1.2500f * adiff + 1.5000f * diff;
				break;
			case 3: //higher 
				//a = _mm_set1_ps(-0.25);
				//b = _mm_set1_ps(0.25);
				//c = _mm_set1_ps(1.0);
				af =-0.2500f * adiff - 0.5000f * diff;
				cf = 1.5000f * adiff + 2.0000f * diff;
				break;
			case 4: //overkill sharpen
				//a = _mm_set1_ps(-0.5);
				//b =  _mm_set1_ps(0.5);
				//c = _mm_set1_ps(1.0);
				af =-0.5000f * adiff - 0.5000f * diff;
				cf = 2.0000f * adiff + 2.0000f * diff;
				break;
			case 5: //highest sharpen
				//a = _mm_set1_ps(-0.5);
				//b = _mm_set1_ps(0.0);
				//c = _mm_set1_ps(2.0);
				af =-0.5000f;
				cf = 2.0000f;
				break;
			}

			
			a = _mm_set1_ps(af);
			c = _mm_set1_ps(cf);

			zero_epi16 = _mm_set1_epi16(0);


			for(i=0; i<pixels8; i+=8)
			{
				__m128i mix_epi16;
				__m128i B_epi16 = _mm_load_si128((__m128i *)Bptr);
				__m128i C_epi16 = _mm_load_si128((__m128i *)Cptr);
				__m128i D_epi16 = _mm_load_si128((__m128i *)Dptr);

				Aptr+=8;
				Bptr+=8;
				Cptr+=8;
				Dptr+=8;
				Eptr+=8;


				__m128i B_epi32a =  _mm_unpackhi_epi16(zero_epi16, B_epi16);
				__m128i C_epi32a =  _mm_unpackhi_epi16(zero_epi16, C_epi16);
				__m128i D_epi32a =  _mm_unpackhi_epi16(zero_epi16, D_epi16);
																 
				__m128i B_epi32b =  _mm_unpacklo_epi16(zero_epi16, B_epi16);
				__m128i C_epi32b =  _mm_unpacklo_epi16(zero_epi16, C_epi16);
				__m128i D_epi32b =  _mm_unpacklo_epi16(zero_epi16, D_epi16);

				__m128 Baps = _mm_cvtepi32_ps(B_epi32a);
				__m128 Caps = _mm_cvtepi32_ps(C_epi32a);
				__m128 Daps = _mm_cvtepi32_ps(D_epi32a);

				__m128 Bbps = _mm_cvtepi32_ps(B_epi32b);
				__m128 Cbps = _mm_cvtepi32_ps(C_epi32b);
				__m128 Dbps = _mm_cvtepi32_ps(D_epi32b);

				Baps = _mm_mul_ps(Baps, a);
				Caps = _mm_mul_ps(Caps, c);
				Daps = _mm_mul_ps(Daps, a);

				Bbps = _mm_mul_ps(Bbps, a);
				Cbps = _mm_mul_ps(Cbps, c);
				Dbps = _mm_mul_ps(Dbps, a);

				Baps = _mm_add_ps(Baps, Caps);
				Baps = _mm_add_ps(Baps, Daps);

				Bbps = _mm_add_ps(Bbps, Cbps);
				Bbps = _mm_add_ps(Bbps, Dbps);


				C_epi32a = _mm_cvtps_epi32(Baps);
				C_epi32b = _mm_cvtps_epi32(Bbps);


				C_epi32a = _mm_srai_epi32(C_epi32a,16);
				C_epi32b = _mm_srai_epi32(C_epi32b,16);

				mix_epi16 = _mm_packs_epi32(C_epi32b,C_epi32a);

				_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
			}
		}
		else
		{
			memcpy(output, Cptr, pixels*3*2);
		}
	}
}



void FastSharpeningBlurVW13A(short *Aptr,
						 short *Bptr,
						 short *Cptr,
						 short *Dptr,
						 short *Eptr,
						 int pitch,
						 int edgenear,
						 short *output, 
						 int pixels, 
						 float sharpness,
						 int resolution,
						 int channel_blend_type)
{
	int i=0,taps=1;
	__m128i zero_epi16;
	__m128 a,b,c;
	__m128i maskA_epi16 = _mm_set_epi16((short)-1,0,0,0, (short)-1,0,0,0);
	__m128i maskRGB_epi16 = _mm_set1_epi16((short)-1);

	float af,bf,cf;
	int FIRsize = 5;

	float diff = sharpness * 5.0f - (float)((int)(sharpness * 5.0f));
	float adiff = 1.0f - diff;

	maskRGB_epi16 = _mm_sub_epi16 (maskRGB_epi16, maskA_epi16);
	
	switch(resolution)
	{
	case DECODED_RESOLUTION_FULL:
	case DECODED_RESOLUTION_FULL_DEBAYER:
	case DECODED_RESOLUTION_HALF_HORIZONTAL:
		taps = 5;

		if(	channel_blend_type == BLEND_STACKED_ANAMORPHIC ||
			channel_blend_type == BLEND_LINE_INTERLEAVED ||
			channel_blend_type == BLEND_FREEVIEW)
		{
			taps = 3;
		}
		break;
	case DECODED_RESOLUTION_HALF:
	case DECODED_RESOLUTION_HALF_NODEBAYER:
	case DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER:
	case DECODED_RESOLUTION_HALF_VERTICAL:
		taps = 3;
		
		if(	channel_blend_type == BLEND_STACKED_ANAMORPHIC ||
			channel_blend_type == BLEND_LINE_INTERLEAVED ||
			channel_blend_type == BLEND_FREEVIEW)
		{
			taps = 1;
		}
		break;
	case DECODED_RESOLUTION_QUARTER:
	case DECODED_RESOLUTION_LOWPASS_ONLY:
	case DECODED_RESOLUTION_QUARTER_NODEBAYER_SCALED:
		taps = 1;
		break;
	}



	if(sharpness < 0.0)
	{	if(taps == 5)
		{
			diff = -sharpness * 4.0f - (float)((int)(-sharpness * 4.0f));
			adiff = 1.0f - diff;

			switch(-1 + (int)(sharpness * 4.0f))
			{
				case -5://highest blur
					diff = 1.0f;
				case -4: //blur		
					FIRsize = 9;
					af = 1.0f / 9.0f * diff; 
					bf = (1.0f - af*2.0f) / 7.0f;
					if(edgenear)
					{
						FIRsize = 5;
						af = 0.2f; 
						bf = 0.2f; 
						cf = 0.2f;   
					}
					break;
					
				case -3: //blur	
					FIRsize = 7;
					af = 1.0f / 7.0f * diff; 
					bf = (1.0f - af*2.0f) / 5.0f;
					if(edgenear)
					{
						FIRsize = 5;
						af = 0.2f; 
						bf = 0.2f; 
						cf = 0.2f;   
					}
					break;
				case -2: //blur	
					FIRsize = 5;
					af = 0.00f * adiff + 0.125f * diff; 
					bf = 0.25f * adiff + 0.25f * diff; 
					cf = 0.50f * adiff + 0.25f * diff;   
					break;
				default:
				case -1: //blur	
					FIRsize = 5;
					af = 0.00f * adiff + 0.00f * diff; 
					bf = 0.00f * adiff + 0.25f * diff; 
					cf = 1.00f * adiff + 0.50f * diff;   
					break;
			}
		}
		else if(taps == 3)
		{
			diff = -sharpness;
			adiff = 1.0f - diff;
		
			FIRsize = 5;
			af = 0.00f * adiff + 0.125f * diff; 
			bf = 0.00f * adiff + 0.25f * diff; 
			cf = 1.00f * adiff + 0.25f * diff;   
			
		}
		else
		{
			memcpy(output, Cptr, pixels*3*2);
			FIRsize = 1;
		}

		switch(FIRsize)
		{
		case 9:
			{
				a = _mm_set1_ps(af);
				b = _mm_set1_ps(bf);

				short *A2ptr = Aptr - (pitch>>1)*2;
				short *A1ptr = Aptr - (pitch>>1);
				short *E1ptr = Eptr + (pitch>>1);
				short *E2ptr = Eptr + (pitch>>1)*2;

				zero_epi16 = _mm_set1_epi16(0);

				for(i=0; i<pixels*4; i+=8)
				{
					__m128i mix_epi16;
					__m128i A2_epi16 = _mm_load_si128((__m128i *)A2ptr);
					__m128i A1_epi16 = _mm_load_si128((__m128i *)A1ptr);
					__m128i A_epi16 = _mm_load_si128((__m128i *)Aptr);
					__m128i B_epi16 = _mm_load_si128((__m128i *)Bptr);
					__m128i C_epi16 = _mm_load_si128((__m128i *)Cptr);
					__m128i D_epi16 = _mm_load_si128((__m128i *)Dptr);
					__m128i E_epi16 = _mm_load_si128((__m128i *)Eptr);
					__m128i E1_epi16 = _mm_load_si128((__m128i *)E1ptr);
					__m128i E2_epi16 = _mm_load_si128((__m128i *)E2ptr);

					A2ptr+=8;
					A1ptr+=8;
					Aptr+=8;
					Bptr+=8;
					Cptr+=8;
					Dptr+=8;
					Eptr+=8;
					E1ptr+=8;
					E2ptr+=8;

					__m128i A2_epi32a =  _mm_unpackhi_epi16(zero_epi16, A2_epi16);
					__m128i A1_epi32a =  _mm_unpackhi_epi16(zero_epi16, A1_epi16);
					__m128i A_epi32a =  _mm_unpackhi_epi16(zero_epi16, A_epi16);
					__m128i B_epi32a =  _mm_unpackhi_epi16(zero_epi16, B_epi16);
					__m128i C_epi32a =  _mm_unpackhi_epi16(zero_epi16, C_epi16);
					__m128i D_epi32a =  _mm_unpackhi_epi16(zero_epi16, D_epi16);
					__m128i E_epi32a =  _mm_unpackhi_epi16(zero_epi16, E_epi16);
					__m128i E1_epi32a =  _mm_unpackhi_epi16(zero_epi16, E1_epi16);
					__m128i E2_epi32a =  _mm_unpackhi_epi16(zero_epi16, E2_epi16);
																	 
					__m128i A2_epi32b =  _mm_unpacklo_epi16(zero_epi16, A2_epi16);
					__m128i A1_epi32b =  _mm_unpacklo_epi16(zero_epi16, A1_epi16);
					__m128i A_epi32b =  _mm_unpacklo_epi16(zero_epi16, A_epi16);
					__m128i B_epi32b =  _mm_unpacklo_epi16(zero_epi16, B_epi16);
					__m128i C_epi32b =  _mm_unpacklo_epi16(zero_epi16, C_epi16);
					__m128i D_epi32b =  _mm_unpacklo_epi16(zero_epi16, D_epi16);
					__m128i E_epi32b =  _mm_unpacklo_epi16(zero_epi16, E_epi16);
					__m128i E1_epi32b =  _mm_unpacklo_epi16(zero_epi16, E1_epi16);
					__m128i E2_epi32b =  _mm_unpacklo_epi16(zero_epi16, E2_epi16);

					__m128 A2aps = _mm_cvtepi32_ps(A2_epi32a);
					__m128 A1aps = _mm_cvtepi32_ps(A1_epi32a);
					__m128 Aaps = _mm_cvtepi32_ps(A_epi32a);
					__m128 Baps = _mm_cvtepi32_ps(B_epi32a);
					__m128 Caps = _mm_cvtepi32_ps(C_epi32a);
					__m128 Daps = _mm_cvtepi32_ps(D_epi32a);
					__m128 Eaps = _mm_cvtepi32_ps(E_epi32a);
					__m128 E1aps = _mm_cvtepi32_ps(E1_epi32a);
					__m128 E2aps = _mm_cvtepi32_ps(E2_epi32a);

					__m128 A2bps = _mm_cvtepi32_ps(A2_epi32b);
					__m128 A1bps = _mm_cvtepi32_ps(A1_epi32b);
					__m128 Abps = _mm_cvtepi32_ps(A_epi32b);
					__m128 Bbps = _mm_cvtepi32_ps(B_epi32b);
					__m128 Cbps = _mm_cvtepi32_ps(C_epi32b);
					__m128 Dbps = _mm_cvtepi32_ps(D_epi32b);
					__m128 Ebps = _mm_cvtepi32_ps(E_epi32b);
					__m128 E1bps = _mm_cvtepi32_ps(E1_epi32b);
					__m128 E2bps = _mm_cvtepi32_ps(E2_epi32b);



					A2aps = _mm_mul_ps(A2aps, a);
					A1aps = _mm_mul_ps(A1aps, b);
					Aaps = _mm_mul_ps(Aaps, b);
					Baps = _mm_mul_ps(Baps, b);
					Caps = _mm_mul_ps(Caps, b);
					Daps = _mm_mul_ps(Daps, b);
					Eaps = _mm_mul_ps(Eaps, b);
					E1aps = _mm_mul_ps(E1aps, b);
					E2aps = _mm_mul_ps(E2aps, a);

					A2bps = _mm_mul_ps(A2bps, a);
					A1bps = _mm_mul_ps(A1bps, b);
					Abps = _mm_mul_ps(Abps, b);
					Bbps = _mm_mul_ps(Bbps, b);
					Cbps = _mm_mul_ps(Cbps, b);
					Dbps = _mm_mul_ps(Dbps, b);
					Ebps = _mm_mul_ps(Ebps, b);
					E1bps = _mm_mul_ps(E1bps, b);
					E2bps = _mm_mul_ps(E2bps, a);

					Aaps = _mm_add_ps(Aaps, A2aps);
					Aaps = _mm_add_ps(Aaps, A1aps);
					Aaps = _mm_add_ps(Aaps, Baps);
					Aaps = _mm_add_ps(Aaps, Caps);
					Aaps = _mm_add_ps(Aaps, Daps);
					Aaps = _mm_add_ps(Aaps, Eaps);
					Aaps = _mm_add_ps(Aaps, E1aps);
					Aaps = _mm_add_ps(Aaps, E2aps);

					Abps = _mm_add_ps(Abps, A2bps);
					Abps = _mm_add_ps(Abps, A1bps);
					Abps = _mm_add_ps(Abps, Bbps);
					Abps = _mm_add_ps(Abps, Cbps);
					Abps = _mm_add_ps(Abps, Dbps);
					Abps = _mm_add_ps(Abps, Ebps);
					Abps = _mm_add_ps(Abps, E1bps);
					Abps = _mm_add_ps(Abps, E2bps);


					C_epi32a = _mm_cvtps_epi32(Aaps);
					C_epi32b = _mm_cvtps_epi32(Abps);


					C_epi32a = _mm_srai_epi32(C_epi32a,16);
					C_epi32b = _mm_srai_epi32(C_epi32b,16);

					mix_epi16 = _mm_packs_epi32(C_epi32b,C_epi32a);

					_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
				}
			}
			break;

		case 7:
			{
				a = _mm_set1_ps(af);
				b = _mm_set1_ps(bf);

				short *A1ptr = Aptr - (pitch>>1);
				short *E1ptr = Eptr + (pitch>>1);

				zero_epi16 = _mm_set1_epi16(0);

				for(i=0; i<pixels*4; i+=8)
				{
					__m128i mix_epi16;
					__m128i A1_epi16 = _mm_load_si128((__m128i *)A1ptr);
					__m128i A_epi16 = _mm_load_si128((__m128i *)Aptr);
					__m128i B_epi16 = _mm_load_si128((__m128i *)Bptr);
					__m128i C_epi16 = _mm_load_si128((__m128i *)Cptr);
					__m128i D_epi16 = _mm_load_si128((__m128i *)Dptr);
					__m128i E_epi16 = _mm_load_si128((__m128i *)Eptr);
					__m128i E1_epi16 = _mm_load_si128((__m128i *)E1ptr);

					A1ptr+=8;
					Aptr+=8;
					Bptr+=8;
					Cptr+=8;
					Dptr+=8;
					Eptr+=8;
					E1ptr+=8;

					__m128i A1_epi32a =  _mm_unpackhi_epi16(zero_epi16, A1_epi16);
					__m128i A_epi32a =  _mm_unpackhi_epi16(zero_epi16, A_epi16);
					__m128i B_epi32a =  _mm_unpackhi_epi16(zero_epi16, B_epi16);
					__m128i C_epi32a =  _mm_unpackhi_epi16(zero_epi16, C_epi16);
					__m128i D_epi32a =  _mm_unpackhi_epi16(zero_epi16, D_epi16);
					__m128i E_epi32a =  _mm_unpackhi_epi16(zero_epi16, E_epi16);
					__m128i E1_epi32a =  _mm_unpackhi_epi16(zero_epi16, E1_epi16);
																	 
					__m128i A1_epi32b =  _mm_unpacklo_epi16(zero_epi16, A1_epi16);
					__m128i A_epi32b =  _mm_unpacklo_epi16(zero_epi16, A_epi16);
					__m128i B_epi32b =  _mm_unpacklo_epi16(zero_epi16, B_epi16);
					__m128i C_epi32b =  _mm_unpacklo_epi16(zero_epi16, C_epi16);
					__m128i D_epi32b =  _mm_unpacklo_epi16(zero_epi16, D_epi16);
					__m128i E_epi32b =  _mm_unpacklo_epi16(zero_epi16, E_epi16);
					__m128i E1_epi32b =  _mm_unpacklo_epi16(zero_epi16, E1_epi16);

					__m128 A1aps = _mm_cvtepi32_ps(A1_epi32a);
					__m128 Aaps = _mm_cvtepi32_ps(A_epi32a);
					__m128 Baps = _mm_cvtepi32_ps(B_epi32a);
					__m128 Caps = _mm_cvtepi32_ps(C_epi32a);
					__m128 Daps = _mm_cvtepi32_ps(D_epi32a);
					__m128 Eaps = _mm_cvtepi32_ps(E_epi32a);
					__m128 E1aps = _mm_cvtepi32_ps(E1_epi32a);

					__m128 A1bps = _mm_cvtepi32_ps(A1_epi32b);
					__m128 Abps = _mm_cvtepi32_ps(A_epi32b);
					__m128 Bbps = _mm_cvtepi32_ps(B_epi32b);
					__m128 Cbps = _mm_cvtepi32_ps(C_epi32b);
					__m128 Dbps = _mm_cvtepi32_ps(D_epi32b);
					__m128 Ebps = _mm_cvtepi32_ps(E_epi32b);
					__m128 E1bps = _mm_cvtepi32_ps(E1_epi32b);



					A1aps = _mm_mul_ps(A1aps, a);
					Aaps = _mm_mul_ps(Aaps, b);
					Baps = _mm_mul_ps(Baps, b);
					Caps = _mm_mul_ps(Caps, b);
					Daps = _mm_mul_ps(Daps, b);
					Eaps = _mm_mul_ps(Eaps, b);
					E1aps = _mm_mul_ps(E1aps, a);

					A1bps = _mm_mul_ps(A1bps, a);
					Abps = _mm_mul_ps(Abps, b);
					Bbps = _mm_mul_ps(Bbps, b);
					Cbps = _mm_mul_ps(Cbps, b);
					Dbps = _mm_mul_ps(Dbps, b);
					Ebps = _mm_mul_ps(Ebps, b);
					E1bps = _mm_mul_ps(E1bps, a);

					Aaps = _mm_add_ps(Aaps, A1aps);
					Aaps = _mm_add_ps(Aaps, Baps);
					Aaps = _mm_add_ps(Aaps, Caps);
					Aaps = _mm_add_ps(Aaps, Daps);
					Aaps = _mm_add_ps(Aaps, Eaps);
					Aaps = _mm_add_ps(Aaps, E1aps);

					Abps = _mm_add_ps(Abps, A1bps);
					Abps = _mm_add_ps(Abps, Bbps);
					Abps = _mm_add_ps(Abps, Cbps);
					Abps = _mm_add_ps(Abps, Dbps);
					Abps = _mm_add_ps(Abps, Ebps);
					Abps = _mm_add_ps(Abps, E1bps);


					C_epi32a = _mm_cvtps_epi32(Aaps);
					C_epi32b = _mm_cvtps_epi32(Abps);


					C_epi32a = _mm_srai_epi32(C_epi32a,16);
					C_epi32b = _mm_srai_epi32(C_epi32b,16);

					mix_epi16 = _mm_packs_epi32(C_epi32b,C_epi32a);

					_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
				}
			}
			break;
		case 5:
			{
				a = _mm_set1_ps(af);
				b = _mm_set1_ps(bf);
				c = _mm_set1_ps(cf);

				zero_epi16 = _mm_set1_epi16(0);

				for(i=0; i<pixels*4; i+=8)
				{
					__m128i mix_epi16;
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


					__m128i A_epi32a =  _mm_unpackhi_epi16(zero_epi16, A_epi16);
					__m128i B_epi32a =  _mm_unpackhi_epi16(zero_epi16, B_epi16);
					__m128i C_epi32a =  _mm_unpackhi_epi16(zero_epi16, C_epi16);
					__m128i D_epi32a =  _mm_unpackhi_epi16(zero_epi16, D_epi16);
					__m128i E_epi32a =  _mm_unpackhi_epi16(zero_epi16, E_epi16);
																	 
					__m128i A_epi32b =  _mm_unpacklo_epi16(zero_epi16, A_epi16);
					__m128i B_epi32b =  _mm_unpacklo_epi16(zero_epi16, B_epi16);
					__m128i C_epi32b =  _mm_unpacklo_epi16(zero_epi16, C_epi16);
					__m128i D_epi32b =  _mm_unpacklo_epi16(zero_epi16, D_epi16);
					__m128i E_epi32b =  _mm_unpacklo_epi16(zero_epi16, E_epi16);

					__m128 Aaps = _mm_cvtepi32_ps(A_epi32a);
					__m128 Baps = _mm_cvtepi32_ps(B_epi32a);
					__m128 Caps = _mm_cvtepi32_ps(C_epi32a);
					__m128 Daps = _mm_cvtepi32_ps(D_epi32a);
					__m128 Eaps = _mm_cvtepi32_ps(E_epi32a);

					__m128 Abps = _mm_cvtepi32_ps(A_epi32b);
					__m128 Bbps = _mm_cvtepi32_ps(B_epi32b);
					__m128 Cbps = _mm_cvtepi32_ps(C_epi32b);
					__m128 Dbps = _mm_cvtepi32_ps(D_epi32b);
					__m128 Ebps = _mm_cvtepi32_ps(E_epi32b);



					Aaps = _mm_mul_ps(Aaps, a);
					Baps = _mm_mul_ps(Baps, b);
					Caps = _mm_mul_ps(Caps, c);
					Daps = _mm_mul_ps(Daps, b);
					Eaps = _mm_mul_ps(Eaps, a);

					Abps = _mm_mul_ps(Abps, a);
					Bbps = _mm_mul_ps(Bbps, b);
					Cbps = _mm_mul_ps(Cbps, c);
					Dbps = _mm_mul_ps(Dbps, b);
					Ebps = _mm_mul_ps(Ebps, a);

					Aaps = _mm_add_ps(Aaps, Baps);
					Aaps = _mm_add_ps(Aaps, Caps);
					Aaps = _mm_add_ps(Aaps, Daps);
					Aaps = _mm_add_ps(Aaps, Eaps);

					Abps = _mm_add_ps(Abps, Bbps);
					Abps = _mm_add_ps(Abps, Cbps);
					Abps = _mm_add_ps(Abps, Dbps);
					Abps = _mm_add_ps(Abps, Ebps);


					C_epi32a = _mm_cvtps_epi32(Aaps);
					C_epi32b = _mm_cvtps_epi32(Abps);


					C_epi32a = _mm_srai_epi32(C_epi32a,16);
					C_epi32b = _mm_srai_epi32(C_epi32b,16);

					mix_epi16 = _mm_packs_epi32(C_epi32b,C_epi32a);

					_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
				}
			}
			break;
		}
	}
	else
	{
		if(taps == 5)
		{
			switch((int)(sharpness * 5.0))
			{
			case 0:
			default:
				af = 0.000f * adiff - 0.0625f * diff;
				bf = 0.000f * adiff + 0.2500f * diff;
				cf = 1.000f * adiff + 0.6250f * diff;
				break;
			case 1: //small sharpen
				//a = _mm_set1_ps(-0.0625);
				//b = _mm_set1_ps(0.25);
				//c = _mm_set1_ps(0.625);
				af =-0.0625f * adiff - 0.1250f * diff;
				bf = 0.2500f * adiff + 0.2500f * diff;
				cf = 0.6250f * adiff + 0.7500f * diff;
				break;
			case 2: //nice sharpen
				//a = _mm_set1_ps(-0.125);
				//b = _mm_set1_ps(0.25);
				//c = _mm_set1_ps(0.75);
				af =-0.1250f * adiff - 0.2500f * diff;
				bf = 0.2500f * adiff + 0.2500f * diff;
				cf = 0.7500f * adiff + 1.0000f * diff;
				break;
			case 3: //higher 
				//a = _mm_set1_ps(-0.25);
				//b = _mm_set1_ps(0.25);
				//c = _mm_set1_ps(1.0);
				af =-0.2500f * adiff - 0.5000f * diff;
				bf = 0.2500f * adiff + 0.5000f * diff;
				cf = 1.0000f * adiff + 1.0000f * diff;
				break;
			case 4: //overkill sharpen
				//a = _mm_set1_ps(-0.5);
				//b =  _mm_set1_ps(0.5);
				//c = _mm_set1_ps(1.0);
				af =-0.5000f * adiff - 0.5000f * diff;
				bf = 0.5000f * adiff + 0.0000f * diff;
				cf = 1.0000f * adiff + 2.0000f * diff;
				break;
			case 5: //highest sharpen
				//a = _mm_set1_ps(-0.5);
				//b = _mm_set1_ps(0.0);
				//c = _mm_set1_ps(2.0);
				af =-0.5000f;
				bf = 0.0000f;
				cf = 2.0000f;
				break;
			}

			
			a = _mm_set1_ps(af);
			b = _mm_set1_ps(bf);
			c = _mm_set1_ps(cf);

			zero_epi16 = _mm_set1_epi16(0);

			/*Bset = _mm_set1_epi16(B);
			Cset = _mm_set1_epi16(C);

			shiftsse2 = shift - prescale;
			if(preshift)
			{
				Bset = _mm_srai_epi16(Bset, preshift);
				Cset = _mm_srai_epi16(Cset, preshift);
				shiftsse2 -= preshift;
			}
			*/

			for(i=0; i<pixels*4; i+=8)
			{
				__m128i mix_epi16;
				__m128i tmp_epi16;
				__m128i A_epi16 = _mm_load_si128((__m128i *)Aptr);
				__m128i B_epi16 = _mm_load_si128((__m128i *)Bptr);
				__m128i C_epi16 = _mm_load_si128((__m128i *)Cptr);
				__m128i D_epi16 = _mm_load_si128((__m128i *)Dptr);
				__m128i E_epi16 = _mm_load_si128((__m128i *)Eptr);

				tmp_epi16 = C_epi16;

				Aptr+=8;
				Bptr+=8;
				Cptr+=8;
				Dptr+=8;
				Eptr+=8;


				__m128i A_epi32a =  _mm_unpackhi_epi16(zero_epi16, A_epi16);
				__m128i B_epi32a =  _mm_unpackhi_epi16(zero_epi16, B_epi16);
				__m128i C_epi32a =  _mm_unpackhi_epi16(zero_epi16, C_epi16);
				__m128i D_epi32a =  _mm_unpackhi_epi16(zero_epi16, D_epi16);
				__m128i E_epi32a =  _mm_unpackhi_epi16(zero_epi16, E_epi16);
																 
				__m128i A_epi32b =  _mm_unpacklo_epi16(zero_epi16, A_epi16);
				__m128i B_epi32b =  _mm_unpacklo_epi16(zero_epi16, B_epi16);
				__m128i C_epi32b =  _mm_unpacklo_epi16(zero_epi16, C_epi16);
				__m128i D_epi32b =  _mm_unpacklo_epi16(zero_epi16, D_epi16);
				__m128i E_epi32b =  _mm_unpacklo_epi16(zero_epi16, E_epi16);

				__m128 Aaps = _mm_cvtepi32_ps(A_epi32a);
				__m128 Baps = _mm_cvtepi32_ps(B_epi32a);
				__m128 Caps = _mm_cvtepi32_ps(C_epi32a);
				__m128 Daps = _mm_cvtepi32_ps(D_epi32a);
				__m128 Eaps = _mm_cvtepi32_ps(E_epi32a);

				__m128 Abps = _mm_cvtepi32_ps(A_epi32b);
				__m128 Bbps = _mm_cvtepi32_ps(B_epi32b);
				__m128 Cbps = _mm_cvtepi32_ps(C_epi32b);
				__m128 Dbps = _mm_cvtepi32_ps(D_epi32b);
				__m128 Ebps = _mm_cvtepi32_ps(E_epi32b);



				Aaps = _mm_mul_ps(Aaps, a);
				Baps = _mm_mul_ps(Baps, b);
				Caps = _mm_mul_ps(Caps, c);
				Daps = _mm_mul_ps(Daps, b);
				Eaps = _mm_mul_ps(Eaps, a);

				Abps = _mm_mul_ps(Abps, a);
				Bbps = _mm_mul_ps(Bbps, b);
				Cbps = _mm_mul_ps(Cbps, c);
				Dbps = _mm_mul_ps(Dbps, b);
				Ebps = _mm_mul_ps(Ebps, a);

				Aaps = _mm_add_ps(Aaps, Baps);
				Aaps = _mm_add_ps(Aaps, Caps);
				Aaps = _mm_add_ps(Aaps, Daps);
				Aaps = _mm_add_ps(Aaps, Eaps);

				Abps = _mm_add_ps(Abps, Bbps);
				Abps = _mm_add_ps(Abps, Cbps);
				Abps = _mm_add_ps(Abps, Dbps);
				Abps = _mm_add_ps(Abps, Ebps);


				C_epi32a = _mm_cvtps_epi32(Aaps);
				C_epi32b = _mm_cvtps_epi32(Abps);


				C_epi32a = _mm_srai_epi32(C_epi32a,16);
				C_epi32b = _mm_srai_epi32(C_epi32b,16);

				mix_epi16 = _mm_packs_epi32(C_epi32b,C_epi32a);


				tmp_epi16 = _mm_and_si128(tmp_epi16, maskA_epi16);
				mix_epi16 = _mm_and_si128(mix_epi16, maskRGB_epi16);

				mix_epi16 = _mm_add_epi16 (mix_epi16, tmp_epi16);


				_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
			}
		}
		else if(taps == 3)
		{
			switch((int)(sharpness * 5.0))
			{
			case 0:
			default:
				af = 0.000f * adiff - 0.0625f * diff;
				cf = 1.000f * adiff + 1.1250f * diff;
				break;
			case 1: //small sharpen
				//a = _mm_set1_ps(-0.0625);
				//b = _mm_set1_ps(0.25);
				//c = _mm_set1_ps(0.625);
				af =-0.0625f * adiff - 0.1250f * diff;
				cf = 1.1250f * adiff + 1.2500f * diff;
				break;
			case 2: //nice sharpen
				//a = _mm_set1_ps(-0.125);
				//b = _mm_set1_ps(0.25);
				//c = _mm_set1_ps(0.75);
				af =-0.1250f * adiff - 0.2500f * diff;
				cf = 1.2500f * adiff + 1.5000f * diff;
				break;
			case 3: //higher 
				//a = _mm_set1_ps(-0.25);
				//b = _mm_set1_ps(0.25);
				//c = _mm_set1_ps(1.0);
				af =-0.2500f * adiff - 0.5000f * diff;
				cf = 1.5000f * adiff + 2.0000f * diff;
				break;
			case 4: //overkill sharpen
				//a = _mm_set1_ps(-0.5);
				//b =  _mm_set1_ps(0.5);
				//c = _mm_set1_ps(1.0);
				af =-0.5000f * adiff - 0.5000f * diff;
				cf = 2.0000f * adiff + 2.0000f * diff;
				break;
			case 5: //highest sharpen
				//a = _mm_set1_ps(-0.5);
				//b = _mm_set1_ps(0.0);
				//c = _mm_set1_ps(2.0);
				af =-0.5000f;
				cf = 2.0000f;
				break;
			}

			
			a = _mm_set1_ps(af);
			c = _mm_set1_ps(cf);

			zero_epi16 = _mm_set1_epi16(0);


			for(i=0; i<pixels*4; i+=8)
			{
				__m128i mix_epi16;
				__m128i B_epi16 = _mm_load_si128((__m128i *)Bptr);
				__m128i C_epi16 = _mm_load_si128((__m128i *)Cptr);
				__m128i D_epi16 = _mm_load_si128((__m128i *)Dptr);

				__m128i tmp_epi16 = C_epi16;

				Aptr+=8;
				Bptr+=8;
				Cptr+=8;
				Dptr+=8;
				Eptr+=8;


				__m128i B_epi32a =  _mm_unpackhi_epi16(zero_epi16, B_epi16);
				__m128i C_epi32a =  _mm_unpackhi_epi16(zero_epi16, C_epi16);
				__m128i D_epi32a =  _mm_unpackhi_epi16(zero_epi16, D_epi16);
																 
				__m128i B_epi32b =  _mm_unpacklo_epi16(zero_epi16, B_epi16);
				__m128i C_epi32b =  _mm_unpacklo_epi16(zero_epi16, C_epi16);
				__m128i D_epi32b =  _mm_unpacklo_epi16(zero_epi16, D_epi16);

				__m128 Baps = _mm_cvtepi32_ps(B_epi32a);
				__m128 Caps = _mm_cvtepi32_ps(C_epi32a);
				__m128 Daps = _mm_cvtepi32_ps(D_epi32a);

				__m128 Bbps = _mm_cvtepi32_ps(B_epi32b);
				__m128 Cbps = _mm_cvtepi32_ps(C_epi32b);
				__m128 Dbps = _mm_cvtepi32_ps(D_epi32b);

				Baps = _mm_mul_ps(Baps, a);
				Caps = _mm_mul_ps(Caps, c);
				Daps = _mm_mul_ps(Daps, a);

				Bbps = _mm_mul_ps(Bbps, a);
				Cbps = _mm_mul_ps(Cbps, c);
				Dbps = _mm_mul_ps(Dbps, a);

				Baps = _mm_add_ps(Baps, Caps);
				Baps = _mm_add_ps(Baps, Daps);

				Bbps = _mm_add_ps(Bbps, Cbps);
				Bbps = _mm_add_ps(Bbps, Dbps);


				C_epi32a = _mm_cvtps_epi32(Baps);
				C_epi32b = _mm_cvtps_epi32(Bbps);


				C_epi32a = _mm_srai_epi32(C_epi32a,16);
				C_epi32b = _mm_srai_epi32(C_epi32b,16);

				mix_epi16 = _mm_packs_epi32(C_epi32b,C_epi32a);
				

				tmp_epi16 = _mm_and_si128(tmp_epi16, maskA_epi16);
				mix_epi16 = _mm_and_si128(mix_epi16, maskRGB_epi16);

				mix_epi16 = _mm_add_epi16 (mix_epi16, tmp_epi16);


				_mm_storeu_si128((__m128i *)output, mix_epi16); output+=8;
			}
		}
		else
		{
			memcpy(output, Cptr, pixels*3*2);
		}
	}
}



void FastBlurHinplace(int width, unsigned short *sptr)
{
	unsigned short *outptr = sptr;
	int i=0;
	int rneg1;
	int gneg1;
	int bneg1;
//	*outptr++ = *sptr++; //R
//	*outptr++ = *sptr++; //G
//	*outptr++ = *sptr++; //B
	rneg1 = *sptr++; //R
	gneg1 = *sptr++; //G
	bneg1 = *sptr++; //B

	for(i=1;i<width-1;i++)
	{
		*outptr++ = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //R
		*outptr++ = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //G
		*outptr++ = ((sptr[-3] + sptr[0]*2 + sptr[3])>>2); sptr++; //B
	}

	*outptr++ = *sptr++; //R
	*outptr++ = *sptr++; //G
	*outptr++ = *sptr++; //B

	outptr += 2;

	for(i=2;i<width;i++)
	{
        //TODO: The GCC compiler warns that the operation on outptr may be undefined
        //*outptr-- = outptr[-3];
        //*outptr-- = outptr[-3];
        //*outptr-- = outptr[-3];
        outptr[0] = outptr[-3];
        outptr[-1] = outptr[-4];
        outptr[-2] = outptr[-5];
        outptr -= 3;
	}

	*outptr-- = bneg1;
	*outptr-- = gneg1;
	*outptr-- = rneg1;
}




void DoDEBAYER_ORDER_RED_GRN(
	int width,
	int height,
	int line,
	int pixelstride,
	unsigned short *grn,
	unsigned short *red,
	unsigned short *blu,
	unsigned short *basebayer,
	int highquality,
	int sharpening)
{
	int row;

    //omp_set_dynamic(64);
    //omp_set_num_threads(6);

	//#pragma omp parallel for
	//for (row = 0; row < height; row+=2)
	row = line;
	{
		int x;
		int offset = row*width;
		int rgboffset = 0;//row*width*pixelstride;
		int oddrow = row+1;
		int even_done=0,odd_done=0;

		//even rows
#if DEBAYER5x5
		if(highquality)
		{
			if(row>0 && row<height-2)
			{
				/*red cell*/
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
				blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				for(x=2;x<width-2;x+=2)
				{
					/*red cell*/
					REDCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;

					/*grn cell*/
					GRNREDCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;
				}

				/*red cell*/
				grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
				offset++, rgboffset+=pixelstride;

				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset-1];
				blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				offset++, rgboffset+=pixelstride;
				even_done = 1;
			}
		}
#endif

		if(!even_done)
		{
			if(row>0)
			{
				/*red cell*/
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/*grn cell*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
					offset++, rgboffset+=pixelstride;

					/*red cell*/
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
					red[rgboffset] = basebayer[offset];
					blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
					offset++, rgboffset+=pixelstride;
				}

				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset-1];
				blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				offset++, rgboffset+=pixelstride;
			}
			else // first row
			{
				/*red cell*/
				grn[rgboffset] = (basebayer[offset+1]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = basebayer[offset+width+1];
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/*grn cell*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					blu[rgboffset] = basebayer[offset+width];
					offset++, rgboffset+=pixelstride;

					/*red cell*/
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					red[rgboffset] = basebayer[offset];
					blu[rgboffset] = (basebayer[offset+width-1]+basebayer[offset+width+1] + 1)>>1;
					offset++, rgboffset+=pixelstride;
				}
				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset-1];
				blu[rgboffset] = basebayer[offset+width];
				offset++, rgboffset+=pixelstride;
			}
		}


		//odd rows
#if DEBAYER5x5
		if(highquality)
		{
			if(oddrow > 1 && oddrow<height-1)
			{
				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = basebayer[offset+1];
				offset++, rgboffset+=pixelstride;

				/* blu */
				grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
				red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
				blu[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;

				for(x=2;x<width-2;x+=2)
				{
					/*grn*/
					GRNBLUCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;

					/* blu */
					BLUCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;
				}

				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				/* blu */
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset+width-1] + 1)>>1;
				blu[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;
				odd_done = 1;
			}
		}
#endif

		if(!odd_done)
		{
			if(oddrow<height-1)
			{
				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = basebayer[offset+1];
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/* blu */
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
					red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
					blu[rgboffset] = basebayer[offset];
					offset++, rgboffset+=pixelstride;

					/*grn*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
					blu[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					offset++, rgboffset+=pixelstride;
				}

				/* blu */
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset+width-1] + 1)>>1;
				blu[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;
			}
			else // last row
			{
				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset-width];
				blu[rgboffset] = basebayer[offset+1];
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/* blu */
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1] + 1)>>1;
					blu[rgboffset] = basebayer[offset];
					offset++, rgboffset+=pixelstride;

					/*grn*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = basebayer[offset-width];
					blu[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					offset++, rgboffset+=pixelstride;
				}

				/* blu */
				grn[rgboffset] = basebayer[offset-1];
				red[rgboffset] = basebayer[offset-width-1];
				blu[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;
			}
		}

		{
			switch(sharpening)
			{
			case 0: // just blur
				FastBlurHinplace(width, &red[0]);
				FastBlurHinplace(width, &red[width*pixelstride]);
				break;
			case 1: // blur/sharpen
			case 2: // blur/sharpen
			case 3: // blur/sharpen
				FastSharpeningBlurHinplace(width, &red[0], sharpening);
				FastSharpeningBlurHinplace(width, &red[width*pixelstride], sharpening);
				break;
			default:// do nothing
				break;
			}
		}
	}
}




void DoDEBAYER_ORDER_GRN_BLU(
	int width,
	int height,
	int line,
	int pixelstride,
	unsigned short *grn,
	unsigned short *red,
	unsigned short *blu,
	unsigned short *basebayer,
	int highquality,
	int sharpening)
{
	int row;

    //omp_set_dynamic(64);
    //omp_set_num_threads(6);

	//#pragma omp parallel for
	//for (row = 0; row < height; row+=2)
	row = line;
	{
		int offset = row*width;
		int rgboffset = 0;//row*width*pixelstride;
		int oddrow = row+1;
		int x,even_done=0,odd_done=0;

		//even rows
#if DEBAYER5x5
		if(highquality)
		{
			if(row>0 && row<height-2)
			{
				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width] + basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = basebayer[offset+1];
				offset++, rgboffset+=pixelstride;

				/* blu */
				grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
				red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
				blu[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;


				for(x=2;x<width-2;x+=2)
				{
					/*grn cell*/
					GRNBLUCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;

					/* blu */
					BLUCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;
				}


				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
				blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				/*blu*/
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset+width-1] + 1)>>1;
				blu[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;

				even_done = 1;
			}
		}
#endif

		if(!even_done)
		{
			if(row>0)
			{
				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width] + basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = basebayer[offset+1];
				offset++, rgboffset+=pixelstride;


				for(x=1;x<width-1;x+=2)
				{
					/* blu */
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
					red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
					blu[rgboffset] = basebayer[offset];
					offset++, rgboffset+=pixelstride;

					/*grn cell*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
					blu[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					offset++, rgboffset+=pixelstride;

				}
				/*blu*/
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset+width-1] + 1)>>1;
				blu[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;
			}
			else // first row
			{
				/*grn*/
				grn[rgboffset] = basebayer[offset]; //g
				blu[rgboffset] = basebayer[offset+1];
				red[rgboffset] = basebayer[offset+width];
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/*blu*/
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					red[rgboffset] = (basebayer[offset+width-1]+basebayer[offset+width+1] + 1)>>1;
					blu[rgboffset] = basebayer[offset];
					offset++, rgboffset+=pixelstride;

					/*grn*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = basebayer[offset+width];
					blu[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					offset++, rgboffset+=pixelstride;
				}
				/*blu*/
				grn[rgboffset] = basebayer[offset-1];
				red[rgboffset] = basebayer[offset+width-1];
				blu[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;
			}
		}

		//odd rows
#if DEBAYER5x5
		if(highquality)
		{
			if(oddrow > 1 && oddrow<height-1)
			{
				/*red cell*/
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
				blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				for(x=2;x<width-2;x+=2)
				{
					/*red cell*/
					REDCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;

					/*grn*/
					GRNREDCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;
				}

				/*red cell*/
				grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
				offset++, rgboffset+=pixelstride;

				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset-1];
				blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				odd_done = 1;
			}
		}
#endif

		if(!odd_done)
		{
			if(oddrow<height-1)
			{
				/*red cell*/
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/*grn*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
					offset++, rgboffset+=pixelstride;

					/*red cell*/
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
					red[rgboffset] = basebayer[offset];
					blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
					offset++, rgboffset+=pixelstride;
				}

				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset-1];
				blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				offset++, rgboffset+=pixelstride;
			}
			else // last row
			{
				/*red cell*/
				grn[rgboffset] = basebayer[offset-width];
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = basebayer[offset-width+1];
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/*grn*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					blu[rgboffset] = basebayer[offset-width];
					offset++, rgboffset+=pixelstride;

					/*red cell*/
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					red[rgboffset] = basebayer[offset];
					blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1] + 1)>>1;
					offset++, rgboffset+=pixelstride;
				}
				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset-1];
				blu[rgboffset] = basebayer[offset-width];
				offset++, rgboffset+=pixelstride;
			}
		}

		{
			switch(sharpening)
			{
			case 0: // just blur
				FastBlurHinplace(width, &red[0]);
				FastBlurHinplace(width, &red[width*pixelstride]);
				break;
			case 1: // blur/sharpen
			case 2: // blur/sharpen
			case 3: // blur/sharpen
				FastSharpeningBlurHinplace(width, &red[0], sharpening);
				FastSharpeningBlurHinplace(width, &red[width*pixelstride], sharpening);
				break;
			default:// do nothing
				break;
			}
		}
	}
}




void DoDEBAYER_ORDER_GRN_RED(
	int width,
	int height,
	int line,
	int pixelstride,
	unsigned short *grn,
	unsigned short *red,
	unsigned short *blu,
	unsigned short *basebayer,
	int highquality,
	int sharpening)
{
	int row;

    //omp_set_dynamic(64);
    //omp_set_num_threads(6);

	//#pragma omp parallel for
	//for (row = 0; row < height; row+=2)
	row = line;
	{
		int offset = row*width;
		int rgboffset = 0;//row*width*pixelstride;
		int oddrow = row + 1;
		int x,even_done=0,odd_done=0;

		//even rows
#if DEBAYER5x5
		if(highquality)
		{
			if(row>0 && row<height-2)
			{
				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset+1];
				blu[rgboffset] = (basebayer[offset+width]+basebayer[offset-width] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				/*red cell*/
				grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
				offset++, rgboffset+=pixelstride;

				for(x=2;x<width-2;x+=2)
				{
					/*grn cell*/
					GRNREDCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;

					/*red cell*/
					REDCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;
				}
				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
				blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				/*red cell*/
				grn[rgboffset] = basebayer[offset-1];
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset+width-1] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				even_done = 1;
			}
		}
#endif

		if(!even_done)
		{
			if(row>0)
			{
				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset+1];
				blu[rgboffset] = (basebayer[offset+width]+basebayer[offset-width]+1)>>1;
				offset++, rgboffset+=pixelstride;


				for(x=1;x<width-1;x+=2)
				{
					/*red cell*/
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
					red[rgboffset] = basebayer[offset];
					blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
					offset++, rgboffset+=pixelstride;

					/*grn cell*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
					offset++, rgboffset+=pixelstride;

				}
				/*red cell*/
				grn[rgboffset] = basebayer[offset-1];
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset+width-1] + 1)>>1;
				offset++, rgboffset+=pixelstride;
			}
			else // first row
			{
				/*grn*/
				grn[rgboffset] = basebayer[offset]; //g
				red[rgboffset] = basebayer[offset+1];
				blu[rgboffset] = basebayer[offset+width];
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/*red*/
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					red[rgboffset] = basebayer[offset];
					blu[rgboffset] = (basebayer[offset+width-1]+basebayer[offset+width+1] + 1)>>1;
					offset++, rgboffset+=pixelstride;

					/*grn*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					blu[rgboffset] = basebayer[offset+width];
					offset++, rgboffset+=pixelstride;
				}
				/*red*/
				grn[rgboffset] = basebayer[offset-1];
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = basebayer[offset+width-1];
				offset++, rgboffset+=pixelstride;
			}
		}

		//odd rows
#if DEBAYER5x5
		if(highquality)
		{
			if(oddrow > 1 && oddrow<height-1)
			{
				/*blu*/
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + 1)>>1;
				blu[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;

				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				for(x=2;x<width-2;x+=2)
				{
					/*blu*/
					BLUCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;

					/*grn*/
					GRNBLUCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;
				}

				/*blu*/
				grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
				red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
				blu[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;

				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = basebayer[offset-1];
				offset++, rgboffset+=pixelstride;

				odd_done = 1;
			}
		}
#endif

		if(!odd_done)
		{
			if(oddrow<height-1)
			{
				/*blu*/
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + 1)>>1;
				blu[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/*grn*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
					blu[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					offset++, rgboffset+=pixelstride;

					/*blu*/
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
					red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
					blu[rgboffset] = basebayer[offset];
					offset++, rgboffset+=pixelstride;
				}

				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = basebayer[offset-1];
				offset++, rgboffset+=pixelstride;
			}
			else // last row
			{
				/*blu*/
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+1] + 1)>>1;
				red[rgboffset] = basebayer[offset-width+1];
				blu[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/*grn*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = basebayer[offset-width];
					blu[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					offset++, rgboffset+=pixelstride;

					/*blu*/
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1] + 1)>>1;
					blu[rgboffset] = basebayer[offset];
					offset++, rgboffset+=pixelstride;
				}
				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset-width];
				blu[rgboffset] = basebayer[offset-1];
				offset++, rgboffset+=pixelstride;
			}
		}

		{
			switch(sharpening)
			{
			case 0: // just blur
				FastBlurHinplace(width, &red[0]);
				FastBlurHinplace(width, &red[width*pixelstride]);
				break;
			case 1: // blur/sharpen
			case 2: // blur/sharpen
			case 3: // blur/sharpen
				FastSharpeningBlurHinplace(width, &red[0], sharpening);
				FastSharpeningBlurHinplace(width, &red[width*pixelstride], sharpening);
				break;
			default:// do nothing
				break;
			}
		}
	}
}



void DoDEBAYER_ORDER_BLU_GRN(
	int width,
	int height,
	int line,
	int pixelstride,
	unsigned short *grn,
	unsigned short *red,
	unsigned short *blu,
	unsigned short *basebayer,
	int highquality,
	int sharpening)
{
	int row;

    //omp_set_dynamic(64);
    //omp_set_num_threads(6);

	//#pragma omp parallel for
	//for (row = 0; row < height; row+=2)
	row = line;
	{
		int x;
		int offset = row*width;
		int rgboffset = 0;//row*width*pixelstride;
		int oddrow = row+1;
		int even_done=0,odd_done=0;

		//even rows
#if DEBAYER5x5
		if(highquality)
		{
			if(row>0 && row<height-2)
			{
				/*b  cell*/
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
				red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				for(x=2;x<width-2;x+=2)
				{
					/*b cell*/
					BLUCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;

					/*grn cell*/
					GRNBLUCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;
				}

				/*b cell*/
				grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
				blu[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
				offset++, rgboffset+=pixelstride;

				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				blu[rgboffset] = basebayer[offset-1];
				red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				even_done = 1;
			}
		}
#endif

		if(!even_done)
		{
			if(row>0)
			{
				/*b cell*/
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/*grn cell*/
					grn[rgboffset] = basebayer[offset];
					blu[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
					offset++, rgboffset+=pixelstride;

					/*b cell*/
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
					blu[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
					offset++, rgboffset+=pixelstride;
				}

				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				blu[rgboffset] = basebayer[offset-1];
				red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				offset++, rgboffset+=pixelstride;
			}
			else // first row
			{
				/*b cell*/
				grn[rgboffset] = (basebayer[offset+1]+basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset+width+1];
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/*grn cell*/
					grn[rgboffset] = basebayer[offset];
					blu[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					red[rgboffset] = basebayer[offset+width];
					offset++, rgboffset+=pixelstride;

					/*b cell*/
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					blu[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset+width-1]+basebayer[offset+width+1] + 1)>>1;
					offset++, rgboffset+=pixelstride;
				}
				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				blu[rgboffset] = basebayer[offset-1];
				red[rgboffset] = basebayer[offset+width];
				offset++, rgboffset+=pixelstride;
			}
		}


		//odd rows
#if DEBAYER5x5
		if(highquality)
		{
			if(oddrow > 1 && oddrow<height-1)
			{
				/*grn*/
				grn[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = basebayer[offset+1];
				offset++, rgboffset+=pixelstride;

				/* r */
				grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
				blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
				red[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;

				for(x=2;x<width-2;x+=2)
				{
					/*grn*/
					GRNREDCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;

					/* r */
					REDCELL(&red[rgboffset],&basebayer[offset], width);
					offset++, rgboffset+=pixelstride;
				}

				/*grn*/
				grn[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
				offset++, rgboffset+=pixelstride;

				/* r */
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset+width-1] + 1)>>1;
				red[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;

				odd_done = 1;
			}
		}
#endif

		if(!odd_done)
		{
			if(oddrow<height-1)
			{
				/*grn*/
				grn[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = basebayer[offset+1];
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/* r */
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1]+basebayer[offset-width]+basebayer[offset+width] + 2)>>2;
					blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1]+basebayer[offset+width-1]+basebayer[offset+width+1] + 2)>>2;
					red[rgboffset] = basebayer[offset];
					offset++, rgboffset+=pixelstride;

					/*grn*/
					grn[rgboffset] = basebayer[offset];
					blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
					red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					offset++, rgboffset+=pixelstride;
				}

				/* r */
				grn[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset+width-1] + 1)>>1;
				red[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;
			}
			else // last row
			{
				/*grn*/
				grn[rgboffset] = basebayer[offset];
				blu[rgboffset] = basebayer[offset-width];
				red[rgboffset] = basebayer[offset+1];
				offset++, rgboffset+=pixelstride;

				for(x=1;x<width-1;x+=2)
				{
					/* r */
					grn[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					blu[rgboffset] = (basebayer[offset-width-1]+basebayer[offset-width+1] + 1)>>1;
					red[rgboffset] = basebayer[offset];
					offset++, rgboffset+=pixelstride;

					/*grn*/
					grn[rgboffset] = basebayer[offset];
					blu[rgboffset] = basebayer[offset-width];
					red[rgboffset] = (basebayer[offset-1]+basebayer[offset+1] + 1)>>1;
					offset++, rgboffset+=pixelstride;
				}

				/* r */
				grn[rgboffset] = basebayer[offset-1];
				blu[rgboffset] = basebayer[offset-width-1];
				red[rgboffset] = basebayer[offset];
				offset++, rgboffset+=pixelstride;
			}
		}

		{
			switch(sharpening)
			{
			case 0: // just blur
				FastBlurHinplace(width, &red[0]);
				FastBlurHinplace(width, &red[width*pixelstride]);
				break;
			case 1: // blur/sharpen
			case 2: // blur/sharpen
			case 3: // blur/sharpen
				FastSharpeningBlurHinplace(width, &red[0], sharpening);
				FastSharpeningBlurHinplace(width, &red[width*pixelstride], sharpening);
				break;
			default:// do nothing
				break;
			}
		}
	}
}


// Send one frame to the Debayer Unit (row pitch is in bytes)
void DebayerLine(int width, int height, int linenum,
				unsigned short *bayer_source,
				DEBAYER_ORDERING order,
				unsigned short *RGB_output,
				int highquality,
				int sharpening)
{
	assert(bayer_source != NULL);
	if (bayer_source == NULL) return;

	assert(RGB_output != NULL);
	if (RGB_output == NULL) return;

	unsigned short *grn;
	unsigned short *red;
	unsigned short *blu;
	//unsigned short *g1bayer;
	//unsigned short *r_bayer;
	//unsigned short *b_bayer;
	//unsigned short *g2bayer;
	int pixelstride=3;
	//int bayerplanar=0;
	unsigned short *basebayer = (unsigned short *)bayer_source;

	red = RGB_output++;
	grn = RGB_output++;
	blu = RGB_output++;

	RGB_output-=3;

	switch(order)
	{
	case BAYER_FORMAT_RED_GRN:
		DoDEBAYER_ORDER_RED_GRN(
			width,
			height,
			linenum,
			pixelstride,
			grn,
			red,
			blu,
			basebayer,
			highquality,
			sharpening);
		break;

	case BAYER_FORMAT_GRN_BLU:
		DoDEBAYER_ORDER_GRN_BLU(
			width,
			height,
			linenum,
			pixelstride,
			grn,
			red,
			blu,
			basebayer,
			highquality,
			sharpening);
		break;

	case BAYER_FORMAT_GRN_RED:
		DoDEBAYER_ORDER_GRN_RED(
			width,
			height,
			linenum,
			pixelstride,
			grn,
			red,
			blu,
			basebayer,
			highquality,
			sharpening);
		break;

	case BAYER_FORMAT_BLU_GRN:
		DoDEBAYER_ORDER_BLU_GRN(
			width,
			height,
			linenum,
			pixelstride,
			grn,
			red,
			blu,
			basebayer,
			highquality,
			sharpening);
		break;
	}

	return;
}




void DoVertical_DEBAYER_ORDER_RED_GRN(
	int width,
	int height,
	int line,
	int pixelstride,
	unsigned short *grn,
	unsigned short *red,
	unsigned short *blu,
	unsigned short *basebayer,
	int highquality,
	int sharpening)
{
	int row;

	row = line;
	{
		int x;
		int offset = row*width;
		int rgboffset = 0;//row*width*pixelstride;
		int oddrow = row+1;
		int even_done=0,odd_done=0;

		//even rows
		if(!even_done)
		{
			if(row>0)
			{
				/*red cell*/
				grn[rgboffset] = basebayer[offset+1];
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + 1)>>1;
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*red cell*/
					grn[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					red[rgboffset] = basebayer[offset];
					blu[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + basebayer[offset-width-1]+basebayer[offset+width-1] + 2)>>2;
					offset+=2, rgboffset+=pixelstride;
				}
			}
			else // first row
			{
				/*red cell*/
				grn[rgboffset] = basebayer[offset+1];
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = basebayer[offset+width+1];
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*red cell*/
					grn[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					red[rgboffset] = basebayer[offset];
					blu[rgboffset] = basebayer[offset+width+1];
					offset+=2, rgboffset+=pixelstride;
				}
			}
		}

		rgboffset+=pixelstride*(width/2);

		if(!odd_done)
		{
			if(oddrow<height-1)
			{
				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = basebayer[offset+1];
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*grn*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
					blu[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					offset+=2, rgboffset+=pixelstride;
				}
			}
			else // last row
			{
				/*grn*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset-width];
				blu[rgboffset] = basebayer[offset+1];
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*grn*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = basebayer[offset-width];
					blu[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					offset+=2, rgboffset+=pixelstride;
				}
			}
		}

		{
			switch(sharpening)
			{
			case 0: // just blur
				FastBlurHinplace(width/2, &red[0]);
				FastBlurHinplace(width/2, &red[width*pixelstride]);
				break;
			case 1: // blur/sharpen
			case 2: // blur/sharpen
			case 3: // blur/sharpen
				FastSharpeningBlurHinplace(width/2, &red[0], sharpening);
				FastSharpeningBlurHinplace(width/2, &red[width*pixelstride], sharpening);
				break;
			default:// do nothing
				break;
			}
		}
	}
}




void DoVertical_DEBAYER_ORDER_GRN_BLU(
	int width,
	int height,
	int line,
	int pixelstride,
	unsigned short *grn,
	unsigned short *red,
	unsigned short *blu,
	unsigned short *basebayer,
	int highquality,
	int sharpening)
{
	int row;

	row = line;
	{
		int offset = row*width;
		int rgboffset = 0;//row*width*pixelstride;
		int oddrow = row+1;
		int x,even_done=0,odd_done=0;

		//even rows
		if(!even_done)
		{
			if(row>0)
			{
				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width] + basebayer[offset+width] + 1)>>1;
				blu[rgboffset] = basebayer[offset+1];
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*grn cell*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-width] + basebayer[offset+width] + 1)>>1;
					blu[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					offset+=2, rgboffset+=pixelstride;
				}
			}
			else // first row
			{
				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset+width];
				blu[rgboffset] = basebayer[offset+1];
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*grn cell*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = basebayer[offset+width];
					blu[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					offset+=2, rgboffset+=pixelstride;
				}
			}
		}

		rgboffset+=pixelstride*(width/2);

		//odd rows
		if(!odd_done)
		{
			if(oddrow<height-1)
			{
				/*red cell*/
				grn[rgboffset] = basebayer[offset+1];
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + 1)>>1;
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*red cell*/
					grn[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					red[rgboffset] = basebayer[offset];
					blu[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + basebayer[offset-width-1]+basebayer[offset+width-1] + 2)>>2;
					offset+=2, rgboffset+=pixelstride;
				}
			}
			else // last row
			{
				/*red cell*/
				grn[rgboffset] = basebayer[offset+1];
				red[rgboffset] = basebayer[offset];
				blu[rgboffset] = basebayer[offset-width+1];
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*red cell*/
					grn[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					red[rgboffset] = basebayer[offset];
					blu[rgboffset] = basebayer[offset-width+1];
					offset+=2, rgboffset+=pixelstride;
				}
			}
		}

		{
			switch(sharpening)
			{
			case 0: // just blur
				FastBlurHinplace(width/2, &red[0]);
				FastBlurHinplace(width/2, &red[width*pixelstride]);
				break;
			case 1: // blur/sharpen
			case 2: // blur/sharpen
			case 3: // blur/sharpen
				FastSharpeningBlurHinplace(width/2, &red[0], sharpening);
				FastSharpeningBlurHinplace(width/2, &red[width*pixelstride], sharpening);
				break;
			default:// do nothing
				break;
			}
		}
	}
}




void DoVertical_DEBAYER_ORDER_GRN_RED(
	int width,
	int height,
	int line,
	int pixelstride,
	unsigned short *grn,
	unsigned short *red,
	unsigned short *blu,
	unsigned short *basebayer,
	int highquality,
	int sharpening)
{
	int row;

	row = line;
	{
		int offset = row*width;
		int rgboffset = 0;//row*width*pixelstride;
		int oddrow = row + 1;
		int x,even_done=0,odd_done=0;

		//even rows
		if(!even_done)
		{
			if(row>0)
			{
				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset+1];
				blu[rgboffset] = (basebayer[offset+width]+basebayer[offset-width]+1)>>1;
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*grn cell*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					blu[rgboffset] = (basebayer[offset+width]+basebayer[offset-width]+1)>>1;
					offset+=2, rgboffset+=pixelstride;
				}
			}
			else // first row
			{
				/*grn cell*/
				grn[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset+1];
				blu[rgboffset] = basebayer[offset+width];
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*grn cell*/
					grn[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					blu[rgboffset] = basebayer[offset+width];
					offset+=2, rgboffset+=pixelstride;
				}
			}
		}

		rgboffset+=pixelstride*(width/2);

		//odd rows
		if(!odd_done)
		{
			if(oddrow<height-1)
			{
				/*blu*/
				grn[rgboffset] = basebayer[offset+1];
				red[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + 1)>>1;
				blu[rgboffset] = basebayer[offset];
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*blu*/
					grn[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					red[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + basebayer[offset-width-1]+basebayer[offset+width-1] + 2)>>2;
					blu[rgboffset] = basebayer[offset];
					offset+=2, rgboffset+=pixelstride;
				}
			}
			else // last row
			{
				/*blu*/
				grn[rgboffset] = basebayer[offset+1];
				red[rgboffset] = basebayer[offset-width+1];
				blu[rgboffset] = basebayer[offset];
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*blu*/
					grn[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					red[rgboffset] = basebayer[offset-width+1];
					blu[rgboffset] = basebayer[offset];
					offset+=2, rgboffset+=pixelstride;
				}
			}
		}

		{
			switch(sharpening)
			{
			case 0: // just blur
				FastBlurHinplace(width/2, &red[0]);
				FastBlurHinplace(width/2, &red[width*pixelstride]);
				break;
			case 1: // blur/sharpen
			case 2: // blur/sharpen
			case 3: // blur/sharpen
				FastSharpeningBlurHinplace(width/2, &red[0], sharpening);
				FastSharpeningBlurHinplace(width/2, &red[width*pixelstride], sharpening);
				break;
			default:// do nothing
				break;
			}
		}
	}
}



void DoVertical_DEBAYER_ORDER_BLU_GRN(
	int width,
	int height,
	int line,
	int pixelstride,
	unsigned short *grn,
	unsigned short *red,
	unsigned short *blu,
	unsigned short *basebayer,
	int highquality,
	int sharpening)
{
	int row;

	row = line;
	{
		int x;
		int offset = row*width;
		int rgboffset = 0;//row*width*pixelstride;
		int oddrow = row+1;
		int even_done=0,odd_done=0;

		//even rows
		if(!even_done)
		{
			if(row>0)
			{
				/*b cell*/
				grn[rgboffset] = basebayer[offset+1];
				blu[rgboffset] = basebayer[offset];
				red[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + 1)>>1;
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*b cell*/
					grn[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					blu[rgboffset] = basebayer[offset];
					red[rgboffset] = (basebayer[offset-width+1]+basebayer[offset+width+1] + basebayer[offset-width-1]+basebayer[offset+width-1] + 2)>>2;
					offset+=2, rgboffset+=pixelstride;
				}
			}
			else // first row
			{
				/*b cell*/
				grn[rgboffset] = basebayer[offset+1];
				blu[rgboffset] = basebayer[offset];
				red[rgboffset] = basebayer[offset+width+1];
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*b cell*/
					grn[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					blu[rgboffset] = basebayer[offset];
					red[rgboffset] = basebayer[offset+width+1];
					offset+=2, rgboffset+=pixelstride;
				}
			}
		}

		rgboffset+=pixelstride*(width/2);

		//odd rows
		if(!odd_done)
		{
			if(oddrow<height-1)
			{
				/*grn*/
				grn[rgboffset] = basebayer[offset];
				blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
				red[rgboffset] = basebayer[offset+1];
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*grn*/
					grn[rgboffset] = basebayer[offset];
					blu[rgboffset] = (basebayer[offset-width]+basebayer[offset+width] + 1)>>1;
					red[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					offset+=2, rgboffset+=pixelstride;
				}
			}
			else // last row
			{
				/*grn*/
				grn[rgboffset] = basebayer[offset];
				blu[rgboffset] = basebayer[offset-width];
				red[rgboffset] = basebayer[offset+1];
				offset+=2, rgboffset+=pixelstride;

				for(x=2;x<width;x+=2)
				{
					/*grn*/
					grn[rgboffset] = basebayer[offset];
					blu[rgboffset] = basebayer[offset-width];
					red[rgboffset] = (basebayer[offset-1] + basebayer[offset+1] + 1)>>1;
					offset+=2, rgboffset+=pixelstride;
				}
			}
		}

		{
			switch(sharpening)
			{
			case 0: // just blur
				FastBlurHinplace(width/2, &red[0]);
				FastBlurHinplace(width/2, &red[width*pixelstride]);
				break;
			case 1: // blur/sharpen
			case 2: // blur/sharpen
			case 3: // blur/sharpen
				FastSharpeningBlurHinplace(width/2, &red[0], sharpening);
				FastSharpeningBlurHinplace(width/2, &red[width*pixelstride], sharpening);
				break;
			default:// do nothing
				break;
			}
		}
	}
}



// Send one frame to the Debayer Unit (row pitch is in bytes)
void VerticalOnlyDebayerLine(int width, int height, int linenum,
				unsigned short *bayer_source,
				DEBAYER_ORDERING order,
				unsigned short *RGB_output, 
				int highquality, int sharpening)
{
	assert(bayer_source != NULL);
	if (bayer_source == NULL) return;

	assert(RGB_output != NULL);
	if (RGB_output == NULL) return;

	unsigned short *grn;
	unsigned short *red;
	unsigned short *blu;
	int pixelstride=3;
	unsigned short *basebayer = (unsigned short *)bayer_source;

	red = RGB_output++;
	grn = RGB_output++;
	blu = RGB_output++;

	RGB_output-=3;

	switch(order)
	{
	case BAYER_FORMAT_RED_GRN:
		DoVertical_DEBAYER_ORDER_RED_GRN(
			width,
			height,
			linenum,
			pixelstride,
			grn,
			red,
			blu,
			basebayer,
			highquality,
			sharpening);
		break;

	case BAYER_FORMAT_GRN_BLU:
		DoVertical_DEBAYER_ORDER_GRN_BLU(
			width,
			height,
			linenum,
			pixelstride,
			grn,
			red,
			blu,
			basebayer,
			highquality,
			sharpening);
		break;

	case BAYER_FORMAT_GRN_RED:
		DoVertical_DEBAYER_ORDER_GRN_RED(
			width,
			height,
			linenum,
			pixelstride,
			grn,
			red,
			blu,
			basebayer,
			highquality,
			sharpening);
		break;

	case BAYER_FORMAT_BLU_GRN:
		DoVertical_DEBAYER_ORDER_BLU_GRN(
			width,
			height,
			linenum,
			pixelstride,
			grn,
			red,
			blu,
			basebayer,
			highquality,
			sharpening);
		break;
	}

	return;
}




void ColorDifference2Bayer(int width,
						   unsigned short *srcptr,
						   int bayer_pitch,
						   int bayer_format)
{
	int x;
	//int i;
	unsigned short *bayerptr,*G,*RG,*BG,*GD,*lineA16,*lineB16;
	unsigned short buffer[16384];								// was 8192 - could not handle 4.5K RAW

	lineA16 = buffer;
	lineB16 = lineA16 + bayer_pitch/2;

	bayerptr = srcptr;
	G = bayerptr;
	RG = G + bayer_pitch/4;
	BG = RG + bayer_pitch/4;
	GD = BG + bayer_pitch/4;


	__m128i gggggggg,ggggggg1,ggggggg2,rgrgrgrg,bgbgbgbg,gdgdgdgd;
	__m128i rrrrrrrr,bbbbbbbb;
	__m128i mid8192 = _mm_set1_epi16(8192);
	//__m128i mid16384 = _mm_set1_epi16(16384);
	//__m128i mid32768 = _mm_set1_epi16(32768);

	__m128i overflowprotectRGB_epi16 = _mm_set1_epi16(0x7fff-0x3fff);
	int sse2width = width & 0xfff8;


	x = 0;
	for(; x<sse2width; x+=8) //TODO SSE version
	{
		gggggggg = _mm_loadu_si128((__m128i *)G); G+=8;
		rgrgrgrg = _mm_loadu_si128((__m128i *)RG); RG+=8;
		bgbgbgbg = _mm_loadu_si128((__m128i *)BG); BG+=8;
		gdgdgdgd = _mm_loadu_si128((__m128i *)GD); GD+=8;


		gggggggg = _mm_srli_epi16(gggggggg, 2);// 0-16383 14bit unsigned
		rgrgrgrg = _mm_srli_epi16(rgrgrgrg, 2);// 14bit unsigned
		bgbgbgbg = _mm_srli_epi16(bgbgbgbg, 2);// 14bit unsigned
		gdgdgdgd = _mm_srli_epi16(gdgdgdgd, 2);// 14bit unsigned

		gdgdgdgd = _mm_subs_epi16(gdgdgdgd, mid8192);// -8191 to 8191 14bit signed

		rrrrrrrr = _mm_subs_epi16(rgrgrgrg, mid8192);// -8191 to 8191 14bit signed
		rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 1);		// -16382 to 16382 15bit signed
		rrrrrrrr = _mm_adds_epi16(rrrrrrrr, gggggggg); // -16382 to 32767

		bbbbbbbb = _mm_subs_epi16(bgbgbgbg, mid8192);// -8191 to 8191 14bit signed
		bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 1);		// -16382 to 16382 15bit signed
		bbbbbbbb = _mm_adds_epi16(bbbbbbbb, gggggggg); // -16382 to 32767

		ggggggg1 = _mm_adds_epi16(gggggggg, gdgdgdgd);// -8191 to 8191 14bit signed

		ggggggg2 = _mm_subs_epi16(gggggggg, gdgdgdgd);// -8191 to 8191 14bit signed

		//limit to 0 to 16383
		rrrrrrrr = _mm_adds_epi16(rrrrrrrr, overflowprotectRGB_epi16);
		rrrrrrrr = _mm_subs_epu16(rrrrrrrr, overflowprotectRGB_epi16);
		bbbbbbbb = _mm_adds_epi16(bbbbbbbb, overflowprotectRGB_epi16);
		bbbbbbbb = _mm_subs_epu16(bbbbbbbb, overflowprotectRGB_epi16);
		ggggggg1 = _mm_adds_epi16(ggggggg1, overflowprotectRGB_epi16);
		ggggggg1 = _mm_subs_epu16(ggggggg1, overflowprotectRGB_epi16);
		ggggggg2 = _mm_adds_epi16(ggggggg2, overflowprotectRGB_epi16);
		ggggggg2 = _mm_subs_epu16(ggggggg2, overflowprotectRGB_epi16);

		rrrrrrrr = _mm_slli_epi16(rrrrrrrr, 2); // restore to 0 to 65535
		bbbbbbbb = _mm_slli_epi16(bbbbbbbb, 2); // restore to 0 to 65535
		ggggggg1 = _mm_slli_epi16(ggggggg1, 2); // restore to 0 to 65535
		ggggggg2 = _mm_slli_epi16(ggggggg2, 2); // restore to 0 to 65535

		switch(bayer_format)
		{
		case BAYER_FORMAT_RED_GRN: //Red-grn phase
			_mm_storeu_si128((__m128i *)lineA16, _mm_unpacklo_epi16(rrrrrrrr,ggggggg1)); lineA16+=8;
			_mm_storeu_si128((__m128i *)lineA16, _mm_unpackhi_epi16(rrrrrrrr,ggggggg1)); lineA16+=8;
			_mm_storeu_si128((__m128i *)lineB16, _mm_unpacklo_epi16(ggggggg2,bbbbbbbb)); lineB16+=8;
			_mm_storeu_si128((__m128i *)lineB16, _mm_unpackhi_epi16(ggggggg2,bbbbbbbb)); lineB16+=8;
			break;
		case BAYER_FORMAT_GRN_RED:// grn-red
			_mm_storeu_si128((__m128i *)lineA16, _mm_unpacklo_epi16(ggggggg1,rrrrrrrr)); lineA16+=8;
			_mm_storeu_si128((__m128i *)lineA16, _mm_unpackhi_epi16(ggggggg1,rrrrrrrr)); lineA16+=8;
			_mm_storeu_si128((__m128i *)lineB16, _mm_unpacklo_epi16(bbbbbbbb,ggggggg2)); lineB16+=8;
			_mm_storeu_si128((__m128i *)lineB16, _mm_unpackhi_epi16(bbbbbbbb,ggggggg2)); lineB16+=8;
			break;
		case BAYER_FORMAT_GRN_BLU:
			_mm_storeu_si128((__m128i *)lineA16, _mm_unpacklo_epi16(ggggggg1,bbbbbbbb)); lineA16+=8;
			_mm_storeu_si128((__m128i *)lineA16, _mm_unpackhi_epi16(ggggggg1,bbbbbbbb)); lineA16+=8;
			_mm_storeu_si128((__m128i *)lineB16, _mm_unpacklo_epi16(rrrrrrrr,ggggggg2)); lineB16+=8;
			_mm_storeu_si128((__m128i *)lineB16, _mm_unpackhi_epi16(rrrrrrrr,ggggggg2)); lineB16+=8;
			break;
		case BAYER_FORMAT_BLU_GRN:
			_mm_storeu_si128((__m128i *)lineA16, _mm_unpacklo_epi16(bbbbbbbb,ggggggg1)); lineA16+=8;
			_mm_storeu_si128((__m128i *)lineA16, _mm_unpackhi_epi16(bbbbbbbb,ggggggg1)); lineA16+=8;
			_mm_storeu_si128((__m128i *)lineB16, _mm_unpacklo_epi16(ggggggg2,rrrrrrrr)); lineB16+=8;
			_mm_storeu_si128((__m128i *)lineB16, _mm_unpackhi_epi16(ggggggg2,rrrrrrrr)); lineB16+=8;
			break;
		}
	}

	for(; x<width; x++)
	{
		int r,g,b,rg,bg,gd,g1,g2;


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

		switch(bayer_format)
		{
		case BAYER_FORMAT_RED_GRN: //Red-grn phase
			*lineA16++ = r;
			*lineA16++ = g1;
			*lineB16++ = g2;
			*lineB16++ = b;
			break;
		case BAYER_FORMAT_GRN_RED:// grn-red
			*lineA16++ = g1;
			*lineA16++ = r;
			*lineB16++ = b;
			*lineB16++ = g2;
			break;
		case BAYER_FORMAT_GRN_BLU:
			*lineA16++ = g1;
			*lineA16++ = b;
			*lineB16++ = r;
			*lineB16++ = g2;
			break;
		case BAYER_FORMAT_BLU_GRN:
			*lineA16++ = b;
			*lineA16++ = g1;
			*lineB16++ = g2;
			*lineB16++ = r;
			break;
		}
	}

	memcpy(bayerptr, buffer, bayer_pitch*2);
}





void BayerRippleFilter(	int width,
						   unsigned short *srcptr,
						   int bayer_pitch,
						   int bayer_format,
						   unsigned short *srcbase)
{
	unsigned char *line = (unsigned char *)srcptr;
	unsigned short *outA16;
	int x,offset = bayer_pitch/2;

	outA16 = (unsigned short *)line;

	// If on a red line, move to a blue line
	//Normalize to a blue pixel for the start point

	switch(bayer_format)
	{
	case BAYER_FORMAT_GRN_RED:
		outA16 -= offset;
		break;
	case BAYER_FORMAT_RED_GRN:
		outA16 -= offset;
		outA16 ++; //blue
		break;
	case BAYER_FORMAT_GRN_BLU:
		outA16 ++; //blue
		break;
	case BAYER_FORMAT_BLU_GRN:
		//blue
		break;
	}

	if(&outA16[-2*offset-2] < srcbase)
		return; //HACK to make sure we are reading within the picture


	{

		outA16++; //b
		outA16++; //g

		outA16++; //r
		//now point to green

		for(x=2; x<width-2; x++)
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


#ifdef _WIN32

static int
WINAPI
lstrlenWInternal(
    LPCWSTR lpString
    )
{
    int i = -1;
    while (*(lpString+(++i)))
        ;
    return i;
}

#endif

float *LoadCube64_3DLUT(DECODER *decoder, CFHDDATA *cfhddata, int *lutsize)
{
	int size = 0;
	float *LUT = NULL;
	bool useLUT = false;
	CFLook_Header CFLKhdr;
	char crcname[256];
	FILE *fp;
	int err = 0;

	if(cfhddata->user_look_CRC != 0 && decoder)
	{
		if(cfhddata->user_look_CRC == decoder->LUTcacheCRC && decoder->LUTcache != NULL)
		{
			*lutsize = decoder->LUTcacheSize;
			return decoder->LUTcache;
		}
		else if(decoder->LUTcache != NULL)
		{
#if _ALLOCATOR
			Free(decoder->allocator, decoder->LUTcache);
#else
			MEMORY_FREE(decoder->LUTcache);
#endif
			decoder->LUTcache = NULL;
			decoder->LUTcacheCRC = 0;
			decoder->LUTcacheSize = 0;
		}

		if(cfhddata->user_look_CRC == 0x3f6f5788) // Default Protune preview LUT
		{
			*lutsize = size = 32;
		/*	float PreviewLUT[32];
			
			for(i=0; i<size; i++)
			{
				PreviewLUT[i] = 0.5 * sinf(3.14159265359 * (float)i/(float)(size-1) + 4.71238898038) + 0.5;
				{
					char t[100];
					sprintf(t, "%f", PreviewLUT[i]);
					OutputDebugString(t);
				}
			} */
			
			float PreviewLUT[32] = {
				0.000000f,
				0.002565f,
				0.010235f,
				0.022930f,
				0.040521f,
				0.062827f,
				0.089618f,
				0.120621f,
				0.155517f,
				0.193947f,
				0.235518f,
				0.279803f,
				0.326347f,
				0.374674f,
				0.424286f,
				0.474675f,
				0.525325f,
				0.575714f,
				0.625326f,
				0.673653f,
				0.720197f,
				0.764482f,
				0.806053f,
				0.844483f,
				0.879379f,
				0.910382f,
				0.937173f,
				0.959479f,
				0.977070f,
				0.989765f,
				0.997435f,
				1.000000f
			};

#if _ALLOCATOR
			LUT = (float *)Alloc(decoder->allocator, 4*size*size*size*3);
#else
			LUT = (float *)MEMORY_ALLOC(4*size*size*size*3);
#endif
			if(LUT)
			{
				int r,g,b;
				float *fptr = LUT; 

				for(r=0; r<size; r++)
				{
					for(g=0; g<size; g++)
					{
						for(b=0; b<size; b++)
						{
							*fptr++ = PreviewLUT[b];
							*fptr++ = PreviewLUT[g];
							*fptr++ = PreviewLUT[r];
						}
					}
				}

				decoder->LUTcacheCRC = cfhddata->user_look_CRC;
				decoder->LUTcache = LUT;
				decoder->LUTcacheSize = size;
				return LUT;
			}
		}

		if(decoder->LUTsPathStr[0] == 0)
			InitLUTPaths(decoder);

#ifdef _WIN32
		sprintf_s(crcname, sizeof(crcname), "%s/%08X.cflook", decoder->LUTsPathStr, (uint32_t)cfhddata->user_look_CRC);
		err = fopen_s(&fp, crcname, "rb");
#else
		sprintf(crcname,"%s/%08X.cflook", decoder->LUTsPathStr, (uint32_t)cfhddata->user_look_CRC);
		fp = fopen(crcname, "rb");
#endif

		if (err == 0 && fp != NULL)
		{
			int endianswap = 0;
			int validcflook = 0;
			int len = 0;

#ifdef _MSC_VER
			len = (int)fread_s(&CFLKhdr, sizeof(CFLook_Header), 1, sizeof(CFLook_Header), fp);
#else
			len = (int)fread(&CFLKhdr, 1, sizeof(CFLook_Header), fp);
#endif

			if(MAKEID('C','F','L','K') == CFLKhdr.CFLK_ID)
			{
				endianswap = true;
				validcflook = true;
			}
			else if(MAKEID_SWAP('C','F','L','K') == CFLKhdr.CFLK_ID)
			{
				validcflook = true;
			}

			if(validcflook && len > 0)
			{
				if(endianswap)
				{
					*lutsize = size = SwapInt32(CFLKhdr.lutsize);

					if(size >= 8 && size <= 65)
					{
#if _ALLOCATOR
						LUT = (float *)Alloc(decoder->allocator, 4*size*size*size*3);
#else
						LUT = (float *)MEMORY_ALLOC(4*size*size*size*3);
#endif
						if(LUT)
						{
							fseek(fp, SwapInt32(CFLKhdr.hdrsize), SEEK_SET);
							len = (int)fread(LUT,4,size*size*size*3,fp);
							if(len == size*size*size*3)
							{
								unsigned int *uiLUT = (unsigned int *)LUT;
								for(int i=0;i<len;i++)
								{
									uiLUT[i] = SwapInt32(uiLUT[i]);
								}
								useLUT = true;
							}
							else
							{
#if _ALLOCATOR
								Free(decoder->allocator, LUT);
#else
								MEMORY_FREE(LUT);
#endif
								LUT = NULL;
							}
						}
					}
				}
				else
				{
					*lutsize = size = CFLKhdr.lutsize;

					if(size >= 8 && size <= 65)
					{
#if _ALLOCATOR
						LUT = (float *)Alloc(decoder->allocator, 4*size*size*size*3);
#else
						LUT = (float *)MEMORY_ALLOC(4*size*size*size*3);
#endif
						if(LUT)
						{
							fseek(fp,CFLKhdr.hdrsize,SEEK_SET);
							len = (int)fread(LUT,4,size*size*size*3,fp);
							if(len == size*size*size*3)
							{
								useLUT = true;
							}
							else
							{
								MEMORY_FREE(LUT);
								LUT = NULL;
							}
						}
					}
				}
			}
			fclose(fp);
		}
	}

	if (decoder)
	{
		if(useLUT)
		{
			decoder->LUTcacheCRC = cfhddata->user_look_CRC;
			decoder->LUTcache = LUT;
			decoder->LUTcacheSize = *lutsize;
		}
		else
		{
			decoder->LUTcacheCRC = 0;
			decoder->LUTcache = NULL;
			decoder->LUTcacheSize = 0;
		}
	}

	return LUT;
}


float *ResetCube64_3DLUT(DECODER *decoder, int cube_base)
{
	//int len = 0;
	int size = 1 << cube_base;
	float *LUT = NULL;


#if _ALLOCATOR
	LUT = (float *)Alloc(decoder->allocator, 4*size*size*size*3);
#else
	LUT = (float *)MEMORY_ALLOC(4*size*size*size*3);
#endif
	if(LUT)
	{
		int r,g,b,pos=0;

		for(b=0;b<size;b++)
		{
			for(g=0;g<size;g++)
			{
				for(r=0;r<size;r++)
				{
					LUT[pos++] = (float)r/(float)(size-1);
					LUT[pos++] = (float)g/(float)(size-1);
					LUT[pos++] = (float)b/(float)(size-1);
				}
			}
		}
	}

	return LUT;
}

//DAN20100927 -- return 0 if to tag groups can the same data types are sizes, although many different content
int CompareTags(unsigned char *ptr1, unsigned char *ptr2, int len)
{
	int ret = 0, size;
	uint32_t *src = (uint32_t *)ptr1;
	uint32_t *dst = (uint32_t *)ptr2;

	len >>= 2;

	while(len>=3)
	{
		if(src[0] != dst[0]) //tag
		{
			ret = 1; // no match
			break;
		}
		if(src[1] != dst[1]) //typesize
		{
			ret = 1; // no match
			break;
		}

		size = ((src[1] & 0xffffff) + 3)>>2;
		size += 2; //tag + typesize
		src += size;
		dst += size;
		len -= size;
	}

	return ret;
}


void UpdateCFHDDATA(DECODER *decoder, unsigned char *ptr, int len, int delta, int priority)
{
	int chn = 0;
	CFHDDATA *cfhddata = NULL;
	if(decoder)
		cfhddata = &decoder->cfhddata;


	if(delta)
		chn = delta;

	if(cfhddata && ptr && len) // overrides form database or external control
	{
		//unsigned char *base = ptr;
		void *data;
		unsigned char type;
		int pos = 0;
		size_t size, copysize;
		uint32_t tag;
		//void *metadatastart = data;
		float tmp;
		bool terminate = false;
		int localpri = priority;
		
		if(decoder->metadatachunks < METADATA_CHUNK_MAX)
		{
			int i;
			bool found = false;
	
			for (i = 0; i < decoder->metadatachunks; i++)
			{
				if(decoder->mdc_size[i] == len)
				{
					if(0 == CompareTags(decoder->mdc[i], ptr, len))
					{
						memcpy(decoder->mdc[i], ptr, len); // If same info type is present, use the later info (e.g. latest relevant keyframe.)
						found = true;
						break;
					}
				}
			}

			if (!found)
			{
		#if _ALLOCATOR
				if(decoder->mdc[decoder->metadatachunks])
					Free(decoder->allocator, decoder->mdc[decoder->metadatachunks]);
				decoder->mdc[decoder->metadatachunks] = (unsigned char *)Alloc(decoder->allocator, len);
		#else
				if(decoder->mdc[decoder->metadatachunks])
					MEMORY_FREE(decoder->mdc[decoder->metadatachunks]);
				decoder->mdc[decoder->metadatachunks] = (unsigned char *)MEMORY_ALLOC(len);
		#endif
				if(decoder->mdc[decoder->metadatachunks])
					memcpy(decoder->mdc[decoder->metadatachunks], ptr, len);
				decoder->mdc_size[decoder->metadatachunks] = len;
		
				decoder->metadatachunks++;
			}
		}

		while(pos+12 <= len && !terminate)
		{
			data = (void *)&ptr[8];
			type = ptr[7];
			size = ptr[4] + (ptr[5]<<8) + (ptr[6]<<16);
			tag = MAKETAG(ptr[0],ptr[1],ptr[2],ptr[3]);

#if _WIN32 && _DEBUG && 0
			if(type == 'f')
			{
				char t[1000],tt[100];
				int cc = 16; 
				int lsize = (int)size;
				float *fdata = (float *)data;
				sprintf(t,"%c%c%c%c %1.8f ", ptr[0], ptr[1], ptr[2], ptr[3], *fdata++);

				while(lsize > 4 && cc > 0)
				{
					sprintf(tt,"%1.8f ", *fdata++), lsize -= 4, cc--;
					strcat(t,tt);
				}
				OutputDebugString(t);
			}
			if(type == 'L')
			{
				char t[1000],tt[100];
				int cc = 16; 
				int lsize = (int)size;
				int *ddata = (int *)data;
				sprintf(t,"%c%c%c%c %d ", ptr[0], ptr[1], ptr[2], ptr[3], *ddata++);

				while(lsize > 4 && cc > 0)
				{
					sprintf(tt,"%d ", *ddata++), lsize -= 4, cc--;
					strcat(t,tt);
				}
				OutputDebugString(t);
			}
			if(type == 'H')
			{
				char t[1000],tt[100];
				int cc = 16; 
				int lsize = (int)size;
				int *ddata = (int *)data;
				sprintf(t,"%c%c%c%c %08X ", ptr[0], ptr[1], ptr[2], ptr[3], *ddata++);

				while(lsize > 4 && cc > 0)
				{
					sprintf(tt,"%08X ", *ddata++), lsize -= 4, cc--;
					strcat(t,tt);
				}
				OutputDebugString(t);
			}
#endif

			switch(tag)
			{
			case 0:
				terminate = true;
				break;

			case TAG_CLIP_GUID:
				if(size == sizeof(cfhddata->clip_guid))
					memcpy(&cfhddata->clip_guid, data, size);
				break;
			case TAG_PROCESS_PATH:
				if(!delta)
				{
					unsigned int val = *((unsigned int *)data);
					if(val & PROCESSING_ACTIVE2)
						cfhddata->process_path_flags = val;
					else
					{
						cfhddata->process_path_flags &= 0xffffff00;
						cfhddata->process_path_flags |= (val & 0xff);
					}
				}
				break;

			case TAG_COLORSPACE_YUV: // 601/709
				if(*((uint32_t *)data) & 1)
				{
					cfhddata->colorspace &= ~COLOR_SPACE_BT_709;
					cfhddata->colorspace |= COLOR_SPACE_BT_601;
				}
				if(*((uint32_t *)data) & 2)
				{
					cfhddata->colorspace &= ~COLOR_SPACE_BT_601;
					cfhddata->colorspace |= COLOR_SPACE_BT_709;
				}
				
				decoder->frame.colorspace_override = cfhddata->colorspace;
				break;
				
			case TAG_COLORSPACE_RGB: // cgRGB/vsRGB
				if(*((uint32_t *)data) & 1)
				{
					cfhddata->colorspace &= ~COLOR_SPACE_VS_RGB;
				}
				if(*((uint32_t *)data) & 2)
				{
					cfhddata->colorspace |= COLOR_SPACE_VS_RGB;
				}
				if((cfhddata->colorspace & (COLOR_SPACE_BT_601|COLOR_SPACE_BT_709)) == 0) // YUV mode not set
					cfhddata->colorspace |= COLOR_SPACE_BT_709;
				
				decoder->frame.colorspace_override = cfhddata->colorspace;
				break;

			case TAG_COLORSPACE_LIMIT:
				if(*((uint32_t *)data) == 1)
					decoder->broadcastLimit = 1;
				else
					decoder->broadcastLimit = 0;
				break;
				
			case TAG_COLORSPACE_FTR: // 422 dup'd/422to444 filtered
				if(*((uint32_t *)data) & 1)
				{
					cfhddata->colorspace |= COLOR_SPACE_422_TO_444;
				}
				else
				{
					cfhddata->colorspace &= ~COLOR_SPACE_422_TO_444;
				}
				break;

			case TAG_PIXEL_RATIO:
				if(type == 'R' || type == 'H') // some older bitstreams under 'H' instead of 'R'
				{
					uint32_t val = *((uint32_t *)data);
					decoder->pixel_aspect_x = (val >> 16) & 0xffff;
					decoder->pixel_aspect_y = val & 0xffff;
				}
				break;

			case TAG_MIX_DOWN_ALPHA:
				decoder->useAlphaMixDown[0] = *((uint32_t *)data);
				if(size >= 8)
					decoder->useAlphaMixDown[1] = *(((uint32_t *)data)+1);
				break;

			case TAG_CALIBRATE:
				cfhddata->calibration = *((uint32_t *)data);
				break;

			case TAG_BAYER_FORMAT:
				cfhddata->bayer_format = *((unsigned int *)data);
				break;
				
			case TAG_CHANNELS_ACTIVE:
				if(!delta)
				{
					cfhddata->MSChannel_type_value &= 0xffffff00;
					cfhddata->MSChannel_type_value |= *((unsigned int *)data);
				}
				break;

			case TAG_CHANNELS_MIX:
				if(!delta)
				{
					cfhddata->MSChannel_type_value &= 0xffff00ff;
					cfhddata->MSChannel_type_value |= *((unsigned int *)data)<<8;
				}
				break;

			case TAG_CHANNELS_MIX_VAL:
				if(!delta)
				{
					cfhddata->MSChannel_type_value &= 0x0000ffff;
					cfhddata->MSChannel_type_value |= (*((unsigned int *)data))<<16;
					cfhddata->split_pos_xy = ((*((unsigned int *)data))>>16) & 0xffff;
				}
				break;	

			case TAG_DEMOSAIC_TYPE:
				if(!delta)
					cfhddata->demosaic_type = *((unsigned int *)data);
				break;

				
			case TAG_CHANNEL_SWAP:
				{
					size_t lsize = size;
					if (lsize > sizeof(unsigned long))
						lsize = sizeof(unsigned long);

					if (*((uint32_t *)data) == 0)
						cfhddata->FramingFlags &= ~2;
					else
						cfhddata->FramingFlags |= 2;
				}
				break; 

			case TAG_LENS_GOPRO:
				cfhddata->lensGoPro = *((uint32_t *)data);
				break; 
			case TAG_LENS_SPHERE:
				cfhddata->lensSphere = *((uint32_t *)data);
				break; 
			case TAG_LENS_FILL:
				cfhddata->lensFill = *((uint32_t *)data);
				break; 
			case TAG_LENS_STYLE:
				cfhddata->lensStyleSel = *((uint32_t *)data);
				switch(cfhddata->lensStyleSel)
				{
				case 0:
					cfhddata->lensGoPro = -1;
					cfhddata->lensSphere = 0;
					cfhddata->lensFill = 0;
					break;
				case 1: //GoPro
					cfhddata->lensGoPro = 1;
					cfhddata->lensSphere = 1;
					cfhddata->lensFill = 0;
					break;
				case 2: //GoPro + fill
					cfhddata->lensGoPro = 1;
					cfhddata->lensSphere = 1;
					cfhddata->lensFill = 1;
					break;
				case 3: //equi-rect
					cfhddata->lensGoPro = 2;
					cfhddata->lensSphere = 1;
					cfhddata->lensFill = 0;
					break;
				case 4: //custom lens
					cfhddata->lensGoPro = 4;
					cfhddata->lensSphere = 1;
					cfhddata->lensFill = 0;
					break;
		/*		case 3: //Rectinlinear
					cfhddata->lensGoPro = 0;
					cfhddata->lensSphere = 1;
					cfhddata->lensFill = 0;
					break;
				case 4: //Rectinlinear + fill
					cfhddata->lensGoPro = 0;
					cfhddata->lensSphere = 1;
					cfhddata->lensFill = 1;
					break; */
	//			case 6: //DE-fish
		//			cfhddata->lensGoPro = 3;
			//		cfhddata->lensSphere = 0;
				//	cfhddata->lensFill = 0;
					//break;

				}
				break;

			case TAG_LENS_SRC_PARAMS:
				memcpy(&cfhddata->lensCustomSRC, data, size <= sizeof(cfhddata->lensCustomSRC) ? size : sizeof(cfhddata->lensCustomSRC));
				break;
			case TAG_LENS_DST_PARAMS:
				memcpy(&cfhddata->lensCustomDST, data, size <= sizeof(cfhddata->lensCustomDST) ? size : sizeof(cfhddata->lensCustomDST));
				break;

			case TAG_CHANNEL_FLIP:
				//(1(Horiz)|2(Vert))<<channel num.  0 = no flip, 1 = h flip chn.1, 4 h flip chn.2, 0xf v/h flip chns.1&2, etc
				if(!delta)
				{
					cfhddata->channel_flip = *((uint32_t *)data);
				}
				break;				
			case TAG_ENCODE_PRESET: // used by BYR4 inputs to indicate the source data is not linear.
				if(!delta)
					cfhddata->encode_curve_preset = *((unsigned int *)data);
				break;
			case TAG_ENCODE_CURVE:
				if(!delta)
					cfhddata->encode_curve = *((unsigned int *)data);
				break;
			case TAG_DECODE_CURVE:
				if(!delta)
					cfhddata->decode_curve = *((unsigned int *)data);
				break;
			case TAG_PRIMARIES_CURVE:
				if(!delta)
				{
					cfhddata->PrimariesUseDecodeCurve = (*((unsigned int *)data) == CURVE_LINEAR ? 0 : 1);
				}
				break;

			case TAG_CPU_MAX:
				cfhddata->cpu_limit =  *((uint32_t *)data);
				if(decoder->thread_cntrl.capabilities && cfhddata->cpu_limit > 0)
				{				
					int cpus = decoder->thread_cntrl.capabilities>>16;

					if(cpus > (int)cfhddata->cpu_limit)
					{
						cpus = cfhddata->cpu_limit;
						decoder->thread_cntrl.capabilities &= 0xffff;
						decoder->thread_cntrl.capabilities |= cpus<<16;
					}
				}
				break;

			case TAG_AFFINITY_MASK:
				cfhddata->cpu_affinity =  *((uint32_t *)data);
				break;

			case TAG_IGNORE_DATABASE:
				cfhddata->ignore_disk_database =  *((uint32_t *)data);
				break;

			case TAG_FORCE_DATABASE:
				cfhddata->force_disk_database =  *((uint32_t *)data);
				break;

			case TAG_UPDATE_LAST_USED:
				cfhddata->update_last_used = *((uint32_t *)data);
				break;

			case TAG_UNIQUE_FRAMENUM:
				decoder->codec.unique_framenumber =  *((uint32_t *)data);
				break;

			case TAG_TIMECODE:
#ifdef _WIN32
				strncpy_s(cfhddata->FileTimecodeData.orgtime, sizeof(cfhddata->FileTimecodeData.orgtime),(char *)data, 15);
#else				
				strncpy(cfhddata->FileTimecodeData.orgtime, (char *)data, 15);
#endif
				break;

			case TAG_TIMECODE_BASE:
				cfhddata->timecode_base = *((uint32_t *)data);
				break;
				
			case TAG_PREFORMATTED_3D:
				decoder->preformatted_3D_type = *((unsigned int *)data);
				break;

				
				// Moved out for now
			case TAG_OVERLAYS:
				{
					size_t lsize = size;
					if (lsize > sizeof(uint32_t))
						lsize = sizeof(uint32_t);
					
					if (*((uint32_t *)data) == 0)
						cfhddata->BurninFlags &= ~1;
					else
						cfhddata->BurninFlags |= 1;
				}
				break;
				
			case TAG_TOOLS:
				{
					size_t lsize = size;
					if (lsize > sizeof(uint32_t))
						lsize = sizeof(uint32_t);
					
					if (*((uint32_t *)data) == 0)
						cfhddata->BurninFlags &= ~2;
					else
						cfhddata->BurninFlags |= 2;
				}
				break;
			}

			
			{
				switch(tag)
				{
				case TAG_LOOK_CRC:
					if(!delta)
					{
						cfhddata->user_look_CRC = *((unsigned int *)data);
						if(cfhddata->user_look_CRC == 0)
							cfhddata->process_path_flags &= ~PROCESSING_LOOK_FILE;
					}
					break;
				case TAG_LOOK_FILE:
					if(!delta)
					{
						int copysize = (int)size;
						if(copysize > 39) copysize = 39;
#ifdef _WIN32
						strncpy_s(cfhddata->look_filename, sizeof(cfhddata->look_filename), (char *)data, copysize);
#else
						strncpy(cfhddata->look_filename, (char *)data, copysize);
#endif
						cfhddata->look_filename[copysize] = '\0';
					}
					break;
				case TAG_LOOK_EXPORT:
					if(!delta)
					{
						if(0 != strncmp(cfhddata->look_export_path, (char *)data, size))
						{
#ifdef _WIN32
							strncpy_s(cfhddata->look_export_path, sizeof(cfhddata->look_export_path), (char *)data, size);
#else
							strncpy(cfhddata->look_export_path, (char *)data, size);
#endif
							cfhddata->look_export_path[size] = '\0';
							cfhddata->export_look = 1;
						}
					}
					break;
				case TAG_WHITE_BALANCE:
					if(delta)
					{
						size_t i;
						int col = 0;
						float *fptr = (float *)data;
						for(i=0; i<size/sizeof(float); i++)
						{
							tmp = *fptr++;// - 1.0;  //DAN20100922 -- Fix error in who left/right White balance was calculated.

							if(i != 2) //second green skip
							{
								cfhddata->channel[chn].white_balance[col] = cfhddata->channel[0].white_balance[col] * tmp;
								if(cfhddata->channel[chn].white_balance[col] < 0.4f) cfhddata->channel[chn].white_balance[col] = 0.4f;
								if(cfhddata->channel[chn].white_balance[col] > 10.0f) cfhddata->channel[chn].white_balance[col] = 10.0f;
						
								col++;	
							}
						}
					}
					else
					{
						size_t i;
						int col = 0;
						float *fptr = (float *)data;
						for(i=0; i<size/sizeof(float); i++)
						{
							tmp = *fptr++;

							if(i != 2) //second green skip
							{
								cfhddata->channel[0].white_balance[col] = tmp;
								if(cfhddata->channel[0].white_balance[col] < 0.4f) cfhddata->channel[0].white_balance[col] = 0.4f;
								if(cfhddata->channel[0].white_balance[col] > 10.0f) cfhddata->channel[0].white_balance[col] = 10.0f;
						
								cfhddata->channel[1].white_balance[col] = cfhddata->channel[0].white_balance[col];
								cfhddata->channel[2].white_balance[col] = cfhddata->channel[0].white_balance[col];
								col++;	
							}
						}
					}
					break;
				case TAG_COLOR_MATRIX:
					if(delta)
					{
						size_t i;
						float *fptr = (float *)data;
						float *fcolm = &cfhddata->orig_colormatrix[0][0];
						float *fcolm2 = &cfhddata->custom_colormatrix[0][0];

						for(i=0; i<size/sizeof(float); i++)
						{
							tmp = *fptr++;

							if(priority >= METADATA_PRIORITY_OVERRIDE)
								*fcolm2++ += tmp;
							else
							{
								*fcolm++ += tmp;
								*fcolm2++ += tmp;
							}
						}
					}
					else
					{
						if(priority >= METADATA_PRIORITY_OVERRIDE)
						{
							memcpy(cfhddata->custom_colormatrix, data, size);
						}
						else
						{
							memcpy(cfhddata->orig_colormatrix, data, size);
							memcpy(cfhddata->custom_colormatrix, data, size);
						}
					}
					break;
				case TAG_GAMMA_TWEAKS:
					{
						size_t i;
						float *fptr = (float *)data;
						for(i=0; i<size/sizeof(float); i++)
						{
							tmp = *fptr++;
							if(delta)
							{
								cfhddata->channel[chn].user_rgb_gamma[i] = cfhddata->channel[0].user_rgb_gamma[i] + tmp;
								if(cfhddata->channel[chn].user_rgb_gamma[i] < 0.01f) cfhddata->channel[chn].user_rgb_gamma[i] = 0.01f;
								if(cfhddata->channel[chn].user_rgb_gamma[i] > 10.0f) cfhddata->channel[chn].user_rgb_gamma[i] = 10.0f;
							}
							else
							{
								cfhddata->channel[0].user_rgb_gamma[i] = tmp;
								if(cfhddata->channel[0].user_rgb_gamma[i] < 0.01f) cfhddata->channel[0].user_rgb_gamma[i] = 0.01f;
								if(cfhddata->channel[0].user_rgb_gamma[i] > 10.0f) cfhddata->channel[0].user_rgb_gamma[i] = 10.0f;

								cfhddata->channel[1].user_rgb_gamma[i] = cfhddata->channel[0].user_rgb_gamma[i];
								cfhddata->channel[2].user_rgb_gamma[i] = cfhddata->channel[0].user_rgb_gamma[i];
							}
						}
					}
					break;
				case TAG_RGB_GAIN:
					{
						size_t i;
						float *fptr = (float *)data;
						for(i=0; i<size/sizeof(float); i++)
						{
							tmp = *fptr++;
							if(delta)
							{
								cfhddata->channel[chn].user_rgb_gain[i] = cfhddata->channel[0].user_rgb_gain[i] * tmp;
								if(cfhddata->channel[chn].user_rgb_gain[i] < 0.0) cfhddata->channel[chn].user_rgb_gain[i] = 0.0;
								if(cfhddata->channel[chn].user_rgb_gain[i] > 10.0) cfhddata->channel[chn].user_rgb_gain[i] = 10.0;
							}
							else
							{
								cfhddata->channel[0].user_rgb_gain[i] = tmp;	 // unity at 1.0
								if(cfhddata->channel[0].user_rgb_gain[i] < 0.0) cfhddata->channel[0].user_rgb_gain[i] = 0.0;
								if(cfhddata->channel[0].user_rgb_gain[i] > 10.0) cfhddata->channel[0].user_rgb_gain[i] = 10.0;

								cfhddata->channel[1].user_rgb_gain[i] = cfhddata->channel[0].user_rgb_gain[i];
								cfhddata->channel[2].user_rgb_gain[i] = cfhddata->channel[0].user_rgb_gain[i];
							}
						}
					}
					break;
				case TAG_RGB_OFFSET:
					{
						size_t i;
						float *fptr = (float *)data;
						for(i=0; i<size/sizeof(float); i++)
						{
							tmp = *fptr++;
							if(delta)
							{
								cfhddata->channel[chn].user_rgb_lift[i] = cfhddata->channel[0].user_rgb_lift[i] + tmp;
								if(cfhddata->channel[chn].user_rgb_lift[i] < -1.0) cfhddata->channel[chn].user_rgb_lift[i] = -1.0;
								if(cfhddata->channel[chn].user_rgb_lift[i] > 1.0) cfhddata->channel[chn].user_rgb_lift[i] = 1.0;
							}
							else
							{
								cfhddata->channel[0].user_rgb_lift[i] = tmp;
								if(cfhddata->channel[0].user_rgb_lift[i] < -1.0) cfhddata->channel[0].user_rgb_lift[i] = -1.0;
								if(cfhddata->channel[0].user_rgb_lift[i] > 1.0) cfhddata->channel[0].user_rgb_lift[i] = 1.0;

								cfhddata->channel[1].user_rgb_lift[i] = cfhddata->channel[0].user_rgb_lift[i];
								cfhddata->channel[2].user_rgb_lift[i] = cfhddata->channel[0].user_rgb_lift[i];
							}
						}
					}
					break;
				case TAG_SATURATION:
					{
						if(delta)
						{
							cfhddata->channel[chn].user_saturation = cfhddata->channel[0].user_saturation + (*((float *)data));	// unity at 0.0
                            if(cfhddata->channel[chn].user_saturation < -1.0) cfhddata->channel[chn].user_saturation = -1.0;
							if(cfhddata->channel[chn].user_saturation > 10.0) cfhddata->channel[chn].user_saturation = 10.0;
						}
						else
						{
							cfhddata->channel[0].user_saturation = (*((float *)data) - 1.0f);	// unity at 0.0
							if(cfhddata->channel[0].user_saturation < -1.0f) cfhddata->channel[0].user_saturation = -1.0f;
							if(cfhddata->channel[0].user_saturation > 10.0f) cfhddata->channel[0].user_saturation = 10.0f;

							cfhddata->channel[1].user_saturation = cfhddata->channel[0].user_saturation;
							cfhddata->channel[2].user_saturation = cfhddata->channel[0].user_saturation;
						}
					}
					break;
				
				case TAG_BLUR_SHARPEN:
					tmp = *((float *)data);
					if(tmp < -1.0) tmp = -1.0;
					if(tmp > 1.0) tmp = 1.0;
					//cfhddata->blur_sharpen = tmp;
					{
						if(delta)
						{
							cfhddata->channel[chn].user_blur_sharpen = cfhddata->channel[0].user_blur_sharpen + tmp;	// unity at 0.0
							if(cfhddata->channel[chn].user_blur_sharpen < -1.0) cfhddata->channel[chn].user_blur_sharpen = -1.0;
							if(cfhddata->channel[chn].user_blur_sharpen > 1.0) cfhddata->channel[chn].user_blur_sharpen = 1.0;
						}
						else
						{
							cfhddata->channel[0].user_blur_sharpen = tmp;	// unity at 0.0
							cfhddata->channel[1].user_blur_sharpen = cfhddata->channel[0].user_blur_sharpen;
							cfhddata->channel[2].user_blur_sharpen = cfhddata->channel[0].user_blur_sharpen;
						}
					}
					break;
				case TAG_ASC_SATURATION:
					{
						if(delta)
						{
							cfhddata->channel[chn].user_cdl_sat = cfhddata->channel[0].user_cdl_sat + (*((float *)data));	// unity at 0.0
							if(cfhddata->channel[chn].user_cdl_sat < -1.0f) cfhddata->channel[chn].user_cdl_sat = -1.0f;
							if(cfhddata->channel[chn].user_cdl_sat > 10.0f) cfhddata->channel[chn].user_cdl_sat = 10.0f;
						}
						else
						{
							cfhddata->channel[0].user_cdl_sat = (*((float *)data) - 1.0f);	// unity at 0.0
							if(cfhddata->channel[0].user_cdl_sat < -1.0f) cfhddata->channel[0].user_cdl_sat = -1.0f;
							if(cfhddata->channel[0].user_cdl_sat > 10.0f) cfhddata->channel[0].user_cdl_sat = 10.0f;

							cfhddata->channel[1].user_cdl_sat = cfhddata->channel[0].user_cdl_sat;
							cfhddata->channel[2].user_cdl_sat = cfhddata->channel[0]. user_cdl_sat;
						}
					}
					break;
				case TAG_HIGHLIGHT_DESAT:
					{
						if(!delta)
						{
							cfhddata->channel[0].user_highlight_sat = (*((float *)data) - 1.0f);	// unity at 0.0
							if(cfhddata->channel[0].user_highlight_sat < -1.0f) cfhddata->channel[0].user_highlight_sat = -1.0f;
							if(cfhddata->channel[0].user_highlight_sat > 4.0f) cfhddata->channel[0].user_highlight_sat = 4.0f;

							cfhddata->channel[1].user_highlight_sat = cfhddata->channel[0].user_highlight_sat;
							cfhddata->channel[2].user_highlight_sat = cfhddata->channel[0].user_highlight_sat;
						}
					}
					break;
				case TAG_VIGNETTE_START:
					{
						if(!delta)
						{
							cfhddata->channel[0].user_vignette_start = (*((float *)data) - 1.0f);	// unity at 0.0
							if(cfhddata->channel[0].user_vignette_start < -1.0f) cfhddata->channel[0].user_vignette_start = -1.0f;
							if(cfhddata->channel[0].user_vignette_start > 0.0f) cfhddata->channel[0].user_vignette_start = 0.0f;

							cfhddata->channel[1].user_vignette_start = cfhddata->channel[0].user_vignette_start;
							cfhddata->channel[2].user_vignette_start = cfhddata->channel[0].user_vignette_start;
						}
					}
					break;
				case TAG_VIGNETTE_END:
					{
						if(!delta)
						{
							cfhddata->channel[0].user_vignette_end = (*((float *)data) - 1.0f);	// unity at 0.0
							if(cfhddata->channel[0].user_vignette_end < -1.0f) cfhddata->channel[0].user_vignette_end = -1.0f;
							if(cfhddata->channel[0].user_vignette_end > 1.0f) cfhddata->channel[0].user_vignette_end = 1.0f;

							cfhddata->channel[1].user_vignette_end = cfhddata->channel[0].user_vignette_end;
							cfhddata->channel[2].user_vignette_end = cfhddata->channel[0].user_vignette_end;
						}
					}
					break;
				case TAG_VIGNETTE_GAIN:
					{
						if(!delta)
						{
							cfhddata->channel[0].user_vignette_gain = (*((float *)data));	// unity at 0.0
							if(cfhddata->channel[0].user_vignette_gain < 0.0f) cfhddata->channel[0].user_vignette_gain = 0.0f;
							if(cfhddata->channel[0].user_vignette_gain > 4.0f) cfhddata->channel[0].user_vignette_gain = 4.0f;

							cfhddata->channel[1].user_vignette_gain = cfhddata->channel[0].user_vignette_gain;
							cfhddata->channel[2].user_vignette_gain = cfhddata->channel[0].user_vignette_gain;
						}
					}
					break;
				case TAG_HIGHLIGHT_POINT:
					{
						if(!delta)
						{
				//	cfhddata->channel[0].user_vignette_start = *((float *)data)-1.0;
				//	cfhddata->channel[0].user_vignette_end = *((float *)data)-0.5;
				//	cfhddata->channel[0].user_vignette_gain = 0.0;

							cfhddata->channel[0].user_highlight_point = (*((float *)data) - 1.0f);	// unity at 0.0
							if(cfhddata->channel[0].user_highlight_point < -1.0f) cfhddata->channel[0].user_highlight_point = -1.0f;
							if(cfhddata->channel[0].user_highlight_point > 0.0f) cfhddata->channel[0].user_highlight_point = 0.0f;

							cfhddata->channel[1].user_highlight_point = cfhddata->channel[0].user_highlight_point;
							cfhddata->channel[2].user_highlight_point = cfhddata->channel[0].user_highlight_point;
						}
					}
					break;
				case TAG_CONTRAST:
					{
						if(delta)
						{
							cfhddata->channel[chn].user_contrast = cfhddata->channel[0].user_contrast + (*((float *)data));	// unity at 0.0
							if(cfhddata->channel[chn].user_contrast < -1.0f) cfhddata->channel[chn].user_contrast = -1.0f;
							if(cfhddata->channel[chn].user_contrast > 10.0f) cfhddata->channel[chn].user_contrast = 10.0f;
						}
						else
						{
							cfhddata->channel[0].user_contrast = (*((float *)data) - 1.0f);	// unity at 0.0
							if(cfhddata->channel[0].user_contrast < -1.0f) cfhddata->channel[0].user_contrast = -1.0f;
							if(cfhddata->channel[0].user_contrast > 10.0f) cfhddata->channel[0].user_contrast = 10.0f;

							cfhddata->channel[1].user_contrast = cfhddata->channel[0].user_contrast;
							cfhddata->channel[2].user_contrast = cfhddata->channel[0].user_contrast;
						}
					}
					break;
						
				case TAG_EXPOSURE:
					{
						//int i;
						float *fptr = (float *)data;
						tmp = *fptr++;
						if(delta)
						{
							cfhddata->channel[chn].user_exposure = ((cfhddata->channel[0].user_exposure+1.0f) * tmp) - 1.0f;	// unity at 1.0
							if(cfhddata->channel[chn].user_exposure < -1.0f) cfhddata->channel[chn].user_exposure = -1.0f;
							if(cfhddata->channel[chn].user_exposure > 10.0f) cfhddata->channel[chn].user_exposure = 10.0f;
						}
						else
						{
							cfhddata->channel[0].user_exposure = tmp - 1.0f;	// unity at 1.0
							if(cfhddata->channel[0].user_exposure < -1.0f) cfhddata->channel[0].user_exposure = -1.0f;
							if(cfhddata->channel[0].user_exposure > 10.0f) cfhddata->channel[0].user_exposure = 10.0f;

							cfhddata->channel[1].user_exposure = cfhddata->channel[0].user_exposure;
							cfhddata->channel[2].user_exposure = cfhddata->channel[0].user_exposure;
						}
					}
					break;			
				case TAG_BASE_MATRIX:
					if(!delta)
						cfhddata->use_base_matrix = *((unsigned int *)data);
					break;
				case TAG_GHOST_BUST_LEFT:
					if(!delta)
					{
						decoder->ghost_bust_left = *((unsigned int *)data);
						if(decoder->sqrttable == NULL)
						{
	#if _ALLOCATOR
							decoder->sqrttable = (unsigned short *)Alloc(decoder->allocator, sizeof(short)*1024*1024 );
	#else
							decoder->sqrttable = (unsigned short *)MEMORY_ALLOC(sizeof(short)*1024*1024 );
	#endif
							memset(decoder->sqrttable, -1, sizeof(short)*1024*1024 );
						}
					}
					break;
				case TAG_GHOST_BUST_RIGHT:
					if(!delta)
					{
						decoder->ghost_bust_right = *((unsigned int *)data);
						if(decoder->sqrttable == NULL)
						{
	#if _ALLOCATOR
							decoder->sqrttable = (unsigned short *)Alloc(decoder->allocator, sizeof(short)*1024*1024 );
	#else
							decoder->sqrttable = (unsigned short *)MEMORY_ALLOC(sizeof(short)*1024*1024 );
	#endif
							memset(decoder->sqrttable, -1, sizeof(short)*1024*1024 );
						}
					}
					break;
					
				case TAG_MASK_LEFT:
					if(delta)
					{
					}
					else
					{
						tmp = *((float *)data);
						if(tmp < -0.2f) tmp = -0.2f;
						if(tmp > 0.2f) tmp = 0.2f;
						cfhddata->channel[0].FloatingWindowMaskL = tmp;
					}
					break;
				case TAG_MASK_RIGHT:
					if(delta)
					{
					}
					else
					{
						tmp = *((float *)data);
						if(tmp < -0.2f) tmp = -0.2f;
						if(tmp > 0.2f) tmp = 0.2f;

						if(tmp > -0.0001 && tmp < 0.0001f) 
							tmp = 0.0;
						cfhddata->channel[0].FloatingWindowMaskR = tmp;
					}
					break;
					
				case TAG_FRAME_TILT:
					if(delta)
					{
					}
					else
					{
						tmp = *((float *)data);
						if(tmp < -0.1f) tmp = -0.1f;
						if(tmp > 0.1f) tmp = 0.1f;
						cfhddata->channel[0].FrameTilt = tmp;

						cfhddata->channel[1].FrameTilt = cfhddata->channel[0].FrameTilt;
						cfhddata->channel[2].FrameTilt = cfhddata->channel[0].FrameTilt;
					}
					break;

				case TAG_HORIZONTAL_OFFSET:
					if(delta)
					{
						tmp = *((float *)data);
						cfhddata->channel[chn].HorizontalOffset = cfhddata->channel[0].HorizontalOffset + tmp;
						if(cfhddata->channel[chn].HorizontalOffset < -1.0) cfhddata->channel[chn].HorizontalOffset = -1.0;
						if(cfhddata->channel[chn].HorizontalOffset > 1.0) cfhddata->channel[chn].HorizontalOffset = 1.0;
					}
					else
					{
						tmp = *((float *)data);
						if(tmp < -1.0f) tmp = -1.0f;
						if(tmp > 1.0f) tmp = 1.0f;
						cfhddata->channel[0].HorizontalOffset = tmp;

						cfhddata->channel[1].HorizontalOffset = cfhddata->channel[0].HorizontalOffset;
						cfhddata->channel[2].HorizontalOffset = cfhddata->channel[0].HorizontalOffset;
					}
					break;
				case TAG_VERTICAL_OFFSET:
					if(delta)
					{
						tmp = *((float *)data);
						cfhddata->channel[chn].VerticalOffset = cfhddata->channel[0].VerticalOffset + tmp;
						if(cfhddata->channel[chn].VerticalOffset < -1.0) cfhddata->channel[chn].VerticalOffset = -1.0;
						if(cfhddata->channel[chn].VerticalOffset > 1.0) cfhddata->channel[chn].VerticalOffset = 1.0;
					}
					else
					{
						tmp = *((float *)data);
						if(tmp < -1.0f) tmp = -1.0f;
						if(tmp > 1.0f) tmp = 1.0f;
						cfhddata->channel[0].VerticalOffset = tmp;

						cfhddata->channel[1].VerticalOffset = cfhddata->channel[0].VerticalOffset;
						cfhddata->channel[2].VerticalOffset = cfhddata->channel[0].VerticalOffset;
					}
					break;
				case TAG_ROTATION_OFFSET:
					if(delta)
					{
						tmp = *((float *)data);
						cfhddata->channel[chn].RotationOffset = cfhddata->channel[0].RotationOffset + tmp;
						if(cfhddata->channel[chn].RotationOffset < -0.2f) cfhddata->channel[chn].RotationOffset = -0.2f;
						if(cfhddata->channel[chn].RotationOffset > 0.2f) cfhddata->channel[chn].RotationOffset = 0.2f;
					}
					else
					{
						tmp = *((float *)data);
						if(tmp < -0.2f) tmp = -0.2f;
						if(tmp > 0.2f) tmp = 0.2f;
						cfhddata->channel[0].RotationOffset = tmp;

						cfhddata->channel[1].RotationOffset = cfhddata->channel[0].RotationOffset;
						cfhddata->channel[2].RotationOffset = cfhddata->channel[0].RotationOffset;

					}
					break;
				case TAG_FRAME_ZOOM:
					{
						tmp = *((float *)data);
						if(delta)
						{
							cfhddata->channel[chn].FrameZoom = cfhddata->channel[0].FrameZoom * tmp;
						}
						else
						{
							tmp = *((float *)data);
							if(tmp < 0.10f) tmp = 0.10f;
							if(tmp > 4.0f) tmp = 4.0f;
							cfhddata->channel[0].FrameZoom = tmp;

							cfhddata->channel[1].FrameZoom = cfhddata->channel[0].FrameZoom;
							cfhddata->channel[2].FrameZoom = cfhddata->channel[0].FrameZoom;
						}
					}
					break;
				case TAG_FRAME_DIFF_ZOOM:
					{
						tmp = *((float *)data);
						if(delta)
						{
							cfhddata->channel[chn].FrameDiffZoom = cfhddata->channel[0].FrameDiffZoom * tmp;
						}
						else
						{
							tmp = *((float *)data);
							if(tmp < 0.5f) tmp = 0.5f;
							if(tmp > 2.0f) tmp = 2.0f;
							cfhddata->channel[0].FrameDiffZoom = tmp;

							cfhddata->channel[1].FrameDiffZoom = cfhddata->channel[0].FrameDiffZoom;
							cfhddata->channel[2].FrameDiffZoom = cfhddata->channel[0].FrameDiffZoom;
						}
					}
					break;
				case TAG_FRAME_KEYSTONE:
					{
						tmp = *((float *)data);
						if(delta)
						{
						//	cfhddata->channel[chn].FrameKeyStone = cfhddata->channel[0].FrameKeyStone + tmp;
						}
						else
						{
							tmp = *((float *)data);
							if(tmp < -0.2f) tmp = -0.2f;
							if(tmp > 0.2f) tmp = 0.2f;
							cfhddata->channel[0].FrameKeyStone = tmp;

							cfhddata->channel[1].FrameKeyStone = cfhddata->channel[0].FrameKeyStone;
							cfhddata->channel[2].FrameKeyStone = cfhddata->channel[0].FrameKeyStone;
						}
					}
					break;
				case TAG_AUTO_ZOOM:
					{
						size_t lsize = size;
						if (lsize > sizeof(unsigned long))
							lsize = sizeof(unsigned long);

						if (*((uint32_t *)data) == 0)
							cfhddata->FramingFlags &= ~1;
						else
							cfhddata->FramingFlags |= 1;
					}
					break;

				case TAG_FRAME_MASK:
					{
						size_t lsize = size;
						if (lsize > sizeof(Frame_Region)*2)
							lsize = sizeof(Frame_Region)*2;
						memcpy(&cfhddata->channel[0].FrameMask, data, lsize);
					}
					break;
				case TAG_FRAME_OFFSET_X:
					tmp = *((float *)data);
					if(tmp < -0.5f) tmp = -0.5f;
					if(tmp > 0.5f) tmp = 0.5f;
					cfhddata->FrameOffsetX = -tmp;
					break;
				case TAG_FRAME_OFFSET_Y:
					tmp = *((float *)data);
					if(tmp < -0.5f) tmp = -0.5f;
					if(tmp > 0.5f) tmp = 0.5f;
					cfhddata->FrameOffsetY = tmp;
					break;
				case TAG_FRAME_OFFSET_R:
					tmp = *((float *)data);
					if (tmp < -0.5f) tmp = -0.5f;
					if (tmp > 0.5f) tmp = 0.5f;
					cfhddata->FrameOffsetR = tmp;
					break;
				case TAG_FRAME_OFFSET_F:
					tmp = *((float *)data);
					if (tmp < -90.0f) tmp = -90.0f;
					if (tmp > 90.0f) tmp = 90.0f;
					cfhddata->FrameOffsetF = tmp;
					break;
				case TAG_FRAME_HSCALE:
					tmp = *((float *)data);
					//if(tmp < 0.75) tmp = 0.75;
					//if(tmp > 1.25) tmp = 1.25;
					cfhddata->FrameHScale = tmp;
					break;
				case TAG_FRAME_HDYNAMIC:
					tmp = *((float *)data);
					if(tmp < 0.5f) tmp = 0.5f;
					if(tmp > 1.5f) tmp = 1.5f;
					cfhddata->FrameHDynamic = tmp;
					break;
				case TAG_FRAME_DYNCENTER:
					tmp = *((float *)data);
					if(tmp < 0.0f) tmp = 0.0f;
					if(tmp > 1.0f) tmp = 1.0f;
					cfhddata->FrameHDynCenter = tmp;
					break;
				case TAG_FRAME_DYNWIDTH:
					tmp = *((float *)data);
					if(tmp < 0.0f) tmp = 0.0f;
					if(tmp > 1.0f) tmp = 1.0f;
					cfhddata->FrameHDynWidth = tmp;
					break;

				case TAG_SPLIT_POS:
					tmp = *((float *)data);
					if(tmp < 0.0f) tmp = 0.0f;
					if(tmp > 1.0f) tmp = 1.0f;
					cfhddata->split_CC_position = tmp;
					break;

	/*			case TAG_HISTOGRAM:
					{
						int lsize = size;
						if(lsize > sizeof(uint32_t))
							lsize = sizeof(uint32_t);

						if(*((uint32_t *)data) == 0)
							cfhddata->BurninFlags &= ~2;
						else
							cfhddata->BurninFlags |= 2;
					}
					break;
				case TAG_WAVEFORM:
					{
						int lsize = size;
						if(lsize > sizeof(uint32_t))
							lsize = sizeof(uint32_t);

						if(*((uint32_t *)data) == 0)
							cfhddata->BurninFlags &= ~4;
						else
							cfhddata->BurninFlags |= 4;
					}
					break;
				case TAG_VECTORSCOPE:
					{
						int lsize = size;
						if(lsize > sizeof(uint32_t))
							lsize = sizeof(uint32_t);

						if(*((uint32_t *)data) == 0)
							cfhddata->BurninFlags &= ~8;
						else
							cfhddata->BurninFlags |= 8;
					}
					break;
	*/
				case TAG_DISPLAY_METADATA:
					{
						int i,foundID = 0;
						char CurrentID[64];
						char LoadedID[64];
						GetCurrentID(decoder, &ptr[8], (unsigned int)size, CurrentID, sizeof(CurrentID));

						if(0 == strncmp(CurrentID, "Tool:",5))
						{
							if(0 == strcmp(CurrentID, "Tool:Histogram"))
								cfhddata->ComputeFlags |= 2;
							if(0 == strcmp(CurrentID, "Tool:Waveform"))
								cfhddata->ComputeFlags |= 4;
							if(0 == strcmp(CurrentID, "Tool:Vectorscope") || 0 == strcmp(CurrentID, "Tool:Vectorscope2"))
								cfhddata->ComputeFlags |= 8;
							if(0 == strncmp(CurrentID, "Tool:Grid", 9))
								cfhddata->ComputeFlags |= 16;
						}

						for(i=0;i<decoder->drawmetadataobjects;i++)
						{
							GetCurrentID(decoder, decoder->dmo[i], decoder->dmo_size[i], LoadedID, sizeof(LoadedID));
							if(0 == strcmp(LoadedID, CurrentID))
							{
								foundID = 1;
								break;
							}
						}
						if(!foundID)
						{
							decoder->dmo[decoder->drawmetadataobjects] = &ptr[8];
							decoder->dmo_size[decoder->drawmetadataobjects] = (unsigned int)size;
							decoder->drawmetadataobjects++;
							//skip the data within (process later)
							memcpy(&decoder->MDPcurrent, &decoder->MDPdefault, sizeof(MDParams));
						}
					}
					break;

				case TAG_DISPLAY_ACTION_SAFE:
					memcpy(&decoder->ActiveSafe[0], data, size); 
					break;
				case TAG_DISPLAY_TITLE_SAFE:
					memcpy(&decoder->TitleSafe[0], data, size); 
					break;
				case TAG_DISPLAY_OVERLAY_SAFE:
					memcpy(&decoder->OverlaySafe[0], data, size); 
					break;

				case TAG_DISPLAY_SCRIPT:	
				case TAG_DISPLAY_SCRIPT_FILE:
					break;
				case TAG_DISPLAY_TAG:
					decoder->MDPdefault.tag = *((uint32_t *)data);
					decoder->MDPdefault.freeform[0] = 0;
					break;
				case TAG_DISPLAY_FREEFORM:
					copysize = size;
					if(copysize >= FREEFORM_STR_MAXSIZE) copysize = FREEFORM_STR_MAXSIZE-1;
#ifdef _WIN32
					strncpy_s(decoder->MDPdefault.freeform, sizeof(decoder->MDPdefault.freeform), (char *)data, copysize);
#else
					strncpy(decoder->MDPdefault.freeform, (char *)data, copysize);
#endif
					decoder->MDPdefault.freeform[copysize] = '\0';
					decoder->MDPdefault.tag = 0;
					break;
				case TAG_DISPLAY_FONT:
					copysize = size;
					if(copysize >= FONTNAME_STR_MAXSIZE) copysize = FONTNAME_STR_MAXSIZE-1;

#ifdef _WIN32
					strncpy_s(decoder->MDPdefault.font, sizeof(decoder->MDPdefault.font), (char *)data, copysize);
#else
					strncpy(decoder->MDPdefault.font, (char *)data, copysize);
#endif
					decoder->MDPdefault.font[copysize] = 0;
					break;
				case TAG_DISPLAY_FONTSIZE:
					decoder->MDPdefault.fontsize = *((float *)data);
					break;
				case TAG_DISPLAY_JUSTIFY:
					decoder->MDPdefault.justication = *((uint32_t *)data);
					break;
				case TAG_DISPLAY_FCOLOR:
					memcpy(&decoder->MDPdefault.fcolor[0], data, sizeof(float)*4); 
					break;
				case TAG_DISPLAY_BCOLOR:
					memcpy(&decoder->MDPdefault.bcolor[0], data, sizeof(float)*4); 
					break;
				case TAG_DISPLAY_SCOLOR:
					memcpy(&decoder->MDPdefault.scolor[0], data, sizeof(float)*4); 
					break;
				case TAG_DISPLAY_STROKE_WIDTH:
					decoder->MDPdefault.stroke_width = *((float *)data);
					break;
				case TAG_DISPLAY_XPOS:
					decoder->MDPdefault.xypos[0][0] = *((float *)data);
					break;
				case TAG_DISPLAY_YPOS:
					decoder->MDPdefault.xypos[0][1] = *((float *)data);
					break;
				case TAG_DISPLAY_XYPOS:
					memcpy(&decoder->MDPdefault.xypos[0][0], data, sizeof(float)*2); 
					break;
				case TAG_DISPLAY_FORMAT:
					copysize = size;
					if(copysize >= FORMAT_STR_MAXSIZE) copysize = FORMAT_STR_MAXSIZE-1;
#ifdef _WIN32
					strncpy_s(decoder->MDPdefault.format_str, sizeof(decoder->MDPdefault.format_str), (char *)data, copysize);
#else
					strncpy(decoder->MDPdefault.format_str, (char *)data, copysize);
#endif
					decoder->MDPdefault.format_str[copysize] = '\0';
					break;
				case TAG_DISPLAY_PNG_PATH:
					copysize = size;
					if(copysize >= PNG_PATH_MAXSIZE) copysize = PNG_PATH_MAXSIZE-1;
#ifdef _WIN32
					strncpy_s(decoder->MDPdefault.png_path, sizeof(decoder->MDPdefault.png_path), (char *)data, copysize);
#else
					strncpy(decoder->MDPdefault.png_path, (char *)data, copysize);
#endif
					decoder->MDPdefault.png_path[copysize] = '\0';
					break;
				case TAG_DISPLAY_PNG_SIZE:
					memcpy(&decoder->MDPdefault.object_scale[0], data, sizeof(float)*2); 
					break;
				case TAG_DISPLAY_PARALLAX:
					decoder->MDPdefault.parallax = *((int32_t *)data);
					break;

				case TAG_CONTROL_POINT:
					NewControlPoint(decoder, &ptr[8], (int)size, delta, priority);
					break;

				case TAG_EYE_DELTA_2:
					localpri++;
				case TAG_EYE_DELTA_1:
					localpri++;

					decoder->hasFileDB[localpri] = 2; // Indicate the COL1 and COL2 where retrieved from the sample/colr, no need to read files *.col1|*.col2,

					if(priority == METADATA_PRIORITY_FRAME || priority == METADATA_PRIORITY_DATABASE || priority == METADATA_PRIORITY_OVERRIDE)
					{
						if(size > decoder->DataBasesAllocSize[localpri] || decoder->DataBases[localpri] == NULL)
						{
							if(decoder->DataBases[localpri])
							{		
								#if _ALLOCATOR
								Free(decoder->allocator, decoder->DataBases[localpri]);
								#else
								MEMORY_FREE(decoder->DataBases[localpri]);
								#endif
								decoder->DataBases[localpri] = NULL;
							}
							decoder->DataBasesAllocSize[localpri] = (size + 511) & ~0xff;
							#if _ALLOCATOR
							decoder->DataBases[localpri]= (unsigned char *)Alloc(decoder->allocator, decoder->DataBasesAllocSize[localpri]);
							#else
							decoder->DataBases[localpri] = (unsigned char *)MEMORY_ALLOC(decoder->DataBasesAllocSize[localpri]);
							#endif

						}

						if(size && size <= decoder->DataBasesAllocSize[localpri] && decoder->DataBases[localpri])
						{
							memcpy(decoder->DataBases[localpri], data, size);
							decoder->DataBasesSize[localpri] = (unsigned int)size;
						}
						else
						{
							decoder->DataBasesSize[localpri] = 0;
						}
					}
					localpri = priority;
					break;
				}
			}

			if(!terminate)
			{
				ptr += (8 + size + 3) & 0xfffffc;
				pos += (8 + size + 3) & 0xfffffc;
			}
		}

		if(cfhddata->FramingFlags & 1)
		{
			int i;//both,left,rght
			int w=16,h=9;
			float denom, autozoom,horizZoom1,horizZoom2,verticalZoom;

			GetDisplayAspectRatio(decoder, &w, &h); 

			for(i=0;i<3; i++)
			{				
				horizZoom1 = horizZoom2 = fabsf(cfhddata->channel[i].HorizontalOffset) + fabsf(cfhddata->channel[i].RotationOffset * 0.5f);
				verticalZoom = fabsf(cfhddata->channel[i].VerticalOffset) + fabsf(cfhddata->channel[i].RotationOffset * (float)(w*w) / (float)(h*h) * 0.5f); // 16/9
				verticalZoom += fabsf(cfhddata->channel[i].FrameKeyStone/4.0f);

				horizZoom1 += cfhddata->channel[0].FrameTilt*0.5f;
				horizZoom2 -= cfhddata->channel[0].FrameTilt*0.5f;
				
				denom = (1.0f - verticalZoom*2);
				if(denom > (1.0f - horizZoom1*2))
					denom = (1.0f - horizZoom1*2);
				if(denom > (1.0f - horizZoom2*2))
					denom = (1.0f - horizZoom2*2);

				if(denom < 0.25f) denom = 0.25f;

				autozoom = 1.0f / denom;
				if(autozoom > 4.0f)
					autozoom = 4.0f;

				if(i<2)
					cfhddata->channel[i].FrameAutoZoom = autozoom / cfhddata->channel[1].FrameDiffZoom;
				else
					cfhddata->channel[i].FrameAutoZoom = autozoom * cfhddata->channel[2].FrameDiffZoom;
			}

			if(cfhddata->channel[0].FrameAutoZoom < cfhddata->channel[1].FrameAutoZoom)
				cfhddata->channel[0].FrameAutoZoom = cfhddata->channel[1].FrameAutoZoom;
			if(cfhddata->channel[0].FrameAutoZoom < cfhddata->channel[2].FrameAutoZoom)
				cfhddata->channel[0].FrameAutoZoom = cfhddata->channel[2].FrameAutoZoom;
		}
		else
		{
			cfhddata->channel[0].FrameAutoZoom = 1.0f;
			cfhddata->channel[1].FrameAutoZoom = 1.0f / cfhddata->channel[1].FrameDiffZoom;
			cfhddata->channel[2].FrameAutoZoom = 1.0f * cfhddata->channel[2].FrameDiffZoom;
		}
	}
}



void GetCurrentID(DECODER *decoder, unsigned char *ptr, unsigned int len, char *id, unsigned int id_size)
{

	if(decoder && ptr && len && id) // overrides form database or external control
	{
		//int inframe = 0, duration = 0;
		//unsigned char *base = ptr;
		void *data;
		unsigned char type;
		unsigned int pos = 0;
		unsigned int size;
		//unsigned int copysize;
		unsigned int tag;

		while(pos+12 <= len)
		{
			data = (void *)&ptr[8];
			type = ptr[7];
			size = ptr[4] + (ptr[5]<<8) + (ptr[6]<<16);
			tag = MAKETAG(ptr[0],ptr[1],ptr[2],ptr[3]);

			switch(tag)
			{
			default:
				break;
			case TAG_DISPLAY_TAG:
				tag =  *((uint32_t *)data);
				id[0] = 'T';
				id[1] = 'A';
				id[2] = 'G';
				id[3] = ':';
				id[4] = tag & 0xff;
				id[5] = (tag>>8) & 0xff;
				id[6] = (tag>>16) & 0xff;
				id[7] = (tag>>24) & 0xff;
				id[8] = 0;
				break;
			case TAG_DISPLAY_FREEFORM:
				if(size > id_size-1) size = id_size-1;
#ifdef _WIN32
				strncpy_s(id, id_size,(char *)data, size);
#else
				strncpy(id, (char *)data, size);
#endif
				id[size] = 0;
				break;
			}

			ptr += (8 + size + 3) & 0xfffffc;
			pos += (8 + size + 3) & 0xfffffc;
		}
	}
}
