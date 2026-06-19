/*! @file SampleDecoder.cpp
*
*  @brief Implementation of the C++ class for the CineForm decoder that is used internally
*  by the C language decoder SDK that returns a pointer to an instance of this class
*  as an opaque data type.  The C language API isolates customer code from possible
*  changes in the sample decoder class.
*
*  This class is used directly by CineForm software and is the preferred way to call
*  the decoder API in teh codec library since the sample decoder class isolates our
*  software from changes to the codec library.
*
*  Statements were added to the methods that invoke routines in the codec library
*  to catch all exceptions and return an error code that indicates that an internal
*  codec error occurred.  A message is also printed to the console.  Is there a way
*  to capture more information about the source and cause of the exception in the
*  catch handler and log this information to the console?
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


#include "StdAfx.h"

#if _WIN32

// Export the interface to the sample decoder
#define DECODERDLL_EXPORTS	1

#else

#ifdef DECODERDLL_API
#undef DECODERDLL_API
#endif

#define DECODERDLL_API __attribute__((visibility("default")))

#if __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#endif

// Include files from the codec library
#include "decoder.h"
#include "thumbnail.h"
#include "metadata.h"

// Include files for the encoder DLL
#ifdef _WIN32
#include "CFHDDecoder.h"
#else
#include "CFHDDecoder.h"
#endif
#include "IAllocator.h"
#include "ISampleDecoder.h"
#include "SampleDecoder.h"
#include "Conversion.h"

//TODO: Need to add logfile capability
#define LOGFILE 0

//TODO: Support arbitrary scaling
#define _SCALING 0

// Clear previous settings of the flag for debug output
#ifdef TRACE
#undef TRACE
#endif

// Set the flag for debug output
#ifndef _PRODUCTION
#define TRACE	1
#else
#define TRACE	0
#endif

typedef struct decodedFormatKey
{
		CFHD_PixelFormat outputFormat;		// The requested output pixel format
		ENCODED_FORMAT encodedFormat;		// Internal format of the encoded sample

} DecodedFormatKey;

// Table of decoded format informaton indexed by the requested format and the encoded format
static struct decodedFormatEntry
{
	DecodedFormatKey key;			// The key used for table lookups
	DECODED_FORMAT decodedFormat;	// Format used for decoding the sample
	uint32_t pixelSize;				// Size of the decoded pixel in bits (all components)

} decodedFormatTable[] =
{
	// These table entries were adapted from the QuickTime decompressor
	{{CFHD_PIXEL_FORMAT_2VUY,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_UYVY,	2},	// 2vuy
	{{CFHD_PIXEL_FORMAT_YUY2,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_YUYV,	2},	// YUY2
	{{CFHD_PIXEL_FORMAT_V210,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_V210,	3},	// v210
	{{CFHD_PIXEL_FORMAT_BGRA,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_RGB32,	4},	// BGRA
	{{CFHD_PIXEL_FORMAT_BGRa,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_RGB32_INVERTED,	4},	// BGRA
	{{CFHD_PIXEL_FORMAT_RG24,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_RGB24,	3},	// RGB
	{{CFHD_PIXEL_FORMAT_B64A,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_B64A,	8},	// b64a
	{{CFHD_PIXEL_FORMAT_RG64,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_RG64,	8},	// RG64
	{{CFHD_PIXEL_FORMAT_R210,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_R210,	4},	// r210
	{{CFHD_PIXEL_FORMAT_DPX0,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_DPX0,	4},	// DPX0
	{{CFHD_PIXEL_FORMAT_RG30,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_RG30,	4},	// RG30
	{{CFHD_PIXEL_FORMAT_AB10,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_AB10,	4},	// AB10
	{{CFHD_PIXEL_FORMAT_AR10,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_AR10,	4},	// AR10
	{{CFHD_PIXEL_FORMAT_YU64,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_YU64,	4}, // YU64
	{{CFHD_PIXEL_FORMAT_RG48,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_RG48,	6},	// RG48
	{{CFHD_PIXEL_FORMAT_WP13,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_WP13,	6},	// WP13
	{{CFHD_PIXEL_FORMAT_W13A,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_W13A,	8},	// W13A
	{{CFHD_PIXEL_FORMAT_YUYV,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_YUYV,	2},	// yuyv
	{{CFHD_PIXEL_FORMAT_R408,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_R408,	4},	// yuva
	{{CFHD_PIXEL_FORMAT_V408,	ENCODED_FORMAT_YUV_422},	DECODED_FORMAT_V408,	4},	// yuva

	{{CFHD_PIXEL_FORMAT_2VUY,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_UYVY,	2},	// 2vuy
	{{CFHD_PIXEL_FORMAT_YUY2,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_YUYV,	2},	// YUY2
	{{CFHD_PIXEL_FORMAT_V210,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_V210,	3},	// v210
	{{CFHD_PIXEL_FORMAT_B64A,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_B64A,	8},	// b64a
	{{CFHD_PIXEL_FORMAT_RG64,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_RG64,	8},	// RG64
	{{CFHD_PIXEL_FORMAT_R210,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_R210,	4},	// r210
	{{CFHD_PIXEL_FORMAT_DPX0,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_DPX0,	4},	// DPX0
	{{CFHD_PIXEL_FORMAT_RG30,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_RG30,	4},	// RG30
	{{CFHD_PIXEL_FORMAT_AB10,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_AB10,	4},	// AB10
	{{CFHD_PIXEL_FORMAT_AR10,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_AR10,	4},	// AR10
	{{CFHD_PIXEL_FORMAT_YU64,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_YU64,	4}, // YU64
	{{CFHD_PIXEL_FORMAT_BGRA,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_RGB32,	4},	// BGRA
	{{CFHD_PIXEL_FORMAT_BGRa,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_RGB32_INVERTED,	4},	// BGRA
	{{CFHD_PIXEL_FORMAT_RG24,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_RGB24,	3},	// RGB
	{{CFHD_PIXEL_FORMAT_RG48,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_RG48,	6},	// RG48
	{{CFHD_PIXEL_FORMAT_WP13,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_WP13,	6},	// WP13
	{{CFHD_PIXEL_FORMAT_W13A,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_W13A,	8},	// W13A
	{{CFHD_PIXEL_FORMAT_YUYV,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_YUYV,	2},	// yuyv
	{{CFHD_PIXEL_FORMAT_R408,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_R408,	4},	// yuva
	{{CFHD_PIXEL_FORMAT_V408,	ENCODED_FORMAT_RGB_444},	DECODED_FORMAT_V408,	4},	// yuva

	{{CFHD_PIXEL_FORMAT_2VUY,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_UYVY,	2},	// 2vuy
	{{CFHD_PIXEL_FORMAT_YUY2,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_YUYV,	2},	// YUY2
	{{CFHD_PIXEL_FORMAT_V210,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_V210,	3},	// v210
	{{CFHD_PIXEL_FORMAT_B64A,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_B64A,	8},	// b64a
	{{CFHD_PIXEL_FORMAT_RG64,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_RG64,	8},	// RG64
	{{CFHD_PIXEL_FORMAT_R210,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_R210,	4},	// r210
	{{CFHD_PIXEL_FORMAT_DPX0,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_DPX0,	4},	// DPX0
	{{CFHD_PIXEL_FORMAT_RG30,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_RG30,	4},	// RG30
	{{CFHD_PIXEL_FORMAT_AB10,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_AB10,	4},	// AB10
	{{CFHD_PIXEL_FORMAT_YU64,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_YU64,	4}, // YU64
	{{CFHD_PIXEL_FORMAT_AR10,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_AR10,	4},	// AR10
	{{CFHD_PIXEL_FORMAT_BGRA,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_RGB32,	4},	// BGRA
	{{CFHD_PIXEL_FORMAT_BGRa,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_RGB32_INVERTED,	4},	// BGRA
	{{CFHD_PIXEL_FORMAT_RG24,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_RGB24,	3},	// RGB
	{{CFHD_PIXEL_FORMAT_RG48,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_RG48,	6},	// RG48
	{{CFHD_PIXEL_FORMAT_WP13,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_WP13,	6},	// WP13
	{{CFHD_PIXEL_FORMAT_W13A,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_W13A,	8},	// W13A
	{{CFHD_PIXEL_FORMAT_YUYV,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_YUYV,	2},	// yuyv
	{{CFHD_PIXEL_FORMAT_R408,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_R408,	4},	// yuva
	{{CFHD_PIXEL_FORMAT_V408,	ENCODED_FORMAT_RGBA_4444},	DECODED_FORMAT_V408,	4},	// yuva

	{{CFHD_PIXEL_FORMAT_2VUY,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_UYVY,	2},	// 2vuy
	{{CFHD_PIXEL_FORMAT_YUY2,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_YUYV,	2},	// YUY2
	{{CFHD_PIXEL_FORMAT_V210,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_V210,	3},	// v210
	{{CFHD_PIXEL_FORMAT_B64A,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_B64A,	8},	// b64a
	{{CFHD_PIXEL_FORMAT_RG64,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_RG64,	8},	// RG64
	{{CFHD_PIXEL_FORMAT_R210,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_R210,	4},	// r210
	{{CFHD_PIXEL_FORMAT_DPX0,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_DPX0,	4},	// DPX0
	{{CFHD_PIXEL_FORMAT_RG30,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_RG30,	4},	// RG30
	{{CFHD_PIXEL_FORMAT_AB10,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_AB10,	4},	// AB10
	{{CFHD_PIXEL_FORMAT_AR10,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_AR10,	4},	// AR10
	{{CFHD_PIXEL_FORMAT_BGRA,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_RGB32,	4},	// BGRA
	{{CFHD_PIXEL_FORMAT_BGRa,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_RGB32_INVERTED,	4},	// BGRA
	{{CFHD_PIXEL_FORMAT_RG24,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_RGB24,	3},	// RGB
	{{CFHD_PIXEL_FORMAT_RG48,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_RG48,	6},	// RG48
	{{CFHD_PIXEL_FORMAT_WP13,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_WP13,	6},	// WP13
	{{CFHD_PIXEL_FORMAT_W13A,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_W13A,	8},	// W13A
	{{CFHD_PIXEL_FORMAT_YUYV,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_YUYV,	2},	// yuyv
	{{CFHD_PIXEL_FORMAT_BYR2,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_BYR2,	2},	// BYR2
	{{CFHD_PIXEL_FORMAT_BYR4,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_BYR4,	2},	// BYR2
	{{CFHD_PIXEL_FORMAT_YU64,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_YU64,	4}, // YU64
	{{CFHD_PIXEL_FORMAT_R408,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_R408,	4},	// yuva
	{{CFHD_PIXEL_FORMAT_V408,	ENCODED_FORMAT_BAYER},		DECODED_FORMAT_V408,	4},	// yuva

	// Avid pixel formats
	{{CFHD_PIXEL_FORMAT_CT_UCHAR, ENCODED_FORMAT_YUV_422}, DECODED_FORMAT_CT_UCHAR, 2},		// avu8
	{{CFHD_PIXEL_FORMAT_CT_UCHAR, ENCODED_FORMAT_RGB_444}, DECODED_FORMAT_CT_UCHAR, 2},		// avu8
	{{CFHD_PIXEL_FORMAT_CT_UCHAR, ENCODED_FORMAT_RGBA_4444}, DECODED_FORMAT_CT_UCHAR, 2},	// avu8
	{{CFHD_PIXEL_FORMAT_CT_UCHAR, ENCODED_FORMAT_BAYER}, DECODED_FORMAT_CT_UCHAR, 2},		// avu8

	{{CFHD_PIXEL_FORMAT_CT_SHORT_2_14, ENCODED_FORMAT_YUV_422}, DECODED_FORMAT_CT_SHORT_2_14, 4},	// a214
	{{CFHD_PIXEL_FORMAT_CT_SHORT_2_14, ENCODED_FORMAT_RGB_444}, DECODED_FORMAT_CT_SHORT_2_14, 4},	// a214
	{{CFHD_PIXEL_FORMAT_CT_SHORT_2_14, ENCODED_FORMAT_RGBA_4444}, DECODED_FORMAT_CT_SHORT_2_14, 4},	// a214

	{{CFHD_PIXEL_FORMAT_CT_USHORT_10_6, ENCODED_FORMAT_YUV_422}, DECODED_FORMAT_CT_USHORT_10_6, 4},	// a106
	{{CFHD_PIXEL_FORMAT_CT_USHORT_10_6, ENCODED_FORMAT_RGB_444}, DECODED_FORMAT_CT_USHORT_10_6, 4},	// a106
	{{CFHD_PIXEL_FORMAT_CT_USHORT_10_6, ENCODED_FORMAT_RGBA_4444}, DECODED_FORMAT_CT_USHORT_10_6, 4},// a106

	{{CFHD_PIXEL_FORMAT_CT_SHORT, ENCODED_FORMAT_YUV_422},	 DECODED_FORMAT_CT_SHORT, 4},	// avu8
	{{CFHD_PIXEL_FORMAT_CT_SHORT, ENCODED_FORMAT_RGB_444},	 DECODED_FORMAT_CT_SHORT, 4},	// avu8
	{{CFHD_PIXEL_FORMAT_CT_SHORT, ENCODED_FORMAT_RGBA_4444}, DECODED_FORMAT_CT_SHORT, 4},	// avu8

	{{CFHD_PIXEL_FORMAT_CT_10BIT_2_8, ENCODED_FORMAT_YUV_422}, DECODED_FORMAT_CT_10Bit_2_8, 4},	// av28
	{{CFHD_PIXEL_FORMAT_CT_10BIT_2_8, ENCODED_FORMAT_RGB_444}, DECODED_FORMAT_YU64, 4},			// av28
	{{CFHD_PIXEL_FORMAT_CT_10BIT_2_8, ENCODED_FORMAT_RGBA_4444}, DECODED_FORMAT_YU64, 4},		// av28
	{{CFHD_PIXEL_FORMAT_CT_10BIT_2_8, ENCODED_FORMAT_BAYER}, DECODED_FORMAT_YU64, 4},			// av28

	//TODO: Add support for decoding Bayer to other Avid pixel formats

};

static const int decodedFormatTableLength = sizeof(decodedFormatTable)/sizeof(decodedFormatTable[0]);

typedef struct decodedFormatEntry DecodedFormatEntry;

static bool LookupDecodedFormat(CFHD_PixelFormat outputFormat,
								ENCODED_FORMAT encodedFormat,
								DecodedFormatEntry **entryPtrOut)
{
	int index;

	// Form the lookup key
	//DecodedFormatKey key = {outputFormat, encodedFormat};
	DecodedFormatKey key = {outputFormat, encodedFormat};

	DecodedFormatEntry *entry = decodedFormatTable;
	bool entryFoundFlag = false;
	for (index = 0; index < decodedFormatTableLength; index++, entry++)
	{
		if (memcmp(&entry->key, &key, sizeof(key)) == 0) {
			entryFoundFlag = true;
			break;
		}
	}

	if (entryPtrOut != NULL)
	{
		*entryPtrOut = (entryFoundFlag ? entry : NULL);
	}

	return entryFoundFlag;
}

bool GetDecodedFormat(ENCODED_FORMAT encodedFormat,
					  CFHD_PixelFormat outputFormat,
					  DECODED_FORMAT *decodedFormatOut,
					  uint32_t *pixelSizeOut)
{
	DecodedFormatEntry *entryPtr = NULL;

	if (decodedFormatOut == NULL || pixelSizeOut == NULL) {
		return false;
	}

	// Clear the output values in case the correct values cannot be determined
	*decodedFormatOut = DECODED_FORMAT_UNSUPPORTED;
	*pixelSizeOut = 0;

	if (LookupDecodedFormat(outputFormat, encodedFormat, &entryPtr))
	{
		assert(entryPtr != NULL);

		*decodedFormatOut = entryPtr->decodedFormat;
		*pixelSizeOut = entryPtr->pixelSize;

		return true;
	}

	// Not able to set the decoded format or pixel size
	return false;
}


// Convenience methods for computing frame dimensions and formats
size_t GetFrameSize(int width, int height, CFHD_PixelFormat format)
{
size_t framePitch = GetFramePitch(width, format);
return (height * framePitch);
}

int32_t GetFramePitch(int width, CFHD_PixelFormat format)
{
// Need to handle the v210 format as a special case
if (format == CFHD_PIXEL_FORMAT_V210) {
return V210FramePitch(width);
}

// Compute the pitch for the decoded format and width (in bytes)
int32_t pixelSize = GetPixelSize(format);
int32_t pitch = width * pixelSize;

// Round the pitch to a multiple of 16 bytes
pitch = ((pitch + 0x0F) & ~0x0F);

return pitch;
}


int GetPixelSize(CFHD_PixelFormat format)
{
int pixelSize = 0;

// Compute the pixel size
switch (format)
{
case CFHD_PIXEL_FORMAT_YUY2:
case CFHD_PIXEL_FORMAT_2VUY:
case CFHD_PIXEL_FORMAT_YUYV:
case CFHD_PIXEL_FORMAT_BYR2:
case CFHD_PIXEL_FORMAT_BYR4:
case CFHD_PIXEL_FORMAT_CT_10BIT_2_8:		// Avid format with two planes of 2-bit and 8-bit pixels
pixelSize = 2;
break;

case CFHD_PIXEL_FORMAT_V210:
pixelSize = 0;				// 3 is close, but no true pixel size can be returned.
break;

case CFHD_PIXEL_FORMAT_BGRA:
case CFHD_PIXEL_FORMAT_BGRa:
case CFHD_PIXEL_FORMAT_R408:
case CFHD_PIXEL_FORMAT_V408:
case CFHD_PIXEL_FORMAT_R210:
case CFHD_PIXEL_FORMAT_DPX0:
case CFHD_PIXEL_FORMAT_RG30:
case CFHD_PIXEL_FORMAT_AB10:
case CFHD_PIXEL_FORMAT_AR10:
case CFHD_PIXEL_FORMAT_YU64:
case CFHD_PIXEL_FORMAT_CT_SHORT_2_14:		// Avid fixed point 2.14 pixel format
case CFHD_PIXEL_FORMAT_CT_USHORT_10_6:		// Avid fixed point 10.6 pixel format
case CFHD_PIXEL_FORMAT_CT_SHORT:			// Avid 16-bit signed pixels
pixelSize = 4;
break;

case CFHD_PIXEL_FORMAT_RG48:
case CFHD_PIXEL_FORMAT_WP13:
pixelSize = 6;
break;

case CFHD_PIXEL_FORMAT_RG64:
case CFHD_PIXEL_FORMAT_B64A:
case CFHD_PIXEL_FORMAT_W13A:
pixelSize = 8;
break;

case CFHD_PIXEL_FORMAT_RG24:
pixelSize = 3;
break;

default:
throw CFHD_ERROR_BADFORMAT;
break;
}

return pixelSize;
}

int V210FramePitch(int width)
{
// Force 16 byte alignment
width = ((width + 47)/48) * 48;

// The v210 format uses 16 bytes to encode 6 pixels
int pitch = (width * 8) / 3;

// Check that the pitch is a multiple of 16 bytes
assert((pitch & 0x0F) == 0);

return pitch;
}


__inline static size_t Align16(size_t x)
{
	return ((x + 0x0F) & ~0x0F);
}

CSampleDecoder::CSampleDecoder(CFHD_ALLOCATOR *allocator,
							   CFHD_LicenseKey license,
							   FILE *logfile) :
	m_logfile(logfile),
	m_decoder(NULL),
	m_allocator(allocator),
	m_encodedWidth(0),
	m_encodedHeight(0),
	m_encodedFormat(ENCODED_FORMAT_UNKNOWN),
	m_decodedWidth(0),
	m_decodedHeight(0),
	m_outputWidth(0),
	m_outputHeight(0),
	m_outputFormat(CFHD_PIXEL_FORMAT_UNKNOWN),
	m_decodedFormat(DECODED_FORMAT_UNSUPPORTED),
	m_decodedResolution(DECODED_RESOLUTION_UNSUPPORTED),
	m_decodedFrameBuffer(NULL),
	m_decodedFrameSize(0),
	m_decodedFramePitch(0),
	m_decodingFlags(CFHD_DECODING_FLAGS_NONE),
	m_preparedForThumbnails(false),
	m_channelsActive(1),
	m_channelMix(0)
{
	if (license)
	{
		// Copy the license provided as an argument
		memcpy(m_license, license, sizeof(m_license));
	}
	else
	{
		// Clear the license key
		memset(m_license, 0, sizeof(m_license));
	}
}

/*!
	@brief Destructor for the sample decoder

	The sample decoder allocates and owns the pointer to the instance
	of the decoder from the codec library, so must free the decoder in
	the destructor.

	The sample decoder does not own the pointer to the logfile, which
	is passed in as an argument to the constructor, so must not attempt
	to free the logfile.  In a typical use case, a program may instantiate
	multiple sample decoders and pass the same logfile to all decoders.
*/
CSampleDecoder::~CSampleDecoder()
{
	ReleaseDecoder();
}


