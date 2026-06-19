/*! @file codec.c

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
//#include "logo80x18.h"
//#include "logo40x5.h"
#include "../Common/ver.h"

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif
#define TIMING (1 && _TIMING)
#define XMMOPT (1 && _XMMOPT)

#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif

#if (DEBUG && _WIN32)
#include <tchar.h>		// For printing debug string in the console window
#endif

#include "codec.h"
//#include "ipp.h"		// Use Intel Performance Primitives
#include "image.h"
#include "bitstream.h"
//#include "zerotree.h"
#include "debug.h"
#include "color.h"
#include "filter.h"

#include <assert.h>
#include <stdio.h>
#include <memory.h>

#include "codebooks.h"

// Must declare byte swap even though it is an intrinsic
//int _bswap(int);
#include "swap.h"

#ifndef _LOWPASS_BYTES
#define _LOWPASS_BYTES	1	// Use faster loop for reading lowpass coefficients
#endif


// Codec parameters (number of bits per field)

#define CODEC_DIMENSION_SIZE	16		// Frame dimensions
#define CODEC_FORMAT_SIZE		32		// Frame format information
#define CODEC_NUMBITS_SIZE		8
#define CODEC_BORDER_SIZE		8
#define CODEC_SCALE_SIZE		10
#define CODEC_DIVISOR_SIZE		10
#define CODEC_SUBBAND_SIZE		8
#define CODEC_NUMLEVELS_SIZE	8
#define CODEC_NUMCHANNELS_SIZE	8		// Number of color channels
#define CODEC_HIGHINDEX_SIZE	8		// Index of the transform wavelet
#define CODEC_NUMBANDS_SIZE		8
#define CODEC_COUNTER_SIZE		32
#define CODEC_BAND_SIZE			8
#define CODEC_NUMBITS_SIZE		8
#define CODEC_VERSION_SIZE		8		// Each element in the version number
#define CODEC_FLAGS_SIZE		32		// Bitstream option flags
#define CODEC_QUANT_SIZE		16		// Quantization divisor
#define CODEC_PIXEL_SIZE		16		// Number of bits for a non-quantized pixel

#define CODEC_TRANSFORM_SIZE	4		// Organization of the transform
#define CODEC_NUMWAVELETS_SIZE	8		// Number of wavelets in the transform
#define CODEC_NUMFRAMES_SIZE	3		// Number of frames in the group
#define CODEC_NUMSUBBANDS_SIZE	8		// Number of subbands in the group

#define CODEC_WAVELET_SIZE		8		// Type of wavelet
#define CODEC_ENCODING_SIZE		8		// Band encoding method

#define CODEC_BITCOUNT_SIZE		32		// Length of sequence in bits
#define CODEC_CHECKSUM_SIZE		32

#define CODEC_FRAME_TYPE_SIZE	4		// Code for the type of frame
#define CODEC_GROUP_INDEX_SIZE	4		// Index of frame within a group

#define CODEC_CHANNEL_SIZE		4		// Channel number within a group or frame


// New codec paramaters that will be coded in the GOP header
#define CODEC_CHANNEL_BITSTREAM_SIZE	32	// Bitstream size (in bytes) for each channel

#define CODEC_FOURCC(c1, c2, c3, c4)	((c1) << 24 | (c2) << 16 | (c3) << 8 | (c4))

// The marker will be encoded into the bitstream if it is defined

#define CODEC_SEQUENCE_START_CODE		CODEC_FOURCC('C','I','N','E')
#define CODEC_SEQUENCE_START_SIZE		32
#define CODEC_SEQUENCE_END_CODE			CODEC_FOURCC('F','O','R','M')
#define CODEC_SEQUENCE_END_SIZE			32

#define CODEC_GROUP_START_CODE			0x1C4C
#define CODEC_GROUP_START_SIZE			16
#define CODEC_GROUP_END_CODE			0x1B4B
#define CODEC_GROUP_END_SIZE			16

#define CODEC_FRAME_START_CODE			0x0A0A
#define CODEC_FRAME_START_SIZE			16
#define CODEC_FRAME_END_CODE			0x0B0B
#define CODEC_FRAME_END_SIZE			16

#define CODEC_LOWPASS_START_CODE		0x1A4A
#define CODEC_LOWPASS_START_SIZE		16
#define CODEC_LOWPASS_END_CODE			0x1B4B
#define CODEC_LOWPASS_END_SIZE			16

#define CODEC_HIGHPASS_START_CODE		0x0D0D
#define CODEC_HIGHPASS_START_SIZE		16
#define CODEC_HIGHPASS_END_CODE			0x0C0C
#define CODEC_HIGHPASS_END_SIZE			16

#define CODEC_BAND_START_CODE			0x0E0E
#define CODEC_BAND_START_SIZE			16
//#define CODEC_BAND_END_CODE 			0x038F0B3E	//Codeset dependent cs9
//#define CODEC_BAND_END_SIZE 			26			//Codeset dependent cs9
#define CODEC_BAND_END_CODE				0x0000E33F	//Codeset dependent cs15
#define CODEC_BAND_END_SIZE				16			//Codeset dependent cs15

#define CODEC_SAMPLE_STOP_CODE			0x1E1E
#define CODEC_SAMPLE_STOP_SIZE			16

#define CODEC_COEFFICIENT_START_CODE	0x0F0F
#define CODEC_COEFFICIENT_START_SIZE	16

#define CODEC_CHANNEL_START_CODE		0x1F0F
#define CODEC_CHANNEL_START_SIZE		16


// Row end code must be one of the codebook bit patterns reserved for
// use as markers in the run length encoded bitstream so that the code
// does not confuse the fast decoding algorithms.
#if RUNS_ROWEND_MARKER

 #define CODEC_ROWEND_CODE  			0x01C7859E	//Codeset dependent cs9
 #define CODEC_ROWEND_SIZE  			25			//Codeset dependent cs9
// #define CODEC_ROWEND_CODE  			0x0001C67F	//Codeset dependent cs15
// #define CODEC_ROWEND_SIZE  			17			//Codeset dependent cs15

#endif


// Forward references
void PrintCompressionInfo(FILE *logfile, IFRAME *frame);
ENCODED_FORMAT DefaultEncodedFormat(COLOR_FORMAT format, uint32_t channel_count);


// Local Data

// Number of transform levels (excluding the spatial levels) indexed by transform type
static const int numTransformLevels[] =
{
	0,		// Spatial transform
	2,		// Field transform
	1,		// Progressive frames
	2		// Fields combined into interlaced frames
};


// Initialize the current state of the bitstream
void InitCodecState(CODEC_STATE *codec)
{
	static int wavelet_type[] =
	{
		WAVELET_TYPE_FRAME,
		WAVELET_TYPE_FRAME,
		WAVELET_TYPE_TEMPORAL,
		WAVELET_TYPE_SPATIAL,
		WAVELET_TYPE_SPATIAL,
		WAVELET_TYPE_SPATIAL
	};

	int i, unique_framenumber = codec->unique_framenumber; // don't reset as it was al.
	int encoded_format = codec->encoded_format; // we may already know it

	// Clear the codec state
	memset(codec, 0, sizeof(CODEC_STATE));

	codec->unique_framenumber = unique_framenumber;

	// Set the flags that describe the video stream interlace structure
	codec->interlaced_flags = INTERLACED_FLAGS;

	// Set the copy protection flags
	codec->protection_flags = PROTECTION_FLAGS;

	// Set the default aspect ratio
	codec->picture_aspect_x = PICTURE_ASPECT_X;
	codec->picture_aspect_y = PICTURE_ASPECT_Y;
	
	// Set the default chroma offset
	codec->chroma_offset = _CODEC_CHROMA_OFFSET;

	// Set the default frame dimensions
	codec->frame_width = FRAME_WIDTH;
	codec->frame_height = FRAME_HEIGHT;

	// Initialize the default number of frames in the group
	codec->num_frames = FRAMES_PER_GROUP;

	// Initialize the default number of wavelets in the transform
	codec->num_wavelets = WAVELETS_PER_TRANSFORM;

	// Initialize the number of spatial wavelets in each transform
	codec->num_spatial = SPATIALS_PER_TRANSFORM;

	// Initialize the number of channels in each sample
	codec->num_channels = CODEC_NUM_CHANNELS;

	// Initialize the type of transform
	codec->transform_type = TRANSFORM_TYPE_FIELDPLUS;

	// Initialize the largest subband index
	codec->max_subband = CODEC_MAX_SUBBAND;

	// Clear the wavelet dimensions which will be properly
	// initialized after the group header is decoded
	for (i = 0; i < codec->num_wavelets; i++) {
		codec->wavelet[i].type = wavelet_type[i];
		codec->wavelet[i].width = 0;
		codec->wavelet[i].height = 0;
	}

	// Clear the table of channel sizes
	memset(codec->channel_size, 0, sizeof(codec->channel_size));

	// Initialize the number of bits in the source video
	codec->precision = CODEC_PRECISION_DEFAULT;

	// Initialize the default encoding method
	codec->band.encoding = BAND_ENCODING_RUNLENGTHS;

	// Indicate that the encoded format has not been encountered in the bitstream
	codec->encoded_format = encoded_format; //DAN20110210 preserve what we have previously discovered
}

// Update the flags in the codec state using the flag bits encoded in the sample
CODEC_ERROR UpdateCodecFlags(CODEC_STATE *codec, TAGWORD value)
{
	CODEC_ERROR result = CODEC_ERROR_OKAY;

	codec->progressive = !!(value & SAMPLE_FLAGS_PROGRESSIVE);

	return result;
}

// Update the transform data structure using the information in the codec state
void UpdateCodecTransform(TRANSFORM *transform, CODEC_STATE *codec)
{
	transform->type = (TRANSFORM_TYPE)codec->transform_type;
	transform->num_wavelets = codec->num_wavelets;

	// Note: Most of the transform data structure is not used in the new decoder

	// Either eliminate the unused fields or initialize all of the fields when
	// the codec state is updated as tag value pairs are read from the bitstream
}

/*
// Update the encoded (internal) format according to the input format
CODEC_ERROR UpdateEncodedFormat(CODEC_STATE *codec, COLOR_FORMAT input_format)
{
	// Do not replace the encoded format if it was already defined in the bitstream
	if (codec->encoded_format == ENCODED_FORMAT_UNKNOWN)
	{
		// Set the encoded format to an appropriate default value
		codec->encoded_format = DefaultEncodedFormat(input_format);
	}

	return CODEC_ERROR_OKAY;
}
*/

// Set the encoded format to the default value if it has not been set already
CODEC_ERROR SetDefaultEncodedFormat(CODEC_STATE *codec)
{
	// Do not replace the encoded format if it has been set already
	if (codec->encoded_format == ENCODED_FORMAT_UNKNOWN)
	{
		// Set the encoded format to an appropriate default value
		codec->encoded_format = ENCODED_FORMAT_DEFAULT;
	}

	return CODEC_ERROR_OKAY;
}

bool IsLowPassHeaderMarker(int marker)
{
	return (marker == CODEC_LOWPASS_START_CODE);
}

bool IsLowPassBandMarker(int marker)
{
	return (marker == CODEC_COEFFICIENT_START_CODE);
}

bool IsHighPassBandMarker(int marker)
{
	return (marker == CODEC_BAND_START_CODE);
}

int SampleFrameCount(SAMPLE *sample)
{
	int num_frames = 0;

	switch (sample->type)
	{
	case SAMPLE_TYPE_FRAME:
		num_frames = 1;
		break;

	case SAMPLE_TYPE_GROUP:
		num_frames = sample->data.group->header.num_frames;
		break;

	default:
	case SAMPLE_TYPE_NONE:
		num_frames = 0;
		break;
	}

	return num_frames;
}

/*
	Map the original input format of the data provided to the encoder into
	an appropriate value for the encoded format.  This routine is used
	for backward compatibility to provide a value for the encoded format
	when the encoded format is not present in the bitstream.
*/
ENCODED_FORMAT DefaultEncodedFormat(COLOR_FORMAT input_format, uint32_t channel_count)
{
	ENCODED_FORMAT encoded_format = ENCODED_FORMAT_YUV_422;

	switch (input_format)
	{
	// All of the Bayer input formats used the same internal format
	case COLOR_FORMAT_BAYER:
	case COLOR_FORMAT_BYR1:
	case COLOR_FORMAT_BYR2:
	case COLOR_FORMAT_BYR3:
	case COLOR_FORMAT_BYR4:
	case COLOR_FORMAT_BYR5:
		encoded_format = ENCODED_FORMAT_BAYER;
		break;

	//assumed all the 10/16 RGB format wanted 444
	case COLOR_FORMAT_RG30:
	case COLOR_FORMAT_R210:
	case COLOR_FORMAT_DPX0:
	case COLOR_FORMAT_AB10:
	case COLOR_FORMAT_AR10:
	case COLOR_FORMAT_RG48:
	case COLOR_FORMAT_WP13:
		encoded_format = ENCODED_FORMAT_RGB_444;
		break;

	//the RGBA formats default to 444, unless alpha is flagged
	case COLOR_FORMAT_RG64:
	case COLOR_FORMAT_B64A:
	case COLOR_FORMAT_W13A:
		if(channel_count == 4)
			encoded_format = ENCODED_FORMAT_RGBA_4444;
		else
			encoded_format = ENCODED_FORMAT_RGB_444;
		break;
	
	// The QuickTime codec originally used YUV 4:2:2 as the internal format
	case COLOR_FORMAT_R4FL:
		encoded_format = ENCODED_FORMAT_YUV_422;
		break;

	// All 8-bit RGB formats are encoded as YUV
	// Most codecs released before Bayer and RGB 4:4:4 used YUV 4:2:2 internally
	default:
		encoded_format = ENCODED_FORMAT_YUV_422;
		break;
	}

	return encoded_format;
}


