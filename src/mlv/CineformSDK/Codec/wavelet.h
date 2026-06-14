/*! @file wavelet.h

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

#ifndef _WAVELET_H
#define _WAVELET_H

#include "config.h"
#include "image.h"
#include "frame.h"
#include "buffer.h"

// Use the same structure packing as the Intel C/C++ compiler
//#pragma pack(push)
//#pragma pack(16)
#pragma pack(push, 16)

// Forward reference
//typedef struct encoder ENCODER;
//typedef struct decoder DECODER;
struct encoder;
struct decoder;

#define WAVELET_MAX_FRAMES	2	// Maximum number of frames in a group

/*
	There are several types of wavelet transforms.  The most common type is
	the spatial wavelet transform with four bands: one lowpass band and three
	highpass bands (horizontal, vertical, and diagonal).  There are wavelets
	with only two bands from a transform applied in only one of the three
	dimensions (horizontal, vertical, or temporal) and spatio-temporal wavelets
	from the application of a temporal transform and a spatial transform in one
	of the spatial dimensions (horizontal or vertical).

	Two band wavelets usually store the results in band zero (lowpass) and
	band one (highpass).  Four band spatial wavelets always store the results
	in the order lowpass (band zero), horizontal highpass (band one), vertical
	highpass (band two), and diagonal highpass (band three).

	Horizontal-temporal wavelets store the lowpass result in band zero and the
	highpass bands in the order horizontal (band one), temporal (band two), and
	horizontal-temporal (band three).

	Vertical-temporal wavelets are not used currently, but if used would store
	the lowpass result in band zero and the highpass results in an order that
	divides vertical lowpass/highpass in a vertical dimension: temporal in
	band one, vertical in band two, and vertical temporal in band three.

	The wavelet types codes are organized to use bits to specify the types of
	transforms.  The number of one bits specify whether the transform has two
	bands or four bands.  A one band transform is just an image and eight band
	wavelets have not been implemented.

	The wavelet type code is stored in 'wavelet_type' in the image descriptor
	(see image.h)
*/
#define WAVELET_TYPE_IMAGE		0	// Not really a wavelet

#define WAVELET_TYPE_HORIZONTAL	1
#define WAVELET_TYPE_VERTICAL	2
#define WAVELET_TYPE_TEMPORAL	4

#define WAVELET_TYPE_SPATIAL	(WAVELET_TYPE_HORIZONTAL | WAVELET_TYPE_VERTICAL)
#define WAVELET_TYPE_HORZTEMP	(WAVELET_TYPE_HORIZONTAL | WAVELET_TYPE_TEMPORAL)
#define WAVELET_TYPE_VERTTEMP	(WAVELET_TYPE_VERTICAL   | WAVELET_TYPE_TEMPORAL)

// Special cases used during code development and testing
#define WAVELET_TYPE_TEMPQUAD	8
#define WAVELET_TYPE_HORZQUAD	9

// Alternate name for a temporal-horizontal wavelet
#define WAVELET_TYPE_FRAME		WAVELET_TYPE_HORZTEMP

// Number of types of wavelets (including the image wavelet type zero)
#define WAVELET_TYPE_COUNT		10

// Maximum wavelet type that can appear in normal code
#define WAVELET_TYPE_HIGHEST	5

/*
	The maximum number of levels in the wavelet transform tree is determined
	by the maximum number of temporal transforms, horizontal transforms, and
	spatial (horizontal and vertical) transforms.
*/
#define TRANSFORM_MAX_TEMPORAL		2	// Number of temporal transform levels
#define TRANSFORM_MAX_HORIZONTAL	1	// Number of horizontal transform levels
#define TRANSFORM_MAX_SPATIAL		4	// Number of spatial transform levels

#define TRANSFORM_MAX_LEVELS (TRANSFORM_MAX_TEMPORAL + TRANSFORM_MAX_HORIZONTAL + TRANSFORM_MAX_SPATIAL)

#define TRANSFORM_MAX_WAVELETS (TRANSFORM_MAX_LEVELS + 1)

#define TRANSFORM_MAX_CHANNELS	4	//DAN06302004	// Maximum number of color channels (including luminance)

#define TRANSFORM_MAX_FRAMES	2		// Maximum number of frames in a group


