/*! @file timing.h

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

#ifndef _TIMING_H
#define _TIMING_H

// Enable timing by default if in debug mode
#ifdef _WIN32
#ifndef _TIMING
#if defined(_DEBUG)
#define _TIMING _DEBUG
#endif
#endif
#endif

#if _TIMING

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>

// Filename used for importing into Excel
#define TIMING_CSV_FILENAME "c:/Cedoc/Results/timing.csv"

// The timer is a large integer as used by QueryPerformanceCounter
typedef __int64 TIMER;

// Construct for initializing timers
#define TIMER_INITIALIZER 0

// Conversion to units of milliseconds
#define SEC(timer) ((float)timer / (float)frequency)
#define MS(timer) (1000 * SEC(timer))

#else

#include <stdio.h>

#if __APPLE__
#include "macdefs.h"
#endif

// Use the absolute timer on the Macintosh
#include <mach/mach_time.h>

// The Macintosh absolute timer returns an unsigned 64-bit integer
typedef uint64_t TIMER;

#define TIMER_INITIALIZER 0

// Conversion to units of milliseconds
#define SEC(timer) AbsoluteTimeInSeconds(timer)
#define MS(timer) (1000 * SEC(timer))

#endif


// The counter is an unsigned doubleword integer
typedef uint32_t COUNTER;

// Construct for initializing counters
#define COUNTER_INITIALIZER 0


#ifdef __cplusplus
extern "C" {
#endif

BOOL InitTiming(void);
void StartTimer(TIMER *timer);
void StopTimer(TIMER *timer);
void DoThreadTiming(int startend);

#ifdef _WIN32
void PrintStatistics(FILE *logfile, int frame_count, HWND hwnd, char *results);
#else
void PrintStatistics(FILE *logfile, int frame_count, void *unused, char *results);
float AbsoluteTimeInSeconds(TIMER timer);
float AbsoluteTimerResolution();
#endif

#ifdef __cplusplus
}
#endif

// Define the timer start/stop macros if the configuration includes timing

#define START(timer)	{if (TIMING) StartTimer(&timer);}
#define STOP(timer)		{if (TIMING) StopTimer(&timer);}

#else

// Define null macros for the start and stop timer functions

#define START(timer)
#define STOP(timer)

#endif

#endif