/*
	The general strategy is to first list formats that do not require
	color conversion in decreasing pixel depth and then to list other
	pixel formats that require color conversion.  No pixel formants are
	listed if it does not make sense to decode the video to that format.
*/
CFHD_Error
CSampleDecoder::GetOutputFormats(void *samplePtr,
								 size_t sampleSize,
								 CFHD_PixelFormat *outputFormatArray,
								 int outputFormatArrayLength,
								 int *actualOutputFormatCountOut)
{
	// List the output formats in decreasing order for each encoded format

	// Best output formats for video encoded as YUV 4:2:2
	static CFHD_PixelFormat outputFormatYUV422[] =
	{
		//TODO: Need to provide a deeper YUV format such as YU16
		CFHD_PIXEL_FORMAT_YU64,			// 16 bit YUV
		CFHD_PIXEL_FORMAT_V210,			// Component Y'CbCr 10-bit 4:2:2
		CFHD_PIXEL_FORMAT_2VUY,			// Component Y'CbCr 8-bit 4:2:2
		CFHD_PIXEL_FORMAT_YUY2,			// Component Y'CbCr 8-bit 4:2:2
		CFHD_PIXEL_FORMAT_B64A,			// ARGB with 16-bits per component
		CFHD_PIXEL_FORMAT_R210,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_DPX0,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_RG30,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_AB10,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_AR10,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_RG48,			// RGB with 16-bits per component
		CFHD_PIXEL_FORMAT_WP13,			// RGB with signed 16-bits per component, white point at 1<<13
		CFHD_PIXEL_FORMAT_W13A,			// RGBA with signed 16-bits per component, white point at 1<<13
		CFHD_PIXEL_FORMAT_BGRA,			// RGBA 8-bit 4:4:4:4 inverted
		CFHD_PIXEL_FORMAT_BGRa,			// RGBA 8-bit 4:4:4:4
		CFHD_PIXEL_FORMAT_RG24,			// RGB 8-bit 4:4:4
	};
	const static int outputFormatYUV422Length = sizeof(outputFormatYUV422)/sizeof(outputFormatYUV422[0]);

	// Best output formats for video encoded as RGB 4:4:4
	static CFHD_PixelFormat outputFormatRGB444[] =
	{
		CFHD_PIXEL_FORMAT_B64A,			// ARGB with 16-bits per component
		CFHD_PIXEL_FORMAT_R210,			// RGB with 10-bits per
		CFHD_PIXEL_FORMAT_DPX0,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_RG30,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_AB10,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_AR10,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_RG48,			// RGB with 16-bits per component
		CFHD_PIXEL_FORMAT_WP13,			// RGB with signed 16-bits per component, white point at 1<<13
		CFHD_PIXEL_FORMAT_W13A,			// RGBA with signed 16-bits per component, white point at 1<<13
		CFHD_PIXEL_FORMAT_BGRA,			// RGBA 8-bit 4:4:4:4 inverted
		CFHD_PIXEL_FORMAT_BGRa,			// RGBA 8-bit 4:4:4:4
		CFHD_PIXEL_FORMAT_RG24,			// RGB 8-bit 4:4:4
		CFHD_PIXEL_FORMAT_V210,			// Component Y'CbCr 10-bit 4:2:2
		CFHD_PIXEL_FORMAT_2VUY,			// Component Y'CbCr 8-bit 4:2:2
		CFHD_PIXEL_FORMAT_YUY2,			// Component Y'CbCr 8-bit 4:2:2
	};
	const static int outputFormatRGB444Length = sizeof(outputFormatRGB444)/sizeof(outputFormatRGB444[0]);

	// Best output formats for raw Bayer pixel data
	static CFHD_PixelFormat outputFormatBayer[] =
	{
#if 0 // Hack for Resolve
		// no bayer formats
#else
		CFHD_PIXEL_FORMAT_BYR2,			// Raw Bayer pixel data
		CFHD_PIXEL_FORMAT_BYR4,			// Raw Bayer pixel data
#endif
		CFHD_PIXEL_FORMAT_B64A,			// ARGB with 16-bits per component
		CFHD_PIXEL_FORMAT_R210,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_DPX0,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_RG30,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_AB10,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_AR10,			// RGB with 10-bits per component
		CFHD_PIXEL_FORMAT_RG48,			// RGB with 16-bits per component
		CFHD_PIXEL_FORMAT_WP13,			// RGB with signed 16-bits per component, white point at 1<<13
		CFHD_PIXEL_FORMAT_W13A,			// RGBA with signed 16-bits per component, white point at 1<<13
		CFHD_PIXEL_FORMAT_BGRA,			// RGBA 8-bit 4:4:4:4 inverted
		CFHD_PIXEL_FORMAT_BGRa,			// RGBA 8-bit 4:4:4:4
		CFHD_PIXEL_FORMAT_RG24,			// RGB 8-bit 4:4:4
		CFHD_PIXEL_FORMAT_V210,			// Component Y'CbCr 10-bit 4:2:2
		CFHD_PIXEL_FORMAT_2VUY,			// Component Y'CbCr 8-bit 4:2:2
		CFHD_PIXEL_FORMAT_YUY2,			// Component Y'CbCr 8-bit 4:2:2
	};
	const static int outputFormatBayerLength = sizeof(outputFormatBayer)/sizeof(outputFormatBayer[0]);

	// Check the input arguments for errors
	if (outputFormatArray == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	ENCODED_FORMAT encodedFormat = ENCODED_FORMAT_YUV_422;		//Set default for initization without a valid sample
	if (samplePtr && sampleSize > 0)
	{
		// Parse the sample header to determine the encoded format
		encodedFormat = GetEncodedFormat(samplePtr, sampleSize);
	}

	CFHD_PixelFormat *outputFormatList = NULL;
	int outputFormatLength = 0;

	// Select the list of output formats that corresponds to the encoded format
	switch (encodedFormat)
	{
	case ENCODED_FORMAT_YUV_422:
		outputFormatList = outputFormatYUV422;
		outputFormatLength = outputFormatYUV422Length;
		break;

	case ENCODED_FORMAT_RGB_444:
	case ENCODED_FORMAT_RGBA_4444:
		outputFormatList = outputFormatRGB444;
		outputFormatLength = outputFormatRGB444Length;
		break;

	case ENCODED_FORMAT_BAYER:
		outputFormatList = outputFormatBayer;
		outputFormatLength = outputFormatBayerLength;
		break;

	default:
		return CFHD_ERROR_BADFORMAT;
		break;
	}

	// Copy the best formats to the output array
	if (outputFormatLength > outputFormatArrayLength) {
		outputFormatLength = outputFormatArrayLength;
	}

	for (int index = 0; index < outputFormatLength; index++)
	{
		outputFormatArray[index] = outputFormatList[index];
	}

	// Return the number of output formats copied into the output array
	if (actualOutputFormatCountOut != NULL) {
		*actualOutputFormatCountOut = outputFormatLength;
	}

	return CFHD_ERROR_OKAY;
}

bool IsSameFormat(DECODED_FORMAT decodedFormat, CFHD_PixelFormat outputFormat)
{
	struct formatEntry
	{
		DECODED_FORMAT decodedFormat;
		CFHD_PixelFormat outputFormat;
	}
	formatTable[] =
	{
		{DECODED_FORMAT_B64A, CFHD_PIXEL_FORMAT_B64A},
		{DECODED_FORMAT_R210, CFHD_PIXEL_FORMAT_R210},
		{DECODED_FORMAT_DPX0, CFHD_PIXEL_FORMAT_DPX0},
		{DECODED_FORMAT_RG30, CFHD_PIXEL_FORMAT_RG30},
		{DECODED_FORMAT_AB10, CFHD_PIXEL_FORMAT_AB10},
		{DECODED_FORMAT_AR10, CFHD_PIXEL_FORMAT_AR10},
		{DECODED_FORMAT_RG48, CFHD_PIXEL_FORMAT_RG48},
		{DECODED_FORMAT_WP13, CFHD_PIXEL_FORMAT_WP13},
		{DECODED_FORMAT_W13A, CFHD_PIXEL_FORMAT_W13A},
		{DECODED_FORMAT_BYR2, CFHD_PIXEL_FORMAT_BYR2},
		{DECODED_FORMAT_BYR4, CFHD_PIXEL_FORMAT_BYR4},
		{DECODED_FORMAT_UYVY, CFHD_PIXEL_FORMAT_2VUY},
		{DECODED_FORMAT_YUYV, CFHD_PIXEL_FORMAT_YUY2},
		{DECODED_FORMAT_V210, CFHD_PIXEL_FORMAT_V210},
		{DECODED_FORMAT_R408, CFHD_PIXEL_FORMAT_R408},
		{DECODED_FORMAT_V408, CFHD_PIXEL_FORMAT_V408},
		{DECODED_FORMAT_RGB32, CFHD_PIXEL_FORMAT_BGRA},
		{DECODED_FORMAT_RGB32_INVERTED, CFHD_PIXEL_FORMAT_BGRa},
		{DECODED_FORMAT_RGB24, CFHD_PIXEL_FORMAT_RG24},
		{DECODED_FORMAT_YU64, CFHD_PIXEL_FORMAT_YU64},
		{DECODED_FORMAT_YUYV, CFHD_PIXEL_FORMAT_YUYV},
		{DECODED_FORMAT_CT_UCHAR, CFHD_PIXEL_FORMAT_CT_UCHAR},
		{DECODED_FORMAT_CT_SHORT, CFHD_PIXEL_FORMAT_CT_SHORT},
		{DECODED_FORMAT_CT_10Bit_2_8, CFHD_PIXEL_FORMAT_CT_10BIT_2_8},
		{DECODED_FORMAT_CT_SHORT_2_14, CFHD_PIXEL_FORMAT_CT_SHORT_2_14},
		{DECODED_FORMAT_CT_USHORT_10_6, CFHD_PIXEL_FORMAT_CT_USHORT_10_6},

		//TODO: Add more entries to the format equivalence table
	};

	const int tableLength = sizeof(formatTable)/sizeof(formatTable[0]);

	for (int i = 0; i < tableLength; i++)
	{
		if (decodedFormat == formatTable[i].decodedFormat &&
			outputFormat == formatTable[i].outputFormat) {
				return true;
			}
	}

	return false;
}

/*!
	@brief Now obselete, this was used to license the commerical version.
*/
CFHD_Error
CSampleDecoder::SetLicense(const unsigned char *licenseKey)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	if (m_decoder != NULL) {
		InitDecoderLicense(m_decoder, licenseKey);
	}
	else
	{
		errorCode = CFHD_ERROR_LICENSING;
#ifdef _WIN32
		OutputDebugString("m_decoder is NULL, can't set the license");
#endif
	}

	return errorCode;
}

