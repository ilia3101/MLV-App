/*! @file CFHDDecoder.cpp

*  @brief This module implements the C functions for the decoder API
*  
*  Interface to the CineForm HD decoder.  The decoder API uses an opaque
*  data type to represent an instance of a decoder.  The decoder reference
*  is returned by the call to @ref CFHD_OpenDecoder.
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

#include "StdAfx.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif


#if _WIN32

// Export the interface to the decoder
#define DECODERDLL_EXPORTS	1

#elif __APPLE__

#define DECODERDLL_EXPORTS	1

#ifdef DECODERDLL_API
#undef DECODERDLL_API
#endif

#define DECODERDLL_API __attribute__((visibility("default")))
#include <CoreFoundation/CoreFoundation.h>

#else

// Code required by GCC on Linux to define the entry points

#ifdef DECODERDLL_API
#undef DECODERDLL_API
#endif

#define DECODERDLL_API __attribute__((visibility("default")))

#endif

// Include declarations from the codec library
#include "decoder.h"
#include "swap.h"
#include "thumbnail.h"

// Include declarations for the decoder component
#include "CFHDDecoder.h"
#include "CFHDMetadata.h"
#include "IAllocator.h"
#include "ISampleDecoder.h"
#include "SampleDecoder.h"
#include "SampleMetadata.h"

	
#if _WIN32
	#ifdef DYNAMICLIB
	BOOL APIENTRY DllMain(HANDLE hModule,
						  DWORD ulReasonForCall,
						  LPVOID lpReserved)
	{
		switch (ulReasonForCall)
		{
		case DLL_PROCESS_ATTACH:
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
			break;
		}
		return TRUE;
	}
    #endif
#else

void _splitpath( const char * fullPath, char * drive, char * dir, char * fname, char * ext)
{
	int			pathLen = 0;
	char	*	namePtr;
	char	*	extPtr;
	char	*	originalNamePtr;

	drive[0] = '\0';
	dir[0] = '\0';
	fname[0] = '\0';
	ext[0] = '\0';
	originalNamePtr = namePtr = (char *)malloc( strlen( fullPath )+1 );
	if(namePtr) {
		strcpy( namePtr, fullPath );
		while( namePtr[0] && strchr( namePtr, '/' ) ) {
			pathLen++;
			namePtr++;
		}
		strncpy( dir, fullPath, pathLen );
		dir[pathLen] = '\0';
		extPtr = strrchr( namePtr, '.');
		if(extPtr) {
			strcpy( ext, extPtr );
			namePtr = strtok(namePtr, extPtr );
		}
		strcpy( fname, namePtr );
		free(originalNamePtr);
	}
}

void _makepath(char * filename,  char * drive, char * dir, char * fname, char * ext)
{
	filename = strcat( fname, ext );
}

#endif


/*!
	@function CFHD_OpenDecoder

	@brief Open an instance of the CineForm HD decoder and return a reference
	to the decoder through the pointer provided as the first argument.

	@param decoderRefOut
	An opaque reference to a decoder returned by but this function.

	@param allocator
	Optional CFHD_ALLOCATOR structure, for those was wishing to control memory allocations.

	@return Returns a CFHD error code.
*/
CFHDDECODER_API CFHD_Error
CFHD_OpenDecoder(CFHD_DecoderRef *decoderRefOut,
				 CFHD_ALLOCATOR *allocator)
{
	// Check the input arguments
	if (decoderRefOut == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	// Allocate a new decoder data structure
	CSampleDecoder *decoderRef = new CSampleDecoder;
	if (decoderRef == NULL) {
		return CFHD_ERROR_OUTOFMEMORY;
	}

	decoderRef->SetAllocator(allocator);

	// Return the decoder data structure
	*decoderRefOut = (CFHD_DecoderRef)decoderRef;

	return CFHD_ERROR_OKAY;
}

/*!
	@function CFHD_GetOutputFormats

	@brief Returns a list of output formats that are appropriate for
	the encoded sample that is provided as an argument.

	@description The CineForm HD codec encodes source video in a variety
	of internal formats depending on the product in which the codec is
	delivered, the video source format, and options provided to the encoder.
	This routine examines the tags that are embedded in the encoded sample
	and selects the output formats that are best for the encoded format,
	in decreasing order of preference.  Output formats that are not
	appropriate to the encoded format are omitted.  For example, raw Bayer
	output formats are not provided if the encoded samples are not
	raw Bayer data.  The list of output formats is ordered to avoid color
	conversion and deeper pixel formats are listed first.

	@param decoderRef
	An opaque reference to a decoder created by a call to @ref CFHD_OpenDecoder.

	@param samplePtr
	The memory address of a CineForm compressed sample.

	@param sampleSize
	The size of a CineForm compressed sample.

	@param outputFormatArray
	Pointer to a preallocated array of type CFHD_PixelFormat.
	
	@param outputFormatArrayLength
	Number elements in the preallocated array of type CFHD_PixelFormat.

	@param actualOutputFormatCountOut
	Location to return the number of recommended formats.

	@return Returns a CFHD error code.
*/
CFHDDECODER_API CFHD_Error
CFHD_GetOutputFormats(CFHD_DecoderRef decoderRef,
					  void *samplePtr,
					  size_t sampleSize,
					  CFHD_PixelFormat *outputFormatArray,
					  int outputFormatArrayLength,
					  int *actualOutputFormatCountOut)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (decoderRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleDecoder *decoder = (CSampleDecoder *)decoderRef;

	errorCode = decoder->GetOutputFormats(samplePtr,
										  sampleSize,
										  outputFormatArray,
										  outputFormatArrayLength,
										  actualOutputFormatCountOut);

	return errorCode;
}




/*!
	@function CFHD_GetSampleInfo

	@brief Returns requested information about the current sample.

	@description Requesting miscellaneous information from a CineForm
	sample, by Tag-Value pair.
		
	@param decoderRef
	An opaque reference to a decoder created by a call to @ref CFHD_OpenDecoder.

	@param samplePtr
	The memory address of a CineForm compressed sample.

	@param sampleSize
	The size of a CineForm compressed sample.

	@param tag
	The request the desired data.

	@param value
	Pointer to an buffer that holds the return value.

	@param buffer_size
	Size of the buffer for the return value.

	@return Returns a CFHD error code.
*/
CFHDDECODER_API CFHD_Error
CFHD_GetSampleInfo(	CFHD_DecoderRef decoderRef,
					void *samplePtr,
					size_t sampleSize,
					CFHD_SampleInfoTag tag,
					void *value,
					size_t buffer_size)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (decoderRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleDecoder *decoder = (CSampleDecoder *)decoderRef;

	errorCode = decoder->GetSampleInfo(samplePtr,
									  sampleSize,
									  tag,
									  value,
									  buffer_size);

	return errorCode;
}


/*!
	@function CFHD_PrepareToDecode

	@brief Initializes an instance of the CineForm HD decoder that was
	created by a call to @ref CFHD_OpenDecoder.

	@description The caller can specify the exact dimensions of the decoded
	frame or pass zero for either the output width or output height arguments
	to allow this routine to choose the best output dimensions.  Typically,
	the output dimensions will be the same as the encoded dimensions,
	with a reduction as specified by the decoded resolution argument.
	Likewise, the caller can specify an output pixel format	or allow the routine
	to select the best format.
	The function @ref CFHD_GetOutputFormats provides a list of output formats in
	decreasing order of preference and this function will use the first output
	format from that list if an output format is not specified.  The actual
	output dimensions and pixel format are returned.

	@param decoderRef
	An opaque reference to a decoder created by a call to @ref CFHD_OpenDecoder.

	@param outputWidth
	The desired width of the decoded frame.  Pass zero to allow this routine
	to choose the best output width.

	@param outputHeight
	The desired width of the decoded frame.  Pass zero to allow this routine
	to choose the best output height.

	@param outputFormat
	The desired output format passed as a four character code.  The requested
	output format will be used if it is one of the formats that would be returned
	by a call to @ref CFHD_GetOutputFormats.  See the pixel formats defined in the
	enumeration CFHD_PixelFormat.  The decoder will always output frames in the
	specified pixel format if possible; otherwise, the call to this routine will
	return an error code.

	@param decodedResolution
	The desired resolution for decoding relative to the encoded frame size.
	See the possible resolutions defined in the enumeration CFHD_DecodedResolution.
	If this argument is non-zero, it must specify a valid decoded resolution such
	as full or half resolution.  The decoder will divide the encoded dimensions
	by the divisor implied by this parameter to determine the actual output dimensions.

	@param decodingFlags
	Flags that specify options for initializing the decoder.  See the flags defined in
	the enumeration for CFHD_DecodingFlags.  The decoding flags are not currently used.
	Pass zero for this argument.

	@param samplePtr
	Pointer to an encoded sample that is representative of the samples that
	will be passed to the decoder.  The sample is parsed to obtain information
	about how the video was encoded.  This information guides this routine in
	initializing the decoder.

	@param sampleSize
	Normally this size of the sample in bytes, if you intend to go on to decode the frame.
	However, if you was only initializing a decode, and wish to reduce disk overhead,
	you can set the size to a little as 512, as that is sufficient to pass all the need
	information from the sample header.

	@param actualWidthOut
	Pointer to a variable that will receive the actual width of the decoded
	frames.  The caller can pass NULL, but it is recommended that the caller
	always use the actual dimensions and output format to allocate buffers
	for the decoded frames.

	@param actualHeightOut
	Pointer to a variable that will receive the actual height of the decoded
	frames.  The caller can pass NULL, but it is recommended that the caller
	always use the actual dimensions and output format to allocate buffers
	for the decoded frames.

	@param actualFormatOut
	Pointer to a variable that will receive the actual pixel format of the
	decoded frames.  The caller can pass NULL, but should use the output pixel
	format to determine the size of the output pixels for allocating the buffers
	that will receive the decoded frames.
	
	@details If the output width or height are zero, the decoder will compute
	the output width and height by using the encoded width and height obtained
	from the video sample passed as an argument and reducing the width and height
	as specified by the decoded resolution argument.  This makes it very easy to
	initialize the decoder so that it provides frames at close to the desired
	size needed by the application as efficiently as possible.  It is anticipated
	that in this scenario the application will provide it own scaling routines if
	necessary.

	@bug Arbitrary scaling is not supported by the decoder in this version of the
	codec SDK.

	@return Returns a CFHD error code.
*/

CFHDDECODER_API CFHD_Error
CFHD_PrepareToDecode(CFHD_DecoderRef decoderRef,
					 int outputWidth,
					 int outputHeight,
					 CFHD_PixelFormat outputFormat,
					 CFHD_DecodedResolution decodedResolution,
					 CFHD_DecodingFlags decodingFlags,
					 void *samplePtr,
					 size_t sampleSize,
					 int *actualWidthOut,
					 int *actualHeightOut,
					 CFHD_PixelFormat *actualFormatOut)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (decoderRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleDecoder *decoder = (CSampleDecoder *)decoderRef;

	errorCode = decoder->PrepareDecoder(outputWidth,
										outputHeight,
										outputFormat,
										decodedResolution,
										decodingFlags,
										samplePtr,
										sampleSize,
										actualWidthOut,
										actualHeightOut,
										actualFormatOut);
	if (errorCode != CFHD_ERROR_OKAY) {
		return errorCode;
	}

	return errorCode;
}

/*-- not include in Doxygen
	@function CFHD_ParseSampleHeader

	@brief Parse the header in the encoded video sample. OBSOLETED by CFHD_GetSampleInfo()

	@description The sample header is parsed to obtain information about the
	video sample without decoding the video sample.
	
	@param samplePtr
	The memory address of a CineForm compressed sample.

	@param sampleSize
	The size of a CineForm compressed sample.

	@param sampleHeader
	The address of a pre-allocated structure of type CFHD_SampleHeader.

*/
CFHDDECODER_API CFHD_Error
CFHD_ParseSampleHeader(void *samplePtr,
					   size_t sampleSize,
					   CFHD_SampleHeader *sampleHeader)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Catch any errors in the decoder
	try
	{
		CFHD_EncodedFormat encodedFormat = CFHD_ENCODED_FORMAT_YUV_422;
		CFHD_FieldType fieldType = CFHD_FIELD_TYPE_UNKNOWN;

		// Initialize a bitstream to the sample data
		BITSTREAM bitstream;
		InitBitstreamBuffer(&bitstream, (uint8_t  *)samplePtr, sampleSize, BITSTREAM_ACCESS_READ);

		// Clear the fields in the sample header
		SAMPLE_HEADER header;
		memset(&header, 0, sizeof(SAMPLE_HEADER));

		// Decode the sample header
		bool result = ::ParseSampleHeader(&bitstream, &header);
		if (!result)
		{
			// The frame dimensions must be obtained from the encoded sample
			if (header.width == 0 || header.height == 0)
			{
				assert(0);
				errorCode = CFHD_ERROR_BADSAMPLE;
				goto finish;
			}

			// Try to fill in missing information with default values
			if (header.encoded_format == ENCODED_FORMAT_UNKNOWN)
			{
				// The encoded format is probably YUV 4:2:2
				header.encoded_format = ENCODED_FORMAT_YUV_422;
			}

			// It is okay if the original input format is not known
		}

		// Copy the sample header information to the output

		encodedFormat = CSampleDecoder::EncodedFormat(header.encoded_format);
		sampleHeader->SetEncodedFormat(encodedFormat);

		fieldType = CSampleDecoder::FieldType(&header);
		sampleHeader->SetFieldType(fieldType);

		sampleHeader->SetFrameSize(header.width, header.height);
	}
	catch (...)
	{
#if _WIN32
		char message[256];
		sprintf_s(message, sizeof(message), "CSampleDecoder::PrepareDecoder caught internal codec error\n");
		OutputDebugString(message);
#endif
		return CFHD_ERROR_INTERNAL;
	}

finish:

	return errorCode;
}

