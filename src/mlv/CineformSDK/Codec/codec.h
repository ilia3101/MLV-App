/*! @file codec.h

*  @brief Definitions and data structures that are common to the decoder and encoder.
*
*  Note that some data structures that are used only be the decoder, such as the
*  decoder state information (DECODER), are defined in this file even though the
*  data structures are not used by the encoder.
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
#ifndef _CODEC_H
#define _CODEC_H

#include "config.h"
#include "timing.h"
#include "dump.h"
#include "image.h"
#include "wavelet.h"
#include "bitstream.h"
#include "vlc.h"
#include "codebooks.h"
#include "frame.h"
#include "color.h"
#include "buffer.h"
#include "error.h"

#include "../Common/AVIExtendedHeader.h"
#include <stdio.h>
#include <time.h>
#include <stdint.h>

#if _THREADED
#include "thread.h"
#endif

// Compile time switches that control encoding and decoding
#define _ENCODE_FAST_RUNS		0		// Use fast runs encoding routine
#define _ENCODE_LONG_RUNS		1		// Encode runs of zeros across rows
#define _DECODE_FRAME_8U		1		// Decode frames to 8-bit pixels
#define _DECODE_LOWPASS_16S		1		// Use 16 bit lowpass coefficients
#define _DECODE_HIGHPASS_8S		0		// Use 8 bit highpass coefficients

#define DECODE_RESOLUTION_FULL		0
#define DECODE_RESOLUTION_HALF		1
#define DECODE_RESOLUTION_QUARTER	2

#define _DEQUANTIZE_IN_FSM		1

#define _FIELDPLUS_TRANSFORM	1

#ifndef _ENCODE_CHROMA_ZERO
#define _ENCODE_CHROMA_ZERO		0		// Encode chroma as difference from zero
#endif

#if _ENCODE_CHROMA_ZERO
#ifndef _CODEC_CHROMA_OFFSET
#define _CODEC_CHROMA_OFFSET	128		// Default chroma offset applied during encoding
#endif
#else
#ifndef _CODEC_CHROMA_OFFSET
#define _CODEC_CHROMA_OFFSET	0		// Default chroma offset applied during encoding
#endif
#endif

#if _ENCODE_CHROMA_ZERO
#ifndef _ENCODE_CHROMA_OFFSET
#define _ENCODE_CHROMA_OFFSET	1		// Enable use of chroma offset during encoding
#endif
#else
#ifndef _ENCODE_CHROMA_OFFSET
#define _ENCODE_CHROMA_OFFSET	0		// Disable use of chroma offset during encoding
#endif
#endif

#if _DEBUG
#define ENCODER_THREAD_TIMEOUT	INFINITE
#else
#define ENCODER_THREAD_TIMEOUT	100
#endif

#define _SIF					1

#ifndef _CODEC_TAGS
#define _CODEC_TAGS				1
#endif

#ifndef _CODEC_MARKERS
#define _CODEC_MARKERS			1
#endif

// Options that control the format of run length encoding

#define RUNS_ROWEND_MARKER		0	// Does each row of runs end with a marker?

#if (_ENCODE_FAST_RUNS && _ENCODE_LONG_RUNS)
#error Must set at most one of the run length encoding options
#endif

//typedef struct zerotree ZEROTREE;	// Forward references
//typedef struct iframe IFRAME;
//typedef struct group GROUP;
struct iframe;
struct group;

#define CODEC_VERSION_MAJOR		0	// Version number major.minor.revision
#define CODEC_VERSION_MINOR		1
#define CODEC_VERSION_REVISION	0

#if _DEBUG
#define CODEC_VERSION_SUFFIX "(debug)"
#elif _TIMING
#define CODEC_VERSION_SUFFIX "(timing)"
#else
#define CODEC_VERSION_SUFFIX "(release)"
#endif

#define WAVELET_NUM_LEVELS		3	// Number of wavelet transforms

#define BITS_PER_PIXEL			8	// Bits per gray pixel
#define BITS_PER_SIGCODE		2	// Bits per significance map code
#define BITS_PER_COEFFICIENT	8	// Bits per highpass coefficient

#define CODEC_MAX_LEVELS		8	// Maximum number of pyramid levels
#define CODEC_MAX_HIGHBANDS		3	// Maximum highpass bands per level

#define CODEC_MAX_BANDS (CODEC_MAX_HIGHBANDS + 1)

// Maximum number of bands in the wavelet transform
#define CODEC_MAX_SUBBANDS	(1 + CODEC_MAX_LEVELS * CODEC_MAX_HIGHBANDS)

// Number of highpass bands in the spatial wavelet
#define CODEC_NUM_BANDS			3

// Number of frames per group of frames (GOP)
#define CODEC_GOP_LENGTH		2

// Maximum number of channels
#define CODEC_MAX_CHANNELS		4 // //DAN06302004


#define PEAK_THRESHOLD			250
#define DIFFERENCE_TEMPORAL_LL	0  // difference temporal lowlow band

#if _RECURSIVE
#define DIFFERENCE_CODING	0
#else
#define DIFFERENCE_CODING	1
#endif

#define alphacompandDCoffset 256 // 256
#define alphacompandGain	 9400 // 588<<4 = 9408

#define MAX_ENCODE_DATADASE_LENGTH		4096
#define MAX_DECODE_DATADASE_LENGTH		65536

#define MIDPOINT_PREQUANT		1  // Add half the quantization factor before quantizing

enum band_encoding {
	BAND_ENCODING_ZEROTREE = 1,
	BAND_ENCODING_CODEBOOK,
	BAND_ENCODING_RUNLENGTHS,
	BAND_ENCODING_16BIT,
	BAND_ENCODING_LOSSLESS
};

#if _CODEC_TAGS

#define CODEC_TAG_SIZE		16			// Size of a codec tag (in bits)
#define CODEC_TAG_MASK		0xFFFF		// Mask for usable part of tag or value

//#define CODEC_TAG_OPTIONAL	(1 << 15)	// Optional tag bit

#ifndef NEG
#define NEG(x)			(-(x))		// Negate the tag value to toggle optional/mandatory
#define OPTIONALTAG(x)	(-(x))		// Negate the tag value to toggle optional/mandatory
#endif

#define _CODEC_GROUP_EXTENSION	1		// Write the group header extension
#define _CODEC_SAMPLE_FLAGS		1		// Write the sample flags

#define MAX_CHUNK_SIZE	0xffff

typedef enum codec_tag
{
	/***** Mandatory tags defined in the first release *****/

	CODEC_TAG_ZERO = 0,				// Unused
	CODEC_TAG_SAMPLE,				// Type of sample
	CODEC_TAG_INDEX,				// Sample index table
	CODEC_TAG_ENTRY,				// Entry in sample index
	CODEC_TAG_MARKER,				// Bitstream marker


	/***** Tags used for encoding the video sequence header *****/

	CODEC_TAG_VERSION_MAJOR,		// Version (5)
	CODEC_TAG_VERSION_MINOR,		// Minor version number
	CODEC_TAG_VERSION_REVISION,		// Revision number
	CODEC_TAG_VERSION_EDIT,			// Edit number
	CODEC_TAG_SEQUENCE_FLAGS,		// Video sequence flags


	/***** Tags used for encoding a wavelet group *****/

	CODEC_TAG_TRANSFORM_TYPE,		// Type of transform (10)
	CODEC_TAG_NUM_FRAMES,			// Length of group of frames
	CODEC_TAG_NUM_CHANNELS,			// Number of channels in the transform
	CODEC_TAG_NUM_WAVELETS,			// Number of wavelets in the transform
	CODEC_TAG_NUM_SUBBANDS,			// Number of encoded subbands
	CODEC_TAG_NUM_SPATIAL,			// Number of spatial levels
	CODEC_TAG_FIRST_WAVELET,		// Type of the first wavelet
	CODEC_TAG_CHANNEL_SIZE,			// Number of bytes in each channel
	CODEC_TAG_GROUP_TRAILER,		// Group trailer and checksum


	/***** Tags used for encoding a wavelet frame *****/

	CODEC_TAG_FRAME_TYPE,			// Type of frame (19)
	CODEC_TAG_FRAME_WIDTH,			// Width of the frame
	CODEC_TAG_FRAME_HEIGHT,			// Height of the frame
	CODEC_TAG_FRAME_FORMAT,			// Format of the pixels
	CODEC_TAG_FRAME_INDEX,			// Position of frame within the group
	CODEC_TAG_FRAME_TRAILER,		// Frame trailer


	/***** Tags used for encoding the lowpass wavelet bands *****/

	CODEC_TAG_LOWPASS_SUBBAND,		// Subband number of the lowpass band (25)
	CODEC_TAG_NUM_LEVELS,			// Number of wavelet levels
	CODEC_TAG_LOWPASS_WIDTH,		// Width of the lowpass band
	CODEC_TAG_LOWPASS_HEIGHT,		// Height of the lowpass band
	CODEC_TAG_MARGIN_TOP,			// Margins that define the encoded subset
	CODEC_TAG_MARGIN_BOTTOM,
	CODEC_TAG_MARGIN_LEFT,
	CODEC_TAG_MARGIN_RIGHT,
	CODEC_TAG_PIXEL_OFFSET,			// Quantization parameters
	CODEC_TAG_QUANTIZATION,
	CODEC_TAG_PIXEL_DEPTH,			// Number of bits per pixel
	CODEC_TAG_LOWPASS_TRAILER,		// Lowpass trailer


	/***** Tags used for encoding the highpass wavelet bands *****/

	CODEC_TAG_WAVELET_TYPE,			// Type of wavelet (37)
	CODEC_TAG_WAVELET_NUMBER,		// Number of the wavelet in the transform
	CODEC_TAG_WAVELET_LEVEL,		// Level of the wavelet in the transform
	CODEC_TAG_NUM_BANDS,			// Number of wavelet bands
	CODEC_TAG_HIGHPASS_WIDTH,		// Width of each highpass band
	CODEC_TAG_HIGHPASS_HEIGHT,		// Height of each highpass band
	CODEC_TAG_LOWPASS_BORDER,		// Dimensions of lowpass border (obsolete)
	CODEC_TAG_HIGHPASS_BORDER,		// Dimensions of highpass border (obsolete)
	CODEC_TAG_LOWPASS_SCALE,		// Scale factor for lowpass band
	CODEC_TAG_LOWPASS_DIVISOR,		// Divisor for the lowpass band
	CODEC_TAG_HIGHPASS_TRAILER,		// Highpass trailer


	/***** Tags used for encoding the highpass band *****/

	CODEC_TAG_BAND_NUMBER,			// Identifying number of a wavelet band (48)
	CODEC_TAG_BAND_WIDTH,			// Band dimensions
	CODEC_TAG_BAND_HEIGHT,
	CODEC_TAG_BAND_SUBBAND,			// Subband number of this wavelet band
	CODEC_TAG_BAND_ENCODING,		// Encoding method for this band
	CODEC_TAG_BAND_QUANTIZATION,	// Quantization applied to band
	CODEC_TAG_BAND_SCALE,			// Band scale factor
	CODEC_TAG_BAND_HEADER,			// Band header -- was CODEC_TAG_BAND_DIVISOR
	CODEC_TAG_BAND_TRAILER,			// Band trailer


	/***** Tags used for encoding zerotrees *****/

	CODEC_TAG_NUM_ZEROVALUES,		// Number of zero values (57)
	CODEC_TAG_NUM_ZEROTREES,		// Number of zerotrees
	CODEC_TAG_NUM_POSITIVES,		// Number of positive values
	CODEC_TAG_NUM_NEGATIVES,		// Number of negative values
	CODEC_TAG_NUM_ZERONODES,		// Number of zerotree nodes


	/***** Tags used for encoding the color channel header *****/

	CODEC_TAG_CHANNEL,				// Channel number (62)

	/***** Optional tags defined in the first release *****/

	CODEC_TAG_INTERLACED_FLAGS,		// Interlaced structure of the video stream (63)
	CODEC_TAG_PROTECTION_FLAGS,		// Copy protection bits
	CODEC_TAG_PICTURE_ASPECT_X,		// Numerator of the picture aspect ratio
	CODEC_TAG_PICTURE_ASPECT_Y,		// Denominator of the picture aspect ratio


	/***** Define tags for future releases here *****/

	CODEC_TAG_SUBBAND,	//NOT USED  // Marks the beginning of an encoded subband (67)

	CODEC_TAG_SAMPLE_FLAGS,			// Flag bits that control sample decoding (68)
									// (See flag definitions below)

	CODEC_TAG_FRAME_NUMBER,			// Sequence number of the frame in the bitstream (69)
	CODEC_TAG_PRECISION,			// Number of bits in the source video

	CODEC_TAG_INPUT_FORMAT,			// Use for formats like BYR1 so that output formats  (71)
									// can match input formats

	CODEC_TAG_BAND_CODING_FLAGS,	// (72)

	CODEC_TAG_INPUT_COLORSPACE,		// Obselete

	/***** *****/
	CODEC_TAG_PEAK_LEVEL,			// Required tag for parsing a Peak table. (74)
	CODEC_TAG_PEAK_TABLE_OFFSET_L,	// Tag that indicated the subband has a peaks table following (low bytes)
	CODEC_TAG_PEAK_TABLE_OFFSET_H,	// Tag that indicated the subband has a peaks table following (high bytes)


	/***** Tags that should not be encoded into a normal bitstream *****/

	CODEC_TAG_SAMPLE_END,			// Marks the end of the sample (for debugging only) (77)
	CODEC_TAG_COUNT,				// Number of codec tags (for error detection only)

	CODEC_TAG_VERSION,				// New 2006-06-26  version = value>>12, subversion = (value>>8) & 0xf,  sub-sub-ver = value & 0xff (79)
	CODEC_TAG_QUALITY_L,			// New 2006-06-26  The requested quality : filmscan1,etc. and flags like preemphasis and force 10-bit.
	CODEC_TAG_QUALITY_H,			// New 2006-06-26  The requested quality : filmscan1,etc. and flags like preemphasis and force 10-bit.

	CODEC_TAG_BAND_SECONDPASS,		// Band marker to begin the second pass (82)

	CODEC_TAG_PRESCALE_TABLE,		// New 2006-11-27  Used to be hardwared (83)

	CODEC_TAG_ENCODED_FORMAT,		// Format of the encoded bitstream (see the ENCODED_FORMAT enum)   (84)

	CODEC_TAG_FRAME_DISPLAY_HEIGHT,	// Display Height (can be different form encoded height) (85)
	CODEC_TAG_FRAME_DISPLAY_WIDTH,	// Display Width (currently not implemented)
	CODEC_TAG_FRAME_DISPLAY_OFFSET_X,	// Start the display X pixels from the left (currently not implemented)
	CODEC_TAG_FRAME_DISPLAY_OFFSET_Y,	// Start the display Y pixels from the top (currently not implemented)

	CODEC_TAG_ENCODED_COLORSPACE_OLD, // Do not use
	CODEC_TAG_ENCODED_COLORSPACE_OLD3pt9,	//Do not use (90) YUV 709 vs 601, RGB VS vs CG
	CODEC_TAG_ENCODED_COLORSPACE,	//(91) YUV 709 vs 601, RGB VS vs CG

	CODEC_TAG_ENCODED_CHANNELS,		//(92) 2 - Stereo or 2+ multicam,
	CODEC_TAG_ENCODED_CHANNEL_NUMBER,	//(93) 0 - first of several frame, 1 is the second frame
	CODEC_TAG_ENCODED_CHANNEL_QUALITY,	//(94) 0 - unset, 1 - best qualkity, 2 - next best, use to mark the best image if one impacted by a stereo beam-splitter.

	CODEC_TAG_SKIP,					// (95) Tag Removed, used to clear a field from an existing bitstrem (must be parsed optionally, e.g. NEG())
	
	CODEC_TAG_PRESENTATION_HEIGHT,	// (96) If present, it must be decoded, as the MOV/AVI as the same size in its header.  e.g. Present 2704x1524 as 1920x1080
	CODEC_TAG_PRESENTATION_WIDTH,	// (97) If present, it must be decoded, as the MOV/AVI as the same size in its header.  e.g. Present 2704x1524 as 1920x1080
	
	CODEC_TAG_NOP = 128,			//always option NEG(128) = FF80 used to PAD if extra alightment is needed.

	CODEC_TAG_LAST_NON_SIZED = 0xff, // allow room to add more tags, to mark the end of non-sizes 32-bit tag/value tuplets.






	//***** Place new tags here and redefine CODEC_TAG_LAST_TAG if necessary *****

	CODEC_TAG_CHUNK24BIT = 0x2000,	// TAG with value & 0x2000 are chunk types with size fields (allowing the whole chunk to be bypassed)
									// size does not include itself = ie. CODEC_TAG_CHUNK|0 is legal -- just no data follows.
									// Valid TAGs are 0x20-- to 0x3f-- = 31 tags.
									// chunksize is 24bit * 4 = bytes. = value + ((TAG & 0xff)<<16)
									// These chunks are not skipped, and are generally only used for marking the size of a subband.
									// If unknown they should be ignored, not skipped, if optional -- for backward compatibility.
									// There will contain standard CineForm TAGs and data.

	CODEC_TAG_SUBBAND_SIZE = 0x2000,// Indicates the amount of data in the entropy encoded subband LL, and High Pass bands
									// There is one other tag|value before the subband entropy data.
	CODEC_TAG_LEVEL_SIZE = 0x2100,	// Indicates the amount of data in the entropy encoded in an entire level of several subbands
	CODEC_TAG_SAMPLE_SIZE = 0x2200,	// Indicates the amount of data for all the levels

	CODEC_TAG_UNCOMPRESS = 0x2300,	// uncompressed chunk -- limited to 256MByte images.

