/*! @file image.h

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

#ifndef _IMAGE_H
#define _IMAGE_H

#include <stdint.h>
#include "config.h"		// Read program configuration parameters
#include <limits.h>		// Range of integer data types

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include "../Common/macdefs.h"
#endif

#if _DEBUG
#include <stdio.h>		// Debugging output
#endif

#include "allocator.h"

// Use the same structure packing as the Intel C/C++ compiler
//#pragma pack(push)
//#pragma pack(16)
#pragma pack(push, 16)

// Pixel parameters for the V210 format
#define V210_VALUE1_SHIFT		 0
#define V210_VALUE2_SHIFT		10
#define V210_VALUE3_SHIFT		20

#define V210_VALUE_MASK		0x03FF

// Macros for clipping image processing results to pixel range
#define _SATURATE(l,x,u)	(((l) <= (x)) ? (((x) <= (u)) ? (x) : (u)) : (l))
#define SATURATE_8U(x)		_SATURATE(0, (x), UINT8_MAX)
#define SATURATE_16S(x)		_SATURATE(INT16_MIN, (x), INT16_MAX)
#define SATURATE_8S(x)		_SATURATE(INT8_MIN, (x), INT8_MAX)
#define SATURATE_16U(x)		_SATURATE(0, (x), UINT16_MAX)

#define SATURATE_V210(x)	_SATURATE(0, (x), V210_VALUE_MASK)

#define LOWER_LIMIT_LUMA	16
#define UPPER_LIMIT_LUMA	235
#define LOWER_LIMIT_CHROMA	16
#define UPPER_LIMIT_CHROMA	240

#define SATURATE_LUMA(x)	_SATURATE(LOWER_LIMIT_LUMA, (x), UPPER_LIMIT_LUMA)
#define SATURATE_CHROMA(x)	_SATURATE(LOWER_LIMIT_CHROMA, (x), UPPER_LIMIT_CHROMA)

typedef short int PIXEL;

#define PIXEL_MINIMUM SHRT_MIN
#define PIXEL_MAXIMUM SHRT_MAX

#define PIXEL_MIN SHRT_MIN
#define PIXEL_MAX SHRT_MAX

#define SATURATE SATURATE_16S
#define PIXEL_ZERO_OFFSET 0


// Define additional pixel types

typedef char PIXEL8S;
#define PIXEL8S_MIN SCHAR_MIN
#define PIXEL8S_MAX SCHAR_MAX

typedef unsigned char PIXEL8U;
#define PIXEL8U_MIN 0
#define PIXEL8U_MAX UCHAR_MAX

typedef short int PIXEL16S;
#define PIXEL16S_MIN SHRT_MIN
#define PIXEL16S_MAX SHRT_MAX

typedef unsigned short PIXEL16U;
#define PIXEL16U_MIN 0
#define PIXEL16U_MAX USHRT_MAX

INLINE static int Clamp16s(int x)
{
	if (x < SHRT_MIN) x = SHRT_MIN;
	else if (x > SHRT_MAX) x = SHRT_MAX;
	return x;
}

// Prescaling definitions

// Amount of prescaling (right shift) required to avoid overflows
#define PRESCALE_V210_INPUT		0
#define PRESCALE_V210_OUTPUT	2


typedef enum image_type		// Image type codes
{
	IMAGE_TYPE_GENERIC = 0,	// Unspecified use of bands
	IMAGE_TYPE_GRAY,		// Single gray band, other bands unused
	IMAGE_TYPE_WAVELET,		// Wavelet decomposition into four bands
							// Band[0] is the low resolution gray image

	IMAGE_TYPE_ZEROTREE,	// Only used to fill the type field in zerotrees

	IMAGE_TYPE_UNIMPLEMENTED

} IMAGE_TYPE;

typedef enum image_format	// Format of image in memory
{
	IMAGE_FORMAT_UNKNOWN = 0,
	IMAGE_FORMAT_FRAME,		// Image is stored as a single frame
	IMAGE_FORMAT_FIELD,		// Image is stored as two fields

	IMAGE_FORMAT_UNIMPLEMENTED

} IMAGE_FORMAT;

typedef enum image_source	// Format of the image source
{
	IMAGE_SOURCE_PROGRESSIVE = 0,	// Rows are adjacent in space
	IMAGE_SOURCE_INTERLACED = 1,	// Rows alternate in time
	IMAGE_SOURCE_PULLDOWN = 3,		// 3:2 pulldown

	IMAGE_SOURCE_UNIMPLEMENTED

} IMAGE_SOURCE;

typedef enum image_field	// Designation for even and odd fields
{
	IMAGE_FIELD_EVEN = 0,	// Even field
	IMAGE_FIELD_ODD = 1,	// Odd field
	IMAGE_FIELD_BOTH = 3,	// Both fields

	IMAGE_FIELD_UNIMPLEMENTED

} IMAGE_FIELD;

typedef enum image_alloc_t	// Method used to allocate the image bands
{
	IMAGE_ALLOC_UNKNOWN = 0,
	IMAGE_ALLOC_ONE_MALLOC,		// One memory allocation for all bands
	IMAGE_ALLOC_BAND_MALLOC,	// Separate allocation for each band
	IMAGE_ALLOC_STATIC_DATA,	// Bands are pointers into a data array

	IMAGE_ALLOC_UNALLOCATED = IMAGE_ALLOC_UNKNOWN,

	IMAGE_ALLOC_UNUSED			// Next avilable image allocation code

} IMAGE_ALLOC_T;

typedef enum pixel_type
{
	PIXEL_TYPE_UNKNOWN = 0,		// Unspecified type of pixel
	PIXEL_TYPE_16S,				// Signed 16 bits
	PIXEL_TYPE_8S,				// Signed 8 bits
	PIXEL_TYPE_8U,				// Unsigned 8 bits
	PIXEL_TYPE_RUNS,			// Run length encoded pixels
	PIXEL_TYPE_CODED,			// Variable length encoded pixels

	PIXEL_TYPE_8BPP = PIXEL_TYPE_8S,
	PIXEL_TYPE_16BPP = PIXEL_TYPE_16S
} PIXEL_TYPE;

typedef struct image_statistics
{
	PIXEL minPixel;				// Minimum and maximum pixel values
	PIXEL maxPixel;

	uint32_t cntNegative;	// Histogram of pixel signs
	uint32_t cntPositive;
	uint32_t cntZero;

} IMAGE_STATISTICS_T;

/*
	The image data structure handles gray value images and wavelet decompositions.
	By convention, the four bands of a wavelet decomposition (lowpass, horizontal,
	vertical, and diagonal) are stored in band[0], band[1], and so on in that order
	and the bands are pointers into a single image array with the bands arranged as
		band[0]   band[1]
		band[2]   band[3]
	with each band having the dimensions width by height as specified in the header.
	An image processing operation can be applied to any combination of bands,
	or to the entire wavelet decomposition be treating band[0] as if it were
	dimensioned as 2*width by 2*height;

	For images in field format (each field in a different array)
*/

