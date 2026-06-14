/*! @file spatial.h

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
#ifndef _SPATIAL_H
#define _SPATIAL_H

//#include "ipp.h"
#include "image.h"
#include "filter.h"
#include "codec.h"

// Forward reference
//typedef struct encoder ENCODER;
struct encoder;

#ifdef __cplusplus
extern "C" {
#endif

// Apply the lowpass and highpass horizontal filters to one row
void FilterHorizontalRow16s(PIXEL *input, PIXEL *lowpass, PIXEL *highpass, int width);

// Apply the lowpass and highpass horizontal filters to one row and scale the results
void FilterHorizontalRowScaled16s(PIXEL *input, PIXEL *lowpass, PIXEL *highpass,
								  int width, int lowpass_scale, int highpass_scale);

// Apply the lowpass and highpass horizontal filters to one row and scale the results
void FilterHorizontalRowScaled16sDifferenceFiltered(PIXEL *input, PIXEL *lowpass, PIXEL *highpass,
					  int width, int lowpass_scale, int highpass_scale, int lp_divisor);

// Apply the lowpass and highpass horizontal filters to one row and scale the results
void FilterHorizontalRowQuant16s(PIXEL *input, PIXEL *lowpass, PIXEL *highpass,
								  int width, int lowpass_div, int highpass_div);

// Apply the lowpass and highpass horizontal filters to one row after prescaling
void FilterHorizontalRowYUV16s(uint8_t *input, PIXEL *lowpass, PIXEL *highpass,
							   int width, int channel, PIXEL *buffer, size_t buffer_size,
							   FRAME_INFO *frame, int precision, int limit_yuv, int conv_601_709);

void FilterHorizontalRowBYR3_16s(PIXEL *input, PIXEL *lowpass, PIXEL *highpass, int width);


void InvertHorizontalQuant16sBuffered(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch,
									 PIXEL *highpass, int highpass_quantization, int highpass_pitch,
									 PIXEL *output, int output_pitch,
									 ROI roi, bool fastmode, PIXEL *buffer);

void InvertVertical16s(PIXEL *lowpass, int lowpass_pitch,
					   PIXEL *highpass, int highpass_pitch,
					   PIXEL *output, int output_pitch,
					   ROI roi);

// Apply the lowpass and highpass horizontal filters to the input image
void FilterHorizontal(PIXEL *input, int input_pitch,
					  PIXEL *lowpass, int lowpass_pitch,
					  PIXEL *highpass, int highpass_pitch,
					  ROI roi, int input_scale);

// Apply a horizontal filter to the rows and downsample the result by two
// with optimizations for the lowpass coefficients used in the 2/6 wavelet
void FilterLowpassHorizontal(PIXEL *input, int input_pitch, PIXEL *output, int output_pitch, ROI roi, int prescale);
void FilterLowpassHorizontal8s(PIXEL8S *input, int input_pitch, PIXEL *output, int output_pitch, ROI roi, int prescale);

// This version performs highpass convolution and downsampling in one pass
// and is optimized for the highpass coefficients used in the 2/6 wavelet
void FilterHighpassHorizontal(PIXEL *input, int input_pitch,
							  PIXEL *output, int output_pitch,
							  ROI roi, int input_scale, int prescale);
void FilterHighpassHorizontal8s(PIXEL8S *input, int input_pitch,
							    PIXEL *output, int output_pitch,
							    ROI roi, int input_scale, int prescale);

// Vertical filters optimized for the coefficients of the 2/6 wavelet transform
void FilterVertical(PIXEL *input, int input_pitch,
					PIXEL *lowpass, int lowpass_pitch,
					PIXEL *highpass, int highpass_pitch,
					ROI roi);

void FilterLowpassVertical(PIXEL *input, int input_pitch, PIXEL *output, int output_pitch, ROI roi);
void FilterHighpassVertical(PIXEL *input, int input_pitch, PIXEL *output, int output_pitch, ROI roi);
void FilterLowpassVerticalScaled(PIXEL *input, int input_pitch, PIXEL *output, int output_pitch, ROI roi);

// Optimized even reconstruction filter
void FilterEvenHorizontal(PIXEL *input, int input_pitch, PIXEL *output, int output_pitch, ROI roi);

// Optimized odd reconstruction filter
void FilterOddHorizontal(PIXEL *input, int input_pitch, PIXEL *output, int output_pitch, ROI roi);


// Optimized horizontal reconstruction filters that combine lowpass filtering with
// addition or subtraction of the highpass correction and interleaving the columns

void FilterEvenLowHigh(PIXEL *lowpass, int lowpass_pitch,
					   PIXEL *highpass, int highpass_pitch, int highpass_border,
					   PIXEL *output, int output_pitch, ROI roi);

void FilterOddLowHigh(PIXEL *lowpass, int lowpass_pitch,
					  PIXEL *highpass, int highpass_pitch, int highpass_border,
					  PIXEL *output, int output_pitch, ROI roi);

void InvertHorizontalRow16s(PIXEL *lowpass, //int lowpass_quantization,
							PIXEL *highpass, //int highpass_quantization,
							PIXEL *output, int width);

void BypassHorizontalRow16s(PIXEL *lowpass,  	// Row of horizontal lowpass coefficients
							PIXEL *highpass,	// Row of horizontal highpass coefficients
							PIXEL *output,		// Row of reconstructed results
							int width			// Length of each row of horizontal coefficients
							);

void InvertHorizontalRow16s8sTo16s(PIXEL *lowpass, int lowpass_quantization,
								   PIXEL8S *highpass, int highpass_quantization,
								   PIXEL *output, int width);

void InvertHorizontalRow16s8sTo16sBuffered(PIXEL *lowpass, int lowpass_quantization,
										   PIXEL8S *highpass, int highpass_quantization,
										   PIXEL *output, int width, PIXEL *buffer);
#if _DEBUG
void InvertHorizontalRowDuplicated16s(PIXEL *lowpass, int lowpass_quantization,
										   PIXEL8S *highpass, int highpass_quantization,
										   PIXEL *output, int width, PIXEL *buffer);
#endif

void InvertHorizontalRow8sBuffered(PIXEL8S *lowpass, int lowpass_quantization,
								   PIXEL8S *highpass, int highpass_quantization,
								   PIXEL *output, int width, PIXEL *buffer);

void InvertHorizontalStrip16s(PIXEL *lowpass_band, int lowpass_pitch,
							  PIXEL *highpass_band, int highpass_pitch,
							  PIXEL *output_image, int output_pitch,
							  ROI roi);

// Apply the inverse horizontal transform to reconstruct a strip of rows into packed YUV
void InvertHorizontalStrip16sToYUV(HorizontalFilterParams);


//! Apply the inverse horizontal transform to reconstruct a strip of rows to the specified output format
void InvertHorizontalStrip16sToOutput(HorizontalFilterParams);


//! Apply the inverse horizontal transform to reconstruct a strip of rows to the specified output format
void InvertHorizontalYUVStrip16sToYUVOutput(HorizontalFilterParams);

// Apply the inverse horizontal transform to reconstruct a strip of rows into packed YUYV
void InvertHorizontalStrip16sToYUYV(PIXEL *lowpass_band[],	// Horizontal lowpass coefficients
									int lowpass_pitch[],	// Distance between rows in bytes
									PIXEL *highpass_band[],	// Horizontal highpass coefficients
									int highpass_pitch[],	// Distance between rows in bytes
									uint8_t *output_image,		// Row of reconstructed results
									int output_pitch,		// Distance between rows in bytes
									ROI roi,				// Height and width of the strip
									int precision);			// Precision of the original video

// Apply the inverse horizontal transform to reconstruct a strip of rows into packed UYVY
void InvertHorizontalStrip16sToUYVY(PIXEL *lowpass_band[],	// Horizontal lowpass coefficients
									int lowpass_pitch[],	// Distance between rows in bytes
									PIXEL *highpass_band[],	// Horizontal highpass coefficients
									int highpass_pitch[],	// Distance between rows in bytes
									uint8_t *output_image,		// Row of reconstructed results
									int output_pitch,		// Distance between rows in bytes
									ROI roi,				// Height and width of the strip
									int precision);			// Precision of the original video

void InvertHorizontalStrip16sToBayerYUV(HorizontalFilterParams);

void InvertHorizontalStrip16sRGB2YUV(HorizontalFilterParams);

void InvertHorizontalStrip16sRGBA2YUVA(HorizontalFilterParams);

void InvertHorizontalStrip16sRGB2YR16(HorizontalFilterParams);

void InvertHorizontalStrip16sRGB2v210(HorizontalFilterParams);

void InvertHorizontalStrip16sRGB2RG30(HorizontalFilterParams);

void InvertHorizontalStrip16sRGB2B64A(HorizontalFilterParams);

void InvertHorizontalStrip16sRGB2r210(HorizontalFilterParams);

void InvertHorizontalStrip16sYUVtoRGB(HorizontalFilterParams);

void InvertHorizontalStrip16sThruActiveMetadata(HorizontalFilterParams);

void InvertHorizontalStrip16sToBayer_3DLUT_YUV(HorizontalFilterParams);

void InvertHorizontalStrip16sToRow16u(PIXEL *lowpass_band, int lowpass_pitch,
									  PIXEL *highpass_band, int highpass_pitch,
									  PIXEL16U *output, int output_pitch, ROI roi,
									  int precision);


void InvertSpatialTopRow16s(PIXEL *lowlow_band, int lowlow_pitch,
	PIXEL *lowhigh_band, int lowhigh_pitch,
	PIXEL *highlow_band, int highlow_pitch,
	PIXEL *highhigh_band, int highhigh_pitch,
	uint8_t *output, int output_pitch,
	int row, int width,
	PIXEL *buffer, size_t buffer_size,
	int precision, FRAME_INFO *info);

#if 0
void InvertHorizontalStripRGB444ToB64A(PIXEL *lowpass_band, int lowpass_pitch,
									   PIXEL *highpass_band, int highpass_pitch,
									   PIXEL16U *output, int output_pitch, ROI roi,
									   int precision);
#endif

// Apply the forward spatial (horizontal and vertical) transform with quantization
void FilterSpatialQuant16s(PIXEL *input_image, int input_pitch,
						   PIXEL *lowlow_band, int lowlow_pitch,
						   PIXEL *lowhigh_band, int lowhigh_pitch,
						   PIXEL *highlow_band, int highlow_pitch,
						   PIXEL *highhigh_band, int highhigh_pitch,
						   PIXEL *buffer, size_t buffer_size,
						   ROI roi, int quantization[4]);

// Apply the forward spatial (horizontal and vertical) transform with quantization differencing the LL band
void FilterSpatialQuantDifferenceLL16s(PIXEL *input_image, int input_pitch,
						   PIXEL *lowlow_band, int lowlow_pitch,
						   PIXEL *lowhigh_band, int lowhigh_pitch,
						   PIXEL *highlow_band, int highlow_pitch,
						   PIXEL *highhigh_band, int highhigh_pitch,
						   PIXEL *buffer, size_t buffer_size,
						   ROI roi, int quantization[4]);

// Compute the forward spatial transform with prescaling and quantization
void FilterSpatialPrescaleQuant16s(PIXEL *input_image, int input_pitch,
								   PIXEL *lowlow_band, int lowlow_pitch,
								   PIXEL *lowhigh_band, int lowhigh_pitch,
								   PIXEL *highlow_band, int highlow_pitch,
								   PIXEL *highhigh_band, int highhigh_pitch,
								   PIXEL *buffer, size_t buffer_size, ROI roi,
								   int quantization[4]);

// Forward spatial wavelet transform with prescaling for 10-bit video sources
void FilterSpatialV210Quant16s(PIXEL *input_image, int input_pitch,
							   PIXEL *lowlow_band, int lowlow_pitch,
							   PIXEL *lowhigh_band, int lowhigh_pitch,
							   PIXEL *highlow_band, int highlow_pitch,
							   PIXEL *highhigh_band, int highhigh_pitch,
							   PIXEL *buffer, size_t buffer_size, ROI roi,
							   int quantization[4]);


void FilterSpatialQuant16sToCoded(struct encoder *encoder,
								  PIXEL *input_image, int input_pitch,
								  PIXEL *lowlow_band, int lowlow_pitch,
								  PIXEL *lowhigh_band, int lowhigh_pitch,
								  PIXEL *highlow_band, int highlow_pitch,
								  PIXEL *highhigh_band, int highhigh_pitch,
								  PIXEL *buffer, size_t buffer_size, ROI roi,
								  int quantization[4], int coded_size[4]);

void FilterSpatialYUVQuant16s(uint8_t *input_data, int input_pitch,
							  PIXEL *lowlow_band, int lowlow_pitch,
							  PIXEL *lowhigh_band, int lowhigh_pitch,
							  PIXEL *highlow_band, int highlow_pitch,
							  PIXEL *highhigh_band, int highhigh_pitch,
							  PIXEL *buffer, size_t buffer_size, ROI roi,
							  int channel, int quantization[4], FRAME_INFO *frame, 
							  int precision, int limit_yuv, int conv_601_709);

// Forward spatial (horizontal and vertical) transform
void FilterSpatial8s(PIXEL *input_image, int input_pitch,
					 PIXEL *lowlow_band, int lowlow_pitch,
					 PIXEL *lowhigh_band, int lowhigh_pitch,
					 PIXEL *highlow_band, int highlow_pitch,
					 PIXEL *highhigh_band, int highhigh_pitch,
					 ROI roi, int input_scale);


void InvertSpatialMiddleRow16s(PIXEL *lowlow_band, int lowlow_pitch,
							   PIXEL *lowhigh_band, int lowhigh_pitch,
							   PIXEL *highlow_band, int highlow_pitch,
							   PIXEL *highhigh_band, int highhigh_pitch,
							   uint8_t *output, int output_pitch,
							   int row, int width,
							   PIXEL *buffer, size_t buffer_size,
							   int precision, FRAME_INFO *info);

void InvertSpatialBottomRow16s(PIXEL *lowlow_band, int lowlow_pitch,
							   PIXEL *lowhigh_band, int lowhigh_pitch,
							   PIXEL *highlow_band, int highlow_pitch,
							   PIXEL *highhigh_band, int highhigh_pitch,
							   uint8_t *output, int output_pitch,
							   int row, int width,
							   PIXEL *buffer, size_t buffer_size,
							   int precision, FRAME_INFO *info);

void InvertSpatialTopRow10bit16s(PIXEL *lowlow_band, int lowlow_pitch,
								 PIXEL *lowhigh_band, int lowhigh_pitch,
								 PIXEL *highlow_band, int highlow_pitch,
								 PIXEL *highhigh_band, int highhigh_pitch,
								 PIXEL *output, int output_pitch,
								 int row, int width,
								 PIXEL *buffer, size_t buffer_size);

void InvertSpatialMiddleRow10bit16s(PIXEL *lowlow_band, int lowlow_pitch,
									PIXEL *lowhigh_band, int lowhigh_pitch,
									PIXEL *highlow_band, int highlow_pitch,
									PIXEL *highhigh_band, int highhigh_pitch,
									PIXEL *output, int output_pitch,
									int row, int width,
									PIXEL *buffer, size_t buffer_size);

void InvertSpatialBottomRow10bit16s(PIXEL *lowlow_band, int lowlow_pitch,
									PIXEL *lowhigh_band, int lowhigh_pitch,
									PIXEL *highlow_band, int highlow_pitch,
									PIXEL *highhigh_band, int highhigh_pitch,
									PIXEL *output, int output_pitch,
									int row, int width,
									PIXEL *buffer, size_t buffer_size);

void InvertSpatialTopRow16sToYUV16(PIXEL *lowlow_band, int lowlow_pitch,
								   PIXEL *lowhigh_band, int lowhigh_pitch,
								   PIXEL *highlow_band, int highlow_pitch,
								   PIXEL *highhigh_band, int highhigh_pitch,
								   PIXEL16U *output, int output_pitch,
								   int row, int width,
								   PIXEL *buffer, size_t buffer_size,
								   int precision);

void InvertSpatialMiddleRow16sToYUV16(PIXEL *lowlow_band, int lowlow_pitch,
									  PIXEL *lowhigh_band, int lowhigh_pitch,
									  PIXEL *highlow_band, int highlow_pitch,
									  PIXEL *highhigh_band, int highhigh_pitch,
									  PIXEL16U *output, int output_pitch,
									  int row, int width,
									  PIXEL *buffer, size_t buffer_size,
									  int precision);

void InvertSpatialBottomRow16sToYUV16(PIXEL *lowlow_band, int lowlow_pitch,
									  PIXEL *lowhigh_band, int lowhigh_pitch,
									  PIXEL *highlow_band, int highlow_pitch,
									  PIXEL *highhigh_band, int highhigh_pitch,
									  PIXEL16U *output, int output_pitch,
									  int row, int width,
									  PIXEL *buffer, size_t buffer_size,
									  int precision);

void InvertRGB444TopRow16sToB64A(PIXEL *lowlow_band, int lowlow_pitch,
								 PIXEL *lowhigh_band, int lowhigh_pitch,
								 PIXEL *highlow_band, int highlow_pitch,
								 PIXEL *highhigh_band, int highhigh_pitch,
								 PIXEL16U *output, int output_pitch,
								 int row, int width,
								 PIXEL *buffer, size_t buffer_size,
								 int precision);

void InvertRGB444MiddleRow16sToB64A(PIXEL *lowlow_band, int lowlow_pitch,
									PIXEL *lowhigh_band, int lowhigh_pitch,
									PIXEL *highlow_band, int highlow_pitch,
									PIXEL *highhigh_band, int highhigh_pitch,
									PIXEL16U *output, int output_pitch,
									int row, int width,
									PIXEL *buffer, size_t buffer_size,
									int precision);

void InvertRGB444BottomRow16sToB64A(PIXEL *lowlow_band, int lowlow_pitch,
									PIXEL *lowhigh_band, int lowhigh_pitch,
									PIXEL *highlow_band, int highlow_pitch,
									PIXEL *highhigh_band, int highhigh_pitch,
									PIXEL16U *output, int output_pitch,
									int row, int width,
									PIXEL *buffer, size_t buffer_size,
									int precision);

#if 1

void InvertSpatialQuantOverflowProtected16s(PIXEL *lowlow_band, int lowlow_pitch,
											PIXEL *lowhigh_band, int lowhigh_pitch,
											PIXEL *highlow_band, int highlow_pitch,
											PIXEL *highhigh_band, int highhigh_pitch,
											PIXEL *output_image, int output_pitch,
											ROI roi, PIXEL *buffer, size_t buffer_size,
											int quantization[] /*, PIXEL *line_buffer */);

