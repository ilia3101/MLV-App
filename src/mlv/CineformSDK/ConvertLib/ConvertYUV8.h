/*! @file ConvertYUV.h

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

void ConvertCbYCrY_8bitToNV12(void *input_buffer, int input_pitch,
							  void *output_buffer, int output_pitch,
							  int width, int height);


void ConvertYCrYCb_8bitToNV12(void *input_buffer, int input_pitch,
							  void *output_buffer, int output_pitch,
							  int width, int height);
