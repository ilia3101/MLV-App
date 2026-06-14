/*! @file encoder.h

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

#ifndef _ENCODE_H
#define _ENCODE_H

#include "config.h"
#include "codec.h"
#include "wavelet.h"
#include "quantize.h"
//#include <vfw.h>
#include "dump.h"
#include "metadata.h"
#include "bandfile.h"

#define TRANSFORM_GOP_LENGTH	2

#ifndef TRANSFORM_NUM_SPATIAL
#if _FIELDPLUS_TRANSFORM
#define TRANSFORM_NUM_SPATIAL	3
#else
#define TRANSFORM_NUM_SPATIAL	2
#endif
#endif

#ifndef ENCODER_MAX_THREADS
#define ENCODER_MAX_THREADS		3
#endif

// Use the same structure packing as the Intel C/C++ compiler
//#pragma pack(push)
//#pragma pack(16)
#pragma pack(push, 16)

#define	ENCODEINITFLAGS_CHROMA_FULL_RES		(1<<0)
#define	ENCODEINITFLAGS_SET601				(1<<1)
#define	ENCODEINITFLAGS_SET709				(1<<2)
#define	ENCODEINITFLAGS_SETcgRGB			(1<<3)
#define	ENCODEINITFLAGS_SETvsRGB			(1<<4)

// Structure used to pass encoding parameters during initialization
typedef struct encoding_parameters
{
	uint32_t version;
	uint32_t gop_length;
	uint32_t encoded_width;
	uint32_t encoded_height;
	FILE *logfile;
	int fixed_quality;
	int fixed_bitrate;
	int format;
	int progressive;
	FRAME_SAMPLING frame_sampling;

	uint32_t colorspace_yuv;	//0 = unset, 1 = 601, 2 = 709
	uint32_t colorspace_rgb;	//0 = unset, 1 = cgRGB, 2 = vsRGB

} ENCODING_PARAMETERS;

// Increment the current version when the encoding parametes are changed
#define ENCODING_PARAMETERS_CURRENT_VERSION		1

#if _THREADED_ENCODER

typedef enum {
	THREAD_TYPE_SINGLE = 0,		// No parallel processing
	THREAD_TYPE_COLORS,			// Process color channels in parallel
	THREAD_TYPE_FRAMES,			// Process frames in parallel

	THREAD_NUM_TYPES,			// Number of different types of threads

	// Assign the default type of thread
	THREAD_TYPE_DEFAULT = THREAD_TYPE_COLORS

} THREAD_TYPE;

typedef struct thread_frame_data
{
	BYTE *input;
	int input_pitch;
	FRAME_INFO frame;
	TRANSFORM **transform;
	int frame_index;
	int num_channels;
	PIXEL *buffer;
	size_t buffer_size;
	int chroma_offset;

} THREAD_FRAME_DATA;

typedef struct thread_spatial_data
{
	int channel;
	BYTE *input;
	int input_pitch;
	PIXEL *band[CODEC_MAX_BANDS];
	int pitch[CODEC_MAX_BANDS];
	int width;
	int height;
	int quantization[IMAGE_NUM_BANDS];

} THREAD_SPATIAL_DATA;

typedef struct _thread_finish_data
{
	ENCODER *encoder;
	TRANSFORM *transform;
	int channel;
	int prescale;
	int num_frames;
	int num_spatial;

} THREAD_FINISH_DATA;

typedef struct _thread_encode_data
{
	ENCODER *encoder;
	BITSTREAM *bitstream;
	TRANSFORM *transform;
	size_t channel_size;
	int channel;

} THREAD_ENCODE_DATA;

typedef struct thread_field_data
{
	int channel;
	int frame_height;
	int frame_format;
	BYTE *even_row_ptr;
	BYTE *odd_row_ptr;
	int field_pitch;
	int frame_row_length;
	PIXEL *temporal_lowpass;
	PIXEL *temporal_highpass;
	int offset;
	PIXEL *horizontal_lowlow;
	PIXEL *horizontal_lowhigh;
	PIXEL *horizontal_highlow;
	PIXEL *horizontal_highhigh;
	int horizontal_width;
	int horizontal_pitch;
	PIXEL *lowhigh_row_buffer;
	PIXEL *highlow_row_buffer;
	PIXEL *highhigh_row_buffer;
	int temporal_width;
	int lowlow_scale;
	int lowhigh_scale;
	int highlow_scale;
	int highhigh_scale;
	int quantization[CODEC_MAX_BANDS];

} THREAD_FIELD_DATA;

#endif


/*!
	@brief Data structure for storing the encoder state information

	This data structure holds all of the information used by the encoder
	to convert input frames to an intermediate format (if necessary), apply
	the wavelet transform, encoded the wavelet bands, and pack the encoded
	bands into the bitstream for a sample.

	This encoder data structure includes fields for both wavelet band encoding
	and arranging bands into the sample bitstream.  Future implementations may
	perform the transforms, quantization, and variable-length encoding in a
	DSP or FPGA leaving the creation of the sample bitstream to another processor
	such as an ARM.  To facilitate such partitioning of the encoder, this data
	structure is being reorganized into different sections for each phase of
	encoding.

	Note: The lowpass pixel statistics in the encoder may duplicate some of
	the statistics in the image descriptor, but the statistics used by the
	encoder may change frequently as the encoder is refined so it is better
	to encapsulate the statistics used for encoding in the encoder state.

	@todo Eliminate the code that unpacks the input frame into separate channels
	by performing unpacking and conversion in the first wavelet transform.  Any
	unpacking and conversion should be performed row by row with the converted rows
	delivered to the first wavelet transform on row at a time.  This scheme would
	use less memory and be more cache efficient.
*/
typedef struct encoder {	// Encoder state (derived from codec)

	/***** The following fields are common between the encoder and decoder *****/

	FILE *logfile;				// Used for saving encoder progress messages
	CODEC_ERROR error;			// Error code set during encoding
	uint32_t frame_count;		// Number of frames encoded

	ALLOCATOR *allocator;		// Interface for memory allocation (optional)

	CODEC_STATE codec;			// Current state of bitstream during encoding

#if _DUMP
	DUMP_INFO dump;			// Used for dumping wavelet bands to files
#endif

	/***** End of the fields that are common between the encoder and decoder *****/


	/***** Fields for the wavelet transforms and variable-length encoding *****/

	struct {				// Dimensions and format of the input to the encoder
		int width;
		int height;
		int format;			// Input pixel format (see COLOR_FORMAT)
		//int display_height;
		int color_space;
	} input;

	//NOTE: Currently the pixel format and color space are stored in seperate fields
	// in the input struct.  The color space could be stored in the high word of the
	// format double word as is done in the decoder.

	struct {				// Dimensions and format of the encoded data
		//int width;
		//int height;
		int format;
	} encoded;

	struct {				// Dimensions and format of the Bayer image
		int width;
		int height;
		int format;			// Bayer pixel pattern (see BAYER_FORMAT)
	} bayer;

	struct {				// Dimensions of the displayed image (display aperture)
		int width;
		int height;
	} display;

	struct					// Information returned from encoding
	{
		int iskey;			// Was the frame encoded as a key frame?

	} output;

	uint8_t num_levels;		// Number of levels used by this encoder
	uint8_t num_spatial;	// Number of spatial wavelet levels
	uint8_t gop_length;		// Number of frames in group of pictures
	uint8_t num_subbands;	// Number of subbands encoded into the bitstream

	//TODO: Eliminate the intermediate frame
	//TODO: Perform conversion in the first wavelet transform
	FRAME *frame;			// Current encoded frame

	uint32_t encode_curve;			// 0 - Unset/default, used for BYR4 linear to curve mapping.
	uint32_t encode_curve_preset;	// 1 - used for BYR4 to indicate that curve is pre-applied.

	uint32_t presentationWidth;		// To support resolution independent decoding
	uint32_t presentationHeight;	// To support resolution independent decoding

	// Quantization parameters used by this encoder
	QUANTIZER q;

	uint8_t num_quant_channels;		// Number of channels in the quant table
	uint8_t num_quant_subbands;		// Number of subbands for each channel

	// Codebooks used for encoding
	//VLCBOOK *magsbook[CODEC_NUM_CODESETS];
	//RMCBOOK *codebook[CODEC_NUM_CODESETS];
	RLCBOOK *codebook_runbook[CODEC_NUM_CODESETS];	// Codebook for the run length
	VLCBOOK *codebook_magbook[CODEC_NUM_CODESETS];

	VALBOOK *valuebook[CODEC_NUM_CODESETS];		// Indexable table for signed values

	//TODO: Eliminate the following copy of the band end codes
	// because the band end codes are already in the codebook

	// Band end codeword and size (in bits) for each codebook
	uint32_t band_end_code[CODEC_NUM_CODESETS];
	int band_end_size[CODEC_NUM_CODESETS];

	int64_t lastgopbitcount;	// Used by variable bitrate control
	int vbrscale;				// Variable bitrate scale factor

#if 0
	/*
		The lowpass statistics are only referenced by the routine ComputeLowPassStatistics
		which is no longer called from any code fragment that has not been commented out.
	*/

	// Statistics used for encoding the lowpass band
	struct lowpass_encoding
 	{
		PIXEL average;
		PIXEL minimum;
		PIXEL maximum;
		//PIXEL mode;		// Location of the peak in the lowpass histogram
		//PIXEL offset;		// Pixel value subtracted prior to quantization
	} lowpass;
#endif

	struct					// Information about the group of frames (GOP)
	{
		int count;
		//FRAME *frame[CODEC_GOP_LENGTH];
	} group;

	//TODO: Eliminate the progressive and chroma full resolution member variables
	// since they are already defined in the codec state data structure?

	int progressive;		// Progressive or interlaced frame encoding
	int encoder_quality;	//requested encoder quality //DAN20060626
	int encoded_format;		// Encoder initiallized with this base format
	int chromaFullRes;		// True if the encoder was initialized with full resolution chroma

	// Number of the most recent frame processed by the encoder (first frame is number one)
	uint32_t frame_number;

#if _THREADED_ENCODER

	// Threads for processing each frame in the group
	HANDLE frame_thread[CODEC_GOP_LENGTH];

	// Opaque reference to the object that owns this encoder
	//void *handle;

	DWORD affinity_mask;

	// Channel processing threads within each frame
	HANDLE frame_channel_thread[CODEC_GOP_LENGTH][CODEC_MAX_CHANNELS];

	// Threads for processing channels after the first level transform
	HANDLE finish_channel_thread[CODEC_MAX_CHANNELS];

	// Data structures for parameters passed to thread procedures
	THREAD_FRAME_DATA thread_frame_data[CODEC_GOP_LENGTH];
	THREAD_SPATIAL_DATA thread_spatial_data[CODEC_GOP_LENGTH][CODEC_MAX_CHANNELS];
	THREAD_FINISH_DATA thread_finish_data[CODEC_MAX_CHANNELS];
	THREAD_ENCODE_DATA thread_encode_data[CODEC_MAX_CHANNELS];
	THREAD_FIELD_DATA thread_field_data[CODEC_MAX_CHANNELS];

#endif

	int no_video_seq_hdr;  // default 0, set when do encoder2 as the sequence header is thrown away, we need an normal P frame.

	uint32_t video_channels;	// 0 not used, 1 - default (ignore),
									// 2 - stereo/2channel (double height),
									// 3 channels (triple height), etc.
	uint32_t video_channel_gap;	// default 0, to help with HMDI 1.4 3D encodes

	uint32_t ignore_overrides;		// When video_Channel is set by TAG_VIDEO_CHANNELS, ignore the 3D setting in the override.colr file.

	uint32_t current_channel;	// 0 - first, 1 - second etc.
	uint32_t mix_type_value;	// 1 = stacked half height, 2 = sibe_by-side, 3 = fields, 16-21 = anaglypth
	uint32_t preformatted3D;	// 2 channel 3D, at half height or width (based on mix_type_value)

	uint32_t limit_yuv; // Canon 5D patch
	uint32_t conv_601_709; // Canon 5D patch

	int	uncompressed;		// is this frame uncompressed
	uint8_t unc_lastsixteen[16];
	uint8_t *unc_buffer;
	uint8_t *unc_data;
	int unc_pitch;
	FRAME unc_frame;
	int unc_origformat;

	//Used by BRY5 unpacking, can be used by
	uint8_t *linebuffer;

	// use to generate a DPX thumbnail.
	int thumbnail_generate;


	/***** Parameters for higher-level encoding operations *****/

	// The 16 byte license key controls what encoder features are enabled
	//NOTE: The license key must be decrypted into a LICENSE structure
	uint8_t licensekey[16];

	uint32_t ignore_database;

	int reported_license_issue;
	int reported_error;
	
	struct {
		//uint32_t *global_metadata;
		//size_t global_metasize;
		//uint32_t *local_metadata;
		//size_t local_metasize;
		METADATA global;
		METADATA local;
	} metadata;

//database overrides
	uint32_t last_set_time;		// External Metadata is only checked every 1000ms
	char OverridePathStr[260];	// default path to overrides
	char LUTsPathStr[260];		// default path to LUTs
	char UserDBPathStr[64];		// database directory in LUTs
	unsigned char baseData[MAX_ENCODE_DATADASE_LENGTH]; // default user data
	uint32_t baseDataSize; // default user data
	//unsigned char userData[MAX_DATADASE_LENGTH]; // database user data
	//uint32_t userDataSize; // database user data
	unsigned char forceData[MAX_ENCODE_DATADASE_LENGTH];// override user data
	uint32_t forceDataSize; // override user data

#if _DEBUG
	// Band data file and bitstream used to debug entropy coding of highpass bands
	BANDFILE encoded_band_file;
	BITSTREAM *encoded_band_bitstream;
	int encoded_band_channel;
	int encoded_band_wavelet;
	int encoded_band_number;
#endif

} ENCODER;

