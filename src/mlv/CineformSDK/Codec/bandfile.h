/*! @file bandilfe.h

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

#ifndef _BANDFILE_H
#define _BANDFILE_H

/*!	@file bandfile.h

	Declaration of the data structures and functions for reading
	and writing binary files that contain band data (for debugging).

	The band file can contain band data from multiple frames and any
	combination of bands can be written for each frame.
*/

/*!
	@brief Declaration of the band file data structure

	@todo Replace the Boolean flags that are currently defined to
	be one byte with a 32-bit word using one bit per flag.
*/
#pragma pack(push, 1)

typedef struct _bandfile
{
	FILE *file;				//!< Current open band file
	uint32_t frame;			//!< Most recent frame number
	uint16_t channel;		//!< Most recent channel index
	uint16_t wavelet;		//!< Most recent wavelet index
	uint16_t band;			//!< Most recent band index
	uint16_t type;			//!< Data type of the most recent band
	uint32_t size;			//!< Size of the most recent band (in bytes)

	// Dimensions of the most recent band
	uint16_t width;
	uint16_t height;

	// Largest dimensions of the bands in the band file
	uint16_t max_band_width;
	uint16_t max_band_height;

	// Maximum size of the bands in the band file
	uint32_t max_band_size;

	// Flags that indicate the state of the band data file
	uint8_t file_header_flag;		//!< Has the file header been written?
	uint8_t frame_header_flag;		//!< Has the frame header been written?
	uint8_t channel_header_flag;	//!< Has the channel header been written?
	uint8_t wavelet_header_flag;	//!< Has the wavelet header been written?
	uint8_t band_header_flag;		//!< Has the band header been written?

	uint8_t padding[3];				// Pad the structure to a multiple of four bytes

} BANDFILE;

#pragma pack(pop)

//#define BANDFILE_INITIALIZER {NULL}

/*!
	@brief Data type of the data for a band in the band file
*/
typedef enum
{
	BAND_TYPE_UINT16 = 0,
	BAND_TYPE_SINT16,

	// Reserve a block of values for encoded bands
	BAND_TYPE_ENCODED = 16,
	BAND_TYPE_ENCODED_RUNLENGTHS,		// Encoding method used by the codec

} BAND_TYPE;


#ifdef __cplusplus
extern "C" {
#endif

CODEC_ERROR OpenBandFile(BANDFILE *bandfile, const char *pathname);

CODEC_ERROR FindNextBand(BANDFILE *bandfile);

CODEC_ERROR ReadFileHeader(BANDFILE *bandfile);

CODEC_ERROR ReadFrameHeader(BANDFILE *bandfile);

CODEC_ERROR ReadChannelHeader(BANDFILE *bandfile);

CODEC_ERROR ReadWaveletHeader(BANDFILE *bandfile);

CODEC_ERROR ReadBandHeader(BANDFILE *bandfile);

CODEC_ERROR ReadBandData(BANDFILE *bandfile, void *data, size_t size);


CODEC_ERROR CreateBandFile(BANDFILE *bandfile, const char *pathname);

CODEC_ERROR WriteFileHeader(BANDFILE *bandfile, int max_band_width, int max_band_height);

CODEC_ERROR WriteWaveletBand(BANDFILE *bandfile,
							 int frame,
							 int channel,
							 int wavelet,
							 int band,
							 int type,
							 int width,
							 int height,
							 void *data,
							 size_t size);

CODEC_ERROR WriteFrameHeader(BANDFILE *bandfile, int frame);

CODEC_ERROR WriteChannelHeader(BANDFILE *bandfile, int channel);

CODEC_ERROR WriteWaveletHeader(BANDFILE *bandfile, int wavelet);

CODEC_ERROR WriteBandHeader(BANDFILE *bandfile, int band, int type, int width, int height, size_t size);

CODEC_ERROR WriteBandData(BANDFILE *bandfile, void *data, size_t size);

CODEC_ERROR CloseBandFile(BANDFILE *bandfile);


CODEC_ERROR WriteDecodedBandFile(DECODER *decoder,
								 int channel_index,
								 uint32_t subband_mask,
								 const char *pathname);

CODEC_ERROR WriteDecodedWaveletBandFile(DECODER *decoder,
										int channel_index,
										int wavelet_index,
										uint32_t band_mask,
										const char *pathname);

CODEC_ERROR WriteDecodedTransformBandFile(DECODER *decoder,
										  int channel_index,
										  uint32_t wavelet_mask,
										  uint32_t wavelet_band_mask,
										  const char *pathname);

CODEC_ERROR WriteDecodedTransformBands(DECODER *decoder,
									   uint32_t channel_mask,
									   uint32_t channel_wavelet_mask,
									   uint32_t wavelet_band_mask,
									   const char *pathname);

#ifdef __cplusplus
}
#endif

#endif
