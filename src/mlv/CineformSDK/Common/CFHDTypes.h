/*! @file CFHDTypes.h
*
*  @brief Data types and pixel formats used within CineForm SDKs.
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
*/

#pragma once
#ifndef CFHD_TYPES_H
#define CFHD_TYPES_H

#include "CFHDAllocator.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Convert the four character code to the correct byte order
#ifndef FOUR_CHAR_CODE
#define FOUR_CHAR_CODE(a,b,c,d)		(((d&0xff)<<0)|((c&0xff)<<8)|((b&0xff)<<16)|((a&0xff)<<24))
#endif

// Opaque datatype for the CineForm license string
typedef unsigned char *CFHD_LicenseKey;

#ifdef __APPLE__

//! Pixel formats are specified using four character codes
typedef enum CFHD_PixelFormat
{
	CFHD_PIXEL_FORMAT_UNKNOWN = 0,
	CFHD_PIXEL_FORMAT_CFHD = ('CFHD'),	// compressed data

	// Encoder and Decoder formats
	CFHD_PIXEL_FORMAT_BGRA = ('BGRA'),	// RGBA 8-bit 4:4:4:4 inverted
	CFHD_PIXEL_FORMAT_BGRa = ('BGRa'),	// RGBA 8-bit 4:4:4:4
	CFHD_PIXEL_FORMAT_RG24 = ('RG24'),	// RGB 8-bit 4:4:4 inverted
	CFHD_PIXEL_FORMAT_2VUY = ('2vuy'),	// Component Y'CbCr 8-bit 4:2:2
	CFHD_PIXEL_FORMAT_YUY2 = ('YUY2'),	// Component Y'CbCr 8-bit 4:2:2
	CFHD_PIXEL_FORMAT_B64A = ('b64a'),	// ARGB with 16-bits per component
	CFHD_PIXEL_FORMAT_RG48 = ('RG48'),	// 16-bit RGB CFHD format
	CFHD_PIXEL_FORMAT_YU64 = ('YU64'),
	CFHD_PIXEL_FORMAT_V210 = ('v210'),
	CFHD_PIXEL_FORMAT_RG30 = ('RG30'),	// (AJA format)
	CFHD_PIXEL_FORMAT_AB10 = ('AB10'),	// A2B10G10R10 (same as RG30)
	CFHD_PIXEL_FORMAT_AR10 = ('AR10'),	// A2R10G10B10
	CFHD_PIXEL_FORMAT_R210 = ('r210'),	// DPX packed format
	CFHD_PIXEL_FORMAT_DPX0 = ('DPX0'),	// DPX packed format
	CFHD_PIXEL_FORMAT_NV12 = ('NV12'),	// Planar YUV 4:2:0 format for MPEG-2
	CFHD_PIXEL_FORMAT_YV12 = ('YV12'),	// Planar YUV 4:2:0 format for MPEG-2
	CFHD_PIXEL_FORMAT_R408 = ('R408'),	// Component Y'CbCrA 8-bit 4:4:4:4 (alpha is not populated)
	CFHD_PIXEL_FORMAT_V408 = ('V408'),	// Component Y'CbCrA 8-bit 4:4:4:4 (alpha is not populated)
	CFHD_PIXEL_FORMAT_BYR4 = ('BYR4'),	// Raw Bayer 16-bits per component

	// Decoder only formats
	CFHD_PIXEL_FORMAT_BYR2 = ('BYR2'),	// Raw Bayer pixel data
	CFHD_PIXEL_FORMAT_WP13 = ('WP13'),	// signed 16-bit RGB CFHD format, whitepoint at 1<<13
	CFHD_PIXEL_FORMAT_W13A = ('W13A'),	// signed 16-bit RGBA CFHD format, whitepoint at 1<<13
	CFHD_PIXEL_FORMAT_YUYV = ('yuyv'),	// YUYV 8-bit 4:2:2

	// Encoder only formats
	CFHD_PIXEL_FORMAT_BYR5 = ('BYR5'),	// Raw Bayer 12-bits per component, packed line of 8-bit then line a 4-bit reminder
	CFHD_PIXEL_FORMAT_B48R = ('b48r'),	// RGB 16-bits per component
	CFHD_PIXEL_FORMAT_RG64 = ('RG64'),	// 16-bit RGBA CFHD format

	// Avid pixel formats
	CFHD_PIXEL_FORMAT_CT_UCHAR = ('avu8'),		// Avid 8-bit CbYCrY 4:2:2 (no alpha)
	CFHD_PIXEL_FORMAT_CT_10BIT_2_8 = ('av28'),	// Two planes of 8-bit and 2-bit pixels
	CFHD_PIXEL_FORMAT_CT_SHORT_2_14 = ('a214'),	// Avid fixed point 2.14 pixel format
	CFHD_PIXEL_FORMAT_CT_USHORT_10_6 = ('a106'),	// Avid fixed point 10.6 pixel format
	CFHD_PIXEL_FORMAT_CT_SHORT = ('av16'),		// Avid 16-bit signed pixels
	CFHD_PIXEL_FORMAT_UNC_ARGB_444 = ('ar10'),	// Avid 10-bit ARGB 4:4:4:4

	// Pixel formats in the Macintosh coding style
	kCFHDPixelFormatUnknown = CFHD_PIXEL_FORMAT_UNKNOWN,
	kCFHDPixelFormat32BGRA = CFHD_PIXEL_FORMAT_BGRA,		// RGBA 8-bit 4:4:4:4
	kCFHDPixelFormat422YpCbCr8 = CFHD_PIXEL_FORMAT_2VUY,	// Component Y'CbCr 8-bit 4:2:2
	kCFHDPixelFormat64ARGB = CFHD_PIXEL_FORMAT_B64A,		// ARGB with 16-bits per component
	kCFHDPixelFormatRG48 = CFHD_PIXEL_FORMAT_RG48,
	kCFHDPixelFormatRG64 = CFHD_PIXEL_FORMAT_RG64,
	kCFHDPixelFormatYU64 = CFHD_PIXEL_FORMAT_YU64,
	kCFHDPixelFormat422YpCbCr10 = CFHD_PIXEL_FORMAT_V210,	// Component Y'CbCr 10-bit 4:2:2
	kCFHDPixelFormatRG30 = CFHD_PIXEL_FORMAT_RG30,
	kCFHDPixelFormatAB10 = CFHD_PIXEL_FORMAT_AB10,
	kCFHDPixelFormatAR10 = CFHD_PIXEL_FORMAT_AR10,
	kCFHDPixelFormatR210 = CFHD_PIXEL_FORMAT_R210,			// DPX packed format
	kCFHDPixelFormatDPX0 = CFHD_PIXEL_FORMAT_DPX0,			// DPX packed format
	kCFHDPixelFormatBayer = CFHD_PIXEL_FORMAT_BYR2,			// Raw Bayer pixel data
	kCFHDPixelFormatWP13 = CFHD_PIXEL_FORMAT_WP13,
	kCFHDPixelFormatW13A = CFHD_PIXEL_FORMAT_W13A,
	kCFHDPixelFormatYUYV = CFHD_PIXEL_FORMAT_YUYV,
	kCFHDPixelFormatb48r = CFHD_PIXEL_FORMAT_B48R,
	k4444YpCrA32RPixelFormat = ('r4fl'),

} CFHD_PixelFormat;

