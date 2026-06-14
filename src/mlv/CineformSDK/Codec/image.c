/*! @file image.c

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

#include "config.h"
#include "timing.h"

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif
#define TIMING (1 && _TIMING)
#define XMMOPT (1 && _XMMOPT)

#ifdef _WIN32
#define SYSLOG	0
#else
#define SYSLOG	(0 && DEBUG)
#endif

#include <stdlib.h>
#include <string.h>		// Use memcpy to copy runs of image pixels
#include <memory.h>
#include <assert.h>
#include <limits.h>
#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif

#include "image.h"
//#include "ipp.h"		// Use Intel Performance Primitives
#include "codec.h"
#include "filter.h"
#include "debug.h"
#include "wavelet.h"
#include "color.h"
#include "allocator.h"


#if __APPLE__
#include "../Common/macdefs.h"
#endif

#if !defined(_WIN32)
#define min(x,y)	(((x) < (y)) ? (x) : (y))
#define max(x,y)	(((x) > (y)) ? (x) : (y))
#endif

#ifndef _MAX_PATH
#define _MAX_PATH 256	// Maximum length of pathname on the Mac
#endif

//TODO: Replace uses of Windows _MAX_PATH with PATH_MAX
#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif

// Global Variables
// Debugging

#if DEBUG

static const size_t image_struct_size = sizeof(IMAGE);
static size_t image_request_size = 0;

// Replace border with null border to print the entire band
//#define BORDER &border
#define BORDER NULL

#endif


// Local Functions

static void set_image_dimensions(IMAGE *image, int width, int height)
{
	const int cache_line_size = _CACHE_LINE_SIZE;
	int alignment;
	int pitch;

	// Check that the image descriptor has been allocated
	assert(image != NULL);
	if (image == NULL) return;

	image->width = width;
	image->height = height;

	// Compute the byte offset between rows
	pitch = width * sizeof(PIXEL);

	// Round the pitch to an integral number of 16 byte blocks
	if (pitch < cache_line_size) alignment = 16;
	else alignment = cache_line_size;

	image->pitch = ALIGN(pitch, alignment);
}


PIXEL *subimage_address(IMAGE *image, int band_index, SUBIMAGE *subimage)
{
	PIXEL *address = image->band[band_index];
	int pitch = image->pitch/sizeof(PIXEL);

	address += (subimage->row * pitch) + subimage->column;

	return address;
}

void InitFrameInfo(FRAME_INFO *info, int width, int height, int format)
{
	if (info != NULL)
	{
		memset(info, 0, sizeof(FRAME_INFO));
		info->width = width;
		info->height = height;
		info->format = format;
	}
}

bool IsBandValid(IMAGE *wavelet, int band)
{
	return (wavelet != NULL && ((wavelet->band_valid_flags & BAND_VALID_MASK(band)) != 0));
}

// Allocate space for an image and initialize its image descriptor
#if _ALLOCATOR
void AllocImage(ALLOCATOR *allocator, IMAGE *image, int width, int height)
#else
void AllocImage(IMAGE *image, int width, int height)
#endif
{
	const int alignment = _CACHE_LINE_SIZE;
	int i;

	// Check that an image descriptor was provided
	assert(image != NULL);

	// Zero all fields and set the image level to zero (full size)
	memset(image, 0, sizeof(IMAGE));

	if (width > 0 && height > 0)
	{
		size_t image_size;

		// Calculate the image dimensions
		set_image_dimensions(image, width, height);

		// Allocate space for the image
		image_size = height * image->pitch;
#if DEBUG && 0
		image_request_size = image_size;
		image->memory_size = image_size;
#endif
#if _ALLOCATOR
		image->memory = (PIXEL *)AllocAligned(allocator, image_size, alignment);
#else
		image->memory = (PIXEL *)MEMORY_ALIGNED_ALLOC(image_size, alignment);
#endif
#if (1 && SYSLOG)
		fprintf(stderr, "AllocImage address: 0x%p, size: %d\n", (int)image->memory, image_size);
#endif
        
#if DEBUG
        if (image->memory == NULL)
        {
            fprintf(stderr, "[%s, line#%d] malloc(%d bytes, align=%d) FAILED !!\n", __FILE__,__LINE__, (int)image_size,alignment);
        }
#endif
        
		// Check that a memory block was allocated
		assert(image->memory != NULL);

		// The image is in band zero by convention
		image->band[0] = image->memory;

		// Set the number of bands in use
		image->num_bands = 1;

#if (0 && DEBUG)
		// Clear the image to make debugging easier
		memset(image->memory, 0, image_size);
#endif
	}

	// Set the image type to gray
	image->type = IMAGE_TYPE_GRAY;

	// Save the dimensions of the image before filtering
	//image->band_width = width;
	//image->band_height = height;

	// Indicate that bands point into the same block
	for (i = 0; i < IMAGE_NUM_BANDS; i++) {
		image->alloc[i] = IMAGE_ALLOC_ONE_MALLOC;
	}

	// The image is in frame rather than field format
	image->format = IMAGE_FORMAT_FRAME;

	// Initialize the scale factor that records the effects of filtering
	image->scale[0] = 1;

	// Record the pixel type
	image->pixel_type[0] = PIXEL_TYPE_16S;
}

// Create a new image
#if _ALLOCATOR
IMAGE *CreateImage(ALLOCATOR *allocator, int width, int height)
#else
IMAGE *CreateImage(int width, int height)
#endif
{
	IMAGE *image;

	assert(width > 0 && height > 0);

#if _ALLOCATOR
	image = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
#else
	image = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
#endif

	if (image != NULL)
	{
#if _ALLOCATOR
		AllocImage(allocator, image, width, height);
#else
		AllocImage(image, width, height);
#endif
	}
	else
	{
#if (DEBUG && _WIN32)
		OutputDebugString("CreateImage sizeof(IMAGE)");
#endif
	}

	return image;
}

// Adjust the size of an image to the specified dimensions
#if 0
void ResizeImage(IMAGE *image, int width, int height)
{
#if _ALLOCATOR
	//TODO: Modify this routine to pass an allocator as the first argument
	ALLOCATOR *allocator = decoder->allocator;
#endif

	int i;

	assert(image != NULL);
	if (image != NULL) {
		if (image->width != width || image->height != height) {
			size_t image_size;
			image->width = width;
			image->height = height;
			image->pitch = ALIGN16(width * sizeof(PIXEL));

			image_size = height * image->pitch;

			for (i = 0; i < IMAGE_NUM_BANDS; i++)
			{
				switch (image->alloc[i])
				{

				case IMAGE_ALLOC_ONE_MALLOC:
					image->band[i] = (PIXEL *)MEMORY_ALIGNED_ALLOC(image_size, 16);
					break;

				case IMAGE_ALLOC_BAND_MALLOC:
#if 0
					if (image->band[i] != NULL) {
						image->band[i] = (PIXEL *)realloc(image->band[i], image_size);
					}
					else {
						image->band[i] = (PIXEL *)MEMORY_ALLOC(image_size);
					}
#elif _ALLOCATOR
					// Intel library does not have realloc
					if (image->band[i] != NULL) {
						FreeAligned(allocator, image->band[i]);
					}

					image->band[i] = (PIXEL *)AllocAligned(allocator, image_size, 16);
#else
					// Intel library does not have realloc
					if (image->band[i] != NULL) {
						MEMORY_ALIGNED_FREE(image->band[i]);
					}

					image->band[i] = (PIXEL *)MEMORY_ALIGNED_ALLOC(image_size, 16);
#endif
					break;

				default:
					assert(0);
					break;
				}
			}

			// Can MEMORY_FREE the single memory block that may have been used by some bands
			if (image->memory != NULL)
			{
#if _ALLOCATOR
				FreeAligned(allocator, image->memory);
#else
				MEMORY_ALIGNED_FREE(image->memory);
#endif
			}
		}
	}
}
#endif

// Allocate space for a new band
#if _ALLOCATOR
void AllocateBand(ALLOCATOR *allocator, IMAGE *image, int band_index)
#else
void AllocateBand(IMAGE *image, int band_index)
#endif
{
	size_t size;
	assert(image != NULL);
	if (image == NULL) return;

	// Check for a valid band index and that is MEMORY_FREE
	assert(0 <= band_index && band_index < IMAGE_NUM_BANDS);
	if (image->band[band_index] != NULL) return;

	// Calculate size of one band
	size = image->height * image->pitch;

	// Allocate the new band
	if (image->band[band_index] && image->alloc[band_index] == IMAGE_ALLOC_BAND_MALLOC)
	{
#if _ALLOCATOR
		FreeAligned(allocator, image->band[band_index]);
#else
		MEMORY_ALIGNED_FREE(image->band[band_index]);
#endif
	}

#if _ALLOCATOR
	image->band[band_index] = (PIXEL *)AllocAligned(allocator, size, 16);
#else
	image->band[band_index] = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, 16);
#endif

	// Check that the band was allocated
	assert(image->band[band_index] != NULL);

	// Indicate that this band was allocated separately
	image->alloc[band_index] = IMAGE_ALLOC_BAND_MALLOC;

	// Initialize the scale factor for the new band
	image->scale[band_index] = 1;
}

#if _ALLOCATOR
void DeleteImage(ALLOCATOR *allocator, IMAGE *image)
#else
void DeleteImage(IMAGE *image)
#endif
{
	int band_index;

	assert(image != NULL);
	if (image != NULL)
	{
		// Free memory allocated for bands
		for (band_index = 0; band_index < IMAGE_NUM_BANDS; band_index++)
		{
			// Skip this band if it was not allocated
			if (image->band[band_index] == NULL) continue;

			// Determine how the band was allocated
			switch (image->alloc[band_index])
			{
			case IMAGE_ALLOC_BAND_MALLOC:
				if (image->band[band_index] != NULL)
				{
#if _ALLOCATOR
					FreeAligned(allocator, image->band[band_index]);
#else
					MEMORY_ALIGNED_FREE(image->band[band_index]);
#endif
				}
				break;

			case IMAGE_ALLOC_UNKNOWN:
			default:
				// Do not understand how this band was allocated
				assert(0);
				break;

			case IMAGE_ALLOC_ONE_MALLOC:
				// Memory block will be MEMORY_FREEd after this loop
				break;

			case IMAGE_ALLOC_STATIC_DATA:
				// Memory block does not have to be MEMORY_FREEd
				break;
			}
		}

		// Free the common memory block
		if (image->memory != NULL)
		{
#if _ALLOCATOR
			FreeAligned(allocator, image->memory);
#else
			MEMORY_ALIGNED_FREE(image->memory);
#endif
			image->memory = NULL;
		}

		// Free the image descriptor
#if _ALLOCATOR
		Free(allocator, image);
#else
		MEMORY_FREE(image);
#endif
	}
}

// Free the memory used by an image
#if _ALLOCATOR
void FreeImage(ALLOCATOR *allocator, IMAGE *image)
#else
void FreeImage(IMAGE *image)
#endif
{
	// Should check that the image was allocated as a single block

	if (image != NULL)
	{
		int i;

		// Free the block allocated for the image bands

		if(image->memory)
		{
#if _ALLOCATOR
			FreeAligned(allocator, image->memory);
#else
			MEMORY_ALIGNED_FREE(image->memory);
#endif
		}

		// Indicate that the block has been MEMORY_FREEd
		image->memory = NULL;

		// Clear the band data pointers into the image memory block
		for (i = 0; i < IMAGE_NUM_BANDS; i++) {
			assert(image->alloc[i] == IMAGE_ALLOC_ONE_MALLOC || image->band[i] == NULL);
			image->band[i] = NULL;
		}
	}
}

// Create an image with the same dimensions as another image
#if _ALLOCATOR
IMAGE *CreateImageFromImage(ALLOCATOR *allocator, IMAGE *image)
#else
IMAGE *CreateImageFromImage(IMAGE *image)
#endif
{
	int width = image->width;
	int height = image->height;

	// Note: This code should be extended to duplicate the bands
#if _ALLOCATOR
	IMAGE *new_image = CreateImage(allocator, width, height);
#else
	IMAGE *new_image = CreateImage(width, height);
#endif

	return new_image;
}

#if _ALLOCATOR
IMAGE *CreateImageFromArray(ALLOCATOR *allocator, PIXEL *array, int width, int height, int pitch)
#else
IMAGE *CreateImageFromArray(PIXEL *array, int width, int height, int pitch)
#endif
{
	// Create an image descriptor
#if _ALLOCATOR
	IMAGE *image = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
#else
	IMAGE *image = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
#endif
	if (image != NULL) {
		int i;

		// Zero all fields
		memset(image, 0, sizeof(IMAGE));

		// Initialize the image dimensions
		image->width = width;
		image->height = height;
		image->pitch = pitch;

		// Initialize the image bands
		image->band[0] = array;
		image->band[1] = NULL;
		image->band[2] = NULL;
		image->band[3] = NULL;

		// Only one image band
		image->num_bands = 1;

		// Set the image type to gray
		image->type = IMAGE_TYPE_GRAY;

		// Save the dimensions of the image bands before filtering
		//image->band_width = width;
		//image->band_height = height;

		// Indicate that the image was allocated from an existing array
		for (i = 0; i < IMAGE_NUM_BANDS; i++) {
			image->alloc[i] = IMAGE_ALLOC_STATIC_DATA;
		}

		// No memory block was allocated
		image->memory = NULL;

		// Initialize the image scale factor
		image->scale[0] = 1;
	}
	else
	{
#if (DEBUG && _WIN32)
		OutputDebugString("CreateImageFromArray sizeof(IMAGE)");
#endif
		assert(image != NULL);
	}

	return image;
}

// Create a wavelet with a single band from an array
#if _ALLOCATOR
IMAGE *CreateWaveletBandFromArray(ALLOCATOR *allocator, PIXEL *array, int width, int height, int pitch, int band)
#else
IMAGE *CreateWaveletBandFromArray(PIXEL *array, int width, int height, int pitch, int band)
#endif
{
	// Create an image descriptor for the wavelet
#if _ALLOCATOR
	IMAGE *wavelet = (IMAGE *)Alloc(allocator, sizeof(IMAGE));
#else
	IMAGE *wavelet = (IMAGE *)MEMORY_ALLOC(sizeof(IMAGE));
#endif

	if (wavelet != NULL)
	{
		int i;

		// Zero all fields
		memset(wavelet, 0, sizeof(IMAGE));

		// Initialize the wavelet dimensions
		wavelet->width = width;
		wavelet->height = height;
		wavelet->pitch = pitch;

		// Initialize the wavelet bands
		for (i = 0; i < IMAGE_NUM_BANDS; i++) {
			wavelet->band[i] = NULL;
		}

		// Assign the array to the specified band
		wavelet->band[band] = array;

		// Only one wavelet band
		wavelet->num_bands = 1;

		// Set the image type to wavelet
		wavelet->type = IMAGE_TYPE_WAVELET;

		// Indicate that the wavelet was allocated from an existing array
		for (i = 0; i < IMAGE_NUM_BANDS; i++) {
			wavelet->alloc[i] = IMAGE_ALLOC_STATIC_DATA;
		}

		// No memory block was allocated
		wavelet->memory = NULL;

		// Initialize the wavelet scale factors
		for (i = 0; i < IMAGE_NUM_BANDS; i++) {
			wavelet->scale[i] = 1;
		}
	}
	else
	{
#if (DEBUG && _WIN32)
		OutputDebugString("wavelet sizeof(IMAGE)");
#endif
		assert(wavelet != NULL);
	}

	return wavelet;
}

void ConvertImageToRGB(IMAGE *image, uint8_t *output_buffer, int32_t output_pitch, int format, bool inverted)
{
	int width = image->width;
	int height = image->height;
	int pitch = image->pitch/sizeof(PIXEL);
	int row, column;
	PIXEL *rowptr = image->band[0];
	uint8_t *outrow = output_buffer;
	uint8_t *outptr;

	// Only 24 and 32 bit true color RGB formats are supported
	assert(format == COLOR_FORMAT_RGB24 || format == COLOR_FORMAT_RGB32);

	// The output pitch should be a positive number before inversion
	assert(output_pitch > 0);

	// Should the image be inverted?
	if (inverted) {
		outrow += (height - 1) * output_pitch;		// Start at the bottom row
		output_pitch = (- output_pitch);			// Negate the pitch to go up
	}

	for (row = 0; row < height; row++) {
		outptr = outrow;
		for (column = 0; column < width; column++) {
			PIXEL value = rowptr[column];
			uint8_t luminance = SATURATE_8U(value);

			// Copy the luminance byte into all three channels
			*(outptr++) = luminance;
			*(outptr++) = luminance;
			*(outptr++) = luminance;

			// The last byte in a quad is zero
			if (format == COLOR_FORMAT_RGB32) *(outptr++) = 0;
		}
		rowptr += pitch;
		outrow += output_pitch;
	}
}

void ConvertImageToYUV(IMAGE *image, uint8_t *output_buffer, int32_t output_pitch, int format, bool inverted)
{
	int width = image->width;
	int height = image->height;
	int pitch = image->pitch/sizeof(PIXEL);
	int row, column;
	PIXEL *rowptr = image->band[0];
	uint8_t *outrow = output_buffer;
	uint8_t *outptr;

	// Compute positions of luminance and chrominance bytes within the YUV tuple
	int luma_offset = ((format&0xffff) == COLOR_FORMAT_YUYV) ? 0 : 1;
	int chroma_offset = ((format&0xffff) == COLOR_FORMAT_YUYV) ? 1 : 0;
	const int tuple_size = 2;

	// Color format YUV 4:2:0 is not supported yet
	assert((format&0xffff) == COLOR_FORMAT_YUYV || (format&0xffff) == COLOR_FORMAT_UYVY);

	// The output pitch should be a positive number before inversion
	assert(output_pitch > 0);

	// Frames with the YUV color format are not usually inverted
	assert(!inverted);

	// Should the image be inverted?
	if (inverted) {
		outrow += (height - 1) * output_pitch;		// Start at the bottom row
		output_pitch = (- output_pitch);			// Negate the pitch to go up
	}

	for (row = 0; row < height; row++) {
		outptr = outrow;
		for (column = 0; column < width; column++) {
			PIXEL value = rowptr[column];
			uint8_t luminance = SATURATE_8U(value);
			outptr[luma_offset] = luminance;			// Output the luminance value
			outptr[chroma_offset] = COLOR_CHROMA_ZERO;	// Output one chrominance channel
			outptr += tuple_size;						// Advance tuple output pointer
		}
		rowptr += pitch;
		outrow += output_pitch;
	}
}

#ifdef _WIN32

int ColorTableLength(LPBITMAPINFOHEADER lpbi)
{
	int num_colors = lpbi->biClrUsed;
	if (num_colors > 0) return num_colors;

	switch (lpbi->biBitCount)
	{
		case 1:
			num_colors = 2;
			break;

		case 4:
			num_colors = 16;
			break;

		case 8:
			num_colors = 256;
			break;

		case 16:
		case 24:
		case 32:
			num_colors = 0;
			break;

		default:
			assert(0);
			break;
	}

	return num_colors;
}

#endif

// Make a copy of a frame image in field format
#if _ALLOCATOR
IMAGE *CreateFieldImageFromFrame(ALLOCATOR *allocator, IMAGE *frame)
#else
IMAGE *CreateFieldImageFromFrame(IMAGE *frame)
#endif
{
	IMAGE *field;
	int field_width;
	int field_height;

	assert(frame != NULL);
	if (frame == NULL) return NULL;

	// Check that the frame is not in field format already
	assert(frame->format == IMAGE_FORMAT_FRAME);

	// Calculate the field dimensions
	field_height = frame->height/2;
	field_width = frame->width;

#if _ALLOCATOR
	field = CreateImage(allocator, field_width, field_height);
#else
	field = CreateImage(field_width, field_height);
#endif
	assert(field != NULL);
	if (field == NULL) return NULL;

	// Allocate the band for the odd field
	assert(field->band[BAND_INDEX_FIELD_ODD] == NULL);
#if _ALLOCATOR
	AllocateBand(allocator, field, BAND_INDEX_FIELD_ODD);
#else
	AllocateBand(field, BAND_INDEX_FIELD_ODD);
#endif
	assert(field->band[BAND_INDEX_FIELD_ODD] != NULL);

	// Split the frame into two even and odd fields
	SplitFrameIntoFields(frame, field);

	// Copy the scale factor into the even and odd fields
	field->scale[BAND_INDEX_FIELD_EVEN] = frame->scale[0];
	field->scale[BAND_INDEX_FIELD_ODD] = frame->scale[0];

	return field;
}

// Create an image data structure from planar video frame data
#if _ALLOCATOR
IMAGE *CreateImageFromPlanes(ALLOCATOR *allocator, uint8_t *data, int width, int height, int pitch, int format)
#else
IMAGE *CreateImageFromPlanes(uint8_t *data, int width, int height, int pitch, int format)
#endif
{
	// To be written
	assert(0);

	// Disable warnings about unused variables
	(void) data;
	(void) width;
	(void) height;
	(void) pitch;
	(void) format;

	return NULL;
}

void ConvertPackedToImage(uint8_t *data, int width, int height, int pitch, IMAGE *image)
{
	uint8_t *rowptr = data;
	PIXEL *outptr = image->band[0];
	int data_pitch = pitch;
	int image_pitch = image->pitch/sizeof(PIXEL);
	int row, column;

	for (row = 0; row < height; row++) {
		for (column = 0; column < width; column++) {
			PIXEL value = rowptr[2 * column];
			outptr[column] = SATURATE(value);
		}
		rowptr += data_pitch;
		outptr += image_pitch;
	}
}

// Split a frame into two fields
void SplitFrameIntoFields(IMAGE *frame, IMAGE *field)
{
	ROI roi = {frame->width, frame->height};
	PIXEL *input = frame->band[0];
	int row_offset = frame->pitch/sizeof(PIXEL);
	PIXEL *even = field->band[0];
	PIXEL *odd = field->band[1];

	// Check that the frame and field sizes are compatible
	assert(field->width == frame->width);
	assert(field->height == frame->height/2);

	// Check that both fields have been allocated
	assert(even != NULL);
	assert(odd != NULL);

	// Copy the even rows into the even field
	DownsampleHeight(input, frame->pitch, even, field->pitch, roi);

	// Copy the odd rows into the odd field
	input += row_offset;
	DownsampleHeight(input, frame->pitch, odd, field->pitch, roi);
}

// Interleave two fields into a single frame
void InterleaveFieldsIntoFrame(IMAGE *even_field, int even_band,
							   IMAGE *odd_field, int odd_band,
							   IMAGE *frame, int output_band)
{
	PIXEL *rowptr = frame->band[output_band];
	PIXEL *even_row = even_field->band[even_band];
	PIXEL *odd_row = odd_field->band[odd_band];
	int width = frame->width;
	int height = frame->height;
	int pitch = frame->pitch/sizeof(PIXEL);
	int even_pitch = even_field->pitch/sizeof(PIXEL);
	int odd_pitch = odd_field->pitch/sizeof(PIXEL);
	int row;

	// Check that the output image is the correct size
	assert(width == even_field->width);
	assert(width == odd_field->width);
	assert(height >= even_field->height);

	for (row = 0; row < height; row += 2)
	{
		// Copy the even row
		memcpy(rowptr, even_row, width * sizeof(PIXEL));
		rowptr += pitch;

		// Copy the odd row
		memcpy(rowptr, odd_row, width * sizeof(PIXEL));
		rowptr += pitch;

		even_row += even_pitch;
		odd_row += odd_pitch;
	}
}

void DownsampleWidth(PIXEL *imgInput, int pitchInput, PIXEL *imgOutput, int pitchOutput, ROI roi)
{
	PIXEL *pInputRow = imgInput;
	PIXEL *pOutputRow = imgOutput;
	int i, j;

	// Convert pitch from bytes to pixels
	pitchInput /= sizeof(PIXEL);
	pitchOutput /= sizeof(PIXEL);

	for (i = 0; i < roi.height; i++) {
		PIXEL *pOutput = pOutputRow;

		for (j = 0; j < roi.width; j += 2) {
			*(pOutput++) = pInputRow[j];
		}

		pInputRow += pitchInput;
		pOutputRow += pitchOutput;
	}
}

void DownsampleHeight(PIXEL *imgInput, int pitchInput, PIXEL *imgOutput, int pitchOutput, ROI roi)
{
	PIXEL *pInputRow = imgInput;
	PIXEL *pOutputRow = imgOutput;
	size_t sizeOutputRow = pitchOutput;		// Length of each row in bytes
	int i;

	// Convert pitch from bytes to pixels
	pitchInput /= sizeof(PIXEL);
	pitchOutput /= sizeof(PIXEL);

	for (i = 0; i < roi.height; i += 2) {
		memcpy(pOutputRow, pInputRow, sizeOutputRow);
		pInputRow += 2 * pitchInput;		// Skip the odd rows
		pOutputRow += pitchOutput;
	}
}

// Interleave the columns of the even and odd images
void InterleaveColumns(PIXEL *imgEven, int pitchEven,
					   PIXEL *imgOdd, int pitchOdd,
					   PIXEL *imgOutput, int pitchOutput,
					   ROI roi)
{
	PIXEL *pEvenRow = imgEven;
	PIXEL *pOddRow = imgOdd;
	PIXEL *pOutputRow = imgOutput;
	int row, column;

	// Convert pitch from bytes to pixels
	pitchEven /= sizeof(PIXEL);
	pitchOdd /= sizeof(PIXEL);
	pitchOutput /= sizeof(PIXEL);

	for (row = 0; row < roi.height; row++) {
		for (column = 0; column < roi.width; column++) {
			pOutputRow[2 * column] = pEvenRow[column];
			pOutputRow[2 * column + 1] = pOddRow[column];
		}
		pEvenRow += pitchEven;
		pOddRow += pitchOdd;
		pOutputRow += pitchOutput;
	}
}

// Interleave the rows of the even and odd images
void InterleaveRows(PIXEL *imgEven, int pitchEven,
					PIXEL *imgOdd, int pitchOdd,
					PIXEL *imgOutput, int pitchOutput,
					ROI roi)
{
	PIXEL *pEvenRow = imgEven;
	PIXEL *pOddRow = imgOdd;
	PIXEL *pOutputRow = imgOutput;
	int row;

	size_t sizeOutputRow = roi.width * sizeof(PIXEL);

	// Convert pitch from bytes to pixels
	pitchEven /= sizeof(PIXEL);
	pitchOdd /= sizeof(PIXEL);
	pitchOutput /= sizeof(PIXEL);

	for (row = 0; row < roi.height; row++)
	{
		// Copy the even row
		memcpy(pOutputRow, pEvenRow, sizeOutputRow);
		pEvenRow += pitchEven;
		pOutputRow += pitchOutput;

		// Copy the odd row
		memcpy(pOutputRow, pOddRow, sizeOutputRow);
		pOddRow += pitchOdd;
		pOutputRow += pitchOutput;
	}
}

void Expand8uTo16s(unsigned char *imgInput, int pitchInput, short *imgOutput, int pitchOutput, ROI roi)
{
	int row, column;

	// Convert pitch from bytes to words
	pitchOutput /= sizeof(short);

	for (row = 0; row < roi.height; row++)
	{
		// Expand each row from right to left to allow inplace computation
		for (column = roi.width - 1; column >= 0; column--)
		{
			// Copy eight bit integer without sign extension
			imgOutput[column] = imgInput[column];

			// Check the range of the result
			assert(0 <= imgOutput[column] && imgOutput[column] <= UCHAR_MAX);
		}
		imgInput += pitchInput;
		imgOutput += pitchOutput;
	}
}


#if _ALLOCATOR
PIXEL *CreateImageBuffer(ALLOCATOR *allocator, int pitch, int height, size_t *allocated_size)
#else
PIXEL *CreateImageBuffer(int pitch, int height, size_t *allocated_size)
#endif
{
	size_t size = abs(pitch) * height;
	PIXEL *buffer;

#if 1
	// Some paths through the code may need extra buffer space
	size += 32 * pitch;
	size *= 2; // Converting RGB24 to RGB48 requires double the buffer size
#endif

	// Round up the buffer allocation to an integer number of cache lines
	size = ALIGN(size, _CACHE_LINE_SIZE);

	// Allocate a buffer aligned to the cache line size
#if _ALLOCATOR
	buffer = (PIXEL *)AllocAligned(allocator, size, _CACHE_LINE_SIZE);
#else
	buffer = (PIXEL *)MEMORY_ALIGNED_ALLOC(size, _CACHE_LINE_SIZE);
#endif

	// Return the allocated size
	if (allocated_size != NULL) {
		if (buffer != NULL) *allocated_size = size;
		else *allocated_size = 0;
	}

	// Return the pointer to the allocated buffer
	return buffer;
}


#if _ALLOCATOR
PIXEL *ReallocImageBuffer(ALLOCATOR *allocator, PIXEL *buffer, int pitch, int height, size_t *allocated_size)
#else
PIXEL *ReallocImageBuffer(PIXEL *buffer, int pitch, int height, size_t *allocated_size)
#endif
{
#if _ALLOCATOR
	DeleteImageBuffer(allocator, buffer);
	buffer = CreateImageBuffer(allocator, pitch, height, allocated_size);
#else
	DeleteImageBuffer(buffer);
	buffer = CreateImageBuffer(pitch, height, allocated_size);
#endif
	// Return the pointer to the allocated buffer
	return buffer;
}


#if _ALLOCATOR
void DeleteImageBuffer(ALLOCATOR *allocator, PIXEL *buffer)
#else
void DeleteImageBuffer(PIXEL *buffer)
#endif
{
	if (buffer != NULL) {
#if _ALLOCATOR
		FreeAligned(allocator, buffer);
#else
		MEMORY_ALIGNED_FREE(buffer);
#endif
	}
}

void InitImageBandStatistics(IMAGE *image, int band_index)
{
	if (image->band[band_index] == NULL) {
		memset(&image->stats[band_index], 0, sizeof(image->stats[0]));
		return;
	}

	image->stats[band_index].maxPixel = PIXEL_MINIMUM;
	image->stats[band_index].minPixel = PIXEL_MAXIMUM;
	image->stats[band_index].cntNegative = 0;
	image->stats[band_index].cntPositive = 0;
	image->stats[band_index].cntZero = 0;
}

void InitImageStatistics(IMAGE *image)
{
	int band;

	for (band = 0; band < IMAGE_NUM_BANDS; band++)
		InitImageBandStatistics(image, band);
}

void ComputePixelStatistics(PIXEL *image, int width, int height, int pitch, IMAGE_STATISTICS_T *stats)
{
	PIXEL *rowptr = image;
	int row, column;

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	for (row = 0; row < height; row++)
	{
		for (column = 0; column < width; column++)
		{
			PIXEL value = rowptr[column];
			if (value < stats->minPixel) stats->minPixel = value;
			if (value > stats->maxPixel) stats->maxPixel = value;
			if (value < 0)
				stats->cntNegative++;
			else if (value > 0)
				stats->cntPositive++;
			else
				stats->cntZero++;
		}

		// Advance to the next row
		rowptr += pitch;
	}
}

void ComputeImageStatistics(IMAGE *image)
{
	int i;

	// Initialize the counters
	InitImageStatistics(image);

	for (i = 0; i < IMAGE_NUM_BANDS; i++)
	{
		if (image->band[i] != NULL) {
			int width = image->width;
			int height = image->height;
			ComputePixelStatistics(image->band[i], width, height, image->pitch, &image->stats[i]);
		}
	}
}

void ComputeImageBandStatistics(IMAGE *image, int band_index)
{
	// Initialize the counters
	InitImageBandStatistics(image, band_index);

	if (image->band[band_index] != NULL) {
		int width = image->width;
		int height = image->height;
		ComputePixelStatistics(image->band[band_index], width, height, image->pitch,
							   &image->stats[band_index]);
	}
}

#if 0
HISTOGRAM *CreateImageHistogram(IMAGE *image, int band, int bucket_width)
{
#if _ALLOCATOR
	//TODO: Modify this routine to pass an allocator as the first argument
	ALLOCATOR *allocator = decoder->allocator;
#endif

	PIXEL minimum, maximum;
	int num_buckets;
	size_t size;
	HISTOGRAM *histogram;
	static char buffer[128 * 1024];
	//_CrtMemState s1, s2;

	//_CrtDumpMemoryLeaks();
	//_CrtMemCheckpoint(&s1);
	//_CrtMemDumpStatistics(&s1);

	minimum = image->stats[band].minPixel;
	maximum = image->stats[band].maxPixel;
	num_buckets = (maximum - minimum + bucket_width) / bucket_width;
	size = sizeof(HISTOGRAM) + (num_buckets - 1) * sizeof(BUCKET);
#if _ALLOCATOR
	histogram = (HISTOGRAM *)Alloc(allocator, size);
#elif 1
	histogram = (HISTOGRAM *)MEMORY_ALLOC(size);
#else
	histogram = (HISTOGRAM *)&buffer;
#endif

	//_CrtMemCheckpoint(&s2);
	//_CrtMemDumpStatistics(&s2);

	if (histogram != NULL) {
		int i;

		histogram->image = image;
		histogram->band = band;
		histogram->length = num_buckets;
		histogram->minimum = minimum;
		histogram->maximum = maximum;
		histogram->width = bucket_width;

		for (i = 0; i < num_buckets; i++)
			histogram->bucket[i] = 0;
	}

	return histogram;
}
#endif

#if 0
void DeleteImageHistogram(HISTOGRAM *histogram)
{
#if _ALLOCATOR
	//TODO: Modify this routine to pass an allocator as the first argument
	ALLOCATOR *allocator = decoder->allocator;
#endif

#if _ALLOCATOR
	if (histogram != NULL) Free(allocator, histogram);
#else
	if (histogram != NULL) MEMORY_FREE(histogram);
#endif
}
#endif

void IncrementBucket(HISTOGRAM *histogram, PIXEL value)
{
	int index;

	// Check for invalid arguments
	assert(histogram != NULL);
	assert(histogram->minimum <= value && value <= histogram->maximum);

	// Compute the bucket index and increment that bucket
	index = (value - histogram->minimum) / histogram->width;
	histogram->bucket[index]++;
}

PIXEL BucketValue(HISTOGRAM *histogram, int bucket)
{
	return (PIXEL)((bucket * histogram->width + histogram->minimum));
}

#define HISTOGRAM_LENGTH 50

#if 0
HISTOGRAM *ComputeImageHistogram(IMAGE *image, int band)
{
	HISTOGRAM *histogram;
	PIXEL minimum, maximum;
	int bucket_width;
	int width, height, pitch;
	PIXEL *rowptr;
	int row, column;

	// Make sure that the image statistics are current
	ComputeImageBandStatistics(image, band);

	// Calculate the number of histogram buckets
	maximum = image->stats[band].maxPixel;
	minimum = image->stats[band].minPixel;

#if 0
	bucket_width = (maximum - minimum)/HISTOGRAM_LENGTH;
	if (bucket_width == 0) bucket_width = 1;
#else
	// Force smallest bucket width for gathering entropy coding statistics
	bucket_width = 1;
#endif

	// Allocate the histogram
	histogram = CreateImageHistogram(image, band, bucket_width);
	assert(histogram != NULL);
	if (histogram == NULL) return NULL;

	// Calculate the image dimensions adjusted for filtering
	height = image->height;
	width = image->width;
	rowptr = image->band[band];
	pitch = image->pitch/sizeof(PIXEL);

	for (row = 0; row < height; row++) {
		for (column = 0; column < width; column++) {
			PIXEL pixel = rowptr[column];
			IncrementBucket(histogram, pixel);
		}
		rowptr += pitch;
	}

	return histogram;
}
#endif

void PrintImageHistogram(HISTOGRAM *histogram, FILE *file, char *label)
{
	int num_buckets = histogram->length;
	//PIXEL minimum = histogram->minimum;
	IMAGE *image = histogram->image;
	int level;
	uint32_t total = 0;
	int i;

	if (image != NULL) level = image->level;
	else level = 0;

	fprintf(file, "%s image histogram: %d buckets, minimum: %d, maximum: %d\n",
			label, histogram->length, histogram->minimum, histogram->maximum);

	fprintf(file, "Image: 0x%p, level: %d, band: %d\n",
			image, level, histogram->band);

	for (i = 0; i < num_buckets; i++) {
		int count = histogram->bucket[i];
		fprintf(file, "%5d %5d\n", BucketValue(histogram, i), count);
		total += count;
	}

	fprintf(file, "Total: %uld\n", total);
}


void FillImageRandom(IMAGE *image, int nominal, int range, unsigned int seed)
{
	PIXEL *rowptr = image->band[0];
	int width = image->width;
	int height = image->height;
	int pitch = image->pitch;
	int chrominance = 128;
	int row, column;

	// Seed the random number generator
	srand(seed);

	// Convert the pitch to units of pixels
	pitch /= sizeof(PIXEL);

	// Set the chrominance to a constant value and luminance to random variation
	for (row = 0; row < height; row++)
	{
		PIXEL *colptr = rowptr;

		for (column = 0; column < width; column++)
		{
			int luminance = nominal + (rand() % range) - range/2;;

			*(colptr++) = luminance;
			*(colptr++) = chrominance;
		}

		// Advance to the next row
		rowptr += pitch;
	}
}

void FillPixelMemory(PIXEL *array, int length, PIXEL value)
{
	int i;

	for (i = 0; i < length; i++)
	{
		array[i] = value;
	}
}

int ImageBandScale(IMAGE *image, int band)
{
	int maximum = PIXEL_MINIMUM;
	int width = image->width;
	int height = image->height;
	int pitch = image->pitch/sizeof(PIXEL);
	PIXEL *rowptr = image->band[band];
	int row, column;

	for (row = 0; row < height; row++) {
		for (column = 0; column < width; column++) {
			if (rowptr[column] > maximum) maximum = rowptr[column];
		}
		rowptr += pitch;
	}

	return (maximum / 255);
}

// Find the index of the first nonzero pixel in a row of pixels
int FindNonZero(PIXEL *rowptr, int length)
{
	PIXEL *pixptr = rowptr;
	int index = 0;

#if (0 && XMMOPT)

	int stepsize = 8;

	if (length >= stepsize)
	{
		__m64 *quad_ptr = (__m64 *)pixptr;

		// Table that maps zero mask to position of first nonzero value
		static const unsigned char nonzero_index[] = {
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 7,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 8
		};

		// Use MMX instructions to check eight coefficients at once
		while ((length - index) >= stepsize)
		{
			__m64 quad1_pi16;
			__m64 quad2_pi16;
			__m64 run_pu8;
			__m64 mask_pu8;
			int mask;
			int count;

			// Read eight pixels
			quad1_pi16 = *(quad_ptr++);
			quad2_pi16 = *(quad_ptr++);

			// Pack the pixels into bytes
			run_pu8 = _mm_packs_pi16(quad1_pi16, quad2_pi16);

			// Compare each pixel with zero
			mask_pu8 = _mm_cmpeq_pi8(run_pu8, _mm_setzero_si64());

			// Create an eight bit mask
			mask = _mm_movemask_pi8(mask_pu8);

			// Lookup the count to the first nonzero value in the run
			count = nonzero_index[mask];

			// Advance the index into the row of pixels
			index += count;

			// Terminate the loop if the run was not all zeros
			if (count < stepsize) {
				//_mm_empty();
				goto finish;
			}
		}

		//_mm_empty();	// Clear the mmx register state

		// Update the pixel pointer with the result of the fast search
		pixptr = (PIXEL *)quad_ptr;
	}

#endif
#if 0

	// Search the rest of the row by int32_t words for a nonzero value
	{
		const int group_step = sizeof(DWORD) / sizeof(PIXEL);
		int group_length = length - (length % group_step);

		for (; index < group_length; index += group_step)
		{
			DWORD group = *((DWORD *)pixptr);
			if (group != 0) break;
			else pixptr += group_step;
		}
	}

#elif 0

	// Search the rest of the row by longint32_t words for a nonzero value
	{
		const int group_step = sizeof(LONGLONG) / sizeof(PIXEL);
		int group_length = length - (length % group_step);

		for (; index < group_length; index += group_step)
		{
			LONGint32_t group = *((LONGint32_t *)pixptr);
			if (group != 0) break;
			else pixptr += group_step;
		}
	}

#endif

	// Search the rest of the row for a nonzero value
	for (; index < length; index++)
		if (*(pixptr++) != 0) break;

//finish:
	// Either the search went past the end of the row or a nonzero value was found
	assert((index == length) || ((index < length) && (rowptr[index] != 0)));

	// Return the index to the first non-zero pixel in the row
	// or the length of the row if the entire row was zero
	return index;
}

// Find the index of the first nonzero pixel in a row of pixels
int FindNonZeroPacked(PIXEL8S *rowptr, int length)
{
	PIXEL8S *pixptr = rowptr;
	int index = 0;

#if (0 && XMMOPT)

	int stepsize = 8;

	if (length >= stepsize)
	{
		__m64 *quad_ptr = (__m64 *)pixptr;

		// Table that maps zero mask to position of first nonzero value
		static const unsigned char nonzero_index[] = {
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 7,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
			0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 8
		};

		// Use MMX instructions to check eight coefficients at once
		while ((length - index) >= stepsize)
		{
			__m64 quad_pi8;
			__m64 mask_pi8;
			int mask;
			int count;

			// Read eight pixels
			quad_pi8 = *(quad_ptr++);

			// Compare each pixel with zero
			mask_pi8 = _mm_cmpeq_pi8(quad_pi8, _mm_setzero_si64());

			// Create an eight bit mask
			mask = _mm_movemask_pi8(mask_pi8);

			// Lookup the count to the first nonzero value in the run
			count = nonzero_index[mask];

			// Advance the index into the row of pixels
			index += count;

			// Terminate the loop if the run was not all zeros
			if (count < stepsize) {
				//_mm_empty();
				goto finish;
			}
		}

		//_mm_empty();	// Clear the mmx register state

		// Update the pixel pointer with the result of the fast search
		pixptr = (PIXEL8S *)quad_ptr;
	}

#endif

	// Search the rest of the row for a nonzero value
	for (; index < length; index++)
		if (*(pixptr++) != 0) break;

//finish:
	// Either the search went past the end of the row or a nonzero value was found
	assert((index == length) || ((index < length) && (rowptr[index] != 0)));

	// Return the index to the first non-zero pixel in the row
	// or the length of the row if the entire row was zero
	return index;
}

int32_t CompareImages(IMAGE *image1, IMAGE *image2, PIXEL *error, int pitch)
{
	int32_t sum = 0;
	int width = min(image1->width, image2->width);
	int height = min(image1->height, image2->height);
	int pitch1 = image1->pitch/sizeof(PIXEL);
	int pitch2 = image2->pitch/sizeof(PIXEL);
	PIXEL *rowptr1 = image1->band[0];
	PIXEL *rowptr2 = image2->band[0];
	int row, column;

	// Convert the error pitch to units of pixels
	pitch /= sizeof(PIXEL);

	for (row = 0; row < height; row++) {
		for (column = 0; column < width; column++) {
			int32_t delta = rowptr2[column] - rowptr1[column];
			if (error != NULL) error[column] = delta;
			sum += abs(delta);
		}
		rowptr1 += pitch1;
		rowptr2 += pitch2;
		if (error != NULL) error += pitch;
	}

	return sum;
}

int32_t CompareImageBands16s(IMAGE *image1, int band1, IMAGE *image2, int band2, PIXEL *residual, int pitch)
{
	int32_t sum = 0;
	int width = min(image1->width, image2->width);
	int height = min(image1->height, image2->height);
	int pitch1 = image1->pitch/sizeof(PIXEL);
	int pitch2 = image2->pitch/sizeof(PIXEL);
	PIXEL *rowptr1 = image1->band[band1];
	PIXEL *rowptr2 = image2->band[band2];
	int row, column;

	// Convert pitch to units of pixels
	pitch /= sizeof(PIXEL);

	for (row = 0; row < height; row++) {
		for (column = 0; column < width; column++) {
			int32_t delta = rowptr2[column] - rowptr1[column];
			residual[column] = delta;
			sum += abs(delta);
		}
		rowptr1 += pitch1;
		rowptr2 += pitch2;
		residual += pitch;
	}

	return sum;
}

bool CompareImageBufferConstantYUV(uint8_t *buffer, int length, int y_value, int u_value, int v_value)
{
	uint8_t *bufptr = buffer;
	bool result = true;
	int i;

	for (i = 0; i < length; i += 2)
	{
		int y1 = *(bufptr++);
		int u  = *(bufptr++);
		int y2 = *(bufptr++);
		int v  = *(bufptr++);

		if (y1 != y_value || y2 != v_value) {
			assert(0);
			result = false;
			break;
		}

		if (u != u_value) {
			assert(0);
			result = false;
			break;
		}

		if (v != v_value) {
			assert(0);
			result = false;
			break;
		}
	}

	return result;
}


void OutputRGB(unsigned char *outbuffer, IMAGE *waveletY, IMAGE *waveletV, IMAGE *waveletU, int scale)
{
	PIXEL *band_rowY = waveletY->band[0];
	PIXEL *band_rowU = waveletU->band[0];
	PIXEL *band_rowV = waveletV->band[0];
	int band_width = waveletY->width;
	int band_height = waveletY->height;
	int band_pitchY = waveletY->pitch/sizeof(PIXEL);
	int band_pitchU = waveletU->pitch/sizeof(PIXEL);
	int band_pitchV = waveletV->pitch/sizeof(PIXEL);
	//int lowpass_border = wavelet->lowpass_border;
	int first_row, first_column;
	int last_row, last_column;
	int bottom_row, left_column;
	int row;
	int column;
	//int offset;
	FILE *file;
	unsigned char *buffer = outbuffer;

	char name[_MAX_PATH];
	//int levelshift = 0;
	//int total = 0;

	int output_width;
	int output_height;
	int PPM = 0;

	if(outbuffer == NULL)
		PPM = 1;

	// Calculate the true last row and column
	left_column = band_width - 1;
	bottom_row = band_height - 1;

	first_row = 0;
	first_column = 0;
	last_row = band_height - 1;
	last_column = band_width - 1;

	if (last_row > bottom_row) last_row = bottom_row;
	if (last_column > left_column) last_column = left_column;

	output_width = last_column - first_column + 1;
	output_height = last_row - first_row + 1;

	if(PPM)
	{
		int err = 0;
#ifdef _WIN32
		sprintf_s(name, sizeof(name), "C:\\Cedoc\\Preview%dx%d.ppm", output_width, output_height);
#else
		sprintf(name, "C:\\Cedoc\\Preview%dx%d.ppm", output_width, output_height);
#endif

#ifdef _WIN32
		err = fopen_s(&file, name, "wb");
#else
		file = fopen(name,"wb");
#endif

		if (err != 0 || file == NULL) {
			return;
		}

		fprintf(file, "P6\n# CREATOR: DAN\n%d %d\n255\n", output_width, output_height);
	}


	{
		int Y,Y2,U,V;
		int R,G,B;
		unsigned char *ptr, line[2048*3];


		PIXEL *rowptrY = band_rowY;
		PIXEL *rowptrU = band_rowU;
		PIXEL *rowptrV = band_rowV;

		for (row = first_row; row <= last_row; row++)
		{
			if(PPM)
				ptr = line;
			else
				ptr = buffer;

			for (column = first_column; column <= last_column; column+=2)
			{
				Y = rowptrY[column] >> scale;
				Y2 = rowptrY[column+1] >> scale;
				U = rowptrU[column>>1] >> scale;
				V = rowptrV[column>>1] >> scale;

				/*
					if(Y<16) Y=16;
					if(Y>235) Y=235;
					if(Y2<16) Y2=16;
					if(Y2>235) Y2=235;
					if(U<16) U=16;
					if(U>240) U=240;
					if(V<16) V=16;
					if(V>240) V=240;
				*/

				Y = Y - 16;
				Y2 = Y2 - 16;
				U = U - 128;
				V = V - 128;

				Y = Y * 149;
				Y2 = Y2 * 149;

				R = (Y           + 204 * V) >> 7;
				G = (Y -  50 * U - 104 * V) >> 7;
				B = (Y + 258 * U) >> 7;

				if(R>255) R=255;
				if(R<0) R=0;
				if(G>255) G=255;
				if(G<0) G=0;
				if(B>255) B=255;
				if(B<0) B=0;

