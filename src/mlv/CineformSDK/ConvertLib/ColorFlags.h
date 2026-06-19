/*! @file ColorFlags.h

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

// Color conversion flag bits

#define	VSRGB	1	// Video safe range (versus full range)
#define	CS709	2	// Color space is 709 (versus 601)

#define COLOR_FLAGS_MASK	0x03

typedef enum color_flags
{
	COLOR_FLAGS_BT601 = 0,
	COLOR_FLAGS_VSRGB = VSRGB,
	COLOR_FLAGS_CS709 = CS709,
	COLOR_FLAGS_VS709 = VSRGB+CS709,

	// Other names for the folor flags
	COLOR_FLAGS_CS_601 = COLOR_FLAGS_BT601,
	COLOR_FLAGS_CS_709 = COLOR_FLAGS_CS709,
	COLOR_FLAGS_VS_601 = COLOR_FLAGS_VSRGB,
	COLOR_FLAGS_VS_709 = COLOR_FLAGS_VS709,

	// Default color space is 709 and not video safe
	COLOR_FLAGS_DEFAULT = COLOR_FLAGS_CS_709

} COLOR_FLAGS;

typedef COLOR_FLAGS ColorFlags;