//	CODEC_TAG_HUGE_FLAG = 0x1000,	// all data is in 256 bytes, rather than 4 byte units.
//	CODEC_TAG_SUBBAND_SIZE_HUGE = 0x3000,	// Indicates the amount of data for all the levels (256 bytes, rather than 4 byte units)
//	CODEC_TAG_LEVEL_SIZE_HUGE = 0x3100,		// Indicates the amount of data in the entropy encoded in an entire level of several subbands
//	CODEC_TAG_SAMPLE_SIZE_HUGE = 0x3200,	// Indicates the amount of data for all the levels

	
//	CODEC_TAG_UNCOMPRESS_HUGE = 0x3300,	// uncompressed chunk -- limited to 16TByte images.


	//------------------
	/***** Tags used for custom chucks that have chunk size information *****/
	CODEC_TAG_CHUNK = 0x4000,		// TAG with value >= 0x4000 are chunk type with size fields (allowing the whole chunk to be bypassed)
									// size does not include itself = ie. CODEC_TAG_CHUNK|0 is legal -- just no data follows.

	CODEC_TAG_PEAK_TABLE,			// Chunk of peak data (value is size -- this is be skipped if unsupports)
	//Peak tables structure
	//  CODEC_TAG_PEAK_TABLE|(chunk size) // chunk size = (((num + 1)&0xfffffe)/2 + 2 (for level + num)) // size in longs
	//  NEG(CODEC_TAG_PEAK_LEVEL)|(level)
	//  NEG(CODEC_TAG_PEAK_NUM_OF_ENTRIES)|(num of 16bit values)
	//  table of 16bit values
	//  two byte pad if necessary
	CODEC_TAG_METADATA,				// Chunk of metadata (value is size -- this is be skipped if unsupports)

	// Old encoded format tag (should be obsolete and can be removed)
	CODEC_TAG_OLD_ENCODED_FORMAT,

	CODEC_TAG_CUSTOM_CHUNK24BIT = 0x6000, // These are 24bit versions of CODEC_TAG_CHUNK, although different than CODEC_TAG_CHUNK24BIT as
										// they should be skipped if there are not recognize and if option flag is set.  This Chunk type
										// contains non-Codec data, like metadata, codec is not expect to decode the data within.
	CODEC_TAG_METADATA_LARGE = 0x6000,	// For attaching Metadata > 256K, normally use CODEC_TAG_METADATA


} CODEC_TAG;

#endif