#if 0	// output BGR for Windows DIB
				*ptr++ = R;
				*ptr++ = G;
				*ptr++ = B;
#else

				*ptr++ = B;
				*ptr++ = G;
				*ptr++ = R;
#endif

				R = (Y2           + 204 * V) >> 7;
				G = (Y2 -  50 * U - 104 * V) >> 7;
				B = (Y2 + 258 * U) >> 7;

				if(R>255) R=255;
				if(R<0) R=0;
				if(G>255) G=255;
				if(G<0) G=0;
				if(B>255) B=255;
				if(B<0) B=0;

#if 0	// output BGR for Windows DIB
				*ptr++ = R;
				*ptr++ = G;
				*ptr++ = B;
#else
				*ptr++ = B;
				*ptr++ = G;
				*ptr++ = R;
#endif
			}

			if(PPM)
				fwrite(line,1,output_width*3,file);
			else
			{
			//	CopyMemory(buffer, line, output_width*3);
				buffer += output_width*3;
			}

			rowptrY += band_pitchY;
			rowptrU += band_pitchU;
			rowptrV += band_pitchV;
		}
	}

	if(PPM) fclose(file);
}




#if _DEBUG

void DumpPGM(char *label, IMAGE *image, SUBIMAGE *subimage)
{
	PIXEL *image_row = image->band[0];
	int image_width = image->width;
	int image_height = image->height;
	int image_pitch = image->pitch/sizeof(PIXEL);
	//int lowpass_border = image->lowpass_border;
	int first_row, first_column;
	int last_row, last_column;
	int bottom_row, left_column;
	int row, column;
	FILE *file;
	int err = 0;

	char name[_MAX_PATH];
	int levelshift=0, min=40000, max=0, total=0;
	float lumashift;
	float lumashift2;

	// Calculate the true last row and column
	left_column = image_width - 1;
	bottom_row = image_height - 1;

	if (subimage == NULL)
	{
		first_row = 0;
		first_column = 0;
		last_row = image_height - 1;
		last_column = image_width - 1;
	}
	else
	{
		if (subimage->row < 0) first_row = image_height + subimage->row;
		else first_row = subimage->row;

		if (subimage->column < 0) first_column = image_width + subimage->column;
		else first_column = subimage->column;

		if (subimage->height == 0) last_row = image_height - 1;
		else last_row = first_row + subimage->height - 1;

		if (subimage->width == 0) last_column = image_width - 1;
		else last_column = first_column + subimage->width - 1;

		image_row += (first_row * image_pitch);
	}

	if (last_row > bottom_row) last_row = bottom_row;
	if (last_column > left_column) last_column = left_column;

	//fprintf (file, "\n%s, Level: %d:\n", label, image->level);

	//autolevel
	{
		int lmax = 0;
		PIXEL *ptr = image_row;

		for (row = first_row; row <= last_row; row++)
		{
			for (column = first_column; column <= last_column; column++)
			{
				total += ptr[column];
				if(ptr[column] > max) max = ptr[column];
				if(ptr[column] < min) min = ptr[column];
			}
			ptr += image_pitch;
		}

		lmax = max;
		while(lmax > 265)
		{
			levelshift++;
			lmax >>= 1;
		}
	}

	//calc luma shift
	{
		PIXEL *ptr = image_row;
		int count = 0,total=0, totalshifted=0;

		for (row = first_row; row <= last_row; row++)
		{
			for (column = first_column; column <= last_column; column++)
			{
				totalshifted += ptr[column]>>levelshift;
				total += ptr[column];
				count ++;
			}
			ptr += image_pitch;
		}
		lumashift = (float)total / (float)count;
		lumashift2 = (float)totalshifted / (float)count;
	}

#ifdef _WIN32
	sprintf_s(name, sizeof(name), "C:\\Cedoc\\%s%dx%d.pgm", label, last_column - first_column + 1, last_row - first_row + 1);
#else
	sprintf(name, "C:\\Cedoc\\%s%dx%d.pgm", label, last_column - first_column + 1, last_row - first_row + 1);
#endif

#ifdef _WIN32
	err = fopen_s(&file, name, "w");
#else
	file = fopen(name,"w");
#endif

	if (err || file == NULL) {
		return;
	}

	fprintf(file, "P2\n# CREATOR: DAN min=%d max=%d lumashift=%5.3f, %5.3f\n%d %d\n255\n", min,max,lumashift,lumashift2,last_column - first_column + 1, last_row - first_row + 1);


	for (row = first_row; row <= last_row; row++) {
		for (column = first_column; column <= last_column; column++) {
			int val = image_row[column]>>levelshift;
			if(val > 255) val = 255;
			fprintf(file, "%d\n", image_row[column]>>levelshift);
		}
		image_row += image_pitch;
	}

	fclose(file);
}

