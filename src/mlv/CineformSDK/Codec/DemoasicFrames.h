/*! @file DemoasicFrames.h

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

#ifndef _DEMOSAICFRAMESLIB_H
#define _DEMOSAICFRAMESLIB_H

#include "../Common/AVIExtendedHeader.h"
#include "codec.h"

typedef int DEBAYER_ORDERING;

#define BAYER_FORMAT_RED_GRN		0
#define BAYER_FORMAT_GRN_RED		1
#define BAYER_FORMAT_GRN_BLU		2
#define BAYER_FORMAT_BLU_GRN		3


#ifdef __cplusplus
extern "C" {
#endif

void DebayerLine(int width, int height, int linenum,
				unsigned short *bayer_source, 
				DEBAYER_ORDERING order,
				unsigned short *RGB_output, 
				int highquality,
				int sharpening);

void ColorDifference2Bayer(int width, 
						   unsigned short *srcptr, 
						   int bayer_pitch,
						   int bayer_format);

void BayerRippleFilter(	int width, 
						   unsigned short *srcptr, 
						   int bayer_pitch,
						   int bayer_formatm,
						   unsigned short *srcbase);

float *LoadCube64_3DLUT(DECODER *decoder, CFHDDATA *cfhddata, int *lutsize);
float *ResetCube64_3DLUT(DECODER *decoder, int cube_base);

void VerticalOnlyDebayerLine(int width, int height, int linenum,
				unsigned short *bayer_source,
				DEBAYER_ORDERING order,
				unsigned short *RGB_output, 
				int highquality,
				int sharpening);

#ifdef __cplusplus
}
#endif

#endif