/*!
	@brief Return true if the decoder will be reinitialized

	This predicate is used to determine whether a call to PrepareDecoder
	will cause the decoder to be released and reallocated which would cause
	state information required for decoding non-key frames to be lost.  It is
	critical that this predicate return true if there is any chance that the
	decoder may be reinitialized by a subsequent call to PrepareDecoder.

	There are several calculations in PrepareDecoder leading up to the test that
	determines whether the decoder must be reinitialized.  It is very important
	that this routine be modified if any changes are made to the corresponding
	calculations in PrepareDecoder.

	PrepareDecoder parses a sample to determine the encoding format and the
	encoded frame dimensions, but this routine assumes that the format and
	dimensions have not changed since the last call to PrepareDecoder.

	This predicate was written to solve a green frame problem in the asynchronous
	AVI file importer that occurred when the output format requested by Premiere
	was changed immediately before a non-key frame.  The decoder was released and
	reallocated, discarding the state information required to decode the frame.

	This predicate has only been tested with the asynchronous AVI file importer
	which passes zero for the output width and height to both this routine and
	PrepareDecoder, so the case where the output dimensions are non-zero has not
	been tested.
*/
bool CSampleDecoder::IsDecoderObsolete(int outputWidth,
									   int outputHeight,
									   CFHD_PixelFormat outputFormat,
									   int decodedResolution)
{
	// Has the decoder been allocated and initialized?
	if (m_decoder)
	{
		// The encoded format should have already been determined
		assert(m_encodedFormat != ENCODED_FORMAT_UNKNOWN);

		DECODED_FORMAT decodedFormat = DECODED_FORMAT_UNSUPPORTED;
		uint32_t decodedPixelSize = 0;

		// Translate the encoded and output pixel formats into the decoded format
		GetDecodedFormat(m_encodedFormat, outputFormat, &decodedFormat, &decodedPixelSize);

		// The encoded frame dimensions should have already been determined
		assert(m_encodedWidth > 0 && m_encodedHeight > 0);

		// Use the encoded dimensions if the output dimensions were not specified
		if (outputWidth == 0 || outputHeight == 0)
		{
			outputWidth = m_encodedWidth;
			outputHeight = m_encodedHeight;

			// Reduce the output resolution if the decoded resolution was specified
			if (decodedResolution > CFHD_DECODED_RESOLUTION_FULL)
			{
				switch (decodedResolution)
				{
				case CFHD_DECODED_RESOLUTION_HALF:
					outputWidth /= 2;
					outputHeight /= 2;
					break;

				case CFHD_DECODED_RESOLUTION_QUARTER:
					outputWidth /= 4;
					outputHeight /= 4;
					break;

				default:
					// Do not change the output dimensions
					break;
				}
			}
		}

		// The decoded resolution variable is reused to hold the codec internal resolution

#if _SCALING
		// Compute the decoded resolution for arbitrary frame scaling
		decodedResolution = DecodedScale(encodedWidth, encodedHeight, outputWidth, outputHeight);
		if (decodedResolution == DECODED_RESOLUTION_UNSUPPORTED)
		{
			// Cannot decode and scale this combination of encoded and output dimensions
			assert(0);
			errorCode = CFHD_ERROR_BADSCALING;
			goto finish;
		}
#else
		// Compute the decoded resolution without arbitrary frame scaling
		decodedResolution = DecodedResolution(m_encodedWidth, m_encodedHeight, outputWidth, outputHeight);
		if (decodedResolution == DECODED_RESOLUTION_UNSUPPORTED)
		{
			// Force the output dimensions to be the encoded dimensions
			outputWidth = m_encodedWidth;
			outputHeight = m_encodedHeight;
			decodedResolution = DECODED_RESOLUTION_FULL;
		}
#endif
        // Return true if the decoding parameters have changed
		return (decodedFormat != m_decodedFormat ||
				decodedResolution != m_decodedResolution);
	}

	// The decoder will have to be initialized if it has not already been allocated
	return true;
}


