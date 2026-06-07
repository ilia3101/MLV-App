/*! @file exception.h

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

#pragma once

#if defined(__APPLE__)

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include "../Common/macdefs.h"

#define EXCEPTION_POINTERS int

#elif defined(__GNUC__)

#include <stdlib.h>
#include <stdint.h>

#endif

#ifdef __cplusplus

/*!
	@brief Class for representing system exceptions in a C++ catch or throw

	The exception includes an error code that is defined in winnt.h and the address
	at which the exception occurred, but I do not know whether this address is code
	or data.  The context record contains the instruction pointer for the program
	location at which the error occurred as well as the other registers, including
	the stack pointer.
	
	Other information is available in the exception and context records that are
	passed to the exception handler.
*/
class SystemException
{
public:

	SystemException(uint32_t exception_code, void *exception_address = NULL) :
		code(exception_code),
		address(exception_address)
	{
	}

	// Information obtained from the exception record and context
	uint32_t code;
	void *address;

};

extern "C" {

#endif

#if defined(__APPLE__) || defined(__GNUC__)

	typedef void ExceptionHandlerProc();
	
	// Platform-independent function to set the exception handler
	void *SetExceptionHandler();
	
	// Translate system exceptions into C++ exceptions
	void DefaultExceptionHandler();
	
	// Set the default handler for system exceptions
	static void SetDefaultExceptionHandler()
	{
		SetExceptionHandler();
	}

#else

	// Declaration for exception handlers
	typedef void ExceptionHandlerProc(unsigned int, struct _EXCEPTION_POINTERS*);

	// Platform-independent function to set the exception handler
	ExceptionHandlerProc *SetExceptionHandler(ExceptionHandlerProc handler);
	
	// Translate system exceptions into C++ exceptions
	void DefaultExceptionHandler(unsigned int u, struct _EXCEPTION_POINTERS *pe);

	// Set the default handler for system exceptions
	static void SetDefaultExceptionHandler()
	{
		SetExceptionHandler(DefaultExceptionHandler);
	}

#endif

#ifdef __cplusplus
}
#endif
