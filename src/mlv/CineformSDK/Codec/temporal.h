/*! @file temporal.h

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

#ifndef _TEMPORAL_H
#define _TEMPORAL_H

#include "image.h"

// Apply the temporal transform between two images
void FilterTemporal(PIXEL *field1, int pitch1, PIXEL *field2, int pitch2,
					PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					ROI roi);

void FilterTemporal16s(PIXEL *field1, int pitch1, PIXEL *field2, int pitch2,
					   PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					   ROI roi);

#if 0
void FilterTemporalQuant16s(PIXEL *field1, int pitch1, PIXEL *field2, int pitch2,
							PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
							ROI roi, PIXEL *buffer, size_t buffer_size, int quantization);
#endif

// Perform the temporal transform on a pair of rows producing 16-bit coefficients
void FilterTemporalRow8uTo16s(PIXEL8U *row1, PIXEL8U *row2, int length,
							  PIXEL16S *lowpass, PIXEL16S *highpass,
							  int offset);

// Perform the temporal transform on a pair of rows of 16-bit coefficients
void FilterTemporalRow16s(PIXEL *row1, PIXEL *row2, int length,
						  PIXEL *lowpass, PIXEL *highpass, int offset);

// Apply the temporal transform to a row of packed YUV pixels
void FilterTemporalRowYUVTo16s(uint8_t *row1, uint8_t *row2, int frame_width,
							   PIXEL *lowpass[], PIXEL *highpass[], int num_channels);

// Apply the temporal transform to one channel in the even and odd rows
void FilterTemporalRowYUYVChannelTo16s(uint8_t *row1, uint8_t *row2, int frame_width,
									   int channel, PIXEL *lowpass, PIXEL *highpass,
									   int offset, int precision, int limit_yuv);

void FilterTemporalRowUYVYChannelTo16s(uint8_t *row1, uint8_t *row2, int frame_width,
									   int channel, PIXEL *lowpass, PIXEL *highpass,
									   int offset, int precision, int limit_yuv);

void InvertTemporal16s(PIXEL *lowpass, int lowpass_pitch, PIXEL *highpass, int highpass_pitch,
					   PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi);

void InvertTemporalQuant16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch,
							PIXEL *highpass, int highpass_quantization, int highpass_pitch,
							PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi,
							PIXEL *buffer, size_t buffer_size, int precision);

void FilterInterlaced(PIXEL *frame, int frame_pitch,
					  PIXEL *lowpass, int lowpass_pitch,
					  PIXEL *highpass, int highpass_pitch,
					  ROI roi);

void InvertInterlaced16s(PIXEL *lowpass, int lowpass_pitch,
						 PIXEL *highpass, int highpass_pitch,
						 PIXEL *even, int even_pitch,
						 PIXEL *odd, int odd_pitch,
						 ROI roi);

void InvertInterlaced16sTo8u(PIXEL16S *lowpass, int lowpass_pitch,
							 PIXEL16S *highpass, int highpass_pitch,
							 PIXEL8U *even_field, int even_pitch,
							 PIXEL8U *odd_field, int odd_pitch, ROI roi);

void InvertInterlaced16sToYUV(PIXEL16S *y_lowpass, int y_lowpass_pitch,
							  PIXEL16S *y_highpass, int y_highpass_pitch,
							  PIXEL16S *u_lowpass, int u_lowpass_pitch,
							  PIXEL16S *u_highpass, int u_highpass_pitch,
							  PIXEL16S *v_lowpass, int v_lowpass_pitch,
							  PIXEL16S *v_highpass, int v_highpass_pitch,
							  PIXEL8U *output, int output_pitch, ROI roi);

void InvertInterlacedRow16sToYUV(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
								 uint8_t *output, int pitch, int output_width, int frame_width,
								 int chroma_offset, int format);

// Invert the temporal bands from all channels and pack the output pixels into YUV format
void InvertInterlacedRow16s10bitToYUV(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
									  uint8_t *output, int pitch, int output_width, int frame_width,
									  int chroma_offset);

// Invert the temporal bands from all channels and pack the output pixels into UYVY format
void InvertInterlacedRow16s10bitToUYVY(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
									   uint8_t *output, int pitch, int output_width, int frame_width,
									   int chroma_offset);

void InvertInterlacedRow16sToRow16u(PIXEL *lowpass, PIXEL *highpass, PIXEL16U *output, int pitch,
									int output_width, int frame_width, int chroma_offset, int precision);

void InvertInterlacedRow16s(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
							uint8_t *output, int pitch, int output_width, int frame_width,
							char *buffer, size_t buffer_size,
							int format, int colorspace, int chroma_offset, int precision, int row);

void InvertInterlacedRow16sToV210(PIXEL *lowpass[], PIXEL *highpass[], int num_channels,
								  uint8_t *output, int pitch, int output_width, int frame_width,
								  char *buffer, size_t buffer_size, int format, int chroma_offset, int precision);

void InvertTemporal16s8sTo16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch,
							  PIXEL8S *highpass, int highpass_quantization, int highpass_pitch,
							  PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi);

void InvertTemporalQuant16s8sTo16s(PIXEL *lowpass, int lowpass_quantization, int lowpass_pitch,
								   PIXEL8S *highpass, int highpass_quantization, int highpass_pitch,
								   PIXEL *field1, int pitch1, PIXEL *field2, int pitch2, ROI roi,
								   PIXEL *buffer, size_t buffer_size);

void InvertTemporalRow16s(PIXEL *lowpass, PIXEL *highpass,
						  PIXEL *even, PIXEL *odd, int width);

void InvertTemporalRow16s8sTo16s(PIXEL *lowpass, PIXEL8S *highpass,
								 PIXEL *even, PIXEL *odd, int width);

// Invert the temporal transform at quarter resolution
void InvertTemporalQuarterRow16s(PIXEL *lowpass, PIXEL *highpass, PIXEL *even, PIXEL *odd, int width);

// Invert the temporal transform at quarter resolution and return the even row
void InvertTemporalQuarterEvenRow16s(PIXEL *lowpass, PIXEL *highpass, PIXEL *output, int width, int precision);

// Invert the temporal transform at quarter resolution and return the odd row
void InvertTemporalQuarterOddRow16s(PIXEL *lowpass, PIXEL *highpass, PIXEL *output, int width, int precision);

// Descale and pack the pixels in each output row
void CopyQuarterRowToBuffer(PIXEL **input, int num_channels, uint8_t *output, int width, int precision, int format);


#endif