/*!
	@function CFHD_GetPixelSize

	@brief Return the size of the specified pixel format in bytes.
	
	@description Return the size of a pixel in byte is it uniquely
	addressable.  Note that the pixel size is not defined for some
	image formats such as v210.  This routine returns zero for pixel
	formats that do not have a size that is an integer number of bytes.
	When the pixel size is not well-defined, it cannot be used to
	compute the pitch of the image rows.  See @ref CFHD_GetImagePitch.

	@param pixelFormat
	CFHD_PixelFormat of the decoding pixel type.

	@param pixelSizeOut
	Pointer to return the pixel size.

	@return Returns a CFHD error code.
*/
CFHDDECODER_API CFHD_Error
CFHD_GetPixelSize(CFHD_PixelFormat pixelFormat, uint32_t *pixelSizeOut)
{
	CFHD_Error ret = CFHD_ERROR_OKAY;
	if (pixelSizeOut == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}
	
	// Catch any errors in the decoder
	try
	{
		*pixelSizeOut = (uint32_t)GetPixelSize(pixelFormat);
	}
	catch (...)
	{
		*pixelSizeOut = 0;
		ret = CFHD_ERROR_BADFORMAT;
	}

	return ret;
/*
	uint32_t pixelSize = 0;

	switch (pixelFormat)
	{
	case CFHD_PIXEL_FORMAT_YUY2:
	case CFHD_PIXEL_FORMAT_2VUY:
	case CFHD_PIXEL_FORMAT_BYR2:
	case CFHD_PIXEL_FORMAT_BYR4:	
	case CFHD_PIXEL_FORMAT_CT_10BIT_2_8:		// Avid format with two planes of 2-bit and 8-bit pixels
		pixelSize = 2;
		break;

	case CFHD_PIXEL_FORMAT_V210:
		pixelSize = 0;				// 3 is close, but no true pixel size can be returned.
		break;

	case CFHD_PIXEL_FORMAT_BGRA:
	case CFHD_PIXEL_FORMAT_BGRa:
	case CFHD_PIXEL_FORMAT_R408:
	case CFHD_PIXEL_FORMAT_V408:
	case CFHD_PIXEL_FORMAT_R210:
	case CFHD_PIXEL_FORMAT_DPX0:
	case CFHD_PIXEL_FORMAT_RG30:
	case CFHD_PIXEL_FORMAT_AB10:
	case CFHD_PIXEL_FORMAT_AR10:
	case CFHD_PIXEL_FORMAT_YU64:
	case CFHD_PIXEL_FORMAT_CT_SHORT_2_14:		// Avid fixed point 2.14 pixel format
	case CFHD_PIXEL_FORMAT_CT_USHORT_10_6:		// Avid fixed point 10.6 pixel format
	case CFHD_PIXEL_FORMAT_CT_SHORT:			// Avid 16-bit signed pixels
		pixelSize = 4;
		break;

	case CFHD_PIXEL_FORMAT_RG48:
	case CFHD_PIXEL_FORMAT_WP13:
		pixelSize = 6;
		break;

	case CFHD_PIXEL_FORMAT_B64A:
	case CFHD_PIXEL_FORMAT_W13A:
		pixelSize = 8;
		break;

	default:
		//TODO: Add more pixel formats
		assert(0);
		return CFHD_ERROR_INVALID_ARGUMENT;
		break;
	}

	*pixelSizeOut = pixelSize;
*/
	return CFHD_ERROR_OKAY;
}

