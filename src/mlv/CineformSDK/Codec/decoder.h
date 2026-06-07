/*! @file decoder.h

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

#ifndef _DECODER_H
#define _DECODER_H

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include "../Common/macdefs.h"
#endif

#include <stdio.h>
#include "config.h"
#include "codec.h"
#include "color.h"
#include "bitstream.h"
#include "../Common/AVIExtendedHeader.h"

#if _THREADED
#include "thread.h"
#endif


// Should move decoder definition here

#define DECODER_FLAGS_NORMAL 		0x00000000		// Default flags
#define DECODER_FLAGS_RENDER 		0x00000001		// The decoded frame will be rendered
#define DECODER_FLAGS_YUV709 		0x00000002		// Using BT.709
#define DECODER_FLAGS_VIDEO_RGB 	0x00000004		// Use 16-235 RGB vs sRGB
#define DECODER_FLAGS_HIGH_QUALITY 	0x00000008		// Use green ripple filtering for CineForm RAW clips

#define DECODED_FLAGS_NORENDER		0x00000000		// The decoded frame will not be rendered

// Output formats that will be supported by the decoder
typedef enum decoded_format
{
	DECODED_FORMAT_UNSUPPORTED = 0,

	DECODED_FORMAT_YUYV = COLOR_FORMAT_YUYV,
	DECODED_FORMAT_UYVY = COLOR_FORMAT_UYVY,

	DECODED_FORMAT_RGB32 = COLOR_FORMAT_RGB32,
	DECODED_FORMAT_RGB24 = COLOR_FORMAT_RGB24,

	DECODED_FORMAT_RGB32_INVERTED = MAKE_FORMAT(FRAME_FORMAT_INVERTED, COLOR_FORMAT_RGB32),
	DECODED_FORMAT_RGB24_INVERTED = MAKE_FORMAT(FRAME_FORMAT_INVERTED, COLOR_FORMAT_RGB24),

	DECODED_FORMAT_V210 = COLOR_FORMAT_V210,
	DECODED_FORMAT_YU64 = COLOR_FORMAT_YU64,

	DECODED_FORMAT_ROW16U = COLOR_FORMAT_YR16,

	DECODED_FORMAT_YUVA = COLOR_FORMAT_YUVA,

	// The following two formats are not currently implemented
	DECODED_FORMAT_RG48 = COLOR_FORMAT_RG48,		//encoded as RGB
	DECODED_FORMAT_WP13 = COLOR_FORMAT_WP13,		//encoded as RGB
	DECODED_FORMAT_W13A = COLOR_FORMAT_W13A,		//encoded as RGBA
	DECODED_FORMAT_RG64 = COLOR_FORMAT_RG64,		//encoded as RGBA
	DECODED_FORMAT_RG30 = COLOR_FORMAT_RG30,		//encoded as RG30 (packed RGB48)
	DECODED_FORMAT_R210 = COLOR_FORMAT_R210,		//encoded as RG30 (packed RGB48)
	DECODED_FORMAT_DPX0 = COLOR_FORMAT_DPX0,		//encoded as RG30 (packed RGB48)
	DECODED_FORMAT_AR10 = COLOR_FORMAT_AR10,		//
	DECODED_FORMAT_AB10 = COLOR_FORMAT_AB10,		//

	// YUV 4:2:0 formats used by MPEG codecs
	DECODED_FORMAT_NV12 = COLOR_FORMAT_NV12,
	DECODED_FORMAT_YV12 = COLOR_FORMAT_YV12,

	// Bayer formats
	DECODED_FORMAT_BYR1 = COLOR_FORMAT_BYR1,
	DECODED_FORMAT_BYR2 = COLOR_FORMAT_BYR2,
	DECODED_FORMAT_BYR3 = COLOR_FORMAT_BYR3,
	DECODED_FORMAT_BYR4 = COLOR_FORMAT_BYR4,
	DECODED_FORMAT_BYR5 = COLOR_FORMAT_BYR5,

	// QuickTime formats
	DECODED_FORMAT_B64A = COLOR_FORMAT_B64A,
	DECODED_FORMAT_R4FL = COLOR_FORMAT_R4FL,
	DECODED_FORMAT_2VUY = COLOR_FORMAT_UYVY,
	DECODED_FORMAT_R408 = COLOR_FORMAT_R408,
	DECODED_FORMAT_V408 = COLOR_FORMAT_V408,

	// Avid formats (used internally because these definitions are more precise)
	DECODED_FORMAT_CbYCrY_8bit = COLOR_FORMAT_CbYCrY_8bit,
	DECODED_FORMAT_CbYCrY_16bit = COLOR_FORMAT_CbYCrY_16bit,
	DECODED_FORMAT_CbYCrY_10bit_2_8 = COLOR_FORMAT_CbYCrY_10bit_2_8,
	DECODED_FORMAT_CbYCrY_16bit_2_14 = COLOR_FORMAT_CbYCrY_16bit_2_14,
	DECODED_FORMAT_CbYCrY_16bit_10_6 = COLOR_FORMAT_CbYCrY_16bit_10_6,

	// Alternate names using the Avid naming conventions
	DECODED_FORMAT_CT_UCHAR = DECODED_FORMAT_CbYCrY_8bit,
	DECODED_FORMAT_CT_SHORT = DECODED_FORMAT_CbYCrY_16bit,
	DECODED_FORMAT_CT_10Bit_2_8 = DECODED_FORMAT_CbYCrY_10bit_2_8,
	DECODED_FORMAT_CT_SHORT_2_14 = DECODED_FORMAT_CbYCrY_16bit_2_14,
	DECODED_FORMAT_CT_USHORT_10_6 = DECODED_FORMAT_CbYCrY_16bit_10_6,

	// Alternative names
	DECODED_FORMAT_RGBA = DECODED_FORMAT_RGB32,
	DECODED_FORMAT_RGBa = DECODED_FORMAT_RGB32_INVERTED,
	DECODED_FORMAT_YR16 = DECODED_FORMAT_ROW16U,

	//NOTE: After the YUVA format is fully supported, change the
	// entry for the maximum decoded format to the new YUVA format.
	DECODED_FORMAT_MINIMUM = DECODED_FORMAT_UYVY,
	DECODED_FORMAT_MAXIMUM = DECODED_FORMAT_ROW16U,

} DECODED_FORMAT;

// Range of valid color formats encountered during decoding
#define MAX_DECODED_COLOR_FORMAT	13

typedef enum decoded_resolution
{
	DECODED_RESOLUTION_UNSUPPORTED = 0,			// Unknown decoded resolution
	DECODED_RESOLUTION_FULL = 1,				// Full resolution decoding
	DECODED_RESOLUTION_HALF,					// Half resolution decoding
	DECODED_RESOLUTION_QUARTER,					// Quarter resolution decoding at full frame rate
	DECODED_RESOLUTION_LOWPASS_ONLY,			// Lowest resolution decoding

	DECODED_RESOLUTION_FULL_DEBAYER,
	DECODED_RESOLUTION_HALF_NODEBAYER,			// Decode 4K RAW at 2K into BYR2 (requires fake bayer reconstruction)
	DECODED_RESOLUTION_QUARTER_NODEBAYER_SCALED,// To allow uncompressed RAW to decode at Quarter res.
	DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER,	

	// Useful for 3D
	DECODED_RESOLUTION_HALF_HORIZONTAL,		// Decode 1920x1080 at 960x1080 using fewer subbands in the inverse wavelet
	DECODED_RESOLUTION_HALF_VERTICAL,		// Decode 1920x1080 at 1920x540 using fewer subbands in the inverse wavelet

	// Add more entries here

	// Older code used this SD definition for half resolution
	DECODED_RESOLUTION_SIF = DECODED_RESOLUTION_HALF

} DECODED_RESOLUTION;

// Encoded samples must be aligned on a four byte boundary
#define ENCODED_SAMPLE_ALIGNMENT	4

typedef struct sample_header
{
	// Errors code for parsing the sample header
	CODEC_ERROR error;

	// Dimensions of the encoded frames
	uint32_t width;
	uint32_t height;
	uint32_t display_height;

	// One channel for 2D, two channels for 3D
	uint32_t videoChannels;
 
	// Version number: major << 16 | minor << 8 | revision (no build number)
	uint32_t encoder_version;

	int key_frame;
	int difference_frame;
	int droppable_frame;

	// Is the video interlaced or progressive?
	int hdr_progressive;
	int hdr_uncompressed;

	// Original format of the encoded frames
	COLOR_FORMAT input_format;

	// Internal representation of the encoded data
	ENCODED_FORMAT encoded_format;

	uint32_t encode_quality;

	// Frame number of the sample (used for debugging)
	uint32_t frame_number;

	// Interlaced field information
	int interlaced_flags;

	// Size of the left stereo sample and offset to the right stereo sample (in bytes)
	int left_sample_size;

	// Find the low pass bands
	int find_lowpass_bands;
	int thumbnail_channel_offsets[CODEC_MAX_CHANNELS];
	int thumbnail_channel_offsets_2nd_Eye[CODEC_MAX_CHANNELS];

	//TODO: Modify the CHANNEL_OFFSET struct if the channels offset definitions change

} SAMPLE_HEADER;

#ifdef __cplusplus
extern "C" {
#endif

//extern int PixelSize[];

#if (0 && _DEBUG)
extern char *decoded_format_string[];
#endif

// Find the codec tag (not the metadata tag) in the specified buffer
bool GetTuplet(unsigned char *data, int datasize,
			   unsigned short findtag, unsigned short *retvalue);

// Return the address of the codec tag (not the metadata tag)
uint8_t *GetTupletAddr(uint8_t *data, int datasize,
					   uint16_t findtag, int16_t *retvalue);

#if 0
unsigned char *GetTupletAddr(unsigned char *data, int datasize,
			   unsigned short findtag, unsigned short *retvalue);
#endif

void InitDecoder(DECODER *decoder, FILE *logfile, CODESET *cs);
void ClearDecoder(DECODER *decoder);

#if _ALLOCATOR
bool DecodeInit(ALLOCATOR *allocator, DECODER *decoder, int width, int height, int format, int resolution, FILE *logfile);
#else
bool DecodeInit(DECODER *decoder, int width, int height, int format, int resolution, FILE *logfile);
#endif
size_t DecoderSize();

void DecodeEntropyInit(DECODER *decoder);

bool DecodeOverrides(DECODER *decoder, unsigned char *overrideData, int overrideSize);
bool DecodeSample(DECODER *decoder, BITSTREAM *input, uint8_t *output, int pitch, ColorParam *colorparams, CFHDDATA *cfhddata);
void DecodeRelease(DECODER *decoder, TRANSFORM **transform, int num_transforms);
void DecodeForceMetadataRefresh(DECODER *decoder);

// Decode a sample that encoded a group of frames (return the first frame)
bool DecodeSampleGroup(DECODER *decoder, BITSTREAM *input, uint8_t *output, int pitch, ColorParam *colorparams);

// Decode a sample that represents the second frame in a group
bool DecodeSampleFrame(DECODER *decoder, BITSTREAM *input, uint8_t *output, int pitch, ColorParam *colorparams);

// Decode a sample that represents an isolated frame
bool DecodeSampleIntraFrame(DECODER *decoder, BITSTREAM *input, uint8_t *output, int pitch, ColorParam *colorparams);

bool ParseSampleHeader(BITSTREAM *sample, SAMPLE_HEADER *header);
bool DumpSampleHeader(BITSTREAM *input, FILE *logfile);

// Return the pixel size of the decoded format (in bytes)
int DecodedPixelSize(DECODED_FORMAT format);

void GetDisplayAspectRatio(DECODER *decoder, int *w, int *h);

// Compute the resolution corresponding to the specified combination of input and output dimensions
int DecodedResolution(int input_width, int input_height, int output_width, int output_height);

// Compute the decoded resolution that is closest to the output dimensions (for scaling)
int DecodedScale(int input_width, int input_height, int output_width, int output_height);

// Compute the dimensions of the decoded image from the encoded dimensions and decoding resolution
void ComputeDecodedDimensions(int encoded_width, int encoded_height, int decoded_resolution,
							  int *decoded_width_out, int *decoded_height_out);

// Return true if the specified resolution is supported
bool IsDecodedResolution(int resolution);

// Return true if the encoded sample is a key frame
bool IsSampleKeyFrame(uint8_t *sample, size_t size);

// Return true if this decoder can decode to quarter resolution
bool IsQuarterResolutionEnabled(DECODER *decoder);

// Return the number of the more recent decoded frame
uint32_t DecodedFrameNumber(DECODER *decoder);

void SetDecoderFormat(DECODER *decoder, int width, int height, int format, int resolution);
void SetDecoderCapabilities(DECODER *decoder);
int GetDecoderCapabilities(DECODER *decoder);
bool SetDecoderColorFlags(DECODER *decoder, uint32_t color_flags);
void SetDecoderFlags(DECODER *decoder, uint32_t flags);
bool ResizeDecoderBuffer(DECODER *decoder, int width, int height, int format);

IMAGE *DecodeNextFrame(DECODER *decoder, BITSTREAM *input);
#ifdef _WIN32
bool DecodeFile(DECODER *decoder, HANDLE file);
#endif
bool DecodeSequence(DECODER *decoder, BITSTREAM *input);
bool DecodeGroup(DECODER *decoder, BITSTREAM *input, int sample_type, ColorParam *colorparams);
bool DecodeGroupTransform(DECODER *decoder, BITSTREAM *input, int sample_type, ColorParam *colorparams);
bool DecodeHighPassBand8s(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet, int index, int band, int subband, int channel, ColorParam *colorparams);
bool SkipHighPassBand(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet, int index, int band, int subband, int channel, ColorParam *colorparams);
bool DecodeHighPassBand(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet, int index, int band, int subband, int channel, ColorParam *colorparams);
bool SkipHighPassBands(DECODER *decoder, BITSTREAM *stream);
bool DecodeEmptyHighPassBand(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet, int index, int band, int subband, int channel, ColorParam *colorparams);
bool DecodeBandCodes(DECODER *ecoder, BITSTREAM *stream, IMAGE *wavelet,
					 int band_index, int width, int height, int quantization);
bool DecodeBandRuns(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
					int band_index, int width, int height, int quantization);

// Decode the highpass band in a temporal transform, then perform dequantization
// and compute the inverse temporal transform in one pass for reduced memory usage.
bool DecodeTemporalBand8s(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet, IMAGE *lowpass[],
						  int index, int band, int subband, int channel,
						  PIXEL *buffer, size_t buffer_size, ColorParam *colorparams);

// Optimized decoding routine
bool DecodeFastRuns(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
					int band_index, int width, int height, int quantization);

// Finite state machine decoder for run length encoded coefficients
bool DecodeFastRunsFSM(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
					   int band_index, int width, int height, int quantization);
bool DecodeFastRunsFSM8s(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
						 int band_index, int width, int height);
bool DecodeFastRunsFSM16s(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
						 int band_index, int width, int height, int threading);
bool SkipFastRunsFSM(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
					 int band_index, int width, int height);

// Start of a routine that combines highpass band decoding with temporal inverse transform
bool DecodeBandRunsFSM8s(DECODER *decoder, BITSTREAM *stream, IMAGE *wavelet,
						 int band_index, int width, int height,
						 IMAGE *even, IMAGE *odd);


void CopyLowpass16sToBuffer(DECODER *decoder, IMAGE *images[], int num_channels,
							uint8_t *output_buffer, int32_t output_pitch,
							struct frame_info *info, int chroma_offset,
							int precision, int encoded_format, int whitebitdepthint);

// Update the codec state with the information in a tag value pair
CODEC_ERROR UpdateCodecState(DECODER *decoder, BITSTREAM *input, CODEC_STATE *codec, TAGWORD tag, TAGWORD value);

// Update the transform data structure using the information in the codec state
void UpdateCodecTransform(TRANSFORM *transform, CODEC_STATE *codec);
bool DecodeSampleSubband(DECODER *decoder, BITSTREAM *input, int subband);
bool DecodeSampleChannelHeader(DECODER *decoder, BITSTREAM *input);

void DeQuantFSM(FSM *fsm, int quant);

// Routines that combine inverse wavelet transforms with color conversion

// Apply the inverse horizontal-temporal transform to reconstruct the output frame
void ReconstructFrameToBuffer(DECODER *decoder, TRANSFORM *transform[],
								   int frame, uint8_t *output, int pitch);

// Invert the wavelet to reconstruct the lower wavelet in the transform
#if 0
void ReconstructWaveletBand(TRANSFORM *transform, int channel, IMAGE *wavelet, int index,
							int precision, PIXEL *buffer, size_t buffer_size);
#else
void ReconstructWaveletBand(DECODER *decoder, TRANSFORM *transform, int channel,
							IMAGE *wavelet, int index,
							int precision, const SCRATCH *scratch,
							int allocations_only /* for queued work */);
