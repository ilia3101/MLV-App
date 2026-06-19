/*! @file debug.c

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
#include "debug.h"

#if _DEBUG

FILE *debugfile = NULL;		// Debug output file
char debug[256];			// Array of debug flags

void InitDebugFlags(char *flags)
{
	unsigned int chr;

	for (chr = 0; chr < sizeof(debug)/sizeof(debug[0]); chr++) {
		debug[chr] = 0;
	}

	SetDebugFlags(flags);
}

void SetDebugFlags(const char *flags)
{
	while (*flags != '\0') {
		uint8_t index = (uint8_t)(*(flags++));
		debug[index] = 1;
	}
}

void ClearDebugFlags(const char *flags)
{
	while (*flags != '\0') {
		uint8_t index = (uint8_t)(*(flags++));
		debug[index]= 0;
	}
}

void GetDebugFlags(char *flags)
{
	unsigned int chr = 1;	// Skip the nul character

	for (; chr < sizeof(debug)/sizeof(debug[0]); chr++) {
		if (debug[chr]) *(flags++) = chr;
	}

	// Terminate the string
	*flags = '\0';
}

#endif
