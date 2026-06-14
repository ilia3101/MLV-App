/*! @file ColorMatrix.h

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

template <typename PixelType>
class ColorMatrix
{
public:

	ColorMatrix(ColorFlags color_flags = COLOR_FLAGS_DEFAULT,
				float scale_factor = 64.0,
				float scale_offset = 16384.0) :
		color_flags(color_flags),
		scale_factor(scale_factor),
		scale_offset(scale_offset)
	{
	}

	// Convert the three tuple of pixels to converted values
	void Convert(PixelType p1, PixelType p2, PixelType p3,
				 PixelType *c1_out, PixelType *c2_out, PixelType *c3_out);

protected:

	// Color conversion matrix (fourth column is the offset)
	PixelType m[3][4];

	// Color space flags used to select the conversion matrix
	ColorFlags color_flags;

	float scale_factor;
	float scale_offset;

};

typedef ColorMatrix<float> FloatColorMatrix;


/*!
	@brief RGB to YCbCr color space conversion for 16-bit unsigned pixels

	@description The computer safe region uses the full pixel range while
	video safe uses a limited range that allows for super blacks and whites.


	The floating-point and fixed-point coefficients for each color space.

	Y  = 0.257 * R + 0.504 * G + 0.098 * B + 16.5;
	Cb =-0.148 * R - 0.291 * G + 0.439 * B + 128.5;
	Cr = 0.439 * R - 0.368 * G - 0.071 * B + 128.5;

	Fixed point approximation (8-bit) is

	Y  = ( 66 * R + 129 * G +  25 * B +  4224) >> 8;
	Cb = (-38 * R -  74 * G + 112 * B + 32896) >> 8;
	Cr = (112 * R -  94 * G -  18 * B + 32896) >> 8;


	Computer safe 601 color space:

	Y  = 0.257R + 0.504G + 0.098B + 16;
	Cb = -0.148R - 0.291G + 0.439B + 128;
	Cr = 0.439R - 0.368G - 0.071B + 128;


	Video safe 601 color space:

	Y = 0.299R + 0.587G + 0.114B
	Cb = -0.172R - 0.339G + 0.511B + 128
	Cr = 0.511R - 0.428G - 0.083B + 128


	Compute safe 709 color space:

	Y = 0.183R + 0.614G + 0.062B + 16
	Cb = -0.101R - 0.338G + 0.439B + 128
	Cr = 0.439R - 0.399G - 0.040B + 128


	Video safe 709 color space:

	Y = 0.213R + 0.715G + 0.072B
	Cb = -0.117R - 0.394G + 0.511B + 128
	Cr = 0.511R - 0.464G - 0.047B + 128

*/
class RGBToYCbCr : public FloatColorMatrix
{
public:

	// Initialize a color converter with 16-bit precision by default
	RGBToYCbCr(ColorFlags color_flags,
			   int luma_offset = (16 << 8),
			   int chroma_offset = (128 << 8),
			   int descale_shift = 6,
			   float scale_factor = 64.0);

	//TODO: Define a constructor that comutes the scale factors from the precision
	RGBToYCbCr(int input_precision,
			   int output_precision,
			   ColorFlags color_flags);

	//TODO: Make the input and output pixel type a template parameter

	// Convert the three tuple of 16-bit pixels
	void Convert(uint16_t R, uint16_t G, uint16_t B,
				 uint16_t *Y_out, uint16_t *Cb_out, uint16_t *Cr_out);

private:

	int luma_offset;		// The luma offset is determined by the range of the luma values
	int chroma_offset;		// The chroma offset is determined by the rangte of the chroma values
	int descale_shift;		// Descale the pixels by this amount before applying the offsets

};