void InvertSpatialQuant16s(PIXEL *lowlow_band, int lowlow_pitch,
						  PIXEL *lowhigh_band, int lowhigh_pitch,
						  PIXEL *highlow_band, int highlow_pitch,
						  PIXEL *highhigh_band, int highhigh_pitch,
						  PIXEL *output_image, int output_pitch,
						  ROI roi, PIXEL *buffer, size_t buffer_size,
						  int quantization[]);

void InvertSpatialQuantDescale16s(PIXEL *lowlow_band, int lowlow_pitch,
								  PIXEL *lowhigh_band, int lowhigh_pitch,
								  PIXEL *highlow_band, int highlow_pitch,
								  PIXEL *highhigh_band, int highhigh_pitch,
								  PIXEL *output_image, int output_pitch,
								  ROI roi, PIXEL *buffer, size_t buffer_size,
								  int prescale, int quantization[]);

void InvertSpatialPrescaledQuant16s(PIXEL *lowlow_band, int lowlow_pitch,
									PIXEL *lowhigh_band, int lowhigh_pitch,
									PIXEL *highlow_band, int highlow_pitch,
									PIXEL *highhigh_band, int highhigh_pitch,
									PIXEL *output_image, int output_pitch,
									ROI roi, PIXEL *buffer, size_t buffer_size,
									int quantization[]);