#define IMAGE_NUM_BANDS		4
#define IMAGE_MAX_WIDTH	  720		// Maximum image dimensions for allocating scratch space
#define IMAGE_MAX_HEIGHT  480

#define BAND_INDEX_LOWPASS				0
#define BAND_INDEX_HIGHPASS_HORIZONTAL	1
#define BAND_INDEX_HIGHPASS_VERTICAL	2
#define BAND_INDEX_HIGHPASS_DIAGONAL	3

#define BAND_INDEX_FIELD_EVEN			0
#define BAND_INDEX_FIELD_ODD			1

#define BAND_INDEX_HIGHPASS_TEMPORAL	2

enum {
	HIGHPASS_DISPLAY_GRAY = 0,		// Display highpass wavelet bands as gray images
	HIGHPASS_DISPLAY_BINARY = 1		// Display highpass wavelet bands as binary images
};

// Flags that indicate whether a band has been decoded or reconstructed
#define BAND_VALID_MASK(band)		(1 << (band))
#define BANDS_ALL_VALID(wavelet)	((wavelet)->band_valid_flags == (uint32_t)((1 << (wavelet)->num_bands) - 1))
#define BANDS_ALL_STARTED(wavelet)	(((wavelet)->band_started_flags & (uint32_t)((1 << (wavelet)->num_bands) - 2)) == (uint32_t)((1 << (wavelet)->num_bands) - 2))
#define HIGH_BANDS_VALID(wavelet)	((wavelet)->band_valid_flags == (uint32_t)(((1 << (wavelet)->num_bands - 1) - 1) << 1)

// Convert a subband index into a bitmask
#define SUBBAND_MASK(subband)		(1 << (subband))

