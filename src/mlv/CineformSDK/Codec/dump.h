/*! @file dump.h

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

#pragma once

#ifndef _DUMP
#if !_DEBUG
#define _DUMP 0		// Always disable the dump facility in production builds
#else
#define _DUMP 0		// Disable the dump facility in all builds by default
#endif
#endif

#if _DUMP

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "wavelet.h"

// Dump files are placed in the root directory of the default drive
// if no directory is provided or if the directory is the null string

#define DUMP_ENCODER_DIRECTORY	"C:/Cedoc/Dump/Encoder"
#define DUMP_DECODER_DIRECTORY	"C:/Cedoc/Dump/Decoder"

#define DUMP_DEFAULT_FILENAME	"f%04dc%1dw%1db%1d.pgm"

typedef struct dump
{
	BOOL enabled;			// Switch that controls debug output
//	BOOL decode;

	DWORD channel_mask;		// Bitmask for selecting which channels to output
	DWORD wavelet_mask;		// Bitmask for selecting which wavelet bands to output

	char directory[_MAX_PATH];
	char filename[_MAX_PATH];

} DUMP_INFO;

// Forward reference
typedef struct codec CODEC;

#if __cplusplus
extern "C" {
#endif

BOOL SetDumpDirectory(CODEC *codec, char *directory);
BOOL SetDumpFilename(CODEC *codec, char *filename);
BOOL SetDumpChannelMask(CODEC *codec, DWORD mask);
BOOL SetDumpWaveletMask(CODEC *codec, DWORD mask);
BOOL DumpTransformBands(CODEC *codec, TRANSFORM *transform, int channel, BOOL requantize);
BOOL DumpWaveletBand(IMAGE *wavelet, int band, BOOL requantize, FILE *file);

#if __cplusplus
}
#endif

#endif