//TODO: Need to update this initializer to match the fields in the encoder
#define ENCODER_INITIALIZER {(CODEC_ERROR)0, 0, 0, 0, FALSE, NULL, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0, 0}

#pragma pack(pop)


#ifdef __cplusplus
extern "C" {
#endif

// Clear data allocated within the encoder and close the log file
void ClearEncoder(ENCODER *encoder);
void ExitEncoder(ENCODER *encoder);

// Compute the size of the encoding buffer required for the specified combination
// of frame dimensions and format, GOP length, and progressive versus interlaced.
// Must keep this routine in sync with the actual amount of scratch buffer space
// used by the encoder.
size_t EncodingBufferSize(int width, int height, int pitch, int format,
						  int gop_length, bool progressive);

// Compute the encoding buffer size forcing the size to be at least as large as a frame
size_t TotalEncodingBufferSize(int width, int height, int pitch, int format,
							   int gop_length, bool progressive);

// Create a scratch buffer for use by the encoder
#if _ALLOCATOR
PIXEL *CreateEncodingBuffer(ALLOCATOR *allocator,
							int width, int height, int pitch, int format,
							int gop_length, bool progressive,
							size_t *allocated_size);
#else
PIXEL *CreateEncodingBuffer(int width, int height, int pitch, int format,
							int gop_length, bool progressive,
							size_t *allocated_size);
#endif

#if _ALLOCATOR
void DeleteEncodingBuffer(ALLOCATOR *allocator, PIXEL *buffer);
#else
void DeleteEncodingBuffer(PIXEL *buffer);
#endif

// Compute statistics required for encoding the lowpass band
//void ComputeLowPassStatistics(ENCODER *encoder, IMAGE *image);

// Intialize an encoder data structure
void InitEncoder(ENCODER *encoder, FILE *logfile, CODESET *cs);

// Routines for setting the encoder state
void SetEncoderParams(ENCODER *encoder, int gop_length, int num_spatial);

void SetEncoderFormat(ENCODER *encoder, int width, int height, int display_height, int format, int encoded_format);

bool SetEncoderColorSpace(ENCODER *encoder, int color_flags);

void SetEncoderQuantization(ENCODER *encoder, int format, int i_fixedquality, int fixedbitrate, custom_quant *custom);

void SetLogfile(ENCODER *state, FILE *file);

// Compute the index for the wavelet transform subband
int SubBandIndex(ENCODER *encoder, int level, int band);

// New routine for creating and initializing an encoder
ENCODER *CreateEncoderWithParameters(ALLOCATOR *allocator, TRANSFORM *transform[], int num_channels,
									 ENCODING_PARAMETERS *parameters);

// Fill in defauilt values for encoding parameters that did not exist in older code
void SetDefaultEncodingParameters(ENCODING_PARAMETERS *parameters);

#if _ALLOCATOR
bool InitializeEncoderWithParameters(ALLOCATOR *allocator,
									 ENCODER *encoder, TRANSFORM *transform[], int num_channels,
									 ENCODING_PARAMETERS *parameters);
#else
bool InitializeEncoderWithParameters(ENCODER *encoder, TRANSFORM *transform[], int num_channels,
									 ENCODING_PARAMETERS *parameters);
#endif

void SetEncoderQuality(ENCODER *encoder, int fixedquality);

// Routines for encoding a stream of video samples
bool EncodeInit(ENCODER *encoder, TRANSFORM *transform[], int num_channels,
				int gop_length, int width, int height, FILE *logfile,
				int fixedquality, int fixedbitrate, int format, int progressive, int flags);

void EncodeRelease(ENCODER *encoder, TRANSFORM *transform[], int num_transforms, BITSTREAM *output);

// Encode one frame of video
bool EncodeSample(ENCODER *encoder, uint8_t *data, int width, int height, int pitch, int format,
				  TRANSFORM *transform[], int num_transforms, BITSTREAM *output,
				  PIXEL *buffer, size_t buffer_size, int fixedquality, int fixedbitrate,
				  uint8_t* pPreviewBuffer, float framerate, custom_quant *custom);

//TODO: Move the encoder metadata functions to a separate file

void AttachMetadata(ENCODER *encoder, METADATA *dst, METADATA *src);
void PreviewDuringEncoding(ENCODER *encoder, TRANSFORM *transform[], int num_transforms, uint8_t *pPreviewBuffer);

#if _RECURSIVE
bool EncodeFirstSampleRecursive(ENCODER *encoder, BITSTREAM *output,
								TRANSFORM *transform[], int num_transforms,
								int width, int height, int format, int input_format);
#endif

bool EncodeFirstSample(ENCODER *encoder, TRANSFORM *transform[], int num_transforms,
					   FRAME *frame, BITSTREAM *output, int input_format);

bool EncodeFirstYUYV(ENCODER *encoder, TRANSFORM *transform[], int num_transforms,
					 uint8_t *frame, FRAME_INFO *info, int pitch, BITSTREAM *output,
					 char *buffer, size_t buffer_size);

// Quantize and encode in one pass
void EncodeQuant(ENCODER *encoder, TRANSFORM *transform[], int num_transforms, BITSTREAM *stream);

// Encode the highpass bands and the lowpass band at the top of the pyramid
void EncodeGroup(ENCODER *encoder, TRANSFORM *transform[], int num_transforms, BITSTREAM *stream);

void EncodeBand(ENCODER *encoder, BITSTREAM *stream, IMAGE *wavelet,
				int band, int subband, int encoding, int quantization);

void EncodeCoeffs(ENCODER *encoder, BITSTREAM *stream, PIXEL *image,
				  int width, int height, int pitch);

void EncodeRuns(ENCODER *encoder, BITSTREAM *stream, PIXEL *image,
				int width, int height, int pitch);

void EncodeFastRuns(ENCODER *encoder, BITSTREAM *stream, PIXEL *image,
					int width, int height, int pitch);

void EncodeLongRuns(ENCODER *encoder, BITSTREAM *stream, PIXEL *image,
					int width, int height, int pitch);

void EncodeByteRuns(ENCODER *encoder, BITSTREAM *stream, PIXEL8S *image,
					int width, int height, int pitch);

void EncodePackedRuns(ENCODER *encoder, BITSTREAM *stream, PIXEL8S *image,
					  int width, int height, int pitch);

void EncodeLowPassBand(ENCODER *encoder, BITSTREAM *stream, IMAGE *wavelet, int channel, int subband);

void EncodeZeroRun(ENCODER *encoder, BITSTREAM *stream, int count);

// Compute the upper levels of the wavelet transform for a group of frames
void ComputeGroupTransformQuant(ENCODER *encoder, TRANSFORM *transform[], int num_transforms);

// Finish the wavelet transform for the group of frames
//void FinishFieldPlusTransformQuant(ENCODER *encoder, TRANSFORM *transform, int channel, int prescale);
void FinishFieldPlusTransformQuant(ENCODER *encoder, TRANSFORM *transform, int channel);

// Finish the wavelet transform for an intra frame group
//void FinishFrameTransformQuant(ENCODER *encoder, TRANSFORM *transform, int channel, int prescale);
void FinishFrameTransformQuant(ENCODER *encoder, TRANSFORM *transform, int channel);

// Set the quantization divisors in the transform wavelets
void SetTransformQuantization(ENCODER *encoder, TRANSFORM *transform, int channel, float framerate);

// Encode highpass coefficients that have been quantized and run length encoded
void EncodeQuantizedRuns(ENCODER *encoder, BITSTREAM *stream, PIXEL *image, int num_runs, int width, int height);

void OverrideEncoderSettings(ENCODER *encoder);
int RemoveHiddenMetadata(unsigned char *ptr, int len);
void UpdateEncoderOverrides(ENCODER *encoder, unsigned char *ptr, int len);


#if _DEBUG

CODEC_ERROR WriteTransformBandFile(TRANSFORM *transform[], int num_transforms,
								   uint32_t channel_mask, uint32_t wavelet_mask,
								   uint32_t wavelet_band_mask, const char *pathname);

CODEC_ERROR CreateEncodedBandFile(ENCODER *encoder, const char *pathname);

CODEC_ERROR CloseEncodedBandFile(ENCODER *encoder);

#endif


#if _THREADED_ENCODER

// Set the handle to the instance of CFEncode
void SetEncoderHandle(ENCODER *encoder, void *handle);

// Encode one frame of video using multiple threads
bool EncodeSampleThreaded(ENCODER *encoder, uint8_t *data, int width, int height, int pitch, int format,
						  TRANSFORM *transform[], int num_transforms, BITSTREAM *output,
						  PIXEL *buffer, size_t buffer_size, int fixedquality, int fixedbitrate);

#endif

#if _HIGHPASS_CODED

// Encode a band of quantized coefficients
void EncodeQuantizedCoefficients(ENCODER *encoder, BITSTREAM *stream, PIXEL *input, int length,
								 int gap, int *runs_count_out, bool output_runs_flag);
#endif

#if _TEST
// Test quantization and encoding using pixel runs
//int32_t TestEncodeQuantizedRuns(uint32_t seed, FILE *logfile);
#endif

#if _AVIFILES

// Utility routines for AVI files
#include <vfw.h>

static __inline bool IsSampleInRange(PAVISTREAM pavi, int32_t index)
{
	return (AVIStreamStart(pavi) <= index && index <= AVIStreamEnd(pavi));
}

LPBITMAPINFOHEADER ReadSample(PGETFRAME pgf, int32_t this_sample);

#endif

#ifdef __cplusplus
}
#endif

#endif