typedef struct image
{
	IMAGE_TYPE type;	// Type of image (must be at same offset in zerotree)

	/***** Fields above must correspond with the zerotree definition *****/

	IMAGE_FORMAT format;

	int height;			// Dimensions of each image band
	int width;
	int pitch;			// Width of the array in which the bands are embedded (in bytes)

	int num_bands;		// Number of bands that are used

	// Wavelet bands (if allocated) with band[0] always the gray or color image
	PIXEL *band[IMAGE_NUM_BANDS];

	// Record the method used to allocate memory space for image bands
	IMAGE_ALLOC_T alloc[IMAGE_NUM_BANDS];

	// Record the allocated block when all bands point into the same block
	PIXEL *memory;

#if _DEBUG
	size_t memory_size;			// Size of the allocated memory block
#endif

	// Pointers to the lower and higher resolution images (or wavelet decompositions)
	// in the pyramid.  The reduced resolution image (wavelet) is higher in the pyramid,
	// the expanded resoution image is lower in the pyramid.  This top image in the
	// pyramid has reduced equal to null, the bottom image has expanded equal to null;

	//struct image *reduced, *expanded;

	// Original image from which the wavelet was computed
	//struct image *original;

	int level;				// Level within an image pyramid (level zero is the bottom)

	int wavelet_type;		// Wavelet type code (see wavelet.h)

	// Scale factors for accumulating the effect of filter operations
	// The scale factor is reduced by pre- or post-scaling during filtering
	int scale[IMAGE_NUM_BANDS];

	// Pixel type (bits per pixel)
	int pixel_type[IMAGE_NUM_BANDS];

	// Number of run length codes in a band of runs
	int num_runs[IMAGE_NUM_BANDS];

	// Size of the band if it has been encoded
	int coded_size[IMAGE_NUM_BANDS];

	// Amount by which band was reduced (scaled down) during filtering
	//int divisor[IMAGE_NUM_BANDS];

	// Vector of quantization values for this wavelet
	int quant[IMAGE_NUM_BANDS];

	// Amount of quantization applied to each band before encoding
	int quantization[IMAGE_NUM_BANDS];

	// Method for displaying the highpass bands
	int highpass_display;

	// Alternative pitch used for bands that contain 8-bit pixels so that
	// the rows are packed more closely together within a band that was
	// allocated to hold 16-bit pixels.
	int pitch8s;

#if 0
	// For debugging
	int encoded_pitch;
#endif

	// Flag that indicates if the lowpass band has been reconstructed during decoding
	int valid_lowpass_band;

	// Flag that indicates if the temporal highpass band is valid
	int valid_highpass_band;

	// Note: Should merge the two flags into a vector of flags indexed by the band

	// Image statistics organized by band
	IMAGE_STATISTICS_T stats[4];

	uint32_t band_started_flags;	//used in threaded, entropy decode is started
	uint32_t band_valid_flags;		//entropy decode is complete

} IMAGE;


/*
	Image or wavelet pyramid.

	The pyramid data structure contains pointers to the top (highest resolution)
	and bottom images of the pyramid.  The images can be gray value images or
	four bands of wavelet decomposition (see comments for image datatype above).

	The pyramid contains pointers to the top (lowest resolution) and bottom images.
	Between levels, the images are connected in a double linked list.
*/

typedef struct pyramid	// Not currently used
{
	IMAGE *top;			// Lowest resolution image (or wavelet decomposition)
	IMAGE *bottom;		// Highest resolution image (or wavelet decomposition)

	int num_levels;		// Number of levels in the pyramid

} PYRAMID;

/*
	Sequence of images

	The sequence data structure allows the program to step through the images
	in a sequence processing each image to an output sequence or encoding the
	images into a bitstream.

	Images in the sequence can be formatted as frames or fields, but all images
	in the sequence are assumed to have the same format.
*/

typedef struct sequence
{
	IMAGE_TYPE type;
	IMAGE_FORMAT format;	// Images are organized by field or frame

	int width;				// Dimensions of each image
	int height;

} SEQUENCE;

/*
	Data structures used for specifying a subimage
*/

typedef struct{
	int width;
	int height;
} ROI;

typedef struct subimage
{
	int row;		// Row offset from top of image
	int column;		// Column offset from left side of image
	int width;		// Height of the subimage rectangle
	int height;		// Width of the subimage rectangle

} SUBIMAGE;

#define SUBIMAGE_INITIALIZER {0, 0, 0, 0}

