/*! @file Interface.h

*  @brief Define macros that are used to specify external entry points.
*  
*  Some platforms such as Windows require special attribute declarations
*  that determine whether a routine is beining imported or exported as an
*  external entry point.  This file defines the interface macros used in
*  the declarations of routines that imported or exported.
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

#ifdef _WIN32

// Export the interface to the encoder
#ifndef ENCODERDLL_EXPORTS
#define ENCODERDLL_EXPORTS	1
#endif

#elif __APPLE__

#ifndef ENCODERDLL_EXPORTS
#define ENCODERDLL_EXPORTS	1
#endif

#include <CoreFoundation/CoreFoundation.h>
#include <uuid/uuid.h>

#else

//TODO: Change this code to define CFHDENCODER_API instead of ENCODERDLL_API

#ifdef ENCODERDLL_API
#undef ENCODERDLL_API
#endif

#define ENCODERDLL_API __attribute__((visibility("default")))

#endif