void InvertSpatialQuant1x16s(PIXEL *lowlow_band, int lowlow_pitch,
							 PIXEL *lowhigh_band, int lowhigh_pitch,
							 PIXEL *highlow_band, int highlow_pitch,
							 PIXEL *highhigh_band, int highhigh_pitch,
							 PIXEL *output_image, int output_pitch,
							 ROI roi, PIXEL *buffer, size_t buffer_size,
							 int quantization[]);
#else

void InvertSpatial8sBuffered(PIXEL8S *lowlow_band, int lowlow_pitch,
							 PIXEL8S *lowhigh_band, int lowhigh_pitch,
							 PIXEL8S *highlow_band, int highlow_pitch,
							 PIXEL8S *highhigh_band, int highhigh_pitch,
							 PIXEL *output_image, int output_pitch,
							 ROI roi, PIXEL *buffer, int quantization[],
							 PIXEL *line_buffer);
#endif

// Apply the inverse transform on coefficients that were prescaled
void InvertSpatialPrescaledQuant8s(PIXEL8S *lowlow_band, int lowlow_pitch,
								   PIXEL8S *lowhigh_band, int lowhigh_pitch,
								   PIXEL8S *highlow_band, int highlow_pitch,
								   PIXEL8S *highhigh_band, int highhigh_pitch,
								   PIXEL *output_image, int output_pitch,
								   ROI roi, PIXEL *buffer, size_t buffer_size,
								   int quantization[]);

