/*! @file Conversion.h

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

#ifndef FOURCC
#define FOURCC(x)	(int)(((x) >> 24) & 0xFF), (int)(((x) >> 16) & 0xFF), (int)(((x) >> 8) & 0xFF), (int)((x) & 0xFF)
#endif

// Some pixel formats are not defined in the QuickTime header files
#define k4444YpCbCrA32RPixelFormat 'r4fl'

#ifdef __cplusplus
extern "C"
{
#endif

static bool IsPrintableFourCC(char *fourcc)
{
		int i;

		// Check that all of the characters are printable
		for (i = 0; i < 4; i++)
		{
#if _WIN32
			if (isspace(fourcc[i]) || !isprint(fourcc[i])) {
#else
			if( (fourcc[i]<=32) || ((unsigned char)fourcc[i]>127)) {
#endif
				return false;
			}
		}

		return true;
}

__inline static const char *CStringFromOSType(unsigned int fourcc)
{
	//static char string[16];
	static char string[8];
	//Boolean isPrintable = true;
	//int i;

#if _WIN32
	sprintf_s(string, sizeof(string), "%c%c%c%c", FOURCC(fourcc));
#else
	sprintf(string, "%c%c%c%c", FOURCC(fourcc));
#endif

	if (!IsPrintableFourCC(string)) {
		//sprintf(string, "0x%08X", (unsigned int)fourcc);
#if _WIN32
		sprintf_s(string, sizeof(string), "%d", (int)fourcc);
#else
		sprintf(string, "%d", (int)fourcc);
#endif
	}

	return string;
}

// Convert the input image format to the output format
CFHD_Error ConvertToOutputBuffer(void *inputBuffer, int inputPitch, int inputFormat,
								 void *outputBuffer, int outputPitch, CFHD_PixelFormat outputFormat,
								 int width, int height, int byte_swap_flag);

// Scale the input image to fit the dimensions of the output image
CFHD_Error ScaleToOutputBuffer(void *inputBuffer, int inputWidth, int inputHeight,
							   int inputPitch, int inputFormat,
							   void *outputBuffer, int outputWidth, int outputHeight,
							   int outputPitch, CFHD_PixelFormat outputFormat,
							   int byte_swap_flag);

#ifdef __cplusplus
}
#endif
