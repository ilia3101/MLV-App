/*! @file filer.h

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

#ifndef _FILTER_H
#define _FILTER_H

#include "config.h"		// Configuration determines the pixel datatype
#include "image.h"		// Use the pixel datatype from the image module
//#include "ipp.h"		// Use Intel Performance Primitives


#define _HIGHPASS_8S		0			// Output highpass values as signed bytes?
#define _HIGHPASS_CODED		0			// Variable length code the highpass values?

//#ifndef _TEMPORAL_PRESCALE
#define _TEMPORAL_PRESCALE	0			// Amount of prescaling for temporal transform
//#endif

//#ifndef _TEMPORAL_HIGHPASS_PRESCALE
#define _TEMPORAL_HIGHPASS_PRESCALE	0	// Amount of prescaling for the spatial transform on temporal highpass
//#endif

//#ifndef _FRAME_PRESCALE
#define _FRAME_PRESCALE		2			// Amount of prescaling for interlaced frame transform 
//#endif

//#ifndef _SPATIAL_PRESCALE
#define _SPATIAL_PRESCALE	0			// Amount of prescaling for spatial transform
//#endif

//#ifndef _TRANSFORM_PRESCALE
#define _TRANSFORM_PRESCALE	0			// Do not perform prescaling in the transform
//#endif

//#ifndef _INVERSE_DESCALE
#define _INVERSE_DESCALE	1			// Remove scaling during inverse transforms
//#endif

//#ifndef _INVERSE_PRESCALE
#define _INVERSE_PRESCALE	0			// Do not perform prescaling in early stages of decoding
//#endif

//#ifndef _INVERSE_UNSCALED
#define _INVERSE_UNSCALED	1			// Input to spatial transform is not prescaled
//#endif

//#ifndef _INVERSE_MIDSCALE
//#define _INVERSE_MIDSCALE	1
#define _INVERSE_MIDSCALE 0			// No intermediate scaling during the spatial inverse
//#endif

// Prescaling for spatial transforms applied to the temporal lowpass band to avoid overflow
#ifndef _LOWPASS_PRESCALE
#define _LOWPASS_PRESCALE	2
#endif

//#ifndef _PRESERVE_PRECISION
//#define _PRESERVE_PRECISION	1
//#endif


// The rounding adjustment is required to make the wavelet transforms
// perfectly reversible, but does not seem to affect the quality in
// normal use.  So the rounding adjustment is only used for debugging
// so diagnostic routines can rely on reversibility to test correctness.

#if (0 && _DEBUG)
#define _ROUNDING	1	// Add a rounding adjustment before division
#define _FASTDIV	0	// Disable use of approximations in division
#else
#define _ROUNDING	0	// Disable the rounding adjustments
#define _FASTDIV	1	// Use approximations for signed division
#endif

// Rounding adjustment added to x before division by y
#if _ROUNDING
//#define ROUNDING(x,y)	((x >= 0) ? y/2 : -y/2)
#define ROUNDING(x,y)	(4)	//DAN20050914 -- ROUNDING(sum,8) should be just +4 thru 7 (6 works)
#else
#define ROUNDING(x,y)	(4)	//DAN20050914 -- ROUNDING(sum,8) should be just +4 thru 7 (6 works)
#endif

// Truncate result to specified number of bits
#define TRUNCATE(n,m)	((n) & ~((1 << (m)) - 1))

// Define macros for saturating the lowpass and highpass coefficients
#if _HIGHPASS_8S
#define HIGHPASS(x) SATURATE_8S(x)
#else
#define HIGHPASS(x) SATURATE_16S(x)
#endif
#define LOWPASS(x) SATURATE_16S(x)

//#ifndef _ENCODE_QUANT
#define _ENCODE_QUANT	0		// Do not perform quantization during coefficient encoding
//#endif

//#ifndef _ENCODE_PITCH8S
#define _ENCODE_PITCH8S	0		// Do not use 8-bit pitch for highpass coefficients
//#endif

#ifdef __cplusplus
extern "C" {
#endif

#if 0

// Divide a signed number using arithmetic right shift
static __inline int32_t DivideByShift(int32_t x, int shift)
{
 #if 1
	int quotient;

	if (x < 0)
		quotient = -((- x) >> shift);
	else
		quotient = (x >> shift);

 #else
	int32_t quotient;
  #if 1
	if (x < 0)
	{
		// Signed division is a special case
   #if 1
		int32_t mask = (1 << shift) - 1;
		int32_t adjustment = (x & mask) ? 1 : 0;

		quotient = (x >> shift) + adjustment;
   #else
		quotient = x / (1 << shift);
   #endif
	}
	else
	{
		quotient = (x >> shift);
	}

	//quotient = SATURATE(quotient);
  #else
	quotient = x / (1 << shift);
  #endif
 #endif
	return quotient;
}

#else

//#define DivideByShift(x, s)		((x) < 0) ? NEG(NEG(x) >> (s)) : ((x) >> (s))

//DAN20050914 -- all DivideByShift that use sign aren't reversible in wavelet transforms
//#define DivideByShift(x, s)		((x)>>=(s))

// Fix compiler warnings about undefined operation
#define DivideByShift(x, s)		((x) >> (s))

#endif

#if 0
// The temporal filter declarations have been moved to temporal.h

// Apply the temporal transform between two images
void FilterTemporal(PIXEL *field1, int pitch1, PIXEL *field2, int pitch2,
					PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					ROI roi);

// Apply the temporal transform between two bands of 16-bit signed pixels
void FilterTemporal16s(PIXEL *field1, int pitch1, PIXEL *field2, int pitch2,
					   PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					   ROI roi);

// Perform the temporal transform on a pair of rows producing 16-bit coefficients
void FilterTemporalRow8uTo16s(PIXEL8U *row1, PIXEL8U *row2, int length,
							  PIXEL16S *lowpass, PIXEL16S *highpass,
							  int offset);

// Apply the temporal transform to a row of packed YUV pixels
void FilterTemporalRowYUVTo16s(BYTE *row1, BYTE *row2, int frame_width,
							   PIXEL *lowpass[], PIXEL *highpass[], int num_channels);

// Apply the temporal transform to one channel in the even and odd rows
void FilterTemporalRowYUYVChannelTo16s(BYTE *row1, BYTE *row2, int frame_width,
									   int channel, PIXEL *lowpass, PIXEL *highpass,
									   int offset, int precision, int limit_yuv);

void FilterTemporalRowUYVYChannelTo16s(BYTE *row1, BYTE *row2, int frame_width,
									   int channel, PIXEL *lowpass, PIXEL *highpass,
									   int offset, int precision, int limit_yuv);

// Apply the inverse temporal transform to reconstruct two fields of 16-bit signed pixels
void InvertTemporal16s(PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					   PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi);

void InvertTemporal16s8sTo16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch, 
							  PIXEL8S *highpass, int highpass_quantization, int highpass_pitch,
							  PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi);

void InvertTemporalQuant16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch, 
							PIXEL *highpass, int highpass_quantization, int highpass_pitch,
							PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi,
							PIXEL *buffer, size_t buffer_size);

void InvertTemporalQuant16s8sTo16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch, 
								   PIXEL8S *highpass, int highpass_quantization, int highpass_pitch,
								   PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi,
								   PIXEL *buffer, size_t buffer_size);

void InvertTemporalRow16s(PIXEL *lowpass, PIXEL *highpass,
						  PIXEL *even, PIXEL *odd, int width);

// Apply temporal filter between the even and odd fields of a frame
// Computation can be done in place by passing the even and odd fields as outputs
void FilterInterlaced(PIXEL *frame, int frame_pitch,
					  PIXEL *lowpass, int lowpass_pitch,
					  PIXEL *highpass, int highpass_pitch,
					  ROI roi);

// Apply inverse temporal transform to reconstruct two fields
void InvertInterlaced16s(PIXEL *lowpass, int lowpass_pitch,
						 PIXEL *highpass, int highpass_pitch,
						 PIXEL *even, int even_pitch,
						 PIXEL *odd, int odd_pitch,
						 ROI roi);

void InvertInterlaced16sTo8u(PIXEL16S *lowpass, int lowpass_pitch,
							 PIXEL16S *highpass, int highpass_pitch,
							 PIXEL8U *even_field, int even_pitch,
							 PIXEL8U *odd_field, int odd_pitch, ROI roi);

// Apply the inverse temporal transform to reconstruct an interlaced
// color frame of packed YUV planes from 16-bit signed coefficients
void InvertInterlaced16sToYUV(PIXEL16S *y_lowpass, int y_lowpass_pitch,
							  PIXEL16S *y_highpass, int y_highpass_pitch,
							  PIXEL16S *u_lowpass, int u_lowpass_pitch,
							  PIXEL16S *u_highpass, int u_highpass_pitch,
							  PIXEL16S *v_lowpass, int v_lowpass_pitch,
							  PIXEL16S *v_highpass, int v_highpass_pitch,
							  PIXEL8U *output, int output_pitch, ROI roi);

// Invert the temporal bands from all channels and pack output pixels
void InvertInterlacedRow16sToYUV(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
								 BYTE *output, int pitch, int output_width, int frame_width,
								 int chroma_offset, int format);
#endif


/***** Declarations for the filters that implement the frame transforms *****/