// Handy macros for specifying the corners of an image or wavelet band
#define SUBIMAGE_UPPER_LEFT(w, h)	{    0,    0, (w), (h)}
#define SUBIMAGE_UPPER_RIGHT(w, h)	{    0, -(w), (w), (h)}
#define SUBIMAGE_LOWER_LEFT(w, h)	{ -(h),    0, (w), (h)}
#define SUBIMAGE_LOWER_RIGHT(w, h)	{ -(h), -(w), (w), (h)}

/*
	Data structure for image histograms
*/

typedef uint32_t BUCKET;

typedef struct histogram
{
	IMAGE *image;		// Source image for histogram
	int band;			// Image band used for histogram
	int length;			// Number of buckets
	int width;			// Number of pixels per bucket
	PIXEL minimum;		// Pixel range
	PIXEL maximum;
	BUCKET bucket[1];	// Histogram follows the header

} HISTOGRAM;

// Macros for memory alignment
#define LONG_MASK			((size_t)(sizeof(long) - 1))
#define LONG_ROUND_DOWN(n)	((n) & ~LONG_MASK)
#define LONG_ROUND_UP(n)	LONG_ROUND_DOWN((n) + LONG_MASK)
#define LONG_ALIGNED(n)		((((long)(n)) & LONG_MASK) == 0)

#define ALIGN16(n)			((((size_t)(n)) + 0x0F) & ~(size_t)0x0F)
#define ISALIGNED16(n)		((((size_t)(n)) & 0x0F) == 0)

#define ALIGN(n,m)			((((size_t)(n)) + ((m)-1)) & ~((size_t)((m)-1)))
#define ISALIGNED(n,m)		((((size_t)(n)) & ((m)-1)) == 0)


//TODO: Use standard integers to define the fields in the frame info structure

// Frame dimensions and format for encoding or decoding
typedef struct frame_info {
		int width;			// Frame width after decoding or before encoding
		int height;			// Frame height after decoding or before encoding
		int format;			// Internal Format of decoded frames
		int output_format;	// Output Format of decoded frames
		int resolution;		// Resolution of decoded frames
		int pixel_size;		// Size of decoded pixel in bytes
		int colorspace;		// 601 vs 709 -- videoRGB vs sRGB
		int colorspace_filedefault;// hack used to change the default behavior
		int colorspace_override;// hack used to change the default behavior

		int generate_look;	// don't decode, fill the buffer with raw LUT data.
		int white_point;	// default 0, means use all available bits.
		int black_point;	// default 0
		//int signed_pixels;	// default 0, 1 mean RGB48 outputs are -32768 to 32767
		int alpha_Companded;// default 0, 1 means alpha has expanded to full range.
} FRAME_INFO;

// Encapsulate the representations for the pixel format and color space
#define COLORFORMAT(info)	((info)->format)
#define COLORSPACE(info)	((info)->colorspace)

// Some uses for the color format may not include the color space
#define DECODEDFORMAT(info)	((info)->format)

#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif

void InitFrameInfo(FRAME_INFO *info, int width, int height, int format);

// Determine if the specified wavelet band is valid
bool IsBandValid(IMAGE *wavelet, int band);

// Create a new image
#if _ALLOCATOR
void AllocImage(ALLOCATOR *allocator, IMAGE *image, int width, int height);
IMAGE *CreateImage(ALLOCATOR *allocator, int width, int height);
IMAGE *CreateImageFromImage(ALLOCATOR *allocator, IMAGE *image);
IMAGE *CreateImageFromArray(ALLOCATOR *allocator, PIXEL *array, int width, int height, int pitch);
IMAGE *CreateWaveletBandFromArray(ALLOCATOR *allocator, PIXEL *array, int width, int height, int pitch, int band);
IMAGE *CreateFieldImageFromFrame(ALLOCATOR *allocator, IMAGE *frame);
IMAGE *CreateImageFromPlanes(ALLOCATOR *allocator, uint8_t *data, int width, int height, int pitch, int format);
IMAGE *CreateImageFromPacked(ALLOCATOR *allocator, uint8_t *data, int width, int height, int pitch, int format);
#else
void AllocImage(IMAGE *image, int width, int height);
IMAGE *CreateImage(int width, int height);
IMAGE *CreateImageFromImage(IMAGE *image);
IMAGE *CreateImageFromArray(PIXEL *array, int width, int height, int pitch);
IMAGE *CreateWaveletBandFromArray(PIXEL *array, int width, int height, int pitch, int band);
IMAGE *CreateFieldImageFromFrame(IMAGE *frame);
IMAGE *CreateImageFromPlanes(uint8_t *data, int width, int height, int pitch, int format);
IMAGE *CreateImageFromPacked(uint8_t *data, int width, int height, int pitch, int format);
#endif

