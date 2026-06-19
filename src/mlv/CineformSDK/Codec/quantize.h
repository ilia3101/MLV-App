/*! @file quantize.h

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

#ifndef _QUANTIZE_H
#define _QUANTIZE_H

#include "codec.h"
#include "wavelet.h"
//#include "encoder.h"


// Forward reference (to avoid including encoder.h)
//typedef struct encoder ENCODER;
//struct encoder;


#define MAX_QUANT_SUBBANDS	17		// Maximum number of subbands

#define CBFLAG_TABLMASK		0x0f		// value table 1 throu 15 -- 0 use existing value.
#define CBFLAG_DIFFCODE		0x10
#define CBFLAG_PEAKCODE		0x20

typedef struct {
	int magicnumber;
	int quantY[MAX_QUANT_SUBBANDS];
	int quantC[MAX_QUANT_SUBBANDS];
	int codebookflags[MAX_QUANT_SUBBANDS];
} custom_quant;


#define NUM_QUANT_LEVELS		4
#define NUM_QUANT_SUBBANDS		17

#define QUANT_SCALE_FACTOR		2

#define LUMA_QUALITY_DEFAULT	{4, 4,5,5,    4,5,5,    9,8,8,8, 		4,4,4, 		4,4,4}
//#define LUMA_QUALITY_LOW		{4, 16,16,24,	  16,16,24,   9,16,16,40,  	64,64,96, 	64,64,96}	 //red test
#define LUMA_QUALITY_LOW		{4, 8,8,12,	  8,8,12,   9,12,12,16,  	32,32,48, 	32,32,48}	// film grain is blurred -- looks OK.
#define LUMA_QUALITY_MEDIUM		{4, 6,6,8,    6,6,8,    5,8,8,12,  		16,16,24, 	16,16,24}	// this isn't good for film grain, it starts to store it, but fails for realism (waveletty)
#define LUMA_QUALITY_HIGH		{4, 4,4,6,    4,4,6,    5,8,8,8,  		8,8,12, 	8,8,12}		// base level for good film grain reproduction. (level q4 or q5 is prefect -- q6 overkill)
									//h,v,d	  h,v,d	    h,v,d           h,v,d       h,v,d   //h-horizontal,v-vertical,d-diagonal

#define CHROMA_QUALITY_DEFAULT	{4, 4,5,5,    4,5,5,    9,8,8,8, 		8,8,8, 		8,8,8,}
//#define CHROMA_QUALITY_LOW		{4, 16,16,24,	  16,16,24,   16,36,36,40,  	64,64,128, 	64,64,128} //red test
#define CHROMA_QUALITY_LOW		{4, 8,8,12,	  8,8,12,   9,12,12,16,  	32,32,48, 	32,32,48}
#define CHROMA_QUALITY_MEDIUM	{4, 6,6,8,    6,6,8,    5,8,8,12,   	16,16,32, 	16,16,32}
#define CHROMA_QUALITY_HIGH		{4, 6,6,8,    6,6,8,    5,8,8,8,  		8,8,16, 	8,8,16}


// Definitions of quality settings (not used)
enum {
	QUANTIZATION_QUALITY_DEFAULT = 0,
	QUANTIZATION_QUALITY_LOW,
	QUANTIZATION_QUALITY_MEDIUM,
	QUANTIZATION_QUALITY_HIGH
};



// Default quantization parameters

#define DEFAULT_TARGET_BITRATE	16000000
#define DEFAULT_FIXED_QUALITY	0
#define DEFAULT_QUANT_LIMIT		3072
#define DEFAULT_LOWPASS_QUANT	1


// Definition of the quantizer data structure

typedef struct quantizer {
	int TargetBitRate;
	int FixedQuality; //range 1 thru 3 -- selecting quant tables
	int quantLimit;
	int LowPassQuant[CODEC_MAX_CHANNELS];
	int quantLuma[MAX_QUANT_SUBBANDS];
	int quantLumaMAX[MAX_QUANT_SUBBANDS];
	int quantChroma[MAX_QUANT_SUBBANDS];
	int quantChromaMAX[MAX_QUANT_SUBBANDS];
	int overbitrate;
	int progressive;
	int newQuality; //range 1 thru 6
	int midpoint_prequant;
	int FSratelimiter; //0 to 16 switch FS3 to FS2 to FS1 if the data is too high.
	int inputFixedQuality;

	int codebookflags[MAX_QUANT_SUBBANDS];
} QUANTIZER;

#define QUANTIZER_INITIALIZER	{DEFAULT_TARGET_BITRATE, DEFAULT_FIXED_QUALITY, DEFAULT_QUANT_LIMIT, DEFAULT_LOWPASS_QUANT}


#ifdef __cplusplus
extern "C" {
#endif

#if 0
// Table that maps mask to position after a run
extern const unsigned char delta[];
#endif

// Initialize the quantization parameters
void InitQuantizer(QUANTIZER *q);

// Initialize the default quantization tables
//void InitDefaultQuantizer(void);

// Read updated quality table from disk (eventually this should be in a registary I guess)
void QuantizationLoadTables(QUANTIZER *q);

// Set the quantization quality (1: low,  2: medium,  3: high, 0: default)
void QuantizationSetQuality(QUANTIZER *q, int factor, bool progressive, int precision, int goplength, 
							bool ChromaFullRes, FRAME *frame, int64_t lastgopbytes, int video_channels);

// Set the quantization bitrate (8 to 100 Mbps)
void QuantizationSetRate(QUANTIZER *q, int rate, bool progressive, int precision, int goplength, bool ChromaFullRes);

// Quantize a wavelet band using the specified quantization divisor
void QuantizeBand(IMAGE *wavelet, int band, int divisor);

// Quantize a row of 16-bit signed coefficients using inplace computation
void QuantizeRow16s(PIXEL16S *rowptr, int length, int divisor);

// Quantize a row of 8-bit signed coefficients using inplace computation
//void QuantizeRow8s(PIXEL8S *rowptr, int length, int divisor);

// Quantize a row of 16-bit signed coefficients without overwriting the input
void QuantizeRow16sTo16s(PIXEL *input, PIXEL *output, int length, int divisor);

#if 0
// Quantize a row of 16-bit signed coefficients (overwriting the input row) and then
// encode the coefficients using run lengths and entropy codes for the final output
void QuantizeRow16sToCoded(struct encoder *encoder, BITSTREAM *stream, PIXEL *input, int length,
						   int gap, int divisor, int *zero_count_ptr, bool output_runs_flag);
#endif

// Quantize a row of 16-bit signed coefficients and pack to 8 bits
//void QuantizeRow16sTo8s(PIXEL16S *input, PIXEL8S *output, int length, int divisor);

void DequantizeBandRow(PIXEL8S *input, int width, int quantization, PIXEL *output);
void DequantizeBandRow16s(PIXEL16S *input, int width, int quantization, PIXEL16S *output);

#if 0

// Fill a quantization vector with entries from an encoder quant table
// Assume that the transform is a fieldplus transform
void SetWaveletQuantization(ENCODER *encoder, int channel,
							IMAGE *wavelet, int index,
							int quantization[IMAGE_NUM_BANDS]);

// Fill a quantization vector for a frame transform with entries from the
// encoder quant table.  Assume that the transform is a fieldplus transform.
void SetFrameTransformQuantization(ENCODER *encoder, int channel,
								   IMAGE *wavelet, int index,
								   int quantization[IMAGE_NUM_BANDS]);

// Fill a quantization vector for a spatial transform with entries from the
// encoder quant table.  Assume that the transform is a fieldplus transform.
void SetSpatialTransformQuantization(ENCODER *encoder, int channel,
									 IMAGE *wavelet, int index,
									 int quantization[IMAGE_NUM_BANDS]);
#endif

#if (_DEBUG || _TIMING)

// Print the values in a quantizer
void PrintQuantizer(QUANTIZER *q, FILE *logfile);

// Print the prescaling used for the transform wavelets
void PrintTransformPrescale(TRANSFORM *transform, FILE *logfile);

// Print the quantization vectors in the transform wavelets
void PrintTransformQuantization(TRANSFORM *transform, FILE *logfile);

#endif

#if _TEST

// Test quantization of signed byte coefficients
int32_t TestQuantization(unsigned int seed, FILE *logfile);

// Test quantization of a single row of coefficients
int32_t TestQuantizeRow(unsigned int seed, FILE *logfile);

// Test quantization and run length compression of a row of coefficients
int32_t TestQuantizeRow16sToRuns(unsigned int seed, FILE *logfile);

//void DumpEncoderQuant(ENCODER *encoder, int channel);

#endif

#ifdef __cplusplus
}
#endif

#endif
