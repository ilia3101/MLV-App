/*! @file CFHDMetadataTags.h
*
*  @brief Active Metadata FourCC tags and control flags
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
#ifndef CFHD_METADATA_TAGS_H
#define CFHD_METADATA_TAGS_H

// only using the bottom 16-bits, set to 0xffff and mask out elements to disable. (used by TAG_PROCESS_PATH)
#define PROCESSING_ACTIVE			(1L<<0) //set to indicate flags are in use
#define PROCESSING_COLORMATRIX		(1L<<1)
#define PROCESSING_WHITEBALANCE		(1L<<2)
#define PROCESSING_LOOK_FILE		(1L<<3)
#define PROCESSING_DEFECT_PIXELS	(1L<<4)
#define PROCESSING_GAMMA_TWEAKS		(1L<<5)
#define PROCESSING_PAD1				(1L<<6)
#define PROCESSING_PAD2				(1L<<7)

#define PROCESSING_ACTIVE2			(1L<<8) //set to indicate the next set of flags are in use
#define PROCESSING_ORIENTATION		(1L<<9)	//Dzoom, convergence, and floating windows, etc.
#define PROCESSING_BURNINS			(1L<<10) //Histograms, Vectorscope, metadata burns
#define PROCESSING_FRAMING			(1L<<11) //zoom, x,y offset
#define PROCESSING_IMAGEFLIPS		(1L<<12) //3D frame flips

#define PROCESSING_ACTIVE_COLORMATRIX		((1L<<1)|1)
#define PROCESSING_ACTIVE_WHITEBALANCE		((1L<<2)|1)
#define PROCESSING_ACTIVE_LOOK_FILE			((1L<<3)|1)
#define PROCESSING_ACTIVE_DEFECT_PIXELS		((1L<<4)|1)
#define PROCESSING_ACTIVE_GAMMA_TWEAKS		((1L<<5)|1)
#define PROCESSING_ACTIVE_ORIENTATION		( (1L<<9)|256) //Dzoom, convergence, and floating windows, etc.
#define PROCESSING_ACTIVE_BURNINS			((1L<<10)|256) //Histograms, Vectorscope, metadata burns
#define PROCESSING_ACTIVE_FRAMING			((1L<<11)|256) //zoom, x,y offset
#define PROCESSING_ACTIVE_IMAGEFLIPS		((1L<<12)|256) //3D frame flips


#define PROCESSING_ALL_ON			0xffff
#define PROCESSING_ALL_OFF			(PROCESSING_ACTIVE2|PROCESSING_ACTIVE)


#define METADATA_EYE_BOTH			0
#define METADATA_EYE_LEFT			1
#define METADATA_EYE_RIGHT			2

#define METADATA_PRIORITY_BASE			0
#define METADATA_PRIORITY_FRAME			0x10		// sample DB priority
#define METADATA_PRIORITY_FRAME_1		0x11		// channel 1 - delta
#define METADATA_PRIORITY_FRAME_2		0x12		// channel 2 - delta
#define METADATA_PRIORITY_DATABASE		0x20		// disk DB priority
#define METADATA_PRIORITY_DATABASE_1	0x21		// channel 1 - delta
#define METADATA_PRIORITY_DATABASE_2	0x22		// channel 2 - delta
#define METADATA_PRIORITY_OVERRIDE		0x30
#define METADATA_PRIORITY_OVERRIDE_1	0x31		// channel 1 - delta overrides -- not used currently
#define METADATA_PRIORITY_OVERRIDE_2	0x32		// channel 2 - delta overrides -- not used currently
#define METADATA_PRIORITY_MAX			0x3f

#define BAYER_FORMAT_RED_GRN		0
#define BAYER_FORMAT_GRN_RED		1
#define BAYER_FORMAT_GRN_BLU		2
#define BAYER_FORMAT_BLU_GRN		3

//Extended metadata format.
//4 char tag, 1 char format, 24bit size, data of size.  // string, bytes and shorts are pads to 32-bit.
//eg  white balance    WBAL(16)..f(1.0,1.0,1.0,1.0)  57 42 41 4c 10 00 00 66 00 00 80 3f 00 00 80 3f 00 00 80 3f 00 00 80 3f
//eg  scene number     SCEN(2)..S(3)                 53 43 45 4e 02 00 00 53 03 00 00 00

#ifndef MAKETAG
#define MAKETAG(d,c,b,a) (((a)<<24)|((b)<<16)|((c)<<8)|(d))
#endif

//types
// 'c' - zero terminated string
// 'C' - short coordinate "(x,y)"
// 'b' - signed byte
// 'B' - unsigned BYTE
// 'd' - double
// 'f' - float		"3.1415"
// 'G' - GUID 16byte "{B8120D2B-FBDA-48e4-8FDD-0E4B91E8DCCD}""
// 'H' - unsigned 32bit int32_t "0xB8120D2B"
// 'h' - hidden unsigned int 32bit  -- not displayed
// 'l' - signed 32-bit long
// 'L' - unsigned 32-bit int32_t
// 'R' - unsigned short ratio "x:y"
// 's' - signed short
// 'S' - unsigned short
// 'x' - XML
// NULL or 'v' - void or custom data

typedef enum
{
	METADATA_TYPE_STRING = 'c',
	//METADATA_TYPE_COORDINATE = 'C',
	METADATA_TYPE_SIGNED_BYTE = 'b',
	METADATA_TYPE_UNSIGNED_BYTE = 'B',
	METADATA_TYPE_DOUBLE = 'd',
	METADATA_TYPE_FLOAT = 'f',
	METADATA_TYPE_FOURCC = 'F',
	METADATA_TYPE_GUID = 'G',
	METADATA_TYPE_HIDDEN = 'h',
	METADATA_TYPE_UNSIGNED_LONG_HEX = 'H',
	METADATA_TYPE_SIGNED_LONG = 'l',
	METADATA_TYPE_UNSIGNED_LONG = 'L',
	METADATA_TYPE_UNSIGNED_SHORT_RATIO = 'R',
	METADATA_TYPE_SIGNED_SHORT = 's',
	METADATA_TYPE_UNSIGNED_SHORT = 'S',
	METADATA_TYPE_XML = 'x',
	METADATA_TYPE_TAG = 'T',
	METADATA_TYPE_CUSTOM_DATA = 0,

	// Metadata types using standard integer names (preferred)
	METADATA_TYPE_INT32 = METADATA_TYPE_SIGNED_LONG,
	METADATA_TYPE_INT16 = METADATA_TYPE_SIGNED_SHORT,
	METADATA_TYPE_INT8 = METADATA_TYPE_SIGNED_BYTE,
	METADATA_TYPE_UINT32 = METADATA_TYPE_UNSIGNED_LONG,
	METADATA_TYPE_UINT16 = METADATA_TYPE_UNSIGNED_SHORT,
	METADATA_TYPE_UINT8 = METADATA_TYPE_UNSIGNED_BYTE,

	// Add more metadata types here

} MetadataType;


// The four character code for the metadata tag
typedef uint32_t METADATA_TAG;

// Type of the metadata stored in a metadata tuple (see enumeration above)
typedef unsigned char METADATA_TYPE;

// Size of the metadata stored in a metadata tuple (the size field is 24 bits)
typedef int32_t METADATA_SIZE;

// Metadata flags
typedef uint32_t METADATA_FLAGS;

// Pack the metadata type and size into 32 bits
#define METADATA_TYPESIZE(t,s) ((uint32_t)((METADATA_TYPE)(t) << 24 | (METADATA_SIZE)(s)))

// Maximum size of a metadata item is limited to 24 bits unsigned
#define METADATA_SIZE_MAX ((1 << 24) - 1)


typedef struct metadata_tuple
{
	METADATA_TAG tag;		// Four character code that identifies the metadata item
	METADATA_TYPE type;		// Metadata type code (see enumeration for MetadataType)
	METADATA_SIZE size;		// Size of the metadata item (in bytes)
	uint32_t *data;			// Pointer to the metadata value

} METADATA_TUPLE;

typedef struct metadata_control_point_header
{
	uint32_t cptype;			// Four character code that identifies the control point type
	uint32_t reserved;			// zero for now
	METADATA_TAG position_type;	// nearly always TAG_UNIQUE_FRAMENUM (UFRM), but it can be TAG_TIMECODE (TIMC)
	uint32_t tsize;				// type_size of UFRM/TIMC
	union {
		uint32_t keyframe;		// key frame as a UFRM
		char keyframeTC[12];	// key frame as a timecode "xx:xx:xx:xx"
	};
} METADATA_CP_HDR;


typedef enum MetadataTag
{
//function         														  				Tag	  type	size
TAG_FREESPACE 			= MAKETAG('F','R','E','E'),		//Free metadata    				FREE	c	(n bytes) can be used for any data
TAG_NOP					= MAKETAG('N','O','P','P'),		//erase entry	  				NOPP	?	Used to erase a single entry
TAG_COLOR_MATRIX		= MAKETAG('C','O','L','M'),		//Color matrix     				COLM	f	12 floats (48 bytes)
TAG_UNITY_MATRIX		= MAKETAG('U','T','Y','M'),		//unity matrix          		UTYM	l	non-zero bypasses COLM
TAG_BASE_MATRIX			= MAKETAG('B','M','T','X'),		//Unity matrix     				BMTX	L	1 long, 0 - unity Matrix, 1 - camera original matrix, 2 - custom matrix.
TAG_SATURATION			= MAKETAG('S','A','T','U'),		//Saturation       				SATU	f	1 float // unity 1.0 range 0.0 to 4.0
TAG_BLUR_SHARPEN		= MAKETAG('B','L','S','H'),		//Blur/Sharpen     				BLSH	f	1 float // unity 0.0 range -1.0 (blur) to 1.0 full sharpen
TAG_HIGHLIGHT_DESAT		= MAKETAG('H','S','A','T'),		//highlight (de)Saturation     	HSAT	f	1 float // unity 1.0 range 0.0 to 1.0 (saturation)
TAG_HIGHLIGHT_POINT		= MAKETAG('H','P','N','T'),		//highlight point		     	HPNT	f	1 float // unity 1.0 range 0.0 (all highlights) to 1.0 (off)
TAG_VIGNETTE_START 		= MAKETAG('V','G','N','S'),		//vignette start point		    VGNS	f	1 float // unity 1.0 range 0.0 to 1.0, 1.0 is the horizontal with as radius
TAG_VIGNETTE_END 		= MAKETAG('V','G','N','E'),		//vignette end point		    VGNE	f	1 float // unity 1.0 range 0.0 to 2.0, 1.0 is the horizontal with as radius
TAG_VIGNETTE_GAIN		= MAKETAG('V','G','N','G'),		//vignette gain				    VGNG	f	1 float // unity 0.0 range 0.0 to 8.0,
TAG_SPLIT_POS			= MAKETAG('S','P','L','T'),		//highlight point		     	SPLT	f	1 float // split screen position for color correction 0.0 to 1.0 - 0.5 the center.
TAG_CONTRAST			= MAKETAG('C','T','R','S'),		//Contrast         				CTRS	f	1 float // unity 1.0 range 0.0 to 4.0
TAG_EXPOSURE			= MAKETAG('E','X','P','S'),		//Exposure         				EXPS	f	1 float // unity 1.0 range 0.0 to 8.0
TAG_ASC_CDL_MODE    	= MAKETAG('A','C','D','L'),		//ASC CDL Mode     				ACDL	H	1 long (4 bytes) 0 - off, 1 - on
TAG_RGB_GAMMA	 		= MAKETAG('G','A','M','T'),		//channel gamma    				GAMT	f	3 floats // (12 bytes)
TAG_RGB_GAIN			= MAKETAG('R','G','B','G'),		//RGB Gain         				RGBG	f	3 floats //RGB gains, unity 1.0 range 0.0 to 4.0
TAG_RGB_LIFT			= MAKETAG('R','G','B','O'),		//RGB Black Offset 				RGBO	f	3 floats // unity 1.0 range 0.0 to 4.0
TAG_RGB_OFFSET			= MAKETAG('R','G','B','O'),		//RGB Black Offset 				RGBO	f	3 floats // unity 1.0 range 0.0 to 4.0
TAG_GAMMA_TWEAKS 		= MAKETAG('G','A','M','T'),		//channel gamma    				GAMT	f	3 floats // (12 bytes)
TAG_TIMECODE 			= MAKETAG('T','I','M','C'),		//Timecode         				TIMC	c	11 chars (00:00:00:00)
TAG_TIMECODE_ALT		= MAKETAG('T','I','M','A'),		//Timecode ALT     				TIMA	c	11 chars
TAG_TIMECODE_BASE 		= MAKETAG('T','I','M','B'),		//Timecode Base    				TIMB	B	1 byte of common 24,25,30,50,60
TAG_TIMECODE_DROP 		= MAKETAG('T','I','M','D'),		//TC Dropframe     				TIMD	B	1 byte of 1=on, 0=off.  Only value for 30/60Hz base
TAG_REELNAME 			= MAKETAG('R','E','E','L'),		//Reel name        				REEL	c	40 chars
TAG_REELNAME_ALT 		= MAKETAG('R','E','E','A'),		//Reel name ALT    				REEA	c	40 chars
TAG_LOG_COMMENT 		= MAKETAG('L','O','G','C'),		//Log Comment      				LOGC	c	256 chars
TAG_WHITE_BALANCE 		= MAKETAG('W','B','A','L'),		//White balance    				WBAL	f	4 floats (16 bytes)
TAG_LOOK_FILE 			= MAKETAG('L','O','O','K'),		//LOOK File        				LOOK	c	40 chars
TAG_LOOK_EXPORT			= MAKETAG('L','K','E','X'),		//LOOK File export 				LKEX	c	x chars or a full path.
TAG_LOOK_CRC 			= MAKETAG('L','C','R','C'),		//LOOK CRC         				LCRC	H	1 long (4 bytes)
TAG_ENCODE_CURVE 		= MAKETAG('E','C','R','V'),		//Encode curve     				ECRV	H	1 long (4 bytes)
TAG_ENCODE_PRESET 		= MAKETAG('P','C','R','V'),		//Encode preset    				PCRV	H	1 long (4 bytes) // if non zero assume curve has been applied.
TAG_DECODE_CURVE 		= MAKETAG('D','C','R','V'),		//Decode curve     				DCRV	H	1 long (4 bytes)
TAG_PRIMARIES_CURVE 	= MAKETAG('C','C','R','V'),		//Primaries curve  				CCRV	H	1 long (4 bytes)
TAG_BAD_PIXEL 			= MAKETAG('B','A','D','P'),		//Bad Pixel        				BADP	H	n pixels long (4 * n bytes) //there can be many
TAG_TAKE_NUMBER 		= MAKETAG('T','A','K','E'),		//Take numer       				TAKE	S	1 short (2 bytes in 4)
TAG_TAKE_MODIFIER	 	= MAKETAG('T','K','M','D'),     //Take Modifier					TKMD	c	x chars like 'A' or 'B' etc for Take 3A or 5B
TAG_SHOT_NUMBER 		= MAKETAG('S','H','O','T'),		//Shot number      				SHOT	S	1 short (2 bytes in 4)
TAG_SCENE_NUMBER 		= MAKETAG('S','C','E','N'),		//Scene number     				SCEN	S	1 short (2 bytes in 4)
TAG_SCENE_NAME			= MAKETAG('S','C','E','S'),		//Scene name     				SCES	c	x bytes string name of scene
TAG_PROJECT_NUMBER 		= MAKETAG('P','R','O','J'),		//Project number   				PROJ	B	1 byte (1 byte in 4)
TAG_CAMERA_NUMBER 		= MAKETAG('C','A','M','R'),		//Camera number    				CAMR	B	1 byte (1 byte in 4)
TAG_ENCODE_DATE 		= MAKETAG('D','A','T','E'),		//Encode Date             		DATE	c	10 chars (format yyyy-mm-dd)
TAG_ENCODE_TIME 		= MAKETAG('T','I','M','E'),		//Encode Time             		TIME	c	8 chars (24hr format hh-mm-ss)
TAG_SOURCE_DATE 		= MAKETAG('S','D','A','T'),		//Source Date             		SDAT	c	10 chars (format yyyy-mm-dd)
TAG_SOURCE_TIME 		= MAKETAG('S','T','I','M'),		//Source Time             		STIM	c	8 chars (24hr format hh-mm-ss)
TAG_FINGERPRINT 		= MAKETAG('P','R','N','T'),		//Hdwr Fingerprint 				PRNT	H	1 long (4 bytes)
TAG_PIXEL_RATIO 		= MAKETAG('P','I','X','R'),		//Pixel ratio     				PIXR	R	ratio of two unsigned shorts. (4 bytes) (2-bytes numerator then 2-bytes demoninator) ratio = (float)(value >> 16)/(float)(value & 0xffff)
TAG_PROCESS_PATH 		= MAKETAG('P','R','C','S'),		//Process Path     				PRCS	H	1 long (4 bytes)
TAG_BAYER_FORMAT 		= MAKETAG('B','F','M','T'),		//Bayer Format     				BFMT	B	1 byte (1 byte in 4)
TAG_CLIP_GUID 			= MAKETAG('G','U','I','D'),		//GUID             				GUID	G	16 bytes
TAG_SUBTYPE 	 		= MAKETAG('S','U','B','T'),		//cfhd_subtype     				SUBT	L	4 bytes //0-normal (YUY2), 1-Bayer, 2-RGB native, 3-RGBA native
TAG_NUM_CHANNELS 		= MAKETAG('N','U','M','C'),		//num chroma channels  			NUMC	L	4 bytes //total number of chroma channels per stream
TAG_DEMOSAIC_TYPE 		= MAKETAG('D','E','M','O'),		//demosaic type    				DEMO	L	1 long
TAG_MARK_GOOD_TAKE 		= MAKETAG('M','R','K','G'),		//Mark Good Take   				MRKG	L	1 long
TAG_UNIQUE_FRAMENUM		= MAKETAG('U','F','R','M'),		//Unique FrameNum  				UFRM	L	1 long  // Unique to the current project, increments
TAG_ANALOG_GAIN 		= MAKETAG('G','A','I','N'),		//Cam analog gain  				GAIN	S	1 short // typical values -3,0,3,6,9,12
TAG_SHUTTER_SPEED 		= MAKETAG('S','H','U','T'),		//Shutter Speed    				SHUT	S	1 short // 24,48,50,60,120 etc (1/value of a second.)
TAG_COLORSPACE_YUV 		= MAKETAG('C','L','S','Y'),		//Colorspace YUV   				CLSY	H	1 long (4 bytes) //0 = unset, 1 = 601, 2 = 709
TAG_COLORSPACE_RGB 		= MAKETAG('C','L','S','R'),		//Colorspace RGB   				CLSR	H	1 long (4 bytes) //0 = unset, 1 = cgRGB, 2 = vsRGB
TAG_COLORSPACE_FTR		= MAKETAG('C','L','S','F'),		//Filter 422to444  				CLSF	H	1 long (4 bytes) //0 = off, 1 = on
TAG_COLORSPACE_LIMIT	= MAKETAG('C','L','S','L'),		//Limit YUV Broadcast levels	CLSL	H	1 long (4 bytes) //0 = off, 1 = on
TAG_VIDEO_CHANNELS		= MAKETAG('V','C','H','N'),		//No. streams(3D)  				VCHN	H	1 long (4 bytes) //0 = unset, 1 = standard, 2 = two stream like 3D, 3 = 3 video streams.
TAG_VIDEO_CHANNEL_GAP	= MAKETAG('V','C','G','P'),		//Pixel gap between channels	VCGP	L	1 long (4 bytes) //0 = default
TAG_LIMIT_YUV			= MAKETAG('L','Y','U','V'),		//Limit range YUV  				LYUV	H	1 long (4 bytes) //0 = no change, 1 = convert full range 0-255 to 16-235.
TAG_CONV_601_709		= MAKETAG('C','V','6','7'),		//Color space fix 4 CanonDLSRs	CV67	H	1 long (4 bytes) //0 = no change, 1 = convert 601 to 709 upon encode.
TAG_CHANNEL				= MAKETAG('C','H','N','L'),		//channels number  				CHNL	L	1 long (4 bytes) //1 - tag left, 2 tag right, etc.
TAG_CHANNEL_FLIP		= MAKETAG('C','H','F','P'),		//Flip channels    				CHFP	H	1 long (4 bytes) //(1(Horiz)|2(Vert))<<channel num.  0 = no flip, 1 = h flip channel 1, 4 h flip channel 2, 0xf v/h flip channels 1 & 2, etc
TAG_CHANNEL_SWAP		= MAKETAG('C','S','W','P'),		//Swap L & R channels    		CSWP	H	1 long (4 bytes) //0 = no swap, 1 = swapped
TAG_CHANNELS_ACTIVE		= MAKETAG('C','A','C','T'),		//channels on mask 				CACT	H	1 long (4 bytes) //0,1 = chnl 1, 2 = chn 2, 3 = 1+2 channels, etc.
TAG_CHANNELS_MIX		= MAKETAG('C','M','I','X'),		//channel mix type 				CMIX	H	1 long (4 bytes) //0 = normal single channel, 1 = stacked half height, 2 = sibe_by-side, 3 = fields, 16-21 = anaglypth
TAG_CHANNELS_MIX_VAL	= MAKETAG('C','M','V','L'),		//channel mix valu 				CMVL	H	1 long (4 bytes) //dependent on type, could be a dissolve percentage or PIP control, etc.
TAG_LENS_GOPRO			= MAKETAG('L','G','P','R'),		//Apply GoPro Lens Curve     	LGPR	H	1 long (4 bytes) //0 - Rectilinear, 1 - GoPro curve
TAG_LENS_SPHERE			= MAKETAG('L','S','P','H'),		//Use Image Sphere           	LSPH	H	1 long (4 bytes) //0 - Planar image, 1 - spherical image
TAG_LENS_FILL			= MAKETAG('L','F','I','L'),		//Fill background            	LFIL	H	1 long (4 bytes) //0 - fill with black, 1 - pattern fill
TAG_LENS_STYLE			= MAKETAG('L','S','T','L'),		//Lens Style	            	LSTL	H	1 long (4 bytes) //0 - no magic, ...
TAG_LENS_SRC_PARAMS		= MAKETAG('L','S','R','C'),		//Lens parameters           	LSRC	f	6 floats (24 bytes) //0 - no magic, ...
TAG_LENS_DST_PARAMS		= MAKETAG('L','D','S','T'),		//Lens Style	            	LDST	f	6 floats (24 bytes) //0 - no magic, ...
TAG_PREFORMATTED_3D		= MAKETAG('P','F','3','D'),		//Preformatted 3D (like SBS)	PF3D	H	1 long (4 bytes) //some codes as CMIX, although only 1 = stacked half height, 2 = sibe_by-side, 3 = fields, make sense.
TAG_MIX_DOWN_ALPHA		= MAKETAG('M','I','X','A'),		//mix alpha channel with RGB	MIXA	H	1-2 long (4-8 bytes) //0xRRGGBB01 and 0xRRGGBB01 for checkerboard. 0 = no mixdown.
TAG_GHOST_BUST_LEFT		= MAKETAG('G','H','T','L'),		//ghost bust left valu 			GHTL	H	1 long (4 bytes) //0 = none, 0xffff max (negative)
TAG_GHOST_BUST_RIGHT	= MAKETAG('G','H','T','R'),		//ghost bust right valu			GHTR	H	1 long (4 bytes) //0 = none, 0xffff max (negative)
TAG_CHANNEL_QUALITY 	= MAKETAG('C','Q','U','L'),		//channel quality  				CQUL	H	1 long (4 bytes) //Stereo/3D sources. 1- best quality, 2-next best.  Used to select a channel upon decode or set a quality on encode (as a channel mask (1|2)<<channel num)
TAG_HORIZONTAL_OFFSET	= MAKETAG('H','O','F','F'),		//H.Convergence    				HOFF	f	1 float (range -1.0 to 1.0)
TAG_VERTICAL_OFFSET 	= MAKETAG('V','O','F','F'),		//V.Convergence    				VOFF	f	1 float (range -1.0 to 1.0)
TAG_ROTATION_OFFSET 	= MAKETAG('R','O','F','F'),		//R.Convergence    				ROFF	f	1 float (range -0.1 to 0.1)
TAG_LICENSEE			= MAKETAG('L','C','N','S'),		//Name of licensee 				LCNS	c	(n bytes) The username of the license holder
TAG_CPU_MAX				= MAKETAG('C','P','U','M'),		//Limit to X cores 				CPUM	h	1 long hidden -- limit to 'x' cores on decoder, 0 - unset (use all)
TAG_AFFINITY_MASK		= MAKETAG('A','F','F','I'),		//Affinity Mask    				AFFI	h	1 long hidden -- 0 - unset (use all)
TAG_IGNORE_DATABASE 	= MAKETAG('I','G','N','R'),		//Not read disk DB 				IGNR	h	1 long hidden -- non-zero, don't read any Database data form disk
TAG_FORCE_DATABASE  	= MAKETAG('F','O','R','C'),		//Always RD dsk DB 				FORC	H	1 long  -- non-zero, always read any Database data form disk
TAG_UPDATE_LAST_USED  	= MAKETAG('U','P','L','T'),		//Update registry current GUID 	UPLT	H	1 long  -- default active, 0 to disable.
TAG_CALIBRATE			= MAKETAG('C','A','L','I'),		//internal use     				CALI	H	1 long
TAG_FRAME_MASK			= MAKETAG('M','A','S','K'),		//frame mask       				MASK	f	8xChannels  float coords, toprgt, botRgt, botLft 0.0 to 1.0, repeated for extra channels.
TAG_NATURAL_FRAMING 	= MAKETAG('N','F','R','M'),		//natural framing       		NFRM	f	picture aspect ratio, e.g. 16by9 = 1.7777
TAG_FRAME_DIFF_ZOOM		= MAKETAG('D','Z','O','M'),		//frame diff zoom       		DZOM	f	1xChannel float 0.0 is unity, repeated for extra channels.
TAG_FRAME_ZOOM			= MAKETAG('Z','O','O','M'),		//frame zoom       				ZOOM	f	1xChannel float 1.0 is unity, repeated for extra channels.
TAG_FRAME_KEYSTONE		= MAKETAG('K','Y','S','T'),		//frame keystone   				KYST	f	1xChannel float 0.0 is unity, repeated for extra channels, -0.1 to 0.1.
TAG_FRAME_TILT			= MAKETAG('T','I','L','T'),		//frame Tilt	   				TILT	f	1xChannel float 0.0 is unity, repeated for extra channels, -0.1 to 0.1.
TAG_AUTO_ZOOM			= MAKETAG('A','T','Z','M'),		//frame zoom       				ATZM	H	If non-zero set FramingFlag |= 1
TAG_FRAME_OFFSET_X		= MAKETAG('O','F','F','X'),		//frame offset x   				OFFX	f	1xChannel float 0.0 is center, repeated for extra channels.
TAG_FRAME_OFFSET_Y		= MAKETAG('O','F','F','Y'),		//frame offset y   				OFFY	f	1xChannel float 0.0 is center, repeated for extra channels.
TAG_FRAME_OFFSET_R		= MAKETAG('O','F','F','R'),		//frame offset rotaiton   		OFFR	f	1xChannel float 0.0 is center, repeated for extra channels.
TAG_FRAME_OFFSET_F		= MAKETAG('O','F','F','F'),		//frame offset fisheye   		OFFF	f	1xChannel float 0.0 is center, repeated for extra channels.
TAG_FRAME_HSCALE		= MAKETAG('O','F','F','H'),		//frame offset Horizontal Scale	OFFH	f	1xChannel float 0.0 is center, repeated for extra channels.
TAG_FRAME_HDYNAMIC		= MAKETAG('O','F','F','D'),		//frame offset Dynamic Scale   	OFFD	f	1xChannel float 0.0 is center, repeated for extra channels.
TAG_FRAME_DYNCENTER		= MAKETAG('O','F','F','C'),		//frame offset Dynamic Scale   	OFFC	f	1xChannel float 0.0 is center, repeated for extra channels.
TAG_FRAME_DYNWIDTH		= MAKETAG('O','F','F','W'),		//frame offset Dynamic Scale   	OFFW	f	1xChannel float 0.0 is center, repeated for extra channels.
TAG_MASK_LEFT			= MAKETAG('M','S','K','L'),		//floading window left   		MSKL	f	window mask 0.0 is no mask.
TAG_MASK_RIGHT			= MAKETAG('M','S','K','R'),		//floading window right   		MSKR	f	window mask 0.0 is no mask.
TAG_PROXY_COPY			= MAKETAG('P','R','X','Y'),		//proxy for another CFHD file	PRXY	H	1 -  proxy.
TAG_CALLING_APP			= MAKETAG('S','A','P','P'),		//calling App 4cc				SAPP	H	1 long (4 byte 4cc of calling application)
TAG_SOURCE_PIXEL_FMT	= MAKETAG('S','F','M','T'),		//format of pixel passed to enc SFMT	H	1 long (4 byte 4cc of pixel format)

TAG_EYE_DELTA_1			= MAKETAG('C','O','L','1'),   	//eye difference information  	COL1	-	x bytes of CineForm metadata
TAG_EYE_DELTA_2			= MAKETAG('C','O','L','2'),   	//eye difference information  	COL2	-	x bytes of CineForm metadata

TAG_SET_EYE				= MAKETAG('S','E','T','E'),  	//start writing eye channel x	SETE	L	1 long to indicate the eye number the following metadata is applied  0-both, 1-left, 2-right

TAG_CLIP_HASH			= MAKETAG('H','A','S','H'),  	//CRC for all active metadata	HASH	H	1 long -- read only, useful for controlling frame caches, if the HASH changes the Active Metadata has changed.
TAG_SMART_RENDER_OK		= MAKETAG('S','R','O','K'),  	//only Smart Render some clips	SMOK	H	1 long -- return 1 is not Active Metadata is modify the image, otherwise 0.

// tags for FirstLight
TAG_GAINS				= MAKETAG('G','A','I','N'),		//early first light version

TAG_SYNC_3D				= MAKETAG('S','Y','N','C'),		//3D sync frame					SYNC	L	frame number of 3D sync
TAG_HISTOGRAM			= MAKETAG('H','I','S','T'),		//Histogram on     				HIST	H	non-zero is active.
TAG_OVERLAYS			= MAKETAG('O','V','E','R'),		//text overlay     				OVER	H	non-zero is active.
TAG_TOOLS				= MAKETAG('T','O','O','L'),		//tools overlay     			TOOL	H	non-zero is active.
TAG_WAVEFORM			= MAKETAG('W','V','F','M'),		//Waveform	     				WVFM	H	non-zero is active.
TAG_VECTORSCOPE			= MAKETAG('V','T','S','P'),		//vectorscope    				VTSP	H	non-zero is active.

TAG_DPX_FILE			= MAKETAG('D','P','X','F'),		//DPX File Information     		DPXF	-	x bytes //
TAG_DPX_INFO			= MAKETAG('D','P','X','I'),		//DPX Image_Information    		DPXI	-	x bytes //
TAG_DPX_ORIENT			= MAKETAG('D','P','X','O'),		//DPX Image_Orientation    		DPXO	-	x bytes //
TAG_DPX_MOTION			= MAKETAG('D','P','X','M'),		//DPX Motion_Picture_Film  		DPXM	-	x bytes //
TAG_DPX_TV				= MAKETAG('D','P','X','T'),		//DPX Television_Header    		DPXT	-	x bytes //
TAG_DPX_USER			= MAKETAG('D','P','X','U'),		//DPX User Data            		DPXU	-	x bytes //
TAG_DPX_FRAME_POSITION	= MAKETAG('D','P','F','N'),		//DPX Frame Number				DPFN	L	1 long frame number for DPX file
TAG_DPX_FILE_FIELD		= MAKETAG('D','P','X','X'),		//DPX file from ALE file		DPXX	c	x bytes from ALE file

TAG_DISPLAY_METADATA	= MAKETAG('D','S','P','m'),		//Burn-in Display parameters	DSPm	-	x bytes of CineForm metadata
TAG_DISPLAY_SCRIPT		= MAKETAG('D','S','C','R'),		//Embedded script text			DSCR	c	x bytes of metadata control script
TAG_DISPLAY_SCRIPT_FILE = MAKETAG('D','S','C','P'),		//Script file path				DSCP	c	x bytes of full path to a metadata control script
TAG_DISPLAY_ACTION_SAFE	= MAKETAG('D','A','S','F'),		//Draw Action safe markers		DASF	f	2 floats for width and height % range 0 to 0.5, e.g. 0.05,0.05
TAG_DISPLAY_TITLE_SAFE	= MAKETAG('D','T','S','F'),		//Draw Title safe markers		DTSF	f	2 floats for width and height % range 0 to 0.5, e.g. 0.1,0.1
TAG_DISPLAY_OVERLAY_SAFE= MAKETAG('D','O','S','F'),		//Overlay safe region			DOSF	f	2 floats for width and height % range 0 to 0.5, e.g. 0.1,0.1

//many of the TAG_DISPLAY_xxx types can in used outside of a DSPm for a global default
TAG_DISPLAY_TAG			= MAKETAG('D','T','A','G'),		//TAG to display				DTAG	T	4 bytes, FOURCC value of the tag to display
TAG_DISPLAY_FREEFORM	= MAKETAG('D','F','F','M'),		//freeform to display			DFFM	c	x bytes, string name / value metadata
TAG_DISPLAY_FONT		= MAKETAG('D','F','N','T'),		//Font to use					DFMT	c	x bytes, name of font to load
TAG_DISPLAY_FONTSIZE	= MAKETAG('D','F','S','Z'),		//Font size						DFSZ	f	1 float, size of font 0 to 0.1, where 1 is the display height
TAG_DISPLAY_JUSTIFY		= MAKETAG('D','J','S','T'),		//Justifaction for fonts & gfx	DJST	H	4 bytes, Justification flags JUSTIFY_CENTER = 0, JUSTIFY_LEFT = 1, JUSTIFY_RIGHT = 2, JUSTIFY_TOP = 4, JUSTIFY_TL = 5, JUSTIFY_TR = 6, JUSTIFY_BOTTOM = 8, JUSTIFY_BL = 9, JUSTIFY_BR = 10
TAG_DISPLAY_TIMING_IN 	= MAKETAG('D','T','I','N'),		//Timing in frame number    	DTIN	L	4 bytes, In frame
TAG_DISPLAY_TIMING_DUR 	= MAKETAG('D','T','D','R'),		//Timing duration in frames  	DTDR	L	4 bytes, Duration frames
TAG_DISPLAY_T_FADEIN 	= MAKETAG('D','T','F','I'),		//Timing fade in frames 		DTFI	L	4 bytes, in fade frames
TAG_DISPLAY_T_FADEOUT	= MAKETAG('D','T','F','O'),		//Timing fade out frames 		DTFO	L	4 bytes, out fade frames
TAG_DISPLAY_FCOLOR		= MAKETAG('D','F','C','L'),		//Foreground color				DFCL	f	4 floats, R,G,B,A from 0.0 to 1.0 -- font color
TAG_DISPLAY_BCOLOR		= MAKETAG('D','B','C','L'),		//Background color				DBCL	f	4 floats, R,G,B,A from 0.0 to 1.0 -- font background
TAG_DISPLAY_SCOLOR		= MAKETAG('D','S','C','L'),		//Stroke color					DSCL	f	4 floats, R,G,B,A from 0.0 to 1.0 -- font background
TAG_DISPLAY_STROKE_WIDTH= MAKETAG('D','S','W','D'),		//Stroke width					DSWD	f	1 float in pixel size 1.0 is one pixel, 1.5 etc.
TAG_DISPLAY_XPOS		= MAKETAG('D','X','P','S'),		//x horizontal position			DXPS	f	1 float 0 to screen_width/screen_height (1.7777 for 16:9)
TAG_DISPLAY_YPOS		= MAKETAG('D','Y','P','S'),		//y vertical position			DYPS	f	1 float 0 to 1.0
TAG_DISPLAY_XYPOS		= MAKETAG('D','P','O','S'),		//x,y position					DPOS	f	2 floats (same as DXPS,DYPS)
TAG_DISPLAY_FORMAT		= MAKETAG('D','F','M','T'),		//format string					DFMT	c	x bytes of C formating string "Timecode: %s" or "RGB Gain: (%1.2f,%1.2f,%1.2f)"
TAG_DISPLAY_PNG_PATH	= MAKETAG('D','P','N','G'),		//PNG path						DPGP	c	x bytes of full path of PNG to load.
TAG_DISPLAY_PNG_SIZE	= MAKETAG('D','P','N','S'),		//PNG size						DPGS	f	2 float PNG size, 0->0.2, 0->0.2, width and height, as 20% of screen height
TAG_DISPLAY_PARALLAX	= MAKETAG('D','P','L','X'),		//3D parallax					DPLX	l	pixel parallax, neative is in front of the screen plane.

TAG_CONTROL_POINT		= MAKETAG('C','T','L','p'),		//Add a timecode metadat event	CTLp	-	x bytes of CineForm metadata, first tag within is the trigger point, normally TIMC or UFRM
 CP_3D					= MAKETAG('C','P','3','D'),		//within CTLp for 3D correction	CP3D	-	x bytes of CineForm metadata, first tag within is the trigger point, normally TIMC or UFRM
 CP_WHITE_BALANCE		= MAKETAG('C','P','W','B'),		//within CTLp for white balance	CPWB	-	x bytes of CineForm metadata, first tag within is the trigger point, normally TIMC or UFRM
 CP_PRIMARIES			= MAKETAG('C','P','P','R'),		//within CTLp for primaries		CPPR	-	x bytes of CineForm metadata, first tag within is the trigger point, normally TIMC or UFRM
 CP_FRAMING				= MAKETAG('C','P','F','R'),		//within CTLp for framing		CPFR	-	x bytes of CineForm metadata, first tag within is the trigger point, normally TIMC or UFRM

TAG_ATTACH_SPI_PATH		= MAKETAG('A','S','P','I'),		//Path to SPI file				ASPI	c	x bytes of full path of SPI to load.
TAG_SPI_OFFSET_TC		= MAKETAG('S','P','I','O'),		//SPI TC offset					SPIO	c	11 bytes of TC 00:00:00:00
TAG_SPI_PARALLAX		= MAKETAG('S','P','I','P'),		//SPI Parallax					SPIP	l	pixel parallax, neative is in front of the screen plane.

// Values added to support Avid ALE metadata

TAG_AUX_INK_END			= MAKETAG('A','N','K','E'),	    //Aux Ink out point for clip    ANKE    c	x chars - depends on format of aux ink
TAG_AUX_INK_FILM_TYPE	= MAKETAG('A','N','K','F'),		//Aux Ink film type				ANKF	c	x chars - defines counting format of aux ink
TAG_AUX_INK_EDGE		= MAKETAG('A','N','K','G'),		//Aux ink edge type				ANKG	c	x chars - defines how Aux Ink number displayed
TAG_AUX_INK_NUMBER		= MAKETAG('A','N','K','N'),		//Aux Ink						ANKN	c	x chars - Aux Ink: feet & frame or frame with prefix
TAG_ASC_SOP				= MAKETAG('A','S','C','M'),		//ASC_SOP						ASCM	f	9 floats slope, offset power for R,G,B
TAG_ASC_SATURATION		= MAKETAG('A','S','C','S'),		//ASC_SAT						ASCS	f	1 float for saturation
TAG_AUX_TC1				= MAKETAG('A','T','C','1'),		//Aux timecode 1				ATC1	c	11 chars (00:00:00:00)
TAG_AUX_TC2				= MAKETAG('A','T','C','2'),		//Aux timecode 2				ATC2	c	11 chars (00:00:00:00)
TAG_AUX_TC3				= MAKETAG('A','T','C','3'),		//Aux timecode 3				ATC3	c	11 chars (00:00:00:00)
TAG_AUX_TC4				= MAKETAG('A','T','C','4'),		//Aux timecode 4				ATC4	c	11 chars (00:00:00:00)
TAG_AUX_TC5				= MAKETAG('A','T','C','5'),		//Aux timecode 5				ATC5	c	11 chars (00:00:00:00)
TAG_AUDIO_FILE			= MAKETAG('A','U','D','F'),		//Audio file name				AUDF	c	x chars from ALE file
TAG_AUDIO				= MAKETAG('A','U','D','I'),		//Audio							AUDI	c	x chars from ALE file
TAG_PULLDOWN_CADENCE	= MAKETAG('C','A','D','N'),		//Pulldown cadance				CADN	H	4 bytes, cadence type 0=NONE=2:2:2:2, 1=NORM=2:3:2:3, 2=ADVA=2:3:3:2
TAG_CAMERA_ROLL			= MAKETAG('C','A','M','L'),		//Camera roll					CAML	c	x bytes Camera Roll from ALE
TAG_DISK				= MAKETAG('D','I','S','K'),		//Disk name						DISK	c	x bytes: XDCam or XDCam-HD Disk label
TAG_INK_DURATION		= MAKETAG('I','N','K','D'),		//Ink duration					INKD	c	x bytes: depends on settings of Ink edge and Ink film clip duration
TAG_INK_END				= MAKETAG('I','N','K','E'),		//Ink end						INKE	c	x bytes: depends on settings of Ink edge and Ink film - last frame of clip
TAG_INK_FILM			= MAKETAG('I','N','K','F'),		//Ink film type					INKF	c	x bytes: method of counting ink
TAG_INK_EDGE			= MAKETAG('I','N','K','G'),		//Ink edge type					INKG	c	x bytes: Prefix and footage/frame count of ink
TAG_INK_NUMBER			= MAKETAG('I','N','K','N'),		//Ink number of first frame		INKN	c	x bytes: Number of the first frame of the clip
TAG_KN_DURATION			= MAKETAG('K','N','D','U'),		//KN duration					KNDU	c	x bytes: Duration of the clip in KN
TAG_KN_EDGE				= MAKETAG('K','N','E','D'),		//KN Gauge						KNED	S	1 short 0=35.3 (35mm, 3perf) 1=35.4 2=16.20 (16mm, 20 frame)

TAG_KN_END				= MAKETAG('K','N','E','N'),		//KN End						KNEN	c	x bytes: KN of the last frame of clip
TAG_KN_NUMBER			= MAKETAG('K','N','N','U'),		//KN Number						KNNU	c	x bytes: KN of frame
TAG_KN_START			= MAKETAG('K','N','S','T'),		//KN Start						KNST	c	x bytes: KN of first frame of clip
TAG_LABROLL				= MAKETAG('L','A','B','R'),		//Labroll						LABR	c	x bytes: Labroll of clip
TAG_CLIPNAME			= MAKETAG('N','A','M','E'),		//Name							NAME	c	x bytes: Name of clip from ALE file
TAG_PULLIN				= MAKETAG('P','U','L','I'),		//Pullin						PULI	c	1 byte A,B,X,C,D Cadence setting for first frame of clip
TAG_PULLOUT				= MAKETAG('P','U','L','O'),		//Pullout						PULO	c	1 byte Cadance of last frame of clip
TAG_SHOT_DURATION		= MAKETAG('S','D','U','R'),		//Duration						SDUR	c	11 bytes (00:00:00:00) Shot duration from ALE
TAG_SHOT_END			= MAKETAG('S','E','N','D'),		//Shot end						SEND	c	11 bytes (00:00:00:00) Timecode of last frame in clip +1 (EDL style)
TAG_SOUNDROLL			= MAKETAG('S','O','U','N'),		//Soundroll name				SOUN	c	x bytes Soundroll
TAG_TAPE				= MAKETAG('T','A','P','E'),		//Tape name						TAPE	c	x bytes: Tape name from ALE
TAG_TC24				= MAKETAG('T','C','2','4'),		//TC 24							TC24	c	11 bytes (00:00:00:00) timecode at 24 fps
TAG_TC25				= MAKETAG('T','C','2','5'),		//TC 25							TC25	c	11 bytes (00:00:00:00) timecode at 25 fps
TAG_TC24A				= MAKETAG('T','C','4','A'),		//Aux TC 24						TC2A	c	11 bytes (00:00:00:00) auxiliary timecode at 24 fps
TAG_TC30				= MAKETAG('T','C','3','0'),		//TC 30							TC30	c	11 bytes (00:00:00:00) timecode at 30 fps
TAG_TC30NP				= MAKETAG('T','C','3','N'),		//TC 30NP						TC3N	c	11 bytes (00:00:00:00) timecode at 30 fps no pulldown 24fps to NTSC 2:2:2:2
TAG_TC25PULLDOWN		= MAKETAG('T','C','5','P'),		// TC 25PD						TC5P	c	11 bytes (00:00:00:00) timecode for PAL 25fps with pulldown field inserted every 12 frames
TAG_TC60				= MAKETAG('T','C','6','0'),		//TC 60							TC60	c	11 bytes (00:00:00:00) timecode at 60 fps
TAG_TIMECODE_FILM		= MAKETAG('T','I','M','F'),		//Film TC						TIMF	c	11 bytes (00:00:00:00)
TAG_TIMECODE_SOUND		= MAKETAG('T','I','M','S'),		//Sound TC						TIMS	c	11 bytes (00:00:00:00)
TAG_TRACKS				= MAKETAG('T','R','A','K'),		//Tracks						TRAK	c	x bytes: VA1A2A3.. or V1V2A1A2... for 3D
TAG_TRANSFER			= MAKETAG('T','R','N','S'),		//Transfer						TRNS	c	x bytes: from punch frame up to 32 AN followed by frame "-000000" 6 digit frame number
TAG_UNCPATH				= MAKETAG('U','N','C','P'),		//UNC Path						UNCP	c	x bytes: UNC path from ALE file
TAG_VFX					= MAKETAG('V','F','X','F'),		//VFX name and frame			VFXF	c	x bytes: frame based counter with 32 An prefix and 6 frame counter (32-6)
TAG_VFX_REEL			= MAKETAG('V','F','X','R'),		//VFX Reel name					VFXR	c	x bytes VFX reel name

// lower case last character indicates items can have multiple metadata entries
TAG_DIRECTOR			= MAKETAG('D','R','T','r'),    //Director Name   				DRTr	c	x chars (variable length)
TAG_PRODUCER 			= MAKETAG('P','R','O','d'),    //Producer Name   				PROd	c	x chars (variable length)
TAG_DIR_PHOTOGR 		= MAKETAG('D','R','P','t'),    //D.P. Name       				DRPt	c	x chars (variable length)
TAG_SHOT_TYPE 			= MAKETAG('S','H','T','y'),    //Shot Type       				SHTy	c	x chars (variable length)
TAG_PRODUCTION 			= MAKETAG('P','R','D','l'),    //Production length  			PRDl	c	x chars (variable length)
TAG_LOCATION        	= MAKETAG('L','O','C','n'),    //Location        				LOCn	c	x chars (variable length)
TAG_KEYWORD         	= MAKETAG('K','W','R','d'),    //Keyword         				KWRd	c	x chars (variable length)
TAG_SCRIPT_PAGE			= MAKETAG('S','C','P','g'),    //Scriptpage num  				SCPq	L	4 bytes as unsigned integer
TAG_MODIFIER_NUMBER 	= MAKETAG('M','D','F','r'),    //Modifier number (deprecated)	MDFr	S	1 short (2 bytes in 4) 0 = NONE, 1 = 'A', 2 = 'B', take modifier

TAG_CAMERA_MODEL 		= MAKETAG('C','M','D','L'),    //Camera Model     				CMDL	c	variable length
TAG_CAMERA_ID 			= MAKETAG('C','M','I','d'),	   //Serial or camera ID 			CMId	c	x chars (variable length)

TAG_STEREO_SHIFT		= MAKETAG('S','M','V','D'),		// Vertical displacement		SMVD	f
TAG_STEREO_ROTATION		= MAKETAG('S','M','C','R'),		// Rotation about center		SMCR	f
TAG_STEREO_SIGNIFICANCE	= MAKETAG('S','M','S','G'),		// Significance					SMSG	f

TAG_GOPRO_FIRMWARE		= MAKETAG('F','I','R','M'),		// Camera firmare number		FIRM	c	string (variable length)
TAG_GOPRO_SENSOR_ID		= MAKETAG('S','N','I','D'),		// Camera sensor ID				SNID	H	n x 32-bit id
TAG_GOPRO_SETTINGS		= MAKETAG('G','P','S','T'),		// Camera sensor ID				GPST	H	n x 32-bit settings flags

TAG_FRAMERATE			= MAKETAG('F','R','M','R'),		// Frame rate & scale			FRMR	L	2 longs (8-byes), rate and scale
TAG_PRESENTATION_WIDTH	= MAKETAG('P','R','S','W'),		//Presentation Width			PRSW	L	presentation width, independent to the encoded width (4 bytes)
TAG_PRESENTATION_HEIGHT	= MAKETAG('P','R','S','H'),		//Presetnation Hieght			PRSH	L	presentation height, independent to the encoded height (4 bytes)


// REGN/REGV and TAGN/TAGV are pairs that can be mulitple times.
TAG_REGISTRY_NAME 		= MAKETAG('R','E','G','N'),    //Registry name    				REGN	c	variable length
TAG_REGISTRY_VALUE 		= MAKETAG('R','E','G','V'),    //Registry value   				REGV	L/c	variable length for a string or only DWORD

// Free form third party data in TAG NAME/VALUE pairs
TAG_NAME 				= MAKETAG('T','A','G','N'),    //Registry name    				TAGN	c	variable length
TAG_VALUE 				= MAKETAG('T','A','G','V'),    //Registry value  				TAGV	any	variable length for a string or only DWORD

// Third party can create their own FOURCC codes as long as they are completely lower case

} MetadataTag;

#endif // CFHD_METADATA_TAGS_H
