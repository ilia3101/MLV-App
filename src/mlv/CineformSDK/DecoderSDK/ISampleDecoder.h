/*! @file ISampleDecoder.h

	@brief Interface for the sample decoder class.

	This class is used internally by CineForm software and is not currently
	mentioned in the documentation provided to customers.  Modify this comment
	and add tags for Doxygen to publish this interface in customer documentation.

	The interface uses pure virtual methods to isolate applications that use the
	CineForm decoder from changes to the codec library.  The interface includes
	macros (methods defined in the class declaration) for common calculations
	involving pixel formats.

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

#include "CFHDError.h"
#include "CFHDDecoder.h"
#include "IAllocator.h"


class CFHDDECODER_API ISampleDecoder
{
public:

	// Derived classes must implement a virtual destructor
	virtual ~ISampleDecoder()
	{
	}

	virtual CFHD_Error GetOutputFormats(
		void *samplePtr,
		size_t sampleSize,
		CFHD_PixelFormat *outputFormatArray,
		int outputFormatArrayLength,
		int *actualOutputFormatCountOut) = 0;
	
	virtual CFHD_Error GetSampleInfo(
		void *samplePtr,
		size_t sampleSize,
		CFHD_SampleInfoTag tag,
		void *value,
		size_t buffer_size) = 0;

	virtual CFHD_Error PrepareDecoder(
		int outputWidth,
		int outputHeight,
		CFHD_PixelFormat outputFormat,
		int decodedResolution,
		CFHD_DecodingFlags decodingFlags,
		void *samplePtr,
		size_t sampleSize,
		int *actualWidthOut,
		int *actualHeightOut,
		CFHD_PixelFormat *actualFormatOut) = 0;

	virtual CFHD_Error SetLicense(const unsigned char *license) = 0;

	virtual CFHD_Error ParseSampleHeader(
		void *samplePtr,
		size_t sampleSize,
		CFHD_SampleHeader *sampleHeaderOut) = 0;

	virtual CFHD_Error DecodeSample(
		void *samplePtr,
		size_t sampleSize,
		void *outputBuffer,
		int outputPitch) = 0;

	virtual CFHD_Error GetFrameFormat(int &width, int &height, CFHD_PixelFormat &format) = 0;

	virtual CFHD_Error GetRequiredBufferSize(uint32_t &bytes) = 0;

	virtual CFHD_Error ReleaseDecoder() = 0;

	virtual bool IsDecoderObsolete(int outputWidth,
								   int outputHeight,
								   CFHD_PixelFormat outputFormat,
								   int decodedResolution) = 0;
/*
	// Convenience methods for computing frame dimensions and formats
	static size_t GetFrameSize(int width, int height, CFHD_PixelFormat format)
	{
		size_t framePitch = GetFramePitch(width, format);
		return (height * framePitch);
	}

	static int32_t GetFramePitch(int width, CFHD_PixelFormat format)
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

	static int GetPixelSize(CFHD_PixelFormat format)
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

protected:

	// It is not necessary for applications to call this method directly
	static int V210FramePitch(int width)
	{
		// Force 16 byte alignment
		width = ((width + 47)/48) * 48;

		// The v210 format uses 16 bytes to encode 6 pixels
		int pitch = (width * 8) / 3;

		// Check that the pitch is a multiple of 16 bytes
		assert((pitch & 0x0F) == 0);

		return pitch;
	}
*/
};

#ifdef __cplusplus
extern "C" {
#endif

// Expose the factory function used to instantiate an instance of an ISampleDecoder
CFHDDECODER_API ISampleDecoder *CFHD_CreateSampleDecoder(IAllocator *allocator, CFHD_LicenseKey license, FILE *logfile = NULL);

// Convenience methods for computing frame dimensions and formats
size_t GetFrameSize(int width, int height, CFHD_PixelFormat format);
int32_t GetFramePitch(int width, CFHD_PixelFormat format);
int GetPixelSize(CFHD_PixelFormat format);
int V210FramePitch(int width);

#ifdef __cplusplus
}
#endif
