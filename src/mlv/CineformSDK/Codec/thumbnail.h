/*! @file thumbnail.h

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

#ifndef _THUMBNAIL_H
#define _THUMBNAIL_H

// Flags passed to the routine for generating thumbnails
typedef enum thumbnail_flags
{
	// The low eight bits determine the type of thumbnail
	THUMBNAIL_FLAGS_NONE = 0,
	THUMBNAIL_FLAGS_ENABLE = 1,
	THUMBNAIL_FLAGS_DPXC = 2,

	// Default thumbnail flags none provided
	THUMBNAIL_FLAGS_DEFAULT = THUMBNAIL_FLAGS_ENABLE,

	// The high eight bits determine the type of watermark
	THUMBNAIL_WATERMARK_MASK = 0x0F,
	THUMBNAIL_WATERMARK_SHIFT = 8,

	// Watermark bits in the high eight bits of the thumbnail flags
	THUMBNAIL_WATERMARK_EXPIRED = 1,
	THUMBNAIL_WATERMARK_444 = 2,
	THUMBNAIL_WATERMARK_RAW = 4,
	THUMBNAIL_WATERMARK_1080P = 8,
	THUMBNAIL_WATERMARK_3D = 16,

} THUMBNAIL_FLAGS;

// Watermark flags within the thumbnail flags
typedef enum thumbnail_watermark
{
	THUMBNAIL_WATERMARK_NONE = 0,

	//TODO: Finish the definitions of the watermark flags

} THUMBNAIL_WATERMARK;

// Table of channels offsets in a stereo sample
typedef struct channel_offsets
{
	int offset_count;
	int left_channel_offsets[CODEC_MAX_CHANNELS];
	int right_channel_offsets[CODEC_MAX_CHANNELS];

	//TODO: May need to modify this struct if the SAMPLE_HEADER changes

} CHANNEL_OFFSETS;

// Flags that control modifications to the lowpass values
typedef enum modify_lowpass_flags
{
	MODIFY_LOWPASS_DISABLE = 0,
	MODIFY_LOWPASS_ENABLE = 1,

} MODIFY_LOWPASS_FLAGS;


#ifdef __cplusplus
extern "C" {
#endif

bool GetThumbnailInfo(void *sample_ptr,
					  size_t sample_size,
					  uint32_t flags,
					  size_t *actual_width_out,
					  size_t *actual_height_out,
					  size_t *actual_size_out);

bool GenerateThumbnail(void *sample_ptr,
					   size_t sample_size,
					   void *output_buffer,
					   size_t output_size,
					   uint32_t flags,
					   size_t *actual_width_out,
					   size_t *actual_height_out,
					   size_t *actual_size_out);

// Get the lowpass image with the encoded dimensions and format
bool GetLowpassThumbnail(void *sample_ptr,
						 size_t sample_size,
						 void *output_buffer,
						 size_t output_size,
						 size_t *actual_width_out,
						 size_t *actual_height_out,
						 size_t *actual_size_out,
						 int *actual_format_out);

// Get the lowpass stereo thumbnail (left image over right image)
bool GetStereoThumbnail(void *sample_ptr,
						size_t sample_size,
						void *output_buffer,
						size_t output_size,
						size_t *actual_width_out,
						size_t *actual_height_out,
						size_t *actual_size_out,
						int *actual_format_out,
						CHANNEL_OFFSETS *channel_offsets_out);

void ChangeLowpassColumnValues(void *sample_buffer,
							   size_t sample_size,
							   int width,
							   int height,
							   int *channel_offsets,
							   uint16_t *y_column_values,
							   uint16_t *u_column_values,
							   uint16_t *v_column_values);

void ModifyLowpassColumnValues(void *sample_buffer,
							   size_t sample_size,
							   int width,
							   int height,
							   int *channel_offsets,
							   uint16_t *y_column_flags,
							   uint16_t *u_column_flags,
							   uint16_t *v_column_flags);

// Get the information about the lowpass image in the sample
bool GetLowpassInfo(void *sample_ptr,
					size_t sample_size,
					size_t *actual_width_out,
					size_t *actual_height_out,
					size_t *actual_size_out,
					int *actual_format_out);

// Get the lowpass image from the sample in its encoded format
bool GetLowpassImage(void *sample_ptr,
					 size_t sample_size,
					 void *output_buffer);

#ifdef __cplusplus
}
#endif

#endif