// Apply the frame (temporal and horizontal) transform and quantize to unsigned bytes
// New version that processes data by rows to improve the memory access pattern and
// will encode the quantized coefficients as runs if the transform runs switch is set.
void FilterFrameRuns8u(PIXEL8U *frame, int frame_pitch,
					   PIXEL *lowlow_band, int lowlow_pitch,
					   PIXEL *lowhigh_band, int lowhigh_pitch,
					   PIXEL *highlow_band, int highlow_pitch,
					   PIXEL *highhigh_band, int highhigh_pitch,
					   ROI roi, int input_scale,
					   PIXEL *buffer, size_t buffer_size,
					   int offset,
					   int quantization[IMAGE_NUM_BANDS],
					   int num_runs[IMAGE_NUM_BANDS]);

// Apply the frame (temporal and horizontal) transform and quantize the highpass bands
void FilterFrameQuant16s(PIXEL *frame, int frame_pitch,
						 PIXEL *lowlow_band, int lowlow_pitch,
						 PIXEL *lowhigh_band, int lowhigh_pitch,
						 PIXEL *highlow_band, int highlow_pitch,
						 PIXEL *highhigh_band, int highhigh_pitch,
						 ROI roi, int input_scale,
						 PIXEL *buffer, size_t buffer_size,
						 int offset,
						 int quantization[IMAGE_NUM_BANDS]);

void InvertFrameTo8u(PIXEL *lowlow_band, int lowlow_pitch,
					 PIXEL *lowhigh_band, int lowhigh_pitch,
					 PIXEL *highlow_band, int highlow_pitch,
					 PIXEL *highhigh_band, int highhigh_pitch,
					 PIXEL8U *frame, int frame_pitch, PIXEL *buffer, ROI roi);

//void DequantizeBandRow(PIXEL8S *input, int width, int quantization, PIXEL *output);

void FilterHorizontalDelta(PIXEL *data, int width, int height, int pitch);
double BandEnergy(PIXEL *data, int width, int height, int pitch, int band, int subband);

#ifdef __cplusplus
}
#endif

#endif
