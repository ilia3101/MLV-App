/*! @file Convert.h

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

# pragma once

// Include the definitions used for the color flags argument
#include "ColorFlags.h"
#include "../Codec/codec.h"

#ifdef __cplusplus

// New image scaling and color conversion routines
#include "MemAlloc.h"
#include "ImageConverter.h"
#include "ImageScaler.h"
#include "Bilinear.h"

#endif

#if _WIN32
#include "ConvertYUV8.h"
#endif


#ifdef __cplusplus
extern "C" {
#endif
	
typedef void (*GammaFunc)( unsigned char *outputBuffer, int width, int to2point2 );

void ConvertYU64ToARGB32(unsigned char *input, long input_pitch,
							 unsigned char *output, long output_pitch,
							 int width, int height, int alpha, int flags);
	
void ConvertYU64ToARGB64(unsigned char *input, long input_pitch,
						 unsigned char *output, long output_pitch,
						 int width, int height,
						 int color_flags, int pixel_size,
						 int component_size, int alpha);

void ConvertYU64ToRGBRow( unsigned char *input, long input_pitch,
						 unsigned char *output, long output_pitch,
						 int width, int height,
						 int color_flags, int pixel_size,
						 int component_size, int alpha,
					     int byteOrder);

void ConvertYU64ToARGB64Float(unsigned char *input, long input_pitch,
					 unsigned char *output, long output_pitch,
					 int width, int height,
					 int color_flags, int pixel_size,
					 int alpha);

void ScaleYU64ToARGB64(unsigned char *input, long input_pitch,
					   unsigned char *output, long output_pitch,
					   int width, int height,
					   int color_flags, int pixel_size,
					   int alpha);

void ConvertYU64ToRGB48(unsigned char *input, long input_pitch,
						 unsigned char *output, long output_pitch,
						 int width, int height,
						 int color_flags, int pixel_size, int component_size);

void ConvertYU64ToRGB48Float(unsigned char *input, long input_pitch,
							 unsigned char *output, long output_pitch,
							 int width, int height,
							 int color_flags, int pixel_size, int component_size);

void ConvertYU64ToUYVY(unsigned char *input, long input_pitch,
						   unsigned char *output, long output_pitch,
						   int width, int height);
	
void ConvertV210ToYU64Row(unsigned char *input, 
						   unsigned char *output, 
						   int width);
	
void ConvertRGB24ToRGB48(unsigned char *input, long input_pitch,
						 unsigned char *output, long output_pitch,
						 int width, int height,
						 int color_flags, int pixel_size);

void ConvertRGB24ToQuickTime(unsigned char *input, long input_pitch,
							 unsigned char *output, long output_pitch,
							 int width, int height);

void ConvertRGB32ToQuickTimeRow(unsigned char *input,
							 unsigned char *output, long width,
							 int order, int gammaChoice,
							 void (*GammaFixRGBA)( unsigned char *outputBuffer, int width, int to2point2 ));
void ConvertRGB32ToQuickTime(unsigned char *input, long input_pitch,
							 unsigned char *output, long output_pitch,
							 int width, int height, int order
#ifndef _WIN32
							 , int gammaChoice,
							 void (*GammaFixRGBA)( unsigned char *outputBuffer, int width, int to2point2 )
#endif
							 );

void ConvertYUVToQuickTimeRow(unsigned char *input, 
							  unsigned char *output, 
							  int width);

void ConvertYUVToQuickTime(unsigned char *input, long input_pitch,
						   unsigned char *output, long output_pitch,
						   int width, int height);

void ConvertQuickTimeARGB64ToBGRARow(unsigned char *input,
									 unsigned char *output,
									 int width, int gammaChoice,
									 void (*GammaFixRGBA)( unsigned char *outputBuffer, int width, int to2point2 ));

void ConvertQuickTimeARGB64ToBGRA(unsigned char *input, long input_pitch,
								  unsigned char *output, long output_pitch,
								  int width, int height);

void ConvertQuickTimeARGB64ToR4FLRow(unsigned char *input,
									 unsigned char *output,
									 int width, int whitepoint);

void ConvertQuickTimeW13AToR4FLRow(unsigned char *input,
									 unsigned char *output,
									 int width, int whitepoint);

void ConvertQuickTimeARGB64ToR4FL(unsigned char *input, long input_pitch,
								  unsigned char *output, long output_pitch,
								  int width, int height, int whitepoint);

void ConvertYU64ToV216(unsigned char *input, long input_pitch,
					   unsigned char *output, long output_pitch,
					   int width, int height);

void ConvertYU64ToV410(unsigned char *input, long input_pitch,
					   unsigned char *output, long output_pitch,
					   int width, int height);

void ConvertYU64ToFloatYUVA(unsigned char *input, long input_pitch,
							unsigned char *output, long output_pitch,
							int width, int height);

void ConvertFloatYUVAToARGB64(unsigned char *input, long input_pitch,
							  unsigned char *output, long output_pitch,
							  int width, int height);
#if _DEBUG

void FillYU64(unsigned char *input_buffer, long input_pitch,
			  int width, int height);
#endif


#ifdef __cplusplus
}
#endif
