/*!	@file bandfile.c


*  @brief Implementation of a module that writes wavelet bands to a file

	The band file is a custom file format for wavelet bands and can
	be used for debugging by comparing the bands computed by different
	versions of the codec.  The decoder does not free the wavelets
	after decoding a sample since they can be used for decoding the
	next sample, so after calling @ref DecodeSample selected wavelet
	bands can be written to a band file.

	A band file can contain bands for multiple frames (samples), one
	or more channels for each decoded sample, and any combination of
	wavelet bands in a channel.

	There is a header identified by a four character code for the start
	of the band file, frames, channels, wavelets, and bands.  A particular
	type of header is not repeated if the previous header is sufficient.
	For example, one wavelet header will be written in the band file for
	all of the bands in that wavelet.

	The file header contains that maximum dimensions and size of all of the
	bands written into the band file and can be used to allocate memory for
	storing bands that are read from the file.

	Typical usage is to use the routine @ref FindNextBand to find the next
	band in the file and then read the band data by calling the routine
	@ref ReadBandData.  The call to @ref FindNextBand updates the copy of
	the band parameters in the band file data structure using the values
	in the headers that it encounters while searching for the next chunk
	of band data.
	
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

#include "stdafx.h"
#include "error.h"
#include "decoder.h"
#include "bandfile.h"

#ifndef FOUR_CHAR_CODE
#define FOUR_CHAR_CODE(a,b,c,d)		(((d&0xff)<<0)|((c&0xff)<<8)|((b&0xff)<<16)|((a&0xff)<<24))
#endif

/*!
	@brief Four character codes for the band file headers

	@todo Byte swap the four character codes so that the letters
	appear in the correct order on little endian machines.
*/
#ifdef __APPLE__
typedef enum
{
	BAND_HEADER_FILE = 'file',
	BAND_HEADER_FRAME = 'fram',
	BAND_HEADER_CHANNEL = 'chan',
	BAND_HEADER_WAVELET = 'wave',
	BAND_HEADER_DATA = 'band',

} BAND_HEADER_TYPE;
#else
typedef enum
{
	BAND_HEADER_FILE =		FOUR_CHAR_CODE('f','i','l','e'),
	BAND_HEADER_FRAME =		FOUR_CHAR_CODE('f','r','a','m'),
	BAND_HEADER_CHANNEL =	FOUR_CHAR_CODE('c','h','a','n'),
	BAND_HEADER_WAVELET =	FOUR_CHAR_CODE('w','a','v','e'),
	BAND_HEADER_DATA =		FOUR_CHAR_CODE('b','a','n','d'),

} BAND_HEADER_TYPE;
#endif

struct header
{
	uint32_t type;
	uint32_t size;
};

struct bandfile_file_header
{
	struct header h;
	uint16_t max_band_width;		// Maximum width of the wavelet bands
	uint16_t max_band_height;		// Maximum height of the wavelet bands
	uint32_t max_band_size;			// Size of the largest wavelet band (in bytes)
};

struct bandfile_frame_header
{
	struct header h;
	uint32_t frame;
};

struct bandfile_channel_header
{
	struct header h;
	uint16_t channel;
	uint16_t reserved;
};

struct bandfile_wavelet_header
{
	struct header h;
	uint16_t wavelet;
	uint16_t reserved;
};

struct bandfile_band_header
{
	struct header h;
	uint16_t band;
	uint16_t type;
	uint16_t width;
	uint16_t height;
	uint32_t size;
};


/*!
	@brief Open the band file for reading band data
*/
CODEC_ERROR OpenBandFile(BANDFILE *bandfile, const char *pathname)
{
	memset(bandfile, 0, sizeof(BANDFILE));
#ifdef _WIN32
	fopen_s(&bandfile->file, pathname, "rb");
#else
	bandfile->file = fopen(pathname, "rb");
#endif
	return CODEC_ERROR_OKAY;
}

