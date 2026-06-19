/*! @file bayer.h

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

#ifdef __cplusplus
extern "C" {
#endif

#include "image.h"
#include "filter.h"
#include "codec.h"

#define ACTIVEMETADATA_PRESATURATED			1 // already saturated in ActiveMetaData
#define ACTIVEMETADATA_PLANAR				2 // Data convert to scanline Planar RRRR...GGGG...BBBB
#define ACTIVEMETADATA_COLORFORMATDONE		4 // Color format handled by 3DLUT
#define ACTIVEMETADATA_SRC_8PIXEL_PLANAR	8 // Color format handled by 3DLUT

//Convert any decompressed planar ROWs of PIXEL16U into most output formats.
void ConvertRow16uToOutput(DECODER *decoder, int frame_index, int num_channels,
						   uint8_t *output, int pitch, FRAME_INFO *info,
						   int chroma_offset, int precision);

// Convert packed Bayer data to an image with the specified pixel format
void ConvertPackedBayerToRGB32(PIXEL16U *input_buffer, FRAME_INFO *info, int input_pitch, uint8_t *output_buffer, int output_pitch, int width, int height);
void ConvertPackedBayerToRGB24(PIXEL16U *input_buffer, FRAME_INFO *info, int input_pitch, uint8_t *output_buffer, int output_pitch, int width, int height);

// Convert planes of Bayer data to the specified pixel format
void ConvertPlanarBayerToRGB32(PIXEL16U *g1_plane, int g1_pitch, PIXEL16U *rg_plane, int rg_pitch,
							   PIXEL16U *bg_plane, int bg_pitch, PIXEL16U *g2_plane, int g2_pitch,
							   uint8_t *output_buffer, int pitch, int width, int height);

void InvertHorizontalStrip16sBayerThruLUT(HorizontalFilterParams);

void InvertHorizontalStrip16s444ThruLUT(HorizontalFilterParams);

void InvertHorizontalStrip16s422ThruLUT(HorizontalFilterParams);

//unsigned short *ApplyActiveMetaData(DECODER *decoder, int width, int height, int y, unsigned short *src, unsigned short *dst, int colorformat, int *whitebitdepth, int *flags);
void *ApplyActiveMetaData(DECODER *decoder, int width, int height, int y, uint32_t *src, uint32_t *dst, int colorformat, int *whitebitdepth, int *flags);

void ConvertLinesToOutput(DECODER *decoder, int width, int height, int linenum, unsigned short *src, uint8_t *outA8, int pitch,
							 int colorformat, int whitepoint, int flags);

void Convert4444LinesToOutput(DECODER *decoder, int width, int height, int linenum, unsigned short *src, uint8_t *outA8, int pitch,
							 int colorformat, int whitepoint, int flags);

void FastBlendWP13(short *Aptr, short *Bptr, short *output, int bytes);

void FastBlurV(unsigned short *Aptr, unsigned short *Bptr, unsigned short *Cptr,
			   unsigned short *output, int pixels);

void FastSharpeningBlurV(unsigned short *Aptr,
						 unsigned short *Bptr,
						 unsigned short *Cptr,
						 unsigned short *Dptr,
						 unsigned short *Eptr,
						 unsigned short *output, int pixels, int sharpness);

void ComputeCube(DECODER *decoder);

#ifdef __cplusplus
}
#endif