CFHD_Error
CSampleDecoder::GetSampleInfo(
					void *samplePtr,
					size_t sampleSize,
					CFHD_SampleInfoTag tag,
					void *value,
					size_t buffer_size)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;
	bool result;

	BITSTREAM bitstream;
	InitBitstreamBuffer(&bitstream, (uint8_t  *)samplePtr, sampleSize, BITSTREAM_ACCESS_READ);

	// Clear the fields in the sample header
	SAMPLE_HEADER header;
	memset(&header, 0, sizeof(SAMPLE_HEADER));

	result = ::ParseSampleHeader(&bitstream, &header);
	if (result)
	{
		// The frame dimensions must be obtained from the encoded sample
		if (header.width == 0 || header.height == 0)
		{
			assert(0);
			errorCode = CFHD_ERROR_BADSAMPLE;
			goto finish;
		}

		switch(tag)
		{
			case CFHD_SAMPLE_SDK_VERSION:
				*((int *)value) = (kCFHDCodecVersionMajor << 16) | (kCFHDCodecVersionMinor << 8)
								| (kCFHDCodecVersionRevision << 0);
				break;
			case CFHD_SAMPLE_ENCODE_VERSION:
				*((int *)value) = header.encoder_version;
				break;
			case CFHD_SAMPLE_INFO_CHANNELS:
				if(buffer_size >= 4)
					*((int *)value) = header.videoChannels;
				break;
			case CFHD_SAMPLE_DISPLAY_WIDTH:
				if(buffer_size >= 4)
					*((int *)value) = header.width;
				break;
			case CFHD_SAMPLE_DISPLAY_HEIGHT:
				if(buffer_size >= 4)
					*((int *)value) = header.display_height;
				break;
			case CFHD_SAMPLE_KEY_FRAME:
				if(buffer_size >= 4)
					*((int *)value) = header.key_frame;
				break;
			case CFHD_SAMPLE_ENCODED_FORMAT:
				if(buffer_size >= 4)
				{
					switch(header.encoded_format)
					{
					default:
					case ENCODED_FORMAT_YUV_422:
						*((int *)value) = CFHD_ENCODED_FORMAT_YUV_422;
						break;
					case ENCODED_FORMAT_BAYER:
						*((int *)value) = CFHD_ENCODED_FORMAT_BAYER;
						break;
					case ENCODED_FORMAT_RGB_444:
						*((int *)value) = CFHD_ENCODED_FORMAT_RGB_444;
						break;
					case ENCODED_FORMAT_RGBA_4444:
						*((int *)value) = CFHD_ENCODED_FORMAT_RGBA_4444;
						break;
					}
				}
				break;
			case CFHD_SAMPLE_PROGRESSIVE:
				if(buffer_size >= 4)
					*((int *)value) = header.hdr_progressive;
				break;
			default:
				errorCode = CFHD_ERROR_UNKNOWN_TAG;
				break;
		}
	}

