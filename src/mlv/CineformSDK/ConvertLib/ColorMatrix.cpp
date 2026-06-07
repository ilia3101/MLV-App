/*! @file ColorMatrix.cpp

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

#include "StdAfx.h"
#include "ColorFlags.h"
#include "ColorMatrix.h"


template <typename PixelType>
void ColorMatrix<PixelType>::Convert(PixelType p1, PixelType p2, PixelType p3,
									 PixelType *c1_out, PixelType *c2_out, PixelType *c3_out)
{
	*c1_out = m[0][0] * p1 + m[0][1] * p2 + m[0][2] * p3 + m[0][3];
	*c2_out = m[1][0] * p1 + m[1][1] * p2 + m[1][2] * p3 + m[1][3];
	*c3_out = m[2][0] * p1 + m[2][1] * p2 + m[2][2] * p3 + m[2][3];
}

// Instantiate color conversion for the floating-point pixel type
template class ColorMatrix<float>;


RGBToYCbCr::RGBToYCbCr(ColorFlags color_flags,
					   int luma_offset,
					   int chroma_offset,
					   int descale_shift,
					   float scale_factor) :
	FloatColorMatrix(color_flags, scale_factor, 0.0),
	luma_offset(luma_offset),
	chroma_offset(chroma_offset),
	descale_shift(descale_shift)
{
	// Computer systems 601 color space
	static const float cs601[3][4] =
	{
        {0.257f,  0.504f,  0.098f,   16.0f/255.0f},
        {-0.148f, -0.291f,  0.439f,  128.0f/255.0f},
        {0.439f, -0.368f, -0.071f,  128.0f/255.0f}
	};

	// Computer systems 709 color space
	static const float cs709[3][4] =
	{
        {0.183f,  0.614f,  0.062f,   16.0f/255.0f},
        {-0.101f, -0.338f,  0.439f,  128.0f/255.0f},
        {0.439f, -0.399f, -0.040f,  128.0f/255.0f}
	};

	// Video safe 601 color space
	static const float vs601[3][4] =
	{
        {0.299f,  0.587f,  0.114f,  0},
        {-0.172f, -0.339f,  0.511f,  128.0f/255.0f},
        {0.511f, -0.428f, -0.083f,  128.0f/255.0f}
	};

	// Video safe 709 color space
	static const float vs709[3][4] =
	{
        {0.213f,  0.715f,  0.072f,  0},
        {-0.117f, -0.394f,  0.511f,  128.0f/255.0f},
        {0.511f, -0.464f, -0.047f,  128.0f/255.0f}
	};

	// Use the color conversion matrix that matches the color flags
	switch (color_flags)
	{
	case COLOR_FLAGS_CS_601:
		memcpy(m, cs601, sizeof(m));
		break;

	case COLOR_FLAGS_CS_709:
		memcpy(m, cs709, sizeof(m));
		break;

	case COLOR_FLAGS_VS_601:
		memcpy(m, vs601, sizeof(m));
		break;

	case COLOR_FLAGS_VS_709:
		memcpy(m, vs709, sizeof(m));
		break;

	default:
		assert(0);
		break;
	}

	// Scale the coefficients for this color matrix instance
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			m[i][j] *= scale_factor;
		}

		m[i][3] *= scale_offset;
	}
}

void RGBToYCbCr::Convert(uint16_t R, uint16_t G, uint16_t B,
						 uint16_t *Y_out, uint16_t *Cb_out, uint16_t *Cr_out)
{
	float Y_float, Cb_float, Cr_float;

	// Apply the color conversion matrix
	ColorMatrix<float>::Convert(R, G, B, &Y_float, &Cb_float, &Cr_float);

	// Convert to integers and apply the luma and chroma offsets
	int Y = (static_cast<int>(Y_float) >> descale_shift) + luma_offset;
	int Cb = (static_cast<int>(Cb_float) >> descale_shift) + chroma_offset;
	int Cr = (static_cast<int>(Cr_float) >> descale_shift) + chroma_offset;

	// Check that the luma and chroma results are 16-bit values
	assert(0 <= Y && Y <= UINT16_MAX);
	assert(0 <= Cb && Cb <= UINT16_MAX);
	assert(0 <= Cr && Cr <= UINT16_MAX);

	*Y_out = Y;
	*Cb_out = Cb;
	*Cr_out = Cr;
}