/*!
	@function CFHD_GetImagePitch

	@brief Return the allocated length of each image row in bytes.

	@description This routine must be used to determine the pitch for
	pixel formats such as v210 where the pixel size is not defined.

	@param imageWidth
	Width of the image. 

	@param pixelFormat
	CFHD_PixelFormat of the decoding pixel type.

	@param imagePitchOut
	Pointer to return the rowsize/pitch in bytes.

	@return Returns a CFHD error code.
*/
CFHDDECODER_API CFHD_Error
CFHD_GetImagePitch(uint32_t imageWidth, CFHD_PixelFormat pixelFormat, int32_t *imagePitchOut)
{
	int32_t imagePitch = GetFramePitch(imageWidth, pixelFormat);
	if (imagePitchOut)
	{
		// Return the image pitch (in bytes)
		*imagePitchOut = imagePitch;
		return CFHD_ERROR_OKAY;
	}
	return CFHD_ERROR_INVALID_ARGUMENT;
}

/*!
	@function CFHD_GetImageSize

	@brief Return the size of an image in bytes.

	@description This image size returned by this routine can be used to allocate a 
	buffer for a decoded 2D or 3D image.

	@param imageWidth
	Width of the image.

	@param imageHeight
	Height of the image.  In the case of a 3D image, this is the height of a single eye.

	@param pixelFormat
	CFHD_PixelFormat of the decoding pixel type.

	@param videoselect
	CFHD_VideoSelect type to specify if you are decoding left/right or both eyes.

	@param stereotype
	CFHD_Stereo3DType type to specify 3D format if decoding both eyes.

	@param imageSizeOut
	Pointer to return the image size in bytes.

	@return Returns a CFHD error code.
*/
CFHDDECODER_API CFHD_Error
CFHD_GetImageSize(uint32_t imageWidth, uint32_t imageHeight, CFHD_PixelFormat pixelFormat, 
				  CFHD_VideoSelect videoselect,	CFHD_Stereo3DType stereotype, uint32_t *imageSizeOut)
{
	uint32_t imagePitch = GetFramePitch(imageWidth, pixelFormat);
	uint32_t imageSize = imagePitch * imageHeight;

	if(stereotype == STEREO3D_TYPE_DEFAULT && videoselect == VIDEO_SELECT_BOTH_EYES)
		imageSize *= 2;

	if (imageSizeOut)
	{
		// Return the size of the image (in bytes)
		*imageSizeOut = imageSize;
		return CFHD_ERROR_OKAY;
	}
	return CFHD_ERROR_INVALID_ARGUMENT;
}