#else // __APPLE__

//! Pixel formats are specified using four character codes
typedef enum CFHD_PixelFormat
{
	CFHD_PIXEL_FORMAT_UNKNOWN = 0,
	CFHD_PIXEL_FORMAT_CFHD = FOUR_CHAR_CODE('C','F','H','D'),	// compressed data

	// Encoder and Decoder formats
	CFHD_PIXEL_FORMAT_BGRA = FOUR_CHAR_CODE('B','G','R','A'),	// RGBA 8-bit 4:4:4:4 inverted
	CFHD_PIXEL_FORMAT_BGRa = FOUR_CHAR_CODE('B','G','R','a'),	// RGBA 8-bit 4:4:4:4
	CFHD_PIXEL_FORMAT_RG24 = FOUR_CHAR_CODE('R','G','2','4'),	// RGB 8-bit 4:4:4 inverted
	CFHD_PIXEL_FORMAT_2VUY = FOUR_CHAR_CODE('2','v','u','y'),	// Component Y'CbCr 8-bit 4:2:2
	CFHD_PIXEL_FORMAT_YUY2 = FOUR_CHAR_CODE('Y','U','Y','2'),	// Component Y'CbCr 8-bit 4:2:2
	CFHD_PIXEL_FORMAT_B64A = FOUR_CHAR_CODE('b','6','4','a'),	// ARGB with 16-bits per component
	CFHD_PIXEL_FORMAT_RG48 = FOUR_CHAR_CODE('R','G','4','8'),	// 16-bit RGB CFHD format
	CFHD_PIXEL_FORMAT_YU64 = FOUR_CHAR_CODE('Y','U','6','4'),
	CFHD_PIXEL_FORMAT_V210 = FOUR_CHAR_CODE('v','2','1','0'),
	CFHD_PIXEL_FORMAT_RG30 = FOUR_CHAR_CODE('R','G','3','0'),	// (AJA format)
	CFHD_PIXEL_FORMAT_AB10 = FOUR_CHAR_CODE('A','B','1','0'),	// A2B10G10R10(same as RG30)
	CFHD_PIXEL_FORMAT_AR10 = FOUR_CHAR_CODE('A','R','1','0'),	// A2R10G10B10
	CFHD_PIXEL_FORMAT_R210 = FOUR_CHAR_CODE('r','2','1','0'),	// DPX packed format
	CFHD_PIXEL_FORMAT_DPX0 = FOUR_CHAR_CODE('D','P','X','0'),	// DPX packed format
	CFHD_PIXEL_FORMAT_NV12 = FOUR_CHAR_CODE('N','V','1','2'),	// Planar YUV 4:2:0 format for MPEG-2
	CFHD_PIXEL_FORMAT_YV12 = FOUR_CHAR_CODE('Y','V','1','2'),	// Planar YUV 4:2:0 format for MPEG-2
	CFHD_PIXEL_FORMAT_R408 = FOUR_CHAR_CODE('R','4','0','8'),	// Component Y'CbCrA 8-bit 4:4:4:4 (alpha is not populated)
	CFHD_PIXEL_FORMAT_V408 = FOUR_CHAR_CODE('V','4','0','8'),	// Component Y'CbCrA 8-bit 4:4:4:4 (alpha is not populated)
	CFHD_PIXEL_FORMAT_BYR4 = FOUR_CHAR_CODE('B','Y','R','4'),	// Raw bayer 16-bits per component

	// Decoder only formats
	CFHD_PIXEL_FORMAT_BYR2 = FOUR_CHAR_CODE('B','Y','R','2'),	// Raw Bayer pixel data
	CFHD_PIXEL_FORMAT_WP13 = FOUR_CHAR_CODE('W','P','1','3'),	// signed 16-bit RGB CFHD format, whitepoint at 1<<13
	CFHD_PIXEL_FORMAT_W13A = FOUR_CHAR_CODE('W','1','3','A'),	// signed 16-bit RGBA CFHD format, whitepoint at 1<<13
	CFHD_PIXEL_FORMAT_YUYV = FOUR_CHAR_CODE('y','u','y','v'),	// YUYV 8-bit 4:2:2

	// Encoder only formats
	CFHD_PIXEL_FORMAT_BYR5 = FOUR_CHAR_CODE('B','Y','R','5'),	// Raw Bayer 12-bits per component, packed line of 8-bit then line a 4-bit reminder
	CFHD_PIXEL_FORMAT_B48R = FOUR_CHAR_CODE('b','4','8','r'),	// RGB 16-bits per component
	CFHD_PIXEL_FORMAT_RG64 = FOUR_CHAR_CODE('R','G','6','4'),	// 16-bit RGBA CFHD format

	// Avid pixel formats
	CFHD_PIXEL_FORMAT_CT_UCHAR =       FOUR_CHAR_CODE('a','v','u','8'),	// Avid 8-bit CbYCrY 4:2:2 (no alpha)
	CFHD_PIXEL_FORMAT_CT_10BIT_2_8 =   FOUR_CHAR_CODE('a','v','2','8'),	// Two planes of 8-bit and 2-bit pixels
	CFHD_PIXEL_FORMAT_CT_SHORT_2_14 =  FOUR_CHAR_CODE('a','2','1','4'),	// Avid fixed point 2.14 pixel format
	CFHD_PIXEL_FORMAT_CT_USHORT_10_6 = FOUR_CHAR_CODE('a','1','0','6'),	// Avid fixed point 10.6 pixel format
	CFHD_PIXEL_FORMAT_CT_SHORT =	   FOUR_CHAR_CODE('a','v','1','6'),	// Avid 16-bit signed pixels
	CFHD_PIXEL_FORMAT_UNC_ARGB_444 =   FOUR_CHAR_CODE('a','r','1','0'),	// Avid 10-bit ARGB 4:4:4:4

	// Pixel formats in the Macintosh coding style
	kCFHDPixelFormatUnknown = CFHD_PIXEL_FORMAT_UNKNOWN,
	kCFHDPixelFormat32BGRA = CFHD_PIXEL_FORMAT_BGRA,		// RGBA 8-bit 4:4:4:4
	kCFHDPixelFormat422YpCbCr8 = CFHD_PIXEL_FORMAT_2VUY,	// Component Y'CbCr 8-bit 4:2:2
	kCFHDPixelFormat64ARGB = CFHD_PIXEL_FORMAT_B64A,		// ARGB with 16-bits per component
	kCFHDPixelFormatRG48 = CFHD_PIXEL_FORMAT_RG48,
	kCFHDPixelFormatRG64 = CFHD_PIXEL_FORMAT_RG64,
	kCFHDPixelFormatYU64 = CFHD_PIXEL_FORMAT_YU64,
	kCFHDPixelFormat422YpCbCr10 = CFHD_PIXEL_FORMAT_V210,	// Component Y'CbCr 10-bit 4:2:2
	kCFHDPixelFormatRG30 = CFHD_PIXEL_FORMAT_RG30,
	kCFHDPixelFormatAB10 = CFHD_PIXEL_FORMAT_AB10,
	kCFHDPixelFormatAR10 = CFHD_PIXEL_FORMAT_AR10,
	kCFHDPixelFormatR210 = CFHD_PIXEL_FORMAT_R210,			// DPX packed format
	kCFHDPixelFormatDPX0 = CFHD_PIXEL_FORMAT_DPX0,			// DPX packed format
	kCFHDPixelFormatBayer = CFHD_PIXEL_FORMAT_BYR2,			// Raw Bayer pixel data
	kCFHDPixelFormatWP13 = CFHD_PIXEL_FORMAT_WP13,
	kCFHDPixelFormatW13A = CFHD_PIXEL_FORMAT_W13A,
	kCFHDPixelFormatYUYV = CFHD_PIXEL_FORMAT_YUYV,
	kCFHDPixelFormatb48r = CFHD_PIXEL_FORMAT_B48R,
	k4444YpCrA32RPixelFormat = FOUR_CHAR_CODE('r','4','f','l'),

} CFHD_PixelFormat;

