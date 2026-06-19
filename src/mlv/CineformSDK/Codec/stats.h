/*! @file stats.h

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

#ifndef _STATS_H
#define _STATS_H

#include "wavelet.h"
#include "image.h"

// Generates encoding statistics that are used to optimize the codebooks

//#ifndef _STATS
//#define _STATS _DEBUG
//#endif

// Sets of statistics are identified by an opaque integer identifier so
// that the routines can support multiple sets of histograms in the future

#define STATS_DEFAULT 0


void DumpString(char *txt);

#if (_STATS || 0)

void ReadStats(int stats);
//void CountValues(int stats, int count, int value);
void CountValues(int stats, int value, int num);
void CountRuns(int stats, int count);
void UpdateStats(int stats);
void NewSubBand(int width,int height,int first, int bits, int overhead);
void SetQuantStats(int quantization);
void StatsAverageLevels(IMAGE *frame);
void StatsMemoryAlloc(int size, char *func);
void DumpText(char *txt, int hex);
void DumpData(int a, int b, int c);

#else

#define ReadStats(stats)
//#define CountValues(stats, count, value)
#define CountValues(stats, value, num)
#define CountRuns(stats, count)
#define UpdateStats(stats)
#define NewSubBand(width, height, first, bits, overhead)
#define SetQuantStats(quantization)
#define StatsAverageLevels(frame)
#define StatsMemoryAlloc(size, func)
#define DumpText(txt, hex)
#define DumpData(a, b, c);

#endif

#endif
