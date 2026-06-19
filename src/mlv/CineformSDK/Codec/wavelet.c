/*! @file wavelet.c

*  @brief Wavelet tools
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

#include "config.h"
#include "timing.h"

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include "../Common/macdefs.h"
#endif

#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif
#define TIMING (1 && _TIMING)
#define XMMOPT (1 && _XMMOPT)

#include <assert.h>

#include "wavelet.h"
#include "image.h"

#if (0 && _DEBUG)
#include "ipp.h"		// The Intel Performance Primitives are used in the test routines
#endif

#include "filter.h"
#include "debug.h"
#include "timing.h"
#include "string.h"
#include "codec.h"
#include "color.h"
#include "spatial.h"
#include "convert.h"
#include "temporal.h"
#include "quantize.h"
#include "buffer.h"
#include "decoder.h"

#if _RECURSIVE
#include "recursive.h"
#endif

#if __APPLE__
#include "macdefs.h"
#else
#ifndef ZeroMemory
#define ZeroMemory(p,s)		memset(p,0,s)
#endif
#endif

#if DEBUG
// Make the logfile available for debugging
#include <stdio.h>
extern FILE *logfile;
#endif

// Enable console output for debugging only on the Macintosh
#ifdef _WIN32
#define SYSLOG	0
#else
#define SYSLOG	(0 && DEBUG)
#endif

// Local functions
int FindUnusedBand(bool *band_in_use);

// Number of bands in each type of wavelet
static const int numWaveletBands[] =
{
	1, 2, 2, 4,		// Image, horizontal, vertical, spatial,
	2, 4, 4, 0,		// Temporal, horizontal-temporal, vertical-temporal, unimplemented,
	4, 4, 0, 0		// Temporal quad, horizontal quad, unimplemented, unimplemented
};

// Table of image descriptors used for the wavelet transforms
#ifndef IMAGE_TABLE_LENGTH
#define IMAGE_TABLE_LENGTH (TRANSFORM_MAX_WAVELETS * TRANSFORM_MAX_CHANNELS)
#endif

// Performance measurements
#if _TIMING

extern TIMER tk_spatial;				// Timers
extern TIMER tk_temporal;
extern TIMER tk_horizontal;
extern TIMER tk_vertical;
extern TIMER tk_frame;
extern TIMER tk_inverse;
extern TIMER tk_progressive;

extern TIMER tk_spatial1;
extern TIMER tk_spatial2;

extern COUNTER alloc_wavelet_count;		// Counters
extern COUNTER spatial_transform_count;
extern COUNTER temporal_transform_count;

#endif

#if _DEBUG

// Check that the wavelet bands are contained in the allocated memory block
static bool IsWaveletAllocationValid(IMAGE *wavelet)
{
	// Get the start and end address of the allocated memory block
	char *block_start_address = (char *)(wavelet->memory);
	char *block_limit_address = block_start_address + wavelet->memory_size;

	// Compute the size of the memory block that contains a band
	size_t allocated_band_size = wavelet->height * wavelet->pitch;

	int k;

	// Cannot use this routine if the wavelet was not allocated from a single memory block
	if (block_start_address == NULL)
		return false;

	for (k = 0; k < wavelet->num_bands; k++) {
		// Compute the address of the beginning of the band
		char *band_start_address = (char *)(wavelet->band[k]);

		// Compute the address immediately after the band
		char *band_limit_address = band_start_address + allocated_band_size;

		// Check that the band does not start before the allocated block
	/*	if (band_start_address < block_start_address)
			return false;

		// Check that the band does not extend beyond the allocated block
		if (band_limit_address > block_limit_address)
			return false;
			*/
	}

	// Allocation of the wavelet bands is okay
	return true;
}

#endif


void InitWavelet(IMAGE *wavelet, int width, int height, int level, int type, int half_width)
{
	int num_bands = numWaveletBands[type];
	int i;

	// Check that the wavelet type is valid
	assert(0 < type && type <= WAVELET_TYPE_HIGHEST);

	// Check that the number of bands is valid
	assert(0 < num_bands && num_bands <= IMAGE_NUM_BANDS);

	// Set the wavelet dimensions
	wavelet->width = width;
	wavelet->height = height;

	// Set the image type to wavelet
	wavelet->type = IMAGE_TYPE_WAVELET;

	// Set the type of wavelet
	wavelet->wavelet_type = type;

	// Initialize pointers into the high frequency bands
	if (num_bands == 2) {
		wavelet->band[1] = wavelet->band[0] + half_width;
		wavelet->band[2] = NULL;
		wavelet->band[3] = NULL;
	}
	else {
		// Initialize a four band wavelet
		wavelet->band[1] = wavelet->band[0] + half_width;
		wavelet->band[2] = wavelet->band[0] + wavelet->height * wavelet->pitch/sizeof(PIXEL);
		wavelet->band[3] = wavelet->band[2] + half_width;
	}

	// Check that all bands start on a int32_t word boundary
	assert(ISALIGNED16(wavelet->band[0]));
	assert(ISALIGNED16(wavelet->band[1]));
	assert(ISALIGNED16(wavelet->band[2]));
	assert(ISALIGNED16(wavelet->band[3]));

	// Indicate that the highpass bands share a common memory block
	for (i = 1; i < num_bands; i++)
		wavelet->alloc[i] = IMAGE_ALLOC_ONE_MALLOC;

	for (; i < IMAGE_NUM_BANDS; i++)
		wavelet->alloc[i] = IMAGE_ALLOC_UNALLOCATED;

	// Set the number of bands in use
	wavelet->num_bands = num_bands;

	// Set the level of the wavelet
	wavelet->level = level;

	// The new wavelet image is not linked into an image pyramid
	//assert(wavelet->reduced == NULL);
	//assert(wavelet->expanded == NULL);

	// The wavelet was not created from an existing image
	//wavelet->original = NULL;

	// Set the scale factors for display
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->scale[i] = 1;

	// Set the pixel type for all bands
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->pixel_type[i] = PIXEL_TYPE_16S;

#if 0
	// Set the divisor used during filter operations (will be set by the filter)
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->divisor[i] = 1;
#endif

#if 1
	// Initialize the amount of quantization applied to each band before encoding
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->quantization[i] = 1;
#endif

	// Initialize the wavelet to display the highpass bands as gray images
	wavelet->highpass_display = HIGHPASS_DISPLAY_GRAY;

	// Quad wavelets use the same pitch for both 16-bit and 8-bit pixels
	wavelet->pitch8s = wavelet->pitch;

	// The lowpass band is empty
	wavelet->valid_lowpass_band = false;

	// The highpass band is empty
	wavelet->valid_highpass_band = false;
}

#if _ALLOCATOR
void AllocWavelet(ALLOCATOR *allocator, IMAGE *wavelet, int width, int height, int level, int type)
#else
void AllocWavelet(IMAGE *wavelet, int width, int height, int level, int type)
#endif
{
	int image_width;		// Dimensions of the image that contains the quad wavelet
	int image_height;
	int half_width;			// Width of each wavelet band
	int num_bands;			// Number of wavelet bands

	// Adjust the width so that all bands start on a 16 byte boundary
	half_width = ALIGN16(width);
	image_width = 2 * half_width;

	// The number of wavelet bands depends on the type fo wavelet
	switch (type)
	{
	case WAVELET_TYPE_HORIZONTAL:
	case WAVELET_TYPE_VERTICAL:
	case WAVELET_TYPE_TEMPORAL:
		image_height = height;
		num_bands = 2;
		break;

	case WAVELET_TYPE_SPATIAL:
	case WAVELET_TYPE_HORZTEMP:
	case WAVELET_TYPE_VERTTEMP:
	default:
		image_height = 2 * height;
		num_bands = 4;
		break;
	}

	// Allocate a new image for the wavelet
#if _ALLOCATOR
	AllocImage(allocator, wavelet, image_width, image_height);
#else
	AllocImage(wavelet, image_width, image_height);
#endif

	InitWavelet(wavelet, width, height, level, type, half_width);
}

// Initialize a wavelet with the band rows close together in memory
void InitWaveletStack(IMAGE *wavelet, int width, int height, int pitch, int level, int type)
{
	size_t band_size;
	int band_offset;
	int num_bands = numWaveletBands[type];
	int i;

	// Check that the wavelet type is valid
	assert(0 < type && type <= WAVELET_TYPE_HIGHEST);

	// Check that the number of bands is valid
	assert(0 < num_bands && num_bands <= IMAGE_NUM_BANDS);

	// Calculate the size of each band (in bytes)
	band_size = height * pitch;

	// Start each band on a cache line boundary
	band_size = ALIGN(band_size, _CACHE_LINE_SIZE);

	// Calculate the band size in pixels
	band_offset = (int)band_size/sizeof(PIXEL);

	// Set the wavelet dimensions
	wavelet->width = width;
	wavelet->height = height;
	wavelet->pitch = pitch;

	// Set the image type to wavelet
	wavelet->type = IMAGE_TYPE_WAVELET;

	// Set the type of wavelet
	wavelet->wavelet_type = type;

	// Initialize pointers into the high frequency bands
	if (num_bands == 2) {
		wavelet->band[1] = wavelet->band[0] + band_offset;
		wavelet->band[2] = NULL;
		wavelet->band[3] = NULL;
	}
	else {
		// Initialize a four band wavelet
		wavelet->band[1] = wavelet->band[0] + band_offset;
		wavelet->band[2] = wavelet->band[1] + band_offset;
		wavelet->band[3] = wavelet->band[2] + band_offset;
	}

	// Check that all bands start on a cache line boundary
	assert(ISALIGNED(wavelet->band[0], _CACHE_LINE_SIZE));
	assert(ISALIGNED(wavelet->band[1], _CACHE_LINE_SIZE));
	assert(ISALIGNED(wavelet->band[2], _CACHE_LINE_SIZE));
	assert(ISALIGNED(wavelet->band[3], _CACHE_LINE_SIZE));

	// Indicate that the highpass bands share a common memory block
	for (i = 1; i < num_bands; i++)
		wavelet->alloc[i] = IMAGE_ALLOC_ONE_MALLOC;

	for (; i < IMAGE_NUM_BANDS; i++)
		wavelet->alloc[i] = IMAGE_ALLOC_UNALLOCATED;

	// Set the number of bands in use
	wavelet->num_bands = num_bands;

	// Set the level of the wavelet
	wavelet->level = level;

	// The new wavelet image is not linked into an image pyramid
	//assert(wavelet->reduced == NULL);
	//assert(wavelet->expanded == NULL);

	// The wavelet was not created from an existing image
	//wavelet->original = NULL;

	// Set the scale factors for display
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->scale[i] = 1;

	// Set the pixel type for all bands
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->pixel_type[i] = PIXEL_TYPE_16S;

#if 0
	// Set the divisor used during filter operations (will be set by the filter)
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->divisor[i] = 1;
#endif

#if 1
	// Initialize the amount of quantization applied to each band before encoding
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->quantization[i] = 1;
#endif

#if 1
	// Set the default quantization
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->quant[i] = 1;
#endif

	// Initialize the wavelet to display the highpass bands as gray images
	wavelet->highpass_display = HIGHPASS_DISPLAY_GRAY;

	// Compute a more compact pitch for use with bands that contain 8-bit pixels.
	// Warning: This is work in progress so not all 8-bit transforms use this pitch.
#if 1
	wavelet->pitch8s = ALIGN16(wavelet->width);
#else
	// For debugging
	//wavelet->pitch8s = ALIGN16(wavelet->width) + _CACHE_LINE_SIZE;
	wavelet->pitch8s = ALIGN(wavelet->width, _CACHE_LINE_SIZE);
	//wavelet->encoded_pitch = ALIGN16(wavelet->width);
#endif

	// The lowpass band is empty
	wavelet->valid_lowpass_band = false;

	// The highpass band is empty
	wavelet->valid_highpass_band = false;
}

// Allocate a wavelet but keep the rows close together in memory
#if _ALLOCATOR
void AllocWaveletStack(ALLOCATOR *allocator, IMAGE *wavelet, int width, int height, int level, int type)
#else
void AllocWaveletStack(IMAGE *wavelet, int width, int height, int level, int type)
#endif
{
	int pitch;				// Distance between each row (in bytes)
	int num_bands;			// Number of wavelet bands
	int image_width;		// Dimensions of allocated image
	int image_height;
	size_t band_size;		// Size of each wavelet band (in bytes)

	// Compute the pitch of the wavelet rows
	pitch = width * sizeof(PIXEL);

	// Adjust the pitch so that all rows start on a 16 byte boundary
	pitch = ALIGN16(pitch);
	//pitch = ALIGN(pitch, 32);
	//pitch = ALIGN(pitch, _CACHE_LINE_SIZE);

	// The number of wavelet bands depends on the type of wavelet
	switch (type)
	{
	case WAVELET_TYPE_HORIZONTAL:
	case WAVELET_TYPE_VERTICAL:
	case WAVELET_TYPE_TEMPORAL:
		num_bands = 2;
		break;

	case WAVELET_TYPE_SPATIAL:
	case WAVELET_TYPE_HORZTEMP:
	case WAVELET_TYPE_VERTTEMP:
	default:
		num_bands = 4;
		break;
	}

	// Calculate the dimensions of an image that can contain the wavelet
	image_width = pitch/sizeof(PIXEL);
	image_height = num_bands * height;
	band_size = height * pitch;

	// Adjust the image allocation so that each band can start on a cache line boundary
	if (!ISALIGNED(band_size, _CACHE_LINE_SIZE)) {
		size_t image_size;
		band_size = ALIGN(band_size, _CACHE_LINE_SIZE);
		image_size = band_size * num_bands;
		image_height = (int)(image_size + pitch - 1) / pitch;
	}

	// Allocate a new image for the wavelet
#if _ALLOCATOR
	AllocImage(allocator, wavelet, image_width, image_height);
#else
	AllocImage(wavelet, image_width, image_height);
#endif
	assert(wavelet->band[0] != NULL);

#if (1 && SYSLOG)
	fprintf(stderr, "AllocWaveletStack wavelet: 0x%p, image address: 0x%p\n", wavelet, wavelet->band[0]);
#endif

	// Initialize the wavelet image descriptor
	InitWaveletStack(wavelet, width, height, pitch, level, type);
	assert(wavelet->band[num_bands - 1] != NULL);

	// Check that the wavelet bands are within the allocated memory
#if _DEBUG
	assert(IsWaveletAllocationValid(wavelet));
#endif
}

// Create a four band wavelet image with each band width by height
#if _ALLOCATOR
IMAGE *CreateWavelet(ALLOCATOR *allocator, int width, int height, int level)
#else
IMAGE *CreateWavelet(int width, int height, int level)
#endif
{
	IMAGE *wavelet;
	int image_width, image_height;
	int half_width;
	int wavelet_type = WAVELET_TYPE_SPATIAL;
	//int i;
	
	// Adjust the width so that all bands start on a 16 byte boundary
	half_width = ALIGN16(width);
	image_width = 2 * half_width;
	image_height = 2 * height;

	// Create a new image with the same dimensions as the existing image
#if _ALLOCATOR
	wavelet = CreateImage(allocator, image_width, image_height);
#else
	wavelet = CreateImage(image_width, image_height);
#endif
	assert(wavelet != NULL);
	if (wavelet == NULL) return NULL;

#if 1

	InitWavelet(wavelet, width, height, level, wavelet_type, half_width);

#else

	// Set the image type to wavelet
	wavelet->type = IMAGE_TYPE_WAVELET;

	// Adjust the dimensions of the new image to account for downsampling
	wavelet->width /= 2;
	wavelet->height /= 2;

	// Wavelet must not be rounded up to a larger size than requested
	if (wavelet->width > width) wavelet->width = width;
	if (wavelet->height > height) wavelet->height = height;

	// Initialize pointers into the high frequency bands
	wavelet->band[1] = wavelet->band[0] + half_width;
	wavelet->band[2] = wavelet->band[0] + wavelet->height * wavelet->pitch/sizeof(PIXEL);
	wavelet->band[3] = wavelet->band[2] + half_width;

	// Indicate that the highpass bands share a common memory block
	for (i = 1; i <= 3; i++)
		wavelet->alloc[i] = IMAGE_ALLOC_ONE_MALLOC;

	// Check that all bands start on a int32_t word boundary
	assert(LONG_ALIGNED(wavelet->band[0]));
	assert(LONG_ALIGNED(wavelet->band[1]));
	assert(LONG_ALIGNED(wavelet->band[2]));
	assert(LONG_ALIGNED(wavelet->band[3]));

	// Set the number of bands in use
	wavelet->num_bands = 4;

	// Set the level of the wavelet
	wavelet->level = level;

	// Note: The new wavelet image is not linked into an image pyramid by this routine
	assert(wavelet->reduced == NULL);
	assert(wavelet->expanded == NULL);

	// Save the dimensions of each wavelet band
	//wavelet->band_width = wavelet->width;
	//wavelet->band_height = wavelet->height;

	// The wavelet was not created from an existing image
	wavelet->original = NULL;

	// Assume that this is a spatial (horizontal and vertical) wavelet
	wavelet->wavelet_type = WAVELET_TYPE_SPATIAL;

	// Set the scale factors for display
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->scale[i] = 1;

	// Set the pixel type for all bands
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->pixel_type[i] = PIXEL_TYPE_16S;

#if 0
	// Set the divisor used during filter operations (will be set by the filter)
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->divisor[i] = 1;
#endif

#if 1
	// Initialize the amount of quantization applied to each band before encoding
	for (i = 0; i < IMAGE_NUM_BANDS; i++)
		wavelet->quantization[i] = 1;
#endif

	// Initialize the wavelet to display the highpass bands as gray images
	wavelet->highpass_display = HIGHPASS_DISPLAY_GRAY;

#endif

	return wavelet;
}

// Create a new wavelet image from an existing image
#if _ALLOCATOR
IMAGE *CreateWaveletFromImage(ALLOCATOR *allocator, IMAGE *image)
#else
IMAGE *CreateWaveletFromImage(IMAGE *image)
#endif
{
	int wavelet_width;
	int wavelet_height;
	int wavelet_level;
	IMAGE *wavelet;

	// Check for an image descriptor
	assert(image != NULL);
	if (image == NULL) return NULL;

	// Compute the dimensions of each wavelet band
	wavelet_width = image->width / 2;
	wavelet_height = image->height / 2;

	// Set the level of the wavelet relative to the level of the existing image
	wavelet_level = image->level + 1;

	// Create a wavelet with the specified dimensions for each band
#if _ALLOCATOR
	wavelet = CreateWavelet(allocator, wavelet_width, wavelet_height, wavelet_level);
#else
	wavelet = CreateWavelet(wavelet_width, wavelet_height, wavelet_level);
#endif

#if 0
	assert(wavelet != NULL);
	if (wavelet != NULL) {
		// Remember the image used to create this wavelet
		if (image->type == IMAGE_TYPE_GRAY) {
			wavelet->original = image;
		}
		else {
			assert(image->type == IMAGE_TYPE_WAVELET);
			wavelet->original = image->original;
		}
	}
#endif

	return wavelet;
}

// Create wavelet image that is twice as large as the argument wavelet
#if _ALLOCATOR
IMAGE *CreateExpandedWavelet(ALLOCATOR *allocator, IMAGE *wavelet)
#else
IMAGE *CreateExpandedWavelet(IMAGE *wavelet)
#endif
{
	int wavelet_width;
	int wavelet_height;
	int wavelet_level;
	IMAGE *new_wavelet;

	// Check for an image descriptor
	assert(wavelet != NULL);
	if (wavelet == NULL) return NULL;

	// Compute the dimensions of this larger wavelet
	wavelet_width = 2 * wavelet->width;
	wavelet_height = 2 * wavelet->height;

	// Set the level of the wavelet relative to the level of the existing image
	wavelet_level = wavelet->level - 1;

	// Create a wavelet with the specified dimensions for each band
#if _ALLOCATOR
	new_wavelet = CreateWavelet(allocator, wavelet_width, wavelet_height, wavelet_level);
#else
	new_wavelet = CreateWavelet(wavelet_width, wavelet_height, wavelet_level);
#endif
#if 0
	assert(wavelet != NULL);
	if (wavelet != NULL) {
		// Link the wavelets into a pyramid
		new_wavelet->reduced = wavelet;
		wavelet->expanded = new_wavelet;

		// Remember the image used to create this wavelet
		new_wavelet->original = wavelet->original;
	}
#endif

	return new_wavelet;
}

#if _ALLOCATOR
IMAGE *CreateWaveletFromArray(ALLOCATOR *allocator, PIXEL *array,
							  int width, int height, int pitch,
							  int level, int type)
#else
IMAGE *CreateWaveletFromArray(PIXEL *array, int width, int height, int pitch, int level, int type)
#endif
{
	int wavelet_width;
	int wavelet_height;
	int wavelet_pitch;
	int wavelet_level = level;
	int num_bands;

	// Create a wavelet with the specified dimensions for each band
#if _ALLOCATOR
	IMAGE *wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
#else
	IMAGE *wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
#endif

	if (wavelet != NULL)
	{
		size_t band_size;
		int i;

		// Zero all fields
		memset(wavelet, 0, sizeof(IMAGE));

		// Compute the dimensions of each wavelet band
		switch (type)
		{

		case WAVELET_TYPE_IMAGE:
			wavelet_width = width;
			wavelet_height = height;
			wavelet_pitch = pitch;
			num_bands = 1;
			break;

		case WAVELET_TYPE_HORIZONTAL:
		case WAVELET_TYPE_VERTICAL:
			wavelet_width = width / 2;
			wavelet_height = height;
			wavelet_pitch = pitch / 2;
			num_bands = 2;
			break;

		case WAVELET_TYPE_TEMPORAL:
			wavelet_width = width;
			wavelet_height = height;
			wavelet_pitch = pitch;
			num_bands = 2;
			break;

		case WAVELET_TYPE_SPATIAL:
		case WAVELET_TYPE_HORZTEMP:
		case WAVELET_TYPE_VERTTEMP:
		default:
			wavelet_width = width / 2;
			wavelet_height = height / 2;
			wavelet_pitch = pitch / 2;
			num_bands = 4;
			break;
		}

		// Initialize the wavelet dimensions
		wavelet->width = wavelet_width;
		wavelet->height = wavelet_height;

		// Set the pitch
		wavelet->pitch = wavelet_pitch;

		// Calculate the size of each band
		band_size = wavelet_height * pitch/sizeof(PIXEL);

		// Initialize the wavelet bands
		if (num_bands > 0)
			wavelet->band[0] = array;

		if (num_bands > 1)
			wavelet->band[1] = wavelet->band[0] + band_size;

		if (num_bands > 3) {
			wavelet->band[2] = wavelet->band[1] + band_size;
			wavelet->band[3] = wavelet->band[2] + band_size;
		}

		// Set the image type to wavelet
		wavelet->type = IMAGE_TYPE_WAVELET;

		// Set the wavelet level
		wavelet->level = wavelet_level;

		// Set the wavelet type
		wavelet->wavelet_type = type;

		// Set the number of bands
		wavelet->num_bands = num_bands;

		// Save the dimensions of the wavelet bands before filtering
		//wavelet->band_width = wavelet_width;
		//wavelet->band_height = wavelet_height;

		// Indicate that the wavelet was allocated from an existing array
		for (i = 0; i < IMAGE_NUM_BANDS; i++) {
			wavelet->alloc[i] = IMAGE_ALLOC_STATIC_DATA;
		}

#if 0
		// Set the divisor used during filter operations (will be set by the filter)
		for (i = 0; i < IMAGE_NUM_BANDS; i++) {
			wavelet->divisor[i] = 1;
		}
#endif

#if 1
		// Initialize the amount of quantization applied to each band before encoding
		for (i = 0; i < IMAGE_NUM_BANDS; i++) {
			wavelet->quantization[i] = 1;
		}
#endif
	}
	else
	{
#if (DEBUG && _WIN32)
		OutputDebugString("CreateWaveletFromArray sizeof(IMAGE)");
#endif
		assert(wavelet != NULL);
	}

	return wavelet;
}

// Create a wavelet of the specified type with each band width by height
#if _ALLOCATOR
IMAGE *CreateWaveletEx(ALLOCATOR *allocator, int width, int height, int level, int type)
#else
IMAGE *CreateWaveletEx(int width, int height, int level, int type)
#endif
{
	IMAGE *wavelet;

	// Allocate an image descriptor for the wavelet
#if _ALLOCATOR
	wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
#else
	wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
#endif

	if (wavelet != NULL)
	{
		// Allocate memory for the wavelet bands and initialize the descriptor
#if _ALLOCATOR
		AllocWaveletStack(allocator, wavelet, width, height, level, type);
#else
		AllocWaveletStack(wavelet, width, height, level, type);
#endif
	}
	else
	{
#if (DEBUG && _WIN32)
		OutputDebugString("CreateWaveletEx sizeof(IMAGE)");
#endif
		assert(wavelet != NULL);
	}

	return wavelet;
}

#if _ALLOCATOR
IMAGE *ReallocWaveletEx(ALLOCATOR *allocator, IMAGE *wavelet, int width, int height, int level, int type)
#else
IMAGE *ReallocWaveletEx(IMAGE *wavelet, int width, int height, int level, int type)
#endif
{
	if (wavelet != NULL)
	{
#if 1
		// Just return the wavelet if it is the same as requested
		if (wavelet->width == width &&
			wavelet->height >= height &&		// Allow for padding
			wavelet->level == level &&
			//wavelet->wavelet_type == type &&
			wavelet->memory)	// now that we sometimes MEMORY_FREE memory, we need to check if it is allocated.
#else
		// Just return the wavelet if it is the same as requested
		if (wavelet->width == width &&
			wavelet->height == height &&
			wavelet->level == level &&
			wavelet->wavelet_type == type &&
			wavelet->memory)	// now that we sometimes MEMORY_FREE memory, we need to check if it is allocated.
#endif
		{
			// Do we need to reinitialize any parameters in the wavelet?
			//wavelet->lowpass_border = 0;
			//wavelet->highpass_border = 0;
#if 1
			// Force the correct wavelet type
			if (wavelet->wavelet_type == 5 && type == 3) {
				wavelet->wavelet_type = type;
			}
#endif
			return wavelet;
		}

		// Need to free this wavelet and create another
#if _ALLOCATOR
		DeleteImage(allocator, wavelet);
#else
		DeleteImage(wavelet);
#endif
	}

	// Allocate a new wavelet
#if _ALLOCATOR
	wavelet = CreateWaveletEx(allocator, width, height, level, type);
#else
	wavelet = CreateWaveletEx(width, height, level, type);
#endif
#if _TIMING
	alloc_wavelet_count++;
#endif

	// Invalidate the data in the bands
	wavelet->band_valid_flags = 0;
	wavelet->band_started_flags = 0;

	return wavelet;
}

#if _ALLOCATOR
IMAGE *CreateWaveletFromImageEx(ALLOCATOR *allocator, IMAGE *image, int level, int type)
#else
IMAGE *CreateWaveletFromImageEx(IMAGE *image, int level, int type)
#endif
{
#if _ALLOCATOR
	IMAGE *wavelet = CreateWaveletFromImage(allocator, image);
#else
	IMAGE *wavelet = CreateWaveletFromImage(image);
#endif
	wavelet->level = level;
	wavelet->wavelet_type = WAVELET_TYPE_SPATIAL;

	return wavelet;
}

// Initialize a transform data structure
void InitTransform(TRANSFORM *transform)
{
	//int i;

	assert(transform != NULL);

	// Indicate that the transform data structure is unused
	transform->num_levels = 0;
	transform->width = 0;
	transform->height = 0;

	// No buffer has been allocated for image processing
	transform->buffer = NULL;
	transform->size = 0;

#if _RECURSIVE

	transform->row_buffer = NULL;

	// State information for each wavelet in the resursion
	for (i = 0; i < TRANSFORM_MAX_WAVELETS; i++)
	{
		InitTransformState(&transform->state[i], transform);
	}

	ZeroMemory(transform->rowptr, sizeof(transform->rowptr));

	ZeroMemory(transform->descriptor, sizeof(transform->descriptor));

#endif

#if _DEBUG

	transform->logfile = NULL;

#endif

}

// Initialize an array of transforms
#if 0
void InitTransformArray(TRANSFORM **transform, int num_transforms)
{
#if _ALLOCATOR
	//TODO: Modify this routine to pass an allocator as the first argument
	ALLOCATOR *allocator = decoder->allocator;
#endif

	int channel;

	for (channel = 0; channel < num_transforms; channel++)
	{
#if _ALLOCATOR
		transform[channel] = (TRANSFORM *)Alloc(allocator, sizeof(TRANSFORM));
#else
		transform[channel] = (TRANSFORM *)MEMORY_ALLOC(sizeof(TRANSFORM));
#endif
		if(transform[channel] == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("InitTransformArray sizeof(TRANSFORM))");
#endif
			assert(transform[channel] != NULL);
		}


		InitTransform(transform[channel]);
	}
}
#endif

#if _ALLOCATOR
void ClearTransform(ALLOCATOR *allocator, TRANSFORM *transform)
#else
void ClearTransform(TRANSFORM *transform)
#endif
{
	if (transform == NULL) return;

	// Free the image processing buffer (if allocated)
	if (transform->buffer != NULL)
	{
#if _ALLOCATOR
		FreeAligned(allocator, transform->buffer);
#else
		MEMORY_ALIGNED_FREE(transform->buffer);
#endif
		transform->buffer = NULL;
		transform->size = 0;
	}

	// Free wavelets created during transform processing
	if (transform->num_levels > 0)
	{
		int num_wavelets = transform->num_wavelets;
		int i;

		for (i = 0; i < num_wavelets; i++) {
			IMAGE *wavelet = transform->wavelet[i];
			if (wavelet != NULL)
			{
				// Free the allocated memory and the wavelet itself
#if _ALLOCATOR
				DeleteImage(allocator, wavelet);
#else
				DeleteImage(wavelet);
#endif
			}
		}
	}

	// Clear the transform data structure
	memset(transform, 0, sizeof(TRANSFORM));
	transform->num_levels = 0;
}

#if _ALLOCATOR
void FreeTransform(ALLOCATOR *allocator, TRANSFORM *transform)
#else
void FreeTransform(TRANSFORM *transform)
#endif
{
	int i;

	if (transform == NULL) return;

	// Free the image processing buffer (if allocated)
	if (transform->buffer != NULL)
	{
#if _ALLOCATOR
		FreeAligned(allocator, transform->buffer);
#else
		MEMORY_ALIGNED_FREE(transform->buffer);
#endif
		transform->buffer = NULL;
		transform->size = 0;
	}

	// Free wavelets created during transform processing
	for (i = 0; i < TRANSFORM_MAX_WAVELETS; i++)
	{
		IMAGE *wavelet = transform->wavelet[i];
		if (wavelet != NULL)
		{
			// Free the allocated memory and the wavelet itself
#if _ALLOCATOR
			DeleteImage(allocator, wavelet);
#else
			DeleteImage(wavelet);
#endif
		}
	}

	// Free the transform data structure
#if _ALLOCATOR
	Free(allocator, transform);
#else
	MEMORY_FREE(transform);
#endif
}

// Return the number of subbands in the transform
int SubbandCount(TRANSFORM *transform)
{
	int subband_count = 0;

	switch (transform->type)
	{
	case TRANSFORM_TYPE_SPATIAL:
		// Three subbands in each spatio-temporal wavelet per frame
		subband_count += (3 * transform->num_frames);

		// Three highpass bands in each spatial transform
		subband_count += 3 * (transform->num_spatial);

		// Plus one subband for the lowpass image
		subband_count++;
		break;

	case TRANSFORM_TYPE_FIELD:
		// Three subbands in each spatio-temporal wavelet per frame
		subband_count += (3 * transform->num_frames);

		// One subband in each temporal transform between frames
		subband_count += transform->num_frames - 1;

		// Three highpass bands in each spatial transform
		subband_count += 3 * (transform->num_levels - transform->num_frames);

		// Plus one subband for the lowpass image
		subband_count++;
		break;

	case TRANSFORM_TYPE_FIELDPLUS:
		// Three subbands in each spatio-temporal wavelet per frame
		subband_count += (3 * transform->num_frames);

		// One subband in each temporal transform between frames
		subband_count += transform->num_frames - 1;

		// Three highpass bands in each spatial transform
		subband_count += 3 * (transform->num_spatial);

		// Plus one subband for the lowpass image
		subband_count++;
		break;

	case TRANSFORM_TYPE_FRAME:
		assert(0);
		break;

	case TRANSFORM_TYPE_INTERLACED:
		assert(0);
		break;

	default:
		assert(0);
		break;
	}

	return subband_count;
}

// Allocate transform wavelets from dynamic memory
#if _ALLOCATOR
void AllocTransform(ALLOCATOR *allocator, TRANSFORM *transform, int type,
					int width, int height, int num_frames, int num_spatial)