/*
	Definition of bits in the sample flags

	The sample flags specify whether decoding should use a progressive
	or interlaced inverse transform.  The sample flags are not inserted
	into the sample header if the flags are all zero.
*/
#define SAMPLE_FLAGS_PROGRESSIVE	0x0001		// The first transform is a spatial transform


typedef struct wavelet_info		// Wavelet information structure
{
	int type;		// Type of wavelet
	int width;		// Number of columns in each wavelet band
	int height;		// Number of rows in each wavelet band
} WAVELET_INFO;

// Mask for determining if all lower subbands have been decoded
#define DECODED_SUBBAND_MASK(subband)	(1 << (subband))

// Masks for decoding full and reduced resolution
#define DECODED_SUBBAND_MASK_FULL		((1 << CODEC_NUM_SUBBANDS_FULL) - 1)
#define DECODED_SUBBAND_MASK_HALF		((1 << CODEC_NUM_SUBBANDS_HALF) - 1)

// Old name for half resolution
#define DECODED_SUBBAND_MASK_SIF		DECODED_SUBBAND_MASK_HALF

// Decoded the first spatial wavelet and the lowpass band from the temporal high pass wavelet
#define DECODED_SUBBAND_MASK_QUARTER	0x8F

typedef enum codec_precision
{
	CODEC_PRECISION_8BIT = 8,
	CODEC_PRECISION_10BIT = 10,
	CODEC_PRECISION_12BIT = 12,

	CODEC_PRECISION_DEFAULT = CODEC_PRECISION_8BIT

} CODEC_PRECISION;

// Enumerated values for the type of encoding
typedef enum encoded_format
{
	ENCODED_FORMAT_UNKNOWN = 0,
	ENCODED_FORMAT_YUV_422,
	ENCODED_FORMAT_BAYER,
	ENCODED_FORMAT_RGB_444,
	ENCODED_FORMAT_RGBA_4444,
	ENCODED_FORMAT_YUVA_4444,


	//***** Add new tags above this line *****/

	ENCODED_FORMAT_COUNT,		// Number of encoded formats that have been defined

	// Encoded format used before the introduction of RGB 4:4:4
	ENCODED_FORMAT_DEFAULT = ENCODED_FORMAT_YUV_422,

	// If the encoded format is unknown, then the decoder should assume that
	// the internal representation of the encoded data is the default format.

	//TODO: Update the minimum and maximum formats after adding new formats
	ENCODED_FORMAT_MINIMUM = ENCODED_FORMAT_YUV_422,
	ENCODED_FORMAT_MAXIMUM = ENCODED_FORMAT_YUVA_4444

} ENCODED_FORMAT;

#if 0
typedef enum bayer_format
{
	BAYER_FORMAT_UNKNOWN = 0,		// Bayer pixel pattern not set
	BAYER_FORMAT_GREEN_RED = 1,		// Green-Red
	BAYER_FORMAT_GREEN_BLUE = 2,	// Green-Blue
	BAYER_FORMAT_BLUE_GREEN = 3,	// Blue-Green
	BAYER_FORMAT_RED_GREEN = 4,		// Red-Green

	// The default Bayer pixel format is Red-Green
	BAYER_FORMAT_DEFAULT = BAYER_FORMAT_RED_GREEN

} BAYER_FORMAT;
#else
// The Bayer pixel patterns are defined in AVIExtendedHeader.h
typedef BayerFormat BAYER_FORMAT;
#endif

typedef struct codec_state		// State of bitstream during encoding or decoding
{
	int interlaced_flags;		// Flags that describe interlaced structure (defined below)
	int protection_flags;		// Copy protection flags (defined below)
	int picture_aspect_x;		// Numerator of the picture aspect ratio
	int picture_aspect_y;		// Denominator of the picture aspect ratio
	int chroma_offset;			// Offset added to chroma after decoding
	int frame_width;			// Width of the current encoded frame
	int frame_height;			// Height of the current encoded frame
	int num_frames;				// Number of frames in the group
	int num_wavelets;			// Number of wavelets in the transform
	int num_subbands;			// Number of subbands in the transform
	int num_spatial;			// Number of spatial wavelets in the transform
	int num_channels;			// Number of channels
	int transform_type;			// Type of transform
	int channel;				// Channel index (zero is luminance)
	int max_subband;			// Largest subband index
	int first_wavelet;			// Type of the first wavelet to decode
	int marker;					// Most recent bitstream marker

	int sample_done;			// Has the sample been decoded?
	int progressive;			// Is the sample decoded as a progressive or interlaced frame?

	int precision;				// Number of bits in the original video source

	int input_format;			// Source format of the encoded pixels (RGB, BYR1, etc.)
	int encoded_format;			// Internal representation of the encoded data

	int PFrame;					// 0 for I-frame, 1 for P-frame

	uint32_t frame_number;		// Sequence number of the frame in the bitstream -- unfortunatel increased in eash encode thread
	uint32_t unique_framenumber;// UFRM metadata

	// Track which subbands have been decoded
	uint32_t decoded_subband_flags;

	// Table of wavelet dimensions indexed by the wavelet index (same as transform)
	WAVELET_INFO wavelet[TRANSFORM_MAX_WAVELETS];

	// Size of each channel in bytes
	uint32_t channel_size[TRANSFORM_MAX_CHANNELS];

	int active_codebook;		// Normally 0, non-zero indicates which codebook is needed.
	int difference_coding;		// Normally 0. non-zero mean this band was differenced

	unsigned char version[4];	// sub,subver,subsubver -- of the codec used to encode the image
	unsigned int encode_quality;// filmscan1,etc. and flags like preempahasis and force 10-bit.

	struct						// Lowpass band header
	{
		int subband;			// Subband number of the lowpass band
		int level;				// Wavelet level of the lowpass band
		int width;				// Lowpass band dimensions
		int height;

		struct {				// Border around the lowpass band image
			int left;
			int top;
			int right;
			int bottom;
		} margin;

		int pixel_offset;		// Quantization parameters
		int quantization;
		int bits_per_pixel;

	} lowpass;

	struct						// Highpass band header
	{
		int wavelet_type;
		int wavelet_number;
		int wavelet_level;
		int num_bands;			// Number of bands
		int width;				// Band dimensions
		int height;
		int lowpass_border;		// Left and botom borders in each band
		int highpass_border;
		int lowpass_scale;
		int lowpass_divisor;

	} highpass;

	struct						// Subband header
	{
		int number;				// Band number within the wavelet
		int width;				// Band dimensions
		int height;
		int subband;			// Subband number within the transform
		int encoding;			// Band encoding method
		int quantization;		// Band quantization
		int scale;
		int divisor;

	} band;

	struct						// Frame header
	{
		int type;				// Type of frame
		int width;				// Frame dimensions
		int height;
		int group_index;		// Index of frame within the group
								// (zero is the first frame)
	} frame;

	struct						//sub-band peak data
	{
		PIXEL *base;			// bit-stream ptr to peak data
		int offset;				// offset from current position
		int size;				// number of entries
		int level;				// level at which peaks exceed

	} peak_table;

	uint8_t  *channel_position;	// Used for skip subbands and jumping to particular channels

} CODEC_STATE;


// Default values for the codec state parameters

#define	INTERLACED_FLAGS		0		// Default interlace structure flags
#define PROTECTION_FLAGS		0		// Default copy protection flags
#define PICTURE_ASPECT_X		16		// Default picture aspect ratio
#define PICTURE_ASPECT_Y		9

#define FRAME_WIDTH				720		// Default frame width
#define FRAME_HEIGHT			480		// Default frame height
#define FRAMES_PER_GROUP		2		// Number of frames in the group
#define WAVELETS_PER_TRANSFORM	6		// Number of wavelets in a transform
#define SPATIALS_PER_TRANSFORM	3		// Number of spatial wavelets
#define CODEC_NUM_CHANNELS		3		// Default number of channels
#define CODEC_MAX_CHANNELS		4		// Maximum number of channels
#define CODEC_MAX_SUBBAND		16		// Largest subband index in the default transform

#define CODEC_NUM_SUBBANDS		17		// Number of subbands (including the lowpass band)

// Define the number of subbands in full and half resolution
#define CODEC_NUM_SUBBANDS_FULL		17
#define CODEC_NUM_SUBBANDS_HALF		11

// Old name for half resolution
#define CODEC_NUM_SUBBANDS_SIF		CODEC_NUM_SUBBANDS_HALF


// Bits in the interlace structure flags

#define CODEC_FLAGS_INTERLACED			0x01	// Interlaced flags
#define CODEC_FLAGS_FIELD1_FIRST		0x02	// NTSC has this bit cleared
#define CODEC_FLAGS_FIELD1_ONLY			0x04	// Indicates missing fields
#define CODEC_FLAGS_FIELD2_ONLY			0x08
#define CODEC_FLAGS_DOMINANCE			0x10

#define CODEC_FLAGS_INTERLACED_MASK		0x1F	// Unused bits must be zero

// Useful macros for testing the interlaced flags

#define INTERLACED(flags)			(((flags) & CODEC_FLAGS_INTERLACED) != 0)
#define PROGRESSIVE(flags)			(((flags) & CODEC_FLAGS_INTERLACED) == 0)
#define FIELD_ORDER_NTSC(flags)		(((flags) & CODEC_FLAGS_FIELD1_FIRST) == 0)
#define FIELD_ORDER_PAL(flags)		(((flags) & CODEC_FLAGS_FIELD1_FIRST) != 0)
#define FIELD_ONE_ONLY(flags)		(((flags) & CODEC_FLAGS_FIELD1_ONLY) != 0)
#define FIELD_TWO_ONLY(flags)		(((flags) & CODEC_FLAGS_FIELD2_ONLY) != 0)
#define FIELD_ONE_PRESENT(flags)	(((flags) & CODEC_FLAGS_FIELD2_ONLY) == 0)
#define FIELD_TWO_PRESENT(flags)	(((flags) & CODEC_FLAGS_FIELD1_ONLY) == 0)
#define FIELD_BOTH_PRESENT(flags)	(((flags) & (CODEC_FLAGS_FIELD1_ONLY | CODEC_FLAGS_FIELD1_ONLY)) == 0)

// Bits in the copy protection flags

#define CODEC_FLAGS_PROTECTED			0x01	// Copy protection flags
#define CODEC_FLAGS_PROTECTION_MASK		0x01	// Unused bits must be zero