#endif // __APPLE__

typedef enum CFHD_SampleInfoTag
{
	CFHD_SAMPLE_INFO_CHANNELS = 0,	// int_32
	CFHD_SAMPLE_DISPLAY_WIDTH,		// int_32
	CFHD_SAMPLE_DISPLAY_HEIGHT,		// int_32
	CFHD_SAMPLE_KEY_FRAME,			// int_32
	CFHD_SAMPLE_PROGRESSIVE,		// int_32

	// The follow started working with 6.7.3
	CFHD_SAMPLE_ENCODED_FORMAT,		// int_32   // With early SDKs return 1 for YUV (rather than 0)
	CFHD_SAMPLE_SDK_VERSION,		// int_32	//
	CFHD_SAMPLE_ENCODE_VERSION,		// int_32
} CFHD_SampleInfoTag;

/*! @enum CFHD_EncodingQuality

	@brief Encoding quality settings (adapted from Encoder2).
*/
typedef enum CFHD_EncodingQuality
{
	CFHD_ENCODING_QUALITY_FIXED = 0, // also interpreted as unset as there is no CBR mode.
	CFHD_ENCODING_QUALITY_LOW,
	CFHD_ENCODING_QUALITY_MEDIUM,
	CFHD_ENCODING_QUALITY_HIGH,
	CFHD_ENCODING_QUALITY_FILMSCAN1,
	CFHD_ENCODING_QUALITY_FILMSCAN2,
	CFHD_ENCODING_QUALITY_FILMSCAN3, // overkill but useful for get higher data-rates from animation or extremely clean sources
	CFHD_ENCODING_QUALITY_KEYING = CFHD_ENCODING_QUALITY_FILMSCAN2 | 0x04000000, //444 only keying variation of FILMSCAN2
	CFHD_ENCODING_QUALITY_ONE_EIGHTH_UNCOMPRESSED = 1<<8,
	CFHD_ENCODING_QUALITY_QUARTER_UNCOMPRESSED = 2<<8,
	CFHD_ENCODING_QUALITY_THREE_EIGHTH_UNCOMPRESSED = 3<<8,
	CFHD_ENCODING_QUALITY_HALF_UNCOMPRESSED = 4<<8,
	CFHD_ENCODING_QUALITY_FIVE_EIGHTH_UNCOMPRESSED = 5<<8,
	CFHD_ENCODING_QUALITY_THREE_QUARTER_UNCOMPRESSED = 6<<8,
	CFHD_ENCODING_QUALITY_SEVEN_EIGHTH_UNCOMPRESSED = 7<<8,
	CFHD_ENCODING_QUALITY_UNCOMPRESSED = 16<<8,
	CFHD_ENCODING_QUALITY_UNC_NO_STORE = (32|16)<<8,

	// Default encoding quality
	CFHD_ENCODING_QUALITY_DEFAULT = CFHD_ENCODING_QUALITY_FILMSCAN1,

} CFHD_EncodingQuality;