/*!
	@brief Find the next band in the band file

	This routine is the recommended method for reading band data from
	a band file.  Any combination of bands can be stored in any order.
	This routine updates the band file data structure with the index
	of the next frame, channel, wavelet, and band and the type of band.

	After calling this routine, the calling application should call the
	routine @ref ReadBandData to read the actual band data.

*/
CODEC_ERROR FindNextBand(BANDFILE *bandfile)
{
	struct {
		struct header prefix;		// All headers start with a common prefix
		uint8_t payload[64];		// Allocate space for the maximum payload
	} header;

	memset(&header, 0, sizeof(header));

	while (header.prefix.type != BAND_HEADER_DATA)
	{
		size_t size = 0;
		size_t result;

		// Read the common prefix for all headers
		result = fread(&header.prefix, sizeof(header.prefix), 1, bandfile->file);
		if (result != 1) {
			return CODEC_ERROR_BANDFILE_READ_FAILED;
		}

		// Read the rest of the header
		size = header.prefix.size - sizeof(header.prefix);
		assert(size <= sizeof(header.payload));
		if (size > 0) {
			result = fread(header.payload, size, 1, bandfile->file);
			if (result != 1) {
				return CODEC_ERROR_BANDFILE_READ_FAILED;
			}
		}

		switch (header.prefix.type)
		{
		case BAND_HEADER_FILE:
			bandfile->max_band_width = ((struct bandfile_file_header *)&header)->max_band_width;
			bandfile->max_band_height = ((struct bandfile_file_header *)&header)->max_band_height;
			bandfile->max_band_size = ((struct bandfile_file_header *)&header)->max_band_size;
			break;

		case BAND_HEADER_FRAME:
			bandfile->frame = ((struct bandfile_frame_header *)&header)->frame;
			break;

		case BAND_HEADER_CHANNEL:
			bandfile->channel = ((struct bandfile_channel_header *)&header)->channel;
			break;

		case BAND_HEADER_WAVELET:
			bandfile->wavelet = ((struct bandfile_wavelet_header *)&header)->wavelet;
			break;

		case BAND_HEADER_DATA:
			bandfile->band = ((struct bandfile_band_header *)&header)->band;
			bandfile->type = ((struct bandfile_band_header *)&header)->type;
			bandfile->width = ((struct bandfile_band_header *)&header)->width;
			bandfile->height = ((struct bandfile_band_header *)&header)->height;
			bandfile->size = ((struct bandfile_band_header *)&header)->size;
			break;

		default:
			// Unknown header
			assert(0);
			return CODEC_ERROR_UNEXPECTED;
			break;
		}
	}

	return CODEC_ERROR_OKAY;
}

CODEC_ERROR ReadFileHeader(BANDFILE *bandfile)
{
	struct bandfile_file_header header;
	size_t result;

	memset(&header, 0, sizeof(header));
	result = fread(&header, sizeof(header), 1, bandfile->file);
	if (result != 1) {
		return CODEC_ERROR_BANDFILE_READ_FAILED;
	}
	assert(header.h.type == BAND_HEADER_FILE);
	assert(header.h.size == sizeof(header));
	return CODEC_ERROR_OKAY;
}

CODEC_ERROR ReadFrameHeader(BANDFILE *bandfile)
{
	struct bandfile_frame_header header;
	size_t result;

	memset(&header, 0, sizeof(header));
	result = fread(&header, sizeof(header), 1, bandfile->file);
	if (result != 1) {
		return CODEC_ERROR_BANDFILE_READ_FAILED;
	}
	assert(header.h.type == BAND_HEADER_FRAME);
	assert(header.h.size == sizeof(header));
	bandfile->frame = header.frame;
	return CODEC_ERROR_OKAY;
}

CODEC_ERROR ReadChannelHeader(BANDFILE *bandfile)
{
	struct bandfile_channel_header header;
	size_t result;

	memset(&header, 0, sizeof(header));
	result = fread(&header, sizeof(header), 1, bandfile->file);
	if (result != 1) {
		return CODEC_ERROR_BANDFILE_READ_FAILED;
	}
	assert(header.h.type == BAND_HEADER_FILE);
	assert(header.h.size == sizeof(header));
	bandfile->channel = header.channel;
	return CODEC_ERROR_OKAY;
}

