/*! @file AVIExtendedHeader.h

*  @brief Curve definitions and internal active metadata parameters.
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

#ifndef AVIEH
#define AVIEH

#include "CFHDMetadataTags.h"
#include <math.h>
#include <stdint.h>

#define CFHDDATA_MAGIC_NUMBER	0x12345678
#define	CFHDDATA_VERSION		7

#define MAX_PIXEL_DEFECTS		8
//#define MAX_METADATA_HEADER		3072		// 256 byte more prevents mediaplayer/windows from working (can't find the codec.)

typedef struct PixelDefect
{
	uint16_t xpos;
	uint16_t ypos;

} DEFECT;

typedef struct myGUID
{
	uint32_t Data1;
	uint16_t Data2;
	uint16_t Data3;
	uint8_t Data4[8];
} myGUID;

#define RATIONAL(a,b)	(((a) << 16) | (b))

// Aspect ratios for pictures and pixels
enum
{
	ASPECT_RATIO_UNKNOWN = 0,
	ASPECT_RATIO_SQUARE	 = RATIONAL(1, 1),	// 1920x1080 1290x720 as 16x9 etc
	ASPECT_RATIO_4_3     = RATIONAL(4, 3),	// 1440x1080 HD 16x9
	ASPECT_RATIO_9_10    = RATIONAL(9, 10),	// 720x486 4x3 NTSC
	ASPECT_RATIO_2_1     = RATIONAL(2, 1),	// anamorphic film
	ASPECT_RATIO_3_2     = RATIONAL(3, 2),	// 1280x1080 DVCPRO-HD
	ASPECT_RATIO_6_5     = RATIONAL(6, 5),	// 720x486 16x9 NTSC
	ASPECT_RATIO_16_15   = RATIONAL(16, 15),// 720x576 4x3 PAL
	ASPECT_RATIO_64_45   = RATIONAL(64, 45),// 720x576 16x9 PAL

};

// The pixel aspect ratio is a rational number packed in a 32-bit word
typedef uint32_t ASPECT_RATIO;

// Other names
typedef ASPECT_RATIO PIXEL_ASPECT_RATIO;
typedef ASPECT_RATIO PICTURE_ASPECT_RATIO;

// Short name
typedef ASPECT_RATIO PAR;

// Functions for getting the numerator and denominator of an aspect ratio 
static __inline uint16_t AspectRatioX(ASPECT_RATIO aspectRatio)
{
	return (uint16_t)(aspectRatio >> 16);
}

static __inline uint16_t AspectRatioY(ASPECT_RATIO aspectRatio)
{
	return (uint16_t)(aspectRatio & 0xFFFF);
}


#define CURVE_TYPE_UNDEF	0
#define CURVE_TYPE_LOG		1
#define CURVE_TYPE_GAMMA	2
#define CURVE_TYPE_ITU709	3		// values for b and c are unused (can be 100/45 = 2.2222)
									//encode if(R<0.018) R'=4.5*R; else R'=(1.099R^0.45)-0.099;  
									//decode if(R'<0.0812) R=R'/4.5; else R=((R'+0.099)/1.099)^(1/0.45);
#define CURVE_TYPE_LINEAR	4
#define CURVE_TYPE_CINEON	5		//black at 95 and white 685, b and c are the gamma curve (ie. 17/10 = 1.7)
#define CURVE_TYPE_PARA		6		//b and c are the gain and power parameters  (1.0-(float)pow((1.0-(double)i),(1.0/((double)power*256.0)))*gain;
#define CURVE_TYPE_CINE985	7		//black at 95 and white 685, b and c are the gamma curve (ie. 17/10 = 1.7)
#define CURVE_TYPE_CSTYLE	8  		//Model close to Technicolor CineStyle(TM) for Canon DSLRs
#define CURVE_TYPE_SLOG		9  		//Sony's S-Log
#define CURVE_TYPE_LOGC	   10  		//Arri Alexa's Log-C

#define CURVE_TYPE_MASK		0x00ff	//AND'd with the above types.
#define CURVE_TYPE_NEGATIVE	0x8000	//Flags or'd with the above types.
#define CURVE_TYPE_EXTENDED	0x4000	//Use the b and c, fields read as a single 16-bit integer for the log base (range 0 to 65535)

	
#define CURVE_TYPE(a,b,c)	(((a) << 16) | (b<<8) | c)		// 0xaaaabbcc  a - type, b - value numerator, c - value denominator
#define CURVE_TYPE_EXT(a,b)	(((a|CURVE_TYPE_EXTENDED) << 16) | (b))		// 0xaaaabbcc  a - type, b - base

#if 0

#define CURVE_LOG2LIN(i,b)		((pow((double)(b), (double)(i))-1.0)/((double)(b) - 1.0))			// i = input float 0.0 to 1.0, b = log base
#define CURVE_LIN2LOG(i,b)		((i)>=0.0?log10((i)*((b)-1.0)+1.0)/log10((b)):-log10(-(i)*((b)-1.0)+1.0)/log10((b)))

#define CURVE_GAM2LIN(i,p)		((i)>=0.0?pow((double)(i),(double)(p)):-pow(-(double)(i),(double)(p)))
#define CURVE_LIN2GAM(i,p)		((i)>=0.0?pow((double)(i),1.0/(double)(p)):-pow(-(double)(i),1.0/(double)(p)))

#else

#define CURVE_LOG2LIN(i,b)		log2lin((i),(b))
#define CURVE_LIN2LOG(i,b)		lin2log((i),(b))

#define CURVE_GAM2LIN(i,p)		gam2lin((i),(p))
#define CURVE_LIN2GAM(i,p)		lin2gam((i),(p))

#define CURVE_CINEON2LIN(i,p)	cineon2lin((i),(p))
#define CURVE_LIN2CINEON(i,p)	lin2cineon((i),(p))

#define CURVE_CINE9852LIN(i,p)	cine9852lin((i),(p))
#define CURVE_LIN2CINE985(i,p)	lin2cine985((i),(p))

#define CURVE_LIN2PARA(i,g,p)	lin2para((i),(g),(p))
#define CURVE_PARA2LIN(i,g,p)	para2lin((i),(g),(p))

#define CURVE_CSTYLE2LIN(i,p)	cstyle2lin((i),(p))
#define CURVE_LIN2CSTYLE(i,p)	lin2cstyle((i),(p))

#define CURVE_SLOG2LIN(i)		slog2lin(i)
#define CURVE_LIN2SLOG(i)		lin2slog(i)

#define CURVE_LOGC2LIN(i)		logc2lin(i)
#define CURVE_LIN2LOGC(i)		lin2logc(i)


static __inline float log2lin(float i, float b)
{
	return (float)((pow(b, i) - 1.0)/(b - 1.0));
}

static __inline float lin2log(float i, float b)
{
	return (float)((i >= 0.0) ? log10(i * (b - 1.0) + 1.0)/log10(b) : -log10(-i * (b - 1.0) + 1.0)/log10(b));
}

//static __inline float gam2lin(float i, float p)
static __inline float gam2lin(double i, double p)
{
	//return (float)((i >= 0.0) ? pow(i, p) : -pow(-i, p));

	// New gamma curve has a linear extension in the negative values
	return (float)((i >= 0.0) ? pow(i, p) : i/(100.0*pow(0.01, (1.0/p))));
}

//static __inline float lin2gam(float i, float p)
static __inline float lin2gam(double i, double p)
{
	//float exponent = (float)(1.0/p);
	double exponent = (float)(1.0/p);
//	return (float)((i >= 0.0) ? pow(i, exponent) : -pow(-i, exponent));

	// New gamma curve has a linear extension in the negative values
	return (float)((i >= 0.0) ? pow(i, exponent) : i*100.0*pow(0.01, exponent));
}

static __inline float calc_contrast(double i, double cntrst)
{
	//TODO: Eliminate compiler warnings about conversion of double to float
	double p = cntrst >= 1.0 ? (cntrst-1.0)*3.0 + 1.0 : cntrst; //3x to boost the contrast effect
	float b6 = (float)pow(0.5, p);
	float b7 = (float)(0.5/b6);
	float b8 = (float)(p>1.0 ? 1.0-1.0/p : 1.0);

	if(i<0.0)
		return (float)(i*100.0*((0.01*(1-b8))+b8*(pow(0.01,p)*b7)));
	else if(i<0.5)
		return (float)((i*(1-b8))+b8*(pow(i,p)*b7));
	else if(i<=1.0)
		return (float)((i*(1-b8))+b8*(1-pow(1-i,p)*b7));
	else 
		return (float)((1+(i-1)*100.0*(1-((0.99*(1-b8))+b8*(1-pow(0.01,p)*b7)))));
}

//DAN20080904 -- changed the Display gamma math below.
static __inline float lin2cineon(double i, float p) 
{
	double black = (pow(10.0, (95.0/1023.0 - 685.0/1023.0)*1023.0 * (p/1.7) * 0.002 / 0.6));

	i += black;
	if(i<0.0001) i = 0.0001;

	//return 685.0/1023.0 + (float)(log10(i) / ((p/1.7) * 0.002 / 0.6))/1023.0;
	return (float)(685.0/1023.0 + (float)(log10(i) / ((p/1.7) * 0.002 / 0.6))/1023.0);
}

static __inline float cineon2lin(double i, float p) 
{
	double black = (pow(10.0, (95.0/1023.0 - 685.0/1023.0)*1023.0 * (p/1.7) * 0.002 / 0.6));

	if(i < 0.0) i = 0.0;
	
	return (float)(pow(10.0, (i - 685.0/1023.0)*1023.0 *(p/1.7) * 0.002 / 0.6) - black);
}

//DAN20080904 -- changed the Display gamma math below.
static __inline float lin2cine985(double i, float p) 
{
	double black = (pow(10.0, (95.0/1023.0 - 985.0/1023.0)*1023.0 * (p/1.7) * 0.002 / 0.6));

	i += black;
	if(i<0.0001) i = 0.0001;

	return (float)(985.0/1023.0 + (float)(log10(i) / ((p/1.7) * 0.002 / 0.6))/1023.0);
}

static __inline float cine9852lin(double i, float p) 
{
	if(i < 0.0) i = 0.0;
	
	return (float)(pow(10.0, (i - 985.0/1023.0)*1023.0 *(p/1.7) * 0.002 / 0.6));
}

        
//DAN20081011 - Support for Redspace (gain 202, power 4)
static __inline float para2lin(float i, int gain, int power)
{
	// = (1-((1-i)^(1/(power*256))))*gain
	if(i>=1.0)
		return (float)(1.0+(float)pow((double)i-1.0,(1.0/((double)power*256.0))))*(float)gain;
	else
		return (float)(1.0-(float)pow((1.0-(double)i),(1.0/((double)power*256.0))))*(float)gain;
}

static __inline float lin2para(float i, int gain, int power)
{
	// = (1-((1-i/gain)^(power*256)))
	return (float)(1.0-(float)(pow((double)(1.0-i/(float)gain), (double)(power*256))));
}



static __inline float cstyle2lin(float i, int flavor)
{
	int maxpoint = 20;
	static float points[] = {
		0.000f, 0.000f,	//0
		0.050f, 0.001f, 
		0.100f, 0.002f, 
		0.150f, 0.004f, 
		0.200f, 0.010f,
		0.251f, 0.022f,	//5
		0.302f, 0.040f,
		0.349f, 0.070f,
		0.400f, 0.110f,
		0.451f, 0.160f,
		0.502f, 0.240f,	//10
		0.557f, 0.340f,
		0.698f, 0.657f,
		0.741f, 0.751f,
		0.804f, 0.852f,
		0.839f, 0.900f,	//15
		0.886f, 0.940f,
		0.916f, 0.960f,
		0.950f, 0.980f,
		0.975f, 0.990f,
		1.000f, 1.000f,	//20		
	};

	if(i < points[0])
	{
		int pos = 0;
		float mix;
		
		mix = (i - points[pos*2]) / (points[pos*2+2] - points[pos*2]);
			
		return ((points[pos*2+3] - points[pos*2+1]) * mix) + points[pos*2+1];
	}
	else
	{
		int pos = 0;
		float mix;
		while((points[pos*2] > i || i > points[pos*2+2]) && pos < maxpoint-1) pos++;
		
		mix = (i - points[pos*2]) / (points[pos*2+2] - points[pos*2]);
			
		return ((points[pos*2+3] - points[pos*2+1]) * mix) + points[pos*2+1];
	}
}

static __inline float lin2cstyle(float i, int flavor)
{
	int maxpoint = 20;
	static float points[] = {
		0.000f, 0.000f,	//0
		0.001f, 0.050f, 
		0.002f ,0.100f, 
		0.004f, 0.150f, 
		0.010f, 0.200f,
		0.022f, 0.251f,	//5
		0.040f, 0.302f,
		0.070f, 0.349f,
		0.110f, 0.400f,
		0.160f, 0.451f,
		0.240f, 0.502f,	//10
		0.340f, 0.557f,
		0.657f, 0.698f,
		0.751f, 0.741f,
		0.852f, 0.804f,
		0.900f, 0.839f,	//15
		0.940f, 0.886f,
		0.960f, 0.916f,
		0.980f, 0.950f,
		0.990f, 0.975f,
		1.000f, 1.000f,	//20		
	};
		
	if(i < points[0])
	{
		int pos = 0;
		float mix;
		
		mix = (i - points[pos*2]) / (points[pos*2+2] - points[pos*2]);
			
		return ((points[pos*2+3] - points[pos*2+1]) * mix) + points[pos*2+1];
	}
	else
	{
		int pos = 0;
		float mix;
		while((points[pos*2] > i || i > points[pos*2+2]) && pos < maxpoint-1) pos++;
		
		mix = (i - points[pos*2]) / (points[pos*2+2] - points[pos*2]);
			
		return ((points[pos*2+3] - points[pos*2+1]) * mix) + points[pos*2+1];
	}
}

static __inline float slog2lin(float x)
{
	//S-Log to Linear                                             
	//Y = Power(10.0, ((i - 0.616596 - 0.03) / 0.432699)) - 0.037584
	return (float)(pow(10.0, ((x - 0.616596 - 0.03) / 0.432699)) - 0.037584);
}


static __inline float lin2slog(float x)
{
	//Linear to S-log (input i is 0 to 1, supports up to 10.0)    
	//y = (0.432699 * Log(i + 0.037584) + 0.616596) + 0.03          
	return (float)((0.432699 * log10(x + 0.037584) + 0.616596) + 0.03);
}


#define LOGCOFFSET		0.00937677 
static __inline float logc2lin(float x)
{
	//Alexa LogC to Linear                                             
	if(x > 0.1496582)
		return (float)(pow(10.0, (x - 0.385537) / 0.2471896) * 0.18 - LOGCOFFSET);
	else
		return (float)((x / 0.9661776 - 0.04378604) * 0.18 - LOGCOFFSET);
}


static __inline float lin2logc(float x)
{
	//Alexa Linear to LogC     
	if(x > 0.02 - LOGCOFFSET)
		return (float)(((log10((x + LOGCOFFSET) / 0.18)) * 0.2471896) + 0.385537);
	else 
		return (float)((((x + LOGCOFFSET) / 0.18) + 0.04378604) * 0.9661776);
}

#endif

// Enumerated values for the curves that are applied to the input pixels during encoding
typedef enum encode_curve
{
	CURVE_LOG_90 = CURVE_TYPE(CURVE_TYPE_LOG,90,1),
	CURVE_GAMMA_2pt2 = CURVE_TYPE(CURVE_TYPE_GAMMA,22,10),
	CURVE_GAMMA_709 = CURVE_TYPE(CURVE_TYPE_ITU709,100,45),
	CURVE_CINEON_1pt7 = CURVE_TYPE(CURVE_TYPE_CINEON,17,10),
	CURVE_CINE985_1pt7 = CURVE_TYPE(CURVE_TYPE_CINE985,17,10),
	CURVE_CINEON_1pt0 = CURVE_TYPE(CURVE_TYPE_CINEON,1,1),
	CURVE_LINEAR = CURVE_TYPE(CURVE_TYPE_LINEAR,1,1),
	CURVE_REDSPACE = CURVE_TYPE(CURVE_TYPE_PARA,202,4),  
	CURVE_DEFAULT = CURVE_LOG_90,

	// Longer names using the enumerated type as a prefix
	ENCODE_CURVE_LOG_90 = CURVE_TYPE(CURVE_TYPE_LOG,90,1),
	ENCODE_CURVE_GAMMA_2_2 = CURVE_TYPE(CURVE_TYPE_GAMMA,22,10),
	ENCODE_CURVE_GAMMA_709 = CURVE_TYPE(CURVE_TYPE_ITU709,100,45),
	ENCODE_CURVE_CINEON_1_7 = CURVE_TYPE(CURVE_TYPE_CINEON,17,10),
	ENCODE_CURVE_CINE985_1_7 = CURVE_TYPE(CURVE_TYPE_CINE985,17,10),
	ENCODE_CURVE_CINEON_1_0 = CURVE_TYPE(CURVE_TYPE_CINEON,1,1),
	ENCODE_CURVE_LINEAR = CURVE_TYPE(CURVE_TYPE_LINEAR,1,1),
	ENCODE_CURVE_REDSPACE = CURVE_TYPE(CURVE_TYPE_PARA,202,4),  
	ENCODE_CURVE_DEFAULT = CURVE_LOG_90,

} ENCODE_CURVE;


typedef struct   AVIFileMetaData2Tag
{	
	char			orgtime[16];
	char			alttime[16];
	char			orgreel[40];
	char			altreel[40];
	char			logcomment[256];
} AVIFileMetaData2;


#define LOOK_NAME_MAX	40

typedef struct   CFLOOK_HEADER
{	
	unsigned int	CFLK_ID;
	unsigned int	version;
	unsigned int	hdrsize;
	unsigned int	lutsize;
	unsigned int	input_curve;
	unsigned int	output_curve;
	char			displayname[LOOK_NAME_MAX]; // added in version 2
} CFLook_Header;

#define CFLOOK_VERSION		2

typedef struct frame_region
{	
	float		topLftX;		// frame is 0.0 to 1.0
	float		topLftY;		// frame is 0.0 to 1.0
	float		topRgtX;		// frame is 0.0 to 1.0
	float		topRgtY;		// frame is 0.0 to 1.0
	float		botRgtX;		// frame is 0.0 to 1.0
	float		botRgtY;		// frame is 0.0 to 1.0
	float		botLftX;		// frame is 0.0 to 1.0
	float		botLftY;		// frame is 0.0 to 1.0
} Frame_Region;

typedef Frame_Region FRAME_REGION;

// Some compilers warn if not all of the fields are initialized
#define FRAME_REGION_INITIALIZER {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}


typedef struct channel_data
{
	float user_contrast;		// -1.0 to 3.0+, 0.0 unity   real range 0 to 4
	float user_saturation;		// -1.0 to 3.0+, 0.0 unity   real range 0 to 4
	float user_highlight_sat;	// -1.0 to 0.0+, 0.0 unity   real range 0 to 1
	float user_highlight_point;	// -1.0 to 0.0+, 0.0 unity   real range 0 to 11
	float user_vignette_start;	// -1.0 to 0.0+, 0.0 unity   real range 0 to 1
	float user_vignette_end;	// -1.0 to 0.0+, 0.0 unity   real range 0 to 2
	float user_vignette_gain;	// 0.0 unity   real range 0 to 8
	float user_exposure;		// -1.0 to 7.0+, 0.0 unity   real range 0 to 8
	float user_rgb_lift[3];		// -1.0 to 1.0, 0.0 unity black offsets   
	float user_rgb_gamma[3];		// if 0.0  then no gamma tweaks -- not a camera control used in post.	
	float user_rgb_gain[3];		// -1.0 to 3.0+, 0.0 unity RGB gains (upon the current matrix)   real range 0 to 4
	float white_balance[3];
	float user_cdl_sat;			// -1.0 to 3.0+, 0.0 unity   real range 0 to 4
	float user_blur_sharpen;	// 0.0 to 1.0, 0.0 unity -- 1.0 sharp

	float FrameZoom;
	float FrameDiffZoom;
	float FrameAutoZoom;
	float HorizontalOffset;			// 0.0 centre, -1.0 far left, 1.0 far right 
	float VerticalOffset;			// 0.0 centre, -1.0 far up, 1.0 far down 
	float RotationOffset;			// 0.0 centre, -0.1 anti-clockwize, 0.1 clockwize 
	float FrameKeyStone;
	float FloatingWindowMaskL;
	float FloatingWindowMaskR;
	float FrameTilt;
	Frame_Region FrameMask;	// Two channel for separate stereo masks.

} ChannelData;

typedef enum BayerFormat
{
	BAYER_FORMAT_DEFAULT = 0,		// Defaults to red-green
	BAYER_FORMAT_GREEN_RED = 1,
	BAYER_FORMAT_GREEN_BLUE = 2,
	BAYER_FORMAT_BLUE_GREEN = 3,
	BAYER_FORMAT_RED_GREEN = BAYER_FORMAT_DEFAULT,

} BayerFormat;

typedef struct tagCFHDDATA_NEW
{
	uint32_t MagicNumber;
	uint32_t size;					//size of this structure
	uint32_t version;				//version of this structure (originally 0)
	uint32_t cfhd_subtype;			//0-normal (YUY2), 1-Bayer, 2-RGB native, 3-RGBA native
	uint32_t num_channels;
	uint32_t channel_depth;

	// version 2 or greater
	float custom_colormatrix[3][4];		// active color matrix

	// version 3 or greater
	float orig_colormatrix[3][4];		// original color matrix camera settings.
	
	
	// for backward compatibility we need 516 bytes between here and PIXEL_ASPECT_RATIO]
	// version 5 or greater
	AVIFileMetaData2 FileTimecodeData;	// 368 bytes
	uint32_t custom_data_offset;	// offset to the union from the set of CFHDDATA_NEW;
	
	float default_white_balance[4];		// obsolete -- in camera WB, channel gains R,G,G,B
	float user_white_balance[4];		// obsolete -- post WB, channel gains R,G,G,B
	
	char look_filename[40];				// ####filename####.look (no path)
	char look_export_path[260];			// ####filename####.look
	uint32_t export_look;			// 
	uint32_t default_look_CRC;		// to generate 01AB34CD.look
	uint32_t user_look_CRC;		// to generate 01AB34CD.look
	
	uint32_t encode_curve;			// CURVE_LOG_90, CURVE_GAMMA_2pt2, etc. if zero assume CURVE_DEFAULT = CURVE_LOG_90.
	uint32_t decode_curve;			// if zero play data as 1:1 -- no curve changes, if differenct than encode_curve then decode2curve(encode2linear(in)).
	uint32_t PrimariesUseDecodeCurve;	// 0 - original, 1 - new processing default

	uint32_t process_path_flags;	// if zero assume look has color matrix pre-applied, otherwise PROCRESSING_ACTIVE + ??? indicates the processing elements 
	
	DEFECT badpixels[MAX_PIXEL_DEFECTS];// up to 8 defects supported per AVI
	
	uint16_t take_number;	//auto incremented
	uint16_t shot_number;
	uint16_t scene_number;
	uint8_t old_project_number;
	uint8_t camera_number;
	
	// automatically extracted from camera/recorder's system clock.
	uint16_t time_year;  //0-2xxx
	uint8_t time_month;  //1-12
	uint8_t time_day;    //1-31
	uint8_t time_hour;   //0-23
	uint8_t time_minute; //0-59
	uint8_t time_second; //0-59
	
	// Bayer pixel foramt (see BayerFormat enumeration above)
	uint8_t bayer_format;	// 0 - Red-Green,  1 - Green-Red, 2 - Green-Blue, 3 - Blue-Green, 4 - Red-Green (set, whereas 0 is just a default)

	
	// automatically extracted from the encoder license
	uint32_t capture_fingerprint;  //Hardware figureprint

	// version 4 or greater
	PIXEL_ASPECT_RATIO pixel_aspect_ratio;

	// ... 	more universal data can go here
	myGUID	clip_guid; //16bytes		//Global Unique ID
	myGUID	parent_guids[3]; //48bytes	//Option source files (s) global unique IDs // NOT USED (yet)
	
	uint32_t project_number;		//user defined ID for the project
	
	// version 6+
	float channal_gamma_correction[3];	// obsolete -- if 0.0  then no gamma tweaks -- not a camera control used in post.	

	// version 7 and internal use only
	uint32_t process_path_flags_mask;	
	// bits 0-15, processing flags mask
	// bits 16-19 = 0-15 preview demosaic 
	// bits 20-23 = 0-15 render demosaic 
	//              0 - automatic 
	//              1 - bilinear 
	//              2 - Matrix 5x5 Enhanced 
	//              3 - CF Advanced Smooth 
	//              4 - CF Advanced Detail 1 
	//              5 - CF Advanced Detail 2 
	//              6 - CF Advanced Detail 3
	
	
	uint32_t demosaic_type;	// 0= unused, 1-bilinear, 2-5x5 Enh, 3-Advanced Smooth, 4-6-Advanced Detail 1-3
	uint32_t MSChannel_type_value; //0 
									// Channel = 0,1 = normal, 2 = channel 2 of 3D/multicam, 3 = 1+2 channel mix, etc. 
									// Type = 0<<8 - none, 
									//	1<<8 - stacked half vert, 
									//	2<<8 - side by side, half horiz, 
									//	3<<8 - fields, 
									//	4<<8 - odd/even pixels, 
									//	16<<8 - Red/Cyan anaglyph RGB, 
									//	17<<8 - Red/Cyan B&W anaglyph (Luma), 
									//	18<<8 - Amber/Blue  anaglyph RGB,
									//	19<<8 - Amber/Blue B&W anaglyph (luma),
									//	20<<8 - Green/Magneta  anaglyph RGB,
									// Value = 
									//	0x8000 - swapped
	uint32_t split_pos_xy;		// upper 16-bit bits of CMVL x | y<<8;
	uint32_t MSCTV_Override;	//	Same as above, but set within DShow or similar

	uint32_t InvertOffset;		// RGB is upsidedown, so invert V & R offsets.
	uint32_t channel_flip;		// (1(Horiz)|2(Vert))<<channel num.  0 = no flip, 1 = h flip chn.1, 4 h flip chn.2, 0xf v/h flip chns.1&2, etc	
	
	uint32_t cpu_limit;			// if non-zero limit to number of cores used to run.
	uint32_t cpu_affinity;		// if non-zero set the CPU affinity used to run each thread.
	uint32_t ignore_disk_database;	// if non-zero skip disk DB overides
	uint32_t force_disk_database;	// if non-zero read disk DB overides on every frame
    uint32_t force_metadata_refresh;    // if non-zero, refresh the database
	
	uint32_t colorspace;		// Active Metadata Colorspace override controls, 1 - 601, 2 - 709, 4 - studioRGB range, 8 - 422to444 upsampling
	uint32_t calibration;		// internal used only

	float FrameOffsetX;			// Change center position for all channels.
	float FrameOffsetY;
	float FrameOffsetR;
	float FrameOffsetF;
	float FrameHScale;
	float FrameHDynamic;
	float FrameHDynCenter;
	float FrameHDynWidth;
	

	float LensZoom;		
	float LensOffsetX;			// Change center position for all channels.
	float LensOffsetY;
	float LensOffsetZ;
	float LensOffsetR;
	float LensFishFOV;
	float LensHScale;
	float LensHDynamic;
	float LensHDynCenter;
	float LensHDynWidth;

	float LensXmin;
	float LensXmax;
	float LensYmin;
	float LensYmax;



	float split_CC_position;		// 0 - all normal - 1.0 all color corrections off 0.5 is the middle.

	uint32_t use_base_matrix;	// 0 = unity, 1 = camera orig, 2 custom matrix

	ChannelData channel[3];		// both, left and Right Eye info
	ChannelData channelAlt[3];	// When using the WarpLib for framing

	uint32_t FramingFlags;		// 1 - auto zoom, 2 - channel swap
	uint32_t BurninFlags;		// 1 - overlay, 2 - Tools
	uint32_t ComputeFlags;		// 1 - overlay, 2 - Histogram RGB, 4 - Waveform RGB, 8 - Vectorscope 1, 16 - Vectorscope 2, 
	uint32_t timecode_base;		// 0 assume 24, otherwise 24,25 or 30

	uint32_t update_last_used;	// if NOT called by First Light, update registry with current GUID, UFRM and TIMECODE.
	
	uint32_t encode_curve_preset;		// used by BYR4 inputs to indicate the source data is not linear.

	int32_t lensGoPro;
	uint32_t lensSphere;
	uint32_t lensFill;
	uint32_t lensStyleSel;
	uint32_t doMesh;

	float lensCustomSRC[6];
	float lensCustomDST[6];

	//	custom_data_offset points here
	// 256 byte more prevents mediaplayer/windows from working (can't find the codec.)	
//	uint8_t freeform[MAX_METADATA_HEADER];

} CFHDDATA;


#endif //AVIEH