typedef int CFHD_EncodingBitrate;

/*! @enum CFHD_EncodedFormat

	@brief Internal format used by the encoder.
*/
typedef enum CFHD_EncodedFormat
{
	CFHD_ENCODED_FORMAT_YUV_422 = 0,
	CFHD_ENCODED_FORMAT_RGB_444,
	CFHD_ENCODED_FORMAT_RGBA_4444,
	CFHD_ENCODED_FORMAT_BAYER,
	CFHD_ENCODED_FORMAT_YUVA_4444, //not implemented
	CFHD_ENCODED_FORMAT_UNKNOWN
} CFHD_EncodedFormat;


#define THUMBNAILSIZE(w,h)  ((((w)+7)/8)*(((h)+7)/8)*4)

/*! @enum CFHD_EncodingFlags

	@brief Flags that provide additional information about the video format.
*/
enum
{
	CFHD_ENCODING_FLAGS_NONE				= 0,

	//YUV flags
	CFHD_ENCODING_FLAGS_YUV_INTERLACED		= 1<<0,	//YUV 4:2:2 only
	CFHD_ENCODING_FLAGS_YUV_2FRAME_GOP		= 1<<1,	//YUV 4:2:2 only
	CFHD_ENCODING_FLAGS_YUV_601				= 1<<2,	//YUV 4:2:2 only, force 601, default is 709.
	//spare									= 1<<3, //unused