void DumpBandPGM(char *label, IMAGE *wavelet, int band, SUBIMAGE *subimage)
{
	PIXEL *band_row = wavelet->band[band];
	int band_width = wavelet->width;
	int band_height = wavelet->height;
	int band_pitch = wavelet->pitch/sizeof(PIXEL);
	//int lowpass_border = wavelet->lowpass_border;
	int first_row, first_column;
	int last_row, last_column;
	int bottom_row, left_column;
	int row;
	int column;
	int offset;
	FILE *file;
	int err = 0;

	char name[_MAX_PATH];
	int levelshift = 0;
	int min = PIXEL_MAX;
	int max = PIXEL_MIN;
	int total = 0;

	int output_width;
	int output_height;

	//float lumashift;
	//float lumashift2;

	// Calculate the true last row and column
	left_column = band_width - 1;
	bottom_row = band_height - 1;

	if (subimage == NULL)
	{
		first_row = 0;
		first_column = 0;
		last_row = band_height - 1;
		last_column = band_width - 1;
	}
	else
	{
		if (subimage->row < 0) first_row = band_height + subimage->row;
		else first_row = subimage->row;

		if (subimage->column < 0) first_column = band_width + subimage->column;
		else first_column = subimage->column;

		if (subimage->height == 0) last_row = band_height - 1;
		else last_row = first_row + subimage->height - 1;

		if (subimage->width == 0) last_column = band_width - 1;
		else last_column = first_column + subimage->width - 1;

		band_row += (first_row * band_pitch);
	}

	if (last_row > bottom_row) last_row = bottom_row;
	if (last_column > left_column) last_column = left_column;

	output_width = last_column - first_column + 1;
	output_height = last_row - first_row + 1;

	//fprintf (file, "\n%s, Level: %d:\n", label, wavelet->level);

	//autolevel
	{
		int lmax = 0,cc=0;
		PIXEL *rowptr = band_row;

		for (row = first_row; row <= last_row; row++)
		{
			for (column = first_column; column <= last_column; column++)
			{
				total += rowptr[column];cc++;
				if (rowptr[column] > max) max = rowptr[column];
				if (rowptr[column] < min) min = rowptr[column];
			}
			rowptr += band_pitch;
		}

		if (min < 0)
		{
			int amin = abs(min);
			offset = 128 + (total/cc);
			if (amin > max) lmax = 2*amin;
			else lmax = 2*max;
		}
		else
		{
			offset = 0;
			lmax = max;// - min;
		}

		while (lmax > 127)
		{
			levelshift++;
			lmax >>= 1;
		}
	}

	//calc luma shift
	{
		PIXEL *rowptr = band_row;
		int count = 0;
		int total = 0;

		for (row = first_row; row <= last_row; row++)
		{
			for (column = first_column; column <= last_column; column++)
			{
				int value = rowptr[column];
				if (value >= 0)
				{
					value = (value >> levelshift) + offset;
				}
				else
				{
					value = abs(value);
					value = offset - (value >> levelshift);
				}

				total += rowptr[column];
				count ++;
			}

			rowptr += band_pitch;
		}

		//lumashift = (float)total / (float)count;
		//lumashift2 = (float)totalshifted / (float)count;
	}

	//sprintf(name, "C:\\Cedoc\\%s%dx%d-l%d.pgm", label, output_width, output_height, levelshift);
#ifdef _WIN32
	sprintf_s(name, sizeof(name), "C:\\Cedoc\\%s%dx%d.pgm", label, output_width, output_height);
#else
	sprintf(name, "C:\\Cedoc\\%s%dx%d.pgm", label, output_width, output_height);
#endif

#ifdef _WIN32
	err = fopen_s(&file, name, "w");
#else
	file = fopen(name, "w");
#endif

	if (err || file == NULL) {
		return;
	}

	fprintf(file, "P2\n# CREATOR: DAN min=%d max=%d\n%d %d\n255\n", min, max, output_width, output_height);

	{

		PIXEL *rowptr = band_row;
		for (row = first_row; row <= last_row; row++)
		{
			for (column = first_column; column <= last_column; column++)
			{
				int output = rowptr[column];
				if (output >= 0)
				{
					output = (output >> levelshift) + offset;
				}
				else
				{
					output = abs(output);
					output = offset - (output >> levelshift);
				}
			//	assert(0 <= output && output <= 255);

				fprintf(file, "%d\n", output);
			}

			rowptr += band_pitch;
		}
	}

	fclose(file);
}