ENCODED_FORMAT Toggle444vs422EncodedFormat(COLOR_FORMAT input_format, uint32_t channel_count)
{
	ENCODED_FORMAT encoded_format = DefaultEncodedFormat(input_format, channel_count);

	switch (input_format)
	{
	case COLOR_FORMAT_RGB24:
	case COLOR_FORMAT_RGB32:
	case COLOR_FORMAT_RGB32_INVERTED:
	case COLOR_FORMAT_BGRA32:
	case COLOR_FORMAT_R4FL:
	case COLOR_FORMAT_QT32:
		encoded_format = ENCODED_FORMAT_RGB_444;
		break;

	case COLOR_FORMAT_RG30:
	case COLOR_FORMAT_R210:
	case COLOR_FORMAT_DPX0:
	case COLOR_FORMAT_AB10:
	case COLOR_FORMAT_AR10:
	case COLOR_FORMAT_RG48:
	case COLOR_FORMAT_RG64:
	case COLOR_FORMAT_B64A:
	case COLOR_FORMAT_W13A:
		encoded_format = ENCODED_FORMAT_YUV_422;
		break;
    default:
        break;
	}

	return encoded_format;
}



ENCODED_FORMAT Toggle4444vs444EncodedFormat(COLOR_FORMAT input_format, uint32_t channel_count)
{
	ENCODED_FORMAT encoded_format = DefaultEncodedFormat(input_format, channel_count);

	switch (input_format)
	{
	case COLOR_FORMAT_RGB32:
	case COLOR_FORMAT_BGRA32:
	case COLOR_FORMAT_RGB32_INVERTED:
	case COLOR_FORMAT_QT32:
	case COLOR_FORMAT_RG64:
	case COLOR_FORMAT_B64A:
	case COLOR_FORMAT_R4FL:
		encoded_format = ENCODED_FORMAT_RGBA_4444;
		break;
    default:
        break;
	}

	return encoded_format;
}

ENCODED_FORMAT Toggle4444vs422EncodedFormat(COLOR_FORMAT input_format, uint32_t channel_count)
{
	ENCODED_FORMAT encoded_format = DefaultEncodedFormat(input_format, channel_count);

	switch (input_format)
	{
	case COLOR_FORMAT_RGB32:
	case COLOR_FORMAT_BGRA32:
	case COLOR_FORMAT_RGB32_INVERTED:
	case COLOR_FORMAT_QT32:
	case COLOR_FORMAT_R4FL:
		encoded_format = ENCODED_FORMAT_RGBA_4444;
		break;
		
	case COLOR_FORMAT_RG30:
	case COLOR_FORMAT_R210:
	case COLOR_FORMAT_DPX0:
	case COLOR_FORMAT_AB10:
	case COLOR_FORMAT_AR10:
	case COLOR_FORMAT_RG48:
	case COLOR_FORMAT_RG64:
	case COLOR_FORMAT_B64A:
	case COLOR_FORMAT_W13A:
		encoded_format = ENCODED_FORMAT_YUV_422;
		break;
    default:
        break;
	}

	return encoded_format;
}


ENCODED_FORMAT GetEncodedFormat(COLOR_FORMAT format, uint32_t fixedquality, uint32_t channel_count)
{
	ENCODED_FORMAT encoded_format = ENCODED_FORMAT_YUV_422;

	if(format == 0) 
	{
		if((fixedquality & 0x08000000) == 0x08000000)
			encoded_format = ENCODED_FORMAT_RGB_444;
		else if((fixedquality & 0x20000000)) 
			encoded_format = ENCODED_FORMAT_RGBA_4444;
	}
	else
	{
		if((fixedquality & 0x08000000) == 0x08000000) //CFEncode_Force_RGBasYUV / CFEncode_Force_12bitRGB
		{
			encoded_format = Toggle444vs422EncodedFormat(format, channel_count);
		}
		else if((fixedquality & 0x20000000) == 0x20000000) //CFEncode_Force_12bitAlpha
		{
			encoded_format = Toggle4444vs444EncodedFormat(format, channel_count);
		}
		else if((fixedquality & 0x28000000) == 0x28000000) //CFEncode_Force_RGBasYUV / CFEncode_Force_12bitRGB
		{
			encoded_format = Toggle4444vs422EncodedFormat(format, channel_count);
		}
		else
		{
			encoded_format = DefaultEncodedFormat(format, channel_count);
		}
	}	

	return encoded_format;
}

// Compute the size of the uncompressed image in bits
uint32_t ComputeImageSizeBits(IMAGE *image)
{
	return ((uint32_t)image->width * image->height * BITS_PER_PIXEL);
}


#if 0 //unused
// Apply quantization to 16 bit signed coefficients
void Quantize16s(PIXEL *image, int width, int height, int pitch, int divisor)
{
	// Convert the quantization divisor to a fraction
	PIXEL *rowptr = image;
	short multiplier;
	int shift;
	const int column_step = 8;
	int post_column = width - (width % column_step);
	int row, column;

	if (divisor <= 1) return;

	multiplier = (uint32_t)(1 << 16) / divisor;
	shift = 0;

	// Convert the pitch to pixels
	pitch /= sizeof(PIXEL);

	for (row = 0; row < height; row++)
	{

		column = 0;

#if (1 && XMMOPT)
		__m128i *input_ptr = (__m128i *)rowptr;
		__m128i quant_epi16 = _mm_set1_epi16(multiplier);
		__m128i zero_si128 = _mm_setzero_si128();
		//__m128i round_epi16 = _mm_set1_epi16(1);
		__m128i sign_epi16;
		__m128i input1_epi16;
		__m128i input2_epi16;
		__m128i result_epi16;

		// Quantize eight signed coefficients in parallel
		for (; column < post_column; column += column_step)
		{
			// Load eight pixels and compute the sign
			input1_epi16 = _mm_load_si128(input_ptr);
			sign_epi16 = _mm_cmplt_epi16(input1_epi16, zero_si128);

			// Compute the absolute value
			input1_epi16 = _mm_xor_si128(input1_epi16, sign_epi16);
			input1_epi16 = _mm_sub_epi16(input1_epi16, sign_epi16);

			// Multiply by the quantization factor
			result_epi16 = _mm_mulhi_epu16(input1_epi16, quant_epi16);

			// Restore the sign
			result_epi16 = _mm_xor_si128(result_epi16, sign_epi16);
			result_epi16 = _mm_sub_epi16(result_epi16, sign_epi16);

			// Store the results and advance to the next group
			_mm_store_si128(input_ptr++, result_epi16);
		}

		// Check that the fast loop terminated at the post processing column
		assert(column == post_column);

#endif

		// Finish the rest of the row
		for (; column < width; column++)
		{
			rowptr[column] /= divisor;
		}

		rowptr += pitch;
	}
}
#endif

#if 0 //unused
// Apply quantization to 8 bit signed coefficients
void Quantize8s(PIXEL8S *image, int width, int height, int pitch, int divisor)
{
	// Convert the quantization divisor to a fraction
	PIXEL8S *rowptr = image;
	short multiplier;
	int shift;
	const int column_step = 16;
	int post_column = width - (width % column_step);
	int row, column;

	if (divisor <= 1) return;

	multiplier = (uint32_t)(1 << 16) / divisor;
	shift = 0;

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL8S);

	for (row = 0; row < height; row++)
	{

		column = 0;

#if (1 && XMMOPT)
		__m128i *group_ptr = (__m128i *)rowptr;
		__m128i quant_epi16 = _mm_set1_epi16(multiplier);
		__m128i zero_si128 = _mm_setzero_si128();
		//__m128i round_epi16 = _mm_set1_epi16(1);
		__m128i group_epi8;
		__m128i sign_epi8;
		__m128i input1_epi16;
		__m128i input2_epi16;
		__m128i result_epi8;

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

		// Check that the fast loop terminated at the post processing column
		assert(column == post_column);

#endif

		// Finish the rest of the row
		for (; column < width; column++)
		{
			rowptr[column] /= divisor;
		}

		rowptr += pitch;
	}
}
#endif

#if 0
// Quantize the highpass coefficients and pack into signed bytes
void Quantize16sTo8s(PIXEL *image, int width, int height, int pitch, int quantization)
{
	// Convert quantization divisor to a fraction
	short multiplier = (uint32_t)(1 << 16) / quantization;
	PIXEL *rowptr = image;
	PIXEL8S *outptr = (PIXEL8S *)image;
	int input_pitch = pitch/sizeof(PIXEL);
	int output_pitch = pitch/sizeof(PIXEL8S);
	int row, column;

	for (row = 0; row < height; row++)
	{
		for (column = 0; column < width; column++) {
			PIXEL value = rowptr[column];
			int32_t result;

			// Multiply by the quantization fraction and take the high part
			result = (multiplier * ((long)value)) >> 16;
			//result = ((long)value) / quantization;

			// Saturate to an 8 bit signed number
			assert(PIXEL8S_MIN <= result && result <= PIXEL8S_MAX);
			result = SATURATE_8S(result);

			outptr[column] = (char)result;
		}

		// Advance the input and output pointers
		rowptr += input_pitch;
		outptr += output_pitch;
	}
}
#endif


/***** Encoding Routines *****/

void PutVideoSequenceHeader(BITSTREAM *output, int major, int minor, int revision, uint32_t flags,
							int width, int height, int display_height, int format, int input_format, int encoded_format, int presentation_width, int presentation_height)
{
	int reserved = 0;

#if _CODEC_TAGS

	// Output the type of sample
	PutTagPair(output, CODEC_TAG_SAMPLE, SAMPLE_TYPE_SEQUENCE_HEADER);

	// Output the video sequence header
	PutTagPair(output, CODEC_TAG_VERSION_MAJOR, major);
	PutTagPair(output, CODEC_TAG_VERSION_MINOR, minor);
	PutTagPair(output, CODEC_TAG_VERSION_REVISION, revision);
	PutTagPair(output, CODEC_TAG_VERSION_EDIT, reserved);
	PutTagPair(output, CODEC_TAG_SEQUENCE_FLAGS, flags);

	// Output the dimensions and format of each frame in the sequence
	PutTagPair(output, CODEC_TAG_FRAME_WIDTH, width);
	PutTagPair(output, CODEC_TAG_FRAME_HEIGHT, height);

	if(width != presentation_width && presentation_width > 0)
		PutTagPair(output, CODEC_TAG_PRESENTATION_WIDTH, presentation_width);
	if(height != presentation_height && presentation_height > 0)
		PutTagPair(output, CODEC_TAG_PRESENTATION_HEIGHT, presentation_height);

	PutTagPair(output, CODEC_TAG_FRAME_FORMAT, format);

	if (input_format >= COLOR_FORMAT_INPUT_FORMAT_TAG_REQUIRED)
	{
		PutTagPair(output, CODEC_TAG_INPUT_FORMAT, input_format);
	}
	else
	{
		PutTagPairOptional(output, CODEC_TAG_INPUT_FORMAT, input_format);
		//PutTagPairOptional(output, CODEC_TAG_ENCODED_FORMAT, encoded_format);
	}

	// The encoded format is required if the default format is not used
	if (encoded_format != ENCODED_FORMAT_DEFAULT)
	{
		// Check that the encoded format is valid
		assert(encoded_format <= ENCODED_FORMAT_MAXIMUM);

		// The decoder requires the encoded format if it is not the default
		PutTagPair(output, CODEC_TAG_ENCODED_FORMAT, encoded_format);
	}



#if (0 && DEBUG)
	// Indicate that this is the end of the sample
	PutTagPair(output, CODEC_TAG_SAMPLE_END, 0);
#endif

#else

	// Align start of header with a bitword boundary
	PadBits(output);

#ifdef CODEC_SEQUENCE_START_CODE
	PutBits(output, CODEC_SEQUENCE_START_CODE, CODEC_SEQUENCE_START_SIZE);
	PutBits(output, major, CODEC_VERSION_SIZE);
	PutBits(output, minor, CODEC_VERSION_SIZE);
	PutBits(output, revision, CODEC_VERSION_SIZE);
	PutBits(output, reserved, CODEC_VERSION_SIZE);		// For padding
	PutBits(output, flags, CODEC_FLAGS_SIZE);
#endif

	// Output the dimensions and format of each frame in the sequence
	PutBits(output, width, CODEC_DIMENSION_SIZE);
	PutBits(output, height, CODEC_DIMENSION_SIZE);
	PutBits(output, format, CODEC_FORMAT_SIZE);

#endif
}

void PutVideoSequenceTrailer(BITSTREAM *output)
{
	uint32_t checksum = 0;

	// Align start of trailer with a bitword boundary
	PadBits(output);

#ifdef CODEC_SEQUENCE_END_CODE
	PutBits(output, CODEC_SEQUENCE_END_CODE, CODEC_SEQUENCE_END_SIZE);

	// Output the length of the bitstream for computing the compression ratio
#if (0 && _TIMING)
	PutBits(output, (uint32_t)output->cntBits, CODEC_BITCOUNT_SIZE);
#else
	PutBits(output, 0, CODEC_BITCOUNT_SIZE);
#endif

	// Output a checksum
	PutBits(output, checksum, CODEC_CHECKSUM_SIZE);
#endif
}