#else
void AllocTransform(TRANSFORM *transform, int type, int width, int height, int num_frames, int num_spatial)
#endif
{
	IMAGE *wavelet;
	int wavelet_width;
	int wavelet_height;
	int wavelet_level;
	int wavelet_type;
	int k = 0;
	int i;

	// Ignore this call if the transform has already been allocated as requested
	if (transform->num_frames == num_frames &&
		transform->num_spatial == num_spatial &&
		transform->type == (TRANSFORM_TYPE)type &&
		transform->width == width &&
		transform->height == height) return;

	// Need to handle the case where the wavelet must be reallocated

	// Routine only knows how to allocate a field or fieldplus transform or spatial transform
	assert(type == TRANSFORM_TYPE_FIELDPLUS || type == TRANSFORM_TYPE_FIELD || type == TRANSFORM_TYPE_SPATIAL);

	// Must have two frames in the group (except if intra frame)
	assert((type == TRANSFORM_TYPE_SPATIAL && num_frames == 1) || num_frames == 2);

	// Initialize the array of prescale shifts
	memset(transform->prescale, 0, sizeof(transform->prescale));

#if 1
	switch (type)
	{
	case TRANSFORM_TYPE_SPATIAL:

		transform->type = TRANSFORM_TYPE_SPATIAL;
		transform->num_frames = num_frames;
		transform->num_spatial = num_spatial;
		transform->num_levels = num_spatial + 1;
		transform->num_wavelets = transform->num_levels;

		// Allocate one frame (temporal and horizontal) wavelet
		wavelet_width = width / 2;
		wavelet_height = height / 2;
		wavelet_level = 1;
		wavelet_type = WAVELET_TYPE_FRAME;

#if _ALLOCATOR
		wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(allocator, wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#else
		wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#endif
		transform->wavelet[k++] = wavelet;

		// Allocate the spatial wavelets
		wavelet_type = WAVELET_TYPE_SPATIAL;

		for (i = 0; i < num_spatial; i++)
		{
			// Reduce the size of each wavelet band
			wavelet_width /= 2;
			wavelet_height /= 2;
			wavelet_level++;

			// Spatial wavelet for the temporal lowpass
#if _ALLOCATOR
			wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
			if(wavelet == NULL)
			{
#if (DEBUG && _WIN32)
				OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
				assert(wavelet != NULL);
			}
			AllocWaveletStack(allocator, wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#else
			wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
			if(wavelet == NULL)
			{
#if (DEBUG && _WIN32)
				OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
				assert(wavelet != NULL);
			}
			AllocWaveletStack(wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#endif
			transform->wavelet[k++] = wavelet;
		}

		// Save the dimensions that were used to allocate the transform
		transform->width = width;
		transform->height = height;
#if 1
		// Allocate a buffer for image processing (if necessary)
		if (transform->buffer == NULL) {
			int pitch = ALIGN16(width * sizeof(PIXEL));
			size_t size = (height * pitch);
#if _ALLOCATOR
			transform->buffer = (PIXEL *)AllocAligned(allocator, size, 16);
#else
			transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
#endif
			assert(transform->buffer != NULL);
			transform->size = size;
		}
#endif
		break;

	case TRANSFORM_TYPE_FIELD:

		transform->type = TRANSFORM_TYPE_FIELD;
		transform->num_frames = num_frames;
		transform->num_spatial = num_spatial;
		transform->num_levels = transform->num_spatial + TRANSFORM_FIELD_BASE_LEVELS;
		transform->num_wavelets = transform->num_levels + 1;

		// Allocate two frame (temporal and horizontal) wavelets
		wavelet_width = width / 2;
		wavelet_height = height / 2;
		wavelet_level = 1;
		wavelet_type = WAVELET_TYPE_FRAME;

#if _ALLOCATOR
		wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(allocator, wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#else
		wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#endif
		transform->wavelet[k++] = wavelet;

#if _ALLOCATOR
		wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(allocator, wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#else
		wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#endif
		transform->wavelet[k++] = wavelet;

		// Allocate a temporal wavelet
		wavelet_level++;
		wavelet_type = WAVELET_TYPE_TEMPORAL;

#if _ALLOCATOR
		wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(allocator, wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#else
		wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#endif
		transform->wavelet[k++] = wavelet;

		// Allocate the spatial wavelets
		wavelet_type = WAVELET_TYPE_SPATIAL;
		for (k = 0; k < transform->num_spatial; k++) {
			wavelet_width /= 2;
			wavelet_height /= 2;
			wavelet_level++;

#if _ALLOCATOR
			wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
			if(wavelet == NULL)
			{
#if (DEBUG && _WIN32)
				OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
				assert(wavelet != NULL);
			}
			AllocWaveletStack(allocator, wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#else
			wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
			if(wavelet == NULL)
			{
#if (DEBUG && _WIN32)
				OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
				assert(wavelet != NULL);
			}
			AllocWaveletStack(wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#endif
			transform->wavelet[k++] = wavelet;
		}

		// Save the dimensions that were used to allocate the transform
		transform->width = width;
		transform->height = height;

#if 1
		// Allocate a buffer for image processing (if necessary)
		if (transform->buffer == NULL) {
			int pitch = ALIGN16(width * sizeof(PIXEL));
			size_t size = (height * pitch);
#if _ALLOCATOR
			transform->buffer = (PIXEL *)AllocAligned(allocator, size, 16);
#else
			transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
#endif
			assert(transform->buffer != NULL);
			transform->size = size;
		}
#endif
		break;

	// Field+ transform performs one additional level of spatial transform on temporal highpass band
	case TRANSFORM_TYPE_FIELDPLUS:

		transform->type = TRANSFORM_TYPE_FIELDPLUS;
		transform->num_frames = num_frames;
		transform->num_spatial = num_spatial;
		transform->num_levels = 2 + TRANSFORM_FIELD_BASE_LEVELS;
		transform->num_wavelets = transform->num_levels + 2;

		// Allocate two frame (temporal and horizontal) wavelets
		wavelet_width = width / 2;
		wavelet_height = height / 2;
		wavelet_level = 1;
		wavelet_type = WAVELET_TYPE_FRAME;

#if _ALLOCATOR
		wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(allocator, wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#else
		wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#endif
		transform->wavelet[k++] = wavelet;

#if _ALLOCATOR
		wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(allocator, wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#else
		wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#endif
		transform->wavelet[k++] = wavelet;

		// Allocate a temporal wavelet
		wavelet_level++;
		wavelet_type = WAVELET_TYPE_TEMPORAL;

#if _ALLOCATOR
		wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(allocator, wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#else
		wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#endif
		transform->wavelet[k++] = wavelet;

		// Allocate the spatial wavelets
		wavelet_type = WAVELET_TYPE_SPATIAL;

		wavelet_width /= 2;
		wavelet_height /= 2;
		wavelet_level++;

		// Spatial wavelet for the temporal lowpass
#if _ALLOCATOR
		wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(allocator, wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#else
		wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#endif
		transform->wavelet[k++] = wavelet;

		// Spatial wavelet for the temporal highpass
#if _ALLOCATOR
		wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(allocator, wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#else
		wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#endif
		transform->wavelet[k++] = wavelet;

		wavelet_width /= 2;
		wavelet_height /= 2;
		wavelet_level++;

#if _ALLOCATOR
		wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(allocator, wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#else
		wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
		if(wavelet == NULL)
		{
#if (DEBUG && _WIN32)
			OutputDebugString("AllocTransform sizeof(IMAGE))");
#endif
			assert(wavelet != NULL);
		}
		AllocWaveletStack(wavelet, wavelet_width, wavelet_height, wavelet_level, wavelet_type);
#endif
		transform->wavelet[k++] = wavelet;

		// Save the dimensions that were used to allocate the transform
		transform->width = width;
		transform->height = height;
#if 1
		// Allocate a buffer for image processing (if necessary)
		if (transform->buffer == NULL) {
			int pitch = ALIGN16(width * sizeof(PIXEL));
			size_t size = (height * pitch);
			transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
			assert(transform->buffer != NULL);
			transform->size = size;
		}
#endif
		break;

	default:			// Unsupported type of transform
		assert(0);
	}
#endif
}

void SetTransformFrame(TRANSFORM *transform, int width, int height)
{
	transform->width = width;
	transform->height = height;
}

/////////////////////////////////////////////////////////////////////////
/////////////  Do Not Change -- required for backward compatibility
/////////////////////////////////////////////////////////////////////////
static int spatial_prescale[]   = {0, 2, 0, 0,  0, 0, 0, 0};
static int fieldplus_prescale[] = {0, 0, 0, 0,  2, 0, 0, 0};
								 //frm0, frm1, temp diff, temnp high, spatial, spatial, 0 0
	/////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////
// This is for the decoder only as it sets up backward compatible tables for versions of the codec Pre-2007.
void GetTransformPrescale(TRANSFORM *transform, int transform_type, int input_precision)
{

	if (input_precision == CODEC_PRECISION_8BIT) {
		memset(transform->prescale, 0, sizeof(transform->prescale));
		return;
	}

	if(input_precision == CODEC_PRECISION_10BIT)
	{
		switch (transform_type)
		{
		case TRANSFORM_TYPE_SPATIAL:
			assert(sizeof(transform->prescale) == sizeof(spatial_prescale));
			memcpy(transform->prescale, spatial_prescale, sizeof(spatial_prescale));
			break;

	/*	case TRANSFORM_TYPE_FIELD:
			assert(sizeof(transform->prescale) == sizeof(field_prescale));
			memcpy(transform->prescale, field_prescale, sizeof(field_prescale));
			break;
	*/
		case TRANSFORM_TYPE_FIELDPLUS:
			assert(sizeof(transform->prescale) == sizeof(fieldplus_prescale));
			memcpy(transform->prescale, fieldplus_prescale, sizeof(fieldplus_prescale));
			break;

		default:
			assert(0);
			memset(transform->prescale, 0, sizeof(transform->prescale));
			break;
		}
	}
	else if(input_precision == CODEC_PRECISION_12BIT)
	{
		switch (transform_type)
		{
		case TRANSFORM_TYPE_SPATIAL:
			{
			int spatial_prescale[]   = {0, 2, 2, 0,  0, 0, 0, 0};
			assert(sizeof(transform->prescale) == sizeof(spatial_prescale));
			memcpy(transform->prescale, spatial_prescale, sizeof(spatial_prescale));
			}
			break;

	/*	case TRANSFORM_TYPE_FIELD:
			assert(sizeof(transform->prescale) == sizeof(field_prescale));
			memcpy(transform->prescale, field_prescale, sizeof(field_prescale));
			break;
	*/
		case TRANSFORM_TYPE_FIELDPLUS:
			{//frm0, frm1, temp diff, temnp high, spatial, spatial, 0 0
			int fieldplus_prescale[] = {0, 0, 0, 2,  2, 2, 0, 0};
			assert(sizeof(transform->prescale) == sizeof(fieldplus_prescale));
			memcpy(transform->prescale, fieldplus_prescale, sizeof(fieldplus_prescale));
			}
			break;

		default:
			assert(0);
			memset(transform->prescale, 0, sizeof(transform->prescale));
			break;
		}
	}
}


void SetTransformPrescale(TRANSFORM *transform, int transform_type, int input_precision)
{
	if (input_precision == CODEC_PRECISION_8BIT) {
		memset(transform->prescale, 0, sizeof(transform->prescale));
		return;
	}

	if(input_precision == CODEC_PRECISION_10BIT)
	{
		switch (transform_type)
		{
		case TRANSFORM_TYPE_SPATIAL:
			{
				int spatial_prescale[]   = {0, 2, 0, 0,  0, 0, 0, 0};
										//frame, spatial, spatial, ...
				memcpy(transform->prescale, spatial_prescale, sizeof(spatial_prescale));
			}
			break;

	/*	case TRANSFORM_TYPE_FIELD:
			{
				int field_prescale[]     = {0, 0, 0, 2,  2, 0, 0, 0};
				memcpy(transform->prescale, field_prescale, sizeof(field_prescale));
			}
			break;
	*/
		case TRANSFORM_TYPE_FIELDPLUS:
			{
				int fieldplus_prescale[] = {0, 0, 0, 0,  2, 0, 0, 0};
										//frm0, frm1, temp diff, temp high, spatial, spatial, 0 0
				memcpy(transform->prescale, fieldplus_prescale, sizeof(fieldplus_prescale));
			}
			break;

		default:
			assert(0);
			memset(transform->prescale, 0, sizeof(transform->prescale));
			break;
		}
	}
	else if(input_precision == CODEC_PRECISION_12BIT)
	{
		switch (transform_type)
		{
		case TRANSFORM_TYPE_SPATIAL:
			{
				int spatial_prescale[]   = {0, 2, 2, 0,  0, 0, 0, 0};
										//frame, spatial, spatial, ...
				memcpy(transform->prescale, spatial_prescale, sizeof(spatial_prescale));
			}
			break;

	/*	case TRANSFORM_TYPE_FIELD:
			assert(sizeof(transform->prescale) == sizeof(field_prescale));
			memcpy(transform->prescale, field_prescale, sizeof(field_prescale));
			break;
	*/
		case TRANSFORM_TYPE_FIELDPLUS:
			{//frm0, frm1, temp diff, temnp high, spatial, spatial, 0 0
				int fieldplus_prescale[] = {0, 0, 0, 2,  2, 2, 0, 0};
				//int fieldplus_prescale[] = {0, 0, 0, 2,  2, 2, 0, 0};
										//frm0, frm1, temp diff, temp high, spatial, spatial, 0 0
				memcpy(transform->prescale, fieldplus_prescale, sizeof(fieldplus_prescale));
			}
			break;

		default:
			assert(0);
			memset(transform->prescale, 0, sizeof(transform->prescale));
			break;
		}
	}
}

bool TestTransformPrescaleMatch(TRANSFORM *transform, int transform_type, int input_precision)
{
	int i,tot;
	if (input_precision == CODEC_PRECISION_8BIT)
	{
		tot = 0;
		for(i=0;i<TRANSFORM_MAX_WAVELETS;i++)
			tot += transform->prescale[i];

		if(tot != 0)
			return false;
		else
			return true;
	}

	//assert(input_precision == CODEC_PRECISION_10BIT);

	switch (transform_type)
	{
	case TRANSFORM_TYPE_SPATIAL:
		{
			tot = 0;
			for(i=0;i<TRANSFORM_MAX_WAVELETS;i++)
				tot += abs(transform->prescale[i] - spatial_prescale[i]);

			if(tot != 0)
				return false;
			else
				return true;
		}
		break;

	case TRANSFORM_TYPE_FIELDPLUS:
		{
			tot = 0;
			for(i=0;i<TRANSFORM_MAX_WAVELETS;i++)
				tot += abs(transform->prescale[i] - fieldplus_prescale[i]);

			if(tot != 0)
				return false;
			else
				return true;
		}
		break;
	}


	return false;
}


#if _PACK_RUNS_IN_BAND_16S

// Pack all the zero runs into the same band data.
// returns the number of sample added to the band.
int PackRuns16s(PIXEL *input, int width)
{
	int index = 0,count = 0,outcount=0;
	PIXEL *rowptr = input;

	while (index < width)
	{
		for (; index < width; index++) {
			if (rowptr[index] == 0) count++;
			else break;
		}

		// Need to output a value?
		if (index < width) {
			PIXEL value = rowptr[index];

			// Need to output a run of zeros before this value?
			if (count > 0)
			{
				rowptr[outcount++] = count << 1;  // if a zero run, leave the LSB as '0'
				count = 0;
			}

			rowptr[outcount++] = (value << 1) | 1; // if a value, set the LSB to '1'
			index++;
		}
		else
		{
			// output the run at the end of the line.
			rowptr[outcount++] = count << 1;
		}
	}

	//if the line has been optimized, terminate with zero.
	if(outcount < width)
	{
		rowptr[outcount++] = 0; // terminate
	}

	return outcount;
}
#endif


#if 0
// Compute the horizontal wavelet transform
void TransformForwardHorizontal(IMAGE *input, int band,
								IMAGE *lowpass, int lowpass_band,
								IMAGE *highpass, int highpass_band)
{
//	Ipp32s arrayLowPassKernel[2] = {1, 1};
	int nLowPassKernel = 2;
	int iLowPassAnchor = 1;
	int iLowPassDivisor = 1;
	int nLowPassArea = 2;

	// Wavelet high-pass filter
//	Ipp32s arrayHighPassKernel[6] = {1, 1, -8, 8, -1, -1};
	int nHighPassKernel = 6;
	int iHighPassAnchor = 5;
	int iHighPassDivisor = 8;

	// Image processing dimensions
	ROI roi = {input->width, input->height};
	ROI lowpass_roi = {lowpass->width, lowpass->height};
	ROI highpass_roi = {highpass->width, highpass->height};

	PIXEL *input_address;
	PIXEL *lowpass_address;
	PIXEL *highpass_address;

	int input_pitch;
	int lowpass_pitch;
	int highpass_pitch;
	int buffer_pitch;
	int input_scale;

#if DEBUG
	SUBIMAGE border = SUBIMAGE_UPPER_LEFT(8, 8);
#endif

	START(tk_horizontal);

	// Check that a valid input image has been provided
	assert(input != NULL);
	assert(input->type == IMAGE_TYPE_GRAY || input->type == IMAGE_TYPE_WAVELET);

	// Get the input image address and check that it is valid
	input_address = input->band[band];
	assert(input_address != NULL);

	// Get the scale of the input signal
	input_scale = input->scale[band];

	// Check that the lowpass image has the correct dimensions
	assert(lowpass != NULL);
	assert(lowpass->width == input->width/2);
	assert(lowpass->height == input->height);

	// Get the address for the lowpass transform
	lowpass_address = lowpass->band[lowpass_band];
	assert(lowpass_address != NULL);

	// Check that the highpass image has the correct dimensions
	assert(highpass != NULL);
	assert(highpass->width == input->width/2);
	assert(highpass->height == input->height);

	// Get the address for the highpass transform
	highpass_address = highpass->band[highpass_band];
	assert(highpass_address != NULL);

#if DEBUG
	if (debugfile && debug['t']) {
		DumpImage("Input", input, &border, debugfile);
	}
#endif

	input_pitch = input->pitch;
	lowpass_pitch = lowpass->pitch;
	highpass_pitch = highpass->pitch;
	buffer_pitch = roi.width * sizeof(PIXEL);

	// Apply the low pass filter to the input image
	FilterLowpassHorizontal(input_address, input_pitch, lowpass_address, lowpass_pitch, roi, 0);

	// Apply the high pass filter to the input image
	FilterHighpassHorizontal(input_address, input_pitch, highpass_address, highpass_pitch,
							 roi, input_scale, 0);

#if 0
	// Calculate the scale factors for the output bands
	lowpass->scale[lowpass_band] = nLowPassArea * input->scale[band];
	highpass->scale[highpass_band] = input->scale[band];
#endif

	STOP(tk_horizontal);
}
#endif


#if 0

// Invert the horizontal wavelet
void TransformInverseHorizontal(IMAGE *input, int lowpass_band, int highpass_band,
								IMAGE *output, int output_band, bool fastmode)
{
	// Even reconstruction filter
//	Ipp32s even_kernel[] = {-1, 8, 1};
//	int even_length = sizeof(even_kernel)/sizeof(even_kernel[0]);
//	int even_anchor = even_length - 1;
	int even_divisor = 8;

	// Odd reconstruction filter
//	Ipp32s odd_kernel[] = {1, 8, -1};
//	int odd_length = sizeof(odd_kernel)/sizeof(odd_kernel[0]);
//	int odd_anchor = odd_length - 1;
	int odd_divisor = 8;

	// Processing dimensions
	int input_width = input->width;
	int input_height = input->height;
	ROI roi = {input_width, input_height};

	int input_pitch = input->pitch;
	int lowpass_border = 0;		//input->lowpass_border;
	int highpass_border = 0;	//input->highpass_border;
	int inverse_offset = 0;
	int output_pitch = output->pitch;

	PIXEL *input_lowpass;
	PIXEL *input_highpass;
	PIXEL *output_lowpass;

	//START(tk_horizontal);

	// Check that the output array is large enough
	assert(output->width >= 2 * input->width);
	assert(output->height >= input->height);

	// Check for valid bands
	assert(0 <= lowpass_band  && lowpass_band < IMAGE_NUM_BANDS);
	assert(0 <= highpass_band && highpass_band < IMAGE_NUM_BANDS);
	assert(0 <= output_band   && output_band < IMAGE_NUM_BANDS);

	input_lowpass = input->band[lowpass_band];
	input_highpass = input->band[highpass_band];
	output_lowpass = output->band[output_band];

	roi.width = input_width;
	roi.height = input_height;

	InvertHorizontal16s(input_lowpass, input_pitch, input_highpass, input_pitch,
						output_lowpass, output_pitch, roi, fastmode);

#if (0 && DEBUG)
	if (debugfile && debug['k']) {
		DumpImage("Horizontal Interleaved", output, &border, debugfile);
	}
#endif

	//STOP(tk_horizontal);
}
#endif


#if 0

// Compute the vertical wavelet transform
void TransformForwardVertical(IMAGE *input, int band,
							  IMAGE *lowpass, int lowpass_band,
							  IMAGE *highpass, int highpass_band)
{
//	Ipp32s arrayLowPassKernel[2] = {1, 1};
	int nLowPassKernel = 2;
	int iLowPassAnchor = 1;
	int iLowPassDivisor = 1;
	int nLowPassArea = 2;

	// Wavelet high-pass filter
//	Ipp32s arrayHighPassKernel[6] = {1, 1, -8, 8, -1, -1};
	int nHighPassKernel = 6;
	int iHighPassAnchor = 5;
	int iHighPassDivisor = 8;

	// Image processing dimensions
	ROI roi = {input->width, input->height};
	ROI lowpass_roi = {lowpass->width, lowpass->height};
	ROI highpass_roi = {highpass->width, highpass->height};

	PIXEL *input_address;
	PIXEL *lowpass_address;
	PIXEL *highpass_address;

	int input_pitch;
	int lowpass_pitch;
	int highpass_pitch;
	int buffer_pitch;

#if DEBUG
	SUBIMAGE border = SUBIMAGE_UPPER_LEFT(8, 8);
#endif

	START(tk_vertical);

	// Check that a valid input image has been provided
	assert(input != NULL);
	assert(input->type == IMAGE_TYPE_GRAY || input->type == IMAGE_TYPE_WAVELET);

	// Get the input image address and check that it is valid
	input_address = input->band[band];
	assert(input_address != NULL);

	// Check that the lowpass image has the correct dimensions
	assert(lowpass != NULL);
	assert(lowpass->width == input->width);
	assert(lowpass->height == input->height/2);

	// Get the address for the lowpass transform
	lowpass_address = lowpass->band[lowpass_band];
	assert(lowpass_address != NULL);

	// Check that the highpass image has the correct dimensions
	assert(highpass != NULL);
	assert(highpass->width == input->width);
	assert(highpass->height == input->height/2);

	// Get the address for the highpass transform
	highpass_address = highpass->band[highpass_band];
	assert(highpass_address != NULL);

#if DEBUG
	if (debugfile && debug['t']) {
		DumpImage("Input", input, &border, debugfile);
	}
#endif

	// Reduce the width by the lowpass filter border
	input_pitch = input->pitch;
	lowpass_pitch = lowpass->pitch;
	highpass_pitch = highpass->pitch;
	buffer_pitch = roi.width * sizeof(PIXEL);

	// Apply the vertical lowpass filter
	FilterLowpassVertical(input_address, input_pitch, lowpass_address, lowpass_pitch, roi);

	// Apply the vertical highpass filter
	FilterHighpassVertical(input_address, input_pitch, highpass_address, highpass_pitch, roi);

#if 0
	// Calculate the scale factors for the output bands
	lowpass->scale[lowpass_band] = nLowPassArea * input->scale[band];
	highpass->scale[highpass_band] = input->scale[band];
#endif

	STOP(tk_vertical);
}
#endif

#if 0
void TransformInverseVertical(IMAGE *input, int lowpass_band, int highpass_band,
							  IMAGE *output, int output_band)
{
	// Even reconstruction filter
//	Ipp32s even_kernel[] = {-1, 8, 1};
//	int even_length = sizeof(even_kernel)/sizeof(even_kernel[0]);
//	int even_anchor = even_length - 1;
	int even_divisor = 8;

	// Odd reconstruction filter
//	Ipp32s odd_kernel[] = {1, 8, -1};
//	int odd_length = sizeof(odd_kernel)/sizeof(odd_kernel[0]);
//	int odd_anchor = odd_length - 1;
	int odd_divisor = 8;

	// Processing dimensions
	int input_width = input->width;
	int input_height = input->height;
	ROI roi = {input_width, input_height};

	//PIXEL *input_block, *output_block;
	int input_pitch = input->pitch;
	int lowpass_border = 0;		//input->lowpass_border;
	int highpass_border = 0;	//input->highpass_border;
	int inverse_offset = 0;
	int output_pitch = output->pitch;

	PIXEL *input_lowpass;
	PIXEL *input_highpass;
	PIXEL *output_lowpass;

	//START(tk_vertical);

	// Check that the output array is large enough
	assert(output->width >= 2 * input->width);
	assert(output->height >= input->height);

	// Check for valid bands
	assert(0 <= lowpass_band  && lowpass_band < IMAGE_NUM_BANDS);
	assert(0 <= highpass_band && highpass_band < IMAGE_NUM_BANDS);
	assert(0 <= output_band   && output_band < IMAGE_NUM_BANDS);

	// Optimized version
	input_lowpass = input->band[lowpass_band];
	input_highpass = input->band[highpass_band];
	output_lowpass = output->band[output_band];

	roi.width = input_width;
	roi.height = input_height;

	InvertVertical16s(input_lowpass, input_pitch, input_highpass, input_pitch,
					  output_lowpass, output_pitch, roi /* lowpass_border, highpass_border */);

	//STOP(tk_vertical);
}
#endif

// Compute the two point (sum and difference) wavelet transform between two images
void TransformForwardTemporal(IMAGE *input1, int band1,
							  IMAGE *input2, int band2,
							  IMAGE *lowpass_image, int lowpass_band,
							  IMAGE *highpass_image, int highpass_band)
{
	PIXEL *field1 = input1->band[band1];
	PIXEL *field2 = input2->band[band2];
	PIXEL *lowpass = lowpass_image->band[lowpass_band];
	PIXEL *highpass = highpass_image->band[highpass_band];
	int pitch1 = input1->pitch;
	int pitch2 = input2->pitch;
	int lowpass_pitch = lowpass_image->pitch;
	int highpass_pitch = highpass_image->pitch;
	ROI roi = {input1->width, input1->height};
	//int scale1, scale2;
	//int scale = 0;
	int k;

	// This code only works for short integer pixels
	assert(sizeof(PIXEL) == sizeof(PIXEL16S));

	// Inputs should be the same height and width
	assert(input1->width == input2->width);
	assert(input1->height == input2->height);

	START(tk_temporal);

	// Apply the lowpass and highpass temporal filters
	FilterTemporal(field1, pitch1, field2, pitch2,
				   lowpass, lowpass_pitch, highpass, highpass_pitch, roi);

	// Set the lowpass and highpass coefficient pixel types
	lowpass_image->pixel_type[lowpass_band] = PIXEL_TYPE_16S;
	highpass_image->pixel_type[highpass_band] = PIXEL_TYPE_16S;

	for (k = 0; k < lowpass_image->num_bands; k++)
		lowpass_image->quantization[k] = 1;

	for (k = 0; k < highpass_image->num_bands; k++)
		highpass_image->quantization[k] = 1;

	STOP(tk_temporal);
}

void TransformInverseTemporal(IMAGE *temporal, IMAGE *frame0, IMAGE *frame1)
{
	ROI roi = {temporal->width, temporal->height};
	//int scale = 1;

	PIXEL *lowpass = temporal->band[0];			// Temporal sum
	PIXEL *highpass = temporal->band[1];		// Temporal difference
	PIXEL *even = frame0->band[0];				// First frame is the even field
	PIXEL *odd = frame1->band[0];				// Second frame is the odd field

	int lowpass_pitch = temporal->pitch;
	int highpass_pitch = temporal->pitch;
	int even_pitch = frame0->pitch;
	int odd_pitch = frame1->pitch;
	int row;
	//int column;

	// Convert pitch to units of pixels
	lowpass_pitch /= sizeof(PIXEL);
	highpass_pitch /= sizeof(PIXEL);
	even_pitch /= sizeof(PIXEL);
	odd_pitch /= sizeof(PIXEL);

	// Processing loops may require an integral number of 8 word blocks
	//assert((roi.width % 8) == 0);

	// Process each pair of lowpass and highpass rows
	for (row = 0; row < roi.height; row++)
	{
		int column = 0;

#if (1 && XMMOPT)

		__m128i *low_ptr = (__m128i *)lowpass;
		__m128i *high_ptr = (__m128i *)highpass;
		__m128i *even_ptr = (__m128i *)even;
		__m128i *odd_ptr = (__m128i *)odd;

		int column_step = 8;
		int post_column = roi.width - (roi.width % column_step);

		for (; column < post_column; column += column_step)
		{
			__m128i low_epi16;
			__m128i high_epi16;
			__m128i even_epi16;
			__m128i odd_epi16;

			// Check that the pointers to the next groups of pixels are properly aligned
			assert(ISALIGNED16(low_ptr));
			assert(ISALIGNED16(high_ptr));

			// Get four lowpass and four highpass coefficients
			low_epi16 = _mm_load_si128(low_ptr++);
			high_epi16 = _mm_load_si128(high_ptr++);

			// Reconstruct the pixels in the frame0 row
			even_epi16 = _mm_subs_epi16(low_epi16, high_epi16);
			even_epi16 = _mm_srai_epi16(even_epi16, 1);
			_mm_store_si128(even_ptr++, even_epi16);

			// Reconstruct the pixels in the frame1 row
			odd_epi16 = _mm_adds_epi16(low_epi16, high_epi16);
			odd_epi16 = _mm_srai_epi16(odd_epi16, 1);
			_mm_store_si128(odd_ptr++, odd_epi16);
		}

		// Should have exited the loop at the post processing column
		assert(column == post_column);
#endif

		for (; column < roi.width; column++)
		{
			// Get the lowpass and highpass coefficients
			int low = lowpass[column];
			int high = highpass[column];

			// Reconstruct the pixels in the even and odd fields
			odd[column] = (low + high)/2;
			even[column] = (low - high)/2;
		}

		// Advance to the next input and output rows
		lowpass += lowpass_pitch;
		highpass += highpass_pitch;
		even += even_pitch;
		odd += odd_pitch;
	}

	//_mm_empty();	// Clear the mmx register state
}

void TransformInverseTemporalQuant(IMAGE *temporal, IMAGE *frame0, IMAGE *frame1,
								   PIXEL *buffer, size_t buffer_size, int precision)
{
	ROI roi = {temporal->width, temporal->height};

	PIXEL *lowpass = temporal->band[0];			// Temporal sum
	PIXEL *highpass = temporal->band[1];		// Temporal difference
	PIXEL *even = frame0->band[0];				// First frame is the even field
	PIXEL *odd = frame1->band[0];				// Second frame is the odd field

	int lowpass_pitch = temporal->pitch;
	int highpass_pitch = temporal->pitch;
	int even_pitch = frame0->pitch;
	int odd_pitch = frame1->pitch;
	//int row, column;

	int quantization;

	// Do the highpass bands use 8-bit coefficients?
	highpass_pitch = temporal->pitch;
	quantization = temporal->quantization[1];

 	InvertTemporalQuant16s(lowpass, temporal->quantization[0], lowpass_pitch,
						   highpass, quantization, highpass_pitch,
						   even, even_pitch, odd, odd_pitch, roi, buffer, buffer_size, precision);
}

// Apply the temporal transform to the even and odd fields of a single frame.
// This version uses in place computation so the frame data will be overwritten.
void TransformForwardInterlaced(IMAGE *frame)
{
	int frame_pitch = frame->pitch;
	PIXEL *even_field = frame->band[0];
	PIXEL *odd_field = even_field + frame_pitch/sizeof(PIXEL);
	int field_pitch = 2 * frame->pitch;
	//int scale = frame->scale[0];

	ROI roi = {frame->width, frame->height};

	START(tk_temporal);

	// Apply the temporal transform to the image fields (in place computation)
	FilterInterlaced(even_field, frame_pitch,
					 even_field, field_pitch,
					 odd_field, field_pitch, roi);

	STOP(tk_temporal);
}

// Invert the temporal wavelet transform that was applied to an interlaced frame
void TransformInverseInterlaced(IMAGE *lowpass, int lowpass_band,
								IMAGE *highpass, int highpass_band,
								IMAGE *frame, int output_band)
{
	ROI roi = {lowpass->width, lowpass->height};
	PIXEL *even_field = frame->band[output_band];
	PIXEL *odd_field = (PIXEL *)(((char *)frame->band[output_band]) + frame->pitch);
	int field_pitch = 2 * frame->pitch;

	// Invert the temporal transform and interlaced the output fields into the frame
	InvertInterlaced16s(lowpass->band[lowpass_band], lowpass->pitch,
						highpass->band[highpass_band], highpass->pitch,
						even_field, field_pitch, odd_field, field_pitch, roi);
}


// Compute the size of buffer used by the forward spatial transform
size_t ForwardSpatialBufferSize(int width)
{
	size_t buffer_size;

	// The output image is half as wide as the input image
	buffer_size = (width / 2) * sizeof(PIXEL);

	// Align each row of the buffer to the cache line size
	buffer_size = ALIGN(buffer_size, _CACHE_LINE_SIZE);

	// Need a maximum of eighteen rows of buffer space
	buffer_size *= 18;

	// Return the buffer size
	return buffer_size;
}


// New version that calls FilterSpatialQuant16s
#if _ALLOCATOR
IMAGE *TransformForwardSpatial(ALLOCATOR *allocator,
							   IMAGE *image, int band, IMAGE *wavelet, int level,
							   PIXEL *buffer, size_t size, int prescale,
							   int quantization[IMAGE_NUM_BANDS], int difference_LL)
#else
IMAGE *TransformForwardSpatial(IMAGE *image, int band, IMAGE *wavelet, int level,
							   PIXEL *buffer, size_t size, int prescale,
							   int quantization[IMAGE_NUM_BANDS], int difference_LL)
#endif
{
	ROI roi = {image->width, image->height};
	//int divisor = (image->scale[band] < 128) ? 1 : 2;
	size_t buffer_size;
	bool isBufferLocal = false;

	// Allocate the output wavelet if necessary
	if (wavelet == NULL)
	{
#if _ALLOCATOR
		wavelet = CreateWaveletFromImageEx(allocator, image, level, WAVELET_TYPE_SPATIAL);
#else
		wavelet = CreateWaveletFromImageEx(image, level, WAVELET_TYPE_SPATIAL);
#endif
		if (wavelet == NULL) return NULL;
	}

	// The image processing buffer should have already been allocated
	assert(buffer != NULL);

	// Compute the size of buffer required for the forward wavelet transform
	buffer_size = ForwardSpatialBufferSize(image->width);

	// Allocate a buffer for the intermediate wavelet data if necessary
	if (buffer == NULL || size < buffer_size)
	{
		// The image processing buffer should be preallocated
		assert(0);

		// Allocate a buffer for image processing
#if _ALLOCATOR
		buffer = (PIXEL *)AllocAligned(allocator, buffer_size, 16);
#else
		buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(buffer_size, 16);
#endif
		if (buffer == NULL) return NULL;
		isBufferLocal = true;
	}

	START(tk_spatial);

	if (band == 1)
	{
		if(difference_LL)
		{
			// Any additional prescaling that is required is included in the quantization
			FilterSpatialQuantDifferenceLL16s(image->band[band], image->pitch, wavelet->band[0], wavelet->pitch,
				wavelet->band[1], wavelet->pitch, wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
				buffer, buffer_size, roi, quantization);
		}
		else
		{
			if (prescale == 2)
			{
				// Prescale the input to avoid overflow with 10-bit video sources
				FilterSpatialV210Quant16s(image->band[band], image->pitch, wavelet->band[0], wavelet->pitch,
					wavelet->band[1], wavelet->pitch, wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
					buffer, buffer_size, roi, quantization);
			}
			else
			{
				assert(prescale == 0);

				// Any additional prescaling that is required is included in the quantization
				FilterSpatialQuant16s(image->band[band], image->pitch, wavelet->band[0], wavelet->pitch,
					wavelet->band[1], wavelet->pitch, wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
					buffer, buffer_size, roi, quantization);
			}
		}
	}
	else
	{
#if (_LOWPASS_PRESCALE > 0)

#if (0 && DEBUG)
		if(band == 0) DumpPGM("LLenc",image,NULL);
#endif
		if (image->pixel_type[0] == PIXEL_TYPE_16S)
		{
#if 1

			if (prescale == 2)
			{
				// Prescale the input to avoid overflow with 10-bit video sources
				FilterSpatialV210Quant16s(image->band[band], image->pitch, wavelet->band[0], wavelet->pitch,
					wavelet->band[1], wavelet->pitch, wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
					buffer, buffer_size, roi, quantization);
			}
			else
			{
				// Check that no prescaling is being performed
				assert(prescale == 0);

				// Must prescale the lowpass coefficients without changing the lowpass band
				/*FilterSpatialPrescaleQuant16s*/	//DAN20061127 -- white point test found these routine
													// to produce the some output, so use the one that supports SSE2.
				FilterSpatialQuant16s(image->band[band], image->pitch, wavelet->band[0], wavelet->pitch,
					wavelet->band[1], wavelet->pitch, wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
					buffer, buffer_size, roi, quantization);
			}
#else
			FilterSpatialQuant16s(image->band[band], image->pitch, wavelet->band[0], wavelet->pitch,
				wavelet->band[1], wavelet->pitch, wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
				buffer, buffer_size, roi, quantization);
#endif
		}
		else
		{
			assert(0); // 8-bit path obselete

		}
#else
		// Any additional prescaling that is required is included in the quantization
		FilterSpatialQuant16s(image->band[band], image->pitch, wavelet->band[0], wavelet->pitch,
			wavelet->band[1], wavelet->pitch, wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
			buffer, buffer_size, roi, image->scale[band], prescale, 0, quantization);
#endif
	}

	// Free the intermediate results
	if (isBufferLocal)
	{
#if _ALLOCATOR
		FreeAligned(allocator, buffer);
#else
		MEMORY_ALIGNED_FREE(buffer);
#endif
	}

	// Set the output pixel type
	wavelet->pixel_type[0] = PIXEL_TYPE_16S;

#if _HIGHPASS_8S
	wavelet->pixel_type[1] = PIXEL_TYPE_8S;
	wavelet->pixel_type[2] = PIXEL_TYPE_8S;
	wavelet->pixel_type[3] = PIXEL_TYPE_8S;
#else
	wavelet->pixel_type[1] = PIXEL_TYPE_16S;
	wavelet->pixel_type[2] = PIXEL_TYPE_16S;
	wavelet->pixel_type[3] = PIXEL_TYPE_16S;
#endif

	// Record any quantization that was applied after filtering
	if (quantization != NULL) {
		int k;
		for (k = 0; k < IMAGE_NUM_BANDS; k++) {
			wavelet->quantization[k] = quantization[k];
		}
	}
	else {
		int k;
		for (k = 0; k < IMAGE_NUM_BANDS; k++) {
			wavelet->quantization[k] = 1;
		}
	}

	STOP(tk_spatial);

	// Return the output wavelet
	return wavelet;
}

#if _HIGHPASS_CODED

bool TransformForwardSpatialCoded(ENCODER *encoder, IMAGE *image, int band,
								  IMAGE *wavelet, int level,
								  PIXEL *buffer, size_t size, int prescale,
								  int quantization[IMAGE_NUM_BANDS])
{
	ROI roi = {image->width, image->height};
	int divisor = (image->scale[band] < 128) ? 1 : 2;
	size_t buffer_size;
	bool isBufferLocal = false;

#if 0
	// Allocate the output wavelet if necessary
	if (wavelet == NULL) {
		wavelet = CreateWaveletFromImageEx(image, level, WAVELET_TYPE_SPATIAL);
		if (wavelet == NULL) return false;
	}
#endif

	// Check that the output wavelet has been allocated
	assert(wavelet != NULL);

	// The image processing buffer should have already been allocated
	assert(buffer != NULL);

	// Compute the size of buffer required for the forward wavelet transform
	buffer_size = (image->width / 2) * sizeof(PIXEL);		// Output image is half as wide
	buffer_size = ALIGN(buffer_size, _CACHE_LINE_SIZE);		// Align each output row
	//buffer_size *= 12;									// Need twelve rows
	buffer_size *= 14;										// Need fourteen rows

	// Allocate a buffer for the intermediate wavelet data if necessary
	if (buffer == NULL || size < buffer_size)
	{
		// The image processing buffer should be preallocated
		assert(0);

		// Allocate a buffer for image processing
		buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(buffer_size, 16);
		if (buffer == NULL) return false;
		isBufferLocal = true;
	}

	START(tk_spatial);

	// Spatially filter the wavelet and encode the highpass bands after quantization
	FilterSpatialQuant16sToCoded(encoder,
								 image->band[band], image->pitch,
								 wavelet->band[0], wavelet->pitch,
								 wavelet->band[1], wavelet->pitch,
								 wavelet->band[2], wavelet->pitch,
								 wavelet->band[3], wavelet->pitch,
								 buffer, buffer_size, roi,
								 quantization, wavelet->coded_size);

	// Free the intermediate results
	if (isBufferLocal)
	{
#if _ALLOCATOR
		FreeAligned(allocator, buffer);
#else
		MEMORY_ALIGNED_FREE(buffer);
#endif
	}

	// Set the output pixel types
	wavelet->pixel_type[0] = PIXEL_TYPE_16S;
	wavelet->pixel_type[1] = PIXEL_TYPE_CODED;
	wavelet->pixel_type[2] = PIXEL_TYPE_CODED;
	wavelet->pixel_type[3] = PIXEL_TYPE_CODED;

	// Record any quantization that was applied after filtering
	if (quantization != NULL) {
		int k;
		for (k = 0; k < IMAGE_NUM_BANDS; k++)
			wavelet->quantization[k] = quantization[k];
	}
	else {
		int k;
		for (k = 0; k < IMAGE_NUM_BANDS; k++)
			wavelet->quantization[k] = 1;
	}

	STOP(tk_spatial);

	// Return indication that the wavelet has been computed
	return true;
}

#endif


#if 0

// Apply the forward wavelet transform (horizontal and vertical) with stacked bands
void TransformForwardWaveletStack(IMAGE *input, int band, IMAGE *output,
								  PIXEL *buffer, size_t size, int prescale,
								  int quantization[IMAGE_NUM_BANDS])
{
	// Wavelet transform lowpass filter
	//Ipp32s arrayLowPassKernel[2] = {1, 1};
	int nLowPassKernel = 2;
	int iLowPassAnchor = 1;
	//int iLowPassDivisor = (input->scale[0] < 128) ? 1 : 2;
	int iLowPassDivisor = (input->scale[band] < 128) ? 1 : 2;

	// Wavelet transform highpass filter
	//Ipp32s arrayHighPassKernel[6] = {1, 1, -8, 8, -1, -1};
	int nHighPassKernel = 6;
	int iHighPassAnchor = 5;
	int iHighPassDivisor = 8;

	int input_width = input->width;			// Width of input data
	int input_height = input->height;
	int output_width = input_width / 2;		// Width of output data
	int output_height = input_height / 2;	// Width of output data

	// Compute the allocated size and dimensions of the output bands
	int output_band_width = output->pitch / sizeof(PIXEL);
	int output_band_size = output->band[1] - output->band[0];
	int output_band_height = output_band_size / output_band_width;

	// Image processing dimensions
	ROI roi = {input_width, input_height};

	PIXEL *lowpass_buffer;			// Pointer into buffer for the lowpass results
	PIXEL *highpass_buffer;			// Pointer into buffer for the highpass results
	int buffer_pitch;				// Pitch of the buffer rows
	size_t buffer_size;				// Required size of the temporary buffer

	// Check that a valid input image has been provided
	assert(input != NULL);
	assert(input->type == IMAGE_TYPE_GRAY || input->type == IMAGE_TYPE_WAVELET);

	// Check that the output image has been configured as a wavelet image
	assert(output->type == IMAGE_TYPE_WAVELET);
	assert(output->band[0] != NULL);
	assert(output->band[1] != NULL);
	assert(output->band[2] != NULL);
	assert(output->band[3] != NULL);

	// The horizontal lowpass result will be stacked on the highpass result in memory

	roi.width = 2 * output->width;
	roi.height = 2 * output->height;

	// Check that the buffer is large enough
	buffer_size = 4 * output_band_height * output_band_width * sizeof(PIXEL);
	assert(buffer_size <= size);

	// Determine placement of intermediate results in the buffer
	lowpass_buffer = buffer;
	highpass_buffer = buffer + (2 * output_band_height * output_band_width);
	buffer_pitch = output->pitch;

	// Does the band contain 16-bit or 8-bit signed coefficients?
	if (input->pixel_type[band] == PIXEL_TYPE_16S)
	{
		// Perform horizontal filtering on the 16-bit lowpass band

		// It is okay for this to be a 16-bit highpass band

		// Check that this is the lowpass band
		//assert(band == 0);

		// Apply the low pass filter to the input image with downsampling
		FilterLowpassHorizontal(input->band[band], input->pitch, lowpass_buffer, buffer_pitch, roi, prescale);

		// Apply the high pass filter to the input image with downsampling
		FilterHighpassHorizontal(input->band[band], input->pitch,
								 highpass_buffer, buffer_pitch,
								 roi, input->scale[band], prescale);
	}
	else	// Perform horizontal filtering on the 8-bit highpass band
	{
		// Check that this is the highpass band
		assert(band == 1);

		// Apply the low pass filter to the input image with downsampling
		FilterLowpassHorizontal8s((PIXEL8S *)input->band[band], input->pitch, lowpass_buffer, buffer_pitch, roi, prescale);

		// Apply the high pass filter to the input image with downsampling
		FilterHighpassHorizontal8s((PIXEL8S *)input->band[band], input->pitch,
								   highpass_buffer, buffer_pitch,
								   roi, input->scale[band], prescale);
	}

	// The upper half of the buffer contains the downsampled horizontal low pass band
	// and the lower half contains the horizontal high pass band after downsampling

	// Set the processing dimension to the size of each half of the intermediate results
	roi.width = output->width;

	// Apply the low pass filter to the lowpass horizontal result
#if 1
	 // Is the input band the temporal highpass band?
	if (band == 1) {
		// Prescale the lowlow band during the transform
		FilterLowpassVerticalScaled(lowpass_buffer, buffer_pitch, output->band[0], output->pitch, roi);
	}
	else  {
		// Do not prescale the lowlow band during the transform
		FilterLowpassVertical(lowpass_buffer, buffer_pitch, output->band[0], output->pitch, roi);
	}
#else
	FilterLowpassVertical(lowpass_buffer, buffer_pitch, output->band[0], output->pitch, roi);
#endif

	// Apply the high pass filter to the lowpass horizontal result
	FilterHighpassVertical(lowpass_buffer, buffer_pitch, output->band[2], output->pitch, roi);

	// Apply the low pass filter to the highpass horizontal result
	FilterLowpassVertical(highpass_buffer, buffer_pitch, output->band[1], output->pitch, roi);

	// Apply the high pass filter to the highpass horizontal result
	FilterHighpassVertical(highpass_buffer, buffer_pitch, output->band[3], output->pitch, roi);

	// Record the divisors that were applied during filtering
	//output->divisor[0] = iLowPassDivisor * iLowPassDivisor;
	//output->divisor[1] = iLowPassDivisor;
	//output->divisor[2] = iLowPassDivisor;
	//output->divisor[3] = 1;

#if (0 && DEBUG && _WIN32)
	_CrtCheckMemory();
#endif
}
#endif


// Unpack YUV pixels in a progressive frame and perform the forward spatial transform
void TransformForwardSpatialYUV(uint8_t *input, int input_pitch, FRAME_INFO *frame,
								TRANSFORM *transform[], int frame_index, int num_channels,
								PIXEL *buffer, size_t buffer_size, int chroma_offset, int IFrame, 
								int precision, int limit_yuv, int conv_601_709)
{
	int frame_width = frame->width;
	//int frame_height = frame->height;
	//int divisor = (image->scale[band] < 128) ? 1 : 2;
	size_t size;
	//bool isBufferLocal = false;
	int channel;

	// Compute the size of buffer required for the forward wavelet transform
	size = (frame_width / 2) * sizeof(PIXEL);		// Output image is half as wide
	size = ALIGN(size, _CACHE_LINE_SIZE);			// Align each output row
	size *= 18;										// Need a maximum of 18 rows

	// The image processing buffer should have already been allocated
	assert(buffer != NULL);
	assert(buffer_size >= size);

#if (0 && DEBUG)
	if (band == 0) DumpPGM("LLenc", image, NULL);
#endif

#if TIMING
#if _THREADED_ENCODER
	DoThreadTiming(0);
	if (frame_index == 0)
	{
		START(tk_spatial1);
	}
	else
	{
		START(tk_spatial2);
	}
#else
	START(tk_progressive);
#endif
#endif

	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		PIXEL *lowlow_band = wavelet->band[0];
		PIXEL *lowhigh_band = wavelet->band[1];
		PIXEL *highlow_band = wavelet->band[2];
		PIXEL *highhigh_band = wavelet->band[3];

		// Compute the input dimensions from the output dimensions
		ROI roi = {2 * width, 2 * height};

		int quantization[IMAGE_NUM_BANDS];
		int k;

		for (k = 0; k < IMAGE_NUM_BANDS; k++) {
			quantization[k] = wavelet->quant[k];
		}

		// Check the input dimensions
		assert((channel == 0 && roi.width == frame_width) ||
				(channel > 0 && roi.width == frame_width/2));
		assert(roi.height == frame->height);

		//if (channel > 0)
		//if (1)
		//if (0)
		{
			// Apply the spatial transform to the pixels for this channel
			FilterSpatialYUVQuant16s(input, input_pitch,
									 lowlow_band, wavelet->pitch,
									 lowhigh_band, wavelet->pitch,
									 highlow_band, wavelet->pitch,
									 highhigh_band, wavelet->pitch,
									 buffer, buffer_size, roi, channel,
									 quantization, frame, precision, limit_yuv, conv_601_709);
		}
#if 0
		else
		{
			// Apply the spatial transform to vertical strips in the input
			int input_width = roi.width;
			int half_width = roi.width / 2;
			int quarter_width = half_width / 2;
			roi.width = half_width;

			// Apply the spatial transform to the left half of this channel
			FilterSpatialYUVQuant16s(input, input_pitch,
									 lowlow_band, wavelet->pitch,
									 lowhigh_band, wavelet->pitch,
									 highlow_band, wavelet->pitch,
									 highhigh_band, wavelet->pitch,
									 buffer, buffer_size, roi, channel,
									 quantization, frame, precision);

			// Apply the spatial transform to the right half of this channel
			FilterSpatialYUVQuant16s(input + input_width, input_pitch,
									 lowlow_band + quarter_width, wavelet->pitch,
									 lowhigh_band + quarter_width, wavelet->pitch,
									 highlow_band + quarter_width, wavelet->pitch,
									 highhigh_band + quarter_width, wavelet->pitch,
									 buffer, buffer_size, roi, channel,
									 quantization, frame, precision);
		}
#endif

		// Set the output pixel type
		wavelet->pixel_type[0] = PIXEL_TYPE_16S;
		wavelet->pixel_type[1] = PIXEL_TYPE_16S;
		wavelet->pixel_type[2] = PIXEL_TYPE_16S;
		wavelet->pixel_type[3] = PIXEL_TYPE_16S;

		// Record any quantization that was applied after filtering
		//if (quantization != NULL)
		{
			int k;
			for (k = 0; k < IMAGE_NUM_BANDS; k++)
				wavelet->quantization[k] = quantization[k];
		}
#if 0
		else {
			int k;
			for (k = 0; k < IMAGE_NUM_BANDS; k++)
				wavelet->quantization[k] = 1;
		}
#endif
	}

#if TIMING
#if _THREADED_ENCODER
	if (frame_index == 0)
	{
		STOP(tk_spatial1);
	}
	else
	{
		STOP(tk_spatial2);
	}
	DoThreadTiming(1);
#else
	STOP(tk_progressive);
#endif
#endif

}



// Unpack BYR3 pixels in a progressive frame and perform the forward spatial transform
void TransformForwardSpatialBYR3(uint8_t *input, int input_pitch, FRAME_INFO *frame,
								TRANSFORM *transform[], int frame_index, int num_channels,
								PIXEL *buffer, size_t buffer_size, int chroma_offset, int IFrame, int display_height)
{
	int frame_width = frame->width;
	int frame_height = frame->height;
	//int divisor = (image->scale[band] < 128) ? 1 : 2;
	size_t size;
	//bool isBufferLocal = false;
	int channel;

	int width = transform[0]->wavelet[frame_index]->width;

	// Compute the size of buffer required for the forward wavelet transform
	size = (frame_width / 2) * sizeof(PIXEL);		// Output image is half as wide
	size = ALIGN(size, _CACHE_LINE_SIZE);			// Align each output row
	size *= 15;										// Need fifteen rows

	// The image processing buffer should have already been allocated
	assert(buffer != NULL);
	assert(buffer_size >= size);
	assert(num_channels == 4);

	{
		PIXEL *lowlow_band[4];
		PIXEL *lowhigh_band[4];
		PIXEL *highlow_band[4];
		PIXEL *highhigh_band[4];

		//PIXEL *lowlow_buffer;
		PIXEL *lowhigh_buffer;
		PIXEL *highlow_buffer;
		PIXEL *highhigh_buffer;

		// Pointers to the rows of coefficients in the wavelet bands
		PIXEL *lowlow_row_ptr;
		PIXEL *lowhigh_row_ptr;
		PIXEL *highlow_row_ptr;
		PIXEL *highhigh_row_ptr;

		// Compute the input dimensions from the output dimensions
#if DEBUG
		int height = transform[0]->wavelet[frame_index]->height;
		ROI roi = {2 * width, 2 * height};
#endif

		int k;
		//int quantization[4][IMAGE_NUM_BANDS];
#if 1
		int last_row = frame_height;

		if (display_height == frame_height) {
			last_row -= 2;
		}
#else
		int last_row = display_height - 2;
#endif

		// Check the input dimensions
#if DEBUG
		assert(roi.width == frame_width);
		assert(roi.height == frame_height);
#endif

		for (channel = 0; channel < num_channels; channel++)
		{
			lowlow_band[channel] = transform[channel]->wavelet[frame_index]->band[0];
			lowhigh_band[channel] = transform[channel]->wavelet[frame_index]->band[1];
			highlow_band[channel] = transform[channel]->wavelet[frame_index]->band[2];
			highhigh_band[channel] = transform[channel]->wavelet[frame_index]->band[3];

			for(k=0; k<4; k++)
			{
				transform[channel]->wavelet[frame_index]->pixel_type[k] = PIXEL_TYPE_16S;
			}

			for (k = 0; k < IMAGE_NUM_BANDS; k++) {
				transform[channel]->wavelet[frame_index]->quantization[k] = transform[channel]->wavelet[frame_index]->quant[k];
			}
		}





		{
			uint8_t *rowptr = input;

			// Six rows of lowpass and highpass horizontal results
			PIXEL *lowpass[6];
			PIXEL *highpass[6];

			int output_width;
			size_t output_buffer_size;
			int output_buffer_width;
			PIXEL *bufptr;
			const int buffer_row_count = sizeof(lowpass)/sizeof(lowpass[0]);
			PIXEL *unpacking_buffer;
			size_t unpacking_buffer_size;
			int unpacking_buffer_width;
			int row, column;
			int k;


			// Convert pitch from bytes to pixels
			//lowlow_pitch /= sizeof(PIXEL);
			//lowhigh_pitch /= sizeof(PIXEL);
			//highlow_pitch /= sizeof(PIXEL);
			//highhigh_pitch /= sizeof(PIXEL);

				// Compute the width of each row of horizontal filter output
			output_width = width*4;// all four channels done at once

			// Compute the size of each row of horizontal filter output in bytes
			output_buffer_size = output_width * sizeof(PIXEL);
			// Round up the buffer size to an integer number of cache lines
			output_buffer_size = ALIGN(output_buffer_size, _CACHE_LINE_SIZE);
			// Compute the size of the buffer for unpacking the input rows
			unpacking_buffer_size = frame_width * sizeof(PIXEL);
			// Round up the buffer size to an integer number of cache lines
			unpacking_buffer_size = ALIGN(unpacking_buffer_size, _CACHE_LINE_SIZE);
			// The buffer must be large enough for fifteen rows plus the unpacking buffer
			assert(buffer_size >= ((15 * output_buffer_size) + unpacking_buffer_size));
			// Compute the allocated width of each wavelet band buffer
			output_buffer_width = (int)output_buffer_size / sizeof(PIXEL);
			// Compute the allocated width of the unpacking buffer
			unpacking_buffer_width = (int)unpacking_buffer_size / sizeof(PIXEL);
			// Start allocating intermediate buffers at the beginning of the supplied buffer
			bufptr = buffer;

			// Allocate space in the buffer for the horizontal filter results
			for (k = 0; k < buffer_row_count; k++) {
				lowpass[k] = bufptr;		bufptr += output_buffer_width;
				highpass[k] = bufptr;		bufptr += output_buffer_width;
			}

			// Allocate space in the buffer for the pre-quantized coefficients
			lowhigh_buffer = bufptr;		bufptr += output_buffer_width;
			highlow_buffer = bufptr;		bufptr += output_buffer_width;
			highhigh_buffer = bufptr;		bufptr += output_buffer_width;

			// Allocate space in the buffer for unpacking the input coefficients
			unpacking_buffer = bufptr;		bufptr += unpacking_buffer_width;

			// Compute the first six rows of horizontal filter output on all 4 channel
			for (k = 0; k < buffer_row_count; k++)
			{
				FilterHorizontalRowBYR3_16s((PIXEL *)rowptr, lowpass[k], highpass[k], frame_width);
				rowptr += input_pitch;
			}


			// Use border filters for the first row
			row = 0;

			for (channel = 0; channel < num_channels; channel++)
			{
				int offset = channel*width;

				lowlow_row_ptr = lowlow_band[channel] + (row*transform[channel]->wavelet[frame_index]->pitch>>2);
				lowhigh_row_ptr = lowhigh_band[channel] + (row*transform[channel]->wavelet[frame_index]->pitch>>2);
				highlow_row_ptr = highlow_band[channel] + (row*transform[channel]->wavelet[frame_index]->pitch>>2);
				highhigh_row_ptr = highhigh_band[channel] + (row*transform[channel]->wavelet[frame_index]->pitch>>2);


				for (column = 0; column < width; column++)
				{
					int32_t sum;

					// Apply the lowpass vertical filter to the lowpass horizontal results
					sum  = lowpass[0][offset];
					sum += lowpass[1][offset];
					lowlow_row_ptr[column] = SATURATE(sum);

					// Apply the highpass vertical filter to the lowpass horizontal results
					sum  =  5 * lowpass[0][offset];
					sum -= 11 * lowpass[1][offset];
					sum +=  4 * lowpass[2][offset];
					sum +=  4 * lowpass[3][offset];
					sum -=  1 * lowpass[4][offset];
					sum -=  1 * lowpass[5][offset];
					sum += ROUNDING(sum,8);
					sum = DivideByShift(sum, 3);
					highlow_buffer[column] = SATURATE(sum);

					// Apply the lowpass vertical filter to the highpass horizontal results
					sum  = highpass[0][offset];
					sum += highpass[1][offset];
					lowhigh_buffer[column] = SATURATE(sum);

					// Apply the highpass vertical filter to the highpass horizontal results
					sum  =  5 * highpass[0][offset];
					sum -= 11 * highpass[1][offset];
					sum +=  4 * highpass[2][offset];
					sum +=  4 * highpass[3][offset];
					sum -=  1 * highpass[4][offset];
					sum -=  1 * highpass[5][offset];
					sum += ROUNDING(sum,8);
					sum = DivideByShift(sum, 3);
					highhigh_buffer[column] = SATURATE(sum);

					offset++;
				}


			#if _PACK_RUNS_IN_BAND_16S
				// Quantize the current row of results for each 16-bit output band
				QuantizeRow16sTo16s(lowhigh_buffer, lowhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[1]);
				lowhigh_row_ptr += PackRuns16s(lowhigh_row_ptr, width);
				QuantizeRow16sTo16s(highlow_buffer, highlow_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[2]);
				highlow_row_ptr += PackRuns16s(highlow_row_ptr, width);
				QuantizeRow16sTo16s(highhigh_buffer, highhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[3]);
				highhigh_row_ptr += PackRuns16s(highhigh_row_ptr, width);
			#else
				// Quantize the first row of results for each 16-bit highpass band
				QuantizeRow16sTo16s(lowhigh_buffer, lowhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[1]);
				QuantizeRow16sTo16s(highlow_buffer, highlow_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[2]);
				QuantizeRow16sTo16s(highhigh_buffer, highhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[3]);
			#endif
			}





			row += 2;								// Advance the row being processed


			for (; row < last_row; row += 2)
			{
				for (channel = 0; channel < num_channels; channel++)
				{
					__m128i *lowlow_ptr;
					__m128i *highlow_ptr;
					__m128i *lowhigh_ptr;
					__m128i *highhigh_ptr;
					__m128i half_epi16;
					int offset = channel*width;

					int column_step = 8;
					int post_column = width - (width % column_step);

					lowlow_row_ptr = lowlow_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);
					lowhigh_row_ptr = lowhigh_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);
					highlow_row_ptr = highlow_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);
					highhigh_row_ptr = highhigh_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);

					lowlow_ptr = (__m128i *)lowlow_row_ptr;
					highlow_ptr = (__m128i *)highlow_buffer;
					lowhigh_ptr = (__m128i *)lowhigh_buffer;
					highhigh_ptr = (__m128i *)highhigh_buffer;

					half_epi16 = _mm_set1_epi16(4); //was 4

					// Start at the first column
					column = 0;
#if 1
					// Process a group of eight pixels at a time
					for (; column < post_column; column += column_step)
					{
						__m128i low_epi16;
						__m128i sum_epi16;
						__m128i sum8_epi16;
						__m128i quad_epi16;


						/***** Apply the vertical filters to the horizontal lowpass results *****/

						// Load the first row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&lowpass[0][offset]);

						// Initialize the highpass filter sum
						sum_epi16 = _mm_setzero_si128();

						// Multiply each pixel by the first filter coefficient and sum the result
						sum_epi16 = _mm_subs_epi16(sum_epi16, quad_epi16);

						// Load the second row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&lowpass[1][offset]);

						// Multiply each pixel by the second filter coefficient and sum the result
						sum_epi16 = _mm_subs_epi16(sum_epi16, quad_epi16);

						// Load the third row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&lowpass[2][offset]);

						// Initialize the lowpass sum
						low_epi16 = quad_epi16;

						// Multiply each pixel by the third filter coefficient and sum the result
						sum8_epi16 = _mm_setzero_si128();
						sum8_epi16 = _mm_adds_epi16(sum8_epi16, quad_epi16);

						// Load the fourth row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&lowpass[3][offset]);

						// Compute the four lowpass results
						low_epi16 = _mm_adds_epi16(low_epi16, quad_epi16);

						// Store the lowpass results
						_mm_store_si128(lowlow_ptr++, low_epi16);

						// Multiply each pixel by the fourth filter coefficient and sum the result
						sum8_epi16 = _mm_subs_epi16(sum8_epi16, quad_epi16);

						// Load the fifth row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&lowpass[4][offset]);

						// Multiply each pixel by the fifth filter coefficient and sum the result
						sum_epi16 = _mm_adds_epi16(sum_epi16, quad_epi16);

						// Load the sixth (last) row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&lowpass[5][offset]);

						// Multiply each pixel by the sixth filter coefficient and sum the result
						sum_epi16 = _mm_adds_epi16(sum_epi16, quad_epi16);

						sum_epi16 = _mm_adds_epi16(sum_epi16, half_epi16); // +7 rounding
						sum_epi16 = _mm_srai_epi16(sum_epi16, 3); // divide 8

						sum_epi16 = _mm_adds_epi16(sum_epi16, sum8_epi16);

						// Store the four highpass results
						_mm_store_si128(highlow_ptr++, sum_epi16);


						/***** Apply the vertical filters to the horizontal highpass results *****/

						sum_epi16 = _mm_setzero_si128();

						// Load the first row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&highpass[0][offset]);

						// Multiply each pixel by the first filter coefficient and sum the result
						sum_epi16 = _mm_subs_epi16(sum_epi16, quad_epi16);

						// Load the second row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&highpass[1][offset]);

						// Multiply each pixel by the second filter coefficient and sum the result
						sum_epi16 = _mm_subs_epi16(sum_epi16, quad_epi16);

						// Load the third row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&highpass[2][offset]);

						// Initialize the lowpass sum
						low_epi16 = quad_epi16;

						// Multiply each pixel by the third filter coefficient and sum the result
						sum8_epi16 = _mm_setzero_si128();
						sum8_epi16 = _mm_adds_epi16(sum8_epi16, quad_epi16);

						// Load the fourth row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&highpass[3][offset]);

						// Compute and store the four lowpass results
						low_epi16 = _mm_adds_epi16(low_epi16, quad_epi16);
						_mm_store_si128(lowhigh_ptr++, low_epi16);

						// Multiply each pixel by the fourth filter coefficient and sum the result
						sum8_epi16 = _mm_subs_epi16(sum8_epi16, quad_epi16);

						// Load the fifth row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&highpass[4][offset]);

						// Multiply each pixel by the fifth filter coefficient and sum the result
						sum_epi16 = _mm_adds_epi16(sum_epi16, quad_epi16);

						// Load the sixth (last) row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&highpass[5][offset]);

						// Multiply each pixel by the sixth filter coefficient and sum the result
						sum_epi16 = _mm_adds_epi16(sum_epi16, quad_epi16);

						sum_epi16 = _mm_adds_epi16(sum_epi16, half_epi16); // +7 rounding
						sum_epi16 = _mm_srai_epi16(sum_epi16, 3); // divide 8

						sum_epi16 = _mm_adds_epi16(sum_epi16, sum8_epi16);

						// Store the four highpass results
						_mm_store_si128(highhigh_ptr++, sum_epi16);


						offset+=column_step;
					}

					// Should have terminated the fast loop at the post processing column
					assert(column == post_column);
#endif

					// Process the remaining pixels to the end of the row
					for (; column < width; column++)
					{
						int32_t sum;

						// Apply the lowpass vertical filter to the lowpass horizontal results
						sum  = lowpass[2][offset];
						sum += lowpass[3][offset];
						lowlow_row_ptr[column] = SATURATE(sum);

						// Apply the highpass vertical filter to the lowpass horizontal results
						sum  = -1 * lowpass[0][offset];
						sum += -1 * lowpass[1][offset];
						sum +=  1 * lowpass[4][offset];
						sum +=  1 * lowpass[5][offset];
						sum +=	4;
						sum >>= 3;
						sum +=  1 * lowpass[2][offset];
						sum += -1 * lowpass[3][offset];
						highlow_buffer[column] = SATURATE(sum);

						// Apply the lowpass vertical filter to the highpass horizontal results
						sum  = highpass[2][offset];
						sum += highpass[3][offset];
						lowhigh_buffer[column] = SATURATE(sum);

						// Apply the highpass vertical filter to the highpass horizontal results
						sum  = -1 * highpass[0][offset];
						sum += -1 * highpass[1][offset];
						sum +=  1 * highpass[4][offset];
						sum +=  1 * highpass[5][offset];
						sum +=	4;
						sum >>= 3;
						sum +=  1 * highpass[2][offset];
						sum += -1 * highpass[3][offset];
						highhigh_buffer[column] = SATURATE(sum);

						offset++;
					}

			#if _PACK_RUNS_IN_BAND_16S
					// Quantize the current row of results for each 16-bit output band
					QuantizeRow16sTo16s(lowhigh_buffer, lowhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[1]);
					lowhigh_row_ptr += PackRuns16s(lowhigh_row_ptr, width);
					QuantizeRow16sTo16s(highlow_buffer, highlow_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[2]);
					highlow_row_ptr += PackRuns16s(highlow_row_ptr, width);
					QuantizeRow16sTo16s(highhigh_buffer, highhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[3]);
					highhigh_row_ptr += PackRuns16s(highhigh_row_ptr, width);
			#else
					// Quantize the current row of results for each 16-bit output band
					QuantizeRow16sTo16s(lowhigh_buffer, lowhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[1]);
					QuantizeRow16sTo16s(highlow_buffer, highlow_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[2]);
					QuantizeRow16sTo16s(highhigh_buffer, highhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[3]);
			#endif
				}



				if((int)(rowptr - input)/input_pitch < display_height)
				{
					// Rotate the horizontal filter results by two rows
					PIXEL *temp0 = lowpass[0];
					PIXEL *temp1 = lowpass[1];
					PIXEL *high0 = highpass[0];
					PIXEL *high1 = highpass[1];

					for (k = 0; k < buffer_row_count - 2; k++) {
						lowpass[k] = lowpass[k+2];
						highpass[k] = highpass[k+2];
					}

					lowpass[buffer_row_count - 2] = temp0;
					lowpass[buffer_row_count - 1] = temp1;
					highpass[buffer_row_count - 2] = high0;
					highpass[buffer_row_count - 1] = high1;

					// Compute the next two rows of horizontal filter results
					for (; k < buffer_row_count; k++) {

						FilterHorizontalRowBYR3_16s((PIXEL *)rowptr, lowpass[k], highpass[k], frame_width);
						rowptr += input_pitch;
					}
				}
				else // handle wavelets like 1080 bayer which has 540 display line and 544 wavelet lines
				{
			/*		for (k = 0; k < buffer_row_count - 2; k++) {
						lowpass[k] = lowpass[k+2];
						highpass[k] = highpass[k+2];
					}
					lowpass[5] = lowpass[4] = lowpass[3];
					highpass[5] = highpass[4] = highpass[3];*/
				}
			}

			// Should have left the loop at the last row
			if(row == display_height-2)
			{
			//	row++;

				if(row > display_height)
					row = display_height;

				for (channel = 0; channel < num_channels; channel++)
				{
					int offset = channel*width;

					lowlow_row_ptr = lowlow_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);
					lowhigh_row_ptr = lowhigh_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);
					highlow_row_ptr = highlow_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);
					highhigh_row_ptr = highhigh_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);


					// Use the border filters for the last row
					for (column = 0; column < width; column++)
					{
						int32_t sum;

						// Apply the lowpass vertical filter to the lowpass horizontal results
						sum  = lowpass[4][offset];
						sum += lowpass[5][offset];
						lowlow_row_ptr[column] = SATURATE(sum);

						// Apply the highpass vertical filter to the lowpass horizontal results
						sum  = 11 * lowpass[4][offset];
						sum -=  5 * lowpass[5][offset];
						sum -=  4 * lowpass[3][offset];
						sum -=  4 * lowpass[2][offset];
						sum +=  1 * lowpass[1][offset];
						sum +=  1 * lowpass[0][offset];
						sum +=  ROUNDING(sum,8);
						sum = DivideByShift(sum, 3);
						highlow_buffer[column] = SATURATE(sum);

						// Apply the lowpass vertical filter to the highpass horizontal results
						sum  = highpass[4][offset];
						sum += highpass[5][offset];
						lowhigh_buffer[column] = SATURATE(sum);

						// Apply the highpass vertical filter to the highpass horizontal results
						sum  = 11 * highpass[4][offset];
						sum -=  5 * highpass[5][offset];
						sum -=  4 * highpass[3][offset];
						sum -=  4 * highpass[2][offset];
						sum +=  1 * highpass[1][offset];
						sum +=  1 * highpass[0][offset];
						sum +=  ROUNDING(sum,8);
						sum = DivideByShift(sum, 3);
						highhigh_buffer[column] = SATURATE(sum);

						offset++;
					}

				#if _PACK_RUNS_IN_BAND_16S
					// Quantize the current row of results for each 16-bit output band
					QuantizeRow16sTo16s(lowhigh_buffer, lowhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[1]);
					lowhigh_row_ptr += PackRuns16s(lowhigh_row_ptr, width);
					QuantizeRow16sTo16s(highlow_buffer, highlow_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[2]);
					highlow_row_ptr += PackRuns16s(highlow_row_ptr, width);
					QuantizeRow16sTo16s(highhigh_buffer, highhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[3]);
					highhigh_row_ptr += PackRuns16s(highhigh_row_ptr, width);
				#else
					// Quantize the last row of results for each 16-bit output band
					QuantizeRow16sTo16s(lowhigh_buffer, lowhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[1]);
					QuantizeRow16sTo16s(highlow_buffer, highlow_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[2]);
					QuantizeRow16sTo16s(highhigh_buffer, highhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[3]);
				#endif

				}
			}
		}
	}



/*	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		PIXEL *lowlow_band = wavelet->band[0];
		PIXEL *lowhigh_band = wavelet->band[1];
		PIXEL *highlow_band = wavelet->band[2];
		PIXEL *highhigh_band = wavelet->band[3];

		// Compute the input dimensions from the output dimensions
		ROI roi = {2 * width, 2 * height};

		int quantization[IMAGE_NUM_BANDS];
		int k;

		for (k = 0; k < IMAGE_NUM_BANDS; k++) {
			quantization[k] = wavelet->quant[k];
		}

		// Check the input dimensions
		assert(roi.width == frame_width);
		assert(roi.height == frame_height);

		//if (channel > 0)
		//if (1)
		//if (0)

		{
			// Apply the spatial transform to the pixels for this channel
			FilterSpatialYUVQuant16s(input, input_pitch,
									 lowlow_band, wavelet->pitch,
									 lowhigh_band, wavelet->pitch,
									 highlow_band, wavelet->pitch,
									 highhigh_band, wavelet->pitch,
									 buffer, buffer_size, roi, channel,
									 quantization, frame, precision);
		}

		// Set the output pixel type
		wavelet->pixel_type[0] = PIXEL_TYPE_16S;
		wavelet->pixel_type[1] = PIXEL_TYPE_16S;
		wavelet->pixel_type[2] = PIXEL_TYPE_16S;
		wavelet->pixel_type[3] = PIXEL_TYPE_16S;

		// Record any quantization that was applied after filtering
		//if (quantization != NULL)
		{
			int k;
			for (k = 0; k < IMAGE_NUM_BANDS; k++)
				wavelet->quantization[k] = quantization[k];
		}
	}
  */
}


// Unpack RG30 pixels in a progressive frame and perform the forward spatial transform
// Blue << 20 | Green << 10 | Red
void TransformForwardSpatialRGB30(uint8_t *input, int input_pitch, FRAME_INFO *frame,
								TRANSFORM *transform[], int frame_index, int num_channels,
								PIXEL *buffer, size_t buffer_size, int chroma_offset, int IFrame,
								int display_height, int precision, int format)
{
	int frame_width = frame->width;
	int frame_height = frame->height;
	//int divisor = (image->scale[band] < 128) ? 1 : 2;
	size_t size;
	//bool isBufferLocal = false;
	int channel;

	int width = transform[0]->wavelet[frame_index]->width;

	// Compute the size of buffer required for the forward wavelet transform
	size = (frame_width / 2) * sizeof(PIXEL);		// Output image is half as wide
	size = ALIGN(size, _CACHE_LINE_SIZE);			// Align each output row
	size *= 15;										// Need fifteen rows

	// The image processing buffer should have already been allocated
	assert(buffer != NULL);
	assert(buffer_size >= size);
	assert(num_channels == 3);

	{
		PIXEL *lowlow_band[4];
		PIXEL *lowhigh_band[4];
		PIXEL *highlow_band[4];
		PIXEL *highhigh_band[4];

		//PIXEL *lowlow_buffer;
		PIXEL *lowhigh_buffer;
		PIXEL *highlow_buffer;
		PIXEL *highhigh_buffer;

		// Pointers to the rows of coefficients in the wavelet bands
		PIXEL *lowlow_row_ptr;
		PIXEL *lowhigh_row_ptr;
		PIXEL *highlow_row_ptr;
		PIXEL *highhigh_row_ptr;

		// Compute the input dimensions from the output dimensions
#if DEBUG
		int height = transform[0]->wavelet[frame_index]->height;
		ROI roi = {width * 2, height * 2};
#endif

		int k;
		int last_row = frame_height;

		if(display_height == frame_height)
			last_row -= 2;


		// Check the input dimensions
#if DEBUG
		assert(roi.width == frame_width);
		assert(roi.height == frame_height);
#endif

		for (channel = 0; channel < num_channels; channel++)
		{
			lowlow_band[channel] = transform[channel]->wavelet[frame_index]->band[0];
			lowhigh_band[channel] = transform[channel]->wavelet[frame_index]->band[1];
			highlow_band[channel] = transform[channel]->wavelet[frame_index]->band[2];
			highhigh_band[channel] = transform[channel]->wavelet[frame_index]->band[3];

			for(k=0; k<4; k++)
			{
				transform[channel]->wavelet[frame_index]->pixel_type[k] = PIXEL_TYPE_16S;
			}

			for (k = 0; k < IMAGE_NUM_BANDS; k++) {
				transform[channel]->wavelet[frame_index]->quantization[k] = transform[channel]->wavelet[frame_index]->quant[k];
			}
		}





		{
			uint8_t *rowptr = input;

			// Six rows of lowpass and highpass horizontal results
			PIXEL *lowpass[6];
			PIXEL *highpass[6];

			int output_width;
			size_t output_buffer_size;
			int output_buffer_width;
			PIXEL *bufptr;
			const int buffer_row_count = sizeof(lowpass)/sizeof(lowpass[0]);
			PIXEL *unpacking_buffer;
			size_t unpacking_buffer_size;
			int unpacking_buffer_width;
			int row, column;
			int k;

			// Convert pitch from bytes to pixels
			//lowlow_pitch /= sizeof(PIXEL);
			//lowhigh_pitch /= sizeof(PIXEL);
			//highlow_pitch /= sizeof(PIXEL);
			//highhigh_pitch /= sizeof(PIXEL);

				// Compute the width of each row of horizontal filter output
			output_width = frame_width*3;// all three channels done at once

			// Compute the size of each row of horizontal filter output in bytes
			output_buffer_size = output_width * sizeof(PIXEL);
			// Round up the buffer size to an integer number of cache lines
			output_buffer_size = ALIGN(output_buffer_size, _CACHE_LINE_SIZE);
			// Compute the size of the buffer for unpacking the input rows
			unpacking_buffer_size = frame_width * sizeof(PIXEL);
			// Round up the buffer size to an integer number of cache lines
			unpacking_buffer_size = ALIGN(unpacking_buffer_size, _CACHE_LINE_SIZE);
			// The buffer must be large enough for fifteen rows plus the unpacking buffer
			assert(buffer_size >= ((15 * output_buffer_size) + unpacking_buffer_size));
			// Compute the allocated width of each wavelet band buffer
			output_buffer_width = (int)output_buffer_size / sizeof(PIXEL);
			// Compute the allocated width of the unpacking buffer
			unpacking_buffer_width = (int)unpacking_buffer_size / sizeof(PIXEL);
			// Start allocating intermediate buffers at the beginning of the supplied buffer
			bufptr = buffer;

			// Allocate space in the buffer for the horizontal filter results
			for (k = 0; k < buffer_row_count; k++) {
				lowpass[k] = bufptr;		bufptr += output_buffer_width;
				highpass[k] = bufptr;		bufptr += output_buffer_width;
			}

			// Allocate space in the buffer for the pre-quantized coefficients
			lowhigh_buffer = bufptr;		bufptr += output_buffer_width;
			highlow_buffer = bufptr;		bufptr += output_buffer_width;
			highhigh_buffer = bufptr;		bufptr += output_buffer_width;

			// Allocate space in the buffer for unpacking the input coefficients
			unpacking_buffer = bufptr;		bufptr += unpacking_buffer_width;

			// Compute the first six rows of horizontal filter output on all 4 channel
			for (k = 0; k < buffer_row_count; k++)
			{
				FilterHorizontalRowRGB30_16s((PIXEL *)rowptr, lowpass[k], highpass[k], frame_width, precision, format);
				rowptr += input_pitch;
			}


			// Use border filters for the first row
			row = 0;

			for (channel = 0; channel < num_channels; channel++)
			{
				int offset = channel*width;

				lowlow_row_ptr = lowlow_band[channel] + (row*transform[channel]->wavelet[frame_index]->pitch>>2);
				lowhigh_row_ptr = lowhigh_band[channel] + (row*transform[channel]->wavelet[frame_index]->pitch>>2);
				highlow_row_ptr = highlow_band[channel] + (row*transform[channel]->wavelet[frame_index]->pitch>>2);
				highhigh_row_ptr = highhigh_band[channel] + (row*transform[channel]->wavelet[frame_index]->pitch>>2);


				for (column = 0; column < width; column++)
				{
					int32_t sum;

					// Apply the lowpass vertical filter to the lowpass horizontal results
					sum  = lowpass[0][offset];
					sum += lowpass[1][offset];
					lowlow_row_ptr[column] = SATURATE(sum);

					// Apply the highpass vertical filter to the lowpass horizontal results
					sum  =  5 * lowpass[0][offset];
					sum -= 11 * lowpass[1][offset];
					sum +=  4 * lowpass[2][offset];
					sum +=  4 * lowpass[3][offset];
					sum -=  1 * lowpass[4][offset];
					sum -=  1 * lowpass[5][offset];
					sum += ROUNDING(sum,8);
					sum = DivideByShift(sum, 3);
					highlow_buffer[column] = SATURATE(sum);

					// Apply the lowpass vertical filter to the highpass horizontal results
					sum  = highpass[0][offset];
					sum += highpass[1][offset];
					lowhigh_buffer[column] = SATURATE(sum);

					// Apply the highpass vertical filter to the highpass horizontal results
					sum  =  5 * highpass[0][offset];
					sum -= 11 * highpass[1][offset];
					sum +=  4 * highpass[2][offset];
					sum +=  4 * highpass[3][offset];
					sum -=  1 * highpass[4][offset];
					sum -=  1 * highpass[5][offset];
					sum += ROUNDING(sum,8);
					sum = DivideByShift(sum, 3);
					highhigh_buffer[column] = SATURATE(sum);

					offset++;
				}


			#if _PACK_RUNS_IN_BAND_16S
				// Quantize the current row of results for each 16-bit output band
				QuantizeRow16sTo16s(lowhigh_buffer, lowhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[1]);
				lowhigh_row_ptr += PackRuns16s(lowhigh_row_ptr, width);
				QuantizeRow16sTo16s(highlow_buffer, highlow_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[2]);
				highlow_row_ptr += PackRuns16s(highlow_row_ptr, width);
				QuantizeRow16sTo16s(highhigh_buffer, highhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[3]);
				highhigh_row_ptr += PackRuns16s(highhigh_row_ptr, width);
			#else
				// Quantize the first row of results for each 16-bit highpass band
				QuantizeRow16sTo16s(lowhigh_buffer, lowhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[1]);
				QuantizeRow16sTo16s(highlow_buffer, highlow_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[2]);
				QuantizeRow16sTo16s(highhigh_buffer, highhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[3]);
			#endif
			}





			row += 2;								// Advance the row being processed


			for (; row < last_row; row += 2)
			{
				for (channel = 0; channel < num_channels; channel++)
				{
					__m128i *lowlow_ptr;
					__m128i *highlow_ptr;
					__m128i *lowhigh_ptr;
					__m128i *highhigh_ptr;
					__m128i half_epi16;
					int offset = channel*width;

					int column_step = 8;
					int post_column = width - (width % column_step);

					lowlow_row_ptr = lowlow_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);
					lowhigh_row_ptr = lowhigh_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);
					highlow_row_ptr = highlow_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);
					highhigh_row_ptr = highhigh_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);

					lowlow_ptr = (__m128i *)lowlow_row_ptr;
					highlow_ptr = (__m128i *)highlow_buffer;
					lowhigh_ptr = (__m128i *)lowhigh_buffer;
					highhigh_ptr = (__m128i *)highhigh_buffer;

					half_epi16 = _mm_set1_epi16(4); //was 4

					// Start at the first column
					column = 0;
#if 1
					// Process a group of eight pixels at a time
					for (; column < post_column; column += column_step)
					{
						__m128i low_epi16;
						__m128i sum_epi16;
						__m128i sum8_epi16;
						__m128i quad_epi16;

						/***** Apply the vertical filters to the horizontal lowpass results *****/

						// Load the first row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&lowpass[0][offset]);

						// Initialize the highpass filter sum
						sum_epi16 = _mm_setzero_si128();

						// Multiply each pixel by the first filter coefficient and sum the result
						sum_epi16 = _mm_subs_epi16(sum_epi16, quad_epi16);

						// Load the second row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&lowpass[1][offset]);

						// Multiply each pixel by the second filter coefficient and sum the result
						sum_epi16 = _mm_subs_epi16(sum_epi16, quad_epi16);

						// Load the third row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&lowpass[2][offset]);

						// Initialize the lowpass sum
						low_epi16 = quad_epi16;

						// Multiply each pixel by the third filter coefficient and sum the result
						sum8_epi16 = _mm_setzero_si128();
						sum8_epi16 = _mm_adds_epi16(sum8_epi16, quad_epi16);

						// Load the fourth row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&lowpass[3][offset]);

						// Compute the four lowpass results
						low_epi16 = _mm_adds_epi16(low_epi16, quad_epi16);

						// Store the lowpass results
						_mm_store_si128(lowlow_ptr++, low_epi16);

						// Multiply each pixel by the fourth filter coefficient and sum the result
						sum8_epi16 = _mm_subs_epi16(sum8_epi16, quad_epi16);

						// Load the fifth row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&lowpass[4][offset]);

						// Multiply each pixel by the fifth filter coefficient and sum the result
						sum_epi16 = _mm_adds_epi16(sum_epi16, quad_epi16);

						// Load the sixth (last) row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&lowpass[5][offset]);

						// Multiply each pixel by the sixth filter coefficient and sum the result
						sum_epi16 = _mm_adds_epi16(sum_epi16, quad_epi16);

						sum_epi16 = _mm_adds_epi16(sum_epi16, half_epi16); // +7 rounding
						sum_epi16 = _mm_srai_epi16(sum_epi16, 3); // divide 8

						sum_epi16 = _mm_adds_epi16(sum_epi16, sum8_epi16);

						// Store the four highpass results
						_mm_store_si128(highlow_ptr++, sum_epi16);


						/***** Apply the vertical filters to the horizontal highpass results *****/

						sum_epi16 = _mm_setzero_si128();

						// Load the first row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&highpass[0][offset]);

						// Multiply each pixel by the first filter coefficient and sum the result
						sum_epi16 = _mm_subs_epi16(sum_epi16, quad_epi16);

						// Load the second row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&highpass[1][offset]);

						// Multiply each pixel by the second filter coefficient and sum the result
						sum_epi16 = _mm_subs_epi16(sum_epi16, quad_epi16);

						// Load the third row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&highpass[2][offset]);

						// Initialize the lowpass sum
						low_epi16 = quad_epi16;

						// Multiply each pixel by the third filter coefficient and sum the result
						sum8_epi16 = _mm_setzero_si128();
						sum8_epi16 = _mm_adds_epi16(sum8_epi16, quad_epi16);

						// Load the fourth row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&highpass[3][offset]);

						// Compute and store the four lowpass results
						low_epi16 = _mm_adds_epi16(low_epi16, quad_epi16);
						_mm_store_si128(lowhigh_ptr++, low_epi16);

						// Multiply each pixel by the fourth filter coefficient and sum the result
						sum8_epi16 = _mm_subs_epi16(sum8_epi16, quad_epi16);

						// Load the fifth row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&highpass[4][offset]);

						// Multiply each pixel by the fifth filter coefficient and sum the result
						sum_epi16 = _mm_adds_epi16(sum_epi16, quad_epi16);

						// Load the sixth (last) row of four pixels
						quad_epi16 = _mm_load_si128((__m128i *)&highpass[5][offset]);

						// Multiply each pixel by the sixth filter coefficient and sum the result
						sum_epi16 = _mm_adds_epi16(sum_epi16, quad_epi16);

						sum_epi16 = _mm_adds_epi16(sum_epi16, half_epi16); // +7 rounding
						sum_epi16 = _mm_srai_epi16(sum_epi16, 3); // divide 8

						sum_epi16 = _mm_adds_epi16(sum_epi16, sum8_epi16);

						// Store the four highpass results
						_mm_store_si128(highhigh_ptr++, sum_epi16);


						offset+=column_step;
					}

					// Should have terminated the fast loop at the post processing column
					assert(column == post_column);
#endif

					// Process the remaining pixels to the end of the row
					for (; column < width; column++)
					{
						int32_t sum;

						// Apply the lowpass vertical filter to the lowpass horizontal results
						sum  = lowpass[2][offset];
						sum += lowpass[3][offset];
						lowlow_row_ptr[column] = SATURATE(sum);

						// Apply the highpass vertical filter to the lowpass horizontal results
						sum  = -1 * lowpass[0][offset];
						sum += -1 * lowpass[1][offset];
						sum +=  1 * lowpass[4][offset];
						sum +=  1 * lowpass[5][offset];
						sum +=	4;
						sum >>= 3;
						sum +=  1 * lowpass[2][offset];
						sum += -1 * lowpass[3][offset];
						highlow_buffer[column] = SATURATE(sum);

						// Apply the lowpass vertical filter to the highpass horizontal results
						sum  = highpass[2][offset];
						sum += highpass[3][offset];
						lowhigh_buffer[column] = SATURATE(sum);

						// Apply the highpass vertical filter to the highpass horizontal results
						sum  = -1 * highpass[0][offset];
						sum += -1 * highpass[1][offset];
						sum +=  1 * highpass[4][offset];
						sum +=  1 * highpass[5][offset];
						sum +=	4;
						sum >>= 3;
						sum +=  1 * highpass[2][offset];
						sum += -1 * highpass[3][offset];
						highhigh_buffer[column] = SATURATE(sum);

						offset++;
					}

			#if _PACK_RUNS_IN_BAND_16S
					// Quantize the current row of results for each 16-bit output band
					QuantizeRow16sTo16s(lowhigh_buffer, lowhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[1]);
					lowhigh_row_ptr += PackRuns16s(lowhigh_row_ptr, width);
					QuantizeRow16sTo16s(highlow_buffer, highlow_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[2]);
					highlow_row_ptr += PackRuns16s(highlow_row_ptr, width);
					QuantizeRow16sTo16s(highhigh_buffer, highhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[3]);
					highhigh_row_ptr += PackRuns16s(highhigh_row_ptr, width);
			#else
					// Quantize the current row of results for each 16-bit output band
					QuantizeRow16sTo16s(lowhigh_buffer, lowhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[1]);
					QuantizeRow16sTo16s(highlow_buffer, highlow_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[2]);
					QuantizeRow16sTo16s(highhigh_buffer, highhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[3]);
			#endif
				}



				if((int)(rowptr - input)/input_pitch < display_height)
				{
					// Rotate the horizontal filter results by two rows
					PIXEL *temp0 = lowpass[0];
					PIXEL *temp1 = lowpass[1];
					PIXEL *high0 = highpass[0];
					PIXEL *high1 = highpass[1];

					for (k = 0; k < buffer_row_count - 2; k++) {
						lowpass[k] = lowpass[k+2];
						highpass[k] = highpass[k+2];
					}

					lowpass[buffer_row_count - 2] = temp0;
					lowpass[buffer_row_count - 1] = temp1;
					highpass[buffer_row_count - 2] = high0;
					highpass[buffer_row_count - 1] = high1;

					// Compute the next two rows of horizontal filter results
					for (; k < buffer_row_count; k++)
					{
						FilterHorizontalRowRGB30_16s((PIXEL *)rowptr, lowpass[k], highpass[k], frame_width, precision, format);
						rowptr += input_pitch;
					}
				}
				else // handle wavelets like 1080 bayer which has 540 display line and 544 wavelet lines
				{
			/*		for (k = 0; k < buffer_row_count - 2; k++) {
						lowpass[k] = lowpass[k+2];
						highpass[k] = highpass[k+2];
					}
					lowpass[5] = lowpass[4] = lowpass[3];
					highpass[5] = highpass[4] = highpass[3];*/
				}
			}

			// Should have left the loop at the last row
			if(row == display_height-2)
			{
			//	row++;

				if(row > display_height)
					row = display_height;

				for (channel = 0; channel < num_channels; channel++)
				{
					int offset = channel*width;

					lowlow_row_ptr = lowlow_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);
					lowhigh_row_ptr = lowhigh_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);
					highlow_row_ptr = highlow_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);
					highhigh_row_ptr = highhigh_band[channel] + row*(transform[channel]->wavelet[frame_index]->pitch>>2);


					// Use the border filters for the last row
					for (column = 0; column < width; column++)
					{
						int32_t sum;

						// Apply the lowpass vertical filter to the lowpass horizontal results
						sum  = lowpass[4][offset];
						sum += lowpass[5][offset];
						lowlow_row_ptr[column] = SATURATE(sum);

						// Apply the highpass vertical filter to the lowpass horizontal results
						sum  = 11 * lowpass[4][offset];
						sum -=  5 * lowpass[5][offset];
						sum -=  4 * lowpass[3][offset];
						sum -=  4 * lowpass[2][offset];
						sum +=  1 * lowpass[1][offset];
						sum +=  1 * lowpass[0][offset];
						sum +=  ROUNDING(sum,8);
						sum = DivideByShift(sum, 3);
						highlow_buffer[column] = SATURATE(sum);

						// Apply the lowpass vertical filter to the highpass horizontal results
						sum  = highpass[4][offset];
						sum += highpass[5][offset];
						lowhigh_buffer[column] = SATURATE(sum);

						// Apply the highpass vertical filter to the highpass horizontal results
						sum  = 11 * highpass[4][offset];
						sum -=  5 * highpass[5][offset];
						sum -=  4 * highpass[3][offset];
						sum -=  4 * highpass[2][offset];
						sum +=  1 * highpass[1][offset];
						sum +=  1 * highpass[0][offset];
						sum +=  ROUNDING(sum,8);
						sum = DivideByShift(sum, 3);
						highhigh_buffer[column] = SATURATE(sum);

						offset++;
					}

				#if _PACK_RUNS_IN_BAND_16S
					// Quantize the current row of results for each 16-bit output band
					QuantizeRow16sTo16s(lowhigh_buffer, lowhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[1]);
					lowhigh_row_ptr += PackRuns16s(lowhigh_row_ptr, width);
					QuantizeRow16sTo16s(highlow_buffer, highlow_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[2]);
					highlow_row_ptr += PackRuns16s(highlow_row_ptr, width);
					QuantizeRow16sTo16s(highhigh_buffer, highhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[3]);
					highhigh_row_ptr += PackRuns16s(highhigh_row_ptr, width);
				#else
					// Quantize the last row of results for each 16-bit output band
					QuantizeRow16sTo16s(lowhigh_buffer, lowhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[1]);
					QuantizeRow16sTo16s(highlow_buffer, highlow_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[2]);
					QuantizeRow16sTo16s(highhigh_buffer, highhigh_row_ptr, width, transform[channel]->wavelet[frame_index]->quantization[3]);
				#endif

				}
			}
		}
	}
}