void DumpBandSignPGM(char *label, IMAGE *wavelet, int band, SUBIMAGE *subimage)
{
	PIXEL *band_row = wavelet->band[band];
	int band_width = wavelet->width;
	int band_height = wavelet->height;
	int band_pitch = wavelet->pitch/sizeof(PIXEL);
	//int lowpass_border = wavelet->lowpass_border;
	int first_row, first_column;
	int last_row, last_column;
	int bottom_row, left_column;
	int row;
	int column;
	FILE *file;
	int err = 0;

	char name[_MAX_PATH];
	int levelshift = 0;
	int min = PIXEL_MAX;
	int max = PIXEL_MIN;
	int total = 0;

	int output_width;
	int output_height;

	// Calculate the true last row and column
	left_column = band_width - 1;
	bottom_row = band_height - 1;

	if (subimage == NULL)
	{
		first_row = 0;
		first_column = 0;
		last_row = band_height - 1;
		last_column = band_width - 1;
	}
	else
	{
		if (subimage->row < 0) first_row = band_height + subimage->row;
		else first_row = subimage->row;

		if (subimage->column < 0) first_column = band_width + subimage->column;
		else first_column = subimage->column;

		if (subimage->height == 0) last_row = band_height - 1;
		else last_row = first_row + subimage->height - 1;

		if (subimage->width == 0) last_column = band_width - 1;
		else last_column = first_column + subimage->width - 1;

		band_row += (first_row * band_pitch);
	}

	if (last_row > bottom_row) last_row = bottom_row;
	if (last_column > left_column) last_column = left_column;

	output_width = last_column - first_column + 1;
	output_height = last_row - first_row + 1;

#ifdef _WIN32
	sprintf_s(name, sizeof(name), "C:\\Cedoc\\%s%dx%d.pgm", label, output_width, output_height);
#else
	sprintf(name, "C:\\Cedoc\\%s%dx%d.pgm", label, output_width, output_height);
#endif

#ifdef _WIN32
	err = fopen_s(&file, name, "w");
#else
	file = fopen(name,"w");
#endif

	if (err || file == NULL) {
		return;
	}

	fprintf(file, "P2\n# CREATOR: DAN min=0 max=255\n%d %d\n255\n", output_width, output_height);

	for (row = first_row; row <= last_row; row++)
	{
		for (column = first_column; column <= last_column; column++)
		{
			int output = band_row[column];
			int sign = (output > 0) ? 255 : 0;
			fprintf(file, "%d\n", sign);
		}

		band_row += band_pitch;
	}

	fclose(file);
}