void PutVideoGroupHeader(BITSTREAM *output, TRANSFORM *transform, int num_channels, int subband_count,
						 uint32_t **channel_size_vector, int precision, uint32_t frame_number,
						 int input_format, int color_space, int encoder_quality, int encoded_format,
						 int frame_width, int frame_height, int display_height, int presentation_width, int presentation_height)
{
#if _CODEC_TAGS

	IMAGE *wavelet;
	int num_wavelets;
	int first_wavelet;
	int width = frame_width;
	int height = frame_height;
	//int i;

	// Align start of header with a bitword boundary
	//PadBits(output);

	// Align the start of the header on a tag boundary
	PadBitsTag(output);

	// The bitstream should be aligned to a tag boundary
	assert(IsAlignedTag(output));

	// Output the tag for the group header
	PutTagPair(output, CODEC_TAG_SAMPLE, SAMPLE_TYPE_GROUP);

	// Align the header on uint32_t  boundary before writing the channel size information
	//PadBits32(output);

	// Number of bytes each channel occupies in the bitstream
	// At this point 0 is written temporarily for each channel.
	// The current numbers will be written after the GOP encoding is finished
	PutGroupIndex(output, NULL, num_channels, channel_size_vector);

	// Align the header on uint32_t  boundary before writing the channel size information
	//PadBits32(output);

	// The bitstream should be aligned to a tag boundary
	assert(IsAlignedTag(output));

	// Output the type of transform
	PutTagPair(output, CODEC_TAG_TRANSFORM_TYPE, transform->type);

	// Output the number of frames in the group
	PutTagPair(output, CODEC_TAG_NUM_FRAMES, transform->num_frames);

#if (0 && DEBUG)
	// Test that the decoder ignores randomly placed optional tags
	PutTagPairOptional(output, CODEC_TAG_PROTECTION_FLAGS, 0);
#endif

#if (0 && DEBUG)
	// Test how the decoder handles an unknown mandatory tag
	PutTagPair(output, CODEC_TAG_COUNT+1, 0);
#endif

	// Output the number of channels
	PutTagPair(output, CODEC_TAG_NUM_CHANNELS, num_channels);

	// Original source video format (RGB, YUY2, BYR1, etc.)
	if (input_format >= COLOR_FORMAT_INPUT_FORMAT_TAG_REQUIRED)
	{
		// The format is required for proper decoding
		PutTagPair(output, CODEC_TAG_INPUT_FORMAT, input_format);
	}
	else
	{
		// The format is optional information
		PutTagPairOptional(output, CODEC_TAG_INPUT_FORMAT, input_format);
		//PutTagPairOptional(output, CODEC_TAG_ENCODED_FORMAT, encoded_format);
	}

	// The encoded format is required if the default format is not used
	if (encoded_format != ENCODED_FORMAT_DEFAULT)
	{
		// Check that the encoded format is valid
		assert(encoded_format <= ENCODED_FORMAT_MAXIMUM);

		// The decoder requires the encoded format if it is not the default
		PutTagPair(output, CODEC_TAG_ENCODED_FORMAT, encoded_format);
	}

	// Color space of the original video source
	switch(encoded_format)
	{
		case ENCODED_FORMAT_YUV_422:
			color_space &= ~COLOR_SPACE_VS_RGB; // only store 601 vs 709
			break;
		case ENCODED_FORMAT_BAYER:
			color_space = 0;
			break;
		case ENCODED_FORMAT_RGB_444:
		case ENCODED_FORMAT_RGBA_4444:
			color_space &= ~(COLOR_SPACE_BT_601|COLOR_SPACE_BT_709); // only store VS vs CG
			break;
	}
	if(color_space)
	{
		PutTagPairOptional(output, CODEC_TAG_ENCODED_COLORSPACE, color_space);  //DAN20080716
	}


	// The decoder may want to allocate storage for all wavelets at the beginning
	num_wavelets = transform->num_wavelets;
	PutTagPair(output, CODEC_TAG_NUM_WAVELETS, num_wavelets);

	// Inform the decoder of the number of subbands in the transform in case
	// the decoder needs to allocate space for all subbands at the beginning
	// Note that fewer subbands may be encoded (each subband has its own header)
	PutTagPair(output, CODEC_TAG_NUM_SUBBANDS, subband_count);

	// The number of spatial levels is independent of the type of transform
	PutTagPair(output, CODEC_TAG_NUM_SPATIAL, transform->num_spatial);

	// The decoder needs to allocate the first wavelet before decoding it and
	// therefore needs to be told the type of wavelet to allocate so it knows
	// how many bands to create (the dimensions and level are available earlier)
	first_wavelet = num_wavelets - 1;
	wavelet = transform->wavelet[first_wavelet];
	PutTagPair(output, CODEC_TAG_FIRST_WAVELET, wavelet->wavelet_type);

	// Inform the decoder of the final size of the image being transmitted
	//width = transform->width;
	//height = transform->height;
	PutTagPair(output, CODEC_TAG_FRAME_WIDTH, width);
	PutTagPair(output, CODEC_TAG_FRAME_HEIGHT, height);

	if(width != presentation_width && presentation_width > 0)
		PutTagPair(output, CODEC_TAG_PRESENTATION_WIDTH, presentation_width);
	if(height != presentation_height && presentation_height > 0)
		PutTagPair(output, CODEC_TAG_PRESENTATION_HEIGHT, presentation_height);

	// Encode the frame number into the bitstream (useful for debugging)
	PutTagPairOptional(output, CODEC_TAG_FRAME_NUMBER, frame_number);

	// Was the original video high resolution?
	if (precision != CODEC_PRECISION_DEFAULT)
	{
		// Encode the precision of the source video
		PutTagPair(output, CODEC_TAG_PRECISION, precision);
	}

	PutTagPairOptional(output, CODEC_TAG_FRAME_DISPLAY_HEIGHT, display_height);
  //#endif

	// Send out the version number
	{
		int version[4] = { FILE_VERSION_NUMERIC };
		int ver,subver,subsubver,code;

		ver = version[0];
		subver = version[1];
		subsubver = version[2];

		code = (ver << 12) | (subver << 8) | (subsubver);
		PutTagPairOptional(output, CODEC_TAG_VERSION, code);			// later filled in if needed
	}

	// What quality is being encoded
	PutTagPairOptional(output, CODEC_TAG_QUALITY_L, encoder_quality & 0xffff);
	PutTagPairOptional(output, CODEC_TAG_QUALITY_H, (encoder_quality >> 16) & 0xffff);

	{
		unsigned int i,prescaletable =0;
		for(i=0;i<TRANSFORM_MAX_WAVELETS;i++)
			prescaletable += transform->prescale[i]<<(14-i*2);

		//NOTE : if the values with SetTransformPrescale() change from the
		// hardware values of GetTransformPrescale() the CODEC_TAG_PRESCALE_TABLE TAG can't be optional.
		if(TestTransformPrescaleMatch(transform, transform->type, precision) == true)
		{
			PutTagPairOptional(output, CODEC_TAG_PRESCALE_TABLE, prescaletable );
		}
		else
		{
			PutTagPair(output, CODEC_TAG_PRESCALE_TABLE, prescaletable );
		}
	}

#else
	IMAGE *wavelet;
	int num_wavelets;
	int first_wavelet;
	int i;

	uint8_t  *temp = output->lpCurrentWord;

	// Align start of header with a bitword boundary
	PadBits(output);

#ifdef CODEC_GROUP_START_CODE
	PutBits(output, CODEC_GROUP_START_CODE, CODEC_GROUP_START_SIZE);
#endif

	PutBits(output, transform->type, CODEC_TRANSFORM_SIZE);
	PutBits(output, transform->num_frames, CODEC_NUMFRAMES_SIZE);

	// Use the number of levels field for encoding the number of channels
	// since the number of levels can be computed from the transform type
	// and the number of spatial transforms
	//PutBits(output, transform->num_levels, CODEC_NUMLEVELS_SIZE);

	// The size of the field for the number of levels must equal the size of the field
	// for the number of color channels for backward compatibility
	assert(CODEC_NUMCHANNELS_SIZE == CODEC_NUMLEVELS_SIZE);

	// Encode the number of color channels
	PutBits(output, num_channels, CODEC_NUMCHANNELS_SIZE);

	// The decoder may want to allocate storage for all wavelets at the beginning
	num_wavelets = transform->num_wavelets;
	PutBits(output, num_wavelets, CODEC_NUMWAVELETS_SIZE);

	// Inform the decoder of the number of subbands in the transform in case
	// the decoder needs to allocate space for all subbands at the beginning
	// Note that fewer subbands may be encoded (each subband has its own header)
	PutBits(output, subband_count, CODEC_NUMSUBBANDS_SIZE);

	// The number of spatial levels is independent of the type of transform
	PutBits(output, transform->num_spatial, CODEC_NUMLEVELS_SIZE);

	// The decoder needs to allocate the first wavelet before decoding it and
	// therefore needs to be told the type of wavelet to allocate so it knows
	// how many bands to create (the dimensions and level are available earlier)
	first_wavelet = num_wavelets - 1;
	wavelet = transform->wavelet[first_wavelet];
	PutBits(output, wavelet->wavelet_type, CODEC_WAVELET_SIZE);

	// Align the header on uint32_t  boundary before writing the channel size information
	PadBits32(output);

	// Number of bytes each channel occupies in the bitstream
	// At this point 0 is written temporarily for each channel.
	// The current numbers will be written after the GOP encoding is finished
	for(i = 0; i < num_channels; i++)
	 	WriteLong(output, 0, CODEC_CHANNEL_BITSTREAM_SIZE);

#endif
}

void PutVideoGroupTrailer(BITSTREAM *output)
{
#if _CODEC_TAGS

	unsigned short checksum;

	// Need to properly compute the checksum
	checksum = 0;

	PadBitsTag(output);
	PutTagPair(output, CODEC_TAG_SAMPLE, SAMPLE_TYPE_GROUP_TRAILER);
	PutTagPair(output, CODEC_TAG_GROUP_TRAILER, checksum);

#if (0 && DEBUG)
	// Mark the end of the sample
	PutTagPair(output, CODEC_TAG_SAMPLE_END, 0);
#endif

#else

#ifdef CODEC_GROUP_END_CODE
	// Align start of trailer with a bitword boundary
	PadBits(output);

	// Output the marker for the group trailer
	PutBits(output, CODEC_GROUP_END_CODE, CODEC_GROUP_END_SIZE);
#endif

#endif
}

// Write an index block for the sample bands
void PutGroupIndex(BITSTREAM *stream,
				   void *index[],
				   int length,
				   uint32_t **channel_size_vector)
{
	int i;

	// Output the tag and the length of the index
	PutTagPair(stream, CODEC_TAG_INDEX, length);

	// Save the location of the channel size vector
	if (channel_size_vector != NULL) {
		*channel_size_vector = (uint32_t *)stream->lpCurrentWord;
	}

	// Was a vector of index entries provided by the caller?
	if (index == NULL) {
		// Output an empty vector of longwords for the index entries
		// that can be filled later when the addresses are available
		for (i = 0; i < length; i++) PutTagPair(stream, CODEC_TAG_ENTRY, (uint32_t )i);
	}
	else {
		// Output the vector of longwords for the index entries
		for (i = 0; i < length; i++) {
			uintptr_t longword = (uintptr_t)index[i];
			assert(longword <= UINT32_MAX);
			PutLong(stream, (uint32_t )longword);
		}
	}
}

#if 0
// Read an index block for the sample bands
void GetGroupIndex(BITSTREAM *stream, void *index[], int length)
{
	TAGVALUE segment;
	int count;
	int i;

	segment = GetTagValue(stream);

	assert(stream->error == BITSTREAM_ERROR_OKAY);
	if (stream->error != BITSTREAM_ERROR_OKAY)
		return;

	assert(segment.tuple.tag == CODEC_TAG_INDEX);
	if (segment.tuple.tag != CODEC_TAG_INDEX) {
		stream->error = BITSTREAM_ERROR_BADTAG;
		return;
	}

	count = segment.tuple.value;
	if (count > length) count = length;

	for (i = 0; i < count; i++) {
		index[i] = (void *)(uintptr_t)GetLong(stream);
	}
}
#endif

// Read the entries in an index block for the sample bands
void DecodeGroupIndex(BITSTREAM *stream, uint32_t *index, int count)
{
	int i;

	for (i = 0; i < count; i++)
		index[i] = GetLong(stream);
}


// Write the optional parameters that follow the group header
void PutVideoGroupExtension(BITSTREAM *output, CODEC_STATE *codec)
{
	int interlaced_flags = codec->interlaced_flags;
	int protection_flags = codec->protection_flags;
	int picture_aspect_x = codec->picture_aspect_x;
	int picture_aspect_y = codec->picture_aspect_y;

	// Write the interlaced flags
	interlaced_flags &= CODEC_FLAGS_INTERLACED_MASK;
	PutTagPairOptional(output, CODEC_TAG_INTERLACED_FLAGS, interlaced_flags);

	// Write the copy protection bits
	protection_flags &= CODEC_FLAGS_PROTECTION_MASK;
	PutTagPairOptional(output, CODEC_TAG_PROTECTION_FLAGS, protection_flags);

	// Write the pixel aspect ratio
	if (!(0 <= picture_aspect_x && picture_aspect_x <= SHRT_MAX)) picture_aspect_x = SHRT_MAX;
	if (!(0 <= picture_aspect_y && picture_aspect_y <= SHRT_MAX)) picture_aspect_y = SHRT_MAX;
	PutTagPairOptional(output, CODEC_TAG_PICTURE_ASPECT_X, picture_aspect_x);
	PutTagPairOptional(output, CODEC_TAG_PICTURE_ASPECT_Y, picture_aspect_y);
}


#if _CODEC_SAMPLE_FLAGS

void PutVideoSampleFlags(BITSTREAM *output, CODEC_STATE *codec)
{
	// The flags are all zero by default
	int flags = 0;

	// Encode the codec state in the flag bits
	if (codec->progressive) {
		flags |= SAMPLE_FLAGS_PROGRESSIVE;
	}

	// Output the encoding flags (if any of the flags were set)
	if (flags != 0) {
		PutTagPair(output, CODEC_TAG_SAMPLE_FLAGS, flags);
	}
}

#endif

void PutVideoSampleStop(BITSTREAM *output)
{
#if _CODEC_TAGS
	assert(0);
#else

#ifdef CODEC_SAMPLE_STOP_CODE
	PutBits(output, CODEC_SAMPLE_STOP_CODE, CODEC_SAMPLE_STOP_SIZE);
#endif

#endif
}