CODEC_ERROR ReadWaveletHeader(BANDFILE *bandfile)
{
	struct bandfile_wavelet_header header;
	size_t result;

	memset(&header, 0, sizeof(header));
	result = fread(&header, sizeof(header), 1, bandfile->file);
	if (result != 1) {
		return CODEC_ERROR_BANDFILE_READ_FAILED;
	}
	assert(header.h.type == BAND_HEADER_FILE);
	assert(header.h.size == sizeof(header));
	bandfile->wavelet = header.wavelet;
	return CODEC_ERROR_OKAY;
}

CODEC_ERROR ReadBandHeader(BANDFILE *bandfile)
{
	struct bandfile_band_header header;
	size_t result;

	memset(&header, 0, sizeof(header));
	result = fread(&header, sizeof(header), 1, bandfile->file);
	if (result != 1) {
		return CODEC_ERROR_BANDFILE_READ_FAILED;
	}
	assert(header.h.type == BAND_HEADER_FILE);
	assert(header.h.size == sizeof(header));
	bandfile->band = header.band;
	bandfile->type = header.type;
	bandfile->width = header.width;
	bandfile->height = header.height;
	bandfile->size = header.size;
	return CODEC_ERROR_OKAY;
}

/*!
	@brief Read the data for the next band from the band file
*/
CODEC_ERROR ReadBandData(BANDFILE *bandfile, void *data, size_t size)
{
	size_t result = fread(data, size, 1, bandfile->file);
	if (result != 1) {
		return CODEC_ERROR_BANDFILE_READ_FAILED;
	}
	return CODEC_ERROR_OKAY;
}

/*!
	@brief Create a band file for storing band data
*/
CODEC_ERROR CreateBandFile(BANDFILE *bandfile, const char *pathname)
{
	memset(bandfile, 0, sizeof(BANDFILE));
#ifdef _WIN32
	fopen_s(&bandfile->file, pathname, "rb");
#else
	bandfile->file = fopen(pathname, "wb");
#endif
	if (bandfile->file == NULL) {
		return CODEC_ERROR_BANDFILE_CREATE_FAILED;
	}
	return CODEC_ERROR_OKAY;
}

/*!
	@brief Write the band data to the band file

	This is the recommended method for writing band data to a file.
	Any headers that must be written to the file will be written
	before the band data.  For example, if the frame, channel, and
	wavelet numbers have not changed since the last call to this routine
	then the frame, channel, and wavelet headers will be be rewritten.
*/
CODEC_ERROR WriteWaveletBand(BANDFILE *bandfile,
					 int frame,
					 int channel,
					 int wavelet,
					 int band,
					 int type,
					 int width,
					 int height,
					 void *data,
					 size_t size)
{
	assert(bandfile->file_header_flag);

	if (!bandfile->frame_header_flag || bandfile->frame != frame) {
		WriteFrameHeader(bandfile, frame);
	}

	if (!bandfile->channel_header_flag || bandfile->channel != channel) {
		WriteChannelHeader(bandfile, channel);
	}

	if (!bandfile->wavelet_header_flag || bandfile->wavelet != wavelet) {
		WriteWaveletHeader(bandfile, wavelet);
	}

	if (!bandfile->band_header_flag || bandfile->band != band || bandfile->type != type) {
		WriteBandHeader(bandfile, band, type, width, height, size);
	}

	WriteBandData(bandfile, data, size);

	return CODEC_ERROR_OKAY;
}

CODEC_ERROR WriteFileHeader(BANDFILE *bandfile, int max_band_width, int max_band_height)
{
	struct bandfile_file_header header;
	size_t result;

	memset(&header, 0, sizeof(header));
	header.h.type = BAND_HEADER_FILE;
	header.h.size = sizeof(header);
	header.max_band_width = (uint16_t)max_band_width;
	header.max_band_height = (uint16_t)max_band_height;
	header.max_band_size = (max_band_width * max_band_height) * 2;
	result = fwrite(&header, sizeof(header), 1, bandfile->file);
	if (result != 1) {
		return CODEC_ERROR_BANDFILE_WRITE_FAILED;
	}
	bandfile->file_header_flag = true;
	bandfile->frame_header_flag = false;
	return CODEC_ERROR_OKAY;
}