// Invert a spatial wavelet transform to rows of 16-bit luma and chroma
void TransformInverseSpatialToRow16u(TRANSFORM *transform[], int frame_index, int num_channels,
									 PIXEL16U *output, int output_pitch, FRAME_INFO *info,
									 const SCRATCH *scratch, int chroma_offset, int precision)
{
	PIXEL16U *output_row_ptr = output;
	PIXEL16U *output_ptr = output_row_ptr;
	//int output_width = info->width;
	int output_row_width[CODEC_MAX_CHANNELS];
	int output_row_pitch = output_pitch;
	//int format = info->format;
	//char *bufptr;
	int last_row;
	int last_display_row;
	int channel;
	int row;
	int do_edge_row = 0;

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	// This version is for 16-bit pixels
	assert(sizeof(PIXEL) == 2);

	// Check that the output format is YUY16
	//assert(format == DECODED_FORMAT_YR16);

	// Convert the output pitch to units of pixels
	output_pitch /= sizeof(PIXEL16U);

	//DAN20050606 Added to fix issue with non-div by 8 heights.
	last_display_row = info->height/2;

	for (channel = 0; channel < num_channels; channel++)
	{
		// Compute the output row width for this channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		output_row_width[channel] = 2 * wavelet->width;

		// The dimensions of the output image are the same as the luma channel
		if (channel == 0)
		{
			int height = wavelet->height;
			last_row = height;
			last_display_row = info->height/2; //DAN20050606 Added to fix issue with non-div by 8 heihts.
		}
	}

	// Process the top border (first two rows) of the output frame
	row = 0;
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the spatial wavelet associated with this frame and channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Get the width of this band
		int width = wavelet->width;

		// Invert the spatial wavelet into two rows of 16-bit luma or chroma pixels
		InvertSpatialTopRow16sToYUV16(wavelet->band[0], wavelet->pitch, wavelet->band[1], wavelet->pitch,
									  wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
									  output_ptr, output_row_pitch, row, width,
									  (PIXEL *)buffer, buffer_size, precision);

		// Advance the output pointer to the row of output pixels for the next channel
		output_ptr += output_row_width[channel];
	}

	// Pack the color channels into the output frame
	//ConvertYUVStripPlanarToPacked(plane_array, plane_pitch, strip, output_row_ptr, output_pitch, output_width, info->format);

	// Advance the output row pointer past the two rows that were computed
	output_row_ptr += 2 * output_pitch;

	if(last_row == last_display_row)
		do_edge_row = 1;

	// Process the middle rows of the output frame
	for (row = 1; row < last_display_row-do_edge_row; row++)
	{
		output_ptr = output_row_ptr;

		// Invert the spatial transform for each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;

			// Invert the spatial wavelet into two rows of 16-bit luma or chroma pixels
			InvertSpatialMiddleRow16sToYUV16(wavelet->band[0], wavelet->pitch, wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
											 output_ptr, output_row_pitch, row, width,
											 (PIXEL *)buffer, buffer_size, precision);
#if (0 && DEBUG)
			if (channel > 0) {
				PIXEL16U *output1 = output_ptr;
				PIXEL16U *output2 = output_ptr + output_row_pitch/sizeof(PIXEL16U);
				int channel_width = 2 * width;
				PIXEL16U value = (128 << 8);
				FillPixelMemory(output1, channel_width, value);
				FillPixelMemory(output2, channel_width, value);
			}
#endif
			// Advance the output pointer to the row of output pixels for the next channel
			output_ptr += output_row_width[channel];
		}

		// Pack the color channels into the output frame
		//ConvertYUVStripPlanarToPacked(plane_array, plane_pitch, strip, output_row_ptr, output_pitch, output_width, info->format);

		// Advance the output row pointer past the two rows that were computed
		output_row_ptr += 2 * output_pitch;
	}

	// Check that the middle row loop exited at the last row
	assert(row == last_display_row-do_edge_row);

	if(do_edge_row) // Standard disable by 8 -- therefore a edge row.
	{
		output_ptr = output_row_ptr;

		//return;	//***DEBUG*** //DAN07022004 this WASN'T commented out

		// Process the bottom border (last two rows) of the output frame
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;

			// Invert the spatial wavelet into two rows of 16-bit luma or chroma pixels
			InvertSpatialBottomRow16sToYUV16(wavelet->band[0], wavelet->pitch, wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
											 output_ptr, output_row_pitch, row, width,
											 (PIXEL *)buffer, buffer_size, precision);

			// Advance the output pointer to the row of output pixels for the next channel
			output_ptr += output_row_width[channel];
		}
	}

	// Pack the color channels into the output frame
	//ConvertYUVStripPlanarToPacked(plane_array, plane_pitch, strip, output_row_ptr, output_pitch, output_width, info->format);
}

