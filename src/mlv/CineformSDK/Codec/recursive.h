/*! @file recursive.h

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

#ifndef _RECURSIVE_H
#define _RECURSIVE_H

#include "config.h"
#include "image.h"
#include "encoder.h"
#include "wavelet.h"

void InitTransformState(TRANSFORM_STATE *state, TRANSFORM *transform);
void ClearTransformState(TRANSFORM_STATE *state);

// Copy the quantization divisors into the transform state
//void SetTransformStateQuantization(TRANSFORM *transform);

// Initialize the vector of descriptors that specifies the sequence of transforms
void SetTransformDescriptors(ENCODER *encoder, TRANSFORM *transform);

// Allocate processing buffers using the input level and input dimensions
BYTE *AllocateTransformStateBuffers(TRANSFORM_STATE *state, int width, int height,
									int level, int type, BYTE *buffer);

// Called with the next row to perform the recursive wavelet transform
void FilterSpatialRecursiveRow(TRANSFORM_STATE *state, PIXEL *input, int width, BYTE *buffer, FILE *logfile);

// Initialize the encoder
//void InitEncoder(ENCODER *encoder, FILE *logfile, int width, int height, int num_levels, BYTE *buffer);

// Allocate data structures for the recursive wavelet transforms
BYTE *AllocateRecursiveTransform(TRANSFORM *transform, int width, int height, int num_levels, BYTE *buffer);
//void AllocateRecursiveWavelets(ENCODER *encoder, BYTE *buffer);

// Entry point for beginning the recursive wavelet transform
void FilterSpatialRecursive(TRANSFORM *transform, PIXEL *image, int width, int height, int pitch, BYTE *buffer);

// Entry point for the intermediate levels in the recursive wavelet transform
//void FilterSpatialRecursiveAux(TRANSFORM *transform, PIXEL *input, int width, int level, BYTE *buffer);
void FilterRecursive(TRANSFORM *transform, PIXEL *input, int width, int level, BYTE *buffer);

//void OutputWaveletBandRows(ENCODER *encoder, PIXEL *wavelet[NUM_WAVELET_BANDS], int width, int level);
void StoreWaveletBandRows(TRANSFORM *transform, PIXEL *result[NUM_WAVELET_BANDS], int width, int level);

void StoreWaveletHighpassRows(TRANSFORM *transform,
							  PIXEL *lowlow_result, PIXEL *lowhigh_result,
							  PIXEL *highlow_result, PIXEL *highhigh_result,
							  int width, int level);

//void PrintRecursiveWavelets(ENCODER *encoder);

// Perform the full intra frame transform on a progressive frame using recursive wavelets
void TransformForwardProgressiveIntraFrameRecursive(ENCODER *encoder, IMAGE *image,
													TRANSFORM *transform, int channel,
													BYTE *buffer, size_t buffer_size);

// Perform the full intra frame recursive transform on a progressive frame of packed YUV
void TransformForwardProgressiveIntraFrameRecursiveYUV(ENCODER *encoder, BYTE *frame,
													   int width, int height, int pitch,
													   TRANSFORM *transform_array[], int num_transforms,
													   BYTE *buffer, size_t buffer_size);

// Perform a recursive transform on a packed YUV frame
void TransformForwardRecursiveYUYV(ENCODER *encoder, BYTE *frame, int frame_index,
								   int width, int height, int pitch,
								   TRANSFORM *transform_array[], int num_transforms,
								   BYTE *buffer, size_t buffer_size);
#endif
