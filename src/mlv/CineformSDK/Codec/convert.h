/*! @file convert.h

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

#ifndef _CONVERT_H
#define _CONVERT_H

#ifdef __APPLE__
#include "../Common/macdefs.h"
#endif

#include "codec.h"
#include "image.h"
#include "dither.h"


#define LUTYUV(colorformat) \
			((colorformat & 0x7fffffff) == COLOR_FORMAT_UYVY || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_YUYV || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_YVYU || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_R408 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_V408 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_V210 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_YU64 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_YR16 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_CbYCrY_8bit || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_CbYCrY_16bit || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_CbYCrY_10bit_2_8 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_CbYCrY_16bit_2_14 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_CbYCrY_16bit_10_6)

#define FORMATRGB(colorformat) \
		   ((colorformat & 0x7fffffff) == COLOR_FORMAT_RGB24 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_RGB32 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_BGRA32 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_RGB32_INVERTED || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_QT32 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_RG64 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_B64A || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_RG48 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_RG30 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_R210 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_DPX0 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_AR10 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_AB10 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_WP13 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_W13A || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_RGB_8PIXEL_PLANAR)

#define INVERTEDFORMAT(colorformat) \
		   ((colorformat) == COLOR_FORMAT_RGB24 || \
			(colorformat) == COLOR_FORMAT_RGB32 || \
			(colorformat) == COLOR_FORMAT_QT32 || \
			(colorformat) == COLOR_FORMAT_BGRA32  )

#define FLIPCOLORS(colorformat) \
			(colorformat == COLOR_FORMAT_UYVY || \
			colorformat == COLOR_FORMAT_YUYV || \
			colorformat == COLOR_FORMAT_YVYU || \
			colorformat == COLOR_FORMAT_R408 || \
			colorformat == COLOR_FORMAT_V408 || \
			colorformat == COLOR_FORMAT_V210 || \
			colorformat == COLOR_FORMAT_NV12 || \
			colorformat == COLOR_FORMAT_YU64 || \
			colorformat == COLOR_FORMAT_YR16 || \
			colorformat == COLOR_FORMAT_RG48 || \
			colorformat == COLOR_FORMAT_RG64 || \
			colorformat == COLOR_FORMAT_B64A || \
			colorformat == COLOR_FORMAT_RG30 || \
			colorformat == COLOR_FORMAT_R210 || \
			colorformat == COLOR_FORMAT_DPX0 || \
			colorformat == COLOR_FORMAT_AR10 || \
			colorformat == COLOR_FORMAT_AB10 || \
			colorformat == COLOR_FORMAT_WP13 || \
			colorformat == COLOR_FORMAT_W13A || \
			colorformat == COLOR_FORMAT_RGB_8PIXEL_PLANAR)



#define FORMAT8BIT(colorformat) \
		   ((colorformat & 0x7fffffff) == COLOR_FORMAT_RGB24 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_RGB32 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_BGRA32 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_RGB32_INVERTED || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_QT32 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_UYVY || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_YUYV || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_YVYU || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_R408 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_V408 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_CbYCrY_8bit)

#define ALPHAOUTPUT(colorformat) \
		   ((colorformat & 0x7fffffff) == COLOR_FORMAT_RGB32 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_BGRA32 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_RGB32_INVERTED || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_QT32 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_RG64 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_B64A || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_W13A || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_R408 || \
			(colorformat & 0x7fffffff) == COLOR_FORMAT_V408)


#ifdef __cplusplus
extern "C" {
#endif

int Timecode2frames(char *tc, int rate);

void Convert8sTo16s(PIXEL8S *input, int input_pitch, PIXEL16S *output, int output_pitch, ROI roi);
void Convert16sTo8u(PIXEL16S *input, int input_pitch, PIXEL8U *output, int output_pitch, ROI roi);
void Copy16sTo16s(PIXEL16S *input, int input_pitch, PIXEL16S *output, int output_pitch, ROI roi);
void Convert16sTo8s(PIXEL16S *input, int input_pitch, PIXEL8S *output, int output_pitch, ROI roi);
void ConvertRow16sTo8s(PIXEL16S *input, PIXEL8S *output, int length);


void ConvertYUYVRowToRGB(uint8_t *input, uint8_t *output, int length, int format, int colorspace, int precision);
void ConvertYUYVRowToUYVY(uint8_t *input, uint8_t *output, int length, int format);
void ConvertYUYVRowToV210(uint8_t *input, uint8_t *output, int length, int format);
void ConvertYUYVRowToYU64(uint8_t *input, uint8_t *output, int length, int format);

void ConvertRGBRowToYUYV(uint8_t *input, uint8_t *output, int length);

// Convert packed 10-bit YUV 4:2:2 to packed 8-bit YUV
void ConvertV210ToYUV(uint8_t *input, int width, int height, int input_pitch,
					  uint8_t *output, int output_pitch, uint8_t *buffer);

// Convert packed 10-bit YUV 4:2:2 to rows of 16-bit luma and chroma
void ConvertV210ToYR16(uint8_t *input, int width, int height, int input_pitch,
					   uint8_t *output, int output_pitch, uint8_t *buffer);

// Convert one row of 10-bit YUV padded to 32 bits to 16-bit YUV
void ConvertV210RowToYUV(uint8_t *input, PIXEL *output, int length);

// Convert one row of 10-bit YUV padded to rows of 16-bit luma and chroma
void ConvertV210RowToYUV16(uint8_t *input, PIXEL16U *y_output, PIXEL16U *u_output, PIXEL16U *v_output,
						   int length, uint8_t *buffer);

// Convert one row of 10-bit YUV padded to packed 8-bit YUV
void ConvertV210RowToPackedYUV(uint8_t *input, uint8_t *output, int length, uint8_t *buffer);

// Convert one row of 64-bit YUV 10-bit YUV precision
void ConvertYU64RowToYUV10bit(uint8_t *input, PIXEL *output, int length);

// Convert one row of YUV uint8_ts to 10-bit YUV padded to 32 bits
void ConvertYUVRowToV210(uint8_t *input, uint8_t *output, int length);

// Convert one row of 16-bit YUV values to 10-bit YUV padded to 32 bits
void ConvertYUV16sRowToV210(PIXEL *input, uint8_t *output, int frame_width);

// Convert rows of 16-bit luma and chroma to packed YUV
void ConvertYUV16ToYUV(uint8_t *input, int width, int height, int input_pitch,
					   uint8_t *output, int output_pitch, uint8_t *buffer);

// Convert rows of 16-bit luma and chroma to V210 format
void ConvertYUV16ToV210(uint8_t *input, int width, int height, int input_pitch,
						uint8_t *output, int output_pitch, uint8_t *buffer);

void ConvertYUV16sRowToYU64(PIXEL *input, uint8_t *output, int length);

// Convert one row of 10-bit YUV padded to three channels of 16-bit YUV
void ConvertV210RowToPlanar16s(uint8_t *input, int length, PIXEL *y_output, PIXEL *u_output, PIXEL *v_output);

// Convert a frame of RGB24 to V210
void ConvertRGB24ToV210(uint8_t *data, int width, int height, int pitch, uint8_t *buffer);

// Unpack the row of packed pixel values for the specified channel
void UnpackRowYUV16s(uint8_t *input, PIXEL *output, int width, int channel, int format, int shift, int limit_yuv, int conv_601_709);

// Unpack one row of YUV pixels into 16-bit values
void UnpackYUVRow16s(uint8_t *input, int width, PIXEL *output[3]);

void ConvertYUVStripPlanarToPacked(uint8_t *planar_output[], int planar_pitch[], ROI strip,
								   uint8_t *output, int output_pitch, int frame_width, int format);


void ConvertPlanarGRBToPlanarRGB(PIXEL *dstline, PIXEL *srcline, int frame_width);
void ConvertPlanarGRBAToPlanarRGBA(PIXEL *dstline, PIXEL *srcline, int frame_width);

void ConvertPlanarRGB16uToPackedB64A(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									 uint8_t *output, int output_pitch, int frame_width);

void ConvertPlanarRGB16uToPackedRGB24(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									  uint8_t *output, int output_pitch, int frame_width, int shift);

void ConvertPlanarRGB16uToPackedRGB32(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									  uint8_t *output, int output_pitch, int frame_width, int shift, int num_channels);

void ConvertPlanarRGB16uToPackedRGB48(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									  uint8_t *output, int output_pitch, int frame_width);

void ConvertPlanarRGB16uToPackedRGBA64(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									  uint8_t *output, int output_pitch, int frame_width);

void ConvertPlanarRGB16uToPackedRGB30(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									  uint8_t *output, int output_pitch, int frame_width,
									  int format, int colorspace);

void ConvertPlanarRGB16uToPackedYU64(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									 uint8_t *output, int output_pitch, int frame_width, int colorspace);

void ConvertPlanarRGB16uToPackedYUV(PIXEL *planar_output[], int planar_pitch[], ROI strip,
									uint8_t *output_buffer, int output_pitch, int frame_width);

void ConvertBAYER2YUVStripPlanarToPacked(uint8_t *planar_output[], int planar_pitch[], ROI strip,
								   uint8_t *output, int output_pitch, int frame_width, int format, int colorspace);

void ConvertRGB2YUVStripPlanarToPacked(uint8_t *planar_output[], int planar_pitch[], ROI strip,
								   uint8_t *output, int output_pitch, int frame_width, int format, int colorspace);

void ConvertRGB2RG30StripPlanarToPacked(uint8_t *planar_output[], int planar_pitch[], ROI strip,
								   uint8_t *output, int output_pitch, int frame_width);
#if 0
void ConvertBAYER_3DLUT_YUVStripPlanarToPacked(DECODER *decoder, uint8_t *planar_output[], int planar_pitch[], ROI strip,
								   uint8_t *output, int output_pitch, int frame_width, int format);
#endif

void ConvertYUVStripPlanar16uToPacked(PIXEL16U *planar_output[], int planar_pitch[], ROI strip,
									  PIXEL16U *output, int output_pitch, int frame_width, int format);

void ConvertYUVStripPlanarToBuffer(uint8_t *planar_output[], int planar_pitch[], ROI strip,
								   uint8_t *output, int output_pitch, int frame_width,
								   int format, int colorspace);

void ConvertYUVStripPlanarToV210(PIXEL *planar_output[], int planar_pitch[], ROI strip,
								 uint8_t *output, int output_pitch, int frame_width,
								 int format, int colorspace, int precision);

void ConvertYUVStripPlanarToYU64(PIXEL *planar_output[], int planar_pitch[], ROI strip,
								 uint8_t *output, int output_pitch, int frame_width,
								 int format, int precision);

void ConvertPlanarYUVToRGB(uint8_t *planar_output[], int planar_pitch[], ROI roi,
						   uint8_t *output_buffer, int output_width, int output_pitch,
						   int format, int colorspace, bool inverted);

void ConvertRow16uToDitheredRGB(DECODER *decoder, uint8_t *planar_output[], int planar_pitch[], ROI roi,
						   uint8_t *output_buffer, int output_width, int output_pitch,
						   int format, int colorspace, bool inverted);

void ConvertCGRGBtoVSRGB(PIXEL *sptr, int width, int whitebitdepth, int flags);

void ConvertCGRGBAtoVSRGBA(PIXEL *sptr, int width, int whitebitdepth, int flags);

void ConvertYUVRow16uToBGRA64(uint8_t *planar_output[], int planar_pitch[], ROI roi,
						   uint8_t *output_buffer, int output_width, int output_pitch,
						   int format, int colorspace, int *whitebitdepth, int *ret_flags);

void ConvertYUVRow16uToYUV444(uint8_t *planar_output[], int planar_pitch[], ROI roi,
						   uint8_t *output_buffer, int output_width, int output_pitch, int format);

void ConvertPlanarYUVToUYVY(uint8_t *planar_output[], int planar_pitch[], ROI roi,
							uint8_t *output_buffer, int output_width, int output_pitch,
							int format, int colorspace, bool inverted);

void ConvertPlanarYUVToV210(PIXEL *planar_output[], int planar_pitch[], ROI roi,
							uint8_t *output_buffer, int output_width, int output_pitch,
							int format, int colorspace, bool inverted, int precision);

void ConvertPlanarYUVToYU64(PIXEL *planar_output[], int planar_pitch[], ROI roi,
							uint8_t *output_buffer, int output_width, int output_pitch,
							int format, int colorspace, bool inverted, int precision);

void ConvertPlanarYUVToYR16(PIXEL *planar_output[], int planar_pitch[], ROI roi,
							uint8_t *output_buffer, int output_width, int output_pitch,
							int format, int colorspace, bool inverted, int precision);

// Convert a row of 16-bit YUV to 8-bit planes
void ConvertYUVPacked16sRowToPlanar8u(PIXEL *input, int length, uint8_t *y_output, uint8_t *u_output, uint8_t *v_output);

// Convert a row of 16-bit YUV to 16-bit planes
void ConvertYUVPacked16sRowToPlanar16s(PIXEL *input, int length, PIXEL *y_output, PIXEL *u_output, PIXEL *v_output);

// Convert a row of RGB24 data to YUV (can use in place computation)
void ConvertRGB24RowToYUV(uint8_t *input, uint8_t *output, int length);

// Pack the intermediate results into the output row
void ConvertYUV16uRowToYUV(PIXEL16U *y_row_ptr, PIXEL16U *u_row_ptr, PIXEL16U *v_row_ptr,
						   uint8_t *output_ptr, int output_width);

// Pack one row of 16-bit unpacked luma and chroma into 8-bit luma and chroma (4:2:2 sampling)
void ConvertUnpacked16sRowToPacked8u(PIXEL **channel_row_ptr, int num_channels,
									 uint8_t *output_ptr, int output_width, int format);


void ConvertRGB2YUV(PIXEL *rlineptr, PIXEL *glineptr, PIXEL *blineptr,
					int pitchr, int pitchg, int pitchb,
					uint8_t *output_image,		// Row of reconstructed results
					int output_pitch,		// Distance between rows in bytes
					int width, int height, int precision, int color_space, int format);

void ConvertRGB2UYVY(PIXEL *rlineptr, PIXEL *glineptr, PIXEL *blineptr,
					 int pitchr, int pitchg, int pitchb,
					 uint8_t *output_image,	// Row of reconstructed results
					 int output_pitch,		// Distance between rows in bytes
					 int width, int height, int precision, int color_space, int format);

void ConvertRGB48toRGB24(PIXEL *rlineptr, PIXEL *glineptr, PIXEL *blineptr,
					int pitchr, int pitchg, int pitchb,
					uint8_t *output_image,		// Row of reconstructed results
					int output_pitch,		// Distance between rows in bytes
					int width, int height, int precision, int color_space);

void ConvertRGBA48toRGB32(PIXEL *rlineptr, PIXEL *glineptr, PIXEL *blineptr, PIXEL *alineptr,
					int input_pitch,
					uint8_t *output_image,		// Row of reconstructed results
					int output_pitch,		// Distance between rows in bytes
					int width, int height, int precision, int color_space, int num_channels);

// Convert one row of 16-bit unpacked luma and chroma into 8-bit RGB
void ConvertUnpacked16sRowToRGB24(PIXEL **channel_row_ptr, int num_channels,
								  uint8_t *output_ptr, int output_width, int descale,
								  int format, int color_space);

// Convert one row of 16-bit unpacked luma and chroma into 8-bit RGBA32
void ConvertUnpacked16sRowToRGB32(PIXEL **channel_row_ptr, int num_channels,
								  uint8_t *output_ptr, int length, int descale,
								  int format, int color_space, int alpha);

void ConvertUnpacked16sRowToYU64(PIXEL **input, int num_channels, uint8_t *output, int width,
								 const int descale, int precision, int format);

void ConvertUnpacked16sRowToB64A(PIXEL **input, int num_channels, uint8_t *output, int width,
								 const int descale, int precision);
void ConvertUnpacked16sRowToRGB30(PIXEL **input, int num_channels, uint8_t *output, int width,
								 const int descale, int precision, int format, int colorspace);
void ConvertUnpackedYUV16sRowToRGB48(PIXEL **input, int num_channels, uint8_t *output, int width,
								 const int descale, int precision, int format, int colorspace);

void ConvertUnpacked16sRowToRGB48(PIXEL **input, int num_channels, uint8_t *output, int width,
								 const int descale, int precision);

void ConvertUnpacked16sRowToRGBA64(PIXEL **input, int num_channels, uint8_t *output, int width,
								 const int descale, int precision);

unsigned char *Get2DLUT(DECODER *decoder, int bg);

void ConvertYUV16ToCbYCrY_10bit_2_8(DECODER *decoder,
									 int width,
									 int height,
									 int linenum,
									 PIXEL16U *src,
									 uint8_t *output,
									 int pitch,
									 int format,
									 int whitepoint,
									 int flags);

void ConvertCbYCrY_10bit_2_8ToRow16u(DECODER *decoder,
									 int width,
									 int height,
									 int linenum,
									 uint8_t *input,
									 PIXEL16U *output,
									 int pitch,
									 int format,
									 int whitepoint,
									 int flags);

void ConvertYUV16ToCbYCrY_16bit_2_14(DECODER *decoder,
									  int width,
									  int height,
									  int linenum,
									  PIXEL16U *input,
									  uint8_t *output,
									  int pitch,
									  int format,
									  int whitepoint,
									  int flags);

void ConvertCbYCrY_16bit_2_14ToRow16u(DECODER *decoder,
									  int width,
									  int height,
									  int linenum,
									  uint8_t *input,
									  PIXEL16U *output,
									  int pitch,
									  int format,
									  int whitepoint,
									  int flags);

void ConvertYUV16ToCbYCrY_16bit_10_6(DECODER *decoder,
									  int width,
									  int height,
									  int linenum,
									  PIXEL16U *input,
									  uint8_t *output,
									  int pitch,
									  int format,
									  int whitepoint,
									  int flags);

void ConvertCbYCrY_16bit_10_6ToRow16u(DECODER *decoder,
									  int width,
									  int height,
									  int linenum,
									  uint8_t *input,
									  PIXEL16U *output,
									  int pitch,
									  int format,
									  int whitepoint,
									  int flags);

void ConvertYUV16ToCbYCrY_8bit(DECODER *decoder,
								int width,
								int height,
								int linenum,
								PIXEL16U *input,
								uint8_t *output,
								int pitch,
								int format,
								int whitepoint,
								int flags,
								int rgb2yuv[3][4],
								int yoffset);

void ConvertCbYCrY_8bitToRow16u(DECODER *decoder,
								int width,
								int height,
								int linenum,
								uint8_t *input,
								PIXEL16U *output,
								int pitch,
								int format,
								int whitepoint,
								int flags);

void ConvertYUV16ToCbYCrY_16bit(DECODER *decoder,
								 int width,
								 int height,
								 int linenum,
								 PIXEL16U *input,
								 uint8_t *output,
								 int pitch,
								 int format,
								 int whitepoint,
								 int flags);

void ConvertCbYCrY_16bitToRow16u(DECODER *decoder,
								 int width,
								 int height,
								 int linenum,
								 uint8_t *input,
								 PIXEL16U *output,
								 int pitch,
								 int format,
								 int whitepoint,
								 int flags);

void ConvertYUV16ToNV12(DECODER *decoder,
						int width,
						int height,
						int linenum,
						uint16_t *input,
						uint8_t *output,
						int pitch,
						int format,
						int whitepoint,
						int flags);

void ConvertYUV16ToYV12(DECODER *decoder,
						int width,
						int height,
						int linenum,
						uint16_t *input,
						uint8_t *output,
						int pitch,
						int format,
						int whitepoint,
						int flags);

bool ConvertPreformatted3D(DECODER *decoder, 
						   int use_local_buffer, 
						   int internal_format,
						   int channel_mask, 
						   uint8_t *local_buffer,
						   int local_pitch,
						   int *channel_offset);
			

#ifdef __cplusplus
}
#endif

#endif
