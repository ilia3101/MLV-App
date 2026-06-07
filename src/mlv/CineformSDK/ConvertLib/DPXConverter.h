/*! @file DPXConverter.h

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

// Define the maximum unsigned integer (should use stdint.h)
#ifndef UINT16_MAX
#define UINT16_MAX 0xFFFF
#endif

class DPX_PixelFormat
{
private:

	// Shift and mask constants for the DPX 10-bit RGB pixel format
	static const int rgb10_red_shift = 22;
	static const int rgb10_green_shift = 12;
	static const int rgb10_blue_shift = 2;
	static const uint32_t rgb10_mask = 0x3FF;

public:

	DPX_PixelFormat(bool byte_swap_flag = true) :
		byte_swap_flag(byte_swap_flag)
	{
	}

	void SetByteSwapFlag(bool byte_swap_flag)
	{
		this->byte_swap_flag = byte_swap_flag;
	}

	// Byte swap the DPX 10-bit RGB pixels (if necessary)
	void SwapRGB10(void *input_buffer, size_t input_pitch,
				   void *output_buffer, size_t output_pitch,
				   int width, int height);

	// Byte swap the DPX 10-bit RGB pixels (in-place computation)
	void SwapRGB10(void *buffer, size_t pitch, int width, int height);

protected:

	uint32_t Swap32(uint32_t x)
	{
#if _WIN32
		// Use the byte swap routine from the standard library 
		return (byte_swap_flag ? _byteswap_ulong(x) : (x));
#elif defined(__APPLE__)
		return (byte_swap_flag ? _OSSwapInt32(x) : (x));
#elif defined(linux)
		return (byte_swap_flag ? __builtin_bswap32(x) : (x));
#else
		// Use the Linux byte swap routine
		return (byte_swap_flag ? bswap_32(x) : (x));
#endif
	}

	// Unpack the 10-bit color components in a DPX pixel
	void Unpack10(uint32_t word, uint16_t *red, uint16_t *green, uint16_t *blue)
	{
		// Scale each color value to 16 bits
		const int scale_shift = 6;

		// Swap the input pixel (if necessary)
		word = Swap32(word);

		// Shift and mask the DPX pixel to extract the components
		*red = ((word >> rgb10_red_shift) & rgb10_mask) << scale_shift;
		*green = ((word >> rgb10_green_shift) & rgb10_mask) << scale_shift;
		*blue = ((word >> rgb10_blue_shift) & rgb10_mask) << scale_shift;
	}

	// Pack 10-bit color components into a DPX pixel
	uint32_t Pack10(uint16_t red, uint16_t green, uint16_t blue)
	{
		// Reduce each 16 bit color value to 10 bits
		const int descale_shift = 6;

		red >>= descale_shift;
		green >>= descale_shift;
		blue >>= descale_shift;

		// Pack the color values into a 32 bit word
		uint32_t word = ((red & rgb10_mask) << rgb10_red_shift) |
						((green & rgb10_mask) << rgb10_green_shift) |
						((blue & rgb10_mask) << rgb10_blue_shift);

		// Return the packed word after byte swapping (if necessary)
		return Swap32(word);
	}

private:

	bool byte_swap_flag;
};

class V210_PixelFormat
{
public:

	void Unpack(uint32_t word, uint16_t *c1_out, uint16_t *c2_out, uint16_t *c3_out)
	{
		const int c1_shift = 0;
		const int c2_shift = 10;
		const int c3_shift = 20;
		const uint16_t mask = 0x3FF;

		uint16_t c1 = (word >> c1_shift) & mask;
		uint16_t c2 = (word >> c2_shift) & mask;
		uint16_t c3 = (word >> c3_shift) & mask;

		if (c1_out) {
			*c1_out = (c1 << 6);
		}

		if (c2_out) {
			*c2_out = (c2 << 6);
		}

		if (c3_out) {
			*c3_out = (c3 << 6);
		}
	}
};

//class DPXConverter : public CImageConverter
class DPXConverter : public DPX_PixelFormat, public V210_PixelFormat
{
private:

	// Luma and chroma offsets for 10-bit pixels
	//static const int luma_offset = (16 << 2);
	//static const int chroma_offset = (128 << 2);

public:

	DPXConverter(bool byte_swap_flag = true) :
		DPX_PixelFormat(byte_swap_flag)
	{
	}

	void ConvertRGB10ToCbYCrY_10bit_2_8(void *input_buffer, size_t input_pitch,
										void *output_buffer, size_t output_pitch,
										int width, int height);

	void ConvertRGB10ToARGB_10bit_2_8(void *input_buffer, size_t input_pitch,
									  void *output_buffer, size_t output_pitch,
									  int width, int height);

	void ConvertB64AToDPX0(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);

	void ConvertRGB32ToDPX0(void *input_buffer, size_t input_pitch,
							void *output_buffer, size_t output_pitch,
							int width, int height);

	void ConvertWP13ToDPX0(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);

	void ConvertBYR4ToDPX0(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);

	void ConvertBYR3ToDPX0(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);

	void ConvertYU64ToDPX0(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);

	void ConvertYU64ToDPX1(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);

	void ConvertYUYVToDPX0(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);

	void ConvertYUYVToDPX1(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);

	void ConvertUYVYToDPX1(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);

	void ConvertV210ToDPX1(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);

	void ConvertR408ToDPX1(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);

	void ConvertV408ToDPX1(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);

	void ConvertCbYCrY_10bit_2_8ToDPX1(void *input_buffer, size_t input_pitch,
									   void *output_buffer, size_t output_pitch,
									   int width, int height);

	void ConvertCbYCrY_16bit_2_14ToDPX1(void *input_buffer, size_t input_pitch,
										void *output_buffer, size_t output_pitch,
										int width, int height);
#if 0
	void ConvertR408ToDPX2(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);
#endif
#if 0
	void ConvertR408ToDPX3(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);
#endif

	void ConvertNV12ToDPX0(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);
#if 0
	//TODO: Implement this routine if the DPX viewer supports DPX1
	void ConvertNV12ToDPX1(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);
#endif

	void ConvertYV12ToDPX0(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);
#if 0
	//TODO: Implement this routine if the DPX viewer supports DPX1
	void ConvertYV12ToDPX1(void *input_buffer, size_t input_pitch,
						   void *output_buffer, size_t output_pitch,
						   int width, int height);
#endif

	static uint16_t Saturate16u(int32_t x)
	{
		return ((x < 0) ? 0 : ((x > UINT16_MAX) ? UINT16_MAX : x));
	}
};