#if 0
// Quantization table
typedef struct quantization_table
{
	int num_levels;			// Number of levels in the quantization table
	struct quant_level {
		int num_bands;		// Number of bands including the lowpass band
		int divisor;		// Common quantization divisor at this level
		struct quant_band {
			int divisor;	// Quantization divisor for this band and level
		} quant[CODEC_MAX_BANDS];
	} quant[CODEC_MAX_LEVELS + 1];
} QUANT;
#endif

// Bitstream structures for unpacked data representation
typedef struct sequence_header {
	uint32_t  marker;

	struct version {		// Codec version number
		int major;
		int minor;
		int revision;
	} version;

	uint32_t flags;	// Flags bits for optional data in the bitstream

	struct {
		int width;			// Frame dimensions and format
		int height;
		int format;
	} frame;

	int gop_length;			// Maximum number of frames in a group

} SEQUENCE_HEADER;

// Sequence header flags
#define SEQUENCE_FLAGS_RUNROWEND	0x00000001		// Run length coding marks end of rows
#define SEQUENCE_FLAGS_EXTENSION	0x80000000		// Extra data follows sequence header

typedef struct sequence_trailer {
	uint32_t  marker;
	uint32_t bitcount;
	uint32_t checksum;
} SEQUENCE_TRAILER;

typedef struct group_header {
	uint32_t  marker;
	int num_frames;			// Number of frames in the group (must be two)
	int num_channels;		// Number of color channels per frame
	int num_subbands;		// Total number of subbands in the group transform
	int num_spatial;		// Number of levels in the spatial wavelet pyramid
	int wavelet_type;		// Type of the first wavelet in the bitstream
	int frame_format;		// Color format of the frames

	// Size of each channel in bytes
	int channel_size[TRANSFORM_MAX_CHANNELS];

} GROUP_HEADER;

typedef struct channel_header {
	uint32_t  marker;
	int channel;			// Number of the next channel to decode
} CHANNEL_HEADER;

typedef struct group_trailer {
	uint32_t  marker;
} GROUP_TRAILER;

typedef struct frame_header {
	uint32_t  marker;
	int type;				// Type of frame (I versus P)
	int width;
	int height;
	int display_height;		// can be zero if not set
	int group_index;		// Index within group of frames (first frame is zero)
} FRAME_HEADER;

// Note: A frame preceeded by a frame header with group index zero is a single
// frame group and cannot be followed by other frames in the same group of frames

typedef enum {
	FRAME_TYPE_IFRAME = 1,
	FRAME_TYPE_PFRAME
} FRAME_HEADER_TYPE;

typedef struct frame_trailer {
	uint32_t  marker;
	uint32_t bitcount;
} FRAME_TRAILER;

typedef struct lowpass_header {
	uint32_t  marker;
	int subband;	// Index in the sequence of encoded bands
	int width;		// Dimensions of lowpass band at the top of the pyramid
	int height;
	int level;		// Level in the image pyramid (zero is lowest)
	int bpp;		// Bits per pixel in the encoded bitstream

	// The transmitted lowpass image may be smaller than the reconstructed dimensions
	// It is the responsibility of the decoder to correctly place the transmitted image
	// within the lowpass band and fill the border with appropriate values as necessary

	int offset_width;		// Amount of crop on the left
	int offset_height;		// Amount of crop on top
	int border_width;		// Amount of crop on the right
	int border_height;		// Amount of crop on the bottom

	struct {				// Quantization parameters
		int offset;
		int divisor;
	} quantization;

} LOWPASS_HEADER;

typedef struct lowpass_trailer {
	uint32_t  marker;
} LOWPASS_TRAILER;

typedef struct highpass_header {
	uint32_t  marker;
	int type;		// Wavelet type (determines the number of highpass bands)
	int number;		// Number of the wavelet in the transform
	int level;		// Level of the wavelet in the pyramid
	int width;		// Dimensions of each band of highpass coefficients
	int height;
	int num_bands;	// Number of highpass bands at this level

	// Parameters that indicate the border of coefficients lost due to filtering
	// Refer to the comments in the definition of the wavelet image descriptor
	int lowpass_border;
	int highpass_border;

	// Parameters for the scaling of the lowpass image during filtering
	int lowpass_scale;
	int lowpass_divisor;

	// The scaling parameters for the highpass bands are in the band headers

} HIGHPASS_HEADER;

typedef struct highpass_trailer {
	uint32_t  marker;
	uint32_t positive;
	uint32_t negative;
	uint32_t zerovalues;
	uint32_t zerotrees;
	uint32_t zeronodes;
} HIGHPASS_TRAILER;

typedef struct band_header {
	uint32_t  marker;
	int band;				// Band number within the wavelet
	int width;				// Dimensions of the highpass band
	int height;
	int subband;			// Subband number within the transform
	int encoding;			// Method used for encoding the band
	int scale;				// Scale factors used during filtering
	int divisor;
	int quantization;		// Coefficient quantization factor
	uint32_t count;	// Count of significance codes in bitstream
							// (Present if the encoding method is zerotree)
} BAND_HEADER;

#define BAND_END_TRAILER	(1<<15)-1

typedef struct band_trailer {
#if 0
	FSM *fsm;
#endif
	uint32_t  marker;
} BAND_TRAILER;

typedef struct coeff_header {
	uint32_t  marker;
	int count;				// Count of coefficients in bitstream
	int divisor;			// Quantization divisor
	int bits_per_coefficient;
} COEFF_HEADER;

typedef struct iframe		// Unpacked bitstream for a single frame
{
	CODEC_ERROR error;		// Decoding error code

	int num_levels;			// Number of levels decoded for the frame
	int num_bands;			// Number of decoded bands for all levels

	//ZEROTREE *zerotree;	// Root of the reconstructed zerotree
	IMAGE *wavelet;			// Root of the reconstructed wavelet pyramid
	IMAGE *image;			// Reconstructed full resolution image

	FRAME_HEADER header;	// Frame header and trailer
	FRAME_TRAILER trailer;

	// Header and trailer for the lowpass band at the top of the wavelet
	struct {
		LOWPASS_HEADER header;
		LOWPASS_TRAILER trailer;
	} lowpass;

	// Data for the highpass bands at each level
	struct {
		HIGHPASS_HEADER header;		// Highpass header at this level

		struct {					// Data for each highpass band
			BAND_HEADER header;
			COEFF_HEADER coefficients;
			BAND_TRAILER trailer;
		} band[CODEC_MAX_HIGHBANDS];

		HIGHPASS_TRAILER trailer;	// Highpass trailer at this level

	} highpass[CODEC_MAX_LEVELS];

} IFRAME;

typedef struct group		// Unpacked bitstream for a group of frames
{
	GROUP_HEADER header;	// Header for group of frames

	struct {				// Lowpass band header and trailer
		LOWPASS_HEADER header;
		LOWPASS_TRAILER trailer;
	} lowpass;

	struct {				// Highpass band data for each wavelet
		HIGHPASS_HEADER header;

		struct {
			BAND_HEADER header;
			BAND_TRAILER trailer;
		} band[CODEC_MAX_HIGHBANDS];

		HIGHPASS_TRAILER trailer;

	} highpass[TRANSFORM_MAX_WAVELETS];

	// Array of wavelet transforms (one per channel)
	TRANSFORM *transform[TRANSFORM_MAX_CHANNELS];

	GROUP_TRAILER trailer;	// Trailer for the group of frames

} GROUP;

// The breakdown of significance codes by type is passed in the bitstream for debugging
typedef struct scode_counters {
	uint32_t zerovalues;
	uint32_t zerotrees;
	uint32_t positives;
	uint32_t negatives;
	uint32_t zeronodes;
} SCODE_COUNTERS;

// Type of data sample handled by the decoder
enum {
	SAMPLE_TYPE_NONE = 0,	// No sample being decoded
	SAMPLE_TYPE_FRAME,		// Decoding a single frame (second frame in the group)
	SAMPLE_TYPE_GROUP,		// Decoding a group of frames (return the first frame in the group)
	SAMPLE_TYPE_CHANNEL,	// Decoding channel data
	SAMPLE_TYPE_FIRST,		// Decoding the first frame in a group (unused)
	SAMPLE_TYPE_SECOND,		// Decoding the remaining frames in a group (unused)

	// Found the end of a group while looking for more channel data
	SAMPLE_TYPE_GROUP_TRAILER,

	// The video sequence header may appear as the first sample
	SAMPLE_TYPE_SEQUENCE_HEADER,

	// The video sequence trailer may appear as the last sample
	SAMPLE_TYPE_SEQUENCE_TRAILER,

	// Sample types used for intra frames and inter frames
	SAMPLE_TYPE_INTRA_FRAME,
	SAMPLE_TYPE_INTER_FRAME,

	// Alternate names
	SAMPLE_TYPE_IFRAME = SAMPLE_TYPE_INTRA_FRAME,
	SAMPLE_TYPE_PFRAME = SAMPLE_TYPE_INTER_FRAME,

	SAMPLE_TYPE_ERROR = -1	// Unknown or unexpected sample
};

typedef struct {
	int type;			// Type of data sample (frame or group)
	union {
		IFRAME *frame;	// Single video frame
		GROUP *group;	// Group of frames (GOP)
		void *ptr;		// Generic reference to data
	} data;
} SAMPLE;


typedef enum {
	DECODER_STATE_INITIALIZED = 1
} DECODER_STATE;

typedef struct codec		// Fields common to the encoder and decoder
{
	FILE *logfile;			// Output file for status information
	CODEC_ERROR error;		// Code for error during encoding or decoding
	uint32_t frame_count;	// Number of frames encoded or decoded

#if _DUMP
	DUMP_INFO dump;			// Used for dumping wavelet bands to files
#endif

} CODEC;


#if _THREADED

// Number of threads in the transform worker thread pool
#define TRANSFORM_WORKER_POOL_COUNT		4

#define TRANSFORM_WORKER_TOP_THREAD		0		// Thread for top rows
#define TRANSFORM_WORKER_BOTTOM_THREAD	1		// Thread for bottom rows
#define TRANSFORM_WORKER_UPPER_THREAD	2		// Thread for upper middle rows
#define TRANSFORM_WORKER_LOWER			3		// Thread for lower middle rows

