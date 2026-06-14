/*! @file frame.h

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
#ifndef _FRAME_H
#define _FRAME_H

#include "config.h"
#include "image.h"
#include "error.h"

#if _ALLOCATOR
#include "allocator.h"
#endif

#define FRAME_MAX_CHANNELS	4 //DAN070202004

// Monochrome converts color to gray
#ifndef _MONOCHROME
#define _MONOCHROME 0
#endif

// Can have YUV 4:2:2 format (alternating u and v) or YUV 4:4:4 format
#ifndef _YUV422
#define _YUV422 1
#endif

#if _MONOCHROME
#define FRAME_FORMAT_DEFAULT FRAME_FORMAT_GRAY
#else
#define FRAME_FORMAT_DEFAULT FRAME_FORMAT_YUV
#endif

typedef enum frame_sampling
{
	FRAME_SAMPLING_422,
	FRAME_SAMPLING_444,

} FRAME_SAMPLING;

// Use the same structure packing as the Intel C/C++ compiler
//#pragma pack(push)
//#pragma pack(16)
#pragma pack(push, 16)

struct frame_info;		// Forward reference

// Definition of a video frame
typedef struct frame
{
	int num_channels;		// Number of channels of gray and color
	int format;				// Organization of the image color planes

	int width;				// Frame dimensions (all channels or luma channel)
	int height;
	int display_height;

	int iskey;				// Is this a key frame?

	// Color components are separated into planes
	IMAGE *channel[FRAME_MAX_CHANNELS];

} FRAME;

enum
{
	FRAME_FORMAT_NONE = 0,		// No color format
	FRAME_FORMAT_GRAY = 1,		// One plane of gray pixels
	FRAME_FORMAT_YUV = 2,		// Three planes of YUV
	FRAME_FORMAT_RGB = 3,		// Three planes of RGB
	FRAME_FORMAT_RGBA = 4		// Four planes of RGBA
};

enum
{
	FRAME_CHANNEL_GRAY = 0,		// First channel is the gray value image
	FRAME_CHANNEL_U = 1,		// Chrominance
	FRAME_CHANNEL_V = 2,

	FRAME_CHANNEL_RED = 0,		// Allocation of RGB to channels
	FRAME_CHANNEL_GREEN = 1,
	FRAME_CHANNEL_BLUE = 2,
	FRAME_CHANNEL_ALPHA = 3
};


#ifdef __cplusplus
extern "C" {
#endif

#if _ALLOCATOR
FRAME *CreateFrame(ALLOCATOR *allocator, int width, int height, int display_height, int format);
FRAME *ReallocFrame(ALLOCATOR *allocator, FRAME *frame, int width, int height, int display_height, int format);
#else
FRAME *CreateFrame(int width, int height, int display_height, int format);
FRAME *ReallocFrame(FRAME *frame, int width, int height, int display_height, int format);
#endif

// Set the frame dimensions without allocating memory for the planes
void SetFrameDimensions(FRAME *frame, int width, int height, int display_height, int format);

#if _ALLOCATOR
FRAME *CreateFrameFromFrame(ALLOCATOR *allocator, FRAME *frame);
#else
FRAME *CreateFrameFromFrame(FRAME *frame);
#endif


void ConvertPackedToFrame(uint8_t *data, int width, int height, int pitch, FRAME *frame);

void ConvertRGB32to10bitYUVFrame(uint8_t *rgb, int pitch, FRAME *frame, uint8_t *scratch,
								 int scratchsize, int colorspace, int precision, int srcHasAlpha, int rgbaswap);
void ConvertNV12to10bitYUVFrame(uint8_t *rgb, int pitch, FRAME *frame, uint8_t *scratch,
								 int scratchsize, int colorspace, int precision, int progressive);
void ConvertYV12to10bitYUVFrame(uint8_t *rgb, int pitch, FRAME *frame, uint8_t *scratch,
								 int scratchsize, int colorspace, int precision, int progressive);
				
void ConvertYUYVToFrame16s(uint8_t *yuv, int pitch, FRAME *frame, uint8_t *buffer);

// Convert the packed 10-bit YUV 4:2:2 to planes of 8-bit YUV
void ConvertV210ToFrame8u(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer);

// Convert the packed 10-bit YUV 4:2:2 to planes of 16-bit YUV
void ConvertV210ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer);
// Convert the unpacked 16-bit YUV 4:2:2 to planes of 16-bit YUV
void ConvertYU64ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer);


int ConvertRGB10ToDPX0(uint8_t *data, int pitch, int width, int height, int unc_format);
int ConvertDPX0ToRGB10(uint8_t *data, int pitch, int width, int height, int unc_format);

void ConvertBYR1ToFrame16s(int bayer_format, uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer);
void ConvertBYR2ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer);
void ConvertBYR3ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer);
void ConvertBYR3ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer);
int ConvertBYR3ToPacked(uint8_t *data, int pitch, int width, int height, uint8_t *buffer);
int ConvertBYR4ToPacked(uint8_t *data, int pitch, int width, int height, uint8_t *buffer, int bayer_format);
void ConvertBYR4ToFrame16s(int bayer_format, uint32_t encode_curve, uint32_t encode_curve_preset, uint8_t *data, int pitch, FRAME *frame, int precision);
void ConvertBYR5ToFrame16s(int bayer_format, uint8_t *uncompressed_chunk, int pitch, FRAME *frame, uint8_t *scratch);

int ConvertPackedToBYR2(int width, int height, uint32_t *uncompressed_chunk,
						uint32_t uncompressed_size, uint8_t *output_buffer, int output_pitch, 
						unsigned short *curve);
int ConvertPackedToBYR3(int width, int height, uint32_t *uncompressed_chunk,
						uint32_t uncompressed_size, uint8_t *output_buffer, int output_pitch);
int ConvertPackedToRawBayer16(int width, int height, uint32_t *uncompressed_chunk,
							  uint32_t uncompressed_size, PIXEL16U *output_buffer, PIXEL16U *scratch,
							  int resolution);


void ConvertRGB48ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int precision, int origformat);
void ConvertRGBA64ToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int precision, int origformat, int alpha);
void ConvertRGBtoRGB48(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int precision);
void ConvertRGBAtoRGB48(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int precision, int rgbaswap);
void ConvertRGBAtoRGBA64(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int precision, int rgbaswap);

void ConvertCbYCrY_10bit_2_8ToFrame16s(void *data, int pitch, FRAME *frame, void *buffer, int precision, int alpha);
void ConvertCbYCrY_16bit_2_14ToFrame16s(void *data, int pitch, FRAME *frame, void *buffer, int precision, int alpha);
void ConvertCbYCrY_16bit_10_6ToFrame16s(void *data, int pitch, FRAME *frame, void *buffer, int precision, int alpha);
void ConvertCbYCrY_8bitToFrame16s(void *data, int pitch, FRAME *frame, void *buffer, int precision, int alpha);
void ConvertCbYCrY_16bitToFrame16s(void *data, int pitch, FRAME *frame, void *buffer, int precision, int alpha);

// Convert QuickTime b64a to planar YUV
void ConvertAnyDeep444to422(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int colorspace, int origformat);

// Convert the lowpass bands of planar RGB 4:4:4 to the specified output format
void ConvertLowpassRGB444ToRGB(IMAGE *images[], uint8_t *output_buffer,
							   int output_width, int output_height,
							   int32_t output_pitch, int format,
							   bool inverted, int shift, int num_channels);

void ConvertLowpassRGB444ToRGB24(PIXEL *plane_array[], int pitch_array[],
								 uint8_t *output_buffer, int output_pitch,
								 ROI roi, bool inverted, int shift);

void ConvertLowpassRGB444ToRGB32(PIXEL *plane_array[], int pitch_array[],
								 uint8_t *output_buffer, int output_pitch,
								 ROI roi, bool inverted, int shift, int num_channels);

void ConvertLowpassRGB444ToB64A(PIXEL *plane_array[], int pitch_array[],
								uint8_t *output_buffer, int output_pitch,
								ROI roi, bool inverted, int shift, int num_channels);

void ConvertLowpassRGB444ToRGB30(PIXEL *plane_array[], int pitch_array[],
								 uint8_t *output_buffer, int output_pitch,
								 ROI roi, bool inverted, int shift, int format);

void ConvertLowpassRGB444ToRGB48(PIXEL *plane_array[], int pitch_array[],
								 uint8_t *output_buffer, int output_pitch,
								 ROI roi, bool inverted, int shift);

void ConvertLowpassRGB444ToRGBA64(PIXEL *plane_array[], int pitch_array[],
								  uint8_t *output_buffer, int output_pitch,
								  ROI roi, bool inverted, int shift);

// Convert QuickTime b64a to planar RGB with optional alpha channel
CODEC_ERROR ConvertBGRA64ToFrame_4444_16s(uint8_t *data, int pitch, FRAME *frame,
										  uint8_t *buffer, int precision);

// Convert Final Cut Pro 'r4fl' to planar YUV
void ConvertYUVAFloatToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer);

// Convert Final Cut Pro 'v408' && 'r408' to planar YUV
void ConvertYUVAToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int format);

// Convert QuickTime format r4fl to a frame of planar RGB 4:4:4
void ConvertYUVAFloatToFrame_RGB444_16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer);
// Convert QuickTime format r4fl to a frame of planar RGBA 4:4:4:4
void ConvertYUVAFloatToFrame_RGBA4444_16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer);

// Convert QuickTime 'BGRA' to planar YUV
//void ConvertQuickTimeBGRAToFrame16s(uint8_t *data, int pitch, FRAME *frame, uint8_t *buffer, int color_space, int precision, int rgbaswap);

void ConvertLowpass16sToRGBNoIPPFast(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch, int format, int colorspace, bool inverted, int descale);
void ConvertLowpass16sYUVtoRGB48(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch, int colorspace, bool inverted, int descale, int format, int whitebitdepth);
void ConvertLowpass16sRGB48ToRGB(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch, int format, int colorspace, bool inverted, int descale, int num_channels);
void ConvertLowpass16sRGB48ToRGB48(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch, int descale, int num_channels);
void ConvertLowpass16sBayerToRGB48(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch, int descale, int num_channels);
void ConvertLowpass16sRGBA64ToRGBA64(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch, int descale, int num_channels, int format);
void ConvertLowpass16sToYUV(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch,
							int format, bool inverted);
void ConvertLowpass16sToYUV64(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch,
							int format, bool inverted, int precision);

// Convert the lowpass band to rows of unpacked 16-bit YUV
void ConvertLowpass16sToYR16(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch,
							 int format, bool inverted, int precision);

void ConvertLowpass16s10bitToYUV(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height, int32_t output_pitch,
								 int format, bool inverted, int lineskip);
void ConvertLowpass16s10bitToV210(IMAGE *images[], uint8_t *output_buffer, int output_width, int output_height,
								  int32_t output_pitch, int format, bool inverted);
void AddCurveToUncompressedBYR4(uint32_t encode_curve, uint32_t encode_curve_preset,
								uint8_t *data, int pitch, FRAME *frame);

#if _ALLOCATOR
void DeleteFrame(ALLOCATOR *allocator, FRAME *frame);
#else
void DeleteFrame(FRAME *frame);
#endif

#ifdef __cplusplus
}
#endif

#pragma pack(pop)

#endif
