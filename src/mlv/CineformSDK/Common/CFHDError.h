/*! @file CFHDError.h
*
*  @brief List of error codes returned from the CineForm codec SDKs.
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
*/

#pragma once
#ifndef CFHD_ERROR_H
#define CFHD_ERROR_H

typedef enum CFHD_Error
{
	CFHD_ERROR_OKAY = 0,
	CFHD_ERROR_INVALID_ARGUMENT,
	CFHD_ERROR_OUTOFMEMORY,
	CFHD_ERROR_BADFORMAT,
	CFHD_ERROR_BADSCALING,
	CFHD_ERROR_BADSAMPLE,
	CFHD_ERROR_INTERNAL,
	CFHD_ERROR_METADATA_CLASS,
	CFHD_ERROR_METADATA_UNDEFINED,
	CFHD_ERROR_METADATA_END,
	CFHD_ERROR_UNEXPECTED,
	CFHD_ERROR_BAD_RESOLUTION,
	CFHD_ERROR_BAD_PIXEL_SIZE,
	CFHD_ERROR_NOT_FINISHED,
	CFHD_ERROR_ENCODING_NOT_STARTED,
	CFHD_ERROR_METADATA_ATTACHED,
	CFHD_ERROR_BAD_METADATA,
	CFHD_ERROR_THREAD_CREATE_FAILED,
	CFHD_ERROR_THREAD_WAIT_FAILED,
	CFHD_ERROR_UNKNOWN_TAG,
	CFHD_ERROR_LICENSING,

	// Error codes returned by the codec library
	CFHD_ERROR_CODEC_ERROR = 2048,
	CFHD_ERROR_DECODE_BUFFER_SIZE,

	// Error codes used by the sample code distributed with the codec SDK
	CFHD_ERROR_SAMPLE_CODE = 4096,
	CFHD_ERROR_FILE_CREATE,
	CFHD_ERROR_FILE_OPEN,
	CFHD_ERROR_BADFILE,
	CFHD_ERROR_READ_FAILURE,
	CFHD_ERROR_WRITE_FAILURE,
	CFHD_ERROR_FILE_SIZE,
	CFHD_ERROR_END_OF_FILE,
	CFHD_ERROR_END_OF_DATABASE,
	CFHD_ERROR_THREAD,

	// CFHD codec error codes in the Macintosh coding style
	kCFHDErrorOkay = CFHD_ERROR_OKAY,
	kCFHDErrorInvalidArgument = CFHD_ERROR_INVALID_ARGUMENT,
	kCFHDErrorOutOfMemory = CFHD_ERROR_OUTOFMEMORY,
	kCFHDErrorBadFormat = CFHD_ERROR_BADFORMAT,
	kCFHDErrorBadScaling = CFHD_ERROR_BADSCALING,
	kCFHDErrorBadSample = CFHD_ERROR_BADSAMPLE,
	kCFHDErrorCodecError = CFHD_ERROR_CODEC_ERROR,
	kCFHDErrorInternal = CFHD_ERROR_INTERNAL,
	kCFHDErrorMetadataClass = CFHD_ERROR_METADATA_CLASS,
	kCFHDErrorMetadataUndefined = CFHD_ERROR_METADATA_UNDEFINED,

	// Error codes used by the sample code (Macintosh style)
	kCFHDErrorBadFile = CFHD_ERROR_BADFILE,
	kCFHDErrorReadFailure = CFHD_ERROR_READ_FAILURE,
	kCFHDErrorWriteFailure = CFHD_ERROR_WRITE_FAILURE,

} CFHD_Error;

#endif // CFHD_ERROR_H