/*!
	@function CFHD_DecodeSample

	@brief Decode one frame of CineForm HD video.

	@description The decoder must have been initialized by a call to
	CFHD_PrepareToDecode.  The decoded frame will have the dimensions
	and format returned by the call to CFHD_PrepareToDecode.

	@param decoderRef
	A reference to a decoder that was initialized by a call to
	CFHD_PrepareToDecode.

	@param samplePtr
	Pointer to a sample containing one frame of encoded video in the
	CineForm HD format.

	@param sampleSize
	Size of the encoded sample.

	@param outputBuffer
	Buffer that will receive the decoded frame.  The buffer must start on an address
	that is aligned to 16 bytes.

	@param outputPitch
	Pitch of the output buffer in bytes.  The pitch must be at least as large as the
	size of one row of decoded pixels.  Since each output row must start on an address
	that is aligned to 16 bytes, the pitch must be a multiple of 16 bytes.

	@return Returns a CFHD error code.
*/
CFHDDECODER_API CFHD_Error
CFHD_DecodeSample(CFHD_DecoderRef decoderRef,
				  void *samplePtr,
				  size_t sampleSize,
				  void *outputBuffer,
				  int outputPitch)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (decoderRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleDecoder *decoder = (CSampleDecoder *)decoderRef;

	// Test the memory buffer provided for the required size
	try
	{
		uint32_t length = 0;
		uint8_t *test_mem = (uint8_t *)outputBuffer;

		decoder->GetRequiredBufferSize(length);

		test_mem[0] = 0;
		if(length > 0)
		{
			int len = length;
			if(outputPitch > 0)
				test_mem[len - 1] = 0;
			if(outputPitch < 0)
				test_mem[-(len + outputPitch)] = 0;
		}
	}
	catch (...)
	{
#ifdef _WIN32
		OutputDebugString("Target memory buffer is an invalid size");
#endif
		return CFHD_ERROR_DECODE_BUFFER_SIZE;
	}

	errorCode = decoder->DecodeSample(samplePtr, sampleSize, outputBuffer, outputPitch);
	if (errorCode != CFHD_ERROR_OKAY) {
		return errorCode;
	}


	return CFHD_ERROR_OKAY;
}

/*!
	@function CFHD_SetLicense

	@brief Now obsolete, this was used to license the commercial version, but it is no longer required.
	The interface is maintained for backward compatibility.

	@description The license key is used to control trial periods and decode resolution limits.

	@param decoderRef An opaque reference to a decoder created by a
	call to @ref CFHD_OpenDecoder.

	@param licenseKey Pointer to an array of 16 bytes contain the license key.

	@return Returns a CFHD error code.
*/
CFHDDECODER_API CFHD_Error
CFHD_SetLicense(CFHD_DecoderRef decoderRef,
				const unsigned char *licenseKey)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;
	
	// Check the input arguments
	if (decoderRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}
	if (licenseKey == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}
	
	CSampleDecoder *decoder = (CSampleDecoder *)decoderRef;

	errorCode = decoder->SetLicense(licenseKey);
	
	return errorCode;
}