void InvertSpatial8sTo16s(PIXEL8S *lowlow_band, int lowlow_pitch,
						  PIXEL8S *lowhigh_band, int lowhigh_pitch,
						  PIXEL8S *highlow_band, int highlow_pitch,
						  PIXEL8S *highhigh_band, int highhigh_pitch,
						  PIXEL16S *output_image, int output_pitch,
						  ROI roi, PIXEL *buffer, int quantization[],
						  PIXEL *line_buffer);

void InvertSpatial16sTo16s(PIXEL16S *lowlow_band, int lowlow_pitch,
						  PIXEL16S *lowhigh_band, int lowhigh_pitch,
						  PIXEL16S *highlow_band, int highlow_pitch,
						  PIXEL16S *highhigh_band, int highhigh_pitch,
						  PIXEL16S *output_image, int output_pitch,
						  ROI roi, PIXEL *buffer, int quantization[],
						  PIXEL *line_buffer);

void FilterHorizontalRowRGB30_16s(PIXEL *input, PIXEL *lowpass, PIXEL *highpass, int width, int precision, int format);


// Filters that invert the spatial transform and convert the results into 8-bit YUV
void
InvertSpatialTopRow16sToPackedYUV8u(PIXEL *lowlow_band[], int lowlow_band_pitch[],
									PIXEL *lowhigh_band[], int lowhigh_band_pitch[],
									PIXEL *highlow_band[], int highlow_band_pitch[],
									PIXEL *highhigh_band[], int highhigh_band_pitch[],
									uint8_t *output, int output_pitch, int output_width,
									int format, int row, int luma_band_width,
									PIXEL *buffer, size_t buffer_size, int precision);