void PutVideoChannelHeader(BITSTREAM *output, int channel)
{
#if _CODEC_TAGS

	PadBitsTag(output);
	PutTagPair(output, CODEC_TAG_SAMPLE, SAMPLE_TYPE_CHANNEL);
	PutTagPair(output, CODEC_TAG_CHANNEL, channel);

#else

#ifdef CODEC_CHANNEL_START_CODE
	// Align start of header with a bitword boundary
	PadBits(output);

	PutBits(output, CODEC_CHANNEL_START_CODE, CODEC_CHANNEL_START_SIZE);
	PutBits(output, channel, CODEC_CHANNEL_SIZE);

	// Align end of header with a bitword boundary
	PadBits(output);
#endif

#endif
}

void PutVideoFrameHeader(BITSTREAM *output, int type, int width, int height, int display_height,
						 int group_index, uint32_t frame_number, int encoded_format, int presentation_width, int presentation_height)
{
#if _CODEC_TAGS

	// Align the start of the header on a tag boundary
	//PadBitsTag(output);

	// The bitstream should be aligned to a tag boundary
	assert(IsAlignedTag(output));

	// Output the tag for the frame header
	PutTagPair(output, CODEC_TAG_SAMPLE, SAMPLE_TYPE_FRAME);

	// Output the start of frame marker
	PutTagPair(output, CODEC_TAG_FRAME_TYPE, type);

	// Inform the decoder of the final size of the image being transmitted
	PutTagPair(output, CODEC_TAG_FRAME_WIDTH, width);
	PutTagPair(output, CODEC_TAG_FRAME_HEIGHT, height);

	if(width != presentation_width && presentation_width > 0)
		PutTagPair(output, CODEC_TAG_PRESENTATION_WIDTH, presentation_width);
	if(height != presentation_height && presentation_height > 0)
		PutTagPair(output, CODEC_TAG_PRESENTATION_HEIGHT, presentation_height);

  #if 1
	if (encoded_format != ENCODED_FORMAT_DEFAULT)
	{
		// Check that the encoded format is valid
		assert(encoded_format <= ENCODED_FORMAT_MAXIMUM);

		// The decoder requires the encoded format if it is not the default
		PutTagPair(output, CODEC_TAG_ENCODED_FORMAT, encoded_format);
	}
  #endif

	// Encode the frame number into the bitstream (useful for debugging)
	PutTagPairOptional(output, CODEC_TAG_FRAME_NUMBER, frame_number);

	// Indicate the position of this frame within the group (zero is the first frame)
	PutTagPair(output, CODEC_TAG_FRAME_INDEX, group_index);

#else

  #ifdef CODEC_FRAME_START_CODE
	// Output the start of frame marker
	PutBits(output, CODEC_FRAME_START_CODE, CODEC_FRAME_START_SIZE);
  #endif

	// Indicate the type of frame
	PutBits(output, type, CODEC_FRAME_TYPE_SIZE);

	// Inform the decoder of the final size of the image being transmitted
	PutBits(output, width, CODEC_DIMENSION_SIZE);
	PutBits(output, height, CODEC_DIMENSION_SIZE);

	// Indicate the position of this frame within the group (zero is the first frame)
	PutBits(output, group_index, CODEC_GROUP_INDEX_SIZE);

#endif
}

void PutVideoFrameTrailer(BITSTREAM *output)
{
#if _CODEC_TAGS

	unsigned short checksum;

	// Need to properly compute the checksum
	checksum = 0;

	PadBitsTag(output);
	//PutTagPair(output, CODEC_TAG_SAMPLE, SAMPLE_TYPE_FRAME_TRAILER);
	PutTagPair(output, CODEC_TAG_FRAME_TRAILER, checksum);

#if (0 && DEBUG)
	// Mark the end of the sample
	PutTagPair(output, CODEC_TAG_SAMPLE_END, 0);
#endif


#else

#ifdef CODEC_FRAME_END_CODE

#if TIMING
	uint32_t bitcount;
#endif

	// Output the end of frame marker
	PutBits(output, CODEC_FRAME_END_CODE, CODEC_FRAME_END_SIZE);

#if (0 && TIMING)
	// Output the number of bits placed in the bitstream (for debugging)
	bitcount = (uint32_t)output->cntBits;
	PutBits(output, bitcount, CODEC_COUNTER_SIZE);
#endif

#endif

#endif
}

#if _CODEC_TAGS

void PutVideoIntraFrameHeader(BITSTREAM *output, TRANSFORM *transform, int num_channels, int subband_count,
							  uint32_t **channel_size_vector, int precision, uint32_t frame_number,
							  int input_format, int color_space, int encoder_quality, int encoded_format,
							  int width, int height, int display_height, int presentation_width, int presentation_height)
{
	IMAGE *wavelet;
	int num_wavelets;
	int first_wavelet;
	//int i;

	// Check that a valid transform was supplied
	assert(transform != NULL);

	// Align start of header with a bitword boundary
	//PadBits(output);

	// Align the start of the header on a tag boundary
	PadBitsTag(output);

	// The bitstream should be aligned to a tag boundary
	assert(IsAlignedTag(output));

	// Output the tag for the group header
	PutTagPair(output, CODEC_TAG_SAMPLE, SAMPLE_TYPE_IFRAME);

	// Number of bytes each channel occupies in the bitstream
	// At this point 0 is written temporarily for each channel.
	// The current numbers will be written after the GOP encoding is finished
	PutGroupIndex(output, NULL, num_channels, channel_size_vector);

	// Align the header on uint32_t  boundary before writing the channel size information
	//PadBits32(output);

	// The bitstream should be aligned to a tag boundary
	assert(IsAlignedTag(output));

	// Output the type of transform
	PutTagPair(output, CODEC_TAG_TRANSFORM_TYPE, transform->type);

	// Output the number of frames in the group
	PutTagPair(output, CODEC_TAG_NUM_FRAMES, transform->num_frames);

	// Output the number of channels
	PutTagPair(output, CODEC_TAG_NUM_CHANNELS, num_channels);

	// Original source video format (RGB, YUY2, BYR1, etc.)
	if (input_format >= COLOR_FORMAT_INPUT_FORMAT_TAG_REQUIRED)
	{
		// The format is required for proper decoding
		PutTagPair(output, CODEC_TAG_INPUT_FORMAT, input_format);
	}
	else
	{
		// The format is optional information
		PutTagPairOptional(output, CODEC_TAG_INPUT_FORMAT, input_format);
		//PutTagPairOptional(output, CODEC_TAG_ENCODED_FORMAT, encoded_format);
	}

	// The encoded format is required if the default format is not used
	//if (encoded_format != ENCODED_FORMAT_DEFAULT) //DAN20110218 aways insert now.
	{
		// Check that the encoded format is valid
		assert(encoded_format <= ENCODED_FORMAT_MAXIMUM);

		// The decoder requires the encoded format if it is not the default
		PutTagPair(output, CODEC_TAG_ENCODED_FORMAT, encoded_format);
	}

	// Color space of the original video source
	switch(encoded_format)
	{
		case ENCODED_FORMAT_YUV_422:
			color_space &= ~COLOR_SPACE_VS_RGB; // only store 601 vs 709
			break;
		case ENCODED_FORMAT_BAYER:
			color_space = 0;
			break;
		case ENCODED_FORMAT_RGB_444:
		case ENCODED_FORMAT_RGBA_4444:
			color_space &= ~(COLOR_SPACE_BT_601|COLOR_SPACE_BT_709); // only store VS vs CG
			break;
	}
	if(color_space)
	{
		PutTagPairOptional(output, CODEC_TAG_ENCODED_COLORSPACE, color_space); //DAN20080716 - added
	}

	// The decoder may want to allocate storage for all wavelets at the beginning
	num_wavelets = transform->num_wavelets;
	PutTagPair(output, CODEC_TAG_NUM_WAVELETS, num_wavelets);

	// Inform the decoder of the number of subbands in the transform in case
	// the decoder needs to allocate space for all subbands at the beginning
	// Note that fewer subbands may be encoded (each subband has its own header)
	PutTagPair(output, CODEC_TAG_NUM_SUBBANDS, subband_count);

	// The number of spatial levels is independent of the type of transform
	PutTagPair(output, CODEC_TAG_NUM_SPATIAL, transform->num_spatial);

	// The decoder needs to allocate the first wavelet before decoding it and
	// therefore needs to be told the type of wavelet to allocate so it knows
	// how many bands to create (the dimensions and level are available earlier)
	first_wavelet = num_wavelets - 1;
	wavelet = transform->wavelet[first_wavelet];
	PutTagPair(output, CODEC_TAG_FIRST_WAVELET, wavelet->wavelet_type);

	// Inform the decoder of the final size of the image being transmitted
	//width = transform->width;
	//height = transform->height;
	PutTagPair(output, CODEC_TAG_FRAME_WIDTH, width);
	PutTagPair(output, CODEC_TAG_FRAME_HEIGHT, height);

	if(width != presentation_width && presentation_width > 0)
		PutTagPair(output, CODEC_TAG_PRESENTATION_WIDTH, presentation_width);
	if(height != presentation_height && presentation_height > 0)
		PutTagPair(output, CODEC_TAG_PRESENTATION_HEIGHT, presentation_height);

	// Encode the frame number into the bitstream (useful for debugging)
	PutTagPairOptional(output, CODEC_TAG_FRAME_NUMBER, frame_number);

	// Was the original video high resolution?
	if (precision != CODEC_PRECISION_DEFAULT)
	{
		// Encode the precision of the source video
		PutTagPair(output, CODEC_TAG_PRECISION, precision);
	}
//#endif
	PutTagPairOptional(output, CODEC_TAG_FRAME_DISPLAY_HEIGHT, display_height);

	// Send out the version number
	{
		int version[4] = { FILE_VERSION_NUMERIC };
		int ver,subver,subsubver,code;

		ver = version[0];
		subver = version[1];
		subsubver = version[2];

		code = (ver << 12) | (subver << 8) | (subsubver);
		PutTagPairOptional(output, CODEC_TAG_VERSION, code);			// later filled in if needed
	}

	// What quality is being encoded
	PutTagPairOptional(output, CODEC_TAG_QUALITY_L, encoder_quality & 0xffff);
	PutTagPairOptional(output, CODEC_TAG_QUALITY_H, (encoder_quality >> 16) & 0xffff);

	{
		unsigned int i,prescaletable =0;
		for(i=0;i<TRANSFORM_MAX_WAVELETS;i++)
			prescaletable += transform->prescale[i]<<(14-i*2);

		//NOTE : if the values with SetTransformPrescale() change from the
		// hardware values of GetTransformPrescale() the CODEC_TAG_PRESCALE_TABLE TAG can't be optional.
		if(TestTransformPrescaleMatch(transform, transform->type, precision) == true)
		{
			PutTagPairOptional(output, CODEC_TAG_PRESCALE_TABLE, prescaletable );
		}
		else
		{
			PutTagPair(output, CODEC_TAG_PRESCALE_TABLE, prescaletable );
		}
	}
}

void PutVideoIntraFrameTrailer(BITSTREAM *output)
{
	unsigned short checksum;

	// Need to properly compute the checksum
	checksum = 0;

	PadBitsTag(output);
	//PutTagPair(output, CODEC_TAG_SAMPLE, SAMPLE_TYPE_FRAME_TRAILER);
	PutTagPair(output, CODEC_TAG_FRAME_TRAILER, checksum);

#if (0 && DEBUG)
	// Mark the end of the sample
	PutTagPair(output, CODEC_TAG_SAMPLE_END, 0);
#endif
}

#endif

void PutVideoLowPassHeader(BITSTREAM *output,
						   int subband, int level, int width, int height,
						   int left_margin, int top_margin,
						   int right_margin, int bottom_margin,
						   int pixel_offset, int quantization, int bits_per_pixel)
{
#if _CODEC_TAGS

#if (_CODEC_MARKERS && CODEC_LOWPASS_START_CODE)
	// Output a marker for debugging
	assert(CODEC_LOWPASS_START_SIZE == 16);
	PutTagMarker(output, CODEC_LOWPASS_START_CODE, CODEC_LOWPASS_START_SIZE);
#endif

	// Output the lowpass subband number
	PutTagPair(output, CODEC_TAG_LOWPASS_SUBBAND, subband);

	// Inform the decoder of the number of levels at the top of the pyramid
	// (The number of levels transmitted may be less)
	PutTagPair(output, CODEC_TAG_NUM_LEVELS, level);

	// Inform the decoder of the lowpass image dimensions
	PutTagPair(output, CODEC_TAG_LOWPASS_WIDTH, width);
	PutTagPair(output, CODEC_TAG_LOWPASS_HEIGHT, height);

	// The transmitted dimensions may be less than the image dimensions
	PutTagPair(output, CODEC_TAG_MARGIN_LEFT, left_margin);
	PutTagPair(output, CODEC_TAG_MARGIN_TOP, top_margin);
	PutTagPair(output, CODEC_TAG_MARGIN_RIGHT, right_margin);
	PutTagPair(output, CODEC_TAG_MARGIN_BOTTOM, bottom_margin);

	// Transmit the quantization parameters
	PutTagPair(output, CODEC_TAG_PIXEL_OFFSET, pixel_offset);
	PutTagPair(output, CODEC_TAG_QUANTIZATION, quantization);

	// Inform the decoder of the number of bits per pixel
	PutTagPair(output, CODEC_TAG_PIXEL_DEPTH, bits_per_pixel);

	//	Put Subbandsize field here.
	SizeTagPush(output, CODEC_TAG_SUBBAND_SIZE);
#else

#ifdef CODEC_LOWPASS_START_CODE
	// Output the start of lowpass image code
	PutBits(output, CODEC_LOWPASS_START_CODE, CODEC_LOWPASS_START_SIZE);
#endif

	// Inform the decoder of the subband number for the lowpass image
	// (This should be the largest subband number)
	PutBits(output, subband, CODEC_SUBBAND_SIZE);

	// Inform the decoder of the number of levels at the top of the pyramid
	// (The number of levels transmitted may be less)
	PutBits(output, level, CODEC_NUMLEVELS_SIZE);

	// Inform the decoder of the lowpass image dimensions
	PutBits(output, width, CODEC_DIMENSION_SIZE);
	PutBits(output, height, CODEC_DIMENSION_SIZE);

	// The transmitted dimensions may be less than the image dimensions
	PutBits(output, left_margin, CODEC_BORDER_SIZE);
	PutBits(output, top_margin, CODEC_BORDER_SIZE);
	PutBits(output, right_margin, CODEC_BORDER_SIZE);
	PutBits(output, bottom_margin, CODEC_BORDER_SIZE);

	// Transmit the quantization parameters
	PutBits(output, pixel_offset, CODEC_PIXEL_SIZE);
	PutBits(output, quantization, CODEC_QUANT_SIZE);

	// Inform the decoder of the number of bits per pixel
	PutBits(output, bits_per_pixel, CODEC_NUMBITS_SIZE);
#endif
}