void DumpImage(char *label, IMAGE *image, SUBIMAGE *subimage, FILE *file)
{
	PIXEL *image_row = image->band[0];
	int image_width = image->width;
	int image_height = image->height;
	int image_pitch = image->pitch/sizeof(PIXEL);
	//int lowpass_border = image->lowpass_border;
	int first_row, first_column;
	int last_row, last_column;
	int bottom_row, left_column;
	int row, column;

	if (file == NULL) return;

	// Calculate the true last row and column
	left_column = image_width /* - lowpass_border */ - 1;
	bottom_row = image_height /* - lowpass_border */ - 1;

	if (subimage == NULL)
	{
		first_row = 0;
		first_column = 0;
		last_row = image_height - 1;
		last_column = image_width - 1;
	}
	else
	{
		if (subimage->row < 0) first_row = image_height + subimage->row;
		else first_row = subimage->row;

		if (subimage->column < 0) first_column = image_width + subimage->column;
		else first_column = subimage->column;

		if (subimage->height == 0) last_row = image_height - 1;
		else last_row = first_row + subimage->height - 1;

		if (subimage->width == 0) last_column = image_width - 1;
		else last_column = first_column + subimage->width - 1;

		image_row += (first_row * image_pitch);
	}

	if (last_row > bottom_row) last_row = bottom_row;
	if (last_column > left_column) last_column = left_column;

	fprintf (file, "\n%s, Level: %d:\n", label, image->level);
	for (row = first_row; row <= last_row; row++) {
		for (column = first_column; column <= last_column; column++) {
			fprintf(file, "%5d", image_row[column]);
		}
		fprintf(file, "\n");
		image_row += image_pitch;
	}
}

