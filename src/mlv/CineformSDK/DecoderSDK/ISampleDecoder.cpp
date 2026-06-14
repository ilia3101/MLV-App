/*! @file ISampleDecoder.cpp

	@brief Interface for the sample decoder class.

	This class is used internally by CineForm software and is not currently
	mentioned in the documentation provided to customers.  Modify this comment
	and add tags for Doxygen to publish this interface in customer documentation.

	The interface uses pure virtual methods to isolate applications that use the
	CineForm decoder from changes to the codec library.  The interface includes
	macros (methods defined in the class declaration) for common calculations
	involving pixel formats.

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

#if _WIN32

// Export the interface to the decoder
#define DECODERDLL_EXPORTS	1

#elif __APPLE__


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


#if defined(__APPLE__)
#include "decoder.h"
#endif

#include "CFHDDecoder.h"
#include "SampleDecoder.h"
#include "ISampleDecoder.h"

CFHDDECODER_API ISampleDecoder *CFHD_CreateSampleDecoder(IAllocator *allocator, CFHD_LicenseKey license, FILE *logfile)
{
	// No longer supporting this type of codec interface, it was only used by the old Premiere importers.
	return CSampleDecoder::CreateSampleDecoder(allocator, license, logfile);
}