void PutVideoLowPassTrailer(BITSTREAM *output)
{
#if _CODEC_TAGS

	// Check that the bitstream is tag aligned before writing the pixels
	assert(IsAlignedBits(output));

#if (_CODEC_MARKERS && CODEC_LOWPASS_END_CODE)
	// Output a debugging marker
	assert(CODEC_LOWPASS_END_SIZE == 16);
	PutTagMarker(output, CODEC_LOWPASS_END_CODE, CODEC_LOWPASS_END_SIZE);
#endif

	//	Set the previous subbandsize field here.
	SizeTagPop(output);

#else

#ifdef CODEC_LOWPASS_END_CODE
	// Output the code that marks the end of the lowpass image
	PutBits(output, CODEC_LOWPASS_END_CODE, CODEC_LOWPASS_END_SIZE);
#endif

#endif

}

#if (_CODEC_TAGS && _CODEC_MARKERS)

// Output a tag and marker before the lowpass coefficients for debugging
void PutVideoLowPassMarker(BITSTREAM *output)
{
	assert(CODEC_COEFFICIENT_START_SIZE == 16);
	PutTagMarker(output, CODEC_COEFFICIENT_START_CODE, CODEC_COEFFICIENT_START_SIZE);
}

#endif

void PutVideoHighPassHeader(BITSTREAM *output,
							int nType,
							int nWaveletNumber,
							int nWaveletLevel,
							int nBandWidth,
							int nBandHeight,
							int nBandCount,
							//int lowpass_border,
							//int highpass_border,
							int lowpass_scale,
							int lowpass_divisor)
{
	int lowpass_border = 0;
	int highpass_border = 0;

#if _CODEC_TAGS

#if (_CODEC_MARKERS && CODEC_HIGHPASS_START_CODE)
	// Output a marker for the start of the highpass coefficients
	assert(CODEC_HIGHPASS_START_SIZE == 16);
	PutTagMarker(output, CODEC_HIGHPASS_START_CODE, CODEC_HIGHPASS_START_SIZE);
#endif

	// Output the type of wavelet transform
	PutTagPair(output, CODEC_TAG_WAVELET_TYPE, nType);

	// Output the number of the wavelet in the transform
	PutTagPair(output, CODEC_TAG_WAVELET_NUMBER, nWaveletNumber);

	// Encode the level of the wavelet in the transform pyramid
	PutTagPair(output, CODEC_TAG_WAVELET_LEVEL, nWaveletLevel);

	// Inform the decoder of the number of bands
	PutTagPair(output, CODEC_TAG_NUM_BANDS, nBandCount);

	// Inform the decoder of the dimensions of each band of highpass coefficients
	PutTagPair(output, CODEC_TAG_HIGHPASS_WIDTH, nBandWidth);
	PutTagPair(output, CODEC_TAG_HIGHPASS_HEIGHT, nBandHeight);

	// Provide the parameters for the left and bottom borders in each band
	PutTagPair(output, CODEC_TAG_LOWPASS_BORDER, lowpass_border);
	PutTagPair(output, CODEC_TAG_HIGHPASS_BORDER, highpass_border);

	// Provide the scale factors used for the lowpass band
	PutTagPair(output, CODEC_TAG_LOWPASS_SCALE, lowpass_scale);
	PutTagPair(output, CODEC_TAG_LOWPASS_DIVISOR, lowpass_divisor);


	//	Put level size field here.
	SizeTagPush(output, CODEC_TAG_LEVEL_SIZE);

#else

#ifdef CODEC_HIGHPASS_START_CODE
	// Output the code for the start of the highpass coefficients
	PutBits(output, CODEC_HIGHPASS_START_CODE, CODEC_HIGHPASS_START_SIZE);
#endif

	// Align output to next byte boundary
	//PadBits(output);

	// Inform the decoder of the type of wavelet transform
	PutBits(output, nType, CODEC_WAVELET_SIZE);

	// Encode the number of the wavelet in the transform
	PutBits(output, nWaveletNumber, CODEC_HIGHINDEX_SIZE);

	// Encode the level of the wavelet in the transform pyramid
	PutBits(output, nWaveletLevel, CODEC_NUMLEVELS_SIZE);

	// Inform the decoder of the number of bands
	PutBits(output, nBandCount, CODEC_NUMBANDS_SIZE);

	// Inform the decoder of the dimensions of each band of highpass coefficients
	PutBits(output, nBandWidth, CODEC_DIMENSION_SIZE);
	PutBits(output, nBandHeight, CODEC_DIMENSION_SIZE);

	// Provide the parameters for the left and bottom borders in each band
	PutBits(output, lowpass_border, CODEC_BORDER_SIZE);
	PutBits(output, highpass_border, CODEC_BORDER_SIZE);

	// Provide the scale factors used for the lowpass band
	PutBits(output, lowpass_scale, CODEC_SCALE_SIZE);
	PutBits(output, lowpass_divisor, CODEC_DIVISOR_SIZE);

#endif
}

void PutVideoHighPassTrailer(BITSTREAM *output,
							 uint32_t cntPositive, uint32_t cntNegative,
							 uint32_t cntZeroValues, uint32_t cntZeroTrees,
							 uint32_t cntZeroNodes)
{
#if _CODEC_TAGS

#if (_CODEC_MARKERS && CODEC_HIGHPASS_END_CODE)
	assert(CODEC_HIGHPASS_END_SIZE == 16);
	PutTagMarker(output, CODEC_HIGHPASS_END_CODE, CODEC_HIGHPASS_END_SIZE);
#endif

	//	Set the previous subbandsize field here.
	SizeTagPop(output);

#else

#ifdef CODEC_HIGHPASS_END_CODE
	PutBits(output, CODEC_HIGHPASS_END_CODE, CODEC_HIGHPASS_END_SIZE);

	// Output the coefficient statistics
	PutBits(output, cntPositive, CODEC_COUNTER_SIZE);
	PutBits(output, cntNegative, CODEC_COUNTER_SIZE);
	PutBits(output, cntZeroValues, CODEC_COUNTER_SIZE);
	PutBits(output, cntZeroTrees, CODEC_COUNTER_SIZE);
	PutBits(output, cntZeroNodes, CODEC_COUNTER_SIZE);
#endif

#endif
}

void PutVideoBandHeader(BITSTREAM *output, int band, int width, int height,
						int subband, int encoding, int quantization,
						int scale, int divisor, uint32_t *counters, int codingflags, int do_peaks)
{
	//int i;

#if _CODEC_TAGS

#if (_CODEC_MARKERS && CODEC_BAND_START_CODE)
	assert(CODEC_BAND_START_SIZE == 16);
	PutTagMarker(output, CODEC_BAND_START_CODE, CODEC_BAND_START_SIZE);
#endif

	// Output the band parameters
	PutTagPair(output, CODEC_TAG_BAND_NUMBER, band);
	if(codingflags)
	{
		PutTagPair(output, CODEC_TAG_BAND_CODING_FLAGS, codingflags);
	}
	PutTagPair(output, CODEC_TAG_BAND_WIDTH, width);
	PutTagPair(output, CODEC_TAG_BAND_HEIGHT, height);
	PutTagPair(output, CODEC_TAG_BAND_SUBBAND, subband);
	PutTagPair(output, CODEC_TAG_BAND_ENCODING, encoding);
	PutTagPair(output, CODEC_TAG_BAND_QUANTIZATION, quantization);
	PutTagPair(output, CODEC_TAG_BAND_SCALE, scale);

	if(do_peaks)
	{
		PutTagPair(output, OPTIONALTAG(CODEC_TAG_PEAK_TABLE_OFFSET_L), 0);	// later filled in if needed
		PutTagPair(output, OPTIONALTAG(CODEC_TAG_PEAK_TABLE_OFFSET_H), 0);	// later filled in if needed
		PutTagPair(output, OPTIONALTAG(CODEC_TAG_PEAK_LEVEL), 0);			// later filled in if needed
	}
	//	Put Subbandsize field here.
	SizeTagPush(output, CODEC_TAG_SUBBAND_SIZE);

	PutTagPair(output, CODEC_TAG_BAND_HEADER, 0);	// was PutTagPair(output, CODEC_TAG_BAND_DIVISOR, divisor);

	// Must encode the counters if the encoding method is zerotree
	assert(encoding != BAND_ENCODING_ZEROTREE || counters != NULL);

#else

#ifdef CODEC_BAND_START_CODE
	PutBits(output, CODEC_BAND_START_CODE, CODEC_BAND_START_SIZE);
#endif

	// Output the band parameters
	PutBits(output, band, CODEC_BAND_SIZE);
	PutBits(output, width, CODEC_DIMENSION_SIZE);
	PutBits(output, height, CODEC_DIMENSION_SIZE);
	PutBits(output, subband, CODEC_SUBBAND_SIZE);
	PutBits(output, encoding, CODEC_ENCODING_SIZE);
	PutBits(output, quantization, CODEC_QUANT_SIZE);
	PutBits(output, scale, CODEC_SCALE_SIZE);
	PutBits(output, divisor, CODEC_DIVISOR_SIZE);

	// Must encode the counters if the encoding method is zerotree
	assert(encoding != BAND_ENCODING_ZEROTREE || counters != NULL);

	// Output the counts of zerotree significance codes if available
	if (counters != NULL)
	{
		int i;

		// Output all significance code counters for debugging even
		// though some significance codes are not in the bitstream
		for (i = 0; i < SIGCODE_NUM_CODES; i++)
			PutBits(output, counters[i], CODEC_COUNTER_SIZE);
	}

#endif
}

void PutVideoCoefficientHeader(BITSTREAM *output, int band, int coefficient_count,
							   int bits_per_coefficient, int quantization)
{
#ifdef CODEC_COEFFICIENT_START_CODE

	// Must have some bits per coefficient unless there are no coefficients
	assert(bits_per_coefficient > 0 || coefficient_count == 0);

	PutBits(output, CODEC_COEFFICIENT_START_CODE, CODEC_COEFFICIENT_START_SIZE);

	// Align output to next byte boundary
	//PadBits(output);

	// Output the band number (redundant with data provided in header)
	PutBits(output, band, CODEC_BAND_SIZE);

	// Output the number of coefficients
	PutBits(output, coefficient_count, CODEC_COUNTER_SIZE);

	// Output the number of bits per transmitted coefficient
	PutBits(output, bits_per_coefficient, CODEC_NUMBITS_SIZE);

	// Output the quantization divisor
	PutBits(output, quantization, CODEC_QUANT_SIZE);

#endif
}

// Append the band end codeword to the encoded coefficients
void FinishEncodeBand(BITSTREAM *output, unsigned int code, int size)
{
#ifdef CODEC_BAND_END_CODE
	// Output the codeword that marks the end of the band coefficients
//	PutBits(output, CODEC_BAND_END_CODE, CODEC_BAND_END_SIZE);
	PutBits(output, code, size);
#endif
}

void PutVideoBandTrailer(BITSTREAM *output)
{
#if _CODEC_TAGS
	// Pad the bitstream to a tag boundary
	PadBitsTag(output);

	// Output the tag value pairs for the band trailer
	PutTagPair(output, CODEC_TAG_BAND_TRAILER, 0);

	//	Set the previous subbandsize field here.
	SizeTagPop(output);

#else
	// The finite state machine decoder reads one byte at a time from the bitstream
	PadBits(stream);
#endif
}

// This add the tag in the middle of the two pass lossless encoding, where upper and
// lower bytes are encoded separately.
void PutVideoBandMidPoint2Pass(BITSTREAM *output)
{
#if _CODEC_TAGS
	// Pad the bitstream to a tag boundary
	PadBitsTag(output);

	// Output the tag value pairs for the band trailer
	PutTagPair(output, CODEC_TAG_BAND_SECONDPASS, 0);

#else
	// The finite state machine decoder reads one byte at a time from the bitstream
	PadBits(stream);
#endif
}

#if RUNS_ROWEND_MARKER

int32_t PutRowTrailer(BITSTREAM *output)
{
	uint32_t bitcount = 0;

#ifdef CODEC_ROWEND_CODE
	PutBits(output, CODEC_ROWEND_CODE, CODEC_ROWEND_SIZE);
	bitcount = CODEC_ROWEND_SIZE;
#endif

	return bitcount;
}

#endif


/***** Decoding Routines *****/

// Codebook for the sample markers that must be recognized in the bitstream
// The codebook entries must correspond to the sample type codes
#if 0
static VLCTABLE sample_markers = {
	9,
	{
        {CODEC_SAMPLE_STOP_SIZE, CODEC_SAMPLE_STOP_CODE},
        {CODEC_FRAME_START_SIZE, CODEC_FRAME_START_CODE},
        {CODEC_GROUP_START_SIZE, CODEC_GROUP_START_CODE},
        {CODEC_CHANNEL_START_SIZE, CODEC_CHANNEL_START_CODE},
        {CODEC_FRAME_START_SIZE, CODEC_FRAME_START_CODE},
        {CODEC_FRAME_START_SIZE, CODEC_FRAME_START_CODE},
        {CODEC_GROUP_END_SIZE, CODEC_GROUP_END_CODE},
        {CODEC_SEQUENCE_START_SIZE, CODEC_SEQUENCE_START_CODE},
        {CODEC_SEQUENCE_END_SIZE, CODEC_SEQUENCE_END_CODE}
	}
};
#endif