CODEC_ERROR WriteFrameHeader(BANDFILE *bandfile, int frame)
{
	struct bandfile_frame_header header;
	size_t result;

	memset(&header, 0, sizeof(header));
	header.h.type = BAND_HEADER_FRAME;
	header.h.size = sizeof(header);\
	header.frame = frame;
	result = fwrite(&header, sizeof(header), 1, bandfile->file);
	if (result != 1) {
		return CODEC_ERROR_BANDFILE_WRITE_FAILED;
	}
	bandfile->frame = frame;
	bandfile->frame_header_flag = true;
	bandfile->channel_header_flag = false;
	return CODEC_ERROR_OKAY;
}

CODEC_ERROR WriteChannelHeader(BANDFILE *bandfile, int channel)
{
	struct bandfile_channel_header header;
	size_t result;

	memset(&header, 0, sizeof(header));
	header.h.type = BAND_HEADER_CHANNEL;
	header.h.size = sizeof(header);
	header.channel = (uint16_t)channel;
	result = fwrite(&header, sizeof(header), 1, bandfile->file);
	if (result != 1) {
		return CODEC_ERROR_BANDFILE_WRITE_FAILED;
	}
	bandfile->channel = channel;
	bandfile->channel_header_flag = true;
	bandfile->wavelet_header_flag = false;
	return CODEC_ERROR_OKAY;
}

CODEC_ERROR WriteWaveletHeader(BANDFILE *bandfile, int wavelet)
{
	struct bandfile_wavelet_header header;
	size_t result;

	memset(&header, 0, sizeof(header));
	header.h.type = BAND_HEADER_WAVELET;
	header.h.size = sizeof(header);
	header.wavelet = (uint16_t)wavelet;
	result = fwrite(&header, sizeof(header), 1, bandfile->file);
	if (result != 1) {
		return CODEC_ERROR_BANDFILE_WRITE_FAILED;
	}
	bandfile->wavelet = wavelet;
	bandfile->wavelet_header_flag = true;
	bandfile->band_header_flag = false;
	return CODEC_ERROR_OKAY;
}

CODEC_ERROR WriteBandHeader(BANDFILE *bandfile, int band, int type, int width, int height, size_t size)
{
	struct bandfile_band_header header;
	size_t result;

	memset(&header, 0, sizeof(header));
	header.h.type = BAND_HEADER_DATA;
	header.h.size = sizeof(header);
	header.band = (uint16_t)band;
	header.type = (uint16_t)type;
	header.width = (uint16_t)width;
	header.height = (uint16_t)height;
	header.size = (uint32_t)size;
	result = fwrite(&header, sizeof(header), 1, bandfile->file);
	if (result != 1) {
		return CODEC_ERROR_BANDFILE_WRITE_FAILED;
	}
	bandfile->band = band;
	bandfile->type = type;
	bandfile->width = width;
	bandfile->height = height;
	bandfile->size = (uint32_t)size;
	bandfile->band_header_flag = true;
	return CODEC_ERROR_OKAY;
}

CODEC_ERROR WriteBandData(BANDFILE *bandfile, void *data, size_t size)
{
	size_t result = fwrite(data, size, 1, bandfile->file);
	if (result != 1) {
		return CODEC_ERROR_BANDFILE_WRITE_FAILED;
	}
	return CODEC_ERROR_OKAY;
}

CODEC_ERROR CloseBandFile(BANDFILE *bandfile)
{
	if (bandfile->file) {
		fclose(bandfile->file);
		bandfile->file = NULL;
	}
	return CODEC_ERROR_OKAY;
}