finish:
	return errorCode;
}

CFHD_Error
CSampleDecoder::PrepareDecoder(int outputWidth,
							   int outputHeight,
							   CFHD_PixelFormat outputFormat,
							   int decodedResolution,
							   CFHD_DecodingFlags decodingFlags,
							   void *samplePtr,
							   size_t sampleSize,
							   int *actualWidthOut,
							   int *actualHeightOut,
							   CFHD_PixelFormat *actualFormatOut)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;
	int encodedWidth = 0;		// Encoded dimensions
	int encodedHeight = 0;

	int decodedWidth = 0;		// Decoded dimensions
	int decodedHeight = 0;

	//int sourceVideoChannels = decodedResolution; // used for metadata image development of uncompressed
	//CFHD_PixelFormat inputFormat = (CFHD_PixelFormat)decodingFlags; // used for metadata image development of uncompressed

	ENCODED_FORMAT encodedFormat = ENCODED_FORMAT_UNKNOWN;
	DECODED_FORMAT decodedFormat = DECODED_FORMAT_UNSUPPORTED;

	// Catch any errors in the decoder
	try
	{
		// special case for thumbnail decodes...
		if(decodedResolution == CFHD_DECODED_RESOLUTION_THUMBNAIL)
		{
			size_t wOut, hOut, aOut;
			if(!GetThumbnailInfo(samplePtr, sampleSize, 0, &wOut, &hOut, &aOut))
				return CFHD_ERROR_INTERNAL;

			if(actualWidthOut)
				*actualWidthOut = (int)wOut;
			if(actualHeightOut)
				*actualHeightOut = (int)hOut;
			if(actualFormatOut)
				*actualFormatOut = CFHD_PIXEL_FORMAT_BGRA; // currently *hardwired thumbail* output in BGRA format

			m_outputWidth = (int)wOut;
			m_outputHeight = (int)hOut;
			m_preparedForThumbnails = true;

			m_decodingFlags = decodingFlags;

			return CFHD_ERROR_OKAY;
		}
		else
		{
			// otherwise, as we can call prepare on an already prepared decoder, get
			// these values back to initialized defaults
			m_outputWidth = 0;
			m_outputHeight = 0;
			m_preparedForThumbnails = false;
		}

		//int decodedResolution = 0;

		// The size of the decoded pixel is used to allocate the decoding buffer
		uint32_t decodedPixelSize = 0;	// Size of the pixel in bytes (all components)

		bool result;

		if (samplePtr != NULL && sampleSize > 0)
		{
			// Initialize a bitstream to the sample data
			BITSTREAM bitstream;
			InitBitstreamBuffer(&bitstream, (uint8_t  *)samplePtr, sampleSize, BITSTREAM_ACCESS_READ);

			// Clear the fields in the sample header
			SAMPLE_HEADER header;
			memset(&header, 0, sizeof(SAMPLE_HEADER));

			// Decode the sample header
			result = ::ParseSampleHeader(&bitstream, &header);
			if (!result)
			{
				// The frame dimensions must be obtained from the encoded sample
				if (header.width == 0 || header.height == 0)
				{
					assert(0);
					errorCode = CFHD_ERROR_BADSAMPLE;
					goto finish;
				}

				// Try to fill in missing information with default values
				if (header.encoded_format == ENCODED_FORMAT_UNKNOWN)
				{
					// The encoded format is probably YUV 4:2:2
					header.encoded_format = ENCODED_FORMAT_YUV_422;
				}

				// It is okay if the original input format is not known
			}

			if(header.key_frame == 0 && m_encodedWidth > 0 && m_encodedHeight > 0)
			{
				// The size information was wrong in older codecs
				encodedWidth = m_encodedWidth;
				encodedHeight = m_encodedHeight;
				encodedFormat = header.encoded_format;
			}
			else
			{
				// Use the frame dimensions from the encoded sample
				encodedWidth = header.width;
				encodedHeight = header.height;
				encodedFormat = header.encoded_format;
			}

			// Should have determined the encoded dimensions and format
			assert(encodedWidth > 0 && encodedHeight > 0 && encodedFormat != ENCODED_FORMAT_UNKNOWN);
			if (! (encodedWidth > 0 && encodedHeight > 0 && encodedFormat != ENCODED_FORMAT_UNKNOWN)) {
				return CFHD_ERROR_BADFORMAT;
			}
		}
		else
		{
			// Assume that the encoded dimensions are the same as the output dimensions
			encodedWidth = outputWidth;
			encodedHeight = outputHeight;

			decodedResolution = CFHD_DECODED_RESOLUTION_FULL;
			decodingFlags = 0;

			encodedFormat = ENCODED_FORMAT_RGB_444; // yets assume 4:4:4 for all image development

			decodingFlags = CFHD_DECODING_FLAGS_INTERNAL_ONLY; // used to indicate the image development decode type
		}


#if (0 && LOGFILE)
		if (m_logfile) {
			fprintf(m_logfile, "CFHD_DecompressorBeginBand, glob: 0x%08X, encoded width: %d, height: %d, output width: %d, height: %d\n",
					(unsigned int)glob, encodedWidth, encodedHeight, outputWidth, outputHeight);
		}
#endif

		// Translate the encoded and output pixel formats into the decoded format
		GetDecodedFormat(encodedFormat, outputFormat, &decodedFormat, &decodedPixelSize);

#if (0 && TRACE)
		char message[256];
		sprintf(message, "Encoded format: %d, output format: %c%c%c%c, decoded format: %d, pixel size: %d\n",
			encodedFormat, FOURCC(outputFormat), decodedFormat, decodedPixelSize);
		OutputDebugString(message);
#endif

		// Save the encoded format that was used to compute the decoded format
		m_encodedFormat = encodedFormat;

		// Use the encoded dimensions if the output dimensions were not specified
		if (outputWidth == 0 || outputHeight == 0)
		{
			outputWidth = encodedWidth;
			outputHeight = encodedHeight;

			// Reduce the output resolution if the decoded resolution was specified
			if (decodedResolution > CFHD_DECODED_RESOLUTION_FULL)
			{
				switch (decodedResolution)
				{
				case CFHD_DECODED_RESOLUTION_HALF:
					outputWidth /= 2;
					outputHeight /= 2;
					break;

				case CFHD_DECODED_RESOLUTION_QUARTER:
					outputWidth /= 4;
					outputHeight /= 4;
					break;

				default:
					// Do not change the output dimensions
					break;
				}
			}
		}
		else if (decodingFlags & CFHD_DECODING_FLAGS_USE_RESOLUTION)
		{
			switch (decodedResolution)
			{
			case CFHD_DECODED_RESOLUTION_HALF:
				outputWidth /= 2;
				outputHeight /= 2;
				break;

			case CFHD_DECODED_RESOLUTION_QUARTER:
				outputWidth /= 4;
				outputHeight /= 4;
				break;

			default:
				// Do not change the output dimensions
				break;
			}
		}

		// The decoded resolution variable is reused to hold the codec internal resolution

#if _SCALING
		// Compute the decoded resolution for arbitrary frame scaling
		decodedResolution = DecodedScale(encodedWidth, encodedHeight, outputWidth, outputHeight);
		if (decodedResolution == DECODED_RESOLUTION_UNSUPPORTED)
		{
			// Cannot decode and scale this combination of encoded and output dimensions
			assert(0);
			errorCode = CFHD_ERROR_BADSCALING;
			goto finish;
		}
#else
		// Compute the decoded resolution without arbitrary frame scaling
		decodedResolution = DecodedResolution(encodedWidth, encodedHeight, outputWidth, outputHeight);
		if (decodedResolution == DECODED_RESOLUTION_UNSUPPORTED)
		{
			// Force the output dimensions to be the encoded dimensions
			outputWidth = encodedWidth;
			outputHeight = encodedHeight;
			decodedResolution = DECODED_RESOLUTION_FULL;

			//TODO: Use the decoded resolution argument to adjust the output dimensions
		}
#endif

		if (decodedFormat == DECODED_FORMAT_UNSUPPORTED) {
			errorCode = CFHD_ERROR_BADFORMAT;
			goto finish;
		}

#if LOGFILE
		if (m_logfile) {
			char formatString[FOURCC_STRING_LENGTH];
			ConvertFourccToString(pixelFormat, formatString);
			fprintf(m_logfile, "CFHD_DecompressorBeginBand, glob: 0x%08X, pixel format: %s, decoded format: %d, decoded resolution: %d\n",
				(unsigned int)glob, formatString, decodedFormat, decodedResolution);
		}
#endif

		// Has the decoder been allocated and initialized?
		if (m_decoder != NULL)
		{
			// Have the decoding parameters changed?
			if (encodedWidth != m_encodedWidth   ||
				encodedHeight != m_encodedHeight ||
				decodedFormat != m_decodedFormat ||
				decodedResolution != m_decodedResolution)
			{
				// Safest method is to destroy the decoder and let it be rebuilt
				DecodeRelease(m_decoder, NULL, 0);
				Free(m_decoder);
				m_decoder = NULL;
			}
		}

		// Allocate the decoder
		if (m_decoder == NULL)
		{
			bool result;

#if (0 && TRACE)
			char message[256];
			sprintf_s(message, "CSampleDecoder::PrepareDecoder allocating decoder size: %d\n", sizeof(DECODER));
			OutputDebugString(message);
#endif
			m_decoder = (DECODER *)Alloc(DecoderSize());
			assert(m_decoder != NULL);
			if (! (m_decoder != NULL)) {
				errorCode = CFHD_ERROR_OUTOFMEMORY;
				goto finish;
			}

			//TODO: Fix bug in InitDecoder which sets the CPU parameters before clearing the decoder data structure
			memset(m_decoder, 0, DecoderSize());

#if _ALLOCATOR
			// Initialize the decoder using a memory allocator
			ALLOCATOR *allocator = (ALLOCATOR *)m_allocator;
			result = DecodeInit(allocator, m_decoder, encodedWidth, encodedHeight,
								decodedFormat, decodedResolution, m_logfile);
#else
			// Initialize the decoder using the default memory allocator
			result = DecodeInit(m_decoder, encodedWidth, encodedHeight,
								decodedFormat, decodedResolution, m_logfile);
#endif
			if (!result) {
				assert(0);
				errorCode = CFHD_ERROR_CODEC_ERROR;
				goto finish;
			}

			// Apply the license key to this sample decoder
			//SetLicense(m_license);
			InitDecoderLicense(m_decoder, m_license);

			// Assume video systems 709 color space
			SetDecoderColorFlags(m_decoder, COLOR_SPACE_CG_709);

			// Remember the dimensions and format used for initializing the decoder
			m_encodedWidth = encodedWidth;
			m_encodedHeight = encodedHeight;
			m_decodedFormat = decodedFormat;

			// Remember the decoded resolution used for initializing the decoder
			m_decodedResolution = (DECODED_RESOLUTION)decodedResolution;

			// Assume that the frame will be rendered
			//m_willRender = true;

#if _SCALING
			// Compute the dimensions of the decoded frame
			ComputeDecodedDimensions(encodedWidth, encodedHeight, decodedResolution,
									 &decodedWidth, &decodedHeight);
#else
			// Assume that the decoded dimensions are the same as the output dimensions
			decodedWidth = outputWidth;
			decodedHeight = outputHeight;
#endif
			//Note: The decoded dimensions are not the same as the output dimensions if the decoded frame is scaled

			// Remember the decoded dimensions
			m_decodedWidth = decodedWidth;
			m_decodedHeight = decodedHeight;
		}
#if 1
		// Allocate the buffer for the decoded frame
		if (!IsSameFormat(decodedFormat, outputFormat) && m_decodedFrameBuffer == NULL)
		{
			int widthRoundedUp;
			int heightRoundedUp;
			//size_t pixelSize;
			int32_t decodedRowSize;
			uint32_t decodedFrameSize;
			//int rowBytes;

			// Compute the aligned dimensions
			widthRoundedUp = (int)Align16(decodedWidth);
			heightRoundedUp = (int)Align16(decodedHeight);

			decodedRowSize = (int32_t)GetFramePitch(widthRoundedUp, outputFormat);
			decodedFrameSize = heightRoundedUp * decodedRowSize;

			assert(decodedRowSize > 0 && decodedFrameSize > 0);
			if (! (decodedRowSize > 0 && decodedFrameSize > 0)) {
				errorCode = CFHD_ERROR_CODEC_ERROR;
				goto finish;
			}

			// Allocate an aligned buffer for the decoded frame
			//m_decodedFrameBuffer = (char *)_mm_malloc(decodedFrameSize, 16);
			m_decodedFrameBuffer = (char *)AlignAlloc(decodedFrameSize, 16);
			assert(m_decodedFrameBuffer != NULL);
			if (! (m_decodedFrameBuffer != NULL)) {
				errorCode = CFHD_ERROR_OUTOFMEMORY;
				goto finish;
			}

			// Remember the size and stride of the decoding buffer
			m_decodedFrameSize = decodedFrameSize;
			m_decodedFramePitch = decodedRowSize;
		}
#endif
		// Remember the output dimensions and format
		m_outputWidth = outputWidth;
		m_outputHeight = outputHeight;
		m_outputFormat = outputFormat;

		// Save the decoding flags for use when actually decoding a sample
		m_decodingFlags = decodingFlags;

		// Return the actual output dimensions and format
		if (actualWidthOut != NULL) {
			*actualWidthOut = outputWidth;
		}

		if (actualHeightOut != NULL) {
			*actualHeightOut = outputHeight;
		}

		if (actualFormatOut != NULL) {
			*actualFormatOut = outputFormat;
		}
#if LOGFILE
		if (m_logfile) {
			fprintf(m_logfile, "CFHD_DecompressorBeginBand, glob: 0x%08X, key frame: %d\n",
					(unsigned int)glob, keyFrame);
		}
#endif
	}
	catch (...)
	{
#if _WIN32
		char message[256];
		sprintf_s(message, sizeof(message), "CSampleDecoder::PrepareDecoder caught internal codec error\n");
		OutputDebugString(message);
#endif
		return CFHD_ERROR_INTERNAL;
	}