typedef enum
{
	JOB_TYPE_OUTPUT	= 0,
	JOB_TYPE_HORIZONAL_3D,
	JOB_TYPE_WAVELET,
	JOB_TYPE_VERTICAL_3D,
	JOB_TYPE_SHARPEN,
	JOB_TYPE_HISTOGRAM,
	JOB_TYPE_BURNINS,
	JOB_TYPE_BUILD_1DS_2LINEAR,
	JOB_TYPE_BUILD_1DS_2CURVE,
	JOB_TYPE_BUILD_LUT_CURVES,
	JOB_TYPE_BUILD_CUBE,
	JOB_TYPE_OUTPUT_UNCOMPRESSED,  // output from uncompressed v210 or ??? frame buffer
	JOB_TYPE_WARP,					// Do the warp
	JOB_TYPE_WARP_CACHE,			// Cache/Calculate the offsets for the warp
	JOB_TYPE_WARP_BLURV,			// blur the background fill
} JOB_TYPES;

typedef struct
{
	uint8_t *output;			// Output frame buffer
	int pitch;				// Output frame pitch
	int framenum;			// frqame 0 or 1 for two frame GOPs
	uint8_t *channeldata[TRANSFORM_MAX_CHANNELS]; // used in quarter res decodes
	int channelpitch[TRANSFORM_MAX_CHANNELS]; // used in quarter res decodes
	FRAME_INFO info;		// Frame Info for width and height (which can change for bayer)

	int jobType;			// 0 - Debayer stuff, 1 - 3d work

	// jobType 1, 3d work
	uint8_t *local_output;
	int local_pitch;
	int channel_offset;
	int chunk_size;
	int line_max;
	int fine_vertical;

	// jobType 2, wavelet work
	int frame;				// Index of output frame to produce
	int num_channels;		// Number of channels in the transform array
	int chroma_offset;		// Offset for the output chroma
	int precision;			// Source pixel bit depth

	// jobType JOB_TYPE_HISTOGRAM, histogram
	// jobtype JOB_TYPE_BURNINS, burnins

	void *data;				// job data, used for the meshin warp.
	uint32_t flags;			// 1 - fill background

	// Inverse horizontal filter that outputs the correct format
	HorizontalInverseFilterOutputProc horizontal_filter_proc;

} WORKER_THREAD_DATA;

typedef enum
{
	QT_CONVERT_DEFAULT,
	QT_CONVERT_TO_BGRA64,
	QT_CONVERT_TO_FLOATYUVA,
	QT_CONVERT_TO_QT,			// BGRA RGBA 32
	QT_CONVERT_YUV_TO_QT,
	QT_CONVERT_DEFAULT_B64A,
	QT_CONVERT_ARGB64_TO_BGRA,
	QT_CONVERT_ARGB64_TO_R4FL,
	QT_CONVERT_W13A_TO_R4FL
} QT_CONVERSION_TYPE;

// was 8
#define QT_WORK_CHUNK 135
typedef struct
{
	uint8_t				*output;		// frame buffer
	int					outPitch;		// frame pitch
	uint8_t				*input;			// input buffer
	int					inPitch;		// input pitch
	int					param;			// byte ordering of output, byte swap or white point
	int					gammaFix;		// Possibly app specific gamma correction
	QT_CONVERSION_TYPE	conversion;
	FRAME_INFO			info;			// for width & height
} CONVERT_WORKER_DATA;

typedef enum
{
	QT_SCALE_NONE,
	QT_SCALE_YU64_TO_BGRA64,
	QT_SCALE_RGBA,
	QT_SCALE_RGBA_TO_BGRA,
	QT_SCALE_B64A,
	QT_SCALE_B64A_TO_BGRA,
	QT_SCALE_2VUY_TO_422_8U,
	QT_SCALE_R408,
	QT_SCALE_YU64_TO_R4FL,
	QT_SCALE_B64A_TO_R4FL				// cmd 2009.07.20
} QT_SCALE_TYPE;

typedef struct
{
	int					sampleCount;
	uint8_t				*lmY;			// array of mix coefficients
} COL_SCALE_FACTOR;

typedef struct
{
	QT_SCALE_TYPE		currentScaler;	// current version of the scaler
	int					step;			// 0=row, 1=col
	uint8_t				*input;			// input frame
	int					inWidth;
	int					inHeight;
	int					inPitch;
	uint8_t				*output;		// output frame
	int					outWidth;
	int					outHeight;
	int					outPitch;
	int					byteSwapFlag;
	int					gammaFix;
	int					outAdjustedHeight;
	int					outStartRow;
	short				*rowScaleFactors;	// single or Luma
	short				*rowScaleFactorsC;	// chroma
	uint8_t				*blackYUVRow;		// for letterbox
	uint8_t				*tempBuffer;		// row scaled image
	COL_SCALE_FACTOR	*colScaleFactors;	// one for each row
} SCALE_WORKER_DATA;

#endif

#define FREEFORM_STR_MAXSIZE	128
#define FONTNAME_STR_MAXSIZE	128
#define FORMAT_STR_MAXSIZE		128
#define PNG_PATH_MAXSIZE		256

typedef struct MetadataDisplayParameters 
{
	int initialized; //non zero
	uint32_t tag;
	char freeform[FREEFORM_STR_MAXSIZE];
	char font[FONTNAME_STR_MAXSIZE];
	float fontsize;
	uint32_t justication;
	float fcolor[4];
	float bcolor[4];
	float scolor[4]; //stroke color
	float stroke_width;
	float xypos[16][2]; // all justifiaction styles
	char format_str[FORMAT_STR_MAXSIZE];
	char png_path[PNG_PATH_MAXSIZE];
	float object_scale[2];		// 0.1 to 1.0 scale form original size, for PNGs and Tools
	float display_opacity;
	int parallax;			// pixel at full res decode, negative is in front of the screen.
	int inframe;
	int outframe;
	int fadeinframes;
	int fadeoutframes;
} MDParams;

typedef struct ItemSubtitle
{
	struct ItemSubtitle *prev;
	struct ItemSubtitle *next;
	char png_name[256];
	char startTimecode[16];
	char endTimecode[16];
	int startFrame;
	int endFrame;
	int Width;
	int Height;
	int TopLeftX;
	int TopLeftY;
	int plt; //always �0� [Not used]
	int frcd; // yes = 1, no = 0; �yes� if it is forced subtitle, otherwise set �no� [Always flag as �no�]
	int fdup;// Number of frames for fade-in (fade-up)
	int fddn;// Number of frames for fade-out (fade-down)
	int tifplt;//always �0� [Not used]
	char text[256];// Comment text

} Subtitle;

typedef struct MetadataSubtitlingParameters 
{
	char *spi_file_buffer;
	int spi_error;
	unsigned char spi_error_msg[64];
	char spi_path[PNG_PATH_MAXSIZE];
	char tcoffset[16];
	int frameoffset;
	int FormatRes; //480/720/1080
	int FormatRate; // 23/24/25/29/50/59
	char imageDir[PNG_PATH_MAXSIZE];
	int DropFrame; //0 or 1
	int subcount;
	int parallax;
	Subtitle *firstTitle;
} Subtitling; 

#define KEYFRAME_PAYLOAD_MAX	256
typedef struct ItemKeyframePair
{
	uint32_t control_point_type;	//FOURCC of the keygframe group, CP3D or CPPR -- (3D or primiaries)
	uint32_t control_point_flags;	//unused, spaces for spline, hold and linear control
	uint32_t trigger_frame_prev;	//UFRM or converted TIMECODE
	uint32_t trigger_frame_in;		//UFRM or converted TIMECODE
	uint32_t trigger_frame_out;		//UFRM or converted TIMECODE
	uint32_t trigger_frame_next;	//UFRM or converted TIMECODE
	uint32_t payload_size;
	unsigned char frame_prev_payload[KEYFRAME_PAYLOAD_MAX];
	unsigned char frame_in_payload[KEYFRAME_PAYLOAD_MAX];
	unsigned char frame_out_payload[KEYFRAME_PAYLOAD_MAX];
	unsigned char frame_next_payload[KEYFRAME_PAYLOAD_MAX];
	float computed_fraction;		// used to limit excusive reprocessing.
} KeyframePair;

#define MAX_CONTROL_POINT_PAIRS		8
typedef struct KeyframingParameters 
{
	int keyframetypecount;
	KeyframePair KeyframePairs[MAX_CONTROL_POINT_PAIRS];
} Keyframing; 

// Cast an encoder or decoder to the common data type (the superclass)
#define CODEC_TYPE(p)	((CODEC *)(p))

typedef struct ToolsHandle				
{
	int histogram;					// set when active
	uint32_t histR[256];
	uint32_t histG[256];
	uint32_t histB[256];
	uint32_t maxR,maxG,maxB;

	int waveformWidth;					// up to 360
	unsigned short waveR[360][256];		// 0-359 screen width, 0-255 intesity, 0 to 65535 instance count. 
	unsigned short waveG[360][256];
	unsigned short waveB[360][256];

	int blurUVdone;
	unsigned short scopeUV[256][256];	// 0-255 U, 0-255 V, 0 to 65535 instance count. 
} ToolsHandle;


typedef struct thread_cntrl // presevered over a DECODER reset if set_thread_params is set to 1.
{
	int capabilities;				// Processor capabilities
	int limit;						// if 0 ignore, otherwise limit thread to x
	uint32_t affinity;		// if 0 ignore, otherwise set affinity to y
	unsigned int set_thread_params;	// if not 1 ignore affinity and limit.
} Thread_cntrl;

// Definitions used by the decoder data structure defined below
#define METADATA_CHUNK_MAX	64

#if _INTERLACED_WORKER_THREADS
#define UPPER_DECODING_THREAD	0
#define LOWER_DECODING_THREAD	1
#define MIDDLE_UP_DECODING_THREAD	2
#define MIDDLE_DOWN_DECODING_THREAD	3
#define THREADS_IN_LAST_WAVELET	4
#endif