	//Encoding curve
	CFHD_ENCODING_FLAGS_CURVE_APPLIED		= 1<<4,	//BYR4 source is typically linear, this instructs the encoder not to apply another curve
	CFHD_ENCODING_FLAGS_CURVE_GAMMA22		= 0,	//default (particular for YUV and RGB sources.)
	CFHD_ENCODING_FLAGS_CURVE_LOG90			= 1<<5,	//recommended for RAW
	CFHD_ENCODING_FLAGS_CURVE_LINEAR		= 1<<6,	//not recommend
	CFHD_ENCODING_FLAGS_CURVE_CUSTOM		= 1<<7,	//use metadata tag TAG_ENCODE_CURVE
	CFHD_ENCODING_FLAGS_RGB_STUDIO			= 1<<8,	//RGB 4:4:4 only, force Studio RGB Levels, default is cgRGB (black at 0 vs black at 16/64 out of 255/1023).

	//For Compressed DPX encoding (produces a 10-bit RGB 32-bit packed Thumbnail.)
	// thumbnail is appended to the end of the sample, so can be written directly into a compressed DPX file.  The image offset for the DPX header pointes
	// ((width+7)/8)*((height+7)/8)*4 bytes or THUMBNAILSIZE(width,height) from the end of the sample.
	CFHD_ENCODING_FLAGS_APPEND_THUMBNAIL	= 1<<9, //Auto generate a 1/8th size thumbnail, size (width+7)/8, (height+7)/8, 1920x1080 gives 240x135
	CFHD_ENCODING_FLAGS_WATERMARK_THUMBNAIL	= 1<<10, //Auto generate a 1/8th size thumbnail with compressed DPX watermark

	CFHD_ENCODING_FLAGS_LARGER_OUTPUT		= 1<<11, //Allocate output buffer big enough to support uncompressed stereo sequences.
												// The output buffer is typically 1:1 to the source frame size, for 3D the output can
												// be bigger than one fraem size (as there are two frames encoded.)