//static const VLCBOOK * const sample_codebook = (VLCBOOK *)&sample_markers;


// Parse the bitstream to find the next media sample
int FindNextSample(BITSTREAM *stream)
{
#if _CODEC_TAGS

	TAGVALUE segment = GetTagValue(stream);

	// Return null sample if an error occurred while reading the bitstream
	if (stream->error != BITSTREAM_ERROR_OKAY)
		return SAMPLE_TYPE_NONE;

	// Return null sample if that tag does not specify the sample type
	assert(segment.tuple.tag == CODEC_TAG_SAMPLE);
	if (segment.tuple.tag != CODEC_TAG_SAMPLE)
		return SAMPLE_TYPE_NONE;

	// Return the type of sample
	return segment.tuple.value;

#else

	int sample_type;

	// The marker for the sample type should always be aligned to a word boundary
	AlignBits(stream);

	// Use the codebook to decode the next sample type
	sample_type = GetVlc(stream, sample_codebook);

	// Return null sample if an error occurred while reading the bitstream
	if (stream->error != BITSTREAM_ERROR_OKAY)
		sample_type = SAMPLE_TYPE_NONE;

	// Return the type of media sample found in the bitstream
	return sample_type;

#endif
}

#if _CODEC_TAGS

CODEC_ERROR DecodeFrameHeader(BITSTREAM *stream, FRAME_HEADER *header, int sample_type)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
	//int marker;
	int type;
	int width;
	int height;
	//int display_height = 0;
	int group_index;

	TAGVALUE segment;

	// Has the caller already found the bitstream marker?
	switch (sample_type)
	{
	case SAMPLE_TYPE_NONE:		// Caller has not read the bitstream marker
		segment = GetTagValue(stream);
		assert(segment.tuple.tag == CODEC_TAG_SAMPLE);
		assert(segment.tuple.value == SAMPLE_TYPE_FRAME);
		if (!IsTagValue(segment, CODEC_TAG_SAMPLE, SAMPLE_TYPE_FRAME)) {
			error = CODEC_ERROR_FRAME_START_MARKER;
			return error;
		}

		// Fall through and process the frame

	case SAMPLE_TYPE_IFRAME:	// Single frame (not yet supported)
	default:
		assert(0);
		error = CODEC_ERROR_FRAME_TYPE;
		return error;

	case SAMPLE_TYPE_PFRAME:	// Second or later frame in a group
	case SAMPLE_TYPE_FRAME:		// Assume the frame is part of a group

		// Extract the type of frame
		type = GetValue(stream, CODEC_TAG_FRAME_TYPE);
		header->type = type;
		assert(type == FRAME_TYPE_PFRAME);

		// Extract the dimensions of the full resolution image
		width = GetValue(stream, CODEC_TAG_FRAME_WIDTH);
		height = GetValue(stream, CODEC_TAG_FRAME_HEIGHT);
		group_index = GetValue(stream, CODEC_TAG_FRAME_INDEX);

		header->width = width;
		header->height = height;
		header->group_index = group_index;

		// Get the index of the frame within the group
		break;
	}

	return error;
}

#else

CODEC_ERROR DecodeFrameHeader(BITSTREAM *stream, FRAME_HEADER *header, int sample_type)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
	int marker;
	int type;
	int width;
	int height;
	int group_index;

	// Has the caller already found the bitstream marker?
	switch (sample_type)
	{
	case SAMPLE_TYPE_NONE:		// Caller has not read the bitstream marker

#ifdef CODEC_FRAME_START_CODE
		marker = GetBits(stream, CODEC_FRAME_START_SIZE);
		header->marker = marker;
		if (marker != CODEC_FRAME_START_CODE) {
			error = CODEC_ERROR_FRAME_START_MARKER;
			return error;
		}
#endif
		// Fall through and process the frame

	case SAMPLE_TYPE_IFRAME:	// Single frame (not yet supported)
	default:
		assert(0);
		error = CODEC_ERROR_FRAME_TYPE;
		return error;

	case SAMPLE_TYPE_PFRAME:	// Second or later frame in a group
	case SAMPLE_TYPE_FRAME:		// Assume the frame is part of a group

		// Extract the type of frame
		type = GetBits(stream, CODEC_FRAME_TYPE_SIZE);
		header->type = type;
		assert(type == FRAME_TYPE_PFRAME);

		// Extract the dimensions of the full resolution image
		width = GetBits(stream, CODEC_DIMENSION_SIZE);
		height = GetBits(stream, CODEC_DIMENSION_SIZE);
		header->width = width;
		header->height = height;

		// Get the index of the frame within the group
		group_index = GetBits(stream, CODEC_GROUP_INDEX_SIZE);
		header->group_index = group_index;
		break;
	}

	return error;
}

#endif

CODEC_ERROR DecodeLowPassHeader(BITSTREAM *stream, LOWPASS_HEADER *header)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if _CODEC_TAGS

	TAGVALUE segment;

#if (_CODEC_MARKERS && CODEC_LOWPASS_START_CODE)
	// Read the debugging marker
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_MARKER);
	if (!IsValidSegment(stream, segment, CODEC_TAG_MARKER)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	assert(segment.tuple.value == CODEC_LOWPASS_START_CODE);
	if (segment.tuple.value != CODEC_LOWPASS_START_CODE) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
#endif

	// Get the lowpass subband number
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_LOWPASS_SUBBAND);
	if (!IsValidSegment(stream, segment, CODEC_TAG_LOWPASS_SUBBAND)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->subband = segment.tuple.value;

	// Get the number of levels in the transform pyramid
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_NUM_LEVELS);
	if (!IsValidSegment(stream, segment, CODEC_TAG_NUM_LEVELS)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->level = segment.tuple.value;

	// Get the lowpass image width
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_LOWPASS_WIDTH);
	if (!IsValidSegment(stream, segment, CODEC_TAG_LOWPASS_WIDTH)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->width = segment.tuple.value;

	// Get the lowpass image height
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_LOWPASS_HEIGHT);
	if (!IsValidSegment(stream, segment, CODEC_TAG_LOWPASS_HEIGHT)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->height = segment.tuple.value;

	// Get the adjustments to the transmitted size
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_MARGIN_LEFT);
	if (!IsValidSegment(stream, segment, CODEC_TAG_MARGIN_LEFT)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->offset_width = segment.tuple.value;

	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_MARGIN_TOP);
	if (!IsValidSegment(stream, segment, CODEC_TAG_MARGIN_TOP)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->offset_height = segment.tuple.value;

	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_MARGIN_RIGHT);
	if (!IsValidSegment(stream, segment, CODEC_TAG_MARGIN_RIGHT)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->border_width = segment.tuple.value;

	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_MARGIN_BOTTOM);
	if (!IsValidSegment(stream, segment, CODEC_TAG_MARGIN_BOTTOM)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->border_height = segment.tuple.value;

	// Get the lowpass image quantization parameters
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_PIXEL_OFFSET);
	if (!IsValidSegment(stream, segment, CODEC_TAG_PIXEL_OFFSET)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->quantization.offset = segment.tuple.value;

	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_QUANTIZATION);
	if (!IsValidSegment(stream, segment, CODEC_TAG_QUANTIZATION)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->quantization.divisor = segment.tuple.value;

	// Get the number of bits per pixel in the encoded image
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_PIXEL_DEPTH);
	if (!IsValidSegment(stream, segment, CODEC_TAG_PIXEL_DEPTH)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->bpp = segment.tuple.value;

#else

	int subband;
	int level;
	int width;
	int height;
	int offset_width;
	int offset_height;
	int border_width;
	int border_height;
	int pixel_offset;
	int pixel_factor;
	int bits_per_pixel;

#ifdef CODEC_LOWPASS_START_CODE
	int marker;
	marker = GetBits(stream, CODEC_LOWPASS_START_SIZE);
	header->marker = marker;
	if (marker != CODEC_LOWPASS_START_CODE) {
		error = CODEC_ERROR_LOWPASS_START_MARKER;
		return error;
	}
#endif

	// Get the index in the sequence of subbands
	subband = GetBits(stream, CODEC_SUBBAND_SIZE);
	header->subband = subband;

	// Get the number of this level in the image pyramid
	level = GetBits(stream, CODEC_NUMLEVELS_SIZE);
	header->level = level;

	// Get the lowpass image dimensions
	width = GetBits(stream, CODEC_DIMENSION_SIZE);
	height = GetBits(stream, CODEC_DIMENSION_SIZE);
	header->width = width;
	header->height = height;

	// Get the adjustments to the transmitted size
	offset_width = GetBits(stream, CODEC_BORDER_SIZE);
	offset_height = GetBits(stream, CODEC_BORDER_SIZE);
	header->offset_width = offset_width;
	header->offset_height = offset_height;

	border_width = GetBits(stream, CODEC_BORDER_SIZE);
	border_height = GetBits(stream, CODEC_BORDER_SIZE);
	header->border_width = border_width;
	header->border_height = border_height;

	// Get the lowpass image quantization parameters
	pixel_offset = GetBits(stream, CODEC_PIXEL_SIZE);
	pixel_factor = GetBits(stream, CODEC_QUANT_SIZE);
	header->quantization.offset = pixel_offset;
	header->quantization.divisor = pixel_factor;

	// Get the number of bits per pixel in the encoded image
	bits_per_pixel = GetBits(stream, CODEC_NUMBITS_SIZE);
	header->bpp = bits_per_pixel;

#endif

	return error;
}



CODEC_ERROR DecodeLowPassTrailer(BITSTREAM *stream, LOWPASS_TRAILER *trailer)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if _CODEC_TAGS

	TAGVALUE segment;

	// Align the bitstream to a tag boundary
	AlignBitsTag(stream);

	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

#if (_CODEC_MARKERS && CODEC_LOWPASS_END_CODE)
	// Read the debugging marker
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_MARKER);
	assert(segment.tuple.value == CODEC_LOWPASS_END_CODE);
#endif

#else

#ifdef CODEC_LOWPASS_END_CODE
	int marker;
	marker = GetBits(stream, CODEC_LOWPASS_END_SIZE);
	trailer->marker = marker;
	if (marker != CODEC_LOWPASS_END_CODE) {
		error = CODEC_ERROR_LOWPASS_END_MARKER;
		return error;
	}
#endif

#endif

	return error;
}

CODEC_ERROR DecodeHighPassHeader(BITSTREAM *stream, HIGHPASS_HEADER *header, int target_index)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
	int highpass_type;
	int highpass_index;
	int highpass_level;
	int highpass_num_bands;
	int highpass_width;
	int highpass_height;
	int lowpass_border;
	int highpass_border;
	int lowpass_scale;
	int lowpass_divisor;

#if _CODEC_TAGS

	TAGVALUE segment;

#if (_CODEC_MARKERS && CODEC_HIGHPASS_START_CODE)
	// Read the debugging marker
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_MARKER);
	assert(segment.tuple.value == CODEC_HIGHPASS_START_CODE);
#endif

	// Get the type of wavelet transform
	highpass_type = GetValue(stream, CODEC_TAG_WAVELET_TYPE);
	header->type = highpass_type;

	// Get the wavelet index of this group of highpass coefficients
	highpass_index = GetValue(stream, CODEC_TAG_WAVELET_NUMBER);
	header->number = highpass_index;
	if (highpass_index != target_index) {
		error = CODEC_ERROR_HIGHPASS_INDEX;
		return error;
	}

	// Get the level of this group of highpass coefficients
	highpass_level = GetValue(stream, CODEC_TAG_WAVELET_LEVEL);
	header->level = highpass_level;

	// Get the number of high pass bands
	highpass_num_bands = GetValue(stream, CODEC_TAG_NUM_BANDS);
	header->num_bands = highpass_num_bands;

	// Get the dimensions of each band of highpass coefficients
	highpass_width = GetValue(stream, CODEC_TAG_HIGHPASS_WIDTH);
	highpass_height = GetValue(stream, CODEC_TAG_HIGHPASS_HEIGHT);
	header->width = highpass_width;
	header->height = highpass_height;

	// Get the parameters of the borders around the lowpass and highpass bands
	lowpass_border = GetValue(stream, CODEC_TAG_LOWPASS_BORDER);
	highpass_border = GetValue(stream, CODEC_TAG_HIGHPASS_BORDER);
	header->lowpass_border = lowpass_border;
	header->highpass_border = highpass_border;

	// Get the scaling parameters for the lowpass image
	lowpass_scale = GetValue(stream, CODEC_TAG_LOWPASS_SCALE);
	lowpass_divisor = GetValue(stream, CODEC_TAG_LOWPASS_DIVISOR);
	header->lowpass_scale = lowpass_scale;
	header->lowpass_divisor = lowpass_divisor;

#else

#ifdef CODEC_HIGHPASS_START_CODE
	int marker;
	marker = GetBits(stream, CODEC_HIGHPASS_START_SIZE);
	header->marker = marker;
	if (marker != CODEC_HIGHPASS_START_CODE) {
		error = CODEC_ERROR_HIGHPASS_START_MARKER;
		return error;
	}
#endif

	// Get the type of wavelet transform
	highpass_type = GetBits(stream, CODEC_WAVELET_SIZE);
	header->type = highpass_type;

	// Get the wavelet index of this group of highpass coefficients
	highpass_index = GetBits(stream, CODEC_HIGHINDEX_SIZE);
	header->number = highpass_index;
	if (highpass_index != target_index) {
		error = CODEC_ERROR_HIGHPASS_INDEX;
		return error;
	}

	// Get the level of this group of highpass coefficients
	highpass_level = GetBits(stream, CODEC_NUMLEVELS_SIZE);
	header->level = highpass_level;

	// Get the number of high pass bands
	highpass_num_bands = GetBits(stream, CODEC_NUMBANDS_SIZE);
	header->num_bands = highpass_num_bands;

	// Get the dimensions of each band of highpass coefficients
	highpass_width = GetBits(stream, CODEC_DIMENSION_SIZE);
	highpass_height = GetBits(stream, CODEC_DIMENSION_SIZE);
	header->width = highpass_width;
	header->height = highpass_height;

	// Get the parameters of the borders around the lowpass and highpass bands
	lowpass_border = GetBits(stream, CODEC_BORDER_SIZE);
	highpass_border = GetBits(stream, CODEC_BORDER_SIZE);
	header->lowpass_border = lowpass_border;
	header->highpass_border = highpass_border;

	// Get the scaling parameters for the lowpass image
	lowpass_scale = GetBits(stream, CODEC_SCALE_SIZE);
	lowpass_divisor = GetBits(stream, CODEC_DIVISOR_SIZE);
	header->lowpass_scale = lowpass_scale;
	header->lowpass_divisor = lowpass_divisor;