/*!
	@brief Write selected subbands into a wavelet band file

	Each bit in the subband mask argument specifies whether the corresponding
	subband should be written to the wavelet band file.

	Note that the band file can contain reconstructed lowpass bands, but this
	routine only write decoded subbands to the wavelet band file.
*/
CODEC_ERROR WriteDecodedBandFile(DECODER *decoder,
								 int channel_index,
								 uint32_t subband_mask,
								 const char *pathname)
{
	BANDFILE file;
	int max_band_width;
	int max_band_height;
	int subband;

	//TODO: Modify this routine to take the frame index as an argument
	const int frame_index = 0;

	// Compute the maximum dimensions of each subband
	max_band_width = decoder->frame.width;
	max_band_height = decoder->frame.height;

	// Create the band file
	CreateBandFile(&file, pathname);

	// Write the band file header
	WriteFileHeader(&file, max_band_width, max_band_height);

	for (subband = 0; subband_mask != 0; subband_mask >>= 1, subband++)
	{
		// Write this subband?
		if ((subband_mask & 0x01) != 0)
		{
			//int wavelet_index = SubbandWaveletIndex(subband);
			int wavelet_index = decoder->subband_wavelet_index[subband];
			int band_index = decoder->subband_band_index[subband];
			IMAGE *wavelet = decoder->transform[channel_index]->wavelet[wavelet_index];
			int width = wavelet->width;
			int height = wavelet->height;
			void *data = wavelet->band[band_index];
			size_t size = wavelet->width * wavelet->height * sizeof(PIXEL);

			WriteWaveletBand(&file, frame_index, channel_index, wavelet_index,
							 band_index, BAND_TYPE_SINT16, width, height, data, size);
		}
	}

	CloseBandFile(&file);

	return CODEC_ERROR_OKAY;
}

CODEC_ERROR WriteDecodedWaveletBandFile(DECODER *decoder,
										int channel_index,
										int wavelet_index,
										uint32_t band_mask,
										const char *pathname)
{
	BANDFILE file;
	IMAGE *wavelet;
	int max_band_width;
	int max_band_height;
	int band_index;
	int width;
	int height;

	//TODO: Modify this routine to take the frame index as an argument
	const int frame_index = 0;

	if (decoder == NULL) {
		return CODEC_ERROR_NULLPTR;
	}

	if (! (0 <= channel_index && channel_index < decoder->codec.num_channels)) {
		return CODEC_ERROR_BAD_ARGUMENT;
	}

	if (! (0 <= wavelet_index && wavelet_index < decoder->transform[channel_index]->num_wavelets)) {
		return CODEC_ERROR_BAD_ARGUMENT;
	}

	// Get the specified wavelet from the transform
	wavelet = decoder->transform[channel_index]->wavelet[wavelet_index];
	assert(wavelet != NULL);

	// Compute the maximum dimensions of each subband
	max_band_width = decoder->frame.width;
	max_band_height = decoder->frame.height;

	// Get the actual dimensions of the bands in this wavelet
	width = wavelet->width;
	height = wavelet->height;

	// Create the band file
	CreateBandFile(&file, pathname);

	// Write the band file header
	WriteFileHeader(&file, max_band_width, max_band_height);

	for (band_index = 0; band_mask != 0; band_mask >>= 1, band_index++)
	{
		// Write this subband?
		if ((band_mask & 0x01) != 0)
		{
			void *data = wavelet->band[band_index];
			size_t size = wavelet->width * wavelet->height * sizeof(PIXEL);

			WriteWaveletBand(&file, frame_index, channel_index, wavelet_index,
							 band_index, BAND_TYPE_SINT16, width, height, data, size);
		}
	}

	CloseBandFile(&file);

	return CODEC_ERROR_OKAY;
}

