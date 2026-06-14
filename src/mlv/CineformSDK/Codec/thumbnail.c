/*! @file thumbnail.c

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
#include "bitstream.h"
#include "decoder.h"
#include "encoder.h"
#include "swap.h"
#include "thumbnail.h"


bool GetThumbnailInfo(void *sample_ptr,
					  size_t sample_size,
					  uint32_t flags,
					  size_t *width_out,
					  size_t *height_out,
					  size_t *size_out)
{
	BITSTREAM input;
	SAMPLE_HEADER header;

	InitBitstreamBuffer(&input, (BITWORD *)sample_ptr, sample_size, BITSTREAM_ACCESS_READ);
	memset(&header, 0, sizeof(SAMPLE_HEADER));
	header.find_lowpass_bands = 1;

	if (ParseSampleHeader(&input, &header))
	{
		int width = (header.width+7)/8;
		int height = (header.height+7)/8;

		if (width_out) {
			*width_out = width;
		}
		if (height_out) {
			*height_out = height;
		}
		if (size_out) {
			size_t pixel_size = 4;
			size_t frame_size = width * height * pixel_size;
			*size_out = frame_size;
		}
		return true;
	}
	return false;
}

bool GenerateThumbnail(void *sample_ptr,
					   size_t sample_size,
					   void *output_buffer,
					   size_t output_size,
					   uint32_t flags, // Now carries the pixelformat
					   size_t *retWidth,
					   size_t *retHeight,
					   size_t *retSize)
{
	BITSTREAM input;
	SAMPLE_HEADER header;
	uint8_t *ptr = sample_ptr;
	BITLONG *optr = (BITLONG *)output_buffer;
	
	InitBitstreamBuffer(&input, (BITWORD *)sample_ptr, sample_size, BITSTREAM_ACCESS_READ);
	
	memset(&header, 0, sizeof(SAMPLE_HEADER));
	header.find_lowpass_bands = 1;

	if (ParseSampleHeader(&input, &header))
	{
		uint32_t *yptr;
		uint16_t *uptr16;
		uint16_t *vptr16;
		uint32_t *gptr;
		uint32_t *gptr2;
		uint32_t *rptr;
		uint32_t *rptr2;
		uint32_t *bptr;
		uint32_t *bptr2;
		int x,y,width,height;

		if(header.hdr_uncompressed == 1 || header.encoded_format == ENCODED_FORMAT_BAYER)
		{
			InitBitstreamBuffer(&input, (BITWORD *)sample_ptr, sample_size, BITSTREAM_ACCESS_READ);
		
			{
				bool result;
				DECODER decoder;
				int q_width = ((header.width+7)/8)*2;
				int q_height = ((header.height+7)/8)*2;
				int q_pitch = q_width * 4; 
#ifdef __APPLE__
				uint8_t *buffer = malloc(q_pitch * q_height);
#else
				uint8_t *buffer = _mm_malloc(q_pitch * q_height, 256);
#endif

				if(buffer)
				{
					memset(&decoder, 0, sizeof(DECODER));
									
					#if _ALLOCATOR
					DecodeInit(NULL, &decoder, header.width, header.height, DECODED_FORMAT_DPX0, DECODED_RESOLUTION_QUARTER, NULL);
					#else
					DecodeInit(&decoder, header.width, header.height, DECODED_FORMAT_DPX0, DECODED_RESOLUTION_QUARTER, NULL);
					#endif


					decoder.basic_only = 1;
					decoder.flags = DECODER_FLAGS_RENDER;

					// Decode the sample
					result = DecodeSample(&decoder, &input, buffer, q_pitch, NULL, NULL);

					ClearDecoder(&decoder);

					if(result != true)
					{
#ifdef __APPLE__
						free(buffer);
#else
						_mm_free(buffer);
#endif
						return false;
					}
					else
					{
						int x,y;
						for(y=0; y<q_height; y+=2)
						{	
							uint32_t *rgbptr;
							rgbptr = (uint32_t *)buffer; 
							rgbptr += y * q_width;

							for(x=0; x<q_width; x+=2)
							{
								uint32_t rgb = *rgbptr;
								rgbptr+= 2; // two RGB pixels
														
								*(optr++) = rgb;
							}
						}
					}

#ifdef __APPLE__
					free(buffer);
#else
					_mm_free(buffer);
#endif
					goto finished;
				}
			}
		}

		width = (header.width+7)/8;
		height = (header.height+7)/8;

		//if (flags & 3)
		{
			if (output_size < (size_t)width * height * 4) {
				return false; // failed
			}

			// Convert the lowpass image to an RGB thumbnail
			switch(header.encoded_format)
			{
				case ENCODED_FORMAT_UNKNOWN:
				case ENCODED_FORMAT_YUV_422:
					yptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[0]);
					uptr16 = (uint16_t *)(ptr + header.thumbnail_channel_offsets[1]);
					vptr16 = (uint16_t *)(ptr + header.thumbnail_channel_offsets[2]);
					for(y=0; y<height; y++)
					{
						int shift = 4;

						if(header.key_frame && !header.droppable_frame) // 2 frame GOP
							shift = 5;
						else if(!header.key_frame)
							shift = 5;


						for(x=0; x<width; x+=2)
						{
							int y1,u1,v1,y2,r,g,b,rgb,pp;

							pp = _bswap(*yptr++);
							y1 = ((pp>>(shift+16)) & 0x3ff) - 64;
							y2 = ((pp>>shift) & 0x3ff) - 64;
							pp = SwapInt16(*uptr16++);
							u1 = ((pp>>shift) & 0x3ff) - 0x200;
							pp = SwapInt16(*vptr16++);
							v1 = ((pp>>shift) & 0x3ff) - 0x200;

							r = (1192*y1 + 1836*u1)>>10;
							g = (1192*y1 - 547*u1 - 218*v1)>>10;
							b = (1192*y1 + 2166*v1)>>10;
							if(r<0) r=0; if(r>0x3ff) r=0x3ff;
							if(g<0) g=0; if(g>0x3ff) g=0x3ff;
							if(b<0) b=0; if(b>0x3ff) b=0x3ff;
							rgb = ((r<<22)|(g<<12)|(b<<2));
							*(optr++) = _bswap(rgb);

							r = (1192*y2 + 1836*u1)>>10;
							g = (1192*y2 - 547*u1 - 218*v1)>>10;
							b = (1192*y2 + 2166*v1)>>10;
							if(r<0) r=0; if(r>0x3ff) r=0x3ff;
							if(g<0) g=0; if(g>0x3ff) g=0x3ff;
							if(b<0) b=0; if(b>0x3ff) b=0x3ff;
							rgb = ((r<<22)|(g<<12)|(b<<2));
							*(optr++) = _bswap(rgb);
						}
					}
					break;
				case ENCODED_FORMAT_BAYER:
					// Expand the Bayer data to the same size as other thumbnails
					for(y=0; y<height; y++)
					{
						int y1 = (y)/2;
						int y2 = (y+1)/2;
						if(y2 == height/2)
							y2--;
						if(y1 == height/2)
							y1--;
						gptr = gptr2 = (uint32_t *)(ptr + header.thumbnail_channel_offsets[0]);
						gptr += (y1) * (width/4);
						rptr = rptr2 = (uint32_t *)(ptr + header.thumbnail_channel_offsets[1]);
						rptr += (y1) * (width/4);
						bptr = bptr2 = (uint32_t *)(ptr + header.thumbnail_channel_offsets[2]);
						bptr += (y1) * (width/4);

						if(y1!=y2)
						{
							gptr2 += (y2) * (width/4);
							rptr2 += (y2) * (width/4);
							bptr2 += (y2) * (width/4);

							for(x=0; x<width; x+=4)
							{
								int r,g,b,r1,g1,b1,r2,g2,b2,r3,g3,b3,r4,g4,b4,rgb,pp;

								pp = _bswap(*gptr++);
								g1 = (pp>>20) & 0x3ff;
								g2 = (pp>>4) & 0x3ff;
								pp = _bswap(*gptr2++);
								g3 = (pp>>20) & 0x3ff;
								g4 = (pp>>4) & 0x3ff;
								pp = _bswap(*rptr++);
								r1 = (pp>>20) & 0x3ff;
								r2 = (pp>>4) & 0x3ff;
								pp = _bswap(*rptr2++);
								r3 = (pp>>20) & 0x3ff;
								r4 = (pp>>4) & 0x3ff;
								pp = _bswap(*bptr++);
								b1 = (pp>>20) & 0x3ff;
								b2 = (pp>>4) & 0x3ff;
								pp = _bswap(*bptr2++);
								b3 = (pp>>20) & 0x3ff;
								b4 = (pp>>4) & 0x3ff;

								r1 = (r1-0x200)*2 + g1;
								if(r1<0) r1=0;
								if(r1>0x3ff) r1=0x3ff;
								b1 = (b1-0x200)*2 + g1;
								if(b1<0) b1=0;
								if(b1>0x3ff) b1=0x3ff;

								r2 = (r2-0x200)*2 + g2;
								if(r2<0) r2=0;
								if(r2>0x3ff) r2=0x3ff;
								b2 = (b2-0x200)*2 + g2;
								if(b2<0) b2=0;
								if(b2>0x3ff) b2=0x3ff;

								r3 = (r3-0x200)*2 + g3;
								if(r3<0) r3=0;
								if(r3>0x3ff) r3=0x3ff;
								b3 = (b3-0x200)*2 + g3;
								if(b3<0) b3=0;
								if(b3>0x3ff) b3=0x3ff;

								r4 = (r4-0x200)*2 + g4;
								if(r4<0) r4=0;
								if(r4>0x3ff) r4=0x3ff;
								b4 = (b2-0x200)*2 + g4;
								if(b4<0) b4=0;
								if(b4>0x3ff) b4=0x3ff;

								r = (r1+r3)>>1;
								g = (g1+g3)>>1;
								b = (b1+b3)>>1;
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*(optr++) = _bswap(rgb);
								
								r = (r1+r2+r3+r4)>>2;
								g = (g1+g2+g3+g4)>>2;
								b = (b1+b2+b3+b4)>>2;
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*(optr++) = _bswap(rgb);

								r = (r2+r4)>>1;
								g = (g2+g4)>>1;
								b = (b2+b4)>>1;
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*(optr++) = _bswap(rgb);
								
								
								pp = _bswap(*gptr);
								g1 = (pp>>20) & 0x3ff;
								pp = _bswap(*gptr2);
								g3 = (pp>>20) & 0x3ff;
								pp = _bswap(*rptr);
								r1 = (pp>>20) & 0x3ff;
								pp = _bswap(*rptr2);
								r3 = (pp>>20) & 0x3ff;
								pp = _bswap(*bptr);
								b1 = (pp>>20) & 0x3ff;
								pp = _bswap(*bptr2);
								b3 = (pp>>20) & 0x3ff;

								
								r1 = (r1-0x200)*2 + g1;
								if(r1<0) r1=0;
								if(r1>0x3ff) r1=0x3ff;
								b1 = (b1-0x200)*2 + g1;
								if(b1<0) b1=0;
								if(b1>0x3ff) b1=0x3ff;

								r3 = (r3-0x200)*2 + g3;
								if(r3<0) r3=0;
								if(r3>0x3ff) r3=0x3ff;
								b3 = (b3-0x200)*2 + g3;
								if(b3<0) b3=0;
								if(b3>0x3ff) b3=0x3ff;

								r = (r1+r2+r3+r4)>>2;
								g = (g1+g2+g3+g4)>>2;
								b = (b1+b2+b3+b4)>>2;
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*(optr++) = _bswap(rgb);
							}
						}
						else
						{
							for(x=0; x<width; x+=4)
							{
								int r,g,b,r1,g1,b1,r2,g2,b2,rgb,pp;

								pp = _bswap(*gptr++);
								g1 = (pp>>20) & 0x3ff;
								g2 = (pp>>4) & 0x3ff;
								pp = _bswap(*rptr++);
								r1 = (pp>>20) & 0x3ff;
								r2 = (pp>>4) & 0x3ff;
								pp = _bswap(*bptr++);
								b1 = (pp>>20) & 0x3ff;
								b2 = (pp>>4) & 0x3ff;
								
								r1 = (r1-0x200)*2 + g1;
								if(r1<0) r1=0;
								if(r1>0x3ff) r1=0x3ff;
								b1 = (b1-0x200)*2 + g1;
								if(b1<0) b1=0;
								if(b1>0x3ff) b1=0x3ff;

								r2 = (r2-0x200)*2 + g2;
								if(r2<0) r2=0;
								if(r2>0x3ff) r2=0x3ff;
								b2 = (b2-0x200)*2 + g2;
								if(b2<0) b2=0;
								if(b2>0x3ff) b2=0x3ff;

								rgb = ((r1<<22)|(g1<<12)|(b1<<2));
								*(optr++) = _bswap(rgb);
								
								r = (r1+r2)>>1;
								g = (g1+g2)>>1;
								b = (b1+b2)>>1;
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*(optr++) = _bswap(rgb);

								rgb = ((r2<<22)|(g2<<12)|(b2<<2));
								*(optr++) = _bswap(rgb);
								
								pp = _bswap(*gptr);
								g1 = (pp>>20) & 0x3ff;
								pp = _bswap(*rptr);
								r1 = (pp>>20) & 0x3ff;
								pp = _bswap(*bptr);
								b1 = (pp>>20) & 0x3ff;
								
								r1 = (r1-0x200)*2 + g1;
								if(r1<0) r1=0;
								if(r1>0x3ff) r1=0x3ff;
								b1 = (b1-0x200)*2 + g1;
								if(b1<0) b1=0;
								if(b1>0x3ff) b1=0x3ff;

								r = (r1+r2)>>1;
								g = (g1+g2)>>1;
								b = (b1+b2)>>1;
								rgb = ((r<<22)|(g<<12)|(b<<2));
								*(optr++) = _bswap(rgb);
							}
						}
					}
					break;

				case ENCODED_FORMAT_RGB_444:
				case ENCODED_FORMAT_RGBA_4444:
					// Return an RGB thumbnail
					gptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[0]);
					rptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[1]);
					bptr = (uint32_t *)(ptr + header.thumbnail_channel_offsets[2]);
					for(y=0; y<height; y++)
					{
						for(x=0; x<width; x+=2)
						{
							int r1,g1,b1,r2,g2,b2,rgb,pp;

							pp = _bswap(*gptr++);
							g1 = (pp>>20) & 0x3ff;
							g2 = (pp>>4) & 0x3ff;
							pp = _bswap(*rptr++);
							r1 = (pp>>20) & 0x3ff;
							r2 = (pp>>4) & 0x3ff;
							pp = _bswap(*bptr++);
							b1 = (pp>>20) & 0x3ff;
							b2 = (pp>>4) & 0x3ff;
							rgb = ((r1<<22)|(g1<<12)|(b1<<2));
							*(optr++) = _bswap(rgb);

							rgb = ((r2<<22)|(g2<<12)|(b2<<2));
							*(optr++) = _bswap(rgb);
						}
					}
					break;

				default:
					assert(0);
					break;
			}
		}
	}

finished:
	if(retWidth)
		*retWidth = (header.width+7)/8;
	if(retHeight)
		*retHeight = (header.height+7)/8;
	if(retSize)
		*retSize = (*retWidth * *retHeight * 4);

	return true;
}

static void ConvertLowpassYU64(void *sample_buffer,
							   size_t sample_size,
							   int width,
							   int height,
							   int *channel_offsets,
							   void *output_buffer,
							   size_t output_size)
{
	uint8_t *sample = (uint8_t *)sample_buffer;

	uint32_t *y_ptr;
	uint32_t *u_ptr;
	uint32_t *v_ptr;

	uint16_t *outptr = (uint16_t *)output_buffer;

	int row, column;

	// Get pointers to the lowpass images in each channel
	y_ptr = (uint32_t *)(sample + channel_offsets[0]);
	u_ptr = (uint32_t *)(sample + channel_offsets[1]);
	v_ptr = (uint32_t *)(sample + channel_offsets[2]);

	for (row = 0; row < height; row++)
	{
		// Process four luma and chroma pairs per iteration
		for (column = 0; column < width; column += 4)
		{
			uint32_t y1, y2, y3, y4;
			uint32_t u1, v1, u2, v2;
			uint32_t word;

			// Scale the 10-bit values to 16 bits
			const int shift = 6;

			word = _bswap(*y_ptr++);
			y1 = ((word >> 20) & 0x3FF);
			y2 = ((word >>  4) & 0x3FF);

			word = _bswap(*y_ptr++);
			y3 = ((word >> 20) & 0x3FF);
			y4 = ((word >>  4) & 0x3FF);

			word = _bswap(*u_ptr++);
			u1 = ((word >> 20) & 0x3FF);
			u2 = ((word >>  4) & 0x3FF);

			word = _bswap(*v_ptr++);
			v1 = ((word >> 20) & 0x3FF);
			v2 = ((word >>  4) & 0x3FF);

			// Scale to 16 bit precision
			y1 <<= shift;
			y2 <<= shift;
			y3 <<= shift;
			y4 <<= shift;
			u1 <<= shift;
			u2 <<= shift;
			v1 <<= shift;
			v2 <<= shift;

			*(outptr++) = y1;
			*(outptr++) = u1;
			*(outptr++) = y2;
			*(outptr++) = v1;
			*(outptr++) = y3;
			*(outptr++) = u2;
			*(outptr++) = y4;
			*(outptr++) = v2;
		}
	}
}

bool GetLowpassThumbnail(void *sample_buffer,
						 size_t sample_size,
						 void *output_buffer,
						 size_t output_size,
						 size_t *actual_width_out,
						 size_t *actual_height_out,
						 size_t *actual_size_out,
						 int *actual_format_out)
{
	BITSTREAM input;
	SAMPLE_HEADER header;

	// Initialize the sample header and force search for the lowpass bands
	memset(&header, 0, sizeof(SAMPLE_HEADER));
	header.find_lowpass_bands = 1;
	
	// Bind a bitstreeam to the sample
	InitBitstreamBuffer(&input, (BITWORD *)sample_buffer, sample_size, BITSTREAM_ACCESS_READ);

	// Parse the sample header to get the location of the lowpass bands
	if (ParseSampleHeader(&input, &header))
	{
		uint32_t actual_width = (header.width+7)/8;
		uint32_t actual_height = (header.height+7)/8;
		size_t actual_size = actual_width * actual_height * 4;

		if (output_size < actual_size) {
			// Insufficient space for the lowpass image
			return false;
		}

		switch (header.encoded_format)
		{
			case ENCODED_FORMAT_UNKNOWN:
			case ENCODED_FORMAT_YUV_422:
				ConvertLowpassYU64(sample_buffer, sample_size, actual_width, actual_height,
					header.thumbnail_channel_offsets, output_buffer, output_size);
				break;

			case ENCODED_FORMAT_BAYER:
			case ENCODED_FORMAT_RGB_444:
			case ENCODED_FORMAT_RGBA_4444:
				//TODO: Convert lowpass images for other encoded formats

			default:
				assert(0);
				break;
		}

		if (actual_width_out) {
			*actual_width_out = actual_width;
		}
		if (actual_height_out) {
			*actual_height_out = actual_height;
		}
		if (actual_size_out) {
			*actual_size_out = (actual_width * actual_height * 4);
		}

		return true;
	}

	// Could not obtain the lowpass image
	return false;
}

bool GetStereoThumbnail(void *sample_buffer,
						size_t sample_size,
						void *output_buffer,
						size_t output_size,
						size_t *actual_width_out,
						size_t *actual_height_out,
						size_t *actual_size_out,
						int *actual_format_out,
						CHANNEL_OFFSETS *channel_offsets)
{
	BITSTREAM input;
	SAMPLE_HEADER header;

	// Initialize the sample header and force search for the lowpass bands
	memset(&header, 0, sizeof(SAMPLE_HEADER));
	header.find_lowpass_bands = 1;
	
	// Bind a bitstreeam to the sample
	InitBitstreamBuffer(&input, (BITWORD *)sample_buffer, sample_size, BITSTREAM_ACCESS_READ);

	// Parse the sample header to get the location of the lowpass bands
	if (ParseSampleHeader(&input, &header))
	{
		uint32_t actual_width = (header.width+7)/8;
		uint32_t actual_height = (header.height+7)/8;
		size_t frame_size = actual_width * actual_height * 4;
		size_t actual_size = 0;

		// The buffer is divided equally between the left and right images
		size_t left_output_size = output_size / 2;
		size_t right_output_size = output_size / 2;

		uint8_t *left_output_buffer = output_buffer;
		uint8_t *right_output_buffer = left_output_buffer + frame_size;

		// The total buffer size must be enough for both channels
		actual_size = frame_size * 2;

		if (output_size < actual_size) {
			// Insufficient space for both channels in lowpass image
			return false;
		}

		switch (header.encoded_format)
		{
			case ENCODED_FORMAT_UNKNOWN:
			case ENCODED_FORMAT_YUV_422:

				// Get the lowpass image in the left side of the stereo pair
				ConvertLowpassYU64(sample_buffer, sample_size, actual_width, actual_height,
					header.thumbnail_channel_offsets, left_output_buffer, left_output_size);

				// Get the lowpass image in the right side of the stereo pair
				ConvertLowpassYU64(sample_buffer, sample_size, actual_width, actual_height,
					header.thumbnail_channel_offsets_2nd_Eye, right_output_buffer, right_output_size);

				break;

			case ENCODED_FORMAT_BAYER:
			case ENCODED_FORMAT_RGB_444:
			case ENCODED_FORMAT_RGBA_4444:
				//TODO: Convert lowpass images for other encoded formats

			default:
				assert(0);
				break;
		}

		if (actual_width_out) {
			*actual_width_out = actual_width;
		}

		if (actual_height_out) {
			*actual_height_out = actual_height;
		}

		if (actual_size_out) {
			*actual_size_out = (actual_width * actual_height * 4);
		}

		if (channel_offsets)
		{
			// Return the locations of the chroma channel offset in each stereo channel
			int i;

			//TODO: Need to modify this code to handle other encoded formats
			channel_offsets->offset_count = 3;

			// Clear the channel offsets for the left and right lowpass images
			memset(channel_offsets->left_channel_offsets, 0, sizeof(channel_offsets->left_channel_offsets));
			memset(channel_offsets->right_channel_offsets, 0, sizeof(channel_offsets->right_channel_offsets));

			for (i = 0; i < channel_offsets->offset_count; i++)
			{
				channel_offsets->left_channel_offsets[i] = header.thumbnail_channel_offsets[i];
				channel_offsets->right_channel_offsets[i] = header.thumbnail_channel_offsets_2nd_Eye[i];
			}
		}

		return true;
	}

	// Could not obtain the lowpass image
	return false;
}

void ChangeLowpassColumnValues(void *sample_buffer,
							   size_t sample_size,
							   int width,
							   int height,
							   int *channel_offsets,
							   uint16_t *y_column_values,
							   uint16_t *u_column_values,
							   uint16_t *v_column_values)
{
	uint8_t *sample = (uint8_t *)sample_buffer;

	// Get pointers to the lowpass images in each channel
	uint32_t *y_ptr = (uint32_t *)(sample + channel_offsets[0]);
	uint32_t *u_ptr = (uint32_t *)(sample + channel_offsets[1]);
	uint32_t *v_ptr = (uint32_t *)(sample + channel_offsets[2]);

	int row, column;

	for (row = 0; row < height; row++)
	{
		// Process four luma and chroma pairs per iteration
		for (column = 0; column < width; column += 4)
		{
			uint32_t u1, u2;
			uint32_t v1, v2;
			uint32_t word;

			// Scale the 10-bit values to 16 bits
			const int shift = 6;

			if (y_column_values)
			{
				uint32_t y1, y2, y3, y4;

				// Reduce the new luma values to 10 bits
				y1 = y_column_values[column + 0] >> shift;
				y2 = y_column_values[column + 1] >> shift;
				y3 = y_column_values[column + 2] >> shift;
				y4 = y_column_values[column + 3] >> shift;

				// Pack the new chroma values in the word
				word = (y1 << 20) | (y2 << 4);
				*(y_ptr++) = _bswap(word);

				word = (y3 << 20) | (y4 << 4);
				*(y_ptr++) = _bswap(word);
			}

			if (u_column_values)
			{
				// Reduce the new chroma values to 10 bits
				u1 = u_column_values[column/2 + 0] >> shift;
				u2 = u_column_values[column/2 + 1] >> shift;

				// Pack the new chroma values in the word
				word = (u1 << 20) | (u2 << 4);
				*(u_ptr++) = _bswap(word);
			}

			if (v_column_values)
			{
				// Reduce the new chroma values to 10 bits
				v1 = v_column_values[column/2 + 0] >> shift;
				v2 = v_column_values[column/2 + 1] >> shift;

				// Pack the new chroma values in the word
				word = (v1 << 20) | (v2 << 4);
				*(v_ptr++) = _bswap(word);
			}
		}
	}
}

void ModifyLowpassColumnValues(void *sample_buffer,
							   size_t sample_size,
							   int width,
							   int height,
							   int *channel_offsets,
							   uint16_t *y_column_flags,
							   uint16_t *u_column_flags,
							   uint16_t *v_column_flags)
{
	uint8_t *sample = (uint8_t *)sample_buffer;

	// Get pointers to the lowpass images in each channel
	uint32_t *y_ptr = (uint32_t *)(sample + channel_offsets[0]);
	uint32_t *u_ptr = (uint32_t *)(sample + channel_offsets[1]);
	uint32_t *v_ptr = (uint32_t *)(sample + channel_offsets[2]);

	int row, column;

	for (row = 0; row < height; row++)
	{
		// Process four luma and chroma pairs per iteration
		for (column = 0; column < width; column += 4)
		{
			uint32_t word;

			// Scale the 10-bit values to 16 bits
			//const int shift = 6;

			if (y_column_flags)
			{
				uint32_t y1, y2, y3, y4;

				// Get the first pair of luma values
				word = _bswap(*y_ptr);
				//y1 = ((word >> 20) & 0x3FF);
				//y2 = ((word >>  4) & 0x3FF);
				y1 = (word >> 16) & 0xFFFF;
				y2 = word & 0xFFFF;

				// Expand the luma values to 16 bits
				//y1 <<= shift;
				//y2 <<= shift;

				// Manipulate the luma values according to the flags
				if ((y_column_flags[column + 0] & MODIFY_LOWPASS_ENABLE) == 0) {
					// Darken the luma value
					//y1 = 0;
					//y1 /= 2;
					y1 = (1 << 14);
				}

				if ((y_column_flags[column + 1] & MODIFY_LOWPASS_ENABLE) == 0) {
					// Darken the luma value
					//y2 = 0;
					//y2 /= 2;
					y2 = (1 << 14);
				}

				// Reduce the luma values to 10 bits
				//y1 >>= shift;
				//y2 >>= shift;

				// Write the first pair of modified luma values
				//word = (y1 << 20) | (y2 << 4);
				word = (y1 << 16) | y2;
				*(y_ptr++) = _bswap(word);

				// Get the second pair of luma values
				word = _bswap(*y_ptr);
				//y3 = ((word >> 20) & 0x3FF);
				//y4 = ((word >>  4) & 0x3FF);
				y3 = (word >> 16) & 0xFFFF;
				y4 = word & 0xFFFF;

				// Expand the luma values to 16 bits
				//y3 <<= shift;
				//y4 <<= shift;

				// Manipulate the luma values according to the flags
				if ((y_column_flags[column + 2] & MODIFY_LOWPASS_ENABLE) == 0) {
					// Darken the luma value
					//y3 = 0;
					//y3 /= 2;
					y3 = (1 << 14);
				}

				if ((y_column_flags[column + 3] & MODIFY_LOWPASS_ENABLE) == 0) {
					// Darken the luma value
					//y4 = 0;
					//y4 /= 2;
					y4 = (1 << 14);
				}

				// Reduce the luma values to 10 bits
				//y3 >>= shift;
				//y4 >>= shift;

				// Write the second pair of modified luma values
				//word = (y3 << 20) | (y4 << 4);
				word = (y3 << 16) | y4;
				*(y_ptr++) = _bswap(word);
			}

			if (u_column_flags)
			{
				uint32_t u1, u2;

				// Get the current chroma values
				word = _bswap(*u_ptr);
				//u1 = ((word >> 20) & 0x3FF);
				//u2 = ((word >>  4) & 0x3FF);
				u1 = (word >> 16) & 0xFFFF;
				u2 = word & 0xFFFF;

				// Expand the chroma values to 16 bits
				//u1 <<= shift;
				//u2 <<= shift;

				// Manipulate the chroma values according to the flags
				if ((u_column_flags[column/2 + 0] & MODIFY_LOWPASS_ENABLE) == 0) {
					u1 = (1 << 14);
				}
				
				if ((u_column_flags[column/2 + 1] & MODIFY_LOWPASS_ENABLE) == 0) {
					u2 = (1 << 14);
				}

				// Reduce the chroma values to 10 bits
				//u1 >>= shift;
				//u2 >>= shift;

				// Pack the new chroma values in the word
				//word = (u1 << 20) | (u2 << 4);
				word = (u1 << 16) | u2;
				*(u_ptr++) = _bswap(word);
			}

			if (v_column_flags)
			{
				uint32_t v1, v2;

				// Get the current chroma values
				word = _bswap(*v_ptr);
				//v1 = ((word >> 20) & 0x3FF);
				//v2 = ((word >>  4) & 0x3FF);
				v1 = (word >> 16) & 0xFFFF;
				v2 = word & 0xFFFF;

				// Expand the chroma values to 16 bits
				//v1 <<= shift;
				//v2 <<= shift;

				// Manipulate the chroma values according to the flags
				if ((v_column_flags[column/2 + 0] & MODIFY_LOWPASS_ENABLE) == 0) {
					v1 = (1 << 14);
				}
				
				if ((v_column_flags[column/2 + 1] & MODIFY_LOWPASS_ENABLE) == 0) {
					v2 = (1 << 14);
				}

				// Reduce the chroma values to 10 bits
				//v1 >>= shift;
				//v2 >>= shift;

				// Pack the new chroma values in the word
				//word = (v1 << 20) | (v2 << 4);
				word = (v1 << 16) | v2;
				*(v_ptr++) = _bswap(word);
			}
		}
	}
}
