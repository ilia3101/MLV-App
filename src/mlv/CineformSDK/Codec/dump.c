/*! @file dump.c

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

#include <stdlib.h>

#ifdef _WIN32
#include <io.h>			// for _access()
#include <direct.h>		// for _mkdir()
#endif

#include <assert.h>
#include <limits.h>

#include "config.h"
#include "dump.h"
#include "codec.h"

#if _DUMP

#ifdef BITMASK
#undef BITMASK
#endif

#define BITMASK(n)	(1 << (n))
#define isalpha2(a)	(((a)>='a' && (a)<='z') || ((a)>='A' && (a)<='Z'))

BOOL MakeDirectoryPath(char *input_path, char *output_path)
{
	char *p = output_path;
	char *q = input_path;
	char *s;
	int count;

	//char msg[1024];

	*p = '\0';
	if (q == NULL || q[0] == '\0') return FALSE;

	// The input string has at least one character
	//printf("Input: %s\n", input_path);

	// Check for the drive letter
	if (q[1] != ':') return FALSE;

	// Copy the drive letter and colon
	*(p++) = *(q++);
	*(p++) = *(q++);

	// Check for the start of the directory path
	if (q[0] != '/' && q[0] != '\\') return FALSE;

	// Copy the start of the directory path
	*(p++) = '/'; q++;

	// Check for the start of a folder name
	if (!isalpha2(q[0])) return FALSE;

	// Copy the list of folders
	for (;;)
	{
		// Copy the folder name
		while (q[0] != '\0' && q[0] != '/' && q[0] != '\\') {
			*(p++) = *(q++);
		}

		// Terminate the output string
		*p = '\0';

		//printf(" Path: %s\n", output_path);
		//sprintf(msg, "Path: '%s'\n", output_path);
		//OutputDebugString(msg);

		// Does the directory path exist?
		if (_access(output_path, 0))
		{
			// The directory path does not exist
			//OutputDebugString("Making folder\n");

			// Try to make the folder at the end of the path
			if (_mkdir(output_path) != 0)
			{
				// Could not make the folder
				//OutputDebugString("Could not make folder\n");
				return FALSE;
			}
		}

		// Check for the end of the directory path
		if (q[0] == '\0') break;

		// Check for another folder name
		if ((q[0] == '/' || q[0] == '\\') && !isalpha2(q[1])) break;

		// Copy the separator
		*(p++) = '/'; q++;
	}

	// Check that the string was terminated properly
	assert(*p == '\0');

	//sprintf(msg, "Returning: '%s'\n", output_path);
	//OutputDebugString(msg);

	return TRUE;
}

BOOL SetDumpDirectory(CODEC *codec, char *directory)
{
	char path[_MAX_PATH];

	//char msg[1024];

	if (! MakeDirectoryPath(directory, path))
	{
		// Could not create the folders along the directory path
		//OutputDebugString("Could not make directory path\n");
		return FALSE;
	}

	// Copy the directory path into the codec
	strncpy(codec->dump.directory, path, sizeof(codec->dump.directory));
	codec->dump.directory[sizeof(codec->dump.directory) - 1] = '\0';

	//sprintf(msg, "Dump directory: '%s'\n", codec->dump.directory);
	//OutputDebugString(msg);

	return TRUE;
}

BOOL SetDumpFilename(CODEC *codec, char *filename)
{
	// Copy the filename template into the codec
	strncpy(codec->dump.filename, filename, sizeof(codec->dump.filename));
	codec->dump.filename[sizeof(codec->dump.filename) - 1] = '\0';

	return TRUE;
}

BOOL SetDumpChannelMask(CODEC *codec, DWORD mask)
{
	codec->dump.channel_mask = mask;
	return TRUE;
}

BOOL SetDumpWaveletMask(CODEC *codec, DWORD mask)
{
	codec->dump.wavelet_mask = mask;
	return TRUE;
}

BOOL TestDumpWaveletMask(CODEC *codec, int wavelet, int band)
{
	DWORD wavelet_mask = (1 << band) << wavelet;
	return ((codec->dump.wavelet_mask & wavelet_mask) != 0);
}

BOOL DumpTransformBands(CODEC *codec, TRANSFORM *transform, int channel, BOOL requantize)
{
	char filename[_MAX_PATH];
	char pathname[_MAX_PATH];

	int num_wavelets = transform->num_wavelets;
	int i;

	// Should this channel be dumped?
	int bitmask = BITMASK(channel);
	if ((codec->dump.channel_mask & BITMASK(channel)) == 0) {
		return FALSE;
	}

	for (i = 0; i < num_wavelets; i++)
	{
		IMAGE *wavelet = transform->wavelet[i];
		int num_bands = wavelet->num_bands;
		int band;

		if((codec->dump.wavelet_mask>>4) & 1<<i)
		{
			for (band = 0; band < num_bands; band++)
			{
				FILE *file;
				int frame_number;
				BOOL result;

				//char msg[1024];

				// Should this wavelet band be output?
				//if (!TestDumpWaveletMask(codec, i, band)) continue;
				
				if(!(codec->dump.wavelet_mask & 1<<band)) continue;

				// Format the output filename (all parameters start at zero)
				frame_number = codec->frame_count;
		//		if(codec->dump.decode == FALSE) 
			//		frame_number--;

				sprintf(filename, codec->dump.filename, frame_number, channel, i, band);
				sprintf(pathname, "%s/%s", codec->dump.directory, filename);

				//sprintf(msg, "Pathname: '%s'\n", pathname);
				//OutputDebugString(msg);

				// Open the output file
				file = fopen(pathname, "wb");
				if (file == NULL) return FALSE;

				// Write out a PGM file for this wavelet band
				result = DumpWaveletBand(wavelet, band, requantize, file);

				// Close the output file
				fclose(file);

				// Stop dumping bands if the last attempt failed
				if (!result) return FALSE;
			}
		}
	}

	return TRUE;
}

BOOL DumpWaveletBand(IMAGE *wavelet, int band, BOOL requantize, FILE *file)
{
	PIXEL *band_row = wavelet->band[band];
	int band_width = wavelet->width;
	int band_height = wavelet->height;
	int band_pitch = wavelet->pitch/sizeof(PIXEL);
	int first_row, first_column;
	int last_row, last_column;
	int bottom_row, left_column;
	int row;
	int column;

	// Parameters for adjusting the output pixels
	int minimum = PIXEL_MAX;
	int maximum = PIXEL_MIN;
	int total;
	int count;
	int limit;
	int offset;
	int amplitude;
	int lower;
	int upper;
	int scale = 1;

	// Actual output dimensions
	int output_width;
	int output_height;

	// Quantization applied to the wavelet band
	int divisor;

	// Buffer for the scaled output pixels
	size_t pixel_size;
	size_t row_size;
	BYTE *row_buffer = NULL;

	PIXEL *rowptr;

	// Calculate the true last row and column
	left_column = band_width - 1;
	bottom_row = band_height - 1;

	// Get the quantization that was applied to this band during encoding
	divisor = wavelet->quantization[band];

	//if (subimage == NULL)
	if (1)
	{
		first_row = 0;
		first_column = 0;
		last_row = band_height - 1;
		last_column = band_width - 1;
	}
#if 0
	else
	{
		if (subimage->row < 0) first_row = band_height + subimage->row;
		else first_row = subimage->row;

		if (subimage->column < 0) first_column = band_width + subimage->column;
		else first_column = subimage->column;

		if (subimage->height == 0) last_row = band_height - 1;
		else last_row = first_row + subimage->height - 1;

		if (subimage->width == 0) last_column = band_width - 1;
		else last_column = first_column + subimage->width - 1;

		band_row += (first_row * band_pitch);
	}
#endif

	// Check the boundaries of the output image
	assert(0 <= first_row);
	assert(0 <= first_column);
	if (last_row > bottom_row) last_row = bottom_row;
	if (last_column > left_column) last_column = left_column;

	// Calculate the dimensions of the output image
	output_width = last_column - first_column + 1;
	output_height = last_row - first_row + 1;

	// Calculate the minimum and maximum pixel values
	rowptr = band_row;
	total = 0;
	count = 0;
	for (row = first_row; row <= last_row; row++) 
	{
		for (column = first_column; column <= last_column; column++) 
		{
			int value = rowptr[column];

			if (requantize) {
				value /= divisor;
			}

			if (value > maximum) maximum = value;
			if (value < minimum) minimum = value;

			total += value;
			count++;
		}
		rowptr += band_pitch;
	}

	if (minimum < 0)
	{
		// Compute the maximum excursion from zero
		amplitude = abs(minimum);
		if (amplitude < maximum) amplitude = maximum;

		offset = (amplitude <= 127) ? 128 : (USHRT_MAX + 1)/2;
		limit = maximum + offset;

		// Is the range too small for easy viewing?
		if (amplitude < 127)
		{
			// Scale the image for better display
			scale = (amplitude > 0) ? (127 / amplitude) : 1;
			if (scale < 1) scale = 1;
			limit = scale * maximum + offset;
		}
	}
	else
	{
		offset = 0;
		limit = maximum;
		amplitude = maximum - minimum;

		// Is the range too small for easy viewing?
		if (maximum < 255)
		{
			// Scale the image for better display
			scale = (maximum > 0) ? (255 / maximum) : 1;
			if (scale < 1) scale = 1;
		}	limit = scale * maximum;
	}

	// Check the upper and lower limits
	lower = scale * minimum + offset;
	upper = scale * maximum + offset;
	assert(0 <= lower && lower <= upper <= USHRT_MAX);

	// Handle the case where the band is uniform
//	if (minimum == 0 && maximum == 0) {
		limit = 255;
//		limit = 65535;
//	}

//	if(limit < 256) limit = 255; else limit = 65535;


	// Check for a valid maximum output pixel value
	assert(0 < limit && limit <= USHRT_MAX);

	// Write out the PGM header for a binary file
	fprintf(file, "P5\n# CREATOR: DAN min=%d max=%d quant=%d scale=%d\n%d %d\n%d\n",
			minimum, maximum, divisor, scale, output_width, output_height, limit);

	// Allocate space for the row buffer
	pixel_size = (limit <= 255) ? 1 : 2;
	row_size = output_width * pixel_size;
	row_buffer = (BYTE *)malloc(row_size);
	if (row_buffer == NULL) {
		return FALSE;
	}

	if(amplitude == 0)
		amplitude = 1;

	// Output the band values
	rowptr = band_row;
	for (row = first_row; row <= last_row; row++)
	{
		BYTE *outptr = row_buffer;

		for (column = first_column; column <= last_column; column++)
		{
			int output = rowptr[column];

			if(limit == 255)
			{

				if (requantize) {
					output /= divisor;
				}
				output = abs(output);

				if(minimum > 0)
				{
					output = (output - minimum)*255/amplitude;
				}
				else
				{
					output = output*255/amplitude;
				}
			}
			else
			{
				output = abs(output);
			}
/*
			if (output >= 0)
			{
				output = scale * output + offset;
			}
			else
			{
				output = offset - scale * abs(output);
			}

			assert(0 <= output && output <= limit);
*/
			if (limit <= 255)
			{
				// Output a single byte
				*(outptr++) = (BYTE)output;
			}
			else
			{
				// Output two bytes (most significant byte first)
				int byte0 = (BYTE)output;
				int byte1 = (BYTE)(output >> 8);
				*(outptr++) = byte1;
				*(outptr++) = byte0;
			}
		}

		// Write out one row of pixels
		fwrite(row_buffer, row_size, 1, file);

		// Advance to the next row in the wavelet band
		rowptr += band_pitch;
	}

	// Free the row buffer
	free(row_buffer);

	// The band was written to the file successfully
	return TRUE;
}

#endif