enum BlendTypes {
	BLEND_NONE = 0,
	BLEND_STACKED_ANAMORPHIC,  //half high
	BLEND_SIDEBYSIDE_ANAMORPHIC,  //half wide
	BLEND_LINE_INTERLEAVED, 
	BLEND_ONION, 
	BLEND_DIFFERENCE, 
	BLEND_STEREO_YUY2inRGBA, 
	BLEND_FREEVIEW,
	BLEND_SPLITVIEW,

	BLEND_ANAGLYPH_RC = 16,
	BLEND_ANAGLYPH_RC_BW,
	BLEND_ANAGLYPH_AB,
	BLEND_ANAGLYPH_AB_BW,
	BLEND_ANAGLYPH_GM,
	BLEND_ANAGLYPH_GM_BW,
	BLEND_ANAGLYPH_DUBOIS
};

/*!
	@brief Data structure for storing the decoder state information

	This data structure holds all of the information used by the decoder
	to convert encoded samples to output frames, including decoding the
	wavelet bands and applying the inverse wavelet transform.

	The codec state member of this data structure is intended to represent
	the state of the bitstream during decoding.  At any step during decoding
	the codec state in the decoder should match the codec state in the encoder
	at the same step when the bitstream was encoded.  The intent is to minimize
	the amount of overhead information that was encoded into the bitstream.
	For example, during encoding the encoder updates the codec state information
	with the width and height of the next wavelet band based on the dimensions
	of the previous wavelet band.  The decoder should perform the same calculation
	during decoding.  When the codec state in the encoder and the codec state in
	the decoder are the same, then it is not necessary to encode the dimensions
	of the next wavelet band into the bitstream.  In practice, all current encoders
	insert the full codec state into the bitstream and the all current decoders
	expect that all information required for decoding is present in the bitstream.
*/
typedef struct decoder		// Decoder state (derived from codec)
{
	/***** The following fields are common between the encoder and decoder *****/

	FILE *logfile;			// Output file for status information
	CODEC_ERROR error;		// Code for error during decoding
	uint32_t frame_count;	// Number of frames decoded

	ALLOCATOR *allocator;	// Interface for memory allocation (optional)

	CODEC_STATE codec;		// Current state of bitstream during decoding

#if _DUMP
	DUMP_INFO dump;			// Used for dumping wavelet bands to files
#endif

	/***** End of the fields that are common between the encoder and decoder *****/

	DECODER_STATE state;	// Decoder state
	uint32_t marker;		// Last marker found during decoding
	//DWORD sample_count;	// Number of samples processed

	uint32_t flags;			// Flags that control decoding

	FRAME_INFO frame;		// Output format requested by the caller

	VLCBOOK *magsbook[CODEC_NUM_CODESETS];		// Codebook for coefficient magnitudes
	RLVBOOK *runsbook[CODEC_NUM_CODESETS];		// Codebook for runs of coefficients
	FLCBOOK *fastbook[CODEC_NUM_CODESETS];		// Fast codebook lookup table

	FSM fsm[CODEC_NUM_CODESETS];				// Finite state machine for this decoder

	FRAME *workspacegop;	// Used for interim processing when needed (i.e. YUV to RGB)

	//TODO: Why is the GOP length defined here?
	// It is also defined in the codec state and is read from the bitstream
	int gop_length;

	// Second frame in a decoded group of frames (obsolete)
	//FRAME *next_frame;

	char *buffer;			// Buffer used during decoding
	size_t buffer_size;		// Size of the buffer in bytes

	SCRATCH scratch;		// Scratch buffer

	void *threads_buffer[_MAX_CPUS];	// Buffer used during decoding
	size_t threads_buffer_size;		// Size of each debayer buffer in bytes

	//TODO: The scratch buffer should replace buffer/buffer_size throughout the codec

	int vfw;
	int no_output;
	int sdk_access;			// Codec under direct control (not called form DShow or QT, VFW etc.)

	// Array of wavelet transforms (one per channel)
	TRANSFORM *transform[TRANSFORM_MAX_CHANNELS];

	// Table for mapping the subband number to the wavelet
	int subband_wavelet_index[CODEC_MAX_SUBBANDS];

	// Table for mapping the subband number to the band within the wavelet
	int subband_band_index[CODEC_MAX_SUBBANDS];

	int gop_frame_num;		// 0 or 1 -- used

	unsigned int band_end_code[CODEC_NUM_CODESETS];		// Band end code word for each codebook
	int band_end_size[CODEC_NUM_CODESETS];				// Band end code size for each codebook

#if _THREADED_DECODER

#define DECODING_QUEUE_LENGTH	(TRANSFORM_MAX_WAVELETS * TRANSFORM_MAX_CHANNELS)
	struct transform_queue				// Thread used for the intermediate transforms
	{
		int started;
		int num_entries;			// Number of entries in the transform queue
		int next_entry;				// Index to the next transform to perform
		int free_entry;				// Index to the next free entry in the queue

		struct entry				// Queue of pending transforms
		{
			TRANSFORM *transform;
			int channel;
			int index;
			int precision;
			int done;

		} queue[DECODING_QUEUE_LENGTH];
	} transform_queue;
#endif

#if _INTERLACED_WORKER_THREADS

	struct interlaced_worker							// Worker threads used for the final transform
	{
		DWORD id[THREADS_IN_LAST_WAVELET];				// Worker thread identifier
		HANDLE handle[THREADS_IN_LAST_WAVELET];			// Worker thread handles
		HANDLE start_event[THREADS_IN_LAST_WAVELET];	// Signals the worker threads to begin processing
		HANDLE row_semaphore;							// Signals that a row is available for processing
		HANDLE done_event[THREADS_IN_LAST_WAVELET];		// Signals that a thread has finished processing
		HANDLE stop_event;								// Forces the threads to terminate

		int thread_count;			// Count of worker threads that are active

		int current_row;			// Next row processed

		int lock_init;				//
		CRITICAL_SECTION lock;		// Controls access to the worker thread data

		struct interlace_data					// Processing parameters for each worker thread
		{
			int type;				// Type of inverse transform to perform
			int frame;				// Index of output frame to produce
			int num_channels;		// Number of channels in the transform array
			uint8_t *output;			// Output frame buffer
			int pitch;				// Output frame pitch
			FRAME_INFO info;		// Format of the output frame
			int chroma_offset;		// Offset for the output chroma
			int precision;			// Source pixel bit depth

		} interlace_data;

	} interlaced_worker;

#endif

#if _THREADED

  #define ENTROPY_ENGINE_QUEUE		(3 * TRANSFORM_MAX_WAVELETS * TRANSFORM_MAX_CHANNELS)
	struct entropy_worker_new					// Worker threads used for entropy decoding
	{
		// Define a pool of worker threads
		THREAD_POOL pool;

		// Control access to the transform worker thread data
		LOCK lock;

		int threads_used;
		int next_queue_num;

		struct entropy_data_new			// Processing parameters for each worker thread
		{
			BITSTREAM stream;
			PIXEL *rowptr;
			int width;
			int height;
			int pitch;
			PIXEL *peaks;
			int level;
			int quant;
			IMAGE *wavelet;
			int band_index;
			int active_codebook;
			int difference_coding;
			int initialized;
		} entropy_data[ENTROPY_ENGINE_QUEUE];

	} entropy_worker_new;

	struct worker_thread
	{
		// Define a pool of worker threads
		THREAD_POOL pool;

		// Control access to the transform worker thread data
		LOCK lock;

		// Next row to be processed (use the thread pool work index)
		//int current_row;

		// Processing parameters for each transform worker thread
		WORKER_THREAD_DATA data;

	} worker_thread;

	
	struct draw_thread
	{
		// Define a pool of worker threads
		THREAD_POOL pool;

		// Control access to the transform worker thread data
		LOCK lock;

	} draw_thread;

	struct decoder_thread
	{
		// Define a pool of worker threads
		THREAD_POOL pool;

		// Control access to the transform worker thread data
		LOCK lock;

		BITSTREAM *input;
		uint8_t *output;
		int pitch;
		ColorParam *colorparams;

	} decoder_thread;
#endif

	int playPosition;
	int	initialized;					// set non-zero once any element is initialized

	PIXEL16U *RawBayer16;			// a buffer only used for BYR2/3 decodes (scrubbing/render in Premiere_
	int RawBayerSize;
	PIXEL16U *RGBFilterBuffer16;	// only use when vertically filtering (demosaicing) bayer data.
	int RGBFilterBufferSize;
	PIXEL16U *StereoBuffer;	// only use when vertically filtering (demosaicing) bayer data.
	int StereoBufferSize;
	int StereoBufferFormat;
	int RGBFilterBufferPhase;		// 0 = RGB, 1 = GRB, 2 = YUV
	short *RawCube;					// a buffer use new 3DLUT cubes.
	short *Curve2Linear;
	short *Linear2CurveRed;
	short *Linear2CurveGrn;
	short *Linear2CurveBlu;
	short *GammaContrastRed;		// input -16384 to 32768+16384 i.e. -2 to +6, 13-bit, output signed 13-bit
	short *GammaContrastGrn;
	short *GammaContrastBlu;
	unsigned short *BYR4LinearRestore;
	int linear_color_matrix_highlight_sat[12];	// signed 13-bit
	int linear_color_matrix[12];	// signed 13-bit
	int linear_matrix_non_unity;
	int curved_color_matrix[12];	// signed 13-bit
	int curved_matrix_non_unity;
	int contrast_gamma_non_unity;
	int forceBuildLUT;				// if non-zero, build the 3D LUT even if calculating color corrections could work.  Some CC can cause overflow.
	int useFloatCC;					// if non-zero, use floating point color corrections, as some interger CC can cause overflow.
	int curve_change_active;
	int use_three_1DLUTS;
	CFHDDATA Cube_cfhddata;			// current cfhd_data used in the cube.
	int Cube_format;			// current output format used in the cube.
	int Cube_output_colorspace;		// current output colorspace used in the cube.
	int use_active_metadata_decoder;		// set if the cube in non-unity
	int apply_color_active_metadata;	// set if the cube in non-unity
	unsigned int last_set_time;		// External Metadata is only checked every 1000ms
	time_t last_time_t;				// External Metadata is only checked every 1000ms
	int decode_resolution;
	int basic_only;					// internal control for no active metadata.
	int use_local_buffer;			// decoding to an interal format be applying 3D or similar corrections

	CFHDDATA cfhddata;				// Extra Information from the AVI Header

	uint32_t *uncompressed_chunk;
	uint32_t uncompressed_size;
	uint32_t sample_uncompressed;
	uint32_t image_dev_only;

	uint8_t *local_output;

//REDTEST
	int frm;
	int run;

//database overrides
	unsigned char *overrideData;
	int overrideSize;

	char OverridePathStr[260];	// default path to overrides
	char LUTsPathStr[260];		// default path to LUTs
	char UserDBPathStr[64];		// database directory in LUTs
/*	unsigned char baseData[MAX_DATADASE_LENGTH]; // default user data
	unsigned int baseDataSize; // default user data
	unsigned char userData[MAX_DATADASE_LENGTH]; // database user data
	unsigned int userDataSize; // database user data
	unsigned char userData2[MAX_DATADASE_LENGTH]; // database user data
	unsigned int userData1Size; // database user data
	unsigned char userData1[MAX_DATADASE_LENGTH]; // database user data
	unsigned int userData2Size; // database user data
	unsigned char userDataB[MAX_DATADASE_LENGTH]; // database user data
	unsigned int userDataBSize; // database user data
	unsigned char forceData[MAX_DATADASE_LENGTH];// override user data
	unsigned int forceDataSize; // override user data
	unsigned char forceData2[MAX_DATADASE_LENGTH];// override user data
	unsigned int forceData2Size; // override user data
	unsigned char forceDataB[MAX_DATADASE_LENGTH];// override user data
	unsigned int forceDataBSize; // override user data*/

	unsigned char *DataBases[METADATA_PRIORITY_MAX+1];
	unsigned int DataBasesAllocSize[METADATA_PRIORITY_MAX+1];
	unsigned int DataBasesSize[METADATA_PRIORITY_MAX+1];

	unsigned char hasFileDB[METADATA_PRIORITY_MAX+1]; // Flag whether .colr existed.

	Thread_cntrl thread_cntrl;		// holds CPU/limits and affinity

	int	premiere_embedded;			// 1 is true
	int cube_base;					// 4= 17x17x17, 5=33x33x33, 6=65x65x65

	uint8_t *upper_plane;			// Used for decoding to the Avid 2.8 output format
	uint8_t *lower_plane;

	int preformatted_3D_type;		// 0 - not preformatted, 1 - under-over, 2 - side-by-side, 3 - fields (full res-only)
	int channel_current;			// 0 - left, 1 - Right
	int channel_decodes;			// 3D work
	int channel_blend_type;			// 3D work,
	int channel_swapped_flags;		// swapped L/R
	int channel_mix_half_res;		// in side-side or stack/interlaced mode using half res decode and scaling up for speed.
	int ghost_bust_left;			// out of 65535
	int ghost_bust_right;			// out of 65535
	unsigned short *sqrttable;
	int sharpen_flip;				// flip the frame in sharpening

	int doVerticalFilter;			// used for sharpen and blur.

	// For Stereo speed
	struct decoder *parallelDecoder;

	// Aligned sample buffer
	uint8_t *aligned_sample_buffer;
	size_t aligned_sample_buffer_size;

	ToolsHandle *tools;
	
	void *vs_surface;	//cast to cairo_surface_t
	void *vs_cr;		//cast to cairo_t
	int vs_surface_w;
	int vs_surface_h;

/*	int histogram;					// set when active
	uint32_t histR[256];
	uint32_t histG[256];
	uint32_t histB[256];
	uint32_t maxR,maxG,maxB;

	int waveformWidth;					// up to 360
	unsigned short waveR[360][256];		// 0-359 screen width, 0-255 intesity, 0 to 65535 instance count. 
	unsigned short waveG[360][256];
	unsigned short waveB[360][256];

	unsigned short scopeUV[256][256];	// 0-255 U, 0-255 V, 0 to 65535 instance count. 
*/
	int source_channels; // 3D file, pseudo preformatted or real multichannel -- either way.
	int real_channels; // number of real video channels (not performatted.)

	int cairo_loaded;
	void *cairoHandle;

	//Different places in memory metadata chunks to search
	int metadatachunks;
	unsigned char *mdc[METADATA_CHUNK_MAX];
	unsigned int mdc_size[METADATA_CHUNK_MAX];
	//unsigned int mdc_crc[METADATA_CHUNK_MAX]; //DAN20100927 - removed CRC a where way to test metadata chuncks, using CompareTags now.

	MDParams MDPdefault;
	MDParams MDPcurrent;
	float last_xypos[16][2]; // all the justification styles
	float last_container_y1[16];
	float last_container_y2[16];
	float ActiveSafe[2]; // w,h 0.1,0.1 default if on
	float TitleSafe[2]; // w,h 0.2,0.2 default if on
	float OverlaySafe[2]; // w,h 0.2,0.2 default if on
	int	drawSafeMarkers;
	int drawmetadataobjects; // number of DSPm found
	unsigned char *dmo[64];
	unsigned int dmo_size[64];
	unsigned int dmo_png_width[64];
	unsigned int dmo_png_height[64];
	char dmo_png_path[64][260];

	unsigned int  LUTcacheCRC; // last LUT CRC currently loaded
	float *LUTcache; // last LUT currently loaded
	int LUTcacheSize; // last LUT currently loaded

	float lastLensOffsetX;
	float lastLensOffsetY;
	float lastLensOffsetZ;
	float lastLensOffsetR;
	float lastLensZoom;
	float lastLensFishFOV;
	int32_t lastLensGoPro;
	uint32_t lastLensSphere;
	uint32_t lastLensFill;
	uint32_t lastLensStyleSel;
	float lastLensCustomSRC[6];
	float lastLensCustomDST[6];
	void *mesh;
	int *lens_correct_buffer;

	
	int lin2curve_type;
	float lin2curve_base;
	int last_cube_depth;
	float contrast;
	float cdl_sat;
	float red_gamma_tweak;
	float grn_gamma_tweak;
	float blu_gamma_tweak;
	float lin2curve[2048+512+2];
	float redgammatweak[2048+512+2];
	float grngammatweak[2048+512+2];
	float blugammatweak[2048+512+2];	
	int curve2lin_type;
	float curve2lin_base;
	float curve2lin[65];
	int cube_depth;
	float linear_mtrx[3][4];
	//float linear_mtrx_highlight_sat[3][4];
	float highlight_desat_gains[3];
	float curved_mtrx[3][4];
	float *LUT;
	int LUTsize;
	//int convert2YUV;
	int broadcastLimit;
	int cg_non_unity;
	int curve_change;
	int useLUT;
	int encode_curve_type1D;
	float encode_curvebase1D;
	float decode_curvebase1D;
	int RawCubeThree1Ds; // 0 - 3D - 1 - 3 x 1D
	
	int pixel_aspect_x;			// Numerator of the pixel aspect ratio // newer, takes precedence over picture_aspect_x if non-zero
	int pixel_aspect_y;			// Denominator of the pixel aspect ratio

	int useAlphaMixDown[2]; // check colors

	Subtitling Subtitles; 

	Keyframing Keyframes;


	//**** High level decoding data *****/

	// The 16 byte license key controls what decoder features are enabled
	//NOTE: The license key must be decrypted into a LICENSE structure
	uint8_t licensekey[16];

} DECODER;