CODEC_ERROR WriteDecodedTransformBandFile(DECODER *decoder,
										  int channel_index,
										  uint32_t wavelet_mask,
										  uint32_t wavelet_band_mask,
										  const char *pathname)
{
	BANDFILE file;
	int max_band_width;
	int max_band_height;
	int wavelet_count;
	int wavelet_index;

	//TODO: Modify this routine to take the frame index as an argument
	const int frame_index = 0;

	wavelet_count = decoder->transform[channel_index]->num_wavelets;

	// Compute the maximum dimensions of each subband
	max_band_width = decoder->frame.width;
	max_band_height = decoder->frame.height;

	// Create the band file
	CreateBandFile(&file, pathname);

	// Write the band file header
	WriteFileHeader(&file, max_band_width, max_band_height);

	for (wavelet_index = 0;
		 wavelet_index < wavelet_count && wavelet_mask != 0;
		 wavelet_mask >>= 1, wavelet_index++)
	{
		// Write bands in this wavelet?
		if ((wavelet_mask & 0x01) != 0)
		{
			IMAGE *wavelet = decoder->transform[channel_index]->wavelet[wavelet_index];
			uint32_t band_mask = wavelet_band_mask;
			int band_count = wavelet->num_bands;
			int band_index;

			// Get the actual wavelet band dimensions
			int width = wavelet->width;
			int height = wavelet->height;

			for (band_index = 0;
				 band_index < band_count && band_mask != 0;
				 band_mask >>= 1, band_index++)
			{
				// Write this band in the wavelet?
				if ((band_mask & 0x01) != 0)
				{
					void *data = wavelet->band[band_index];
					size_t size = wavelet->width * wavelet->height * sizeof(PIXEL);

					WriteWaveletBand(&file, frame_index, channel_index, wavelet_index,
									 band_index, BAND_TYPE_SINT16, width, height, data, size);
				}
			}
		}
	}

	CloseBandFile(&file);

	return CODEC_ERROR_OKAY;
}


/*!
	@brief Write selected wavelet bands to a band file
*/
CODEC_ERROR WriteDecodedTransformBands(DECODER *decoder,
									   uint32_t channel_mask,
									   uint32_t channel_wavelet_mask,
									   uint32_t wavelet_band_mask,
									   const char *pathname)
{
	BANDFILE file;
	int max_band_width;
	int max_band_height;
	int channel_count;
	int channel_index;

	//TODO: Modify this routine to take the frame index as an argument
	const int frame_index = 0;

	// Get the number of channels in the decoder wavelet transform
	channel_count = decoder->codec.num_channels;

	// Compute the maximum dimensions of each subband
	max_band_width = decoder->codec.frame_width;
	max_band_height = decoder->codec.frame_height;

	// Create the band file
	CreateBandFile(&file, pathname);

	// Write the band file header
	WriteFileHeader(&file, max_band_width, max_band_height);

	for (channel_index = 0;
		 channel_index < channel_count && channel_mask != 0;
		 channel_mask >>= 1, channel_index++)
	{
		uint32_t wavelet_mask = channel_wavelet_mask;
		int wavelet_count = decoder->codec.num_wavelets;
		int wavelet_index;

		for (wavelet_index = 0;
			 wavelet_index < wavelet_count && wavelet_mask != 0;
			 wavelet_mask >>= 1, wavelet_index++)
		{
			// Write bands in this wavelet?
			if ((wavelet_mask & 0x01) != 0)
			{
				IMAGE *wavelet = decoder->transform[channel_index]->wavelet[wavelet_index];
				int width = wavelet->width;
				int height = wavelet->height;
				uint32_t band_mask = wavelet_band_mask;
				int band_count = wavelet->num_bands;
				int band_index;

				for (band_index = 0;
					 band_index < band_count && band_mask != 0;
					 band_mask >>= 1, band_index++)
				{
					// Write this band in the wavelet?
					if ((band_mask & 0x01) != 0)
					{
						void *data = wavelet->band[band_index];
						size_t size = wavelet->width * wavelet->height * sizeof(PIXEL);

						WriteWaveletBand(&file, frame_index, channel_index, wavelet_index,
										 band_index, BAND_TYPE_SINT16, width, height, data, size);
					}
				}
			}
		}
	}

	CloseBandFile(&file);

	return CODEC_ERROR_OKAY;
}
