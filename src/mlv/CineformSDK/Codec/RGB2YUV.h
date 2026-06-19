/*! @file RGB2YUV.h

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

#include <stdint.h>

#ifndef COLOR_SPACE_BT_601
	#define COLOR_SPACE_BT_601			1
	#define COLOR_SPACE_BT_709			2		// BT 709 versus 601 YUV source
	#define COLOR_SPACE_VS_RGB			4 		// RGB that ranges normally from 16 to 235 just like luma
	#define COLOR_SPACE_422_TO_444		8
	#define COLOR_SPACE_8_PIXEL_PLANAR	16

	#define COLOR_SPACE_VS_709  (COLOR_SPACE_BT_709 | COLOR_SPACE_VS_RGB)
	#define COLOR_SPACE_VS_601  (COLOR_SPACE_BT_601 | COLOR_SPACE_VS_RGB)
	#define COLOR_SPACE_CG_709  (COLOR_SPACE_BT_709)
	#define COLOR_SPACE_CG_601  (COLOR_SPACE_BT_601)

	#define COLOR_SPACE_DEFAULT  COLOR_SPACE_CG_709
#endif

#define COLOR_SPACE_MASK	(COLOR_SPACE_BT_601|COLOR_SPACE_BT_709|COLOR_SPACE_VS_RGB)



#ifdef __cplusplus
extern "C" {
#endif

//TODO: Use standard integer datatypes for the function parameters

void UpShift16(unsigned short *in_rgb16, int pixels, int upshift, int saturate);
void ChannelYUYV16toPlanarYUV16(unsigned short *planar_output[], unsigned short *out_YUV, int width, int colorspace);
void ChunkyRGB16toPlanarRGB16(unsigned short *in_rgb16, unsigned short *out_rgb16, int width);
void ChunkyRGB8toPlanarRGB16(unsigned char *in_rgb8, unsigned short *out_rgb16, int width);
void ChunkyRGB8toChunkyRGB16(unsigned char *in_rgb8, unsigned short *out_rgb16, int width);
void ChunkyBGR8toPlanarRGB16(unsigned char *in_rgb8, unsigned short *out_rgb16, int width);
void ChunkyBGRA8toPlanarRGB16(unsigned char *in_rgb8, unsigned short *out_rgb16, int width);
void ChunkyARGB8toPlanarRGB16(unsigned char *in_rgb8, unsigned short *out_rgb16, int width);
void PlanarRGB16toChunkyRGB16(unsigned short *in_rgb16, unsigned short *out_rgb16, int width);
void PlanarYUV16toChunkyYUYV16(unsigned short *in_YUV, unsigned short *out_YUYV, int width, int colorspace);
void PlanarYUV16toChunkyYUYV8(unsigned short *in_YUV, unsigned char *out_YUYV, int width, int colorspace);
void PlanarYUV16toChannelYUYV16(unsigned short *in_YUV, unsigned short *planar_output[], int width, int colorspace, int shift);
void ChunkyYUYV16toPlanarYUV16(unsigned short *in_YUYV, unsigned short *out_YUV, int width, int colorspace);
void ChunkyYUYV8toPlanarYUV16(uint8_t *in_YUYV, uint8_t *out_YUV, int width, int colorspace);
void ChunkyRGB16toChunkyYUV16(unsigned short *in_RGB48, unsigned short *out_YUV48, int width, int colorspace);

void PlanarRGB16toPlanarYUV16(unsigned short *linebufRGB, unsigned short *linebufYUV, int width, int colorspace);
void PlanarYUV16toPlanarRGB16(unsigned short *linebufYUV, unsigned short *linebufRGB, int width, int colorspace);

void ChunkyRGB16toChunkyYUYV16(int width, int height, 
							  unsigned short *rgb16, int RGBpitch, 
							  unsigned short *yuyv16, int YUVpitch, 
							  unsigned short *scratch, int scratchsize,
							  int colorspace);

void ChunkyYUYV16toChunkyRGB16(int width, int height,
							  unsigned short *yuyv16, int YUVpitch, 
							  unsigned short *rgb16, int RGBpitch, 
							  unsigned short *scratch, int scratchsize,
							  int colorspace);

#ifdef __cplusplus
}
#endif