void
InvertSpatialBottomRow16sToPackedYUV8u(PIXEL *lowlow_band[], int lowlow_band_pitch[],
									   PIXEL *lowhigh_band[], int lowhigh_band_pitch[],
									   PIXEL *highlow_band[], int highlow_band_pitch[],
									   PIXEL *highhigh_band[], int highhigh_band_pitch[],
									   uint8_t *output, int output_pitch, int output_width,
									   int format, int row, int luma_band_width,
									   PIXEL *buffer, size_t buffer_size, int precision);

void
InvertHorizontalStripRGB16sToPackedYUV8u(PIXEL *lowpass_band[],	// Horizontal lowpass coefficients
										 int lowpass_pitch[],		// Distance between rows in bytes
										 PIXEL *highpass_band[],	// Horizontal highpass coefficients
										 int highpass_pitch[],		// Distance between rows in bytes
										 uint8_t *output_image,		// Row of reconstructed results
										 int output_pitch,			// Distance between rows in bytes
										 ROI roi,					// Height and width of the strip
										 int precision);			// Precision of the original video

void InvertHorizontalStripYUV16sToPackedRGB32(HorizontalFilterParams);		// Target pixel format

void InvertHorizontalStrip16sToRow16uPlanar(HorizontalFilterParams);		// Target pixel format