/*!
	@function CFHD_CloseDecoder

	@brief Release all resources held by the decoder.

	@description Do not attempt to use the decoder after it has been
	closed by a call to this routine.

	@return Returns a CFHD error code.
*/
CFHDDECODER_API CFHD_Error
CFHD_CloseDecoder(CFHD_DecoderRef decoderRef)
{
	//CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (decoderRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleDecoder *decoder = (CSampleDecoder *)decoderRef;

	delete decoder;

	return CFHD_ERROR_OKAY;
}


#ifdef _WIN32
#include "CFHDMetadata.h"
#else
#include "CFHDMetadata.h"
#endif
#include "AVIExtendedHeader.h"
#include "SampleMetadata.h"
#include "../Codec/metadata.h"



// Return the pathname of the LUT directory and the filename of the database directory
extern void InitGetLUTPaths(char *pPathStr,	//!< Pathname to the LUT directory
	size_t pathSize,	//!< Size of the LUT pathname (in bytes)
	char *pDBStr,		//!< Filename of the database directory
	size_t DBSize		//!< Size of the database filename (in bytes)
);



#define BUFSIZE	1024
/* Table of CRCs of all 8-bit messages. */
uint32_t crc_table[256];

/* Flag: has the table been computed? Initially false. */
int crc_table_computed = 0;

/* Make the table for a fast CRC. */
void make_crc_table(void)
{
 uint32_t c;
 int n, k;

 for (n = 0; n < 256; n++) {
   c = (uint32_t) n;
   for (k = 0; k < 8; k++) {
     if (c & 1)
       c = 0xedb88320L ^ (c >> 1);
     else
       c = c >> 1;
   }
   crc_table[n] = c;
 }
 crc_table_computed = 1;
}

/* Update a running CRC with the bytes buf[0..len-1]--the CRC
  should be initialized to all 1's, and the transmitted value
  is the 1's complement of the final running CRC (see the
  crc() routine below)). */

uint32_t update_crc(uint32_t crc, unsigned char *buf,
                    int len)
{
 uint32_t c = crc;
 int n;

 if (!crc_table_computed)
   make_crc_table();
 for (n = 0; n < len; n++) {
   c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
 }
 return c;
}

