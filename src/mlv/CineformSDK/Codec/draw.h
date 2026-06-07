/*! @file draw.h

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

#ifndef _DRAW_H
#define _DRAW_H

#if _GRAPHICS

typedef enum {
	JUSTIFY_CENTER = 0,
	JUSTIFY_LEFT = 1,
	JUSTIFY_RIGHT = 2,
	JUSTIFY_TOP	= 4,
	JUSTIFY_TL = 5,
	JUSTIFY_TR = 6,
	JUSTIFY_BOTTOM = 8,
	JUSTIFY_BL = 9,
	JUSTIFY_BR = 10,

} JUSTICATION;

#ifdef __cplusplus
extern "C" {
#endif
	
int DrawOpen(DECODER *decoder);
void DrawClose(DECODER *decoder);
void DrawInit(DECODER *decoder);
void DrawSafeMarkers(DECODER *decoder);
void DrawMetadataObjects(DECODER *decoder);	
void DrawMetadataString(DECODER *decoder, unsigned char type, int size, void *data, int parallax);
// scale = 1.0, match PNG size
void DrawPNG(DECODER *decoder, char *path, float scaleX, float scaleY, int parameter, int *ret_width, int *ret_height, char *ret_path);
void DrawPrepareTool(DECODER *decoder, char *tool, char *subtype, float scaleX, float scaleY, int parameter);

void DrawScreen(DECODER *decoder, uint8_t *output, int pitch, int output_format);
void DrawHistogram(DECODER *decoder, int parallax);
void DrawWaveform(DECODER *decoder, int parallax);
void DrawVectorscope(DECODER *decoder, int parallax);
void DrawGrid(DECODER *decoder, int parallax);
void DrawStartThreaded(DECODER *decoder);
void DrawWaitThreaded(DECODER *decoder);

//void HistogramRender(DECODER *decoder, uint8_t *output, int pitch, int output_format, int x, int targetW, int targetH);

void GetDisplayParameters(DECODER *decoder, unsigned char *ptr, int len);
//void GetCurrentID(DECODER *decoder, unsigned char *ptr, int len, char *id);

#ifdef __cplusplus
}
#endif

#endif

#endif