// Adapted from TransformInverseSpatialToRow16u
void TransformInverseRGB444ToB64A(TRANSFORM *transform[], int frame_index, int num_channels,
								  uint8_t *output_buffer, int output_pitch, FRAME_INFO *info,
								  const SCRATCH *scratch, int chroma_offset, int precision)
{
	PIXEL *plane_array[TRANSFORM_MAX_CHANNELS];
	int plane_pitch[TRANSFORM_MAX_CHANNELS];

	uint8_t *output_row_ptr = output_buffer;
	int output_width = info->width;

	ROI strip;
	char *bufptr;
	int last_row,last_display_row;
	int channel;
	int row;
	int do_edge_row = 0;

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	//TODO: Replace uses of buffer variables with calls to the scratch space API

	// This version is for 16-bit pixels
	assert(sizeof(PIXEL) == 2);

	// Round the buffer pointer up to the next cache line
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)buffer & _CACHE_LINE_MASK));
	bufptr = (char *)ALIGN(buffer, _CACHE_LINE_SIZE);

	// Allocate buffer space for the output rows from each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the row width for this channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		//int pitch = wavelet->pitch;
		size_t channel_buffer_size;

		// Compute the width and pitch for the output rows stored in this buffer
		int buffer_width = 2 * width;
		int buffer_height = 2;
		int buffer_pitch = buffer_width * sizeof(PIXEL);

		// Force the proper address alignment for each buffer row
		//buffer_pitch = ALIGN(buffer_width, _CACHE_LINE_SIZE);
		buffer_pitch = ALIGN16(buffer_pitch);

		// Compute the total allocation for this channel
		channel_buffer_size = buffer_height * buffer_pitch;

		// Check that there is enough space available
		assert(channel_buffer_size <= buffer_size);

		// Allocate the buffer for this channel
		plane_array[channel] = (PIXEL *)bufptr;

		// Remember the pitch for rows in this channel
		plane_pitch[channel] = buffer_pitch;

		// Advance the buffer pointer past the allocated space for this channel
		bufptr += channel_buffer_size;

		// Reduce the amount of space remaining in the buffer
		buffer_size -= channel_buffer_size;

		// The dimensions of the output image are the same as the first channel
		if (channel == 0)
		{
			strip.width = buffer_width;
			strip.height = buffer_height;
			last_row = height;

			//DAN20050606: Added to fix issue with heights that are not divisible by eight
			last_display_row = info->height/2;
		}

		// Save the bands per channel for routines that process all channels at once
		//lowlow_band[channel] = wavelet->band[0];
		//lowhigh_band[channel] = wavelet->band[1];
		//highlow_band[channel] = wavelet->band[2];
		//highhigh_band[channel] = wavelet->band[3];

		//lowlow_pitch[channel] = wavelet->pitch;
		//lowhigh_pitch[channel] = wavelet->pitch;
		//highlow_pitch[channel] = wavelet->pitch;
		//highhigh_pitch[channel] = wavelet->pitch;
	}

	// Use the remaining buffer space for intermediate results
	//buffer = bufptr;
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)bufptr & _CACHE_LINE_MASK));
	buffer = (char *)ALIGN(bufptr, _CACHE_LINE_SIZE);

	// Note: Even though the inverse transform routines use the YUV16 prefix,
	// they will work with planes of RGB 4:4:4 with 16 bits per component as
	// int32_t as the array of plane addresses and bytes per row are set correctly.

	// Process the top border (first two rows) of the output frame
	row = 0;
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the spatial wavelet associated with this frame and channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Get the width of this band
		int width = wavelet->width;