typedef enum transform_type
{
	TRANSFORM_TYPE_SPATIAL = 0,	// Transform does not use temporal wavelets
	TRANSFORM_TYPE_FIELD,		// Frames organized by field
	TRANSFORM_TYPE_FIELDPLUS,	// Field transform with an additional wavelet transform on temporal highpass
	TRANSFORM_TYPE_FRAME,		// Progressive frames
	TRANSFORM_TYPE_INTERLACED,	// Fields combined into interlaced frames

	TRANSFORM_TYPE_COUNT,		// Number of transform types

	// First transform type that has been implemented
	TRANSFORM_TYPE_FIRST = TRANSFORM_TYPE_SPATIAL,

	// Last transform type that has been implemented
	TRANSFORM_TYPE_LAST = TRANSFORM_TYPE_FIELDPLUS

} TRANSFORM_TYPE;

// Number of levels in a field transform excluding the spatial levels
#define TRANSFORM_FIELD_BASE_LEVELS 2


// Values for error checking during decoding

#define TRANSFORM_NUM_FRAMES	CODEC_GOP_LENGTH
#define TRANSFORM_NUM_CHANNELS	CODEC_MAX_CHANNELS

#ifndef TRANSFORM_TYPE_DEFAULT
#define TRANSFORM_TYPE_DEFAULT	TRANSFORM_TYPE_FIELDPLUS
#endif

#ifndef TRANSFORM_FIRST_WAVELET
#define TRANSFORM_FIRST_WAVELET		(WAVELET_TYPE_SPATIAL)
#endif

#if 0	//(TRANSFORM_TYPE_DEFAULT == TRANSFORM_TYPE_FIELD)

// Parameters for the field transform
#define TRANSFORM_NUM_WAVELETS		5		// Number of wavelets in the transform
#define TRANSFORM_NUM_SPATIAL		2		// Number of spatial wavelets in the transform
#define TRANSFORM_NUM_SUBBANDS		14		// Number of encoded transform subbands

#else

// Parameters for the fieldplus transform
#define TRANSFORM_NUM_WAVELETS		6		// Number of wavelets in the transform
#define TRANSFORM_NUM_SPATIAL		3		// Number of spatial wavelets in the transform
#define TRANSFORM_NUM_SUBBANDS		17		// Number of encoded transform subbands

#endif

#if _RECURSIVE

#define NUM_WAVELET_ROWS	6
#define NUM_WAVELET_BANDS	4

typedef struct transform TRANSFORM;		// Forward reference

typedef struct transform_state
{
	int num_processed;		// Number of rows processed

	int width;				// Width of each wavelet row
	int height;				// Number of rows to process

	int level;				// Level of this wavelet transform

	int num_rows;			// Number of rows in the processing buffers

	TRANSFORM *transform;	// Transform that contains this level in the recursion

	// Vector of quantization values for the wavelet at this level
	//int quant[IMAGE_NUM_BANDS];

	// Buffers for the various types of transforms
	union
	{
		// Buffers for the spatial (horizontal and vertical) transform
		struct
		{
			// Processing buffers for the horizontal lowpass and highpass results
			PIXEL *lowpass[NUM_WAVELET_ROWS];
			PIXEL *highpass[NUM_WAVELET_ROWS];

			// Four rows of wavelet transform results (one per band)
			PIXEL *output[NUM_WAVELET_BANDS];

		} spatial;

		// Buffers for the interlaced (temporal and horizontal) transform
		struct
		{
			// Buffers for the results of the temporal transform
			PIXEL *lowpass;
			PIXEL *highpass;

			// Buffers for the results of the horizontal transform
			PIXEL *lowlow;
			PIXEL *lowhigh;

			PIXEL *highlow;
			PIXEL *highhigh;

		} interlaced;

		// Buffers for the temporal transform
		struct
		{
			PIXEL *input_row_ptr;	// Next input row in the first frame
			int input_row_pitch;	// Pitch of the first frame

			PIXEL *input1;			// Current input row in the first frame

			PIXEL *lowpass;			// Buffers for the temporal transform results
			PIXEL *highpass;

		} temporal;

	} buffers;

} TRANSFORM_STATE;