#endif

// Apply the inverse horizontal-temporal transform and pack the output into a buffer
void TransformInverseFrameToYUV(TRANSFORM *transform[], int frame, int num_channels,
								uint8_t *output, int pitch, FRAME_INFO *info,
								const SCRATCH *scratch, int chroma_offset, int precision);

// Apply the inverse horizontal-temporal transform and output rows of luma and chroma
void TransformInverseFrameToRow16u(DECODER *decoder, TRANSFORM *transform[], int frame_index, int num_channels,
								   PIXEL16U *output, int output_pitch, FRAME_INFO *frame,
								   const SCRATCH *scratch, int chroma_offset,
								   int precision);

// Apply the inverse horizontal-temporal transform and pack the output into a buffer
#if 0
void TransformInverseFrameToBuffer(TRANSFORM *transform[], int frame_index, int num_channels,
								   uint8_t *output, int output_pitch, FRAME_INFO *frame,
								   char *buffer, size_t buffer_size, int chroma_offset,
								   int precision);
#else
void TransformInverseFrameToBuffer(TRANSFORM *transform[], int frame_index, int num_channels,
								   uint8_t *output, int output_pitch, FRAME_INFO *frame,
								   const SCRATCH *scratch, int chroma_offset, int precision);
#endif

// Routines that perform color conversion and pack the pixels in the output buffer