#if 1
		// Invert the spatial wavelet into strips of RGB pixels
		InvertSpatialTopRow16sToYUV16(wavelet->band[0], wavelet->pitch,
									  wavelet->band[1], wavelet->pitch,
									  wavelet->band[2], wavelet->pitch,
									  wavelet->band[3], wavelet->pitch,
									  (PIXEL16U *)plane_array[channel], plane_pitch[channel],
									  row, width, (PIXEL *)buffer, buffer_size, precision);
#else
		// Invert the spatial wavelet into strips of RGB pixels
		InvertSpatialTopRow16sToYUV16(wavelet->band[0], wavelet->pitch,
									  wavelet->band[1], wavelet->pitch,
									  wavelet->band[2], wavelet->pitch,
									  wavelet->band[3], wavelet->pitch,
									  plane_array[channel], plane_pitch[channel],
									  row, width, scratch, precision);
#endif
	}

	// Pack the color channels into the output frame
	ConvertPlanarRGB16uToPackedB64A(plane_array, plane_pitch, strip,
									output_row_ptr, output_pitch, output_width);

	// Advance the output row pointer past the two rows that were computed
	output_row_ptr += 2 * output_pitch;

	if (last_display_row == last_row) {
		do_edge_row = 1;
	}

	// Process the middle rows of the output frame
	for (row = 1; row < last_display_row - do_edge_row; row++)
	{
		// Invert the spatial transform for each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;
#if 1
			// Invert the spatial wavelet into strips of RGB pixels
			InvertSpatialMiddleRow16sToYUV16(wavelet->band[0], wavelet->pitch,
											 wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch,
											 wavelet->band[3], wavelet->pitch,
											 (PIXEL16U *)plane_array[channel], plane_pitch[channel],
											 row, width, (PIXEL *)buffer, buffer_size, precision);
#else
			// Invert the spatial wavelet into strips of RGB pixels
			InvertSpatialMiddleRow16sToYUV16(wavelet->band[0], wavelet->pitch,
											 wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch,
											 wavelet->band[3], wavelet->pitch,
											 plane_array[channel], plane_pitch[channel],
											 row, width, scratch, precision);
#endif
		}

		// Pack the color channels into the output frame
		ConvertPlanarRGB16uToPackedB64A(plane_array, plane_pitch, strip,
										output_row_ptr, output_pitch, output_width);

		// Advance the output row pointer past the two rows that were computed
		output_row_ptr += 2 * output_pitch;
	}

	// Check that the middle row loop exited at the last row
	assert(row == last_display_row - do_edge_row);

	if (do_edge_row) // Standard disable by 8 -- therefore a edge row.
	{
		// Process the bottom border (last two rows) of the output frame
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;

			// Invert the spatial wavelet into strips of RGB pixels
			InvertSpatialBottomRow16sToYUV16(wavelet->band[0], wavelet->pitch,
											 wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch,
											 wavelet->band[3], wavelet->pitch,
											 (PIXEL16U *)plane_array[channel], plane_pitch[channel],
											 row, width, (PIXEL *)buffer, buffer_size, precision);
		}

		// Pack the color channels into the output frame
		ConvertPlanarRGB16uToPackedB64A(plane_array, plane_pitch, strip,
										output_row_ptr, output_pitch, output_width);
	}
}

// Adapted from TransformInverseRGB444ToB64A to output YU64
void TransformInverseRGB444ToYU64(TRANSFORM *transform[], int frame_index, int num_channels,
								  uint8_t *output_buffer, int output_pitch, FRAME_INFO *info,
								  const SCRATCH *scratch, int chroma_offset, int precision)
{
	PIXEL *plane_array[TRANSFORM_MAX_CHANNELS];
	int plane_pitch[TRANSFORM_MAX_CHANNELS];

	uint8_t *output_row_ptr = output_buffer;
	int output_width = info->width;

	ROI strip;
	char *bufptr;
	int last_row,last_display_row;
	int channel;
	int row;
	int do_edge_row = 0;

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	//TODO: Replace uses of buffer variables with calls to the scratch space API

	// This version is for 16-bit pixels
	assert(sizeof(PIXEL) == 2);

	// Round the buffer pointer up to the next cache line
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)buffer & _CACHE_LINE_MASK));
	bufptr = (char *)ALIGN(buffer, _CACHE_LINE_SIZE);

	// Allocate buffer space for the output rows from each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the row width for this channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		//int pitch = wavelet->pitch;
		size_t channel_buffer_size;

		// Compute the width and pitch for the output rows stored in this buffer
		int buffer_width = 2 * width;
		int buffer_height = 2;
		int buffer_pitch = buffer_width * sizeof(PIXEL);

		// Force the proper address alignment for each buffer row
		//buffer_pitch = ALIGN(buffer_width, _CACHE_LINE_SIZE);
		buffer_pitch = ALIGN16(buffer_pitch);

		// Compute the total allocation for this channel
		channel_buffer_size = buffer_height * buffer_pitch;

		// Check that there is enough space available
		assert(channel_buffer_size <= buffer_size);

		// Allocate the buffer for this channel
		plane_array[channel] = (PIXEL *)bufptr;

		// Remember the pitch for rows in this channel
		plane_pitch[channel] = buffer_pitch;

		// Advance the buffer pointer past the allocated space for this channel
		bufptr += channel_buffer_size;

		// Reduce the amount of space remaining in the buffer
		buffer_size -= channel_buffer_size;

		// The dimensions of the output image are the same as the first channel
		if (channel == 0)
		{
			strip.width = buffer_width;
			strip.height = buffer_height;
			last_row = height;

			//DAN20050606: Added to fix issue with heights that are not divisible by eight
			last_display_row = info->height/2;
		}

		// Save the bands per channel for routines that process all channels at once
		//lowlow_band[channel] = wavelet->band[0];
		//lowhigh_band[channel] = wavelet->band[1];
		//highlow_band[channel] = wavelet->band[2];
		//highhigh_band[channel] = wavelet->band[3];

		//lowlow_pitch[channel] = wavelet->pitch;
		//lowhigh_pitch[channel] = wavelet->pitch;
		//highlow_pitch[channel] = wavelet->pitch;
		//highhigh_pitch[channel] = wavelet->pitch;
	}

	// Use the remaining buffer space for intermediate results
	//buffer = bufptr;
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)bufptr & _CACHE_LINE_MASK));
	buffer = (char *)ALIGN(bufptr, _CACHE_LINE_SIZE);

	// Note: Even though the inverse transform routines use the YUV16 prefix,
	// they will work with planes of RGB 4:4:4 with 16 bits per component as
	// int32_t as the array of plane addresses and bytes per row are set correctly.

	// Process the top border (first two rows) of the output frame
	row = 0;
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the spatial wavelet associated with this frame and channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Get the width of this band
		int width = wavelet->width;
#if 1
		// Invert the spatial wavelet into strips of RGB pixels
		InvertSpatialTopRow16sToYUV16(wavelet->band[0], wavelet->pitch,
									  wavelet->band[1], wavelet->pitch,
									  wavelet->band[2], wavelet->pitch,
									  wavelet->band[3], wavelet->pitch,
									  (PIXEL16U *)plane_array[channel], plane_pitch[channel],
									  row, width, (PIXEL *)buffer, buffer_size, precision);
#else
		// Invert the spatial wavelet into strips of RGB pixels
		InvertSpatialTopRow16sToYUV16(wavelet->band[0], wavelet->pitch,
									  wavelet->band[1], wavelet->pitch,
									  wavelet->band[2], wavelet->pitch,
									  wavelet->band[3], wavelet->pitch,
									  plane_array[channel], plane_pitch[channel],
									  row, width, scratch, precision);
#endif
	}

	// Pack the color channels into the output frame
	ConvertPlanarRGB16uToPackedYU64(plane_array, plane_pitch, strip,
		output_row_ptr, output_pitch, output_width, info->colorspace);

	// Advance the output row pointer past the two rows that were computed
	output_row_ptr += 2 * output_pitch;

	if (last_display_row == last_row) {
		do_edge_row = 1;
	}

	// Process the middle rows of the output frame
	for (row = 1; row < last_display_row - do_edge_row; row++)
	{
		// Invert the spatial transform for each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;
			// Invert the spatial wavelet into strips of RGB pixels
			InvertSpatialMiddleRow16sToYUV16(wavelet->band[0], wavelet->pitch,
											 wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch,
											 wavelet->band[3], wavelet->pitch,
											 (PIXEL16U *)plane_array[channel], plane_pitch[channel],
											 row, width, (PIXEL *)buffer, buffer_size, precision);
		}

		// Pack the color channels into the output frame
		ConvertPlanarRGB16uToPackedYU64(plane_array, plane_pitch, strip,
			output_row_ptr, output_pitch, output_width, info->colorspace);

		// Advance the output row pointer past the two rows that were computed
		output_row_ptr += 2 * output_pitch;
	}

	// Check that the middle row loop exited at the last row
	assert(row == last_display_row - do_edge_row);

	if (do_edge_row) // Standard disable by 8 -- therefore a edge row.
	{
		// Process the bottom border (last two rows) of the output frame
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;

			// Invert the spatial wavelet into strips of RGB pixels
			InvertSpatialBottomRow16sToYUV16(wavelet->band[0], wavelet->pitch,
											 wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch,
											 wavelet->band[3], wavelet->pitch,
											 (PIXEL16U *)plane_array[channel], plane_pitch[channel],
											 row, width, (PIXEL *)buffer, buffer_size, precision);
		}

		// Pack the color channels into the output frame
		ConvertPlanarRGB16uToPackedYU64(plane_array, plane_pitch, strip,
			output_row_ptr, output_pitch, output_width, info->colorspace);
	}
}


// Adapted from TransformInverseRGB444ToB64A to output RGB32
void TransformInverseRGB444ToRGB32(TRANSFORM *transform[], int frame_index, int num_channels,
								   uint8_t *output_buffer, int output_pitch, FRAME_INFO *info,
								   const SCRATCH *scratch, int chroma_offset, int precision)
{
	PIXEL *plane_array[TRANSFORM_MAX_CHANNELS];
	int plane_pitch[TRANSFORM_MAX_CHANNELS];

	uint8_t *output_row_ptr = output_buffer;
	int output_width = info->width;
	int output_height = info->height;

	ROI strip;
	char *bufptr;
	int last_row;
	int last_display_row;
	int channel;
	int row;
	int do_edge_row = 0;
	int odd_display_lines = 0;

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	//TODO: Replace uses of buffer variables with calls to the scratch space API

	// This version is for 16-bit pixels
	assert(sizeof(PIXEL) == 2);

	// Round the buffer pointer up to the next cache line
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)buffer & _CACHE_LINE_MASK));
	bufptr = (char *)ALIGN(buffer, _CACHE_LINE_SIZE);

	// Allocate buffer space for the output rows from each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the row width for this channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		//int pitch = wavelet->pitch;
		size_t channel_buffer_size;

		// Compute the width and pitch for the output rows stored in this buffer
		int buffer_width = 2 * width;
		int buffer_height = 2;
		int buffer_pitch = buffer_width * sizeof(PIXEL);

		// Force the proper address alignment for each buffer row
		//buffer_pitch = ALIGN(buffer_width, _CACHE_LINE_SIZE);
		buffer_pitch = ALIGN16(buffer_pitch);

		// Compute the total allocation for this channel
		channel_buffer_size = buffer_height * buffer_pitch;

		// Check that there is enough space available
		assert(channel_buffer_size <= buffer_size);

		// Allocate the buffer for this channel
		plane_array[channel] = (PIXEL *)bufptr;

		// Remember the pitch for rows in this channel
		plane_pitch[channel] = buffer_pitch;
		// Advance the buffer pointer past the allocated space for this channel
		bufptr += channel_buffer_size;

		// Reduce the amount of space remaining in the buffer
		buffer_size -= channel_buffer_size;

		// The dimensions of the output image are the same as the first channel
		if (channel == 0)
		{
			strip.width = buffer_width;
			strip.height = buffer_height;
			last_row = height;

			//DAN20050606: Added to fix issue with heights that are not divisible by eight
			last_display_row = (info->height+1)/2; // DAN20090215 -- fix for odd display lines.
			odd_display_lines = info->height & 1;
		}
	}

	// Invert the output frame
	if(output_pitch > 0 && !(info->format & (1<<31)))
	{
		output_row_ptr += (output_height - 1) * output_pitch;
		output_pitch = NEG(output_pitch);
	}

	// Use the remaining buffer space for intermediate results
	//buffer = bufptr;
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)bufptr & _CACHE_LINE_MASK));
	buffer = (char *)ALIGN(bufptr, _CACHE_LINE_SIZE);

	// Note: Even though the inverse transform routines use the YUV16 prefix,
	// they will work with planes of RGB 4:4:4 with 16 bits per component as
	// int32_t as the array of plane addresses and bytes per row are set correctly.

	// Process the top border (first two rows) of the output frame
	row = 0;
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the spatial wavelet associated with this frame and channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Get the width of this band
		int width = wavelet->width;
#if 1
		// Invert the spatial wavelet into strips of RGB pixels
		InvertSpatialTopRow16sToYUV16(wavelet->band[0], wavelet->pitch,
									  wavelet->band[1], wavelet->pitch,
									  wavelet->band[2], wavelet->pitch,
									  wavelet->band[3], wavelet->pitch,
									  (PIXEL16U *)plane_array[channel], plane_pitch[channel],
									  row, width, (PIXEL *)buffer, buffer_size, precision);
#else
		// Invert the spatial wavelet into strips of RGB pixels
		InvertSpatialTopRow16sToYUV16(wavelet->band[0], wavelet->pitch,
									  wavelet->band[1], wavelet->pitch,
									  wavelet->band[2], wavelet->pitch,
									  wavelet->band[3], wavelet->pitch,
									  plane_array[channel], plane_pitch[channel],
									  row, width, scratch, precision);
#endif
	}

	// Pack the color channels into the output frame
	if(info->format == DECODED_FORMAT_RGB24_INVERTED || info->format == DECODED_FORMAT_RGB24)
	{
		ConvertPlanarRGB16uToPackedRGB24(plane_array, plane_pitch, strip,
										 output_row_ptr, output_pitch, output_width, 8);
	}
	else
	{
		ConvertPlanarRGB16uToPackedRGB32(plane_array, plane_pitch, strip,
										 output_row_ptr, output_pitch, output_width, 8, num_channels);
	}

	// Advance the output row pointer past the two rows that were computed
	output_row_ptr += 2 * output_pitch;

	if (last_display_row == last_row) {
		do_edge_row = 1;
	}

	// Process the middle rows of the output frame
	for (row = 1; row < last_display_row - do_edge_row; row++)
	{
		// Invert the spatial transform for each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;
#if 1
			// Invert the spatial wavelet into strips of RGB pixels
			InvertSpatialMiddleRow16sToYUV16(wavelet->band[0], wavelet->pitch,
											 wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch,
											 wavelet->band[3], wavelet->pitch,
											 (PIXEL16U *)plane_array[channel], plane_pitch[channel],
											 row, width, (PIXEL *)buffer, buffer_size, precision);
#else
			// Invert the spatial wavelet into strips of RGB pixels
			InvertSpatialMiddleRow16sToYUV16(wavelet->band[0], wavelet->pitch,
											 wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch,
											 wavelet->band[3], wavelet->pitch,
											 plane_array[channel], plane_pitch[channel],
											 row, width, scratch, precision);
#endif
		}

		if(odd_display_lines)
		{
			if(row == last_display_row - do_edge_row - 1)
			{
				strip.height = 1;
			}
		}

		// Pack the color channels into the output frame
		if(info->format == DECODED_FORMAT_RGB24_INVERTED || info->format == DECODED_FORMAT_RGB24)
		{
			ConvertPlanarRGB16uToPackedRGB24(plane_array, plane_pitch, strip,
											 output_row_ptr, output_pitch, output_width, 8);
		}
		else
		{
			ConvertPlanarRGB16uToPackedRGB32(plane_array, plane_pitch, strip,
											 output_row_ptr, output_pitch, output_width, 8,
											 num_channels);
		}

		// Advance the output row pointer past the two rows that were computed
		output_row_ptr += 2 * output_pitch;
	}

	// Check that the middle row loop exited at the last row
	assert(row == last_display_row - do_edge_row);

	if (do_edge_row) // Standard disable by 8 -- therefore a edge row.
	{
		// Process the bottom border (last two rows) of the output frame
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;

			// Invert the spatial wavelet into strips of RGB pixels
			InvertSpatialBottomRow16sToYUV16(wavelet->band[0], wavelet->pitch,
											 wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch,
											 wavelet->band[3], wavelet->pitch,
											 (PIXEL16U *)plane_array[channel], plane_pitch[channel],
											 row, width, (PIXEL *)buffer, buffer_size, precision);
		}

		// Pack the color channels into the output frame

		if(info->format == DECODED_FORMAT_RGB24_INVERTED || info->format == DECODED_FORMAT_RGB24)
			ConvertPlanarRGB16uToPackedRGB24(plane_array, plane_pitch, strip,
											output_row_ptr, output_pitch, output_width, 8);
		else
			ConvertPlanarRGB16uToPackedRGB32(plane_array, plane_pitch, strip,
											output_row_ptr, output_pitch, output_width, 8, num_channels);
	}
}

// Adapted from TransformInverseRGB444ToRGB32 to output RGB48
void TransformInverseRGB444ToRGB48(TRANSFORM *transform[], int frame_index, int num_channels,
								   uint8_t *output_buffer, int output_pitch, FRAME_INFO *info,
								   const SCRATCH *scratch, int chroma_offset, int precision)
{
	PIXEL *plane_array[TRANSFORM_MAX_CHANNELS];
	int plane_pitch[TRANSFORM_MAX_CHANNELS];

	uint8_t *output_row_ptr = output_buffer;
	int output_width = info->width;
	//int output_height = info->height;

	ROI strip;
	uint8_t *bufptr;
	int last_row;
	int last_display_row;
	int channel;
	int row;
	int do_edge_row = 0;

	// Push the scratch space state to allocate a new section
	uint8_t *buffer = (uint8_t *)scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	//TODO: Remove chroma-offset if it is never used anywhere in the code
	(void) chroma_offset;

	//TODO: Replace uses of buffer variables with calls to the scratch space API

	// This version is for 16-bit pixels
	assert(sizeof(PIXEL) == 2);

	// Round the buffer pointer up to the next cache line
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)buffer & _CACHE_LINE_MASK));
	bufptr = (uint8_t *)ALIGN(buffer, _CACHE_LINE_SIZE);

	// Allocate buffer space for the output rows from each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the row width for this channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		//int pitch = wavelet->pitch;
		size_t channel_buffer_size;

		// Compute the width and pitch for the output rows stored in this buffer
		int buffer_width = 2 * width;
		int buffer_height = 2;
		int buffer_pitch = buffer_width * sizeof(PIXEL);

		// Force the proper address alignment for each buffer row
		//buffer_pitch = ALIGN(buffer_width, _CACHE_LINE_SIZE);
		buffer_pitch = ALIGN16(buffer_pitch);

		// Compute the total allocation for this channel
		channel_buffer_size = buffer_height * buffer_pitch;

		// Check that there is enough space available
		assert(channel_buffer_size <= buffer_size);

		// Allocate the buffer for this channel
		plane_array[channel] = (PIXEL *)bufptr;

		// Remember the pitch for rows in this channel
		plane_pitch[channel] = buffer_pitch;

		// Advance the buffer pointer past the allocated space for this channel
		bufptr += channel_buffer_size;

		// Reduce the amount of space remaining in the buffer
		buffer_size -= channel_buffer_size;

		// The dimensions of the output image are the same as the first channel
		if (channel == 0)
		{
			strip.width = buffer_width;
			strip.height = buffer_height;
			last_row = height;

			//DAN20050606: Added to fix issue with heights that are not divisible by eight
			last_display_row = info->height/2;
		}
	}

	// Invert the output frame
	//output_row_ptr += (output_height - 1) * output_pitch;
	//output_pitch = NEG(output_pitch);

	// Use the remaining buffer space for intermediate results
	//buffer = bufptr;
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)bufptr & _CACHE_LINE_MASK));
	buffer = (uint8_t *)ALIGN(bufptr, _CACHE_LINE_SIZE);

	// Note: Even though the inverse transform routines use the YUV16 prefix,
	// they will work with planes of RGB 4:4:4 with 16 bits per component as
	// int32_t as the array of plane addresses and bytes per row are set correctly.

	// Process the top border (first two rows) of the output frame
	row = 0;
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the spatial wavelet associated with this frame and channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Get the width of this band
		int width = wavelet->width;
#if 1
		// Invert the spatial wavelet into strips of RGB pixels
		InvertSpatialTopRow16sToYUV16(wavelet->band[0], wavelet->pitch,
									  wavelet->band[1], wavelet->pitch,
									  wavelet->band[2], wavelet->pitch,
									  wavelet->band[3], wavelet->pitch,
									  (PIXEL16U *)plane_array[channel], plane_pitch[channel],
									  row, width, (PIXEL *)buffer, buffer_size, precision);
#else
		// Invert the spatial wavelet into strips of RGB pixels
		InvertSpatialTopRow16sToYUV16(wavelet->band[0], wavelet->pitch,
									  wavelet->band[1], wavelet->pitch,
									  wavelet->band[2], wavelet->pitch,
									  wavelet->band[3], wavelet->pitch,
									  plane_array[channel], plane_pitch[channel],
									  row, width, scratch, precision);
#endif
	}

	// Pack the color channels into the output frame
	if(info->format == DECODED_FORMAT_RG48)
	{
		ConvertPlanarRGB16uToPackedRGB48(plane_array, plane_pitch, strip,
										output_row_ptr, output_pitch, output_width);
	}
	else if(info->format == COLOR_FORMAT_RG64)
	{
		//WIP
		ConvertPlanarRGB16uToPackedRGBA64(plane_array, plane_pitch, strip,
										output_row_ptr, output_pitch, output_width);
	}
	else
	{
		ConvertPlanarRGB16uToPackedRGB30(plane_array, plane_pitch, strip,
				output_row_ptr, output_pitch, output_width, info->format, info->colorspace);
	}

	// Advance the output row pointer past the two rows that were computed
	output_row_ptr += 2 * output_pitch;

	if (last_display_row == last_row) {
		do_edge_row = 1;
	}

	// Process the middle rows of the output frame
	for (row = 1; row < last_display_row - do_edge_row; row++)
	{
		// Invert the spatial transform for each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;
#if 1
			// Invert the spatial wavelet into strips of RGB pixels
			InvertSpatialMiddleRow16sToYUV16(wavelet->band[0], wavelet->pitch,
											 wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch,
											 wavelet->band[3], wavelet->pitch,
											 (PIXEL16U *)plane_array[channel], plane_pitch[channel],
											 row, width, (PIXEL *)buffer, buffer_size, precision);
#else
			// Invert the spatial wavelet into strips of RGB pixels
			InvertSpatialMiddleRow16sToYUV16(wavelet->band[0], wavelet->pitch,
											 wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch,
											 wavelet->band[3], wavelet->pitch,
											 plane_array[channel], plane_pitch[channel],
											 row, width, scratch, precision);
#endif
		}

		// Pack the color channels into the output frame
		if(info->format == DECODED_FORMAT_RG48)
			ConvertPlanarRGB16uToPackedRGB48(plane_array, plane_pitch, strip,
										 output_row_ptr, output_pitch, output_width);
		else if(info->format == COLOR_FORMAT_RG64) //WIP
			ConvertPlanarRGB16uToPackedRGBA64(plane_array, plane_pitch, strip,
										 output_row_ptr, output_pitch, output_width);
		else
			ConvertPlanarRGB16uToPackedRGB30(plane_array, plane_pitch, strip,
				output_row_ptr, output_pitch, output_width, info->format, info->colorspace);


		// Advance the output row pointer past the two rows that were computed
		output_row_ptr += 2 * output_pitch;
	}

	// Check that the middle row loop exited at the last row
	assert(row == last_display_row - do_edge_row);

	if (do_edge_row) // Standard disable by 8 -- therefore a edge row.
	{

		// Process the bottom border (last two rows) of the output frame
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;

			// Invert the spatial wavelet into strips of RGB pixels
			InvertSpatialBottomRow16sToYUV16(wavelet->band[0], wavelet->pitch,
											 wavelet->band[1], wavelet->pitch,
											 wavelet->band[2], wavelet->pitch,
											 wavelet->band[3], wavelet->pitch,
											 (PIXEL16U *)plane_array[channel], plane_pitch[channel],
											 row, width, (PIXEL *)buffer, buffer_size, precision);
		}

		// Pack the color channels into the output frame
		if(info->format == DECODED_FORMAT_RG48)
			ConvertPlanarRGB16uToPackedRGB48(plane_array, plane_pitch, strip,
										 output_row_ptr, output_pitch, output_width);
		else if(info->format == COLOR_FORMAT_RG64) //WIP
			ConvertPlanarRGB16uToPackedRGBA64(plane_array, plane_pitch, strip,
										 output_row_ptr, output_pitch, output_width);
		else
			ConvertPlanarRGB16uToPackedRGB30(plane_array, plane_pitch, strip,
										 output_row_ptr, output_pitch, output_width,
										 info->format, info->colorspace);
	}
}



// Invert a spatial wavelet transform to packed pixels.  The code was copied from
// TransformInverseSpatialToYUV and adapted to handle any decoded color format.
#if 0
void TransformInverseSpatialToBuffer(DECODER *decoder, TRANSFORM *transform[], int frame_index, int num_channels,
									 uint8_t *output, int output_pitch, FRAME_INFO *info,
									 char *buffer, size_t buffer_size, int chroma_offset,
									 int precision)
#else
void TransformInverseSpatialToBuffer(DECODER *decoder, TRANSFORM *transform[], int frame_index, int num_channels,
									 uint8_t *output, int output_pitch, FRAME_INFO *info,
									 const SCRATCH *scratch, int chroma_offset, int precision)