finish:
	if(errorCode) // return which we can
	{
		// return the actual output dimensions and format
		if (actualWidthOut != NULL) {
			*actualWidthOut = outputWidth;
		}

		if (actualHeightOut != NULL) {
			*actualHeightOut = outputHeight;
		}

		if (actualFormatOut != NULL) {
			switch (encodedFormat)
			{
			case ENCODED_FORMAT_YUV_422:
				*actualFormatOut = CFHD_PIXEL_FORMAT_V210;
				break;

			case ENCODED_FORMAT_RGB_444:
				*actualFormatOut = CFHD_PIXEL_FORMAT_RG48;
				break;

			case ENCODED_FORMAT_RGBA_4444:
				*actualFormatOut = CFHD_PIXEL_FORMAT_B64A;
				break;

			case ENCODED_FORMAT_BAYER:
#if 0 // Hack for Resolve
				*actualFormatOut = CFHD_PIXEL_FORMAT_RG48;
#else
				*actualFormatOut = CFHD_PIXEL_FORMAT_BYR4;
#endif
				break;

			default:
				*actualFormatOut = CFHD_PIXEL_FORMAT_UNKNOWN;
				break;
			}
		}
	}

	return errorCode;
}

/*!
	@brief Parse the sample header

	The information in the sample header is stored in an instance of the
	CFHD_SampleHeader class to isolate the routines that call this method
	from the inconsistencies in the encoded sample header.  This method
	calls helper methods to compute consistent values for the field type
	and other information.  The CFHD_SampleHeader class defines access methods
	that provide an extra layer of isolation from changes and inconsistencies
	in the sample header information.
*/

CFHD_Error
CSampleDecoder::ParseSampleHeader(void *samplePtr,
								  size_t sampleSize,
								  CFHD_SampleHeader *sampleHeader)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Catch any errors in the decoder
	try
	{
		CFHD_EncodedFormat encodedFormat = CFHD_ENCODED_FORMAT_YUV_422;
		CFHD_FieldType fieldType = CFHD_FIELD_TYPE_UNKNOWN;

		// Initialize a bitstream to the sample data
		BITSTREAM bitstream;
		InitBitstreamBuffer(&bitstream, (uint8_t  *)samplePtr, sampleSize, BITSTREAM_ACCESS_READ);

		// Clear the fields in the sample header
		SAMPLE_HEADER header;
		memset(&header, 0, sizeof(SAMPLE_HEADER));

		// Decode the sample header
		bool result = ::ParseSampleHeader(&bitstream, &header);
		if (!result)
		{
			// The frame dimensions must be obtained from the encoded sample
			if (header.width == 0 || header.height == 0)
			{
				assert(0);
				errorCode = CFHD_ERROR_BADSAMPLE;
				goto finish;
			}

			// Try to fill in missing information with default values
			if (header.encoded_format == ENCODED_FORMAT_UNKNOWN)
			{
				// The encoded format is probably YUV 4:2:2
				header.encoded_format = ENCODED_FORMAT_YUV_422;
			}

			// It is okay if the original input format is not known
		}

		// Copy the sample header information to the output

		encodedFormat = EncodedFormat(header.encoded_format);
		sampleHeader->SetEncodedFormat(encodedFormat);

		fieldType = FieldType(&header);
		sampleHeader->SetFieldType(fieldType);

		sampleHeader->SetFrameSize(header.width, header.height);
	}
	catch (...)
	{
#if _WIN32
		char message[256];
		sprintf_s(message, sizeof(message), "CSampleDecoder::PrepareDecoder caught internal codec error\n");
		OutputDebugString(message);
#endif
		return CFHD_ERROR_INTERNAL;
	}

finish:

	return errorCode;
}