// Copy image to buffer with specified output format
void CopyImageToBuffer(IMAGE *image, uint8_t *output_buffer, int32_t output_pitch, int format);

void ConvertYUVStripPlanarToBuffer(uint8_t *planar_output[], int planar_pitch[], ROI roi,
								   uint8_t *output_buffer, int output_pitch, int frame_width,
								   int format, int colorspace);

void ConvertRow16uToDitheredBuffer(DECODER *decoder, uint8_t *planar_output[], int planar_pitch[], ROI roi,
								   uint8_t *output_buffer, int output_pitch, int frame_width,
								   int format, int colorspace);

// Convert one row of packed YUYV to the specified color
void ConvertRowYUYV(uint8_t *input, uint8_t *output, int length, int format, int colorspace, int precision);

#if _DEBUG
bool DecodeBandFSM8sNoGap(FSM *fsm, BITSTREAM *stream, PIXEL8S *image, int width, int height, int pitch);
#endif

#if _THREADED_DECODER

//DWORD ThreadedBandMask(int transform_type, int index);
//IMAGE *ThreadOutputWavelet(TRANSFORM *transform, IMAGE *wavelet, int index);
//void SetThreadNextWavelet(DECODER *decoder, TRANSFORM *transform, int index);

IMAGE *GetWaveletThreadSafe(DECODER *decoder, TRANSFORM *transform, int index,
							int width, int height, int level, int type);