#endif
{
	uint8_t *output_row_ptr = output;
	uint8_t *plane_array[TRANSFORM_MAX_CHANNELS];
	int plane_pitch[TRANSFORM_MAX_CHANNELS];
	int output_width = info->width;
	int format = info->format;
	ROI strip;
	char *bufptr;
	int last_row,last_display_row;
	int channel;
	int row;
	int do_edge_row = 0;

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	// This version is for 16-bit pixels
	assert(sizeof(PIXEL) == 2);

	// Round the buffer pointer up to the next cache line
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)buffer & _CACHE_LINE_MASK));
	bufptr = (char *)ALIGN(buffer, _CACHE_LINE_SIZE);

	// Allocate buffer space for the output rows from each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the row width for this channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = info->height/2;//wavelet->height;  //DAN20041022 Fix for decoding clips with height not divisible by 8.
		//int pitch = wavelet->pitch;
		size_t channel_buffer_size;

		// Compute the width and pitch for the output rows stored in this buffer
		int buffer_width = 2 * width;
		int buffer_height = 2;
		//int buffer_pitch = ALIGN(buffer_width, _CACHE_LINE_SIZE);
		int buffer_pitch = ALIGN16(buffer_width);

		//TODO: Replace these buffer allocations with calls to the scratch space API

		//DAN20070501 -- 10-bit YUV encodes decoding to 8-bit RGB need the space to dither.
		if(precision > 8)
			buffer_pitch *= 2;

		// Compute the total allocation for this channel
		channel_buffer_size = buffer_height * buffer_pitch;

		// Check that there is enough space available
		assert(channel_buffer_size <= buffer_size);

		// Allocate the buffer for this channel
		plane_array[channel] = (uint8_t *)bufptr;

		// Remember the pitch for rows in this channel
		plane_pitch[channel] = buffer_pitch;

		// Advance the buffer pointer past the allocated space for this channel
		bufptr += channel_buffer_size;

		// Reduce the amount of space remaining in the buffer
		buffer_size -= channel_buffer_size;

		// The dimensions of the output image are the same as the luma channel
		if (channel == 0)
		{
			strip.width = buffer_width;
			strip.height = buffer_height;
			last_row = height;
			last_display_row = info->height/2; //DAN20050606 Added to fix issue with non-div by 8 heihts.
		}
	}

	// Use the remaining buffer space for intermediate results
	//buffer = bufptr;
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)bufptr & _CACHE_LINE_MASK));
	buffer = (char *)ALIGN(bufptr, _CACHE_LINE_SIZE);

	// Process the top border (first two rows) of the output frame
	row = 0;
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the spatial wavelet associated with this frame and channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Get the width of this band
		int width = wavelet->width;

		// Invert the spatial wavelet into strips of YUV pixels (into packed YUV later)
		InvertSpatialTopRow16s(wavelet->band[0], wavelet->pitch, wavelet->band[1], wavelet->pitch,
							   wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
							   plane_array[channel], plane_pitch[channel],
							   row, width, (PIXEL *)buffer, buffer_size, precision,info);
	}

	// Pack the color channels into the output frame
	STOP(tk_inverse);
	if(precision == 8 || _NODITHER || DECODEDFORMAT(info)==DECODED_FORMAT_YUYV || DECODEDFORMAT(info)==COLOR_FORMAT_UYVY)
	{
		ConvertYUVStripPlanarToBuffer(plane_array, plane_pitch, strip, output_row_ptr,
								  output_pitch, output_width, format, info->colorspace);
	}
	else
	{
		ConvertRow16uToDitheredBuffer(decoder, plane_array, plane_pitch, strip, output_row_ptr,
								  output_pitch, output_width, format, info->colorspace);
	}
	START(tk_inverse);

	// Advance the output row pointer past the two rows that were computed
	output_row_ptr += 2 * output_pitch;

	if(last_row == last_display_row)
		do_edge_row = 1;

	// Process the middle rows of the output frame
	for (row = 1; row < last_display_row-do_edge_row; row++)
	{
		// Invert the spatial transform for each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;

			// Invert the spatial wavelet into strips of YUV pixels (into packed YUV later)
			InvertSpatialMiddleRow16s(wavelet->band[0], wavelet->pitch, wavelet->band[1], wavelet->pitch,
									  wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
									  plane_array[channel], plane_pitch[channel],
									  row, width, (PIXEL *)buffer, buffer_size, precision, info);
		}

		// Pack the color channels into the output frame
		STOP(tk_inverse);
		if(precision == 8 || _NODITHER || DECODEDFORMAT(info)==DECODED_FORMAT_YUYV || DECODEDFORMAT(info)==COLOR_FORMAT_UYVY)
		{
			ConvertYUVStripPlanarToBuffer(plane_array, plane_pitch, strip, output_row_ptr,
									  output_pitch, output_width, format, info->colorspace);
		}
		else
		{
			ConvertRow16uToDitheredBuffer(decoder, plane_array, plane_pitch, strip, output_row_ptr,
									  output_pitch, output_width, format, info->colorspace);
		}
		START(tk_inverse);

		// Advance the output row pointer past the two rows that were computed
		output_row_ptr += 2 * output_pitch;
	}

	// Check that the middle row loop exited at the last row
	assert(row == last_display_row-do_edge_row);

	if(do_edge_row) // Standard disable by 8 -- therefore a edge row.
	{
		// Process the bottom border (last two rows) of the output frame
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;

			// Invert the spatial wavelet into strips of YUV pixels (into packed YUV later)
			InvertSpatialBottomRow16s(wavelet->band[0], wavelet->pitch, wavelet->band[1], wavelet->pitch,
									  wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
									  plane_array[channel], plane_pitch[channel],
									  row, width, (PIXEL *)buffer, buffer_size, precision, info);
		}

		// Pack the color channels into the output frame
		STOP(tk_inverse);
		if(precision == 8 || _NODITHER || DECODEDFORMAT(info)==DECODED_FORMAT_YUYV || DECODEDFORMAT(info)==COLOR_FORMAT_UYVY)
		{
			ConvertYUVStripPlanarToBuffer(plane_array, plane_pitch, strip, output_row_ptr,
									  output_pitch, output_width, format, info->colorspace);
		}
		else
		{
			ConvertRow16uToDitheredBuffer(decoder, plane_array, plane_pitch, strip, output_row_ptr,
									  output_pitch, output_width, format, info->colorspace);
		}
		START(tk_inverse);
	}
}


//#if BUILD_PROSPECT
// Invert a spatial wavelet transform to packed 10-bit pixels.  The code was copied from
// TransformInverseSpatialToBuffer and adapted to store the output pixels in V210 format.
#if 0
void TransformInverseSpatialToV210(TRANSFORM *transform[], int frame_index, int num_channels,
								   uint8_t *output, int output_pitch, FRAME_INFO *info,
								   char *buffer, size_t buffer_size, int chroma_offset, int precision)
#else
void TransformInverseSpatialToV210(TRANSFORM *transform[], int frame_index, int num_channels,
								   uint8_t *output, int output_pitch, FRAME_INFO *info,
								   const SCRATCH *scratch, int chroma_offset, int precision)
#endif
{
	uint8_t *output_row_ptr = output;
	PIXEL *plane_array[TRANSFORM_MAX_CHANNELS];
	int plane_pitch[TRANSFORM_MAX_CHANNELS];
	int output_width = info->width;
	int format = info->format;
	ROI strip;
	char *bufptr;
	int last_row,last_display_row;
	int channel;
	int row;
	int do_edge_row = 0;

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	// This version is for 16-bit pixels
	assert(sizeof(PIXEL) == 2);

	if (format == COLOR_FORMAT_V210)
	{
		// Compute the number of pixels to the end of the row in the frame buffer
		int frame_width = (3 * output_pitch) / 8;
		assert(output_width <= frame_width);

		// Adjust the output width to provide enough data for six pairs of luma and chroma
		output_width = frame_width;
	}

	// Round the buffer pointer up to the next cache line
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)buffer & _CACHE_LINE_MASK));
	bufptr = (char *)ALIGN(buffer, _CACHE_LINE_SIZE);

	// Allocate buffer space for the output rows from each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the row width for this channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		//int pitch = wavelet->pitch;
		size_t channel_buffer_size;

		// Compute the dimensions of the strip of output rows stored in this buffer
		int buffer_width = ((format == COLOR_FORMAT_V210) ? output_width : 2 * width);
		int buffer_height = 2;
		int buffer_pitch = buffer_width * sizeof(PIXEL);
		buffer_pitch = ALIGN16(buffer_pitch);

		// Compute the total allocation for this channel
		channel_buffer_size = buffer_height * buffer_pitch;

		// Check that there is enough space available
		assert(channel_buffer_size <= buffer_size);

		//TODO: Replace buffer allocation code with calls to the scratch API

		// Allocate the buffer for this channel
		plane_array[channel] = (PIXEL *)bufptr;

		// Remember the pitch for rows in this channel
		plane_pitch[channel] = buffer_pitch;

		// Advance the buffer pointer past the allocated space for this channel
		bufptr += channel_buffer_size;

		// Reduce the amount of space remaining in the buffer
		buffer_size -= channel_buffer_size;

		// The dimensions of the output image are the same as the luma channel
		if (channel == 0)
		{
			strip.width = buffer_width;
			strip.height = buffer_height;
			last_row = height;
			last_display_row = info->height/2; //DAN20050606 Added to fix issue with non-div by 8 heihts.
		}
	}

	// Use the remaining buffer space for intermediate results
	//buffer = bufptr;
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)bufptr & _CACHE_LINE_MASK));
	buffer = (char *)ALIGN(bufptr, _CACHE_LINE_SIZE);

	// Process the top border (first two rows) of the output frame
	row = 0;
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the spatial wavelet associated with this frame and channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Get the width of this band
		int width = wavelet->width;

		// Invert the spatial wavelet into strips of YUV pixels (packed into V210 format later)
		InvertSpatialTopRow10bit16s(wavelet->band[0], wavelet->pitch, wavelet->band[1], wavelet->pitch,
									wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
									plane_array[channel], plane_pitch[channel],
									row, width, (PIXEL *)buffer, buffer_size);
	}

	// Pack the color channels into the output frame
	STOP(tk_inverse);
	ConvertYUVStripPlanarToV210(plane_array, plane_pitch, strip, output_row_ptr,
								output_pitch, output_width, format, info->colorspace, precision);
	START(tk_inverse);

	// Advance the output row pointer past the two rows that were computed
	output_row_ptr += 2 * output_pitch;

	if(last_display_row == last_row)// Standard disable by 8 -- therefore last row is an edge
		do_edge_row = 1;

	// Process the middle rows of the output frame
	for (row = 1; row < last_display_row-do_edge_row; row++)
	{
		// Invert the spatial transform for each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;

			// Invert the spatial wavelet into strips of YUV pixels (packed into V210 format later)
			InvertSpatialMiddleRow10bit16s(wavelet->band[0], wavelet->pitch, wavelet->band[1], wavelet->pitch,
										   wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
										   plane_array[channel], plane_pitch[channel],
										   row, width, (PIXEL *)buffer, buffer_size);
		}

		// Pack the color channels into the output frame
		STOP(tk_inverse);
		ConvertYUVStripPlanarToV210(plane_array, plane_pitch, strip, output_row_ptr,
								output_pitch, output_width, format, info->colorspace, precision);
		START(tk_inverse);

		// Advance the output row pointer past the two rows that were computed
		output_row_ptr += 2 * output_pitch;
	}

	// Check that the middle row loop exited at the last row
	assert(row == last_display_row-do_edge_row);

	if(do_edge_row) // Standard disable by 8 -- therefore a edge row.
	{

		// Process the bottom border (last two rows) of the output frame
		for (channel = 0; channel < num_channels; channel++)
		{
			// Get the spatial wavelet associated with this frame and channel
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];

			// Get the width of this band
			int width = wavelet->width;

			// Invert the spatial wavelet into strips of YUV pixels (packed into V210 format later)
			InvertSpatialBottomRow10bit16s(wavelet->band[0], wavelet->pitch, wavelet->band[1], wavelet->pitch,
										   wavelet->band[2], wavelet->pitch, wavelet->band[3], wavelet->pitch,
										   plane_array[channel], plane_pitch[channel],
										   row, width, (PIXEL *)buffer, buffer_size);
		}

		// Pack the color channels into the output frame
		STOP(tk_inverse);
		ConvertYUVStripPlanarToV210(plane_array, plane_pitch, strip, output_row_ptr,
									output_pitch, output_width, format, info->colorspace, precision);
		START(tk_inverse);
	}
}
//#endif


#if 0

// Optimized version of routine to invert a spatial wavelet transform.
// Part of the buffer is used to hold dequantized highpass coefficients.
void TransformInverseSpatialQuantLowpass(IMAGE *input, IMAGE *output,
										 PIXEL *buffer, size_t buffer_size,
										 int scale, bool inverse_prescale)
{
	// Dimensions of each wavelet band
	int input_width = input->width;
	int input_height = input->height;
	//int lowpass_border = 0;		//input->lowpass_border;
	//int highpass_border = 0;		//input->highpass_border;

	ROI roi = {input_width, input_height};

	int pixel_type = input->pixel_type[1];

	// This version is for 16-bit pixels
	//assert(sizeof(PIXEL) == 2);

	// Check that a valid input image has been provided
	assert(input != NULL);
	assert(input->type == IMAGE_TYPE_WAVELET);
	assert(input->band[0] != NULL);
	assert(input->band[1] != NULL);
	assert(input->band[2] != NULL);
	assert(input->band[3] != NULL);

	// Check that the output image is a gray image or a lowpass wavelet band
	assert(output->type == IMAGE_TYPE_GRAY || output->type == IMAGE_TYPE_WAVELET);
	assert(output->band[0] != NULL);

	// Check that the divisors make sense
	//assert(input->divisor[0] == (input->divisor[1] * input->divisor[2]));
	//assert(input->divisor[3] == 1);

	// Check that the spatial highpass band coefficients are 16 bits
	assert(pixel_type == PIXEL_TYPE_16S);

	{
#if (0 && DEBUG)
		char name[_MAX_PATH];
		sprintf(name, "Input");
		DumpPGM(name, input, NULL);
#endif

	//if (input->level >= 4)
	if (0)
	{
#if (0 && DEBUG)
		char filename[_MAX_PATH];
		static int count = 0;
		sprintf(filename, "Lowpass-%d-", count++);
#endif
		// Overflow protection needed when the coefficients are large.
		InvertSpatialQuantOverflowProtected16s(input->band[0], input->pitch,
											   input->band[1], input->pitch,
											   input->band[2], input->pitch,
											   input->band[3], input->pitch,
											   output->band[0], output->pitch,
											   roi, inverse_prescale,
											   buffer, buffer_size, input->quantization);
#if (0 && DEBUG)
		DumpBandSignPGM(filename, output, 0, NULL);
#endif
	}
	else if (inverse_prescale)
	{
		// Apply the inverse spatial transform for a lowpass band that was prescaled
		InvertSpatialPrescaledQuant16s((PIXEL *)input->band[0], input->pitch,
									   (PIXEL *)input->band[1], input->pitch,
									   (PIXEL *)input->band[2], input->pitch,
									   (PIXEL *)input->band[3], input->pitch,
									   output->band[0], output->pitch, roi,
									   buffer, buffer_size, input->quantization);
	}
	else
	{
		// Apply the inverse spatial transform for a lowpass band that is not prescaled
		InvertSpatialQuant16s((PIXEL *)input->band[0], input->pitch,   (PIXEL *)input->band[1], input->pitch,
							 (PIXEL *)input->band[2], input->pitch, (PIXEL *)input->band[3], input->pitch,
							 output->band[0], output->pitch, roi, buffer, buffer_size, input->quantization);
	}

#if (0 && DEBUG)
		sprintf(name,"out%02d-",count++);
		DumpPGM(name,output,NULL);
#endif
	}

	// Adjust the scale factor for display to account for inverse filtering
	//output->scale[0] = input->scale[0] / 4;
}

#else

// Simplified version for debugging problems with prescaling during encoding
#if 0
void TransformInverseSpatialQuantLowpass(IMAGE *input, IMAGE *output,
										 PIXEL *buffer, size_t buffer_size,
										 int scale, bool inverse_prescale)
#else
void TransformInverseSpatialQuantLowpass(IMAGE *input, IMAGE *output,
										 const SCRATCH *scratch,
										 int scale, bool inverse_prescale)
#endif
{
	// Dimensions of each wavelet band
	int input_width = input->width;
	int input_height = input->height;
	ROI roi = {input_width, input_height};

	PIXEL *buffer = (PIXEL *)scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	//int pixel_type = input->pixel_type[1];

#if (0 && DEBUG)
	char name[PATH_MAX];
	static int count = 0;
#endif

	// Check that a valid input image has been provided
	assert(input != NULL);
	assert(input->type == IMAGE_TYPE_WAVELET);
	assert(input->band[0] != NULL);
	assert(input->band[1] != NULL);
	assert(input->band[2] != NULL);
	assert(input->band[3] != NULL);

	// Check that the output image is a gray image or a lowpass wavelet band
	assert(output->type == IMAGE_TYPE_GRAY || output->type == IMAGE_TYPE_WAVELET);
	assert(output->band[0] != NULL);

	// Check that the spatial highpass band coefficients are 16 bits
	assert(input->pixel_type[1] == PIXEL_TYPE_16S);

#if (0 && DEBUG)
	sprintf(name, "Input%02d-", count);
	DumpPGM(name, input, NULL);
#endif

// universal decoder
	if (scale == 1)
	{
		// This is a spatial transform for an intra frame transform
		//int prescale = (inverse_prescale ? scale : 0);

		// Apply the inverse spatial transform for a lowpass band that was prescaled
		InvertSpatialQuant1x16s((PIXEL *)input->band[0], input->pitch,
								(PIXEL *)input->band[1], input->pitch,
								(PIXEL *)input->band[2], input->pitch,
								(PIXEL *)input->band[3], input->pitch,
								output->band[0], output->pitch, roi,
								buffer, buffer_size, input->quantization);
	}
	else if (scale > 1)
	{
		// This is a spatial transform for the lowpass temporal band
		int prescale = (inverse_prescale ? 2 : 0);

		// Apply the inverse spatial transform for a lowpass band that is not prescaled
		InvertSpatialQuantDescale16s((PIXEL *)input->band[0], input->pitch,
									 (PIXEL *)input->band[1], input->pitch,
									 (PIXEL *)input->band[2], input->pitch,
									 (PIXEL *)input->band[3], input->pitch,
									 output->band[0], output->pitch,
									 roi, buffer, buffer_size,
									 prescale, input->quantization);
	}
	else
	{
		assert(scale == 0);
		// This case does not handle any prescaling applied during encoding

		// Apply the inverse spatial transform for a lowpass band that is not prescaled
		if (input->level >= 4)
		{
			InvertSpatialQuantOverflowProtected16s((PIXEL *)input->band[0], input->pitch,   (PIXEL *)input->band[1], input->pitch,
							  (PIXEL *)input->band[2], input->pitch, (PIXEL *)input->band[3], input->pitch,
							  output->band[0], output->pitch, roi, buffer, buffer_size, input->quantization);
		}
		else
		{
			InvertSpatialQuant16s((PIXEL *)input->band[0], input->pitch,   (PIXEL *)input->band[1], input->pitch,
							  (PIXEL *)input->band[2], input->pitch, (PIXEL *)input->band[3], input->pitch,
							  output->band[0], output->pitch, roi, buffer, buffer_size, input->quantization);
		}
	}


#if (0 && DEBUG)
	sprintf(name, "Output%02d-", count++);
	DumpPGM(name, output, NULL);
#endif

}

#endif


// Optimized version of routine to invert a spatial wavelet transform
// A line_buffer is passed to temporarily hold dequantized highpass coefficients
void TransformInverseSpatialQuantHighpass(IMAGE *input, IMAGE *output, PIXEL *buffer, size_t buffer_size, int scale)
{
	// Even reconstruction filter
	//Ipp32s arrayEvenKernel[3] = {1, 8, -1};
	//Ipp32s arrayEvenKernel[3] = {-1, 8, 1};
	//int nEvenKernel = 3;
	//int iEvenAnchor = 2;
	//int iEvenDivisor = 8;

	// Odd reconstruction filter
	//Ipp32s arrayOddKernel[3] = {-1, 8, 1};
	//Ipp32s arrayOddKernel[3] = {1, 8, -1};
	//int nOddKernel = 3;
	//int iOddAnchor = 2;
	//int iOddDivisor = 8;

	// Dimensions of each wavelet band
	int input_width = input->width;
	int input_height = input->height;
	//int lowpass_border = 0;		//input->lowpass_border;
	//int highpass_border = 0;	//input->highpass_border;

	ROI roi = {input_width, input_height};

	//int input_pitch = input->pitch/sizeof(PIXEL);
	//int output_pitch = output->pitch/sizeof(PIXEL);

	// Start with the divisor for the vertical reconstruction
	//int divisor = input->divisor[2];

	//ROI all = {output->width, output->height};
		
	PIXEL *line_buffer;
	size_t buffer_row_size;

	//int pixel_type = input->pixel_type[1];

	//bool inverse_prescale = false;

	// This version is for 16-bit pixels
	assert(sizeof(PIXEL) == 2);

	// Check that a valid input image has been provided
	assert(input != NULL);
	assert(input->type == IMAGE_TYPE_WAVELET);
	assert(input->band[0] != NULL);
	assert(input->band[1] != NULL);
	assert(input->band[2] != NULL);
	assert(input->band[3] != NULL);

	// Check that the output image is a wavelet with the highpass band allocated
	assert(output->type == IMAGE_TYPE_WAVELET);
	assert(output->band[1] != NULL);

	// Check that the divisors make sense
	//assert(input->divisor[0] == (input->divisor[1] * input->divisor[2]));
	//assert(input->divisor[3] == 1);

	// Allocate the buffer for dequantizing the highpass coefficients
	buffer_row_size = input_width * sizeof(PIXEL);
	buffer_row_size = ALIGN16(buffer_row_size);

	line_buffer = buffer + 4 * buffer_row_size/sizeof(PIXEL);

	// Check that prescale is 0 in the spatial bands in the temporal highpass
//	assert(scale == 0);

	// Check that the spatial highpass band coefficients are 8 bits
	assert(input->pixel_type[0] == PIXEL_TYPE_16S);
	assert(input->pixel_type[1] == PIXEL_TYPE_16S);
	assert(input->pixel_type[2] == PIXEL_TYPE_16S);
	assert(input->pixel_type[3] == PIXEL_TYPE_16S);

#if 1

	if(scale)
	{
		int prescale = scale;

		// Apply the inverse spatial transform for a lowpass band that is not prescaled
		InvertSpatialQuantDescale16s((PIXEL *)input->band[0], input->pitch,
									 (PIXEL *)input->band[1], input->pitch,
									 (PIXEL *)input->band[2], input->pitch,
									 (PIXEL *)input->band[3], input->pitch,
									 output->band[1], output->pitch,
									 roi, buffer, buffer_size,
									 prescale, input->quantization);
	}
	else
	{
		InvertSpatialQuantOverflowProtected16s(input->band[0], input->pitch,
									   input->band[1], input->pitch,
									   input->band[2], input->pitch,
									   input->band[3], input->pitch,
									   output->band[1], output->pitch,
									   roi, buffer, buffer_size, input->quantization);
	}

#else
	// Invert the highpass spatial transform to produce a 16-bit result
	InvertSpatial16sTo16s((PIXEL *)input->band[0], input->pitch,
						 (PIXEL *)input->band[1], input->pitch,
						 (PIXEL *)input->band[2], input->pitch,
						 (PIXEL *)input->band[3], input->pitch,
						 (PIXEL *)output->band[1], output->pitch,
						 roi, buffer, input->quantization, line_buffer);
#endif

	// The inverse spatial transform produces sixteen bit pixels
	output->pixel_type[1] = PIXEL_TYPE_16S;

	// Adjust for the shift in the reconstructed image due to filtering
	//output->lowpass_offset = 0;		//inverse_offset + INVERSE_LOWPASS_SHIFT;

	// Adjust the scale factor for display to account for inverse filtering
	//output->scale[0] = input->scale[0] / 4;
}


#if 0

// Compute temporal-horizontal wavelet transform of two image fields
IMAGE *TransformForwardField(IMAGE *field, int even_band, int odd_band, PIXEL *buffer)
{
	bool bands_used[IMAGE_NUM_BANDS];
	IMAGE *wavelet;
	int width, height, level;
	int temporal_lowpass_band;
	int temporal_highpass_band;

	assert(field != NULL);
	if (field == NULL) return NULL;


	/***** Temporal Transform *****/

	// Allocate the two lower bands to receive the temporal transform
	memset(bands_used, 0, sizeof(bands_used));
	bands_used[even_band] = true;
	bands_used[odd_band] = true;

	// Find an unused band for the temporal lowpass band
	temporal_lowpass_band = FindUnusedBand(bands_used);
	assert(temporal_lowpass_band > 0);

	// Find an unused band for the temporal highpass band
	temporal_highpass_band = FindUnusedBand(bands_used);
	assert(temporal_highpass_band > 0);

	// Return if could not find two unused bands
	if (temporal_lowpass_band < 0 || temporal_highpass_band < 0)
		return NULL;

	// Allocate space for the temporal bands
	AllocateBand(field, temporal_lowpass_band);
	AllocateBand(field, temporal_highpass_band);

	// Apply the temporal transform to the image fields
	TransformForwardTemporal(field, even_band,					// Even input field
							 field, odd_band,					// Odd input field
							 field, temporal_lowpass_band,		// Temporal lowpass result
							 field, temporal_highpass_band);	// Temporal highpass result


	/***** Horizontal Transform *****/

	// Allocate a wavelet for the spatial transform
	width = field->width / 2;
	height = field->height;

	// The horizontal-temporal transform is the first pyramid level
	level = 1;

	wavelet = CreateWavelet(width, height, level);
	assert(wavelet != NULL);
	if (wavelet == NULL) return NULL;

	//wavelet->original = field;		// Remember the original image

	// Apply the horizontal transform to the lowpass temporal result
	TransformForwardHorizontal(field, temporal_lowpass_band,
							   wavelet, LL_BAND,
							   wavelet, LH_BAND);

	// Apply the horizontal transform to the highpass temporal result
	TransformForwardHorizontal(field, temporal_highpass_band,
							   wavelet, HL_BAND,
							   wavelet, HH_BAND);

	// Return the wavelet image containing the spatio-temporal transform
	return wavelet;
}
#endif


// Apply the temporal-horizontal wavelet transform to an interlaced frame
void TransformForwardFrame(IMAGE *frame, IMAGE *wavelet, PIXEL *buffer, size_t buffer_size,
						   int offset, int quantization[4])
{
	int frame_width = frame->width;
	int frame_height = frame->height;
	ROI roi = {frame_width, frame_height};
	//int frame_pitch = frame->pitch;
	//PIXEL *even_field = frame->band[0];
	//PIXEL *odd_field = even_field + frame_pitch/sizeof(PIXEL);
	//int field_pitch = 2 * frame->pitch;
	//int scale = frame->scale[0];
	//int temporal_lowpass_area = 2;
	//int horizontal_lowpass_area = 2;

	assert(frame != NULL);
	if (frame == NULL) return;

	// Since the frame transform performs both temporal and horizontal filtering
	// the time spent in both transforms will be counted with a separate timer
	START(tk_frame);

	// Perform the temporal and horizontal transforms
	switch (frame->pixel_type[0])
	{
	case PIXEL_TYPE_16S:
		// Perform the frame transform and quantize the highpass bands
		FilterFrameQuant16s(frame->band[0], frame->pitch,
							wavelet->band[LL_BAND], wavelet->pitch,
							wavelet->band[LH_BAND], wavelet->pitch,
							wavelet->band[HL_BAND], wavelet->pitch,
							wavelet->band[HH_BAND], wavelet->pitch,
							roi, frame->scale[0], buffer, buffer_size,
							offset, quantization);
		break;

	case PIXEL_TYPE_8U:

		// Okay to use this transform when runs are disabled
		FilterFrameRuns8u((PIXEL8U *)(frame->band[0]), frame->pitch,
					  wavelet->band[LL_BAND], wavelet->pitch,
					  wavelet->band[LH_BAND], wavelet->pitch,
					  wavelet->band[HL_BAND], wavelet->pitch,
					  wavelet->band[HH_BAND], wavelet->pitch,
					  roi, frame->scale[0], buffer, buffer_size,
					  offset, quantization, NULL);
		break;

	default:
		assert(0);
		break;
	}

	// Set the pixel type for the lowpass and highpass results
#if _HIGHPASS_8S
	wavelet->pixel_type[LL_BAND] = PIXEL_TYPE_16S;
	wavelet->pixel_type[LH_BAND] = PIXEL_TYPE_8S;
	wavelet->pixel_type[HL_BAND] = PIXEL_TYPE_8S;
	wavelet->pixel_type[HH_BAND] = PIXEL_TYPE_8S;
#else
	wavelet->pixel_type[LL_BAND] = PIXEL_TYPE_16S;
	wavelet->pixel_type[LH_BAND] = PIXEL_TYPE_16S;
	wavelet->pixel_type[HL_BAND] = PIXEL_TYPE_16S;
	wavelet->pixel_type[HH_BAND] = PIXEL_TYPE_16S;
#endif

	// Record any quantization that was applied after filtering
	if (quantization != NULL) {
		int k;
		for (k = 0; k < IMAGE_NUM_BANDS; k++)
			wavelet->quantization[k] = quantization[k];
	}
	else {
		int k;
		for (k = 0; k < IMAGE_NUM_BANDS; k++)
			wavelet->quantization[k] = 1;
	}

	STOP(tk_frame);

}