void DumpImage8u(char *label, IMAGE *image, SUBIMAGE *subimage, FILE *file)
{
	PIXEL8U *image_row = (PIXEL8U *)(image->band[0]);
	int image_width = image->width;
	int image_height = image->height;
	int image_pitch = image->pitch/sizeof(PIXEL8U);
	//int lowpass_border = image->lowpass_border;
	int first_row, first_column;
	int last_row, last_column;
	int bottom_row, left_column;
	int row, column;

	if (file == NULL) return;

	// Calculate the true last row and column
	left_column = image_width /* - lowpass_border */ - 1;
	bottom_row = image_height /* - lowpass_border */ - 1;

	if (subimage == NULL)
	{
		first_row = 0;
		first_column = 0;
		last_row = image_height - 1;
		last_column = image_width - 1;
	}
	else
	{
		if (subimage->row < 0) first_row = image_height + subimage->row;
		else first_row = subimage->row;

		if (subimage->column < 0) first_column = image_width + subimage->column;
		else first_column = subimage->column;

		if (subimage->height == 0) last_row = image_height - 1;
		else last_row = first_row + subimage->height - 1;

		if (subimage->width == 0) last_column = image_width - 1;
		else last_column = first_column + subimage->width - 1;

		image_row += (first_row * image_pitch);
	}

	if (last_row > bottom_row) last_row = bottom_row;
	if (last_column > left_column) last_column = left_column;

	fprintf (file, "\n%s, Level: %d:\n", label, image->level);
	for (row = first_row; row <= last_row; row++) {
		for (column = first_column; column <= last_column; column++) {
			fprintf(file, "%5d", image_row[column]);
		}
		fprintf(file, "\n");
		image_row += image_pitch;
	}
}

void DumpArray(char *label, PIXEL *array, int width, int height, int pitch, FILE *file)
{
	PIXEL *rowptr = array;
	int row, column;

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	if (file == NULL) return;

	fprintf (file, "\n%s:\n", label);
	for (row = 0; row < height; row++) {
		for (column = 0; column < width; column++) {
			fprintf(file, "%5d", rowptr[column]);
		}
		fprintf(file, "\n");
		rowptr += pitch;
	}
}

void DumpArray8u(char *label, PIXEL8U *array, int width, int height, int pitch, FILE *file)
{
	PIXEL8U *rowptr = array;
	int row, column;

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL8U);

	if (file == NULL) return;

	fprintf (file, "\n%s:\n", label);
	for (row = 0; row < height; row++) {
		for (column = 0; column < width; column++) {
			fprintf(file, "%5d", rowptr[column]);
		}
		fprintf(file, "\n");
		rowptr += pitch;
	}
}

void DumpArray8s(char *label, PIXEL8S *array, int width, int height, int pitch, FILE *file)
{
	PIXEL8S *rowptr = array;
	int row, column;

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL8S);

	if (file == NULL) return;

	fprintf (file, "\n%s:\n", label);
	for (row = 0; row < height; row++) {
		for (column = 0; column < width; column++) {
			fprintf(file, "%5d", rowptr[column]);
		}
		fprintf(file, "\n");
		rowptr += pitch;
	}
}

void DumpArray16s(char *label, PIXEL16S *array, int width, int height, int pitch, FILE *file)
{
	PIXEL16S *rowptr = array;
	int row, column;

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	if (file == NULL) return;

	fprintf (file, "\n%s:\n", label);
	for (row = 0; row < height; row++) {
		for (column = 0; column < width; column++) {
			//fprintf(file, "%7d", rowptr[column]);
			fprintf(file, "%5d", rowptr[column]);
		}
		fprintf(file, "\n");
		rowptr += pitch;
	}
}

void DumpLine16s(char *label, PIXEL16S *array, int width, int line, int pitch, FILE *file)
{
	PIXEL16S *rowptr = array;
	int column;
	int position = 0;

	if (file == NULL) return;

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	// Compute the row within the array
	rowptr += line * pitch;

	fprintf (file, "\n%s:\n", label);
	for (column = 0; column < width; column++) {
		fprintf(file, "%5d", rowptr[column]);
		position += 5;
		if (position > 80) {
			fprintf(file, "\n");
			position = 0;
		}
	}
	fprintf(file, "\n");
}