void UpdateWaveletBandValidFlags(DECODER *decoder, IMAGE *wavelet, int band);
void UpdateWaveletBandStartedFlags(DECODER *decoder, IMAGE *wavelet, int band);
bool DecodedBandsValid(IMAGE *wavelet, int index, int transform_type);
void QueueThreadedTransform(DECODER *decoder, int channel, int wavelet_index);
bool VerifyTransformQueue(DECODER *decoder);
void WaitForTransformThread(DECODER *decoder);
THREAD_PROC(TransformThreadProc, lpParam);

#endif

enum	// Types of transforms supported by the worker threads
{
	THREAD_TRANSFORM_FRAME_YUV = 1, // interlaced
	THREAD_TRANSFORM_FRAME_ROW16U,
/*	THREAD_TRANSFORM_SPATIAL_YUV,  // progressive formats moved to the new threading model
	THREAD_TRANSFORM_SPATIAL_ROW16U,
	THREAD_TRANSFORM_SPATIAL_BAYER2YUV,
	THREAD_TRANSFORM_SPATIAL_BAYER_3DLUT_YUV,
	THREAD_TRANSFORM_SPATIAL_RGB2YUV,
	THREAD_TRANSFORM_SPATIAL_RGB2YR16,
	THREAD_TRANSFORM_SPATIAL_RGB2RG30,
	THREAD_TRANSFORM_SPATIAL_RGB2r210,
	THREAD_TRANSFORM_SPATIAL_RGB32,
	THREAD_TRANSFORM_SPATIAL_BAYER_NEW3DLUT,
	THREAD_TRANSFORM_SPATIAL_RGB2B64A,
*/
};