// Apply the forward horizontal-temporal transform to a packed frame of YUV data
void TransformForwardFrameYUV(uint8_t *input, int input_pitch, FRAME_INFO *frame,
							  TRANSFORM *transform[], int frame_index, int num_channels,
							  char *buffer, size_t buffer_size, int chroma_offset,
							  int precision, int limit_yuv, int conv601_709)
{
	// Pointers to the even and odd rows of packed pixels
	uint8_t *even_row_ptr = input;
	uint8_t *odd_row_ptr = input + input_pitch;

	// For allocating buffer space
	char *bufptr = buffer;

	// Pointers to the rows in the temporal wavelet for each channel
	PIXEL *temporal_lowpass;
	PIXEL *temporal_highpass;

	// Pointers to the rows in the horizontal wavelet for each channel
	PIXEL *horizontal_lowlow[TRANSFORM_MAX_CHANNELS];

#if _HIGHPASS_8S
	PIXEL8S *horizontal_lowhigh[TRANSFORM_MAX_CHANNELS];
	PIXEL8S *horizontal_highlow[TRANSFORM_MAX_CHANNELS];
	PIXEL8S *horizontal_highhigh[TRANSFORM_MAX_CHANNELS];
#else
	PIXEL *horizontal_lowhigh[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highlow[TRANSFORM_MAX_CHANNELS];
	PIXEL *horizontal_highhigh[TRANSFORM_MAX_CHANNELS];
#endif

	// Buffer for the horizontal highpass coefficients
	PIXEL *lowhigh_row_buffer;
	PIXEL *highlow_row_buffer;
	PIXEL *highhigh_row_buffer;

	// Length of each temporal row
	int temporal_width[TRANSFORM_MAX_CHANNELS];

	// Horizontal wavelet band width and pitch
	int horizontal_width[TRANSFORM_MAX_CHANNELS];
	int horizontal_pitch[TRANSFORM_MAX_CHANNELS];
	//int horizontal_pitch8s[TRANSFORM_MAX_CHANNELS];

	// Quantization factors
	int lowlow_quantization[TRANSFORM_MAX_CHANNELS];
	int lowhigh_quantization[TRANSFORM_MAX_CHANNELS];
	int highlow_quantization[TRANSFORM_MAX_CHANNELS];
	int highhigh_quantization[TRANSFORM_MAX_CHANNELS];

	// Scale factors for the frame transform
	//int lowlow_scale = 0;
	//int lowhigh_scale = 0;
	int highlow_scale = 0;
	int highhigh_scale = 0;

	// Dimensions of the frame
	int frame_width = frame->width;
	int frame_height = frame->height;
	int frame_format = frame->format;
	//int half_height = frame_height / 2;
	int half_width = frame_width/2;
	int field_pitch = 2 * input_pitch;
	size_t temporal_row_size;
	size_t horizontal_row_size;
	size_t total_buffer_size;
	//int horizontal_row_length;
	int frame_row_length;
	int channel;
	int row;

	// Check that the frame format is supported
	assert((frame_format&0xffff) == COLOR_FORMAT_YUYV || (frame_format&0xffff) == COLOR_FORMAT_UYVY);

	// Check that the number of channels is reasonable
	assert(0 < num_channels && num_channels <= TRANSFORM_MAX_CHANNELS);

	// Check that the buffer starts on a cache line boundary
	assert(ISALIGNED(buffer, _CACHE_LINE_SIZE));

	// Since the frame transform performs both temporal and horizontal filtering
	// the time spent in both transforms will be counted with a separate timer
	START(tk_frame);

	// Round up the frame width to an integer number of cache lines
	frame_row_length = frame_width * 2;
	frame_row_length = ALIGN(frame_row_length, _CACHE_LINE_SIZE);
	frame_row_length /= 2;

	// Compute the size of the largest temporal output row
	temporal_row_size = frame_row_length * sizeof(PIXEL);

	// Round up the temporal row size to an integer number of cache lines
	temporal_row_size = ALIGN(temporal_row_size, _CACHE_LINE_SIZE);

	// Round up the horizontal row length to a multiple of 16 coefficients
	//horizontal_row_length = ALIGN16(half_width);

	// Compute the size of the largest horizontal output row
	horizontal_row_size = half_width * sizeof(PIXEL);

	// Round up the horizontal row size to an integer number of cache lines
	horizontal_row_size = ALIGN(horizontal_row_size, _CACHE_LINE_SIZE);

	// Check that the buffer is large enough
	total_buffer_size = 2 * temporal_row_size + 3 * horizontal_row_size;
	assert(buffer_size >= total_buffer_size);

	// Allocate buffers for a single row of lowpass and highpass temporal coefficients
	// and initialize the arrays of row pointers into the horizontal transform bands
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		// Initialize the row pointers into the horizontal bands
		horizontal_lowlow[channel] = wavelet->band[LL_BAND];
#if _HIGHPASS_8S
		horizontal_lowhigh[channel] = (PIXEL8S *)wavelet->band[LH_BAND];
		horizontal_highlow[channel] = (PIXEL8S *)wavelet->band[HL_BAND];
		horizontal_highhigh[channel] = (PIXEL8S *)wavelet->band[HH_BAND];
#else
		horizontal_lowhigh[channel] = (PIXEL *)wavelet->band[LH_BAND];
		horizontal_highlow[channel] = (PIXEL *)wavelet->band[HL_BAND];
		horizontal_highhigh[channel] = (PIXEL *)wavelet->band[HH_BAND];
#endif
		lowlow_quantization[channel] = wavelet->quant[LL_BAND];
		lowhigh_quantization[channel] = wavelet->quant[LH_BAND];
		highlow_quantization[channel] = wavelet->quant[HL_BAND];
		highhigh_quantization[channel] = wavelet->quant[HH_BAND];

		// Compute the width of the temporal rows for this channel
		temporal_width[channel] = (channel == 0) ? frame_width : half_width;

		// Round the width up to an integer number of cache lines
		//temporal_width[channel] = ALIGN(temporal_width[channel], _CACHE_LINE_SIZE);

		// Compute the pitch in units of pixels
		//horizontal_pitch[channel] = wavelet->pitch/sizeof(PIXEL);

		// Keep the pitch in units of bytes
		horizontal_pitch[channel] = wavelet->pitch;

		// Remember the width of the horizontal wavelet rows for this channel
		horizontal_width[channel] = wavelet->width;

		// Divide the buffer into temporal lowpass and highpass rows
		temporal_lowpass = (PIXEL *)bufptr;		bufptr += temporal_row_size;
		temporal_highpass = (PIXEL *)bufptr;	bufptr += temporal_row_size;
	}

	// Allocate buffer space for the horizontal highpass coefficients
	lowhigh_row_buffer = (PIXEL *)bufptr;		bufptr += horizontal_row_size;
	highlow_row_buffer = (PIXEL *)bufptr;		bufptr += horizontal_row_size;
	highhigh_row_buffer = (PIXEL *)bufptr;		bufptr += horizontal_row_size;

	// Apply the temporal transform to the even and odd rows each iteration of the loop
	for (row = 0; row < frame_height; row += 2)
	{
		// Apply the temporal and horizontal transforms to each channel
		for (channel = 0; channel < num_channels; channel++)
		{
			int offset = (channel == 0) ? 0 : chroma_offset;

			if ((frame_format&0xffff) == COLOR_FORMAT_YUYV) {
				// Apply the temporal transform to one channel in the even and odd rows
				FilterTemporalRowYUYVChannelTo16s(even_row_ptr, odd_row_ptr, frame_row_length, channel,
												  temporal_lowpass, temporal_highpass, offset, precision, limit_yuv);
			}
			else {
				// Frame color format must be UYUV
				assert((frame_format&0xffff) == COLOR_FORMAT_UYVY);

				// Apply the temporal transform to one channel in the even and odd rows
				FilterTemporalRowUYVYChannelTo16s(even_row_ptr, odd_row_ptr, frame_row_length, channel,
												  temporal_lowpass, temporal_highpass, offset, precision, limit_yuv);
			}


			// Apply the horizontal transform to the temporal lowpass
			//DAN20051004 -- possible reversibility issue
	//		FilterHorizontalRowScaled16s(temporal_lowpass, horizontal_lowlow[channel], lowhigh_row_buffer,
	//									 temporal_width[channel], lowlow_scale, lowhigh_scale);
			//DAN20051004 -- fix?
			FilterHorizontalRow16s(temporal_lowpass, horizontal_lowlow[channel], lowhigh_row_buffer,
										 temporal_width[channel]);

			// Quantize and pack the rows of highpass coefficients
			#if _HIGHPASS_8S
			QuantizeRow16sTo8s(lowhigh_row_buffer, horizontal_lowhigh[channel], horizontal_width[channel],
							   lowhigh_quantization[channel]);
			#else
			QuantizeRow16sTo16s(lowhigh_row_buffer, horizontal_lowhigh[channel], horizontal_width[channel], lowhigh_quantization[channel]);
			 #if _PACK_RUNS_IN_BAND_16S
				horizontal_lowhigh[channel] += PackRuns16s(horizontal_lowhigh[channel], horizontal_width[channel]);
			 #endif
			#endif


			// Apply the horizontal transform to the temporal highpass
			#if DIFFERENCE_CODING  // for interlaced data use the new differencing transfrom
			{// test DifferenceFiltering opf the interlace LH band.
		//		FilterHorizontalRowScaled16sDifferenceFiltered(temporal_highpass, highlow_row_buffer, highhigh_row_buffer,
		//									 temporal_width[channel], highlow_scale, highhigh_scale);
				//DAN20051004 -- possible reversibility issue
				FilterHorizontalRowScaled16sDifferenceFiltered(temporal_highpass, horizontal_highlow[channel], highhigh_row_buffer,
											 temporal_width[channel], highlow_scale, highhigh_scale,  highlow_quantization[channel]);
			//	FilterHorizontalRow16sDifferenceFiltered(temporal_highpass, horizontal_highlow[channel], highhigh_row_buffer,
			//								 temporal_width[channel], highlow_quantization[channel]);
				// Quantize and pack the rows of highpass coefficients
				#if _HIGHPASS_8S
				QuantizeRow16sTo8s(highhigh_row_buffer, horizontal_highhigh[channel], horizontal_width[channel],
								   highhigh_quantization[channel]);
				#else

				QuantizeRow16sTo16s(highhigh_row_buffer, horizontal_highhigh[channel], horizontal_width[channel], highhigh_quantization[channel]);
				 #if _PACK_RUNS_IN_BAND_16S
					horizontal_highhigh[channel] += PackRuns16s(horizontal_highhigh[channel], horizontal_width[channel]);
				 #endif
				#endif
			}
			#else
			{	//DAN20051004 -- possible reversibility issue
				//FilterHorizontalRowScaled16s(temporal_highpass, highlow_row_buffer, highhigh_row_buffer,
				//							 temporal_width[channel], highlow_scale, highhigh_scale);
				//DAN20051004 -- fix?
				FilterHorizontalRow16s(temporal_highpass, highlow_row_buffer, highhigh_row_buffer,
											 temporal_width[channel]);

				// Quantize and pack the rows of highpass coefficients
				#if _HIGHPASS_8S
				QuantizeRow16sTo8s(highlow_row_buffer, horizontal_highlow[channel], horizontal_width[channel],
								   highlow_quantization[channel]);
				QuantizeRow16sTo8s(highhigh_row_buffer, horizontal_highhigh[channel], horizontal_width[channel],
								   highhigh_quantization[channel]);
				#else

				QuantizeRow16sTo16s(highlow_row_buffer, horizontal_highlow[channel], horizontal_width[channel], highlow_quantization[channel]);
				 #if _PACK_RUNS_IN_BAND_16S
					horizontal_highlow[channel] += PackRuns16s(horizontal_highlow[channel], horizontal_width[channel]);
				 #endif

				QuantizeRow16sTo16s(highhigh_row_buffer, horizontal_highhigh[channel], horizontal_width[channel], highhigh_quantization[channel]);
				 #if _PACK_RUNS_IN_BAND_16S
					horizontal_highhigh[channel] += PackRuns16s(horizontal_highhigh[channel], horizontal_width[channel]);
				 #endif
				#endif
			}
			#endif

			// Advance to the next row in each highpass band
#if _HIGHPASS_8S
			horizontal_lowhigh[channel] += horizontal_pitch[channel];
			horizontal_highlow[channel] += horizontal_pitch[channel];
			horizontal_highhigh[channel] += horizontal_pitch[channel];
#else
  #if !_PACK_RUNS_IN_BAND_16S
			horizontal_lowhigh[channel] += horizontal_pitch[channel] / sizeof(PIXEL);
			horizontal_highlow[channel] += horizontal_pitch[channel] / sizeof(PIXEL);
			horizontal_highhigh[channel] += horizontal_pitch[channel] / sizeof(PIXEL);
  #endif
#endif
			// Advance to the next row in the lowpass band
			horizontal_lowlow[channel] += horizontal_pitch[channel] / sizeof(PIXEL);
		}

		// Advance to the next row in each input field
		even_row_ptr += field_pitch;
		odd_row_ptr += field_pitch;
	}

	// Record the pixel type in each band
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];

		wavelet->pixel_type[LL_BAND] = PIXEL_TYPE_16S;

#if _HIGHPASS_8S
		wavelet->pixel_type[LH_BAND] = PIXEL_TYPE_8S;
		wavelet->pixel_type[HL_BAND] = PIXEL_TYPE_8S;
		wavelet->pixel_type[HH_BAND] = PIXEL_TYPE_8S;
#else
		wavelet->pixel_type[LH_BAND] = PIXEL_TYPE_16S;
		wavelet->pixel_type[HL_BAND] = PIXEL_TYPE_16S;
		wavelet->pixel_type[HH_BAND] = PIXEL_TYPE_16S;
#endif

		wavelet->num_runs[LL_BAND] = 0;
		wavelet->num_runs[LH_BAND] = 0;
		wavelet->num_runs[HL_BAND] = 0;
		wavelet->num_runs[HH_BAND] = 0;
	}

	// Record the quantization that was applied to each wavelet band
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int band;

		for (band = 0; band < wavelet->num_bands; band++)
		{
			wavelet->quantization[band] = wavelet->quant[band];
		}
	}

	STOP(tk_frame);
}


#if 0
void TransformGroupFields(TRANSFORM *transform,
						  IMAGE **group, int group_length,
						  int num_spatial, PIXEL *buffer)
{
	IMAGE *temporal;	// Temporal wavelet (two bands)
	IMAGE *wavelet;		// Spatio-temporal wavelet

	int even_band = BAND_INDEX_FIELD_EVEN;
	int odd_band = BAND_INDEX_FIELD_ODD;

	int width, height, level;
	int last_level;
	int i;

	// Initially we only handle a group length of two
	//assert(group_length == 2);

	// Cannot exceed the maximum number of frames
	assert(group_length <= WAVELET_MAX_FRAMES);

	transform->type = TRANSFORM_TYPE_FIELD;
	transform->num_frames = group_length;
	transform->num_spatial = num_spatial;

	// Indicate that no transforms have been computed
	transform->num_levels = 0;

	width = group[0]->width;
	height = group[0]->height;
	level = 0;

	// Allocate a two band temporal wavelet for the intermediate results
	temporal = CreateWaveletEx(width, height, level, WAVELET_TYPE_TEMPORAL);

	for (i = 0; i < group_length; i++)
	{
		/***** Temporal Transform *****/

		int temporal_lowpass_band = 0;
		int temporal_highpass_band = 1;

		IMAGE *frame = group[i];	// Frame with two fields

		// Apply the temporal transform to the image fields
		TransformForwardTemporal(frame, even_band,					// Even input field
								 frame, odd_band,					// Odd input field
								 temporal, temporal_lowpass_band,	// Temporal lowpass result
								 temporal, temporal_highpass_band);	// Temporal highpass result


		/***** Horizontal Transform *****/

		// Allocate a wavelet for the spatial-temporal transform
		width = frame->width / 2;
		height = frame->height;

		// The horizontal-temporal transform is the first pyramid level
		level = 1;

		wavelet = CreateWaveletEx(width, height, level, WAVELET_TYPE_HORZTEMP);
		assert(wavelet != NULL);
		if (wavelet == NULL) return;

		//wavelet->original = frame;		// Remember the original image

		// Apply the horizontal filter to the lowpass temporal result
		TransformForwardHorizontal(temporal, temporal_lowpass_band,
								   wavelet, LL_BAND,
								   wavelet, LH_BAND);

		// Apply the horizontal filter to the highpass temporal result
		TransformForwardHorizontal(temporal, temporal_highpass_band,
								   wavelet, HL_BAND,
								   wavelet, HH_BAND);

		// Save the spatio-temporal wavelet in the transform results
		transform->wavelet[i] = wavelet;
	}

	// Free the intermediate temporal wavelet image
#if _ALLOCATOR
	DeleteImage(allocator, temporal);
#else
	DeleteImage(temporal);
#endif


	/***** Temporal Transform *****/

	// Compute a temporal wavelet between the two spatio-temporal wavelets
	level = 2;
#if _ALLOCATOR
	temporal = CreateWaveletEx(allocator, width, height, level, WAVELET_TYPE_TEMPORAL);
#else
	temporal = CreateWaveletEx(width, height, level, WAVELET_TYPE_TEMPORAL);
#endif

	TransformForwardTemporal(transform->wavelet[0], 0, transform->wavelet[1], 0, temporal, 0, temporal, 1);
	transform->wavelet[2] = temporal;


	/***** Spatial Transforms *****/

	last_level = level + num_spatial;
	while (level < last_level)
	{
		// No prescaling used in this routine
		int prescale = 0;

		// Compute the spatial wavelet transform
		int next_level = level + 1;
		assert(next_level < sizeof(transform->wavelet)/sizeof(transform->wavelet[0]));

		wavelet = TransformForwardSpatial(transform->wavelet[level], 0, transform->wavelet[next_level],
										  next_level, NULL, 0, prescale, NULL, 0);
		if (wavelet == NULL) break;

		// Save the wavelet as the next level in the pyramid
		transform->wavelet[next_level] = wavelet;

		// Advance to the next level in the pyramid
		level = next_level;
	}

	// Record the number levels in the wavelet pyramid
	transform->num_levels = level;

	// Record the number of wavelets
	transform->num_wavelets = level + 1;
}
#endif

#if 0
void TransformFrames(TRANSFORM *transform,
					 FRAME *frame[], int group_length,
					 int num_spatial, PIXEL *buffer)
{
	// Convert the frame images to field images
	IMAGE *field1 = CreateFieldImageFromFrame(frame[0]->channel[0]);
	IMAGE *field2 = CreateFieldImageFromFrame(frame[1]->channel[0]);

	// Create a two frame (four field) group
	IMAGE *gop[] = {field1, field2};

	// Code has only been implemented for GOP length of two frames
	assert(group_length == 2);

	// Apply the spatio-temporal transform to the group
	TransformGroupFields(transform, gop, group_length, num_spatial, buffer);
}
#endif

#if 0
void TransformGroupFrames(FRAME **group, int group_length,
						  TRANSFORM *transform[], int num_transforms,
						  int num_spatial, PIXEL *buffer, size_t buffer_size)
{
	int channel;

	for (channel = 0; channel < num_transforms; channel++)
		TransformGroupChannel(group, group_length, channel,
							  transform[channel], num_spatial,
							  buffer, buffer_size);
}
#endif


#if 0

#define LOWPASS_BORDER	0
#define HIGHPASS_BORDER	2

// Compute the wavelet transform for the specified channel in the group of frames
void TransformGroupChannel(FRAME **group, int group_length, int channel,
						   TRANSFORM *transform, int num_spatial,
						   PIXEL *buffer, size_t buffer_size)
{
	IMAGE *temporal;	// Temporal wavelet (two bands)
	IMAGE *wavelet;		// Spatio-temporal wavelet
	size_t size = transform->size;
	int even_band = BAND_INDEX_FIELD_EVEN;
	int odd_band = BAND_INDEX_FIELD_ODD;
	int background = ((channel == 0) ? COLOR_LUMA_BLACK : COLOR_CHROMA_ZERO);
	int width, height, level;
	int last_level;
	int wavelet_index = 0;
	int offset = 0;				// Chroma offset
	int k = channel;			// Index to the channel within the frame
	int i;						// Index to the frame within the group

	// Only handle a group length of two
	assert(group_length == 2);

	// Cannot exceed the maximum number of frames
	assert(group_length <= WAVELET_MAX_FRAMES);

#if 0
	transform->type = TRANSFORM_TYPE_FIELD;
	transform->num_frames = group_length;
	transform->num_spatial = num_spatial;

	// Indicate that no transforms have been computed
	transform->num_levels = 0;
#endif
#if 1
	// Allocate a buffer for image processing (if necessary)
	if (transform->buffer == NULL) {
		IMAGE *frame = group[0]->channel[k];
		assert(frame != NULL);
		size = frame->height * frame->pitch;
		transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
		assert(transform->buffer != NULL);
		transform->size = size;
	}
#endif

	//memset(transform->buffer, background, size);

	width = group[0]->channel[k]->width;
	height = group[0]->channel[k]->height;

	// Allocate the transform data structures if not already allocated
	AllocTransform(allocator, transform, TRANSFORM_TYPE_FIELD,
				   width, height, group_length, num_spatial);
	level = 0;

	for (i = 0; i < group_length; i++)
	{
		IMAGE *frame = group[i]->channel[k];	// Frame with two fields
#if 0
		int frame_width = frame->width;
		int frame_height = frame->height;
		ROI roi = {frame_width, frame_height};
		int frame_pitch = frame->pitch;
		PIXEL *even_field = frame->band[0];
		PIXEL *odd_field = even_field + frame_pitch/sizeof(PIXEL);
		int field_pitch = 2 * frame->pitch;
		int lowpass_area = 2;


		/***** Temporal Transform *****/

		// Apply the temporal transform to the even and odd fields of the frame
		// using in place computation so the frame data will be overwritten
		TransformForwardInterlaced(frame);


		/***** Horizontal Transform *****/

		START(tk_horizontal);

		// The horizontal-temporal transform is the first pyramid level
		level = 1;
#if 0
		// Allocate a wavelet for the spatial-temporal transform
		width = frame_width / 2;
		height = frame_height / 2;

		wavelet = CreateWaveletEx(width, height, level, WAVELET_TYPE_HORZTEMP);
		assert(wavelet != NULL);
		if (wavelet == NULL) return;
#else
		// Use the preallocated wavelet
		wavelet = transform->wavelet[wavelet_index++];
#endif
		wavelet->original = NULL;		// The original image was overwritten

		// The region of interest is half as high due to the temporal transform
		roi.height /= 2;

		// Apply the lowpass horizontal filter to the lowpass temporal result
		FilterLowpassHorizontal(even_field, field_pitch, wavelet->band[LL_BAND], wavelet->pitch, roi);

		// Apply the highpass horizontal filter to the lowpass temporal result
		FilterHighpassHorizontal(even_field, field_pitch, wavelet->band[LH_BAND], wavelet->pitch,
								 roi, frame->scale[0]);

		// Apply the lowpass horizontal filter to the highpass temporal result
		FilterLowpassHorizontal(odd_field, field_pitch, wavelet->band[HL_BAND], wavelet->pitch, roi);

		// Apply the highpass horizontal filter to the highpass temporal result
		FilterHighpassHorizontal(odd_field, field_pitch, wavelet->band[HH_BAND], wavelet->pitch,
								 roi, frame->scale[1]);
#if 0
		// Calculate the scale factors for the output bands
		wavelet->scale[0] = lowpass_area * frame->scale[0];
		wavelet->scale[1] = frame->scale[0];
		wavelet->scale[2] = lowpass_area * frame->scale[1];
		wavelet->scale[3] = frame->scale[1];
#endif

		//wavelet->lowpass_border = LOWPASS_BORDER;
		//wavelet->highpass_border = HIGHPASS_BORDER;
#if 0
		// Save the spatio-temporal wavelet in the transform results
		transform->wavelet[i] = wavelet;
#endif
		STOP(tk_horizontal);
#else
		// Use the preallocated wavelet
		wavelet = transform->wavelet[wavelet_index++];

		// Apply the temporal-horizontal transform to the frame using inplace computation
		TransformForwardFrame(frame, wavelet, buffer, buffer_size, offset, NULL);
#endif
	}


	/***** Temporal Transform *****/

	// Compute a temporal wavelet between the two spatio-temporal wavelets
	level = 2;

#if 0
	temporal = CreateWaveletEx(width, height, level, WAVELET_TYPE_TEMPORAL);
#else
	temporal = transform->wavelet[wavelet_index++];
#endif

	TransformForwardTemporal(transform->wavelet[0], 0, transform->wavelet[1], 0, temporal, 0, temporal, 1);

#if 0
	transform->wavelet[2] = temporal;
#endif


	/***** Spatial Transforms *****/

	last_level = level + num_spatial;
	while (level < last_level)
	{
		// Prescale before applying the spatial transform to the lowpass band
		int prescale = (channel == 0) ? PRESCALE_LUMA : PRESCALE_CHROMA;

		// Compute the spatial wavelet transform
		int next_level = level + 1;
		assert(next_level < sizeof(transform->wavelet)/sizeof(transform->wavelet[0]));

		wavelet = TransformForwardSpatial(transform->wavelet[level], 0, transform->wavelet[next_level],
										  next_level, transform->buffer, transform->size, prescale, NULL, 0);
		if (wavelet == NULL) break;
#if 0
		// Save the wavelet as the next level in the pyramid
		transform->wavelet[next_level] = wavelet;
#endif
		// Advance to the next level in the pyramid
		level = next_level;
	}

	// Record the number levels in the wavelet pyramid
	transform->num_levels = level;

	// Record the number of wavelets
	transform->num_wavelets = level + 1;
}
#endif

// Compute the upper levels of the wavelet transform for a group of frames
#if _ALLOCATOR
void ComputeGroupTransform(ALLOCATOR *allocator,
						   TRANSFORM *transform[], int num_transforms,
						   int group_length, int num_spatial, int precision)
#else
void ComputeGroupTransform(TRANSFORM *transform[], int num_transforms,
						   int group_length, int num_spatial, int precision)
#endif
{
	int channel;

	for (channel = 0; channel < num_transforms; channel++)
	{
		// Prescale before applying the spatial transform to the lowpass band
		//int prescale = (channel == 0) ? PRESCALE_LUMA : PRESCALE_CHROMA;
		int prescale = (precision == CODEC_PRECISION_DEFAULT) ? 0 : 2;

		assert(transform[channel]->type == TRANSFORM_TYPE_FIELDPLUS ||
			   transform[channel]->type == TRANSFORM_TYPE_FIELD);

		// Compute the temporal and spatial wavelets to finish the transform
		if (transform[channel]->type == TRANSFORM_TYPE_FIELDPLUS)
		{
#if _ALLOCATOR
			FinishFieldPlusTransform(allocator, transform[channel], group_length, num_spatial, prescale);
#else
			FinishFieldPlusTransform(transform[channel], group_length, num_spatial, prescale);
#endif
		}
		else if (transform[channel]->type == TRANSFORM_TYPE_FIELD)
		{
#if _ALLOCATOR
			FinishFieldTransform(allocator, transform[channel], group_length, num_spatial);
#else
			//FinishFieldTransform(transform[channel], group_length, num_spatial, prescale);
			FinishFieldTransform(transform[channel], group_length, num_spatial);
#endif
		}
		else
		{
			// Other transforms not yet defined
			assert(0);
		}
	}
}

// Finish the wavelet transform for the group of frames
#if _ALLOCATOR
void FinishFieldTransform(ALLOCATOR *allocator, TRANSFORM *transform, int group_length, int num_spatial)
#else
//void FinishFieldTransform(TRANSFORM *transform, int group_length, int num_spatial, int prescale)
void FinishFieldTransform(TRANSFORM *transform, int group_length, int num_spatial)
#endif
{
	IMAGE *temporal;	// Temporal wavelet (two bands)
	IMAGE *wavelet;		// Spatio-temporal wavelet
	size_t size = transform->size;
	//int background = ((channel == 0) ? COLOR_LUMA_BLACK : COLOR_CHROMA_ZERO);
	int level;
	int last_level;
	int wavelet_index = 0;

	int prescale = 0;

	// Can only handle a group length of two
	assert(group_length == 2);

	// Cannot exceed the maximum number of frames
	assert(group_length <= WAVELET_MAX_FRAMES);

#if 0
	// Allocate a buffer for image processing (if necessary)
	if (transform->buffer == NULL) {
		IMAGE *wavelet = transform->wavelet[0];
		assert(wavelet != NULL);
		size = wavelet->height * wavelet->pitch;
		transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
		assert(transform->buffer != NULL);
		transform->size = size;
	}
#elif 1
	// Allocate a buffer as large as the original frame (if necessary)
	if (transform->buffer == NULL) {
		int width = transform->height;
		int height = transform->width;
		int pitch = width * sizeof(PIXEL);
		size = height * ALIGN16(pitch);
		transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
		assert(transform->buffer != NULL);
		transform->size = size;
	}
#endif

	// Have already computed the frame transforms at the base of the wavelet pyramid
	wavelet_index += group_length;


	/***** Temporal Transform *****/

	// Compute a temporal wavelet between the two frame (temporal-horizontal) wavelets
	level = 2;
	temporal = transform->wavelet[wavelet_index++];
	TransformForwardTemporal(transform->wavelet[0], 0, transform->wavelet[1], 0, temporal, 0, temporal, 1);

	/***** Spatial Transforms *****/

	last_level = level + num_spatial;
	while (level < last_level)
	{
		// Compute the spatial wavelet transform
		int next_level = level + 1;
		assert((size_t)next_level < sizeof(transform->wavelet)/sizeof(transform->wavelet[0]));

#if _ALLOCATOR
		wavelet = TransformForwardSpatial(allocator,
										  transform->wavelet[level], 0, transform->wavelet[next_level],
										  next_level, transform->buffer, transform->size, prescale, NULL, 0);
#else
		wavelet = TransformForwardSpatial(transform->wavelet[level], 0, transform->wavelet[next_level],
										  next_level, transform->buffer, transform->size, prescale, NULL, 0);
#endif
		if (wavelet == NULL) break;

		// Advance to the next level in the pyramid
		level = next_level;
	}

	// Record the number levels in the wavelet pyramid
	transform->num_levels = level;

	// Record the number of wavelets
	transform->num_wavelets = level + 1;

#if (0 && DEBUG && _WIN32)
	_CrtCheckMemory();
#endif
}

// Finish the wavelet transform for the group of frames
#if _ALLOCATOR
void FinishFieldPlusTransform(ALLOCATOR *allocator, TRANSFORM *transform,
							  int group_length, int num_spatial, int prescale)
#else
void FinishFieldPlusTransform(TRANSFORM *transform, int group_length, int num_spatial, int prescale)
#endif
{
	IMAGE *temporal;	// Temporal wavelet (two bands)
	IMAGE *wavelet;		// Spatio-temporal wavelet
	size_t size = transform->size;
	//int background = ((channel == 0) ? COLOR_LUMA_BLACK : COLOR_CHROMA_ZERO);
	int level;
	int wavelet_index = 0;

	// Apply prescaling only to the last spatial transform
	int last_spatial_prescale = prescale;
	prescale = 0;

	// Can only handle a group length of two
	assert(group_length == 2);

	// Cannot exceed the maximum number of frames
	assert(group_length <= WAVELET_MAX_FRAMES);

#if 1
	// Allocate a buffer for image processing (if necessary)
	if (transform->buffer == NULL) {
		IMAGE *wavelet = transform->wavelet[0];
		assert(wavelet != NULL);
		size = wavelet->height * wavelet->pitch;
		transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
		assert(transform->buffer != NULL);
		transform->size = size;
	}
#endif

	// Have already computed the frame transforms at the base of the wavelet pyramid
	wavelet_index += group_length;


	/***** Perform the temporal transform between frames *****/

	// Compute a temporal wavelet between the two frame (temporal-horizontal) wavelets
	level = 2;
	temporal = transform->wavelet[wavelet_index];
	TransformForwardTemporal(transform->wavelet[0], 0, transform->wavelet[1], 0, temporal, 0, temporal, 1);


	/***** Apply spatial transforms to the temporal highpass band *****/

	assert(num_spatial == 3);

	assert((size_t)(level + 1) < sizeof(transform->wavelet)/sizeof(transform->wavelet[0]));

	// Compute the spatial wavelet transform for the temporal highpass band
#if _ALLOCATOR
	wavelet = TransformForwardSpatial(allocator, transform->wavelet[wavelet_index],
									  1, transform->wavelet[wavelet_index+1],
									  level+1, transform->buffer, transform->size, 0/*prescale*/, NULL, 0);
#else
	wavelet = TransformForwardSpatial(transform->wavelet[wavelet_index], 1, transform->wavelet[wavelet_index+1],
									  level+1, transform->buffer, transform->size, 0/*prescale*/, NULL, 0);
#endif
	if (wavelet == NULL) {
		// Record the number levels in the wavelet pyramid
		transform->num_levels = level;

		// Record the number of wavelets
		transform->num_wavelets = wavelet_index;
		return;
	}


	/***** Apply spatial transforms to the temporal lowpass band *****/

	// First spatial transform
#if _ALLOCATOR
	wavelet = TransformForwardSpatial(allocator, transform->wavelet[wavelet_index],
									  0, transform->wavelet[wavelet_index+2],
									  level+1, transform->buffer, transform->size, prescale, NULL, 0);
#else
	wavelet = TransformForwardSpatial(transform->wavelet[wavelet_index], 0, transform->wavelet[wavelet_index+2],
									  level+1, transform->buffer, transform->size, prescale, NULL, 0);
#endif
	if (wavelet == NULL) {
		// Record the number levels in the wavelet pyramid
		transform->num_levels = level;

		// Record the number of wavelets
		transform->num_wavelets = wavelet_index+1;
		return;
	}

	wavelet_index += 2;

	// Second spatial transform
	level++;
	assert((size_t)(level + 1) < sizeof(transform->wavelet)/sizeof(transform->wavelet[0]));

#if _ALLOCATOR
	wavelet = TransformForwardSpatial(allocator, transform->wavelet[wavelet_index],
									  0, transform->wavelet[wavelet_index+1],
									  level+1, transform->buffer, transform->size, last_spatial_prescale, NULL, 0);
#else
	wavelet = TransformForwardSpatial(transform->wavelet[wavelet_index], 0, transform->wavelet[wavelet_index+1],
									  level+1, transform->buffer, transform->size, last_spatial_prescale, NULL, 0);
#endif
	if (wavelet == NULL) {
		// Record the number levels in the wavelet pyramid
		transform->num_levels = level;

		// Record the number of wavelets
		transform->num_wavelets = wavelet_index;
		return;
	}

	level++;
	wavelet_index += 1;

//finish:
	// Record the number levels in the wavelet pyramid
	transform->num_levels = level;

	// Record the number of wavelets
	transform->num_wavelets = wavelet_index+1;
}

int FindUnusedBand(bool *band_in_use)
{
	int band;

	for (band = 0; band < IMAGE_NUM_BANDS; band++) {
		if (!band_in_use[band]) {
			band_in_use[band] = true;
			return band;
		}
	}

	return (-1);
}

void SetTransformScale(TRANSFORM *transform)
{
	int num_wavelets = transform->num_wavelets;
	int num_spatial = transform->num_spatial;

	int num_lowpass_spatial;	// Number of spatial transforms for lowpass temporal band
	int num_highpass_spatial;	// Number of spatial transforms for highpass temporal band
	int num_frame_wavelets;		// Number of frame wavelets at the base of the pyramid

	int temporal_lowpass_area = 2;
	int horizontal_lowpass_area = 2;
	int vertical_lowpass_area = 2;
	int spatial_lowpass_area = (horizontal_lowpass_area * vertical_lowpass_area);

	int temporal_lowpass_scale;
	int temporal_highpass_scale;

	IMAGE *wavelet;
	IMAGE *temporal;

	int k;
	int i;

	// Coefficients in each band are scaled by the forward wavelet filters
	//int scale[4] = {4, 1, 1, 1};
	int scale[4] = {1, 1, 1, 1};

	switch (transform->type)
	{
	case TRANSFORM_TYPE_SPATIAL:

		// Compute the number of frame and spatial wavelets
		num_frame_wavelets = 1;
		num_lowpass_spatial = num_spatial;

		// Compute the change in scale due to the filters used in the frame transform
		temporal_lowpass_scale = temporal_lowpass_area * scale[0];
		temporal_highpass_scale = scale[0];

		// Compute the scale factors for the first wavelet
		scale[0] = horizontal_lowpass_area * temporal_lowpass_scale;
		scale[1] = temporal_lowpass_scale;
		scale[2] = horizontal_lowpass_area * temporal_highpass_scale;
		scale[3] = temporal_highpass_scale;

		for (k = 0; k < num_frame_wavelets; k++)
		{
			wavelet = transform->wavelet[k];

			wavelet->scale[0] = scale[0];
			wavelet->scale[1] = scale[1];
			wavelet->scale[2] = scale[2];
			wavelet->scale[3] = scale[3];
		}

		// Compute the scale factors for the spatial wavelets
		for (i = 0; i < num_lowpass_spatial; i++)
		{
			IMAGE *spatial = transform->wavelet[k++];
			//int k;

			assert(spatial != NULL);

			// The lowpass band is the input to the spatial transform
			temporal_lowpass_scale = wavelet->scale[0];

			spatial->scale[0] = (spatial_lowpass_area * temporal_lowpass_scale);// >> _LOWPASS_PRESCALE;
			spatial->scale[1] = (vertical_lowpass_area * temporal_lowpass_scale);// >> _LOWPASS_PRESCALE;
			spatial->scale[2] = (horizontal_lowpass_area * temporal_lowpass_scale);// >> _LOWPASS_PRESCALE;
			spatial->scale[3] = (temporal_lowpass_scale);// >> _LOWPASS_PRESCALE;

#if (0 && DEBUG)
			// Force non-zero spatial scale to avoid assertions later in the code
			for (k = 0; k < IMAGE_NUM_BANDS; k++)
				if (spatial->scale[k] == 0) spatial->scale[k] = 1;
#endif
			// The spatial wavelet is the input for the next level
			wavelet = spatial;
		}
		break;

	case TRANSFORM_TYPE_FIELD:

		// Accumulate the scale factors due to filtering as the wavelet tree is computed
		for (k = 0; k < num_wavelets; k++)
		{
			i = 0;

			wavelet = transform->wavelet[k];

			// Adjust the scale of the pixel display range
			switch (wavelet->wavelet_type)
			{
			case WAVELET_TYPE_HORZTEMP:
				// The horizontal-temporal transforms do not require additional scaling
				break;

			case WAVELET_TYPE_TEMPORAL:
				// Temporal transforms use just one filter pass so need less scaling
				for (; i < 4; i++) scale[i] *= 2;
				break;

			case WAVELET_TYPE_SPATIAL:
				// Transforms that use two filter passes require more scaling
				for (; i < 4; i++)
				{
					scale[i] *= 4;
				}
				break;

			default:
				// Need to add scaling adjustments for other wavelet types
				assert(0);
				break;
			}

			// Save the scale factors in the wavelet data structure
			memcpy(wavelet->scale, scale, sizeof(scale));
		}
		break;

	case TRANSFORM_TYPE_FIELDPLUS:

		// Compute the number of frame and spatial wavelets
		num_frame_wavelets = 2;
		num_highpass_spatial = 1;
		num_lowpass_spatial = num_spatial - num_highpass_spatial;

		// Compute the change in scale due to the filters used in the frame transform
		temporal_lowpass_scale = temporal_lowpass_area * scale[0];
		temporal_highpass_scale = scale[0];

		// Compute the scale factors for the first two wavelets
		scale[0] = horizontal_lowpass_area * temporal_lowpass_scale;
		scale[1] = temporal_lowpass_scale;
		scale[2] = horizontal_lowpass_area * temporal_highpass_scale;
		scale[3] = temporal_highpass_scale;

		for (k = 0; k < num_frame_wavelets; k++)
		{
			wavelet = transform->wavelet[k];

			wavelet->scale[0] = scale[0];
			wavelet->scale[1] = scale[1];
			wavelet->scale[2] = scale[2];
			wavelet->scale[3] = scale[3];
		}

		// Compute the scale factors for the temporal wavelet between frames
		temporal = transform->wavelet[k++];

		temporal->scale[0] = temporal_lowpass_area * scale[0];
		temporal->scale[1] = scale[0];
		temporal->scale[2] = 0;
		temporal->scale[3] = 0;

		// The temporal highpass band is the input for the following chain of spatial transforms
		wavelet = temporal;
		temporal_highpass_scale = wavelet->scale[1];

		// Compute the scale factors for the spatial wavelets from the temporal highpass band
		for (i = 0; i < num_highpass_spatial; i++)
		{
			IMAGE *spatial = transform->wavelet[k++];

			assert(spatial != NULL);

			spatial->scale[0] = spatial_lowpass_area * temporal_highpass_scale;
			spatial->scale[1] = vertical_lowpass_area * temporal_highpass_scale;
			spatial->scale[2] = horizontal_lowpass_area * temporal_highpass_scale;
			spatial->scale[3] = temporal_highpass_scale;

			// The spatial wavelet is the input for the next level
			wavelet = spatial;

			// The lowpass output band is the input for the next spatial level
			temporal_highpass_scale = wavelet->scale[0];
		}

		// The temporal lowpass band is the input for the following chain of spatial transforms
		wavelet = temporal;

		// Compute the scale factors for the spatial wavelets from the temporal lowpass band
		for (i = 0; i < num_lowpass_spatial; i++)
		{
			IMAGE *spatial = transform->wavelet[k++];

			assert(spatial != NULL);

			// The lowpass band is the input to the spatial transform
			temporal_lowpass_scale = wavelet->scale[0];

			spatial->scale[0] = (spatial_lowpass_area * temporal_lowpass_scale);// >> _LOWPASS_PRESCALE;
			spatial->scale[1] = (vertical_lowpass_area * temporal_lowpass_scale);// >> _LOWPASS_PRESCALE;
			spatial->scale[2] = (horizontal_lowpass_area * temporal_lowpass_scale);// >> _LOWPASS_PRESCALE;
			spatial->scale[3] = (temporal_lowpass_scale);// >> _LOWPASS_PRESCALE;

#if (0 && DEBUG)
			// Force non-zero spatial scale to avoid assertions later in the code
			for (k = 0; k < IMAGE_NUM_BANDS; k++)
				if (spatial->scale[k] == 0) spatial->scale[k] = 1;
#endif
			// The spatial wavelet is the input for the next level
			wavelet = spatial;
		}
		break;

	default:
		assert(0);
		break;
	}
}