int fileexnum = 0;
CFHD_Error
CSampleDecoder::DecodeSample(void *samplePtr,
							 size_t sampleSize,
							 void *outputBuffer,
							 int outputPitch)
{
	// Catch any errors in the decoder
	try
	{
		// short circuit this if this decoder was prepared for thumbnails
		if(m_preparedForThumbnails)
		{
			// REMEMBER: we are currently hardwired to create CFHD_PIXEL_FORMAT_BGRA thumbnail images

			// for thumbnails, if we are set to ignore output, do nothing?
			if(m_decodingFlags&CFHD_DECODING_FLAGS_IGNORE_OUTPUT)
				return CFHD_ERROR_OKAY;

			// here we can test only the pitch and if the pitch is off, fail
			if (m_outputWidth * GetPixelSize(CFHD_PIXEL_FORMAT_BGRA) > outputPitch) {
				return CFHD_ERROR_INVALID_ARGUMENT;
			}

			uint32_t rawThumbnailNumPix = m_outputWidth * m_outputHeight;
			uint32_t * rawThumbnailPix = NULL;

			if(m_allocator)
				rawThumbnailPix = reinterpret_cast<uint32_t*>(::Alloc(m_allocator, rawThumbnailNumPix * sizeof(uint32_t)));
			else
				rawThumbnailPix = new uint32_t [rawThumbnailNumPix];

			if(!rawThumbnailPix)
				return CFHD_ERROR_OUTOFMEMORY;

			if(!GenerateThumbnail(samplePtr, sampleSize, rawThumbnailPix, rawThumbnailNumPix * sizeof(uint32_t), THUMBNAIL_FLAGS_DEFAULT, NULL, NULL, NULL))
			{
				if(m_allocator)
					::Free(m_allocator, rawThumbnailPix);
				else
					delete [] rawThumbnailPix;
				rawThumbnailPix = NULL;
				return CFHD_ERROR_INTERNAL;
			}

			// convert the 10-bit packed thumbnail info returned from GetThumbnail into BGRA
			uint32_t val;
			unsigned char * dest = (unsigned char *)outputBuffer;
			unsigned char * destRowPtr = dest + ((m_outputHeight-1) * outputPitch);
			uint32_t * src = rawThumbnailPix;
			for (int i = 0; i < m_outputHeight; i++)
			{
				for (int j = 0; j < m_outputWidth; j++)
				{
					// get the value
					val = *src++;

					// swap it
					val = (((val&0xFF000000)>>24)|((val&0x00FF0000)>>8)|((val&0x0000FF00)<<8)|((val&0x000000FF)<<24));

					// get rid of our two garbage bits
					val >>= 2;

					// 10-bits B, 10-bits G, 10-bit R
					// are now packed into val

					// B
					dest[0] = val>>2;

					// G
					dest[1] = val>>12;

					// R
					dest[2] = val>>22;

					// a
					dest[3] = 255;

					dest+=4;
				}

				destRowPtr -= outputPitch;
				dest = destRowPtr;
			}

			if(m_allocator)
				::Free(m_allocator, rawThumbnailPix);
			else
				delete [] rawThumbnailPix;
			rawThumbnailPix = NULL;

			return CFHD_ERROR_OKAY;
		}

		//CFHD_Error errorCode = CFHD_ERROR_OKAY;
		uint32_t decoding_flags;
		bool result;

#if LOGFILE
		if (m_logfile) {
			fprintf(m_logfile,
					"DecodeSample, glob: 0x%08X, decodedRowBytes: %d, drpRowBytes: %ld\n",
					(unsigned int)glob, glob->decodedRowBytes, drp->rowBytes);
		}
#endif

		// Check the input arguments
		assert(samplePtr != NULL && sampleSize > 0);
		if (! (samplePtr != NULL && sampleSize > 0)) {
			return CFHD_ERROR_INVALID_ARGUMENT;
		}

		// Must have a valid output buffer if the output is not ignored
		assert((m_decodingFlags & CFHD_DECODING_FLAGS_IGNORE_OUTPUT) ||
			   (outputBuffer != NULL && outputPitch != 0));
		if (! ((m_decodingFlags & CFHD_DECODING_FLAGS_IGNORE_OUTPUT) ||
			   (outputBuffer != NULL && outputPitch != 0))) {
			return CFHD_ERROR_INVALID_ARGUMENT;
		}

		// Initialize a bitstream to the sample data
		BITSTREAM bitstream;
		InitBitstreamBuffer(&bitstream, (uint8_t  *)samplePtr, sampleSize, BITSTREAM_ACCESS_READ);

		// Set the decoding flags
		decoding_flags = ((m_decodingFlags & CFHD_DECODING_FLAGS_IGNORE_OUTPUT) ? 0 : DECODER_FLAGS_RENDER | m_decodingFlags);
		SetDecoderFlags(m_decoder, decoding_flags);

		// Buffer used for the decoded frame
		uint8_t *decodedFrameBuffer;
		int decodedFramePitch;
		bool conversionIsRequired;

		// Can the sample be decoded directly to the output buffer?
		if (
#if _SCALING
			m_decodedWidth != m_outputWidth ||
			m_decodedHeight != m_outputHeight ||
#endif
			IsSameFormat(m_decodedFormat, m_outputFormat)
			)
		{
			// Decode the sample into the output buffer
			decodedFrameBuffer = (uint8_t *)outputBuffer;
			decodedFramePitch = outputPitch;
			conversionIsRequired = false;
		}
		else
		{
			// Decode the sample into a temporary buffer for color conversion and scaling
			decodedFrameBuffer = (uint8_t *)m_decodedFrameBuffer;
			decodedFramePitch = m_decodedFramePitch;
			conversionIsRequired = true;
		}

		// Check that the decoding buffer is valid if the sample is to be fully decoded
		assert(((m_decodingFlags & CFHD_DECODING_FLAGS_IGNORE_OUTPUT) == 0 &&
				decodedFrameBuffer != NULL &&
				decodedFramePitch != 0) ||
				(m_decodingFlags & CFHD_DECODING_FLAGS_IGNORE_OUTPUT) != 0);
		if ( !(((m_decodingFlags & CFHD_DECODING_FLAGS_IGNORE_OUTPUT) == 0 &&
				decodedFrameBuffer != NULL &&
				decodedFramePitch != 0) ||
				(m_decodingFlags & CFHD_DECODING_FLAGS_IGNORE_OUTPUT) != 0)) {
			return CFHD_ERROR_INTERNAL;
		}

		assert(m_decoder != NULL);
		if (! (m_decoder != NULL)) {
			return CFHD_ERROR_INTERNAL;
		}

		try
		{
			// Decode the sample
			//result = ::DecodeSample(m_decoder, &bitstream, (BYTE *)outputBuffer, outputPitch, NULL, NULL);
			result = ::DecodeSample(m_decoder, &bitstream, decodedFrameBuffer, decodedFramePitch, NULL, NULL);
		}
		catch (...)
		{
#if _WIN32
// #if DEBUG
			OutputDebugString("::DecodeSample: Unexpected error");
			char tt[100];
			int err = 0;
			FILE *fp;

			sprintf_s(tt, sizeof(tt), "C:/Cedoc/Logfiles/%04d.cfhd", fileexnum++);

#ifdef _WIN32
			err = fopen_s(&fp, tt, "wb");
#else
			fp = fopen(tt,"wb");
#endif
			if(err == 0 && fp)
			{
				fwrite(bitstream.lpCurrentBuffer, 1, bitstream.dwBlockLength, fp);
				fclose(fp);
			}
// #endif
#endif

			return CFHD_ERROR_CODEC_ERROR;
		}

		if (!result) {
			assert(0);
			return CFHD_ERROR_CODEC_ERROR;
		}

		// Was the frame decoded into a temporary buffer for color conversion and scaling?
		if (conversionIsRequired)
		{
			// Convert and scale the decoded frame into the output buffer
			CopyToOutputBuffer(decodedFrameBuffer, decodedFramePitch, outputBuffer, outputPitch);
		}

		//HACK TODO -- support W13A decodes natively
		if(m_outputFormat == CFHD_PIXEL_FORMAT_W13A)
		{
			if(m_decoder->frame.white_point == 16)
			{
				//convert to 13
				ConvertWhitePoint(decodedFrameBuffer, decodedFramePitch);
			}
		}

		// Indicate that the frame has been decoded
		return CFHD_ERROR_OKAY;
	}
	catch (...)
	{
#if _WIN32
		char message[256];
		sprintf_s(message, sizeof(message), "CSampleDecoder::PrepareDecoder caught internal codec error\n");
		OutputDebugString(message);
#endif
		return CFHD_ERROR_INTERNAL;
	}
}

ENCODED_FORMAT CSampleDecoder::GetEncodedFormat(void *samplePtr,
												size_t sampleSize)
{
	// Initialize a bitstream to the sample data
	BITSTREAM bitstream;
	InitBitstreamBuffer(&bitstream, (uint8_t  *)samplePtr, sampleSize, BITSTREAM_ACCESS_READ);

	// Clear the fields in the sample header
	SAMPLE_HEADER header;
	memset(&header, 0, sizeof(SAMPLE_HEADER));

	// Decode the sample header and return the encoded format
	::ParseSampleHeader(&bitstream, &header);
	return header.encoded_format;
}



