/*! @file error.h

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

#ifndef _ERROR_H
#define _ERROR_H

#include "bitstream.h"		// The codec error codes include the bitstream errors

typedef enum codec_error
{
	CODEC_ERROR_OKAY = 0,				// No error during encoding or decoding
	CODEC_ERROR_SEQUENCE_START_MARKER,	// Could not find start of video sequence header
	CODEC_ERROR_SEQUENCE_END_MARKER,	// Could not find start of video sequence trailer
	CODEC_ERROR_GROUP_START_MARKER,		// Could not find start of video group header
	CODEC_ERROR_GROUP_END_MARKER,		// Could not find start of video group trailer
	CODEC_ERROR_FRAME_START_MARKER,		// Could not find start of frame header
	CODEC_ERROR_FRAME_END_MARKER,		// Could not find start of frame trailer
	CODEC_ERROR_LOWPASS_START_MARKER,	// Could not find start of lowpass band header
	CODEC_ERROR_LOWPASS_END_MARKER,		// Could not find start of lowpass band trailer
	CODEC_ERROR_HIGHPASS_START_MARKER,	// Could not find start of wavelet highpass header
	CODEC_ERROR_HIGHPASS_END_MARKER,	// Could not find start of wavelet highpass trailer (10)
	CODEC_ERROR_HIGHPASS_INDEX,			// Wavelet highpass data not in expected order
	CODEC_ERROR_HIGHPASS_LEVEL,
	CODEC_ERROR_HIGHPASS_BANDS,
	CODEC_ERROR_BAND_START_MARKER,		// Could not find start of highpass band header
	CODEC_ERROR_BAND_END_MARKER,		// Could not find start of highpass band trailer
	CODEC_ERROR_BAND_NUMBER,
	CODEC_ERROR_SCODE_COUNT,
	CODEC_ERROR_COEFFICIENT_START_MARKER,
	CODEC_ERROR_COEFFICIENT_END_MARKER,
	CODEC_ERROR_BITS_PER_COEFFICIENT,	// (20)
	CODEC_ERROR_COEFFICIENT_COUNT,
	CODEC_ERROR_VLC_DECODE,
	CODEC_ERROR_RUN_DECODE,
	CODEC_ERROR_RUN_ROWEND_MARKER,
	
	CODEC_ERROR_NULL_WAVELET,
	CODEC_ERROR_NULL_ZEROTREE,
	
	CODEC_ERROR_STREAM_SYNTAX,
	
	CODEC_ERROR_SAMPLE_INDEX,
	CODEC_ERROR_READ_SAMPLE,
	CODEC_ERROR_CONVERT_SAMPLE,			// (30)
	
	CODEC_ERROR_MEMORY_ALLOC,
	CODEC_ERROR_FRAME_TYPE,				// Unsupported type of frame
	CODEC_ERROR_RESERVED_1,				// Unused error code (was CODEC_ERROR_BITSTREAM)
	
	CODEC_ERROR_TRANSFORM,				// Error reconstructing the transform
	
	CODEC_ERROR_FRAMESIZE,				// Requested output frame is too small for the decoded frame
	CODEC_ERROR_RESOLUTION,				// Requested output frame resolution is not supported
	
	CODEC_ERROR_SAMPLE_TYPE,			// Unknown type of sample
	
	CODEC_ERROR_TRANSFORM_TYPE,			// Invalid transform type
	CODEC_ERROR_NUM_FRAMES,				// Invalid number of frames in the group
	CODEC_ERROR_NUM_CHANNELS,			// (40) Invalid number of channels in the group (40)
	CODEC_ERROR_NUM_WAVELETS,			// Invalid number of wavelets in the transform
	CODEC_ERROR_NUM_SUBBANDS,			// Invalid number of subbands in the transform
	CODEC_ERROR_NUM_SPATIAL,			// Invalid number of spatial wavelets in the transform
	CODEC_ERROR_FIRST_WAVELET,			// Invalid type for the first wavelet to decode
	
	CODEC_ERROR_TRANSFORM_MEMORY,		// Could not allocate memory for the wavelet transform
	
	CODEC_ERROR_UNKNOWN_REQUIRED_TAG,	// Require not supported in this decoder version.
	
	CODEC_ERROR_INIT_CODEBOOKS,			// Error initializing the codebooks
	CODEC_ERROR_INIT_FSM,				// Error initializing the decoder finite state machine
	CODEC_ERROR_NUM_STATES,				// Too many states for the finite state machine tables
	CODEC_ERROR_FSM_ALLOC,				// (50) Could not allocate finite state machine lookup table (50)
	
	CODEC_ERROR_DECODING_SUBBAND,
	CODEC_ERROR_DECODE_SAMPLE_CHANNEL_HEADER,
	
	CODEC_ERROR_BADFORMAT,				// The encoder cannot handle the input format
	CODEC_ERROR_INVALID_BITSTREAM,		// The bitstream is not valid
	
	CODEC_ERROR_INVALID_FORMAT,			// The format is not supported by the encoder or decoder
	CODEC_ERROR_INVALID_SIZE,			// The image dimensions are not supported by the encoder
	
	CODEC_ERROR_INVALID_ARGUMENT,		// The subroutine argument is not valid
	CODEC_ERROR_BAD_FRAME,				// The frame data structure is not valid

	CODEC_ERROR_UNSUPPORTED_FORMAT,		// The decoder does not support the output format
	CODEC_ERROR_LICENCE_EXPIRED,		// License
	CODEC_ERROR_3D_UNKNOWN,				// Unknown 3D error
	CODEC_ERROR_FRAME_DIMENSIONS,		// Could not determine the frame dimensions

	CODEC_ERROR_NULLPTR,				// Unexpected null pointer
	CODEC_ERROR_UNEXPECTED,				// Unexpected condition


	/***** Reserve a block of error codes for bitstream errors *****/

	CODEC_ERROR_BITSTREAM = 256,		// Error while reading or writing the bitstream


	/***** Reserve a block of error codes for problems parsing a preferences file *****/

	CODEC_ERROR_PREFSFILE = 512,		// Error while parsing the user preferences file


	/***** Reserve a block of error codes for the calling application *****/
	CODEC_ERROR_APPLICATION = 1024,
	CODEC_ERROR_BAD_ARGUMENT,

	CODEC_ERROR_BANDFILE_OPEN_FAILED,	// Could not open a band file for reading
	CODEC_ERROR_BANDFILE_CREATE_FAILED,	// Could not open a band file for writing
	CODEC_ERROR_BANDFILE_READ_FAILED,	// Error while reading data from the band file
	CODEC_ERROR_BANDFILE_WRITE_FAILED,	// Error while writing data to the band file


	/***** Insert new error codes above this line *****/

	//CODEC_ERROR_NUM_ERRORS			// Number of error codes (including okay)

	CODEC_ERROR_NUM_ERRORS = CODEC_ERROR_BITSTREAM + BITSTREAM_ERROR_NUM_ERRORS,

	/*
		The codec error code for the number of errors may not be useful
		because blocks of error codes are reserved for subsystem errors.
	*/

} CODEC_ERROR;

// Convert a bistream error code into a codec error code
INLINE static CODEC_ERROR CodecErrorBitstream(BITSTREAM *stream)
{
	int32_t error_code = (int32_t)CODEC_ERROR_BITSTREAM;

	if (stream != NULL)
	{
		// Do not return a codec error if the bitstream does not have an error
		if (stream->error == BITSTREAM_ERROR_OKAY) {
			return CODEC_ERROR_OKAY;
		}

		// Embed the bitstream error code in a codec error code
		return (CODEC_ERROR)(error_code | stream->error);
	}
	else
	{
		// Return the generic bitstream error code
		return (CODEC_ERROR)error_code;
	}
}

#endif