#define FLAG3D_SWAPPED				1
#define FLAG3D_HALFRES				2
#define FLAG3D_GHOSTBUST			4


#define LICENSE_FORMAT_DEEP			1 // greater than 8-bit
#define LICENSE_FORMAT_444			2
#define LICENSE_FORMAT_BAYER		4
#define LICENSE_FORMAT_3D			8
#define LICENSE_FORMAT_ALL			0xf

#define FEATURE_DSHOW_ENCODER		0 // feature is set to zero, likely the licensing for Oceaneering encoders
#define FEATURE_ENCODING_FLAG		1
#define FEATURE_DECODING_FLAG		2
#define FEATURE_ENDUSER_LICENSE		4
#define FEATURE_DECODING_FULL_FLAG	8

typedef struct license		// Decrypted 16 byte license key
{
	uint8_t expire_year;		// value + 2008, 0xf = unlimited, top nibble random for security.
	uint8_t expire_month;		// 1 to 12, 0xf = unlimited, top nibble is random.
	uint8_t expire_day;			// 1 to 31,
	uint8_t format_mask;		// 0 = YUV, 1 = 10-12 bit, 2 = RGB/RGBA, 4 = Bayer, 0xf = all, etc.
								// top nibble is random.

	uint8_t width16;			// value*16 = license width,
								// 128 = 2048, 255 = unlimited, 120 = 1920, 1440 = 90;

	uint8_t height16;			// value*16 = license height,
								// 128 = 2048, 255 = unlimited, 68 = 1088;

	uint16_t max_usage;			// unused, could be the number of encodes, and number frames total,
								// number of frames per encoder, etc.  Likely controlled by Feature flags.

	uint16_t customer_number;	// Unique for corporate customer

	uint16_t feature_flags;		// FEATURE_ENCODING_FLAG or FEATURE_DECODING_FLAG plus future use.

	uint32_t CRC;

} LICENSE;



#define CUSTOMER_NEW			0x0
#define CUSTOMER_CINEFORM		0x0009
//Unlimited encoding license  {0xC1,0xE8,0x57,0xDF,0xB2,0x72,0xCE,0x6C,0xE2,0xCF,0xCB,0x1C,0xBC,0x14,0x6C,0xE8}
//End-user keyed license  {0xBF,0xB6,0x92,0xB3,0x64,0xA8,0xAE,0xF0,0x08,0x9E,0xFB,0xF8,0x14,0x2F,0x26,0x0E}


#define ISBAYER(format)(((format) == COLOR_FORMAT_BYR1) || \
						((format) == COLOR_FORMAT_BYR2) || \
						((format) == COLOR_FORMAT_BYR3) || \
						((format) == COLOR_FORMAT_BYR4) || \
						((format) == COLOR_FORMAT_BYR5)	)


