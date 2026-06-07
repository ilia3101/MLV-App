/*! @file cpuid.c

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

// Adapted from Microsoft Visual Studio .NET sample code

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include "../Common/macdefs.h"
#endif

#include <string.h>

#include "cpuid.h"


// These are the bit flags that get set on calling cpuid with register eax set to 1
#define _MMX_FEATURE_BIT        0x00800000
#define _SSE_FEATURE_BIT        0x02000000
#define _SSE2_FEATURE_BIT       0x04000000

// This bit is set when cpuid is called with register set to 80000001h (only applicable to AMD)
#define _3DNOW_FEATURE_BIT      0x80000000

#ifdef _WIN32

int GetProcessorCount()
{
	SYSTEM_INFO cSystem_info;
	GetSystemInfo(&cSystem_info);
	return cSystem_info.dwNumberOfProcessors;
}

#elif __APPLE__

#include <sys/types.h>
#include <sys/sysctl.h>

int GetProcessorCount()
{
	int mib[2];
	int processor_count;
	size_t length;

	mib[0] = CTL_HW;
	mib[1] = HW_NCPU;
	length = sizeof(processor_count);

	//sysctl(mib, 2, &processor_count, &length, NULL, 0);
	sysctlbyname("hw.physicalcpu", &processor_count, &length, NULL, 0);

	return processor_count;
}

#else	// Linux

#include <stdint.h>
//#include <sched.h>
#include <unistd.h>

int GetProcessorCount()
{
#if 0
	uint64_t cpu_mask;
	sched_getaffinity(0, sizeof(cpu_mask), &cpu_mask);

	int cpu_count = 0;
	while (cpu_mask != 0) {
		cpu_mask >>= 1;
		cpu_count++;
	}
#else
	int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
#endif

	return cpu_count;
}

#endif