// Adjust the size of an image to the specified dimensions
void ResizeImage(IMAGE *image, int width, int height);

// Allocate space for an new band
#if _ALLOCATOR
void AllocateBand(ALLOCATOR *allocator, IMAGE *image, int band_index);
#else
void AllocateBand(IMAGE *image, int band_index);
#endif

// Delete an image data structure and the image buffers
#if _ALLOCATOR
void DeleteImage(ALLOCATOR *allocator, IMAGE *image);
#else
void DeleteImage(IMAGE *image);
#endif

// Free the memory used by an image
#if _ALLOCATOR
void FreeImage(ALLOCATOR *allocator, IMAGE *image);
#else
void FreeImage(IMAGE *image);
#endif

// Convert RGB data into a gray value image
IMAGE *CreateImageFromRGB(uint8_t *rgb, int pitch, int width, int height);
void ConvertRGBToImage(uint8_t *rgb, int pitch, IMAGE *image);

#ifdef _WIN32
// Convert between images and device independent bitmaps
IMAGE *CreateImageFromDIB(LPBITMAPINFOHEADER lpbi);
void ConvertDibToImage(LPBITMAPINFOHEADER lpbi, IMAGE *image);
#endif

// Convert image to RGB and other formats
void ConvertImageToRGB(IMAGE *image, uint8_t *output_buffer, int32_t output_pitch, int format, bool inverted);
void ConvertImageToYUV(IMAGE *image, uint8_t *output_buffer, int32_t output_pitch, int format, bool inverted);

// Convert multiple image planes to a packed format
void PackImagePlanes(PIXEL *planes[], int num_planes, int width, int height, int pitch[],
					 uint8_t *output_buffer, uint32_t output_pitch, int format);

// Functions for manipulating fields within frames
void SplitFrameIntoFields(IMAGE *frame, IMAGE *field);

void InterleaveFieldsIntoFrame(IMAGE *even_field, int even_band,
							   IMAGE *odd_field, int odd_band,
							   IMAGE *frame, int output_band);

void SplitImageIntoBands(IMAGE *image, IMAGE *wavelet, int left_band, int right_band);

// Copy a band from one image to another
void CopyImageBand(IMAGE *input, int input_band, IMAGE *output, int output_band);

// Copy a subimage from one band to another
void CopySubImageBand(IMAGE *source_image, int source_band, SUBIMAGE *source_subimage,
					  IMAGE *target_image, int target_band, SUBIMAGE *target_subimage);

// Allocate a buffer large enough for a scratch image with the specified dimensions
#if _ALLOCATOR
PIXEL *CreateImageBuffer(ALLOCATOR *allocator, int pitch, int height, size_t *allocated_size);
PIXEL *ReallocImageBuffer(ALLOCATOR *allocator, PIXEL *buffer, int pitch, int height, size_t *allocated_size);
void DeleteImageBuffer(ALLOCATOR *allocator, PIXEL *buffer);
#else
PIXEL *CreateImageBuffer(int pitch, int height, size_t *allocated_size);
PIXEL *ReallocImageBuffer(PIXEL *buffer, int pitch, int height, size_t *allocated_size);
void DeleteImageBuffer(PIXEL *buffer);
#endif

// Downsample images in horizontal or vertical directions by a factor of two
void DownsampleWidth(PIXEL *imgInput, int pitchInput, PIXEL *imgOutput, int pitchOutput, ROI roi);
void DownsampleHeight(PIXEL *imgInput, int pitchInput, PIXEL *imgOutput, int pitchOutput, ROI roi);

void InterleaveColumns(PIXEL *imgEven, int pitchEven,
					   PIXEL *imgOdd, int pitchOdd,
					   PIXEL *imgOutput, int pitchOutput,
					   ROI roi);

void InterleaveRows(PIXEL *imgEven, int pitchEven,
					PIXEL *imgOdd, int pitchOdd,
					PIXEL *imgOutput, int pitchOutput,
					ROI roi);

void ComputeImageStatistics(IMAGE *wavelet);
void ComputeImageBandStatistics(IMAGE *image, int band_index);