#define IS444(format)  (((format) == COLOR_FORMAT_RGB24) || \
						((format) == COLOR_FORMAT_QT32) || \
						((format) == COLOR_FORMAT_BGRA) || \
						((format) == COLOR_FORMAT_RGB32) || \
						((format) == COLOR_FORMAT_RG48) || \
						((format) == COLOR_FORMAT_RG64) || \
						((format) == COLOR_FORMAT_RG30) || \
						((format) == COLOR_FORMAT_R210) || \
						((format) == COLOR_FORMAT_AR10) || \
						((format) == COLOR_FORMAT_AB10) || \
						((format) == COLOR_FORMAT_DPX0) || \
						((format) == COLOR_FORMAT_B64A) || \
						((format) == COLOR_FORMAT_WP13) || \
						((format) == COLOR_FORMAT_R4FL) || \
						((format) == COLOR_FORMAT_RGB_8PIXEL_PLANAR) || \
						((format) == COLOR_FORMAT_W13A)	)


#define DECODER_INITIALIZER {NULL, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL}


#ifdef __cplusplus
extern "C" {
#endif

//extern QUANT *quant1;		// Default quantization table

// Initialize the current state of the bitstream
void InitCodecState(CODEC_STATE *state);

// Update the encoded (internal) format according to the input format
CODEC_ERROR UpdateEncodedFormat(CODEC_STATE *codec, COLOR_FORMAT input_format);

// Set the encoded format to the default value if it has not been set already
CODEC_ERROR SetDefaultEncodedFormat(CODEC_STATE *codec);

// Update the flags in the codec state using the flag bits encoded in the sample
CODEC_ERROR UpdateCodecFlags(CODEC_STATE *codec, TAGWORD value);

// Return the number of frames in the video sample
int SampleFrameCount(SAMPLE *sample);

// Compute the size of the uncompressed image in bits
uint32_t ComputeImageSizeBits(IMAGE *image);

// Determine appropriate quantization
//int QuantDivisor(QUANT *table, int level, int band);

// Quantization of the highpass coefficients
void QuantizeCoefficients(PIXEL *image, int width, int height, int pitch, int divisor);
void Quantize16s(PIXEL *image, int width, int height, int pitch, int divisor);
void Quantize8s(PIXEL8S *image, int width, int height, int pitch, int divisor);
void Quantize16sTo8s(PIXEL *image, int width, int height, int pitch, int divisor);
void RestoreCoefficients(PIXEL *image, int width, int height, int pitch, int multiplier);

//void QuantizeHighPassCoefficients(ENCODER *encoder, QUANT *table, IMAGE *wavelet);


// Routines for encoding the various headers in the bitstream

void PutVideoSequenceHeader(BITSTREAM *output, int major, int minor, int revision,
							uint32_t flags, int width, int height, int display_height,
							int format, int input_format, int encoded_format, int presentation_width, int presentation_height);

void PutVideoSequenceTrailer(BITSTREAM *output);

void PutVideoGroupHeader(BITSTREAM *output, TRANSFORM *transform, int num_channels, int subband_count,
						 uint32_t **channel_size_vector, int precision, uint32_t frame_number,
						 int input_format, int color_space, int encoder_quality, int encoded_format,
						 int frame_width, int frame_height, int display_height, int presentation_width, int presentation_height);

void PutVideoGroupTrailer(BITSTREAM *output);

// Write an index block for the sample bands
void PutGroupIndex(BITSTREAM *stream, void *index[], int length, uint32_t **channel_size_vector);

// Read the entries in an index block for the sample bands
void DecodeGroupIndex(BITSTREAM *stream, uint32_t *index, int count);

// Write the optional parameters that follow the group header
void PutVideoGroupExtension(BITSTREAM *output, CODEC_STATE *codec);

void PutVideoSampleFlags(BITSTREAM *output, CODEC_STATE *codec);

void PutVideoFrameHeader(BITSTREAM *output, int type, int width, int height, int display_height,
						 int group_index, uint32_t frame_number, int encoded_format, int presentation_width, int presentation_height);

void PutVideoFrameTrailer(BITSTREAM *output);

void PutVideoIntraFrameHeader(BITSTREAM *output, TRANSFORM *transform, int num_channels, int subband_count,
							  uint32_t **channel_size_vector, int precision, uint32_t frame_number,
							  int input_format, int color_space, int encoder_quality, int encoded_format,
							  int width, int height, int display_height, int presentation_width, int presentation_height);

void PutVideoIntraFrameTrailer(BITSTREAM *output);

// Mark the end of the video samples
void PutVideoSampleStop(BITSTREAM *output);

// Output marker between channel information within a group or frame
void PutVideoChannelHeader(BITSTREAM *output, int channel);

// Obsolete version (use EncodeLowPassBand)
//void PutVideoLowPassImage(ENCODER *encoder, IMAGE *wavelet, BITSTREAM *output);

void PutVideoLowPassHeader(BITSTREAM *output,
						   int subband, int level, int width, int height,
						   int left_margin, int top_margin,
						   int right_margin, int bottom_margin,
						   int pixel_offset, int quantization, int bits_per_pixel);

void PutVideoLowPassTrailer(BITSTREAM *output);

// Output a tag and marker before the lowpass coefficients for debugging
void PutVideoLowPassMarker(BITSTREAM *output);

void PutVideoHighPassHeader(BITSTREAM *output,
							int nType,
							int nWaveletNumber,
							int nWaveletLevel,
							int nBandWidth,
							int nBandHeight,
							int nBandCount,
							//int nLowPassBorder,
							//int nHighPassBorder,
							int lowpass_scale,
							int lowpass_divisor);

void PutVideoHighPassTrailer(BITSTREAM *output,
							 uint32_t cntPositive, uint32_t cntNegative,
							 uint32_t cntZeroValues, uint32_t cntZeroTrees,
							 uint32_t cntZeroNodes);

void PutVideoBandHeader(BITSTREAM *output, int band, int width, int height,
						int subband, int encoding, int quantization,
						int scale, int divisor, uint32_t *counters, int codingflags, int do_peaks);

void PutVideoCoefficientHeader(BITSTREAM *output,int band, int coefficient_count,
							   int bits_per_coefficient, int quantization_divisor);

// Append the band end codeword to the encoded coefficients
void FinishEncodeBand(BITSTREAM *output, unsigned int code, int size);

void PutVideoBandTrailer(BITSTREAM *output);
void PutVideoBandMidPoint2Pass(BITSTREAM *output);


int32_t PutRowTrailer(BITSTREAM *output);

void DumpEncodedFrame(BITSTREAM *stream, FILE *dump, uint32_t *bitcount);
void DumpBitstreamFile(BITSTREAM *stream, char *filename, FILE *dump);

//void DecodeFile(DECODER *state, HANDLE file);
//void DecodeFrame(BITSTREAM *stream, IFRAME *frame, FILE *logfile);

void InitDecoder(DECODER *decoder, FILE *logfile, CODESET *cs);
void InitDecoderLicense(DECODER *decoder, const unsigned char *license);
void ExitDecoder(DECODER *decoder);

// Bitstream parsing routines

int FindNextSample(BITSTREAM *stream);

CODEC_ERROR DecodeSequenceHeader(BITSTREAM *stream, SEQUENCE_HEADER *header, int sample_type);

CODEC_ERROR DecodeGroupHeader(BITSTREAM *stream, GROUP_HEADER *header, TRANSFORM *transform, int sample_type);

CODEC_ERROR DecodeLowPassHeader(BITSTREAM *stream, LOWPASS_HEADER *header);

CODEC_ERROR DecodeLowPassTrailer(BITSTREAM *stream, LOWPASS_TRAILER *trailer);

CODEC_ERROR DecodeHighPassHeader(BITSTREAM *stream, HIGHPASS_HEADER *header, int target_level);

CODEC_ERROR DecodeBandHeader(BITSTREAM *stream, BAND_HEADER *header, int band, SCODE_COUNTERS *scode);

CODEC_ERROR DecodeCoeffs(BITSTREAM *stream, IMAGE *wavelet, int band_index,
						 int band_width, int band_height, int coefficient_count,
						 int bits_per_coefficient, int quantization);

CODEC_ERROR DecodeRuns(BITSTREAM *stream, IMAGE *wavelet, int band_index,
					   int band_width, int band_height, int coefficient_count,
					   int bits_per_coefficient, int quantization);

CODEC_ERROR DecodeBandTrailer(BITSTREAM *stream, BAND_TRAILER *trailer);

CODEC_ERROR DecodeHighPassTrailer(BITSTREAM *stream, HIGHPASS_TRAILER *trailer);

CODEC_ERROR DecodeChannelHeader(BITSTREAM *stream, CHANNEL_HEADER *header, int sample_type);

CODEC_ERROR DecodeGroupTrailer(BITSTREAM *stream, GROUP_TRAILER *trailer, int sample_type);

CODEC_ERROR DecodeGroupExtension(BITSTREAM *stream, CODEC_STATE *codec);

CODEC_ERROR DecodeSequenceTrailer(BITSTREAM *stream, SEQUENCE_TRAILER *trailer, int sample_type);

CODEC_ERROR DecodeRowTrailer(BITSTREAM *stream);

CODEC_ERROR DecodeFrameHeader(BITSTREAM *stream, FRAME_HEADER *header, int sample_type);

void InitChannelTransform(TRANSFORM *next, TRANSFORM *prev);

// Free data allocated for decoding a group of frames
void FreeGroup(GROUP *group);

// Can a frame with the specified dimensions be transformed into a wavelet pyramid?
bool IsFrameTransformable(int width, int height, int transform_type, int num_spatial);

bool IsLowPassHeaderMarker(int marker);
bool IsLowPassBandMarker(int marker);
bool IsHighPassBandMarker(int marker);

ENCODED_FORMAT GetEncodedFormat(COLOR_FORMAT input_format, uint32_t quality, uint32_t channel_count);
ENCODED_FORMAT DefaultEncodedFormat(COLOR_FORMAT input_format, uint32_t channel_count);
ENCODED_FORMAT Toggle444vs422EncodedFormat(COLOR_FORMAT format, uint32_t channel_count);
ENCODED_FORMAT Toggle4444vs444EncodedFormat(COLOR_FORMAT format, uint32_t channel_count);
ENCODED_FORMAT Toggle4444vs422EncodedFormat(COLOR_FORMAT format, uint32_t channel_count);

#ifdef __cplusplus
}
#endif

#endif