	// Encoding flags in the Macintosh coding style
	kCFHDEncodingFlagsNone = CFHD_ENCODING_FLAGS_NONE,
	kCFHDEncodingFlagsInterlaced = CFHD_ENCODING_FLAGS_YUV_INTERLACED,

};

typedef uint32_t CFHD_EncodingFlags;

/*! @enum CFHD_FieldType

	@brief Organization of the video fields (progressive versus interlaced)
*/
typedef enum CFHD_FieldType
{
	CFHD_FIELD_TYPE_UNKNOWN = 0,
	CFHD_FIELD_TYPE_PROGRESSIVE = 1,

	// The second bit is used to indicate whether the frame is interlaced
	CFHD_FIELD_TYPE_UPPER_FIELD_FIRST = 2,
	CFHD_FIELD_TYPE_LOWER_FIELD_FIRST = 3,

} CFHD_FieldType;


// Four character code for the metadata tag
typedef uint32_t CFHD_MetadataTag;

// Size of a single item of metadata
typedef int32_t CFHD_MetadataSize;


typedef enum CFHD_MetadataType
{
	METADATATYPE_UNKNOWN = 0,
	METADATATYPE_STRING = 1,
	METADATATYPE_UINT32 = 2,
	METADATATYPE_UINT16 = 3,
	METADATATYPE_UINT8 = 4,
	METADATATYPE_FLOAT = 5,
	METADATATYPE_DOUBLE = 6,
	METADATATYPE_GUID = 7,
	METADATATYPE_XML = 8,
	METADATATYPE_LONG_HEX = 9,
	METADATATYPE_CINEFORM = 10, // used for a setting CineForm pre-formatted metadata
	METADATATYPE_HIDDEN = 11,
	METADATATYPE_TAG = 12,

} CFHD_MetadataType;


//  Use with TAG_BAYER_FORMAT metadata to set bayer phase
typedef enum CFHD_BayerFormat
{
	CFHD_BAYER_FORMAT_UNKNOWN = -1,
	CFHD_BAYER_FORMAT_RED_GRN = 0,
	CFHD_BAYER_FORMAT_GRN_RED = 1,
	CFHD_BAYER_FORMAT_GRN_BLU = 2,
	CFHD_BAYER_FORMAT_BLU_GRN = 3,
} CFHD_BayerFormat;

// Use with TAG_DEMOSAIC_TYPE to control which demosaic
enum
{
	DEMOSAIC_USER_DEFAULT = 0,
	DEMOSAIC_BILINEAR = 1,
	DEMOSAIC_MATRIX5x5 = 2,
	DEMOSAIC_ADVANCED_SMOOTH = 3,
	DEMOSAIC_ADVANCED_DETAIL1 = 4,
	DEMOSAIC_ADVANCED_DETAIL2 = 5,
	DEMOSAIC_ADVANCED_DETAIL3 = 6,
};



#define CFHD_CURVE_TYPE_UNDEF	0
#define CFHD_CURVE_TYPE_LOG		1
#define CFHD_CURVE_TYPE_GAMMA	2
#define CFHD_CURVE_TYPE_LINEAR	4
#define CFHD_CURVE_TYPE_CINEON	5		//black at 95 and white 685, b and c are the gamma curve (ie. 17/10 = 1.7)
#define CFHD_CURVE_TYPE_PARA	6		//b and c are the gain and power parameters  (1.0-(float)pow((1.0-(double)i),(1.0/((double)power*256.0)))*gain;
#define CFHD_CURVE_TYPE_CINE985	7		//black at 95 and white 685, b and c are the gamma curve (ie. 17/10 = 1.7)
#define CFHD_CURVE_TYPE_CSTYLE	8  		//Model close to Technicolor CineStyle(TM) for Canon DSLRs
#define CFHD_CURVE_TYPE_SLOG	9  		//Sony's S-Log
#define CFHD_CURVE_TYPE_LOGC   10  		//Alexa's Log-C


//Flags or'd with the above types.
#define CFHD_CURVE_TYPE_NEGATIVE	0x8000	//Negative filmscan support
#define CFHD_CURVE_TYPE_EXTENDED	0x4000	//Use the b and c, fields read as a single 16-bit integer for the log base (range 0 to 65535)

