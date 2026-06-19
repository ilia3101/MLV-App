/*! @file dither.h

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

#ifndef _DITHER_H
#define _DITHER_H

typedef struct color_matrix
{
	float array[3][4];		// The last column is the offset
	float scale;			// Amplitude of the coefficients

	// Record the color space used to fill the color matrix
	COLOR_SPACE color_space;

} COLOR_MATRIX;


typedef struct color_conversion
{
	int array[3][4];	// The last column is the offset
	int shift;			// Scale of the coefficients

	int luma_offset;	// Offset for video safe luma

	// Record the color space used to fill the color matrix
	int color_space;

} COLOR_CONVERSION;


#ifdef __cplusplus
extern "C" {
#endif

// Compute integer color conversion coefficients for the specified color space

void ComputeColorCoefficientsRGBToYUV(COLOR_CONVERSION *conversion,
									  int color_space);

void ComputeColorCoefficientsYUVToRGB(COLOR_CONVERSION *conversion,
									  int color_space);

#ifdef __cplusplus
}
#endif

#endif