// Type of transform filters used in the transform descriptor
enum
{
	TRANSFORM_FILTER_UNSPECIFIED = 0,
	TRANSFORM_FILTER_SPATIAL,
	TRANSFORM_FILTER_TEMPORAL,
	TRANSFORM_FILTER_INTERLACED,

	// Insert new filter types here

	TRANSFORM_FILTER_COUNT			// Number of transform filters
};

// Descriptor for the type of transform filter and its arguments
typedef struct transform_descriptor
{
	int type;		// Type of transform filter to apply

	int wavelet1;	// Index of the wavelet and band for the filter
	int band1;

	int wavelet2;	// Index of the wavelet and band for optional second argument
	int band2;

} TRANSFORM_DESCRIPTOR;

#endif

// The spatio-temporal wavelet transform creates a forest of wavelet trees
typedef struct transform
{
	TRANSFORM_TYPE type;	// Organization of the wavelet pyramid
	int num_frames;			// Number of frames in the original image
	int num_levels;			// Number of levels in the wavelet pyramid
	int num_wavelets;		// Number of entries used in the wavelet array
	int num_spatial;		// Number of levels in the spatial wavelet pyramid

	int width;				// Dimensions of the original image
	int height;

	// Buffer for use by the wavelet transform (same size as input image)
	PIXEL *buffer;
	size_t size;

	// Prescale the input by the specified shift before the transform
	int prescale[TRANSFORM_MAX_WAVELETS];

	// Array of wavelet transforms
	IMAGE *wavelet[TRANSFORM_MAX_WAVELETS];

#if _RECURSIVE

	// Buffer for each input row from the original image
	PIXEL *row_buffer;

	// State information for each wavelet in the resursion
	TRANSFORM_STATE state[TRANSFORM_MAX_WAVELETS];

	// Pointers for storing the transform results in each wavelet band
	PIXEL *rowptr[TRANSFORM_MAX_WAVELETS][IMAGE_NUM_BANDS];

	TRANSFORM_DESCRIPTOR descriptor[TRANSFORM_MAX_WAVELETS];

#endif

#if _DEBUG

	FILE *logfile;

#endif

} TRANSFORM;

// Result bands for the spatial and temporal-horizontal transforms
enum {
	LL_BAND = 0,	// Lowpass transform of lowpass intermediate result
	LH_BAND,		// Lowpass transform of highpass intermediate result
	HL_BAND,		// Highpass transform of lowpass intermediate result
	HH_BAND			// Highpass transform of highpass intermediate result
};

// Result bands for the two band wavelet transforms
enum {
	LOWPASS_BAND = 0,
	HIGHPASS_BAND = 1
};

enum {
	EVEN_BAND = 0,
	ODD_BAND = 1
};

// Longer names
enum {
	WAVELET_BAND_LOWLOW = LL_BAND,
	WAVELET_BAND_LOWHIGH = LH_BAND,
	WAVELET_BAND_HIGHLOW = HL_BAND,
	WAVELET_BAND_HIGHHIGH = HH_BAND,
	WAVELET_BAND_NUMBANDS
};

// Prescaling (right shift) applied to 8-bit lowpass channels
#define PRESCALE_LUMA		2
#define PRESCALE_CHROMA		2

// Prescaling (right shift) applied to 10-bit lowpass channels
#define PRESCALE_LUMA10		(PRESCALE_LUMA + PRESCALE_V210_OUTPUT)
#define PRESCALE_CHROMA10	(PRESCALE_CHROMA + PRESCALE_V210_OUTPUT)

// Perform quantization in the forward wavelet transforms
//#ifndef _TRANSFORM_QUANT
#define _TRANSFORM_QUANT	1
//#endif

// Disable code for packing the quantized coefficients using run length coding
//#ifndef _TRANSFORM_RUNS
#define _TRANSFORM_RUNS		0
//#endif

struct encoder;				// Forward reference