// Filters that invert the spatial transform and produce an arbitrary output format
void
InvertSpatialTopRow16sToOutput(DECODER *decoder, int thread_index, PIXEL *lowlow_band[], int lowlow_band_pitch[],
							   PIXEL *lowhigh_band[], int lowhigh_band_pitch[],
							   PIXEL *highlow_band[], int highlow_band_pitch[],
							   PIXEL *highhigh_band[], int highhigh_band_pitch[],
							   uint8_t *output, int output_pitch, int output_width,
							   int format, int colorspace, int row, int channel_width[],
							   PIXEL *buffer, size_t buffer_size, int precision,
							   HorizontalInverseFilterOutputProc horizontal_filter_proc);

void
InvertSpatialMiddleRow16sToOutput(DECODER *decoder, int thread_index, PIXEL *lowlow_band[], int lowlow_band_pitch[],
								  PIXEL *lowhigh_band[], int lowhigh_band_pitch[],
								  PIXEL *highlow_band[], int highlow_band_pitch[],
								  PIXEL *highhigh_band[], int highhigh_band_pitch[],
								  uint8_t *output, int output_pitch, int output_width,
								  int format, int colorspace, int row, int channel_width[],
								  PIXEL *buffer, size_t buffer_size, int precision,
								  HorizontalInverseFilterOutputProc horizontal_filter_proc,
								  int outputlines);