/* Return the CRC of the bytes buf[0..len-1]. */
uint32_t calccrc(unsigned char *buf, int len)
{
 return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

#define BINARY_LUT	1

#if 0
void GenerateLUTfile(unsigned int crc, float *LUT, int size, char *displayname)
{
	char PathStr[260];
	char DBStr[64];
	InitGetLUTPaths(PathStr, (size_t)sizeof(PathStr), DBStr, (size_t)sizeof(DBStr));
	//GetLUTPath(PathStr);

	char crcname[32];
#if BINARY_LUT
	sprintf(crcname,"%08X.cflook", crc);
#else
	sprintf(crcname,"%08X.look", crc);
#endif

	char lutfile[260];
	sprintf(lutfile, "%s\\%s", PathStr, crcname);


#define MAKEID(a,b,c,d) ((a<<24)|(b<<16)|(c<<8)|(d))
#define MAKEID_SWAP(d,c,b,a) ((a<<24)|(b<<16)|(c<<8)|(d))

	FILE *fp = fopen(lutfile,"r");
	if (fp != NULL)
	{
		//int endianswap = 0;
		int regen = 0;
		CFLook_Header CFLKhdr;

		fread(&CFLKhdr,1,sizeof(CFLKhdr),fp);

		if(MAKEID('C','F','L','K') == CFLKhdr.CFLK_ID)
		{
			//endianswap = true;
			if (CFLOOK_VERSION != SwapInt32(CFLKhdr.version)) {
				regen = 1;
			}
		}
		if(MAKEID_SWAP('C','F','L','K') == CFLKhdr.CFLK_ID)
		{
			if(CFLOOK_VERSION != CFLKhdr.version)
				regen = 1;
		}

		fclose(fp);

		if(regen == 0)
			return;
	}
#if BINARY_LUT // binary look file

	fp = fopen(lutfile,"wb");
	if (fp != NULL)
	{
		CFLook_Header CFLKhdr;

		CFLKhdr.CFLK_ID = MAKEID_SWAP('C','F','L','K');	// CFLK identifier
		CFLKhdr.version = CFLOOK_VERSION; // version of this CFLK header
		CFLKhdr.hdrsize = 6*4+40;// header size, number of byte before the 3D LUT.
		CFLKhdr.lutsize = size;// size 'n' for an n x n x n 3D-LUT
		CFLKhdr.input_curve = CURVE_LOG_90;// CURVE_TYPE input to the LUT
		CFLKhdr.output_curve = CURVE_GAMMA_2pt2;// CURVE_TYPE output from the LUT

		char drive[260], dir[260], fname[260], ext[64];

		_splitpath(displayname, drive, dir, fname, ext);
		strncpy(CFLKhdr.displayname, fname, 39);

		fwrite(&CFLKhdr,1,sizeof(CFLook_Header),fp);
		fwrite(LUT,4,size*size*size*3,fp);
		fclose(fp);
	}
#else // simplified text IRIDAS look 32 LUT
	else if(fp = fopen(lutfile,"w"))
	{
		char space[16] = "\n      ";
		char header[100];

		sprintf(header,"<?xml version=\"1.0\" ?>\n<look>\n  <LUT>\n    <size>\"%d\"</size>\n    <data>\"", size);
		fwrite(header,1,strlen(header),fp);

		for(int i=0; i<size*size*size*3; i++)
		{
			char hexval[10];
			unsigned char *ptr = (unsigned char *)&LUT[i];

			if((i&7)==0)
				fwrite(space,1,strlen(space),fp);

			sprintf(hexval,"%02X%02X%02X%02X", ptr[0], ptr[1], ptr[2], ptr[3]);
			fwrite(hexval,1,8,fp);
		}



		char tail[100] = "\"\n    </data>\n  </LUT>\n</look>\n";
		fwrite(tail,1,strlen(tail),fp);
		fclose(fp);
	}
#endif
}


unsigned int ValidateLookGenCRC(char* path)
{
	int crc = 0;
	FILE *fp = fopen(path,"r");

	if(fp == NULL)
	{
#if OUTPUT
		OutputDebugString("ValidateLookGenCRC : no file");
#endif
		return 0;
	}
	else
	{
		uint32_t len=0,lastlen=0,pos=0;
		char buf[BUFSIZE];
		bool LUTfound = false;
		bool SIZEfound = false;
		bool DATAfound = false;
		bool finished = false;
		int size = 0;
		int rgbpos = 0;
		int entries = 0;
		float *LUT = NULL;
		unsigned char *iLUT = NULL;

		pos = 0;
		do
		{
			lastlen = len;
			if(pos)
				memcpy(buf, &buf[pos], lastlen-pos);

			len = (int)fread(&buf[lastlen-pos],1,BUFSIZE-(lastlen-pos),fp) + (lastlen-pos);

			pos = 0;

			if(!LUTfound)
			{
				do
				{
					if(strncmp(&buf[pos],"<LUT>",5) == 0)
					{
						pos+=5;
						LUTfound = true;
						break;
					}
					pos++;
				} while(pos < len-5);
			}
			else if(!SIZEfound)
			{
				do
				{
					if(strncmp(&buf[pos],"<size>",6) == 0)
					{
						int j = 0;
						pos+=7;
						while(buf[pos+j] >= '0' && buf[pos+j] <= '9') j++;
						buf[pos+j] = 0;

						size = atoi(&buf[pos]);

						if(size > 65)
						{
#if OUTPUT
							OutputDebugString("LUT too big");
#endif
							return 0;
						}
#if OUTPUT
						printf("size = %d\n",size);
#endif
						LUT = (float *)malloc(size*size*size*sizeof(float)*3);
						if(LUT == NULL)
						{
#if OUTPUT
							OutputDebugString("no memory\n");
#endif
							return 0;
						}
						iLUT = (unsigned char *)malloc(size*size*size*sizeof(char)*3);
						if(iLUT == NULL)
						{
#if OUTPUT
							OutputDebugString("no memory\n");
#endif
							return 0;
						}

						SIZEfound = true;
						break;

					}
					pos++;
				} while(pos < len-10);
			}
			else if(!DATAfound)
			{
				do
				{
					if(strncmp(&buf[pos],"<data>",6) == 0)
					{
						int j = 0;
						pos+=6;
						while(	!( (buf[pos+j] >= '0' && buf[pos+j] <= '9') ||
								(buf[pos+j] >= 'a' && buf[pos+j] <= 'f') ||
								(buf[pos+j] >= 'A' && buf[pos+j] <= 'F') ))
							pos++;

						//printf("%s\n",&buf[pos+j]);

						DATAfound = true;
						break;

					}
					pos++;
				} while(pos < len-256);
			}
			else if(DATAfound)
			{
				char hexstring[12] = "00000000";
				do
				{
					while(	!(  (buf[pos] >= '0' && buf[pos] <= '9') ||
								(buf[pos] >= 'a' && buf[pos] <= 'f') ||
								(buf[pos] >= 'A' && buf[pos] <= 'F') ))
					{
							if(buf[pos] == '"' || buf[pos] == '<')
							{
								finished = true;
#if OUTPUT
								OutputDebugString("finished\n");
#endif
								break;
							}
							pos++;
					}

					if(!finished)
					{
						float val;
						hexstring[0] = buf[pos+6];
						hexstring[1] = buf[pos+7];
						hexstring[2] = buf[pos+4];
						hexstring[3] = buf[pos+5];
						hexstring[4] = buf[pos+2];
						hexstring[5] = buf[pos+3];
						hexstring[6] = buf[pos+0];
						hexstring[7] = buf[pos+1];

						//printf("%s",hexstring);
						sscanf(hexstring, "%08x", (int *)&val);
#if OUTPUT && 0
						printf("%6.3f",val);
#endif
						LUT[entries] = val;
						rgbpos++;
						entries++;
						if(rgbpos < 3)
						{
#if OUTPUT && 0
							printf(",");
#endif
						}
						else
						{
#if OUTPUT && 0
							printf("\n");
#endif
							rgbpos = 0;
						}
					}

					pos+=8;
				} while(pos < len-16 && !finished);
			}

		//	printf("len = %d\n", len);
		}
		while(len > 0 && !finished);

		fclose(fp);

		if(finished && (size*size*size*3 == entries))
		{
			// valid 3D LUT
			crc = calccrc((unsigned char *)LUT, entries*4);
			char fullpath[260];
#if	_WIN32
			if(0 == ::GetLongPathName(path, fullpath, 259))
				strcat(fullpath, path);
#else
			strcpy(fullpath, path);
#endif

			GenerateLUTfile(crc, LUT, size, fullpath);
		}

		if(LUT)
			free(LUT), LUT=NULL;
	}
	return crc;
}

#endif


/*!
	@function CFHD_SetActiveMetadata

	@brief Recursively add active metadata for the decoder to use.

	@description Decoder will use the active metadata store in the sample, or in
	the color database or override by the Tags added by this function.  If you
	want the decoder to use the original camera data with a few change, initialize
	the metadata engine with @ref CFHD_InitSampleMetadata with the track set to
	METADATATYPE_ORIGINAL. Then call CFHD_SetActiveMetadata we the tag you want it
	to act upon (new whilebalanc, Look etc.)

	@param decoderRef An opaque reference to a decoder created by a
	call to @ref CFHD_OpenDecoder.

	@param metadataRef Reference to a metadata interface returned by a call
	to @ref CFHD_OpenMetadata.

	@param tag The FOURCC of the Tag you wish to add for active decoder
	control.

	@param type The data type of active metadata.

	@param data Pointer to the data.

	@param size The number of bytes of data.

	@return Returns a CFHD error code.
*/
CFHDDECODER_API CFHD_Error
CFHD_SetActiveMetadata(	CFHD_DecoderRef decoderRef,
						CFHD_MetadataRef metadataRef,
						unsigned int tag,
						CFHD_MetadataType type,
						void *data,
						unsigned int size)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (metadataRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}
	if (decoderRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	if ((tag == 0 && type != METADATATYPE_CINEFORM) || data == NULL || size == 0) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleMetadata *metadata = (CSampleMetadata *)metadataRef;
	CSampleDecoder *decoder = (CSampleDecoder *)decoderRef;

	//Hack, pass the decoders custom allocator on to CSampleMetadata
	metadata->SetAllocator(decoder->GetAllocator());

	//if(metadata->m_overrideSize == 0)
	{
		if(metadata->m_metadataTrack & METADATAFLAG_MODIFIED)
		{
			int data = 1;
			int typesizebytes = ('H'<<24)|4;

			metadata->AddMetaData(TAG_FORCE_DATABASE, typesizebytes, (void *)&data);

			data = 0;
			metadata->AddMetaData(TAG_IGNORE_DATABASE, typesizebytes, (void *)&data);
		}
		else
		{
			int data = 1;
			int typesizebytes = ('H'<<24)|4;

			metadata->AddMetaData(TAG_IGNORE_DATABASE, typesizebytes, (void *)&data);

			data = 0;
			metadata->AddMetaData(TAG_FORCE_DATABASE, typesizebytes, (void *)&data);
		}
	}

	unsigned int typesizebytes = 0;

	switch(type)
	{
		case METADATATYPE_STRING:
			typesizebytes = 'c' << 24;
			break;
		case METADATATYPE_UINT32:
			typesizebytes = 'L' << 24;
			break;
		case METADATATYPE_UINT16:
			typesizebytes = 'S' << 24;
			break;
		case METADATATYPE_UINT8:
			typesizebytes = 'B' << 24;
			break;
		case METADATATYPE_FLOAT:
			typesizebytes = 'f' << 24;
			break;
		case METADATATYPE_DOUBLE:
			typesizebytes = 'd' << 24;
			break;
		case METADATATYPE_GUID:
			typesizebytes = 'G' << 24;
			break;
		case METADATATYPE_XML:
			typesizebytes = 'x' << 24;
			break;
		case METADATATYPE_LONG_HEX:
			typesizebytes = 'H' << 24;
			break;
		case METADATATYPE_HIDDEN:
			typesizebytes = 'h' << 24;
			break;
		case METADATATYPE_TAG:
			typesizebytes = 'T' << 24;
			break;
		case METADATATYPE_UNKNOWN:
		default:
			break;
	}

	typesizebytes |= size;

	if(tag == TAG_CHANNELS_ACTIVE)
	{
		decoder->SetChannelsActive(*((uint32_t *)data));
	}
	if(tag == TAG_CHANNELS_MIX)
	{
		decoder->SetChannelMix(*((uint32_t *)data));
	}
	
	if(tag == TAG_LOOK_FILE)
	{
		uint32_t crc = 0;
		static char lastpath[260] = "";
		static char lastLUTfilename[40] = "";
		static uint32_t lastLUTcrc = 0;

		if(lastLUTcrc && 0 == strcmp(lastpath, (char *)data))
		{
			typesizebytes = ('c'<<24)|39;
			metadata->AddMetaData(TAG_LOOK_FILE, typesizebytes, (void *)&lastLUTfilename[0]);
			typesizebytes = ('H'<<24)|4;
			metadata->AddMetaData(TAG_LOOK_CRC, typesizebytes, (void *)&lastLUTcrc);
		}
		else 
		{
			char drive[260];
			char dir[260];
			char fname[260];
			char ext[64];
			char filename[260];
			
//DANREMOVE			crc = ValidateLookGenCRC((char *)data);

#ifdef _WIN32
			strcpy_s(lastpath, sizeof(lastpath), (char *)data);
			_splitpath_s((char *)data, drive, sizeof(drive), dir, sizeof(dir), fname, sizeof(fname), ext, sizeof(ext));
			_makepath_s(filename, sizeof(filename), NULL, NULL, fname, ext);
#else
			strcpy(lastpath, (char *)data);
			_splitpath((char *)data, drive, dir, fname, ext);
			_makepath(filename, NULL, NULL, fname, ext);
#endif

			if(strlen(filename) < 40)
			{
				typesizebytes = ('c'<<24)|39;
				metadata->AddMetaData(TAG_LOOK_FILE, typesizebytes, (void *)&filename[0]);

#ifdef _WIN32
				strcpy_s(lastLUTfilename, sizeof(lastLUTfilename), filename); 
#else
				strcpy(lastLUTfilename, filename);
#endif
				
				if(crc)
				{
					typesizebytes = ('H'<<24)|4;
					metadata->AddMetaData(TAG_LOOK_CRC, typesizebytes, (void *)&crc);
					lastLUTcrc = crc;
				}
			}
		}
	}
	else if(type == METADATATYPE_CINEFORM)
	{
		uint32_t *ptr = (uint32_t*)data;
		while(size>=12 && size < 4096)
		{

			uint32_t tag = *ptr++; size-=4;
			uint32_t typesizebytes = *ptr++; size-=4;
			uint32_t *newdata = ptr;
			uint32_t tagsize = typesizebytes & 0xffffff;

			metadata->AddMetaData(tag, typesizebytes, newdata);

			tagsize += 3;
			tagsize &=~3;
			size-=tagsize; ptr += tagsize/4;
		}
	}
	else if(tag == TAG_UNIQUE_FRAMENUM)
	{
		metadata->m_currentUFRM = *(uint32_t *)data;
	}
	else
	{
		if(metadata->m_metadataTrack & METADATAFLAG_LEFT_EYE)
		{
			metadata->AddMetaDataChannel(tag, typesizebytes, data, 1);
		}
		else if(metadata->m_metadataTrack & METADATAFLAG_RIGHT_EYE)
		{
			metadata->AddMetaDataChannel(tag, typesizebytes, data, 2);
		}
		else
		{
			metadata->AddMetaData(tag, typesizebytes, data);
		}
	}

	if(metadata->m_overrideSize)
	{
		decoder->SetDecoderOverrides(metadata->m_overrideData, metadata->m_overrideSize);
	}

	return errorCode;
}


CFHDDECODER_API CFHD_Error
CFHD_ClearActiveMetadata(	CFHD_DecoderRef decoderRef,
							CFHD_MetadataRef metadataRef)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (metadataRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}
	if (decoderRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleMetadata *metadata = (CSampleMetadata *)metadataRef;
	CSampleDecoder *decoder = (CSampleDecoder *)decoderRef;

	metadata->FreeDatabase();
	decoder->SetDecoderOverrides(NULL, 0);

	return errorCode;
}


/*!
	@function CFHD_GetThumbnail

	@brief The generate a thumbnail 

	@description Extract the base wavelet into a using image thumbnail without 
	decompressing the sample

	@param decoderRef An opaque reference to a decoder created by a
	call to @ref CFHD_OpenDecoder.

	@param samplePtr
	Pointer to a sample containing one frame of encoded video in the
	CineForm HD format.

	@param sampleSize
	Size of the encoded sample.

	@param outputBuffer
	Buffer that will receive the thumbnail of size 1/8 x 1/8 the original frame.

	@param outputBufferSize
	Size must be at least ((w+7)/8) * ((h+7)/8) * 4 for 10-bit RGB format.

	@param flags
	future usage
	
	@param retWidth
	If successful contains thumbnail width.

	@param retHeight
	If successful contains thumbnail Height.

	@param retSize
	If successful contains thumbnail size in bytes.

	@return Returns a CFHD error code.
*/
CFHDDECODER_API CFHD_Error
CFHD_GetThumbnail(CFHD_DecoderRef decoderRef,
				  void *samplePtr,
				  size_t sampleSize,
				  void *outputBuffer,
				  size_t outputBufferSize,
				  uint32_t flags,
				  size_t *retWidth = NULL,
				  size_t *retHeight = NULL,
				  size_t *retSize = NULL)
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (decoderRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}
	if (samplePtr == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}
	if (outputBuffer == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

	CSampleDecoder *decoder = reinterpret_cast<CSampleDecoder *>(decoderRef);

	// Have the thumbnail flags been set?
	if (flags == THUMBNAIL_FLAGS_NONE)
	{
		// Use the default thumbnail flags
		flags = THUMBNAIL_FLAGS_DEFAULT;
	}

	errorCode = decoder->GetThumbnail(
					samplePtr,
					sampleSize,
					outputBuffer,
					outputBufferSize,
					flags,
					retWidth,
					retHeight,
					retSize);

	return errorCode;
}



/*!
	@function CFHD_CreateImageDeveloper

	@brief Allocate a decoder for image development use upon uncompressed image data only.

	@description Do not pass a compressed image to this handle.

	@return Returns a CFHD error code.
*/
CFHDDECODER_API CFHD_Error
CFHD_CreateImageDeveloper(CFHD_DecoderRef decoderRef, 
						  uint32_t imageWidth, 
						  uint32_t imageHeight,
						  uint32_t sourceVideoChannels, 
						  CFHD_PixelFormat pixelFormatSrc,
						  CFHD_PixelFormat pixelFormatDst) //1 or 2 for 3D double high stacked
{
	CFHD_Error errorCode = CFHD_ERROR_OKAY;

	// Check the input arguments
	if (decoderRef == NULL) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}
	
	CSampleDecoder *decoder = (CSampleDecoder *)decoderRef;

	int actualWidthOut,actualHeightOut;
	CFHD_PixelFormat actualFormatOut;

	errorCode = decoder->PrepareDecoder(imageWidth,
										imageHeight,
										pixelFormatDst,
										(CFHD_DecodedResolution)sourceVideoChannels, // Reuse of the decode resolution for 2D vs 3D
										(CFHD_DecodingFlags)pixelFormatSrc,  // Reuse the the decoding flags
										NULL,//samplePtr,
										0,//sampleSize,
										&actualWidthOut,
										&actualHeightOut,
										&actualFormatOut);
	if (errorCode != CFHD_ERROR_OKAY) {
		return errorCode;
	}

	return CFHD_ERROR_OKAY;
}



#ifdef __cplusplus
}
#endif
