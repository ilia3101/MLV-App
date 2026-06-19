/*! @file debug.h

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

#ifndef _DEBUG_H
#define _DEBUG_H

#if _DEBUG

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern FILE *debugfile;		// Debug output file
extern char debug[256];		// Array of debug flags

void InitDebugFlags(char *flags);
void SetDebugFlags(const char *flags);
void ClearDebugFlags(const char *flags);
void GetDebugFlags(char *flags);

#ifdef __cplusplus
}
#endif

#else

#define InitDebugFlags(flags)
#define SetDebugFlags(flags)
#define ClearDebugFlags(flags)
#define GetDebugFlags(flags)

#endif

#endif