#ifdef __cplusplus
extern "C" {
#endif

#define HorizontalFilterParams \
	struct decoder *decoder, 	/* deocde */ \
	int thread_index,			/* use for select the scratch buffer */ \
	PIXEL *lowpass_band[],		/* Horizontal lowpass coefficients */ \
	int lowpass_pitch[],		/* Distance between rows in bytes */ \
	PIXEL *highpass_band[],		/* Horizontal highpass coefficients */ \
	int highpass_pitch[],		/* Distance between rows in bytes */ \
	uint8_t *output_image,			/* Row of reconstructed results */ \
	int output_pitch,			/* Distance between rows in bytes */ \
	ROI roi,					/* Height and width of the strip */ \
	int precision,				/* Precision of the original video */ \
	int format					/* Target pixel format */

// Template for horizontal inverse filters that convert the results to the output format
typedef void (* HorizontalInverseFilterOutputProc)
			(HorizontalFilterParams);
/*
			(struct decoder *decoder,	// decoder
			int thread_index,			// Which thread number (used for scatch buffer.)
			PIXEL *lowpass_band[],		// Horizontal lowpass coefficients
			int lowpass_pitch[],		// Distance between rows in bytes
			PIXEL *highpass_band[],		// Horizontal highpass coefficients
			int highpass_pitch[],		// Distance between rows in bytes
			uint8_t *output_image,			// Row of reconstructed results
			int output_pitch,			// Distance between rows in bytes
			ROI roi,					// Height and width of the strip
			int precision,				// Precision of the original video
			int format);				// Target pixel format	*/

// Initialize a transform data structure
void InitTransform(TRANSFORM *transform);

// Initialize an array of transforms
void InitTransformArray(TRANSFORM **transform, int num_transforms);

// Clear the data allocated within a transform data structure
#if _ALLOCATOR
void ClearTransform(ALLOCATOR *allocator, TRANSFORM *transform);
#else
void ClearTransform(TRANSFORM *transform);
#endif

// Free the transform data structure (including any allocated wavelets)
#if _ALLOCATOR
void FreeTransform(ALLOCATOR *allocator, TRANSFORM *transform);
#else
void FreeTransform(TRANSFORM *transform);
#endif

// Return the number of subbands in the transform
int SubbandCount(TRANSFORM *transform);

// Allocate space for the wavelet transform of the specified type and dimensions
#if _ALLOCATOR
void AllocTransform(ALLOCATOR *allocator, TRANSFORM *transform, int type,
					int width, int height, int num_frames, int num_spatial);
#else
void AllocTransform(TRANSFORM *transform, int type, int width, int height, int num_frames, int num_spatial);
#endif

// Record the original (or requested) frame dimensions
void SetTransformFrame(TRANSFORM *transform, int width, int height);

// Get the prescale shifts based on the number of bits in the input
void GetTransformPrescale(TRANSFORM *transform, int transform_type, int precision);

// Set the prescale shifts based on the number of bits in the input
void SetTransformPrescale(TRANSFORM *transform, int transform_type, int precision);

// Test where the Prescale table has been modified from the original defaults.
bool TestTransformPrescaleMatch(TRANSFORM *transform, int transform_type, int precision);

#if _PACK_RUNS_IN_BAND_16S
int PackRuns16s(PIXEL *input, int width);
#endif

// Create a four band wavelet image with each band width by height
void InitWavelet(IMAGE *wavelet, int width, int height, int level, int type, int half_width);
#if _ALLOCATOR
void AllocWavelet(ALLOCATOR *allocator, IMAGE *wavelet, int width, int height, int level, int type);
void AllocWaveletStack(ALLOCATOR *allocator, IMAGE *wavelet, int width, int height, int level, int type);
IMAGE *CreateWavelet(ALLOCATOR *allocator, int width, int height, int level);
#else
void AllocWavelet(IMAGE *wavelet, int width, int height, int level, int type);
void AllocWaveletStack(IMAGE *wavelet, int width, int height, int level, int type);
IMAGE *CreateWavelet(int width, int height, int level);
#endif

#if _ALLOCATOR
// Create a wavelet (four band) image with the same dimensions as an existing image
IMAGE *CreateWaveletFromImage(ALLOCATOR *allocator, IMAGE *image);
IMAGE *CreateWaveletFromArray(ALLOCATOR *allocator, PIXEL *array,
							  int width, int height, int pitch,
							  int level, int type);
#else
// Create a wavelet (four band) image with the same dimensions as an existing image
IMAGE *CreateWaveletFromImage(IMAGE *image);
IMAGE *CreateWaveletFromArray(PIXEL *array, int width, int height, int pitch, int level, int type);
#endif

// Create wavelet image that is twice as larger as the argument wavelet
#if _ALLOCATOR
IMAGE *CreateExpandedWavelet(ALLOCATOR *allocator, IMAGE *wavelet);
#else
IMAGE *CreateExpandedWavelet(IMAGE *wavelet);
#endif

// Create wavelet with extended arguments
#if _ALLOCATOR
IMAGE *CreateWaveletEx(ALLOCATOR *allocator, int width, int height, int level, int type);
IMAGE *ReallocWaveletEx(ALLOCATOR *allocator, IMAGE *wavelet, int width, int height, int level, int type);
#else
IMAGE *CreateWaveletEx(int width, int height, int level, int type);
IMAGE *ReallocWaveletEx(IMAGE *wavelet, int width, int height, int level, int type);
#endif

// Horizontal wavelet transforms

// Compute the horizontal wavelet transform
void TransformForwardHorizontal(IMAGE *input, int band,
								IMAGE *lowpass, int lowpass_band,
								IMAGE *highpass, int highpass_band);

// Invert the horizontal wavelet transform
void TransformInverseHorizontal(IMAGE *input, int lowpass_band, int highpass_band,
								IMAGE *output, int output_band, bool fastmode);


// Vertical wavelet transforms

// Compute the vertical wavelet transform
void TransformForwardVertical(IMAGE *input, int band,
							  IMAGE *lowpass, int lowpass_band,
							  IMAGE *highpass, int highpass_band);

// Invert the vertical wavelet transform
void TransformInverseVertical(IMAGE *input, int lowpass_band, int highpass_band,
							  IMAGE *output, int output_band);


// Temporal wavelet transforms

// Compute the temporal wavelet transform
void TransformForwardTemporal(IMAGE *input1, int band1,
							  IMAGE *input2, int band2,
							  IMAGE *lowpass, int lowpass_band,
							  IMAGE *highpass, int highpass_band);

// Invert the temporal wavelet transform
void TransformInverseTemporal(IMAGE *temporal, IMAGE *frame0, IMAGE *frame1);
void TransformInverseTemporalQuant(IMAGE *temporal, IMAGE *frame0, IMAGE *frame1,
								   PIXEL *buffer, size_t buffer_size, int precision);

// Apply the temporal transform to the even and odd fields of a single frame.
// This version uses in place computation so the frame data will be overwritten.
void TransformForwardInterlaced(IMAGE *frame);

// Invert the temporal wavelet transform that was applied to an interlaced frame
void TransformInverseInterlaced(IMAGE *lowpass, int lowpass_band,
								IMAGE *highpass, int highpass_band,
								IMAGE *frame, int output_band);


// Spatial (horizontal and vertical) transforms

// Compute the size of buffer used by the forward spatial transform
size_t ForwardSpatialBufferSize(int width);

// Compute the spatial (horizontal and vertical) wavelet transform
#if _ALLOCATOR
IMAGE *TransformForwardSpatial(ALLOCATOR *allocator,
							   IMAGE *image, int band, IMAGE *wavelet, int level,
							   PIXEL *buffer, size_t size, int prescale,
							   int quantization[IMAGE_NUM_BANDS], int difference_LL);
#else
IMAGE *TransformForwardSpatial(IMAGE *image, int band, IMAGE *wavelet, int level,
							   PIXEL *buffer, size_t size, int prescale,
							   int quantization[IMAGE_NUM_BANDS], int difference_LL);
#endif

// Compute the spatial wavelet transform and encode the quantized highpass coefficients
bool TransformForwardSpatialCoded(struct encoder *encoder, IMAGE *image, int band,
								  IMAGE *wavelet, int level,
								  PIXEL *buffer, size_t size, int prescale,
								  int quantization[IMAGE_NUM_BANDS]);

// Unpack YUV pixels in a progressive frame and perform the forward spatial transform
void TransformForwardSpatialYUV(uint8_t *input, int input_pitch, FRAME_INFO *frame,
								TRANSFORM *transform[], int frame_index, int num_channels,
								PIXEL *buffer, size_t buffer_size, int chroma_offset, int IFrame,
								int precision, int limit_yuv, int conv_601_709);

// Forward spatial transform for first wavelet level that runs in multiple threads
void TransformForwardSpatialYUVThreaded(struct encoder *encoder, uint8_t *input, int input_pitch, FRAME_INFO *frame,
										TRANSFORM *transform[], int frame_index, int num_channels,
										PIXEL *buffer, size_t buffer_size, int chroma_offset);

// Apply the forward frame transform to a packed frame of YUV data using multiple threads
void TransformForwardFrameYUVThreaded(struct encoder *encoder, uint8_t *input, int input_pitch, FRAME_INFO *frame,
									  TRANSFORM *transform[], int frame_index, int num_channels,
									  char *buffer, size_t buffer_size, int chroma_offset);

// Convert YUV packed to planar and perform the forward spatial transform
void TransformForwardSpatialYUVPlanarThreaded(struct encoder *encoder, uint8_t *input, int input_pitch, FRAME_INFO *frame,
											  TRANSFORM *transform[], int frame_index, int num_channels,
											  PIXEL *buffer, size_t buffer_size, int chroma_offset);

void TransformForwardSpatialBYR3(uint8_t *input, int input_pitch, FRAME_INFO *frame,
								 TRANSFORM *transform[], int frame_index, int num_channels,
								 PIXEL *buffer, size_t buffer_size, int chroma_offset,
								 int IFrame, int display_height);

void TransformForwardSpatialRGB30(uint8_t *input, int input_pitch, FRAME_INFO *frame,
								 TRANSFORM *transform[], int frame_index, int num_channels,
								 PIXEL *buffer, size_t buffer_size, int chroma_offset,
								 int IFrame, int display_height, int precision, int origformat);

// Optmized version of routine to invert a spatial wavelet transform
void TransformInverseSpatial(IMAGE *input, IMAGE *output, PIXEL *buffer, size_t buffer_size, int scale);

// Optimized version of routine to invert a spatial wavelet transform to packed YUV
void TransformInverseSpatialToYUV(struct decoder *decoder, TRANSFORM *transform[], int frame_index, int num_channels,
								  uint8_t *output, int pitch, FRAME_INFO *info,
								  const SCRATCH *scratch, int chroma_offset, int precision);

void TransformInverseSpatialToBuffer(struct decoder *decoder, TRANSFORM *transform[], int frame_index, int num_channels,
									 uint8_t *output, int pitch, FRAME_INFO *info,
									 const SCRATCH *scratch, int chroma_offset,
									 int precision);

void TransformInverseSpatialToV210(TRANSFORM *transform[], int frame_index, int num_channels,
								   uint8_t *output, int pitch, FRAME_INFO *info,
								   const SCRATCH *scratch, int chroma_offset, int precision);

void TransformInverseFrameToRow16u(struct decoder *decoder, TRANSFORM *transform[], int frame_index, int num_channels,
								   PIXEL16U *output, int output_pitch, FRAME_INFO *frame,
								   const SCRATCH *scratch, int chroma_offset, int precision);

void TransformInverseSpatialToRow16u(TRANSFORM *transform[], int frame_index, int num_channels,
									 PIXEL16U *output, int output_pitch, FRAME_INFO *info,
									 const SCRATCH *scratch, int chroma_offset,
									 int precision);

void TransformInverseRGB444ToB64A(TRANSFORM *transform[], int frame_index, int num_channels,
								  uint8_t *output_buffer, int output_pitch, FRAME_INFO *info,
								  const SCRATCH *scratch, int chroma_offset, int precision);

void TransformInverseRGB444ToYU64(TRANSFORM *transform[], int frame_index, int num_channels,
								  uint8_t *output_buffer, int output_pitch, FRAME_INFO *info,
								  const SCRATCH *scratch, int chroma_offset, int precision);

void TransformInverseRGB444ToRGB32(TRANSFORM *transform[], int frame_index, int num_channels,
								   uint8_t *output_buffer, int output_pitch, FRAME_INFO *info,
								   const SCRATCH *scratch, int chroma_offset, int precision);

void TransformInverseRGB444ToRGB48(TRANSFORM *transform[], int frame_index, int num_channels,
								   uint8_t *output_buffer, int output_pitch, FRAME_INFO *info,
								   const SCRATCH *scratch, int chroma_offset, int precision);

// Optmized version of spatial wavelet transform inverse that also performs dequantization
void TransformInverseSpatialQuantLowpass(IMAGE *input, IMAGE *output, const SCRATCH *scratch,
										 int scale, bool inverse_prescale);

//void TransformInverseSpatialBuffered(IMAGE *input, IMAGE *output, PIXEL *buffer, int scale, PIXEL *line_buffer);

void TransformInverseSpatialQuantHighpass(IMAGE *input, IMAGE *output, PIXEL *buffer, size_t buffer_size, int scale);

// Compute the wavelet transform of the input image
//void TransformWavelet(IMAGE *input, IMAGE *output, IMAGE *even, IMAGE *odd);
void TransformForwardWaveletQuad(IMAGE *input, int band, IMAGE *output, PIXEL *buffer, size_t size, int prescale);
void TransformForwardWaveletStack(IMAGE *input, int band, IMAGE *output,
								  PIXEL *buffer, size_t size, int prescale,
								  int quantization[4]);

// Compute the inverse wavelet transform (old version)
void TransformInverseWavelet(IMAGE *input, IMAGE *output, IMAGE *lowpass, IMAGE *highpass);


// Wavelet transforms for image fields (interlaced frames)

// Compute the field (temporal and horizontal) wavelet transform between two image fields
IMAGE *TransformForwardField(IMAGE *fields, int even_band, int odd_band, PIXEL *buffer);

// Wavelet transforms for the temporal-horizontal transform applied to interlaced frames

// Apply the temporal-horizontal wavelet transform to an interlaced frame
void TransformForwardFrame(IMAGE *frame, IMAGE *wavelet, PIXEL *buffer, size_t buffer_size,
						   int offset, int quantization[4]);

// Apply the forward horizontal-temporal transform to a packed frame of YUV data
void TransformForwardFrameYUV(uint8_t *output, int output_pitch, FRAME_INFO *frame,
							  TRANSFORM *transform[], int frame_index, int num_channels,
							  char *buffer, size_t buffer_size, int chroma_offset, int precision, int limit_yuv, int conv601_709);

// Routines that work with collections of wavelets in the transform data structure

// Compute spatio-temporal transform (all levels) of a group of frames in field format
void TransformGroupFields(TRANSFORM *transform,
						  IMAGE **group, int group_length,
						  int num_spatial, PIXEL *buffer);

void TransformFrames(TRANSFORM *transform,
					 FRAME *frame[], int group_length,
					 int num_spatial, PIXEL *buffer);

// Optimized to transform interlaced fields within frames
void TransformGroupFrames(FRAME **group, int group_length,
						  TRANSFORM *transform[], int num_transforms,
						  int num_spatial, PIXEL *buffer, size_t buffer_size);

// Compute the wavelet transform for the specified channel in the group of frames
void TransformGroupChannel(FRAME **group, int group_length, int channel,
						  TRANSFORM *transform, int num_spatial,
						  PIXEL *buffer, size_t buffer_size);

// Compute the upper levels of the wavelet transform for a group of frames
#if _ALLOCATOR
void ComputeGroupTransform(ALLOCATOR *allocator, TRANSFORM *transform[], int num_transforms,
						   int group_length, int num_spatial, int precision);
#else
void ComputeGroupTransform(TRANSFORM *transform[], int num_transforms,
						   int group_length, int num_spatial, int precision);
#endif

// Finish the wavelet transform for the group of frames
#if _ALLOCATOR
void FinishFieldTransform(ALLOCATOR *allocator, TRANSFORM *transform, int group_length, int num_spatial);
#else
//void FinishFieldTransform(TRANSFORM *transform, int group_length, int num_spatial, int prescale);
void FinishFieldTransform(TRANSFORM *transform, int group_length, int num_spatial);
#endif

// Finish the fieldplus transform for the group of frames
#if _ALLOCATOR
void FinishFieldPlusTransform(ALLOCATOR *allocator, TRANSFORM *transform,
							  int group_length, int num_spatial, int prescale);
#else
void FinishFieldPlusTransform(TRANSFORM *transform, int group_length,
							  int num_spatial, int prescale);
#endif

// Compute the scale factors needed for correct display
void SetTransformScale(TRANSFORM *transform);

// Reconstruct the transform from the data decoded from a group of frames
bool ReconstructGroupTransform(TRANSFORM *transform, int width, int height, int num_frames,
							   int channel, PIXEL *buffer, size_t buffer_size);
bool ReconstructQuantTransform(TRANSFORM *transform, int width, int num_frames,
							   int channel, PIXEL *buffer, size_t buffer_size);

// Reconstruct the group transform for a single channel
bool ReconstructGroupImages(TRANSFORM *transform, IMAGE *frame[], int num_frames, int background, int prescale);

// Compute a pyramid of wavelet images and return the root image
IMAGE *CreateWaveletPyramid(IMAGE *input, int num_levels, PIXEL *buffer, size_t size);

void ReconstructImagePyramid(IMAGE *wavelet);

// Convert wavelet coefficients to 16-bit signed
void ConvertGroupTransform(TRANSFORM *transform);
void ConvertWaveletBand(IMAGE *wavelet, int band);

#if _TEST

void PrintTransformScale(TRANSFORM *transform, FILE *logfile);
int32_t TestHorizontalTransform(bool fastmode, unsigned int seed, FILE *logfile);
int32_t TestVerticalTransform(unsigned int seed, FILE *logfile);
int32_t TestTemporalTransform(unsigned int seed, FILE *logfile);
int32_t TestFrameTransform(unsigned int seed, FILE *logfile);
int32_t TestFrameTransform16s(unsigned int seed, FILE *logfile);
int32_t TestFrameTransform8u(unsigned int seed, FILE *logfile);
int32_t TestSpatialTransform(unsigned int seed, FILE *logfile);
int32_t TestSpatialLowpassTransform(unsigned int seed, FILE *logfile);
int32_t TestSpatialHighpassTransform(unsigned int seed, FILE *logfile);
int32_t TestPrescaledSpatialTransform(unsigned int seed, FILE *logfile);
int32_t TestFilterSpatial16s(unsigned int seed, FILE *logfile);
int32_t TestFilterSpatialQuant16s(unsigned int seed, FILE *logfile);
int32_t TestFilterSpatialYUVQuant16s(unsigned int seed, FILE *logfile);
int32_t TestTransformSpatialYUV(unsigned int seed, FILE *logfile);
int32_t TestTemporal16s(unsigned int seed, FILE *logfile);
int32_t TestInterlaced8u(unsigned int seed, FILE *logfile);
int32_t TestInterlacedYUV(unsigned int seed, FILE *logfile);
int32_t TestInterlacedRowYUV(unsigned int seed, FILE *logfile);
int32_t TestInverseTemporalQuant(unsigned int seed, FILE *logfile);
int32_t TestTransformForwardFrameYUV(unsigned int seed, FILE *logfile);
void DumpLowPassBands(TRANSFORM *transform, FILE *logfile);
void DumpTransformStatistics(TRANSFORM *transform, FILE *logfile);
void DumpTransform(char *label, TRANSFORM *transform, int row, FILE *logfile);
void DumpTransform8s(char *label, TRANSFORM *transform, int row, FILE *logfile);

#endif


/***** Threaded implementations of the wavelet transforms *****/

// Unpack YUV pixels in a progressive frame and perform the forward spatial transform
void TransformForwardSpatialThreadedYUV(uint8_t *input, int input_pitch, FRAME_INFO *frame,
										TRANSFORM *transform[], int frame_index, int num_channels,
										PIXEL *buffer, size_t buffer_size, int chroma_offset);

void TransformForwardSpatialThreadedChannels(FRAME *input, int frame, TRANSFORM *transform[],
											 int level, PIXEL *buffer, size_t buffer_size);


// New routine for computing the inverse transform of the largest spatial wavelet
void TransformInverseSpatialYUV422ToOutput(struct decoder *decoder,
									TRANSFORM *transform[], int frame_index, int num_channels,
									uint8_t *output_buffer, int output_pitch, FRAME_INFO *info,
									const SCRATCH *scratch, int chroma_offset, int precision,
									HorizontalInverseFilterOutputProc horizontal_filter_proc);

#ifdef __cplusplus
}
#endif

#pragma pack(pop)

#endif
