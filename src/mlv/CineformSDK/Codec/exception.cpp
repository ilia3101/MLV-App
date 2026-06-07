/*! @file exception.cpp

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

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <typeinfo>
#if _STACKWALKER
#include "../../gopro-contrib-win-stackwalker/StackWalker.h"
#endif
#include "exception.h"


/*!
	@brief Trap system exceptions and throw an exception class instance

	This function is called when a system exception occurs in C/C++ code.
	An instance of a user-defined C++ class is created to hold information
	collected from the exception record and context record and the object
	is throw to signal the exception.

	Must call the function _set_se_translator in every thread where any
	system exception should be trapped and the code must be compiled with
	the asynchronous exception switch /EHa which is not set by default.

	Asynchronous exceptions may result in larger code, so it may be best
	to use this technique in a special build for debugging system errors.

	The exception handler should not try to allocate memory or perform any
	input or output or do anything that may cause another system exception.

	The exception code passed as the first argument should equal the exception
	code in the exception record: pe->ExceptionRecord->ExceptionCode.
*/
void DefaultExceptionHandler(unsigned int code, EXCEPTION_POINTERS *pe)
{
#if 0
    //printf("Inside exception translator\n");

	// The first argument is the exception code
	uint32_t exception_code = code;

	if (pe && pe->ContextRecord)
	{
		// Use the context record to output a stack trace
		StackWalker sw;
		sw.ShowCallstack(GetCurrentThread(), pe->ContextRecord);
	}

	// Use the information in the exception record (if available)
	if (pe && pe->ExceptionRecord)
	{
		void *exception_address = pe->ExceptionRecord->ExceptionAddress;
		throw SystemException(exception_code, exception_address);
	}
	else
	{
		throw SystemException(exception_code);
	}
#endif
}

/*!
	@brief Set the exception handler for trapping system errors

	The old exteption handler is returned so that it may be restored
	after the new exception handler is no longer needed.
*/
ExceptionHandlerProc *SetExceptionHandler(ExceptionHandlerProc handler)
{
#if 0
	return _set_se_translator(handler);
#else
	return NULL;
#endif
}
#endif

// Suppress warnings about redundant specification of calling conventions
#pragma warning(disable: 791)

// Hide the stack walker from the rest of the codec
#if _STACKWALKER
#include "../../contrib/StackWalker/StackWalker.cpp"
#else
#include "exception.h"

void DefaultExceptionHandler()
{
}

void *SetExceptionHandler()
{
	return 0;
}
#endif