CFHD_Error CSampleDecoder::ConvertWhitePoint(void *decodedBuffer, int decodedPitch)
{
	unsigned char *inputRowPtr = (unsigned char *)decodedBuffer;

	for (int row = 0; row < m_decodedHeight; row++)
	{
		unsigned short *inputPtr = (unsigned short *)inputRowPtr;

		// Swap the bytes in each pixel component
		for (int column = 0; column < m_decodedWidth; column++)
		{
			// Copy each of the components with byte swapping
			inputPtr[0] >>= 3;
			inputPtr[1] >>= 3;
			inputPtr[2] >>= 3;
			inputPtr[3] >>= 3;

			inputPtr+=4;
		}

		// Advance to the next input and output rows
		inputRowPtr += decodedPitch;
	}

	return CFHD_ERROR_OKAY;
}


CFHD_Error CSampleDecoder::CopyToOutputBuffer(void *decodedBuffer, int decodedPitch,
											  void *outputBuffer, int outputPitch)
{
	// Enable or disable byte swapping for the b64a output format
	bool byte_swap_flag = false;

	int decodedHeight = m_decodedHeight;

	// Ignore extra lines at the bottom of the frame
	if (m_decodedWidth == m_outputWidth)
	{
		int extraHeight = decodedHeight - m_outputHeight;

		if (0 < extraHeight && extraHeight < 8) {
			decodedHeight = m_outputHeight;
		}
	}

#if _WIN32
	// Do not swap bytes on Windows
	byte_swap_flag = false;
#else
	// Always swap bytes on the Macintosh
	byte_swap_flag = true;
#endif

	if (m_decodedWidth == m_outputWidth && decodedHeight == m_outputHeight)
	{
#if (0 && LOGFILE)
		if (m_logfile) {
			char formatString[FOURCC_STRING_LENGTH];
			ConvertFourccToString(outputFormat, formatString);
			fprintf(glob->logfile, "CopyToOutputBuffer, glob: 0x%08X, converting input format: %d, output format: %s\n",
					(unsigned int)glob, inputFormat, formatString);
		}
#endif
		return ConvertToOutputBuffer(m_decodedFrameBuffer, m_decodedFramePitch, m_decodedFormat,
									 outputBuffer, outputPitch, m_outputFormat,
									 m_decodedWidth, decodedHeight, byte_swap_flag);
	}
	else
	{
#if (0 && LOGFILE)
		if (m_logfile) {
			char formatString[FOURCC_STRING_LENGTH];
			ConvertFourccToString(outputFormat, formatString);
			fprintf(glob->logfile, "ScaleToOutputBuffer, glob: 0x%08X, scaling input format: %d, output format: %s\n",
					(unsigned int)glob, inputFormat, formatString);
		}
#endif
		return ScaleToOutputBuffer(m_decodedFrameBuffer,  m_decodedWidth, decodedHeight, m_decodedFramePitch, m_decodedFormat,
								   outputBuffer, m_outputWidth, m_outputHeight, outputPitch, m_outputFormat,
								   byte_swap_flag);
	}
}


CFHD_Error
CSampleDecoder::SetDecoderOverrides(unsigned char *overrideData, int overrideSize)
{
	// can't do this if the decoder was prepared for thumbnails
	if(m_preparedForThumbnails)
		return CFHD_ERROR_UNEXPECTED;

	DecodeOverrides(m_decoder, overrideData, overrideSize);

	return CFHD_ERROR_OKAY;
}


CFHD_Error
CSampleDecoder::GetThumbnail(void *samplePtr,
		size_t sampleSize,
		void *outputBuffer,
		size_t outputSize,
		size_t flags,
		size_t *retWidth,
		size_t *retHeight,
		size_t *retSize)
{
	if(GenerateThumbnail(
			samplePtr,
			sampleSize,
			outputBuffer,
			outputSize,
			(int)flags,
			retWidth,
			retHeight,
			retSize))
		return CFHD_ERROR_OKAY;
	else
		return CFHD_ERROR_CODEC_ERROR;
}


// Return the dimensions and format of the output frame
CFHD_Error CSampleDecoder::GetFrameFormat(int &width, int &height, CFHD_PixelFormat &format)
{
	width = m_outputWidth;
	height = m_outputHeight;
	format = m_outputFormat;

	return CFHD_ERROR_OKAY;
}

CFHD_Error CSampleDecoder::GetRequiredBufferSize(uint32_t &bytes)
{
	int channels = 1;
	uint32_t active = 0;
	uint32_t mix = 0;

	bytes = 0;

	GetChannelsActive(active);
	GetChannelMix(mix);
	if(active == 3 && mix == 0)
		channels = 2;

	int decodedRowSize =  (int)GetFramePitch(m_decodedWidth, m_outputFormat);
	bytes = m_decodedHeight * decodedRowSize * channels;


	return CFHD_ERROR_OKAY;
}

CFHD_Error CSampleDecoder::ReleaseDecoder()
{
	// Release the decoder
	if (m_decoder) {
		try
		{
			DecodeRelease(m_decoder, NULL, 0);
			Free(m_decoder);
		}
		catch(...)
		{
		//	OutputDebugString("DecodeRelease Exception");
		}
		m_decoder = NULL;
	}

	// Free the buffer allocated for decoding
	ReleaseFrameBuffer();

	return CFHD_ERROR_OKAY;
}

/*!
	@brief Convert the codec internal encoded format to the sample decoder encoded format

	This method returns the default value for the encoded format if it was not present in
	the sample header.  This method should never return the unknown value for the encoded
	format.
*/
CFHD_EncodedFormat CSampleDecoder::EncodedFormat(ENCODED_FORMAT encoded_format)
{
	CFHD_EncodedFormat encodedFormat = CFHD_ENCODED_FORMAT_YUV_422;

	switch (encoded_format)
	{
	case ENCODED_FORMAT_UNKNOWN:
	case ENCODED_FORMAT_YUV_422:
		encodedFormat = CFHD_ENCODED_FORMAT_YUV_422;
		break;

	case ENCODED_FORMAT_RGB_444:
		encodedFormat = CFHD_ENCODED_FORMAT_RGB_444;
		break;

	case ENCODED_FORMAT_RGBA_4444:
		encodedFormat = CFHD_ENCODED_FORMAT_RGBA_4444;
		break;

	case ENCODED_FORMAT_BAYER:
		encodedFormat = CFHD_ENCODED_FORMAT_BAYER;
		break;

	case ENCODED_FORMAT_YUVA_4444:
		encodedFormat = CFHD_ENCODED_FORMAT_YUVA_4444;
		break;

	default:
		assert(0);
		break;
	}

	return encodedFormat;
}

/*!
	@brief Convert the codec internal flags to the sample decoder field type

	The sample header should contain a valid setting for the progressive flag
	since decoding depends on this flag being set correctly so that the first
	transform (spatial versus interlaced) is known to the decoder.

	The progressive flag is one of the sample flags.  The sample flags may not
	be present in the sample if the flags were all zeros, but in that case the
	routine that parses the sample header should clear the progressive flag.
	If the video sample is encoded using a spatial transform, then the sample
	flags must be present in the sample header and the progressive flag must
	be set.  If the sample flags are not present in the sample header, then the
	frame must be interlaced.

	The interlaced flags may provide additional information about the interlaced
	format if the interlaced bit in the interlaced flags is set.  However, the
	interlaced flags can be present in the sample header, but set to all zeros
	even if the video sample is interlaced because the codec always inserts the
	interlaced flags in video sample even if the flags were not properly set.

	The progressive flag always takes precedence over the interlaced flags and the
	information in the interfaced flags should only be used to determine the field
	format if the interlaced bit in the interlaced flags is set.

	The Bayer encoded format is always progressive even if the progressive flag
	is not present in the sample header.
*/
CFHD_FieldType CSampleDecoder::FieldType(SAMPLE_HEADER *header)
{
	CFHD_FieldType fieldType = CFHD_FIELD_TYPE_UNKNOWN;

	// The sample header should specify progressive versus interlaced decoding
	if (header->hdr_progressive)
	{
		return CFHD_FIELD_TYPE_PROGRESSIVE;
	}

	// The Bayer encoded format is always progressive even if the flag is not set
	if (header->encoded_format == ENCODED_FORMAT_BAYER)
	{
		return CFHD_FIELD_TYPE_PROGRESSIVE;
	}

	// Did the optional interlaced flags specify interlaced fields?
	if (header->interlaced_flags & CODEC_FLAGS_INTERLACED)
	{
		// Check the optional interlaced flags for additional field information
		if (header->interlaced_flags & CODEC_FLAGS_FIELD1_FIRST)
		{
			fieldType = CFHD_FIELD_TYPE_UPPER_FIELD_FIRST;
		}
		else
		{
			fieldType = CFHD_FIELD_TYPE_LOWER_FIELD_FIRST;
		}

		//TODO: Use the field dominance and field only flags
	}
	else
	{
		// Assume interlaced fields with the upper field first
		fieldType = CFHD_FIELD_TYPE_UPPER_FIELD_FIRST;
	}

	// Must have set the field type to an interlaced format
	assert(fieldType == CFHD_FIELD_TYPE_UPPER_FIELD_FIRST ||
		   fieldType == CFHD_FIELD_TYPE_LOWER_FIELD_FIRST);

	return fieldType;
}


/****** Implementation of the class factory methods ******/

/*!
	@brief Class factory method for allocating a sample decoder
*/
ISampleDecoder *CSampleDecoder::CreateSampleDecoder(IAllocator *allocator, CFHD_LicenseKey license, FILE *logfile)
{
	CSampleDecoder *pSampleDecoder = NULL;

	// Allocate the sample decoder using the default allocator
	pSampleDecoder = new CSampleDecoder((CFHD_ALLOCATOR *)allocator, license, logfile);

	return dynamic_cast<ISampleDecoder *>(pSampleDecoder);
}