#endif

	return error;
}

#if 0
CODEC_ERROR DecodeBandHeader(BITSTREAM *stream, BAND_HEADER *header, int band, SCODE_COUNTERS *scode)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
	int highpass_band;
	int band_index = band - 1;		// Band number zero is reserved for the lowpass band
	int band_width;
	int band_height;
	int subband;
	int encoding;
	int quantization;
	int scale;
	int divisor;
	uint32_t counter;
	int scode_count = 0;

#if _CODEC_TAGS

	TAGVALUE segment;

#if (_CODEC_MARKERS && CODEC_BAND_START_CODE)
	// Read the debugging marker
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_MARKER);
	assert(segment.tuple.value == CODEC_BAND_START_CODE);
#endif

	// Get the highpass band number within the wavelet
	highpass_band = GetValue(stream, CODEC_TAG_BAND_NUMBER);
	header->band = highpass_band + 1;

	// Get the band dimensions
	band_width = GetValue(stream, CODEC_TAG_BAND_WIDTH);
	band_height = GetValue(stream, CODEC_TAG_BAND_HEIGHT);
	header->width = band_width;
	header->height = band_height;

	// Get the subband number
	subband = GetValue(stream, CODEC_TAG_BAND_SUBBAND);
	header->subband = subband;

	// Get the encoding method
	encoding = GetValue(stream, CODEC_TAG_BAND_ENCODING);
	header->encoding = encoding;

	// Get the quantization
	quantization = GetValue(stream, CODEC_TAG_BAND_QUANTIZATION);
	header->quantization = quantization;

	// Get the scale factors
	scale = GetValue(stream, CODEC_TAG_BAND_SCALE);
//	divisor = GetValue(stream, CODEC_TAG_BAND_DIVISOR);
	divisor = GetValue(stream, CODEC_TAG_BAND_HEADER);
	header->scale = scale;
	header->divisor = 0;	//divisor;
	header->count = 0;

#else

#ifdef CODEC_BAND_START_CODE
	int marker;

	// Read the band header
	marker = GetBits(stream, CODEC_BAND_START_SIZE);
	header->marker = marker;
	if (marker != CODEC_BAND_START_CODE) {
		error = CODEC_ERROR_BAND_START_MARKER;
		return error;
	}
#endif

	// Get the highpass band number within the wavelet
	highpass_band = GetBits(stream, CODEC_BAND_SIZE);

	header->band = highpass_band + 1;

	// Get the band dimensions
	band_width = GetBits(stream, CODEC_DIMENSION_SIZE);
	band_height = GetBits(stream, CODEC_DIMENSION_SIZE);
	header->width = band_width;
	header->height = band_height;

	// Get the subband number
	subband = GetBits(stream, CODEC_SUBBAND_SIZE);
	header->subband = subband;

	// Get the encoding method
	encoding = GetBits(stream, CODEC_ENCODING_SIZE);
	header->encoding = encoding;

	// Get the quantization
	quantization = GetBits(stream, CODEC_QUANT_SIZE);
	header->quantization = quantization;

	// Get the scale factors
	scale = GetBits(stream, CODEC_SCALE_SIZE);
	divisor = GetBits(stream, CODEC_DIVISOR_SIZE);
	header->scale = scale;
	header->divisor = 0;	//divisor;

	if (encoding == BAND_ENCODING_ZEROTREE)
	{
		// Get the number of significance codes in this band
		counter = GetBits(stream, CODEC_COUNTER_SIZE);
		if (scode != NULL) scode->zerovalues = counter;
		scode_count += counter;

		counter = GetBits(stream, CODEC_COUNTER_SIZE);
		if (scode != NULL) scode->zerotrees = counter;
		scode_count += counter;

		counter = GetBits(stream, CODEC_COUNTER_SIZE);
		if (scode != NULL) scode->positives = counter;
		scode_count += counter;

		counter = GetBits(stream, CODEC_COUNTER_SIZE);
		if (scode != NULL) scode->negatives = counter;
		scode_count += counter;

		// Should not encounter any zerotree nodes except the root
		counter = GetBits(stream, CODEC_COUNTER_SIZE);
		if (scode != NULL) scode->zeronodes = counter;

		header->count = scode_count;
	}
	else
	{
		header->count = 0;
	}

#endif

	return error;
}
#endif

CODEC_ERROR DecodeBandTrailer(BITSTREAM *stream, BAND_TRAILER *trailer)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if _CODEC_TAGS

	TAGVALUE segment;

#if (0 && DEBUG)
	DebugOutputBitstreamPosition(stream);
#endif

	// Advance the bitstream to a tag boundary
	AlignBitsTag(stream);

#if (_CODEC_MARKERS && CODEC_BAND_END_CODE)
	// Read the debugging marker
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_BAND_TRAILER);
	assert(segment.tuple.value == 0);
	if (!IsTagValue(segment, CODEC_TAG_BAND_TRAILER, 0)) {
		error = CODEC_ERROR_BAND_END_MARKER;
		return error;
	}
#endif

#else

#if CODEC_BAND_END_CODE
	int marker;
	marker = GetBits(stream, CODEC_BAND_END_SIZE);
	trailer->marker = marker;
	if (marker != CODEC_BAND_END_CODE) {
		error = CODEC_ERROR_BAND_END_MARKER;
		return error;
	}
#endif

#endif

	return error;
}


#if !_NEW_DECODER

CODEC_ERROR DecodeHighPassTrailer(BITSTREAM *stream, HIGHPASS_TRAILER *trailer)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if _CODEC_TAGS

	TAGVALUE segment;

#if (_CODEC_MARKERS && CODEC_HIGHPASS_END_CODE)
	segment = GetTagValue(stream);
	if (!IsTagValue(segment, CODEC_TAG_MARKER, CODEC_HIGHPASS_END_CODE)) {
		error = CODEC_ERROR_HIGHPASS_END_MARKER;
		return error;
	}

#endif

#else

#ifdef CODEC_HIGHPASS_END_CODE
	int marker;
	uint32_t counter;

	marker = GetBits(stream, CODEC_HIGHPASS_END_SIZE);
	trailer->marker = marker;
	if (marker != CODEC_HIGHPASS_END_CODE) {
		error = CODEC_ERROR_HIGHPASS_END_MARKER;
		return error;
	}

	// Read the coefficient counters from the bitstream
	counter = GetBits(stream, CODEC_COUNTER_SIZE);
	trailer->positive = counter;

	counter = GetBits(stream, CODEC_COUNTER_SIZE);
	trailer->negative = counter;

	counter = GetBits(stream, CODEC_COUNTER_SIZE);
	trailer->zerovalues = counter;

	counter = GetBits(stream, CODEC_COUNTER_SIZE);
	trailer->zerotrees = counter;

	counter = GetBits(stream, CODEC_COUNTER_SIZE);
	trailer->zeronodes = counter;
#endif

#endif

	return error;
}

CODEC_ERROR DecodeFrameTrailer(BITSTREAM *stream, FRAME_TRAILER *trailer)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#ifdef CODEC_FRAME_END_CODE
	int marker;
	uint32_t counter;

	marker = GetBits(stream, CODEC_FRAME_END_SIZE);
	trailer->marker = marker;
	if (marker != CODEC_FRAME_END_CODE) {
		error = CODEC_ERROR_FRAME_END_MARKER;
		return error;
	}

	counter = GetBits(stream, CODEC_COUNTER_SIZE);
	trailer->bitcount = counter;
#endif

	return error;
}

CODEC_ERROR DecodeCoeffs(BITSTREAM *stream, IMAGE *wavelet, int band_index,
						 int band_width, int band_height, int coefficient_count,
						 int bits_per_coefficient, int quantization)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

	return error;
}

CODEC_ERROR DecodeRuns(BITSTREAM *stream, IMAGE *wavelet, int band_index,
					   int band_width, int band_height, int coefficient_count,
					   int bits_per_coefficient, int quantization)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

	return error;
}

CODEC_ERROR DecodeSequenceHeader(BITSTREAM *stream, SEQUENCE_HEADER *header, int sample_type)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
	int reserved;

#if _CODEC_TAGS

	TAGVALUE segment;

	// Get the version information and options flags
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_VERSION_MAJOR);
	header->version.major = segment.tuple.value;

	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_VERSION_MINOR);
	header->version.minor = segment.tuple.value;

	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_VERSION_REVISION);
	header->version.revision = segment.tuple.value;

	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_VERSION_EDIT);
	assert(segment.tuple.value == 0);

	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_SEQUENCE_FLAGS);
	header->flags = segment.tuple.value;

	// Get the video frame dimensions and format
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_FRAME_WIDTH);
	header->frame.width = segment.tuple.value;

	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_FRAME_HEIGHT);
	header->frame.height = segment.tuple.value;

	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_FRAME_FORMAT);
	header->frame.format = segment.tuple.value;

	// The bitstream does not contain the maximum group length
	header->gop_length = WAVELET_MAX_FRAMES;

#else

#ifdef CODEC_SEQUENCE_START_CODE

	// Has the caller already found the bitstream marker?
	if (sample_type == SAMPLE_TYPE_NONE)
	{
		int marker;

		// Skip to the next bitword boundary
		AlignBits(stream);

		marker = GetBits(stream, CODEC_SEQUENCE_START_SIZE);
		header->marker = marker;
		if (marker != CODEC_SEQUENCE_START_CODE) {
			error = CODEC_ERROR_SEQUENCE_START_MARKER;
			return error;
		}
	}
	else if (sample_type == SAMPLE_TYPE_SEQUENCE_HEADER)
	{
		// Fill in the header with the marker found by the caller
		header->marker = CODEC_SEQUENCE_START_CODE;
	}
	else
	{
		// This routine was called by mistake
		error = CODEC_ERROR_SEQUENCE_START_MARKER;
		return error;
	}

	// Get the version information and options flags
	header->version.major = GetBits(stream, CODEC_VERSION_SIZE);
	header->version.minor = GetBits(stream, CODEC_VERSION_SIZE);
	header->version.revision = GetBits(stream, CODEC_VERSION_SIZE);
	reserved = GetBits(stream, CODEC_VERSION_SIZE);
	assert(reserved == 0);
	header->flags = GetBits(stream, CODEC_FLAGS_SIZE);

#endif

	// Get the video frame dimensions and format
	header->frame.width = GetBits(stream, CODEC_DIMENSION_SIZE);
	header->frame.height = GetBits(stream, CODEC_DIMENSION_SIZE);
	header->frame.format = GetBits(stream, CODEC_FORMAT_SIZE);

	// The bitstream does not contain the maximum group length
	header->gop_length = WAVELET_MAX_FRAMES;

#endif

	return error;
}

CODEC_ERROR DecodeSequenceTrailer(BITSTREAM *stream, SEQUENCE_TRAILER *trailer, int sample_type)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#ifdef CODEC_SEQUENCE_END_CODE

	uint32_t bitcount;
	uint32_t checksum;

	// Has the caller already found the bitstream marker?
	if (sample_type == SAMPLE_TYPE_NONE)
	{
		int marker;

		// Skip to the next bitword boundary
		AlignBits(stream);

		marker = GetBits(stream, CODEC_SEQUENCE_END_SIZE);
		trailer->marker = marker;
		if (marker != CODEC_SEQUENCE_END_CODE) {
			error = CODEC_ERROR_SEQUENCE_END_MARKER;
			return error;
		}
	}
	else if (sample_type == SAMPLE_TYPE_SEQUENCE_TRAILER)
	{
		// Fill in the header with the marker found by the caller
		trailer->marker = CODEC_SEQUENCE_END_CODE;
	}
	else
	{
		// This routine was called by mistake
		error = CODEC_ERROR_SEQUENCE_END_MARKER;
		return error;
	}

	// The bitcount and checksum follow the sequence end marker
	bitcount = GetBits(stream, CODEC_BITCOUNT_SIZE);
	trailer->bitcount = bitcount;

	checksum = GetBits(stream, CODEC_CHECKSUM_SIZE);
	trailer->checksum = 0;
#endif

	return error;
}

CODEC_ERROR DecodeGroupHeader(BITSTREAM *stream, GROUP_HEADER *header, TRANSFORM *transform, int sample_type)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
	int num_frames;
	int num_channels;
	int num_spatial;
	int i;