#if _INTERLACED_WORKER_THREADS

// Routines that invoke the worker threads to perform the transforms

void TransformInverseFrameThreadedToYUV(DECODER *decoder, int frame_index, int num_channels,
										uint8_t *output, int pitch, FRAME_INFO *info,
										int chroma_offset, int precision);

void TransformInverseFrameThreadedToRow16u(DECODER *decoder, int frame_index, int num_channels,
										   PIXEL16U *output, int pitch, FRAME_INFO *info,
										   int chroma_offset, int precision);

// Routines that perform the threaded transforms

DWORD WINAPI InterlacedWorkerThreadProc(LPVOID lpParam);

void TransformInverseFrameSectionToYUV(DECODER *decoder, int thread_index, int frame_index, int num_channels,
									   uint8_t *output, int output_pitch, FRAME_INFO *info,
									   int chroma_offset, int precision);

void TransformInverseFrameSectionToRow16u(DECODER *decoder, int thread_index, int frame_index, int num_channels,
										  PIXEL16U *output, int output_pitch, FRAME_INFO *info,
										  int chroma_offset, int precision);
#endif


#if _THREADED

// Threaded inverse transform using the new threads API
void TransformInverseSpatialThreadedYUV422ToBuffer(DECODER *decoder, int frame_index, int num_channels,
											 uint8_t *output, int pitch, FRAME_INFO *info,
											 int chroma_offset, int precision);

void TransformInverseSpatialUniversalThreadedToRow16u(DECODER *decoder, int frame_index, int num_channels,
											 uint8_t *output, int pitch, FRAME_INFO *info,
											 int chroma_offset, int precision);

void TransformInverseSpatialUniversalThreadedToOutput(DECODER *decoder, int frame_index, int num_channels,
											 uint8_t *output, int pitch, FRAME_INFO *info,
											 int chroma_offset, int precision,
											 HorizontalInverseFilterOutputProc horizontal_filter_proc);


// Routines for the worker threads that use the new threads API

THREAD_PROC(TransformWorkerThreadProc, lpParam);
THREAD_PROC(WorkerThreadProc, lpParam);
THREAD_PROC(EntropyWorkerThreadProc, lpParam);
THREAD_PROC(ParallelThreadProc, lpParam);

void TransformInverseSpatialSectionToBuffer(DECODER *decoder, int thread_index,
											int frame_index, int num_channels,
											uint8_t *output, int pitch, FRAME_INFO *info,
											int chroma_offset, int precision, int type);


#endif

#ifdef __cplusplus
}
#endif

#endif
