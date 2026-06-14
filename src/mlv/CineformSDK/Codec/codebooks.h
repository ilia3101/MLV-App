/*! @file codebooks.h

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

#ifndef _CODEBOOKS_H
#define _CODEBOOKS_H

#include "vlc.h"
#include "allocator.h"

//num of codeset used by decoder and encoder
#define CODEC_NUM_CODESETS		3 //DAN20041024

//#define CURRENT_CODESET &cs7	// Codeset that is currently used
//#define CURRENT_CODESET &cs9
#define CURRENT_CODESET cs9
#define SECOND_CODESET cs17
#define THIRD_CODESET cs18


//#define CURRENT_CODESET &cs10
//#define CURRENT_CODESET &cs11
//#define CURRENT_CODESET &cs12 // couldn't get it working


// Forward reference
struct decoder;


// Collection of codebooks that are used by the encoder and decoder.
// The codeset is a consistent set of codebooks for encoding and decoding.
typedef struct codeset {
	char *title;			// Identifying string for the codeset
	VLCBOOK *magsbook;		// Codebook used for magnitudes
	RLCBOOK *zerobook;		// Codebook used for runs of zeros
	uint32_t *tagsbook;		// Codebook used for special codes (band_end)
	RMCBOOK *src_codebook;	// Pair of codebooks used for encoding (from tables)
//	RMCBOOK *codebook;		// Pair of codebooks used for encoding (computed from tables, active)
	RLCBOOK *codebook_runbook;	// Codebook for the run length
	VLCBOOK *codebook_magbook;
	RLVBOOK *runsbook;		// Computed Decoding book for runs and magnitudes
	FLCBOOK *fastbook;		// Computed Lookup table for faster decoding
	VALBOOK *valuebook;		// Computed Fast indexable codebook for signed values
	FSMTABLE *fsm_table;	// Finite state machine
	const FSMARRAY *fsm_array;
	uint32_t flags;
} CODESET;

#ifdef __cplusplus
extern "C" {
#endif

extern CODESET cs18;	// 256 element codebook (optimized for subbands 7,8,9,10,12,15 -- HAAR differenced bands)
extern CODESET cs17;	// 256 element codebook (optimized for subbands 7,8,9,10,12,15 -- HAAR differenced bands)
extern CODESET cs11;	// code tables and FSM, longest zero run is 2880
extern CODESET cs10;	// code tables and FSM, longest zero run is 720
//extern CODESET cs12;	//
extern CODESET cs9;		// code tables automatically generated, compliant with the FSM
extern CODESET cs8;
extern CODESET cs7;		// Codeset with extra codes for bitstream markers
extern CODESET cs6;		// Newer codeset used for encoding and decoding
extern CODESET cs5;		// Older codeset used for encoding and decoding

// The codebook of markers uses extra codes from the codeset
extern VLC markbook[];

#if _ALLOCATOR
bool InitCodebooks(ALLOCATOR *allocator, CODESET *cs);
#else
bool InitCodebooks(CODESET *cs);
#endif

bool InitDecoderFSM(struct decoder *decoder, CODESET *cs);

void FreeCodebooks(struct decoder *decoder /*, CODESET *cs */);

#if 0
void FillRunLengthCodeTable(RLC *input_codes, int input_length, RLC *output_codes, int output_length,
							uint32_t  zero_code, int zero_size);
#else
#if _ALLOCATOR
void ComputeRunLengthCodeTable(ALLOCATOR *allocator,
							   RLE *input_codes, int input_length,
							   RLC *output_codes, int output_length,
							   uint32_t  zero_code, int zero_size);
#else
void ComputeRunLengthCodeTable(RLE *input_codes, int input_length,
							   RLC *output_codes, int output_length,
							   uint32_t  zero_code, int zero_size);
#endif
void SortDecreasingRunLength(RLC *codebook, int length);
void FillRunLengthCodeTable(RLC *codebook, int codebook_length, RLC *table, int table_length);
#endif

void FillCodeLookupTable(RLV *codebook, int length, FLC *table, int size);
int MatchBitPattern(uint32_t  word, int width, RLV *codebook, int length, FLC *match);
void FillScanLookupTable(RLV *codebook, int length, FLC *table, int size);
void FillVlcTable(VLCBOOK *codebook, VLC *table, int size, int flags);
void FillVleTable(VLCBOOK *codebook, VLE *table, int size, int flags);

// Create a finite state machine table from an array of entries
bool FillFSM(struct decoder *decoder, FSMTABLE *fsm_table, const FSMARRAY *fsm_array);

// Initialize a finite state machine to work with the specified table
void InitFSM(FSM *fsm, FSMTABLE *table);

#if _COMPANDING
void ScaleFSM(FSMTABLE *fsm_table);
#endif


#if _TEST

// Print a codebook lookup table
void PrintCodeLookupTable(FILE *logfile, FLC *table, int size);

// Print the codebook used for fast encoding of run lengths
void PrintRunLengthTable(FILE *logfile, RLCBOOK *runsbook);
void PrintFastLookupTable(FILE *logfile, FLCBOOK *fastbook);
void PrintValueCodebook(FILE *logfile, VALBOOK *codebook);

// Routine for testing a code set
BOOL TestCodeSet(CODESET *cs);

#endif

#ifdef __cplusplus
}
#endif

#endif