void DumpLine8u(char *label, PIXEL8U *array, int width, int line, int pitch, FILE *file)
{
	PIXEL8U *rowptr = array;
	int column;
	int position = 0;

	if (file == NULL) return;

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL8U);

	// Compute the row within the array
	rowptr += line * pitch;

	fprintf (file, "\n%s:\n", label);
	for (column = 0; column < width; column++) {
		fprintf(file, "%5d", rowptr[column]);
		position += 5;
		if (position > 80) {
			fprintf(file, "\n");
			position = 0;
		}
	}
	fprintf(file, "\n");
}

void DumpWavelet(char *label, IMAGE *wavelet, SUBIMAGE *subimage, FILE *file)
{
	int wavelet_height;		// Dimensions of each band
	int wavelet_width;
	int upper_height;		// Height of upper bands
	int lower_height;		// Height of lower bands
	int left_width;			// Width of left bands
	int right_width;		// Width of right bands
	int lowpass_border;
	int highpass_border;
	PIXEL *left_row_ptr;
	PIXEL *right_row_ptr;
	int wavelet_pitch = wavelet->pitch/sizeof(PIXEL);
	int first_row, last_row;
	int first_column, last_column;
	int row, column;

	if (file == NULL) return;

	// If the input image is not a wavelet then just dump band zero
	if (wavelet->type != IMAGE_TYPE_WAVELET) {
		DumpImage(label, wavelet, subimage, file);
		return;
	}

	// Adjust the band dimensions to account for wavelet filtering
	wavelet_height = wavelet->height;
	wavelet_width = wavelet->width;
	lowpass_border = 0;		//wavelet->lowpass_border;
	highpass_border = 0;	//wavelet->highpass_border;
	upper_height = wavelet_height - lowpass_border;
	lower_height = wavelet_height - highpass_border;
	left_width = wavelet_width - lowpass_border;
	right_width = wavelet_width - highpass_border;

	// Narrow the printing range if a subimage was supplied
	if (subimage == NULL)
	{
		first_row = 0;
		first_column = 0;
		last_row = wavelet_height - 1;
		last_column = wavelet_width - 1;
	}
	else
	{
		if (subimage->row < 0) first_row = wavelet_height + subimage->row;
		else first_row = subimage->row;

		if (subimage->column < 0) first_column = wavelet_width + subimage->column;
		else first_column = subimage->column;

		if (subimage->height == 0) last_row = wavelet_height - 1;
		else last_row = first_row + subimage->height - 1;

		if (subimage->width == 0) last_column = wavelet_width - 1;
		else last_column = first_column + subimage->width - 1;
	}

	// Begin printing the wavelet bands
	fprintf (file, "\n%s, Level: %d:\n", label, wavelet->level);

	// Print the two upper bands
	left_row_ptr = wavelet->band[0] + (first_row * wavelet_pitch);
	right_row_ptr = wavelet->band[1] + (first_row * wavelet_pitch);

	for (row = first_row; row <= last_row; row++)
	{
		for (column = first_column; column <= last_column; column++) {
			if (row < upper_height && column < left_width)
				fprintf(file, "%5d", left_row_ptr[column]);
			else
				fprintf(file, "%5s", "B");
		}

		//fprintf(file, "  ");

		for (column = first_column; column <= last_column; column++) {
			if (row < upper_height && column < right_width)
				fprintf(file, "%5d", right_row_ptr[column]);
			else
				fprintf(file, "%5s", "B");
		}

		fprintf(file, "\n");
		left_row_ptr += wavelet_pitch;
		right_row_ptr += wavelet_pitch;
	}

	if (wavelet->num_bands <= 2) return;

	fprintf(file, "\n");

	// Print the two lower bands
	left_row_ptr = wavelet->band[2] + (first_row * wavelet_pitch);
	right_row_ptr = wavelet->band[3] + (first_row * wavelet_pitch);

	for (row = first_row; row <= last_row; row++)
	{
		for (column = first_column; column <= last_column; column++) {
			if (row < lower_height && column < left_width)
				fprintf(file, "%5d", left_row_ptr[column]);
			else
				fprintf(file, "%5s", "B");
		}

		//fprintf(file, "  ");

		for (column = first_column; column <= last_column; column++) {
			if (row < lower_height && column < right_width)
				fprintf(file, "%5d", right_row_ptr[column]);
			else
				fprintf(file, "%5s", "B");
		}

		fprintf(file, "\n");
		left_row_ptr += wavelet_pitch;
		right_row_ptr += wavelet_pitch;
	}
}

void DumpBand(char *label, IMAGE *wavelet, int band, SUBIMAGE *subimage, FILE *file)
{
	PIXEL *band_row = wavelet->band[band];
	int band_width = wavelet->width;
	int band_height = wavelet->height;
	int band_pitch = wavelet->pitch/sizeof(PIXEL);
	int lowpass_border = 0;		//wavelet->lowpass_border;
	int highpass_border = 0;	//wavelet->highpass_border;
	int first_row, first_column;
	int last_row, last_column;
	int bottom_row, left_column;
	int row, column;

	if (file == NULL) return;

	// Calculate the true last row and column
	left_column = band_width /* - BORDER_WIDTH(band, lowpass_border, highpass_border) */ - 1;
	bottom_row = band_height /* - BORDER_HEIGHT(band, lowpass_border, highpass_border) */ - 1;

	if (subimage == NULL)
	{
		first_row = 0;
		first_column = 0;
		last_row = band_height - 1;
		last_column = band_width - 1;
	}
	else
	{
		if (subimage->row < 0) first_row = band_height + subimage->row;
		else first_row = subimage->row;

		if (subimage->column < 0) first_column = band_width + subimage->column;
		else first_column = subimage->column;

		if (subimage->height == 0) last_row = band_height - 1;
		else last_row = first_row + subimage->height - 1;

		if (subimage->width == 0) last_column = band_width - 1;
		else last_column = first_column + subimage->width - 1;

		band_row += (first_row * band_pitch);
	}

	fprintf (file, "\n%s, Level: %d, Band %d:\n", label, wavelet->level, band);
	for (row = first_row; row <= last_row; row++) {
		for (column = first_column; column <= last_column; column++) {
			if (row <= bottom_row && column <= left_column)
				fprintf(file, "%7d", band_row[column]);
			else
				fprintf(file, "%7s", "B");
		}
		fprintf(file, "\n");
		band_row += band_pitch;
	}
}

void DumpBand16s(char *label, IMAGE *wavelet, int band, SUBIMAGE *subimage, FILE *file)
{
	DumpBand(label, wavelet, band, subimage, file);
}

void DumpBand8s(char *label, IMAGE *wavelet, int band, SUBIMAGE *subimage, FILE *file)
{
	PIXEL8S *band_row = (PIXEL8S *)wavelet->band[band];
	int band_width = wavelet->width;
	int band_height = wavelet->height;
	int band_pitch = wavelet->pitch8s/sizeof(PIXEL8S);
	int lowpass_border = 0;		//wavelet->lowpass_border;
	int highpass_border = 0;	//wavelet->highpass_border;
	int first_row, first_column;
	int last_row, last_column;
	int bottom_row, left_column;
	int row, column;

	if (file == NULL) return;

	assert(wavelet->pixel_type[band] == PIXEL_TYPE_8S);

	// Calculate the true last row and column
	left_column = band_width /* - BORDER_WIDTH(band, lowpass_border, highpass_border) */ - 1;
	bottom_row = band_height /* - BORDER_HEIGHT(band, lowpass_border, highpass_border) */ - 1;

	if (subimage == NULL)
	{
		first_row = 0;
		first_column = 0;
		last_row = band_height - 1;
		last_column = band_width - 1;
	}
	else
	{
		if (subimage->row < 0) first_row = band_height + subimage->row;
		else first_row = subimage->row;

		if (subimage->column < 0) first_column = band_width + subimage->column;
		else first_column = subimage->column;

		if (subimage->height == 0) last_row = band_height - 1;
		else last_row = first_row + subimage->height - 1;

		if (subimage->width == 0) last_column = band_width - 1;
		else last_column = first_column + subimage->width - 1;

		band_row += (first_row * band_pitch);
	}

	fprintf (file, "\n%s, Level: %d, Band %d:\n", label, wavelet->level, band);
	for (row = first_row; row <= last_row; row++) {
		for (column = first_column; column <= last_column; column++) {
			if (row <= bottom_row && column <= left_column)
				fprintf(file, "%5d", band_row[column]);
			else
				fprintf(file, "%5s", "B");
		}
		fprintf(file, "\n");
		band_row += band_pitch;
	}
}

void DumpQuad(char *label, IMAGE *wavelet, SUBIMAGE *subimage, FILE *file)
{
	DumpQuad16s8s(label,
				  wavelet->band[0], wavelet->pitch,
				  (PIXEL8S *)(wavelet->band[1]), wavelet->pitch,
				  (PIXEL8S *)(wavelet->band[2]), wavelet->pitch,
				  (PIXEL8S *)(wavelet->band[3]), wavelet->pitch,
				  wavelet->width, wavelet->height, file);
}

void DumpPair(char *label, IMAGE *wavelet, SUBIMAGE *subimage, FILE *file)
{
	DumpQuad16s8s(label,
				  wavelet->band[0], wavelet->pitch,
				  (PIXEL8S *)(wavelet->band[1]), wavelet->pitch,
				  NULL, 0, NULL, 0,
				  wavelet->width, wavelet->height, file);
}