#if _DEBUG

void PrintTransformScale(TRANSFORM *transform, FILE *logfile)
{
	int num_wavelets = transform->num_wavelets;
	int k;

	for (k = 0; k < num_wavelets; k++)
	{
		IMAGE *wavelet = transform->wavelet[k];

		assert(wavelet != NULL);

		switch (wavelet->wavelet_type)
		{

		case WAVELET_TYPE_HORIZONTAL:		// One highpass band
		case WAVELET_TYPE_VERTICAL:
		case WAVELET_TYPE_TEMPORAL:
			fprintf(logfile, "Wavelet scale: %d %d\n", wavelet->scale[0], wavelet->scale[1]);
			break;

		case WAVELET_TYPE_SPATIAL:			// Three highpass bands
		case WAVELET_TYPE_HORZTEMP:
		case WAVELET_TYPE_VERTTEMP:
			fprintf(logfile, "Wavelet scale: %d %d %d %d\n",
					wavelet->scale[0], wavelet->scale[1], wavelet->scale[2], wavelet->scale[3]);
			break;

		case WAVELET_TYPE_IMAGE:			// Not really a wavelet
		case WAVELET_TYPE_TEMPQUAD:			// Should not occur in normal code
		case WAVELET_TYPE_HORZQUAD:
		default:
			assert(0);
			break;
		}
	}
}

#endif

#if 0
bool ReconstructGroupTransform(TRANSFORM *transform, int width, int height, int num_frames, int channel,
							   PIXEL *buffer, size_t buffer_size)
{
	IMAGE *frame[CODEC_GOP_LENGTH];
	IMAGE *wavelet;
	IMAGE *lowpass;
	IMAGE *highpass;
	int num_wavelets = transform->num_wavelets;
	int temporal_wavelet_index = 2;
	IMAGE *even, *odd;		// Scratch images for inverting the spatial transforms
	IMAGE *temporal;		// Temporal wavelet between the two frame wavelets
	IMAGE *quad[2];			// Quad wavelets for the two frames in the group
	IMAGE *pair[2];			// Temporal wavelets between the two fields in each frame
	IMAGE *field[2];		// Even and odd fields of each frame
	IMAGE *temp;			// Scratch for inverting the wavelets
	int quad_width;			// Dimensions and level of the two quad wavelets
	int quad_height;
	int quad_level;
	int pair_width;			// Width of the temporal wavelets
	//PIXEL *buffer;
	//size_t buffer_size;
	size_t frame_size;
	size_t image_size;
	int pitch;
	int background = ((channel == 0) ? COLOR_LUMA_BLACK : COLOR_CHROMA_ZERO);
	int prescale = (channel == 0) ? PRESCALE_LUMA : PRESCALE_CHROMA;
	int k;

	PIXEL *line_buffer;
	size_t line_buffer_size;

	// Check that the array of frames has enough frames to invert the transform
	assert(num_frames >= transform->num_frames);

	START(tk_inverse);

	// Get the image planes to reconstruct

	// Get the frame dimensions
	//height = frame[0]->height;
	//width = frame[0]->width;
	//pitch = frame[0]->pitch;

	pitch = ((width+63)/64)*64; // I don't have a real pitch, kludge.

	// Compute the maximum size of each frame
	frame_size = height * pitch;

	// Has the routine been provided with a buffer?
	if (buffer == NULL || buffer_size < frame_size)
	{
		// Does the transform contain a buffer that is large enough?
		if (transform->buffer == NULL || transform->size < frame_size)
		{
			// Need to reallocate the existing buffer in the transform?
			if (transform->buffer != NULL) MEMORY_ALIGNED_FREE(transform->buffer);//TODO Allocator

			// Allocate a buffer that is large enough to hold one frame
			transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(frame_size, 16);//TODO Allocator
			transform->size = frame_size;
		}

		// Use the buffer from the transform data structure
		buffer = transform->buffer;
		buffer_size = transform->size;
	}

	// Check that the buffer for transform processing is valid
	assert(buffer != NULL && buffer_size >= frame_size);
	if(buffer == NULL || buffer_size < frame_size) return false;

	switch(transform->type)
	{
	/**** Reconstruct field transform */
	case TRANSFORM_TYPE_FIELD:
		// Start with the wavelet at the top of the transform pyramid and
		// reconstruct the lowpass bands by inverting the spatial transforms
		for (k = num_wavelets - 1; k > temporal_wavelet_index; k--)
		{
			const bool inverse_prescale = false;

			wavelet = transform->wavelet[k];		// Wavelet to be inverted
			lowpass = transform->wavelet[k-1];		// Lowpass result

			assert(wavelet != NULL);
			if (wavelet == NULL) goto finish;

			assert(lowpass != NULL);
			if (lowpass == NULL) goto finish;

			// Reconstruct the lowpass band from the wavelet
			TransformInverseSpatial(wavelet, lowpass, buffer, buffer_size, prescale);
		}
		break;

	/**** Reconstruct field+ transform ****/
	case TRANSFORM_TYPE_FIELDPLUS:
		// Start with the wavelet at the top of the transform pyramid and
		// reconstruct the top-level spatial transform
		k = num_wavelets - 1;
		{
			const bool inverse_prescale = false;

			wavelet = transform->wavelet[k];		// Wavelet to be inverted
			lowpass = transform->wavelet[k-1];		// Lowpass result

			assert(wavelet != NULL);
			if (wavelet == NULL) goto finish;

			assert(lowpass != NULL);
			if (lowpass == NULL) goto finish;

			// Reconstruct the lowpass band from the wavelet
			assert(0);
			k--;
		}

		// Reconstruct the spatial transform the temporal lowpass band
		{
			wavelet = transform->wavelet[k];		// Wavelet to be inverted
			lowpass = transform->wavelet[k-2];		// Lowpass result

			assert(wavelet != NULL);
			if (wavelet == NULL) goto finish;

			assert(lowpass != NULL);
			if (lowpass == NULL) goto finish;

			// Reconstruct the lowpass band from the wavelet
			assert(0);
			k--;
		}

		// Reconstruct the spatial transform for the temporal highpass band
		{
			wavelet = transform->wavelet[k];		// Wavelet to be inverted
			highpass = transform->wavelet[k-1];		// Highpass result

			assert(wavelet != NULL);
			if (wavelet == NULL) goto finish;

			assert(highpass != NULL);
			if (highpass == NULL) goto finish;

			// Reconstruct the lowpass band from the wavelet
			assert(0);		// Need to implement later
			k--;
		}

		assert(k == temporal_wavelet_index);
		break;
	default:
		assert(0);		// Other transform types not yet implemented.
		break;
	}

	// Loop should have terminated at the temporal wavelet that is the
	// base (original image) of the spatial wavelet transform pyramid
	assert(k == temporal_wavelet_index);

	// Get the spatio-temporal wavelets corresponding to the two frames
	// in this group of frames and get the temporal wavelet between them
	temporal = transform->wavelet[k];
	quad[0] = transform->wavelet[k-2];
	quad[1] = transform->wavelet[k-1];

	// Invert the temporal transform to recover the lowpass band
	// in the wavelets for the frame fields in the group of frames
	TransformInverseTemporal(temporal, quad[0], quad[1]);

	// Get the dimensions of the two wavelets at the bottom of the pyramid
	quad_width = quad[0]->width;
	quad_height = quad[0]->height;
	quad_level = quad[0]->level;

	assert(quad_width == quad[1]->width);
	assert(quad_height == quad[1]->height);
	assert(quad_level == quad[1]->level);

#if 0		// The inverse frame transform is done by the caller

	// Invert the horizontal and temporal transforms applied to the first frame
	TransformInverseField(quad[0], frame[0]);

	// Repeat for the second frame
	TransformInverseField(quad[1], frame[1]);

#endif

finish:
	STOP(tk_inverse);

	// Indicate that the transform was successfully reconstructed
	return true;
}
#endif

#if 0
bool ReconstructGroupImages(TRANSFORM *transform, IMAGE *frame[], int num_frames, int background, int prescale)
{
	IMAGE *wavelet;
	IMAGE *lowpass;
	int num_wavelets = transform->num_wavelets;
	int temporal_wavelet_index = 2;
	IMAGE *even, *odd;		// Scratch images for inverting the spatial transforms
	IMAGE *temporal;		// Temporal wavelet between the two frame wavelets
	IMAGE *quad[2];			// Quad wavelets for the two frames in the group
	IMAGE *pair[2];			// Temporal wavelets between the two fields in each frame
	IMAGE *field[2];		// Even and odd fields of each frame
	IMAGE *temp;			// Scratch for inverting the wavelets
	int quad_width;			// Dimensions and level of the two quad wavelets
	int quad_height;
	int quad_level;
	int pair_width;			// Width of the temporal wavelets
	PIXEL *buffer;
	size_t frame_size;
	size_t image_size;
	size_t buffer_size;
	int height, width, pitch;
	int k;

	// Check that the array of frames has enough frames to invert the transform
	assert(num_frames >= transform->num_frames);
	assert(frame != NULL);
	assert(frame[0] != NULL);
	assert(frame[1] != NULL);

	if (frame[0] == NULL) return false;
	if (frame[1] == NULL) return false;

	START(tk_inverse);

	// Get the frame dimensions
	height = frame[0]->height;
	width = frame[0]->width;
	pitch = frame[0]->pitch;

	// Allocate buffer space for one frame
	frame_size = height * pitch;
	if (transform->buffer != NULL) {
		if (transform->size < frame_size) {
			MEMORY_ALIGNED_FREE(transform->buffer);
			transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(frame_size, 16);
			transform->size = frame_size;
		}
	}
	else {
		transform->buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(frame_size, 16);
		transform->size = frame_size;
	}
	buffer = transform->buffer;
	buffer_size = transform->size;
	assert(buffer != NULL);
	if (buffer == NULL) return false;

	// Start with the wavelet at the top of the transform pyramid and
	// reconstruct the lowpass bands by inverting the spatial transforms
	for (k = num_wavelets - 1; k > temporal_wavelet_index; k--)
	{
		wavelet = transform->wavelet[k];		// Wavelet to be inverted
		lowpass = transform->wavelet[k-1];		// Lowpass result

		assert(wavelet != NULL);
		if (wavelet == NULL) goto finish;

		assert(lowpass != NULL);
		if (lowpass == NULL) goto finish;

		// Reconstruct the lowpass band from the wavelet
		TransformInverseSpatial(wavelet, lowpass, buffer, buffer_size, prescale);
	}

	// Loop should have terminated at the temporal wavelet that is the
	// base (original image) of the spatial wavelet transform pyramid
	assert(k == temporal_wavelet_index);

	// Get the spatio-temporal wavelets corresponding to the two frames
	// in this group of frames and get the temporal wavelet between them
	temporal = transform->wavelet[k];
	quad[0] = transform->wavelet[k-2];
	quad[1] = transform->wavelet[k-1];

	// Invert the temporal transform to recover the lowpass band
	// in the wavelets for the frame fields in the group of frames
	TransformInverseTemporal(temporal, quad[0], quad[1]);

	// Get the dimensions of the two wavelets at the bottom of the pyramid
	quad_width = quad[0]->width;
	quad_height = quad[0]->height;
	quad_level = quad[0]->level;

	assert(quad_width == quad[1]->width);
	assert(quad_height == quad[1]->height);
	assert(quad_level == quad[1]->level);

	// Invert the horizontal and temporal transforms applied to the first frame
	TransformInverseField(quad[0], frame[0]);

	// Repeat for the second frame
	TransformInverseField(quad[1], frame[1]);

finish:
	STOP(tk_inverse);

	// Indicate that the transform was successfully reconstructed
	return true;
}
#endif

#if 0
void ReconstructImagePyramid(IMAGE *wavelet)
{
	IMAGE *lowpass;
	PIXEL *buffer;			// Temporary image for the inverse transform
	size_t size;			// Buffer size
	//int offset = 0;		// Each reconstruction shifts the image

	assert(wavelet != NULL);
	if (wavelet == NULL) return;

	// Get the lowpass image in the lower level wavelet
	lowpass = wavelet->expanded;

	// Did subroutine start at the bottom of the wavelet pyramid?
	if (lowpass == NULL)
	{
		// Reconstruct the base of the pyramid
		lowpass = wavelet->original;

		// Check that the base is really the bottom of the image pyramid
		assert(lowpass == NULL || lowpass->expanded == NULL);
	}

	// Reconstruct the wavelet pyramid
	while (lowpass != NULL /* && lowpass->level > 0 */)
	{
		// Allocate scratch images
		int width = wavelet->width;
		int height = wavelet->height;
		int level = wavelet->level;

		size = lowpass->height * lowpass->pitch;
		buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
		TransformInverseSpatial(wavelet, lowpass, buffer, size, 0);
		MEMORY_ALIGNED_FREE(buffer);

		// Advance to the next level in the wavelet pyramid
		wavelet = lowpass;
		lowpass = wavelet->expanded;

		if (lowpass == NULL)
		{
			// Reconstruct the base of the pyramid
			lowpass = wavelet->original;

			// Check that the base is really the bottom of the image pyramid
			assert(lowpass == NULL || lowpass->expanded == NULL);
		}
	}
}
#endif

// Convert the transform from 8-bit to 16-bit coefficients
void ConvertGroupTransform(TRANSFORM *transform)
{
	int i, k;

	for (i = 0; i < transform->num_wavelets; i++) {
		IMAGE *wavelet = transform->wavelet[i];
		for (k = 0; k < wavelet->num_bands; k++)
			if (wavelet->pixel_type[k] != PIXEL_TYPE_16S)
				ConvertWaveletBand(wavelet, k);
	}
}

void ConvertWaveletBand(IMAGE *wavelet, int k)
{
	PIXEL8S *rowptr = (PIXEL8S *)wavelet->band[k];
	PIXEL16S *outptr = wavelet->band[k];
	int width = wavelet->width;
	int height = wavelet->height;
	int input_pitch = wavelet->pitch;
	int output_pitch = wavelet->pitch;
	int row, column;

	// Check that there is enough room in each row for the converted pixels
	assert((size_t)output_pitch >= (width * sizeof(PIXEL16S)));

	input_pitch /= sizeof(PIXEL8S);
	output_pitch /= sizeof(PIXEL16S);

	for (row = 0; row < height; row++)
	{
		// Convert the pixels in place from right to left
		for (column = width - 1; column >= 0; column--)
			outptr[column] = rowptr[column];

		// Advance to the next row
		rowptr += input_pitch;
		outptr += output_pitch;
	}
}

#if _THREADED_ENCODER

/***** Threaded implementations of the wavelet transforms *****/


#ifdef _WIN32
#include <windows.h>
#endif


// Structure for passing data to each thread
typedef struct _thread_filter_data
{
	uint8_t *input;
	int input_pitch;
	IMAGE *wavelet;
	//int wavelet_pitch;
	PIXEL *buffer;
	size_t buffer_size;
	int width;
	int height;
	int channel;
	int quantization[IMAGE_NUM_BANDS];

} THREAD_FILTER_DATA;


DWORD WINAPI FilterSpatialYUVQuant16sThread(LPVOID param)
{
	THREAD_FILTER_DATA *data = (THREAD_FILTER_DATA *)param;
	uint8_t *input = data->input;
	int input_pitch = data->input_pitch;
	IMAGE *wavelet = data->wavelet;
	PIXEL *buffer = data->buffer;
	size_t buffer_size = data->buffer_size;
	ROI roi = {data->width, data->height};
	int channel = data->channel;

	// Must prescale the lowpass coefficients without changing the lowpass band
	FilterSpatialYUVQuant16s(input, input_pitch,
							 wavelet->band[0], wavelet->pitch,
							 wavelet->band[1], wavelet->pitch,
							 wavelet->band[2], wavelet->pitch,
							 wavelet->band[3], wavelet->pitch,
							 buffer, buffer_size, roi, channel,
							 data->quantization, NULL, 8);
	return 0;
}


// Unpack YUV pixels in a progressive frame and perform the forward spatial transform
void TransformForwardSpatialThreadedYUV(uint8_t *input, int input_pitch, FRAME_INFO *frame,
										TRANSFORM *transform[], int frame_index, int num_channels,
										PIXEL *buffer, size_t buffer_size, int chroma_offset)
{
	int frame_width = frame->width;
	int frame_height = frame->height;
	size_t size;
	size_t luma_buffer_size;
	size_t chroma_buffer_size;
	size_t total_size;
	HANDLE thread[CODEC_MAX_CHANNELS];
	THREAD_FILTER_DATA data[CODEC_MAX_CHANNELS];
	DWORD active_threads = (1 << num_channels) - 1;
	DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;
	int channel;

	// Compute the size of buffer required for the forward wavelet transform
	size = (frame_width / 2) * sizeof(PIXEL);	// Output image is half as wide
	size = ALIGN(size, _CACHE_LINE_SIZE);		// Align each output row
	size *= 14;									// Need fourteen rows

	luma_buffer_size = size;
	chroma_buffer_size = size/2;
	total_size = luma_buffer_size + 2 * chroma_buffer_size;

	// Check the size of the processing buffer
	assert(buffer != NULL);
	assert(buffer_size >= total_size);

	// Allocate a buffer for the luma channel
	data[0].buffer = buffer;
	data[0].buffer_size = luma_buffer_size;

	// Allocate buffers for the chroma channels
	data[1].buffer = data[0].buffer + luma_buffer_size;
	data[1].buffer_size = chroma_buffer_size;
	data[2].buffer = data[1].buffer + chroma_buffer_size;
	data[2].buffer_size = chroma_buffer_size;

	//START(tk_spatial);
	START(tk_progressive);

	// Launch a thread to process each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		DWORD dwThreadID;
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		int k;

		// Set the channel number
		data[channel].channel = channel;

		// Set the input and output parameters
		data[channel].input = input;
		data[channel].input_pitch = input_pitch;
		data[channel].wavelet = wavelet;

		// Compute the input dimensions from the output dimensions
		data[channel].width = 2 * width;
		data[channel].height = 2 * height;

		// Fill the quantization vector
		for (k = 0; k < IMAGE_NUM_BANDS; k++) {
			data[channel].quantization[k] = wavelet->quant[k];
		}

		// Check the input dimensions
		assert((channel == 0 && data[channel].width == frame_width) ||
				(channel > 0 && data[channel].width == frame_width/2));
		assert(data[channel].height == frame_height);

		// Create a thread for processing this channel
		thread[channel] = CreateThread(NULL, 0, FilterSpatialYUVQuant16sThread, &data[channel], 0, &dwThreadID);
	}

	// Wait for all of the threads to finish
	while (active_threads > 0)
	{
		// Wait for one of the threads to finish
		int32_t result = WaitForMultipleObjects(num_channels, thread, false, dwTimeout);

		if (WAIT_OBJECT_0 <= result && result < WAIT_OBJECT_0 + num_channels)
		{
			IMAGE *wavelet = transform[channel]->wavelet[frame_index];
			int channel = result - WAIT_OBJECT_0;

			// Set the output pixel type
			wavelet->pixel_type[0] = PIXEL_TYPE_16S;
			wavelet->pixel_type[1] = PIXEL_TYPE_16S;
			wavelet->pixel_type[2] = PIXEL_TYPE_16S;
			wavelet->pixel_type[3] = PIXEL_TYPE_16S;

			// Record any quantization that was applied after filtering
			//if (quantization != NULL)
			{
				int k;
				for (k = 0; k < IMAGE_NUM_BANDS; k++)
					wavelet->quantization[k] = data[channel].quantization[k];
			}

			// Indicate that this channel is done
			active_threads &= ~((DWORD)(1 << channel));
		}
	}

	//STOP(tk_spatial);
	STOP(tk_progressive);
}


// Structure for passing data to each thread
typedef struct _thread_transform_data
{
	IMAGE *input;
	IMAGE *wavelet;
	PIXEL *buffer;
	size_t buffer_size;
	int channel;

} THREAD_TRANSFORM_DATA;


DWORD WINAPI TransformForwardSpatialThread(LPVOID param)
{
	THREAD_TRANSFORM_DATA *data = (THREAD_TRANSFORM_DATA *)param;
	IMAGE *image = data->input;
	IMAGE *wavelet = data->wavelet;
	PIXEL *buffer = data->buffer;
	size_t buffer_size = data->buffer_size;
	int level = wavelet->level;
	const int band = 0;

	// Apply the spatial transform to the image plane for this channel
	TransformForwardSpatial(image, band, wavelet, level, buffer, buffer_size, 0, wavelet->quant, 0);

	return 0;
}


void TransformForwardSpatialThreadedChannels(FRAME *input, int frame, TRANSFORM *transform[],
											 int level, PIXEL *buffer, size_t buffer_size)
{
	HANDLE thread[CODEC_MAX_CHANNELS];
	THREAD_TRANSFORM_DATA data[CODEC_MAX_CHANNELS];
	//DWORD active_threads = (1 << num_channels) - 1;
	DWORD dwTimeout = ENCODER_THREAD_TIMEOUT;
	size_t luma_buffer_size;
	size_t chroma_buffer_size;
	size_t total_buffer_size;
	int num_channels = input->num_channels;
	int channel;
	int luma_width;
	int chroma_width;
	int32_t result;

	// Compute the width of the image for each channel
	IMAGE *luma_image = input->channel[0];
	IMAGE *chroma_image = input->channel[1];
	luma_width = luma_image->width;
	chroma_width = chroma_image->width;

	// Compute the required buffer size for each channel
	luma_buffer_size = ForwardSpatialBufferSize(luma_width);
	chroma_buffer_size = ForwardSpatialBufferSize(chroma_width);

	// Compute the total size of all buffers
	total_buffer_size = luma_buffer_size + 2 * chroma_buffer_size;

	// Check that the supplied buffer is large enough
	assert(buffer_size >= total_buffer_size);

	// Allocate space for each channel buffer
	data[0].buffer = buffer;
	data[0].buffer_size = luma_buffer_size;
	data[1].buffer = data[0].buffer + data[0].buffer_size;
	data[1].buffer_size = chroma_buffer_size;
	data[2].buffer = data[1].buffer + data[1].buffer_size;
	data[2].buffer_size = chroma_buffer_size;

	START(tk_progressive);

	// Apply the spatial wavelet transform to each plane
	for (channel = 0; channel < num_channels; channel++)
	{
		IMAGE *image = input->channel[channel];
		IMAGE *wavelet = transform[channel]->wavelet[frame];
		DWORD dwThreadID;
		const int band = 0;
		const int level = 1;

		// Set the input and output images
		data[channel].input = image;
		data[channel].wavelet = wavelet;

		// Set the channel number
		data[channel].channel = channel;

		// The lowpass band must be one byte pixels
		assert(image->pixel_type[0] == PIXEL_TYPE_8U);

		// Create a thread for processing this channel
		thread[channel] = CreateThread(NULL, 0, TransformForwardSpatialThread, &data[channel], 0, &dwThreadID);
	}

	// Wait for all of the threads to finish
	result = WaitForMultipleObjects(num_channels, thread, true, dwTimeout);
	assert(result != WAIT_FAILED && result != WAIT_TIMEOUT);

	STOP(tk_progressive);
}

#endif


// New routine for computing the inverse transform of the largest spatial wavelet
#if 0
// Adapted from TransformInverseRGB444ToPackedYUV8u
void TransformInverseSpatialYUV422ToOutput(DECODER *decoder,
											TRANSFORM *transform[], int frame_index, int num_channels,
											uint8_t *output_buffer, int output_pitch, FRAME_INFO *info,
											const SCRATCH *scratch, int chroma_offset, int precision,
											HorizontalInverseFilterOutputProc horizontal_filter_proc)
{
	PIXEL *lowlow_band[CODEC_MAX_CHANNELS];
	PIXEL *lowhigh_band[CODEC_MAX_CHANNELS];
	PIXEL *highlow_band[CODEC_MAX_CHANNELS];
	PIXEL *highhigh_band[CODEC_MAX_CHANNELS];

	int lowlow_pitch[CODEC_MAX_CHANNELS];
	int lowhigh_pitch[CODEC_MAX_CHANNELS];
	int highlow_pitch[CODEC_MAX_CHANNELS];
	int highhigh_pitch[CODEC_MAX_CHANNELS];

	int channel_width[CODEC_MAX_CHANNELS];

	uint8_t *output_row_ptr = output_buffer;
	//uint8_t *plane_array[TRANSFORM_MAX_CHANNELS];
	//int plane_pitch[TRANSFORM_MAX_CHANNELS];
	int output_width = info->width;
	int output_height = info->height;
	int half_height = output_height/2;
	int odd_height = output_height&1;
	//int format = info->format;
	int luma_band_width;
	ROI strip;
	char *bufptr;
	int last_row;
	int last_display_row;
	int last_line;
	int channel;
	int row;
	int do_edge_row = 0;

	// Push the scratch space state to allocate a new section
	char *buffer = scratch->free_ptr;
	size_t buffer_size = scratch->free_size;

	//TODO: Replace uses of buffer variables with calls to the scratch space API

	// This version is for 16-bit pixels
	assert(sizeof(PIXEL) == 2);

	// Check that the output format is YUYV packed pixels
	//assert(format == DECODED_FORMAT_YUYV);

	// Divide the buffer space between the four threads
	//buffer_size /= 4;
	//buffer += buffer_size * thread_index;

	// Round the buffer pointer up to the next cache line
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)buffer & _CACHE_LINE_MASK));
	bufptr = (char *)ALIGN(buffer, _CACHE_LINE_SIZE);

	// Allocate buffer space for the output rows from each channel
	for (channel = 0; channel < num_channels; channel++)
	{
		// Get the row width for this channel
		IMAGE *wavelet = transform[channel]->wavelet[frame_index];
		int width = wavelet->width;
		int height = wavelet->height;
		int pitch = wavelet->pitch;
		//size_t channel_buffer_size;

		// Compute the width and pitch for the output rows stored in this buffer
		int buffer_width = 2 * width;
		int buffer_height = 2;
		//int buffer_pitch = ALIGN(buffer_width, _CACHE_LINE_SIZE);
		int buffer_pitch = ALIGN16(buffer_width);

		// Compute the total allocation for this channel
		//channel_buffer_size = buffer_height * buffer_pitch;

		// Check that there is enough space available
		//assert(channel_buffer_size <= buffer_size);

		// Allocate the buffer for this channel
		//plane_array[channel] = bufptr;

		// Remember the pitch for rows in this channel
		//plane_pitch[channel] = buffer_pitch;

		// Advance the buffer pointer past the allocated space for this channel
		//bufptr += channel_buffer_size;

		// Reduce the amount of space remaining in the buffer
		//buffer_size -= channel_buffer_size;

		// The dimensions of the output image are determined by the first channel
		if (channel == 0)
		{
			strip.width = buffer_width;
			strip.height = buffer_height;
			last_row = height;

			//DAN20050606 Added to fix issue with heights that are not divisible by eight
			last_display_row = info->height/2;

			// Remember the width of the wavelet bands for luma
			luma_band_width = width;
		}

		// Save the bands per channel for routines that process all channels at once
		lowlow_band[channel] = wavelet->band[0];
		lowhigh_band[channel] = wavelet->band[1];
		highlow_band[channel] = wavelet->band[2];
		highhigh_band[channel] = wavelet->band[3];

		lowlow_pitch[channel] = wavelet->pitch;
		lowhigh_pitch[channel] = wavelet->pitch;
		highlow_pitch[channel] = wavelet->pitch;
		highhigh_pitch[channel] = wavelet->pitch;

		// Remember the width of the wavelet for this channel
		channel_width[channel] = width;
	}

	// Use the remaining buffer space for intermediate results
	//buffer = bufptr;
	buffer_size -= (_CACHE_LINE_SIZE - ((uintptr_t)bufptr & _CACHE_LINE_MASK));
	buffer = (char *)ALIGN(bufptr, _CACHE_LINE_SIZE);

	if (last_row == last_display_row) {
		do_edge_row = 1;
	}

	// Start processing at the first row
	row = 0;

	// Invert the spatial wavelet into strips of RGB pixels and pack into 8-bit YUV
	InvertSpatialTopRow16sToOutput(decoder, lowlow_band, lowlow_pitch,
								   lowhigh_band, lowhigh_pitch,
								   highlow_band, highlow_pitch,
								   highhigh_band, highhigh_pitch,
								   output_row_ptr, output_pitch,
								   output_width, info->format, info->colorspace,
								   row, channel_width,
								   (PIXEL *)buffer, buffer_size,
								   precision,
								   horizontal_filter_proc);

	// Advance the output row pointer past the two rows that were computed
	output_row_ptr += 2 * output_pitch;

	// Process the middle rows of the output frame
	for (row = 1; row < last_display_row - do_edge_row; row++)
	{
		int thread_index = -1;
		// Invert the spatial wavelet into strips of RGB pixels and pack into 8-bit YUV
		InvertSpatialMiddleRow16sToOutput(decoder, thread_index, lowlow_band, lowlow_pitch,
										  lowhigh_band, lowhigh_pitch,
										  highlow_band, highlow_pitch,
										  highhigh_band, highhigh_pitch,
										  output_row_ptr, output_pitch,
										  output_width, info->format, info->colorspace,
										  row, channel_width,
										  (PIXEL *)buffer, buffer_size,
										  precision,
										  horizontal_filter_proc, 2);

		// Advance the output row pointer past the two rows that were computed
		output_row_ptr += 2 * output_pitch;
	}

	// Check that the middle row loop exited at the last row
	assert(row == last_display_row - do_edge_row);

	if (do_edge_row)
	{
		// Process the bottom border (last two rows) of the output frame

		// Invert the spatial wavelet into strips of RGB pixels and pack into 8-bit YUV
		InvertSpatialBottomRow16sToOutput(decoder, thread_index, lowlow_band, lowlow_pitch,
										  lowhigh_band, lowhigh_pitch,
										  highlow_band, highlow_pitch,
										  highhigh_band, highhigh_pitch,
										  output_row_ptr, output_pitch,
										  output_width, info->format, info->colorspace,
										  row, channel_width,
										  (PIXEL *)buffer, buffer_size,
										  precision, odd_height,
										  horizontal_filter_proc);
	}
}
#endif // not used