void
InvertSpatialBottomRow16sToOutput(DECODER *decoder, int thread_index, PIXEL *lowlow_band[], int lowlow_band_pitch[],
								  PIXEL *lowhigh_band[], int lowhigh_band_pitch[],
								  PIXEL *highlow_band[], int highlow_band_pitch[],
								  PIXEL *highhigh_band[], int highhigh_band_pitch[],
								  uint8_t *output, int output_pitch, int output_width,
								  int format, int colorspace, int row, int channel_width[],
								  PIXEL *buffer, size_t buffer_size, int precision, int odd_height,
								  HorizontalInverseFilterOutputProc horizontal_filter_proc);

void InvertHorizontalStrip16s10bitLimit(PIXEL *lowpass_band,	// Horizontal lowpass coefficients
							  int lowpass_pitch,	// Distance between rows in bytes
							  PIXEL *highpass_band,	// Horizontal highpass coefficients
							  int highpass_pitch,	// Distance between rows in bytes
							  PIXEL *output_image,	// Row of reconstructed results
							  int output_pitch,		// Distance between rows in bytes
							  ROI roi);				// Height and width of the strip

void InvertHorizontalStripDescale16s(PIXEL *lowpass_band, int lowpass_pitch,
									 PIXEL *highpass_band, int highpass_pitch,
									 PIXEL *output_image, int output_pitch,
									 ROI roi, int descale);


void InvertHorizontalStrip1x16s(PIXEL *lowpass_band, int lowpass_pitch,
								PIXEL *highpass_band, int highpass_pitch,
								PIXEL *output_image, int output_pitch,
								ROI roi);


void InvertHorizontalStripPrescaled16s(PIXEL *lowpass_band,		// Horizontal lowpass coefficients
									   int lowpass_pitch,		// Distance between rows in bytes
									   PIXEL *highpass_band,	// Horizontal highpass coefficients
									   int highpass_pitch,		// Distance between rows in bytes
									   PIXEL *output_image,		// Row of reconstructed results
									   int output_pitch,		// Distance between rows in bytes
									   ROI roi);				// Height and width of the strip


#ifdef __cplusplus
}
#endif

#endif