void DumpQuad16s8s(char *label,
				   PIXEL16S *lowlow_band, int lowlow_pitch,
				   PIXEL8S *lowhigh_band, int lowhigh_pitch,
				   PIXEL8S *highlow_band, int highlow_pitch,
				   PIXEL8S *highhigh_band, int highhigh_pitch,
				   int width, int height, FILE *file)
{
	PIXEL16S *lowlow_row_ptr = lowlow_band;
	PIXEL8S *lowhigh_row_ptr = lowhigh_band;
	PIXEL8S *highlow_row_ptr = highlow_band;
	PIXEL8S *highhigh_row_ptr = highhigh_band;
	int row, column;

	lowlow_pitch /= sizeof(PIXEL16S);
	lowhigh_pitch /= sizeof(PIXEL8S);
	highlow_pitch /= sizeof(PIXEL8S);
	highhigh_pitch /= sizeof(PIXEL8S);

	// Begin printing the wavelet bands
	fprintf (file, "\n%s:\n", label);

	if (width < 20)
	{
		// Print the two upper bands
		for (row = 0; row < height; row++)
		{
			for (column = 0; column < width; column++)
				fprintf(file, "%5d", lowlow_row_ptr[column]);

			for (column = 0; column < width; column++)
				fprintf(file, "%5d", lowhigh_row_ptr[column]);

			fprintf(file, "\n");
			lowlow_row_ptr += lowlow_pitch;
			lowhigh_row_ptr += lowhigh_pitch;
		}

		if (highlow_band == NULL && highhigh_band == NULL) return;

		fprintf(file, "\n");

		// Print the two lower bands
		for (row = 0; row < height; row++)
		{
			for (column = 0; column < width; column++)
				if (highlow_band != NULL)
					fprintf(file, "%5d", highlow_row_ptr[column]);

			for (column = 0; column < width; column++)
				if (highhigh_band != NULL)
					fprintf(file, "%5d", highhigh_row_ptr[column]);

			fprintf(file, "\n");
			if (highlow_band != NULL) highlow_row_ptr += highlow_pitch;
			if (highhigh_band != NULL) highhigh_row_ptr += highhigh_pitch;
		}
	}
	else
	{
		// Print the first band
		for (row = 0; row < height; row++)
		{
			for (column = 0; column < width; column++)
				fprintf(file, "%5d", lowlow_row_ptr[column]);
			fprintf(file, "\n");
			lowlow_row_ptr += lowlow_pitch;
		}

		fprintf(file, "\n");

		// Print the second band
		for (row = 0; row < height; row++)
		{
			for (column = 0; column < width; column++)
				fprintf(file, "%5d", lowhigh_row_ptr[column]);
			fprintf(file, "\n");
			lowhigh_row_ptr += lowhigh_pitch;
		}

		if (highlow_band == NULL && highhigh_band == NULL) return;

		fprintf(file, "\n");

		// Print the third lower band
		for (row = 0; row < height; row++)
		{
			for (column = 0; column < width; column++)
				if (highlow_band != NULL)
					fprintf(file, "%5d", highlow_row_ptr[column]);
			fprintf(file, "\n");
			if (highlow_band != NULL) highlow_row_ptr += highlow_pitch;
		}

		fprintf(file, "\n");

		// Print the fourth lower band
		for (row = 0; row < height; row++)
		{
			for (column = 0; column < width; column++)
				if (highhigh_band != NULL)
					fprintf(file, "%5d", highhigh_row_ptr[column]);
			fprintf(file, "\n");
			if (highhigh_band != NULL) highhigh_row_ptr += highhigh_pitch;
		}
	}
}

void DumpBandRow8s(PIXEL8S *data, int length, FILE *file)
{
	int i;

	for (i = 0; i < length; i++)
		fprintf(file, "%5d", data[i]);

	fprintf(file, "\n");
}

void DumpBandRow8u(PIXEL8U *data, int length, FILE *file)
{
	int i;

	for (i = 0; i < length; i++)
		fprintf(file, "%5d", data[i]);

	fprintf(file, "\n");
}

void DumpBandRow16s(PIXEL16S *data, int length, FILE *file)
{
	int i;

	for (i = 0; i < length; i++)
		fprintf(file, "%5d", data[i]);

	fprintf(file, "\n");
}

void DumpBandRow(PIXEL *data, int length, int type, FILE *file)
{
	switch (type)
	{
	case PIXEL_TYPE_8S:
		DumpBandRow8s((PIXEL8S *)data, length, file);
		break;

	case PIXEL_TYPE_8U:
		DumpBandRow8u((PIXEL8U *)data, length, file);
		break;

	case PIXEL_TYPE_16S:
		DumpBandRow16s((PIXEL16S *)data, length, file);
		break;
	}
}

void DumpLowpassRow(IMAGE *wavelet, int row, FILE *file)
{
	PIXEL *rowptr = wavelet->band[0];
	int pitch = wavelet->pitch;
	int width = wavelet->width;
	int column;
	int position = 0;

	if (file == NULL) return;

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL);

	// Compute the row within the array
	rowptr += row * pitch;

	//fprintf (file, "\n%s:\n", label);
	for (column = 0; column < width; column++) {
		fprintf(file, "%7d", rowptr[column]);
		position += 7;
		if (position > 80) {
			fprintf(file, "\n");
			position = 0;
		}
	}
	fprintf(file, "\n");
}

void DumpWaveletRow(IMAGE *wavelet, int band, int row, FILE *file)
{
	union {
		PIXEL16S *s16;
		PIXEL8S *s8;
	} rowptr;
	int pitch = wavelet->pitch;
	int width = wavelet->width;
	int column;
	int position = 0;

	if (file == NULL) return;

	switch (wavelet->pixel_type[band])
	{
	case PIXEL_TYPE_16S:
	default:
		// Convert the pitch from bytes to pixels
		pitch /= sizeof(PIXEL16S);

		// Compute the row within the array
		rowptr.s16 = wavelet->band[band] + row * pitch;

		//fprintf (file, "\n%s:\n", label);
		for (column = 0; column < width; column++) {
			fprintf(file, "%7d", rowptr.s16[column]);
			position += 7;
			if (position > 80) {
				fprintf(file, "\n");
				position = 0;
			}
		}
		fprintf(file, "\n");
		break;

	case PIXEL_TYPE_8S:
		// Convert the pitch from bytes to pixels
		pitch /= sizeof(PIXEL8S);

		// Compute the row within the array
		rowptr.s8 = (PIXEL8S *)(wavelet->band[band]) + row * pitch;

		//fprintf (file, "\n%s:\n", label);
		for (column = 0; column < width; column++) {
			fprintf(file, "%5d", rowptr.s8[column]);
			position += 5;
			if (position > 80) {
				fprintf(file, "\n");
				position = 0;
			}
		}
		fprintf(file, "\n");
		break;
	}
}

void DumpWaveletRow8s(IMAGE *wavelet, int band, int row, FILE *file)
{
	PIXEL8S *rowptr;
	int pitch = wavelet->pitch8s;
	int width = wavelet->width;
	int column;
	int position = 0;

	if (file == NULL) return;

	// Check that the band uses eight bit pixels
	assert(wavelet->pixel_type[band] == PIXEL_TYPE_8S);

	// Convert the pitch from bytes to pixels
	pitch /= sizeof(PIXEL8S);

	// Compute the row within the array
	rowptr = (PIXEL8S *)(wavelet->band[band]) + row * pitch;

	//fprintf (file, "\n%s:\n", label);
	for (column = 0; column < width; column++) {
		fprintf(file, "%5d", rowptr[column]);
		position += 5;
		if (position > 80) {
			fprintf(file, "\n");
			position = 0;
		}
	}

	fprintf(file, "\n");
}

// Output the lowpass image statistics
void DumpImageStatistics(char *label, IMAGE *image, FILE *file)
{
	int32_t pixel_sum = 0;
	int pixel_count = 0;
	int pixel_minimum = PIXEL_MAXIMUM;
	int pixel_maximum = PIXEL_MINIMUM;
	float pixel_average;
	PIXEL *rowptr;
	int pitch;
	int row, column;

	int border = 0;

	int width = image->width;
	int height = image->height;
	int first_row = border;
	int first_column = border;
	int last_row = height - border - 1;
	int last_column = width - border - 1;

	assert(image != NULL);

	rowptr = image->band[0];

	// Convert pitch to units of pixels
	pitch = image->pitch/sizeof(PIXEL);

	for (row = first_row; row <= last_row; row++)
	{
		for (column = first_column; column <= last_column; column++)
		{
			int pixel_value = rowptr[column];
			pixel_sum += pixel_value;
			pixel_count++;
			if (pixel_value < pixel_minimum) pixel_minimum = pixel_value;
			if (pixel_value > pixel_maximum) pixel_maximum = pixel_value;
		}
		rowptr += pitch;
	}

	pixel_average = (float)pixel_sum / (float)pixel_count;

	fprintf(file, "%s, min: %d, max: %d, avg: %.2f\n",
			label, pixel_minimum, pixel_maximum, pixel_average);
}

// Output the wavelet band statistics
void DumpBandStatistics(char *label, IMAGE *wavelet, int band, FILE *file)
{
	int32_t pixel_sum = 0;
	int pixel_count = 0;
	int pixel_minimum = PIXEL_MAXIMUM;
	int pixel_maximum = PIXEL_MINIMUM;
	float pixel_average;
	PIXEL *rowptr;
	int pitch;
	int row, column;

	int border = 0;

	int width = wavelet->width;
	int height = wavelet->height;
	int first_row = border;
	int first_column = border;
	int last_row = height - border - 1;
	int last_column = width - border - 1;

	assert(wavelet != NULL);

	rowptr = wavelet->band[band];

	// Convert pitch to units of pixels
	pitch = wavelet->pitch/sizeof(PIXEL);

	for (row = first_row; row <= last_row; row++)
	{
		for (column = first_column; column <= last_column; column++)
		{
			int pixel_value = rowptr[column];
			pixel_sum += pixel_value;
			pixel_count++;
			if (pixel_value < pixel_minimum) pixel_minimum = pixel_value;
			if (pixel_value > pixel_maximum) pixel_maximum = pixel_value;
		}
		rowptr += pitch;
	}

	pixel_average = (float)pixel_sum / (float)pixel_count;

	fprintf(file, "%s, min: %d, max: %d, avg: %.2f\n",
			label, pixel_minimum, pixel_maximum, pixel_average);
}

// Output the statistics in a packed buffer
void DumpBufferStatistics(char *label, uint8_t *buffer, int width, int height, int pitch, FILE *file)
{
	int32_t luma_sum = 0;
	int luma_count = 0;
	int luma_min = UCHAR_MAX;
	int luma_max = 0;

	int32_t chroma_sum = 0;
	int chroma_count = 0;
	int chroma_min = UCHAR_MAX;
	int chroma_max = 0;

	float luma_average;
	float chroma_average;

	uint8_t *rowptr = buffer;
	int row, column;

	// Convert pitch to units of pixels
	pitch = pitch/sizeof(uint8_t);

	for (row = 0; row < height; row++)
	{
		for (column = 0; column < width; column += 2)
		{
			int luma = rowptr[column];
			int chroma = rowptr[column + 1];

			luma_sum += luma;
			luma_count++;
			if (luma < luma_min) luma_min = luma;
			if (luma > luma_max) luma_max = luma;

			chroma_sum += chroma;
			chroma_count++;
			if (luma < chroma_min) chroma_min = chroma;
			if (luma > chroma_max) chroma_max = chroma;
		}
		rowptr += pitch;
	}

	luma_average = (float)luma_sum / (float)luma_count;
	chroma_average = (float)chroma_sum / (float)chroma_count;

	fprintf(file, "%s, luma min: %d, max: %d, avg: %.2f, chroma min: %d, max: %d, avg: %.2f\n",
			label, luma_min, luma_max, luma_average, chroma_min, chroma_max, chroma_average);
}

void DumpWaveletBandsPGM(IMAGE *wavelet, int frame_index, int num_channels)
{
	int channel;

	for (channel = 0; channel < num_channels; channel++)
	{
		int band;

		for (band = 0; band < CODEC_MAX_BANDS; band++)
		{
			static int count = 0;
			if (count < 20) {
				char label[_MAX_PATH];
#ifdef _WIN32
				sprintf_s(label, sizeof(label), "Frame%dc%db%d-decode-%d-", frame_index, channel, band, count);
#else
				sprintf(label, "Frame%dc%db%d-decode-%d-", frame_index, channel, band, count);
#endif
				if (band == 0) DumpPGM(label, wavelet, NULL);
				else DumpBandPGM(label, wavelet, band, NULL);
			}
			count++;
		}
	}
}

#endif