#if _CODEC_TAGS

	TAGVALUE segment;

	// Has the caller already found the bitstream marker?
	if (sample_type == SAMPLE_TYPE_NONE)
	{
		int sample_type;

		// Skip to the tag boundary
		//AlignBits(stream);

		// Check that the bitstream is aligned on a tag boundary
		assert(IsAlignedTag(stream));

		// Get the sample type
		sample_type = FindNextSample(stream);

		// The sample type should be group header
		assert(sample_type == SAMPLE_TYPE_GROUP);

		if (sample_type != SAMPLE_TYPE_GROUP) {
			error = CODEC_ERROR_GROUP_START_MARKER;
			return error;
		}
	}
	else if (sample_type == SAMPLE_TYPE_GROUP)
	{
		// Fill in the header with the marker found by the caller
		// (For backward compatibility)
		header->marker = CODEC_GROUP_START_CODE;
	}
	else
	{
		// This routine was called by mistake
		error = CODEC_ERROR_GROUP_START_MARKER;
		return error;
	}

	// The channel byte counts come before the group transform parameters
	GetGroupIndex(stream, (void **)header->channel_size, TRANSFORM_MAX_CHANNELS);
	if (stream->error != BITSTREAM_ERROR_OKAY) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}

	// Check that the transform data structure was allocated
	assert(transform != NULL);

	// Note that some information is duplicated in the transform and the header
	// and the duplicate entries should probably be resolved to prevent confusion

	// Get the transform type
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_TRANSFORM_TYPE);
	if (!IsValidSegment(stream, segment, CODEC_TAG_TRANSFORM_TYPE)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	transform->type = segment.tuple.value;
	assert(0 <= transform->type && transform->type <= TRANSFORM_TYPE_INTERLACED);

	// Get the number of frames and store in transform and group header
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_NUM_FRAMES);
	if (!IsValidSegment(stream, segment, CODEC_TAG_NUM_FRAMES)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	num_frames = segment.tuple.value;
	header->num_frames = num_frames;
	transform->num_frames = num_frames;

	// Get the number of channels
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_NUM_CHANNELS);
	if (!IsValidSegment(stream, segment, CODEC_TAG_NUM_CHANNELS)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	num_channels = segment.tuple.value;

	// Older versions of the codec that did not support color encoded the number
	// of levels instead of the number of channels in this field, so just change
	// the number of channels to one since the older bitstream has only gray data
	if (num_channels > TRANSFORM_MAX_CHANNELS) num_channels = 1;

	// Record the number of channels in the group header
	// Each channel gets its own transform
	header->num_channels = num_channels;

	// The number of levels can be calculated from the other parameters
	//transform->num_levels = GetBits(stream, CODEC_NUMLEVELS_SIZE);

	// Get the number of wavelets in the transform
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_NUM_WAVELETS);
	if (!IsValidSegment(stream, segment, CODEC_TAG_NUM_WAVELETS)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	transform->num_wavelets = segment.tuple.value;

	// Get the number of subbands to read from the bitstream
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_NUM_SUBBANDS);
	if (!IsValidSegment(stream, segment, CODEC_TAG_NUM_SUBBANDS)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->num_subbands = segment.tuple.value;

	// Get the number of levels in the spatial wavelet pyramid
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_NUM_SPATIAL);
	if (!IsValidSegment(stream, segment, CODEC_TAG_NUM_SPATIAL)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	num_spatial = segment.tuple.value;
	header->num_spatial = num_spatial;
	transform->num_spatial = num_spatial;

	// Get the type of the first wavelet encoded into the bitstream
	segment = GetTagValue(stream);
	assert(segment.tuple.tag == CODEC_TAG_FIRST_WAVELET);
	if (!IsValidSegment(stream, segment, CODEC_TAG_FIRST_WAVELET)) {
		error = CODEC_ERROR_BITSTREAM;
		return error;
	}
	header->wavelet_type = segment.tuple.value;

	// Calculate the number of levels
	//assert(transform->type = TRANSFORM_TYPE_INTERLACED);
	transform->num_levels = num_spatial + numTransformLevels[transform->type];

	// Set the frame format using a constant but read from bitstream later
	header->frame_format = FRAME_FORMAT_YUV;

	// The bitstream should be aligned on a bitword boundary
	assert(IsAlignedBits(stream));

	return error;

#else

#ifdef CODEC_GROUP_START_CODE

	// Has the caller already found the bitstream marker?
	if (sample_type == SAMPLE_TYPE_NONE)
	{
		int marker;

		// Skip to the next bitword boundary
		AlignBits(stream);

		// Look for the group header marker in the bitstream
		marker = GetBits(stream, CODEC_GROUP_START_SIZE);
		header->marker = marker;
		if (marker != CODEC_GROUP_START_CODE) {
			error = CODEC_ERROR_GROUP_START_MARKER;
			return error;
		}
	}
	else if (sample_type == SAMPLE_TYPE_GROUP)
	{
		// Fill in the header with the marker found by the caller
		header->marker = CODEC_GROUP_START_CODE;
	}
	else
	{
		// This routine was called by mistake
		error = CODEC_ERROR_GROUP_START_MARKER;
		return error;
	}

#endif

	// Check that the transform data structure was allocated
	assert(transform != NULL);

	// Note that some information is duplicated in the transform and the header
	// and the duplicate entries should probably be resolved to prevent confusion

	// Get the transform type
	transform->type = GetBits(stream, CODEC_TRANSFORM_SIZE);
	assert(0 <= transform->type && transform->type <= TRANSFORM_TYPE_INTERLACED);

	// Get the number of frames and store in transform and group header
	num_frames = GetBits(stream, CODEC_NUMFRAMES_SIZE);
	header->num_frames = num_frames;
	transform->num_frames = num_frames;

	// Get the number of channels
	num_channels = GetBits(stream, CODEC_NUMCHANNELS_SIZE);

	// Older versions of the codec that did not support color encoded the number
	// of levels instead of the number of channels in this field, so just change
	// the number of channels to one since the older bitstream has only gray data
	if (num_channels > TRANSFORM_MAX_CHANNELS) num_channels = 1;

	// Record the number of channels in the group header
	// Each channel gets its own transform
	header->num_channels = num_channels;

	// The number of levels can be calculated from the other parameters
	//transform->num_levels = GetBits(stream, CODEC_NUMLEVELS_SIZE);

	// Get the number of wavelets in the transform
	transform->num_wavelets = GetBits(stream, CODEC_NUMWAVELETS_SIZE);

	// Get the number of subbands to read from the bitstream
	header->num_subbands = GetBits(stream, CODEC_NUMSUBBANDS_SIZE);

	// Get the number of levels in the spatial wavelet pyramid
	num_spatial = GetBits(stream, CODEC_NUMLEVELS_SIZE);
	header->num_spatial = num_spatial;
	transform->num_spatial = num_spatial;

	// Get the type of the first wavelet encoded into the bitstream
	header->wavelet_type = GetBits(stream, CODEC_WAVELET_SIZE);

	// Calculate the number of levels
	//assert(transform->type = TRANSFORM_TYPE_INTERLACED);
	transform->num_levels = num_spatial + numTransformLevels[transform->type];

	// Set the frame format using a constant but read from bitstream later
	header->frame_format = FRAME_FORMAT_YUV;

	// Align on bitword boundary
	AlignBits(stream);

	// Get the channel byte counts
	for(i = 0; i < num_channels; i++)
		header->channel_size[i] = GetBits(stream, CODEC_CHANNEL_BITSTREAM_SIZE);

	// Align on bitword boundary
	AlignBits(stream);

	return error;

#endif
}

#endif

CODEC_ERROR DecodeChannelHeader(BITSTREAM *stream, CHANNEL_HEADER *header, int sample_type)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
	int channel;

#if _CODEC_TAGS

	// Check that the stream is aligned to a tag boundary
	assert(IsAlignedTag(stream));

	// Get the channel number
	channel = GetValue(stream, CODEC_TAG_CHANNEL);
	header->channel = channel;

#else

#ifdef CODEC_CHANNEL_START_CODE
	if (sample_type == SAMPLE_TYPE_NONE)
	{
		int marker;

		// Skip to the next bitword boundary
		AlignBits(stream);

		marker = GetBits(stream, CODEC_GROUP_END_SIZE);
		header->marker = marker;
		if (marker != CODEC_GROUP_END_CODE) {
			error = CODEC_ERROR_GROUP_END_MARKER;
			return error;
		}
	}
#endif

	// Get the channel number
	channel = GetBits(stream, CODEC_CHANNEL_SIZE);
	header->channel = channel;

	AlignBits(stream);

#endif

	return error;
}

#if 0

CODEC_ERROR DecodeGroupTrailer(BITSTREAM *stream, GROUP_TRAILER *trailer, int sample_type)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#if _CODEC_TAGS

	unsigned short checksum;	// Need to compute the checksum

	//AlignBitsTag(stream);

	// The sample type should already have been determined
	assert(sample_type == SAMPLE_TYPE_GROUP_TRAILER);

	// Get the checksum from the group trailer
	checksum = GetValue(stream, CODEC_TAG_GROUP_TRAILER);

	// Need to properly check the checksum
	assert(checksum == 0);

#else

#ifdef CODEC_GROUP_END_CODE
	if (sample_type == SAMPLE_TYPE_NONE)
	{
		int marker;

		// Skip to the next bitword boundary
		AlignBits(stream);

		marker = GetBits(stream, CODEC_GROUP_END_SIZE);
		trailer->marker = marker;
		if (marker != CODEC_GROUP_END_CODE) {
			error = CODEC_ERROR_GROUP_END_MARKER;
			return error;
		}
	}
#endif

#endif

	return error;
}


// Decode the group header extension
CODEC_ERROR DecodeGroupExtension(BITSTREAM *stream, CODEC_STATE *codec)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;
	TAGVALUE segment;

	// Read optional tag value pairs from the bitstream
	for (;;)
	{
		// Read the next optional tag value pair from the bitstream
		segment = GetTagOptional(stream);

		// Use the value to modify the codec state
		switch (segment.tuple.tag)
		{
		case CODEC_TAG_INTERLACED_FLAGS:	// Interlaced structure of the video stream
			codec->interlaced_flags = segment.tuple.value;
			break;

		case CODEC_TAG_PROTECTION_FLAGS:	// Copy protection bits
			codec->protection_flags = segment.tuple.value;
			break;

		case CODEC_TAG_PICTURE_ASPECT_X:	// Numerator of the picture aspect ratio
			codec->picture_aspect_x = segment.tuple.value;
			break;

		case CODEC_TAG_PICTURE_ASPECT_Y:	// Denominator of the picture aspect ratio
			codec->picture_aspect_y = segment.tuple.value;
			break;

		case CODEC_TAG_ZERO:
			// Done reading optional tag value pairs from the group header extension
			goto finish;
			break;

		default:
			// Ignore optional tags that are not known to this decoder
			break;
		}
	}

finish:

	return error;
}


CODEC_ERROR DecodeRowTrailer(BITSTREAM *stream)
{
	CODEC_ERROR error = CODEC_ERROR_OKAY;

#ifdef CODEC_ROWEND_CODE
	int marker = GetBits(stream, CODEC_ROWEND_SIZE);
	if (marker != CODEC_ROWEND_CODE) {
		error = CODEC_ERROR_RUN_ROWEND_MARKER;
		return error;
	}
#endif

	return error;
}

#endif

// Initialize the transform for the next channel
void InitChannelTransform(TRANSFORM *next, TRANSFORM *prev)
{
	next->type = prev->type;
	assert(0 <= next->type && next->type <= TRANSFORM_TYPE_INTERLACED);

	next->num_frames = prev->num_frames;
	next->num_wavelets = prev->num_wavelets;
	next->num_spatial = prev->num_spatial;

	next->num_levels = next->num_spatial + numTransformLevels[next->type];
}

#if !_NEW_DECODER

#if _ALLOCATOR
void FreeGroup(ALLOCATOR *allocator, GROUP *group)
#else
void FreeGroup(GROUP *group)
#endif
{
	int num_channels;
	int i;

	if (group == NULL) return;

	num_channels = group->header.num_channels;

	for (i = 0; i < num_channels; i++)
	{
		TRANSFORM *transform = group->transform[i];

#if _ALLOCATOR
		// Free the memory allocated for the transform
		ClearTransform(allocator, transform);

		// Free the transform data structure itself
		Free(allocator, transform);
#else
		// Free the memory allocated for the transform
		ClearTransform(transform);

		// Free the transform data structure itself
		MEMORY_FREE(transform);
#endif
	}

	// Free the group structure itself
#if _ALLOCATOR
	Free(allocator, group);
#else
	MEMORY_FREE(group);
#endif
}

#endif


// Can a frame with the specified dimensions be transformed into a wavelet pyramid?
bool IsFrameTransformable(int width, int height, int transform_type, int num_spatial)
{
	// Use the chroma dimensions since they are smaller
	int chroma_width = width;
	int chroma_height = height;

	// The reduction is the number of times that the image dimensions are reduced by half
	int reduction;
	int divisor;

	// Is the transform type valid?
	assert(TRANSFORM_TYPE_FIRST <= transform_type && transform_type <= TRANSFORM_TYPE_LAST);
	if (!(TRANSFORM_TYPE_FIRST <= transform_type && transform_type <= TRANSFORM_TYPE_LAST))
		return false;

	// Compute the reduction in the frame dimensions
	reduction = num_spatial;

	// The fieldplus transform uses one spatial transform for the temporal highpass
	if (transform_type == TRANSFORM_TYPE_FIELDPLUS)
		--reduction;

	// The reduction due to the frame transform is the same as for a spatial transform
	if (transform_type == TRANSFORM_TYPE_SPATIAL ||
		transform_type == TRANSFORM_TYPE_FIELD   ||
		transform_type == TRANSFORM_TYPE_FIELDPLUS)
		++reduction;

	divisor = (1 << reduction);

	// Check that the chroma width and height are even divisible by the reduction
	if ((chroma_width % divisor) != 0) return false;
	if ((chroma_height % divisor) != 0) return false;

	// The frame can be transformed by the specified transform
	return true;
}


/***** Routines for printing the bitstream to a file *****/

#if _DEBUG

void PrintCompressionInfo(FILE *logfile, IFRAME *frame)
{
	IMAGE *image = frame->image;
	//ZEROTREE *zerotree = frame->zerotree;
	uint32_t bitcount = frame->trailer.bitcount;
	uint32_t size = ComputeImageSizeBits(image);
	float ratio = (float)bitcount / size;
	//float estimate = CompressionRatio(zerotree);

	fprintf(logfile, "Bitstream length:  %d\n", bitcount);
	fprintf(logfile, "Uncompressed size: %d\n", size);
	fprintf(logfile, "\n");
	fprintf(logfile, "Compression ratio: %.0f percent\n", 100.0 * ratio);

	//fprintf(logfile, "\n");
	//fprintf(logfile, "Estimated compression ratio: %.2f percent\n", 100.0 * estimate);
}

#endif