HISTOGRAM *CreateImageHistogram(IMAGE *image, int band, int bucket_width);
void DeleteImageHistogram(HISTOGRAM *histogram);
HISTOGRAM *ComputeImageHistogram(IMAGE *image, int band);
void IncrementBucket(HISTOGRAM *histogram, PIXEL value);
PIXEL BucketValue(HISTOGRAM *histogram, int bucket);

void FillImageRandom(IMAGE *image, int nominal, int range, unsigned int seed);
void FillPixelMemory(PIXEL *array, int length, PIXEL value);
int ImageBandScale(IMAGE *image, int band);
void ConvertPackedToImage(uint8_t *data, int width, int height, int pitch, IMAGE *image);
int FindNonZero(PIXEL *row, int length);
int FindNonZeroPacked(PIXEL8S *row, int length);
int32_t CompareImages(IMAGE *image1, IMAGE *image2, PIXEL *error, int pitch);
int32_t CompareImageBands16s(IMAGE *image1, int band1, IMAGE *image2, int band2, PIXEL *residual, int pitch);
bool CompareImageBufferConstantYUV(uint8_t *buffer, int length, int y_value, int u_value, int v_value);

#ifdef _WIN32
int ColorTableLength(LPBITMAPINFOHEADER lpbi);
#endif

void OutputRGB(unsigned char *buffer, IMAGE *waveletY, IMAGE *waveletU, IMAGE *waveletV, int scale);

#if _DEBUG

void PrintImageHistogram(HISTOGRAM *histogram, FILE *file, char *label);
void DumpPGM(char *label, IMAGE *image, SUBIMAGE *subimage);
void DumpBandPGM(char *label, IMAGE *wavelet, int band, SUBIMAGE *subimage);
void DumpBandSignPGM(char *label, IMAGE *wavelet, int band, SUBIMAGE *subimage);
void DumpImage(char *label, IMAGE *image, SUBIMAGE *subimage, FILE *file);
void DumpImage8u(char *label, IMAGE *image, SUBIMAGE *subimage, FILE *file);
void DumpArray(char *label, PIXEL *array, int width, int height, int pitch, FILE *file);
void DumpArray8u(char *label, PIXEL8U *array, int width, int height, int pitch, FILE *file);
void DumpArray8s(char *label, PIXEL8S *array, int width, int height, int pitch, FILE *file);
void DumpArray16s(char *label, PIXEL16S *array, int width, int height, int pitch, FILE *file);
void DumpLine16s(char *label, PIXEL16S *array, int width, int line, int pitch, FILE *file);
void DumpLine8u(char *label, PIXEL8U *array, int width, int line, int pitch, FILE *file);
void DumpWavelet(char *label, IMAGE *wavelet, SUBIMAGE *subimage, FILE *file);
void DumpBand(char *label, IMAGE *wavelet, int band, SUBIMAGE *subimage, FILE *file);
void DumpBand16s(char *label, IMAGE *wavelet, int band, SUBIMAGE *subimage, FILE *file);
void DumpBand8s(char *label, IMAGE *wavelet, int band, SUBIMAGE *subimage, FILE *file);
void DumpQuad(char *label, IMAGE *wavelet, SUBIMAGE *subimage, FILE *file);
void DumpPair(char *label, IMAGE *wavelet, SUBIMAGE *subimage, FILE *file);

void DumpQuad16s8s(char *label,
				   PIXEL16S *lowlow_band, int lowlow_pitch,
				   PIXEL8S *lowhigh_band, int lowhigh_pitch,
				   PIXEL8S *highlow_band, int highlow_pitch,
				   PIXEL8S *highhigh_band, int highhigh_pitch,
				   int width, int height, FILE *file);

void DumpBandRow(PIXEL *data, int length, int type, FILE *file);
void DumpLowpassRow(IMAGE *wavelet, int row, FILE *file);
void DumpWaveletRow(IMAGE *wavelet, int band, int row, FILE *file);
void DumpWaveletRow8s(IMAGE *wavelet, int band, int row, FILE *file);
void DumpImageStatistics(char *label, IMAGE *wavelet, FILE *dump);
void DumpBandStatistics(char *label, IMAGE *wavelet, int band, FILE *file);
void DumpBufferStatistics(char *label, uint8_t *buffer, int width, int height, int pitch, FILE *file);

void DumpWaveletBandsPGM(IMAGE *wavelet, int frame_index, int num_channels);

#endif

#ifdef __cplusplus
}
#endif

#endif
