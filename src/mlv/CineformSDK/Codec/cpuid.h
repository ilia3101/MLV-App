/*! @file cpuid.h

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

#ifndef _CPUID_H
#define _CPUID_H

#define _CPU_FEATURE_MMX    0x0001
#define _CPU_FEATURE_SSE    0x0002
#define _CPU_FEATURE_SSE2   0x0004
#define _CPU_FEATURE_3DNOW  0x0008

#define _MAX_VNAME_LEN  13
#define _MAX_MNAME_LEN  30

#ifdef __cplusplus
extern "C" {
#endif
	
int GetProcessorCount();

#ifdef __cplusplus
}
#endif

#endif