//! Encode the curve as 0xaaaabbcc, where a is the type, b is the value numerator, and c is the value denominator
#define CFHD_CURVE_TYPE(a,b,c)	(((a) << 16) | ((b) << 8) | (c))
#define CFHD_CURVE_TYPE_EXT(a,b)	(((a|CFHD_CURVE_TYPE_EXTENDED) << 16) | (b))		// 0xaaaabbcc  a - type, b - base


//  Use with TAG_ENCODE_CURVE & TAG_ENCODE_PRESET metadata to set source encoding curve
enum
{
	CFHD_CURVE_LOG_90 = 		CFHD_CURVE_TYPE(CFHD_CURVE_TYPE_LOG,90,1),
	CFHD_CURVE_GAMMA_2pt2 = 	CFHD_CURVE_TYPE(CFHD_CURVE_TYPE_GAMMA,22,10),
	CFHD_CURVE_CINEON_1pt7 = 	CFHD_CURVE_TYPE(CFHD_CURVE_TYPE_CINEON,17,10),
	CFHD_CURVE_CINE985_1pt7 = 	CFHD_CURVE_TYPE(CFHD_CURVE_TYPE_CINE985,17,10),
	CFHD_CURVE_CINEON_1pt0 = 	CFHD_CURVE_TYPE(CFHD_CURVE_TYPE_CINEON,1,1),
	CFHD_CURVE_LINEAR = 		CFHD_CURVE_TYPE(CFHD_CURVE_TYPE_LINEAR,1,1),
	CFHD_CURVE_REDSPACE = 		CFHD_CURVE_TYPE(CFHD_CURVE_TYPE_PARA,202,4),
	CFHD_CURVE_CSTYLE = 		CFHD_CURVE_TYPE(CFHD_CURVE_TYPE_CSTYLE,1,1),
	CFHD_CURVE_SLOG = 			CFHD_CURVE_TYPE(CFHD_CURVE_TYPE_SLOG,1,1),
	CFHD_CURVE_LOGC = 			CFHD_CURVE_TYPE(CFHD_CURVE_TYPE_LOGC,1,1),
	CFHD_CURVE_PROTUNE =		CFHD_CURVE_TYPE(CFHD_CURVE_TYPE_LOG,113,1),
	CFHD_CURVE_LOG_9_STOP =		CFHD_CURVE_TYPE_EXT(1,30),
	CFHD_CURVE_LOG_10_STOP =	CFHD_CURVE_TYPE_EXT(1,70),
	CFHD_CURVE_LOG_11_STOP =	CFHD_CURVE_TYPE_EXT(1,170),
	CFHD_CURVE_LOG_12_STOP =	CFHD_CURVE_TYPE_EXT(1,400),
	CFHD_CURVE_LOG_13_STOP =	CFHD_CURVE_TYPE_EXT(1,900)
};


typedef float CFHD_WhiteBalance[4];

typedef float CFHD_ColorMatrix[3][4];


#define METADATAFLAG_FILTERED	1		//Data filtered by the users active decoder preference.
										//If the operater wasn't displaying corrected whitebalance,
										//whilebalance will be returned as zero.
#define METADATAFLAG_MODIFIED	2		// Get any user changes from the database (external to the file.)
#define METADATAFLAG_RIGHT_EYE  4		// Extract Right Eye metadata when reading/writing, default is both eyes.
#define METADATAFLAG_LEFT_EYE	8		// Extract Left Eye metadata when reading/writing, default is both eyes.

typedef enum CFHD_MetadataTrack
{
	METADATATYPE_ORIGINAL = 0,
	METADATATYPE_ORIGINAL_FILTERED = METADATAFLAG_FILTERED,
	METADATATYPE_MODIFIED = METADATAFLAG_MODIFIED,
	METADATATYPE_MODIFIED_FILTERED = METADATAFLAG_MODIFIED | METADATAFLAG_FILTERED,
	METADATATYPE_MODIFIED_RIGHT = METADATAFLAG_RIGHT_EYE | METADATAFLAG_MODIFIED,
	METADATATYPE_MODIFIED_RIGHT_FILTERED = METADATAFLAG_RIGHT_EYE | METADATAFLAG_MODIFIED | METADATAFLAG_FILTERED,
	METADATATYPE_MODIFIED_LEFT = METADATAFLAG_LEFT_EYE | METADATAFLAG_MODIFIED,
	METADATATYPE_MODIFIED_LEFT_FILTERED = METADATAFLAG_LEFT_EYE | METADATAFLAG_MODIFIED | METADATAFLAG_FILTERED
} CFHD_MetadataTrack;

