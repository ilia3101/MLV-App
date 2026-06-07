/*! @file Conversion.cpp

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
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1

#if 0
#include "StdAfx.h"
#else
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <limits.h>
#include <memory.h>
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#endif

#if __APPLE_CC__
//#include <QuickTime/QuickTime.h>
#elif __GNUC__ && !_WIN32
// Use byte swapping functions on Linux
#include <byteswap.h>
#else
#if 0
    #include <ConditionalMacros.h>
	#ifndef _WIN64
		#include <Endian.h>
		#include <ImageCodec.h>
	#endif
#endif
#endif

#define ASSERT(x)	assert(x)

#include "../Common/CFHDError.h"
#include "../Common/CFHDDecoder.h"

#include "../ConvertLib/ConvertLib.h"			// Color conversion routines
//#include "ImageUtilities.h"
#include "Conversion.h"


#include "decoder.h"				// Decoder data structure and entry points

#ifdef _WIN32

#include <stdlib.h>

// Use the byte swapping routines defined in the standard library
#define SwapInt16(x)	_byteswap_ushort(x)
#define SwapInt32(x)	_byteswap_ulong(x)

#if _DEBUG && 0
// The byteswap functions are not defined in the debug DLL runtime libraries
unsigned short _byteswap_ushort(unsigned short x)
{
	int x1 = (x >> 8) & 0xFF;
	int x2 = (x >> 0) & 0xFF;

	return ((x2 << 8) | x1);
}
#endif

#elif __APPLE__

#include "CoreFoundation/CoreFoundation.h"

// Use the byte swapping routines from the Core Foundation framework
#define SwapInt16(x)	_OSSwapInt16(x)
#define SwapInt32(x)	_OSSwapInt32(x)

#else

// Use the byte swapping functions provided by GCC
#include <byteswap.h>
#define SwapInt16(x)	bswap_16(x)
#define SwapInt32(x)	bswap_32(x)

#endif

//TODO: Replace system log output with logfile output
#define SYSLOG (0)

// Convert the input image format to the output format
CFHD_Error ConvertToOutputBuffer(void *inputBuffer, int inputPitch, int inputFormat,
								 void *outputBuffer, int outputPitch, CFHD_PixelFormat outputFormat,
								 int width, int height, int byte_swap_flag)
{
	CFHD_Error error = CFHD_ERROR_OKAY;

#if (0 && SYSLOG)
	fprintf(stderr,
			"ConvertToOutputBuffer width: %d, height: %d, format: %d, output format: %s\n",
			width, height, inputFormat, CStringFromOSType(outputFormat));
#endif

	// Was the frame decoded to the YU64 pixel format?
	if (inputFormat == DECODED_FORMAT_YU64)
	{
		if (outputFormat == CFHD_PIXEL_FORMAT_B64A)
		{
			const bool colorSpaceIs709 = true;
			const bool interleaved = false;

			// Allocate and initialize an image converter for RGB output
			CImageConverterYU64ToRGB converter(colorSpaceIs709, interleaved);

			// Convert YU64 to RGB plus Alpha with 16 bits per channel
			converter.ConvertToBGRA64((unsigned char *)inputBuffer, inputPitch,
									  (unsigned char *)outputBuffer, outputPitch,
									  width, height, byte_swap_flag);

#if 0	//_WIN32	//For Adobe Aftger Effects CS3
			if (byte_swap_flag)
			{
				char *outputRowPtr = (char *)outputBuffer;

				// Copy the decoded frame to the output buffer
				for (int row = 0; row < height; row++)
				{
					unsigned short *outputPtr = (unsigned short *)outputRowPtr;

					// Swap the bytes in each pixel component
					for (int column = 0; column < width; column++)
					{
						// Copy each of the components with byte swapping
						*(outputPtr++) = SwapInt16(*outputPtr);
						*(outputPtr++) = SwapInt16(*outputPtr);
						*(outputPtr++) = SwapInt16(*outputPtr);
						*(outputPtr++) = SwapInt16(*outputPtr);
					}
					// Advance to the next input and output rows
					outputRowPtr += outputPitch;
				}
			}
#endif
		}
#if 0
		else if (outputFormat == k4444YpCbCrA32RPixelFormat)
		{
			const bool colorSpaceIs709 = true;
			const bool interleaved = false;

			// Allocate and initialize an image converter for YUVA output
			CImageConverterYU64ToYUV converter;

			// Convert YU64 to the floating-point format used by Final Cut Pro
			converter.ConvertToFloatYUVA((unsigned char *)inputBuffer, inputPitch,
										 (unsigned char *)outputBuffer, outputPitch,
										 width, height);

			//DumpImageRowYU64((unsigned char *)inputBuffer, width);
			//DumpImageRowFloatYUVA((unsigned char *)outputBuffer, width);
		}
#endif
		else if (outputFormat == CFHD_PIXEL_FORMAT_CT_10BIT_2_8)
		{
			//const bool colorSpaceIs709 = true;
			//const bool interleaved = false;

			// Allocate and initialize an image converter for RGB output
			CImageConverterYU64ToYUV converter;

			// Convert YU64 to RGB plus Alpha with 16 bits per channel
			converter.ConvertToAvid_CbYCrY_10bit_2_8((uint8_t *)inputBuffer, inputPitch,
													 (uint8_t *)outputBuffer, outputPitch,
													 width, height);
		}
		else
		{
			// Unsupported output format
			ASSERT(0);
			error = CFHD_ERROR_BADFORMAT;
		}
	}

	// Was the frame decoded to 8-bit BGRA?
	else if (inputFormat == DECODED_FORMAT_RGB32)
	{
		switch (outputFormat)
		{
		case CFHD_PIXEL_FORMAT_BGRA:
		case CFHD_PIXEL_FORMAT_BGRa:
		    assert(0);
			//DANREMOVED  ConvertRGB32ToQuickTime((unsigned char *)inputBuffer, inputPitch,
//									(unsigned char *)outputBuffer, outputPitch,
//									width, height, 0
//#ifndef _WIN32
//									, 0, NULL
//#endif
//									);
			break;

		default:
				ASSERT(0);
				error = CFHD_ERROR_BADFORMAT;
				goto bail;
		}
	}

	// Was the frame decoded to 8-bit RGB24?
	else if (inputFormat == DECODED_FORMAT_RGB24)
	{
		switch (outputFormat)
		{
		case CFHD_PIXEL_FORMAT_BGRA:
		case CFHD_PIXEL_FORMAT_BGRa:
			ASSERT(0);
			error = CFHD_ERROR_BADFORMAT;
			goto bail;
			break;

		default:
			ASSERT(0);
			error = CFHD_ERROR_BADFORMAT;
			goto bail;
		}
	}

	// Was the frame decoded to 16-bit ARGB?
	else if (inputFormat == DECODED_FORMAT_B64A)
	{
		if (outputFormat == CFHD_PIXEL_FORMAT_BGRA || outputFormat == CFHD_PIXEL_FORMAT_BGRa)
		{
			// Invert the rows of RGBA and swap red and blue components
			ASSERT(0);
			//DANREMOVED  ConvertQuickTimeARGB64ToBGRA((unsigned char *)inputBuffer, inputPitch,
			//							 (unsigned char *)outputBuffer, outputPitch,
			//							 width, height);
		}
		else
		{
			char *inputRowPtr = (char *)inputBuffer;
			char *outputRowPtr = (char *)outputBuffer;

			// Copy the decoded frame to the output buffer
			for (int row = 0; row < height; row++)
			{
				if (byte_swap_flag)
				{
					unsigned short *inputPtr = (unsigned short *)inputRowPtr;
					unsigned short *outputPtr = (unsigned short *)outputRowPtr;

					// Swap the bytes in each pixel component
					for (int column = 0; column < width; column++)
					{
						// Copy each of the components with byte swapping
						*(outputPtr++) = SwapInt16(*(inputPtr++));
						*(outputPtr++) = SwapInt16(*(inputPtr++));
						*(outputPtr++) = SwapInt16(*(inputPtr++));
						*(outputPtr++) = SwapInt16(*(inputPtr++));
					}
				}
				else
				{
					// Copy the row of pixels without swapping the bytes
					memcpy(outputRowPtr, inputRowPtr, outputPitch);
				}

				// Advance to the next input and output rows
				inputRowPtr += inputPitch;
				outputRowPtr += outputPitch;
			}
		}
	}
#if 0
	// Was the frame decoded to YUV with 4:2:2 sampling?
	else if (inputFormat == DECODED_FORMAT_UYVY)
	{
		switch (outputFormat)
		{
			case k422YpCbCr8PixelFormat:
				ConvertYUVToQuickTime((unsigned char *)inputBuffer, inputPitch,
									  (unsigned char *)outputBuffer, outputPitch,
									  width, height);
				break;

			default:
				ASSERT(0);
				return paramErr;
		}
	}
#endif
	// Was the frame decoded to the Avid 8-bit CbYCrY 4:2:2 format?
	else if (inputFormat == DECODED_FORMAT_CT_UCHAR)
	{
		if (outputFormat == CFHD_PIXEL_FORMAT_CT_UCHAR)
		{
			char *inputRowPtr = (char *)inputBuffer;
			char *outputRowPtr = (char *)outputBuffer;

			// Copy the decoded frame to the output buffer
			for (int row = 0; row < height; row++)
			{
				// Copy the row of pixels without swapping the bytes
				memcpy(outputRowPtr, inputRowPtr, outputPitch);

				// Advance to the next input and output rows
				inputRowPtr += inputPitch;
				outputRowPtr += outputPitch;
			}
		}
		else
		{
			// Unsupported output format
			ASSERT(0);
			error = CFHD_ERROR_BADFORMAT;
		}
	}
	// Was the frame decoded to two planes of 8-bit and 2-bit pixels?
	else if (inputFormat == DECODED_FORMAT_CT_10Bit_2_8)
	{
		if (outputFormat == CFHD_PIXEL_FORMAT_CT_10BIT_2_8)
		{
			char *inputRowPtr = (char *)inputBuffer;
			char *outputRowPtr = (char *)outputBuffer;

			// Copy the decoded frame to the output buffer
			for (int row = 0; row < height; row++)
			{
				// Copy the row of pixels without swapping the bytes
				memcpy(outputRowPtr, inputRowPtr, outputPitch);

				// Advance to the next input and output rows
				inputRowPtr += inputPitch;
				outputRowPtr += outputPitch;
			}
		}
		else
		{
			// Unsupported output format
			ASSERT(0);
			error = CFHD_ERROR_BADFORMAT;
		}
	}
	// Was the frame decoded to the Avid fixed point 2.14 pixel format?
	else if (inputFormat == DECODED_FORMAT_CT_SHORT_2_14)
	{
		if (outputFormat == CFHD_PIXEL_FORMAT_CT_SHORT_2_14)
		{
			char *inputRowPtr = (char *)inputBuffer;
			char *outputRowPtr = (char *)outputBuffer;

			// Copy the decoded frame to the output buffer
			for (int row = 0; row < height; row++)
			{
				// Copy the row of pixels without swapping the bytes
				memcpy(outputRowPtr, inputRowPtr, outputPitch);

				// Advance to the next input and output rows
				inputRowPtr += inputPitch;
				outputRowPtr += outputPitch;
			}
		}
		else
		{
			// Unsupported output format
			ASSERT(0);
			error = CFHD_ERROR_BADFORMAT;
		}
	}
	// Was the frame decoded to the Avid fixed point 10.6 pixel format?
	else if (inputFormat == DECODED_FORMAT_CT_USHORT_10_6)
	{
		if (outputFormat == CFHD_PIXEL_FORMAT_CT_USHORT_10_6)
		{
			char *inputRowPtr = (char *)inputBuffer;
			char *outputRowPtr = (char *)outputBuffer;

			// Copy the decoded frame to the output buffer
			for (int row = 0; row < height; row++)
			{
				// Copy the row of pixels without swapping the bytes
				memcpy(outputRowPtr, inputRowPtr, outputPitch);

				// Advance to the next input and output rows
				inputRowPtr += inputPitch;
				outputRowPtr += outputPitch;
			}
		}
		else
		{
			// Unsupported output format
			ASSERT(0);
			error = CFHD_ERROR_BADFORMAT;
		}
	}
	// Was the frame decoded to the Avid 16-bit signed pixel format?
	else if (inputFormat == DECODED_FORMAT_CT_SHORT)
	{
		if (outputFormat == CFHD_PIXEL_FORMAT_CT_SHORT)
		{
			char *inputRowPtr = (char *)inputBuffer;
			char *outputRowPtr = (char *)outputBuffer;

			// Copy the decoded frame to the output buffer
			for (int row = 0; row < height; row++)
			{
				// Copy the row of pixels without swapping the bytes
				memcpy(outputRowPtr, inputRowPtr, outputPitch);

				// Advance to the next input and output rows
				inputRowPtr += inputPitch;
				outputRowPtr += outputPitch;
			}
		}
		else
		{
			// Unsupported output format
			ASSERT(0);
			error = CFHD_ERROR_BADFORMAT;
		}
	}
	else
	{
		// Unsupported input format
		ASSERT(0);
		error = CFHD_ERROR_BADFORMAT;
	}

bail:

	return error;
}

// Scale the input image to fit the dimensions of the output image
CFHD_Error ScaleToOutputBuffer(void *inputBuffer, int inputWidth, int inputHeight,
							   int inputPitch, int inputFormat,
							   void *outputBuffer, int outputWidth, int outputHeight,
							   int outputPitch, CFHD_PixelFormat outputFormat,
							   int byte_swap_flag)
{
	CFHD_Error error = CFHD_ERROR_OKAY;

	// Use the memory allocator provided by ConvertLib
	CMemAlloc allocator;

#if (0 && SYSLOG)
	fprintf(stderr,
			"ScaleToOutputBuffer input width: %d, height: %d, pitch: %d, format: %d, output width: %d, height: %d, pitch: %d, format: %s\n",
			inputWidth, inputHeight, inputPitch, inputFormat,
			outputWidth, outputHeight, outputPitch, CStringFromOSType(outputFormat));
#endif

	if (inputFormat == DECODED_FORMAT_YU64)
	{
		// Allocate and initialize an image scaler with color conversion
		CImageScalerConverterYU64ToRGB scaler(&allocator);

		switch (outputFormat)
		{
		case CFHD_PIXEL_FORMAT_B64A:
			scaler.ScaleToBGRA64((unsigned char *)inputBuffer, inputWidth, inputHeight, inputPitch,
								 (unsigned char *)outputBuffer, outputWidth, outputHeight, outputPitch,
								 byte_swap_flag);
			break;

		default:
			// Unsupported output format
			ASSERT(0);
			error = CFHD_ERROR_BADFORMAT;
			break;
		}
	}
	else if (inputFormat == DECODED_FORMAT_RGBA)
	{
		// Allocate and initialize an image scaler with color conversion
		//CImageScalerConverterRGB32ToQuickTime scaler(&allocator);
		CBilinearScalerRGB32 scaler(&allocator);

		switch (outputFormat)
		{
		case CFHD_PIXEL_FORMAT_BGRA:
		case CFHD_PIXEL_FORMAT_BGRa:
			scaler.ScaleToQuickTimeBGRA((unsigned char *)inputBuffer, inputWidth, inputHeight, inputPitch,
										(unsigned char *)outputBuffer, outputWidth, outputHeight, outputPitch);
			break;
		default:
			// Unsupported output format
			ASSERT(0);
			error = CFHD_ERROR_BADFORMAT;
			break;
		}
	}
	else if (inputFormat == DECODED_FORMAT_B64A)
	{
		// Allocate and initialize an image scaler
		CImageScalerConverterB64A scaler(&allocator);

		switch (outputFormat)
		{
		case CFHD_PIXEL_FORMAT_B64A:
			scaler.ScaleToB64A((unsigned char *)inputBuffer, inputWidth, inputHeight, inputPitch,
							   (unsigned char *)outputBuffer, outputWidth, outputHeight, outputPitch,
							   byte_swap_flag);
			break;

		case CFHD_PIXEL_FORMAT_BGRA:
		case CFHD_PIXEL_FORMAT_BGRa:
			scaler.ScaleToBGRA((unsigned char *)inputBuffer, inputWidth, inputHeight, inputPitch,
							   (unsigned char *)outputBuffer, outputWidth, outputHeight, outputPitch);
			break;

		default:
			// Unsupported output format
			ASSERT(0);
			error = CFHD_ERROR_BADFORMAT;
			break;
		}
	}
	else
	{
		// Unsupported input format
		ASSERT(0);
		error = CFHD_ERROR_BADFORMAT;
	}

	return error;
}