typedef enum CFHD_VideoSelect
{
	VIDEO_SELECT_DEFAULT = 0, // use left eye.
	VIDEO_SELECT_LEFT_EYE = 1,
	VIDEO_SELECT_RIGHT_EYE = 2,
	VIDEO_SELECT_BOTH_EYES = 3,
} CFHD_VideoSelect;

typedef enum CFHD_Stereo3DType
{
	STEREO3D_TYPE_DEFAULT = 0,
	STEREO3D_TYPE_STACKED = 1,
	STEREO3D_TYPE_SIDEBYSIDE = 2,
	STEREO3D_TYPE_FIELDS = 3,
	STEREO3D_TYPE_ONION = 4,
	STEREO3D_TYPE_DIFFERENCE = 5,
	STEREO3D_TYPE_FREEVIEW = 7,
	STEREO3D_TYPE_ANAGLYPH_RED_CYAN = 16,
	STEREO3D_TYPE_ANAGLYPH_RED_CYAN_BW = 17,
	STEREO3D_TYPE_ANAGLYPH_BLU_YLLW = 18,
	STEREO3D_TYPE_ANAGLYPH_BLU_YLLW_BW = 19,
	STEREO3D_TYPE_ANAGLYPH_GRN_MGTA = 20,
	STEREO3D_TYPE_ANAGLYPH_GRN_MGTA_BW = 21,
	STEREO3D_TYPE_ANAGLYPH_OPTIMIZED = 22,
} CFHD_Stereo3DType;


typedef enum CFHD_StereoFlags
{
	STEREO_FLAGS_DEFAULT = 0,
	STEREO_FLAGS_SWAP_EYES = 1,
	STEREO_FLAGS_SPEED_3D = 2,	//use half res wavelet decode, even if full res output is request (so scale.)
} CFHD_StereoFlags;

typedef enum CFHD_DecodedResolution
{
	CFHD_DECODED_RESOLUTION_UNKNOWN = 0,
	CFHD_DECODED_RESOLUTION_FULL = 1,
	CFHD_DECODED_RESOLUTION_HALF = 2,
	CFHD_DECODED_RESOLUTION_QUARTER = 3,
	CFHD_DECODED_RESOLUTION_THUMBNAIL = 4,

	CFHD_DECODED_RESOLUTION_DEFAULT = CFHD_DECODED_RESOLUTION_FULL,

	// Decoded resolution in the Macintosh coding style
	kCFHDDecodedResolutionUnknown = CFHD_DECODED_RESOLUTION_UNKNOWN,
	kCFHDDecodedResolutionFull = CFHD_DECODED_RESOLUTION_FULL,
	kCFHDDecodedResolutionHalf = CFHD_DECODED_RESOLUTION_HALF,
	kCFHDDecodedResolutionQuarter = CFHD_DECODED_RESOLUTION_QUARTER,
	kCFHDDecodedResolutionThumbnail = CFHD_DECODED_RESOLUTION_THUMBNAIL,

	kCFHDDecodedResolutionDefault = kCFHDDecodedResolutionFull,

} CFHD_DecodedResolution;

// Definitions of the flags for CFHD_DecodingFlags (see below)
enum
{
	CFHD_DECODING_FLAGS_NONE = 0,
	CFHD_DECODING_FLAGS_IGNORE_OUTPUT	= (1 << 0),
	CFHD_DECODING_FLAGS_MUST_SCALE		= (1 << 1),
	CFHD_DECODING_FLAGS_USE_RESOLUTION	= (1 << 2),
	CFHD_DECODING_FLAGS_INTERNAL_ONLY	= (1 << 3),

	// Decoding flags in the Macintosh coding style
	kCFHDDecodingFlagsNone = CFHD_DECODING_FLAGS_NONE,
	kCFHDDecodingFlagsIgnoreOutput = CFHD_DECODING_FLAGS_IGNORE_OUTPUT,
	kCFHDDecodingFlagsMustScale = CFHD_DECODING_FLAGS_MUST_SCALE,
	kCFHDDecodingFlagsUseResolution = CFHD_DECODING_FLAGS_USE_RESOLUTION,

};

typedef uint32_t CFHD_DecodingFlags;

#endif // CFHD_TYPES_H
